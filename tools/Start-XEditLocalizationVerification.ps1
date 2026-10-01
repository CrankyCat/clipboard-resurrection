# SPDX-License-Identifier: GPL-3.0-or-later
#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('en', 'ru', 'de', 'es', 'esmx', 'fr', 'it', 'ja', 'pl', 'ptbr', 'zhhans', 'zhhant', 'cn')][string] $Language,
    [string] $EvidenceRoot = 'outputs\localization-implementation',
    [switch] $Launch
)
$ErrorActionPreference = 'Stop'
$Language = $Language.ToLowerInvariant()
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$legacyEvidence = Join-Path $root 'outputs\localization-implementation'
$evidence = if ([IO.Path]::IsPathRooted($EvidenceRoot)) {
    [IO.Path]::GetFullPath($EvidenceRoot)
} else {
    [IO.Path]::GetFullPath((Join-Path $root $EvidenceRoot))
}
$outputsScope = [IO.Path]::GetFullPath((Join-Path $root 'outputs')) + [IO.Path]::DirectorySeparatorChar
if (-not $evidence.StartsWith($outputsScope, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'EvidenceRoot must be a directory below this project outputs folder.'
}
$ancestor = $evidence
while ($ancestor.StartsWith($outputsScope, [StringComparison]::OrdinalIgnoreCase)) {
    if ((Test-Path -LiteralPath $ancestor) -and
        ((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "EvidenceRoot must not traverse a directory link: $ancestor"
    }
    $ancestor = [IO.Path]::GetDirectoryName($ancestor)
}
$isolated = Join-Path $evidence 'xedit'
$run = Join-Path $evidence ('xedit-readback\' + $Language)
$exe = Join-Path $isolated 'app\FO4Edit.exe'
$data = Join-Path $isolated 'Data'
$esp = Join-Path $data 'Clipboard.esp'
$overlay = Join-Path $root 'package\v240'
$mappingPath = Join-Path $root 'localization\esp-map.json'
$mapping = Get-Content -LiteralPath $mappingPath -Raw | ConvertFrom-Json
$locales = @('en', 'ru', 'de', 'es', 'esmx', 'fr', 'it', 'ja', 'pl', 'ptbr', 'zhhans', 'zhhant', 'cn')
$catalogLocale = if ($Language -eq 'cn') { 'zhhant' } else { $Language }
$catalogPath = Join-Path $root ('localization\' + $catalogLocale + '.json')
$englishSourcePath = Join-Path $root 'localization\source\esp.en.json'
$englishSourceHash = (Get-FileHash -LiteralPath $englishSourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
$displayOverridesPath = Join-Path $root 'localization\esp-display-overrides.json'
$displayOverridesHash = if (Test-Path -LiteralPath $displayOverridesPath -PathType Leaf) {
    (Get-FileHash -LiteralPath $displayOverridesPath -Algorithm SHA256).Hash.ToLowerInvariant()
} else { $null }
if ($Language -ne 'en' -and -not (Test-Path -LiteralPath $catalogPath -PathType Leaf)) {
    throw "Generate the translated source catalog before readback: $catalogPath"
}
if (Get-Process -Name FO4Edit, FO4Edit64 -ErrorAction SilentlyContinue) {
    throw 'An xEdit process is already open. Leave the existing session to its owner.'
}
if ((Test-Path -LiteralPath (Join-Path $run 'readback.json')) -or
    (Test-Path -LiteralPath (Join-Path $run 'readback.tsv'))) {
    throw "A readback already exists; retain or relocate the previous run before repeating: $run"
}
# Freeze the complete language set before any fixture copy. A different candidate
# needs a new EvidenceRoot so completed readbacks retain their original inputs.
$tableSources = @()
$readbackBase = Join-Path $evidence 'xedit-readback'
$hasReadbacks = (Test-Path -LiteralPath $readbackBase) -and
    @(Get-ChildItem -LiteralPath $readbackBase -Filter 'readback.json' -Recurse -File).Count -gt 0
foreach ($kind in @('STRINGS', 'DLSTRINGS', 'ILSTRINGS')) {
    foreach ($locale in $locales) {
        $name = 'Clipboard_' + $locale + '.' + $kind
        $source = Join-Path $overlay ('Strings\' + $name)
        $destination = Join-Path $data ('Strings\' + $name)
        $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hasReadbacks -and (Test-Path -LiteralPath $destination -PathType Leaf) -and
            (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) {
            throw "Completed readback inputs must remain unchanged. Choose a new EvidenceRoot: $name"
        }
        $tableSources += [ordered]@{ source = $source; path = $destination; sha256 = $hash }
    }
}
if ((Get-FileHash -LiteralPath (Join-Path $overlay 'Clipboard.esp') -Algorithm SHA256).Hash -ne $mapping.localized_esp_sha256) {
    throw 'Package ESP hash does not match esp-map.json.'
}
# New evidence roots get one independent fixture shared by all language runs.
# Existing EN/RU evidence, its Data directory and its executable remain untouched.
$fixtureFiles = @('app\FO4Edit.exe', 'Data\Fallout4.esm', 'Data\Fallout4 - Interface.ba2',
    'Data\Clipboard.esp', 'ini\Fallout4.ini', 'ini\Fallout4Custom.ini', 'ini\Fallout4Prefs.ini', 'plugins.txt')
$fixtureRecords = @()
foreach ($relative in $fixtureFiles) {
    $target = Join-Path $isolated $relative
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        if ($evidence -eq $legacyEvidence) { throw 'Prepare the original isolated project-local xEdit fixture first.' }
        $source = Join-Path (Join-Path $legacyEvidence 'xedit') $relative
        $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $sourceHash) {
            throw "Fixture copy mismatch: $relative"
        }
    }
    $fixtureRecords += [ordered]@{ path = $target; sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() }
}
if ((Get-FileHash -LiteralPath $esp -Algorithm SHA256).Hash -ne $mapping.localized_esp_sha256) {
    throw "Localized ESP hash does not match esp-map.json: $esp"
}
New-Item -ItemType Directory -Path (Join-Path $data 'Strings') -Force | Out-Null
$originals = Join-Path $evidence 'xedit-original-tables'
if (-not (Test-Path -LiteralPath $originals)) {
    New-Item -ItemType Directory -Path $originals | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $data 'Strings') -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $originals $_.Name)
    }
}
New-Item -ItemType Directory -Path $run -Force | Out-Null
foreach ($directory in @('cache', 'temp', 'backups', 'output')) {
    New-Item -ItemType Directory -Path (Join-Path $run $directory) -Force | Out-Null
}
$tableRecords = @()
foreach ($table in $tableSources) {
    Copy-Item -LiteralPath $table.source -Destination $table.path
    if ((Get-FileHash -LiteralPath $table.path -Algorithm SHA256).Hash -ne $table.sha256) { throw "Table copy mismatch: $($table.path)" }
    $tableRecords += $table
}
$script = Join-Path $run 'VerifyClipboardLocalization.pas'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'xedit\VerifyClipboardLocalization.pas') -Destination $script
$arguments = @(
    '-fo4', ('-script:"' + $script + '"'), ('-l:' + $Language), '-nobuildrefs',
    ('-D:"' + $data + '"'), ('-S:"' + $run + '"'),
    ('-P:"' + (Join-Path $isolated 'plugins.txt') + '"'),
    ('-I:"' + (Join-Path $isolated 'ini\Fallout4.ini') + '"'),
    ('-CustomIni:"' + (Join-Path $isolated 'ini\Fallout4Custom.ini') + '"'),
    ('-M:"' + (Join-Path $isolated 'ini') + '"'),
    ('-C:"' + (Join-Path $run 'cache') + '"'),
    ('-T:"' + (Join-Path $run 'temp') + '"'),
    ('-B:"' + (Join-Path $run 'backups') + '"'),
    ('-O:"' + (Join-Path $run 'output') + '"'),
    ('-R:"' + (Join-Path $run 'xedit.log') + '"')
)
$record = [ordered]@{
    schema_version = 2; language = $Language; catalog_locale = $catalogLocale; executable = $exe
    executable_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    esp_path = $esp; esp_sha256_before = (Get-FileHash -LiteralPath $esp -Algorithm SHA256).Hash.ToLowerInvariant()
    script_path = $script; script_sha256 = (Get-FileHash -LiteralPath $script -Algorithm SHA256).Hash.ToLowerInvariant()
    tables = $tableRecords; fixture_inputs = $fixtureRecords; arguments = $arguments; launched = [bool]$Launch
    mapping_path = $mappingPath; mapping_sha256 = (Get-FileHash -LiteralPath $mappingPath -Algorithm SHA256).Hash.ToLowerInvariant()
    english_source_path = $englishSourcePath; english_source_sha256 = $englishSourceHash
    display_overrides_path = $(if ($displayOverridesHash) { $displayOverridesPath } else { $null })
    display_overrides_sha256 = $displayOverridesHash
    evidence_root = $evidence
    started_utc = [DateTime]::UtcNow.ToString('o')
}
if ($Language -ne 'en') {
    $record.catalog_path = $catalogPath
    $record.catalog_sha256 = (Get-FileHash -LiteralPath $catalogPath -Algorithm SHA256).Hash.ToLowerInvariant()
}
if ($Launch) {
    # xEdit script mode is interactive: module selection must remain visible.
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $run -WindowStyle Normal -PassThru
    $record.process_id = $process.Id
}
$json = $record | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText((Join-Path $run 'launch.json'), $json + [Environment]::NewLine, (New-Object Text.UTF8Encoding($false)))
$json
