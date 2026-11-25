# 升级、回滚与卸载

## 升级前必做

1. **备份现有 SMX 与 gamedata**（两个字都要）：

   ```
   addons/sourcemod/plugins/tankdirector_menu.smx
   addons/sourcemod/gamedata/tankrun_firstrock.txt
   ```

2. **记录当前参数读数**：控制台 `sm_tank_params`，把输出贴到文本文件。
   理由：`sm_tank_restore` 依赖插件记录的「原始值」文件，跨版本不一定兼容。
3. **导出诊断**：`sm_tank_dump`，留一份 `logs/tankrun_control_dump.txt`。
4. 读 [版本表](../reference/versions.md) 确认目标版本改了什么。

## 升级步骤

1. 停服或准备 reload。
2. 覆盖 `tankdirector_menu.smx`；gamedata 有变化时一并覆盖（v2.2.5 起包内都带）。
3. 重载：

   ```
   sm plugins reload tankdirector_menu
   ```

   冷启动场景直接重启服务端更稳，因为插件在 `OnPluginStart` 里做了 ConVar 注册与依赖探测。
4. 验证版本：`sm plugins list` 显示新版本号。
5. 验证状态：`sm_tank_status` 无「警告：」行。
6. **重新进图一次**。插件在 `OnMapStart` / `OnConfigsExecuted` 复位运行状态并重探脚本；不换图可能留着旧地图的定时器状态。

### 热重载的已知限制

- 热重载前已在场的 Tank **出生顺序不可确认**，插件的满员轮换会暂停，直到这些 Tank 自然离场或开新回合（`docs/tankrun-control-center.md` 第 38 行）。
- 首石窗口状态在重载时丢失，已打开窗口的 Tank 回退原生攻击。
- 已修改的引擎 ConVar **不会**因为重载而恢复。

## 回滚

### 回滚插件本体

```
sm plugins unload tankdirector_menu
```

把备份的 SMX 覆盖回去，再：

```
sm plugins load tankdirector_menu
```

项目内保留了历史构建，可用于对照：

| 版本 | 文件 | SHA256（前 16 位） |
|---|---|---|
| 2.2.11 | `dist/TankRun-Control-Center-v2.2.11.zip`（内置 `tankdirector_menu.smx`） | `ACEC74E98B753553` |
| 2.2.8 | `build/releases/2.2.8/tankdirector_menu.smx` | `07B63555DCEAF4A1` |
| 2.2.7 | `build/releases/2.2.7/tankdirector_menu.smx` | `63954CA566B1423C` |
| 2.2.6 | `build/releases/2.2.6/tankdirector_menu.smx` | `F4CFCE3FED337764` |
| 2.2.5 | `build/releases/2.2.5/tankdirector_menu.smx` | `0A91B6025ADDB3A2` |
| 2.2.4 | `build/releases/2.2.4/tankdirector_menu.smx` | `9619A85D8CB03276` |
| 2.2.3 | `build/releases/2.2.3/tankdirector_menu.smx` | `54FB1BE63CA59088` |
| 2.2.2 | `build/releases/2.2.2/tankdirector_menu.smx` | `271F4201719533AA` |
| 2.2.1 | `build/releases/2.2.1/tankdirector_menu.smx` | `ECD4C85B73C30909` |
| 2.2.0 | `build/releases/2.2.0/tankdirector_menu.smx` | `97107B1A7E9D70D2` |
| 2.1.8 | `build/releases/2.1.8/tankdirector_menu.smx` | `78ABFB4C76D21D46` |
| 2.1.7 | `build/releases/2.1.7/tankdirector_menu.smx` | `90073235C0DFA116` |
| 2.1.6 | `build/releases/2.1.6/tankdirector_menu.smx` | `A667A873B8D35C72` |

完整哈希见 [版本表](../reference/versions.md)。

> 2.1.6 与 2.1.7 的发布包内含源码与 README；2.2.5 起只含 SMX + gamedata。回滚到 2.1.x 时注意包结构不同。

### 恢复被修改的原生 ConVar

插件不会在卸载时自动回滚引擎 ConVar（设计如此，避免抢占用户设置）。

菜单：「11. 插件状态、兼容性与帮助」→「恢复全部参数初始值」。
控制台：

```
sm_tank_restore all
```

原始值存放在 `addons/sourcemod/configs/tankrun_control_originals.cfg`。若该文件丢失或损坏，插件无法恢复，只能手工设置。

### 恢复脚本字段

脚本字段（间隔、数量门、双克）不在 `sm_tank_restore all` 的覆盖范围内，要单独改：

```
sm_tank_set tankrun_script_spawn_interval 20
sm_tank_set tankrun_script_tank_limit 8
```

或者换图，让原版模式脚本重建默认值。

## 完全卸载

1. `sm plugins unload tankdirector_menu`
2. 删除 `addons/sourcemod/plugins/tankdirector_menu.smx`
3. 删除 `addons/sourcemod/gamedata/tankrun_firstrock.txt`
4. 可选：清理插件生成的文件

   ```
   addons/sourcemod/configs/tankrun_control_settings.cfg
   addons/sourcemod/configs/tankrun_control_originals.cfg
   addons/sourcemod/configs/tankrun_control_preset1.cfg … preset8.cfg
   addons/sourcemod/logs/tankrun_control.log
   addons/sourcemod/logs/tankrun_control_dump.txt
   ```

5. **卸载前**如果启用过自动生成，插件会在 `OnPluginEnd` 尝试恢复 Tank Run 脚本的自动生成开关；如果卸载时没有地图，这一步会被跳过，需要你手工确认脚本字段。

## 版本间的行为差异（回滚容易踩的坑）

| 从 | 到 | 注意 |
|---|---|---|
| 2.2.11 | 2.2.8 | 2.2.11 引入分阶段拳种/距离与投石视角控制（`tr_attacks.inc`）、方案 1b 首石（默认 `tankrun_firstrock_timeout=0`、近距极速扔饼、障碍物拍打开关）、次帧槽位回收及目标跳过倒地/滞回；旧版配置不含这些 ConVar |
| 2.2.x | 2.1.x | 2.2 引入统一生成调度与内嵌补位；回到 2.1 后「补位 Tank」概念消失，槽位满即停 |
| 2.2.7 / 2.2.8 | 2.2.5 / 2.2.6 | 2.2.7 移除了插件侧的专家预判改写，交还引擎；`tankrun_rock_expert_lock` 参数已不存在 |
| 2.1.8+ | 2.1.7 及更早 | 2.1.8 起默认每张图设专家难度，`tankrun_default_expert` 在旧版不存在 |
| 任意 | 2.1.6 及更早 | 更早版本没有 `tankrun_spawn_visibility`，遮挡刷点不存在 |

参数是否存在于某一版本，以 [参数参考](../reference/api/convars.md) 的「引入版本」列为准。
