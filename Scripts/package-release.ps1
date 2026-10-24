[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Artifact,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist'),
    [string]$SourceRoot = (Join-Path $PSScriptRoot '..'),
    [string]$Tag,
    [string]$SourceRevision
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression

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

$payload = New-Object 'System.Collections.Generic.List[object]'
function Add-PayloadFile([string]$RelativePath, [string]$InputPath) {
    Assert-RegularFile $InputPath
    $payload.Add([pscustomobject]@{ path = $RelativePath; bytes = [IO.File]::ReadAllBytes($InputPath) })
}
$artifactBytes = [IO.File]::ReadAllBytes($artifactPath)
Assert-Asi $artifactBytes
$payload.Add([pscustomobject]@{ path = 'scripts/GlossTrainer.asi'; bytes = $artifactBytes })
Add-PayloadFile 'Scripts/install.ps1' (Join-Path $source 'Scripts\install.ps1')
Add-PayloadFile 'docs/install.md' (Join-Path $source 'docs\install.md')
$licenseRoot = Join-Path $source 'licenses'
if (-not (Test-Path -LiteralPath $licenseRoot -PathType Container) -or
    (((Get-Item -LiteralPath $licenseRoot -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
    throw 'A regular licenses directory is required.'
}
$licenseFiles = @(Get-ChildItem -LiteralPath $licenseRoot -File -Force | Sort-Object Name)
if ($licenseFiles.Count -eq 0) { throw 'Dependency licenses are missing.' }
foreach ($license in $licenseFiles) {
    if ($license.Extension -cne '.txt') { throw "Unexpected license input: $($license.Name)" }
    Add-PayloadFile ('licenses/' + $license.Name) $license.FullName
}
foreach ($requiredLicense in @('licenses/imgui.txt', 'licenses/minhook.txt', 'licenses/nlohmann_json.txt')) {
    if ($requiredLicense -cnotin @($payload | ForEach-Object { $_.path })) { throw "Required license is missing: $requiredLicense" }
}
$readmeFile = Join-Path $source 'BlackMythWukong\readme.md'
Assert-RegularFile $readmeFile
$readmeTargets = @{'BlackMythWukong/readme.md' = 'README.md'; 'licenses/' = 'licenses/'}
foreach ($file in $payload) { $readmeTargets[$file.path] = $file.path }
$sourcePrefix = $source.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$readmeDirectory = [IO.Path]::GetDirectoryName($readmeFile)
# The game README moves one directory up in the ZIP. Keep packaged links local
# and point source-only documentation at the exact release tag instead.
$readme = [regex]::Replace([IO.File]::ReadAllText($readmeFile), '(?<prefix>\]\(\s*)(?<target><[^>\r\n]+>|[^\s\)]+)', [Text.RegularExpressions.MatchEvaluator]{ param($match)
    $target = $match.Groups['target'].Value
    $angled = $target.StartsWith('<')
    if ($angled) { $target = $target.Substring(1, $target.Length - 2) }
    if ($target -match '^(?:[a-zA-Z][a-zA-Z0-9+.-]*:|/|#)') { return $match.Value }
    $parts = [regex]::Match($target, '^(?<path>[^?#]*)(?<suffix>.*)$')
    $relative = [Uri]::UnescapeDataString($parts.Groups['path'].Value).Replace('/', [IO.Path]::DirectorySeparatorChar)
    $resolved = [IO.Path]::GetFullPath((Join-Path $readmeDirectory $relative))
    if (-not $resolved.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase)) { throw "README link leaves the source tree: $target" }
    $repositoryPath = $resolved.Substring($sourcePrefix.Length).Replace('\', '/')
    if ($readmeTargets.ContainsKey($repositoryPath)) { $rewritten = $readmeTargets[$repositoryPath] }
    elseif ($readmeTargets.ContainsKey($repositoryPath.TrimEnd('/') + '/')) { $rewritten = $readmeTargets[$repositoryPath.TrimEnd('/') + '/'] }
    else {
        $kind = if (Test-Path -LiteralPath $resolved -PathType Container) { 'tree' } else { 'blob' }
        $escapedPath = ($repositoryPath.Split('/') | ForEach-Object { [Uri]::EscapeDataString($_) }) -join '/'
        $rewritten = "https://github.com/GlossMod/GlossTrainer/$kind/$Tag/$escapedPath"
    }
    $rewritten += $parts.Groups['suffix'].Value
    if ($angled) { $rewritten = '<' + $rewritten + '>' }
    return $match.Groups['prefix'].Value + $rewritten
})
$utf8 = New-Object Text.UTF8Encoding($false)
$payload.Add([pscustomobject]@{ path = 'README.md'; bytes = $utf8.GetBytes($readme) })
$files = @($payload | Sort-Object path | ForEach-Object {
    [ordered]@{ path = $_.path; size = $_.bytes.LongLength; sha256 = (Get-BytesSha256 $_.bytes) }
})
$manifest = [ordered]@{
    schemaVersion = 1
    project = 'GlossTrainer'
    game = 'BlackMythWukong'
    version = $version
    tag = $Tag
    architecture = 'x64'
    entryPoint = 'InitializeASI'
    sourceRepository = 'https://github.com/GlossMod/GlossTrainer'
    sourceRevision = $(if ($SourceRevision) { $SourceRevision.ToLowerInvariant() } else { $null })
    configurationPolicy = 'User configuration is created at runtime and is never included in the release package.'
    files = $files
}
$payload.Add([pscustomobject]@{ path = 'release-manifest.json'; bytes = $utf8.GetBytes(($manifest | ConvertTo-Json -Depth 6) + "`n") })

if (Test-Path -LiteralPath $destination) {
    if (-not (Test-Path -LiteralPath $destination -PathType Container) -or
        (((Get-Item -LiteralPath $destination -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw 'OutputDirectory must be a regular directory.'
    }
} else { New-Item -ItemType Directory -Path $destination -Force | Out-Null }
$archiveName = "GlossTrainer-BlackMythWukong-$Tag-win64.zip"
$archivePath = Join-Path $destination $archiveName
$partial = Join-Path $destination ([guid]::NewGuid().ToString('N') + '.zip.partial')
$checksumPartial = Join-Path $destination ([guid]::NewGuid().ToString('N') + '.sha.partial')
foreach ($outputPath in @($archivePath, (Join-Path $destination 'SHA256SUMS.txt'))) {
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
    [IO.File]::WriteAllText($checksumPartial, "$archiveSha  $archiveName`n", $utf8)
    Move-Item -LiteralPath $partial -Destination $archivePath -Force
    Move-Item -LiteralPath $checksumPartial -Destination (Join-Path $destination 'SHA256SUMS.txt') -Force
    [pscustomobject]@{ version = $version; archive = $archivePath; sha256 = $archiveSha; manifestFiles = $files.Count }
}
finally {
    foreach ($temporary in @($partial, $checksumPartial)) {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary -Force }
    }
}
