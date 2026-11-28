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
$upgraded = @("the game's picture upgraded (3840x2160)", "the game's picture upgraded (2560x1440)")
$notReadable = '[Total War] 64-bit Attila: game code not readable at the first present'
$noPlugin = '[Total War] 64-bit Attila: engine plugin not installed, rows offered 0x0'
$notLoaded = '[Total War] 64-bit Attila: engine plugin in plugins\ but not loaded (attila64_log.txt says why), rows offered 0x0'
$switchedOff = '[Total War] 64-bit Attila: engine plugin not loaded: plugins switched off (ATTILA_NO_PLUGINS / ATTILA_PLAIN), rows offered 0x0'
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
                     forbid = @('engine plugin not installed', 'callbacks registered', 'engine scales active', 'engine plugin notices') }
}
# The scale rows (a v2 block offering Unit Detail, Model Detail and Shadow Map Size, rows 0x3b, with the rest-pack
# notice raised) and the case's settings in [renodx-preset1], asymmetric so a wrong index changes the mask: Unit Detail
# Cavalry and Artillery (1, 3), Unit Animation (8-11), Facial (13), Footstep (15) and Model Detail (17) at 2x give bits
# 0x2af0a (one line; Foot Soldiers, Elephants, the fade widths 4-7, Variety 12, Hand 14 and the unit shadows 16 stay
# neutral), Shadow Map Size 4096 is written for the next start, the notice logged once.
$table['plugin_scales'] = @{
  addon = 'current'; args = 'd3d11 fixed upgraded plugin'; encoding = 0; plugin = '1 59 0 2 59 1'
  ini = @('GraphicsUnitDetailMedium=2', 'GraphicsUnitDetailHuge=2', 'GraphicsUnitAnimation=2', 'GraphicsFacialAnimation=2',
          'GraphicsFootstepEffects=2', 'GraphicsObjectDetail=2', 'GraphicsShadowMapSize=1')
  lines = @($identity, $mapped, '[Total War] 64-bit Attila: engine plugin ready, rows offered 0x3b',
            '[Total War] 64-bit Attila: engine plugin rows tried 0x3b',
            '[Total War] 64-bit Attila: engine scales active 0x2af0a',
            '[Total War] 64-bit Attila: engine value ShadowMapSize: 0x1 at the next start (this start: 0x0)',
            '[Total War] 64-bit Attila: engine plugin notices 0x1 (rest pack: yes, model table full: no)') + $upgraded
  forbid = @('engine plugin not installed', 'callbacks registered')
}
# Two Shadow Cascades Under Rays (S5e; a v2 block offering the shadow map row, rows 0x3b): its key in
# [Attila64Plugin.Values] for the next start. plugin_cascades_on: the row on and the rays' shadows on (Ray-Traced Sun
# Shadows with DLAA): 1 written. plugin_cascades_rays_off (control): the row on, the rays' shadows off and a key 1 left
# by an earlier start: 0 written at once. plugin_cascades_not_offered: the shadow map row tried, not offered (rows
# 0x1b), the row and the rays' shadows on, a key 1 left: 0 written.
$cascadesValue = '[Total War] 64-bit Attila: engine value ShadowCascades'
$raysShadowsOn = @('RayTracedShadowCascades=1', 'RayTracedSunShadows=1', 'AntiAliasing=5')
$cascadesCases = [ordered]@{
  plugin_cascades_on = @{ plugin = '1 59 0 2 59'; ini = $raysShadowsOn; offered = '0x3b'; value = "${cascadesValue}: 0x1 at the next start" }
  plugin_cascades_rays_off = @{ plugin = '1 59 0 2 59'; ini = @('RayTracedShadowCascades=1', '[Attila64Plugin.Values]', 'ShadowCascades=1')
                                offered = '0x3b'; value = "${cascadesValue}: 0x0 at the next start" }
  plugin_cascades_not_offered = @{ plugin = '1 27 0 2 59'; ini = $raysShadowsOn + @('[Attila64Plugin.Values]', 'ShadowCascades=1')
                                   offered = '0x1b'; value = "${cascadesValue}: 0x0 at the next start" }
}
foreach ($name in $cascadesCases.Keys) {
  $cascades = $cascadesCases[$name]
  $table[$name] = @{
    addon = 'current'; args = 'd3d11 fixed upgraded plugin'; encoding = 0; plugin = $cascades.plugin; ini = $cascades.ini
    lines = @($identity, $mapped, "[Total War] 64-bit Attila: engine plugin ready, rows offered $($cascades.offered)",
              '[Total War] 64-bit Attila: engine plugin rows tried 0x3b', $cascades.value) + $upgraded
    forbid = @('engine plugin not installed', "${cascadesValue}: 0x$(if ($cascades.value -match '0x1 at') { '0' } else { '1' }) at")
  }
}
# Batched Unit Capture on the port (S5a, rt_port_64 R1.1 gate 1 + R2.6 RS1): the add-on's per-change line
# "unit flush offered {0|1}, registered {0|1}, on the draw thread {0|1}" over the truth table. A v2 block offering
# BONE_FLUSH or not (rows 0x3 / 0x2), the rays traced (Ray-Traced Sun Shadows with DLSS: the batch wanted needs them),
# Batched Unit Capture's row on or off, object motion's own bit (RENODX_TW_PLUGIN_WANT=1) or not, and the plugin's
# before_pack on the presenting thread (RENODX_F7_PACK=present: the port's draw thread) or on its own thread. Expected:
# offered = READY && BONE_FLUSH offered; registered = offered && (row on || object motion's bit); on the draw thread =
# registered && the pack on the presenting thread (before_pack runs only while a row is registered). The first line
# before a registration reads "registered 0, on the draw thread 0"; every other combination is forbidden. The control
# (unit_flush_o1_r0_m1_other): object motion's 0x1 registered, the pack on its own thread: never "on the draw thread 1".
$unitFlush = '[Total War] 64-bit Attila: unit flush offered'
$callbacksWanted = '[Total War] 64-bit Attila: engine plugin callbacks registered (rows wanted'
$unitFlushRaysOn = @('RayTracedSunShadows=1', 'AntiAliasing=5')
foreach ($offered in 0, 1) {
  foreach ($row in 0, 1) {
    foreach ($motion in 0, 1) {
      foreach ($pack in 'draw', 'other') {
        $registered = [int]($offered -eq 1 -and ($row -eq 1 -or $motion -eq 1))
        $here = [int]($registered -eq 1 -and $pack -eq 'draw')
        $rows = if ($offered -eq 1) { 3 } else { 2 }
        $others = @(foreach ($r in 0, 1) { foreach ($d in 0, 1) {
          if (($r -ne $registered -or $d -ne $here) -and -not ($registered -eq 1 -and $r -eq 0 -and $d -eq 0)) {
            "$unitFlush $offered, registered $r, on the draw thread $d"
          } } })
        $table["unit_flush_o${offered}_r${row}_m${motion}_$pack"] = @{
          addon = 'current'; args = 'd3d11 fixed upgraded plugin'; encoding = 0; plugin = "1 $rows 0 2 $rows"
          want = $(if ($motion -eq 1) { '1' } else { '' }); pack = $(if ($pack -eq 'draw') { 'present' } else { '' })
          ini = $unitFlushRaysOn + @("RayTracedBatchedUnits=$row")
          lines = @($identity, $mapped, "[Total War] 64-bit Attila: engine plugin ready, rows offered 0x$rows",
                    "$unitFlush $offered, registered $registered, on the draw thread $here") +
                  @(if ($registered -eq 1) { "$callbacksWanted 0x1)" }) + $upgraded
          forbid = @('engine plugin not installed', "$unitFlush $(1 - $offered)") + $others +
                   @(if ($registered -eq 0) { 'callbacks registered' })
        }
      }
    }
  }
}
# The same with no plugin READY (hidden, nothing wanted, no flush: the line is never logged): not installed, in
# plugins\ but not loaded, plugins switched off. A v1 block (an older plugin, rows 0x3): BONE_FLUSH is its row 0, so the
# flush is offered and registered; it runs here with the pack on the presenting thread (unit_flush_v1_draw) and not
# with the pack on its own thread (unit_flush_v1).
$unitFlushHidden = [ordered]@{
  unit_flush_not_installed = @{ lines = @($noPlugin) }
  unit_flush_not_loaded = @{ plugins_dir = $true; lines = @($notLoaded) }
  unit_flush_plugins_off = @{ plugins_dir = $true; no_plugins = '1'; lines = @($switchedOff) }
}
foreach ($name in $unitFlushHidden.Keys) {
  $hidden = $unitFlushHidden[$name]
  $table[$name] = @{ addon = 'current'; args = 'd3d11 fixed upgraded'; encoding = 0; plugins_dir = $hidden.plugins_dir
                     no_plugins = $hidden.no_plugins; ini = $unitFlushRaysOn + @('RayTracedBatchedUnits=1')
                     lines = @($identity, $mapped) + $hidden.lines + $upgraded
                     forbid = @($unitFlush, 'callbacks registered') }
}
foreach ($pack in 'other', 'draw') {
  $here = [int]($pack -eq 'draw')
  $table["unit_flush_v1$(if ($pack -eq 'draw') { '_draw' })"] = @{
    addon = 'current'; args = 'd3d11 fixed upgraded plugin'; encoding = 0; plugin = '1 3 0 1'
    pack = $(if ($pack -eq 'draw') { 'present' } else { '' }); ini = $unitFlushRaysOn + @('RayTracedBatchedUnits=1')
    lines = @($identity, $mapped,
              '[Total War] 64-bit Attila: engine plugin ready (block v1: an older plugin, its first rows only), rows offered 0x3',
              "$unitFlush 1, registered 1, on the draw thread $here", "$callbacksWanted 0x1)") + $upgraded
    forbid = @('engine plugin not installed', "$unitFlush 0", "$unitFlush 1, registered 0, on the draw thread 1",
               "$unitFlush 1, registered 1, on the draw thread $(1 - $here)")
  }
}
# The grass and campaign tree rows (a v2 block offering the grass, grass cell and campaign tree rows, rows 0x1c3, no
# engine values): Grass Detail Distance 2x and Grass Density 1.5x set the grass scales' bits 19-21 (one line); Campaign
# Tree Detail 2x sets no bit while its engine value is not found (its row hidden). The exe checks the kept scales 18-21
# (1, 4, 2, 1.5). plugin_grass_tree: the fake plugin also publishes Campaign Tree Detail's value (rigid_campaign_tree_lod):
# 18 = 4, bits 0x3c0000, the value written. plugin_tree_refused: the same value found but the campaign tree row not
# offered (rows 0xc3, tried 0x1c3): 18 neutral, the value left at the game's.
$grassIni = @('GraphicsGrassDistance=2', 'GraphicsGrassDensity=1.5', 'GraphicsCampaignDetail=2')
$table['plugin_grass'] = @{
  addon = 'current'; args = 'd3d11 fixed upgraded plugin grass_scales'; encoding = 0; plugin = '1 451 0 2 451'
  ini = $grassIni
  lines = @($identity, $mapped, '[Total War] 64-bit Attila: engine plugin ready, rows offered 0x1c3',
            '[Total War] 64-bit Attila: engine plugin rows tried 0x1c3',
            '[Total War] 64-bit Attila: engine scales active 0x380000') + $upgraded
  forbid = @('engine plugin not installed', 'callbacks registered', 'engine scales active 0x3c0000', 'engine plugin notices')
}
$table['plugin_grass_tree'] = @{
  addon = 'current'; args = 'd3d11 fixed upgraded plugin grass_scales_tree tree_value_written'; encoding = 0
  plugin = '1 451 0 2 451 0 1'; ini = $grassIni
  lines = @($identity, $mapped, '[Total War] 64-bit Attila: engine plugin ready, rows offered 0x1c3',
            '[Total War] 64-bit Attila: engine plugin rows tried 0x1c3',
            '[Total War] 64-bit Attila: engine scales active 0x3c0000') + $upgraded
  forbid = @('engine plugin not installed', 'callbacks registered', 'engine scales active 0x380000', 'engine plugin notices')
}
$table['plugin_tree_refused'] = @{
  addon = 'current'; args = 'd3d11 fixed upgraded plugin grass_scales tree_value_kept'; encoding = 0
  plugin = '1 195 0 2 451 0 1'; ini = $grassIni
  lines = @($identity, $mapped, '[Total War] 64-bit Attila: engine plugin ready, rows offered 0xc3',
            '[Total War] 64-bit Attila: engine plugin rows tried 0x1c3',
            '[Total War] 64-bit Attila: engine scales active 0x380000') + $upgraded
  forbid = @('engine plugin not installed', 'callbacks registered', 'engine scales active 0x3c0000', 'engine plugin notices')
}
# The sprite rows (a v2 block offering the sprites row, rows 0x203): Small Soldiers as Sprites on and Dead Unit Detail
# 0.5x. F7 draws no main view, so the focal length is unknown and Small Soldiers as Sprites sends 0 (off, its bit 22
# clear); Dead Unit Detail sets bit 23 (one line). The exe checks the kept scales 22-23 (0, 0.5).
# plugin_sprites_refused: the sprites row changed after the plugin (rows 0x203, 0x200 changed): its rows hidden, no bit,
# no line (the scales stored as the add-on folded them).
$spriteIni = @('GraphicsSmallSoldiersAsSprites=1', 'GraphicsDeadUnits=0.5')
$table['plugin_sprites'] = @{
  addon = 'current'; args = 'd3d11 fixed upgraded plugin sprite_scales'; encoding = 0; plugin = '1 515 0 2 515'
  ini = $spriteIni
  lines = @($identity, $mapped, '[Total War] 64-bit Attila: engine plugin ready, rows offered 0x203',
            '[Total War] 64-bit Attila: engine plugin rows tried 0x203',
            '[Total War] 64-bit Attila: engine scales active 0x800000') + $upgraded
  forbid = @('engine plugin not installed', 'callbacks registered', 'engine scales active 0xc00000',
             'engine scales active 0x400000', 'engine plugin notices')
}
$table['plugin_sprites_refused'] = @{
  addon = 'current'; args = 'd3d11 fixed upgraded plugin sprite_scales'; encoding = 0; plugin = '1 515 512 2 515'
  ini = $spriteIni
  lines = @($identity, $mapped, '[Total War] 64-bit Attila: engine plugin ready, rows offered 0x3',
            '[Total War] 64-bit Attila: engine plugin rows tried 0x203') + $upgraded
  forbid = @('engine plugin not installed', 'callbacks registered', 'engine scales active', 'engine plugin notices')
}
# The game code row (a v2 block offering it, rows 0x403): Faster Game CPU Code mirrored into [Attila64Plugin.Values] for
# the next start. plugin_game_code: the row Off, the key absent (on): 0 written against this start's 1.
# plugin_game_code_key_off: the key 0 and the row at its default On: 1 written against this start's 0.
# plugin_game_code_default: the row On, the key absent: nothing written. plugin_game_code_refused: the row tried, not
# offered (rows 0x3, tried 0x403): the row Off, nothing written.
$gameCodeValue = '[Total War] 64-bit Attila: engine value FasterGameCpuCode'
$gameCodeCases = [ordered]@{
  plugin_game_code = @{ plugin = '1 1027 0 2 1027'; ini = @('FasterGameCpuCode=0'); offered = '0x403'; tried = '0x403'
                        value = "${gameCodeValue}: 0x0 at the next start (this start: 0x1)" }
  plugin_game_code_key_off = @{ plugin = '1 1027 0 2 1027'; ini = @('[Attila64Plugin.Values]', 'FasterGameCpuCode=0'); offered = '0x403'
                                tried = '0x403'; value = "${gameCodeValue}: 0x1 at the next start (this start: 0x0)" }
  plugin_game_code_default = @{ plugin = '1 1027 0 2 1027'; ini = @(); offered = '0x403'; tried = '0x403' }
  plugin_game_code_refused = @{ plugin = '1 3 0 2 1027'; ini = @('FasterGameCpuCode=0'); offered = '0x3'; tried = '0x403' }
}
foreach ($name in $gameCodeCases.Keys) {
  $gameCode = $gameCodeCases[$name]
  $table[$name] = @{
    addon = 'current'; args = 'd3d11 fixed upgraded plugin'; encoding = 0; plugin = $gameCode.plugin; ini = $gameCode.ini
    lines = @($identity, $mapped, "[Total War] 64-bit Attila: engine plugin ready, rows offered $($gameCode.offered)",
              "[Total War] 64-bit Attila: engine plugin rows tried $($gameCode.tried)") + @($gameCode.value | Where-Object { $_ }) + $upgraded
    forbid = @('engine plugin not installed', 'callbacks registered', 'engine scales active', 'engine plugin notices') +
             @(if (-not $gameCode.value) { $gameCodeValue })
  }
}
# The core rows (a v2 block offering the CPU row, rows 0x7, the render part built: render_core_status 1, WAITING): each
# writes only its own core_wanted bit (CORE_STEP 4: Faster Battle Step's, hidden, writes the plugin's default On, the
# bit already set); the exe checks the bits left. Keep Render Core Free is on by default (CORE_WANTED_DEFAULT holds
# CORE_RENDER), and its first present writes only a row off that default (attila64_plugin_link::Update).
# Effects on All Cores at its default Automatic adds both effect bits (48) in every case here and below, the row
# offered or not. plugin_render_core_kept: no ini, the plugin's bits 7: 7, no render line. plugin_render_core_saved_off:
# Off saved: 5, "off". plugin_render_core_battle_off: Keep Battle Core Free Off: 6, no render line.
# plugin_render_core_saved_on: On saved, the plugin's bits 5 (the render bit clear): at its default the bit stays the
# plugin's: 5, no render line.
$renderCore = '[Total War] 64-bit Attila: Keep Render Core Free'
$renderCoreCases = [ordered]@{
  plugin_render_core_kept = @{ plugin = '1 7 0 2 7 0 0 7 1'; ini = @(); bits = 55; battle = 'on' }
  plugin_render_core_saved_off = @{ plugin = '1 7 0 2 7 0 0 7 1'; ini = @('KeepRenderCoreFree=0'); bits = 53; battle = 'on'
                                    render = 'off' }
  plugin_render_core_battle_off = @{ plugin = '1 7 0 2 7 0 0 7 1'; ini = @('KeepBattleCoreFree=0'); bits = 54; battle = 'off' }
  plugin_render_core_saved_on = @{ plugin = '1 7 0 2 7 0 0 5 1'; ini = @('KeepRenderCoreFree=1'); bits = 53; battle = 'on' }
}
foreach ($name in $renderCoreCases.Keys) {
  $core = $renderCoreCases[$name]
  $table[$name] = @{
    addon = 'current'; args = "d3d11 fixed upgraded plugin core_wanted_$($core.bits)"; encoding = 0; plugin = $core.plugin; ini = $core.ini
    lines = @($identity, $mapped, '[Total War] 64-bit Attila: engine plugin ready, rows offered 0x7',
              '[Total War] 64-bit Attila: engine plugin rows tried 0x7', "[Total War] 64-bit Attila: Keep Battle Core Free $($core.battle)") +
            @(if ($core.render) { "$renderCore $($core.render)" }) + $upgraded
    forbid = @('engine plugin not installed', 'callbacks registered', 'engine plugin notices') + @(if (-not $core.render) { $renderCore })
  }
}
# Effects on All Cores (PK9 P9-4, rp94_ MUST-1: the add-on owns bits 4-5 from its first present, Off included): a v2
# block offering ROW_PARALLEL_EFFECTS (rows 0x803), the loader's slot VFX_PARALLEL_UPDATER OK after the start check.
# plugin_effects_default: the row at its default Automatic, core_wanted 5 (battle and step, the plugin's default):
# both effect bits set (53), its line. plugin_effects_off: EffectsOnAllCores=0, core_wanted 0x35 (as EffectsMode=1
# leaves them): bits 4-5 cleared (5), its line. plugin_effects_on: EffectsOnAllCores=2: CORE_EFFECTS_AUTO cleared (21),
# its line. plugin_effects_slot_changed: the slot's verdict CHANGED: the bits still written (53), no line.
$effects = '[Total War] 64-bit Attila: Effects on All Cores'
$effectsCases = [ordered]@{
  plugin_effects_default = @{ plugin = '1 2051 0 2 2051 0 0 5 0 1'; ini = @(); bits = 53; line = "$effects Automatic" }
  plugin_effects_off = @{ plugin = '1 2051 0 2 2051 0 0 53 0 1'; ini = @('EffectsOnAllCores=0'); bits = 5; line = "$effects Off" }
  plugin_effects_on = @{ plugin = '1 2051 0 2 2051 0 0 53 0 1'; ini = @('EffectsOnAllCores=2'); bits = 21; line = "$effects On" }
  plugin_effects_slot_changed = @{ plugin = '1 2051 0 2 2051 0 0 5 0 2'; ini = @(); bits = 53 }
}
# Faster Battle Step (PK8 W-A3; the add-on owns CORE_STEP 4 from its first present): a v2 block offering
# ROW_BATTLE_STEP (rows 0x1003, lane NOT_USED_YET) with core_wanted 1 (the bit clear, so the write shows). While the
# row is hidden (BATTLE_STEP_ROW_MEASURED false) it writes the plugin's default On, a saved Off included.
# plugin_battle_step_on: no key: 5, the line "on". plugin_battle_step_saved_off: FasterBattleStep=0 waits: 5, the line
# "on", no "off" (at the measured flip: 1, the line "off"). plugin_battle_step_not_offered: rows 0x3 (BattleStep=0
# leaves the site out): the bit written (5), no line.
$battleStep = '[Total War] 64-bit Attila: Faster Battle Step'
$effectsCases['plugin_battle_step_on'] = @{ plugin = '1 4099 0 2 4099 0 0 1 0 0 1'; ini = @(); bits = 53; line = "$battleStep on"
                                            forbid = @("$battleStep off") }
$effectsCases['plugin_battle_step_saved_off'] = @{ plugin = '1 4099 0 2 4099 0 0 1 0 0 1'; ini = @('FasterBattleStep=0'); bits = 53
                                                   line = "$battleStep on"; forbid = @("$battleStep off") }
$effectsCases['plugin_battle_step_not_offered'] = @{ plugin = '1 3 0 2 3 0 0 1 0 0 1'; ini = @(); bits = 53; forbid = @($battleStep) }
# Multithreaded Collisions (PK8 Stage B step 6): a v2 block offering ROW_CONTACT_SPLIT (rows 0x2003). While the row is
# hidden (CONTACT_SPLIT_ROW_MEASURED false) the add-on writes no CORE_SPLIT 8, a saved row of the other value included,
# so a batch arm's ContactSplit key holds. plugin_contact_split_hidden_on: core_wanted 13 (ContactSplit=1, lane B
# NOT_USED_YET), MultithreadedCollisions=0 saved: 13, no line. plugin_contact_split_hidden_off: core_wanted 5 (the
# default ini, lane B OFF), MultithreadedCollisions=1 saved: 5, no line.
$collisions = '[Total War] 64-bit Attila: Multithreaded Collisions'
$effectsCases['plugin_contact_split_hidden_on'] = @{ plugin = '1 8195 0 2 8195 0 0 13 0 0 257'
                                                     ini = @('MultithreadedCollisions=0'); bits = 61; forbid = @($collisions) }
$effectsCases['plugin_contact_split_hidden_off'] = @{ plugin = '1 8195 0 2 8195 0 0 5 0 0 1025'
                                                      ini = @('MultithreadedCollisions=1'); bits = 53; forbid = @($collisions) }
foreach ($name in $effectsCases.Keys) {
  $case = $effectsCases[$name]
  $rows = ($case.plugin -split ' ')[1]
  $offered = '0x{0:x}' -f [int]$rows
  $table[$name] = @{
    addon = 'current'; args = "d3d11 fixed upgraded plugin core_wanted_$($case.bits)"; encoding = 0; plugin = $case.plugin; ini = $case.ini
    lines = @($identity, $mapped, "[Total War] 64-bit Attila: engine plugin ready, rows offered $offered",
              "[Total War] 64-bit Attila: engine plugin rows tried $offered") + @($case.line | Where-Object { $_ }) + $upgraded
    forbid = @('engine plugin not installed', 'callbacks registered', 'engine plugin notices') + @($case.forbid | Where-Object { $_ }) +
             @(if ($name.StartsWith('plugin_effects') -and -not $case.line) { $effects })
  }
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
  $env:RENODX_F7_PACK = $case.pack
  $env:ATTILA_NO_PLUGINS = $case.no_plugins
  # A fresh ini: no LoadFromDllMain key, the tutorial done (no splash), the add-on's output rows explicit, then the
  # case's own settings.
  (@(
    '[ADDON]', 'DisabledAddons=', '[GENERAL]', 'EffectSearchPaths=', 'TextureSearchPaths=', 'PresetPath=.\ReShadePreset.ini',
    '[OVERLAY]', 'TutorialProgress=4', '[renodx]', "SwapChainEncoding=$($case.encoding)",
    '[renodx-preset1]', 'tonemapper=3', 'peak_white_nits=1000', 'diffuse_white_nits=203', 'graphics_white_nits=203', 'gamma_correction=1'
  ) + @($case.ini | Where-Object { $_ })) | Set-Content -LiteralPath (Join-Path $dir 'ReShade.ini') -Encoding ascii
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
