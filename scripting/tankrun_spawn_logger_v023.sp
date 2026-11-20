// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

/**
 * TankRun Spawn Logger v0.2.3, derived independently from v0.2.2-r1.
 * Read-only evidence collector. See analysis/spawn-logger-v023/state-design.md.
 * userid is the owner key; spawn_seq identifies a lifecycle across explicit
 * owner changes. tankid is diagnostic only. No game state is written.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>

#define PLUGIN_VERSION              "0.2.3"
#define LOG_FILE_NAME               "logs/tankrun_spawn_v023.log"
#define LOG_SCHEMA                  "v023"

#define MAX_ACTIVE_TANKS            MAXPLAYERS

#define TEAM_INFECTED               3
#define ZOMBIE_CLASS_TANK           8

#define TANK_STATE_NONE             -1
#define TANK_STATE_PENDING          0
#define TANK_STATE_CONFIRMED        1
#define TANK_STATE_UNCONFIRMED      2
#define TANK_STATE_TRANSIENT        3

#define ANCHOR_ORIGIN_NONE          0
#define ANCHOR_ORIGIN_RAW_START     1
#define ANCHOR_ORIGIN_POST_NAV      2
#define ANCHOR_ORIGIN_IMPLICIT      3

#define CONFIRM_BY_NONE             0
#define CONFIRM_BY_POST_NAV         1
#define CONFIRM_BY_ACTIVITY         2

#define TANK_RECONCILE_INTERVAL     1.0
#define TANK_PENDING_GRACE          5.0
#define TANK_UNCONFIRMED_RETENTION  15.0
#define TANK_CONFIRM_LOSS_GRACE     10.0
#define TANK_DISCONNECT_GRACE       10.0
#define ROUND_RAW_WINDOW            30.0
#define TANK_TRANSFER_GRACE         10.0

public Plugin myinfo =
{
    name = "TankRun Spawn Logger v0.2.3",
    author = "Tank Run Control Project",
    description = "Read-only TankRun lifecycle logger with tracked/confirmed state",
    version = PLUGIN_VERSION,
    url = ""
};

char g_LogPath[PLATFORM_MAX_PATH];
char g_MapName[128];
char g_RoundUid[32];

// Independent from map_start time-anchor validity. Never infer event map in a gap.
enum MapLifecycleState
{
    MAP_STATE_INITIALIZING,
    MAP_STATE_ACTIVE,
    MAP_STATE_TRANSITION
};
MapLifecycleState g_MapState = MAP_STATE_INITIALIZING;
bool g_MapClockComparable;
bool g_MapStartValid;
float g_MapStartTime;

int g_MapSerial;
int g_RoundSerial;
bool g_RoundActive;
bool g_RoundEnded;

int g_RoundAnchorOrigin;
int g_RoundAnchorConfirmedBy;
bool g_RoundAnchorValid;
float g_RoundAnchorTime;
bool g_RoundHasPostNavTime;
float g_RoundPostNavTime;
bool g_RoundPostNavSeen;
bool g_RoundBoundaryAmbiguous;
float g_RoundLastPostNavTime;
int g_RoundRawCount;
float g_RoundFirstRawTime;
float g_RoundLastRawTime;
int g_CandidateRawMap = -1;
int g_CandidateRawCount;
float g_CandidateRawFirst;
float g_CandidateRawLast;
int g_CandidateRawFirstOrder;
int g_BoundaryOrder;
int g_RoundEndOrder;
char g_Session[80];
int g_LogSeq;
bool g_RawStartSinceRoundBegin;

bool g_HasLastRawRoundStart;
float g_LastRawRoundStartTime;
bool g_HasRoundEndTime;
float g_RoundEndTime;

int g_RawRoundStartSeq;
int g_RoundDupStartCount;
int g_RoundPostNavCount;
int g_RoundDupPostNavCount;

bool g_RoundFirstSafeEventSeen;
float g_RoundFirstSafeEventTime;
int g_RoundFirstSafeEventUserId;
int g_RoundFirstSafeEventClient;
bool g_RoundFirstSafeEventBot;
int g_RoundFirstSafeEventPlayerCount;

bool g_RoundFirstSafeConfirmedSeen;
float g_RoundFirstSafeConfirmedTime;
int g_RoundFirstSafeConfirmedUserId;
int g_RoundFirstSafeConfirmedClient;
bool g_RoundFirstSafeConfirmedBot;

int g_RoundSafeExitEvents;
int g_SafeExitEventSeq;

bool g_RoundHasLastTankSpawn;
float g_RoundLastTankSpawnTime;
int g_RoundLastTankSpawnSeq;
int g_RoundTankSpawns;
int g_RoundTankConfirmed;
int g_RoundMaxTracked;
int g_RoundMaxConfirmed;

bool g_HasGlobalLastTankSpawn;
float g_GlobalLastTankSpawnTime;
int g_GlobalLastTankSpawnSeq;

int g_TankEventSeq;
int g_TankSpawnSeq;
int g_TankKillSeq;
int g_DisconnectSeq;

bool g_TankActive[MAX_ACTIVE_TANKS];
int g_TankState[MAX_ACTIVE_TANKS];
int g_TankUserId[MAX_ACTIVE_TANKS];
int g_TankTankId[MAX_ACTIVE_TANKS];
int g_TankClient[MAX_ACTIVE_TANKS];
int g_TankRecordSpawnSeq[MAX_ACTIVE_TANKS];
int g_TankSpawnRound[MAX_ACTIVE_TANKS];
float g_TankSpawnTime[MAX_ACTIVE_TANKS];
float g_TankLastValidTime[MAX_ACTIVE_TANKS];
float g_TankInvalidSince[MAX_ACTIVE_TANKS];
bool g_TankEverConfirmed[MAX_ACTIVE_TANKS];
bool g_TankDisconnectPending[MAX_ACTIVE_TANKS];
float g_TankDisconnectSince[MAX_ACTIVE_TANKS];

bool g_TankTransferPending[MAX_ACTIVE_TANKS];
int g_TankTransferUser[MAX_ACTIVE_TANKS];
float g_TankTransferSince[MAX_ACTIVE_TANKS];
char g_TankTransferKind[MAX_ACTIVE_TANKS][24];
char g_TankTransferReason[MAX_ACTIVE_TANKS][48];
Handle g_ReconcileTimer;

int g_LastLoggedTracked = -1;
int g_LastLoggedConfirmed = -1;
int g_LastLoggedPending = -1;
int g_LastLoggedUnconfirmed = -1;

public void OnPluginStart()
{
    if (GetEngineVersion() != Engine_Left4Dead2)
    {
        SetFailState("TankRun Spawn Logger v0.2.3 requires Left 4 Dead 2.");
        return;
    }

    int pathLen = BuildPath(Path_SM, g_LogPath, sizeof(g_LogPath), "%s", LOG_FILE_NAME);
    if (pathLen <= 0 || pathLen >= sizeof(g_LogPath) || g_LogPath[0] == '\0')
    {
        SetFailState("Could not resolve the SourceMod log path.");
        return;
    }

    GetCurrentMap(g_MapName, sizeof(g_MapName));
    if (g_MapName[0] == '\0')
    {
        strcopy(g_MapName, sizeof(g_MapName), "unknown");
    }

    // SourceMod uniform random streams are per-plugin (float.inc).
    Format(g_Session, sizeof(g_Session), "%d-%d-%08X%08X", GetTime(), GetSysTickCount(), GetURandomInt(), GetURandomInt());
    g_MapStartValid = false;
    g_MapSerial = 0;
    g_RoundSerial = 0;
    strcopy(g_RoundUid, sizeof(g_RoundUid), "none");
    ResetRoundState();
    ClearTankRegistry();

    if (!HookRequired("tank_spawn", Event_TankSpawn))
    {
        return;
    }
    if (!HookRequired("tank_killed", Event_TankKilled))
    {
        return;
    }
    if (!HookRequired("player_left_safe_area", Event_PlayerLeftSafeArea))
    {
        return;
    }
    if (!HookRequired("round_start", Event_RoundStart))
    {
        return;
    }
    if (!HookRequired("round_end", Event_RoundEnd))
    {
        return;
    }

    HookOptional("round_start_post_nav", Event_RoundStartPostNav);
    HookOptional("round_start_pre_entity", Event_RoundStartPreEntity);
    HookOptional("round_freeze_end", Event_RoundFreezeEnd);
    HookOptional("player_bot_replace", Event_PlayerBotReplace);
    HookOptional("bot_player_replace", Event_BotPlayerReplace);

    if (!StartReconcileTimer())
    {
        return;
    }

    g_MapClockComparable = true;
    g_MapState = MAP_STATE_ACTIVE; // Late load observes map_id=0 without inventing a map_start anchor.
    LogV2("PLUGIN_LOAD|version=%s|sm=%s|engine=l4d2|map=%s|gametime=%.3f|log=%s|map_anchor=%s|reconcile_interval=%.1f|pending_grace=%.1f|unconfirmed_retention=%.1f|confirm_loss_grace=%.1f|disconnect_grace=%.1f|confirmed_semantics=client_state_sample|raw_window=30.0|transfer_grace=10.0",
        PLUGIN_VERSION,
        SOURCEMOD_VERSION,
        g_MapName,
        GetGameTime(),
        g_LogPath,
        g_MapStartValid ? "map_start" : "late_load",
        TANK_RECONCILE_INTERVAL,
        TANK_PENDING_GRACE,
        TANK_UNCONFIRMED_RETENTION,
        TANK_CONFIRM_LOSS_GRACE,
        TANK_DISCONNECT_GRACE);

    PrintToServer("[TankLog v023] Loaded %s; log: %s", PLUGIN_VERSION, g_LogPath);
}

public void OnMapStart()
{
    g_MapState = MAP_STATE_INITIALIZING;
    g_MapClockComparable = false; // GetGameTime may already belong to the next map.
    float now = GetGameTime();
    LogV2("MAP_INIT_BEGIN|gametime=%.3f|previous_map=%d|attribution=unassigned", now, g_MapSerial);
    ArchiveCandidateRaw("map_start_unassigned");

    if (g_RoundActive)
    {
        LogRoundSummary("map_change");
    }
    ClearActiveTanks("map_change", "TANK_STALE", now, true);

    g_MapSerial++;
    GetCurrentMap(g_MapName, sizeof(g_MapName));
    if (g_MapName[0] == '\0')
    {
        strcopy(g_MapName, sizeof(g_MapName), "unknown");
    }

    g_MapStartTime = now;
    g_MapStartValid = true;

    g_RoundSerial = 0;
    g_RoundActive = false;
    g_RoundEnded = false;
    strcopy(g_RoundUid, sizeof(g_RoundUid), "none");
    ResetRoundState();

    ResetCandidateRaw();
    // Boundary order is session-monotonic, including equal-time cross-map events.
    g_RoundEndOrder = 0;
    g_HasLastRawRoundStart = false;
    g_LastRawRoundStartTime = 0.0;
    g_HasRoundEndTime = false;
    g_RoundEndTime = 0.0;
    g_HasGlobalLastTankSpawn = false;
    g_GlobalLastTankSpawnTime = 0.0;
    g_GlobalLastTankSpawnSeq = 0;

    if (!StartReconcileTimer()) return; // Failure remains INITIALIZING.
    g_MapClockComparable = true;
    g_MapState = MAP_STATE_ACTIVE;
    LogV2("MAP_START|map=%s|map_id=%d|gametime=%.3f|map_anchor=map_start|round=%d|round_uid=%s|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d",
        g_MapName,
        g_MapSerial,
        now,
        g_RoundSerial,
        g_RoundUid,
        CountTrackedTanks(),
        CountConfirmedTanks(),
        CountPendingTanks(),
        CountUnconfirmedTanks());
}

public void OnMapEnd()
{
    g_MapState = MAP_STATE_TRANSITION; // Close the gate before any cleanup.
    LogV2("MAP_TRANSITION_BEGIN|gametime=%.3f|map_id=%d", GetGameTime(), g_MapSerial);
    if (g_ReconcileTimer != null)
    {
        KillTimer(g_ReconcileTimer);
        g_ReconcileTimer = null;
    }

    float now = GetGameTime();
    char sinceMap[32];
    FormatDelta(sinceMap, sizeof(sinceMap), DeltaOrNeg(now, g_MapStartValid, g_MapStartTime));

    ArchiveCandidateRaw("map_end_unassigned");
    if (g_RoundActive) LogRoundSummary("map_end");
    ClearActiveTanks("map_end", "TANK_STALE", now, true);
    g_RoundActive = false;
    g_RoundEnded = false;

    LogV2("MAP_END|map=%s|map_id=%d|gametime=%.3f|since_map=%s|round=%d|round_uid=%s|active=%d|ended=%d|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d|spawns=%d|confirmed_spawns=%d|max_tracked_alive=%d|max_confirmed_alive=%d|safe_events=%d|raw_starts=%d",
        g_MapName,
        g_MapSerial,
        now,
        sinceMap,
        g_RoundSerial,
        g_RoundUid,
        g_RoundActive ? 1 : 0,
        g_RoundEnded ? 1 : 0,
        CountTrackedTanks(),
        CountConfirmedTanks(),
        CountPendingTanks(),
        CountUnconfirmedTanks(),
        g_RoundTankSpawns,
        g_RoundTankConfirmed,
        g_RoundMaxTracked,
        g_RoundMaxConfirmed,
        g_RoundSafeExitEvents,
        g_RawRoundStartSeq);
    g_MapClockComparable = false;
}

public void OnPluginEnd()
{
    if (g_ReconcileTimer != null)
    {
        KillTimer(g_ReconcileTimer);
        g_ReconcileTimer = null;
    }

    ArchiveCandidateRaw("unload_unassigned");
    if (g_RoundActive)
    {
        LogRoundSummary("plugin_unload");
    }
    ClearActiveTanks("plugin_unload", "TANK_STALE", GetGameTime(), true);

    LogV2("PLUGIN_UNLOAD|version=%s|map=%s|map_id=%d|round=%d|round_uid=%s|tank_seq=%d|kill_seq=%d|safe_seq=%d|tracked_alive=%d|confirmed_alive=%d",
        PLUGIN_VERSION,
        g_MapName,
        g_MapSerial,
        g_RoundSerial,
        g_RoundUid,
        g_TankSpawnSeq,
        g_TankKillSeq,
        g_SafeExitEventSeq,
        CountTrackedTanks(),
        CountConfirmedTanks());
}

public void OnClientDisconnect(int client)
{
    if (!CanObserveMapActivity("client_disconnect")) return;
    int userid = UserIdForClient(client);
    int slot = userid > 0 ? FindTankByUserId(userid) : FindTankByClient(client);
    if (slot < 0) return;
    if (g_TankClient[slot] > 0 && g_TankClient[slot] != client) return;
    if (userid > 0 && userid != g_TankUserId[slot]) return;
    float now = GetGameTime();
    if (g_TankDisconnectPending[slot])
    {
        LogV2("TANK_DISCONNECT_PENDING|duplicate=1|spawn_seq=%d|userid=%d|client=%d|gametime=%.3f|deadline_kept=1",
            g_TankRecordSpawnSeq[slot], g_TankUserId[slot], client, now);
        return;
    }
    g_TankDisconnectPending[slot] = true;
    g_TankDisconnectSince[slot] = now;
    LogV2("TANK_DISCONNECT_PENDING|disc_seq=%d|duplicate=0|spawn_seq=%d|userid=%d|client=%d|gametime=%.3f|grace=%.1f|tracked_alive=%d|confirmed_alive=%d",
        ++g_DisconnectSeq, g_TankRecordSpawnSeq[slot], g_TankUserId[slot], client, now, TANK_DISCONNECT_GRACE,
        CountTrackedTanks(), CountConfirmedTanks());
    EvaluateTankSlot(slot, now, true);
    LogStateIfChanged(now);
}

public void Event_RoundStart(Event event, const char[] name, bool dontBroadcast)
{
    float now = GetGameTime();
    int order = ++g_BoundaryOrder;
    int seq = ++g_RawRoundStartSeq;
    if (g_MapState == MAP_STATE_ACTIVE && g_RoundActive && !g_RoundEnded)
        g_RoundBoundaryAmbiguous = true;
    LogV2("ROUND_START_RAW|raw_seq=%d|event_order=%d|round_context=%d|round_uid_context=%s|active=%d|ended=%d|gametime=%.3f|since_prev_raw=%.3f|attribution=%s|stats_reset=0",
        seq, order, g_RoundSerial, g_RoundUid, g_RoundActive ? 1 : 0, g_RoundEnded ? 1 : 0, now,
        DeltaOrNeg(now, g_HasLastRawRoundStart, g_LastRawRoundStartTime),
        g_MapState == MAP_STATE_ACTIVE ? "pending" : "unassigned");
    if (!CanObserveMapActivity("round_start"))
    {
        LogV2("ROUND_START_DECISION|raw_seq=%d|event_order=%d|decision=unassigned|reason=map_not_active|gametime=%.3f|stats_reset=0|candidate_count=0|assigned=0|ambiguous=1",
            seq, order, now);
        return;
    }
    if (g_CandidateRawCount > 0 && (g_CandidateRawMap != g_MapSerial
        || now < g_CandidateRawFirst || now - g_CandidateRawFirst > ROUND_RAW_WINDOW))
        ArchiveCandidateRaw(g_CandidateRawMap != g_MapSerial ? "map_mismatch_before_new_raw"
            : (now < g_CandidateRawFirst ? "clock_regressed_before_new_raw" : "expired_before_new_raw"));
    if (g_CandidateRawCount == 0)
    {
        g_CandidateRawMap = g_MapSerial;
        g_CandidateRawFirst = now;
        g_CandidateRawFirstOrder = order;
    }
    g_CandidateRawCount++;
    g_CandidateRawLast = now;
    g_HasLastRawRoundStart = true;
    g_LastRawRoundStartTime = now;
    g_RawStartSinceRoundBegin = true;
    LogV2("ROUND_START_DECISION|raw_seq=%d|decision=buffer|stats_reset=0|candidate_count=%d|first_raw_gametime=%.3f|last_raw_gametime=%.3f|ambiguous=1",
        seq, g_CandidateRawCount, g_CandidateRawFirst, g_CandidateRawLast);
}

public void Event_RoundStartPreEntity(Event event, const char[] name, bool dontBroadcast)
{
    LogV2("ROUND_MARKER|marker=round_start_pre_entity|round=%d|round_uid=%s|gametime=%.3f|active=%d|ended=%d|raw_since_begin=%d",
        g_RoundSerial,
        g_RoundUid,
        GetGameTime(),
        g_RoundActive ? 1 : 0,
        g_RoundEnded ? 1 : 0,
        g_RawStartSinceRoundBegin ? 1 : 0);
}

public void Event_RoundStartPostNav(Event event, const char[] name, bool dontBroadcast)
{
    float now = GetGameTime();
    int order = ++g_BoundaryOrder;
    bool freshRaw = HasFreshBoundaryRaw(now);
    LogV2("ROUND_MARKER|marker=round_start_post_nav|event_order=%d|round_context=%d|round_uid_context=%s|gametime=%.3f|active=%d|ended=%d|activity=%d|candidate_raw=%d|fresh_raw=%d|attribution=pending",
        order, g_RoundSerial, g_RoundUid, now, g_RoundActive ? 1 : 0, g_RoundEnded ? 1 : 0,
        HasActivity() ? 1 : 0, g_CandidateRawCount, freshRaw ? 1 : 0);

    if (!CanObserveMapActivity("round_start_post_nav")) return;
    // This single boundary gate precedes ALL anchor-specific handling.
    bool newBoundary = !g_RoundActive || (g_RoundEnded && freshRaw);
    if (newBoundary)
    {
        LogV2("ROUND_BOUNDARY_DECISION|decision=new|previous_round=%d|event_order=%d|evidence=%s|ambiguous=%d|gametime=%.3f",
            g_RoundSerial, order, g_RoundActive ? "end_raw_post_nav" : "initial_post_nav", freshRaw ? 0 : 1, now);
        float anchor = freshRaw ? g_CandidateRawFirst : now;
        if (!BeginRound(anchor, freshRaw ? ANCHOR_ORIGIN_RAW_START : ANCHOR_ORIGIN_POST_NAV,
            CONFIRM_BY_POST_NAV, g_RoundActive ? "end_raw_post_nav" : "initial_post_nav")) return;
        RecordPostNav(now);
        LogRoundTimes("post_nav");
        return;
    }
    if (g_RoundEnded)
    {
        LogV2("ROUND_POST_NAV_DECISION|decision=ambiguous|stats_reset=0|round=%d|reason=end_without_fresh_ordered_raw|ambiguous=1|gametime=%.3f",
            g_RoundSerial, now);
        // Candidate raw remains available but never extends its first timestamp.
        return;
    }

    bool duplicate = g_RoundPostNavSeen;
    bool ambiguous = duplicate || g_CandidateRawCount > 0;
    if (ambiguous) g_RoundBoundaryAmbiguous = true;
    AssignCandidateRaw("same_round_ambiguous");
    RecordPostNav(now);
    if (!duplicate)
    {
        ReanchorRound(g_RoundAnchorTime, g_RoundAnchorOrigin, CONFIRM_BY_POST_NAV, "first_post_nav_anchor_kept");
    }
    LogV2("ROUND_POST_NAV_DECISION|decision=%s|stats_reset=0|round=%d|reason=%s|ambiguous=%d|anchor_kept=1|gametime=%.3f",
        duplicate ? "duplicate" : "confirm_existing", g_RoundSerial,
        duplicate ? "no_confirmed_new_boundary" : "first_post_nav", ambiguous ? 1 : 0, now);
    LogRoundTimes("post_nav");
}

public void Event_RoundFreezeEnd(Event event, const char[] name, bool dontBroadcast)
{
    LogV2("ROUND_MARKER|marker=round_freeze_end|round=%d|round_uid=%s|gametime=%.3f|active=%d|ended=%d",
        g_RoundSerial, g_RoundUid, GetGameTime(), g_RoundActive ? 1 : 0, g_RoundEnded ? 1 : 0);
}

public void Event_PlayerBotReplace(Event event, const char[] name, bool dontBroadcast)
{
    if (event == null)
    {
        LogV2("EVENT_NULL|event=player_bot_replace|gametime=%.3f", GetGameTime());
        return;
    }

    TryTransferTankOwnership(event.GetInt("player", 0), event.GetInt("bot", 0), "player_to_bot");
}

public void Event_BotPlayerReplace(Event event, const char[] name, bool dontBroadcast)
{
    if (event == null)
    {
        LogV2("EVENT_NULL|event=bot_player_replace|gametime=%.3f", GetGameTime());
        return;
    }

    TryTransferTankOwnership(event.GetInt("bot", 0), event.GetInt("player", 0), "bot_to_player");
}

public void Event_RoundEnd(Event event, const char[] name, bool dontBroadcast)
{
    float now = GetGameTime();
    int endOrder = ++g_BoundaryOrder;
    int winner = -1;
    int reason = -1;
    float eventTime = -1.0;

    if (event != null)
    {
        winner = event.GetInt("winner", -1);
        reason = event.GetInt("reason", -1);
        eventTime = event.GetFloat("time", -1.0);
    }
    else
    {
        LogV2("EVENT_NULL|event=round_end|gametime=%.3f", now);
    }

    if (!CanObserveMapActivity("round_end"))
    {
        LogV2("ROUND_END|round=0|round_uid=none|gametime=%.3f|winner=%d|reason=%d|event_time=%.3f|duplicate=0|note=map_not_active|tracked_alive=%d|confirmed_alive=%d|event_order=%d|attribution=unassigned",
            now, winner, reason, eventTime, CountTrackedTanks(), CountConfirmedTanks(), endOrder);
        return;
    }
    char roundDt[32];
    char sinceMap[32];
    FormatDelta(roundDt, sizeof(roundDt), DeltaOrNeg(now, g_RoundAnchorValid, g_RoundAnchorTime));
    FormatDelta(sinceMap, sizeof(sinceMap), DeltaOrNeg(now, g_MapStartValid, g_MapStartTime));

    if (!g_RoundActive)
    {
        LogV2("ROUND_END|round=0|round_uid=none|gametime=%.3f|winner=%d|reason=%d|event_time=%.3f|duplicate=0|note=no_active_round|tracked_alive=%d|confirmed_alive=%d|event_order=%d",
            now, winner, reason, eventTime, CountTrackedTanks(), CountConfirmedTanks(), endOrder);
        return;
    }

    if (g_RoundEnded)
    {
        LogV2("ROUND_END|round=%d|round_uid=%s|gametime=%.3f|winner=%d|reason=%d|event_time=%.3f|duplicate=1|tracked_alive=%d|confirmed_alive=%d|event_order=%d",
            g_RoundSerial, g_RoundUid, now, winner, reason, eventTime,
            CountTrackedTanks(), CountConfirmedTanks(), endOrder);
        return;
    }

    g_RoundEnded = true;
    g_HasRoundEndTime = true;
    g_RoundEndTime = now;
    g_RoundEndOrder = endOrder;
    // Pre-end raw cannot be recycled into a post-end boundary.
    AssignCandidateRaw("before_round_end");

    LogV2("ROUND_END|round=%d|round_uid=%s|gametime=%.3f|since_map=%s|round_dt=%s|winner=%d|reason=%d|event_time=%.3f|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d|spawns=%d|confirmed_spawns=%d|max_tracked_alive=%d|max_confirmed_alive=%d|safe_events=%d|duplicate=0|event_order=%d",
        g_RoundSerial,
        g_RoundUid,
        now,
        sinceMap,
        roundDt,
        winner,
        reason,
        eventTime,
        CountTrackedTanks(),
        CountConfirmedTanks(),
        CountPendingTanks(),
        CountUnconfirmedTanks(),
        g_RoundTankSpawns,
        g_RoundTankConfirmed,
        g_RoundMaxTracked,
        g_RoundMaxConfirmed,
        g_RoundSafeExitEvents, endOrder);
}

public void Event_PlayerLeftSafeArea(Event event, const char[] name, bool dontBroadcast)
{
    if (event == null)
    {
        LogV2("EVENT_NULL|event=player_left_safe_area|gametime=%.3f", GetGameTime());
        return;
    }

    float now = GetGameTime();
    int userid = event.GetInt("userid", 0);
    int playercount = event.GetInt("playercount", -1);
    int client = ClientForUserId(userid);
    bool inGame = IsClientInGameSafe(client);
    bool alive = inGame ? IsPlayerAlive(client) : false;
    int team = inGame ? GetClientTeam(client) : -1;
    int zombieClass = inGame ? GetClientZombieClass(client) : -1;
    bool isBot = inGame ? IsFakeClient(client) : false;
    bool isSurvivor = inGame && alive && team == 2;


    LogV2("SAFE_EXIT_RAW|userid=%d|playercount=%d|client=%d|client_in_game=%d|is_survivor=%d|gametime=%.3f|round_context=%d|attribution=pending",
        userid, playercount, client, inGame ? 1 : 0, isSurvivor ? 1 : 0, now, g_RoundSerial);
    if (!CanObserveMapActivity("player_left_safe_area"))
    {
        LogV2("SAFE_EXIT_REJECT|userid=%d|gametime=%.3f|reason=map_not_active|ambiguous=1|stats_reset=0|attribution=unassigned", userid, now);
        return;
    }
    if ((!g_RoundActive || g_RoundEnded) && (!isSurvivor || !EnsureRoundForActivity(now, "player_left_safe_area")))
    {
        LogV2("SAFE_EXIT_REJECT|userid=%d|gametime=%.3f|reason=not_eligible_for_round|ambiguous=1|stats_reset=0", userid, now);
        return;
    }
    g_SafeExitEventSeq++;
    g_RoundSafeExitEvents++;

    bool firstEvent = (userid > 0 && !g_RoundFirstSafeEventSeen);
    if (firstEvent)
    {
        g_RoundFirstSafeEventSeen = true;
        g_RoundFirstSafeEventTime = now;
        g_RoundFirstSafeEventUserId = userid;
        g_RoundFirstSafeEventClient = client;
        g_RoundFirstSafeEventBot = isBot;
        g_RoundFirstSafeEventPlayerCount = playercount;
    }

    bool firstConfirmed = (isSurvivor && !g_RoundFirstSafeConfirmedSeen);
    if (firstConfirmed)
    {
        g_RoundFirstSafeConfirmedSeen = true;
        g_RoundFirstSafeConfirmedTime = now;
        g_RoundFirstSafeConfirmedUserId = userid;
        g_RoundFirstSafeConfirmedClient = client;
        g_RoundFirstSafeConfirmedBot = isBot;
    }

    float safeDt = g_RoundFirstSafeEventSeen
        ? DeltaOrNeg(now, true, g_RoundFirstSafeEventTime)
        : -1.0;
    float confirmedSafeDt = g_RoundFirstSafeConfirmedSeen
        ? DeltaOrNeg(now, true, g_RoundFirstSafeConfirmedTime)
        : -1.0;

    char roundDt[32];
    char safeDelta[32];
    char confirmedSafeDelta[32];
    char sinceMap[32];
    FormatDelta(roundDt, sizeof(roundDt), DeltaOrNeg(now, g_RoundAnchorValid, g_RoundAnchorTime));
    FormatDelta(safeDelta, sizeof(safeDelta), safeDt);
    FormatDelta(confirmedSafeDelta, sizeof(confirmedSafeDelta), confirmedSafeDt);
    FormatDelta(sinceMap, sizeof(sinceMap), DeltaOrNeg(now, g_MapStartValid, g_MapStartTime));

    LogV2("SAFE_EXIT|seq=%d|round=%d|round_uid=%s|userid=%d|client=%d|client_in_game=%d|client_alive=%d|team=%d|zombie_class=%d|is_survivor=%d|bot=%d|playercount=%d|first_event=%d|first_confirmed=%d|gametime=%.3f|since_map=%s|round_dt=%s|safe_dt=%s|confirmed_safe_dt=%s|events=%d|first_event_userid=%d|first_confirmed_userid=%d|first_event_bot=%d|first_confirmed_bot=%d",
        g_SafeExitEventSeq,
        g_RoundSerial,
        g_RoundUid,
        userid,
        client,
        inGame ? 1 : 0,
        alive ? 1 : 0,
        team,
        zombieClass,
        isSurvivor ? 1 : 0,
        isBot ? 1 : 0,
        playercount,
        firstEvent ? 1 : 0,
        firstConfirmed ? 1 : 0,
        now,
        sinceMap,
        roundDt,
        safeDelta,
        confirmedSafeDelta,
        g_RoundSafeExitEvents,
        g_RoundFirstSafeEventUserId,
        g_RoundFirstSafeConfirmedUserId,
        g_RoundFirstSafeEventBot ? 1 : 0,
        g_RoundFirstSafeConfirmedBot ? 1 : 0);
}

public void Event_TankSpawn(Event event, const char[] name, bool dontBroadcast)
{
    if (event == null)
    {
        LogV2("EVENT_NULL|event=tank_spawn|gametime=%.3f", GetGameTime());
        return;
    }

    float now = GetGameTime();
    int eventSeq = ++g_TankEventSeq;
    int userid = event.GetInt("userid", 0);
    int tankid = event.GetInt("tankid", 0);
    int client = ClientForUserId(userid);
    bool inGame = IsClientInGameSafe(client);
    int team = inGame ? GetClientTeam(client) : -1;
    int zombieClass = inGame ? GetClientZombieClass(client) : -1;
    bool clientIsTank = IsTankClient(client);
    int entityRefValid = (tankid > 0 && IsValidEntity(tankid)) ? 1 : 0;
    int tankidInClientRange = (tankid >= 1 && tankid <= MaxClients) ? 1 : 0;
    int tankidClientInGame = (tankid >= 1 && tankid <= MaxClients && IsClientInGame(tankid)) ? 1 : 0;
    int tankidIsTank = (tankid >= 1 && tankid <= MaxClients && IsTankClient(tankid)) ? 1 : 0;
    int clientMatch = (client > 0 && tankid > 0 && client == tankid) ? 1 : 0;


    LogV2("TANK_SPAWN_RAW|evseq=%d|userid=%d|tankid=%d|client=%d|client_in_game=%d|client_class=%d|client_ghost=%d|client_is_tank=%d|tankid_entity_valid=%d|tankid_in_client_range=%d|tankid_client_in_game=%d|tankid_is_tank=%d|tankid_class=%d|tankid_ghost=%d|gametime=%.3f|round_context=%d|attribution=pending",
        eventSeq, userid, tankid, client, inGame ? 1 : 0, zombieClass, GetClientGhostState(client), clientIsTank ? 1 : 0, entityRefValid, tankidInClientRange, tankidClientInGame, tankidIsTank, GetClientZombieClass(tankid), GetClientGhostState(tankid), now, g_RoundSerial);
    if (!CanObserveMapActivity("tank_spawn"))
    {
        LogV2("TANK_SPAWN|evseq=%d|accepted=0|reason=map_not_active|userid=%d|tankid=%d|client=%d|gametime=%.3f|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d|ambiguous=1|attribution=unassigned",
            eventSeq, userid, tankid, client, now, CountTrackedTanks(), CountConfirmedTanks(), CountPendingTanks(), CountUnconfirmedTanks());
        return;
    }
    if (userid > 0 && (!g_RoundActive || g_RoundEnded))
    {
        bool oldIdentity = g_RoundEnded && (FindTankByUserId(userid) >= 0 || FindTankByClient(client) >= 0
            || FindTankByTankId(tankid) >= 0 || FindTransferReservation(userid) >= 0);
        if (oldIdentity || !EnsureRoundForActivity(now, "tank_spawn"))
        {
            LogV2("TANK_SPAWN|evseq=%d|accepted=0|reason=ambiguous_round_activity|userid=%d|tankid=%d|client=%d|gametime=%.3f|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d|ambiguous=1",
                eventSeq, userid, tankid, client, now, CountTrackedTanks(), CountConfirmedTanks(), CountPendingTanks(), CountUnconfirmedTanks());
            return;
        }
    }
    int trackedAtEntry = CountTrackedTanks();
    int confirmedAtEntry = CountConfirmedTanks();
    int staleClosed = 0;
    int trackedBefore = CountTrackedTanks();
    int confirmedBefore = CountConfirmedTanks();

    float roundDt = DeltaOrNeg(now, g_RoundAnchorValid, g_RoundAnchorTime);
    float safeDt = DeltaOrNeg(now, g_RoundFirstSafeEventSeen, g_RoundFirstSafeEventTime);
    float confirmedSafeDt = DeltaOrNeg(now, g_RoundFirstSafeConfirmedSeen, g_RoundFirstSafeConfirmedTime);
    float intervalEvent = DeltaOrNeg(now, g_RoundHasLastTankSpawn, g_RoundLastTankSpawnTime);
    float intervalGlobal = DeltaOrNeg(now, g_HasGlobalLastTankSpawn, g_GlobalLastTankSpawnTime);
    int previousSpawnSeq = g_RoundHasLastTankSpawn ? g_RoundLastTankSpawnSeq : 0;
    int previousGlobalSeq = g_HasGlobalLastTankSpawn ? g_GlobalLastTankSpawnSeq : 0;

    char roundDelta[32];
    char safeDelta[32];
    char confirmedSafeDelta[32];
    char intervalDelta[32];
    char intervalGlobalDelta[32];
    char sinceMap[32];
    FormatDelta(roundDelta, sizeof(roundDelta), roundDt);
    FormatDelta(safeDelta, sizeof(safeDelta), safeDt);
    FormatDelta(confirmedSafeDelta, sizeof(confirmedSafeDelta), confirmedSafeDt);
    FormatDelta(intervalDelta, sizeof(intervalDelta), intervalEvent);
    FormatDelta(intervalGlobalDelta, sizeof(intervalGlobalDelta), intervalGlobal);
    FormatDelta(sinceMap, sizeof(sinceMap), DeltaOrNeg(now, g_MapStartValid, g_MapStartTime));

    if (userid <= 0)
    {
        LogV2("TANK_SPAWN|evseq=%d|accepted=0|reason=userid_missing|round=%d|round_uid=%s|userid=%d|tankid=%d|client=%d|client_in_game=%d|client_team=%d|client_class=%d|client_alive=%d|client_is_tank=%d|bot=%d|tankid_entity_valid=%d|tankid_in_client_range=%d|tankid_client_in_game=%d|tankid_is_tank=%d|tankid_equals_resolved_client=%d|gametime=%.3f|since_map=%s|round_dt=%s|safe_dt=%s|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d|stale_closed=%d|prev_seq=%d|prev_global_seq=%d",
            eventSeq,
            g_RoundSerial,
            g_RoundUid,
            userid,
            tankid,
            client,
            inGame ? 1 : 0,
            team,
            zombieClass,
            (inGame && IsPlayerAlive(client)) ? 1 : 0,
            clientIsTank ? 1 : 0,
            (inGame && IsFakeClient(client)) ? 1 : 0,
            entityRefValid,
            tankidInClientRange,
            tankidClientInGame,
            tankidIsTank,
            clientMatch,
            now,
            sinceMap,
            roundDelta,
            safeDelta,
            CountTrackedTanks(),
            CountConfirmedTanks(),
            CountPendingTanks(),
            CountUnconfirmedTanks(),
            staleClosed,
            previousSpawnSeq,
            previousGlobalSeq);
        return;
    }


    int slotByUser = FindTankByUserId(userid);
    int slotByTank = FindTankByTankId(tankid);
    int slotByClient = FindTankByClient(client);
    int reserved = FindTransferReservation(userid);
    bool conflictResolved = false;
    bool compatibleTank = slotByUser >= 0 && (tankid <= 0 || g_TankTankId[slotByUser] <= 0 || g_TankTankId[slotByUser] == tankid);
    bool compatibleClient = slotByUser >= 0 && (client <= 0 || g_TankClient[slotByUser] <= 0 || g_TankClient[slotByUser] == client);
    bool duplicate = slotByUser >= 0 && compatibleTank && compatibleClient
        && (slotByTank < 0 || slotByTank == slotByUser)
        && (slotByClient < 0 || slotByClient == slotByUser)
        && (reserved < 0 || reserved == slotByUser);
    if (duplicate)
    {
        bool enriched = g_TankTankId[slotByUser] <= 0 && tankid > 0;
        if (enriched) g_TankTankId[slotByUser] = tankid;
        // Duplicate events never clear a disconnect/transfer deadline.
        if (client > 0 && !g_TankDisconnectPending[slotByUser]) g_TankClient[slotByUser] = client;
        EvaluateTankSlot(slotByUser, now, true);
        UpdateMaxAlive();
        char stateName[16];
        GetTankStateName(g_TankState[slotByUser], stateName, sizeof(stateName));
        LogV2("TANK_SPAWN|evseq=%d|accepted=0|reason=duplicate_userid_tankid|round=%d|round_uid=%s|userid=%d|tankid=%d|client=%d|enriched=%d|spawn_seq=%d|spawn_state=%s|gametime=%.3f|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d",
            eventSeq, g_RoundSerial, g_RoundUid, userid, tankid, client, enriched ? 1 : 0,
            g_TankRecordSpawnSeq[slotByUser], stateName, now, CountTrackedTanks(), CountConfirmedTanks(), CountPendingTanks(), CountUnconfirmedTanks());
        LogStateIfChanged(now);
        return;
    }
    if (slotByUser >= 0 || slotByTank >= 0 || slotByClient >= 0 || reserved >= 0)
    {
        char reason[48];
        int distinct = 0;
        if (slotByUser >= 0) distinct++;
        if (slotByTank >= 0 && slotByTank != slotByUser) distinct++;
        if (slotByClient >= 0 && slotByClient != slotByUser && slotByClient != slotByTank) distinct++;
        if (distinct > 1) strcopy(reason, sizeof(reason), "multi_key_conflict");
        else if (reserved >= 0) strcopy(reason, sizeof(reason), "transfer_reserved");
        else if (slotByUser >= 0) strcopy(reason, sizeof(reason), compatibleTank ? "userid_client_conflict" : "userid_tankid_conflict");
        else if (slotByClient >= 0) strcopy(reason, sizeof(reason), "client_conflict");
        else strcopy(reason, sizeof(reason), "tankid_conflict");
        LogSpawnConflict(eventSeq, userid, tankid, client, slotByUser, slotByTank, slotByClient, reserved, reason, now);
        return;
    }
    trackedBefore = CountTrackedTanks();
    confirmedBefore = CountConfirmedTanks();

    int slot = FindFreeTankSlot();
    if (slot < 0)
    {
        LogV2("TANK_SPAWN|evseq=%d|accepted=0|reason=registry_full|round=%d|round_uid=%s|userid=%d|tankid=%d|client=%d|gametime=%.3f|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d",
            eventSeq,
            g_RoundSerial,
            g_RoundUid,
            userid,
            tankid,
            client,
            now,
            CountTrackedTanks(),
            CountConfirmedTanks(),
            CountPendingTanks(),
            CountUnconfirmedTanks());
        return;
    }

    int spawnSeq = ++g_TankSpawnSeq;
    ResetTankSlot(slot);
    g_TankActive[slot] = true;
    g_TankState[slot] = clientIsTank ? TANK_STATE_CONFIRMED : TANK_STATE_PENDING;
    g_TankUserId[slot] = userid;
    g_TankTankId[slot] = tankid;
    g_TankClient[slot] = client;
    g_TankRecordSpawnSeq[slot] = spawnSeq;
    g_TankSpawnRound[slot] = g_RoundSerial;
    g_TankSpawnTime[slot] = now;
    g_TankLastValidTime[slot] = clientIsTank ? now : 0.0;
    g_TankInvalidSince[slot] = 0.0;
    g_TankEverConfirmed[slot] = clientIsTank;
    g_TankDisconnectPending[slot] = false;
    g_TankDisconnectSince[slot] = 0.0;

    g_RoundTankSpawns++;
    int roundSeq = g_RoundTankSpawns;
    if (clientIsTank)
    {
        g_RoundTankConfirmed++;
    }

    g_RoundHasLastTankSpawn = true;
    g_RoundLastTankSpawnTime = now;
    g_RoundLastTankSpawnSeq = spawnSeq;
    g_HasGlobalLastTankSpawn = true;
    g_GlobalLastTankSpawnTime = now;
    g_GlobalLastTankSpawnSeq = spawnSeq;

    UpdateMaxAlive();

    char stateName[16];
    GetTankStateName(g_TankState[slot], stateName, sizeof(stateName));

    LogV2("TANK_SPAWN|evseq=%d|accepted=1|seq=%d|round=%d|round_uid=%s|round_seq=%d|userid=%d|tankid=%d|client=%d|client_in_game=%d|client_team=%d|client_class=%d|client_alive=%d|client_is_tank=%d|bot=%d|gametime=%.3f|since_map=%s|round_dt=%s|safe_dt=%s|confirmed_safe_dt=%s|interval_event=%s|interval_global=%s|prev_seq=%d|prev_global_seq=%d|spawn_state=%s|tracked_before=%d|tracked_alive=%d|confirmed_before=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d|max_tracked_alive=%d|max_confirmed_alive=%d|stale_closed=%d|conflict_resolved=%d|entry_tracked=%d|entry_confirmed=%d|tankid_entity_valid=%d|tankid_in_client_range=%d|tankid_client_in_game=%d|tankid_is_tank=%d|tankid_equals_resolved_client=%d",
        eventSeq,
        spawnSeq,
        g_RoundSerial,
        g_RoundUid,
        roundSeq,
        userid,
        tankid,
        client,
        inGame ? 1 : 0,
        team,
        zombieClass,
        (inGame && IsPlayerAlive(client)) ? 1 : 0,
        clientIsTank ? 1 : 0,
        (inGame && IsFakeClient(client)) ? 1 : 0,
        now,
        sinceMap,
        roundDelta,
        safeDelta,
        confirmedSafeDelta,
        intervalDelta,
        intervalGlobalDelta,
        previousSpawnSeq,
        previousGlobalSeq,
        stateName,
        trackedBefore,
        CountTrackedTanks(),
        confirmedBefore,
        CountConfirmedTanks(),
        CountPendingTanks(),
        CountUnconfirmedTanks(),
        g_RoundMaxTracked,
        g_RoundMaxConfirmed,
        staleClosed,
        conflictResolved ? 1 : 0,
        trackedAtEntry,
        confirmedAtEntry,
        entityRefValid,
        tankidInClientRange,
        tankidClientInGame,
        tankidIsTank,
        clientMatch);
    LogStateIfChanged(now);
}

public void Event_TankKilled(Event event, const char[] name, bool dontBroadcast)
{
    if (event == null)
    {
        LogV2("EVENT_NULL|event=tank_killed|gametime=%.3f", GetGameTime());
        return;
    }

    float now = GetGameTime();
    int eventSeq = ++g_TankKillSeq;
    int userid = event.GetInt("userid", 0);
    int attacker = event.GetInt("attacker", 0);
    bool solo = event.GetBool("solo", false);
    bool meleeOnly = event.GetBool("melee_only", false);
    int client = ClientForUserId(userid);

    int trackedBefore = CountTrackedTanks();
    int confirmedBefore = CountConfirmedTanks();
    LogPendingTransferDeath(userid, eventSeq, now);
    int matches = CountTankMatchesByUserId(userid);
    int slot = FindTankByUserId(userid);

    char roundDt[32];
    char safeDt[32];
    char confirmedSafeDt[32];
    char sinceMap[32];
    FormatDelta(roundDt, sizeof(roundDt), DeltaOrNeg(now, g_RoundAnchorValid, g_RoundAnchorTime));
    FormatDelta(safeDt, sizeof(safeDt), DeltaOrNeg(now, g_RoundFirstSafeEventSeen, g_RoundFirstSafeEventTime));
    FormatDelta(confirmedSafeDt, sizeof(confirmedSafeDt),
        DeltaOrNeg(now, g_RoundFirstSafeConfirmedSeen, g_RoundFirstSafeConfirmedTime));
    FormatDelta(sinceMap, sizeof(sinceMap), DeltaOrNeg(now, g_MapStartValid, g_MapStartTime));

    if (slot < 0 || matches != 1)
    {
        LogV2("TANK_KILLED|evseq=%d|matched=0|round=%d|round_uid=%s|userid=%d|attacker=%d|client=%d|userid_matches=%d|gametime=%.3f|since_map=%s|round_dt=%s|safe_dt=%s|tracked_before=%d|tracked_alive=%d|confirmed_before=%d|confirmed_alive=%d|delta=0|solo=%d|melee_only=%d|reason=%s",
            eventSeq,
            g_RoundSerial,
            g_RoundUid,
            userid,
            attacker,
            client,
            matches,
            now,
            sinceMap,
            roundDt,
            safeDt,
            trackedBefore,
            CountTrackedTanks(),
            confirmedBefore,
            CountConfirmedTanks(),
            solo ? 1 : 0,
            meleeOnly ? 1 : 0,
            userid <= 0 ? "userid_missing" : (matches > 1 ? "multiple_userid_records" : "no_active_match"));
        return;
    }

    int storedTankId = g_TankTankId[slot];
    int storedClient = g_TankClient[slot];
    int spawnSeq = g_TankRecordSpawnSeq[slot];
    int spawnRound = g_TankSpawnRound[slot];
    int state = g_TankState[slot];
    bool everConfirmed = g_TankEverConfirmed[slot];
    float spawnTime = g_TankSpawnTime[slot];
    float lastValidTime = g_TankLastValidTime[slot];
    int entityMatch = (client > 0 && storedTankId > 0 && client == storedTankId) ? 1 : 0;

    char stateName[16];
    char lifetime[32];
    char lastValidDelta[32];
    GetTankStateName(state, stateName, sizeof(stateName));
    FormatDelta(lifetime, sizeof(lifetime), DeltaOrNeg(now, true, spawnTime));
    FormatDelta(lastValidDelta, sizeof(lastValidDelta),
        DeltaOrNeg(now, lastValidTime > 0.0, lastValidTime));

    ResetTankSlot(slot);

    int trackedAfter = CountTrackedTanks();
    int confirmedAfter = CountConfirmedTanks();

    LogV2("TANK_KILLED|evseq=%d|matched=1|round=%d|round_uid=%s|userid=%d|attacker=%d|client=%d|stored_tankid=%d|stored_client=%d|tankid_equals_resolved_client=%d|spawn_seq=%d|spawn_round=%d|spawn_state=%s|ever_confirmed=%d|gametime=%.3f|since_map=%s|round_dt=%s|safe_dt=%s|confirmed_safe_dt=%s|lifetime=%s|last_valid_dt=%s|tracked_before=%d|tracked_alive=%d|tracked_delta=%d|confirmed_before=%d|confirmed_alive=%d|spawn_never_confirmed=%d|solo=%d|melee_only=%d",
        eventSeq,
        g_RoundSerial,
        g_RoundUid,
        userid,
        attacker,
        client,
        storedTankId,
        storedClient,
        entityMatch,
        spawnSeq,
        spawnRound,
        stateName,
        everConfirmed ? 1 : 0,
        now,
        sinceMap,
        roundDt,
        safeDt,
        confirmedSafeDt,
        lifetime,
        lastValidDelta,
        trackedBefore,
        trackedAfter,
        trackedAfter - trackedBefore,
        confirmedBefore,
        confirmedAfter,
        everConfirmed ? 0 : 1,
        solo ? 1 : 0,
        meleeOnly ? 1 : 0);

    UpdateMaxAlive();
    LogStateIfChanged(now);
}

bool StartReconcileTimer()
{
    if (g_ReconcileTimer != null)
    {
        return true;
    }

    g_ReconcileTimer = CreateTimer(TANK_RECONCILE_INTERVAL, Timer_Reconcile,
        INVALID_HANDLE, TIMER_REPEAT);
    if (g_ReconcileTimer == null)
    {
        LogV2("TIMER_FAIL|timer=reconcile|interval=%.1f", TANK_RECONCILE_INTERVAL);
        SetFailState("Could not create the Tank reconciliation timer.");
        return false;
    }
    return true;
}

public Action Timer_Reconcile(Handle timer, any data)
{
    if (g_LogPath[0] == '\0')
    {
        return Plugin_Continue;
    }

    ReconcileTanks(GetGameTime());
    return Plugin_Continue;
}

void ReconcileTanks(float now)
{
    if (g_MapState != MAP_STATE_ACTIVE) return;
    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (!g_TankActive[i]) continue;
        EvaluateTankSlot(i, now, true);
        // Even a delayed timer must expose elapsed pending grace before a
        // transfer/disconnect timeout can close this lifecycle.
        if (g_TankState[i] == TANK_STATE_PENDING && now - g_TankSpawnTime[i] >= TANK_PENDING_GRACE)
        {
            g_TankState[i] = TANK_STATE_UNCONFIRMED;
            char reason[48];
            GetTankInvalidReason(i, reason, sizeof(reason));
            LogV2("TANK_UNCONFIRMED|spawn_seq=%d|userid=%d|tankid=%d|client=%d|reason=%s|gametime=%.3f|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d",
                g_TankRecordSpawnSeq[i], g_TankUserId[i], g_TankTankId[i], g_TankClient[i], reason, now,
                CountTrackedTanks(), CountConfirmedTanks(), CountPendingTanks(), CountUnconfirmedTanks());
        }
        // SourceMod callbacks are serial. Candidate retry always precedes ALL
        // timeout paths, including a disconnect that arrived first.
        if (g_TankTransferPending[i])
        {
            RetryTankTransfer(i, now);
            if (!g_TankActive[i]) continue;
        }
        // Waiting changes closure priority, never the pending state deadline.
        if (g_TankTransferPending[i]) continue;
        if (g_TankDisconnectPending[i])
        {
            if (now - g_TankDisconnectSince[i] >= TANK_DISCONNECT_GRACE)
                CloseTankSlot(i, "client_disconnect_timeout", "TANK_DISCONNECT", now, true);
            continue;
        }
        if (g_TankState[i] == TANK_STATE_UNCONFIRMED && now - g_TankSpawnTime[i] >= TANK_UNCONFIRMED_RETENTION)
            CloseTankSlot(i, "unconfirmed_timeout", "TANK_STALE", now, true);
        else if (g_TankState[i] == TANK_STATE_TRANSIENT && now - g_TankInvalidSince[i] >= TANK_CONFIRM_LOSS_GRACE)
            CloseTankSlot(i, "confirmed_lost_timeout", "TANK_STALE", now, true);
    }
    UpdateMaxAlive();
    LogStateIfChanged(now);
}

void TryTransferTankOwnership(int oldUserId, int newUserId, const char[] kind)
{
    float now = GetGameTime();
    LogV2("OWNER_REPLACE_RAW|kind=%s|old_userid=%d|new_userid=%d|gametime=%.3f", kind, oldUserId, newUserId, now);
    if (!CanObserveMapActivity("owner_replace")) return;
    int slot = FindTankByUserId(oldUserId);
    if (slot < 0 && oldUserId > 0 && newUserId > 0 && oldUserId != newUserId)
    {
        int candidate = FindUniqueTransferReservation(oldUserId);
        if (candidate >= 0 && g_TankUserId[candidate] != newUserId)
        {
            int conflict = FindTankByUserId(newUserId);
            int reserved = FindTransferReservation(newUserId, candidate);
            if (conflict < 0 && reserved < 0)
            {
                g_TankTransferUser[candidate] = newUserId;
                strcopy(g_TankTransferKind[candidate], sizeof(g_TankTransferKind[]), "chain");
                g_TankTransferReason[candidate][0] = '\0';
                LogV2("TANK_OWNER_TRANSFER_CHAIN|spawn_seq=%d|owner_userid=%d|previous_target=%d|new_userid=%d|gametime=%.3f|deadline_kept=1",
                    g_TankRecordSpawnSeq[candidate], g_TankUserId[candidate], oldUserId, newUserId, now);
                RetryTankTransfer(candidate, now);
                LogStateIfChanged(now);
                return;
            }
            LogV2("TANK_OWNER_TRANSFER_CONFLICT|spawn_seq=%d|old_userid=%d|new_userid=%d|reason=chain_target_occupied|gametime=%.3f",
                g_TankRecordSpawnSeq[candidate], oldUserId, newUserId, now);
            return;
        }
    }
    if (oldUserId <= 0 || newUserId <= 0 || oldUserId == newUserId || slot < 0)
    {
        LogV2("TANK_OWNER_TRANSFER_FAIL|kind=%s|old_userid=%d|new_userid=%d|reason=%s|gametime=%.3f",
            kind, oldUserId, newUserId, oldUserId <= 0 || newUserId <= 0 ? "missing_userid" : (oldUserId == newUserId ? "same_userid" : "no_active_old_record"), now);
        return;
    }
    if (g_TankTransferPending[slot])
    {
        LogV2("TANK_OWNER_TRANSFER_CANDIDATE|spawn_seq=%d|old_userid=%d|new_userid=%d|duplicate=%d|reason=%s|gametime=%.3f|deadline_kept=1",
            g_TankRecordSpawnSeq[slot], oldUserId, newUserId, g_TankTransferUser[slot] == newUserId ? 1 : 0,
            g_TankTransferUser[slot] == newUserId ? "same_candidate" : "different_candidate_conflict", now);
        return;
    }
    g_TankTransferPending[slot] = true;
    g_TankTransferUser[slot] = newUserId;
    g_TankTransferSince[slot] = now;
    strcopy(g_TankTransferKind[slot], sizeof(g_TankTransferKind[]), kind);
    g_TankTransferReason[slot][0] = '\0';
    LogV2("TANK_OWNER_TRANSFER_CANDIDATE|spawn_seq=%d|old_userid=%d|new_userid=%d|duplicate=0|reason=created|gametime=%.3f|grace=%.1f",
        g_TankRecordSpawnSeq[slot], oldUserId, newUserId, now, TANK_TRANSFER_GRACE);
    RetryTankTransfer(slot, now);
    LogStateIfChanged(now);
}

bool HookRequired(const char[] eventName, EventHook callback)
{
    if (!HookEventEx(eventName, callback, EventHookMode_Post))
    {
        LogV2("HOOK_FAIL|event=%s|required=1", eventName);
        SetFailState("Required L4D2 event is unavailable: %s", eventName);
        return false;
    }
    LogV2("HOOK_OK|event=%s|required=1", eventName);
    return true;
}

void HookOptional(const char[] eventName, EventHook callback)
{
    if (!HookEventEx(eventName, callback, EventHookMode_Post))
    {
        LogV2("HOOK_FAIL|event=%s|required=0", eventName);
        return;
    }
    LogV2("HOOK_OK|event=%s|required=0", eventName);
}

bool BeginRound(float anchorTime, int anchorOrigin, int confirmedBy, const char[] trigger)
{
    if (!CanObserveMapActivity(trigger)) return false;
    float now = GetGameTime();
    int previous = g_RoundSerial;
    // No candidate raw/post_nav has yet mutated the old round's counters.
    if (g_RoundActive) LogRoundSummary("superseded");
    ClearActiveTanks("round_begin", "TANK_STALE", now, true);
    g_RoundSerial++;
    g_RoundActive = true;
    g_RoundEnded = false;
    BuildRoundUid();
    ResetRoundState();
    g_RoundBoundaryAmbiguous =
        (confirmedBy == CONFIRM_BY_ACTIVITY
         || anchorOrigin == ANCHOR_ORIGIN_POST_NAV);
    g_RoundAnchorTime = anchorTime;
    g_RoundAnchorValid = true;
    g_RoundAnchorOrigin = anchorOrigin;
    g_RoundAnchorConfirmedBy = confirmedBy;
    char origin[24], confirmation[24];
    GetAnchorOriginName(anchorOrigin, origin, sizeof(origin));
    GetConfirmedByName(confirmedBy, confirmation, sizeof(confirmation));
    LogV2("ROUND_BEGIN|round=%d|round_uid=%s|previous_round=%d|trigger=%s|anchor_origin=%s|anchor_confirmed_by=%s|anchor_gametime=%.3f|gametime=%.3f|stats_reset=1|ambiguous=%d",
        g_RoundSerial, g_RoundUid, previous, trigger, origin, confirmation, anchorTime, now,
        g_RoundBoundaryAmbiguous ? 1 : 0);
    AssignCandidateRaw("new_round");
    LogRoundTimes("begin");
    return true;
}

bool EnsureRoundForActivity(float now, const char[] trigger)
{
    if (!CanObserveMapActivity(trigger)) return false;
    if (g_RoundActive && !g_RoundEnded) return true;
    bool freshRaw = HasFreshBoundaryRaw(now);
    if (g_RoundEnded && !freshRaw)
    {
        LogV2("ROUND_ACTIVITY_AMBIGUOUS|trigger=%s|round_context=%d|gametime=%.3f|reason=end_without_fresh_ordered_raw|stats_reset=0|ambiguous=1",
            trigger, g_RoundSerial, now);
        return false;
    }
    LogV2("ROUND_BOUNDARY_DECISION|decision=new|previous_round=%d|evidence=activity_fallback|ambiguous=1|gametime=%.3f",
        g_RoundSerial, now);
    return BeginRound(freshRaw ? g_CandidateRawFirst : now,
        freshRaw ? ANCHOR_ORIGIN_RAW_START : ANCHOR_ORIGIN_IMPLICIT, CONFIRM_BY_ACTIVITY, trigger);
}

void ReanchorRound(float anchor, int origin, int confirmedBy, const char[] trigger)
{
    // Effective anchor is immutable throughout a round, even without activity.
    if (!g_RoundActive || anchor != g_RoundAnchorTime || origin != g_RoundAnchorOrigin)
    {
        LogV2("ROUND_REANCHOR_REJECT|trigger=%s|round=%d|reason=immutable_anchor|stats_reset=0", trigger, g_RoundSerial);
        return;
    }
    g_RoundAnchorConfirmedBy = confirmedBy;
    char source[24];
    GetConfirmedByName(confirmedBy, source, sizeof(source));
    LogV2("ROUND_ANCHOR_CONFIRMED|round=%d|round_uid=%s|trigger=%s|anchor_confirmed_by=%s|anchor_gametime=%.3f|gametime=%.3f|anchor_kept=1|stats_reset=0",
        g_RoundSerial, g_RoundUid, trigger, source, anchor, GetGameTime());
}

void ResetRoundState()
{
    g_RoundAnchorOrigin = ANCHOR_ORIGIN_NONE;
    g_RoundAnchorConfirmedBy = CONFIRM_BY_NONE;
    g_RoundAnchorValid = false;
    g_RoundAnchorTime = 0.0;
    g_RoundHasPostNavTime = false;
    g_RoundPostNavTime = 0.0;
    g_RoundPostNavSeen = false;
    g_RawStartSinceRoundBegin = false;

    g_HasRoundEndTime = false;
    g_RoundEndTime = 0.0;

    g_RoundRawCount = 0;
    g_RoundBoundaryAmbiguous = false;
    g_RoundFirstRawTime = 0.0;
    g_RoundLastRawTime = 0.0;
    g_RoundLastPostNavTime = 0.0;
    g_RoundDupStartCount = 0;
    g_RoundPostNavCount = 0;
    g_RoundDupPostNavCount = 0;

    g_RoundFirstSafeEventSeen = false;
    g_RoundFirstSafeEventTime = 0.0;
    g_RoundFirstSafeEventUserId = 0;
    g_RoundFirstSafeEventClient = 0;
    g_RoundFirstSafeEventBot = false;
    g_RoundFirstSafeEventPlayerCount = -1;

    g_RoundFirstSafeConfirmedSeen = false;
    g_RoundFirstSafeConfirmedTime = 0.0;
    g_RoundFirstSafeConfirmedUserId = 0;
    g_RoundFirstSafeConfirmedClient = 0;
    g_RoundFirstSafeConfirmedBot = false;

    g_RoundSafeExitEvents = 0;

    g_RoundHasLastTankSpawn = false;
    g_RoundLastTankSpawnTime = 0.0;
    g_RoundLastTankSpawnSeq = 0;
    g_RoundTankSpawns = 0;
    g_RoundTankConfirmed = 0;
    g_RoundMaxTracked = 0;
    g_RoundMaxConfirmed = 0;

    g_LastLoggedTracked = -1;
    g_LastLoggedConfirmed = -1;
    g_LastLoggedPending = -1;
    g_LastLoggedUnconfirmed = -1;
}

void LogRoundSummary(const char[] reason)
{
    float now = GetGameTime();
    LogRoundTimes(reason);
    char roundDt[32];
    char sinceMap[32];
    char originName[24];
    char confirmedByName[24];
    char postNavTime[32];
    char anchorAge[32];

    FormatDelta(roundDt, sizeof(roundDt), DeltaOrNeg(now, g_RoundAnchorValid, g_RoundAnchorTime));
    FormatDelta(sinceMap, sizeof(sinceMap), DeltaOrNeg(now, g_MapStartValid, g_MapStartTime));
    GetAnchorOriginName(g_RoundAnchorOrigin, originName, sizeof(originName));
    GetConfirmedByName(g_RoundAnchorConfirmedBy, confirmedByName, sizeof(confirmedByName));
    FormatDelta(postNavTime, sizeof(postNavTime),
        DeltaOrNeg(now, g_RoundHasPostNavTime, g_RoundPostNavTime));
    FormatDelta(anchorAge, sizeof(anchorAge), DeltaOrNeg(now, g_RoundAnchorValid, g_RoundAnchorTime));

    LogV2("ROUND_SUMMARY|reason=%s|map=%s|map_id=%d|round=%d|round_uid=%s|gametime=%.3f|since_map=%s|round_dt=%s|anchor_age=%s|anchor_origin=%s|anchor_confirmed_by=%s|post_nav_age=%s|spawns=%d|confirmed_spawns=%d|max_tracked_alive=%d|max_confirmed_alive=%d|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d|safe_events=%d|first_event=%d|first_event_userid=%d|first_event_client=%d|first_event_bot=%d|first_event_playercount=%d|first_confirmed=%d|first_confirmed_userid=%d|first_confirmed_client=%d|first_confirmed_bot=%d|raw_dup=%d|post_nav=%d|post_nav_dup=%d|ended=%d",
        reason,
        g_MapName,
        g_MapSerial,
        g_RoundSerial,
        g_RoundUid,
        now,
        sinceMap,
        roundDt,
        anchorAge,
        originName,
        confirmedByName,
        postNavTime,
        g_RoundTankSpawns,
        g_RoundTankConfirmed,
        g_RoundMaxTracked,
        g_RoundMaxConfirmed,
        CountTrackedTanks(),
        CountConfirmedTanks(),
        CountPendingTanks(),
        CountUnconfirmedTanks(),
        g_RoundSafeExitEvents,
        g_RoundFirstSafeEventSeen ? 1 : 0,
        g_RoundFirstSafeEventUserId,
        g_RoundFirstSafeEventClient,
        g_RoundFirstSafeEventBot ? 1 : 0,
        g_RoundFirstSafeEventPlayerCount,
        g_RoundFirstSafeConfirmedSeen ? 1 : 0,
        g_RoundFirstSafeConfirmedUserId,
        g_RoundFirstSafeConfirmedClient,
        g_RoundFirstSafeConfirmedBot ? 1 : 0,
        g_RoundDupStartCount,
        g_RoundPostNavCount,
        g_RoundDupPostNavCount,
        g_RoundEnded ? 1 : 0);
}

void BuildRoundUid()
{
    Format(g_RoundUid, sizeof(g_RoundUid), "m%d-r%d", g_MapSerial, g_RoundSerial);
}

int CountTankMatchesByUserId(int userid)
{
    if (userid <= 0)
    {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (g_TankActive[i] && g_TankUserId[i] == userid)
        {
            count++;
        }
    }
    return count;
}

int CountTrackedTanks()
{
    int count = 0;
    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (g_TankActive[i])
        {
            count++;
        }
    }
    return count;
}

int CountConfirmedTanks()
{
    int count = 0;
    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (g_TankActive[i] && g_TankState[i] == TANK_STATE_CONFIRMED
            && IsTankSlotCurrentlyConfirmed(i))
        {
            count++;
        }
    }
    return count;
}

int CountPendingTanks()
{
    int count = 0;
    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (g_TankActive[i] && g_TankState[i] == TANK_STATE_PENDING)
        {
            count++;
        }
    }
    return count;
}

int CountUnconfirmedTanks()
{
    int count = 0;
    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (g_TankActive[i] && (g_TankState[i] == TANK_STATE_UNCONFIRMED || g_TankState[i] == TANK_STATE_TRANSIENT))
        {
            count++;
        }
    }
    return count;
}

bool IsTankSlotCurrentlyConfirmed(int slot)
{
    if (slot < 0 || slot >= MAX_ACTIVE_TANKS || !g_TankActive[slot])
    {
        return false;
    }

    int client = ClientForUserId(g_TankUserId[slot]);
    if (g_TankDisconnectPending[slot] || client <= 0) return false;
    if (g_TankClient[slot] > 0 && g_TankClient[slot] != client) return false;
    int owner = FindTankByClient(client);
    if (owner >= 0 && owner != slot) return false;
    return IsTankClient(client);
}

void UpdateMaxAlive()
{
    int tracked = CountTrackedTanks();
    int confirmed = CountConfirmedTanks();

    if (tracked > g_RoundMaxTracked)
    {
        g_RoundMaxTracked = tracked;
    }
    if (confirmed > g_RoundMaxConfirmed)
    {
        g_RoundMaxConfirmed = confirmed;
    }
}

void LogStateIfChanged(float now)
{
    int tracked = CountTrackedTanks();
    int confirmed = CountConfirmedTanks();
    int pending = CountPendingTanks();
    int unconfirmed = CountUnconfirmedTanks();

    if (tracked == g_LastLoggedTracked
        && confirmed == g_LastLoggedConfirmed
        && pending == g_LastLoggedPending
        && unconfirmed == g_LastLoggedUnconfirmed)
    {
        return;
    }

    g_LastLoggedTracked = tracked;
    g_LastLoggedConfirmed = confirmed;
    g_LastLoggedPending = pending;
    g_LastLoggedUnconfirmed = unconfirmed;

    LogV2("TANK_STATE|round=%d|round_uid=%s|gametime=%.3f|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d|max_tracked_alive=%d|max_confirmed_alive=%d|confirmed_semantics=client_state_sample|confirmed_records=%d|transient=%d",
        g_RoundSerial,
        g_RoundUid,
        now,
        tracked,
        confirmed,
        pending,
        unconfirmed,
        g_RoundMaxTracked,
        g_RoundMaxConfirmed,
        CountTankState(TANK_STATE_CONFIRMED),
        CountTankState(TANK_STATE_TRANSIENT));
}

int FindFreeTankSlot()
{
    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (!g_TankActive[i])
        {
            return i;
        }
    }
    return -1;
}

int FindTankByUserId(int userid)
{
    if (userid <= 0)
    {
        return -1;
    }

    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (g_TankActive[i] && g_TankUserId[i] == userid)
        {
            return i;
        }
    }
    return -1;
}

int FindTankByTankId(int tankid)
{
    if (tankid <= 0)
    {
        return -1;
    }

    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (g_TankActive[i] && g_TankTankId[i] == tankid)
        {
            return i;
        }
    }
    return -1;
}

int FindTankByClient(int client)
{
    if (client <= 0)
    {
        return -1;
    }

    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (g_TankActive[i] && g_TankClient[i] == client)
        {
            return i;
        }
    }
    return -1;
}

void EvaluateTankSlot(int slot, float now, bool logTransition)
{
    if (slot < 0 || slot >= MAX_ACTIVE_TANKS || !g_TankActive[slot]) return;
    bool confirmed = IsTankSlotCurrentlyConfirmed(slot);
    int oldState = g_TankState[slot];
    if (confirmed)
    {
        int client = ClientForUserId(g_TankUserId[slot]);
        if (g_TankClient[slot] <= 0) g_TankClient[slot] = client;
        g_TankState[slot] = TANK_STATE_CONFIRMED;
        g_TankLastValidTime[slot] = now;
        g_TankInvalidSince[slot] = 0.0;
        if (!g_TankEverConfirmed[slot])
        {
            g_TankEverConfirmed[slot] = true;
            g_RoundTankConfirmed++;
        }
        if (logTransition && oldState != TANK_STATE_CONFIRMED)
        {
            char previous[16];
            GetTankStateName(oldState, previous, sizeof(previous));
            LogV2("%s|spawn_seq=%d|userid=%d|client=%d|previous_state=%s|gametime=%.3f|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d",
                oldState == TANK_STATE_TRANSIENT ? "TANK_RECONFIRM" : "TANK_CONFIRM",
                g_TankRecordSpawnSeq[slot], g_TankUserId[slot], client, previous, now,
                CountTrackedTanks(), CountConfirmedTanks(), CountPendingTanks(), CountUnconfirmedTanks());
        }
    }
    else if (oldState == TANK_STATE_CONFIRMED)
    {
        g_TankState[slot] = TANK_STATE_TRANSIENT;
        g_TankInvalidSince[slot] = now;
        char reason[48];
        GetTankInvalidReason(slot, reason, sizeof(reason));
        LogV2("TANK_UNCONFIRM_TRANSIENT|spawn_seq=%d|userid=%d|client=%d|reason=%s|gametime=%.3f|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d|grace=%.1f",
            g_TankRecordSpawnSeq[slot], g_TankUserId[slot], g_TankClient[slot], reason, now,
            CountTrackedTanks(), CountConfirmedTanks(), CountPendingTanks(), CountUnconfirmedTanks(), TANK_CONFIRM_LOSS_GRACE);
    }
}

void CloseTankSlot(int slot, const char[] reason, const char[] logType, float now, bool writeLog)
{
    if (slot < 0 || slot >= MAX_ACTIVE_TANKS || !g_TankActive[slot])
    {
        return;
    }

    int trackedBefore = CountTrackedTanks();
    int confirmedBefore = CountConfirmedTanks();
    int pendingBefore = CountPendingTanks();
    int unconfirmedBefore = CountUnconfirmedTanks();

    int state = g_TankState[slot];
    int userid = g_TankUserId[slot];
    int tankid = g_TankTankId[slot];
    int client = g_TankClient[slot];
    int spawnSeq = g_TankRecordSpawnSeq[slot];
    int spawnRound = g_TankSpawnRound[slot];
    bool everConfirmed = g_TankEverConfirmed[slot];
    float spawnTime = g_TankSpawnTime[slot];
    float lastValid = g_TankLastValidTime[slot];
    float invalidSince = g_TankInvalidSince[slot];

    char stateName[16];
    char lifetime[32];
    char lastValidDelta[32];
    char invalidAge[32];
    char sinceMap[32];
    GetTankStateName(state, stateName, sizeof(stateName));
    FormatDelta(lifetime, sizeof(lifetime), DeltaOrNeg(now, true, spawnTime));
    FormatDelta(lastValidDelta, sizeof(lastValidDelta),
        DeltaOrNeg(now, lastValid > 0.0, lastValid));
    FormatDelta(invalidAge, sizeof(invalidAge),
        DeltaOrNeg(now, invalidSince > 0.0, invalidSince));
    FormatDelta(sinceMap, sizeof(sinceMap), DeltaOrNeg(now, g_MapStartValid, g_MapStartTime));

    ResetTankSlot(slot);

    int trackedAfter = CountTrackedTanks();
    int confirmedAfter = CountConfirmedTanks();

    if (writeLog)
    {
        LogV2("%s|reason=%s|round=%d|round_uid=%s|userid=%d|tankid=%d|client=%d|spawn_seq=%d|spawn_round=%d|spawn_state=%s|ever_confirmed=%d|gametime=%.3f|since_map=%s|lifetime=%s|last_valid_dt=%s|invalid_age=%s|tracked_before=%d|tracked_alive=%d|confirmed_before=%d|confirmed_alive=%d|pending_before=%d|pending=%d|unconfirmed_before=%d|unconfirmed=%d",
            logType,
            reason,
            g_RoundSerial,
            g_RoundUid,
            userid,
            tankid,
            client,
            spawnSeq,
            spawnRound,
            stateName,
            everConfirmed ? 1 : 0,
            now,
            sinceMap,
            lifetime,
            lastValidDelta,
            invalidAge,
            trackedBefore,
            trackedAfter,
            confirmedBefore,
            confirmedAfter,
            pendingBefore,
            CountPendingTanks(),
            unconfirmedBefore,
            CountUnconfirmedTanks());
    }

    UpdateMaxAlive();
}

void ClearActiveTanks(const char[] reason, const char[] logType, float now, bool writeEach)
{
    int before = CountTrackedTanks();
    int confirmedBefore = CountConfirmedTanks();

    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (g_TankActive[i])
        {
            CloseTankSlot(i, reason, logType, now, writeEach);
        }
    }

    if (before > 0)
    {
        LogV2("TANK_CLEAR|reason=%s|count=%d|confirmed_before=%d|gametime=%.3f|round=%d|round_uid=%s|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d",
            reason,
            before,
            confirmedBefore,
            now,
            g_RoundSerial,
            g_RoundUid,
            CountTrackedTanks(),
            CountConfirmedTanks(),
            CountPendingTanks(),
            CountUnconfirmedTanks());
    }

    LogStateIfChanged(now);
}

void ClearTankRegistry()
{
    for (int i = 0; i < MAX_ACTIVE_TANKS; i++) ResetTankSlot(i);
}

bool IsTankClient(int client)
{
    if (client < 1 || client > MaxClients || !IsClientInGame(client))
    {
        return false;
    }
    if (GetClientTeam(client) != TEAM_INFECTED)
    {
        return false;
    }
    if (!IsPlayerAlive(client))
    {
        return false;
    }
    if (GetClientGhostState(client) != 0)
    {
        return false;
    }
    return GetClientZombieClass(client) == ZOMBIE_CLASS_TANK;
}

int GetClientGhostState(int client)
{
    if (!IsClientInGameSafe(client) || !IsValidEntity(client)
        || !HasEntProp(client, Prop_Send, "m_isGhost")) return -1;
    return GetEntProp(client, Prop_Send, "m_isGhost") != 0 ? 1 : 0;
}

int GetClientZombieClass(int client)
{
    if (!IsClientInGameSafe(client) || !IsValidEntity(client)
        || !HasEntProp(client, Prop_Send, "m_zombieClass")) return -1;
    return GetEntProp(client, Prop_Send, "m_zombieClass");
}

bool IsClientInGameSafe(int client)
{
    return (client >= 1 && client <= MaxClients && IsClientInGame(client));
}

int ClientForUserId(int userid)
{
    if (userid <= 0)
    {
        return 0;
    }

    int client = GetClientOfUserId(userid);
    if (client < 1 || client > MaxClients)
    {
        return 0;
    }
    return client;
}

int UserIdForClient(int client)
{
    if (client < 1 || client > MaxClients || !IsClientConnected(client)) return 0;
    return GetClientUserId(client);
}

void GetTankInvalidReason(int slot, char[] buffer, int maxLength)
{
    if (slot < 0 || slot >= MAX_ACTIVE_TANKS || !g_TankActive[slot])
    {
        strcopy(buffer, maxLength, "inactive");
        return;
    }
    int client = ClientForUserId(g_TankUserId[slot]);
    GetClientTankReason(client, buffer, maxLength);
    if (g_TankDisconnectPending[slot]) strcopy(buffer, maxLength, "disconnect_pending");
    else if (client > 0 && g_TankClient[slot] > 0 && client != g_TankClient[slot]) strcopy(buffer, maxLength, "client_mapping_changed");
    else if (client > 0 && FindTankByClient(client) >= 0 && FindTankByClient(client) != slot) strcopy(buffer, maxLength, "client_occupied");
}

bool HasActivity()
{
    return (g_RoundFirstSafeEventSeen || g_RoundTankSpawns > 0);
}

void GetTankStateName(int state, char[] buffer, int maxLength)
{
    switch (state)
    {
        case TANK_STATE_PENDING:
        {
            strcopy(buffer, maxLength, "pending");
            return;
        }
        case TANK_STATE_CONFIRMED:
        {
            strcopy(buffer, maxLength, "confirmed");
            return;
        }
        case TANK_STATE_TRANSIENT:
        {
            strcopy(buffer, maxLength, "transient");
            return;
        }
        case TANK_STATE_UNCONFIRMED:
        {
            strcopy(buffer, maxLength, "unconfirmed");
            return;
        }
    }
    strcopy(buffer, maxLength, "none");
}

void GetAnchorOriginName(int origin, char[] buffer, int maxLength)
{
    switch (origin)
    {
        case ANCHOR_ORIGIN_RAW_START:
        {
            strcopy(buffer, maxLength, "round_start");
            return;
        }
        case ANCHOR_ORIGIN_POST_NAV:
        {
            strcopy(buffer, maxLength, "post_nav");
            return;
        }
        case ANCHOR_ORIGIN_IMPLICIT:
        {
            strcopy(buffer, maxLength, "implicit");
            return;
        }
    }
    strcopy(buffer, maxLength, "none");
}

void GetConfirmedByName(int confirmedBy, char[] buffer, int maxLength)
{
    switch (confirmedBy)
    {
        case CONFIRM_BY_POST_NAV:
        {
            strcopy(buffer, maxLength, "post_nav");
            return;
        }
        case CONFIRM_BY_ACTIVITY:
        {
            strcopy(buffer, maxLength, "activity");
            return;
        }
    }
    strcopy(buffer, maxLength, "none");
}

float DeltaOrNeg(float now, bool valid, float then)
{
    if (!g_MapClockComparable || !valid || then < 0.0)
    {
        return -1.0;
    }

    float delta = now - then;
    if (delta < 0.0)
    {
        return 0.0;
    }
    return delta;
}

void FormatDelta(char[] buffer, int maxLength, float value)
{
    if (value < 0.0)
    {
        strcopy(buffer, maxLength, "NA");
        return;
    }
    Format(buffer, maxLength, "%.3f", value);
}

void LogV2(const char[] format, any ...)
{
    if (g_LogPath[0] == '\0') return;
    char message[3072];
    int length = VFormat(message, sizeof(message), format, 2);
    if (length >= sizeof(message) - 1)
    {
        LogToFileEx(g_LogPath, "%s|LOG_TRUNCATED|session=%s|obs=%d|ctx_map=%d|ctx_round=%d|ctx_uid=%s|ctx_boundary_ambiguous=%d|ctx_map_state=%s|ctx_attribution=%s|ctx_clock_comparable=%d",
            LOG_SCHEMA, g_Session, ++g_LogSeq, g_MapSerial, g_RoundSerial, g_RoundUid, g_RoundBoundaryAmbiguous ? 1 : 0,
            g_MapState == MAP_STATE_ACTIVE ? "active" : (g_MapState == MAP_STATE_TRANSITION ? "transition" : "initializing"),
            g_MapState == MAP_STATE_ACTIVE ? "current_map" : "unassigned", g_MapClockComparable ? 1 : 0);
        return;
    }
    LogToFileEx(g_LogPath, "%s|%s|session=%s|obs=%d|ctx_map=%d|ctx_round=%d|ctx_uid=%s|ctx_boundary_ambiguous=%d|ctx_map_state=%s|ctx_attribution=%s|ctx_clock_comparable=%d",
        LOG_SCHEMA, message, g_Session, ++g_LogSeq, g_MapSerial, g_RoundSerial, g_RoundUid, g_RoundBoundaryAmbiguous ? 1 : 0,
            g_MapState == MAP_STATE_ACTIVE ? "active" : (g_MapState == MAP_STATE_TRANSITION ? "transition" : "initializing"),
            g_MapState == MAP_STATE_ACTIVE ? "current_map" : "unassigned", g_MapClockComparable ? 1 : 0);
}

// Raw boundary evidence is separate from effective round state.
void ArchiveCandidateRaw(const char[] reason)
{
    if (g_CandidateRawCount <= 0) return;
    LogV2("ROUND_RAW_ARCHIVE|reason=%s|count=%d|first_raw_gametime=%.3f|last_raw_gametime=%.3f|gametime=%.3f|assigned=0|ambiguous=1|raw_map_id=%d|first_raw_order=%d|clock_comparable=%d",
        reason, g_CandidateRawCount, g_CandidateRawFirst, g_CandidateRawLast, GetGameTime(),
        g_CandidateRawMap, g_CandidateRawFirstOrder, g_MapClockComparable ? 1 : 0);
    ResetCandidateRaw();
}

bool HasFreshBoundaryRaw(float now)
{
    return g_MapState == MAP_STATE_ACTIVE && g_CandidateRawMap == g_MapSerial
        && g_CandidateRawCount > 0 && g_CandidateRawFirst <= now
        && now - g_CandidateRawFirst <= ROUND_RAW_WINDOW
        && (!g_RoundEnded || (g_HasRoundEndTime
            && g_CandidateRawFirstOrder > g_RoundEndOrder && g_CandidateRawFirst >= g_RoundEndTime));
}

void AssignCandidateRaw(const char[] attribution)
{
    if (g_CandidateRawCount <= 0) return;
    if (g_MapState != MAP_STATE_ACTIVE || g_CandidateRawMap != g_MapSerial)
    {
        ArchiveCandidateRaw("map_mismatch_unassigned");
        return;
    }
    bool assign = g_RoundActive && (!StrEqual(attribution, "new_round") || g_CandidateRawFirst >= g_RoundAnchorTime);
    if (assign)
    {
        if (g_RoundRawCount == 0) g_RoundFirstRawTime = g_CandidateRawFirst;
        g_RoundLastRawTime = g_CandidateRawLast;
        g_RoundRawCount += g_CandidateRawCount;
        g_RoundDupStartCount = g_RoundRawCount > 0 ? g_RoundRawCount - 1 : 0;
    }
    LogV2("ROUND_RAW_ATTRIBUTION|round=%d|attribution=%s|assigned=%d|count=%d|first_raw_gametime=%.3f|last_raw_gametime=%.3f|gametime=%.3f|ambiguous=%d",
        g_RoundSerial, attribution, assign ? 1 : 0, g_CandidateRawCount, g_CandidateRawFirst, g_CandidateRawLast, GetGameTime(),
        StrEqual(attribution, "same_round_ambiguous") || !assign ? 1 : 0);
    ResetCandidateRaw();
}

void RecordPostNav(float now)
{
    if (!g_RoundPostNavSeen)
    {
        g_RoundPostNavTime = now;
        g_RoundHasPostNavTime = true;
        g_RoundPostNavSeen = true;
    }
    else g_RoundDupPostNavCount++;
    g_RoundLastPostNavTime = now;
    g_RoundPostNavCount++;
}

void LogRoundTimes(const char[] reason)
{
    char firstRaw[32], lastRaw[32], firstPost[32], lastPost[32], postAge[32];
    FormatDelta(firstRaw, sizeof(firstRaw), g_RoundRawCount > 0 ? g_RoundFirstRawTime : -1.0);
    FormatDelta(lastRaw, sizeof(lastRaw), g_RoundRawCount > 0 ? g_RoundLastRawTime : -1.0);
    FormatDelta(firstPost, sizeof(firstPost), g_RoundHasPostNavTime ? g_RoundPostNavTime : -1.0);
    FormatDelta(lastPost, sizeof(lastPost), g_RoundHasPostNavTime ? g_RoundLastPostNavTime : -1.0);
    FormatDelta(postAge, sizeof(postAge), DeltaOrNeg(GetGameTime(), g_RoundHasPostNavTime, g_RoundPostNavTime));
    char origin[24], confirmation[24];
    GetAnchorOriginName(g_RoundAnchorOrigin, origin, sizeof(origin));
    GetConfirmedByName(g_RoundAnchorConfirmedBy, confirmation, sizeof(confirmation));
    LogV2("ROUND_TIMES|reason=%s|round=%d|round_uid=%s|anchor_origin=%s|anchor_confirmed_by=%s|anchor_gametime=%.3f|raw_count=%d|first_raw_gametime=%s|last_raw_gametime=%s|post_nav_count=%d|post_nav_gametime=%s|last_post_nav_gametime=%s|post_nav_age=%s|gametime=%.3f",
        reason, g_RoundSerial, g_RoundUid, origin, confirmation, g_RoundAnchorTime, g_RoundRawCount,
        firstRaw, lastRaw, g_RoundPostNavCount, firstPost, lastPost, postAge, GetGameTime());
}

int SlotSequence(int slot)
{
    return slot >= 0 && slot < MAX_ACTIVE_TANKS && g_TankActive[slot] ? g_TankRecordSpawnSeq[slot] : 0;
}

void LogSpawnConflict(int eventSeq, int userid, int tankid, int client, int byUser, int byTank, int byClient, int reserved, const char[] reason, float now)
{
    char userState[16], tankState[16], clientState[16];
    GetTankStateName(byUser >= 0 ? g_TankState[byUser] : TANK_STATE_NONE, userState, sizeof(userState));
    GetTankStateName(byTank >= 0 ? g_TankState[byTank] : TANK_STATE_NONE, tankState, sizeof(tankState));
    GetTankStateName(byClient >= 0 ? g_TankState[byClient] : TANK_STATE_NONE, clientState, sizeof(clientState));
    LogV2("TANK_SPAWN|evseq=%d|accepted=0|reason=%s|round=%d|round_uid=%s|userid=%d|tankid=%d|client=%d|userid_seq=%d|tankid_seq=%d|client_seq=%d|reserved_seq=%d|userid_state=%s|tankid_state=%s|client_state=%s|gametime=%.3f|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d|action=retain_all_no_merge",
        eventSeq, reason, g_RoundSerial, g_RoundUid, userid, tankid, client, SlotSequence(byUser), SlotSequence(byTank), SlotSequence(byClient), SlotSequence(reserved),
        userState, tankState, clientState, now, CountTrackedTanks(), CountConfirmedTanks(), CountPendingTanks(), CountUnconfirmedTanks());
}

int FindTransferReservation(int userid, int except = -1)
{
    if (userid <= 0) return -1;
    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
        if (i != except && g_TankActive[i] && g_TankTransferPending[i] && g_TankTransferUser[i] == userid) return i;
    return -1;
}

int FindUniqueTransferReservation(int userid)
{
    int found = -1;
    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (!g_TankActive[i] || !g_TankTransferPending[i] || g_TankTransferUser[i] != userid) continue;
        if (found >= 0) return -1;
        found = i;
    }
    return found;
}

int CountTankState(int state)
{
    int count = 0;
    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
        if (g_TankActive[i] && g_TankState[i] == state) count++;
    return count;
}

void GetClientTankReason(int client, char[] reason, int length)
{
    if (client <= 0) strcopy(reason, length, "userid_unresolved");
    else if (!IsClientInGameSafe(client)) strcopy(reason, length, "client_not_in_game");
    else if (!IsValidEntity(client)) strcopy(reason, length, "entity_invalid");
    else if (GetClientTeam(client) != TEAM_INFECTED) strcopy(reason, length, "team_changed");
    else if (GetClientZombieClass(client) < 0) strcopy(reason, length, "class_unknown");
    else if (GetClientZombieClass(client) != ZOMBIE_CLASS_TANK) strcopy(reason, length, "class_changed");
    else if (!IsPlayerAlive(client)) strcopy(reason, length, "not_alive");
    else if (GetClientGhostState(client) < 0) strcopy(reason, length, "ghost_unknown");
    else if (GetClientGhostState(client) != 0) strcopy(reason, length, "ghost");
    else strcopy(reason, length, "ok");
}

void RetryTankTransfer(int slot, float now)
{
    if (g_MapState != MAP_STATE_ACTIVE) return;
    if (slot < 0 || slot >= MAX_ACTIVE_TANKS || !g_TankActive[slot] || !g_TankTransferPending[slot]) return;
    int target = g_TankTransferUser[slot];
    int client = ClientForUserId(target);
    int byUser = FindTankByUserId(target);
    int byClient = FindTankByClient(client);
    int reserved = FindTransferReservation(target, slot);
    char reason[48];
    GetClientTankReason(client, reason, sizeof(reason));
    if (byUser >= 0 && byUser != slot) strcopy(reason, sizeof(reason), "new_userid_already_tracked");
    else if (byClient >= 0 && byClient != slot) strcopy(reason, sizeof(reason), "new_client_already_tracked");
    else if (reserved >= 0) strcopy(reason, sizeof(reason), "new_userid_reserved");
    if (StrEqual(reason, "ok"))
    {
        int oldUser = g_TankUserId[slot];
        int oldClient = g_TankClient[slot];
        bool hadDisconnect = g_TankDisconnectPending[slot];
        g_TankUserId[slot] = target;
        g_TankClient[slot] = client;
        g_TankTransferPending[slot] = false;
        g_TankDisconnectPending[slot] = false;
        g_TankDisconnectSince[slot] = 0.0;
        // Preserve spawn state until Evaluate emits any required transition.
        LogV2("TANK_OWNER_TRANSFER|kind=%s|spawn_seq=%d|spawn_round=%d|userid_old=%d|userid_new=%d|client_old=%d|client_new=%d|tankid=%d|gametime=%.3f|spawn_gametime=%.3f|wait=%.3f|resolved_disconnect_pending=%d|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d|tankid_role=diagnostic_only|team=%d|class=%d|alive=%d|ghost=%d",
            g_TankTransferKind[slot], g_TankRecordSpawnSeq[slot], g_TankSpawnRound[slot], oldUser, target, oldClient, client, g_TankTankId[slot], now,
            g_TankSpawnTime[slot], now - g_TankTransferSince[slot], hadDisconnect ? 1 : 0,
            CountTrackedTanks(), CountConfirmedTanks(), CountPendingTanks(), CountUnconfirmedTanks(),
            GetClientTeam(client), GetClientZombieClass(client), IsPlayerAlive(client) ? 1 : 0, GetClientGhostState(client));
        g_TankTransferUser[slot] = 0;
        g_TankTransferSince[slot] = 0.0;
        g_TankTransferKind[slot][0] = '\0';
        g_TankTransferReason[slot][0] = '\0';
        EvaluateTankSlot(slot, now, true);
        UpdateMaxAlive();
        return;
    }
    if (!StrEqual(g_TankTransferReason[slot], reason))
    {
        strcopy(g_TankTransferReason[slot], sizeof(g_TankTransferReason[]), reason);
        LogV2("TANK_OWNER_TRANSFER_WAIT|spawn_seq=%d|old_userid=%d|new_userid=%d|new_client=%d|reason=%s|userid_seq=%d|client_seq=%d|reserved_seq=%d|gametime=%.3f|team=%d|class=%d|alive=%d|ghost=%d",
            g_TankRecordSpawnSeq[slot], g_TankUserId[slot], target, client, reason, SlotSequence(byUser), SlotSequence(byClient), SlotSequence(reserved), now,
            IsClientInGameSafe(client) ? GetClientTeam(client) : -1, GetClientZombieClass(client),
            IsClientInGameSafe(client) ? (IsPlayerAlive(client) ? 1 : 0) : -1, GetClientGhostState(client));
    }
    if (now - g_TankTransferSince[slot] < TANK_TRANSFER_GRACE) return;
    bool keepOld = IsTankSlotCurrentlyConfirmed(slot);
    LogV2("TANK_OWNER_TRANSFER_TIMEOUT|spawn_seq=%d|old_userid=%d|new_userid=%d|reason=%s|gametime=%.3f|keep_old=%d",
        g_TankRecordSpawnSeq[slot], g_TankUserId[slot], target, reason, now, keepOld ? 1 : 0);
    g_TankTransferPending[slot] = false;
    g_TankTransferUser[slot] = 0;
    g_TankTransferSince[slot] = 0.0;
    g_TankTransferKind[slot][0] = '\0';
    g_TankTransferReason[slot][0] = '\0';
    if (!keepOld) CloseTankSlot(slot, "transfer_timeout_old_invalid", "TANK_STALE", now, true);
}

void LogPendingTransferDeath(int userid, int killEventSeq, float now)
{
    if (userid <= 0) return;
    for (int i = 0; i < MAX_ACTIVE_TANKS; i++)
    {
        if (!g_TankActive[i] || !g_TankTransferPending[i]) continue;
        bool ownerDied = g_TankUserId[i] == userid;
        bool candidateDied = g_TankTransferUser[i] == userid;
        if (!ownerDied && !candidateDied) continue;
        LogV2("TANK_OWNER_TRANSFER_DEATH_OBS|kill_evseq=%d|spawn_seq=%d|owner_userid=%d|candidate_userid=%d|death_userid=%d|death_role=%s|gametime=%.3f|candidate_since=%.3f|candidate_age=%.3f|disconnect_pending=%d|tracked_alive=%d|confirmed_alive=%d|pending=%d|unconfirmed=%d|action=observe_only",
            killEventSeq, g_TankRecordSpawnSeq[i], g_TankUserId[i], g_TankTransferUser[i], userid,
            ownerDied ? "owner" : "candidate", now, g_TankTransferSince[i], now - g_TankTransferSince[i],
            g_TankDisconnectPending[i] ? 1 : 0, CountTrackedTanks(), CountConfirmedTanks(), CountPendingTanks(), CountUnconfirmedTanks());
    }
}

void ResetTankSlot(int slot)
{
    if (slot < 0 || slot >= MAX_ACTIVE_TANKS) return;
    g_TankActive[slot] = false;
    g_TankState[slot] = TANK_STATE_NONE;
    g_TankUserId[slot] = 0;
    g_TankTankId[slot] = 0;
    g_TankClient[slot] = 0;
    g_TankRecordSpawnSeq[slot] = 0;
    g_TankSpawnRound[slot] = 0;
    g_TankSpawnTime[slot] = 0.0;
    g_TankLastValidTime[slot] = 0.0;
    g_TankInvalidSince[slot] = 0.0;
    g_TankEverConfirmed[slot] = false;
    g_TankDisconnectPending[slot] = false;
    g_TankDisconnectSince[slot] = 0.0;
    g_TankTransferPending[slot] = false;
    g_TankTransferUser[slot] = 0;
    g_TankTransferSince[slot] = 0.0;
    g_TankTransferKind[slot][0] = '\0';
    g_TankTransferReason[slot][0] = '\0';
}

// The single map-activity policy is shared by every BeginRound entry.
bool CanObserveMapActivity(const char[] trigger)
{
    if (g_MapState == MAP_STATE_ACTIVE) return true;
    LogV2("MAP_ACTIVITY_REJECT|trigger=%s|reason=map_not_active|gametime=%.3f|stats_reset=0|attribution=unassigned|ambiguous=1",
        trigger, GetGameTime());
    return false;
}

void ResetCandidateRaw()
{
    g_CandidateRawCount = 0;
    g_CandidateRawMap = -1;
    g_CandidateRawFirst = 0.0;
    g_CandidateRawLast = 0.0;
    g_CandidateRawFirstOrder = 0;
    g_RawStartSinceRoundBegin = false;
}
