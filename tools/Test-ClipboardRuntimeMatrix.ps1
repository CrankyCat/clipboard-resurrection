<#
.SYNOPSIS
Checks Clipboard's actual Runtime Database tuples against preserved game images.

.DESCRIPTION
Runs the linked host inspector in a fresh process for each executable. The game
PE is mapped with DONT_RESOLVE_DLL_REFERENCES: no game code or F4SE load callback
is executed. A separate DLL initializer smoke check loads only clipboard.dll in
an empty host directory. These are static/host checks, not runtime certification.
The database is copied to a local inspection directory, never to a deployment.
#>

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2018 to 2026 Everett C Sands
#Requires -Version 5.1

[CmdletBinding()]
param(
    [string] $InspectorPath,
    [string] $DllPath,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string] $RuntimeDatabasePath,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string[]] $ExecutablePath,
    [string[]] $ExpectedRejectedExecutablePath,
    [string] $OutputDirectory,
    [ValidateRange(10, 1800)] [int] $ProcessTimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $InspectorPath) { $InspectorPath = Join-Path $PSScriptRoot '..\build\native\vs2026\Release\ClipboardRuntimeCompatibilityTests.exe' }
if (-not $DllPath) { $DllPath = Join-Path $PSScriptRoot '..\build\native\vs2026\Release\clipboard.dll' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot '..\outputs\unified-runtime\matrix' }
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $outputRoot.StartsWith($projectRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Inspection output must stay inside this project.'
}
$immutableRoot = Join-Path $projectRoot 'Original Project Files'
if ($outputRoot.Equals($immutableRoot, [StringComparison]::OrdinalIgnoreCase) -or
    $outputRoot.StartsWith($immutableRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Inspection output cannot be written into immutable Original Project Files.'
}

function Resolve-InputFile([string] $Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) { throw "Input file missing: $fullPath" }
    return $fullPath
}
function Get-Sha256([string] $Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}
function Write-Json([string] $Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 15) + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}
function Invoke-Inspector([string] $Mode, [string] $InputPath, [string] $WorkingDirectory) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $inspector
    # Existing Windows paths cannot contain a quotation mark. No shell is used.
    $start.Arguments = $Mode + ' "' + $InputPath + '"'
    $start.WorkingDirectory = $WorkingDirectory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $timer = [Diagnostics.Stopwatch]::StartNew()
    try {
        if (-not $process.Start()) { throw 'Inspector could not be started.' }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($ProcessTimeoutSeconds * 1000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "Inspector timed out after $ProcessTimeoutSeconds seconds for $InputPath"
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        try { $result = $stdout | ConvertFrom-Json }
        catch { throw "Inspector returned invalid JSON (exit $($process.ExitCode)): $stdout $stderr" }
        return [ordered]@{
            exitCode = $process.ExitCode
            elapsedSeconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
            stderr = $stderr
            result = $result
        }
    }
    finally { $timer.Stop(); $process.Dispose() }
}

$inspector = Resolve-InputFile $InspectorPath
$dll = Resolve-InputFile $DllPath
$database = Resolve-InputFile $RuntimeDatabasePath
$fixtures = @($ExecutablePath | ForEach-Object { Resolve-InputFile $_ })
$rejectedFixtures = @($ExpectedRejectedExecutablePath | Where-Object { $_ } | ForEach-Object { Resolve-InputFile $_ })
$inputRecords = @($inspector, $dll, $database) + $fixtures + $rejectedFixtures + @(
    (Join-Path $projectRoot 'native\src\EngineSymbols.h'),
    (Join-Path $projectRoot 'native\src\RuntimeSymbols.h'),
    (Join-Path $projectRoot 'native\src\RuntimeCompatibility.h'),
    (Join-Path $projectRoot 'native\tests\RuntimeCompatibilityTests.cpp'),
    (Join-Path $projectRoot 'native\CommonLibF4RD.snapshot.json'),
    $PSCommandPath
) | ForEach-Object { [ordered]@{ path = $_; sha256 = Get-Sha256 $_ } }

$offlineRoot = Join-Path $outputRoot 'offline-root'
$databaseDirectory = Join-Path $offlineRoot 'Data\F4SE\Plugins'
$emptyRoot = Join-Path $outputRoot 'initializer-host'
New-Item -ItemType Directory -Path $databaseDirectory, $emptyRoot -Force | Out-Null
if (@(Get-ChildItem -LiteralPath $emptyRoot -Force).Count -ne 0) {
    throw 'Initializer host directory must be empty; select a new output directory.'
}
$databaseCopy = Join-Path $databaseDirectory 'f4rd-runtime.bin'
$databaseHash = Get-Sha256 $database
if (-not (Test-Path -LiteralPath $databaseCopy -PathType Leaf) -or (Get-Sha256 $databaseCopy) -ne $databaseHash) {
    Copy-Item -LiteralPath $database -Destination $databaseCopy -Force
}
if ((Get-Sha256 $databaseCopy) -ne $databaseHash) { throw 'Inspection database copy hash mismatch.' }

$startup = Invoke-Inspector '--load-plugin' $dll $emptyRoot
Write-Json (Join-Path $outputRoot 'dll-initializers.json') $startup
if ($startup.exitCode -ne 0 -or -not $startup.result.passed) {
    throw "DLL initializer smoke check failed; see $outputRoot\dll-initializers.json"
}

$runs = @()
$failed = 0
$fixtureIndex = 0
foreach ($fixture in $fixtures) {
    Write-Host "Inspecting Runtime Database resolution: $([IO.Path]::GetFileName($fixture))"
    $run = Invoke-Inspector '--inspect' $fixture $offlineRoot
    $reportName = '{0:D2}-{1}.json' -f $fixtureIndex, [IO.Path]::GetFileNameWithoutExtension($fixture)
    ++$fixtureIndex
    Write-Json (Join-Path $outputRoot $reportName) $run
    if ($run.exitCode -ne 0) { ++$failed }
    $runs += [ordered]@{ executable = $fixture; report = $reportName; exitCode = $run.exitCode; result = $run.result }
}
$rejectionRuns = @()
foreach ($fixture in $rejectedFixtures) {
    Write-Host "Checking expected resolution rejection: $([IO.Path]::GetFileName($fixture))"
    $run = Invoke-Inspector '--inspect' $fixture $offlineRoot
    $reportName = '{0:D2}-expected-rejection-{1}.json' -f $fixtureIndex, [IO.Path]::GetFileNameWithoutExtension($fixture)
    ++$fixtureIndex
    Write-Json (Join-Path $outputRoot $reportName) $run
    # A crash, timeout, mapping error or missing database is not the intended
    # capability rejection. Require a completed resolver report with failures.
    $passed = $run.exitCode -eq 2 -and $run.result.mode -eq 'offline-mapped-pe' -and $run.result.failed -gt 0
    if (-not $passed) { ++$failed }
    $rejectionRuns += [ordered]@{ executable = $fixture; report = $reportName; passed = $passed; exitCode = $run.exitCode; result = $run.result }
}
foreach ($record in $inputRecords) {
    if ((Get-Sha256 $record.path) -ne $record.sha256) { throw "Inspection input changed during checks: $($record.path)" }
}
$summary = [ordered]@{
    schemaVersion = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString('o')
    passed = ($failed -eq 0)
    validation = 'Offline original CommonLib resolver against mapped PE images; no engine function or F4SE callback executed.'
    liveRuntimeCertified = $false
    inputs = $inputRecords
    dllInitializerSmoke = $startup
    fixtureCount = $runs.Count
    expectedRejectionCount = $rejectionRuns.Count
    failedFixtures = $failed
    runs = $runs
    expectedRejections = $rejectionRuns
}
Write-Json (Join-Path $outputRoot 'summary.json') $summary
if ($failed -ne 0) { throw "$failed runtime fixture(s) failed; see $outputRoot\summary.json" }
Write-Host "PASS: DLL initializers, $($runs.Count) offline runtime fixtures, $($rejectionRuns.Count) expected rejections. Report: $outputRoot\summary.json"
