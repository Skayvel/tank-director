# 架构与仓库结构

## 1. 三条产出

本仓库不是单一产品，而是三条产出共用一棵树：

| 产出 | 形态 | 目录 |
|---|---|---|
| A. 原生逆向 | 报告 + 索引 + 证据，**无运行时代码** | `analysis/`、`evidence/`、`indexes/` |
| B. Tank Run 控制中心 | 已发布到 v2.2.8 的服务端插件 | `scripting/`、`build/releases/` |
| C. 辅助工具 | 记录器、容量、席位、Tick 采样、M08 记录器 | `scripting/`、`build/` |

三者共享同一套证据纪律，但**发布时间线不同步**：研究文档停在 v2.0.1 基线，产品已到 v2.2.8。这是本仓库最容易误解的地方，[版本表](../reference/versions.md) 说明了正确读法。

## 2. 插件内部分层

```
tankdirector_menu.sp          统一入口：生命周期、事件、命令注册
├── tankrun/tr_core.inc       参数表、ConVar、VScript 桥、工具函数
├── tankrun/tr_tank.inc       生成、轮换、槽位、伤害钩子
├── tankrun/tr_lead_spawn.inc 前沿选位、遮挡检查、NAV
├── tankrun/tr_rock.inc       投石/首石/拳击事件与原生 detour
├── tankrun/tr_status.inc     状态输出、诊断导出、依赖探测
├── tankrun/tr_menu.inc       11 个菜单分节
├── tankrun/tr_diag.inc       命令处理
└── tankrun_capacity.sp       内嵌补位实体（#define TR_EMBEDDED_CAPACITY）
```

依赖方向：`tr_*.inc` 之间通过 `tr_core.inc` 的全局与工具通信；`tankdirector_menu.sp` 顺序 include，因此**顺序不能随意调整**（`tr_rock.inc` 需要先 `#undef REQUIRE_EXTENSIONS` 再 `#include <dhooks>`）。

### 六层研究架构（研究侧）

| 层 | 产物 |
|---|---|
| L0 原始证据 | `evidence/` 下的 records/dossiers/exports/supplemental/packages |
| L1 静态逆向 | 函数/CFG/调用/虚表索引 |
| L2 语义重建 | 函数档案、行为关系、claims |
| L3 引擎依赖 | 时间、实体、感知、导航、运动、动画、武器/物理契约 |
| L4 复刻实现 | **未开始** |
| L5 验证与差分 | 静态逐条件对照；动态仅按需 |

原始定义见 `docs/architecture.md`（注意该文件开头标注了「历史架构」）。

## 3. 仓库结构

```
tank-director/
├── README.md                项目主页
├── AGENTS.md                协作 AI 的工作约定与边界（非对外文档）
├── docs/                    文档
│   ├── README.md            文档总索引
│   ├── getting-started/     安装与首跑
│   ├── user-guide/          当前版本使用
│   ├── reference/           接口与版本事实
│   ├── research/            研究结论分级
│   ├── project/             仓库自身（本目录）
│   ├── compatibility/       32 位现状、64 位计划
│   ├── operations/          开服、配置、排障
│   ├── audit/               本次整理的审计与交付
│   ├── archive/             被替换的旧文档存档
│   ├── player/              旧玩家说明（已重定向）
│   ├── coordination/        派工与验收记录
│   └── *.md                 37 份历史工作报告
├── scripting/               SourceMod 源码
├── build/                   编译产物、发布包、历史备份（体积大）
├── analysis/                原生逆向报告与反编译转储
├── evidence/                原始证据、REA 导出、解码副本
├── indexes/                 机器可读索引与状态
├── configs/                 服务器配置样例
├── gamedata/                DHooks 签名
├── history/                 历史架构快照（冻结）
└── tools/                   索引重建、审计、打包、检查脚本
```

## 4. 每个目录放什么

| 目录 | 内容 | 能不能改 |
|---|---|---|
| `analysis/` | 按轮次的原生研究报告 + 反编译 `.c` / `.asm` 转储 | 只增不改 |
| `evidence/` | 原始证据、REA 导出、解码副本、dossier | **只增不改** |
| `indexes/` | 函数目录、分支账本、证据索引、状态 | 可重建，但 `status.json` 是权威 |
| `history/` | 2026-11-12 的架构快照 | 冻结 |
| `build/` | 编译产物、发布 ZIP、备份目录 | 只增；发布包不要覆盖 |
| `configs/` | 服务器配置样例 | 可改 |
| `gamedata/` | DHooks 签名 | 改就必须同时更新注释里的 SHA256 |
| `tools/` | Python/PowerShell 工具 | 可改 |

## 5. 为什么研究目录不搬

研究报告之间、报告与 `indexes/` 之间有大量相对路径交叉引用。搬动会同时破坏两类链接，而且违反「不删除原始证据」。

因此新文档体系只做两件事：**在上面加导航层**，**把结论分级呈现**。原文位置不动。

## 6. 数据流

```
原始 DLL/VPK ──静态分析──→ analysis/ 报告
                  │              │
                  ├─→ evidence/（REA 导出、dossier、字节）
                  └─→ indexes/（函数、分支、调用、状态）
                                  │
                                  ▼
                        docs/research/（分级结论）
                                  │
                                  ▼
                        scripting/（产品实现）
                                  │
                                  ▼
                        build/releases/（发布包）
```

**注意方向**：产品实现不直接依赖 `analysis/` 的结论，而是依赖「已确认的公开接口」。这就是为什么插件文档反复强调「只用公开接口」。
