[CmdletBinding()]
param([string]$LoaderArchive)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$installer = Join-Path $PSScriptRoot 'install.ps1'
$engine = Join-Path $PSHOME 'powershell.exe'
if (-not (Test-Path -LiteralPath $engine -PathType Leaf)) { $engine = Join-Path $PSHOME 'pwsh.exe' }
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('GlossTrainer-deploy-test-' + [guid]::NewGuid().ToString('N'))
$game = Join-Path $testRoot 'fake game\Win64'
$cache = Join-Path $testRoot 'cache'
$artifact = Join-Path $testRoot 'test.asi'
$checks = 0
$fixtureProcess = $null

function Assert-Condition([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "ASSERTION FAILED: $Message" }
}

function Invoke-Installer([string]$Name, [string[]]$Arguments, [int]$ExpectedExit = 0) {
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $engine -NoProfile -ExecutionPolicy Bypass -File $installer @Arguments 2>&1
        $actualExit = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previousPreference }
    if ($actualExit -ne $ExpectedExit) {
        throw "$Name expected exit $ExpectedExit, got $actualExit.`n$($output -join [Environment]::NewLine)"
    }
    $script:checks++
    Write-Host "PASS: $Name"
}

try {
    if (@(Get-Process -Name 'b1', 'b1-Win64-Shipping' -ErrorAction SilentlyContinue).Count -gt 0) {
        throw 'Exit the real game before running deployment fixture tests.'
    }
    New-Item -ItemType Directory -Path $game -Force | Out-Null
    # System PE files are used only as deployment fixtures; the ASI is never loaded.
    Copy-Item -LiteralPath (Join-Path $env:WINDIR 'System32\ping.exe') -Destination (Join-Path $game 'b1-Win64-Shipping.exe')
    Copy-Item -LiteralPath (Join-Path $env:WINDIR 'System32\kernel32.dll') -Destination $artifact
    if (-not [string]::IsNullOrWhiteSpace($LoaderArchive)) {
        New-Item -ItemType Directory -Path $cache | Out-Null
        Copy-Item -LiteralPath $LoaderArchive -Destination (Join-Path $cache 'Ultimate-ASI-Loader_x64.zip')
    }
    $common = @('-ExeDir', $game, '-Artifact', $artifact, '-CacheDir', $cache)
    $missing = Join-Path $testRoot 'must-not-be-created'
    Invoke-Installer 'nonexistent game path is rejected' @('-ExeDir', $missing, '-Artifact', $artifact, '-DryRun') 1
    Assert-Condition (-not (Test-Path -LiteralPath $missing)) 'invalid game path was created'
    Invoke-Installer 'missing game executable is rejected' @('-ExeDir', $testRoot, '-Artifact', $artifact, '-DryRun') 1
    Invoke-Installer 'non-DLL artifact is rejected' @('-ExeDir', $game, '-Artifact', (Join-Path $game 'b1-Win64-Shipping.exe'), '-DryRun') 1
    Invoke-Installer 'DryRun previews without game writes' ($common + @('-DryRun'))
    Invoke-Installer 'WhatIf previews without game writes' ($common + @('-WhatIf'))
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $game 'scripts'))) 'preview created scripts directory'

    foreach ($relative in @('version.dll', 'version.ini', 'scripts\GlossTrainer.asi')) {
        $conflict = Join-Path $game $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $conflict) -Force | Out-Null
        [IO.File]::WriteAllText($conflict, 'unknown file')
        Invoke-Installer "unknown $relative is protected" ($common + @('-DryRun')) 1
        Assert-Condition ([IO.File]::ReadAllText($conflict) -eq 'unknown file') "unknown file changed: $relative"
        Remove-Item -LiteralPath $conflict
    }

    $badCache = Join-Path $testRoot 'bad-cache'
    New-Item -ItemType Directory -Path $badCache | Out-Null
    [IO.File]::WriteAllText((Join-Path $badCache 'Ultimate-ASI-Loader_x64.zip'), 'wrong hash')
    Invoke-Installer 'corrupt cache is rejected' @('-ExeDir', $game, '-Artifact', $artifact, '-CacheDir', $badCache) 1
    Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $game 'version.dll'))) 'corrupt archive installed a Loader'

    $fixtureProcess = Start-Process -FilePath (Join-Path $game 'b1-Win64-Shipping.exe') -ArgumentList @('-t', '127.0.0.1') -WindowStyle Hidden -PassThru
    Invoke-Installer 'running game process blocks install' ($common + @('-DryRun')) 1
    Stop-Process -Id $fixtureProcess.Id -Force
    $fixtureProcess.WaitForExit()
    $fixtureProcess = $null

    [IO.File]::WriteAllText((Join-Path $game 'dwmapi.dll'), 'existing proxy')
    [IO.File]::WriteAllText((Join-Path $game 'wukong_minimap.dll'), 'existing minimap')
    Invoke-Installer 'clean install downloads or validates fixed Loader' $common
    $manifest = Join-Path $game 'scripts\GlossTrainer\install-manifest.json'
    Assert-Condition (Test-Path -LiteralPath $manifest -PathType Leaf) 'manifest missing'
    $parsed = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    Assert-Condition (@($parsed.files).Count -eq 3) 'manifest file count differs'
    foreach ($entry in $parsed.files) {
        $hash = (Get-FileHash -LiteralPath (Join-Path $game $entry.path) -Algorithm SHA256).Hash
        Assert-Condition ($hash -eq $entry.sha256) "installed hash differs: $($entry.path)"
    }
    $configuration = Join-Path $game 'scripts\GlossTrainer\ui.ini'
    $logs = Join-Path $game 'scripts\GlossTrainer\logs'
    New-Item -ItemType Directory -Path $logs | Out-Null
    [IO.File]::WriteAllText($configuration, 'user settings')
    [IO.File]::WriteAllText((Join-Path $logs 'trainer.log'), 'runtime log')
    Invoke-Installer 'repeat installation updates owned files' $common
    Assert-Condition ([IO.File]::ReadAllText($configuration) -eq 'user settings') 'update changed UI settings'
    Remove-Item -LiteralPath (Join-Path $game 'scripts\GlossTrainer.asi')
    Invoke-Installer 'missing owned file is repaired' $common

    $ownedIni = Join-Path $game 'version.ini'
    $originalIni = [IO.File]::ReadAllBytes($ownedIni)
    [IO.File]::AppendAllText($ownedIni, '; external change')
    Invoke-Installer 'external modification blocks update' $common 1
    Invoke-Installer 'external modification blocks uninstall' @('-ExeDir', $game, '-Uninstall') 1
    Assert-Condition (Test-Path -LiteralPath (Join-Path $game 'version.dll')) 'rejected uninstall partially removed Loader'
    [IO.File]::WriteAllBytes($ownedIni, $originalIni)
    Invoke-Installer 'uninstall preview preserves owned files' @('-ExeDir', $game, '-Uninstall', '-WhatIf')
    Assert-Condition (Test-Path -LiteralPath $manifest) 'uninstall preview deleted manifest'
    Invoke-Installer 'manifest uninstall removes owned files' @('-ExeDir', $game, '-Uninstall')
    foreach ($relative in @('version.dll', 'version.ini', 'scripts\GlossTrainer.asi', 'scripts\GlossTrainer\install-manifest.json')) {
        Assert-Condition (-not (Test-Path -LiteralPath (Join-Path $game $relative))) "uninstall left owned file: $relative"
    }
    Assert-Condition ([IO.File]::ReadAllText($configuration) -eq 'user settings') 'uninstall removed UI settings'
    Assert-Condition ([IO.File]::ReadAllText((Join-Path $logs 'trainer.log')) -eq 'runtime log') 'uninstall removed runtime log'
    Assert-Condition ([IO.File]::ReadAllText((Join-Path $game 'dwmapi.dll')) -eq 'existing proxy') 'existing proxy changed'
    Assert-Condition ([IO.File]::ReadAllText((Join-Path $game 'wukong_minimap.dll')) -eq 'existing minimap') 'existing minimap changed'
    Invoke-Installer 'repeat uninstall is a no-op' @('-ExeDir', $game, '-Uninstall')
    Write-Host "Deployment fixture tests passed: $checks cases. No real game installation or ASI loading was performed."
}
finally {
    if ($null -ne $fixtureProcess -and -not $fixtureProcess.HasExited) { Stop-Process -Id $fixtureProcess.Id -Force }
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $expectedParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolvedTestRoot.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolvedTestRoot).StartsWith('GlossTrainer-deploy-test-') -and
        (Test-Path -LiteralPath $resolvedTestRoot -PathType Container)) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
