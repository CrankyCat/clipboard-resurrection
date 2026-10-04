# Offline generation from reviewed local English and translation inputs.
#Requires -Version 5.1
[CmdletBinding()]
param([string] $PythonPath)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$PythonPath = & (Join-Path $PSScriptRoot 'Get-ClipboardPython.ps1') -PythonPath $PythonPath
$output = [IO.Path]::GetFullPath((Join-Path $root 'build/generated/localization'))
$allowed = [IO.Path]::GetFullPath((Join-Path $root 'build/generated')) + '\'
if (-not $output.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe generation output' }
if (Test-Path -LiteralPath $output) {
    if ((Get-Item -LiteralPath $output).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Generation output is a link' }
    Remove-Item -LiteralPath $output -Recurse -Force
}
$reports = Join-Path $root 'build/metadata/generated'
New-Item -ItemType Directory -Path $output,$reports -Force | Out-Null
& $PythonPath (Join-Path $root 'tools/localization/Build-Localization.py') --root $root --output $output --report (Join-Path $reports 'localization.json') | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Local catalog/document generation failed' }
& $PythonPath (Join-Path $root 'tools/localization/ClipboardLocalizationEsp.py') generate --output $output |
    Set-Content -LiteralPath (Join-Path $reports 'esp-tables.json') -Encoding UTF8
if ($LASTEXITCODE -ne 0) { throw 'ESP string-table generation failed' }
Write-Output "Generated approved catalogs, translated guides and ESP string tables: $output"
