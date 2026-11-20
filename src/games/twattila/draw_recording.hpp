/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#if !defined(_WIN64)
#include <emmintrin.h>
#include <float.h>
#include <xmmintrin.h>

#include <bit>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/log.hpp"
#include "./cpu_sets.hpp"
#endif

// Threaded Draw Recording (private build, Attila 32-bit): the game's draw loops scan their instance lists on private copies
// of the tracker, on the render thread and on a few workers; every step with a side effect (the flush, the virtual calls
// that write shared state) becomes an op, and the render thread replays the ops in item order, so D3D11, ReShade and the
// add-on callbacks see the calls of the serial loop, on the same thread, in the same order.
// A chunk starts at a mesh change; what the loops carry across a mesh change (the animation index for the depth loop) is
// seeded by a one-item warm-up of the game's own loop and checked at replay. Anything else (a seed miss, an op buffer
// overflow, a site this build does not record, a call that starts from state the chunks can't reproduce) runs that chunk or
// call again serially on the real tracker: slower, never different.
// The B groups (the folded driver's loop over a round's meshes) are cut by mesh: DispatchMesh plans the round, records runs of
// census meshes in chunks and runs the other classes' meshes directly, in list order (the B notes in the design reports).
// The thunks that call Record and the sites they replace belong to engine_code.hpp; this file owns the types they use.
// x86 only: on x64 everything is an empty stub.
namespace twattila::draw_recording {

// Row values, in the order of the stages.
enum class Stage : uint8_t { OFF, DIRECT, TOUCH_AHEAD, CHUNKED, RECORDED, ONE_WORKER, WORKERS };

// The loop groups (append only): the four loop groups are the bits of Settings::loops; SHARED is the flush entry's.
enum class Group : uint32_t { DEPTH_A, COLOUR_A, DEPTH_B, COLOUR_B, SHARED, COUNT };

struct Settings {
  Stage stage = Stage::OFF;
  // Threads that scan, the render thread included (2 to 16).
  uint32_t threads = 4;
  uint32_t loops = 1;
  // The thread context_hooks installed on (0: not installed), for the trace's note.
  DWORD context_hooks_thread = 0;
  // The B groups' dispatcher families (bit f: family f may record; a clear bit makes its classes direct segments).
  uint32_t b_families = 0xff;
};

#if !defined(_WIN64)

constexpr uint32_t MAX_THREADS = 16;
constexpr uint32_t MAX_WORKERS = MAX_THREADS - 1;
constexpr uint32_t SLOT_SIZE = 256;
// One slot per chunk, one more for the compare run.
constexpr uint32_t SLOT_COUNT = MAX_THREADS + 1;
constexpr uint32_t COMPARE_SLOT = MAX_THREADS;
constexpr uint32_t TRACKER_SIZE = 0xa0;

// The page the thunks read at fixed offsets. Never freed while any site may be written.
struct Shared {
  // Cut stubs: lock inc before the dispatcher, lock dec after.
  std::atomic<int32_t> active{0};
  // The animation index a direct loop call starts with.
  int32_t seed_anim = -1;
  // The last index a direct animation site set.
  int32_t last_anim = -1;
  uint32_t arena = 0;
  uint32_t arena_size = 0;
  uint32_t record = 0;
  uint32_t dispatch_depth = 0;
  uint32_t dispatch_colour = 0;
  // The B groups' per-mesh cut (the folded driver's loop over a round's meshes).
  uint32_t dispatch_mesh = 0;
};
static_assert(std::atomic<int32_t>::is_always_lock_free && sizeof(std::atomic<int32_t>) == 4);
static_assert(offsetof(Shared, active) == 0x00 && offsetof(Shared, seed_anim) == 0x04 && offsetof(Shared, last_anim) == 0x08
              && offsetof(Shared, arena) == 0x0c && offsetof(Shared, arena_size) == 0x10 && offsetof(Shared, record) == 0x14
              && offsetof(Shared, dispatch_depth) == 0x18 && offsetof(Shared, dispatch_colour) == 0x1c
              && offsetof(Shared, dispatch_mesh) == 0x20);

constexpr uint32_t MODE_SUPPRESS = 0;
constexpr uint32_t MODE_RECORD = 1;

// Why a slot's chunk is run again serially instead of replayed.
constexpr uint32_t SLOT_OVERFLOW = 1;
constexpr uint32_t SLOT_UNKNOWN_CLASS = 2;
// The whole call is run again.
constexpr uint32_t SLOT_CONFLICT = 4;

// A chunk's tracker copy: the game's loop runs on it while a thunk finds `esi` inside the arena. The census names no field
// above +0x7c in any group's tracker (every group works on the driver's one 0xa0 byte object): GROUPS[].tracker_bytes.
struct alignas(SLOT_SIZE) Slot {
  std::array<uint8_t, TRACKER_SIZE> tracker = {};
  uint32_t mode = MODE_RECORD;
  // Read by the init sites; written by the animation site's record in both modes.
  int32_t anim = -1;
  // C++ only from here on.
  uint32_t* ops_begin = nullptr;
  uint32_t* ops_cursor = nullptr;
  uint32_t* ops_end = nullptr;
  uint32_t flags = 0;
  // The Group the chunk belongs to (a site of another loop group in it is a bug: the chunk is run again).
  uint32_t group = 0;
  uint32_t chunk = 0;
  uint8_t* real_tracker = nullptr;
  // What the warm-up left: the tracker's first 0x14 bytes (read by colour only) and the animation index.
  std::array<uint8_t, 0x14> seed_fields = {};
  int32_t seed_anim = -1;
  // The header of the last op when it is a constant (equal constants that follow it add to its repeat count); else null.
  uint32_t* last_const = nullptr;
};
static_assert(sizeof(Slot) == SLOT_SIZE && offsetof(Slot, tracker) == 0x00 && offsetof(Slot, mode) == 0xa0 && offsetof(Slot, anim) == 0xa4);

// What the thunk leaves on the stack; the game's stack at the site starts right above it.
struct Frame {
  uint32_t site;
  // The low float of xmm0, as bits (read by the constant ops only).
  uint32_t xmm0;
  uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
  uint32_t eflags;
};
static_assert(sizeof(Frame) == 44 && offsetof(Frame, xmm0) == 4 && offsetof(Frame, edi) == 8 && offsetof(Frame, esi) == 12
              && offsetof(Frame, esp) == 20 && offsetof(Frame, ecx) == 32 && offsetof(Frame, eax) == 36 && offsetof(Frame, eflags) == 40);

// One per recording site (append only); the numbers are what the thunks push. The call-site flush ids stay as the fallback
// of group A when the flush entry hook can't be written; FLUSH_ENTRY is the hook's (its slot is arg 4 - 0x69).
enum class Site : uint32_t {
  DEPTH_FLUSH_BOUNDARY,
  DEPTH_FLUSH_END,
  DEPTH_FLUSH_ANIM,
  DEPTH_VF68,
  DEPTH_ANIM,
  COLOUR_FLUSH_BOUNDARY,
  COLOUR_FLUSH_END,
  COLOUR_FLUSH_VARIATION,
  COLOUR_FLUSH_CHANGE,
  COLOUR_SETUP,
  COLOUR_VF68_SETUP,
  COLOUR_VF68_VARIATION,
  COLOUR_SETANY,
  COLOUR_ANIM,
  COLOUR_CONST1,
  COLOUR_CONST2,
  COLOUR_SRV_DIFFUSE,
  COLOUR_SRV_NORMAL,
  COLOUR_FACTION,
  FLUSH_ENTRY,
  DEPTH_B_VF68,
  DEPTH_B_VF64,
  COLOUR_B_VF68,
  COLOUR_B_VF64,
  COLOUR_B_STATE,
  COUNT,
};

// What a colour B body's state block (the constants, textures and faction block it sets for an item) hands to Record, as the
// stack of its site (a STATE thunk builds it from the game's registers and locals, which the engine code decodes per variant):
// the tracker writes the block makes (done in both modes) and the deferred calls, in the block's order.
constexpr uint32_t STATE_SNOW = 1;
constexpr uint32_t STATE_DIFFUSE = 2;
constexpr uint32_t STATE_NORMAL = 4;
constexpr uint32_t STATE_FACTION = 8;
constexpr uint32_t STATE_WRITE_FLAGS = 1;
constexpr uint32_t STATE_WRITE_TEXTURES = 2;
constexpr uint32_t STATE_WRITE_FACTION = 4;
struct StateBlock {
  // STATE_* bits: the deferred calls.
  uint32_t parts;
  // STATE_WRITE_* bits: the tracker's +0x10 (the flag byte), +0x04 / +0x08 (the textures), +0x00 / +0x0c (the faction and its
  // mask's index).
  uint32_t writes;
  uint32_t flags;
  uint32_t snow_object;
  uint32_t snow_bits;
  uint32_t diffuse_slot;
  uint32_t diffuse;
  uint32_t normal_slot;
  uint32_t normal;
  int32_t faction;
  uint32_t mask;
  uint32_t node;
  uint32_t state;
};
static_assert(sizeof(StateBlock) == 52);

// A mesh class a B group's planner may record (the vtable of the mesh), with what it needs: the table of the 23 routed classes.
struct MeshClass {
  uint32_t vtable = 0;
  // The list's offset in the mesh: the list is at mesh + base + 32 * list type + 4.
  uint32_t base = 0;
  // The family's depth and colour bodies (thiscall, ecx = the mesh).
  uint32_t depth_body = 0;
  uint32_t colour_body = 0;
  uint32_t family = 0;
  // The class records in Depth B / Colour B (false: its meshes are direct segments).
  bool depth = false;
  bool colour = false;
};
constexpr uint32_t MAX_CLASSES = 32;

// The game's addresses, decoded from the sites the engine code found.
struct GameCalls {
  // The loops' entry points, by Group (the B groups call meshes' own bodies: 0).
  std::array<uint32_t, static_cast<size_t>(Group::COUNT)> loops = {};
  uint32_t flush = 0;
  uint32_t anim_call = 0;
  uint32_t dirty_call = 0;
  uint32_t view_call = 0;
  uint32_t srv_call = 0;
  uint32_t faction_call = 0;
  const uint8_t* cached_constants = nullptr;
  const float* faction_default = nullptr;
  std::array<uint32_t, 10> weighted_vtables = {};
  // The B groups: the sorted list's accessor (thiscall, ecx = the list, index: the address of the entry holding the mesh), the
  // virtual call that does nothing and returns 0 (a class whose call is this one records no op), the module's load address
  // minus the listings' 0x10000000, and the routed classes.
  uint32_t accessor = 0;
  uint32_t vf64_stub = 0;
  uint32_t module_shift = 0;
  std::array<MeshClass, MAX_CLASSES> classes = {};
  uint32_t class_count = 0;
  // False: a pattern did not match or an entry jump is in; the recording stands down whole.
  bool usable = false;
  // The loop groups (bits of Group) whose sites are all found, with the flush path they use: the others run directly.
  uint32_t usable_groups = ~0u;
};

#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
constexpr bool TEST_SWITCHES = true;
#else
constexpr bool TEST_SWITCHES = false;
#endif

namespace internal {

// The kinds of op (0: a site this build does not record; its chunk is run again), and what a site does in Record.
enum class OpKind : uint16_t { NONE, FLUSH, VF68, ANIM, CONSTANT, SRV, FACTION, VF64, STATE, COUNT };
enum class SiteKind : uint8_t { NONE, FLUSH, VF68, ANIM, SETUP, CONST1, CONST2, SRV, FACTION, VF64, STATE };

struct SiteInfo {
  Group group;
  SiteKind kind;
  // ANIM: the index's slot on the game's stack (stack[0] is the return address). SRV: the slot's offset from the state.
  uint32_t operand;
};
constexpr std::array<SiteInfo, static_cast<size_t>(Site::COUNT)> SITES = {{
    {Group::DEPTH_A, SiteKind::FLUSH, 0},
    {Group::DEPTH_A, SiteKind::FLUSH, 0},
    {Group::DEPTH_A, SiteKind::FLUSH, 0},
    {Group::DEPTH_A, SiteKind::VF68, 0},
    {Group::DEPTH_A, SiteKind::ANIM, 9},
    {Group::COLOUR_A, SiteKind::FLUSH, 0},
    {Group::COLOUR_A, SiteKind::FLUSH, 0},
    {Group::COLOUR_A, SiteKind::FLUSH, 0},
    {Group::COLOUR_A, SiteKind::FLUSH, 0},
    {Group::COLOUR_A, SiteKind::SETUP, 0},
    {Group::COLOUR_A, SiteKind::VF68, 0},
    {Group::COLOUR_A, SiteKind::VF68, 0},
    // The "any" byte is captured at the flush now: this site is not written.
    {Group::COLOUR_A, SiteKind::NONE, 0},
    {Group::COLOUR_A, SiteKind::ANIM, 11},
    {Group::COLOUR_A, SiteKind::CONST1, 0},
    {Group::COLOUR_A, SiteKind::CONST2, 0},
    {Group::COLOUR_A, SiteKind::SRV, 0x41f4},
    {Group::COLOUR_A, SiteKind::SRV, 0x421c},
    {Group::COLOUR_A, SiteKind::FACTION, 0},
    {Group::SHARED, SiteKind::FLUSH, 0},
    {Group::DEPTH_B, SiteKind::VF68, 0},
    {Group::DEPTH_B, SiteKind::VF64, 0},
    {Group::COLOUR_B, SiteKind::VF68, 0},
    {Group::COLOUR_B, SiteKind::VF64, 0},
    {Group::COLOUR_B, SiteKind::STATE, 0},
}};

static GameCalls game_calls;

// Whether a mesh's vtable is one of the 10 classes the census covered (an unfilled table knows none: every class is unknown).
inline bool WeightedClass(uint32_t vtable) {
  return vtable != 0 && std::ranges::find(game_calls.weighted_vtables, vtable) != game_calls.weighted_vtables.end();
}

// The colour loop's meshes: which chunk set each one up in the call being recorded. A mesh set up by two chunks would have its
// +0x40 / +0x44 written (and read back by the virtual calls) by two threads: the call then runs directly, before any replay.
// Entries are {mesh 32 bits | call 24 bits | chunk 8 bits}; an entry of an older call is free.
// Cost: each setup is one compare-exchange on a random line of the 64 KB table, which another worker owned last: about one
// cross-core transfer per setup (an estimate, 50 to 100 ns, not measured). If Colour A misses its time goal, look here first;
// the alternative is a mesh list per chunk, checked after the join.
constexpr uint32_t OWNER_ENTRIES = 8192;
constexpr uint32_t OWNER_PROBES = 64;
static std::array<std::atomic<uint64_t>, OWNER_ENTRIES> owners;
static_assert(std::atomic<uint64_t>::is_always_lock_free, "a locked fallback would serialise every claim");
static uint32_t owner_call = 0;

// Starts a call's registrations (render thread, before the wake).
inline void NextOwnerCall() {
  if (++owner_call >= (1u << 24)) {
    for (std::atomic<uint64_t>& entry : owners) {
      entry.store(0, std::memory_order_relaxed);
    }
    owner_call = 1;
  }
}

// False: the mesh is owned by another chunk in this call, or the table is too full to say.
inline bool ClaimMesh(uint32_t mesh, uint32_t chunk) noexcept {
  const uint64_t mine = (static_cast<uint64_t>(mesh) << 32) | (static_cast<uint64_t>(owner_call) << 8) | chunk;
  uint32_t index = (mesh * 2654435761u) >> 19;
  for (uint32_t probe = 0; probe < OWNER_PROBES; ++probe, index = (index + 1) % OWNER_ENTRIES) {
    uint64_t entry = owners[index].load(std::memory_order_relaxed);
    for (;;) {
      if (((entry >> 8) & 0xffffff) == owner_call) {
        if ((entry >> 32) == mesh) return (entry & 0xff) == chunk;
        break;
      }
      if (owners[index].compare_exchange_weak(entry, mine, std::memory_order_relaxed)) return true;
    }
  }
  return false;
}

// Payloads, in dwords. An op is a header (kind in the low 16 bits, the op's size in dwords, header included, in the high 16)
// and its payload.
constexpr uint32_t FLUSH_PAYLOAD = 14;  // mesh, 10 arguments, mesh +0x40, mesh +0x44, the tracker's +0x69 byte (any)
constexpr uint32_t VF68_PAYLOAD = 5;    // mesh, 2 arguments, mesh +0x40, mesh +0x44
constexpr uint32_t ANIM_PAYLOAD = 3;    // state, value, index
constexpr uint32_t CONST_PAYLOAD = 3;   // object, the float's bits, repeat count
constexpr uint32_t SRV_PAYLOAD = 2;     // slot, texture
constexpr uint32_t FACTION_PAYLOAD = 4; // faction, mask, node, state
constexpr uint32_t VF64_PAYLOAD = 6;    // mesh, 3 arguments, mesh +0x40, mesh +0x44
constexpr uint32_t STATE_PAYLOAD = sizeof(StateBlock) / 4;
// The block a CONST2 site sets, from the state.
constexpr uint32_t CONST2_BLOCK = 0x2d4;
// The most bytes the ops of one item take: a flush (an item flushes at most once: every flush zeroes the pending count, and
// the count grows only at the item's end, so a second flush of the same item has no instances and records nothing), the
// shared-state calls and an animation change; colour has two shared-state calls (the mesh's setup and its variation) and adds
// the two constants, the two textures and the faction block.
constexpr uint32_t DEPTH_OP_BYTES_PER_ITEM = 4 * ((1 + FLUSH_PAYLOAD) + (1 + VF68_PAYLOAD) + (1 + ANIM_PAYLOAD));
constexpr uint32_t COLOUR_OP_BYTES_PER_ITEM =
    4 * ((1 + FLUSH_PAYLOAD) + 2 * (1 + VF68_PAYLOAD) + (1 + ANIM_PAYLOAD) + 2 * (1 + CONST_PAYLOAD) + 2 * (1 + SRV_PAYLOAD)
         + (1 + FACTION_PAYLOAD));

struct CopyRange {
  uint32_t begin;
  uint32_t end;
};
// What replay copies from a replayed chunk's tracker to the real one, per group. Besides these: the stride at +0x50 only
// when the chunk wrote one (the driver reads it; the slot's is zeroed when it is filled and after the warm-up, so non-zero
// means "written here", and the last chunk to write wins as in the serial order), and +0x69 ORed in.
struct GroupInfo {
  uint32_t tracker_bytes;
  std::array<CopyRange, 4> copy_back;
  uint32_t op_bytes_per_item;
  // The first bytes of the tracker a warm-up seeds and replay checks (the colour loop carries its fields from item to item;
  // the animation index is checked in every group).
  uint32_t seed_bytes;
  // The group registers the meshes its chunks set up: one mesh set up by two chunks makes the call run directly.
  bool owns_meshes;
  // B groups: the ops a mesh's own end flush adds (a body ends with a flush of what it has pending) besides its items'.
  uint32_t op_bytes_per_mesh;
};
constexpr std::array<CopyRange, 4> SAME_COPY_BACK = {{{0x00, 0x18}, {0x58, 0x5c}, {0x68, 0x69}, {0x6c, 0x7c}}};
constexpr GroupInfo DEPTH_INFO = {.tracker_bytes = TRACKER_SIZE,
                                  .copy_back = SAME_COPY_BACK,
                                  .op_bytes_per_item = DEPTH_OP_BYTES_PER_ITEM,
                                  .seed_bytes = 0,
                                  .owns_meshes = false,
                                  .op_bytes_per_mesh = 0};
constexpr GroupInfo COLOUR_INFO = {.tracker_bytes = TRACKER_SIZE,
                                   .copy_back = SAME_COPY_BACK,
                                   .op_bytes_per_item = COLOUR_OP_BYTES_PER_ITEM,
                                   .seed_bytes = 0x14,
                                   .owns_meshes = true,
                                   .op_bytes_per_mesh = 0};
// The B groups' bodies have no animation call (no animation seed); their items' ops are a flush, the shared-state calls (the
// colour body's are the setup's and the variation's), the virtual call at a batch start, and the colour body's state block.
// Every B mesh ends with a flush; each mesh appears once in a round, so no group registers meshes.
constexpr GroupInfo DEPTH_B_INFO = {.tracker_bytes = TRACKER_SIZE,
                                    .copy_back = SAME_COPY_BACK,
                                    .op_bytes_per_item = 4 * ((1 + FLUSH_PAYLOAD) + (1 + VF68_PAYLOAD) + (1 + VF64_PAYLOAD)),
                                    .seed_bytes = 0x14,
                                    .owns_meshes = false,
                                    .op_bytes_per_mesh = 4 * (1 + FLUSH_PAYLOAD)};
constexpr GroupInfo COLOUR_B_INFO = {.tracker_bytes = TRACKER_SIZE,
                                     .copy_back = SAME_COPY_BACK,
                                     .op_bytes_per_item =
                                         4 * ((1 + FLUSH_PAYLOAD) + 2 * (1 + VF68_PAYLOAD) + (1 + VF64_PAYLOAD) + (1 + STATE_PAYLOAD)),
                                     .seed_bytes = 0x14,
                                     .owns_meshes = false,
                                     .op_bytes_per_mesh = 4 * (1 + FLUSH_PAYLOAD)};
constexpr std::array<GroupInfo, static_cast<size_t>(Group::COUNT)> GROUPS = {DEPTH_INFO, COLOUR_INFO, DEPTH_B_INFO, COLOUR_B_INFO,
                                                                              DEPTH_INFO};
constexpr uint32_t STRIDE_OFFSET = 0x50;
constexpr uint32_t ANY_OFFSET = 0x69;

// Test switches (compiled in only with TWATTILA_DRAW_RECORDING_CONTROLS): each turns one safeguard off, and the test checks
// that the run then differs.
struct Controls {
  bool validate_seeds = true;
  bool translate_pointers = true;
  bool guard_continuation = true;
  // Trace frames compare only in the last frame of a trace (the others are timed); the tests compare every call.
  bool compare_every_call = false;
  bool capture_any = true;
  bool owner_check = true;
  bool restore_snapshots = true;
  // The compare joins what earlier chunks left in the "any" byte to the flush of the compare run's it meets.
  bool pending_any_compare = true;
  // The chunks (bit mask) that "race": another thread's setup of the mesh lands before the chunk's VF68 snapshot.
  uint32_t race_chunks = 0;
  bool stride_non_zero = true;
  bool fail_thread_creation = false;
  // Chunk 0's first mesh is claimed at the call start (a continuation has no setup site to claim it).
  bool claim_first_mesh = true;
  // The replay of a constant op repeats it as often as the loop did.
  bool replay_repeats = true;
  // The warm-up puts back the mesh state it wrote.
  bool restore_warm_up = true;
  // A call or warm-up that continues the tracker's mesh (no setup site before it) needs a class the census covered.
  bool check_continuation_class = true;
  // B: a census chunk that follows a direct segment is replayed only when the real tracker is at a mesh end (+0x58 / +0x78 clear)
  // and holds the round's +0x64 (M2).
  bool entry_check = true;
  // B: the planner honours the classes' flags (a direct class never runs on a slot).
  bool class_flags = true;
  // A compare difference switches the group off for the rest of the run (S9b; the tests clear it).
  bool latch_groups = true;
  // B trace frames: the compare runs leave the round's first mesh (a straddler, continued with no setup) as it was before them.
  bool restore_straddler = true;
  // B: the entry check also compares the tracker's +0x18 .. +0x28 (what the bodies read into flush and virtual call arguments).
  bool entry_check_inputs = true;
  // B: the entry check's change mask (+0x78) conjunct alone.
  bool entry_check_mask = true;
  // A chunk's replay ends by joining its pending "any" flag to the real one (not copying it over what is there).
  bool any_join = true;
  // A dispatcher call made inside a round runs directly (S-2).
  bool nested_guard = true;
};
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
static Controls controls;
#else
constexpr Controls controls;
#endif

}  // namespace internal

// The thunk's call (thiscall `esi` = the tracker, which is the slot's first byte): appends the site's op in RECORD mode,
// keeps the slot's animation index and the tracker's +0x69 byte in both modes, and sets the results the loop expects (the
// shared-state virtual call returns 0).
inline void __fastcall Record(Slot* slot, Frame* frame) noexcept {
  using internal::OpKind;
  using internal::SiteKind;
  if (frame->site >= static_cast<uint32_t>(Site::COUNT)) {
    slot->flags |= SLOT_UNKNOWN_CLASS;
    return;
  }
  const internal::SiteInfo& site = internal::SITES[frame->site];
  if (site.group != Group::SHARED && static_cast<uint32_t>(site.group) != slot->group) {
    slot->flags |= SLOT_UNKNOWN_CLASS;
    return;
  }
  const uint32_t* stack = reinterpret_cast<const uint32_t*>(frame + 1);
  const bool recording = (slot->mode == MODE_RECORD);
  const auto* mesh = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(frame->ecx));
  // Returns the payload, or null when the buffer is full (the chunk is then run again).
  const auto append = [slot](OpKind kind, uint32_t payload_dwords) noexcept -> uint32_t* {
    const uint32_t total = payload_dwords + 1;
    if ((slot->flags & SLOT_OVERFLOW) != 0 || static_cast<size_t>(slot->ops_end - slot->ops_cursor) < total) {
      slot->flags |= SLOT_OVERFLOW;
      return nullptr;
    }
    slot->last_const = nullptr;
    uint32_t* op = slot->ops_cursor;
    op[0] = static_cast<uint32_t>(kind) | (total << 16);
    slot->ops_cursor += total;
    return op + 1;
  };
  switch (site.kind) {
    case SiteKind::FLUSH: {
      // A flush of no instances returns at once in the game and leaves *any alone: dropped (exact).
      if (stack[5] == 0) return;
      // The flush reads and clears *any (the tracker's +0x69); the copy's is taken here, in both modes, and carried in the
      // op: the replay ORs it into the real byte just before the flush.
      uint32_t any = 0;
      if (internal::controls.capture_any) {
        any = slot->tracker[internal::ANY_OFFSET];
        slot->tracker[internal::ANY_OFFSET] = 0;
      }
      if (!recording) return;
      uint32_t* payload = append(OpKind::FLUSH, internal::FLUSH_PAYLOAD);
      if (payload == nullptr) return;
      const uint32_t slot_address = reinterpret_cast<uint32_t>(slot);
      const uint32_t real_address = reinterpret_cast<uint32_t>(slot->real_tracker);
      const uint32_t tracker_bytes = internal::GROUPS[slot->group].tracker_bytes;
      payload[0] = frame->ecx;
      // The pointers into the tracker copy are the real tracker's for the replay.
      for (uint32_t i = 0; i < 10; ++i) {
        const uint32_t argument = stack[1 + i];
        const bool in_tracker = (internal::controls.translate_pointers && argument - slot_address < tracker_bytes);
        payload[1 + i] = (in_tracker ? real_address + (argument - slot_address) : argument);
      }
      std::memcpy(payload + 11, mesh + 0x40, 4);
      std::memcpy(payload + 12, mesh + 0x44, 4);
      payload[13] = any;
      return;
    }
    case SiteKind::VF68: {
      frame->eax = 0;
      if (!recording) return;
      if constexpr (TEST_SWITCHES) {
        // Test hook: another chunk's setup of this mesh lands before this chunk's snapshot.
        if (((internal::controls.race_chunks >> slot->chunk) & 1) != 0) {
          *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(frame->ecx) + 0x44) ^= 0x5a5a;
        }
      }
      uint32_t* payload = append(OpKind::VF68, internal::VF68_PAYLOAD);
      if (payload == nullptr) return;
      payload[0] = frame->ecx;
      payload[1] = stack[1];
      payload[2] = stack[2];
      std::memcpy(payload + 3, mesh + 0x40, 4);
      std::memcpy(payload + 4, mesh + 0x44, 4);
      return;
    }
    case SiteKind::ANIM: {
      const uint32_t index = stack[site.operand];
      slot->anim = static_cast<int32_t>(index);
      if (!recording) return;
      uint32_t* payload = append(OpKind::ANIM, internal::ANIM_PAYLOAD);
      if (payload == nullptr) return;
      payload[0] = frame->ecx;
      payload[1] = stack[1];
      payload[2] = index;
      return;
    }
    case SiteKind::SETUP: {
      // Before the loop reads or writes anything of the mesh (ebx), in every mode: a class the census did not cover makes
      // the chunk run again and the loop skip the mesh (eax = 1: no technique); a known one is registered to its chunk. The
      // known classes are the 10 vtables of the weighted mesh classes the census covered.
      frame->eax = 0;
      if (!internal::WeightedClass(*reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(frame->ebx)))) {
        slot->flags |= SLOT_UNKNOWN_CLASS;
        frame->eax = 1;
        return;
      }
      if (recording && slot->chunk != COMPARE_SLOT && internal::controls.owner_check && !internal::ClaimMesh(frame->ebx, slot->chunk)) {
        slot->flags |= SLOT_CONFLICT;
      }
      return;
    }
    case SiteKind::CONST1:
    case SiteKind::CONST2: {
      if (!recording) return;
      // CONST1 sets the block in ecx to 0.0f; CONST2 the block at the state + 0x2d4 to the float in xmm0. Equal constants in a
      // row make one op with a repeat count (the game writes each time when its cache flag is off).
      const bool zero = (site.kind == SiteKind::CONST1);
      const uint32_t object = (zero ? frame->ecx : stack[5] + internal::CONST2_BLOCK);
      const uint32_t bits = (zero ? 0 : frame->xmm0);
      if (slot->last_const != nullptr && slot->last_const[1] == object && slot->last_const[2] == bits) {
        ++slot->last_const[3];
        return;
      }
      uint32_t* payload = append(OpKind::CONSTANT, internal::CONST_PAYLOAD);
      if (payload == nullptr) return;
      payload[0] = object;
      payload[1] = bits;
      payload[2] = 1;
      slot->last_const = payload - 1;
      return;
    }
    case SiteKind::SRV: {
      if (!recording) return;
      uint32_t* payload = append(OpKind::SRV, internal::SRV_PAYLOAD);
      if (payload == nullptr) return;
      payload[0] = frame->ebx + site.operand;
      payload[1] = frame->ecx;
      return;
    }
    case SiteKind::FACTION: {
      // The two stores before this site are done (the faction into the tracker); the third, the mask's index, is this site's,
      // in both modes. The item is in edi.
      const uint32_t mask = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(frame->edi) + 0x54);
      const int32_t mask_index = (mask != 0 ? *reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(mask) + 4) : -1);
      std::memcpy(slot->tracker.data() + 0x0c, &mask_index, 4);
      if (!recording) return;
      uint32_t* payload = append(OpKind::FACTION, internal::FACTION_PAYLOAD);
      if (payload == nullptr) return;
      payload[0] = frame->ecx;
      payload[1] = mask;
      payload[2] = stack[0x28 / 4];
      payload[3] = stack[0x14 / 4];
      return;
    }
    case SiteKind::VF64: {
      // The class's virtual call at a batch's start: one that does nothing (the stub) records no op, in both modes; another is
      // deferred (it binds shader resources: on the render thread at replay, as today).
      frame->eax = 0;
      if (!recording || (*reinterpret_cast<const uint32_t* const*>(mesh))[0x64 / 4] == internal::game_calls.vf64_stub) return;
      uint32_t* payload = append(OpKind::VF64, internal::VF64_PAYLOAD);
      if (payload == nullptr) return;
      payload[0] = frame->ecx;
      payload[1] = stack[1];
      payload[2] = stack[2];
      payload[3] = stack[3];
      std::memcpy(payload + 4, mesh + 0x40, 4);
      std::memcpy(payload + 5, mesh + 0x44, 4);
      return;
    }
    case SiteKind::STATE: {
      // A colour body's state block: its tracker writes are done in both modes, its calls are deferred in the block's order.
      StateBlock block = {};
      std::memcpy(&block, stack, sizeof(block));
      if ((block.writes & STATE_WRITE_FLAGS) != 0) {
        slot->tracker[0x10] = static_cast<uint8_t>(block.flags);
      }
      if ((block.writes & STATE_WRITE_TEXTURES) != 0) {
        std::memcpy(slot->tracker.data() + 0x04, &block.diffuse, 4);
        std::memcpy(slot->tracker.data() + 0x08, &block.normal, 4);
      }
      if ((block.writes & STATE_WRITE_FACTION) != 0) {
        const int32_t mask_index = (block.mask != 0 ? *reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(block.mask) + 4) : -1);
        std::memcpy(slot->tracker.data() + 0x00, &block.faction, 4);
        std::memcpy(slot->tracker.data() + 0x0c, &mask_index, 4);
      }
      if (!recording) return;
      uint32_t* payload = append(OpKind::STATE, internal::STATE_PAYLOAD);
      if (payload == nullptr) return;
      std::memcpy(payload, &block, sizeof(block));
      return;
    }
    default:
      slot->flags |= SLOT_UNKNOWN_CLASS;
      return;
  }
}

namespace internal {

using DepthLoopFn = void(__thiscall*)(void* node, void* tracker, void* list, uint32_t begin, uint32_t end);
using ColourLoopFn = void(__thiscall*)(void* node, void* tracker, void* list, uint32_t unused, uint32_t begin, uint32_t end);
using FlushFn =
    void(__thiscall*)(void* mesh, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Vf68Fn = uint8_t(__thiscall*)(void* mesh, uint32_t, uint32_t);
using Vf64Fn = uint8_t(__thiscall*)(void* mesh, uint32_t, uint32_t, uint32_t);
// The B groups: a mesh's own slot-0x138 call, the bodies of its family, the sorted list's accessor and a view's type.
using MeshVf138Fn = void(__thiscall*)(void* mesh, void* tracker, void* device, void* view, uint32_t list_type, void* node);
using DepthBodyFn = void(__thiscall*)(void* mesh, void* tracker, void* list, uint32_t begin, uint32_t end);
using ColourBodyFn = void(__thiscall*)(void* mesh, void* tracker, void* list, void* node, uint32_t begin, uint32_t end);
using AccessorFn = uint32_t*(__thiscall*)(void* sorted, uint32_t index);
using ViewTypeFn = uint32_t(__thiscall*)(void* view);
using AnimCallFn = void(__thiscall*)(void* state, uint32_t value);
using DirtyFn = void(__thiscall*)(uint32_t object);
using ViewFn = uint32_t(__thiscall*)(uint32_t texture);
using SrvFn = void(__thiscall*)(uint32_t slot, uint32_t view);
using FactionFn = void(__thiscall*)(uint32_t state, const float* colours);
using FactionListFn = void(__thiscall*)(uint32_t node, int32_t faction, uint32_t* list);

// One call of a loop, as the dispatcher got it.
struct Call {
  Group group = Group::DEPTH_A;
  uint32_t loop = 0;
  void* node = nullptr;
  void* list = nullptr;
  uint32_t unused = 0;
};

// What the workers are woken to do.
enum class WorkKind : uint8_t { RECORD_LOOP, RECORD_MESHES, TOUCH };

// The B groups' round: the arguments the driver's loop gives every mesh's own slot-0x138 call.
struct MeshRound {
  void* device = nullptr;
  void* view = nullptr;
  uint32_t list_type = 0;
  void* node = nullptr;
  void* sorted = nullptr;
};

// The hand-off to the workers: written by the render thread before it wakes anyone.
struct alignas(64) Descriptor {
  void (*work)(const Descriptor&, uint32_t worker) = nullptr;
  WorkKind kind = WorkKind::RECORD_LOOP;
  // Workers woken for this dispatch.
  uint32_t workers = 0;
  uint32_t x87 = 0;
  uint32_t sse = 0;
  Call call;
  MeshRound meshes;
  uint32_t chunks = 0;
  bool one_worker = false;
  std::array<uint32_t, MAX_THREADS + 1> cuts = {};
  // B rounds: the threads (the render thread and workers) the chunks are dealt to (chunk c to thread c % participants; the render
  // thread is thread 0), and each chunk's range of planned meshes.
  uint32_t participants = 1;
  std::array<uint32_t, MAX_THREADS> mesh_begin = {};
  std::array<uint32_t, MAX_THREADS> mesh_end = {};
  // Touch-ahead: the range, and the stop generation the helpers poll.
  uint32_t touch_begin = 0;
  uint32_t touch_end = 0;
  uint32_t touch_generation = 0;
};

struct alignas(64) PoolWorker {
  std::atomic<uint32_t> ticket{0};
  HANDLE thread = nullptr;
};

// What the dispatcher reads on the render thread (Update writes it there too).
struct Config {
  Stage stage = Stage::OFF;
  uint32_t threads = 4;
  uint32_t loops = 1;
  uint32_t families = 0xff;
};

enum class CompareResult : uint8_t { NONE, EQUAL, SLOT_NOT_COMPARABLE, CHUNK_RERUN, DIFFERS, COMPARE_HAS_MORE };

constexpr uint32_t NO_FAMILY = 0xff;

// Phases of one call, in QPC ticks. `total` leaves out what only trace frames do (the compare run, the mesh run length scan,
// the warm-ups of Chunked), so that the sum of the calls of a trace frame is what the stage costs in a normal frame.
struct CallTrace {
  Group group = Group::DEPTH_A;
  // B calls: the family of the differing op's mesh (0 to 7; NO_FAMILY: no difference, or not a B call).
  uint32_t family = NO_FAMILY;
  // B rounds: census items and chunks per family (a chunk counts once under every family it holds).
  std::array<uint32_t, 8> family_items = {};
  std::array<uint32_t, 8> family_chunks = {};
  uint32_t items = 0;
  uint32_t threads = 0;
  uint32_t chunks = 0;
  std::array<uint32_t, MAX_THREADS> chunk_items = {};
  // The call ran the compare (its times are not a normal frame's: read timings only from calls without it).
  bool compared = false;
  Stage stage = Stage::OFF;
  const char* direct_reason = "";
  uint32_t cut_targets = 0;
  uint32_t cut_probes = 0;
  uint32_t cut_failures = 0;
  // Mesh run lengths: 1, 2-3, 4-7, ... 256+.
  std::array<uint32_t, 9> runs = {};
  std::array<uint32_t, static_cast<size_t>(OpKind::COUNT)> ops = {};
  uint32_t op_bytes = 0;
  uint32_t op_buffer_bytes = 0;
  uint32_t seed_hits = 0;
  uint32_t seed_misses = 0;
  // The misses by what differed (one miss counts under every field that differs): faction, decals, mask, flags, animation.
  std::array<uint32_t, 5> seed_miss_fields = {};
  // The sum of the repeat counts of the replayed constant ops.
  uint32_t constant_repeats = 0;
  uint32_t overflows = 0;
  uint32_t unknown_classes = 0;
  uint32_t conflicts = 0;
  uint32_t reruns = 0;
  // B rounds (the gates' numbers): meshes and census items in the round, the ones in direct segments, meshes that appear twice
  // (trace frames; target 0), chunks whose warm-ups found no seed, chunks run again by the entry check (M2). round_meshes: the
  // meshes the round lists (a round over MAX_ROUND_MESHES runs directly, planned as none).
  uint32_t round_meshes = 0;
  uint32_t meshes = 0;
  uint32_t direct_meshes = 0;
  uint32_t census_items = 0;
  uint32_t direct_items = 0;
  uint32_t duplicate_meshes = 0;
  uint32_t unknown_seeds = 0;
  uint32_t entry_reruns = 0;
  uint64_t direct = 0;
  uint64_t plan = 0;
  uint64_t cut = 0;
  uint64_t warm_ups = 0;
  uint64_t chunk_zero = 0;
  uint64_t join_wait = 0;
  uint64_t replay = 0;
  uint64_t rerun = 0;
  uint64_t total = 0;
  // Trace frames only.
  uint64_t compare = 0;
  uint64_t run_scan = 0;
  uint64_t wall = 0;
  CompareResult compare_result = CompareResult::NONE;
  // Where the compare's result comes from: the run (a B round has one per run of census chunks), the op index in that run, the
  // kind of the differing op, the chunk it was found in and the class (index in the game's table) of the op's mesh.
  uint32_t compare_run = 0;
  uint32_t compare_op = 0;
  uint32_t compare_kind = 0;
  uint32_t compare_chunk = 0;
  uint32_t compare_class = 0;
};

struct FrameTrace {
  uint64_t call_ticks = 0;
  bool thread_matched = true;
  // A call of the frame ran the compare (its times are not a normal frame's).
  bool compared = false;
};

// The seed fields a miss is counted under: four byte ranges of the tracker's first 0x14 bytes (the animation index is the
// fifth counter).
constexpr std::array<CopyRange, 4> SEED_FIELDS = {{{0x00, 0x04}, {0x04, 0x0c}, {0x0c, 0x10}, {0x10, 0x14}}};

// The one-frame trace: armed by RENODX_TW_DRAW_TRACE=<seconds after the first present>[,<frames>]. Render thread only.
struct Trace {
  bool armed = false;
  bool active = false;
  bool finished = false;
  bool read_environment = false;
  uint64_t arm_ticks = 0;
  uint32_t frames = 3;
  uint32_t frames_left = 0;
  uint64_t first_present = 0;
  uint64_t frame_call_ticks = 0;
  bool frame_compared = false;
  std::vector<CallTrace> calls;
  std::vector<FrameTrace> frame_list;
};

// What the last call did (the tests read it).
struct LastCall {
  uint32_t chunks = 0;
  uint32_t reruns = 0;
  uint32_t replayed = 0;
  uint32_t ops = 0;
  bool direct = false;
  uint32_t split_constants = 0;
  // B rounds: direct segments, chunks run again by the entry check, chunks with no seed.
  uint32_t direct_segments = 0;
  uint32_t entry_reruns = 0;
  uint32_t unknown_seeds = 0;
};

// Op memory is one address range, committed as a call needs it. The chunks of a call tile its items, so their buffers (the
// group's worst case bytes an item, plus the end flush) take consecutive parts of one buffer of the whole call; the compare run's
// follows. A part that does not fit makes that chunk overflow, which runs it again (exact, slower).
constexpr uint32_t OP_ARENA_BYTES = 32 * 1024 * 1024;
constexpr uint32_t OP_SLACK_BYTES = 128;
// Calls with fewer items run directly.
constexpr uint32_t MIN_ITEMS = 512;
// How far past its target a cut looks for a mesh change, and the shortest chunk.
constexpr uint32_t CUT_SEARCH = 256;
constexpr uint32_t MIN_CHUNK = 64;
constexpr uint32_t PREFETCH_AHEAD = 8;
constexpr uint32_t WORKER_STACK_BYTES = 512 * 1024;

static Config config;
static LastCall last_call;
static Trace trace;
static std::atomic<DWORD> present_thread = 0;
static std::atomic<bool> detached = false;
static bool allocation_failed = false;
// The stage and K a worker thread could not be created for (no retry until they change).
static uint32_t pool_failed_key = ~0u;
static bool kept_logged = false;
// The groups already logged as on but not usable in this game build (bits of Group).
static uint32_t unusable_logged = 0;
// The groups the engine code keeps its sites in (SiteGroups).
static uint32_t sticky_groups = 0;
static Shared* shared = nullptr;
static uint8_t* arena = nullptr;
static uint8_t* op_memory = nullptr;
static uint32_t op_committed = 0;
static HMODULE module_reference = nullptr;
// The most op bytes a chunk may use (tests lower it to force overflows).
static uint32_t op_buffer_limit = OP_ARENA_BYTES;
static uint32_t thread_attempts = 0;

static std::array<PoolWorker, MAX_WORKERS> pool;
static uint32_t worker_count = 0;
alignas(64) static std::atomic<uint32_t> done = 0;
static std::atomic<bool> stopping = false;
static std::atomic<uint32_t> touch_generation = 0;
alignas(64) static Descriptor descriptor;
// The generation of cpu_sets' published sets the pool threads were given (all ones: none yet).
static uint32_t given_generation = ~0u;

static uint64_t qpc_frequency = [] {
  LARGE_INTEGER frequency = {};
  QueryPerformanceFrequency(&frequency);
  return static_cast<uint64_t>(frequency.QuadPart);
}();

inline uint64_t Qpc() {
  LARGE_INTEGER counter = {};
  QueryPerformanceCounter(&counter);
  return static_cast<uint64_t>(counter.QuadPart);
}

inline Slot* SlotAt(uint32_t index) { return reinterpret_cast<Slot*>(arena + static_cast<size_t>(index) * SLOT_SIZE); }

inline void RunLoop(const Call& call, void* tracker, uint32_t begin, uint32_t end) {
  switch (call.group) {
    case Group::DEPTH_A:
      reinterpret_cast<DepthLoopFn>(static_cast<uintptr_t>(call.loop))(call.node, tracker, call.list, begin, end);
      break;
    case Group::COLOUR_A:
      reinterpret_cast<ColourLoopFn>(static_cast<uintptr_t>(call.loop))(call.node, tracker, call.list, call.unused, begin, end);
      break;
    default:
      // No-op: the other groups arrive with their loops; the dispatcher never gets here for them.
      break;
  }
}

// The planned meshes of the round, in list order (the planner reads each mesh once: its class and its slice), and the calls that
// run a mesh or a range of them: the mesh's own slot-0x138 call, what the game's loop makes.
struct PlannedMesh {
  uint32_t mesh;
  // The mesh's slice (items) and its class in the game's table (the table's size: none).
  uint32_t items;
  uint32_t class_index;
  bool census;
};
constexpr uint32_t MAX_ROUND_MESHES = 8192;
static std::array<PlannedMesh, MAX_ROUND_MESHES> planned;
// S3's sorted copy of the planned meshes' addresses (the render thread's only).
static std::array<uint32_t, MAX_ROUND_MESHES> duplicate_scan;

inline void RunMesh(const MeshRound& round, void* tracker, uint32_t mesh) noexcept {
  const auto vtable = *reinterpret_cast<const uint32_t* const*>(static_cast<uintptr_t>(mesh));
  reinterpret_cast<MeshVf138Fn>(static_cast<uintptr_t>(vtable[0x138 / 4]))(
      reinterpret_cast<void*>(static_cast<uintptr_t>(mesh)), tracker, round.device, round.view, round.list_type, round.node);
}

inline void RunMeshes(const MeshRound& round, void* tracker, uint32_t begin, uint32_t end) noexcept {
  for (uint32_t i = begin; i < end; ++i) {
    RunMesh(round, tracker, planned[i].mesh);
  }
}

// A worker records its chunks. Loops: worker w runs chunk w + 1, or, one worker alone, chunks 1 to n - 1 in order. Mesh rounds:
// chunk c belongs to thread c % participants (thread 0 is the render thread), so worker w runs chunks w + 1, w + 1 + P, ...
inline void RecordWork(const Descriptor& d, uint32_t worker) {
  if (d.kind == WorkKind::RECORD_MESHES) {
    for (uint32_t chunk = worker + 1; chunk < d.chunks; chunk += d.participants) {
      RunMeshes(d.meshes, SlotAt(chunk), d.mesh_begin[chunk], d.mesh_end[chunk]);
    }
    return;
  }
  const uint32_t first = (d.one_worker ? 1 : worker + 1);
  const uint32_t last = (d.one_worker ? d.chunks : worker + 2);
  for (uint32_t chunk = first; chunk < last; ++chunk) {
    RunLoop(d.call, SlotAt(chunk), d.cuts[chunk], d.cuts[chunk + 1]);
  }
}

// The replays of the shared-state ops, on the render thread in the game's order. The constants: `*t = value` and the block's
// dirty call, unless the game's cache flag is on and the cell already equals the value (NaN never equals, -0 equals 0), once
// per repeat.
inline void ReplayConst(uint32_t object, uint32_t bits, uint32_t repeat) {
  float value = 0;
  std::memcpy(&value, &bits, 4);
  for (uint32_t i = 0; i < (controls.replay_repeats ? repeat : 1); ++i) {
    auto* const cell = *reinterpret_cast<float**>(static_cast<uintptr_t>(object) + 0x14);
    if (*game_calls.cached_constants == 0 || !(*cell == value)) {
      std::memcpy(cell, &bits, 4);
      reinterpret_cast<DirtyFn>(static_cast<uintptr_t>(game_calls.dirty_call))(object);
    }
  }
}

inline void ReplaySrv(uint32_t slot, uint32_t texture) {
  reinterpret_cast<SrvFn>(static_cast<uintptr_t>(game_calls.srv_call))(
      slot, reinterpret_cast<ViewFn>(static_cast<uintptr_t>(game_calls.view_call))(texture));
}

// The faction block: the four colours of a faction (the default one without a faction), picked by the item's mask when it has
// one; sent when there are 4 or more. Only the first four colours are read by the call, so only those are gathered.
inline void ReplayFaction(int32_t faction, uint32_t mask, uint32_t node, uint32_t state) {
  const auto faction_fn = reinterpret_cast<FactionFn>(static_cast<uintptr_t>(game_calls.faction_call));
  std::array<float, 12> colours = {};
  if (faction == -1) {
    for (uint32_t i = 0; i < 4; ++i) {
      colours[3 * i] = *game_calls.faction_default;
      colours[3 * i + 1] = *game_calls.faction_default;
      colours[3 * i + 2] = 1.0f;
    }
    faction_fn(state, colours.data());
    return;
  }
  uint32_t list = 0;
  const auto* const node_vtable = *reinterpret_cast<const uint32_t* const*>(static_cast<uintptr_t>(node));
  reinterpret_cast<FactionListFn>(static_cast<uintptr_t>(node_vtable[0xc0 / 4]))(node, faction, &list);
  const auto* const list_bytes = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(list));
  uint32_t count = 0;
  uint32_t data = 0;
  std::memcpy(&count, list_bytes + 4, 4);
  std::memcpy(&data, list_bytes + 8, 4);
  if (count < 4) return;
  if (mask == 0) {
    faction_fn(state, reinterpret_cast<const float*>(static_cast<uintptr_t>(data)));
    return;
  }
  // The game gathers the list's `count` entries into a vector and the call reads the first four (48 bytes): gathered here.
  const auto* const entries = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(data));
  const auto* const picks = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(mask));
  for (uint32_t i = 0; i < 4; ++i) {
    std::memcpy(&colours[3 * i], entries + 12 * picks[i], 12);
  }
  faction_fn(state, colours.data());
}

// Whether a chunk's op is the compare run's. A flush compares its mesh, its ten arguments and its any byte (the compare run's
// carries what earlier chunks left pending), not the mesh snapshot: the flush writes +0x40 itself before it reads it and never
// reads +0x44. The other ops compare whole, snapshots included (the virtual call reads +0x44).
inline bool OpsEqual(const uint32_t* chunk_op, const uint32_t* whole_op, uint32_t pending_any) noexcept {
  if (chunk_op[0] != whole_op[0]) return false;
  if (static_cast<OpKind>(chunk_op[0] & 0xffff) != OpKind::FLUSH) return std::memcmp(chunk_op, whole_op, (chunk_op[0] >> 16) * 4) == 0;
  return std::memcmp(chunk_op, whole_op, 12 * 4) == 0 && whole_op[14] == (chunk_op[14] | pending_any);
}

// Touch-ahead: helper h of H reads blocks of 16 items from begin + 64 + 16 h on, stride 16 H, until the end or the stop.
// Reads only; the loop itself runs unchanged on the render thread.
inline void TouchWork(const Descriptor& d, uint32_t worker) {
  const uint32_t* data = *reinterpret_cast<uint32_t* const*>(static_cast<uint8_t*>(d.call.list) + 8);
  uint32_t sink = 0;
  for (uint32_t block = d.touch_begin + 64 + 16 * worker; block < d.touch_end; block += 16 * d.workers) {
    if (touch_generation.load(std::memory_order_relaxed) != d.touch_generation) break;
    for (uint32_t i = block; i < std::min(block + 16, d.touch_end); ++i) {
      const auto* item = reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(data[i]));
      sink += item[0x00 / 4] + item[0x3c / 4] + item[0x5c / 4] + item[0xec / 4];
      const auto* mesh = reinterpret_cast<const volatile uint32_t*>(static_cast<uintptr_t>(item[0]));
      sink += mesh[0x00 / 4] + mesh[0x40 / 4] + mesh[0x44 / 4];
    }
  }
  static volatile uint32_t touched = 0;
  touched = touched + sink;
}

// A worker: spins about 3 us for its next ticket, then waits on it; takes the dispatching thread's x87 and SSE control
// words every dispatch (game code on a worker may have changed its own and not restored it); runs the descriptor's work;
// the last one to finish wakes the join.
static DWORD WINAPI WorkerMain(void* context) {
  PoolWorker& self = pool[reinterpret_cast<uintptr_t>(context)];
  const uint32_t index = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context));
  const uint64_t spin_ticks = std::max<uint64_t>(1, qpc_frequency * 3 / 1000000);
  uint32_t seen = 0;
  for (;;) {
    uint32_t ticket = self.ticket.load(std::memory_order_acquire);
    if (ticket == seen) {
      const uint64_t start = Qpc();
      while (ticket == seen && Qpc() - start < spin_ticks) {
        _mm_pause();
        ticket = self.ticket.load(std::memory_order_acquire);
      }
      while (ticket == seen) {
        self.ticket.wait(seen);
        ticket = self.ticket.load(std::memory_order_acquire);
      }
    }
    seen = ticket;
    if (stopping.load(std::memory_order_acquire)) return 0;
    const Descriptor& d = descriptor;
    unsigned int ignored = 0;
    __control87_2(d.x87, _MCW_EM | _MCW_RC | _MCW_PC | _MCW_IC, &ignored, nullptr);
    __control87_2(d.sse, _MCW_EM | _MCW_RC | _MCW_DN, nullptr, &ignored);
    const uint32_t woken = d.workers;
    d.work(d, index);
    if (done.fetch_add(1, std::memory_order_release) + 1 == woken) {
      done.notify_one();
    }
  }
}

// Render thread: wakes the first `count` workers for the filled descriptor, runs `render_part`, and joins (a short spin,
// then a wait; no timeout: a worker can't be stopped inside the game's code). `join_ticks` (optional) gets the join wait.
template <typename Part>
inline void RunOnWorkers(uint32_t count, Part&& render_part, uint64_t* join_ticks) {
  descriptor.workers = count;
  __control87_2(0, 0, &descriptor.x87, &descriptor.sse);
  done.store(0, std::memory_order_relaxed);
  for (uint32_t w = 0; w < count; ++w) {
    pool[w].ticket.fetch_add(1, std::memory_order_release);
    pool[w].ticket.notify_one();
  }
  render_part();
  const uint64_t join_start = Qpc();
  const uint64_t spin_ticks = qpc_frequency * 2 / 1000;
  while (done.load(std::memory_order_acquire) != count && Qpc() - join_start < spin_ticks) {
    _mm_pause();
  }
  for (uint32_t finished = done.load(std::memory_order_acquire); finished != count; finished = done.load(std::memory_order_acquire)) {
    done.wait(finished);
  }
  if (join_ticks != nullptr) {
    *join_ticks += Qpc() - join_start;
  }
}

// Ends the workers and joins them (Update with the row off, Uninstall; never DllMain).
inline void StopPool() {
  if (worker_count == 0) return;
  stopping.store(true, std::memory_order_release);
  std::array<HANDLE, MAX_WORKERS> handles = {};
  for (uint32_t w = 0; w < worker_count; ++w) {
    handles[w] = pool[w].thread;
    pool[w].ticket.fetch_add(1, std::memory_order_release);
    pool[w].ticket.notify_one();
  }
  WaitForMultipleObjects(worker_count, handles.data(), TRUE, INFINITE);
  for (uint32_t w = 0; w < worker_count; ++w) {
    CloseHandle(handles[w]);
    pool[w].thread = nullptr;
  }
  worker_count = 0;
  given_generation = ~0u;
}

// The next slot of a call: a copy of the real tracker (the stride and the pending "any" flag are the chunk's own from here on)
// and an empty op buffer of `wanted_bytes` plus the end flush's slack, taken from the call's op memory at *op_used (committed as
// needed). A buffer that does not fit is short: the chunk overflows and is run again (exact, slower).
inline Slot* PrepareSlot(uint32_t index, uint32_t group_index, void* tracker, uint32_t wanted_bytes, uint32_t* op_used) {
  Slot* slot = SlotAt(index);
  std::memcpy(slot->tracker.data(), tracker, GROUPS[group_index].tracker_bytes);
  std::memset(slot->tracker.data() + STRIDE_OFFSET, 0, 4);
  slot->tracker[ANY_OFFSET] = 0;
  slot->mode = MODE_RECORD;
  slot->anim = -1;
  uint32_t bytes = std::min({wanted_bytes + OP_SLACK_BYTES, op_buffer_limit, OP_ARENA_BYTES - *op_used}) & ~3u;
  if (*op_used + bytes > op_committed) {
    const uint32_t target = (*op_used + bytes + 4095) & ~4095u;
    if (VirtualAlloc(op_memory + op_committed, target - op_committed, MEM_COMMIT, PAGE_READWRITE) != nullptr) {
      op_committed = target;
    } else {
      bytes = (op_committed - *op_used) & ~3u;
    }
  }
  slot->ops_begin = reinterpret_cast<uint32_t*>(op_memory + *op_used);
  *op_used += bytes;
  slot->ops_cursor = slot->ops_begin;
  slot->ops_end = slot->ops_begin + bytes / 4;
  slot->flags = 0;
  slot->last_const = nullptr;
  slot->group = group_index;
  slot->chunk = index;
  slot->real_tracker = static_cast<uint8_t*>(tracker);
  return slot;
}

// A colour body's state block, replayed: its calls in the block's order.
inline void ReplayState(const StateBlock& block) {
  if ((block.parts & STATE_SNOW) != 0) ReplayConst(block.snow_object, block.snow_bits, 1);
  if ((block.parts & STATE_DIFFUSE) != 0) ReplaySrv(block.diffuse_slot, block.diffuse);
  if ((block.parts & STATE_NORMAL) != 0) ReplaySrv(block.normal_slot, block.normal);
  if ((block.parts & STATE_FACTION) != 0) ReplayFaction(block.faction, block.mask, block.node, block.state);
}

// The four loop groups' names, as the rows call them (the logs).
constexpr std::array<const char*, 4> GROUP_NAMES = {"Depth A", "Colour A", "Depth B", "Colour B"};

static bool bad_ops_logged = false;

// N-1: the op walks step by the header's length, so a buffer whose headers do not tile it up to the cursor (a zero header
// would never end) is never walked: its call runs directly. Logged once.
inline bool OpsWellFormed(const Slot* slot) {
  for (const uint32_t* op = slot->ops_begin; op < slot->ops_cursor; op += (*op >> 16)) {
    if ((*op >> 16) == 0 || (*op >> 16) > static_cast<size_t>(slot->ops_cursor - op)) {
      if (!bad_ops_logged) {
        renodx::utils::log::w("[Total War] Threaded Draw Recording: a recorded op buffer is damaged; its calls run directly.");
      }
      bad_ops_logged = true;
      return false;
    }
  }
  return true;
}

// The replay of a call, on the render thread, in the game's order: what its chunks' ops need.
struct Replay {
  uint8_t* real;
  uint32_t group_index;
  // The animation index the last replayed animation op set (the groups with animation seeds).
  int32_t last_anim;
  uint32_t op_count;
  CallTrace* trace_call;
};

// Replays one chunk's ops on the real tracker and copies its tracker back.
inline void ReplayChunk(Replay* replay, const Slot* slot) {
  uint8_t* const real = replay->real;
  // (A zero header ends the walk: damage made during the replay gives a wrong frame, not a freeze.)
  for (const uint32_t* op = slot->ops_begin; op < slot->ops_cursor && (*op >> 16) != 0; op += (*op >> 16)) {
    const uint32_t* payload = op + 1;
    const auto kind = static_cast<OpKind>(*op & 0xffff);
    ++replay->op_count;
    if (replay->trace_call != nullptr) {
      replay->trace_call->ops[std::min<size_t>(static_cast<size_t>(kind), replay->trace_call->ops.size() - 1)] += 1;
      replay->trace_call->op_bytes += (*op >> 16) * 4;
      replay->trace_call->constant_repeats += (kind == OpKind::CONSTANT ? payload[2] : 0);
    }
    switch (kind) {
      case OpKind::FLUSH: {
        auto* const mesh = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(payload[0]));
        if (controls.restore_snapshots) {
          std::memcpy(mesh + 0x40, payload + 11, 4);
          std::memcpy(mesh + 0x44, payload + 12, 4);
        }
        real[ANY_OFFSET] |= static_cast<uint8_t>(payload[13]);
        reinterpret_cast<FlushFn>(static_cast<uintptr_t>(game_calls.flush))(mesh, payload[1], payload[2], payload[3], payload[4],
                                                                             payload[5], payload[6], payload[7], payload[8],
                                                                             payload[9], payload[10]);
        break;
      }
      case OpKind::VF68: {
        auto* const mesh = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(payload[0]));
        if (controls.restore_snapshots) {
          std::memcpy(mesh + 0x40, payload + 3, 4);
          std::memcpy(mesh + 0x44, payload + 4, 4);
        }
        real[ANY_OFFSET] |= reinterpret_cast<Vf68Fn>(static_cast<uintptr_t>((*reinterpret_cast<const uint32_t* const*>(mesh))[0x68 / 4]))(
            mesh, payload[1], payload[2]);
        break;
      }
      case OpKind::VF64: {
        auto* const mesh = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(payload[0]));
        if (controls.restore_snapshots) {
          std::memcpy(mesh + 0x40, payload + 4, 4);
          std::memcpy(mesh + 0x44, payload + 5, 4);
        }
        real[ANY_OFFSET] |= reinterpret_cast<Vf64Fn>(static_cast<uintptr_t>((*reinterpret_cast<const uint32_t* const*>(mesh))[0x64 / 4]))(
            mesh, payload[1], payload[2], payload[3]);
        break;
      }
      case OpKind::ANIM:
        reinterpret_cast<AnimCallFn>(static_cast<uintptr_t>(game_calls.anim_call))(
            reinterpret_cast<void*>(static_cast<uintptr_t>(payload[0])), payload[1]);
        replay->last_anim = static_cast<int32_t>(payload[2]);
        break;
      case OpKind::CONSTANT:
        ReplayConst(payload[0], payload[1], payload[2]);
        break;
      case OpKind::SRV:
        ReplaySrv(payload[0], payload[1]);
        break;
      case OpKind::FACTION:
        ReplayFaction(static_cast<int32_t>(payload[0]), payload[1], payload[2], payload[3]);
        break;
      case OpKind::STATE:
        ReplayState(*reinterpret_cast<const StateBlock*>(payload));
        break;
      default:
        break;
    }
  }
  for (const auto& [from, to] : GROUPS[replay->group_index].copy_back) {
    std::memcpy(real + from, slot->tracker.data() + from, to - from);
  }
  // The stride only when this chunk wrote one (the slot's was zeroed when it was filled and after the warm-up); the pending
  // "any" flag joins the real one.
  uint32_t stride = 0;
  std::memcpy(&stride, slot->tracker.data() + STRIDE_OFFSET, 4);
  if (stride != 0 || !controls.stride_non_zero) {
    std::memcpy(real + STRIDE_OFFSET, &stride, 4);
  }
  real[ANY_OFFSET] = slot->tracker[ANY_OFFSET] | (controls.any_join ? real[ANY_OFFSET] : 0);
}

// The compare run's stream and how far the chunks' ops have matched it (trace frames).
struct CompareCursor {
  const uint32_t* cursor = nullptr;
  const uint32_t* end = nullptr;
  CompareResult result = CompareResult::NONE;
  uint32_t compared_ops = 0;
  uint32_t kind = 0;
  // The chunk the first difference was found in, and the mesh of the differing op (the latest op of the chunk that carries one, up
  // to the differing op itself; 0: none).
  uint32_t chunk = 0;
  uint32_t mesh = 0;
  // The compare run merges equal constants that a chunk end splits: the repeats of its current constant op already matched.
  uint32_t used = 0;
  // What the chunks before left in +0x69 without a flush after it: the next flush of the compare run carries it.
  uint32_t pending_any = 0;
  // Chunk ends that fell inside one of the compare run's constant ops (the tests count them).
  uint32_t split_constants = 0;
};

inline CompareCursor MakeCompare(const Slot* compare_slot) {
  return {.cursor = compare_slot->ops_begin,
          .end = compare_slot->ops_cursor,
          .result = ((compare_slot->flags & (SLOT_OVERFLOW | SLOT_UNKNOWN_CLASS)) != 0 || !OpsWellFormed(compare_slot)
                         ? CompareResult::SLOT_NOT_COMPARABLE
                         : CompareResult::EQUAL)};
}

// This chunk's stream must be the compare run's next ops.
inline void CompareChunk(CompareCursor* compare, const Slot* slot, uint32_t chunk) {
  uint32_t mesh = 0;
  for (const uint32_t* op = slot->ops_begin; op < slot->ops_cursor && compare->result == CompareResult::EQUAL; op += (*op >> 16)) {
    const auto kind = static_cast<OpKind>(*op & 0xffff);
    mesh = (kind == OpKind::FLUSH || kind == OpKind::VF68 || kind == OpKind::VF64 ? op[1] : mesh);
    bool same = (compare->cursor < compare->end);
    if (same && kind == OpKind::CONSTANT) {
      same = (static_cast<OpKind>(*compare->cursor & 0xffff) == OpKind::CONSTANT && compare->cursor[1] == op[1]
              && compare->cursor[2] == op[2] && compare->cursor[3] - compare->used >= op[3]);
      if (same) {
        compare->used += op[3];
        if (compare->used == compare->cursor[3]) {
          compare->cursor += 1 + CONST_PAYLOAD;
          compare->used = 0;
        }
      }
    } else {
      same = (same && compare->used == 0 && OpsEqual(op, compare->cursor, (controls.pending_any_compare ? compare->pending_any : 0)));
      if (same) {
        compare->pending_any = (kind == OpKind::FLUSH ? 0 : compare->pending_any);
        compare->cursor += *compare->cursor >> 16;
      }
    }
    if (same) {
      ++compare->compared_ops;
    } else {
      compare->result = CompareResult::DIFFERS;
      compare->kind = *op & 0xffff;
      compare->chunk = chunk;
      compare->mesh = mesh;
    }
  }
  compare->pending_any |= slot->tracker[ANY_OFFSET];
  compare->split_constants += (compare->used != 0 ? 1 : 0);
}

// After the last chunk: the compare run must have nothing left.
inline void CompareEnd(CompareCursor* compare) {
  if (compare->result == CompareResult::EQUAL && (compare->cursor != compare->end || compare->used != 0)) {
    compare->result = CompareResult::COMPARE_HAS_MORE;
  }
}

// The groups a compare difference switched off for the rest of the run (bits of Group; render thread).
static uint32_t latched_groups = 0;

// S9b: a compare that differs latches its group off (the dispatcher runs it directly from the next call) and logs once.
inline void LatchOnDifference(Group group, CompareResult result) {
  if (!controls.latch_groups || (result != CompareResult::DIFFERS && result != CompareResult::COMPARE_HAS_MORE)) return;
  const uint32_t bit = 1u << static_cast<uint32_t>(group);
  if ((latched_groups & bit) == 0) {
    renodx::utils::log::w(std::format(
        "[Total War] Threaded Draw Recording: the compare of group {} differs: the group is switched off for this run.",
        GROUP_NAMES[static_cast<uint32_t>(group) % GROUP_NAMES.size()]));
  }
  latched_groups |= bit;
}

// How a round's compare runs combine: the worst one names the result (a difference first, then a compare run with ops left, a
// chunk run again, a compare slot that could not be compared), by CompareResult.
constexpr std::array<uint8_t, 6> COMPARE_SEVERITY = {0, 0, 1, 2, 4, 3};
constexpr uint8_t Severity(CompareResult result) { return COMPARE_SEVERITY[static_cast<size_t>(result)]; }
static_assert(COMPARE_SEVERITY.size() == static_cast<size_t>(CompareResult::COMPARE_HAS_MORE) + 1
                  && Severity(CompareResult::NONE) == Severity(CompareResult::EQUAL)
                  && Severity(CompareResult::EQUAL) < Severity(CompareResult::SLOT_NOT_COMPARABLE)
                  && Severity(CompareResult::SLOT_NOT_COMPARABLE) < Severity(CompareResult::CHUNK_RERUN)
                  && Severity(CompareResult::CHUNK_RERUN) < Severity(CompareResult::COMPARE_HAS_MORE)
                  && Severity(CompareResult::COMPARE_HAS_MORE) < Severity(CompareResult::DIFFERS),
              "COMPARE_SEVERITY follows the CompareResult order");

// S-2: set on the render thread from the moment a round's plan stands (or a loop's chunks are cut) to the end of the dispatch. A
// dispatcher call made inside it (a direct segment's mesh, a direct class's own call into the dispatcher) would take the slots,
// the planned meshes and the descriptor the round is using: it runs directly ("nested").
static bool in_round = false;
// The nested calls that ran directly (the tests count them).
static uint32_t nested_calls = 0;

struct RoundScope {
  RoundScope() noexcept { in_round = true; }
  ~RoundScope() noexcept { in_round = false; }
  RoundScope(const RoundScope&) = delete;
  RoundScope& operator=(const RoundScope&) = delete;
};

// True inside a round on the render thread (the flag is read there only).
inline bool InRound() noexcept {
  return controls.nested_guard && GetCurrentThreadId() == present_thread.load(std::memory_order_relaxed) && in_round;
}

// What a warm-up left must be what the real tracker holds when the chunk starts.
inline bool SeedMatches(const Slot* slot, const void* real_tracker, int32_t last_anim, uint32_t group_index) {
  return slot->seed_anim == last_anim && std::memcmp(slot->seed_fields.data(), real_tracker, GROUPS[group_index].seed_bytes) == 0;
}

// Trace frames: counts a seed hit, or a miss under every field that differs.
inline void NoteSeed(CallTrace* trace_call, const Slot* slot, const void* real_tracker, int32_t last_anim, uint32_t group_index) {
  if (SeedMatches(slot, real_tracker, last_anim, group_index)) {
    ++trace_call->seed_hits;
    return;
  }
  ++trace_call->seed_misses;
  for (size_t field = 0; field < SEED_FIELDS.size(); ++field) {
    const auto [from, to] = SEED_FIELDS[field];
    trace_call->seed_miss_fields[field] += static_cast<uint32_t>(
        to <= GROUPS[group_index].seed_bytes
        && std::memcmp(slot->seed_fields.data() + from, static_cast<const uint8_t*>(real_tracker) + from, to - from) != 0);
  }
  trace_call->seed_miss_fields[SEED_FIELDS.size()] += static_cast<uint32_t>(slot->seed_anim != last_anim);
}

// The shared implementation of the loop dispatchers (one per group).
inline void Dispatch(Group group, void* node, void* tracker, void* list, uint32_t unused, uint32_t begin, uint32_t end) {
  const Config settings = config;
  const uint32_t group_index = static_cast<uint32_t>(group);
  const Call call = {.group = group, .loop = game_calls.loops[group_index], .node = node, .list = list, .unused = unused};
  const uint32_t items = (end > begin ? end - begin : 0);
  const bool nested = InRound();
  // Trace-only (noexcept: an allocation failure here ends the process; Update reserves room when the trace arms). A nested call
  // adds no entry: the round it is in holds a pointer to its own.
  CallTrace* const trace_call = (trace.active && !nested ? &trace.calls.emplace_back() : nullptr);
  const bool compare_now = (trace_call != nullptr && (controls.compare_every_call || trace.frames_left == 1));
  uint64_t last_lap = (trace_call != nullptr ? Qpc() : 0);
  const uint64_t call_start = last_lap;
  // Work only trace frames do: left out of the call's total.
  uint64_t trace_only_ticks = 0;
  // Adds the time since the last lap to a phase of the trace.
  const auto lap = [&](uint64_t CallTrace::* phase, bool trace_only = false) {
    if (trace_call == nullptr) return;
    const uint64_t now = Qpc();
    trace_call->*phase += now - last_lap;
    if (trace_only) {
      trace_only_ticks += now - last_lap;
    }
    last_lap = now;
  };
  if (trace_call != nullptr) {
    trace_call->group = group;
    trace_call->items = items;
    trace_call->threads = settings.threads;
    trace_call->stage = settings.stage;
    trace_call->compared = compare_now;
    trace.frame_compared = trace.frame_compared || compare_now;
  }
  const uint32_t* data = (items != 0 ? *reinterpret_cast<uint32_t* const*>(static_cast<uint8_t*>(list) + 8) : nullptr);
  const auto mesh_of = [data](uint32_t i) { return *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(data[i])); };
  // A dword of item i's mesh (only meshes of this list are read: a stale pointer of the tracker may be freed).
  const auto mesh_field = [&](uint32_t i, uint32_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(mesh_of(i))) + offset, 4);
    return value;
  };
  const auto finish = [&](const char* direct_reason) {
    if (trace_call != nullptr) {
      if (items != 0) {
        // Mesh run lengths over the whole call: trace-only, after everything else the call does, so it warms nothing.
        uint32_t run = 1;
        for (uint32_t i = begin + 1; i <= end; ++i) {
          if (i < end && mesh_of(i) == mesh_of(i - 1)) {
            ++run;
            continue;
          }
          ++trace_call->runs[std::min<uint32_t>(std::bit_width(run) - 1, trace_call->runs.size() - 1)];
          run = 1;
        }
      }
      lap(&CallTrace::run_scan, true);
      trace_call->direct_reason = (direct_reason != nullptr ? direct_reason : "");
      trace_call->wall = Qpc() - call_start;
      trace_call->total = trace_call->wall - trace_only_ticks;
      trace.frame_call_ticks += trace_call->total;
    }
  };
  if constexpr (TEST_SWITCHES) {
    last_call = {.direct = true};
  }

  // The workers this stage wakes.
  uint32_t helpers = 0;
  switch (settings.stage) {
    case Stage::TOUCH_AHEAD:
    case Stage::WORKERS:
      helpers = settings.threads - 1;
      break;
    case Stage::ONE_WORKER:
      helpers = 1;
      break;
    default:
      break;
  }
  const auto read32 = [](const void* base, uint32_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, static_cast<const uint8_t*>(base) + offset, 4);
    return value;
  };
  const char* direct_reason = nullptr;
  if (!game_calls.usable || ((game_calls.usable_groups >> group_index) & 1) == 0) {
    direct_reason = "not usable";
  } else if (detached.load(std::memory_order_relaxed)) {
    direct_reason = "detached";
  } else if (settings.stage <= Stage::DIRECT) {
    direct_reason = "stage";
  } else if (nested) {
    direct_reason = "nested";
    if constexpr (TEST_SWITCHES) ++nested_calls;
  } else if (shared->arena_size == 0) {
    // Not one site of the thunks is in (engine_code zeroes it when the last comes out): the thunks only run directly.
    direct_reason = "sites out";
  } else if (((settings.loops >> group_index) & 1) == 0) {
    direct_reason = "loop off";
  } else if (((latched_groups >> group_index) & 1) != 0) {
    direct_reason = "latched off";
  } else if (GetCurrentThreadId() != present_thread.load(std::memory_order_relaxed)) {
    direct_reason = "not the present thread";
  } else if (items < MIN_ITEMS) {
    direct_reason = "small call";
  } else if (worker_count < helpers) {
    direct_reason = "pool missing";
  } else if (read32(tracker, 0x58) != 0 || read32(tracker, 0x78) != 0) {
    // The chunks start with no pending instances and no change mask, as a call the loop itself ended does.
    direct_reason = "entry state";
  } else if (GROUPS[group_index].owns_meshes && controls.check_continuation_class && settings.stage >= Stage::CHUNKED
             && mesh_of(begin) == read32(tracker, 0x70) && !WeightedClass(mesh_field(begin, 0))) {
    // The call continues a mesh of a class the census did not cover: no setup site precedes its first item to stop it, so
    // its virtual calls would run while recording (and again in the compare run and in warm-ups).
    direct_reason = "unknown class";
  } else if (controls.guard_continuation && settings.stage >= Stage::CHUNKED && mesh_of(begin) == read32(tracker, 0x70)
             && read32(tracker, 0x14) == 0 && (mesh_field(begin, 0x40) != read32(tracker, 0x20) || mesh_field(begin, 0x44) != 0)) {
    // The call continues a mesh whose state is not what this tracker's setup would write (another view's, the colour
    // loop's): no setup precedes its first flush, so a snapshot taken while a chunk sets the mesh up would differ.
    direct_reason = "continuation";
  }
  if (direct_reason != nullptr) {
    RunLoop(call, tracker, begin, end);
    lap(&CallTrace::direct);
    finish(direct_reason);
    return;
  }
  const RoundScope round_scope;

  if (settings.stage == Stage::TOUCH_AHEAD) {
    descriptor.work = TouchWork;
    descriptor.kind = WorkKind::TOUCH;
    descriptor.call = call;
    descriptor.touch_begin = begin;
    descriptor.touch_end = end;
    descriptor.touch_generation = touch_generation.load(std::memory_order_relaxed);
    RunOnWorkers(helpers, [&] {
      RunLoop(call, tracker, begin, end);
      touch_generation.fetch_add(1, std::memory_order_relaxed);
    }, (trace_call != nullptr ? &trace_call->join_wait : nullptr));
    lap(&CallTrace::chunk_zero);
    if constexpr (TEST_SWITCHES) {
      last_call.direct = false;
    }
    finish(nullptr);
    return;
  }

  // The cut: targets begin + j * items / K, each moved forward (at most CUT_SEARCH items) to the first mesh change.
  std::array<uint32_t, MAX_THREADS + 1> cuts = {begin};
  uint32_t chunks = 1;
  for (uint32_t j = 1; j < settings.threads; ++j) {
    const uint32_t target = begin + static_cast<uint32_t>(static_cast<uint64_t>(items) * j / settings.threads);
    const uint32_t limit = std::min(target + CUT_SEARCH, end);
    uint32_t cut = 0;
    for (uint32_t i = std::max(target, cuts[chunks - 1] + 1); i < limit; ++i) {
      if (i + PREFETCH_AHEAD < end) {
        _mm_prefetch(reinterpret_cast<const char*>(static_cast<uintptr_t>(data[i + PREFETCH_AHEAD])), _MM_HINT_T0);
      }
      if (trace_call != nullptr) {
        ++trace_call->cut_probes;
      }
      if (mesh_of(i) != mesh_of(i - 1)) {
        cut = i;
        break;
      }
    }
    if (trace_call != nullptr) {
      ++trace_call->cut_targets;
    }
    if (cut != 0 && cut - cuts[chunks - 1] >= MIN_CHUNK && end - cut >= MIN_CHUNK) {
      cuts[chunks++] = cut;
    } else if (trace_call != nullptr) {
      ++trace_call->cut_failures;
    }
  }
  cuts[chunks] = end;
  lap(&CallTrace::cut);
  if (trace_call != nullptr) {
    trace_call->chunks = chunks;
    for (uint32_t k = 0; k < chunks; ++k) {
      trace_call->chunk_items[k] = cuts[k + 1] - cuts[k];
    }
  }
  if (chunks == 1) {
    RunLoop(call, tracker, begin, end);
    lap(&CallTrace::direct);
    finish("no cut");
    return;
  }
  if constexpr (TEST_SWITCHES) {
    last_call = {.chunks = chunks};
  }

  // The slots: a copy of the real tracker each, an empty op buffer sized for the chunk, the next part of the op memory
  // (committed as needed).
  uint32_t op_used = 0;
  const auto prepare_slot = [&](uint32_t index, uint32_t chunk_items) {
    Slot* slot = PrepareSlot(index, group_index, tracker, chunk_items * GROUPS[group_index].op_bytes_per_item, &op_used);
    if (trace_call != nullptr && index != COMPARE_SLOT) {
      trace_call->op_buffer_bytes = std::max(trace_call->op_buffer_bytes, static_cast<uint32_t>(slot->ops_end - slot->ops_begin) * 4);
    }
    return slot;
  };
  if (settings.stage >= Stage::RECORDED || compare_now) {
    for (uint32_t k = 0; k < chunks; ++k) {
      prepare_slot(k, cuts[k + 1] - cuts[k]);
    }
    // Warm-up of chunk k: the game's own loop over the item before its first, suppressed; what it leaves is the seed.
    for (uint32_t k = 1; k < chunks; ++k) {
      Slot* slot = SlotAt(k);
      // A warm-up item that continues the tracker's mesh has no setup site before it: with a class the census did not cover,
      // the warm-up would run that class's virtual calls. No warm-up then: the chunk is run again (its seed is never a hit).
      if (GROUPS[group_index].owns_meshes && controls.check_continuation_class && mesh_of(cuts[k] - 1) == read32(tracker, 0x70)
          && !WeightedClass(mesh_field(cuts[k] - 1, 0))) {
        slot->flags = SLOT_UNKNOWN_CLASS;
        slot->seed_anim = INT32_MIN;
        continue;
      }
      // The chunk before sets the warm-up item's mesh up itself: what the warm-up writes into it (+0x40 / +0x44) is put back.
      auto* const warm_mesh = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(mesh_of(cuts[k] - 1)));
      std::array<uint8_t, 8> mesh_state = {};
      std::memcpy(mesh_state.data(), warm_mesh + 0x40, 8);
      slot->mode = MODE_SUPPRESS;
      RunLoop(call, slot, cuts[k] - 1, cuts[k]);
      if (controls.restore_warm_up) {
        std::memcpy(warm_mesh + 0x40, mesh_state.data(), 8);
      }
      std::memcpy(slot->seed_fields.data(), slot->tracker.data(), slot->seed_fields.size());
      slot->seed_anim = slot->anim;
      slot->mode = MODE_RECORD;
      slot->flags = 0;
      std::memset(slot->tracker.data() + STRIDE_OFFSET, 0, 4);
      slot->tracker[ANY_OFFSET] = 0;
    }
    // Chunked does these in compared trace frames only (to count seed hits).
    lap(&CallTrace::warm_ups, settings.stage == Stage::CHUNKED);
  }

  if (settings.stage == Stage::CHUNKED) {
    // One chunk after the other on the real tracker, each started with the animation index the one before ended with.
    shared->last_anim = -1;
    for (uint32_t k = 0; k < chunks; ++k) {
      shared->seed_anim = (k == 0 ? -1 : shared->last_anim);
      if (compare_now && k > 0) {
        NoteSeed(trace_call, SlotAt(k), tracker, shared->seed_anim, group_index);
      }
      RunLoop(call, tracker, cuts[k], cuts[k + 1]);
    }
    shared->seed_anim = -1;
    lap(&CallTrace::chunk_zero);
    if constexpr (TEST_SWITCHES) {
      last_call.direct = false;
    }
    finish(nullptr);
    return;
  }

  if (GROUPS[group_index].owns_meshes) {
    NextOwnerCall();
    // Chunk 0 may continue the tracker's last mesh with no setup site before it: it owns its first mesh from the start (a fresh
    // call's table cannot refuse the entry).
    if (controls.claim_first_mesh) {
      ClaimMesh(mesh_of(begin), 0);
    }
  }

  // The in-frame compare (trace frames): the same call recorded whole in the compare slot, before anything changes.
  Slot* compare_slot = nullptr;
  if (compare_now) {
    compare_slot = prepare_slot(COMPARE_SLOT, items);
    RunLoop(call, compare_slot, begin, end);
    lap(&CallTrace::compare, true);
  }

  // Record: all chunks here (Recorded), or chunk 0 here and the rest on the workers.
  if (settings.stage == Stage::RECORDED) {
    for (uint32_t k = 0; k < chunks; ++k) {
      RunLoop(call, SlotAt(k), cuts[k], cuts[k + 1]);
    }
  } else {
    descriptor.work = RecordWork;
    descriptor.kind = WorkKind::RECORD_LOOP;
    descriptor.call = call;
    descriptor.chunks = chunks;
    descriptor.one_worker = (settings.stage == Stage::ONE_WORKER);
    descriptor.cuts = cuts;
    RunOnWorkers((settings.stage == Stage::ONE_WORKER ? 1 : chunks - 1), [&] { RunLoop(call, SlotAt(0), cuts[0], cuts[1]); },
                 (trace_call != nullptr ? &trace_call->join_wait : nullptr));
  }
  lap(&CallTrace::chunk_zero);

  bool conflict = false;
  bool damaged = false;
  for (uint32_t k = 0; k < chunks; ++k) {
    conflict = conflict || (SlotAt(k)->flags & SLOT_CONFLICT) != 0;
    damaged = damaged || !OpsWellFormed(SlotAt(k));
  }
  if (conflict || damaged) {
    if (trace_call != nullptr) {
      trace_call->conflicts += (conflict ? 1 : 0);
    }
    RunLoop(call, tracker, begin, end);
    lap(&CallTrace::rerun);
    finish(conflict ? "conflict" : "bad ops");
    return;
  }

  // Replay in item order; a chunk that can't be trusted runs again, serially, on the real tracker.
  Replay replay = {
      .real = static_cast<uint8_t*>(tracker), .group_index = group_index, .last_anim = -1, .op_count = 0, .trace_call = trace_call};
  uint32_t replayed = 0;
  uint32_t reruns = 0;
  CompareCursor compare;
  if (compare_slot != nullptr) {
    compare = MakeCompare(compare_slot);
  }
  for (uint32_t k = 0; k < chunks; ++k) {
    Slot* slot = SlotAt(k);
    const bool flagged = (slot->flags & (SLOT_OVERFLOW | SLOT_UNKNOWN_CLASS)) != 0;
    const bool seed_missed = (k > 0 && !SeedMatches(slot, replay.real, replay.last_anim, group_index));
    if (trace_call != nullptr) {
      trace_call->overflows += ((slot->flags & SLOT_OVERFLOW) != 0 ? 1 : 0);
      trace_call->unknown_classes += ((slot->flags & SLOT_UNKNOWN_CLASS) != 0 ? 1 : 0);
      if (k > 0) {
        NoteSeed(trace_call, slot, replay.real, replay.last_anim, group_index);
      }
    }
    if (flagged || (seed_missed && controls.validate_seeds)) {
      lap(&CallTrace::replay);
      shared->seed_anim = replay.last_anim;
      shared->last_anim = replay.last_anim;
      RunLoop(call, tracker, cuts[k], cuts[k + 1]);
      replay.last_anim = shared->last_anim;
      shared->seed_anim = -1;
      ++reruns;
      lap(&CallTrace::rerun);
      if (compare.result == CompareResult::EQUAL) {
        compare.result = CompareResult::CHUNK_RERUN;
      }
      continue;
    }
    if (compare.result == CompareResult::EQUAL) {
      CompareChunk(&compare, slot, k);
    }
    ReplayChunk(&replay, slot);
    ++replayed;
  }
  lap(&CallTrace::replay);
  CompareEnd(&compare);
  if (trace_call != nullptr) {
    trace_call->reruns = reruns;
    trace_call->compare_result = compare.result;
    trace_call->compare_op = compare.compared_ops;
    trace_call->compare_kind = compare.kind;
    trace_call->compare_chunk = compare.chunk;
    LatchOnDifference(group, compare.result);
  }
  if constexpr (TEST_SWITCHES) {
    last_call = {.chunks = chunks, .reruns = reruns, .replayed = replayed, .ops = replay.op_count, .direct = false,
                 .split_constants = compare.split_constants};
  }
  finish(nullptr);
}

}  // namespace internal

inline void __fastcall DispatchDepth(void* node, void*, void* tracker, void* list, uint32_t begin, uint32_t end) {
  internal::Dispatch(Group::DEPTH_A, node, tracker, list, 0, begin, end);
}

inline void __fastcall DispatchColour(void* node, void*, void* tracker, void* list, uint32_t unused, uint32_t begin, uint32_t end) {
  internal::Dispatch(Group::COLOUR_A, node, tracker, list, unused, begin, end);
}

namespace internal {

// A B round's plan: the round's meshes in list order, cut into segments. A census segment is one chunk (a run of census meshes
// the pool records); anything else is a direct segment, run on the real tracker when its turn comes in the replay.
struct Segment {
  uint32_t begin;  // planned[begin, end)
  uint32_t end;
  uint32_t chunk;  // NO_CHUNK: a direct segment
};
constexpr uint32_t NO_CHUNK = ~0u;
// At most 16 chunks, and a direct segment between each pair of them and at both ends.
constexpr uint32_t MAX_SEGMENTS = 2 * MAX_THREADS + 2;

struct Plan {
  std::array<Segment, MAX_SEGMENTS> segments = {};
  uint32_t segment_count = 0;
  uint32_t chunks = 0;
  std::array<uint32_t, MAX_THREADS> chunk_segment = {};
  uint32_t meshes = 0;
  uint32_t direct_meshes = 0;
  uint32_t census_items = 0;
  uint32_t direct_items = 0;
};

// The classes whose meshes never record, whatever the game's table says (the census could not clear their virtual calls: a
// getter reached through a runtime object): their vtables as the listings print them. The last is Colour B's only. (Two sources of
// "direct" now, these lists and the MeshClass flags; C.2-4 puts them in the table.)
constexpr std::array<uint32_t, 3> FORCED_DIRECT_BOTH = {0x11cbcb80, 0x11cbddd0, 0x11cbdc84};
constexpr uint32_t FORCED_DIRECT_COLOUR = 0x11cbd9d4;

// True when a mesh of this class (its index in the game's table; the table's size: no class) records in the group.
inline bool ClassRecords(uint32_t index, bool colour, uint32_t families) noexcept {
  if (index >= game_calls.class_count) return false;
  const MeshClass& meshes_class = game_calls.classes[index];
  if (!controls.class_flags) return true;
  const uint32_t listing = meshes_class.vtable - game_calls.module_shift;
  const bool forced =
      std::ranges::find(FORCED_DIRECT_BOTH, listing) != FORCED_DIRECT_BOTH.end() || (colour && listing == FORCED_DIRECT_COLOUR);
  return (colour ? meshes_class.colour : meshes_class.depth) && ((families >> meshes_class.family) & 1) != 0 && !forced;
}

// Plans the round [first, last] (render thread, no allocation): reads each mesh once (the accessor, its vtable, its slice),
// marks the census meshes, and cuts the census items into item-balanced chunks at mesh boundaries inside runs of census meshes.
// A chunk never spans a direct segment; chunks under MIN_CHUNK items merge into the chunk before them or become direct; chunks
// past the sixteenth become direct. Null: the plan stands; else why the round runs directly.
inline const char* PlanRound(const MeshRound& round, bool colour, uint32_t families, uint32_t threads, uint32_t first, uint32_t last,
                             Plan* plan) noexcept {
  const uint32_t count = last - first + 1;
  if (count > MAX_ROUND_MESHES) return "too many meshes";
  const auto accessor = reinterpret_cast<AccessorFn>(static_cast<uintptr_t>(game_calls.accessor));
  for (uint32_t i = 0; i < count; ++i) {
    if (i + PREFETCH_AHEAD < count) {
      _mm_prefetch(reinterpret_cast<const char*>(static_cast<uintptr_t>(*accessor(round.sorted, first + i + PREFETCH_AHEAD))),
                   _MM_HINT_T0);
    }
    const uint32_t mesh = *accessor(round.sorted, first + i);
    uint32_t vtable = 0;
    uint32_t items = 0;
    std::memcpy(&vtable, reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(mesh)), 4);
    std::memcpy(&items, reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(mesh)) + 0xb0, 4);
    uint32_t index = 0;
    while (index < game_calls.class_count && game_calls.classes[index].vtable != vtable) ++index;
    const bool census = ClassRecords(index, colour, families);
    planned[i] = {.mesh = mesh, .items = items, .class_index = index, .census = census};
    if (census) {
      plan->census_items += items;
    } else {
      plan->direct_items += items;
      ++plan->direct_meshes;
    }
  }
  plan->meshes = count;
  if (plan->census_items < MIN_ITEMS) return "small round";

  const uint32_t per_chunk = std::max((plan->census_items + threads - 1) / threads, MIN_CHUNK);
  const auto add_direct = [plan](uint32_t begin, uint32_t end) {
    if (plan->segment_count != 0 && plan->segments[plan->segment_count - 1].chunk == NO_CHUNK) {
      plan->segments[plan->segment_count - 1].end = end;
      return;
    }
    plan->segments[plan->segment_count++] = {.begin = begin, .end = end, .chunk = NO_CHUNK};
  };
  const auto add_chunk = [&](uint32_t begin, uint32_t end, uint32_t items) {
    const bool follows_chunk = (plan->segment_count != 0 && plan->segments[plan->segment_count - 1].chunk != NO_CHUNK
                                && plan->segments[plan->segment_count - 1].end == begin);
    if (items < MIN_CHUNK && follows_chunk) {
      plan->segments[plan->segment_count - 1].end = end;
    } else if (items < MIN_CHUNK || plan->chunks == MAX_THREADS) {
      add_direct(begin, end);
    } else {
      plan->chunk_segment[plan->chunks] = plan->segment_count;
      plan->segments[plan->segment_count++] = {.begin = begin, .end = end, .chunk = plan->chunks++};
    }
  };
  for (uint32_t i = 0; i < count;) {
    uint32_t run_end = i;
    while (run_end < count && planned[run_end].census == planned[i].census) ++run_end;
    if (!planned[i].census) {
      add_direct(i, run_end);
    } else {
      uint32_t chunk_begin = i;
      uint32_t chunk_items = 0;
      for (uint32_t m = i; m < run_end; ++m) {
        chunk_items += planned[m].items;
        if (chunk_items >= per_chunk && m + 1 < run_end) {
          add_chunk(chunk_begin, m + 1, chunk_items);
          chunk_begin = m + 1;
          chunk_items = 0;
        }
      }
      add_chunk(chunk_begin, run_end, chunk_items);
    }
    i = run_end;
  }
  return (plan->chunks < 2 ? "no cut" : nullptr);
}

}  // namespace internal

// The B groups' cut (the folded driver's per-mesh loop): the game calls every mesh of the round [first, last] in list order with
// its own slot-0x138 call; this runs the round the same way (the same calls, on the same thread, in the same order, as far as
// anything can tell) with the census meshes' calls recorded on the pool. The thunk calls it with what the stub pushes
// (stdcall). Which group the round belongs to comes from the view's type: {0, 0x55, 0x60} Depth B, 0x49 (visibility) direct, any
// other Colour B.
inline void __stdcall DispatchMesh(void* tracker, void* device, void* view, uint32_t list_type, void* node, void* sorted, uint32_t first,
                                   uint32_t last) noexcept {
  using namespace internal;
  if (first > last) return;
  const Config settings = config;
  const MeshRound round = {.device = device, .view = view, .list_type = list_type, .node = node, .sorted = sorted};
  const bool nested = InRound();
  // Trace-only (noexcept: an allocation failure here ends the process; Update reserves room when the trace arms). A nested call
  // adds no entry: the round it is in holds a pointer to its own.
  CallTrace* const trace_call = (trace.active && !nested ? &trace.calls.emplace_back() : nullptr);
  const bool compare_now = (trace_call != nullptr && (controls.compare_every_call || trace.frames_left == 1));
  uint64_t last_lap = (trace_call != nullptr ? Qpc() : 0);
  const uint64_t call_start = last_lap;
  // Work only trace frames do: left out of the call's total.
  uint64_t trace_only_ticks = 0;
  const auto lap = [&](uint64_t CallTrace::* phase, bool trace_only = false) {
    if (trace_call == nullptr) return;
    const uint64_t now = Qpc();
    trace_call->*phase += now - last_lap;
    if (trace_only) {
      trace_only_ticks += now - last_lap;
    }
    last_lap = now;
  };
  Plan plan;
  const auto finish = [&](const char* direct_reason) {
    if (trace_call != nullptr) {
      trace_call->direct_reason = (direct_reason != nullptr ? direct_reason : "");
      trace_call->wall = Qpc() - call_start;
      trace_call->total = trace_call->wall - trace_only_ticks;
      trace_call->meshes = plan.meshes;
      trace_call->direct_meshes = plan.direct_meshes;
      trace_call->census_items = plan.census_items;
      trace_call->direct_items = plan.direct_items;
      trace.frame_call_ticks += trace_call->total;
    }
  };
  if (trace_call != nullptr) {
    trace_call->threads = settings.threads;
    trace_call->stage = settings.stage;
    trace_call->compared = compare_now;
    trace.frame_compared = trace.frame_compared || compare_now;
  }
  if constexpr (TEST_SWITCHES) {
    last_call = {.direct = true};
  }

  // ONE_WORKER is the render thread and one worker here (the A groups' is one worker for the chunks after the first).
  uint32_t participants = 1;
  switch (settings.stage) {
    case Stage::WORKERS:
      participants = settings.threads;
      break;
    case Stage::ONE_WORKER:
      participants = 2;
      break;
    default:
      break;
  }
  const char* direct_reason = nullptr;
  if (!game_calls.usable) {
    direct_reason = "not usable";
  } else if (detached.load(std::memory_order_relaxed)) {
    direct_reason = "detached";
  } else if (settings.stage <= Stage::DIRECT || settings.stage == Stage::TOUCH_AHEAD) {
    direct_reason = "stage";
  } else if (nested) {
    direct_reason = "nested";
    if constexpr (TEST_SWITCHES) ++nested_calls;
  }
  // The view's type (a virtual call of the game) is read once the cheap checks are through; a trace wants every call's group.
  uint32_t view_type = 0;
  if (direct_reason == nullptr || trace_call != nullptr) {
    const auto view_vtable = *reinterpret_cast<const uint32_t* const*>(view);
    view_type = reinterpret_cast<ViewTypeFn>(static_cast<uintptr_t>(view_vtable[0x3c / 4]))(view);
  }
  const bool is_depth = (view_type == 0 || view_type == 0x55 || view_type == 0x60);
  const Group group = (is_depth ? Group::DEPTH_B : Group::COLOUR_B);
  const uint32_t group_index = static_cast<uint32_t>(group);
  if (trace_call != nullptr) {
    trace_call->group = group;
    trace_call->round_meshes = last - first + 1;
  }
  if (direct_reason != nullptr) {
    // Decided above.
  } else if (((game_calls.usable_groups >> group_index) & 1) == 0) {
    direct_reason = "not usable";
  } else if (shared->arena_size == 0) {
    // Not one site of the thunks is in (engine_code zeroes it when the last comes out): the thunks only run directly.
    direct_reason = "sites out";
  } else if (view_type == 0x49) {
    direct_reason = "visibility view";
  } else if (((settings.loops >> group_index) & 1) == 0) {
    direct_reason = "loop off";
  } else if (((latched_groups >> group_index) & 1) != 0) {
    direct_reason = "latched off";
  } else if (GetCurrentThreadId() != present_thread.load(std::memory_order_relaxed)) {
    direct_reason = "not the present thread";
  } else if (worker_count + 1 < participants) {
    direct_reason = "pool missing";
  } else if (*reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(tracker) + 0x58) != 0
             || *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(tracker) + 0x78) != 0) {
    // The chunks start with no pending instances and no change mask, as a round the bodies themselves ended does.
    direct_reason = "entry state";
  } else {
    direct_reason = PlanRound(round, !is_depth, settings.families, settings.threads, first, last, &plan);
    lap(&CallTrace::plan);
    if (direct_reason == nullptr && compare_now) {
      // S3: no mesh appears twice in a round (compared trace frames only: a repeat makes the round direct and is counted). The
      // scan buffer is static: only the render thread gets here (the present thread check above).
      for (uint32_t i = 0; i < plan.meshes; ++i) duplicate_scan[i] = planned[i].mesh;
      std::sort(duplicate_scan.begin(), duplicate_scan.begin() + plan.meshes);
      for (uint32_t i = 1; i < plan.meshes; ++i) trace_call->duplicate_meshes += (duplicate_scan[i] == duplicate_scan[i - 1] ? 1 : 0);
      lap(&CallTrace::run_scan, true);
      if (trace_call->duplicate_meshes != 0) direct_reason = "duplicate meshes";
    }
  }
  if (direct_reason != nullptr) {
    if (plan.meshes != 0) {
      // S-1: the planned list is in use while these run: a dispatcher call from a mesh's body runs directly.
      const RoundScope planned_scope;
      RunMeshes(round, tracker, 0, plan.meshes);
    } else {
      const auto accessor = reinterpret_cast<AccessorFn>(static_cast<uintptr_t>(game_calls.accessor));
      for (uint32_t i = first; i <= last; ++i) {
        RunMesh(round, tracker, *accessor(sorted, i));
      }
    }
    lap(&CallTrace::direct);
    finish(direct_reason);
    return;
  }
  const RoundScope round_scope;
  const uint32_t chunks = plan.chunks;
  if (trace_call != nullptr) {
    trace_call->chunks = chunks;
    trace_call->items = plan.census_items + plan.direct_items;
    for (uint32_t c = 0; c < chunks && c < trace_call->chunk_items.size(); ++c) {
      const Segment& segment = plan.segments[plan.chunk_segment[c]];
      uint32_t families_in_chunk = 0;
      for (uint32_t m = segment.begin; m < segment.end; ++m) {
        trace_call->chunk_items[c] += planned[m].items;
        const uint32_t family = game_calls.classes[planned[m].class_index].family;
        trace_call->family_items[family % 8] += planned[m].items;
        families_in_chunk |= 1u << (family % 8);
      }
      for (uint32_t family = 0; family < 8; ++family) trace_call->family_chunks[family] += (families_in_chunk >> family) & 1;
    }
  }
  if constexpr (TEST_SWITCHES) {
    last_call = {.chunks = chunks};
  }

  // The slots: a copy of the real tracker each and a buffer sized for the chunk's items and meshes. Chunk c's slot is warmed up
  // when something comes before it in the round (a chunk, a direct segment).
  uint32_t op_used = 0;
  const GroupInfo& info = GROUPS[group_index];
  const auto chunk_bytes = [&](const Segment& segment) {
    uint32_t items = 0;
    for (uint32_t m = segment.begin; m < segment.end; ++m) items += planned[m].items;
    return items * info.op_bytes_per_item + (segment.end - segment.begin) * info.op_bytes_per_mesh;
  };
  std::array<uint32_t, MAX_THREADS> mesh_begin = {};
  std::array<uint32_t, MAX_THREADS> mesh_end = {};
  uint32_t unknown_seeds = 0;
  if (settings.stage >= Stage::RECORDED || compare_now) {
    for (uint32_t c = 0; c < chunks; ++c) {
      const Segment& segment = plan.segments[plan.chunk_segment[c]];
      mesh_begin[c] = segment.begin;
      mesh_end[c] = segment.end;
      Slot* slot = PrepareSlot(c, group_index, tracker, chunk_bytes(segment), &op_used);
      if (trace_call != nullptr) {
        trace_call->op_buffer_bytes = std::max(trace_call->op_buffer_bytes, static_cast<uint32_t>(slot->ops_end - slot->ops_begin) * 4);
      }
    }
    // Warm-up of a chunk that something precedes: the body of the nearest earlier census mesh with items, over its last item,
    // suppressed (at most four meshes back, until the item reached the state code: +0x74 set); what it leaves in the tracker's
    // first 0x14 bytes is the seed. No such mesh: the round's start state is the seed, and validation decides at the replay.
    for (uint32_t c = 0; c < chunks; ++c) {
      const Segment& segment = plan.segments[plan.chunk_segment[c]];
      if (segment.begin == 0) continue;
      Slot* slot = SlotAt(c);
      std::array<uint8_t, 0x14> start_fields = {};
      std::memcpy(start_fields.data(), slot->tracker.data(), start_fields.size());
      uint32_t attempts = 0;
      bool seeded = false;
      for (uint32_t j = segment.begin; j > 0 && attempts < 4 && !seeded;) {
        const PlannedMesh& earlier = planned[--j];
        if (!earlier.census || earlier.items == 0) continue;
        ++attempts;
        // The test below reads what this attempt's body left in +0x74: not what an earlier attempt or the tracker held.
        std::memset(slot->tracker.data() + 0x74, 0, 4);
        const MeshClass& earlier_class = game_calls.classes[earlier.class_index];
        auto* const mesh = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(earlier.mesh));
        // The mesh's own chunk sets it up again: what the warm-up writes into it (+0x40 / +0x44) is put back.
        std::array<uint8_t, 8> mesh_state = {};
        std::memcpy(mesh_state.data(), mesh + 0x40, 8);
        uint32_t item = 0;
        std::memcpy(&item, mesh + 0xac, 4);
        item += earlier.items - 1;
        auto* const list = reinterpret_cast<void*>(static_cast<uintptr_t>(earlier.mesh + earlier_class.base + 32 * list_type + 4));
        slot->mode = MODE_SUPPRESS;
        if (is_depth) {
          reinterpret_cast<DepthBodyFn>(static_cast<uintptr_t>(earlier_class.depth_body))(mesh, slot, list, item, item + 1);
        } else {
          reinterpret_cast<ColourBodyFn>(static_cast<uintptr_t>(earlier_class.colour_body))(mesh, slot, list, node, item, item + 1);
        }
        if (controls.restore_warm_up) {
          std::memcpy(mesh + 0x40, mesh_state.data(), 8);
        }
        slot->mode = MODE_RECORD;
        slot->flags = 0;
        // The body ends with its pending count and change mask clear; the stride and the "any" byte are the chunk's own.
        std::memset(slot->tracker.data() + 0x58, 0, 4);
        std::memset(slot->tracker.data() + 0x78, 0, 4);
        std::memset(slot->tracker.data() + STRIDE_OFFSET, 0, 4);
        slot->tracker[ANY_OFFSET] = 0;
        std::memcpy(&item, slot->tracker.data() + 0x74, 4);
        seeded = (item != 0);
      }
      if (!seeded) {
        std::memcpy(slot->tracker.data(), start_fields.data(), start_fields.size());
        ++unknown_seeds;
      }
      std::memcpy(slot->seed_fields.data(), slot->tracker.data(), slot->seed_fields.size());
      slot->seed_anim = -1;
    }
    // Chunked does these in compared trace frames only (to count seed hits).
    lap(&CallTrace::warm_ups, settings.stage == Stage::CHUNKED);
  }

  if (settings.stage == Stage::CHUNKED) {
    for (uint32_t s = 0; s < plan.segment_count; ++s) {
      const Segment& segment = plan.segments[s];
      if (compare_now && segment.chunk != NO_CHUNK && segment.begin > 0) {
        NoteSeed(trace_call, SlotAt(segment.chunk), tracker, -1, group_index);
      }
      RunMeshes(round, tracker, segment.begin, segment.end);
    }
    lap(&CallTrace::chunk_zero);
    if constexpr (TEST_SWITCHES) {
      last_call.direct = false;
    }
    finish(nullptr);
    return;
  }

  // The in-frame compare (trace frames): each run of census chunks recorded whole in the compare slot, from the state its first
  // chunk starts with, before anything changes. (Direct segments are not run on a slot: a run's compare starts at its seed.) One
  // compare per run replaces A's DIRECT marker: the runs are what a direct segment cuts the round into.
  // The compare runs set the round's meshes up, the first one (a straddler: the previous round's last mesh, continued with no
  // setup of its own) included; the chunk that records it reads that state before it writes it, so it is put back (M-2). The
  // recording of the straddler's chunk also writes its +0x44 (the variation path) without the flush that writes +0x40 with it: a
  // chunk 0 run again, or a round run directly after a damaged buffer, starts from the state saved here (R-S1).
  std::array<CompareCursor, MAX_THREADS> compares = {};
  std::array<uint32_t, MAX_SEGMENTS> run_of = {};
  uint32_t runs = 0;
  auto* const straddler = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(planned[0].mesh));
  std::array<uint8_t, 8> straddler_state = {};
  std::memcpy(straddler_state.data(), straddler + 0x40, straddler_state.size());
  for (uint32_t s = 0; s < plan.segment_count; ++s) {
    const Segment& segment = plan.segments[s];
    if (segment.chunk == NO_CHUNK) continue;
    const bool starts_run = (s == 0 || plan.segments[s - 1].chunk == NO_CHUNK);
    run_of[s] = (starts_run ? runs++ : runs - 1);
    if (!compare_now || !starts_run) continue;
    uint32_t run_end = s;
    uint32_t run_bytes = 0;
    while (run_end < plan.segment_count && plan.segments[run_end].chunk != NO_CHUNK) run_bytes += chunk_bytes(plan.segments[run_end++]);
    Slot* compare_slot = PrepareSlot(COMPARE_SLOT, group_index, tracker, run_bytes, &op_used);
    std::memcpy(compare_slot->tracker.data(), SlotAt(segment.chunk)->tracker.data(), TRACKER_SIZE);
    RunMeshes(round, compare_slot, segment.begin, plan.segments[run_end - 1].end);
    compares[run_of[s]] = MakeCompare(compare_slot);
  }
  if (compare_now && controls.restore_straddler) {
    std::memcpy(straddler + 0x40, straddler_state.data(), straddler_state.size());
  }
  lap(&CallTrace::compare, true);

  // Record: all chunks here (Recorded), or chunk c on thread c % participants (thread 0 is this one).
  if (settings.stage == Stage::RECORDED) {
    for (uint32_t c = 0; c < chunks; ++c) {
      RunMeshes(round, SlotAt(c), mesh_begin[c], mesh_end[c]);
    }
  } else {
    descriptor.work = RecordWork;
    descriptor.kind = WorkKind::RECORD_MESHES;
    descriptor.meshes = round;
    descriptor.chunks = chunks;
    descriptor.participants = participants;
    descriptor.mesh_begin = mesh_begin;
    descriptor.mesh_end = mesh_end;
    RunOnWorkers(std::min(participants, chunks) - 1, [&] {
      for (uint32_t c = 0; c < chunks; c += participants) {
        RunMeshes(round, SlotAt(c), mesh_begin[c], mesh_end[c]);
      }
    }, (trace_call != nullptr ? &trace_call->join_wait : nullptr));
  }
  lap(&CallTrace::chunk_zero);

  // A damaged op buffer is never walked: nothing has run on the real tracker yet, so the whole round runs directly.
  bool damaged = false;
  for (uint32_t c = 0; c < chunks; ++c) damaged = damaged || !OpsWellFormed(SlotAt(c));
  if (damaged) {
    if (controls.restore_straddler) std::memcpy(straddler + 0x40, straddler_state.data(), straddler_state.size());
    RunMeshes(round, tracker, 0, plan.meshes);
    lap(&CallTrace::rerun);
    finish("bad ops");
    return;
  }

  // Replay in list order: direct segments run on the real tracker; a census chunk that can't be trusted (an overflow, a seed
  // that is not the real tracker's, an entry state after a direct segment the chunks did not see) runs directly too.
  Replay replay = {
      .real = static_cast<uint8_t*>(tracker), .group_index = group_index, .last_anim = -1, .op_count = 0, .trace_call = trace_call};
  uint32_t replayed = 0;
  uint32_t reruns = 0;
  uint32_t entry_reruns = 0;
  const uint32_t round_start_64 = *reinterpret_cast<const uint32_t*>(replay.real + 0x64);
  uint32_t direct_segments = 0;
  uint32_t split_constants = 0;
  for (uint32_t s = 0; s < plan.segment_count; ++s) {
    const Segment& segment = plan.segments[s];
    if (segment.chunk == NO_CHUNK) {
      lap(&CallTrace::replay);
      RunMeshes(round, tracker, segment.begin, segment.end);
      ++direct_segments;
      lap(&CallTrace::direct);
      continue;
    }
    Slot* slot = SlotAt(segment.chunk);
    const bool flagged = (slot->flags & (SLOT_OVERFLOW | SLOT_UNKNOWN_CLASS)) != 0;
    const bool seed_missed = (segment.begin > 0 && !SeedMatches(slot, replay.real, -1, group_index));
    // M2: once a direct segment has run in this round, a census chunk replays only when the real tracker is where a body leaves
    // it, with the +0x64 the record started from (also a chunk that follows a replayed chunk: the batch size a direct segment
    // changed stays changed).
    // The bodies also read +0x18 .. +0x28 (into flush and virtual call arguments, and +0x20 into the mesh's +0x40): what the slot
    // was copied with must still be there.
    const bool entry_bad = (direct_segments > 0 && controls.entry_check
                            && (*reinterpret_cast<const uint32_t*>(replay.real + 0x58) != 0
                                || (controls.entry_check_mask && *reinterpret_cast<const uint32_t*>(replay.real + 0x78) != 0)
                                || *reinterpret_cast<const uint32_t*>(replay.real + 0x64) != round_start_64
                                || (controls.entry_check_inputs
                                    && std::memcmp(replay.real + 0x18, slot->tracker.data() + 0x18, 0x10) != 0)));
    if (trace_call != nullptr) {
      trace_call->overflows += ((slot->flags & SLOT_OVERFLOW) != 0 ? 1 : 0);
      trace_call->unknown_classes += ((slot->flags & SLOT_UNKNOWN_CLASS) != 0 ? 1 : 0);
      trace_call->entry_reruns += (entry_bad ? 1 : 0);
      if (segment.begin > 0) {
        NoteSeed(trace_call, slot, replay.real, -1, group_index);
      }
    }
    CompareCursor& compare = compares[run_of[s]];
    if (flagged || entry_bad || (seed_missed && controls.validate_seeds)) {
      lap(&CallTrace::replay);
      if (segment.begin == 0 && controls.restore_straddler) std::memcpy(straddler + 0x40, straddler_state.data(), straddler_state.size());
      RunMeshes(round, tracker, segment.begin, segment.end);
      ++reruns;
      entry_reruns += (entry_bad ? 1 : 0);
      lap(&CallTrace::rerun);
      if (compare.result == CompareResult::EQUAL) {
        compare.result = CompareResult::CHUNK_RERUN;
      }
      continue;
    }
    if (compare.result == CompareResult::EQUAL) {
      CompareChunk(&compare, slot, segment.chunk);
    }
    ReplayChunk(&replay, slot);
    ++replayed;
  }
  lap(&CallTrace::replay);
  if (compare_now) {
    // The round's compare: the runs combined by severity (the first of the worst names the result and where it is), so that a
    // later run's difference is not hidden by an earlier run that was run again; the latch sees the combined result.
    uint32_t worst = 0;
    for (uint32_t r = 0; r < runs; ++r) {
      CompareEnd(&compares[r]);
      split_constants += compares[r].split_constants;
      if (Severity(compares[r].result) > Severity(compares[worst].result)) {
        worst = r;
      }
    }
    const CompareCursor& named = compares[worst];
    trace_call->compare_result = named.result;
    trace_call->compare_run = worst;
    trace_call->compare_op = named.compared_ops;
    trace_call->compare_kind = named.kind;
    if (named.result == CompareResult::DIFFERS) {
      // The differing op's own mesh names the class (a chunk's first flush is the mesh before it: anywhere in the round; an op
      // with no mesh, or one that is not in the round: the chunk's first mesh).
      uint32_t at = plan.segments[plan.chunk_segment[named.chunk]].begin;
      for (uint32_t m = 0; m < plan.meshes; ++m) {
        if (planned[m].mesh == named.mesh) at = m;
      }
      const uint32_t class_index = planned[at].class_index;
      trace_call->compare_chunk = named.chunk;
      trace_call->compare_class = class_index;
      trace_call->family = game_calls.classes[class_index].family;
    }
    LatchOnDifference(group, named.result);
  }
  if (trace_call != nullptr) {
    trace_call->reruns = reruns;
    trace_call->unknown_seeds = unknown_seeds;
  }
  if constexpr (TEST_SWITCHES) {
    last_call = {.chunks = chunks, .reruns = reruns, .replayed = replayed, .ops = replay.op_count, .direct = false,
                 .split_constants = split_constants, .direct_segments = direct_segments, .entry_reruns = entry_reruns,
                 .unknown_seeds = unknown_seeds};
  }
  finish(nullptr);
}

namespace internal {

// The first allocation (SharedPage or the first Update with the row on): the page, the slots, the op address space and a
// reference to this module (the thunks and the workers run its code). False after a failure, which is logged once.
inline bool Allocate() {
  if (shared != nullptr) return true;
  if (allocation_failed) return false;
  void* const page = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  void* const slots = VirtualAlloc(nullptr, static_cast<size_t>(SLOT_COUNT) * SLOT_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  void* const ops = VirtualAlloc(nullptr, OP_ARENA_BYTES, MEM_RESERVE, PAGE_READWRITE);
  HMODULE module = nullptr;
  if (page == nullptr || slots == nullptr || ops == nullptr
      || GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&Record), &module) == FALSE) {
    for (void* memory : {page, slots, ops}) {
      if (memory != nullptr) {
        VirtualFree(memory, 0, MEM_RELEASE);
      }
    }
    allocation_failed = true;
    renodx::utils::log::w("[Total War] Threaded Draw Recording: stands down: its memory could not be allocated.");
    return false;
  }
  arena = static_cast<uint8_t*>(slots);
  op_memory = static_cast<uint8_t*>(ops);
  module_reference = module;
  op_committed = 0;
  for (uint32_t k = 0; k < SLOT_COUNT; ++k) {
    new (arena + static_cast<size_t>(k) * SLOT_SIZE) Slot();
  }
  shared = new (page) Shared();
  shared->arena = reinterpret_cast<uint32_t>(arena);
  shared->arena_size = SLOT_COUNT * SLOT_SIZE;
  shared->record = reinterpret_cast<uint32_t>(&Record);
  shared->dispatch_depth = reinterpret_cast<uint32_t>(&DispatchDepth);
  shared->dispatch_colour = reinterpret_cast<uint32_t>(&DispatchColour);
  shared->dispatch_mesh = reinterpret_cast<uint32_t>(&DispatchMesh);
  return true;
}

}  // namespace internal

// The page the thunks read; created the first time it is asked for. Null when the memory could not be allocated.
inline Shared* SharedPage() { return (internal::Allocate() ? internal::shared : nullptr); }

// The game's addresses (Prepare, once the sites are verified).
inline void SetGame(const GameCalls& calls) { internal::game_calls = calls; }

// Every present on the render thread, after the engine code's Update. `sites_in`: any of this feature's sites is written.
inline void Update(const Settings& settings, bool sites_in) {
  using namespace internal;
  present_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
  Stage stage = settings.stage;
  const uint32_t threads = std::clamp<uint32_t>(settings.threads, 1, MAX_THREADS);
  if (stage != Stage::OFF && !Allocate()) {
    stage = Stage::DIRECT;
  }
  // A group that is on but not usable in this build (the whole game, or its own sites) runs the game's loop: said once each.
  if (stage != Stage::OFF) {
    const uint32_t unusable = settings.loops & (game_calls.usable ? ~game_calls.usable_groups : ~0u) & ~unusable_logged
                              & ((1u << static_cast<uint32_t>(Group::SHARED)) - 1);
    for (uint32_t group = 0; unusable >> group != 0; ++group) {
      if (((unusable >> group) & 1) != 0) {
        renodx::utils::log::w(std::format(
            "[Total War] Threaded Draw Recording: group {} is on but not usable in this build: it runs the game's own loop.",
            GROUP_NAMES[group]));
      }
    }
    unusable_logged |= unusable;
  }

  // The pool grows here, never inside a dispatch. After a failure it is not tried again until the stage or K changes.
  uint32_t wanted = 0;
  switch (stage) {
    case Stage::TOUCH_AHEAD:
    case Stage::WORKERS:
      wanted = threads - 1;
      break;
    case Stage::ONE_WORKER:
      wanted = 1;
      break;
    default:
      break;
  }
  const uint32_t pool_key = (static_cast<uint32_t>(stage) << 8) | threads;
  if (stage == Stage::OFF || pool_key != pool_failed_key) {
    pool_failed_key = ~0u;
  }
  if (wanted > worker_count && pool_failed_key == ~0u) {
    stopping.store(false, std::memory_order_release);
  }
  for (; worker_count < wanted && pool_failed_key == ~0u; ++worker_count) {
    pool[worker_count].ticket.store(0, std::memory_order_relaxed);
    HANDLE thread = nullptr;
    if constexpr (TEST_SWITCHES) {
      ++thread_attempts;
    }
    if (!controls.fail_thread_creation) {
      thread = CreateThread(nullptr, WORKER_STACK_BYTES, WorkerMain, reinterpret_cast<void*>(static_cast<uintptr_t>(worker_count)),
                            STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    }
    if (thread == nullptr) {
      renodx::utils::log::w("[Total War] Threaded Draw Recording: a worker thread could not start; the calls run directly.");
      pool_failed_key = pool_key;
      break;
    }
    SetThreadDescription(thread, L"RenoDX draw recording");
    SetThreadPriority(thread, GetThreadPriority(GetCurrentThread()));
    pool[worker_count].thread = thread;
    given_generation = ~0u;
  }

  // The pool follows Keep Render Core Free: the sets it applied to the game's other threads (none: cleared). The sets are
  // copied only when their generation changed.
  if (worker_count > 0 && cpu_sets::AppliedGeneration() != given_generation) {
    uint32_t generation = 0;
    const std::vector<ULONG> sets = cpu_sets::AppliedWorkerSets(&generation);
    for (uint32_t w = 0; w < worker_count; ++w) {
      SetThreadSelectedCpuSets(pool[w].thread, (sets.empty() ? nullptr : sets.data()), static_cast<ULONG>(sets.size()));
    }
    given_generation = generation;
  }
  if (stage == Stage::OFF && !sites_in) {
    StopPool();
  }

  // The one-frame trace.
  if (!trace.read_environment) {
    trace.read_environment = true;
    trace.first_present = Qpc();
    char text[64] = {};
    if (GetEnvironmentVariableA("RENODX_TW_DRAW_TRACE", text, sizeof(text)) != 0) {
      const std::string_view view(text);
      double seconds = 0;
      const auto [seconds_end, seconds_error] = std::from_chars(view.data(), view.data() + view.size(), seconds);
      if (seconds_error == std::errc()) {
        trace.armed = true;
        trace.arm_ticks = trace.first_present + static_cast<uint64_t>(seconds * static_cast<double>(qpc_frequency));
        if (seconds_end != view.data() + view.size() && *seconds_end == ',') {
          std::from_chars(seconds_end + 1, view.data() + view.size(), trace.frames);
        }
      }
    }
  }
  if (trace.active) {
    trace.frame_list.push_back(
        {.call_ticks = trace.frame_call_ticks,
         .thread_matched = (settings.context_hooks_thread == 0 || settings.context_hooks_thread == GetCurrentThreadId()),
         .compared = trace.frame_compared});
    trace.frame_call_ticks = 0;
    trace.frame_compared = false;
    if (--trace.frames_left == 0) {
      trace.active = false;
      trace.finished = true;
      std::array<wchar_t, MAX_PATH> exe = {};
      GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
      const auto milliseconds = [](uint64_t ticks) { return static_cast<double>(ticks) * 1000.0 / static_cast<double>(qpc_frequency); };
      std::string text = "Threaded Draw Recording trace\n";
      for (const FrameTrace& frame : trace.frame_list) {
        text += std::format("frame: calls {:.3f} ms ({}; trace-only work left out), present thread {} context_hooks' thread\n",
                            milliseconds(frame.call_ticks),
                            (frame.compared ? "compared frame: warmed by the compare run, not a normal frame's time" : "timing frame"),
                            (frame.thread_matched ? "is" : "is not"));
      }
      for (const CallTrace& call : trace.calls) {
        text += std::format("call: group {} family {} stage {} K {} items {} chunks {} direct '{}' {}\n  chunk items:",
                            static_cast<uint32_t>(call.group), (call.family == NO_FAMILY ? std::string("-") : std::to_string(call.family)),
                            static_cast<uint32_t>(call.stage), call.threads, call.items, call.chunks, call.direct_reason,
                            (call.compared ? "compared" : "timing"));
        for (uint32_t k = 0; k < call.chunks && k < call.chunk_items.size(); ++k) {
          text += std::format(" {}", call.chunk_items[k]);
        }
        const bool b_call = (call.group == Group::DEPTH_B || call.group == Group::COLOUR_B);
        if (!b_call) {
          // (The B rounds cut by the planner: no targets, probes or run lengths.)
          text += std::format("\n  cut: targets {} probes {} failures {}; runs", call.cut_targets, call.cut_probes, call.cut_failures);
          for (const uint32_t runs : call.runs) {
            text += std::format(" {}", runs);
          }
        }
        // The seed fields: Depth B's bytes are the tracker's, not the colour loops' fields.
        const std::array<const char*, 5> field_names = (call.group == Group::DEPTH_B
                                                            ? std::array<const char*, 5>{"+0x00", "+0x04", "+0x0c", "+0x10", "anim"}
                                                            : std::array<const char*, 5>{"faction", "decals", "mask", "flags", "anim"});
        std::string miss_fields;
        for (size_t field = 0; field < field_names.size(); ++field) {
          miss_fields += std::format("{}{} {}", (field == 0 ? "" : " "), field_names[field], call.seed_miss_fields[field]);
        }
        std::string compare_text = "not run (timing frame)";
        if (call.compared) {
          compare_text = (call.direct_reason[0] != '\0' ? std::format("not run (direct: {})", call.direct_reason)
                                                        : std::string("not run (this stage does not compare)"));
        }
        switch (call.compare_result) {
          case CompareResult::EQUAL:
            compare_text = "equal";
            break;
          case CompareResult::SLOT_NOT_COMPARABLE:
            compare_text = "not comparable (compare slot)";
            break;
          case CompareResult::CHUNK_RERUN:
            compare_text = "not comparable (a chunk was run again)";
            break;
          case CompareResult::COMPARE_HAS_MORE:
            compare_text = "differs (the compare run has more)";
            break;
          default:
            break;
        }
        text += std::format(
            "\n  ops: flush {} vf68 {} vf64 {} anim {} const {} (repeats {}) srv {} faction {} state {} bytes {} (buffer {});"
            " seeds hit {} miss {} (by field: {}); overflow {} unknown {} conflict {} rerun {}\n"
            "  ms: direct {:.3f} cut {:.3f} warm-ups {:.3f} chunk0 {:.3f} join {:.3f} replay {:.3f} rerun {:.3f} total {:.3f}"
            " (trace only: compare {:.3f} run scan {:.3f}; wall {:.3f})\n  compare: ",
            call.ops[1], call.ops[2], call.ops[7], call.ops[3], call.ops[4], call.constant_repeats, call.ops[5], call.ops[6], call.ops[8],
            call.op_bytes, call.op_buffer_bytes, call.seed_hits, call.seed_misses, miss_fields, call.overflows, call.unknown_classes,
            call.conflicts, call.reruns,
            milliseconds(call.direct), milliseconds(call.cut), milliseconds(call.warm_ups), milliseconds(call.chunk_zero),
            milliseconds(call.join_wait), milliseconds(call.replay), milliseconds(call.rerun), milliseconds(call.total),
            milliseconds(call.compare), milliseconds(call.run_scan), milliseconds(call.wall));
        if (call.compare_result == CompareResult::DIFFERS) {
          text += std::format("differs at op {} of run {} (kind {}) in chunk {}", call.compare_op, call.compare_run, call.compare_kind,
                              call.compare_chunk);
          if (b_call) {
            text += std::format(" (the op's mesh: class {} family {})", call.compare_class, call.family);
          }
          text += "\n";
        } else {
          text += compare_text + "\n";
        }
        if (call.group == Group::DEPTH_B || call.group == Group::COLOUR_B) {
          // The B gates: the share of the round's items the pool records (G1), the seeds that held (G3), the chunks run again,
          // and what the planner cost (G2: its time sits in total).
          const uint32_t all_items = call.census_items + call.direct_items;
          const uint32_t seeds = call.seed_hits + call.seed_misses;
          text += std::format(
              "  B round: meshes {} (direct {}; the round lists {}{}) items {} census {} ({:.1f} %) direct {}; duplicate meshes {};"
              " seeds hit {:.1f} % unknown {};"
              " chunks run again {} (entry check {}, of {}); planner {:.3f} ms\n",
              call.meshes, call.direct_meshes, call.round_meshes, (call.round_meshes > MAX_ROUND_MESHES ? ": over the limit" : ""),
              all_items, call.census_items,
              (all_items != 0 ? 100.0 * call.census_items / all_items : 0.0), call.direct_items, call.duplicate_meshes,
              (seeds != 0 ? 100.0 * call.seed_hits / seeds : 100.0), call.unknown_seeds, call.reruns, call.entry_reruns, call.chunks,
              milliseconds(call.plan));
          // Census items / chunks of each family (the planner's, the class table's family numbers).
          text += "  B families (census items / chunks):";
          for (size_t family = 0; family < call.family_items.size(); ++family) {
            text += std::format(" f{} {}/{}", family, call.family_items[family], call.family_chunks[family]);
          }
          text += "\n";
        }
      }
      std::ofstream(std::filesystem::path(exe.data()).parent_path() / "tw_draw_recording.txt") << text;
      trace.calls.clear();
      trace.frame_list.clear();
    }
  } else if (trace.armed && !trace.finished && Qpc() >= trace.arm_ticks) {
    trace.active = true;
    trace.frames_left = std::max<uint32_t>(trace.frames, 1);
    // Room for a window's calls: a reallocation inside a timing frame would evict the caches before the timed phases.
    trace.calls.reserve(4096);
  }

  config = {.stage = stage, .threads = threads, .loops = settings.loops, .families = settings.b_families};
}

// The `groups` the engine code's Update takes (bits of Group), from this present's stage and live loops mask: none while the
// stage is Off (which also makes the engine code check its sites again when the stage comes back); with the stage on, a group
// stays once its row was on until the stage goes Off (switched off, its cut calls the loop directly and the sites stay in).
inline uint32_t SiteGroups(Stage stage, uint32_t loops) {
  internal::sticky_groups = (stage == Stage::OFF ? 0 : internal::sticky_groups | loops);
  return internal::sticky_groups;
}

// Addon detach, right after the engine code's Uninstall. `sites_in`: a site is still written (the engine code gave up).
inline void Uninstall(bool sites_in) {
  using namespace internal;
  detached.store(true, std::memory_order_relaxed);
  if (shared == nullptr) return;
  if (sites_in || shared->active.load(std::memory_order_acquire) != 0) {
    // A thread may still be in a thunk or the dispatcher: everything stays for the life of the process.
    if (!kept_logged) {
      renodx::utils::log::w("[Total War] Threaded Draw Recording: left in at detach (a game thread may still be in it).");
    }
    kept_logged = true;
    return;
  }
  // S4: no site is in (the engine code zeroes the size when the last comes out): zeroed before the memory goes. On the kept
  // path it stays as it is: a dispatch that may still be recording needs its thunks on the slots.
  shared->arena_size = 0;
  StopPool();
  for (void* memory : {static_cast<void*>(shared), static_cast<void*>(arena), static_cast<void*>(op_memory)}) {
    VirtualFree(memory, 0, MEM_RELEASE);
  }
  shared = nullptr;
  arena = nullptr;
  op_memory = nullptr;
  FreeLibrary(module_reference);
  module_reference = nullptr;
}

#else

struct Shared {};
struct GameCalls {
  bool usable = false;
};
inline Shared* SharedPage() { return nullptr; }
inline void SetGame(const GameCalls&) {}
inline uint32_t SiteGroups(Stage, uint32_t) { return 0; }
inline void Update(const Settings&, bool) {}
inline void Uninstall(bool) {}

#endif

}  // namespace twattila::draw_recording
