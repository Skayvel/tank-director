# 原生接口研究

原生侧被研究过的接口分三类：**ConVar**、**公开 Hook/native**、**模式脚本字段**。本文是索引与结论摘要，完整表在 `analysis/tankrun-interface-inventory-20261115/interfaces.md`。

> 该清单是 2026-11-15 的快照。产品接口现状见 [接口参考](../../reference/api/README.md)。

---

## 1. 原生 ConVar

### 1.1 投石相关

| ConVar | 注册默认 | 模式声明 | 消费点 | 结论 |
|---|---:|---|---|---|
| `z_tank_throw_interval` | 5 | Tank Run 声明 999999 | Activate `10367b60` / Update `10369590` / clear `10367d90` → setter `10360e00` | 激活设 `interval+1`；特定中断设 `interval`；clear 设 `max(interval,param)`；自然结束不重设 |
| `tank_throw_min_interval` | —— | —— | 与上一项不是同一控制点 | 两项常被写成同一个东西，实际分开 |
| `tank_throw_allow_range` | —— | —— | 资格距离门 | 与「执行能力门」不同 |
| `z_tank_throw_fail_interval` | —— | —— | 失败间隔 | 不覆盖所有失败 |
| `z_tank_throw_force` | —— | —— | 出手力度 | `OnRelease` 可作未来矢量入口 |
| `tank_throw_lead_time_factor` | —— | —— | 预测提前量 | |
| `tank_throw_loft_rate` / `tank_throw_max_loft_angle` | —— | —— | 抬角计算 | 不是完整飞行物理 |
| `tank_throw_aim_error` | —— | —— | 特定数值模式分支 | 模式语义待补 |
| `tank_rock_overhead_percent` | —— | —— | 过肩投掷 | 研究记录存在，本构建注册未确认 |

### 1.2 拳击相关

| ConVar | 结论 |
|---|---|
| `tank_swing_range` | 四处武器读取点；参与攻击选型、候选查询、挥击初始扫掠点。**不等于最大实际命中距离** |
| `tank_attack_range` | 已审普通 AI 起拳分支**不读它**，用固定 50.0 |
| `tank_fist_radius` | 不能与 56 简单相加成「拳距 71」 |
| `tank_swing_interval` / `tank_swing_fast_interval` / `tank_swing_miss_interval` | 三个不同间隔，按分支回归 |
| `tank_swing_duration` / `tank_windup_time` | 需要先补动画与生效帧 |
| `z_tank_attack_interval` | 局部计时路径，与其他拳击计时关系未闭合 |

### 1.3 生成与导航

| ConVar | 结论 |
|---|---|
| `z_spawn_range` | 先分清命令入口与脚本入口 |
| `z_spawn_safety_range` | **只影响局部门**，不是统一硬最小距离 |
| `z_finale_spawn_tank_safety_range` | 只提供该分支共用的 r；独立 min/max 仍需追加策略 |
| `nb_allow_avoiding` / `nb_allow_climbing` | 原生导航开关 |

### 1.4 卡住保护（七个）

| ConVar | 注册默认 |
|---|---:|
| `tank_stuck_failsafe` | `1` |
| `tank_stuck_time_choose_new_target` | `2` |
| `tank_stuck_visibility_tolerance_choose_new_target` | `5` |
| `tank_stuck_time_suicide` | `10` |
| `tank_stuck_visibility_tolerance_suicide` | `15` |
| `tank_visibility_tolerance_suicide` | `60` |
| `tank_stasis_time_suicide` | `30` |

研究结论见 [已验证结论](../verified-findings.md) 第 6 节。**当前 Tank Run 菜单没有接入这七项。**

---

## 2. PC01–PC26 参数映射

项目早期为 26 个「可控制的参数/能力」建立了映射，编号 PC01–PC26，并拆成 30 个控制单元。原始表在 `indexes/parameter-control/parameter-behavior-map.json`，解读在 `docs/parameter-control-research.md`。

**这 26 项是研究映射，不等于产品功能。** 当前接入情况（v2.2.8）：

| 状态 | 数量 | 举例 |
|---|---:|---|
| 菜单已接入 | 7 | PC01、PC02、PC08、PC10、PC16、PC17 |
| 未接入但 ConVar 可读写 | 若干 | 见 [参数参考](../../reference/api/convars.md) |
| 未接入且需要窄干预 | 若干 | PC05（前摇）、PC07（起拳 range）、PC06（首见优先投石） |

关键区分：

- **PC16** 是「生成间隔 + 双 Tank 状态」，**PC17** 只是数量上限。两者常被混为一谈。
- **PC07** 虽有原生 ConVar，已审 AI 触发分支仍用固定 50。
- **PC05** 只有 timer 线索，安全修改入口尚未知。
- **PC06** 的「首见优先投石」后来由插件用不同机制实现（v2.2.7 起为近战保护 + 离手确认），**不是**原设计里的窄干预。

---

## 3. 公开 Hook / native 的能力边界

这是「插件为什么做不到某些事」的直接原因。完整表在 `interfaces.md` 第四节，摘要：

| 入口 | 能控制什么 | **不能**据此宣称什么 |
|---|---|---|
| `L4D2_OnSelectTankAttackPre(client, &sequence)` | 为到达动画选择边界的请求改 activity | 不是「出拳还是投石」的战术决策 Hook；改 activity 不足以证明能力激活、石头创建/离手、冷却同步 |
| `L4D2_OnSelectTankAttack(client, &sequence)` | 改模型动画 sequence | 动画序列号不是原生 AI 选择分支；不能从动画长度推投石冷却 |
| `L4D_OnCThrowActivate(ability)` | 能力执行的否决点 | 没有「强制投石」「修改冷却」「修改前摇」参数；不给目标与失败原因 |
| `L4D_OnCThrowActivate_Post` | 记录 wrapper 走了未阻止路径 | **不代表激活成功或石头已离手** |
| `L4D_TankRock_OnRelease(...)` | 离手时位置/角度/初速度/旋转改写 | 该版本 `Plugin_Handled` 阻止分支被注释，不能靠它阻止离手 |
| `L4D_TankRock_OnRelease_Post` | 离手成功边界的观察信号 | 不能证明后续命中、重力、碰撞/伤害 |
| `L4D2_OnChooseVictim_Pre(SI, &lastTarget)` | 改传给原生选择器的 lastTarget | 不是「首见幸存者」事件；flags/ignoreTarget 在公开实现中被注释 |
| `L4D2_OnChooseVictim(SI, &curTarget)` | 替换普通选择函数结果 | 不保证所有事件/机枪/命令路径都经过它；不保证朝向、路径、攻击对象同时改变 |
| `L4D_TankClaw_DoSwing_Pre/Post` | 记录进入挥击边界 | **不能 return Handled 阻止挥击** |
| `L4D_TankClaw_OnPlayerHit_Pre` | 可否决该命中消费者函数 | 不能等同于修改触发距离、命中几何或出拳选择 |
| `L4D_OnSpawnTank(pos, ang)` | 读取坐标准备并否决创建 | **不是坐标改写 forward** |
| `L4D_GetRandomPZSpawnPosition(...)` native | 向原生请求候选点，可自行再过滤 | 候选过滤是否等同 VScript ZSpawn 未证实 |
| `L4D2_SpawnTank(pos, ang)` native | 有坐标的 Tank 创建入口 | wrapper 不做距离/LOS/NAV 过滤 |

---

## 4. 原版模式脚本字段

| 字段 | 类型 | 用途 |
|---|---|---|
| `SessionState.SpawnInterval` | float | 自动生成间隔 |
| `SessionState.DoubleTanks` | bool | 每批双 Tank |
| `SessionState.SpawnTankThink` | bool | 脚本自动生成开关 |
| `SessionState.TankHealth` | int | 生成时应用的血量 |
| `SessionState.TanksDisabled` | bool | 阶段禁用标志 |
| `SessionOptions.cm_TankLimit` | int | 数量门 |

脚本逐分支账本：`indexes/tankrun-script-branches.json`；源码：`evidence/packages/update/scripts/vscripts/tankrun.decoded.nut`。

**没有运行时加载来源证明。** 字段存在只说明有这几个名字，不说明是哪个版本、有没有被服务器覆盖。

---

## 5. 历史外部参考

研究还审查过 NanakaNeko 的第三方 `tankrun.sp` 1.6：它通过 `L4D_OnFirstSurvivorLeftSafeArea`、重复定时器和 `FakeClientCommand("z_spawn_old tank auto")` 请求生成。

**那不是官方 `tankrun.nut`，也不是当前服务器安装的项目控制插件。** 引用时要区分。
