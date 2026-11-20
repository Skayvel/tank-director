// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

// Tank Run 综合控制中心 - 投石 / 拳击 / 目标策略层（M04 / M05 / M06 / M07 / M08）
// 投石由原版能力执行，以 CTankRock::OnRelease_Post 和真实实体确认离手。

#if defined _tankrun_rock_included
 #endinput
#endif
#define _tankrun_rock_included

#define TR_ACT_HULK_THROW          761
#define TR_ACT_TANK_OVERHEAD_THROW 762
#define TR_FIRST_ROCK_TERRAIN_GRACE 6.0
#define TR_FIRST_ROCK_EXIT_CLAW_GRACE 1.75
#define TR_FIRST_ROCK_RELEASE_GRACE 3.25

DynamicDetour g_hFirstRockClawGate;
ConVar g_cvAiAbilities;

int g_iStatThrowActivate = 0;
int g_iStatThrowGateBlocked = 0;
int g_iStatDelayedBlocked = 0;
int g_iStatChooseVictim = 0;

int g_iTankRockLastRef[MAXPLAYERS + 1];
float g_fTankRockReleaseClock[MAXPLAYERS + 1];
int g_iTankRockThrowActivity[MAXPLAYERS + 1];

void TR_InstallFirstRockClawGate()
{
    g_cvAiAbilities = FindConVar("z_allow_ai_to_use_abilities");

    bool enabled;
    if (GetFeatureStatus(FeatureType_Native, "DynamicDetour.DynamicDetour") == FeatureStatus_Available)
    {
        GameData data = new GameData("tankrun_firstrock");
        if (data != null)
        {
            Address primary = data.GetMemSig("TankRun::CTankClaw::PrimaryAttack");
            delete data;
            if (primary != Address_Null)
            {
                g_hFirstRockClawGate = new DynamicDetour(primary, CallConv_THISCALL, ReturnType_Void, ThisPointer_CBaseEntity);
                enabled = g_hFirstRockClawGate != null && g_hFirstRockClawGate.Enable(Hook_Pre, TR_DetourFirstRockClaw);
                if (!enabled) delete g_hFirstRockClawGate;
            }
        }
    }
    TR_Log("严格首石拳击入口：%s", enabled ? "已启用原生 CTankClaw::PrimaryAttack 拦截" : "签名或扩展不可用，使用输入、动画和命中保护");
}

void TR_ResetRockTelemetry(int tank)
{
    g_iTankRockLastRef[tank] = INVALID_ENT_REFERENCE;
    g_fTankRockReleaseClock[tank] = 0.0;
    g_iTankRockThrowActivity[tank] = 0;
}

// ---------------------------------------------------------------- 工具
int TR_SafeOwner(int entity)
{
    if (entity <= MaxClients || !IsValidEntity(entity)) return 0;
    if (HasEntProp(entity, Prop_Send, "m_hOwnerEntity")) return GetEntPropEnt(entity, Prop_Send, "m_hOwnerEntity");
    if (HasEntProp(entity, Prop_Data, "m_hOwnerEntity")) return GetEntPropEnt(entity, Prop_Data, "m_hOwnerEntity");
    return 0;
}

int TR_TankFromThrowAbility(int ability)
{
    if (ability <= 0 || !IsValidEntity(ability))
    {
        return 0;
    }
    int owner = TR_SafeOwner(ability);
    if (owner > 0 && owner <= MaxClients && TR_IsTankClient(owner))
    {
        return owner;
    }
    // Concurrent Tanks can choose the same animation. Match the ability entity itself.
    for (int i = 1; i <= MaxClients; i++)
    {
        if (TR_IsTankClient(i) && HasEntProp(i, Prop_Send, "m_customAbility")
            && GetEntPropEnt(i, Prop_Send, "m_customAbility") == ability) return i;
    }
    return 0;
}

void TR_FormatRockSummary(char[] buffer, int maxlen)
{
    Format(buffer, maxlen,
        "投石激活：%d 次\n被总开关/间隔拦截：%d / %d 次\n激活后无释放：%d 次\n首石请求：%d 次\n真实释放：%d 次\n首石成功/超时：%d / %d\n拳击挥击：%d 次，命中：%d 次",
        g_iStatThrowActivate, g_iStatThrowGateBlocked, g_iStatDelayedBlocked,
        g_iStatActivationNoRelease, g_iStatOverrides, g_iStatReleases,
        g_iStatFirstRockSuccess, g_iStatFirstRockTimeout, g_iStatSwings, g_iStatHits);
}

// ---------------------------------------------------------------- 首石窗口
void TR_StartRockWindow(int tank, const char[] reason)
{
    if (!TR_IsAiTank(tank) || !TR_FeatureEnabled())
    {
        return;
    }
    if (!TR_FirstRockEnabled() || g_bTankStrategyOff[tank])
    {
        return;
    }
    g_bTankRockWindow[tank] = true;
    float now = GetGameTime();
    g_fTankRockDeadline[tank] = now + g_cvFirstRockTimeout.FloatValue;
    g_fTankRockHardDeadline[tank] = g_fTankRockDeadline[tank] + TR_FIRST_ROCK_TERRAIN_GRACE;
    g_fTankRockPausedAt[tank] = 0.0;
    g_iTankRockAttempts[tank] = 0;
    TR_LogEvent("首石窗口开启 客户端#%d（%s）超时 %.1f 秒", tank, reason,
        g_cvFirstRockTimeout.FloatValue);
}

void TR_CloseRockWindow(int tank, bool success, const char[] reason)
{
    if (!g_bTankRockWindow[tank])
    {
        return;
    }
    g_bTankRockWindow[tank] = false;
    g_fTankRockDeadline[tank] = 0.0;
    g_fTankRockPausedAt[tank] = 0.0;
    g_fTankTraversalPunchUntil[tank] = 0.0;
    if (success)
    {
        g_iStatFirstRockSuccess++;
    }
    TR_LogEvent("首石窗口关闭 客户端#%d 成功=%s（%s）", tank, success ? "是" : "否", reason);
}

bool TR_FirstRockEnabled()
{
    return TR_FeatureEnabled() && !g_bRoundEnded && g_cvFirstRock.BoolValue && g_cvRockEnabled.BoolValue
        && (g_cvAiAbilities == null || g_cvAiAbilities.BoolValue);
}

void TR_NoteFirstRockClimb(int tank, float now)
{
    if (TR_FirstRockEnabled() && !g_bTankFirstContact[tank] && !g_bTankStrategyOff[tank])
        g_fTankTraversalPunchUntil[tank] = now + TR_FIRST_ROCK_EXIT_CLAW_GRACE;
}

bool TR_FirstRockClawGuard(int tank, float now)
{
    if (!TR_FirstRockEnabled() || !TR_IsAiTank(tank)) return false;
    if (!g_bTankTracked[tank] || g_iTankUserId[tank] != GetClientUserId(tank)) TR_TrackTank(tank, "首个拳击检查");
    TR_UpdateFirstRockWindow(tank, now);
    if (g_bTankStrategyOff[tank]) return false;
    if (g_bTankRockWindow[tank]) return true;
    if (g_bTankFirstContact[tank]) return false;
    if (TR_RockTraversalBusy(tank, now)) TR_NoteFirstRockClimb(tank, now);
    return now < g_fTankTraversalPunchUntil[tank];
}

MRESReturn TR_DetourFirstRockClaw(int claw)
{
    if (!TR_FirstRockEnabled() || !TR_IsClawDamageEntity(claw) || !HasEntProp(claw, Prop_Data, "m_hOwner")) return MRES_Ignored;
    int tank = GetEntPropEnt(claw, Prop_Data, "m_hOwner");
    if (!TR_IsAiTank(tank)) return MRES_Ignored;
    if (!g_bTankTracked[tank] || g_iTankUserId[tank] != GetClientUserId(tank)) TR_TrackTank(tank, "首个原生拳击请求");
    if (!g_bTankFirstContact[tank]) TR_UpdateFirstContact(tank, true);
    if (!TR_FirstRockClawGuard(tank, GetGameTime())) return MRES_Ignored;
    TR_LogBlockedFirstRockClaw(tank, "武器入口");
    return MRES_Supercede;
}

void TR_LogBlockedFirstRockClaw(int tank, const char[] source)
{
    if (g_bTankTraversalPunchLogged[tank]) return;
    g_bTankTraversalPunchLogged[tank] = true;
    TR_Log("严格首石禁拳：Tank#%d userid=%d 入口=%s", tank, GetClientUserId(tank), source);
}

bool TR_IsClawDamageEntity(int entity)
{
    if (entity <= MaxClients || !IsValidEntity(entity)) return false;
    char classname[32];
    return GetEntityClassname(entity, classname, sizeof(classname))
        && (StrEqual(classname, "weapon_tank_claw") || StrEqual(classname, "tank_claw"));
}

bool TR_RockTraversalBusy(int tank, float now)
{
    if (now < g_fTankRockTraverseUntil[tank] || GetEntityMoveType(tank) == MOVETYPE_LADDER
        || (GetEntityFlags(tank) & FL_ONGROUND) == 0) return true;
    if (HasEntProp(tank, Prop_Send, "m_hGroundEntity") && GetEntPropEnt(tank, Prop_Send, "m_hGroundEntity") < 0) return true;
    if (GetFeatureStatus(FeatureType_Native, "PlayerAnimState.FromPlayer") == FeatureStatus_Available
        && GetFeatureStatus(FeatureType_Native, "PlayerAnimState.GetMainActivity") == FeatureStatus_Available)
    {
        PlayerAnimState anim = PlayerAnimState.FromPlayer(tank);
        if (view_as<Address>(anim) != Address_Null)
        {
            switch (anim.GetMainActivity())
            {
                // Same-build Tank locomotion climbing exclusions, plus jump activities.
                case 658, 659, 660, 661, 662, 663, 664, 665, 710, 711, 719, 720, 722, 724, 729, 731, 734, 736: return true;
            }
        }
    }
    return false;
}

public Action L4D_OnDoAnimationEvent(int client, int &event, int &variantParam)
{
    if (event == view_as<int>(PLAYERANIMEVENT_HAND_ATTACK) || event == view_as<int>(PLAYERANIMEVENT_HAND_LOW_ATTACK))
    {
        if (TR_FirstRockClawGuard(client, GetGameTime()))
        {
            TR_LogBlockedFirstRockClaw(client, "出拳动画");
            return Plugin_Handled;
        }
    }
    return Plugin_Continue;
}

public void L4D_OnDoAnimationEvent_Post(int client, int event, int variantParam)
{
    if (!TR_FeatureEnabled() || !TR_IsAiTank(client)) return;
    PlayerAnimEvent_t activityEvent = view_as<PlayerAnimEvent_t>(event);
    if (activityEvent == PLAYERANIMEVENT_ROCK_THROW)
    {
        // CThrow emits this only after passing its initial eligibility gates.
        g_fTankPendingActivation[client] = GetGameTime();
        g_iTankRockThrowActivity[client] = 0;
        return;
    }
    if (activityEvent == PLAYERANIMEVENT_JUMP || activityEvent == PLAYERANIMEVENT_TANK_CLIMB || activityEvent == PLAYERANIMEVENT_LAND)
    {
        if (!TR_FirstRockEnabled()) return;
        if (!g_bTankTracked[client] || g_iTankUserId[client] != GetClientUserId(client))
            TR_TrackTank(client, "首个攀跳事件");
        float now = GetGameTime();
        g_fTankRockTraverseUntil[client] = now + 0.20;
        TR_NoteFirstRockClimb(client, now);
    }
}

void TR_UpdateFirstRockWindow(int tank, float now)
{
    if (!g_bTankRockWindow[tank]) return;
    if (!TR_FirstRockEnabled() || g_bTankStrategyOff[tank])
    {
        TR_CloseRockWindow(tank, false, "开关或该 Tank 策略关闭");
        return;
    }
    float pending = g_fTankPendingActivation[tank];
    if (pending > 0.0 && now - pending <= TR_FIRST_ROCK_RELEASE_GRACE) return;

    if (TR_RockTraversalBusy(tank, now))
    {
        if (g_fTankRockPausedAt[tank] == 0.0)
        {
            g_fTankRockPausedAt[tank] = now;
            TR_LogEvent("首石等待落地：Tank#%d userid=%d", tank, GetClientUserId(tank));
        }
        return;
    }
    if (g_fTankRockPausedAt[tank] > 0.0)
    {
        g_fTankRockDeadline[tank] += now - g_fTankRockPausedAt[tank];
        g_fTankRockPausedAt[tank] = 0.0;
        // A long climb must still leave an opportunity to aim and throw on stable ground.
        float landingDeadline = now + g_cvFirstRockTimeout.FloatValue;
        if (g_fTankRockDeadline[tank] < landingDeadline) g_fTankRockDeadline[tank] = landingDeadline;
        if (g_fTankRockHardDeadline[tank] < landingDeadline) g_fTankRockHardDeadline[tank] = landingDeadline;
        TR_LogEvent("首石落地恢复：Tank#%d userid=%d", tank, GetClientUserId(tank));
    }
    if (now >= g_fTankRockHardDeadline[tank] || now >= g_fTankRockDeadline[tank])
    {
        TR_Log("严格首石超时：Tank#%d userid=%d 请求次数=%d，恢复近战", tank, GetClientUserId(tank), g_iTankRockAttempts[tank]);
        TR_CloseRockWindow(tank, false, "等待至上限仍未确认原生离手");
        g_iStatFirstRockTimeout++;
        return;
    }
    if (g_iTankRockAttempts[tank] >= g_cvFirstRockAttempts.IntValue)
        TR_CloseRockWindow(tank, false, "原版投石尝试已用尽");
}

void TR_UpdateRockWindows()
{
    float now = GetGameTime();
    for (int i = 1; i <= MaxClients; i++)
    {
        // 投石激活后长时间没有真实释放：记录可观测的失败信息（不修改引擎行为）。
        if (g_fTankPendingActivation[i] > 0.0 && now - g_fTankPendingActivation[i] > TR_FIRST_ROCK_RELEASE_GRACE)
        {
            g_fTankPendingActivation[i] = 0.0;
            g_iStatActivationNoRelease++;
            TR_LogEvent("投石可观测失败：Tank#%d 激活后 %.2f 秒内没有离手后回调", i, TR_FIRST_ROCK_RELEASE_GRACE);
        }

        if (!g_bTankRockWindow[i])
        {
            continue;
        }
        if (!TR_IsTankClient(i))
        {
            g_bTankRockWindow[i] = false;
            continue;
        }
        TR_UpdateFirstRockWindow(i, now);
    }
}

public Action OnPlayerRunCmd(int client, int &buttons, int &impulse, float move[3], float angles[3],
    int &weapon, int &subtype, int &cmdnum, int &tickcount, int &seed, int mouse[2])
{
    if (!TR_FirstRockEnabled() || !TR_IsAiTank(client)) return Plugin_Continue;
    if (!g_bTankTracked[client] || g_iTankUserId[client] != GetClientUserId(client)) TR_TrackTank(client, "首个 Tank 输入帧");
    if (!g_bTankFirstContact[client]) TR_UpdateFirstContact(client, true);
    if ((buttons & IN_ATTACK) == 0) return Plugin_Continue;
    if (!TR_FirstRockClawGuard(client, GetGameTime())) return Plugin_Continue;

    buttons &= ~IN_ATTACK;
    TR_LogBlockedFirstRockClaw(client, "主攻击输入");
    return Plugin_Changed;
}

// 首次接敌检测（周期扫描调用，避免每帧 trace）
void TR_UpdateFirstContact(int tank, bool earlyCommand = false)
{
    if (!TR_IsAiTank(tank) || g_bTankFirstContact[tank])
    {
        return;
    }
    float interval = g_cvTargetScan.FloatValue;
    if (earlyCommand && interval > 0.12) interval = 0.12;
    if (interval < 0.1)
    {
        interval = 0.1;
    }
    float now = GetGameTime();
    if (now - g_fTankLastContactScan[tank] < interval)
    {
        return;
    }
    g_fTankLastContactScan[tank] = now;

    int survivor = TR_FindVisibleSurvivor(tank);
    if (survivor <= 0)
    {
        return;
    }
    g_bTankFirstContact[tank] = true;
    g_fTankFirstContactTime[tank] = now;
    TR_LogEvent("首次接敌 客户端#%d → 幸存者#%d", tank, survivor);
    TR_StartRockWindow(tank, "首次接敌");
}

// ---------------------------------------------------------------- 目标策略
int TR_SelectStrategyTarget(int tank)
{
    TR_TargetMode mode = view_as<TR_TargetMode>(g_cvTargetMode.IntValue);
    if (mode == TRT_NATIVE)
    {
        return 0;
    }
    if (mode == TRT_FIXED)
    {
        int fixed = g_cvTargetFixed.IntValue;
        if (fixed > 0 && fixed <= MaxClients && TR_IsSurvivor(fixed))
        {
            return fixed;
        }
        return 0;
    }

    float tankPos[3];
    GetClientAbsOrigin(tank, tankPos);
    int best = 0;
    float bestValue = 0.0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!TR_IsSurvivor(i))
        {
            continue;
        }
        float dist = 0.0;
        if (mode == TRT_NEAREST || mode == TRT_FARTHEST)
        {
            float target[3];
            GetClientAbsOrigin(i, target);
            dist = GetVectorDistance(tankPos, target);
        }
        else if (mode == TRT_LOWHP)
        {
            dist = float(GetClientHealth(i));
        }

        if (best == 0)
        {
            best = i;
            bestValue = dist;
            continue;
        }
        bool better = false;
        switch (mode)
        {
            case TRT_NEAREST:  better = dist < bestValue;
            case TRT_FARTHEST: better = dist > bestValue;
            case TRT_LOWHP:    better = dist < bestValue;
        }
        if (better)
        {
            best = i;
            bestValue = dist;
        }
    }
    return best;
}

public Action L4D2_OnChooseVictim(int specialInfected, int &curTarget)
{
    if (specialInfected <= 0 || specialInfected > MaxClients || !TR_FeatureEnabled())
    {
        return Plugin_Continue;
    }
    if (!TR_IsTankClient(specialInfected))
    {
        return Plugin_Continue;
    }
    if (!TR_IsAiTank(specialInfected))
    {
        return Plugin_Continue;
    }

    g_iStatChooseVictim++;
    int previousTarget = curTarget;
    g_iTankChosenTarget[specialInfected] = curTarget;
    float now = GetGameTime();
    g_fTankChosenTime[specialInfected] = now;
    bool logDecision = now >= g_fTankNextVictimTrace[specialInfected];
    if (logDecision)
    {
        g_fTankNextVictimTrace[specialInfected] = now + 1.0;
        TR_LogEvent("目标选择 forward：Tank#%d 当前目标=%d 策略=%d", specialInfected, curTarget, g_cvTargetMode.IntValue);
    }

    int desired = TR_SelectStrategyTarget(specialInfected);
    if (desired <= 0 || desired == curTarget)
    {
        return Plugin_Continue;
    }
    curTarget = desired;
    g_iTankChosenTarget[specialInfected] = desired;
    if (logDecision)
    {
        TR_LogEvent("目标策略覆盖：Tank#%d 目标 %d → %d（插件策略）", specialInfected, previousTarget, desired);
    }
    return Plugin_Changed;
}

// ---------------------------------------------------------------- 投石激活门
public Action L4D_OnCThrowActivate(int ability)
{
    if (!TR_FeatureEnabled())
    {
        return Plugin_Continue;
    }
    int tank = TR_TankFromThrowAbility(ability);
    g_iStatThrowActivate++;

    if (!g_cvRockEnabled.BoolValue)
    {
        g_iStatThrowGateBlocked++;
        if (tank > 0)
        {
            g_iTankBlockedCount[tank]++;
        }
        TR_LogEvent("投石被拦截：总开关关闭（ability=%d tank=%d）", ability, tank);
        return Plugin_Handled;
    }

    if (tank > 0)
    {
        float minDelay = g_cvRockMinDelay.FloatValue;
        if (minDelay > 0.0 && g_fTankLastRelease[tank] > 0.0)
        {
            float since = GetGameTime() - g_fTankLastRelease[tank];
            if (since < minDelay)
            {
                g_iStatDelayedBlocked++;
                g_iTankBlockedCount[tank]++;
                TR_LogEvent("投石被拦截：插件最小间隔（Tank#%d 距上次 %.1f 秒 < %.1f 秒）",
                    tank, since, minDelay);
                return Plugin_Handled;
            }
        }
        float now = GetGameTime();
        g_fTankLastThrowAttempt[tank] = now;
        if (TR_FirstRockEnabled() && TR_IsAiTank(tank))
        {
            if (!g_bTankTracked[tank] || g_iTankUserId[tank] != GetClientUserId(tank)) TR_TrackTank(tank, "首次投石检查");
            if (!g_bTankFirstContact[tank]) TR_UpdateFirstContact(tank, true);
            TR_UpdateFirstRockWindow(tank, now);
            if (g_bTankRockWindow[tank])
            {
                g_iTankRockAttempts[tank]++;
                g_iTankOverrides[tank]++;
                g_iStatOverrides++;
                TR_LogEvent("首石原版激活尝试：Tank#%d userid=%d ability=%d 次数=%d",
                    tank, GetClientUserId(tank), ability, g_iTankRockAttempts[tank]);
            }
        }
    }

    TR_LogEvent("投石激活：ability=%d tank=%d", ability, tank);
    return Plugin_Continue;
}

public void L4D_OnCThrowActivate_Post(int ability)
{
    if (!TR_FeatureEnabled()) return;
    int tank = TR_TankFromThrowAbility(ability);
    if (!TR_IsAiTank(tank)) return;
    float now = GetGameTime();
    if (g_fTankPendingActivation[tank] > 0.0 && now - g_fTankPendingActivation[tank] <= GetTickInterval())
    {
        TR_LogEvent("投石能力发出原版动画请求：Tank#%d userid=%d ability=%d", tank, GetClientUserId(tank), ability);
    }
    else
    {
        TR_LogEvent("投石能力后回调未观察到原版动画请求：Tank#%d userid=%d ability=%d", tank, GetClientUserId(tank), ability);
    }
}

// ---------------------------------------------------------------- 投石动作与释放
public Action L4D2_OnSelectTankAttackPre(int client, int &sequence)
{
    if (client <= 0 || client > MaxClients || !TR_FeatureEnabled())
    {
        return Plugin_Continue;
    }
    if (!TR_IsTankClient(client))
    {
        return Plugin_Continue;
    }

    g_iTankLastActivity[client] = sequence;
    g_fTankLastActivityTime[client] = GetGameTime();

    if (sequence == TR_ACT_HULK_THROW || sequence == TR_ACT_TANK_OVERHEAD_THROW)
    {
        g_iTankRockThrowActivity[client] = sequence;
    }
    if ((sequence == TR_ACT_HULK_THROW || sequence == TR_ACT_TANK_OVERHEAD_THROW)
        && g_fTankLastActivityTime[client] >= g_fTankNextThrowTrace[client])
    {
        g_fTankNextThrowTrace[client] = g_fTankLastActivityTime[client] + 1.0;
        TR_LogEvent("动作选择：Tank#%d 选择投石动作 %d", client, sequence);
    }
    return Plugin_Continue;
}

void TR_LogNativeRockReleasePose(int tank, int rock, const float origin[3])
{
    if (!TR_IsTankClient(tank)) return;
    int sequence = HasEntProp(tank, Prop_Send, "m_nSequence") ? GetEntProp(tank, Prop_Send, "m_nSequence") : -1;
    float cycle = HasEntProp(tank, Prop_Send, "m_flCycle") ? GetEntPropFloat(tank, Prop_Send, "m_flCycle") : -1.0;
    float hand[3], angles[3];
    int attachment = LookupEntityAttachment(tank, "debris");
    float handOffset = attachment > 0 && GetEntityAttachment(tank, attachment, hand, angles)
        ? GetVectorDistance(origin, hand) : -1.0;
    TR_LogEvent("原版投石帧：Tank=%d userid=%d rock=%d activity=%d seq=%d cycle=%.3f 离手点与附件距离=%.1f",
        tank, GetClientUserId(tank), rock, g_iTankRockThrowActivity[tank], sequence, cycle, handOffset);
}

public Action L4D_TankRock_OnRelease(int tank, int rock, float vecPos[3], float vecAng[3],
    float vecVel[3], float vecRot[3])
{
    if (!TR_FeatureEnabled())
    {
        return Plugin_Continue;
    }

    int who = 0;
    float dist = TR_NearestSurvivorDistance(vecPos, who);
    float speed = SquareRoot(vecVel[0] * vecVel[0] + vecVel[1] * vecVel[1] + vecVel[2] * vecVel[2]);
    TR_LogEvent("投石释放前：Tank=%d rock=%d pos=(%.1f, %.1f, %.1f) 速度=%.0f 原版向量=(%.1f,%.1f,%.1f) 最近幸存者=%d 距离=%.0f",
        tank, rock, vecPos[0], vecPos[1], vecPos[2], speed, vecVel[0], vecVel[1], vecVel[2], who, dist);

    TR_LogNativeRockReleasePose(tank, rock, vecPos);
    if (!TR_IsAiTank(tank)) return Plugin_Continue;
    float scale = g_cvRockForceScale.FloatValue;
    if (scale > 0.0 && FloatAbs(scale - 1.0) > 0.001)
    {
        ScaleVector(vecVel, scale);
        TR_LogEvent("投石力度倍率：%.2f（新速度约 %.0f）", scale, GetVectorLength(vecVel));
        return Plugin_Changed;
    }
    return Plugin_Continue;
}

public void L4D_TankRock_OnRelease_Post(int tank, int rock, const float vecPos[3],
    const float vecAng[3], const float vecVel[3], const float vecRot[3])
{
    if (!TR_FeatureEnabled())
    {
        return;
    }
    char classname[32], model[PLATFORM_MAX_PATH];
    bool liveRock = rock > MaxClients && IsValidEntity(rock)
        && GetEntityClassname(rock, classname, sizeof(classname)) && StrEqual(classname, "tank_rock");
    if (TR_IsTankClient(tank))
    {
        g_fTankPendingActivation[tank] = 0.0;
        if (liveRock)
        {
            g_iTankReleaseCount[tank]++;
            g_fTankLastRelease[tank] = GetGameTime();
            g_iTankRockLastRef[tank] = EntIndexToEntRef(rock);
            g_fTankRockReleaseClock[tank] = GetEngineTime();
            if (g_bTankRockWindow[tank])
            {
                TR_Log("严格首石离手：Tank#%d userid=%d rock=%d，解除禁拳", tank, GetClientUserId(tank), rock);
                TR_CloseRockWindow(tank, true, "原生离手并确认石头实体");
            }
        }
    }
    if (!liveRock)
    {
        TR_Log("投石释放后未确认石头实体：Tank=%d rock=%d class=%s", tank, rock, classname);
        return;
    }
    g_iStatReleases++;
    if (HasEntProp(rock, Prop_Data, "m_ModelName")) GetEntPropString(rock, Prop_Data, "m_ModelName", model, sizeof(model));
    float speed = SquareRoot(vecVel[0] * vecVel[0] + vecVel[1] * vecVel[1] + vecVel[2] * vecVel[2]);
    TR_LogEvent("投石释放后：Tank=%d rock=%d 最终速度=%.0f 向量=(%.1f,%.1f,%.1f) entref=%d model=%s",
        tank, rock, speed, vecVel[0], vecVel[1], vecVel[2], EntIndexToEntRef(rock), model);
}

public void L4D_TankRock_OnDetonate(int tank, int rock)
{
    if (!TR_FeatureEnabled())
    {
        return;
    }
    if (tank > 0 && tank <= MaxClients && rock > MaxClients && IsValidEntity(rock)
        && EntIndexToEntRef(rock) == g_iTankRockLastRef[tank])
    {
        float elapsed = GetEngineTime() - g_fTankRockReleaseClock[tank];
        if (elapsed >= 0.0 && elapsed <= 0.10)
            TR_Log("投石离手后立即碰撞：Tank=%d rock=%d entref=%d 耗时=%.3f 秒",
                tank, rock, g_iTankRockLastRef[tank], elapsed);
        g_iTankRockLastRef[tank] = INVALID_ENT_REFERENCE;
    }
    TR_LogEvent("投石爆炸/落地：Tank=%d rock=%d", tank, rock);
}

// ---------------------------------------------------------------- 拳击事件
public void L4D_TankClaw_DoSwing_Pre(int tank, int claw)
{
    if (!TR_FeatureEnabled() || tank <= 0 || tank > MaxClients)
    {
        return;
    }
    g_iStatSwings++;
    if (TR_IsTankClient(tank))
    {
        g_iTankSwingCount[tank]++;
        g_fTankLastSwing[tank] = GetGameTime();
    }
    TR_LogEvent("拳击挥击开始：Tank#%d claw=%d 动作=%d", tank, claw, g_iTankLastActivity[tank]);
}

public void L4D_TankClaw_DoSwing_Post(int tank, int claw)
{
    if (!TR_FeatureEnabled())
    {
        return;
    }
    TR_LogEvent("拳击挥击结束：Tank#%d claw=%d", tank, claw);
}

public Action L4D_TankClaw_OnPlayerHit_Pre(int tank, int claw, int player)
{
    if (!TR_FeatureEnabled())
    {
        return Plugin_Continue;
    }
    if (TR_IsSurvivor(player) && TR_FirstRockClawGuard(tank, GetGameTime()))
    {
        TR_LogBlockedFirstRockClaw(tank, "拳击击退");
        return Plugin_Handled;
    }
    g_iStatHits++;
    if (tank > 0 && tank <= MaxClients)
    {
        g_iTankHitCount[tank]++;
    }
    TR_LogEvent("拳击命中：Tank#%d → 玩家#%d", tank, player);

    if (g_cvClawBlock.BoolValue)
    {
        TR_LogEvent("拳击命中判定被阻挡（仅阻挡击退，不修改伤害）Tank#%d 玩家#%d", tank, player);
        return Plugin_Handled;
    }
    return Plugin_Continue;
}

public void L4D_TankClaw_OnPlayerHit_Post(int tank, int claw, int player)
{
    if (!TR_FeatureEnabled())
    {
        return;
    }
    TR_LogEvent("拳击命中后：Tank#%d 玩家#%d", tank, player);
}

// ---------------------------------------------------------------- 统计
void TR_ResetRockStats()
{
    g_iStatThrowActivate = 0;
    g_iStatThrowGateBlocked = 0;
    g_iStatDelayedBlocked = 0;
    g_iStatReleases = 0;
    g_iStatSwings = 0;
    g_iStatHits = 0;
    g_iStatOverrides = 0;
    g_iStatFirstRockSuccess = 0;
    g_iStatFirstRockTimeout = 0;
    g_iStatChooseVictim = 0;
}
