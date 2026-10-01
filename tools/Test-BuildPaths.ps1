# SPDX-License-Identifier: GPL-3.0-or-later
# Input discovery and deployment sequencing checks; never compiles or stages a mod.
#Requires -Version 5.1
[CmdletBinding()]
param([string] $PythonPath)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$PythonPath = & (Join-Path $PSScriptRoot 'Get-ClipboardPython.ps1') -PythonPath $PythonPath
$testRoot = Join-Path $projectRoot ('outputs/build-path-tests/' + [guid]::NewGuid().ToString('N'))
$checkout = Join-Path $testRoot 'source checkout [literal]'
$utf8 = New-Object Text.UTF8Encoding($false)
$script:checks = 0

function Write-Fixture([string] $Path, [string] $Content = 'fixture') {
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force
    [IO.File]::WriteAllText($Path, $Content, $utf8)
}
function Assert-Equal($Actual, $Expected, [string] $Description) {
    if ($Actual -cne $Expected) { throw "Failed: $Description (actual: $Actual; expected: $Expected)" }
    $script:checks++
}
function Assert-Fails([scriptblock] $Action, [string] $ExpectedText) {
    $message = $null
    try { & $Action | Out-Null } catch { $message = $_.Exception.Message }
    if (-not $message -or $message -notlike ('*' + $ExpectedText + '*')) {
        throw "Expected a failure containing '$ExpectedText'; got '$message'."
    }
    $script:checks++
}

. (Join-Path $PSScriptRoot 'ClipboardBuildPaths.ps1')
$game = Join-Path $testRoot 'game inputs'
$vs = Join-Path $testRoot 'VS Build Tools'
$sdk = Join-Path $testRoot 'Windows SDK'
$vcpkg = Join-Path $testRoot 'custom vcpkg'
$jdk = Join-Path $testRoot 'custom JDK'
$ffdec = Join-Path $testRoot 'custom FFDec'
$cmake = Join-Path $testRoot 'custom CMake/cmake.exe'
$vcVersion = '14.52.99999'
$sdkVersion = '10.0.29999.0'
foreach ($relative in @('Fallout4.exe', 'f4se_1_11_240.dll', 'Data/F4SE/Plugins/f4rd-runtime.bin',
    'Papyrus Compiler/PapyrusCompiler.exe', 'Data/Scripts/Source/Base/Institute_Papyrus_Flags.flg')) {
    Write-Fixture (Join-Path $game $relative)
}
foreach ($relative in @('MSBuild/Current/Bin/MSBuild.exe',
    "VC/Tools/MSVC/$vcVersion/bin/Hostx64/x64/cl.exe", "VC/Tools/MSVC/$vcVersion/bin/Hostx64/x64/link.exe",
    "VC/Tools/MSVC/$vcVersion/bin/Hostx64/x64/dumpbin.exe")) { Write-Fixture (Join-Path $vs $relative) }
foreach ($relative in @("Lib/$sdkVersion/um/x64/kernel32.lib", "Lib/$sdkVersion/ucrt/x64/ucrt.lib",
    "Include/$sdkVersion/um/Windows.h", "Include/$sdkVersion/ucrt/stdio.h")) { Write-Fixture (Join-Path $sdk $relative) }
foreach ($relative in @('vcpkg.exe', 'scripts/buildsystems/vcpkg.cmake')) { Write-Fixture (Join-Path $vcpkg $relative) }
foreach ($relative in @('bin/java.exe', 'bin/javac.exe')) { Write-Fixture (Join-Path $jdk $relative) }
foreach ($relative in @('lib/ffdec_lib.jar', 'flashlib/playerglobal32_0.swc')) { Write-Fixture (Join-Path $ffdec $relative) }
Write-Fixture $cmake
Write-Fixture (Join-Path $checkout 'literal [input].txt')

# This test SDK is deliberately unregistered; production reads the Windows registry.
function Get-ClipboardWindowsSdkRoot { return $sdk }
$options = @{
    ProjectRoot = $checkout; GameRoot = $game; VisualStudioPath = $vs; CMakePath = $cmake
    VcpkgRoot = $vcpkg; VCToolsVersion = $vcVersion; WindowsSdkVersion = $sdkVersion
}
$resolved = Resolve-ClipboardNativePaths @options
Assert-Equal $resolved.Parameters.Fallout4Executable (Join-Path $game 'Fallout4.exe') 'game executable default'
Assert-Equal $resolved.Parameters.F4SEDllPath (Join-Path $game 'f4se_1_11_240.dll') 'F4SE default'
Assert-Equal $resolved.Parameters.RuntimeDatabasePath (Join-Path $game 'Data/F4SE/Plugins/f4rd-runtime.bin') 'database default'
Assert-Equal $resolved.Compiler (Join-Path $vs "VC/Tools/MSVC/$vcVersion/bin/Hostx64/x64/cl.exe") 'compiler follows selected installation/version'
Assert-Equal $resolved.Parameters.CMakePath $cmake 'standalone CMake override'
$fixtureOverride = Join-Path $testRoot 'independent fixtures'
foreach ($leaf in @('Fallout4.exe', 'f4se_1_11_240.dll', 'f4rd-runtime.bin')) { Write-Fixture (Join-Path $fixtureOverride $leaf) }
$options.Fallout4Executable = Join-Path $fixtureOverride 'Fallout4.exe'
$options.F4SEDllPath = Join-Path $fixtureOverride 'f4se_1_11_240.dll'
$options.RuntimeDatabasePath = Join-Path $fixtureOverride 'f4rd-runtime.bin'
$resolved = Resolve-ClipboardNativePaths @options
foreach ($name in @('Fallout4Executable', 'F4SEDllPath', 'RuntimeDatabasePath')) {
    Assert-Equal $resolved.Parameters[$name] $options[$name] "explicit $name override"
}
Assert-Fails { Test-ClipboardFixtureVersions $resolved.Parameters } 'expected 1.11.240.0'
$bad = @{} + $options
$bad.F4SEDllPath = Join-Path $fixtureOverride 'missing.dll'
Assert-Fails { Resolve-ClipboardNativePaths @bad } '-F4SEDllPath input was not found'
$bad = @{} + $options
$bad.VCToolsVersion = '14.52.88888'
Assert-Fails { Resolve-ClipboardNativePaths @bad } '-VCToolsVersion input was not found'
Push-Location -LiteralPath $checkout
try { Assert-Equal (Resolve-ClipboardInputPath '.\literal [input].txt' 'Test') (Join-Path $checkout 'literal [input].txt') 'literal relative path with brackets' }
finally { Pop-Location }

$savedEnvironment = @{}
foreach ($name in @('CLIPBOARD_GAME_ROOT', 'VCPKG_ROOT', 'JAVA_HOME', 'FFDEC_ROOT')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $env:CLIPBOARD_GAME_ROOT = $game
    $env:VCPKG_ROOT = $vcpkg
    $env:JAVA_HOME = $jdk
    $env:FFDEC_ROOT = $ffdec
    Assert-Equal (Resolve-ClipboardGameRoot '' $checkout) $game 'game environment fallback'
    Assert-Equal (Resolve-ClipboardUIPaths '' '').JavaHome $jdk 'JDK environment fallback'
    Assert-Equal (Resolve-ClipboardUIPaths '' '').FFDecRoot $ffdec 'FFDec environment fallback'
    $environmentOptions = @{} + $options
    $environmentOptions.Remove('VcpkgRoot')
    Assert-Equal (Resolve-ClipboardNativePaths @environmentOptions).Parameters.VcpkgRoot $vcpkg 'vcpkg environment fallback'
    $env:CLIPBOARD_GAME_ROOT = $null
    Assert-Fails { Resolve-ClipboardGameRoot '' $checkout } 'Supply -GameRoot'
    $record = @{ compiler = (Join-Path $game 'Papyrus Compiler/PapyrusCompiler.exe') } | ConvertTo-Json
    Write-Fixture (Join-Path $checkout 'build/papyrus/v240/papyrus-build.json') $record
    Assert-Equal (Resolve-ClipboardGameRoot '' $checkout) $game 'previous compiler tree fallback'
    Assert-Fails { Resolve-ClipboardGameRoot (Join-Path $testRoot 'missing game') $checkout } '-GameRoot'
    Assert-Fails { Resolve-ClipboardUIPaths (Join-Path $testRoot 'missing FFDec') $jdk } '-FFDecRoot'
    $jre = Join-Path $testRoot 'JRE without compiler'
    Write-Fixture (Join-Path $jre 'bin/java.exe')
    Assert-Fails { Resolve-ClipboardUIPaths $ffdec $jre } 'javac.exe'
} finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
}

# Exercise the real orchestrator and native entry point in an isolated checkout.
# Only external build steps and PE version resources are stubbed; path discovery,
# parameter binding, preflight placement and the build lock run unchanged.
$sandboxTools = Join-Path $checkout 'tools'
$null = New-Item -ItemType Directory -Path $sandboxTools -Force
foreach ($name in @('Build-Deployment.ps1', 'Build-Clipboard.ps1', 'ClipboardBuildPaths.ps1',
    'ClipboardBuildIdentity.ps1', 'Get-ClipboardPython.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $sandboxTools $name)
}
$resolverCopy = Join-Path $sandboxTools 'ClipboardBuildPaths.ps1'
$overrideText = @'

function Get-ClipboardWindowsSdkRoot { return (Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'Windows SDK') }
function Test-ClipboardFixtureVersions { param([hashtable] $Parameters) }
'@
[IO.File]::AppendAllText($resolverCopy, $overrideText, $utf8)
$logFunction = @'
function Record-Step([string] $Step, $Values) {
    [IO.File]::AppendAllText((Join-Path $PSScriptRoot 'calls.jsonl'), ((@{step=$Step; parameters=$Values} | ConvertTo-Json -Compress) + [Environment]::NewLine))
}
'@
Write-Fixture (Join-Path $sandboxTools 'Prepare-ClipboardBuild.ps1') ($logFunction + "`nRecord-Step 'prepare' @{}")
Write-Fixture (Join-Path $sandboxTools 'Build-ClipboardInputUI.ps1') ("param([string]`$FFDecRoot, [string]`$JavaHome)`n" + $logFunction + "`nRecord-Step 'ui' `$PSBoundParameters")
Write-Fixture (Join-Path $sandboxTools 'Build-Papyrus.ps1') ("param([string]`$Target, [string]`$GameRoot)`n" + $logFunction + "`nRecord-Step 'papyrus' `$PSBoundParameters")
Write-Fixture (Join-Path $sandboxTools 'Build-Native-V240.ps1') (@'
param([string]$VisualStudioPath, [string]$CMakePath, [string]$VcpkgRoot, [string]$VCToolsVersion,
    [string]$WindowsSdkVersion, [string]$Fallout4Executable, [string]$F4SEDllPath, [string]$RuntimeDatabasePath)
'@ + "`n" + $logFunction + "`nRecord-Step 'native' `$PSBoundParameters")
Write-Fixture (Join-Path $sandboxTools 'Stage-Clipboard.ps1') ("param([switch]`$Archive, [string]`$PythonPath)`n" + $logFunction + "`nRecord-Step 'stage' `$PSBoundParameters")
Write-Fixture (Join-Path $sandboxTools 'Test-F4SECurrentReference.ps1') "param([switch]`$AsJson)`n'{`"result`":`"passed`"}'"
$deployment = @{} + $options
$deployment.Remove('ProjectRoot')
$deployment.FFDecRoot = $ffdec
$deployment.JavaHome = $jdk
$deployment.PythonPath = $PythonPath
$entry = Join-Path $sandboxTools 'Build-Deployment.ps1'
$callsPath = Join-Path $sandboxTools 'calls.jsonl'
$plan = & $entry @deployment -CheckOnly
Assert-Equal $plan.result 'passed' 'read-only deployment preflight'
Assert-Equal (Test-Path -LiteralPath $callsPath) $false 'preflight invokes no build or stamping step'
$bad = @{} + $deployment
$bad.JavaHome = $jre
Assert-Fails { & $entry @bad } 'javac.exe'
Assert-Equal (Test-Path -LiteralPath $callsPath) $false 'failed preflight invokes no build or stamping step'
& $entry @deployment
$calls = @(Get-Content -LiteralPath $callsPath | ForEach-Object { $_ | ConvertFrom-Json })
Assert-Equal ($calls.step -join ',') 'prepare,ui,papyrus,native,stage' 'numbered build step order'
$nativeCall = $calls | Where-Object step -eq 'native'
foreach ($name in $resolved.Parameters.Keys) {
    Assert-Equal $nativeCall.parameters.$name $resolved.Parameters[$name] "deployment forwards $name through native wrapper"
}
$uiCall = $calls | Where-Object step -eq 'ui'
Assert-Equal $uiCall.parameters.FFDecRoot $ffdec 'deployment forwards FFDec root'
Assert-Equal $uiCall.parameters.JavaHome $jdk 'deployment forwards JDK'
Assert-Equal ($calls | Where-Object step -eq 'papyrus').parameters.GameRoot $game 'deployment forwards Papyrus game root'
Assert-Equal ($calls | Where-Object step -eq 'stage').parameters.PythonPath $PythonPath 'deployment forwards Python'
$report = [ordered]@{ result = 'passed'; checks = $script:checks; scope = 'Synthetic input and orchestration checks; no mod compilation'; fixtures = $testRoot }
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $testRoot 'verification.json') -Encoding UTF8
[pscustomobject] $report
