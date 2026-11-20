# 安装指南

三种安装方式，按你的环境选一种。**升级已有安装请看 [升级与回滚](upgrade-and-rollback.md)**，不要直接照本文覆盖。

## 安装前确认

1. 服务器能加载 MetaMod:Source 与 SourceMod：控制台输入 `meta version`、`sm version`，两条都要有输出。
2. SourceMod 版本是 **1.12**。低于 1.12 的 SourceMod 不能加载 1.12 编译的插件。
3. 已安装 Left4DHooks：`sm exts list` 中能看到，或 `sm plugins list` 里有 `Left 4 DHooks Direct`。
4. 服务器当前是 Tank Run 模式（`mp_gamemode tankrun`）。插件在其他模式下也能加载，但生成、阶段、脚本字段相关功能没有意义。
5. 记下当前`addons/sourcemod/plugins/`目录的插件清单，便于回滚比对。

> 插件只在引擎不是 L4D2 时主动 `SetFailState`。依赖扩展缺失时是 SourceMod 拒绝加载，不是插件自己报错，所以**先看 `sm exts list`**，不要先怀疑插件。

## 方式一：手动安装（推荐，可控）

1. 下载 [TankRun-Control-Center-v2.2.8.zip](../../dist/TankRun-Control-Center-v2.2.8.zip)。
2. 备份你服务器上已存在的同名文件：

   ```
   addons/sourcemod/plugins/tankdirector_menu.smx
   addons/sourcemod/gamedata/tankrun_firstrock.txt
   ```

3. 把压缩包解压到游戏的 **`left4dead2`** 目录（不是 `addons` 目录，也不是服务器根目录）。解压后应得到：

   ```
   left4dead2/addons/sourcemod/plugins/tankdirector_menu.smx      (122,874 字节)
   left4dead2/addons/sourcemod/gamedata/tankrun_firstrock.txt      (532 字节)
   ```

4. 重启服务端。或在不重启的情况下，用服务器控制台加载：

   ```
   sm plugins load tankdirector_menu
   ```

   已经加载过旧版时用 `sm plugins reload tankdirector_menu`。

5. 校验：`sm plugins list` 应显示

   ```
   Tank Run Control Center 2.2.8
   ```

   版本号对不上说明装的是旧文件，或服务器读的是另一个目录。

### 校验文件是否完整

发布包的 SMX SHA256 为：

```
07B63555DCEAF4A135A3BE6D62CFBF650FC0BC752662796A9C4547191319045B
```

Windows PowerShell 校验：

```powershell
Get-FileHash -Algorithm SHA256 `
  'C:\path\to\left4dead2\addons\sourcemod\plugins\tankdirector_menu.smx'
```

哈希不一致时不要继续，重新下载。

## 方式二：SourceMod 插件平台（.smx 上传）

1. 登录服务器面板的插件管理页。
2. 上传 `tankdirector_menu.smx`。
3. **同时**把 `tankrun_firstrock.txt` 放到 `addons/sourcemod/gamedata/`。平台通常只允许上传插件文件，gamedata 需要你用文件管理器单独放。
4. 启动或重载服务器。

只上传 SMX 而忘记 gamedata 的后果：插件能加载，但日志里会出现

```
严格首石拳击入口：签名或扩展不可用，使用输入、动画和命中保护
```

首石功能降级为输入拦截路径，不会报错，很容易被误认为「装好了」。

## 方式三：从源码重新编译

适合要改代码或换编译器版本的情况。

1. 需要 SourceMod 1.12 的 `spcomp.exe` 及**同一套** `include/` 目录。混用不同版本的 include 会产生难以定位的编译错误。
2. 项目源码是 `scripting/tankdirector_menu.sp` 加 `scripting/tankrun/` 下的七个 `.inc`。**必须保持相对目录结构**，因为 `.sp` 用 `#include "tankrun/tr_core.inc"` 这样的路径引用。
3. 编译命令：

   ```powershell
   $sm      = 'D:\path\to\sourcemod\addons\sourcemod\scripting'
   $inc     = Join-Path $sm 'include'
   $spcomp  = Join-Path $sm 'spcomp.exe'

   & $spcomp .\scripting\tankdirector_menu.sp "-i$inc" '-o.\build\tankdirector_menu.smx'
   ```

4. 编译成功只说明编译器接受源码，**不说明游戏内行为正确**。

项目自带的写文件构建脚本是 `tools/build_tankrun_control.ps1`，会额外写构建日志与部署记录（含源码/编译器/产物三个哈希）。详见 [构建](../project/build.md)。

## 首次加载后必做的三件事

1. 看日志文件 `addons/sourcemod/logs/tankrun_control.log` 首行应为：

   ```
   插件已加载 v2.2.8：注册参数 xx 项，日志 ...
   ```

2. 服务器控制台执行 `sm_tank_status`，确认：
   - 版本 2.2.8
   - 模式显示 `tankrun`
   - 依赖行：`Left4DHooks=可用 | SDKHooks=可用`
   - 脚本行不是「不可用」——若显示不可用，见 [故障排查](../operations/troubleshooting.md)

3. 管理员在游戏内聊天输入 `!tank`，确认中文菜单能打开、能逐级进入和返回。

完整验收流程见 [首次运行与验收](quickstart.md)。

## 安装时不要做的事

- **不要**把 ZIP 解压到 `addons/`，会多出一层 `addons/addons/`。
- **不要**同时装独立版 `tankrun_capacity.smx` 和这个 SMX 而不看 [容量说明](../user-guide/capacity-and-seats.md) —— 控制中心已内嵌补位实现，两者同时启用会争夺同一批生成。
- **不要**在服务器运行中直接覆盖 SMX 而不 reload；SourceMod 不会自动重新加载已加载的插件。
- **不要**依赖「文件在目录里」当作「已加载」。以 `sm plugins list` 为准。
