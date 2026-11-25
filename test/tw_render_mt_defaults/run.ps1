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
  @('KeepRenderCoreFree', '0.f', 'keep_render_core_row', '0.f', $true),
  @('FasterJobWakeUps', '0.f', 'job_wakeups_row', '0.f', $true),
  @('FasterRenderLists', '1.f', 'render_lists_row', '1.f', $true),
  @('FasterEffectPrefetch', '0.f', 'effect_prefetch_row', '0.f', $true),
  @('FasterEmitterPrefetch', '0.f', 'emitter_prefetch_row', '0.f', $true),
  @('FasterParticlePrefetch', '0.f', 'particle_prefetch_row', '0.f', $true),
  @('FasterSweepPrefetch', '0.f', 'sweep_prefetch_row', '0.f', $true),
  @('FewerConstantUploads', '0.f', 'fewer_constant_uploads_row', '0.f', $true),
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
Check ((Read-Source 'worker_pool.hpp') -match 'constexpr uint32_t MIN_GAME_CPUS = 8;') 'MIN_GAME_CPUS 8'
# Automatic L1 (inside its row, which stays Off above): parallel from 20000 effect instances, serial below 16000, never below
# 256; the bench env RENODX_TW_L1_UNITS read again at the pool's env generation.
$effect = Read-Source 'effect_update.hpp'
Check ($effect.Contains('constexpr UnitRule SHIPPED_UNIT_RULE = {MIN_ROUND_UNITS, 20000, 16000};') -and $effect -match 'constexpr uint32_t MIN_ROUND_UNITS = 256;' `
       -and $effect.Contains('static UnitRule unit_rule = SHIPPED_UNIT_RULE;')) 'L1 rule 256 / 20000 / 16000'
Check ($text.Contains('twattila::effect_update::Update(effect_update_on, effect_spawns_row != 0.f, env_generation);')) 'L1 gets the env generation'

if ($failures.Count -ne 0) { throw "render_mt defaults: $($failures.Count) mismatch(es)" }
'render_mt defaults: all match'
