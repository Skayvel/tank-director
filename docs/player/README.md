# 玩家与服主：请从这里开始

> **本页是公开发行版的玩家入口。** 早期版本的玩家说明已并入新版用户指南；本仓库不包含历史存档。

如果你只是想装插件、开 Tank Run、用中文菜单控制 Tank，按顺序看这三篇：

| 步骤 | 文档 |
|---|---|
| 1. 确认依赖 | [依赖与版本要求](../getting-started/requirements.md) |
| 2. 安装 v2.2.11 | [安装指南](../getting-started/install.md) |
| 3. 装完验收 | [首次运行与验收](../getting-started/quickstart.md) |

然后按需要继续：

| 你想做什么 | 看这里 |
|---|---|
| 用菜单和命令 | [v2.2.11 用户指南](../user-guide/control-center.md) |
| 调刷点位置 | [生成方式与选位](../user-guide/spawn-modes.md) |
| 刷更多 Tank / 8 人同服 | [容量与真人席位](../user-guide/capacity-and-seats.md) |
| 出问题了 | [故障排查](../operations/troubleshooting.md) |
| 想系统验证功能 | [验收剧本](../user-guide/validation-playbook.md) |

## 一分钟速查（v2.2.11）

```
下载  dist/TankRun-Control-Center-v2.2.11.zip（或含完整源码的 -full.zip）
解压  到 left4dead2 目录（不是 addons）
得到  addons/sourcemod/plugins/tankdirector_menu.smx
      addons/sourcemod/gamedata/tankrun_firstrock.txt
重启  服务端
确认  sm plugins list  →  Tank Run Control Center 2.2.11
打开  管理员聊天输入 !tank
```

玩家用原版客户端即可，不需要装任何东西。

## 一个必须知道的限制

**「上限 30」不等于「能刷 30 只原版 Tank」。** 每只原版 Tank 占一个客户端槽位。8 名真人 + 31 槽时，原版 Tank 实际最多约 22–23 只。详见 [容量与席位](../user-guide/capacity-and-seats.md)。
