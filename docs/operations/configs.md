# 配置样例说明

`configs/` 下的四个文件各自是什么，能改什么，不能改什么。

## 一览

| 文件 | 类型 | 是否被插件自动读取 | 用途 |
|---|---|---|---|
| `tankrun_control_settings.example.cfg` | 示例 | **否** | 展示设置文件格式 |
| `server_tankrun_capacity.cfg` | 服务器配置 | 由 `exec` 加载 | 高容量档的服务器参数 |
| `tankrun_local_firstrock.cfg` | 服务器配置 | 由 `exec` 加载 | 本地首石隔离测试 |
| `tankrun_local_pressure.cfg` | 服务器配置 | 由 `exec` 加载 | 本地 30 只压测 |

## tankrun_control_settings.example.cfg

**这是示例，不会被自动读取。** 插件实际读取的是 `addons/sourcemod/configs/tankrun_control_settings.cfg`（游戏内改参数后自动生成）。

要用它作为起点：

1. 复制并改名为 `configs/tankrun_control_settings.cfg`（放在 SourceMod 的 `configs/` 目录）。
2. `sm_tank_reload` 或换图。
3. 未知键会被忽略。

内容分四组：全局开关（6 项）、M02/M03 生成与位置、M04/M08 目标与首石、M05–M09 投石/拳击/生命。

**格式**：每行 `参数名=数值`；`#` 开头是注释。

注意示例里 `tankrun_firstrock_enabled=0` 和 `tankrun_auto_spawn=0` —— 这是保守起点（手动生成、首石关闭），不是推荐生产值。

## server_tankrun_capacity.cfg

高容量专用。要点：

- `exec server_100tick.cfg` —— 依赖你的服务器上有这个文件。
- `sv_maxplayers 8` / `sv_visiblemaxplayers 8` —— 8 个真人席位。
- `tankrun_capacity_enabled 1` —— 启用容量插件（独立插件，不是控制中心内嵌）。
- `tankrun_human_seats 1` —— 启用真人席位插件。
- `tankrun_auto_spawn 0` —— **关掉控制中心的统一调度**，避免与容量插件抢生成。

配合 `start_tankrun_100tick_30tanks_8players.bat` 使用。

## tankrun_local_firstrock.cfg

单只 Tank 的首石隔离测试。

| 设置 | 值 | 为什么 |
|---|---|---|
| `tankrun_auto_spawn` | 0 | 不自动生成，手动一只 |
| `tankrun_rotate_oldest` | 0 | 不轮换 |
| `tankrun_firstrock_timeout` | 10 | 拉长窗口，便于观察完整序列 |
| `tankrun_firstrock_attempts` | 5 | 允许更多重试 |
| `tankrun_log_events` | 未关闭 | 需要事件记录 |
| `tankrun_script_tank_limit` | 1 | 脚本数量门只允许 1 只 |
| `tankrun_script_double_tanks` | 0 | 关双 Tank |

配合 `start_tankrun_local_firstrock.bat`。

## tankrun_local_pressure.cfg

本地 30 只压测。

| 设置 | 值 | 为什么 |
|---|---|---|
| `tankrun_auto_spawn` | 0 | 交给容量插件的 2 秒 ramp |
| `tankrun_capacity_enabled` | 1 | 启用补位 |
| `tankrun_capacity_total` | 30 | 目标 30 只 |
| `tankrun_capacity_rotate` | 0 | 压测期间不轮换，数量稳定 |
| `tankrun_capacity_ramp` | 1 | 2 秒合并节拍，先填原版槽位 |
| `tankrun_log_events` | 0 | 关事件日志，避免 I/O 干扰 |
| `tankrun_tick_window` | 2 | Tick 采样 2 秒窗口 |

配合 `start_tankrun_local_pressure.bat`。用 `sm_tank_capacity_status` 看实际数量。

## 修改建议

- **不要直接改这些样例文件**当作你的生产配置；复制出去再改。
- 生产环境优先把参数写进 `server.cfg`，避免与插件保存的设置文件冲突（见 [开服配置](server-setup.md) 第 3 节）。
- 改 `tankrun_capacity_*` 前先确认装的是**独立**容量插件；控制中心内嵌版没有这些 ConVar。
