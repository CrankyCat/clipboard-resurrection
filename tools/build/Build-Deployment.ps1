<# Builds and publishes the next numbered local package. No game/MO2 installation.
   Failed attempts retain their candidate number; every publication needs a new one. #>
#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch] $CheckOnly,
    [switch] $Release,
    [string] $ReleaseVersion,
    [string] $GameRoot,
    [string] $VisualStudioPath,
    [string] $CMakePath,
    [string] $VcpkgRoot,
    [string] $VCToolsVersion = '14.52.36725',
    [string] $WindowsSdkVersion = '10.0.28000.0',
    [string] $Fallout4Executable,
    [string] $F4SEDllPath,
    [string] $RuntimeDatabasePath,
    [string] $FFDecRoot,
    [string] $JavaHome,
    [string] $PythonPath
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ClipboardBuildIdentity.ps1')
. (Join-Path $PSScriptRoot 'ClipboardBuildPaths.ps1')
. (Join-Path $PSScriptRoot 'ClipboardPackageNames.ps1')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Release -and -not [string]::IsNullOrWhiteSpace($ReleaseVersion)) { throw '-ReleaseVersion requires -Release.' }
if ($Release) {
    $product = & (Join-Path $PSScriptRoot 'Get-ClipboardVersion.ps1') -ProjectRoot $root
    $ReleaseVersion = Resolve-ClipboardReleaseVersion $ReleaseVersion $product.productVersion
}
$GameRoot = Resolve-ClipboardGameRoot $GameRoot $root
$nativeOptions = @{
    ProjectRoot = $root; GameRoot = $GameRoot; VisualStudioPath = $VisualStudioPath
    CMakePath = $CMakePath; VcpkgRoot = $VcpkgRoot; VCToolsVersion = $VCToolsVersion
    WindowsSdkVersion = $WindowsSdkVersion; Fallout4Executable = $Fallout4Executable
    F4SEDllPath = $F4SEDllPath; RuntimeDatabasePath = $RuntimeDatabasePath
}
$nativePaths = Resolve-ClipboardNativePaths @nativeOptions
$nativeOptions = $nativePaths.Parameters
Test-ClipboardFixtureVersions $nativeOptions
$uiOptions = Resolve-ClipboardUIPaths $FFDecRoot $JavaHome $root
if (-not $PythonPath) { $PythonPath = Get-ClipboardLocalBuildPath $root 'PythonPath' }
$PythonPath = & (Join-Path $PSScriptRoot 'Get-ClipboardPython.ps1') -PythonPath $PythonPath
Assert-ClipboardPersistentInput $PythonPath $root
& $PythonPath -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'
if ($LASTEXITCODE -ne 0) { throw 'Python 3.10 or later is required.' }
foreach ($relative in @('Papyrus Compiler/PapyrusCompiler.exe', 'Data/Scripts/Source/Base/Institute_Papyrus_Flags.flg')) {
    $null = Resolve-ClipboardInputPath (Join-Path $GameRoot $relative) 'GameRoot'
}
$reference = & (Join-Path $PSScriptRoot '../../tests/build/Test-F4SECurrentReference.ps1') -AsJson | ConvertFrom-Json
if ($reference.result -ne 'passed') { throw 'Initialize and verify the pinned F4SE reference before building.' }
if ($CheckOnly) {
    [pscustomobject]@{
        result = 'passed'; scope = 'Input preflight only; no compilation, stamping or staging'
        GameRoot = $GameRoot; Native = $nativeOptions; InputUI = $uiOptions; PythonPath = $PythonPath
        Release = [bool] $Release; ReleaseVersion = $ReleaseVersion
    }
    return
}
$buildLock = Enter-ClipboardBuildLock $root
try {
    & (Join-Path $PSScriptRoot 'Prepare-ClipboardBuild.ps1')
    & (Join-Path $PSScriptRoot 'Build-GeneratedAssets.ps1') -PythonPath $PythonPath
    & (Join-Path $PSScriptRoot 'Build-ClipboardInputUI.ps1') @uiOptions
    & (Join-Path $PSScriptRoot 'Build-Papyrus.ps1') -Target Current -GameRoot $GameRoot
    & (Join-Path $PSScriptRoot 'Build-Clipboard.ps1') @nativeOptions
    & (Join-Path $PSScriptRoot 'Stage-Clipboard.ps1') -Archive -PythonPath $PythonPath -Release:$Release -ReleaseVersion $ReleaseVersion
    if ($Release) { & (Join-Path $PSScriptRoot 'Export-ClipboardRelease.ps1') -ReleaseVersion $ReleaseVersion }
} finally { $buildLock.ReleaseMutex(); $buildLock.Dispose() }
