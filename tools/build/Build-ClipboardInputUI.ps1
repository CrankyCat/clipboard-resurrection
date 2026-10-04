# SPDX-License-Identifier: GPL-3.0-or-later
[CmdletBinding()]
param(
    [string] $FFDecRoot,
    [string] $JavaHome = '',
    [string] $OutputPath = ''
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $PSScriptRoot 'ClipboardBuildPaths.ps1')
$paths = Resolve-ClipboardUIPaths $FFDecRoot $JavaHome $root
$FFDecRoot = $paths.FFDecRoot
$JavaHome = $paths.JavaHome
if (-not $OutputPath) { $OutputPath = Join-Path $root 'build\generated\ui\Interface\ClipboardInput.swf' }
$library = Join-Path $FFDecRoot 'lib\*'
$player = Join-Path $FFDecRoot 'flashlib\playerglobal32_0.swc'
if (-not (Test-Path -LiteralPath $player)) { throw "Missing JPEXS playerglobal: $player" }
$classes = Join-Path $root 'build\input-ui\classes'
New-Item -ItemType Directory -Force -Path $classes, (Split-Path $OutputPath -Parent) | Out-Null
& (Join-Path $JavaHome 'bin\javac.exe') -encoding UTF-8 -cp $library -d $classes (Join-Path $PSScriptRoot 'input-ui\CompileClipboardInput.java')
if ($LASTEXITCODE -ne 0) { throw 'Clipboard input UI compiler helper failed' }
$temporary = Join-Path $root 'build\input-ui\ClipboardInput.swf'
& (Join-Path $JavaHome 'bin\java.exe') '-Djava.awt.headless=true' -cp "$classes;$library" CompileClipboardInput (Join-Path $root 'src\ui\ClipboardInput\ClipboardInput.as') $temporary $player
if ($LASTEXITCODE -ne 0) { throw 'Clipboard input UI compilation failed' }
Copy-Item -LiteralPath $temporary -Destination $OutputPath -Force
$recordDirectory = Join-Path $root 'build\interface'
New-Item -ItemType Directory -Force -Path $recordDirectory | Out-Null
$sources = @('src/ui/ClipboardInput/ClipboardInput.as', 'tools/build/input-ui/CompileClipboardInput.java', 'tools/build/Build-ClipboardInputUI.ps1', 'tools/build/ClipboardBuildPaths.ps1')
$record = [ordered]@{
    protocol = 1
    documentClass = 'ClipboardInput'
    asset = 'build/generated/ui/Interface/ClipboardInput.swf'
    sha256 = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash
    compiler = 'JPEXS 26.2.1 ActionScript3Parser'
    compilerSha256 = (Get-FileHash -LiteralPath (Join-Path $FFDecRoot 'lib\ffdec_lib.jar') -Algorithm SHA256).Hash
    playerGlobalSha256 = (Get-FileHash -LiteralPath $player -Algorithm SHA256).Hash
    sources = @($sources | ForEach-Object { [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $root $_) -Algorithm SHA256).Hash } })
}
$record | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $recordDirectory 'clipboard-input-build.json') -Encoding UTF8
Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256
