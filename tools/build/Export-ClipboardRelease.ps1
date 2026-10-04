# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2018 to 2026 Everett C Sands
# Exports an existing verified numbered ZIP under its public release filename.
# No compilation, restaging, build-number allocation, upload or installation.
#Requires -Version 5.1
[CmdletBinding()]
param([string] $ReleaseVersion)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'ClipboardBuildIdentity.ps1')
. (Join-Path $PSScriptRoot 'ClipboardPackageNames.ps1')
$product = & (Join-Path $PSScriptRoot 'Get-ClipboardVersion.ps1') -ProjectRoot $root
$names = Get-ClipboardPackageNames -Product $product -Release -ReleaseVersion $ReleaseVersion
$buildLock = Enter-ClipboardBuildLock $root
try {
    $deployments = Join-Path $root 'dist'
    # Recognize records made before the stable deployment base name was introduced.
    $legacyName = 'Clipboard v' + $product.productVersion + ' - OG NG AE - Test Candidate'
    $sourceRecords = @(foreach ($publicationRoot in Get-ClipboardPublicationRoots $root) {
        foreach ($baseName in @($product.deploymentName, $legacyName)) {
            $candidate = Join-Path $publicationRoot ($baseName + '.build.json')
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                [pscustomobject]@{ path = $candidate; internalBuild = Read-ClipboardBuildNumber $candidate }
            }
        }
    })
    if ($sourceRecords.Count -eq 0) { throw 'No completed numbered package is available to export.' }
    $sourceRecord = ($sourceRecords | Sort-Object internalBuild -Descending | Select-Object -First 1).path
    $record = Get-Content -LiteralPath $sourceRecord -Raw | ConvertFrom-Json
    if ($record.result -ne 'success' -or $record.productVersion -cne $product.productVersion) {
        throw 'A successful numbered package for the current product version is required.'
    }
    $testNames = Get-ClipboardPackageNames -Product $product -InternalBuild $record.internalBuild
    $allowedArchives = @($testNames.archiveName, $names.archiveName, ($legacyName + ' - Build ' + $record.internalBuild + '.zip'))
    $numberedArchive = [IO.Path]::GetFullPath($record.archive)
    if ((Split-Path -Parent $numberedArchive) -ine (Split-Path -Parent $sourceRecord) -or
        $allowedArchives -notcontains [IO.Path]::GetFileName($numberedArchive)) {
        throw 'The recorded archive does not match the numbered deployment path.'
    }
    $archiveHash = (Get-FileHash -LiteralPath $numberedArchive -Algorithm SHA256).Hash
    if ($archiveHash -ine $record.archiveSha256) { throw 'The numbered archive hash does not match its build record.' }

    $releaseName = [IO.Path]::GetFileNameWithoutExtension($names.archiveName)
    $null = New-Item -ItemType Directory -Path $deployments -Force
    $releaseArchive = Join-Path $deployments ($releaseName + '.zip')
    $releaseRecord = Join-Path $deployments ($releaseName + '.build.json')
    if (Test-Path -LiteralPath $releaseArchive) {
        if ((Get-FileHash -LiteralPath $releaseArchive -Algorithm SHA256).Hash -ine $archiveHash) {
            throw "A different release archive already exists: $releaseArchive. Preserve or move it before exporting a replacement."
        }
    } else {
        [IO.File]::Copy($numberedArchive, $releaseArchive, $false)
    }
    if ((Get-FileHash -LiteralPath $releaseArchive -Algorithm SHA256).Hash -ine $archiveHash) {
        throw 'Release archive verification failed.'
    }
    $record.archive = $releaseArchive
    $record | Add-Member -NotePropertyName releaseExport -NotePropertyValue ([ordered]@{
        exportedUtc = (Get-Date).ToUniversalTime().ToString('o')
        numberedArchive = $numberedArchive
        sourceBuildRecord = $sourceRecord
        sourceBuildRecordSha256 = (Get-FileHash -LiteralPath $sourceRecord -Algorithm SHA256).Hash
        helper = $PSCommandPath
        helperSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
        method = 'Byte-for-byte archive copy; no rebuild or restaging'
    }) -Force
    [IO.File]::WriteAllText($releaseRecord, ($record | ConvertTo-Json -Depth 20) + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
    [pscustomobject]@{
        result = 'passed'; productVersion = $product.productVersion; internalBuild = $record.internalBuild
        archive = $releaseArchive; archiveSha256 = $archiveHash; buildRecord = $releaseRecord
    }
} finally { $buildLock.ReleaseMutex(); $buildLock.Dispose() }
