# 依赖与版本要求

控制中心 v2.2.11 的实际依赖，以及每项缺失时会发生什么。

## 运行环境

| 项目 | 要求 | 依据 |
|---|---|---|
| 游戏 | Left 4 Dead 2（Steam 版） | `tankdirector_menu.sp:37-41`：`GetEngineVersion() != Engine_Left4Dead2` 时 `SetFailState` |
| 服务端类型 | 专用服务器或本地（listen）服务器 | 两种都在项目内实测过 |
| 操作系统 | **Windows** | `gamedata/tankrun_firstrock.txt` 只有 `windows` 平台签名 |
| 游戏模式 | Tank Run（`mp_gamemode tankrun`） | 脚本字段、阶段判定、刷点规则都基于此模式 |
| Tick 基线 | 100 tick | 项目全部实测记录都在 100 tick 下；128 tick **未验证** |
| 游戏构建 | `server.dll` 基线 `a93ec916…`；2026-11-25 更新版 `6c543884…` 已核验签名同样 1/1 唯一命中 | DHooks 签名按此两构建核对 |

> **构建绑定**：`tankrun_firstrock.txt` 的签名注释写明该地址在 VA `0x103e3860`、对应上述 SHA256。游戏更新后该签名可能不再匹配，此时原生禁拳钩子会静默降级（见下文），插件其余功能不受影响。

## 必需组件

| 组件 | 版本 | 为什么必需 |
|---|---|---|
| [MetaMod:Source](https://www.sourcemm.net/) | 1.11 或更新 | SourceMod 的运行前提 |
| [SourceMod](https://www.sourcemod.net/) | **1.12** | 插件用 1.12 的新声明语法编译（`#pragma newdecls required`） |
| [Left4DHooks](https://github.com/SilvDev/Left4DHooks) | 与游戏构建兼容的版本；历史记录为 1.175 | 生成 Tank、原生随机选位、投石/拳击/目标 forward、NAV 路径接口 |
| SDKTools | 随 SourceMod | 实体属性、射线、计时 |
| SDKHooks | 随 SourceMod | `SDKHook_OnTakeDamage`（伤害倍率） |

### 缺失时会怎样

插件直接调用这些扩展的 native，没有做逐项降级：

| 缺失 | 预期结果 | 说明 |
|---|---|---|
| SourceMod 1.12 | 无法编译该源码 | 旧版 SourcePawn 不接受当前语法 |
| Left4DHooks | **插件加载失败** | `L4D2_SpawnTank`、`L4D_GetRandomPZSpawnPosition`、`L4D2_NavAreaBuildPath` 等 native 无法解析，SourceMod 拒绝加载 |
| SDKHooks | **插件加载失败** | `SDKHook` native 无法解析 |
| SDKTools | **插件加载失败** | 实体/射线 native 无法解析 |

插件内的 `TR_ProbeLibraries()`（`tr_status.inc:12-20`）只用于**状态显示**，它通过 `GetFeatureStatus` 报告可用性，不会改变 SourceMod 的加载判定。`docs/tankrun-control-center.md` 第 1 节曾把 SDKHooks 缺失写成「伤害倍率失效，其余可用」，这与源码行为不符——本次以源码为准。

## 可选组件

| 组件 | 不装会怎样 | 装了会怎样 |
|---|---|---|
| [DHooks](https://forums.alliedmods.net/showthread.php?t=180114) | 首石窗口**只用**输入拦截 + 动画判定 + 命中钩子保护；已开始的挥拳可能完成 | 安装 `CTankClaw::PrimaryAttack` 原生 Pre-detour，在武器入口直接阻断起拳（`tr_rock.inc:27-48`） |
| 官方 Tank Run 模式脚本 | 菜单里脚本类字段显示「不可用」，插件其余功能正常 | 可读写 `SessionState.SpawnInterval`、`SessionState.DoubleTanks`、`SessionOptions.cm_TankLimit` 等字段 |
| [L4DToolZ](https://github.com/lakwsh/l4dtoolz) 2.5.1 | 客户端槽位固定为游戏默认（通常 18），原生 Tank 数量受限 | 用 `sv_setmax` 把总槽位提到 18–32；31 槽是当前推荐上限 |
| `tankrun_human_seats` | 幸存者席位按原版规则 | 可把真人席位提到 8（需配合高容量配置） |

**DHooks 的降级是可观察的。** 插件加载时会在 `logs/tankrun_control.log` 写一行：

```
严格首石拳击入口：已启用原生 CTankClaw::PrimaryAttack 拦截
严格首石拳击入口：签名或扩展不可用，使用输入、动画和命中保护
```

看到第二行说明 gamedata 没匹配上你的构建。这不影响生成、参数、诊断等其它功能，但首石的第一道防线不存在。

## 编译器版本（构建时）

发布记录里出现两个版本，都是真的，对应不同时间点：

| 版本 | 使用的编译器 | 说明 |
|---|---|---|
| ≤ v2.1.7 | SourcePawn 1.12.0.7260（项目自带工具链） | 早期构建用的是随项目维护的 SourceMod 工具链 |
| ≥ v2.1.8 | SourcePawn 1.12.0.7220（Steam 安装内的 SourceMod） | 之后改用服务器上实际加载的 SourceMod 编译，减少版本错配 |

两者都是 1.12，源码兼容。自己重编译时用你服务器上的 SourceMod 版本即可，不要混用两套 include。当前的构建验证结果见 [公开发布检查报告](../audit/public-release-report.md)。

## 下一步

- 装： [安装指南](install.md)
- 装完验收： [首次运行与验收](quickstart.md)
- 版本历史： [版本表](../reference/versions.md)
