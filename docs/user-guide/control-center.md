# Tank Run 控制中心 v2.2.11 · 用户指南

面向服主与管理员的完整使用说明。读完这一篇就能装、能配、能验。细节分散在相邻文档里，本文给出入口。

---

## 1. 它是什么

《Left 4 Dead 2》**Tank Run** 模式的服务端插件。原版 Tank Run 由 Valve 脚本决定 Tank 何时出现、刷在哪里；本插件在保留原版阶段与规则的前提下，接管生成调度与选位，并提供一组可读回、可恢复、可关闭的参数。

玩家用**原版客户端**加入，不需要装任何东西。

### 它不做什么

- 不重写 Tank AI。感知、目标、追击、挥拳、投石的执行仍由引擎负责。
- 不强制投石。没有「按下必投」这种公开接口。
- 不改变原版的阶段门、终局判定、安全区规则。

完整否定清单见 [未实现能力与硬限制](../reference/api/limits.md)。

---

## 2. 依赖与平台

| 项 | 要求 |
|---|---|
| 平台 | Windows（原生禁拳钩子只有 Windows 签名；其它功能跨平台） |
| 游戏 | Left 4 Dead 2，Tank Run 模式 |
| SourceMod | **1.12** |
| 必需扩展 | Left4DHooks、SDKHooks、SDKTools |
| 可选 | DHooks（原生禁拳）、官方 Tank Run 脚本（脚本字段）、L4DToolZ（槽位扩展） |
| Tick 基线 | **100 tick**；128 tick 未验证 |
| 游戏构建 | `server.dll` 基线 `a93ec916…`；更新版 `6c543884…` 签名同样 1/1 唯一命中（见 [版本表](../reference/versions.md)） |

细节与缺失后果：[依赖要求](../getting-started/requirements.md)。

---

## 3. 安装

```
addons/sourcemod/plugins/tankdirector_menu.smx
addons/sourcemod/gamedata/tankrun_firstrock.txt
```

解压到 `left4dead2` 目录，重启或 `sm plugins load tankdirector_menu`。
`sm plugins list` 应显示 `Tank Run Control Center 2.2.11`。

三种方式（手动 / 插件平台 / 从源码编译）与校验哈希：[安装指南](../getting-started/install.md)。
升级与回滚：[升级与回滚](../getting-started/upgrade-and-rollback.md)。

---

## 4. 五种能力

### 4.1 生成与数量

- **统一调度**：默认每 2 个游戏秒尝试一只，原版 Tank 与内嵌补位 Tank 合计上限 30。
- **五种生成方式**：原生随机区域 / 距离带 / 准星位置 / 脚本 ZSpawn / 幸存者附近原生选位。
- **前置条件**：必须有一名存活真人**确实离开起始安全区**，且原版阶段开放（`TanksDisabled` 为假、`cm_TankLimit > 0`）。
- **满员轮换与次帧槽位回收**：达到上限时处死最早出生的 AI Tank，确认槽位释放后补一只；`tankrun_fast_slot_reclaim`（默认 `1`，仅 Tank Run 生效）在死亡 AI Tank 次帧释放槽位，避免原版 6 秒占槽堆积。
- **自伤与处死豁免**：低 `tankrun_tank_damage_scale` 不再把原生 `tank_stuck_failsafe` 自杀 Tank 变成半血失能活体。
- **双 Tank**：可选一次调度生成两只。

详见 [生成方式与选位](spawn-modes.md)。

### 4.2 生成位置

- **距离范围**：默认 220–3000 单位，范围内均分为近 / 中 / 远三段，随机排定查找顺序，从首个有合格候选的段内随机选点。
- **刷点难度**（`tankrun_spawn_visibility`）：
  - `0` 跟随游戏难度：简单/普通用**遮挡**，高级/专家用**前线**
  - `1` 只用遮挡
  - `2` 只用前线（沿用 v2.2.7）
- **候选来源**：原生随机点、随机半径与角度的地面投影、地图导航点。
- **合格条件**：所有存活生还者的最小距离、领先真人的最远距离、前线进度、NAV 可达性、安全区限制、出生空间检查、（遮挡档）全队视线检查。

### 4.3 首石（严格首石优先投石 · 方案 1b）

v2.2.11 的行为：**出生即严锁拳击，引擎负责投石起手与弹道，插件负责近战保护、近距极速触发原版右键扔饼与真实离手确认。**

- **出生即严锁**：AI Tank 出生即激活首石保护，消除接敌扫描节流间隙漏出「落地拳」的可能。
- **四层禁拳保护**：
  1. `OnPlayerRunCmd` 剥掉 `IN_ATTACK` 位
  2. 原生 `CTankClaw::PrimaryAttack` Pre-detour（需 DHooks + 签名匹配）
  3. 出拳动画层拦截
  4. 已进入命中判定时阻止对幸存者的击退与拳击伤害
- **障碍物独立开关**：`tankrun_firstrock_allow_props`（默认 `0` 全部严锁）；设为 `1` 时仅在远离存活幸存者 `> 130u` 时允许拍打汽车/破门，近身及对人命中仍 100% 拦截。
- **近距极速扔饼**：`tankrun_firstrock_quick_throw`（默认 `1`）与 `tankrun_firstrock_quick_range`（默认 `280`）。当 Tank 脚踩实地面、直线可见存活幸存者、能力无冷却且处于近距内（或近距试图出拳被锁）时，通过 1-tick 交替脉冲注入 `IN_ATTACK2` 快速拉起原版 `CThrow`；远距拍障被拦不误触发，交回原生 AI 高抛预瞄。
- **方案 1b 硬直区分解锁**：
  - 未抬手投石前遭硬直（如炸药桶/榴弹/撞击）：**不消耗首石**，硬直后继续要求扔饼；
  - 已触发 `CThrow` 起手后、石头离手前遭硬直打断（或 3.25 秒无离手且原生冷却卡入长冷却 `> 5s`）：解锁近战防止变木桩。
- **默认禁用时间超时**：`tankrun_firstrock_timeout` 默认 `0.0`（不靠计时超时放拳；如需超时回退可在菜单调回正数秒）。
- 成功判据仍为 `L4D_TankRock_OnRelease_Post` 确认到真实 `tank_rock` 实体。

相关参数：`tankrun_firstrock_enabled` / `_allow_props` / `_quick_throw` / `_quick_range` / `_timeout` / `_attempts`。

> **v2.2.7 之前**插件会在 `L4D2_OnSelectTankAttackPre` 里把拳击动作 763 改选为投石动作 761。当前版本**不做**这个改选，也**没有** `tankrun_rock_expert_lock` 参数。读旧文档时注意。

### 4.4 投石与拳击

- **投石总开关**：`tankrun_rock_enabled 0` 会真实拦截 `CThrow` 激活，Tank 无法投石。
- **最小间隔**：`tankrun_rock_min_delay`，按每只 Tank 的真实释放时间计算。
- **力度倍率**：`tankrun_rock_force_scale` 在 `OnRelease` 缩放真实释放速度。
- 在 `Tank 投石控制 → 投石类型与视角锁定` 勾选原版抛法并分别设置视角。`不改变视角` 对所有已选抛法统一锁定抬手前视角；`tankrun_rock_keep_loft`（默认 `1`）仅锁水平 Yaw 保留引擎 Pitch 抬角防砸脚；`tankrun_rock_after_delay`（默认 `0.35s`）避免抬手后模式首帧锁住半途残角。
- 在 `Tank 普通拳击控制` 的两个阶段分别勾选上勾、右勾、左勾，并设置允许拳击的最远距离（原版砸地动作序列 `46/47` 受保护不改写为站立拳，且仍遵守禁拳与距离上限）。投石抬手至离手或中断后的保持期使用投石期间设置。[菜单与参数参考](menu-reference.md)
- **拳击伤害倍率**：`tankrun_claw_damage_scale`。
- **拳击命中阻挡**：`tankrun_claw_block_hit`，**风险项**，只影响击退。
- **原生拳击/投石 ConVar**：一组 `tank_*`、`z_tank_*` 参数可读写并读回；部分原生消费点已在静态分析中实证。

### 4.5 目标策略

- `tankrun_target_mode`：`0` 原生 / `1` 最近 / `2` 最远 / `3` 最低血（含临时血） / `4` 固定玩家。
- `tankrun_target_skip_incap`（默认 `1`）：优先跳过倒地、挂边及 4 类特感控制中的幸存者，避免与原生首石资格冲突；全员倒地或单人倒地时自动回退。
- `tankrun_target_hysteresis`（默认 `200`）：最近/最远策略需优于当前目标该距离才切换，减少频繁切目标。
- 实现方式是覆盖 `L4D2_OnChooseVictim` 的返回值，**只影响经过这个回调的路径**。机枪、接触换人、命令等路径不受影响；Body 朝向与追击路径也不会同步改变。

---

## 5. 菜单

管理员聊天输入 `!tank`（或 `!tankmenu`），控制台 `sm_tank`。

```
Tank Run 综合控制中心
 1. Tank 生成与数量管理
 2. Tank 血量、速度与属性
 3. Tank 普通拳击控制
 4. Tank 投石控制
 5. Tank 目标与行为策略
 6. Tank 生成位置与距离
 7. AI Director 与游戏规则
 8. 当前 Tank 列表与独立控制
 9. 参数预设与配置保存
10. 实时监控与调试
11. 插件状态、兼容性与帮助
```

行为约定：

- 每项显示**当前实际值**，不是插件记住的值。
- 参数子页显示：当前值、引擎注册默认值、允许范围、参数名；支持 ±步进、±10 倍步进、常用值、设为默认、恢复初始值、聊天输入自定义值。
- 修改后**重新读取并回报**；写入失败会显示实际读回值，而不是报成功。
- 未验证参数标注「（效果未验证）」；做不到的功能显示为禁用项并给原因。
- 普通玩家默认打不开；`tankrun_public_readonly 1` 时可打开**只读**菜单，所有修改项禁用。

逐项说明：[菜单参考](menu-reference.md)。

---

## 6. 命令

| 命令 | 权限 | 用途 |
|---|---|---|
| `sm_tank` / `sm_tankmenu` | 管理员（只读模式放宽） | 打开菜单 |
| `sm_tank_status` | 一般管理员 | 完整状态 |
| `sm_tank_diag` | 一般管理员 | 重探依赖 + 状态 + 写日志 |
| `sm_tank_dump` | 一般管理员 | 导出诊断文件 |
| `sm_tank_params` | 一般管理员 | 列出 81 项参数与当前值 |
| `sm_tank_spawn [native\|band\|aim\|script\|near]` | 配置管理员 | 手动生成 |
| `sm_tank_clear` | 配置管理员 | 清理全部 AI Tank |
| `sm_tank_kill <编号\|oldest\|newest>` | 配置管理员 | 处死指定 Tank |
| `sm_tank_set <参数> <值>` | 配置管理员 | 写入参数 |
| `sm_tank_restore <参数\|all>` | 配置管理员 | 恢复初始值 |
| `sm_tank_preset <save\|load\|delete\|list> [槽位] [名称]` | 配置管理员 | 预设管理 |
| `sm_tank_debug 0/1` | 配置管理员 | 调试输出 |
| `sm_tank_log 0/1` | 配置管理员 | 事件日志 |
| `sm_tank_reload` | 配置管理员 | 重载设置与探测 |
| `sm_tank_help` | 所有人 | 帮助 |
| `sm_zs`（游戏内 `!zs` / `/zs`） | 所有人 | 自杀，只作用于自己 |

完整参数、返回值与失败提示：[命令参考](../reference/api/commands.md)。

---

## 7. 关键参数速查

默认值取自注册值，**实际值请用 `sm_tank_params` 读**。

| 参数 | 默认 | 作用 |
|---|---:|---|
| `tankrun_auto_spawn` | `1` | 统一自动生成开关 |
| `tankrun_auto_interval` | `2.0` | 生成尝试间隔（秒） |
| `tankrun_auto_limit` | `30` | 在场 Tank 总上限 |
| `tankrun_rotate_oldest` | `1` | 满员轮换最早出生 |
| `tankrun_fast_slot_reclaim` | `1` | 死亡 AI Tank 次帧回收槽位（0 原版 / 1 仅 Tank Run / 2 全模式） |
| `tankrun_auto_double` | `0` | 一次生成两只 |
| `tankrun_spawn_visibility` | `0` | 刷点难度（0 跟随 / 1 遮挡 / 2 前线） |
| `tankrun_spawn_min_dist` | `220` | 最小距离 |
| `tankrun_spawn_max_dist` | `3000` | 最大距离 |
| `tankrun_firstrock_enabled` | `1` | 严格首石（出生即锁拳，方案 1b） |
| `tankrun_firstrock_allow_props` | `0` | 首石前拍车/破障（0 全锁 / 1 远离幸存者允许） |
| `tankrun_firstrock_quick_throw` | `1` | 近距禁拳时极速原版扔饼 |
| `tankrun_firstrock_quick_range` | `280` | 极速扔饼触发距离 |
| `tankrun_firstrock_timeout` | `0.0` | 首石窗口超时（0 禁用时间超时） |
| `tankrun_firstrock_attempts` | `2` | 首石尝试上限（仅超时 > 0 启用） |
| `tankrun_rock_enabled` | `1` | 允许投石 |
| `tankrun_rock_min_delay` | `0.0` | 投石最小间隔 |
| `tankrun_rock_force_scale` | `1.0` | 投石力度倍率 |
| `tankrun_rock_keep_loft` | `1` | 锁视角时保留引擎 Pitch 抬角 |
| `tankrun_rock_after_delay` | `0.35` | 抬手后锁角采样延迟（秒） |
| `tankrun_claw_damage_scale` | `1.0` | 拳击伤害倍率 |
| `tankrun_tank_health` | `0` | 生成时血量（0 不干预） |
| `tankrun_target_mode` | `0` | 目标策略 |
| `tankrun_target_skip_incap` | `1` | 目标策略跳过倒地与被控 |
| `tankrun_target_hysteresis` | `200` | 最近/最远策略切换距离滞回 |
| `tankrun_default_expert` | `1` | 进图默认专家 |

完整 81 项：[参数参考](../reference/api/convars.md)。

---

## 8. 部署前必须算清的一件事：客户端槽位

原版 Tank 占**客户端槽位**。所以「上限 30」不等于「能刷 30 只原版」。

| 总槽位 | 其他客户端 | 预留交接 | 原版 Tank 名额 |
|---:|---:|---:|---:|
| 18 | 1 | 0 | 17 |
| 18 | 8 | 0 | 10 |
| 31 | 1 | 0 | 30 |
| 31 | 4 | 0 | 27 |
| 31 | 8 | 0 | 23 |
| 31 | 8 | 1 | 22 |

- 槽位由 L4DToolZ 的 `sv_setmax` 设定；**31 是推荐上限**，32 上游标注有崩溃风险。
- 同构建客户端在槽位 33 触发 `UpdatePlayerName with bogus slot`，即使该槽为空。
- 内嵌补位 Tank 不占客户端槽位，但**不是原版 AI**。

详见 [容量与席位](capacity-and-seats.md)。

---

## 9. 验证

装完先做 [首次运行与验收](../getting-started/quickstart.md) 的 8 步。

要系统验证某一项行为，用 [验收剧本](validation-playbook.md)。核心原则：

- **一次只改一个变量**；
- **用日志判定，不用画面猜**；
- **恢复测试前读数**；
- **记录地图、回合、出生序号、实际参数值**。

---

## 10. 诊断与排障

| 工具 | 用途 |
|---|---|
| `sm_tank_status` | 一眼看状态 |
| `sm_tank_dump` | 留一份完整快照 |
| `logs/tankrun_control.log` | 事件与决策记录 |
| `logs/tankrun_control_dump.txt` | 参数、原值、每 Tank 状态 |
| Tick Telemetry | 排除服务端掉速 |

日志字段说明：[文件与格式](../reference/api/files.md)。
常见故障：[故障排查](../operations/troubleshooting.md)。
Tick 采样怎么读：[诊断](diagnostics.md)。

---

## 11. 已知问题与诚实边界

1. **尚未达到全场景验证。** 核心首石方案 1b 与极速扔饼基线（`v2.2.10`）已在 Steam 本地单人 `c1m2_streets` 完成 145 只 AI Tank 实测，但 `v2.2.11` 远距拍障过滤以及多人专用服、其余地图、复杂策略组合仍**待实测**。
2. **遮挡刷点在真实地图上的分布未验证。**
3. **首石受引擎原生规则约束。** Easy/Normal 难度开局 10 秒存在原生禁投门；已抬手起手但离手前遭硬直打断时会按方案 1b 解锁近战防木桩。
4. **满员轮换在槽位耗尽时的失败路径未实测。**
5. **超过 4 只原生 Tank 的稳定性样本有限。** 历史样本：4 只时中位约 100 tick，出现过长间隔与工坊 VPK I/O 时间重合，但未证明因果。
6. **128 tick 完全没有数据。**
7. **补位 Tank 的移动与动画在客户端可能有跳步/抽搐**，v1.2.0 修过一版，复测待做。

---

## 12. 变更历史

v2.2.11 的关键变化：整合 PR #1 分阶段拳种/距离与四类投石动作/视角控制；升级首石方案 1b（出生即严锁、默认禁时间超时、硬直区分解锁、拍车/障碍物独立开关、近距极速原版扔饼与远距拍障过滤）；新增砸地拳保护、锁视角保留 Pitch 抬角、抬手后延迟采样、死亡槽位次帧回收、自伤豁免、目标跳倒地/滞回与 4 项 `tank_stuck_failsafe` 原生参数。
完整演进与每版验证状态：[版本表](../reference/versions.md)。

原始逐版本技术记录（含具体日志行号与诊断样本）仍保留在 `docs/tankrun-control-center.md`，它是变更记录而不是使用手册。
