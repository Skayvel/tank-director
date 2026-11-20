// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

// Tank Run 综合控制中心 - 中文游戏内菜单（M11 交互层）
// 多级菜单、返回上级、实时数值、聊天输入自定义值、只读权限模式。

#if defined _tankrun_menu_included
 #endinput
#endif
#define _tankrun_menu_included

// 待输入状态
#define TR_PEND_NONE      0
#define TR_PEND_PARAM     1
#define TR_PEND_PRESET    2
#define TR_PEND_TANKHP    3
#define TR_PEND_RENAME    4

int g_iMenuView[MAXPLAYERS + 1];
TR_Section g_iMenuSection[MAXPLAYERS + 1];
int g_iMenuParam[MAXPLAYERS + 1];
int g_iMenuTankUserid[MAXPLAYERS + 1];
int g_iMenuSlot[MAXPLAYERS + 1];

int g_iPendKind[MAXPLAYERS + 1];
int g_iPendParam[MAXPLAYERS + 1];
int g_iPendSlot[MAXPLAYERS + 1];
int g_iPendTank[MAXPLAYERS + 1];


Menu TR_NewMenu(MenuHandler handler, const char[] title, bool back = true)
{
    Menu menu = new Menu(handler);
    menu.SetTitle("%s", title);
    menu.Pagination = 7;
    menu.ExitBackButton = back;
    return menu;
}

int TR_EditDraw(int client)
{
    return TR_CanEdit(client) ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED;
}

void TR_GetSectionName(TR_Section section, char[] buffer, int maxlen)
{
    switch (section)
    {
        case SEC_SPAWN:    strcopy(buffer, maxlen, "Tank 生成与数量管理");
        case SEC_HEALTH:   strcopy(buffer, maxlen, "Tank 血量、速度与属性");
        case SEC_CLAW:     strcopy(buffer, maxlen, "Tank 普通拳击控制");
        case SEC_ROCK:     strcopy(buffer, maxlen, "Tank 投石控制");
        case SEC_TARGET:   strcopy(buffer, maxlen, "Tank 目标与行为策略");
        case SEC_POS:      strcopy(buffer, maxlen, "Tank 生成位置与距离");
        case SEC_RULES:    strcopy(buffer, maxlen, "AI Director 与游戏规则");
        case SEC_INSTANCE: strcopy(buffer, maxlen, "当前 Tank 列表与独立控制");
        case SEC_PRESET:   strcopy(buffer, maxlen, "参数预设与配置保存");
        case SEC_MONITOR:  strcopy(buffer, maxlen, "实时监控与调试");
        case SEC_STATUS:   strcopy(buffer, maxlen, "插件状态、兼容性与帮助");
        default:           strcopy(buffer, maxlen, "未知分区");
    }
}

void TR_FormatRange(int idx, char[] buffer, int maxlen)
{
    if (g_bParamInt[idx])
    {
        Format(buffer, maxlen, "%d ~ %d", RoundToNearest(g_fParamMin[idx]), RoundToNearest(g_fParamMax[idx]));
    }
    else
    {
        Format(buffer, maxlen, "%.2f ~ %.2f", g_fParamMin[idx], g_fParamMax[idx]);
    }
}

// ---------------------------------------------------------------- 主菜单
void ShowMainMenu(int client)
{
    g_iMenuView[client] = SEC_STATUS;
    char mode[64];
    char map[128];
    TR_GetMode(mode, sizeof(mode));
    GetCurrentMap(map, sizeof(map));

    char title[192];
    Format(title, sizeof(title), "Tank Run 综合控制中心 v%s\n模式：%s　地图：%s\n功能开关：%s",
        TR_VERSION, mode, map, TR_FeatureEnabled() ? "开启" : "关闭");

    Menu menu = TR_NewMenu(Handler_Main, title, false);
    menu.AddItem("sec:0", "1. Tank 生成与数量管理");
    menu.AddItem("sec:1", "2. Tank 血量、速度与属性");
    menu.AddItem("sec:2", "3. Tank 普通拳击控制");
    menu.AddItem("sec:3", "4. Tank 投石控制");
    menu.AddItem("sec:4", "5. Tank 目标与行为策略");
    menu.AddItem("sec:5", "6. Tank 生成位置与距离");
    menu.AddItem("sec:6", "7. AI Director 与游戏规则");
    menu.AddItem("sec:7", "8. 当前 Tank 列表与独立控制");
    menu.AddItem("sec:8", "9. 参数预设与配置保存");
    menu.AddItem("sec:9", "10. 实时监控与调试");
    menu.AddItem("sec:10", "11. 插件状态、兼容性与帮助");
    menu.AddItem("off", "关闭菜单");
    menu.Display(client, MENU_TIME_FOREVER);
}

public int Handler_Main(Menu menu, MenuAction action, int client, int item)
{
    if (action == MenuAction_End)
    {
        delete menu;
        return 0;
    }
    if (!TR_CanOpen(client))
    {
        return 0;
    }
    if (action != MenuAction_Select)
    {
        return 0;
    }
    char info[64];
    menu.GetItem(item, info, sizeof(info));
    if (StrContains(info, "sec:") == 0)
    {
        TR_Section section = view_as<TR_Section>(StringToInt(info[4]));
        switch (section)
        {
            case SEC_INSTANCE: ShowTankListMenu(client);
            case SEC_PRESET:   ShowPresetMenu(client);
            case SEC_MONITOR:  ShowMonitorMenu(client);
            case SEC_STATUS:   ShowStatusMenu(client);
            default:           ShowSectionMenu(client, section);
        }
    }
    return 0;
}

// ---------------------------------------------------------------- 分区菜单
void ShowSectionMenu(int client, TR_Section section)
{
    g_iMenuView[client] = section;
    g_iMenuSection[client] = section;

    char title[192];
    char sectionName[48];
    TR_GetSectionName(section, sectionName, sizeof(sectionName));
    if (section == SEC_RULES && TR_IsTankRunMode())
    {
        Format(title, sizeof(title), "%s\n统一生成阶段：%s\n生成间隔和上限可在 Tank 生成中设置",
            sectionName, g_bFrontPhaseOpen ? "开放" : "等待");
    }
    else if (section == SEC_RULES || section == SEC_SPAWN || section == SEC_POS)
    {
        Format(title, sizeof(title), "%s\n规则/生成参数可能需等下一次生成或换图生效", sectionName);
    }
    else
    {
        Format(title, sizeof(title), "%s\n当前值来自引擎/插件实时读取", sectionName);
    }

    Menu menu = TR_NewMenu(Handler_Section, title, true);
    char key[24];
    char text[160];
    int draw = TR_EditDraw(client);

    for (int i = 0; i < g_iParamCount; i++)
    {
        if (g_iParamSection[i] != section)
        {
            continue;
        }
        if (TR_IsTankRunMode() && (StrEqual(g_sParamName[i], "tankrun_spawn_mode")
            || g_iParamVField[i] == TRV_INTERVAL || g_iParamVField[i] == TRV_DOUBLE || g_iParamVField[i] == TRV_LIMIT)) continue;
        TR_ParamLabelValue(i, text, sizeof(text));
        int itemDraw = draw;
        if (g_iParamKind[i] == TRK_ENGINE && g_ParamCVar[i] == null)
        {
            itemDraw = ITEMDRAW_DISABLED;
        }
        if (g_iParamKind[i] == TRK_VSCRIPT && !g_bVValid)
        {
            itemDraw = ITEMDRAW_DISABLED;
        }
        Format(key, sizeof(key), "p:%d", i);
        if (!g_bParamVerified[i] && itemDraw != ITEMDRAW_DISABLED)
        {
            char marked[160];
            Format(marked, sizeof(marked), "%s（效果未验证）", text);
            strcopy(text, sizeof(text), marked);
        }
        menu.AddItem(key, text, itemDraw);
    }

    // 分区动作
    switch (section)
    {
        case SEC_SPAWN:
        {
            if (TR_IsTankRunMode())
            {
                menu.AddItem("a:spawn_mode", "立即生成（领先玩家前方）", draw);
                menu.AddItem("a:auto_toggle", g_cvAutoSpawn.BoolValue ? "关闭统一自动生成" : "开启统一自动生成", draw);
            }
            else
            {
                menu.AddItem("a:spawn_native", "立即生成（原生随机区域）", draw);
                menu.AddItem("a:spawn_band", "立即生成（距离带）", draw);
                menu.AddItem("a:spawn_aim", "立即生成（我的准星位置）", draw);
            }
            menu.AddItem("a:clear_tanks", "清理全部 AI Tank（含补位）", draw);
            menu.AddItem("a:refresh", "刷新实际值");
        }
        case SEC_HEALTH:
        {
            menu.AddItem("a:heal_all", "治疗全部 AI Tank 至满血", draw);
            menu.AddItem("a:apply_health", "把当前血量设置应用到现有 Tank", draw);
            menu.AddItem("a:apply_speed", "把当前速度倍率应用到现有 Tank", draw);
            menu.AddItem("a:refresh", "刷新实际值");
        }
        case SEC_CLAW:
        {
            menu.AddItem("a:claw_stats", "查看拳击统计与最近事件", ITEMDRAW_DEFAULT);
            menu.AddItem("a:refresh", "刷新实际值");
        }
        case SEC_ROCK:
        {
            menu.AddItem("a:rock_stats", "查看投石统计与最近事件", ITEMDRAW_DEFAULT);
            menu.AddItem("a:vscript_refresh", "刷新 Tank Run 脚本实际值", ITEMDRAW_DEFAULT);
            menu.AddItem("x:force_throw", "强制投石：无可靠公开接口（需 GPT 深度研究）", ITEMDRAW_DISABLED);
            menu.AddItem("a:refresh", "刷新实际值");
        }
        case SEC_TARGET:
        {
            menu.AddItem("a:contact_list", "查看每个 Tank 的接敌与策略状态", ITEMDRAW_DEFAULT);
            menu.AddItem("a:refresh", "刷新实际值");
        }
        case SEC_RULES:
        {
            menu.AddItem("a:vscript_refresh", "刷新 Tank Run 脚本实际值", ITEMDRAW_DEFAULT);
            menu.AddItem("a:refresh", "刷新实际值");
        }
        case SEC_STATUS:
        {
            menu.AddItem("a:help", "查看帮助与命令", ITEMDRAW_DEFAULT);
            menu.AddItem("a:probe", "重新探测依赖与脚本", ITEMDRAW_DEFAULT);
            menu.AddItem("a:restore_all", "恢复全部参数初始值（含插件参数）", draw);
            menu.AddItem("a:dump", "导出诊断文件", ITEMDRAW_DEFAULT);
        }
    }
    menu.AddItem("a:main", "返回主菜单");
    menu.Display(client, MENU_TIME_FOREVER);
}

public int Handler_Section(Menu menu, MenuAction action, int client, int item)
{
    if (action == MenuAction_End)
    {
        delete menu;
        return 0;
    }
    if (!TR_CanOpen(client))
    {
        return 0;
    }
    if (action == MenuAction_Cancel)
    {
        if (item == MenuCancel_ExitBack)
        {
            ShowMainMenu(client);
        }
        return 0;
    }
    if (action != MenuAction_Select)
    {
        return 0;
    }

    char info[64];
    menu.GetItem(item, info, sizeof(info));
    TR_Section section = g_iMenuSection[client];

    if (StrContains(info, "p:") == 0)
    {
        int idx = StringToInt(info[2]);
        if (idx >= 0 && idx < g_iParamCount)
        {
            g_iMenuParam[client] = idx;
            ShowParamMenu(client, idx);
        }
        return 0;
    }
    if (StrEqual(info, "a:main"))
    {
        ShowMainMenu(client);
        return 0;
    }
    TR_HandleSectionAction(client, section, info);
    return 0;
}

void TR_HandleSectionAction(int client, TR_Section section, const char[] info)
{
    char message[192];
    if (StrEqual(info, "a:refresh"))
    {
        if (section == SEC_RULES)
        {
            TR_VScriptProbe(true);
        }
        ShowSectionMenu(client, section);
        return;
    }
    if (StrEqual(info, "a:vscript_refresh"))
    {
        bool ok = TR_VScriptProbe(true);
        if (ok)
        {
            TR_Chat(client, "脚本实际值：SpawnInterval=%.1f 数量门=%d 双克=%s 脚本思考=%s 脚本血量=%d",
                g_fVSpawnInterval, g_iVTankLimit, g_bVDoubleTanks ? "开" : "关",
                g_bVThink ? "开" : "关", g_iVTankHealth);
        }
        else
        {
            TR_Chat(client, "脚本刷新失败：%s", g_sVStatus);
        }
        ShowSectionMenu(client, section);
        return;
    }
    if (StrEqual(info, "a:spawn_mode"))
    {
        TR_ActionSpawn(client, view_as<TR_SpawnMode>(g_cvSpawnMode.IntValue));
        return;
    }
    if (StrEqual(info, "a:spawn_native"))
    {
        TR_ActionSpawn(client, TRSP_NATIVE);
        return;
    }
    if (StrEqual(info, "a:spawn_band"))
    {
        TR_ActionSpawn(client, TRSP_BAND);
        return;
    }
    if (StrEqual(info, "a:spawn_aim"))
    {
        TR_ActionSpawn(client, TRSP_AIM);
        return;
    }
    if (StrEqual(info, "a:auto_toggle"))
    {
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足：只读模式不能修改");
            return;
        }
        g_cvAutoSpawn.BoolValue = !g_cvAutoSpawn.BoolValue;
        TR_Chat(client, "自动生成已%s", g_cvAutoSpawn.BoolValue ? "开启" : "关闭");
        ShowSectionMenu(client, section);
        return;
    }
    if (StrEqual(info, "a:clear_tanks"))
    {
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足：只读模式不能执行清理");
            return;
        }
        int killed = TR_KillAllTanks("菜单清理");
        TR_Chat(client, "已清理 %d 只 AI Tank", killed);
        ShowSectionMenu(client, section);
        return;
    }
    if (StrEqual(info, "a:heal_all"))
    {
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足：只读模式不能执行治疗");
            return;
        }
        TR_HealAllTanks();
        TR_Chat(client, "已治疗全部 AI Tank");
        ShowSectionMenu(client, section);
        return;
    }
    if (StrEqual(info, "a:apply_health"))
    {
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足");
            return;
        }
        TR_ApplyHealthToAll("菜单应用");
        TR_Chat(client, "已把当前血量设置应用到现有 AI Tank");
        ShowSectionMenu(client, section);
        return;
    }
    if (StrEqual(info, "a:apply_speed"))
    {
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足");
            return;
        }
        TR_ApplySpeedToAll();
        TR_Chat(client, "已把速度倍率 %.2f 应用到现有 AI Tank", g_cvSpeedScale.FloatValue);
        ShowSectionMenu(client, section);
        return;
    }
    if (StrEqual(info, "a:claw_stats") || StrEqual(info, "a:rock_stats") || StrEqual(info, "a:contact_list"))
    {
        char summary[512];
        TR_FormatRockSummary(summary, sizeof(summary));
        TR_Chat(client, "%s", summary);
        ShowTankListMenu(client);
        return;
    }
    if (StrEqual(info, "a:restore_all"))
    {
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足");
            return;
        }
        TR_RestoreAll();
        TR_Chat(client, "已恢复全部引擎参数初始值");
        ShowSectionMenu(client, section);
        return;
    }
    if (StrEqual(info, "a:help"))
    {
        ShowHelpMenu(client);
        return;
    }
    if (StrEqual(info, "a:probe"))
    {
        if (!TR_IsAdmin(client))
        {
            TR_Chat(client, "权限不足：依赖探测需要管理员权限");
            return;
        }
        TR_ProbeDependencies();
        TR_Chat(client, "依赖探测完成：Left4DHooks=%s 脚本=%s", g_bLeft4D ? "可用" : "不可用", g_sVStatus);
        ShowStatusMenu(client);
        return;
    }
    if (StrEqual(info, "a:dump"))
    {
        if (!TR_IsAdmin(client))
        {
            TR_Chat(client, "权限不足：导出诊断需要管理员权限");
            return;
        }
        char path[PLATFORM_MAX_PATH];
        TR_WriteDiagnosticDump(path, sizeof(path));
        TR_Chat(client, "诊断文件已写入：%s", path);
        ShowSectionMenu(client, section);
        return;
    }
    Format(message, sizeof(message), "未知操作：%s", info);
    TR_Chat(client, "%s", message);
}

void TR_ActionSpawn(int client, TR_SpawnMode mode)
{
    if (!TR_CanEdit(client))
    {
        TR_Chat(client, "权限不足：只读模式不能生成 Tank");
        return;
    }
    char message[192];
    int tank = TR_SpawnNow(client, mode, message, sizeof(message));
    TR_Chat(client, "%s", message);
    TR_Log("管理员#%d 手动生成（方式 %d）：%s 结果=%d", client, mode, message, tank);
    ShowSectionMenu(client, g_iMenuSection[client]);
}

// ---------------------------------------------------------------- 参数编辑
void ShowParamMenu(int client, int idx)
{
    if (idx < 0 || idx >= g_iParamCount)
    {
        ShowMainMenu(client);
        return;
    }
    g_iMenuView[client] = 100 + idx;
    g_iMenuParam[client] = idx;

    char current[32];
    char def[32];
    char range[48];
    char title[384];
    TR_FormatValue(idx, current, sizeof(current));
    if (g_iParamKind[idx] == TRK_VSCRIPT)
    {
        strcopy(def, sizeof(def), "脚本字段");
    }
    else
    {
        TR_FormatNumber(TR_ParamDefault(idx), def, sizeof(def));
    }
    TR_FormatRange(idx, range, sizeof(range));
    Format(title, sizeof(title), "%s\n当前：%s　注册默认：%s\n旧菜单范围（参考）：%s\n%s",
        g_sParamLabel[idx], current, def, range, g_sParamName[idx]);

    Menu menu = TR_NewMenu(Handler_Param, title, true);
    char key[32];
    char text[160];
    int draw = TR_EditDraw(client);
    if (TR_IsPositiveSpawnInterval(idx))
    {
        menu.AddItem("x:interval", "实验间隔可填任意正数，实际频率取决于引擎调度", ITEMDRAW_DISABLED);
    }
    if (g_sParamNote[idx][0] != '\0') menu.AddItem("x:description", g_sParamNote[idx], ITEMDRAW_DISABLED);

    if (!g_bParamVerified[idx])
    {
        menu.AddItem("x:note1", "该参数可修改，但原生实际消费未验证", ITEMDRAW_DISABLED);
    }
    if (g_iParamKind[idx] == TRK_VSCRIPT && !g_bVValid)
    {
        menu.AddItem("x:note2", "脚本接口当前不可用：需要 tankrun 模式并加载脚本", ITEMDRAW_DISABLED);
    }
    if (g_iParamKind[idx] == TRK_ENGINE && g_ParamCVar[idx] == null)
    {
        menu.AddItem("x:note3", "本构建未注册该 ConVar，无法修改", ITEMDRAW_DISABLED);
    }

    if (TR_IsBoolParam(idx))
    {
        float value = 0.0;
        if (TR_ParamRead(idx, value))
        {
            Format(text, sizeof(text), "切换为：%s", value != 0.0 ? "关" : "开");
        }
        else
        {
            strcopy(text, sizeof(text), "切换（当前值不可读）");
        }
        menu.AddItem("toggle", text, draw);
    }
    else
    {
        Format(text, sizeof(text), "减少 %.2f", g_fParamStep[idx]);
        menu.AddItem("minus", text, draw);
        Format(text, sizeof(text), "增加 %.2f", g_fParamStep[idx]);
        menu.AddItem("plus", text, draw);
        Format(text, sizeof(text), "减少 10 倍步进（%.2f）", g_fParamStep[idx] * 10.0);
        menu.AddItem("minus10", text, draw);
        Format(text, sizeof(text), "增加 10 倍步进（%.2f）", g_fParamStep[idx] * 10.0);
        menu.AddItem("plus10", text, draw);
        for (int i = 0; i < g_iParamQuickCount[idx]; i++)
        {
            Format(key, sizeof(key), "q:%d", i);
            Format(text, sizeof(text), "常用值：%.2f", g_fParamQuick[idx][i]);
            menu.AddItem(key, text, draw);
        }
        menu.AddItem("input", "自定义数值（聊天输入）", draw);
    }

    menu.AddItem("default", "设为引擎注册默认值", draw);
    if (g_bParamHasOriginal[idx])
    {
        char original[32];
        TR_FormatNumber(g_fParamOriginal[idx], original, sizeof(original));
        Format(text, sizeof(text), "恢复本插件记录的初始值（%s）", original);
        menu.AddItem("original", text, draw);
    }
    menu.AddItem("back", "返回上一级");
    menu.Display(client, MENU_TIME_FOREVER);
}

public int Handler_Param(Menu menu, MenuAction action, int client, int item)
{
    if (action == MenuAction_End)
    {
        delete menu;
        return 0;
    }
    if (!TR_CanOpen(client))
    {
        return 0;
    }
    int idx = g_iMenuParam[client];
    if (action == MenuAction_Cancel)
    {
        if (item == MenuCancel_ExitBack)
        {
            ShowSectionMenu(client, g_iParamSection[idx]);
        }
        return 0;
    }
    if (action != MenuAction_Select)
    {
        return 0;
    }

    char info[64];
    menu.GetItem(item, info, sizeof(info));

    if (StrEqual(info, "back"))
    {
        ShowSectionMenu(client, g_iParamSection[idx]);
        return 0;
    }
    if (StrContains(info, "x:") == 0)
    {
        ShowParamMenu(client, idx);
        return 0;
    }
    if (!TR_CanEdit(client))
    {
        TR_Chat(client, "权限不足：只读模式不能修改参数");
        ShowParamMenu(client, idx);
        return 0;
    }

    float current = 0.0;
    bool readable = TR_ParamRead(idx, current);
    float target = current;

    if (StrEqual(info, "toggle"))
    {
        target = readable && current != 0.0 ? 0.0 : 1.0;
    }
    else if (StrEqual(info, "minus"))
    {
        target = current - g_fParamStep[idx];
    }
    else if (StrEqual(info, "plus"))
    {
        target = current + g_fParamStep[idx];
    }
    else if (StrEqual(info, "minus10"))
    {
        target = current - g_fParamStep[idx] * 10.0;
    }
    else if (StrEqual(info, "plus10"))
    {
        target = current + g_fParamStep[idx] * 10.0;
    }
    else if (StrContains(info, "q:") == 0)
    {
        int qi = StringToInt(info[2]);
        if (qi >= 0 && qi < g_iParamQuickCount[idx])
        {
            target = g_fParamQuick[idx][qi];
        }
    }
    else if (StrEqual(info, "default"))
    {
        if (g_iParamKind[idx] != TRK_ENGINE)
        {
            TR_Chat(client, "脚本字段没有引擎注册默认值；请直接输入数值");
            ShowParamMenu(client, idx);
            return 0;
        }
        target = TR_ParamDefault(idx);
    }
    else if (StrEqual(info, "original"))
    {
        if (TR_ParamRestore(idx))
        {
            char now[32];
            TR_FormatValue(idx, now, sizeof(now));
            TR_Chat(client, "%s 已恢复初始值，实际 = %s", g_sParamLabel[idx], now);
        }
        else
        {
            TR_Chat(client, "恢复失败：参数不可用或插件已关闭");
        }
        ShowParamMenu(client, idx);
        return 0;
    }
    else if (StrEqual(info, "input"))
    {
        g_iPendKind[client] = TR_PEND_PARAM;
        g_iPendParam[client] = idx;
        TR_Chat(client, "请在聊天栏输入 %s 的数值（发送 cancel 取消，或 !tank 重新打开菜单）", g_sParamLabel[idx]);
        return 0;
    }
    else
    {
        ShowParamMenu(client, idx);
        return 0;
    }

    bool ok = TR_ParamWrite(idx, target, false);
    char now[32];
    TR_FormatValue(idx, now, sizeof(now));
    if (ok)
    {
        TR_Chat(client, "%s = %s（已读回实际值）", g_sParamLabel[idx], now);
    }
    else
    {
        TR_Chat(client, "%s 修改失败，当前实际 = %s（检查范围/模式覆盖/脚本状态）", g_sParamLabel[idx], now);
    }
    ShowParamMenu(client, idx);
    return 0;
}

// ---------------------------------------------------------------- Tank 列表
void ShowTankListMenu(int client)
{
    g_iMenuView[client] = SEC_INSTANCE;
    int aiCount = TR_CountTanks(true);
    int allCount = TR_CountTanks(false);
    char title[192];
    Format(title, sizeof(title), "当前 Tank 列表\nAI Tank：%d 只　玩家 Tank：%d 只\n状态每 1 秒刷新，点击可单独控制",
        aiCount, allCount - aiCount);

    Menu menu = TR_NewMenu(Handler_TankList, title, true);
    if (aiCount > 0)
    {
        menu.AddItem("a:kill_oldest", "处死最早出生的 AI Tank", TR_EditDraw(client));
        menu.AddItem("a:kill_newest", "处死最后出生的 AI Tank", TR_EditDraw(client));
    }
    int list[MAXPLAYERS + 1];
    int count = TR_GetTankList(list, sizeof(list), !g_cvAiOnly.BoolValue);
    if (count == 0)
    {
        menu.AddItem("x:none", "当前没有可控制的 Tank", ITEMDRAW_DISABLED);
    }
    char key[24];
    char text[160];
    for (int i = 0; i < count; i++)
    {
        Format(key, sizeof(key), "t:%d", GetClientUserId(list[i]));
        TR_FormatTankLine(list[i], text, sizeof(text));
        menu.AddItem(key, text);
    }
    menu.AddItem("a:kill_all", "清理全部 AI Tank", TR_EditDraw(client));
    menu.AddItem("a:main", "返回主菜单");
    menu.Display(client, MENU_TIME_FOREVER);
}

public int Handler_TankList(Menu menu, MenuAction action, int client, int item)
{
    if (action == MenuAction_End)
    {
        delete menu;
        return 0;
    }
    if (!TR_CanOpen(client))
    {
        return 0;
    }
    if (action == MenuAction_Cancel)
    {
        if (item == MenuCancel_ExitBack)
        {
            ShowMainMenu(client);
        }
        return 0;
    }
    if (action != MenuAction_Select)
    {
        return 0;
    }
    char info[64];
    menu.GetItem(item, info, sizeof(info));
    if (StrEqual(info, "a:main"))
    {
        ShowMainMenu(client);
        return 0;
    }
    if (StrContains(info, "t:") == 0)
    {
        int tank = GetClientOfUserId(StringToInt(info[2]));
        if (tank > 0 && tank <= MaxClients && TR_IsTankClient(tank))
        {
            ShowTankMenu(client, tank);
        }
        else
        {
            TR_Chat(client, "该 Tank 已不存在，列表已刷新");
            ShowTankListMenu(client);
        }
        return 0;
    }
    if (StrEqual(info, "a:kill_all"))
    {
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足");
            return 0;
        }
        int killed = TR_KillAllTanks("列表清理");
        TR_Chat(client, "已清理 %d 只 AI Tank", killed);
        ShowTankListMenu(client);
        return 0;
    }
    if (StrEqual(info, "a:kill_oldest") || StrEqual(info, "a:kill_newest"))
    {
        if (!TR_CanEdit(client)) return 0;
        int tank = TR_FindBirthOrderedTank(StrEqual(info, "a:kill_newest"));
        if (tank == 0) TR_Chat(client, "没有可按出生顺序处死的 AI Tank，请从列表选择具体 Tank");
        else TR_Chat(client, "Tank #%d：%s", tank, TR_KillTank(tank, "Tank 列表处死", true) ? "已处死" : "未确认退场");
        ShowTankListMenu(client);
        return 0;
    }
    return 0;
}

void ShowTankMenu(int client, int tank)
{
    g_iMenuView[client] = 200;
    g_iMenuTankUserid[client] = GetClientUserId(tank);
    char title[200];
    char targetText[40];
    int target = g_iTankChosenTarget[tank];
    if (target > 0 && target <= MaxClients && IsClientInGame(target))
    {
        GetClientName(target, targetText, sizeof(targetText));
    }
    else
    {
        strcopy(targetText, sizeof(targetText), "未知");
    }
    Format(title, sizeof(title), "Tank #%d 独立控制\n血量 %d/%d　投石 %d　拳击 %d\n首石：%s　目标：%s",
        tank, GetClientHealth(tank), GetEntProp(tank, Prop_Data, "m_iMaxHealth"),
        g_iTankReleaseCount[tank], g_iTankSwingCount[tank],
        g_bTankRockWindow[tank] ? "窗口激活" : (g_bTankStrategyOff[tank] ? "已停用" : "待命"),
        targetText);

    Menu menu = TR_NewMenu(Handler_Tank, title, true);
    int draw = TR_EditDraw(client);
    char info[128];
    int nearestWho = 0;
    float nearestDist = -1.0;
    if (TR_IsTankClient(tank))
    {
        float origin[3];
        GetClientAbsOrigin(tank, origin);
        nearestDist = TR_NearestSurvivorDistance(origin, nearestWho);
    }
    float allowRange = 0.0;
    int throwRangeIdx = TR_FindParamByName("tank_throw_allow_range");
    if (throwRangeIdx >= 0)
    {
        TR_ParamRead(throwRangeIdx, allowRange);
    }
    if (nearestDist >= 0.0)
    {
        Format(info, sizeof(info), "距最近幸存者 %.0f，投石允许距离 %.0f（原生资格门）", nearestDist, allowRange);
    }
    else
    {
        strcopy(info, sizeof(info), "当前没有存活幸存者");
    }
    menu.AddItem("x:range", info, ITEMDRAW_DISABLED);
    menu.AddItem("a:kill", "处死该 Tank", draw);
    menu.AddItem("a:heal", "治疗至满血", draw);
    menu.AddItem("a:sethp", "设置血量（聊天输入）", draw);
    menu.AddItem("a:applyhp", "应用当前血量策略", draw);
    menu.AddItem("a:window", "重新开启首石窗口", draw);
    menu.AddItem("a:togglestrategy", "切换首石策略停用状态", draw);
    menu.AddItem("a:reset", "重置该 Tank 统计", draw);
    menu.AddItem("a:back", "返回 Tank 列表");
    menu.Display(client, MENU_TIME_FOREVER);
}

public int Handler_Tank(Menu menu, MenuAction action, int client, int item)
{
    if (action == MenuAction_End)
    {
        delete menu;
        return 0;
    }
    if (!TR_CanOpen(client))
    {
        return 0;
    }
    int tank = GetClientOfUserId(g_iMenuTankUserid[client]);
    if (action == MenuAction_Cancel)
    {
        if (item == MenuCancel_ExitBack)
        {
            ShowTankListMenu(client);
        }
        return 0;
    }
    if (action != MenuAction_Select)
    {
        return 0;
    }
    char info[64];
    menu.GetItem(item, info, sizeof(info));
    if (StrEqual(info, "a:back"))
    {
        ShowTankListMenu(client);
        return 0;
    }
    if (!TR_IsTankClient(tank))
    {
        TR_Chat(client, "该 Tank 已不存在");
        ShowTankListMenu(client);
        return 0;
    }
    if (!TR_CanEdit(client))
    {
        TR_Chat(client, "权限不足：只读模式不能执行该操作");
        ShowTankMenu(client, tank);
        return 0;
    }

    if (StrEqual(info, "a:heal"))
    {
        TR_HealTank(tank);
        TR_Chat(client, "Tank #%d 已治疗", tank);
    }
    else if (StrEqual(info, "a:sethp"))
    {
        g_iPendKind[client] = TR_PEND_TANKHP;
        g_iPendTank[client] = tank;
        TR_Chat(client, "请在聊天栏输入 Tank #%d 的目标血量（发送 cancel 取消）", tank);
        return 0;
    }
    else if (StrEqual(info, "a:applyhp"))
    {
        TR_ApplyHealthPolicy(tank, "单独应用");
        TR_Chat(client, "已应用当前血量策略（当前 %d 最大 %d）", GetClientHealth(tank),
            GetEntProp(tank, Prop_Data, "m_iMaxHealth"));
    }
    else if (StrEqual(info, "a:window"))
    {
        g_bTankStrategyOff[tank] = false;
        TR_StartRockWindow(tank, "管理员手动开启");
        TR_Chat(client, "首石窗口状态：%s", g_bTankRockWindow[tank] ? "已开启" : "未开启（策略关闭或插件停用）");
    }
    else if (StrEqual(info, "a:togglestrategy"))
    {
        g_bTankStrategyOff[tank] = !g_bTankStrategyOff[tank];
        TR_Chat(client, "该 Tank 首石策略：%s", g_bTankStrategyOff[tank] ? "已停用" : "已启用");
    }
    else if (StrEqual(info, "a:reset"))
    {
        g_iTankReleaseCount[tank] = 0;
        g_iTankSwingCount[tank] = 0;
        g_iTankHitCount[tank] = 0;
        g_iTankBlockedCount[tank] = 0;
        g_iTankOverrides[tank] = 0;
        g_bTankFirstContact[tank] = false;
        TR_Chat(client, "该 Tank 统计已重置");
    }
    else if (StrEqual(info, "a:kill"))
    {
        bool killed = TR_KillTank(tank, "单只菜单处死", true);
        TR_Chat(client, "Tank #%d：%s", tank, killed ? "已处死" : "未确认退场");
        ShowTankListMenu(client);
        return 0;
    }
    ShowTankMenu(client, tank);
    return 0;
}

// ---------------------------------------------------------------- 预设
void ShowPresetMenu(int client)
{
    g_iMenuView[client] = SEC_PRESET;
    Menu menu = TR_NewMenu(Handler_Preset, "参数预设与配置保存\n实验配置已自动保留，服务器重启后清空\n手动预设保存引擎 ConVar 与插件参数", true);
    char key[24];
    char text[128];
    for (int i = 0; i < TR_PRESET_SLOTS; i++)
    {
        TR_FormatPresetSlot(i, text, sizeof(text));
        Format(key, sizeof(key), "ps:%d", i);
        menu.AddItem(key, text);
    }
    menu.AddItem("a:restore_all", "恢复全部参数初始值（含插件参数）", TR_EditDraw(client));
    menu.AddItem("a:save_settings", "保存当前插件设置到文件", TR_EditDraw(client));
    menu.AddItem("a:reload", "手动加载插件设置文件", TR_EditDraw(client));
    menu.AddItem("a:main", "返回主菜单");
    menu.Display(client, MENU_TIME_FOREVER);
}

public int Handler_Preset(Menu menu, MenuAction action, int client, int item)
{
    if (action == MenuAction_End)
    {
        delete menu;
        return 0;
    }
    if (!TR_CanOpen(client))
    {
        return 0;
    }
    if (action == MenuAction_Cancel)
    {
        if (item == MenuCancel_ExitBack)
        {
            ShowMainMenu(client);
        }
        return 0;
    }
    if (action != MenuAction_Select)
    {
        return 0;
    }
    char info[64];
    menu.GetItem(item, info, sizeof(info));
    if (StrEqual(info, "a:main"))
    {
        ShowMainMenu(client);
        return 0;
    }
    if (StrEqual(info, "a:restore_all"))
    {
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足");
            return 0;
        }
        TR_RestoreAll();
        ShowPresetMenu(client);
        return 0;
    }
    if (StrEqual(info, "a:save_settings"))
    {
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足");
            return 0;
        }
        TR_SaveSettings();
        TR_Chat(client, "插件设置已保存");
        ShowPresetMenu(client);
        return 0;
    }
    if (StrEqual(info, "a:reload"))
    {
        if (!TR_CanEdit(client)) return 0;
        TR_LoadSettings();
        TR_LoadOriginals();
        TR_Chat(client, "已重新加载设置与初始值文件");
        ShowPresetMenu(client);
        return 0;
    }
    if (StrContains(info, "ps:") == 0)
    {
        int slot = StringToInt(info[3]);
        if (slot >= 0 && slot < TR_PRESET_SLOTS)
        {
            g_iMenuSlot[client] = slot;
            ShowPresetSlotMenu(client, slot);
        }
        return 0;
    }
    return 0;
}

void ShowPresetSlotMenu(int client, int slot)
{
    g_iMenuView[client] = 300 + slot;
    char name[64];
    if (!TR_GetPresetName(slot, name, sizeof(name)))
    {
        strcopy(name, sizeof(name), "（空）");
    }
    char title[160];
    Format(title, sizeof(title), "预设槽 %d：%s\n应用/保存/重命名/删除", slot + 1, name);
    Menu menu = TR_NewMenu(Handler_PresetSlot, title, true);
    int draw = TR_EditDraw(client);
    menu.AddItem("a:apply", "应用该预设", draw);
    menu.AddItem("a:save", "把当前参数保存到该槽（聊天输入名称）", draw);
    menu.AddItem("a:rename", "重命名该槽（聊天输入名称）", draw);
    menu.AddItem("a:delete", "删除该槽", draw);
    menu.AddItem("a:back", "返回预设列表");
    menu.Display(client, MENU_TIME_FOREVER);
}

public int Handler_PresetSlot(Menu menu, MenuAction action, int client, int item)
{
    if (action == MenuAction_End)
    {
        delete menu;
        return 0;
    }
    if (!TR_CanOpen(client))
    {
        return 0;
    }
    int slot = g_iMenuSlot[client];
    if (action == MenuAction_Cancel)
    {
        if (item == MenuCancel_ExitBack)
        {
            ShowPresetMenu(client);
        }
        return 0;
    }
    if (action != MenuAction_Select)
    {
        return 0;
    }
    char info[64];
    menu.GetItem(item, info, sizeof(info));
    if (StrEqual(info, "a:back"))
    {
        ShowPresetMenu(client);
        return 0;
    }
    if (!TR_CanEdit(client))
    {
        TR_Chat(client, "权限不足：只读模式不能修改预设");
        ShowPresetSlotMenu(client, slot);
        return 0;
    }
    if (StrEqual(info, "a:apply"))
    {
        int applied = TR_ApplyPreset(slot, client);
        if (applied < 0)
        {
            TR_Chat(client, "预设槽 %d 为空或读取失败", slot + 1);
        }
        ShowPresetSlotMenu(client, slot);
        return 0;
    }
    if (StrEqual(info, "a:save") || StrEqual(info, "a:rename"))
    {
        g_iPendKind[client] = StrEqual(info, "a:save") ? TR_PEND_PRESET : TR_PEND_RENAME;
        g_iPendSlot[client] = slot;
        TR_Chat(client, "请在聊天栏输入预设名称（最多 32 字符，发送 cancel 取消）");
        return 0;
    }
    if (StrEqual(info, "a:delete"))
    {
        bool ok = TR_DeletePreset(slot);
        TR_Chat(client, ok ? "预设槽 %d 已删除" : "预设槽 %d 为空，无需删除", slot + 1);
        ShowPresetSlotMenu(client, slot);
        return 0;
    }
    return 0;
}

// ---------------------------------------------------------------- 监控
void ShowMonitorMenu(int client)
{
    g_iMenuView[client] = SEC_MONITOR;
    char title[240];
    Format(title, sizeof(title), "实时监控与调试\nTick：%.3f 秒（%.0f tick）　累计生成：%d\n投石释放：%d　拳击挥击：%d　命中：%d",
        TR_TickInterval(), TR_CurrentTickRate(), g_iStatSpawns,
        g_iStatReleases, g_iStatSwings, g_iStatHits);

    Menu menu = TR_NewMenu(Handler_Monitor, title, true);
    int list[MAXPLAYERS + 1];
    int count = TR_GetTankList(list, sizeof(list), true);
    char key[24];
    char text[160];
    if (count == 0)
    {
        menu.AddItem("x:none", "当前没有 Tank", ITEMDRAW_DISABLED);
    }
    for (int i = 0; i < count; i++)
    {
        Format(key, sizeof(key), "t:%d", list[i]);
        TR_FormatTankLine(list[i], text, sizeof(text));
        menu.AddItem(key, text);
    }
    menu.AddItem("a:refresh", "刷新监控数据");
    Format(text, sizeof(text), "调试控制台输出：%s（点击切换）", g_cvDebug.BoolValue ? "开" : "关");
    menu.AddItem("a:toggledebug", text, TR_EditDraw(client));
    Format(text, sizeof(text), "事件日志：%s（点击切换）", g_cvLogEvents.BoolValue ? "开" : "关");
    menu.AddItem("a:togglelog", text, TR_EditDraw(client));
    menu.AddItem("a:dump", "导出诊断文件（供后续分析）");
    menu.AddItem("a:main", "返回主菜单");
    menu.Display(client, MENU_TIME_FOREVER);
}

public int Handler_Monitor(Menu menu, MenuAction action, int client, int item)
{
    if (action == MenuAction_End)
    {
        delete menu;
        return 0;
    }
    if (!TR_CanOpen(client))
    {
        return 0;
    }
    if (action == MenuAction_Cancel)
    {
        if (item == MenuCancel_ExitBack)
        {
            ShowMainMenu(client);
        }
        return 0;
    }
    if (action != MenuAction_Select)
    {
        return 0;
    }
    char info[64];
    menu.GetItem(item, info, sizeof(info));
    if (StrEqual(info, "a:main"))
    {
        ShowMainMenu(client);
        return 0;
    }
    if (StrEqual(info, "a:refresh"))
    {
        ShowMonitorMenu(client);
        return 0;
    }
    if (StrContains(info, "t:") == 0)
    {
        int tank = StringToInt(info[2]);
        if (tank > 0 && tank <= MaxClients && TR_IsTankClient(tank))
        {
            ShowTankMenu(client, tank);
        }
        else
        {
            ShowMonitorMenu(client);
        }
        return 0;
    }
    if (StrEqual(info, "a:toggledebug") || StrEqual(info, "a:togglelog"))
    {
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足");
            return 0;
        }
        if (StrEqual(info, "a:toggledebug"))
        {
            g_cvDebug.BoolValue = !g_cvDebug.BoolValue;
            TR_Chat(client, "调试输出：%s", g_cvDebug.BoolValue ? "开" : "关");
        }
        else
        {
            g_cvLogEvents.BoolValue = !g_cvLogEvents.BoolValue;
            TR_Chat(client, "事件日志：%s", g_cvLogEvents.BoolValue ? "开" : "关");
        }
        ShowMonitorMenu(client);
        return 0;
    }
    if (StrEqual(info, "a:dump"))
    {
        if (!TR_IsAdmin(client))
        {
            TR_Chat(client, "权限不足：导出诊断需要管理员权限");
            return 0;
        }
        char path[PLATFORM_MAX_PATH];
        TR_WriteDiagnosticDump(path, sizeof(path));
        TR_Chat(client, "诊断文件已写入：%s", path);
        ShowMonitorMenu(client);
        return 0;
    }
    return 0;
}

// ---------------------------------------------------------------- 状态与帮助
void ShowStatusMenu(int client)
{
    g_iMenuView[client] = SEC_STATUS;
    char mode[64];
    char map[128];
    char diff[32];
    TR_GetMode(mode, sizeof(mode));
    GetCurrentMap(map, sizeof(map));
    TR_GetDifficulty(diff, sizeof(diff));

    char title[240];
    Format(title, sizeof(title), "插件状态、兼容性与帮助\n版本 v%s　模式：%s　难度：%s\n地图：%s",
        TR_VERSION, mode, diff, map);

    Menu menu = TR_NewMenu(Handler_Status, title, true);
    menu.AddItem("a:toggle_enabled", TR_FeatureEnabled() ? "功能总开关：开（点击关闭）" : "功能总开关：关（点击开启）", TR_IsAdmin(client) ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
    menu.AddItem("a:chat_status", "在聊天栏输出完整状态");
    menu.AddItem("a:probe", "重新探测依赖与脚本", TR_IsAdmin(client) ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
    menu.AddItem("a:help", "查看帮助与命令");
    menu.AddItem("a:restore_all", "恢复全部参数初始值（含插件参数）", TR_EditDraw(client));
    menu.AddItem("a:dump", "导出诊断文件", TR_IsAdmin(client) ? ITEMDRAW_DEFAULT : ITEMDRAW_DISABLED);
    menu.AddItem("a:main", "返回主菜单");
    menu.Display(client, MENU_TIME_FOREVER);
}

public int Handler_Status(Menu menu, MenuAction action, int client, int item)
{
    if (action == MenuAction_End)
    {
        delete menu;
        return 0;
    }
    if (!TR_CanOpen(client))
    {
        return 0;
    }
    if (action == MenuAction_Cancel)
    {
        if (item == MenuCancel_ExitBack)
        {
            ShowMainMenu(client);
        }
        return 0;
    }
    if (action != MenuAction_Select)
    {
        return 0;
    }
    char info[64];
    menu.GetItem(item, info, sizeof(info));
    if (StrEqual(info, "a:main"))
    {
        ShowMainMenu(client);
        return 0;
    }
    if (StrEqual(info, "a:toggle_enabled"))
    {
        if (!TR_IsAdmin(client))
        {
            TR_Chat(client, "权限不足");
            return 0;
        }
        g_cvEnabled.BoolValue = !g_cvEnabled.BoolValue;
        TR_Chat(client, "功能总开关：%s（关闭后所有修改与策略停止）", g_cvEnabled.BoolValue ? "开" : "关");
        TR_Log("管理员#%d 切换功能总开关为 %d", client, g_cvEnabled.BoolValue ? 1 : 0);
        ShowStatusMenu(client);
        return 0;
    }
    if (StrEqual(info, "a:chat_status"))
    {
        TR_PrintStatus(client);
        ShowStatusMenu(client);
        return 0;
    }
    if (StrEqual(info, "a:probe"))
    {
        TR_ProbeDependencies();
        TR_Chat(client, "依赖探测：Left4DHooks=%s；脚本=%s", g_bLeft4D ? "可用" : "不可用", g_sVStatus);
        ShowStatusMenu(client);
        return 0;
    }
    if (StrEqual(info, "a:help"))
    {
        ShowHelpMenu(client);
        return 0;
    }
    if (StrEqual(info, "a:restore_all"))
    {
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足");
            return 0;
        }
        TR_RestoreAll();
        ShowStatusMenu(client);
        return 0;
    }
    if (StrEqual(info, "a:dump"))
    {
        char path[PLATFORM_MAX_PATH];
        TR_WriteDiagnosticDump(path, sizeof(path));
        TR_Chat(client, "诊断文件已写入：%s", path);
        ShowStatusMenu(client);
        return 0;
    }
    return 0;
}

void ShowHelpMenu(int client)
{
    g_iMenuView[client] = SEC_STATUS;
    Menu menu = TR_NewMenu(Handler_Help, "帮助与命令\n聊天：!tank 打开菜单\n控制台：以下命令", true);
    menu.AddItem("x:1", "sm_tank / sm_tankmenu：打开本菜单", ITEMDRAW_DISABLED);
    menu.AddItem("x:2", "sm_tank_status：文本状态", ITEMDRAW_DISABLED);
    menu.AddItem("x:3", "sm_tank_diag：诊断摘要并写日志", ITEMDRAW_DISABLED);
    menu.AddItem("x:4", "sm_tank_dump：导出诊断文件", ITEMDRAW_DISABLED);
    menu.AddItem("x:5", "sm_tank_spawn [native|band|aim|script|near]", ITEMDRAW_DISABLED);
    menu.AddItem("x:6", "sm_tank_clear：清理全部 AI Tank", ITEMDRAW_DISABLED);
    menu.AddItem("x:7", "sm_tank_set <参数名> <数值>：直接写入参数", ITEMDRAW_DISABLED);
    menu.AddItem("x:8", "sm_tank_restore [参数名|all]：恢复初始值", ITEMDRAW_DISABLED);
    menu.AddItem("x:9", "sm_tank_preset <save|load|delete|list> [槽位] [名称]", ITEMDRAW_DISABLED);
    menu.AddItem("x:10", "sm_tank_debug 0/1、sm_tank_log 0/1、sm_tank_reload", ITEMDRAW_DISABLED);
    menu.AddItem("x:11", "注意：本插件不会伪造投石或动画；会被拦截的条件写入日志", ITEMDRAW_DISABLED);
    menu.AddItem("a:back", "返回状态菜单");
    menu.Display(client, MENU_TIME_FOREVER);
}

public int Handler_Help(Menu menu, MenuAction action, int client, int item)
{
    if (action == MenuAction_End)
    {
        delete menu;
        return 0;
    }
    if (!TR_CanOpen(client))
    {
        return 0;
    }
    if (action == MenuAction_Cancel)
    {
        if (item == MenuCancel_ExitBack)
        {
            ShowStatusMenu(client);
        }
        return 0;
    }
    if (action == MenuAction_Select)
    {
        ShowStatusMenu(client);
    }
    return 0;
}

// ---------------------------------------------------------------- 聊天输入
bool TR_HandleChatInput(int client, const char[] text)
{
    if (g_iPendKind[client] == TR_PEND_NONE)
    {
        return false;
    }
    char buffer[128];
    strcopy(buffer, sizeof(buffer), text);
    TrimString(buffer);

    if (StrEqual(buffer, "cancel", false) || StrEqual(buffer, "取消", false))
    {
        g_iPendKind[client] = TR_PEND_NONE;
        TR_Chat(client, "已取消输入");
        return true;
    }

    int kind = g_iPendKind[client];
    g_iPendKind[client] = TR_PEND_NONE;

    if (kind == TR_PEND_PRESET || kind == TR_PEND_RENAME)
    {
        int slot = g_iPendSlot[client];
        if (strlen(buffer) < 1 || strlen(buffer) > 31)
        {
            TR_Chat(client, "预设名称长度必须为 1~31 个字符");
            return true;
        }
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足");
            return true;
        }
        if (kind == TR_PEND_PRESET)
        {
            bool ok = TR_SavePreset(slot, buffer);
            TR_Chat(client, ok ? "已保存到预设槽 %d：%s" : "保存失败（槽位 %d）", slot + 1, buffer);
        }
        else
        {
            // 重命名：使用现有参数重新保存为新名称
            bool ok = TR_SavePreset(slot, buffer);
            TR_Chat(client, ok ? "预设槽 %d 已重命名为：%s" : "重命名失败（槽位 %d）", slot + 1, buffer);
        }
        ShowPresetSlotMenu(client, slot);
        return true;
    }

    if (kind == TR_PEND_TANKHP)
    {
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足");
            return true;
        }
        int tank = g_iPendTank[client];
        if (!TR_IsTankClient(tank))
        {
            TR_Chat(client, "该 Tank 已不存在");
            return true;
        }
        if (!TR_IsNumeric(buffer))
        {
            TR_Chat(client, "输入无效：请输入数字");
            return true;
        }
        float amount = StringToFloat(buffer);
        if (!(amount >= 1.0 && amount <= TR_INT_MAX) || StrContains(buffer, ".") != -1)
        {
            TR_Chat(client, "请输入可表示的正整数血量（旧范围 1~20000，仅供参考）");
            return true;
        }
        int hp = StringToInt(buffer);
        int maxhp = GetEntProp(tank, Prop_Data, "m_iMaxHealth");
        if (hp > maxhp)
        {
            SetEntProp(tank, Prop_Data, "m_iMaxHealth", hp);
        }
        SetEntityHealth(tank, hp);
        TR_Chat(client, "Tank #%d 血量已设为 %d（最大 %d）", tank, GetClientHealth(tank),
            GetEntProp(tank, Prop_Data, "m_iMaxHealth"));
        ShowTankMenu(client, tank);
        return true;
    }

    if (kind == TR_PEND_PARAM)
    {
        int idx = g_iPendParam[client];
        if (idx < 0 || idx >= g_iParamCount)
        {
            TR_Chat(client, "参数状态已失效，请重新打开菜单");
            return true;
        }
        if (!TR_CanEdit(client))
        {
            TR_Chat(client, "权限不足");
            return true;
        }
        if (!TR_IsNumeric(buffer))
        {
            TR_Chat(client, "输入无效：请输入数字（可带小数）");
            return true;
        }
        float value = StringToFloat(buffer);
        float clamped = value;
        if (!TR_ClampValue(idx, clamped))
        {
            TR_Chat(client, "数值无效：生成间隔必须大于 0，数值必须有限");
            return true;
        }
        if (FloatAbs(clamped - value) > 0.0001)
        {
            TR_Chat(client, "数值已按范围校正为 %.2f", clamped);
        }
        bool ok = TR_ParamWrite(idx, clamped, false);
        char now[32];
        TR_FormatValue(idx, now, sizeof(now));
        TR_Chat(client, ok ? "%s = %s（已读回）" : "%s 写入失败，实际 = %s", g_sParamLabel[idx], now);
        ShowParamMenu(client, idx);
        return true;
    }

    return false;
}

bool TR_IsNumeric(const char[] text)
{
    int len = strlen(text);
    if (len == 0)
    {
        return false;
    }
    bool digit = false;
    bool dot = false;
    for (int i = 0; i < len; i++)
    {
        char c = text[i];
        if (c == '-' && i == 0)
        {
            continue;
        }
        if (c == '.' && !dot)
        {
            dot = true;
            continue;
        }
        if (c >= '0' && c <= '9')
        {
            digit = true;
            continue;
        }
        return false;
    }
    return digit;
}
