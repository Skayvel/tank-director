// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tank Director contributors

#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>

#define PLUGIN_VERSION "0.1.0"

public Plugin myinfo =
{
    name = "Tank Run Control",
    author = "Tank Run Control Project",
    description = "M01: L4D2 environment identification and read-only admin diagnostics",
    version = PLUGIN_VERSION,
    url = ""
};

public void OnPluginStart()
{
    if (GetEngineVersion() != Engine_Left4Dead2)
    {
        SetFailState("Tank Run Control requires Left 4 Dead 2.");
        return;
    }

    RegAdminCmd("sm_tankrun_status", Command_TankRunStatus, ADMFLAG_GENERIC,
        "Show read-only Tank Run Control environment and verification status.");

    // Future M02-M11 integration belongs after explicit module validation.
    // M01 does not register gameplay callbacks or apply registry parameters.
}

public Action Command_TankRunStatus(int client, int args)
{
    // Keep this diagnostic admin-only even if a command override is permissive.
    // Client 0 is the trusted server console (including authenticated RCON).
    if (client != 0 && GetUserAdmin(client) == INVALID_ADMIN_ID)
    {
        ReplyToCommand(client, "[TankRun] Access denied: administrator required.");
        return Plugin_Handled;
    }

    char mode[256];
    ConVar gameMode = FindConVar("mp_gamemode");
    if (gameMode == null)
    {
        strcopy(mode, sizeof(mode), "<unavailable: mp_gamemode not found>");
    }
    else
    {
        // Read on demand; do not cache a mode across changes or map loads.
        gameMode.GetString(mode, sizeof(mode));
        if (mode[0] == '\0')
        {
            strcopy(mode, sizeof(mode), "<empty>");
        }
    }

    ReplyToCommand(client, "[TankRun] Tank Run Control v%s | game: Left 4 Dead 2", PLUGIN_VERSION);
    ReplyToCommand(client, "[TankRun] Diagnostics: enabled (read-only) | gameplay control: disabled (not implemented)");
    ReplyToCommand(client, "[TankRun] mp_gamemode: %s", mode);
    ReplyToCommand(client, "[TankRun] Original Tank Run script source: UNVERIFIED; mode name alone is not proof.");
    ReplyToCommand(client, "[TankRun] Native DLL SHA256/build match: UNVERIFIED (no binary inspection).");
    ReplyToCommand(client, "[TankRun] Registry parameter values/load overrides: UNVERIFIED (not read or applied).");
    ReplyToCommand(client, "[TankRun] Single/multiplayer compatibility: UNVERIFIED; M02-M11: not implemented.");
    return Plugin_Handled;
}
