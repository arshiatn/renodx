# F7: the 64-bit Attila loader's present path (tests.cpp as attila64.exe) under ReShade (dxgi.dll) with the add-on and
# zz_capture.addon64 (capture.cpp), one folder per case under testbin\attila64_present\run, on the hardware device
# (only inside a GPU window: testbin\attila64_present\f7_gpu.sh; -Gpu is accepted and is the default). -Warp stops:
# ReShade leaves Microsoft Basic Render Driver devices alone, so no add-on would load. Each case: the exe's own result
# (image-freed asserts, captures against the swap chain proxy's reference), then ReShade.log (load order, the add-on's
# lines, the output color space per runtime, unloads without warnings). -Addon: the add-on under test (default: this
# tree's build\Release). -Cases: names from the table below (default: all but C2_scrgb_1320, whose driver crash can
# leave an exe that does not exit). The C2 control (atn-mods before the bring-up) sits in testbin\attila64_present\c2,
# the step-4 build (A2a without A2b, C5's control) in testbin\attila64_present\step4. A case folder that cannot be
# emptied (a process still holds it) is replaced by <name>.2, .3, ... -BuildOnly: the exe and zz_capture, no case;
# -NoBuild: the cases with the binaries built before. -Stage: another staging folder for bin\ and run\ (an agent's own
# test folder; the controls stay in the default one).
param([string]$Addon = '', [string[]]$Cases = @(), [switch]$Gpu, [switch]$Warp, [switch]$NoBuild, [switch]$BuildOnly, [string]$Stage = '')
$ErrorActionPreference = 'Stop'
if ($Warp) { throw 'F7 on WARP loads no add-on (ReShade skips Microsoft Basic Render Driver devices): run it on the GPU (f7_gpu.sh)' }
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$testRoot = Join-Path $repoRoot 'test\tw_attila64_present'
$tools = Join-Path $env:USERPROFILE '.claude\projects\D--Dev-renodx\tools\tw'
$controls = Join-Path $tools 'testbin\attila64_present'
$stage = if ($Stage -eq '') { $controls } else { $Stage }
$binDir = Join-Path $stage 'bin'
$reshade = Join-Path $tools 'testbin\reshade_suites\reshade680_amd64\ReShade64.dll'
if ($Addon -eq '') { $Addon = Join-Path $repoRoot 'build\Release\renodx-twattila.addon64' }
$addons = @{ current = $Addon; c2 = (Join-Path $controls 'c2\renodx-twattila.addon64'); step4 = (Join-Path $controls 'step4\renodx-twattila.addon64'); none = '' }

if (-not $NoBuild) {
  $vsRoot = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -products '*' -latest -property installationPath
  & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
  New-Item -ItemType Directory -Force -Path $binDir | Out-Null
  Push-Location -LiteralPath $binDir
  & rc.exe /nologo /fo attila64.res "$testRoot\attila64.rc"
  if ($LASTEXITCODE -ne 0) { throw 'attila64.rc failed' }
  $clang = 'C:\Program Files\LLVM\bin\clang-cl.exe'
  & $clang --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I $repoRoot "$testRoot\tests.cpp" /Fe:attila64.exe /link attila64.res d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib
  if ($LASTEXITCODE -ne 0) { throw 'attila64.exe compile failed' }
  & $clang --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /LD /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I $repoRoot /I "$repoRoot\external\reshade" "$testRoot\capture.cpp" /Fe:zz_capture.addon64 /link
  if ($LASTEXITCODE -ne 0) { throw 'zz_capture.addon64 compile failed' }
  & $clang --target=x86_64-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /MT /LD /DNOMINMAX /DWIN32_LEAN_AND_MEAN `
    /I $repoRoot "$testRoot\fake_plugin.cpp" /Fe:renodx_attila64.dll /link
  if ($LASTEXITCODE -ne 0) { throw 'renodx_attila64.dll (fake plugin) compile failed' }
  Pop-Location
}
if ($BuildOnly) { 'built'; return }

# Lines every add-on run has; the forbidden ones never.
$identity = '[Total War] 64-bit Attila: attila64.exe, loader 1.3.20.0'
$mapped = '[Total War] 64-bit Attila: game code mapped, build id a64b000102030405060708090a0b0c0d0e0f1011, 1 code ranges'
$upgraded = @("the game's picture upgraded (1280x720)", "the game's picture upgraded (1600x900)")
$notReadable = '[Total War] 64-bit Attila: game code not readable at the first present'
$noPlugin = '[Total War] 64-bit Attila: engine plugin not installed, rows offered 0x0'
$forbidden = @('has no upgrade', 'leaves it alone', 'not readable at the first present', 'was not unregistered',
               'did not leave the add-on within 1 s', 'callback failed', 'another add-on holds', 'Engine Performance Fix loaded')
# The color space every effect runtime reports, per output.
$colorSpaces = @{ 0 = 'hdr10_st2084'; 1 = 'extended_srgb_linear'; 2 = 'srgb_nonlinear' }
$encodings = @{ hdr10 = 0; scrgb = 1; sdr = 2 }
$outputNames = @{ 0 = 'Output: HDR10'; 1 = 'Output: scRGB'; 2 = 'Output: SDR' }
$table = [ordered]@{}
foreach ($encoding in 'hdr10', 'scrgb', 'sdr') {
  foreach ($form in '1320', 'fixed') {
    $overlay = if ($form -eq '1320') { "the loader's 8-bit overlay converted (buffer)" } else { "the loader's 8-bit overlay converted (picture)" }
    $table["${encoding}_$form"] = @{ addon = 'current'; args = "d3d11 $form upgraded"; encoding = $encodings[$encoding]
                                     lines = @($identity, $mapped, $noPlugin) + $upgraded + @($overlay) }
  }
}
# The engine plugin's block (fake_plugin.cpp, RENODX_F7_PLUGIN = "<status> <rows_applied> <rows_changed> [<version>
# [<rows_tried>]]"): found at the game's init_device, its status logged once, frames as without it, no callbacks left in
# the block at the end. plugin_v1: an older plugin's 40-byte block; plugin_cpu_refused: the CPU row tried, not offered.
$pluginCases = [ordered]@{
  plugin_ready = @('1 3 0', 'engine plugin ready, rows offered 0x3', 'engine plugin rows tried 0x3')
  plugin_changed = @('1 3 2', 'engine plugin ready, rows offered 0x1', 'engine plugin rows tried 0x3')
  plugin_v1 = @('1 3 0 1', 'engine plugin ready (block v1: an older plugin, its first rows only), rows offered 0x3')
  plugin_cpu_refused = @('1 3 0 2 7', 'engine plugin ready, rows offered 0x3', 'engine plugin rows tried 0x7')
  plugin_off = @('2 0 0', 'engine plugin off (ReShade.ini), rows offered 0x0')
  plugin_wrong_game = @('3 0 0', 'engine plugin off (another game build), rows offered 0x0')
  plugin_wrong_loader = @('4 0 0', 'engine plugin off (another loader version), rows offered 0x0')
  plugin_loading = @('0 3 0', 'engine plugin not started by the loader (another loader version), rows offered 0x0')
  plugin_unknown = @('9 3 0', 'engine plugin of another version, rows offered 0x0')
}
foreach ($name in $pluginCases.Keys) {
  $table[$name] = @{ addon = 'current'; args = 'd3d11 fixed upgraded plugin'; encoding = 0
                     plugin = $pluginCases[$name][0]
                     lines = @($identity, $mapped) + @($pluginCases[$name] | Select-Object -Skip 1 | ForEach-Object { "[Total War] 64-bit Attila: $_" }) + $upgraded
                     forbid = @('engine plugin not installed', 'callbacks registered') }
}
# Registration end to end: rows forced wanted (RENODX_TW_PLUGIN_WANT), the callbacks registered at a present, the fake
# hooks' thread calling into them, then unregistered and drained inside ReShade's unload before the add-on is freed.
$table['plugin_calls'] = @{
  addon = 'current'; args = 'd3d11 fixed upgraded plugin plugin_calls'; encoding = 0; plugin = '1 3 0'; want = '3'
  lines = @($identity, $mapped, '[Total War] 64-bit Attila: engine plugin ready, rows offered 0x3',
            '[Total War] 64-bit Attila: rows 0x3 wanted from RENODX_TW_PLUGIN_WANT (tests only)',
            '[Total War] 64-bit Attila: engine plugin callbacks registered (rows wanted 0x3)') + $upgraded
  forbid = @('engine plugin not installed')
}
# The plugin's file in the loader's plugins\ but not loaded: by itself (the loader's log says why), or with the loader's
# plugins switched off (ATTILA_NO_PLUGINS=1, or ATTILA_PLAIN set to nothing by the exe).
$notLoaded = '[Total War] 64-bit Attila: engine plugin in plugins\ but not loaded (attila64_log.txt says why), rows offered 0x0'
$switchedOff = '[Total War] 64-bit Attila: engine plugin not loaded: plugins switched off (ATTILA_NO_PLUGINS / ATTILA_PLAIN), rows offered 0x0'
$table['plugin_not_loaded'] = @{ addon = 'current'; args = 'd3d11 fixed upgraded'; encoding = 0; plugins_dir = $true
                                 lines = @($identity, $mapped, $notLoaded) + $upgraded; forbid = @('engine plugin not installed', 'switched off') }
$table['plugin_no_plugins'] = @{ addon = 'current'; args = 'd3d11 fixed upgraded'; encoding = 0; plugins_dir = $true; no_plugins = '1'
                                 lines = @($identity, $mapped, $switchedOff) + $upgraded; forbid = @('engine plugin not installed', 'but not loaded') }
$table['plugin_plain_empty'] = @{ addon = 'current'; args = 'd3d11 fixed upgraded plain_empty'; encoding = 0; plugins_dir = $true
                                  lines = @($identity, $mapped, $switchedOff) + $upgraded; forbid = @('engine plugin not installed', 'but not loaded') }
# Engine Performance Fix's start notes: perffix64.dll (a copy of the fake plugin) loaded, and its files as the add-on reads
# them at the first present: <exe dir>\..\FrameBridge (run\FrameBridge: made for the case, removed before every case)
# with helper64.exe (`bridge`) and its settings.txt lines (`settings`; none: no file), perffix64.log's lines, and
# flagwave_off.txt (`flags_off`). One line names both.
$flagsOn = '01:37:48.419 flag wave: on (a pack carries flagwave\flag_wave.txt = 1; log: flagwave.log)'
$perffixCases = [ordered]@{
  perffix_on = @{ bridge = $true; settings = @('addon=on'); log = @($flagsOn); result = 'on, waving flags on' }
  perffix_off = @{ bridge = $true; settings = @('frame_generation=on', 'ADDON=OFF'); log = @($flagsOn); flags_off = $true
                   result = 'off, waving flags off' }
  perffix_none = @{ bridge = $false; log = @('01:37:48.419 flag wave not installed: no loaded pack carries flagwave\flag_wave.txt (a flag pack does)')
                    result = 'off, waving flags off' }
  perffix_no_settings = @{ bridge = $true; log = @('01:37:48.419 flag wave: off in heapfix.ini'); result = 'on, waving flags off' }
}
foreach ($name in $perffixCases.Keys) {
  $table[$name] = $perffixCases[$name] + @{
    addon = 'current'; args = 'd3d11 fixed upgraded perffix'; encoding = 0; perffix = $true
    lines = @($identity, $mapped, $noPlugin, "[Total War] 64-bit Attila: Engine Performance Fix loaded: FrameBridge upscaling $($perffixCases[$name].result)") + $upgraded
    allow = @('Engine Performance Fix loaded') }
}
$table['C1_1320'] = @{ addon = 'none'; args = 'd3d11 1320 plain'; encoding = 0; lines = @() }
$table['C1_fixed'] = @{ addon = 'none'; args = 'd3d11 fixed plain'; encoding = 0; lines = @() }
$table['C2_hdr10_1320'] = @{ addon = 'c2'; args = 'd3d11 1320 broken'; encoding = 0; lines = @() }
$table['C2_scrgb_1320'] = @{ addon = 'c2'; args = 'd3d11 1320 broken'; encoding = 1; lines = @() }
$table['C2_hdr10_fixed'] = @{ addon = 'c2'; args = 'd3d11 fixed broken'; encoding = 0; lines = @() }
$table['C2_scrgb_fixed'] = @{ addon = 'c2'; args = 'd3d11 fixed broken'; encoding = 1; lines = @() }
$table['C3_scrgb_pitch8'] = @{ addon = 'current'; args = 'd3d11 1320 upgraded pitch8'; encoding = 1; lines = @($identity) + $upgraded; forbid = @("8-bit overlay converted") }
$table['C4_d3d12_plain'] = @{ addon = 'none'; args = 'd3d12'; encoding = 0; lines = @() }
$table['C4_d3d12'] = @{ addon = 'current'; args = 'd3d12'; encoding = 0; lines = @($identity, '[Total War] 64-bit Attila: a d3d12 device: the add-on leaves it alone'); compare = 'C4_d3d12_plain'; allow = @('leaves it alone') }
$table['C5_hdr10'] = @{ addon = 'current'; args = 'd3d11 fixed upgraded'; encoding = 0; lines = @("the loader's 8-bit overlay converted (picture)") }
$table['C5_scrgb'] = @{ addon = 'current'; args = 'd3d11 fixed upgraded'; encoding = 1; lines = @("the loader's 8-bit overlay converted (picture)") }
$table['C5_control_hdr10'] = @{ addon = 'step4'; args = 'd3d11 fixed no_overlay'; encoding = 0; lines = @() }
$table['C5_control_scrgb'] = @{ addon = 'step4'; args = 'd3d11 fixed no_overlay'; encoding = 1; lines = @() }
$table['C6_opengl_plain'] = @{ addon = 'none'; args = 'opengl'; encoding = 0; lines = @() }
$table['C6_opengl'] = @{ addon = 'current'; args = 'opengl'; encoding = 0; lines = @(); compare = 'C6_opengl_plain'; allow = @('leaves it alone') }
# The game's image at 0x400000 unusable (left reserved, a wrong magic, a header table past the page): the warning, no
# access violation inside the range (the exe counts them), frames as with the image.
foreach ($image in 'image_none', 'image_badmagic', 'image_table') {
  $table[$image] = @{ addon = 'current'; args = "d3d11 fixed upgraded $image"; encoding = 0; lines = @($identity, $notReadable) + $upgraded; allow = @('not readable at the first present'); forbid = @('game code mapped') }
}
$Cases = @($Cases | ForEach-Object { $_ -split "," } | Where-Object { $_ })
if ($Cases.Count -eq 0) { $Cases = @($table.Keys | Where-Object { $_ -ne 'C2_scrgb_1320' }) }

$failed = @()
$passed = @()
$dirs = @{}
foreach ($name in $Cases) {
  $case = $table[$name]
  if ($null -eq $case) { throw "no case $name" }
  $dir = Join-Path $stage "run\$name"
  for ($try = 2; (Test-Path -LiteralPath $dir); ++$try) {
    try { Get-ChildItem -LiteralPath $dir -File | Remove-Item -Force; break }
    catch { "  ${name}: $dir is held by a process: run\$name.$try"; $dir = Join-Path $stage "run\$name.$try" }
  }
  $dirs[$name] = $dir
  New-Item -ItemType Directory -Force -Path $dir | Out-Null
  Copy-Item -LiteralPath (Join-Path $binDir 'attila64.exe') -Destination $dir
  Copy-Item -LiteralPath (Join-Path $binDir 'zz_capture.addon64') -Destination $dir
  Copy-Item -LiteralPath $reshade -Destination (Join-Path $dir 'dxgi.dll')
  $addonPath = $addons[$case.addon]
  if ($addonPath -ne '') { Copy-Item -LiteralPath $addonPath -Destination (Join-Path $dir 'renodx-twattila.addon64') }
  if ($case.plugin) { Copy-Item -LiteralPath (Join-Path $binDir 'renodx_attila64.dll') -Destination $dir }
  $bridgeDir = Join-Path $stage 'run\FrameBridge'
  if (Test-Path -LiteralPath $bridgeDir) { Remove-Item -LiteralPath $bridgeDir -Recurse -Force }
  if ($case.perffix) {
    Copy-Item -LiteralPath (Join-Path $binDir 'renodx_attila64.dll') -Destination (Join-Path $dir 'perffix64.dll')
    $case.log | Set-Content -LiteralPath (Join-Path $dir 'perffix64.log') -Encoding ascii
    if ($case.flags_off) { 'Made by 64-bit Attila' | Set-Content -LiteralPath (Join-Path $dir 'flagwave_off.txt') -Encoding ascii }
    if ($case.bridge) {
      New-Item -ItemType Directory -Force -Path $bridgeDir | Out-Null
      '' | Set-Content -LiteralPath (Join-Path $bridgeDir 'helper64.exe') -Encoding ascii
      if ($case.settings) { $case.settings | Set-Content -LiteralPath (Join-Path $bridgeDir 'settings.txt') -Encoding ascii }
    }
  }
  if ($case.plugins_dir) {
    New-Item -ItemType Directory -Force -Path (Join-Path $dir 'plugins') | Out-Null
    Copy-Item -LiteralPath (Join-Path $binDir 'renodx_attila64.dll') -Destination (Join-Path $dir 'plugins')
  }
  $env:RENODX_F7_PLUGIN = $case.plugin
  $env:RENODX_TW_PLUGIN_WANT = $case.want
  $env:ATTILA_NO_PLUGINS = $case.no_plugins
  # A fresh ini: no LoadFromDllMain key, the tutorial done (no splash), the add-on's output rows explicit.
  @(
    '[ADDON]', 'DisabledAddons=', '[GENERAL]', 'EffectSearchPaths=', 'TextureSearchPaths=', 'PresetPath=.\ReShadePreset.ini',
    '[OVERLAY]', 'TutorialProgress=4', '[renodx]', "SwapChainEncoding=$($case.encoding)",
    '[renodx-preset1]', 'tonemapper=3', 'peak_white_nits=1000', 'diffuse_white_nits=203', 'graphics_white_nits=203', 'gamma_correction=1'
  ) | Set-Content -LiteralPath (Join-Path $dir 'ReShade.ini') -Encoding ascii
  $env:RENODX_CAPTURE_EVERY = '1'
  $exeArgs = $case.args + ' gpu'
  $process = Start-Process -FilePath (Join-Path $dir 'attila64.exe') -ArgumentList $exeArgs -WorkingDirectory $dir -PassThru -NoNewWindow `
    -RedirectStandardOutput (Join-Path $dir 'stdout.txt')
  $null = $process.Handle  # keeps the exit code readable after the exit
  if (-not $process.WaitForExit(240000)) { $process.Kill(); "FAIL: ${name}: timed out"; $failed += $name; continue }
  $process.WaitForExit()
  $problems = @()
  $log = if (Test-Path -LiteralPath (Join-Path $dir 'ReShade.log')) { Get-Content -LiteralPath (Join-Path $dir 'ReShade.log') -Raw } else { '' }
  # The control add-on may crash in the driver (scRGB, the overlay into the buffer): the loader's overlay writes 4-byte
  # rows into the RGBA16F buffer, and the driver reads them as 8-byte pixels, past the rows. Only that site counts as the
  # control's bug, in C2_scrgb_1320 only; any other crash fails.
  $controlCrash = $name -eq 'C2_scrgb_1320' -and $process.ExitCode -eq -1073741819 -and $log.Contains('Unhandled exception 0xC0000005 at nvwgf2umx.dll')
  if ($controlCrash) { "  ${name}: the control crashed in the driver's overlay copy (access violation): broken" }
  elseif ($process.ExitCode -ne 0) { $problems += "exit $($process.ExitCode)" }
  # zz_capture's present callback must run after RenoDX's: it loaded after it (registration order = load order).
  $renodxAt = $log.LastIndexOf("renodx-twattila.addon64' ...")
  $captureAt = $log.LastIndexOf("zz_capture.addon64' ...")
  if ($captureAt -lt 0) { $problems += 'zz_capture not loaded' }
  if ($case.addon -ne 'none' -and ($renodxAt -lt 0 -or $renodxAt -gt $captureAt)) { $problems += 'zz_capture did not load after RenoDX' }
  if ($case.args.StartsWith('d3d11') -and $case.addon -ne 'none' -and $case.addon -ne 'c2' -and -not $log.Contains($outputNames[$case.encoding])) {
    $problems += "no '$($outputNames[$case.encoding])' line (Windows HDR off?)"
  }
  foreach ($line in $case.lines) { if (-not $log.Contains($line)) { $problems += "ReShade.log lacks '$line'" } }
  if ($case.args.StartsWith('d3d11') -and $case.addon -ne 'none' -and -not $controlCrash) {
    $spaces = @([regex]::Matches($log, 'OnInitEffectRuntime\(Runtime: \d+, colorspace: (\w+)\)') | ForEach-Object { $_.Groups[1].Value })
    $wrong = @($spaces | Where-Object { $_ -ne $colorSpaces[$case.encoding] })
    if ($spaces.Count -eq 0) { $problems += 'no colorspace line' }
    elseif ($wrong.Count -ne 0) { $problems += "colorspace $($wrong -join ', ') (expected $($colorSpaces[$case.encoding]))" }
    if ($log.Contains('Failed to set DirectX color space')) { $problems += "ReShade.log has 'Failed to set DirectX color space'" }
  }
  $forbid = @($forbidden | Where-Object { $case.allow -notcontains $_ }) + @($case.forbid | Where-Object { $_ })
  if ($case.addon -ne 'c2') { foreach ($line in $forbid) { if ($log.Contains($line)) { $problems += "ReShade.log has '$line'" } } }
  elseif ($log.Contains('was not unregistered')) { $problems += "ReShade.log has 'was not unregistered'" }
  if ($case.compare) {
    $other = if ($dirs.ContainsKey($case.compare)) { $dirs[$case.compare] } else { Join-Path $stage "run\$($case.compare)" }
    $mine = @(Get-ChildItem -LiteralPath $dir -Filter 'capture_*.raw' | Sort-Object Name)
    $theirCount = @(Get-ChildItem -LiteralPath $other -Filter 'capture_*.raw' -ErrorAction SilentlyContinue).Count
    # OpenGL: ReShade loaded as dxgi.dll may not hook GL at all (no captures with or without the add-on): recorded, and
    # the run must still end cleanly.
    if ($mine.Count -eq 0 -and $theirCount -eq 0 -and $case.args -eq 'opengl') { "  ${name}: ReShade (as dxgi.dll) did not hook OpenGL: no frames to compare" }
    elseif ($mine.Count -eq 0) { $problems += 'no captures' }
    foreach ($file in $mine) {
      $theirs = Join-Path $other $file.Name
      if (-not (Test-Path -LiteralPath $theirs)) { $problems += "$($file.Name) missing in $($case.compare) (run it first)"; continue }
      if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $theirs).Hash) { $problems += "$($file.Name) differs from $($case.compare)" }
    }
  }
  $summary = Get-Content -LiteralPath (Join-Path $dir 'stdout.txt')
  if ($problems.Count -eq 0) { "PASS: $name"; $passed += $name } else { "FAIL: ${name}: $($problems -join '; ')"; $failed += $name }
  $summary | Where-Object { $_ -match 'FAIL|present (2|5|7|10) |freed' } | ForEach-Object { "  $_" }
}
"F7: $($passed.Count) passed, $($failed.Count) failed of $($Cases.Count)"
if ($failed.Count -ne 0) { throw "F7 failed: $($failed -join ', ')" }
'PASS: F7'
