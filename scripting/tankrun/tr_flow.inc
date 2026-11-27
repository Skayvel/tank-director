// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors
//
// Tank Run 综合控制中心 - 流程节奏层（开场过场跳过 + 合作团灭快进）
//
// 模块二（跳过开场过场动画）：
//   实体 I/O 动作参考 SilverShot 的 "[L4D & L4D2] First Map - Skip Intro Cutscenes"
//   (l4d_skip_intro.sp v1.14, GPL-3.0-or-later)，在遵循原版视角实体完成链路的前提下，
//   原创重写为带 Tank Run 作用域门禁、终局/非首关保护与 L4D_OnFinishIntro 状态收敛的实现。
//
// 模块三（缩短合作团灭结束演出/等待）：
//   基于对原版 server.dll CDirector::EndScenario (VA 0x10268f95) 与
//   CDirector::UpdateScenarioState (VA 0x10271bf0) 的静态实证：
//   - 原版合作全灭时发送 7.0s 黑屏淡出 (FFADE_OUT|FFADE_STAYOUT|FFADE_PURGE) 并启动
//     CDirector::m_RestartTimer (7.0s)，到期后自动调用原版 CDirector::Restart()。
//   - director_report_screen_delay 在本构建 server.dll 中未被任何游戏逻辑消费，不予采用。
//   - 本模块仅在权威 mission_lost 事件触发、确认合作全灭、且 m_RestartTimer 双重自校验
//     命中原版 7.0s 窗口后，同步缩短 m_RestartTimer 与客户端 Fade 淡出时长；
//     任意条件或校验不满足时安全回退至原版 7.0s，绝不强制换图或破坏统计语义。

#if defined _tankrun_flow_included
 #endinput
#endif
#define _tankrun_flow_included

#define TR_FFADE_IN       0x0001
#define TR_FFADE_OUT      0x0002
#define TR_FFADE_STAYOUT  0x0008
#define TR_FFADE_PURGE    0x0010
#define TR_FADE_SCALE     512.0

#define TR_WIPE_VANILLA_DELAY  7.0
#define TR_WIPE_MIN_DELAY      0.5
#define TR_WIPE_MAX_DELAY      7.0

#define TR_INTRO_RETRY_COUNT   5
float g_fIntroRetryOffsets[TR_INTRO_RETRY_COUNT] = { 0.3, 1.0, 2.5, 4.5, 6.5 };

// 模块二状态
Handle g_hIntroTimers[TR_INTRO_RETRY_COUNT];
bool g_bIntroSkipArmed;
bool g_bIntroFinished;
bool g_bIntroFaded;
bool g_bIntroTriggeredThisRound;
bool g_bIntroOutputWired[2];
float g_fIntroDetectedAt;
float g_fIntroLastElapsed = -1.0;
int g_iStatIntroSkipped;

// 模块三状态
int g_iOffDirectorRestartTimer = -1;
bool g_bWipeGamedataLoaded;
bool g_bWipeMapTransition;
bool g_bWipeFinaleLeaving;
bool g_bWipeFastPending;
float g_fWipeTriggeredAt;
float g_fWipeLastElapsed = -1.0;
float g_fFlowSuppressWipeUntil = 0.0;
int g_iStatWipeAccelerated;
int g_iStatWipeFallbacks;
char g_sWipeLastResult[128];

void TR_FlowSuppressWipe(float seconds)
{
    float until = GetGameTime() + seconds;
    if (until > g_fFlowSuppressWipeUntil)
    {
        g_fFlowSuppressWipeUntil = until;
    }
}

int TR_FlowCountHumanSurvivors()
{
    int count = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsClientInGame(i) && !IsFakeClient(i) && !IsClientInKickQueue(i) && GetClientTeam(i) == 2)
        {
            count++;
        }
    }
    return count;
}

// ---------------------------------------------------------------- 工具：模式与淡入淡出
bool TR_FlowIsCoopMode()
{
    if (g_bV23MapStarted && GetFeatureStatus(FeatureType_Native, "L4D2_IsGenericCooperativeMode") == FeatureStatus_Available)
    {
        return L4D2_IsGenericCooperativeMode();
    }
    char mode[64];
    TR_GetMode(mode, sizeof(mode));
    if (StrContains(mode, "versus", false) != -1
        || StrContains(mode, "scavenge", false) != -1
        || StrContains(mode, "survival", false) != -1)
    {
        return false;
    }
    return true;
}

bool TR_FlowScopeEnabled(ConVar cvScope)
{
    if (!TR_FeatureEnabled() || cvScope == null)
    {
        return false;
    }
    int scope = cvScope.IntValue;
    if (scope <= 0)
    {
        return false;
    }
    if (!TR_FlowIsCoopMode())
    {
        return false;
    }
    if (scope == 1)
    {
        return TR_IsTankRunMode();
    }
    return true;
}

bool TR_FlowIsIntroAllowed()
{
    return TR_FlowScopeEnabled(g_cvSkipIntro);
}

bool TR_FlowIsFastWipeAllowed()
{
    return TR_FlowScopeEnabled(g_cvFastWipe);
}

int TR_SecondsToFadeUnits(float seconds)
{
    if (seconds <= 0.0)
    {
        return 0;
    }
    int units = RoundToZero(seconds * TR_FADE_SCALE);
    if (units < 0)
    {
        return 0;
    }
    if (units > 65535)
    {
        return 65535;
    }
    return units;
}

void TR_SendFadeToHumans(float duration, float hold, int flags, int r, int g, int b, int a)
{
    int targets[MAXPLAYERS + 1];
    int count = 0;
    for (int i = 1; i <= MaxClients; i++)
    {
        if (IsClientInGame(i) && !IsFakeClient(i))
        {
            targets[count++] = i;
        }
    }
    if (count <= 0)
    {
        return;
    }
    Handle msg = StartMessage("Fade", targets, count, USERMSG_RELIABLE | USERMSG_BLOCKHOOKS);
    if (msg == null)
    {
        return;
    }
    BfWrite bf = UserMessageToBfWrite(msg);
    bf.WriteShort(TR_SecondsToFadeUnits(duration));
    bf.WriteShort(TR_SecondsToFadeUnits(hold));
    bf.WriteShort(flags);
    bf.WriteByte(r);
    bf.WriteByte(g);
    bf.WriteByte(b);
    bf.WriteByte(a);
    EndMessage();
}

bool TR_FlowIsFinaleMap()
{
    if (GetFeatureStatus(FeatureType_Native, "L4D_IsMissionFinalMap") == FeatureStatus_Available)
    {
        if (L4D_IsMissionFinalMap(true))
        {
            return true;
        }
    }
    return FindEntityByClassname(-1, "trigger_finale") != INVALID_ENT_REFERENCE
        || FindEntityByClassname(-1, "finale_trigger") != INVALID_ENT_REFERENCE;
}

bool TR_FlowIsFinaleEscaping()
{
    if (g_bWipeFinaleLeaving)
    {
        return true;
    }
    if (GetFeatureStatus(FeatureType_Native, "L4D_IsFinaleEscapeInProgress") == FeatureStatus_Available)
    {
        return L4D_IsFinaleEscapeInProgress();
    }
    return false;
}

// ---------------------------------------------------------------- 生命周期
void TR_FlowOnPluginStart()
{
    strcopy(g_sWipeLastResult, sizeof(g_sWipeLastResult), "尚未触发");
    TR_FlowLoadWipeGamedata();
    HookEvent("gameinstructor_nodraw", Event_FlowIntroNoDraw, EventHookMode_PostNoCopy);
    HookEvent("round_start_post_nav", Event_FlowRoundPostNav, EventHookMode_PostNoCopy);
    HookEvent("mission_lost", Event_FlowMissionLost, EventHookMode_PostNoCopy);
    HookEvent("map_transition", Event_FlowMapTransition, EventHookMode_PostNoCopy);
    HookEvent("finale_vehicle_leaving", Event_FlowFinaleLeaving, EventHookMode_PostNoCopy);
}

void TR_FlowLoadWipeGamedata()
{
    g_iOffDirectorRestartTimer = -1;
    g_bWipeGamedataLoaded = false;

    if (GetFeatureStatus(FeatureType_Native, "L4D_GetServerOS") == FeatureStatus_Available
        && L4D_GetServerOS() != 0)
    {
        TR_Log("团灭快进：当前非 Windows 服务端平台，安全回退原版 7.0 秒");
        return;
    }

    char gdPath[PLATFORM_MAX_PATH];
    BuildPath(Path_SM, gdPath, sizeof(gdPath), "gamedata/tankrun_wipe.txt");
    if (!FileExists(gdPath))
    {
        TR_Log("团灭快进：gamedata/tankrun_wipe.txt 未找到，安全回退原版 7.0 秒团灭等待");
        return;
    }

    GameData gd = new GameData("tankrun_wipe");
    if (gd == null)
    {
        TR_Log("团灭快进：gamedata/tankrun_wipe.txt 未找到，将保持原版 7.0 秒团灭等待");
        return;
    }
    int offset = gd.GetOffset("CDirector::m_RestartTimer");
    delete gd;

    if (offset < 0)
    {
        TR_Log("团灭快进：当前平台未配置 CDirector::m_RestartTimer 偏移，安全回退原版 7.0 秒");
        return;
    }
    g_iOffDirectorRestartTimer = offset;
    g_bWipeGamedataLoaded = true;
    TR_Log("团灭快进：已加载 CDirector::m_RestartTimer 偏移=%d（运行时执行 7.0s 双重自校验）", offset);
}

void TR_FlowStopIntroTimers()
{
    for (int i = 0; i < TR_INTRO_RETRY_COUNT; i++)
    {
        if (g_hIntroTimers[i] != null)
        {
            KillTimer(g_hIntroTimers[i]);
            g_hIntroTimers[i] = null;
        }
    }
    g_bIntroSkipArmed = false;
}

void TR_FlowResetIntroState()
{
    TR_FlowStopIntroTimers();
    g_bIntroFinished = false;
    g_bIntroFaded = false;
    g_bIntroTriggeredThisRound = false;
    g_bIntroOutputWired[0] = false;
    g_bIntroOutputWired[1] = false;
    g_fIntroDetectedAt = 0.0;
}

void TR_FlowOnMapStart()
{
    TR_FlowResetIntroState();
    g_bWipeMapTransition = false;
    g_bWipeFinaleLeaving = false;
    g_bWipeFastPending = false;
    g_fWipeTriggeredAt = 0.0;
    g_fFlowSuppressWipeUntil = 0.0;
}

void TR_FlowOnMapEnd()
{
    TR_FlowResetIntroState();
    g_bWipeMapTransition = true;
    g_bWipeFastPending = false;
}

void TR_FlowOnPluginEnd()
{
    TR_FlowStopIntroTimers();
}

void TR_FlowOnRoundStart()
{
    if (g_bWipeFastPending)
    {
        g_bWipeFastPending = false;
        if (g_fWipeTriggeredAt > 0.0)
        {
            g_fWipeLastElapsed = GetGameTime() - g_fWipeTriggeredAt;
            TR_Log("团灭快进完成：mission_lost 至 round_start 实测耗时 %.2f 秒（原版 7.00 秒）", g_fWipeLastElapsed);
            Format(g_sWipeLastResult, sizeof(g_sWipeLastResult), "已生效（实测 %.2fs，原版 7.0s）", g_fWipeLastElapsed);
        }
        // 清理团灭淡出残留，确保客户端绝不停留在黑屏。
        TR_SendFadeToHumans(0.30, 0.0, TR_FFADE_IN | TR_FFADE_PURGE, 0, 0, 0, 255);
    }
    g_bWipeMapTransition = false;
    g_bWipeFinaleLeaving = false;
    g_fWipeTriggeredAt = 0.0;
    TR_FlowResetIntroState();
}

void TR_FlowOnRoundEnd()
{
    TR_FlowStopIntroTimers();
}

public void Event_FlowMapTransition(Event event, const char[] name, bool dontBroadcast)
{
    g_bWipeMapTransition = true;
    g_bWipeFastPending = false;
    TR_FlowStopIntroTimers();
}

public void Event_FlowFinaleLeaving(Event event, const char[] name, bool dontBroadcast)
{
    g_bWipeFinaleLeaving = true;
    g_bWipeFastPending = false;
    TR_FlowStopIntroTimers();
}

// ---------------------------------------------------------------- 模块二：跳过开场过场动画
bool TR_FlowIsCurrentlyInIntro()
{
    if (GetFeatureStatus(FeatureType_Native, "L4D_IsInIntro") == FeatureStatus_Available)
    {
        return L4D_IsInIntro() != 0;
    }
    return false;
}

bool TR_FlowIsFirstCampaignMap()
{
    if (GetFeatureStatus(FeatureType_Native, "L4D_IsFirstMapInScenario") == FeatureStatus_Available)
    {
        return L4D_IsFirstMapInScenario();
    }
    return false;
}

bool TR_FlowHasIntroCamera()
{
    return FindEntityByClassname(-1, "point_viewcontrol_survivor") != INVALID_ENT_REFERENCE
        || FindEntityByClassname(-1, "point_viewcontrol_multiplayer") != INVALID_ENT_REFERENCE;
}

void TR_FlowArmIntroSkip(const char[] source)
{
    if (g_bIntroSkipArmed || g_bIntroFinished || g_bWipeMapTransition || g_bRoundEnded)
    {
        return;
    }
    if (!TR_IsMapRunning() || !TR_FlowIsIntroAllowed())
    {
        return;
    }
    if (TR_FlowIsFinaleMap() || TR_FlowIsFinaleEscaping())
    {
        return;
    }
    if (GetFeatureStatus(FeatureType_Native, "L4D_HasAnySurvivorLeftSafeArea") == FeatureStatus_Available
        && L4D_HasAnySurvivorLeftSafeArea())
    {
        return;
    }
    bool firstMap = TR_FlowIsFirstCampaignMap();
    bool inIntro = TR_FlowIsCurrentlyInIntro();
    if (!firstMap && !inIntro)
    {
        return;
    }
    if (!TR_FlowHasIntroCamera() && !inIntro)
    {
        return;
    }

    g_bIntroSkipArmed = true;
    g_fIntroDetectedAt = GetGameTime();
    TR_Log("开场跳过：检测到开场过场（来源=%s firstMap=%d inIntro=%d），安排受控跳过",
        source, firstMap ? 1 : 0, inIntro ? 1 : 0);

    for (int i = 0; i < TR_INTRO_RETRY_COUNT; i++)
    {
        g_hIntroTimers[i] = CreateTimer(g_fIntroRetryOffsets[i], Timer_FlowSkipIntro, i, TIMER_FLAG_NO_MAPCHANGE);
    }
}

public void Event_FlowIntroNoDraw(Event event, const char[] name, bool dontBroadcast)
{
    TR_FlowArmIntroSkip("gameinstructor_nodraw");
}

public void Event_FlowRoundPostNav(Event event, const char[] name, bool dontBroadcast)
{
    if (TR_FlowIsCurrentlyInIntro())
    {
        TR_FlowArmIntroSkip("round_start_post_nav");
    }
}

public void L4D_OnFinishIntro()
{
    if (!g_bIntroFinished)
    {
        g_bIntroFinished = true;
        if (g_bIntroTriggeredThisRound && g_fIntroDetectedAt > 0.0)
        {
            g_fIntroLastElapsed = GetGameTime() - g_fIntroDetectedAt;
            TR_Log("开场跳过完成：L4D_OnFinishIntro 已确认，耗时 %.2f 秒", g_fIntroLastElapsed);
        }
    }
    TR_FlowStopIntroTimers();
}

public Action Timer_FlowSkipIntro(Handle timer, any slot)
{
    if (slot >= 0 && slot < TR_INTRO_RETRY_COUNT && g_hIntroTimers[slot] == timer)
    {
        g_hIntroTimers[slot] = null;
    }
    if (g_bIntroFinished || g_bWipeMapTransition || g_bRoundEnded || !TR_IsMapRunning() || !TR_FlowIsIntroAllowed())
    {
        TR_FlowStopIntroTimers();
        return Plugin_Stop;
    }
    if (TR_FlowIsFinaleMap() || TR_FlowIsFinaleEscaping())
    {
        TR_FlowStopIntroTimers();
        return Plugin_Stop;
    }
    if (GetFeatureStatus(FeatureType_Native, "L4D_HasAnySurvivorLeftSafeArea") == FeatureStatus_Available
        && L4D_HasAnySurvivorLeftSafeArea())
    {
        g_bIntroFinished = true;
        TR_FlowStopIntroTimers();
        return Plugin_Stop;
    }

    bool hasIntroNative = GetFeatureStatus(FeatureType_Native, "L4D_IsInIntro") == FeatureStatus_Available;
    bool inIntro = hasIntroNative ? (L4D_IsInIntro() != 0) : false;
    if (hasIntroNative && !inIntro && g_bIntroTriggeredThisRound)
    {
        g_bIntroFinished = true;
        TR_FlowStopIntroTimers();
        return Plugin_Stop;
    }

    int director = FindEntityByClassname(-1, "info_director");
    if (director == INVALID_ENT_REFERENCE)
    {
        // 无可靠 info_director 时保持安全回退。
        return Plugin_Stop;
    }

    char directorName[64];
    GetEntPropString(director, Prop_Data, "m_iName", directorName, sizeof(directorName));

    int touchedCameras = 0;
    bool firedViaCamera = false;
    char outputBuf[160];
    for (int kind = 0; kind < 2; kind++)
    {
        int kindCount = 0;
        int cam = INVALID_ENT_REFERENCE;
        while ((cam = FindEntityByClassname(cam, kind == 0 ? "point_viewcontrol_survivor" : "point_viewcontrol_multiplayer")) != INVALID_ENT_REFERENCE)
        {
            touchedCameras++;
            kindCount++;
            if (directorName[0] != '\0')
            {
                if (!g_bIntroOutputWired[kind])
                {
                    Format(outputBuf, sizeof(outputBuf), "OnUser1 %s:ReleaseSurvivorPositions::0:-1", directorName);
                    SetVariantString(outputBuf);
                    AcceptEntityInput(cam, "AddOutput");

                    Format(outputBuf, sizeof(outputBuf), "OnUser1 %s:FinishIntro::0:-1", directorName);
                    SetVariantString(outputBuf);
                    AcceptEntityInput(cam, "AddOutput");
                }
                AcceptEntityInput(cam, "FireUser1");
                firedViaCamera = true;
            }
            // 不 Kill 镜头实体，通过 StartMovement 推进原版镜头完成并保留地图 I/O（开门/传送/初始化）。
            SetVariantString("!self");
            AcceptEntityInput(cam, "StartMovement");
        }
        if (kindCount > 0)
        {
            g_bIntroOutputWired[kind] = true;
        }
    }

    if (touchedCameras == 0 && !inIntro)
    {
        // 本关卡无可靠开场镜头且引擎未处于开场态，安全回退不做任何事。
        return Plugin_Stop;
    }

    // 仅当未能通过已命名 info_director 的相机 OnUser1 链路触发时，再直接向 info_director 触发收尾输入，避免同帧重复触发。
    if (!firedViaCamera)
    {
        AcceptEntityInput(director, "ReleaseSurvivorPositions");
        AcceptEntityInput(director, "FinishIntro");
    }

    if (!g_bIntroTriggeredThisRound)
    {
        g_bIntroTriggeredThisRound = true;
        g_iStatIntroSkipped++;
        TR_LogEvent("开场过场已跳过：相机实体=%d inIntro=%d 地图=%s", touchedCameras, inIntro ? 1 : 0, g_sMap);
    }

    if (!g_bIntroFaded)
    {
        g_bIntroFaded = true;
        TR_SendFadeToHumans(0.45, 0.0, TR_FFADE_IN | TR_FFADE_PURGE, 0, 0, 0, 255);
    }

    if (hasIntroNative && L4D_IsInIntro() == 0)
    {
        g_bIntroFinished = true;
        TR_FlowStopIntroTimers();
    }
    return Plugin_Stop;
}

// ---------------------------------------------------------------- 模块三：缩短合作团灭结束演出与等待
bool TR_FlowHasStandingSurvivor()
{
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!IsClientInGame(i) || GetClientTeam(i) != 2 || !IsPlayerAlive(i))
        {
            continue;
        }
        if (GetEntProp(i, Prop_Send, "m_isIncapacitated") == 0
            && GetEntProp(i, Prop_Send, "m_isHangingFromLedge") == 0)
        {
            return true;
        }
    }
    return false;
}

public void Event_FlowMissionLost(Event event, const char[] name, bool dontBroadcast)
{
    if (!TR_IsMapRunning() || !TR_FlowIsFastWipeAllowed())
    {
        return;
    }
    // 严防误触发：过图、逃脱、或仍有站立可行动幸存者时绝不介入。
    if (g_bWipeMapTransition || TR_FlowIsFinaleEscaping())
    {
        TR_Log("团灭快进跳过：处于过图或终局逃脱阶段");
        return;
    }
    if (GetGameTime() < g_fFlowSuppressWipeUntil || TR_FlowCountHumanSurvivors() == 0)
    {
        strcopy(g_sWipeLastResult, sizeof(g_sWipeLastResult), "跳过（无真人幸存者或刚由挂机转旁观触发）");
        TR_Log("团灭快进跳过：场上无真人幸存者或处于挂机转旁观保护窗口，防止误加速");
        return;
    }
    if (TR_FlowHasStandingSurvivor())
    {
        TR_Log("团灭快进跳过：检测到仍有站立存活幸存者，防止非权威 mission_lost 误触发");
        return;
    }

    float targetDelay = g_cvWipeDelay != null ? g_cvWipeDelay.FloatValue : 1.5;
    if (targetDelay < TR_WIPE_MIN_DELAY)
    {
        targetDelay = TR_WIPE_MIN_DELAY;
    }
    if (targetDelay >= TR_WIPE_VANILLA_DELAY - 0.05)
    {
        strcopy(g_sWipeLastResult, sizeof(g_sWipeLastResult), "目标时长接近原版 7.0s，未干预");
        return;
    }

    if (!g_bWipeGamedataLoaded || g_iOffDirectorRestartTimer < 0)
    {
        g_iStatWipeFallbacks++;
        strcopy(g_sWipeLastResult, sizeof(g_sWipeLastResult), "回退原版 7.0s（缺少当前平台偏移）");
        TR_Log("团灭快进回退：缺少 CDirector::m_RestartTimer 偏移，保持原版 7.0 秒");
        return;
    }

    if (GetFeatureStatus(FeatureType_Native, "L4D_GetPointer") != FeatureStatus_Available)
    {
        g_iStatWipeFallbacks++;
        strcopy(g_sWipeLastResult, sizeof(g_sWipeLastResult), "回退原版 7.0s（Left4DHooks L4D_GetPointer 不可用）");
        TR_Log("团灭快进回退：L4D_GetPointer 不可用，保持原版 7.0 秒");
        return;
    }

    Address director = L4D_GetPointer(POINTER_DIRECTOR);
    if (director == Address_Null)
    {
        g_iStatWipeFallbacks++;
        strcopy(g_sWipeLastResult, sizeof(g_sWipeLastResult), "回退原版 7.0s（POINTER_DIRECTOR 为空）");
        TR_Log("团灭快进回退：POINTER_DIRECTOR 为空，保持原版 7.0 秒");
        return;
    }

    Address durationAddr = director + view_as<Address>(g_iOffDirectorRestartTimer + 4);
    Address timestampAddr = director + view_as<Address>(g_iOffDirectorRestartTimer + 8);
    float now = GetGameTime();
    float curDuration = view_as<float>(LoadFromAddress(durationAddr, NumberType_Int32));
    float curTimestamp = view_as<float>(LoadFromAddress(timestampAddr, NumberType_Int32));
    float remaining = curTimestamp - now;

    // 严格双重正向区间校验（NaN 安全）：仅当该偏移处确实是刚启动的原版 7.0 秒重启计时器时才写入。
    bool validDuration = (FloatAbs(curDuration - TR_WIPE_VANILLA_DELAY) <= 0.25);
    bool validRemaining = (remaining >= 5.5 && remaining <= 7.5);
    if (!validDuration || !validRemaining)
    {
        g_iStatWipeFallbacks++;
        Format(g_sWipeLastResult, sizeof(g_sWipeLastResult),
            "回退原版（偏移校验未通过 dur=%.2f rem=%.2f）", curDuration, remaining);
        TR_Log("团灭快进回退：m_RestartTimer 校验未通过（offset=%d duration=%.3f remaining=%.3f），未写内存",
            g_iOffDirectorRestartTimer, curDuration, remaining);
        return;
    }

    StoreToAddress(durationAddr, view_as<int>(targetDelay), NumberType_Int32);
    StoreToAddress(timestampAddr, view_as<int>(now + targetDelay), NumberType_Int32);

    // 同步缩短客户端淡出，覆盖引擎刚发送的 7.0 秒慢淡出，避免客户端滞留在半黑过场。
    float fadeDur = targetDelay * 0.85;
    if (fadeDur < 0.25)
    {
        fadeDur = 0.25;
    }
    TR_SendFadeToHumans(fadeDur, 0.50, TR_FFADE_OUT | TR_FFADE_STAYOUT | TR_FFADE_PURGE, 0, 0, 0, 255);

    g_bWipeFastPending = true;
    g_fWipeTriggeredAt = now;
    g_iStatWipeAccelerated++;
    Format(g_sWipeLastResult, sizeof(g_sWipeLastResult), "已缩短为 %.2fs（原版 7.0s）", targetDelay);
    TR_Log("团灭快进已应用：m_RestartTimer %.2fs -> %.2fs，客户端淡出同步缩短为 %.2fs",
        curDuration, targetDelay, fadeDur);
    TR_LogEvent("合作团灭快进：原版 7.00s -> %.2fs（剩余 %.2fs -> %.2fs）", targetDelay, remaining, targetDelay);
}
