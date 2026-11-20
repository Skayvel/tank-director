# 64 位兼容层设计

> ## ⚠ 这是设计，不是实现
>
> - 本仓库**没有任何一行** 64 位适配代码。
> - 本仓库**没有**做过 64 位逆向，也没有 64 位游戏构建可供验证。
> - 下文的接口名是**建议命名**，不是已实现的符号；不要引用为 API。
>
> 本文的作用：把「将来适配 64 位时要改哪里」收敛到一个明确的边界，避免到时候散落在整个插件里。

相关文档：[32 位基线](32-bit-baseline.md) · [64 位计划](64-bit-plan.md) · [接口稳定性分级](../reference/api/stability.md)

---

## 1. 为什么需要这一层

先看事实。当前插件对原生地址的依赖**只有一处**：

| 依赖 | 位置 | 32 位绑定 |
|---|---|---|
| `TankRun::CTankClaw::PrimaryAttack` 的 Pre-detour | `scripting/tankrun/tr_rock.inc:27-48` | `gamedata/tankrun_firstrock.txt`，x86 字节签名 + `CallConv_THISCALL` |

其余部分都不碰地址：

| 机制 | 为什么与架构无关 |
|---|---|
| 生成 / 选位 | 走 Left4DHooks native 与 SourceMod NAV 封装 |
| 投石 / 拳击事件 | 走 Left4DHooks forward |
| 血量、速度 | 走 SourceMod netprops（`m_iMaxHealth`、`m_flMaxSpeed`），由 SourceMod 解析偏移 |
| 伤害倍率 | 走 `SDKHook_OnTakeDamage` |
| 脚本字段 | 走实体 + `RunScriptCode`（VScript） |
| 菜单、命令、配置、日志 | 纯 SourcePawn 与文本 |

**结论**：64 位适配的真实工作面，就是「这一处 detour + 它的签名」。所以值得为它单独设一层边界。

---

## 2. 目标与非目标

### 目标

1. 把原生依赖收敛到**单一模块**，插件其余代码不出现 `DynamicDetour`、`GameData`、`CallConv_*`。
2. 该模块对外只暴露**布尔能力**与**安装/卸载**，不暴露地址。
3. 未来新增任何原生调用，都必须先进这个模块。

### 非目标

- 不做「同时兼容 32/64」的运行时探测。没有 64 位构建可测，写了也是猜。
- 不引入新的原生调用。收敛边界不等于扩大攻击面。
- 不改现有行为。当前降级路径（detour 装不上 → 输入/动画/命中三层保护）必须保持不变。

---

## 3. 建议的模块边界

建议文件：`scripting/tankrun/tr_native.inc`（**未创建**）

### 3.1 对外契约（建议）

| 函数（建议名） | 签名 | 语义 |
|---|---|---|
| `TRN_Init` | `bool TRN_Init()` | 读取 gamedata、创建并启用 detour。成功返回 true。**不得**在失败时中止插件 |
| `TRN_Shutdown` | `void TRN_Shutdown()` | 禁用并释放 detour；`OnPluginEnd` 调用 |
| `TRN_ClawGateAvailable` | `bool TRN_ClawGateAvailable()` | detour 是否已安装 |
| `TRN_SetClawGateEnabled` | `void TRN_SetClawGateEnabled(bool enabled)` | 运行期开关闸门 |
| `TRN_ClawGateBlocks` | `bool TRN_ClawGateBlocks(int client)` | 单只 Tank 是否应被阻断（策略判定仍留在 `tr_rock.inc`） |

### 3.2 不变式

1. `TRN_Init` 返回 false 时，插件必须继续加载，只是失去第一道防线。这与当前行为一致。
2. 任何函数在 detour 未安装时都必须安全返回（false / no-op）。
3. 模块内部**唯一**允许出现的原生概念：`GameData`、`GetMemSig`、`DynamicDetour`、`CallConv_*`、`Address`。
4. 模块不持有 Tank 策略状态（窗口、尝试次数、userid 绑定仍在 `tr_rock.inc`）。

### 3.3 迁移映射（现状 → 目标）

| 现状位置 | 目标位置 |
|---|---|
| `tr_rock.inc:27-48` 的安装逻辑 | `TRN_Init` |
| `tr_rock.inc` 的 `TR_DetourFirstRockClaw` 回调 | 留在 `tr_rock.inc`（策略），由 `TRN_*` 决定是否 Hook |
| `tankdirector_menu.sp:155-160` 的卸载 | `TRN_Shutdown` |
| `g_hFirstRockClawGate` 全局 | 模块私有 |
| 日志「严格首石拳击入口：…」 | 由 `TRN_Init` 写出，措辞保持不变 |

**这只是一次重构，不改变行为。** 它没有做，因为当前 32 位实现已经稳定，重构本身需要一轮完整的回归验证。

---

## 4. 64 位适配时这一层要改什么

| 项 | 32 位 | 64 位（预期） | 影响面 |
|---|---|---|---|
| gamedata 平台段 | `"windows"` | 需要新的平台键（具体键名取决于 DHooks 与游戏构建，**未知**） | 仅 `gamedata/` |
| 字节签名 | x86 序言 `55 8B EC 51 A1` | x64 编码不同，需重新定位函数并重新生成模式 | 仅 `gamedata/` |
| 调用约定 | `CallConv_THISCALL` | x64 统一调用约定；具体常量取决于 DHooks 支持 | 仅模块内部 |
| 地址宽度 | 4 字节 | 8 字节 | 模块内使用 `Address` 类型即可，不要在别处用 `int` 存地址 |
| 结构体偏移 | 见研究报告 | 指针变宽，布局重排 | **不在插件里**——插件用 netprops，偏移由 SourceMod 提供 |

关键点：**插件代码里没有硬编码的结构体偏移**。这是当前架构最大的幸运，也是为什么 64 位适配的工作量主要集中在签名而不是业务逻辑。

---

## 5. 验收标准（若将来实现这一层）

| # | 条件 | 验证方式 |
|---|---|---|
| 1 | 插件其余文件不再出现 `DynamicDetour` / `GameData` / `CallConv_` | 源码检索 |
| 2 | 无 gamedata 时插件能加载并降级 | 删除 `gamedata` 后启动，检查日志 |
| 3 | 有 gamedata 时 detour 生效 | 日志显示「已启用原生 CTankClaw::PrimaryAttack 拦截」 |
| 4 | 卸载插件时 detour 释放 | `OnPluginEnd` 后无残留 Hook |
| 5 | 行为与重构前一致 | 回归 [验收剧本](../user-guide/validation-playbook.md) 的 S7 / S8 |
| 6 | 严格编译 0 error 0 warning | `tools/build.ps1`（默认 `-E`） |

第 5 条是硬门槛：**没有实服回归，就不算完成**。

---

## 6. 明确不做的事

| 不做 | 原因 |
|---|---|
| 预先写 64 位签名 | 没有目标构建，写出来一定是错的 |
| 运行时按指针宽度分支 | 没有 64 位环境可测，分支只会增加未测路径 |
| 用偏移扫描替代 gamedata | 脆弱且更难审计 |
| 在 64 位之外引入新的原生调用 | 收敛边界的意义是缩小而不是扩大 |

---

## 7. 如果 64 位真的来了

按这个顺序，不要跳步：

1. 记录新 `server.dll` 的 SHA256 与字节数（新构建 = 旧地址作废）。
2. 确认 SourceMod / Left4DHooks 有 64 位构建且 native 可用。**这一步不满足，后面都不用做。**
3. 重新定位 `CTankClaw::PrimaryAttack`，生成新签名，只改 `gamedata/`。
4. 按第 3 节把 detour 收进 `tr_native.inc`（此时才有必要重构）。
5. 重跑验收剧本 S7 / S8，登记为新构建的证据。
6. 更新 [32 位基线](32-bit-baseline.md) 与 [64 位计划](64-bit-plan.md) 的状态。

在此之前，本文件保持「设计」状态。
