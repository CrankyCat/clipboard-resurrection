# SPDX-License-Identifier: GPL-3.0-or-later
#Requires -Version 7.0
<# Report by default. -Apply verifies a complete archive outside build, then
   removes only the exact workspace build directory under the publication lock. #>
[CmdletBinding()]
param([switch] $Apply)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).TrimEnd('\')
$build = [IO.Path]::GetFullPath((Join-Path $root 'build'))
if ($build -ine ($root + '\build')) { throw 'Unexpected build cleanup target.' }
. (Join-Path $root 'tools/build/ClipboardBuildIdentity.ps1')
$lock = Enter-ClipboardBuildLock $root
try {
    $part = $build
    while ($part) {
        if ((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Reparse point rejected: $part"
        }
        $part = Split-Path -Parent $part
    }
    if (-not (Test-Path -LiteralPath $build)) {
        [pscustomobject]@{result='already-clean';build=$build}
        return
    }
    $items = @(Get-ChildItem -LiteralPath $build -Recurse -Force)
    if (@($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Reparse point inside build.' }
    $files = @($items | Where-Object { -not $_.PSIsContainer })
    if (-not $Apply) {
        $bytes = if ($files.Count) { ($files | Measure-Object Length -Sum).Sum } else { 0 }
        [pscustomobject]@{result='dry-run';build=$build;files=$files.Count;bytes=$bytes;action='Archive all remaining contents under .local/build-history, verify, then delete build only'}
        return
    }
    $preflight = & (Join-Path $root 'Build.ps1') -CheckOnly
    if ($preflight.result -ne 'passed') { throw 'Persistent-input preflight failed.' }
    $python = $preflight.PythonPath
    $output = & $python -B (Join-Path $PSScriptRoot 'ArchiveBuildTree.py') --root $root
    if ($LASTEXITCODE -ne 0) { throw 'Build archive verification failed; nothing was deleted.' }
    $saved = ($output -join [Environment]::NewLine) | ConvertFrom-Json
    if ($saved.result -ne 'passed') { throw 'Missing verified archive.' }
    $manifest = Get-Content -LiteralPath $saved.manifest -Raw | ConvertFrom-Json
    if ($manifest.root -ine $root) { throw 'Archive belongs to another workspace.' }
    $items = @(Get-ChildItem -LiteralPath $build -Recurse -Force)
    if (@($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Reparse point appeared inside build.' }
    $files = @($items | Where-Object { -not $_.PSIsContainer })
    if ($files.Count -ne $manifest.files.Count) { throw 'Build inventory changed after archiving.' }
    foreach ($entry in $manifest.files) {
        $path = [IO.Path]::GetFullPath((Join-Path $root $entry.path))
        if (-not $path.StartsWith($build + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Archive entry escaped build.' }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $entry.sha256) { throw "Build file changed: $path" }
    }
    if ((Get-FileHash -LiteralPath $saved.archive -Algorithm SHA256).Hash -ine $manifest.archive.sha256) { throw 'Retained archive changed.' }
    # Root containment and every descendant were checked in this PowerShell
    # process. No paths are passed to another shell for destructive operations.
    Remove-Item -LiteralPath $build -Recurse -Force
    $after = & (Join-Path $root 'Build.ps1') -CheckOnly
    if ($after.result -ne 'passed' -or (Test-Path -LiteralPath $build)) { throw 'Empty-build preflight failed; complete pre-clean archive retained.' }
    [pscustomobject]@{result='cleaned';build=$build;files=$saved.files;removedBytes=$saved.sourceBytes;archiveBytes=$saved.archiveBytes;archive=$saved.archive;manifest=$saved.manifest;emptyBuildPreflight='passed'}
} finally { $lock.ReleaseMutex(); $lock.Dispose() }
