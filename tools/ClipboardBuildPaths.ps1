# SPDX-License-Identifier: GPL-3.0-or-later
# Shared, read-only input discovery. Explicit paths always take precedence.
#Requires -Version 5.1

function Resolve-ClipboardInputPath {
    param([string] $Path, [string] $Parameter, [switch] $Directory)
    if ([string]::IsNullOrWhiteSpace($Path)) { throw "Supply -$Parameter with a local path." }
    $full = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    $kind = if ($Directory) { 'Container' } else { 'Leaf' }
    if (-not (Test-Path -LiteralPath $full -PathType $kind)) {
        throw "-$Parameter input was not found: $full"
    }
    return $full
}

function Find-ClipboardApplication([string] $Name) {
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    return $null
}

function Resolve-ClipboardGameRoot([string] $GameRoot, [string] $ProjectRoot) {
    if (-not $GameRoot) { $GameRoot = $env:CLIPBOARD_GAME_ROOT }
    if (-not $GameRoot) {
        $previous = Join-Path $ProjectRoot 'build/papyrus/v240/papyrus-build.json'
        if (Test-Path -LiteralPath $previous -PathType Leaf) {
            $record = Get-Content -LiteralPath $previous -Raw | ConvertFrom-Json
            if ($record.PSObject.Properties['compiler'] -and $record.compiler) {
                $GameRoot = Split-Path -Parent (Split-Path -Parent $record.compiler)
            }
        }
    }
    return Resolve-ClipboardInputPath $GameRoot 'GameRoot (or set CLIPBOARD_GAME_ROOT)' -Directory
}

function Resolve-ClipboardVisualStudio([string] $VisualStudioPath) {
    if (-not $VisualStudioPath) {
        $vswhere = Find-ClipboardApplication 'vswhere.exe'
        if (-not $vswhere -and ${env:ProgramFiles(x86)}) {
            $candidate = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { $vswhere = $candidate }
        }
        if ($vswhere) {
            $found = @(& $vswhere -latest -products '*' -version '[18.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
            if ($LASTEXITCODE -ne 0) { throw 'Visual Studio discovery failed; supply -VisualStudioPath.' }
            if ($found.Count -gt 0) { $VisualStudioPath = ([string] $found[0]).Trim() }
        }
    }
    return Resolve-ClipboardInputPath $VisualStudioPath 'VisualStudioPath' -Directory
}

function Get-ClipboardWindowsSdkRoot {
    foreach ($key in @('HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots')) {
        $record = Get-ItemProperty -LiteralPath $key -Name KitsRoot10 -ErrorAction SilentlyContinue
        if ($record -and $record.KitsRoot10) { return [string] $record.KitsRoot10 }
    }
    throw 'Windows SDK 10 is not registered. Install it using Visual Studio Installer.'
}

function Resolve-ClipboardNativePaths {
    [CmdletBinding()]
    param(
        [string] $ProjectRoot,
        [string] $GameRoot,
        [string] $VisualStudioPath,
        [string] $CMakePath,
        [string] $VcpkgRoot,
        [string] $VCToolsVersion = '14.52.36725',
        [string] $WindowsSdkVersion = '10.0.28000.0',
        [string] $Fallout4Executable,
        [string] $F4SEDllPath,
        [string] $RuntimeDatabasePath
    )
    if ($VCToolsVersion -notmatch '^14\.\d+\.\d+$') { throw 'Supply -VCToolsVersion as an installed MSVC version, for example 14.52.36725.' }
    if ($WindowsSdkVersion -notmatch '^10\.0\.\d+\.0$') { throw 'Supply -WindowsSdkVersion as an installed Windows SDK version.' }
    $VisualStudioPath = Resolve-ClipboardVisualStudio $VisualStudioPath
    if (-not $CMakePath) {
        $candidate = Join-Path $VisualStudioPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $CMakePath = $candidate }
        else { $CMakePath = Find-ClipboardApplication 'cmake.exe' }
    }
    if (-not $VcpkgRoot) { $VcpkgRoot = $env:VCPKG_ROOT }
    if (-not $VcpkgRoot) { $VcpkgRoot = Join-Path $VisualStudioPath 'VC/vcpkg' }
    if (-not $Fallout4Executable -or -not $F4SEDllPath -or -not $RuntimeDatabasePath) {
        $GameRoot = Resolve-ClipboardGameRoot $GameRoot $ProjectRoot
        if (-not $Fallout4Executable) { $Fallout4Executable = Join-Path $GameRoot 'Fallout4.exe' }
        if (-not $F4SEDllPath) { $F4SEDllPath = Join-Path $GameRoot 'f4se_1_11_240.dll' }
        if (-not $RuntimeDatabasePath) { $RuntimeDatabasePath = Join-Path $GameRoot 'Data/F4SE/Plugins/f4rd-runtime.bin' }
    }
    $parameters = @{
        VisualStudioPath = $VisualStudioPath
        CMakePath = Resolve-ClipboardInputPath $CMakePath 'CMakePath'
        VcpkgRoot = Resolve-ClipboardInputPath $VcpkgRoot 'VcpkgRoot' -Directory
        VCToolsVersion = $VCToolsVersion
        WindowsSdkVersion = $WindowsSdkVersion
        Fallout4Executable = Resolve-ClipboardInputPath $Fallout4Executable 'Fallout4Executable'
        F4SEDllPath = Resolve-ClipboardInputPath $F4SEDllPath 'F4SEDllPath'
        RuntimeDatabasePath = Resolve-ClipboardInputPath $RuntimeDatabasePath 'RuntimeDatabasePath'
    }
    $compilerRoot = Join-Path $VisualStudioPath "VC/Tools/MSVC/$VCToolsVersion/bin/Hostx64/x64"
    $sdkRoot = Get-ClipboardWindowsSdkRoot
    $sdkLib = Join-Path $sdkRoot "Lib/$WindowsSdkVersion"
    foreach ($relative in @('um/x64/kernel32.lib', 'ucrt/x64/ucrt.lib')) {
        $null = Resolve-ClipboardInputPath (Join-Path $sdkLib $relative) 'WindowsSdkVersion'
    }
    foreach ($relative in @('um/Windows.h', 'ucrt/stdio.h')) {
        $null = Resolve-ClipboardInputPath (Join-Path $sdkRoot "Include/$WindowsSdkVersion/$relative") 'WindowsSdkVersion'
    }
    $null = Resolve-ClipboardInputPath (Join-Path $parameters.VcpkgRoot 'scripts/buildsystems/vcpkg.cmake') 'VcpkgRoot'
    return [pscustomobject]@{
        Parameters = $parameters
        MSBuild = Resolve-ClipboardInputPath (Join-Path $VisualStudioPath 'MSBuild/Current/Bin/MSBuild.exe') 'VisualStudioPath'
        Compiler = Resolve-ClipboardInputPath (Join-Path $compilerRoot 'cl.exe') 'VCToolsVersion'
        Linker = Resolve-ClipboardInputPath (Join-Path $compilerRoot 'link.exe') 'VCToolsVersion'
        Dumpbin = Resolve-ClipboardInputPath (Join-Path $compilerRoot 'dumpbin.exe') 'VCToolsVersion'
        Vcpkg = Resolve-ClipboardInputPath (Join-Path $parameters.VcpkgRoot 'vcpkg.exe') 'VcpkgRoot'
        WindowsSdkLibRoot = $sdkLib
    }
}

function Resolve-ClipboardUIPaths([string] $FFDecRoot, [string] $JavaHome) {
    if (-not $FFDecRoot) { $FFDecRoot = $env:FFDEC_ROOT }
    if (-not $FFDecRoot -and ${env:ProgramFiles(x86)}) { $FFDecRoot = Join-Path ${env:ProgramFiles(x86)} 'FFDec' }
    if (-not $JavaHome) { $JavaHome = $env:JAVA_HOME }
    if (-not $JavaHome) {
        $javac = Find-ClipboardApplication 'javac.exe'
        if ($javac) { $JavaHome = Split-Path -Parent (Split-Path -Parent $javac) }
    }
    $FFDecRoot = Resolve-ClipboardInputPath $FFDecRoot 'FFDecRoot' -Directory
    $JavaHome = Resolve-ClipboardInputPath $JavaHome 'JavaHome' -Directory
    foreach ($relative in @('bin/java.exe', 'bin/javac.exe')) {
        $null = Resolve-ClipboardInputPath (Join-Path $JavaHome $relative) 'JavaHome'
    }
    foreach ($relative in @('lib/ffdec_lib.jar', 'flashlib/playerglobal32_0.swc')) {
        $null = Resolve-ClipboardInputPath (Join-Path $FFDecRoot $relative) 'FFDecRoot'
    }
    return @{ FFDecRoot = $FFDecRoot; JavaHome = $JavaHome }
}

function Test-ClipboardFixtureVersions([hashtable] $Parameters) {
    foreach ($fixture in @(
        @{ Name = 'Fallout4Executable'; Version = '1.11.240.0' },
        @{ Name = 'F4SEDllPath'; Version = '0.0.7.9' }
    )) {
        $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Parameters[$fixture.Name])
        $actual = @($info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart) -join '.'
        if ($actual -ne $fixture.Version) { throw "-$($fixture.Name) has version $actual; expected $($fixture.Version)." }
    }
}
