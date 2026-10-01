<#
.SYNOPSIS
Creates a local Fallout 4 OG/NG/AE Clipboard test deployment and optional ZIP.

.DESCRIPTION
Stages the reviewed Release240 DLL, immutable static base plus the verified
modern twelve-language overlay, and the separately
verified Modern079 Papyrus build (compiled against the verified official
F4SE 0.7.9 API reference) into one shared test-package directory under
Deployments. The helper rechecks all recorded hashes, the vendored
CommonLibF4RD snapshot, and the installed Runtime Database. It never deploys
to the game or mod-manager directories and never compiles Papyrus.
#>

#Requires -Version 5.1

[CmdletBinding()]
param(
    [switch] $Archive,
    [string] $PythonPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
. (Join-Path $PSScriptRoot 'ClipboardBuildIdentity.ps1')
$publicationLock = Enter-ClipboardBuildLock $projectRoot
try {
$versionReader = Join-Path $projectRoot 'tools\Get-ClipboardVersion.ps1'
$product = & $versionReader -ProjectRoot $projectRoot
$internalBuild = Get-ClipboardInternalBuild $projectRoot -CheckScripts
$packageBase = Join-Path $projectRoot 'package\base'
$packageOverlay = Join-Path $projectRoot 'package\v240'
$documentationRoot = 'Docs/clipboard'
$englishDocuments = @(
    "$documentationRoot/Clipboard-ReadMe.txt", "$documentationRoot/Clipboard-Object-Filtering.txt",
    "$documentationRoot/Clipboard-Localization.txt", "$documentationRoot/CHANGELOG.txt"
)
$v240Readme = Join-Path $packageOverlay "$documentationRoot/Clipboard-ReadMe.txt"
$v240FilterGuide = Join-Path $packageOverlay "$documentationRoot/Clipboard-Object-Filtering.txt"
$v240LocalizationGuide = Join-Path $packageOverlay "$documentationRoot/Clipboard-Localization.txt"
$v240Changelog = Join-Path $packageOverlay "$documentationRoot/CHANGELOG.txt"
$documentationLayoutPath = Join-Path $projectRoot 'localization/source/docs-layout.json'
$localizedEsp = Join-Path $packageOverlay 'Clipboard.esp'
$localizationBuilder = Join-Path $projectRoot 'tools\Build-Localization.py'
$espLocalizationTool = Join-Path $projectRoot 'tools\ClipboardLocalizationEsp.py'
$pythonResolver = Join-Path $projectRoot 'tools\Get-ClipboardPython.ps1'
$espMappingPath = Join-Path $projectRoot 'localization\esp-map.json'
$pythonExecutable = & $pythonResolver -PythonPath $PythonPath
$locales = @('en', 'ru', 'de', 'es', 'esmx', 'fr', 'it', 'ja', 'pl', 'ptbr', 'zhhans', 'zhhant')
$translatedLocales = @($locales | Where-Object { $_ -cne 'en' })
$localeAliases = [ordered]@{ cn = 'zhhant' }
$fileLocales = @($locales) + @($localeAliases.Keys)
$translatedDocuments = @()
$catalogOutputPaths = @($locales | ForEach-Object { "F4SE/Plugins/Clipboard/Localization/$_.tsv" }) + @(
    $fileLocales | ForEach-Object { "Interface/Translations/Clipboard_$_.txt" }
)
$espOutputPaths = @($fileLocales | ForEach-Object {
    $locale = $_
    @('STRINGS', 'DLSTRINGS', 'ILSTRINGS') | ForEach-Object { "Strings/Clipboard_$locale.$_" }
})
$approvedOverlayPaths = @(
    'Clipboard.esp', 'MCM/Config/Clipboard/config.json', 'MCM/Config/Clipboard/keybinds.json',
    'MCM/Config/Clipboard/settings.ini', 'Interface/ClipboardInput.swf'
) + $englishDocuments + $catalogOutputPaths + $espOutputPaths
$nativeRoot = Join-Path $projectRoot 'native'
$nativeBinaryRoot = Join-Path $projectRoot 'build\native\vs2026'
$nativeMetadataPath = Join-Path $projectRoot 'build\native\release-1.11.240\native-build.json'
$nativeDll = Join-Path $nativeBinaryRoot 'Release\clipboard.dll'
$nativePdb = Join-Path $nativeBinaryRoot 'Release\clipboard.pdb'
$preflightExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardRuntimeDatabasePreflightTests.exe'
$legacyCompatTestExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardLegacyCompatTests.exe'
$runtimeCompatTestExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardRuntimeCompatibilityTests.exe'
$importJobTestExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardImportJobTests.exe'
$conduitPolicyTestExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardConduitConnectionPolicyTests.exe'
$localizationTestExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardLocalizationTests.exe'
$patternExportTestExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardPatternExportTests.exe'
$inputStateTestExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardInputStateTests.exe'
$componentRecipeTestExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardComponentRecipeMemoTests.exe'
$componentInventoryTestExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardComponentInventoryTests.exe'
$referenceRowsTestExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardReferenceRowsTests.exe'
$selectionGeometryTestExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardSelectionGeometryTests.exe'
$importCursorPayloadTestExecutable = Join-Path $nativeBinaryRoot 'Release\ClipboardImportCursorPayloadTests.exe'
$snapshotPath = Join-Path $nativeRoot 'CommonLibF4RD.snapshot.json'
$papyrusSource = Join-Path $projectRoot 'Scripts\Source\User'
$papyrusOutput = Join-Path $projectRoot 'build\papyrus\v240\Scripts'
$papyrusMetadataPath = Join-Path $projectRoot 'build\papyrus\v240\papyrus-build.json'
$papyrusReferenceVerifier = Join-Path $projectRoot 'tools\Test-F4SECurrentReference.ps1'
$verifier = Join-Path $projectRoot 'tools\Test-V240VendoredDependency.ps1'
$metadataVerifier = Join-Path $projectRoot 'tools\Test-ClipboardMetadata.ps1'
$runtimeSymbolsVerifier = Join-Path $projectRoot 'tools\Test-RuntimeSymbols.ps1'
$stageEntryPoint = Join-Path $projectRoot 'tools\Stage-Clipboard.ps1'
$commonLibLicense = Join-Path $projectRoot 'external\CommonLibF4RD\LICENSE'
$vcpkgShare = Join-Path $nativeBinaryRoot 'vcpkg_installed\x64-windows-static-md\share'
$deploymentsRoot = Join-Path $projectRoot 'Deployments'
$logRoot = Join-Path $projectRoot 'build\logs'
$stageLog = Join-Path $logRoot 'package-clipboard-test-candidate.log'
$dllName = 'clipboard.dll'
$deploymentName = $product.deploymentName
$stageRoot = Join-Path $deploymentsRoot $deploymentName
$manifestPath = Join-Path $deploymentsRoot ($deploymentName + '.manifest.sha256')
$buildInfoPath = Join-Path $deploymentsRoot ($deploymentName + '.build.json')
$archivePath = Join-Path $deploymentsRoot ($deploymentName + ' - Build ' + $internalBuild + '.zip')
$currentPowerShell = (Get-Process -Id $PID).Path
$utf8NoBom = New-Object Text.UTF8Encoding($false)

function Assert-File([string] $Path, [string] $Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found: $Path"
    }
}

function Assert-NoBundledPatterns([object[]] $Records) {
    foreach ($record in $Records) {
        if ([IO.Path]::GetFileName([string] $record.relativePath) -ieq 'pattern.ini') {
            throw "Player pattern files must not be bundled in the modern deployment: $($record.relativePath). Keep slot PLACEHOLDER files only."
        }
    }
}

function Assert-Directory([string] $Path, [string] $Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description was not found: $Path"
    }
}

function Assert-DeploymentPath([string] $Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $allowed = [IO.Path]::GetFullPath($deploymentsRoot) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing generated deployment operation outside $deploymentsRoot`: $resolved"
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

function Get-RelativePath([string] $Root, [string] $Path) {
    $rootPrefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $fullPath = [IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is not under its expected root: $fullPath"
    }
    return $fullPath.Substring($rootPrefix.Length).Replace('\', '/')
}

function Get-FilesOrdinal([string] $Root, [switch] $Recurse) {
    $fileByFullName = [Collections.Generic.Dictionary[string, IO.FileInfo]]::new(
        [StringComparer]::Ordinal
    )
    $files = if ($Recurse) {
        Get-ChildItem -LiteralPath $Root -Recurse -Force -File
    }
    else {
        Get-ChildItem -LiteralPath $Root -Force -File
    }
    foreach ($file in $files) {
        $fileByFullName.Add($file.FullName, $file)
    }
    $fullNames = [string[]] @($fileByFullName.Keys)
    [Array]::Sort($fullNames, [StringComparer]::Ordinal)
    return @($fullNames | ForEach-Object { $fileByFullName[$_] })
}

function Assert-SafeRelativePath([string] $RelativePath, [string] $Description) {
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [IO.Path]::IsPathRooted($RelativePath) -or
        @($RelativePath.Replace('\', '/') -split '/') -contains '..') {
        throw "$Description contains an unsafe relative path: $RelativePath"
    }
}

function Get-PackageRecords([string] $Root) {
    return @(Get-FilesOrdinal -Root $Root -Recurse | ForEach-Object {
        [ordered]@{
            relativePath = Get-RelativePath $Root $_.FullName
            bytes = $_.Length
            sha256 = Get-Sha256 $_.FullName
        }
    })
}

function Get-RecordsTreeHash([object[]] $Records) {
    $text = ($Records | ForEach-Object {
        "$($_.sha256.ToLowerInvariant())  $($_.relativePath)`n"
    }) -join ''
    return Get-TextSha256 $text
}

function Assert-OutputManifest([object] $Report, [string[]] $ExpectedPaths, [string] $Description) {
    if (-not $Report.PSObject.Properties['outputs']) { throw "$Description did not return an output manifest." }
    $actual = @($Report.outputs.PSObject.Properties | ForEach-Object { $_.Name } | Sort-Object)
    $expected = @($ExpectedPaths | Sort-Object)
    if (($actual -join '|') -cne ($expected -join '|')) {
        throw "$Description output allowlist changed. Expected $($expected.Count) exact paths, found $($actual.Count)."
    }
    foreach ($property in $Report.outputs.PSObject.Properties) {
        Assert-SafeRelativePath $property.Name $Description
        if ([string] $property.Value.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or [Int64] $property.Value.bytes -lt 0) {
            throw "$Description contains an invalid file record: $($property.Name)"
        }
    }
}

function Assert-GeneratedFiles([string] $Root, [object] $Report, [string] $Description) {
    foreach ($property in $Report.outputs.PSObject.Properties) {
        $path = Join-Path $Root $property.Name
        Assert-File $path $Description
        if ((Get-Sha256 $path) -ne [string] $property.Value.sha256 -or
            (Get-Item -LiteralPath $path).Length -ne [Int64] $property.Value.bytes) {
            throw "$Description differs from its generated source: $($property.Name)"
        }
    }
}

function Assert-SameOutputManifest([object] $Before, [object] $After, [string] $Description) {
    $names = @($Before.outputs.PSObject.Properties | ForEach-Object { $_.Name })
    Assert-OutputManifest $After $names $Description
    foreach ($name in $names) {
        if ([string] $Before.outputs.$name.sha256 -cne [string] $After.outputs.$name.sha256 -or
            [Int64] $Before.outputs.$name.bytes -ne [Int64] $After.outputs.$name.bytes) {
            throw "$Description changed during staging: $name"
        }
    }
}

function Assert-ModernPapyrusMetadata([object] $Metadata, [object] $Reference) {
    if (-not $Metadata.PSObject.Properties['buildTarget'] -or
        $Metadata.buildTarget -ne 'Modern079' -or
        [int] $Metadata.schemaVersion -lt 2 -or
        $Metadata.result -ne 'success' -or
        [int] $Metadata.compilerExitCode -ne 0 -or
        $Metadata.target -ne 'Fallout 4 1.11.240 baseline' -or
        $Metadata.fallout4Runtime -ne '1.11.240.0' -or
        $Metadata.f4seVersion -ne '0.7.9' -or
        [int] $Metadata.sourceCount -ne 16 -or
        @($Metadata.sourceFiles).Count -ne 16 -or
        @($Metadata.outputs).Count -ne 16) {
        throw 'Papyrus metadata is not a successful 16-script Modern079/F4SE 0.7.9 build. Run Build-Papyrus.ps1 -Target Modern079 before modern staging; legacy 0.7.8 outputs cannot be reused.'
    }
    if (-not $Metadata.PSObject.Properties['f4seReference'] -or
        $null -eq $Metadata.f4seReference -or
        $Reference.result -ne 'passed') {
        throw 'Modern Papyrus metadata must record a verified official F4SE reference.'
    }
    foreach ($field in @('manifestPath', 'manifestSha256', 'upstreamCommit', 'normalizedTextOrBinaryTreeSha256')) {
        if (-not $Metadata.f4seReference.PSObject.Properties[$field] -or
            -not $Reference.PSObject.Properties[$field] -or
            [string]::IsNullOrWhiteSpace([string] $Reference.$field) -or
            [string] $Metadata.f4seReference.$field -ne [string] $Reference.$field) {
            throw "The Papyrus build's F4SE reference differs from the verified current reference: $field. Rebuild Modern079 before staging."
        }
    }
}

function Assert-RecordedTree(
    [string] $Root,
    [object[]] $Records,
    [string] $ExpectedTreeHash,
    [string] $Description,
    [switch] $ExcludeNativeUserPresets
) {
    $currentFiles = @(Get-FilesOrdinal -Root $Root -Recurse)
    if ($ExcludeNativeUserPresets) {
        if ([IO.Path]::GetFullPath($Root).TrimEnd('\') -ine [IO.Path]::GetFullPath($nativeRoot).TrimEnd('\')) {
            throw 'The CMakeUserPresets exclusion is confined to the maintained native source root.'
        }
        $userPresets = Join-Path $nativeRoot 'CMakeUserPresets.json'
        $currentFiles = @($currentFiles | Where-Object { $_.FullName -ine $userPresets })
    }
    if ($currentFiles.Count -ne $Records.Count) {
        throw "$Description file count changed: recorded $($Records.Count), current $($currentFiles.Count)."
    }

    $recordByPath = @{}
    foreach ($record in $Records) {
        $relative = [string] $record.relativePath
        Assert-SafeRelativePath $relative $Description
        if ($recordByPath.ContainsKey($relative)) {
            throw "$Description metadata repeats a path: $relative"
        }
        $recordByPath.Add($relative, $record)
    }

    $currentRecords = @($currentFiles | ForEach-Object {
        $relative = Get-RelativePath $Root $_.FullName
        if (-not $recordByPath.ContainsKey($relative)) {
            throw "$Description contains an unrecorded file: $relative"
        }
        $record = $recordByPath[$relative]
        $hash = Get-Sha256 $_.FullName
        if ($hash -ne [string] $record.sha256 -or $_.Length -ne [Int64] $record.bytes) {
            throw "$Description file changed since the native build: $relative"
        }
        [ordered]@{
            relativePath = $relative
            bytes = $_.Length
            sha256 = $hash
        }
    })
    $treeText = ($currentRecords | ForEach-Object {
        "$($_.sha256.ToLowerInvariant())  $($_.relativePath)`n"
    }) -join ''
    if ((Get-TextSha256 $treeText) -ne $ExpectedTreeHash) {
        throw "$Description aggregate hash does not match native build metadata."
    }
}

function Invoke-StageCheck(
    [string] $FilePath,
    [string[]] $ArgumentList,
    [string] $Description,
    [switch] $QuietOutput
) {
    $renderedArguments = @($ArgumentList | ForEach-Object {
        $value = [string] $_
        if ($value -match '\s') { '"' + $value + '"' } else { $value }
    }) -join ' '
    $command = "> $FilePath $renderedArguments"
    [IO.File]::AppendAllText($stageLog, $command + [Environment]::NewLine, $utf8NoBom)
    Write-Host $command
    $output = @(& $FilePath @ArgumentList 2>&1)
    $exitCode = $LASTEXITCODE
    $lines = [string[]] @($output | ForEach-Object { [string] $_ })
    if ($lines.Count -gt 0) {
        [IO.File]::AppendAllLines($stageLog, $lines, $utf8NoBom)
        if (-not $QuietOutput) {
            foreach ($lineText in $lines) { Write-Host $lineText }
        }
    }
    [IO.File]::AppendAllText($stageLog, "Exit code: $exitCode`r`n`r`n", $utf8NoBom)
    if ($exitCode -ne 0) {
        throw "$Description failed with exit code $exitCode. See $stageLog"
    }
    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = $lines
    }
}

foreach ($directory in @($packageBase, $packageOverlay, $nativeRoot, $papyrusSource, $papyrusOutput, $vcpkgShare)) {
    Assert-Directory $directory 'Required v240 package input directory'
}
foreach ($file in @(
    $nativeMetadataPath,
    $nativeDll,
    $nativePdb,
    $preflightExecutable,
    $papyrusMetadataPath,
    $papyrusReferenceVerifier,
    $verifier,
    $metadataVerifier,
    $snapshotPath,
    $runtimeSymbolsVerifier,
    $stageEntryPoint,
    $commonLibLicense,
    $v240Readme,
    $v240FilterGuide,
    $v240LocalizationGuide,
    $v240Changelog,
    $documentationLayoutPath,
    $localizedEsp,
    $localizationBuilder,
    $espLocalizationTool,
    $pythonResolver,
    $pythonExecutable,
    $espMappingPath,
    $currentPowerShell
)) {
    Assert-File $file 'Required v240 package input'
}
$documentationLayout = Get-Content -LiteralPath $documentationLayoutPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ([int] $documentationLayout.schema -ne 2 -or
    @('pending', 'ready') -cnotcontains [string] $documentationLayout.translation_status) {
    throw 'Deployment documentation must use the reviewed schema-2 translation layout.'
}
$documentationLocales = @($documentationLayout.enabled_locales)
if (($documentationLayout.translation_status -ceq 'pending' -and $documentationLocales.Count -ne 0) -or
    @($documentationLocales | Where-Object { $translatedLocales -cnotcontains $_ }).Count -ne 0 -or
    @($documentationLocales | Select-Object -Unique).Count -ne $documentationLocales.Count) {
    throw 'Documentation translations must remain disabled until English wording is finalized, with each enabled locale listed once.'
}
$layoutEnglishPaths = @($documentationLayout.documents | ForEach-Object {
    $filename = [string] $_.filename
    if ([IO.Path]::GetFileName($filename) -cne $filename -or $filename -match '[/\\]') {
        throw "Documentation layout contains an invalid filename: $filename"
    }
    $relative = "$documentationRoot/$filename"
    if ([string] $_.source -cne "package/v240/$relative") {
        throw "Documentation source must match its approved English package path: $relative"
    }
    $relative
})
if ((@($layoutEnglishPaths | Sort-Object) -join '|') -cne (@($englishDocuments | Sort-Object) -join '|')) {
    throw 'Documentation layout must contain the four approved English end-user documents.'
}
$translatedDocuments = @($documentationLocales | ForEach-Object {
    $locale = $_
    $documentationLayout.documents | ForEach-Object {
        $filename = [string] $_.filename
        $localizedNames = $_.PSObject.Properties['localized_filenames']
        if ($null -ne $localizedNames) {
            $localizedName = $localizedNames.Value.PSObject.Properties[$locale]
            if ($null -eq $localizedName -or [string]::IsNullOrWhiteSpace([string] $localizedName.Value)) {
                throw "Missing localized documentation filename for $locale`: $filename"
            }
            $filename = [string] $localizedName.Value
        }
        if ([IO.Path]::GetFileName($filename) -cne $filename -or $filename -match '[/\\]') {
            throw "Documentation layout contains an invalid localized filename: $filename"
        }
        "$documentationRoot/$locale/$filename"
    }
})
$approvedOverlayPaths += $translatedDocuments
foreach ($path in @($stageRoot, $manifestPath, $buildInfoPath, $archivePath)) {
    Assert-DeploymentPath $path
}

New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
if (Test-Path -LiteralPath $stageLog -PathType Leaf) {
    Remove-Item -LiteralPath $stageLog -Force
}
[IO.File]::WriteAllText(
    $stageLog,
    "Clipboard OG/NG/AE test staging started $((Get-Date).ToUniversalTime().ToString('o'))`r`n`r`n",
    $utf8NoBom
)

$nativeMetadata = Get-Content -LiteralPath $nativeMetadataPath -Raw | ConvertFrom-Json
if (-not $nativeMetadata.PSObject.Properties['productVersionSource'] -or
    $nativeMetadata.productVersionSource.manifestPath -cne $product.manifestPath -or
    $nativeMetadata.productVersionSource.manifestSha256 -cne $product.manifestSha256) {
    throw 'The native build does not attest the current authoritative Clipboard product version.'
}
# The exact versions below identify the available build/test installation,
# not a DLL patch whitelist. Require the separate cross-family eligibility record.
if (-not $nativeMetadata.PSObject.Properties['runtimeEligibility'] -or
    -not $nativeMetadata.runtimeEligibility.PSObject.Properties['families'] -or
    (@($nativeMetadata.runtimeEligibility.families) -join ',') -cne 'OG,NG,AE' -or
    [bool] $nativeMetadata.runtimeEligibility.exactPatchWhitelist -or
    $nativeMetadata.runtimeEligibility.addressIndependence -ne 'Signatures' -or
    (@($nativeMetadata.runtimeEligibility.structureIndependence) -join ',') -cne '1.10.980Layout,1.11.137Layout' -or
    $nativeMetadata.runtimeEligibility.ogLoader -ne 'F4SEPlugin_Query') {
    throw 'The native build does not declare reviewed OG/NG/AE capability-based eligibility.'
}
if (
    [int] $nativeMetadata.schemaVersion -lt 2 -or
    $nativeMetadata.result -ne 'success' -or
    $nativeMetadata.flavor -ne 'Release240' -or
    $nativeMetadata.productVersion -cne $product.productVersion -or
    $nativeMetadata.fallout4Runtime -ne '1.11.240.0' -or
    $nativeMetadata.f4seVersion -ne '0.7.9' -or
    $nativeMetadata.configuration -ne 'Release' -or
    $nativeMetadata.platform -ne 'x64' -or
    $nativeMetadata.platformToolset -ne 'v145' -or
    $nativeMetadata.hostChecks.result -ne 'passed' -or
    $nativeMetadata.hostChecks.installedRuntimeDatabaseValidation -ne 'passed' -or
    $nativeMetadata.hostChecks.runtimeSymbolSourceInventory -ne 'passed' -or
    $nativeMetadata.runtimeDatabase.validationResult -ne 'accepted' -or
    [int] $nativeMetadata.warningCount -ne 0 -or
    @($nativeMetadata.warnings).Count -ne 0 -or
    -not [bool] $nativeMetadata.runtimeDatabase.externalDependency -or
    [bool] $nativeMetadata.runtimeDatabase.packaged
) {
    throw 'Native metadata is not a successful reviewed Release240/F4SE 0.7.9 build.'
}
$expectedCtests = @(
    'clipboard_v240_commonlib_integrity', 'clipboard_runtime_database_preflight',
    'clipboard_legacy_compat', 'clipboard_runtime_compatibility',
    'clipboard_import_jobs', 'clipboard_conduit_connection_policy', 'clipboard_localization', 'clipboard_pattern_export', 'clipboard_input_state',
    'clipboard_component_recipe_memo', 'clipboard_component_inventory',
    'clipboard_reference_rows', 'clipboard_selection_geometry', 'clipboard_import_cursor_payload'
)
if ([int] $nativeMetadata.hostChecks.ctestCount -ne $expectedCtests.Count -or
    (@($nativeMetadata.hostChecks.tests | Sort-Object) -join ',') -cne (@($expectedCtests | Sort-Object) -join ',')) {
    throw 'Native metadata does not record the fourteen required CTest checks.'
}
foreach ($testEntry in @(
    @{ Path = $preflightExecutable; Record = $nativeMetadata.hostChecks.executables.runtimeDatabasePreflight },
    @{ Path = $legacyCompatTestExecutable; Record = $nativeMetadata.hostChecks.executables.legacyCompatibility },
    @{ Path = $runtimeCompatTestExecutable; Record = $nativeMetadata.hostChecks.executables.runtimeCompatibility },
    @{ Path = $importJobTestExecutable; Record = $nativeMetadata.hostChecks.executables.importJobs },
    @{ Path = $conduitPolicyTestExecutable; Record = $nativeMetadata.hostChecks.executables.conduitConnectionPolicy },
    @{ Path = $localizationTestExecutable; Record = $nativeMetadata.hostChecks.executables.localization }
    @{ Path = $patternExportTestExecutable; Record = $nativeMetadata.hostChecks.executables.patternExport }
    @{ Path = $inputStateTestExecutable; Record = $nativeMetadata.hostChecks.executables.inputState }
    @{ Path = $componentRecipeTestExecutable; Record = $nativeMetadata.hostChecks.executables.componentRecipeMemo }
    @{ Path = $componentInventoryTestExecutable; Record = $nativeMetadata.hostChecks.executables.componentInventory }
    @{ Path = $referenceRowsTestExecutable; Record = $nativeMetadata.hostChecks.executables.referenceRows }
    @{ Path = $selectionGeometryTestExecutable; Record = $nativeMetadata.hostChecks.executables.selectionGeometry }
    @{ Path = $importCursorPayloadTestExecutable; Record = $nativeMetadata.hostChecks.executables.importCursorPayload }
)) {
    Assert-File $testEntry.Path 'Recorded native host-test executable'
    if ([string] $testEntry.Record.path -ne $testEntry.Path -or
        (Get-Sha256 $testEntry.Path) -ne [string] $testEntry.Record.sha256 -or
        (Get-Item -LiteralPath $testEntry.Path).Length -ne [Int64] $testEntry.Record.bytes) {
        throw "A native host-test executable changed after compilation: $($testEntry.Path)"
    }
}
$snapshot = Get-Content -LiteralPath $snapshotPath -Raw | ConvertFrom-Json
if ($nativeMetadata.commonLibF4RD.sourceState -ne 'pinned upstream with documented local patches' -or
    $nativeMetadata.commonLibF4RD.upstreamCommit -cne $snapshot.upstreamCommit -or
    $nativeMetadata.commonLibF4RD.normalizedTextTreeSha256 -cne $snapshot.normalizedTextTreeSha256 -or
    $nativeMetadata.commonLibF4RD.upstreamNormalizedTextTreeSha256 -cne $snapshot.upstreamNormalizedTextTreeSha256 -or
    [int] $nativeMetadata.commonLibF4RD.fileCount -ne [int] $snapshot.fileCount -or
    (@($nativeMetadata.commonLibF4RD.localPatches) | ConvertTo-Json -Depth 5 -Compress) -cne
        (@($snapshot.localPatches) | ConvertTo-Json -Depth 5 -Compress)) {
    throw 'The native attestation does not match the pinned CommonLibF4RD snapshot and documented local patches.'
}

if ((Get-Sha256 $nativeDll) -ne [string] $nativeMetadata.dll.sha256) {
    throw 'clipboard.dll does not match the Release240 native metadata.'
}
if ((Get-Sha256 $nativePdb) -ne [string] $nativeMetadata.pdb.sha256) {
    throw 'clipboard.pdb does not match the Release240 native metadata.'
}
foreach ($logRecord in @(
    $nativeMetadata.logs.text,
    $nativeMetadata.logs.nativeBinlog,
    $nativeMetadata.logs.hostChecksBinlog
)) {
    $logPath = [string] $logRecord.path
    Assert-File $logPath 'Recorded native v240 build log'
    if ((Get-Sha256 $logPath) -ne [string] $logRecord.sha256 -or
        (Get-Item -LiteralPath $logPath).Length -ne [Int64] $logRecord.bytes) {
        throw "Recorded native v240 build log changed after compilation: $logPath"
    }
}

$nativeTreeCheck = @{
    Root = $nativeRoot
    Records = [object[]] @($nativeMetadata.inputs.nativeTree)
    ExpectedTreeHash = [string] $nativeMetadata.inputs.nativeTreeSha256
    Description = 'native source tree'
    ExcludeNativeUserPresets = $true
}
if (-not $nativeMetadata.inputs.PSObject.Properties['nativeTreeExcludedPaths'] -or
    (@($nativeMetadata.inputs.nativeTreeExcludedPaths) -join ',') -cne 'CMakeUserPresets.json') {
    throw 'Native source attestation must exclude only the local root CMakeUserPresets.json file.'
}
Assert-RecordedTree @nativeTreeCheck

foreach ($record in @(
    $nativeMetadata.inputs.buildHelper,
    $nativeMetadata.inputs.pathResolver,
    $nativeMetadata.inputs.dependencyVerifier,
    $nativeMetadata.inputs.papyrusContractVerifier,
    $nativeMetadata.inputs.metadataVerifier,
    $nativeMetadata.inputs.versionReader,
    $nativeMetadata.inputs.buildEntryPoint,
    $nativeMetadata.inputs.runtimeSymbolsVerifier
)) {
    Assert-File ([string] $record.path) 'Recorded v240 build script'
    if ((Get-Sha256 ([string] $record.path)) -ne [string] $record.sha256) {
        throw "Recorded v240 build script changed after compilation: $($record.path)"
    }
}

$integrity = Invoke-StageCheck $currentPowerShell @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $verifier,
    '-ProjectRoot', $projectRoot
) 'Vendored CommonLibF4RD integrity verification'

$papyrusContractVerifier = [string] $nativeMetadata.inputs.papyrusContractVerifier.path
$papyrusContract = Invoke-StageCheck $currentPowerShell @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $papyrusContractVerifier,
    '-ProjectRoot', $projectRoot
) 'Papyrus native contract verification'

$runtimeSymbolsCheck = Invoke-StageCheck $currentPowerShell @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $runtimeSymbolsVerifier,
    '-AsJson'
) 'Runtime symbol inventory verification'
$runtimeSymbolsResult = ($runtimeSymbolsCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
if ($runtimeSymbolsResult.result -ne 'passed' -or
    [int] $runtimeSymbolsResult.commonLibSymbolCount -le 0 -or
    [int] $runtimeSymbolsResult.engineSymbolCount -le 0 -or
    [int] $runtimeSymbolsResult.engineSymbolCount -ne [int] $nativeMetadata.runtimeDatabase.sourceInventory.engineSymbolCount -or
    [int] $runtimeSymbolsResult.commonLibSymbolCount -ne [int] $nativeMetadata.runtimeDatabase.sourceInventory.commonLibSymbolCount -or
    [int] $runtimeSymbolsResult.vendorSourceFileCount -ne [int] $nativeMetadata.runtimeDatabase.sourceInventory.vendorSourceFileCount) {
    throw 'The runtime symbol inventories differ from their verified native-build inputs.'
}

$pluginMetadataCheck = Invoke-StageCheck $currentPowerShell @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $metadataVerifier,
    '-DllPath', $nativeDll
) 'Compiled Clipboard OG/NG/AE eligibility metadata verification'
$pluginMetadataResult = ($pluginMetadataCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
if ($pluginMetadataResult.result -ne 'passed' -or
    $pluginMetadataResult.sha256 -ne [string] $nativeMetadata.dll.sha256 -or
    [Int64] $pluginMetadataResult.bytes -ne [Int64] $nativeMetadata.dll.bytes) {
    throw 'Compiled OG/NG/AE eligibility inspection did not identify the exact recorded native DLL.'
}

$runtimeDatabasePath = [string] $nativeMetadata.runtimeDatabase.path
Assert-File $runtimeDatabasePath 'Recorded installed Runtime Database'
if ((Get-Sha256 $runtimeDatabasePath) -ne [string] $nativeMetadata.runtimeDatabase.sha256) {
    throw 'Installed Runtime Database changed after the native build; rebuild before staging.'
}
$validatorRecord = $nativeMetadata.runtimeDatabase.validator
if ([string] $validatorRecord.path -ne $preflightExecutable -or
    (Get-Sha256 $preflightExecutable) -ne [string] $validatorRecord.sha256 -or
    (Get-Item -LiteralPath $preflightExecutable).Length -ne [Int64] $validatorRecord.bytes) {
    throw 'Runtime Database validator does not match native build metadata.'
}
$runtimeValidation = Invoke-StageCheck $preflightExecutable @(
    '--validate', $runtimeDatabasePath
) 'Installed Runtime Database validation'
$runtimeValidationText = $runtimeValidation.Output -join [Environment]::NewLine
$recordMatch = [regex]::Match(
    $runtimeValidationText,
    'Runtime Database accepted with\s+(\d+)\s+records'
)
if (-not $recordMatch.Success -or
    [UInt64] $recordMatch.Groups[1].Value -ne [UInt64] $nativeMetadata.runtimeDatabase.recordCount) {
    throw 'Installed Runtime Database validation no longer matches native build metadata.'
}

foreach ($installationRecord in @(
    $nativeMetadata.targetInstallation.fallout4,
    $nativeMetadata.targetInstallation.f4se
)) {
    Assert-File ([string] $installationRecord.path) 'Recorded target installation binary'
    if ((Get-Sha256 ([string] $installationRecord.path)) -ne [string] $installationRecord.sha256) {
        throw "Recorded target installation binary changed after compilation: $($installationRecord.path)"
    }
}

$papyrusMetadata = Get-Content -LiteralPath $papyrusMetadataPath -Raw | ConvertFrom-Json
$internalBuild = Assert-ClipboardPublishableBuild $projectRoot $nativeMetadata $papyrusMetadata
$papyrusReferenceCheck = Invoke-StageCheck $currentPowerShell @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $papyrusReferenceVerifier,
    '-AsJson'
) 'Current official F4SE Papyrus reference verification'
$papyrusReferenceResult = ($papyrusReferenceCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
Assert-ModernPapyrusMetadata $papyrusMetadata $papyrusReferenceResult

foreach ($source in @($papyrusMetadata.sourceFiles)) {
    $relative = [string] $source.file
    Assert-SafeRelativePath $relative 'Papyrus source metadata'
    if ([IO.Path]::GetExtension($relative) -ne '.psc') {
        throw "Papyrus source metadata contains a non-PSC file: $relative"
    }
    $sourcePath = Join-Path $papyrusSource $relative
    Assert-File $sourcePath "Papyrus source $relative"
    if ((Get-Sha256 $sourcePath) -ne [string] $source.sha256 -or
        (Get-Item -LiteralPath $sourcePath).Length -ne [Int64] $source.bytes) {
        throw "Papyrus source changed since its separate build: $relative"
    }
}
foreach ($output in @($papyrusMetadata.outputs)) {
    $relative = [string] $output.file
    Assert-SafeRelativePath $relative 'Papyrus output metadata'
    if ([IO.Path]::GetExtension($relative) -ne '.pex') {
        throw "Papyrus output metadata contains a non-PEX file: $relative"
    }
    $outputPath = Join-Path $papyrusOutput $relative
    Assert-File $outputPath "Papyrus output $relative"
    if ((Get-Sha256 $outputPath) -ne [string] $output.sha256 -or
        (Get-Item -LiteralPath $outputPath).Length -ne [Int64] $output.bytes) {
        throw "Papyrus output changed since its separate build: $relative"
    }
}

if (Test-Path -LiteralPath (Join-Path $packageBase 'Scripts')) {
    throw 'Static package base unexpectedly contains Scripts; Papyrus must remain a separate recorded input.'
}
if (Get-ChildItem -LiteralPath $packageBase -Recurse -File -Filter '*.dll') {
    throw 'Static package base unexpectedly contains a DLL.'
}

$expectedLicensePackages = @(
    'boost-assert',
    'boost-cmake',
    'boost-config',
    'boost-headers',
    'boost-stl-interfaces',
    'boost-type-traits',
    'fmt',
    'rsm-mmio',
    'spdlog',
    'zycore',
    'zydis'
) | Sort-Object
$vcpkgCopyrights = @(Get-ChildItem -LiteralPath $vcpkgShare -Directory | ForEach-Object {
    $copyright = Join-Path $_.FullName 'copyright'
    if (Test-Path -LiteralPath $copyright -PathType Leaf) {
        [pscustomobject]@{
            package = $_.Name
            path = $copyright
        }
    }
} | Sort-Object package)
$actualLicensePackages = @($vcpkgCopyrights | ForEach-Object { $_.package })
$licenseDifference = @(Compare-Object -ReferenceObject $expectedLicensePackages -DifferenceObject $actualLicensePackages)
if ($licenseDifference.Count -gt 0) {
    throw "vcpkg notice set differs from the reviewed Release240 dependency set: $($actualLicensePackages -join ', ')"
}

$localizationCheck = Invoke-StageCheck $pythonExecutable @(
    $localizationBuilder, '--root', $projectRoot, '--output', $packageOverlay, '--check'
) 'Twelve-language catalog and documentation-layout validation' -QuietOutput
$localizationReport = ($localizationCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
Assert-OutputManifest $localizationReport @($catalogOutputPaths + $translatedDocuments) 'Generated catalog/documentation manifest'
if ($localizationReport.documentation.translation_status -cne $documentationLayout.translation_status -or
    (@($localizationReport.documentation.enabled_locales) -join ',') -cne ($documentationLocales -join ',') -or
    (@($localizationReport.documentation.english_paths | Sort-Object) -join '|') -cne (@($englishDocuments | Sort-Object) -join '|')) {
    throw 'Localization generation does not match the reviewed documentation translation layout.'
}
if ([int] $localizationReport.schema -ne 1 -or $localizationReport.source_locale -cne 'en' -or
    (@($localizationReport.translated_locales) -join ',') -cne ($translatedLocales -join ',') -or
    @($localizationReport.fallback_locales).Count -ne 0) {
    throw 'Localization generation did not declare translations for every reviewed locale without English fallback copies.'
}
if ((@($localizationReport.locale_aliases.PSObject.Properties.Name) -join ',') -cne 'cn' -or
    $localizationReport.locale_aliases.cn -cne 'zhhant') {
    throw 'Localization generation did not declare the reviewed cn-to-zhhant file alias.'
}
Assert-GeneratedFiles $packageOverlay $localizationReport 'Catalog/documentation overlay'

$espTableCheck = Invoke-StageCheck $pythonExecutable @(
    $espLocalizationTool, 'generate', '--output', $packageOverlay, '--check'
) 'ESP language-table validation' -QuietOutput
$espTableReport = ($espTableCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
Assert-OutputManifest $espTableReport $espOutputPaths 'Generated ESP table manifest'
if ((@($espTableReport.generated_locales) -join ',') -cne ($fileLocales -join ',') -or
    (@($espTableReport.locale_aliases.PSObject.Properties.Name) -join ',') -cne 'cn' -or
    $espTableReport.locale_aliases.cn -cne 'zhhant') {
    throw 'ESP table generation did not declare the reviewed locale set.'
}
Assert-GeneratedFiles $packageOverlay $espTableReport 'ESP table overlay'

$espMigrationCheck = Invoke-StageCheck $pythonExecutable @(
    $espLocalizationTool, 'verify', '--esp', $localizedEsp
) 'xEdit ESP non-text migration verification' -QuietOutput
$espMigrationResult = ($espMigrationCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
$espMapping = Get-Content -LiteralPath $espMappingPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ([string] $espMigrationResult.baseline_localized_esp_sha256 -ne [string] $espMapping.localized_esp_sha256 -or
    [string] $espMigrationResult.source_esp_sha256 -ne [string] $espMapping.source_esp_sha256 -or
    [string] $espTableReport.localized_esp_sha256 -ne [string] $espMigrationResult.localized_esp_sha256 -or
    (Get-Sha256 $localizedEsp) -ne [string] $espMigrationResult.localized_esp_sha256) {
    throw 'The localized ESP does not match the captured xEdit migration and generated language tables.'
}

$localizationInputPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($sourceReport in @($localizationReport, $espTableReport)) {
    foreach ($localizationInput in $sourceReport.inputs.PSObject.Properties) {
        Assert-SafeRelativePath $localizationInput.Name 'Localization source manifest'
        $path = Join-Path $projectRoot $localizationInput.Name
        Assert-File $path 'Localization source input'
        if ((Get-Sha256 $path) -ne [string] $localizationInput.Value) {
            throw "Localization source changed during validation: $($localizationInput.Name)"
        }
        [void] $localizationInputPaths.Add($localizationInput.Name)
    }
}
foreach ($relative in @(
    'localization/esp-map.json', 'localization/source/docs-layout.json', 'package/base/Clipboard.esp',
    'localization/esp-baseline/Clipboard-inline.esp', 'localization/esp-baseline/Clipboard.esp', 'localization/esp-revision.json',
    'tools/Build-Localization.py', 'tools/ClipboardLocalizationEsp.py', 'tools/Get-ClipboardPython.ps1'
)) { [void] $localizationInputPaths.Add($relative) }
$localizationInputRecords = @($localizationInputPaths | Sort-Object | ForEach-Object {
    $path = Join-Path $projectRoot $_
    Assert-File $path 'Localization source/tool input'
    [ordered]@{ relativePath = $_; bytes = (Get-Item -LiteralPath $path).Length; sha256 = Get-Sha256 $path }
})

$baseRecords = @(Get-PackageRecords $packageBase)
$baseFileCount = $baseRecords.Count
# Preserve the historical package base byte-for-byte. Only the modern staging
# view omits its superseded readme and relocates the unchanged project license.
$omittedBasePaths = @('Clipboard-ReadMe.txt')
$remappedBasePaths = [ordered]@{ 'LICENSE.txt' = "$documentationRoot/LICENSE.txt" }
$effectiveBaseRecords = @($baseRecords | ForEach-Object {
    $sourceRelativePath = [string] $_.relativePath
    if ($omittedBasePaths -cnotcontains $sourceRelativePath) {
        $relative = if ($remappedBasePaths.Contains($sourceRelativePath)) {
            [string] $remappedBasePaths[$sourceRelativePath]
        }
        else { $sourceRelativePath }
        [ordered]@{
            relativePath = $relative
            sourceRelativePath = $sourceRelativePath
            bytes = $_.bytes
            sha256 = $_.sha256
        }
    }
})
foreach ($relative in @($omittedBasePaths + @($remappedBasePaths.Keys))) {
    if (@($baseRecords | Where-Object { $_.relativePath -ceq $relative }).Count -ne 1) {
        throw "The modern base-document mapping is missing its historical source: $relative"
    }
}
$overlayRecords = @(Get-PackageRecords $packageOverlay)
$inputUIRecordPath = Join-Path $projectRoot 'Build/interface/clipboard-input-build.json'
Assert-File $inputUIRecordPath 'Clipboard input UI build record'
$inputUIRecord = Get-Content -LiteralPath $inputUIRecordPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($inputUIRecord.protocol -ne 1 -or $inputUIRecord.documentClass -cne 'ClipboardInput' -or
    $inputUIRecord.asset -cne 'package/v240/Interface/ClipboardInput.swf' -or
    (Get-Sha256 (Join-Path $projectRoot $inputUIRecord.asset)) -cne $inputUIRecord.sha256) {
    throw 'Clipboard input UI asset/protocol differs from its build record.'
}
$inputUISources = @('UI/ClipboardInput/ClipboardInput.as', 'tools/input-ui/CompileClipboardInput.java', 'tools/Build-ClipboardInputUI.ps1', 'tools/ClipboardBuildPaths.ps1')
if ((@($inputUIRecord.sources.path | Sort-Object) -join '|') -cne (@($inputUISources | Sort-Object) -join '|')) {
    throw 'Clipboard input UI source manifest is incomplete.'
}
foreach ($source in $inputUIRecord.sources) {
    Assert-SafeRelativePath $source.path 'Clipboard input UI source'
    if ((Get-Sha256 (Join-Path $projectRoot $source.path)) -cne $source.sha256) {
        throw "Clipboard input UI source changed after compilation: $($source.path)"
    }
}
$overlayPaths = @($overlayRecords | ForEach-Object { $_.relativePath } | Sort-Object)
if (($overlayPaths -join '|') -cne (@($approvedOverlayPaths | Sort-Object) -join '|')) {
    throw "The modern overlay must contain exactly its $($approvedOverlayPaths.Count) approved files; found $($overlayPaths.Count)."
}
Assert-NoBundledPatterns @($effectiveBaseRecords + $overlayRecords)
$expectedStageRecords = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($record in @($effectiveBaseRecords + $overlayRecords)) {
    Assert-SafeRelativePath $record.relativePath 'Merged package payload'
    if ($record.relativePath -match '^[^/\\]+\.txt$') {
        throw "Modern deployment text documents must be under Docs/clipboard, not the package root: $($record.relativePath)"
    }
    $expectedStageRecords[$record.relativePath] = $record
}
$mergedStaticFileCount = $expectedStageRecords.Count
$overlayAdditionalFileCount = $mergedStaticFileCount - $effectiveBaseRecords.Count
Write-Host "Validated $($localizationReport.total_keys) localization keys, $($catalogOutputPaths.Count) catalogs, $($englishDocuments.Count) English documents, $($translatedDocuments.Count) translated documents and $($espOutputPaths.Count) ESP tables."

New-Item -ItemType Directory -Path $deploymentsRoot -Force | Out-Null
if (Test-Path -LiteralPath $stageRoot) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
foreach ($generatedFile in @($manifestPath, $buildInfoPath, $archivePath)) {
    if (Test-Path -LiteralPath $generatedFile -PathType Leaf) {
        Remove-Item -LiteralPath $generatedFile -Force
    }
}
New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null

foreach ($record in $effectiveBaseRecords) {
    $destination = Join-Path $stageRoot $record.relativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $packageBase $record.sourceRelativePath) -Destination $destination -Force
}
foreach ($record in $overlayRecords) {
    $destination = Join-Path $stageRoot $record.relativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $packageOverlay $record.relativePath) -Destination $destination -Force
}

$stagedReadmePath = Join-Path $stageRoot "$documentationRoot/Clipboard-ReadMe.txt"
$stagedFilterGuidePath = Join-Path $stageRoot "$documentationRoot/Clipboard-Object-Filtering.txt"
$stagedReadme = Get-Content -LiteralPath $stagedReadmePath -Raw -Encoding UTF8
if ($stagedReadme -notmatch ('(?m)^Clipboard Resurrection ' + [regex]::Escape($product.productVersion) + '\r?$') -or
    $stagedReadme -notmatch '\bOG\b' -or $stagedReadme -notmatch '\bNG\b' -or $stagedReadme -notmatch '\bAE\b' -or
    $stagedReadme -notmatch '(?is)F4SE.{0,100}match.{0,100}(runtime|Fallout 4)' -or
    $stagedReadme -notmatch 'Runtime Database' -or $stagedReadme -notmatch 'f4rd-runtime\.bin') {
    throw 'The staged readme must identify the current product version, runtime families, matching F4SE and external Runtime Database.'
}
$stagedFilterGuide = Get-Content -LiteralPath $stagedFilterGuidePath -Raw -Encoding UTF8
$mcmEnglishLabels = @{}
foreach ($entry in @(Get-Content -LiteralPath (Join-Path $projectRoot 'localization/source/mcm.en.json') -Raw -Encoding UTF8 | ConvertFrom-Json)) {
    $mcmEnglishLabels[[string] $entry.key] = [string] $entry.text
}
foreach ($key in @(
    '$Clipboard_MCM_bAllowAllObjectsToBeImported_Selection_Text',
    '$Clipboard_MCM_bAllowNormallyFilteredObjectsToBeSelectedAndExported_Selection_Text',
    '$Clipboard_MCM_bDisableHavokOnImportedObjects_Balance_Text'
)) {
    if (-not $mcmEnglishLabels.ContainsKey($key) -or [string]::IsNullOrWhiteSpace($mcmEnglishLabels[$key]) -or
        $stagedFilterGuide -notmatch [regex]::Escape($mcmEnglishLabels[$key])) {
        throw "The staged object-filter guide does not use the current English MCM label: $key"
    }
}
foreach ($relative in $englishDocuments) {
    $documentText = Get-Content -LiteralPath (Join-Path $stageRoot $relative) -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($documentText) -or
        $documentText -match '(?i)\b(release candidate|test candidate|in-game certification|testing status|pending gameplay|unverified|not been tested)\b') {
        throw "Deployment documentation must contain end-user guidance without candidate or testing-status language: $relative"
    }
}

$stageScriptSource = Join-Path $stageRoot 'Scripts\Source\User'
$stageScripts = Join-Path $stageRoot 'Scripts'
$stagePlugins = Join-Path $stageRoot 'F4SE\Plugins'
$stageLicenses = Join-Path $stageRoot 'Docs\clipboard\Licenses'
foreach ($directory in @($stageScriptSource, $stageScripts, $stagePlugins, $stageLicenses)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

foreach ($source in @($papyrusMetadata.sourceFiles)) {
    Copy-Item -LiteralPath (Join-Path $papyrusSource ([string] $source.file)) -Destination $stageScriptSource
    $relative = 'Scripts/Source/User/' + ([string] $source.file).Replace('\', '/')
    $expectedStageRecords.Add($relative, [ordered]@{ relativePath = $relative; bytes = [Int64] $source.bytes; sha256 = [string] $source.sha256 })
}
foreach ($output in @($papyrusMetadata.outputs)) {
    Copy-Item -LiteralPath (Join-Path $papyrusOutput ([string] $output.file)) -Destination $stageScripts
    $relative = 'Scripts/' + ([string] $output.file).Replace('\', '/')
    $expectedStageRecords.Add($relative, [ordered]@{ relativePath = $relative; bytes = [Int64] $output.bytes; sha256 = [string] $output.sha256 })
}
Copy-Item -LiteralPath $nativeDll -Destination (Join-Path $stagePlugins $dllName)
$relative = 'F4SE/Plugins/' + $dllName
$expectedStageRecords.Add($relative, [ordered]@{ relativePath = $relative; bytes = [Int64] $nativeMetadata.dll.bytes; sha256 = [string] $nativeMetadata.dll.sha256 })
Copy-Item -LiteralPath $commonLibLicense -Destination (Join-Path $stageLicenses 'CommonLibF4RD.txt')
$relative = 'Docs/clipboard/Licenses/CommonLibF4RD.txt'
$expectedStageRecords.Add($relative, [ordered]@{ relativePath = $relative; bytes = (Get-Item -LiteralPath $commonLibLicense).Length; sha256 = Get-Sha256 $commonLibLicense })
foreach ($copyright in $vcpkgCopyrights) {
    Copy-Item -LiteralPath $copyright.path -Destination (Join-Path $stageLicenses ("vcpkg-$($copyright.package).txt"))
    $relative = "Docs/clipboard/Licenses/vcpkg-$($copyright.package).txt"
    $expectedStageRecords.Add($relative, [ordered]@{ relativePath = $relative; bytes = (Get-Item -LiteralPath $copyright.path).Length; sha256 = Get-Sha256 $copyright.path })
}

$stagePsc = @(Get-ChildItem -LiteralPath $stageScriptSource -File -Filter '*.psc')
$stagePex = @(Get-ChildItem -LiteralPath $stageScripts -File -Filter '*.pex')
if ($stagePsc.Count -ne 16 -or $stagePex.Count -ne 16) {
    throw "Staged script count mismatch: PSC=$($stagePsc.Count) PEX=$($stagePex.Count)"
}

$slotRoot = Join-Path $stageRoot 'F4SE\Plugins\Clipboard'
$allSlotDirectories = @(Get-ChildItem -LiteralPath $slotRoot -Directory)
$slots = @($allSlotDirectories | Where-Object { $_.Name -match '^\d+$' })
$localizationDirectories = @($allSlotDirectories | Where-Object { $_.Name -ceq 'Localization' })
if ($allSlotDirectories.Count -ne 501 -or $slots.Count -ne 500 -or $localizationDirectories.Count -ne 1) {
    throw "Staged slot topology must contain exactly 500 numeric slots and the Localization directory; found $($allSlotDirectories.Count) total and $($slots.Count) numeric."
}
$slotByNumber = @{}
foreach ($slot in $slots) {
    $number = [int] $slot.Name
    if ($slotByNumber.ContainsKey($number)) {
        throw "Staged slot number is duplicated: $number"
    }
    $slotByNumber.Add($number, $slot)
}
foreach ($number in 1..500) {
    if (-not $slotByNumber.ContainsKey($number)) {
        throw "Staged slot directory is missing: $number"
    }
    Assert-File (Join-Path $slotByNumber[$number].FullName 'PLACEHOLDER') "Slot $number placeholder"
}
$placeholderCount = @(Get-ChildItem -LiteralPath $slotRoot -Recurse -File -Filter 'PLACEHOLDER').Count
if ($placeholderCount -ne 500) {
    throw "Staged placeholder count is $placeholderCount, expected 500."
}

$stagedDlls = @(Get-ChildItem -LiteralPath $stageRoot -Recurse -File -Filter '*.dll')
if ($stagedDlls.Count -ne 1 -or $stagedDlls[0].FullName -ne (Join-Path $stagePlugins $dllName)) {
    throw 'The v240 package must contain exactly F4SE\Plugins\clipboard.dll.'
}
if (Get-ChildItem -LiteralPath $stageRoot -Recurse -File -Filter 'Clipboard.swf') {
    throw 'Clipboard.swf is deprecated and must not be staged.'
}
if (Get-ChildItem -LiteralPath $stageRoot -Recurse -File | Where-Object { $_.Name -like '*.dll.241' }) {
    throw 'The disabled historical .dll.241 payload must not be staged.'
}
if (Get-ChildItem -LiteralPath $stageRoot -Recurse -File -Filter '*.pdb') {
    throw 'PDB files must remain with local build records, not in the deployment.'
}
if (Get-ChildItem -LiteralPath $stageRoot -Recurse -File | Where-Object { $_.Name -like 'f4rd-runtime*.bin' }) {
    throw 'Runtime Database is an external dependency and must not be copied into this package.'
}
if (Get-ChildItem -LiteralPath $stageRoot -Recurse -File -Filter 'meta.ini') {
    throw 'Mod Organizer meta.ini must not be included in a release package.'
}
if (Get-ChildItem -LiteralPath $stageRoot -File -Filter '*.txt') {
    throw 'Deployment-root text files must be moved under Docs/clipboard.'
}

$licenseCount = 1 + $vcpkgCopyrights.Count
$nativeDllCount = 1
$expectedFileCount = $mergedStaticFileCount + @($papyrusMetadata.sourceFiles).Count + @($papyrusMetadata.outputs).Count + $nativeDllCount + $licenseCount
$stagedRecords = @(Get-PackageRecords $stageRoot)
$fileCount = $stagedRecords.Count
if ($fileCount -ne $expectedFileCount -or $fileCount -ne $expectedStageRecords.Count) {
    throw "Expected $expectedFileCount staged files from the base/overlay union and recorded artifacts; found $fileCount."
}
foreach ($record in $stagedRecords) {
    if (-not $expectedStageRecords.ContainsKey($record.relativePath)) {
        throw "The staged package contains an unexpected file: $($record.relativePath)"
    }
    $expected = $expectedStageRecords[$record.relativePath]
    if ($record.sha256 -ne $expected.sha256 -or [Int64] $record.bytes -ne [Int64] $expected.bytes) {
        throw "The staged package differs from the verified input: $($record.relativePath)"
    }
}

# Check generated bytes again at their final location and repeat the supported
# migration comparison against the exact staged ESP and staged English tables.
$stagedLocalizationCheck = Invoke-StageCheck $pythonExecutable @(
    $localizationBuilder, '--root', $projectRoot, '--output', $stageRoot, '--check'
) 'Staged localization/catalog documentation validation' -QuietOutput
$stagedLocalizationReport = ($stagedLocalizationCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
Assert-SameOutputManifest $localizationReport $stagedLocalizationReport 'Staged catalogs and documentation'
Assert-GeneratedFiles $stageRoot $localizationReport 'Staged catalogs and documentation'
$stagedEspTableCheck = Invoke-StageCheck $pythonExecutable @(
    $espLocalizationTool, 'generate', '--output', $stageRoot, '--check'
) 'Staged ESP language-table validation' -QuietOutput
$stagedEspTableReport = ($stagedEspTableCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
Assert-SameOutputManifest $espTableReport $stagedEspTableReport 'Staged ESP language tables'
Assert-GeneratedFiles $stageRoot $espTableReport 'Staged ESP language tables'
$stagedEspMigrationCheck = Invoke-StageCheck $pythonExecutable @(
    $espLocalizationTool, 'verify', '--esp', (Join-Path $stageRoot 'Clipboard.esp')
) 'Staged ESP non-text migration verification' -QuietOutput
$stagedEspMigrationResult = ($stagedEspMigrationCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
if ([string] $stagedEspMigrationResult.localized_esp_sha256 -ne [string] $espMigrationResult.localized_esp_sha256 -or
    [string] $stagedEspMigrationResult.source_esp_sha256 -ne [string] $espMapping.source_esp_sha256) {
    throw 'The staged ESP changed from the captured xEdit migration.'
}

foreach ($record in $localizationInputRecords) {
    $path = Join-Path $projectRoot $record.relativePath
    if ((Get-Sha256 $path) -ne $record.sha256 -or (Get-Item -LiteralPath $path).Length -ne [Int64] $record.bytes) {
        throw "Localization source/tool input changed during staging: $($record.relativePath)"
    }
}
if ((Get-RecordsTreeHash @(Get-PackageRecords $packageBase)) -ne (Get-RecordsTreeHash $baseRecords) -or
    (Get-RecordsTreeHash @(Get-PackageRecords $packageOverlay)) -ne (Get-RecordsTreeHash $overlayRecords)) {
    throw 'The package base or modern overlay changed during staging.'
}

$manifestLines = @(Get-FilesOrdinal -Root $stageRoot -Recurse | ForEach-Object {
        $relative = Get-RelativePath $stageRoot $_.FullName
        '{0} *{1}' -f (Get-Sha256 $_.FullName), $relative
    })
[IO.File]::WriteAllLines($manifestPath, [string[]] $manifestLines, $utf8NoBom)

$licenseRecords = @(Get-FilesOrdinal -Root $stageLicenses | ForEach-Object {
    [ordered]@{
        file = $_.Name
        bytes = $_.Length
        sha256 = Get-Sha256 $_.FullName
    }
})
$baseTreeText = ($baseRecords | ForEach-Object {
    "$($_.sha256.ToLowerInvariant())  $($_.relativePath)`n"
}) -join ''

$buildInfo = [ordered]@{
    schemaVersion = 2
    internalBuild = $internalBuild
    stagedUtc = (Get-Date).ToUniversalTime().ToString('o')
    result = 'success'
    productVersion = $product.productVersion
    productVersionSource = $product
    releaseLabel = $product.releaseLabel
    fallout4Runtime = '1.11.240.0'
    f4seVersion = '0.7.9'
    runtimeEligibility = $nativeMetadata.runtimeEligibility
    versionFieldsDescribe = 'productVersion identifies Clipboard; fallout4Runtime/f4seVersion identify the available build/test installation, not an executable patch whitelist'
    nativeFlavor = 'Release240'
    activeDllName = $dllName
    fileCount = $fileCount
    inputUI = $inputUIRecord
    staticBase = [ordered]@{
        source = $packageBase
        fileCount = $baseFileCount
        treeSha256 = Get-TextSha256 $baseTreeText
        files = $baseRecords
        omittedPaths = $omittedBasePaths
        remappedPaths = $remappedBasePaths
        effectiveFileCount = $effectiveBaseRecords.Count
        effectiveFiles = $effectiveBaseRecords
        effectiveTreeSha256 = Get-RecordsTreeHash $effectiveBaseRecords
    }
    runtimeOverlay = [ordered]@{
        root = $packageOverlay
        fileCount = $overlayRecords.Count
        additionalFileCount = $overlayAdditionalFileCount
        approvedPaths = @($approvedOverlayPaths | Sort-Object)
        files = $overlayRecords
        treeSha256 = Get-RecordsTreeHash $overlayRecords
        stagedFilesMatch = $true
        source = $v240Readme
        sha256 = Get-Sha256 $v240Readme
        stagedSha256 = Get-Sha256 $stagedReadmePath
        filterGuide = [ordered]@{
            source = $v240FilterGuide
            sha256 = Get-Sha256 $v240FilterGuide
            stagedSha256 = Get-Sha256 $stagedFilterGuidePath
        }
    }
    localization = [ordered]@{
        sourceLocale = 'en'
        translatedLocales = $translatedLocales
        localeAliases = $localeAliases
        fallbackLocales = @()
        sameNativeAndPapyrusAcrossLanguages = $true
        documentation = [ordered]@{
            root = $documentationRoot
            englishPaths = $englishDocuments
            translationStatus = [string] $documentationLayout.translation_status
            enabledLocales = $documentationLocales
            translatedPaths = $translatedDocuments
        }
        catalogFileCount = $catalogOutputPaths.Count
        translatedDocumentCount = $translatedDocuments.Count
        espTableFileCount = $espOutputPaths.Count
        inputs = $localizationInputRecords
        inputsTreeSha256 = Get-RecordsTreeHash $localizationInputRecords
        catalogsAndDocuments = $localizationReport
        espTables = $espTableReport
        espMigration = $stagedEspMigrationResult
        stagedOutputsMatch = $true
        python = [ordered]@{
            path = $pythonExecutable
            bytes = (Get-Item -LiteralPath $pythonExecutable).Length
            sha256 = Get-Sha256 $pythonExecutable
        }
        certification = 'Static catalog, placeholder, MCM structure, ESP non-text migration and staged-byte checks passed. Display and glyph coverage for all languages, existing saves, localized input and language switching require live testing.'
    }
    nativeBuild = [ordered]@{
        metadata = $nativeMetadataPath
        metadataSha256 = Get-Sha256 $nativeMetadataPath
        builtUtc = ([DateTime] $nativeMetadata.builtUtc).ToUniversalTime().ToString('o')
        dllSha256 = Get-Sha256 (Join-Path $stagePlugins $dllName)
        pdbRetainedLocally = $nativePdb
        hostChecks = 'passed'
        ctestCount = [int] $nativeMetadata.hostChecks.ctestCount
        hostTestExecutables = $nativeMetadata.hostChecks.executables
        papyrusContract = $papyrusContract.Output -join [Environment]::NewLine
        compiledEligibility = $pluginMetadataResult
        compiledEligibilityVerifier = [ordered]@{
            path = $metadataVerifier
            sha256 = Get-Sha256 $metadataVerifier
        }
    }
    papyrusBuild = [ordered]@{
        gate = 'separate verified input'
        compiledByThisHelper = $false
        reuse = 'hash-verified maintained PSC and PEX outputs compiled with Modern079 against the verified official F4SE 0.7.9 API reference; one shared Papyrus contract accompanies the OG/NG/AE DLL'
        metadata = $papyrusMetadataPath
        metadataSha256 = Get-Sha256 $papyrusMetadataPath
        sourceTarget = [string] $papyrusMetadata.target
        buildTarget = [string] $papyrusMetadata.buildTarget
        sourceFallout4Runtime = [string] $papyrusMetadata.fallout4Runtime
        sourceF4seVersion = [string] $papyrusMetadata.f4seVersion
        f4seReference = $papyrusReferenceResult
        referenceVerifier = [ordered]@{
            path = $papyrusReferenceVerifier
            sha256 = Get-Sha256 $papyrusReferenceVerifier
        }
        sourceCount = 16
        outputCount = 16
        runtimeRegistrationCertification = 'Predecessor logs confirm only their own eager IDs and six Papyrus structure descriptors, not cross-family engine ABI validation. The current native/script contract is checked by Test-V240PapyrusContract.ps1. This exact staged DLL/PEX set requires fresh loader and registration confirmation on each family.'
    }
    runtimeDatabase = [ordered]@{
        externalDependency = $true
        packaged = $false
        pathValidated = $runtimeDatabasePath
        sha256 = [string] $nativeMetadata.runtimeDatabase.sha256
        recordCount = [UInt64] $nativeMetadata.runtimeDatabase.recordCount
        validationResult = 'passed at build and staging'
        sourceInventory = $runtimeSymbolsResult
    }
    commonLibF4RD = [ordered]@{
        upstreamCommit = [string] $nativeMetadata.commonLibF4RD.upstreamCommit
        normalizedTextTreeSha256 = [string] $nativeMetadata.commonLibF4RD.normalizedTextTreeSha256
        upstreamNormalizedTextTreeSha256 = [string] $nativeMetadata.commonLibF4RD.upstreamNormalizedTextTreeSha256
        localPatches = @($nativeMetadata.commonLibF4RD.localPatches)
        sourceState = [string] $nativeMetadata.commonLibF4RD.sourceState
        integrityResult = 'passed at build and staging'
        stagingVerifierOutput = $integrity.Output -join [Environment]::NewLine
    }
    notices = $licenseRecords
    clipboardSwf = 'deprecated; intentionally absent'
    stage = $stageRoot
    manifest = $manifestPath
    manifestSha256 = Get-Sha256 $manifestPath
    stageLog = $stageLog
    helper = [ordered]@{
        path = $PSCommandPath
        sha256 = Get-Sha256 $PSCommandPath
        versionReader = [ordered]@{
            path = $versionReader
            sha256 = Get-Sha256 $versionReader
        }
        entryPoint = [ordered]@{
            path = $stageEntryPoint
            sha256 = Get-Sha256 $stageEntryPoint
        }
    }
    inGameCertification = 'AE predecessors retain only their recorded scenarios. This single DLL attempts Runtime Database resolution for OG, NG and AE with runtime-selected layouts and no executable patch whitelist. Eligibility and dependency resolution do not certify engine ABI or behavior. OG and NG have not been tested in game; this exact staged DLL/PEX set requires fresh F4SE load, Papyrus registration and behavior tests on every family. The available fixture remains 1.11.240/F4SE 0.7.9. No in-game result is claimed for this exact staged package.'
}

if ($Archive) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory(
        $stageRoot,
        $archivePath,
        [IO.Compression.CompressionLevel]::Optimal,
        $false
    )
    $buildInfo['archive'] = $archivePath
    $buildInfo['archiveSha256'] = Get-Sha256 $archivePath
}
[IO.File]::WriteAllText(
    $buildInfoPath,
    ($buildInfo | ConvertTo-Json -Depth 10) + [Environment]::NewLine,
    $utf8NoBom
)

[IO.File]::AppendAllText(
    $stageLog,
    "Staged $fileCount files at $stageRoot`r`nManifest: $manifestPath`r`n",
    $utf8NoBom
)
Write-Output "Package staging succeeded: $stageRoot"
Write-Output "Files: $fileCount"
Write-Output "Manifest: $manifestPath"
Write-Output "Build record: $buildInfoPath"
if ($Archive) {
    Write-Output "Archive: $archivePath"
    Write-Output "Archive SHA-256: $($buildInfo['archiveSha256'])"
}
Write-Output 'No game or mod-manager directory was modified.'
Write-ClipboardBuildRecord (Join-Path $projectRoot 'tools/Clipboard.PublishedBuild.json') ([ordered]@{
    internalBuild = $internalBuild
    publishedUtc = (Get-Date).ToUniversalTime().ToString('o')
})
Write-Output "Published Clipboard internal build $internalBuild."
} finally { $publicationLock.ReleaseMutex(); $publicationLock.Dispose() }
