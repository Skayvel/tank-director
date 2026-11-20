// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

#if defined _tankrun_capacity_nav_included
 #endinput
#endif
#define _tankrun_capacity_nav_included

#define TC_NAV_NODES 256
#define TC_NAV_PATH 48
#define TC_EF_NOINTERP (1 << 3)
#define TC_NAV_MAX_YAW_STEP 45.0

static const float TC_NAV_MINS[3] = {-24.0, -24.0, 2.0};
static const float TC_NAV_MAXS[3] = {24.0, 24.0, 74.0};
static const float TC_NAV_AVOID_ANGLES[] = {30.0, -30.0, 60.0, -60.0, 90.0, -90.0};

static Address g_TCNavPath[TC_MAX_REPLICAS][TC_NAV_PATH];
static Address g_TCNavDestination[TC_MAX_REPLICAS];
static int g_TCNavLength[TC_MAX_REPLICAS];
static int g_TCNavCursor[TC_MAX_REPLICAS];
static float g_TCNavReplan[TC_MAX_REPLICAS];
static float g_TCNavMoveStart[TC_MAX_REPLICAS][3];
static float g_TCNavMoveEnd[TC_MAX_REPLICAS][3];
static float g_TCNavMoveAt[TC_MAX_REPLICAS];
static float g_TCNavMoveYaw[TC_MAX_REPLICAS];
static float g_TCNavMoveYawDelta[TC_MAX_REPLICAS];
static bool g_TCNavMovePending[TC_MAX_REPLICAS];
static bool g_TCNavMoveTranslates[TC_MAX_REPLICAS];
static bool g_TCNavMoveStepUp[TC_MAX_REPLICAS];
static ArrayList g_TCNavNodes;
static ArrayList g_TCNavNeighbors;
static bool g_TCNavClosed[TC_NAV_NODES];
static int g_TCNavParent[TC_NAV_NODES];
static float g_TCNavCost[TC_NAV_NODES];
static float g_TCNavEstimate[TC_NAV_NODES];
static float g_TCNavCenter[TC_NAV_NODES][3];

void TC_NavReset(int slot)
{
    g_TCNavDestination[slot] = Address_Null;
    g_TCNavLength[slot] = 0;
    g_TCNavCursor[slot] = 0;
    g_TCNavReplan[slot] = 0.0;
    g_TCNavMovePending[slot] = false;
}

void TC_NavMapEnd()
{
    for (int slot = 0; slot < TC_MAX_REPLICAS; slot++)
    {
        TC_NavReset(slot);
    }
    delete g_TCNavNodes;
    delete g_TCNavNeighbors;
}

public bool TC_NavTraceFilter(int entity, int mask, any ignored)
{
    return entity != ignored;
}

static bool TC_NavCorridorClear(const float start[3], const float end[3], any ent)
{
    Handle trace = TR_TraceHullFilterEx(start, end, TC_NAV_MINS, TC_NAV_MAXS,
        MASK_PLAYERSOLID, TC_NavTraceFilter, ent, TRACE_EVERYTHING_FILTER_PROPS);
    bool clear = !TR_DidHit(trace) && !TR_StartSolid(trace);
    delete trace;
    return clear;
}

bool TC_NavSpawnHullClear(const float position[3])
{
    return TC_NavCorridorClear(position, position, -1);
}

static bool TC_NavGround(const float here[3], const float candidate[3], int ent, float ground[3])
{
    float top[3], bottom[3], normal[3];
    top = candidate;
    bottom = candidate;
    top[2] = here[2] + 34.0;
    bottom[2] = here[2] - 60.0;
    Handle trace = TR_TraceRayFilterEx(top, bottom, MASK_PLAYERSOLID_BRUSHONLY,
        RayType_EndPoint, TC_NavTraceFilter, ent, TRACE_EVERYTHING_FILTER_PROPS);
    bool found = TR_DidHit(trace) && !TR_StartSolid(trace);
    TR_GetEndPosition(ground, trace);
    if (found)
    {
        TR_GetPlaneNormal(trace, normal);
        found = normal[2] >= 0.60;
    }
    delete trace;
    ground[2] += 1.0;
    return found;
}

static bool TC_NavCanStep(const float here[3], const float there[3], int ent, bool &stepUp)
{
    stepUp = false;
    if (TC_NavCorridorClear(here, there, ent))
    {
        return true;
    }

    // A rising step needs a lift before crossing the ledge. Sweep each segment.
    float rise = there[2] - here[2];
    if (rise < 0.5 || rise > 32.0)
    {
        return false;
    }
    float raised[3];
    raised = here;
    raised[2] = there[2];
    stepUp = TC_NavCorridorClear(here, raised, ent) && TC_NavCorridorClear(raised, there, ent);
    return stepUp;
}

void TC_NavStopMotion(int slot)
{
    g_TCNavMovePending[slot] = false;
}

void TC_NavAdvanceMotion(int slot, int ent, float now)
{
    if (!g_TCNavMovePending[slot]) return;
    float progress = (now - g_TCNavMoveAt[slot]) / TC_INTERVAL;
    if (progress <= 0.0) return;
    if (progress >= 1.0)
    {
        progress = 1.0;
        g_TCNavMovePending[slot] = false;
    }

    float angles[3];
    angles[1] = g_TCNavMoveYaw[slot] + progress * g_TCNavMoveYawDelta[slot];
    angles[1] -= 360.0 * float(RoundToFloor((angles[1] + 180.0) / 360.0));
    if (!g_TCNavMoveTranslates[slot])
    {
        TeleportEntity(ent, NULL_VECTOR, angles, NULL_VECTOR);
        return;
    }

    float pos[3];
    float fractionXY = progress;
    float fractionZ = progress;
    if (g_TCNavMoveStepUp[slot])
    {
        // Keep the lifted and horizontal sweeps on the same path used by the collision check.
        float rise = g_TCNavMoveEnd[slot][2] - g_TCNavMoveStart[slot][2];
        float dx = g_TCNavMoveEnd[slot][0] - g_TCNavMoveStart[slot][0];
        float dy = g_TCNavMoveEnd[slot][1] - g_TCNavMoveStart[slot][1];
        float horizontal = SquareRoot(dx * dx + dy * dy);
        float fractionUp = rise / (rise + horizontal);
        fractionZ = progress < fractionUp ? progress / fractionUp : 1.0;
        fractionXY = progress < fractionUp || fractionUp >= 1.0 ? 0.0 : (progress - fractionUp) / (1.0 - fractionUp);
    }
    for (int axis = 0; axis < 3; axis++)
    {
        float fraction = axis == 2 ? fractionZ : fractionXY;
        pos[axis] = g_TCNavMoveStart[slot][axis] + fraction * (g_TCNavMoveEnd[slot][axis] - g_TCNavMoveStart[slot][axis]);
    }
    TeleportEntity(ent, pos, angles, NULL_VECTOR);
    int effects = GetEntProp(ent, Prop_Send, "m_fEffects");
    if (effects & TC_EF_NOINTERP) SetEntProp(ent, Prop_Send, "m_fEffects", effects & ~TC_EF_NOINTERP);
}

static void TC_NavQueueMotion(int slot, int ent, const float here[3], const float end[3], float yaw,
    bool translates, bool stepUp)
{
    float current[3];
    GetEntPropVector(ent, Prop_Data, "m_angAbsRotation", current);
    float delta = yaw - current[1];
    delta -= 360.0 * float(RoundToFloor((delta + 180.0) / 360.0));
    if (delta > TC_NAV_MAX_YAW_STEP) delta = TC_NAV_MAX_YAW_STEP;
    else if (delta < -TC_NAV_MAX_YAW_STEP) delta = -TC_NAV_MAX_YAW_STEP;
    g_TCNavMoveStart[slot] = here;
    g_TCNavMoveEnd[slot] = end;
    g_TCNavMoveAt[slot] = GetGameTime();
    g_TCNavMoveYaw[slot] = current[1];
    g_TCNavMoveYawDelta[slot] = delta;
    g_TCNavMoveTranslates[slot] = translates;
    g_TCNavMoveStepUp[slot] = stepUp;
    g_TCNavMovePending[slot] = translates || FloatAbs(delta) > 0.1;
}

static void TC_NavFace(int slot, int ent, const float here[3], const float there[3])
{
    float delta[3], angles[3];
    MakeVectorFromPoints(here, there, delta);
    delta[2] = 0.0;
    if (GetVectorLength(delta, true) > 1.0)
    {
        GetVectorAngles(delta, angles);
        TC_NavQueueMotion(slot, ent, here, here, angles[1], false, false);
    }
}

static bool TC_NavStep(int slot, int ent, const float here[3], const float goal[3], float maxStep, float deflection = 0.0)
{
    float delta[3], angles[3], candidate[3], ground[3];
    MakeVectorFromPoints(here, goal, delta);
    delta[2] = 0.0;
    float distance = GetVectorLength(delta);
    if (distance < 1.0)
    {
        return false;
    }
    GetVectorAngles(delta, angles);
    angles[1] += deflection;
    float step = distance < maxStep ? distance : maxStep;
    GetAngleVectors(angles, delta, NULL_VECTOR, NULL_VECTOR);
    ScaleVector(delta, step);
    AddVectors(here, delta, candidate);
    bool stepUp;
    if (!TC_NavGround(here, candidate, ent, ground) || !TC_NavCanStep(here, ground, ent, stepUp))
    {
        return false;
    }
    TC_NavQueueMotion(slot, ent, here, ground, angles[1], true, stepUp);
    return true;
}

static bool TC_NavCanSeeGoal(int ent, const float here[3], const float goal[3])
{
    if (FloatAbs(goal[2] - here[2]) > 52.0)
    {
        return false;
    }
    float start[3], end[3];
    start = here;
    end = goal;
    start[2] += 36.0;
    end[2] += 36.0;
    Handle trace = TR_TraceRayFilterEx(start, end, MASK_PLAYERSOLID_BRUSHONLY,
        RayType_EndPoint, TC_NavTraceFilter, ent, TRACE_EVERYTHING_FILTER_PROPS);
    bool visible = !TR_DidHit(trace);
    delete trace;
    return visible;
}

static void TC_NavSaveRoute(int slot, int last)
{
    int reversed[TC_NAV_NODES];
    int count = 0;
    for (int node = last; node > 0 && count < sizeof(reversed); node = g_TCNavParent[node])
    {
        reversed[count++] = node;
    }
    int length = count < TC_NAV_PATH ? count : TC_NAV_PATH;
    for (int i = 0; i < length; i++)
    {
        g_TCNavPath[slot][i] = g_TCNavNodes.Get(reversed[count - 1 - i]);
    }
    g_TCNavCursor[slot] = 0;
    g_TCNavLength[slot] = length;
}

// This search can be retried from the next NAV area when a very long route fills its budget.
static void TC_NavBuildRoute(int slot, Address start, Address goal)
{
    if (g_TCNavNodes == null)
    {
        g_TCNavNodes = new ArrayList();
        g_TCNavNeighbors = new ArrayList();
    }
    g_TCNavNodes.Clear();
    g_TCNavNodes.Push(start);
    g_TCNavParent[0] = -1;
    g_TCNavCost[0] = 0.0;
    g_TCNavClosed[0] = false;
    L4D_GetNavAreaCenter(start, g_TCNavCenter[0]);
    float destination[3];
    L4D_GetNavAreaCenter(goal, destination);
    g_TCNavEstimate[0] = GetVectorDistance(g_TCNavCenter[0], destination);

    int closest = 0;
    float closestDistance = g_TCNavEstimate[0];
    for (int expanded = 0; expanded < TC_NAV_NODES; expanded++)
    {
        int current = -1;
        for (int i = 0; i < g_TCNavNodes.Length; i++)
        {
            if (!g_TCNavClosed[i] && (current == -1 || g_TCNavEstimate[i] < g_TCNavEstimate[current]))
            {
                current = i;
            }
        }
        if (current == -1)
        {
            break;
        }
        Address area = g_TCNavNodes.Get(current);
        if (area == goal)
        {
            TC_NavSaveRoute(slot, current);
            return;
        }

        g_TCNavClosed[current] = true;
        for (int dir = NAV_NORTH; dir < NAV_ALL; dir++)
        {
            g_TCNavNeighbors.Clear();
            L4D_NavArea_GetAdjacentAreas(area, dir, g_TCNavNeighbors);
            for (int i = 0; i < g_TCNavNeighbors.Length; i++)
            {
                Address neighbor = g_TCNavNeighbors.Get(i);
                if (neighbor == Address_Null || L4D_NavArea_IsBlocked(neighbor, 3, false))
                {
                    continue;
                }
                int index = g_TCNavNodes.FindValue(neighbor);
                if ((index != -1 && g_TCNavClosed[index]) || (index == -1 && g_TCNavNodes.Length == TC_NAV_NODES))
                {
                    continue;
                }
                float center[3];
                L4D_GetNavAreaCenter(neighbor, center);
                float changeZ = center[2] - g_TCNavCenter[current][2];
                if (changeZ > 52.0 || changeZ < -72.0)
                {
                    continue;
                }
                float cost = g_TCNavCost[current] + GetVectorDistance(g_TCNavCenter[current], center);
                if (index != -1 && cost >= g_TCNavCost[index])
                {
                    continue;
                }
                if (index == -1)
                {
                    index = g_TCNavNodes.Push(neighbor);
                    g_TCNavCenter[index] = center;
                    g_TCNavClosed[index] = false;
                }
                float remaining = GetVectorDistance(center, destination);
                g_TCNavCost[index] = cost;
                g_TCNavEstimate[index] = cost + remaining;
                g_TCNavParent[index] = current;
                if (remaining < closestDistance)
                {
                    closest = index;
                    closestDistance = remaining;
                }
            }
        }
    }
    TC_NavSaveRoute(slot, closest);
}

static float TC_NavClamp(float value, float lower, float upper)
{
    if (value < lower) return lower;
    if (value > upper) return upper;
    return value;
}

static void TC_NavPortal(Address from, Address to, const float currentPos[3], float waypoint[3])
{
    L4D_GetNavAreaCenter(to, waypoint);
    if (from == Address_Null || from == to)
    {
        return;
    }
    float fromMin[3], fromMax[3], toMin[3], toMax[3];
    L4D_NavArea_GetCorner(from, NORTH_WEST, fromMin);
    L4D_NavArea_GetCorner(from, SOUTH_EAST, fromMax);
    L4D_NavArea_GetCorner(to, NORTH_WEST, toMin);
    L4D_NavArea_GetCorner(to, SOUTH_EAST, toMax);
    for (int axis = 0; axis < 2; axis++)
    {
        int other = 1 - axis;
        float lower = fromMin[other] > toMin[other] ? fromMin[other] : toMin[other];
        float upper = fromMax[other] < toMax[other] ? fromMax[other] : toMax[other];
        if (upper <= lower)
        {
            continue;
        }
        if (fromMax[axis] <= toMin[axis] + 1.0)
        {
            waypoint[axis] = TC_NavClamp(toMin[axis] + 32.0, toMin[axis], toMax[axis]);
        }
        else if (toMax[axis] <= fromMin[axis] + 1.0)
        {
            waypoint[axis] = TC_NavClamp(toMax[axis] - 32.0, toMin[axis], toMax[axis]);
        }
        else
        {
            continue;
        }
        float inset = (upper - lower) * 0.5;
        if (inset > 25.0) inset = 25.0;
        waypoint[other] = TC_NavClamp(currentPos[other], lower + inset, upper - inset);
        return;
    }
}

bool TC_MoveReplica(int slot, int ent, int target, float speed, float dt)
{
    if (speed <= 0.0 || dt <= 0.0)
    {
        return false;
    }
    float here[3], goal[3], waypoint[3];
    GetEntPropVector(ent, Prop_Data, "m_vecAbsOrigin", here);
    GetClientAbsOrigin(target, goal);
    float maxStep = speed * (dt < 0.15 ? dt : 0.15);
    bool direct = TC_NavCanSeeGoal(ent, here, goal);
    if (direct && GetVectorDistance(here, goal, true) < 72.0 * 72.0)
    {
        TC_NavFace(slot, ent, here, goal);
        return false;
    }
    if (direct && TC_NavStep(slot, ent, here, goal, maxStep))
    {
        return true;
    }

    Address current = L4D_GetNearestNavArea(here, 160.0, false, false, true, 3);
    float now = GetGameTime();
    if (now >= g_TCNavReplan[slot])
    {
        Address destination = L4D_GetNearestNavArea(goal, 200.0, false, false, false, 3);
        g_TCNavReplan[slot] = now + 0.75 + float(slot % 8) * 0.04;
        if (destination != g_TCNavDestination[slot] || g_TCNavCursor[slot] >= g_TCNavLength[slot])
        {
            g_TCNavDestination[slot] = destination;
            g_TCNavLength[slot] = 0;
            g_TCNavCursor[slot] = 0;
            if (current != Address_Null && destination != Address_Null && current != destination)
            {
                TC_NavBuildRoute(slot, current, destination);
            }
        }
    }
    for (int i = g_TCNavCursor[slot]; i < g_TCNavLength[slot]; i++)
    {
        if (g_TCNavPath[slot][i] == current)
        {
            g_TCNavCursor[slot] = i + 1;
            break;
        }
    }
    if (g_TCNavCursor[slot] < g_TCNavLength[slot])
    {
        TC_NavPortal(current, g_TCNavPath[slot][g_TCNavCursor[slot]], here, waypoint);
        if (TC_NavStep(slot, ent, here, waypoint, maxStep))
        {
            return true;
        }
    }
    else
    {
        waypoint = goal;
    }
    for (int i = 0; i < sizeof(TC_NAV_AVOID_ANGLES); i++)
    {
        if (TC_NavStep(slot, ent, here, waypoint, maxStep, TC_NAV_AVOID_ANGLES[i]))
        {
            return true;
        }
    }
    if (now >= g_TCNavReplan[slot] - 0.5)
    {
        g_TCNavLength[slot] = 0;
    }
    TC_NavFace(slot, ent, here, goal);
    return false;
}
