<#
.SYNOPSIS
Creates a local Fallout 4 OG/NG/AE Clipboard test deployment and optional ZIP.

.DESCRIPTION
Stages the reviewed Release DLL, authored assets, generated language/UI files, and the separately
verified Current Papyrus build (compiled against the verified official
F4SE 0.7.9 API reference) under build/stage. With -Archive, publishes the verified set to dist/build-N. The helper rechecks all recorded hashes, the vendored
CommonLibF4RD snapshot, and the installed Runtime Database. It never deploys
to the game or mod-manager directories and never compiles Papyrus.
#>

#Requires -Version 5.1

[CmdletBinding()]
param(
    [switch] $Archive,
    [switch] $Release,
    [string] $ReleaseVersion,
    [string] $PythonPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).TrimEnd('\')
. (Join-Path $PSScriptRoot 'ClipboardBuildIdentity.ps1')
. (Join-Path $PSScriptRoot 'ClipboardBuildPaths.ps1')
. (Join-Path $PSScriptRoot 'ClipboardPackageNames.ps1')
$publicationLock = Enter-ClipboardBuildLock $projectRoot
try {
$versionReader = Join-Path $projectRoot 'tools\build\Get-ClipboardVersion.ps1'
$product = & $versionReader -ProjectRoot $projectRoot
$internalBuild = Get-ClipboardInternalBuild $projectRoot -CheckScripts
$packageNames = Get-ClipboardPackageNames -Product $product -InternalBuild $internalBuild -Release:$Release -ReleaseVersion $ReleaseVersion
$authoredAssets = Join-Path $projectRoot 'assets'
$generatedLocalization = Join-Path $projectRoot 'build/generated/localization'
$generatedUI = Join-Path $projectRoot 'build/generated/ui'
$recipePath = Join-Path $projectRoot 'config/package.json'
$recipe = Get-Content -LiteralPath $recipePath -Raw -Encoding UTF8 | ConvertFrom-Json
$recipeHash = (Get-FileHash -LiteralPath $recipePath -Algorithm SHA256).Hash
$packagePlanner = Join-Path $PSScriptRoot 'PackagePlan.py'
$documentationRoot = 'Docs/clipboard'
$englishDocuments = @(
    "$documentationRoot/Clipboard-ReadMe.txt", "$documentationRoot/Clipboard-Object-Filtering.txt",
    "$documentationRoot/Clipboard-Localization.txt", "$documentationRoot/CHANGELOG.txt"
)
$englishDocumentationRoot = Join-Path $projectRoot 'docs/user/en'
$englishReadme = Join-Path $englishDocumentationRoot 'Clipboard-ReadMe.txt'
$englishFilterGuide = Join-Path $englishDocumentationRoot 'Clipboard-Object-Filtering.txt'
$englishLocalizationGuide = Join-Path $englishDocumentationRoot 'Clipboard-Localization.txt'
$englishChangelog = Join-Path $englishDocumentationRoot 'CHANGELOG.txt'
$documentationLayoutPath = Join-Path $projectRoot 'localization/metadata/docs-layout.json'
$localizedEsp = Join-Path $authoredAssets 'Clipboard.esp'
$localizationBuilder = Join-Path $projectRoot 'tools\localization\Build-Localization.py'
$espLocalizationTool = Join-Path $projectRoot 'tools\localization\ClipboardLocalizationEsp.py'
$pythonResolver = Join-Path $projectRoot 'tools\build\Get-ClipboardPython.ps1'
$espMappingPath = Join-Path $projectRoot 'localization\metadata\esp-map.json'
$pythonExecutable = & $pythonResolver -PythonPath $PythonPath
$locales = @($recipe.locales)
$translatedLocales = @($locales | Where-Object { $_ -cne 'en' })
$localeAliases = [ordered]@{}
foreach ($alias in $recipe.localeAliases.PSObject.Properties) { $localeAliases[$alias.Name] = [string] $alias.Value }
$fileLocales = @($locales) + @($localeAliases.Keys)
$translatedDocuments = @()
$catalogOutputPaths = @($locales | ForEach-Object { "F4SE/Plugins/Clipboard/Localization/$_.tsv" }) + @(
    $fileLocales | ForEach-Object { "Interface/Translations/Clipboard_$_.txt" }
)
$espOutputPaths = @($fileLocales | ForEach-Object {
    $locale = $_
    @('STRINGS', 'DLSTRINGS', 'ILSTRINGS') | ForEach-Object { "Strings/Clipboard_$locale.$_" }
})
$nativeRoot = $projectRoot
$nativeBinaryRoot = Join-Path $projectRoot 'build\native\vs2026'
$nativeMetadataPath = Join-Path $projectRoot 'build\metadata\native\native-build.json'
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
$snapshotPath = Join-Path $projectRoot 'config/dependencies/CommonLibF4RD.snapshot.json'
$papyrusSource = Join-Path $projectRoot 'src\papyrus'
$papyrusOutput = Join-Path $projectRoot 'build\papyrus\current\Scripts'
$papyrusMetadataPath = Join-Path $projectRoot 'build\papyrus\current\papyrus-build.json'
$papyrusReferenceVerifier = Join-Path $projectRoot 'tests\build\Test-F4SECurrentReference.ps1'
$verifier = Join-Path $projectRoot 'tests\native\Test-NativeDependencies.ps1'
$metadataVerifier = Join-Path $projectRoot 'tests\native\Test-ClipboardMetadata.ps1'
$runtimeSymbolsVerifier = Join-Path $projectRoot 'tests\native\Test-RuntimeSymbols.ps1'
$stageEntryPoint = Join-Path $projectRoot 'tools\build\Stage-Clipboard.ps1'
$commonLibLicense = Join-Path $projectRoot 'external\CommonLibF4RD\LICENSE'
$vcpkgShare = Join-Path $nativeBinaryRoot 'vcpkg_installed\x64-windows-static-md\share'
$deploymentsRoot = Join-Path $projectRoot 'dist'
$logRoot = Join-Path $projectRoot 'build\logs'
$stageLog = Join-Path $logRoot 'package-clipboard-test-candidate.log'
$dllName = 'clipboard.dll'
$deploymentName = $packageNames.deploymentName
$candidateRoot = Join-Path $projectRoot ('build/stage/' + $internalBuild)
$publicationRoot = Join-Path $deploymentsRoot ('build-' + $internalBuild)
$stageRoot = Join-Path $candidateRoot $deploymentName
$manifestPath = Join-Path $candidateRoot ($deploymentName + '.manifest.sha256')
$buildInfoPath = Join-Path $candidateRoot ($deploymentName + '.build.json')
$archivePath = Join-Path $candidateRoot $packageNames.archiveName
$publishedStage = Join-Path $publicationRoot $deploymentName
$publishedManifest = Join-Path $publicationRoot ([IO.Path]::GetFileName($manifestPath))
$publishedBuildInfo = Join-Path $publicationRoot ([IO.Path]::GetFileName($buildInfoPath))
$publishedArchive = Join-Path $publicationRoot ([IO.Path]::GetFileName($archivePath))
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
    $allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build/stage')) + '\'
    if (-not $resolved.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing staging operation outside $allowed`: $resolved"
    }
    $parent = $resolved
    while ($parent -and $parent -ine $projectRoot) {
        if ((Test-Path -LiteralPath $parent) -and ((Get-Item -LiteralPath $parent).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Refusing staging operation through a link: $parent"
        }
        $parent = Split-Path -Parent $parent
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
        $Metadata.buildTarget -ne 'Current' -or
        [int] $Metadata.schemaVersion -lt 2 -or
        $Metadata.result -ne 'success' -or
        [int] $Metadata.compilerExitCode -ne 0 -or
        $Metadata.target -ne 'Fallout 4 1.11.240 baseline' -or
        $Metadata.fallout4Runtime -ne '1.11.240.0' -or
        $Metadata.f4seVersion -ne '0.7.9' -or
        [int] $Metadata.sourceCount -ne 16 -or
        @($Metadata.sourceFiles).Count -ne 16 -or
        @($Metadata.outputs).Count -ne 16) {
        throw 'Papyrus metadata is not a successful 16-script Current/F4SE 0.7.9 build. Run Build-Papyrus.ps1 -Target Current before staging; legacy 0.7.8 outputs cannot be reused.'
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
            throw "The Papyrus build's F4SE reference differs from the verified current reference: $field. Rebuild Current before staging."
        }
    }
}

function Assert-RecordedTree(
    [string] $Root,
    [object[]] $Records,
    [string] $ExpectedTreeHash,
    [string] $Description,
    [switch] $NativeInputs
) {
    if ($NativeInputs) {
        if ([IO.Path]::GetFullPath($Root).TrimEnd('\') -ine [IO.Path]::GetFullPath($nativeRoot).TrimEnd('\')) {
            throw 'Native input enumeration requires the project root.'
        }
        $currentFiles = @(Get-ClipboardNativeInputFiles $Root)
    } else { $currentFiles = @(Get-FilesOrdinal -Root $Root -Recurse) }
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

foreach ($directory in @($authoredAssets, $generatedLocalization, $generatedUI, $nativeRoot, $papyrusSource, $papyrusOutput, $vcpkgShare)) {
    Assert-Directory $directory 'Required current package input directory'
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
    $englishReadme,
    $englishFilterGuide,
    $englishLocalizationGuide,
    $englishChangelog,
    $documentationLayoutPath,
    $localizedEsp,
    $localizationBuilder,
    $espLocalizationTool,
    $pythonResolver,
    $pythonExecutable,
    $espMappingPath,
    $currentPowerShell
)) {
    Assert-File $file 'Required current package input'
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
    if ([string] $_.source -cne "docs/user/en/$filename") {
        throw "Documentation source must match its approved English source path: $filename"
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
    [int] $nativeMetadata.schemaVersion -lt 3 -or
    $nativeMetadata.result -ne 'success' -or
    $nativeMetadata.flavor -ne 'Release' -or
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
    throw 'Native metadata is not a successful reviewed Release/F4SE 0.7.9 build.'
}
$expectedCtests = @(
    'clipboard_commonlib_integrity', 'clipboard_runtime_database_preflight',
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
    throw 'clipboard.dll does not match the Release native metadata.'
}
if ((Get-Sha256 $nativePdb) -ne [string] $nativeMetadata.pdb.sha256) {
    throw 'clipboard.pdb does not match the Release native metadata.'
}
foreach ($logRecord in @(
    $nativeMetadata.logs.text,
    $nativeMetadata.logs.nativeBinlog,
    $nativeMetadata.logs.hostChecksBinlog
)) {
    $logPath = [string] $logRecord.path
    Assert-File $logPath 'Recorded native build log'
    if ((Get-Sha256 $logPath) -ne [string] $logRecord.sha256 -or
        (Get-Item -LiteralPath $logPath).Length -ne [Int64] $logRecord.bytes) {
        throw "Recorded native build log changed after compilation: $logPath"
    }
}

$nativeTreeCheck = @{
    Root = $nativeRoot
    Records = [object[]] @($nativeMetadata.inputs.nativeTree)
    ExpectedTreeHash = [string] $nativeMetadata.inputs.nativeTreeSha256
    Description = 'native source tree'
    NativeInputs = $true
}
if (-not $nativeMetadata.inputs.PSObject.Properties['nativeTreeIncludedPaths'] -or
    (@($nativeMetadata.inputs.nativeTreeIncludedPaths) -join ',') -cne (@(Get-ClipboardNativeInputPaths) -join ',') -or
    -not $nativeMetadata.inputs.PSObject.Properties['nativeTreeExcludedPaths'] -or
    (@($nativeMetadata.inputs.nativeTreeExcludedPaths) -join ',') -cne 'CMakeUserPresets.json') {
    throw 'Native source attestation must record the maintained input scope and exclude local CMake user presets.'
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
    Assert-File ([string] $record.path) 'Recorded native build script'
    if ((Get-Sha256 ([string] $record.path)) -ne [string] $record.sha256) {
        throw "Recorded native build script changed after compilation: $($record.path)"
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

if (Test-Path -LiteralPath (Join-Path $authoredAssets 'Scripts')) {
    throw 'Authored assets unexpectedly contains Scripts; Papyrus must remain a separate recorded input.'
}
if (Get-ChildItem -LiteralPath $authoredAssets -Recurse -File -Filter '*.dll') {
    throw 'Authored assets unexpectedly contains a DLL.'
}

$expectedLicensePackages = @($recipe.dependencyNotices.packages | Sort-Object)
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
    throw "vcpkg notice set differs from the reviewed Release dependency set: $($actualLicensePackages -join ', ')"
}

$localizationCheck = Invoke-StageCheck $pythonExecutable @(
    $localizationBuilder, '--root', $projectRoot, '--output', $generatedLocalization, '--check'
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
Assert-GeneratedFiles $generatedLocalization $localizationReport 'Catalog/documentation overlay'

$espTableCheck = Invoke-StageCheck $pythonExecutable @(
    $espLocalizationTool, 'generate', '--output', $generatedLocalization, '--check'
) 'ESP language-table validation' -QuietOutput
$espTableReport = ($espTableCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
Assert-OutputManifest $espTableReport $espOutputPaths 'Generated ESP table manifest'
if ((@($espTableReport.generated_locales) -join ',') -cne ($fileLocales -join ',') -or
    (@($espTableReport.locale_aliases.PSObject.Properties.Name) -join ',') -cne 'cn' -or
    $espTableReport.locale_aliases.cn -cne 'zhhant') {
    throw 'ESP table generation did not declare the reviewed locale set.'
}
Assert-GeneratedFiles $generatedLocalization $espTableReport 'ESP table overlay'

$espMigrationCheck = Invoke-StageCheck $pythonExecutable @(
    $espLocalizationTool, 'verify', '--esp', $localizedEsp, '--strings', (Join-Path $generatedLocalization 'Strings')
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
    'localization/metadata/esp-map.json', 'localization/metadata/docs-layout.json', 'assets/Clipboard.esp',
    'tests/fixtures/esp/Clipboard-inline.esp', 'tests/fixtures/esp/Clipboard.esp', 'localization/metadata/esp-revision.json',
    'tools/localization/Build-Localization.py', 'tools/localization/ClipboardLocalizationEsp.py', 'tools/build/Get-ClipboardPython.ps1',
    'config/package.json', 'tools/build/PackagePlan.py', 'tools/build/Build-GeneratedAssets.ps1'
)) { [void] $localizationInputPaths.Add($relative) }
$localizationInputRecords = @($localizationInputPaths | Sort-Object | ForEach-Object {
    $path = Join-Path $projectRoot $_
    Assert-File $path 'Localization source/tool input'
    [ordered]@{ relativePath = $_; bytes = (Get-Item -LiteralPath $path).Length; sha256 = Get-Sha256 $path }
})

$authoredRecords = @(Get-PackageRecords $authoredAssets)
$generatedRecords = @(Get-PackageRecords $generatedLocalization)
$generatedPaths = @($generatedRecords.relativePath | Sort-Object)
$approvedGeneratedPaths = @($catalogOutputPaths + $translatedDocuments + $espOutputPaths | Sort-Object)
if (($generatedPaths -join '|') -cne ($approvedGeneratedPaths -join '|')) {
    throw 'Generated localization inventory differs from the reviewed outputs.'
}
$inputUIRecordPath = Join-Path $projectRoot 'build/interface/clipboard-input-build.json'
Assert-File $inputUIRecordPath 'Clipboard input UI build record'
$inputUIRecord = Get-Content -LiteralPath $inputUIRecordPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($inputUIRecord.protocol -ne 1 -or $inputUIRecord.documentClass -cne 'ClipboardInput' -or
    $inputUIRecord.asset -cne 'build/generated/ui/Interface/ClipboardInput.swf' -or
    (Get-Sha256 (Join-Path $projectRoot $inputUIRecord.asset)) -cne $inputUIRecord.sha256) {
    throw 'Clipboard input UI asset/protocol differs from its build record.'
}
$inputUISources = @('src/ui/ClipboardInput/ClipboardInput.as', 'tools/build/input-ui/CompileClipboardInput.java', 'tools/build/Build-ClipboardInputUI.ps1', 'tools/build/ClipboardBuildPaths.ps1')
if ((@($inputUIRecord.sources.path | Sort-Object) -join '|') -cne (@($inputUISources | Sort-Object) -join '|')) {
    throw 'Clipboard input UI source manifest is incomplete.'
}
foreach ($source in $inputUIRecord.sources) {
    Assert-SafeRelativePath $source.path 'Clipboard input UI source'
    if ((Get-Sha256 (Join-Path $projectRoot $source.path)) -cne $source.sha256) {
        throw "Clipboard input UI source changed after compilation: $($source.path)"
    }
}
$planCheck = Invoke-StageCheck $pythonExecutable @($packagePlanner, '--root', $projectRoot) 'Package recipe resolution' -QuietOutput
$packagePlan = ($planCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
if ($packagePlan.recipeSha256 -ine $recipeHash) { throw 'Package recipe changed during planning' }
$expectedStageRecords = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($record in $packagePlan.files) {
    Assert-SafeRelativePath $record.relativePath 'Package recipe destination'
    if ($record.relativePath -match '^[^/\\]+\.txt$') { throw 'Root text documents are not package inputs' }
    $expectedStageRecords.Add($record.relativePath, $record)
}
# Recipe assembly must preserve the independent compiled artifact identities.
$compiledRecords = @(
    [pscustomobject]@{ relativePath = 'F4SE/Plugins/clipboard.dll'; bytes = $nativeMetadata.dll.bytes; sha256 = $nativeMetadata.dll.sha256 }
) + @(foreach ($source in $papyrusMetadata.sourceFiles) {
    [pscustomobject]@{ relativePath = 'Scripts/Source/User/' + $source.file.Replace('\', '/'); bytes = $source.bytes; sha256 = $source.sha256 }
}) + @(foreach ($output in $papyrusMetadata.outputs) {
    [pscustomobject]@{ relativePath = 'Scripts/' + $output.file.Replace('\', '/'); bytes = $output.bytes; sha256 = $output.sha256 }
})
foreach ($record in $compiledRecords) {
    if (-not $expectedStageRecords.ContainsKey($record.relativePath) -or
        $expectedStageRecords[$record.relativePath].sha256 -ine $record.sha256 -or
        [long] $expectedStageRecords[$record.relativePath].bytes -ne [long] $record.bytes) {
        throw "Package recipe differs from the compiled artifact contract: $($record.relativePath)"
    }
}
Assert-NoBundledPatterns @($packagePlan.files)
Write-Host "Validated $($localizationReport.total_keys) localization keys and $($expectedStageRecords.Count) package destinations."
Assert-DeploymentPath $candidateRoot
if (Test-Path -LiteralPath $publicationRoot) { throw "Completed publication already exists: $publicationRoot" }
if (Test-Path -LiteralPath $candidateRoot) { Remove-Item -LiteralPath $candidateRoot -Recurse -Force }
New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
$planPath = Join-Path $candidateRoot 'package-plan.json'
[IO.File]::WriteAllText($planPath, ($packagePlan | ConvertTo-Json -Depth 8), $utf8NoBom)
foreach ($record in $packagePlan.files) {
    $destination = Join-Path $stageRoot $record.relativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    if ($null -eq $record.source) { [IO.File]::WriteAllBytes($destination, [byte[]]@()) }
    else {
        Assert-SafeRelativePath $record.source 'Package recipe source'
        Copy-Item -LiteralPath (Join-Path $projectRoot $record.source) -Destination $destination
    }
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
    throw 'The current package must contain exactly F4SE\Plugins\clipboard.dll.'
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

$expectedFileCount = $expectedStageRecords.Count
$stagedRecords = @(Get-PackageRecords $stageRoot)
$fileCount = $stagedRecords.Count
if ($fileCount -ne $expectedFileCount -or $fileCount -ne $expectedStageRecords.Count) {
    throw "Expected $expectedFileCount staged files from the package recipe and recorded artifacts; found $fileCount."
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
if ((Get-RecordsTreeHash @(Get-PackageRecords $authoredAssets)) -ne (Get-RecordsTreeHash $authoredRecords) -or
    (Get-RecordsTreeHash @(Get-PackageRecords $generatedLocalization)) -ne (Get-RecordsTreeHash $generatedRecords)) {
    throw 'Authored assets or generated localization changed during staging.'
}
# Re-resolve every source, including scripts, notices and the UI, after assembly.
$finalPlanCheck = Invoke-StageCheck $pythonExecutable @($packagePlanner, '--root', $projectRoot) 'Final package input identity' -QuietOutput
if (($finalPlanCheck.Output -join "`n") -cne ($planCheck.Output -join "`n")) { throw 'Package inputs changed during staging' }

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
$buildInfo = [ordered]@{
    schemaVersion = 3
    internalBuild = $internalBuild
    stagedUtc = (Get-Date).ToUniversalTime().ToString('o')
    result = 'success'
    productVersion = $product.productVersion
    productVersionSource = $product
    releaseLabel = $product.releaseLabel
    buildKind = $packageNames.buildKind
    releaseVersion = $packageNames.releaseVersion
    fallout4Runtime = '1.11.240.0'
    f4seVersion = '0.7.9'
    runtimeEligibility = $nativeMetadata.runtimeEligibility
    versionFieldsDescribe = 'productVersion identifies Clipboard; fallout4Runtime/f4seVersion identify the available build/test installation, not an executable patch whitelist'
    nativeFlavor = 'Release'
    activeDllName = $dllName
    fileCount = $fileCount
    inputUI = $inputUIRecord
    packageRecipe = [ordered]@{
        path = $recipePath; sha256 = $recipeHash; plan = $packagePlan
        authoredRoot = $authoredAssets; generatedRoot = $generatedLocalization
        stagedFilesMatch = $true
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
        reuse = 'hash-verified maintained PSC and PEX outputs compiled with Current against the verified official F4SE 0.7.9 API reference; one shared Papyrus contract accompanies the OG/NG/AE DLL'
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
        runtimeRegistrationCertification = 'Predecessor logs confirm only their own eager IDs and six Papyrus structure descriptors, not cross-family engine ABI validation. The current native/script contract is checked by Test-PapyrusContract.ps1. This exact staged DLL/PEX set requires fresh loader and registration confirmation on each family.'
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
    stage = $(if ($Archive) { $publishedStage } else { $stageRoot })
    manifest = $(if ($Archive) { $publishedManifest } else { $manifestPath })
    manifestSha256 = Get-Sha256 $manifestPath
    stageLog = $stageLog
    helper = [ordered]@{
        path = $PSCommandPath
        sha256 = Get-Sha256 $PSCommandPath
        packageNaming = [ordered]@{
            path = (Join-Path $PSScriptRoot 'ClipboardPackageNames.ps1')
            sha256 = Get-Sha256 (Join-Path $PSScriptRoot 'ClipboardPackageNames.ps1')
        }
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
    $archiveCheck = Invoke-StageCheck $pythonExecutable @($packagePlanner, '--archive', $archivePath, '--plan', $planPath) 'ZIP inventory, CRC and SHA-256 verification' -QuietOutput
    $buildInfo['archiveVerification'] = ($archiveCheck.Output -join [Environment]::NewLine) | ConvertFrom-Json
    if ($buildInfo.archiveVerification.result -cne 'passed') { throw 'Archive verification failed' }
    $buildInfo['archive'] = $publishedArchive
    $buildInfo['archiveSha256'] = Get-Sha256 $archivePath
}
[IO.File]::WriteAllText(
    $buildInfoPath,
    ($buildInfo | ConvertTo-Json -Depth 10) + [Environment]::NewLine,
    $utf8NoBom
)

if ($Archive) {
    # A published package must remain diagnosable after deleting build/. Retain
    # its matching symbols/attestations before the atomic package publication.
    $retention = & $pythonExecutable (Join-Path $PSScriptRoot 'PreserveBuildDiagnostics.py') --root $projectRoot --record $buildInfoPath
    if ($LASTEXITCODE -ne 0) { throw 'Could not retain publication diagnostics outside build.' }
    $buildInfo['retainedDiagnostics'] = ($retention -join [Environment]::NewLine) | ConvertFrom-Json
    if ($buildInfo.retainedDiagnostics.result -ne 'passed') { throw 'Diagnostic retention failed.' }
    [IO.File]::WriteAllText($buildInfoPath, ($buildInfo | ConvertTo-Json -Depth 10) + [Environment]::NewLine, $utf8NoBom)
    # Publish the complete verified directory in one same-volume rename. A crash
    # before the counter write is recovered by the numbered build record.
    New-Item -ItemType Directory -Path $deploymentsRoot -Force | Out-Null
    $resolvedPublication = [IO.Path]::GetFullPath($publicationRoot)
    $allowedPublication = [IO.Path]::GetFullPath($deploymentsRoot) + '\'
    if (-not $resolvedPublication.StartsWith($allowedPublication, [StringComparison]::OrdinalIgnoreCase) -or
        ((Get-Item -LiteralPath $deploymentsRoot).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe publication path' }
    Assert-DeploymentPath $candidateRoot
    [IO.Directory]::Move($candidateRoot, $resolvedPublication)
    Write-ClipboardBuildRecord (Join-Path $projectRoot 'config/Clipboard.PublishedBuild.json') ([ordered]@{
        internalBuild = $internalBuild; publishedUtc = (Get-Date).ToUniversalTime().ToString('o')
    })
    Write-Output "Published Clipboard internal build $internalBuild`: $publicationRoot"
    Write-Output "Archive: $publishedArchive"
    Write-Output "Archive SHA-256: $($buildInfo['archiveSha256'])"
} else { Write-Output "Validated staging preview: $stageRoot (not published; counter unchanged)" }
[IO.File]::AppendAllText($stageLog, "Verified $fileCount files; publication requested: $Archive`r`n", $utf8NoBom)
Write-Output "Files: $fileCount"
Write-Output 'No game or mod-manager directory was modified.'
} finally { $publicationLock.ReleaseMutex(); $publicationLock.Dispose() }
