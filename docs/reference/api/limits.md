# 未实现能力与硬限制

这份清单和功能清单同等重要。**凡是这里列出的，插件都做不到**，不要从菜单或其它文档期望这些能力。

来源：`docs/tankrun-control-center.md` 第 6 节的否定清单（v2.0.1 时点）＋ v2.2.11 源码核对。

## 一、公开接口层面做不到

| 能力 | 状态 | 原因 | 依据 |
|---|---|---|---|
| **强制 AI 真实投石**（按下即投） | 未实现 | 公开接口没有「强制释放 `CTankRock`」的入口。`L4D_TankRockPrj` 创建的是插件自己的抛射物，不是 Tank 的投石能力，项目明确不使用 | `tr_rock.inc`、菜单项 `x:force_throw` 显示为禁用 |
| **阻止已开始的挥击** | 未实现 | `L4D_TankClaw_DoSwing_Pre` 签名是 `void`，不能返回 `Handled`。插件只能阻止**新的输入帧**、**新的武器入口调用**和**已进入命中判定的击退** | [回调参考](left4dhooks.md) |
| **改写原生生成坐标** | 未实现 | `L4D_OnSpawnTank` 该版本不提供向量的 CopyBack，只能放行/否决 | [回调参考](left4dhooks.md) |
| **改投石前摇 / 动画阶段时钟** | 未实现 | 没有可确认的公开参数或事件入口 | `docs/tankrun-control-center.md` 第 262 行 |
| **每 Tank 独立的引擎 ConVar** | **不成立** | 引擎 ConVar 是全局的。插件只提供每 Tank 的**策略状态与统计**，不把全局值伪装成独立参数 | `docs/tankrun-control-center.md` 第 263 行 |

## 二、实现层面只有部分

| 能力 | 状态 | 说明 |
|---|---|---|
| **首石 100% 成功** | 部分 | 插件能维持窗口、禁拳、确认真实离手，**不能保证引擎一定接受投石请求**。超时是正常结局 |
| **原版完整目标选择算法** | 未实现 | 只覆盖经过 `L4D2_OnChooseVictim` 的路径。机枪、接触、命令、事件改写等路径不受影响 |
| **拳击命中距离 / 角度的精确控制** | 部分 / 未验证 | `tank_attack_range`、`tank_fist_radius` 可改，但已审 AI 触发分支用固定 `50.0`（M07-01 报告，见研究仓库 `analysis/parameter-control/M07-01/report.md`） |
| **火焰伤害的原生参数** | 部分 | 未确认专用 ConVar，用 `DMG_BURN` 伤害倍率实现 |
| **补位 Tank 的完整行为** | 部分 | 用地图 NAV + 实体碰撞追击、盒形命中；**未复刻**翻越、爬梯、逐骨骼命中 |
| **原版槽位超过 33** | 未实现 | 同构建客户端在槽位 33 触发 `UpdatePlayerName with bogus slot`，即使该槽为空 |

## 三、明确不做

| 能力 | 原因 |
|---|---|
| 玩家控制 Tank 的 AI 策略 | 避免影响真人玩家；`tankrun_ai_only 1` 时玩家 Tank 只观察不修改 |
| 路程刷克 / 压力刷克 | 不属于已确认的公开接口能力 |
| 完整 Tank AI 复刻 | 研究目标，不是本插件的交付范围 |
| 替换原版 Tank Run 核心玩法 | 模式阶段、终局、安全区判定仍由原版脚本决定 |

## 四、部署层面的硬限制

| 限制 | 具体数字 | 依据 |
|---|---|---|
| 客户端槽位上限 | 31 槽（32 有崩溃风险，上游标注） | `docs/tankrun-capacity.md`、L4DToolZ 2.5.1 |
| 客户端槽位 33 崩溃 | 客户端循环在槽位 33 触发 `tier0!Error` | `evidence/tankrun-capacity/client-slot-guard.txt` |
| 参数表容量 | 84 项（当前已注册 81 项） | `tr_core.inc:20` `TR_PARAM_MAX` |
| 平台 | 仅 Windows 有原生 detour 签名 | `gamedata/tankrun_firstrock.txt` |
| Tick 基线 | 100 tick；128 tick 未验证 | `indexes/status.json` |
| 生成位置重试 | 单次选位最多尝试 20 次 | `tankrun_spawn_attempts` 上限 |

## 五、文档里出现过但**当前版本没有**的东西

读旧文档时最容易踩的坑，逐条列出：

| 项目 | 出现位置 | 当前状态（v2.2.11） |
|---|---|---|
| `tankrun_rock_expert_lock` 参数 | `docs/tankrun-control-center.md` 第 458 行（v2.2.5 节） | **已移除**。v2.2.7 起投石预判交还引擎，源码无此 ConVar |
| 首石「763 → 761 动作改选」 | 同上第 231、317 行；`docs/coordination/command-board.md` | **已移除**。v2.2.7 起不再改写 `sequence`，`L4D2_OnSelectTankAttackPre` 只记录 |
| `sm plugins list` 显示版本 `2.0.0` | 同上第 296 行 | 当前显示 `2.2.11` |
| `tankdirector_menu` 只接 4 个原生 ConVar | `analysis/tankrun-interface-inventory-20261115/interfaces.md` 第 15 行 | 当前有 55 个自有 ConVar（另 1 个内部桥接）、17 个命令、81 项参数表条目 |
| 编译器 1.12.0.7260 | 同上第 282 行 | v2.1.8 起用 1.12.0.7220 |
| 投石瞄准「原版」以外的描述 | v2.2.5 节 | v2.2.7 起明确 `投石瞄准：原版` |

## 六、为什么保留这份清单

项目的可信度建立在「说清楚做不到什么」。任何新增能力在进入本文件之前，必须先有：

1. 公开接口证据（哪个 native / forward / ConVar）；
2. 源码实现；
3. 编译通过；
4. 至少一条实服日志或玩家验证。

缺任何一条，它就应该继续留在这份清单里。
