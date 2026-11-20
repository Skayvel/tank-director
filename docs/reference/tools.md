# 辅助插件参考

控制中心之外的工具插件。它们**不是**控制中心的一部分：独立安装、独立验证、独立文档。

## 一览

| 插件 | 版本 | 命令 | ConVar | 用途 |
|---|---|---|---|---|
| `tankrun_capacity` | 1.3.0 | `sm_tank_capacity_status` | `tankrun_capacity_*` (5) | 服务端补位 Tank，突破原版槽位上限 |
| `tankrun_human_seats` | 1.1.0 | `sm_jg` / `sm_join` | `tankrun_human_seats` | 真人幸存者席位与 Bot 清理 |
| `tankrun_tick_telemetry` | 1.2.0 | `sm_tank_tick` | — | Tick 速率与 Tank 数量采样 |
| `tankrun_spawn_logger` | 0.2.3 | — | — | 出生/死亡/回合事件记录（只读） |
| `tankrun_m08_probe` | 1.0.0 | `sm_m08_begin` / `sm_m08_end` | — | M08 首石归因记录 |
| `tankrun_control` | 0.1.0 | `sm_tankrun_status` | — | M01 只读诊断骨架（历史） |

---

## tankrun_capacity

**注意**：控制中心 v2.2.0 起**内嵌**了同一份源码（`tankdirector_menu.sp:18-20` 用 `#define TR_EMBEDDED_CAPACITY` 引入 `tankrun_capacity.sp`）。内嵌时命令注册与 ConVar 注册被 `#if !defined TR_EMBEDDED_CAPACITY` 排除，因此：

- 装控制中心 → 补位功能可用，但**没有** `sm_tank_capacity_status` 命令，也没有 `tankrun_capacity_*` ConVar。
- **只有独立插件导出 native**：`TankRun_GetLiveReplicas`、`TankRun_TrySpawnReplica`、`TankRun_GetOldestReplicaTime`、`TankRun_RetireOldestReplica`（4 个）。内嵌副本不注册它们，所以控制中心不提供任何插件 API。声明方式与其他限制见 [接口稳定性分级](api/stability.md) 第 5 节。
- 装独立 `tankrun_capacity.smx` → 有上述命令与 ConVar，用于高容量档单独部署。
- **不要同时装两者并都启用补位**，会争夺同一批生成。

| ConVar | 默认 | 范围 | 说明 |
|---|---:|---|---|
| `tankrun_capacity_enabled` | `0` | 0–1 | 启用补位 |
| `tankrun_capacity_total` | `30` | 1–40 | 原版 + 补位合计目标 |
| `tankrun_capacity_rotate` | `1` | 0–2 | 满员时：0 停 / 1 淘汰最早 / 2 淘汰最新 |
| `tankrun_capacity_ramp` | `0` | 0–1 | 压测用：2 秒合并节拍，先填原版 |
| `tankrun_capacity_bridge` | 空 | — | 内部脚本回读通道，勿改 |

`sm_tank_capacity_status` 输出真人幸存者、原版 Tank、补位 Tank、占用槽位与峰值。`peak_with_eight_living_survivors` 只在八名真人同时存活时累计。

**补位 Tank 不是原版 AI**：地图 NAV + 实体碰撞追击、盒形命中、不会出现在以玩家 Tank 为基础的计数器里、不被其它插件当原版 Tank 接管。翻越、爬梯、逐骨骼命中未复刻。

详见 [容量与席位](../user-guide/capacity-and-seats.md)、`docs/tankrun-capacity.md`。

---

## tankrun_human_seats

| ConVar | 默认 | 说明 |
|---|---:|---|
| `tankrun_human_seats` | `1` | 启用真人席位管理 |

- 默认最多 4 名幸存者；服务器提供 `survivor_limit` 时沿用其值；高容量模式提升到 8。
- 真人连入优先沿用游戏自身入队；玩家仍在旁观且有空格时接管一个可用 Bot，或创建临时 Bot 再接管。
- 玩家退出时按 `player_bot_replace` 的 `userid` 踢出对应补位 Bot；闲置 Bot 由每 0.25 秒一次检查清理。
- 已在旁观的玩家可在聊天用 `!jg` / `!join` 申请席位。

日志标记：`[HumanSeats] Survivor handoff confirmed`（同一对 userid 且真人已入队）、`Player reached survivors`（观察到入队）、`Removing departing player's survivor bot`（已提交踢出）。

详见 `docs/tankrun-human-seats.md`。

---

## tankrun_tick_telemetry

无 ConVar、无配置。每秒采样原生 AI Tank 数量，每 5 秒写一行 `logs/tankrun_tick.log`。

字段：

| 字段 | 含义 |
|---|---|
| `effective` | 引擎设定速率 |
| 实际推进速率 | 观察到的 tick 速率 |
| `gap_max_ms` / `gap_p95_upper_ms` | 最长 / p95 上界回调间隔 |
| `native_tanks_min` / `native_tanks_peak` | 窗口内原生 Tank 采样最小/峰值 |
| `actors` | 窗口结束时的补位数量 |
| `total_min` / `total_peak` | 窗口内总数范围 |
| `slow_streak` | 连续低于配置速率 95% 的窗口数 |
| `gap_end_s` | 最长间隔结束时距窗口开始的秒数 |

`sm_tank_tick` 显示最近一次采样。**限制**：这是服务端推进速率，不含客户端帧时间与画面观感。

---

## tankrun_spawn_logger

纯观察插件，无命令、无 ConVar、无行为控制。订阅 `tank_spawn`、`tank_killed`、`player_left_safe_area`、`round_start`、`round_end` 等事件，写自己的日志。

版本演进：0.1.0 → 0.2.0（结构化字段）→ 0.2.1（校验与自检）→ 0.2.2（状态机与配对）→ 0.2.3（生命周期与回合边界）。**0.2.3 是当前工程版本**，历史版本保留在 `scripting/` 与 `build/`。

详见 `docs/tankrun-spawn-logger-v023.md` 及其 diff / protection / tests 子文档。

> 与控制中心**无功能冲突**，但两者都会记录 Tank 生成事件，日志重复属预期。

---

## tankrun_m08_probe

依赖 SDKTools 与 Left4DHooks，**只读**游戏状态与 ConVar，所有可改行为的 Left4DHooks 回调都返回 `Plugin_Continue`。

| 命令 | 参数 | 权限 |
|---|---|---|
| `sm_m08_begin` | `off` / `on` / `fail` | `ADMFLAG_CONFIG` |
| `sm_m08_end` | — | `ADMFLAG_CONFIG` |

三组对照：

| 组别 | 控制中心设置 | 命令 |
|---|---|---|
| 默认对照 | 首石关闭、投石允许 | `sm_m08_begin off` |
| 策略对照 | 首石开启、投石允许 | `sm_m08_begin on` |
| 超时对照 | 首石开启、投石关闭 | `sm_m08_begin fail` |

每组只生成一只新 AI Tank，结束时 `sm_m08_end`。日志 `logs/tankrun_m08_probe.log` 与 `logs/tankrun_control.log` 按墙钟 + 游戏时间 + Tank 槽位对齐。

记录器会拒绝在已有存活 AI Tank 或组别开关不符时开始；`CONFIG_CHANGED` 或 `peak_ai>1` 的组需要单列。

详见 `docs/tankrun-m08-probe.md`。

---

## tankrun_control（历史）

M01 阶段的只读诊断骨架，`sm_tankrun_status`，`ADMFLAG_GENERIC`。只识别引擎/模式和 `mp_gamemode`，不注册 ConVar、不创建实体、不控制行为。

它的能力已被控制中心的 `sm_tank_status` 完全覆盖。保留在仓库是为了历史可追溯，**新部署不需要安装**。详见 `docs/m01-build-and-test.md`。
