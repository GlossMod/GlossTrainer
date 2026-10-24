[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [string]$ExeDir = 'E:\steam\steamapps\common\BlackMythWukong\b1\Binaries\Win64',
    [string]$Artifact,
    [switch]$Uninstall,
    [switch]$DryRun,
    [string]$CacheDir = (Join-Path $env:LOCALAPPDATA 'GlossTrainer\Cache\Ultimate-ASI-Loader\v10.0.0')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$loaderVersion = '10.0.0'
$archiveSha256 = '3d5f2f2a47f456db878d17c1b135b0eae3209a7d8dabb0ff3f84e1b008c4f3a7'
$archiveUrl = 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.0/Ultimate-ASI-Loader_x64.zip'
$relativeFiles = @('version.dll', 'version.ini', 'scripts\GlossTrainer.asi')
$manifestRelativePath = 'scripts\GlossTrainer\install-manifest.json'

function Get-Sha256([string]$Path) {
    # Get-FileHash in Windows PowerShell 5.1 inherits -WhatIf internally. Hashing
    # through a read-only stream keeps preflight identical for preview and install.
    $stream = [IO.File]::OpenRead($Path)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $sha256.Dispose(); $stream.Dispose() }
}

function Assert-X64Pe([string]$Path, [bool]$RequireDll = $false) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) {
            throw "Not a PE file: $Path"
        }
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt ($stream.Length - 26)) { throw "Invalid PE header: $Path" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw "Expected a Windows x64 PE file: $Path"
        }
        $stream.Position = $offset + 22
        $characteristics = $reader.ReadUInt16()
        $optionalMagic = $reader.ReadUInt16()
        if ($optionalMagic -ne 0x20b -or ($RequireDll -and ($characteristics -band 0x2000) -eq 0)) {
            throw "Expected a Windows x64 $(if ($RequireDll) { 'DLL/ASI' } else { 'executable' }): $Path"
        }
    }
    finally { $reader.Dispose(); $stream.Dispose() }
}

function Assert-NoReparsePoint([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        $item = Get-Item -LiteralPath $Path -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to write through a symbolic link or junction: $Path"
        }
    }
}

function Assert-GameStopped {
    $running = @(Get-Process -Name 'b1', 'b1-Win64-Shipping' -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        throw 'Black Myth: Wukong is running. Exit the game before installing or uninstalling.'
    }
}

function Read-Manifest([string]$Path, [string]$GameDirectory) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $parsed = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    if ($parsed.schemaVersion -ne 1 -or $parsed.project -ne 'GlossTrainer' -or
        -not [string]::Equals($parsed.gameDirectory, $GameDirectory, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unrecognized installation manifest: $Path"
    }
    $entries = @($parsed.files)
    if ($entries.Count -ne $relativeFiles.Count) { throw "Invalid file list in manifest: $Path" }
    $seen = @{}
    foreach ($entry in $entries) {
        if ($entry.path -cnotin $relativeFiles -or $seen.ContainsKey($entry.path) -or
            $entry.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
            throw "Invalid file entry in manifest: $Path"
        }
        $seen[$entry.path] = $entry.sha256.ToLowerInvariant()
    }
    return $seen
}

function Assert-OwnedFiles([string]$GameDirectory, $OwnedFiles) {
    foreach ($relative in $relativeFiles) {
        $destination = Join-Path $GameDirectory $relative
        if (Test-Path -LiteralPath $destination) {
            if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
                throw "Expected a file, found another filesystem object: $destination"
            }
            if ($null -eq $OwnedFiles -or -not $OwnedFiles.ContainsKey($relative)) {
                throw "File conflict; no GlossTrainer ownership record exists: $destination"
            }
            if ((Get-Sha256 $destination) -ne $OwnedFiles[$relative]) {
                throw "File was changed outside this installer; refusing to overwrite or delete: $destination"
            }
        }
    }
}

function Get-LoaderArchive([string]$Directory) {
    $archivePath = Join-Path $Directory 'Ultimate-ASI-Loader_x64.zip'
    if (Test-Path -LiteralPath $archivePath -PathType Leaf) {
        if ((Get-Sha256 $archivePath) -ne $archiveSha256) {
            throw "Cached Loader archive SHA-256 mismatch. Remove this file and retry: $archivePath"
        }
        return $archivePath
    }
    New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    $partial = Join-Path $Directory ([guid]::NewGuid().ToString('N') + '.download')
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Write-Host "Downloading Ultimate ASI Loader v$loaderVersion x64..."
        Invoke-WebRequest -Uri $archiveUrl -OutFile $partial -UseBasicParsing
        if ((Get-Sha256 $partial) -ne $archiveSha256) { throw 'Downloaded Loader archive SHA-256 mismatch.' }
        Move-Item -LiteralPath $partial -Destination $archivePath
    }
    finally { if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force } }
    return $archivePath
}

$stagingDirectory = $null
try {
    if ([string]::IsNullOrWhiteSpace($ExeDir) -or -not [IO.Path]::IsPathRooted($ExeDir)) {
        throw 'ExeDir must be an existing absolute path to the game executable directory.'
    }
    $gameDirectory = [IO.Path]::GetFullPath($ExeDir).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $gameDirectory -PathType Container)) {
        throw "Game executable directory does not exist; no directories were created: $gameDirectory"
    }
    $gameExe = Join-Path $gameDirectory 'b1-Win64-Shipping.exe'
    if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) { throw "Game executable not found: $gameExe" }
    Assert-X64Pe $gameExe
    Assert-GameStopped
    foreach ($relative in @('', 'scripts', 'scripts\GlossTrainer') + $relativeFiles + @($manifestRelativePath)) {
        $candidate = if ($relative.Length -eq 0) { $gameDirectory } else { Join-Path $gameDirectory $relative }
        Assert-NoReparsePoint $candidate
    }
    foreach ($relative in @('scripts', 'scripts\GlossTrainer')) {
        $candidate = Join-Path $gameDirectory $relative
        if ((Test-Path -LiteralPath $candidate) -and -not (Test-Path -LiteralPath $candidate -PathType Container)) {
            throw "Expected a directory: $candidate"
        }
    }
    $manifestPath = Join-Path $gameDirectory $manifestRelativePath
    if ((Test-Path -LiteralPath $manifestPath) -and -not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Expected an installation manifest file: $manifestPath"
    }
    $ownedFiles = Read-Manifest $manifestPath $gameDirectory
    $initialManifestHash = if (Test-Path -LiteralPath $manifestPath -PathType Leaf) { Get-Sha256 $manifestPath } else { $null }
    Assert-OwnedFiles $gameDirectory $ownedFiles

    if ($Uninstall) {
        if ($null -eq $ownedFiles) {
            Write-Host 'No GlossTrainer installation manifest found; nothing was removed.'
            exit 0
        }
        Write-Host "Uninstall plan: remove owned Loader, Loader configuration, ASI, and manifest from $gameDirectory"
        Write-Host 'GlossTrainer settings/logs and all other plugins will be preserved.'
        if ($DryRun -or -not $PSCmdlet.ShouldProcess($gameDirectory, 'Uninstall GlossTrainer owned files')) { exit 0 }
        Assert-GameStopped
        Assert-OwnedFiles $gameDirectory $ownedFiles
        foreach ($relative in $relativeFiles) {
            $destination = Join-Path $gameDirectory $relative
            if (Test-Path -LiteralPath $destination -PathType Leaf) { Remove-Item -LiteralPath $destination }
        }
        Remove-Item -LiteralPath $manifestPath
        Write-Host 'Uninstalled GlossTrainer. Settings and logs were preserved.'
        exit 0
    }

    if ([string]::IsNullOrWhiteSpace($Artifact)) { throw 'Artifact is required for installation. Build the ASI first or use xmake bi.' }
    $artifactPath = [IO.Path]::GetFullPath($Artifact)
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) { throw "ASI artifact not found: $artifactPath" }
    Assert-X64Pe $artifactPath $true
    Write-Host "Install plan: $artifactPath -> $(Join-Path $gameDirectory 'scripts\GlossTrainer.asi')"
    Write-Host "Loader: v$loaderVersion x64, version.dll; archive SHA-256: $archiveSha256"
    Write-Host 'Loader configuration: DontLoadFromDllMain=1, LoadPlugins=1, LoadFromScriptsOnly=1, DebugLog=1'
    if ($DryRun -or -not $PSCmdlet.ShouldProcess($gameDirectory, 'Install or update GlossTrainer and Ultimate ASI Loader')) { exit 0 }

    $archivePath = Get-LoaderArchive ([IO.Path]::GetFullPath($CacheDir))
    $stagingDirectory = Join-Path ([IO.Path]::GetTempPath()) ('GlossTrainer-install-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
    $extractDirectory = Join-Path $stagingDirectory 'loader'
    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractDirectory
    $loaderDll = Join-Path $extractDirectory 'version.dll'
    if (-not (Test-Path -LiteralPath $loaderDll -PathType Leaf)) { $loaderDll = Join-Path $extractDirectory 'dinput8.dll' }
    if (-not (Test-Path -LiteralPath $loaderDll -PathType Leaf)) { throw 'Verified archive contains neither version.dll nor dinput8.dll.' }
    Assert-X64Pe $loaderDll $true
    $stagedIni = Join-Path $stagingDirectory 'version.ini'
    [IO.File]::WriteAllText($stagedIni, "[GlobalSets]`r`nDontLoadFromDllMain=1`r`nLoadPlugins=1`r`nLoadFromScriptsOnly=1`r`nDebugLog=1`r`n", [Text.Encoding]::ASCII)
    $stagedAsi = Join-Path $stagingDirectory 'GlossTrainer.asi'
    Copy-Item -LiteralPath $artifactPath -Destination $stagedAsi
    $sources = @{ 'version.dll' = $loaderDll; 'version.ini' = $stagedIni; 'scripts\GlossTrainer.asi' = $stagedAsi }
    $manifestFiles = @($relativeFiles | ForEach-Object { @{ path = $_; sha256 = (Get-Sha256 $sources[$_]) } })
    $manifest = [ordered]@{
        schemaVersion = 1
        project = 'GlossTrainer'
        gameDirectory = $gameDirectory
        installedAtUtc = [DateTime]::UtcNow.ToString('o')
        loaderVersion = $loaderVersion
        loaderArchiveSha256 = $archiveSha256
        files = $manifestFiles
    }
    $stagedManifest = Join-Path $stagingDirectory 'install-manifest.json'
    [IO.File]::WriteAllText($stagedManifest, ($manifest | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))

    # Validate again after downloading. An external change must never become owned by this installer.
    Assert-GameStopped
    $currentManifestHash = if (Test-Path -LiteralPath $manifestPath -PathType Leaf) { Get-Sha256 $manifestPath } else { $null }
    if ($currentManifestHash -ne $initialManifestHash) { throw 'Installation manifest changed during preparation. Retry after other installers have finished.' }
    foreach ($relative in @('', 'scripts', 'scripts\GlossTrainer') + $relativeFiles + @($manifestRelativePath)) {
        Assert-NoReparsePoint $(if ($relative.Length -eq 0) { $gameDirectory } else { Join-Path $gameDirectory $relative })
    }
    Assert-OwnedFiles $gameDirectory $ownedFiles
    $originals = @{}
    $written = New-Object 'System.Collections.Generic.List[string]'
    foreach ($relative in $relativeFiles + @($manifestRelativePath)) {
        $destination = Join-Path $gameDirectory $relative
        if (Test-Path -LiteralPath $destination -PathType Leaf) {
            $backup = Join-Path $stagingDirectory ([guid]::NewGuid().ToString('N') + '.backup')
            Copy-Item -LiteralPath $destination -Destination $backup
            $originals[$relative] = $backup
        }
    }
    try {
        New-Item -ItemType Directory -Path (Join-Path $gameDirectory 'scripts\GlossTrainer') -Force | Out-Null
        foreach ($relative in $relativeFiles) {
            $written.Add($relative)
            Copy-Item -LiteralPath $sources[$relative] -Destination (Join-Path $gameDirectory $relative) -Force
        }
        $written.Add($manifestRelativePath)
        Copy-Item -LiteralPath $stagedManifest -Destination $manifestPath -Force
        $installedFiles = Read-Manifest $manifestPath $gameDirectory
        Assert-OwnedFiles $gameDirectory $installedFiles
    }
    catch {
        $failure = $_
        foreach ($relative in $written) {
            $destination = Join-Path $gameDirectory $relative
            if ($originals.ContainsKey($relative)) { Copy-Item -LiteralPath $originals[$relative] -Destination $destination -Force }
            elseif (Test-Path -LiteralPath $destination -PathType Leaf) { Remove-Item -LiteralPath $destination -Force }
        }
        throw $failure
    }
    Write-Host "Installed GlossTrainer. Manifest: $manifestPath"
}
catch {
    Write-Error -Message $_.Exception.Message -ErrorAction Continue
    exit 1
}
finally {
    if ($null -ne $stagingDirectory -and (Test-Path -LiteralPath $stagingDirectory -PathType Container)) {
        $resolvedStaging = [IO.Path]::GetFullPath($stagingDirectory)
        $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if ($resolvedStaging.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
            [IO.Path]::GetFileName($resolvedStaging).StartsWith('GlossTrainer-install-')) {
            Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
        }
    }
}
