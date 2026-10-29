[CmdletBinding()]
param(
    [string]$Artifact,
    [string]$LoaderArchive,
    [string]$OutputDirectory,
    [string]$Game = 'BlackMythWukong'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$sourceRelease = & (Join-Path $PSScriptRoot 'resolve-release.ps1') -Game $Game
if ([string]::IsNullOrWhiteSpace($Artifact)) { $Artifact = Join-Path $PSScriptRoot ('../' + $sourceRelease.artifact) }
if ([string]::IsNullOrWhiteSpace($LoaderArchive)) {
    $LoaderArchive = Join-Path $PSScriptRoot ('../' + $sourceRelease.outputDirectory + '/cache/Ultimate-ASI-Loader/v10.0.0/Ultimate-ASI-Loader_x64.zip')
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $PSScriptRoot ("../output/package-validation/$Game/v" + $sourceRelease.version)
}
$packager = Join-Path $PSScriptRoot 'package-release.ps1'
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$engine = Join-Path $PSHOME 'pwsh.exe'
if (-not (Test-Path -LiteralPath $engine -PathType Leaf)) { $engine = Join-Path $PSHOME 'powershell.exe' }
$testRoot = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ('tests-ps' + $PSVersionTable.PSVersion.Major + '-' + [guid]::NewGuid().ToString('N'))
$fixture = Join-Path $testRoot 'source'
$checks = 0
$results = New-Object 'System.Collections.Generic.List[object]'
$failure = $null
$utf8 = New-Object Text.UTF8Encoding($false)
$loaderPath = [IO.Path]::GetFullPath($LoaderArchive)
$loaderSha256 = '3d5f2f2a47f456db878d17c1b135b0eae3209a7d8dabb0ff3f84e1b008c4f3a7'

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "ASSERTION FAILED: $Message" }
}
function Pass([string]$Message) {
    $script:checks++
    $results.Add([ordered]@{ check = $script:checks; name = $Message; passed = $true })
    Write-Host "PASS: $Message"
}
function Bytes-Sha256([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($Bytes)).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Invoke-Packager([string]$Name, [string[]]$Arguments, [int]$ExpectedExit = 0, [string]$ExpectedMessage, [switch]$UseCache) {
    if ($Game -cne 'BlackMythWukong' -and '-Game' -notin $Arguments) { $Arguments += @('-Game', $Game) }
    if (-not $UseCache -and '-LoaderArchive' -notin $Arguments) { $Arguments += @('-LoaderArchive', $loaderPath) }
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $engine -NoProfile -ExecutionPolicy Bypass -File $packager @Arguments 2>&1
        $actualExit = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousPreference }
    $outputText = $output -join [Environment]::NewLine
    $logPath = Join-Path $testRoot (('{0:D2}-' -f ($script:checks + 1)) + ($Name -replace '[^a-zA-Z0-9-]+', '-') + '.log')
    [IO.File]::WriteAllText($logPath, $outputText + "`n", $utf8)
    if ($actualExit -ne $ExpectedExit) { throw "$Name expected exit $ExpectedExit, got $actualExit.`n$outputText" }
    if ($ExpectedMessage) { Assert-Condition ($outputText.Contains($ExpectedMessage)) "$Name did not fail for the expected reason: $outputText" }
    Pass $Name
}
function Read-Zip([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Read, $false)
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
        return $contents
    }
    finally { $archive.Dispose(); $stream.Dispose() }
}

try {
    New-Item -ItemType Directory -Path $fixture -Force | Out-Null
    $artifactSourcePath = [IO.Path]::GetFullPath($Artifact)
    Assert-Condition (Test-Path -LiteralPath $artifactSourcePath -PathType Leaf) "Build a Release ASI before running package tests: $artifactSourcePath"
    # Another local task may rebuild the same output while tests are running.
    # Use one immutable snapshot throughout this run, including failure fixtures.
    $artifactPath = Join-Path $testRoot 'input.asi'
    [IO.File]::WriteAllBytes($artifactPath, [IO.File]::ReadAllBytes($artifactSourcePath))
    Assert-Condition (Test-Path -LiteralPath $loaderPath -PathType Leaf) "Offline tests require a pinned official Loader ZIP. Pass -LoaderArchive with its cached path: $loaderPath"
    Assert-Condition ((Get-FileHash -LiteralPath $loaderPath -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $loaderSha256) 'Offline Loader archive does not match the pinned SHA-256'
    Pass 'offline Loader fixture matches the pinned official x64 release'
    # The resolver validates ownership across the entire registry. Include every
    # registered game's sources so adding a game cannot break another's tests.
    $fixtureInputs = @('Scripts\release-games.json', 'Scripts\resolve-release.ps1', 'LICENSE')
    $sourceRegistry = [IO.File]::ReadAllText((Join-Path $source 'Scripts/release-games.json')) | ConvertFrom-Json
    foreach ($registeredGame in $sourceRegistry.games) {
        foreach ($field in @('versionFile', 'readme', 'readmeEn', 'releaseNotes', 'validationFile', 'packageScript', 'packageTests', 'installerTests')) {
            $fixtureInputs += $registeredGame.$field
        }
    }
    $fixtureInputs += @(Get-ChildItem -LiteralPath (Join-Path $source 'licenses') -File | ForEach-Object { 'licenses\' + $_.Name })
    foreach ($relative in @($fixtureInputs | Sort-Object -Unique)) {
        $target = Join-Path $fixture $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $source $relative) -Destination $target
    }
    [IO.File]::WriteAllText((Join-Path $fixture 'README.md'), 'ROOT README MUST NOT SHIP')
    [IO.File]::AppendAllText((Join-Path $fixture $sourceRelease.readme), "`nRELEASE README FIXTURE`n", $utf8)
    [IO.File]::AppendAllText((Join-Path $fixture $sourceRelease.readmeEn), "`nENGLISH RELEASE README FIXTURE`n", $utf8)
    # No fixtures access a game, download data, or write outside output/.
    foreach ($relative in @('data\reference.exe', 'data\reference.rar', 'Saved\save.sav', 'scripts\GlossTrainer\ui.ini', 'scripts\GlossTrainer\hotkeys.ini', 'scripts\GlossTrainer\trainer.ini', 'Scripts\install.ps1', "$Game/docs/install.md")) {
        $target = Join-Path $fixture $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        [IO.File]::WriteAllText($target, 'MUST NOT SHIP')
    }
    $version = [regex]::Match([IO.File]::ReadAllText((Join-Path $fixture $sourceRelease.versionFile)), 'Version\[\]\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"').Groups[1].Value
    Assert-Condition (-not [string]::IsNullOrEmpty($version)) 'Fixture source version missing'
    $tag = "$Game/v$version"
    $stem = "GlossTrainer-$Game-v$version-win64"
    $releaseDirectory = Join-Path $testRoot 'release'
    $common = @('-Artifact', $artifactPath, '-SourceRoot', $fixture, '-OutputDirectory', $releaseDirectory, '-Tag', $tag, '-SourceRevision', ('1' * 40))
    Invoke-Packager 'valid x64 ASI and verified Loader produce a portable release' $common
    $zipPath = Join-Path $releaseDirectory "$stem.zip"
    $initialHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Condition ([IO.File]::ReadAllText((Join-Path $releaseDirectory "$stem.SHA256SUMS.txt")) -ceq "$initialHash  $([IO.Path]::GetFileName($zipPath))`n") 'External SHA256SUMS is incomplete or incorrect'
    Pass 'external checksum identifies the complete ZIP'

    $contents = Read-Zip $zipPath
    $expected = @('version.dll', 'version.ini', 'README.md', 'readme_en.md', 'scripts/GlossTrainer.asi')
    Assert-Condition ($contents.Count -eq $expected.Count) 'ZIP contains files outside the explicit release allowlist'
    foreach ($path in $expected) { Assert-Condition ($contents.ContainsKey($path)) "Release file is missing: $path" }
    Assert-Condition (@($contents.Keys | Where-Object { $_ -match '\.asi$' }).Count -eq 1) 'ASI appears more than once'
    Assert-Condition ((Bytes-Sha256 $contents['scripts/GlossTrainer.asi']) -ceq (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()) 'Packaged ASI differs from the verified input'
    Pass 'ZIP contains exactly Loader INI bilingual READMEs and a single scripts ASI'
    $officialLoaderContents = Read-Zip $loaderPath
    Assert-Condition ($officialLoaderContents.Count -eq 1 -and $officialLoaderContents.ContainsKey('dinput8.dll')) 'Official Loader layout differs'
    Assert-Condition ((Bytes-Sha256 $contents['version.dll']) -ceq (Bytes-Sha256 $officialLoaderContents['dinput8.dll'])) 'Loader differs from the verified official ZIP'
    Pass 'official dinput8 DLL is renamed to version DLL without modification'
    $expectedIni = "[GlobalSets]`r`nDontLoadFromDllMain=1`r`nLoadPlugins=1`r`nLoadFromScriptsOnly=1`r`nDebugLog=1`r`n"
    Assert-Condition ([Text.Encoding]::ASCII.GetString($contents['version.ini']) -ceq $expectedIni) 'Loader configuration differs'
    if ($Game -ceq 'BlackMythWukong') {
        Assert-Condition ([IO.File]::ReadAllText((Join-Path $source 'Scripts\install.ps1')).Contains($expectedIni.Replace("`r`n", '`r`n'))) 'Installer configuration changed without updating release settings'
    }
    Pass 'Loader uses deferred InitializeASI loading from scripts with logging'

    $packagedReadme = [Text.Encoding]::UTF8.GetString($contents['README.md'])
    foreach ($expectedText in @('RELEASE README FIXTURE', "GlossTrainer v$version", '## licenses', 'By: ', 'version.dll', 'scripts', 'GlossTrainer.asi')) {
        Assert-Condition ($packagedReadme.Contains($expectedText)) "Release README required text missing: $expectedText"
    }
    # Code points keep this script ASCII-compatible with Windows PowerShell 5.1.
    $chineseSections = @(@(0x5b89, 0x88c5, 0x65b9, 0x6cd5), @(0x5305, 0x542b, 0x529f, 0x80fd), @(0x7f72, 0x540d), @(0x5c0f, 0x83ab))
    if ($Game -ceq 'BlackMythWukong') { $chineseSections += ,@(0x9ed1, 0x795e, 0x8bdd, 0xff1a, 0x609f, 0x7a7a, 0x5185, 0x7f6e, 0x4fee, 0x6539, 0x5668) }
    foreach ($codepoints in $chineseSections) {
        $text = -join @($codepoints | ForEach-Object { [char]$_ })
        Assert-Condition ($packagedReadme.Contains($text)) "User-facing README section or attribution missing: $text"
    }
    Assert-Condition (-not $packagedReadme.Contains('ROOT README MUST NOT SHIP') -and $packagedReadme -notmatch '\{\{[A-Z_]+\}\}') 'Wrong README or unresolved placeholder shipped'
    foreach ($relative in @('LICENSE') + @($fixtureInputs | Where-Object { $_ -like 'licenses\*.txt' })) {
        $license = [IO.File]::ReadAllText((Join-Path $fixture $relative))
        Assert-Condition ($license.Length -gt 0 -and $packagedReadme.Contains($license)) "Full license text missing: $relative"
    }
    Pass 'README contains requested sections version attribution and all complete licenses'
    $packagedReadmeEn = [Text.Encoding]::UTF8.GetString($contents['readme_en.md'])
    foreach ($expectedText in @('ENGLISH RELEASE README FIXTURE', "GlossTrainer v$version", $sourceRelease.name,
        '## Installation', '## Features', '## Credits', '## About', '## licenses', 'Current version', 'Latest version', 'Author:',
        'version.dll', 'scripts', 'GlossTrainer.asi', 'README.md')) {
        Assert-Condition ($packagedReadmeEn.Contains($expectedText)) "English README required text missing: $expectedText"
    }
    if ($Game -ceq 'BlackMythWukong') {
        Assert-Condition ($packagedReadmeEn.Contains('Black Myth: Wukong In-Game Trainer')) 'English Wukong README title missing'
    }
    foreach ($text in @($packagedReadme, $packagedReadmeEn)) {
        foreach ($url in @('https://github.com/GlossMod/GlossTrainer/blob/master/LICENSE',
            'https://github.com/GlossMod/GlossTrainer', 'https://sponsor.aoe.top/', 'https://www.glossmod.com/commissions')) {
            Assert-Condition ($text.Contains($url)) "Bilingual README URL missing: $url"
        }
        foreach ($relative in @('LICENSE') + @($fixtureInputs | Where-Object { $_ -like 'licenses\*.txt' })) {
            Assert-Condition ($text.Contains([IO.File]::ReadAllText((Join-Path $fixture $relative)))) "Bilingual README full license missing: $relative"
        }
        Assert-Condition (-not $text.Contains('ROOT README MUST NOT SHIP') -and $text -notmatch '\{\{[A-Z_]+\}\}') 'Bilingual README contains development text or unresolved placeholders'
    }
    Assert-Condition ($packagedReadme.Contains('(readme_en.md)') -and $packagedReadmeEn.Contains('(README.md)')) 'Bilingual README navigation is missing'
    Pass 'both language READMEs contain about links versions complete licenses and reciprocal navigation'

    $manifestPath = Join-Path $releaseDirectory "$stem.manifest.json"
    Assert-Condition (-not $contents.ContainsKey('release-manifest.json')) 'Audit manifest leaked into the install ZIP'
    $manifest = [IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json
    Assert-Condition ($manifest.schemaVersion -eq 3 -and $manifest.project -ceq 'GlossTrainer' -and
        $manifest.game -ceq $Game -and $manifest.version -ceq $version -and
        $manifest.tag -ceq $tag -and $manifest.architecture -ceq 'x64' -and
        $manifest.entryPoint -ceq 'InitializeASI' -and $manifest.sourceRevision -ceq ('1' * 40)) 'Manifest metadata differs'
    Assert-Condition ($manifest.archive.name -ceq [IO.Path]::GetFileName($zipPath) -and $manifest.archive.sha256 -ceq $initialHash -and $manifest.archive.size -eq (Get-Item -LiteralPath $zipPath).Length) 'Manifest archive receipt differs'
    Assert-Condition ($manifest.loader.version -ceq '10.0.0' -and $manifest.loader.archiveSha256 -ceq $loaderSha256 -and
        $manifest.loader.archiveUrl -ceq 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.0/Ultimate-ASI-Loader_x64.zip' -and
        $manifest.loader.archiveEntry -ceq 'dinput8.dll' -and $manifest.loader.installedAs -ceq 'version.dll') 'Manifest pinned Loader provenance differs'
    $manifestPaths = @{}
    foreach ($file in $manifest.files) {
        Assert-Condition (-not $manifestPaths.ContainsKey($file.path)) 'Manifest contains duplicate files'
        $manifestPaths[$file.path] = $true
        Assert-Condition ($contents.ContainsKey($file.path)) 'Manifest references an unexpected file'
        Assert-Condition ($file.size -eq $contents[$file.path].LongLength -and $file.sha256 -ceq (Bytes-Sha256 $contents[$file.path])) "Manifest hash/size differs: $($file.path)"
    }
    Assert-Condition ($manifestPaths.Count -eq $contents.Count) 'Manifest omits payloads'
    Pass 'external manifest records every payload archive checksum source and pinned Loader'

    $cleanInstallation = Join-Path $testRoot 'clean-installation'
    Expand-Archive -LiteralPath $zipPath -DestinationPath $cleanInstallation
    foreach ($path in $expected) { Assert-Condition (Test-Path -LiteralPath (Join-Path $cleanInstallation $path) -PathType Leaf) "Clean extraction missing $path" }
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $cleanInstallation 'scripts\GlossTrainer'))) 'Clean extraction creates default user configuration'
    Assert-Condition (@(Get-ChildItem -LiteralPath $cleanInstallation -Filter '*.asi' -File).Count -eq 0) 'Loader root contains a duplicate ASI'
    Pass 'clean extraction supplies the automatic loading layout without an installer'
    $existingInstallation = Join-Path $testRoot 'existing-installation'
    $configurationDirectory = Join-Path $existingInstallation 'scripts\GlossTrainer'
    New-Item -ItemType Directory -Path $configurationDirectory -Force | Out-Null
    foreach ($configuration in @('ui.ini', 'hotkeys.ini', 'trainer.ini')) { [IO.File]::WriteAllText((Join-Path $configurationDirectory $configuration), "existing-$configuration") }
    Expand-Archive -LiteralPath $zipPath -DestinationPath $existingInstallation -Force
    foreach ($configuration in @('ui.ini', 'hotkeys.ini', 'trainer.ini')) {
        Assert-Condition ([IO.File]::ReadAllText((Join-Path $configurationDirectory $configuration)) -ceq "existing-$configuration") 'ZIP extraction overwrote settings'
    }
    Pass 'updating from the ZIP preserves existing user configuration'
    $initialManifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    Invoke-Packager 'unchanged inputs produce the same ZIP again' $common
    Assert-Condition ((Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $initialHash -and
        (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash -ceq $initialManifestHash) 'Repeated ZIP or manifest is not deterministic'
    Pass 'repeat packaging is byte-for-byte deterministic including the audit manifest'
    # A second game exists only inside this fixture. Both channels deliberately
    # have the same version and share an output directory to detect collisions.
    $secondGame = 'FixtureGame'
    New-Item -ItemType Directory -Path (Join-Path $fixture "$secondGame/src") -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $fixture $sourceRelease.versionFile) -Destination (Join-Path $fixture "$secondGame/src/runtime.hpp")
    [IO.File]::WriteAllText((Join-Path $fixture "$secondGame/readme.md"), "SECOND GAME README v{{VERSION}}`n{{LICENSES}}", $utf8)
    [IO.File]::WriteAllText((Join-Path $fixture "$secondGame/readme_en.md"), "SECOND GAME ENGLISH README v{{VERSION}}`n{{LICENSES}}", $utf8)
    [IO.File]::WriteAllText((Join-Path $fixture "$secondGame/tests.lua"), 'target("FixtureGameTests")', $utf8)
    $registryPath = Join-Path $fixture 'Scripts/release-games.json'
    $registry = [IO.File]::ReadAllText($registryPath) | ConvertFrom-Json
    $registry.games += [pscustomobject]@{ id = $secondGame; name = 'Fixture game'; target = $secondGame; versionFile = "$secondGame/src/runtime.hpp"; validationFile = "$secondGame/tests.lua"; readme = "$secondGame/readme.md"; readmeEn = "$secondGame/readme_en.md"; releaseNotes = "$secondGame/readme.md"; packageScript = 'Scripts/package-release.ps1'; packageTests = 'Scripts/test-package-release.ps1'; installerTests = 'Scripts/test-install.ps1'; artifact = "build/release/$secondGame.asi"; format = 'asi-win64' }
    [IO.File]::WriteAllText($registryPath, ($registry | ConvertTo-Json -Depth 6), $utf8)
    $secondStem = "GlossTrainer-$secondGame-v$version-win64"
    Invoke-Packager 'second registered game packages independently at the same version' @('-Artifact', $artifactPath, '-Game', $secondGame, '-SourceRoot', $fixture, '-OutputDirectory', $releaseDirectory, '-Tag', "$secondGame/v$version")
    $secondContents = Read-Zip (Join-Path $releaseDirectory "$secondStem.zip")
    $secondManifest = [IO.File]::ReadAllText((Join-Path $releaseDirectory "$secondStem.manifest.json")) | ConvertFrom-Json
    Assert-Condition ([Text.Encoding]::UTF8.GetString($secondContents['README.md']).Contains('SECOND GAME README') -and $secondManifest.game -ceq $secondGame) 'Second game used another games README or receipt'
    Assert-Condition ([Text.Encoding]::UTF8.GetString($secondContents['readme_en.md']).Contains('SECOND GAME ENGLISH README')) 'Second game used another games English README'
    Assert-Condition ((Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $initialHash -and (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash -ceq $initialManifestHash) 'Second game overwrote the first game archive or manifest'
    Assert-Condition ([IO.File]::ReadAllText((Join-Path $releaseDirectory "$stem.SHA256SUMS.txt")) -ceq "$initialHash  $([IO.Path]::GetFileName($zipPath))`n") 'Second game overwrote the first game checksum'
    Pass 'same-directory cross-game packaging preserves the first archive checksum and manifest'
    Invoke-Packager 'cross-game tag cannot package another selected game' @('-Artifact', $artifactPath, '-Game', $secondGame, '-SourceRoot', $fixture, '-OutputDirectory', (Join-Path $testRoot 'cross-game'), '-Tag', $tag) 1 'does not match selected game'
    Invoke-Packager 'unscoped legacy tag cannot publish a game' @('-Artifact', $artifactPath, '-SourceRoot', $fixture, '-OutputDirectory', (Join-Path $testRoot 'legacy-tag'), '-Tag', "v$version") 1 'Release tag must be'
    Invoke-Packager 'mismatching tag is rejected' @('-Artifact', $artifactPath, '-SourceRoot', $fixture, '-OutputDirectory', (Join-Path $testRoot 'bad-tag'), '-Tag', "$Game/v999.999.999") 1 'does not match source version'
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $testRoot 'bad-tag'))) 'Rejected tag created output'

    $englishTemplate = Join-Path $fixture $sourceRelease.readmeEn
    $englishOriginal = [IO.File]::ReadAllText($englishTemplate)
    try {
        [IO.File]::WriteAllText($englishTemplate, $englishOriginal.Replace('{{LICENSES}}', ''), $utf8)
        Invoke-Packager 'English README without complete license placeholder is rejected' @('-Artifact', $artifactPath, '-SourceRoot', $fixture, '-OutputDirectory', (Join-Path $testRoot 'bad-english')) 1 'exactly one LICENSES placeholder'
        Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $testRoot 'bad-english'))) 'Invalid English template created output'
        [IO.File]::WriteAllText($englishTemplate, $englishOriginal + "`n{{UNRESOLVED}}", $utf8)
        Invoke-Packager 'English README with unresolved placeholder is rejected' @('-Artifact', $artifactPath, '-SourceRoot', $fixture, '-OutputDirectory', (Join-Path $testRoot 'bad-english-placeholder')) 1 'unresolved placeholders'
    } finally { [IO.File]::WriteAllText($englishTemplate, $englishOriginal, $utf8) }

    $original = [IO.File]::ReadAllBytes($artifactPath)
    $pe = [BitConverter]::ToUInt32($original, 0x3c)
    foreach ($failureCase in @('x86', 'no-export-directory', 'missing-initialize-asi', 'not-dll')) {
        $invalid = [byte[]]$original.Clone()
        if ($failureCase -eq 'x86') { $invalid[$pe + 4] = 0x4c; $invalid[$pe + 5] = 0x01; $diagnostic = 'ASI must be Windows x64.' }
        elseif ($failureCase -eq 'no-export-directory') { [Array]::Clear($invalid, $pe + 24 + 112, 8); $diagnostic = 'ASI has no export directory.' }
        elseif ($failureCase -eq 'not-dll') { $invalid[$pe + 23] = $invalid[$pe + 23] -band 0xdf; $diagnostic = 'ASI must contain valid PE32+ DLL headers.' }
        else {
            $ascii = [Text.Encoding]::ASCII.GetString($invalid)
            $position = $ascii.IndexOf('InitializeASI', [StringComparison]::Ordinal)
            Assert-Condition ($position -ge 0) 'Fixture InitializeASI string missing'
            while ($position -ge 0) {
                $invalid[$position] = [byte][char]'X'
                $position = $ascii.IndexOf('InitializeASI', $position + 1, [StringComparison]::Ordinal)
            }
            $diagnostic = 'ASI does not export InitializeASI.'
        }
        $invalidPath = Join-Path $testRoot ($failureCase + '.asi')
        [IO.File]::WriteAllBytes($invalidPath, $invalid)
        Invoke-Packager "$failureCase ASI is rejected" @('-Artifact', $invalidPath, '-SourceRoot', $fixture, '-OutputDirectory', (Join-Path $testRoot $failureCase)) 1 $diagnostic
    }

    $corruptLoader = Join-Path $testRoot 'corrupt-loader.zip'
    $corruptBytes = [IO.File]::ReadAllBytes($loaderPath)
    $corruptBytes[0] = $corruptBytes[0] -bxor 1
    [IO.File]::WriteAllBytes($corruptLoader, $corruptBytes)
    Invoke-Packager 'corrupt supplied Loader is rejected' @('-Artifact', $artifactPath, '-SourceRoot', $fixture, '-OutputDirectory', (Join-Path $testRoot 'bad-loader'), '-LoaderArchive', $corruptLoader) 1 'Supplied Loader archive SHA-256 mismatch.'
    Assert-Condition (@(Get-ChildItem -LiteralPath (Join-Path $testRoot 'bad-loader') -Filter '*.zip' -File).Count -eq 0) 'Corrupt Loader produced an install ZIP'
    $cacheOutput = Join-Path $testRoot 'cache-release'
    $cacheDirectory = Join-Path $cacheOutput 'cache\Ultimate-ASI-Loader\v10.0.0'
    New-Item -ItemType Directory -Path $cacheDirectory -Force | Out-Null
    $cacheArchive = Join-Path $cacheDirectory 'Ultimate-ASI-Loader_x64.zip'
    Copy-Item -LiteralPath $loaderPath -Destination $cacheArchive
    $cacheArguments = @('-Artifact', $artifactPath, '-SourceRoot', $fixture, '-OutputDirectory', $cacheOutput)
    Invoke-Packager 'cached official Loader supports fully offline packaging' $cacheArguments -UseCache
    $cacheZip = Join-Path $cacheOutput "$stem.zip"
    $cacheZipHash = (Get-FileHash -LiteralPath $cacheZip -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $corruptLoader -Destination $cacheArchive -Force
    Invoke-Packager 'cached Loader is rehashed and corruption is rejected' $cacheArguments 1 'Cached Loader archive SHA-256 mismatch:' -UseCache
    Assert-Condition ((Get-FileHash -LiteralPath $cacheZip -Algorithm SHA256).Hash -ceq $cacheZipHash) 'Corrupt cache replaced the previous valid package'

    # The fixed SHA rejects x86 replacement archives before extraction. Exercise
    # the PE guard independently without weakening production archive validation.
    $tokens = $null; $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($packager, [ref]$tokens, [ref]$parseErrors)
    Assert-Condition ($parseErrors.Count -eq 0) 'Packager has parser errors'
    $validator = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Assert-X64Dll' }, $true)
    Assert-Condition ($null -ne $validator) 'Loader architecture validator missing'
    . ([scriptblock]::Create($validator.Extent.Text))
    Assert-X64Dll $contents['version.dll']
    $invalidLoader = [byte[]]$contents['version.dll'].Clone()
    $loaderPe = [BitConverter]::ToUInt32($invalidLoader, 0x3c)
    $invalidLoader[$loaderPe + 4] = 0x4c; $invalidLoader[$loaderPe + 5] = 0x01
    $rejected = $false
    try { Assert-X64Dll $invalidLoader } catch { $rejected = $_.Exception.Message -ceq 'Loader must be a Windows x64 PE32+ DLL.' }
    Assert-Condition $rejected 'Loader PE validator accepted x86'
    Pass 'Loader PE validator independently rejects an x86 DLL'
    Move-Item -LiteralPath (Join-Path $fixture 'licenses\ultimate_asi_loader.txt') -Destination (Join-Path $testRoot 'loader-license.missing')
    Invoke-Packager 'missing Loader redistribution license is rejected' @('-Artifact', $artifactPath, '-SourceRoot', $fixture, '-OutputDirectory', (Join-Path $testRoot 'missing-license')) 1 'Required license is missing: ultimate_asi_loader.txt'
    Write-Host "GlossTrainer portable release tests passed ($checks checks). Evidence: $testRoot"
}
catch { $failure = $_.Exception.Message; throw }
finally {
    if (Test-Path -LiteralPath $testRoot -PathType Container) {
        $report = [ordered]@{ passed = ($null -eq $failure); game = $Game; checks = $checks; powerShell = $PSVersionTable.PSVersion.ToString(); artifact = $Artifact; loaderArchive = $loaderPath; failure = $failure; results = @($results.ToArray()) }
        $snapshot = Join-Path $testRoot 'input.asi'
        if (Test-Path -LiteralPath $snapshot -PathType Leaf) { $report.artifactSha256 = (Get-FileHash -LiteralPath $snapshot -Algorithm SHA256).Hash.ToLowerInvariant() }
        [IO.File]::WriteAllText((Join-Path $testRoot 'results.json'), ($report | ConvertTo-Json -Depth 6) + "`n", $utf8)
    }
}
