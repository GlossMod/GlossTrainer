[CmdletBinding()]
param(
    [string]$Game,
    [string]$Tag,
    [string]$SourceRoot = (Join-Path $PSScriptRoot '..'),
    [switch]$Json
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath($SourceRoot)
function Resolve-SourceFile([string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or
        $Relative -match '(^|[\\/])\.\.([\\/]|$)') { throw "Unsafe release source path: $Relative" }
    $resolved = [IO.Path]::GetFullPath((Join-Path $source $Relative))
    if (-not $resolved.StartsWith($source.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $resolved -PathType Leaf)) { throw "Release source is missing: $Relative" }
    return $resolved
}
if ($Tag) {
    if ($Tag -cnotmatch '^([A-Za-z][A-Za-z0-9]*)/v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw 'Release tag must be Game/vX.Y.Z with a registered game ID.'
    }
    $tagGame = $Matches[1]
    if ($Game -and $Game -cne $tagGame) { throw "Release tag game '$tagGame' does not match selected game '$Game'." }
    $Game = $tagGame
}
if (-not $Game -or $Game -cnotmatch '^[A-Za-z][A-Za-z0-9]*$') { throw 'A registered game ID is required.' }
$registry = [IO.File]::ReadAllText((Join-Path $source 'Scripts/release-games.json')) | ConvertFrom-Json
if ($registry.schemaVersion -ne 1) { throw 'Unsupported release registry schema.' }
$ids = @($registry.games | ForEach-Object { $_.id })
if (@($ids | Sort-Object -Unique).Count -ne $ids.Count) { throw 'Duplicate game IDs in release registry.' }
foreach ($id in $ids) { if ($id -cnotmatch '^[A-Za-z][A-Za-z0-9]*$') { throw 'Invalid game ID in release registry.' } }
$ownedTargets = @{}
$testInventories = @{}
foreach ($registeredGame in $registry.games) {
    if ($registeredGame.target -cnotmatch '^[A-Za-z][A-Za-z0-9]*$') { throw 'Invalid registered game target.' }
    foreach ($field in @('versionFile', 'validationFile', 'readme', 'readmeEn', 'releaseNotes')) {
        $relative = $registeredGame.$field.Replace('\', '/')
        if (-not $relative.StartsWith($registeredGame.id + '/', [StringComparison]::Ordinal)) { throw "Game source field '$field' must belong to its game directory." }
        [void](Resolve-SourceFile $relative)
    }
    $gameTests = @([regex]::Matches([IO.File]::ReadAllText((Resolve-SourceFile $registeredGame.validationFile)), 'target\("([^"\r\n]+Tests)"\)') | ForEach-Object { $_.Groups[1].Value })
    if ($gameTests.Count -eq 0) { throw 'Game validation target inventory is empty.' }
    foreach ($targetName in @($registeredGame.target) + $gameTests) {
        if ($ownedTargets.ContainsKey($targetName)) { throw "Duplicate build or validation target '$targetName' across release games." }
        $ownedTargets[$targetName] = $registeredGame.id
    }
    $testInventories[$registeredGame.id] = $gameTests
}
$selected = @($registry.games | Where-Object { $_.id -ceq $Game })
if ($selected.Count -ne 1) { throw "Unknown release game: $Game" }
$entry = $selected[0]
if ($entry.target -cnotmatch '^[A-Za-z][A-Za-z0-9]*$' -or $entry.format -cne 'asi-win64') { throw 'Unsupported game release target or package format.' }
$versionFile = Resolve-SourceFile $entry.versionFile
$versionMatches = [regex]::Matches([IO.File]::ReadAllText($versionFile), 'inline\s+constexpr\s+char\s+Version\[\]\s*=\s*"((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*))"\s*;')
if ($versionMatches.Count -ne 1) { throw 'Exactly one formal source version is required.' }
$version = $versionMatches[0].Groups[1].Value
$expectedTag = "$Game/v$version"
if ($Tag -and $Tag -cne $expectedTag) { throw "Release tag '$Tag' does not match source version '$expectedTag'." }
foreach ($field in @('releaseNotes', 'readme', 'readmeEn', 'validationFile', 'packageScript', 'packageTests', 'installerTests')) { [void](Resolve-SourceFile $entry.$field) }
if ($entry.artifact -cne "build/release/$($entry.target).asi") { throw 'Artifact must match the registered game target.' }
$tests = $testInventories[$Game]
$stem = "GlossTrainer-$Game-v$version-win64"
$result = [pscustomobject][ordered]@{
    game = $Game; name = $entry.name; version = $version; tag = $expectedTag; versionTag = "v$version"
    target = $entry.target; versionFile = $entry.versionFile; validationFile = $entry.validationFile
    readme = $entry.readme; readmeEn = $entry.readmeEn; releaseNotes = $entry.releaseNotes; packageScript = $entry.packageScript; packageTests = $entry.packageTests
    installerTests = $entry.installerTests; artifact = $entry.artifact; tests = $tests
    archiveName = "$stem.zip"; checksumName = "$stem.SHA256SUMS.txt"; manifestName = "$stem.manifest.json"
    outputDirectory = "output/releases/$Game/v$version"; candidateName = "release-candidate-$Game-v$version"
}
if ($Json) { $result | ConvertTo-Json -Depth 5 } else { $result }
