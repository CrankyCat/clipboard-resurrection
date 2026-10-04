<#
.SYNOPSIS
Reads compiled Clipboard PE export metadata without loading or executing it.

.DESCRIPTION
Checks the exact x64 F4SE export set and F4SEPlugin_Version data in the file.
The schema is pinned to CommonLibF4RD/CommonLibF4/include/F4SE/Interfaces.h:
PluginVersionData version 1, offsets 0x000 through 0x25C, sizeof 0x45C.
Requires Clipboard name, signatures (1), NG and AE layouts (6), no exact
runtime whitelist, no minimum F4SE version and zero reserved fields. The PE is
read-only; no LoadLibrary, reflection load, or DLL entry point is invoked.
Success emits one JSON object; invalid input raises a terminating error.
#>

#Requires -Version 5.1

[CmdletBinding()]
param(
    [string] $DllPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$product = & (Join-Path $PSScriptRoot '..\..\tools\build\Get-ClipboardVersion.ps1')

if ([string]::IsNullOrWhiteSpace($DllPath)) {
    $DllPath = Join-Path $PSScriptRoot '..\..\build\native\vs2026\Release\clipboard.dll'
}

if (-not [BitConverter]::IsLittleEndian) {
    throw 'This PE verifier requires a little-endian host.'
}
$resolvedPath = [IO.Path]::GetFullPath($DllPath)
if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
    throw "Clipboard DLL was not found: $resolvedPath"
}
# Hold a read-only, read-sharing handle while obtaining the exact bytes. Hash
# those same bytes rather than reopening a potentially replaced path later.
$inputStream = [IO.File]::Open($resolvedPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try {
    if ($inputStream.Length -lt 64 -or $inputStream.Length -gt 536870912) {
        throw 'PE file size is outside the supported 64-byte to 512-MiB range.'
    }
    $reader = [IO.BinaryReader]::new($inputStream)
    try {
        [byte[]] $peBytes = $reader.ReadBytes([int] $inputStream.Length)
        if ($peBytes.LongLength -ne $inputStream.Length) { throw 'PE file read was incomplete.' }
    }
    finally { $reader.Dispose() }
}
finally { $inputStream.Dispose() }

function Assert-FileRange([long] $Offset, [long] $Count) {
    if ($Offset -lt 0 -or $Count -lt 0 -or $Offset -gt $peBytes.LongLength -or
        $Count -gt ($peBytes.LongLength - $Offset)) {
        throw "PE file range is outside the file: offset=$Offset count=$Count"
    }
}
function Read-U16([long] $Offset) {
    Assert-FileRange $Offset 2
    return [BitConverter]::ToUInt16($peBytes, [int] $Offset)
}
function Read-U32([long] $Offset) {
    Assert-FileRange $Offset 4
    return [BitConverter]::ToUInt32($peBytes, [int] $Offset)
}
function Read-AsciiZ([long] $Offset, [long] $Available, [int] $Maximum = 512) {
    $limit = [Math]::Min($Available, [long] $Maximum)
    Assert-FileRange $Offset $limit
    for ($index = 0; $index -lt $limit; ++$index) {
        $value = $peBytes[[int] ($Offset + $index)]
        if ($value -eq 0) {
            return [Text.Encoding]::ASCII.GetString($peBytes, [int] $Offset, $index)
        }
        if ($value -lt 32 -or $value -gt 126) { throw 'PE string contains non-printable/non-ASCII bytes.' }
    }
    throw 'PE string is not NUL-terminated within its bounded file mapping.'
}

if ((Read-U16 0) -ne 0x5A4D) { throw 'Missing DOS MZ signature.' }
[long] $peOffset = Read-U32 0x3C
if ($peOffset -lt 64) { throw 'PE header overlaps the DOS header.' }
Assert-FileRange $peOffset 24
if ((Read-U32 $peOffset) -ne 0x00004550) { throw 'Missing PE signature.' }
$coffOffset = $peOffset + 4
if ((Read-U16 $coffOffset) -ne 0x8664) { throw 'Clipboard must be an AMD64/x64 PE.' }
[int] $sectionCount = Read-U16 ($coffOffset + 2)
if ($sectionCount -lt 1 -or $sectionCount -gt 96) { throw 'PE section count is outside the supported loader range.' }
if (((Read-U16 ($coffOffset + 18)) -band 0x2000) -eq 0) { throw 'PE is not marked as a DLL.' }
[long] $optionalSize = Read-U16 ($coffOffset + 16)
$optionalOffset = $coffOffset + 20
Assert-FileRange $optionalOffset $optionalSize
if ($optionalSize -lt 120 -or (Read-U16 $optionalOffset) -ne 0x020B) { throw 'Missing complete PE32+ optional header.' }
[long] $sizeOfImage = Read-U32 ($optionalOffset + 56)
[long] $sizeOfHeaders = Read-U32 ($optionalOffset + 60)
[long] $directoryCount = Read-U32 ($optionalOffset + 108)
if ($directoryCount -lt 1 -or ($directoryCount * 8) -gt ($optionalSize - 112)) {
    throw 'PE data directory count exceeds the optional header.'
}
$sectionTable = $optionalOffset + $optionalSize
Assert-FileRange $sectionTable ($sectionCount * 40)
Assert-FileRange 0 $sizeOfHeaders
if ($sizeOfHeaders -lt ($sectionTable + $sectionCount * 40) -or $sizeOfImage -lt $sizeOfHeaders) {
    throw 'PE header/image sizes are inconsistent.'
}

$sections = @()
for ($index = 0; $index -lt $sectionCount; ++$index) {
    $entry = $sectionTable + $index * 40
    [long] $virtualSize = Read-U32 ($entry + 8)
    [long] $virtualAddress = Read-U32 ($entry + 12)
    [long] $rawSize = Read-U32 ($entry + 16)
    [long] $rawOffset = Read-U32 ($entry + 20)
    [long] $span = [Math]::Max($virtualSize, $rawSize)
    $section = [pscustomobject]@{
        Rva = $virtualAddress
        Span = $span
        RawOffset = $rawOffset
        RawSize = $rawSize
        Flags = Read-U32 ($entry + 36)
    }
    if ($span -gt 0 -and ($virtualAddress -lt $sizeOfHeaders -or
        $virtualAddress -gt $sizeOfImage -or $span -gt ($sizeOfImage - $virtualAddress))) {
        throw 'PE section virtual range is outside the image or overlaps its headers.'
    }
    if ($rawSize -gt 0) {
        Assert-FileRange $rawOffset $rawSize
        if ($rawOffset -lt $sizeOfHeaders) { throw 'PE section raw data overlaps the headers.' }
    }
    foreach ($previous in $sections) {
        if ($span -gt 0 -and $previous.Span -gt 0 -and
            $virtualAddress -lt ($previous.Rva + $previous.Span) -and $previous.Rva -lt ($virtualAddress + $span)) {
            throw 'PE sections have ambiguous overlapping virtual ranges.'
        }
        if ($rawSize -gt 0 -and $previous.RawSize -gt 0 -and
            $rawOffset -lt ($previous.RawOffset + $previous.RawSize) -and $previous.RawOffset -lt ($rawOffset + $rawSize)) {
            throw 'PE sections have overlapping raw file ranges.'
        }
    }
    $sections += $section
}

function Resolve-Rva([long] $Rva, [long] $Count) {
    if ($Rva -lt 0 -or $Count -le 0 -or $Rva -gt $sizeOfImage -or $Count -gt ($sizeOfImage - $Rva)) {
        throw "PE RVA range is outside the image: rva=$Rva count=$Count"
    }
    if ($Rva -lt $sizeOfHeaders) {
        if ($Count -gt ($sizeOfHeaders - $Rva)) { throw 'PE RVA range crosses the header mapping.' }
        Assert-FileRange $Rva $Count
        return [pscustomobject]@{ Offset = $Rva; Available = $sizeOfHeaders - $Rva; Flags = 0L }
    }
    foreach ($section in $sections) {
        if ($Rva -ge $section.Rva -and $Rva -lt ($section.Rva + $section.Span)) {
            $delta = $Rva - $section.Rva
            if ($delta -gt $section.RawSize -or $Count -gt ($section.RawSize - $delta)) {
                throw 'PE RVA points into unbacked virtual data or crosses its section mapping.'
            }
            $offset = $section.RawOffset + $delta
            Assert-FileRange $offset $Count
            return [pscustomobject]@{ Offset = $offset; Available = $section.RawSize - $delta; Flags = $section.Flags }
        }
    }
    throw "PE RVA has no file mapping: $Rva"
}

[long] $exportRva = Read-U32 ($optionalOffset + 112)
[long] $exportSize = Read-U32 ($optionalOffset + 116)
if ($exportRva -eq 0 -or $exportSize -lt 40) { throw 'PE export directory is absent or too small.' }
$exportDirectory = Resolve-Rva $exportRva $exportSize
$exportOffset = $exportDirectory.Offset
[long] $functionCount = Read-U32 ($exportOffset + 20)
[long] $nameCount = Read-U32 ($exportOffset + 24)
if ($functionCount -ne 3 -or $nameCount -ne 3) { throw 'Expected exactly three named F4SE exports and no unnamed exports.' }
$functions = Resolve-Rva (Read-U32 ($exportOffset + 28)) ($functionCount * 4)
$names = Resolve-Rva (Read-U32 ($exportOffset + 32)) ($nameCount * 4)
$ordinals = Resolve-Rva (Read-U32 ($exportOffset + 36)) ($nameCount * 2)
$expectedNames = @('F4SEPlugin_Load', 'F4SEPlugin_Query', 'F4SEPlugin_Version')
$observedNames = @()
$observedOrdinals = @()
$metadataRva = 0L
for ($index = 0; $index -lt $nameCount; ++$index) {
    $nameMapping = Resolve-Rva (Read-U32 ($names.Offset + $index * 4)) 1
    $name = Read-AsciiZ $nameMapping.Offset $nameMapping.Available
    $ordinal = Read-U16 ($ordinals.Offset + $index * 2)
    if ($ordinal -ge $functionCount -or $observedOrdinals -contains $ordinal -or
        $expectedNames -cnotcontains $name -or $observedNames -ccontains $name) {
        throw 'PE export names or ordinals do not form the exact Clipboard F4SE contract.'
    }
    [long] $targetRva = Read-U32 ($functions.Offset + $ordinal * 4)
    if ($targetRva -eq 0 -or ($targetRva -ge $exportRva -and $targetRva -lt ($exportRva + $exportSize))) {
        throw 'Clipboard F4SE exports must be present in this image, not forwarded.'
    }
    $target = Resolve-Rva $targetRva 1
    if ($name -ceq 'F4SEPlugin_Version') {
        if (($target.Flags -band 0x40000000) -eq 0 -or ($target.Flags -band 0x20000000) -ne 0) {
            throw 'F4SEPlugin_Version must reside in readable, non-executable data.'
        }
        $metadataRva = $targetRva
    }
    elseif (($target.Flags -band 0x20000000) -eq 0) {
        throw 'F4SEPlugin_Load and F4SEPlugin_Query must map to executable sections.'
    }
    $observedNames += $name
    $observedOrdinals += $ordinal
}
if ($metadataRva -eq 0) { throw 'F4SEPlugin_Version export was not found.' }
$metadata = Resolve-Rva $metadataRva 0x45C
$dataOffset = $metadata.Offset
if ((Read-U32 $dataOffset) -ne 1) { throw 'Unsupported F4SE PluginVersionData schema version.' }
$pluginName = Read-AsciiZ ($dataOffset + 0x008) 256 256
if ($pluginName -cne 'Clipboard') { throw "Unexpected plugin metadata name: $pluginName" }
$author = Read-AsciiZ ($dataOffset + 0x108) 256 256
$addressIndependence = Read-U32 ($dataOffset + 0x208)
$structureIndependence = Read-U32 ($dataOffset + 0x20C)
if ($addressIndependence -ne 1 -or $structureIndependence -ne 6) {
    throw 'Plugin metadata must declare signatures (1) and NG plus AE layouts (6).'
}
$compatibleVersions = @()
for ($index = 0; $index -lt 16; ++$index) {
    $value = Read-U32 ($dataOffset + 0x210 + $index * 4)
    if ($value -ne 0) { throw "Plugin metadata contains a patch whitelist at index $index." }
    $compatibleVersions += $value
}
if ((Read-U32 ($dataOffset + 0x250)) -ne 0) { throw 'Plugin metadata has an unexpected minimum F4SE version.' }
for ($index = 0x254; $index -lt 0x45C; ++$index) {
    if ($peBytes[[int] ($dataOffset + $index)] -ne 0) { throw 'Plugin metadata contains nonzero reserved fields.' }
}
$pluginVersion = Read-U32 ($dataOffset + 4)
if ($pluginVersion -ne $product.packed) {
    throw "Plugin metadata version $('0x{0:X8}' -f $pluginVersion) does not match Clipboard $($product.productVersion) ($($product.pluginVersionPacked))."
}
$hashAlgorithm = [Security.Cryptography.SHA256]::Create()
try { $sha256 = [BitConverter]::ToString($hashAlgorithm.ComputeHash($peBytes)).Replace('-', '') }
finally { $hashAlgorithm.Dispose() }

[ordered]@{
    schemaVersion = 1
    result = 'passed'
    dllPath = $resolvedPath
    bytes = $peBytes.LongLength
    sha256 = $sha256
    inspection = 'read-only PE bytes; DLL not loaded or executed'
    machine = 'AMD64 (0x8664)'
    peFormat = 'PE32+'
    exports = $observedNames
    metadataExportRva = ('0x{0:X8}' -f $metadataRva)
    metadataFileOffset = ('0x{0:X8}' -f $dataOffset)
    metadataBytes = 0x45C
    dataVersion = 1
    name = $pluginName
    author = $author
    pluginVersionPacked = ('0x{0:X8}' -f $pluginVersion)
    productVersion = $product.productVersion
    addressIndependence = $addressIndependence
    structureIndependence = $structureIndependence
    compatibleVersions = $compatibleVersions
    seVersionRequired = 0
    reservedFieldsZero = $true
    runtimeEligibility = 'OG via Query; NG and AE via structure flags; no executable patch whitelist'
    runtimeCertification = 'not established by metadata inspection'
} | ConvertTo-Json -Depth 5
