# Tests publication policy against a disposable checkout fixture, never Deployments.
#Requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ClipboardBuildIdentity.ps1')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixture = Join-Path $root ('build/build-identity-tests/' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path "$fixture/tools", "$fixture/Scripts/Source/User", "$fixture/Deployments" -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Clipboard.InternalBuild.json') -Destination "$fixture/tools"
Write-ClipboardBuildRecord "$fixture/tools/Clipboard.InternalBuild.json" @{internalBuild=100}
Write-ClipboardBuildRecord "$fixture/tools/Clipboard.PublishedBuild.json" @{internalBuild=99}
Get-ChildItem -LiteralPath "$root/Scripts/Source/User" -File -Filter '*.psc' | Copy-Item -Destination "$fixture/Scripts/Source/User"
$script:checks = 0
function Check([bool] $Condition, [string] $Description) {
    if (-not $Condition) { throw "FAILED: $Description" }
    $script:checks++
}
function Reject([scriptblock] $Action, [string] $Description) {
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    Check $failed $Description
}
function Prepare { & (Join-Path $PSScriptRoot 'Prepare-ClipboardBuild.ps1') -ProjectRoot $fixture | Out-Null }
Prepare
Check ((Get-ClipboardInternalBuild $fixture -CheckScripts) -eq 100) 'first package is 100, all 16 source stamps agree'
Prepare
Check ((Get-ClipboardInternalBuild $fixture -CheckScripts) -eq 100) 'failed/unpublished attempts do not consume a number'
$native = [pscustomobject]@{internalBuild=100}
$papyrus = [pscustomobject]@{internalBuild=100}
Check ((Assert-ClipboardPublishableBuild $fixture $native $papyrus) -eq 100) 'matching fresh components accepted'
Reject { Assert-ClipboardPublishableBuild $fixture $native ([pscustomobject]@{internalBuild=101}) } 'mixed components rejected'
Reject { Assert-ClipboardPublishableBuild $fixture $native ([pscustomobject]@{}) } 'unstamped older metadata rejected'
$path = "$fixture/Scripts/Source/User/ClipboardManager.psc"
$original = [IO.File]::ReadAllText($path)
[IO.File]::WriteAllText($path, $original.Replace('return 100', 'return 99'))
Reject { Assert-ClipboardPublishableBuild $fixture $native $papyrus } 'stale individual source stamp rejected'
Prepare
Write-ClipboardBuildRecord "$fixture/Deployments/test.build.json" @{internalBuild=100;result='success'}
Reject { Assert-ClipboardPublishableBuild $fixture $native $papyrus } 'deployment record blocks reuse even before counter commit'
Prepare
Check ((Get-ClipboardInternalBuild $fixture -CheckScripts) -eq 101) 'next publication advances by exactly one'
Prepare
Check ((Get-ClipboardInternalBuild $fixture -CheckScripts) -eq 101) 'retry stays on 101'
Write-ClipboardBuildRecord "$fixture/tools/Clipboard.PublishedBuild.json" @{internalBuild=101}
Prepare
Check ((Get-ClipboardInternalBuild $fixture -CheckScripts) -eq 102) 'tracked publication survives missing deployment records'
$outer = Enter-ClipboardBuildLock $fixture
try {
    $inner = Enter-ClipboardBuildLock $fixture
    $inner.ReleaseMutex(); $inner.Dispose()
    Check $true 'nested build/stage helpers share recursive publication lock'
} finally { $outer.ReleaseMutex(); $outer.Dispose() }
[pscustomobject]@{ result='passed'; checks=$script:checks; fixture=$fixture } | ConvertTo-Json
