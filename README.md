# Tank Director · L4D2 Tank Run 控制中心

[Left 4 Dead 2](https://store.steampowered.com/app/550/) **Tank Run** 模式的服务端插件，以及围绕它整理的研究笔记、接口文档与构建工具。

- **控制中心**：让服主控制 AI Tank 何时出现、刷在哪里、满员后换掉哪一只、首次接敌是否优先投石。玩家用原版客户端即可加入。
- **当前版本**：v2.2.8（`tankdirector_menu.smx`）
- **平台**：Windows 专用服务器或本地服务器；SourceMod 1.12
- **状态**：已编译、已打包；**行为效果仍需在真实服务器上验证**——本文档不会把「编译通过」说成「验证通过」

> **许可证：[GPL-3.0-or-later](LICENSE)。** 全文见 [`LICENSE`](LICENSE)；第三方组件的版权归属、编译期嵌入说明与 `gamedata` 签名的披露见 [`NOTICE`](NOTICE)；逐文件版权与来源审计见 [版权与许可证审计](docs/audit/copyright-audit.md)。

---

## 快速开始

**服主**

1. 确认依赖：MetaMod:Source + SourceMod 1.12 + [Left4DHooks](https://github.com/SilvDev/Left4DHooks) + SDKHooks / SDKTools。
2. 下载 [v2.2.8 独立发行包](dist/TankRun-Control-Center-v2.2.8-full.zip)。包内附有 LICENSE、NOTICE、对应源码及[重新构建说明](SOURCE.md)。
3. 解压到工作目录，把其中的 `addons` 合并到游戏的 `left4dead2/addons`，安装后应有：

   ```
   addons/sourcemod/plugins/tankdirector_menu.smx
   addons/sourcemod/gamedata/tankrun_firstrock.txt
   ```

4. 重启服务端，`sm plugins list` 应显示 `Tank Run Control Center 2.2.8`。
5. 管理员聊天输入 `!tank` 打开中文菜单。

完整步骤与校验哈希：[安装指南](docs/getting-started/install.md) · 装完验收：[首次运行与验收](docs/getting-started/quickstart.md)

**开发者**

```powershell
# 需要 SourceMod 1.12 的 spcomp.exe 与同一套 include/（含 Left4DHooks）
.\tools\build.ps1 -SourceMod 'C:\path\to\addons\sourcemod\scripting'
```

见 [构建与编译](docs/project/build.md)。

---

## 仓库内容

| 路径 | 内容 |
|---|---|
| `scripting/` | 插件源码（SourcePawn 1.12） |
| `gamedata/` | DHooks 签名（唯一的一处原生地址依赖） |
| `configs/` | 服务器配置样例 |
| `tools/` | 构建、打包、链接检查、SMX 段级比对脚本 |
| `dist/` | 当前发布包（编译产物） |
| `third_party/` | 外部依赖说明（不包含第三方代码） |
| `docs/` | 文档体系 |

### 文档地图

| 目录 | 内容 | 读者 |
|---|---|---|
| [docs/getting-started/](docs/getting-started/) | 依赖、安装、首跑、升级回滚 | 服主 |
| [docs/user-guide/](docs/user-guide/) | v2.2.8 功能、菜单、命令、参数、预设、验收剧本 | 服主 / 管理员 |
| [docs/reference/](docs/reference/) | 接口参考、**接口稳定性分级**、版本表、文件布局 | 二次开发 |
| [docs/research/](docs/research/) | 研究结论分级：已验证 / 实验性 / 推测 | 研究者 |
| [docs/project/](docs/project/) | 架构、构建、测试、证据政策、贡献指南、**公开发行范围** | 贡献者 |
| [docs/compatibility/](docs/compatibility/) | 32 位基线、64 位计划、**64 位兼容层设计** | 所有人 |
| [docs/operations/](docs/operations/) | 开服、配置、排障 | 运维 |
| [docs/audit/](docs/audit/) | 公开发布检查报告、公开文件清单 | 维护者 |

完整索引：[docs/README.md](docs/README.md)

---

## 接口稳定性

二次开发前请看 [接口稳定性分级](docs/reference/api/stability.md)。它把每个 Command、ConVar、Forward、Native 标成：

- **稳定**：接口形状固定，行为由插件自身实现决定
- **实验性**：依赖未在实服验证的引擎行为，可能随版本调整
- **内部**：实现细节，随时可能变化，不要依赖

示例：`sm_tank_status` 稳定；`tankrun_target_mode` 实验性；`tankrun_bridge` 内部。

---

## 能力边界

插件**不做**的事（这是设计决策，不是缺陷）：

| 做不到 | 原因 |
|---|---|
| 强制 AI 真实投石 | 公开接口没有「强制释放 `CTankRock`」的入口 |
| 阻止已开始的挥击 | `L4D_TankClaw_DoSwing_Pre` 返回 `void`，无法 Handled |
| 改写原生生成坐标 | `L4D_OnSpawnTank` 该版本不提供向量 CopyBack |
| 每 Tank 独立的引擎 ConVar | 引擎 ConVar 是全局的 |
| 原版完整目标选择算法 | 只覆盖经过 `L4D2_OnChooseVictim` 的路径 |

完整清单：[未实现能力与硬限制](docs/reference/api/limits.md)

---

## 容量限制

**「上限 30」不等于「能刷 30 只原版 Tank」。** 每只原版 Tank 占一个客户端槽位。

| 总槽位 | 其他客户端 | 原版 Tank 名额 |
|---:|---:|---:|
| 18 | 1 | 17 |
| 31 | 8 | 23 |
| 31 | 8（预留 1 交接） | 22 |

31 是推荐上限；32 有崩溃风险；同构建客户端在槽位 33 触发 `UpdatePlayerName with bogus slot`。详见 [容量与真人席位](docs/user-guide/capacity-and-seats.md)。

---

## 32 位与 64 位

- **当前只支持 32 位。** 全部地址、偏移、字节签名绑定 `server.dll` SHA256 `a93ec916…`。见 [32 位基线](docs/compatibility/32-bit-baseline.md)。
- **64 位没有实现，也没有做过逆向。** 计划与兼容层设计见 [64 位计划](docs/compatibility/64-bit-plan.md) 与 [64 位兼容层设计](docs/compatibility/64-bit-compat-layer.md)。设计文档不构成实现承诺。

---

## 本仓库**没有**收录什么

公开发行版只包含自行编写的源码、文档与工具。以下内容留在私有研究工作区：

| 未收录 | 原因 |
|---|---|
| 原生逆向报告与反编译转储（`analysis/`） | 对游戏二进制的分析，权利状态未明 |
| 原始证据与 REA 导出（`evidence/`） | 同上；含第三方资料 |
| 机器可读索引（`indexes/`） | 与上述语料配套 |
| 历史构建与备份（`build/`） | 体积大、多为过期产物 |
| 内部派工记录（`docs/coordination/`） | 内部协作材料 |

研究结论的摘要与分级**仍然公开**（`research/`），但原始证据链不在本仓库。详见 [公开发行范围](docs/project/publication-scope.md)。

---

## 已知限制（不要当成「已完成」）

1. **所有行为级改动只做过编译与静态检查。** 游戏内效果由玩家验证。
2. **100 tick 基线。** 128 tick 完全无数据。
3. **遮挡刷点在真实地图上的分布未验证。**
4. **首石成功率未知。** 超时是正常结局。
5. **八个真实 Steam 客户端未验证**（8 席位只用服务器 Bot 测过）。
6. **补位 Tank 不是原版 AI**：地图 NAV + 实体碰撞追击、盒形命中，未复刻翻越/爬梯/逐骨骼命中。

逐版本验证状态见 [版本表](docs/reference/versions.md)。

---

## AI 参与说明

本项目在原生 Tank AI 代码分析辅助、SourcePawn 实现与审查、技术文档整理、构建结果比对及开源发布审计中使用过 AI 工具。AI 提供了研究和开发辅助，但其输出可能存在错误；相关结论不因 AI 参与而被视为已验证，尤其不能替代真实服务器上的行为测试。

项目维护者负责采纳、修改和发布相关成果，并对版权及第三方许可进行核查。AI 工具不作为本项目的著作权人。社区贡献与第三方依赖的署名及许可分别按实际来源保留。

---

## 致谢

[SourceMod](https://www.sourcemod.net/) · [MetaMod:Source](https://www.sourcemm.net/) · [Left4DHooks](https://github.com/SilvDev/Left4DHooks) · [L4DToolZ](https://github.com/lakwsh/l4dtoolz) · [hl2sdk](https://github.com/alliedmodders/hl2sdk)

第三方组件的许可与分发边界见 [third_party/README.md](third_party/README.md)。
