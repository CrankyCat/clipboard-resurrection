# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2018 to 2026 Everett C Sands
#Requires -Version 5.1

[CmdletBinding()]
param(
    [string] $ProjectRoot,
    [switch] $AsJson
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Join-Path $PSScriptRoot '..'
}
$manifestPath = [IO.Path]::GetFullPath((Join-Path $ProjectRoot 'native\vcpkg.json'))
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.name -cne 'clipboard') { throw 'The modern native manifest must identify clipboard.' }
$productVersion = [string] $manifest.'version-string'
if ($productVersion -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw 'Clipboard product version must be major.minor.patch without leading zeros.'
}
[int[]] $parts = $productVersion.Split('.')
if ($parts[0] -gt 255 -or $parts[1] -gt 255 -or $parts[2] -gt 4095) {
    throw 'Clipboard product version exceeds the F4SE packed-version field bounds.'
}
[uint32] $packed = ([uint64] $parts[0] * 16777216) + ([uint64] $parts[1] * 65536) + ([uint64] $parts[2] * 16)
$result = [pscustomobject][ordered]@{
    productVersion = $productVersion
    fileVersion = $productVersion + '.0'
    major = $parts[0]
    minor = $parts[1]
    patch = $parts[2]
    tweak = 0
    packed = $packed
    pluginVersionPacked = ('0x{0:X8}' -f $packed)
    releaseLabel = 'v' + $productVersion
    deploymentName = 'Clipboard v' + $productVersion + ' - OG NG AE - Test Candidate'
    manifestPath = $manifestPath
    manifestSha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
}
if ($AsJson) { $result | ConvertTo-Json } else { $result }
