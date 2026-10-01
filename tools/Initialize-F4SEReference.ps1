<#
.SYNOPSIS
Fetches and verifies the pinned official F4SE source for local Papyrus imports.
.DESCRIPTION
F4SE remains an external prerequisite. Its source cache is excluded from Git
and Clipboard packages. Existing caches are verified without being replaced.
#>
# SPDX-License-Identifier: GPL-3.0-or-later
#Requires -Version 5.1
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$manifestPath = Join-Path $projectRoot 'external\F4SE\current-reference.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$expectedRelative = 'external/F4SE/source-0.7.9'
if ($manifest.sourceRoot -cne $expectedRelative -or
    $manifest.upstreamRepository -cne 'https://github.com/ianpatt/f4se' -or
    $manifest.upstreamTag -cne 'v0.7.9' -or
    $manifest.upstreamCommit -cne '4692e9bba0f87d8b8d1a0b79110bf212f2b2ada7') {
    throw 'F4SE source manifest differs from the reviewed upstream pin.'
}
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot $expectedRelative))
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'external\F4SE')) + '\'
if (-not $sourceRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'F4SE cache must remain under external/F4SE.'
}
if (-not (Test-Path -LiteralPath $sourceRoot)) {
    $git = (Get-Command git -ErrorAction Stop).Source
    & $git clone --depth 1 --single-branch --branch $manifest.upstreamTag -- $manifest.upstreamRepository $sourceRoot
    if ($LASTEXITCODE -ne 0) { throw 'Could not fetch the official F4SE source reference.' }
    $commit = (& $git -C $sourceRoot rev-parse HEAD | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -cne $manifest.upstreamCommit) {
        throw 'Fetched F4SE tag does not match the reviewed commit; no build was started.'
    }
}
& (Join-Path $PSScriptRoot 'Test-F4SECurrentReference.ps1')
