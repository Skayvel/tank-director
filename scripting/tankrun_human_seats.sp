// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <sdktools>
#include <left4dhooks>

#define TEAM_SPECTATOR 1
#define TEAM_SURVIVOR 2
#define EXPANDED_SURVIVORS 8
#define VERSION "1.1.0"

public Plugin myinfo =
{
    name = "Tank Run Human Survivor Seats",
    author = "Tank Run project",
    description = "Keeps survivor seats human-only, with temporary bot handoffs",
    version = VERSION,
    url = ""
};

enum struct JoinRequest
{
    int userid;
    int resumeBot;
    float deadline;
}

ConVar g_Enabled;
ConVar g_Mode;
ConVar g_SurvivorLimit;
ConVar g_CapacityProfile;
Handle g_Timer;
bool g_Late;
float g_ReadyAt;
bool g_OwnerWarning;
bool g_CapacityActive;
bool g_QueueCapacityRoster;
int g_PreviousSurvivorLimit;
bool g_HadSurvivorUpperBound;
float g_PreviousSurvivorUpperBound;
bool g_RaisedSurvivorUpperBound;
JoinRequest g_Requests[MAXPLAYERS + 1];
int g_IdleOffer[MAXPLAYERS + 1];
int g_DepartingUid[MAXPLAYERS + 1];
int g_ReplacementOwnerUid[MAXPLAYERS + 1];
int g_ReplacementBotUid[MAXPLAYERS + 1];
int g_RetiredBotUid[MAXPLAYERS + 1];

int g_HumanUid;
int g_BotUid;
float g_OperationDeadline;
bool g_CreationRequested;
bool g_ReplacementConfirmed;
float g_VerifyAt;
int g_BotBeforeCreate[MAXPLAYERS + 1];

public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int maxlen)
{
    if (GetEngineVersion() != Engine_Left4Dead2)
    {
        strcopy(error, maxlen, "Tank Run Human Survivor Seats requires Left 4 Dead 2.");
        return APLRes_SilentFailure;
    }
    g_Late = late;
    return APLRes_Success;
}

public void OnPluginStart()
{
    g_Enabled = CreateConVar("tankrun_human_seats", "1", "Use human-only survivor seats in Tank Run.", FCVAR_NOTIFY, true, 0.0, true, 1.0);
    g_Mode = FindConVar("mp_gamemode");
    g_SurvivorLimit = FindConVar("survivor_limit");
    RegConsoleCmd("sm_jg", Command_JoinSeat, "Join a free human survivor seat.");
    RegConsoleCmd("sm_join", Command_JoinSeat, "Join a free human survivor seat.");
    HookEvent("player_bot_replace", Event_PlayerBotReplace, EventHookMode_Post);
    HookEvent("bot_player_replace", Event_BotPlayerReplace, EventHookMode_Post);
    HookEvent("round_start_post_nav", Event_RoundReady, EventHookMode_Post);
    if (g_Late)
    {
        StartMap();
    }
}

public void OnMapStart()
{
    StartMap();
}

public void OnConfigsExecuted()
{
    if (g_Enabled.BoolValue && IsTankRun()) UpdateSurvivorLimit();
}

public void OnPluginEnd()
{
    RestoreSurvivorLimit();
}

void StartMap()
{
    g_ReadyAt = GetGameTime() + 3.0;
    g_OwnerWarning = false;
    g_QueueCapacityRoster = true;
    StopOperation();
    for (int i = 1; i <= MaxClients; i++)
    {
        ClearRequest(i);
        g_IdleOffer[i] = 0;
        g_DepartingUid[i] = 0;
        g_ReplacementOwnerUid[i] = 0;
        g_ReplacementBotUid[i] = 0;
        g_RetiredBotUid[i] = 0;
    }
    delete g_Timer;
    g_Timer = CreateTimer(0.25, Timer_Reconcile, _, TIMER_REPEAT | TIMER_FLAG_NO_MAPCHANGE);
}

public void OnMapEnd()
{
    delete g_Timer;
    StopOperation();
}

public void Event_RoundReady(Event event, const char[] name, bool dontBroadcast)
{
    StopOperation();
    g_ReadyAt = GetGameTime() + 1.0;
    g_QueueCapacityRoster = true;
}

bool IsTankRun()
{
    char mode[32];
    if (g_Mode == null)
    {
        return false;
    }
    g_Mode.GetString(mode, sizeof(mode));
    return StrEqual(mode, "tankrun", false);
}

bool IsHuman(int client)
{
    return client > 0 && client <= MaxClients && IsClientInGame(client) && !IsFakeClient(client)
        && !IsClientInKickQueue(client);
}

bool IsSurvivorBot(int client)
{
    return client > 0 && client <= MaxClients && IsClientInGame(client) && IsFakeClient(client)
        && !IsClientSourceTV(client) && !IsClientReplay(client) && !IsClientInKickQueue(client)
        && GetClientTeam(client) == TEAM_SURVIVOR;
}

bool CanJoin(int client)
{
    return IsHuman(client) && GetClientTeam(client) <= TEAM_SPECTATOR;
}

void UpdateSurvivorLimit()
{
    if (g_CapacityProfile == null)
    {
        g_CapacityProfile = FindConVar("tankrun_capacity_enabled");
    }
    if (g_CapacityProfile == null || !g_CapacityProfile.BoolValue)
    {
        RestoreSurvivorLimit();
        return;
    }
    bool justEnabled = !g_CapacityActive;
    if (justEnabled)
    {
        g_CapacityActive = true;
        g_QueueCapacityRoster = true;
        if (g_SurvivorLimit != null)
        {
            g_PreviousSurvivorLimit = g_SurvivorLimit.IntValue;
            g_HadSurvivorUpperBound = g_SurvivorLimit.GetBounds(ConVarBound_Upper, g_PreviousSurvivorUpperBound);
            g_RaisedSurvivorUpperBound = g_HadSurvivorUpperBound && g_PreviousSurvivorUpperBound < float(EXPANDED_SURVIVORS);
            if (g_RaisedSurvivorUpperBound)
            {
                g_SurvivorLimit.SetBounds(ConVarBound_Upper, true, float(EXPANDED_SURVIVORS));
            }
        }
    }
    if (g_SurvivorLimit != null && g_SurvivorLimit.IntValue != EXPANDED_SURVIVORS)
    {
        g_SurvivorLimit.SetInt(EXPANDED_SURVIVORS);
    }
    if (justEnabled)
    {
        LogMessage("[HumanSeats] Capacity profile enabled: %d human survivor seats; engine survivor_limit=%d.",
            EXPANDED_SURVIVORS, g_SurvivorLimit == null ? -1 : g_SurvivorLimit.IntValue);
    }
}

void RestoreSurvivorLimit()
{
    if (!g_CapacityActive)
    {
        return;
    }
    g_CapacityActive = false;
    if (g_SurvivorLimit == null)
    {
        return;
    }
    if (g_RaisedSurvivorUpperBound)
    {
        g_SurvivorLimit.SetBounds(ConVarBound_Upper, g_HadSurvivorUpperBound, g_PreviousSurvivorUpperBound);
        g_RaisedSurvivorUpperBound = false;
    }
    if (g_SurvivorLimit.IntValue == EXPANDED_SURVIVORS)
    {
        g_SurvivorLimit.SetInt(g_PreviousSurvivorLimit);
    }
}

// A missing owner property is treated as unknown and prevents automatic removal.
int OwnerUserId(int bot)
{
    if (HasEntProp(bot, Prop_Send, "m_humanSpectatorUserID"))
    {
        return GetEntProp(bot, Prop_Send, "m_humanSpectatorUserID");
    }
    if (!g_OwnerWarning)
    {
        g_OwnerWarning = true;
        LogError("[HumanSeats] Cannot read survivor bot owner; skipping bots with unknown ownership.");
    }
    return -1;
}

void ClearRequest(int client)
{
    JoinRequest empty;
    g_Requests[client] = empty;
}

void QueueJoin(int client, int resumeBot)
{
    g_Requests[client].userid = GetClientUserId(client);
    g_Requests[client].resumeBot = resumeBot;
    g_Requests[client].deadline = GetGameTime() + (resumeBot > 0 ? 3.0 : 20.0);
}

void RetireReplacement(int bot, int botUid, int ownerUid)
{
    if (botUid <= 0)
    {
        return;
    }
    g_RetiredBotUid[bot] = botUid;
    if (!IsSurvivorBot(bot) || GetClientUserId(bot) != botUid)
    {
        return;
    }
    int owner = OwnerUserId(bot);
    if (owner == 0 || owner == ownerUid)
    {
        LogMessage("[HumanSeats] Removing departing player's survivor bot: bot userid=%d, player userid=%d.", botUid, ownerUid);
        KickClient(bot, "Player left the server");
    }
}

public void OnClientPostAdminCheck(int client)
{
    if (g_Enabled.BoolValue && IsTankRun() && CanJoin(client))
    {
        QueueJoin(client, 0);
    }
}

public void OnClientDisconnect(int client)
{
    ClearRequest(client);
    g_IdleOffer[client] = 0;
    if (IsFakeClient(client) || !g_Enabled.BoolValue || !IsTankRun())
    {
        return;
    }
    int uid = GetClientUserId(client);
    if (uid <= 0)
    {
        return;
    }
    g_DepartingUid[client] = uid;
    // The replacement event may fire before this disconnect callback.
    for (int bot = 1; bot <= MaxClients; bot++)
    {
        if (g_ReplacementOwnerUid[bot] == uid)
        {
            RetireReplacement(bot, g_ReplacementBotUid[bot], uid);
        }
    }
}

public Action Command_JoinSeat(int client, int args)
{
    if (!g_Enabled.BoolValue || !IsTankRun() || !CanJoin(client))
    {
        ReplyToCommand(client, "[HumanSeats] Join is available to Tank Run spectators.");
        return Plugin_Handled;
    }
    QueueJoin(client, 0);
    ReplyToCommand(client, "[HumanSeats] Survivor seat requested.");
    return Plugin_Handled;
}

public void Event_PlayerBotReplace(Event event, const char[] name, bool dontBroadcast)
{
    if (!g_Enabled.BoolValue || !IsTankRun())
    {
        return;
    }
    int humanUid = event.GetInt("player");
    int botUid = event.GetInt("bot");
    int human = GetClientOfUserId(humanUid);
    int bot = GetClientOfUserId(botUid);
    if (humanUid <= 0 || bot == 0)
    {
        return;
    }
    g_ReplacementOwnerUid[bot] = humanUid;
    g_ReplacementBotUid[bot] = botUid;
    if (human == 0 || g_DepartingUid[human] == humanUid)
    {
        RetireReplacement(bot, botUid, humanUid);
        return;
    }
    if (IsHuman(human) && IsSurvivorBot(bot) && OwnerUserId(bot) == humanUid)
    {
        g_IdleOffer[bot] = botUid;
        QueueJoin(human, botUid);
    }
}

public void Event_BotPlayerReplace(Event event, const char[] name, bool dontBroadcast)
{
    if (g_BotUid > 0 && event.GetInt("bot") == g_BotUid && event.GetInt("player") == g_HumanUid)
    {
        g_ReplacementConfirmed = true;
    }
}

bool RequestIsCurrent(int client)
{
    return CanJoin(client) && g_Requests[client].userid == GetClientUserId(client)
        && g_Requests[client].deadline > GetGameTime();
}

bool HasCapacity()
{
    // Tank Run uses the standard cooperative roster unless the server exposes a different limit.
    int maximum = 4;
    if (g_CapacityActive)
    {
        maximum = EXPANDED_SURVIVORS;
    }
    else if (g_SurvivorLimit != null && g_SurvivorLimit.IntValue > 0)
    {
        maximum = g_SurvivorLimit.IntValue;
    }
    int humans = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsHuman(i) && GetClientTeam(i) == TEAM_SURVIVOR)
        {
            humans++;
        }
    }
    return humans < maximum;
}

void StopOperation()
{
    g_HumanUid = 0;
    g_BotUid = 0;
    g_CreationRequested = false;
    g_ReplacementConfirmed = false;
    g_OperationDeadline = 0.0;
    g_VerifyAt = 0.0;
}

bool BotCanBeAssigned(int bot, int humanUid)
{
    if (!IsSurvivorBot(bot) || !IsPlayerAlive(bot) || g_RetiredBotUid[bot] == GetClientUserId(bot))
    {
        return false;
    }
    int owner = OwnerUserId(bot);
    return owner == 0 || owner == humanUid;
}

int FindSeat(int human)
{
    int humanUid = GetClientUserId(human);
    int resumeUid = g_Requests[human].resumeBot;
    if (resumeUid != 0)
    {
        int bot = GetClientOfUserId(resumeUid);
        return BotCanBeAssigned(bot, humanUid) && OwnerUserId(bot) == humanUid ? bot : 0;
    }
    int available = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!BotCanBeAssigned(i, humanUid))
        {
            continue;
        }
        if (OwnerUserId(i) == humanUid)
        {
            return i;
        }
        if (!g_CreationRequested || g_BotBeforeCreate[i] != GetClientUserId(i))
        {
            available = i;
        }
    }
    return available;
}

int ChooseCharacter()
{
    int offset = L4D2_GetSurvivorSetMod() == 1 ? 4 : 0;
    int occupied = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsClientInGame(i) && GetClientTeam(i) == TEAM_SURVIVOR
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
            return slot; // Native indices 0–3 use the current map's survivor set.
        }
    }
    return g_CapacityActive ? GetRandomInt(0, 3) : SurvivorCharacter_First;
}

void RequestBot()
{
    for (int i = 1; i <= MaxClients; i++)
    {
        g_BotBeforeCreate[i] = IsSurvivorBot(i) ? GetClientUserId(i) : 0;
    }
    g_CreationRequested = true;
    // The native returns 0 even on success. Resolve the new seat by its userid.
    L4D2Direct_AddSurvivorBot(ChooseCharacter());
}

void FinishJoin(int human, bool success)
{
    if (IsHuman(human) && GetClientUserId(human) == g_HumanUid)
    {
        ClearRequest(human);
        if (success && g_ReplacementConfirmed)
        {
            LogMessage("[HumanSeats] Survivor handoff confirmed: player userid=%d, bot userid=%d.", g_HumanUid, g_BotUid);
        }
        else if (success && g_BotUid != 0)
        {
            LogMessage("[HumanSeats] Player reached survivors: player userid=%d, requested bot userid=%d.", g_HumanUid, g_BotUid);
        }
        else if (!success)
        {
            if (GetClientTeam(human) == 0)
            {
                ChangeClientTeam(human, TEAM_SPECTATOR);
            }
            PrintToChat(human, "[HumanSeats] No survivor seat could be assigned. Try !jg or !join.");
            LogMessage("[HumanSeats] Survivor handoff expired: player userid=%d, bot userid=%d.", g_HumanUid, g_BotUid);
        }
    }
    StopOperation();
}

void ReconcileJoin()
{
    if (g_HumanUid == 0)
    {
        if (!HasCapacity())
        {
            return;
        }
        for (int i = 1; i <= MaxClients; i++)
        {
            if (RequestIsCurrent(i))
            {
                g_HumanUid = g_Requests[i].userid;
                g_OperationDeadline = GetGameTime() + 2.5;
                break;
            }
        }
    }
    int human = GetClientOfUserId(g_HumanUid);
    if (!IsHuman(human))
    {
        StopOperation();
        return;
    }
    if (GetClientTeam(human) == TEAM_SURVIVOR)
    {
        FinishJoin(human, true);
        return;
    }
    if (!RequestIsCurrent(human) || GetGameTime() >= g_OperationDeadline)
    {
        FinishJoin(human, false);
        return;
    }
    if (g_VerifyAt > 0.0)
    {
        if (GetGameTime() >= g_VerifyAt)
        {
            FinishJoin(human, false);
        }
        return;
    }

    int bot = FindSeat(human);
    if (bot == 0 && !g_CreationRequested && g_Requests[human].resumeBot == 0 && GetClientCount(false) < MaxClients)
    {
        RequestBot();
        bot = FindSeat(human);
    }
    if (bot == 0 || !RequestIsCurrent(human) || !BotCanBeAssigned(bot, g_HumanUid))
    {
        return;
    }
    g_BotUid = GetClientUserId(bot);
    if (GetClientTeam(human) != 0)
    {
        ChangeClientTeam(human, 0);
    }
    // ChangeClientTeam can dispatch other plugins. Recheck both identities afterwards.
    if (!RequestIsCurrent(human) || GetClientOfUserId(g_BotUid) != bot || !BotCanBeAssigned(bot, g_HumanUid))
    {
        FinishJoin(human, IsHuman(human) && GetClientTeam(human) == TEAM_SURVIVOR);
        return;
    }
    if (OwnerUserId(bot) != g_HumanUid)
    {
        L4D_SetHumanSpec(bot, human);
    }
    if (RequestIsCurrent(human) && GetClientOfUserId(g_BotUid) == bot
        && IsSurvivorBot(bot) && OwnerUserId(bot) == g_HumanUid)
    {
        L4D_TakeOverBot(human);
    }
    if (IsHuman(human) && GetClientTeam(human) == TEAM_SURVIVOR)
    {
        FinishJoin(human, true);
    }
    else
    {
        g_VerifyAt = GetGameTime() + 0.5;
    }
}

void RemoveUnusedBots()
{
    for (int bot = 1; bot <= MaxClients; bot++)
    {
        if (!IsSurvivorBot(bot))
        {
            continue;
        }
        int botUid = GetClientUserId(bot);
        if (botUid == g_BotUid)
        {
            continue;
        }
        int ownerUid = OwnerUserId(bot);
        if (ownerUid < 0)
        {
            continue;
        }
        // A bot from the current creation request can be in-game before it is alive.
        if (g_CreationRequested && g_BotUid == 0 && g_BotBeforeCreate[bot] != botUid
            && g_RetiredBotUid[bot] != botUid && (ownerUid == 0 || ownerUid == g_HumanUid))
        {
            continue;
        }
        int owner = GetClientOfUserId(ownerUid);
        if (g_RetiredBotUid[bot] != botUid && CanJoin(owner) && IsPlayerAlive(bot))
        {
            if (g_IdleOffer[bot] != botUid && !RequestIsCurrent(owner))
            {
                g_IdleOffer[bot] = botUid;
                QueueJoin(owner, botUid);
            }
            if (RequestIsCurrent(owner))
            {
                continue;
            }
        }
        // Only remove fake survivor clients, never a human or an infected bot.
        LogMessage("[HumanSeats] Removing unused survivor bot: bot userid=%d, owner userid=%d.", botUid, ownerUid);
        KickClient(bot, "Unused survivor seat");
    }
}

public Action Timer_Reconcile(Handle timer)
{
    if (!g_Enabled.BoolValue || !IsTankRun())
    {
        RestoreSurvivorLimit();
        StopOperation();
        return Plugin_Continue;
    }
    if (!IsServerProcessing() || !LibraryExists("left4dhooks"))
    {
        return Plugin_Continue;
    }
    UpdateSurvivorLimit();
    if (GetGameTime() >= g_ReadyAt)
    {
        if (g_CapacityActive && g_QueueCapacityRoster)
        {
            g_QueueCapacityRoster = false;
            for (int client = 1; client <= MaxClients; client++)
            {
                if (CanJoin(client) && !RequestIsCurrent(client))
                {
                    QueueJoin(client, 0);
                }
            }
        }
        ReconcileJoin();
    }
    RemoveUnusedBots();
    for (int i = 1; i <= MaxClients; i++)
    {
        if (g_Requests[i].userid == 0 || g_Requests[i].deadline > GetGameTime())
        {
            continue;
        }
        if (CanJoin(i) && g_Requests[i].userid == GetClientUserId(i) && g_Requests[i].resumeBot == 0)
        {
            PrintToChat(i, "[HumanSeats] Survivor seat unavailable. Try !jg or !join when a seat opens.");
        }
        ClearRequest(i);
    }
    return Plugin_Continue;
}
