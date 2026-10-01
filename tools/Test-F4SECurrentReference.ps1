<#
.SYNOPSIS
Verifies every file in the pinned official F4SE 0.7.9 source reference.
#>
# SPDX-License-Identifier: GPL-3.0-or-later
#Requires -Version 5.1

[CmdletBinding()]
param([switch] $AsJson)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$manifestPath = Join-Path $projectRoot 'external\F4SE\current-reference.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

function Get-Sha256([byte[]] $Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Get-ComparableHash([string] $Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    try {
        $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($text.Replace("`r`n", "`n").Replace("`r", "`n"))
    } catch [Text.DecoderFallbackException] {
        # Files that are not valid UTF-8 are hashed as binary.
    }
    return Get-Sha256 $bytes
}

function Resolve-ChildPath([string] $Root, [string] $Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or
        @($Relative.Split([char[]]'\/') | Where-Object { $_ -eq '..' }).Count -ne 0) {
        throw "Expected a confined relative path: $Relative"
    }
    $full = [IO.Path]::GetFullPath((Join-Path $Root $Relative))
    $prefix = $Root.TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Source reference path escapes its root: $Relative"
    }
    return $full
}

if ($manifest.schemaVersion -ne 1 -or $manifest.version -cne '0.7.9' -or
    $manifest.upstreamTag -cne 'v0.7.9' -or
    $manifest.upstreamRepository -cne 'https://github.com/ianpatt/f4se' -or
    $manifest.upstreamCommit -cne '4692e9bba0f87d8b8d1a0b79110bf212f2b2ada7' -or
    $manifest.runtimeFixture -cne '1.11.240.0' -or $manifest.releaseIndex -ne 33 -or
    $manifest.sourceFileCount -le 0 -or @($manifest.files).Count -ne $manifest.sourceFileCount -or
    $manifest.normalizedTextOrBinaryTreeSha256 -cnotmatch '^[0-9a-f]{64}$') {
    throw 'The current F4SE reference is not the reviewed complete official 0.7.9 source manifest.'
}

$sourceRoot = Resolve-ChildPath $projectRoot ([string] $manifest.sourceRoot)
$scriptRoot = Resolve-ChildPath $sourceRoot ([string] $manifest.scriptSubdirectory)
$expected = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in @($manifest.files)) {
    $relative = [string] $entry.path
    $null = Resolve-ChildPath $sourceRoot $relative
    if ($relative.Contains('\') -or $entry.normalizedTextOrBinarySha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $expected.ContainsKey($relative)) {
        throw "Malformed or duplicate source manifest entry: $relative"
    }
    $expected.Add($relative, [string] $entry.normalizedTextOrBinarySha256)
}

$actual = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File -Force) {
    $relative = $file.FullName.Substring($sourceRoot.Length + 1).Replace('\', '/')
    if ($relative -eq '.git' -or $relative.StartsWith('.git/')) { continue }
    if (-not $expected.ContainsKey($relative)) { throw "Unexpected file in the F4SE source reference: $relative" }
    $hash = Get-ComparableHash $file.FullName
    if ($hash -cne $expected[$relative]) { throw "F4SE source differs from official 0.7.9: $relative" }
    $actual.Add($relative, $hash)
}
if ($actual.Count -ne $expected.Count) { throw 'F4SE source reference is incomplete.' }
$paths = [string[]] @($actual.Keys)
[Array]::Sort($paths, [StringComparer]::Ordinal)
$records = @($paths | ForEach-Object { "$($actual[$_])  $_`n" }) -join ''
$treeHash = Get-Sha256 ([Text.UTF8Encoding]::new($false).GetBytes($records))
if ($treeHash -cne $manifest.normalizedTextOrBinaryTreeSha256) { throw 'F4SE reference tree hash mismatch.' }
foreach ($subdirectory in 'vanilla', 'modified') {
    if (-not (Test-Path -LiteralPath (Join-Path $scriptRoot $subdirectory) -PathType Container)) {
        throw "F4SE Papyrus import directory is missing: $subdirectory"
    }
}

$result = [ordered]@{
    result = 'passed'
    sourceRoot = $sourceRoot
    scriptRoot = $scriptRoot
    sourceFileCount = $actual.Count
    upstreamTag = [string] $manifest.upstreamTag
    upstreamCommit = [string] $manifest.upstreamCommit
    normalizedTextOrBinaryTreeSha256 = $treeHash
    manifestPath = $manifestPath
    manifestSha256 = Get-Sha256 ([IO.File]::ReadAllBytes($manifestPath))
}
if ($AsJson) { $result | ConvertTo-Json -Depth 3 }
else {
    Write-Output "F4SE 0.7.9 source reference verified: $($actual.Count) files, commit $($manifest.upstreamCommit)."
    Write-Output "Source: $sourceRoot"
    Write-Output "Tree SHA-256: $treeHash"
}
