# 构建

## 1. 编译控制中心

### 前置

- SourceMod **1.12** 的 `spcomp.exe` 与**同一套** `include/`。
- 源码包括 `scripting/tankdirector_menu.sp`、`scripting/tankrun/` 下的 `.inc`，以及内嵌的 `scripting/tankrun_capacity.sp` 和 `scripting/tankrun_capacity/` 下的 `.inc`。

### 命令

```powershell
$sm     = 'D:\path\to\sourcemod\addons\sourcemod\scripting'
$inc    = Join-Path $sm 'include'
$spcomp = Join-Path $sm 'spcomp.exe'

& $spcomp .\scripting\tankdirector_menu.sp "-i$inc" '-o.\build\tankdirector_menu.smx'
```

### 严格模式

发布版使用 `-E`（警告视为错误）。当前源码在严格模式下 0 error / 0 warning。

历史记录中的编译器版本有两个：≤2.1.7 用 1.12.0.7260（项目工具链），≥2.1.8 用 1.12.0.7220（Steam 安装内的 SourceMod）。两者都是 1.12，源码兼容。

### 项目脚本

`tools/build_tankrun_control.ps1` 会额外写：

- `build/tankdirector_menu_build.log`
- `build/tankdirector_menu_deployment.json`（源码/编译器/产物三个 SHA256）

**注意**：spcomp 对同一源码多次编译可能产生不同 SMX 哈希与栈大小估算（编译器内部非确定性，实测栈字段在 19k–25k 间波动）。因此**以部署记录里那一份为准**，不要用「重编后哈希不同」判断源码被改过。

## 2. 编译辅助插件

| 插件 | 构建脚本 |
|---|---|
| `tankdirector_menu` | `tools/build_tankrun_control.ps1` |
| `tankrun_capacity` | 无独立脚本；由控制中心内嵌编译 |
| `tankrun_human_seats` | 直接 `spcomp` |
| `tankrun_tick_telemetry` | 直接 `spcomp` |
| `tankrun_m08_probe` | 直接 `spcomp` |
| `tankrun_spawn_logger` v02–v023 | `tools/build_tankrun_spawn_logger_v02/v021/v022/v023.ps1` |
| `tankdirector_menu` 早期版本 | `tools/build_tankdirector_menu.ps1` |

## 3. 打包

发布包结构（v2.2.5 起）：

```
TankRun-Control-Center-vX.Y.Z.zip
└── addons/sourcemod/
    ├── plugins/tankdirector_menu.smx
    └── gamedata/tankrun_firstrock.txt
```

2.1.6 / 2.1.7 的包还包含 `addons/sourcemod/scripting/` 源码与 `README.md`。

## 4. 发布前检查

```powershell
# 1) 编译产物哈希
Get-FileHash .\build\tankdirector_menu.smx -Algorithm SHA256

# 2) 与已发布产物做「段级」比较（不是比整包哈希，理由见 4.1）
python .\tools\compare-smx.py .\dist\<已发布>.smx .\build\tankdirector_menu.smx

# 3) 包内容
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::OpenRead('.\dist\TankRun-Control-Center-v2.2.8.zip').Entries |
  ForEach-Object { $_.FullName }
```

确认：

1. 编译 0 error / 0 warning（严格模式）；
2. `compare-smx.py` 报告 `.code` / `.publics` / `.pubvars` / `.natives` 全部 `YES`；
3. ZIP 只包含 `plugins/tankdirector_menu.smx` 与 `gamedata/tankrun_firstrock.txt`；
4. `gamedata` 里的 SHA256 注释与当前目标构建一致；
5. SMX 的 `.names` 里没有构建者的绝对路径（见 4.2）。

### 4.1 为什么不能比整包哈希

spcomp 产生的 SMX **不是字节可复现的**。同一份源码、同一套 include，两次编译会得到不同的文件哈希，原因有三：

| 差异来源 | 影响 | 能否消除 |
|---|---|---|
| 栈/堆大小估算 | `.data` 头部 4 字节中的 stack/heap 字段 | 否，编译器内部行为（实测在 19k–25k 间波动） |
| 编译时间戳 | `.data` 尾部 6 字节 ASCII（`HH:MM:SS`） | 否 |
| include 目录的绝对路径写法 | `.names` 中的 7 个路径字符串（分隔符风格） | 部分：用相对路径编译项目源文件 |

所以**是否同源**只能靠比较 payload（`.code` 等）判断，不能靠哈希。

### 4.2 编译路径会被写进 SMX

`.names` 段保存了每个源文件与 include 的路径字符串。如果调用 spcomp 时传**绝对路径**，构建者的目录结构（例如 `D:\...\scripting\...`）就会被封进发布的二进制。

`tools/build.ps1` 因此固定以**仓库根目录 + 相对路径**（`scripting/<file>.sp`，正斜杠）调用编译器。发布前请确认 `.names` 里只有 `scripting/...` 这类相对项。

## 5. 仓库内工具

| 工具 | 作用 |
|---|---|
| `tools/build.ps1` | 编译全部或指定插件；严格模式默认开启；写 `build/build-report.txt` |
| `tools/package.ps1` | 按发布布局打包 ZIP；生成 `dist/SHA256SUMS.txt` |
| `tools/check-links.ps1` | Markdown 链接审计（分类互斥，带自检等式） |
| `tools/compare-smx.py` | SMX 段级比较（`.code` / `.data` / …），用于判断是否同源 |

前三个是 PowerShell（Windows），最后一个需要 Python 3.8+。它们都是**检验工具**，通过不代表游戏内行为正确。

## 6. 不要做的事

- 不要用不同 SourceMod 版本的 include 混编。
- 不要在没记录哈希的情况下覆盖已发布的 SMX。
- 不要修改 `gamedata` 签名却不更新注释里的 SHA256 与 VA。
- 不要把 `.smx` 提交进 Git（`.gitignore` 已忽略 `*.smx`）。
