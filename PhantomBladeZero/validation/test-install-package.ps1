[CmdletBinding()]
param(
    [string]$Artifact,
    [string]$LoaderArchive,
    [string]$OutputDirectory
)

# Verify the portable ZIP and its documented manual-copy installation contract.
# This is not an automatic game installer and never starts or modifies a game.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputRoot = Join-Path $source 'output'
$utf8 = New-Object Text.UTF8Encoding($false)
function Resolve-InputPath([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $source $Path }
    return [IO.Path]::GetFullPath($Path)
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = 'output/pbz-release/installer' }
$destination = Resolve-InputPath $OutputDirectory
if (-not $destination.StartsWith($outputRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Installer validation artifacts must stay under repository-root output/.'
}
for ($ancestor = $destination; $ancestor.Length -ge $outputRoot.Length; $ancestor = Split-Path -Parent $ancestor) {
    if ((Test-Path -LiteralPath $ancestor) -and
        (((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw "Validation output must not traverse a junction or symbolic link: $ancestor"
    }
}
$testRoot = Join-Path $destination ('ps' + $PSVersionTable.PSVersion.Major + '-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$results = New-Object 'System.Collections.Generic.List[object]'
$failure = $null
$artifactHash = $null
$archiveHash = $null
$archivePath = $null
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "FAILED: $Name" }
    $results.Add([ordered]@{ name = $Name; passed = $true })
    Write-Host "PASS: $Name"
}
function Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Assert-FixturePath([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not $resolved.StartsWith($testRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing a fixture write outside this test run: $Path"
    }
}
try {
    $release = & (Join-Path $source 'Scripts/resolve-release.ps1') -Game PhantomBladeZero
    Check ($release.game -ceq 'PhantomBladeZero' -and $release.target -ceq 'PBZ' -and
        $release.artifact -ceq 'build/release/GlossTrainer.asi') 'PBZ registry resolves its own build target and ASI'
    if ([string]::IsNullOrWhiteSpace($Artifact)) { $Artifact = $release.artifact }
    $artifactSource = Resolve-InputPath $Artifact
    Check (Test-Path -LiteralPath $artifactSource -PathType Leaf) 'Release ASI input exists'
    # Keep a stable input if another local task rebuilds build/release concurrently.
    $artifactSnapshot = Join-Path $testRoot 'input.asi'
    Copy-Item -LiteralPath $artifactSource -Destination $artifactSnapshot
    $artifactHash = Hash $artifactSnapshot
    $packageDirectory = Join-Path $testRoot 'package'
    $arguments = @{
        Game = 'PhantomBladeZero'; Artifact = $artifactSnapshot; SourceRoot = $source
        OutputDirectory = $packageDirectory; Tag = $release.tag
    }
    if (-not [string]::IsNullOrWhiteSpace($LoaderArchive)) {
        $arguments.LoaderArchive = Resolve-InputPath $LoaderArchive
    } else {
        $cachedLoader = Join-Path $source ($release.outputDirectory + '/cache/Ultimate-ASI-Loader/v10.0.0/Ultimate-ASI-Loader_x64.zip')
        if (Test-Path -LiteralPath $cachedLoader -PathType Leaf) { $arguments.LoaderArchive = $cachedLoader }
    }
    $packaged = & (Join-Path $source $release.packageScript) @arguments
    [IO.File]::WriteAllText((Join-Path $testRoot 'package-result.json'), ($packaged | ConvertTo-Json -Depth 6) + "`n", $utf8)
    $archivePath = Join-Path $packageDirectory $release.archiveName
    Check (Test-Path -LiteralPath $archivePath -PathType Leaf) 'Packager accepts the x64 ASI with a defined InitializeASI export'
    $archiveHash = Hash $archivePath
    $manifest = [IO.File]::ReadAllText((Join-Path $packageDirectory $release.manifestName)) | ConvertFrom-Json
    Check ($manifest.game -ceq 'PhantomBladeZero' -and $manifest.version -ceq $release.version -and
        $manifest.tag -ceq $release.tag -and $manifest.archive.name -ceq $release.archiveName -and
        $manifest.archive.sha256 -ceq $archiveHash) 'Package receipt identifies the PBZ channel and archive hash'
    Check ([IO.File]::ReadAllText((Join-Path $packageDirectory $release.checksumName)) -ceq
        "$archiveHash  $($release.archiveName)`n") 'Archive checksum file matches the complete ZIP'
    $expected = @('README.md', 'readme_en.md', 'scripts/GlossTrainer.asi', 'version.dll', 'version.ini')
    $extracted = Join-Path $testRoot 'extracted'
    New-Item -ItemType Directory -Path $extracted | Out-Null
    $stream = [IO.File]::OpenRead($archivePath)
    $zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Read, $false)
    try {
        Check ($zip.Entries.Count -eq $expected.Count) 'Portable ZIP contains exactly five allowed files'
        $seen = @{}
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName -cnotin $expected -or $seen.ContainsKey($entry.FullName)) {
                throw "Unexpected or duplicate ZIP path: $($entry.FullName)"
            }
            $seen[$entry.FullName] = $true
            $file = Join-Path $extracted $entry.FullName
            Assert-FixturePath $file
            New-Item -ItemType Directory -Path (Split-Path -Parent $file) -Force | Out-Null
            $inputStream = $entry.Open()
            $outputStream = [IO.File]::Open($file, [IO.FileMode]::CreateNew)
            try { $inputStream.CopyTo($outputStream) }
            finally { $outputStream.Dispose(); $inputStream.Dispose() }
        }
        Check ($seen.Count -eq $expected.Count) 'Extraction accepts only canonical allowlisted paths without duplicates'
    } finally { $zip.Dispose(); $stream.Dispose() }
    Check (@($manifest.files).Count -eq $expected.Count) 'Receipt lists the same five portable files'
    foreach ($relative in $expected) {
        $file = Join-Path $extracted $relative
        $receipts = @($manifest.files | Where-Object { $_.path -ceq $relative })
        Check ($receipts.Count -eq 1 -and $receipts[0].sha256 -ceq (Hash $file) -and
            $receipts[0].size -eq (Get-Item -LiteralPath $file).Length) "Receipt validates $relative"
    }
    Check ((Hash (Join-Path $extracted 'scripts/GlossTrainer.asi')) -ceq $artifactHash) 'Portable ASI equals the source snapshot'
    Check (@(Get-ChildItem -LiteralPath $extracted -Filter '*.asi' -Recurse -File).Count -eq 1) 'Portable ZIP carries one trainer ASI and no legacy PBZ.asi'
    $expectedIni = "[GlobalSets]`r`nDontLoadFromDllMain=1`r`nLoadPlugins=1`r`nLoadFromScriptsOnly=1`r`nDebugLog=1`r`n"
    Check ([IO.File]::ReadAllText((Join-Path $extracted 'version.ini')) -ceq $expectedIni) 'Loader uses deferred initialization and scripts-only plugin loading'
    foreach ($readme in @('README.md', 'readme_en.md')) {
        $text = [IO.File]::ReadAllText((Join-Path $extracted $readme))
        foreach ($required in @("GlossTrainer v$($release.version)", 'platform/steam', 'PBZ.exe', 'Insert',
            'version.dll', 'version.ini', 'scripts/PBZ.asi', 'scripts/GlossTrainer.asi', 'scripts/GlossTrainer/PBZ/',
            'ui.ini', 'hotkeys.ini', 'features.ini', 'language.ini', 'PhantomBladeZero/v',
            '7b4fcd286a350780faf0824d73aa150b21cbf1bccde883e6a96386cbe09973b4', 'bcdec.txt')) {
            if (-not $text.Contains($required)) { throw "$readme is missing its PBZ installation contract: $required" }
        }
        Check ($text -notmatch '\{\{[A-Z_]+\}\}' -and $text -notmatch 'b1-Win64-Shipping\.exe|xmake bi') "$readme describes PBZ paths and controls without unresolved placeholders or a Wukong installer command"
    }

    # Simulate the documented copy operation in a new fixture, never a game path.
    $game = Join-Path $testRoot 'fixture/PBZ/platform/steam'
    $preferences = Join-Path $game 'scripts/GlossTrainer/PBZ'
    New-Item -ItemType Directory -Path (Join-Path $preferences 'logs') -Force | Out-Null
    $preserved = @{}
    foreach ($relative in @('PBZ.exe', 'scripts/OtherPlugin.asi', 'scripts/GlossTrainer/PBZ/ui.ini',
        'scripts/GlossTrainer/PBZ/hotkeys.ini', 'scripts/GlossTrainer/PBZ/features.ini',
        'scripts/GlossTrainer/PBZ/language.ini', 'scripts/GlossTrainer/PBZ/logs/trainer.log')) {
        $file = Join-Path $game $relative
        Assert-FixturePath $file
        [IO.File]::WriteAllText($file, "Fixture content to preserve: $relative`r`n", $utf8)
        $preserved[$relative] = Hash $file
    }
    $legacy = Join-Path $game 'scripts/PBZ.asi'
    [IO.File]::WriteAllText($legacy, 'Fixture legacy trainer; never executable.', $utf8)
    $backup = Join-Path $testRoot 'legacy-PBZ.asi.backup'
    Assert-FixturePath $legacy
    Assert-FixturePath $backup
    $legacyHash = Hash $legacy
    Move-Item -LiteralPath $legacy -Destination $backup
    Check ((Hash $backup) -ceq $legacyHash -and -not (Test-Path -LiteralPath $legacy)) 'Documented legacy trainer move preserves a backup outside the loading directory'
    foreach ($phase in @('initial copy', 'upgrade copy')) {
        if ($phase -eq 'upgrade copy') {
            [IO.File]::WriteAllText((Join-Path $game 'scripts/GlossTrainer.asi'), 'Fixture old ASI content.', $utf8)
        }
        foreach ($relative in $expected) {
            $target = Join-Path $game $relative
            Assert-FixturePath $target
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $extracted $relative) -Destination $target -Force
            Check ((Hash $target) -ceq (Hash (Join-Path $extracted $relative))) "$phase installs exact $relative bytes"
        }
        foreach ($relative in $preserved.Keys) {
            Check ((Hash (Join-Path $game $relative)) -ceq $preserved[$relative]) "$phase preserves $relative"
        }
        Check (-not (Test-Path -LiteralPath $legacy) -and
            (Hash (Join-Path $game 'scripts/GlossTrainer.asi')) -ceq $artifactHash) "$phase leaves only the current trainer and preserves unrelated plugins"
    }
    Write-Host "PBZ portable installation contract passed ($($results.Count) checks). Evidence: $testRoot"
} catch {
    $failure = $_.Exception.Message
    throw
} finally {
    $receipt = [ordered]@{
        passed = ($null -eq $failure); game = 'PhantomBladeZero'; powerShell = $PSVersionTable.PSVersion.ToString()
        scope = 'Portable ZIP and documented manual file-copy fixture only; no game execution or validation of Scripts/install.ps1.'
        artifactSha256 = $artifactHash; archive = $archivePath; archiveSha256 = $archiveHash
        checks = $results.Count; failure = $failure; results = @($results.ToArray())
    }
    [IO.File]::WriteAllText((Join-Path $testRoot 'results.json'), ($receipt | ConvertTo-Json -Depth 6) + "`n", $utf8)
}
