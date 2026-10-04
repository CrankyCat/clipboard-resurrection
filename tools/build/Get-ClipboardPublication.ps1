# SPDX-License-Identifier: GPL-3.0-or-later
#Requires -Version 7.0
[CmdletBinding()]
param(
    [string] $ProjectRoot = ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))),
    [ValidateRange(1, 2147483646)][int] $InternalBuild,
    [string] $PythonPath
)
$ErrorActionPreference = 'Stop'
$product = & (Join-Path $PSScriptRoot 'Get-ClipboardVersion.ps1') -ProjectRoot $ProjectRoot
$python = & (Join-Path $PSScriptRoot 'Get-ClipboardPython.ps1') -PythonPath $PythonPath
$arguments = @('-B', (Join-Path $PSScriptRoot '../maintenance/verified_publication.py'), '--root', $ProjectRoot,
    '--product-version', $product.productVersion)
if ($PSBoundParameters.ContainsKey('InternalBuild')) { $arguments += @('--build', "$InternalBuild") }
$result = & $python @arguments
if ($LASTEXITCODE -ne 0) { throw 'No verified publication is available; see validation error above.' }
return ($result | ConvertFrom-Json)
