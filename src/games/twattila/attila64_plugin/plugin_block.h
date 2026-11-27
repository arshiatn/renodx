/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>

// The block the 64-bit Attila plugin (renodx_attila64.dll, a loader plugin that hooks game code) shares with the
// Attila add-on in the same process: what the plugin applied, which hooks the add-on wants, and the add-on's callbacks.
// The add-on finds it with GetModuleHandleW(MODULE_NAME) + GetProcAddress(BLOCK_EXPORT). Append-only like dlaa_bridge.h:
// new fields at the end with VERSION + 1, never reordered or reused; Callbacks the same. The v1 plugin reads neither
// Callbacks::version nor bytes (one layout); a later plugin will call a member added after v1 only when the add-on's
// `bytes` covers it. Layout version 1 (the first 40 bytes) is frozen (2026-11-25), version 2 too (2026-11-27): later
// changes append Row values, core_wanted bits and the default constant only; a field that fits nowhere in v2 needs v3.
// The add-on reads a v2 field only when `version >= 2 && bytes >= BlockBytes(2)`.
//
// The add-on's side of the callback protocol (the plugin's side is hooks.hpp's CallbackScope):
// - Register: take a load reference on the add-on's own module, compare_exchange `callbacks` nullptr -> its Callbacks
//   (a lost exchange: another add-on has it; release the reference), then set the `hooks_wanted` bits.
// - Unregister: clear the `hooks_wanted` bits, `callbacks.store(nullptr)` (seq_cst), then seq_cst loads of
//   `calls_in_flight` until it is 0. The first 0 ends the wait: a hook that enters later still counts, but it loads
//   nullptr and calls nothing. Both operations must be seq_cst: a release store may pass the following load (on x86 a
//   plain mov), and a hook could then load the old pointer after the wait. Bound the wait (1 s, Sleep(0) / Sleep(1));
//   drained -> release the module reference; not drained -> keep it for the process. At process-exit detach
//   (lpReserved != nullptr) skip the wait and keep the reference.
// - Never unregister from inside a callback, or while holding a lock a callback takes: the wait would count the caller
//   itself and end only at its bound, not drained.
// - Every Callbacks member is non-null: the plugin calls them without a check.
// - Callbacks run on the game's threads, possibly several at once, with the Windows calling convention; they may nest
//   (the pack hook can run inside a measured pass's original). None runs while a game original runs inside its call.
// - pass_end runs only when `callbacks` holds the same pointer as at its pass_begin. A re-registration at the same
//   address (the same static Callbacks) looks the same, so pass_begin writes the add-on's registration epoch into
//   PassMeasure::reserved and the add-on ignores a pass_end whose epoch is not the current one.
//
// The rows without callbacks (the scale rows, the value rows and CPU_PLACEMENT) ignore `hooks_wanted`: their hooks read
// `scales_active` and `scales`, or `core_wanted`. The add-on leaves the scales as they are at its detach, as the 32-bit
// add-on keeps its engine values (values switched back and forth in a running battle crashed Warhammer III). The scales
// and `scales_active` are separate atomics read without a lock, the stated exception to one lock per logical state: the
// hooks run per call on the game's threads and may not lock. Each scale is its own value and the mask is derived from
// them; a hook that reads a new mask with an old scale applies the old scale for that one call, as a 32-bit slider
// change does.
namespace twattila::attila64_plugin {

inline constexpr uint32_t VERSION = 2;
inline constexpr uint32_t OLDEST_VERSION = 1;  // the oldest block the add-on reads

inline constexpr wchar_t MODULE_NAME[] = L"renodx_attila64.dll";
inline constexpr char BLOCK_EXPORT[] = "renodx_attila64_block";

enum Status : uint32_t {
  STATUS_LOADING,
  STATUS_READY,
  STATUS_OFF,          // the [Attila64Plugin] Enabled=0 kill switch
  STATUS_WRONG_GAME,   // another game build
  STATUS_WRONG_LOADER  // loader interface, game range or the plugin's file name not as written for
};

// One per feature; bit (1 << row) in rows_applied, rows_refused, rows_changed, rows_tried and hooks_wanted. Append only.
// Reserved, no value yet: Big Battle Fix, Cached Particle Count and Exit Job Drain (parked), and the battle step's rows
// ROW_BATTLE_STEP (ini key BattleStep) and ROW_CONTACT_SPLIT (ContactSplit).
enum Row : uint32_t {
  ROW_BONE_FLUSH,      // before the game packs its animation textures: the add-on flushes its unit records
  ROW_PASS_DRAW,       // each scene node's draw into a view: the add-on may skip the game's shadow-map work
  ROW_CPU_PLACEMENT,   // the battle step's task: Keep Battle Core Free, with Keep Render Core Free as its second part
  ROW_UNIT_LOD,        // the unit LOD distances: Unit Detail and the animation, variety and footstep rows (scales 0-16)
  ROW_MODEL_DETAIL,    // Model Detail Distance (scale 17)
  ROW_SHADOW_MAP,      // Shadow Map Size: a code patch at init, restart-only
  ROW_GRASS,           // Grass Detail Distance and Grass Density (scales 19-21)
  ROW_GRASS_CELLS,     // the grass cell radius (scales 19 and 20)
  ROW_CAMPAIGN_TREES,  // Campaign Tree Detail Distance (scale 18)
  ROW_SPRITES,         // Small Soldiers as Sprites, Sprite Size and Dead Unit Detail (scales 22 and 23)
  ROW_GAME_CODE,       // Faster Game CPU Code: hand-made stubs for game code (game_code.hpp), restart-only
  ROW_PARALLEL_EFFECTS,  // Effects on All Cores: the walk over a scene node's effect types (core_wanted CORE_EFFECTS)
  ROW_COUNT
};

constexpr uint32_t RowBit(Row row) { return 1u << row; }

enum PassAction : uint32_t { PASS_DRAW, PASS_SKIP, PASS_DRAW_MEASURED };

// The add-on's state for one measured call, kept on the plugin hook's stack between pass_begin and pass_end (zeroed
// before pass_begin). Fixed size forever: another layout means new callback members in a new Callbacks version.
struct PassMeasure {
  uint32_t sun_binds_before = 0;
  uint32_t other_binds_before = 0;
  uint32_t sun_at_start = 0;
  uint32_t reserved = 0;  // the add-on's registration epoch (see the protocol above)
};

// The add-on's, immutable while published. Called from the game's threads; none may throw.
struct Callbacks {
  uint32_t version = VERSION;  // VERSION the add-on was built with
  uint32_t bytes = sizeof(Callbacks);
  void (*before_pack)() noexcept = nullptr;
  PassAction (*pass_begin)(uint32_t pass_kind, PassMeasure* measure) noexcept = nullptr;
  void (*pass_end)(uint32_t pass_kind, const PassMeasure& measure) noexcept = nullptr;
};

// core_wanted, per bit. The plugin tests each part's bit (`& CORE_BATTLE`, `& CORE_RENDER`) and ignores bits it does
// not know. The add-on changes only its rows' bits (`fetch_or` / `fetch_and`) and never stores the whole word, so a bit
// that no loaded row writes keeps the plugin's initial value. The initial value is CORE_WANTED_DEFAULT; the plugin's
// RenderCore key then sets or clears CORE_RENDER at init.
inline constexpr uint32_t CORE_BATTLE = 1u << 0;  // Keep Battle Core Free
inline constexpr uint32_t CORE_RENDER = 1u << 1;  // Keep Render Core Free
inline constexpr uint32_t CORE_STEP = 1u << 2;    // reserved: the battle step row
inline constexpr uint32_t CORE_SPLIT = 1u << 3;   // reserved: the contact split row
// Off = neither bit, Automatic = both, On = CORE_EFFECTS alone. CORE_EFFECTS_AUTO without CORE_EFFECTS reads as Off:
// CORE_EFFECTS is the wanted bit, AUTO only qualifies it. Each change of stop is one fetch_or or fetch_and, so no
// reader sees a stop that was not chosen.
inline constexpr uint32_t CORE_EFFECTS = 1u << 4;       // Effects on All Cores
inline constexpr uint32_t CORE_EFFECTS_AUTO = 1u << 5;  // ... only in big battles (Automatic)
// Both parts by default is a release step (this constant), not a layout change.
inline constexpr uint32_t CORE_WANTED_DEFAULT = CORE_BATTLE;

// One state list for both core status words; 0 = not built. The render part has its own state and error. A render
// failure or yield never ends the battle part. A shared (process-level) end or gate ends both, and the render word then
// holds CORE_WITH_BATTLE (so it never holds NO_SMT, FEW_CORES or UNSUPPORTED).
enum CoreState : uint8_t {
  CORE_WAITING = 1,  // no battle step yet
  CORE_ACTIVE,       // the core is kept free
  CORE_NO_SMT,       // not used: no core with two logical CPUs
  CORE_FEW_CORES,    // not used: too few cores
  CORE_UNSUPPORTED,  // not used: more than 64 CPUs, or processor groups
  CORE_YIELDED,      // stopped: another program changed the game's CPU sets
  CORE_FAILED,       // stopped: Windows refused (the error field)
  CORE_NO_CORE,      // the render part: no core fits beside the battle core
  CORE_OWN_RULE,     // the render part: its thread has a CPU rule of its own
  CORE_OFF_INI,      // the render part: RenderCore=0
  CORE_NO_THREAD,    // the render part: its thread was never found, or it ended
  CORE_NOT_RENDER,   // the render part: the thread found is not the render thread
  CORE_WITH_BATTLE,  // the render part: ended with the battle part (a shared end or gate)
  CORE_STATE_COUNT
};

// core_status (the battle part, 64 bits) and render_core_status (the render part, 32 bits) share bits 0-7 the
// CoreState, 8-15 logical CPU A and 16-23 logical CPU B (the core's two). core_status: 24-31 the cores the game keeps,
// 32-63 the Win32 error. render_core_status: 24-31 its own Win32 error, 0-254 as it is, RENDER_ERROR_LARGE for a larger
// one (attila64_log.txt has the full code).
inline constexpr uint32_t CORE_STATE_SHIFT = 0;
inline constexpr uint32_t CORE_LOGICAL_A_SHIFT = 8;
inline constexpr uint32_t CORE_LOGICAL_B_SHIFT = 16;
inline constexpr uint32_t CORE_ALLOWED_SHIFT = 24;
inline constexpr uint32_t CORE_ERROR_SHIFT = 32;
inline constexpr uint32_t RENDER_ERROR_SHIFT = 24;
inline constexpr uint32_t CORE_FIELD_MASK = 0xff;  // each 8-bit field of both words
inline constexpr uint64_t CORE_ERROR_MASK = 0xffffffff;
inline constexpr uint32_t RENDER_ERROR_LARGE = 0xff;

// engine_status: one 8-bit lane per engine row; 0 = not built (the add-on hides that row). The plugin changes only its
// own lane, with a compare-exchange loop, and only on a state change (no counters). The add-on only reads it. Bits
// 16-31 are free (neutral 0).
inline constexpr uint32_t ENGINE_STEP_SHIFT = 0;   // the battle step row
inline constexpr uint32_t ENGINE_SPLIT_SHIFT = 8;  // the contact split row
inline constexpr uint32_t ENGINE_LANE_MASK = 0xff;
enum EngineState : uint8_t {
  ENGINE_NOT_USED_YET = 1,
  ENGINE_ACTIVE,
  ENGINE_PARTLY,        // sticky: a call took the game's own path
  ENGINE_OFF,           // its core_wanted bit is clear
  ENGINE_OFF_INI,       // its install key is 0
  ENGINE_CODE_DIFFERS,  // a fingerprint or a site check failed, at init or at the game's start
};

enum TweaksStatus : uint32_t {
  TWEAKS_NONE,     // no table in this build, or Tweaks=0
  TWEAKS_PENDING,  // checked at the game's start
  TWEAKS_READY,    // stored with release at the end of the game's start: the slots are final (acquire)
};
enum TweakType : uint8_t { TWEAK_BOOL, TWEAK_FLOAT, TWEAK_INT, TWEAK_UINT };
enum TweakVerdict : uint8_t { VERDICT_OK, VERDICT_ABSENT, VERDICT_WRONG_TYPE, VERDICT_WRONG_NAME, VERDICT_CHANGED };
enum RestartState : uint8_t { RESTART_NONE, RESTART_APPLIED, RESTART_OUT_OF_RANGE, RESTART_NOT_DX11 };

// One engine value as the plugin checked it.
struct TweakSlot {
  const char* name = nullptr;  // the game's tweak name
  // bool: the object + 0x62, the others: + 0x64; 0 when ABSENT, and for a value the loader writes (tweaks.h
  // loader_writes): that one is read by its verdict only.
  uint64_t value_address = 0;
  uint32_t default_bits = 0;   // what the check found
  uint32_t written_bits = 0;   // restart-only: the bits written at the game's start; else default_bits
  uint8_t type = TWEAK_BOOL;
  uint8_t verdict = VERDICT_ABSENT;
  uint8_t restart_only = 0;
  uint8_t restart_state = RESTART_NONE;
  uint32_t reserved = 0;
};

// notices: sticky fetch_or bits, no counters.
inline constexpr uint32_t NOTICE_REST_PACK = 1u << 0;         // the game packed the rest of a big battle's animation
inline constexpr uint32_t NOTICE_MODEL_TABLE_FULL = 1u << 1;  // models past the LOD table keep the game's distances

// One live multiplier each; bit s of scales_active = scales[s] is not NEUTRAL_SCALES[s]. Slots 24-31 are reserved
// (neutral, bit clear): a later plugin reads a new scale without a v3, and an older add-on leaves it neutral.
enum Scale : uint32_t {
  SCALE_UNIT_LOD = 0,       // 0-16: the unit LOD table's entries 0-16 (17, the cap, is kept)
  SCALE_MODEL_DETAIL = 17,  // the key x 1 / s^2
  SCALE_CAMPAIGN_TREES,     // an entry's distance^2 x s^2
  SCALE_GRASS_LOD,          // s^2
  SCALE_GRASS_RADIUS,
  SCALE_GRASS_DENSITY,
  SCALE_SPRITE_DISTANCE,  // metres per metre of soldier height; 0 = off
  SCALE_DEAD_UNITS,
  SCALE_COUNT = 32
};
inline constexpr auto NEUTRAL_SCALES = [] {
  std::array<float, SCALE_COUNT> neutral = {};
  neutral.fill(1.0f);
  neutral[SCALE_SPRITE_DISTANCE] = 0.0f;
  return neutral;
}();

struct Block {
  uint32_t version = VERSION;
  uint32_t bytes = sizeof(Block);
  std::atomic<uint32_t> status = STATUS_LOADING;  // stored last at init (release): the other fields final once not LOADING
  uint32_t rows_applied = 0;
  uint32_t rows_refused = 0;
  std::atomic<uint32_t> rows_changed = 0;     // set at game start (after READY) when a site's bytes changed after us
  std::atomic<uint32_t> hooks_wanted = 0;     // the add-on's; each hook reads it relaxed before any callback work
  // Plugin hooks inside the guard: raised before `callbacks` is loaded, lowered after the last call into it.
  std::atomic<uint32_t> calls_in_flight = 0;
  std::atomic<const Callbacks*> callbacks = nullptr;  // the add-on's
  // v2.
  std::atomic<uint32_t> core_wanted = CORE_WANTED_DEFAULT;  // per bit (CORE_BATTLE above)
  uint32_t rows_tried = 0;                // final at READY: the rows with sites in this build, minus those off in the ini
  std::atomic<uint64_t> core_status = 0;  // the battle part (CoreState and its fields above)
  std::atomic<uint32_t> tweaks_status = TWEAKS_NONE;
  uint32_t tweak_count = 0;  // with `tweaks`: final once tweaks_status is TWEAKS_READY
  const TweakSlot* tweaks = nullptr;
  std::atomic<uint32_t> notices = 0;
  std::atomic<uint32_t> render_core_status = 0;  // the render part; 0 = not built (the add-on hides its row)
  std::atomic<uint32_t> scales_active = 0;       // the add-on's, relaxed, after the changed scales
  std::atomic<uint32_t> engine_status = 0;       // the engine rows' lanes (above)
  // The add-on's, relaxed, stored only on a change; the hooks read them relaxed. The plugin's init stores NEUTRAL_SCALES.
  std::array<std::atomic<float>, SCALE_COUNT> scales = {};
};

// The bytes a block of a given version has (an older plugin's ends earlier; v1's where core_wanted starts).
constexpr size_t BlockBytes(uint32_t version) {
  if (version >= 2) return sizeof(Block);
  return (version == 1) ? offsetof(Block, core_wanted) : 0;
}

using BlockExportFn = Block* (*)();

// [Attila64Plugin] in <exe dir>\ReShade.ini, read once at the plugin's init (diagnosis and kill switches, by hand; the
// add-on never writes this section).
inline constexpr wchar_t INI_SECTION[] = L"Attila64Plugin";
inline constexpr wchar_t INI_ENABLED[] = L"Enabled";
// Absent: the build's default; 0: off; 1: CORE_RENDER set at init (an A/B without the add-on).
inline constexpr wchar_t INI_RENDER_CORE[] = L"RenderCore";
inline constexpr wchar_t INI_TWEAKS[] = L"Tweaks";  // check and publish the engine values at all
// Absent: no change (Off); 0 Off, 1 Automatic, 2 On: sets CORE_EFFECTS / CORE_EFFECTS_AUTO at init (an A/B without the
// add-on).
inline constexpr wchar_t INI_EFFECTS_MODE[] = L"EffectsMode";
inline constexpr int ENABLED_DEFAULT = 1;
inline constexpr int TWEAKS_DEFAULT = 1;
// Install the row's sites at all.
inline constexpr const wchar_t* ROW_INI_KEYS[ROW_COUNT] = {L"BoneFlush", L"PassDraw",   L"CpuPlacement", L"UnitLod",
                                                           L"ModelDetail", L"ShadowMap", L"Grass",       L"GrassCells",
                                                           L"CampaignTrees", L"Sprites",  L"GameCode",
                                                           L"ParallelEffects"};
inline constexpr int ROW_INSTALL_DEFAULT = 1;
// The restart-only values the add-on writes and the plugin reads at init.
inline constexpr wchar_t INI_VALUES_SECTION[] = L"Attila64Plugin.Values";
// The loader's frame cap for the next start, read as a decimal integer: exactly 1 keeps it, anything else (0, absent)
// turns it off. The add-on writes 0 or 1. (An engine value's key is its tweak name, its text 8 hex digits.)
inline constexpr wchar_t INI_SOL_FRAME_CAP[] = L"SolFrameCap";
// Shadow Map Size for the next start, read as a decimal integer: 0 (or absent) keeps the game's sizes, n = 1 or 2
// makes them x 2^n (Max and High 2048, Medium 1024). The add-on writes 0, 1 or 2. Written only on the DirectX 11 route.
inline constexpr wchar_t INI_SHADOW_MAP_SIZE[] = L"ShadowMapSize";
inline constexpr int SHADOW_MAP_SIZE_STEPS = 2;  // the largest ShadowMapSize
// Faster Game CPU Code for the next start, read as a decimal integer: 0 writes none of the game code row's stubs (the
// row is still checked, and in when its checks pass; text that is no number reads as 0), any other number, or no
// key, writes them. The add-on writes 0 or 1.
inline constexpr wchar_t INI_FASTER_GAME_CODE[] = L"FasterGameCpuCode";
inline constexpr int FASTER_GAME_CODE_DEFAULT = 1;
// Two Shadow Cascades for the next start (0 or 1). Reserved: no plugin reads it yet and the add-on does not write it;
// ROW_SHADOW_MAP only checks the cascade sites.
inline constexpr wchar_t INI_SHADOW_CASCADES[] = L"ShadowCascades";

static_assert(std::ranges::find(ROW_INI_KEYS, nullptr) == std::end(ROW_INI_KEYS), "an install key for every row");
static_assert(std::is_standard_layout_v<PassMeasure> && sizeof(PassMeasure) == 16);
static_assert(std::is_standard_layout_v<Callbacks> && std::is_standard_layout_v<Block>);
static_assert(std::is_standard_layout_v<TweakSlot>);
static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic<const Callbacks*>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free && std::atomic<float>::is_always_lock_free);
static_assert(sizeof(std::atomic<uint32_t>) == 4 && sizeof(std::atomic<uint64_t>) == 8 && sizeof(std::atomic<float>) == 4);
static_assert(offsetof(Block, version) == 0);
static_assert(offsetof(Block, bytes) == 4);
static_assert(offsetof(Block, status) == 8);
static_assert(offsetof(Block, rows_applied) == 12);
static_assert(offsetof(Block, rows_refused) == 16);
static_assert(offsetof(Block, rows_changed) == 20);
static_assert(offsetof(Block, hooks_wanted) == 24);
static_assert(offsetof(Block, calls_in_flight) == 28);
static_assert(offsetof(Block, callbacks) == 32);
static_assert(offsetof(Callbacks, version) == 0);
static_assert(offsetof(Callbacks, bytes) == 4);
// The block is shared only inside the 64-bit game; 32-bit builds include this header for PassMeasure / PassAction (v1 is
// 36 bytes there and the pointers shrink).
static_assert(sizeof(void*) != 8 || (BlockBytes(0) == 0 && BlockBytes(1) == 40 && BlockBytes(2) == 216 && sizeof(Block) == 216),
              "v1: 40 bytes, v2: 216 bytes in the 64-bit build");
static_assert(sizeof(void*) != 8
              || (offsetof(Block, core_wanted) == 40 && offsetof(Block, rows_tried) == 44
                  && offsetof(Block, core_status) == 48 && offsetof(Block, tweaks_status) == 56
                  && offsetof(Block, tweak_count) == 60 && offsetof(Block, tweaks) == 64 && offsetof(Block, notices) == 72
                  && offsetof(Block, render_core_status) == 76 && offsetof(Block, scales_active) == 80
                  && offsetof(Block, engine_status) == 84 && offsetof(Block, scales) == 88));
static_assert(sizeof(void*) != 8
              || (offsetof(TweakSlot, name) == 0 && offsetof(TweakSlot, value_address) == 8
                  && offsetof(TweakSlot, default_bits) == 16 && offsetof(TweakSlot, written_bits) == 20
                  && offsetof(TweakSlot, type) == 24 && offsetof(TweakSlot, verdict) == 25
                  && offsetof(TweakSlot, restart_only) == 26 && offsetof(TweakSlot, restart_state) == 27
                  && offsetof(TweakSlot, reserved) == 28 && sizeof(TweakSlot) == 32));
static_assert(sizeof(void*) != 8 || (offsetof(Callbacks, before_pack) == 8 && offsetof(Callbacks, pass_begin) == 16
                                     && offsetof(Callbacks, pass_end) == 24 && sizeof(Callbacks) == 32));

}  // namespace twattila::attila64_plugin
