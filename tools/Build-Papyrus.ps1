<#
.SYNOPSIS
Validates or compiles Clipboard's Fallout 4 Papyrus sources.

.DESCRIPTION
Builds only the 16 maintained Clipboard scripts. Modern079 defaults to the
verified official F4SE 0.7.9 imports; Legacy221 preserves the separate 0.7.8
reproduction inputs and output paths. A generated import directory merges the
vanilla sources with the append-only modified fragments before game Base/DLC
imports because Clipboard calls F4SE additions to ScriptObject, Form, and Game.
#>

#Requires -Version 5.1

[CmdletBinding()]
param(
    [switch] $CheckOnly,
    [ValidateSet('Modern079', 'Legacy221')]
    [string] $Target = 'Modern079',
    [string] $GameRoot
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'ClipboardBuildPaths.ps1')
$GameRoot = Resolve-ClipboardGameRoot $GameRoot $projectRoot
$sourceRoot = Join-Path $projectRoot 'Scripts\Source\User'
$f4seReference = $null
$sourceSnapshot = $null
$internalBuild = $null
if ($Target -eq 'Modern079') {
    . (Join-Path $PSScriptRoot 'ClipboardBuildIdentity.ps1')
    $internalBuild = Get-ClipboardInternalBuild $projectRoot -CheckScripts
    $referenceVerifier = Join-Path $PSScriptRoot 'Test-F4SECurrentReference.ps1'
    $f4seReference = (& $referenceVerifier -AsJson | ConvertFrom-Json)
    if ($f4seReference.result -ne 'passed') { throw 'The official F4SE 0.7.9 reference did not verify.' }
    $vendorScripts = [string] $f4seReference.scriptRoot
    $buildLabel = 'v240'
    $f4seVersion = '0.7.9'
    $runtimeVersion = '1.11.240.0'
    $sourceTarget = 'Fallout 4 1.11.240 baseline'
}
else {
	$sourceSnapshot = (& (Join-Path $PSScriptRoot 'Test-LegacyPapyrusSnapshot.ps1') -AsJson | ConvertFrom-Json)
	if ($sourceSnapshot.result -ne 'passed') { throw 'The preserved legacy Papyrus source snapshot did not verify.' }
	$sourceRoot = [string] $sourceSnapshot.sourceRoot
    $vendorScripts = Join-Path $projectRoot 'legacy\v221\vendor\f4se-0.7.8\scripts'
    $buildLabel = 'v221'
    $f4seVersion = '0.7.8'
    $runtimeVersion = '1.11.221.0'
    $sourceTarget = 'Fallout 4 1.11.221 baseline'
}
$buildRoot = Join-Path $projectRoot "build\papyrus\$buildLabel"
$mergedImport = Join-Path $buildRoot "imports\f4se-$f4seVersion-merged"
$outputRoot = Join-Path $buildRoot 'Scripts'
$logRoot = Join-Path $projectRoot 'build\logs'
$compiler = Join-Path $GameRoot 'Papyrus Compiler\PapyrusCompiler.exe'
$gameSources = Join-Path $GameRoot 'Data\Scripts\Source'
$flags = Join-Path $gameSources 'Base\Institute_Papyrus_Flags.flg'

function Assert-File([string] $Path, [string] $Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found: $Path"
    }
}

function Assert-Directory([string] $Path, [string] $Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description was not found: $Path"
    }
}

function Assert-GeneratedPath([string] $Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build')) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing generated-file operation outside the project build directory: $resolved"
    }
}

Assert-Directory $sourceRoot 'Clipboard Papyrus source root'
Assert-Directory (Join-Path $vendorScripts 'vanilla') 'Selected F4SE vanilla source root'
Assert-Directory (Join-Path $vendorScripts 'modified') 'Selected F4SE modified source root'
Assert-File $compiler 'Fallout 4 Papyrus compiler'
Assert-File $flags 'Fallout 4 Papyrus flags file'

$sources = @(Get-ChildItem -LiteralPath $sourceRoot -File -Filter '*.psc' | Sort-Object Name)
if ($sources.Count -ne 16) {
    throw "Expected exactly 16 Clipboard Papyrus sources; found $($sources.Count)."
}

foreach ($source in $sources) {
    $scriptName = $null
    foreach ($line in Get-Content -LiteralPath $source.FullName) {
        if ($line -match '^\s*ScriptName\s+([^\s]+)') {
            $scriptName = $Matches[1]
            break
        }
    }
    if ([string]::IsNullOrWhiteSpace($scriptName)) {
        throw "No ScriptName declaration was found: $($source.FullName)"
    }
    if (-not [string]::Equals($scriptName, $source.BaseName, [StringComparison]::OrdinalIgnoreCase)) {
        throw "ScriptName '$scriptName' does not match filename '$($source.Name)'."
    }
}

Assert-GeneratedPath $mergedImport
if (Test-Path -LiteralPath $mergedImport) {
    Remove-Item -LiteralPath $mergedImport -Recurse -Force
}
New-Item -ItemType Directory -Path $mergedImport -Force | Out-Null

Get-ChildItem -LiteralPath (Join-Path $vendorScripts 'vanilla') -File -Filter '*.psc' |
    Copy-Item -Destination $mergedImport

$utf8NoBom = New-Object Text.UTF8Encoding($false)
foreach ($fragment in Get-ChildItem -LiteralPath (Join-Path $vendorScripts 'modified') -File -Filter '*.psc') {
    $mergedFile = Join-Path $mergedImport $fragment.Name
    if (Test-Path -LiteralPath $mergedFile -PathType Leaf) {
        $baseText = [IO.File]::ReadAllText($mergedFile)
        $fragmentText = [IO.File]::ReadAllText($fragment.FullName)
        [IO.File]::WriteAllText($mergedFile, $baseText.TrimEnd() + [Environment]::NewLine + $fragmentText, $utf8NoBom)
    }
    else {
        Copy-Item -LiteralPath $fragment.FullName -Destination $mergedFile
    }
}

foreach ($required in 'F4SE.psc', 'ScriptObject.psc', 'Form.psc', 'Game.psc') {
    Assert-File (Join-Path $mergedImport $required) "Merged F4SE dependency $required"
}

if (-not $CheckOnly) {
    if ($Target -eq 'Modern079' -and (Get-ClipboardInternalBuild $projectRoot -CheckScripts) -ne $internalBuild) {
        throw 'Internal build changed during Papyrus compilation.'
    }
    Assert-GeneratedPath $outputRoot
    if (Test-Path -LiteralPath $outputRoot) {
        Remove-Item -LiteralPath $outputRoot -Recurse -Force
    }
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null

$imports = New-Object 'Collections.Generic.List[string]'
$imports.Add($sourceRoot)
$imports.Add($mergedImport)
foreach ($candidate in @(
    (Join-Path $gameSources 'User'),
    (Join-Path $gameSources 'Base'),
    (Join-Path $gameSources 'DLC01'),
    (Join-Path $gameSources 'DLC02'),
    (Join-Path $gameSources 'DLC03'),
    (Join-Path $gameSources 'DLC04'),
    (Join-Path $gameSources 'DLC05'),
    (Join-Path $gameSources 'DLC06')
)) {
    if (Test-Path -LiteralPath $candidate -PathType Container) {
        $imports.Add($candidate)
    }
}

$arguments = @(
    $sourceRoot,
    '-all',
    "-f=$flags",
    ('-i=' + ($imports -join ';')),
    "-o=$outputRoot",
    '-ignorecwd',
    '-quiet'
)
if ($CheckOnly) {
    $arguments += '-noasm'
}

$operation = if ($CheckOnly) { 'check' } else { 'build' }
$logPath = Join-Path $logRoot "papyrus-$buildLabel-$operation.log"
$compilerOutput = @(& $compiler @arguments 2>&1)
$exitCode = $LASTEXITCODE
$logLines = @(
    "operation=$operation"
    "buildTarget=$Target"
    "f4seVersion=$f4seVersion"
    "f4seSourceRoot=$vendorScripts"
    "compiler=$compiler"
    "compilerVersion=$((Get-Item -LiteralPath $compiler).VersionInfo.FileVersion)"
    "sourceRoot=$sourceRoot"
    "outputRoot=$outputRoot"
    "imports=$($imports -join ';')"
    "checkOnly=$([bool] $CheckOnly)"
    "exitCode=$exitCode"
) + $compilerOutput
[IO.File]::WriteAllLines($logPath, [string[]] $logLines, $utf8NoBom)
$compilerOutput | ForEach-Object { Write-Output $_ }
if ($exitCode -ne 0) {
    throw "Papyrus $operation failed with exit code $exitCode. See $logPath"
}

if (-not $CheckOnly) {
    $missing = @($sources | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $outputRoot ($_.BaseName + '.pex')) -PathType Leaf)
    })
    $outputs = @(Get-ChildItem -LiteralPath $outputRoot -File -Filter '*.pex')
    if ($missing.Count -gt 0 -or $outputs.Count -ne 16) {
        throw "Papyrus compiler returned success, but the expected 16 PEX outputs were not produced."
    }

    $manifest = [ordered]@{
        schemaVersion = 2
        internalBuild = $internalBuild
        builtUtc = (Get-Date).ToUniversalTime().ToString('o')
        result = 'success'
        compilerExitCode = $exitCode
        buildTarget = $Target
        target = $sourceTarget
        fallout4Runtime = $runtimeVersion
        f4seVersion = $f4seVersion
        f4seSourceRoot = $vendorScripts
        f4seReference = $f4seReference
        sourceRoot = $sourceRoot
        sourceSnapshot = $sourceSnapshot
        compiler = $compiler
        compilerVersion = (Get-Item -LiteralPath $compiler).VersionInfo.FileVersion
        compilerSha256 = (Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash
        flagsSha256 = (Get-FileHash -LiteralPath $flags -Algorithm SHA256).Hash
        sourceCount = $sources.Count
        mergedF4seSourceCount = (Get-ChildItem -LiteralPath $mergedImport -File -Filter '*.psc').Count
        sourceFiles = @($sources | ForEach-Object {
            [ordered]@{ file = $_.Name; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
        })
        mergedF4seSources = @(Get-ChildItem -LiteralPath $mergedImport -File -Filter '*.psc' | Sort-Object Name | ForEach-Object {
            [ordered]@{ file = $_.Name; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
        })
        outputs = @($outputs | Sort-Object Name | ForEach-Object {
            [ordered]@{ file = $_.Name; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
        })
    }
    $manifestPath = Join-Path $buildRoot 'papyrus-build.json'
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 5) + [Environment]::NewLine, $utf8NoBom)
}

Write-Output ("Papyrus {0} succeeded for 16 Clipboard scripts ({1}, F4SE {2})." -f $operation, $Target, $f4seVersion)
Write-Output "Log: $logPath"
if (-not $CheckOnly) {
    Write-Output "Output: $outputRoot"
}
