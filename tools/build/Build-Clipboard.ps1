<#
.SYNOPSIS
Builds and statically validates the single OG/NG/AE Clipboard DLL.

.DESCRIPTION
Builds the maintained src/native/ tree through the existing build helper.
The 1.11.240/F4SE 0.7.9 input fixture records provenance, not runtime eligibility.
Papyrus compilation and package staging remain separate recorded gates.
#>

#Requires -Version 5.1

[CmdletBinding()]
param(
    [string] $GameRoot,
    [string] $VisualStudioPath,
    [string] $CMakePath,
    [string] $VcpkgRoot,
    [string] $VCToolsVersion,
    [string] $WindowsSdkVersion,
    [string] $Fallout4Executable,
    [string] $F4SEDllPath,
    [string] $RuntimeDatabasePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Build-Native.ps1') @PSBoundParameters
