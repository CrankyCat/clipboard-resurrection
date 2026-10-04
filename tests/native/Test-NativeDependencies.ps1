<#
.SYNOPSIS
Verifies the exact CommonLibF4RD source snapshot used by the native target.

.DESCRIPTION
Hashes every vendored file after normalizing text line endings. The sorted
per-file hash records are then hashed into one deterministic tree digest.
Git metadata is deliberately excluded. Pristine snapshots retain their pinned
upstream identity; patched snapshots additionally validate the documented
local patch files and preserve the original upstream tree digest separately.
#>

#Requires -Version 5.1

[CmdletBinding()]
param(
    [string] $ProjectRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Join-Path $PSScriptRoot '../..'
}

$project = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
$vendorRoot = Join-Path $project 'external\CommonLibF4RD'
$commitPath = Join-Path $project 'config\dependencies\CommonLibF4RD.commit'
$snapshotPath = Join-Path $project 'config\dependencies\CommonLibF4RD.snapshot.json'

foreach ($path in @($vendorRoot, $commitPath, $snapshotPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Vendored CommonLibF4RD input is missing: $path"
    }
}

$expectedCommit = (Get-Content -LiteralPath $commitPath -Raw).Trim()
if ($expectedCommit -cnotmatch '^[0-9a-f]{40}$') {
    throw 'config\dependencies\CommonLibF4RD.commit must contain one lowercase, full Git commit ID.'
}

$snapshot = Get-Content -LiteralPath $snapshotPath -Raw | ConvertFrom-Json
if ($snapshot.schemaVersion -ne 1) {
    throw "Unsupported CommonLibF4RD snapshot schema: $($snapshot.schemaVersion)"
}
if ([string] $snapshot.upstreamCommit -cne $expectedCommit) {
    throw 'CommonLibF4RD snapshot metadata disagrees with the pinned commit marker.'
}
if ([int] $snapshot.fileCount -le 0) {
    throw "CommonLibF4RD snapshot has an invalid file count: $($snapshot.fileCount)"
}
$expectedTreeHash = ([string] $snapshot.normalizedTextTreeSha256).ToLowerInvariant()
if ($expectedTreeHash -cnotmatch '^[0-9a-f]{64}$') {
    throw 'CommonLibF4RD snapshot metadata does not contain a lowercase SHA-256 digest.'
}
$localPatchCount = 0
if ($snapshot.PSObject.Properties['localPatches']) {
    $localPatches = @($snapshot.localPatches)
    if ($localPatches.Count -eq 0 -or
        -not $snapshot.PSObject.Properties['upstreamNormalizedTextTreeSha256'] -or
        [string] $snapshot.upstreamNormalizedTextTreeSha256 -cnotmatch '^[0-9a-f]{64}$') {
        throw 'Patched CommonLibF4RD snapshots require a nonempty local patch list and the original upstream tree SHA-256.'
    }
    $patchPrefix = [IO.Path]::GetFullPath((Join-Path $project 'external\patches')).TrimEnd('\') + '\'
    $seenPatchPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($patchRecord in $localPatches) {
        $relativePatchPath = [string] $patchRecord.path
        if ([string]::IsNullOrWhiteSpace($relativePatchPath) -or
            [IO.Path]::IsPathRooted($relativePatchPath) -or
            [string] $patchRecord.sha256 -cnotmatch '^[0-9a-f]{64}$') {
            throw 'CommonLibF4RD local patch metadata must contain a relative path and lowercase SHA-256.'
        }
        $patchPath = [IO.Path]::GetFullPath((Join-Path $project $relativePatchPath))
        if (-not $patchPath.StartsWith($patchPrefix, [StringComparison]::OrdinalIgnoreCase) -or
            -not $seenPatchPaths.Add($patchPath) -or
            -not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
            throw "CommonLibF4RD local patch is missing, duplicated or outside external/patches: $relativePatchPath"
        }
        $patchHasher = [Security.Cryptography.SHA256]::Create()
        try {
            $actualPatchHash = [BitConverter]::ToString(
                $patchHasher.ComputeHash([IO.File]::ReadAllBytes($patchPath))
            ).Replace('-', '').ToLowerInvariant()
        }
        finally { $patchHasher.Dispose() }
        if ($actualPatchHash -cne [string] $patchRecord.sha256) {
            throw "CommonLibF4RD local patch SHA-256 changed: $relativePatchPath"
        }
    }
    $localPatchCount = $localPatches.Count
}

$vendorPrefix = $vendorRoot.TrimEnd('\') + '\'
$fileByRelativePath = [Collections.Generic.Dictionary[string, IO.FileInfo]]::new(
    [StringComparer]::Ordinal
)
foreach ($file in Get-ChildItem -LiteralPath $vendorRoot -Recurse -Force -File) {
    $relative = $file.FullName.Substring($vendorPrefix.Length).Replace('\', '/')
    if ($relative -eq '.git' -or
        $relative.StartsWith('.git/', [StringComparison]::OrdinalIgnoreCase)) {
        continue
    }
    $fileByRelativePath.Add($relative, $file)
}

$relativePaths = [string[]] @($fileByRelativePath.Keys)
[Array]::Sort($relativePaths, [StringComparer]::Ordinal)
if ($relativePaths.Count -ne [int] $snapshot.fileCount) {
    throw "CommonLibF4RD snapshot file count changed. Expected $($snapshot.fileCount), found $($relativePaths.Count)."
}

$utf8 = [Text.UTF8Encoding]::new($false, $true)
$records = foreach ($relative in $relativePaths) {
    $file = $fileByRelativePath[$relative]
    try {
        $text = $utf8.GetString([IO.File]::ReadAllBytes($file.FullName))
    }
    catch {
        throw "CommonLibF4RD snapshot contains a non-UTF-8 file: $relative"
    }

    $normalized = $text.Replace("`r`n", "`n").Replace("`r", "`n")
    $fileHasher = [Security.Cryptography.SHA256]::Create()
    try {
        $fileHash = [BitConverter]::ToString(
            $fileHasher.ComputeHash($utf8.GetBytes($normalized))
        ).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $fileHasher.Dispose()
    }
    "$fileHash  $relative`n"
}

$treeHasher = [Security.Cryptography.SHA256]::Create()
try {
    $actualTreeHash = [BitConverter]::ToString(
        $treeHasher.ComputeHash($utf8.GetBytes(($records -join '')))
    ).Replace('-', '').ToLowerInvariant()
}
finally {
    $treeHasher.Dispose()
}

if ($actualTreeHash -cne $expectedTreeHash) {
    throw "CommonLibF4RD source snapshot changed. Expected SHA-256 $expectedTreeHash, found $actualTreeHash."
}

$sourceDescription = if ($localPatchCount -gt 0) { "upstream plus $localPatchCount documented local patch(es)" } else { 'pristine upstream' }
Write-Output "Verified CommonLibF4RD snapshot: $expectedCommit ($sourceDescription; $($relativePaths.Count) files, SHA-256 $actualTreeHash)"
