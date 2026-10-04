# Tests publication policy against a disposable checkout fixture, never Deployments.
#Requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../tools/build/ClipboardBuildIdentity.ps1')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$fixture = Join-Path $root ('build/validation/build-identity-tests/' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path "$fixture/config", "$fixture/src/papyrus", "$fixture/Deployments", "$fixture/dist" -Force
Write-ClipboardBuildRecord "$fixture/config/Clipboard.InternalBuild.json" @{internalBuild=100}
Write-ClipboardBuildRecord "$fixture/config/Clipboard.PublishedBuild.json" @{internalBuild=99}
Get-ChildItem -LiteralPath "$root/src/papyrus" -File -Filter '*.psc' | Copy-Item -Destination "$fixture/src/papyrus"
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
function Prepare { & (Join-Path $root 'tools/build/Prepare-ClipboardBuild.ps1') -ProjectRoot $fixture | Out-Null }
Prepare
Check ((Get-ClipboardInternalBuild $fixture -CheckScripts) -eq 100) 'first package is 100, all 16 source stamps agree'
Prepare
Check ((Get-ClipboardInternalBuild $fixture -CheckScripts) -eq 100) 'failed/unpublished attempts do not consume a number'
$native = [pscustomobject]@{internalBuild=100}
$papyrus = [pscustomobject]@{internalBuild=100}
Check ((Assert-ClipboardPublishableBuild $fixture $native $papyrus) -eq 100) 'matching fresh components accepted'
Reject { Assert-ClipboardPublishableBuild $fixture $native ([pscustomobject]@{internalBuild=101}) } 'mixed components rejected'
Reject { Assert-ClipboardPublishableBuild $fixture $native ([pscustomobject]@{}) } 'unstamped older metadata rejected'
$path = "$fixture/src/papyrus/ClipboardManager.psc"
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
Write-ClipboardBuildRecord "$fixture/config/Clipboard.PublishedBuild.json" @{internalBuild=101}
Prepare
Check ((Get-ClipboardInternalBuild $fixture -CheckScripts) -eq 102) 'tracked publication survives missing deployment records'
Write-ClipboardBuildRecord "$fixture/dist/current.build.json" @{internalBuild=105;result='success'}
Prepare
Check ((Get-ClipboardInternalBuild $fixture -CheckScripts) -eq 106) 'current dist records prevent reuse after reverted counters'
Write-ClipboardBuildRecord "$fixture/Deployments/historical.build.json" @{internalBuild=110;result='success'}
Prepare
Check ((Get-ClipboardInternalBuild $fixture -CheckScripts) -eq 111) 'historical Deployments records remain in publication high-water mark'
$outer = Enter-ClipboardBuildLock $fixture
try {
    $inner = Enter-ClipboardBuildLock $fixture
    $inner.ReleaseMutex(); $inner.Dispose()
    Check $true 'nested build/stage helpers share recursive publication lock'
} finally { $outer.ReleaseMutex(); $outer.Dispose() }

# Export a verified historical archive into the current output tree, without
# modifying the historical archive or consuming an internal build number.
$exportTools = Join-Path $fixture 'tools/build'
$null = New-Item -ItemType Directory -Path $exportTools, "$fixture/archive-input" -Force
foreach ($name in @('ClipboardBuildIdentity.ps1', 'ClipboardPackageNames.ps1', 'Get-ClipboardVersion.ps1', 'Export-ClipboardRelease.ps1')) {
    Copy-Item -LiteralPath (Join-Path $root "tools/build/$name") -Destination $exportTools
}
Copy-Item -LiteralPath (Join-Path $root 'vcpkg.json') -Destination $fixture
[IO.File]::WriteAllText("$fixture/archive-input/fixture.txt", 'Archive identity fixture')
$product = & (Join-Path $exportTools 'Get-ClipboardVersion.ps1') -ProjectRoot $fixture
. (Join-Path $exportTools 'ClipboardPackageNames.ps1')
$legacyName = 'Clipboard v' + $product.productVersion + ' - OG NG AE - Test Candidate'
$historicalArchive = Join-Path "$fixture/Deployments" ($legacyName + ' - Build 110.zip')
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory("$fixture/archive-input", $historicalArchive)
$historicalHash = (Get-FileHash -LiteralPath $historicalArchive -Algorithm SHA256).Hash
Write-ClipboardBuildRecord (Join-Path "$fixture/Deployments" ($legacyName + '.build.json')) @{
    result='success'; productVersion=$product.productVersion; internalBuild=110
    archive=$historicalArchive; archiveSha256=$historicalHash
}
$export = & (Join-Path $exportTools 'Export-ClipboardRelease.ps1') -ReleaseVersion $product.productVersion
Check ($export.archive -ceq (Join-Path "$fixture/dist" ('Clipboard Resurrection - OG NG AE - ' + $product.productVersion + '.zip'))) 'historical archive exports with new release name'
Check ($export.archiveSha256 -eq $historicalHash) 'release export preserves exact archive bytes'
Check ((Get-FileHash -LiteralPath $historicalArchive -Algorithm SHA256).Hash -eq $historicalHash) 'historical archive remains unchanged'
Check ((Get-ClipboardInternalBuild $fixture -CheckScripts) -eq 111) 'release filename export consumes no build number'

# Completed numbered directories participate in recovery and release export.
$candidate = Join-Path $fixture 'build/stage/112'
$completed = Join-Path $fixture 'dist/build-112'
$null = New-Item -ItemType Directory -Path $candidate -Force
$newArchive = Join-Path $candidate (Get-ClipboardPackageNames -Product $product -InternalBuild 112).archiveName
Copy-Item -LiteralPath $historicalArchive -Destination $newArchive
Write-ClipboardBuildRecord (Join-Path $candidate ($product.deploymentName + '.build.json')) @{
    result='success'; productVersion=$product.productVersion; internalBuild=112
    archive=(Join-Path $completed ([IO.Path]::GetFileName($newArchive))); archiveSha256=$historicalHash
}
Check ((Get-ClipboardPublishedBuild $fixture) -eq 110) 'unpublished staging directories do not allocate a published number'
# The paths are fixed descendants of this disposable workspace fixture.
$scope = [IO.Path]::GetFullPath($fixture) + '\'
foreach ($path in @($candidate, $completed)) { if (-not [IO.Path]::GetFullPath($path).StartsWith($scope)) { throw 'Unsafe fixture move' } }
[IO.Directory]::Move($candidate, $completed)
Check ((Get-ClipboardPublishedBuild $fixture) -eq 112) 'completed directory prevents number reuse even before counter update'
$export = & (Join-Path $exportTools 'Export-ClipboardRelease.ps1') -ReleaseVersion $product.productVersion
Check ($export.internalBuild -eq 112) 'release exporter selects latest numbered directory'
Check ($export.archiveSha256 -eq $historicalHash) 'numbered directory export remains byte-identical'
Prepare
Check ((Get-ClipboardInternalBuild $fixture -CheckScripts) -eq 113) 'next build recovers publication after interrupted counter update'

Check ($product.deploymentName -ceq 'Clipboard Resurrection - OG NG AE') 'stable deployment folder name'
$testNames = Get-ClipboardPackageNames -Product $product -InternalBuild 128
Check ($testNames.archiveName -ceq 'Clipboard Resurrection - OG NG AE - build 128.zip') 'test ZIP uses lowercase build suffix'
$releaseNames = Get-ClipboardPackageNames -Product $product -InternalBuild 128 -Release -ReleaseVersion $product.productVersion
Check ($releaseNames.archiveName -ceq ('Clipboard Resurrection - OG NG AE - ' + $product.productVersion + '.zip')) 'release ZIP uses semantic version instead of internal build'
Check ($releaseNames.deploymentName -ceq $testNames.deploymentName) 'test and release share the same folder name'
foreach ($invalid in @('128', '3.0', '3.0.0.0', 'v3.0.0', '03.0.0', '3.0.0-beta', '../3.0.0')) {
    Reject { Get-ClipboardPackageNames -Product $product -Release -ReleaseVersion $invalid } "invalid release version rejected: $invalid"
}
Reject { Get-ClipboardPackageNames -Product $product -Release -ReleaseVersion '1.2.3' } 'release version cannot relabel different product bytes'
Reject { Get-ClipboardPackageNames -Product $product -InternalBuild 128 -ReleaseVersion $product.productVersion } 'test mode cannot accept a release version'
$releasePromptState = @{ count = 0; version = $product.productVersion }
function Read-Host([string] $Prompt) { $releasePromptState.count++; return $releasePromptState.version }
$prompted = Get-ClipboardPackageNames -Product $product -InternalBuild 128 -Release
Check ($releasePromptState.count -eq 1 -and $prompted.archiveName -ceq $releaseNames.archiveName) 'missing release version prompts and uses the response'

# A completed release build has a versioned source ZIP and remains exportable.
$releaseDirectory = Join-Path $fixture 'dist/build-114'
$null = New-Item -ItemType Directory -Path $releaseDirectory
$versionedArchive = Join-Path $releaseDirectory $releaseNames.archiveName
Copy-Item -LiteralPath $historicalArchive -Destination $versionedArchive
Write-ClipboardBuildRecord (Join-Path $releaseDirectory ($product.deploymentName + '.build.json')) @{
    result='success'; productVersion=$product.productVersion; internalBuild=114
    archive=$versionedArchive; archiveSha256=$historicalHash; buildKind='release'
}
$export = & (Join-Path $exportTools 'Export-ClipboardRelease.ps1') -ReleaseVersion $product.productVersion
Check ($export.internalBuild -eq 114 -and $export.archiveSha256 -eq $historicalHash) 'versioned release archive exports unchanged'
[pscustomobject]@{ result='passed'; checks=$script:checks; fixture=$fixture } | ConvertTo-Json
