# 参数参考（ConVar）

控制中心 v2.2.11 的完整参数表。**所有条目都对应源码**：插件自有参数在 `scripting/tankrun/tr_core.inc:1430-1494`（注册）与 `tr_core.inc:1496-1758`（参数表），引擎参数通过 `FindConVar` 绑定。

## 三类参数

| 类别 | 前缀/来源 | 谁拥有 | 说明 |
|---|---|---|---|
| 插件参数 | `tankrun_*` | 本插件 | 插件自己的 ConVar，`CreateConVar` 注册 |
| 引擎参数 | `tank_*`、`z_*`、`nb_*`、`tank_throw_*` | 游戏引擎 | 插件只做 `FindConVar` + 读写，不注册。本构建未注册时菜单显示「本构建未注册该 ConVar，无法修改」 |
| 脚本参数 | `tankrun_script_*` | 官方 Tank Run 模式脚本 | 通过临时 `logic_script` 读写 `SessionState` / `SessionOptions` 字段，**不是 ConVar** |

统计：55 个自有 ConVar（其中 49 个进入菜单参数表，6 个全局开关不入表）、1 个内部桥接；菜单参数表另有 28 个引擎参数与 4 个脚本参数，合计 **81 项**。

> **容量上限**：`TR_PARAM_MAX = 84`（`tr_core.inc:20`），当前已用 81 项（剩余 3 槽）。新增参数超过容量前须调大该宏，否则 `TR_AddParam` 会写日志 `参数表已满，忽略 <名字>` 并返回 `-1`。

## 效果验证状态的含义

每个参数在参数表里带一个 `verified` 标记（`TR_AddParam` 第 9 个参数，`tr_core.inc:365`）：

- `已验证`：原生消费点已在静态分析中确认，改值按预期作用于该消费点。
- `未验证`：菜单会标注「（效果未验证）」。**能改、能读回，但改了之后游戏里是否真的变，没有证据。**

`未验证` 不等于「无效」，只表示「没有证据」。详见 [未实现能力与限制](limits.md)。

## 内部 ConVar

| 名称 | 默认 | 标志 | 用途 |
|---|---|---|---|
| `tankrun_bridge` | 空 | `FCVAR_DONTRECORD` | VScript 读回通道。插件把同步脚本写进这个 ConVar，再读回结果。**不要手工修改**，改了会破坏脚本字段的读回校验 |

## 全局开关（不进入参数表）

这 6 个 ConVar 是整体行为开关，不出现在菜单参数子页里，用 `sm_tank_set` 或服务器 `sm_cvar` 修改。

| 名称 | 默认 | 范围 | 说明 |
|---|---|---|---|
| `tankrun_enabled` | `1` | 0/1 | 功能总开关。关闭后菜单仍能打开，但策略不生效 |
| `tankrun_public_readonly` | `0` | 0/1 | `1` 时普通玩家可打开**只读**菜单（所有修改项禁用） |
| `tankrun_debug` | `0` | 0/1 | 控制台调试输出。等价于 `sm_tank_debug 1` |
| `tankrun_log_events` | `1` | 0/1 | 事件日志开关。等价于 `sm_tank_log 1` |
| `tankrun_ai_only` | `1` | 0/1 | 只对 AI Tank 应用策略；玩家控制的 Tank 只观察不修改 |
| `tankrun_conflict_mode` | `0` | 0/1 | 参数冲突处理：`0` = 警告后写入，`1` = 拒绝写入 |
| `tankrun_default_expert` | `1` | 0/1 | Tank Run 每张图执行配置后默认设专家难度 |

## M02 生成与数量

| 参数 | 类型 | 注册默认 | 允许范围（步进） | 效果验证 | 说明 |
|---|---|---:|---|---|---|
| `tankrun_auto_spawn` | 整数 | `1` | 0–1 (1) | 已验证 | 统一自动生成总开关；原版脚本仍提供地图阶段门 |
| `tankrun_auto_interval` | 浮点 | `2.0` | 0.5–600 (0.5) | 已验证 | 自动生成间隔（秒）。用引擎 tick 间隔换算，不按固定 tick 计 |
| `tankrun_auto_limit` | 整数 | `30` | 1–31 (1) | 已验证 | 在场 Tank 总量上限。实际数量还受客户端槽位限制 |
| `tankrun_rotate_oldest` | 整数 | `1` | 0–1 (1) | 已验证 | 满员时处死最早出生的 AI Tank 以腾位置 |
| `tankrun_fast_slot_reclaim` | 整数 | `1` | 0–2 (1) | 已验证 | 死亡 AI Tank 快速回收槽位：`0` 原版 6 秒 / `1` 仅 Tank Run 次帧回收 / `2` 全模式次帧回收 |
| `tankrun_auto_double` | 整数 | `0` | 0–1 (1) | 已验证 | 一次调度生成两只 |
| `tankrun_spawn_mode` | 整数 | `0` | 0–4 (1) | 已验证 | 默认生成方式：`0` 原生随机区域 / `1` 距离带 / `2` 准星位置 / `3` Tank Run 脚本 / `4` 幸存者附近原生选位 |

## M09 生命、伤害与速度

| 参数 | 类型 | 注册默认 | 允许范围（步进） | 效果验证 | 说明 |
|---|---|---:|---|---|---|
| `tankrun_tank_health` | 整数 | `0` | 0–20000 (100) | 已验证 | 生成后写入实体当前血量；`0` = 不干预 |
| `tankrun_tank_maxhealth` | 整数 | `0` | 0–20000 (100) | 已验证 | 写入 `m_iMaxHealth`；引擎/模式可能在生成时重算 |
| `tankrun_mode_health_sync` | 整数 | `1` | 0–1 (1) | 已验证 | 同步血量到脚本 `SessionState.TankHealth` |
| `tankrun_tank_damage_scale` | 浮点 | `1.0` | 0.1–10 (0.1) | 已验证 | Tank 受伤倍率，只对 AI Tank 生效 |
| `tankrun_fire_damage_scale` | 浮点 | `1.0` | 0–10 (0.1) | 已验证 | 火焰伤害倍率，仅对带燃烧属性的伤害生效 |
| `tankrun_speed_scale` | 浮点 | `1.0` | 0.5–2 (0.05) | 已验证 | 写 `m_flMaxSpeed`；引擎状态可能覆盖，插件会再校正 |

## M07 拳击

| 参数 | 类型 | 注册默认 | 允许范围（步进） | 效果验证 | 说明 |
|---|---|---:|---|---|---|
| `tankrun_claw_types_idle` | 整数 | `7` | 0–7 (1) | **未验证** | 未投石阶段拳种掩码：`1` 上勾 / `2` 右勾 / `4` 左勾，累加多选，`0` 禁拳 |
| `tankrun_claw_range_idle` | 浮点 | `0.0` | 0–500 (2) | **未验证** | 未投石阶段按三维碰撞边界距离限制起拳、伤害和击退，`0` 原版 |
| `tankrun_claw_types_rock` | 整数 | `7` | 0–7 (1) | **未验证** | 投石期间拳种掩码（编码同上） |
| `tankrun_claw_range_rock` | 浮点 | `0.0` | 0–500 (2) | **未验证** | 投石期间碰撞边界距离上限，`0` 原版 |
| `tankrun_claw_damage_scale` | 浮点 | `1.0` | 0.05–10 (0.05) | 已验证 | 拦截 Tank 攻击者的伤害并按倍率缩放 |
| `tankrun_claw_block_hit` | 整数 | `0` | 0–1 (1) | **未验证** | 让 `L4D_TankClaw_OnPlayerHit_Pre` 返回 `Handled`。**风险项**，只影响击退，不保证不影响其它效果 |
| `tank_swing_range` | 浮点 | 引擎值 | 16–500 (2) | 已验证 | 原生注册参数。参与初始攻击点计算，**不等于最大实际命中距离** |
| `tank_attack_range` | 浮点 | 引擎值 | 0–500 (2) | **未验证** | 已审 AI 触发分支用固定 `50.0`，未见业务读取此项（见研究仓库 `analysis/parameter-control/M07-01/report.md`） |
| `tank_fist_radius` | 浮点 | 引擎值 | 0–200 (1) | 已验证 | 静态实证：`CTankClaw::Sweep` 盒体扫描半边长（默认 `15`） |
| `tank_swing_interval` | 浮点 | 引擎值 | 0.1–10 (0.1) | 已验证 | 普通挥击间隔 |
| `tank_swing_fast_interval` | 浮点 | 引擎值 | 0.1–10 (0.1) | 已验证 | 快速挥击间隔 |
| `tank_swing_miss_interval` | 浮点 | 引擎值 | 0.1–10 (0.1) | 已验证 | 挥空间隔 |
| `tank_windup_time` | 浮点 | 引擎值 | 0–5 (0.05) | 已验证 | 静态实证：`StartSwing` 起手到触发 `DoSwing` 命中扫描的延迟（默认 `0.5`） |
| `tank_swing_duration` | 浮点 | 引擎值 | 0–5 (0.05) | 已验证 | 静态实证：`DoSwing` 命中扫描有效窗口时长（默认 `0.2`） |
| `z_tank_attack_interval` | 浮点 | 引擎值 | 0.1–10 (0.1) | 已验证 | 静态实证：`CTankClaw::OnHitPlayer` 击飞站立幸存者后重置的挥击冷却（默认 `1.5`） |

## M05 / M06 投石

| 参数 | 类型 | 注册默认 | 允许范围（步进） | 效果验证 | 说明 |
|---|---|---:|---|---|---|
| `tankrun_rock_types` | 整数 | `15` | 0–15 (1) | **未验证** | AI 投石动作掩码：`1` 下勾 / `2` 单手过肩 / `4` 腰侧 / `8` 双手过顶，累加多选，`0` 禁投 |
| `tankrun_rock_no_turn` | 整数 | `0` | 0–1 (1) | **未验证** | 覆盖各投石类型视角设置，统一锁定抬手前视角 |
| `tankrun_rock_keep_loft` | 整数 | `1` | 0–1 (1) | 已验证 | 锁视角时仅锁水平 Yaw，保留引擎计算的俯仰抬角 Pitch，防砸脚 |
| `tankrun_rock_after_delay` | 浮点 | `0.35` | 0–1.5 (0.05) | 已验证 | 抬手后锁角模式等待 AI 完成转向的采样延迟（秒） |
| `tankrun_rock_cancel_clear_stamina` | 整数 | `0` | 0–1 (1) | **未验证** | 实验项：投石前摇被硬直中断时尝试清零 `m_flStamina` 体力减速惩罚 |
| `tankrun_rock_recovery` | 浮点 | `0.75` | 0–3 (0.05) | **未验证** | 离手、动画中断或超时后延续视角锁定及投石阶段拳击设置的秒数 |
| `tankrun_rock_view_undercut` | 整数 | `0` | 0–2 (1) | **未验证** | 下勾斜抛视角：`0` 原版跟随 / `1` 锁定抬手前 / `2` 锁定抬手后 |
| `tankrun_rock_view_onehand` | 整数 | `0` | 0–2 (1) | **未验证** | 单手过肩抛视角（编码同上） |
| `tankrun_rock_view_hip` | 整数 | `0` | 0–2 (1) | **未验证** | 腰侧抛视角（编码同上） |
| `tankrun_rock_view_twohand` | 整数 | `0` | 0–2 (1) | **未验证** | 双手过顶抛视角（编码同上） |
| `tankrun_rock_enabled` | 整数 | `1` | 0–1 (1) | 已验证 | `0` = 拦截 `CThrow` 激活，Tank 真实无法投石 |
| `tankrun_rock_min_delay` | 浮点 | `0.0` | 0–60 (0.5) | 已验证 | 按 Tank 记录真实释放时间，窗口内拦截新的激活 |
| `tankrun_rock_force_scale` | 浮点 | `1.0` | 0.2–3 (0.05) | 已验证 | 在 `OnRelease` 修改真实释放速度 |
| `z_tank_throw_interval` | 浮点 | 引擎值 | 0–999999 (1) | 已验证 | 原生执行冷却。Tank Run 模式可能覆盖为极大值，看实际读回 |
| `tank_throw_min_interval` | 浮点 | 引擎值 | 0–120 (0.5) | 已验证 | AI 投石请求间隔，**与上一项不是同一控制点** |
| `z_tank_throw_fail_interval` | 浮点 | 引擎值 | 0–120 (0.5) | 已验证 | 失败间隔 |
| `tank_throw_allow_range` | 浮点 | 引擎值 | 0–5000 (10) | 已验证 | 原生资格距离门 |
| `z_tank_throw_force` | 浮点 | 引擎值 | 0–3000 (10) | 已验证 | 投石力；与释放速度倍率共同影响轨迹 |
| `tank_throw_lead_time_factor` | 浮点 | 引擎值 | 0–5 (0.05) | 已验证 | 预测提前量系数 |
| `tank_throw_loft_rate` | 浮点 | 引擎值 | 0–1 (0.01) | 已验证 | 抛射抬升率，参与抬角计算，不是完整飞行物理 |
| `tank_throw_max_loft_angle` | 浮点 | 引擎值 | 0–89 (1) | 已验证 | 最大抬升角 |
| `tank_throw_aim_error` | 浮点 | 引擎值 | 0–500 (5) | 已验证 | 静态实证：仅 Easy 难度由 `ThrowRockAimHelper` 施加垂直视线随机偏移半径（默认 `100`） |
| `tank_rock_overhead_percent` | 浮点 | 引擎值 | 0–100 (5) | **未验证** | 研究记录存在，但本构建是否注册未确认；缺失时菜单显示不可用 |

## M04 / M08 目标与首石

| 参数 | 类型 | 注册默认 | 允许范围（步进） | 效果验证 | 说明 |
|---|---|---:|---|---|---|
| `tankrun_target_mode` | 整数 | `0` | 0–4 (1) | **未验证** | `0` 原生 / `1` 最近 / `2` 最远 / `3` 最低血（含临时血） / `4` 固定玩家。只覆盖 `L4D2_OnChooseVictim` 路径 |
| `tankrun_target_fixed` | 整数 | `0` | 0–MaxClients (1) | **未验证** | 固定目标玩家序号，仅策略=4 时使用 |
| `tankrun_target_skip_incap` | 整数 | `1` | 0–1 (1) | 已验证 | 优先跳过倒地、挂边及 4 类特感控制幸存者（全员倒地自动回退） |
| `tankrun_target_hysteresis` | 浮点 | `200.0` | 0–2000 (25) | 已验证 | 最近/最远目标策略切换距离滞回（单位，`0` 关闭） |
| `tankrun_target_scan` | 浮点 | `0.4` | 0.1–5 (0.1) | 已验证 | 插件侧感知扫描 / 首次接敌检测周期 |
| `tankrun_firstrock_enabled` | 整数 | `1` | 0–1 (1) | 已验证 | 方案 1b：出生即严锁拳击直至真实离手或起手遭硬直打断；近距禁拳时极速扔饼 |
| `tankrun_firstrock_allow_props` | 整数 | `0` | 0–1 (1) | 已验证 | 首石前拍车/破障：`0` 全部严锁 / `1` 远离幸存者（`>130u`）允许拍打障碍物，近身及对人命中仍严锁 |
| `tankrun_firstrock_quick_throw` | 整数 | `1` | 0–1 (1) | 已验证 | 首石未完成且脚踩实地面、处于近距离或近距试图出拳被锁时，立刻注入原版右键投石 |
| `tankrun_firstrock_quick_range` | 浮点 | `280.0` | 0–1000 (20) | 已验证 | 极速扔饼触发距离（覆盖原生 `tank_throw_allow_range=250` 盲区；`0` 仅贴脸试图出拳被锁时触发） |
| `tankrun_firstrock_timeout` | 浮点 | `0.0` | 0–30 (1) | 已验证 | 首石窗口超时秒数；`0` 禁用时间超时触发（方案 1b 默认）；正数时离地暂停计时 |
| `tankrun_firstrock_attempts` | 整数 | `2` | 1–5 (1) | 已验证 | 激活尝试上限（仅在 `tankrun_firstrock_timeout > 0` 时启用） |
| `tankrun_ai_only` | 整数 | `1` | 0–1 (1) | 已验证 | 见全局开关段落 |

## M03 生成位置

| 参数 | 类型 | 注册默认 | 允许范围（步进） | 效果验证 | 说明 |
|---|---|---:|---|---|---|
| `tankrun_spawn_visibility` | 整数 | `0` | 0–2 (1) | 已验证 | `0` 跟随游戏难度 / `1` 只刷在遮挡后 / `2` 沿用前线选位 |
| `tankrun_spawn_min_dist` | 浮点 | `220.0` | 0–5000 (50) | 已验证 | 离所有存活生还者的最小距离 |
| `tankrun_spawn_max_dist` | 浮点 | `3000.0` | 100–10000 (50) | 已验证 | 距领先玩家的最远距离；范围内分近中远三段随机 |
| `tankrun_spawn_attempts` | 整数 | `6` | 1–20 (1) | 已验证 | 位置尝试次数 |
| `tankrun_spawn_fallback` | 整数 | `1` | 0–1 (1) | 已验证 | 距离带失败后回退原生随机区域 |

## M01 / M11 规则与导航

| 参数 | 类型 | 注册默认 | 允许范围（步进） | 效果验证 | 说明 |
|---|---|---:|---|---|---|
| `z_spawn_range` | 浮点 | 引擎值 | 200–10000 (100) | 已验证 | 原生注册参数；**不是** VScript 自动生成的最大距离 |
| `z_spawn_safety_range` | 浮点 | 引擎值 | 0–5000 (50) | 已验证 | 只影响局部门，不是统一最小距离 |
| `z_finale_spawn_tank_safety_range` | 浮点 | 引擎值 | 0–5000 (50) | 已验证 | 本构建可能未注册，缺失时显示不可用 |
| `nb_allow_avoiding` | 整数 | 引擎值 | 0–1 (1) | 已验证 | 原生导航避让 |
| `nb_allow_climbing` | 整数 | 引擎值 | 0–1 (1) | 已验证 | 原生导航攀爬 |
| `tank_stuck_failsafe` | 整数 | 引擎值 | 0–1 (1) | 已验证 | 静态实证：`TankAttack` 不可见且卡死超时自杀退场总开关 |
| `tank_stuck_time_choose_new_target` | 浮点 | 引擎值 | 0–30 (0.5) | 已验证 | 静态实证：卡死超过此时长时触发重选目标（默认 `2.0`） |
| `tank_stuck_time_suicide` | 浮点 | 引擎值 | 0–60 (1) | 已验证 | 静态实证：卡死满此时长且不可见时触发原生自杀（默认 `10.0`） |
| `tank_visibility_tolerance_suicide` | 浮点 | 引擎值 | 0–120 (5) | 已验证 | 静态实证：卡死自杀需同时满足不可见时长门限（默认 `60.0`） |

## 脚本参数（不是 ConVar）

这四项通过 VScript 读写官方 Tank Run 模式的会话字段，**不是 ConVar**，用 `sm_cvar` / `FindConVar` 都读不到。

| 参数 | 类型 | 允许范围（步进） | 映射到的脚本字段 | 说明 |
|---|---|---|---|---|
| `tankrun_script_spawn_interval` | 浮点 | 0.5–600 (0.5) | `SessionState.SpawnInterval` | 统一自动生成间隔的兼容入口 |
| `tankrun_script_tank_limit` | 整数 | 0–31 (1) | `SessionOptions.cm_TankLimit` | `0` = 暂停阶段生成；正值同步到在场总量上限 |
| `tankrun_script_double_tanks` | 整数 | 0–1 (1) | `SessionState.DoubleTanks` | 统一双 Tank 开关的兼容入口 |
| `tankrun_script_tank_health` | 整数 | 500–20000 (100) | `SessionState.TankHealth` | 模式在 `tank_spawn` 时应用 |

脚本字段不可用时（非 tankrun 模式、脚本未加载、桥接失败），菜单里这四项显示「脚本接口当前不可用」，写操作被拒绝。详见 [VScript 桥接](vscript-bridge.md)。

## 读写语义

- **写入前做范围校验**：`sm_tank_set` 会把值夹到允许范围内，并在聊天栏回显「数值按范围校正：A → B」（`tr_diag.inc:199-202`）。
- **写后读回**：写入成功后重新读取并回报实际值，不报「成功」了事。
- **冲突检测**：写入前比较「本插件上次写入值」，发现外部修改时按 `tankrun_conflict_mode` 警告后写入或拒绝写入，并写日志。
- **原始值**：首次修改前记录引擎/参数原值到 `configs/tankrun_control_originals.cfg`，供 `sm_tank_restore` 使用。

## 参数名称与菜单的关系

菜单「参数子页」显示的每个参数，与本文的 `参数名称` 一一对应，可以直接用 `sm_tank_set <参数名称> <数值>` 设置。`sm_tank_params` 会列出全部 81 项及当前值。

菜单里还有一类**不是参数**的行：`a:refresh`（刷新实际值）、`a:help`（帮助）等动作项，不要误当参数名。
