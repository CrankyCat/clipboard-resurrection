# SPDX-License-Identifier: GPL-3.0-or-later
# Prepare a disposable xEdit Data directory; never localize the inherited ESP.
#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $GameData,
    [string] $XEditDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ClipboardBuildPaths.ps1')
$GameData = Resolve-ClipboardInputPath $GameData 'GameData' -Directory
if (-not $XEditDirectory) {
    $xedit = Find-ClipboardApplication 'FO4Edit.exe'
    if ($xedit) { $XEditDirectory = Split-Path -Parent $xedit }
}
$XEditDirectory = Resolve-ClipboardInputPath $XEditDirectory 'XEditDirectory' -Directory
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workRoot = Join-Path $projectRoot 'outputs\localization-implementation\xedit'
if (Test-Path -LiteralPath $workRoot) { throw "Existing localization workspace retained: $workRoot" }
$copies = @(
    @{Source=(Join-Path $XEditDirectory 'FO4Edit.exe'); Target='app\FO4Edit.exe'},
    @{Source=(Join-Path $GameData 'Fallout4.esm'); Target='Data\Fallout4.esm'},
    @{Source=(Join-Path $GameData 'Fallout4 - Interface.ba2'); Target='Data\Fallout4 - Interface.ba2'},
    @{Source=(Join-Path $projectRoot 'package\base\Clipboard.esp'); Target='Data\Clipboard.esp'}
)
foreach ($copy in $copies) { $null = Resolve-ClipboardInputPath $copy.Source 'GameData/XEditDirectory' }
foreach ($relative in @('app', 'Data', 'ini', 'cache', 'backups', 'temp', 'logs', 'saves')) {
    New-Item -ItemType Directory -Path (Join-Path $workRoot $relative) -Force | Out-Null
}
$inventory = foreach ($copy in $copies) {
    $target = Join-Path $workRoot $copy.Target
    Copy-Item -LiteralPath $copy.Source -Destination $target
    $sourceHash = (Get-FileHash -LiteralPath $copy.Source -Algorithm SHA256).Hash
    $targetHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    if ($sourceHash -ne $targetHash) { throw "Copy mismatch: $target" }
    [ordered]@{ source=$copy.Source; destination=$target; sha256=$sourceHash }
}
$utf8 = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $workRoot 'ini\Fallout4.ini'), "[General]`r`nsLanguage=en`r`n[Archive]`r`nsResourceArchiveList=Fallout4 - Interface.ba2`r`n", $utf8)
[IO.File]::WriteAllText((Join-Path $workRoot 'ini\Fallout4Prefs.ini'), "[General]`r`n", $utf8)
[IO.File]::WriteAllText((Join-Path $workRoot 'ini\Fallout4Custom.ini'), "[General]`r`n", $utf8)
[IO.File]::WriteAllText((Join-Path $workRoot 'plugins.txt'), "*Clipboard.esp`r`n", $utf8)
$inventory | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $workRoot 'inputs.json') -Encoding UTF8
Write-Output $workRoot
