# VScript 桥接参考

控制中心通过**临时创建一个 `logic_script` 实体、执行一段固定代码、立刻删除实体**的方式，读写官方 Tank Run 模式脚本的会话字段。本文记录这套桥的实际协议，便于二次开发和排障。

**这套机制不是公开 API。** 它依赖模式脚本里存在 `::SessionState` / `::SessionOptions` 全局表，以及这几个字段名。Valve 或社区更新脚本后字段可能变化，届时桥会返回「模式脚本未提供 SessionState/SessionOptions」。

## 通道

| 项 | 值 |
|---|---|
| 回读通道 | ConVar `tankrun_bridge`（`FCVAR_DONTRECORD`，默认空字符串） |
| 通道名宏 | `TR_BRIDGE`（`tr_core.inc:20`） |
| 执行实体 | `logic_script`，`RunScriptCode` 输入 |
| 代码包装 | `try { <body> } catch(e) { Convars.SetValue("tankrun_bridge", "ERR"); }`（`tr_core.inc:1248-1249`） |
| 实体生命周期 | `CreateEntityByName` → `DispatchSpawn` → `AcceptEntityInput("RunScriptCode")` → `RemoveEntity`（`tr_core.inc:1227-1256`） |

**不要手工修改 `tankrun_bridge`。** 它被用作同步回读缓冲区，写入会破坏校验。

## 探测协议

探测代码（`tr_core.inc:1181-1187`）：

```lua
local s = ::SessionState; local o = ::SessionOptions;
if (("SpawnInterval" in s) && ("cm_TankLimit" in o)) {
  Convars.SetValue("tankrun_bridge", format("V|%.9g|%d|%d|%d|%d",
    s.SpawnInterval.tofloat(),
    o.cm_TankLimit.tointeger(),
    (("DoubleTanks" in s) ? (s.DoubleTanks ? 1 : 0) : 0),
    (("SpawnTankThink" in s) ? (s.SpawnTankThink ? 1 : 0) : -1),
    (("TankHealth" in s) ? s.TankHealth.tointeger() : 0)));
} else {
  Convars.SetValue("tankrun_bridge", "NOSCRIPT");
}
```

返回格式：

```
V|<SpawnInterval>|<cm_TankLimit>|<DoubleTanks>|<SpawnTankThink>|<TankHealth>
```

用 `%.9g` 而不是 `%.2f`，是为了保留 `SpawnInterval` 的小数位（例如 `2.5` 不被截成 `2.50`）。

## 返回值与状态映射

| 返回值 | `g_sVStatus` | 含义 |
|---|---|---|
| `V|...` 六段 | `已连接 Tank Run 模式脚本` | 正常 |
| `NOSCRIPT` | `模式脚本未提供 SessionState/SessionOptions` | 模式不是 Tank Run，或脚本未加载 |
| `ERR` 或空 | `脚本执行失败（详见日志）` | 代码抛异常，日志有 `VScript 探测失败：...` |
| 段数 < 6 或首段非 `V` | `脚本返回值格式不匹配` | 通道被别的插件/人写过 |
| 空返回 | `脚本桥无返回值（logic_script 不可用？）` | 实体创建或执行失败 |
| 地图未就绪 | `地图/实体系统未就绪，脚本探测已推迟` | 正常保护，换图后再查 |
| 非 tankrun 模式 | `当前模式不是 tankrun` | 模式判定未通过 |

## 六个字段（含接管用的一个）

| 字段 | 位置 | 类型 | 插件参数 | 说明 |
|---|---|---|---|---|
| `SessionState.SpawnInterval` | `SessionState` | float | `tankrun_script_spawn_interval` | 脚本自动生成间隔（秒）。**不是投石间隔** |
| `SessionOptions.cm_TankLimit` | `SessionOptions` | int | `tankrun_script_tank_limit` | 模式数量门；`0` 暂停阶段生成。统一调度接管期间被改写为 `tankrun_auto_limit` |
| `SessionState.DoubleTanks` | `SessionState` | bool | `tankrun_script_double_tanks` | 每批双 Tank |
| `SessionState.SpawnTankThink` | `SessionState` | bool | *（无直接参数）* | 原版脚本的自动生成开关。插件接管时写 `false`，卸载或关闭自动生成时恢复 `true` |
| `SessionState.TankHealth` | `SessionState` | int | `tankrun_script_tank_health` | 模式在 `tank_spawn` 时应用的 Tank 血量 |
| `SessionState.TanksDisabled` | `SessionState` | bool | *（只读）* | 原版脚本的阶段禁用标志。只读，插件不改它 |

`SpawnTankThink` 的探测值 `-1` 表示字段不存在，与 `false`（`0`）区分开。

**为什么插件要接管 `SpawnTankThink`**：原版脚本会在安全区、终局、阶段切换等时机把自己的开关重新打开，与插件的统一调度冲突。因此插件在接管时写 `false`，并每 5 秒低频校正一次（`docs/tankrun-control-center.md` 第 197 行）。

## 第二套协议：阶段读取与接管

除探测外，桥还有一对「接管 / 交还」调用，用于把原版脚本的自动生成换成插件的统一调度。

### 接管：`TR_VScriptReadFrontPhase()`（`tr_core.inc:1362-1383`）

```lua
local s = ::SessionState; local o = ::SessionOptions;
if (s.TanksDisabled || o.cm_TankLimit <= 0) {
  Convars.SetValue("tankrun_bridge", "PAUSE");
} else {
  local old = o.cm_TankLimit; local wasOn = s.SpawnTankThink;
  o.cm_TankLimit = <tankrun_auto_limit>;
  s.SpawnTankThink = false;
  Convars.SetValue("tankrun_bridge", format("OPEN|%d|%d", old.tointeger(), wasOn ? 1 : 0));
}
```

| 返回 | 含义 | 副作用 |
|---|---|---|
| `PAUSE` | `SessionState.TanksDisabled` 为真，或 `cm_TankLimit <= 0` | 无。插件把阶段显示为「暂停」 |
| `OPEN|<原limit>|<原SpawnTankThink>` | 阶段开放 | **把 `cm_TankLimit` 改写为 `tankrun_auto_limit`，把 `SpawnTankThink` 置 `false`** |
| 其它 | 解析失败 | 无 |

这个调用不只是「读取阶段」，它同时**接管了两个字段**。原值记在 `g_iOriginalStockLimit` 与 `g_bScriptThinkDisabled` 里，供交还时使用。

### 交还：`TR_VScriptReleaseFrontPhase(bool restoreThink)`（`tr_core.inc:1385-1392`）

```lua
if (<原limit> > 0 && o.cm_TankLimit == <插件上次写入值>) { o.cm_TankLimit = <原limit>; }
if (<restoreThink> && !s.TanksDisabled && o.cm_TankLimit > 0) { s.SpawnTankThink = true; }
```

两个保护条件很重要：

- 只有当前 `cm_TankLimit` **仍等于插件上次写入的值**才恢复，避免覆盖别人后来的修改。
- 只有 `restoreThink` 为真且阶段未暂停时才把 `SpawnTankThink` 打开。

## 写入流程

`TR_VScriptSetField`（`tr_core.inc:1292` 起）：

1. 若当前快照无效，先强制重新探测；仍失败则拒绝写入并写日志 `脚本字段 <名字> 写入失败：脚本接口不可用`。
2. 生成设置语句，`Convars.SetValue` 写回通道。
3. 执行后**重新读回整个快照**，只有读回值与请求值匹配才认为生效。

因此菜单里脚本字段的显示永远是「脚本读回值」，不是插件记住的值。

## 与 `tankrun_script_*` 参数的关系

`tankrun_script_*` 是**参数名**，不是 ConVar 名。它们只存在于插件的参数表里（`TRK_VSCRIPT` 类型），实际值来自上面的快照。

- `sm_tank_params` 里它们显示为读回值，读不到时显示 `unavailable`。
- `sm_cvar tankrun_script_spawn_interval` **不会**返回任何东西——这个名字没有注册成 ConVar。
- 要直接看脚本真实值，用 `sm_tank_status` 的脚本状态行，或 `sm_tank_dump` 的脚本字段段。

## 已知限制

1. **每次读都要建实体。** 探测会创建一个 `logic_script` 再删除。插件为此做了防抖：地图未就绪不建、已探测失败不每次重试、同一时间只允许一个延迟探测。
2. **不能证明脚本版本。** 字段存在只说明有这几个名字，不说明是官方 `tankrun.nut`、哪个版本、有没有被服务器覆盖。项目内 `docs/tankrun-control-scope.md` 第 11 行明确记录「目前没有运行时加载来源证明」。
3. **多插件争抢同一字段。** 如果另一个插件也写 `SpawnInterval` / `cm_TankLimit` / `SpawnTankThink`，双方会互相覆盖。本插件每次写后读回校验并写日志，但**不能自动裁决归属**，只能由运维停用其中一方。
4. **Windows 平台限制不适用。** 桥走实体 + `RunScriptCode`，与平台无关；受平台限制的是 gamedata 签名那一处（见 [gamedata](gamedata.md)）。

## 相关源码

| 功能 | 位置 |
|---|---|
| 探测代码生成 | `tr_core.inc:1181-1187` |
| 返回值解析 | `tr_core.inc:1189-1223` |
| 通用执行入口 | `tr_core.inc:1227-1256` |
| 探测调度 | `tr_core.inc:1258-1290` |
| 字段写入 | `tr_core.inc:1292` 起 |
| `SpawnTankThink` 接管与恢复 | `tr_core.inc:1367`、`tr_core.inc:1390` |
