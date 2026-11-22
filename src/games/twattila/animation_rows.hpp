/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>

#if !defined(_WIN64)
#include <emmintrin.h>
#include <xmmintrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "../../utils/log.hpp"
#include "./worker_pool.hpp"
#endif

// Animation Rows on Workers (Attila, 32-bit): the animation update's call of the animation texture fill, done on the worker
// pool's threads (worker_pool.hpp, User::ANIMATION_ROWS) with the same results as the game's own code. Engine code
// (engine_code.hpp) puts a stub at that call; the stub reaches `Dispatch` through a word in its data page, 0 while the row is
// off or the site is coming out, and then runs the game's own call.
//
// The game's fill walks the instances [*row, end) of the render list once, in order: per instance it looks at the instance's
// animation, whose cached placement (frame, texture, texel) is valid when the animation was already placed for this texture in
// this frame; if not, the animation needs `need` texels of the texture's current row (or of the next row when they do not fit
// in what is left), the placement is written back to the animation, a 16-byte record {row, used, animation, record word} is
// pushed on the cache's record vector, and the instance gets the texture and the first texel (as a float). It stops when the
// rows of the texture are used up. Then its second half Maps the texture and fills it from the records (the game's code).
//
// Dispatch runs the first half in three parts and calls the game's fill for the second half only:
//   A (gather, any thread, reads only): per item the instance, its animation, the record word and `need`.
//   B (allocate, the render thread, in item order, in the game's memory): the game's loop word for word, in u32 arithmetic,
//     with the same stops, the same placements, the same records (the game's own push when the vector grows) and the texel
//     of every item as the float bits (SSE, this thread's MXCSR, as the game's cvtpd2ps).
//   C (scatter, any thread): the instance words +0x5c (texture) and +0xa4 (texel bits).
// then `*row` is the item B stopped at and the game's fill is called with `end = *row`: its limit is 0, so it runs only the
// second half on the state the game's loop would have left. Bit-exact with the game's own loop: Check mode runs both and
// compares everything.
//
// Schedules: with workers, Bulk (one job per call, one wake: rounds, each A over the round's blocks, then B, then C over what
// B finished; the barrier between the parts of a round is a counter of finished units); with no workers (not the owner thread,
// a job open, the pool empty, the env limit `animation:1`, or fewer than two blocks), the same rounds of one block on this
// thread. Every fallback runs the game's own fill unchanged.
namespace twattila::animation_rows {

// The row's stage.
enum class Stage : uint8_t { OFF, CHECK, ON };

#if !defined(_WIN64)

// The game's routines, as offsets from the module base (Attila's only build; engine_code.hpp checks the fill's place).
constexpr uint32_t FILL_RVA = 0x182e080;
constexpr uint32_t RECORD_PUSH_RVA = 0x2a2f40;
// A block of items is the unit of a worker's claim: 512 items of 16 bytes and their texel words.
constexpr uint32_t BLOCK_ITEMS = 512;
constexpr uint32_t RING_SLOTS = 64;
// Calls with fewer items than this run the game's own loop (the wake and the join of a job cost more than they save).
constexpr uint32_t MIN_ITEMS = 2048;
// Bulk's first round covers at least this many items (and what the last call finished).
constexpr uint32_t FIRST_ROUND_ITEMS = 4096;

// What a call of Dispatch did: ours, or the game's fill for the reason named. BAD_ITEM counts in addition to OURS.
enum class Path : uint8_t { OURS, STAGE_OFF, FORCED, ROW_AT_END, FEW_ITEMS, END_PAST_LIST, TEXTURE_RANGE, VECTOR_NOT_EMPTY, NO_ROWS,
                            RING_BUSY, BAD_ITEM, COUNT };

namespace internal {

// The game's fill: `cache` is its this (ecx), the arguments are the seven the animation update pushes (callee pops them).
using Fill = uint32_t(__thiscall*)(void* cache, void* device, uint32_t force, void* list, uint32_t texture, uint32_t* row, uint32_t end,
                                   uint32_t* out);
// The game's record push (thiscall, ecx = the vector {capacity, count, data}, one stack argument: the 16-byte record).
using Push = void(__thiscall*)(void* vector, const void* record);
// The game's texture's height (thiscall, the texture's virtual function +0x68).
using Height = uint32_t(__thiscall*)(void* texture);

static Fill game_fill = nullptr;
static Push game_push = nullptr;
static std::atomic<Stage> stage = Stage::OFF;
// Set by the first Check mismatch: the row stays off for the session.
static std::atomic<bool> failed = false;
// The ring is one for the process: a call takes it before any path touches it (re-entry, a second thread).
static std::atomic<bool> ring_in_use = false;
static std::array<std::atomic<uint32_t>, static_cast<size_t>(Path::COUNT)> paths = {};
// Items the last call finished: Bulk's first round covers at least as many.
static uint32_t last_items = 0;

#if defined(TWATTILA_WORKER_CONTROLS)
// Test switches (twattila's tests only): each one breaks a rule of the design, and the test expects its comparison to fail.
struct Controls {
  // Calls with fewer items than this run the game's loop (the tests use small calls).
  uint32_t min_items = MIN_ITEMS;
  // Scatter before allocate: the instances get the previous call's texel words.
  bool scatter_before_allocate = false;
  // No ordered mode: a texel of 0xFFFFFFFF with duplicate instances scatters in parallel.
  bool no_ordered_mode = false;
  // The texel words become floats in the scatter units, on the workers, with round-down MXCSR.
  bool convert_in_scatter = false;
  // A vector that grows by itself (not the game's push): the allocator's log differs.
  bool own_vector_growth = false;
  // The room of a row as a signed 64-bit number (the game's is u32 and wraps).
  bool wide_room = false;
  // Allocate the blocks of a round in swapped pairs (1, 0, 3, 2, ...).
  bool swap_allocate_blocks = false;
  // Check restores the instance words in item order (the earliest capture must come last).
  bool forward_restore = false;
  // No check that the call's end is within the list's count.
  bool no_count_check = false;
  // Item (list index) whose texel word gets 1 added (Check must see it); ~0u: none.
  uint32_t corrupt_item = ~0u;
  // The scatter unit of this block (counted from the call's first item) waits this many milliseconds before it writes: the blocks after it land first.
  uint32_t scatter_delay_block = ~0u;
  uint32_t scatter_delay_ms = 0;
};
static Controls controls;
#endif

// A word of an object of the game's.
inline uint32_t& Word(void* object, uint32_t offset) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(object) + offset); }

// What gather finds of an item: the instance, its animation, the instance's record word and the texels the animation needs.
// A null instance or animation is a bad item (allocate stops there, the game's own fill faults on it).
struct Item {
  uint8_t* instance = nullptr;
  uint8_t* animation = nullptr;
  uint32_t record_word = 0;
  uint32_t need = 0;
};
static_assert(sizeof(Item) == 16);

// A block's items and its texel words (float bits, or the raw texel in the scatter-conversion control).
struct alignas(64) Slot {
  std::array<Item, BLOCK_ITEMS> items = {};
  std::array<uint32_t, BLOCK_ITEMS> texels = {};
};
static std::array<Slot, RING_SLOTS> ring;

// Check keeps, per item, what the instance words were before the call (the earliest capture is the true original) and what ours left.
struct Capture {
  uint32_t old_texture = 0;
  uint32_t old_texel = 0;
  uint32_t our_texture = 0;
  uint32_t our_texel = 0;
};
// A placement B wrote: the animation and its three words before and after.
struct Placement {
  uint8_t* animation = nullptr;
  std::array<uint32_t, 3> before = {};
  std::array<uint32_t, 3> ours = {};
};
using Record = std::array<uint32_t, 4>;
struct CheckState {
  std::vector<Capture> captures;
  std::vector<Placement> placements;
  std::vector<Record> records;
  uint32_t calls = 0;
};
static CheckState check;

// What Dispatch left of the last call, for the tests.
struct LastCall {
  uint32_t items = 0;    // finished
  uint32_t records = 0;  // pushed
  uint32_t rounds = 0;
  uint32_t workers = 0;  // asked for
  bool ordered = false;  // a texel of 0xFFFFFFFF switched the scatter to item order
};
static LastCall last_call;

// What a unit of Bulk's claim word is: the parts of a round, or the end of the call.
enum class Kind : uint8_t { NONE, GATHER, SCATTER, DONE };

// The call's shared words. Bulk's claim word: kind (2 bits) | phase counter (14) | end of the phase's blocks (24) | next block
// (24): a worker's claim is one CAS on it, so a worker that read an old phase cannot claim a block of a newer one.
struct Call {
  const uint32_t* instances = nullptr;  // the list's instance pointers from the first item
  uint32_t texture = 0;
  uint32_t total = 0;                   // items in the call: end - first
  Capture* captures = nullptr;          // Check: per item, else null
  uint32_t finished_items = 0;          // written by the render thread before it publishes a scatter phase
  alignas(64) std::atomic<uint64_t> claim = 0;
  alignas(64) std::atomic<uint32_t> done_units = 0;
};

// The next unit of the open phase: NONE when there is none now (or the CAS lost), DONE when the call is over.
inline Kind Claim(Call* call, uint32_t* block) {
  uint64_t word = call->claim.load(std::memory_order_acquire);
  const Kind kind = static_cast<Kind>(word >> 62);
  if (kind == Kind::DONE) return Kind::DONE;
  const uint32_t next = static_cast<uint32_t>(word & 0xFFFFFF);
  if (kind == Kind::NONE || next >= ((word >> 24) & 0xFFFFFF) || !call->claim.compare_exchange_strong(word, word + 1, std::memory_order_acq_rel)) {
    return Kind::NONE;
  }
  *block = next;
  return kind;
}

// The float the game's loop stores in the instance for a texel: u32 to double exactly (cvtdq2pd and the add of 2^32 for the
// sign), then the MXCSR's rounding to float (cvtpd2ps). SSE, never x87, so that the rounding and the sticky flag are the game's.
inline uint32_t TexelBits(uint32_t texel) {
  static constexpr std::array<double, 2> HIGH_BIT = {0.0, 4294967296.0};
  const __m128d exact = _mm_add_sd(_mm_cvtepi32_pd(_mm_cvtsi32_si128(static_cast<int>(texel))), _mm_load_sd(&HIGH_BIT[texel >> 31]));
  return static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_castps_si128(_mm_cvtpd_ps(exact))));
}

// A, one block: reads only. Called by Work (a worker's unit) and Dispatch (the render thread's own units).
inline void Gather(Call* call, uint32_t block) {
  Slot& slot = ring[block % RING_SLOTS];
  const uint32_t first = block * BLOCK_ITEMS;
  const uint32_t count = std::min(BLOCK_ITEMS, call->total - first);
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t item_index = first + i;
    // Two levels ahead: the instance's line holding +0x60 and +0x70 for the item 16 on; the animation's lines for the item 8 on.
    if (item_index + 16 < call->total) {
      _mm_prefetch(reinterpret_cast<const char*>(call->instances[item_index + 16]) + 0x40, _MM_HINT_T0);
    }
    if (item_index + 8 < call->total) {
      if (const uint8_t* near_instance = reinterpret_cast<const uint8_t*>(call->instances[item_index + 8]); near_instance != nullptr) {
        const char* near_animation = reinterpret_cast<const char*>(*reinterpret_cast<const uintptr_t*>(near_instance + 0x60));
        _mm_prefetch(near_animation, _MM_HINT_T0);
        _mm_prefetch(near_animation + 0x1b, _MM_HINT_T0);
      }
    }
    Item& item = slot.items[i];
    item = {.instance = reinterpret_cast<uint8_t*>(call->instances[item_index])};
    if (item.instance == nullptr) continue;
    item.animation = reinterpret_cast<uint8_t*>(*reinterpret_cast<uintptr_t*>(item.instance + 0x60));
    if (item.animation == nullptr) continue;
    item.record_word = Word(item.instance, 0x70);
    // The game's: n, or n + 1 + n / 12 when the animation's flag word is set (a multiply by 0xAAAAAAAB and a shift by 35).
    const uint32_t texels = Word(item.animation, 4);
    item.need = (Word(item.animation, 0xC) != 0 ? texels + 1 + static_cast<uint32_t>((static_cast<uint64_t>(texels) * 0xAAAAAAABull) >> 35) : texels);
    if (call->captures != nullptr) {
      call->captures[item_index].old_texture = std::atomic_ref<uint32_t>(Word(item.instance, 0x5C)).load(std::memory_order_relaxed);
      call->captures[item_index].old_texel = std::atomic_ref<uint32_t>(Word(item.instance, 0xA4)).load(std::memory_order_relaxed);
    }
  }
}

// C, one block: the instance words for the items B finished. Called by Work and Dispatch.
inline void Scatter(Call* call, uint32_t block) {
  const Slot& slot = ring[block % RING_SLOTS];
  const uint32_t first = block * BLOCK_ITEMS;
  const uint32_t count = std::min(BLOCK_ITEMS, call->finished_items - first);
#if defined(TWATTILA_WORKER_CONTROLS)
  if (controls.scatter_delay_block == block) {
    Sleep(controls.scatter_delay_ms);
  }
#endif
  for (uint32_t i = 0; i < count; ++i) {
    if (i + 16 < count) {
      __builtin_prefetch(slot.items[i + 16].instance + 0x5C, 1);
      __builtin_prefetch(slot.items[i + 16].instance + 0xA4, 1);
    }
    uint8_t* instance = slot.items[i].instance;
    uint32_t bits = slot.texels[i];
#if defined(TWATTILA_WORKER_CONTROLS)
    if (controls.convert_in_scatter) {
      const unsigned int saved = _MM_GET_ROUNDING_MODE();
      _MM_SET_ROUNDING_MODE(_MM_ROUND_DOWN);
      bits = TexelBits(bits);
      _MM_SET_ROUNDING_MODE(saved);
    }
#endif
    std::atomic_ref<uint32_t>(Word(instance, 0x5C)).store(call->texture, std::memory_order_relaxed);
    std::atomic_ref<uint32_t>(Word(instance, 0xA4)).store(bits, std::memory_order_relaxed);
  }
}

// The pool's callback: claim units until the call is over. A worker runs no game code and no allocator, and it needs well under 4 KB of stack.
inline void Work(void* context, uint32_t) {
  Call* call = static_cast<Call*>(context);
  for (;;) {
    uint32_t block = 0;
    const Kind kind = Claim(call, &block);
    if (kind == Kind::DONE) return;
    if (kind == Kind::NONE) {
      _mm_pause();
      continue;
    }
    if (kind == Kind::GATHER) {
      Gather(call, block);
    } else {
      Scatter(call, block);
    }
    call->done_units.fetch_add(1, std::memory_order_release);
  }
}

}  // namespace internal

// Engine code's Prepare: where the game's fill is (found and checked there); the record push is at its fixed offset from it.
inline void SetGame(const uint8_t* fill) {
  internal::game_fill = reinterpret_cast<internal::Fill>(const_cast<uint8_t*>(fill));
  internal::game_push = reinterpret_cast<internal::Push>(const_cast<uint8_t*>(fill - FILL_RVA + RECORD_PUSH_RVA));
}

// Every present on the present thread (addon.cpp): the row's stage. A Check mismatch keeps it off.
inline void Update(Stage requested) { internal::stage.store((internal::failed.load() ? Stage::OFF : requested), std::memory_order_relaxed); }

// The stub's target (engine_code.hpp): the stub pushes the seven arguments again and calls it with ecx = the cache, edx unused;
// the callee pops them (ret 0x1c) as the game's own fill does, and the result is the game's (the animation update reads al).
inline uint32_t __fastcall Dispatch(void* cache, void*, void* device, uint32_t force, void* list, uint32_t texture, uint32_t* row,
                                    uint32_t end, uint32_t* out) {
  using namespace internal;
  const auto game = [&](Path path) {
    paths[static_cast<size_t>(path)].fetch_add(1, std::memory_order_relaxed);
    return game_fill(cache, device, force, list, texture, row, end, out);
  };
  const Stage stage_now = stage.load(std::memory_order_relaxed);
  if (stage_now == Stage::OFF) return game(Path::STAGE_OFF);
  if ((force & 0xFF) != 0) return game(Path::FORCED);
  const uint32_t first = *row;
  if (first >= end) return game(Path::ROW_AT_END);
  const uint32_t total = end - first;
#if defined(TWATTILA_WORKER_CONTROLS)
  if (total < controls.min_items) return game(Path::FEW_ITEMS);
#else
  if (total < MIN_ITEMS) return game(Path::FEW_ITEMS);
#endif
  bool count_checked = true;
#if defined(TWATTILA_WORKER_CONTROLS)
  count_checked = !controls.no_count_check;
#endif
  if (count_checked && end > Word(list, 4)) return game(Path::END_PAST_LIST);
  if (texture >= Word(cache, 4)) return game(Path::TEXTURE_RANGE);
  uint32_t* const records = &Word(cache, 0x1C);  // the record vector {capacity, count, data}
  if (records[1] != 0) return game(Path::VECTOR_NOT_EMPTY);
  void* const texture_object = reinterpret_cast<void**>(Word(cache, 8))[texture];
  // The game's limit: the rows the texture has, at most one per item left.
  const uint32_t limit = std::min(reinterpret_cast<Height>((*reinterpret_cast<uintptr_t**>(texture_object))[0x68 / 4])(texture_object), total);
  if (limit == 0) return game(Path::NO_ROWS);
  if (ring_in_use.exchange(true, std::memory_order_acquire)) return game(Path::RING_BUSY);

  const uint32_t blocks = total / BLOCK_ITEMS + (total % BLOCK_ITEMS != 0 ? 1 : 0);
  const uint32_t workers = std::min(worker_pool::Available(worker_pool::User::ANIMATION_ROWS), blocks - 1);
  if (stage_now == Stage::CHECK) {
    check.captures.assign(total, {});
    check.placements.clear();
    ++check.calls;
  }
  Call call = {.instances = *reinterpret_cast<const uint32_t* const*>(static_cast<uint8_t*>(list) + 8) + first,
               .texture = texture,
               .total = total,
               .captures = (stage_now == Stage::CHECK ? check.captures.data() : nullptr)};
  const uint32_t frame = Word(cache, 0x28);
  const uint32_t row_size = Word(cache, 0x10);
  const uint32_t texels_per_row = Word(cache, 0x14);
  const uint32_t entry_out = *out;

  // B's state: the game's row index and the texels used of that row, the items finished, and why it stopped.
  uint32_t row_index = 0;
  uint32_t used = 0;
  uint32_t finished = 0;
  bool stopped = false;
  bool bad_item = false;
  uint32_t ordered_from = ~0u;

  // B over one block: the game's loop word for word (the fast path of an animation placed already, the next-fit row, the
  // record, the placement), on the game's memory.
  const auto allocate = [&](uint32_t block) {
    Slot& slot = ring[block % RING_SLOTS];
    const uint32_t block_first = block * BLOCK_ITEMS;
    const uint32_t count = std::min(BLOCK_ITEMS, total - block_first);
    for (uint32_t i = 0; i < count; ++i) {
      const Item& item = slot.items[i];
      if (item.instance == nullptr || item.animation == nullptr) {
        bad_item = true;
        stopped = true;
        finished = block_first + i;
        return;
      }
      if (i + 32 < count) {
        __builtin_prefetch(slot.items[i + 32].animation + 0x10, 1);
        __builtin_prefetch(slot.items[i + 32].animation + 0x1B, 1);
      }
      uint32_t& placed_frame = Word(item.animation, 0x10);
      uint32_t& placed_texture = Word(item.animation, 0x14);
      uint32_t& placed_texel = Word(item.animation, 0x18);
      uint32_t texel = placed_texel;
      if (!(placed_frame == frame && placed_texture != ~0u && placed_texture == texture && placed_texel != ~0u)) {
        bool next_row = item.need > row_size - used;
#if defined(TWATTILA_WORKER_CONTROLS)
        if (controls.wide_room) {
          next_row = static_cast<int64_t>(item.need) > static_cast<int64_t>(row_size) - static_cast<int64_t>(used);
        }
#endif
        if (next_row) {
          // The row is full (or the item is bigger than a row): the next row, and the loop ends when the texture has no more.
          if (++row_index == limit) {
            stopped = true;
            finished = block_first + i;
            return;
          }
          used = 0;
        }
        texel = texels_per_row * row_index + used * 3;
        const Record record = {row_index, used, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(item.animation)), item.record_word};
#if defined(TWATTILA_WORKER_CONTROLS)
        if (controls.own_vector_growth && records[1] >= records[0]) {
          const uint32_t capacity = records[0] * 2 + 1;
          auto* grown = static_cast<uint8_t*>(VirtualAlloc(nullptr, static_cast<size_t>(capacity) * sizeof(Record), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
          std::memcpy(grown, reinterpret_cast<uint8_t*>(records[2]), static_cast<size_t>(records[1]) * sizeof(Record));
          records[0] = capacity;
          records[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(grown));
        }
#endif
        if (records[1] < records[0]) {
          // The game's push without growth, byte for byte.
          std::memcpy(reinterpret_cast<uint8_t*>(records[2]) + static_cast<size_t>(records[1]) * sizeof(Record), record.data(), sizeof(Record));
          ++records[1];
        } else {
          game_push(records, record.data());
        }
        if (stage_now == Stage::CHECK) {
          check.placements.push_back({.animation = item.animation, .before = {placed_frame, placed_texture, placed_texel}});
        }
        used += item.need;
        placed_frame = frame;
        placed_texture = texture;
        placed_texel = texel;
        // A later duplicate of this instance would place again (the cache is not valid for a texel of all ones), and its words
        // would differ: from this block on the scatter runs in item order.
        bool ordered = true;
#if defined(TWATTILA_WORKER_CONTROLS)
        ordered = !controls.no_ordered_mode;
#endif
        if (texel == ~0u && ordered_from == ~0u && ordered) {
          ordered_from = block;
        }
      }
      uint32_t word = TexelBits(texel);
#if defined(TWATTILA_WORKER_CONTROLS)
      if (controls.corrupt_item == first + block_first + i) {
        ++word;
      }
      if (controls.convert_in_scatter) {
        word = texel;
      }
#endif
      slot.texels[i] = word;
    }
    finished = block_first + count;
  };

  uint32_t phase = 0;
  uint32_t rounds = 0;
  const auto pack_claim = [](Kind kind, uint32_t phase_counter, uint32_t high, uint32_t next) {
    return (static_cast<uint64_t>(kind) << 62) | (static_cast<uint64_t>(phase_counter & 0x3FFF) << 48) | (static_cast<uint64_t>(high) << 24) | next;
  };
  // Publishes a part of a round (kind, blocks [low, high)): the render thread claims units too, then waits for the finished ones.
  const auto run_phase = [&](Kind kind, uint32_t low, uint32_t high) {
    if (low == high) return;
    call.done_units.store(0, std::memory_order_relaxed);
    call.claim.store(pack_claim(kind, ++phase, high, low), std::memory_order_release);
    while (call.done_units.load(std::memory_order_acquire) != high - low) {
      uint32_t block = 0;
      const Kind claimed = Claim(&call, &block);
      if (claimed == Kind::NONE) {
        _mm_pause();
        continue;
      }
      if (claimed == Kind::GATHER) {
        Gather(&call, block);
      } else {
        Scatter(&call, block);
      }
      call.done_units.fetch_add(1, std::memory_order_release);
    }
  };

  const auto part = [&] {
    uint32_t round_blocks = 1;
    if (workers != 0) {
      round_blocks = std::min(RING_SLOTS, (std::min(total, std::max(FIRST_ROUND_ITEMS, last_items)) + BLOCK_ITEMS - 1) / BLOCK_ITEMS);
    }
    for (uint32_t low = 0; low < blocks && !stopped; low += round_blocks, round_blocks = (workers != 0 ? std::min(RING_SLOTS, round_blocks * 2) : 1)) {
      const uint32_t high = std::min(blocks, low + round_blocks);
      ++rounds;
      bool scatter_first = false;
#if defined(TWATTILA_WORKER_CONTROLS)
      scatter_first = controls.scatter_before_allocate;
#endif
      run_phase(Kind::GATHER, low, high);
      call.finished_items = std::min(total, high * BLOCK_ITEMS);
      if (scatter_first) {
        run_phase(Kind::SCATTER, low, high);
      }
      for (uint32_t block = low; block < high && !stopped; ++block) {
        uint32_t chosen = block;
#if defined(TWATTILA_WORKER_CONTROLS)
        if (controls.swap_allocate_blocks && (block ^ 1) >= low && (block ^ 1) < high) {
          chosen = block ^ 1;
        }
#endif
        allocate(chosen);
      }
      if (scatter_first) continue;
      call.finished_items = finished;
      const uint32_t scatter_end = (finished + BLOCK_ITEMS - 1) / BLOCK_ITEMS;
      const uint32_t parallel_end = std::clamp(ordered_from, low, scatter_end);
      run_phase(Kind::SCATTER, low, parallel_end);
      for (uint32_t block = parallel_end; block < scatter_end; ++block) {
        Scatter(&call, block);
      }
    }
    call.claim.store(pack_claim(Kind::DONE, 0, 0, 0), std::memory_order_release);
  };
  if (workers != 0) {
    worker_pool::Run({.work = Work, .context = &call, .user = worker_pool::User::ANIMATION_ROWS, .join = worker_pool::JoinRule::ENTERED}, workers, part);
  } else {
    part();
  }
  ring_in_use.store(false, std::memory_order_release);
  paths[static_cast<size_t>(Path::OURS)].fetch_add(1, std::memory_order_relaxed);
  last_items = finished;
  last_call = {.items = finished, .records = records[1], .rounds = rounds, .workers = workers, .ordered = (ordered_from != ~0u)};
  const uint32_t stop_at = first + finished;
  *row = stop_at;
  if (bad_item) {
    // The game's own loop, from the bad item, faults on it the way it always does.
    paths[static_cast<size_t>(Path::BAD_ITEM)].fetch_add(1, std::memory_order_relaxed);
    return game_fill(cache, device, force, list, texture, row, end, out);
  }
  if (stage_now != Stage::CHECK) {
    return game_fill(cache, device, force, list, texture, row, stop_at, out);
  }

  // Check: ours aside, the state put back, the game's own fill on it, then everything compared.
  const uint32_t our_records = records[1];
  const uint32_t our_capacity = records[0];
  const uint32_t our_data = records[2];
  check.records.resize(our_records);
  std::memcpy(check.records.data(), reinterpret_cast<const uint8_t*>(our_data), static_cast<size_t>(our_records) * sizeof(Record));
  for (uint32_t i = 0; i < finished; ++i) {
    uint8_t* instance = reinterpret_cast<uint8_t*>(call.instances[i]);
    check.captures[i].our_texture = Word(instance, 0x5C);
    check.captures[i].our_texel = Word(instance, 0xA4);
  }
  for (Placement& placement : check.placements) {
    placement.ours = {Word(placement.animation, 0x10), Word(placement.animation, 0x14), Word(placement.animation, 0x18)};
  }
  // The earliest capture of an instance is its true original: restoring in reverse order leaves it last.
  for (uint32_t step = 0; step < finished; ++step) {
    uint32_t i = finished - 1 - step;
#if defined(TWATTILA_WORKER_CONTROLS)
    if (controls.forward_restore) {
      i = step;
    }
#endif
    uint8_t* instance = reinterpret_cast<uint8_t*>(call.instances[i]);
    Word(instance, 0x5C) = check.captures[i].old_texture;
    Word(instance, 0xA4) = check.captures[i].old_texel;
  }
  for (size_t step = check.placements.size(); step-- > 0;) {
    const Placement& placement = check.placements[step];
    Word(placement.animation, 0x10) = placement.before[0];
    Word(placement.animation, 0x14) = placement.before[1];
    Word(placement.animation, 0x18) = placement.before[2];
  }
  *row = first;
  records[1] = 0;
  for (uint32_t i = 0; i < std::min(our_records + 1, our_capacity); ++i) {
    std::memset(reinterpret_cast<uint8_t*>(our_data) + static_cast<size_t>(i) * sizeof(Record), 0xFF, sizeof(Record));
  }
  const uint32_t result = game_fill(cache, device, force, list, texture, row, end, out);

  // The first difference: what, which item, ours, the game's.
  std::string what;
  uint32_t where = 0;
  uint32_t ours = 0;
  uint32_t theirs = 0;
  const auto differ = [&](const char* name, uint32_t index, uint32_t our_value, uint32_t their_value) {
    if (what.empty() && our_value != their_value) {
      what = name;
      where = index;
      ours = our_value;
      theirs = their_value;
    }
  };
  differ("row", 0, stop_at, *row);
  differ("records capacity", 0, our_capacity, records[0]);
  differ("records data", 0, our_data, records[2]);
  const uint32_t* game_records = reinterpret_cast<const uint32_t*>(our_data);
  for (uint32_t i = 0; i < our_records && what.empty(); ++i) {
    for (uint32_t word = 0; word < 4; ++word) {
      differ("record", i * 4 + word, check.records[i][word], game_records[i * 4 + word]);
    }
  }
  if (our_records < our_capacity) {
    differ("record after the last", 0, 0xFFFFFFFFu, game_records[our_records * 4]);
  }
  for (uint32_t i = 0; i < finished && what.empty(); ++i) {
    uint8_t* instance = reinterpret_cast<uint8_t*>(call.instances[i]);
    differ("instance texture", first + i, check.captures[i].our_texture, Word(instance, 0x5C));
    differ("instance texel", first + i, check.captures[i].our_texel, Word(instance, 0xA4));
  }
  for (const Placement& placement : check.placements) {
    for (uint32_t word = 0; word < 3; ++word) {
      differ("placement", word, placement.ours[word], Word(placement.animation, 0x10 + word * 4));
    }
  }
  const uint32_t rise = *out - entry_out;
  differ("out", 0, (rise == 0 || rise == our_records ? rise : our_records), rise);
  if (!what.empty()) {
    renodx::utils::log::w(std::format(
        "[Total War] Animation Rows on Workers: Check found a difference in call {} ({}) at {}: ours {:#x}, the game's {:#x}; the row is off for this session.",
        check.calls, what, where, ours, theirs));
    failed.store(true, std::memory_order_relaxed);
    stage.store(Stage::OFF, std::memory_order_relaxed);
  }
  return result;
}

#else

inline void Update(Stage) {}

#endif

}  // namespace twattila::animation_rows
