<# Builds and publishes the next numbered local package. No game/MO2 installation.
   Failed attempts retain their candidate number; every publication needs a new one. #>
#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch] $CheckOnly,
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
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
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
$uiOptions = Resolve-ClipboardUIPaths $FFDecRoot $JavaHome
$PythonPath = & (Join-Path $PSScriptRoot 'Get-ClipboardPython.ps1') -PythonPath $PythonPath
& $PythonPath -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'
if ($LASTEXITCODE -ne 0) { throw 'Python 3.10 or later is required.' }
foreach ($relative in @('Papyrus Compiler/PapyrusCompiler.exe', 'Data/Scripts/Source/Base/Institute_Papyrus_Flags.flg')) {
    $null = Resolve-ClipboardInputPath (Join-Path $GameRoot $relative) 'GameRoot'
}
$reference = & (Join-Path $PSScriptRoot 'Test-F4SECurrentReference.ps1') -AsJson | ConvertFrom-Json
if ($reference.result -ne 'passed') { throw 'Initialize and verify the pinned F4SE reference before building.' }
if ($CheckOnly) {
    [pscustomobject]@{
        result = 'passed'; scope = 'Input preflight only; no compilation, stamping or staging'
        GameRoot = $GameRoot; Native = $nativeOptions; InputUI = $uiOptions; PythonPath = $PythonPath
    }
    return
}
$buildLock = Enter-ClipboardBuildLock $root
try {
    & (Join-Path $PSScriptRoot 'Prepare-ClipboardBuild.ps1')
    & (Join-Path $PSScriptRoot 'Build-ClipboardInputUI.ps1') @uiOptions
    & (Join-Path $PSScriptRoot 'Build-Papyrus.ps1') -Target Modern079 -GameRoot $GameRoot
    & (Join-Path $PSScriptRoot 'Build-Clipboard.ps1') @nativeOptions
    & (Join-Path $PSScriptRoot 'Stage-Clipboard.ps1') -Archive -PythonPath $PythonPath
} finally { $buildLock.ReleaseMutex(); $buildLock.Dispose() }
