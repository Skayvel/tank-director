# 32 位基线

**当前唯一支持的平台。** 本文只记录事实，不含计划；计划见 [64 位兼容计划](64-bit-plan.md)。

## 1. 环境事实

| 项 | 值 |
|---|---|
| 平台 | Windows **x86（32 位）** |
| 安装路径 | `C:\Program Files (x86)\Steam\steamapps\common\Left 4 Dead 2\` |
| 首选映像基址 | `0x10000000` |
| 指针宽度 | 4 字节 |
| 地址形式 | 静态 VA（如 `0x103e3860`），不是运行时地址 |
| 调用约定 | `thiscall`（MSVC x86） |

## 2. 构建身份

| 文件 | 字节数 | SHA256 |
|---|---:|---|
| `left4dead2/bin/server.dll` | 9,130,288 | `a93ec916a8f26fbea85688c321d2ba8dd346d435fab09a0b926dd950c2e1ea12` |
| `bin/client.dll` | 8,305,664 | `c0a8e1e88f7312db11bcfd4b0ec574d224a34b2d9a615d6088b28335eeb4af1f` |
| `bin/engine.dll` | 4,817,712 | `8a6d468059394bf4cfce32f11dcba12ef45de88da1373084d5a2916d0bf7ba86` |
| `bin/vphysics.dll` | 1,693,488 | `45aa54cd8907b62ba1e7798ed3b3a27575ba761b500968338dcdfbf3ee93327d` |
| `bin/vscript.dll` | 493,872 | `1d603257c0fd63561895495e2d119f501632f12e1ca74c22240eb7abfdef7852` |
| `left4dead2/gameinfo.txt` | 1,670 | `e1d1bae00e7de0d3ec6fd2b335ec5377be0ef1b94a17f6a2df6d2496403ee887` |
| `left4dead2/pak01_dir.vpk` | 3,798,698 | `51cc2957bbc5fd545357fd6b8da0fd5c387243756cd6f82e067d5e256e48fcdd` |
| `update/pak01_dir.vpk` | 859,687 | `47f31ef57a94f88463da8b2132a55cfed14bae127f35e4cfb68948e840ed8b52` |

PatchVersion 声明 `2.2.4.3`。完整清单见 `indexes/artifacts.json`。2026-11-25 检测到 Steam `server.dll` 更新至 `6c543884961f87b8a86cebfc980eabc8501b63387397f1ee583d9e32cecc9950`（9,130,800 字节），经字节扫描核实 `gamedata/tankrun_firstrock.txt` 签名在新旧两构建中均唯一命中 1 处（1/1）。

## 3. 32 位假设出现在哪些地方

这些是**真正依赖 32 位的地方**，不是「可能有关」。

| # | 依赖点 | 位置 | 32 位下的具体表现 |
|---|---|---|---|
| 1 | 字节签名 | `gamedata/tankrun_firstrock.txt` | 模式串里 `55 8B EC 51 A1` 是 x86 序言；`2A` 通配的是 32 位绝对地址与 32 位相对位移 |
| 2 | 硬编码 VA | 所有报告与注释 | `0x103e3860` 是 32 位 VA；64 位下地址空间与基址都不同 |
| 3 | 结构体偏移 | 报告中的 `+0x1c`、`+0x1c0`、`+0x648` 等 | 32 位指针 4 字节；64 位下含指针的结构体偏移几乎全变 |
| 4 | `thiscall` | `DynamicDetour(..., CallConv_THISCALL, ...)` | x86 MSVC 专有；x64 统一调用约定 |
| 5 | SourceMod 二进制 | `spcomp.exe` 与 `.smx` | 32 位插件二进制与宿主架构绑定 |
| 6 | Left4DHooks | 扩展二进制 | 按 32 位 `server.dll` 编译与签名 |
| 7 | L4DToolZ | `sv_setmax` 补丁 | 按 32 位客户端/服务端指令打补丁 |
| 8 | 客户端槽位证据 | `0x1008ac14` 循环 | 32 位客户端代码 |

## 4. 32 位下已知的硬限制

| 限制 | 数字 | 来源 |
|---|---|---|
| 客户端槽位 | 31（32 有崩溃风险） | L4DToolZ 上游 + 项目实测 |
| 槽位 33 崩溃 | `UpdatePlayerName with bogus slot` | 同构建 `client.dll` 指令 |
| 参数表容量 | 84 项（当前已注册 81 项） | `TR_PARAM_MAX` |
| 服务器单进程可用内存 | 32 位地址空间 | 平台限制 |

## 5. 32 位下的验证状态

| 项 | 状态 |
|---|---|
| 100 tick 行为 | 有实服/本地样本 |
| 128 tick 行为 | **未验证** |
| 8 个真实 Steam 客户端 | **未验证** |
| 32 槽 | **未实测** |
| Linux 服务器 | **未测试**（按代码推断：detour 不安装，其余功能可用） |
| 客户端画面层 | **无采样数据** |

## 6. 相关文档

- [64 位兼容计划](64-bit-plan.md) —— 计划，不是结论
- [版本表](../reference/versions.md) —— 版本与产物哈希
- [gamedata](../reference/api/gamedata.md) —— 唯一原生签名的细节
- [容量与席位](../user-guide/capacity-and-seats.md) —— 槽位预算
