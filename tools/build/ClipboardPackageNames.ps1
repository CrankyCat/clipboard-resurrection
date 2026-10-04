# Shared naming for staging, release export and deployment preflight.
# Product version and internal build identity remain separate.
Set-StrictMode -Version Latest

function Resolve-ClipboardReleaseVersion([string] $Version, [string] $ProductVersion) {
    if ([string]::IsNullOrWhiteSpace($Version)) {
        $Version = Read-Host 'Release version (Major.Minor.Patch)'
    }
    $Version = $Version.Trim()
    if ($Version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw 'Release version must use Major.Minor.Patch without leading zeros, for example 3.0.0.'
    }
    if ($Version -cne $ProductVersion) {
        throw "Release version $Version differs from configured product version $ProductVersion. Update the product version and affected documentation before building this release."
    }
    return $Version
}

function Get-ClipboardPackageNames(
    [object] $Product,
    [int] $InternalBuild,
    [switch] $Release,
    [string] $ReleaseVersion
) {
    if (-not $Release -and -not [string]::IsNullOrWhiteSpace($ReleaseVersion)) {
        throw '-ReleaseVersion requires -Release.'
    }
    $suffix = if ($Release) {
        Resolve-ClipboardReleaseVersion $ReleaseVersion $Product.productVersion
    } else {
        if ($InternalBuild -lt 100) { throw 'A test archive requires a valid internal build number.' }
        'build ' + $InternalBuild
    }
    [pscustomobject]@{
        deploymentName = $Product.deploymentName
        archiveName = $Product.deploymentName + ' - ' + $suffix + '.zip'
        buildKind = $(if ($Release) { 'release' } else { 'test' })
        releaseVersion = $(if ($Release) { $suffix } else { $null })
    }
}
