# SPDX-License-Identifier: GPL-3.0-or-later
#Requires -Version 7.0
<#
.SYNOPSIS
Reports repository cleanup status; -Apply retains completed output cases and cleans build output.
.DESCRIPTION
See docs/development/CLEANUP.md. No publication or game access.
PlanPath and AuditPath are workspace-relative paths using forward slashes.
The default audit is the completed October 3 pass. It returns already-clean only
after verifying its plan, deletion journal and retained runtime database/index.
Routine -Apply inventories outputs, archives eligible completed cases and invokes
verified build-folder cleanup. No manual file moves or prepared plan are needed.
The older historical deletion workflow remains available with explicit plan/hash.
#>
[CmdletBinding()]
param(
    [string] $PlanPath = ('.local/cleanup-plans/' + [guid]::NewGuid().ToString('N') + '.json'),
    [string] $AuditPath = '.local/cleanup-audit/2026-10-03',
    [switch] $Apply,
    [string] $ReviewedPlanSha256,
    [string] $PythonPath,
    [string] $RestoreOutputCase,
    [string] $CompleteOutputCase,
    [string] $KeepOutputCase
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$python = & (Join-Path $PSScriptRoot '../build/Get-ClipboardPython.ps1') -PythonPath $PythonPath
$caseActions = @(@{parameter='RestoreOutputCase';option='--restore';value=$RestoreOutputCase},
                 @{parameter='CompleteOutputCase';option='--complete';value=$CompleteOutputCase},
                 @{parameter='KeepOutputCase';option='--keep';value=$KeepOutputCase}) |
    Where-Object { $PSBoundParameters.ContainsKey($_.parameter) }
if (@($caseActions).Count) {
    if (@($caseActions).Count -ne 1 -or $Apply -or $PSBoundParameters.ContainsKey('PlanPath') -or
        $PSBoundParameters.ContainsKey('ReviewedPlanSha256') -or [string]::IsNullOrWhiteSpace($caseActions.value)) {
        throw 'Use one nonempty output-case action without -Apply or a historical plan.'
    }
    & $python -B (Join-Path $PSScriptRoot 'outputs_retention.py') --root $root $caseActions.option $caseActions.value
    if ($LASTEXITCODE -ne 0) { throw 'Output case action failed; see details above.' }
    return
}
$arguments = @('-B', (Join-Path $PSScriptRoot 'cleanup_repository.py'), '--root', $root, '--audit', $AuditPath, '--plan', $PlanPath)
$hasReviewedHash = $PSBoundParameters.ContainsKey('ReviewedPlanSha256')
$historicalApply = $Apply -and ($PSBoundParameters.ContainsKey('PlanPath') -or $hasReviewedHash)
if ($historicalApply) {
    if (-not $PSBoundParameters.ContainsKey('PlanPath') -or $ReviewedPlanSha256 -notmatch '^[0-9A-Fa-f]{64}$') {
        throw 'Historical -Apply requires both an explicit -PlanPath and -ReviewedPlanSha256.'
    }
    $arguments += @('--apply', '--reviewed-sha256', $ReviewedPlanSha256)
} elseif ($hasReviewedHash) { throw '-ReviewedPlanSha256 requires -Apply.' }
$output = & $python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Cleanup refused; see the validation error above.' }
$historical = ($output -join [Environment]::NewLine) | ConvertFrom-Json
if ($historicalApply) {
    $historical | ConvertTo-Json -Depth 10
    return
}

$outputArguments = @('-B', (Join-Path $PSScriptRoot 'outputs_retention.py'), '--root', $root)
if ($Apply) { $outputArguments += '--apply' }
$outputStatus = & $python @outputArguments
if ($LASTEXITCODE -ne 0) { throw 'Outputs retention failed; verified archives and journals are retained.' }
$outputs = ($outputStatus -join [Environment]::NewLine) | ConvertFrom-Json
$build = & (Join-Path $PSScriptRoot 'Clean-ClipboardBuild.ps1') -Apply:$Apply
$result = if ($build.result -eq 'cleaned' -or $outputs.result -eq 'cleaned') { 'cleaned' }
          elseif ($outputs.eligibleCases -gt 0 -or $historical.result -eq 'dry-run' -or $build.result -eq 'dry-run') { 'dry-run' }
          elseif ($outputs.totalBytes -gt 0) { 'retained' }
          else { 'already-clean' }
[pscustomobject]@{
    result = $result
    historical = $historical
    outputs = $outputs
    build = $build
} | ConvertTo-Json -Depth 10
