[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')][string]$Mode = 'release',
    [ValidateSet('dx11', 'dx12')][string]$Renderer = 'dx12',
    [ValidateRange(0, 5)][int]$Page = 1,
    [switch]$Light,
    [ValidateSet('zh-CN', 'en')][string]$Language = 'zh-CN',
    [switch]$GridFixture,
    [string]$IconSamplePath,
    [ValidateSet('grid', 'hover', 'quantity', 'features', 'settings')][string]$FixtureView = 'grid',
    [ValidateRange(0.8, 1.5)][double]$Scale = 1,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$artifactName = if ($GridFixture) { 'PBZGridFixture.asi' } else { 'GlossTrainer.asi' }
$asi = Join-Path $root "build/$Mode/$artifactName"
$hostExe = Join-Path $root 'build/release/GlossTrainer.PBZ.Host.exe'
if (-not (Test-Path -LiteralPath $asi)) { throw "Build PBZ in $Mode first: $asi" }
if (-not (Test-Path -LiteralPath $hostExe)) { throw 'Build PBZHost in release first (requires the local Tests/host.cpp).' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$outputRoot = [IO.Path]::GetFullPath((Join-Path $root 'output')).TrimEnd('\') + '\'
if (-not $output.StartsWith($outputRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Validation artifacts must be stored under repository-root output/.'
}
if (Test-Path -LiteralPath $output) { throw "Use a new evidence directory: $output" }
$settings = Join-Path $output 'artifact/GlossTrainer/PBZ'
$null = New-Item -ItemType Directory -Path $settings -Force
$testAsi = Join-Path $output "artifact/$artifactName"
Copy-Item -LiteralPath $asi -Destination $testAsi
if ($GridFixture) {
    if ($FixtureView -notin @('features', 'settings') -and (-not $IconSamplePath -or (Get-Item -LiteralPath $IconSamplePath).Length -ne 262144)) {
        throw 'Grid fixture requires one captured 512x512 BC3 sample (262144 bytes).'
    }
    if ($FixtureView -notin @('features', 'settings')) { Copy-Item -LiteralPath $IconSamplePath -Destination (Join-Path $settings 'fixture.dxt5') }
    [IO.File]::WriteAllText((Join-Path $settings 'fixture-mode.txt'), $FixtureView)
}
$scaleText = $Scale.ToString([Globalization.CultureInfo]::InvariantCulture)
[IO.File]::WriteAllText((Join-Path $settings 'ui.ini'), "[UI]`r`nLightTheme=$([int]$Light.IsPresent)`r`nScale=$scaleText`r`nPage=$Page`r`nX=-1`r`nY=-1`r`n")
[IO.File]::WriteAllText((Join-Path $settings 'language.ini'), "[Interface]`r`nLanguage=$Language`r`n")

# This existing host-only flag performs a minimize/restore before timed tests,
# allowing a background terminal launch to acquire foreground focus. The PBZ
# production ASI does not inspect it and retains all normal focus-loss handling.
$previousBootstrap = $env:GLOSS_SKILL_UI_THEME
try {
    $env:GLOSS_SKILL_UI_THEME = 'pbz-host-focus-bootstrap'
    $automationFlag = if ($GridFixture) { '--visual-only' } else { '--automated' }
    & $hostExe "--$Renderer" $automationFlag "--asi=$testAsi" "--output=$output" *> (Join-Path $output 'console.log')
    $hostCode = $LASTEXITCODE
    [ordered]@{
        mode = $Mode; renderer = $Renderer; page = $Page; light = $Light.IsPresent; scale = $Scale; language = $Language
        fixtureView = if ($GridFixture) { $FixtureView } else { $null }
        exitCode = $hostCode; sha256 = (Get-FileHash -LiteralPath $testAsi -Algorithm SHA256).Hash
        scope = if ($GridFixture) { 'Visual fixture; no game connection, grants or input validation' } else { 'Local graphics host; not PBZ in-game validation' }
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'result.json') -Encoding UTF8
    Get-Content -LiteralPath (Join-Path $output 'console.log') -Tail 4
    if ($hostCode -ne 0) { throw "PBZ host failed ($hostCode). See $output" }
}
finally {
    $env:GLOSS_SKILL_UI_THEME = $previousBootstrap
}
