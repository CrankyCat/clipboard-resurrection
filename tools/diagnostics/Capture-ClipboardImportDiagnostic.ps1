# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
Copies import logs and inputs into a new project evidence directory.
.DESCRIPTION
Read-only with respect to the game, mod manager and settings. Run before an
import after settings are selected, and after its result before another launch
rotates the logs. Disk hashes do not attest the DLL loaded in a running process.
#>
#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$')][string] $RunName,
    [Parameter(Mandatory)][ValidateSet('Baseline', 'Before', 'After')][string] $Phase,
    [Parameter(Mandatory)][string] $InstalledMod,
    [string] $GameUserRoot = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\Fallout4'),
    [string] $PatternPath,
    [string] $SettingsPath,
    [string] $ProfilePath,
    [string] $Notes,
    [ValidateRange(1, 2147483646)][int] $InternalBuild,
    [string] $PythonPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../build/ClipboardBuildPaths.ps1')
$InstalledMod = Resolve-ClipboardInputPath $InstalledMod 'InstalledMod' -Directory
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
# Validate explicit inputs and the complete publication before creating output.
# Missing optional logs are still recorded; installed/staged differences are evidence.
$GameUserRoot = Resolve-ClipboardInputPath $GameUserRoot 'GameUserRoot' -Directory
if ($PatternPath) { $PatternPath = Resolve-ClipboardInputPath $PatternPath 'PatternPath' }
if ($SettingsPath) { $SettingsPath = Resolve-ClipboardInputPath $SettingsPath 'SettingsPath' }
if ($ProfilePath) { $ProfilePath = Resolve-ClipboardInputPath $ProfilePath 'ProfilePath' -Directory }
$publicationArguments = @{ProjectRoot=$projectRoot; PythonPath=$PythonPath}
if ($PSBoundParameters.ContainsKey('InternalBuild')) { $publicationArguments.InternalBuild = $InternalBuild }
$publication = & (Join-Path $PSScriptRoot '../build/Get-ClipboardPublication.ps1') @publicationArguments
$stage = $publication.stage
$relativeFiles = @('F4SE\Plugins\clipboard.dll') + @(
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'src\papyrus') -Filter '*.psc' -File |
        Sort-Object Name | ForEach-Object { 'Scripts\' + $_.BaseName + '.pex' }
)
$identities = foreach ($relative in $relativeFiles) {
    $installedPath = Resolve-ClipboardInputPath (Join-Path $InstalledMod $relative) 'InstalledMod component'
    $stagedPath = Resolve-ClipboardInputPath (Join-Path $stage $relative) 'Publication component'
    $installedHash = (Get-FileHash -LiteralPath $installedPath -Algorithm SHA256).Hash
    $stagedHash = (Get-FileHash -LiteralPath $stagedPath -Algorithm SHA256).Hash
    [pscustomobject]@{
        file=$relative; installedPath=$installedPath; installedSha256=$installedHash
        stagedPath=$stagedPath; stagedSha256=$stagedHash; matchesStage=($installedHash -eq $stagedHash)
    }
}
$captureRoot = Join-Path $projectRoot 'outputs\import-diagnostics'
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$destination = Join-Path $captureRoot "$RunName-$Phase-$stamp"
if (Test-Path -LiteralPath $destination) { throw "Capture already exists: $destination" }

$sources = [ordered]@{
    'Clipboard.log' = Join-Path $GameUserRoot 'F4SE\Clipboard.log'
    'Papyrus.0.log' = Join-Path $GameUserRoot 'Logs\Script\Papyrus.0.log'
    'ClipboardPowerProbe.0.log' = Join-Path $GameUserRoot 'Logs\Script\User\ClipboardPowerProbe.0.log'
    'installed-default-settings.ini' = Join-Path $InstalledMod 'MCM\Config\Clipboard\settings.ini'
    'f4se.log' = Join-Path $GameUserRoot 'F4SE\f4se.log'
    'Fallout4.ini' = Join-Path $GameUserRoot 'Fallout4.ini'
    'Fallout4Custom.ini' = Join-Path $GameUserRoot 'Fallout4Custom.ini'
}
if ($PatternPath) { $sources['pattern.ini'] = $PatternPath }
if ($SettingsPath) { $sources['Clipboard.ini'] = $SettingsPath }
if ($ProfilePath) {
    foreach ($name in @('modlist.txt', 'plugins.txt', 'loadorder.txt', 'Fallout4.ini', 'Fallout4Custom.ini')) {
        $sources["profile-$name"] = Join-Path $ProfilePath $name
    }
}
New-Item -ItemType Directory -Path $destination | Out-Null
$copies = foreach ($entry in $sources.GetEnumerator()) {
    $source = [IO.Path]::GetFullPath($entry.Value)
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        [pscustomobject]@{source=$source; copy=$entry.Key; status='missing'}
        continue
    }
    $before = Get-Item -LiteralPath $source
    $lengthBefore = $before.Length
    $writeBefore = $before.LastWriteTimeUtc
    $target = Join-Path $destination $entry.Key
    Copy-Item -LiteralPath $source -Destination $target
    $after = Get-Item -LiteralPath $source
    [pscustomobject]@{
        source=$source; copy=$entry.Key; status='copied'
        sourceLengthBefore=$lengthBefore; sourceLengthAfter=$after.Length
        sourceWriteBefore=$writeBefore.ToString('o'); sourceWriteAfter=$after.LastWriteTimeUtc.ToString('o')
        sourceStableDuringCopy=($lengthBefore -eq $after.Length -and $writeBefore -eq $after.LastWriteTimeUtc)
        bytes=(Get-Item -LiteralPath $target).Length
        sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    }
}
$gameProcesses = @(Get-Process -Name Fallout4 -ErrorAction SilentlyContinue | ForEach-Object {
    [pscustomobject]@{id=$_.Id; startTimeUtc=$_.StartTime.ToUniversalTime().ToString('o')}
})
$record = [ordered]@{
    schemaVersion=1; capturedUtc=[DateTime]::UtcNow.ToString('o'); runName=$RunName; phase=$Phase; notes=$Notes
    scope='Disk copies and identity only; not effective virtual-filesystem or loaded-memory attestation. Settings may have changed after launch. Source stability compares size and timestamp.'
    publication=$publication
    gameProcesses=$gameProcesses; files=@($copies); identities=@($identities)
}
[IO.File]::WriteAllText((Join-Path $destination 'capture.json'), ($record | ConvertTo-Json -Depth 8) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output "Capture: $destination"
Write-Output "Installed/staged identities matching: $(@($identities | Where-Object matchesStage).Count)/$($identities.Count)"
foreach ($copy in $copies) {
    if ($copy.status -eq 'missing') { Write-Warning "Missing input: $($copy.source)" }
    elseif (-not $copy.sourceStableDuringCopy) { Write-Warning "Source changed during copy: $($copy.source); take another capture after it settles." }
}
