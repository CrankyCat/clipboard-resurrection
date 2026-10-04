<#
.SYNOPSIS
Checks, compiles and locally stages a standalone Clipboard diagnostic probe.

.DESCRIPTION
Run from a Clipboard Resurrection source checkout with the pinned F4SE 0.7.9
reference initialized and a Fallout 4 Papyrus compiler/Base source installation.
Uses the same verified vanilla-plus-modified F4SE merge as Build-Papyrus.ps1,
ahead of game imports. Compiles only the selected probe, outside the main
16-script build and package. The default runs a no-assembly check before the
real build and produces a separate project-local package and ZIP. -CheckOnly
performs only the compiler check. -Probe Power selects ClipboardPowerProbe in
separate build/stage directories and imports the maintained ClipboardExtension.
The default Object probe and its output paths remain unchanged.
Nothing is installed into the game or a mod
manager. The packaged copy of this helper requires the complete repository;
it is preserved as the exact build recipe, not a standalone compiler bundle.
#>
# SPDX-License-Identifier: GPL-3.0-or-later
#Requires -Version 5.1

[CmdletBinding()]
param(
    [switch] $CheckOnly,
    [ValidateSet('Object', 'Power')]
    [string] $Probe = 'Object',
    [string] $GameRoot,
    [string] $ReadmePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).TrimEnd('\')
. (Join-Path $PSScriptRoot '../build/ClipboardBuildPaths.ps1')
$GameRoot = Resolve-ClipboardGameRoot $GameRoot $projectRoot
if (-not $CheckOnly) {
    $ReadmePath = Resolve-ClipboardInputPath $ReadmePath 'ReadmePath (the instructions to include in the standalone probe package)'
}
$sourceRoot = Join-Path $projectRoot 'tools\diagnostics\papyrus'
$scriptName = 'Clipboard' + $Probe + 'Probe'
$productName = 'Clipboard ' + $Probe + ' Probe'
$sourcePath = Join-Path $sourceRoot ($scriptName + '.psc')
$licensePath = Join-Path $projectRoot 'LICENSE.txt'
$buildRoot = Join-Path $projectRoot ('build\papyrus\' + $Probe.ToLowerInvariant() + '-probe')
$mergedImport = Join-Path $buildRoot 'imports\f4se-0.7.9-merged'
$outputRoot = Join-Path $buildRoot 'Scripts'
$outputPath = Join-Path $outputRoot ($scriptName + '.pex')
$logRoot = Join-Path $buildRoot 'logs'
$stageRoot = Join-Path $projectRoot ('dist\' + $productName)
$archivePath = $stageRoot + '.zip'
$stageManifestPath = $stageRoot + '.manifest.sha256'
$stageBuildPath = $stageRoot + '.build.json'
$compiler = Join-Path $GameRoot 'Papyrus Compiler\PapyrusCompiler.exe'
$gameSources = Join-Path $GameRoot 'Data\Scripts\Source'
$flags = Join-Path $gameSources 'Base\Institute_Papyrus_Flags.flg'
$utf8NoBom = New-Object Text.UTF8Encoding($false)
$runStamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffffffZ')

function Assert-File([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file was not found: $Path"
    }
}

function Assert-GeneratedPath([string] $Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $allowed = $false
    foreach ($root in @($buildRoot, $stageRoot)) {
        if ($resolved.Equals($root, [StringComparison]::OrdinalIgnoreCase) -or
            $resolved.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
            $allowed = $true
        }
    }
    foreach ($file in @($archivePath, $stageManifestPath, $stageBuildPath)) {
        if ($resolved.Equals($file, [StringComparison]::OrdinalIgnoreCase)) { $allowed = $true }
    }
    if (-not $allowed) { throw "Refusing generated-file operation outside $productName paths: $resolved" }
    # A lexical child must not be redirected outside the workspace by a junction.
    $ancestor = $resolved
    while ($ancestor.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $ancestor) {
            if (((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing generated-file operation through a reparse point: $ancestor"
            }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}

function New-GeneratedDirectory([string] $Path) {
    Assert-GeneratedPath $Path
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Get-Sha256([string] $Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Get-FileRecord([string] $Path, [string] $Name) {
    Assert-File $Path
    return [ordered]@{ file = $Name; bytes = (Get-Item -LiteralPath $Path).Length; sha256 = Get-Sha256 $Path }
}

function Write-Json([string] $Path, $Value) {
    Assert-GeneratedPath $Path
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 10) + [Environment]::NewLine, $utf8NoBom)
}

function Assert-ExpectedFiles([string] $Root, [string[]] $Expected) {
    Assert-GeneratedPath $Root
    if (-not (Test-Path -LiteralPath $Root)) { return }
    foreach ($entry in Get-ChildItem -LiteralPath $Root -Recurse -Force) {
        Assert-GeneratedPath $entry.FullName
        if (-not $entry.PSIsContainer) {
            $relative = $entry.FullName.Substring($Root.Length + 1).Replace('\', '/')
            if ($Expected -notcontains $relative) {
                throw "Unexpected file in the isolated probe directory; inspect it before rebuilding: $($entry.FullName)"
            }
        }
    }
}

foreach ($path in @($sourcePath, $compiler, $flags, (Join-Path $PSScriptRoot '../../tests/build/Test-F4SECurrentReference.ps1'))) {
    Assert-File $path
}
if (-not $CheckOnly) {
    Assert-File $readmePath
    Assert-File $licensePath
}
$sourceText = [IO.File]::ReadAllText($sourcePath)
if ($sourceText -notmatch ('(?im)^\s*ScriptName\s+' + [regex]::Escape($scriptName) + '(?:\s|$)')) {
    throw "The diagnostic source must declare ScriptName $scriptName."
}
$f4seReference = & (Join-Path $PSScriptRoot '../../tests/build/Test-F4SECurrentReference.ps1') -AsJson | ConvertFrom-Json
if ($f4seReference.result -ne 'passed') { throw 'The pinned official F4SE 0.7.9 reference did not verify.' }
$vendorScripts = [string] $f4seReference.scriptRoot
$vanillaFiles = @(Get-ChildItem -LiteralPath (Join-Path $vendorScripts 'vanilla') -File -Filter '*.psc')
$fragments = @(Get-ChildItem -LiteralPath (Join-Path $vendorScripts 'modified') -File -Filter '*.psc')
$expectedImports = @(@($vanillaFiles.Name) + @($fragments.Name) | Sort-Object -Unique)
Assert-ExpectedFiles $mergedImport $expectedImports
New-GeneratedDirectory $mergedImport
foreach ($file in $vanillaFiles) {
    $destination = Join-Path $mergedImport $file.Name
    Assert-GeneratedPath $destination
    Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
}
foreach ($fragment in $fragments) {
    $destination = Join-Path $mergedImport $fragment.Name
    Assert-GeneratedPath $destination
    $vanillaPath = Join-Path (Join-Path $vendorScripts 'vanilla') $fragment.Name
    if (Test-Path -LiteralPath $vanillaPath -PathType Leaf) {
        $text = [IO.File]::ReadAllText($vanillaPath).TrimEnd() + [Environment]::NewLine + [IO.File]::ReadAllText($fragment.FullName)
        [IO.File]::WriteAllText($destination, $text, $utf8NoBom)
    }
    else { Copy-Item -LiteralPath $fragment.FullName -Destination $destination -Force }
}
foreach ($name in @('F4SE.psc', 'ScriptObject.psc', 'Form.psc', 'Game.psc', 'ObjectReference.psc')) {
    Assert-File (Join-Path $mergedImport $name)
}
Assert-ExpectedFiles $outputRoot @($scriptName + '.pex')
New-GeneratedDirectory $outputRoot
New-GeneratedDirectory $logRoot
$imports = New-Object 'Collections.Generic.List[string]'
$imports.Add($sourceRoot)
if ($Probe -eq 'Power') {
    $clipboardImports = Join-Path $projectRoot 'src\papyrus'
    Assert-File (Join-Path $clipboardImports 'ClipboardExtension.psc')
    $imports.Add($clipboardImports)
}
$imports.Add($mergedImport)
foreach ($name in @('User', 'Base', 'DLC01', 'DLC02', 'DLC03', 'DLC04', 'DLC05', 'DLC06')) {
    $candidate = Join-Path $gameSources $name
    if (Test-Path -LiteralPath $candidate -PathType Container) { $imports.Add($candidate) }
}
$arguments = @($sourcePath, "-f=$flags", ('-i=' + ($imports -join ';')), "-o=$outputRoot", '-ignorecwd', '-quiet')
$manifest = [ordered]@{
    schemaVersion = 1
    builtUtc = (Get-Date).ToUniversalTime().ToString('o')
    product = $productName
    result = 'pending'
    checkOnly = [bool] $CheckOnly
    runtimeCertification = 'pending; compilation and packaging do not certify in-game behavior'
    f4seVersion = '0.7.9'
    f4seReference = $f4seReference
    compiler = [ordered]@{ path = $compiler; version = (Get-Item -LiteralPath $compiler).VersionInfo.FileVersion; sha256 = Get-Sha256 $compiler }
    flags = Get-FileRecord $flags $flags
    source = Get-FileRecord $sourcePath ('tools/diagnostics/papyrus/' + $scriptName + '.psc')
    buildHelper = Get-FileRecord $PSCommandPath 'tools/diagnostics/Build-ObjectProbe.ps1'
    imports = @()
    compilerRuns = @()
    outputs = @()
}
# Capture every available PSC in ordered import roots, including duplicates.
# This describes compiler inputs, not a claim that every listed script was used.
foreach ($root in $imports) {
    $manifest.imports += [ordered]@{
        path = $root
        files = @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.psc' | Sort-Object FullName | ForEach-Object {
            Get-FileRecord $_.FullName ($_.FullName.Substring($root.Length + 1).Replace('\', '/'))
        })
    }
}
$operations = if ($CheckOnly) { @('check') } else { @('check', 'build') }
foreach ($operation in $operations) {
    $runArguments = $arguments
    if ($operation -eq 'check') { $runArguments += '-noasm' }
    elseif (Test-Path -LiteralPath $outputPath) {
        # Remove this one known generated output so success cannot reuse a stale PEX.
        Assert-GeneratedPath $outputPath
        Remove-Item -LiteralPath $outputPath -Force
    }
    $logPath = Join-Path $logRoot ($runStamp + '-' + $operation + '.log')
    Assert-GeneratedPath $logPath
    $compilerOutput = @(& $compiler @runArguments 2>&1)
    $exitCode = $LASTEXITCODE
    $lines = @("operation=$operation", "compiler=$compiler", "source=$sourcePath", "imports=$($imports -join ';')", "exitCode=$exitCode") + $compilerOutput
    [IO.File]::WriteAllLines($logPath, [string[]] $lines, $utf8NoBom)
    $compilerOutput | ForEach-Object { Write-Output $_ }
    $manifest.compilerRuns += [ordered]@{ operation = $operation; arguments = $runArguments; exitCode = $exitCode; log = Get-FileRecord $logPath $logPath }
    if ($exitCode -ne 0) {
        $manifest.result = 'failed'
        Write-Json (Join-Path $buildRoot ('verification-' + $runStamp + '.json')) $manifest
        throw "$productName Papyrus $operation failed with exit code $exitCode. See $logPath"
    }
}
if ((Get-Sha256 $sourcePath) -cne $manifest.source.sha256 -or
    (Get-Sha256 $PSCommandPath) -cne $manifest.buildHelper.sha256) {
    throw 'The probe source or build helper changed during compilation; rerun before staging.'
}
if ($CheckOnly) {
    $manifest.result = 'check-passed'
    Write-Json (Join-Path $buildRoot 'check.json') $manifest
    Write-Output "$productName compiler check passed. Logs: $logRoot"
    return
}
Assert-File $outputPath
if ((Get-Item -LiteralPath $outputPath).Length -eq 0) { throw 'The compiler produced an empty probe PEX.' }
$manifest.outputs = @(Get-FileRecord $outputPath ('Scripts/' + $scriptName + '.pex'))
$payload = [ordered]@{
    ('Scripts/' + $scriptName + '.pex') = $outputPath
    ('Scripts/Source/User/' + $scriptName + '.psc') = $sourcePath
    'README.md' = $readmePath
    'LICENSE.txt' = $licensePath
    'Source/tools/diagnostics/Build-ObjectProbe.ps1' = $PSCommandPath
    'Source/tools/ClipboardBuildPaths.ps1' = Join-Path $PSScriptRoot '../build/ClipboardBuildPaths.ps1'
}
Assert-ExpectedFiles $stageRoot @($payload.Keys)
New-GeneratedDirectory $stageRoot
$packageFiles = @()
foreach ($relative in $payload.Keys) {
    $destination = Join-Path $stageRoot $relative
    Assert-GeneratedPath $destination
    New-GeneratedDirectory ([IO.Path]::GetDirectoryName($destination))
    Copy-Item -LiteralPath $payload[$relative] -Destination $destination -Force
    $record = Get-FileRecord $destination $relative
    if ($record.sha256 -cne (Get-Sha256 $payload[$relative])) { throw "Staged file hash mismatch: $relative" }
    $packageFiles += $record
}
Assert-GeneratedPath $stageManifestPath
[IO.File]::WriteAllLines($stageManifestPath, [string[]] @($packageFiles | ForEach-Object { "$($_.sha256)  $($_.file)" }), $utf8NoBom)
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporaryArchive = Join-Path $buildRoot ('package-' + $runStamp + '.zip')
Assert-GeneratedPath $temporaryArchive
$archive = [IO.Compression.ZipFile]::Open($temporaryArchive, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($relative in $payload.Keys) {
        $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Join-Path $stageRoot $relative), $relative, [IO.Compression.CompressionLevel]::Optimal)
    }
}
finally { $archive.Dispose() }
$archive = [IO.Compression.ZipFile]::OpenRead($temporaryArchive)
try {
    if ($archive.Entries.Count -ne $payload.Count) { throw 'Probe ZIP has an unexpected entry count.' }
    foreach ($record in $packageFiles) {
        $entry = $archive.GetEntry($record.file)
        if (-not $entry -or $entry.Length -ne $record.bytes) { throw "Probe ZIP entry mismatch: $($record.file)" }
        $stream = $entry.Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
        finally { $sha.Dispose(); $stream.Dispose() }
        if ($hash -cne $record.sha256) { throw "Probe ZIP hash mismatch: $($record.file)" }
    }
}
finally { $archive.Dispose() }
Assert-GeneratedPath $archivePath
Move-Item -LiteralPath $temporaryArchive -Destination $archivePath -Force
$manifest.result = 'passed'
$manifest.package = [ordered]@{ path = $stageRoot; files = $packageFiles; hashManifest = Get-FileRecord $stageManifestPath $stageManifestPath; archive = Get-FileRecord $archivePath $archivePath }
Write-Json (Join-Path $buildRoot 'verification.json') $manifest
Write-Json (Join-Path $buildRoot ('verification-' + $runStamp + '.json')) $manifest
Write-Json $stageBuildPath $manifest
Write-Output "$productName compiler check, compilation and five-file package/ZIP verification passed."
Write-Output "Package: $stageRoot"
Write-Output "Archive: $archivePath"
Write-Output "Evidence: $(Join-Path $buildRoot 'verification.json')"
