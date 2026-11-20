# 版权与许可证审计

对 `tank-director-release`（公开发行候选版）的逐文件版权核查、第三方依赖授权矩阵与高风险项处置。

审计日期：2026-11-20。拟定许可证：**GPL-3.0-or-later**。

## 1. 结论摘要

| 项 | 结果 |
|---|---|
| 文件总数 | 82（含 `LICENSE`、`NOTICE`；含 `dist/` 内 1 个 ZIP） |
| 原创文件 | 81（本项目作者；含文档、源码、工具、配置、产物与声明） |
| 第三方文件 | 0（**仓库内没有任何第三方源码或二进制**） |
| 许可证文件 | 1（GPLv3 官方文本，FSF 版权） |
| 声明文件 | 1（`NOTICE`，本项目） |
| 来源不明内容 | **0** 处（详见 §3 的判定与 §5 的保留项） |
| 高风险文件 | **1** 个：`gamedata/tankrun_firstrock.txt`（逆向所得字节模式，已披露并保留） |
| GPL-3.0-or-later 兼容性 | **兼容**（论证见 §4.3） |
| 结论 | **Go** |

## 2. 审计方法

### 2.1 枚举

1. `Get-ChildItem -Recurse -File -Force` 枚举仓库全部文件，包含隐藏文件（`.gitignore`）。
2. 用 `System.IO.Compression.ZipFile` 打开 `dist/*.zip`，列出**包内**每个条目（2 个），不依赖文件名推断。
3. 记录每个文件的字节数与 SHA256（见 [公开文件清单](public-file-manifest.md)）。

### 2.2 来源判定（四道独立检查）

| 检查 | 做法 | 目的 |
|---|---|---|
| 版权头扫描 | 正则搜索 `Copyright`、`SPDX`、`©`、`ported`、`adapted`、`credit`、`Silvers`、`AlliedModders` 等 | 找出显式声明与可疑出处 |
| 逐行比对 | 把 `scripting/` 中长度 ≥40 字符的行，与本地 SourceMod/Left4DHooks 全部 `.inc` 的对应行做集合比对 | 发现复制粘贴的代码 |
| 空插件实验 | 编译一个只含 `#include`、无业务代码的插件，检查其 `.names` | 区分「我们写的代码」与「头文件必然生成的符号」 |
| 产物段级分析 | 用 `tools/compare-smx.py` 解出已发布 SMX 的 `.names`/`.natives` 等段 | 确认编译产物里到底有什么 |

### 2.3 判定原则

- **「没有版权声明」≠「可以自由授权」**：本仓库自己的文件原先都没有版权头，但它们由本项目作者编写，因此可以加 SPDX；对来源不明的内容（本仓库为 0），一律保留并标注，不擅自授权。
- **API 调用 ≠ 复制代码**：调用 Left4DHooks native/forward 属于接口使用；是否构成衍生作品在 §4 单独论证。
- **头文件生成物单独归类**：编译产物里的 autoload/stock 符号来自 include，不是我们的源代码，但确实进入了分发的二进制。

## 3. 逐文件清单

说明：

- 「版权人 / 许可」指**该文件本身**的权利状态，不是它所依赖的库。
- 「来源证据」给出判定依据；文档类文件的证据为「本项目撰写，无外部引用复制」。
- 风险等级：**低** = 本项目原创、无第三方内容；**中** = 含逆向所得信息或第三方生成物；**高** = 权利状态不明或需外部授权（本次为 0）。

> **哈希说明**：`docs/audit/copyright-audit.md`（本文件）与 `docs/audit/public-file-manifest.md` 的哈希不在此表内——两者互相包含对方的摘要，无法自洽。其余 80 个文件的哈希为本表生成时刻的实际值；清单文件自身记录的哈希见 [public-file-manifest.md](public-file-manifest.md)。
| 类别 | 文件 | 字节 | SHA256（前 12） | 版权人 | 许可 | 来源 / 证据 | 风险 |
|---|---|---:|---|---|---|---|---|
| 仓库配置 | `.gitignore` | 410 | `F2AC95EE13D4` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 配置样例 | `configs/server_tankrun_capacity.cfg` | 386 | `6D3120FDF4C6` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注 | 低 |
| 配置样例 | `configs/tankrun_control_settings.example.cfg` | 1694 | `D4E8A3EAEECF` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注 | 低 |
| 配置样例 | `configs/tankrun_local_firstrock.cfg` | 559 | `CAB16BBBDD79` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注 | 低 |
| 配置样例 | `configs/tankrun_local_pressure.cfg` | 687 | `493513A3E3B8` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注 | 低 |
| 发布产物 | `dist/SHA256SUMS.txt` | 521 | `6CAA289E8149` | Tank Director contributors | GPL-3.0-or-later（产物含三方 boilerplate） | 本项目编译/打包产物；包内 2 条目 | 低 |
| 发布产物 | `dist/TankRun-Control-Center-v2.2.8.zip` | 122065 | `7661FCD83A04` | Tank Director contributors | GPL-3.0-or-later（产物含三方 boilerplate） | 本项目编译/打包产物；包内 2 条目 | 低 |
| 文档 | `docs/audit/copyright-audit.md` | — | （本文件） | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 历史记录 | `docs/audit/license-pending-2026-11-20.md` | 7629 | `0B14AABC8BD1` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写的历史分析（已失效） | 低 |
| 文档 | `docs/audit/public-file-manifest.md` | — | （见本清单） | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/audit/public-release-report.md` | 17322 | `C16AB42AB128` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/compatibility/32-bit-baseline.md` | 3588 | `BC1335040BFE` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/compatibility/64-bit-compat-layer.md` | 6700 | `D15C8C3B1D11` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/compatibility/64-bit-plan.md` | 6124 | `8702BAA8F0D8` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/getting-started/install.md` | 5167 | `AF8DC65F95AB` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/getting-started/quickstart.md` | 6385 | `920F5934EB42` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/getting-started/requirements.md` | 4811 | `C4424C5EE097` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/getting-started/upgrade-and-rollback.md` | 5126 | `5A1F3AD0D63D` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/operations/configs.md` | 3469 | `3A160CB6759A` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/operations/server-setup.md` | 3927 | `52CD172F43B5` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/operations/troubleshooting.md` | 7212 | `7E9BFE279549` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/player/README.md` | 1723 | `D675306088AA` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/project/architecture.md` | 5700 | `5BDBC69BB634` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/project/build.md` | 5023 | `AF27BA57A84D` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/project/contributing.md` | 2699 | `B2C827CA02AD` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/project/evidence-policy.md` | 4103 | `A156624FB5C5` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/project/glossary.md` | 4147 | `B5559C1D7F5A` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/project/history.md` | 4589 | `3CEBA3F82A86` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/project/publication-scope.md` | 4998 | `FCF3A4D2DC06` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/project/testing.md` | 4414 | `B5D0346A4B8B` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/README.md` | 7639 | `AD9F4D50CA5C` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/reference/api/commands.md` | 7974 | `6C5771315440` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/reference/api/convars.md` | 12791 | `086F062E3EDD` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/reference/api/files.md` | 7265 | `7C132D8B80AD` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/reference/api/gamedata.md` | 3647 | `35AF685BA0B9` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/reference/api/left4dhooks.md` | 11093 | `1DBD308715D2` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/reference/api/limits.md` | 5047 | `8A50FB383A15` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/reference/api/README.md` | 2925 | `5ADF7F75D187` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/reference/api/stability.md` | 14399 | `BD40C8D2B646` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/reference/api/vscript-bridge.md` | 8099 | `85EEC8B29F9D` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/reference/tools.md` | 6376 | `7984F7A79205` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/reference/versions.md` | 7761 | `5816B65E3C3D` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/research/experimental-findings.md` | 8228 | `F05E8E3EC816` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/research/interfaces/README.md` | 7415 | `FD3BE8DB0943` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/research/method.md` | 5760 | `23710AFC27C1` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/research/native-ai/README.md` | 7126 | `9821D838993B` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/research/open-questions.md` | 6785 | `55AD6955A10F` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/research/README.md` | 1907 | `A1A9CCBDAF7E` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/research/roadmap.md` | 3966 | `3FDE1E23E67E` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/research/verified-findings.md` | 10019 | `322760D22265` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/user-guide/capacity-and-seats.md` | 6637 | `8439A86E76B0` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/user-guide/control-center.md` | 11650 | `0AFA32672BEF` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/user-guide/diagnostics.md` | 6356 | `DCFD74003D70` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/user-guide/menu-reference.md` | 8866 | `EB4230E238C9` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/user-guide/presets.md` | 5152 | `16FF988AE1D3` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/user-guide/spawn-modes.md` | 6611 | `74D7D005B6E8` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 文档 | `docs/user-guide/validation-playbook.md` | 8570 | `CFE1D28B0007` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 原生签名 | `gamedata/tankrun_firstrock.txt` | 532 | `BDA4CC9BAB44` | Tank Director contributors | GPL-3.0-or-later | 逆向所得 40 字节指令模式；NOTICE §3 披露 | 中 |
| 许可证文本 | `LICENSE` | 35149 | `3972DC9744F6` | Free Software Foundation, Inc. | GPL-3.0（许可证文本） | GNU 官方 gpl-3.0.txt；该文本允许逐字复制 | 低 |
| 法律声明 | `NOTICE` | 5591 | `D5295C345E3E` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写；汇总三方归属与披露 | 低 |
| 文档 | `README.md` | 7034 | `57E2C906447D` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写 | 低 |
| 原创源码 | `scripting/tankdirector_menu.sp` | 14247 | `A763E318CCED` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun/tr_core.inc` | 58743 | `9A2BB1FD6816` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun/tr_diag.inc` | 14009 | `2C989E0168A7` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun/tr_lead_spawn.inc` | 12934 | `2FA2D1680D9F` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun/tr_menu.inc` | 49354 | `DE8D17AC680D` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun/tr_rock.inc` | 26275 | `455FC9BBDFA2` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun/tr_status.inc` | 11704 | `C7A9F6A613B6` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun/tr_tank.inc` | 38348 | `DFA7CC8D6DDD` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun_capacity.sp` | 32220 | `B88AF5E4F590` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun_capacity/tc_combat.inc` | 28245 | `C01127BB285B` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun_capacity/tc_nav.inc` | 15437 | `C3C65965AB19` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun_control.sp` | 2532 | `0BA26AB93028` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun_human_seats.sp` | 17963 | `A118DAF39A04` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun_m08_probe.sp` | 21121 | `8A25D8D1E103` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun_spawn_logger_v023.sp` | 81485 | `1F528CFB78D7` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 原创源码 | `scripting/tankrun_tick_telemetry.sp` | 7298 | `3E0BEFE9296A` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注；逐行比对无第三方重合 | 低 |
| 文档 | `third_party/README.md` | 4693 | `743358A7ABA6` | Tank Director contributors | GPL-3.0-or-later | 本项目撰写；仅列依赖，不含三方代码 | 低 |
| 工具脚本 | `tools/build.ps1` | 6490 | `DEF53983E106` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注 | 低 |
| 工具脚本 | `tools/check-links.ps1` | 5682 | `BD2DACDE9E13` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注 | 低 |
| 工具脚本 | `tools/compare-smx.py` | 8356 | `76F5437CCD69` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注 | 低 |
| 工具脚本 | `tools/package.ps1` | 4371 | `56CB2C6F9D61` | Tank Director contributors | GPL-3.0-or-later | 原创；SPDX 已标注 | 低 |

## 4. 第三方依赖授权矩阵

### 4.1 区分四种「使用」

| 使用方式 | 本仓库的实际情况 | 是否需要分发其许可证 |
|---|---|---|
| 编译依赖（`#include`） | SourceMod SDK、Left4DHooks、DHooks | 是（已在 `NOTICE` 列出并给出上游链接） |
| 运行依赖 | SourceMod、Left4DHooks、SDKHooks/SDKTools（随 SM）、DHooks（可选）、L4DToolZ（可选） | 是（已在 `NOTICE` + `third_party/README.md` 声明） |
| 复制进仓库 | **无** | —— |
| 实际打包分发 | 仅 `dist/` 内的 `tankdirector_menu.smx` 与 `tankrun_firstrock.txt` | 是（`.smx` 含头文件生成物，见 4.2） |

### 4.2 矩阵

| 组件 | 版权人 | 许可证（以本机头部为准） | 使用方式 | 分发 | 与 GPL-3.0-or-later 兼容 |
|---|---|---|---|---|---|
| SourceMod / SourcePawn SDK<br>`sourcemod.inc` `sdktools.inc` `sdkhooks.inc` `dhooks.inc` | AlliedModders LLC<br>2004–2008 / 2004–2017 / 2009–2013 / 2021 | GPL-3.0 + AlliedModders 特殊例外（允许插件以 GPLv2+ 双许可） | 编译期 include；编译产物含 autoload/stock 生成物 | 否（只分发我们的产物） | **是** |
| Left4DHooks<br>`left4dhooks.inc` | ProdigySim 2010；Downtown1 2009；Visor 2012–2015；Attano 2015；Accelerator74 2017；Silvers 2026 | GPL-3.0（含 AM 链接例外文字） | 编译期 include；运行期 `left4dhooks.smx` | 否 | **是** |
| DHooks（Dynamic Hooks） | AlliedModders LLC 2021 | GPL-3.0 + AM 例外 | 可选运行期依赖；`dhooks.inc` 为编译期 include | 否 | **是** |
| L4DToolZ | 上游未声明 | **无许可证**（默认保留所有权利） | 仅文档列为可选依赖 | **否** | 不适用（不分发） |
| Left 4 Dead 2 / Source 引擎 | Valve Corporation | 商业许可 | 目标运行环境；静态分析对象 | **否**（无任何游戏文件） | 不适用 |

### 4.3 兼容性论证

1. **`or-later` 与 GPL-3.0-only 组件共存**
   本仓库代码可按 GPLv3 使用；与 SourceMod / Left4DHooks / DHooks（均 GPL-3.0）组合分发时，整体按 GPLv3 传递，无版本冲突。

2. **符合 SourceMod 的插件例外**
   SourceMod `LICENSE.txt`（2019-9-30）明确：插件属于**衍生作品**，且允许其以「GPL v2 或更高版本」双许可。`GPL-3.0-or-later` 正落在允许集合内（v3 ∈ "v2 or higher"）。

3. **未复制第三方源码**
   逐行比对：`scripting/` 共 3,280 条 ≥40 字符的行，与本地 SourceMod/Left4DHooks 头文件库（13,044 条）比对，命中 10 条，全部是同一句惯用循环：

   ```sourcepawn
   for (int client = 1; client <= MaxClients; client++)
   ```

   该写法属于通用编程惯用法，不构成有表达性的受版权保护代码。除此之外**零重合**。

4. **编译产物内的第三方成分已识别**
   编译一个**空插件**（只 include、无业务代码）后检查 `.names`：

   | 符号 | 空插件中是否存在 | 归属 |
   |---|---|---|
   | `__ext_core` `__ext_sdktools` `__ext_sdkhooks` `__ext_dhooks` | 是 | SourceMod 扩展 autoload 结构 |
   | `__pl_l4dh` | 是 | Left4DHooks `SharedPlugin` autoload |
   | `L4D2WeaponName` `L4D2ZombieClassname` `g_hWeaponNameTrie` `VerifyCoreVersion` | 是 | 头文件中的 stock 实现 |

   结论：这些符号**不是从第三方源码拷贝来的**，而是任何使用这些 include 的插件都会生成的 boilerplate。它们以 GPLv3 分发，与本项目许可证一致。

## 5. 高风险文件与处置

### 5.1 `gamedata/tankrun_firstrock.txt`（中风险）

| 项 | 内容 |
|---|---|
| 是什么 | 40 字节 x86 指令模式（含 4 处 `2A` 通配），用于定位 `CTankClaw::PrimaryAttack` |
| 来源 | 对本机合法安装的 `server.dll` 做静态分析得出；绑定 SHA256 `a93ec916…` 与 VA `0x103e3860` |
| 是否含游戏代码 | 否。只有指令字节形状，无任何函数体、资源或符号 |
| 是否可版权 | 短小的功能性模式，通常不构成受版权保护的表达；但它是从受版权保护的二进制中提取的信息 |
| 处置 | **保留**，不删除；在 `NOTICE` §3 主动披露来源、用途与边界；在 [gamedata 文档](../reference/api/gamedata.md) 说明平台与构建绑定 |
| 残余风险 | 低—中。若权利人主张，最坏情况是要求移除该文件；移除后插件降级为「无原生禁拳」路径，其余功能不受影响（见 [未实现能力](../reference/api/limits.md)） |

### 5.2 `dist/*.zip`（中低风险）

| 项 | 内容 |
|---|---|
| 内容 | `addons/sourcemod/plugins/tankdirector_menu.smx`（122,874 B）+ `addons/sourcemod/gamedata/tankrun_firstrock.txt`（532 B） |
| 性质 | 本项目的编译产物 + 5.1 的签名文件 |
| 产物内的第三方成分 | 头文件生成的 autoload/stock boilerplate（见 4.3 第 4 点） |
| 处置 | 保留；`NOTICE` 说明产物性质；ZIP 内文件哈希记录在 `dist/SHA256SUMS.txt` |
| 备注 | 该 SMX 由**未加 SPDX 头的源码**编译。经段级比对，加头后重建的 `.code`/`.publics`/`.pubvars`/`.natives` 与 `.names` 符号**完全一致**，仅 `.data` 的 8 字节（栈估算 + 时间戳）不同，因此源码与产物的对应关系不受 SPDX 头影响 |

### 5.3 已排除、零风险

| 类别 | 状态 |
|---|---|
| 游戏二进制 / VPK / 模型 / 材质 | 仓库内 0 份 |
| 反编译转储（`.c` / `.asm`） | 0 份 |
| REA / Ghidra 导出、dossiers | 0 份 |
| 第三方插件源码或 `.smx` | 0 份 |
| 第三方插件源码镜像（研究仓库中的 `evidence/parameter-control/sources/`） | 未随公开发行版发布 |

## 6. 特殊内容核查

| 项目 | 核查方式 | 结果 |
|---|---|---|
| `gamedata` 字节签名 | 逐字节读取；核对 SHA256 注释与构建绑定 | 40 字节模式，无游戏代码；见 5.1 |
| `dist` 包内真实内容 | 用 ZIP API 枚举条目，不信任文件名 | 2 个条目，与 `SHA256SUMS.txt` 一致 |
| 编译产物与源码关系 | 段级比对（`.code`/`.data`/`.names` 等） | 同源；差异仅栈估算 + 时间戳 + include 路径分隔符 |
| 文档中的第三方代码引用 | 检查 `docs/` 是否内嵌第三方源码 | 只引用接口名与文档链接，无源码块 |
| 是否存在未授权游戏材料 | 全库扩展名与关键词扫描 | 无 |
| 文档中的本机路径 / 身份信息 | 扫描私有目录名、盘符、SteamID | 0 处 |

## 7. 未解决项与所有者保留事项

以下**不影响 Go**，但需要所有者确认或择机处理：

| # | 事项 | 现状 | 建议 |
|---|---|---|---|
| P1 | 版权人署名 | 使用集体署名 `Tank Director contributors` | 若要署名到个人/实体，替换 `NOTICE` 顶部与 24 个文件的 SPDX 版权行 |
| P2 | 文档许可 | 与代码同为 GPL-3.0-or-later | 如希望文档更宽松，可另用 CC BY 4.0 并在 `NOTICE` 声明 |
| P3 | `tankrun_spawn_logger_v023.sp` 头部引用了研究仓库路径 | 注释中的 `analysis/spawn-logger-v023/state-design.md` 不在公开仓库 | 可改为「见项目文档」；不构成许可问题 |
| P4 | L4DToolZ 无许可证 | 只在文档中作为可选依赖提及 | 若将来要打包必须取得授权；当前不分发 |
| P5 | 4 个导出 native 缺少公共 `.inc` | 第三方需自行声明 | 下个版本补 `include/`；不需要改许可证 |

## 8. 复现本审计

```powershell
# 1) 枚举全部文件（含隐藏文件）与字节数
Get-ChildItem -Recurse -File -Force .

# 2) 检查 SPDX / 版权头覆盖
Get-ChildItem -Recurse -File scripting, tools, configs |
  Select-String -Pattern 'SPDX-License-Identifier'

# 3) 打开发布包，核对内部条目
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::OpenRead('.\dist\TankRun-Control-Center-v2.2.8.zip').Entries |
  ForEach-Object { $_.FullName }

# 4) 段级比对：本地重建 vs 已发布产物
python .\tools\compare-smx.py .\dist\<解出的>.smx .\build\tankdirector_menu.smx

# 5) 空插件实验（验证 __ext_* / __pl_l4dh 来源）
#    新建只含 #include 的 probe.sp 并编译，检查其 .names 是否含这些符号
```

## 9. 关联文档

| 文档 | 内容 |
|---|---|
| [`LICENSE`](../../LICENSE) | GPLv3 官方全文 |
| [`NOTICE`](../../NOTICE) | 三方版权、编译期嵌入、gamedata 披露、商标与免责 |
| [公开发布检查报告](public-release-report.md) | 构建/链接/隐私/发布结论 |
| [公开文件清单](public-file-manifest.md) | 逐文件字节数与 SHA256 |
| [第三方组件](../../third_party/README.md) | 依赖说明与许可一览 |
| [历史：许可待决报告](license-pending-2026-11-20.md) | 决定前的分析记录 |
