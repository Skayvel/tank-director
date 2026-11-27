// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

// Tank Run v2.3 骗拳处死层（Bait-Punch Slay）
// 精确规则：
//   - 仅作用于原版 AI Tank 与容量补位 Tank
//   - 出拳时按双方碰撞体边缘间距 0~2 个站立生还者标准身位（0..64u）取样，隔墙资格仍有效
//   - 严格因果确证：
//     * 原版路径通过只读 DHooks detour 观测 TankBehavior::Update / TankBehavior::OnContact /
//       NextBotPlayer::PressMeleeButton（严格 Windows + 签名 + 偏移 + 指令字节校验），
//       仅对已验证的真实朝真人生还者出拳提供成功判定；彻底移除前向/最近真人兜底和过期 ChooseVictim 缓存猜测；
//       任何道具/门接触出拳（不依赖固定类名白名单，也不受道具伤害被拦截影响）一律排除；无可靠证据时宁不处死，绝不误杀。
//     * 容量路径使用补位 Tank 真实攻击目标 attackTarget（仅当其为活着的真人生还者且边缘距 <= 64u）。
//   - 严格区分动画请求与实际攻击结算（原版须经过 DoSwing/GroundPound 结算，补位须经过 attackHit 结算）
//   - 打中、掉血或倒地仍触发处死；仅当本次拳击造成任意生还者（含 Bot）死亡时才不处死
//   - 按 Tank 生命代次（lifeGen）、攻击代次（attackGen）与受害人实例严格加固同步 damage guard，防同调用栈串台误算

#if defined _tankrun_bait_punch_included
 #endinput
#endif
#define _tankrun_bait_punch_included

#define TR_SURVIVOR_BODY_WIDTH      32.0
#define TR_BAIT_PUNCH_MAX_EDGE_DIST (TR_SURVIVOR_BODY_WIDTH * 2.0) // 64.0 units
#define TR_BAIT_REPLICA_SLOTS       32

#define TR_BAIT_CAUSE_NONE              0
#define TR_BAIT_CAUSE_SURVIVOR_UPDATE   1
#define TR_BAIT_CAUSE_SURVIVOR_CONTACT  2
#define TR_BAIT_CAUSE_PROP              3
#define TR_BAIT_CAUSE_NON_HUMAN         4

#define TR_BAIT_CONTACT_NONE            0
#define TR_BAIT_CONTACT_HUMAN           1
#define TR_BAIT_CONTACT_PROP            2
#define TR_BAIT_CONTACT_OTHER           3

#define TR_BAIT_EXPECTED_OFF_INPUT      0x4270
#define TR_BAIT_EXPECTED_OFF_BODY       0x4330
#define TR_BAIT_EXPECTED_OFF_VISION     0x433C
#define TR_BAIT_EXPECTED_OFF_THREAT     0x148
#define TR_BAIT_EXPECTED_OFF_EXPIRE     0x6C
#define TR_BAIT_EXPECTED_OFF_REPLY      0x78
#define TR_BAIT_MELEE_TOKEN_TTL         0.35
#define TR_BAIT_CONTACT_AIM_TTL         1.25

enum struct TR_NativeBaitState
{
    int lifeGen;
    int attackGen;
    int lastSlayedAttackGen;
    int intendedHumanUid;
    int causeKind;
    bool pending;
    bool qualified;
    bool isGroundPound;
    bool settled;
    bool hitSurvivor;
    bool triggeredByProp;
    bool killedAnySurvivor;
    float swingStartTime;
    float firstSettleTime;
    Handle evalTimer;
}

enum struct TR_ReplicaBaitState
{
    int lifeGen;
    int attackGen;
    int lastSlayedAttackGen;
    int intendedHumanUid;
    bool pending;
    bool qualified;
    bool settled;
    bool killedAnySurvivor;
}

int g_iNextTankLifeGen;
int g_iNextAttackGen;

int g_iTankLifeGen[MAXPLAYERS + 1];
TR_NativeBaitState g_NativeBait[MAXPLAYERS + 1];
TR_ReplicaBaitState g_ReplicaBait[TR_BAIT_REPLICA_SLOTS];

// 原版 AI Tank 出拳因果只读 DHooks 观测状态
DynamicDetour g_hBaitBehaviorUpdateDetour;
DynamicDetour g_hBaitBehaviorContactDetour;
DynamicDetour g_hBaitPressMeleeDetour;
bool g_bBaitNativeCauseReady;
bool g_bBaitPendingOsRetry;
int  g_iBaitOffPlayerInput = -1;
int  g_iBaitOffBody = -1;
int  g_iBaitOffVision = -1;
int  g_iBaitOffPrimaryThreat = -1;
int  g_iBaitOffLookAtExpire = -1;
int  g_iBaitOffLookAtReply = -1;
char g_sBaitNativeStatus[160] = "未初始化";

// 同步调用栈上下文（TankBehavior::Update / TankBehavior::OnContact Pre..Post）
int g_iBaitInUpdateTank;
int g_iBaitInUpdateTankUid;
int g_iBaitInContactTank;
int g_iBaitInContactTankUid;
int g_iBaitInContactOther = -1;
int g_iBaitContactPreExpireBits;
int g_iBaitContactPreReply;

// 仅当 TankBehavior::OnContact 实际装填了 PlayerBody::AimHeadTowards(..., reply) 时才记录的待触发接触目标
bool  g_bBaitArmedContactValid[MAXPLAYERS + 1];
int   g_iBaitArmedContactKind[MAXPLAYERS + 1];
int   g_iBaitArmedContactTankUid[MAXPLAYERS + 1];
int   g_iBaitArmedContactHumanUid[MAXPLAYERS + 1];
float g_fBaitArmedContactTime[MAXPLAYERS + 1];

// 最近一次 PressMeleeButton 证明的实时出拳因果令牌（单次消费，绝不跨拳复用）
int   g_iBaitMeleeCause[MAXPLAYERS + 1];
int   g_iBaitMeleeTankUid[MAXPLAYERS + 1];
int   g_iBaitMeleeTargetUid[MAXPLAYERS + 1];
float g_fBaitMeleeTime[MAXPLAYERS + 1];
bool  g_bBaitMeleeConsumed[MAXPLAYERS + 1];

// 同步结算上下文：严格绑定 (lifeGen, attackGen, target, targetUid) 防同调用栈其他受害人串台
int  g_iActiveNativeClawTank;
int  g_iActiveNativeClawLifeGen;
int  g_iActiveNativeClawAttackGen;

int  g_iActiveReplicaClawSlot = -1;
int  g_iActiveReplicaClawLifeGen;
int  g_iActiveReplicaClawAttackGen;
int  g_iActiveReplicaClawTarget;
int  g_iActiveReplicaClawTargetUid;
bool g_bActiveReplicaTargetKilledByOther;

int g_iStatBaitPunchSlaysNative;
int g_iStatBaitPunchSlaysReplica;
int g_iStatBaitPunchBlockedDeath;
int g_iStatBaitPunchExcludedObstacle;

bool TR_BaitPunchEnabled()
{
    return TR_FeatureEnabled() && !g_bRoundEnded && g_cvBaitPunchSlay != null && g_cvBaitPunchSlay.BoolValue;
}

stock bool TR_Bait_IsNativeCauseReady()
{
    return g_bBaitNativeCauseReady;
}

// ---------------------------------------------------------------- 原版 AI Tank 出拳因果只读 Detour 安装与卸载
void TR_Bait_ClearMeleeToken(int tank)
{
    if (tank < 1 || tank > MaxClients)
    {
        return;
    }
    g_iBaitMeleeCause[tank] = TR_BAIT_CAUSE_NONE;
    g_iBaitMeleeTankUid[tank] = 0;
    g_iBaitMeleeTargetUid[tank] = 0;
    g_fBaitMeleeTime[tank] = 0.0;
    g_bBaitMeleeConsumed[tank] = true;

    g_bBaitArmedContactValid[tank] = false;
    g_iBaitArmedContactKind[tank] = TR_BAIT_CONTACT_NONE;
    g_iBaitArmedContactTankUid[tank] = 0;
    g_iBaitArmedContactHumanUid[tank] = 0;
    g_fBaitArmedContactTime[tank] = 0.0;
}

void TR_Bait_UninstallNativeCauseDetours()
{
    g_bBaitNativeCauseReady = false;
    g_iBaitOffPlayerInput = -1;
    g_iBaitOffBody = -1;
    g_iBaitOffVision = -1;
    g_iBaitOffPrimaryThreat = -1;
    g_iBaitOffLookAtExpire = -1;
    g_iBaitOffLookAtReply = -1;
    g_iBaitInUpdateTank = 0;
    g_iBaitInUpdateTankUid = 0;
    g_iBaitInContactTank = 0;
    g_iBaitInContactTankUid = 0;
    g_iBaitInContactOther = -1;
    g_iBaitContactPreExpireBits = 0;
    g_iBaitContactPreReply = 0;

    if (g_hBaitBehaviorUpdateDetour != null)
    {
        g_hBaitBehaviorUpdateDetour.Disable(Hook_Pre, TR_Bait_DetourBehaviorUpdatePre);
        g_hBaitBehaviorUpdateDetour.Disable(Hook_Post, TR_Bait_DetourBehaviorUpdatePost);
        delete g_hBaitBehaviorUpdateDetour;
        g_hBaitBehaviorUpdateDetour = null;
    }
    if (g_hBaitBehaviorContactDetour != null)
    {
        g_hBaitBehaviorContactDetour.Disable(Hook_Pre, TR_Bait_DetourBehaviorContactPre);
        g_hBaitBehaviorContactDetour.Disable(Hook_Post, TR_Bait_DetourBehaviorContactPost);
        delete g_hBaitBehaviorContactDetour;
        g_hBaitBehaviorContactDetour = null;
    }
    if (g_hBaitPressMeleeDetour != null)
    {
        g_hBaitPressMeleeDetour.Disable(Hook_Pre, TR_Bait_DetourPressMeleePre);
        delete g_hBaitPressMeleeDetour;
        g_hBaitPressMeleeDetour = null;
    }
}

void TR_Bait_InstallNativeCauseDetours()
{
    TR_Bait_UninstallNativeCauseDetours();
    g_bBaitPendingOsRetry = false;

    if (GetFeatureStatus(FeatureType_Native, "L4D_GetServerOS") != FeatureStatus_Available)
    {
        g_bBaitPendingOsRetry = true;
        strcopy(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus), "降级关闭（L4D_GetServerOS 不可用，无可靠证据不处死）");
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }
    int serverOs = L4D_GetServerOS();
    if (serverOs != 0)
    {
        Format(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus), "降级关闭（非 Windows 平台 os=%d，无可靠证据不处死）", serverOs);
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }
    if (GetFeatureStatus(FeatureType_Native, "DynamicDetour.DynamicDetour") != FeatureStatus_Available)
    {
        strcopy(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus), "降级关闭（DHooks DynamicDetour 不可用）");
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }

    char gdPath[PLATFORM_MAX_PATH];
    BuildPath(Path_SM, gdPath, sizeof(gdPath), "gamedata/tankrun_rocktarget.txt");
    if (!FileExists(gdPath))
    {
        strcopy(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus), "降级关闭（缺少 gamedata/tankrun_rocktarget.txt）");
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }

    GameData gd = LoadGameConfigFile("tankrun_rocktarget");
    if (gd == null)
    {
        strcopy(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus), "降级关闭（gamedata/tankrun_rocktarget.txt 加载失败）");
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }

    Address addrUpdate = gd.GetMemSig("TankRun::TankBehavior::Update");
    Address addrContact = gd.GetMemSig("TankRun::TankBehavior::OnContact");
    Address addrPress = gd.GetMemSig("TankRun::NextBotPlayer::PressMeleeButton");
    int offInput = gd.GetOffset("Tank::m_pPlayerInput");
    int offBody = gd.GetOffset("Tank::m_pBodyInterface");
    int offVision = gd.GetOffset("Tank::m_pVisionInterface");
    int offThreat = gd.GetOffset("ZombieBotVision::m_hPrimaryThreat");
    int offExpire = gd.GetOffset("PlayerBody::m_lookAtExpire");
    int offReply = gd.GetOffset("PlayerBody::m_lookAtReply");
    delete gd;

    if (addrUpdate == Address_Null || addrContact == Address_Null || addrPress == Address_Null)
    {
        Format(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus),
            "降级关闭（签名缺失 upd=%d cnt=%d prs=%d）",
            addrUpdate != Address_Null ? 1 : 0, addrContact != Address_Null ? 1 : 0, addrPress != Address_Null ? 1 : 0);
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }
    if (offInput != TR_BAIT_EXPECTED_OFF_INPUT || offBody != TR_BAIT_EXPECTED_OFF_BODY
        || offVision != TR_BAIT_EXPECTED_OFF_VISION || offThreat != TR_BAIT_EXPECTED_OFF_THREAT
        || offExpire != TR_BAIT_EXPECTED_OFF_EXPIRE || offReply != TR_BAIT_EXPECTED_OFF_REPLY)
    {
        Format(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus),
            "降级关闭（偏移校验失败 in=%d body=%d vis=%d thr=%d exp=%d rep=%d）",
            offInput, offBody, offVision, offThreat, offExpire, offReply);
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }

    // 安装 detour 前执行严格入口与关键特征指令字节校验
    static const int sigUpd[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x56, 0x8B, 0x75, 0x0C };
    static const int sigUpdBot[] = { 0x8B, 0x86, 0x40, 0x40, 0x00, 0x00, 0x8B, 0x90, 0xC8, 0x00, 0x00, 0x00 };
    static const int sigCnt[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18 };
    static const int sigCntArg[] = { 0x56, 0x8B, 0x75, 0x10, 0x85, 0xF6, 0x0F, 0x84 };
    static const int sigPrs[] = { 0x55, 0x8B, 0xEC, 0x51, 0x83, 0x49, 0x04, 0x01, 0x56, 0x57, 0x8D, 0x79, 0x0C };

    int badOff = 0, expB = 0, actB = 0;
    bool bytesOk = TR_Glow_VerifyCodeBytes(addrUpdate, 0x00, sigUpd, sizeof(sigUpd), badOff, expB, actB)
        && TR_Glow_VerifyCodeBytes(addrUpdate, 0x1E, sigUpdBot, sizeof(sigUpdBot), badOff, expB, actB)
        && TR_Glow_VerifyCodeBytes(addrContact, 0x00, sigCnt, sizeof(sigCnt), badOff, expB, actB)
        && TR_Glow_VerifyCodeBytes(addrContact, 0x20, sigCntArg, sizeof(sigCntArg), badOff, expB, actB)
        && TR_Glow_VerifyCodeBytes(addrPress, 0x00, sigPrs, sizeof(sigPrs), badOff, expB, actB);

    if (!bytesOk)
    {
        Format(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus),
            "降级关闭（字节校验不符 +0x%02X 期望=0x%02X 实际=0x%02X）", badOff, expB, actB);
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }

    // 1) TankBehavior::Update(this, sret, Tank* me, float interval) [ret 0xc]
    g_hBaitBehaviorUpdateDetour = new DynamicDetour(addrUpdate, CallConv_THISCALL, ReturnType_Int, ThisPointer_Address);
    if (g_hBaitBehaviorUpdateDetour == null)
    {
        TR_Bait_UninstallNativeCauseDetours();
        strcopy(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus), "降级关闭（Update detour 创建失败）");
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }
    g_hBaitBehaviorUpdateDetour.AddParam(HookParamType_Int);
    g_hBaitBehaviorUpdateDetour.AddParam(HookParamType_CBaseEntity);
    g_hBaitBehaviorUpdateDetour.AddParam(HookParamType_Float);
    if (!g_hBaitBehaviorUpdateDetour.Enable(Hook_Pre, TR_Bait_DetourBehaviorUpdatePre)
        || !g_hBaitBehaviorUpdateDetour.Enable(Hook_Post, TR_Bait_DetourBehaviorUpdatePost))
    {
        TR_Bait_UninstallNativeCauseDetours();
        strcopy(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus), "降级关闭（Update detour 启用失败）");
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }

    // 2) TankBehavior::OnContact(this, sret, Tank* me, CBaseEntity* other, CGameTrace* trace) [ret 0x10]
    g_hBaitBehaviorContactDetour = new DynamicDetour(addrContact, CallConv_THISCALL, ReturnType_Int, ThisPointer_Address);
    if (g_hBaitBehaviorContactDetour == null)
    {
        TR_Bait_UninstallNativeCauseDetours();
        strcopy(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus), "降级关闭（OnContact detour 创建失败）");
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }
    g_hBaitBehaviorContactDetour.AddParam(HookParamType_Int);
    g_hBaitBehaviorContactDetour.AddParam(HookParamType_CBaseEntity);
    g_hBaitBehaviorContactDetour.AddParam(HookParamType_CBaseEntity);
    g_hBaitBehaviorContactDetour.AddParam(HookParamType_Int);
    if (!g_hBaitBehaviorContactDetour.Enable(Hook_Pre, TR_Bait_DetourBehaviorContactPre)
        || !g_hBaitBehaviorContactDetour.Enable(Hook_Post, TR_Bait_DetourBehaviorContactPost))
    {
        TR_Bait_UninstallNativeCauseDetours();
        strcopy(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus), "降级关闭（OnContact detour 启用失败）");
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }

    // 3) NextBotPlayer<CTerrorPlayer>::PressMeleeButton(INextBotPlayerInput* this, float duration) [ret 4]
    g_hBaitPressMeleeDetour = new DynamicDetour(addrPress, CallConv_THISCALL, ReturnType_Void, ThisPointer_Address);
    if (g_hBaitPressMeleeDetour == null)
    {
        TR_Bait_UninstallNativeCauseDetours();
        strcopy(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus), "降级关闭（PressMeleeButton detour 创建失败）");
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }
    g_hBaitPressMeleeDetour.AddParam(HookParamType_Float);
    if (!g_hBaitPressMeleeDetour.Enable(Hook_Pre, TR_Bait_DetourPressMeleePre))
    {
        TR_Bait_UninstallNativeCauseDetours();
        strcopy(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus), "降级关闭（PressMeleeButton detour 启用失败）");
        TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
        return;
    }

    g_iBaitOffPlayerInput = offInput;
    g_iBaitOffBody = offBody;
    g_iBaitOffVision = offVision;
    g_iBaitOffPrimaryThreat = offThreat;
    g_iBaitOffLookAtExpire = offExpire;
    g_iBaitOffLookAtReply = offReply;
    g_bBaitNativeCauseReady = true;
    strcopy(g_sBaitNativeStatus, sizeof(g_sBaitNativeStatus),
        "已启用（TankBehavior::Update/OnContact + PlayerBody Reply + PressMeleeButton 只读因果确证）");
    TR_Log("骗拳原版因果路径：%s", g_sBaitNativeStatus);
}

void TR_Bait_OnPluginStart()
{
    TR_Bait_ResetAll();
    TR_Bait_InstallNativeCauseDetours();
}

void TR_Bait_OnAllPluginsLoaded()
{
    if (!g_bBaitNativeCauseReady && g_bBaitPendingOsRetry
        && GetFeatureStatus(FeatureType_Native, "L4D_GetServerOS") == FeatureStatus_Available)
    {
        TR_Bait_InstallNativeCauseDetours();
    }
}

void TR_Bait_OnPluginEnd()
{
    TR_Bait_UninstallNativeCauseDetours();
    TR_Bait_ResetAll();
}

bool TR_Bait_ReadBodyLookAtState(int tank, int &outExpireBits, int &outReply)
{
    outExpireBits = 0;
    outReply = 0;
    if (!g_bBaitNativeCauseReady || tank < 1 || tank > MaxClients || !TR_IsAiTank(tank))
    {
        return false;
    }
    Address tankAddr = GetEntityAddress(tank);
    if (tankAddr == Address_Null)
    {
        return false;
    }
    Address bodyAddr = view_as<Address>(LoadFromAddress(tankAddr + view_as<Address>(g_iBaitOffBody), NumberType_Int32));
    if (bodyAddr == Address_Null)
    {
        return false;
    }
    outExpireBits = LoadFromAddress(bodyAddr + view_as<Address>(g_iBaitOffLookAtExpire), NumberType_Int32);
    outReply = LoadFromAddress(bodyAddr + view_as<Address>(g_iBaitOffLookAtReply), NumberType_Int32);
    return true;
}

int TR_Bait_FindTankByPlayerInput(Address pInput)
{
    if (pInput == Address_Null || g_iBaitOffPlayerInput != TR_BAIT_EXPECTED_OFF_INPUT)
    {
        return 0;
    }
    if (g_iBaitInUpdateTank >= 1 && g_iBaitInUpdateTank <= MaxClients && TR_IsAiTank(g_iBaitInUpdateTank))
    {
        Address tankAddr = GetEntityAddress(g_iBaitInUpdateTank);
        if (tankAddr != Address_Null && (tankAddr + view_as<Address>(g_iBaitOffPlayerInput)) == pInput)
        {
            return g_iBaitInUpdateTank;
        }
    }
    if (g_iBaitInContactTank >= 1 && g_iBaitInContactTank <= MaxClients && TR_IsAiTank(g_iBaitInContactTank))
    {
        Address tankAddr = GetEntityAddress(g_iBaitInContactTank);
        if (tankAddr != Address_Null && (tankAddr + view_as<Address>(g_iBaitOffPlayerInput)) == pInput)
        {
            return g_iBaitInContactTank;
        }
    }
    for (int t = 1; t <= MaxClients; t++)
    {
        if (!TR_IsAiTank(t))
        {
            continue;
        }
        Address tankAddr = GetEntityAddress(t);
        if (tankAddr != Address_Null && (tankAddr + view_as<Address>(g_iBaitOffPlayerInput)) == pInput)
        {
            return t;
        }
    }
    return 0;
}

int TR_Bait_ReadVisionPrimarySurvivor(int tank)
{
    if (!g_bBaitNativeCauseReady || tank < 1 || tank > MaxClients || !TR_IsAiTank(tank))
    {
        return 0;
    }
    Address tankAddr = GetEntityAddress(tank);
    if (tankAddr == Address_Null)
    {
        return 0;
    }
    Address visionAddr = view_as<Address>(LoadFromAddress(tankAddr + view_as<Address>(g_iBaitOffVision), NumberType_Int32));
    if (visionAddr == Address_Null)
    {
        return 0;
    }
    int rawHandle = LoadFromAddress(visionAddr + view_as<Address>(g_iBaitOffPrimaryThreat), NumberType_Int32);
    int entIdx = -1, serial = 0;
    return TR_Glow_DecodeSurvivorHandle(rawHandle, entIdx, serial);
}

public MRESReturn TR_Bait_DetourBehaviorUpdatePre(Address pBehavior, DHookReturn hReturn, DHookParam hParams)
{
    #pragma unused pBehavior, hReturn
    if (!g_bBaitNativeCauseReady || !TR_FeatureEnabled() || g_bRoundEnded)
    {
        return MRES_Ignored;
    }
    int tank = hParams.Get(2);
    if (tank >= 1 && tank <= MaxClients && TR_IsAiTank(tank))
    {
        g_iBaitInUpdateTank = tank;
        g_iBaitInUpdateTankUid = GetClientUserId(tank);
    }
    return MRES_Ignored;
}

public MRESReturn TR_Bait_DetourBehaviorUpdatePost(Address pBehavior, DHookReturn hReturn, DHookParam hParams)
{
    #pragma unused pBehavior, hReturn, hParams
    g_iBaitInUpdateTank = 0;
    g_iBaitInUpdateTankUid = 0;
    return MRES_Ignored;
}

public MRESReturn TR_Bait_DetourBehaviorContactPre(Address pBehavior, DHookReturn hReturn, DHookParam hParams)
{
    #pragma unused pBehavior, hReturn
    if (!g_bBaitNativeCauseReady || !TR_FeatureEnabled() || g_bRoundEnded)
    {
        return MRES_Ignored;
    }
    int tank = hParams.Get(2);
    if (tank < 1 || tank > MaxClients || !TR_IsAiTank(tank))
    {
        return MRES_Ignored;
    }
    int other = hParams.IsNull(3) ? -1 : hParams.Get(3);
    int tankUid = GetClientUserId(tank);

    g_iBaitInContactTank = tank;
    g_iBaitInContactTankUid = tankUid;
    g_iBaitInContactOther = other;
    TR_Bait_ReadBodyLookAtState(tank, g_iBaitContactPreExpireBits, g_iBaitContactPreReply);
    return MRES_Ignored;
}

public MRESReturn TR_Bait_DetourBehaviorContactPost(Address pBehavior, DHookReturn hReturn, DHookParam hParams)
{
    #pragma unused pBehavior, hReturn, hParams
    int tank = g_iBaitInContactTank;
    int tankUid = g_iBaitInContactTankUid;
    int other = g_iBaitInContactOther;
    int preExpire = g_iBaitContactPreExpireBits;
    int preReply = g_iBaitContactPreReply;

    g_iBaitInContactTank = 0;
    g_iBaitInContactTankUid = 0;
    g_iBaitInContactOther = -1;
    g_iBaitContactPreExpireBits = 0;
    g_iBaitContactPreReply = 0;

    // 仅当本次 OnContact 实际更新了 PlayerBody::AimHeadTowards(..., reply) 时才装填待触发接触目标，
    // 防止普通擦碰墙体、拍飞轻道具或优先级被拒的接触污染已装填的因果归属。
    if (tank >= 1 && tank <= MaxClients && TR_IsAiTank(tank) && GetClientUserId(tank) == tankUid)
    {
        int postExpire = 0, postReply = 0;
        if (TR_Bait_ReadBodyLookAtState(tank, postExpire, postReply)
            && postReply != 0 && (postReply != preReply || postExpire != preExpire))
        {
            if (other >= 1 && other <= MaxClients)
            {
                if (TR_IsSurvivor(other) && !IsFakeClient(other))
                {
                    g_iBaitArmedContactKind[tank] = TR_BAIT_CONTACT_HUMAN;
                    g_iBaitArmedContactHumanUid[tank] = GetClientUserId(other);
                }
                else
                {
                    g_iBaitArmedContactKind[tank] = TR_BAIT_CONTACT_OTHER;
                    g_iBaitArmedContactHumanUid[tank] = 0;
                }
                g_iBaitArmedContactTankUid[tank] = tankUid;
                g_fBaitArmedContactTime[tank] = GetGameTime();
                g_bBaitArmedContactValid[tank] = true;
            }
            else if (other > MaxClients && IsValidEntity(other))
            {
                g_iBaitArmedContactKind[tank] = TR_BAIT_CONTACT_PROP;
                g_iBaitArmedContactHumanUid[tank] = 0;
                g_iBaitArmedContactTankUid[tank] = tankUid;
                g_fBaitArmedContactTime[tank] = GetGameTime();
                g_bBaitArmedContactValid[tank] = true;
            }
        }
    }
    return MRES_Ignored;
}

public MRESReturn TR_Bait_DetourPressMeleePre(Address pInput, DHookParam hParams)
{
    #pragma unused hParams
    if (!g_bBaitNativeCauseReady || !TR_FeatureEnabled() || g_bRoundEnded)
    {
        return MRES_Ignored;
    }
    int tank = TR_Bait_FindTankByPlayerInput(pInput);
    if (tank < 1 || tank > MaxClients || !TR_IsAiTank(tank))
    {
        return MRES_Ignored;
    }

    int tankUid = GetClientUserId(tank);
    float now = GetGameTime();

    // 因果分支 1：TankBehavior::Update 直接触发近战 -> 读取实时主威胁 [vision + 0x148]
    if (g_iBaitInUpdateTank == tank && g_iBaitInUpdateTankUid == tankUid)
    {
        int surv = TR_Bait_ReadVisionPrimarySurvivor(tank);
        if (surv > 0 && !IsFakeClient(surv))
        {
            g_iBaitMeleeCause[tank] = TR_BAIT_CAUSE_SURVIVOR_UPDATE;
            g_iBaitMeleeTankUid[tank] = tankUid;
            g_iBaitMeleeTargetUid[tank] = GetClientUserId(surv);
        }
        else
        {
            g_iBaitMeleeCause[tank] = TR_BAIT_CAUSE_NON_HUMAN;
            g_iBaitMeleeTankUid[tank] = tankUid;
            g_iBaitMeleeTargetUid[tank] = 0;
        }
        g_fBaitMeleeTime[tank] = now;
        g_bBaitMeleeConsumed[tank] = false;
        return MRES_Ignored;
    }

    // 因果分支 2：TankBehavior::OnContact 直接触发近战（重型物理道具/车辆等，不依赖类名白名单与伤害是否被拦截）
    if (g_iBaitInContactTank == tank && g_iBaitInContactTankUid == tankUid)
    {
        int other = g_iBaitInContactOther;
        if (other >= 1 && other <= MaxClients && TR_IsSurvivor(other) && !IsFakeClient(other))
        {
            g_iBaitMeleeCause[tank] = TR_BAIT_CAUSE_SURVIVOR_CONTACT;
            g_iBaitMeleeTankUid[tank] = tankUid;
            g_iBaitMeleeTargetUid[tank] = GetClientUserId(other);
        }
        else
        {
            g_iBaitMeleeCause[tank] = TR_BAIT_CAUSE_PROP;
            g_iBaitMeleeTankUid[tank] = tankUid;
            g_iBaitMeleeTargetUid[tank] = 0;
        }
        g_fBaitMeleeTime[tank] = now;
        g_bBaitMeleeConsumed[tank] = false;
        return MRES_Ignored;
    }

    // 因果分支 3：TankBehavior::OnContact 通过 AimHeadTowards -> INextBotReply::OnSuccess 触发的延迟近战（单次消费）
    if (g_bBaitArmedContactValid[tank] && g_iBaitArmedContactTankUid[tank] == tankUid
        && (now - g_fBaitArmedContactTime[tank]) <= TR_BAIT_CONTACT_AIM_TTL)
    {
        int kind = g_iBaitArmedContactKind[tank];
        int humanUid = g_iBaitArmedContactHumanUid[tank];
        g_bBaitArmedContactValid[tank] = false;

        if (kind == TR_BAIT_CONTACT_HUMAN && humanUid > 0)
        {
            int surv = GetClientOfUserId(humanUid);
            if (TR_IsSurvivor(surv) && !IsFakeClient(surv))
            {
                g_iBaitMeleeCause[tank] = TR_BAIT_CAUSE_SURVIVOR_CONTACT;
                g_iBaitMeleeTankUid[tank] = tankUid;
                g_iBaitMeleeTargetUid[tank] = humanUid;
                g_fBaitMeleeTime[tank] = now;
                g_bBaitMeleeConsumed[tank] = false;
                return MRES_Ignored;
            }
        }
        if (kind == TR_BAIT_CONTACT_PROP)
        {
            g_iBaitMeleeCause[tank] = TR_BAIT_CAUSE_PROP;
            g_iBaitMeleeTankUid[tank] = tankUid;
            g_iBaitMeleeTargetUid[tank] = 0;
            g_fBaitMeleeTime[tank] = now;
            g_bBaitMeleeConsumed[tank] = false;
            return MRES_Ignored;
        }
    }

    // 因果分支 4：无法证明来源 -> 保守拒绝（宁不处死，绝不误杀）
    g_iBaitMeleeCause[tank] = TR_BAIT_CAUSE_NONE;
    g_iBaitMeleeTankUid[tank] = tankUid;
    g_iBaitMeleeTargetUid[tank] = 0;
    g_fBaitMeleeTime[tank] = now;
    g_bBaitMeleeConsumed[tank] = false;
    return MRES_Ignored;
}

// ---------------------------------------------------------------- 碰撞体边缘间距（精确 3D AABB 盒体表面距离，不依赖视线）
float TR_ComputeAABBEdgeDistance(const float posA[3], const float minsA[3], const float maxsA[3],
    const float posB[3], const float minsB[3], const float maxsB[3])
{
    float distSq = 0.0;
    for (int axis = 0; axis < 3; axis++)
    {
        float left = (posA[axis] + minsA[axis]) - (posB[axis] + maxsB[axis]);
        float right = (posB[axis] + minsB[axis]) - (posA[axis] + maxsA[axis]);
        float gap = left > right ? left : right;
        if (gap > 0.0)
        {
            distSq += gap * gap;
        }
    }
    return SquareRoot(distSq);
}

float TR_GetClientAABBEdgeDistance(int clientA, int clientB)
{
    float posA[3], minsA[3], maxsA[3];
    float posB[3], minsB[3], maxsB[3];
    GetClientAbsOrigin(clientA, posA);
    GetClientMins(clientA, minsA);
    GetClientMaxs(clientA, maxsA);
    GetClientAbsOrigin(clientB, posB);
    GetClientMins(clientB, minsB);
    GetClientMaxs(clientB, maxsB);
    return TR_ComputeAABBEdgeDistance(posA, minsA, maxsA, posB, minsB, maxsB);
}

float TR_GetReplicaSurvivorEdgeDistance(int replicaEnt, int survivor)
{
    float posA[3], minsA[3], maxsA[3];
    float posB[3], minsB[3], maxsB[3];
    GetEntPropVector(replicaEnt, Prop_Data, "m_vecAbsOrigin", posA);
    bool gotBounds = false;
    if (HasEntProp(replicaEnt, Prop_Send, "m_vecMins") && HasEntProp(replicaEnt, Prop_Send, "m_vecMaxs"))
    {
        GetEntPropVector(replicaEnt, Prop_Send, "m_vecMins", minsA);
        GetEntPropVector(replicaEnt, Prop_Send, "m_vecMaxs", maxsA);
        if ((maxsA[0] - minsA[0]) > 1.0 && (maxsA[1] - minsA[1]) > 1.0 && (maxsA[2] - minsA[2]) > 1.0)
        {
            gotBounds = true;
        }
    }
    if (!gotBounds)
    {
        // 原版站立 Tank 碰撞盒回退值（与 TC_NAV_MINS/MAXS 一致）
        minsA[0] = -24.0; minsA[1] = -24.0; minsA[2] = 0.0;
        maxsA[0] =  24.0; maxsA[1] =  24.0; maxsA[2] = 74.0;
    }
    GetClientAbsOrigin(survivor, posB);
    GetClientMins(survivor, minsB);
    GetClientMaxs(survivor, maxsB);
    return TR_ComputeAABBEdgeDistance(posA, minsA, maxsA, posB, minsB, maxsB);
}

// ---------------------------------------------------------------- 状态生命周期
void TR_Bait_CancelTimer(int tank)
{
    if (g_NativeBait[tank].evalTimer != null)
    {
        KillTimer(g_NativeBait[tank].evalTimer);
        g_NativeBait[tank].evalTimer = null;
    }
}

void TR_Bait_ResetNativeTank(int tank)
{
    if (tank < 1 || tank > MaxClients)
    {
        return;
    }
    TR_Bait_CancelTimer(tank);
    TR_Bait_ClearMeleeToken(tank);
    TR_NativeBaitState empty;
    g_NativeBait[tank] = empty;
    g_iTankLifeGen[tank] = 0;
}

void TR_Bait_OnNativeTankTracked(int tank)
{
    if (tank < 1 || tank > MaxClients)
    {
        return;
    }
    TR_Bait_CancelTimer(tank);
    TR_Bait_ClearMeleeToken(tank);
    TR_NativeBaitState empty;
    g_iTankLifeGen[tank] = ++g_iNextTankLifeGen;
    empty.lifeGen = g_iTankLifeGen[tank];
    g_NativeBait[tank] = empty;
}

void TR_Bait_OnReplicaCreated(int slot)
{
    if (slot < 0 || slot >= TR_BAIT_REPLICA_SLOTS)
    {
        return;
    }
    TR_ReplicaBaitState empty;
    empty.lifeGen = ++g_iNextTankLifeGen;
    g_ReplicaBait[slot] = empty;
}

void TR_Bait_OnReplicaRemoved(int slot)
{
    if (slot < 0 || slot >= TR_BAIT_REPLICA_SLOTS)
    {
        return;
    }
    TR_ReplicaBaitState empty;
    g_ReplicaBait[slot] = empty;
}

void TR_Bait_ResetAll()
{
    for (int i = 1; i <= MAXPLAYERS; i++)
    {
        TR_Bait_ResetNativeTank(i);
    }
    for (int s = 0; s < TR_BAIT_REPLICA_SLOTS; s++)
    {
        TR_Bait_OnReplicaRemoved(s);
    }
    g_iActiveNativeClawTank = 0;
    g_iActiveNativeClawLifeGen = 0;
    g_iActiveNativeClawAttackGen = 0;
    g_iActiveReplicaClawSlot = -1;
    g_iActiveReplicaClawLifeGen = 0;
    g_iActiveReplicaClawAttackGen = 0;
    g_iActiveReplicaClawTarget = 0;
    g_iActiveReplicaClawTargetUid = 0;
    g_bActiveReplicaTargetKilledByOther = false;
}

// ---------------------------------------------------------------- 原版 AI Tank 出拳取样（严格实证因果，零猜测兜底）
bool TR_Bait_ConsumeVerifiedNativeCause(int tank, int &outCauseKind, int &outHuman, float &outEdgeDist, bool &outIsProp)
{
    outCauseKind = TR_BAIT_CAUSE_NONE;
    outHuman = 0;
    outEdgeDist = 999999.0;
    outIsProp = false;

    if (!g_bBaitNativeCauseReady || tank < 1 || tank > MaxClients || !TR_IsAiTank(tank))
    {
        return false;
    }

    int tankUid = GetClientUserId(tank);
    float now = GetGameTime();
    if (g_bBaitMeleeConsumed[tank] || g_iBaitMeleeTankUid[tank] != tankUid
        || (now - g_fBaitMeleeTime[tank]) > TR_BAIT_MELEE_TOKEN_TTL)
    {
        return false;
    }

    int cause = g_iBaitMeleeCause[tank];
    int targetUid = g_iBaitMeleeTargetUid[tank];
    g_bBaitMeleeConsumed[tank] = true;
    outCauseKind = cause;

    if (cause == TR_BAIT_CAUSE_PROP)
    {
        outIsProp = true;
        return false;
    }
    if (cause != TR_BAIT_CAUSE_SURVIVOR_UPDATE && cause != TR_BAIT_CAUSE_SURVIVOR_CONTACT)
    {
        return false;
    }

    int surv = GetClientOfUserId(targetUid);
    if (!TR_IsSurvivor(surv) || IsFakeClient(surv))
    {
        return false;
    }

    // 碰撞体边缘间距 0..64u（不要求视线通畅，保留隔墙/拐角后撤真实针对真人的挥击资格）
    float edgeDist = TR_GetClientAABBEdgeDistance(tank, surv);
    if (edgeDist < 0.0 || edgeDist > TR_BAIT_PUNCH_MAX_EDGE_DIST)
    {
        return false;
    }

    outHuman = surv;
    outEdgeDist = edgeDist;
    return true;
}

void TR_Bait_BeginNativeSwing(int tank, bool isGroundPound, const char[] source)
{
    if (!TR_BaitPunchEnabled() || !TR_IsAiTank(tank))
    {
        return;
    }
    if (g_iTankLifeGen[tank] <= 0)
    {
        TR_Bait_OnNativeTankTracked(tank);
    }

    float now = GetGameTime();
    // 同一记出拳的前摇期可能依次触发 OnSwingStart / DoAnimationEvent / GroundPound，合并为同一 attackGen
    if (g_NativeBait[tank].pending && (now - g_NativeBait[tank].swingStartTime) < 0.30)
    {
        if (isGroundPound)
        {
            g_NativeBait[tank].isGroundPound = true;
        }
        return;
    }

    TR_Bait_CancelTimer(tank);
    int attackGen = ++g_iNextAttackGen;
    g_NativeBait[tank].lifeGen = g_iTankLifeGen[tank];
    g_NativeBait[tank].attackGen = attackGen;
    g_NativeBait[tank].pending = true;
    g_NativeBait[tank].qualified = false;
    g_NativeBait[tank].isGroundPound = isGroundPound;
    g_NativeBait[tank].settled = false;
    g_NativeBait[tank].hitSurvivor = false;
    g_NativeBait[tank].triggeredByProp = false;
    g_NativeBait[tank].killedAnySurvivor = false;
    g_NativeBait[tank].intendedHumanUid = 0;
    g_NativeBait[tank].causeKind = TR_BAIT_CAUSE_NONE;
    g_NativeBait[tank].swingStartTime = now;
    g_NativeBait[tank].firstSettleTime = 0.0;

    int causeKind = TR_BAIT_CAUSE_NONE;
    int human = 0;
    float edgeDist = 0.0;
    bool isProp = false;
    if (TR_Bait_ConsumeVerifiedNativeCause(tank, causeKind, human, edgeDist, isProp))
    {
        g_NativeBait[tank].qualified = true;
        g_NativeBait[tank].intendedHumanUid = GetClientUserId(human);
        g_NativeBait[tank].causeKind = causeKind;
        TR_LogEvent("骗拳取样合格（原版实证因果）：Tank#%d life=%d atk=%d 来源=%s cause=%d 砸地=%d 目标真人=#%d 边缘距=%.1fu",
            tank, g_NativeBait[tank].lifeGen, attackGen, source, causeKind, isGroundPound ? 1 : 0, human, edgeDist);
    }
    else if (isProp)
    {
        g_NativeBait[tank].triggeredByProp = true;
        g_NativeBait[tank].causeKind = TR_BAIT_CAUSE_PROP;
        TR_LogEvent("骗拳识别为道具/门接触出拳（原版排除）：Tank#%d life=%d atk=%d 来源=%s",
            tank, g_NativeBait[tank].lifeGen, attackGen, source);
    }
    else
    {
        g_NativeBait[tank].causeKind = causeKind;
    }
}

void TR_Bait_ScheduleNativeEval(int tank, float delay)
{
    TR_Bait_CancelTimer(tank);
    DataPack pack = new DataPack();
    pack.WriteCell(GetClientUserId(tank));
    pack.WriteCell(g_NativeBait[tank].lifeGen);
    pack.WriteCell(g_NativeBait[tank].attackGen);
    g_NativeBait[tank].evalTimer = CreateTimer(delay, TR_Bait_TimerEvalNative, pack, TIMER_FLAG_NO_MAPCHANGE | TIMER_DATA_HNDL_CLOSE);
}

void TR_Bait_OnNativeSwingPre(int tank, bool isGroundPound)
{
    if (!TR_BaitPunchEnabled() || !TR_IsAiTank(tank))
    {
        return;
    }
    if (!g_NativeBait[tank].pending || (GetGameTime() - g_NativeBait[tank].swingStartTime) > 1.5)
    {
        // 若此前未捕获动画开始（例如直接进入砸地拳结算），在此兜底打开 attackGen
        TR_Bait_BeginNativeSwing(tank, isGroundPound, isGroundPound ? "GroundPound_Pre" : "DoSwing_Pre");
    }
    else if (isGroundPound)
    {
        g_NativeBait[tank].isGroundPound = true;
    }

    g_iActiveNativeClawTank = tank;
    g_iActiveNativeClawLifeGen = g_NativeBait[tank].lifeGen;
    g_iActiveNativeClawAttackGen = g_NativeBait[tank].attackGen;
}

void TR_Bait_OnNativePlayerHit(int tank, int player)
{
    if (tank < 1 || tank > MaxClients || !g_NativeBait[tank].pending)
    {
        return;
    }
    if (TR_IsSurvivor(player))
    {
        g_NativeBait[tank].hitSurvivor = true;
    }
}

void TR_Bait_OnNativeSwingPost(int tank, bool isGroundPound)
{
    g_iActiveNativeClawTank = 0;
    g_iActiveNativeClawLifeGen = 0;
    g_iActiveNativeClawAttackGen = 0;

    if (!TR_BaitPunchEnabled() || tank < 1 || tank > MaxClients || !g_NativeBait[tank].pending)
    {
        return;
    }
    if (g_iTankLifeGen[tank] != g_NativeBait[tank].lifeGen)
    {
        return;
    }

    float now = GetGameTime();
    bool firstSettle = !g_NativeBait[tank].settled;
    g_NativeBait[tank].settled = true;
    if (firstSettle)
    {
        g_NativeBait[tank].firstSettleTime = now;
    }

    // 等待本次有效扫描窗口完全结束再结算，避免第 1 帧挥空误杀而后续帧实际击杀生还者（含 Bot）
    float delay = 0.05;
    if (!isGroundPound && !g_NativeBait[tank].hitSurvivor)
    {
        ConVar cvDur = FindConVar("tank_swing_duration");
        float swingDur = (cvDur != null && cvDur.FloatValue > 0.0) ? cvDur.FloatValue : 0.20;
        float remaining = (g_NativeBait[tank].firstSettleTime + swingDur + 0.04) - now;
        if (remaining > delay)
        {
            delay = remaining;
        }
    }
    TR_Bait_ScheduleNativeEval(tank, delay);
}

public Action TR_Bait_TimerEvalNative(Handle timer, DataPack pack)
{
    pack.Reset();
    int userid = pack.ReadCell();
    int lifeGen = pack.ReadCell();
    int attackGen = pack.ReadCell();

    int tank = GetClientOfUserId(userid);
    if (tank < 1 || tank > MaxClients)
    {
        return Plugin_Stop;
    }
    if (g_NativeBait[tank].evalTimer == timer)
    {
        g_NativeBait[tank].evalTimer = null;
    }
    if (!TR_BaitPunchEnabled() || !TR_IsAiTank(tank))
    {
        return Plugin_Stop;
    }
    if (g_iTankLifeGen[tank] != lifeGen || g_NativeBait[tank].lifeGen != lifeGen || g_NativeBait[tank].attackGen != attackGen)
    {
        return Plugin_Stop;
    }
    if (g_NativeBait[tank].lastSlayedAttackGen == attackGen)
    {
        return Plugin_Stop;
    }

    g_NativeBait[tank].pending = false;

    if (g_NativeBait[tank].triggeredByProp)
    {
        g_iStatBaitPunchExcludedObstacle++;
        TR_LogEvent("骗拳排除（原版道具/门接触因果）：Tank#%d life=%d atk=%d", tank, lifeGen, attackGen);
        return Plugin_Stop;
    }
    if (!g_NativeBait[tank].qualified || !g_NativeBait[tank].settled)
    {
        return Plugin_Stop;
    }
    if (g_NativeBait[tank].killedAnySurvivor)
    {
        g_iStatBaitPunchBlockedDeath++;
        TR_LogEvent("骗拳不处死（原版本击造成生还者死亡）：Tank#%d life=%d atk=%d", tank, lifeGen, attackGen);
        return Plugin_Stop;
    }

    if (TR_HasNativeRockInFlight(tank))
    {
        TR_LogEvent("骗拳跳过（原版存在未结算投石/原石）：Tank#%d life=%d atk=%d", tank, lifeGen, attackGen);
        return Plugin_Stop;
    }
    g_NativeBait[tank].lastSlayedAttackGen = attackGen;
    g_iStatBaitPunchSlaysNative++;
    TR_Log("骗拳处死生效（原版）：Tank#%d userid=%d life=%d atk=%d cause=%d 砸地=%d 命中=%d 目标真人uid=%d",
        tank, userid, lifeGen, attackGen, g_NativeBait[tank].causeKind,
        g_NativeBait[tank].isGroundPound ? 1 : 0,
        g_NativeBait[tank].hitSurvivor ? 1 : 0,
        g_NativeBait[tank].intendedHumanUid);
    TR_KillTank(tank, "骗拳处死", true);
    return Plugin_Stop;
}

// ---------------------------------------------------------------- 容量补位 Tank 骗拳取样、世界归因与严格同步 damage guard
void TR_Bait_OnReplicaClawStart(int slot, int replicaEnt, int targetClient)
{
    if (slot < 0 || slot >= TR_BAIT_REPLICA_SLOTS)
    {
        return;
    }
    if (g_ReplicaBait[slot].lifeGen <= 0)
    {
        TR_Bait_OnReplicaCreated(slot);
    }

    int attackGen = ++g_iNextAttackGen;
    g_ReplicaBait[slot].attackGen = attackGen;
    g_ReplicaBait[slot].pending = true;
    g_ReplicaBait[slot].qualified = false;
    g_ReplicaBait[slot].settled = false;
    g_ReplicaBait[slot].killedAnySurvivor = false;
    g_ReplicaBait[slot].intendedHumanUid = 0;

    if (!TR_BaitPunchEnabled())
    {
        return;
    }
    if (!TR_IsSurvivor(targetClient) || IsFakeClient(targetClient))
    {
        return;
    }

    float edgeDist = TR_GetReplicaSurvivorEdgeDistance(replicaEnt, targetClient);
    if (edgeDist >= 0.0 && edgeDist <= TR_BAIT_PUNCH_MAX_EDGE_DIST)
    {
        g_ReplicaBait[slot].qualified = true;
        g_ReplicaBait[slot].intendedHumanUid = GetClientUserId(targetClient);
        TR_LogEvent("骗拳取样合格（补位）：slot=%d ent=%d life=%d atk=%d 目标真人=#%d 边缘距=%.1fu",
            slot, replicaEnt, g_ReplicaBait[slot].lifeGen, attackGen, targetClient, edgeDist);
    }
}

void TR_Bait_BeginReplicaClawDamage(int slot, int targetClient)
{
    if (slot < 0 || slot >= TR_BAIT_REPLICA_SLOTS)
    {
        return;
    }
    g_iActiveReplicaClawSlot = slot;
    g_iActiveReplicaClawLifeGen = g_ReplicaBait[slot].lifeGen;
    g_iActiveReplicaClawAttackGen = g_ReplicaBait[slot].attackGen;
    g_iActiveReplicaClawTarget = targetClient;
    g_iActiveReplicaClawTargetUid = (targetClient >= 1 && targetClient <= MaxClients && IsClientInGame(targetClient))
        ? GetClientUserId(targetClient) : 0;
    g_bActiveReplicaTargetKilledByOther = false;
}

void TR_Bait_EndReplicaClawDamage(int slot, int targetClient, bool targetWasAliveBefore)
{
    if (slot >= 0 && slot < TR_BAIT_REPLICA_SLOTS
        && slot == g_iActiveReplicaClawSlot
        && g_ReplicaBait[slot].lifeGen == g_iActiveReplicaClawLifeGen
        && g_ReplicaBait[slot].attackGen == g_iActiveReplicaClawAttackGen
        && !g_bActiveReplicaTargetKilledByOther
        && targetWasAliveBefore
        && targetClient == g_iActiveReplicaClawTarget
        && g_iActiveReplicaClawTargetUid > 0
        && targetClient >= 1 && targetClient <= MaxClients
        && GetClientOfUserId(g_iActiveReplicaClawTargetUid) == targetClient
        && (!IsClientInGame(targetClient) || !IsPlayerAlive(targetClient)))
    {
        // 双重保险：仅针对本次世界归因调用的精确目标且未被其他攻击者击杀时生效
        g_ReplicaBait[slot].killedAnySurvivor = true;
    }
    g_iActiveReplicaClawSlot = -1;
    g_iActiveReplicaClawLifeGen = 0;
    g_iActiveReplicaClawAttackGen = 0;
    g_iActiveReplicaClawTarget = 0;
    g_iActiveReplicaClawTargetUid = 0;
    g_bActiveReplicaTargetKilledByOther = false;
}

bool TR_Bait_OnReplicaClawSettled(int slot, int replicaEnt)
{
    if (slot < 0 || slot >= TR_BAIT_REPLICA_SLOTS || !g_ReplicaBait[slot].pending)
    {
        return false;
    }

    int lifeGen = g_ReplicaBait[slot].lifeGen;
    int attackGen = g_ReplicaBait[slot].attackGen;
    g_ReplicaBait[slot].settled = true;
    g_ReplicaBait[slot].pending = false;

    if (!TR_BaitPunchEnabled() || !g_ReplicaBait[slot].qualified)
    {
        return false;
    }
    if (g_ReplicaBait[slot].lastSlayedAttackGen == attackGen)
    {
        return false;
    }
    if (g_ReplicaBait[slot].killedAnySurvivor)
    {
        g_iStatBaitPunchBlockedDeath++;
        TR_LogEvent("骗拳不处死（补位本击造成生还者死亡）：slot=%d ent=%d life=%d atk=%d", slot, replicaEnt, lifeGen, attackGen);
        return false;
    }

    if (TC_CombatIsRockProtected(slot))
    {
        TR_LogEvent("骗拳跳过（补位存在未结算投石/原石）：slot=%d ent=%d life=%d atk=%d", slot, replicaEnt, lifeGen, attackGen);
        return false;
    }
    g_ReplicaBait[slot].lastSlayedAttackGen = attackGen;
    g_iStatBaitPunchSlaysReplica++;
    TR_Log("骗拳处死生效（补位）：slot=%d ent=%d life=%d atk=%d 目标真人uid=%d",
        slot, replicaEnt, lifeGen, attackGen, g_ReplicaBait[slot].intendedHumanUid);
    return true;
}

// ---------------------------------------------------------------- 生还者（含 Bot）死亡归因（严格防同调用栈串台）
void TR_Bait_OnSurvivorDeath(int victim, int attacker, const char[] weapon)
{
    if (victim < 1 || victim > MaxClients || !IsClientInGame(victim) || GetClientTeam(victim) != 2)
    {
        return;
    }

    // 1) 同步容量补位拳击上下文：仅匹配精确受害人实例 (victim + targetUid) 且为世界归因伤害 (attacker==0)
    if (g_iActiveReplicaClawSlot >= 0 && g_iActiveReplicaClawSlot < TR_BAIT_REPLICA_SLOTS)
    {
        int s = g_iActiveReplicaClawSlot;
        int victimUid = GetClientUserId(victim);
        if (g_ReplicaBait[s].lifeGen == g_iActiveReplicaClawLifeGen
            && g_ReplicaBait[s].attackGen == g_iActiveReplicaClawAttackGen
            && victim == g_iActiveReplicaClawTarget
            && g_iActiveReplicaClawTargetUid > 0
            && victimUid == g_iActiveReplicaClawTargetUid)
        {
            if (attacker == 0)
            {
                g_ReplicaBait[s].killedAnySurvivor = true;
                return;
            }
            // 同调用栈内该目标被非世界来源（例如其他 Tank/插件伤害）击杀，绝不误算到补位 Tank
            g_bActiveReplicaTargetKilledByOther = true;
        }
        // 同调用栈内其他受害人死亡：绝不误算到补位 Tank，继续向下归因给真正攻击者
    }

    // 2) 同步原版 Tank 挥拳/砸地拳上下文：仅当攻击者就是当前挥拳的 Tank 且武器为拳击（或空）时归因
    if (g_iActiveNativeClawTank >= 1 && g_iActiveNativeClawTank <= MaxClients)
    {
        int t = g_iActiveNativeClawTank;
        if (attacker == t
            && g_NativeBait[t].lifeGen == g_iActiveNativeClawLifeGen
            && g_NativeBait[t].attackGen == g_iActiveNativeClawAttackGen
            && (weapon[0] == '\0' || StrContains(weapon, "tank_claw", false) != -1 || StrEqual(weapon, "claw", false)))
        {
            g_NativeBait[t].killedAnySurvivor = true;
            return;
        }
    }

    // 3) 非同步但在待结算窗口内，明确由该 Tank 的拳击武器击杀任意生还者（含 Bot）
    if (attacker >= 1 && attacker <= MaxClients && g_NativeBait[attacker].pending
        && g_NativeBait[attacker].lifeGen == g_iTankLifeGen[attacker]
        && (StrContains(weapon, "tank_claw", false) != -1 || StrEqual(weapon, "claw", false)))
    {
        g_NativeBait[attacker].killedAnySurvivor = true;
    }
}

// ---------------------------------------------------------------- Forward 钩子
public void L4D_OnSwingStart(int client, int weapon)
{
    if (!TR_BaitPunchEnabled() || !TR_IsAiTank(client))
    {
        return;
    }
    if (TR_IsClawDamageEntity(weapon))
    {
        TR_Bait_BeginNativeSwing(client, false, "L4D_OnSwingStart");
    }
}

public void L4D_TankClaw_GroundPound_Post(int tank, int claw)
{
    #pragma unused claw
    TR_Bait_OnNativeSwingPost(tank, true);
}
