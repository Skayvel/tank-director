# 回调与 Native 参考（Left4DHooks / SourceMod）

控制中心 v2.2.8 实际使用的公开接口。**本文只列源码里真实存在的调用**，每条附源码行号。未列出的接口没有被使用。

## 概览

| 类型 | 数量 | 来源 |
|---|---:|---|
| **本插件导出给其他插件的 native** | **0** | 控制中心不注册任何 native |
| **本插件导出的 forward / 库标识** | **0** | 没有 `CreateGlobalForward` / `RegPluginLibrary` |
| 实现的 Left4DHooks forward（被扩展调用） | **14** | Left4DHooks |
| 处理的 SourceMod forward | 1 | `OnPlayerRunCmd` |
| 调用的外部 native | 4 类 | Left4DHooks / SDKHooks |
| 订阅的游戏事件 | 8 | SourceMod `HookEvent` |

> 唯一的对外 native 接口在**独立**的 `tankrun_capacity.smx` 中（4 个），控制中心的内嵌副本不注册它们。
> 导出接口、稳定性与调用约定见 [接口稳定性分级](stability.md) 第 5 节；本节只描述**本插件实现的外部回调**。

## 一、Left4DHooks Forward（插件实现）

### 目标选择

#### `L4D2_OnChooseVictim(int specialInfected, int &curTarget)`

- 位置：`tr_rock.inc:414-453`
- 用途：实现 `tankrun_target_mode` 策略（最近 / 最远 / 最低血 / 固定玩家），必要时改写目标。
- 返回：`Plugin_Continue` 表示保持原生；`Plugin_Changed` 表示改写了 `curTarget`。
- 前置过滤：非 Tank 客户端、非 AI Tank、功能关闭 → 一律 `Plugin_Continue`。
- 日志节流：每个 Tank 每秒最多一条 `目标选择 forward：...`（`tr_rock.inc:434-439`）。

**能力边界**（依接口清单 `analysis/tankrun-interface-inventory-20261115/interfaces.md` 第四节，研究仓库）：
它替换的是原生选择函数的**返回值**，不保证所有路径都经过这个函数。机枪、接触换人、命令、事件改写等路径不受影响，Body 朝向、追击路径与实际攻击对象也不会同时改变。这就是菜单把 `tankrun_target_mode` 标为「效果未验证」的原因。

### 动画事件（首石的第一道可见拦截）

#### `L4D_OnDoAnimationEvent(int client, int &event, int &variantParam)`

- 位置：`tr_rock.inc:205-216`
- 用途：首石窗口内拦截**出拳动画事件**。`PLAYERANIMEVENT_HAND_ATTACK` / `PLAYERANIMEVENT_HAND_LOW_ATTACK` 且 `TR_FirstRockClawGuard` 为真时返回 `Plugin_Handled`，日志 `严格首石禁拳：... 入口=出拳动画`。
- 这是四层禁拳保护里的一层（另三层：输入剥离、原生 detour、命中击退拦截）。

#### `L4D_OnDoAnimationEvent_Post(int client, int event, int variantParam)`

- 位置：`tr_rock.inc:218-238`
- 用途：
  - `PLAYERANIMEVENT_ROCK_THROW` → 记录「能力已发出原版动画请求」的时间戳，供 `_Post` 判定关联；
  - `PLAYERANIMEVENT_JUMP` / `TANK_CLIMB` / `LAND` → 标注攀跳/落地窗口，用于首石期间的超时暂停。
- **限制**：这些事件只说明动画阶段，不代表投石被接受或石头已离手。

### 投石

#### `L4D_OnCThrowActivate(int ability)`

- 位置：`tr_rock.inc:456-511`
- 用途：投石总开关与最小间隔的**真实拦截点**。
- 返回 `Plugin_Handled` 的两种情形：
  1. `tankrun_rock_enabled = 0` → 拦截，日志 `投石被拦截：总开关关闭`；
  2. 距该 Tank 上次真实释放不足 `tankrun_rock_min_delay` → 拦截，日志 `投石被拦截：插件最小间隔`。
- 附带：首石窗口内的激活会累加尝试次数并写 `首石原版激活尝试`。
- **限制**：该 forward 只给出 ability 实体，没有目标、没有失败原因、没有强制释放入口。所谓「强制投石」在公开接口上没有实现路径（见 [未实现能力](limits.md)）。

#### `L4D_OnCThrowActivate_Post(int ability)`

- 位置：`tr_rock.inc:513-527`
- 用途：只做记录，区分本次激活是否在同一个 tick 内观察到原版动画请求。
- **限制**：`Post` 只表示「wrapper 走了未阻止路径」，**不代表激活成功、也不代表石头已离手**。

#### `L4D2_OnSelectTankAttackPre(int client, int &sequence)`

- 位置：`tr_rock.inc:530-555`
- 用途：**只记录**，不修改 `sequence`，永远返回 `Plugin_Continue`。
- 记录内容：动作编号（761 = `ACT_HULK_THROW`，762 = `ACT_TANK_OVERHEAD_THROW`），每秒最多一条。

> **重要版本变化**：v2.0.1–v2.2.6 曾在这里把拳击动作 763 改选为投石动作 761。**v2.2.7 起已移除该改选**，投石起手、目标预测、出手方向与碰撞都交还引擎，插件只负责近战保护与真实离手确认。v2.2.8 源码中不存在任何对 `sequence` 的写回。
>
> 因此 `docs/tankrun-control-center.md` 第 5 节 M08 与 `docs/coordination/command-board.md` 中「首石策略覆盖动作：763 → 761」的验收口径**只适用于 v2.2.6 及更早**。当前版本不应期待这条日志。

#### `L4D_TankRock_OnRelease(...)`

- 位置：`tr_rock.inc:570-594`
- 用途：记录释放前向量；按 `tankrun_rock_force_scale` 缩放 AI Tank 的真实释放速度。
- 返回：缩放生效时 `Plugin_Changed`，否则 `Plugin_Continue`。
- 附带记录：`原版投石帧`（activity、`m_nSequence`、`m_flCycle`、离手点与 `debris` 附件的距离）。
- **限制**：`Plugin_Handled` 阻止分支在 Left4DHooks 该版本中是注释掉的，**不能靠这个返回阻止离手**。改初速度不是改 AI 决策，也不是完整弹道控制。

#### `L4D_TankRock_OnRelease_Post(...)`

- 位置：`tr_rock.inc:596-632`
- 用途：确认石头实体真实存在（classname 必须为 `tank_rock`），这是首石成功的**唯一判据**；同时统计释放次数、记录模型与最终速度。
- 首石窗口在此关闭：日志 `严格首石离手：Tank#N userid=M rock=R，解除禁拳`。
- 若实体校验失败 → `投石释放后未确认石头实体`，不计入成功。

#### `L4D_TankRock_OnDetonate(int tank, int rock)`

- 位置：`tr_rock.inc:634-650`
- 用途：记录落地/引爆；若离手后 0.10 秒内碰撞，额外写 `投石离手后立即碰撞`（用于诊断「石头刚出手就撞墙」）。

### 拳击

#### `L4D_TankClaw_DoSwing_Pre(int tank, int claw)` / `_Post`

- 位置：`tr_rock.inc:653-675`
- 用途：只记录挥击开始/结束与动作编号，累计统计。
- **限制**：这两个 forward 签名是 `void`，**不能返回 Handled 阻止挥击**，也不是攻击触发距离/频率的修改入口。

#### `L4D_TankClaw_OnPlayerHit_Pre(int tank, int claw, int player)`

- 位置：`tr_rock.inc:677-701`
- 用途：
  1. 首石窗口内对幸存者的击退**被阻止**（`TR_FirstRockClawGuard` 为真时返回 `Plugin_Handled`），每个 Tank 的首条拦截写日志 `严格首石禁拳：Tank#N userid=M 入口=拳击击退`；
  2. `tankrun_claw_block_hit = 1` 时无条件阻止（风险项，日志明确写「仅阻挡击退，不修改伤害」）。
- **限制**：阻止的是这个命中消费者函数。完整效果（是否真的不影响伤害）仍需原生证据，**不能从注释推断**。

#### `L4D_TankClaw_OnPlayerHit_Post`

- 位置：`tr_rock.inc:703-710`。只记录。

### 生成

#### `L4D_OnSpawnTank(const float pos[3], const float ang[3])`

- 位置：`tr_tank.inc:948-954`
- 用途：**接管原版脚本的生成请求**。当统一调度已接管（`TC_ManagedMode()` 为真且前端生成归属本插件）时，只有在「真人已离开起始安全区且当前数量未达有效上限」的情况下放行原版创建，否则返回 `Plugin_Handled` 否决，改由插件自己的选位流程生成。
- 返回：`Plugin_Continue` 放行 / `Plugin_Handled` 否决。
- **限制**：Left4DHooks 该版本的这个 forward **不能改写坐标**，只能放行或否决创建。项目因此没有用它实现「最小/最大距离」，距离筛选在插件自己的选位流程里做。

## 二、SourceMod Forward

#### `OnPlayerRunCmd(...)`

- 位置：`tr_rock.inc:304-316`
- 用途：首石窗口内**从 AI 命令里剥掉 `IN_ATTACK` 位**（`buttons &= ~IN_ATTACK`），从而阻止普通起拳，同时保留移动与副攻击输入。
- 返回 `Plugin_Changed` 仅在实际剥离时。
- **限制**：只作用于当前输入帧。已经进入前摇或已经开始播放的挥击不会被撤销。

## 三、调用的 Native

| Native | 出处 | 用途 | 失败时 |
|---|---|---|---|
| `L4D2_SpawnTank(pos, ang)` | 左4D Hooks | 在指定坐标创建 Tank | 返回 0，插件记为生成失败 |
| `L4D_GetRandomPZSpawnPosition(client, team, tries, pos)` | Left4DHooks | 向引擎请求候选出生点 | 返回 `false`，用于距离带/附近选位 |
| `L4D2_NavAreaBuildPath(area, goal, maxDist, team, ...)` | Left4DHooks | 判断候选点与领先玩家的 NAV 可达性 | 返回 `false` 时跳过该候选（`tr_lead_spawn.inc:142`） |
| `SDKHook(client, SDKHook_OnTakeDamage, ...)` | SDKHooks | 伤害倍率（Tank 受伤、火焰、拳击） | 扩展缺失时插件无法加载 |

## 四、订阅的游戏事件

全部是 `EventHookMode_Post`，位置 `tankdirector_menu.sp:119-129`。

| 事件 | 处理函数 | 用途 |
|---|---|---|
| `tank_spawn` | `Event_TankSpawn` | 记录出生顺序、初始状态；首石窗口起点 |
| `player_death` | `Event_PlayerDeath` | 清理该槽位状态 |
| `player_hurt` | `Event_PlayerHurt` | 拳击伤害统计 |
| `round_start` | `Event_RoundStart` | 复位回合状态 |
| `round_end` | `Event_RoundEnd` | 清理首石窗口与待判定投石 |
| `mission_lost` | `Event_RoundEnd` | 同上 |
| `map_transition` | `Event_RoundEnd` | 同上 |
| `finale_vehicle_leaving` | `Event_RoundEnd` | 同上 |

## 五、明确**没有**使用的接口

以下接口在项目研究里被审查过，但 v2.2.8 **不调用**。列出来是为了避免读者误以为它们可用：

| 接口 | 原因 |
|---|---|
| `L4D_OnSpawnTank` 改写坐标 | 该版本 forward 不提供 CopyBack 向量 |
| `L4D2_OnSelectTankAttack`（非 Pre） | 改的是模型序列，不是 AI 决策 |
| `L4D_OnGetRandomPZSpawnPosition` | 该版本未注册/实现被注释 |
| `L4D2_OnChooseVictim_Pre` | 可改 `lastTarget`，但与当前策略实现不一致，未采用 |
| DHook `NextBotCreatePlayerBot_Tank` | 属于 `admin_extra_menu` 等其它插件的路径，本插件不争夺生成权 |

## 六、自定义 Detour（唯一的一处原生干预）

- 位置：`tr_rock.inc:27-48`
- 目标：`TankRun::CTankClaw::PrimaryAttack`，通过 `gamedata/tankrun_firstrock.txt` 按字节签名定位。
- 方式：`DynamicDetour`，`CallConv_THISCALL`，`ReturnType_Void`，`ThisPointer_CBaseEntity`，`Hook_Pre`。
- 用途：首石窗口内**在武器入口直接阻断起拳**，比输入剥离更早。
- 降级：`DynamicDetour` native 不可用、gamedata 读取失败、签名不匹配、`Enable` 失败 → 不安装 detour，只保留输入/动画/命中三层保护。加载日志会明确写哪一条路径生效。

这是全插件**唯一**的原生地址依赖，因此也是唯一受游戏更新影响的脆弱点。
