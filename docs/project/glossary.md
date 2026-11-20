# 术语表

只收容易混淆的词。数字与版本见 [版本表](../reference/versions.md)。

## 证据与研究

| 术语 | 含义 |
|---|---|
| `D` / `I` / `API` / `R` / `U` | 指令 / 推断 / 接口旁证 / 运行未验证 / 未知 |
| F0–F7 | 函数研究等级，F0 最浅，F7 完成复刻验收 |
| `CONFIRMED` | 当前构建的指令支持某个**有限**结论，不是运行验证 |
| `UNRESOLVED` | 证据不足，需写具体问题与下一步 |
| `RUNTIME_VALIDATED` | 有绑定版本、场景、输入的真实运行记录。**当前为 0** |
| 开放前沿（frontier） | 尚未闭合的函数、事件、虚调用边 |
| 证据 ID / `ev_...` | REA 导出的记录标识 |
| dossier | 单个函数的完整档案（CFG + 伪代码 + 元数据） |
| claims | `indexes/research/claims.json` 里的人工语义结论 |

## 游戏与引擎

| 术语 | 含义 |
|---|---|
| Tank | 游戏中的 Tank 感染者。本文档默认指 **AI Tank**，玩家控制的会单独说明 |
| Director / AI Director | 控制节奏、生成计划、终局阶段的导演系统 |
| ZSpawn | 脚本发起生成的入口 |
| Body | Tank 的瞄准/身体姿态层，与「目标」「移动」是不同层 |
| Action | 行为树里的行动节点（TankAttack、TankBehavior 等） |
| Locomotion | 移动/寻路层 |
| `CTankClaw` | 拳击武器/能力类 |
| `CTankRock` / `CThrow` | 投石能力相关 |
| Tabun / Stasis | 卡住固定状态相关（`tank_stasis_time_suicide`） |
| IT | 胆汁（bile）造成的「被攻击目标」标记 |
| pummel / pounce | Hunter/Charger 等特感的压制动作，与 Tank 伤害转派有关 |
| tick / tickrate | 服务端每秒逻辑帧数。本项目基线 100 tick |
| `GetTickInterval()` | 引擎 tick 间隔，秒。代码里一律用它换算，不写死 tick 数 |

## Tank Run 模式

| 术语 | 含义 |
|---|---|
| Tank Run | 官方突变模式，`mp_gamemode tankrun` |
| `SessionState` / `SessionOptions` | 模式脚本的全局会话表 |
| `SpawnInterval` | 脚本自动生成间隔 |
| `cm_TankLimit` | 模式数量门 |
| `DoubleTanks` | 每批双 Tank |
| `SpawnTankThink` | 脚本自动生成开关 |
| `TanksDisabled` | 阶段禁用标志 |
| 首石 / first rock | 首次接敌时优先使用原生投石的行为/策略 |
| 遮挡刷点 | 要求 Tank 出生时不在任何存活生还者视线内 |
| 前线刷点 | 允许 Tank 在视野内出现，按领先玩家进度选位 |

## 控制中心

| 术语 | 含义 |
|---|---|
| 统一生成 / 统一调度 | v2.2.0 起接管原版生成请求的单一调度器 |
| 原版 Tank | 由引擎创建、占客户端槽位、使用原生 AI 的 Tank |
| 补位 Tank / replica | 由插件创建的服务端实体，不占客户端槽位，行为是脚本模拟 |
| 槽位 / `MaxClients` | 服务器客户端上限，由 `sv_setmax` 设定 |
| 交接保留 | 为玩家重连/入队预留的槽位 |
| 原始值 / 注册默认 / 实际值 | 插件首次修改前的值 / ConVar 注册字符串 / 此刻真实值 |
| 效果未验证 | 参数能改能读回，但原生消费点未确认 |
| 首石窗口 | 首次接敌后维持近战保护、等待真实投石离手的时间段 |

## 容易混淆的三组

### 1. 出生 NAV vs 追击 NAV

- **出生 NAV**：Director/ZSpawn 决定 Tank 出现在哪里。
- **追击 NAV**：Tank 决定怎么走到目标那里。

两者可能共享引擎 NAV 服务，但用途、输入、过滤、输出完全不同。项目分开记录进度。

### 2. 目标 vs 瞄准 vs 移动 vs 武器

- **目标**：追谁（Action 目标句柄）
- **瞄准**：朝向哪里（Body 请求）
- **移动**：走哪条路（路径目标）
- **武器**：能不能打到（武器状态与判定）

一个变了不代表其它三个变。一个请求被拒不会自动取消另外三个。

### 3. 请求 vs 激活 vs 离手

投石的三个不同事件：

1. **请求**：AI 决定投（按下副攻击）
2. **激活**：能力被引擎接受，开始播放动画
3. **离手**：石头实体真正飞出

**只有 3 是成功判据。** 日志里的「已请求」「已激活」都不等于投出来了。
