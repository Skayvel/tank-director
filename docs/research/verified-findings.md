# 已验证结论

只收录**当前构建的指令/数据直接支持**的有限结论（`D` 级 / `CONFIRMED`）。

**读法约定**

- 「已验证」指静态验证，**不是**运行时验证。
- 每条都绑定构建：`server.dll` SHA256 `a93ec916a8f26fbea85688c321d2ba8dd346d435fab09a0b926dd950c2e1ea12`，首选基址 `0x10000000`。
- 「约定」指该命题在报告声明的范围内成立，不是全局规则。

---

## 1. 状态与计数

### 1.1 Body 状态原语

| 命题 | 证据 | 出处 |
|---|---|---|
| setter 只做「取参数 → 写 `this+0x1c` → 返回」，没有调用事件、计时器或目标选择 | `0x104b9c90` 的 `MOV [ECX+0x1c],EAX` | `docs/tank-behavior-map.md` 第 41–45 行 |
| equality 只比较 `+0x1c` 并返回布尔 | `0x104b9cb0` 的 `CMP/SETZ` | 同上 |
| 共享 Spawn 提交 Body 值 2，Tank override 随后提交 0 | C-SPAWN-BODY / C-SHARED-SPAWN-BODY | 同上 |
| Director 计数路径测试 Body 是否等于 0，为真则跳过该 Tank 的计数增一 | `0x102744a1..0x102744af` | 同上 |

**不能推出**：这个状态叫「休眠/中立」；也不能推出 setter 名字暗示一整套行为。**状态值的后果取决于读取它的消费者。**

### 1.2 Director Tank 计数

| 命题 | 证据 | 出处 |
|---|---|---|
| 计数字段是 `[0x1083b1b8]+0x1c0` | —— | `analysis/D03-Tank-count-lifecycle.md` 第 7 行 |
| `0x102215d0`（GetInfectedStats）把它复制到脚本 table 的 `Tanks` 键 | —— | 同上第 11 行 |
| `0x10358f70`（新建 Tank）在 limit 非负时要求 `count < limit` | —— | `analysis/D03-ZSpawn-generation.md` 第 15、37 行 |
| `0x102707e0` 对 team3/class8 且 count 非零执行减一；零值触发后续 | —— | `analysis/D03-Tank-count-lifecycle.md` 第 14 行 |
| 该字段由 Update 重建，也有重置与事件减一 | —— | 同上 |

**不能推出**：这个字段是所有存活 Tank 实体的即时总数。报告明确要求把它与实体数、Body 状态分开观测。

### 1.3 新建数量门的 limit 来源

`0x10358f70` 的 limit 依次来自：

1. 符合条件的 `cm_TankLimit`；
2. 另一个 Director 子供应器（`Director+0x640` 路径）；
3. `TankLimit` 回退。

**不能推出**：所有复用/地图入口都共用这个门（`docs/checkpoint-20261112.md` 第 7 行明确否定）。

---

## 2. 生成链

| 命题 | 证据 | 出处 |
|---|---|---|
| 脚本 ZSpawn 入口 `0x1035fa60` | —— | `analysis/D03-ZSpawn-generation.md` 第 9 行 |
| 缺少 pos 且 `type == 8` 时走自动选位 `0x1035eae0` | —— | 同上第 10 行 |
| 自动选位先收集并缓存区域 `0x1035d480`，再经位置过滤 `0x1035a160` | —— | 同上 |
| 标准 `ZSpawn({type=8})` 传入 `(8, 5, 0, &position)` | —— | `analysis/D04-ZSpawn-position.md` |
| 自动选位函数有 24 个 CFG 基本块、18 条条件跳转，两条后继均登记 | —— | 同上第 7 行 |
| 区域抽样允许重复；每轮第一次校验失败后满足 flag 条件可抬高 18 再校验，最多 10 次校验；晚期 fallback 在第 5 轮（不是第 4 轮） | —— | `docs/checkpoint-20261112.md` 第 9 行 |
| 脚本有三种直接请求来源（普通 / 警报 / 牺牲终局生成器），共 4 个 ZSpawn 调用点 | —— | 同上第 10 行 |
| 第二只请求没有再次做脚本计数预检，返回值被忽略，但下游新建门仍可能拒绝 | —— | 同上 |
| 首只成功后普通时钟照样推进——不能把「双出」当成两只都已成功 | —— | 同上 |

---

## 3. tankrun 原版脚本

| 命题 | 证据 |
|---|---|
| 解码文本 SHA256 `25ace13c92282a8733530afd7a105facd230c32b345e154e61c636bacaf7bc59`，520 行 | `analysis/D05-tankrun-branch-audit.md` |
| 原始 `tankrun.nuc` 33,644 字节，SHA256 `c311702c906321f0c513db3a4dc96b138ee4934bd10d16263a2323d128d8461e` | 同上 |
| 人工审阅覆盖 28 个函数、111 个显式控制 token、18 个短路节点、4 个 ZSpawn 调用点，`unreviewed_explicit_control_tokens = 0` | `indexes/status.json` 的 `script_scope.manual_review` |
| 原生调用边**由脚本审计闭合的数量为 0** | 同上，`native_edges_closed_by_script_audit: 0` |

最后一条很重要：脚本层审阅完整，不代表任何原生边被闭合。

---

## 4. 梯与瞄准

| 命题 | 证据 | 出处 |
|---|---|---|
| 六态 dispatcher 即使得到下一状态 0，当次仍返回 true 并跳过共享跳/爬块；下一次更新才进入默认路径 | —— | `docs/checkpoint-20261112.md` 第 11 行 |
| 优先级 4 的 Body 请求与目标句柄是不同控制层，不能从朝向变化判断换锁 | —— | 同上 |
| 投石定点瞄准请求为 priority 3、持续 4.0 秒；普通跟人为 priority 2、持续 0.2 秒；控制器会拒绝仍在有效期内的较低优先级请求 | —— | `analysis/T02-T03-baseline.md` 第 2 节 |
| 普通目标选择有约 0.3 秒节流 | —— | 同上 |
| 候选比旧目标近超过 200 游戏单位且通过可见性检查时有切换分支 | —— | 同上 |
| 候选可见而旧目标不可见时也有独立切换分支，**不需要**满足 200 差值 | —— | 同上 |

这些常量属于**这些局部路径**，不是全局锁定规则。

---

## 5. CTankClaw 与伤害

| 命题 | 证据 | 出处 |
|---|---|---|
| 普通主动起拳读取**固定的 50.0**，与 Vision 目标的碰撞边界距离严格小于 50 才起手；已核指令**没有**从 `tank_attack_range` 取值 | —— | `analysis/parameter-control/M07-01/report.md` |
| `tank_swing_range` 有四处已识别的武器读取点，能改变攻击选型、候选查询与挥击初始扫掠点 | —— | 同上 |
| 正常第一次 Sweep 的候选筛选使用全局 `tank_swing_range`，经临时对象传递并做开平方后的线性距离比较 | —— | `analysis/CL-SUP-003/report.md` 第 5 行 |
| `0x1024fa80` 的后半段被一个直接调用的常量假门截断 | —— | 同上 |
| `AllowTakeDamage` 名称 → 句柄 → 同一宿主对象 → 实际调用现场已连起 | —— | `analysis/CL-SUP-005/report.md` |
| pummel 与 pounce 关联目标已分开；**减法前的失能替代路径**与**减法后的临时生命值救回路径**已区分 | —— | 同上 |
| 死亡链新增直接的生命状态写入（`f0 = 1`） | —— | 同上 |
| 普通拳击 → 玩家生命值写入的静态路径已连接：server `10041c80`，写入指令 `10041e08` | —— | `analysis/claw-phase2-20261114/report.md`（CL-SUP-004）第 5 行 |

**不能推出**：`weapon+1574` 是 Tank 的实际拳距控制字段；命中登记等于扣血；一个字段就能设置实际拳距。

---

## 6. 卡住 / 自伤 / 条件性死亡

以下来自 `indexes/status.json` 的 `latest_research_round`（本轮 `tank-stuck-cvar-re`）：

| 命题 |
|---|
| 七个 `tank_stuck_*` / `tank_stasis_*` ConVar 的原生构造与静态 CRT 初始化已连起 |
| `TankAttack 104731c0` 在被求值的分支上读取这些 CVar 缓存 |
| `OnStuck` 与 `OnMoveToFailure` 启动**同一个**计时器；成功/unstuck 会重置它 |
| 无历史的 Vision 哨兵值允许在不等默认 15 的情况下触发卡住自杀 |
| 物理监控使用 3D 位移 100 与 `100/((speedScalar+1)*0.1)` |
| Tank 自伤的信息把 attacker/inflictor 都指向自己；type 2 的 amount 是当前 HP |
| Tank 专属的 490 与 12c override 绑定以及 `tank_killed` 生产者已恢复 |
| `f1` 的伤害字节与 `f0` 的生命状态是不同字段；**最终清理未声明** |

**不能推出**：计时开始能区分物理卡住与寻路失败；`tank_killed` 能证明具体死因，更不能证明实体已删除。

---

## 7. 投石 ConVar 的消费点（PC01）

| 命题 | 出处 |
|---|---|
| `z_tank_throw_interval` 的消费者：Activate `10367b60`、Update `10369590`、clear `10367d90` → setter `10360e00` | `analysis/tankrun-interface-inventory-20261115/interfaces.md` 第 195 行 |
| 激活时设 `interval + 1`；特定中断设 `interval`；clear 设 `max(interval, param)`；自然结束在 Update 内**不重设** | 同上第 196 行 |
| `10367809` 只属于 rock 效果读取，不是间隔设置 | 同上 |

---

## 8. 平台与容量（客户端侧）

| 命题 | 证据 |
|---|---|
| 同构建 `client.dll` 循环在 `0x1008ac14` 比较 `slot - 1` 与 `0x1f` | `docs/tankrun-capacity.md` 第 5 行 |
| 槽位 33 时经 `0x1008ac79` 进入 `tier0.dll!Error`，报错 `UpdatePlayerName with bogus slot` | 同上 |
| 越界检查**先于**普通玩家信息查询，因此空槽也会触发 | 同上 |
| `client.dll` SHA256 `c0a8e1e88f7312db11bcfd4b0ec574d224a34b2d9a615d6088b28335eeb4af1f` | `indexes/artifacts.json` |
| L4DToolZ 2.5.1 上游也建议实际槽位最高配置为 31 | 上游 README（外部来源） |

---

## 9. Left4DHooks 接口的静态事实（`API` 级）

| 命题 | 出处 |
|---|---|
| `L4D2_OnSelectTankAttackPre` 的 `sequence` 参数在 Pre 阶段实际是 ACT 枚举 | `interfaces.md` 第 448 行 |
| `L4D_OnCThrowActivate` 只传 ability 实体，不提供目标、失败原因或强制释放参数 | 同上第 450 行 |
| `L4D_TankRock_OnRelease` 的 `Plugin_Handled` 阻止分支在该版本被注释掉 | 同上第 452 行 |
| `L4D_TankClaw_DoSwing_Pre/Post` 签名为 `void`，不能返回 Handled 阻止挥击 | 同上第 456 行 |
| `L4D_OnSpawnTank` 该版本不提供向量 CopyBack，只能放行/否决 | 同上第 458 行 |
| `L4D_OnGetRandomPZSpawnPosition` 在该版本未注册/实现被注释 | 同上第 459 行 |

这些是**接口能力边界**的静态事实，是插件「不实现某些功能」的直接原因。

---

## 10. 没有一条达到运行验证

`indexes/status.json`：

```json
"user_validated_functions": 0,
"global_coverage_percent": null,
"discovery_frontier_closed": false
```

**本页没有任何一条来自游戏内观测。** 全部是静态证据。玩家验证结果见 [实验性结论](experimental-findings.md) 与 `docs/user-guide/validation-playbook.md` 的记录格式。
