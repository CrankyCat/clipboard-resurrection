<#
.SYNOPSIS
Builds and statically validates the single OG/NG/AE CommonLibF4RD DLL.

.DESCRIPTION
Creates a clean CMake Release build under build\native, runs the native
host checks, validates the installed Runtime Database with the same preflight
code used by the plugin, and records tool/input/artifact hashes. Nothing is
copied to a game or mod-manager directory.
#>

#Requires -Version 5.1

[CmdletBinding()]
param(
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

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).TrimEnd('\')
$pathResolver = Join-Path $PSScriptRoot 'ClipboardBuildPaths.ps1'
. $pathResolver
$pathOptions = @{} + $PSBoundParameters
$pathOptions.ProjectRoot = $projectRoot
$resolvedPaths = Resolve-ClipboardNativePaths @pathOptions
$VisualStudioPath = $resolvedPaths.Parameters.VisualStudioPath
$CMakePath = $resolvedPaths.Parameters.CMakePath
$VcpkgRoot = $resolvedPaths.Parameters.VcpkgRoot
$Fallout4Executable = $resolvedPaths.Parameters.Fallout4Executable
$F4SEDllPath = $resolvedPaths.Parameters.F4SEDllPath
$RuntimeDatabasePath = $resolvedPaths.Parameters.RuntimeDatabasePath
$nativeRoot = $projectRoot
$versionReader = Join-Path $projectRoot 'tools\build\Get-ClipboardVersion.ps1'
. (Join-Path $PSScriptRoot 'ClipboardBuildIdentity.ps1')
$internalBuild = Get-ClipboardInternalBuild $projectRoot -CheckScripts
$product = & $versionReader -ProjectRoot $projectRoot
$binaryRoot = Join-Path $projectRoot 'build\native\vs2026'
$recordRoot = Join-Path $projectRoot 'build\metadata\native'
$logRoot = Join-Path $projectRoot 'build\logs'
$textLog = Join-Path $logRoot 'native-release.log'
$nativeBinaryLog = Join-Path $logRoot 'native-release.binlog'
$hostChecksBinaryLog = Join-Path $logRoot 'native-host-checks.binlog'
$metadataPath = Join-Path $recordRoot 'native-build.json'
$dll = Join-Path $binaryRoot 'Release\clipboard.dll'
$pdb = Join-Path $binaryRoot 'Release\clipboard.pdb'
$preflightExecutable = Join-Path $binaryRoot 'Release\ClipboardRuntimeDatabasePreflightTests.exe'
$legacyCompatTestExecutable = Join-Path $binaryRoot 'Release\ClipboardLegacyCompatTests.exe'
$runtimeCompatTestExecutable = Join-Path $binaryRoot 'Release\ClipboardRuntimeCompatibilityTests.exe'
$importJobTestExecutable = Join-Path $binaryRoot 'Release\ClipboardImportJobTests.exe'
$conduitPolicyTestExecutable = Join-Path $binaryRoot 'Release\ClipboardConduitConnectionPolicyTests.exe'
$localizationTestExecutable = Join-Path $binaryRoot 'Release\ClipboardLocalizationTests.exe'
$patternExportTestExecutable = Join-Path $binaryRoot 'Release\ClipboardPatternExportTests.exe'
$inputStateTestExecutable = Join-Path $binaryRoot 'Release\ClipboardInputStateTests.exe'
$componentRecipeTestExecutable = Join-Path $binaryRoot 'Release\ClipboardComponentRecipeMemoTests.exe'
$componentInventoryTestExecutable = Join-Path $binaryRoot 'Release\ClipboardComponentInventoryTests.exe'
$referenceRowsTestExecutable = Join-Path $binaryRoot 'Release\ClipboardReferenceRowsTests.exe'
$selectionGeometryTestExecutable = Join-Path $binaryRoot 'Release\ClipboardSelectionGeometryTests.exe'
$importCursorPayloadTestExecutable = Join-Path $binaryRoot 'Release\ClipboardImportCursorPayloadTests.exe'
$releaseLinkedTargets = @(
    'Clipboard',
    'ClipboardRuntimeDatabasePreflightTests',
    'ClipboardLegacyCompatTests',
    'ClipboardRuntimeCompatibilityTests',
    'ClipboardImportJobTests',
    'ClipboardConduitConnectionPolicyTests',
    'ClipboardLocalizationTests',
    'ClipboardPatternExportTests',
    'ClipboardInputStateTests',
    'ClipboardComponentRecipeMemoTests',
    'ClipboardComponentInventoryTests',
    'ClipboardReferenceRowsTests',
    'ClipboardSelectionGeometryTests',
    'ClipboardImportCursorPayloadTests'
)
$verifier = Join-Path $projectRoot 'tests\native\Test-NativeDependencies.ps1'
$papyrusContractVerifier = Join-Path $projectRoot 'tests\papyrus\Test-PapyrusContract.ps1'
$metadataVerifier = Join-Path $projectRoot 'tests\native\Test-ClipboardMetadata.ps1'
$buildEntryPoint = Join-Path $projectRoot 'tools\build\Build-Clipboard.ps1'
$runtimeSymbolsVerifier = Join-Path $projectRoot 'tests\native\Test-RuntimeSymbols.ps1'
$snapshotPath = Join-Path $projectRoot 'config/dependencies/CommonLibF4RD.snapshot.json'
$commitPath = Join-Path $projectRoot 'config/dependencies/CommonLibF4RD.commit'
$manifestPath = Join-Path $nativeRoot 'vcpkg.json'
$vcpkg = Join-Path $VcpkgRoot 'vcpkg.exe'
$msbuild = $resolvedPaths.MSBuild
$cl = $resolvedPaths.Compiler
$link = $resolvedPaths.Linker
$dumpbin = $resolvedPaths.Dumpbin
$windowsSdkLibRoot = $resolvedPaths.WindowsSdkLibRoot
$currentPowerShell = (Get-Process -Id $PID).Path
$utf8NoBom = New-Object Text.UTF8Encoding($false)

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

function Assert-NativeBuildPath([string] $Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $allowed = @('build/native/vs2026', 'build/metadata/native' | ForEach-Object {
        [IO.Path]::GetFullPath((Join-Path $projectRoot $_)).TrimEnd('\')
    })
    if ($allowed -inotcontains $resolved.TrimEnd('\')) {
        throw "Refusing native cleanup outside the exact configured output directories: $resolved"
    }
}

function Get-Sha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-TextSha256([string] $Text) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString(
            $hasher.ComputeHash($utf8NoBom.GetBytes($Text))
        ).Replace('-', '')
    }
    finally {
        $hasher.Dispose()
    }
}

function Get-FileRecord([string] $Path) {
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        path = $item.FullName
        bytes = $item.Length
        sha256 = Get-Sha256 $item.FullName
    }
}

function Get-ReleaseLinkModeRecord([string] $Target, [switch] $VerifyCommands) {
    $projectPath = Join-Path $binaryRoot "$Target.vcxproj"
    Assert-File $projectPath "Generated $Target project"
    [xml] $project = Get-Content -LiteralPath $projectPath -Raw
    $namespaces = New-Object Xml.XmlNamespaceManager($project.NameTable)
    $namespaces.AddNamespace('msb', 'http://schemas.microsoft.com/developer/msbuild/2003')
    $releaseGroups = @($project.SelectNodes('//msb:ItemDefinitionGroup', $namespaces) |
        Where-Object { $_.GetAttribute('Condition') -match "'Release\|x64'" })
    if ($releaseGroups.Count -ne 1) {
        throw "$Target does not have one unambiguous Release x64 item definition."
    }
    $mode = $releaseGroups[0].SelectSingleNode('msb:Link/msb:LinkTimeCodeGeneration', $namespaces)
    if (-not $mode -or $mode.InnerText -ne 'UseLinkTimeCodeGeneration') {
        throw "$Target Release must explicitly use full /LTCG; incremental or implicit LTCG is rejected."
    }
    $releaseConfigurations = @($project.SelectNodes('//msb:PropertyGroup', $namespaces) |
        Where-Object { $_.GetAttribute('Condition') -match "'Release\|x64'" -and
            $_.GetAttribute('Label') -eq 'Configuration' })
    $wholeProgram = if ($releaseConfigurations.Count -eq 1) {
        $releaseConfigurations[0].SelectSingleNode('msb:WholeProgramOptimization', $namespaces)
    } else { $null }
    if (-not $wholeProgram -or $wholeProgram.InnerText -ne 'true') {
        throw "$Target Release must retain whole-program compilation (/GL)."
    }
    $commandRecords = @()
    if ($VerifyCommands) {
        $targetBuild = Join-Path $binaryRoot "$Target.dir\Release"
        $commands = @(Get-ChildItem -LiteralPath $targetBuild -Recurse -File -Filter 'link.command.1.tlog')
        if ($commands.Count -eq 0) {
            throw "$Target has no final Release linker command record."
        }
        $commandRecords = @(foreach ($command in $commands) {
            $text = Get-Content -LiteralPath $command.FullName -Raw
            if ($text -match '(?i)/LTCG:incremental\b' -or $text -notmatch '(?i)(?<!\S)/LTCG(?=\s|$)') {
                throw "$Target final linker command does not exclusively select full /LTCG."
            }
            Get-FileRecord $command.FullName
        })
    }
    return [ordered]@{
        target = $Target
        wholeProgramCompilation = $true
        linkTimeCodeGeneration = 'full /LTCG'
        incrementalLtcg = $false
        generatedProject = Get-FileRecord $projectPath
        finalLinkCommands = $commandRecords
    }
}

function Get-FilesOrdinal([string] $Root) {
    $fileByFullName = [Collections.Generic.Dictionary[string, IO.FileInfo]]::new(
        [StringComparer]::Ordinal
    )
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -Force -File) {
        $fileByFullName.Add($file.FullName, $file)
    }
    $fullNames = [string[]] @($fileByFullName.Keys)
    [Array]::Sort($fullNames, [StringComparer]::Ordinal)
    return @($fullNames | ForEach-Object { $fileByFullName[$_] })
}

function Get-NativeTreeSnapshot() {
    $nativePrefix = $nativeRoot.TrimEnd('\') + '\'
    $records = @(Get-ClipboardNativeInputFiles $projectRoot | ForEach-Object {
            [ordered]@{
                relativePath = $_.FullName.Substring($nativePrefix.Length).Replace('\', '/')
                bytes = $_.Length
                sha256 = Get-Sha256 $_.FullName
            }
        })
    $treeText = ($records | ForEach-Object {
        "$($_.sha256.ToLowerInvariant())  $($_.relativePath)`n"
    }) -join ''
    return [pscustomobject]@{
        Records = $records
        Sha256 = Get-TextSha256 $treeText
    }
}

function Get-ToolRecord([string] $Path) {
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        path = $item.FullName
        version = $item.VersionInfo.FileVersion
        sha256 = Get-Sha256 $item.FullName
    }
}

function Assert-VersionParts(
    [string] $Path,
    [int[]] $Expected,
    [string] $Description
) {
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    $actual = @(
        $version.FileMajorPart,
        $version.FileMinorPart,
        $version.FileBuildPart,
        $version.FilePrivatePart
    )
    if (($actual -join '.') -ne ($Expected -join '.')) {
        throw "$Description has version $($actual -join '.'), expected $($Expected -join '.'): $Path"
    }
    return $version
}

function Format-Command([string] $FilePath, [string[]] $ArgumentList) {
    $arguments = @($ArgumentList | ForEach-Object {
        $value = [string] $_
        if ($value -match '[\s"]') {
            '"' + $value.Replace('"', '\"') + '"'
        }
        else {
            $value
        }
    })
    return ('> ' + $FilePath + ' ' + ($arguments -join ' ')).TrimEnd()
}

function Invoke-LoggedNative(
    [string] $FilePath,
    [string[]] $ArgumentList,
    [string] $WorkingDirectory,
    [string] $Description
) {
    $command = Format-Command $FilePath $ArgumentList
    [IO.File]::AppendAllText($textLog, $command + [Environment]::NewLine, $utf8NoBom)
    Write-Host $command

    Push-Location -LiteralPath $WorkingDirectory
    try {
        $output = @(& $FilePath @ArgumentList 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    $lines = [string[]] @($output | ForEach-Object { [string] $_ })
    if ($lines.Count -gt 0) {
        [IO.File]::AppendAllLines($textLog, $lines, $utf8NoBom)
        foreach ($lineText in $lines) {
            Write-Host $lineText
        }
    }
    [IO.File]::AppendAllText($textLog, "Exit code: $exitCode`r`n`r`n", $utf8NoBom)

    if ($exitCode -ne 0) {
        throw "$Description failed with exit code $exitCode. See $textLog"
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = $lines
    }
}

foreach ($requiredDirectory in @($nativeRoot, $VcpkgRoot, $windowsSdkLibRoot)) {
    Assert-Directory $requiredDirectory 'Required native directory'
}
foreach ($requiredFile in @(
    $CMakePath,
    $vcpkg,
    $msbuild,
    $cl,
    $link,
    $dumpbin,
    $currentPowerShell,
    $Fallout4Executable,
    $F4SEDllPath,
    $RuntimeDatabasePath,
    $verifier,
    $papyrusContractVerifier,
    $metadataVerifier,
    $buildEntryPoint,
    $runtimeSymbolsVerifier,
    $snapshotPath,
    $commitPath,
    $manifestPath,
    (Join-Path $nativeRoot 'CMakeLists.txt'),
    (Join-Path $nativeRoot 'CMakePresets.json'),
    (Join-Path $nativeRoot 'src\native\Clipboard.cpp'),
    (Join-Path $nativeRoot 'src\native\EngineAPI.cpp'),
    (Join-Path $nativeRoot 'src\native\Localization.cpp'),
    (Join-Path $nativeRoot 'src\native\LocalizationRuntime.cpp'),
    (Join-Path $nativeRoot 'src\native\LatentBridge.cpp'),
    (Join-Path $nativeRoot 'src\native\RuntimeDatabasePreflight.cpp')
)) {
    Assert-File $requiredFile 'Required current build input'
}

$falloutVersion = Assert-VersionParts $Fallout4Executable @(1, 11, 240, 0) 'Fallout4.exe'
$f4seVersionInfo = Assert-VersionParts $F4SEDllPath @(0, 0, 7, 9) 'F4SE runtime DLL'
$snapshot = Get-Content -LiteralPath $snapshotPath -Raw | ConvertFrom-Json
$vcpkgManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$commonLibCommit = (Get-Content -LiteralPath $commitPath -Raw).Trim()
if ($snapshot.upstreamCommit -cne $commonLibCommit) {
    throw 'The CommonLibF4RD commit marker and snapshot manifest disagree.'
}
if (-not $snapshot.PSObject.Properties['upstreamNormalizedTextTreeSha256'] -or
    -not $snapshot.PSObject.Properties['localPatches'] -or
    @($snapshot.localPatches).Count -eq 0) {
    throw 'The CommonLibF4RD snapshot must distinguish pinned upstream from the documented local lazy-globals patch.'
}
if ([string] $vcpkgManifest.'builtin-baseline' -cne 'e03dc9b29710050cd1018bc5674688108658d327') {
    throw 'vcpkg.json does not contain the reviewed dependency baseline.'
}

# Capture every maintained native input before configuration. The matching
# post-build snapshot below prevents a DLL compiled from a moving source tree
# from being attested against only its final state.
$nativeTreeBefore = Get-NativeTreeSnapshot
$criticalInputHashesBefore = [ordered]@{
    buildHelper = Get-Sha256 $PSCommandPath
    pathResolver = Get-Sha256 $pathResolver
    dependencyVerifier = Get-Sha256 $verifier
    papyrusContractVerifier = Get-Sha256 $papyrusContractVerifier
    metadataVerifier = Get-Sha256 $metadataVerifier
    buildEntryPoint = Get-Sha256 $buildEntryPoint
    runtimeSymbolsVerifier = Get-Sha256 $runtimeSymbolsVerifier
    versionReader = Get-Sha256 $versionReader
}

Assert-NativeBuildPath $binaryRoot
Assert-NativeBuildPath $recordRoot
foreach ($path in @($binaryRoot, $recordRoot)) {
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}
New-Item -ItemType Directory -Path $recordRoot -Force | Out-Null
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
foreach ($log in @($textLog, $nativeBinaryLog, $hostChecksBinaryLog)) {
    if (Test-Path -LiteralPath $log -PathType Leaf) {
        Remove-Item -LiteralPath $log -Force
    }
}
[IO.File]::WriteAllText(
    $textLog,
    "Clipboard native build started $((Get-Date).ToUniversalTime().ToString('o'))`r`n`r`n",
    $utf8NoBom
)

# Some automation hosts inject both Path and PATH into the process. MSBuild's
# ToolTask treats those as duplicate case-insensitive keys when launching cl.
$pathKeys = @([Environment]::GetEnvironmentVariables().Keys | Where-Object {
    [string]::Equals([string] $_, 'Path', [StringComparison]::OrdinalIgnoreCase)
})
if ($pathKeys.Count -gt 1) {
    $currentPath = $env:Path
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:Path = $currentPath
}

$hadVcpkgRoot = Test-Path Env:VCPKG_ROOT
$previousVcpkgRoot = $env:VCPKG_ROOT
$env:VCPKG_ROOT = $VcpkgRoot

try {
    $integrity = Invoke-LoggedNative $currentPowerShell @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $verifier,
        '-ProjectRoot', $projectRoot
    ) $projectRoot 'Vendored CommonLibF4RD integrity verification'

    $papyrusContract = Invoke-LoggedNative $currentPowerShell @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $papyrusContractVerifier,
        '-ProjectRoot', $projectRoot
    ) $projectRoot 'Papyrus native contract verification'

    $runtimeSymbolsCheck = Invoke-LoggedNative $currentPowerShell @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $runtimeSymbolsVerifier,
        '-AsJson'
    ) $projectRoot 'Runtime symbol inventory verification'
    $runtimeSymbolsResult = ($runtimeSymbolsCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
    if ($runtimeSymbolsResult.result -ne 'passed' -or
        [int] $runtimeSymbolsResult.commonLibSymbolCount -le 0 -or
        [int] $runtimeSymbolsResult.engineSymbolCount -le 0) {
        throw 'The runtime symbol inventories did not report verified nonempty dependency sets.'
    }

    $configure = Invoke-LoggedNative $CMakePath @(
        '--preset', 'vs2026-windows-vcpkg',
        '--fresh',
        '-T', "v145,version=$VCToolsVersion",
        "-DCMAKE_GENERATOR_INSTANCE=$VisualStudioPath",
        "-DCMAKE_SYSTEM_VERSION=$WindowsSdkVersion",
        '-DCLIPBOARD_BUILD_TESTS=ON',
        '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md'
    ) $nativeRoot 'CMake configure'

    $generatedProject = Join-Path $binaryRoot 'Clipboard.vcxproj'
    $generatedSolution = Join-Path $binaryRoot 'Clipboard.slnx'
    $generatedCache = Join-Path $binaryRoot 'CMakeCache.txt'
    Assert-File $generatedProject 'Generated Clipboard project'
    Assert-File $generatedSolution 'Generated Clipboard solution'
    Assert-File $generatedCache 'Generated CMake cache'
    $generatedProjectText = Get-Content -LiteralPath $generatedProject -Raw
    $generatedCacheText = Get-Content -LiteralPath $generatedCache -Raw
    if ($generatedProjectText -notmatch '<PlatformToolset>v145</PlatformToolset>' -or
        $generatedProjectText -notmatch ("<WindowsTargetPlatformVersion>" + [regex]::Escape($WindowsSdkVersion) + "</WindowsTargetPlatformVersion>")) {
        throw 'Generated CMake project does not select the requested v145 compiler and Windows SDK versions.'
    }
    foreach ($translationUnit in @(
        'Clipboard.cpp',
        'EngineAPI.cpp',
        'Localization.cpp',
        'LocalizationRuntime.cpp',
        'LatentBridge.cpp',
        'RuntimeDatabasePreflight.cpp'
    )) {
        if ($generatedProjectText -notmatch ('<ClCompile Include="[^"]*\\' + [regex]::Escape($translationUnit) + '"')) {
            throw "Clipboard target does not compile required source: $translationUnit"
        }
    }
    $expectedGeneratorToolset = 'v145,version=' + $VCToolsVersion
    $expectedGeneratorToolsetLine = 'CMAKE_GENERATOR_TOOLSET:INTERNAL=' + $expectedGeneratorToolset
    if (@($generatedCacheText -split '\r?\n') -notcontains $expectedGeneratorToolsetLine) {
        throw "CMake did not record the requested compiler toolset: $expectedGeneratorToolset"
    }
    foreach ($target in $releaseLinkedTargets) {
        $null = Get-ReleaseLinkModeRecord $target
    }

    $nativeBuild = Invoke-LoggedNative $CMakePath @(
        '--build', $binaryRoot,
        '--config', 'Release',
        '--target', 'Clipboard',
        '--parallel',
        '--', '/nr:false', '/v:minimal', "/bl:$nativeBinaryLog"
    ) $nativeRoot 'Clipboard Release build'

    $hostChecks = Invoke-LoggedNative $CMakePath @(
        '--build', $binaryRoot,
        '--config', 'Release',
        '--target', 'clipboard_host_checks',
        '--parallel',
        '--', '/nr:false', '/v:minimal', "/bl:$hostChecksBinaryLog"
    ) $nativeRoot 'native host checks'

    Assert-File $dll 'clipboard Release DLL'
    Assert-File $pdb 'clipboard Release PDB'
    Assert-File $preflightExecutable 'Runtime Database preflight executable'
    Assert-File $legacyCompatTestExecutable 'Legacy compatibility host-test executable'
    Assert-File $runtimeCompatTestExecutable 'Runtime compatibility host-test executable'
    Assert-File $importJobTestExecutable 'Import job state host-test executable'
    Assert-File $conduitPolicyTestExecutable 'Conduit connection policy host-test executable'
    Assert-File $localizationTestExecutable 'Localization host-test executable'
    $releaseLinkModeRecords = @(foreach ($target in $releaseLinkedTargets) {
        Get-ReleaseLinkModeRecord $target -VerifyCommands
    })

    $runtimeDatabaseHashBefore = Get-Sha256 $RuntimeDatabasePath
    $runtimeDatabaseValidation = Invoke-LoggedNative $preflightExecutable @(
        '--validate', $RuntimeDatabasePath
    ) $projectRoot 'Installed Runtime Database validation'
    $runtimeDatabaseHashAfter = Get-Sha256 $RuntimeDatabasePath
    if ($runtimeDatabaseHashAfter -ne $runtimeDatabaseHashBefore) {
        throw 'The installed Runtime Database changed while it was being validated.'
    }
    $runtimeValidationText = $runtimeDatabaseValidation.Output -join [Environment]::NewLine
    $recordMatch = [regex]::Match(
        $runtimeValidationText,
        'Runtime Database accepted with\s+(\d+)\s+records'
    )
    if (-not $recordMatch.Success -or [UInt64] $recordMatch.Groups[1].Value -eq 0) {
        throw 'Runtime Database preflight succeeded without reporting a nonzero record count.'
    }
    $runtimeDatabaseRecordCount = [UInt64] $recordMatch.Groups[1].Value

    $exportsResult = Invoke-LoggedNative $dumpbin @('/nologo', '/exports', $dll) $projectRoot 'DLL export inspection'
    $dependentsResult = Invoke-LoggedNative $dumpbin @('/nologo', '/dependents', $dll) $projectRoot 'DLL dependency inspection'
    $headersResult = Invoke-LoggedNative $dumpbin @('/nologo', '/headers', $dll) $projectRoot 'DLL header inspection'
    $pluginMetadataCheck = Invoke-LoggedNative $currentPowerShell @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $metadataVerifier,
        '-DllPath', $dll
    ) $projectRoot 'Compiled Clipboard runtime eligibility verification'
    $pluginMetadataResult = ($pluginMetadataCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
    if ($pluginMetadataResult.result -ne 'passed' -or $pluginMetadataResult.sha256 -ne (Get-Sha256 $dll)) {
        throw 'Compiled runtime eligibility inspection did not identify the exact built DLL.'
    }
    $vcpkgVersionResult = Invoke-LoggedNative $vcpkg @('version') $projectRoot 'vcpkg version inspection'
}
finally {
    if ($hadVcpkgRoot) {
        $env:VCPKG_ROOT = $previousVcpkgRoot
    }
    else {
        Remove-Item Env:VCPKG_ROOT -ErrorAction SilentlyContinue
    }
}

[IO.File]::WriteAllLines(
    (Join-Path $recordRoot 'Clipboard.exports.txt'),
    [string[]] $exportsResult.Output,
    $utf8NoBom
)
[IO.File]::WriteAllLines(
    (Join-Path $recordRoot 'Clipboard.dependents.txt'),
    [string[]] $dependentsResult.Output,
    $utf8NoBom
)
[IO.File]::WriteAllLines(
    (Join-Path $recordRoot 'Clipboard.headers.txt'),
    [string[]] $headersResult.Output,
    $utf8NoBom
)

$actualExports = @($exportsResult.Output | ForEach-Object {
    if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') {
        $Matches[1]
    }
} | Sort-Object -Unique)
$expectedExports = @('F4SEPlugin_Load', 'F4SEPlugin_Query', 'F4SEPlugin_Version') | Sort-Object
$exportDifference = @(Compare-Object -ReferenceObject $expectedExports -DifferenceObject $actualExports)
if ($exportDifference.Count -gt 0) {
    throw "Built DLL export set differs from the reviewed CommonLibF4RD contract: $($actualExports -join ', ')"
}

$actualDependencies = @($dependentsResult.Output | ForEach-Object {
    if ($_ -match '^\s+([A-Za-z0-9._-]+\.dll)\s*$') {
        $Matches[1].ToUpperInvariant()
    }
} | Sort-Object -Unique)
$expectedDependencies = @(
    'API-MS-WIN-CRT-CONVERT-L1-1-0.DLL',
    'API-MS-WIN-CRT-FILESYSTEM-L1-1-0.DLL',
    'API-MS-WIN-CRT-HEAP-L1-1-0.DLL',
    'API-MS-WIN-CRT-LOCALE-L1-1-0.DLL',
    'API-MS-WIN-CRT-MATH-L1-1-0.DLL',
    'API-MS-WIN-CRT-RUNTIME-L1-1-0.DLL',
    'API-MS-WIN-CRT-STDIO-L1-1-0.DLL',
    'API-MS-WIN-CRT-STRING-L1-1-0.DLL',
    'API-MS-WIN-CRT-TIME-L1-1-0.DLL',
    'BCRYPT.DLL',
    'KERNEL32.DLL',
    'MSVCP140.DLL',
    'OLE32.DLL',
    'SHELL32.DLL',
    'USER32.DLL',
    'VCRUNTIME140.DLL',
    'VCRUNTIME140_1.DLL',
    'VERSION.DLL'
) | Sort-Object
$dependencyDifference = @(Compare-Object -ReferenceObject $expectedDependencies -DifferenceObject $actualDependencies)
if ($dependencyDifference.Count -gt 0) {
    throw "Built DLL dependency set differs from the reviewed CommonLibF4RD set: $($actualDependencies -join ', ')"
}

$headerText = $headersResult.Output -join [Environment]::NewLine
if ($headerText -notmatch '8664 machine \(x64\)') {
    throw 'Built DLL is not reported as an x64 image.'
}
$clipboardVersionInfo = Assert-VersionParts $dll @($product.major, $product.minor, $product.patch, $product.tweak) 'Clipboard DLL'
if ($clipboardVersionInfo.ProductVersion -cne $product.productVersion -or
    $clipboardVersionInfo.FileVersion -cne $product.productVersion) {
    throw 'Clipboard Windows resource version strings do not match the authoritative product version.'
}

$nativeTreeAfter = Get-NativeTreeSnapshot
if ($nativeTreeAfter.Sha256 -ne $nativeTreeBefore.Sha256 -or
    $nativeTreeAfter.Records.Count -ne $nativeTreeBefore.Records.Count) {
    throw 'native changed during the build; refusing to attest a mixed source state.'
}
$integrityAfter = Invoke-LoggedNative $currentPowerShell @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $verifier,
    '-ProjectRoot', $projectRoot
) $projectRoot 'Post-build patched CommonLibF4RD integrity verification'
foreach ($criticalInput in $criticalInputHashesBefore.GetEnumerator()) {
    $path = switch ($criticalInput.Key) {
        'buildHelper' { $PSCommandPath }
        'pathResolver' { $pathResolver }
        'dependencyVerifier' { $verifier }
        'papyrusContractVerifier' { $papyrusContractVerifier }
        'metadataVerifier' { $metadataVerifier }
        'buildEntryPoint' { $buildEntryPoint }
        'runtimeSymbolsVerifier' { $runtimeSymbolsVerifier }
        'versionReader' { $versionReader }
    }
    if ((Get-Sha256 $path) -ne $criticalInput.Value) {
        throw "Build input changed while the native build was running: $path"
    }
}
$nativeTree = $nativeTreeBefore.Records

$warningLines = @(
    Get-Content -LiteralPath $textLog |
        Where-Object { $_ -match '(?i)\bwarning\s+(?:C|LNK|A)\d{4}\b' } |
        ForEach-Object { $_.Trim() } |
        Sort-Object -Unique
)
$signature = Get-AuthenticodeSignature -LiteralPath $dll
$vcpkgStatus = Join-Path $binaryRoot 'vcpkg_installed\vcpkg\status'
Assert-File $vcpkgStatus 'vcpkg installed-package status file'

if ((Get-ClipboardInternalBuild $projectRoot -CheckScripts) -ne $internalBuild) {
    throw 'Internal build changed during native compilation.'
}
$metadata = [ordered]@{
    schemaVersion = 3
    internalBuild = $internalBuild
    builtUtc = (Get-Date).ToUniversalTime().ToString('o')
    result = 'success'
    flavor = 'Release'
    productVersion = $product.productVersion
    productVersionSource = $product
    fallout4Runtime = '1.11.240.0'
    f4seVersion = '0.7.9'
    runtimeEligibility = [ordered]@{
        families = @('OG', 'NG', 'AE')
        exactPatchWhitelist = $false
        addressIndependence = 'Signatures'
        structureIndependence = @('1.10.980Layout', '1.11.137Layout')
        ogLoader = 'F4SEPlugin_Query'
        buildTestFixture = '1.11.240.0 / F4SE 0.7.9'
        otherRuntimeCertification = 'not performed; family eligibility and address resolution are not certification'
    }
    configuration = 'Release'
    platform = 'x64'
    cmakeGenerator = 'Visual Studio 18 2026'
    visualStudioPath = $VisualStudioPath
    platformToolset = 'v145'
    vcToolsVersion = $VCToolsVersion
    windowsSdkVersion = $WindowsSdkVersion
    vcpkgTriplet = 'x64-windows-static-md'
    linkTimeCodeGeneration = [ordered]@{
        mode = 'full /LTCG'
        wholeProgramCompilation = $true
        incrementalLtcg = $false
        verifiedTargets = $releaseLinkModeRecords
    }
    commonLibF4RD = [ordered]@{
        upstreamCommit = $commonLibCommit
        fileCount = [int] $snapshot.fileCount
        normalizedTextTreeSha256 = [string] $snapshot.normalizedTextTreeSha256
        upstreamNormalizedTextTreeSha256 = [string] $snapshot.upstreamNormalizedTextTreeSha256
        localPatches = @($snapshot.localPatches)
        sourceState = 'pinned upstream with documented local patches'
        integrityResult = 'passed'
        verifierOutput = $integrity.Output -join [Environment]::NewLine
        postBuildVerifierOutput = $integrityAfter.Output -join [Environment]::NewLine
    }
    runtimeDatabase = [ordered]@{
        externalDependency = $true
        packaged = $false
        path = [IO.Path]::GetFullPath($RuntimeDatabasePath)
        bytes = (Get-Item -LiteralPath $RuntimeDatabasePath).Length
        sha256 = $runtimeDatabaseHashAfter
        recordCount = $runtimeDatabaseRecordCount
        validationResult = 'accepted'
        validator = Get-FileRecord $preflightExecutable
        sourceInventory = $runtimeSymbolsResult
    }
    targetInstallation = [ordered]@{
        fallout4 = [ordered]@{
            path = [IO.Path]::GetFullPath($Fallout4Executable)
            fileVersion = $falloutVersion.FileVersion
            sha256 = Get-Sha256 $Fallout4Executable
        }
        f4se = [ordered]@{
            path = [IO.Path]::GetFullPath($F4SEDllPath)
            fileVersion = $f4seVersionInfo.FileVersion
            sha256 = Get-Sha256 $F4SEDllPath
        }
    }
    tools = [ordered]@{
        cmake = Get-ToolRecord $CMakePath
        vcpkg = [ordered]@{
            path = [IO.Path]::GetFullPath($vcpkg)
            versionOutput = $vcpkgVersionResult.Output -join [Environment]::NewLine
            sha256 = Get-Sha256 $vcpkg
            builtinBaseline = [string] $vcpkgManifest.'builtin-baseline'
            installedStatusSha256 = Get-Sha256 $vcpkgStatus
        }
        msbuild = Get-ToolRecord $msbuild
        compiler = Get-ToolRecord $cl
        linker = Get-ToolRecord $link
        dumpbin = Get-ToolRecord $dumpbin
        powershell = Get-ToolRecord $currentPowerShell
    }
    inputs = [ordered]@{
        nativeTree = $nativeTree
        nativeTreeSha256 = $nativeTreeBefore.Sha256
        nativeTreeIncludedPaths = @(Get-ClipboardNativeInputPaths)
        nativeTreeExcludedPaths = @('CMakeUserPresets.json')
        buildHelper = Get-FileRecord $PSCommandPath
        pathResolver = Get-FileRecord $pathResolver
        dependencyVerifier = Get-FileRecord $verifier
        papyrusContractVerifier = Get-FileRecord $papyrusContractVerifier
        metadataVerifier = Get-FileRecord $metadataVerifier
        buildEntryPoint = Get-FileRecord $buildEntryPoint
        runtimeSymbolsVerifier = Get-FileRecord $runtimeSymbolsVerifier
        versionReader = Get-FileRecord $versionReader
    }
    hostChecks = [ordered]@{
        result = 'passed'
        ctestCount = 14
        tests = @(
            'clipboard_commonlib_integrity',
            'clipboard_runtime_database_preflight',
            'clipboard_legacy_compat',
            'clipboard_runtime_compatibility',
            'clipboard_import_jobs',
            'clipboard_conduit_connection_policy',
            'clipboard_localization',
            'clipboard_pattern_export',
            'clipboard_input_state',
            'clipboard_component_recipe_memo',
            'clipboard_component_inventory',
            'clipboard_reference_rows',
            'clipboard_selection_geometry',
            'clipboard_import_cursor_payload'
        )
        executables = [ordered]@{
            runtimeDatabasePreflight = Get-FileRecord $preflightExecutable
            legacyCompatibility = Get-FileRecord $legacyCompatTestExecutable
            runtimeCompatibility = Get-FileRecord $runtimeCompatTestExecutable
            importJobs = Get-FileRecord $importJobTestExecutable
            conduitConnectionPolicy = Get-FileRecord $conduitPolicyTestExecutable
            localization = Get-FileRecord $localizationTestExecutable
            patternExport = Get-FileRecord $patternExportTestExecutable
            inputState = Get-FileRecord $inputStateTestExecutable
            componentRecipeMemo = Get-FileRecord $componentRecipeTestExecutable
            componentInventory = Get-FileRecord $componentInventoryTestExecutable
            referenceRows = Get-FileRecord $referenceRowsTestExecutable
            selectionGeometry = Get-FileRecord $selectionGeometryTestExecutable
            importCursorPayload = Get-FileRecord $importCursorPayloadTestExecutable
        }
        papyrusContract = $papyrusContract.Output -join [Environment]::NewLine
        installedRuntimeDatabaseValidation = 'passed'
        compiledEligibility = $pluginMetadataResult
        runtimeSymbolSourceInventory = 'passed'
    }
    dll = [ordered]@{
        path = [IO.Path]::GetFullPath($dll)
        bytes = (Get-Item -LiteralPath $dll).Length
        sha256 = Get-Sha256 $dll
        signatureStatus = [string] $signature.Status
    }
    pdb = [ordered]@{
        path = [IO.Path]::GetFullPath($pdb)
        bytes = (Get-Item -LiteralPath $pdb).Length
        sha256 = Get-Sha256 $pdb
    }
    exports = $actualExports
    dependencies = $actualDependencies
    warnings = $warningLines
    warningCount = $warningLines.Count
    logs = [ordered]@{
        text = Get-FileRecord $textLog
        nativeBinlog = Get-FileRecord $nativeBinaryLog
        hostChecksBinlog = Get-FileRecord $hostChecksBinaryLog
    }
    papyrus = [ordered]@{
        compiledByThisHelper = $false
        gate = 'separate verified input required by Stage-Package.ps1'
    }
    certification = [ordered]@{
        staticBuildAndHostChecks = 'passed'
        f4seLoad = 'not performed; open Phase 4 gate'
        papyrusNativeRegistration = 'not performed; open Phase 4 gate'
        inGameBehavior = 'not performed; open Phase 4 gate'
    }
}
[IO.File]::WriteAllText(
    $metadataPath,
    ($metadata | ConvertTo-Json -Depth 10) + [Environment]::NewLine,
    $utf8NoBom
)

Write-Output 'Native Release build and static validation succeeded.'
Write-Output "DLL: $dll"
Write-Output "SHA-256: $($metadata.dll.sha256)"
Write-Output "Runtime Database records: $runtimeDatabaseRecordCount"
Write-Output "Warnings recorded: $($warningLines.Count)"
Write-Output "Metadata: $metadataPath"
Write-Output 'No game or mod-manager directory was modified.'
