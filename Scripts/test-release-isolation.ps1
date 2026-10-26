[CmdletBinding()]
param([string]$OutputDirectory = (Join-Path $PSScriptRoot '../output/release-isolation'))

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$utf8 = New-Object Text.UTF8Encoding($false)
$root = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ('ps' + $PSVersionTable.PSVersion.Major + '-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $root 'Scripts') -Force | Out-Null
$resolver = Join-Path $PSScriptRoot 'resolve-release.ps1'
$results = New-Object 'System.Collections.Generic.List[object]'
$failure = $null
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "FAILED: $Name" }
    $results.Add([ordered]@{ name = $Name; passed = $true })
    Write-Host "PASS: $Name"
}
function Reject([string]$Name, [scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $_.Exception.Message.Contains($Message) }
    Check $rejected $Name
}
try {
    $entries = @()
    foreach ($id in @('FirstGame', 'SecondGame')) {
        New-Item -ItemType Directory -Path (Join-Path $root "$id/src") -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $root "$id/src/runtime.hpp"), 'inline constexpr char Version[] = "1.2.3";', $utf8)
        [IO.File]::WriteAllText((Join-Path $root "$id/tests.lua"), ('target("' + $id + 'Tests")'), $utf8)
        foreach ($file in @('readme.md', 'notes.md', 'package.ps1', 'package-tests.ps1', 'installer-tests.ps1')) {
            [IO.File]::WriteAllText((Join-Path $root "$id/$file"), 'fixture', $utf8)
        }
        $entries += [ordered]@{ id = $id; name = $id; target = $id; versionFile = "$id/src/runtime.hpp"; validationFile = "$id/tests.lua"; readme = "$id/readme.md"; releaseNotes = "$id/notes.md"; packageScript = "$id/package.ps1"; packageTests = "$id/package-tests.ps1"; installerTests = "$id/installer-tests.ps1"; artifact = "build/release/$id.asi"; format = 'asi-win64' }
    }
    $registryPath = Join-Path $root 'Scripts/release-games.json'
    function Save-Registry { [IO.File]::WriteAllText($registryPath, ([ordered]@{ schemaVersion = 1; games = $entries } | ConvertTo-Json -Depth 5), $utf8) }
    Save-Registry
    $first = & $resolver -SourceRoot $root -Tag FirstGame/v1.2.3
    $second = & $resolver -SourceRoot $root -Game SecondGame
    Check ($first.tag -ceq 'FirstGame/v1.2.3' -and $second.tag -ceq 'SecondGame/v1.2.3') 'games may independently use the same semantic version'
    foreach ($field in @('archiveName', 'checksumName', 'manifestName', 'candidateName', 'outputDirectory', 'artifact')) {
        Check ($first.$field -cne $second.$field) "same-version games have independent $field"
    }
    Check ($first.tests.Count -eq 1 -and $first.tests[0] -ceq 'FirstGameTests' -and $second.tests[0] -ceq 'SecondGameTests') 'validation target inventories stay in their game'
    Reject 'legacy unscoped tag rejected' { & $resolver -SourceRoot $root -Tag v1.2.3 } 'Release tag must be'
    Reject 'cross-game tag rejected' { & $resolver -SourceRoot $root -Game FirstGame -Tag SecondGame/v1.2.3 } 'does not match selected game'
    Reject 'source version mismatch rejected' { & $resolver -SourceRoot $root -Tag FirstGame/v1.2.4 } 'does not match source version'
    Reject 'unknown game rejected' { & $resolver -SourceRoot $root -Tag UnknownGame/v1.2.3 } 'Unknown release game'
    Reject 'case-mismatched game rejected' { & $resolver -SourceRoot $root -Game firstgame } 'Unknown release game'
    Reject 'noncanonical version rejected' { & $resolver -SourceRoot $root -Tag FirstGame/v01.2.3 } 'Release tag must be'
    $entries[1].target = 'FirstGame'; Save-Registry
    Reject 'shared game build target rejected' { & $resolver -SourceRoot $root -Game FirstGame } 'Duplicate build or validation target'
    $entries[1].target = 'SecondGame'
    [IO.File]::WriteAllText((Join-Path $root 'SecondGame/tests.lua'), 'target("FirstGameTests")', $utf8); Save-Registry
    Reject 'shared validation target rejected' { & $resolver -SourceRoot $root -Game FirstGame } 'Duplicate build or validation target'
    [IO.File]::WriteAllText((Join-Path $root 'SecondGame/tests.lua'), 'target("SecondGameTests")', $utf8)
    $entries[1].versionFile = 'FirstGame/src/runtime.hpp'; Save-Registry
    Reject 'shared game version source rejected' { & $resolver -SourceRoot $root -Game SecondGame } 'must belong to its game directory'
    $entries[1].versionFile = 'SecondGame/../outside.hpp'; Save-Registry
    Reject 'registry path traversal rejected' { & $resolver -SourceRoot $root -Game SecondGame } 'Unsafe release source path'
    $entries[1].versionFile = 'SecondGame/src/runtime.hpp'; $entries[1].artifact = 'build/release/FirstGame.asi'; Save-Registry
    Reject 'cross-game binary path rejected' { & $resolver -SourceRoot $root -Game SecondGame } 'Artifact must match'
    $entries[1].artifact = 'build/release/SecondGame.asi'; $entries += $entries[0]; Save-Registry
    Reject 'duplicate registry game rejected' { & $resolver -SourceRoot $root -Game FirstGame } 'Duplicate game IDs'
    $entries = @($entries[0], $entries[1]); Save-Registry
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../xmake.lua') -Destination (Join-Path $root 'xmake.lua')
    [IO.File]::WriteAllText((Join-Path $root 'Scripts/xmake.lua'), '-- fixture', $utf8)
    foreach ($id in @('FirstGame', 'SecondGame', 'UnknownGame')) {
        New-Item -ItemType Directory -Path (Join-Path $root $id) -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $root "$id/xmake.lua"), ('target("' + $id + '"); set_kind("phony")'), $utf8)
    }
    # xmake can persist configuration under the invoking directory even with -P.
    # Enter the fixture so neither configuration nor cache can touch the caller.
    $callerConfig = Join-Path $PWD '.xmake/windows/x64/xmake.conf'
    $callerConfigHash = if (Test-Path -LiteralPath $callerConfig) { (Get-FileHash -LiteralPath $callerConfig).Hash } else { $null }
    Push-Location -LiteralPath $root
    try {
        foreach ($id in @('FirstGame', 'SecondGame')) {
            $configOutput = & xmake f -y -P $root "--game=$id" 2>&1
            $configExit = $LASTEXITCODE
            [IO.File]::WriteAllText((Join-Path $root "$id-config.log"), ($configOutput -join "`n"), $utf8)
            Check ($configExit -eq 0) "xmake configures selected $id"
            $targetOutput = & xmake show -P $root -l targets 2>&1
            $targetExit = $LASTEXITCODE
            $targetText = (($targetOutput -join "`n") -replace '\x1b\[[0-9;]*m', '').Trim()
            [IO.File]::WriteAllText((Join-Path $root "$id-targets.log"), $targetText, $utf8)
            Check ($targetExit -eq 0 -and $targetText -ceq $id) "xmake imports only $id targets"
        }
        $previousPreference = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        try { $unknownOutput = & xmake f -y -P $root --game=UnknownGame 2>&1; $unknownExit = $LASTEXITCODE }
        finally { $ErrorActionPreference = $previousPreference }
        [IO.File]::WriteAllText((Join-Path $root 'unknown-game-config.log'), ($unknownOutput -join "`n"), $utf8)
        Check ($unknownExit -ne 0 -and ($unknownOutput -join "`n").Contains('Unknown registered game')) 'xmake rejects an unregistered game even with a build directory'
    } finally { Pop-Location }
    $callerConfigAfter = if (Test-Path -LiteralPath $callerConfig) { (Get-FileHash -LiteralPath $callerConfig).Hash } else { $null }
    Check ($callerConfigHash -ceq $callerConfigAfter) 'fixture configuration preserves caller xmake settings'
    Write-Host "Release isolation tests passed ($($results.Count) checks). Evidence: $root"
} catch { $failure = $_.Exception.Message; throw }
finally {
    [IO.File]::WriteAllText((Join-Path $root 'results.json'), ([ordered]@{ passed = ($null -eq $failure); powerShell = $PSVersionTable.PSVersion.ToString(); checks = $results.Count; failure = $failure; results = @($results.ToArray()) } | ConvertTo-Json -Depth 6) + "`n", $utf8)
}
