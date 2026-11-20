// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

// Tank Run 综合控制中心 - 核心层
// 参数注册、配置持久化、VScript 桥接、日志与时间换算。
// 本文件只使用 SourceMod / SDKTools / Left4DHooks 公开接口。

#if defined _tankrun_core_included
 #endinput
#endif
#define _tankrun_core_included

#include <sourcemod>
#include <sdktools>
#include <sdkhooks>
#include <left4dhooks>

#define TR_VERSION      "2.2.8"
#define TR_TAG          "[TankRun]"
#define TR_PARAM_MAX    57
#define TR_QUICK_MAX    4
#define TR_PRESET_SLOTS 8
#define TR_BRIDGE       "tankrun_bridge"
#define TR_SESSION_EXTRA 5
#define TR_NUMERIC_MAX  3.402823466e38
#define TR_INT_MAX      2147483520.0  // Largest float below the signed 32-bit overflow boundary.

enum TR_Section
{
    SEC_SPAWN = 0,
    SEC_HEALTH,
    SEC_CLAW,
    SEC_ROCK,
    SEC_TARGET,
    SEC_POS,
    SEC_RULES,
    SEC_INSTANCE,
    SEC_PRESET,
    SEC_MONITOR,
    SEC_STATUS,
    SEC_COUNT
};

enum TR_PKind
{
    TRK_ENGINE = 0,     // 引擎原生 ConVar
    TRK_PLUGIN,         // 本插件 ConVar
    TRK_VSCRIPT         // Tank Run 模式脚本字段
};

enum TR_VField
{
    TRV_NONE = 0,
    TRV_INTERVAL,
    TRV_LIMIT,
    TRV_DOUBLE,
    TRV_TANKHEALTH
};

enum TR_SpawnMode
{
    TRSP_NATIVE = 0,    // 原生随机区域选位
    TRSP_BAND,          // 距离带（围绕幸存者）
    TRSP_AIM,           // 准星/视线位置
    TRSP_VSCRIPT,       // Tank Run 脚本 ZSpawn
    TRSP_NEAR,          // 幸存者附近原生选位
    TRSP_COUNT
};

enum TR_TargetMode
{
    TRT_NATIVE = 0,
    TRT_NEAREST,
    TRT_FARTHEST,
    TRT_LOWHP,
    TRT_FIXED,
    TRT_COUNT
};

// ---------------------------------------------------------------- 插件 ConVar
ConVar g_cvEnabled;
ConVar g_cvPublicReadonly;
ConVar g_cvDebug;
ConVar g_cvLogEvents;
ConVar g_cvAiOnly;
ConVar g_cvConflictMode;
ConVar g_cvDefaultExpert;

ConVar g_cvAutoSpawn;
ConVar g_cvAutoInterval;
ConVar g_cvAutoLimit;
ConVar g_cvRotateOldest;
ConVar g_cvAutoDouble;
ConVar g_cvSpawnMode;
ConVar g_cvSpawnVisibility;
ConVar g_cvSpawnMin;
ConVar g_cvSpawnMax;
ConVar g_cvSpawnAttempts;
ConVar g_cvSpawnFallback;

ConVar g_cvTargetMode;
ConVar g_cvTargetFixed;
ConVar g_cvTargetScan;

ConVar g_cvRockEnabled;
ConVar g_cvRockMinDelay;
ConVar g_cvRockForceScale;

ConVar g_cvFirstRock;
ConVar g_cvFirstRockTimeout;
ConVar g_cvFirstRockAttempts;

ConVar g_cvClawScale;
ConVar g_cvClawBlock;
ConVar g_cvTankDmgScale;
ConVar g_cvFireScale;

ConVar g_cvTankHealth;
ConVar g_cvTankMaxHealth;
ConVar g_cvModeHealth;
ConVar g_cvSpeedScale;

ConVar g_cvBridge;

// ---------------------------------------------------------------- 参数表
char  g_sParamName[TR_PARAM_MAX][64];
char  g_sParamLabel[TR_PARAM_MAX][96];
char  g_sParamNote[TR_PARAM_MAX][160];
float g_fParamMin[TR_PARAM_MAX];
float g_fParamMax[TR_PARAM_MAX];
float g_fParamStep[TR_PARAM_MAX];
bool  g_bParamInt[TR_PARAM_MAX];
bool  g_bParamVerified[TR_PARAM_MAX];
TR_Section g_iParamSection[TR_PARAM_MAX];
TR_PKind   g_iParamKind[TR_PARAM_MAX];
TR_VField  g_iParamVField[TR_PARAM_MAX];
float g_fParamQuick[TR_PARAM_MAX][TR_QUICK_MAX];
int   g_iParamQuickCount[TR_PARAM_MAX];
int   g_iParamCount = 0;

ConVar g_ParamCVar[TR_PARAM_MAX];
float  g_fParamOriginal[TR_PARAM_MAX];
bool   g_bParamHasOriginal[TR_PARAM_MAX];
float  g_fParamLastWrite[TR_PARAM_MAX];
bool   g_bParamHasLastWrite[TR_PARAM_MAX];
int    g_iParamWriteCount[TR_PARAM_MAX];
ConVar g_ParamSession[TR_PARAM_MAX];
ConVar g_ExtraConfig[TR_SESSION_EXTRA];
ConVar g_ExtraSession[TR_SESSION_EXTRA];
bool g_bSessionReady;
bool g_bRestoringSession;

// ---------------------------------------------------------------- 运行环境
char g_sMap[128];
char g_sMode[64];
char g_sLogPath[PLATFORM_MAX_PATH];
char g_sSettingsPath[PLATFORM_MAX_PATH];
char g_sOriginalsPath[PLATFORM_MAX_PATH];
char g_sPresetDir[PLATFORM_MAX_PATH];

bool g_bLeft4D = false;
bool g_bSdkHooks = false;

// 地图运行态标志。OnMapEnd 置 true，OnMapStart 置 false。
// 任何需要 CreateEntityByName / DispatchSpawn / 生成 Tank 的路径都必须先通过
// TR_IsMapRunning()，否则引擎会报 "Cannot create new entity when no map is running"。
bool g_bMapEnding = false;

// Tank Run 脚本快照（逻辑脚本 SessionState / SessionOptions）
bool  g_bVValid = false;
bool  g_bVProbed = false;
char  g_sVStatus[128];
float g_fVSpawnInterval = 0.0;
int   g_iVTankLimit = 0;
bool  g_bVDoubleTanks = false;
bool  g_bVThink = false;
bool  g_bScriptThinkDisabled;
int   g_iOriginalStockLimit;
int   g_iLastManagedStockLimit;
int   g_iVTankHealth = 0;

// 累计统计（诊断用）
int g_iStatSpawns = 0;
int g_iStatSpawnFail = 0;
int g_iStatReleases = 0;
int g_iStatSwings = 0;
int g_iStatHits = 0;
int g_iStatOverrides = 0;
int g_iStatActivationNoRelease = 0;
int g_iStatFirstRockSuccess = 0;
int g_iStatFirstRockTimeout = 0;
int g_iStatParamsWritten = 0;
int g_iStatConflicts = 0;

// ---------------------------------------------------------------- 基础工具
void TR_Chat(int client, const char[] fmt, any ...)
{
    char buf[512];
    VFormat(buf, sizeof(buf), fmt, 3);
    if (client > 0 && client <= MaxClients && IsClientInGame(client))
    {
        PrintToChat(client, "%s %s", TR_TAG, buf);
    }
    else
    {
        PrintToServer("%s %s", TR_TAG, buf);
    }
}

void TR_Reply(int client, const char[] fmt, any ...)
{
    char buf[512];
    VFormat(buf, sizeof(buf), fmt, 3);
    if (client > 0 && client <= MaxClients && IsClientInGame(client))
    {
        PrintToChat(client, "%s %s", TR_TAG, buf);
        return;
    }
    PrintToServer("%s %s", TR_TAG, buf);
}

void TR_Log(const char[] fmt, any ...)
{
    char buf[512];
    VFormat(buf, sizeof(buf), fmt, 2);
    LogToFile(g_sLogPath, "[%.2f] %s", GetGameTime(), buf);
    if (g_cvDebug != null && g_cvDebug.BoolValue)
    {
        PrintToServer("%s %s", TR_TAG, buf);
    }
}

void TR_LogEvent(const char[] fmt, any ...)
{
    if (g_cvLogEvents == null || !g_cvLogEvents.BoolValue)
    {
        return;
    }
    char buf[512];
    VFormat(buf, sizeof(buf), fmt, 2);
    LogToFile(g_sLogPath, "[%.2f] EVENT %s", GetGameTime(), buf);
}

bool TR_FeatureEnabled()
{
    return g_cvEnabled != null && g_cvEnabled.BoolValue;
}

bool TR_IsAdmin(int client)
{
    if (client <= 0 || client > MaxClients || !IsClientInGame(client))
    {
        return false;
    }
    return CheckCommandAccess(client, "sm_tank", ADMFLAG_CONFIG);
}

bool TR_CanOpen(int client)
{
    if (TR_IsAdmin(client))
    {
        return true;
    }
    // 允许通过配置开放只读菜单，但只读菜单不能执行任何修改。
    return client > 0 && client <= MaxClients && IsClientInGame(client)
        && g_cvPublicReadonly != null && g_cvPublicReadonly.BoolValue;
}

bool TR_CanEdit(int client)
{
    return TR_IsAdmin(client) && TR_FeatureEnabled();
}

// 128 Tick 等：一律使用引擎 tick 间隔换算，不写死 tick 数。
float TR_TickInterval()
{
    float interval = GetTickInterval();
    if (interval <= 0.0)
    {
        interval = 0.0078125; // 128 tick 回退值，仅用于显示
    }
    return interval;
}

int TR_SecondsToTicks(float seconds)
{
    if (seconds <= 0.0)
    {
        return 0;
    }
    return RoundToNearest(seconds / TR_TickInterval());
}

float TR_TicksToSeconds(int ticks)
{
    return float(ticks) * TR_TickInterval();
}

float TR_CurrentTickRate()
{
    float interval = TR_TickInterval();
    if (interval <= 0.0)
    {
        return 0.0;
    }
    return 1.0 / interval;
}

void TR_GetMode(char[] buffer, int maxlen)
{
    ConVar mode = FindConVar("mp_gamemode");
    if (mode == null)
    {
        strcopy(buffer, maxlen, "<不可用>");
        return;
    }
    mode.GetString(buffer, maxlen);
    if (buffer[0] == '\0')
    {
        strcopy(buffer, maxlen, "<空>");
    }
}

bool TR_IsTankRunMode()
{
    char mode[64];
    TR_GetMode(mode, sizeof(mode));
    return StrContains(mode, "tankrun", false) != -1;
}

// 地图与实体系统是否真正就绪。
// 只做“读取”（地图名、实体数量、worldspawn 是否存在），不创建任何实体，因此可安全用于
// OnPluginStart / 定时器 / 命令等任意上下文。注意：地图名已存在并不代表实体系统就绪
// （服务器冷启动、换图期间就是这种情况），所以必须同时检查 worldspawn。
bool TR_IsMapRunning()
{
    if (g_bMapEnding)
    {
        return false;
    }
    char map[PLATFORM_MAX_PATH];
    GetCurrentMap(map, sizeof(map));
    if (map[0] == '\0')
    {
        return false;
    }
    // 未加载地图时实体表为空，短路后不再调用 FindEntityByClassname。
    if (GetEntityCount() <= 0)
    {
        return false;
    }
    return FindEntityByClassname(-1, "worldspawn") != -1;
}

void TR_GetDifficulty(char[] buffer, int maxlen)
{
    ConVar diff = FindConVar("z_difficulty");
    if (diff == null)
    {
        strcopy(buffer, maxlen, "<不可用>");
        return;
    }
    diff.GetString(buffer, maxlen);
}

// ---------------------------------------------------------------- 参数注册
int TR_AddParam(TR_Section section, TR_PKind kind, const char[] name, const char[] label,
    float min, float max, float step, bool isInt, bool verified, TR_VField vfield,
    const char[] note)
{
    if (g_iParamCount >= TR_PARAM_MAX)
    {
        TR_Log("参数表已满，忽略 %s", name);
        return -1;
    }
    int idx = g_iParamCount++;
    strcopy(g_sParamName[idx], sizeof(g_sParamName[]), name);
    strcopy(g_sParamLabel[idx], sizeof(g_sParamLabel[]), label);
    strcopy(g_sParamNote[idx], sizeof(g_sParamNote[]), note);
    g_fParamMin[idx] = min;
    g_fParamMax[idx] = max;
    g_fParamStep[idx] = step;
    g_bParamInt[idx] = isInt;
    g_bParamVerified[idx] = verified;
    g_iParamSection[idx] = section;
    g_iParamKind[idx] = kind;
    g_iParamVField[idx] = vfield;
    g_ParamCVar[idx] = null;
    g_iParamQuickCount[idx] = 0;
    return idx;
}

void TR_Quick(int idx, float a, float b = -1.0, float c = -1.0, float d = -1.0)
{
    if (idx < 0 || idx >= g_iParamCount)
    {
        return;
    }
    float values[TR_QUICK_MAX];
    values[0] = a;
    values[1] = b;
    values[2] = c;
    values[3] = d;
    int count = 0;
    for (int i = 0; i < TR_QUICK_MAX; i++)
    {
        if (values[i] < 0.0)
        {
            break;
        }
        g_fParamQuick[idx][count++] = values[i];
    }
    g_iParamQuickCount[idx] = count;
}

bool TR_IsBoolParam(int idx)
{
    return idx >= 0 && idx < g_iParamCount && g_bParamInt[idx]
        && g_fParamMin[idx] == 0.0 && g_fParamMax[idx] == 1.0;
}

int TR_FindParamByName(const char[] name)
{
    for (int i = 0; i < g_iParamCount; i++)
    {
        if (StrEqual(g_sParamName[i], name, false))
        {
            return i;
        }
    }
    return -1;
}

int TR_FindParamByVField(TR_VField vfield)
{
    for (int i = 0; i < g_iParamCount; i++)
    {
        if (g_iParamKind[i] == TRK_VSCRIPT && g_iParamVField[i] == vfield)
        {
            return i;
        }
    }
    return -1;
}

void TR_BindConVars()
{
    for (int i = 0; i < g_iParamCount; i++)
    {
        if (g_iParamKind[i] == TRK_VSCRIPT)
        {
            continue;
        }
        g_ParamCVar[i] = FindConVar(g_sParamName[i]);
    }
}

// 读取当前值（不经冲突处理，供写入前比较与显示）
bool TR_ParamRead(int idx, float &value)
{
    if (idx < 0 || idx >= g_iParamCount)
    {
        return false;
    }

    if (g_iParamKind[idx] == TRK_VSCRIPT)
    {
        switch (g_iParamVField[idx])
        {
            case TRV_INTERVAL:    { value = g_cvAutoInterval.FloatValue; return true; }
            case TRV_LIMIT:       { value = g_cvAutoSpawn.BoolValue ? float(g_cvAutoLimit.IntValue) : 0.0; return true; }
            case TRV_DOUBLE:      { value = g_cvAutoDouble.BoolValue ? 1.0 : 0.0; return true; }
            case TRV_TANKHEALTH:  { if (!g_bVValid) return false; value = float(g_iVTankHealth); return true; }
        }
        return false;
    }

    ConVar cv = g_ParamCVar[idx];
    if (cv == null)
    {
        return false;
    }
    if (g_bParamInt[idx])
    {
        value = float(cv.IntValue);
    }
    else
    {
        value = cv.FloatValue;
    }
    return true;
}

float TR_ParamDefault(int idx)
{
    if (idx < 0 || idx >= g_iParamCount)
    {
        return 0.0;
    }
    ConVar cv = g_ParamCVar[idx];
    if (cv == null)
    {
        return 0.0;
    }
    char buf[64];
    cv.GetDefault(buf, sizeof(buf));
    return StringToFloat(buf);
}

void TR_FormatValue(int idx, char[] buffer, int maxlen)
{
    float value;
    if (!TR_ParamRead(idx, value))
    {
        strcopy(buffer, maxlen, "不可用");
        return;
    }
    if (TR_IsBoolParam(idx))
    {
        strcopy(buffer, maxlen, value != 0.0 ? "开" : "关");
    }
    else if (g_bParamInt[idx])
    {
        Format(buffer, maxlen, "%d", RoundToNearest(value));
    }
    else
    {
        TR_FormatNumber(value, buffer, maxlen);
    }
}

void TR_ParamLabelValue(int idx, char[] buffer, int maxlen)
{
    char value[32];
    TR_FormatValue(idx, value, sizeof(value));
    if (g_iParamKind[idx] == TRK_VSCRIPT && !g_bVValid)
    {
        Format(buffer, maxlen, "%s：%s（脚本未就绪）", g_sParamLabel[idx], value);
        return;
    }
    if (g_ParamCVar[idx] == null && g_iParamKind[idx] != TRK_VSCRIPT)
    {
        Format(buffer, maxlen, "%s：本构建未注册", g_sParamLabel[idx]);
        return;
    }
    Format(buffer, maxlen, "%s：%s", g_sParamLabel[idx], value);
}

// ---------------------------------------------------------------- 原始值持久化
void TR_SaveOriginals()
{
    StringMap map = new StringMap();
    for (int i = 0; i < g_iParamCount; i++)
    {
        if (!g_bParamHasOriginal[i])
        {
            continue;
        }
        char buf[64];
        if (g_bParamInt[i])
        {
            Format(buf, sizeof(buf), "%d", RoundToNearest(g_fParamOriginal[i]));
        }
        else
        {
            Format(buf, sizeof(buf), "%.6f", g_fParamOriginal[i]);
        }
        map.SetString(g_sParamName[i], buf);
    }
    TR_WriteKeyValueFile(g_sOriginalsPath, map);
    delete map;
}

void TR_LoadOriginals()
{
    StringMap map = new StringMap();
    if (!TR_ReadKeyValueFile(g_sOriginalsPath, map))
    {
        delete map;
        return;
    }
    for (int i = 0; i < g_iParamCount; i++)
    {
        char buf[64];
        if (map.GetString(g_sParamName[i], buf, sizeof(buf)))
        {
            g_fParamOriginal[i] = StringToFloat(buf);
            g_bParamHasOriginal[i] = true;
        }
    }
    delete map;
}

// 引擎写入前记录初始值；首次接触时记录，之后不覆盖。
void TR_RecordOriginal(int idx, float current)
{
    if (g_bParamHasOriginal[idx])
    {
        return;
    }
    g_fParamOriginal[idx] = current;
    g_bParamHasOriginal[idx] = true;
    TR_SaveOriginals();
}

// Format small and large numbers without rounding subsecond settings to zero.
void TR_FormatNumber(float value, char[] buffer, int maxlen)
{
    float magnitude = FloatAbs(value);
    if (!(magnitude <= TR_NUMERIC_MAX))
    {
        strcopy(buffer, maxlen, "invalid");
        return;
    }
    if (magnitude != 0.0 && (magnitude < 0.01 || magnitude >= 10000000.0))
    {
        int exponent;
        while (magnitude < 1.0)
        {
            magnitude *= 10.0;
            exponent--;
        }
        while (magnitude >= 10.0)
        {
            magnitude /= 10.0;
            exponent++;
        }
        Format(buffer, maxlen, "%.8fe%d", value < 0.0 ? -magnitude : magnitude, exponent);
        return;
    }
    Format(buffer, maxlen, "%.9f", value);
    int end = strlen(buffer) - 1;
    while (end > 0 && buffer[end] == '0') buffer[end--] = '\0';
    if (end > 0 && buffer[end] == '.') buffer[end] = '\0';
}

bool TR_IsPositiveSpawnInterval(int idx)
{
    return StrEqual(g_sParamName[idx], "tankrun_auto_interval") || g_iParamVField[idx] == TRV_INTERVAL;
}

// ---------------------------------------------------------------- 参数写入
bool TR_ClampValue(int idx, float &value)
{
    if (!(FloatAbs(value) <= TR_NUMERIC_MAX) || (TR_IsPositiveSpawnInterval(idx) && value <= 0.0))
    {
        return false;
    }

    // The registry retains the old menu range for display. Only semantic and execution bounds apply here.
    bool fixedRange = TR_IsBoolParam(idx)
        || StrEqual(g_sParamName[idx], "tankrun_spawn_mode")
        || StrEqual(g_sParamName[idx], "tankrun_spawn_visibility")
        || StrEqual(g_sParamName[idx], "tankrun_auto_limit")
        || g_iParamVField[idx] == TRV_LIMIT
        || StrEqual(g_sParamName[idx], "tankrun_target_mode")
        || StrEqual(g_sParamName[idx], "tankrun_target_fixed")
        || StrEqual(g_sParamName[idx], "tankrun_spawn_attempts")
        || StrEqual(g_sParamName[idx], "tank_throw_max_loft_angle")
        || StrEqual(g_sParamName[idx], "tank_rock_overhead_percent");
    float min = fixedRange ? g_fParamMin[idx] : 0.0;
    float max = fixedRange ? g_fParamMax[idx] : TR_NUMERIC_MAX;
    if (g_bParamInt[idx] && !fixedRange)
    {
        max = TR_INT_MAX;
        if (g_iParamVField[idx] == TRV_TANKHEALTH || StrEqual(g_sParamName[idx], "tankrun_firstrock_attempts")) min = 1.0;
    }
    if (value < min) value = min;
    if (value > max) value = max;
    if (g_bParamInt[idx])
    {
        value = float(RoundToNearest(value));
    }
    return true;
}

bool TR_ParamWrite(int idx, float value, bool silent = false)
{
    if (idx < 0 || idx >= g_iParamCount)
    {
        return false;
    }
    if (!TR_FeatureEnabled())
    {
        if (!silent)
        {
            TR_Log("插件已关闭，拒绝写入 %s", g_sParamName[idx]);
        }
        return false;
    }
    if (!TR_ClampValue(idx, value)) return false;

    if (g_iParamKind[idx] == TRK_VSCRIPT)
    {
        if (g_iParamVField[idx] == TRV_INTERVAL || g_iParamVField[idx] == TRV_LIMIT || g_iParamVField[idx] == TRV_DOUBLE)
        {
            char common[32];
            strcopy(common, sizeof(common), g_iParamVField[idx] == TRV_INTERVAL ? "tankrun_auto_interval"
                : (g_iParamVField[idx] == TRV_DOUBLE ? "tankrun_auto_double" : "tankrun_auto_limit"));
            if (g_iParamVField[idx] == TRV_LIMIT && value == 0.0)
            {
                g_cvAutoSpawn.SetBool(false);
                return true;
            }
            return TR_ParamWrite(TR_FindParamByName(common), value, silent);
        }
        if (!TR_VScriptSetField(idx, value)) return false;
        TR_RememberParam(idx, g_fParamLastWrite[idx]);
        return true;
    }

    ConVar cv = g_ParamCVar[idx];
    if (cv == null)
    {
        TR_Log("参数 %s 在本构建未注册，无法写入", g_sParamName[idx]);
        return false;
    }

    float current = 0.0;
    bool haveCurrent = TR_ParamRead(idx, current);

    // 冲突处理：检测到外部写入时不静默覆盖记录。
    if (haveCurrent && g_bParamHasLastWrite[idx]
        && FloatAbs(current - g_fParamLastWrite[idx]) > 0.0001)
    {
        g_iStatConflicts++;
        if (g_cvConflictMode != null && g_cvConflictMode.IntValue == 1)
        {
            TR_Log("冲突：%s 当前值 %.3f 与本插件上次写入 %.3f 不一致，已按配置拒绝写入",
                g_sParamName[idx], current, g_fParamLastWrite[idx]);
            if (!silent)
            {
                TR_Chat(0, "参数 %s 检测到外部修改，已拒绝写入（冲突模式=拒绝）", g_sParamName[idx]);
            }
            return false;
        }
        TR_Log("冲突：%s 被外部修改（当前 %.3f，本插件上次写入 %.3f），保留原始值并继续写入",
            g_sParamName[idx], current, g_fParamLastWrite[idx]);
    }

    if (haveCurrent)
    {
        TR_RecordOriginal(idx, current);
    }

    if (g_bParamInt[idx])
    {
        cv.SetInt(RoundToNearest(value));
    }
    else
    {
        cv.SetFloat(value);
    }

    float after = 0.0;
    if (!TR_ParamRead(idx, after))
    {
        return false;
    }
    g_fParamLastWrite[idx] = after;
    g_bParamHasLastWrite[idx] = true;
    g_iParamWriteCount[idx]++;
    g_iStatParamsWritten++;

    bool ok = FloatAbs(after - value) <= (g_bParamInt[idx] ? 0.5 : 0.0005);
    if (!ok)
    {
        TR_Log("参数 %s 写入未完全生效：请求 %.3f，实际 %.3f（引擎可能限制范围）",
            g_sParamName[idx], value, after);
    }
    else
    {
        TR_Log("参数 %s 写入 %.3f（原值 %s）", g_sParamName[idx], after,
            g_bParamHasOriginal[idx] ? "已记录" : "未记录");
    }
    // An engine bound may accept a different value. Keep the value that actually took effect.
    if (ok || (haveCurrent && after != current)) TR_RememberParam(idx, after);
    return ok;
}

bool TR_ParamRestore(int idx, bool silent = false)
{
    if (idx < 0 || idx >= g_iParamCount)
    {
        return false;
    }
    if (g_iParamKind[idx] == TRK_VSCRIPT)
    {
        // 脚本字段没有“注册默认值”，只做再次读取提示。
        if (!silent)
        {
            TR_Chat(0, "脚本字段 %s 无引擎注册默认值，不能恢复默认", g_sParamName[idx]);
        }
        return false;
    }
    ConVar cv = g_ParamCVar[idx];
    if (cv == null)
    {
        return false;
    }
    float target;
    if (g_bParamHasOriginal[idx])
    {
        target = g_fParamOriginal[idx];
    }
    else
    {
        target = TR_ParamDefault(idx);
    }
    bool ok = TR_ParamWrite(idx, target, silent);
    return ok;
}

void TR_RestoreAll(bool silent = false)
{
    int restored = 0;
    for (int i = 0; i < g_iParamCount; i++)
    {
        if (g_iParamKind[i] == TRK_VSCRIPT || !g_bParamHasOriginal[i])
        {
            continue;
        }
        if (TR_ParamRestore(i, true))
        {
            restored++;
        }
    }
    TR_Log("恢复全部参数：%d 项", restored);
    if (!silent)
    {
        TR_Chat(0, "已恢复 %d 项引擎参数到本插件记录的初始值", restored);
    }
}

// ---------------------------------------------------------------- 简易键值文件
bool TR_ReadKeyValueFile(const char[] path, StringMap out)
{
    if (!FileExists(path))
    {
        return false;
    }
    File file = OpenFile(path, "r");
    if (file == null)
    {
        return false;
    }
    char line[256];
    while (!file.EndOfFile() && file.ReadLine(line, sizeof(line)))
    {
        TrimString(line);
        if (line[0] == '\0' || (line[0] == '#' && StrContains(line, "#name=") != 0) || line[0] == ';')
        {
            continue;
        }
        char key[96];
        int split = SplitString(line, "=", key, sizeof(key));
        if (split <= 0)
        {
            continue;
        }
        char value[128];
        strcopy(value, sizeof(value), line[split]);
        TrimString(key);
        TrimString(value);
        if (key[0] != '\0')
        {
            out.SetString(key, value);
        }
    }
    delete file;
    return true;
}

bool TR_WriteKeyValueFile(const char[] path, StringMap map)
{
    File file = OpenFile(path, "w");
    if (file == null)
    {
        TR_Log("无法写入 %s", path);
        return false;
    }
    file.WriteLine("# Tank Run 综合控制中心 - 自动生成，请勿手工编辑");
    StringMapSnapshot snap = map.Snapshot();
    char key[96];
    char value[192];
    for (int i = 0; i < snap.Length; i++)
    {
        snap.GetKey(i, key, sizeof(key));
        if (map.GetString(key, value, sizeof(value)))
        {
            file.WriteLine("%s=%s", key, value);
        }
    }
    delete snap;
    delete file;
    return true;
}

// Hidden ConVars live in the SourceMod process and survive a plugin unload/reload.
// The default empty value means there is no override for this server session.
ConVar TR_SessionSlot(const char[] name)
{
    char key[96];
    Format(key, sizeof(key), "_tr_session_%s", name);
    return CreateConVar(key, "", "Tank Run session value", FCVAR_HIDDEN | FCVAR_DONTRECORD);
}

void TR_InitSession()
{
    g_ExtraConfig[0] = g_cvEnabled;
    g_ExtraConfig[1] = g_cvPublicReadonly;
    g_ExtraConfig[2] = g_cvDebug;
    g_ExtraConfig[3] = g_cvLogEvents;
    g_ExtraConfig[4] = g_cvConflictMode;
    char name[64];
    for (int i = 0; i < TR_SESSION_EXTRA; i++)
    {
        g_ExtraConfig[i].GetName(name, sizeof(name));
        g_ExtraSession[i] = TR_SessionSlot(name);
        g_ExtraConfig[i].AddChangeHook(TR_OnPluginConfigChanged);
    }
    for (int i = 0; i < g_iParamCount; i++)
    {
        g_ParamSession[i] = TR_SessionSlot(g_sParamName[i]);
        if (g_iParamKind[i] == TRK_PLUGIN && g_ParamCVar[i] != null)
        {
            g_ParamCVar[i].AddChangeHook(TR_OnPluginConfigChanged);
        }
    }
}

void TR_RememberParam(int idx, float value)
{
    if (g_bRestoringSession) return;
    // Keep the exact float bits, including intervals too small for fixed decimal formatting.
    char bits[24];
    IntToString(view_as<int>(value), bits, sizeof(bits));
    g_ParamSession[idx].SetString(bits);
}

bool TR_ReadSessionParam(int idx, float &value)
{
    char bits[24];
    g_ParamSession[idx].GetString(bits, sizeof(bits));
    if (bits[0] == '\0') return false;
    value = view_as<float>(StringToInt(bits));
    return TR_ClampValue(idx, value);
}

void TR_ApplySessionConVars()
{
    g_bRestoringSession = true;
    char saved[24];
    for (int i = 0; i < TR_SESSION_EXTRA; i++)
    {
        g_ExtraSession[i].GetString(saved, sizeof(saved));
        if (saved[0] != '\0') g_ExtraConfig[i].SetString(saved);
    }
    for (int i = 0; i < g_iParamCount; i++)
    {
        ConVar cv = g_ParamCVar[i];
        float value;
        if (cv == null || !TR_ReadSessionParam(i, value)) continue;
        if (g_iParamKind[i] == TRK_ENGINE && !TR_FeatureEnabled()) continue;
        if (g_bParamInt[i]) cv.SetInt(RoundToNearest(value));
        else cv.SetFloat(value);
        TR_ParamRead(i, g_fParamLastWrite[i]);
        g_bParamHasLastWrite[i] = true;
    }
    g_bRestoringSession = false;
}

void TR_ApplySessionScript()
{
    if (!g_bVValid || !TR_FeatureEnabled()) return;
    for (int i = 0; i < g_iParamCount; i++)
    {
        float value, current;
        if (g_iParamKind[i] == TRK_VSCRIPT && g_iParamVField[i] == TRV_TANKHEALTH
            && TR_ReadSessionParam(i, value)
            && TR_ParamRead(i, current) && current != value)
        {
            TR_VScriptSetField(i, value);
        }
    }
}

// ---------------------------------------------------------------- 手动配置文件（与进程内自动保留分开）
void TR_SaveSettings()
{
    StringMap map = new StringMap();
    for (int i = 0; i < g_iParamCount; i++)
    {
        if (g_iParamKind[i] != TRK_PLUGIN || g_ParamCVar[i] == null)
        {
            continue;
        }
        char buf[64];
        if (g_bParamInt[i])
        {
            Format(buf, sizeof(buf), "%d", g_ParamCVar[i].IntValue);
        }
        else
        {
            TR_FormatNumber(g_ParamCVar[i].FloatValue, buf, sizeof(buf));
        }
        map.SetString(g_sParamName[i], buf);
    }
    TR_WriteKeyValueFile(g_sSettingsPath, map);
    delete map;
}

void TR_LoadSettings()
{
    StringMap map = new StringMap();
    if (!TR_ReadKeyValueFile(g_sSettingsPath, map))
    {
        delete map;
        return;
    }
    int applied = 0;
    for (int i = 0; i < g_iParamCount; i++)
    {
        if (g_iParamKind[i] != TRK_PLUGIN || g_ParamCVar[i] == null)
        {
            continue;
        }
        char buf[64];
        if (!map.GetString(g_sParamName[i], buf, sizeof(buf)))
        {
            continue;
        }
        float value = StringToFloat(buf);
        if (!TR_ClampValue(i, value)) continue;
        if (g_bParamInt[i])
        {
            g_ParamCVar[i].SetInt(RoundToNearest(value));
        }
        else
        {
            g_ParamCVar[i].SetFloat(value);
        }
        TR_ParamRead(i, value);
        TR_RememberParam(i, value);
        applied++;
    }
    delete map;
    TR_Log("已加载插件设置 %d 项", applied);
}

// ---------------------------------------------------------------- 预设（8 个槽位）
void TR_GetPresetPath(int slot, char[] path, int maxlen)
{
    BuildPath(Path_SM, path, maxlen, "%s/tankrun_control_preset%d.cfg", g_sPresetDir, slot + 1);
}

bool TR_GetPresetName(int slot, char[] name, int maxlen)
{
    char path[PLATFORM_MAX_PATH];
    TR_GetPresetPath(slot, path, sizeof(path));
    StringMap map = new StringMap();
    bool ok = TR_ReadKeyValueFile(path, map) && map.GetString("#name", name, maxlen);
    if (!ok)
    {
        strcopy(name, maxlen, "");
    }
    delete map;
    return ok && name[0] != '\0';
}

void TR_FormatPresetSlot(int slot, char[] buffer, int maxlen)
{
    char name[64];
    if (TR_GetPresetName(slot, name, sizeof(name)))
    {
        Format(buffer, maxlen, "预设 %d：%s", slot + 1, name);
    }
    else
    {
        Format(buffer, maxlen, "预设 %d：（空）", slot + 1);
    }
}

bool TR_SavePreset(int slot, const char[] name)
{
    char path[PLATFORM_MAX_PATH];
    TR_GetPresetPath(slot, path, sizeof(path));
    StringMap map = new StringMap();
    map.SetString("#name", name);
    int stored = 0;
    for (int i = 0; i < g_iParamCount; i++)
    {
        if (g_iParamKind[i] == TRK_VSCRIPT)
        {
            continue;
        }
        float value;
        if (!TR_ParamRead(i, value))
        {
            continue;
        }
        char buf[64];
        if (g_bParamInt[i])
        {
            Format(buf, sizeof(buf), "%d", RoundToNearest(value));
        }
        else
        {
            TR_FormatNumber(value, buf, sizeof(buf));
        }
        map.SetString(g_sParamName[i], buf);
        stored++;
    }
    bool ok = TR_WriteKeyValueFile(path, map);
    delete map;
    TR_Log("保存预设 %d（%s）：%d 项，结果 %s", slot + 1, name, stored, ok ? "成功" : "失败");
    return ok;
}

int TR_ApplyPreset(int slot, int client)
{
    char path[PLATFORM_MAX_PATH];
    TR_GetPresetPath(slot, path, sizeof(path));
    StringMap map = new StringMap();
    if (!TR_ReadKeyValueFile(path, map))
    {
        delete map;
        return -1;
    }
    int applied = 0;
    int skipped = 0;
    for (int i = 0; i < g_iParamCount; i++)
    {
        if (g_iParamKind[i] == TRK_VSCRIPT)
        {
            continue;
        }
        char buf[64];
        if (!map.GetString(g_sParamName[i], buf, sizeof(buf)))
        {
            continue;
        }
        float value = StringToFloat(buf);
        float validated = value;
        if (!TR_ClampValue(i, validated) || validated != value)
        {
            TR_Log("预设 %d：跳过越界参数 %s=%s", slot + 1, g_sParamName[i], buf);
            skipped++;
            continue;
        }
        bool ok = TR_ParamWrite(i, value, true);
        if (ok)
        {
            applied++;
        }
        else
        {
            skipped++;
        }
    }
    delete map;
    TR_Log("应用预设 %d：成功 %d 项，跳过 %d 项", slot + 1, applied, skipped);
    if (client > 0)
    {
        TR_Chat(client, "应用预设 %d：成功 %d 项，跳过 %d 项", slot + 1, applied, skipped);
    }
    return applied;
}

bool TR_DeletePreset(int slot)
{
    char path[PLATFORM_MAX_PATH];
    TR_GetPresetPath(slot, path, sizeof(path));
    if (!FileExists(path))
    {
        return false;
    }
    bool ok = DeleteFile(path);
    TR_Log("删除预设 %d：%s", slot + 1, ok ? "成功" : "失败");
    return ok;
}

// ---------------------------------------------------------------- VScript 桥
void TR_BuildVProbe(char[] out, int maxlen)
{
    // Preserve fractional seconds instead of truncating to hundredths of a second.
    Format(out, maxlen,
        "local s = ::SessionState; local o = ::SessionOptions; if ((\"SpawnInterval\" in s) && (\"cm_TankLimit\" in o)) { Convars.SetValue(\"%s\", format(\"V|%%.9g|%%d|%%d|%%d|%%d\", s.SpawnInterval.tofloat(), o.cm_TankLimit.tointeger(), ((\"DoubleTanks\" in s) ? (s.DoubleTanks ? 1 : 0) : 0), ((\"SpawnTankThink\" in s) ? (s.SpawnTankThink ? 1 : 0) : -1), ((\"TankHealth\" in s) ? s.TankHealth.tointeger() : 0))); } else { Convars.SetValue(\"%s\", \"NOSCRIPT\"); }",
        TR_BRIDGE, TR_BRIDGE);
}

bool TR_ParseVSnapshot(const char[] result)
{
    if (StrEqual(result, "NOSCRIPT", false))
    {
        g_bVValid = false;
        g_bVProbed = true;
        strcopy(g_sVStatus, sizeof(g_sVStatus), "模式脚本未提供 SessionState/SessionOptions");
        return false;
    }
    if (StrContains(result, "ERR", false) == 0 || result[0] == '\0')
    {
        g_bVValid = false;
        g_bVProbed = true;
        strcopy(g_sVStatus, sizeof(g_sVStatus), "脚本执行失败（详见日志）");
        TR_Log("VScript 探测失败：%s", result);
        return false;
    }
    char parts[6][32];
    int count = ExplodeString(result, "|", parts, sizeof(parts), sizeof(parts[]));
    if (count < 6 || !StrEqual(parts[0], "V", false))
    {
        g_bVValid = false;
        strcopy(g_sVStatus, sizeof(g_sVStatus), "脚本返回值格式不匹配");
        return false;
    }
    g_fVSpawnInterval = StringToFloat(parts[1]);
    g_iVTankLimit = StringToInt(parts[2]);
    g_bVDoubleTanks = StringToInt(parts[3]) != 0;
    g_bVThink = StringToInt(parts[4]) != 0;
    g_iVTankHealth = StringToInt(parts[5]);
    g_bVValid = true;
    g_bVProbed = true;
    strcopy(g_sVStatus, sizeof(g_sVStatus), "已连接 Tank Run 模式脚本");
    return true;
}

// 运行一段 VScript（由调用方给出语句），返回桥变量内容。
// 防御：没有运行中的地图时直接拒绝，绝不调用 CreateEntityByName。
bool TR_RunVScript(const char[] body, char[] out, int outlen)
{
    out[0] = '\0';
    if (!TR_IsMapRunning())
    {
        TR_Log("VScript：当前没有运行中的地图，已拒绝创建 logic_script");
        return false;
    }
    int entity = CreateEntityByName("logic_script");
    if (entity <= 0)
    {
        TR_Log("VScript：无法创建 logic_script 实体");
        return false;
    }
    if (!DispatchSpawn(entity))
    {
        RemoveEntity(entity);
        TR_Log("VScript：logic_script DispatchSpawn 失败");
        return false;
    }
    char code[2048];
    Format(code, sizeof(code),
        "try { %s } catch(e) { Convars.SetValue(\"%s\", \"ERR\"); }", body, TR_BRIDGE);
    g_cvBridge.SetString("");
    SetVariantString(code);
    AcceptEntityInput(entity, "RunScriptCode");
    RemoveEntity(entity);
    g_cvBridge.GetString(out, outlen);
    return out[0] != '\0';
}

bool TR_VScriptProbe(bool force = false)
{
    if (!TR_IsMapRunning())
    {
        g_bVValid = false;
        g_bVProbed = false;
        strcopy(g_sVStatus, sizeof(g_sVStatus), "地图/实体系统未就绪，脚本探测已推迟");
        return false;
    }
    if (!TR_IsTankRunMode())
    {
        g_bVValid = false;
        g_bVProbed = true;
        strcopy(g_sVStatus, sizeof(g_sVStatus), "当前模式不是 tankrun");
        return false;
    }
    if (g_bVProbed && !force && !g_bVValid)
    {
        // 允许偶尔重试，但避免每次菜单刷新都新建实体。
        return false;
    }
    char probe[1024];
    TR_BuildVProbe(probe, sizeof(probe));
    char result[256];
    if (!TR_RunVScript(probe, result, sizeof(result)))
    {
        g_bVValid = false;
        g_bVProbed = true;
        strcopy(g_sVStatus, sizeof(g_sVStatus), "脚本桥无返回值（logic_script 不可用？）");
        return false;
    }
    return TR_ParseVSnapshot(result);
}

bool TR_VScriptSetField(int idx, float value)
{
    if (!g_bVValid && !TR_VScriptProbe(true))
    {
        TR_Log("脚本字段 %s 写入失败：脚本接口不可用", g_sParamName[idx]);
        return false;
    }
    char setStmt[256];
    switch (g_iParamVField[idx])
    {
        case TRV_INTERVAL:
        {
            char number[48];
            TR_FormatNumber(value, number, sizeof(number));
            Format(setStmt, sizeof(setStmt), "::SessionState.SpawnInterval = %s;", number);
        }
        case TRV_LIMIT:
        {
            Format(setStmt, sizeof(setStmt), "::SessionOptions.cm_TankLimit = %d;", RoundToNearest(value));
        }
        case TRV_DOUBLE:
        {
            Format(setStmt, sizeof(setStmt), "::SessionState.DoubleTanks = %s;", value != 0.0 ? "true" : "false");
        }
        case TRV_TANKHEALTH:
        {
            Format(setStmt, sizeof(setStmt), "::SessionState.TankHealth = %d;", RoundToNearest(value));
        }
        default:
        {
            return false;
        }
    }

    char probe[1024];
    TR_BuildVProbe(probe, sizeof(probe));
    char code[1400];
    Format(code, sizeof(code), "%s %s", setStmt, probe);
    char result[256];
    if (!TR_RunVScript(code, result, sizeof(result)))
    {
        TR_Log("脚本字段 %s 写入无返回：%s", g_sParamName[idx], result);
        return false;
    }
    if (!TR_ParseVSnapshot(result))
    {
        return false;
    }

    float actual;
    if (!TR_ParamRead(idx, actual))
    {
        return false;
    }
    bool ok = g_bParamInt[idx] ? actual == value : actual > 0.0 && FloatAbs(actual - value) <= value * 0.000001;
    g_fParamLastWrite[idx] = actual;
    g_bParamHasLastWrite[idx] = true;
    if (ok)
    {
        g_iStatParamsWritten++;
        TR_Log("脚本字段 %s 写入 %.3f 成功", g_sParamName[idx], actual);
    }
    else
    {
        TR_Log("脚本字段 %s 写入未生效：请求 %.3f，实际 %.3f（模式脚本可能覆盖）",
            g_sParamName[idx], value, actual);
    }
    return ok;
}

bool TR_VScriptReadFrontPhase()
{
    char code[840], result[32];
    Format(code, sizeof(code),
        "local s = ::SessionState; local o = ::SessionOptions; if (s.TanksDisabled || o.cm_TankLimit <= 0) { Convars.SetValue(\"%s\", \"PAUSE\"); } else { local old = o.cm_TankLimit; local wasOn = s.SpawnTankThink; o.cm_TankLimit = %d; s.SpawnTankThink = false; Convars.SetValue(\"%s\", format(\"OPEN|%%d|%%d\", old.tointeger(), wasOn ? 1 : 0)); }",
        TR_BRIDGE, g_cvAutoLimit.IntValue, TR_BRIDGE);
    if (!TR_RunVScript(code, result, sizeof(result))) return false;
    if (StrEqual(result, "PAUSE"))
    {
        g_iVTankLimit = 0;
        return false;
    }
    char parts[3][16];
    if (ExplodeString(result, "|", parts, sizeof(parts), sizeof(parts[])) != 3 || !StrEqual(parts[0], "OPEN")) return false;
    int before = StringToInt(parts[1]);
    if (before != g_iLastManagedStockLimit) g_iOriginalStockLimit = before;
    g_iLastManagedStockLimit = g_cvAutoLimit.IntValue;
    g_iVTankLimit = g_cvAutoLimit.IntValue;
    g_bVThink = false;
    if (StringToInt(parts[2]) != 0) g_bScriptThinkDisabled = true;
    return true;
}

void TR_VScriptReleaseFrontPhase(bool restoreThink)
{
    char code[768], result[16];
    Format(code, sizeof(code),
        "local s = ::SessionState; local o = ::SessionOptions; if (%d > 0 && o.cm_TankLimit == %d) { o.cm_TankLimit = %d; } if (%s && !s.TanksDisabled && o.cm_TankLimit > 0) { s.SpawnTankThink = true; } Convars.SetValue(\"%s\", \"RESTORED\");",
        g_iOriginalStockLimit, g_iLastManagedStockLimit, g_iOriginalStockLimit, restoreThink ? "true" : "false", TR_BRIDGE);
    TR_RunVScript(code, result, sizeof(result));
}

// ---------------------------------------------------------------- 初始化
void TR_CreateConVars()
{
    g_cvEnabled         = CreateConVar("tankrun_enabled", "1", "Tank Run 综合控制中心：功能总开关", FCVAR_NOTIFY);
    g_cvPublicReadonly  = CreateConVar("tankrun_public_readonly", "0", "允许普通玩家打开只读菜单（不能修改）", FCVAR_NOTIFY);
    g_cvDebug           = CreateConVar("tankrun_debug", "0", "控制台调试输出", FCVAR_NOTIFY);
    g_cvLogEvents       = CreateConVar("tankrun_log_events", "1", "记录 Tank/投石/拳击事件到日志", FCVAR_NOTIFY);
    g_cvAiOnly          = CreateConVar("tankrun_ai_only", "1", "只对 AI Tank 应用策略（玩家控制的 Tank 不介入）", FCVAR_NOTIFY);
    g_cvConflictMode    = CreateConVar("tankrun_conflict_mode", "0", "参数冲突处理：0=警告后写入 1=拒绝写入", FCVAR_NOTIFY);
    g_cvDefaultExpert   = CreateConVar("tankrun_default_expert", "1", "Tank Run 进图时默认专家难度", FCVAR_NOTIFY, true, 0.0, true, 1.0);

    g_cvAutoSpawn       = CreateConVar("tankrun_auto_spawn", "1", "Tank Run 统一自动生成开关", FCVAR_NOTIFY);
    g_cvAutoInterval    = CreateConVar("tankrun_auto_interval", "2.0", "插件自动生成间隔（秒）", FCVAR_NOTIFY);
    g_cvAutoLimit       = CreateConVar("tankrun_auto_limit", "30", "Tank Run 脚本与插件共用的在场 Tank 上限", FCVAR_NOTIFY, true, 1.0, true, 31.0);
    g_cvRotateOldest    = CreateConVar("tankrun_rotate_oldest", "1", "自动生成满员时处死最早出生的 AI Tank 再补位", FCVAR_NOTIFY, true, 0.0, true, 1.0);
    g_cvAutoDouble      = CreateConVar("tankrun_auto_double", "0", "插件自动生成时每次尝试双 Tank", FCVAR_NOTIFY);
    g_cvSpawnMode       = CreateConVar("tankrun_spawn_mode", "0", "生成方式：0=原生随机区域 1=距离带 2=准星位置 3=Tank Run 脚本 4=幸存者附近原生选位", FCVAR_NOTIFY);
    g_cvSpawnVisibility = CreateConVar("tankrun_spawn_visibility", "0", "Tank Run 刷点规则：0=跟随游戏难度 1=普通遮挡 2=高难度前线", FCVAR_NOTIFY, true, 0.0, true, 2.0);
    g_cvSpawnMin        = CreateConVar("tankrun_spawn_min_dist", "220.0", "生成最小距离（单位）", FCVAR_NOTIFY);
    g_cvSpawnMax        = CreateConVar("tankrun_spawn_max_dist", "3000.0", "生成最大距离（单位）", FCVAR_NOTIFY);
    g_cvSpawnAttempts   = CreateConVar("tankrun_spawn_attempts", "6", "生成位置尝试次数", FCVAR_NOTIFY);
    g_cvSpawnFallback   = CreateConVar("tankrun_spawn_fallback", "1", "距离/准星失败时回退原生选位", FCVAR_NOTIFY);

    g_cvTargetMode      = CreateConVar("tankrun_target_mode", "0", "目标策略：0=原生 1=最近 2=最远 3=最低血 4=固定玩家", FCVAR_NOTIFY);
    g_cvTargetFixed     = CreateConVar("tankrun_target_fixed", "0", "固定目标玩家序号（1..MaxClients）", FCVAR_NOTIFY);
    g_cvTargetScan      = CreateConVar("tankrun_target_scan", "0.4", "感知扫描间隔（秒）", FCVAR_NOTIFY);

    g_cvRockEnabled     = CreateConVar("tankrun_rock_enabled", "1", "允许原生投石（关闭=拦截投石激活）", FCVAR_NOTIFY);
    g_cvRockMinDelay    = CreateConVar("tankrun_rock_min_delay", "0.0", "插件侧投石最小间隔（秒，0=不限制）", FCVAR_NOTIFY);
    g_cvRockForceScale  = CreateConVar("tankrun_rock_force_scale", "1.0", "投石释放速度倍率（1=原版）", FCVAR_NOTIFY);

    g_cvFirstRock       = CreateConVar("tankrun_firstrock_enabled", "1", "首次接敌优先原版投石", FCVAR_NOTIFY);
    g_cvFirstRockTimeout = CreateConVar("tankrun_firstrock_timeout", "3.0", "首石窗口超时（秒）", FCVAR_NOTIFY);
    g_cvFirstRockAttempts = CreateConVar("tankrun_firstrock_attempts", "2", "首石能力激活最大尝试次数", FCVAR_NOTIFY);

    g_cvClawScale       = CreateConVar("tankrun_claw_damage_scale", "1.0", "Tank 拳击伤害倍率", FCVAR_NOTIFY);
    g_cvClawBlock       = CreateConVar("tankrun_claw_block_hit", "0", "阻挡 Tank 拳击命中判定（仅影响击退，风险项）", FCVAR_NOTIFY);
    g_cvTankDmgScale    = CreateConVar("tankrun_tank_damage_scale", "1.0", "Tank 受到伤害倍率", FCVAR_NOTIFY);
    g_cvFireScale       = CreateConVar("tankrun_fire_damage_scale", "1.0", "Tank 火焰伤害倍率", FCVAR_NOTIFY);

    g_cvTankHealth      = CreateConVar("tankrun_tank_health", "0", "生成时设置 Tank 当前血量（0=不干预）", FCVAR_NOTIFY);
    g_cvTankMaxHealth   = CreateConVar("tankrun_tank_maxhealth", "0", "生成时设置 Tank 最大生命（0=不干预）", FCVAR_NOTIFY);
    g_cvModeHealth      = CreateConVar("tankrun_mode_health_sync", "1", "把 Tank 血量同步到 Tank Run 脚本 TankHealth", FCVAR_NOTIFY);
    g_cvSpeedScale      = CreateConVar("tankrun_speed_scale", "1.0", "Tank 移动速度倍率（1=不干预）", FCVAR_NOTIFY);

    g_cvBridge          = CreateConVar(TR_BRIDGE, "", "内部 VScript 读回通道；请勿手工修改", FCVAR_DONTRECORD);
}

void TR_RegisterParams()
{
    // ---------------- M02 生成与数量 ----------------
    int p;
    p = TR_AddParam(SEC_SPAWN, TRK_PLUGIN, "tankrun_auto_spawn", "自动生成 AI Tank",
        0.0, 1.0, 1.0, true, true, TRV_NONE, "Tank Run 自动生成总开关；原版脚本仍提供地图阶段门");
    TR_Quick(p, 0.0, 1.0);
    p = TR_AddParam(SEC_SPAWN, TRK_PLUGIN, "tankrun_auto_interval", "自动生成间隔（秒）",
        0.5, 600.0, 0.5, false, true, TRV_NONE, "使用引擎 tick 间隔换算，不按固定 tick 计");
    TR_Quick(p, 2.0, 5.0, 20.0, 60.0);
    p = TR_AddParam(SEC_SPAWN, TRK_PLUGIN, "tankrun_auto_limit", "在场 Tank 总量上限（只）",
        1.0, 31.0, 1.0, true, true, TRV_NONE, "默认只刷原生 Tank，实际数量随客户端槽位调整");
    TR_Quick(p, 2.0, 8.0, 20.0, 30.0);
    p = TR_AddParam(SEC_SPAWN, TRK_PLUGIN, "tankrun_rotate_oldest", "满员时轮换最早出生的 Tank",
        0.0, 1.0, 1.0, true, true, TRV_NONE, "自动生成到期且满员时处死最早出生的 AI Tank，给新 Tank 腾位置");
    TR_Quick(p, 0.0, 1.0);
    p = TR_AddParam(SEC_SPAWN, TRK_PLUGIN, "tankrun_auto_double", "每次生成双 Tank",
        0.0, 1.0, 1.0, true, true, TRV_NONE, "插件策略：一次调度生成两只");
    TR_Quick(p, 0.0, 1.0);
    p = TR_AddParam(SEC_SPAWN, TRK_PLUGIN, "tankrun_spawn_mode", "生成方式",
        0.0, float(_:TRSP_COUNT) - 1.0, 1.0, true, true, TRV_NONE,
        "0=原生随机区域 1=距离带 2=准星位置 3=Tank Run 脚本 4=幸存者附近原生选位");
    TR_Quick(p, 0.0, 1.0, 2.0, 3.0);

    // ---------------- M09 生命/伤害/速度 ----------------
    p = TR_AddParam(SEC_HEALTH, TRK_PLUGIN, "tankrun_tank_health", "Tank 当前血量（0=不干预）",
        0.0, 20000.0, 100.0, true, true, TRV_NONE, "本插件在 Tank 生成后写入实体血量");
    TR_Quick(p, 0.0, 2000.0, 4000.0, 8000.0);
    p = TR_AddParam(SEC_HEALTH, TRK_PLUGIN, "tankrun_tank_maxhealth", "Tank 最大生命（0=不干预）",
        0.0, 20000.0, 100.0, true, true, TRV_NONE, "写入实体 m_iMaxHealth；引擎/模式可能在生成时重算");
    TR_Quick(p, 0.0, 3000.0, 6000.0, 12000.0);
    p = TR_AddParam(SEC_HEALTH, TRK_PLUGIN, "tankrun_mode_health_sync", "同步血量到 Tank Run 脚本",
        0.0, 1.0, 1.0, true, true, TRV_NONE, "写入 SessionState.TankHealth，由模式脚本在生成时应用");
    TR_Quick(p, 0.0, 1.0);
    p = TR_AddParam(SEC_HEALTH, TRK_PLUGIN, "tankrun_tank_damage_scale", "Tank 受伤倍率",
        0.1, 10.0, 0.1, false, true, TRV_NONE, "SDKHooks OnTakeDamage 插件策略，只对 AI Tank 生效");
    TR_Quick(p, 0.5, 1.0, 2.0, 5.0);
    p = TR_AddParam(SEC_HEALTH, TRK_PLUGIN, "tankrun_fire_damage_scale", "Tank 火焰伤害倍率",
        0.0, 10.0, 0.1, false, true, TRV_NONE, "仅对带燃烧属性的伤害生效");
    TR_Quick(p, 0.0, 0.5, 1.0, 2.0);
    p = TR_AddParam(SEC_HEALTH, TRK_PLUGIN, "tankrun_speed_scale", "移动速度倍率",
        0.5, 2.0, 0.05, false, true, TRV_NONE, "写实体 m_flMaxSpeed；引擎状态可能覆盖，插件会再次校正");
    TR_Quick(p, 0.75, 1.0, 1.25, 1.5);

    // ---------------- M07 拳击 ----------------
    p = TR_AddParam(SEC_CLAW, TRK_PLUGIN, "tankrun_claw_damage_scale", "拳击伤害倍率",
        0.05, 10.0, 0.05, false, true, TRV_NONE, "插件策略：拦截 Tank 攻击者的伤害并缩放");
    TR_Quick(p, 0.5, 1.0, 2.0, 5.0);
    p = TR_AddParam(SEC_CLAW, TRK_PLUGIN, "tankrun_claw_block_hit", "阻挡拳击命中判定",
        0.0, 1.0, 1.0, true, false, TRV_NONE, "L4D_TankClaw_OnPlayerHit_Pre 返回 Handled；仅阻挡击退，风险项");
    TR_Quick(p, 0.0, 1.0);
    p = TR_AddParam(SEC_CLAW, TRK_ENGINE, "tank_swing_range", "挥击范围参数（单位）",
        16.0, 500.0, 2.0, false, true, TRV_NONE, "原生注册参数");
    TR_Quick(p, 40.0, 56.0, 80.0, 120.0);
    p = TR_AddParam(SEC_CLAW, TRK_ENGINE, "tank_attack_range", "攻击范围参数（单位）",
        0.0, 500.0, 2.0, false, false, TRV_NONE, "已审 AI 触发分支可能使用固定 50，可改但效果未验证");
    TR_Quick(p, 50.0, 80.0, 120.0, 200.0);
    p = TR_AddParam(SEC_CLAW, TRK_ENGINE, "tank_fist_radius", "拳击半径（单位）",
        0.0, 200.0, 1.0, false, false, TRV_NONE, "可修改，实际命中几何未验证");
    TR_Quick(p, 10.0, 15.0, 25.0, 40.0);
    p = TR_AddParam(SEC_CLAW, TRK_ENGINE, "tank_swing_interval", "普通挥击间隔（秒）",
        0.1, 10.0, 0.1, false, true, TRV_NONE, "原生注册参数");
    TR_Quick(p, 0.5, 1.0, 1.5, 3.0);
    p = TR_AddParam(SEC_CLAW, TRK_ENGINE, "tank_swing_fast_interval", "快速挥击间隔（秒）",
        0.1, 10.0, 0.1, false, true, TRV_NONE, "原生注册参数");
    TR_Quick(p, 0.3, 0.6, 1.0, 2.0);
    p = TR_AddParam(SEC_CLAW, TRK_ENGINE, "tank_swing_miss_interval", "挥空间隔（秒）",
        0.1, 10.0, 0.1, false, true, TRV_NONE, "原生注册参数");
    TR_Quick(p, 0.5, 1.0, 2.0, 3.0);
    p = TR_AddParam(SEC_CLAW, TRK_ENGINE, "tank_windup_time", "前摇（秒）",
        0.0, 5.0, 0.05, false, false, TRV_NONE, "可修改，需实测动画与生效帧，效果未验证");
    TR_Quick(p, 0.2, 0.5, 0.8, 1.2);
    p = TR_AddParam(SEC_CLAW, TRK_ENGINE, "tank_swing_duration", "动作持续时间（秒）",
        0.0, 5.0, 0.05, false, false, TRV_NONE, "可修改，需实测动画与生效帧，效果未验证");
    TR_Quick(p, 0.2, 0.4, 0.6, 1.0);
    p = TR_AddParam(SEC_CLAW, TRK_ENGINE, "z_tank_attack_interval", "攻击间隔（秒）",
        0.1, 10.0, 0.1, false, false, TRV_NONE, "局部计时路径，与其他拳击计时关系未闭合");
    TR_Quick(p, 0.5, 1.5, 2.5, 4.0);

    // ---------------- M05/M06 投石 ----------------
    p = TR_AddParam(SEC_ROCK, TRK_PLUGIN, "tankrun_rock_enabled", "允许原生投石",
        0.0, 1.0, 1.0, true, true, TRV_NONE, "关闭后拦截 CThrow 激活，Tank 无法投石（真实拦截）");
    TR_Quick(p, 0.0, 1.0);
    p = TR_AddParam(SEC_ROCK, TRK_PLUGIN, "tankrun_rock_min_delay", "插件侧投石最小间隔（秒）",
        0.0, 60.0, 0.5, false, true, TRV_NONE, "按 Tank 记录真实释放时间，窗口内拦截新的投石激活");
    TR_Quick(p, 0.0, 2.0, 5.0, 10.0);
    p = TR_AddParam(SEC_ROCK, TRK_PLUGIN, "tankrun_rock_force_scale", "投石力度倍率",
        0.2, 3.0, 0.05, false, true, TRV_NONE, "在 CTankRock::OnRelease 修改真实释放速度");
    TR_Quick(p, 0.5, 1.0, 1.5, 2.0);
    p = TR_AddParam(SEC_ROCK, TRK_ENGINE, "z_tank_throw_interval", "投石执行冷却（秒）",
        0.0, 999999.0, 1.0, false, true, TRV_NONE, "原生执行冷却；Tank Run 等模式可能覆盖为极大值，请看实际值");
    TR_Quick(p, 5.0, 15.0, 30.0, 60.0);
    p = TR_AddParam(SEC_ROCK, TRK_ENGINE, "tank_throw_min_interval", "AI 投石请求间隔（秒）",
        0.0, 120.0, 0.5, false, true, TRV_NONE, "与 z_tank_throw_interval 不是同一个控制点");
    TR_Quick(p, 0.0, 2.0, 8.0, 20.0);
    p = TR_AddParam(SEC_ROCK, TRK_ENGINE, "z_tank_throw_fail_interval", "投石失败间隔（秒）",
        0.0, 120.0, 0.5, false, true, TRV_NONE, "原生注册参数");
    TR_Quick(p, 0.0, 1.0, 5.0, 10.0);
    p = TR_AddParam(SEC_ROCK, TRK_ENGINE, "tank_throw_allow_range", "投石允许距离（单位）",
        0.0, 5000.0, 10.0, false, true, TRV_NONE, "原生资格距离门");
    TR_Quick(p, 250.0, 500.0, 1000.0, 1500.0);
    p = TR_AddParam(SEC_ROCK, TRK_ENGINE, "z_tank_throw_force", "投石力（引擎参数）",
        0.0, 3000.0, 10.0, false, true, TRV_NONE, "原生注册参数；与释放速度倍率共同影响轨迹");
    TR_Quick(p, 400.0, 800.0, 1200.0, 1600.0);
    p = TR_AddParam(SEC_ROCK, TRK_ENGINE, "tank_throw_lead_time_factor", "预测提前量系数",
        0.0, 5.0, 0.05, false, true, TRV_NONE, "原生瞄准预测参数");
    TR_Quick(p, 0.0, 0.5, 1.0, 2.0);
    p = TR_AddParam(SEC_ROCK, TRK_ENGINE, "tank_throw_loft_rate", "抛射抬升率",
        0.0, 1.0, 0.01, false, true, TRV_NONE, "参与准抬角计算，不是完整飞行物理");
    TR_Quick(p, 0.005, 0.01, 0.05, 0.1);
    p = TR_AddParam(SEC_ROCK, TRK_ENGINE, "tank_throw_max_loft_angle", "最大抬升角（度）",
        0.0, 89.0, 1.0, false, true, TRV_NONE, "原生瞄准参数");
    TR_Quick(p, 15.0, 30.0, 45.0, 60.0);
    p = TR_AddParam(SEC_ROCK, TRK_ENGINE, "tank_throw_aim_error", "瞄准误差",
        0.0, 500.0, 5.0, false, false, TRV_NONE, "只影响特定数值模式分支，效果未验证");
    TR_Quick(p, 0.0, 50.0, 100.0, 200.0);
    p = TR_AddParam(SEC_ROCK, TRK_ENGINE, "tank_rock_overhead_percent", "过肩投掷百分比",
        0.0, 100.0, 5.0, false, false, TRV_NONE, "研究记录存在但本构建注册未确认；缺失时显示不可用");
    TR_Quick(p, 0.0, 50.0, 100.0);

    // ---------------- M04/M08 目标与策略 ----------------
    p = TR_AddParam(SEC_TARGET, TRK_PLUGIN, "tankrun_target_mode", "目标选择策略",
        0.0, float(_:TRT_COUNT) - 1.0, 1.0, true, false, TRV_NONE,
        "插件策略，通过 L4D2_OnChooseVictim 覆盖；原生其他目标路径不受影响");
    TR_Quick(p, 0.0, 1.0, 3.0, 4.0);
    p = TR_AddParam(SEC_TARGET, TRK_PLUGIN, "tankrun_target_fixed", "固定目标玩家序号",
        0.0, float(MaxClients), 1.0, true, false, TRV_NONE, "1..MaxClients；仅策略=固定玩家时使用");
    TR_Quick(p, 0.0);
    p = TR_AddParam(SEC_TARGET, TRK_PLUGIN, "tankrun_target_scan", "感知扫描间隔（秒）",
        0.1, 5.0, 0.1, false, true, TRV_NONE, "插件观察/首次接敌检测周期，默认 0.4 秒");
    TR_Quick(p, 0.2, 0.4, 1.0, 2.0);
    p = TR_AddParam(SEC_TARGET, TRK_PLUGIN, "tankrun_firstrock_enabled", "首次接敌优先原版投石",
        0.0, 1.0, 1.0, true, false, TRV_NONE,
        "攀跳时保留移动，首次接敌后禁拳到真实石头离手或超时；引擎负责原版起手与瞄准");
    TR_Quick(p, 0.0, 1.0);
    p = TR_AddParam(SEC_TARGET, TRK_PLUGIN, "tankrun_firstrock_timeout", "首石窗口超时（秒）",
        1.0, 10.0, 0.5, false, true, TRV_NONE, "攀跳离地时暂停超时，稳定落地后保留完整尝试时间");
    TR_Quick(p, 2.0, 3.0, 5.0, 8.0);
    p = TR_AddParam(SEC_TARGET, TRK_PLUGIN, "tankrun_firstrock_attempts", "首石激活尝试上限",
        1.0, 5.0, 1.0, true, true, TRV_NONE, "原版尝试用尽且没有石头待离手时，解除首石禁拳");
    TR_Quick(p, 1.0, 2.0, 3.0);
    p = TR_AddParam(SEC_TARGET, TRK_PLUGIN, "tankrun_ai_only", "只控制 AI Tank",
        0.0, 1.0, 1.0, true, true, TRV_NONE, "开启时列表与策略只面向 AI Tank；关闭后玩家控制的 Tank 只观察不修改");
    TR_Quick(p, 0.0, 1.0);

    // ---------------- M03 生成位置 ----------------
    p = TR_AddParam(SEC_POS, TRK_PLUGIN, "tankrun_spawn_visibility", "Tank Run 刷点难度",
        0.0, 2.0, 1.0, true, true, TRV_NONE, "0 跟随游戏难度，1 只刷在遮挡后，2 沿用前线选位");
    TR_Quick(p, 0.0, 1.0, 2.0);
    p = TR_AddParam(SEC_POS, TRK_PLUGIN, "tankrun_spawn_min_dist", "最小生成距离（单位）",
        0.0, 5000.0, 50.0, false, true, TRV_NONE, "Tank Run 刷点至少离所有存活生还者这么远");
    TR_Quick(p, 200.0, 300.0, 600.0, 1000.0);
    p = TR_AddParam(SEC_POS, TRK_PLUGIN, "tankrun_spawn_max_dist", "最大生成距离（单位）",
        100.0, 10000.0, 50.0, false, true, TRV_NONE, "Tank Run 距离领先玩家的最远距离，范围内分近中远三段随机选位");
    TR_Quick(p, 1000.0, 2000.0, 3000.0, 5000.0);
    p = TR_AddParam(SEC_POS, TRK_PLUGIN, "tankrun_spawn_attempts", "位置尝试次数",
        1.0, 20.0, 1.0, true, true, TRV_NONE, "距离带/附近选位的重试次数");
    TR_Quick(p, 3.0, 6.0, 10.0, 15.0);
    p = TR_AddParam(SEC_POS, TRK_PLUGIN, "tankrun_spawn_fallback", "失败回退原生选位",
        0.0, 1.0, 1.0, true, true, TRV_NONE, "距离带失败后回退原生随机区域");
    TR_Quick(p, 0.0, 1.0);

    // ---------------- M01/M11 规则 ----------------
    p = TR_AddParam(SEC_RULES, TRK_ENGINE, "z_spawn_range", "Z 生成范围（单位）",
        200.0, 10000.0, 100.0, false, true, TRV_NONE, "原生注册参数；不是 VScript 自动生成的最大距离");
    TR_Quick(p, 500.0, 1500.0, 3000.0, 6000.0);
    p = TR_AddParam(SEC_RULES, TRK_ENGINE, "z_spawn_safety_range", "生成安全距离（单位）",
        0.0, 5000.0, 50.0, false, true, TRV_NONE, "只影响局部门，不是统一最小距离");
    TR_Quick(p, 300.0, 550.0, 800.0, 1200.0);
    p = TR_AddParam(SEC_RULES, TRK_ENGINE, "z_finale_spawn_tank_safety_range", "终局生成安全距离（单位）",
        0.0, 5000.0, 50.0, false, true, TRV_NONE, "本构建可能未注册，缺失时显示不可用");
    TR_Quick(p, 400.0, 600.0, 900.0, 1200.0);
    p = TR_AddParam(SEC_RULES, TRK_ENGINE, "nb_allow_avoiding", "允许导航避让",
        0.0, 1.0, 1.0, true, true, TRV_NONE, "原生导航参数");
    TR_Quick(p, 0.0, 1.0);
    p = TR_AddParam(SEC_RULES, TRK_ENGINE, "nb_allow_climbing", "允许导航攀爬",
        0.0, 1.0, 1.0, true, true, TRV_NONE, "原生导航参数");
    TR_Quick(p, 0.0, 1.0);

    p = TR_AddParam(SEC_RULES, TRK_VSCRIPT, "tankrun_script_spawn_interval", "脚本生成间隔（秒）",
        0.5, 600.0, 0.5, false, true, TRV_INTERVAL,
        "统一自动生成间隔的兼容入口");
    TR_Quick(p, 2.0, 5.0, 20.0, 60.0);
    p = TR_AddParam(SEC_RULES, TRK_VSCRIPT, "tankrun_script_tank_limit", "脚本数量门（只）",
        0.0, 31.0, 1.0, true, true, TRV_LIMIT,
        "0=暂停阶段生成；正值同步到在场总量上限");
    TR_Quick(p, 0.0, 2.0, 8.0, 30.0);
    p = TR_AddParam(SEC_RULES, TRK_VSCRIPT, "tankrun_script_double_tanks", "脚本双 Tank 状态",
        0.0, 1.0, 1.0, true, true, TRV_DOUBLE,
        "统一双 Tank 开关的兼容入口");
    TR_Quick(p, 0.0, 1.0);
    p = TR_AddParam(SEC_RULES, TRK_VSCRIPT, "tankrun_script_tank_health", "脚本 Tank 血量值",
        500.0, 20000.0, 100.0, true, true, TRV_TANKHEALTH,
        "Tank Run 脚本 SessionState.TankHealth；模式在 tank_spawn 时应用");
    TR_Quick(p, 2000.0, 3000.0, 4000.0, 5000.0);
}
