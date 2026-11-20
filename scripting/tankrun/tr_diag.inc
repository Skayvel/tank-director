// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

// Tank Run 综合控制中心 - 控制台命令与诊断（M11）

#if defined _tankrun_diag_included
 #endinput
#endif
#define _tankrun_diag_included

bool TR_CommandAccess(int client, int flags)
{
    if (client == 0)
    {
        return true;
    }
    if (client < 1 || client > MaxClients || !IsClientInGame(client))
    {
        return false;
    }
    if (!CheckCommandAccess(client, "sm_tank", flags))
    {
        TR_Reply(client, "权限不足：需要相应管理员权限");
        return false;
    }
    return true;
}

public Action Command_TankMenu(int client, int args)
{
    if (client == 0)
    {
        TR_Reply(client, "控制台无法显示菜单；请使用 sm_tank_status / sm_tank_set 等命令。");
        return Plugin_Handled;
    }
    if (!TR_CanOpen(client))
    {
        TR_Reply(client, "权限不足：需要配置管理员权限，或由服务器开启 tankrun_public_readonly。");
        return Plugin_Handled;
    }
    ShowMainMenu(client);
    return Plugin_Handled;
}

public Action Command_Status(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_GENERIC))
    {
        return Plugin_Handled;
    }
    TR_PrintStatus(client);
    return Plugin_Handled;
}

public Action Command_Diag(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_GENERIC))
    {
        return Plugin_Handled;
    }
    TR_ProbeDependencies();
    TR_PrintStatus(client);
    char path[PLATFORM_MAX_PATH];
    TR_WriteDiagnosticDump(path, sizeof(path));
    if (path[0] != '\0')
    {
        TR_Reply(client, "诊断文件：%s", path);
    }
    return Plugin_Handled;
}

public Action Command_Dump(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_GENERIC))
    {
        return Plugin_Handled;
    }
    char path[PLATFORM_MAX_PATH];
    TR_WriteDiagnosticDump(path, sizeof(path));
    if (path[0] == '\0')
    {
        TR_Reply(client, "诊断导出失败，请查看 sourcemod 日志");
        return Plugin_Handled;
    }
    TR_Reply(client, "诊断文件：%s", path);
    return Plugin_Handled;
}

public Action Command_Spawn(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_CONFIG))
    {
        return Plugin_Handled;
    }
    TR_SpawnMode mode = view_as<TR_SpawnMode>(g_cvSpawnMode.IntValue);
    if (args >= 1)
    {
        char arg[32];
        GetCmdArg(1, arg, sizeof(arg));
        if (StrEqual(arg, "native", false))
        {
            mode = TRSP_NATIVE;
        }
        else if (StrEqual(arg, "band", false))
        {
            mode = TRSP_BAND;
        }
        else if (StrEqual(arg, "aim", false))
        {
            mode = TRSP_AIM;
        }
        else if (StrEqual(arg, "script", false) || StrEqual(arg, "vscript", false))
        {
            mode = TRSP_VSCRIPT;
        }
        else if (StrEqual(arg, "near", false))
        {
            mode = TRSP_NEAR;
        }
        else
        {
            TR_Reply(client, "未知生成方式：%s（可用 native/band/aim/script/near）", arg);
            return Plugin_Handled;
        }
    }
    char message[192];
    int tank = TR_SpawnNow(client, mode, message, sizeof(message));
    TR_Reply(client, "%s", message);
    TR_Log("命令生成：发起者=%d 方式=%d 结果=%d", client, mode, tank);
    return Plugin_Handled;
}

public Action Command_Clear(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_CONFIG))
    {
        return Plugin_Handled;
    }
    int killed = TR_KillAllTanks("命令清理");
    TR_Reply(client, "已清理 %d 只 AI Tank", killed);
    return Plugin_Handled;
}

public Action Command_KillTank(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_CONFIG)) return Plugin_Handled;
    char target[32];
    if (args != 1 || !GetCmdArg(1, target, sizeof(target)))
    {
        TR_Reply(client, "用法：sm_tank_kill <Tank客户端编号|oldest|newest>");
        return Plugin_Handled;
    }
    int tank;
    if (StrEqual(target, "oldest", false) || StrEqual(target, "newest", false))
        tank = TR_FindBirthOrderedTank(StrEqual(target, "newest", false));
    else if (StringToIntEx(target, tank) != strlen(target))
        tank = 0;
    if (!TR_IsAiTank(tank))
    {
        TR_Reply(client, "没有对应的存活 AI Tank，或出生顺序不可确认。可从 Tank 列表选择具体编号。");
        return Plugin_Handled;
    }
    bool killed = TR_KillTank(tank, "管理员命令处死", true);
    TR_Reply(client, "Tank #%d：%s", tank, killed ? "已处死" : "未确认退场");
    return Plugin_Handled;
}

public Action Command_Set(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_CONFIG))
    {
        return Plugin_Handled;
    }
    if (args < 2)
    {
        TR_Reply(client, "用法：sm_tank_set <参数名> <数值>（参数名清单见 sm_tank_params）");
        return Plugin_Handled;
    }
    char name[64];
    char valueText[64];
    GetCmdArg(1, name, sizeof(name));
    GetCmdArg(2, valueText, sizeof(valueText));

    int idx = TR_FindParamByName(name);
    if (idx < 0)
    {
        TR_Reply(client, "未知参数：%s（使用 sm_tank_params 查看已注册参数）", name);
        return Plugin_Handled;
    }
    if (!TR_IsNumeric(valueText))
    {
        TR_Reply(client, "数值无效：%s", valueText);
        return Plugin_Handled;
    }
    float value = StringToFloat(valueText);
    float clamped = value;
    if (!TR_ClampValue(idx, clamped))
    {
        TR_Reply(client, "数值无效：生成间隔必须大于 0，数值必须有限");
        return Plugin_Handled;
    }
    if (FloatAbs(clamped - value) > 0.0001)
    {
        TR_Reply(client, "数值按范围校正：%.2f → %.2f", value, clamped);
    }
    bool ok = TR_ParamWrite(idx, clamped, false);
    char now[32];
    TR_FormatValue(idx, now, sizeof(now));
    if (ok)
    {
        TR_Reply(client, "%s 已写入，实际读回 %s", g_sParamName[idx], now);
    }
    else
    {
        TR_Reply(client, "%s 写入失败，实际 %s（检查模式覆盖/脚本状态/冲突模式）", g_sParamName[idx], now);
    }
    return Plugin_Handled;
}

public Action Command_Restore(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_CONFIG))
    {
        return Plugin_Handled;
    }
    if (args < 1)
    {
        TR_Reply(client, "用法：sm_tank_restore <参数名|all>");
        return Plugin_Handled;
    }
    char name[64];
    GetCmdArg(1, name, sizeof(name));
    if (StrEqual(name, "all", false))
    {
        TR_RestoreAll();
        TR_Reply(client, "已恢复全部已记录初始值的引擎参数");
        return Plugin_Handled;
    }
    int idx = TR_FindParamByName(name);
    if (idx < 0)
    {
        TR_Reply(client, "未知参数：%s", name);
        return Plugin_Handled;
    }
    if (TR_ParamRestore(idx))
    {
        char now[32];
        TR_FormatValue(idx, now, sizeof(now));
        TR_Reply(client, "%s 已恢复，实际 %s", g_sParamName[idx], now);
    }
    else
    {
        TR_Reply(client, "%s 无法恢复（脚本字段/参数不可用/插件关闭）", g_sParamName[idx]);
    }
    return Plugin_Handled;
}

public Action Command_Params(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_GENERIC))
    {
        return Plugin_Handled;
    }
    for (int i = 0; i < g_iParamCount; i++)
    {
        float value;
        char text[32];
        if (TR_ParamRead(i, value))
        {
            Format(text, sizeof(text), "%.4f", value);
        }
        else
        {
            strcopy(text, sizeof(text), "unavailable");
        }
        ReplyToCommand(client, "%s = %s（%s，分区 %d）", g_sParamName[i], text, g_sParamLabel[i],
            g_iParamSection[i]);
    }
    return Plugin_Handled;
}

public Action Command_Preset(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_CONFIG))
    {
        return Plugin_Handled;
    }
    if (args < 1)
    {
        TR_Reply(client, "用法：sm_tank_preset <save|load|delete|list> [槽位1-8] [名称]");
        return Plugin_Handled;
    }
    char action[32];
    GetCmdArg(1, action, sizeof(action));

    if (StrEqual(action, "list", false))
    {
        for (int i = 0; i < TR_PRESET_SLOTS; i++)
        {
            char text[128];
            TR_FormatPresetSlot(i, text, sizeof(text));
            TR_Reply(client, "%s", text);
        }
        return Plugin_Handled;
    }

    if (args < 2)
    {
        TR_Reply(client, "缺少槽位（1-%d）", TR_PRESET_SLOTS);
        return Plugin_Handled;
    }
    char slotText[16];
    GetCmdArg(2, slotText, sizeof(slotText));
    int slot = StringToInt(slotText) - 1;
    if (slot < 0 || slot >= TR_PRESET_SLOTS)
    {
        TR_Reply(client, "槽位超出范围（1-%d）", TR_PRESET_SLOTS);
        return Plugin_Handled;
    }

    if (StrEqual(action, "save", false))
    {
        char name[64];
        if (args >= 3)
        {
            GetCmdArg(3, name, sizeof(name));
        }
        else
        {
            Format(name, sizeof(name), "预设%d", slot + 1);
        }
        bool ok = TR_SavePreset(slot, name);
        TR_Reply(client, ok ? "已保存到槽位 %d：%s" : "保存失败（槽位 %d）", slot + 1, name);
        return Plugin_Handled;
    }
    if (StrEqual(action, "load", false))
    {
        int applied = TR_ApplyPreset(slot, client);
        if (applied < 0)
        {
            TR_Reply(client, "槽位 %d 为空或读取失败", slot + 1);
        }
        else
        {
            TR_Reply(client, "槽位 %d 应用完成，成功 %d 项", slot + 1, applied);
        }
        return Plugin_Handled;
    }
    if (StrEqual(action, "delete", false))
    {
        bool ok = TR_DeletePreset(slot);
        TR_Reply(client, ok ? "槽位 %d 已删除" : "槽位 %d 为空", slot + 1);
        return Plugin_Handled;
    }
    TR_Reply(client, "未知操作：%s", action);
    return Plugin_Handled;
}

public Action Command_Debug(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_CONFIG))
    {
        return Plugin_Handled;
    }
    if (args >= 1)
    {
        char arg[16];
        GetCmdArg(1, arg, sizeof(arg));
        g_cvDebug.BoolValue = StringToInt(arg) != 0;
    }
    TR_Reply(client, "调试输出：%s", g_cvDebug.BoolValue ? "开" : "关");
    return Plugin_Handled;
}

public Action Command_Log(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_CONFIG))
    {
        return Plugin_Handled;
    }
    if (args >= 1)
    {
        char arg[16];
        GetCmdArg(1, arg, sizeof(arg));
        g_cvLogEvents.BoolValue = StringToInt(arg) != 0;
    }
    TR_Reply(client, "事件日志：%s（文件 %s）", g_cvLogEvents.BoolValue ? "开" : "关", g_sLogPath);
    return Plugin_Handled;
}

public Action Command_Reload(int client, int args)
{
    if (!TR_CommandAccess(client, ADMFLAG_CONFIG))
    {
        return Plugin_Handled;
    }
    TR_LoadSettings();
    TR_LoadOriginals();
    TR_ProbeDependencies();
    TR_EnsureAutoTimer();
    TR_Reply(client, "已重新加载设置/初始值并重新探测依赖：%s", g_sVStatus);
    return Plugin_Handled;
}

public Action Command_Help(int client, int args)
{
    TR_Reply(client, "命令：!tank 菜单 | sm_tank_status | sm_tank_diag | sm_tank_dump | sm_tank_params");
    TR_Reply(client, "写入：sm_tank_set <参数> <值> | sm_tank_restore <参数|all> | sm_tank_spawn [方式] | sm_tank_clear");
    TR_Reply(client, "处死：sm_tank_kill <Tank客户端编号|oldest|newest>");
    TR_Reply(client, "玩家自杀：!zs 或 /zs");
    TR_Reply(client, "预设：sm_tank_preset <save|load|delete|list> <槽位> [名称] | sm_tank_debug 0/1 | sm_tank_log 0/1 | sm_tank_reload");
    TR_Reply(client, "说明：本插件不伪造投石/动画；无法通过公开接口实现的功能会显示为不可用。");
    return Plugin_Handled;
}

public Action Command_Suicide(int client, int args)
{
    if (client < 1 || client > MaxClients || !IsClientInGame(client) || IsFakeClient(client)) return Plugin_Handled;
    if (args == 0 && TR_FeatureEnabled() && IsPlayerAlive(client)) ForcePlayerSuicide(client);
    return Plugin_Handled;
}

void TR_RegisterCommands()
{
    // 普通玩家是否可用由 TR_CanOpen 在回调内检查（只读模式）。
    RegConsoleCmd("sm_tank", Command_TankMenu, "打开 Tank Run 综合控制中心菜单");
    RegConsoleCmd("sm_tankmenu", Command_TankMenu, "打开 Tank Run 综合控制中心菜单（别名）");
    RegConsoleCmd("sm_tank_help", Command_Help, "Tank Run 控制帮助");
    RegConsoleCmd("sm_zs", Command_Suicide, "自杀（只作用于命令发起者）");

    RegAdminCmd("sm_tank_status", Command_Status, ADMFLAG_GENERIC, "Tank Run 控制状态");
    RegAdminCmd("sm_tank_diag", Command_Diag, ADMFLAG_GENERIC, "Tank Run 诊断摘要并写日志");
    RegAdminCmd("sm_tank_dump", Command_Dump, ADMFLAG_GENERIC, "导出 Tank Run 诊断文件");
    RegAdminCmd("sm_tank_params", Command_Params, ADMFLAG_GENERIC, "列出全部已注册参数和当前值");

    RegAdminCmd("sm_tank_spawn", Command_Spawn, ADMFLAG_CONFIG, "手动生成 AI Tank");
    RegAdminCmd("sm_tank_clear", Command_Clear, ADMFLAG_CONFIG, "清理全部 AI Tank");
    RegAdminCmd("sm_tank_kill", Command_KillTank, ADMFLAG_CONFIG, "处死指定、最早或最后出生的 AI Tank");
    RegAdminCmd("sm_tank_set", Command_Set, ADMFLAG_CONFIG, "写入参数");
    RegAdminCmd("sm_tank_restore", Command_Restore, ADMFLAG_CONFIG, "恢复参数初始值");
    RegAdminCmd("sm_tank_preset", Command_Preset, ADMFLAG_CONFIG, "预设管理");
    RegAdminCmd("sm_tank_debug", Command_Debug, ADMFLAG_CONFIG, "调试输出开关");
    RegAdminCmd("sm_tank_log", Command_Log, ADMFLAG_CONFIG, "事件日志开关");
    RegAdminCmd("sm_tank_reload", Command_Reload, ADMFLAG_CONFIG, "重新加载设置并探测依赖");
}
