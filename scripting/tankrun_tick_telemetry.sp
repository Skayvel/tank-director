// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

#pragma semicolon 1
#pragma newdecls required
#include <sourcemod>
#include <sdktools>

native int TankRun_GetLiveReplicas();

public Plugin myinfo =
{
    name = "Tank Run Tick Telemetry",
    author = "Tank Run project",
    description = "Records server tick progress and pacing against elapsed time",
    version = "1.2.0"
};

static const float g_Bounds[] = {5.0, 8.0, 10.0, 12.0, 15.0, 20.0, 25.0, 33.4, 50.0, 100.0};
int g_Buckets[sizeof(g_Bounds) + 1];
int g_FirstTick;
int g_LastTick;
int g_Intervals;
int g_MinTanks;
int g_PeakTanks;
int g_MinTotal;
int g_PeakTotal;
int g_SlowWindows;
int g_SlowStartMin;
int g_SlowStartMax;
float g_Start;
float g_Last;
float g_Peak;
float g_PeakAt;
float g_LastPopulationSample;
bool g_HaveBaseline;
ConVar g_Enabled;
ConVar g_Mode;
ConVar g_Window;
char g_Path[PLATFORM_MAX_PATH];
char g_LastSample[640];

public APLRes AskPluginLoad2(Handle plugin, bool late, char[] error, int maxlen)
{
    MarkNativeAsOptional("TankRun_GetLiveReplicas");
    return APLRes_Success;
}

public void OnPluginStart()
{
    g_Enabled = CreateConVar("tankrun_tick_telemetry", "1", "Record measured tick samples in Tank Run.", FCVAR_NONE, true, 0.0, true, 1.0);
    g_Window = CreateConVar("tankrun_tick_window", "5.0", "Elapsed wall seconds per tick sample.", FCVAR_NONE, true, 2.0, true, 10.0);
    g_Mode = FindConVar("mp_gamemode");
    BuildPath(Path_SM, g_Path, sizeof(g_Path), "logs/tankrun_tick.log");
    RegConsoleCmd("sm_tank_tick", Command_Show, "Show the last server tick pacing sample.");
    g_Enabled.AddChangeHook(OnEnabledChange);
    g_Window.AddChangeHook(OnEnabledChange);
}

public void OnMapStart()
{
    g_LastSample[0] = '\0';
    g_HaveBaseline = false;
    g_SlowWindows = 0;
}

public void OnMapEnd()
{
    g_HaveBaseline = false;
}

public void OnEnabledChange(ConVar convar, const char[] oldValue, const char[] newValue)
{
    g_LastSample[0] = '\0';
    g_HaveBaseline = false;
    g_SlowWindows = 0;
}

void StartWindow(int tick, float now)
{
    g_FirstTick = tick;
    g_LastTick = tick;
    g_Start = now;
    g_Last = now;
    g_Peak = 0.0;
    g_PeakAt = 0.0;
    g_Intervals = 0;
    g_MinTanks = -1;
    g_PeakTanks = 0;
    g_MinTotal = -1;
    g_PeakTotal = 0;
    for (int i = 0; i < sizeof(g_Buckets); i++) g_Buckets[i] = 0;
    g_HaveBaseline = true;
}

float GetP95UpperBound()
{
    int cutoff = RoundToCeil(float(g_Intervals) * 0.95);
    int seen;
    for (int i = 0; i < sizeof(g_Bounds); i++)
    {
        seen += g_Buckets[i];
        if (seen >= cutoff) return g_Bounds[i] < g_Peak ? g_Bounds[i] : g_Peak;
    }
    return g_Peak;
}

void SamplePopulation(int &humans, int &tanks)
{
    for (int client = 1; client <= MaxClients; client++)
    {
        if (!IsClientInGame(client)) continue;
        if (!IsFakeClient(client)) humans++;
        else if (GetClientTeam(client) == 3 && IsPlayerAlive(client)
            && GetEntProp(client, Prop_Send, "m_zombieClass") == 8
            && GetEntProp(client, Prop_Send, "m_isGhost") == 0) tanks++;
    }
}

int CountActors()
{
    if (GetFeatureStatus(FeatureType_Native, "TankRun_GetLiveReplicas") == FeatureStatus_Available)
        return TankRun_GetLiveReplicas();

    int actors;
    static const char classes[][] = {"prop_dynamic", "prop_dynamic_override"};
    for (int classIndex = 0; classIndex < sizeof(classes); classIndex++)
    {
        int entity = -1;
        while ((entity = FindEntityByClassname(entity, classes[classIndex])) != -1)
        {
            char classname[32], name[64];
            if (!GetEntityClassname(entity, classname, sizeof(classname)) || !StrEqual(classname, classes[classIndex])
                || !HasEntProp(entity, Prop_Data, "m_iName")) continue;
            GetEntPropString(entity, Prop_Data, "m_iName", name, sizeof(name));
            if (StrEqual(name, "tankrun_capacity_actor")) actors++;
        }
    }
    return actors;
}

void EmitWindow(int tick, float wallSeconds, int humans, int tanks, int actors)
{
    float configured = 1.0 / GetTickInterval();
    float effective = float(tick - g_FirstTick) / wallSeconds;
    if (g_PeakTotal > 0 && effective < configured * 0.95)
    {
        if (g_SlowWindows == 0)
        {
            g_SlowStartMin = g_MinTotal;
            g_SlowStartMax = g_PeakTotal;
        }
        g_SlowWindows++;
    }
    else
    {
        g_SlowWindows = 0;
        g_SlowStartMin = 0;
        g_SlowStartMax = 0;
    }
    char map[64];
    GetCurrentMap(map, sizeof(map));
    Format(g_LastSample, sizeof(g_LastSample),
        "mode=%s map=%s configured=%.2f effective=%.2f tick_callbacks=%.2f gap_p95_upper_ms=%.1f gap_max_ms=%.1f wall_s=%.3f humans=%d native_tanks=%d actors=%d native_tanks_min=%d native_tanks_peak=%d gap_end_s=%.3f total=%d total_min=%d total_peak=%d slots=%d slow_streak=%d slow_start_min=%d slow_start_max=%d",
        IsDedicatedServer() ? "dedicated" : "listen", map, configured, effective, float(g_Intervals) / wallSeconds,
        GetP95UpperBound(), g_Peak, wallSeconds, humans, tanks, actors, g_MinTanks, g_PeakTanks, g_PeakAt - g_Start,
        tanks + actors, g_MinTotal, g_PeakTotal, MaxClients, g_SlowWindows, g_SlowStartMin, g_SlowStartMax);
    LogToFileEx(g_Path, "%s", g_LastSample);
}

public void OnGameFrame()
{
    if (!g_Enabled.BoolValue) return;
    int tick = GetGameTickCount();
    if (g_HaveBaseline && tick == g_LastTick) return;
    float now = GetEngineTime();
    if (!g_HaveBaseline || tick < g_LastTick || now < g_Last)
    {
        g_LastPopulationSample = now;
        g_SlowWindows = 0;
        StartWindow(tick, now);
        return;
    }
    float gap = (now - g_Last) * 1000.0;
    int bucket;
    while (bucket < sizeof(g_Bounds) && gap > g_Bounds[bucket]) bucket++;
    g_Buckets[bucket]++;
    g_Intervals++;
    if (gap > g_Peak)
    {
        g_Peak = gap;
        g_PeakAt = now;
    }
    g_Last = now;
    g_LastTick = tick;
    float elapsed = now - g_Start;
    bool windowComplete = elapsed >= g_Window.FloatValue;
    if (!windowComplete && now - g_LastPopulationSample < 1.0) return;

    g_LastPopulationSample = now;
    char mode[32];
    if (g_Mode != null) g_Mode.GetString(mode, sizeof(mode));
    if (StrEqual(mode, "tankrun", false))
    {
        int humans, tanks;
        SamplePopulation(humans, tanks);
        if (humans > 0)
        {
            int actors = CountActors();
            int total = tanks + actors;
            if (g_MinTanks < 0 || tanks < g_MinTanks) g_MinTanks = tanks;
            if (tanks > g_PeakTanks) g_PeakTanks = tanks;
            if (g_MinTotal < 0 || total < g_MinTotal) g_MinTotal = total;
            if (total > g_PeakTotal) g_PeakTotal = total;
            if (windowComplete) EmitWindow(tick, elapsed, humans, tanks, actors);
        }
        else if (windowComplete) g_SlowWindows = 0;
    }
    if (windowComplete)
    {
        if (!StrEqual(mode, "tankrun", false)) g_SlowWindows = 0;
        StartWindow(tick, now);
    }
}

public Action Command_Show(int client, int args)
{
    ReplyToCommand(client, "[TankTick] %s", g_LastSample[0] == '\0' ? "Waiting for a Tank Run sample with a human connected." : g_LastSample);
    return Plugin_Handled;
}
