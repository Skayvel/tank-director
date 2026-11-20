# 第三方组件

本仓库**不包含**任何第三方源代码。以下是运行和构建所需的外部组件，按各自许可证分发，需由使用者自行获取。

完整的版权归属、编译期嵌入说明与法律声明见仓库根目录的 [NOTICE](../NOTICE)；逐文件来源审计见 [版权与许可证审计](../docs/audit/copyright-audit.md)。

## 构建依赖

| 组件 | 版本 | 用途 | 许可证（以上游为准） |
|---|---|---|---|
| [SourceMod](https://github.com/alliedmodders/sourcemod) | 1.12（构建验证使用 1.12.0.7220） | 编译器 `spcomp` + 全部 `include/` | GPL-3.0 |
| [Left4DHooks](https://github.com/SilvDev/Left4DHooks) | 与游戏构建兼容的版本（历史记录 1.175） | `left4dhooks.inc` + 运行期扩展 | GPL-3.0 |
| [DHooks](https://forums.alliedmods.net/showthread.php?t=180114) | 随 SourceMod 生态提供 | `dhooks.inc` + `DynamicDetour` | GPL-3.0 |

**编译必须同时具备** SourceMod 的 include 目录与 Left4DHooks 的 `left4dhooks.inc`。缺一不可，详见 [构建与编译](../docs/project/build.md)。

## 运行依赖

| 组件 | 必要性 | 用途 |
|---|---|---|
| MetaMod:Source | 必需 | SourceMod 的运行前提 |
| SourceMod 1.12 | 必需 | 插件宿主 |
| Left4DHooks | 必需 | 生成、随机选位、投石/拳击/目标 forward |
| SDKHooks / SDKTools | 必需（随 SourceMod） | 伤害钩子、实体属性、射线 |
| DHooks | 可选 | 原生 `CTankClaw::PrimaryAttack` detour；缺失时降级 |
| [L4DToolZ](https://github.com/lakwsh/l4dtoolz) 2.5.1 | 可选 | 客户端槽位扩展（`sv_setmax`），高容量配置使用 |

## 运行时调用但不分发的接口

插件通过公开接口与以下组件交互，不复制其代码：

- **Left4DHooks forwards**：`L4D2_OnChooseVictim`、`L4D_OnCThrowActivate`、`L4D2_OnSelectTankAttackPre`、`L4D_TankRock_OnRelease`、`L4D_TankClaw_OnPlayerHit_Pre` 等，共 12 个
- **Left4DHooks natives**：`L4D2_SpawnTank`、`L4D_GetRandomPZSpawnPosition`、`L4D2_NavAreaBuildPath`
- **SourceMod**：`SDKHook_OnTakeDamage`、`HookEvent`、菜单/ConVar/文件 API

接口清单与能力边界：[回调与 Native 参考](../docs/reference/api/left4dhooks.md)。

## 引用的参考资料

文档中链接了以下公开资料用于解释引擎行为，**未复制其内容**：

- [hl2sdk](https://github.com/alliedmodders/hl2sdk)（Source 引擎 SDK 镜像）
- [L4D2 Community Update](https://github.com/Tsuey/L4D2-Community-Update)（`scriptedmode.nut` 参考）
- SourceMod API 文档、Microsoft Win32 文档

## 明确不包含的内容

| 内容 | 原因 |
|---|---|
| 游戏本体文件（`server.dll`、VPK、模型） | 受版权保护，且不随插件分发 |
| 第三方插件二进制（含 Left4DHooks 的 `.smx`） | 按上游许可证分发，本仓库只声明依赖 |
| 反编译转储（`.c` / `.asm`） | 游戏二进制的衍生物，权利状态未明；留在私有研究工作区 |
| REA / Ghidra 导出 | 同上 |

如果未来要随仓库分发任何第三方文件，必须先核对上游许可证并保留其版权声明。

## 版权与编译期嵌入（重要）

`scripting/` 下的 `.sp` / `.inc` **没有复制**任何第三方源码。但编译产物会包含**由头文件生成的少量 boilerplate**，这一点必须说清楚：

| 现象 | 证据 | 影响 |
|---|---|---|
| 编译产物含 `__ext_core`、`__ext_sdktools`、`__ext_sdkhooks`、`__ext_dhooks`、`__pl_l4dh` | 用**空插件**（只 `#include` 头文件、无任何业务代码）编译，同样出现这些符号 | 这是 SourceMod/Left4DHooks 头文件里的 autoload 结构，任何插件都会带上 |
| 编译产物含 `L4D2WeaponName`、`g_hWeaponNameTrie`、`L4D2ZombieClassname` 等 stock 符号 | 同上，空插件中已存在 | 头文件中的 stock 实现被编译进插件 |
| 与第三方头文件逐行比对 | 仅命中 `for (int client = 1; client <= MaxClients; client++)` 这类惯用写法 | 不构成复制 |

因此：**分发的 `.smx` 是 GPLv3 组件的衍生/组合作品**。本项目以 GPL-3.0-or-later 分发，组合整体按 GPLv3 传递，与 SourceMod 的插件例外一致（详见 `NOTICE` §2）。

## 许可证一览

| 组件 | 许可证 | 是否随本仓库分发 |
|---|---|---|
| 本项目代码与文档 | GPL-3.0-or-later | 是 |
| SourceMod / SourcePawn SDK | GPL-3.0 + AlliedModders 例外 | 否（仅头文件被 `#include`） |
| Left4DHooks | GPL-3.0 | 否（运行期依赖） |
| DHooks | GPL-3.0 + AlliedModders 例外 | 否（运行期依赖） |
| L4DToolZ | **无许可证声明** | **否**（仅文档提及） |
| Left 4 Dead 2 游戏文件 | Valve 商业许可 | **否** |