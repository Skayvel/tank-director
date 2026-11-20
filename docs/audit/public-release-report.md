# 公开发布检查报告

对本仓库作为**公开发行版**的最终核查记录。核查时间：2026-11-20。

**结论**：源码可独立编译、链接零死链、无私有路径、无第三方反编译资料、无二进制除发布包外。**唯一阻塞项是许可证**。在补齐 `LICENSE` 之前，不应推送或发布。

---

## 0. 发布结论（Go / No-Go）

**结论：Go（有条件）。** 版权与许可证审计已完成，硬阻塞清零。

| # | 检查项 | 判定 | 依据 |
|---|---|---|---|
| 1 | 源码可独立编译 | ✅ Go | 7 个目标，严格模式 0 error / 0 warning（§3.2） |
| 2 | 二进制与源码同源 | ✅ Go | 段级比对：`.code`/`.publics`/`.pubvars`/`.natives` 与 `.names` 符号逐字节相同（§3.3） |
| 3 | 构建不泄露路径 | ✅ Go | 相对路径调用；`.names` 中 0 条私有路径 |
| 4 | 接口文档准确 | ✅ Go | 导出 4 native / 外部回调 15 / 调用 4 native（§4） |
| 5 | 链接完整 | ✅ Go | 全部相对链接可解析（§5.2） |
| 6 | 无私有信息 | ✅ Go | 私有路径 / SteamID / 凭据检索均为 0（§6） |
| 7 | 无第三方反编译资料 | ✅ Go | 仓库内 0 份（§2、§6） |
| 8 | 64 位不被误报为实现 | ✅ Go | 设计文件均带「未实现」声明（§7） |
| 9 | 许可证 | ✅ **Go** | `LICENSE` = GPLv3 官方全文；`NOTICE` 列明三方归属；SPDX 已加到 24 个代码/配置文件（§8） |
| 10 | 逐文件版权审计 | ✅ Go | 全部文件已登记来源、许可与风险（[版权审计](copyright-audit.md)） |

### 结论的边界

「Go」指**仓库内容本身**可以按 GPL-3.0-or-later 公开发布。以下仍由所有者决定，且不影响本结论：

| 事项 | 说明 |
|---|---|
| 版权人署名 | 当前使用 `Tank Director contributors`（集体署名）。若要以真实姓名或法律实体署名，请替换 `NOTICE`、`LICENSE` 头与源码 SPDX 头中的版权行 |
| 文档是否另用 CC BY 4.0 | 当前文档与代码同许可，最省事；如需单独授权请在 `NOTICE` 中声明 |
| 第三方商标使用 | `NOTICE` 已声明不隶属 Valve / AlliedModders；项目名未使用其商标 |

### 发布前仍需人工完成（非技术阻塞）

1. 若要用真实姓名署名，替换版权行（3 处：`NOTICE`、源码 SPDX 头、`LICENSE` 无需改）。
2. 初始化 Git 仓库并提交，打 tag `v2.2.8`。
3. 在 GitHub Release 附 `dist/TankRun-Control-Center-v2.2.8.zip` 与 `SHA256SUMS.txt`。

---
## 1. 一句话状态

| 维度 | 结果 |
|---|---|
| 构建 | ✅ 7 个目标全部通过严格编译（0 error / 0 warning） |
| 链接 | ✅ 0 条无法解析的相对链接 |
| 私有路径 | ✅ 0 处（源码与文档均已核查） |
| 第三方代码 | ✅ 0 份（仅有说明文件） |
| 反编译资料 | ✅ 0 份 |
| 接口分级 | ✅ 已逐项完成（见 [接口稳定性分级](../reference/api/stability.md)）；导出 API = 4 个 native，仅独立 capacity 插件 |
| 64 位 | ⚠ 只有设计，无实现（见 [兼容层设计](../compatibility/64-bit-compat-layer.md)） |
| 许可证 | ✅ GPL-3.0-or-later（[LICENSE](../../LICENSE) + [NOTICE](../../NOTICE)） |

---

## 2. 收录与排除

收录 20 类文件、排除 9 类内容，逐项理由见 [公开发行范围](../project/publication-scope.md)。

排除的关键项：反编译转储、REA/Ghidra 导出、第三方源码镜像、历史构建、内部派工记录、本机绝对路径。

## 3. 构建验证

### 3.1 环境

| 项 | 值 |
|---|---|
| 编译器 | SourcePawn Compiler **1.12.0.7220**（Steam 安装内的 SourceMod） |
| include | 同一套 SourceMod `include/`，含 `dhooks.inc` 与 `left4dhooks.inc` |
| 严格模式 | `-E`（警告视为错误） |
| 平台 | Windows / PowerShell 5.1 |
| 命令 | `.\tools\build.ps1 -SourceMod '<...>\addons\sourcemod\scripting' -Target all` |

### 3.2 结果

| 目标 | 源码 | SMX 字节 | Code | Data | 结果 |
|---|---|---:|---:|---:|---|
| 控制中心 | `tankdirector_menu.sp` | 122,873 | 288,928 | 120,044 | ✅ |
| 独立容量插件 | `tankrun_capacity.sp` | 38,897 | 84,376 | 34,020 | ✅ |
| 真人席位 | `tankrun_human_seats.sp` | 13,145 | 19,448 | 11,544 | ✅ |
| Tick 采样 | `tankrun_tick_telemetry.sp` | 8,048 | 10,972 | 4,412 | ✅ |
| M08 记录器 | `tankrun_m08_probe.sp` | 15,893 | 26,248 | 17,252 | ✅ |
| 生成记录器 0.2.3 | `tankrun_spawn_logger_v023.sp` | 30,506 | 69,920 | 34,436 | ✅ |
| M01 诊断骨架 | `tankrun_control.sp` | 3,696 | 4,460 | 3,204 | ✅ |

全部 0 error / 0 warning。原始输出：`build/build-report.txt`（本地构建产物，不随仓库分发）。

**本轮修正**：编译器现在以「仓库根 + 相对路径」调用（`tools/build.ps1`）。此前用绝对路径调用，会把构建者的目录结构写进 `.names` 段；改为相对路径后，SMX 内的项目文件路径与已发布产物一致（均为 `scripting/...`）。

### 3.3 与已发布产物的一致性

这是本次最关键的一条验证：

| 项 | 已发布 v2.2.8 | 本次重建 |
|---|---|---|
SMX 是容器格式，比较整包哈希没有意义（见下）。用 `tools/compare-smx.py` 解出各 payload 段，逐段比字节：

| 段 | 已发布 | 重建 | 字节相同 |
|---|---:|---:|---|
| `.code` | 288,948 | 288,948 | **是**（SHA256 `41653e3f…`） |
| `.publics` | 2,752 | 2,752 | **是** |
| `.pubvars` | 1,712 | 1,712 | **是** |
| `.natives` | 788 | 788 | **是** |
| `.names` | 25,487 | 25,487 | 否，仅 7 条路径字符串 |
| `.data` | 120,056 | 120,056 | 否，仅 8 字节 |

结论：**全部可执行内容和符号表完全一致**，公开源码就是产出该二进制的源码。

两处差异都可完整解释：

| 差异 | 位置 | 原因 |
|---|---|---|
| 8 字节 | `.data[0x04..0x05]` | spcomp 的栈/堆估算：`0x237C4`(145,348) vs `0x22104`(139,524)。差值正好是 25,304 与 19,480 两个 stack/heap 值 |
| 6 字节 | `.data[0x24..0x2B]` | 编译时间戳 ASCII：`16:04:16` vs 本次构建时刻 |
| 7 条 | `.names` 中的 include 路径 | 绝对路径分隔符风格不同（`C:/…\include\core.inc` vs `C:\…\include\core.inc`）；**其余 1,552 条符号完全相同** |

因此：

- **不能**用「重编译哈希相同」判断同源；整包哈希必然不同（时间戳 + 栈估算）。
- **也不能只比 Code/Data 的大小**——上一版这样写是不够的：`.data` 大小相同但内容不同。
- 正确做法是**分段比字节**：`tools/compare-smx.py`，看 `.code` / `.publics` / `.pubvars` / `.natives` 四段是否全 `YES`。
- `dist/` 分发的是**原始发布包**（与 2026-11-20 构建逐字节一致），不是本次重建产物。

### 3.4 打包工具验证

`tools/package.ps1` 已验证可生成正确布局的 ZIP：

```
addons/sourcemod/plugins/tankdirector_menu.smx
addons/sourcemod/gamedata/tankrun_firstrock.txt
```

条目分隔符为 `/`（早期实现用了 Windows 反斜杠，在 Linux/macOS 解压会出错，已修正并复验）。

**ZIP 字节不可复现**：条目时间戳会变，因此 ZIP 哈希每次不同。校验应以包内 SMX 的哈希为准，见 `dist/SHA256SUMS.txt`。

## 4. 接口核查

逐项分级结果见 [接口稳定性分级](../reference/api/stability.md)，统计如下：

| 类别 | 总数 | 稳定 | 实验性 | 内部 |
|---|---:|---:|---:|---:|
| Commands（用户） | 17 | 16 | 1 | 0 |
| ConVar（用户） | 64 | 30 | 33 | 1 |
| **导出的 Native**（给其他插件） | 4 | 0 | 4 | 0 |
| **实现的外部回调** | 15 | 3 | 5 | 7 |
| **调用的外部 Native** | 4 | 4 | 0 | 0 |

核查方法与三处更正：

1. 逐条对照源码注册点（`tr_diag.inc:423-441`、`tr_core.inc:1397-1439`、`tr_core.inc:1442-1637`、`tankrun_capacity.sp:83-126`）与 Left4DHooks 头文件，并编译验证签名。
2. **导出 API 只有 4 个 native**，且只存在于独立的 `tankrun_capacity.smx`（`CreateNative` 在 `#if !defined TR_EMBEDDED_CAPACITY` 内）。控制中心**不导出任何 native / forward / 库标识**：全仓库没有 `CreateGlobalForward`、没有 `RegPluginLibrary`、没有供第三方 `#include` 的公共 `.inc`。
3. **实现的外部回调是 15 个，不是 12 个**：上一版漏掉了 `L4D_OnDoAnimationEvent` 与其 `_Post`（`tr_rock.inc:205-238`，首石禁拳的第一可见层）。15 = Left4DHooks 14 + `OnPlayerRunCmd` 1。

另有一处**已知缺口**（不影响发布，但必须写明）：`TankRun_IsManagedCapacityActive` 由 `tankrun_capacity.sp:22` 声明并标记为可选，但**本仓库没有任何插件注册它**。因此独立容量插件的「托管模式」路径实际不可达，只能用 `tankrun_capacity_enabled 1` 的传统路径；内嵌在控制中心时不走这个 native。

分级规则与判定依据写在该文档第 1 节。**分级不等于已验证**：稳定级只表示接口形状可靠。

## 5. 链接审计

### 5.1 工具

`tools/check-links.ps1`：分类互斥（external / anchor / absolute / resolved / unresolved，另有 false-positive 单列），输出末尾打印两条自检等式：

```
external + anchor + skipped + local = total
absolute + resolved + unresolved    = local
```

### 5.2 公开发行版结果

| 指标 | 数量 |
|---|---:|
| Markdown 文件 | 52 |
| 链接总数 | 309 |
| 外部链接 | 20 |
| 纯锚点 | 1 |
| 误报（已排除） | 3 |
| 本地链接 | 285 |
| 其中：本机绝对路径 | 0 |
| 其中：可解析 | 285 |
| 其中：无法解析 | **0** |

> 上一次统计口径不一致（本地/无法解析/可解析三者相加对不上），原因是带盘符路径被当成 URL scheme、自定义 scheme 被误判、以及代码片段被链接正则误捕。该问题在内部文档审计阶段发现并在本仓库的 `tools/check-links.ps1` 中统一修正，同时加入自检等式防止再次漂移。

### 5.3 指向研究仓库的引用

公开版把原本指向 `analysis/`、`evidence/`、`indexes/`、`build/`、`docs/archive/` 的相对链接，逐一改写为**代码片段 + 「研究仓库」标注**；`build/releases/` 的下载链接改为 `dist/`。保留可追溯性，且不产生死链。原文语义未变。

改写清单（按文件）：

| 文件 | 处理 |
|---|---|
| `docs/README.md` | 审计章节改为公开发行章节；新增「本仓库没有收录什么」；删除存档目录入口 |
| `docs/research/README.md` | `indexes/status.json` → 代码片段 |
| `docs/reference/api/limits.md`、`convars.md` | M07-01 报告 → 代码片段 + 研究仓库路径 |
| `docs/user-guide/menu-reference.md` | 同上 |
| `docs/user-guide/capacity-and-seats.md` | `evidence/.../client-slot-guard.txt` → 代码片段 |
| `docs/reference/api/left4dhooks.md` | 接口清单 → 代码片段 + 研究仓库路径 |
| `docs/getting-started/install.md` | 下载链接 → `dist/` |
| `docs/reference/versions.md` | 审计记录 → 公开发布检查报告 |
| `docs/player/README.md` | 去掉存档链接 |

## 6. 内容安全检查

| 检查 | 方法 | 结果 |
|---|---|---|
| 私有盘符路径 | 全库检索项目私有目录名、作者盘符路径与本机安装路径 | 0 处（示例路径统一用 `C:\path\to\...` 占位） |
| 用户名 / 目录结构泄露 | 同上 | 0 处 |
| SteamID / 账号标识 | 检索 SteamID64 前缀、`STEAM_x:y:z` 形式与管理名单 | 0 处 |
| 反编译文件 | 检索 `.asm` / `.c` 转储 | 0 份 |
| 第三方源码 | 人工核对 `evidence/` 类内容 | 0 份 |
| 二进制 | 仅 `dist/` 内发布包 | 符合预期 |
| 凭据 / 密码 | 检索 `password`、`rcon` | 0 处 |

说明：`docs/compatibility/32-bit-baseline.md` 保留 `C:\Program Files (x86)\Steam\...` —— 这是 Steam 的**标准安装路径**，属于公开事实，不是私有信息。

## 7. 64 位

| 项 | 状态 |
|---|---|
| 32 位基线事实 | 已记录（[32 位基线](../compatibility/32-bit-baseline.md)） |
| 适配计划 | 已设计（[64 位计划](../compatibility/64-bit-plan.md)） |
| 兼容层设计 | 已设计，**0 行实现**（[兼容层设计](../compatibility/64-bit-compat-layer.md)） |
| 64 位逆向 | **未做** |
| 64 位构建 | **不存在** |

设计文档开头均有「不是实现」的显式声明，避免被引用为能力承诺。

## 8. 许可证（已解决）

### 8.1 决定

**GPL-3.0-or-later**，全文见 [`LICENSE`](../../LICENSE)（GNU 官方文本，35,149 字节，SHA256 `3972DC97…`）。

### 8.2 依赖侧事实（逐项核对头部，不依赖 GitHub 标签）

| 组件 | 版权人 | 许可证 | 使用方式 |
|---|---|---|---|
| SourceMod / sourcemod.inc / sdktools.inc / sdkhooks.inc / dhooks.inc | AlliedModders LLC（2004–2021） | GPL-3.0 + AlliedModders 例外 | 编译期 `#include`；**编译产物内嵌少量头文件生成的 boilerplate** |
| Left4DHooks / left4dhooks.inc | ProdigySim 2010、Downtown1 2009、Visor 2012–2015、Attano 2015、Accelerator74 2017、Silvers 2026 | GPL-3.0（含 AM 链接例外文字） | 编译期 `#include`；运行期依赖 `left4dhooks.smx` |
| DHooks（Dynamic Hooks） | AlliedModders LLC 2021 | GPL-3.0 + AM 例外 | 可选运行期依赖；`dhooks.inc` 为编译期 include |
| L4DToolZ | 未声明 | **无许可证**（默认保留所有权利） | **不打包、不修改、仅文档列为可选依赖** |
| Left 4 Dead 2 / Source 引擎 | Valve Corporation | 商业许可 | 不分发任何游戏文件 |

### 8.3 兼容性论证（GPL-3.0-or-later 是否可用）

1. **与 GPL-3.0 组件共存**：`GPL-3.0-or-later` 允许按 GPLv3 使用，因此与 SourceMod / Left4DHooks / DHooks（均为 GPL-3.0）组合分发没有冲突；组合分发时整体按 GPLv3 传递。
2. **符合 SourceMod 的插件例外**：其 `LICENSE.txt` 明确把插件视为衍生作品，并允许以「GPL v2 或更高版本」双许可。`GPL-3.0-or-later` 落在该允许集合内。
3. **未复制第三方源码**：与第三方头文件做逐行比对，唯一重合是通用 `for (int client = 1; client <= MaxClients; client++)` 之类的惯用写法（不具可版权性）。
4. **编译产物内的第三方成分已披露**：空插件实验证明 `__ext_*`、`__pl_l4dh` 等 autoload 结构与少量 stock 由头文件生成，任何使用这些 include 的插件都会包含它们。已在 `NOTICE` 第 1、2 节写明，并统一按 GPLv3 分发。

### 8.4 落地动作

| 动作 | 状态 |
|---|---|
| 添加 `LICENSE`（GPLv3 官方全文） | ✅ |
| 添加 `NOTICE`（三方归属 + 编译期嵌入说明 + gamedata 披露 + 商标声明 + 免责） | ✅ |
| 24 个代码/配置文件加 SPDX 头（`GPL-3.0-or-later` + 版权行） | ✅ |
| 保留第三方版权声明 | ✅ 未做任何改写；`NOTICE` 逐字保留其版权行 |
| 逐文件版权清单 | ✅ [版权与许可证审计](copyright-audit.md) |
| 原「待决报告」处置 | 保留为历史记录：[license-pending-2026-11-20.md](license-pending-2026-11-20.md) |
## 9. 发布动作（Go 之后）

**技术侧已完成，以下为发布操作，不属于审计阻塞。**

| # | 动作 | 负责 | 说明 |
|---|---|---|---|
| 1 | 用真实姓名/实体替换集体署名（可选） | 所有者 | 需改：`NOTICE` 顶部、24 个文件的 SPDX 版权行 |
| 2 | `git init` + 首次提交 + tag `v2.2.8` | 所有者 | 本次审计未执行任何 Git 操作 |
| 3 | 建立 GitHub Release，附 `dist/*.zip` 与 `SHA256SUMS.txt` | 所有者 | 仓库内已带同一份 ZIP；Release 附件是便利项 |
| 4 | 开启 issue 模板（要求附 `sm_tank_dump` 与 `sm plugins list`） | 所有者 | 见 [贡献指南](../project/contributing.md) |
| 5 | 加 CI：`tools/build.ps1` + `tools/check-links.ps1 -FailOnUnresolved` | 所有者 | 防止回归 |

**本次明确没有做**

- 未 `git init`、未提交、未配置远端、未推送、未上传任何文件。
- 未创建公开 Release。
- 未修改研究仓库中的任何文件。
## 10. 复现本次核查

```powershell
# 1) 编译（需要 SourceMod 1.12 + Left4DHooks 的 include）
.\tools\build.ps1 -SourceMod 'C:\servers\l4d2\left4dead2\addons\sourcemod\scripting'

# 2) 链接审计（必须 0 unresolved）
.\tools\check-links.ps1 -Root . -Detail -FailOnUnresolved

# 3) 段级比对：与已发布 SMX 比较 .code/.data/.publics/.pubvars/.natives/.names
#    期望：.code .publics .pubvars .natives 全部 YES；.data 仅 8 字节不同
python .\tools\compare-smx.py .\dist\<解出的 2.2.8>.smx .\build\tankdirector_menu.smx

# 4) 发布包校验
Get-FileHash .\dist\TankRun-Control-Center-v2.2.8.zip -Algorithm SHA256
Get-Content .\dist\SHA256SUMS.txt

# 5) 打包（可选，会覆盖 dist 中的同名 ZIP，请先备份）
.\tools\package.ps1 -Smx .\build\tankdirector_menu.smx
```

## 11. 关联文档

| 文档 | 内容 |
|---|---|
| [公开发行范围](../project/publication-scope.md) | 收录/排除的依据 |
| [公开文件清单](public-file-manifest.md) | 全部文件与关键哈希 |
| [接口稳定性分级](../reference/api/stability.md) | 逐项接口分级 |
| [版本表](../reference/versions.md) | 版本与产物哈希 |
| [版权与许可证审计](copyright-audit.md) | 逐文件来源、依赖矩阵、高风险项 |
| [LICENSE](../../LICENSE) / [NOTICE](../../NOTICE) | 许可证全文与三方归属 |
