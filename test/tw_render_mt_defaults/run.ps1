# The render_mt rows' shipped defaults (src/games/twattila/addon.cpp) against the end batch's defaults table (eba_ rev 2,
# section 6, ATN 2026-11-24): each row's default, its binding's start value and whether players see it; the add-on built
# with Threaded Draw Recording; the worker pool's spin. Reads the source only (nothing is built or run).
# -Source: another addon.cpp (a mutation check).
param([string]$Source = '')
$ErrorActionPreference = 'Stop'
$repairRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($Source -eq '') { $Source = Join-Path $repairRoot 'src\games\twattila\addon.cpp' }
$text = [System.IO.File]::ReadAllText($Source).Replace("`r`n", "`n")
$pool = [System.IO.File]::ReadAllText((Join-Path $repairRoot 'src\games\twattila\worker_pool.hpp'))
$failures = [System.Collections.Generic.List[string]]::new()
function Check([bool]$ok, [string]$what) {
  if ($ok) { "ok   $what" } else { "FAIL $what"; $failures.Add($what) }
}
# The text from `start` to the line that closes it (`end`); empty when `start` is missing.
function Block([string]$start, [string]$end) {
  $at = $text.IndexOf($start)
  if ($at -lt 0) { return '' }
  return $text.Substring($at, $text.IndexOf($end, $at) - $at)
}

# Built with Threaded Draw Recording (its rows, Parallel Instance Fill's and Fill With Draw Recording's), before any include.
$define = $text.IndexOf("`n#define TWATTILA_DRAW_RECORDING 1")
Check ($define -ge 0 -and $define -lt $text.IndexOf("`n#include")) 'TWATTILA_DRAW_RECORDING defined before the includes'

# Key, default, binding, its start value, shown to players.
$rows = @(
  @('DrawRecordingStage', 6, 'draw_recording_stage_row', '6.f', $true),
  @('DrawRecordingFamilies', 255, 'draw_recording_families_row', '255.f', $false),
  @('AnimationRowsWorkers', 1, 'animation_rows_row', '1.f', $true),
  @('ParallelInstanceFill', 0, 'instance_fill_row', '0.f', $false),
  @('FillWithDrawRecording', 0, 'fill_fold_row', '0.f', $false),
  @('ParallelEffectUpdate', 0, 'effect_update_row', '0.f', $true),
  @('ParallelEffectSpawns', 0, 'effect_spawns_row', '0.f', $false),
  @('CachedParticleCount', 0, 'particle_count_row', '0.f', $false),
  @('WorkerThreads', 1, 'worker_threads_row', '4.f', $true),
  @('KeepRenderCoreFree', 0, 'keep_render_core_row', '0.f', $true),
  @('FasterJobWakeUps', 0, 'job_wakeups_row', '0.f', $true)
)
$hidden = '.is_visible = [] { return false; },'
foreach ($row in $rows) {
  $key, $default, $binding, $start, $shown = $row
  $block = Block ".key = `"$key`"," "`n      },"
  if ($block -eq '') { Check $false "$key declared"; continue }
  Check ($block -match "\.binding = &$binding,") "$key binds $binding"
  Check ($block -match "\.default_value = $default\.f,") "$key default $default"
  Check ($block.Contains($hidden) -ne $shown) "$key $(if ($shown) { 'shown' } else { 'hidden' })"
  Check ($text -match "(?m)^float $binding = $([regex]::Escape($start));") "$binding starts at $start"
}
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
# Values saved by older versions go once (SettingsRevision 2): every row above whose default changed or that is hidden.
$revision = Block 'OLD_DEFAULT_ROWS = {{' "`n  }};"
Check ($text -match 'constexpr int SETTINGS_REVISION = 2;') 'settings revision 2'
foreach ($key in 'DrawRecordingStage', 'DrawRecordingDepthA', 'DrawRecordingColourA', 'DrawRecordingDepthB', 'DrawRecordingColourB',
         'DrawRecordingFamilies', 'AnimationRowsWorkers', 'ParallelInstanceFill', 'FillWithDrawRecording', 'ParallelEffectSpawns',
         'CachedParticleCount') {
  Check ($revision.Contains("{2, `"$key`"},")) "$key cleared once at revision 2"
}
# RENODX_TW_POOL_SPIN_US unset: 3 us.
Check ($pool -match 'constexpr uint32_t DEFAULT_SPIN_US = 3;') 'pool spin 3 us'

if ($failures.Count -ne 0) { throw "render_mt defaults: $($failures.Count) mismatch(es)" }
'render_mt defaults: all match'
