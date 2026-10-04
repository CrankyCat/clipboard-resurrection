# Shared by the modern build and publication helpers. Product version is separate.
Set-StrictMode -Version Latest

function Read-ClipboardBuildNumber([string] $Path) {
    $record = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if (-not $record.PSObject.Properties['internalBuild'] -or
        "$($record.internalBuild)" -notmatch '^[1-9][0-9]*$' -or
        [long] $record.internalBuild -lt 99 -or [long] $record.internalBuild -gt 2147483646) {
        throw "Invalid internal build record: $Path"
    }
    return [int] $record.internalBuild
}

function Write-ClipboardBuildRecord([string] $Path, $Record) {
    $temporary = $Path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::WriteAllText($temporary, ($Record | ConvertTo-Json -Depth 5) + [Environment]::NewLine, (New-Object Text.UTF8Encoding($false)))
        if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temporary, $Path, [NullString]::Value) }
        else { [IO.File]::Move($temporary, $Path) }
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    }
}

function Enter-ClipboardBuildLock([string] $Root) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $key = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes([IO.Path]::GetFullPath($Root).ToLowerInvariant()))).Replace('-', '') }
    finally { $sha.Dispose() }
    $mutex = New-Object Threading.Mutex($false, "Local\ClipboardBuild_$key")
    try {
        try { $entered = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $entered = $true }
        if (-not $entered) { throw 'Another Clipboard build/publication is running for this checkout.' }
        return $mutex
    } catch { $mutex.Dispose(); throw }
}

function Get-ClipboardPublicationRoots([string] $Root) {
    # Old numbered archives and their records remain immutable in Deployments.
    $dist = Join-Path $Root 'dist'
    $roots = @($dist, (Join-Path $Root 'Deployments'))
    if (Test-Path -LiteralPath $dist -PathType Container) {
        $roots += @(Get-ChildItem -LiteralPath $dist -Directory | Where-Object { $_.Name -cmatch '^build-[1-9][0-9]*$' } | ForEach-Object { $_.FullName })
    }
    return $roots
}

function Get-ClipboardPublishedBuild([string] $Root) {
    $latest = Read-ClipboardBuildNumber (Join-Path $Root 'config/Clipboard.PublishedBuild.json')
    # The verified deployment record also prevents reuse after an interrupted
    # publication, or when the tracked counter was reverted while packages remain.
    foreach ($deployments in Get-ClipboardPublicationRoots $Root) {
        if (-not (Test-Path -LiteralPath $deployments)) { continue }
        foreach ($file in Get-ChildItem -LiteralPath $deployments -File -Filter '*.build.json') {
            $record = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
            if ($record.PSObject.Properties['internalBuild']) {
                $latest = [Math]::Max($latest, (Read-ClipboardBuildNumber $file.FullName))
            }
        }
    }
    return $latest
}

function Get-ClipboardInternalBuild([string] $Root, [switch] $CheckScripts) {
    $number = Read-ClipboardBuildNumber (Join-Path $Root 'config/Clipboard.InternalBuild.json')
    if ($number -lt 100) { throw 'The first internal build must be 100.' }
    if ($CheckScripts) {
        $sources = @(Get-ChildItem -LiteralPath (Join-Path $Root 'src/papyrus') -Filter '*.psc' -File)
        if ($sources.Count -ne 16) { throw 'Expected 16 maintained scripts for build stamping.' }
        foreach ($source in $sources) {
            $pattern = '(?s); BEGIN GENERATED INTERNAL BUILD\s+int Function InternalBuild_' + [regex]::Escape($source.BaseName) + '\(\) Global\s+return ' + $number + '\s+EndFunction\s+; END GENERATED INTERNAL BUILD'
            if ([regex]::Matches([IO.File]::ReadAllText($source.FullName), $pattern).Count -ne 1) {
                throw "Script build stamp mismatch: $($source.Name). Run Prepare-ClipboardBuild.ps1 and rebuild both components."
            }
        }
    }
    return $number
}

function Assert-ClipboardPublishableBuild([string] $Root, $Native, $Papyrus) {
    $number = Get-ClipboardInternalBuild $Root -CheckScripts
    foreach ($component in @($Native, $Papyrus)) {
        if (-not $component.PSObject.Properties['internalBuild'] -or [int] $component.internalBuild -ne $number) {
            throw "Native and Papyrus builds must both attest internal build $number. Rebuild both components."
        }
    }
    if ($number -le (Get-ClipboardPublishedBuild $Root)) {
        throw "Internal build $number has already been published. Use tools/build/Build-Deployment.ps1 for the next numbered package."
    }
    return $number
}
