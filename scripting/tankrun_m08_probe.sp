// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <sdktools>
#include <left4dhooks>

#define VERSION "1.0.0"

public Plugin myinfo =
{
    name = "Tank Run M08 Probe",
    author = "Tank Run project",
    description = "Read-only first-contact rock comparison recorder",
    version = VERSION,
    url = ""
};

enum struct TankSample
{
    int birth;
    int userid;
    bool contact;
    float nextScan;
    float proxyDeadline;
    int activity;
    int activitySeen;
    int sequence;
    int sequenceSeen;
    int attempt;
    int abilityRef;
    int attemptResult; // 1: pre, 2: returned, 3: blocked
    float attemptTime;
    int posts;
    int swings;
    int hits;
    int punchPhase; // Set independently for each side of the proxy deadline.
}

enum RockRow
{
    RockRef, RockBirth, RockUserId, RockClient, RockPreTick, RockAttempt, RockPaired, RockRowSize
};

TankSample g_Tank[MAXPLAYERS + 1];
StringMap g_Rocks;
Handle g_Timer;
bool g_Recording;
int g_Run;
int g_NextBirth;
int g_NextAttempt;
int g_Matched;
int g_Unmatched;
int g_Births;
int g_PeakAI;
int g_ConfigChanges;
char g_Log[PLATFORM_MAX_PATH];
char g_Label[12];

static const char g_ConfigNames[][] =
{
    "tankrun_enabled", "tankrun_log_events", "tankrun_firstrock_enabled",
    "tankrun_firstrock_timeout", "tankrun_firstrock_attempts", "tankrun_rock_enabled",
    "tankrun_rock_min_delay", "tankrun_rock_force_scale", "tankrun_target_scan",
    "tankrun_target_mode", "tankrun_auto_spawn", "z_allow_ai_to_use_abilities",
    "z_tank_throw_interval", "tank_throw_min_interval", "tank_throw_allow_range"
};
ConVar g_Config[sizeof(g_ConfigNames)];

public void OnPluginStart()
{
    BuildPath(Path_SM, g_Log, sizeof(g_Log), "logs/tankrun_m08_probe.log");
    g_Rocks = new StringMap();
    RegAdminCmd("sm_m08_begin", Command_Begin, ADMFLAG_CONFIG, "sm_m08_begin <off|on|fail>");
    RegAdminCmd("sm_m08_end", Command_End, ADMFLAG_CONFIG, "Finish the M08 recording");
    HookEvent("tank_spawn", Event_Spawn, EventHookMode_Post);
    HookEvent("player_death", Event_Death, EventHookMode_Post);
    HookEvent("player_hurt", Event_Hurt, EventHookMode_Post);
    HookEvent("player_bot_replace", Event_Replace, EventHookMode_Post);
    HookEvent("bot_player_replace", Event_Replace, EventHookMode_Post);
    HookEvent("round_end", Event_RoundEnd, EventHookMode_Post);
}

void LogEvent(const char[] name, const char[] format, any ...)
{
    char info[512];
    VFormat(info, sizeof(info), format, 3);
    LogToFileEx(g_Log, "M08 run=%d arm=%s tick=%d gt=%.3f event=%s %s",
        g_Run, g_Label, GetGameTickCount(), GetGameTime(), name, info);
}

bool IsAiTank(int client)
{
    return client > 0 && client <= MaxClients && IsClientInGame(client) && IsFakeClient(client)
        && IsPlayerAlive(client) && GetClientTeam(client) == 3
        && HasEntProp(client, Prop_Send, "m_zombieClass")
        && GetEntProp(client, Prop_Send, "m_zombieClass") == 8;
}

bool IsSample(int client)
{
    return g_Recording && IsAiTank(client) && g_Tank[client].birth > 0
        && g_Tank[client].userid == GetClientUserId(client);
}

public void OnControlChange(ConVar cvar, const char[] oldValue, const char[] newValue)
{
    char name[64];
    cvar.GetName(name, sizeof(name));
    g_ConfigChanges++;
    LogEvent("CONFIG_CHANGED", "name=%s old=%.4f new=%.4f", name,
        StringToFloat(oldValue), StringToFloat(newValue));
}

float ReadCvar(const char[] name, float fallback)
{
    ConVar value = FindConVar(name);
    return value != null ? value.FloatValue : fallback;
}

void GetControlVersion(char[] version, int maxlen)
{
    strcopy(version, maxlen, "unknown");
    Handle plugins = GetPluginIterator();
    while (MorePlugins(plugins))
    {
        Handle item = ReadPlugin(plugins);
        char file[PLATFORM_MAX_PATH];
        GetPluginFilename(item, file, sizeof(file));
        // Also accept paths such as optional/tankdirector_menu.smx.
        int start = FindCharInString(file, '/', true) + 1;
        int backslash = FindCharInString(file, 92, true) + 1;
        if (backslash > start)
        {
            start = backslash;
        }
        if (StrEqual(file[start], "tankdirector_menu.smx", false))
        {
            GetPluginInfo(item, PlInfo_Version, version, maxlen);
            break;
        }
    }
    delete plugins;
}

public Action Command_Begin(int client, int args)
{
    char label[12];
    if (args != 1)
    {
        ReplyToCommand(client, "[M08] Usage: sm_m08_begin <off|on|fail>");
        return Plugin_Handled;
    }
    GetCmdArg(1, label, sizeof(label));
    bool off = StrEqual(label, "off");
    bool fail = StrEqual(label, "fail");
    if (!off && !fail && !StrEqual(label, "on"))
    {
        ReplyToCommand(client, "[M08] Expected off, on or fail.");
        return Plugin_Handled;
    }
    if (g_Recording)
    {
        ReplyToCommand(client, "[M08] Finish the current run with sm_m08_end.");
        return Plugin_Handled;
    }
    float first = ReadCvar("tankrun_firstrock_enabled", -1.0);
    float rock = ReadCvar("tankrun_rock_enabled", -1.0);
    if (first != (off ? 0.0 : 1.0) || rock != (fail ? 0.0 : 1.0))
    {
        ReplyToCommand(client, "[M08] Arm %s expects firstrock=%d rock=%d. Actual %.0f / %.0f (-1=missing).",
            label, off ? 0 : 1, fail ? 0 : 1, first, rock);
        return Plugin_Handled;
    }
    if (ReadCvar("tankrun_log_events", -1.0) <= 0.0)
    {
        ReplyToCommand(client, "[M08] Enable the control center event log first: sm_tank_log 1.");
        return Plugin_Handled;
    }
    if (ReadCvar("tankrun_enabled", -1.0) <= 0.0)
    {
        ReplyToCommand(client, "[M08] The control center must be enabled.");
        return Plugin_Handled;
    }
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsAiTank(i))
        {
            ReplyToCommand(client, "[M08] Start before spawning your test Tank. AI Tank %d is already alive.", i);
            return Plugin_Handled;
        }
    }

    g_Run++;
    g_Births = 0;
    g_Matched = 0;
    g_Unmatched = 0;
    g_PeakAI = 0;
    g_ConfigChanges = 0;
    g_Rocks.Clear();
    g_Recording = true;
    strcopy(g_Label, sizeof(g_Label), label);
    char map[64], mode[64], control[64];
    GetCurrentMap(map, sizeof(map));
    ConVar gameMode = FindConVar("mp_gamemode");
    strcopy(mode, sizeof(mode), "unknown");
    if (gameMode != null)
    {
        gameMode.GetString(mode, sizeof(mode));
    }
    GetControlVersion(control, sizeof(control));
    LogEvent("BEGIN", "utc=%d map=%s mode=%s probe=%s control=%s tick_interval=%.6f firstrock=%.0f rock=%.0f window=%.2f cap=%.0f scan=%.2f rock_delay=%.2f",
        GetTime(), map, mode, VERSION, control, GetTickInterval(), first, rock,
        ReadCvar("tankrun_firstrock_timeout", -1.0), ReadCvar("tankrun_firstrock_attempts", -1.0),
        ReadCvar("tankrun_target_scan", -1.0), ReadCvar("tankrun_rock_min_delay", -1.0));
    for (int i = 0; i < sizeof(g_Config); i++)
    {
        g_Config[i] = FindConVar(g_ConfigNames[i]);
        if (g_Config[i] != null)
        {
            LogEvent("CONFIG", "name=%s value=%.4f", g_ConfigNames[i], g_Config[i].FloatValue);
            g_Config[i].AddChangeHook(OnControlChange);
        }
    }
    g_Timer = CreateTimer(0.25, Timer_Observe, _, TIMER_REPEAT);
    ReplyToCommand(client, "[M08] Recording run %d (%s), control %s. Spawn one AI Tank now.", g_Run, g_Label, control);
    return Plugin_Handled;
}

void CloseBirth(int client, const char[] reason)
{
    if (g_Tank[client].birth == 0)
    {
        return;
    }
    LogEvent("LIFE_END", "birth=%d tank=%d uid=%d reason=%s contact=%d paired_posts=%d swings=%d hits=%d",
        g_Tank[client].birth, client, g_Tank[client].userid, reason, g_Tank[client].contact,
        g_Tank[client].posts, g_Tank[client].swings, g_Tank[client].hits);
    TankSample empty;
    g_Tank[client] = empty;
}

void FinishRun(const char[] reason)
{
    if (!g_Recording)
    {
        return;
    }
    delete g_Timer;
    for (int i = 0; i < sizeof(g_Config); i++)
    {
        if (g_Config[i] != null)
        {
            g_Config[i].RemoveChangeHook(OnControlChange);
            g_Config[i] = null;
        }
    }
    for (int i = 1; i <= MaxClients; i++)
    {
        CloseBirth(i, reason);
    }
    LogEvent("END", "reason=%s births=%d peak_ai=%d paired_posts=%d unmatched_posts=%d config_changes=%d",
        reason, g_Births, g_PeakAI, g_Matched, g_Unmatched, g_ConfigChanges);
    g_Rocks.Clear();
    g_Recording = false;
}

public Action Command_End(int client, int args)
{
    if (!g_Recording)
    {
        ReplyToCommand(client, "[M08] No recording in progress.");
        return Plugin_Handled;
    }
    FinishRun("manual");
    ReplyToCommand(client, "[M08] Run %d saved. Births %d, paired releases %d, unmatched Post %d.",
        g_Run, g_Births, g_Matched, g_Unmatched);
    return Plugin_Handled;
}

public void OnMapEnd() { FinishRun("map_end"); }
public void OnPluginEnd() { FinishRun("unload"); }
public void OnClientDisconnect(int client) { if (g_Recording) CloseBirth(client, "disconnect"); }
public void Event_RoundEnd(Event event, const char[] name, bool dontBroadcast) { FinishRun("round_end"); }

public void Event_Spawn(Event event, const char[] name, bool dontBroadcast)
{
    if (!g_Recording)
    {
        return;
    }
    int uid = event.GetInt("userid");
    int client = GetClientOfUserId(uid);
    if (!IsAiTank(client))
    {
        LogEvent("SPAWN_UNRESOLVED", "tank=%d uid=%d", client, uid);
        return;
    }
    CloseBirth(client, "new_spawn");
    g_Tank[client].birth = ++g_NextBirth;
    g_Tank[client].userid = uid;
    g_Tank[client].activity = -1;
    g_Tank[client].sequence = -1;
    g_Births++;
    int active = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsAiTank(i))
        {
            active++;
        }
    }
    if (active > g_PeakAI)
    {
        g_PeakAI = active;
    }
    float origin[3];
    GetClientAbsOrigin(client, origin);
    LogEvent("SPAWN", "birth=%d tank=%d uid=%d active_ai=%d origin=%.1f,%.1f,%.1f",
        g_Tank[client].birth, client, uid, active, origin[0], origin[1], origin[2]);
}

public void Event_Death(Event event, const char[] name, bool dontBroadcast)
{
    int client = GetClientOfUserId(event.GetInt("userid"));
    if (g_Recording && client > 0 && g_Tank[client].userid == event.GetInt("userid"))
    {
        CloseBirth(client, "death");
    }
}

public void Event_Hurt(Event event, const char[] name, bool dontBroadcast)
{
    int attacker = GetClientOfUserId(event.GetInt("attacker"));
    if (!IsSample(attacker))
    {
        return;
    }
    char weapon[24];
    event.GetString("weapon", weapon, sizeof(weapon));
    if (StrEqual(weapon, "tank_claw"))
    {
        LogEvent("CLAW_DAMAGE", "birth=%d tank=%d uid=%d victim_uid=%d damage=%d",
            g_Tank[attacker].birth, attacker, g_Tank[attacker].userid,
            event.GetInt("userid"), event.GetInt("dmg_health"));
    }
}

public void Event_Replace(Event event, const char[] name, bool dontBroadcast)
{
    int bot = GetClientOfUserId(event.GetInt("bot"));
    if (g_Recording && bot > 0 && g_Tank[bot].userid == event.GetInt("bot"))
    {
        CloseBirth(bot, name);
    }
}

public bool FilterSight(int entity, int mask, any ignored)
{
    return entity != (ignored & 0xff) && entity != (ignored >> 8);
}

void ObserveContact(int tank, float now)
{
    if (g_Tank[tank].contact || now < g_Tank[tank].nextScan)
    {
        return;
    }
    float interval = ReadCvar("tankrun_target_scan", 0.4);
    g_Tank[tank].nextScan = now + (interval < 0.1 ? 0.1 : interval);
    float from[3], to[3];
    GetClientEyePosition(tank, from);
    int chosen = 0;
    float best = 2500.0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!IsClientInGame(i) || !IsPlayerAlive(i) || GetClientTeam(i) != 2)
        {
            continue;
        }
        GetClientEyePosition(i, to);
        float distance = GetVectorDistance(from, to);
        if (distance >= best)
        {
            continue;
        }
        Handle trace = TR_TraceRayFilterEx(from, to, MASK_SHOT, RayType_EndPoint, FilterSight, tank | (i << 8));
        bool clear = !TR_DidHit(trace) || TR_GetFraction(trace) >= 0.99;
        delete trace;
        if (clear)
        {
            chosen = i;
            best = distance;
        }
    }
    if (chosen > 0)
    {
        g_Tank[tank].contact = true;
        g_Tank[tank].proxyDeadline = now + ReadCvar("tankrun_firstrock_timeout", 3.0);
        LogEvent("CONTACT_PROXY", "birth=%d tank=%d uid=%d survivor_uid=%d dist=%.1f estimate_deadline=%.3f",
            g_Tank[tank].birth, tank, g_Tank[tank].userid, GetClientUserId(chosen), best, g_Tank[tank].proxyDeadline);
    }
}

public Action Timer_Observe(Handle timer)
{
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsSample(i))
        {
            ObserveContact(i, GetGameTime());
        }
        else if (g_Tank[i].birth > 0)
        {
            CloseBirth(i, "identity_or_ai_lost");
        }
    }
    return Plugin_Continue;
}

// Log changing samples, not repeated SelectWeightedSequence polling.
public Action L4D2_OnSelectTankAttackPre(int client, int &sequence)
{
    if (IsSample(client))
    {
        int tick = GetGameTickCount();
        if (g_Tank[client].activity != sequence || tick > g_Tank[client].activitySeen + 1)
        {
            LogEvent("ACTIVITY_SEEN", "birth=%d tank=%d uid=%d activity=%d",
                g_Tank[client].birth, client, g_Tank[client].userid, sequence);
        }
        g_Tank[client].activity = sequence;
        g_Tank[client].activitySeen = tick;
    }
    return Plugin_Continue;
}

public Action L4D2_OnSelectTankAttack(int client, int &sequence)
{
    if (IsSample(client))
    {
        int tick = GetGameTickCount();
        if (g_Tank[client].sequence != sequence || tick > g_Tank[client].sequenceSeen + 1)
        {
            LogEvent("SEQUENCE_SEEN", "birth=%d tank=%d uid=%d model_sequence=%d",
                g_Tank[client].birth, client, g_Tank[client].userid, sequence);
        }
        g_Tank[client].sequence = sequence;
        g_Tank[client].sequenceSeen = tick;
    }
    return Plugin_Continue;
}

// Use entity ownership or exact ability-handle equality. Never guess by animation time.
int ResolveAbility(int ability, char[] binding, int maxlen)
{
    strcopy(binding, maxlen, "unknown");
    if (ability <= MaxClients || !IsValidEntity(ability))
    {
        return 0;
    }
    int claimed = -1;
    if (HasEntProp(ability, Prop_Send, "m_hOwnerEntity"))
    {
        claimed = GetEntPropEnt(ability, Prop_Send, "m_hOwnerEntity");
    }
    if (claimed < 1 && HasEntProp(ability, Prop_Data, "m_hOwnerEntity"))
    {
        claimed = GetEntPropEnt(ability, Prop_Data, "m_hOwnerEntity");
    }
    if (claimed > 0)
    {
        strcopy(binding, maxlen, "owner");
        return claimed;
    }
    int match = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsSample(i) && HasEntProp(i, Prop_Send, "m_customAbility")
            && GetEntPropEnt(i, Prop_Send, "m_customAbility") == ability)
        {
            if (match != 0)
            {
                strcopy(binding, maxlen, "ambiguous");
                return 0;
            }
            match = i;
        }
    }
    if (match != 0)
    {
        strcopy(binding, maxlen, "ability_handle");
    }
    return match;
}

void ObserveActivate(int ability, int result, const char[] stage)
{
    if (!g_Recording)
    {
        return;
    }
    char binding[20];
    int tank = ResolveAbility(ability, binding, sizeof(binding));
    int ref = ability > MaxClients && IsValidEntity(ability) ? EntIndexToEntRef(ability) : INVALID_ENT_REFERENCE;
    if (!IsSample(tank))
    {
        LogEvent(stage, "birth=0 tank=%d ability=%d ability_ref=%d binding=%s", tank, ability, ref, binding);
        return;
    }
    if (result == 1)
    {
        g_Tank[tank].attempt = ++g_NextAttempt;
        g_Tank[tank].attemptTime = GetGameTime();
        g_Tank[tank].abilityRef = ref;
        g_Tank[tank].attemptResult = 0;
    }
    bool associated = ref != INVALID_ENT_REFERENCE && g_Tank[tank].abilityRef == ref
        && (result == 1 || g_Tank[tank].attemptResult == 1);
    if (associated)
    {
        g_Tank[tank].attemptResult = result;
    }
    LogEvent(stage, "birth=%d tank=%d uid=%d ability=%d ability_ref=%d binding=%s attempt=%d",
        g_Tank[tank].birth, tank, g_Tank[tank].userid, ability, ref, binding,
        associated ? g_Tank[tank].attempt : 0);
}

public Action L4D_OnCThrowActivate(int ability)
{
    ObserveActivate(ability, 1, "ACTIVATE_PRE");
    return Plugin_Continue;
}
public void L4D_OnCThrowActivate_Post(int ability) { ObserveActivate(ability, 2, "ACTIVATE_POST"); }
public void L4D_OnCThrowActivate_PostHandled(int ability) { ObserveActivate(ability, 3, "ACTIVATE_BLOCKED"); }

public Action L4D_TankRock_OnRelease(int tank, int rock, float pos[3], float ang[3],
    float vel[3], float rot[3])
{
    if (g_Recording)
    {
        int row[RockRowSize];
        if (IsSample(tank))
        {
            row[RockBirth] = g_Tank[tank].birth;
            row[RockUserId] = g_Tank[tank].userid;
            if (g_Tank[tank].attemptResult == 2 && GetGameTime() - g_Tank[tank].attemptTime <= 8.0)
            {
                row[RockAttempt] = g_Tank[tank].attempt;
            }
        }
        row[RockClient] = tank;
        row[RockRef] = rock > MaxClients && IsValidEntity(rock) ? EntIndexToEntRef(rock) : INVALID_ENT_REFERENCE;
        row[RockPreTick] = GetGameTickCount();
        char key[16];
        IntToString(rock, key, sizeof(key));
        if (row[RockRef] != INVALID_ENT_REFERENCE)
        {
            g_Rocks.SetArray(key, row, sizeof(row));
        }
        LogEvent("RELEASE_PRE", "birth=%d tank=%d uid=%d rock=%d rock_ref=%d recent_attempt=%d speed_arg=%.1f",
            row[RockBirth], tank, row[RockUserId], rock, row[RockRef], row[RockAttempt], GetVectorLength(vel));
    }
    return Plugin_Continue;
}

bool FindRockRow(int tank, int rock, int row[RockRowSize])
{
    if (rock <= MaxClients || !IsValidEntity(rock))
    {
        return false;
    }
    char key[16];
    IntToString(rock, key, sizeof(key));
    return g_Rocks.GetArray(key, row, sizeof(row)) && row[RockClient] == tank
        && row[RockRef] == EntIndexToEntRef(rock);
}

public void L4D_TankRock_OnRelease_Post(int tank, int rock, const float pos[3], const float ang[3],
    const float vel[3], const float rot[3])
{
    if (!g_Recording)
    {
        return;
    }
    int row[RockRowSize];
    if (FindRockRow(tank, rock, row) && row[RockBirth] != 0 && IsSample(tank)
        && row[RockBirth] == g_Tank[tank].birth && !row[RockPaired]
        && row[RockPreTick] == GetGameTickCount())
    {
        row[RockPaired] = 1;
        char key[16];
        IntToString(rock, key, sizeof(key));
        g_Rocks.SetArray(key, row, sizeof(row));
        g_Tank[tank].posts++;
        g_Matched++;
        // The attempt is an owner-matched nearby event, not a proven call edge.
        LogEvent("RELEASE_PAIRED", "birth=%d tank=%d uid=%d rock=%d rock_ref=%d pre_tick=%d recent_attempt=%d speed_arg=%.1f",
            row[RockBirth], tank, row[RockUserId], rock, row[RockRef], row[RockPreTick],
            row[RockAttempt], GetVectorLength(vel));
        if (row[RockAttempt] != 0 && row[RockAttempt] == g_Tank[tank].attempt)
        {
            g_Tank[tank].attemptResult = 0;
        }
    }
    else
    {
        g_Unmatched++;
        LogEvent("RELEASE_POST_UNMATCHED", "tank=%d rock=%d speed_arg=%.1f", tank, rock, GetVectorLength(vel));
    }
}

public void L4D_TankRock_OnDetonate(int tank, int rock)
{
    if (!g_Recording)
    {
        return;
    }
    int row[RockRowSize];
    if (FindRockRow(tank, rock, row) && row[RockPaired])
    {
        LogEvent("ROCK_DETONATE", "birth=%d tank=%d uid=%d rock=%d rock_ref=%d",
            row[RockBirth], tank, row[RockUserId], rock, row[RockRef]);
    }
    else
    {
        LogEvent("ROCK_DETONATE_UNMATCHED", "tank=%d rock=%d", tank, rock);
    }
}

public void L4D_TankClaw_DoSwing_Pre(int tank, int claw)
{
    if (IsSample(tank))
    {
        g_Tank[tank].swings++;
        int phase = !g_Tank[tank].contact ? 1 : GetGameTime() < g_Tank[tank].proxyDeadline ? 2 : 4;
        if ((g_Tank[tank].punchPhase & phase) == 0)
        {
            g_Tank[tank].punchPhase |= phase;
            LogEvent("SWING_FIRST_IN_PHASE", "birth=%d tank=%d uid=%d claw=%d proxy_phase=%d",
                g_Tank[tank].birth, tank, g_Tank[tank].userid, claw, phase);
        }
    }
}

public Action L4D_TankClaw_OnPlayerHit_Pre(int tank, int claw, int victim)
{
    if (IsSample(tank))
    {
        g_Tank[tank].hits++;
        LogEvent("CLAW_HIT_PRE", "birth=%d tank=%d uid=%d claw=%d victim=%d",
            g_Tank[tank].birth, tank, g_Tank[tank].userid, claw, victim);
    }
    return Plugin_Continue;
}
