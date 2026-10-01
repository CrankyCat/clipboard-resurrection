<# Selects the next unpublished build and stamps all maintained script sources.
   Repeating after a failed build keeps the same number. Does not compile/deploy. #>
#Requires -Version 5.1
[CmdletBinding()]
param([string] $ProjectRoot = (Join-Path $PSScriptRoot '..'))
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ClipboardBuildIdentity.ps1')
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$buildLock = Enter-ClipboardBuildLock $ProjectRoot
try {
    $current = Get-ClipboardInternalBuild $ProjectRoot
    $published = Get-ClipboardPublishedBuild $ProjectRoot
    $number = [Math]::Max($current, $published + 1)
    if ($number -gt 2147483646) { throw 'Internal build number exhausted.' }
    $sources = @(Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'Scripts/Source/User') -File -Filter '*.psc' | Sort-Object Name)
    if ($sources.Count -ne 16) { throw 'Expected 16 maintained scripts.' }
    foreach ($source in $sources) {
        $content = [IO.File]::ReadAllText($source.FullName)
        $newline = if ($content.Contains("`r`n")) { "`r`n" } else { "`n" }
        $block = @('; BEGIN GENERATED INTERNAL BUILD', "int Function InternalBuild_$($source.BaseName)() Global", "    return $number", 'EndFunction', '; END GENERATED INTERNAL BUILD') -join $newline
        $pattern = '(?s); BEGIN GENERATED INTERNAL BUILD.*?; END GENERATED INTERNAL BUILD'
        if ([regex]::Matches($content, $pattern).Count -gt 1) { throw "Duplicate build stamp: $($source.Name)" }
        $updated = if ($content -match $pattern) { [regex]::Replace($content, $pattern, $block) } else { $content.TrimEnd() + $newline + $newline + $block + $newline }
        if ($updated -cne $content) { [IO.File]::WriteAllText($source.FullName, $updated, (New-Object Text.UTF8Encoding($false))) }
    }
    Write-ClipboardBuildRecord (Join-Path $ProjectRoot 'tools/Clipboard.InternalBuild.json') ([ordered]@{internalBuild=$number})
    $null = Get-ClipboardInternalBuild $ProjectRoot -CheckScripts
    Write-Output "Prepared Clipboard internal build $number."
} finally { $buildLock.ReleaseMutex(); $buildLock.Dispose() }
