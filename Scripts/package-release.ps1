[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Artifact,
    [string]$OutputDirectory,
    [string]$SourceRoot,
    [string]$Tag,
    [string]$SourceRevision,
    [string]$LoaderArchive
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
if ([string]::IsNullOrWhiteSpace($SourceRoot)) { $SourceRoot = Join-Path $PSScriptRoot '..' }
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $PSScriptRoot ('..\output\release-package-' + (Get-Date -Format 'yyyyMMdd') + '\release')
}
$loaderVersion = '10.0.0'
$loaderArchiveSha256 = '3d5f2f2a47f456db878d17c1b135b0eae3209a7d8dabb0ff3f84e1b008c4f3a7'
$loaderArchiveUrl = 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.0/Ultimate-ASI-Loader_x64.zip'

function Get-BytesSha256([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($Bytes)).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Assert-RegularFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required release input is missing: $Path" }
    if (((Get-Item -LiteralPath $Path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Release inputs must not be symbolic links: $Path"
    }
}
function Assert-RegularDirectory([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container) -or
        (((Get-Item -LiteralPath $Path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw "A regular directory is required: $Path"
    }
}
function Assert-X64Dll([byte[]]$Bytes) {
    if ($Bytes.LongLength -lt 64 -or [BitConverter]::ToUInt16($Bytes, 0) -ne 0x5a4d) { throw 'Loader is not a PE file.' }
    $pe = [uint64][BitConverter]::ToUInt32($Bytes, 0x3c)
    if ($pe -lt 64 -or $pe + 24 -gt $Bytes.LongLength -or [BitConverter]::ToUInt32($Bytes, [int]$pe) -ne 0x4550) {
        throw 'Loader has an invalid PE header.'
    }
    $sectionCount = [BitConverter]::ToUInt16($Bytes, [int]$pe + 6)
    $optionalSize = [BitConverter]::ToUInt16($Bytes, [int]$pe + 20)
    if ([BitConverter]::ToUInt16($Bytes, [int]$pe + 4) -ne 0x8664 -or
        ([BitConverter]::ToUInt16($Bytes, [int]$pe + 22) -band 0x2000) -eq 0 -or
        $sectionCount -eq 0 -or $sectionCount -gt 96 -or $optionalSize -lt 112 -or
        $pe + 24 + $optionalSize + [uint64]$sectionCount * 40 -gt $Bytes.LongLength -or
        [BitConverter]::ToUInt16($Bytes, [int]$pe + 24) -ne 0x20b) {
        throw 'Loader must be a Windows x64 PE32+ DLL.'
    }
}
function Get-VerifiedLoaderArchive([string]$ExplicitPath, [string]$Directory) {
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $archivePath = [IO.Path]::GetFullPath($ExplicitPath)
        Assert-RegularFile $archivePath
        if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $loaderArchiveSha256) {
            throw 'Supplied Loader archive SHA-256 mismatch.'
        }
        return $archivePath
    }
    $archivePath = Join-Path $Directory 'Ultimate-ASI-Loader_x64.zip'
    if (Test-Path -LiteralPath $Directory) { Assert-RegularDirectory $Directory }
    else { New-Item -ItemType Directory -Path $Directory -Force | Out-Null }
    if (Test-Path -LiteralPath $archivePath) {
        Assert-RegularFile $archivePath
        if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $loaderArchiveSha256) {
            throw "Cached Loader archive SHA-256 mismatch: $archivePath"
        }
        return $archivePath
    }
    $partial = Join-Path $Directory ([guid]::NewGuid().ToString('N') + '.download')
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Write-Host "Downloading Ultimate ASI Loader v$loaderVersion x64..."
        Invoke-WebRequest -Uri $loaderArchiveUrl -OutFile $partial -UseBasicParsing
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant() -cne $loaderArchiveSha256) {
            throw 'Downloaded Loader archive SHA-256 mismatch.'
        }
        Move-Item -LiteralPath $partial -Destination $archivePath
        return $archivePath
    }
    finally { if (Test-Path -LiteralPath $partial -PathType Leaf) { Remove-Item -LiteralPath $partial -Force } }
}
function Resolve-PeRva([uint64]$Rva, [uint64]$Width, $Sections, [uint64]$FileLength, [uint64]$HeadersSize) {
    if ($Rva -lt $HeadersSize -and $Rva + $Width -le $HeadersSize -and $Rva + $Width -le $FileLength) { return [long]$Rva }
    foreach ($section in $Sections) {
        if ($Rva -ge $section.virtualAddress -and $Rva - $section.virtualAddress + $Width -le $section.rawSize) {
            $offset = $section.rawOffset + $Rva - $section.virtualAddress
            if ($offset + $Width -le $FileLength) { return [long]$offset }
        }
    }
    throw 'ASI contains an invalid PE RVA.'
}
function Assert-Asi([byte[]]$Bytes) {
    $stream = New-Object IO.MemoryStream(, $Bytes)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { throw 'ASI is not a PE file.' }
        $stream.Position = 0x3c
        $pe = $reader.ReadUInt32()
        if ($pe -lt 64 -or [uint64]$pe + 24 -gt $stream.Length) { throw 'ASI has an invalid PE header.' }
        $stream.Position = $pe
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw 'ASI must be Windows x64.' }
        $sectionCount = $reader.ReadUInt16()
        $stream.Position = $pe + 20
        $optionalSize = $reader.ReadUInt16()
        $characteristics = $reader.ReadUInt16()
        $optional = [uint64]$pe + 24
        if ($sectionCount -eq 0 -or $sectionCount -gt 96 -or $optionalSize -lt 120 -or
            $optional + $optionalSize + [uint64]$sectionCount * 40 -gt $stream.Length -or
            ($characteristics -band 0x2000) -eq 0 -or $reader.ReadUInt16() -ne 0x20b) {
            throw 'ASI must contain valid PE32+ DLL headers.'
        }
        $stream.Position = $optional + 60
        $headersSize = $reader.ReadUInt32()
        $stream.Position = $optional + 108
        if ($reader.ReadUInt32() -lt 1) { throw 'ASI has no export directory.' }
        $exportRva = $reader.ReadUInt32()
        $exportSize = $reader.ReadUInt32()
        if ($exportRva -eq 0 -or $exportSize -lt 40) { throw 'ASI has no export directory.' }
        $sections = @()
        for ($index = 0; $index -lt $sectionCount; $index++) {
            $stream.Position = $optional + $optionalSize + $index * 40 + 12
            $sections += [pscustomobject]@{
                virtualAddress = [uint64]$reader.ReadUInt32()
                rawSize = [uint64]$reader.ReadUInt32()
                rawOffset = [uint64]$reader.ReadUInt32()
            }
        }
        $exportOffset = Resolve-PeRva $exportRva 40 $sections $stream.Length $headersSize
        $stream.Position = $exportOffset + 20
        $functionCount = $reader.ReadUInt32()
        $nameCount = $reader.ReadUInt32()
        $functions = $reader.ReadUInt32()
        $names = $reader.ReadUInt32()
        $ordinals = $reader.ReadUInt32()
        if ($nameCount -eq 0 -or $nameCount -gt 65536 -or $functionCount -eq 0) { throw 'ASI has no valid named exports.' }
        $namesOffset = Resolve-PeRva $names ([uint64]$nameCount * 4) $sections $stream.Length $headersSize
        for ($index = 0; $index -lt $nameCount; $index++) {
            $stream.Position = $namesOffset + $index * 4
            $nameRva = $reader.ReadUInt32()
            $nameOffset = Resolve-PeRva $nameRva 1 $sections $stream.Length $headersSize
            $stream.Position = $nameOffset
            $name = New-Object Text.StringBuilder
            for ($letter = 0; $letter -lt 256 -and $stream.Position -lt $stream.Length; $letter++) {
                $value = $reader.ReadByte()
                if ($value -eq 0) { break }
                [void]$name.Append([char]$value)
            }
            if ($name.ToString() -cne 'InitializeASI' -or $value -ne 0) { continue }
            $stream.Position = Resolve-PeRva ($ordinals + [uint64]$index * 2) 2 $sections $stream.Length $headersSize
            $ordinal = $reader.ReadUInt16()
            if ($ordinal -ge $functionCount) { throw 'InitializeASI has an invalid export ordinal.' }
            $stream.Position = Resolve-PeRva ($functions + [uint64]$ordinal * 4) 4 $sections $stream.Length $headersSize
            $functionRva = $reader.ReadUInt32()
            if ($functionRva -eq 0 -or ($functionRva -ge $exportRva -and [uint64]$functionRva -lt [uint64]$exportRva + $exportSize)) {
                throw 'InitializeASI must be defined in the ASI, not forwarded.'
            }
            [void](Resolve-PeRva $functionRva 1 $sections $stream.Length $headersSize)
            return
        }
        throw 'ASI does not export InitializeASI.'
    }
    finally { $reader.Dispose(); $stream.Dispose() }
}

$source = [IO.Path]::GetFullPath($SourceRoot)
$artifactPath = [IO.Path]::GetFullPath($Artifact)
$destination = [IO.Path]::GetFullPath($OutputDirectory)
$versionFile = Join-Path $source 'BlackMythWukong\src\runtime.hpp'
Assert-RegularFile $versionFile
Assert-RegularFile $artifactPath
$versionMatches = [regex]::Matches([IO.File]::ReadAllText($versionFile), 'inline\s+constexpr\s+char\s+Version\[\]\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"\s*;')
if ($versionMatches.Count -ne 1) { throw 'Exactly one formal source version is required in runtime.hpp.' }
$version = $versionMatches[0].Groups[1].Value
$expectedTag = 'v' + $version
if ([string]::IsNullOrWhiteSpace($Tag)) { $Tag = $expectedTag }
if ($Tag -cnotmatch '^v[0-9]+\.[0-9]+\.[0-9]+$' -or $Tag -cne $expectedTag) {
    throw "Release tag '$Tag' does not match source version '$expectedTag'."
}
if ($SourceRevision -and $SourceRevision -notmatch '^[0-9a-fA-F]{40}$') { throw 'SourceRevision must be a full Git commit SHA.' }

$artifactBytes = [IO.File]::ReadAllBytes($artifactPath)
Assert-Asi $artifactBytes
$licenseRoot = Join-Path $source 'licenses'
Assert-RegularDirectory $licenseRoot
$licenseFiles = @(Get-ChildItem -LiteralPath $licenseRoot -File -Force | Sort-Object Name)
if ($licenseFiles.Count -eq 0) { throw 'Dependency licenses are missing.' }
$projectLicense = Join-Path $source 'LICENSE'
Assert-RegularFile $projectLicense
$licenses = New-Object Text.StringBuilder
[void]$licenses.Append("### GlossTrainer (LICENSE)`n`n``````text`n")
[void]$licenses.Append([IO.File]::ReadAllText($projectLicense))
[void]$licenses.Append("`n```````n`n")
foreach ($license in $licenseFiles) {
    if ($license.Extension -cne '.txt') { throw "Unexpected license input: $($license.Name)" }
    Assert-RegularFile $license.FullName
    [void]$licenses.Append("### $($license.Name)`n`n``````text`n")
    [void]$licenses.Append([IO.File]::ReadAllText($license.FullName))
    [void]$licenses.Append("`n```````n`n")
}
foreach ($requiredLicense in @('imgui.txt', 'minhook.txt', 'nlohmann_json.txt', 'ultimate_asi_loader.txt')) {
    if ($requiredLicense -cnotin @($licenseFiles | ForEach-Object { $_.Name })) { throw "Required license is missing: $requiredLicense" }
}
$readmeFile = Join-Path $source 'BlackMythWukong\release-readme.md'
Assert-RegularFile $readmeFile
$readme = [IO.File]::ReadAllText($readmeFile)
if ([regex]::Matches($readme, '\{\{VERSION\}\}').Count -eq 0 -or [regex]::Matches($readme, '\{\{LICENSES\}\}').Count -ne 1) {
    throw 'Release README requires VERSION and exactly one LICENSES placeholder.'
}
$readme = $readme.Replace('{{VERSION}}', $version).Replace('{{LICENSES}}', $licenses.ToString())
if ($readme -match '\{\{[A-Z_]+\}\}') { throw 'Release README has unresolved placeholders.' }
$utf8 = New-Object Text.UTF8Encoding($false)
if (Test-Path -LiteralPath $destination) { Assert-RegularDirectory $destination }
else { New-Item -ItemType Directory -Path $destination -Force | Out-Null }
$verifiedLoaderArchive = Get-VerifiedLoaderArchive $LoaderArchive (Join-Path $destination 'cache\Ultimate-ASI-Loader\v10.0.0')
$loaderStream = [IO.File]::OpenRead($verifiedLoaderArchive)
$loaderZip = New-Object IO.Compression.ZipArchive($loaderStream, [IO.Compression.ZipArchiveMode]::Read, $false)
try {
    # The pinned official ZIP contains only dinput8.dll. The loader also supports
    # version.dll; renaming it matches install.ps1 and the game's direct import.
    if ($loaderZip.Entries.Count -ne 1 -or $loaderZip.Entries[0].FullName -cne 'dinput8.dll') {
        throw 'Pinned Loader archive has an unexpected layout.'
    }
    $loaderEntryStream = $loaderZip.Entries[0].Open()
    $loaderBuffer = New-Object IO.MemoryStream
    try { $loaderEntryStream.CopyTo($loaderBuffer); $loaderBytes = $loaderBuffer.ToArray() }
    finally { $loaderEntryStream.Dispose(); $loaderBuffer.Dispose() }
}
finally { $loaderZip.Dispose(); $loaderStream.Dispose() }
Assert-X64Dll $loaderBytes
$loaderIni = "[GlobalSets]`r`nDontLoadFromDllMain=1`r`nLoadPlugins=1`r`nLoadFromScriptsOnly=1`r`nDebugLog=1`r`n"
$payload = @(
    [pscustomobject]@{ path = 'README.md'; bytes = $utf8.GetBytes($readme) }
    [pscustomobject]@{ path = 'scripts/GlossTrainer.asi'; bytes = $artifactBytes }
    [pscustomobject]@{ path = 'version.dll'; bytes = $loaderBytes }
    [pscustomobject]@{ path = 'version.ini'; bytes = [Text.Encoding]::ASCII.GetBytes($loaderIni) }
)
$files = @($payload | ForEach-Object {
    [ordered]@{ path = $_.path; size = $_.bytes.LongLength; sha256 = (Get-BytesSha256 $_.bytes) }
})
$archiveName = "GlossTrainer-BlackMythWukong-$Tag-win64.zip"
$archivePath = Join-Path $destination $archiveName
$manifestPath = Join-Path $destination 'release-manifest.json'
$partial = Join-Path $destination ([guid]::NewGuid().ToString('N') + '.zip.partial')
$checksumPartial = Join-Path $destination ([guid]::NewGuid().ToString('N') + '.sha.partial')
$manifestPartial = Join-Path $destination ([guid]::NewGuid().ToString('N') + '.json.partial')
foreach ($outputPath in @($archivePath, $manifestPath, (Join-Path $destination 'SHA256SUMS.txt'))) {
    if ((Test-Path -LiteralPath $outputPath) -and
        (((Get-Item -LiteralPath $outputPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw "Refusing to replace an output symbolic link: $outputPath"
    }
}
try {
    $stream = [IO.File]::Open($partial, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite)
    $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        foreach ($file in ($payload | Sort-Object path)) {
            $entry = $archive.CreateEntry($file.path, [IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = [DateTimeOffset]::new(2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
            $entryStream = $entry.Open()
            try { $entryStream.Write($file.bytes, 0, $file.bytes.Length) }
            finally { $entryStream.Dispose() }
        }
    }
    finally { $archive.Dispose(); $stream.Dispose() }
    $archiveSha = (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest = [ordered]@{
        schemaVersion = 2
        project = 'GlossTrainer'
        game = 'BlackMythWukong'
        version = $version
        tag = $Tag
        architecture = 'x64'
        entryPoint = 'InitializeASI'
        sourceRepository = 'https://github.com/GlossMod/GlossTrainer'
        sourceRevision = $(if ($SourceRevision) { $SourceRevision.ToLowerInvariant() } else { $null })
        configurationPolicy = 'User configuration is created at runtime and is never included in the release package.'
        loader = [ordered]@{
            project = 'Ultimate ASI Loader'
            version = $loaderVersion
            archiveUrl = $loaderArchiveUrl
            archiveSha256 = $loaderArchiveSha256
            archiveEntry = 'dinput8.dll'
            installedAs = 'version.dll'
        }
        archive = [ordered]@{ name = $archiveName; size = (Get-Item -LiteralPath $partial).Length; sha256 = $archiveSha }
        files = $files
    }
    [IO.File]::WriteAllText($manifestPartial, ($manifest | ConvertTo-Json -Depth 6) + "`n", $utf8)
    [IO.File]::WriteAllText($checksumPartial, "$archiveSha  $archiveName`n", $utf8)
    Move-Item -LiteralPath $partial -Destination $archivePath -Force
    Move-Item -LiteralPath $manifestPartial -Destination $manifestPath -Force
    Move-Item -LiteralPath $checksumPartial -Destination (Join-Path $destination 'SHA256SUMS.txt') -Force
    [pscustomobject]@{ version = $version; archive = $archivePath; sha256 = $archiveSha; manifest = $manifestPath; manifestFiles = $files.Count }
}
finally {
    foreach ($temporary in @($partial, $checksumPartial, $manifestPartial)) {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary -Force }
    }
}
