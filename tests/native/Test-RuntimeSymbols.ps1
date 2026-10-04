# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2018 to 2026 Everett C Sands
<#
.SYNOPSIS
Verifies that the reviewed CommonLib preflight inventory preserves vendor IDs.
.DESCRIPTION
Checks every family tuple against its named pinned CommonLib source file and
rejects missing provenance, malformed rows and duplicate entries. Also checks
the direct EngineAPI manifest shape and unique logical keys/names; that does
not verify its manually reviewed address provenance. This is a source
consistency gate, not automatic C++ call-graph or runtime validation.
#>
[CmdletBinding()]
param(
    [string] $InventoryPath,
    [string] $EngineInventoryPath,
    [string] $CommonLibRoot,
    [switch] $AsJson
)

$ErrorActionPreference = 'Stop'
if (-not $InventoryPath) { $InventoryPath = Join-Path $PSScriptRoot '..\..\src\native\RuntimeSymbols.h' }
if (-not $EngineInventoryPath) { $EngineInventoryPath = Join-Path $PSScriptRoot '..\..\src\native\EngineSymbols.h' }
if (-not $CommonLibRoot) { $CommonLibRoot = Join-Path $PSScriptRoot '..\..\external\CommonLibF4RD\CommonLibF4' }
$inventory = [IO.File]::ReadAllText([IO.Path]::GetFullPath($InventoryPath))
$engineInventory = [IO.File]::ReadAllText([IO.Path]::GetFullPath($EngineInventoryPath))
$sourceRoot = [IO.Path]::GetFullPath($CommonLibRoot).TrimEnd('\', '/')
$sourcePrefix = $sourceRoot + [IO.Path]::DirectorySeparatorChar
$rowPattern = 'Symbol\{\s*REL::ID\((\d+(?:\s*,\s*\d+){0,2})\),\s*"([^"]+)",\s*"([^"]+)"\s*\}'
$symbolRows = [regex]::Matches($inventory, $rowPattern)
if ($symbolRows.Count -eq 0 -or
    $symbolRows.Count -ne [regex]::Matches($inventory, 'Symbol\{').Count) {
    throw 'The runtime symbol inventory is empty or contains unrecognized rows.'
}

$seenTuples = @{}
$seenNames = @{}
$vendorTexts = @{}
foreach ($row in $symbolRows) {
    $ids = @($row.Groups[1].Value -split '\s*,\s*' | ForEach-Object { [uint64] $_ })
    $tuple = $ids -join ','
    $name = $row.Groups[2].Value
    $provenance = $row.Groups[3].Value
    if ($seenTuples.ContainsKey($tuple) -or $seenNames.ContainsKey($name)) {
        throw "Duplicate runtime symbol tuple or name: $name ($tuple)."
    }
    $seenTuples[$tuple] = $true
    $seenNames[$name] = $true
    $sourcePath = [IO.Path]::GetFullPath((Join-Path $sourceRoot $provenance))
    if (-not $sourcePath.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Runtime symbol provenance escapes CommonLib: $provenance."
    }
    if (-not $vendorTexts.ContainsKey($sourcePath)) {
        $vendorTexts[$sourcePath] = [IO.File]::ReadAllText($sourcePath)
    }
    $vendorPattern = 'REL::ID\(\s*' + (($ids | ForEach-Object { [regex]::Escape([string] $_) }) -join '\s*,\s*') + '\s*\)'
    if (-not [regex]::IsMatch($vendorTexts[$sourcePath], $vendorPattern)) {
        throw "Runtime symbol $name ($tuple) does not match its vendor declaration in $provenance."
    }
}

$enginePattern = 'RequiredID\{\s*(\d+),\s*REL::ID\((\d+(?:\s*,\s*\d+){0,2})\),\s*0x[0-9A-Fa-f]+,\s*"([^"]+)"(?:,\s*true)?\s*\}'
$engineRows = [regex]::Matches($engineInventory, $enginePattern)
if ($engineRows.Count -eq 0 -or
    $engineRows.Count -ne [regex]::Matches($engineInventory, 'RequiredID\{').Count) {
    throw 'The direct runtime symbol manifest is empty or contains unrecognized rows.'
}
$engineKeys = @{}
$engineNames = @{}
foreach ($row in $engineRows) {
    $key = $row.Groups[1].Value
    $name = $row.Groups[3].Value
    if ($engineKeys.ContainsKey($key) -or $engineNames.ContainsKey($name)) {
        throw "Duplicate direct runtime symbol key or name: $name ($key)."
    }
    $engineKeys[$key] = $true
    $engineNames[$name] = $true
}

if ($AsJson) {
    [ordered]@{
        result = 'passed'
        commonLibSymbolCount = $symbolRows.Count
        engineSymbolCount = $engineRows.Count
        vendorSourceFileCount = $vendorTexts.Count
    } | ConvertTo-Json -Compress
} else {
    Write-Output "Runtime symbol inventory accepted: $($symbolRows.Count) exact family tuples in $($vendorTexts.Count) pinned CommonLib source files; $($engineRows.Count) direct manifest rows checked for shape and uniqueness."
}
