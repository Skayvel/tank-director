# gamedata 与原生签名

控制中心只有一个 gamedata 文件：`gamedata/tankrun_firstrock.txt`。它是**全插件唯一的原生地址依赖**。

## 内容

```json
"Games"
{
    "left4dead2"
    {
        "Signatures"
        {
            "TankRun::CTankClaw::PrimaryAttack"
            {
                "library"  "server"
                "windows"  "\x55\x8B\xEC\x51\xA1\x2A\x2A\x2A\x2A\x56\x33\xF6\x57..."
            }
        }
    }
}
```

| 项 | 值 |
|---|---|
| 签名名 | `TankRun::CTankClaw::PrimaryAttack` |
| 目标库 | `server`（`server.dll`） |
| 平台 | **仅 `windows`** |
| 绑定构建 | `a93ec916a8f26fbea85688c321d2ba8dd346d435fab09a0b926dd950c2e1ea12` |
| 注释记录的 VA | `0x103e3860` |
| 通配字节 | `2A`（`?`），用于跳过重定位/相对偏移 |

签名本身是 40 字节的模式：`55 8B EC 51 A1 ?? ?? ?? ?? 56 33 F6 57 8B F9 39 70 30 0F 85 ?? ?? ?? ?? 53 E8 ?? ?? ?? ?? 8B D8 3B DE 0F 84 ?? ?? ?? ??`。

前 5 个字节 `55 8B EC 51 A1` 是标准的 `push ebp; mov ebp, esp; push ecx; mov eax, [addr]` 序言，配合后面的分支形状构成唯一的匹配。

## 用在哪

`tr_rock.inc:27-48`：

1. `GetFeatureStatus(FeatureType_Native, "DynamicDetour.DynamicDetour")` 检查 DHooks。
2. `new GameData("tankrun_firstrock")` 读取本文件。
3. `data.GetMemSig("TankRun::CTankClaw::PrimaryAttack")` 取地址。
4. `new DynamicDetour(primary, CallConv_THISCALL, ReturnType_Void, ThisPointer_CBaseEntity)`。
5. `Enable(Hook_Pre, TR_DetourFirstRockClaw)`。

任一步失败都不会让插件崩溃，只是不安装 detour，日志写：

```
严格首石拳击入口：签名或扩展不可用，使用输入、动画和命中保护
```

## 平台限制的实际含义

- **Windows 服务器**：签名可用，原生禁拳生效。
- **Linux 服务器**：本文件没有 `linux` 段，`GetMemSig` 返回 `Address_Null`，detour 不安装。插件其余功能（参数、生成、投石门、拳击门、诊断）全部照常工作，但首石只剩输入 + 动画 + 命中三层保护。
- 项目**从未在 Linux 上测试**，因此上一条是「按代码推断」，不是实测结论。

## 游戏更新后会怎样

Valve 更新 `server.dll` 后：

1. 文件 SHA256 变化 → 旧 VA 全部作废（这是项目一直强调的「新哈希 = 新构建」）。
2. 签名字节模式**可能仍然匹配**（因为匹配的是指令形状而不是地址），也可能不匹配。
3. 匹配成功 → detour 装上，但调用约定/结构体布局若变化，行为可能不正确且没有静态证据保证。
4. 匹配失败 → 安静降级。

**因此：游戏更新后，第一步不是看插件有没有报错，而是看日志里那一行「严格首石拳击入口」。**

## 重新生成签名的方法

项目没有保留自动重启签名的工具。历史做法（`analysis/parameter-control/stage3a/claw/module.md`）是：

1. 用新构建的 `server.dll` 重新定位 `CTankClaw::PrimaryAttack`（依赖 RTTI/虚表/调用上下文，不是字符串）。
2. 从函数入口取足够长的字节模式，把绝对地址和相对偏移替换为 `?`。
3. 在 gamedata 里新增平台段或替换模式，并**同时更新文件头注释里的 SHA256 与 VA**。
4. 重新验证：`sm plugins reload tankdirector_menu` 后看日志行。

第 1 步没有自动化脚本，需要重新做静态分析。

## 相关源码

| 用途 | 位置 |
|---|---|
| 读取与安装 detour | `tr_rock.inc:27-48` |
| detour 回调 | `tr_rock.inc` 中 `TR_DetourFirstRockClaw` |
| 卸载 | `tankdirector_menu.sp:155-160`（`OnPluginEnd` 中 `Disable` + `delete`） |
