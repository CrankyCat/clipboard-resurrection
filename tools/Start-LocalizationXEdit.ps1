# SPDX-License-Identifier: GPL-3.0-or-later
# Launch the project-local xEdit workspace prepared by Initialize-LocalizationWorkspace.
#Requires -Version 7.0
[CmdletBinding()]
param([ValidateSet('en','ru')][string] $Language = 'en')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workspace = Join-Path $root 'outputs\localization-implementation\xedit'
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = Join-Path $workspace 'app\FO4Edit.exe'
$start.WorkingDirectory = Join-Path $workspace 'app'
$start.UseShellExecute = $false
foreach ($argument in @('-fo4','-edit','-autoload',('-l:' + $Language),
    ('-D:' + (Join-Path $workspace 'Data') + '\'),
    ('-I:' + (Join-Path $workspace 'ini\Fallout4.ini')),
    ('-C:' + (Join-Path $workspace 'cache') + '\'),
    ('-B:' + (Join-Path $workspace 'backups') + '\'),
    ('-T:' + (Join-Path $workspace 'temp') + '\'),
    ('-G:' + (Join-Path $workspace 'saves') + '\'),
    ('-P:' + (Join-Path $workspace 'plugins.txt')),
    ('-R:' + (Join-Path $workspace ('logs\localize-' + $Language + '.log'))))) {
    $start.ArgumentList.Add($argument)
}
[Diagnostics.Process]::Start($start) | Select-Object Id,ProcessName
