# 菜单参考

`!tank` 主菜单的 11 个分节。分节编号与源码 `TR_Section` 枚举一一对应（`tr_core.inc:25-39`），菜单里显示为 1–11。

**通用规则**（每个参数子页都适用）：

- 首行显示参数名与当前实际值。
- 参数页固定提供：`−` / `+` / `−10×` / `+10×`、常用值（最多 4 个）、`自定义数值（聊天输入）`、`设为引擎注册默认值`、`恢复初始值`、`返回上一级`。
- 未验证的参数会额外显示 `该参数可修改，但原生实际消费未验证`。
- 引擎参数在本构建未注册时显示 `本构建未注册该 ConVar，无法修改` 且不可点。
- 脚本参数在脚本不可用时显示 `脚本接口当前不可用：需要 tankrun 模式并加载脚本`。
- 每只 Tank 的独立操作（分节 8）需要 Tank 存活且 userid 匹配。

---

## 1. Tank 生成与数量管理（SEC_SPAWN）

参数：`tankrun_auto_spawn`、`tankrun_auto_interval`、`tankrun_auto_limit`、`tankrun_rotate_oldest`、`tankrun_auto_double`、`tankrun_spawn_mode`。

动作项：

| 项 | 行为 |
|---|---|
| 立即生成（领先玩家前方） | 在领先真人的前方选位生成一只（统一生成入口） |
| 开启 / 关闭统一自动生成 | 切换 `tankrun_auto_spawn` |
| 立即生成（原生随机区域） | 等价 `sm_tank_spawn native` |
| 立即生成（距离带） | 等价 `sm_tank_spawn band` |
| 立即生成（我的准星位置） | 等价 `sm_tank_spawn aim` |
| 清理全部 AI Tank（含补位） | 等价 `sm_tank_clear` |
| 刷新实际值 | 重新读取全部显示值 |

提示：间隔改成亚秒时，原版脚本仍按约 1 秒周期检查生成，实际频率取决于引擎调度。

---

## 2. Tank 血量、速度与属性（SEC_HEALTH）

参数：`tankrun_tank_health`、`tankrun_tank_maxhealth`、`tankrun_mode_health_sync`、`tankrun_tank_damage_scale`、`tankrun_fire_damage_scale`、`tankrun_speed_scale`。

动作项：`治疗全部 AI Tank 至满血`、`把当前血量设置应用到现有 Tank`、`把当前速度倍率应用到现有 Tank`、`刷新实际值`。

要点：

- `0` 表示不干预（血量与最大生命）。
- 脚本血量同步写 `SessionState.TankHealth`，模式在 `tank_spawn` 时应用；插件在生成后 0.2 秒再次应用。
- 速度倍率只改 AI Tank 的 `m_flMaxSpeed`；引擎状态可能覆盖，插件会再校正。

---

## 3. Tank 普通拳击控制（SEC_CLAW）

`未投石：拳种与范围` 与 `投石期间：拳种与范围` 各有一组多选和距离上限。上勾拳、右勾拳、左勾拳可任意组合。取消全部勾选会禁止该阶段的拳击。两种原版砸地动作沿用游戏选型，也遵守禁拳和距离上限。

| 参数 | 默认值 | 含义 |
|---|---:|---|
| `tankrun_claw_types_idle` | `7` | 未投石拳种。上勾 `1`、右勾 `2`、左勾 `4`，累加多选，`0` 禁拳 |
| `tankrun_claw_types_rock` | `7` | 投石期间拳种，使用同样的编码 |
| `tankrun_claw_range_idle` | `0` | 未投石时，Tank 与幸存者三维碰撞边界之间的最大允许距离 |
| `tankrun_claw_range_rock` | `0` | 投石期间的距离上限，`0` 保持游戏原版范围 |

距离上限限制起拳、伤害和击退，使用完整 XYZ 距离。它可以缩短实际可命中的范围，游戏原有的起拳条件和武器碰撞依然决定可达距离。首石优先的禁拳规则在它自己的窗口内继续生效。

插件参数：`tankrun_claw_damage_scale`、`tankrun_claw_block_hit`。
原生参数：`tank_swing_range`、`tank_attack_range`、`tank_fist_radius`、`tank_swing_interval`、`tank_swing_fast_interval`、`tank_swing_miss_interval`、`tank_windup_time`、`tank_swing_duration`、`z_tank_attack_interval`。

动作项：`查看拳击统计与最近事件`、`刷新实际值`。

**重点**：`tank_attack_range` 在已审的原生 AI 起拳分支里**没有被读取**——普通主动起拳用固定 `50.0` 比较碰撞边界距离（M07-01 报告，见研究仓库 `analysis/parameter-control/M07-01/report.md`）。所以把它设成 100 不一定让 Tank 从更远处起拳。`tank_swing_range` 有四处武器侧读取点，会影响攻击选型、候选查询和挥击初始扫掠点。

`tankrun_claw_block_hit` 是风险项：让 `OnPlayerHit_Pre` 返回 `Handled`，只保证阻止击退，**不保证**伤害也被算清。

---

## 4. Tank 投石控制（SEC_ROCK）

`投石类型与视角锁定` 可以多选下勾斜抛、单手过肩、腰侧、双手过顶高抛，也可以全部取消以禁止 AI 投石。进入 `分别设置四种投石视角`，每种都可选原版跟随、锁定抬手前视角或锁定抬手后视角。开启 `不改变视角` 时，已勾选的抛法统一使用抬手前视角。各类型设置保留，关闭统一开关后继续使用。

| 参数 | 默认值 | 含义 |
|---|---:|---|
| `tankrun_rock_types` | `15` | 下勾 `1`、单手过肩 `2`、腰侧 `4`、双手过顶 `8`，累加多选，`0` 禁投 |
| `tankrun_rock_no_turn` | `0` | `1` 为不改变视角，统一使用抬手前视角 |
| `tankrun_rock_view_undercut`、`tankrun_rock_view_onehand`、`tankrun_rock_view_hip`、`tankrun_rock_view_twohand` | `0` | `0` 原版跟随，`1` 抬手前，`2` 抬手后 |
| `tankrun_rock_recovery` | `0.75` | 真实离手、投石动画中断或起手超时后，继续保持投石阶段的秒数 |

投石拳击规则从原版抬手事件开始，持续到恢复时间结束。锁定启用时，Tank 在该窗口保持俯仰和朝向，恢复时间结束后以每秒最多 180° 逐步恢复转向。石头沿锁定的水平朝向出手，保留当次原版释放向量的竖直分量，再应用力度倍率。这里的投石类型指原版抛掷动作。两套配置仅作用于原生 AI Tank 的攻击路径，游戏内行为见[验收剧本](validation-playbook.md)。

插件参数：`tankrun_rock_enabled`、`tankrun_rock_min_delay`、`tankrun_rock_force_scale`。
原生参数：`z_tank_throw_interval`、`tank_throw_min_interval`、`z_tank_throw_fail_interval`、`tank_throw_allow_range`、`z_tank_throw_force`、`tank_throw_lead_time_factor`、`tank_throw_loft_rate`、`tank_throw_max_loft_angle`、`tank_throw_aim_error`、`tank_rock_overhead_percent`。

动作项：`查看投石统计与最近事件`、`刷新 Tank Run 脚本实际值`、`强制投石：无可靠公开接口（需 GPT 深度研究）`（**禁用项**）、`刷新实际值`。

要点：

- `z_tank_throw_interval` 与 `tank_throw_min_interval` **不是同一个控制点**：前者是执行冷却，后者是 AI 请求节流。
- Tank Run 模式可能把 `z_tank_throw_interval` 覆盖成极大值，以实际读回为准。
- 「强制投石」永远禁用。菜单保留这一行是为了明确告知不支持。

---

## 5. Tank 目标与行为策略（SEC_TARGET）

参数：`tankrun_target_mode`、`tankrun_target_fixed`、`tankrun_target_scan`、`tankrun_firstrock_enabled`、`tankrun_firstrock_timeout`、`tankrun_firstrock_attempts`、`tankrun_ai_only`。

动作项：`查看每个 Tank 的接敌与策略状态`、`刷新实际值`。

要点：

- 目标策略只覆盖 `L4D2_OnChooseVictim` 路径，标为「效果未验证」。
- 首石窗口超时在**离地期间暂停**；稳定落地后恢复完整时间。
- `tankrun_ai_only 1` 时玩家控制的 Tank 只观察不修改。

---

## 6. Tank 生成位置与距离（SEC_POS）

参数：`tankrun_spawn_visibility`、`tankrun_spawn_min_dist`、`tankrun_spawn_max_dist`、`tankrun_spawn_attempts`、`tankrun_spawn_fallback`。

动作项：`刷新 Tank Run 脚本实际值`、`刷新实际值`。

要点：

- 距离范围会被均分为近/中/远三段，每段三等分。
- 默认 220–3000 → 三段约 `220–1147` / `1147–2073` / `2073–3000`。
- 手动生成、自动单只、自动双只、满员补位**共用**同一套选位与遮挡规则。

详见 [生成方式与选位](spawn-modes.md)。

---

## 7. AI Director 与游戏规则（SEC_RULES）

原生参数：`z_spawn_range`、`z_spawn_safety_range`、`z_finale_spawn_tank_safety_range`、`nb_allow_avoiding`、`nb_allow_climbing`。
脚本参数：`tankrun_script_spawn_interval`、`tankrun_script_tank_limit`、`tankrun_script_double_tanks`、`tankrun_script_tank_health`。

动作项：`刷新实际值` 与脚本状态行。

要点：

- `z_spawn_range` **不是** VScript 自动生成的最大距离。
- `z_spawn_safety_range` 只影响局部门，不是统一最小距离。
- `tankrun_script_tank_limit = 0` 会让原版阶段暂停生成。

---

## 8. 当前 Tank 列表与独立控制（SEC_INSTANCE）

列表每行显示一只 AI Tank（key 为 `t:<userid>`）。顶部动作：

| 项 | 行为 |
|---|---|
| 处死最早出生的 AI Tank | 等价 `sm_tank_kill oldest` |
| 处死最后出生的 AI Tank | 等价 `sm_tank_kill newest` |
| 清理全部 AI Tank | 等价 `sm_tank_clear` |
| 返回主菜单 | — |

单只 Tank 详情页：

| 项 | 行为 |
|---|---|
| 处死该 Tank | 立即处死并等待确认退场 |
| 治疗至满血 | 恢复当前最大生命 |
| 设置血量（聊天输入） | 单独设置这一只 |
| 应用当前血量策略 | 用全局策略覆盖这一只 |
| 重新开启首石窗口 | 清空该 Tank 的首石状态并重新开窗 |
| 切换首石策略停用状态 | 手动停用/恢复该 Tank 的首石 |
| 重置该 Tank 统计 | 清零计数，不影响其它 Tank |
| 返回 Tank 列表 | — |

若当前没有 Tank，列表显示 `当前没有可控制的 Tank`（禁用项）。

**热加载限制**：热加载前已在场的 Tank 出生顺序不可确认，`oldest`/`newest` 会被拒绝，但仍可从列表选中具体 Tank 操作。

---

## 9. 参数预设与配置保存（SEC_PRESET）

| 项 | 行为 |
|---|---|
| 8 个槽位 | 显示占用与名称；点进可 应用 / 保存（聊天输入名称）/ 重命名 / 删除 |
| 恢复全部参数初始值（含插件参数） | 等价 `sm_tank_restore all` |
| 保存当前插件设置到文件 | 写 `configs/tankrun_control_settings.cfg` |
| 手动加载插件设置文件 | 读回上述文件 |
| 返回主菜单 | — |

预设只保存 `TRK_PLUGIN` 类参数；引擎参数与脚本参数不写入。详见 [预设与持久化](presets.md)。

---

## 10. 实时监控与调试（SEC_MONITOR）

| 项 | 行为 |
|---|---|
| 每只 Tank 实时行 | 血量、投石/拳击次数、最近事件、首石窗口状态 |
| 调试输出开关 | 切换 `tankrun_debug` |
| 事件日志开关 | 切换 `tankrun_log_events` |
| 导出诊断文件（供后续分析） | 等价 `sm_tank_dump` |
| 返回主菜单 | — |

---

## 11. 插件状态、兼容性与帮助（SEC_STATUS）

| 项 | 行为 |
|---|---|
| 功能总开关 | 切换 `tankrun_enabled` |
| 在聊天栏输出完整状态 | 等价 `sm_tank_status` |
| 重新探测依赖与脚本 | 重跑 `TR_ProbeDependencies()` |
| 查看帮助与命令 | 与 `sm_tank_help` 相同内容 |
| 恢复全部参数初始值（含插件参数） | 与分节 9 同一动作 |
| 导出诊断文件 | 与分节 10 同一动作 |
| 返回主菜单 | — |

关闭功能总开关后菜单仍可打开，但所有策略不生效；写操作受 `TR_CanEdit`（要求 `TR_IsAdmin` 且功能开启）限制。
