[CmdletBinding()]
param(
    [string]$Artifact = (Join-Path $PSScriptRoot '..\build\release\BlackMythWukong.asi')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$packager = Join-Path $PSScriptRoot 'package-release.ps1'
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$engine = Join-Path $PSHOME 'pwsh.exe'
if (-not (Test-Path -LiteralPath $engine -PathType Leaf)) { $engine = Join-Path $PSHOME 'powershell.exe' }
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('GlossTrainer-package-test-' + [guid]::NewGuid().ToString('N'))
$fixture = Join-Path $testRoot 'source'
$checks = 0

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "ASSERTION FAILED: $Message" }
}
function Pass([string]$Message) { $script:checks++; Write-Host "PASS: $Message" }
function Bytes-Sha256([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($Bytes)).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Invoke-Packager([string]$Name, [string[]]$Arguments, [int]$ExpectedExit = 0) {
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $engine -NoProfile -ExecutionPolicy Bypass -File $packager @Arguments 2>&1
        $actualExit = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousPreference }
    if ($actualExit -ne $ExpectedExit) { throw "$Name expected exit $ExpectedExit, got $actualExit.`n$($output -join [Environment]::NewLine)" }
    Pass $Name
}

try {
    $artifactPath = [IO.Path]::GetFullPath($Artifact)
    Assert-Condition (Test-Path -LiteralPath $artifactPath -PathType Leaf) "Build a Release ASI before running package tests: $artifactPath"
    New-Item -ItemType Directory -Path $fixture | Out-Null
    foreach ($relative in @('BlackMythWukong\src\runtime.hpp', 'BlackMythWukong\readme.md', 'Scripts\install.ps1', 'docs\install.md', 'licenses\imgui.txt', 'licenses\minhook.txt', 'licenses\nlohmann_json.txt')) {
        $target = Join-Path $fixture $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $source $relative) -Destination $target
    }
    [IO.File]::WriteAllText((Join-Path $fixture 'README.md'), 'ROOT README MUST NOT SHIP')
    $readmeFixture = Join-Path $fixture 'BlackMythWukong\readme.md'
    [IO.File]::AppendAllText($readmeFixture, "`nGAME README RELEASE FIXTURE`n[Installer](../Scripts/install.ps1)`n[Install](../docs/install.md#details)`n[Source only](../docs/gameplay.md)`n[License](../licenses/imgui.txt)`n[License directory](../licenses/)`n[External](https://example.com/document)`n")
    # These files represent local settings and reference artifacts. The package
    # must use its explicit allowlist rather than copying a source directory.
    foreach ($relative in @('data\reference.exe', 'data\reference.rar', 'Saved\save.sav', 'scripts\GlossTrainer\ui.ini', 'scripts\GlossTrainer\hotkeys.ini')) {
        $target = Join-Path $fixture $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        [IO.File]::WriteAllText($target, 'MUST NOT SHIP')
    }
    $version = [regex]::Match([IO.File]::ReadAllText((Join-Path $fixture 'BlackMythWukong\src\runtime.hpp')), 'Version\[\]\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"').Groups[1].Value
    Assert-Condition (-not [string]::IsNullOrEmpty($version)) 'Fixture source version missing'
    $tag = 'v' + $version
    $outputDirectory = Join-Path $testRoot 'release'
    $common = @('-Artifact', $artifactPath, '-SourceRoot', $fixture, '-OutputDirectory', $outputDirectory, '-Tag', $tag, '-SourceRevision', ('1' * 40))
    Invoke-Packager 'valid x64 ASI with InitializeASI produces a release' $common
    $zipPath = Join-Path $outputDirectory "GlossTrainer-BlackMythWukong-$tag-win64.zip"
    $initialHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $sums = [IO.File]::ReadAllText((Join-Path $outputDirectory 'SHA256SUMS.txt'))
    Assert-Condition ($sums -ceq "$initialHash  $([IO.Path]::GetFileName($zipPath))`n") 'External SHA256SUMS is incomplete or incorrect'
    Pass 'external checksum identifies the complete ZIP'

    $archiveStream = [IO.File]::OpenRead($zipPath)
    $archive = New-Object IO.Compression.ZipArchive($archiveStream, [IO.Compression.ZipArchiveMode]::Read, $false)
    try {
        $contents = @{}
        foreach ($entry in $archive.Entries) {
            Assert-Condition (-not $contents.ContainsKey($entry.FullName)) 'ZIP contains duplicate paths'
            Assert-Condition ($entry.FullName -notmatch '(^/|\\|(^|/)\.\.(/|$))') 'ZIP contains an unsafe path'
            $entryStream = $entry.Open()
            $buffer = New-Object IO.MemoryStream
            try { $entryStream.CopyTo($buffer); $contents[$entry.FullName] = $buffer.ToArray() }
            finally { $entryStream.Dispose(); $buffer.Dispose() }
        }
    }
    finally { $archive.Dispose(); $archiveStream.Dispose() }
    $expected = @('scripts/GlossTrainer.asi', 'Scripts/install.ps1', 'README.md', 'docs/install.md', 'licenses/imgui.txt', 'licenses/minhook.txt', 'licenses/nlohmann_json.txt', 'release-manifest.json')
    Assert-Condition ($contents.Count -eq $expected.Count) 'ZIP contains files outside the explicit release allowlist'
    foreach ($path in $expected) { Assert-Condition ($contents.ContainsKey($path)) "Release file is missing: $path" }
    Pass 'only ASI, installer, user documentation, licenses and manifest are included'
    $packagedReadme = [Text.Encoding]::UTF8.GetString($contents['README.md'])
    foreach ($expectedText in @('GAME README RELEASE FIXTURE', '[Installer](Scripts/install.ps1)', '[Install](docs/install.md#details)', "[Source only](https://github.com/GlossMod/GlossTrainer/blob/$tag/docs/gameplay.md)", '[License](licenses/imgui.txt)', '[License directory](licenses/)', '[External](https://example.com/document)')) {
        Assert-Condition ($packagedReadme.Contains($expectedText)) "Game README source or relocated link differs: $expectedText"
    }
    Assert-Condition (-not $packagedReadme.Contains('ROOT README MUST NOT SHIP')) 'Root project README was shipped instead of the game README'
    Pass 'game README is relocated with working package and versioned source links'
    $manifest = [Text.Encoding]::UTF8.GetString($contents['release-manifest.json']) | ConvertFrom-Json
    Assert-Condition ($manifest.schemaVersion -eq 1 -and $manifest.project -ceq 'GlossTrainer' -and
        $manifest.game -ceq 'BlackMythWukong' -and $manifest.version -ceq $version -and
        $manifest.tag -ceq $tag -and $manifest.architecture -ceq 'x64' -and
        $manifest.entryPoint -ceq 'InitializeASI' -and $manifest.sourceRevision -ceq ('1' * 40)) 'Manifest metadata differs'
    $manifestPaths = @{}
    foreach ($file in $manifest.files) {
        Assert-Condition (-not $manifestPaths.ContainsKey($file.path)) 'Manifest contains duplicate files'
        $manifestPaths[$file.path] = $true
        Assert-Condition ($file.path -cne 'release-manifest.json' -and $contents.ContainsKey($file.path)) 'Manifest references an unexpected file'
        Assert-Condition ($file.size -eq $contents[$file.path].LongLength -and $file.sha256 -ceq (Bytes-Sha256 $contents[$file.path])) "Manifest hash/size differs: $($file.path)"
    }
    Assert-Condition ($manifestPaths.Count -eq $contents.Count - 1) 'Manifest omits package payloads'
    Pass 'manifest covers every payload exactly once with version, size and SHA-256'

    $existingInstallation = Join-Path $testRoot 'existing-installation'
    $configurationDirectory = Join-Path $existingInstallation 'scripts\GlossTrainer'
    New-Item -ItemType Directory -Path $configurationDirectory -Force | Out-Null
    foreach ($configuration in @('ui.ini', 'hotkeys.ini')) {
        [IO.File]::WriteAllText((Join-Path $configurationDirectory $configuration), "existing-$configuration")
    }
    foreach ($path in $contents.Keys) {
        $target = Join-Path $existingInstallation $path
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        [IO.File]::WriteAllBytes($target, $contents[$path])
    }
    foreach ($configuration in @('ui.ini', 'hotkeys.ini')) {
        Assert-Condition ([IO.File]::ReadAllText((Join-Path $configurationDirectory $configuration)) -ceq "existing-$configuration") 'Extracting the package overwrote existing settings'
    }
    Assert-Condition (@($contents.Keys | Where-Object { $_ -match '\.(ini|log|sav|rar|exe)$' }).Count -eq 0) 'Package contains configuration, logs, saves, reference executables or archives'
    Pass 'package extraction preserves existing configuration and ships no defaults or local data'

    Invoke-Packager 'unchanged inputs produce the same ZIP again' $common
    Assert-Condition ((Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $initialHash) 'Repeated package content is not deterministic'
    Pass 'repeat packaging is byte-for-byte deterministic'
    Invoke-Packager 'mismatching tag is rejected' @('-Artifact', $artifactPath, '-SourceRoot', $fixture, '-OutputDirectory', (Join-Path $testRoot 'bad-tag'), '-Tag', 'v999.999.999') 1
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $testRoot 'bad-tag'))) 'Rejected tag created an output directory'
    $original = [IO.File]::ReadAllBytes($artifactPath)
    $pe = [BitConverter]::ToUInt32($original, 0x3c)
    foreach ($failureCase in @('x86', 'no-export-directory', 'missing-initialize-asi')) {
        $invalid = [byte[]]$original.Clone()
        if ($failureCase -eq 'x86') { $invalid[$pe + 4] = 0x4c; $invalid[$pe + 5] = 0x01 }
        elseif ($failureCase -eq 'no-export-directory') {
            [Array]::Clear($invalid, $pe + 24 + 112, 8)
        } else {
            $ascii = [Text.Encoding]::ASCII.GetString($invalid)
            $position = $ascii.IndexOf('InitializeASI', [StringComparison]::Ordinal)
            Assert-Condition ($position -ge 0) 'Fixture InitializeASI string missing'
            while ($position -ge 0) {
                $invalid[$position] = [byte][char]'X'
                $position = $ascii.IndexOf('InitializeASI', $position + 1, [StringComparison]::Ordinal)
            }
        }
        $invalidPath = Join-Path $testRoot ($failureCase + '.asi')
        [IO.File]::WriteAllBytes($invalidPath, $invalid)
        Invoke-Packager "$failureCase artifact is rejected" @('-Artifact', $invalidPath, '-SourceRoot', $fixture, '-OutputDirectory', (Join-Path $testRoot $failureCase)) 1
    }
    Write-Host "GlossTrainer release package tests passed ($checks checks). No game files were accessed."
}
finally {
    if (Test-Path -LiteralPath $testRoot -PathType Container) {
        $resolved = [IO.Path]::GetFullPath($testRoot)
        $tempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if ($resolved.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase) -and
            [IO.Path]::GetFileName($resolved).StartsWith('GlossTrainer-package-test-')) {
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}
