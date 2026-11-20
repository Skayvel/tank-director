# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tank Director contributors

#Requires -Version 5.1
<#
.SYNOPSIS
    Package a compiled control center build into the release ZIP layout.

.DESCRIPTION
    Produces dist/TankRun-Control-Center-v<version>.zip containing exactly:

        addons/sourcemod/plugins/tankdirector_menu.smx
        addons/sourcemod/gamedata/tankrun_firstrock.txt

    and writes dist/SHA256SUMS.txt with the hashes of the inputs and the ZIP.

    The version is read from TR_VERSION in scripting/tankrun/tr_core.inc so the
    package name cannot drift from the source.

.PARAMETER Smx
    Compiled plugin. Defaults to build/tankdirector_menu.smx.

.PARAMETER Gamedata
    Signature file. Defaults to gamedata/tankrun_firstrock.txt.

.PARAMETER Version
    Override the version read from the source.

.EXAMPLE
    .\tools\package.ps1

.EXAMPLE
    .\tools\package.ps1 -Smx .\build\tankdirector_menu.smx -Version 2.2.8
#>
[CmdletBinding()]
param(
    [string]$Smx,
    [string]$Gamedata,
    [string]$Version
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Smx) { $Smx = Join-Path $repoRoot 'build\tankdirector_menu.smx' }
if (-not $Gamedata) { $Gamedata = Join-Path $repoRoot 'gamedata\tankrun_firstrock.txt' }

if (-not (Test-Path -LiteralPath $Smx)) { throw "Compiled plugin not found: $Smx (run tools/build.ps1 first)" }
if (-not (Test-Path -LiteralPath $Gamedata)) { throw "Gamedata not found: $Gamedata" }

if (-not $Version) {
    $core = Join-Path $repoRoot 'scripting\tankrun\tr_core.inc'
    if (-not (Test-Path -LiteralPath $core)) { throw "Cannot read version: $core not found" }
    $m = Select-String -LiteralPath $core -Pattern '#define\s+TR_VERSION\s+"([^"]+)"' | Select-Object -First 1
    if (-not $m) { throw 'TR_VERSION not found in tr_core.inc' }
    $Version = $m.Matches[0].Groups[1].Value
}

$distDir = Join-Path $repoRoot 'dist'
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
$zipPath = Join-Path $distDir "TankRun-Control-Center-v$Version.zip"
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }

# Build the archive entry by entry with forward-slash names.
# ZipFile::CreateFromDirectory writes Windows backslashes on .NET Framework, which
# breaks extraction on Linux/macOS; plugin users unzip the package on both.
# ZipArchive/ZipArchiveMode live in System.IO.Compression; ZipFile lives in
# System.IO.Compression.FileSystem. Both must be loaded under Windows PowerShell 5.1.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$entries = [ordered]@{
    'addons/sourcemod/plugins/tankdirector_menu.smx'    = $Smx
    'addons/sourcemod/gamedata/tankrun_firstrock.txt'   = $Gamedata
}

$zipStream = [System.IO.File]::Open($zipPath, [System.IO.FileMode]::Create)
try {
    $archive = New-Object System.IO.Compression.ZipArchive($zipStream, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($name in $entries.Keys) {
            $entry = $archive.CreateEntry($name, [System.IO.Compression.CompressionLevel]::Optimal)
            $entryStream = $entry.Open()
            try {
                $bytes = [System.IO.File]::ReadAllBytes($entries[$name])
                $entryStream.Write($bytes, 0, $bytes.Length)
            }
            finally { $entryStream.Dispose() }
        }
    }
    finally { $archive.Dispose() }
}
finally { $zipStream.Dispose() }

$smxHash = (Get-FileHash -LiteralPath $Smx -Algorithm SHA256).Hash
$gdHash = (Get-FileHash -LiteralPath $Gamedata -Algorithm SHA256).Hash
$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash

$sums = @(
    "# Tank Run Control Center v$Version",
    "# generated $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
    "$smxHash  TankRun-Control-Center-v$Version/addons/sourcemod/plugins/tankdirector_menu.smx",
    "$gdHash  TankRun-Control-Center-v$Version/addons/sourcemod/gamedata/tankrun_firstrock.txt",
    "$zipHash  $(Split-Path $zipPath -Leaf)"
)
$sumsPath = Join-Path $distDir 'SHA256SUMS.txt'
$sums | Set-Content -LiteralPath $sumsPath -Encoding UTF8

Write-Host "version : $Version"
Write-Host "zip     : $zipPath"
Write-Host "size    : $((Get-Item -LiteralPath $zipPath).Length) bytes"
Write-Host "sha256  : $zipHash"
Write-Host "sums    : $sumsPath"
exit 0
