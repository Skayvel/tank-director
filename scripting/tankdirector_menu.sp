// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

#pragma semicolon 1
#pragma newdecls required

// Tank Run 综合控制中心（Tank Run Control Center）
// 统一入口：复用原有 !tank 菜单与命令，扩展为 M01–M11 控制模块。
// 主要使用公开接口。禁拳可另装同构建的原生武器入口钩子。

#include "tankrun/tr_core.inc"
#include "tankrun/tr_tank.inc"
#include "tankrun/tr_lead_spawn.inc"
#undef REQUIRE_EXTENSIONS
#include <dhooks>
#define REQUIRE_EXTENSIONS
#include "tankrun/tr_attacks.inc"
#include "tankrun/tr_rock.inc"
#include "tankrun/tr_status.inc"
#include "tankrun/tr_menu.inc"
#include "tankrun/tr_diag.inc"
#define TR_EMBEDDED_CAPACITY
#include "tankrun_capacity.sp"
#undef TR_EMBEDDED_CAPACITY

public Plugin myinfo =
{
    name = "Tank Run Control Center",
    author = "Tank Run Project",
    description = "AI Tank control center with Chinese in-game menus (M01-M11)",
    version = TR_VERSION,
    url = ""
};

Handle g_hScanTimer = null;
Handle g_hProbeTimer = null;

// ---------------------------------------------------------------- 生命周期
public void OnPluginStart()
{
    if (GetEngineVersion() != Engine_Left4Dead2)
    {
        SetFailState("Tank Run 综合控制中心仅支持 Left 4 Dead 2");
        return;
    }

    BuildPath(Path_SM, g_sLogPath, sizeof(g_sLogPath), "logs/tankrun_control.log");
    BuildPath(Path_SM, g_sSettingsPath, sizeof(g_sSettingsPath), "configs/tankrun_control_settings.cfg");
    BuildPath(Path_SM, g_sOriginalsPath, sizeof(g_sOriginalsPath), "configs/tankrun_control_originals.cfg");
    strcopy(g_sPresetDir, sizeof(g_sPresetDir), "configs");

    TR_CreateConVars();
    TR_RegisterParams();
    TR_BindConVars();
    TR_InitSession();
    TR_LoadOriginals();
    TR_RegisterCommands();
    TR_HookEvents();
    TR_InstallFirstRockClawGate();
    TC_OnPluginStart();

    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsClientInGame(i))
        {
            SDKHook(i, SDKHook_OnTakeDamage, TR_OnTakeDamage);
            SDKHook(i, SDKHook_PostThinkPost, TR_PostThinkRockView);
            TR_ClearRockMotion(i);
        }
    }

    TR_CheckMissingConVars();
    TR_Log("插件已加载 v%s：注册参数 %d 项，日志 %s", TR_VERSION, g_iParamCount, g_sLogPath);

    // 普通依赖探测：只读原生/扩展状态，不创建实体，可在无地图时安全执行。
    TR_ProbeLibraries();
    if (g_sVStatus[0] == '\0')
    {
        strcopy(g_sVStatus, sizeof(g_sVStatus), "尚未探测（等待地图运行/OnConfigsExecuted）");
    }
    TR_LogLibraries();

    // 运行态探测（会创建 logic_script）一律推迟：冷启动交给 OnMapStart/OnConfigsExecuted，
    // 热加载时由 TR_ScheduleRuntimeProbe 在实体系统就绪后延迟 2 秒执行。
    TR_EnsureScanTimer();
    TR_EnsureAutoTimer();
    TR_ScheduleRuntimeProbe("插件加载");
}

void TR_LogLibraries()
{
    TR_Log("依赖探测（静态）：Left4DHooks=%s SDKHooks=%s 地图运行=%s",
        g_bLeft4D ? "可用" : "不可用",
        g_bSdkHooks ? "可用" : "不可用",
        TR_IsMapRunning() ? "是" : "否");
}

// 扫描定时器唯一入口：已存在时不重复创建，地图未就绪时不启动。
void TR_EnsureScanTimer()
{
    if (g_hScanTimer != null || !TR_IsMapRunning())
    {
        return;
    }
    g_hScanTimer = CreateTimer(0.25, Timer_Scan, 0, TIMER_REPEAT | TIMER_FLAG_NO_MAPCHANGE);
}

// 运行态探测排队：同一时间只允许一个延迟探测；地图未就绪则直接跳过，
// 等 OnMapStart / OnConfigsExecuted 再次安排。
void TR_ScheduleRuntimeProbe(const char[] reason)
{
    if (g_hProbeTimer != null)
    {
        return;
    }
    if (!TR_IsMapRunning())
    {
        TR_Log("运行态探测推迟：地图/实体系统未就绪（%s）", reason);
        return;
    }
    g_hProbeTimer = CreateTimer(2.0, Timer_RuntimeProbe, 0, TIMER_FLAG_NO_MAPCHANGE);
    TR_Log("已安排运行态探测（%s）：2 秒后执行，不创建提前实体", reason);
}

void TR_HookEvents()
{
    HookEvent("tank_spawn", Event_TankSpawn, EventHookMode_Post);
    HookEvent("player_death", Event_PlayerDeath, EventHookMode_Post);
    HookEvent("round_start", Event_RoundStart, EventHookMode_Post);
    HookEvent("round_end", Event_RoundEnd, EventHookMode_Post);
    HookEvent("mission_lost", Event_RoundEnd, EventHookMode_Post);
    HookEvent("map_transition", Event_RoundEnd, EventHookMode_Post);
    HookEvent("finale_vehicle_leaving", Event_RoundEnd, EventHookMode_Post);
    HookEvent("player_hurt", Event_PlayerHurt, EventHookMode_Post);
}

void TR_CheckMissingConVars()
{
    char missing[512];
    missing[0] = '\0';
    int count = 0;
    for (int i = 0; i < g_iParamCount; i++)
    {
        if (g_iParamKind[i] == TRK_ENGINE && g_ParamCVar[i] == null)
        {
            count++;
            if (strlen(missing) < 380)
            {
                StrCat(missing, sizeof(missing), g_sParamName[i]);
                StrCat(missing, sizeof(missing), " ");
            }
        }
    }
    if (count > 0)
    {
        TR_Log("以下 %d 个引擎 ConVar 在本构建未注册，菜单中标记为不可用：%s", count, missing);
        PrintToServer("%s Notice: %d engine params missing in this build (see %s)", TR_TAG, count, g_sLogPath);
    }
}

public void OnPluginEnd()
{
    if (g_hFirstRockClawGate != null)
    {
        g_hFirstRockClawGate.Disable(Hook_Pre, TR_DetourFirstRockClaw);
        delete g_hFirstRockClawGate;
    }
    TR_StopTimers();
    TR_RestoreScriptThink();
    TR_ClearLeadSpawnNavCache();
    TC_OnPluginEnd();
    TR_Log("插件已卸载（自动生成定时器与脚本接管已恢复）");
}

public void OnMapStart()
{
    g_bMapEnding = false;
    g_bRoundEnded = false;
    g_bSessionReady = false;
    g_bVValid = false;
    g_bVProbed = false;
    g_bScriptThinkDisabled = false;
    GetCurrentMap(g_sMap, sizeof(g_sMap));
    TR_GetMode(g_sMode, sizeof(g_sMode));
    TR_ResetAllTanks("地图开始");
    TR_ResetRockStats();
    TR_StopTimers();
    TC_OnMapStart();
    TR_ProbeLibraries();
    TR_EnsureScanTimer();
    TR_EnsureAutoTimer();
    TR_ScheduleRuntimeProbe("地图开始");
    TR_Log("地图开始：%s（模式 %s）", g_sMap, g_sMode);
    TR_LogEvent("地图开始：%s", g_sMap);
}

public void OnMapEnd()
{
    g_bMapEnding = true;
    g_bSessionReady = false;
    TR_StopTimers();
    TR_ClearLeadSpawnNavCache();
    TC_OnMapEnd();
    TR_ResetAllTanks("地图结束");
    TR_LogEvent("地图结束");
}

public void OnConfigsExecuted()
{
    g_bMapEnding = false;
    TR_GetMode(g_sMode, sizeof(g_sMode));
    GetCurrentMap(g_sMap, sizeof(g_sMap));
    TR_ProbeLibraries();
    TR_ApplySessionConVars();
    if (TR_FeatureEnabled() && g_cvDefaultExpert.BoolValue && TR_IsTankRunMode())
    {
        ConVar difficulty = FindConVar("z_difficulty");
        if (difficulty != null)
        {
            difficulty.SetString("Impossible");
            char actual[32];
            difficulty.GetString(actual, sizeof(actual));
            TR_Log("Tank Run 地图默认难度：请求 Impossible，读回 %s", actual);
        }
    }
    g_bSessionReady = true;
    TR_EnsureScanTimer();
    TR_EnsureAutoTimer();
    TR_ScheduleRuntimeProbe("配置执行");
}

void TR_StopTimers()
{
    if (g_hProbeTimer != null)
    {
        KillTimer(g_hProbeTimer);
        g_hProbeTimer = null;
    }
    if (g_hScanTimer != null)
    {
        KillTimer(g_hScanTimer);
        g_hScanTimer = null;
    }
    g_bAutoRunning = false;
    g_bFrontReplacement = false;
}

public void TR_OnPluginConfigChanged(ConVar convar, const char[] oldValue, const char[] newValue)
{
    if ((convar == g_cvEnabled || convar == g_cvRockEnabled) && !convar.BoolValue)
    {
        for (int i = 1; i <= MaxClients; i++) TR_ClearRockMotion(i);
    }
    if (!g_bSessionReady || g_bRestoringSession) return;
    for (int i = 0; i < TR_SESSION_EXTRA; i++)
    {
        if (g_ExtraConfig[i] == convar)
        {
            g_ExtraSession[i].SetString(newValue);
            if (convar == g_cvEnabled)
            {
                if (g_cvEnabled.BoolValue) TR_ScheduleRuntimeProbe("重新启用功能");
                else TR_RestoreScriptThink();
                TR_EnsureAutoTimer();
            }
            return;
        }
    }
    for (int i = 0; i < g_iParamCount; i++)
    {
        if (g_iParamKind[i] != TRK_PLUGIN || g_ParamCVar[i] != convar) continue;
        float value;
        TR_ParamRead(i, value);
        float validated = value;
        if (TR_ClampValue(i, validated) && validated == value) TR_RememberParam(i, value);
        break;
    }
    if (convar == g_cvAutoInterval) g_fAutoSpawnNext = GetGameTime() + g_cvAutoInterval.FloatValue;
    if (convar == g_cvAutoSpawn)
    {
        TR_EnsureAutoTimer();
    }
}

public void OnClientPutInServer(int client)
{
    SDKHook(client, SDKHook_OnTakeDamage, TR_OnTakeDamage);
    SDKHook(client, SDKHook_PostThinkPost, TR_PostThinkRockView);
    TR_ClearRockMotion(client);
    if (!IsFakeClient(client))
    {
        TR_LogEvent("玩家进入服务器 #%d", client);
    }
}

public void OnClientDisconnect(int client)
{
    TR_ResetTank(client, "玩家断开连接");
    g_iPendKind[client] = TR_PEND_NONE;
    g_iMenuView[client] = 0;
}

// ---------------------------------------------------------------- 周期扫描
public Action Timer_Scan(Handle timer)
{
    if (!TR_FeatureEnabled())
    {
        return Plugin_Continue;
    }
    TR_SyncTanks();
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!g_bTankTracked[i] || !TR_IsAiTank(i))
        {
            continue;
        }
        TR_UpdateFirstContact(i);
    }
    TR_UpdateRockWindows();
    int liveAi = TR_CountTanks(true);
    if (liveAi > g_iPeakLiveAiTanks) g_iPeakLiveAiTanks = liveAi;
    TR_PollFrontPhase();
    return Plugin_Continue;
}

public Action Timer_RuntimeProbe(Handle timer)
{
    g_hProbeTimer = null;
    // 换图/退出过程中可能已不在地图内，直接结束，绝不在无地图时创建实体。
    if (!TR_IsMapRunning())
    {
        TR_Log("运行态探测取消：地图/实体系统已不可用");
        return Plugin_Continue;
    }
    // 地图确认就绪后再兜底确保定时器存在（冷启动时 OnMapStart 可能早于实体系统就绪）。
    TR_ApplySessionConVars();
    g_bSessionReady = true;
    TR_EnsureScanTimer();
    if (TR_ProbeRuntime()) TR_ApplySessionScript();
    TR_EnsureAutoTimer();
    // Keep the health of ongoing fights during round transitions and hot reloads.
    bool playerOnline = false;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsClientConnected(i) && !IsFakeClient(i))
        {
            playerOnline = true;
            break;
        }
    }
    if (!playerOnline) TR_ApplyHealthToAll("运行态探测后应用");
    TR_ApplySpeedToAll();
    return Plugin_Continue;
}

public Action Timer_ApplyHealthLate(Handle timer, any userid)
{
    int client = GetClientOfUserId(userid);
    if (client <= 0 || !TR_IsAiTank(client))
    {
        return Plugin_Continue;
    }
    TR_ApplyHealthPolicy(client, "生成后延迟应用");
    return Plugin_Continue;
}

// ---------------------------------------------------------------- 事件
public void Event_TankSpawn(Event event, const char[] name, bool dontBroadcast)
{
    int client = GetClientOfUserId(event.GetInt("userid"));
    if (client <= 0 || !TR_IsTankClient(client))
    {
        return;
    }
    if (!IsFakeClient(client) && g_cvAiOnly.BoolValue)
    {
        TR_LogEvent("玩家控制的 Tank 生成 #%d：按配置不介入", client);
        return;
    }
    TR_TrackTank(client, "tank_spawn 事件", true);
    if (TR_IsAiTank(client))
    {
        int liveAi = TR_CountTanks(true);
        if (liveAi > g_iPeakLiveAiTanks) g_iPeakLiveAiTanks = liveAi;
        TR_LogEvent("Tank 出生：userid=%d 当前存活 AI Tank=%d", GetClientUserId(client), liveAi);
        CreateTimer(0.2, Timer_ApplyHealthLate, GetClientUserId(client), TIMER_FLAG_NO_MAPCHANGE);
    }
}

public void Event_PlayerDeath(Event event, const char[] name, bool dontBroadcast)
{
    int userid = event.GetInt("userid");
    int client = GetClientOfUserId(userid);
    if (client <= 0 || !IsClientInGame(client))
    {
        return;
    }
    if (GetClientTeam(client) != 3)
    {
        return;
    }
    if (GetEntProp(client, Prop_Send, "m_zombieClass") != 8)
    {
        return;
    }
    TR_LogEvent("Tank 死亡：#%d 投石 %d 次 拳击 %d 次 命中 %d 次", client,
        g_iTankReleaseCount[client], g_iTankSwingCount[client], g_iTankHitCount[client]);
    TR_ResetTank(client, "player_death");
    if (IsFakeClient(client) && TR_ManagedCapacityActive() && !TC_ManagedMode())
        RequestFrame(TR_ReleaseDeadTankSlot, userid);
}

public void Event_RoundStart(Event event, const char[] name, bool dontBroadcast)
{
    g_bRoundEnded = false;
    g_iPeakLiveAiTanks = 0;
    TR_ResetFrontCycle();
    TR_LogEvent("回合开始");
    g_bVProbed = false;
    TR_ScheduleRuntimeProbe("回合开始");
}

public void Event_RoundEnd(Event event, const char[] name, bool dontBroadcast)
{
    g_bRoundEnded = true;
    g_bFrontReplacement = false;
    TR_LogEvent("回合结束：清理首石窗口与待判定投石状态");
    for (int i = 1; i <= MaxClients; i++)
    {
        TR_ResetRockTelemetry(i);
        g_bTankRockWindow[i] = false;
        g_fTankRockDeadline[i] = 0.0;
        g_fTankPendingActivation[i] = 0.0;
        g_iTankRockAttempts[i] = 0;
    }
}

public void Event_PlayerHurt(Event event, const char[] name, bool dontBroadcast)
{
    int attacker = GetClientOfUserId(event.GetInt("attacker"));
    if (attacker <= 0 || !TR_IsAiTank(attacker))
    {
        return;
    }
    int victim = GetClientOfUserId(event.GetInt("userid"));
    int damage = event.GetInt("dmg_health");
    char weapon[64];
    event.GetString("weapon", weapon, sizeof(weapon));
    TR_LogEvent("Tank 造成伤害：Tank#%d → #%d 伤害=%d 武器=%s", attacker, victim, damage, weapon);
}

// ---------------------------------------------------------------- 聊天输入
public Action OnClientSayCommand(int client, const char[] command, const char[] sArgs)
{
    if (client <= 0 || client > MaxClients)
    {
        return Plugin_Continue;
    }
    if (sArgs[0] == '!' || sArgs[0] == '/')
    {
        return Plugin_Continue;
    }
    if (TR_HandleChatInput(client, sArgs))
    {
        return Plugin_Handled;
    }
    return Plugin_Continue;
}
