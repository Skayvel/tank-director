// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

// Model sequences exposed by Left4DHooks for the stock L4D2 Tank.
static const int TR_ClawSequences[] = {40, 43, 45};
static const int TR_GroundPoundSequences[] = {46, 47};
static const int TR_RockSequences[] = {48, 49, 50, 51};
static const int TR_RockActivities[] = {761, 762, 762, 762};

enum struct TR_RockMotion
{
    bool activating;
    bool animationRequested;
    bool animationSeen;
    int type;
    float windupUntil;
    float afterLockAt;
    float recoveryUntil;
    float returnUntil;
    int viewMode;
    bool locked;
    bool viewWriteFailed;
    int turnTick;
    int sampleTick;
    float lastView[3];
    float before[3];
    float view[3];
}

TR_RockMotion g_RockMotion[MAXPLAYERS + 1];

bool TR_AttackControlsActive(int tank)
{
    return TR_FeatureEnabled() && !g_bRoundEnded && TR_IsAiTank(tank);
}

void TR_ClearRockMotion(int tank)
{
    TR_RockMotion reset;
    reset.type = -1;
    reset.sampleTick = -1;
    reset.turnTick = -1;
    g_RockMotion[tank] = reset;
}

bool TR_InRockPhase(int tank, float now)
{
    return g_RockMotion[tank].windupUntil > 0.0 || g_RockMotion[tank].recoveryUntil > now;
}

int TR_ClawMask(int tank, float now)
{
    return g_cvClawTypes[TR_InRockPhase(tank, now) ? 1 : 0].IntValue;
}

bool TR_ClawWithinRange(int tank, int survivor, float range)
{
    float tankPos[3], tankMins[3], tankMaxs[3];
    float otherPos[3], otherMins[3], otherMaxs[3];
    GetClientAbsOrigin(tank, tankPos);
    GetClientMins(tank, tankMins);
    GetClientMaxs(tank, tankMaxs);
    GetClientAbsOrigin(survivor, otherPos);
    GetClientMins(survivor, otherMins);
    GetClientMaxs(survivor, otherMaxs);

    float distanceSquared;
    for (int axis = 0; axis < 3; axis++)
    {
        float left = tankPos[axis] + tankMins[axis] - otherPos[axis] - otherMaxs[axis];
        float right = otherPos[axis] + otherMins[axis] - tankPos[axis] - tankMaxs[axis];
        float gap = left > right ? left : right;
        if (gap > 0.0) distanceSquared += gap * gap;
    }
    return distanceSquared <= range * range;
}

bool TR_ClawPolicyBlocks(int tank, float now, int victim = 0)
{
    if (!TR_AttackControlsActive(tank)) return false;
    int phase = TR_InRockPhase(tank, now) ? 1 : 0;
    if (g_cvClawTypes[phase].IntValue == 0) return true;
    float range = g_cvClawRange[phase].FloatValue;
    if (range <= 0.0) return false;
    if (victim != 0) return !TR_ClawWithinRange(tank, victim, range);
    for (int i = 1; i <= MaxClients; i++)
    {
        if (TR_IsSurvivor(i) && TR_ClawWithinRange(tank, i, range)) return false;
    }
    return true;
}

int TR_FindAttackType(const int[] sequences, int count, int sequence)
{
    for (int i = 0; i < count; i++)
    {
        if (sequences[i] == sequence) return i;
    }
    return -1;
}

int TR_SelectAllowedAttack(int mask, int nativeType, int count)
{
    if ((mask & (1 << nativeType)) != 0) return nativeType;
    int allowed[sizeof(TR_RockSequences)];
    int used;
    for (int i = 0; i < count; i++)
    {
        if ((mask & (1 << i)) != 0) allowed[used++] = i;
    }
    return used > 0 ? allowed[GetRandomInt(0, used - 1)] : nativeType;
}

void TR_BeginRockActivation(int tank)
{
    g_RockMotion[tank].activating = true;
    g_RockMotion[tank].animationRequested = false;
    g_RockMotion[tank].animationSeen = false;
    g_RockMotion[tank].type = -1;
    g_RockMotion[tank].viewMode = TR_VIEW_NATIVE;
    g_RockMotion[tank].locked = false;
    g_RockMotion[tank].viewWriteFailed = false;
    g_RockMotion[tank].afterLockAt = 0.0;
    g_RockMotion[tank].returnUntil = 0.0;
    g_RockMotion[tank].recoveryUntil = 0.0;
    int age = GetGameTickCount() - g_RockMotion[tank].sampleTick;
    if (g_RockMotion[tank].sampleTick >= 0 && age <= 1)
        g_RockMotion[tank].before = g_RockMotion[tank].lastView;
    else
        GetClientEyeAngles(tank, g_RockMotion[tank].before);
}

void TR_LockRockView(int tank, const float angles[3])
{
    if (g_RockMotion[tank].locked) return;
    g_RockMotion[tank].view = angles;
    g_RockMotion[tank].locked = true;
    TR_LogEvent("投石视角锁定：Tank#%d 模式=%d 俯仰=%.2f 朝向=%.2f", tank,
        g_RockMotion[tank].viewMode, angles[0], angles[1]);
}

void TR_ConfirmRockWindup(int tank, float now)
{
    g_RockMotion[tank].animationRequested = false;
    g_RockMotion[tank].windupUntil = now + TR_ROCK_WINDUP_LIMIT;
    if (g_cvRockNoTurn.BoolValue) g_RockMotion[tank].viewMode = TR_VIEW_BEFORE;
    if (g_RockMotion[tank].viewMode == TR_VIEW_BEFORE) TR_LockRockView(tank, g_RockMotion[tank].before);
    else if (g_RockMotion[tank].viewMode == TR_VIEW_AFTER)
        g_RockMotion[tank].afterLockAt = now + g_cvRockAfterDelay.FloatValue;
}

Action TR_SelectRockActivity(int tank, int &activity)
{
    if (!g_RockMotion[tank].activating && !g_RockMotion[tank].animationRequested
        && g_RockMotion[tank].windupUntil <= GetGameTime()) return Plugin_Continue;
    int mask = g_cvRockTypes.IntValue;
    if (mask == TR_ROCK_ALL) return Plugin_Continue;
    int type = g_RockMotion[tank].type;
    int allowedFamily = (mask & 1) != 0 ? TR_RockActivities[0] : TR_RockActivities[1];
    if (type >= 0) allowedFamily = TR_RockActivities[type];
    else if ((activity == TR_RockActivities[0] && (mask & 1) != 0)
        || (activity == TR_RockActivities[1] && (mask & ~1) != 0)) return Plugin_Continue;
    if (mask == 0 || activity == allowedFamily) return Plugin_Continue;
    activity = allowedFamily;
    return Plugin_Handled;
}

public Action L4D2_OnSelectTankAttack(int client, int &sequence)
{
    if (!TR_AttackControlsActive(client)) return Plugin_Continue;
    int type = TR_FindAttackType(TR_ClawSequences, sizeof(TR_ClawSequences), sequence);
    if (type >= 0)
    {
        int selected = TR_SelectAllowedAttack(TR_ClawMask(client, GetGameTime()), type, sizeof(TR_ClawSequences));
        if (selected == type) return Plugin_Continue;
        sequence = TR_ClawSequences[selected];
        TR_LogEvent("拳种改选：Tank#%d 原版=%d 实际=%d", client, TR_ClawSequences[type], sequence);
        return Plugin_Handled;
    }

    if (TR_FindAttackType(TR_GroundPoundSequences, sizeof(TR_GroundPoundSequences), sequence) >= 0)
    {
        TR_LogEvent("砸地拳放行：Tank#%d 保留原版砸地序列=%d", client, sequence);
        return Plugin_Continue;
    }

    type = TR_FindAttackType(TR_RockSequences, sizeof(TR_RockSequences), sequence);
    if (type < 0 || (!g_RockMotion[client].activating && !g_RockMotion[client].animationRequested
        && g_RockMotion[client].windupUntil <= GetGameTime())) return Plugin_Continue;
    int selected = g_RockMotion[client].type;
    if (selected < 0)
    {
        int mask = g_cvRockTypes.IntValue;
        int family = TR_RockActivities[type] == TR_RockActivities[0] ? mask & 1 : mask & ~1;
        selected = TR_SelectAllowedAttack(family != 0 ? family : mask, type, sizeof(TR_RockSequences));
        g_RockMotion[client].type = selected;
        g_RockMotion[client].viewMode = g_cvRockNoTurn.BoolValue ? TR_VIEW_BEFORE : g_cvRockView[selected].IntValue;
        if (g_RockMotion[client].windupUntil > GetGameTime() && g_RockMotion[client].viewMode == TR_VIEW_BEFORE)
            TR_LockRockView(client, g_RockMotion[client].before);
        TR_LogEvent("投石选型：Tank#%d 原版=%d 实际=%d 锁定模式=%d", client,
            TR_RockSequences[type], TR_RockSequences[selected], g_RockMotion[client].viewMode);
    }
    if (g_cvRockTypes.IntValue == TR_ROCK_ALL) return Plugin_Continue;
    if (selected == type) return Plugin_Continue;
    sequence = TR_RockSequences[selected];
    return Plugin_Handled;
}

float TR_ViewDelta(float target, float current)
{
    float delta = target - current;
    while (delta > 180.0) delta -= 360.0;
    while (delta < -180.0) delta += 360.0;
    return delta;
}

void TR_EndRockWindup(int tank, float now, const char[] reason)
{
    if (g_RockMotion[tank].windupUntil <= 0.0) return;
    g_RockMotion[tank].windupUntil = 0.0;
    g_RockMotion[tank].recoveryUntil = now + g_cvRockRecovery.FloatValue;
    if (g_RockMotion[tank].locked)
        g_RockMotion[tank].returnUntil = g_RockMotion[tank].recoveryUntil + 1.0;
    TR_LogEvent("投石阶段结束起手：Tank#%d 原因=%s 恢复=%.2f 锁定=%d 俯仰=%.2f 朝向=%.2f", tank, reason,
        g_cvRockRecovery.FloatValue, g_RockMotion[tank].locked, g_RockMotion[tank].view[0], g_RockMotion[tank].view[1]);
}

void TR_UpdateRockMotion(int tank, float now, const float angles[3])
{
    if (g_RockMotion[tank].windupUntil <= 0.0) return;
    if (now >= g_RockMotion[tank].windupUntil)
    {
        TR_EndRockWindup(tank, now, "超时未离手");
        return;
    }
    if (TR_IsTankStaggering(tank))
    {
        TR_OnRockCancelled(tank, "起手阶段硬直中断");
        return;
    }
    int sequence = GetEntProp(tank, Prop_Send, "m_nSequence");
    int type = TR_FindAttackType(TR_RockSequences, sizeof(TR_RockSequences), sequence);
    if (type < 0)
    {
        if (g_RockMotion[tank].animationSeen) TR_EndRockWindup(tank, now, "投石动画中断");
        return;
    }
    if (!g_RockMotion[tank].animationSeen && type != g_RockMotion[tank].type)
    {
        TR_LogEvent("投石动画读回：Tank#%d 实际序列=%d 允许=%d", tank, sequence, (g_cvRockTypes.IntValue & (1 << type)) != 0);
        if (!g_RockMotion[tank].locked)
        {
            g_RockMotion[tank].type = type;
            g_RockMotion[tank].viewMode = g_cvRockNoTurn.BoolValue ? TR_VIEW_BEFORE : g_cvRockView[type].IntValue;
            if (g_RockMotion[tank].viewMode == TR_VIEW_BEFORE) TR_LockRockView(tank, g_RockMotion[tank].before);
            else if (g_RockMotion[tank].viewMode == TR_VIEW_AFTER)
                g_RockMotion[tank].afterLockAt = now + g_cvRockAfterDelay.FloatValue;
        }
    }
    g_RockMotion[tank].animationSeen = true;
    if (g_RockMotion[tank].viewMode == TR_VIEW_AFTER && !g_RockMotion[tank].locked)
    {
        if (g_RockMotion[tank].afterLockAt <= 0.0)
            g_RockMotion[tank].afterLockAt = now + g_cvRockAfterDelay.FloatValue;
        if (now >= g_RockMotion[tank].afterLockAt)
            TR_LockRockView(tank, angles);
    }
}

bool TR_GetControlledRockView(int tank, float now, const float current[3], float desired[3])
{
    if (!g_RockMotion[tank].locked) return false;
    bool keepLoft = g_cvRockKeepLoft != null && g_cvRockKeepLoft.BoolValue;
    if (!TR_InRockPhase(tank, now))
    {
        int tick = GetGameTickCount();
        if (g_RockMotion[tank].turnTick != tick)
        {
            float step = 180.0 * GetTickInterval();
            bool caughtUp = true;
            int startAxis = keepLoft ? 1 : 0;
            for (int axis = startAxis; axis < 2; axis++)
            {
                float delta = TR_ViewDelta(current[axis], g_RockMotion[tank].view[axis]);
                if (FloatAbs(delta) > step) caughtUp = false;
                if (delta > step) delta = step;
                if (delta < -step) delta = -step;
                g_RockMotion[tank].view[axis] = TR_ViewDelta(g_RockMotion[tank].view[axis] + delta, 0.0);
            }
            g_RockMotion[tank].turnTick = tick;
            if (caughtUp || now >= g_RockMotion[tank].returnUntil)
            {
                g_RockMotion[tank].locked = false;
                return false;
            }
        }
    }
    desired = g_RockMotion[tank].view;
    if (keepLoft) desired[0] = current[0];
    return true;
}

void TR_PostThinkRockView(int tank)
{
    if (!TR_AttackControlsActive(tank) || !g_cvRockEnabled.BoolValue) return;
    float now = GetGameTime();
    float current[3], desired[3];
    GetClientEyeAngles(tank, current);
    TR_UpdateRockMotion(tank, now, current);
    if (TR_GetControlledRockView(tank, now, current, desired))
    {
        if (FloatAbs(TR_ViewDelta(desired[0], current[0])) < 0.05
            && FloatAbs(TR_ViewDelta(desired[1], current[1])) < 0.05) return;
        TeleportEntity(tank, NULL_VECTOR, desired, NULL_VECTOR);
        GetClientEyeAngles(tank, current);
        if (!g_RockMotion[tank].viewWriteFailed
            && (FloatAbs(TR_ViewDelta(desired[0], current[0])) >= 0.05
                || FloatAbs(TR_ViewDelta(desired[1], current[1])) >= 0.05))
        {
            g_RockMotion[tank].viewWriteFailed = true;
            TR_Log("投石视角读回偏差：Tank#%d 请求=(%.2f,%.2f) 实际=(%.2f,%.2f)", tank,
                desired[0], desired[1], current[0], current[1]);
        }
        return;
    }
    if (!TR_InRockPhase(tank, now))
    {
        g_RockMotion[tank].lastView = current;
        g_RockMotion[tank].sampleTick = GetGameTickCount();
    }
}

bool TR_ControlRockCmd(int tank, float now, float move[3], float angles[3])
{
    if (!g_cvRockEnabled.BoolValue || !g_RockMotion[tank].locked) return false;
    float desired[3];
    if (!TR_GetControlledRockView(tank, now, angles, desired)) return false;
    float yawChange = DegToRad(TR_ViewDelta(angles[1], desired[1]));
    float forwardMove = move[0], sideMove = move[1];
    // Preserve the bot's intended world-space movement when replacing the command yaw.
    move[0] = forwardMove * Cosine(yawChange) + sideMove * Sine(yawChange);
    move[1] = sideMove * Cosine(yawChange) - forwardMove * Sine(yawChange);
    angles = desired;
    return true;
}

bool TR_ApplyRockReleaseDirection(int tank, float velocity[3])
{
    if (g_RockMotion[tank].windupUntil <= 0.0) return false;
    if (!g_RockMotion[tank].locked && g_RockMotion[tank].viewMode == TR_VIEW_AFTER)
    {
        float angles[3];
        GetClientEyeAngles(tank, angles);
        TR_LockRockView(tank, angles);
    }
    if (!g_RockMotion[tank].locked) return false;
    float yaw = DegToRad(g_RockMotion[tank].view[1]);
    float horizontalSpeed = SquareRoot(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
    velocity[0] = horizontalSpeed * Cosine(yaw);
    velocity[1] = horizontalSpeed * Sine(yaw);
    return true;
}
