<#
.SYNOPSIS
Stages the complete OG/NG/AE Clipboard test package locally.

.DESCRIPTION
Revalidates the matching native and Papyrus attestations through the maintained
stager, including twelve-language catalogs and xEdit-localized ESP tables.
Does not deploy into the game or a mod-manager directory.
Rejects an already published internal build. Use Build-Deployment.ps1 to select
the next number, rebuild both components, and stage/archive them together.
#>

#Requires -Version 5.1

[CmdletBinding()]
param(
    [switch] $Archive,
    [string] $PythonPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Stage-Package-V240.ps1') @PSBoundParameters
