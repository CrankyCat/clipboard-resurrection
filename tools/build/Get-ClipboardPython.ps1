# SPDX-License-Identifier: GPL-3.0-or-later
#Requires -Version 5.1
[CmdletBinding()]
param([string] $PythonPath)
$ErrorActionPreference = 'Stop'
if ($PythonPath) {
    if (-not (Test-Path -LiteralPath $PythonPath -PathType Leaf)) { throw "Python not found: $PythonPath" }
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PythonPath)
}
$command = Get-Command python -ErrorAction SilentlyContinue
if ($command -and $command.Source -notmatch '\\WindowsApps\\') { return $command.Source }
$bundled = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
if (Test-Path -LiteralPath $bundled -PathType Leaf) { return $bundled }
throw 'Python 3.10+ is required for localization validation. Supply -PythonPath.'
