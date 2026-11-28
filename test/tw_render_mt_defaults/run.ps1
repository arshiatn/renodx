# The render_mt rows' shipped defaults (src/games/twattila/addon.cpp) against the end batch's defaults table (eba_ rev 2,
# section 6, ATN 2026-11-24): each row's default, its binding's start value and whether players see it; the add-on built
# with Threaded Draw Recording; the saved values cleared once; the worker pool's spin and the animation schedule. Reads the
# source only (nothing is built or run).
# -Source: another addon.cpp (a mutation check).
param([string]$Source = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($Source -eq '') { $Source = Join-Path $repairRoot 'src\games\twattila\addon.cpp' }
$text = [System.IO.File]::ReadAllText($Source).Replace("`r`n", "`n")
$failures = [System.Collections.Generic.List[string]]::new()
function Check([bool]$ok, [string]$what) {
  if ($ok) { "ok   $what" } else { "FAIL $what"; $failures.Add($what) }
}
# The text from `start` to the line that closes it (`end`); empty when `start` is missing.
function Block([string]$start, [string]$end) {
  $at = $text.IndexOf($start, [StringComparison]::Ordinal)
  if ($at -lt 0) { return '' }
  return $text.Substring($at, $text.IndexOf($end, $at, [StringComparison]::Ordinal) - $at)
}
function Read-Source([string]$name) { [System.IO.File]::ReadAllText((Join-Path $repairRoot "src\games\twattila\$name")) }

# Built with Threaded Draw Recording (its rows, Parallel Instance Fill's and Fill With Draw Recording's), before any include.
$define = $text.IndexOf("`n#define TWATTILA_DRAW_RECORDING 1", [StringComparison]::Ordinal)
Check ($define -ge 0 -and $define -lt $text.IndexOf("`n#include", [StringComparison]::Ordinal)) 'TWATTILA_DRAW_RECORDING defined before the includes'

# Key, default, binding, its start value (both as addon.cpp writes them), shown to players.
$rows = @(
  @('DrawRecordingStage', '6.f', 'draw_recording_stage_row', '6.f', $true),
  @('DrawRecordingFamilies', '255.f', 'draw_recording_families_row', '255.f', $false),
  @('AnimationRowsWorkers', '1.f', 'animation_rows_row', '1.f', $true),
  @('ParallelInstanceFill', '0.f', 'instance_fill_row', '0.f', $false),
  @('FillWithDrawRecording', '0.f', 'fill_fold_row', '0.f', $false),
  @('ParallelEffectUpdate', '0.f', 'effect_update_row', '0.f', $true),
  @('ParallelEffectSpawns', '0.f', 'effect_spawns_row', '0.f', $false),
  @('CachedParticleCount', '0.f', 'particle_count_row', '0.f', $false),
  @('WorkerThreads', '1.f', 'worker_threads_row', '4.f', $true),
  @('KeepRenderCoreFree', 'KEEP_RENDER_CORE_DEFAULT', 'keep_render_core_row', 'KEEP_RENDER_CORE_DEFAULT', $true),
  @('FasterJobWakeUps', '0.f', 'job_wakeups_row', '0.f', $true),
  @('FasterRenderLists', '1.f', 'render_lists_row', '1.f', $true),
  @('FasterEffectPrefetch', '0.f', 'effect_prefetch_row', '0.f', $true),
  @('FasterEmitterPrefetch', '0.f', 'emitter_prefetch_row', '0.f', $true),
  @('FasterParticlePrefetch', '0.f', 'particle_prefetch_row', '0.f', $true),
  @('FasterSweepPrefetch', '0.f', 'sweep_prefetch_row', '0.f', $true),
  @('FewerConstantUploads', '(ATTILA64 ? 1.f : 0.f)', 'fewer_constant_uploads_row', '(ATTILA64 ? 1.f : 0.f)', $true),
  @('FewerDriverWaits', '0.f', 'fewer_driver_waits_row', '0.f', $true)
)
$hidden = '.is_visible = [] { return false; },'
foreach ($row in $rows) {
  $key, $default, $binding, $start, $shown = $row
  $block = Block ".key = `"$key`"," "`n      },"
  if ($block -eq '') { Check $false "$key declared"; continue }
  Check ($block -match "\.binding = &$binding,") "$key binds $binding"
  Check ($block.Contains(".default_value = $default,")) "$key default $default"
  Check ($block.Contains($hidden) -ne $shown) "$key $(if ($shown) { 'shown' } else { 'hidden' })"
  Check ($text -match "(?m)^float $binding = $([regex]::Escape($start));") "$binding starts at $start"
}
# The stage's 7 labels (6, the default, is the last one).
$stage = Block '.key = "DrawRecordingStage",' "`n      },"
Check ($stage.Contains('.labels = {"Off", "Test 1", "Test 2", "Test 3", "Test 4", "One Worker", "On"},')) 'DrawRecordingStage labels, On at 6'
# WorkerThreads' index 1 is 4 threads.
Check ($text -match '\.labels = \{"2", "4", "6", "8", "12", "16"\},') 'WorkerThreads index 1 = 4 threads'
# Animation Rows on Workers: Off / On only (Check through RENODX_TW_ANIMATION_CHECK).
$animation = Block '.key = "AnimationRowsWorkers",' "`n      },"
Check ($animation -match '\.labels = \{"Off", "On"\},' -and -not $animation.Contains('"Check"')) 'AnimationRowsWorkers labels Off / On'
# The four loop groups: on, hidden, all through DrawRecordingGroupRow.
$helper = Block 'DrawRecordingGroupRow(const char* key' "`n}`n"
Check ($helper -match '\.default_value = 1\.f,') 'draw recording groups default 1'
Check ($helper.Contains($hidden)) 'draw recording groups hidden'
Check ($text -match '(?m)^std::array<float, 4> draw_recording_group_rows = \{1\.f, 1\.f, 1\.f, 1\.f\};') 'draw recording groups start at 1'
foreach ($group in 'DepthA', 'ColourA', 'DepthB', 'ColourB') {
  Check ($text.Contains("DrawRecordingGroupRow(`"DrawRecording$group`",")) "DrawRecording$group declared"
}
# Values saved by older versions go once: revision 1's rows as they were; revision 2 every row above whose default changed
# or that is hidden.
Check ($text.Contains('twattila::graphics_page::ClearSavedRows(1, {"RayTracingDenoiser", "ScreenSpaceShadowsBannerLight", "ScreenSpaceShadowsColoured"});')) 'revision 1 rows unchanged'
$revision = Block 'twattila::graphics_page::ClearSavedRows(2, {' '});'
foreach ($key in 'DrawRecordingStage', 'DrawRecordingDepthA', 'DrawRecordingColourA', 'DrawRecordingDepthB', 'DrawRecordingColourB',
         'DrawRecordingFamilies', 'AnimationRowsWorkers', 'ParallelInstanceFill', 'FillWithDrawRecording', 'ParallelEffectSpawns',
         'CachedParticleCount') {
  Check ($revision.Contains("`"$key`"")) "$key cleared once at revision 2"
}
# RENODX_TW_POOL_SPIN_US unset: 3 us.
Check ((Read-Source 'worker_pool.hpp') -match 'constexpr uint32_t DEFAULT_SPIN_US = 3;') 'pool spin 3 us'
# Faster Game CPU Code (graphics_page.hpp's row, addon.cpp's binding) on; RENODX_TW_ANIMATION_SCHEDULE unset: stream.
Check ((Read-Source 'graphics_page.hpp') -match '(?s)\.key = "FasterGameCpuCode",.{0,200}?\.default_value = 1\.f,' -and $text -match '(?m)^float engine_code_row = 1\.f;') 'FasterGameCpuCode default 1'
Check ((Read-Source 'animation_rows.hpp').Contains('(text == "bulk" ? Schedule::BULK : Schedule::STREAM)')) 'animation schedule stream unless bulk'
# FCA item 1 (a bug fix, no row): the CPUs the game may use (the bench's env, else cpu_sets::Update every present), the pool's
# room from BudgetWorkers (none below 8 CPUs), every pool user's stage gated by it (L1 in its three places, C's worker stages
# before SiteGroups, the fill, the fold, B3b) and the count passed to the pool last.
Check ($text.Contains('const uint32_t game_cpus = game_cpus_env.value_or(twattila::cpu_sets::Update(!ATTILA64 && keep_render_core_row != 0.f, GAME_THREAD_MODULES));')) 'game_cpus: the env, else cpu_sets::Update (Keep Render Core Free never on the 64-bit port)'
Check ($text.Contains('const bool pool_room = (twattila::worker_pool::BudgetWorkers(game_cpus) != 0);')) 'pool_room from BudgetWorkers(game_cpus)'
Check ($text.Contains('const bool effect_update_on = (pool_room && effect_update_row != 0.f);') -and $text.Contains('bool pool_in_use = effect_update_on;') `
       -and $text.Contains('.effect_update = effect_update_on,') -and $text.Contains('twattila::effect_update::Update(effect_update_on,')) 'L1 gated by pool_room (pool, engine code, its Update)'
$gate = $text.IndexOf('if (recording_wakes_workers && !pool_room) {', [StringComparison]::Ordinal)
Check ($gate -ge 0 -and $gate -lt $text.IndexOf('recording_groups = twattila::draw_recording::SiteGroups(', [StringComparison]::Ordinal) `
       -and $text.Contains('pool_in_use |= (recording_wakes_workers && pool_room);')) 'C''s worker stages gated by pool_room before SiteGroups'
Check ($text.Contains('instance_fill_on = (pool_room && instance_fill_row != 0.f);')) 'instance fill gated by pool_room'
Check ($text.Contains('fill_fold_on = (pool_room && fill_fold_row != 0.f);')) 'fill fold gated by pool_room'
Check ($text.Contains('((animation_rows_row == 0.f || !pool_room)')) 'animation rows gated by pool_room'
Check ($text -match '\.env_generation = env_generation,\s+\.game_cpus = game_cpus\}\);') 'the pool gets .game_cpus = game_cpus, last'
# PK1-64 (the 64-bit port only): Fewer Driver Waits hidden and forced off there; Fewer Constant Uploads on there, alone
# moved to the CPU page's section CPU, its saved Off cleared once under the port's own revision key, and its once-line.
Check ($text.Contains('{.match = "FewerDriverWaits"},')) 'FewerDriverWaits in GAME_CODE_ROWS (hidden on the port)'
Check ($text.Contains('twattila::buffer_pool::Update(context, !ATTILA64 && fewer_driver_waits_row != 0.f, traced);')) 'FewerDriverWaits forced off on the port'
Check ($text.Contains('const auto moved = std::ranges::find(settings, "FewerConstantUploads", &renodx::utils::settings::Setting::key);') `
       -and -not $text.Contains('{"FewerConstantUploads", "FewerDriverWaits"}') `
       -and -not ($text -match '"FewerDriverWaits"[^\n]*&renodx::utils::settings::Setting::key')) 'FewerConstantUploads alone moved to section CPU on the port (no find of FewerDriverWaits)'
$fcu = Block '.key = "FewerConstantUploads",' "`n      },"
Check ($fcu.Contains('.label = (ATTILA64 ? "Fewer Constant Uploads (Experimental, restart)" : "Fewer Constant Uploads (Experimental)"),')) 'FewerConstantUploads label: (Experimental, restart) on the port, (Experimental) on 32-bit'
Check ($fcu.Contains('.section = "Graphics (Experimental)",')) 'FewerConstantUploads 32-bit section Graphics (Experimental) unchanged'
Check ($text -match 'if constexpr \(ATTILA64\) \{\s*twattila::graphics_page::ClearSavedRows\(1, \{"FewerConstantUploads"\}, "Attila64SettingsRevision"\);') 'FewerConstantUploads cleared once on the port (Attila64SettingsRevision 1)'
$port_clear = $text.IndexOf('ClearSavedRows(1, {"FewerConstantUploads"}, "Attila64SettingsRevision");', [StringComparison]::Ordinal)
# The port's revision 2 (the 11-28 batch's defaults): every 64-bit preset saved 0 for both rows, right after revision 1.
Check ($text -match 'ClearSavedRows\(1, \{"FewerConstantUploads"\}, "Attila64SettingsRevision"\);\s*twattila::graphics_page::ClearSavedRows\(2, \{"EffectsOnAllCores", "KeepRenderCoreFree"\}, "Attila64SettingsRevision"\);\s*\}') 'EffectsOnAllCores + KeepRenderCoreFree cleared once on the port (Attila64SettingsRevision 2)'
Check ($port_clear -ge 0 -and $port_clear -lt $text.IndexOf('renodx::utils::settings::Use(DLL_PROCESS_ATTACH', [StringComparison]::Ordinal)) 'the port clear before settings::Use'
Check ($text.Contains('renodx::utils::log::i("[Total War] Fewer Constant Uploads: on.");')) 'Fewer Constant Uploads once-line'
Check ($text.Contains("`n    fewer_constant_uploads_logged = false;`n")) 'Fewer Constant Uploads once-flag reset at detach'
# PK5-64 P5-5 (the 64-bit port only): Faster Game CPU Code is the engine plugin's game code row, moved to the CPU page's
# section CPU with its port label, shown while the plugin offers the row, its restart note after it, and its value
# mirrored for the next start; the 32-bit row as before.
Check ($text.Contains('GAME_CODE = twattila::attila64_plugin::ROW_GAME_CODE,')) 'PluginRow::GAME_CODE = ROW_GAME_CODE'
Check ($text.Contains('{.match = "FasterGameCpuCode", .plugin_row = PluginRow::GAME_CODE},')) 'FasterGameCpuCode in GAME_CODE_ROWS with PluginRow::GAME_CODE'
$gc = Block 'const auto game_code = std::ranges::find(settings, "FasterGameCpuCode", &renodx::utils::settings::Setting::key);' "`n                    });"
Check ($gc.Contains('game_code_row->label = "Faster Game CPU Code (restart)";') -and $gc.Contains('game_code_row->section = CPU_SECTION;') `
       -and $gc.Contains('game_code_row->is_visible = [] { return GameCodeRowShown(PluginRow::GAME_CODE); };') `
       -and $gc.Contains('.label = RESTART_NOW,') -and $gc.Contains('return row->is_visible() && twattila::attila64_plugin_link::GameCodePending();')) 'FasterGameCpuCode on the CPU page: (restart) label, shown by its row, restart note'
Check ($gc.Contains("game_code_row->tooltip = `"Faster versions of some of the game's own CPU code (sprite soldiers), same results.`";")) 'FasterGameCpuCode port tooltip (one sentence)'
Check ($text -match 'MirrorRestartValues\(loader_frame_cap_row != 0\.f, static_cast<int>\(shadow_map_size\),\s+engine_code_row != 0\.f\);') 'FasterGameCpuCode mirrored for the next start'
Check ((Read-Source 'graphics_page.hpp') -match '(?s)\.key = "FasterGameCpuCode",.{0,300}?\.label = "Faster Game CPU Code",') 'FasterGameCpuCode 32-bit label unchanged'
# PK2P P1a (the 64-bit port only): Keep Render Core Free is the engine plugin's render core, its default the plugin's
# CORE_RENDER bit (32-bit: 0, off; the port: on, PK2P W5), moved to the CPU page after Keep Battle Core Free, shown
# since its A/B measured it (RENDER_CORE_ROW_MEASURED true, the 11-28 batch), hidden while the plugin built no render
# part, the toggle also with RenderCore=0; its bit written by the link's Update; the 32-bit row as before.
Check ($text.Contains('constexpr float KEEP_RENDER_CORE_DEFAULT = ((ATTILA64 && twattila::attila64_plugin_link::RENDER_CORE_DEFAULT) ? 1.f : 0.f);')) 'KeepRenderCoreFree default: the plugin''s CORE_RENDER bit on the port, 0 on 32-bit'
Check ((Read-Source 'attila64_plugin_link.hpp') -match '(?s)inline constexpr bool RENDER_CORE_DEFAULT =\s+\(attila64_plugin::CORE_WANTED_DEFAULT & attila64_plugin::CORE_RENDER\) != 0;') 'RENDER_CORE_DEFAULT from CORE_WANTED_DEFAULT & CORE_RENDER'
Check ($text -match '(?m)^constexpr bool RENDER_CORE_ROW_MEASURED = true;') 'RENDER_CORE_ROW_MEASURED true (the port row shown since its A/B)'
Check ((Read-Source 'attila64_plugin\plugin_block.h').Contains('inline constexpr uint32_t CORE_WANTED_DEFAULT = CORE_BATTLE | CORE_RENDER | CORE_STEP;')) 'CORE_WANTED_DEFAULT: battle core, render core, battle step'
Check ($text.Contains('{.match = "KeepRenderCoreFree", .plugin_row = PluginRow::CPU_PLACEMENT},')) 'KeepRenderCoreFree in GAME_CODE_ROWS with PluginRow::CPU_PLACEMENT'
$rc = Block 'const auto render_core = std::ranges::find(settings, "KeepRenderCoreFree", &renodx::utils::settings::Setting::key);' "`n        });"
Check ($rc.Contains('render_core_row->label = "Keep Render Core Free";') -and $rc.Contains('render_core_row->section = CPU_SECTION;') `
       -and $rc.Contains('return render_core_shown() && render_core_state() != plugin::CORE_OFF_INI;') `
       -and $rc.Contains('.is_visible = render_core_shown,')) 'KeepRenderCoreFree on the CPU page: port label, toggle hidden with RenderCore=0, its status line'
Check ($text.Contains('return RENDER_CORE_ROW_MEASURED && GameCodeRowShown(PluginRow::CPU_PLACEMENT) && render_core_state() != 0;')) 'KeepRenderCoreFree shown only measured, the CPU row offered and the render part built'
$battle = $text.IndexOf('.key = "KeepBattleCoreFree",', [StringComparison]::Ordinal)
$render = $text.IndexOf('const auto render_core = std::ranges::find(settings, "KeepRenderCoreFree"', [StringComparison]::Ordinal)
Check ($battle -ge 0 -and $render -gt $battle -and $render -lt $text.IndexOf('const auto moved = std::ranges::find(settings, "FewerConstantUploads"', [StringComparison]::Ordinal)) 'KeepRenderCoreFree after Keep Battle Core Free, before Fewer Constant Uploads'
Check ($text.Contains('(RENDER_CORE_ROW_MEASURED ? keep_render_core_row != 0.f : twattila::attila64_plugin_link::RENDER_CORE_DEFAULT),')) 'KeepRenderCoreFree written to the plugin (link Update) only once measured'
# Effects on All Cores (PK9, the port only): Automatic by default (ATN 11-28), one constant for the binding and the row.
Check ($text -match '(?m)^constexpr float EFFECTS_ON_ALL_CORES_DEFAULT = 1\.f;' -and $text -match '(?m)^float effects_on_all_cores_row = EFFECTS_ON_ALL_CORES_DEFAULT;') 'EffectsOnAllCores starts at EFFECTS_ON_ALL_CORES_DEFAULT = 1 (Automatic)'
$eac = Block '.key = "EffectsOnAllCores",' "`n    });"
Check ($eac.Contains('.binding = &effects_on_all_cores_row,') -and $eac.Contains('.default_value = EFFECTS_ON_ALL_CORES_DEFAULT,') `
       -and $eac.Contains('.label = "Effects on All Cores (Experimental)",')) 'EffectsOnAllCores default EFFECTS_ON_ALL_CORES_DEFAULT, label (Experimental)'
# Multithreaded Collisions (PK8 Stage B 14): default Off, hidden until measured, and no CORE_SPLIT write while hidden.
Check ($text -match '(?m)^constexpr bool CONTACT_SPLIT_ROW_MEASURED = false;' -and $text -match '(?m)^float multithreaded_collisions_row = 0\.f;') 'MultithreadedCollisions hidden, starts at 0'
Check ($text -match '(?s)\.key = "MultithreadedCollisions",.{0,200}?\.default_value = 0\.f,') 'MultithreadedCollisions default 0'
Check ($text.Contains('(CONTACT_SPLIT_ROW_MEASURED ? std::optional<bool>(multithreaded_collisions_row != 0.f) : std::nullopt));')) 'MultithreadedCollisions written to the plugin (link Update) only once measured'
Check ((Read-Source 'addon.cpp') -match '(?s)\.key = "KeepRenderCoreFree",.{0,300}?\.label = "Keep Render Core Free \(Experimental\)",\s+\.section = "Graphics \(Experimental\)",') 'KeepRenderCoreFree 32-bit label and section unchanged'
Check ((Read-Source 'worker_pool.hpp') -match 'constexpr uint32_t MIN_GAME_CPUS = 8;') 'MIN_GAME_CPUS 8'
# Automatic L1 (inside its row, which stays Off above): parallel from 20000 effect instances, serial below 16000, never below
# 256; the bench env RENODX_TW_L1_UNITS read again at the pool's env generation.
$effect = Read-Source 'effect_update.hpp'
Check ($effect.Contains('constexpr UnitRule SHIPPED_UNIT_RULE = {MIN_ROUND_UNITS, 20000, 16000};') -and $effect -match 'constexpr uint32_t MIN_ROUND_UNITS = 256;' `
       -and $effect.Contains('static UnitRule unit_rule = SHIPPED_UNIT_RULE;')) 'L1 rule 256 / 20000 / 16000'
Check ($text.Contains('twattila::effect_update::Update(effect_update_on, effect_spawns_row != 0.f, env_generation);')) 'L1 gets the env generation'

if ($failures.Count -ne 0) { throw "render_mt defaults: $($failures.Count) mismatch(es)" }
'render_mt defaults: all match'
