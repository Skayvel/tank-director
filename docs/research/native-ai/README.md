# 原生机制导航

按机制组织的入口。每一节回答三个问题：**研究了什么、确认了什么、还缺什么**。

结论的可靠性分级见 [研究方法](../method.md)；本页只做导航，不重复完整证据链。

---

## 1. Director 与生成

**研究文件**：`analysis/D01-D02-update-tempo.md`、`D03-ZSpawn-generation.md`、`D03-Tank-count-lifecycle.md`、`D04-ZSpawn-position.md`、`D05-tankrun-branch-audit.md`

| 环节 | 入口 | 状态 |
|---|---|---|
| Director Update 四态节奏 | `0x10266b50`（16 个条件跳转、4 个 switch case） | 局部已解释；`LockTempo` 选项已确认，全部回退谓词仍开放 |
| Tank 计数 | `[0x1083b1b8]+0x1c0` | 重建/清零/减一已连；四个事件调用来源身份未定 |
| 新建数量门 | `0x10358f70` | limit 三级来源已恢复；不证明所有入口共用 |
| 脚本 ZSpawn | `0x1035fa60` | 已连 |
| 自动选位 | `0x1035eae0` → `0x1035d480` + `0x1035a160` | 24 块、18 条件已登记 |
| tankrun 脚本 | 解码文本 SHA256 `25ace13c…` | 28 函数/111 token/4 ZSpawn 点已人工审阅；原生边闭合数 **0** |

**最重要的边界**：Director 的计数、实体数、Body 状态、开始追人，是四件必须分开观测的事。

---

## 2. Action 与调度

**研究文件**：`analysis/tank-action-dispatch/report.md`、`docs/tank-behavior-map.md`

| 环节 | 状态 |
|---|---|
| 卡住通知：Locomotion 组件 → NextBot 广播 → Intention → Behavior → 根 Action | 静态验收通过 |
| 调度器消费攻击 Update 返回值 3 | 已追到清理本次攻击动作、恢复判定、写回父层指针 |
| Action 事件转发与挂起/恢复回调 | 部分 |
| 全帧更新顺序 | **未恢复** |

**最重要的边界**：目标、Body 瞄准、路径目标、武器状态是四层，一层变不自动等于其它层变。一个请求被拒不会自动取消另外三条。

---

## 3. 感知、目标与瞄准

**研究文件**：`analysis/T02-T03-baseline.md`、`T03-T04-aim-override.md`、`analysis/parameter-control/stage3a/vision/module.md`

| 环节 | 状态 |
|---|---|
| 普通目标选择 | 约 0.3 秒节流；200 单位差值分支；独立可见性分支；旧目标失能/可移动者优先 |
| 机枪使用者路径 | 查找先于普通选择；该查找无距离或视线判断 |
| 接触换人 | 独立事件路径 + IT/胆汁保护例外 |
| 命令（电锯广播）路径 | 已追到范围广播 → 写目标；上层全部状态未闭合 |
| Vision 首次获取 | `ZombieBotVision.Update` + `+148` 生产者；标准 Spawn/Reset 会写 `-1` |
| Body 瞄准 | 投石 priority 3 / 4.0 秒；普通跟人 priority 2 / 0.2 秒；高优先级会拒绝低优先级 |

**最重要的边界**：`IsAbleToSee` 在 Vision 缓存 Body 字段 `+14` 为空时**直接返回 true**。因此「Tank 有一个 Body 实体」不等于「LOS 条件成立」。

---

## 4. 拳击（CTankClaw）

**研究文件**：`analysis/claw-phase1-20261114/`、`claw-phase2-20261114/`、`CL-SUP-002/003/005/`、`analysis/claw-supplement-r1/`、`analysis/parameter-control/M07-01/`

| 环节 | 状态 |
|---|---|
| 起拳入口 | 普通近战 Update `104743ae`；障碍/接触 `104743d0` 内 `10474627` |
| 起拳距离门 | 普通主动起拳用**固定 50.0** 比较碰撞边界距离；不读 `tank_attack_range` |
| 武器入口 | `CTankClaw::PrimaryAttack` `0x103e3860`（gamedata 签名即此处） |
| 挥击初始扫掠点 | 用全局 `tank_swing_range`，四处读取点 |
| Sweep / 过滤 / 命中登记 | 已恢复边界 |
| 伤害写入 | server `10041c80`，写入 `10041e08` |
| 脚本门 / 关联目标 / 生命状态 | CL-SUP-005 已分开 |
| 完整执行链 | **未闭合** |

**最重要的边界**：命中登记不保证扣血；`weapon+1574` 不能当拳距字段；打空、命中但伤害被拒、同行拳石竞争、已请求未离手是四种不同情况。

---

## 5. 投石（CTankRock）

**研究文件**：`analysis/parameter-control/stage2/execution/`、`stage3a/conditions/`、`stage3a/claw/`

三层必须分开：**AI 决策 → 动画离手 → 物理飞行/命中**。

| 环节 | 状态 |
|---|---|
| 请求成立条件 | 目标可见、距离门、Body 缓存、导演状态（含「特定状态 + 简单/普通难度的前 10 秒门」） |
| 输入竞争 | 主攻击按钮位 1，次攻击位 0x800；同命令可同时存在 |
| 动画与离手 | `Throw_02` 序列 49，原版在 cycle `0.823` 离手（`anim_hulk.mdl` 第 74/90 帧 `AE_ATTACK_END`） |
| 石头实体 | 离手前已由引擎创建；公开离手钩子只用于校正，不能安全取消 |
| `z_tank_throw_interval` | 激活设 `interval+1`；特定中断设 `interval`；clear 设 `max(interval,param)`；自然结束不重设 |
| 物理/命中/冷却链 | **未闭合** |

**最重要的边界**：投石请求成立、动作播放、石头离手是三个不同事件。只有离手是成功判据。

---

## 6. 伤害与死亡

**研究文件**：`analysis/CL-SUP-005/report.md`、`claw-phase2-20261114/report.md`、`analysis/tank-stuck-cvar-re/`

| 环节 | 状态 |
|---|---|
| `AllowTakeDamage` 链路 | 名称 → 句柄 → 同一宿主对象 → 调用现场已连 |
| pummel / pounce 转派 | 已区分 |
| 失能替代 | 减法前的失能替代路径已分开 |
| 临时生命值救回 | 减法后的 buffer rescue 会设 `GoingToDie` 与 HP=1 |
| 死亡链 | 到达显式 `f0 = 1` 生命状态写入 |
| `tank_killed` 事件 | 生产者已恢复 |
| 最终清理（life-state 2 / 尸体 / 实体删除） | **未闭合** |

**最重要的边界**：`tank_killed` 不能证明具体死因，更不能证明实体已删除。而实体是否删除决定槽位是否释放。

---

## 7. 导航、梯与卡住

**研究文件**：`analysis/T04-ladder-state-machine.md`、`analysis/tank-stuck-cvar-re/`

| 环节 | 状态 |
|---|---|
| 六态梯 dispatcher | 条件、回边、退出时序已解释；**退出当帧即使下一状态为 0 仍返回 true 并跳过共享跳爬块** |
| Body 请求优先级 | 梯上/攀爬用 priority 4；与目标句柄是不同控制层 |
| 卡住检测 | 物理监控用 3D 位移 100 与 `100/((speedScalar+1)*0.1)` |
| 卡住/移动失败时钟 | `OnStuck` 与 `OnMoveToFailure` 启动**同一个**计时器；成功/unstuck 重置 |
| 七个 `tank_stuck_*` / `tank_stasis_*` ConVar | 原生构造、缓存读取、分支已连 |
| 自伤 | attacker/inflictor 指向自己；type 2 amount 为当前 HP |
| 追击 NAV 失败生产者 | **未恢复（U02）** |

**最重要的边界**：出生 NAV（Director/ZSpawn 选位）与追击 NAV 是两套东西，进度分别记录。

---

## 8. 怎么继续挖

每个机制页的末尾都能追到具体报告。继续工作时：

1. 先读 `indexes/frontier.json` 与对应模块的 `frontier.json`。
2. 复用已有档案，不重新提取。
3. 一次只闭合一条边，交付「可实现语义」或「明确的下一依赖」。
4. 新结论按 `D`/`I`/`API`/`R`/`U` 标注，并写进 [已验证结论](../verified-findings.md) 或 [未解决问题](../open-questions.md)。
