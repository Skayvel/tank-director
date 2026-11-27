// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

// Tank Run v2.3 仇恨提示层（Rock Target Glow）
//   - 开关：tankrun_rock_target_glow（默认 0，仅 Tank Run 合作）
//   - 仅于已确认的投石锁定段显示；仅游戏内活着的真人生还者可见
//   - 被锁生还者在锁定自己的 Tank 上看红边；其他真人生还者在 Tank 与对应被锁生还者上看同一配对色
//   - 最多 8 种中等亮度非红配对色；超出 8 组时新被锁者红边保留，配对色对他人隐藏
//   - 绝不全局改写真实玩家/Tank 的 m_iGlowType / m_glowColorOverride
//   - 代理数量有界：每只 Tank <= 2 个代理，每个目标生还者 <= 1 个代理
//   - 保守严格非泄露可见门：通过 SDKHook_SetTransmit 实时零正缓存校验（0ms 过期容忍），
//     要求主体 3D 碰撞盒中心及 8 个包络角点全部严格无遮挡（!TR_DidHit && !TR_StartSolid），
//     墙/闭门/拐角/部分遮挡/传送瞬移后同帧立即阻断传输，防止 m_iGlowType=3 泄露已遮挡轮廓
//   - 严禁猜测目标，严格绑定投石代次（throwGen）：
//     容量路径读 TC_ATTACK_ROCK 的 attackTargetUid 与 rockThrowGen；
//     原版路径通过只读 DHooks Pre/Post detour 观测 TankAttack::ThrowRockHelper 的 [TankAttack + 0x34]，
//     仅绑定当前投石代次，正常取消/动画中断/接管/离手/断线/换队均立即清理观测并失效代次。

#if defined _tankrun_rock_glow_included
 #endinput
#endif
#define _tankrun_rock_glow_included

#define TR_GLOW_MAX_COLORS        8
#define TR_GLOW_MAX_REPLICAS      32
#define TR_GLOW_MAX_PROXY_SLOTS   160    // 上界：32原版*2 + 32补位*2 + 32生还者*1 = 160
#define TR_GLOW_MAX_EDICTS        2048

#define TR_GLOW_ROLE_NONE         0
#define TR_GLOW_ROLE_TANK_RED     1
#define TR_GLOW_ROLE_TANK_PAIR    2
#define TR_GLOW_ROLE_SURV_PAIR    3

// 打包 RGB (L4D2 m_glowColorOverride = R + G*256 + B*65536)
#define TR_GLOW_RGB(%1,%2,%3)     ((%1) | ((%2) << 8) | ((%3) << 16))
#define TR_GLOW_COLOR_RED         TR_GLOW_RGB(255, 40, 40)

// 8 种中等亮度、高区分度、非红配对色
static const int TR_GLOW_PAIR_PALETTE[TR_GLOW_MAX_COLORS] = {
    TR_GLOW_RGB(64, 210, 220),   // 0: 青色 Cyan
    TR_GLOW_RGB(230, 185, 55),   // 1: 琥珀金 Amber
    TR_GLOW_RGB(70, 215, 130),   // 2: 薄荷绿 Mint
    TR_GLOW_RGB(80, 165, 240),   // 3: 天蓝 Sky
    TR_GLOW_RGB(185, 115, 235),  // 4: 兰紫 Orchid
    TR_GLOW_RGB(165, 215, 70),   // 5: 青柠 Lime
    TR_GLOW_RGB(55, 180, 165),   // 6: 蓝绿 Teal
    TR_GLOW_RGB(225, 160, 95)    // 7: 暖杏 Apricot
};

enum struct TR_GlowTankState
{
    int targetUid;          // 当前确认锁定的生还者 userid（0=未锁定）
    int colorSlot;          // 0..7 或 -1（溢出/无配对色）
    int throwGen;           // 绑定的投石代次（0=无有效锁定）
    int redProxyRef;        // 代理 #1：仅被锁者可见红边
    int pairProxyRef;       // 代理 #2：仅其他真人生还者可见配对色
}

enum struct TR_GlowSurvivorState
{
    int targetUid;          // 记录生还者 userid，便于断线/离队后可靠清理
    int colorSlot;          // 0..7 或 -1
    int pairProxyRef;       // 最多 1 个代理：仅其他真人生还者可见配对色
}

enum struct TR_GlowProxyMeta
{
    int entRef;
    int role;
    int subjectRef;
    int replicaSlot;        // 仅补位 Tank 代理有效（0..31），否则 -1
    int targetUid;
    int colorSlot;
    int throwGen;           // 绑定的投石代次，防陈旧代理跨投石放行
}

// 目标组分配表（最多 8 种配对色）
int g_iGlowGroupUid[TR_GLOW_MAX_COLORS];
int g_iGlowGroupRefs[TR_GLOW_MAX_COLORS];

TR_GlowTankState     g_GlowNativeTank[MAXPLAYERS + 1];
TR_GlowTankState     g_GlowReplicaTank[TR_GLOW_MAX_REPLICAS];
TR_GlowSurvivorState g_GlowSurvivor[MAXPLAYERS + 1];
TR_GlowProxyMeta     g_GlowProxies[TR_GLOW_MAX_PROXY_SLOTS];
int                  g_iGlowEntToMeta[TR_GLOW_MAX_EDICTS]; // O(1) 实体索引 -> 代理元数据映射

int g_iGlowNextThrowGen;
int g_iGlowLiveProxies;
int g_iGlowPeakProxies;
int g_iGlowOverflowCount;
int g_iGlowTraceIgnoreSubject;
int g_iGlowTraceIgnoreViewer;

// 原版 AI Tank 真实投石目标只读观测状态（TankAttack::ThrowRockHelper +0x34，严格绑定投石代次）
DynamicDetour g_hGlowThrowHelperDetour;
bool g_bGlowNativeTargetReady;
bool g_bGlowPendingOsRetry;
int  g_iGlowNativeTargetOffset = -1;
char g_sGlowNativeStatus[160] = "未初始化";

// 当前活跃投石代次与对应 Tank userid
int   g_iGlowActiveThrowGen[MAXPLAYERS + 1];
int   g_iGlowActiveThrowTankUid[MAXPLAYERS + 1];

// 仅属于当前投石代次的已验证观测
int   g_iGlowObservedThrowGen[MAXPLAYERS + 1];
int   g_iGlowObservedTankUid[MAXPLAYERS + 1];
int   g_iGlowObservedRawHandle[MAXPLAYERS + 1];
int   g_iGlowObservedTargetUid[MAXPLAYERS + 1];
float g_fGlowObservedTime[MAXPLAYERS + 1];

// 同步调用栈交接槽（仅在 ThrowRockHelper Pre..Post 执行期间有效）
int   g_iGlowInHelperTank;
int   g_iGlowInHelperTankUid;
int   g_iGlowInHelperRaw = -1;
int   g_iGlowInHelperTargetUid;

int   g_iStatGlowNativeReads;
int   g_iStatGlowNativeLockHits;

int TR_Glow_AllocThrowGen()
{
    return ++g_iGlowNextThrowGen;
}

#define TR_GLOW_NATIVE_TARGET_EXPECTED_OFF  0x34
#define TR_GLOW_NATIVE_READ_REL_EXPECTED    0xD8
#define TR_GLOW_NATIVE_OBS_TTL              0.60

// 原版真实投石目标路径是否已通过严格平台+构建+入口字节校验并启用
stock bool TR_Glow_IsNativeTargetPathEnabled()
{
    return g_bGlowNativeTargetReady;
}

bool TR_Glow_VerifyCodeBytes(Address baseAddr, int relOffset, const int[] expected, int count, int &mismatchOff, int &expectedByte, int &actualByte)
{
    for (int i = 0; i < count; i++)
    {
        int actual = LoadFromAddress(baseAddr + view_as<Address>(relOffset + i), NumberType_Int8) & 0xFF;
        if (actual != expected[i])
        {
            mismatchOff = relOffset + i;
            expectedByte = expected[i];
            actualByte = actual;
            return false;
        }
    }
    return true;
}

// 精确解析 Source 引擎 32 位 CBaseHandle（任意在场玩家）：
//   bits  0..11 (0xFFF)  = entIndex
//   bits 12..31 (>>> 12) = serialNumber
// 并通过 SourceMod EntRefToEntIndex((1<<31)|(serial<<12)|entIndex) 核对引擎实况序列号表，
// 确保 0xFFFFFFFF、失效或已复用槽位绝不误判。
int TR_Glow_DecodePlayerHandle(int rawHandle, int &outEntIdx, int &outSerial)
{
    outEntIdx = -1;
    outSerial = 0;
    if (rawHandle == -1 || rawHandle == 0)
    {
        return 0;
    }

    int entIdx = rawHandle & 0xFFF;
    int serial = (rawHandle >>> 12) & 0xFFFFF;
    outEntIdx = entIdx;
    outSerial = serial;

    if (entIdx < 1 || entIdx > MaxClients)
    {
        return 0;
    }
    // SourceMod EntRef 使用 bit31=1、bits12..30 为 19 位 serial；超出范围保守拒绝，绝不猜测
    if (serial <= 0 || serial > 0x7FFFF)
    {
        return 0;
    }

    int smEntRef = (1 << 31) | (serial << 12) | entIdx;
    int resolved = EntRefToEntIndex(smEntRef);
    if (resolved != entIdx)
    {
        return 0;
    }
    if (!IsClientInGame(entIdx))
    {
        return 0;
    }
    return entIdx;
}

int TR_Glow_DecodeSurvivorHandle(int rawHandle, int &outEntIdx, int &outSerial)
{
    int entIdx = TR_Glow_DecodePlayerHandle(rawHandle, outEntIdx, outSerial);
    if (entIdx <= 0 || GetClientTeam(entIdx) != 2 || !IsPlayerAlive(entIdx))
    {
        return 0;
    }
    return entIdx;
}

void TR_Glow_ClearNativeObservation(int tank)
{
    if (tank < 1 || tank > MaxClients)
    {
        return;
    }
    g_iGlowObservedThrowGen[tank] = 0;
    g_iGlowObservedTankUid[tank] = 0;
    g_iGlowObservedRawHandle[tank] = -1;
    g_iGlowObservedTargetUid[tank] = 0;
    g_fGlowObservedTime[tank] = 0.0;
    if (g_iGlowInHelperTank == tank)
    {
        g_iGlowInHelperTank = 0;
        g_iGlowInHelperTankUid = 0;
        g_iGlowInHelperRaw = -1;
        g_iGlowInHelperTargetUid = 0;
    }
}

void TR_Glow_InstallNativeTargetDetour()
{
    g_bGlowNativeTargetReady = false;
    g_bGlowPendingOsRetry = false;
    g_iGlowNativeTargetOffset = -1;

    // 1) 严格限定 Windows 平台（结合已有 L4D_GetServerOS 守卫；0=Windows, 1=Linux）
    if (GetFeatureStatus(FeatureType_Native, "L4D_GetServerOS") != FeatureStatus_Available)
    {
        g_bGlowPendingOsRetry = true;
        strcopy(g_sGlowNativeStatus, sizeof(g_sGlowNativeStatus), "关闭（L4D_GetServerOS 不可用，无法验证 Windows 平台）");
        TR_Log("仇恨提示原版路径降级关闭：%s", g_sGlowNativeStatus);
        return;
    }
    int serverOs = L4D_GetServerOS();
    if (serverOs != 0)
    {
        Format(g_sGlowNativeStatus, sizeof(g_sGlowNativeStatus), "关闭（非 Windows 平台 os=%d）", serverOs);
        TR_Log("仇恨提示原版路径降级关闭：%s", g_sGlowNativeStatus);
        return;
    }

    // 2) 校验 DHooks 扩展可用
    if (GetFeatureStatus(FeatureType_Native, "DynamicDetour.DynamicDetour") != FeatureStatus_Available)
    {
        strcopy(g_sGlowNativeStatus, sizeof(g_sGlowNativeStatus), "关闭（DHooks DynamicDetour 不可用）");
        TR_Log("仇恨提示原版路径降级关闭：%s", g_sGlowNativeStatus);
        return;
    }

    // 3) 加载独立 gamedata/tankrun_rocktarget.txt（先检查文件存在性，并使用不抛异常的 LoadGameConfigFile）
    char gdPath[PLATFORM_MAX_PATH];
    BuildPath(Path_SM, gdPath, sizeof(gdPath), "gamedata/tankrun_rocktarget.txt");
    if (!FileExists(gdPath))
    {
        strcopy(g_sGlowNativeStatus, sizeof(g_sGlowNativeStatus), "关闭（缺少 gamedata/tankrun_rocktarget.txt）");
        TR_Log("仇恨提示原版路径降级关闭：%s", g_sGlowNativeStatus);
        return;
    }

    GameData gd = LoadGameConfigFile("tankrun_rocktarget");
    if (gd == null)
    {
        strcopy(g_sGlowNativeStatus, sizeof(g_sGlowNativeStatus), "关闭（gamedata/tankrun_rocktarget.txt 加载或解析失败）");
        TR_Log("仇恨提示原版路径降级关闭：%s", g_sGlowNativeStatus);
        return;
    }

    Address fnAddr = gd.GetMemSig("TankRun::TankAttack::ThrowRockHelper");
    int targetOff = gd.GetOffset("TankAttack::m_hTarget");
    int readRel = gd.GetOffset("TankAttack::ThrowRockHelper_ReadTargetRel");
    delete gd;

    if (fnAddr == Address_Null)
    {
        strcopy(g_sGlowNativeStatus, sizeof(g_sGlowNativeStatus), "关闭（ThrowRockHelper 签名未匹配当前 server.dll）");
        TR_Log("仇恨提示原版路径降级关闭：%s", g_sGlowNativeStatus);
        return;
    }
    if (targetOff != TR_GLOW_NATIVE_TARGET_EXPECTED_OFF || readRel != TR_GLOW_NATIVE_READ_REL_EXPECTED)
    {
        Format(g_sGlowNativeStatus, sizeof(g_sGlowNativeStatus),
            "关闭（gamedata 偏移校验失败 targetOff=%d readRel=%d）", targetOff, readRel);
        TR_Log("仇恨提示原版路径降级关闭：%s", g_sGlowNativeStatus);
        return;
    }

    // 4) 安装 detour 前执行只读入口与关键特征指令字节校验（绑定已实证 PatchVersion=2.2.4.3 Windows server.dll）
    static const int sigEntry[]      = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x3C };
    static const int sigPrologue[]   = { 0x83, 0x78, 0x30, 0x00, 0x57, 0x8B, 0xF9, 0x89, 0x7D, 0xFC };
    static const int sigAbility[]    = { 0x8B, 0x5D, 0x08, 0x8B, 0x83, 0xD0, 0x30, 0x00, 0x00, 0x83, 0xF8, 0xFF };
    static const int sigIsActive[]   = { 0x8B, 0x06, 0x8B, 0x90, 0x50, 0x03, 0x00, 0x00, 0x8B, 0xCE, 0xFF, 0xD2 };
    int sigReadTarget[8]             = { 0x8B, 0xF7, 0x83, 0xC6, 0x34, 0x8B, 0xCE, 0xE8 };
    sigReadTarget[4] = targetOff & 0xFF;

    int badOff = 0, expB = 0, actB = 0;
    bool bytesOk = TR_Glow_VerifyCodeBytes(fnAddr, 0x00, sigEntry, sizeof(sigEntry), badOff, expB, actB)
        && TR_Glow_VerifyCodeBytes(fnAddr, 0x0B, sigPrologue, sizeof(sigPrologue), badOff, expB, actB)
        && TR_Glow_VerifyCodeBytes(fnAddr, 0x60, sigAbility, sizeof(sigAbility), badOff, expB, actB)
        && TR_Glow_VerifyCodeBytes(fnAddr, 0xC4, sigIsActive, sizeof(sigIsActive), badOff, expB, actB)
        && TR_Glow_VerifyCodeBytes(fnAddr, readRel, sigReadTarget, sizeof(sigReadTarget), badOff, expB, actB);

    if (!bytesOk)
    {
        Format(g_sGlowNativeStatus, sizeof(g_sGlowNativeStatus),
            "关闭（入口/指令字节校验不符 +0x%02X 期望=0x%02X 实际=0x%02X）", badOff, expB, actB);
        TR_Log("仇恨提示原版路径降级关闭：%s", g_sGlowNativeStatus);
        return;
    }

    // 5) 安装只读 DHooks Pre+Post detour：__thiscall void TankAttack::ThrowRockHelper(TankAttack* this, Tank* tank)
    g_hGlowThrowHelperDetour = new DynamicDetour(fnAddr, CallConv_THISCALL, ReturnType_Void, ThisPointer_Address);
    if (g_hGlowThrowHelperDetour == null)
    {
        strcopy(g_sGlowNativeStatus, sizeof(g_sGlowNativeStatus), "关闭（DynamicDetour 创建失败）");
        TR_Log("仇恨提示原版路径降级关闭：%s", g_sGlowNativeStatus);
        return;
    }
    g_hGlowThrowHelperDetour.AddParam(HookParamType_CBaseEntity);
    if (!g_hGlowThrowHelperDetour.Enable(Hook_Pre, TR_Glow_DetourThrowRockHelperPre)
        || !g_hGlowThrowHelperDetour.Enable(Hook_Post, TR_Glow_DetourThrowRockHelperPost))
    {
        g_hGlowThrowHelperDetour.Disable(Hook_Pre, TR_Glow_DetourThrowRockHelperPre);
        g_hGlowThrowHelperDetour.Disable(Hook_Post, TR_Glow_DetourThrowRockHelperPost);
        delete g_hGlowThrowHelperDetour;
        g_hGlowThrowHelperDetour = null;
        strcopy(g_sGlowNativeStatus, sizeof(g_sGlowNativeStatus), "关闭（DynamicDetour 启用失败）");
        TR_Log("仇恨提示原版路径降级关闭：%s", g_sGlowNativeStatus);
        return;
    }

    g_iGlowNativeTargetOffset = targetOff;
    g_bGlowNativeTargetReady = true;
    Format(g_sGlowNativeStatus, sizeof(g_sGlowNativeStatus),
        "已启用（ThrowRockHelper 只读 Pre/Post detour，m_hTarget=+0x%02X 字节校验通过）", targetOff);
    TR_Log("仇恨提示原版路径：%s", g_sGlowNativeStatus);
}

void TR_Glow_UninstallNativeTargetDetour()
{
    g_bGlowNativeTargetReady = false;
    g_iGlowNativeTargetOffset = -1;
    g_iGlowInHelperTank = 0;
    g_iGlowInHelperTankUid = 0;
    g_iGlowInHelperRaw = -1;
    g_iGlowInHelperTargetUid = 0;
    if (g_hGlowThrowHelperDetour != null)
    {
        g_hGlowThrowHelperDetour.Disable(Hook_Pre, TR_Glow_DetourThrowRockHelperPre);
        g_hGlowThrowHelperDetour.Disable(Hook_Post, TR_Glow_DetourThrowRockHelperPost);
        delete g_hGlowThrowHelperDetour;
        g_hGlowThrowHelperDetour = null;
    }
}

public MRESReturn TR_Glow_DetourThrowRockHelperPre(Address pTankAttack, DHookParam hParams)
{
    // 严格只读观察：绝不修改参数、返回值或引擎内存
    if (!g_bGlowNativeTargetReady || pTankAttack == Address_Null || g_iGlowNativeTargetOffset != TR_GLOW_NATIVE_TARGET_EXPECTED_OFF)
    {
        return MRES_Ignored;
    }
    if (!TR_FeatureEnabled() || g_bRoundEnded)
    {
        return MRES_Ignored;
    }
    // 仇恨提示与骗饼处死彼此完全解耦：仅当两者均关闭时才跳过只读观察
    if (!TR_RockGlowEnabled() && !TR_BaitRockEnabled())
    {
        return MRES_Ignored;
    }

    int tank = hParams.Get(1);
    if (tank < 1 || tank > MaxClients || !TR_IsAiTank(tank))
    {
        return MRES_Ignored;
    }

    int rawHandle = LoadFromAddress(pTankAttack + view_as<Address>(g_iGlowNativeTargetOffset), NumberType_Int32);
    int entIdx = -1, serial = 0;
    int surv = TR_Glow_DecodeSurvivorHandle(rawHandle, entIdx, serial);
    int uid = (surv > 0) ? GetClientUserId(surv) : 0;
    int tankUid = GetClientUserId(tank);
    g_iStatGlowNativeReads++;

    // 骗饼观测与仇恨描边开关完全解耦：只要 ThrowRockHelper 只读路径就绪即实时投喂当前观测（含失效清零）
    TR_BaitRockObserve(tank, surv);

    // 记录同步调用栈上下文（仅本次 ThrowRockHelper Pre..Post 内有效）
    g_iGlowInHelperTank = tank;
    g_iGlowInHelperTankUid = tankUid;
    g_iGlowInHelperRaw = rawHandle;
    g_iGlowInHelperTargetUid = uid;

    // 仅当该 Tank 正处于已激活的投石代次内时，才写入代次绑定观测，绝不跨投石预存旧观测
    if (g_iGlowActiveThrowGen[tank] > 0 && g_iGlowActiveThrowTankUid[tank] == tankUid
        && TR_Glow_IsNativeRockLockActive(tank))
    {
        int prevUid = g_iGlowObservedTargetUid[tank];
        int prevRaw = g_iGlowObservedRawHandle[tank];
        g_iGlowObservedThrowGen[tank] = g_iGlowActiveThrowGen[tank];
        g_iGlowObservedTankUid[tank] = tankUid;
        g_iGlowObservedRawHandle[tank] = rawHandle;
        g_iGlowObservedTargetUid[tank] = uid;
        g_fGlowObservedTime[tank] = GetGameTime();

        if (TR_RockGlowEnabled())
        {
            if (prevUid != uid || g_GlowNativeTank[tank].targetUid != uid)
            {
                g_iStatGlowNativeLockHits++;
                TR_LogEvent("仇恨提示原版观测（ThrowRockHelper）：Tank#%d gen=%d raw=0x%08X (idx=%d ser=%d) -> surv=#%d uid=%d (prevRaw=0x%08X prevUid=%d)",
                    tank, g_iGlowActiveThrowGen[tank], rawHandle, entIdx, serial, surv, uid, prevRaw, prevUid);
            }
            TR_Glow_UpdateNativeTank(tank);
        }
    }
    else if (g_GlowNativeTank[tank].targetUid != 0 || g_iGlowActiveThrowGen[tank] != 0)
    {
        TR_Glow_EndNativeThrow(tank);
    }
    return MRES_Ignored;
}

public MRESReturn TR_Glow_DetourThrowRockHelperPost(Address pTankAttack, DHookParam hParams)
{
    #pragma unused pTankAttack, hParams
    g_iGlowInHelperTank = 0;
    g_iGlowInHelperTankUid = 0;
    g_iGlowInHelperRaw = -1;
    g_iGlowInHelperTargetUid = 0;
    return MRES_Ignored;
}

bool TR_Glow_IsNativeRockLockActive(int tank)
{
    if (tank < 1 || tank > MaxClients || !TR_IsAiTank(tank))
    {
        return false;
    }
    if (g_iGlowActiveThrowGen[tank] <= 0 || g_iGlowActiveThrowTankUid[tank] != GetClientUserId(tank))
    {
        return false;
    }
    float now = GetGameTime();
    // 仅于已确认的投石锁定段显示（CThrow 触发 PLAYERANIMEVENT_ROCK_THROW 后至离手/动画中断/取消/超时）
    if (g_fTankPendingActivation[tank] > 0.0 && (now - g_fTankPendingActivation[tank]) <= TR_ROCK_WINDUP_LIMIT)
    {
        return true;
    }
    if (g_RockMotion[tank].windupUntil > now)
    {
        return true;
    }
    return false;
}

int TR_Glow_GetVerifiedNativeRockTarget(int tank)
{
    if (!g_bGlowNativeTargetReady || tank < 1 || tank > MaxClients || !TR_IsAiTank(tank))
    {
        return 0;
    }
    int activeGen = g_iGlowActiveThrowGen[tank];
    int tankUid = GetClientUserId(tank);
    if (activeGen <= 0 || tankUid <= 0 || g_iGlowActiveThrowTankUid[tank] != tankUid)
    {
        return 0;
    }
    // 严格要求观测与当前投石代次及当前 Tank userid 完全匹配，杜绝跨投石/跨接管复用旧观测
    if (g_iGlowObservedThrowGen[tank] != activeGen || g_iGlowObservedTankUid[tank] != tankUid)
    {
        return 0;
    }
    int uid = g_iGlowObservedTargetUid[tank];
    if (uid <= 0)
    {
        return 0;
    }
    if ((GetGameTime() - g_fGlowObservedTime[tank]) > TR_GLOW_NATIVE_OBS_TTL)
    {
        return 0;
    }
    int surv = GetClientOfUserId(uid);
    if (!TR_IsSurvivor(surv))
    {
        return 0;
    }
    return uid;
}

// 开始一次新的原版投石代次：先彻底清理旧代次与旧观测，仅当当前同步处于该 Tank 的 ThrowRockHelper 调用栈内时采纳即时目标
void TR_Glow_BeginNativeThrow(int tank)
{
    if (tank < 1 || tank > MaxClients || !TR_IsAiTank(tank))
    {
        return;
    }
    int tankUid = GetClientUserId(tank);
    bool hasSyncObs = (g_iGlowInHelperTank == tank && g_iGlowInHelperTankUid == tankUid && g_iGlowInHelperTargetUid > 0);
    int syncRaw = g_iGlowInHelperRaw;
    int syncTargetUid = g_iGlowInHelperTargetUid;

    TR_Glow_ClearTankState(g_GlowNativeTank[tank]);
    g_iGlowObservedThrowGen[tank] = 0;
    g_iGlowObservedTankUid[tank] = 0;
    g_iGlowObservedRawHandle[tank] = -1;
    g_iGlowObservedTargetUid[tank] = 0;
    g_fGlowObservedTime[tank] = 0.0;

    int newGen = TR_Glow_AllocThrowGen();
    g_iGlowActiveThrowGen[tank] = newGen;
    g_iGlowActiveThrowTankUid[tank] = tankUid;

    if (hasSyncObs)
    {
        g_iGlowObservedThrowGen[tank] = newGen;
        g_iGlowObservedTankUid[tank] = tankUid;
        g_iGlowObservedRawHandle[tank] = syncRaw;
        g_iGlowObservedTargetUid[tank] = syncTargetUid;
        g_fGlowObservedTime[tank] = GetGameTime();
    }

    TR_Glow_UpdateNativeTank(tank);
}

// 结束当前原版投石代次：立即销毁代理、失效代次并清空全部观测，杜绝任何跨投石/接管残留
void TR_Glow_EndNativeThrow(int tank)
{
    if (tank < 1 || tank > MaxClients)
    {
        return;
    }
    g_iGlowActiveThrowGen[tank] = 0;
    g_iGlowActiveThrowTankUid[tank] = 0;
    TR_Glow_ClearNativeObservation(tank);
    TR_Glow_ClearTankState(g_GlowNativeTank[tank]);
}

bool TR_RockGlowEnabled()
{
    return TR_IsTankRunCoop() && g_cvRockTargetGlow != null && g_cvRockTargetGlow.BoolValue;
}

// ---------------------------------------------------------------- 代理元数据表（O(1) 实体映射）
void TR_Glow_ResetEntMap()
{
    for (int e = 0; e < TR_GLOW_MAX_EDICTS; e++)
    {
        g_iGlowEntToMeta[e] = -1;
    }
}

int TR_Glow_FindProxyMetaByEntity(int entity)
{
    if (entity <= MaxClients || entity >= TR_GLOW_MAX_EDICTS)
    {
        return -1;
    }
    int idx = g_iGlowEntToMeta[entity];
    if (idx >= 0 && idx < TR_GLOW_MAX_PROXY_SLOTS
        && g_GlowProxies[idx].role != TR_GLOW_ROLE_NONE
        && g_GlowProxies[idx].entRef == EntIndexToEntRef(entity))
    {
        return idx;
    }
    return -1;
}

int TR_Glow_AllocProxyMeta()
{
    for (int i = 0; i < TR_GLOW_MAX_PROXY_SLOTS; i++)
    {
        if (g_GlowProxies[i].role == TR_GLOW_ROLE_NONE)
        {
            return i;
        }
    }
    return -1;
}

void TR_Glow_ClearProxyMetaSlot(int idx)
{
    if (idx < 0 || idx >= TR_GLOW_MAX_PROXY_SLOTS)
    {
        return;
    }
    int ent = EntRefToEntIndex(g_GlowProxies[idx].entRef);
    if (ent > MaxClients && ent < TR_GLOW_MAX_EDICTS && g_iGlowEntToMeta[ent] == idx)
    {
        g_iGlowEntToMeta[ent] = -1;
    }
    if (g_GlowProxies[idx].role != TR_GLOW_ROLE_NONE)
    {
        g_iGlowLiveProxies--;
        if (g_iGlowLiveProxies < 0)
        {
            g_iGlowLiveProxies = 0;
        }
    }
    TR_GlowProxyMeta empty;
    empty.replicaSlot = -1;
    g_GlowProxies[idx] = empty;
}

void TR_Glow_DestroyProxyRef(int &proxyRef)
{
    if (proxyRef == 0 || proxyRef == INVALID_ENT_REFERENCE)
    {
        proxyRef = 0;
        return;
    }
    int ent = EntRefToEntIndex(proxyRef);
    for (int i = 0; i < TR_GLOW_MAX_PROXY_SLOTS; i++)
    {
        if (g_GlowProxies[i].role != TR_GLOW_ROLE_NONE && g_GlowProxies[i].entRef == proxyRef)
        {
            TR_Glow_ClearProxyMetaSlot(i);
            break;
        }
    }
    proxyRef = 0;
    if (ent > MaxClients && IsValidEntity(ent))
    {
        SDKUnhook(ent, SDKHook_SetTransmit, TR_Glow_OnTransmit);
        RemoveEntity(ent);
    }
}

void TR_Glow_OnEntityDestroyed(int entity)
{
    if (entity <= MaxClients)
    {
        return;
    }
    int idx = TR_Glow_FindProxyMetaByEntity(entity);
    if (idx >= 0)
    {
        TR_Glow_ClearProxyMetaSlot(idx);
    }
}

// ---------------------------------------------------------------- 跟模代理创建（绝不改写真实主体 glow 属性）
int TR_Glow_CreateProxy(int subject, int role, int targetUid, int colorSlot, int colorOverride, int throwGen, int replicaSlot = -1)
{
    if (!TR_IsMapRunning() || subject <= 0 || !IsValidEntity(subject))
    {
        return 0;
    }
    int metaIdx = TR_Glow_AllocProxyMeta();
    if (metaIdx < 0)
    {
        return 0;
    }

    char model[PLATFORM_MAX_PATH];
    model[0] = '\0';
    if (HasEntProp(subject, Prop_Data, "m_ModelName"))
    {
        GetEntPropString(subject, Prop_Data, "m_ModelName", model, sizeof(model));
    }
    if (model[0] == '\0')
    {
        return 0;
    }

    int proxy = CreateEntityByName("prop_dynamic_override");
    if (proxy <= MaxClients || !IsValidEntity(proxy))
    {
        return 0;
    }

    float origin[3], angles[3];
    if (subject <= MaxClients)
    {
        GetClientAbsOrigin(subject, origin);
        GetClientAbsAngles(subject, angles);
    }
    else
    {
        GetEntPropVector(subject, Prop_Data, "m_vecAbsOrigin", origin);
        GetEntPropVector(subject, Prop_Data, "m_angAbsRotation", angles);
    }

    DispatchKeyValue(proxy, "model", model);
    DispatchKeyValue(proxy, "solid", "0");
    DispatchKeyValue(proxy, "disableshadows", "1");
    DispatchKeyValue(proxy, "disablereceiveshadows", "1");
    DispatchKeyValue(proxy, "DisableBoneFollowers", "1");
    DispatchKeyValue(proxy, "targetname", "tankrun_glow_proxy");
    TeleportEntity(proxy, origin, angles, NULL_VECTOR);

    if (!DispatchSpawn(proxy))
    {
        RemoveEntity(proxy);
        return 0;
    }
    ActivateEntity(proxy);

    // 绑定父实体并启用骨骼合并，仅影响代理实体自身
    SetVariantString("!activator");
    if (!AcceptEntityInput(proxy, "SetParent", subject, proxy))
    {
        RemoveEntity(proxy);
        return 0;
    }

    // EF_BONEMERGE (1) | EF_NOSHADOW (16) | EF_NORECEIVESHADOW (64) | EF_PARENT_ANIMATES (512)
    SetEntProp(proxy, Prop_Send, "m_fEffects", (1 << 0) | (1 << 4) | (1 << 6) | (1 << 9));
    // 严格非碰撞，避免阻挡子弹/石头/导航 trace
    if (HasEntProp(proxy, Prop_Send, "m_nSolidType"))
    {
        SetEntProp(proxy, Prop_Send, "m_nSolidType", 0);
    }
    if (HasEntProp(proxy, Prop_Send, "m_usSolidFlags"))
    {
        SetEntProp(proxy, Prop_Send, "m_usSolidFlags", 4);
    }
    if (HasEntProp(proxy, Prop_Data, "m_CollisionGroup"))
    {
        SetEntProp(proxy, Prop_Data, "m_CollisionGroup", 11);
    }

    // 隐藏代理自身重复网格，仅通过代理呈现描边（原主体 m_iGlowType/m_glowColorOverride 绝不触碰）
    SetEntityRenderMode(proxy, RENDER_TRANSCOLOR);
    SetEntityRenderColor(proxy, 255, 255, 255, 0);

    // 故意不设置 FL_EDICT_ALWAYS，遵循引擎 PVS 裁剪；配合 SetTransmit 实时零正缓存全包络保守可见门阻断遮挡泄露
    SetEntProp(proxy, Prop_Send, "m_iGlowType", 3);
    SetEntProp(proxy, Prop_Send, "m_glowColorOverride", colorOverride);
    SetEntProp(proxy, Prop_Send, "m_nGlowRange", 0);
    SetEntProp(proxy, Prop_Send, "m_nGlowRangeMin", 0);

    if (!SDKHookEx(proxy, SDKHook_SetTransmit, TR_Glow_OnTransmit))
    {
        RemoveEntity(proxy);
        return 0;
    }

    int ref = EntIndexToEntRef(proxy);
    g_GlowProxies[metaIdx].entRef = ref;
    g_GlowProxies[metaIdx].role = role;
    g_GlowProxies[metaIdx].subjectRef = EntIndexToEntRef(subject);
    g_GlowProxies[metaIdx].replicaSlot = replicaSlot;
    g_GlowProxies[metaIdx].targetUid = targetUid;
    g_GlowProxies[metaIdx].colorSlot = colorSlot;
    g_GlowProxies[metaIdx].throwGen = throwGen;
    if (proxy > MaxClients && proxy < TR_GLOW_MAX_EDICTS)
    {
        g_iGlowEntToMeta[proxy] = metaIdx;
    }

    g_iGlowLiveProxies++;
    if (g_iGlowLiveProxies > g_iGlowPeakProxies)
    {
        g_iGlowPeakProxies = g_iGlowLiveProxies;
    }
    return ref;
}

void TR_Glow_UpdateProxyMeta(int proxyRef, int targetUid, int colorSlot, int colorOverride, int throwGen)
{
    int ent = EntRefToEntIndex(proxyRef);
    if (ent <= MaxClients || !IsValidEntity(ent))
    {
        return;
    }
    SetEntProp(ent, Prop_Send, "m_glowColorOverride", colorOverride);
    int idx = TR_Glow_FindProxyMetaByEntity(ent);
    if (idx >= 0)
    {
        g_GlowProxies[idx].targetUid = targetUid;
        g_GlowProxies[idx].colorSlot = colorSlot;
        g_GlowProxies[idx].throwGen = throwGen;
    }
}

// ---------------------------------------------------------------- 严格保守非泄露可见门（墙/门/拐角/部分遮挡零泄露，零正缓存）
public bool TR_Glow_VisTraceFilter(int entity, int contentsMask, any data)
{
    #pragma unused contentsMask, data
    if (entity == g_iGlowTraceIgnoreViewer || entity == g_iGlowTraceIgnoreSubject)
    {
        return false;
    }
    // 忽略玩家模型与本插件 glow proxy，保留世界墙体、闭门、门框、拐角与不透明障碍物遮挡
    if (entity >= 1 && entity <= MaxClients)
    {
        return false;
    }
    if (entity > MaxClients && TR_Glow_FindProxyMetaByEntity(entity) >= 0)
    {
        return false;
    }
    // 忽略主体自身附属碰撞体/挂载件（如 phys_bone_follower、补位 Tank 手持石块），绝不忽略任何外部墙体/门/掩体
    if (entity > MaxClients && g_iGlowTraceIgnoreSubject > 0 && IsValidEntity(entity))
    {
        if (HasEntProp(entity, Prop_Send, "m_hOwnerEntity")
            && GetEntPropEnt(entity, Prop_Send, "m_hOwnerEntity") == g_iGlowTraceIgnoreSubject)
        {
            return false;
        }
        if (HasEntProp(entity, Prop_Data, "m_hMoveParent")
            && GetEntPropEnt(entity, Prop_Data, "m_hMoveParent") == g_iGlowTraceIgnoreSubject)
        {
            return false;
        }
    }
    return true;
}

void TR_Glow_GetSubjectBounds(int subject, float origin[3], float mins[3], float maxs[3])
{
    if (subject <= MaxClients)
    {
        GetClientAbsOrigin(subject, origin);
        GetClientMins(subject, mins);
        GetClientMaxs(subject, maxs);
        return;
    }
    GetEntPropVector(subject, Prop_Data, "m_vecAbsOrigin", origin);
    bool gotBounds = false;
    if (HasEntProp(subject, Prop_Send, "m_vecMins") && HasEntProp(subject, Prop_Send, "m_vecMaxs"))
    {
        GetEntPropVector(subject, Prop_Send, "m_vecMins", mins);
        GetEntPropVector(subject, Prop_Send, "m_vecMaxs", maxs);
        if ((maxs[0] - mins[0]) > 1.0 && (maxs[1] - mins[1]) > 1.0 && (maxs[2] - mins[2]) > 1.0)
        {
            gotBounds = true;
        }
    }
    if (!gotBounds)
    {
        mins[0] = -24.0; mins[1] = -24.0; mins[2] = 0.0;
        maxs[0] =  24.0; maxs[1] =  24.0; maxs[2] = 74.0;
    }
    else
    {
        // 补位 Tank 使用 prop_dynamic_override 渲染模型（hulk.mdl）时，模型渲染包围盒可能向地下伸出负 Z
        // 或远超实际碰撞体积，钳制到 Tank 物理包围盒范围以避免误把地板当遮挡。
        if (mins[0] < -24.0) mins[0] = -24.0;
        if (maxs[0] >  24.0) maxs[0] =  24.0;
        if (mins[1] < -24.0) mins[1] = -24.0;
        if (maxs[1] >  24.0) maxs[1] =  24.0;
        if (mins[2] <   0.0) mins[2] =   0.0;
        if (maxs[2] >  74.0) maxs[2] =  74.0;
    }
}

// 严格保守 3D 包络判定：要求主体 3D AABB 中心 + 全部 8 个包络角点均无任何遮挡碰撞（!TR_StartSolid && !TR_DidHit）。
// 先查中心点实现遮挡场景 1-trace 快速拒绝；任意角点被墙/闭门/拐角/柱子/掩体遮挡即立即返回 false，
// 绝不放行完整 m_iGlowType=3 模型泄露被遮挡轮廓，且不使用 fraction>=0.99 放宽或任何正结果缓存。
bool TR_Glow_IsSubjectVisibleToViewer(int viewer, int subject)
{
    if (viewer < 1 || viewer > MaxClients || !IsClientInGame(viewer) || subject <= 0 || !IsValidEntity(subject))
    {
        return false;
    }

    float eye[3], origin[3], mins[3], maxs[3], sample[3];
    GetClientEyePosition(viewer, eye);
    TR_Glow_GetSubjectBounds(subject, origin, mins, maxs);

    float minX = origin[0] + mins[0] + 2.0;
    float maxX = origin[0] + maxs[0] - 2.0;
    if (minX > maxX)
    {
        minX = origin[0];
        maxX = origin[0];
    }
    float minY = origin[1] + mins[1] + 2.0;
    float maxY = origin[1] + maxs[1] - 2.0;
    if (minY > maxY)
    {
        minY = origin[1];
        maxY = origin[1];
    }

    float midZ = origin[2] + (mins[2] + maxs[2]) * 0.5;
    float lowZ = origin[2] + mins[2] + 6.0;
    if (lowZ > midZ)
    {
        lowZ = midZ;
    }
    float topZ = origin[2] + maxs[2] - 4.0;
    if (topZ < midZ)
    {
        topZ = midZ;
    }

    float samples[9][3];
    // 0: 中心点（全遮挡快速拒绝）
    samples[0][0] = origin[0]; samples[0][1] = origin[1]; samples[0][2] = midZ;
    // 1..4: 上部 4 角
    samples[1][0] = minX;      samples[1][1] = minY;      samples[1][2] = topZ;
    samples[2][0] = minX;      samples[2][1] = maxY;      samples[2][2] = topZ;
    samples[3][0] = maxX;      samples[3][1] = minY;      samples[3][2] = topZ;
    samples[4][0] = maxX;      samples[4][1] = maxY;      samples[4][2] = topZ;
    // 5..8: 下部 4 角
    samples[5][0] = minX;      samples[5][1] = minY;      samples[5][2] = lowZ;
    samples[6][0] = minX;      samples[6][1] = maxY;      samples[6][2] = lowZ;
    samples[7][0] = maxX;      samples[7][1] = minY;      samples[7][2] = lowZ;
    samples[8][0] = maxX;      samples[8][1] = maxY;      samples[8][2] = lowZ;

    g_iGlowTraceIgnoreViewer = viewer;
    g_iGlowTraceIgnoreSubject = subject;
    bool allClear = true;
    for (int i = 0; i < 9; i++)
    {
        sample[0] = samples[i][0];
        sample[1] = samples[i][1];
        sample[2] = samples[i][2];
        TR_TraceRayFilter(eye, sample, MASK_VISIBLE, RayType_EndPoint, TR_Glow_VisTraceFilter, 0);
        if (TR_StartSolid() || TR_DidHit())
        {
            allClear = false;
            break;
        }
    }
    g_iGlowTraceIgnoreViewer = 0;
    g_iGlowTraceIgnoreSubject = 0;
    return allClear;
}

public Action TR_Glow_OnTransmit(int proxy, int client)
{
    if (!TR_RockGlowEnabled())
    {
        return Plugin_Handled;
    }
    // 严格受众门：仅游戏内活着的真人生还者可见（排除观察者、死亡者、感染者、Bot）
    if (client < 1 || client > MaxClients || !IsClientInGame(client)
        || IsFakeClient(client) || GetClientTeam(client) != 2 || !IsPlayerAlive(client))
    {
        return Plugin_Handled;
    }

    int idx = TR_Glow_FindProxyMetaByEntity(proxy);
    if (idx < 0 || g_GlowProxies[idx].role == TR_GLOW_ROLE_NONE)
    {
        return Plugin_Handled;
    }

    int role = g_GlowProxies[idx].role;
    int targetUid = g_GlowProxies[idx].targetUid;
    int colorSlot = g_GlowProxies[idx].colorSlot;
    int proxyGen = g_GlowProxies[idx].throwGen;
    int viewerUid = GetClientUserId(client);

    // 校验目标生还者当前仍存活且 userid 匹配（防断线/换人后旧代理残留放行）
    int targetSurv = GetClientOfUserId(targetUid);
    if (targetUid <= 0 || !TR_IsSurvivor(targetSurv))
    {
        return Plugin_Handled;
    }

    int subject = EntRefToEntIndex(g_GlowProxies[idx].subjectRef);
    if (subject <= 0 || !IsValidEntity(subject))
    {
        return Plugin_Handled;
    }

    // 校验实时投石锁定与代次匹配（绝不放行已过期/跨代次/跨接管的代理）
    if (role == TR_GLOW_ROLE_TANK_RED || role == TR_GLOW_ROLE_TANK_PAIR)
    {
        if (proxyGen <= 0)
        {
            return Plugin_Handled;
        }
        if (subject <= MaxClients)
        {
            if (!TR_Glow_IsNativeRockLockActive(subject)
                || g_GlowNativeTank[subject].targetUid != targetUid
                || g_GlowNativeTank[subject].throwGen != proxyGen
                || g_iGlowActiveThrowGen[subject] != proxyGen)
            {
                return Plugin_Handled;
            }
        }
        else
        {
            int slot = g_GlowProxies[idx].replicaSlot;
            if (slot < 0 || slot >= TR_GLOW_MAX_REPLICAS
                || g_GlowReplicaTank[slot].targetUid != targetUid
                || g_GlowReplicaTank[slot].throwGen != proxyGen)
            {
                return Plugin_Handled;
            }
        }
    }
    else if (role == TR_GLOW_ROLE_SURV_PAIR)
    {
        if (subject != targetSurv || g_GlowSurvivor[subject].targetUid != targetUid
            || g_GlowSurvivor[subject].colorSlot != colorSlot)
        {
            return Plugin_Handled;
        }
    }
    else
    {
        return Plugin_Handled;
    }

    // 严格角色权限隔离（被锁者仅看红边，其他真人生还者仅看有效配对色）
    if (role == TR_GLOW_ROLE_TANK_RED)
    {
        if (viewerUid != targetUid)
        {
            return Plugin_Handled;
        }
    }
    else
    {
        if (viewerUid == targetUid || colorSlot < 0 || colorSlot >= TR_GLOW_MAX_COLORS
            || g_iGlowGroupUid[colorSlot] != targetUid || g_iGlowGroupRefs[colorSlot] <= 0)
        {
            return Plugin_Handled;
        }
    }

    // 实时保守可见校验：绝不缓存正结果，门/拐角/部分遮挡/传送瞬移后同帧立即拒绝传输
    return TR_Glow_IsSubjectVisibleToViewer(client, subject) ? Plugin_Continue : Plugin_Handled;
}

// ---------------------------------------------------------------- 8 色目标分组分配器
int TR_Glow_FindGroupSlot(int targetUid)
{
    if (targetUid <= 0)
    {
        return -1;
    }
    for (int i = 0; i < TR_GLOW_MAX_COLORS; i++)
    {
        if (g_iGlowGroupUid[i] == targetUid && g_iGlowGroupRefs[i] > 0)
        {
            return i;
        }
    }
    return -1;
}

void TR_Glow_SyncSurvivorProxy(int targetUid, int colorSlot)
{
    int surv = GetClientOfUserId(targetUid);
    if (surv < 1 || surv > MaxClients || !TR_IsSurvivor(surv))
    {
        return;
    }
    if (colorSlot < 0 || colorSlot >= TR_GLOW_MAX_COLORS || g_iGlowGroupRefs[colorSlot] <= 0)
    {
        TR_Glow_DestroyProxyRef(g_GlowSurvivor[surv].pairProxyRef);
        g_GlowSurvivor[surv].colorSlot = -1;
        g_GlowSurvivor[surv].targetUid = 0;
        return;
    }

    int color = TR_GLOW_PAIR_PALETTE[colorSlot];
    int existing = EntRefToEntIndex(g_GlowSurvivor[surv].pairProxyRef);
    if (existing > MaxClients && IsValidEntity(existing))
    {
        TR_Glow_UpdateProxyMeta(g_GlowSurvivor[surv].pairProxyRef, targetUid, colorSlot, color, 0);
    }
    else
    {
        g_GlowSurvivor[surv].pairProxyRef = TR_Glow_CreateProxy(surv, TR_GLOW_ROLE_SURV_PAIR, targetUid, colorSlot, color, 0, -1);
    }
    g_GlowSurvivor[surv].targetUid = targetUid;
    g_GlowSurvivor[surv].colorSlot = colorSlot;
}

// 获取或加入目标配对色组。返回 0..7 表示分配/复用的配对色；返回 -1 表示 8 色已满（红边仍保留，配对色对他人隐藏）
int TR_Glow_AcquireGroup(int targetUid)
{
    int slot = TR_Glow_FindGroupSlot(targetUid);
    if (slot >= 0)
    {
        g_iGlowGroupRefs[slot]++;
        TR_Glow_SyncSurvivorProxy(targetUid, slot);
        return slot;
    }

    for (int i = 0; i < TR_GLOW_MAX_COLORS; i++)
    {
        if (g_iGlowGroupRefs[i] <= 0)
        {
            g_iGlowGroupUid[i] = targetUid;
            g_iGlowGroupRefs[i] = 1;
            TR_Glow_SyncSurvivorProxy(targetUid, i);
            return i;
        }
    }

    // 8 种配对色全部被占满：绝不复用冲突色，也不破坏既有组
    g_iGlowOverflowCount++;
    return -1;
}

void TR_Glow_ReleaseGroup(int targetUid, int colorSlot)
{
    if (colorSlot < 0 || colorSlot >= TR_GLOW_MAX_COLORS)
    {
        return;
    }
    if (g_iGlowGroupUid[colorSlot] != targetUid)
    {
        return;
    }
    g_iGlowGroupRefs[colorSlot]--;
    if (g_iGlowGroupRefs[colorSlot] <= 0)
    {
        g_iGlowGroupRefs[colorSlot] = 0;
        g_iGlowGroupUid[colorSlot] = 0;
        TR_Glow_SyncSurvivorProxy(targetUid, -1);
    }
}

// ---------------------------------------------------------------- 单只 Tank 锁定状态更新（通用原版/补位）
void TR_Glow_ClearTankState(TR_GlowTankState state)
{
    int oldUid = state.targetUid;
    int oldColor = state.colorSlot;
    TR_Glow_DestroyProxyRef(state.redProxyRef);
    TR_Glow_DestroyProxyRef(state.pairProxyRef);
    state.targetUid = 0;
    state.colorSlot = -1;
    state.throwGen = 0;
    if (oldUid > 0 && oldColor >= 0)
    {
        TR_Glow_ReleaseGroup(oldUid, oldColor);
    }
}

void TR_Glow_SetTankLockState(TR_GlowTankState state, int subjectEnt, int newTargetUid, int throwGen, int replicaSlot = -1)
{
    if (!TR_RockGlowEnabled() || subjectEnt <= 0 || !IsValidEntity(subjectEnt) || throwGen <= 0)
    {
        newTargetUid = 0;
    }
    if (newTargetUid > 0)
    {
        int surv = GetClientOfUserId(newTargetUid);
        if (!TR_IsSurvivor(surv))
        {
            newTargetUid = 0;
        }
    }

    if (newTargetUid == 0)
    {
        if (state.targetUid != 0 || state.throwGen != 0 || state.redProxyRef != 0 || state.pairProxyRef != 0)
        {
            TR_Glow_ClearTankState(state);
        }
        return;
    }

    // 同一目标且同一投石代次：维持现有代理（若代次变化则强制刷新绑定代次）
    if (state.targetUid == newTargetUid && state.throwGen == throwGen)
    {
        int redEnt = EntRefToEntIndex(state.redProxyRef);
        if (redEnt <= MaxClients || !IsValidEntity(redEnt))
        {
            state.redProxyRef = TR_Glow_CreateProxy(subjectEnt, TR_GLOW_ROLE_TANK_RED, newTargetUid, state.colorSlot, TR_GLOW_COLOR_RED, throwGen, replicaSlot);
        }
        if (state.colorSlot >= 0)
        {
            int pairEnt = EntRefToEntIndex(state.pairProxyRef);
            if (pairEnt <= MaxClients || !IsValidEntity(pairEnt))
            {
                state.pairProxyRef = TR_Glow_CreateProxy(subjectEnt, TR_GLOW_ROLE_TANK_PAIR, newTargetUid, state.colorSlot, TR_GLOW_PAIR_PALETTE[state.colorSlot], throwGen, replicaSlot);
            }
            TR_Glow_SyncSurvivorProxy(newTargetUid, state.colorSlot);
        }
        return;
    }

    // 目标或投石代次发生变化：先释放旧组引用再加入新组
    int oldUid = state.targetUid;
    int oldColor = state.colorSlot;
    state.targetUid = 0;
    state.colorSlot = -1;
    state.throwGen = 0;
    if (oldUid > 0 && oldColor >= 0)
    {
        TR_Glow_ReleaseGroup(oldUid, oldColor);
    }

    int newColor = TR_Glow_AcquireGroup(newTargetUid);
    state.targetUid = newTargetUid;
    state.colorSlot = newColor;
    state.throwGen = throwGen;

    // 代理 #1（红边）：任何情况下只要目标有效就保留
    int redEnt = EntRefToEntIndex(state.redProxyRef);
    if (redEnt > MaxClients && IsValidEntity(redEnt))
    {
        TR_Glow_UpdateProxyMeta(state.redProxyRef, newTargetUid, newColor, TR_GLOW_COLOR_RED, throwGen);
    }
    else
    {
        state.redProxyRef = TR_Glow_CreateProxy(subjectEnt, TR_GLOW_ROLE_TANK_RED, newTargetUid, newColor, TR_GLOW_COLOR_RED, throwGen, replicaSlot);
    }

    // 代理 #2（配对色）：仅当分配到 0..7 配对色时创建/更新；8 色溢出时销毁/隐藏
    if (newColor >= 0)
    {
        int pairEnt = EntRefToEntIndex(state.pairProxyRef);
        if (pairEnt > MaxClients && IsValidEntity(pairEnt))
        {
            TR_Glow_UpdateProxyMeta(state.pairProxyRef, newTargetUid, newColor, TR_GLOW_PAIR_PALETTE[newColor], throwGen);
        }
        else
        {
            state.pairProxyRef = TR_Glow_CreateProxy(subjectEnt, TR_GLOW_ROLE_TANK_PAIR, newTargetUid, newColor, TR_GLOW_PAIR_PALETTE[newColor], throwGen, replicaSlot);
        }
    }
    else
    {
        TR_Glow_DestroyProxyRef(state.pairProxyRef);
    }
}

// ---------------------------------------------------------------- 对外更新与清理入口
void TR_Glow_UpdateNativeTank(int tank)
{
    if (tank < 1 || tank > MaxClients)
    {
        return;
    }
    if (!TR_RockGlowEnabled() || !TR_Glow_IsNativeTargetPathEnabled() || !TR_Glow_IsNativeRockLockActive(tank))
    {
        TR_Glow_SetTankLockState(g_GlowNativeTank[tank], tank, 0, 0, -1);
        return;
    }
    int targetUid = TR_Glow_GetVerifiedNativeRockTarget(tank);
    TR_Glow_SetTankLockState(g_GlowNativeTank[tank], tank, targetUid, g_iGlowActiveThrowGen[tank], -1);
}

void TR_Glow_UpdateReplicaTank(int slot, int replicaEnt, bool rockLocked, int attackTargetUid, int throwGen = 0)
{
    if (slot < 0 || slot >= TR_GLOW_MAX_REPLICAS)
    {
        return;
    }
    if (!TR_RockGlowEnabled() || !rockLocked || replicaEnt <= MaxClients || !IsValidEntity(replicaEnt) || attackTargetUid <= 0)
    {
        TR_Glow_SetTankLockState(g_GlowReplicaTank[slot], replicaEnt, 0, 0, slot);
        return;
    }
    int gen = throwGen;
    if (gen <= 0)
    {
        gen = (g_GlowReplicaTank[slot].throwGen > 0) ? g_GlowReplicaTank[slot].throwGen : TR_Glow_AllocThrowGen();
    }
    TR_Glow_SetTankLockState(g_GlowReplicaTank[slot], replicaEnt, attackTargetUid, gen, slot);
}

void TR_Glow_ClearReplicaTank(int slot)
{
    if (slot < 0 || slot >= TR_GLOW_MAX_REPLICAS)
    {
        return;
    }
    TR_Glow_ClearTankState(g_GlowReplicaTank[slot]);
}

void TR_Glow_OnClientGoneOrDead(int client)
{
    if (client < 1 || client > MaxClients)
    {
        return;
    }
    TR_Glow_EndNativeThrow(client);

    int savedUid = g_GlowSurvivor[client].targetUid;
    TR_Glow_DestroyProxyRef(g_GlowSurvivor[client].pairProxyRef);
    g_GlowSurvivor[client].colorSlot = -1;
    g_GlowSurvivor[client].targetUid = 0;

    int uid = IsClientConnected(client) ? GetClientUserId(client) : 0;
    if (uid <= 0)
    {
        uid = savedUid;
    }
    if (uid > 0)
    {
        // 清理所有曾锁定或观测到该生还者的 Tank 代理与观测状态，并推进代次防旧观测残留
        for (int t = 1; t <= MaxClients; t++)
        {
            if (g_GlowNativeTank[t].targetUid == uid || g_iGlowObservedTargetUid[t] == uid)
            {
                TR_Glow_ClearTankState(g_GlowNativeTank[t]);
                TR_Glow_ClearNativeObservation(t);
                if (g_iGlowActiveThrowGen[t] > 0 && TR_Glow_IsNativeRockLockActive(t))
                {
                    g_iGlowActiveThrowGen[t] = TR_Glow_AllocThrowGen();
                }
                else
                {
                    g_iGlowActiveThrowGen[t] = 0;
                    g_iGlowActiveThrowTankUid[t] = 0;
                }
            }
        }
        for (int s = 0; s < TR_GLOW_MAX_REPLICAS; s++)
        {
            if (g_GlowReplicaTank[s].targetUid == uid)
            {
                TR_Glow_ClearReplicaTank(s);
            }
        }
        for (int i = 0; i < TR_GLOW_MAX_COLORS; i++)
        {
            if (g_iGlowGroupUid[i] == uid)
            {
                g_iGlowGroupUid[i] = 0;
                g_iGlowGroupRefs[i] = 0;
            }
        }
    }
}

void TR_Glow_ClearAll()
{
    g_iGlowInHelperTank = 0;
    g_iGlowInHelperTankUid = 0;
    g_iGlowInHelperRaw = -1;
    g_iGlowInHelperTargetUid = 0;

    for (int t = 1; t <= MAXPLAYERS; t++)
    {
        TR_Glow_DestroyProxyRef(g_GlowNativeTank[t].redProxyRef);
        TR_Glow_DestroyProxyRef(g_GlowNativeTank[t].pairProxyRef);
        g_GlowNativeTank[t].targetUid = 0;
        g_GlowNativeTank[t].colorSlot = -1;
        g_GlowNativeTank[t].throwGen = 0;
        g_iGlowActiveThrowGen[t] = 0;
        g_iGlowActiveThrowTankUid[t] = 0;
        g_iGlowObservedThrowGen[t] = 0;
        g_iGlowObservedTankUid[t] = 0;
        g_iGlowObservedRawHandle[t] = -1;
        g_iGlowObservedTargetUid[t] = 0;
        g_fGlowObservedTime[t] = 0.0;

        TR_Glow_DestroyProxyRef(g_GlowSurvivor[t].pairProxyRef);
        g_GlowSurvivor[t].colorSlot = -1;
        g_GlowSurvivor[t].targetUid = 0;
    }
    for (int s = 0; s < TR_GLOW_MAX_REPLICAS; s++)
    {
        TR_Glow_DestroyProxyRef(g_GlowReplicaTank[s].redProxyRef);
        TR_Glow_DestroyProxyRef(g_GlowReplicaTank[s].pairProxyRef);
        g_GlowReplicaTank[s].targetUid = 0;
        g_GlowReplicaTank[s].colorSlot = -1;
        g_GlowReplicaTank[s].throwGen = 0;
    }
    for (int i = 0; i < TR_GLOW_MAX_COLORS; i++)
    {
        g_iGlowGroupUid[i] = 0;
        g_iGlowGroupRefs[i] = 0;
    }
    // 兜底扫除任何残余代理元数据与实体
    for (int i = 0; i < TR_GLOW_MAX_PROXY_SLOTS; i++)
    {
        if (g_GlowProxies[i].role != TR_GLOW_ROLE_NONE)
        {
            int ref = g_GlowProxies[i].entRef;
            TR_Glow_DestroyProxyRef(ref);
        }
    }
    TR_Glow_ResetEntMap();
    g_iGlowLiveProxies = 0;
}

void TR_Glow_OnConVarChanged(ConVar convar, const char[] oldValue, const char[] newValue)
{
    #pragma unused convar, oldValue, newValue
    if (!TR_RockGlowEnabled())
    {
        TR_Glow_ClearAll();
    }
}

void TR_Glow_OnPluginStart()
{
    TR_Glow_ClearAll();
    TR_Glow_InstallNativeTargetDetour();
    if (g_cvRockTargetGlow != null)
    {
        g_cvRockTargetGlow.AddChangeHook(TR_Glow_OnConVarChanged);
    }
    if (g_cvEnabled != null)
    {
        g_cvEnabled.AddChangeHook(TR_Glow_OnConVarChanged);
    }
}

void TR_Glow_OnAllPluginsLoaded()
{
    if (!g_bGlowNativeTargetReady && g_bGlowPendingOsRetry
        && GetFeatureStatus(FeatureType_Native, "L4D_GetServerOS") == FeatureStatus_Available)
    {
        TR_Glow_InstallNativeTargetDetour();
    }
}

void TR_Glow_OnPluginEnd()
{
    TR_Glow_UninstallNativeTargetDetour();
    TR_Glow_ClearAll();
}

int TR_Glow_CountActiveGroups()
{
    int count = 0;
    for (int i = 0; i < TR_GLOW_MAX_COLORS; i++)
    {
        if (g_iGlowGroupRefs[i] > 0)
        {
            count++;
        }
    }
    return count;
}

void TR_Glow_SyncAllNativeTanks()
{
    if (!TR_RockGlowEnabled())
    {
        if (g_iGlowLiveProxies > 0)
        {
            TR_Glow_ClearAll();
        }
        return;
    }
    for (int i = 1; i <= MaxClients; i++)
    {
        if (g_GlowNativeTank[i].targetUid != 0 || g_iGlowActiveThrowGen[i] != 0 || TR_IsAiTank(i))
        {
            if (g_iGlowActiveThrowGen[i] != 0 && !TR_Glow_IsNativeRockLockActive(i))
            {
                TR_Glow_EndNativeThrow(i);
            }
            else
            {
                TR_Glow_UpdateNativeTank(i);
            }
        }
    }
}
