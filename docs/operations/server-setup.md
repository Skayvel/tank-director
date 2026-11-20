# 开服配置

## 1. 最小可用配置

```
left4dead2/
├── addons/metamod/                     (MetaMod:Source)
├── addons/sourcemod/
│   ├── plugins/
│   │   ├── left4dhooks.smx            (必需)
│   │   ├── tankdirector_menu.smx      (本插件)
│   │   └── ...
│   └── gamedata/
│       └── tankrun_firstrock.txt       (必需)
└── cfg/server.cfg
```

`server.cfg` 至少需要：

```
mp_gamemode tankrun
```

## 2. Tick 与槽位

| 参数 | 推荐 | 说明 |
|---|---|---|
| tickrate | `100` | 项目全部实测基线。128 未验证 |
| `-tickrate 100` | 启动参数 | |
| `+sv_setmax N` | 18–31 | 由 L4DToolZ 提供；**31 是推荐上限** |
| `sv_maxplayers` | ≤ 真人席位 | 真人数上限，与 `sv_setmax` 是两件事 |

**读回检查**：

```
sm_tank_status
```

看「客户端槽位：已连接/总数」。分母必须等于你设的 `sv_setmax`。

## 3. 与插件相关的服务器配置

建议写进 `server.cfg`（或模式专用 cfg），而不是靠菜单临时改：

```
// 生成
sm_cvar tankrun_auto_spawn 1
sm_cvar tankrun_auto_interval 2.0
sm_cvar tankrun_auto_limit 30
sm_cvar tankrun_rotate_oldest 1
sm_cvar tankrun_spawn_visibility 0
sm_cvar tankrun_spawn_min_dist 220
sm_cvar tankrun_spawn_max_dist 3000

// 首石
sm_cvar tankrun_firstrock_enabled 1
sm_cvar tankrun_firstrock_timeout 3.0
sm_cvar tankrun_firstrock_attempts 2

// 难度
sm_cvar tankrun_default_expert 0    // 想沿用开图难度时
```

**注意**：插件在换图/重载后会重新应用自己在 `configs/tankrun_control_settings.cfg` 里保存的值。如果配置文件与 `server.cfg` 冲突，以**后执行的**为准。建议只用一种来源，不要两边都写。

## 4. 高容量配置

参考 `configs/server_tankrun_capacity.cfg`：

```
exec server_100tick.cfg
sv_maxplayers 8
sv_visiblemaxplayers 8
sm_cvar tankrun_capacity_enabled 1
sm_cvar tankrun_human_seats 1
sm_cvar tankrun_auto_spawn 0
sm_cvar tankrun_firstrock_enabled 1
```

要点：

- `tankrun_auto_spawn 0` 把生成交给原版脚本 + 容量插件的 2 秒节拍。
- 关掉控制中心的统一调度，避免与容量插件争夺生成。
- `sv_maxplayers 8` 与 `sv_setmax 31` 配合。

## 5. 本地测试配置

| 配置 | 用途 | 特点 |
|---|---|---|
| `configs/tankrun_local_firstrock.cfg` | 单只 Tank 首石隔离测试 | 脚本数量门 1、双 Tank 关、10 秒窗口、5 次尝试、事件日志关 |
| `configs/tankrun_local_pressure.cfg` | 30 只压测 | 容量插件 2 秒 ramp、`capacity_rotate 0`、Tick 采样 2 秒窗口 |
| `configs/tankrun_control_settings.example.cfg` | 设置文件格式示例 | 不是自动读取的文件 |

## 6. 启动脚本

`tools/` 下：

| 脚本 | 用途 |
|---|---|
| `start_tankrun_100tick_30tanks_8players.bat` | 31 槽 + 8 席位 + 1 交接 + 30 Tank |
| `start_tankrun_100tick_capacity.bat` | 100 tick，启动时选 18 / 24 / 28 / 31 |
| `start_tankrun_local_firstrock.bat` | 本地首石复测（无边框窗口、`engine_no_focus_sleep 0`） |
| `start_tankrun_local_pressure.bat` | 本地压测 |
| `start_tankrun_steam_slots.bat` | 正式版槽位选择（放在游戏目录） |

**启动器只设置参数，不保证生效。** 每次启动后都要读回 `MaxClients`。

## 7. 服务器安全边界

- 控制台（`client == 0`）不受权限位限制。**RCON 密码要强**。
- 普通玩家默认只能 `!tank`（若开只读）、`sm_tank_help`、`!zs`。
- `!zs` 让玩家自杀，只作用于自己。若不需要，用 override 关掉 `sm_zs`。

## 8. 日志与磁盘

- `logs/tankrun_control.log` 不自动轮转。高事件量下增长很快（历史记录出现过单文件 10 万行以上）。
- 建议加定期归档或把 `tankrun_log_events` 设为 `0`（只保留关键决策记录）。
- `logs/tankrun_control_dump.txt` 是覆盖写入，可以频繁调用。
