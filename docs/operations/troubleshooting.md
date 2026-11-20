# 故障排查

按顺序排查，不要跳步。**每一步都有明确判据。**

## 诊断顺序

```
1. 插件加载了吗
2. 依赖齐了吗
3. 版本对吗
4. 原生钩子装上了吗
5. 状态里有警告吗
6. 是「不生成」还是「生成了但行为不对」
7. 留证据
```

---

## 1. 插件根本没有出现在 `sm plugins list`

| 可能原因 | 判据 | 处理 |
|---|---|---|
| 文件不在正确目录 | `addons/sourcemod/plugins/tankdirector_menu.smx` 是否存在 | 解压到 `left4dead2` 目录，不要解到 `addons/` |
| 扩展缺失 | `sm exts list` 无 Left4DHooks / SDKHooks | 装依赖；插件会因 native 无法解析而加载失败 |
| 引擎不是 L4D2 | 日志有 `Tank Run 综合控制中心仅支持 Left 4 Dead 2` | 换服务器 |
| SourceMod 版本过低 | `sm version` < 1.12 | 升级 SourceMod |

**关键**：查 `addons/sourcemod/logs/errors_*.log`，加载失败的真实原因在那里。

---

## 2. 加载了但状态有「警告：」

### `警告：Left4DHooks 原生不可用`

- 生成、投石、拳击事件全部失效。
- `sm exts list` 看 Left4DHooks 状态；`sm plugins list` 看 `Left 4 DHooks Direct` 是否加载。
- 常见原因：Left4DHooks 版本与游戏构建不匹配。

### `警告：tankrun 模式下脚本接口未连接`

| `sm_tank_status` 显示 | 原因 | 处理 |
|---|---|---|
| `当前模式不是 tankrun` | 模式不对 | `mp_gamemode tankrun` 后换图 |
| `模式脚本未提供 SessionState/SessionOptions` | 官方 Tank Run 脚本没加载 | 确认模式正确且地图执行了模式配置 |
| `地图/实体系统未就绪，脚本探测已推迟` | 地图还没加载完 | 等地图加载完再查 |
| `脚本桥无返回值（logic_script 不可用？）` | 实体创建失败 | 看日志；确认服务器能创建 `logic_script` |
| `脚本执行失败（详见日志）` | 脚本抛异常 | 看日志 `VScript 探测失败：...` |
| `脚本返回值格式不匹配` | 通道被别的插件/人写过 | 检查是否有其它插件也写 `tankrun_bridge` |

---

## 3. 版本不对

`sm plugins list` 显示的不是 `2.2.8`。

| 原因 | 处理 |
|---|---|
| 服务器读的是另一个目录 | 用 `sm plugins info tankdirector_menu` 看实际路径 |
| 覆盖了但没重载 | `sm plugins reload tankdirector_menu`，或重启 |
| 装的是旧包 | 校验 SHA256（[版本表](../reference/versions.md)） |

---

## 4. 首石「看起来没生效」

先看日志这一行：

```
严格首石拳击入口：已启用原生 CTankClaw::PrimaryAttack 拦截
```

| 显示 | 含义 | 处理 |
|---|---|---|
| 已启用 | detour 装上 | 走第 5 步 |
| 签名或扩展不可用 | gamedata 签名不匹配，或 DHooks 缺失 | 确认游戏构建哈希；确认 DHooks 已装；确认 `addons/sourcemod/gamedata/tankrun_firstrock.txt` 存在 |

**注意**：即使降级为「输入 + 动画 + 命中」三层保护，首石仍可能工作——只是第一道防线不存在。不要因为这一行就断定首石完全失效。

### 又看不到「首石窗口」记录

| 检查 | 说明 |
|---|---|
| `tankrun_firstrock_enabled` | 必须为 1 |
| 首次接敌检测 | 需要 Tank 真正看到幸存者；检查 `tankrun_target_scan` |
| `tankrun_ai_only` | 玩家控制的 Tank 不参与 |
| Tank 是否为 AI | 菜单分节 8 会显示 |

### 窗口开了但一直不关

| 可能原因 | 说明 |
|---|---|
| 投石一直被拦截 | 看是否有 `投石被拦截：总开关关闭` —— `tankrun_rock_enabled 0` 时首石不可能成功 |
| 中断路径未清理 | 已知未闭合问题（S3A-F08），见 [未解决问题](../research/open-questions.md) |

---

## 5. 不生成

### 5.1 先分清是哪一层

`sm_tank_status` 的输出逐项看：

```
统一生成：在场 0/30（补位 0，开关开）| 真人离开安全区：否 | 阶段：暂停
```

| 字段 | 不满足时怎么办 |
|---|---|
| `开关开` | `tankrun_auto_spawn 1` |
| `真人离开安全区：是` | 让真人真的走出起始安全区 |
| `阶段：开放` | 原版脚本阶段未开放；换图或等阶段推进 |
| `在场 x/y` | `x` 达到 `y` → 满员，看轮换设置 |

### 5.2 三层都通过仍不生成

| 检查 | 判据 |
|---|---|
| 客户端槽位 | 「剩余」是否为 0 |
| 距离范围 | `min` 是否大于 `max`，或范围窄到没有可达点 |
| 遮挡规则 | `tankrun_spawn_visibility 1` 时，开阔地形可能**永远找不到**合格点 |
| NAV 可达 | 换一张图试 |

**最容易被忽略的**：`tankrun_spawn_visibility 1` 在完全开阔的地图上会导致长时间不生成——这是设计行为，不是 bug。

---

## 6. 生成了但行为不对

### 6.1 改参数没效果

| 参数 | 为什么 |
|---|---|
| `tank_attack_range` | 普通起拳用固定 50.0，不读它 |
| 任何标「效果未验证」的参数 | 能写能读回，但消费点未确认 |
| 目标策略 | 只覆盖 `L4D2_OnChooseVictim` 路径 |

看 `sm_tank_params` 确认读回值，再看日志确认回调是否触发。

### 6.2 满员不轮换

| 检查 | 说明 |
|---|---|
| `tankrun_rotate_oldest` | 必须为 1 |
| 槽位是否真的满了 | 满了才触发；没满就继续加 |
| 是否热加载过 | 热加载前已在场的 Tank 出生顺序不可确认，轮换会暂停直到它们离场 |
| 找不到合格刷点 | 遮挡档 + 开阔地形 → 不处死旧 Tank |

### 6.3 石头刚出手就撞墙

日志会有：

```
投石离手后立即碰撞：Tank=N rock=R 耗时=0.0xx 秒
```

这不是插件造成的。原生出手前扫掠会因真人或场景碰撞修正出生点，速度方向保留——贴墙时就会立刻碰撞。

### 6.4 补位 Tank 移动抽搐 / 跳步

已知问题。v1.2.0 修过一版（清除普通步进的 `EF_NOINTERP`），复测待做。见 [容量与席位](../user-guide/capacity-and-seats.md)。

---

## 7. 服务器卡顿

用 Tick Telemetry，不要凭感觉。

```
sm_tank_tick
```

看 `gap_max_ms` 与 `slow_streak`。

| 现象 | 可能原因 | 怎么确认 |
|---|---|---|
| 掉速与工坊 VPK 读写时间重合 | 工坊 I/O（**推测**） | 对齐 `logging_system.log` 时间 |
| 数量增加后持续掉速 | 负载 | 逐档测试（1/4/8/30） |
| 只在首石窗口掉速 | 插件逻辑 | 关 `tankrun_firstrock_enabled` 对照 |

**不要**把「时间重合」写成「因果」。现有样本无法证明具体阻塞函数。

---

## 8. 参数无法恢复

`sm_tank_restore` 失败。

| 原因 | 处理 |
|---|---|
| `configs/tankrun_control_originals.cfg` 丢失 | 只能手工设回 |
| 参数从未被插件改过 | 没有原始值记录，无法恢复 |
| 是脚本参数 | 不在恢复范围；用 `sm_tank_set` 或换图 |
| 功能总开关关闭 | `tankrun_enabled 1` |

---

## 9. 找不到问题就留证据

```
sm plugins list
sm_tank_status
sm_tank_dump
sm exts list
```

加上：

1. 服务器启动参数
2. 地图 / 模式 / 难度 / 真人数量 / tickrate
3. 你做了什么、期望什么、看到什么
4. `logs/tankrun_control.log` 对应时间段（截取）

**不要附**本机绝对路径的链接——别人打不开。

完整的证据清单见 [诊断](../user-guide/diagnostics.md) 第 9 节。
