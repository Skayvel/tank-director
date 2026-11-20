// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <sdktools>
#include <sdkhooks>
#include <left4dhooks>

#define TC_MAX_REPLICAS 32
#define TC_SURVIVORS 8
#define TC_NATIVE_MAX 22
#define TC_MODEL "models/infected/hulk.mdl"
#define TC_PROFILE "server_tankrun_capacity.cfg"
#define TC_BRIDGE "tankrun_capacity_bridge"
#define TC_INTERVAL 0.10
#define TC_MOTION_INTERVAL (1.0 / 30.0)
#define TC_REPLICA_RUN_SPEED 190.0
#define TC_RAMP_INTERVAL 2.0
#define TC_RAMP_NATIVE_TIMEOUT 12.0

#if !defined TR_EMBEDDED_CAPACITY
native bool TankRun_IsManagedCapacityActive();

public Plugin myinfo =
{
    name = "Tank Run Expanded Capacity",
    author = "Tank Run project",
    description = "Native Tank bots plus server-side Tank actors for stock clients",
    version = "1.3.0",
    url = ""
};
#endif

ConVar g_TCEnabled;
ConVar g_TCGoal;
ConVar g_TCRotate;
ConVar g_TCRamp;
ConVar g_TCBridge;
#if !defined TR_EMBEDDED_CAPACITY
ConVar g_TCMode;
#endif
Handle g_TCTimer;
float g_TCNextMotionFrame;
ArrayList g_TCSpawnAreas;
bool g_TCMapReady;
bool g_TCRoundEnded;
bool g_TCHumanExit;
bool g_TCPhase;
#if !defined TR_EMBEDDED_CAPACITY
bool g_TCProfileInit;
bool g_TCBridgeWarning;
#endif
bool g_TCManagedLastTick;
float g_TCNextProbe;
float g_TCNextReplica;
float g_TCRotateAfter;
float g_TCNextTarget[TC_MAX_REPLICAS];
int g_TCTargetUid[TC_MAX_REPLICAS];
int g_TCEntityRefs[TC_MAX_REPLICAS];
int g_TCActorBirths[TC_MAX_REPLICAS];
float g_TCActorBornAt[TC_MAX_REPLICAS];
int g_TCNativeBirths[MAXPLAYERS + 1];
int g_TCNativeUsers[MAXPLAYERS + 1];
int g_TCNextBirth;
int g_TCLiveReplicas;
int g_TCPeakTotal;
int g_TCPeakWithEight;
#if !defined TR_EMBEDDED_CAPACITY
int g_TCSavedLimit = -1;
int g_TCLastLimit = -1;
float g_TCSavedInterval;
float g_TCLastInterval;
#endif
int g_TCHealth = 4000;
float g_TCUnderfilledSince;
float g_TCRampNextSpawn;
float g_TCRampNativeDeadline;
bool g_TCRampActors;

#include "tankrun_capacity/tc_nav.inc"
#include "tankrun_capacity/tc_combat.inc"

#if !defined TR_EMBEDDED_CAPACITY
public APLRes AskPluginLoad2(Handle myself, bool late, char[] error, int maxlen)
{
    if (GetEngineVersion() != Engine_Left4Dead2)
    {
        strcopy(error, maxlen, "Tank Run Expanded Capacity requires Left 4 Dead 2.");
        return APLRes_SilentFailure;
    }
    MarkNativeAsOptional("TankRun_IsManagedCapacityActive");
    CreateNative("TankRun_GetLiveReplicas", TC_NativeGetLiveReplicas);
    CreateNative("TankRun_TrySpawnReplica", TC_NativeTrySpawnReplica);
    CreateNative("TankRun_GetOldestReplicaTime", TC_NativeGetOldestReplicaTime);
    CreateNative("TankRun_RetireOldestReplica", TC_NativeRetireOldestReplica);
    return APLRes_Success;
}

public int TC_NativeGetLiveReplicas(Handle plugin, int params)
{
    return TC_GetLiveReplicas();
}

public int TC_NativeTrySpawnReplica(Handle plugin, int params)
{
    float pos[3], ang[3];
    GetNativeArray(1, pos, sizeof(pos));
    GetNativeArray(2, ang, sizeof(ang));
    return TC_TrySpawnManagedAt(pos, ang);
}

public int TC_NativeGetOldestReplicaTime(Handle plugin, int params)
{
    return view_as<int>(TC_GetOldestReplicaTime());
}

public int TC_NativeRetireOldestReplica(Handle plugin, int params)
{
    return TC_RetireOldestReplica();
}

public void OnPluginStart() { TC_OnPluginStart(); }
public void OnMapStart() { TC_OnMapStart(); }
public void OnMapEnd() { TC_OnMapEnd(); }
public void OnPluginEnd() { TC_OnPluginEnd(); }
#endif

int TC_GetLiveReplicas()
{
    return g_TCLiveReplicas;
}

int TC_TrySpawnManagedAt(const float pos[3], const float ang[3])
{
    if (!TC_ManagedRoundReady()) return 0;

    g_TCManagedLastTick = true;
    if (GetGameTime() >= g_TCNextProbe)
    {
        g_TCNextProbe = GetGameTime() + 1.0;
        TC_ReadManagedHealth();
    }
    return TC_CreateReplica(pos, ang);
}

int TC_GetOldestReplicaSlot()
{
    int oldest = -1;
    for (int slot = 0; slot < TC_MAX_REPLICAS; slot++)
    {
        if (g_TCActorBirths[slot] <= 0 || TC_CombatHealth(slot) <= 0) continue;
        int ent = EntRefToEntIndex(g_TCEntityRefs[slot]);
        if (ent <= MaxClients || !IsValidEntity(ent)) continue;
        if (oldest < 0 || g_TCActorBirths[slot] < g_TCActorBirths[oldest]) oldest = slot;
    }
    return oldest;
}

float TC_GetOldestReplicaTime()
{
    if (!TC_ManagedMode()) return -1.0;
    int slot = TC_GetOldestReplicaSlot();
    return slot < 0 ? -1.0 : g_TCActorBornAt[slot];
}

bool TC_RetireOldestReplica()
{
    if (!TC_ManagedRoundReady()) return false;
    int slot = TC_GetOldestReplicaSlot();
    if (slot < 0) return false;
    TC_RemoveReplica(slot);
    return true;
}

void TC_OnPluginStart()
{
    g_TCEnabled = CreateConVar("tankrun_capacity_enabled", "0", "Enable server-side expanded Tank Run capacity.", FCVAR_NOTIFY, true, 0.0, true, 1.0);
    g_TCGoal = CreateConVar("tankrun_capacity_total", "30", "Target of live native and server-side Tanks combined.", FCVAR_NOTIFY, true, 1.0, true, 40.0);
    g_TCRotate = CreateConVar("tankrun_capacity_rotate", "1", "At cap: 0=stop, 1=replace oldest Tank, 2=replace newest Tank.", FCVAR_NOTIFY, true, 0.0, true, 2.0);
    g_TCRamp = CreateConVar("tankrun_capacity_ramp", "0", "Benchmark a combined two-second spawn interval, filling native Tanks first and stopping at the cap.", FCVAR_NONE, true, 0.0, true, 1.0);
    g_TCBridge = CreateConVar(TC_BRIDGE, "", "Internal Tank Run capacity script response.", FCVAR_DONTRECORD);
#if !defined TR_EMBEDDED_CAPACITY
    g_TCMode = FindConVar("mp_gamemode");
#endif
    HookEvent("round_start", TC_EventRoundStart, EventHookMode_PostNoCopy);
    HookEvent("round_end", TC_EventRoundEnd, EventHookMode_PostNoCopy);
    HookEvent("mission_lost", TC_EventRoundEnd, EventHookMode_PostNoCopy);
    HookEvent("map_transition", TC_EventRoundEnd, EventHookMode_PostNoCopy);
    HookEvent("tank_spawn", TC_EventTankBorn, EventHookMode_Post);
    HookEvent("player_death", TC_EventTankGone, EventHookMode_Post);
    HookEvent("player_left_safe_area", TC_EventSafeExit, EventHookMode_Post);
    RegAdminCmd("sm_tank_capacity_status", TC_CommandStatus, ADMFLAG_GENERIC, "Show the expanded Tank Run population.");
    g_TCEnabled.AddChangeHook(TC_OnEnabled);
    g_TCRamp.AddChangeHook(TC_OnRampChanged);
}

void TC_OnMapStart()
{
    g_TCMapReady = true;
    g_TCNextMotionFrame = 0.0;
#if !defined TR_EMBEDDED_CAPACITY
    g_TCProfileInit = false;
    g_TCSavedLimit = -1;
    g_TCLastLimit = -1;
#endif
    TC_ResetRound();
    PrecacheModel(TC_MODEL, true);
    TC_CombatPrecache();
    delete g_TCTimer;
    g_TCTimer = CreateTimer(TC_INTERVAL, TC_Timer, _, TIMER_REPEAT | TIMER_FLAG_NO_MAPCHANGE);
}

#if !defined TR_EMBEDDED_CAPACITY
public void OnConfigsExecuted()
{
    ConVar serverCfg = FindConVar("servercfgfile");
    if (!TC_ManagedMode() && serverCfg != null && !g_TCProfileInit)
    {
        char name[PLATFORM_MAX_PATH];
        serverCfg.GetString(name, sizeof(name));
        if (StrEqual(name, TC_PROFILE, false)) g_TCEnabled.SetBool(true);
    }
    g_TCProfileInit = true;
}
#endif

void TC_OnMapEnd()
{
    g_TCMapReady = false;
    delete g_TCTimer;
    TC_ClearReplicas();
    delete g_TCSpawnAreas;
    TC_NavMapEnd();
}

void TC_OnPluginEnd()
{
    TC_ClearReplicas();
#if !defined TR_EMBEDDED_CAPACITY
    if (!TC_ManagedMode() && !g_TCManagedLastTick) TC_RestoreStockScript();
#endif
    delete g_TCSpawnAreas;
    TC_NavMapEnd();
}

#if !defined TR_EMBEDDED_CAPACITY
bool TC_IsTankRun()
{
    if (g_TCMode == null) return false;
    char mode[32];
    g_TCMode.GetString(mode, sizeof(mode));
    return StrEqual(mode, "tankrun", false);
}
#endif

bool TC_ManagedMode()
{
#if defined TR_EMBEDDED_CAPACITY
    return g_TCEnabled != null && g_TCEnabled.BoolValue && TR_ManagedCapacityActive();
#else
    return GetFeatureStatus(FeatureType_Native, "TankRun_IsManagedCapacityActive") == FeatureStatus_Available
        && TankRun_IsManagedCapacityActive();
#endif
}

bool TC_Enabled()
{
#if defined TR_EMBEDDED_CAPACITY
    bool enabled = TC_ManagedMode();
#else
    bool enabled = (g_TCEnabled.BoolValue || TC_ManagedMode()) && TC_IsTankRun();
#endif
    return g_TCMapReady && enabled && MaxClients <= 32
        && LibraryExists("left4dhooks");
}

bool TC_ManagedRoundReady()
{
    return TC_ManagedMode() && TC_Enabled() && !g_TCRoundEnded && TC_CountSurvivors(true) > 0 && TC_ObserveSafeExit();
}

bool TC_IsLivingSurvivor(int client)
{
    return client > 0 && client <= MaxClients && IsClientInGame(client)
        && GetClientTeam(client) == 2 && IsPlayerAlive(client);
}

int TC_CountSurvivors(bool onlyAlive)
{
    int count = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsClientInGame(i) && !IsFakeClient(i) && GetClientTeam(i) == 2
            && (!onlyAlive || IsPlayerAlive(i))) count++;
    }
    return count;
}

bool TC_IsNativeTank(int client)
{
    return IsClientInGame(client) && IsFakeClient(client) && GetClientTeam(client) == 3
        && IsPlayerAlive(client) && GetEntProp(client, Prop_Send, "m_zombieClass") == 8
        && GetEntProp(client, Prop_Send, "m_isGhost") == 0;
}

int TC_CountNativeTanks()
{
    int count = 0;
    for (int client = 1; client <= MaxClients; client++)
    {
        if (TC_IsNativeTank(client)) count++;
    }
    return count;
}

int TC_NativeBudget()
{
    int budget = MaxClients - TC_SURVIVORS - 1;
    if (budget < 0) return 0;
    if (budget > TC_NATIVE_MAX) budget = TC_NATIVE_MAX;
    return budget < g_TCGoal.IntValue ? budget : g_TCGoal.IntValue;
}

public void TC_OnEnabled(ConVar convar, const char[] oldValue, const char[] newValue)
{
#if defined TR_EMBEDDED_CAPACITY
    if (!convar.BoolValue)
    {
        TC_ClearReplicas();
        g_TCManagedLastTick = false;
    }
#else
    if (TC_ManagedMode()) return;
    if (!convar.BoolValue)
    {
        TC_ClearReplicas();
        TC_RestoreStockScript();
        g_TCPhase = false;
    }
    g_TCNextProbe = 0.0;
#endif
}

public void TC_OnRampChanged(ConVar convar, const char[] oldValue, const char[] newValue)
{
    g_TCRampNextSpawn = 0.0;
    g_TCRampNativeDeadline = 0.0;
    g_TCRampActors = false;
}

void TC_ResetRound()
{
    TC_ClearReplicas();
    g_TCRoundEnded = false;
    g_TCHumanExit = false;
    g_TCPhase = false;
#if !defined TR_EMBEDDED_CAPACITY
    g_TCBridgeWarning = false;
#endif
    g_TCManagedLastTick = false;
    g_TCNextProbe = GetGameTime() + 0.5;
    g_TCNextReplica = 0.0;
    g_TCRotateAfter = 0.0;
    g_TCUnderfilledSince = 0.0;
    g_TCRampNextSpawn = 0.0;
    g_TCRampNativeDeadline = 0.0;
    g_TCRampActors = false;
    g_TCPeakTotal = 0;
    g_TCPeakWithEight = 0;
    g_TCNextBirth = 0;
    for (int client = 1; client <= MaxClients; client++)
    {
        g_TCNativeUsers[client] = 0;
        g_TCNativeBirths[client] = 0;
    }
}

public void TC_EventRoundStart(Event event, const char[] name, bool dontBroadcast)
{
    TC_ResetRound();
}

public void TC_EventRoundEnd(Event event, const char[] name, bool dontBroadcast)
{
    g_TCRoundEnded = true;
    g_TCPhase = false;
    TC_ClearReplicas();
}

public void TC_EventSafeExit(Event event, const char[] name, bool dontBroadcast)
{
    if (!TC_Enabled()) return;
    int client = GetClientOfUserId(event.GetInt("userid"));
    if (!TC_IsLivingSurvivor(client) || IsFakeClient(client)) return;
    float pos[3];
    GetClientAbsOrigin(client, pos);
    if (!L4D_IsPositionInFirstCheckpoint(pos)) g_TCHumanExit = true;
}

public void TC_EventTankBorn(Event event, const char[] name, bool dontBroadcast)
{
    int uid = event.GetInt("userid");
    int client = GetClientOfUserId(uid);
    if (client == 0 || !TC_IsNativeTank(client)) return;
    if (g_TCNativeUsers[client] == uid && g_TCNativeBirths[client] > 0) return;
    g_TCNativeUsers[client] = uid;
    g_TCNativeBirths[client] = ++g_TCNextBirth;
    if (g_TCRamp.BoolValue && !TC_ManagedMode() && TC_Enabled())
    {
        float now = GetGameTime();
        if (g_TCRampNextSpawn < now + TC_RAMP_INTERVAL) g_TCRampNextSpawn = now + TC_RAMP_INTERVAL;
        g_TCRampNativeDeadline = now + TC_RAMP_NATIVE_TIMEOUT;
        LogMessage("[TankCapacity] Ramp native born: game=%.3f userid=%d native=%d actors=%d slots=%d.",
            now, uid, TC_CountNativeTanks(), g_TCLiveReplicas, MaxClients);
    }
}

public void TC_EventTankGone(Event event, const char[] name, bool dontBroadcast)
{
    int uid = event.GetInt("userid");
    int client = GetClientOfUserId(uid);
    if (client == 0 || g_TCNativeUsers[client] != uid) return;
    g_TCNativeUsers[client] = 0;
    g_TCNativeBirths[client] = 0;
}

bool TC_ObserveSafeExit()
{
    if (g_TCHumanExit) return true;
    if (!L4D_HasAnySurvivorLeftSafeArea()) return false;
    for (int client = 1; client <= MaxClients; client++)
    {
        if (!TC_IsLivingSurvivor(client) || IsFakeClient(client)) continue;
        float pos[3];
        GetClientAbsOrigin(client, pos);
        if (!L4D_IsPositionInFirstCheckpoint(pos))
        {
            g_TCHumanExit = true;
            break;
        }
    }
    return g_TCHumanExit;
}

#if !defined TR_EMBEDDED_CAPACITY
// Native bots share the client pool. Keep slots for all eight humans and one handoff.
public Action L4D_OnSpawnTank(const float pos[3], const float angles[3])
{
    if (TC_ManagedMode() || !TC_Enabled()) return Plugin_Continue;
    if (g_TCRoundEnded || TC_CountSurvivors(true) == 0 || !TC_ObserveSafeExit()) return Plugin_Handled;
    int nativeTanks = TC_CountNativeTanks();
    if (nativeTanks >= TC_NativeBudget() || nativeTanks + g_TCLiveReplicas >= g_TCGoal.IntValue
        || GetClientCount(false) >= MaxClients)
    {
        return Plugin_Handled;
    }
    if (g_TCRamp.BoolValue)
    {
        float now = GetGameTime();
        if (g_TCRampActors || now < g_TCRampNextSpawn) return Plugin_Handled;
        g_TCRampNextSpawn = now + TC_RAMP_INTERVAL;
    }
    return Plugin_Continue;
}
#endif

bool TC_RunScript(const char[] statement, char[] result, int resultSize)
{
    result[0] = '\0';
    if (!g_TCMapReady || FindEntityByClassname(-1, "worldspawn") == -1) return false;
    int logic = CreateEntityByName("logic_script");
    if (logic <= MaxClients || !IsValidEntity(logic)) return false;
    if (!DispatchSpawn(logic))
    {
        RemoveEntity(logic);
        return false;
    }
    char script[1024];
    Format(script, sizeof(script), "try { %s } catch (e) { Convars.SetValue(\"%s\", \"ERR\"); }", statement, TC_BRIDGE);
    g_TCBridge.SetString("");
    SetVariantString(script);
    AcceptEntityInput(logic, "RunScriptCode");
    RemoveEntity(logic);
    g_TCBridge.GetString(result, resultSize);
    return result[0] != '\0';
}

void TC_ReadManagedHealth()
{
    ConVar configured = FindConVar("tankrun_tank_health");
    if (configured != null && configured.IntValue > 0)
    {
        g_TCHealth = configured.IntValue;
        return;
    }
    configured = FindConVar("tankrun_tank_maxhealth");
    if (configured != null && configured.IntValue > 0)
    {
        g_TCHealth = configured.IntValue;
        return;
    }

    char statement[192], reply[16];
    Format(statement, sizeof(statement), "Convars.SetValue(\"%s\", ::SessionState.TankHealth.tointeger().tostring());", TC_BRIDGE);
    if (TC_RunScript(statement, reply, sizeof(reply)))
    {
        int health = StringToInt(reply);
        if (health > 0) g_TCHealth = health;
    }
}

#if !defined TR_EMBEDDED_CAPACITY
void TC_ReadStockPhase()
{
    // Keep the stock script's safe-room/finale decisions and its native ZSpawn entry.
    // Only positive, active budgets are replaced. Script zero retains its pause meaning.
    char statement[864];
    Format(statement, sizeof(statement),
        "local s = ::SessionState; local o = ::SessionOptions; if (!s.SpawnTankThink || s.TanksDisabled || o.cm_TankLimit <= 0) { Convars.SetValue(\"%s\", \"PAUSE\"); } else { local old = o.cm_TankLimit; local interval = s.SpawnInterval; o.cm_TankLimit = %d; s.SpawnInterval = 2.0; Convars.SetValue(\"%s\", format(\"OK|%%d|%%.6f|%%d\", old.tointeger(), interval.tofloat(), s.TankHealth.tointeger())); }",
        TC_BRIDGE, TC_NativeBudget(), TC_BRIDGE);
    char reply[96];
    char parts[4][24];
    bool hadReply = TC_NativeBudget() > 0 && TC_RunScript(statement, reply, sizeof(reply));
    g_TCPhase = hadReply && ExplodeString(reply, "|", parts, sizeof(parts), sizeof(parts[])) == sizeof(parts)
        && StrEqual(parts[0], "OK");
    if (!g_TCPhase)
    {
        if (!g_TCBridgeWarning && (!hadReply || !StrEqual(reply, "PAUSE")))
        {
            g_TCBridgeWarning = true;
            LogError("[TankCapacity] Could not read stock Tank Run state (reply '%s').", reply);
        }
        return;
    }
    g_TCBridgeWarning = false;

    int oldLimit = StringToInt(parts[1]);
    float oldInterval = StringToFloat(parts[2]);
    if (g_TCSavedLimit < 0 || oldLimit != g_TCLastLimit) g_TCSavedLimit = oldLimit;
    if (g_TCLastInterval == 0.0 || FloatAbs(oldInterval - g_TCLastInterval) > 0.0001) g_TCSavedInterval = oldInterval;
    g_TCLastLimit = TC_NativeBudget();
    g_TCLastInterval = 2.0;
    g_TCHealth = StringToInt(parts[3]);
    if (g_TCHealth < 1) g_TCHealth = 4000;
}

void TC_RestoreStockScript()
{
    if (g_TCSavedLimit < 0 || !TC_IsTankRun()) return;
    char statement[512];
    Format(statement, sizeof(statement),
        "local s = ::SessionState; local o = ::SessionOptions; if (o.cm_TankLimit == %d) o.cm_TankLimit = %d; if (s.SpawnInterval == %.6f) s.SpawnInterval = %.6f; Convars.SetValue(\"%s\", \"OK\");",
        g_TCLastLimit, g_TCSavedLimit, g_TCLastInterval, g_TCSavedInterval, TC_BRIDGE);
    char reply[16];
    TC_RunScript(statement, reply, sizeof(reply));
    g_TCSavedLimit = -1;
    g_TCLastLimit = -1;
    g_TCLastInterval = 0.0;
}
#endif

bool TC_SpawnTraceFilter(int entity, int contents, any data)
{
    return entity != data && entity > MaxClients;
}

bool TC_SpawnPositionSafe(float pos[3])
{
    if (L4D_IsPositionInFirstCheckpoint(pos) || L4D_IsPositionInLastCheckpoint(pos)) return false;
    float groundStart[3], groundEnd[3];
    for (int axis = 0; axis < 3; axis++)
    {
        groundStart[axis] = pos[axis];
        groundEnd[axis] = pos[axis];
    }
    groundStart[2] += 18.0;
    groundEnd[2] -= 64.0;
    TR_TraceRayFilter(groundStart, groundEnd, MASK_PLAYERSOLID, RayType_EndPoint, TC_SpawnTraceFilter, -1);
    if (!TR_DidHit() || TR_StartSolid()) return false;
    TR_GetEndPosition(pos);
    pos[2] += 2.0;

    if (!TC_NavSpawnHullClear(pos)) return false;

    float nearest = 999999.0;
    float chest[3];
    chest = pos;
    chest[2] += 48.0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!TC_IsLivingSurvivor(i) || IsFakeClient(i)) continue;
        float eyes[3];
        GetClientEyePosition(i, eyes);
        float distance = GetVectorDistance(eyes, chest);
        if (distance < 260.0) return false;
        if (distance < nearest) nearest = distance;
        if (distance <= 2400.0)
        {
            TR_TraceRayFilter(eyes, chest, MASK_VISIBLE, RayType_EndPoint, TC_SpawnTraceFilter, i);
            if (!TR_DidHit()) return false;
        }
    }
    return nearest <= 2400.0;
}

bool TC_FindSpawnPosition(float pos[3])
{
    for (int attempt = 0; attempt < 6; attempt++)
    {
        if (L4D_GetRandomPZSpawnPosition(0, 8, 4, pos) && TC_SpawnPositionSafe(pos)) return true;
    }
    // Native random Tank positions can be exhausted while the real Tank quota is full.
    if (g_TCSpawnAreas == null)
    {
        g_TCSpawnAreas = new ArrayList();
        L4D_GetAllNavAreas(g_TCSpawnAreas);
    }
    int numAreas = g_TCSpawnAreas.Length;
    for (int attempt = 0; attempt < 72 && numAreas > 0; attempt++)
    {
        Address area = g_TCSpawnAreas.Get(GetRandomInt(0, numAreas - 1));
        if (area == Address_Null || L4D_NavArea_IsBlocked(area, 3, false)) continue;
        if (L4D_GetNavArea_SpawnAttributes(area) & (NAV_SPAWN_PLAYER_START | NAV_SPAWN_CHECKPOINT | NAV_SPAWN_RESCUE_CLOSET | NAV_SPAWN_NO_MOBS)) continue;
        L4D_GetNavAreaCenter(area, pos);
        if (TC_SpawnPositionSafe(pos)) return true;
    }
    return false;
}

int TC_ReplicaSlotForEntity(int entity)
{
    if (entity <= MaxClients) return -1;
    for (int slot = 0; slot < TC_MAX_REPLICAS; slot++)
    {
        if (g_TCEntityRefs[slot] != 0 && EntRefToEntIndex(g_TCEntityRefs[slot]) == entity) return slot;
    }
    return -1;
}

void TC_RemoveReplica(int slot)
{
    if (g_TCEntityRefs[slot] == 0) return;
    int ent = EntRefToEntIndex(g_TCEntityRefs[slot]);
    g_TCEntityRefs[slot] = 0;
    g_TCActorBirths[slot] = 0;
    g_TCActorBornAt[slot] = 0.0;
    g_TCTargetUid[slot] = 0;
    g_TCNextTarget[slot] = 0.0;
    g_TCLiveReplicas--;
    TC_CombatReset(slot);
    TC_NavReset(slot);
    if (ent > MaxClients && IsValidEntity(ent))
    {
        AcceptEntityInput(ent, "Disable");
        AcceptEntityInput(ent, "DisableCollision");
        RemoveEntity(ent);
    }
}

void TC_ClearReplicas()
{
    for (int slot = 0; slot < TC_MAX_REPLICAS; slot++) TC_RemoveReplica(slot);
}

void TC_ReplicaDied(int slot, int attacker)
{
    LogMessage("[TankCapacity] Actor defeated: slot=%d attacker=%d.", slot, attacker);
    TC_RemoveReplica(slot);
}

int TC_CreateReplica(const float pos[3], const float ang[3])
{
    int slot = -1;
    for (int i = 0; i < TC_MAX_REPLICAS; i++)
    {
        if (g_TCEntityRefs[i] == 0)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0) return 0;

    int ent = CreateEntityByName("prop_dynamic_override");
    if (ent <= MaxClients || !IsValidEntity(ent)) return 0;
    DispatchKeyValue(ent, "model", TC_MODEL);
    DispatchKeyValue(ent, "solid", "2");
    DispatchKeyValue(ent, "DisableBoneFollowers", "1");
    DispatchKeyValue(ent, "DefaultAnim", "Idle");
    DispatchKeyValue(ent, "targetname", "tankrun_capacity_actor");
    TeleportEntity(ent, pos, ang, NULL_VECTOR);
    if (!DispatchSpawn(ent))
    {
        RemoveEntity(ent);
        return 0;
    }
    ActivateEntity(ent);
    g_TCEntityRefs[slot] = EntIndexToEntRef(ent);
    g_TCActorBirths[slot] = ++g_TCNextBirth;
    g_TCActorBornAt[slot] = GetGameTime();
    g_TCLiveReplicas++;
    TC_NavReset(slot);
    if (!TC_CombatAttach(slot, ent, g_TCHealth))
    {
        TC_RemoveReplica(slot);
        return 0;
    }
    return ent;
}

bool TC_RotateTank(int mode, float pos[3])
{
    int birth, nativeClient;
    int actorSlot = -1;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!TC_IsNativeTank(i)) continue;
        if (g_TCNativeUsers[i] != GetClientUserId(i) || g_TCNativeBirths[i] <= 0)
        {
            // Exact birth order is required before retiring an existing Tank.
            return false;
        }
        int candidate = g_TCNativeBirths[i];
        if (birth == 0 || (mode == 2 ? candidate > birth : candidate < birth))
        {
            birth = candidate;
            nativeClient = i;
        }
    }
    for (int i = 0; i < TC_MAX_REPLICAS; i++)
    {
        int candidate = g_TCActorBirths[i];
        if (candidate <= 0 || TC_CombatHealth(i) <= 0 || g_TCEntityRefs[i] == 0) continue;
        if (birth == 0 || (mode == 2 ? candidate > birth : candidate < birth))
        {
            birth = candidate;
            actorSlot = i;
        }
    }
    if (actorSlot >= 0)
    {
        TC_RemoveReplica(actorSlot);
        bool spawned = TC_CreateReplica(pos, NULL_VECTOR) > 0;
        LogMessage("[TankCapacity] Rotation mode=%d actor birth=%d retired=1 replacement=%d.", mode, birth, spawned);
        return spawned;
    }
    if (nativeClient <= 0) return false;

    int uid = GetClientUserId(nativeClient);
    ForcePlayerSuicide(nativeClient);
    bool retired = GetClientOfUserId(uid) != nativeClient || !TC_IsNativeTank(nativeClient);
    if (retired && GetClientOfUserId(uid) == nativeClient && IsClientInGame(nativeClient)
        && IsFakeClient(nativeClient) && !IsPlayerAlive(nativeClient) && !IsClientInKickQueue(nativeClient))
    {
        KickClient(nativeClient, "Tank Run rotation released a bot slot");
    }
    // The stock scheduler will refill a retired native bot using the native spawn path.
    LogMessage("[TankCapacity] Rotation mode=%d native birth=%d userid=%d retired=%d; stock scheduler refills.", mode, birth, uid, retired);
    return retired;
}

int TC_SelectTarget(int ent)
{
    float pos[3];
    GetEntPropVector(ent, Prop_Data, "m_vecAbsOrigin", pos);
    int nearest;
    float nearestDistance = 999999.0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!TC_IsLivingSurvivor(i)) continue;
        float player[3];
        GetClientAbsOrigin(i, player);
        float distance = GetVectorDistance(pos, player);
        if (distance < nearestDistance)
        {
            nearest = i;
            nearestDistance = distance;
        }
    }
    return nearest;
}

void TC_ThinkReplicas(float now, bool trackSurvivors = true)
{
    for (int slot = 0; slot < TC_MAX_REPLICAS; slot++)
    {
        int reference = g_TCEntityRefs[slot];
        if (reference == 0) continue;
        int ent = EntRefToEntIndex(reference);
        if (ent <= MaxClients || !IsValidEntity(ent))
        {
            TC_RemoveReplica(slot);
            continue;
        }
        TC_NavAdvanceMotion(slot, ent, now);
        int target = 0;
        if (trackSurvivors)
        {
            target = GetClientOfUserId(g_TCTargetUid[slot]);
            if (!TC_IsLivingSurvivor(target) || now >= g_TCNextTarget[slot])
            {
                target = TC_SelectTarget(ent);
                g_TCTargetUid[slot] = target > 0 ? GetClientUserId(target) : 0;
                g_TCNextTarget[slot] = now + 0.6;
            }
        }
        // This build can report start-solid against the actor itself even with a trace filter.
        // Suspend only its collision during the synchronous movement/combat traces.
        AcceptEntityInput(ent, "DisableCollision");
        bool attacking = TC_CombatThink(slot, ent, target, now);
        if (g_TCEntityRefs[slot] != reference || !IsValidEntity(ent)) continue;
        if (!attacking)
        {
            bool moving;
            if (TC_IsLivingSurvivor(target)) moving = TC_MoveReplica(slot, ent, target, TC_REPLICA_RUN_SPEED, TC_INTERVAL);
            else TC_NavStopMotion(slot);
            TC_CombatMotion(slot, ent, moving, now);
        }
        else TC_NavStopMotion(slot);
        AcceptEntityInput(ent, "EnableCollision");
    }
}

void TC_RunGameFrame()
{
    if (!g_TCMapReady || g_TCRoundEnded || g_TCLiveReplicas <= 0 || !TC_Enabled()) return;
    float now = GetGameTime();
    bool moveFrame = now >= g_TCNextMotionFrame;
    if (moveFrame)
    {
        float baseline = now - g_TCNextMotionFrame > TC_MOTION_INTERVAL ? now : g_TCNextMotionFrame;
        g_TCNextMotionFrame = baseline + TC_MOTION_INTERVAL;
    }
    bool managed = TC_ManagedMode();
    for (int slot = 0; slot < TC_MAX_REPLICAS; slot++)
    {
        int reference = g_TCEntityRefs[slot];
        if (!reference) continue;
        bool throwing = managed && g_TCCombat[slot].attack == TC_ATTACK_ROCK && !g_TCCombat[slot].applied;
        if (!throwing && !moveFrame) continue;
        int ent = EntRefToEntIndex(reference);
        if (ent <= MaxClients || !IsValidEntity(ent)) continue;
        if (throwing) TC_CombatTickRock(slot, ent);
        else TC_NavAdvanceMotion(slot, ent, now);
    }
}

#if !defined TR_EMBEDDED_CAPACITY
public void OnGameFrame() { TC_RunGameFrame(); }
#endif

public Action TC_Timer(Handle timer)
{
    bool managed = TC_ManagedMode();
    if (!managed && g_TCManagedLastTick)
    {
#if defined TR_EMBEDDED_CAPACITY
        TC_ClearReplicas();
#else
        if (!g_TCEnabled.BoolValue) TC_ClearReplicas();
#endif
        g_TCPhase = false;
        g_TCNextProbe = 0.0;
    }
    g_TCManagedLastTick = managed;
    if (!TC_Enabled() || g_TCRoundEnded) return Plugin_Continue;
    float now = GetGameTime();
    int humans = TC_CountSurvivors(true);
    if (humans == 0)
    {
        if (managed && g_TCLiveReplicas > 0) TC_ThinkReplicas(now, false);
        return Plugin_Continue;
    }
    if (!TC_ObserveSafeExit()) return Plugin_Continue;

#if !defined TR_EMBEDDED_CAPACITY
    if (!managed && now >= g_TCNextProbe)
    {
        g_TCNextProbe = now + 1.0;
        ConVar otherAuto = FindConVar("tankrun_auto_spawn");
        if (otherAuto != null && otherAuto.BoolValue) otherAuto.SetBool(false);
        TC_ReadStockPhase();
    }
#endif
    TC_ThinkReplicas(now);
    int nativeTanks = TC_CountNativeTanks();
    int total = nativeTanks + g_TCLiveReplicas;
    if (total > g_TCPeakTotal) g_TCPeakTotal = total;
    if (humans >= TC_SURVIVORS && total > g_TCPeakWithEight)
    {
        g_TCPeakWithEight = total;
        if (total >= 30) LogMessage("[TankCapacity] Goal observed: %d living survivors and %d live Tanks (%d native, %d actor).", humans, total, nativeTanks, g_TCLiveReplicas);
    }
    if (managed || !g_TCPhase) return Plugin_Continue;
    if (g_TCRamp.BoolValue)
    {
        if (total >= g_TCGoal.IntValue) return Plugin_Continue;
        if (!g_TCRampActors)
        {
            if (g_TCRampNativeDeadline == 0.0) g_TCRampNativeDeadline = now + TC_RAMP_NATIVE_TIMEOUT;
            if (nativeTanks < TC_NativeBudget() && now < g_TCRampNativeDeadline) return Plugin_Continue;
            g_TCRampActors = true;
            LogMessage("[TankCapacity] Ramp switches to actors: native=%d/%d total=%d/%d slots=%d.",
                nativeTanks, TC_NativeBudget(), total, g_TCGoal.IntValue, MaxClients);
        }
        if (now < g_TCRampNextSpawn) return Plugin_Continue;
        g_TCRampNextSpawn = now + TC_RAMP_INTERVAL;
        TC_TrySpawnReplica(humans, nativeTanks);
        return Plugin_Continue;
    }
    int preferredReplicas = g_TCGoal.IntValue - TC_NativeBudget();
    if (total >= g_TCGoal.IntValue)
    {
        g_TCUnderfilledSince = 0.0;
        int mode = g_TCRotate.IntValue;
        if (mode == 0) return Plugin_Continue;
        if (g_TCRotateAfter == 0.0) g_TCRotateAfter = now + 2.0;
        if (now >= g_TCRotateAfter)
        {
            g_TCRotateAfter = now + 2.0;
            float spawn[3];
            if (TC_FindSpawnPosition(spawn)) TC_RotateTank(mode, spawn);
        }
        return Plugin_Continue;
    }
    g_TCRotateAfter = 0.0;
    if (g_TCUnderfilledSince == 0.0) g_TCUnderfilledSince = now;
    // Initially give stock Tank Run priority for its normal share. Fill gaps if its
    // native population cannot reach quota over several ordinary spawn intervals.
    if (now >= g_TCNextReplica && (g_TCLiveReplicas < preferredReplicas || now - g_TCUnderfilledSince >= 55.0))
    {
        g_TCNextReplica = now + 1.5;
        TC_TrySpawnReplica(humans, nativeTanks);
    }
    return Plugin_Continue;
}

void TC_TrySpawnReplica(int humans, int nativeTanks)
{
    float pos[3];
    if (TC_FindSpawnPosition(pos) && TC_CreateReplica(pos, NULL_VECTOR) > 0)
    {
        LogMessage("[TankCapacity] Actor spawned: native=%d actors=%d total=%d living_humans=%d slots=%d/%d. game=%.3f",
            nativeTanks, g_TCLiveReplicas, nativeTanks + g_TCLiveReplicas, humans, GetClientCount(false), MaxClients, GetGameTime());
    }
}

public Action TC_CommandStatus(int client, int args)
{
    int natives = TC_CountNativeTanks();
    if (TC_ManagedMode())
    {
        ReplyToCommand(client, "[TankCapacity] Control Center managed: slots=%d/%d native=%d actors=%d total=%d peak=%d",
            GetClientCount(false), MaxClients, natives, TC_GetLiveReplicas(), natives + TC_GetLiveReplicas(), g_TCPeakTotal);
        return Plugin_Handled;
    }
    ReplyToCommand(client, "[TankCapacity] Enabled=%d stock phase=%d survivors=%d living=%d slots=%d/%d native=%d/%d actors=%d total=%d/%d peak=%d peak_with_eight_living_survivors=%d rotation=%d",
        TC_Enabled(), g_TCPhase, TC_CountSurvivors(false), TC_CountSurvivors(true), GetClientCount(false), MaxClients,
        natives, TC_NativeBudget(), g_TCLiveReplicas, natives + g_TCLiveReplicas, g_TCGoal.IntValue, g_TCPeakTotal, g_TCPeakWithEight, g_TCRotate.IntValue);
    return Plugin_Handled;
}
