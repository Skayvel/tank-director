// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

// Tank Run v2.3 规则与机制层：
//   1. 取消友伤（tankrun_no_friendly_fire，默认 1，仅 Tank Run 合作）
//      严格实体实例级归因（按 EntRef 绑定）+ 精确 C++ 同步引爆调用栈限定（Pre 压栈 / Post 立即弹栈），
//      彻底移除跨实体时间窗口（1 tick）与距离（32u）猜测逻辑，绝不吞掉邻近独立无主地图火。
//   2. 取消水中减速（tankrun_no_water_slowdown，默认 1，仅 Tank Run 合作，原版 WaterSlowsMovement）
//   3. 死门模式（tankrun_deaths_door，默认 0，热切换倒地即死 + 双端校验 + 失败回滚）

#if defined _tankrun_v23_rules_included
 #endinput
#endif
#define _tankrun_v23_rules_included

#define TR_MAX_TRACKED_ENTS 2048
#define TR_FF_DET_STACK_MAX 8
#define TR_FF_FUEL_BARREL_ATTACKER_OFF 0x16A4

// ---------------------------------------------------------------- 友伤归因状态（严格实体实例 EntRef 级 + 同步调用栈）
int g_iFFSourceRef[TR_MAX_TRACKED_ENTS];
int g_iFFSourceUid[TR_MAX_TRACKED_ENTS];
int g_iFFSourceTeam[TR_MAX_TRACKED_ENTS];

// 同步引爆 C++ 调用栈（仅在 Detonate / Event_Killed / Explode / SpawnFire Pre..Post 同步执行期间非空，Post 立即弹栈）
int g_iFFDetStackDepth;
int g_iFFDetStackUid[TR_FF_DET_STACK_MAX];
int g_iFFDetStackTeam[TR_FF_DET_STACK_MAX];

// CFuelBarrelProp::Explode 与 CFuelBarrelPiece::SpawnFire 只读 Pre/Post detour
// （将油桶本体爆炸及碎片落地创建的 owner=0 inferno 严格限定于该油桶的同步调用栈）
DynamicDetour g_hFFFuelBarrelExplodeDetour;
DynamicDetour g_hFFFuelBarrelSpawnFireDetour;
bool g_bFFFuelBarrelDetourReady;
bool g_bFFPendingOsRetry;
int  g_iFFFuelBarrelAttackerOff = -1;

int g_iStatFFBlockedDirect;
int g_iStatFFBlockedDelayed;

// ---------------------------------------------------------------- 水中减速状态（原版 WaterSlowsMovement）
bool  g_bWaterApplied;
bool  g_bWaterSavedOriginal;
bool  g_bWaterOrigHasSO;
int   g_iWaterOrigSO = 1;
bool  g_bWaterOrigHasDO;
int   g_iWaterOrigDO = 1;
char  g_sWaterStatus[128] = "尚未应用";

// ---------------------------------------------------------------- 死门模式状态
bool  g_bDeathsDoorActive;
bool  g_bDeathsDoorSyncing;
bool  g_bDeathsDoorHasOrigCvar;
int   g_iDeathsDoorOrigCvar = 2;
bool  g_bDeathsDoorHasOrigScript;
bool  g_bDeathsDoorOrigHasSO;
int   g_iDeathsDoorOrigSO = 2;
bool  g_bDeathsDoorOrigHasDO;
int   g_iDeathsDoorOrigDO = 2;
char  g_sDeathsDoorLastResult[160] = "默认关闭";

// ---------------------------------------------------------------- 基础作用域
bool TR_IsTankRunCoop()
{
    return TR_FeatureEnabled() && TR_IsTankRunMode() && TR_FlowIsCoopMode();
}

bool TR_NoFriendlyFireActive()
{
    return TR_IsTankRunCoop() && g_cvNoFriendlyFire != null && g_cvNoFriendlyFire.BoolValue;
}

bool TR_NoWaterSlowdownActive()
{
    return TR_IsTankRunCoop() && g_cvNoWaterSlowdown != null && g_cvNoWaterSlowdown.BoolValue;
}

bool TR_DeathsDoorShouldBeActive()
{
    return TR_FeatureEnabled() && g_cvDeathsDoor != null && g_cvDeathsDoor.BoolValue;
}

// ---------------------------------------------------------------- 1. 取消友伤（No Friendly Fire）
bool TR_FF_IsExplosivePropClass(const char[] classname)
{
    return StrEqual(classname, "prop_physics", false)
        || StrEqual(classname, "prop_physics_override", false)
        || StrEqual(classname, "prop_fuel_barrel", false)
        || StrEqual(classname, "prop_fuel_barrel_piece", false)
        || StrEqual(classname, "weapon_gascan", false)
        || StrEqual(classname, "weapon_propanetank", false)
        || StrEqual(classname, "weapon_oxygentank", false)
        || StrEqual(classname, "weapon_fireworkcrate", false);
}

bool TR_FF_IsDelayedHazardClass(const char[] classname)
{
    return StrEqual(classname, "inferno", false)
        || StrEqual(classname, "fire_cracker_blast", false)
        || StrEqual(classname, "molotov_projectile", false)
        || StrEqual(classname, "pipe_bomb_projectile", false)
        || StrEqual(classname, "grenade_launcher_projectile", false)
        || StrEqual(classname, "entityflame", false)
        || StrEqual(classname, "env_explosion", false);
}

void TR_FF_ClearEntity(int entity)
{
    if (entity <= 0 || entity >= TR_MAX_TRACKED_ENTS)
    {
        return;
    }
    g_iFFSourceRef[entity] = INVALID_ENT_REFERENCE;
    g_iFFSourceUid[entity] = 0;
    g_iFFSourceTeam[entity] = 0;
}

void TR_FF_ResetAll()
{
    for (int i = 0; i < TR_MAX_TRACKED_ENTS; i++)
    {
        g_iFFSourceRef[i] = INVALID_ENT_REFERENCE;
        g_iFFSourceUid[i] = 0;
        g_iFFSourceTeam[i] = 0;
    }
    g_iFFDetStackDepth = 0;
}

void TR_FF_RememberEntity(int entity, int uid, int team)
{
    if (entity <= MaxClients || entity >= TR_MAX_TRACKED_ENTS || !IsValidEntity(entity) || uid <= 0 || team <= 0)
    {
        return;
    }
    g_iFFSourceRef[entity] = EntIndexToEntRef(entity);
    g_iFFSourceUid[entity] = uid;
    g_iFFSourceTeam[entity] = team;
}

bool TR_FF_ReadClientSource(int client, int &uid, int &team)
{
    if (client >= 1 && client <= MaxClients && IsClientInGame(client))
    {
        uid = GetClientUserId(client);
        team = GetClientTeam(client);
        return uid > 0 && team > 0;
    }
    return false;
}

bool TR_FF_ReadStoredSource(int entity, int &uid, int &team)
{
    if (entity <= MaxClients || entity >= TR_MAX_TRACKED_ENTS || !IsValidEntity(entity))
    {
        return false;
    }
    if (g_iFFSourceRef[entity] != INVALID_ENT_REFERENCE
        && g_iFFSourceRef[entity] == EntIndexToEntRef(entity)
        && g_iFFSourceUid[entity] > 0 && g_iFFSourceTeam[entity] > 0)
    {
        uid = g_iFFSourceUid[entity];
        team = g_iFFSourceTeam[entity];
        return true;
    }
    return false;
}

void TR_FF_PushDetonationContext(int uid, int team)
{
    if (g_iFFDetStackDepth < TR_FF_DET_STACK_MAX)
    {
        g_iFFDetStackUid[g_iFFDetStackDepth] = uid;
        g_iFFDetStackTeam[g_iFFDetStackDepth] = team;
    }
    g_iFFDetStackDepth++;
}

void TR_FF_PopDetonationContext()
{
    if (g_iFFDetStackDepth > 0)
    {
        g_iFFDetStackDepth--;
        if (g_iFFDetStackDepth < TR_FF_DET_STACK_MAX)
        {
            g_iFFDetStackUid[g_iFFDetStackDepth] = 0;
            g_iFFDetStackTeam[g_iFFDetStackDepth] = 0;
        }
    }
}

bool TR_FF_PeekDetonationContext(int &uid, int &team)
{
    if (g_iFFDetStackDepth <= 0)
    {
        return false;
    }
    int top = g_iFFDetStackDepth - 1;
    if (top >= TR_FF_DET_STACK_MAX)
    {
        top = TR_FF_DET_STACK_MAX - 1;
    }
    uid = g_iFFDetStackUid[top];
    team = g_iFFDetStackTeam[top];
    return uid > 0 && team > 0;
}

void TR_FF_HookExistingProps()
{
    if (!TR_IsMapRunning())
    {
        return;
    }
    static const char classes[][] = {
        "prop_physics",
        "prop_physics_override",
        "prop_fuel_barrel",
        "weapon_gascan",
        "weapon_propanetank",
        "weapon_oxygentank",
        "weapon_fireworkcrate"
    };
    for (int i = 0; i < sizeof(classes); i++)
    {
        int ent = MaxClients;
        while ((ent = FindEntityByClassname(ent, classes[i])) != -1)
        {
            if (ent > MaxClients && ent < TR_MAX_TRACKED_ENTS && IsValidEntity(ent))
            {
                SDKHook(ent, SDKHook_OnTakeDamagePost, TR_FF_OnPropDamagedPost);
            }
        }
    }
}

bool TR_FF_ResolveEntityOwner(int entity, int &uid, int &team)
{
    uid = 0;
    team = 0;
    if (entity <= 0)
    {
        return false;
    }
    if (entity <= MaxClients)
    {
        return TR_FF_ReadClientSource(entity, uid, team);
    }
    if (entity >= TR_MAX_TRACKED_ENTS || !IsValidEntity(entity))
    {
        return false;
    }

    // 1) 优先读取本实体实例精确记录的归属（严格按 EntRef 校验，防实体索引复用）
    if (TR_FF_ReadStoredSource(entity, uid, team))
    {
        return true;
    }

    // 2) 读取引擎实体自身的所有者/投掷者句柄（m_hOwnerEntity / m_hThrower）
    int owner = -1;
    if (HasEntProp(entity, Prop_Send, "m_hOwnerEntity"))
    {
        owner = GetEntPropEnt(entity, Prop_Send, "m_hOwnerEntity");
    }
    else if (HasEntProp(entity, Prop_Data, "m_hOwnerEntity"))
    {
        owner = GetEntPropEnt(entity, Prop_Data, "m_hOwnerEntity");
    }
    if ((owner <= 0 || owner > MaxClients) && HasEntProp(entity, Prop_Send, "m_hThrower"))
    {
        int thrower = GetEntPropEnt(entity, Prop_Send, "m_hThrower");
        if (thrower > 0)
        {
            owner = thrower;
        }
    }

    if (TR_FF_ReadClientSource(owner, uid, team))
    {
        return true;
    }
    if (owner > MaxClients && owner != entity && TR_FF_ReadStoredSource(owner, uid, team))
    {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- 油桶 Explode / 碎片 SpawnFire 同步调用栈 Detour
void TR_FF_UninstallFuelBarrelDetour()
{
    g_bFFFuelBarrelDetourReady = false;
    g_iFFFuelBarrelAttackerOff = -1;
    if (g_hFFFuelBarrelExplodeDetour != null)
    {
        g_hFFFuelBarrelExplodeDetour.Disable(Hook_Pre, TR_FF_DetourFuelBarrelExplodePre);
        g_hFFFuelBarrelExplodeDetour.Disable(Hook_Post, TR_FF_DetourFuelBarrelExplodePost);
        delete g_hFFFuelBarrelExplodeDetour;
        g_hFFFuelBarrelExplodeDetour = null;
    }
    if (g_hFFFuelBarrelSpawnFireDetour != null)
    {
        g_hFFFuelBarrelSpawnFireDetour.Disable(Hook_Pre, TR_FF_DetourFuelBarrelSpawnFirePre);
        g_hFFFuelBarrelSpawnFireDetour.Disable(Hook_Post, TR_FF_DetourFuelBarrelSpawnFirePost);
        delete g_hFFFuelBarrelSpawnFireDetour;
        g_hFFFuelBarrelSpawnFireDetour = null;
    }
}

void TR_FF_InstallFuelBarrelDetour()
{
    TR_FF_UninstallFuelBarrelDetour();
    g_bFFPendingOsRetry = false;

    if (GetFeatureStatus(FeatureType_Native, "L4D_GetServerOS") != FeatureStatus_Available)
    {
        g_bFFPendingOsRetry = true;
        return;
    }
    if (L4D_GetServerOS() != 0)
    {
        return;
    }
    if (GetFeatureStatus(FeatureType_Native, "DynamicDetour.DynamicDetour") != FeatureStatus_Available)
    {
        return;
    }

    char gdPath[PLATFORM_MAX_PATH];
    BuildPath(Path_SM, gdPath, sizeof(gdPath), "gamedata/tankrun_rocktarget.txt");
    if (!FileExists(gdPath))
    {
        return;
    }
    GameData gd = LoadGameConfigFile("tankrun_rocktarget");
    if (gd == null)
    {
        return;
    }

    Address fnExplode = gd.GetMemSig("TankRun::CFuelBarrelProp::Explode");
    Address fnSpawnFire = gd.GetMemSig("TankRun::CFuelBarrelPiece::SpawnFire");
    int attackerOff = gd.GetOffset("CFuelBarrelProp::m_hAttacker");
    delete gd;

    if (fnExplode == Address_Null || fnSpawnFire == Address_Null || attackerOff != TR_FF_FUEL_BARREL_ATTACKER_OFF)
    {
        return;
    }

    static const int sigExpEntry[] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x18, 0x01, 0x00, 0x00 };
    static const int sigExpAtt[] = { 0x8B, 0xB6, 0xA4, 0x16, 0x00, 0x00 };
    static const int sigPieceEntry[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x53, 0x56, 0x8B, 0xF1 };
    static const int sigPieceCreate[] = { 0x6A, 0x00, 0x8D, 0x96, 0x68, 0x03, 0x00, 0x00, 0x52 };
    int badOff = 0, expB = 0, actB = 0;
    if (!TR_Glow_VerifyCodeBytes(fnExplode, 0x00, sigExpEntry, sizeof(sigExpEntry), badOff, expB, actB)
        || !TR_Glow_VerifyCodeBytes(fnExplode, 0x7CA, sigExpAtt, sizeof(sigExpAtt), badOff, expB, actB)
        || !TR_Glow_VerifyCodeBytes(fnSpawnFire, 0x00, sigPieceEntry, sizeof(sigPieceEntry), badOff, expB, actB)
        || !TR_Glow_VerifyCodeBytes(fnSpawnFire, 0x32, sigPieceCreate, sizeof(sigPieceCreate), badOff, expB, actB))
    {
        TR_Log("友伤油桶 Explode/SpawnFire 字节校验不符，降级跳过 (+0x%02X exp=0x%02X act=0x%02X)", badOff, expB, actB);
        return;
    }

    g_hFFFuelBarrelExplodeDetour = new DynamicDetour(fnExplode, CallConv_THISCALL, ReturnType_Void, ThisPointer_CBaseEntity);
    g_hFFFuelBarrelSpawnFireDetour = new DynamicDetour(fnSpawnFire, CallConv_THISCALL, ReturnType_Void, ThisPointer_CBaseEntity);
    if (g_hFFFuelBarrelExplodeDetour == null || g_hFFFuelBarrelSpawnFireDetour == null)
    {
        TR_FF_UninstallFuelBarrelDetour();
        return;
    }
    if (!g_hFFFuelBarrelExplodeDetour.Enable(Hook_Pre, TR_FF_DetourFuelBarrelExplodePre)
        || !g_hFFFuelBarrelExplodeDetour.Enable(Hook_Post, TR_FF_DetourFuelBarrelExplodePost)
        || !g_hFFFuelBarrelSpawnFireDetour.Enable(Hook_Pre, TR_FF_DetourFuelBarrelSpawnFirePre)
        || !g_hFFFuelBarrelSpawnFireDetour.Enable(Hook_Post, TR_FF_DetourFuelBarrelSpawnFirePost))
    {
        TR_FF_UninstallFuelBarrelDetour();
        return;
    }

    g_iFFFuelBarrelAttackerOff = attackerOff;
    g_bFFFuelBarrelDetourReady = true;
    TR_Log("友伤油桶调用栈归因已启用（CFuelBarrelProp::Explode + CFuelBarrelPiece::SpawnFire Pre/Post）");
}

public MRESReturn TR_FF_DetourFuelBarrelExplodePre(int barrel)
{
    int uid = 0, team = 0;
    if (barrel > MaxClients && barrel < TR_MAX_TRACKED_ENTS && IsValidEntity(barrel))
    {
        bool resolvedFromEngine = false;
        if (g_bFFFuelBarrelDetourReady && g_iFFFuelBarrelAttackerOff == TR_FF_FUEL_BARREL_ATTACKER_OFF)
        {
            Address addr = GetEntityAddress(barrel);
            if (addr != Address_Null)
            {
                int rawHandle = LoadFromAddress(addr + view_as<Address>(g_iFFFuelBarrelAttackerOff), NumberType_Int32);
                int entIdx = -1, serial = 0;
                int att = TR_Glow_DecodePlayerHandle(rawHandle, entIdx, serial);
                if (TR_FF_ReadClientSource(att, uid, team))
                {
                    TR_FF_RememberEntity(barrel, uid, team);
                    resolvedFromEngine = true;
                }
            }
        }
        if (!resolvedFromEngine)
        {
            TR_FF_ResolveEntityOwner(barrel, uid, team);
        }
    }
    TR_FF_PushDetonationContext(uid, team);
    return MRES_Ignored;
}

public MRESReturn TR_FF_DetourFuelBarrelExplodePost(int barrel)
{
    #pragma unused barrel
    TR_FF_PopDetonationContext();
    return MRES_Ignored;
}

public MRESReturn TR_FF_DetourFuelBarrelSpawnFirePre(int piece)
{
    int uid = 0, team = 0;
    if (piece > MaxClients && piece < TR_MAX_TRACKED_ENTS && IsValidEntity(piece))
    {
        TR_FF_ResolveEntityOwner(piece, uid, team);
    }
    TR_FF_PushDetonationContext(uid, team);
    return MRES_Ignored;
}

public MRESReturn TR_FF_DetourFuelBarrelSpawnFirePost(int piece)
{
    #pragma unused piece
    TR_FF_PopDetonationContext();
    return MRES_Ignored;
}

// ---------------------------------------------------------------- 投掷物、油桶与道具实例级归因钩子
public void TR_FF_OnPropDamagedPost(int entity, int attacker, int inflictor, float damage,
    int damagetype, int weapon, const float damageForce[3], const float damagePosition[3])
{
    #pragma unused damage, damagetype, weapon, damageForce, damagePosition
    if (entity <= MaxClients || entity >= TR_MAX_TRACKED_ENTS || !IsValidEntity(entity))
    {
        return;
    }

    int uid = 0, team = 0;
    if (!TR_FF_ReadClientSource(attacker, uid, team))
    {
        if (!TR_FF_ResolveEntityOwner(inflictor, uid, team) && attacker == 0)
        {
            // 仅在同步引爆调用栈内的连锁引爆时继承上游归属，绝不跨实体按时间/距离猜测
            TR_FF_PeekDetonationContext(uid, team);
        }
    }
    if (uid > 0 && team > 0)
    {
        TR_FF_RememberEntity(entity, uid, team);
    }
}

public void TR_FF_OnBreakPropPre(Event event, const char[] name, bool dontBroadcast)
{
    #pragma unused name, dontBroadcast
    int entity = event.GetInt("entindex");
    if (entity <= MaxClients || entity >= TR_MAX_TRACKED_ENTS || !IsValidEntity(entity))
    {
        return;
    }
    char classname[40];
    GetEntityClassname(entity, classname, sizeof(classname));
    if (!TR_FF_IsExplosivePropClass(classname))
    {
        return;
    }
    int uid = 0, team = 0;
    if (TR_FF_ReadClientSource(GetClientOfUserId(event.GetInt("userid")), uid, team))
    {
        TR_FF_RememberEntity(entity, uid, team);
    }
}

public void L4D_MolotovProjectile_Post(int client, int projectile, const float vecPos[3],
    const float vecAng[3], const float vecVel[3], const float vecRot[3])
{
    #pragma unused vecPos, vecAng, vecVel, vecRot
    int uid = 0, team = 0;
    if (TR_FF_ReadClientSource(client, uid, team))
    {
        TR_FF_RememberEntity(projectile, uid, team);
    }
}

public void L4D_PipeBombProjectile_Post(int client, int projectile, const float vecPos[3],
    const float vecAng[3], const float vecVel[3], const float vecRot[3])
{
    #pragma unused vecPos, vecAng, vecVel, vecRot
    int uid = 0, team = 0;
    if (TR_FF_ReadClientSource(client, uid, team))
    {
        TR_FF_RememberEntity(projectile, uid, team);
    }
}

public void L4D2_GrenadeLauncherProjectile_Post(int client, int projectile, const float vecPos[3],
    const float vecAng[3], const float vecVel[3], const float vecRot[3], bool bIncendiary)
{
    #pragma unused vecPos, vecAng, vecVel, vecRot, bIncendiary
    int uid = 0, team = 0;
    if (TR_FF_ReadClientSource(client, uid, team))
    {
        TR_FF_RememberEntity(projectile, uid, team);
    }
}

public Action L4D_Molotov_Detonate(int entity, int client)
{
    int uid = 0, team = 0;
    if (!TR_FF_ReadClientSource(client, uid, team))
    {
        TR_FF_ResolveEntityOwner(entity, uid, team);
    }
    TR_FF_PushDetonationContext(uid, team);
    return Plugin_Continue;
}

public void L4D_Molotov_Detonate_Post(int entity, int client)
{
    #pragma unused entity, client
    TR_FF_PopDetonationContext();
}

public void L4D_Molotov_Detonate_PostHandled(int entity, int client)
{
    #pragma unused entity, client
    TR_FF_PopDetonationContext();
}

public Action L4D_PipeBomb_Detonate(int entity, int client)
{
    int uid = 0, team = 0;
    if (!TR_FF_ReadClientSource(client, uid, team))
    {
        TR_FF_ResolveEntityOwner(entity, uid, team);
    }
    TR_FF_PushDetonationContext(uid, team);
    return Plugin_Continue;
}

public void L4D_PipeBomb_Detonate_Post(int entity, int client)
{
    #pragma unused entity, client
    TR_FF_PopDetonationContext();
}

public void L4D_PipeBomb_Detonate_PostHandled(int entity, int client)
{
    #pragma unused entity, client
    TR_FF_PopDetonationContext();
}

public Action L4D2_GrenadeLauncher_Detonate(int entity, int client)
{
    int uid = 0, team = 0;
    if (!TR_FF_ReadClientSource(client, uid, team))
    {
        TR_FF_ResolveEntityOwner(entity, uid, team);
    }
    TR_FF_PushDetonationContext(uid, team);
    return Plugin_Continue;
}

public void L4D2_GrenadeLauncher_Detonate_Post(int entity, int client)
{
    #pragma unused entity, client
    TR_FF_PopDetonationContext();
}

public void L4D2_GrenadeLauncher_Detonate_PostHandled(int entity, int client)
{
    #pragma unused entity, client
    TR_FF_PopDetonationContext();
}

public Action L4D2_CGasCan_EventKilled(int gascan, int &inflictor, int &attacker)
{
    int uid = 0, team = 0;
    if (!TR_FF_ReadClientSource(attacker, uid, team)
        && !TR_FF_ResolveEntityOwner(inflictor, uid, team))
    {
        TR_FF_ResolveEntityOwner(gascan, uid, team);
    }
    if (uid > 0 && team > 0)
    {
        TR_FF_RememberEntity(gascan, uid, team);
    }
    TR_FF_PushDetonationContext(uid, team);
    return Plugin_Continue;
}

public void L4D2_CGasCan_EventKilled_Post(int gascan, int inflictor, int attacker)
{
    #pragma unused gascan, inflictor, attacker
    TR_FF_PopDetonationContext();
}

public void L4D2_CGasCan_EventKilled_PostHandled(int gascan, int inflictor, int attacker)
{
    #pragma unused gascan, inflictor, attacker
    TR_FF_PopDetonationContext();
}

public void TR_FF_OnHazardSpawnPost(int entity)
{
    if (entity <= MaxClients || entity >= TR_MAX_TRACKED_ENTS || !IsValidEntity(entity))
    {
        return;
    }

    int uid = 0, team = 0;
    if (TR_FF_ResolveEntityOwner(entity, uid, team))
    {
        TR_FF_RememberEntity(entity, uid, team);
    }
}

void TR_FF_OnEntityCreated(int entity, const char[] classname)
{
    if (entity <= MaxClients || entity >= TR_MAX_TRACKED_ENTS)
    {
        return;
    }
    TR_FF_ClearEntity(entity);

    if (TR_FF_IsExplosivePropClass(classname))
    {
        // 若为油桶爆炸同步调用栈内飞出的 prop_fuel_barrel_piece 碎片，精确绑定母油桶归属
        if (StrEqual(classname, "prop_fuel_barrel_piece", false))
        {
            int stackUid = 0, stackTeam = 0;
            if (TR_FF_PeekDetonationContext(stackUid, stackTeam))
            {
                TR_FF_RememberEntity(entity, stackUid, stackTeam);
            }
        }
        SDKHook(entity, SDKHook_OnTakeDamagePost, TR_FF_OnPropDamagedPost);
    }
    else if (TR_FF_IsDelayedHazardClass(classname))
    {
        // 仅当该危害实体正处于已验证引爆的同步 C++ 调用栈内创建时，直接绑定本实体实例 EntRef；
        // 离开调用栈后创建的任何同 tick 邻近无主地图火绝不继承归属。
        int stackUid = 0, stackTeam = 0;
        if (TR_FF_PeekDetonationContext(stackUid, stackTeam))
        {
            TR_FF_RememberEntity(entity, stackUid, stackTeam);
        }
        SDKHook(entity, SDKHook_SpawnPost, TR_FF_OnHazardSpawnPost);
    }
}

void TR_FF_OnEntityDestroyed(int entity)
{
    TR_FF_ClearEntity(entity);
}

// 返回 true 表示确认属于同队其他生还者友伤（应拦截）；自身/感染者/真实环境/独立无主火一律返回 false。
bool TR_FF_ShouldBlockDamage(int victim, int attacker, int inflictor, int damagetype, int weapon)
{
    #pragma unused weapon
    if (!TR_NoFriendlyFireActive())
    {
        return false;
    }
    if (victim < 1 || victim > MaxClients || !IsClientInGame(victim) || GetClientTeam(victim) != 2)
    {
        return false;
    }

    // 保留坠落等真实环境伤害与自身伤害
    if ((damagetype & DMG_FALL) != 0 || attacker == victim)
    {
        return false;
    }

    // 明确来自感染者的伤害一律保留
    if (attacker >= 1 && attacker <= MaxClients && IsClientInGame(attacker) && GetClientTeam(attacker) == 3)
    {
        return false;
    }

    int victimUid = GetClientUserId(victim);

    // 1) 直接来自其他同队生还者（真人或 Bot）
    if (attacker >= 1 && attacker <= MaxClients && attacker != victim
        && IsClientInGame(attacker) && GetClientTeam(attacker) == 2)
    {
        if (inflictor > MaxClients && IsValidEntity(inflictor))
        {
            char infCls[32];
            GetEntityClassname(inflictor, infCls, sizeof(infCls));
            if (TR_FF_IsDelayedHazardClass(infCls) || TR_FF_IsExplosivePropClass(infCls))
            {
                g_iStatFFBlockedDelayed++;
                return true;
            }
        }
        g_iStatFFBlockedDirect++;
        return true;
    }

    // 2) 严格实体实例级可追溯的延迟燃烧/爆炸实体（燃烧瓶/油桶/煤气罐/土制炸弹/榴弹等）
    if (inflictor > MaxClients && IsValidEntity(inflictor))
    {
        char infCls[32];
        GetEntityClassname(inflictor, infCls, sizeof(infCls));
        // 明确豁免感染者实体与地图环境伤害实体
        if (StrEqual(infCls, "insect_swarm", false) || StrEqual(infCls, "tank_rock", false)
            || StrEqual(infCls, "trigger_hurt", false) || StrEqual(infCls, "worldspawn", false))
        {
            return false;
        }

        int srcUid = 0, srcTeam = 0;
        if (TR_FF_ResolveEntityOwner(inflictor, srcUid, srcTeam))
        {
            // 仅当明确归因到另一名同队生还者时拦截；自身造成的延迟燃烧/爆炸保留！
            if (srcTeam == 2 && srcUid > 0 && srcUid != victimUid)
            {
                g_iStatFFBlockedDelayed++;
                return true;
            }
            // 已明确归因为自身或非生还者：绝不降级继续猜测
            return false;
        }
    }

    // 3) 若 attacker 本身是已追踪爆炸物实体
    if (attacker > MaxClients && attacker < TR_MAX_TRACKED_ENTS && IsValidEntity(attacker))
    {
        int srcUid = 0, srcTeam = 0;
        if (TR_FF_ResolveEntityOwner(attacker, srcUid, srcTeam))
        {
            if (srcTeam == 2 && srcUid > 0 && srcUid != victimUid)
            {
                g_iStatFFBlockedDelayed++;
                return true;
            }
            return false;
        }
    }

    // 4) 同步引爆调用栈内的世界归因直接爆炸伤害（例如 Detonate/Explode 内部同步发出的无主半径冲击波）
    if (attacker == 0)
    {
        int stackUid = 0, stackTeam = 0;
        if (TR_FF_PeekDetonationContext(stackUid, stackTeam) && stackTeam == 2 && stackUid != victimUid)
        {
            g_iStatFFBlockedDelayed++;
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------- 2. 取消水中减速（原版 WaterSlowsMovement）
bool TR_Water_ReadScriptState(bool &hasSO, int &valSO, bool &hasDO, int &valDO)
{
    hasSO = false;
    valSO = 1;
    hasDO = false;
    valDO = 1;
    if (!TR_IsMapRunning())
    {
        return false;
    }

    char code[768], res[64];
    Format(code, sizeof(code),
        "local r = getroottable(); local hSO = (\"SessionOptions\" in r) && (\"WaterSlowsMovement\" in r.SessionOptions); local vSO = hSO ? r.SessionOptions.WaterSlowsMovement.tointeger() : 1; local hDO = (\"DirectorOptions\" in r) && (\"WaterSlowsMovement\" in r.DirectorOptions); local vDO = hDO ? r.DirectorOptions.WaterSlowsMovement.tointeger() : 1; Convars.SetValue(\"%s\", format(\"W|%%d|%%d|%%d|%%d\", hSO ? 1 : 0, vSO, hDO ? 1 : 0, vDO));",
        TR_BRIDGE);
    if (!TR_RunVScript(code, res, sizeof(res)))
    {
        return false;
    }

    char parts[5][16];
    if (ExplodeString(res, "|", parts, sizeof(parts), sizeof(parts[])) != 5 || !StrEqual(parts[0], "W"))
    {
        return false;
    }
    hasSO = StringToInt(parts[1]) != 0;
    valSO = StringToInt(parts[2]);
    hasDO = StringToInt(parts[3]) != 0;
    valDO = StringToInt(parts[4]);
    return true;
}

bool TR_Water_WriteScriptState(bool setSO, int valSO, bool setDO, int valDO)
{
    if (!TR_IsMapRunning())
    {
        return false;
    }

    char stmtSO[160], stmtDO[160], code[768], res[32];
    if (setSO)
    {
        Format(stmtSO, sizeof(stmtSO), "if (\"SessionOptions\" in r) r.SessionOptions.WaterSlowsMovement <- %d;", valSO);
    }
    else
    {
        strcopy(stmtSO, sizeof(stmtSO), "if ((\"SessionOptions\" in r) && (\"WaterSlowsMovement\" in r.SessionOptions)) delete r.SessionOptions.WaterSlowsMovement;");
    }

    if (setDO)
    {
        Format(stmtDO, sizeof(stmtDO), "if (\"DirectorOptions\" in r) r.DirectorOptions.WaterSlowsMovement <- %d;", valDO);
    }
    else
    {
        strcopy(stmtDO, sizeof(stmtDO), "if ((\"DirectorOptions\" in r) && (\"WaterSlowsMovement\" in r.DirectorOptions)) delete r.DirectorOptions.WaterSlowsMovement;");
    }

    Format(code, sizeof(code), "local r = getroottable(); %s %s Convars.SetValue(\"%s\", \"WOK\");", stmtSO, stmtDO, TR_BRIDGE);
    return TR_RunVScript(code, res, sizeof(res)) && StrEqual(res, "WOK");
}

void TR_Water_Sync(const char[] reason)
{
    bool want = TR_NoWaterSlowdownActive();
    if (want)
    {
        if (!TR_IsMapRunning())
        {
            strcopy(g_sWaterStatus, sizeof(g_sWaterStatus), "等待地图就绪");
            return;
        }
        if (!g_bWaterSavedOriginal)
        {
            if (TR_Water_ReadScriptState(g_bWaterOrigHasSO, g_iWaterOrigSO, g_bWaterOrigHasDO, g_iWaterOrigDO))
            {
                g_bWaterSavedOriginal = true;
            }
        }
        bool wrote = TR_Water_WriteScriptState(true, 0, true, 0);
        bool hSO, hDO;
        int vSO, vDO;
        bool readOk = TR_Water_ReadScriptState(hSO, vSO, hDO, vDO);
        int engineVal = -1;
        if (GetFeatureStatus(FeatureType_Native, "L4D2_GetScriptValueInt") == FeatureStatus_Available)
        {
            engineVal = L4D2_GetScriptValueInt("WaterSlowsMovement", -1);
        }
        g_bWaterApplied = true;
        if (wrote && readOk && hSO && vSO == 0)
        {
            Format(g_sWaterStatus, sizeof(g_sWaterStatus), "已生效（WaterSlowsMovement=0，引擎读回=%d）", engineVal);
            TR_Log("取消水中减速已应用（%s）：SessionOptions.WaterSlowsMovement=0 DirectorOptions=%d/%d 引擎读回=%d",
                reason, hDO ? 1 : 0, vDO, engineVal);
        }
        else
        {
            Format(g_sWaterStatus, sizeof(g_sWaterStatus), "脚本直写未确认（依赖 GetScriptValueInt 钩子，引擎读回=%d）", engineVal);
            TR_Log("取消水中减速（%s）：VScript 写回未完全确认（wrote=%d readOk=%d），已启用 GetScriptValueInt 钩子",
                reason, wrote ? 1 : 0, readOk ? 1 : 0);
        }
        return;
    }

    if (g_bWaterApplied || g_bWaterSavedOriginal)
    {
        g_bWaterApplied = false;
        if (TR_IsMapRunning() && g_bWaterSavedOriginal)
        {
            bool restored = TR_Water_WriteScriptState(g_bWaterOrigHasSO, g_iWaterOrigSO, g_bWaterOrigHasDO, g_iWaterOrigDO);
            int engineVal = -1;
            if (GetFeatureStatus(FeatureType_Native, "L4D2_GetScriptValueInt") == FeatureStatus_Available)
            {
                engineVal = L4D2_GetScriptValueInt("WaterSlowsMovement", 1);
            }
            Format(g_sWaterStatus, sizeof(g_sWaterStatus), "已恢复原值（%s，引擎读回=%d）", restored ? "成功" : "部分", engineVal);
            TR_Log("取消水中减速已还原（%s）：恢复原值=%d 引擎读回=%d", reason, restored ? 1 : 0, engineVal);
        }
        else
        {
            strcopy(g_sWaterStatus, sizeof(g_sWaterStatus), "未启用（原版减速）");
        }
    }
    else
    {
        strcopy(g_sWaterStatus, sizeof(g_sWaterStatus), "未启用（原版减速）");
    }
}

void TR_Water_OnMapReset()
{
    g_bWaterApplied = false;
    g_bWaterSavedOriginal = false;
    g_bWaterOrigHasSO = false;
    g_iWaterOrigSO = 1;
    g_bWaterOrigHasDO = false;
    g_iWaterOrigDO = 1;
}

// ---------------------------------------------------------------- 3. 死门模式（Death's Door）
bool TR_DeathsDoor_ReadScriptState(bool &hasSO, int &valSO, bool &hasDO, int &valDO)
{
    hasSO = false;
    valSO = -1;
    hasDO = false;
    valDO = -1;
    if (!TR_IsMapRunning())
    {
        return false;
    }

    char code[896], res[64];
    Format(code, sizeof(code),
        "local r = getroottable(); local hSO = (\"SessionOptions\" in r) && (\"SurvivorMaxIncapacitatedCount\" in r.SessionOptions); local vSO = hSO ? r.SessionOptions.SurvivorMaxIncapacitatedCount.tointeger() : -1; local hDO = (\"DirectorOptions\" in r) && (\"SurvivorMaxIncapacitatedCount\" in r.DirectorOptions); local vDO = hDO ? r.DirectorOptions.SurvivorMaxIncapacitatedCount.tointeger() : -1; Convars.SetValue(\"%s\", format(\"D|%%d|%%d|%%d|%%d\", hSO ? 1 : 0, vSO, hDO ? 1 : 0, vDO));",
        TR_BRIDGE);
    if (!TR_RunVScript(code, res, sizeof(res)))
    {
        return false;
    }
    char parts[5][16];
    if (ExplodeString(res, "|", parts, sizeof(parts), sizeof(parts[])) != 5 || !StrEqual(parts[0], "D"))
    {
        return false;
    }
    hasSO = StringToInt(parts[1]) != 0;
    valSO = StringToInt(parts[2]);
    hasDO = StringToInt(parts[3]) != 0;
    valDO = StringToInt(parts[4]);
    return true;
}

bool TR_DeathsDoor_WriteScriptState(bool setSO, int valSO, bool setDO, int valDO)
{
    if (!TR_IsMapRunning())
    {
        return false;
    }

    char stmtSO[192], stmtDO[192], code[896], res[32];
    if (setSO)
    {
        Format(stmtSO, sizeof(stmtSO), "if (\"SessionOptions\" in r) r.SessionOptions.SurvivorMaxIncapacitatedCount <- %d;", valSO);
    }
    else
    {
        strcopy(stmtSO, sizeof(stmtSO), "if ((\"SessionOptions\" in r) && (\"SurvivorMaxIncapacitatedCount\" in r.SessionOptions)) delete r.SessionOptions.SurvivorMaxIncapacitatedCount;");
    }

    if (setDO)
    {
        Format(stmtDO, sizeof(stmtDO), "if (\"DirectorOptions\" in r) r.DirectorOptions.SurvivorMaxIncapacitatedCount <- %d;", valDO);
    }
    else
    {
        strcopy(stmtDO, sizeof(stmtDO), "if ((\"DirectorOptions\" in r) && (\"SurvivorMaxIncapacitatedCount\" in r.DirectorOptions)) delete r.DirectorOptions.SurvivorMaxIncapacitatedCount;");
    }

    Format(code, sizeof(code), "local r = getroottable(); %s %s Convars.SetValue(\"%s\", \"DOK\");", stmtSO, stmtDO, TR_BRIDGE);
    return TR_RunVScript(code, res, sizeof(res)) && StrEqual(res, "DOK");
}

bool TR_DeathsDoor_HasDirectorTable(bool &hasSessionTable, bool &hasDirectorTable)
{
    hasSessionTable = false;
    hasDirectorTable = false;
    if (!TR_IsMapRunning())
    {
        return false;
    }
    char code[384], res[32];
    Format(code, sizeof(code),
        "local r = getroottable(); Convars.SetValue(\"%s\", format(\"T|%%d|%%d\", (\"SessionOptions\" in r) ? 1 : 0, (\"DirectorOptions\" in r) ? 1 : 0));",
        TR_BRIDGE);
    if (!TR_RunVScript(code, res, sizeof(res)))
    {
        return false;
    }
    char parts[3][8];
    if (ExplodeString(res, "|", parts, sizeof(parts), sizeof(parts[])) != 3 || !StrEqual(parts[0], "T"))
    {
        return false;
    }
    hasSessionTable = StringToInt(parts[1]) != 0;
    hasDirectorTable = StringToInt(parts[2]) != 0;
    return true;
}

// 切换或还原死门模式（仅改倒地次数规则为 0；绝不改 mp_gamemode、不重启、不加补给、不追杀已倒地玩家）。
// 必须确认 ConVar 和 SessionOptions/DirectorOptions 实际生效；若失败则回滚到切换前状态并返回 false。
bool TR_DeathsDoor_Apply(bool enable, char[] message, int maxlen, bool isUnload = false)
{
    message[0] = '\0';
    ConVar cvIncap = FindConVar("survivor_max_incapacitated_count");
    if (cvIncap == null)
    {
        strcopy(message, maxlen, "死门切换失败：未找到 survivor_max_incapacitated_count ConVar");
        strcopy(g_sDeathsDoorLastResult, sizeof(g_sDeathsDoorLastResult), message);
        TR_Log("%s", message);
        return false;
    }

    if (!TR_IsMapRunning())
    {
        if (isUnload && g_bDeathsDoorHasOrigCvar)
        {
            cvIncap.SetInt(g_iDeathsDoorOrigCvar);
            g_bDeathsDoorActive = false;
            return true;
        }
        strcopy(message, maxlen, "死门切换未生效：当前没有运行中的地图，已回滚");
        strcopy(g_sDeathsDoorLastResult, sizeof(g_sDeathsDoorLastResult), message);
        TR_Log("%s", message);
        return false;
    }

    // 1. 记录切换前快照，用于失败时精确回滚
    int prevCvar = cvIncap.IntValue;
    bool prevHasSO, prevHasDO;
    int prevSO, prevDO;
    bool prevScriptOk = TR_DeathsDoor_ReadScriptState(prevHasSO, prevSO, prevHasDO, prevDO);
    bool hasSessionTable, hasDirectorTable;
    bool tableProbeOk = TR_DeathsDoor_HasDirectorTable(hasSessionTable, hasDirectorTable);

    if (!g_bDeathsDoorHasOrigCvar)
    {
        g_iDeathsDoorOrigCvar = prevCvar > 0 ? prevCvar : 2;
        g_bDeathsDoorHasOrigCvar = true;
    }
    if (!g_bDeathsDoorHasOrigScript && prevScriptOk)
    {
        g_bDeathsDoorOrigHasSO = prevHasSO;
        g_iDeathsDoorOrigSO = prevSO;
        g_bDeathsDoorOrigHasDO = prevHasDO;
        g_iDeathsDoorOrigDO = prevDO;
        g_bDeathsDoorHasOrigScript = true;
    }

    if (enable)
    {
        if (!tableProbeOk || (!hasSessionTable && !hasDirectorTable))
        {
            strcopy(message, maxlen, "死门切换未生效：DirectorOptions/SessionOptions 脚本表不可用，已回滚");
            strcopy(g_sDeathsDoorLastResult, sizeof(g_sDeathsDoorLastResult), message);
            TR_Log("%s", message);
            return false;
        }

        // 先激活钩子，再写入 ConVar 与脚本表，随后双向校验读回
        g_bDeathsDoorActive = true;
        cvIncap.SetInt(0);
        bool scriptWritten = TR_DeathsDoor_WriteScriptState(hasSessionTable, 0, hasDirectorTable, 0);

        int afterCvar = cvIncap.IntValue;
        bool afterHasSO, afterHasDO;
        int afterSO, afterDO;
        bool readOk = TR_DeathsDoor_ReadScriptState(afterHasSO, afterSO, afterHasDO, afterDO);

        bool cvarOk = (afterCvar == 0);
        bool soOk = !hasSessionTable || (afterHasSO && afterSO == 0);
        bool doOk = !hasDirectorTable || (afterHasDO && afterDO == 0);
        bool engineOk = true;
        int engineVal = 0;
        if (GetFeatureStatus(FeatureType_Native, "L4D2_GetScriptValueInt") == FeatureStatus_Available)
        {
            engineVal = L4D2_GetScriptValueInt("SurvivorMaxIncapacitatedCount", -1);
            engineOk = (engineVal == 0);
        }

        if (!scriptWritten || !readOk || !cvarOk || !soOk || !doOk || !engineOk)
        {
            // 严格回滚到切换前状态
            g_bDeathsDoorActive = false;
            cvIncap.SetInt(prevCvar);
            if (prevScriptOk)
            {
                TR_DeathsDoor_WriteScriptState(prevHasSO, prevSO, prevHasDO, prevDO);
            }
            Format(message, maxlen, "死门开启未生效并已回滚（cvar=%d SO=%d/%d DO=%d/%d engine=%d）",
                afterCvar, afterHasSO ? 1 : 0, afterSO, afterHasDO ? 1 : 0, afterDO, engineVal);
            strcopy(g_sDeathsDoorLastResult, sizeof(g_sDeathsDoorLastResult), message);
            TR_Log("%s", message);
            return false;
        }

        Format(message, maxlen, "死门已开启（倒地次数=0；cvar=%d SessionOptions=%d DirectorOptions=%d engine=%d）",
            afterCvar, afterSO, afterDO, engineVal);
        strcopy(g_sDeathsDoorLastResult, sizeof(g_sDeathsDoorLastResult), message);
        TR_Log("%s", message);
        return true;
    }

    // 关闭死门：恢复到记录的原值并校验
    int targetCvar = g_bDeathsDoorHasOrigCvar ? g_iDeathsDoorOrigCvar : 2;
    bool targetHasSO = g_bDeathsDoorHasOrigScript ? g_bDeathsDoorOrigHasSO : false;
    int targetSO = g_bDeathsDoorHasOrigScript ? g_iDeathsDoorOrigSO : targetCvar;
    bool targetHasDO = g_bDeathsDoorHasOrigScript ? g_bDeathsDoorOrigHasDO : false;
    int targetDO = g_bDeathsDoorHasOrigScript ? g_iDeathsDoorOrigDO : targetCvar;

    bool wasActive = g_bDeathsDoorActive;
    g_bDeathsDoorActive = false;
    cvIncap.SetInt(targetCvar);
    bool scriptRestored = TR_DeathsDoor_WriteScriptState(targetHasSO, targetSO, targetHasDO, targetDO);

    int afterCvar = cvIncap.IntValue;
    bool afterHasSO, afterHasDO;
    int afterSO, afterDO;
    bool readOk = TR_DeathsDoor_ReadScriptState(afterHasSO, afterSO, afterHasDO, afterDO);

    bool cvarOk = (afterCvar == targetCvar);
    bool soOk = (afterHasSO == targetHasSO) && (!targetHasSO || afterSO == targetSO);
    bool doOk = (afterHasDO == targetHasDO) && (!targetHasDO || afterDO == targetDO);

    if ((!scriptRestored || !readOk || !cvarOk || !soOk || !doOk) && !isUnload)
    {
        // 还原校验失败：回滚至切换前状态
        g_bDeathsDoorActive = wasActive;
        cvIncap.SetInt(prevCvar);
        if (prevScriptOk)
        {
            TR_DeathsDoor_WriteScriptState(prevHasSO, prevSO, prevHasDO, prevDO);
        }
        Format(message, maxlen, "死门关闭还原未生效并已回滚（cvar=%d 目标=%d SO=%d/%d DO=%d/%d）",
            afterCvar, targetCvar, afterHasSO ? 1 : 0, afterSO, afterHasDO ? 1 : 0, afterDO);
        strcopy(g_sDeathsDoorLastResult, sizeof(g_sDeathsDoorLastResult), message);
        TR_Log("%s", message);
        return false;
    }

    Format(message, maxlen, "死门已关闭并还原（survivor_max_incapacitated_count=%d，脚本字段已恢复）", afterCvar);
    strcopy(g_sDeathsDoorLastResult, sizeof(g_sDeathsDoorLastResult), message);
    TR_Log("%s", message);
    return true;
}

bool TR_DeathsDoor_SetEnabled(bool enable, char[] message, int maxlen)
{
    if (g_cvDeathsDoor == null)
    {
        strcopy(message, maxlen, "死门开关未初始化");
        return false;
    }
    if (enable && !TR_FeatureEnabled())
    {
        strcopy(message, maxlen, "插件总开关已关闭，无法开启死门");
        return false;
    }

    bool ok = TR_DeathsDoor_Apply(enable, message, maxlen, false);
    g_bDeathsDoorSyncing = true;
    g_cvDeathsDoor.SetBool(ok ? enable : !enable);
    g_bDeathsDoorSyncing = false;
    if (ok)
    {
        int idx = TR_FindParamByName("tankrun_deaths_door");
        if (idx >= 0)
        {
            TR_RememberParam(idx, enable ? 1.0 : 0.0);
        }
    }
    return ok;
}

void TR_V23Rules_OnConVarChanged(ConVar convar, const char[] oldValue, const char[] newValue)
{
    ConVar cvMode = FindConVar("mp_gamemode");
    if (convar == cvMode)
    {
        TR_Water_Sync("模式变更");
        if (!TR_RockGlowEnabled())
        {
            TR_Glow_ClearAll();
        }
        return;
    }
    if (convar == g_cvNoWaterSlowdown || convar == g_cvEnabled)
    {
        TR_Water_Sync("ConVar 变更");
    }
    if (convar == g_cvEnabled && !convar.BoolValue && g_bDeathsDoorActive)
    {
        char msg[160];
        TR_DeathsDoor_Apply(false, msg, sizeof(msg), true);
    }
    if (convar == g_cvDeathsDoor && !g_bDeathsDoorSyncing)
    {
        bool want = convar.BoolValue;
        bool prev = StringToInt(oldValue) != 0;
        if (want == g_bDeathsDoorActive)
        {
            return;
        }
        char msg[160];
        if (!TR_DeathsDoor_Apply(want, msg, sizeof(msg), false))
        {
            g_bDeathsDoorSyncing = true;
            convar.SetBool(prev);
            g_bDeathsDoorSyncing = false;
            TR_Chat(0, "%s", msg);
        }
    }
    #pragma unused newValue
}

void TR_V23Rules_OnPluginStart()
{
    g_bV23MapStarted = TR_IsMapRunning();
    TR_FF_ResetAll();
    TR_FF_InstallFuelBarrelDetour();
    HookEventEx("break_prop", TR_FF_OnBreakPropPre, EventHookMode_Pre);
    if (g_cvNoWaterSlowdown != null)
    {
        g_cvNoWaterSlowdown.AddChangeHook(TR_V23Rules_OnConVarChanged);
    }
    if (g_cvDeathsDoor != null)
    {
        g_cvDeathsDoor.AddChangeHook(TR_V23Rules_OnConVarChanged);
    }
    if (g_cvEnabled != null)
    {
        g_cvEnabled.AddChangeHook(TR_V23Rules_OnConVarChanged);
    }
    ConVar cvMode = FindConVar("mp_gamemode");
    if (cvMode != null)
    {
        cvMode.AddChangeHook(TR_V23Rules_OnConVarChanged);
    }
    if (TR_IsMapRunning())
    {
        TR_FF_HookExistingProps();
    }
}

void TR_V23Rules_OnAllPluginsLoaded()
{
    if (!g_bFFFuelBarrelDetourReady && g_bFFPendingOsRetry
        && GetFeatureStatus(FeatureType_Native, "L4D_GetServerOS") == FeatureStatus_Available)
    {
        TR_FF_InstallFuelBarrelDetour();
    }
}

void TR_V23Rules_OnMapStart()
{
    g_bV23MapStarted = true;
    TR_FF_ResetAll();
    TR_Water_OnMapReset();
    g_bDeathsDoorHasOrigScript = false;
    g_bDeathsDoorActive = false;
    TR_FF_HookExistingProps();
}

void TR_V23Rules_OnRuntimeReady(const char[] reason)
{
    if (!g_bV23MapStarted || !TR_IsMapRunning())
    {
        return;
    }
    TR_Water_Sync(reason);
    if (TR_DeathsDoorShouldBeActive())
    {
        char msg[160];
        if (!TR_DeathsDoor_Apply(true, msg, sizeof(msg), false))
        {
            g_bDeathsDoorSyncing = true;
            g_cvDeathsDoor.SetBool(false);
            g_bDeathsDoorSyncing = false;
        }
    }
    else if (g_bDeathsDoorActive)
    {
        char msg[160];
        TR_DeathsDoor_Apply(false, msg, sizeof(msg), true);
    }
}

void TR_V23Rules_OnMapEnd()
{
    g_bV23MapStarted = false;
    TR_FF_ResetAll();
}

void TR_V23Rules_OnPluginEnd()
{
    TR_FF_UninstallFuelBarrelDetour();
    // 卸载插件时务必恢复 WaterSlowsMovement 与死门规则原值
    if (g_bWaterApplied || g_bWaterSavedOriginal)
    {
        g_bWaterApplied = false;
        if (TR_IsMapRunning() && g_bWaterSavedOriginal)
        {
            TR_Water_WriteScriptState(g_bWaterOrigHasSO, g_iWaterOrigSO, g_bWaterOrigHasDO, g_iWaterOrigDO);
        }
    }
    if (g_bDeathsDoorActive)
    {
        char msg[160];
        TR_DeathsDoor_Apply(false, msg, sizeof(msg), true);
    }
}

// ---------------------------------------------------------------- Left4DHooks ScriptValue 统一拦截
public Action L4D2_OnGetScriptValueInt(const char[] key, int &retVal, int hScope)
{
    #pragma unused hScope
    if (!g_bV23MapStarted)
    {
        return Plugin_Continue;
    }
    if (StrEqual(key, "WaterSlowsMovement", false))
    {
        if (TR_NoWaterSlowdownActive())
        {
            retVal = 0;
            return Plugin_Handled;
        }
        return Plugin_Continue;
    }
    if (StrEqual(key, "SurvivorMaxIncapacitatedCount", false))
    {
        if (g_bDeathsDoorActive && TR_DeathsDoorShouldBeActive())
        {
            retVal = 0;
            return Plugin_Handled;
        }
        return Plugin_Continue;
    }
    return Plugin_Continue;
}

public Action L4D_OnGetScriptValueInt(const char[] key, int &retVal)
{
    if (!g_bV23MapStarted)
    {
        return Plugin_Continue;
    }
    if (StrEqual(key, "WaterSlowsMovement", false))
    {
        if (TR_NoWaterSlowdownActive())
        {
            retVal = 0;
            return Plugin_Handled;
        }
        return Plugin_Continue;
    }
    if (StrEqual(key, "SurvivorMaxIncapacitatedCount", false))
    {
        if (g_bDeathsDoorActive && TR_DeathsDoorShouldBeActive())
        {
            retVal = 0;
            return Plugin_Handled;
        }
        return Plugin_Continue;
    }
    return Plugin_Continue;
}
