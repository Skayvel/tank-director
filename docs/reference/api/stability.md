# 接口稳定性分级

本页把**所有对外可见的接口**逐项分级，并严格区分四类：Command / ConVar（用户接口）、**本仓库导出的 Native**（给其他 SourceMod 插件用）、**实现的外部回调**（被 Left4DHooks / 引擎调用）、**调用的外部 Native**（依赖别人的东西）。

目的很简单：让二次开发者在改代码前知道，**哪些可以依赖、哪些会变、哪些明天就可能消失**。

- 接口的**行为细节**见 [命令参考](commands.md)、[参数参考](convars.md)、[回调参考](left4dhooks.md)。
- 本页只回答「能不能依赖」。

---

## 1. 分级定义

| 级别 | 含义 | 变更政策 |
|---|---|---|
| **A 稳定** | 接口形状固定；行为由插件自身实现决定，不依赖未验证的引擎路径 | 破坏性变更只在大版本（major）发生，并在 [版本表](../versions.md) 写明 |
| **B 实验性** | 接口存在且可用，但行为依赖未在实服验证的引擎路径，或已知只覆盖部分路径 | 可能在小版本调整语义；升级前请读版本说明 |
| **C 内部** | 实现细节、只读观测、或没有对外契约 | **不保证任何兼容**，随时可变 |

判定所依据的材料：源码行号、编译验证（见 [公开发布检查报告](../../audit/public-release-report.md)）、以及项目自述的验证状态。**分级不等于「已验证」**——A 级只表示接口形状可靠，不代表游戏内行为已被实测。

---

## 2. 统计总览

| 类别 | 总数 | A 稳定 | B 实验性 | C 内部 |
|---|---:|---:|---:|---:|
| Commands | 17 | 16 | 1 | 0 |
| ConVar（含脚本参数与内部桥接） | 64 | 30 | 33 | 1 |
| **导出的 Native**（仅独立 `tankrun_capacity.smx`） | 4 | 0 | 4 | 0 |
| **实现的外部回调**（Left4DHooks 14 + SourceMod 1） | 15 | 3 | 5 | 7 |
| **调用的外部 Native** | 4 | 4 | 0 | 0 |

> ConVar 总计 64 = 6 个全局开关 + 29 个插件参数 + 24 个引擎参数 + 4 个脚本参数 + 1 个内部桥接。
> 其中 `CreateConVar` 实际注册 36 个（35 个 `tankrun_*` + `tankrun_bridge`），其余为 `FindConVar` 绑定的引擎参数与 VScript 字段。

---

## 3. Commands（17）

| 命令 | 级别 | 依据 |
|---|---|---|
| `sm_tank` / `sm_tankmenu` | A | 菜单入口，`RegConsoleCmd` + 内部权限判定；回调行为由插件决定 |
| `sm_tank_help` | A | 固定文本 |
| `sm_tank_status` | A | 纯读取与格式化 |
| `sm_tank_diag` | A | 重新探测 + 写日志 |
| `sm_tank_dump` | A | 覆盖写诊断文件 |
| `sm_tank_params` | A | 遍历参数表 |
| `sm_tank_set` | A | 写入 → 读回 → 回报；失败原因明确 |
| `sm_tank_restore` | A | 读原始值文件 |
| `sm_tank_preset` | A | 8 槽文件读写 |
| `sm_tank_clear` | A | 遍历击杀 AI Tank |
| `sm_tank_kill` | A | 目标三态固定；热加载后拒绝 `oldest`/`newest` 是**已文档化的限制**，不是行为漂移 |
| `sm_tank_debug` / `sm_tank_log` | A | 切换布尔 ConVar |
| `sm_tank_reload` | A | 重读设置 + 重探依赖 |
| `sm_zs`（游戏内 `!zs` / `/zs`） | A | 只对发起者、只在自己存活时生效 |
| `sm_tank_spawn` | **B** | 命令形状固定，但五种生成方式的**选位结果**依赖实验性路径（遮挡判定、NAV 可达、距离段随机） |

---

## 4. ConVar

### 4.1 全局开关（6）— 全部 A

| 名称 | 默认 | 说明 |
|---|---:|---|
| `tankrun_enabled` | 1 | 功能总开关 |
| `tankrun_public_readonly` | 0 | 普通玩家只读菜单 |
| `tankrun_debug` | 0 | 调试输出 |
| `tankrun_log_events` | 1 | 事件日志 |
| `tankrun_conflict_mode` | 0 | 冲突处理策略 |
| `tankrun_default_expert` | 1 | 进图默认专家 |

这 6 个不在参数表里，`sm_tank_params` 不显示；用 `sm_cvar` 或服务器配置设置。

### 4.2 插件参数（29）

**A 稳定（24）** —— 语义由插件实现决定，读写即可确认生效：

| 分组 | 参数 |
|---|---|
| 生成数量 | `tankrun_auto_spawn`、`tankrun_auto_interval`、`tankrun_auto_limit`、`tankrun_rotate_oldest`、`tankrun_auto_double`、`tankrun_spawn_mode` |
| 生成位置 | `tankrun_spawn_visibility`、`tankrun_spawn_min_dist`、`tankrun_spawn_max_dist`、`tankrun_spawn_attempts`、`tankrun_spawn_fallback` |
| 目标/感知 | `tankrun_ai_only`、`tankrun_target_scan` |
| 投石开关 | `tankrun_rock_enabled`、`tankrun_rock_min_delay` |
| 首石计时 | `tankrun_firstrock_timeout`、`tankrun_firstrock_attempts` |
| 生命/伤害 | `tankrun_tank_health`、`tankrun_tank_maxhealth`、`tankrun_mode_health_sync`、`tankrun_tank_damage_scale`、`tankrun_fire_damage_scale`、`tankrun_speed_scale`、`tankrun_claw_damage_scale` |

**B 实验性（5）**：

| 参数 | 为什么是 B |
|---|---|
| `tankrun_target_mode` | 只覆盖经过 `L4D2_OnChooseVictim` 的路径；机枪、接触、命令等路径不受影响 |
| `tankrun_target_fixed` | 同上；且仅在 `target_mode = 4` 时有意义 |
| `tankrun_firstrock_enabled` | 插件能维持窗口与禁拳，但**不能保证引擎接受投石请求**；超时是正常结局 |
| `tankrun_rock_force_scale` | 改写真实释放速度向量；游戏内弹道效果未实服验证 |
| `tankrun_claw_block_hit` | **风险项**：让 `OnPlayerHit_Pre` 返回 `Handled`，只保证阻止击退，不保证不影响其它效果 |

### 4.3 引擎参数（24）— 全部 B

这些是游戏自己的 ConVar，插件只做 `FindConVar` 读写。**接口是稳定的，语义不是我们的**：

| 分组 | 参数 |
|---|---|
| 拳击 | `tank_swing_range`、`tank_attack_range`、`tank_fist_radius`、`tank_swing_interval`、`tank_swing_fast_interval`、`tank_swing_miss_interval`、`tank_windup_time`、`tank_swing_duration`、`z_tank_attack_interval` |
| 投石 | `z_tank_throw_interval`、`tank_throw_min_interval`、`z_tank_throw_fail_interval`、`tank_throw_allow_range`、`z_tank_throw_force`、`tank_throw_lead_time_factor`、`tank_throw_loft_rate`、`tank_throw_max_loft_angle`、`tank_throw_aim_error`、`tank_rock_overhead_percent` |
| 生成/导航 | `z_spawn_range`、`z_spawn_safety_range`、`z_finale_spawn_tank_safety_range`、`nb_allow_avoiding`、`nb_allow_climbing` |

为什么全标 B：

1. ConVar 是否存在、默认值、作用域由游戏构建决定，换构建可能失效（插件会显示「本构建未注册」）。
2. 参数表里带 `verified` 标记的只表示**消费点已静态确认**，不表示游戏内行为已验证。
3. 已知反例：`tank_attack_range` 在普通 AI 起拳分支里**没有被读取**（固定 `50.0`）。

### 4.4 脚本参数（4）— 全部 B

`tankrun_script_spawn_interval`、`tankrun_script_tank_limit`、`tankrun_script_double_tanks`、`tankrun_script_tank_health`

它们不是 ConVar，而是官方 Tank Run 模式脚本的会话字段（`SessionState` / `SessionOptions`）。字段名与语义由模式脚本决定，游戏或社区更新脚本后可能变化。

### 4.5 内部（1）

| 名称 | 为什么是 C |
|---|---|
| `tankrun_bridge` | VScript 同步读回通道。报文格式（`V\|…`、`NOSCRIPT`、`ERR`）是实现细节。**不要读写它** |

---

## 5. 本仓库导出的 Native（唯一给其他插件用的 API）

**控制中心本身不导出任何 native、forward 或库标识。** 全仓库只有一个插件对外提供 API，而且它是独立的 `tankrun_capacity.smx`。

### 5.1 实际导出（4 个）

来源：`tankrun_capacity.sp:92-95`，位于 `#if !defined TR_EMBEDDED_CAPACITY` 块内（`tankrun_capacity.sp:83-126`）。

| Native | 建议声明 | 语义 | 级别 |
|---|---|---|---|
| `TankRun_GetLiveReplicas` | `native int TankRun_GetLiveReplicas();` | 当前存活的补位 Tank 数量 | B |
| `TankRun_TrySpawnReplica` | `native int TankRun_TrySpawnReplica(const float pos[3], const float ang[3]);` | 在指定坐标/角度尝试创建补位 Tank，返回实体索引（失败 0） | B |
| `TankRun_GetOldestReplicaTime` | `native float TankRun_GetOldestReplicaTime();` | 最早补位 Tank 的创建时间，无补位返回 `-1.0` | B |
| `TankRun_RetireOldestReplica` | `native bool TankRun_RetireOldestReplica();` | 移除最早的一只补位 Tank，成功返回 true | B |

**为什么是 B 而不是 A**：函数体稳定，但有三处缺口——

1. **没有公共 `.inc`。** 消费者必须自己写 `native` 声明（`tankrun_tick_telemetry.sp:6` 就是这么做的）。
2. **只在独立插件里注册。** 控制中心内嵌了同一份 `tankrun_capacity.sp`，但那段注册代码被 `#if !defined TR_EMBEDDED_CAPACITY` 排除。装了 v2.2.8 控制中心的服务器上，这些 native **不存在**。
3. **没有库标识。** 全仓库没有 `RegPluginLibrary`，所以其他插件不能用 `LibraryExists()` 探测，只能用 `GetFeatureStatus(FeatureType_Native, "...")`。

### 5.2 消费示例（仓库内唯一实例）

`tankrun_tick_telemetry.sp`：

```sourcepawn
native int TankRun_GetLiveReplicas();                       // 第 6 行：自行声明

public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int maxlen)
{
    MarkNativeAsOptional("TankRun_GetLiveReplicas");        // 第 42 行：可选，未提供时不阻塞加载
    return APLRes_Success;
}

if (GetFeatureStatus(FeatureType_Native, "TankRun_GetLiveReplicas") == FeatureStatus_Available)
    return TankRun_GetLiveReplicas();                       // 第 119-120 行：先探测再调用
```

这段代码就是「如何安全消费本仓库 API」的参考实现：**自行声明 + MarkNativeAsOptional + GetFeatureStatus 探测**。

### 5.3 我方可选依赖的 native（本仓库**不提供**）

| Native | 声明位置 | 提供者 | 影响 |
|---|---|---|---|
| `TankRun_IsManagedCapacityActive` | `tankrun_capacity.sp:22` | **本仓库没有任何插件注册它** | 独立 capacity 插件永远进不了「托管模式」：`TC_ManagedMode()` 在非内嵌分支要求该 native 可用（`tankrun_capacity.sp:256-264`） |

也就是说：**独立容量插件的托管路径目前是死的**，只能用 `tankrun_capacity_enabled 1` 的传统路径。这是已知限制，本次不修（不增加功能）。内嵌在控制中心时走 `TR_ManagedCapacityActive()`（`tr_tank.inc:918`），不受影响。

### 5.4 导出接口的稳定性政策

- 4 个 native 的函数名与参数顺序，若变更会在大版本发生。
- 但在补上公共 `.inc` 与库标识之前，本项目**不承诺**它们是稳定 API。第三方依赖时请按 5.2 的方式做可选调用。

---

## 6. 实现的外部回调（15）

这些不是我们导出的 API，而是我们**实现**别人定义的 forward。分成三类看。

### A 稳定（3）

| Forward | 契约 |
|---|---|
| `L4D_OnCThrowActivate` | 真实拦截点：返回 `Plugin_Handled` 即可阻止投石激活 |
| `L4D_OnSpawnTank` | 统一调度闸门：放行或否决原版创建 |
| `L4D_TankRock_OnRelease_Post` | 首石成功判据：能确认到真实 `tank_rock` 实体 |

### B 实验性（5）

| Forward | 为什么是 B |
|---|---|
| `L4D2_OnChooseVictim` | 只覆盖普通选择函数路径 |
| `L4D_TankRock_OnRelease` | 改写释放速度向量；效果未实服验证 |
| `L4D_TankClaw_OnPlayerHit_Pre` | 阻止击退；是否连带影响伤害未经原生证据确认 |
| `L4D_OnDoAnimationEvent` | 首石窗口内拦截出拳动画事件；实际拦截效果未实服验证 |
| `OnPlayerRunCmd`（SourceMod） | 从 AI 命令里剥离 `IN_ATTACK`；已开始的挥击不受影响 |

### C 内部（7）

只用于记录、统计或状态推进，不构成对外契约：

`L4D_OnDoAnimationEvent_Post`、`L4D_OnCThrowActivate_Post`、`L4D2_OnSelectTankAttackPre`、`L4D_TankRock_OnDetonate`、`L4D_TankClaw_DoSwing_Pre`、`L4D_TankClaw_DoSwing_Post`、`L4D_TankClaw_OnPlayerHit_Post`

> 注意：`L4D2_OnSelectTankAttackPre` 在 v2.2.6 及更早版本会改写 `sequence`（763→761）。v2.2.7 起**只记录**。升级时不要依赖旧行为。

---

## 7. 调用的外部 Native（4）

这些是**我们依赖别人**的接口，与第 5 节的「导出」正好相反。

| Native | 来源 | 级别 | 说明 |
|---|---|---|---|
| `L4D2_SpawnTank` | Left4DHooks | A | 指定坐标创建 Tank |
| `L4D_GetRandomPZSpawnPosition` | Left4DHooks | A | 请求原生候选点 |
| `L4D2_NavAreaBuildPath` | Left4DHooks | A | NAV 可达性判断 |
| `SDKHook`（`OnTakeDamage`） | SDKHooks | A | 伤害倍率 |

「A」是指**接口本身**稳定，但它们是**外部依赖**：Left4DHooks / SourceMod 升级时要跟着核对。本项目不做兼容垫片。

> 另有一个**可选的自家 native** `TankRun_GetLiveReplicas` 由 Tick Telemetry 消费，见 5.2；它不属于外部依赖。

---

## 8. 内部实现（明确不承诺）

以下内容不属于公共接口，**升级会变**：

| 内容 | 说明 |
|---|---|
| `tankrun_bridge` 报文格式 | 见 4.5 |
| 日志文本与字段顺序 | 用于人工阅读与 grep，不是稳定协议 |
| `tankrun_control_dump.txt` 的分块与行号 | 行号随参数个数变化，不要写死 |
| 参数表容量（`TR_PARAM_MAX = 57`） | 已满；新增参数会改变容量 |
| 内部定时器周期（扫描 0.25 秒、校正 5 秒等） | 可随版本调整 |
| 补位实体的内部状态与命名 | 属于实现细节 |
| 文件格式细节（`键=值` 的顺序、注释行） | 读入兼容，写出的顺序不保证 |

---

## 9. 二次开发建议

| 你想做的事 | 建议 |
|---|---|
| 只改参数默认值 | 用 `configs/` 或 `server.cfg`，不要改源码 |
| 加一个新参数 | 需要同时改 `TR_CreateConVar`、`TR_RegisterParams`、`TR_PARAM_MAX`（已满） |
| 依赖某个 ConVar | 只用 A 级；B 级要写清你测过的构建与场景 |
| 依赖日志做自动化 | 不要。日志不是协议；请用 `sm_tank_dump` 的字段或 SourceMod API |
| 接 Left4DHooks | 先读 [回调参考](left4dhooks.md) 的能力边界，避免假设它做不到的事 |
| 调用本仓库的 native | 只有独立 `tankrun_capacity.smx` 提供 4 个；按 5.2 的方式自行声明 + 可选探测。控制中心不导出任何 native |
| 判断某个插件/接口在不在 | 本项目没有库标识，只能用 `GetFeatureStatus(FeatureType_Native, "...")` 或 `FindConVar` |
| 做 64 位适配 | 见 [64 位兼容层设计](../../compatibility/64-bit-compat-layer.md) |
