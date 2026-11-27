// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

// Tank Run v2.3 骗饼处死层（Bait-Rock Slay — 原地等待方案）
// 精确规则：
//   - 仅限 Tank Run 合作模式生效，默认关闭（tankrun_bait_rock_slay = 0）
//   - 覆盖原版 AI Tank（复用 ThrowRockHelper +0x34 只读兼容守卫，与描边开关完全解耦）
//     与容量补位 Tank（读取已提交 attackTargetUid 与实际发射石头 EntRef）
//   - 原版构建/目标读取不可靠时仅关闭原版路径并清晰说明，绝不猜测最近玩家、准星或 ChooseVictim
//   - 确认本次石头真实离手时锁定候选：仅当原锁定者是可验证真人生还者且离手瞬间碰撞体边缘距在 0..64u 内
//   - 原地等待：不传送、不冻结、不提前处死、不改石头归属/弹道、不复制原石
//   - 仅在已验证的正常终结事件（OnDetonate）并确认当帧伤害/命中/击飞标志结齐且实体已销毁后判定；
//     原锁定目标从未被这颗石头击中才立即处死同生命同代次的原 Tank（即使打中他人也允许处死）
//   - 异常删除（无 OnDetonate 直接 OnEntityDestroyed）、回合/地图切换、目标离开、接管、超时、归因不明一律仅清理状态

#if defined _tankrun_bait_rock_included
 #endinput
#endif
#define _tankrun_bait_rock_included

#define TR_BAIT_ROCK_MAX_EDGE_DIST       64.0
#define TR_BAIT_ROCK_DESTROY_GRACE_TICKS 2
#define TR_BAIT_ROCK_PRE_OBS_WINDOW      0.15

enum struct TR_RockIntent
{
    int generation;
    int tankLife;
    int targetUid;
    int targetLife;
    float observedAt;
}

enum struct TR_BaitRock
{
    int rockRef;
    int throwGen;
    int roundGen;
    int tankId;       // 原版：userid；补位：replica EntRef
    int replicaSlot;  // -1 表示原版 AI Tank
    int tankLife;
    int targetUid;
    int targetLife;
    float expires;
    bool hitTarget;
    int detonateTick;
    bool destroyed;
    int preTouchTick;
    float preKnockEnd;
}

ArrayList g_BaitRocks;
TR_RockIntent g_NativeRockIntent[MAXPLAYERS + 1];
TR_RockIntent g_ReplicaRockIntent[TR_BAIT_REPLICA_SLOTS];
int g_RockClientLife[MAXPLAYERS + 1];
int g_RockRoundGen;
int g_RockThrowGen;

int g_iStatBaitRockCandidatesNative;
int g_iStatBaitRockCandidatesReplica;
int g_iStatBaitRockSlaysNative;
int g_iStatBaitRockSlaysReplica;
int g_iStatBaitRockBlockedHit;
int g_iStatBaitRockCancelled;

bool TR_BaitRockEnabled()
{
    return g_cvBaitRockSlay != null && g_cvBaitRockSlay.BoolValue
        && !g_bRoundEnded && !g_bMapEnding && TR_IsTankRunCoop();
}

bool TR_BaitRockHuman(int client)
{
    return TR_IsSurvivor(client) && !IsFakeClient(client);
}

int TR_BaitRockPendingCount()
{
    return g_BaitRocks != null ? g_BaitRocks.Length : 0;
}

void TR_BaitRockGetStats(int &candNative, int &candReplica, int &slayNative, int &slayReplica, int &blockedHit, int &cancelled)
{
    candNative = g_iStatBaitRockCandidatesNative;
    candReplica = g_iStatBaitRockCandidatesReplica;
    slayNative = g_iStatBaitRockSlaysNative;
    slayReplica = g_iStatBaitRockSlaysReplica;
    blockedHit = g_iStatBaitRockBlockedHit;
    cancelled = g_iStatBaitRockCancelled;
}

void TR_BaitRockReset()
{
    g_RockRoundGen++;
    if (g_BaitRocks != null)
    {
        g_BaitRocks.Clear();
    }
    TR_RockIntent empty;
    for (int i = 1; i <= MAXPLAYERS; i++)
    {
        g_NativeRockIntent[i] = empty;
    }
    for (int i = 0; i < sizeof(g_ReplicaRockIntent); i++)
    {
        g_ReplicaRockIntent[i] = empty;
    }
}

public void TR_BaitRockSetting(ConVar convar, const char[] oldValue, const char[] newValue)
{
    #pragma unused convar, oldValue, newValue
    TR_BaitRockReset();
}

void TR_BaitRockStart()
{
    g_BaitRocks = new ArrayList(sizeof(TR_BaitRock));
    if (g_cvBaitRockSlay != null) g_cvBaitRockSlay.AddChangeHook(TR_BaitRockSetting);
    if (g_cvEnabled != null) g_cvEnabled.AddChangeHook(TR_BaitRockSetting);
    if (g_cvRockEnabled != null) g_cvRockEnabled.AddChangeHook(TR_BaitRockSetting);
    ConVar mode = FindConVar("mp_gamemode");
    if (mode != null) mode.AddChangeHook(TR_BaitRockSetting);
    HookEvent("player_spawn", TR_BaitRockLifeEvent, EventHookMode_Post);
    HookEvent("player_team", TR_BaitRockLifeEvent, EventHookMode_Post);
}

void TR_BaitRockNewLife(int client)
{
    if (client < 1 || client > MaxClients)
    {
        return;
    }
    int uid = IsClientConnected(client) ? GetClientUserId(client) : 0;
    if (uid > 0 && g_BaitRocks != null)
    {
        for (int i = g_BaitRocks.Length - 1; i >= 0; i--)
        {
            TR_BaitRock shot;
            g_BaitRocks.GetArray(i, shot);
            bool matchTank = (shot.replicaSlot < 0 && shot.tankId == uid);
            bool matchTarget = (shot.targetUid == uid);
            if (!matchTank && !matchTarget)
            {
                continue;
            }
            if (matchTarget && shot.hitTarget)
            {
                g_iStatBaitRockBlockedHit++;
                TR_LogEvent("骗饼不处死（原目标受击后离场/死亡）：slot=%d throw=%d rockRef=%d targetUid=%d",
                    shot.replicaSlot, shot.throwGen, shot.rockRef, shot.targetUid);
            }
            else
            {
                g_iStatBaitRockCancelled++;
                TR_LogEvent("骗饼状态清理（身份/生命变更）：slot=%d throw=%d rockRef=%d matchTank=%d matchTarget=%d",
                    shot.replicaSlot, shot.throwGen, shot.rockRef, matchTank ? 1 : 0, matchTarget ? 1 : 0);
            }
            g_BaitRocks.Erase(i);
        }
    }
    g_RockClientLife[client]++;
    TR_RockIntent empty;
    g_NativeRockIntent[client] = empty;
}

public void TR_BaitRockLifeEvent(Event event, const char[] name, bool dontBroadcast)
{
    #pragma unused name, dontBroadcast
    TR_BaitRockNewLife(GetClientOfUserId(event.GetInt("userid")));
}

void TR_BaitRockOnReplicaRemoved(int slot)
{
    if (slot < 0 || slot >= TR_BAIT_REPLICA_SLOTS)
    {
        return;
    }
    TR_RockIntent empty;
    g_ReplicaRockIntent[slot] = empty;
    if (g_BaitRocks == null)
    {
        return;
    }
    for (int i = g_BaitRocks.Length - 1; i >= 0; i--)
    {
        if (g_BaitRocks.Get(i, TR_BaitRock::replicaSlot) == slot)
        {
            g_iStatBaitRockCancelled++;
            g_BaitRocks.Erase(i);
        }
    }
}

void TR_BaitRockNativeWindup(int tank)
{
    if (tank < 1 || tank > MaxClients)
    {
        return;
    }
    TR_RockIntent intent;
    if (TR_BaitRockEnabled() && g_bGlowNativeTargetReady && TR_IsAiTank(tank))
    {
        if (g_iTankLifeGen[tank] <= 0)
        {
            TR_Bait_OnNativeTankTracked(tank);
        }
        if (g_iTankLifeGen[tank] > 0)
        {
            intent.generation = ++g_RockThrowGen;
            intent.tankLife = g_iTankLifeGen[tank];
            // 若正处于同一帧 ThrowRockHelper 同步调用栈内（Pre..Post），直接取该次实时读取的目标作为初始观测；
            // 前摇期间每次 ThrowRockHelper 回调都会覆盖更新（包括失效时清零），绝不跨投石复用旧观测。
            int tankUid = GetClientUserId(tank);
            if (g_iGlowInHelperTank == tank && g_iGlowInHelperTankUid == tankUid && g_iGlowInHelperTargetUid > 0)
            {
                int surv = GetClientOfUserId(g_iGlowInHelperTargetUid);
                if (surv > 0 && TR_IsSurvivor(surv))
                {
                    intent.observedAt = GetEngineTime();
                    intent.targetUid = g_iGlowInHelperTargetUid;
                    intent.targetLife = g_RockClientLife[surv];
                }
            }
        }
    }
    g_NativeRockIntent[tank] = intent;
}

void TR_BaitRockObserve(int tank, int survivor)
{
    if (tank < 1 || tank > MaxClients || !g_NativeRockIntent[tank].generation)
    {
        return;
    }
    // 无效句柄或非生还者也会覆盖并清空之前的观测，绝不沿用过期旧目标。
    g_NativeRockIntent[tank].observedAt = GetEngineTime();
    if (survivor > 0 && survivor <= MaxClients && TR_IsSurvivor(survivor))
    {
        g_NativeRockIntent[tank].targetUid = GetClientUserId(survivor);
        g_NativeRockIntent[tank].targetLife = g_RockClientLife[survivor];
    }
    else
    {
        g_NativeRockIntent[tank].targetUid = 0;
        g_NativeRockIntent[tank].targetLife = 0;
    }
}

void TR_BaitRockCancelWindup(int tank)
{
    if (tank < 1 || tank > MaxClients)
    {
        return;
    }
    TR_RockIntent empty;
    g_NativeRockIntent[tank] = empty;
}

void TR_BaitRockReplicaWindup(int slot, int targetUid)
{
    if (slot < 0 || slot >= TR_BAIT_REPLICA_SLOTS)
    {
        return;
    }
    TR_RockIntent intent;
    int target = targetUid > 0 ? GetClientOfUserId(targetUid) : 0;
    if (TR_BaitRockEnabled() && TR_BaitRockHuman(target) && g_TCActorBirths[slot] > 0)
    {
        intent.generation = ++g_RockThrowGen;
        intent.tankLife = g_TCActorBirths[slot];
        intent.targetUid = targetUid;
        intent.targetLife = g_RockClientLife[target];
        intent.observedAt = GetEngineTime();
    }
    g_ReplicaRockIntent[slot] = intent;
}

void TR_BaitRockCancelReplicaWindup(int slot)
{
    if (slot < 0 || slot >= TR_BAIT_REPLICA_SLOTS)
    {
        return;
    }
    TR_RockIntent empty;
    g_ReplicaRockIntent[slot] = empty;
}

int TR_BaitRockGetThrower(int rock)
{
    if (rock <= MaxClients || !IsValidEntity(rock))
    {
        return -1;
    }
    if (HasEntProp(rock, Prop_Send, "m_hThrower"))
    {
        return GetEntPropEnt(rock, Prop_Send, "m_hThrower");
    }
    if (HasEntProp(rock, Prop_Data, "m_hThrower"))
    {
        return GetEntPropEnt(rock, Prop_Data, "m_hThrower");
    }
    return -1;
}

float TR_BaitRockReadKnockdownStamp(int client)
{
    if (client < 1 || client > MaxClients || !IsClientInGame(client))
    {
        return 0.0;
    }
    if (HasEntProp(client, Prop_Send, "m_knockdownTimer"))
    {
        // L4D2 CTerrorPlayer::m_knockdownTimer is an IntervalTimer (1 float element: m_timestamp).
        int count = GetEntPropArraySize(client, Prop_Send, "m_knockdownTimer");
        if (count >= 1)
        {
            return GetEntPropFloat(client, Prop_Send, "m_knockdownTimer", 0);
        }
    }
    return 0.0;
}

void TR_BaitRockReleased(int slot, int tank, int rock)
{
    TR_RockIntent intent;
    TR_RockIntent empty;
    if (slot < 0)
    {
        if (tank < 1 || tank > MaxClients)
        {
            return;
        }
        intent = g_NativeRockIntent[tank];
        g_NativeRockIntent[tank] = empty;
    }
    else
    {
        if (slot >= TR_BAIT_REPLICA_SLOTS)
        {
            return;
        }
        intent = g_ReplicaRockIntent[slot];
        g_ReplicaRockIntent[slot] = empty;
    }

    if (!intent.generation || !TR_BaitRockEnabled() || g_BaitRocks == null)
    {
        return;
    }
    if (rock <= MaxClients || !IsValidEntity(rock))
    {
        return;
    }
    char cls[24];
    if (!GetEntityClassname(rock, cls, sizeof(cls)) || !StrEqual(cls, "tank_rock"))
    {
        return;
    }

    int target = intent.targetUid > 0 ? GetClientOfUserId(intent.targetUid) : 0;
    if (!TR_BaitRockHuman(target) || g_RockClientLife[target] != intent.targetLife)
    {
        return;
    }

    float distance = -1.0;
    if (slot < 0)
    {
        if (!g_bGlowNativeTargetReady || !TR_IsAiTank(tank)
            || g_iTankLifeGen[tank] != intent.tankLife
            || g_fTankPendingActivation[tank] <= 0.0
            || (GetGameTime() - g_fTankPendingActivation[tank]) > TR_ROCK_WINDUP_LIMIT
            || intent.observedAt <= 0.0
            || (GetEngineTime() - intent.observedAt) > TR_GLOW_NATIVE_OBS_TTL
            || TR_BaitRockGetThrower(rock) != tank)
        {
            return;
        }
        distance = TR_GetClientAABBEdgeDistance(tank, target);
    }
    else
    {
        if (tank <= MaxClients || !IsValidEntity(tank)
            || g_TCActorBirths[slot] != intent.tankLife
            || g_TCEntityRefs[slot] != EntIndexToEntRef(tank)
            || g_TCCombat[slot].attackTargetUid != intent.targetUid
            || TC_CombatHealth(slot) <= 0)
        {
            return;
        }
        distance = TR_GetReplicaSurvivorEdgeDistance(tank, target);
    }

    if (!(distance >= 0.0 && distance <= TR_BAIT_ROCK_MAX_EDGE_DIST))
    {
        TR_LogEvent("骗饼离手超距排除：slot=%d tank=%d targetUid=%d edge=%.2fu (> %.1fu)",
            slot, tank, intent.targetUid, distance, TR_BAIT_ROCK_MAX_EDGE_DIST);
        return;
    }

    int rockRef = EntIndexToEntRef(rock);
    if (g_BaitRocks.FindValue(rockRef, TR_BaitRock::rockRef) >= 0)
    {
        return;
    }

    TR_BaitRock shot;
    shot.rockRef = rockRef;
    shot.throwGen = intent.generation;
    shot.roundGen = g_RockRoundGen;
    shot.tankId = slot < 0 ? GetClientUserId(tank) : EntIndexToEntRef(tank);
    shot.replicaSlot = slot;
    shot.tankLife = intent.tankLife;
    shot.targetUid = intent.targetUid;
    shot.targetLife = intent.targetLife;
    shot.expires = GetEngineTime() + TR_ROCK_SETTLE_MAX_AGE;
    g_BaitRocks.PushArray(shot);

    if (slot < 0) g_iStatBaitRockCandidatesNative++;
    else g_iStatBaitRockCandidatesReplica++;

    TR_LogEvent("骗饼离手锁定：slot=%d tank=%d life=%d throw=%d rockRef=%d targetUid=%d targetLife=%d edge=%.2fu",
        slot, tank, shot.tankLife, shot.throwGen, shot.rockRef, shot.targetUid, shot.targetLife, distance);
}

int TR_BaitRockFind(int rock)
{
    if (g_BaitRocks == null || !g_BaitRocks.Length || rock <= MaxClients || !IsValidEntity(rock))
    {
        return -1;
    }
    return g_BaitRocks.FindValue(EntIndexToEntRef(rock), TR_BaitRock::rockRef);
}

// 可信命中归因（OnTakeDamage Pre / Post）：
// 仅当 CTankRock::BounceTouch 通过引擎全部门控（含安全屋防穿门）进入 DispatchTraceAttack 后才会触发；
// 按 rockRef 精确匹配，覆盖同帧多石、ownerless 容量补位石以及零伤但实际进入伤害分发的命中。
void TR_BaitRockOnTakeDamage(int victim, int inflictor)
{
    if (victim < 1 || victim > MaxClients || !IsClientInGame(victim))
    {
        return;
    }
    int row = TR_BaitRockFind(inflictor);
    if (row < 0)
    {
        return;
    }
    if (g_BaitRocks.Get(row, TR_BaitRock::targetUid) != GetClientUserId(victim)
        || g_BaitRocks.Get(row, TR_BaitRock::targetLife) != g_RockClientLife[victim])
    {
        return;
    }
    if (!g_BaitRocks.Get(row, TR_BaitRock::hitTarget))
    {
        g_BaitRocks.Set(row, true, TR_BaitRock::hitTarget);
        TR_LogEvent("骗饼命中记录（OnTakeDamage）：rockRef=%d target=#%d uid=%d",
            g_BaitRocks.Get(row, TR_BaitRock::rockRef), victim, GetClientUserId(victim));
    }
}

// BounceTouch Pre 绝不直接判为命中（避免将安全屋防穿门等阻断误判为命中），
// 仅记录接触前击飞计时器快照，用于 Post 校验无伤但真实触发石头击飞（m_knockdownReason == 2）的边界。
public Action L4D_TankRock_BounceTouch(int tank, int rock, int other)
{
    #pragma unused tank
    if (other < 1 || other > MaxClients || !IsClientInGame(other))
    {
        return Plugin_Continue;
    }
    int row = TR_BaitRockFind(rock);
    if (row < 0)
    {
        return Plugin_Continue;
    }
    if (g_BaitRocks.Get(row, TR_BaitRock::targetUid) != GetClientUserId(other)
        || g_BaitRocks.Get(row, TR_BaitRock::targetLife) != g_RockClientLife[other])
    {
        return Plugin_Continue;
    }
    g_BaitRocks.Set(row, GetGameTickCount(), TR_BaitRock::preTouchTick);
    g_BaitRocks.Set(row, TR_BaitRockReadKnockdownStamp(other), TR_BaitRock::preKnockEnd);
    return Plugin_Continue;
}

public void L4D_TankRock_BounceTouch_Post(int tank, int rock, int other)
{
    #pragma unused tank
    if (other < 1 || other > MaxClients || !IsClientInGame(other))
    {
        return;
    }
    int row = TR_BaitRockFind(rock);
    if (row < 0 || g_BaitRocks.Get(row, TR_BaitRock::hitTarget))
    {
        return;
    }
    if (g_BaitRocks.Get(row, TR_BaitRock::targetUid) != GetClientUserId(other)
        || g_BaitRocks.Get(row, TR_BaitRock::targetLife) != g_RockClientLife[other])
    {
        return;
    }
    if (g_BaitRocks.Get(row, TR_BaitRock::preTouchTick) != GetGameTickCount())
    {
        return;
    }
    float preStamp = g_BaitRocks.Get(row, TR_BaitRock::preKnockEnd);
    float postStamp = TR_BaitRockReadKnockdownStamp(other);
    int reason = HasEntProp(other, Prop_Send, "m_knockdownReason")
        ? GetEntProp(other, Prop_Send, "m_knockdownReason") : 0;
    if (postStamp > preStamp + 0.001 && reason == 2)
    {
        g_BaitRocks.Set(row, true, TR_BaitRock::hitTarget);
        TR_LogEvent("骗饼命中记录（BounceTouch 击飞实证）：rockRef=%d target=#%d uid=%d knockStamp=%.2f->%.2f",
            g_BaitRocks.Get(row, TR_BaitRock::rockRef), other, GetClientUserId(other), preStamp, postStamp);
    }
}

void TR_BaitRockDetonate(int tank, int rock)
{
    int row = TR_BaitRockFind(rock);
    if (row < 0)
    {
        return;
    }
    int ownerId = g_BaitRocks.Get(row, TR_BaitRock::tankId);
    int slot = g_BaitRocks.Get(row, TR_BaitRock::replicaSlot);
    if ((slot < 0 && (tank < 1 || GetClientOfUserId(ownerId) != tank))
        || (slot >= 0 && tank > 0))
    {
        g_iStatBaitRockCancelled++;
        TR_LogEvent("骗饼取消（OnDetonate 归属不匹配）：slot=%d expectedId=%d actualTank=%d rockRef=%d",
            slot, ownerId, tank, g_BaitRocks.Get(row, TR_BaitRock::rockRef));
        g_BaitRocks.Erase(row);
        return;
    }
    if (g_BaitRocks.Get(row, TR_BaitRock::detonateTick) == 0)
    {
        g_BaitRocks.Set(row, GetGameTickCount(), TR_BaitRock::detonateTick);
    }
}

void TR_BaitRockDestroyed(int rock)
{
    int row = TR_BaitRockFind(rock);
    if (row < 0)
    {
        return;
    }
    int detTick = g_BaitRocks.Get(row, TR_BaitRock::detonateTick);
    if (detTick > 0)
    {
        g_BaitRocks.Set(row, true, TR_BaitRock::destroyed);
    }
    else
    {
        // 无正常 OnDetonate 的异常删除：一律仅清理状态，严禁当作骗饼成功自动处死。
        g_iStatBaitRockCancelled++;
        TR_LogEvent("骗饼取消（无 OnDetonate 异常删除）：slot=%d throw=%d rockRef=%d",
            g_BaitRocks.Get(row, TR_BaitRock::replicaSlot),
            g_BaitRocks.Get(row, TR_BaitRock::throwGen),
            g_BaitRocks.Get(row, TR_BaitRock::rockRef));
        g_BaitRocks.Erase(row);
    }
}

bool TR_BaitRockWaiting(int tankId, int slot)
{
    if (g_BaitRocks == null)
    {
        return false;
    }
    for (int i = 0; i < g_BaitRocks.Length; i++)
    {
        if (g_BaitRocks.Get(i, TR_BaitRock::tankId) == tankId
            && g_BaitRocks.Get(i, TR_BaitRock::replicaSlot) == slot)
        {
            return true;
        }
    }
    return false;
}

void TR_BaitRockFrame()
{
    if (g_BaitRocks == null || !g_BaitRocks.Length)
    {
        return;
    }
    if (!TR_BaitRockEnabled())
    {
        TR_BaitRockReset();
        return;
    }

    int tick = GetGameTickCount();
    float now = GetEngineTime();

    // 先完成状态表结算与移除，再执行处死，防止处死回调（player_death -> NewLife）在遍历途中修改数组索引。
    int slaySlot[64];
    int slayTankId[64];
    int slayTankLife[64];
    int slayThrowGen[64];
    int slayRockRef[64];
    int slayTargetUid[64];
    int slayCount = 0;

    for (int i = g_BaitRocks.Length - 1; i >= 0; i--)
    {
        TR_BaitRock shot;
        g_BaitRocks.GetArray(i, shot);

        int slot = shot.replicaSlot;
        int target = shot.targetUid > 0 ? GetClientOfUserId(shot.targetUid) : 0;
        int tank = slot < 0 ? GetClientOfUserId(shot.tankId) : EntRefToEntIndex(shot.tankId);

        bool expired = !(now < shot.expires);
        bool valid = shot.roundGen == g_RockRoundGen && !expired
            && TR_BaitRockHuman(target) && g_RockClientLife[target] == shot.targetLife;
        if (slot < 0)
        {
            valid = valid && TR_IsAiTank(tank) && g_iTankLifeGen[tank] == shot.tankLife;
        }
        else
        {
            valid = valid && slot >= 0 && slot < TR_BAIT_REPLICA_SLOTS
                && tank > MaxClients && g_TCEntityRefs[slot] == shot.tankId
                && g_TCActorBirths[slot] == shot.tankLife && TC_CombatHealth(slot) > 0;
        }

        int rockEnt = EntRefToEntIndex(shot.rockRef);
        bool rockAlive = (rockEnt != INVALID_ENT_REFERENCE && IsValidEntity(rockEnt));

        if (!valid)
        {
            g_BaitRocks.Erase(i);
            if (shot.hitTarget)
            {
                g_iStatBaitRockBlockedHit++;
            }
            else
            {
                g_iStatBaitRockCancelled++;
            }
            TR_LogEvent("骗饼清理（状态失效/超时）：slot=%d throw=%d rockRef=%d expired=%d detonate=%d hit=%d",
                slot, shot.throwGen, shot.rockRef, expired ? 1 : 0, shot.detonateTick, shot.hitTarget ? 1 : 0);
            continue;
        }

        if (shot.detonateTick == 0)
        {
            if (rockAlive)
            {
                continue;
            }
            // 实体已消失但从未收到 OnDetonate：异常删除兜底，不处死。
            g_BaitRocks.Erase(i);
            g_iStatBaitRockCancelled++;
            TR_LogEvent("骗饼取消（实体消失无 OnDetonate）：slot=%d throw=%d rockRef=%d",
                slot, shot.throwGen, shot.rockRef);
            continue;
        }

        // 已收到 OnDetonate：至少等到下一个服务器 tick，确保终结当帧所有伤害/命中/击飞回调全部结齐。
        if (tick <= shot.detonateTick)
        {
            continue;
        }
        // 若 UTIL_Remove 延迟至次帧 CleanupDeleteList，宽容等待最多 TR_BAIT_ROCK_DESTROY_GRACE_TICKS 个 tick 确认销毁。
        if (!shot.destroyed && rockAlive && (tick - shot.detonateTick) <= TR_BAIT_ROCK_DESTROY_GRACE_TICKS)
        {
            continue;
        }

        g_BaitRocks.Erase(i);
        bool confirmedDestroyed = shot.destroyed || !rockAlive;
        TR_LogEvent("骗饼结算：slot=%d throw=%d rockRef=%d detonate=%d destroyed=%d rockAlive=%d hit=%d",
            slot, shot.throwGen, shot.rockRef, shot.detonateTick, shot.destroyed ? 1 : 0, rockAlive ? 1 : 0, shot.hitTarget ? 1 : 0);

        if (!confirmedDestroyed)
        {
            g_iStatBaitRockCancelled++;
            TR_LogEvent("骗饼取消（OnDetonate 后实体仍存活，归因不明）：slot=%d throw=%d rockRef=%d",
                slot, shot.throwGen, shot.rockRef);
            continue;
        }
        if (shot.hitTarget)
        {
            g_iStatBaitRockBlockedHit++;
            TR_LogEvent("骗饼不处死（原目标受击）：slot=%d throw=%d rockRef=%d targetUid=%d",
                slot, shot.throwGen, shot.rockRef, shot.targetUid);
            continue;
        }

        if (slayCount < sizeof(slaySlot))
        {
            slaySlot[slayCount] = slot;
            slayTankId[slayCount] = shot.tankId;
            slayTankLife[slayCount] = shot.tankLife;
            slayThrowGen[slayCount] = shot.throwGen;
            slayRockRef[slayCount] = shot.rockRef;
            slayTargetUid[slayCount] = shot.targetUid;
            slayCount++;
        }
    }

    for (int k = 0; k < slayCount; k++)
    {
        int slot = slaySlot[k];
        if (slot < 0)
        {
            int tank = GetClientOfUserId(slayTankId[k]);
            if (!TR_IsAiTank(tank) || g_iTankLifeGen[tank] != slayTankLife[k])
            {
                continue;
            }
            // 若该 Tank 仍有另一颗未结算的原石在飞，避免立即快踢导致另一颗原石变成空包弹。
            bool hasOtherRock = TR_BaitRockWaiting(slayTankId[k], -1);
            g_iStatBaitRockSlaysNative++;
            TR_Log("骗饼处死生效（原版）：Tank#%d userid=%d life=%d throw=%d rockRef=%d targetUid=%d fastKick=%d",
                tank, slayTankId[k], slayTankLife[k], slayThrowGen[k], slayRockRef[k], slayTargetUid[k], hasOtherRock ? 0 : 1);
            TR_KillTank(tank, "骗饼处死", !hasOtherRock);
        }
        else
        {
            int ent = EntRefToEntIndex(slayTankId[k]);
            if (slot < 0 || slot >= TR_BAIT_REPLICA_SLOTS
                || ent <= MaxClients || !IsValidEntity(ent)
                || g_TCEntityRefs[slot] != slayTankId[k]
                || g_TCActorBirths[slot] != slayTankLife[k]
                || TC_CombatHealth(slot) <= 0)
            {
                continue;
            }
            g_iStatBaitRockSlaysReplica++;
            TR_Log("骗饼处死生效（补位）：slot=%d ent=%d life=%d throw=%d rockRef=%d targetUid=%d",
                slot, ent, slayTankLife[k], slayThrowGen[k], slayRockRef[k], slayTargetUid[k]);
            TC_RemoveReplica(slot);
        }
    }
}
