// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors
//
// Tank Run 幸存者真人席位管理与挂机转旁观（支持独立编译与主插件内嵌双模）
//
// 设计原则：
// 1. 仅在 Tank Run 合作流程生效，非 Tank Run 自动休眠并恢复 survivor_limit 与 director_afk_timeout。
// 2. 清理空出、未被真人占用/预约的幸存者 Bot；绝不碰感染者、Tank 或容量替身。
// 3. 实证（server.dll 0x1026d5f0 与隔离服验证）：合作团灭判定要求 totalSurvivors != 0 && standingSurvivors == 0；
//    因此在清零前先清理已死亡的无主 Bot，使最后一名真人挂机转旁观或离队时可精准踢除对应 Bot 归零而不触发误团灭，
//    后续玩家入队或 !jg/!join/jointeam 2 仍可通过 L4D2Direct_AddSurvivorBot 从 0 幸存者正常按需补位。
// 4. 在线真人的 AFK/Idle 替身保护持续至挂机倒计时结束并安全解绑为止；主动普通旁观者不参与挂机计时。
// 5. 托管原生 director_afk_timeout（调高至不小于 tankrun_afk_timeout + 120，而非置 0），
//    既防止原生 45s 自动闲置抢跑，又完整保留玩家手动使用游戏“闲置（go_away_from_keyboard）”功能。
// 6. 挂机满阈值后启动仅本人可见的 10 秒逐秒中心提示倒计时，期间有实质操作或主动取回控制立即取消；
//    归零后转入真正旁观、清除 m_humanSpectatorUserID/EntIndex 绑定并精准移除对应 Bot（无确认归属绝不乱踢）。
// 7. 并发加入 FIFO 序列化、满槽含 AFK 预约计入、客户端池上限严格护栏、独立版与内嵌版防冲突自动接管。

#if !defined TR_EMBEDDED_HUMAN_SEATS
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <sdktools>
#include <left4dhooks>
#endif

#define HS_TEAM_UNASSIGNED 0
#define HS_TEAM_SPECTATOR  1
#define HS_TEAM_SURVIVOR   2
#define HS_EXPANDED_SURVIVORS 8
#define HS_AFK_COUNTDOWN_SECONDS 10
#define HS_AFK_ACTIVE_BUTTONS (IN_ATTACK | IN_ATTACK2 | IN_JUMP | IN_DUCK | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_USE | IN_RELOAD | IN_SPEED | IN_ZOOM)
#define HS_VERSION "1.3.0"

#if !defined TR_EMBEDDED_HUMAN_SEATS
public Plugin myinfo =
{
    name = "Tank Run Human Survivor Seats",
    author = "Tank Run project",
    description = "Keeps Tank Run survivor seats human-only with safe on-demand bot handoffs and private AFK countdown",
    version = HS_VERSION,
    url = ""
};
#endif

enum struct HS_JoinRequest
{
    int userid;
    int resumeBot;
    float requestedAt;
    float deadline;
}

ConVar g_HS_Enabled;
ConVar g_HS_AfkTimeout;
ConVar g_HS_Mode;
ConVar g_HS_SurvivorLimit;
ConVar g_HS_CapacityProfile;
ConVar g_HS_NativeAfkTimeout;
Handle g_HS_Timer;
#if !defined TR_EMBEDDED_HUMAN_SEATS
bool g_HS_Late;
#endif
bool g_HS_YieldedToOther;
bool g_HS_ConflictLogged;
#if defined TR_EMBEDDED_HUMAN_SEATS
int g_HS_LegacyUnloadAttempts;
float g_HS_NextLegacyUnloadAt;
#endif
bool g_HS_Transitioning;
float g_HS_ReadyAt;
bool g_HS_OwnerWarning;
bool g_HS_CapacityActive;
bool g_HS_QueueCapacityRoster;
int g_HS_PreviousSurvivorLimit;
bool g_HS_HadSurvivorUpperBound;
float g_HS_PreviousSurvivorUpperBound;
bool g_HS_RaisedSurvivorUpperBound;
bool g_HS_ManagedNativeAfk;
int g_HS_SavedNativeAfk;
int g_HS_AppliedNativeAfk;

HS_JoinRequest g_HS_Requests[MAXPLAYERS + 1];
bool g_HS_EverJoined[MAXPLAYERS + 1];
bool g_HS_VoluntarySpec[MAXPLAYERS + 1];
float g_HS_RetryCooldown[MAXPLAYERS + 1];
int g_HS_DepartingUid[MAXPLAYERS + 1];
int g_HS_ReplacementOwnerUid[MAXPLAYERS + 1];
int g_HS_ReplacementBotUid[MAXPLAYERS + 1];
int g_HS_RetiredBotUid[MAXPLAYERS + 1];

// 挂机检测与仅个人可见 10 秒倒计时状态（按客户端槽位独立隔离，配合 userid 防串台）
float g_HS_AfkLastActive[MAXPLAYERS + 1];
int g_HS_AfkLastButtons[MAXPLAYERS + 1];
float g_HS_AfkLastAngles[MAXPLAYERS + 1][3];
bool g_HS_AfkHasAngles[MAXPLAYERS + 1];
Handle g_HS_AfkTimer[MAXPLAYERS + 1];
int g_HS_AfkCountdownUid[MAXPLAYERS + 1];
int g_HS_AfkCountdownLeft[MAXPLAYERS + 1];
bool g_HS_AfkWasIdleBot[MAXPLAYERS + 1];
int g_HS_AfkEvictingUid[MAXPLAYERS + 1];
int g_HS_AfkEvictedBotUid[MAXPLAYERS + 1];
int g_HS_StatAfkEvictions;

int g_HS_HumanUid;
int g_HS_BotUid;
float g_HS_OperationDeadline;
bool g_HS_CreationRequested;
bool g_HS_ReplacementConfirmed;
float g_HS_VerifyAt;
int g_HS_BotBeforeCreate[MAXPLAYERS + 1];

#if !defined TR_EMBEDDED_HUMAN_SEATS
public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int maxlen)
{
    if (GetEngineVersion() != Engine_Left4Dead2)
    {
        strcopy(error, maxlen, "Tank Run Human Survivor Seats requires Left 4 Dead 2.");
        return APLRes_SilentFailure;
    }
    MarkNativeAsOptional("L4D2_IsGenericCooperativeMode");
    MarkNativeAsOptional("L4D_IsInIntro");
    MarkNativeAsOptional("L4D_IsFinaleEscapeInProgress");
    g_HS_Late = late;
    return APLRes_Success;
}

public void OnPluginStart() { HS_OnPluginStart(); }
public void OnAllPluginsLoaded() { HS_CheckPluginConflict(); }
public void OnLibraryAdded(const char[] name) { HS_CheckPluginConflict(); }
public void OnLibraryRemoved(const char[] name) { HS_CheckPluginConflict(); }
public void OnMapStart() { HS_OnMapStart(); }
public void OnConfigsExecuted() { HS_OnConfigsExecuted(); }
public void OnMapEnd() { HS_OnMapEnd(); }
public void OnPluginEnd() { HS_OnPluginEnd(); }
public void OnClientPostAdminCheck(int client) { HS_OnClientPostAdminCheck(client); }
public void OnClientDisconnect(int client) { HS_OnClientDisconnect(client); }

public Action OnPlayerRunCmd(int client, int &buttons, int &impulse, float vel[3], float angles[3],
    int &weapon, int &subtype, int &cmdnum, int &tickcount, int &seed, int mouse[2])
{
    HS_OnPlayerRunCmd(client, buttons, impulse, angles);
    return Plugin_Continue;
}

public Action OnClientSayCommand(int client, const char[] command, const char[] sArgs)
{
    HS_OnClientActivity(client, true);
    return Plugin_Continue;
}
#endif

// ---------------------------------------------------------------- 双插件防冲突检测
#if defined TR_EMBEDDED_HUMAN_SEATS
bool HS_FindLegacyStandaloneSeatPlugin(char[] outFile, int fileLen, char[] outVer, int verLen)
{
    outFile[0] = '\0';
    outVer[0] = '\0';
    Handle iter = GetPluginIterator();
    if (iter == null)
    {
        return false;
    }
    Handle self = GetMyHandle();
    char filename[PLATFORM_MAX_PATH];
    char pluginName[128];
    char ver[32];
    bool found = false;
    while (MorePlugins(iter))
    {
        Handle plugin = ReadPlugin(iter);
        if (plugin == self || GetPluginStatus(plugin) != Plugin_Running)
        {
            continue;
        }
        filename[0] = '\0';
        pluginName[0] = '\0';
        ver[0] = '\0';
        GetPluginFilename(plugin, filename, sizeof(filename));
        GetPluginInfo(plugin, PlInfo_Name, pluginName, sizeof(pluginName));
        if (StrContains(filename, "tankrun_human_seats", false) != -1
            || StrEqual(pluginName, "Tank Run Human Survivor Seats", false))
        {
            // 旧版独立插件（v1.0.0/v1.1.0 或无版本号）不包含 tankrun_human_seats_embedded 自动让权逻辑。
            GetPluginInfo(plugin, PlInfo_Version, ver, sizeof(ver));
            if (ver[0] == '\0' || StrEqual(ver, "1.0.0") || StrEqual(ver, "1.1.0"))
            {
                strcopy(outFile, fileLen, filename);
                strcopy(outVer, verLen, ver[0] != '\0' ? ver : "unknown");
                found = true;
                break;
            }
        }
    }
    delete iter;
    return found;
}
#endif

void HS_CheckPluginConflict()
{
#if defined TR_EMBEDDED_HUMAN_SEATS
    char legacyFile[PLATFORM_MAX_PATH];
    char legacyVer[32];
    bool hasLegacy = HS_FindLegacyStandaloneSeatPlugin(legacyFile, sizeof(legacyFile), legacyVer, sizeof(legacyVer));
    if (hasLegacy)
    {
        if (!g_HS_YieldedToOther)
        {
            g_HS_YieldedToOther = true;
            HS_CancelAllAfkCountdowns();
            HS_RestoreNativeAfkGuard();
            HS_RestoreSurvivorLimit();
            HS_StopOperation();
        }
        float now = GetGameTime();
        if (g_HS_LegacyUnloadAttempts < 2 && now >= g_HS_NextLegacyUnloadAt)
        {
            g_HS_LegacyUnloadAttempts++;
            g_HS_NextLegacyUnloadAt = now + 1.0;
            LogMessage("[HumanSeats] Detected legacy standalone '%s' (v%s) superseded by embedded Tank Run Control Center seats; unloading it automatically (attempt %d/2). Please remove the old .smx file from addons/sourcemod/plugins/.",
                legacyFile, legacyVer, g_HS_LegacyUnloadAttempts);
            PrintToServer("[TankRun][HumanSeats] Auto-unloading legacy standalone '%s' (v%s) so embedded seats & AFK countdown can take over.",
                legacyFile, legacyVer);
            ServerCommand("sm plugins unload \"%s\"", legacyFile);
        }
        else if (g_HS_LegacyUnloadAttempts >= 2 && !g_HS_ConflictLogged)
        {
            g_HS_ConflictLogged = true;
            LogError("[HumanSeats] Legacy standalone '%s' (v%s) is still running after unload attempts! Embedded seats & AFK countdown remain paused to prevent dual-plugin conflict; delete '%s' and reload.",
                legacyFile, legacyVer, legacyFile);
        }
    }
    else if (g_HS_YieldedToOther)
    {
        g_HS_YieldedToOther = false;
        g_HS_ConflictLogged = false;
        g_HS_LegacyUnloadAttempts = 0;
        g_HS_NextLegacyUnloadAt = 0.0;
        LogMessage("[HumanSeats] Legacy standalone seat plugin unloaded; embedded seat & AFK module is now active.");
        if (HS_IsActive())
        {
            HS_UpdateSurvivorLimit();
            HS_UpdateNativeAfkGuard();
        }
    }
#else
    bool embeddedPresent = LibraryExists("tankrun_human_seats_embedded");
    if (embeddedPresent != g_HS_YieldedToOther)
    {
        g_HS_YieldedToOther = embeddedPresent;
        if (embeddedPresent)
        {
            HS_CancelAllAfkCountdowns();
            HS_RestoreNativeAfkGuard();
            HS_RestoreSurvivorLimit();
            HS_StopOperation();
            if (!g_HS_ConflictLogged)
            {
                g_HS_ConflictLogged = true;
                LogMessage("[HumanSeats] Tank Run Control Center already embeds human seats; standalone tankrun_human_seats.smx is yielding.");
            }
        }
        else
        {
            g_HS_ConflictLogged = false;
            LogMessage("[HumanSeats] Embedded seat provider removed; standalone tankrun_human_seats.smx active.");
            if (HS_IsActive())
            {
                HS_UpdateSurvivorLimit();
                HS_UpdateNativeAfkGuard();
            }
        }
    }
#endif
}

// ---------------------------------------------------------------- 生命周期
void HS_OnPluginStart()
{
    g_HS_Enabled = FindConVar("tankrun_human_seats");
    if (g_HS_Enabled == null)
    {
        g_HS_Enabled = CreateConVar("tankrun_human_seats", "1", "Use human-only survivor seats in Tank Run.", FCVAR_NOTIFY, true, 0.0, true, 1.0);
    }
    g_HS_AfkTimeout = FindConVar("tankrun_afk_timeout");
    if (g_HS_AfkTimeout == null)
    {
        g_HS_AfkTimeout = CreateConVar("tankrun_afk_timeout", "30", "AFK idle seconds before a private 10s countdown moves the player to spectator and removes their bot (0=off).", FCVAR_NOTIFY, true, 0.0, true, 3600.0);
    }
    g_HS_Mode = FindConVar("mp_gamemode");
    g_HS_SurvivorLimit = FindConVar("survivor_limit");
    g_HS_NativeAfkTimeout = FindConVar("director_afk_timeout");

    RegConsoleCmd("sm_jg", HS_Command_JoinSeat, "Join a free human survivor seat.");
    RegConsoleCmd("sm_join", HS_Command_JoinSeat, "Join a free human survivor seat.");
    AddCommandListener(HS_Cmd_JoinTeam, "jointeam");
    AddCommandListener(HS_Cmd_Spectate, "spectate");

    HookEvent("player_bot_replace", HS_Event_PlayerBotReplace, EventHookMode_Post);
    HookEvent("bot_player_replace", HS_Event_BotPlayerReplace, EventHookMode_Post);
    HookEvent("player_team", HS_Event_PlayerTeam, EventHookMode_Post);
    HookEvent("round_start_post_nav", HS_Event_RoundReady, EventHookMode_PostNoCopy);
    HookEvent("round_end", HS_Event_TransitionPause, EventHookMode_PostNoCopy);
    HookEvent("mission_lost", HS_Event_TransitionPause, EventHookMode_PostNoCopy);
    HookEvent("map_transition", HS_Event_TransitionPause, EventHookMode_PostNoCopy);
    HookEvent("finale_vehicle_leaving", HS_Event_TransitionPause, EventHookMode_PostNoCopy);

    HS_CheckPluginConflict();
#if !defined TR_EMBEDDED_HUMAN_SEATS
    if (g_HS_Late)
    {
        HS_StartMap();
    }
#endif
}

void HS_OnMapStart()
{
    HS_CheckPluginConflict();
    HS_StartMap();
}

void HS_OnConfigsExecuted()
{
    HS_CheckPluginConflict();
    if (HS_IsActive())
    {
        HS_UpdateSurvivorLimit();
        HS_UpdateNativeAfkGuard();
    }
    else
    {
        HS_RestoreNativeAfkGuard();
    }
}

void HS_OnPluginEnd()
{
    delete g_HS_Timer;
    HS_CancelAllAfkCountdowns();
    HS_StopOperation();
    HS_RestoreNativeAfkGuard();
    HS_RestoreSurvivorLimit();
}

void HS_StartMap()
{
    g_HS_Transitioning = false;
    g_HS_ReadyAt = GetGameTime() + 3.0;
    g_HS_OwnerWarning = false;
    g_HS_QueueCapacityRoster = true;
    HS_StopOperation();
    HS_CancelAllAfkCountdowns();
    float now = GetGameTime();
    for (int i = 1; i <= MaxClients; i++)
    {
        HS_ClearRequest(i);
        g_HS_EverJoined[i] = false;
        g_HS_VoluntarySpec[i] = false;
        g_HS_RetryCooldown[i] = 0.0;
        g_HS_DepartingUid[i] = 0;
        g_HS_ReplacementOwnerUid[i] = 0;
        g_HS_ReplacementBotUid[i] = 0;
        g_HS_RetiredBotUid[i] = 0;
        HS_ResetClientAfkState(i, now);
        if (HS_IsHuman(i) && GetClientTeam(i) == HS_TEAM_SURVIVOR)
        {
            g_HS_EverJoined[i] = true;
        }
    }
    delete g_HS_Timer;
    g_HS_Timer = CreateTimer(0.25, HS_Timer_Reconcile, _, TIMER_REPEAT | TIMER_FLAG_NO_MAPCHANGE);
}

void HS_OnMapEnd()
{
    g_HS_Transitioning = true;
    delete g_HS_Timer;
    HS_CancelAllAfkCountdowns();
    HS_StopOperation();
}

public void HS_Event_RoundReady(Event event, const char[] name, bool dontBroadcast)
{
    HS_StopOperation();
    HS_CancelAllAfkCountdowns();
    g_HS_Transitioning = false;
    g_HS_ReadyAt = GetGameTime() + 1.0;
    g_HS_QueueCapacityRoster = true;
    float now = GetGameTime();
    for (int i = 1; i <= MaxClients; i++)
    {
        g_HS_AfkLastActive[i] = now;
        g_HS_AfkHasAngles[i] = false;
        g_HS_AfkEvictingUid[i] = 0;
        g_HS_AfkEvictedBotUid[i] = 0;
    }
}

public void HS_Event_TransitionPause(Event event, const char[] name, bool dontBroadcast)
{
    g_HS_Transitioning = true;
    HS_CancelAllAfkCountdowns();
    HS_StopOperation();
}

// ---------------------------------------------------------------- 模式与基础判断
bool HS_IsTankRunCoop()
{
    if (g_HS_Mode == null)
    {
        return false;
    }
    char mode[64];
    g_HS_Mode.GetString(mode, sizeof(mode));
    if (StrContains(mode, "tankrun", false) == -1)
    {
        return false;
    }
    if (GetFeatureStatus(FeatureType_Native, "L4D2_IsGenericCooperativeMode") == FeatureStatus_Available)
    {
        return L4D2_IsGenericCooperativeMode();
    }
    if (StrContains(mode, "versus", false) != -1
        || StrContains(mode, "scavenge", false) != -1
        || StrContains(mode, "survival", false) != -1)
    {
        return false;
    }
    return true;
}

bool HS_IsActive()
{
    if (g_HS_YieldedToOther || g_HS_Enabled == null || !g_HS_Enabled.BoolValue)
    {
        return false;
    }
#if defined TR_EMBEDDED_HUMAN_SEATS
    if (!TR_FeatureEnabled())
    {
        return false;
    }
#endif
    return HS_IsTankRunCoop();
}

bool HS_IsHuman(int client)
{
    return client > 0 && client <= MaxClients && IsClientInGame(client) && !IsFakeClient(client)
        && !IsClientInKickQueue(client);
}

bool HS_IsSurvivorBot(int client)
{
    return client > 0 && client <= MaxClients && IsClientInGame(client) && IsFakeClient(client)
        && !IsClientSourceTV(client) && !IsClientReplay(client) && !IsClientInKickQueue(client)
        && GetClientTeam(client) == HS_TEAM_SURVIVOR;
}

bool HS_CanJoin(int client)
{
    return HS_IsHuman(client) && GetClientTeam(client) <= HS_TEAM_SPECTATOR;
}

// 缺失属性时视为未知归属（返回 -1），防止误删。
int HS_OwnerUserId(int bot)
{
    if (HasEntProp(bot, Prop_Send, "m_humanSpectatorUserID"))
    {
        return GetEntProp(bot, Prop_Send, "m_humanSpectatorUserID");
    }
    if (!g_HS_OwnerWarning)
    {
        g_HS_OwnerWarning = true;
        LogError("[HumanSeats] Cannot read survivor bot owner; skipping bots with unknown ownership.");
    }
    return -1;
}

void HS_UnbindBotOwner(int bot)
{
    if (bot <= 0 || bot > MaxClients || !IsClientInGame(bot))
    {
        return;
    }
    if (HasEntProp(bot, Prop_Send, "m_humanSpectatorUserID"))
    {
        SetEntProp(bot, Prop_Send, "m_humanSpectatorUserID", 0);
    }
    if (HasEntProp(bot, Prop_Send, "m_humanSpectatorEntIndex"))
    {
        SetEntProp(bot, Prop_Send, "m_humanSpectatorEntIndex", -1);
    }
}

bool HS_IsBotReservedByOnlineHuman(int bot, int &ownerClient)
{
    ownerClient = 0;
    int ownerUid = HS_OwnerUserId(bot);
    if (ownerUid <= 0)
    {
        return false;
    }
    int owner = GetClientOfUserId(ownerUid);
    if (owner <= 0 || owner > MaxClients || !IsClientConnected(owner) || IsFakeClient(owner))
    {
        return false;
    }
    if (g_HS_DepartingUid[owner] == ownerUid)
    {
        return false;
    }
    ownerClient = owner;
    return true;
}

int HS_FindOwnedBot(int humanUid, bool requireAlive)
{
    if (humanUid <= 0)
    {
        return 0;
    }
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!HS_IsSurvivorBot(i))
        {
            continue;
        }
        if (g_HS_RetiredBotUid[i] == GetClientUserId(i))
        {
            continue;
        }
        if (HS_OwnerUserId(i) != humanUid)
        {
            continue;
        }
        if (requireAlive && !IsPlayerAlive(i))
        {
            continue;
        }
        return i;
    }
    return 0;
}

// ---------------------------------------------------------------- 原生 director_afk_timeout 防冲突托管
void HS_UpdateNativeAfkGuard()
{
    if (g_HS_NativeAfkTimeout == null)
    {
        g_HS_NativeAfkTimeout = FindConVar("director_afk_timeout");
    }
    if (g_HS_NativeAfkTimeout == null)
    {
        return;
    }
    int configured = g_HS_AfkTimeout != null ? g_HS_AfkTimeout.IntValue : 0;
    if (!HS_IsActive() || configured <= 0)
    {
        HS_RestoreNativeAfkGuard();
        return;
    }
    // 实证：server.dll 中原生 director_afk_timeout 默认 45s；若置 0 会令 CTerrorPlayer::IsIdle() 恒返回 false
    // 并连带破坏玩家手动 go_away_from_keyboard（VA 0x102fbafa / 0x1030101e）。
    // 因此将其抬高到不小于 configured + 120（至少 180s），既阻止原生 45s 抢跑，又完整保留手动闲置功能。
    int desired = configured + 120;
    if (desired < 180)
    {
        desired = 180;
    }
    int current = g_HS_NativeAfkTimeout.IntValue;
    if (current > 0 && current < desired)
    {
        if (!g_HS_ManagedNativeAfk)
        {
            g_HS_ManagedNativeAfk = true;
            g_HS_SavedNativeAfk = current;
        }
        g_HS_AppliedNativeAfk = desired;
        g_HS_NativeAfkTimeout.SetInt(desired);
        LogMessage("[HumanSeats] Raised director_afk_timeout %d -> %d to prevent vanilla AFK preempting %ds+10s countdown (manual idle preserved).",
            g_HS_SavedNativeAfk, desired, configured);
    }
}

void HS_RestoreNativeAfkGuard()
{
    if (!g_HS_ManagedNativeAfk)
    {
        return;
    }
    g_HS_ManagedNativeAfk = false;
    if (g_HS_NativeAfkTimeout != null && g_HS_NativeAfkTimeout.IntValue == g_HS_AppliedNativeAfk)
    {
        g_HS_NativeAfkTimeout.SetInt(g_HS_SavedNativeAfk);
        LogMessage("[HumanSeats] Restored director_afk_timeout to %d.", g_HS_SavedNativeAfk);
    }
    g_HS_AppliedNativeAfk = 0;
}

// ---------------------------------------------------------------- 挂机检测、私有 10 秒倒计时与解绑踢 Bot
void HS_ResetClientAfkState(int client, float now)
{
    g_HS_AfkLastActive[client] = now;
    g_HS_AfkLastButtons[client] = 0;
    g_HS_AfkHasAngles[client] = false;
    g_HS_AfkCountdownUid[client] = 0;
    g_HS_AfkCountdownLeft[client] = 0;
    g_HS_AfkWasIdleBot[client] = false;
    g_HS_AfkEvictingUid[client] = 0;
    g_HS_AfkEvictedBotUid[client] = 0;
}

void HS_CancelAfkCountdown(int client, bool notifyClient)
{
    bool hadCountdown = (g_HS_AfkTimer[client] != null || g_HS_AfkCountdownUid[client] != 0);
    if (g_HS_AfkTimer[client] != null)
    {
        KillTimer(g_HS_AfkTimer[client]);
        g_HS_AfkTimer[client] = null;
    }
    g_HS_AfkCountdownUid[client] = 0;
    g_HS_AfkCountdownLeft[client] = 0;
    g_HS_AfkWasIdleBot[client] = false;

    if (notifyClient && hadCountdown && HS_IsHuman(client))
    {
        PrintCenterText(client, "已检测到操作，挂机倒计时已取消。");
    }
}

void HS_CancelAllAfkCountdowns()
{
    for (int i = 1; i <= MaxClients; i++)
    {
        HS_CancelAfkCountdown(i, false);
        g_HS_AfkEvictingUid[i] = 0;
        g_HS_AfkEvictedBotUid[i] = 0;
    }
}

void HS_OnClientActivity(int client, bool notifyIfCancelled)
{
    if (!HS_IsHuman(client))
    {
        return;
    }
    g_HS_AfkLastActive[client] = GetGameTime();
    if (g_HS_AfkTimer[client] != null || g_HS_AfkCountdownUid[client] != 0)
    {
        HS_CancelAfkCountdown(client, notifyIfCancelled);
    }
}

void HS_OnPlayerRunCmd(int client, int buttons, int impulse, const float angles[3])
{
    if (client <= 0 || client > MaxClients || IsFakeClient(client) || !IsClientInGame(client))
    {
        return;
    }
    if (!HS_IsActive() || g_HS_AfkTimeout == null || g_HS_AfkTimeout.IntValue <= 0)
    {
        return;
    }

    int team = GetClientTeam(client);
    int prevButtons = g_HS_AfkLastButtons[client];
    g_HS_AfkLastButtons[client] = buttons;

    if (team == HS_TEAM_SURVIVOR && IsPlayerAlive(client))
    {
        bool hasAction = ((buttons & HS_AFK_ACTIVE_BUTTONS) != 0) || impulse != 0;
        if (!hasAction && g_HS_AfkHasAngles[client])
        {
            float pitchDelta = FloatAbs(angles[0] - g_HS_AfkLastAngles[client][0]);
            float yawDelta = FloatAbs(angles[1] - g_HS_AfkLastAngles[client][1]);
            if (yawDelta > 180.0)
            {
                yawDelta = 360.0 - yawDelta;
            }
            if (pitchDelta > 1.5 || yawDelta > 1.5)
            {
                hasAction = true;
            }
        }
        g_HS_AfkLastAngles[client][0] = angles[0];
        g_HS_AfkLastAngles[client][1] = angles[1];
        g_HS_AfkLastAngles[client][2] = angles[2];
        g_HS_AfkHasAngles[client] = true;

        if (hasAction)
        {
            HS_OnClientActivity(client, true);
        }
        return;
    }

    if (team == HS_TEAM_SPECTATOR)
    {
        // 旁观/Idle 模式下镜头视角可能随被观察的 Bot 自动旋转，绝不用 angles 重置计时，
        // 仅认明确的新按键操作或 impulse（且仅当该玩家处于 Idle 拥有替身时生效）。
        int newlyPressed = (buttons & ~prevButtons) & HS_AFK_ACTIVE_BUTTONS;
        if (newlyPressed != 0 || impulse != 0)
        {
            int uid = GetClientUserId(client);
            if (g_HS_AfkCountdownUid[client] == uid || HS_FindOwnedBot(uid, false) > 0)
            {
                HS_OnClientActivity(client, true);
            }
        }
    }
}

bool HS_IsClientExemptFromAfk(int client)
{
    if (g_HS_Transitioning || GetGameTime() < g_HS_ReadyAt)
    {
        return true;
    }
    if (GetFeatureStatus(FeatureType_Native, "L4D_IsInIntro") == FeatureStatus_Available && L4D_IsInIntro() != 0)
    {
        return true;
    }
    if (GetFeatureStatus(FeatureType_Native, "L4D_IsFinaleEscapeInProgress") == FeatureStatus_Available
        && L4D_IsFinaleEscapeInProgress())
    {
        return true;
    }
    if (GetClientTeam(client) == HS_TEAM_SURVIVOR)
    {
        if (!IsPlayerAlive(client))
        {
            // 已死亡等待救援室复活的幸存者无法控制角色，不误计挂机。
            return true;
        }
        if ((GetEntityFlags(client) & (FL_FROZEN | FL_ATCONTROLS)) != 0
            || GetEntityMoveType(client) == MOVETYPE_NONE)
        {
            return true;
        }
    }
    return false;
}

void HS_ShowPrivateAfkCountdown(int client, int secondsLeft, bool isIdleBot)
{
    if (!HS_IsHuman(client))
    {
        return;
    }
    if (isIdleBot)
    {
        PrintCenterText(client, "长时间闲置提醒：%d 秒后将解除替身并移除 Bot（按左键或输入 !jg 取回控制即可取消）", secondsLeft);
        if (secondsLeft == HS_AFK_COUNTDOWN_SECONDS || secondsLeft <= 3)
        {
            PrintHintText(client, "闲置倒计时 %d 秒：按左键或输入 !jg 取回控制即可取消", secondsLeft);
        }
    }
    else
    {
        PrintCenterText(client, "挂机提醒：%d 秒后将转入旁观并移除替身 Bot（移动/转动视角/按键即可取消）", secondsLeft);
        if (secondsLeft == HS_AFK_COUNTDOWN_SECONDS || secondsLeft <= 3)
        {
            PrintHintText(client, "挂机倒计时 %d 秒：任意操作即可取消", secondsLeft);
        }
    }
}

void HS_StartAfkCountdown(int client, bool isIdleBot)
{
    if (!HS_IsHuman(client))
    {
        return;
    }
    int uid = GetClientUserId(client);
    if (uid <= 0)
    {
        return;
    }
    HS_CancelAfkCountdown(client, false);
    g_HS_AfkCountdownUid[client] = uid;
    g_HS_AfkCountdownLeft[client] = HS_AFK_COUNTDOWN_SECONDS;
    g_HS_AfkWasIdleBot[client] = isIdleBot;

    HS_ShowPrivateAfkCountdown(client, HS_AFK_COUNTDOWN_SECONDS, isIdleBot);
    g_HS_AfkTimer[client] = CreateTimer(1.0, HS_Timer_AfkCountdown, uid, TIMER_REPEAT | TIMER_FLAG_NO_MAPCHANGE);
    LogMessage("[HumanSeats] Started private %ds AFK countdown for player userid=%d (idleBot=%d).",
        HS_AFK_COUNTDOWN_SECONDS, uid, isIdleBot ? 1 : 0);
}

bool HS_IsBotPendingAfkEviction(int botUid)
{
    if (botUid <= 0)
    {
        return false;
    }
    for (int i = 1; i <= MaxClients; i++)
    {
        if (g_HS_AfkEvictingUid[i] != 0 && g_HS_AfkEvictedBotUid[i] == botUid)
        {
            return true;
        }
    }
    return false;
}

// 在踢除最后存活幸存者 Bot 前清理场上已死亡的无主 Bot，避免残留死 Bot 令
// CDirector::CheckForAllSurvivorsDead (totalSurvivors != 0 && standingSurvivors == 0) 误判团灭。
void HS_PurgeDeadUnownedBots(int excludeBot)
{
    for (int i = 1; i <= MaxClients; i++)
    {
        if (i == excludeBot || !HS_IsSurvivorBot(i) || IsPlayerAlive(i))
        {
            continue;
        }
        int botUid = GetClientUserId(i);
        if (botUid == g_HS_BotUid)
        {
            continue;
        }
        int ownerUid = HS_OwnerUserId(i);
        if (ownerUid < 0)
        {
            continue;
        }
        int ownerClient = 0;
        if (HS_IsBotReservedByOnlineHuman(i, ownerClient))
        {
            continue;
        }
        HS_UnbindBotOwner(i);
        g_HS_RetiredBotUid[i] = botUid;
        LogMessage("[HumanSeats] Purging dead unowned survivor bot before seat removal: bot userid=%d.", botUid);
        KickClient(i, "Unused dead survivor seat");
    }
}

void HS_RemoveAfkTargetBot(int bot, int humanUid, const char[] reason)
{
    if (!HS_IsSurvivorBot(bot))
    {
        return;
    }
    int botUid = GetClientUserId(bot);
    if (botUid == g_HS_BotUid)
    {
        LogMessage("[HumanSeats] Skipped bot removal for AFK player userid=%d: bot #%d (userid=%d) is in an active join handoff.",
            humanUid, bot, botUid);
        return;
    }
    int owner = HS_OwnerUserId(bot);
    // 严禁踢其他在线真人的 Bot；仅允许移除归属为 0 或当前挂机玩家本人的对应 Bot。
    if (owner < 0 || (owner != 0 && owner != humanUid))
    {
        LogMessage("[HumanSeats] Skipped bot removal for AFK player userid=%d: bot #%d belongs to userid=%d.",
            humanUid, bot, owner);
        return;
    }
    HS_PurgeDeadUnownedBots(bot);
    HS_UnbindBotOwner(bot);
    g_HS_RetiredBotUid[bot] = botUid;
    LogMessage("[HumanSeats] Removing AFK player's survivor bot (%s): bot userid=%d, player userid=%d.",
        reason, botUid, humanUid);
    KickClient(bot, "AFK survivor removed");
}

public void HS_Frame_FinalizeAfkEviction(any userid)
{
    int client = GetClientOfUserId(userid);
    if (client <= 0 || client > MaxClients)
    {
        return;
    }
    if (g_HS_AfkEvictingUid[client] != userid)
    {
        return;
    }
    int botUid = g_HS_AfkEvictedBotUid[client];
    g_HS_AfkEvictingUid[client] = 0;
    g_HS_AfkEvictedBotUid[client] = 0;

    if (!HS_IsActive() || g_HS_Transitioning)
    {
        return;
    }
    int bot = botUid > 0 ? GetClientOfUserId(botUid) : HS_FindOwnedBot(userid, false);
    if (bot > 0 && HS_IsSurvivorBot(bot))
    {
        HS_RemoveAfkTargetBot(bot, userid, "afk timeout (next-frame)");
    }
    else
    {
        LogMessage("[HumanSeats] AFK player userid=%d moved to spectator; no confirmed owned survivor bot remained to kick.", userid);
    }
}

void HS_ExecuteAfkEviction(int client, int humanUid)
{
    if (!HS_IsHuman(client) || GetClientUserId(client) != humanUid || !HS_IsActive() || g_HS_Transitioning)
    {
        return;
    }
    if (HS_IsClientExemptFromAfk(client))
    {
        HS_OnClientActivity(client, false);
        return;
    }

    int team = GetClientTeam(client);
    int ownedBot = HS_FindOwnedBot(humanUid, false);
    if (team != HS_TEAM_SURVIVOR && !(team == HS_TEAM_SPECTATOR && ownedBot > 0))
    {
        return;
    }

    g_HS_VoluntarySpec[client] = true;
    HS_ClearRequest(client);
    if (g_HS_HumanUid == humanUid)
    {
        HS_StopOperation();
    }
    g_HS_StatAfkEvictions++;

#if defined TR_EMBEDDED_HUMAN_SEATS
    // 防最后一人挂机转旁观时被任何外部因素误触发团灭快进。
    TR_FlowSuppressWipe(3.0);
#endif

    if (team == HS_TEAM_SPECTATOR && ownedBot > 0)
    {
        HS_RemoveAfkTargetBot(ownedBot, humanUid, "idle timeout");
        PrintCenterText(client, "你因长时间闲置已解除替身并转为普通旁观，输入 !jg 或 !join 可重新加入。");
        PrintToChat(client, "[HumanSeats] 你因长时间闲置已转为普通旁观并清理对应替身，输入 !jg 或 !join 可重新加入。");
        return;
    }

    // 玩家仍在幸存者队伍：记录本次转旁观事件，捕捉 ChangeClientTeam 同步产生的对应 Bot 后精准解绑与移除。
    g_HS_AfkEvictingUid[client] = humanUid;
    g_HS_AfkEvictedBotUid[client] = 0;
    ChangeClientTeam(client, HS_TEAM_SPECTATOR);

    int bot = g_HS_AfkEvictedBotUid[client] > 0
        ? GetClientOfUserId(g_HS_AfkEvictedBotUid[client])
        : HS_FindOwnedBot(humanUid, false);
    if (bot > 0 && HS_IsSurvivorBot(bot))
    {
        g_HS_AfkEvictingUid[client] = 0;
        g_HS_AfkEvictedBotUid[client] = 0;
        HS_RemoveAfkTargetBot(bot, humanUid, "afk timeout");
    }
    else
    {
        // 若引擎在同帧稍后才发出 player_bot_replace，推迟至次帧按确认归属精准清理，绝不盲踢其他槽位。
        RequestFrame(HS_Frame_FinalizeAfkEviction, humanUid);
    }

    if (HS_IsHuman(client))
    {
        PrintCenterText(client, "你因长时间挂机已转入旁观并清理对应 Bot，输入 !jg 或 !join 可重新加入。");
        PrintToChat(client, "[HumanSeats] 你因长时间挂机已转入旁观并清理对应 Bot，输入 !jg 或 !join 可重新加入。");
    }
}

public Action HS_Timer_AfkCountdown(Handle timer, any userid)
{
    int client = GetClientOfUserId(userid);
    if (client <= 0 || client > MaxClients || g_HS_AfkTimer[client] != timer)
    {
        return Plugin_Stop;
    }
    if (!HS_IsHuman(client) || g_HS_AfkCountdownUid[client] != userid || !HS_IsActive() || g_HS_Transitioning)
    {
        g_HS_AfkTimer[client] = null;
        HS_CancelAfkCountdown(client, false);
        return Plugin_Stop;
    }
    if (g_HS_AfkTimeout == null || g_HS_AfkTimeout.IntValue <= 0 || HS_IsClientExemptFromAfk(client))
    {
        g_HS_AfkTimer[client] = null;
        HS_CancelAfkCountdown(client, false);
        g_HS_AfkLastActive[client] = GetGameTime();
        return Plugin_Stop;
    }

    int team = GetClientTeam(client);
    int ownedBot = (team == HS_TEAM_SPECTATOR) ? HS_FindOwnedBot(userid, false) : 0;
    if (team != HS_TEAM_SURVIVOR && ownedBot == 0)
    {
        g_HS_AfkTimer[client] = null;
        HS_CancelAfkCountdown(client, false);
        return Plugin_Stop;
    }

    g_HS_AfkCountdownLeft[client]--;
    if (g_HS_AfkCountdownLeft[client] > 0)
    {
        HS_ShowPrivateAfkCountdown(client, g_HS_AfkCountdownLeft[client], ownedBot > 0);
        return Plugin_Continue;
    }

    g_HS_AfkTimer[client] = null;
    g_HS_AfkCountdownUid[client] = 0;
    g_HS_AfkCountdownLeft[client] = 0;
    g_HS_AfkWasIdleBot[client] = false;
    HS_ExecuteAfkEviction(client, userid);
    return Plugin_Stop;
}

void HS_CheckAfkPlayers()
{
    HS_UpdateNativeAfkGuard();
    int timeout = g_HS_AfkTimeout != null ? g_HS_AfkTimeout.IntValue : 0;
    if (timeout <= 0)
    {
        HS_CancelAllAfkCountdowns();
        return;
    }
    float now = GetGameTime();
    float threshold = float(timeout);
    for (int client = 1; client <= MaxClients; client++)
    {
        if (!HS_IsHuman(client))
        {
            HS_CancelAfkCountdown(client, false);
            continue;
        }
        int uid = GetClientUserId(client);
        int team = GetClientTeam(client);
        int ownedBot = (team == HS_TEAM_SPECTATOR) ? HS_FindOwnedBot(uid, false) : 0;
        bool monitored = (team == HS_TEAM_SURVIVOR) || (team == HS_TEAM_SPECTATOR && ownedBot > 0);
        if (!monitored)
        {
            // 主动普通旁观者不参与挂机倒计时。
            HS_CancelAfkCountdown(client, false);
            g_HS_AfkLastActive[client] = now;
            continue;
        }
        if (HS_IsClientExemptFromAfk(client))
        {
            HS_CancelAfkCountdown(client, false);
            g_HS_AfkLastActive[client] = now;
            continue;
        }
        if (g_HS_AfkLastActive[client] <= 0.0)
        {
            g_HS_AfkLastActive[client] = now;
            continue;
        }
        if (g_HS_AfkTimer[client] == null && (now - g_HS_AfkLastActive[client]) >= threshold)
        {
            HS_StartAfkCountdown(client, ownedBot > 0);
        }
    }
}

// ---------------------------------------------------------------- 容量上限联动
void HS_UpdateSurvivorLimit()
{
    if (g_HS_CapacityProfile == null)
    {
        g_HS_CapacityProfile = FindConVar("tankrun_capacity_enabled");
    }
    if (g_HS_CapacityProfile == null || !g_HS_CapacityProfile.BoolValue)
    {
        HS_RestoreSurvivorLimit();
        return;
    }
    bool justEnabled = !g_HS_CapacityActive;
    if (justEnabled)
    {
        g_HS_CapacityActive = true;
        g_HS_QueueCapacityRoster = true;
        if (g_HS_SurvivorLimit != null)
        {
            g_HS_PreviousSurvivorLimit = g_HS_SurvivorLimit.IntValue;
            g_HS_HadSurvivorUpperBound = g_HS_SurvivorLimit.GetBounds(ConVarBound_Upper, g_HS_PreviousSurvivorUpperBound);
            g_HS_RaisedSurvivorUpperBound = g_HS_HadSurvivorUpperBound && g_HS_PreviousSurvivorUpperBound < float(HS_EXPANDED_SURVIVORS);
            if (g_HS_RaisedSurvivorUpperBound)
            {
                g_HS_SurvivorLimit.SetBounds(ConVarBound_Upper, true, float(HS_EXPANDED_SURVIVORS));
            }
        }
    }
    if (g_HS_SurvivorLimit != null && g_HS_SurvivorLimit.IntValue != HS_EXPANDED_SURVIVORS)
    {
        g_HS_SurvivorLimit.SetInt(HS_EXPANDED_SURVIVORS);
    }
    if (justEnabled)
    {
        LogMessage("[HumanSeats] Capacity profile enabled: %d human survivor seats; engine survivor_limit=%d.",
            HS_EXPANDED_SURVIVORS, g_HS_SurvivorLimit == null ? -1 : g_HS_SurvivorLimit.IntValue);
    }
}

void HS_RestoreSurvivorLimit()
{
    if (!g_HS_CapacityActive)
    {
        return;
    }
    g_HS_CapacityActive = false;
    if (g_HS_SurvivorLimit == null)
    {
        return;
    }
    if (g_HS_RaisedSurvivorUpperBound)
    {
        g_HS_SurvivorLimit.SetBounds(ConVarBound_Upper, g_HS_HadSurvivorUpperBound, g_HS_PreviousSurvivorUpperBound);
        g_HS_RaisedSurvivorUpperBound = false;
    }
    if (g_HS_SurvivorLimit.IntValue == HS_EXPANDED_SURVIVORS)
    {
        g_HS_SurvivorLimit.SetInt(g_HS_PreviousSurvivorLimit);
    }
}

// ---------------------------------------------------------------- 请求队列与退服清理
void HS_ClearRequest(int client)
{
    HS_JoinRequest empty;
    g_HS_Requests[client] = empty;
}

void HS_QueueJoin(int client, int resumeBot, bool explicitRequest)
{
    if (!HS_CanJoin(client))
    {
        return;
    }
    if (explicitRequest)
    {
        g_HS_VoluntarySpec[client] = false;
        g_HS_RetryCooldown[client] = 0.0;
        HS_OnClientActivity(client, true);
    }
    float now = GetGameTime();
    if (g_HS_Requests[client].userid != GetClientUserId(client) || g_HS_Requests[client].requestedAt <= 0.0)
    {
        g_HS_Requests[client].requestedAt = now;
    }
    g_HS_Requests[client].userid = GetClientUserId(client);
    g_HS_Requests[client].resumeBot = resumeBot;
    g_HS_Requests[client].deadline = now + (resumeBot > 0 ? 5.0 : 20.0);
}

int HS_CountConnectedHumans(int excludeClient)
{
    int count = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (i == excludeClient || !IsClientConnected(i) || IsFakeClient(i))
        {
            continue;
        }
        if (g_HS_DepartingUid[i] == GetClientUserId(i))
        {
            continue;
        }
        count++;
    }
    return count;
}

void HS_RetireReplacement(int bot, int botUid, int ownerUid)
{
    if (botUid <= 0 || g_HS_Transitioning)
    {
        return;
    }
    if (botUid == g_HS_BotUid)
    {
        return;
    }
    if (!HS_IsSurvivorBot(bot) || GetClientUserId(bot) != botUid)
    {
        return;
    }
    int owner = HS_OwnerUserId(bot);
    if (owner != 0 && owner != ownerUid)
    {
        return;
    }
    HS_PurgeDeadUnownedBots(bot);
    HS_UnbindBotOwner(bot);
    g_HS_RetiredBotUid[bot] = botUid;
    LogMessage("[HumanSeats] Removing departing player's survivor bot: bot userid=%d, player userid=%d.", botUid, ownerUid);
    KickClient(bot, "Player left the server");
}

void HS_OnClientPostAdminCheck(int client)
{
    HS_CancelAfkCountdown(client, false);
    HS_ResetClientAfkState(client, GetGameTime());
    g_HS_EverJoined[client] = false;
    g_HS_VoluntarySpec[client] = false;
    g_HS_RetryCooldown[client] = 0.0;
    g_HS_DepartingUid[client] = 0;

    if (!HS_IsActive() || g_HS_Transitioning)
    {
        return;
    }
    if (HS_IsHuman(client) && GetClientTeam(client) == HS_TEAM_SURVIVOR)
    {
        g_HS_EverJoined[client] = true;
        return;
    }
    if (HS_CanJoin(client))
    {
        HS_QueueJoin(client, 0, false);
    }
}

void HS_OnClientDisconnect(int client)
{
    HS_CancelAfkCountdown(client, false);
    HS_ResetClientAfkState(client, 0.0);
    HS_ClearRequest(client);
    g_HS_EverJoined[client] = false;
    g_HS_VoluntarySpec[client] = false;
    g_HS_RetryCooldown[client] = 0.0;
    if (IsFakeClient(client) || !HS_IsActive())
    {
        return;
    }
    int uid = GetClientUserId(client);
    if (uid <= 0)
    {
        return;
    }
    g_HS_DepartingUid[client] = uid;
    // player_bot_replace 可能早于 OnClientDisconnect 触发。
    for (int bot = 1; bot <= MaxClients; bot++)
    {
        if (g_HS_ReplacementOwnerUid[bot] == uid)
        {
            HS_RetireReplacement(bot, g_HS_ReplacementBotUid[bot], uid);
        }
    }
}

// ---------------------------------------------------------------- 玩家命令与事件
public Action HS_Command_JoinSeat(int client, int args)
{
    if (client <= 0 || client > MaxClients)
    {
        return Plugin_Handled;
    }
    if (!HS_IsActive())
    {
#if !defined TR_EMBEDDED_HUMAN_SEATS
        ReplyToCommand(client, "[HumanSeats] Survivor seat handoff is active only in Tank Run coop.");
#endif
        return Plugin_Continue;
    }
    HS_OnClientActivity(client, true);
    if (!HS_CanJoin(client))
    {
        ReplyToCommand(client, "[HumanSeats] You are already on a playable team.");
        return Plugin_Handled;
    }
    if (g_HS_Transitioning)
    {
        ReplyToCommand(client, "[HumanSeats] Map transition or round end in progress; please retry after round start.");
        return Plugin_Handled;
    }

    int uid = GetClientUserId(client);
    int ownedAlive = HS_FindOwnedBot(uid, true);
    if (ownedAlive > 0)
    {
        HS_QueueJoin(client, GetClientUserId(ownedAlive), true);
        ReplyToCommand(client, "[HumanSeats] Resuming your reserved survivor bot.");
        return Plugin_Handled;
    }
    int ownedDead = HS_FindOwnedBot(uid, false);
    if (ownedDead > 0)
    {
        ReplyToCommand(client, "[HumanSeats] Your reserved survivor is currently dead; wait for rescue or round restart.");
        return Plugin_Handled;
    }
    if (!HS_HasCapacity(uid))
    {
        ReplyToCommand(client, "[HumanSeats] All survivor seats are currently occupied.");
        return Plugin_Handled;
    }

    HS_QueueJoin(client, 0, true);
    ReplyToCommand(client, "[HumanSeats] Survivor seat requested.");
    return Plugin_Handled;
}

public Action HS_Cmd_JoinTeam(int client, const char[] command, int argc)
{
    if (!HS_IsActive() || !HS_IsHuman(client) || g_HS_Transitioning || argc < 1)
    {
        return Plugin_Continue;
    }
    char arg[32];
    GetCmdArg(1, arg, sizeof(arg));
    TrimString(arg);

    if (StrEqual(arg, "1") || StrEqual(arg, "spectator", false) || StrEqual(arg, "spec", false))
    {
        g_HS_VoluntarySpec[client] = true;
        HS_ClearRequest(client);
        HS_CancelAfkCountdown(client, false);
        return Plugin_Continue;
    }
    if ((StrEqual(arg, "2") || StrEqual(arg, "survivor", false) || StrEqual(arg, "survivors", false))
        && HS_CanJoin(client))
    {
        HS_OnClientActivity(client, true);
        int uid = GetClientUserId(client);
        int ownedAlive = HS_FindOwnedBot(uid, true);
        if (ownedAlive > 0)
        {
            HS_QueueJoin(client, GetClientUserId(ownedAlive), true);
        }
        else if (HS_FindOwnedBot(uid, false) == 0)
        {
            if (HS_HasCapacity(uid))
            {
                HS_QueueJoin(client, 0, true);
            }
            else
            {
                PrintToChat(client, "[HumanSeats] All survivor seats are currently occupied.");
                return Plugin_Handled;
            }
        }
    }
    return Plugin_Continue;
}

public Action HS_Cmd_Spectate(int client, const char[] command, int argc)
{
    if (HS_IsActive() && HS_IsHuman(client))
    {
        g_HS_VoluntarySpec[client] = true;
        HS_ClearRequest(client);
        HS_CancelAfkCountdown(client, false);
    }
    return Plugin_Continue;
}

public void HS_Event_PlayerTeam(Event event, const char[] name, bool dontBroadcast)
{
    if (!HS_IsActive())
    {
        return;
    }
    int client = GetClientOfUserId(event.GetInt("userid"));
    if (!HS_IsHuman(client))
    {
        return;
    }
    int newTeam = event.GetInt("team");
    int oldTeam = event.GetInt("oldteam");
    bool disconnect = event.GetBool("disconnect");
    if (disconnect)
    {
        return;
    }
    if (newTeam == HS_TEAM_SURVIVOR)
    {
        g_HS_EverJoined[client] = true;
        g_HS_VoluntarySpec[client] = false;
        HS_ClearRequest(client);
        HS_OnClientActivity(client, true);
    }
    else if (newTeam == HS_TEAM_SPECTATOR && oldTeam == HS_TEAM_SURVIVOR)
    {
        // 玩家从幸存者主动切旁观、进入游戏闲置或因挂机被转旁观：绝不自动拉回。
        g_HS_VoluntarySpec[client] = true;
        HS_ClearRequest(client);
        if (g_HS_AfkEvictingUid[client] != GetClientUserId(client))
        {
            // 若玩家是主动按“闲置（Idle）”，从进入闲置时刻开始重新计算闲置挂机时长。
            HS_CancelAfkCountdown(client, false);
            g_HS_AfkLastActive[client] = GetGameTime();
        }
    }
}

public void HS_Event_PlayerBotReplace(Event event, const char[] name, bool dontBroadcast)
{
    if (!HS_IsActive())
    {
        return;
    }
    int humanUid = event.GetInt("player");
    int botUid = event.GetInt("bot");
    int human = GetClientOfUserId(humanUid);
    int bot = GetClientOfUserId(botUid);
    if (humanUid <= 0 || bot <= 0)
    {
        return;
    }
    g_HS_ReplacementOwnerUid[bot] = humanUid;
    g_HS_ReplacementBotUid[bot] = botUid;

    if (human == 0 || g_HS_DepartingUid[human] == humanUid)
    {
        HS_RetireReplacement(bot, botUid, humanUid);
        return;
    }
    // 若正处于本插件挂机倒计时归零的转旁观过程，精确记录本次离队产生的对应 Bot userid。
    if (g_HS_AfkEvictingUid[human] == humanUid)
    {
        g_HS_AfkEvictedBotUid[human] = botUid;
        return;
    }
    // 玩家主动进入游戏“闲置（Idle）”状态：保留该替身 Bot 直到其归来或挂机倒计时结束并安全解绑，绝不强拉回来。
    if (HS_IsHuman(human))
    {
        g_HS_VoluntarySpec[human] = true;
        HS_ClearRequest(human);
        g_HS_AfkLastActive[human] = GetGameTime();
        HS_CancelAfkCountdown(human, false);
    }
}

public void HS_Event_BotPlayerReplace(Event event, const char[] name, bool dontBroadcast)
{
    int botUid = event.GetInt("bot");
    int playerUid = event.GetInt("player");
    int human = GetClientOfUserId(playerUid);
    if (HS_IsHuman(human))
    {
        g_HS_EverJoined[human] = true;
        g_HS_VoluntarySpec[human] = false;
        HS_OnClientActivity(human, true);
    }
    if (g_HS_BotUid > 0 && botUid == g_HS_BotUid && playerUid == g_HS_HumanUid)
    {
        g_HS_ReplacementConfirmed = true;
    }
}

// ---------------------------------------------------------------- 席位上限与选择
bool HS_RequestIsCurrent(int client)
{
    return HS_CanJoin(client)
        && !g_HS_VoluntarySpec[client]
        && g_HS_Requests[client].userid == GetClientUserId(client)
        && g_HS_Requests[client].deadline > GetGameTime()
        && g_HS_RetryCooldown[client] <= GetGameTime();
}

int HS_GetSeatLimit()
{
    int maximum = 4;
    if (g_HS_CapacityActive)
    {
        maximum = HS_EXPANDED_SURVIVORS;
    }
    else if (g_HS_SurvivorLimit != null && g_HS_SurvivorLimit.IntValue > 0)
    {
        maximum = g_HS_SurvivorLimit.IntValue;
    }
    return maximum;
}

int HS_CountOccupiedSeats(int &humanSurvivors, int &reservedAfkBots)
{
    humanSurvivors = 0;
    reservedAfkBots = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (HS_IsHuman(i) && GetClientTeam(i) == HS_TEAM_SURVIVOR)
        {
            humanSurvivors++;
        }
        else if (HS_IsSurvivorBot(i))
        {
            int ownerClient = 0;
            if (HS_IsBotReservedByOnlineHuman(i, ownerClient) && GetClientTeam(ownerClient) != HS_TEAM_SURVIVOR)
            {
                reservedAfkBots++;
            }
        }
    }
    return humanSurvivors + reservedAfkBots;
}

bool HS_HasCapacity(int forHumanUid)
{
    if (forHumanUid > 0 && HS_FindOwnedBot(forHumanUid, true) > 0)
    {
        return true;
    }
    int humans = 0;
    int afkBots = 0;
    int occupied = HS_CountOccupiedSeats(humans, afkBots);
    return occupied < HS_GetSeatLimit();
}

void HS_StopOperation()
{
    g_HS_HumanUid = 0;
    g_HS_BotUid = 0;
    g_HS_CreationRequested = false;
    g_HS_ReplacementConfirmed = false;
    g_HS_OperationDeadline = 0.0;
    g_HS_VerifyAt = 0.0;
}

bool HS_BotCanBeAssigned(int bot, int humanUid)
{
    if (!HS_IsSurvivorBot(bot) || !IsPlayerAlive(bot))
    {
        return false;
    }
    int botUid = GetClientUserId(bot);
    if (g_HS_RetiredBotUid[bot] == botUid || HS_IsBotPendingAfkEviction(botUid))
    {
        return false;
    }
    int ownerUid = HS_OwnerUserId(bot);
    if (ownerUid < 0)
    {
        return false;
    }
    if (ownerUid == humanUid || ownerUid == 0)
    {
        return true;
    }
    int ownerClient = 0;
    return !HS_IsBotReservedByOnlineHuman(bot, ownerClient);
}

int HS_FindSeat(int human)
{
    int humanUid = GetClientUserId(human);
    int resumeUid = g_HS_Requests[human].resumeBot;
    if (resumeUid != 0)
    {
        int bot = GetClientOfUserId(resumeUid);
        return HS_BotCanBeAssigned(bot, humanUid) && HS_OwnerUserId(bot) == humanUid ? bot : 0;
    }
    int ownedAlive = HS_FindOwnedBot(humanUid, true);
    if (ownedAlive > 0 && HS_BotCanBeAssigned(ownedAlive, humanUid))
    {
        return ownedAlive;
    }
    int available = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!HS_BotCanBeAssigned(i, humanUid))
        {
            continue;
        }
        if (!g_HS_CreationRequested || g_HS_BotBeforeCreate[i] != GetClientUserId(i))
        {
            available = i;
            break;
        }
    }
    return available;
}

int HS_ChooseCharacter()
{
    int offset = L4D2_GetSurvivorSetMod() == 1 ? 4 : 0;
    int occupied = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsClientInGame(i) && GetClientTeam(i) == HS_TEAM_SURVIVOR
            && HasEntProp(i, Prop_Send, "m_survivorCharacter"))
        {
            int relative = GetEntProp(i, Prop_Send, "m_survivorCharacter") - offset;
            if (relative >= 0 && relative < 4)
            {
                occupied |= 1 << relative;
            }
        }
    }
    for (int slot = 0; slot < 4; slot++)
    {
        if ((occupied & (1 << slot)) == 0)
        {
            return slot;
        }
    }
    return g_HS_CapacityActive ? GetRandomInt(0, 3) : SurvivorCharacter_First;
}

void HS_RequestBot()
{
    for (int i = 1; i <= MaxClients; i++)
    {
        g_HS_BotBeforeCreate[i] = HS_IsSurvivorBot(i) ? GetClientUserId(i) : 0;
    }
    g_HS_CreationRequested = true;
    // L4D2Direct_AddSurvivorBot 直接走 CDirector::AddSurvivorBot，不要求场上已有存活幸存者。
    L4D2Direct_AddSurvivorBot(HS_ChooseCharacter());
}

void HS_FinishJoin(int human, bool success)
{
    if (HS_IsHuman(human) && GetClientUserId(human) == g_HS_HumanUid)
    {
        HS_ClearRequest(human);
        if (success)
        {
            g_HS_EverJoined[human] = true;
            g_HS_VoluntarySpec[human] = false;
            HS_OnClientActivity(human, false);
            if (g_HS_ReplacementConfirmed)
            {
                LogMessage("[HumanSeats] Survivor handoff confirmed: player userid=%d, bot userid=%d.", g_HS_HumanUid, g_HS_BotUid);
            }
            else if (g_HS_BotUid != 0)
            {
                LogMessage("[HumanSeats] Player reached survivors: player userid=%d, requested bot userid=%d.", g_HS_HumanUid, g_HS_BotUid);
            }
        }
        else
        {
            g_HS_RetryCooldown[human] = GetGameTime() + 1.0;
            if (GetClientTeam(human) == HS_TEAM_UNASSIGNED)
            {
                ChangeClientTeam(human, HS_TEAM_SPECTATOR);
            }
            PrintToChat(human, "[HumanSeats] No survivor seat could be assigned right now. Try !jg or !join.");
            LogMessage("[HumanSeats] Survivor handoff expired: player userid=%d, bot userid=%d.", g_HS_HumanUid, g_HS_BotUid);
        }
    }
    HS_StopOperation();
}

int HS_SelectNextRequester()
{
    int bestClient = 0;
    float bestTime = 0.0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!HS_RequestIsCurrent(i))
        {
            continue;
        }
        int uid = g_HS_Requests[i].userid;
        if (!HS_HasCapacity(uid))
        {
            continue;
        }
        if (bestClient == 0 || g_HS_Requests[i].requestedAt < bestTime)
        {
            bestClient = i;
            bestTime = g_HS_Requests[i].requestedAt;
        }
    }
    return bestClient;
}

void HS_ReconcileJoin()
{
    if (g_HS_Transitioning)
    {
        HS_StopOperation();
        return;
    }
    if (g_HS_HumanUid == 0)
    {
        int nextClient = HS_SelectNextRequester();
        if (nextClient <= 0)
        {
            return;
        }
        g_HS_HumanUid = g_HS_Requests[nextClient].userid;
        g_HS_OperationDeadline = GetGameTime() + 2.5;
    }

    int human = GetClientOfUserId(g_HS_HumanUid);
    if (!HS_IsHuman(human))
    {
        HS_StopOperation();
        return;
    }
    if (GetClientTeam(human) == HS_TEAM_SURVIVOR)
    {
        HS_FinishJoin(human, true);
        return;
    }
    if (!HS_RequestIsCurrent(human) || GetGameTime() >= g_HS_OperationDeadline)
    {
        HS_FinishJoin(human, false);
        return;
    }
    if (g_HS_VerifyAt > 0.0)
    {
        if (GetGameTime() >= g_HS_VerifyAt)
        {
            HS_FinishJoin(human, false);
        }
        return;
    }

    if (HS_FindOwnedBot(g_HS_HumanUid, true) == 0 && HS_FindOwnedBot(g_HS_HumanUid, false) > 0)
    {
        HS_ClearRequest(human);
        HS_StopOperation();
        return;
    }

    int bot = HS_FindSeat(human);
    if (bot == 0 && !g_HS_CreationRequested && g_HS_Requests[human].resumeBot == 0)
    {
        if (!HS_HasCapacity(g_HS_HumanUid) || GetClientCount(false) >= MaxClients)
        {
            return;
        }
        HS_RequestBot();
        bot = HS_FindSeat(human);
    }
    if (bot == 0 || !HS_RequestIsCurrent(human) || !HS_BotCanBeAssigned(bot, g_HS_HumanUid))
    {
        return;
    }

    g_HS_BotUid = GetClientUserId(bot);
    if (GetClientTeam(human) != HS_TEAM_UNASSIGNED)
    {
        ChangeClientTeam(human, HS_TEAM_UNASSIGNED);
    }
    if (!HS_RequestIsCurrent(human) || GetClientOfUserId(g_HS_BotUid) != bot || !HS_BotCanBeAssigned(bot, g_HS_HumanUid))
    {
        HS_FinishJoin(human, HS_IsHuman(human) && GetClientTeam(human) == HS_TEAM_SURVIVOR);
        return;
    }
    if (HS_OwnerUserId(bot) != g_HS_HumanUid)
    {
        L4D_SetHumanSpec(bot, human);
    }
    if (HS_RequestIsCurrent(human) && GetClientOfUserId(g_HS_BotUid) == bot
        && HS_IsSurvivorBot(bot) && HS_OwnerUserId(bot) == g_HS_HumanUid)
    {
        L4D_TakeOverBot(human);
    }
    if (HS_IsHuman(human) && GetClientTeam(human) == HS_TEAM_SURVIVOR)
    {
        HS_FinishJoin(human, true);
    }
    else
    {
        g_HS_VerifyAt = GetGameTime() + 0.5;
    }
}

void HS_RemoveUnusedBots()
{
    if (g_HS_Transitioning)
    {
        return;
    }

    int humanSurvivors = 0;
    int aliveReservedBots = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (HS_IsHuman(i) && GetClientTeam(i) == HS_TEAM_SURVIVOR)
        {
            humanSurvivors++;
        }
        else if (HS_IsSurvivorBot(i) && IsPlayerAlive(i))
        {
            int ownerClient = 0;
            if (HS_IsBotReservedByOnlineHuman(i, ownerClient))
            {
                aliveReservedBots++;
            }
        }
    }

    // 开局宽限期内若有真人在线连入，暂缓清理无主 Bot 以便首个连入玩家平滑接管。
    if (humanSurvivors == 0 && aliveReservedBots == 0
        && GetGameTime() < g_HS_ReadyAt && HS_CountConnectedHumans(0) > 0)
    {
        return;
    }

    // 分两轮：先清理已死亡的无主 Bot，再清理存活的无主 Bot，防止残存死 Bot 触发误团灭。
    for (int pass = 0; pass < 2; pass++)
    {
        bool wantAlive = (pass == 1);
        for (int bot = 1; bot <= MaxClients; bot++)
        {
            if (!HS_IsSurvivorBot(bot) || IsPlayerAlive(bot) != wantAlive)
            {
                continue;
            }
            int botUid = GetClientUserId(bot);
            if (botUid == g_HS_BotUid)
            {
                continue;
            }
            int ownerUid = HS_OwnerUserId(bot);
            if (ownerUid < 0)
            {
                continue;
            }
            if (g_HS_CreationRequested && g_HS_BotUid == 0 && g_HS_BotBeforeCreate[bot] != botUid
                && g_HS_RetiredBotUid[bot] != botUid && (ownerUid == 0 || ownerUid == g_HS_HumanUid))
            {
                continue;
            }
            // 在线真人的 AFK/Idle 替身保护持续至挂机倒计时结束并安全解绑为止。
            int ownerClient = 0;
            if (HS_IsBotReservedByOnlineHuman(bot, ownerClient))
            {
                continue;
            }
            HS_UnbindBotOwner(bot);
            g_HS_RetiredBotUid[bot] = botUid;
            LogMessage("[HumanSeats] Removing unused survivor bot: bot userid=%d, owner userid=%d.", botUid, ownerUid);
            KickClient(bot, "Unused survivor seat");
        }
    }
}

public Action HS_Timer_Reconcile(Handle timer)
{
    HS_CheckPluginConflict();
    if (!HS_IsActive())
    {
        HS_CancelAllAfkCountdowns();
        HS_RestoreNativeAfkGuard();
        HS_RestoreSurvivorLimit();
        HS_StopOperation();
        return Plugin_Continue;
    }
    if (!IsServerProcessing() || !LibraryExists("left4dhooks") || g_HS_Transitioning)
    {
        return Plugin_Continue;
    }
    HS_UpdateSurvivorLimit();
    HS_CheckAfkPlayers();
    if (GetGameTime() >= g_HS_ReadyAt)
    {
        if (g_HS_CapacityActive && g_HS_QueueCapacityRoster)
        {
            g_HS_QueueCapacityRoster = false;
            for (int client = 1; client <= MaxClients; client++)
            {
                if (HS_CanJoin(client) && !g_HS_EverJoined[client] && !g_HS_VoluntarySpec[client] && !HS_RequestIsCurrent(client))
                {
                    HS_QueueJoin(client, 0, false);
                }
            }
        }
        HS_ReconcileJoin();
    }
    HS_RemoveUnusedBots();

    float now = GetGameTime();
    for (int i = 1; i <= MaxClients; i++)
    {
        if (g_HS_Requests[i].userid == 0 || g_HS_Requests[i].deadline > now)
        {
            continue;
        }
        if (HS_CanJoin(i) && g_HS_Requests[i].userid == GetClientUserId(i) && g_HS_Requests[i].resumeBot == 0)
        {
            PrintToChat(i, "[HumanSeats] Survivor seat unavailable. Try !jg or !join when a seat opens.");
        }
        HS_ClearRequest(i);
    }
    return Plugin_Continue;
}

#if defined TR_EMBEDDED_HUMAN_SEATS
void HS_GetDiagnosticStats(int &humanSurvivors, int &reservedAfkBots, int &unownedBots, int &seatLimit, int &activeCountdowns)
{
    humanSurvivors = 0;
    reservedAfkBots = 0;
    unownedBots = 0;
    activeCountdowns = 0;
    seatLimit = HS_GetSeatLimit();
    for (int i = 1; i <= MaxClients; i++)
    {
        if (g_HS_AfkTimer[i] != null && g_HS_AfkCountdownUid[i] != 0)
        {
            activeCountdowns++;
        }
        if (HS_IsHuman(i) && GetClientTeam(i) == HS_TEAM_SURVIVOR)
        {
            humanSurvivors++;
        }
        else if (HS_IsSurvivorBot(i))
        {
            int ownerClient = 0;
            if (HS_IsBotReservedByOnlineHuman(i, ownerClient))
            {
                reservedAfkBots++;
            }
            else
            {
                unownedBots++;
            }
        }
    }
}
#endif
