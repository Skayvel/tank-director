# 文件与格式参考

## 1. 服务器文件布局

| 文件 | 位置 | 谁生成 | 说明 |
|---|---|---|---|
| 插件二进制 | `addons/sourcemod/plugins/tankdirector_menu.smx` | 发布包 | 唯一需要安装的插件文件 |
| gamedata 签名 | `addons/sourcemod/gamedata/tankrun_firstrock.txt` | 发布包 | DHooks 原生 detour 的位置来源 |
| 运行日志 | `addons/sourcemod/logs/tankrun_control.log` | 插件 | 追加写入，含事件与决策记录 |
| 诊断快照 | `addons/sourcemod/logs/tankrun_control_dump.txt` | `sm_tank_dump` | 覆盖写入 |
| 设置持久化 | `addons/sourcemod/configs/tankrun_control_settings.cfg` | 插件 | KeyValues，保存菜单持久设置 |
| 原始值记录 | `addons/sourcemod/configs/tankrun_control_originals.cfg` | 插件 | KeyValues，首次修改前的原值 |
| 预设槽 1–8 | `addons/sourcemod/configs/tankrun_control_presetN.cfg` | `sm_tank_preset save` | KeyValues，每槽一个文件 |

路径由 `BuildPath(Path_SM, ...)` 拼接（`tankdirector_menu.sp:43-46`、`tr_status.inc:128`、`tr_core.inc:1051`），因此实际根目录随 SourceMod 安装位置变化。

## 2. 配置文件格式

三个 `.cfg` 都是**纯文本 `键=值` 行格式**，不是 KeyValues，也不是控制台命令脚本——**不能用 `exec` 执行**。

读取规则（`tr_core.inc:833-869`）：

- 每行 `键=值`，两侧空白会被裁剪。
- `#` 开头为注释；唯一例外是 `#name=` 前缀（预设名称专用）。
- `;` 开头也当作注释。
- 没有 `=` 的行忽略。
- 未知键会被读进 map，但应用时找不到对应参数就跳过。

写入规则（`tr_core.inc:871-894`）：首行固定写 `# Tank Run 综合控制中心 - 自动生成，请勿手工编辑`，随后每键一行。

> 函数名虽然叫 `TR_WriteKeyValueFile`，**格式不是 Valve KeyValues**。示例文件见 `configs/tankrun_control_settings.example.cfg`。

### 2.1 `tankrun_control_settings.cfg`

保存「用户主动保存」的设置，换图/重载后重新应用。内容包含：

- 5 个全局开关：`tankrun_enabled`、`tankrun_public_readonly`、`tankrun_debug`、`tankrun_log_events`、`tankrun_conflict_mode`；
- 参数表里 `TRK_PLUGIN` 类的插件参数。

引擎参数与脚本参数**不写入**（`M07-01` 报告已核对这一点）。玩家离开不会触发清空。

### 2.2 `tankrun_control_originals.cfg`

记录每个参数**首次被本插件修改前**的值，供 `sm_tank_restore` 使用。

- 删掉它 = 失去恢复能力，只能手工设回。
- 它不会因为恢复操作而清空，因此「恢复初始值」可以反复执行。

### 2.3 `tankrun_control_presetN.cfg`

单个预设槽。额外用一个 `#name=<名称>` 行保存槽名（`tr_core.inc:1059`）。`sm_tank_preset load` 会逐项校验范围，越界项跳过并报告成功条数。

## 3. 运行日志格式

`logs/tankrun_control.log`，每行：

```
[<游戏时间>] <消息>
[<游戏时间>] EVENT <事件消息>
```

- 前缀 `[%.2f]` 是 `GetGameTime()`（秒，地图开始计时）。
- `EVENT` 前缀表示受 `tankrun_log_events` 控制的事件记录（`tr_core.inc:223`、`238`）。
- 普通记录（不受事件开关影响）保留关键决策，例如首石窗口关闭原因。

### 常见日志行（按功能）

| 类别 | 示例文本 | 用途 |
|---|---|---|
| 加载 | `插件已加载 v2.2.11：注册参数 81 项，日志 ...` | 确认实际加载的版本 |
| 依赖 | `依赖探测（静态）：Left4DHooks=可用 SDKHooks=可用 地图运行=是` | 排障第一步 |
| 首石入口 | `严格首石拳击入口：已启用原生 CTankClaw::PrimaryAttack 拦截` / `...签名或扩展不可用，使用输入、动画和命中保护` | 判断 detour 是否装上 |
| 首石窗口 | `首石等待落地：Tank#N userid=M` / `首石落地恢复：...` | 攀跳时序 |
| 首石成功 | `严格首石离手：Tank#N userid=M rock=R，解除禁拳` | 唯一成功判据 |
| 首石失败 | `严格首石超时：Tank#N userid=M 请求次数=K，恢复近战` | 失败分支 |
| 禁拳拦截 | `严格首石禁拳：Tank#N userid=M 入口=<武器入口\|出拳动画\|主攻击输入\|拳击击退>` | 每 Tank 只记首条 |
| 投石 | `投石激活：ability=A tank=T` / `投石被拦截：总开关关闭` / `投石被拦截：插件最小间隔` | 门控效果 |
| 投石离手 | `投石释放前：...` / `投石释放后：... entref=... model=...` | 速度与实体关联 |
| 立即碰撞 | `投石离手后立即碰撞：Tank=T rock=R 耗时=0.0xx 秒` | 出手即撞墙 |
| 拳击 | `拳击挥击开始：...` / `拳击命中：Tank#N → 玩家#M` | 拳击时序 |
| 目标 | `目标选择 forward：Tank#N 当前目标=X 策略=S` / `目标策略覆盖：Tank#N 目标 A → B` | 策略是否生效 |
| 生成 | `命令生成：发起者=... 方式=... 结果=...` | 手动生成 |
| 满员轮换 | `满员轮换：...` / `满员轮换补位：...` | 轮换链路 |
| 参数 | `脚本字段 <名> 写入 <值> 成功` / `写入未生效：请求 A，实际 B` | 脚本字段校验 |
| VScript | `VScript 探测失败：...` / `VScript：当前没有运行中的地图，已拒绝创建 logic_script` | 桥故障 |

> 具体措辞以你服务器上的日志为准。本表用于**定位**，不用于字符串精确匹配的自动化脚本。项目自带的检查器是给生成记录器日志用的（见 [工具参考](../tools.md)），不解析本日志。

## 4. 诊断快照格式

`sm_tank_dump` 生成纯文本，覆盖写入。内容分块：

1. 头部：插件版本、时间、地图、模式、难度、tick 间隔与 tick 率
2. 依赖：Left4DHooks / SDKHooks / VScript 状态
3. 脚本字段段：`SpawnInterval`、`cm_TankLimit`、`DoubleTanks`、`SpawnTankThink`、`TankHealth`
4. 槽位段：`max_clients`、已连接、空闲、保留
5. 参数表：每项的名称、当前值、原始值、写入次数、备注
6. 每 Tank 段：客户端编号、userid、血量、投石/拳击统计、最近事件
7. 累计统计

定位示例（来自历史诊断）：

- 第 9、14–16 行：脚本上限与插件自动生成状态
- 第 27、28 行：`tank_swing_range`、`tank_attack_range` 的写入记录

行号会随参数个数变化，**不要写死行号**，按块标题查找。

## 5. 与生成记录器日志的关系

`tankrun_spawn_logger_*.smx` 写自己的日志（`logs/tankrun_spawn_v0xx.log`），与控制中心日志是两套独立文件。两者可以按墙钟 + 游戏时间 + Tank 槽位对齐，但**格式不同、不能互解析**。

如果要同时分析，建议顺序：

1. 先用控制中心日志定位「哪只 Tank、什么时间、发生了什么」；
2. 再用记录器日志确认「出生/死亡/回合边界」；
3. 用 `tankrun_tick.log`（Tick Telemetry）排除服务端掉速。

## 6. 文件操作的注意事项

- **不要手改正在使用的 `.cfg`**：插件在写回时不读你的编辑，可能在下次保存时覆盖。
- **不要在服务器运行中删除 `originals.cfg`**：会失去当前进程内的恢复能力。
- **日志不会自动轮转**。长时间运行会持续增长，历史记录里出现过单文件 108,709 行以上、单秒 314 行的量级。需要时手工归档。
