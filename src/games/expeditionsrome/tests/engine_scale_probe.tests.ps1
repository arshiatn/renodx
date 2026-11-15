[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$probe_path = Join-Path $PSScriptRoot 'engine_scale_probe.ps1'
$fixture_root = Join-Path ([IO.Path]::GetTempPath()) ('RenoDX_Rome_EngineScaleProbe_Fixtures_' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $fixture_root
$fixture_state = @{ GameRunning = $false; GuardCalls = 0; MutateAtGuard = 0; MutationPath = $null }
$mock_process = {
    [CmdletBinding()]
    param([string]$Name)
    if ($Name -ne 'ExpeditionsRome-Win64-Shipping') { throw "Unexpected process lookup: $Name" }
    ++$fixture_state.GuardCalls
    if ($fixture_state.GuardCalls -eq $fixture_state.MutateAtGuard) {
        [IO.File]::AppendAllText($fixture_state.MutationPath, "r.ConcurrentUserChange=3`n", [Text.UTF8Encoding]::new($false))
    }
    if ($fixture_state.GameRunning) { [pscustomobject]@{ Id = 1234 } }
}.GetNewClosure()
Set-Item -LiteralPath Function:Get-Process -Value $mock_process

$cases = @(
    @{ Name = 'utf8-crlf'; Encoding = [Text.UTF8Encoding]::new($false); Newline = "`r`n"; HasSection = $false },
    @{ Name = 'utf8-bom-lf'; Encoding = [Text.UTF8Encoding]::new($true); Newline = "`n"; HasSection = $true },
    @{ Name = 'utf16le-crlf'; Encoding = [Text.UnicodeEncoding]::new($false, $true); Newline = "`r`n"; HasSection = $true },
    @{ Name = 'utf16be-lf'; Encoding = [Text.UnicodeEncoding]::new($true, $true); Newline = "`n"; HasSection = $false }
)
$checks = 0
foreach ($case in $cases) {
    $config_dir = Join-Path $fixture_root $case.Name
    $null = New-Item -ItemType Directory -Path $config_dir
    $engine_path = Join-Path $config_dir 'Engine.ini'
    $original_text = '[Core.System]' + $case.Newline + 'Paths=../../../Engine/Content' + $case.Newline + $case.Newline + '[Audio]' + $case.Newline + 'UnfocusedVolumeMultiplier=1.000000' + $case.Newline
    if ($case.HasSection) { $original_text += '[SystemSettings]' + $case.Newline + 'r.ExistingSetting=7' + $case.Newline }
    [byte[]]$original_bytes = $case.Encoding.GetPreamble() + $case.Encoding.GetBytes($original_text)
    [IO.File]::WriteAllBytes($engine_path, $original_bytes)
    [IO.File]::SetAttributes($engine_path, [IO.FileAttributes]::Normal)
    $original_hash = (Get-FileHash -LiteralPath $engine_path -Algorithm SHA256).Hash
    $original_attributes = (Get-Item -LiteralPath $engine_path).Attributes
    $applied = & $probe_path -Action Apply -ConfigDirectory $config_dir
    if ($applied.State -ne 'Applied' -or -not (Test-Path -LiteralPath $applied.BackupDirectory)) { throw 'Apply did not preserve a backup.' }
    $applied_bytes = [IO.File]::ReadAllBytes($engine_path)
    $decoded = $case.Encoding.GetString($applied_bytes)
    if (-not $decoded.Contains('r.ScreenPercentage=33.333333' + $case.Newline)) { throw 'Missing probe or changed newline style.' }
    if (-not $decoded.Contains('UnfocusedVolumeMultiplier=1.000000') -or -not $decoded.Contains('Paths=../../../Engine/Content')) { throw 'Existing settings were lost.' }
    if ($case.HasSection -and -not $decoded.Contains('r.ExistingSetting=7')) { throw 'Existing SystemSettings key was lost.' }
    for ($index = 0; $index -lt $case.Encoding.GetPreamble().Length; ++$index) {
        if ($applied_bytes[$index] -ne $case.Encoding.GetPreamble()[$index]) { throw 'The BOM changed.' }
    }
    $applied_hash = (Get-FileHash -LiteralPath $engine_path -Algorithm SHA256).Hash
    try {
        & $probe_path -Action Apply -ConfigDirectory $config_dir | Out-Null
        throw 'Reapply unexpectedly succeeded.'
    } catch {
        if ($_.Exception.Message -notlike '*override already exists*') { throw }
    }
    if ((Get-FileHash -LiteralPath $engine_path -Algorithm SHA256).Hash -ne $applied_hash) { throw 'Rejected reapply changed the file.' }
    $restored = & $probe_path -Action Restore -BackupDirectory $applied.BackupDirectory
    if ($restored.State -ne 'Restored' -or (Get-FileHash -LiteralPath $engine_path -Algorithm SHA256).Hash -ne $original_hash) { throw 'Restore was not byte-exact.' }
    if ((Get-Item -LiteralPath $engine_path).Attributes -ne $original_attributes) { throw 'Original attributes were not restored.' }
    if (-not (Test-Path -LiteralPath $applied.BackupDirectory)) { throw 'Restore deleted the recovery backup.' }
    ++$checks
}

$config_dir = Join-Path $fixture_root 'conflicts'
$null = New-Item -ItemType Directory -Path $config_dir
$engine_path = Join-Path $config_dir 'Engine.ini'
foreach ($conflict in @("[SystemSettings]`nr.ScreenPercentage=70`nr.ScreenPercentage=80`n", "[SystemSettings]`nr.Other=1`n[SystemSettings]`nr.Other=2`n")) {
    [IO.File]::WriteAllText($engine_path, $conflict, [Text.UTF8Encoding]::new($false))
    $conflict_hash = (Get-FileHash -LiteralPath $engine_path -Algorithm SHA256).Hash
    try {
        & $probe_path -Action Apply -ConfigDirectory $config_dir | Out-Null
        throw 'Ambiguous configuration unexpectedly succeeded.'
    } catch {
        if ($_.Exception.Message -notlike '*override already exists*' -and $_.Exception.Message -notlike '*sections are ambiguous*') { throw }
    }
    if ((Get-FileHash -LiteralPath $engine_path -Algorithm SHA256).Hash -ne $conflict_hash) { throw 'Rejected ambiguity changed the file.' }
    ++$checks
}
[IO.File]::WriteAllText($engine_path, "[Audio]`nUnfocusedVolumeMultiplier=1`n", [Text.UTF8Encoding]::new($false))
$applied = & $probe_path -Action Apply -ConfigDirectory $config_dir
[IO.File]::AppendAllText($engine_path, "r.UserChange=2`n", [Text.UTF8Encoding]::new($false))
$changed_hash = (Get-FileHash -LiteralPath $engine_path -Algorithm SHA256).Hash
try {
    & $probe_path -Action Restore -BackupDirectory $applied.BackupDirectory | Out-Null
    throw 'Restore unexpectedly overwrote a user change.'
} catch {
    if ($_.Exception.Message -notlike '*changed since Apply*') { throw }
}
if ((Get-FileHash -LiteralPath $engine_path -Algorithm SHA256).Hash -ne $changed_hash) { throw 'Rejected restore changed the file.' }
++ $checks
[IO.File]::WriteAllText($engine_path, "[Audio]`nUnfocusedVolumeMultiplier=1`n", [Text.UTF8Encoding]::new($false))
$applied = & $probe_path -Action Apply -ConfigDirectory $config_dir
$fixture_state.GuardCalls = 0
$fixture_state.MutateAtGuard = 2
$fixture_state.MutationPath = $engine_path
try {
    & $probe_path -Action Restore -BackupDirectory $applied.BackupDirectory | Out-Null
    throw 'Restore unexpectedly overwrote an edit made during verification.'
} catch {
    if ($_.Exception.Message -notlike '*changed during restore verification*') { throw }
} finally {
    $fixture_state.MutateAtGuard = 0
    $fixture_state.MutationPath = $null
}
if (-not ([IO.File]::ReadAllText($engine_path)).Contains('r.ConcurrentUserChange=3')) { throw 'The intervening user edit was lost.' }
$changed_hash = (Get-FileHash -LiteralPath $engine_path -Algorithm SHA256).Hash
++ $checks
$fixture_state.GameRunning = $true
foreach ($action in @('Apply', 'Restore')) {
    try {
        if ($action -eq 'Apply') { & $probe_path -Action Apply -ConfigDirectory $config_dir | Out-Null }
        else { & $probe_path -Action Restore -BackupDirectory $applied.BackupDirectory | Out-Null }
        throw 'The game-running guard unexpectedly accepted a write.'
    } catch {
        if ($_.Exception.Message -notlike '*Close Expeditions: Rome completely*') { throw }
    }
    if ((Get-FileHash -LiteralPath $engine_path -Algorithm SHA256).Hash -ne $changed_hash) { throw 'The game-running guard changed the file.' }
    ++$checks
}
[pscustomobject]@{ State = 'PASS'; Checks = $checks; FixtureRoot = $fixture_root; ActualGameConfigTouched = $false }
