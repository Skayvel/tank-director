// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

// Tank Run event spawns use the living human survivor with highest NAV flow.

#if defined _tankrun_lead_spawn_included
 #endinput
#endif
#define _tankrun_lead_spawn_included

#define TR_LEAD_DISTANCE_BANDS 3
#define TR_LEAD_GROUND_SECTORS 8
#define TR_LEAD_NAV_SAMPLES_PER_BAND 6

static ArrayList g_hLeadNavCenters;

enum struct TR_LeadSpawnSearch
{
    int client;
    Address area;
    float origin[3];
    float flow;
    float minDistance;
    float maxDistance;
    bool requireCover;
    int bandPriority[TR_LEAD_DISTANCE_BANDS];
    bool found;
    int selectedBand;
    bool ahead;
    float randomRank;
    float candidateFlow;
    float position[3];
}

void TR_ClearLeadSpawnNavCache()
{
    delete g_hLeadNavCenters;
}

bool TR_LeadFlowValid(float flow)
{
    return flow >= 0.0 && flow < TR_NUMERIC_MAX;
}

bool TR_FindLeadSurvivor(TR_LeadSpawnSearch search)
{
    bool haveLastArea = GetFeatureStatus(FeatureType_Native, "L4D_GetLastKnownArea") == FeatureStatus_Available;
    search.client = 0;
    for (int client = 1; client <= MaxClients; client++)
    {
        if (!TR_IsSurvivor(client) || IsFakeClient(client)) continue;

        float origin[3];
        GetClientAbsOrigin(client, origin);
        Address area = view_as<Address>(L4D_GetNearestNavArea(origin, 120.0, false, true, false, 2));
        if (area == Address_Null && haveLastArea) area = view_as<Address>(L4D_GetLastKnownArea(client));
        if (area == Address_Null) continue;

        float flow = L4D2Direct_GetTerrorNavAreaFlow(area);
        if (!TR_LeadFlowValid(flow) || (search.client != 0 && flow <= search.flow)) continue;
        search.client = client;
        search.area = area;
        search.origin = origin;
        search.flow = flow;
    }
    return search.client != 0;
}

int TR_LeadDistanceBand(TR_LeadSpawnSearch search, float distance)
{
    float width = (search.maxDistance - search.minDistance) / float(TR_LEAD_DISTANCE_BANDS);
    if (width <= 0.0 || distance < search.minDistance + width) return 0;
    return distance < search.maxDistance - width ? 1 : 2;
}

bool TR_LeadSpawnNeedsCover()
{
    int rule = g_cvSpawnVisibility.IntValue;
    if (rule != 0) return rule == 1;

    char difficulty[32];
    TR_GetDifficulty(difficulty, sizeof(difficulty));
    return !StrEqual(difficulty, "Hard", false) && !StrEqual(difficulty, "Impossible", false);
}

public bool TR_LeadSpawnCoverFilter(int entity, int contentsMask, any data)
{
    // A player model is not map cover for another player.
    return entity <= 0 || entity > MaxClients;
}

bool TR_IsLeadSpawnHidden(const float position[3])
{
    // Sample the 48x48x80 spawn hull. Test its center first to reject open locations quickly.
    static const float offsets[][3] = {
        {0.0, 0.0, 42.0}, {0.0, 0.0, 77.0}, {0.0, 0.0, 6.0},
        {-22.0, -22.0, 77.0}, {-22.0, 22.0, 77.0}, {22.0, -22.0, 77.0}, {22.0, 22.0, 77.0},
        {-22.0, -22.0, 42.0}, {-22.0, 22.0, 42.0}, {22.0, -22.0, 42.0}, {22.0, 22.0, 42.0},
        {-22.0, -22.0, 6.0}, {-22.0, 22.0, 6.0}, {22.0, -22.0, 6.0}, {22.0, 22.0, 6.0}
    };

    bool checkedViewer;
    for (int client = 1; client <= MaxClients; client++)
    {
        if (!TR_IsSurvivor(client)) continue;
        checkedViewer = true;
        float eye[3], sample[3];
        GetClientEyePosition(client, eye);
        for (int i = 0; i < sizeof(offsets); i++)
        {
            AddVectors(position, offsets[i], sample);
            TR_TraceRayFilter(eye, sample, MASK_VISIBLE, RayType_EndPoint, TR_LeadSpawnCoverFilter, 0, TRACE_EVERYTHING_FILTER_PROPS);
            if (TR_StartSolid() || !TR_DidHit() || TR_GetFraction() >= 1.0) return false;
        }
    }
    return checkedViewer;
}

void TR_ConsiderLeadTankSpawn(TR_LeadSpawnSearch search, const float position[3])
{
    float distance = GetVectorDistance(search.origin, position);
    if (distance < search.minDistance || distance > search.maxDistance || FloatAbs(search.origin[2] - position[2]) > 220.0) return;

    int band = TR_LeadDistanceBand(search, distance);
    if (search.found && search.bandPriority[band] > search.bandPriority[search.selectedBand]) return;

    Address area = view_as<Address>(L4D_GetNearestNavArea(position, 72.0, false, true, false, 3));
    if (area == Address_Null) return;
    float flow = L4D2Direct_GetTerrorNavAreaFlow(area);
    if (!TR_LeadFlowValid(flow) || flow < search.flow) return;

    // The distance band is picked at random. Within it, prefer forward NAV and randomly rank the points.
    bool ahead = flow > search.flow;
    float rank = GetRandomFloat(0.0, 1.0);
    if (search.found && band == search.selectedBand
        && ((search.ahead && !ahead) || (search.ahead == ahead && rank <= search.randomRank))) return;

    const int forbidden = NAV_SPAWN_PLAYER_START | NAV_SPAWN_CHECKPOINT | NAV_SPAWN_EMPTY | NAV_SPAWN_NOTHREAT;
    if ((L4D_GetNavArea_SpawnAttributes(area) & forbidden) != 0 || L4D_NavArea_IsBlocked(area, 3, false)) return;
    int closest;
    if (TR_NearestSurvivorDistance(position, closest) < search.minDistance || !TR_IsSpawnPosClear(position)) return;
    if (search.requireCover && !TR_IsLeadSpawnHidden(position)) return;

    // Check the direction the Tank will actually traverse, including current doors and blockers.
    if (area != search.area && !L4D2_NavAreaBuildPath(area, search.area, search.maxDistance * 2.0, 3, false)) return;
    search.found = true;
    search.selectedBand = band;
    search.ahead = ahead;
    search.randomRank = rank;
    search.candidateFlow = flow;
    search.position = position;
}

bool TR_ProjectLeadGround(const float anchor[3], float rise, float drop, float point[3])
{
    float top[3], bottom[3], normal[3];
    top = anchor;
    bottom = anchor;
    top[2] += rise;
    bottom[2] -= drop;
    TR_TraceRayFilter(top, bottom, MASK_PLAYERSOLID_BRUSHONLY, RayType_EndPoint, TR_FilterNothing, 0);
    if (!TR_DidHit() || TR_StartSolid()) return false;

    TR_GetPlaneNormal(INVALID_HANDLE, normal);
    if (normal[2] <= 0.5) return false;
    TR_GetEndPosition(point);
    point[2] += 6.0;
    return true;
}

void TR_SampleLeadGround(TR_LeadSpawnSearch search)
{
    float offset = GetRandomFloat(0.0, 6.2831853);
    float width = (search.maxDistance - search.minDistance) / float(TR_LEAD_DISTANCE_BANDS);
    for (int band = 0; band < TR_LEAD_DISTANCE_BANDS; band++)
    {
        float inner = search.minDistance + width * float(band);
        for (int sector = 0; sector < TR_LEAD_GROUND_SECTORS; sector++)
        {
            float radius = inner + GetRandomFloat(0.0, width);
            float yaw = offset + 0.78539816 * (float(sector) + GetRandomFloat(0.0, 1.0));
            float anchor[3], point[3];
            anchor = search.origin;
            anchor[0] += Cosine(yaw) * radius;
            anchor[1] += Sine(yaw) * radius;
            if (TR_ProjectLeadGround(anchor, 144.0, 208.0, point)) TR_ConsiderLeadTankSpawn(search, point);
        }
        offset += 0.39269908;
    }
}

bool TR_LoadLeadNavCenters()
{
    if (g_hLeadNavCenters != null) return g_hLeadNavCenters.Length > 0;
    if (GetFeatureStatus(FeatureType_Native, "L4D_GetAllNavAreas") != FeatureStatus_Available
        || GetFeatureStatus(FeatureType_Native, "L4D_GetNavAreaCenter") != FeatureStatus_Available) return false;

    ArrayList areas = new ArrayList();
    L4D_GetAllNavAreas(areas);
    g_hLeadNavCenters = new ArrayList(3);
    for (int i = 0; i < areas.Length; i++)
    {
        Address area = view_as<Address>(areas.Get(i));
        if (area == Address_Null) continue;
        float center[3];
        L4D_GetNavAreaCenter(area, center);
        g_hLeadNavCenters.PushArray(center);
    }
    delete areas;
    return g_hLeadNavCenters.Length > 0;
}

void TR_SampleLeadNav(TR_LeadSpawnSearch search)
{
    if (!TR_LoadLeadNavCenters()) return;

    int seen[TR_LEAD_DISTANCE_BANDS];
    int samples[TR_LEAD_DISTANCE_BANDS][TR_LEAD_NAV_SAMPLES_PER_BAND];
    float minSquared = search.minDistance * search.minDistance;
    float maxSquared = search.maxDistance * search.maxDistance;
    int centerCount = g_hLeadNavCenters.Length;
    for (int i = 0; i < centerCount; i++)
    {
        float center[3];
        g_hLeadNavCenters.GetArray(i, center);
        if (FloatAbs(search.origin[2] - center[2]) > 220.0) continue;
        float squared = GetVectorDistance(search.origin, center, true);
        if (squared < minSquared || squared > maxSquared) continue;

        int band = TR_LeadDistanceBand(search, SquareRoot(squared));
        if (search.found && search.bandPriority[band] > search.bandPriority[search.selectedBand]) continue;
        // Keep a bounded random sample for each band, even on large navigation meshes.
        int slot = seen[band]++;
        if (slot >= TR_LEAD_NAV_SAMPLES_PER_BAND) slot = GetRandomInt(0, slot);
        if (slot < TR_LEAD_NAV_SAMPLES_PER_BAND) samples[band][slot] = i;
    }

    for (int band = 0; band < TR_LEAD_DISTANCE_BANDS; band++)
    {
        for (int i = 0; i < seen[band] && i < TR_LEAD_NAV_SAMPLES_PER_BAND; i++)
        {
            float center[3], point[3];
            g_hLeadNavCenters.GetArray(samples[band][i], center);
            if (TR_ProjectLeadGround(center, 40.0, 64.0, point)) TR_ConsiderLeadTankSpawn(search, point);
        }
    }
}

bool TR_FindLeadTankSpawn(float pos[3], float ang[3], char[] reason, int reasonLen, bool revalidate = false)
{
    if (GetFeatureStatus(FeatureType_Native, "L4D_GetNearestNavArea") != FeatureStatus_Available
        || GetFeatureStatus(FeatureType_Native, "L4D2Direct_GetTerrorNavAreaFlow") != FeatureStatus_Available
        || GetFeatureStatus(FeatureType_Native, "L4D_GetNavArea_SpawnAttributes") != FeatureStatus_Available
        || GetFeatureStatus(FeatureType_Native, "L4D_NavArea_IsBlocked") != FeatureStatus_Available
        || GetFeatureStatus(FeatureType_Native, "L4D2_NavAreaBuildPath") != FeatureStatus_Available)
    {
        strcopy(reason, reasonLen, "领先选位所需的 Left4DHooks NAV 接口不可用");
        return false;
    }

    TR_LeadSpawnSearch search;
    if (!TR_FindLeadSurvivor(search))
    {
        strcopy(reason, reasonLen, "没有处于有效 NAV 区域的存活真人幸存者");
        return false;
    }

    search.minDistance = g_cvSpawnMin.FloatValue;
    search.maxDistance = g_cvSpawnMax.FloatValue;
    search.requireCover = TR_LeadSpawnNeedsCover();
    if (!(search.minDistance >= 0.0 && search.maxDistance >= search.minDistance && search.maxDistance <= 10000.0))
    {
        Format(reason, reasonLen, "生成距离范围无效：最小 %.0f，最大 %.0f，应满足 0 ≤ 最小 ≤ 最大 ≤ 10000",
            search.minDistance, search.maxDistance);
        return false;
    }

    if (revalidate)
    {
        TR_ConsiderLeadTankSpawn(search, pos);
    }
    else
    {
        // All six orders have equal probability. Empty bands fall through to the next available band.
        int firstBand = GetRandomInt(0, TR_LEAD_DISTANCE_BANDS - 1);
        int secondBand = (firstBand + GetRandomInt(1, TR_LEAD_DISTANCE_BANDS - 1)) % TR_LEAD_DISTANCE_BANDS;
        search.bandPriority[firstBand] = 0;
        search.bandPriority[secondBand] = 1;
        search.bandPriority[3 - firstBand - secondBand] = 2;
        if (GetFeatureStatus(FeatureType_Native, "L4D_GetRandomPZSpawnPosition") == FeatureStatus_Available)
        {
            int attempts = g_cvSpawnAttempts.IntValue;
            if (attempts < 1) attempts = 1;
            if (attempts > 8) attempts = 8;
            for (int attempt = 0; attempt < attempts; attempt++)
            {
                float candidate[3];
                if (L4D_GetRandomPZSpawnPosition(search.client, 8, 4, candidate)) TR_ConsiderLeadTankSpawn(search, candidate);
            }
        }
        TR_SampleLeadGround(search);
        TR_SampleLeadNav(search);
    }
    if (!search.found)
    {
        Format(reason, reasonLen, "领先玩家#%d 流程 %.0f，%.0f–%.0f 单位内暂无%s", search.client, search.flow, search.minDistance, search.maxDistance,
            search.requireCover ? "对所有生还者有遮挡且可追击的前方刷点" : "可追击的前方刷点");
        return false;
    }

    pos = search.position;
    float toward[3];
    MakeVectorFromPoints(pos, search.origin, toward);
    GetVectorAngles(toward, ang);
    ang[0] = 0.0;
    ang[2] = 0.0;
    static const char bands[][] = {"近", "中", "远"};
    Format(reason, reasonLen, "领先玩家#%d 流程 %.0f，出生流程 %.0f，距离 %.0f（%s段，范围 %.0f–%.0f，%s）",
        search.client, search.flow, search.candidateFlow, GetVectorDistance(pos, search.origin),
        bands[search.selectedBand], search.minDistance, search.maxDistance, search.requireCover ? "普通遮挡" : "高难度前线");
    return true;
}
