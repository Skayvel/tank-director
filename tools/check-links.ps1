# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Tank Director contributors

#Requires -Version 5.1
<#
.SYNOPSIS
    Markdown link audit for a documentation tree.

.DESCRIPTION
    Parses every "](target)" link in every *.md file under -Root and classifies it:

      external    http(s), mailto, tel, or any other "scheme://" URL
      anchor      "#section" only
      absolute    drive-letter path (C:\...), UNC path (\\...), or "/" rooted path
      resolved    relative path that exists
      unresolved  relative path that does not exist

    Classification is mutually exclusive, so the four buckets always sum to the total.
    Paths inside <...> and percent-encoded characters are handled; "?query" and "#fragment"
    are stripped before the filesystem test, so only the file part is validated.

.PARAMETER Root
    Directory to scan. Defaults to the current directory.

.PARAMETER Detail
    Print every unresolved link, grouped by source file.

.PARAMETER FailOnUnresolved
    Exit with code 1 when at least one unresolved relative link is found.

.EXAMPLE
    pwsh -File tools/check-links.ps1 -Root . -Detail
#>
[CmdletBinding()]
param(
    [string]$Root = '.',
    [switch]$Detail,
    [switch]$FailOnUnresolved
)

$ErrorActionPreference = 'Stop'
$rootFull = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')

# "](target)" or "](target "title")". Targets may not contain whitespace or ')'.
$linkPattern = '\]\((?<u>[^)\s]+)(?:\s+"[^"]*")?\)'

function Test-LinkTarget {
    param([string]$Url)
    # Windows drive paths ("D:/...", "C:\...") must be tested before generic schemes,
    # otherwise the drive letter parses as a one-character URL scheme.
    if ($Url -match '^[A-Za-z]:[\\/]') { return 'absolute' }
    if ($Url -match '^(?<scheme>[A-Za-z][A-Za-z0-9+.\-]*):') {
        return 'external'
    }
    if ($Url.StartsWith('#')) { return 'anchor' }

    $path = $Url
    $hash = $path.IndexOf('#')
    if ($hash -ge 0) { $path = $path.Substring(0, $hash) }
    $q = $path.IndexOf('?')
    if ($q -ge 0) { $path = $path.Substring(0, $q) }
    $path = $path.Trim('<', '>')
    if ([string]::IsNullOrWhiteSpace($path)) { return 'anchor' }

    if ($path -match '^[A-Za-z]:[\\/]' -or $path.StartsWith('\\') -or $path.StartsWith('/')) { return 'absolute' }

    return 'relative'
}

$rows = New-Object System.Collections.Generic.List[object]
$fileCount = 0

Get-ChildItem -LiteralPath $rootFull -Recurse -File -Filter *.md |
    Where-Object { $_.FullName -notmatch '[\\/]\.git[\\/]' } |
    ForEach-Object {
        $file = $_
        $fileCount++
        $text = [System.IO.File]::ReadAllText($file.FullName, [System.Text.Encoding]::UTF8)
        $rel = $file.FullName.Substring($rootFull.Length).TrimStart('\', '/')

        $idx = 0
        foreach ($m in [regex]::Matches($text, $linkPattern)) {
            $idx++
            $url = $m.Groups['u'].Value
            $kind = Test-LinkTarget -Url $url

            # Reject obvious false positives: a bare token without a separator or extension.
            if ($kind -eq 'relative' -and $url -notmatch '[/\\.]') {
                $kind = 'skipped'
            }

            $status = $kind
            if ($kind -eq 'relative') {
                $raw = $url
                $h = $raw.IndexOf('#'); if ($h -ge 0) { $raw = $raw.Substring(0, $h) }
                $qq = $raw.IndexOf('?'); if ($qq -ge 0) { $raw = $raw.Substring(0, $qq) }
                $raw = $raw.Trim('<', '>')
                try { $raw = [System.Uri]::UnescapeDataString($raw) } catch { }
                $target = Join-Path $file.DirectoryName $raw
                if (Test-Path -LiteralPath $target) { $status = 'resolved' } else { $status = 'unresolved' }
            }

            $rows.Add([pscustomobject]@{
                Source   = $rel
                Line     = ($text.Substring(0, $m.Index) -split "`n").Count
                Url      = $url
                Category = $status
            })
        }
    }

$grouped = $rows | Group-Object Category
$count = @{}
foreach ($g in $grouped) { $count[$g.Name] = $g.Count }
foreach ($k in 'external', 'anchor', 'absolute', 'resolved', 'unresolved', 'skipped') {
    if (-not $count.ContainsKey($k)) { $count[$k] = 0 }
}

$total = $rows.Count
$local = $count['resolved'] + $count['unresolved'] + $count['absolute']

Write-Output "root            : $rootFull"
Write-Output "markdown files  : $fileCount"
Write-Output "links total     : $total"
Write-Output "  external      : $($count['external'])"
Write-Output "  anchor only   : $($count['anchor'])"
Write-Output "  skipped(fp)   : $($count['skipped'])"
Write-Output "local links     : $local"
Write-Output "  absolute      : $($count['absolute'])"
Write-Output "  resolved      : $($count['resolved'])"
Write-Output "  unresolved    : $($count['unresolved'])"
Write-Output ""
Write-Output ("check: external+anchor+skipped+local = {0} (total {1})" -f ($count['external'] + $count['anchor'] + $count['skipped'] + $local), $total)
Write-Output ("check: absolute+resolved+unresolved = {0} (local {1})" -f ($count['absolute'] + $count['resolved'] + $count['unresolved']), $local)

if ($Detail -and $count['unresolved'] -gt 0) {
    Write-Output ""
    Write-Output '=== unresolved links by source file ==='
    $rows |
        Where-Object Category -eq 'unresolved' |
        Group-Object Source |
        Sort-Object Count -Descending |
        ForEach-Object {
            Write-Output ("{0,4}  {1}" -f $_.Count, $_.Name)
            foreach ($r in $_.Group) { Write-Output ("        line {0}: {1}" -f $r.Line, $r.Url) }
        }
}

if ($FailOnUnresolved -and $count['unresolved'] -gt 0) { exit 1 }
exit 0
