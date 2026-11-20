# 接口参考

本目录记录控制中心 v2.2.8 **实际存在**的接口，以及与它交互的外部接口。每一条都能在源码或 gamedata 里找到对应位置。

## 这份参考的边界

**是**接口文档的内容：

- 插件注册的命令、ConVar、参数表项
- 插件实现的 forward、调用的 native、订阅的游戏事件
- **本仓库导出给其他插件的 native**（仅独立 `tankrun_capacity.smx`，共 4 个）
- 插件读写的 VScript 会话字段与桥协议
- 插件使用的 gamedata 签名与平台限制
- 插件在服务器上创建的文件与格式

**不是**接口文档的内容：

- 原生游戏函数的「可调用签名」。项目恢复了地址与语义，**没有**恢复可被 `SDKCall` 直接使用的函数签名；地址不等于签名。
- 计划中的接口。没有实现的能力一律进 [未实现能力](limits.md)。
- 只有研究结论、没有产品实现的入口（例如 26 项参数映射里「未接入」的项目）。

## 索引

| 文档 | 内容 |
|---|---|
| [commands.md](commands.md) | 17 个命令：权限、参数、返回值、失败提示 |
| [convars.md](convars.md) | 35 个自有 ConVar + 内部桥接 + 57 项参数的完整表 |
| [left4dhooks.md](left4dhooks.md) | 14 个 Left4DHooks forward、1 个 SourceMod forward、调用的 native、8 个事件、自定义 detour |
| [vscript-bridge.md](vscript-bridge.md) | 桥协议、六个会话字段、接管/交还 |
| [files.md](files.md) | 服务器文件布局、配置格式、日志格式 |
| [gamedata.md](gamedata.md) | 唯一原生签名及其平台与更新风险 |
| [limits.md](limits.md) | **未实现能力与硬限制** |

另有：

- [../versions.md](../versions.md)：版本表与产物哈希（版本号的唯一来源）
- [../tools.md](../tools.md)：辅助插件参考

## 接口的稳定性分级

| 级别 | 含义 | 举例 |
|---|---|---|
| 稳定 | 插件自有、语义由插件定义 | `tankrun_*` 参数、`sm_tank_*` 命令 |
| 半稳定 | 依赖 SourceMod 约定，升级 SourceMod 一般不变 | `HookEvent` 名称、`ADMFLAG_*` 位 |
| 依赖外部 | 由第三方面扩展定义，版本变化会破 | Left4DHooks forward 签名与行为 |
| 依赖游戏构建 | 随 `server.dll` 变化而失效 | `tankrun_firstrock.txt` 签名 |
| 非接口 | 内部实现细节，随时可改 | `tankrun_bridge` 的报文格式、`TR_PARAM_MAX` |

二次开发时只需保证前两级；后三级要么跟着升级，要么明确写清支持的版本。

## 一个重要提醒

`analysis/tankrun-interface-inventory-20261115/interfaces.md` 是 2026-11-15 的接口清单，其中「当前项目实际接入」一节描述的是**当时**的 `tankdirector_menu`（v1.0.0 形态：4 个原生 ConVar + 3 个脚本字段）。v2.2.8 的接口规模远大于它。

两份文档不冲突，但**不要拿它当当前接口表**。当前接口以本目录为准。
