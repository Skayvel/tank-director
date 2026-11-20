# Tank Run Control Center v2.2.8 对应源码

下载或收到 `TankRun-Control-Center-v2.2.8-full.zip` 后，将整个包解压到工作目录。插件的完整项目源码就在包内的 `scripting/`，可直接阅读、修改和重新构建。源码与已编译插件一起交付，无需账号或另行申请。

| 位置 | 内容 |
|---|---|
| `scripting/tankdirector_menu.sp` | 控制中心的编译入口 |
| `scripting/tankrun/` | 刷点、轮换、投石、菜单、配置和诊断 |
| `scripting/tankrun_capacity.sp`、`scripting/tankrun_capacity/` | 编译时嵌入控制中心的容量模块 |
| `addons/sourcemod/` | 供服主安装的插件与对应的 Windows x86 gamedata |
| `tools/build.ps1` | Windows 构建脚本 |
| `SHA256SUMS.txt` | 包内文件的 SHA-256 校验值，路径相对于解压目录 |

## 安装

在已装好 MetaMod:Source、SourceMod 和 Left4DHooks 的服务器上，将包内的 `addons/` 合并到游戏的 `left4dead2/addons/`。升级前备份同名文件。重启服务端后用 `sm plugins list` 核对控制中心版本，管理员进游戏输入 `!tank` 打开菜单。

## 从源码构建

准备 Windows 版 [SourceMod 1.12](https://www.sourcemod.net/downloads.php?branch=1.12-dev) 的编译器和配套 include。本次验证使用 SourcePawn **1.12.0.7220**。从 [Left4DHooks 上游](https://github.com/SilvDev/Left4DHooks/tree/main/sourcemod/scripting/include) 获取全套 `left4dhooks*.inc`，包括主头文件及其引入的 anim、silver、lux_library、stocks，放到 SourceMod 的 `scripting/include/` 下。也可通过脚本的 `-ExtraInclude` 指定单独目录。

在解压目录打开 PowerShell，将示例中的路径换成本机 SourceMod 的 scripting 目录，然后运行：

```powershell
.\tools\build.ps1 -Target main -SourceMod 'C:\server\left4dead2\addons\sourcemod\scripting'
Get-FileHash .\addons\sourcemod\plugins\tankdirector_menu.smx -Algorithm SHA256
```

新编译的文件在 `build/tankdirector_menu.smx`，随包发行的插件哈希见 `SHA256SUMS.txt`。编译器、头文件及调试元数据会影响重建文件的哈希。

## 许可与再分发

项目自有代码沿用 GPL-3.0-or-later。发行包所含组合插件按 GPLv3 分发。许可全文见 [LICENSE](LICENSE)，第三方归属、链接例外及 gamedata 说明见 [NOTICE](NOTICE)。若原样转发这一版，直接提供完整 ZIP 即可让接收者同时取得源码与许可；若分发修改后的插件，请随附与修改版相对应的源码和构建说明。
