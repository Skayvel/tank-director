> **已失效（历史记录）。** 本文件是 2026-11-20 许可证决定前的待决报告，保留用于追溯。
> 最终决定为 **GPL-3.0-or-later**：许可证全文见 [`LICENSE`](../../LICENSE)，
> 第三方归属与披露见 [`NOTICE`](../../NOTICE)，
> 逐文件版权审计见 [版权与许可证审计](copyright-audit.md)。

---
# 许可证待定

**本仓库目前没有 `LICENSE` 文件。**

没有许可证 = 默认保留所有权利。任何人都不能合法复制、修改或再分发本仓库内容，即使仓库公开可见。这是本次发布唯一的硬阻塞项。

本文只做**事实梳理与选项分析**，不替所有者做选择。

---

## 1. 本仓库包含什么（决定许可证范围）

| 内容 | 权利状态 | 是否含他人代码 |
|---|---|---|
| `scripting/*.sp`、`scripting/**/*.inc` | 本项目作者拥有 | **否**（只 `#include` 他人头文件） |
| `gamedata/tankrun_firstrock.txt` | 由分析游戏二进制得出 | 否，但不含游戏代码 |
| `docs/**` | 本项目作者拥有 | 否（引用链接，未复制内容） |
| `configs/**`、`tools/**` | 本项目作者拥有 | 否 |
| `dist/*.zip` | 本项目编译产物 | 否 |

**本仓库不包含**任何第三方源代码、二进制、游戏资源或反编译转储。

---

## 2. 依赖的许可证事实

| 组件 | 许可证 | 与本项目的关系 | 来源 |
|---|---|---|---|
| **SourceMod**（含 SourcePawn 编译器、`sourcemod.inc`、`sdktools.inc`、`sdkhooks.inc`） | **GPL-3.0**，附特殊例外 | 编译期 `#include` 头文件；运行期由服务器加载 | [LICENSE.txt](https://raw.githubusercontent.com/alliedmodders/sourcemod/master/licenses/LICENSE.txt) |
| **Left4DHooks**（`left4dhooks.inc`） | **GPL-3.0** | 编译期 `#include`；运行期调用其 forward/native | 上游源码文件头（Copyright Silvers） |
| **DHooks**（`dhooks.inc`、`DynamicDetour`） | **GPL-3.0** | 编译期 `#include <dhooks>`；运行期可选 | SourceMod 扩展，同上许可证体系 |
| **L4DToolZ** | **未声明许可证**（仓库无 LICENSE 文件） | 仅作为**可选外部依赖**在文档中提及，不分发 | [仓库 README](https://github.com/lakwsh/l4dtoolz) 无许可章节 |

### 2.1 SourceMod 的例外条款（关键）

SourceMod 的 `LICENSE.txt` 原文（2019-9-30 版）有两段对本项目直接相关：

> As a special exception, AlliedModders LLC gives you permission to link the code of this program (as well as its derivative works) to "Half-Life 2," the "Source Engine" and any Game MODs…
>
> As an additional special exception to the GNU General Public License 3.0, AlliedModders LLC permits dual-licensing of **DERIVATIVE WORKS ONLY (that is, SourcePawn/SourceMod Plugins and SourceMod Extensions**, or any software built from the SourceMod SDK or header files) under the GNU General Public License version 2 "or any higher version."

两点含义：

1. SourceMod 把 **插件视为衍生作品**（原文明确列出 "SourcePawn/SourceMod Plugins"）。
2. 它允许这些衍生作品用 **GPL-2.0-or-later** 授权，作为 GPL-3.0 的替代。

也就是说：**SourceMod 一侧没有给出「插件可以用 MIT」的许可，只给了 GPLv2+ 的放宽。**

### 2.2 Left4DHooks 一侧

Left4DHooks 是 SourceMod 扩展，源码头声明 GPL-3.0，**没有**声明针对插件作者的额外例外。本项目在编译期 `#include` 它的 `left4dhooks.inc`（只有 `native`/`forward` 声明与少量 stocks），运行期调用其接口。

这个「头文件包含 + 接口调用」是否构成衍生作品，属于法律判断，不是技术判断。**风险高低取决于所选许可证**：

- 选 GPL-3.0 → 与 Left4DHooks 一致，无冲突。
- 选 GPL-2.0-or-later → 通过 "or later" 可与 GPLv3 组件共存。
- 选 MIT/Apache → 需要主张 `.inc` 只是接口声明、不构成衍生；这是社区常见做法，但**不是**上述任一许可证明文允许的。

### 2.3 L4DToolZ

仓库**没有 LICENSE 文件**，README 也没有许可章节。因此默认保留所有权利。

本项目**不分发**它，仅在文档中把它列为可选依赖并给出上游链接。这不需要额外许可。但如果将来要打包它，必须先取得授权。

### 2.4 gamedata 的字节签名

`gamedata/tankrun_firstrock.txt` 是一段 40 字节的 x86 指令模式（含 4 处 `2A` 通配），用于在 `server.dll` 中定位 `CTankClaw::PrimaryAttack`。它：

- **不含**游戏代码、资源或符号表；
- 是逆向分析得出的**功能性事实**（函数入口的指令形状）；
- 与具体构建绑定（注释中记录了 SHA256 与 VA）。

风险较低，但它是「从受版权保护的二进制中提取的信息」。建议在所选许可证之外，于 `NOTICE` 中**主动披露**其来源与用途。

---

## 3. 候选方案（不替所有者决定）

### 方案 A：GPL-3.0（最省事、最贴合生态）

| 项 | 评价 |
|---|---|
| 与 SourceMod 例外条款 | 完全符合（GPLv3 是允许集合的上界） |
| 与 Left4DHooks / DHooks | 一致，无冲突 |
| 社区预期 | L4D2 SourceMod 插件生态普遍使用 GPL |
| 代价 | 衍生插件也必须开源；商用闭源集成受限 |

### 方案 B：GPL-2.0-or-later

| 项 | 评价 |
|---|---|
| 与 SourceMod 例外条款 | 明文允许（"or any higher version"） |
| 与 GPLv3 依赖共存 | 可以（可升级到 v3） |
| 代价 | 文本较旧，专利条款弱于 v3 |

### 方案 C：MIT / Apache-2.0（宽松）

| 项 | 评价 |
|---|---|
| 与 SourceMod 例外条款 | **需要额外法律主张**：`.inc` 只是接口，不构成衍生作品 |
| 与 Left4DHooks | 同上，风险更高（它是 GPLv3 且无插件例外） |
| 社区预期 | 有先例，但不是默认 |
| 代价 | 最宽松、最易被商用采用；风险由所有者承担 |

### 方案 D：源码可见但禁止再分发（非开源）

| 项 | 评价 |
|---|---|
| 与依赖 | 不冲突（不分发衍生作品） |
| 社区预期 | 社区通常不会把它当开源项目对待 |
| 代价 | 与「供社区下载、研究、二次开发」的目标直接冲突 |

### 文档是否需要单独许可

`docs/` 可以选择与代码相同的许可证，或单独使用 CC BY 4.0。若单独授权，请在 `LICENSE` 与文档页脚同时写明，避免歧义。

---

## 4. 建议（供决策参考）

如果目标是「社区可下载、研究、二次开发」，且希望**不引入法律不确定性**：

1. **首选方案 A（GPL-3.0）**：与全部依赖一致，不需要任何额外论证。
2. 若不希望强 copyleft 外溢，**方案 B（GPL-2.0-or-later）** 是 SourceMod 明文允许的最低成本替代。
3. 只有在所有者愿意承担「接口使用不构成衍生作品」这一法律主张时，才考虑方案 C。
4. 无论选哪个，建议同时产出 `NOTICE`，写明：
   - 依赖 SourceMod / Left4DHooks / DHooks（均为 GPLv3）；
   - `gamedata` 签名的来源与用途；
   - 不包含游戏文件与第三方代码。

**本文不构成法律意见。** 涉及商业分发时请咨询律师。

---

## 5. 选定后要做的四件事

1. 在仓库根目录添加 `LICENSE`（GPL 全文或 SPDX 头 + `LICENSES/`）。
2. 把本文件改为 `NOTICE`，或在 README 顶部替换「许可证未定」的提示。
3. （可选）在每个源文件头部加 SPDX 标识，例如：
   `// SPDX-License-Identifier: GPL-3.0-or-later`
4. 重新运行 [公开发布检查报告](public-release-report.md) 的 §8 许可项，确认从 ❌ 变为 ✅。

在此之前，**请勿再分发本仓库内容。**
