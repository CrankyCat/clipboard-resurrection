# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2018 to 2026 Everett C Sands
<#
.SYNOPSIS
Capture one full access-violation dump while reproducing selected-object Destroy.
.DESCRIPTION
Run after loading the test save, immediately before Destroy. Attaches to exactly
one running Fallout4 process. Uses a first-chance C0000005 filter because an
installed crash handler can intercept the final unhandled exception. No game or
mod files are written. No global postmortem debugger is installed. Ctrl+C stops
monitoring. CheckOnly validates paths and prints the capture plan without attaching.
#>
[CmdletBinding()]
param(
    [int]$GameProcessId = 0,
    [string]$ProcDumpPath,
    [switch]$CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../build/ClipboardBuildPaths.ps1')
if (-not $ProcDumpPath) { $ProcDumpPath = Find-ClipboardApplication 'procdump64.exe' }
$ProcDumpPath = Resolve-ClipboardInputPath $ProcDumpPath 'ProcDumpPath'
$projectRoot = Split-Path -Parent $PSScriptRoot
$captureRoot = Join-Path $projectRoot 'outputs\destroy-crash-captures'
$runDirectory = Join-Path $captureRoot ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
if (-not (Test-Path -LiteralPath $ProcDumpPath -PathType Leaf)) {
    throw "ProcDump not found: $ProcDumpPath"
}
$procDump = (Resolve-Path -LiteralPath $ProcDumpPath).Path
$captureArguments = @('-accepteula', '-ma', '-e', '1', '-f', 'C0000005', '-n', '1')
if ($CheckOnly) {
    [pscustomobject]@{
        ProcDump = $procDump
        ProcDumpVersion = (Get-Item -LiteralPath $procDump).VersionInfo.FileVersion
        Arguments = $captureArguments
        Target = 'One running Fallout4.exe; selected by PID or unambiguous process name'
        Output = $runDirectory
        Attaches = $false
    }
    return
}

if ($GameProcessId -gt 0) {
    $gameProcess = Get-Process -Id $GameProcessId
    if ($gameProcess.ProcessName -ne 'Fallout4') { throw 'The selected PID is not Fallout4.exe.' }
} else {
    $gameProcesses = @(Get-Process -Name Fallout4 -ErrorAction SilentlyContinue)
    if ($gameProcesses.Count -ne 1) {
        throw 'Load the Fallout 4 test save first. If multiple instances exist, specify -GameProcessId.'
    }
    $gameProcess = $gameProcesses[0]
}

New-Item -ItemType Directory -Path $runDirectory | Out-Null
$modules = @($gameProcess.Modules | ForEach-Object {
    $modulePath = $_.FileName
    $entry = [ordered]@{
        name = $_.ModuleName
        path = $modulePath
        baseAddress = ('0x{0:X}' -f $_.BaseAddress.ToInt64())
        size = $_.ModuleMemorySize
        fileVersion = $_.FileVersionInfo.FileVersion
    }
    if ($_.ModuleName -match '^(Fallout4\.exe|clipboard\.dll|f4se_.*\.dll)$') {
        try { $entry['diskSha256'] = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash }
        catch { $entry['hashError'] = $_.Exception.Message }
    }
    [pscustomobject]$entry
})
$record = [ordered]@{
    startedUtc = [DateTime]::UtcNow.ToString('o')
    gameProcessId = $gameProcess.Id
    processStartTime = $gameProcess.StartTime.ToUniversalTime().ToString('o')
    procDumpPath = $procDump
    procDumpArguments = $captureArguments
    modules = $modules
    identityNote = 'Disk hashes describe files visible through module paths; dump memory is the loaded-state evidence.'
}
$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runDirectory 'capture.json') -Encoding UTF8
Write-Host "Capturing one full access-violation dump to: $runDirectory"
Write-Host 'Wait for ProcDump to report monitoring, then repeat Destroy. Keep the same DLL, scripts, save and load order.'
Write-Host 'The game may pause during dump writing. Ctrl+C stops monitoring; the script does not terminate the game.'
& $procDump @captureArguments $gameProcess.Id $runDirectory
$captureExitCode = $LASTEXITCODE
$record['endedUtc'] = [DateTime]::UtcNow.ToString('o')
$record['procDumpExitCode'] = $captureExitCode
$record['dumps'] = @(Get-ChildItem -LiteralPath $runDirectory -Filter '*.dmp' -File | Select-Object Name,Length)
$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runDirectory 'capture.json') -Encoding UTF8
Write-Host "Capture result: $($record['dumps'].Count) dump(s), ProcDump exit $captureExitCode. Preserve the matching Clipboard/Papyrus/crash logs."
if ($captureExitCode -ne 0 -and $record['dumps'].Count -eq 0) {
    throw "ProcDump exited with code $captureExitCode without a dump. See its output above."
}
# ProcDump can return 1 after writing an unhandled fast-fail dump. Keep the
# exit code as evidence; file validity is established by opening it in WinDbg.
