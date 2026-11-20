# 命令参考

控制中心 v2.2.8 注册的全部 17 个命令。**每一条都对应源码 `scripting/tankrun/tr_diag.inc:420-442`。**

## 权限模型

| 层 | 判定 | 说明 |
|---|---|---|
| 控制台（`client == 0`） | 直接放行 | 服务器控制台与已认证 RCON 不受权限位限制（`tr_diag.inc:8-11`） |
| 一般管理员 | `ADMFLAG_GENERIC`（`b`） | `sm_tank_status` / `_diag` / `_dump` / `_params` |
| 配置管理员 | `ADMFLAG_CONFIG`（`i`） | 所有写操作 |
| 普通玩家 | 无权限 | 只有 `!tank`（只读模式）、`sm_tank_help`、`!zs` |

> **覆盖（override）细节**：非控制台调用走 `CheckCommandAccess(client, "sm_tank", flags)`（`tr_diag.inc:13`）。也就是说，为 `sm_tank` 配置的 SourceMod override 会影响**所有**控制中心命令的权限判定，不只是菜单本身。这是实现现状，不是文档承诺。

## 菜单

| 命令 | 注册方式 | 权限 | 说明 |
|---|---|---|---|
| `sm_tank` | `RegConsoleCmd` | 回调内判定 | 打开主菜单 |
| `sm_tankmenu` | `RegConsoleCmd` | 同上 | `sm_tank` 的别名 |

两个命令都是 `RegConsoleCmd`，权限在 `Command_TankMenu` 内部判断，**不是** `RegAdminCmd`。因此：

- 控制台调用会得到「控制台无法显示菜单；请使用 sm_tank_status / sm_tank_set 等命令」。
- 普通玩家默认被拒绝；`tankrun_public_readonly 1` 时普通玩家可打开**只读**菜单，所有修改项禁用（`tr_core.inc:246-253`）。

## 只读诊断

| 命令 | 参数 | 权限 | 输出 |
|---|---|---|---|
| `sm_tank_status` | 无 | `ADMFLAG_GENERIC` | 聊天栏多行状态：版本、模式、地图、难度、tick 间隔、依赖、Tank 计数、统一生成、刷点难度、槽位、首石、累计统计、参数写入计数 |
| `sm_tank_diag` | 无 | `ADMFLAG_GENERIC` | 重新探测依赖 + 状态摘要 + 写日志 |
| `sm_tank_dump` | 无 | `ADMFLAG_GENERIC` | 导出 `logs/tankrun_control_dump.txt` |
| `sm_tank_params` | 无 | `ADMFLAG_GENERIC` | 逐行列出全部 57 项参数：`名称 = 值（标签，分区 N）`；不可读的显示 `unavailable` |
| `sm_tank_help` | 无 | 所有人 | 命令帮助（6 行） |

`sm_tank_params` 输出示例：

```
tankrun_auto_interval = 2.0000（自动生成间隔（秒），分区 0）
tankrun_script_tank_limit = 8.0000（脚本数量门（只），分区 6）
```

值是 `%.4f` 格式，**整数参数也带小数**，不要因此以为类型是浮点。

## 写操作

### `sm_tank_set`

```
sm_tank_set <参数名> <数值>
```

权限 `ADMFLAG_CONFIG`。参数名必须是 `sm_tank_params` 里出现过的 57 项之一。

行为：
1. 未知参数 → `未知参数：X（使用 sm_tank_params 查看已注册参数）`
2. 非数字 → `数值无效：X`
3. 超出允许范围 → 自动夹到边界并提示 `数值按范围校正：A → B`
4. 生成间隔类参数必须 `> 0`，否则 → `数值无效：生成间隔必须大于 0，数值必须有限`
5. 写入后重新读回：成功 → `X 已写入，实际读回 Y`；失败 → `X 写入失败，实际 Y（检查模式覆盖/脚本状态/冲突模式）`

**不要用 `sm_cvar` 修改 `tankrun_*` 参数**再期待插件识别：绕过 `sm_tank_set` 的写入不会进入插件记录的「上次写入值」，可能触发冲突检测，也不会被记录为主动变更。

### `sm_tank_restore`

```
sm_tank_restore <参数名|all>
```

权限 `ADMFLAG_CONFIG`。恢复到 `configs/tankrun_control_originals.cfg` 记录的初始值。

- `all` → `已恢复全部已记录初始值的引擎参数`
  注意提示语只说「引擎参数」，但实现会对所有可恢复项执行。
- 单项失败 → `X 无法恢复（脚本字段/参数不可用/插件关闭）`

## 生成与清理

### `sm_tank_spawn`

```
sm_tank_spawn [native|band|aim|script|vscript|near]
```

权限 `ADMFLAG_CONFIG`。不带参数时使用 `tankrun_spawn_mode` 当前值。

| 参数 | 对应模式 | 含义 |
|---|---|---|
| `native` | `TRSP_NATIVE` | 原生随机区域 |
| `band` | `TRSP_BAND` | 距离带（`tankrun_spawn_min_dist`–`max_dist`） |
| `aim` | `TRSP_AIM` | 我的准星位置 |
| `script` / `vscript` | `TRSP_VSCRIPT` | Tank Run 脚本 ZSpawn |
| `near` | `TRSP_NEAR` | 幸存者附近原生选位 |

未知参数 → `未知生成方式：X（可用 native/band/aim/script/near）`。
`vscript` 是 `script` 的别名，文档旧版只写了 `script`。

### `sm_tank_clear`

```
sm_tank_clear
```

权限 `ADMFLAG_CONFIG`。清理全部 AI Tank（**含补位实体**）。返回 `已清理 N 只 AI Tank`。

### `sm_tank_kill`

```
sm_tank_kill <Tank客户端编号|oldest|newest>
```

权限 `ADMFLAG_CONFIG`。三种目标：

- `oldest`：最早出生的 AI Tank
- `newest`：最后出生的 AI Tank
- 数字：客户端编号（1..MaxClients），必须是存活 AI Tank

失败 → `没有对应的存活 AI Tank，或出生顺序不可确认。可从 Tank 列表选择具体编号。`
最后一句很重要：**热加载前已在场的 Tank 出生顺序不可确认**，此时 `oldest`/`newest` 会被拒绝，只能指定编号。

## 预设

```
sm_tank_preset <save|load|delete|list> [槽位] [名称]
```

权限 `ADMFLAG_CONFIG`。8 个槽位（1–8），对应 `configs/tankrun_control_preset1.cfg` … `preset8.cfg`。

| 子命令 | 参数 | 行为 |
|---|---|---|
| `list` | — | 列出 8 个槽位的占用与名称 |
| `save` | 槽位 [名称] | 把当前全部参数写入槽位；省略名称时用 `预设N` |
| `load` | 槽位 | 逐项应用，越界项跳过并报告成功条数：`槽位 N 应用完成，成功 M 项` |
| `delete` | 槽位 | 删除槽位文件 |

## 日志开关

| 命令 | 参数 | 权限 | 行为 |
|---|---|---|---|
| `sm_tank_debug` | `0/1`（可省略） | `ADMFLAG_CONFIG` | 设置 `tankrun_debug`；省略参数时只回显当前值 |
| `sm_tank_log` | `0/1`（可省略） | `ADMFLAG_CONFIG` | 设置 `tankrun_log_events`；回显当前值 + 日志文件路径 |

## 重载设置

```
sm_tank_reload
```

权限 `ADMFLAG_CONFIG`。重新读取设置文件、初始值文件，重新探测依赖，并确保自动生成定时器存在。返回 `已重新加载设置/初始值并重新探测依赖：<脚本状态>`。

**不重载 SMX 本身。** 要重载插件请用 `sm plugins reload tankdirector_menu`。

## 玩家自杀

| 命令 | 权限 | 行为 |
|---|---|---|
| `sm_zs` | 所有人 | 只对发起者生效，**只在无参数、插件功能开启、自己存活时**执行 `ForcePlayerSuicide`（`tr_diag.inc:413-418`） |

游戏内可写 `!zs` 或 `/zs`。这两个前缀能工作是因为 SourceMod 会把聊天里的 `!cmd` / `/cmd` 映射到 `sm_cmd`，插件并没有注册 `!zs` 这个名字。

## 聊天命令前缀说明

SourceMod 的约定：玩家在聊天框输入 `!tank` 或 `/tank`，引擎转成控制台命令 `sm_tank`。所以：

- 文档里写 `!tank` 是给玩家的说法；
- 控制台/RCON 必须写 `sm_tank`。

这条规则适用于本文所有 `sm_*` 命令，但 `!zs` 例外地只有 `sm_zs` 注册（即 `!zs` ↔ `sm_zs`）。

## 不属于本插件的命令（容易误用）

以下命令**不由控制中心注册**。它们属于其它插件，只有装了对应插件才存在；在只有控制中心的环境里执行会得到「未知命令」。

| 命令 | 实际归属 |
|---|---|
| `sm_tank_tick` | `tankrun_tick_telemetry.sp:52`（Tick 采样插件） |
| `sm_tankrun_status` | `tankrun_control.sp:25`（M01 只读诊断骨架） |
| `sm_tank_capacity_status` | `tankrun_capacity.sp:192`（独立容量插件） |
| `sm_m08_begin` / `sm_m08_end` | `tankrun_m08_probe.sp:74-75`（M08 记录器） |
| `sm_jg` / `sm_join` | `tankrun_human_seats.sp:74-75`（真人席位插件） |

`docs/tankrun-control-center.md` 第 356 行提到「用 `sm_tank_tick` 记录无 Tank 基线」，那是与 Tick Telemetry 插件配合使用的场景，不是控制中心自带命令。
