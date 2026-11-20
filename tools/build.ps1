# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tank Director contributors

#Requires -Version 5.1
<#
.SYNOPSIS
    Compile the Tank Director SourcePawn plugins.

.DESCRIPTION
    Builds one or all plugins from scripting/ using a local SourceMod 1.12
    toolchain. Every plugin that talks to Left4DHooks needs left4dhooks.inc,
    which is NOT shipped with SourceMod; pass its directory with -ExtraInclude
    (or place a copy in the SourceMod include directory).

.PARAMETER SourceMod
    Path to the SourceMod scripting directory that contains spcomp.exe
    and include/. Example:
      C:\servers\l4d2\left4dead2\addons\sourcemod\scripting

.PARAMETER ExtraInclude
    Additional include directory that contains left4dhooks.inc.
    Often the same SourceMod scripting\include directory if Left4DHooks
    is installed on that server.

.PARAMETER Target
    main      -> scripting/tankdirector_menu.sp (the shipped product)
    capacity  -> scripting/tankrun_capacity.sp (standalone variant)
    seats     -> scripting/tankrun_human_seats.sp
    telemetry -> scripting/tankrun_tick_telemetry.sp
    m08       -> scripting/tankrun_m08_probe.sp
    logger    -> scripting/tankrun_spawn_logger_v023.sp
    m01       -> scripting/tankrun_control.sp
    all       -> every target above (default)

.PARAMETER OutDir
    Output directory. Defaults to build/.

.PARAMETER NoStrict
    Disable warnings-as-errors (-E). Release builds should keep strict mode on.

.EXAMPLE
    .\tools\build.ps1 -SourceMod 'C:\srcds\left4dead2\addons\sourcemod\scripting'

.EXAMPLE
    .\tools\build.ps1 -SourceMod $sm -ExtraInclude $sm\include -Target main
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceMod,

    [string]$ExtraInclude,

    [ValidateSet('main', 'capacity', 'seats', 'telemetry', 'm08', 'logger', 'm01', 'all')]
    [string]$Target = 'all',

    [string]$OutDir,

    [switch]$NoStrict
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$scripting = Join-Path $repoRoot 'scripting'
if (-not $OutDir) { $OutDir = Join-Path $repoRoot 'build' }

$spcomp = Join-Path $SourceMod 'spcomp.exe'
$include = Join-Path $SourceMod 'include'

if (-not (Test-Path -LiteralPath $spcomp)) {
    throw "spcomp.exe not found at '$spcomp'. Pass -SourceMod pointing at addons/sourcemod/scripting."
}
if (-not (Test-Path -LiteralPath (Join-Path $include 'sourcemod.inc'))) {
    throw "sourcemod.inc not found in '$include'. The include directory must match spcomp."
}

$includeDirs = @($include)
if ($ExtraInclude) {
    if (-not (Test-Path -LiteralPath $ExtraInclude)) { throw "ExtraInclude directory not found: $ExtraInclude" }
    $includeDirs += (Resolve-Path -LiteralPath $ExtraInclude).Path
}

$hasLeft4D = $includeDirs | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'left4dhooks.inc') }
$hasDhooks = $includeDirs | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'dhooks.inc') }

Write-Host "spcomp   : $spcomp"
Write-Host "include  : $($includeDirs -join '; ')"
if (-not $hasLeft4D) {
    Write-Warning "left4dhooks.inc not found in any include directory. Left4DHooks targets will fail to compile."
}
if (-not $hasDhooks) {
    Write-Warning "dhooks.inc not found. The control center needs it for the optional native gate (#include <dhooks>)."
}

$targets = [ordered]@{
    main      = 'tankdirector_menu.sp'
    capacity  = 'tankrun_capacity.sp'
    seats     = 'tankrun_human_seats.sp'
    telemetry = 'tankrun_tick_telemetry.sp'
    m08       = 'tankrun_m08_probe.sp'
    logger    = 'tankrun_spawn_logger_v023.sp'
    m01       = 'tankrun_control.sp'
}

$selected = if ($Target -eq 'all') { $targets.Keys } else { @($Target) }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$report = New-Object System.Collections.Generic.List[string]
$failures = 0

foreach ($key in $selected) {
    $leaf = $targets[$key]
    $source = Join-Path $scripting $leaf
    if (-not (Test-Path -LiteralPath $source)) {
        Write-Warning "skip: $leaf not found"
        continue
    }

    $output = Join-Path $OutDir ([System.IO.Path]::GetFileNameWithoutExtension($leaf) + '.smx')
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Force }

    # Compile with a path RELATIVE to the repository root.
    # spcomp embeds every source file path it sees into the .names table of the
    # SMX. Passing an absolute path would publish the builder's directory
    # layout inside the shipped binary, and would also make the build depend on
    # where the repo happens to live. Relative + forward slashes matches how the
    # published 2.2.8 artifact was produced.
    $relSource = 'scripting/' + $leaf

    $spArgs = @()
    foreach ($dir in $includeDirs) { $spArgs += "-i$dir" }
    $spArgs += "-o$output"
    if (-not $NoStrict) { $spArgs += '-E' }

    Write-Host ""
    Write-Host "==> $leaf  ($relSource)"

    $logFile = Join-Path $OutDir ([System.IO.Path]::GetFileNameWithoutExtension($leaf) + '.compile.log')
    Push-Location -LiteralPath $repoRoot
    try {
        $stdout = & $spcomp $relSource @spArgs 2>&1
        $code = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    $stdout | Set-Content -LiteralPath $logFile -Encoding UTF8
    $stdout | ForEach-Object { Write-Host "    $_" }

    $size = if (Test-Path -LiteralPath $output) { (Get-Item -LiteralPath $output).Length } else { 0 }
    $hash = if ($size -gt 0) { (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash } else { '-' }

    $status = 'OK'
    if ($code -ne 0 -or $size -le 0) {
        $status = 'FAILED'
        $failures++
    }
    $report.Add(("{0,-10} {1,-32} {2,9}  {3}  {4}" -f $status, $leaf, $size, $hash, $logFile))
}

Write-Host ""
Write-Host "=== build summary ==="
$report | ForEach-Object { Write-Host $_ }

$summaryPath = Join-Path $OutDir 'build-report.txt'
$header = @(
    "Tank Director build report",
    "spcomp        : $spcomp",
    "include dirs  : $($includeDirs -join '; ')",
    "strict (-E)   : $(-not $NoStrict)",
    "left4dhooks   : $(if ($hasLeft4D) { 'found' } else { 'MISSING' })",
    "dhooks        : $(if ($hasDhooks) { 'found' } else { 'MISSING' })",
    "generated     : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')",
    ""
)
($header + $report) | Set-Content -LiteralPath $summaryPath -Encoding UTF8
Write-Host ""
Write-Host "report: $summaryPath"

if ($failures -gt 0) {
    Write-Error "$failures target(s) failed to compile."
    exit 1
}
exit 0
