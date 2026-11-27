// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

// Tank Run 综合控制中心 - Tank 实例层（M02 / M03 / M09 / M10）
// 只用公开接口：Left4DHooks SpawnTank / 原生随机选位、SDKHooks 伤害、实体属性、trace。

#if defined _tankrun_tank_included
 #endinput
#endif
#define _tankrun_tank_included

// 每个客户端槽位的 Tank 状态。使用 userid 做实体索引复用保护。
bool  g_bTankTracked[MAXPLAYERS + 1];
bool  g_bTankIsAi[MAXPLAYERS + 1];
int   g_iTankUserId[MAXPLAYERS + 1];
int   g_iTankBirthOrder[MAXPLAYERS + 1];
int   g_iNextTankBirthOrder;
float g_fTankSpawnTime[MAXPLAYERS + 1];
float g_fTankBaseSpeed[MAXPLAYERS + 1];

bool  g_bTankFirstContact[MAXPLAYERS + 1];
float g_fTankFirstContactTime[MAXPLAYERS + 1];
bool  g_bTankRockWindow[MAXPLAYERS + 1];
bool  g_bTankFirstRockDone[MAXPLAYERS + 1];
bool  g_bTankQuickPulseOff[MAXPLAYERS + 1];
float g_fTankNextQuickThrowLog[MAXPLAYERS + 1];
float g_fTankNextQuickLoS[MAXPLAYERS + 1];
int   g_iTankCachedQuickTarget[MAXPLAYERS + 1];
bool  g_bTankTraversalPunchLogged[MAXPLAYERS + 1];
bool  g_bTankTraversalDamageLogged[MAXPLAYERS + 1];
float g_fTankTraversalPunchUntil[MAXPLAYERS + 1];
float g_fTankRockDeadline[MAXPLAYERS + 1];
float g_fTankRockHardDeadline[MAXPLAYERS + 1];
float g_fTankRockAbsoluteDeadline[MAXPLAYERS + 1];
float g_fTankRockPausedAt[MAXPLAYERS + 1];
float g_fTankRockTraverseUntil[MAXPLAYERS + 1];
int   g_iTankRockAttempts[MAXPLAYERS + 1];
bool  g_bTankStrategyOff[MAXPLAYERS + 1];
int   g_iTankOverrides[MAXPLAYERS + 1];

float g_fTankLastRelease[MAXPLAYERS + 1];
float g_fTankLastThrowAttempt[MAXPLAYERS + 1];
int   g_iTankReleaseCount[MAXPLAYERS + 1];
int   g_iTankBlockedCount[MAXPLAYERS + 1];
int   g_iTankSwingCount[MAXPLAYERS + 1];
int   g_iTankHitCount[MAXPLAYERS + 1];
float g_fTankLastSwing[MAXPLAYERS + 1];
int   g_iTankLastActivity[MAXPLAYERS + 1];
float g_fTankLastActivityTime[MAXPLAYERS + 1];
float g_fTankPendingActivation[MAXPLAYERS + 1];
int   g_iTankChosenTarget[MAXPLAYERS + 1];
float g_fTankChosenTime[MAXPLAYERS + 1];
float g_fTankNextVictimTrace[MAXPLAYERS + 1];
float g_fTankNextThrowTrace[MAXPLAYERS + 1];
float g_fTankLastContactScan[MAXPLAYERS + 1];

bool g_bAutoRunning;
float g_fAutoSpawnNext = 0.0;
bool  g_bRoundEnded;
bool  g_bHumanLeftStart;
bool  g_bFrontPhaseOpen;
bool  g_bFrontSpawnOwned;
bool  g_bFrontReplacement;
int   g_iReplacementUserId;
int   g_iReplacementTick;
float g_fReplacementStarted;
float g_fReplacementRetry;
float g_fReplacementPos[3];
float g_fReplacementAngles[3];
float g_fNextFrontPhaseProbe;
int   g_iPeakLiveAiTanks;
int   g_iStatRotateSkippedProtected;
int   g_iStatRotateDeferred;

int g_iTraceIgnoreA = 0;
int g_iTraceIgnoreB = 0;

// Tank 刷新提示音（c1m4_atrium 逐桶倒油完成原版库存样本，非空间本地播放）
#define TR_TANK_SPAWN_SOUND         "ui/survival_playerrec.wav"
#define TR_SPAWN_SOUND_SPACING      0.18
#define TR_SPAWN_SOUND_SUPPRESS_MAX MAXPLAYERS
#define TR_SPAWN_SOUND_SUPPRESS_TTL 2.0

bool   g_bSpawnSoundPrecached;
int    g_iSpawnSoundSessionGen;
int    g_iSpawnSoundPending[MAXPLAYERS + 1];
int    g_iSpawnSoundUserId[MAXPLAYERS + 1];
float  g_fSpawnSoundNextAllowed[MAXPLAYERS + 1];
Handle g_hSpawnSoundTimer[MAXPLAYERS + 1];
int    g_iSpawnSoundSuppressUid[TR_SPAWN_SOUND_SUPPRESS_MAX];
float  g_fSpawnSoundSuppressUntil[TR_SPAWN_SOUND_SUPPRESS_MAX];

// ---------------------------------------------------------------- 刷新提示音
void TR_PrecacheSpawnSound()
{
    g_bSpawnSoundPrecached = PrecacheSound(TR_TANK_SPAWN_SOUND, true);
}

void TR_ClearSpawnSoundSuppressUid(int userid)
{
    if (userid <= 0)
    {
        return;
    }
    for (int i = 0; i < TR_SPAWN_SOUND_SUPPRESS_MAX; i++)
    {
        if (g_iSpawnSoundSuppressUid[i] == userid)
        {
            g_iSpawnSoundSuppressUid[i] = 0;
            g_fSpawnSoundSuppressUntil[i] = 0.0;
        }
    }
}

void TR_SuppressSpawnSoundForUserId(int userid, float ttl = TR_SPAWN_SOUND_SUPPRESS_TTL)
{
    if (userid <= 0)
    {
        return;
    }
    float now = GetGameTime();
    float until = ttl <= 0.0 ? -1.0 : (now + ttl);
    int freeSlot = -1;
    int oldestTempSlot = -1;
    for (int i = 0; i < TR_SPAWN_SOUND_SUPPRESS_MAX; i++)
    {
        if (g_iSpawnSoundSuppressUid[i] != 0 && g_fSpawnSoundSuppressUntil[i] > 0.0
            && now > g_fSpawnSoundSuppressUntil[i])
        {
            g_iSpawnSoundSuppressUid[i] = 0;
            g_fSpawnSoundSuppressUntil[i] = 0.0;
        }
        if (g_iSpawnSoundSuppressUid[i] == userid)
        {
            if (until < 0.0 || (g_fSpawnSoundSuppressUntil[i] > 0.0 && until > g_fSpawnSoundSuppressUntil[i]))
            {
                g_fSpawnSoundSuppressUntil[i] = until;
            }
            return;
        }
        if (g_iSpawnSoundSuppressUid[i] == 0)
        {
            if (freeSlot == -1)
            {
                freeSlot = i;
            }
        }
        else if (g_fSpawnSoundSuppressUntil[i] > 0.0)
        {
            if (oldestTempSlot == -1 || g_fSpawnSoundSuppressUntil[i] < g_fSpawnSoundSuppressUntil[oldestTempSlot])
            {
                oldestTempSlot = i;
            }
        }
    }
    int slot = freeSlot != -1 ? freeSlot : (oldestTempSlot != -1 ? oldestTempSlot : 0);
    g_iSpawnSoundSuppressUid[slot] = userid;
    g_fSpawnSoundSuppressUntil[slot] = until;
}

void TR_SuppressTankTakeoverSound(int userid)
{
    TR_SuppressSpawnSoundForUserId(userid, TR_SPAWN_SOUND_SUPPRESS_TTL);
}

void TR_MarkExistingTanksSpawnSoundDone()
{
    for (int i = 1; i <= MaxClients; i++)
    {
        if (TR_IsTankClient(i))
        {
            TR_SuppressSpawnSoundForUserId(GetClientUserId(i), -1.0);
        }
    }
}

bool TR_ConsumeSpawnSoundSuppressed(int userid)
{
    if (userid <= 0)
    {
        return false;
    }
    float now = GetGameTime();
    bool matched = false;
    for (int i = 0; i < TR_SPAWN_SOUND_SUPPRESS_MAX; i++)
    {
        if (g_iSpawnSoundSuppressUid[i] == 0)
        {
            continue;
        }
        if (g_fSpawnSoundSuppressUntil[i] > 0.0 && now > g_fSpawnSoundSuppressUntil[i])
        {
            g_iSpawnSoundSuppressUid[i] = 0;
            g_fSpawnSoundSuppressUntil[i] = 0.0;
            continue;
        }
        if (g_iSpawnSoundSuppressUid[i] == userid)
        {
            g_iSpawnSoundSuppressUid[i] = 0;
            g_fSpawnSoundSuppressUntil[i] = 0.0;
            matched = true;
        }
    }
    return matched;
}

void TR_ClearSpawnSoundClient(int client)
{
    if (client <= 0 || client > MaxClients)
    {
        return;
    }
    g_iSpawnSoundPending[client] = 0;
    g_iSpawnSoundUserId[client] = 0;
    g_fSpawnSoundNextAllowed[client] = 0.0;
    if (g_hSpawnSoundTimer[client] != null)
    {
        KillTimer(g_hSpawnSoundTimer[client]);
        g_hSpawnSoundTimer[client] = null;
    }
}

void TR_ClearAllSpawnSounds(bool clearPersistentSuppress = true)
{
    g_iSpawnSoundSessionGen++;
    for (int i = 0; i < TR_SPAWN_SOUND_SUPPRESS_MAX; i++)
    {
        if (clearPersistentSuppress || g_fSpawnSoundSuppressUntil[i] >= 0.0)
        {
            g_iSpawnSoundSuppressUid[i] = 0;
            g_fSpawnSoundSuppressUntil[i] = 0.0;
        }
    }
    for (int i = 1; i <= MAXPLAYERS; i++)
    {
        g_iSpawnSoundPending[i] = 0;
        g_iSpawnSoundUserId[i] = 0;
        g_fSpawnSoundNextAllowed[i] = 0.0;
        if (g_hSpawnSoundTimer[i] != null)
        {
            KillTimer(g_hSpawnSoundTimer[i]);
            g_hSpawnSoundTimer[i] = null;
        }
    }
}

void TR_EmitTankSpawnSoundToClient(int client)
{
    if (!g_bSpawnSoundPrecached && TR_IsMapRunning())
    {
        TR_PrecacheSpawnSound();
    }
    EmitSoundToClient(client, TR_TANK_SPAWN_SOUND, SOUND_FROM_PLAYER, SNDCHAN_AUTO,
        SNDLEVEL_NONE, SND_NOFLAGS, SNDVOL_NORMAL, SNDPITCH_NORMAL);
}

void TR_QueueTankSpawnSoundForClient(int client, float now)
{
    int userid = GetClientUserId(client);
    if (userid <= 0)
    {
        return;
    }
    if (g_iSpawnSoundUserId[client] != 0 && g_iSpawnSoundUserId[client] != userid)
    {
        TR_ClearSpawnSoundClient(client);
    }
    g_iSpawnSoundUserId[client] = userid;

    if (g_iSpawnSoundPending[client] == 0 && g_hSpawnSoundTimer[client] == null
        && now >= g_fSpawnSoundNextAllowed[client])
    {
        TR_EmitTankSpawnSoundToClient(client);
        g_fSpawnSoundNextAllowed[client] = now + TR_SPAWN_SOUND_SPACING;
        return;
    }

    g_iSpawnSoundPending[client]++;
    if (g_hSpawnSoundTimer[client] == null)
    {
        float delay = g_fSpawnSoundNextAllowed[client] - now;
        if (delay < 0.01)
        {
            delay = 0.01;
        }
        g_hSpawnSoundTimer[client] = CreateTimer(delay, Timer_FlushSpawnSound, client, TIMER_FLAG_NO_MAPCHANGE);
    }
}

void TR_NotifyTankSpawnSound()
{
    if (!TR_FeatureEnabled() || g_bMapEnding || g_bRoundEnded)
    {
        return;
    }
    float now = GetGameTime();
    for (int client = 1; client <= MaxClients; client++)
    {
        if (!IsClientInGame(client) || IsFakeClient(client))
        {
            continue;
        }
        TR_QueueTankSpawnSoundForClient(client, now);
    }
}

public Action Timer_FlushSpawnSound(Handle timer, any data)
{
    int client = view_as<int>(data);
    if (client <= 0 || client > MaxClients)
    {
        return Plugin_Stop;
    }
    if (g_hSpawnSoundTimer[client] != timer)
    {
        return Plugin_Stop;
    }
    g_hSpawnSoundTimer[client] = null;

    if (!TR_FeatureEnabled() || g_bMapEnding || g_bRoundEnded)
    {
        TR_ClearSpawnSoundClient(client);
        return Plugin_Stop;
    }

    int userid = g_iSpawnSoundUserId[client];
    if (userid <= 0 || GetClientOfUserId(userid) != client
        || !IsClientInGame(client) || IsFakeClient(client))
    {
        TR_ClearSpawnSoundClient(client);
        return Plugin_Stop;
    }

    if (g_iSpawnSoundPending[client] <= 0)
    {
        return Plugin_Stop;
    }

    g_iSpawnSoundPending[client]--;
    TR_EmitTankSpawnSoundToClient(client);
    g_fSpawnSoundNextAllowed[client] = GetGameTime() + TR_SPAWN_SOUND_SPACING;

    if (g_iSpawnSoundPending[client] > 0)
    {
        g_hSpawnSoundTimer[client] = CreateTimer(TR_SPAWN_SOUND_SPACING, Timer_FlushSpawnSound,
            client, TIMER_FLAG_NO_MAPCHANGE);
    }
    return Plugin_Stop;
}

// ---------------------------------------------------------------- 基础判定
bool TR_IsSurvivor(int client)
{
    return client > 0 && client <= MaxClients && IsClientInGame(client)
        && IsPlayerAlive(client) && GetClientTeam(client) == 2;
}

bool TR_IsTankClient(int client)
{
    if (client <= 0 || client > MaxClients || !IsClientInGame(client) || !IsPlayerAlive(client))
    {
        return false;
    }
    if (GetClientTeam(client) != 3)
    {
        return false;
    }
    if (GetEntProp(client, Prop_Send, "m_zombieClass") != 8)
    {
        return false;
    }
    return !HasEntProp(client, Prop_Send, "m_isGhost") || GetEntProp(client, Prop_Send, "m_isGhost") == 0;
}

bool TR_IsTankClientOrTracked(int client, int userid)
{
    if (client <= 0 || client > MaxClients)
    {
        return false;
    }
    if (userid > 0 && g_bTankTracked[client] && g_iTankUserId[client] == userid)
    {
        return true;
    }
    if (!IsClientInGame(client) || GetClientTeam(client) != 3)
    {
        return false;
    }
    return HasEntProp(client, Prop_Send, "m_zombieClass")
        && GetEntProp(client, Prop_Send, "m_zombieClass") == 8;
}

bool TR_IsAiTank(int client)
{
    return TR_IsTankClient(client) && IsFakeClient(client);
}

int TR_CountTanks(bool aiOnly)
{
    int count = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (TR_IsTankClient(i) && (!aiOnly || IsFakeClient(i)))
        {
            count++;
        }
    }
    return count;
}

int TR_GetTankList(int[] list, int maxlen, bool includePlayers)
{
    int count = 0;
    for (int i = 1; i <= MaxClients && count < maxlen; i++)
    {
        if (!TR_IsTankClient(i))
        {
            continue;
        }
        if (!includePlayers && !IsFakeClient(i))
        {
            continue;
        }
        list[count++] = i;
    }
    return count;
}

// ---------------------------------------------------------------- trace 过滤
public bool TR_FilterNothing(int entity, int contentsMask, any data)
{
    return true;
}

public bool TR_FilterLoS(int entity, int contentsMask, any data)
{
    if (entity >= 1 && entity <= MaxClients)
    {
        return false;
    }
    if (entity == g_iTraceIgnoreA || entity == g_iTraceIgnoreB || (data > MaxClients && entity == data))
    {
        return false;
    }
    return true;
}

bool TR_HasLineOfSight(int tank, int target, const float start[3], const float end[3], int ignoreEntity = 0)
{
    g_iTraceIgnoreA = tank;
    g_iTraceIgnoreB = target;
    TR_TraceRayFilter(start, end, MASK_VISIBLE, RayType_EndPoint, TR_FilterLoS, ignoreEntity);
    g_iTraceIgnoreA = 0;
    g_iTraceIgnoreB = 0;
    if (!TR_DidHit())
    {
        return true;
    }
    return TR_GetFraction() >= 0.99;
}

float TR_NearestSurvivorDistance(const float pos[3], int &who)
{
    float best = 999999.0;
    who = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!TR_IsSurvivor(i))
        {
            continue;
        }
        float target[3];
        GetClientAbsOrigin(i, target);
        float dist = GetVectorDistance(pos, target);
        if (dist < best)
        {
            best = dist;
            who = i;
        }
    }
    return who == 0 ? -1.0 : best;
}

int TR_FindVisibleSurvivor(int tank, float maxRange = 1500.0)
{
    if (!TR_IsTankClient(tank))
    {
        return 0;
    }
    float start[3];
    GetClientEyePosition(tank, start);
    int best = 0;
    float bestDist = maxRange;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!TR_IsSurvivor(i))
        {
            continue;
        }
        float target[3];
        GetClientEyePosition(i, target);
        float dist = GetVectorDistance(start, target);
        if (dist > maxRange || dist >= bestDist)
        {
            continue;
        }
        if (!TR_HasLineOfSight(tank, i, start, target))
        {
            continue;
        }
        best = i;
        bestDist = dist;
    }
    return best;
}

// ---------------------------------------------------------------- 生成位置
bool TR_IsSpawnPosClear(const float pos[3])
{
    float mins[3] = {-24.0, -24.0, 0.0};
    float maxs[3] = {24.0, 24.0, 80.0};
    TR_TraceHullFilter(pos, pos, mins, maxs, MASK_PLAYERSOLID, TR_FilterNothing, 0);
    if (TR_StartSolid() || TR_GetFraction() < 1.0)
    {
        return false;
    }

    float start[3];
    float end[3];
    start = pos;
    start[2] += 20.0;
    end = pos;
    end[2] -= 300.0;
    TR_TraceRayFilter(start, end, MASK_SOLID, RayType_EndPoint, TR_FilterNothing, 0);
    if (!TR_DidHit())
    {
        return false;
    }
    float normal[3];
    TR_GetPlaneNormal(INVALID_HANDLE, normal);
    return normal[2] > 0.5;
}

bool TR_GetAimSpawnPos(int client, float pos[3], float ang[3])
{
    float eye[3];
    float angles[3];
    float dir[3];
    GetClientEyePosition(client, eye);
    GetClientEyeAngles(client, angles);
    GetAngleVectors(angles, dir, NULL_VECTOR, NULL_VECTOR);

    float target[3];
    target[0] = eye[0] + dir[0] * 150.0;
    target[1] = eye[1] + dir[1] * 150.0;
    target[2] = eye[2] + dir[2] * 150.0;

    float start[3];
    float end[3];
    start = target;
    start[2] += 40.0;
    end = target;
    end[2] -= 400.0;
    TR_TraceRayFilter(start, end, MASK_SOLID, RayType_EndPoint, TR_FilterNothing, 0);
    if (!TR_DidHit())
    {
        return false;
    }
    TR_GetEndPosition(pos);
    pos[2] += 8.0;
    ang[0] = 0.0;
    ang[1] = angles[1];
    ang[2] = 0.0;
    return TR_IsSpawnPosClear(pos);
}

// ---------------------------------------------------------------- 跟踪与清理
void TR_ResetTank(int client, const char[] reason)
{
    if (!g_bTankTracked[client])
    {
        return;
    }
    TR_LogEvent("清理 Tank 槽位 客户端#%d（%s）", client, reason);
    TR_ClearSpawnSoundSuppressUid(g_iTankUserId[client]);
    TR_ResetRockTelemetry(client);
    TR_Bait_ResetNativeTank(client);
    TR_Glow_EndNativeThrow(client);
    g_bTankTracked[client] = false;
    g_bTankIsAi[client] = false;
    g_iTankUserId[client] = 0;
    g_iTankBirthOrder[client] = 0;
    g_fTankSpawnTime[client] = 0.0;
    g_fTankBaseSpeed[client] = 0.0;
    g_bTankFirstContact[client] = false;
    g_fTankFirstContactTime[client] = 0.0;
    g_bTankRockWindow[client] = false;
    g_bTankFirstRockDone[client] = false;
    g_bTankQuickPulseOff[client] = false;
    g_fTankNextQuickThrowLog[client] = 0.0;
    g_fTankNextQuickLoS[client] = 0.0;
    g_iTankCachedQuickTarget[client] = 0;
    g_bTankTraversalPunchLogged[client] = false;
    g_bTankTraversalDamageLogged[client] = false;
    g_fTankTraversalPunchUntil[client] = 0.0;
    g_fTankRockDeadline[client] = 0.0;
    g_fTankRockHardDeadline[client] = 0.0;
    g_fTankRockAbsoluteDeadline[client] = 0.0;
    g_fTankRockPausedAt[client] = 0.0;
    g_fTankRockTraverseUntil[client] = 0.0;
    g_iTankRockAttempts[client] = 0;
    g_bTankStrategyOff[client] = false;
    g_iTankOverrides[client] = 0;
    g_fTankLastRelease[client] = 0.0;
    g_fTankLastThrowAttempt[client] = 0.0;
    g_iTankReleaseCount[client] = 0;
    g_iTankBlockedCount[client] = 0;
    g_iTankSwingCount[client] = 0;
    g_iTankHitCount[client] = 0;
    g_fTankLastSwing[client] = 0.0;
    g_iTankLastActivity[client] = 0;
    g_fTankLastActivityTime[client] = 0.0;
    g_fTankPendingActivation[client] = 0.0;
    g_iTankChosenTarget[client] = 0;
    g_fTankChosenTime[client] = 0.0;
    g_fTankNextVictimTrace[client] = 0.0;
    g_fTankNextThrowTrace[client] = 0.0;
    g_fTankLastContactScan[client] = 0.0;
}

bool TR_TrackTank(int client, const char[] reason, bool fromSpawnEvent = false)
{
    if (!TR_IsTankClient(client))
    {
        return false;
    }
    int uid = GetClientUserId(client);
    if (g_bTankTracked[client] && g_iTankUserId[client] == uid)
    {
        if (fromSpawnEvent && g_iTankBirthOrder[client] == 0)
        {
            g_iTankBirthOrder[client] = ++g_iNextTankBirthOrder;
            return true;
        }
        return false;
    }
    if (g_bTankTracked[client])
    {
        TR_ResetTank(client, "实体索引复用");
    }
    TR_ResetTank(client, "初始化");
    g_bTankTracked[client] = true;
    g_bTankIsAi[client] = IsFakeClient(client);
    g_iTankUserId[client] = uid;
    TR_Bait_OnNativeTankTracked(client);
    bool assignedBirthOrder = false;
    if (fromSpawnEvent)
    {
        g_iTankBirthOrder[client] = ++g_iNextTankBirthOrder;
        assignedBirthOrder = true;
    }
    g_fTankSpawnTime[client] = GetGameTime();
    if (HasEntProp(client, Prop_Data, "m_flMaxSpeed"))
    {
        g_fTankBaseSpeed[client] = GetEntPropFloat(client, Prop_Data, "m_flMaxSpeed");
        if (g_fTankBaseSpeed[client] <= 0.0)
        {
            g_fTankBaseSpeed[client] = 300.0;
        }
    }
    TR_LogEvent("跟踪 Tank 客户端#%d userid=%d AI=%s（%s）", client, uid,
        g_bTankIsAi[client] ? "是" : "否", reason);
    return assignedBirthOrder;
}

void TR_ResetAllTanks(const char[] reason)
{
    for (int i = 1; i <= MaxClients; i++)
    {
        TR_ResetTank(i, reason);
    }
    g_bScriptThinkDisabled = false;
    g_iOriginalStockLimit = 0;
    g_iLastManagedStockLimit = 0;
    g_iNextTankBirthOrder = 0;
    g_iPeakLiveAiTanks = 0;
    TR_ResetFrontCycle();
}

void TR_EnforceSpeed(int client)
{
    if (!g_bTankTracked[client] || g_fTankBaseSpeed[client] <= 0.0)
    {
        return;
    }
    // 玩家控制的 Tank 一律不改移动速度。
    if (!IsFakeClient(client))
    {
        return;
    }
    float scale = g_cvSpeedScale.FloatValue;
    if (FloatAbs(scale - 1.0) < 0.001)
    {
        return;
    }
    float target = g_fTankBaseSpeed[client] * scale;
    float current = GetEntPropFloat(client, Prop_Data, "m_flMaxSpeed");
    if (FloatAbs(current - target) > 1.0)
    {
        SetEntPropFloat(client, Prop_Data, "m_flMaxSpeed", target);
    }
}

void TR_ApplySpeedToAll()
{
    for (int i = 1; i <= MaxClients; i++)
    {
        if (TR_IsAiTank(i))
        {
            TR_EnforceSpeed(i);
        }
    }
}

void TR_SyncTanks()
{
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!IsClientInGame(i))
        {
            if (g_bTankTracked[i])
            {
                TR_ResetTank(i, "离开游戏");
            }
            continue;
        }
        if (!TR_IsTankClient(i))
        {
            if (g_bTankTracked[i])
            {
                TR_ResetTank(i, "不再是存活 Tank");
            }
            continue;
        }
        if (g_bTankTracked[i] && g_iTankUserId[i] != GetClientUserId(i))
        {
            TR_ResetTank(i, "userid 变化（实体索引复用保护）");
        }
        if (g_bTankTracked[i])
        {
            TR_EnforceSpeed(i);
            continue;
        }
        if (!IsFakeClient(i) && g_cvAiOnly.BoolValue)
        {
            continue;
        }
        TR_TrackTank(i, "周期扫描");
    }
}

// ---------------------------------------------------------------- 生成实现
int TR_SpawnAt(const float pos[3], const float ang[3], int requester, char[] message, int msglen)
{
    g_bFrontSpawnOwned = true;
    int tank = L4D2_SpawnTank(pos, ang);
    g_bFrontSpawnOwned = false;
    // 引擎返回的实体可能在同一帧内尚未完成 Tank 初始化，这里只要求是有效客户端实体；
    // 真正的 Tank 判定由 tank_spawn 事件与周期扫描完成。
    if (tank <= 0 || tank > MaxClients || !IsClientInGame(tank))
    {
        g_iStatSpawnFail++;
        Format(message, msglen, "引擎 SpawnTank 失败（返回 %d）", tank);
        TR_Log("生成失败：pos=(%.1f, %.1f, %.1f) 返回 %d；已连接客户端 %d/%d，存活 Tank %d",
            pos[0], pos[1], pos[2], tank, GetClientCount(false), MaxClients, TR_CountTanks(false));
        return 0;
    }
    g_iStatSpawns++;
    if (TR_IsTankClient(tank))
    {
        TR_TrackTank(tank, "插件生成");
        TR_ApplyHealthPolicy(tank, "插件生成");
    }
    int who = 0;
    float dist = TR_NearestSurvivorDistance(pos, who);
    TR_LogEvent("生成 Tank 客户端#%d pos=(%.1f, %.1f, %.1f) 发起者=%d 最近幸存者=%d 距离=%.0f",
        tank, pos[0], pos[1], pos[2], requester, who, dist);
    if (who > 0)
    {
        Format(message, msglen, "已生成 AI Tank #%d，距离最近幸存者 %.0f 单位", tank, dist);
    }
    else
    {
        Format(message, msglen, "已生成 AI Tank #%d（当前没有存活幸存者）", tank);
    }
    return tank;
}

int TR_SpawnNativePosition(char[] message, int msglen, int requester = 0, bool quietFail = false)
{
    float pos[3];
    int attempts = g_cvSpawnAttempts.IntValue;
    if (attempts < 1)
    {
        attempts = 1;
    }
    bool found = false;
    for (int i = 0; i < attempts && !found; i++)
    {
        found = L4D_GetRandomPZSpawnPosition(0, 8, 4, pos);
    }
    if (!found)
    {
        if (!quietFail)
        {
            Format(message, msglen, "原生随机区域未找到有效位置（%d 次尝试）", attempts);
        }
        g_iStatSpawnFail++;
        TR_Log("原生选位失败：%d 次尝试", attempts);
        return 0;
    }
    float ang[3] = {0.0, 0.0, 0.0};
    return TR_SpawnAt(pos, ang, requester, message, msglen);
}

int TR_SpawnNearSurvivor(int requester, char[] message, int msglen, bool enforceBand)
{
    int anchors[MAXPLAYERS + 1];
    int anchorCount = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (TR_IsSurvivor(i))
        {
            anchors[anchorCount++] = i;
        }
    }
    if (anchorCount == 0)
    {
        Format(message, msglen, "没有存活幸存者，无法按幸存者位置生成");
        return 0;
    }

    float minDist = g_cvSpawnMin.FloatValue;
    float maxDist = g_cvSpawnMax.FloatValue;
    if (minDist > maxDist)
    {
        float swap = minDist;
        minDist = maxDist;
        maxDist = swap;
    }

    int attempts = g_cvSpawnAttempts.IntValue;
    if (attempts < 1)
    {
        attempts = 1;
    }

    char lastReason[128];
    strcopy(lastReason, sizeof(lastReason), "未取得候选位置");

    for (int attempt = 0; attempt < attempts; attempt++)
    {
        int anchor = anchors[GetRandomInt(0, anchorCount - 1)];
        float pos[3];
        if (!L4D_GetRandomPZSpawnPosition(anchor, 8, 4, pos))
        {
            strcopy(lastReason, sizeof(lastReason), "原生选位返回失败");
            continue;
        }
        if (!TR_IsSpawnPosClear(pos))
        {
            strcopy(lastReason, sizeof(lastReason), "候选位置空间不足或被占用");
            continue;
        }
        int who = 0;
        float dist = TR_NearestSurvivorDistance(pos, who);
        if (enforceBand)
        {
            if (dist < minDist)
            {
                Format(lastReason, sizeof(lastReason), "最近距离 %.0f < 最小 %.0f", dist, minDist);
                continue;
            }
            if (dist > maxDist)
            {
                Format(lastReason, sizeof(lastReason), "最近距离 %.0f > 最大 %.0f", dist, maxDist);
                continue;
            }
        }
        float ang[3] = {0.0, 0.0, 0.0};
        return TR_SpawnAt(pos, ang, requester, message, msglen);
    }

    char fail[192];
    Format(fail, sizeof(fail), "位置策略失败：%s", lastReason);
    if (g_cvSpawnFallback.BoolValue)
    {
        char fallbackMsg[160];
        int tank = TR_SpawnNativePosition(fallbackMsg, sizeof(fallbackMsg), requester, true);
        if (tank > 0)
        {
            Format(message, msglen, "%s；已回退原生选位（%s）", fail, fallbackMsg);
        }
        else
        {
            Format(message, msglen, "%s；回退原生选位也失败", fail);
        }
        return tank;
    }
    Format(message, msglen, "%s（已禁用回退）", fail);
    g_iStatSpawnFail++;
    return 0;
}

int TR_SpawnNow(int requester, TR_SpawnMode mode, char[] message, int msglen)
{
    message[0] = '\0';
    if (!TR_FeatureEnabled())
    {
        Format(message, msglen, "插件已关闭（tankrun_enabled=0）");
        return 0;
    }
    // 防御：没有运行中的地图时禁止生成（避免提前调用实体 Native）。
    if (!TR_IsMapRunning())
    {
        Format(message, msglen, "服务器当前没有运行中的地图，无法生成 Tank");
        TR_Log("生成被拒绝：地图/实体系统未就绪（方式 %d）", mode);
        return 0;
    }
    if (TR_IsTankRunMode())
    {
        if (!TR_HumanHasLeftStart() || !TR_VScriptReadFrontPhase() || g_bRoundEnded)
        {
            strcopy(message, msglen, "当前阶段禁止生成，请先让真人离开起始安全区");
            return 0;
        }
        int limit = TR_FrontEffectiveLimit();
        if (TR_FrontTankCount() >= limit)
        {
            Format(message, msglen, "已达当前有效上限 %d，自动轮换会按周期补位", limit);
            return 0;
        }
        float pos[3], ang[3];
        char site[192];
        if (!TR_FindLeadTankSpawn(pos, ang, site, sizeof(site)))
        {
            strcopy(message, msglen, site);
            return 0;
        }
        int tank = TR_SpawnFrontAt(pos, ang, requester, message, msglen);
        TR_Log("前线生成（手动）：%s；%s", site, message);
        return tank;
    }
    int connected = GetClientCount(false);
    if (connected >= MaxClients)
    {
        Format(message, msglen, "客户端槽位已满 %d/%d（%d 只 Tank）。请先处死一只 AI Tank 腾位", connected, MaxClients, TR_CountTanks(false));
        TR_Log("生成被拒绝：%s", message);
        g_iStatSpawnFail++;
        return 0;
    }

    switch (mode)
    {
        case TRSP_VSCRIPT:
        {
            strcopy(message, msglen, "脚本生成仅在 Tank Run 模式使用统一调度");
            return 0;
        }
        case TRSP_AIM:
        {
            if (requester <= 0 || !IsClientInGame(requester))
            {
                Format(message, msglen, "准星生成只能在游戏内由管理员发起");
                return 0;
            }
            float pos[3];
            float ang[3];
            if (!TR_GetAimSpawnPos(requester, pos, ang))
            {
                Format(message, msglen, "准星位置无效：没有地面或空间不足");
                g_iStatSpawnFail++;
                return 0;
            }
            return TR_SpawnAt(pos, ang, requester, message, msglen);
        }
        case TRSP_BAND:
        {
            return TR_SpawnNearSurvivor(requester, message, msglen, true);
        }
        case TRSP_NEAR:
        {
            return TR_SpawnNearSurvivor(requester, message, msglen, false);
        }
        default:
        {
            return TR_SpawnNativePosition(message, msglen, requester);
        }
    }
}

// ---------------------------------------------------------------- 生命与伤害
void TR_ApplyHealthPolicy(int client, const char[] source)
{
    if (!TR_FeatureEnabled() || !TR_IsAiTank(client))
    {
        return;
    }
    int maxhp = g_cvTankMaxHealth.IntValue;
    int hp = g_cvTankHealth.IntValue;
    if (maxhp > 0)
    {
        SetEntProp(client, Prop_Data, "m_iMaxHealth", maxhp);
    }
    if (hp > 0)
    {
        SetEntityHealth(client, hp);
    }
    else if (maxhp > 0)
    {
        SetEntityHealth(client, maxhp);
    }

    if ((hp > 0 || maxhp > 0) && g_cvModeHealth.BoolValue && TR_IsTankRunMode())
    {
        int value = hp > 0 ? hp : maxhp;
        int idx = TR_FindParamByVField(TRV_TANKHEALTH);
        float saved;
        if (idx >= 0 && !TR_ReadSessionParam(idx, saved))
        {
            TR_VScriptSetField(idx, float(value));
        }
    }

    if (hp > 0 || maxhp > 0)
    {
        TR_LogEvent("应用血量策略 客户端#%d 当前=%d 最大=%d（%s）", client,
            GetClientHealth(client), GetEntProp(client, Prop_Data, "m_iMaxHealth"), source);
    }
}

void TR_ApplyHealthToAll(const char[] source)
{
    for (int i = 1; i <= MaxClients; i++)
    {
        if (TR_IsAiTank(i))
        {
            TR_ApplyHealthPolicy(i, source);
        }
    }
}

void TR_HealTank(int client)
{
    if (!TR_IsTankClient(client))
    {
        return;
    }
    int maxhp = GetEntProp(client, Prop_Data, "m_iMaxHealth");
    if (maxhp <= 0)
    {
        maxhp = 4000;
    }
    SetEntityHealth(client, maxhp);
    TR_LogEvent("治疗 Tank 客户端#%d → %d", client, maxhp);
}

void TR_HealAllTanks()
{
    for (int i = 1; i <= MaxClients; i++)
    {
        if (TR_IsAiTank(i))
        {
            TR_HealTank(i);
        }
    }
}

bool TR_FastSlotReclaimActive()
{
    if (g_cvFastSlotReclaim == null || !TR_FeatureEnabled() || g_bRoundEnded) return false;
    int mode = g_cvFastSlotReclaim.IntValue;
    return mode >= 2 || (mode == 1 && TR_IsTankRunMode());
}

int TR_CountDyingAiTankSlots()
{
    int count = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsClientInGame(i) && IsFakeClient(i) && !IsPlayerAlive(i)
            && GetClientTeam(i) == 3 && GetEntProp(i, Prop_Send, "m_zombieClass") == 8
            && !IsClientInKickQueue(i))
        {
            count++;
        }
    }
    return count;
}

int TR_ReclaimDyingAiTankSlots()
{
    if (!TR_FastSlotReclaimActive())
    {
        return 0;
    }
    int reclaimed = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsClientInGame(i) && IsFakeClient(i) && !IsPlayerAlive(i)
            && GetClientTeam(i) == 3 && GetEntProp(i, Prop_Send, "m_zombieClass") == 8
            && !IsClientInKickQueue(i))
        {
            KickClient(i, "Tank Run: releasing a dying AI Tank slot");
            reclaimed++;
        }
    }
    return reclaimed;
}

public void TR_KickRetiredTankFrame(any userid)
{
    int client = GetClientOfUserId(userid);
    if (client > 0 && IsClientInGame(client) && IsFakeClient(client) && !IsPlayerAlive(client)
        && GetClientTeam(client) == 3 && GetEntProp(client, Prop_Send, "m_zombieClass") == 8
        && !IsClientInKickQueue(client))
    {
        KickClient(client, "Tank Run: releasing a retired AI Tank slot");
    }
}

bool TR_KillTank(int client, const char[] source, bool releaseSlot = false)
{
    if (!TR_IsTankClient(client))
    {
        return false;
    }
    int userid = GetClientUserId(client);
    TR_LogEvent("击杀 Tank 客户端#%d（%s）", client, source);
    SDKHooks_TakeDamage(client, 0, 0, 100000.0, DMG_GENERIC);
    if (GetClientOfUserId(userid) == client && TR_IsTankClient(client))
    {
        ForcePlayerSuicide(client);
    }
    bool retired = GetClientOfUserId(userid) != client || !IsClientInGame(client) || !IsPlayerAlive(client);
    if (retired && releaseSlot && GetClientOfUserId(userid) == client && IsClientInGame(client)
        && IsFakeClient(client) && !IsClientInKickQueue(client))
    {
        RequestFrame(TR_KickRetiredTankFrame, userid);
    }
    return retired;
}

public void TR_ReleaseDeadTankSlot(any userid)
{
    if (!TR_FeatureEnabled() || g_bRoundEnded) return;
    if (!TR_FastSlotReclaimActive())
    {
        if (!TR_ManagedCapacityActive() || !g_bSessionReady || TC_ManagedMode()
            || GetClientCount(false) < MaxClients - TR_ReservedTankSlots()) return;
    }
    int client = GetClientOfUserId(userid);
    if (client > 0 && IsClientInGame(client) && IsFakeClient(client) && !IsPlayerAlive(client)
        && GetClientTeam(client) == 3 && GetEntProp(client, Prop_Send, "m_zombieClass") == 8
        && !IsClientInKickQueue(client))
    {
        KickClient(client, "Tank Run: releasing an occupied slot after Tank death");
    }
}

int TR_KillAllTanks(const char[] source)
{
    g_bFrontReplacement = false;
    int killed = TC_GetLiveReplicas();
    TC_ClearReplicas();
    for (int i = 1; i <= MaxClients; i++)
    {
        if (TR_IsAiTank(i) && TR_KillTank(i, source, true)) killed++;
    }
    return killed;
}

// 伤害倍率：只作用于 AI Tank（受伤与拳击伤害）
public Action TR_OnTakeDamage(int victim, int &attacker, int &inflictor, float &damage,
    int &damagetype, int &weapon, float damageForce[3], float damagePosition[3], int damagecustom)
{
    TR_BaitRockOnTakeDamage(victim, inflictor);
    if (!TR_FeatureEnabled() || damage <= 0.0)
    {
        return Plugin_Continue;
    }

    // v2.3 取消友伤（Tank Run 合作默认开；与 Tank 伤害倍率及拳击防护完整共存）
    if (TR_FF_ShouldBlockDamage(victim, attacker, inflictor, damagetype, weapon))
    {
        damage = 0.0;
        return Plugin_Handled;
    }

    bool changed = false;

    if (victim >= 1 && victim <= MaxClients && TR_IsAiTank(victim))
    {
        // 豁免自伤（如原生 tank_stuck_failsafe 触发的自杀）与插件直接处死，防止低倍率导致卡死自杀失败。
        bool isSelfOrSlay = (attacker == victim) || (attacker == 0 && inflictor == 0 && damage >= 99999.0);
        if (!isSelfOrSlay)
        {
            float scale = g_cvTankDmgScale.FloatValue;
            if (FloatAbs(scale - 1.0) > 0.001)
            {
                damage *= scale;
                changed = true;
            }
            if ((damagetype & DMG_BURN) != 0)
            {
                float fireScale = g_cvFireScale.FloatValue;
                if (FloatAbs(fireScale - 1.0) > 0.001)
                {
                    damage *= fireScale;
                    changed = true;
                }
            }
        }
    }

    if (attacker >= 1 && attacker <= MaxClients && TR_IsAiTank(attacker))
    {
        if (TR_IsSurvivor(victim) && TR_FirstRockClawGuard(attacker, GetGameTime(), true)
            && (TR_IsClawDamageEntity(weapon) || TR_IsClawDamageEntity(inflictor)))
        {
            if (!g_bTankTraversalDamageLogged[attacker])
            {
                g_bTankTraversalDamageLogged[attacker] = true;
                TR_Log("严格首石禁拳：拦截已进入判定的拳击伤害 Tank#%d userid=%d 玩家#%d", attacker, GetClientUserId(attacker), victim);
            }
            return Plugin_Handled;
        }
        if (TR_IsSurvivor(victim) && (TR_IsClawDamageEntity(weapon) || TR_IsClawDamageEntity(inflictor))
            && TR_ClawPolicyBlocks(attacker, GetGameTime(), victim)) return Plugin_Handled;
        float clawScale = g_cvClawScale.FloatValue;
        if (FloatAbs(clawScale - 1.0) > 0.001)
        {
            char cls[64];
            cls[0] = '\0';
            if (inflictor > 0 && IsValidEntity(inflictor))
            {
                GetEntityClassname(inflictor, cls, sizeof(cls));
            }
            if (StrContains(cls, "tank_claw", false) != -1 || StrContains(cls, "tank", false) == 0)
            {
                damage *= clawScale;
                changed = true;
            }
        }
    }

    if (changed)
    {
        if (damage < 0.0)
        {
            damage = 0.0;
        }
        return Plugin_Changed;
    }
    return Plugin_Continue;
}

public void TR_OnTakeDamagePost(int victim, int attacker, int inflictor, float damage,
    int damagetype, int weapon, const float damageForce[3], const float damagePosition[3])
{
    #pragma unused attacker, damage, damagetype, weapon, damageForce, damagePosition
    TR_BaitRockOnTakeDamage(victim, inflictor);
}

// ---------------------------------------------------------------- 自动生成
int TR_FindBirthOrderedTank(bool newest)
{
    int selected = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!TR_IsAiTank(i)) continue;
        // A tank found by scanning after a hot reload has no known birth order.
        if (g_iTankBirthOrder[i] == 0 || g_iTankUserId[i] != GetClientUserId(i)) return 0;
        if (selected == 0 || (newest ? g_iTankBirthOrder[i] > g_iTankBirthOrder[selected] : g_iTankBirthOrder[i] < g_iTankBirthOrder[selected])) selected = i;
    }
    return selected;
}

void TR_ResetFrontCycle()
{
    g_bHumanLeftStart = false;
    g_bFrontPhaseOpen = false;
    g_bFrontReplacement = false;
    g_fNextFrontPhaseProbe = 0.0;
    g_fAutoSpawnNext = 0.0;
}

bool TR_HumanHasLeftStart()
{
    if (g_bHumanLeftStart) return true;
    if (GetFeatureStatus(FeatureType_Native, "L4D_HasAnySurvivorLeftSafeArea") != FeatureStatus_Available
        || GetFeatureStatus(FeatureType_Native, "L4D_IsPositionInFirstCheckpoint") != FeatureStatus_Available
        || !L4D_HasAnySurvivorLeftSafeArea()) return false;

    for (int client = 1; client <= MaxClients; client++)
    {
        if (!TR_IsSurvivor(client) || IsFakeClient(client)) continue;
        float pos[3];
        GetClientAbsOrigin(client, pos);
        if (L4D_IsPositionInFirstCheckpoint(pos)
            || (GetFeatureStatus(FeatureType_Native, "L4D_IsInFirstCheckpoint") == FeatureStatus_Available
                && L4D_IsInFirstCheckpoint(client))) continue;
        char code[320], result[16];
        Format(code, sizeof(code), "local p = GetPlayerFromUserID(%d); Convars.SetValue(\"%s\", p && ResponseCriteria.GetValue(p, \"instartarea\") == \"0\" ? \"OUT\" : \"WAIT\");",
            GetClientUserId(client), TR_BRIDGE);
        if (!TR_RunVScript(code, result, sizeof(result)) || !StrEqual(result, "OUT")) continue;
        g_bHumanLeftStart = true;
        TR_Log("统一生成解锁：真人已离开起始安全区 userid=%d", GetClientUserId(client));
        return true;
    }
    return false;
}

bool TR_ManagedCapacityActive()
{
    return TR_FeatureEnabled() && TR_IsTankRunMode();
}

int TR_FrontTankCount()
{
    return TR_CountTanks(false) + TC_GetLiveReplicas();
}

int TR_ReservedTankSlots()
{
    ConVar humanSeats = FindConVar("tankrun_human_seats");
    return humanSeats != null && humanSeats.BoolValue ? 1 : 0;
}

int TR_NativeTankLimit(int nativeTanks)
{
    // Occupied slots include connecting clients and dead bots until their disconnect is processed.
    int dyingReclaimable = TR_FastSlotReclaimActive() ? TR_CountDyingAiTankSlots() : 0;
    int physical = nativeTanks + MaxClients - GetClientCount(false) + dyingReclaimable - TR_ReservedTankSlots();
    if (physical <= 0) return 0;
    int configured = g_cvAutoLimit.IntValue;
    return physical < configured ? physical : configured;
}

int TR_FrontEffectiveLimit()
{
    return TC_ManagedMode() ? g_cvAutoLimit.IntValue : TR_NativeTankLimit(TR_CountTanks(false));
}

public Action L4D_OnSpawnTank(const float pos[3], const float ang[3])
{
    if (!TR_ManagedCapacityActive()) return Plugin_Continue;
    if (g_bFrontSpawnOwned && !g_bRoundEnded && g_bHumanLeftStart
        && TR_FrontTankCount() < TR_FrontEffectiveLimit()) return Plugin_Continue;
    return Plugin_Handled;
}

int TR_SpawnFrontAt(const float pos[3], const float ang[3], int requester, char[] message, int msglen)
{
    if (TR_LeadSpawnNeedsCover() && !TR_IsLeadSpawnHidden(pos))
    {
        strcopy(message, msglen, "普通档刷点复核失败，等待遮挡位置");
        return 0;
    }
    int before = TR_FrontTankCount();
    int occupied = GetClientCount(false);
    int reserve = TR_ReservedTankSlots();
    if (occupied < MaxClients - reserve && g_bLeft4D)
    {
        int tank = TR_SpawnAt(pos, ang, requester, message, msglen);
        if (tank > 0) return tank;
        if (TR_FrontTankCount() > before || !TC_ManagedMode()) return 0;
    }
    else if (!TC_ManagedMode())
    {
        if (occupied >= MaxClients - reserve)
            Format(message, msglen, "原版槽位已满 %d/%d，交接保留 %d", occupied, MaxClients, reserve);
        else strcopy(message, msglen, "Left4DHooks 的原版生成接口不可用");
        return 0;
    }
    int replica = TC_TrySpawnManagedAt(pos, ang);
    if (replica > 0)
    {
        g_iStatSpawns++;
        Format(message, msglen, "已生成补位 Tank 实体 #%d", replica);
        return replica;
    }
    strcopy(message, msglen, "原版槽位或引擎生成受限，补位实体未生成");
    return 0;
}

// 校验原版 AI Tank 候选是否处于可释放客户端槽位的有效存活状态（排除真人、幽灵、已死/正在死亡、已在踢出队列）
bool TR_CanNativeTankReleaseSlot(int client)
{
    if (!TR_IsAiTank(client) || IsClientInKickQueue(client))
    {
        return false;
    }
    if (GetClientHealth(client) <= 0)
    {
        return false;
    }
    if (HasEntProp(client, Prop_Send, "m_lifeState") && GetEntProp(client, Prop_Send, "m_lifeState") != 0)
    {
        return false;
    }
    return true;
}

// 满员轮换递补候选筛选（不引入倒序反转，也不改变手动管理员操作）：
// 按原配置的最早出生顺序依次检查可释放本路径所需槽位的候选 Tank；
// 若候选处于投石/等待该石头结算保护，则沿同顺序递补查找下一候选；全部不合格则延迟本轮。
bool TR_RetireFrontTank(int &userid, bool nativeOnly = false)
{
    userid = 0;

    // 1) 校验原版 AI Tank 出生顺序完整性（热重载后未知顺序时保守不淘汰）并确认可释放原版客户端槽位
    bool candIsReplica[MAXPLAYERS + 32];
    int candId[MAXPLAYERS + 32];
    int candBirth[MAXPLAYERS + 32];
    float candTime[MAXPLAYERS + 32];
    int candCount = 0;

    for (int i = 1; i <= MaxClients; i++)
    {
        if (!TR_IsAiTank(i))
        {
            continue;
        }
        if (g_iTankBirthOrder[i] == 0 || g_iTankUserId[i] != GetClientUserId(i))
        {
            return false;
        }
        if (!TR_CanNativeTankReleaseSlot(i))
        {
            continue;
        }
        candIsReplica[candCount] = false;
        candId[candCount] = i;
        candBirth[candCount] = g_iTankBirthOrder[i];
        candTime[candCount] = g_fTankSpawnTime[i];
        candCount++;
    }

    // 2) 若本路径允许淘汰补位 Tank（即并非专程腾原版客户端槽位），纳入存活补位候选
    if (!nativeOnly && TC_ManagedMode())
    {
        int repSlots[32];
        int repBirths[32];
        float repTimes[32];
        int repCount = TC_CollectLiveReplicas(repSlots, repBirths, repTimes, sizeof(repSlots));
        for (int r = 0; r < repCount && candCount < sizeof(candId); r++)
        {
            candIsReplica[candCount] = true;
            candId[candCount] = repSlots[r];
            candBirth[candCount] = repBirths[r];
            candTime[candCount] = repTimes[r];
            candCount++;
        }
    }

    if (candCount == 0)
    {
        return false;
    }

    // 3) 按原配置的“最早出生优先”升序排序（绝不倒序反转）
    for (int a = 0; a < candCount - 1; a++)
    {
        for (int b = a + 1; b < candCount; b++)
        {
            bool swap = false;
            if (candTime[b] < candTime[a] - 0.0001)
            {
                swap = true;
            }
            else if (FloatAbs(candTime[b] - candTime[a]) <= 0.0001
                && candIsReplica[a] == candIsReplica[b] && candBirth[b] < candBirth[a])
            {
                swap = true;
            }
            if (swap)
            {
                bool tRep = candIsReplica[a]; candIsReplica[a] = candIsReplica[b]; candIsReplica[b] = tRep;
                int tId = candId[a]; candId[a] = candId[b]; candId[b] = tId;
                int tBirth = candBirth[a]; candBirth[a] = candBirth[b]; candBirth[b] = tBirth;
                float tTime = candTime[a]; candTime[a] = candTime[b]; candTime[b] = tTime;
            }
        }
    }

    // 4) 沿既有顺序逐只筛选，跳过处于投石/等待石头结算保护的候选
    int skipped = 0;
    char reason[64];
    for (int i = 0; i < candCount; i++)
    {
        if (candIsReplica[i])
        {
            int slot = candId[i];
            if (TC_IsReplicaRockProtected(slot, reason, sizeof(reason)))
            {
                skipped++;
                g_iStatRotateSkippedProtected++;
                TR_LogEvent("满员轮换递补跳过（容量路径）：slot=%d 原因=%s", slot, reason);
                continue;
            }
            if (TC_RetireReplicaSlot(slot))
            {
                if (skipped > 0)
                {
                    TR_Log("满员轮换递补选中（容量路径）：跳过 %d 只保护候选后淘汰补位 slot=%d", skipped, slot);
                }
                return true;
            }
        }
        else
        {
            int tank = candId[i];
            if (TR_IsNativeTankRockProtected(tank, reason, sizeof(reason)))
            {
                skipped++;
                g_iStatRotateSkippedProtected++;
                TR_LogEvent("满员轮换递补跳过（原版路径）：Tank#%d userid=%d 原因=%s", tank, GetClientUserId(tank), reason);
                continue;
            }
            userid = GetClientUserId(tank);
            if (TR_KillTank(tank, "统一生成腾位", true))
            {
                if (skipped > 0)
                {
                    TR_Log("满员轮换递补选中（原版路径）：跳过 %d 只保护候选后淘汰 Tank#%d userid=%d", skipped, tank, userid);
                }
                return true;
            }
            userid = 0;
        }
    }

    // 全部候选均不合格（处于投石保护或无法释放目标槽位）：延迟本轮
    g_iStatRotateDeferred++;
    TR_Log("满员轮换延迟本轮：全部 %d 只候选均处于投石/结算保护或不可淘汰（跳过=%d nativeOnly=%d）",
        candCount, skipped, nativeOnly ? 1 : 0);
    return false;
}

void TR_EnsureAutoTimer()
{
    g_bAutoRunning = g_bSessionReady && TR_ManagedCapacityActive() && g_cvAutoSpawn.BoolValue;
    if (!g_bAutoRunning) g_bFrontReplacement = false;
}

void TR_PollFrontPhase()
{
    if (!TR_ManagedCapacityActive() || g_bRoundEnded || !g_bSessionReady) return;
    float now = GetGameTime();
    if (now < g_fNextFrontPhaseProbe) return;
    g_fNextFrontPhaseProbe = now + (g_bFrontPhaseOpen ? 0.5 : 0.25);
    if (!TR_HumanHasLeftStart()) return;
    bool open = TR_VScriptReadFrontPhase();
    if (open && !g_bFrontPhaseOpen)
    {
        g_fAutoSpawnNext = now;
        TR_Log("统一生成启动：间隔 %.2f 秒，目标 %d，当前有效上限 %d，槽位 %d/%d，补位 %s",
            g_cvAutoInterval.FloatValue, g_cvAutoLimit.IntValue, TR_FrontEffectiveLimit(), GetClientCount(false), MaxClients,
            TC_ManagedMode() ? "开" : "关");
    }
    if (!open) g_bFrontReplacement = false;
    g_bFrontPhaseOpen = open;
}

void TR_ProcessTankReplacement()
{
    if (GetGameTickCount() == g_iReplacementTick) return;
    float now = GetGameTime();
    if (now < g_fReplacementRetry) return;
    TR_ReclaimDyingAiTankSlots();
    float elapsed = now - g_fReplacementStarted;
    float timeout = TR_FastSlotReclaimActive() ? 2.0 : 6.5;
    if (elapsed >= timeout)
    {
        g_bFrontReplacement = false;
        TR_Log("前线补位超时：旧 userid=%d，槽位 %d/%d（限时 %.1f 秒）", g_iReplacementUserId, GetClientCount(false), MaxClients, timeout);
        return;
    }
    if (GetClientCount(false) >= MaxClients - TR_ReservedTankSlots() && g_iReplacementUserId != 0
        && GetClientOfUserId(g_iReplacementUserId) != 0) return;
    if (TR_FrontTankCount() >= TR_FrontEffectiveLimit())
    {
        g_bFrontReplacement = false;
        return;
    }
    if (!TR_VScriptReadFrontPhase())
    {
        g_bFrontPhaseOpen = false;
        g_bFrontReplacement = false;
        return;
    }

    char site[192], message[192];
    if ((TR_FindLeadTankSpawn(g_fReplacementPos, g_fReplacementAngles, site, sizeof(site), true)
        || TR_FindLeadTankSpawn(g_fReplacementPos, g_fReplacementAngles, site, sizeof(site)))
        && TR_SpawnFrontAt(g_fReplacementPos, g_fReplacementAngles, 0, message, sizeof(message)) > 0)
    {
        g_bFrontReplacement = false;
        TR_Log("满员轮换补位：旧 userid=%d，%.0fms，%s；%s", g_iReplacementUserId, elapsed * 1000.0, site, message);
        return;
    }
    g_fReplacementRetry = now + 0.10;
}

void TR_AutoSpawnThink()
{
    float now = GetGameTime();
    float interval = g_cvAutoInterval.FloatValue;
    if (!(interval > 0.0 && interval <= TR_NUMERIC_MAX)) return;
    g_fAutoSpawnNext = now + interval;
    if (!TR_VScriptReadFrontPhase())
    {
        g_bFrontPhaseOpen = false;
        return;
    }

    int count = TR_FrontTankCount();
    int limit = TR_FrontEffectiveLimit();
    if (count > limit)
    {
        int retiredUser;
        if (g_cvRotateOldest.BoolValue && TR_RetireFrontTank(retiredUser))
            TR_Log("在场上限下降：淘汰旧 userid=%d，在场=%d，有效上限=%d", retiredUser, count, limit);
        return;
    }
    if (limit <= 0) return;

    float pos[3], ang[3];
    char site[192], message[192];
    if (!TR_FindLeadTankSpawn(pos, ang, site, sizeof(site)))
    {
        TR_Log("前线生成等待：%s", site);
        return;
    }
    if (count < limit)
    {
        int tank = TR_SpawnFrontAt(pos, ang, 0, message, sizeof(message));
        TR_Log("前线生成：%s；%s", site, message);
        if (tank > 0 && g_cvAutoDouble.BoolValue && TR_FrontTankCount() < TR_FrontEffectiveLimit()
            && TR_FindLeadTankSpawn(pos, ang, site, sizeof(site)))
        {
            TR_SpawnFrontAt(pos, ang, 0, message, sizeof(message));
            TR_Log("前线生成（双 Tank）：%s；%s", site, message);
        }
        if (tank > 0 || GetClientCount(false) < MaxClients - TR_ReservedTankSlots()) return;
    }

    // Select a viable front position before retiring anyone.
    if (!g_cvRotateOldest.BoolValue || !TR_RetireFrontTank(g_iReplacementUserId, count < limit)) return;
    TR_Log("满员轮换：旧 userid=%d，在场=%d/%d；%s", g_iReplacementUserId, count, limit, site);
    g_bFrontReplacement = true;
    g_iReplacementTick = GetGameTickCount();
    g_fReplacementStarted = now;
    g_fReplacementRetry = now;
    g_fReplacementPos = pos;
    g_fReplacementAngles = ang;
}

public void OnGameFrame()
{
    TR_BaitRockFrame();
    TC_RunGameFrame();
    TR_Glow_SyncAllNativeTanks();
    if (!g_bAutoRunning || !g_bFrontPhaseOpen || g_bRoundEnded) return;
    if (g_bFrontReplacement) TR_ProcessTankReplacement();
    else if (GetGameTime() >= g_fAutoSpawnNext) TR_AutoSpawnThink();
}

void TR_RestoreScriptThink()
{
    if ((g_bScriptThinkDisabled || g_iOriginalStockLimit > 0) && TR_IsMapRunning() && TR_IsTankRunMode())
    {
        TR_VScriptReleaseFrontPhase(g_bScriptThinkDisabled && g_bFrontPhaseOpen && !g_bRoundEnded);
    }
    g_bScriptThinkDisabled = false;
    g_iOriginalStockLimit = 0;
    g_iLastManagedStockLimit = 0;
    g_bFrontPhaseOpen = false;
}

// ---------------------------------------------------------------- 显示用文本
void TR_FormatTankLine(int client, char[] buffer, int maxlen)
{
    char kind[8];
    strcopy(kind, sizeof(kind), IsFakeClient(client) ? "AI" : "玩家");
    int hp = GetClientHealth(client);
    int maxhp = GetEntProp(client, Prop_Data, "m_iMaxHealth");
    char rockState[32];
    if (!TR_IsAiTank(client))
    {
        strcopy(rockState, sizeof(rockState), "不介入");
    }
    else if (g_bTankRockWindow[client])
    {
        strcopy(rockState, sizeof(rockState), "首石窗口");
    }
    else if (g_bTankStrategyOff[client])
    {
        strcopy(rockState, sizeof(rockState), "策略已停");
    }
    else if (g_bTankFirstContact[client])
    {
        strcopy(rockState, sizeof(rockState), "已接敌");
    }
    else
    {
        strcopy(rockState, sizeof(rockState), "未接敌");
    }

    int target = g_iTankChosenTarget[client];
    char targetText[32];
    if (target > 0 && target <= MaxClients && IsClientInGame(target))
    {
        char name[MAX_NAME_LENGTH];
        GetClientName(target, name, sizeof(name));
        strcopy(targetText, sizeof(targetText), name);
    }
    else
    {
        strcopy(targetText, sizeof(targetText), "未知");
    }

    Format(buffer, maxlen, "#%d %s 血%d/%d 投石%d 拳%d %s 目标:%s",
        client, kind, hp, maxhp, g_iTankReleaseCount[client],
        g_iTankSwingCount[client], rockState, targetText);
}

void TR_FormatTankDetail(int client, char[] buffer, int maxlen)
{
    char kind[8];
    strcopy(kind, sizeof(kind), IsFakeClient(client) ? "AI" : "玩家");
    int hp = GetClientHealth(client);
    int maxhp = GetEntProp(client, Prop_Data, "m_iMaxHealth");
    float lifetime = GetGameTime() - g_fTankSpawnTime[client];
    char first[32];
    if (g_bTankFirstContact[client])
    {
        Format(first, sizeof(first), "%.1f 秒前", GetGameTime() - g_fTankFirstContactTime[client]);
    }
    else
    {
        strcopy(first, sizeof(first), "未发生");
    }
    Format(buffer, maxlen,
        "客户端：#%d（%s）\n血量：%d / %d\n存活：%.1f 秒\n首次接敌：%s\n投石释放：%d 次（最近 %.1f 秒前）\n被拦截：%d 次\n拳击挥击：%d 次，命中：%d 次\n首石请求：%d 次\n首石策略：%s\n最近目标：%d",
        client, kind, hp, maxhp, lifetime, first,
        g_iTankReleaseCount[client],
        g_fTankLastRelease[client] > 0.0 ? GetGameTime() - g_fTankLastRelease[client] : -1.0,
        g_iTankBlockedCount[client], g_iTankSwingCount[client], g_iTankHitCount[client],
        g_iTankOverrides[client],
        g_bTankRockWindow[client] ? "窗口激活" : (g_bTankStrategyOff[client] ? "已停用" : "待命"),
        g_iTankChosenTarget[client]);
}
