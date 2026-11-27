// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

#if defined _tankrun_capacity_combat_included
    #endinput
#endif
#define _tankrun_capacity_combat_included

enum TCCombatAttack
{
    TC_ATTACK_NONE,
    TC_ATTACK_CLAW,
    TC_ATTACK_ROCK
}

enum TCCombatAnimation
{
    TC_ANIM_UNSET,
    TC_ANIM_IDLE,
    TC_ANIM_RUN,
    TC_ANIM_CLAW,
    TC_ANIM_ROCK,
    TC_ANIM_DEATH
}

#define TC_CLAW_REACH 105.0
#define TC_CLAW_DAMAGE 24.0
#define TC_ROCK_MIN_RANGE 270.0
#define TC_ROCK_FIRST_MIN_RANGE 220.0
#define TC_ROCK_MAX_RANGE 950.0
#define TC_ROCK_TURN_TIMEOUT 0.90
#define TC_ROCK_TURN_SETTLE 0.08
#define TC_ROCK_TURN_STEP 36.0
#define TC_ROCK_YAW_TOLERANCE 12.0
#define TC_ROCK_SEQUENCE "Throw_02"
#define TC_ROCK_LAST_FRAME 90.0
#define TC_ROCK_FRAME_RATE 30.0
// Shipped Throw_02: pickup at 17, AE_ATTACK_HIT at 30, actual native release at AE_ATTACK_END (74).
#define TC_ROCK_PICKUP_CYCLE (17.0 / TC_ROCK_LAST_FRAME)
#define TC_ROCK_RELEASE_CYCLE (74.0 / TC_ROCK_LAST_FRAME)
#define TC_ROCK_RELEASE_TIME (74.0 / TC_ROCK_FRAME_RATE)
#define TC_ROCK_DURATION (TC_ROCK_LAST_FRAME / TC_ROCK_FRAME_RATE)
#define TC_ROCK_MODEL "models/props_debris/concrete_chunk01a.mdl"
#define TC_RUN_STOP_GRACE 0.22
#define TC_TANK_POSE_MOVE_Y 3
#define TC_TANK_POSE_MOVE_X 4
#define TC_TANK_FORWARD_ANIM_SPEED 220.0
#define TC_ROCK_SETTLE_MAX_AGE 15.0

enum struct TCCombatState
{
    float health;
    float nextClaw;
    float nextRock;
    float attackHit;
    float attackEnd;
    float keepRunningUntil;
    float rockTurnDeadline;
    float rockAlignedAt;
    float rockAim[3];
    float lastRockCycle;
    float launchedRockAt;
    int attackTargetUid;
    int rockTurnTargetUid;
    int rockThrowGen;
    int killerUid;
    int heldRockRef;
    int launchedRockRef;
    int rockSequence;
    TCCombatAttack attack;
    TCCombatAnimation animation;
    bool applied;
    bool hasMovePose;
    bool hasPlaybackRate;
    bool hasRockCycle;
    bool firstRockPending;
    bool rockPickupShown;
}

TCCombatState g_TCCombat[TC_MAX_REPLICAS];
ConVar g_TCCombatRockToggle;
ConVar g_TCCombatRockForce;
ConVar g_TCCombatFirstRock;
bool g_TCCombatRockModelReady;

void TC_CombatPrecache()
{
    g_TCCombatRockToggle = FindConVar("tankrun_rock_enabled");
    g_TCCombatRockForce = FindConVar("z_tank_throw_force");
    g_TCCombatFirstRock = FindConVar("tankrun_firstrock_enabled");
    g_TCCombatRockModelReady = PrecacheModel(TC_ROCK_MODEL, true) > 0;
}

void TC_CombatRemoveHeldRock(int slot)
{
    int held = EntRefToEntIndex(g_TCCombat[slot].heldRockRef);
    g_TCCombat[slot].heldRockRef = 0;
    if (held > MaxClients && IsValidEntity(held)) RemoveEntity(held);
}

void TC_CombatReset(int slot)
{
    TC_CombatRemoveHeldRock(slot);
#if defined TR_EMBEDDED_CAPACITY
    TR_Glow_ClearReplicaTank(slot);
#endif
    TCCombatState empty;
    g_TCCombat[slot] = empty;
}

void TC_CombatOnRockDetonate(int rock)
{
    if (rock <= MaxClients || !IsValidEntity(rock)) return;
    int ref = EntIndexToEntRef(rock);
    for (int slot = 0; slot < TC_MAX_REPLICAS; slot++)
    {
        if (g_TCCombat[slot].launchedRockRef == ref)
        {
            g_TCCombat[slot].launchedRockRef = 0;
            g_TCCombat[slot].launchedRockAt = 0.0;
        }
    }
}

void TC_CombatOnEntityDestroyed(int entity)
{
    if (entity <= MaxClients) return;
    int ref = EntIndexToEntRef(entity);
    for (int slot = 0; slot < TC_MAX_REPLICAS; slot++)
    {
        if (g_TCCombat[slot].launchedRockRef == ref)
        {
            g_TCCombat[slot].launchedRockRef = 0;
            g_TCCombat[slot].launchedRockAt = 0.0;
        }
    }
}

// 满员轮换投石/等待该石头结算保护（容量路径）。
bool TC_CombatIsRockProtected(int slot, char[] reason = "", int reasonLen = 0)
{
    if (slot < 0 || slot >= TC_MAX_REPLICAS || g_TCCombat[slot].health <= 0.0) return false;
    if (g_TCCombat[slot].attack == TC_ATTACK_ROCK)
    {
        if (reasonLen > 0) strcopy(reason, reasonLen, "补位投石起手/动作保护");
        return true;
    }
    if (g_TCCombat[slot].rockTurnTargetUid != 0)
    {
        if (reasonLen > 0) strcopy(reason, reasonLen, "补位投石转向锁定保护");
        return true;
    }
#if defined TR_EMBEDDED_CAPACITY
    if (TR_BaitRockWaiting(g_TCEntityRefs[slot], slot))
    {
        if (reasonLen > 0) strcopy(reason, reasonLen, "补位原石等待当帧命中结算");
        return true;
    }
#endif
    int rockRef = g_TCCombat[slot].launchedRockRef;
    if (rockRef != 0 && rockRef != INVALID_ENT_REFERENCE)
    {
        int rockEnt = EntRefToEntIndex(rockRef);
        float elapsed = GetEngineTime() - g_TCCombat[slot].launchedRockAt;
        if (rockEnt > MaxClients && IsValidEntity(rockEnt) && elapsed >= 0.0 && elapsed <= TC_ROCK_SETTLE_MAX_AGE)
        {
            char cls[24];
            if (GetEntityClassname(rockEnt, cls, sizeof(cls)) && StrEqual(cls, "tank_rock"))
            {
                if (reasonLen > 0) strcopy(reason, reasonLen, "补位石头飞行待结算保护");
                return true;
            }
        }
        g_TCCombat[slot].launchedRockRef = 0;
        g_TCCombat[slot].launchedRockAt = 0.0;
    }
    return false;
}

bool TC_CombatAnimate(int slot, int entity, TCCombatAnimation animation)
{
    if (g_TCCombat[slot].animation == animation)
    {
        return true;
    }
    char sequence[24];
    switch (animation)
    {
        case TC_ANIM_IDLE: strcopy(sequence, sizeof(sequence), "Idle");
        case TC_ANIM_RUN: strcopy(sequence, sizeof(sequence), "Run_4");
        case TC_ANIM_CLAW: strcopy(sequence, sizeof(sequence), "Attack");
        case TC_ANIM_ROCK: strcopy(sequence, sizeof(sequence), TC_ROCK_SEQUENCE);
        case TC_ANIM_DEATH: strcopy(sequence, sizeof(sequence), "Death_11ab");
        default: return false;
    }
    // Let the prop resume the appropriate base sequence when a clip finishes.
    bool running = animation == TC_ANIM_RUN;
    bool hasMovePose = g_TCCombat[slot].hasMovePose;
    if (hasMovePose)
    {
        // Run_4 blends through a stationary pose unless the forward axis is set.
        SetEntPropFloat(entity, Prop_Send, "m_flPoseParameter", 0.5, TC_TANK_POSE_MOVE_Y);
        SetEntPropFloat(entity, Prop_Send, "m_flPoseParameter", running ? 1.0 : 0.5, TC_TANK_POSE_MOVE_X);
    }
    // A combat clip hands off to the movement controller at its last frame.
    // A prop default of Idle would race that handoff and restart two sequences.
    bool managedThrow = animation == TC_ANIM_ROCK && TC_ManagedMode();
    if (!managedThrow)
    {
        SetVariantString(running ? sequence : "Idle");
        AcceptEntityInput(entity, "SetDefaultAnimation");
    }
    int priorGoal = -1;
    if (managedThrow)
    {
        if (!g_TCCombat[slot].hasRockCycle) return false;
        priorGoal = GetEntProp(entity, Prop_Data, "m_iGoalSequence");
    }
    SetVariantString(sequence);
    if (!AcceptEntityInput(entity, "SetAnimation")) return false;
    if (managedThrow)
    {
        int expected = GetEntProp(entity, Prop_Data, "m_iGoalSequence");
        if (expected < 0 || expected == priorGoal)
        {
            g_TCCombat[slot].animation = TC_ANIM_UNSET;
            return false;
        }
        g_TCCombat[slot].rockSequence = expected;
        g_TCCombat[slot].lastRockCycle = -1.0;
    }
    // SDKTools reset the variant after SetAnimation. A void input clears the default string.
    if (managedThrow) AcceptEntityInput(entity, "SetDefaultAnimation");
    g_TCCombat[slot].animation = animation;
    if (hasMovePose)
    {
        SetVariantFloat(running ? TC_REPLICA_RUN_SPEED / TC_TANK_FORWARD_ANIM_SPEED : 1.0);
        AcceptEntityInput(entity, "SetPlaybackRate");
    }
    return true;
}

bool TC_CombatAttach(int slot, int entity, int health)
{
    if (!HasEntProp(entity, Prop_Data, "m_takedamage")
        || !HasEntProp(entity, Prop_Data, "m_iHealth")
        || !SDKHookEx(entity, SDKHook_OnTakeDamage, TC_CombatTakeDamage))
    {
        return false;
    }
    TC_CombatReset(slot);
    g_TCCombat[slot].health = float(health);
    float now = GetGameTime();
    bool firstRock = TC_ManagedMode() && g_TCCombatFirstRock != null && g_TCCombatFirstRock.BoolValue;
    g_TCCombat[slot].firstRockPending = firstRock;
    g_TCCombat[slot].nextRock = firstRock ? now : now + GetRandomFloat(3.0, 8.0);
    g_TCCombat[slot].hasMovePose = HasEntProp(entity, Prop_Send, "m_flPoseParameter")
        && GetEntPropArraySize(entity, Prop_Send, "m_flPoseParameter") > TC_TANK_POSE_MOVE_X;
    g_TCCombat[slot].hasPlaybackRate = HasEntProp(entity, Prop_Send, "m_flPlaybackRate");
    g_TCCombat[slot].hasRockCycle = HasEntProp(entity, Prop_Send, "m_flCycle")
        && HasEntProp(entity, Prop_Send, "m_nSequence") && HasEntProp(entity, Prop_Data, "m_iGoalSequence");
    // DAMAGE_YES enables damage dispatch even for a model without breakable-prop data.
    SetEntProp(entity, Prop_Data, "m_takedamage", 2, 1);
    SetEntProp(entity, Prop_Data, "m_iHealth", health);
    TC_CombatAnimate(slot, entity, TC_ANIM_IDLE);
    return true;
}

stock int TC_CombatHealth(int slot)
{
    return RoundToCeil(g_TCCombat[slot].health);
}

public Action TC_CombatTakeDamage(int victim, int &attacker, int &inflictor, float &damage,
    int &damageType, int &weapon, float damageForce[3], float damagePosition[3], int damageCustom)
{
    int slot = TC_ReplicaSlotForEntity(victim);
    if (slot < 0)
    {
        return Plugin_Continue;
    }
    if (!(damage > 0.0) || g_TCCombat[slot].health <= 0.0 || attacker == victim)
    {
        return Plugin_Handled;
    }
    if ((attacker > 0 && attacker <= MaxClients && IsClientInGame(attacker) && GetClientTeam(attacker) == 3)
        || (attacker > MaxClients && TC_ReplicaSlotForEntity(attacker) >= 0))
    {
        return Plugin_Handled;
    }
    // World-owned replica rocks should not damage fellow Tanks.
    if (inflictor > MaxClients && IsValidEntity(inflictor))
    {
        char classname[24];
        GetEntityClassname(inflictor, classname, sizeof(classname));
        if (StrEqual(classname, "tank_rock"))
        {
            return Plugin_Handled;
        }
        if (StrEqual(classname, "pipe_bomb_projectile") && TC_IsLivingSurvivor(attacker)) damage = 500.0;
    }
    // Stock Tank Run applies these modifiers only when the victim is a player Tank.
    if (weapon > MaxClients && IsValidEntity(weapon) && TC_IsLivingSurvivor(attacker))
    {
        char weaponClass[24];
        GetEntityClassname(weapon, weaponClass, sizeof(weaponClass));
        if (StrEqual(weaponClass, "weapon_pistol")) damage *= 1.25;
        else if (StrEqual(weaponClass, "weapon_melee")) damage *= 1.334;
    }

    g_TCCombat[slot].health -= damage;
    if (g_TCCombat[slot].health <= 0.0)
    {
        g_TCCombat[slot].health = 0.0;
        g_TCCombat[slot].killerUid = attacker > 0 && attacker <= MaxClients && IsClientInGame(attacker)
            ? GetClientUserId(attacker) : 0;
        SetEntProp(victim, Prop_Data, "m_takedamage", 0, 1);
        TC_CombatRemoveHeldRock(slot);
        TC_CombatAnimate(slot, victim, TC_ANIM_DEATH);
        // Let the engine finish the damage call before the encounter controller removes the actor.
        RequestFrame(TC_CombatFinishDeath, EntIndexToEntRef(victim));
    }
    else
    {
        SetEntProp(victim, Prop_Data, "m_iHealth", RoundToCeil(g_TCCombat[slot].health));
    }
    return Plugin_Handled;
}

public void TC_CombatFinishDeath(any reference)
{
    int entity = EntRefToEntIndex(reference);
    if (entity == INVALID_ENT_REFERENCE)
    {
        return;
    }
    int slot = TC_ReplicaSlotForEntity(entity);
    if (slot >= 0 && g_TCCombat[slot].health <= 0.0)
    {
        TC_ReplicaDied(slot, GetClientOfUserId(g_TCCombat[slot].killerUid));
    }
}

void TC_CombatMotion(int slot, int entity, bool moving, float now)
{
    if (g_TCCombat[slot].health <= 0.0 || g_TCCombat[slot].attack != TC_ATTACK_NONE) return;
    if (moving)
    {
        g_TCCombat[slot].keepRunningUntil = now + TC_RUN_STOP_GRACE;
        TC_CombatAnimate(slot, entity, TC_ANIM_RUN);
        // A model transition can reset the rate once when the goal sequence starts.
        float runRate = TC_REPLICA_RUN_SPEED / TC_TANK_FORWARD_ANIM_SPEED;
        if (g_TCCombat[slot].hasPlaybackRate && g_TCCombat[slot].hasRockCycle
            && GetEntProp(entity, Prop_Send, "m_nSequence") == GetEntProp(entity, Prop_Data, "m_iGoalSequence")
            && FloatAbs(GetEntPropFloat(entity, Prop_Send, "m_flPlaybackRate") - runRate) > 0.025)
        {
            SetVariantFloat(runRate);
            AcceptEntityInput(entity, "SetPlaybackRate");
        }
    }
    else if (now >= g_TCCombat[slot].keepRunningUntil)
    {
        TC_CombatAnimate(slot, entity, TC_ANIM_IDLE);
    }
}

bool TC_CombatRayFilter(int entity, int contentsMask, any ignored)
{
    return entity != ignored;
}

bool TC_CombatClearShot(int entity, int target, const float start[3], const float end[3])
{
    Handle trace = TR_TraceRayFilterEx(start, end, MASK_SHOT, RayType_EndPoint,
        TC_CombatRayFilter, entity, TRACE_EVERYTHING_FILTER_PROPS);
    bool clear = !TR_DidHit(trace) || TR_GetEntityIndex(trace) == target;
    delete trace;
    return clear;
}

void TC_CombatGetShot(int entity, int target, float start[3], float end[3])
{
    GetEntPropVector(entity, Prop_Data, "m_vecAbsOrigin", start);
    GetClientAbsOrigin(target, end);
    start[2] += 52.0;
    end[2] += 38.0;
}

bool TC_CombatRocksEnabled()
{
    return (g_TCCombatRockToggle == null || g_TCCombatRockToggle.BoolValue)
        && GetFeatureStatus(FeatureType_Native, "L4D_TankRockPrj") == FeatureStatus_Available;
}

void TC_CombatClaw(int slot, int entity, int target, const float start[3], const float end[3])
{
    #pragma unused slot
    float distance = GetVectorDistance(start, end);
    if (distance > TC_CLAW_REACH || !TC_CombatClearShot(entity, target, start, end))
    {
        return;
    }
    float bodyAngles[3], facing[3], force[3];
    GetEntPropVector(entity, Prop_Data, "m_angAbsRotation", bodyAngles);
    GetAngleVectors(bodyAngles, facing, NULL_VECTOR, NULL_VECTOR);
    MakeVectorFromPoints(start, end, force);
    force[2] = 0.0;
    NormalizeVector(force, force);
    if (distance > 20.0 && GetVectorDotProduct(facing, force) < 0.5)
    {
        return;
    }
    ScaleVector(force, 285.0);
    force[2] = 110.0;
    // The stock survivor damage code can assume an infected attacker is a player Tank.
    // Use world damage for the replica's scripted hit, as with its stock rock.
#if defined TR_EMBEDDED_CAPACITY
    bool wasAlive = TC_IsLivingSurvivor(target);
    TR_Bait_BeginReplicaClawDamage(slot, target);
#endif
    SDKHooks_TakeDamage(target, 0, 0, TC_CLAW_DAMAGE, DMG_CLUB, -1, force, start, false);
#if defined TR_EMBEDDED_CAPACITY
    TR_Bait_EndReplicaClawDamage(slot, target, wasAlive);
#endif
    // Prop attackers do not get the player-Tank animation and punch impulse automatically.
    if (TC_IsLivingSurvivor(target) && !GetEntProp(target, Prop_Send, "m_isIncapacitated"))
    {
        TeleportEntity(target, NULL_VECTOR, NULL_VECTOR, force);
    }
}

void TC_CombatClearRockTurn(int slot)
{
    g_TCCombat[slot].rockTurnTargetUid = 0;
    g_TCCombat[slot].rockTurnDeadline = 0.0;
    g_TCCombat[slot].rockAlignedAt = 0.0;
}

float TC_CombatNormalizeYaw(float angle)
{
    return angle - 360.0 * float(RoundToFloor((angle + 180.0) / 360.0));
}

bool TC_CombatTurnTowardRockTarget(int entity, const float start[3], const float end[3], float maxStep = TC_ROCK_TURN_STEP)
{
    float shot[3], desired[3], current[3];
    MakeVectorFromPoints(start, end, shot);
    shot[2] = 0.0;
    GetVectorAngles(shot, desired);
    GetEntPropVector(entity, Prop_Data, "m_angAbsRotation", current);
    float delta = TC_CombatNormalizeYaw(desired[1] - current[1]);
    if (FloatAbs(delta) > 0.1)
    {
        float step = delta;
        if (step > maxStep) step = maxStep;
        else if (step < -maxStep) step = -maxStep;
        current[1] = TC_CombatNormalizeYaw(current[1] + step);
        TeleportEntity(entity, NULL_VECTOR, current, NULL_VECTOR);
        int effects = GetEntProp(entity, Prop_Send, "m_fEffects");
        if (effects & TC_EF_NOINTERP) SetEntProp(entity, Prop_Send, "m_fEffects", effects & ~TC_EF_NOINTERP);
    }
    return FloatAbs(delta) <= TC_ROCK_YAW_TOLERANCE;
}

void TC_CombatLogRock(const char[] format, any ...)
{
    char message[192];
    VFormat(message, sizeof(message), format, 2);
#if defined TR_EMBEDDED_CAPACITY
    TR_Log("%s", message);
#else
    LogMessage("[TankCapacity] %s", message);
#endif
}

bool TC_CombatCreateHeldRock(int slot, int entity)
{
    if (!g_TCCombatRockModelReady || LookupEntityAttachment(entity, "debris") <= 0) return false;

    int held = CreateEntityByName("prop_dynamic_override");
    if (held <= MaxClients || !IsValidEntity(held)) return false;
    DispatchKeyValue(held, "model", TC_ROCK_MODEL);
    DispatchKeyValue(held, "solid", "0");
    DispatchKeyValue(held, "StartDisabled", "1");
    DispatchKeyValue(held, "DisableBoneFollowers", "1");
    if (DispatchSpawn(held))
    {
        SetVariantString("!activator");
        if (AcceptEntityInput(held, "SetParent", entity, held))
        {
            SetVariantString("debris");
            if (AcceptEntityInput(held, "SetParentAttachment", entity, held))
            {
                g_TCCombat[slot].heldRockRef = EntIndexToEntRef(held);
                return true;
            }
        }
    }
    if (IsValidEntity(held)) RemoveEntity(held);
    return false;
}

bool TC_CombatRockHullClear(int entity, const float start[3], const float end[3])
{
    static const float mins[3] = {-16.0, -16.0, -10.0};
    static const float maxs[3] = {16.0, 16.0, 10.0};
    Handle trace = TR_TraceHullFilterEx(start, end, mins, maxs, MASK_SOLID_BRUSHONLY,
        TC_CombatRayFilter, entity, TRACE_EVERYTHING_FILTER_PROPS);
    bool clear = !TR_DidHit(trace) && !TR_StartSolid(trace);
    delete trace;
    return clear;
}

bool TC_CombatHasThrowSpace(int entity, const float start[3], const float facing[3])
{
    float origin[3];
    // Reject tight spaces before playing the throw. Release itself uses the attachment only.
    for (int height = 0; height <= 1; height++)
    {
        float base[3];
        base = start;
        base[2] += float(height) * 24.0;
        for (int spacing = 0; spacing <= 1; spacing++)
        {
            float distance = spacing == 0 ? 54.0 : 40.0;
            origin[0] = base[0] + facing[0] * distance;
            origin[1] = base[1] + facing[1] * distance;
            origin[2] = base[2];
            if (TC_CombatRockHullClear(entity, base, origin)) return true;
        }
    }
    return false;
}

int TC_CombatLaunchRock(const float origin[3], const float velocity[3])
{
    float angles[3];
    GetVectorAngles(velocity, angles);
    int rock = L4D_TankRockPrj(0, origin, angles, velocity);
    if (rock <= MaxClients) return 0;
    if (!IsValidEntity(rock)) return -rock;

    char classname[24];
    GetEntityClassname(rock, classname, sizeof(classname));
    if (!StrEqual(classname, "tank_rock")) return -rock;
    if (g_TCCombatRockModelReady && GetEntProp(rock, Prop_Send, "m_nModelIndex") <= 0) SetEntityModel(rock, TC_ROCK_MODEL);
    const int noDraw = (1 << 5);
    int effects = GetEntProp(rock, Prop_Send, "m_fEffects");
    if (effects & noDraw) SetEntProp(rock, Prop_Send, "m_fEffects", effects & ~noDraw);
    return rock;
}

void TC_CombatThrow(int slot, int entity, int target, const float start[3], const float end[3])
{
    if (!TC_CombatRocksEnabled() || !TC_CombatClearShot(entity, target, start, end))
    {
        return;
    }
    float distance = GetVectorDistance(start, end);
    if (distance < TC_ROCK_MIN_RANGE * 0.75 || distance > TC_ROCK_MAX_RANGE * 1.25)
    {
        return;
    }
    float origin[3], velocity[3];
    float force = g_TCCombatRockForce == null ? 800.0 : g_TCCombatRockForce.FloatValue;
    if (!(force >= 300.0 && force <= 1500.0)) force = 800.0;
    MakeVectorFromPoints(start, end, velocity);
    NormalizeVector(velocity, velocity);
    for (int axis = 0; axis < 3; axis++) origin[axis] = start[axis] + 48.0 * velocity[axis];
    if (!TC_CombatClearShot(entity, target, start, origin))
    {
        return;
    }
    ScaleVector(velocity, force);
    velocity[2] += 150.0 + distance * 0.16;
    int rock = TC_CombatLaunchRock(origin, velocity);
    if (rock > 0)
    {
        g_TCCombat[slot].launchedRockRef = EntIndexToEntRef(rock);
        g_TCCombat[slot].launchedRockAt = GetEngineTime();
#if defined TR_EMBEDDED_CAPACITY
        TR_BaitRockReleased(slot, entity, rock);
#endif
    }
}

float TC_CombatRockCycle(int slot, int entity)
{
    int expected = g_TCCombat[slot].rockSequence;
    if (!g_TCCombat[slot].hasRockCycle || expected < 0 || GetEntProp(entity, Prop_Send, "m_nSequence") != expected
        || GetEntProp(entity, Prop_Data, "m_iGoalSequence") != expected) return -1.0;
    return GetEntPropFloat(entity, Prop_Send, "m_flCycle");
}

void TC_CombatTrackCommittedRock(int slot, int entity, int target)
{
    if (!TC_IsLivingSurvivor(target)) return;
    float start[3], end[3];
    TC_CombatGetShot(entity, target, start, end);
    float distance = GetVectorDistance(start, end);
    if (distance >= TC_ROCK_FIRST_MIN_RANGE * 0.75 && distance <= TC_ROCK_MAX_RANGE * 1.25
        && TC_CombatClearShot(entity, target, start, end))
    {
        g_TCCombat[slot].rockAim = end;
        TC_CombatTurnTowardRockTarget(entity, start, end);
    }
}

void TC_CombatTickRock(int slot, int entity)
{
    if (g_TCCombat[slot].attack != TC_ATTACK_ROCK || g_TCCombat[slot].applied) return;
    float cycle = TC_CombatRockCycle(slot, entity);
    if (cycle < 0.0 || cycle + 0.01 < g_TCCombat[slot].lastRockCycle) return;
    g_TCCombat[slot].lastRockCycle = cycle;

    int held = EntRefToEntIndex(g_TCCombat[slot].heldRockRef);
    if (!g_TCCombat[slot].rockPickupShown && cycle >= TC_ROCK_PICKUP_CYCLE)
    {
        if (held > MaxClients && IsValidEntity(held)) AcceptEntityInput(held, "TurnOn");
        g_TCCombat[slot].rockPickupShown = true;
    }
    if (cycle < TC_ROCK_RELEASE_CYCLE) return;

    // This edge is consumed once. A delayed retry would release from a different pose.
    g_TCCombat[slot].applied = true;
    g_TCCombat[slot].rockThrowGen = 0;
#if defined TR_EMBEDDED_CAPACITY
    TR_Glow_ClearReplicaTank(slot);
#endif
    float origin[3], attachmentAngles[3];
    int attachment = LookupEntityAttachment(entity, "debris");
    bool attachmentReady = attachment > 0 && GetEntityAttachment(entity, attachment, origin, attachmentAngles);
    if (!TC_CombatRocksEnabled() || !attachmentReady)
    {
        TC_CombatRemoveHeldRock(slot);
#if defined TR_EMBEDDED_CAPACITY
        TR_BaitRockCancelReplicaWindup(slot);
#endif
        g_TCCombat[slot].nextRock = GetGameTime() + 1.25;
        TC_CombatLogRock("补位投石取消：Tank实体 #%d seq=%d cycle=%.3f 原因=%s",
            entity, g_TCCombat[slot].rockSequence, cycle, attachmentReady ? "投石功能或接口已停用" : "读不到离手附件");
        return;
    }

    float bodyAngles[3], bodyForward[3], velocity[3], toAim[3];
    GetEntPropVector(entity, Prop_Data, "m_angAbsRotation", bodyAngles);
    bodyAngles[0] = 0.0;
    GetAngleVectors(bodyAngles, bodyForward, NULL_VECTOR, NULL_VECTOR);
    MakeVectorFromPoints(origin, g_TCCombat[slot].rockAim, toAim);
    float distance = NormalizeVector(toAim, toAim);
    float horizontal = SquareRoot(toAim[0] * toAim[0] + toAim[1] * toAim[1]);
    float force = g_TCCombatRockForce == null ? 800.0 : g_TCCombatRockForce.FloatValue;
    if (!(force >= 300.0 && force <= 1500.0)) force = 800.0;
    velocity = bodyForward;
    ScaleVector(velocity, horizontal * force);
    // Correct the small lateral offset from the animated hand, within the visible throw direction.
    if (GetVectorDotProduct(bodyForward, toAim) >= horizontal * Cosine(DegToRad(15.0)))
    {
        velocity[0] = toAim[0] * force;
        velocity[1] = toAim[1] * force;
    }
    velocity[2] = toAim[2] * force + 150.0 + distance * 0.16;

    float visibleOffset = -1.0;
    if (held > MaxClients && IsValidEntity(held))
    {
        float heldOrigin[3];
        GetEntPropVector(held, Prop_Data, "m_vecAbsOrigin", heldOrigin);
        visibleOffset = GetVectorDistance(origin, heldOrigin);
    }
    TC_CombatRemoveHeldRock(slot);
    int sequence = g_TCCombat[slot].rockSequence;
    int targetUserid = g_TCCombat[slot].attackTargetUid;
    int reference = EntIndexToEntRef(entity);
    AcceptEntityInput(entity, "DisableCollision");
    int rock = TC_CombatLaunchRock(origin, velocity);
    if (EntRefToEntIndex(reference) == entity)
    {
        AcceptEntityInput(entity, "EnableCollision");
        if (rock != 0) g_TCCombat[slot].firstRockPending = false;
        g_TCCombat[slot].nextRock = GetGameTime() + (rock != 0 ? GetRandomFloat(10.0, 16.0) : 1.25);
    }
    if (rock > 0 && EntRefToEntIndex(reference) == entity && g_TCEntityRefs[slot] == reference)
    {
        g_TCCombat[slot].launchedRockRef = EntIndexToEntRef(rock);
        g_TCCombat[slot].launchedRockAt = GetEngineTime();
#if defined TR_EMBEDDED_CAPACITY
        TR_BaitRockReleased(slot, entity, rock);
#endif
        float rockOrigin[3];
        GetEntPropVector(rock, Prop_Data, "m_vecAbsOrigin", rockOrigin);
        TC_CombatLogRock("补位投石离手：Tank实体 #%d rock=%d 目标userid=%d seq=%d cycle=%.3f 预期=%.3f 附件偏差=%.1f 出生偏差=%.1f",
            entity, rock, targetUserid, sequence, cycle, TC_ROCK_RELEASE_CYCLE, visibleOffset, GetVectorDistance(origin, rockOrigin));
    }
    else
    {
        TC_CombatLogRock("补位投石未确认：Tank实体 #%d rock=%d 目标userid=%d seq=%d cycle=%.3f",
            entity, -rock, targetUserid, sequence, cycle);
    }
}

bool TC_CombatThink(int slot, int entity, int target, float now)
{
    if (g_TCCombat[slot].health <= 0.0)
    {
        return true;
    }
    bool managed = TC_ManagedMode();
    if (g_TCCombat[slot].attack != TC_ATTACK_NONE)
    {
        int attackTarget = GetClientOfUserId(g_TCCombat[slot].attackTargetUid);
        bool managedRock = managed && g_TCCombat[slot].attack == TC_ATTACK_ROCK;
        if (managedRock)
        {
            if (!g_TCCombat[slot].applied)
            {
                TC_CombatTrackCommittedRock(slot, entity, attackTarget);
#if defined TR_EMBEDDED_CAPACITY
                TR_Glow_UpdateReplicaTank(slot, entity, true, g_TCCombat[slot].attackTargetUid, g_TCCombat[slot].rockThrowGen);
#endif
            }
            float cycle = TC_CombatRockCycle(slot, entity);
            bool finished = cycle >= 0.97 && g_TCCombat[slot].applied;
            if (!finished && now < g_TCCombat[slot].attackEnd + 0.35) return true;
            if (!g_TCCombat[slot].applied)
            {
                g_TCCombat[slot].nextRock = now + 1.25;
                TC_CombatLogRock("补位投石取消：Tank实体 #%d seq=%d cycle=%.3f 未经过对应离手帧",
                    entity, g_TCCombat[slot].rockSequence, cycle);
            }
            TC_CombatRemoveHeldRock(slot);
            g_TCCombat[slot].rockThrowGen = 0;
#if defined TR_EMBEDDED_CAPACITY
            TR_BaitRockCancelReplicaWindup(slot);
            TR_Glow_ClearReplicaTank(slot);
#endif
        }
        else
        {
            if (g_TCCombat[slot].attack == TC_ATTACK_ROCK && !g_TCCombat[slot].applied)
            {
#if defined TR_EMBEDDED_CAPACITY
                TR_Glow_UpdateReplicaTank(slot, entity, true, g_TCCombat[slot].attackTargetUid, g_TCCombat[slot].rockThrowGen);
#endif
            }
            if (!g_TCCombat[slot].applied && now >= g_TCCombat[slot].attackHit)
            {
                g_TCCombat[slot].applied = true;
                if (g_TCCombat[slot].attack == TC_ATTACK_CLAW)
                {
                    if (TC_IsLivingSurvivor(attackTarget))
                    {
                        float start[3], end[3];
                        TC_CombatGetShot(entity, attackTarget, start, end);
                        TC_CombatClaw(slot, entity, attackTarget, start, end);
                    }
#if defined TR_EMBEDDED_CAPACITY
                    if (TR_Bait_OnReplicaClawSettled(slot, entity))
                    {
                        TC_RemoveReplica(slot);
                        return true;
                    }
#endif
                }
                else
                {
                    if (TC_IsLivingSurvivor(attackTarget))
                    {
                        float start[3], end[3];
                        TC_CombatGetShot(entity, attackTarget, start, end);
                        TC_CombatThrow(slot, entity, attackTarget, start, end);
                    }
                    g_TCCombat[slot].rockThrowGen = 0;
#if defined TR_EMBEDDED_CAPACITY
                    TR_BaitRockCancelReplicaWindup(slot);
                    TR_Glow_ClearReplicaTank(slot);
#endif
                }
            }
            if (now < g_TCCombat[slot].attackEnd) return true;
        }
        g_TCCombat[slot].attack = TC_ATTACK_NONE;
    }
    if (managed && g_TCCombat[slot].rockTurnTargetUid != 0)
    {
        target = GetClientOfUserId(g_TCCombat[slot].rockTurnTargetUid);
    }
    if (!TC_IsLivingSurvivor(target))
    {
        TC_CombatClearRockTurn(slot);
        return false;
    }

    float start[3], end[3];
    TC_CombatGetShot(entity, target, start, end);
    float distance = GetVectorDistance(start, end);
    bool claw = now >= g_TCCombat[slot].nextClaw && distance <= TC_CLAW_REACH;
    bool firstRock = managed && g_TCCombat[slot].firstRockPending && g_TCCombatFirstRock != null && g_TCCombatFirstRock.BoolValue;
    float minRockRange = firstRock ? TC_ROCK_FIRST_MIN_RANGE : TC_ROCK_MIN_RANGE;
    bool rock = now >= g_TCCombat[slot].nextRock && distance >= minRockRange
        && distance <= TC_ROCK_MAX_RANGE && TC_CombatRocksEnabled();
    if ((!claw && !rock) || !TC_CombatClearShot(entity, target, start, end))
    {
        TC_CombatClearRockTurn(slot);
        return false;
    }

    if (managed && !claw)
    {
        if (g_TCCombat[slot].rockTurnTargetUid == 0)
        {
            g_TCCombat[slot].rockTurnTargetUid = GetClientUserId(target);
            g_TCCombat[slot].rockTurnDeadline = now + TC_ROCK_TURN_TIMEOUT;
        }
        if (now >= g_TCCombat[slot].rockTurnDeadline)
        {
            TC_CombatClearRockTurn(slot);
            g_TCCombat[slot].nextRock = now + 0.3;
            return false;
        }
        TC_CombatAnimate(slot, entity, TC_ANIM_IDLE);
        if (!TC_CombatTurnTowardRockTarget(entity, start, end))
        {
            g_TCCombat[slot].rockAlignedAt = 0.0;
            return true;
        }
        if (g_TCCombat[slot].rockAlignedAt == 0.0) g_TCCombat[slot].rockAlignedAt = now;
        if (now - g_TCCombat[slot].rockAlignedAt < TC_ROCK_TURN_SETTLE) return true;

        float facing[3];
        MakeVectorFromPoints(start, end, facing);
        facing[2] = 0.0;
        NormalizeVector(facing, facing);
        if (!TC_CombatHasThrowSpace(entity, start, facing))
        {
            TC_CombatClearRockTurn(slot);
            g_TCCombat[slot].nextRock = now + 0.30;
            return false;
        }
        if (!TC_CombatCreateHeldRock(slot, entity) || !TC_CombatAnimate(slot, entity, TC_ANIM_ROCK))
        {
            TC_CombatRemoveHeldRock(slot);
            TC_CombatClearRockTurn(slot);
            g_TCCombat[slot].nextRock = now + 1.25;
            TC_CombatLogRock("补位投石未开始：Tank实体 #%d，手持模型或投石动画初始化失败", entity);
            return false;
        }
        g_TCCombat[slot].rockAim = end;
        g_TCCombat[slot].rockPickupShown = false;
        TC_CombatLogRock("补位投石起手：Tank实体 #%d 目标userid=%d 距离=%.0f 首石=%d 手持实体=%d",
            entity, GetClientUserId(target), distance, firstRock, EntRefToEntIndex(g_TCCombat[slot].heldRockRef));
    }
    else
    {
        float facing[3], angles[3];
        MakeVectorFromPoints(start, end, facing);
        facing[2] = 0.0;
        GetVectorAngles(facing, angles);
        TeleportEntity(entity, NULL_VECTOR, angles, NULL_VECTOR);
    }
    TC_CombatClearRockTurn(slot);
    g_TCCombat[slot].attackTargetUid = GetClientUserId(target);
    g_TCCombat[slot].applied = false;
    if (claw)
    {
        // Attack has 46 frames at 38 fps, with AE_ATTACK_HIT at cycle 0.511.
        g_TCCombat[slot].attack = TC_ATTACK_CLAW;
        g_TCCombat[slot].attackHit = now + 0.605;
        g_TCCombat[slot].attackEnd = now + 1.185;
        g_TCCombat[slot].nextClaw = now + 1.6;
#if defined TR_EMBEDDED_CAPACITY
        TR_Bait_OnReplicaClawStart(slot, entity, target);
#endif
        TC_CombatAnimate(slot, entity, TC_ANIM_CLAW);
    }
    else
    {
        g_TCCombat[slot].attack = TC_ATTACK_ROCK;
        g_TCCombat[slot].attackHit = now + TC_ROCK_RELEASE_TIME;
        g_TCCombat[slot].attackEnd = now + TC_ROCK_DURATION;
        if (!managed)
        {
            g_TCCombat[slot].nextRock = now + GetRandomFloat(10.0, 16.0);
            TC_CombatAnimate(slot, entity, TC_ANIM_ROCK);
        }
#if defined TR_EMBEDDED_CAPACITY
        TR_BaitRockReplicaWindup(slot, g_TCCombat[slot].attackTargetUid);
        g_TCCombat[slot].rockThrowGen = TR_Glow_AllocThrowGen();
        TR_Glow_UpdateReplicaTank(slot, entity, true, g_TCCombat[slot].attackTargetUid, g_TCCombat[slot].rockThrowGen);
#endif
    }
    return true;
}
