// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

// Tank Run 综合控制中心 - 状态、依赖探测与诊断导出

#if defined _tankrun_status_included
 #endinput
#endif
#define _tankrun_status_included

char g_sLastDumpPath[PLATFORM_MAX_PATH];

// 普通依赖探测：只检查原生/扩展是否可用，不创建任何实体。
// 可在 OnPluginStart / 命令 / 定时器等任意上下文安全调用。
void TR_ProbeLibraries()
{
    g_bLeft4D = GetFeatureStatus(FeatureType_Native, "L4D2_SpawnTank") == FeatureStatus_Available;
    if (!g_bLeft4D)
    {
        g_bLeft4D = GetFeatureStatus(FeatureType_Native, "L4D_GetRandomPZSpawnPosition") == FeatureStatus_Available;
    }
    g_bSdkHooks = GetFeatureStatus(FeatureType_Native, "SDKHook") == FeatureStatus_Available;
}

// 运行态探测：需要运行中的地图和已就绪的实体系统（会创建 logic_script）。
// 只在 OnMapStart / OnConfigsExecuted 的延迟回调或游戏内命令中调用。
bool TR_ProbeRuntime()
{
    if (!TR_IsMapRunning())
    {
        g_bVValid = false;
        g_bVProbed = false;
        strcopy(g_sVStatus, sizeof(g_sVStatus), "地图/实体系统未就绪，脚本探测已推迟");
        TR_Log("运行态探测跳过：地图或实体系统未就绪");
        return false;
    }
    g_bVProbed = false;
    bool ok = TR_VScriptProbe(true);
    TR_Log("运行态探测：Left4DHooks=%s SDKHooks=%s 模式=%s 脚本=%s",
        g_bLeft4D ? "可用" : "不可用",
        g_bSdkHooks ? "可用" : "不可用",
        TR_IsTankRunMode() ? "tankrun" : "其他",
        g_sVStatus);
    return ok;
}

// 供菜单/命令使用的组合探测：先做安全依赖探测，地图就绪时才做运行态探测。
void TR_ProbeDependencies()
{
    TR_ProbeLibraries();
    if (!TR_IsMapRunning())
    {
        g_bVValid = false;
        g_bVProbed = false;
        strcopy(g_sVStatus, sizeof(g_sVStatus), "地图/实体系统未就绪，脚本探测已推迟");
        TR_Log("依赖探测：Left4DHooks=%s SDKHooks=%s（地图未运行，跳过脚本探测）",
            g_bLeft4D ? "可用" : "不可用", g_bSdkHooks ? "可用" : "不可用");
        return;
    }
    TR_ProbeRuntime();
}

void TR_PrintStatus(int client)
{
    char mode[64];
    char map[128];
    char diff[32];
    TR_GetMode(mode, sizeof(mode));
    GetCurrentMap(map, sizeof(map));
    TR_GetDifficulty(diff, sizeof(diff));
    TR_Reply(client, "Tank Run 综合控制中心 v%s | 功能开关：%s", TR_VERSION,
        TR_FeatureEnabled() ? "开启" : "关闭");
    TR_Reply(client, "模式：%s | 地图：%s | 难度：%s | tick 间隔 %.4f 秒（%.0f tick）",
        mode, map, diff, TR_TickInterval(), TR_CurrentTickRate());
    TR_Reply(client, "依赖：Left4DHooks=%s | SDKHooks=%s | Tank Run 脚本=%s",
        g_bLeft4D ? "可用" : "不可用", g_bSdkHooks ? "可用" : "不可用", g_sVStatus);
    int nativeTanks = TR_CountTanks(false);
    int aiTanks = TR_CountTanks(true);
    TR_Reply(client, "Tank：AI %d 只 / 玩家 %d 只 | 自动生成：%s（间隔 %.0f 秒，上限 %d）",
        aiTanks, nativeTanks - aiTanks,
        g_cvAutoSpawn.BoolValue ? "开" : "关", g_cvAutoInterval.FloatValue, g_cvAutoLimit.IntValue);
    if (TR_IsTankRunMode())
    {
        int replicas = TC_GetLiveReplicas();
        bool mixed = TC_ManagedMode();
        TR_Reply(client, "统一生成：在场 %d/%d（补位 %d，开关%s）| 真人离开安全区：%s | 阶段：%s",
            nativeTanks + replicas, mixed ? g_cvAutoLimit.IntValue : TR_NativeTankLimit(nativeTanks), replicas, mixed ? "开" : "关",
            g_bHumanLeftStart ? "是" : "否", g_bFrontPhaseOpen ? "开放" : "暂停");
        TR_Reply(client, "刷点难度：%s（%s）", TR_LeadSpawnNeedsCover() ? "普通遮挡" : "高难度前线",
            g_cvSpawnVisibility.IntValue == 0 ? "跟随游戏难度" : "手动指定");
    }
    int connected = GetClientCount(false);
    TR_Reply(client, "客户端槽位：%d/%d | 剩余 %d | 交接保留 %d | 当前原版 Tank 上限 %d",
        connected, MaxClients, MaxClients - connected, TR_ReservedTankSlots(), TR_NativeTankLimit(nativeTanks));
    TR_Reply(client, "首石优先：%s | 武器入口禁拳：%s | 投石视角：%s",
        TR_FirstRockEnabled() ? "开" : "关", g_hFirstRockClawGate != null ? "已加载" : "未加载",
        g_cvRockNoTurn.BoolValue ? "不改变视角（统一抬手前）" : "按投石类型设置");
    int hsHumans, hsAfkBots, hsUnownedBots, hsSeatLimit, hsCountdowns;
    HS_GetDiagnosticStats(hsHumans, hsAfkBots, hsUnownedBots, hsSeatLimit, hsCountdowns);
    TR_Reply(client, "真人席位：%s（%s）| 真人 %d / AFK替身 %d / 空闲Bot %d / 上限 %d",
        HS_IsActive() ? "运行中" : "未生效",
        g_HS_YieldedToOther ? "已让权给旧版独立插件" : (g_HS_Enabled != null && g_HS_Enabled.BoolValue ? "内嵌模式" : "开关关闭"),
        hsHumans, hsAfkBots, hsUnownedBots, hsSeatLimit);
    TR_Reply(client, "挂机管理：阈值 %d 秒+10秒私有倒计时 | 倒计时中 %d 人 | 累计转旁观 %d 次 | 原生 director_afk_timeout=%d（%s）",
        g_HS_AfkTimeout != null ? g_HS_AfkTimeout.IntValue : 0, hsCountdowns, g_HS_StatAfkEvictions,
        g_HS_NativeAfkTimeout != null ? g_HS_NativeAfkTimeout.IntValue : -1,
        g_HS_ManagedNativeAfk ? "已托管防抢跑" : "原值");
    TR_Reply(client, "开场跳过：%s（累计 %d 次）| 团灭快进：%s（目标 %.2fs，偏移=%s，累计 %d 次/回退 %d 次，最近：%s）",
        TR_FlowIsIntroAllowed() ? "生效" : "未生效", g_iStatIntroSkipped,
        TR_FlowIsFastWipeAllowed() ? "生效" : "未生效", g_cvWipeDelay.FloatValue,
        g_bWipeGamedataLoaded ? "已加载" : "未加载", g_iStatWipeAccelerated, g_iStatWipeFallbacks, g_sWipeLastResult);
    TR_Reply(client, "取消友伤：%s（拦截直接 %d / 延迟 %d / 油桶栈=%s）| 取消水中减速：%s（%s）",
        TR_NoFriendlyFireActive() ? "生效" : "未生效", g_iStatFFBlockedDirect, g_iStatFFBlockedDelayed,
        g_bFFFuelBarrelDetourReady ? "已启用" : "降级",
        TR_NoWaterSlowdownActive() ? "生效" : "未生效", g_sWaterStatus);
    TR_Reply(client, "死门模式：%s（%s）| 骗拳处死：%s（原版 %d / 补位 %d / 死亡阻断 %d / 障碍排除 %d / 原版因果=%s）",
        g_bDeathsDoorActive ? "开" : "关", g_sDeathsDoorLastResult,
        TR_BaitPunchEnabled() ? "开" : "关", g_iStatBaitPunchSlaysNative, g_iStatBaitPunchSlaysReplica,
        g_iStatBaitPunchBlockedDeath, g_iStatBaitPunchExcludedObstacle, g_sBaitNativeStatus);
    int brCandN, brCandR, brSlayN, brSlayR, brHit, brCancel;
    TR_BaitRockGetStats(brCandN, brCandR, brSlayN, brSlayR, brHit, brCancel);
    TR_Reply(client, "骗饼处死：%s（开关=%d 原版目标=%s | 待结算 %d | 候选 原版%d/补位%d | 处死 原版%d/补位%d | 命中免死 %d | 取消 %d）",
        TR_BaitRockEnabled() ? "生效" : "未生效",
        (g_cvBaitRockSlay != null && g_cvBaitRockSlay.BoolValue) ? 1 : 0,
        g_sGlowNativeStatus, TR_BaitRockPendingCount(),
        brCandN, brCandR, brSlayN, brSlayR, brHit, brCancel);
    TR_Reply(client, "仇恨提示：%s（在用代理 %d / 峰值 %d / 组 %d/8 / 溢出 %d / 原版目标=%s / 读数 %d / 锁定命中 %d）| 轮换跳过保护 %d / 延迟 %d",
        TR_RockGlowEnabled() ? "开" : "关", g_iGlowLiveProxies, g_iGlowPeakProxies,
        TR_Glow_CountActiveGroups(), g_iGlowOverflowCount,
        g_sGlowNativeStatus, g_iStatGlowNativeReads, g_iStatGlowNativeLockHits,
        g_iStatRotateSkippedProtected, g_iStatRotateDeferred);
    TR_Reply(client, "累计：生成 %d（失败 %d）| 投石释放 %d | 投石拦截 %d+%d | 拳击 %d/%d | 首石成功 %d 超时 %d",
        g_iStatSpawns, g_iStatSpawnFail, g_iStatReleases, g_iStatThrowGateBlocked,
        g_iStatDelayedBlocked, g_iStatSwings, g_iStatHits, g_iStatFirstRockSuccess,
        g_iStatFirstRockTimeout);
    TR_Reply(client, "参数写入 %d 次 | 冲突 %d 次 | 记录原始值 %d 项 | 未实现功能见帮助菜单",
        g_iStatParamsWritten, g_iStatConflicts, TR_CountOriginals());
    TR_Reply(client, "时间换算：感知扫描 %.2f 秒 = %d tick（按引擎 tick 间隔，非固定值）",
        g_cvTargetScan.FloatValue, TR_SecondsToTicks(g_cvTargetScan.FloatValue));

    if (!g_bLeft4D)
    {
        TR_Reply(client, "警告：Left4DHooks 原生不可用，Tank 生成/投石/拳击事件将无法工作。");
    }
    if (TR_IsTankRunMode() && !g_bVValid)
    {
        TR_Reply(client, "警告：tankrun 模式下脚本接口未连接，脚本参数不可读写。");
    }
}

int TR_CountOriginals()
{
    int count = 0;
    for (int i = 0; i < g_iParamCount; i++)
    {
        if (g_bParamHasOriginal[i])
        {
            count++;
        }
    }
    return count;
}

void TR_WriteDiagnosticDump(char[] path, int maxlen)
{
    BuildPath(Path_SM, path, maxlen, "logs/tankrun_control_dump.txt");
    File file = OpenFile(path, "w");
    if (file == null)
    {
        TR_Log("诊断导出失败：无法写入 %s", path);
        path[0] = '\0';
        return;
    }

    char mode[64];
    char map[128];
    char diff[32];
    TR_GetMode(mode, sizeof(mode));
    GetCurrentMap(map, sizeof(map));
    TR_GetDifficulty(diff, sizeof(diff));

    bool mapRunning = TR_IsMapRunning();
    if (mapRunning && TR_IsTankRunMode()) TR_VScriptProbe(true);

    file.WriteLine("# Tank Run 综合控制中心 诊断快照");
    file.WriteLine("version=%s", TR_VERSION);
    file.WriteLine("game_time=%.2f tick_interval=%.6f tickrate=%.2f",
        GetGameTime(), TR_TickInterval(), TR_CurrentTickRate());
    file.WriteLine("map=%s mode=%s difficulty=%s", map, mode, diff);
    file.WriteLine("plugin_enabled=%d left4dhooks=%d sdkhooks=%d vscript_valid=%d",
        TR_FeatureEnabled() ? 1 : 0, g_bLeft4D ? 1 : 0, g_bSdkHooks ? 1 : 0, g_bVValid ? 1 : 0);
    file.WriteLine("vscript_status=%s", g_sVStatus);

    ConVar slotSetting = FindConVar("sv_setmax");
    int connected = GetClientCount(false);
    file.WriteLine("max_clients=%d connected_clients=%d free_client_slots=%d sv_setmax=%d",
        MaxClients, connected, MaxClients - connected, slotSetting == null ? -1 : slotSetting.IntValue);
    int directorTankCount = -1;
    if (mapRunning && GetFeatureStatus(FeatureType_Native, "L4D2_GetTankCount") == FeatureStatus_Available)
    {
        directorTankCount = L4D2_GetTankCount();
    }
    int nativeTanks = TR_CountTanks(false);
    int dyingAiSlots = TR_CountDyingAiTankSlots();
    int replicas = TC_GetLiveReplicas();
    int nativeLimit = TR_NativeTankLimit(nativeTanks);
    bool mixed = TC_ManagedMode();
    file.WriteLine("director_tank_count=%d live_tanks=%d dying_ai_tank_slots=%d director_vs_live_delta=%d mounted_gun_survivor=%d",
        directorTankCount, nativeTanks, dyingAiSlots,
        directorTankCount >= 0 ? directorTankCount - nativeTanks : 0,
        TR_FindMountedGunSurvivor());
    file.WriteLine("# note: director_tank_count includes dying Tanks during native 6s leave window (Body+0x1c==0)");
    file.WriteLine("front_live_total=%d front_replicas=%d front_limit=%d human_left_start=%d phase_open=%d",
        nativeTanks + replicas, replicas, mixed ? g_cvAutoLimit.IntValue : nativeLimit, g_bHumanLeftStart, g_bFrontPhaseOpen);
    file.WriteLine("configured_tank_limit=%d native_tank_limit=%d reserved_handoff_slots=%d replica_enabled=%d",
        g_cvAutoLimit.IntValue, nativeLimit, TR_ReservedTankSlots(), mixed);
    int hsHumans, hsAfkBots, hsUnownedBots, hsSeatLimit, hsCountdowns;
    HS_GetDiagnosticStats(hsHumans, hsAfkBots, hsUnownedBots, hsSeatLimit, hsCountdowns);
    file.WriteLine("human_seats_active=%d human_seats_yielded=%d humans=%d afk_bots=%d unowned_bots=%d seat_limit=%d afk_timeout=%d afk_countdowns=%d afk_evictions=%d native_afk_managed=%d native_afk_val=%d",
        HS_IsActive() ? 1 : 0, g_HS_YieldedToOther ? 1 : 0, hsHumans, hsAfkBots, hsUnownedBots, hsSeatLimit,
        g_HS_AfkTimeout != null ? g_HS_AfkTimeout.IntValue : 0, hsCountdowns, g_HS_StatAfkEvictions,
        g_HS_ManagedNativeAfk ? 1 : 0, g_HS_NativeAfkTimeout != null ? g_HS_NativeAfkTimeout.IntValue : -1);
    file.WriteLine("skip_intro_allowed=%d skip_intro_count=%d skip_intro_last_elapsed=%.2f",
        TR_FlowIsIntroAllowed() ? 1 : 0, g_iStatIntroSkipped, g_fIntroLastElapsed);
    file.WriteLine("fast_wipe_allowed=%d fast_wipe_delay=%.2f fast_wipe_gamedata=%d fast_wipe_offset=%d fast_wipe_count=%d fast_wipe_fallbacks=%d fast_wipe_last_elapsed=%.2f fast_wipe_last_result=%s",
        TR_FlowIsFastWipeAllowed() ? 1 : 0, g_cvWipeDelay.FloatValue, g_bWipeGamedataLoaded ? 1 : 0,
        g_iOffDirectorRestartTimer, g_iStatWipeAccelerated, g_iStatWipeFallbacks, g_fWipeLastElapsed, g_sWipeLastResult);
    file.WriteLine("no_friendly_fire=%d ff_blocked_direct=%d ff_blocked_delayed=%d ff_barrel_detour=%d",
        TR_NoFriendlyFireActive() ? 1 : 0, g_iStatFFBlockedDirect, g_iStatFFBlockedDelayed, g_bFFFuelBarrelDetourReady ? 1 : 0);
    file.WriteLine("no_water_slowdown=%d water_applied=%d water_status=%s",
        TR_NoWaterSlowdownActive() ? 1 : 0, g_bWaterApplied ? 1 : 0, g_sWaterStatus);
    file.WriteLine("deaths_door_active=%d deaths_door_cvar=%d deaths_door_last=%s",
        g_bDeathsDoorActive ? 1 : 0, g_cvDeathsDoor != null ? g_cvDeathsDoor.IntValue : 0, g_sDeathsDoorLastResult);
    file.WriteLine("bait_punch_enabled=%d bait_native_cause_ready=%d bait_slays_native=%d bait_slays_replica=%d bait_blocked_death=%d bait_excluded_obstacle=%d",
        TR_BaitPunchEnabled() ? 1 : 0, TR_Bait_IsNativeCauseReady() ? 1 : 0, g_iStatBaitPunchSlaysNative, g_iStatBaitPunchSlaysReplica,
        g_iStatBaitPunchBlockedDeath, g_iStatBaitPunchExcludedObstacle);
    int brCandN, brCandR, brSlayN, brSlayR, brHit, brCancel;
    TR_BaitRockGetStats(brCandN, brCandR, brSlayN, brSlayR, brHit, brCancel);
    file.WriteLine("bait_rock_enabled=%d bait_rock_cvar=%d bait_rock_native_ready=%d bait_rock_pending=%d bait_rock_cand_native=%d bait_rock_cand_replica=%d bait_rock_slays_native=%d bait_rock_slays_replica=%d bait_rock_blocked_hit=%d bait_rock_cancelled=%d bait_rock_native_status=%s",
        TR_BaitRockEnabled() ? 1 : 0, (g_cvBaitRockSlay != null && g_cvBaitRockSlay.BoolValue) ? 1 : 0,
        TR_Glow_IsNativeTargetPathEnabled() ? 1 : 0, TR_BaitRockPendingCount(),
        brCandN, brCandR, brSlayN, brSlayR, brHit, brCancel, g_sGlowNativeStatus);
    file.WriteLine("rock_glow_enabled=%d glow_live_proxies=%d glow_peak_proxies=%d glow_active_groups=%d glow_overflow=%d glow_native_path=%d",
        TR_RockGlowEnabled() ? 1 : 0, g_iGlowLiveProxies, g_iGlowPeakProxies,
        TR_Glow_CountActiveGroups(), g_iGlowOverflowCount, TR_Glow_IsNativeTargetPathEnabled() ? 1 : 0);
    file.WriteLine("rotate_skipped_protected=%d rotate_deferred=%d",
        g_iStatRotateSkippedProtected, g_iStatRotateDeferred);
    if (TR_IsTankRunMode())
        file.WriteLine("spawn_visibility_setting=%d spawn_cover_required=%d", g_cvSpawnVisibility.IntValue, TR_LeadSpawnNeedsCover() ? 1 : 0);

    file.WriteLine("# 脚本字段");
    file.WriteLine("SessionState.SpawnInterval=%.2f", g_fVSpawnInterval);
    file.WriteLine("SessionOptions.cm_TankLimit=%d", g_iVTankLimit);
    file.WriteLine("SessionState.DoubleTanks=%d", g_bVDoubleTanks ? 1 : 0);
    file.WriteLine("SessionState.SpawnTankThink=%d", g_bVThink ? 1 : 0);
    file.WriteLine("SessionState.TankHealth=%d", g_iVTankHealth);

    file.WriteLine("# 参数（kind: 0=引擎 1=插件 2=脚本；unit: 1=整数）");
    for (int i = 0; i < g_iParamCount; i++)
    {
        float value;
        bool readable = TR_ParamRead(i, value);
        char current[32];
        if (readable)
        {
            Format(current, sizeof(current), "%.4f", value);
        }
        else
        {
            strcopy(current, sizeof(current), "unavailable");
        }
        file.WriteLine("param=%s kind=%d section=%d current=%s original=%s writes=%d verified=%d note=%s",
            g_sParamName[i], g_iParamKind[i], g_iParamSection[i], current,
            g_bParamHasOriginal[i] ? "recorded" : "none", g_iParamWriteCount[i],
            g_bParamVerified[i] ? 1 : 0, g_sParamNote[i]);
    }

    file.WriteLine("# Tank 实例");
    file.WriteLine("live_ai_tanks=%d peak_live_ai_tanks=%d", TR_CountTanks(true), g_iPeakLiveAiTanks);
    for (int i = 1; i <= MaxClients; i++)
    {
        if (!TR_IsTankClient(i))
        {
            continue;
        }
        file.WriteLine("tank client=%d userid=%d ai=%d tracked=%d hp=%d maxhp=%d spawn_age=%.1f",
            i, GetClientUserId(i), IsFakeClient(i) ? 1 : 0, g_bTankTracked[i] ? 1 : 0,
            GetClientHealth(i), GetEntProp(i, Prop_Data, "m_iMaxHealth"),
            g_fTankSpawnTime[i] > 0.0 ? GetGameTime() - g_fTankSpawnTime[i] : -1.0);
        if (!g_bTankTracked[i])
        {
            continue;
        }
        file.WriteLine("  state first_contact=%d first_age=%.1f first_done=%d window=%d deadline=%.1f abs_deadline=%.1f attempts=%d strategy_off=%d staggering=%d stamina=%.1f",
            g_bTankFirstContact[i] ? 1 : 0,
            g_fTankFirstContactTime[i] > 0.0 ? GetGameTime() - g_fTankFirstContactTime[i] : -1.0,
            g_bTankFirstRockDone[i] ? 1 : 0,
            g_bTankRockWindow[i] ? 1 : 0, g_fTankRockDeadline[i], g_fTankRockAbsoluteDeadline[i],
            g_iTankRockAttempts[i], g_bTankStrategyOff[i] ? 1 : 0,
            TR_IsTankStaggering(i) ? 1 : 0, TR_GetTankStamina(i));
        file.WriteLine("  counts rock=%d blocked=%d swing=%d hit=%d overrides=%d last_activity=%d chosen_target=%d",
            g_iTankReleaseCount[i], g_iTankBlockedCount[i], g_iTankSwingCount[i],
            g_iTankHitCount[i], g_iTankOverrides[i], g_iTankLastActivity[i],
            g_iTankChosenTarget[i]);
        char detail[512];
        TR_FormatTankDetail(i, detail, sizeof(detail));
        file.WriteLine("%s", detail);
    }

    file.WriteLine("auto_spawn_interval=%.2f ticks=%d roundtrip=%.4f", g_cvAutoInterval.FloatValue, TR_SecondsToTicks(g_cvAutoInterval.FloatValue), TR_TicksToSeconds(TR_SecondsToTicks(g_cvAutoInterval.FloatValue)));
    file.WriteLine("# 统计");
    file.WriteLine("spawns=%d spawn_fail=%d releases=%d blocked_gate=%d blocked_delay=%d activation_no_release=%d stagger_cancel=%d quick_firstrock=%d swings=%d hits=%d overrides=%d firstrock_ok=%d firstrock_timeout=%d params_written=%d conflicts=%d",
        g_iStatSpawns, g_iStatSpawnFail, g_iStatReleases, g_iStatThrowGateBlocked,
        g_iStatDelayedBlocked, g_iStatActivationNoRelease, g_iStatThrowStaggerCancel, g_iStatQuickFirstRock, g_iStatSwings, g_iStatHits, g_iStatOverrides,
        g_iStatFirstRockSuccess, g_iStatFirstRockTimeout, g_iStatParamsWritten, g_iStatConflicts);

    delete file;
    strcopy(g_sLastDumpPath, sizeof(g_sLastDumpPath), path);
    TR_Log("诊断文件已写入 %s", path);
}
