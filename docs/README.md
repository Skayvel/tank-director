# 文档总索引

本仓库是**公开发行版**，文档按读者分流。原始研究工作记录（反编译转储、证据、索引）不在本仓库，见 [公开发行范围](project/publication-scope.md)。

## 按你的身份

| 我是谁 | 从这里开始 |
|---|---|
| 第一次装插件的服主 | [依赖](getting-started/requirements.md) → [安装](getting-started/install.md) → [首跑验收](getting-started/quickstart.md) |
| 已经在用、想查命令和参数 | [用户指南](user-guide/control-center.md) → [命令参考](reference/api/commands.md) → [参数参考](reference/api/convars.md) |
| 想改参数但不确定后果 | [参数参考](reference/api/convars.md) 的「验证状态」列 + [接口稳定性分级](reference/api/stability.md) + [未实现能力](reference/api/limits.md) |
| 想复现作者的测试 | [验收剧本](user-guide/validation-playbook.md) → [诊断](user-guide/diagnostics.md) |
| 想读懂原生研究的结论 | [结论分级](research/verified-findings.md) → [研究方法](research/method.md) |
| 想接手原生研究 | [未解决问题](research/open-questions.md) → [研究路线](research/roadmap.md) → [原生机制导航](research/native-ai/README.md) |
| 想二次开发或移植 | [架构](project/architecture.md) → [接口参考](reference/api/README.md) → [接口稳定性](reference/api/stability.md) → [构建](project/build.md) |
| 担心 64 位 | [32 位基线](compatibility/32-bit-baseline.md) → [64 位计划](compatibility/64-bit-plan.md) → [64 位兼容层设计](compatibility/64-bit-compat-layer.md) |
| 想复核这次公开发行 | [公开发布检查报告](audit/public-release-report.md) → [公开文件清单](audit/public-file-manifest.md) |

## 目录

### getting-started — 安装与首跑

| 文档 | 说明 |
|---|---|
| [requirements.md](getting-started/requirements.md) | 依赖、版本、平台限制 |
| [install.md](getting-started/install.md) | 首次安装（控制台 / 手动 / 插件平台） |
| [quickstart.md](getting-started/quickstart.md) | 装完 5 分钟内的验收动作 |
| [upgrade-and-rollback.md](getting-started/upgrade-and-rollback.md) | 版本升级、回滚、卸载与参数恢复 |

### user-guide — 当前版本使用

| 文档 | 说明 |
|---|---|
| [control-center.md](user-guide/control-center.md) | **v2.2.11 完整用户指南** |
| [menu-reference.md](user-guide/menu-reference.md) | 11 个菜单分节逐项说明 |
| [spawn-modes.md](user-guide/spawn-modes.md) | 五种生成方式、距离段、遮挡规则、轮换 |
| [capacity-and-seats.md](user-guide/capacity-and-seats.md) | 客户端槽位、30 Tank、8 人席位、补位实体 |
| [presets.md](user-guide/presets.md) | 预设槽、设置持久化、原值恢复 |
| [diagnostics.md](user-guide/diagnostics.md) | 日志、诊断导出、Tick 采样怎么读 |
| [validation-playbook.md](user-guide/validation-playbook.md) | 逐项实服验收剧本 |

### reference — 事实与接口

| 文档 | 说明 |
|---|---|
| [api/README.md](reference/api/README.md) | 接口参考的总说明与边界 |
| [api/commands.md](reference/api/commands.md) | 17 个命令，含权限与参数 |
| [api/convars.md](reference/api/convars.md) | 55 个 `tankrun_*` 参数、内部桥接与 81 项参数表 |
| [api/left4dhooks.md](reference/api/left4dhooks.md) | 实际使用的 Left4DHooks / SourceMod 回调及其限制 |
| [api/vscript-bridge.md](reference/api/vscript-bridge.md) | 与 Tank Run 模式脚本交换的字段 |
| [api/files.md](reference/api/files.md) | 服务器文件布局、配置文件、日志格式 |
| [api/gamedata.md](reference/api/gamedata.md) | DHooks 签名与构建绑定 |
| [api/stability.md](reference/api/stability.md) | **接口稳定性分级：稳定 / 实验性 / 内部** |
| [api/limits.md](reference/api/limits.md) | **明确未实现的能力与原因** |
| [versions.md](reference/versions.md) | 版本表（唯一来源） |
| [tools.md](reference/tools.md) | 辅助插件参考 |

### research — 原生逆向

| 文档 | 说明 |
|---|---|
| [method.md](research/method.md) | 证据等级、函数等级、结论状态 |
| [verified-findings.md](research/verified-findings.md) | 已验证结论（指令级） |
| [experimental-findings.md](research/experimental-findings.md) | 已实现但游戏内未验证 |
| [open-questions.md](research/open-questions.md) | 未闭合问题与下一步入口 |
| [roadmap.md](research/roadmap.md) | 研究路线 |
| [native-ai/](research/native-ai/README.md) | 按机制分册的原生机制导航 |
| [interfaces/](research/interfaces/README.md) | 原生 ConVar、PC01–PC26 映射、公开 Hook |

### project — 仓库自身

| 文档 | 说明 |
|---|---|
| [architecture.md](project/architecture.md) | 三条产出的分层与边界 |
| [architecture.md](project/architecture.md) | 分层、仓库结构、数据流 |
| [build.md](project/build.md) | 编译、打包、校验 |
| [testing.md](project/testing.md) | 静态检查 → 编译 → 隔离服 → 实服 |
| [evidence-policy.md](project/evidence-policy.md) | 证据纪律的操作细则 |
| [publication-scope.md](project/publication-scope.md) | 公开发行范围：收录与排除的依据 |
| [contributing.md](project/contributing.md) | 提交前必须满足的条件 |
| [glossary.md](project/glossary.md) | 术语表 |
| [history.md](project/history.md) | 五个阶段的时间线 |

### compatibility — 平台

| 文档 | 说明 |
|---|---|
| [32-bit-baseline.md](compatibility/32-bit-baseline.md) | 当前唯一支持的平台，及其具体依赖点 |
| [64-bit-compat-layer.md](compatibility/64-bit-compat-layer.md) | 原生依赖的隔离层设计（**未实现**） |
| [64-bit-plan.md](compatibility/64-bit-plan.md) | 若出现 64 位构建需要做什么（计划，非结论） |

### operations — 运维

| 文档 | 说明 |
|---|---|
| [server-setup.md](operations/server-setup.md) | 开服配置、启动脚本 |
| [configs.md](operations/configs.md) | `configs/` 下每个文件的用途 |
| [troubleshooting.md](operations/troubleshooting.md) | 常见故障与排查顺序 |

### audit — 公开发行

| 文档 | 说明 |
|---|---|
| [public-release-report.md](audit/public-release-report.md) | 公开发布检查报告：收录范围、构建验证、链接审计、许可待决 |
| [copyright-audit.md](audit/copyright-audit.md) | **逐文件版权与许可证审计、依赖矩阵、高风险项** |
| [public-file-manifest.md](audit/public-file-manifest.md) | 公开文件清单与关键文件哈希 |
| [publication-scope.md](project/publication-scope.md) | 哪些内容没有公开，以及原因 |

## 本仓库没有收录什么

本仓库是**公开发行版**，只包含自行编写的插件源码、接口文档与构建工具。以下内容**不在**本仓库中：

| 未收录 | 原因 |
|---|---|
| 原生逆向报告（`analysis/`） | 私有研究工作区，含对游戏二进制的分析与第三方资料 |
| 原始证据（`evidence/`） | 含反编译转储与 REA 导出，权利状态未明 |
| 机器可读索引（`indexes/`） | 与上述材料配套，离开语料无意义 |
| 历史构建与备份（`build/`） | 体积大且多数为过期产物 |
| 内部派工与验收记录（`docs/coordination/`） | 属于内部协作材料 |

研究结论的**摘要与分级**仍然公开（`research/`），但原始证据链留在私有仓库。详见 [公开发行范围](project/publication-scope.md)。

**注意**：文档中的版本描述对应各自写作时点。当前版本事实以 [versions.md](reference/versions.md) 为准。
