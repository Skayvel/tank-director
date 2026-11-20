# Tank Run 控制中心 v2.2.8 · 用户指南

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
| 游戏构建 | `server.dll` SHA256 `a93ec916...`（见 [版本表](../reference/versions.md)） |

细节与缺失后果：[依赖要求](../getting-started/requirements.md)。

---

## 3. 安装

```
addons/sourcemod/plugins/tankdirector_menu.smx
addons/sourcemod/gamedata/tankrun_firstrock.txt
```

解压到 `left4dead2` 目录，重启或 `sm plugins load tankdirector_menu`。
`sm plugins list` 应显示 `Tank Run Control Center 2.2.8`。

三种方式（手动 / 插件平台 / 从源码编译）与校验哈希：[安装指南](../getting-started/install.md)。
升级与回滚：[升级与回滚](../getting-started/upgrade-and-rollback.md)。

---

## 4. 五种能力

### 4.1 生成与数量

- **统一调度**：默认每 2 个游戏秒尝试一只，原版 Tank 与内嵌补位 Tank 合计上限 30。
- **五种生成方式**：原生随机区域 / 距离带 / 准星位置 / 脚本 ZSpawn / 幸存者附近原生选位。
- **前置条件**：必须有一名存活真人**确实离开起始安全区**，且原版阶段开放（`TanksDisabled` 为假、`cm_TankLimit > 0`）。
- **满员轮换**：达到上限时处死最早出生的 AI Tank，确认槽位释放后补一只。
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

### 4.3 首石（首次接敌优先投石）

v2.2.7 起的行为：**引擎负责起手、预判与出手方向，插件负责近战保护与真实离手确认。**

- 首次扫描到可见幸存者时开启窗口。
- 窗口内禁止普通起拳，共四层保险：
  1. `OnPlayerRunCmd` 剥掉 `IN_ATTACK` 位
  2. 原生 `CTankClaw::PrimaryAttack` Pre-detour（需 DHooks + 签名匹配）
  3. 出拳动画层拦截
  4. 已进入命中判定时阻止对幸存者的击退
- 攀跳/离地期间暂停超时，稳定落地后恢复完整尝试时间。
- 成功判据只有一条：`L4D_TankRock_OnRelease_Post` 里确认到真实的 `tank_rock` 实体。
- 超时或尝试次数用尽 → 解除禁拳，回退近战。

相关参数：`tankrun_firstrock_enabled` / `_timeout` / `_attempts`。

> **v2.2.7 之前**插件会在 `L4D2_OnSelectTankAttackPre` 里把拳击动作 763 改选为投石动作 761。v2.2.8 **不做**这个改选，也**没有** `tankrun_rock_expert_lock` 参数。读旧文档时注意。

### 4.4 投石与拳击

- **投石总开关**：`tankrun_rock_enabled 0` 会真实拦截 `CThrow` 激活，Tank 无法投石。
- **最小间隔**：`tankrun_rock_min_delay`，按每只 Tank 的真实释放时间计算。
- **力度倍率**：`tankrun_rock_force_scale` 在 `OnRelease` 缩放真实释放速度。
- **拳击伤害倍率**：`tankrun_claw_damage_scale`。
- **拳击命中阻挡**：`tankrun_claw_block_hit`，**风险项**，只影响击退。
- **原生拳击/投石 ConVar**：一组 `tank_*`、`z_tank_*` 参数可读写并读回；其中一部分标注「效果未验证」。

### 4.5 目标策略

- `tankrun_target_mode`：`0` 原生 / `1` 最近 / `2` 最远 / `3` 最低血 / `4` 固定玩家。
- 实现方式是覆盖 `L4D2_OnChooseVictim` 的返回值。
- **只影响经过这个回调的路径**。机枪、接触换人、命令等路径不受影响；Body 朝向与追击路径也不会同步改变。

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
| `sm_tank_params` | 一般管理员 | 列出 57 项参数与当前值 |
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
| `tankrun_auto_double` | `0` | 一次生成两只 |
| `tankrun_spawn_visibility` | `0` | 刷点难度（0 跟随 / 1 遮挡 / 2 前线） |
| `tankrun_spawn_min_dist` | `220` | 最小距离 |
| `tankrun_spawn_max_dist` | `3000` | 最大距离 |
| `tankrun_firstrock_enabled` | `1` | 首石 |
| `tankrun_firstrock_timeout` | `3.0` | 首石窗口超时 |
| `tankrun_firstrock_attempts` | `2` | 首石尝试上限 |
| `tankrun_rock_enabled` | `1` | 允许投石 |
| `tankrun_rock_min_delay` | `0.0` | 投石最小间隔 |
| `tankrun_rock_force_scale` | `1.0` | 投石力度倍率 |
| `tankrun_claw_damage_scale` | `1.0` | 拳击伤害倍率 |
| `tankrun_tank_health` | `0` | 生成时血量（0 不干预） |
| `tankrun_target_mode` | `0` | 目标策略 |
| `tankrun_default_expert` | `1` | 进图默认专家 |

完整 57 项：[参数参考](../reference/api/convars.md)。

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

1. **所有行为级改动都只做过编译与静态检查，游戏内效果由玩家验证。** 这不是免责声明，是版本表的每一行都写着「待实测」。
2. **遮挡刷点在真实地图上的分布未验证。** v2.2.8 只通过了编译与遮挡算例。
3. **首石成功率未知。** 超时是正常结局。
4. **满员轮换在槽位耗尽时的失败路径未实测。**
5. **超过 4 只原生 Tank 的稳定性样本有限。** 历史样本：4 只时中位约 100 tick，出现过长间隔与工坊 VPK I/O 时间重合，但未证明因果。
6. **128 tick 完全没有数据。**
7. **补位 Tank 的移动与动画在客户端可能有跳步/抽搐**，v1.2.0 修过一版，复测待做。

---

## 12. 变更历史

v2.2.8 的关键变化：普通难度用遮挡刷点、高难度用前线刷点，并新增 `tankrun_spawn_visibility` 覆盖。
完整演进与每版验证状态：[版本表](../reference/versions.md)。

原始逐版本技术记录（含具体日志行号与诊断样本）仍保留在 `docs/tankrun-control-center.md`，它是变更记录而不是使用手册。
