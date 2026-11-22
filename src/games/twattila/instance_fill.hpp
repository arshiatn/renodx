/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <cstdint>

#if !defined(_WIN64)
#include <emmintrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/log.hpp"
#include "../twrome2/engine_tweaks.hpp"
#include "./effect_update_sites.hpp"
#include "./worker_pool.hpp"
#if defined(TWATTILA_DRAW_RECORDING)
#include <intrin.h>

#include "./draw_recording.hpp"
#endif
#endif

// Parallel Instance Fill (Attila 1.6.0, x86): the draw passes copy each pass's instance records into the ring buffer's
// mapped memory through the game's job system, 670 wakes a frame for 28 bursts. The 4 places that call it (the call
// sites below) are retargeted at a function of this module that copies the same records with the game's own record copy
// on the shared worker pool (worker_pool.hpp, User::RING_FILL): 64-record chunks claimed by the render thread and the
// woken workers, the render thread leaving when every chunk is done. The copies only read their functor and write slots
// that do not overlap, so the ring's bytes are the game's; a fill below FILL_MIN_ITEMS, or with no worker free, is the
// record copy once on the whole range on the render thread. The game unmaps the ring after the call, as before.
//
// A site is an E8 (the call) whose displacement is rewritten: the new target is a plain cdecl function of this module
// (`(range*, functor*, manager)`, the caller pops 12 bytes; it keeps ebx, esi, edi and ebp, and the game reads nothing
// else after the call). There is no thunk page: a site comes out the same way it goes in, while no thread is inside its
// bytes (PausedWrite; 5, or 11 for an Unmap stub), and the functions stay in this module. While the row is off, or no
// worker is free, a site's function runs the game's own call.
//
// Fill With Draw Recording (the fold, private build: TWATTILA_DRAW_RECORDING; draw_recording.hpp): where a draw
// dispatcher's cut follows a fill (the depth and colour passes and the folded driver), the fill's job stays open to the
// cut and the workers go on to record the dispatcher's chunks. The game's ring Unmap, between the fill and the cut, is
// a 3-site stub (sites 4 to 6, an 11-byte block each: the push, a nop and a call whose return address is the block's
// end) that only notes it; the dispatcher's close makes it after the fill is done. The stubs are a group of their own
// (FOLD_SITES): found, wanted and stood down as one, never taking R1's sites with them.
namespace twattila::instance_fill {

#if !defined(_WIN64)

// The 4 fill sites, by the pass they are in. The visibility and folded fills read their items from the tracker, the
// depth and colour fills from a list.
enum class Site : uint8_t {
  WEIGHTED_VISIBLE_FILL,
  WEIGHTED_DEPTH_FILL,
  WEIGHTED_COLOUR_FILL,
  FOLDED_FILL,
#if defined(TWATTILA_DRAW_RECORDING)
  // The ring's Unmap after the depth, colour and folded fills.
  DEPTH_UNMAP,
  COLOUR_UNMAP,
  FOLDED_UNMAP,
#endif
  COUNT
};
constexpr size_t SITE_COUNT = static_cast<size_t>(Site::COUNT);
constexpr size_t FILL_SITE_COUNT = static_cast<size_t>(Site::FOLDED_FILL) + 1;
constexpr uint32_t ALL_SITES = (1u << SITE_COUNT) - 1;
// The sites that are found, go in and stand down as one (a fill with a site missing would run the game's own call on
// the others' ring): the 4 fills are one group (R1), the Unmap stubs another (the fold), so that a stand-down of one
// never takes the other's.
constexpr uint32_t R1_SITES = (1u << FILL_SITE_COUNT) - 1;
#if defined(TWATTILA_DRAW_RECORDING)
constexpr uint32_t FOLD_SITES = ALL_SITES & ~R1_SITES;
constexpr std::array<uint32_t, 2> GROUPS = {R1_SITES, FOLD_SITES};
// Stub k (the depth, colour and folded Unmap) follows the fill FIRST_STUBBED_FILL + k: every fill but the visibility
// one has one.
constexpr size_t FIRST_STUBBED_FILL = static_cast<size_t>(Site::WEIGHTED_DEPTH_FILL);
constexpr size_t FIRST_UNMAP = static_cast<size_t>(Site::DEPTH_UNMAP);
constexpr size_t FOLDED_STUB = static_cast<size_t>(Site::FOLDED_UNMAP) - FIRST_UNMAP;
static_assert(FIRST_UNMAP == FILL_SITE_COUNT && FOLDED_STUB == SITE_COUNT - FILL_SITE_COUNT - 1);
#else
static_assert(SITE_COUNT == FILL_SITE_COUNT, "the plain build has the 4 fill sites only");
constexpr std::array<uint32_t, 1> GROUPS = {R1_SITES};
#endif
constexpr uint32_t SITE_SIZE = 5;       // a fill call (E8 rel32); SiteInfo::size says what a site writes
constexpr uint32_t MAX_SITE_SIZE = 11;  // the most a site writes

// The game's record copies (thiscall, ecx = the functor, one stack argument: the range; ret 4).
enum class Copy : uint8_t { LIST_ITEMS, TRACKER_ITEMS, COUNT };
constexpr size_t COPY_COUNT = static_cast<size_t>(Copy::COUNT);

// The E8 or E9 at `from` goes to `to`.
struct Link {
  uint32_t from = 0;
  uint32_t to = 0;
};

struct CopyInfo {
  // The game's wrapper the sites call (a jump), the call it ends in (to the parallel loop), and the loop's own inline
  // call of the record copy: with the wrapper's target, they tie each site to the record copy its fill is made of.
  std::array<Link, 3> chain = {};
  std::string_view head;  // the record copy's first bytes
};

struct SiteInfo {
  std::string_view name;
  uint32_t rva = 0;     // the E8
  uint32_t before = 0;  // bytes of the pattern before it
  uint32_t size = SITE_SIZE;  // the bytes it writes, from rva
  // The fold's stubs: the key store (a store of the tracker into the fill's functor) that pins the stub's ecx and esp,
  // found by its bytes too. Empty for the fill sites.
  uint32_t key_rva = 0;
  std::string_view key;
  // The bytes around the call (`??`: its displacement), through the `add esp, 0xc` and the ring's flag test (`cmp byte
  // [ecx + 0x44], 0`) after it.
  std::string_view pattern;
  Copy copy = Copy::LIST_ITEMS;
};

using effect_update::PatternSize;  // the byte count of a pattern (the effect update's sites own the formula)

// The Steam build's RVAs (nothing else is supported: another build fails the bytes check and the row stays out).
constexpr std::array<CopyInfo, COPY_COUNT> COPIES = {{
    {.chain = {{{0x17B9070, 0x17BA780}, {0x17BA78F, 0x17BB960}, {0x17BB98B, 0x17DAB80}}},
     .head = "53 8B 5C 24 08 56 57 8B F9"},
    {.chain = {{{0x17B90E0, 0x17BA840}, {0x17BA84F, 0x17BC7E0}, {0x17BC80B, 0x17DB270}}},
     .head = "53 55 56 8B 74 24 10 57 8B F9"},
}};

constexpr std::array<SiteInfo, SITE_COUNT> SITES = {{
    {.name = "weighted visible fill",
     .rva = 0x17AE71C,
     .before = 8,
     .pattern = "0F 47 CE 50 89 4C 24 44 E8 ?? ?? ?? ?? 8B 4F 24 83 C4 0C 80 79 44 00 75 0B",
     .copy = Copy::TRACKER_ITEMS},
    {.name = "weighted depth fill",
     .rva = 0x17AE818,
     .before = 12,
     .pattern = "0F 47 CE 89 6C 24 54 50 89 4C 24 5C E8 ?? ?? ?? ?? 8B 4F 24 83 C4 0C 80 79 44 00 75 0B",
     .copy = Copy::LIST_ITEMS},
    {.name = "weighted colour fill",
     .rva = 0x17AE8D8,
     .before = 12,
     .pattern = "0F 47 CE 89 6C 24 6C 50 89 4C 24 74 E8 ?? ?? ?? ?? 8B 4F 24 83 C4 0C 80 79 44 00 75 0B",
     .copy = Copy::LIST_ITEMS},
    {.name = "folded fill",
     .rva = 0x17AED1F,
     .before = 8,
     .pattern = "0F 47 CE 50 89 4C 24 54 E8 ?? ?? ?? ?? 8B 4D 24 83 C4 0C 80 79 44 00 75 0B",
     .copy = Copy::TRACKER_ITEMS},
#if defined(TWATTILA_DRAW_RECORDING)
    // The stubs: `mov ecx,[ecx+0x10]; push [reg+0x18]; mov eax,[ecx]; call [eax+0x10]` behind the fill's flag test
    // (`jne` over it). Found by the jne and the block (the fill's own pattern ends at the jne), the folded one also by
    // the tail that clears +0x58 and tests the pixel-selection byte the stub reads.
    {.name = "depth unmap",
     .rva = 0x17AE829,
     .before = 2,
     .size = 11,
     .key_rva = 0x17AE7F9,
     .key = "89 7C 24 40",
     .pattern = "75 0B 8B 49 10 FF 77 18 8B 01 FF 50 10"},
    {.name = "colour unmap",
     .rva = 0x17AE8E9,
     .before = 2,
     .size = 11,
     .key_rva = 0x17AE8B9,
     .key = "89 7C 24 58",
     .pattern = "75 0B 8B 49 10 FF 77 18 8B 01 FF 50 10"},
    {.name = "folded unmap",
     .rva = 0x17AED30,
     .before = 2,
     .size = 11,
     .key_rva = 0x17AED04,
     .key = "89 6C 24 40",
     .pattern = "75 0B 8B 49 10 FF 75 18 8B 01 FF 50 10 8A 44 24 78 C7 45 58 00 00 00 00 84 C0 75 42"},
#endif
}};

// Each fill site's pattern has its call (E8) where `before` says; every site's pattern covers its written bytes, which
// fit MAX_SITE_SIZE, and no two sites' written bytes overlap (their found-by patterns may share bytes: a stub's pattern
// begins where its fill's ends).
static_assert([] {
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    const SiteInfo& info = SITES[index];
    const bool is_fill = (index < FILL_SITE_COUNT);
    if ((is_fill && info.pattern.substr(info.before * 3, 2) != "E8") || (is_fill && info.size != SITE_SIZE)
        || info.before * 3 + info.size * 3 - 1 > info.pattern.size() || info.size > MAX_SITE_SIZE
        || (is_fill != info.key.empty())) {
      return false;
    }
    for (size_t other = index + 1; other < SITE_COUNT; ++other) {
      if (effect_update::Overlaps(info.rva, info.size, SITES[other].rva, SITES[other].size)) {
        return false;
      }
    }
  }
  return true;
}());

// The byte ranges the sites own, one visit each (rva, size): what a site writes (`written`: each site's `size` bytes),
// or what Prepare reads to find the sites (the patterns, the stubs' key stores, the links of each record copy's chain,
// the record copies' heads). The heapfix clearance (engine_code.hpp) and the no-overlap checks against the other
// features' sites (test/tw_instance_fill) are built on it.
template <typename Visit>
constexpr void VisitRanges(bool written, Visit&& visit) {
  for (const SiteInfo& info : SITES) {
    if (written) {
      visit(info.rva, info.size);
    } else {
      visit(info.rva - info.before, static_cast<uint32_t>(PatternSize(info.pattern)));
      if (!info.key.empty()) {
        visit(info.key_rva, static_cast<uint32_t>(PatternSize(info.key)));
      }
    }
  }
  if (written) {
    return;
  }
  for (const CopyInfo& copy : COPIES) {
    for (const Link& link : copy.chain) {
      visit(link.from, 5);
    }
    visit(copy.chain[2].to, static_cast<uint32_t>(PatternSize(copy.head)));
  }
}

// What the game's calls and the record copies take: the range {first item, one past the last, the job grain}.
struct Range {
  uint32_t begin = 0;
  uint32_t end = 0;
  uint32_t grain = 0;
};

// Below this many items the fill is the record copy once on the render thread; above, one more worker for each
// FILL_ITEMS_PER_WORKER (at most what the pool has free for the ring); the chunks are FILL_CHUNK_RECORDS items.
constexpr uint32_t FILL_MIN_ITEMS = 512;
constexpr uint32_t FILL_CHUNK_RECORDS = 64;
constexpr uint32_t FILL_ITEMS_PER_WORKER = 384;

// The tracker's fields the record copies read (+0x44 the record size, +0x48 the ring's mapped memory, +0x84 the items
// of the tracker's own list) and how much of it check mode copies: its scratch fills read the copy, and write where it
// points.
constexpr uint32_t TRACKER_STRIDE = 0x44;
constexpr uint32_t TRACKER_DESTINATION = 0x48;
constexpr uint32_t TRACKER_BYTES = 0x90;
constexpr uint64_t CHECK_MAX_BYTES = 64u << 20;

// The test's switches (TWATTILA_INSTANCE_FILL_CONTROLS, test/tw_instance_fill): each breaks one rule of the dispatcher,
// and the test must see the difference. Constants (false) in every other build.
#if defined(TWATTILA_INSTANCE_FILL_CONTROLS)
// each chunk also copies the first item of the next
static bool control_overlap = false;
// the last chunk is never claimed
static bool control_drop_last = false;
// the dispatcher returns before the join (its job is kept alive for the test)
static bool control_early_return = false;
// the other record copy
static bool control_wrong_copy = false;
// the chunks' ranges count from the first item, as if the copy did not know begin0
static bool control_no_begin0 = false;
// the availability guard is bypassed and a refused Start runs no part
static bool control_refused_skips_part = false;
// the job joins every woken worker (JoinRule::ALL), a late one included
static bool control_all_join = false;
// chunk 1 is claimed and neither copied nor counted
static bool control_skip_chunk = false;
// Rewrite's n-th page made writable fails (0: the first; -1: none)
static int32_t control_fail_protect = -1;
// The fold's fill job is not reset before it opens (the next fold of the same size claims no chunk)
static bool control_fold_no_reset = false;
// The stub makes the game's Unmap at once while the fold is ours
static bool control_unmap_at_once = false;
// The folded stub ignores the pixel-selection byte and the empty round
static bool control_ignore_pixel_byte = false;
// The stub of a key mismatch notes its Unmap in the fold and lets the close make it
static bool control_note_on_mismatch = false;
// Prepare's all-or-none runs over all the sites, not by group (a refused stub takes the fills with it)
static bool control_all_or_none = false;
// Wanted asks for the stubs whether or not their cut is in
static bool control_stubs_without_cut = false;
#else
constexpr bool control_overlap = false;
constexpr bool control_drop_last = false;
constexpr bool control_wrong_copy = false;
constexpr bool control_no_begin0 = false;
constexpr bool control_all_join = false;
constexpr bool control_skip_chunk = false;
constexpr bool control_fold_no_reset = false;
constexpr bool control_unmap_at_once = false;
constexpr bool control_ignore_pixel_byte = false;
constexpr bool control_note_on_mismatch = false;
constexpr bool control_all_or_none = false;
constexpr bool control_stubs_without_cut = false;
#endif

using RecordCopy = void(__thiscall*)(void* functor, Range* range);
using GameFill = void(__cdecl*)(Range* range, void* functor, void* manager);

// A fill job on the render thread's stack: the workers read it until Join (worker_pool.hpp, JoinRule::ENTERED). Every
// participant claims chunks of FILL_CHUNK_RECORDS items from `next_chunk` and counts the ones it finished.
struct FillJob {
  RecordCopy copy = nullptr;
  void* functor = nullptr;
  uint32_t begin = 0;
  uint32_t end = 0;
  uint32_t chunks = 0;
  alignas(64) std::atomic<uint32_t> next_chunk = 0;
  alignas(64) std::atomic<uint32_t> chunks_done = 0;
};

// The pool's work function (worker 0 is the render thread): each chunk is the record copy on its own range, then an
// SFENCE (the destination may be write-combined) and a release increment, so that the thread that sees all chunks done
// sees all the bytes.
inline void FillWork(void* context, uint32_t) {
  auto* job = static_cast<FillJob*>(context);
  for (uint32_t chunk = job->next_chunk.fetch_add(1); chunk < job->chunks; chunk = job->next_chunk.fetch_add(1)) {
    if (control_skip_chunk && chunk == 1) {
      continue;
    }
    const uint32_t first = job->begin + chunk * FILL_CHUNK_RECORDS;
    const uint32_t last = std::min(first + FILL_CHUNK_RECORDS + (control_overlap ? 1u : 0u), job->end);
    const uint32_t shift = (control_no_begin0 ? job->begin : 0u);
    Range range = {.begin = first - shift, .end = last - shift, .grain = FILL_CHUNK_RECORDS};
    job->copy(job->functor, &range);
    _mm_sfence();
    job->chunks_done.fetch_add(1, std::memory_order_release);
  }
}

// One site as Prepare made it: where, the game's bytes, the bytes that go in (the E8 with its displacement at our
// function).
struct PatchedSite {
  uint8_t* at = nullptr;
  uint32_t size = SITE_SIZE;
  std::array<uint8_t, MAX_SITE_SIZE> game = {};
  std::array<uint8_t, MAX_SITE_SIZE> patch = {};
};

namespace internal {

// One fill call of a trace window: kept by the dispatcher (no formatting inside the game's code), printed by Wanted.
struct TraceRecord {
  uint8_t site = 0;
  uint8_t asked = 0;       // workers asked for (0: the fill ran alone)
  uint8_t entered = 0;     // workers that ran the job
  bool pool_busy = false;  // alone: with items enough, but no worker was free
  uint32_t count = 0;
  uint32_t chunks_done = 0;
  uint32_t chunks = 0;
  uint32_t render_ticks = 0;
  uint32_t wait_ticks = 0;
  // A stub's close (the fold closed where the game's Unmap is): 0 none, then why.
  enum class StubClose : uint8_t { NONE, PIXEL_PATH, EMPTY_ROUND, KEY_MISMATCH };
  StubClose stub_close = StubClose::NONE;
};
constexpr size_t TRACE_RECORDS = 256;

static std::array<PatchedSite, SITE_COUNT> sites;
static std::array<GameFill, FILL_SITE_COUNT> game_fills = {};
static std::array<RecordCopy, COPY_COUNT> record_copies = {};
static const uint8_t* base = nullptr;
static bool prepared = false;
// ALL_SITES, or 0 (Prepare), then less as groups stand down: a group goes in whole or not at all
static uint32_t found = 0;
static uint32_t written = 0;  // the sites holding their call's new target
static uint32_t asked = 0;    // what the row last asked for
// The row is on and the sites are found: the dispatcher does its own fill; otherwise it runs the game's call.
static std::atomic<bool> row_on = false;
#if defined(TWATTILA_DRAW_RECORDING)
// The fold row is on, R1 is in and the stubs are found: a fill site may open the fold (the stubs' own bits say where).
static std::atomic<bool> fold_on = false;
// The fold's fill job: it outlives the fill site's call (the render thread's stack does not), one fold at a time.
static FillJob fold_fill;
#endif
// Threads inside the dispatcher (the sites' functions): PausedWrite lets the sites out only while it is 0. It is
// counted from inside Dispatch: a thread between the retargeted call and the count is not seen, which row changes do
// not need (the code stays in this module). Fills run on the present thread, the thread that runs PausedWrite;
// Uninstall runs on the detach path: at process exit no other game thread is left, and on a runtime unload a thread
// between the retargeted call and the count is not seen (a window of a few instructions).
static std::atomic<int32_t> active = 0;
// RENODX_TW_FILL_CHECK=1, read once by Prepare; its summary line needs a pool trace window (none: nothing is said)
static bool check_enabled = false;
static std::atomic<uint32_t> checked = 0;  // the fills compared since the start
static uint32_t check_window = 0;          // the pool trace window whose compared fills are being counted (0: none open)
static uint32_t check_window_start = 0;
static std::atomic<uint32_t> check_mismatches = 0;
static std::atomic<uint32_t> check_skipped = 0;
// The fills that ended with chunks not done since the start: the first one is said at the next present, the total at detach.
static std::atomic<uint32_t> incomplete_fills = 0;
struct IncompleteFill {
  uint8_t site = 0;
  uint32_t count = 0;
  uint32_t chunks_done = 0;
  uint32_t chunks = 0;
  const char* join = "";
};
static IncompleteFill first_incomplete = {};
static std::atomic<bool> first_incomplete_kept = false;
static bool first_incomplete_said = false;
static std::array<TraceRecord, TRACE_RECORDS> trace_records;
static std::atomic<uint32_t> trace_count = 0;

#if defined(TWATTILA_DRAW_RECORDING)
// The draw_recording groups (bits of draw_recording::Group) a fold of fill site 1, 2, 3 records.
constexpr std::array<uint32_t, 3> FOLD_GROUPS = {
    1u << static_cast<uint32_t>(draw_recording::Group::DEPTH_A),
    1u << static_cast<uint32_t>(draw_recording::Group::COLOUR_A),
    (1u << static_cast<uint32_t>(draw_recording::Group::DEPTH_B))
        | (1u << static_cast<uint32_t>(draw_recording::Group::COLOUR_B))};
#endif

// A fill that ended with chunks not done. It runs in the game's call: a count and the first one's numbers, no log, no allocation.
inline void NoteIncomplete(const IncompleteFill& fill) {
  if (incomplete_fills.fetch_add(1, std::memory_order_relaxed) == 0) {
    first_incomplete = fill;
    first_incomplete_kept.store(true, std::memory_order_release);
  }
}

#if defined(TWATTILA_DRAW_RECORDING)
static size_t fold_site = 0;  // the site whose fold is open

// The fold's close found chunks not done (draw_recording's callback: the fold knows no site).
inline void NoteFoldIncomplete(uint32_t chunks_done) {
  NoteIncomplete({.site = static_cast<uint8_t>(fold_site),
                  .count = fold_fill.end - fold_fill.begin,
                  .chunks_done = chunks_done,
                  .chunks = fold_fill.chunks,
                  .join = "fold"});
}
#endif

inline void KeepTrace(const TraceRecord& record) {
  if (const uint32_t slot = trace_count.fetch_add(1); slot < TRACE_RECORDS) {
    trace_records[slot] = record;
  }
}

// The dispatcher: what each site's function does. `site` is a Site's index.
inline void Dispatch(size_t site, Range* range, void* functor, void* manager) {
  active.fetch_add(1);
  if (row_on.load(std::memory_order_relaxed)) {
    const size_t copy_index = static_cast<size_t>(SITES[site].copy);
    const RecordCopy copy = record_copies[(control_wrong_copy ? COPY_COUNT - 1 - copy_index : copy_index)];
    const uint32_t count = range->end - range->begin;
    uint32_t workers = (count >= FILL_MIN_ITEMS ? std::min(worker_pool::Available(worker_pool::User::RING_FILL),
                                                           count / FILL_ITEMS_PER_WORKER)
                                                : 0u);
#if defined(TWATTILA_INSTANCE_FILL_CONTROLS)
    if (control_refused_skips_part && count >= FILL_MIN_ITEMS) {
      workers = std::max(1u, count / FILL_ITEMS_PER_WORKER);
    }
#endif
    const bool tracing = worker_pool::Tracing();
    uint64_t start = (tracing ? worker_pool::Qpc() : 0);
    const auto add_trace = [&](uint32_t entered, uint32_t chunks, uint32_t chunks_done, uint64_t wait_ticks) {
      KeepTrace({.site = static_cast<uint8_t>(site),
                 .asked = static_cast<uint8_t>(workers),
                 .entered = static_cast<uint8_t>(entered),
                 .pool_busy = (workers == 0 && count >= FILL_MIN_ITEMS),
                 .count = count,
                 .chunks_done = chunks_done,
                 .chunks = chunks,
                 .render_ticks = static_cast<uint32_t>(worker_pool::Qpc() - start),
                 .wait_ticks = static_cast<uint32_t>(wait_ticks)});
    };
    if (workers == 0) {
      copy(functor, range);
      if (tracing) {
        add_trace(0, 0, 0, 0);
      }
    } else {
      const uint32_t chunks = (count + FILL_CHUNK_RECORDS - 1) / FILL_CHUNK_RECORDS - (control_drop_last ? 1u : 0u);
      uint32_t chunks_done = 0;
      const auto fill = [&](void* job_functor) {
        FillJob job = {
            .copy = copy, .functor = job_functor, .begin = range->begin, .end = range->end, .chunks = chunks};
#if defined(TWATTILA_INSTANCE_FILL_CONTROLS)
        if (control_early_return) {
          auto* kept = new FillJob{
              .copy = copy, .functor = job_functor, .begin = range->begin, .end = range->end, .chunks = chunks};
          worker_pool::Start({.work = &FillWork,
                              .context = kept,
                              .user = worker_pool::User::RING_FILL,
                              .join = worker_pool::JoinRule::ENTERED,
                              .trace = tracing},
                             workers);
          FillWork(kept, 0);
          return worker_pool::JoinResult{};
        }
        if (control_refused_skips_part) {
          if (worker_pool::Start({.work = &FillWork,
                                  .context = &job,
                                  .user = worker_pool::User::RING_FILL,
                                  .join = worker_pool::JoinRule::ENTERED,
                                  .trace = tracing},
                                 workers)) {
            FillWork(&job, 0);
            worker_pool::Join();
          }
          return worker_pool::JoinResult{};
        }
#endif
        const worker_pool::JoinResult joined =
            worker_pool::Run({.work = &FillWork,
                              .context = &job,
                              .user = worker_pool::User::RING_FILL,
                              .join = (control_all_join ? worker_pool::JoinRule::ALL : worker_pool::JoinRule::ENTERED),
                              .trace = tracing},
                             workers, [&] { FillWork(&job, 0); });
        chunks_done = job.chunks_done.load(std::memory_order_acquire);
        if (chunks_done != chunks) {
          NoteIncomplete({.site = static_cast<uint8_t>(site),
                          .count = count,
                          .chunks_done = chunks_done,
                          .chunks = chunks,
                          .join = (control_all_join ? "ALL" : "ENTERED")});
        }
        return joined;
      };
      if (check_enabled) {
        // Serial and dispatched fills of copies of the tracker and the functor, into scratch memory: the bytes must be
        // equal. The functor's first word is the tracker; the list and the first item (begin0) follow for the list
        // copy.
        std::array<uint32_t, 3> words;
        std::memcpy(words.data(), functor, sizeof(words));
        uint8_t* const tracker = reinterpret_cast<uint8_t*>(words[0]);
        uint32_t stride;
        std::memcpy(&stride, tracker + TRACKER_STRIDE, sizeof(stride));
        const uint64_t bytes = uint64_t{count} * stride;
        const uint32_t first = (SITES[site].copy == Copy::LIST_ITEMS ? words[2] : 0u);
        if (range->begin == first && bytes <= CHECK_MAX_BYTES) {
          std::vector<uint8_t> serial_bytes(bytes);
          std::vector<uint8_t> parallel_bytes(bytes);
          std::array<std::array<uint8_t, TRACKER_BYTES>, 2> trackers;
          std::array<std::array<uint32_t, 3>, 2> functors = {words, words};
          const std::array<uint8_t*, 2> destinations = {serial_bytes.data(), parallel_bytes.data()};
          for (size_t side = 0; side < 2; ++side) {
            std::memcpy(trackers[side].data(), tracker, TRACKER_BYTES);
            const auto destination = reinterpret_cast<uintptr_t>(destinations[side]);
            std::memcpy(trackers[side].data() + TRACKER_DESTINATION, &destination, sizeof(destination));
            functors[side][0] = reinterpret_cast<uint32_t>(trackers[side].data());
          }
          Range whole = *range;
          copy(functors[0].data(), &whole);
          fill(functors[1].data());
          checked.fetch_add(1, std::memory_order_relaxed);
          if (bytes != 0 && std::memcmp(serial_bytes.data(), parallel_bytes.data(), bytes) != 0) {
            if (check_mismatches.fetch_add(1, std::memory_order_relaxed) < 8) {
              renodx::utils::log::w(
                  "[Total War] Parallel Instance Fill check: the dispatched fill differs from the serial one (",
                  SITES[site].name, ", ", count, " items, ", stride, " bytes each)");
            }
          }
        } else {
          check_skipped.fetch_add(1, std::memory_order_relaxed);
        }
      }
      // The trace's render time is the fill's, not the check's two scratch fills.
      if (tracing) {
        start = worker_pool::Qpc();
      }
      bool folded = false;
#if defined(TWATTILA_DRAW_RECORDING)
      // The fold: the fill's job stays open to the cut that follows (the fills with a stub). The fill's functor starts
      // with the tracker, whose ring the game's own flag test decides that its Unmap block runs for, so a fold opens
      // only when it will.
      const size_t stub = site - FIRST_STUBBED_FILL;  // meaningful for the stubbed fills only (the test below)
      if (site >= FIRST_STUBBED_FILL && fold_on.load(std::memory_order_relaxed)
          && (written >> (FIRST_UNMAP + stub) & 1) != 0) {
        namespace dr = draw_recording::internal;
        void* const tracker = *static_cast<void**>(functor);
        void* const ring = *reinterpret_cast<void**>(static_cast<uint8_t*>(tracker) + dr::TRACKER_RING);
        const uint32_t fold_workers = dr::FoldWorkers(FOLD_GROUPS[stub]);
        if (fold_workers != 0 && *(static_cast<const uint8_t*>(ring) + dr::RING_FLAG) == 0) {
          fold_site = site;
          fold_fill.copy = copy;
          fold_fill.functor = functor;
          fold_fill.begin = range->begin;
          fold_fill.end = range->end;
          fold_fill.chunks = chunks;
          if (!control_fold_no_reset) {
            fold_fill.next_chunk.store(0, std::memory_order_relaxed);
            fold_fill.chunks_done.store(0, std::memory_order_relaxed);
          }
          folded = dr::OpenFold(tracker, ring,
                                {.work = &FillWork,
                                 .context = &fold_fill,
                                 .chunks_done = &fold_fill.chunks_done,
                                 .chunks = chunks,
                                 .incomplete = &NoteFoldIncomplete},
                                fold_workers);
        }
      }
#endif
      if (!folded) {
        const worker_pool::JoinResult joined = fill(functor);
        if (tracing) {
          add_trace(joined.ran, chunks, chunks_done, joined.wait_ticks);
        }
      }
    }
  } else {
    game_fills[site](range, functor, manager);
  }
  active.fetch_sub(1);
}

// The sites' functions: the game's call is `(range*, functor*, manager)` (cdecl, the caller pops), the manager ignored
// by the wrappers and passed through when the game's own call runs.
template <size_t Index>
void __cdecl Entry(Range* range, void* functor, void* manager) {
  Dispatch(Index, range, functor, manager);
}
constexpr std::array<GameFill, FILL_SITE_COUNT> ENTRIES = {&Entry<0>, &Entry<1>, &Entry<2>, &Entry<3>};

#if defined(TWATTILA_DRAW_RECORDING)
// What a stub does (`stub` 0 to 2: the depth, colour and folded Unmap). `frame` is the caller's esp before the block's
// push: the folded driver's locals sit at fixed places from it (the pixel-selection byte at +0x78, the round's first
// mesh at +0x34 and last at +0x1C, the values the cut's covered bytes load with the same esp).
inline void Unmap(size_t stub, void* ring, void* argument, const uint8_t* frame) {
  namespace dr = draw_recording::internal;
  active.fetch_add(1);
  void* const buffer = *reinterpret_cast<void**>(static_cast<uint8_t*>(ring) + dr::RING_BUFFER);
  const auto close_at_the_stub = [&](TraceRecord::StubClose why) {
    dr::CloseFold(nullptr, [] {});
    dr::fold_trace = {};
    // Only a trace window keeps (and Wanted logs) a record: the folded driver's pixel path and empty rounds close here
    // every frame.
    if (worker_pool::Tracing()) {
      KeepTrace({.site = static_cast<uint8_t>(FIRST_UNMAP + stub), .stub_close = why});
    }
  };
  if (dr::fold.thread.load(std::memory_order_relaxed) != GetCurrentThreadId()) {
    // No fold of this thread: the game's own Unmap, at once.
    dr::GameUnmap(buffer, argument);
  } else if (dr::fold.ring != ring) {
    // A fold of this thread for another ring (it cannot happen by the key proof): the fold closes with its own state,
    // then this stub's ring is unmapped with its own argument.
    if (control_note_on_mismatch) {
      dr::fold.unmap_buffer = buffer;
      dr::fold.unmap_argument = argument;
      dr::fold.unmap_pending = true;
      dr::CloseFold(nullptr, [] {});
    } else {
      close_at_the_stub(TraceRecord::StubClose::KEY_MISMATCH);
      dr::GameUnmap(buffer, argument);
    }
    dr::FoldFault(&dr::fold_key_mismatches);
  } else if (control_unmap_at_once) {
    dr::GameUnmap(buffer, argument);
  } else {
    dr::fold.unmap_buffer = buffer;
    dr::fold.unmap_argument = argument;
    dr::fold.unmap_pending = true;
    dr::fold.unmap_ticks = (dr::trace.active ? worker_pool::Qpc() : 0);
    if (stub == FOLDED_STUB && !control_ignore_pixel_byte) {
      // The folded driver's cut runs the round's meshes unless the pixel-selection path or an empty round skips it (the
      // thunk's own `ja`, compared unsigned): the fold is closed here then, with no record.
      uint32_t first;
      uint32_t last;
      std::memcpy(&first, frame + 0x34, sizeof(first));
      std::memcpy(&last, frame + 0x1C, sizeof(last));
      if (frame[0x78] != 0) {
        close_at_the_stub(TraceRecord::StubClose::PIXEL_PATH);
      } else if (first > last) {
        close_at_the_stub(TraceRecord::StubClose::EMPTY_ROUND);
      }
    }
  }
  active.fetch_sub(1);
}

// The stubs' functions: fastcall `(ring, unused, the block's pushed argument)`, the callee pops it. The frame is
// computed here, in the function's own body (a helper would read its own return address): the return address, then the
// argument, then the caller's esp.
template <size_t Stub>
void __fastcall UnmapEntry(void* ring, void*, void* argument) {
  Unmap(Stub, ring, argument, static_cast<const uint8_t*>(_AddressOfReturnAddress()) + 2 * sizeof(void*));
}
constexpr std::array<void(__fastcall*)(void*, void*, void*), SITE_COUNT - FILL_SITE_COUNT> UNMAP_ENTRIES = {
    &UnmapEntry<0>, &UnmapEntry<1>, &UnmapEntry<2>};
#endif

}  // namespace internal

// The one byte pair of the shipped build that reads as a branch into a site and is not one: the 0x7C of `mov dword ptr
// [esp + 0x30], edi` (89 7C 24 30 at +17AE6F9) reads as a `jl` to +17AE720, inside the visibility fill's call; read in
// the disassembly.
constexpr std::array<uint32_t, 1> IGNORED_SOURCES = {0x17AE6FA};

// The branches (rel32 jmp, call and jcc; with `short_branches` the rel8 forms too) of `code` (the game's bytes from
// `first_rva`) whose target is inside a site past its first byte, by the branch's RVA: no byte pattern knows the
// instruction boundaries. `relocated`: the RVAs of the image's relocated words, sorted (their bytes are data). Prepare
// scans the 2 KB around each site, the rel32 forms only (the bytes of another tool's patch next to a site would read as
// short branches with random targets); test/tw_instance_fill scans the whole code section with both.
inline size_t BranchesIntoSites(std::span<const uint8_t> code, uint32_t first_rva, std::span<const uint32_t> relocated,
                                bool short_branches, std::vector<uint32_t>* hits = nullptr,
                                uint32_t sites = ALL_SITES) {
  return effect_update::internal::BranchesInto(
      code, first_rva, hits, relocated, short_branches, IGNORED_SOURCES, [sites](uint32_t target) {
        for (size_t index = 0; index < SITE_COUNT; ++index) {
          const SiteInfo& info = SITES[index];
          if ((sites >> index & 1) != 0 && target > info.rva
              && effect_update::Overlaps(target, 1, info.rva, info.size)) {
            return true;
          }
        }
        return false;
      });
}

// Once: each fill site found by its bytes and its call's target, each record copy by its chain and its head, no branch
// of the game into a site, each site's new bytes made (nothing written to the game). One log line. The fill sites are
// all found or none (false: none). The fold's stubs (private build) are found only when the fills are, by their bytes
// and key stores, all or none, with a line of their own: a stub refused leaves the fills as they are.
inline bool Prepare(HMODULE module) {
  using namespace internal;
  prepared = true;
  found = 0;
  base = reinterpret_cast<const uint8_t*>(module);
  const auto sections = tw::engine_tweaks::ModuleSections(module);
  const auto holds = [&](const uint8_t* at, std::string_view pattern) {
    const size_t size = PatternSize(pattern);
    for (const std::string_view memory : sections.code) {
      const auto* begin = reinterpret_cast<const uint8_t*>(memory.data());
      if (at >= begin && size <= memory.size() && at <= begin + memory.size() - size) {
        return tw::engine_tweaks::FindPattern({std::string_view(reinterpret_cast<const char*>(at), size)}, pattern)
               == at;
      }
    }
    return false;
  };
  // The call or jump at `rva` goes to `target_rva`.
  const auto reaches = [&](uint32_t rva, uint32_t target_rva) {
    if (!holds(base + rva, "?? ?? ?? ?? ??")) {
      return false;
    }
    int32_t displacement;
    std::memcpy(&displacement, base + rva + 1, sizeof(displacement));
    return (base[rva] == 0xE8 || base[rva] == 0xE9) && rva + 5 + static_cast<uint32_t>(displacement) == target_rva;
  };

  std::string refused;
  const auto refuse = [&](std::string_view name) { refused += std::format("{}{}", refused.empty() ? "" : ", ", name); };
  for (size_t index = 0; index < COPY_COUNT; ++index) {
    const CopyInfo& copy = COPIES[index];
    const bool good = reaches(copy.chain[0].from, copy.chain[0].to) && reaches(copy.chain[1].from, copy.chain[1].to)
                      && reaches(copy.chain[2].from, copy.chain[2].to) && holds(base + copy.chain[2].to, copy.head);
    if (!good) {
      refuse(index == 0 ? "list record copy" : "tracker record copy");
    }
  }
  for (size_t index = 0; index < FILL_SITE_COUNT; ++index) {
    const SiteInfo& info = SITES[index];
    if (!holds(base + info.rva - info.before, info.pattern)
        || !reaches(info.rva, COPIES[static_cast<size_t>(info.copy)].chain[0].from)) {
      refuse(info.name);
    }
  }
  if (!refused.empty()) {
    renodx::utils::log::w(
        "[Total War] Parallel Instance Fill: left alone (not the game's code as expected: another mod or build): ",
        refused);
    return false;
  }
  const std::vector<uint32_t> relocated = effect_update::internal::RelocatedWords(base);
  // The branches of the game into the bytes of a group's sites, looked for within 2 KB of each of them.
  const auto branches_into = [&](uint32_t group) {
    size_t branches = 0;
    for (size_t index = 0; index < SITE_COUNT; ++index) {
      for (const std::string_view memory : sections.code) {
        const auto* begin = reinterpret_cast<const uint8_t*>(memory.data());
        const uint8_t* at = base + SITES[index].rva;
        if ((group >> index & 1) == 0 || at < begin || at >= begin + memory.size()) {
          continue;
        }
        const uint8_t* from = std::max(begin, at - 0x800);
        const uint8_t* to = std::min(begin + memory.size(), at + 0x800);
        branches += BranchesIntoSites({from, to}, static_cast<uint32_t>(from - base), relocated, false, nullptr, group);
      }
    }
    return branches;
  };
  if (branches_into(R1_SITES) != 0) {
    renodx::utils::log::w("[Total War] Parallel Instance Fill: left alone (the game jumps into the middle of a site)");
    return false;
  }
  uint32_t to_build = R1_SITES;
#if defined(TWATTILA_DRAW_RECORDING)
  std::string stubs_refused;
  for (size_t index = FILL_SITE_COUNT; index < SITE_COUNT; ++index) {
    const SiteInfo& info = SITES[index];
    if (!holds(base + info.rva - info.before, info.pattern) || !holds(base + info.key_rva, info.key)) {
      stubs_refused += std::format("{}{}", stubs_refused.empty() ? "" : ", ", info.name);
    }
  }
  if (!stubs_refused.empty()) {
    renodx::utils::log::w(
        "[Total War] Fill With Draw Recording: left alone (not the game's code as expected: another mod or build): ",
        stubs_refused);
    if (control_all_or_none) {
      return false;
    }
  } else if (branches_into(FOLD_SITES) != 0) {
    renodx::utils::log::w(
        "[Total War] Fill With Draw Recording: left alone (the game jumps into the middle of a stub)");
  } else {
    to_build |= FOLD_SITES;
    renodx::utils::log::i("[Total War] Fill With Draw Recording: ", SITE_COUNT - FILL_SITE_COUNT, " stubs found");
  }
#endif
  for (size_t index = 0; index < COPY_COUNT; ++index) {
    record_copies[index] = reinterpret_cast<RecordCopy>(const_cast<uint8_t*>(base + COPIES[index].chain[2].to));
  }
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    if ((to_build >> index & 1) == 0) {
      continue;
    }
    const SiteInfo& info = SITES[index];
    PatchedSite& site = sites[index];
    site.at = const_cast<uint8_t*>(base) + info.rva;
    site.size = info.size;
    std::memcpy(site.game.data(), site.at, info.size);
    // The call goes at the end of the site: its return address is then the game's next instruction, a place a thread
    // can return to whether this site is in or out. A stub (11 bytes) puts the block's own push (bytes 3 to 5 of the
    // block) and a 3-byte nop before it, so that the call's argument is what the block pushed.
    const size_t call_at = info.size - SITE_SIZE;
    if (call_at != 0) {
      constexpr std::array<uint8_t, 3> NOP = {0x0F, 0x1F, 0x00};
      std::memcpy(site.patch.data(), site.game.data() + 3, 3);
      std::memcpy(site.patch.data() + 3, NOP.data(), NOP.size());
    }
#if defined(TWATTILA_DRAW_RECORDING)
    const uintptr_t entry = (index >= FIRST_UNMAP ? reinterpret_cast<uintptr_t>(UNMAP_ENTRIES[index - FIRST_UNMAP])
                                                  : reinterpret_cast<uintptr_t>(ENTRIES[index]));
#else
    const uintptr_t entry = reinterpret_cast<uintptr_t>(ENTRIES[index]);
#endif
    const uint32_t displacement =
        static_cast<uint32_t>(entry) - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(site.at + info.size));
    site.patch[call_at] = 0xE8;
    std::memcpy(site.patch.data() + call_at + 1, &displacement, sizeof(displacement));
    if (index < FILL_SITE_COUNT) {
      game_fills[index] =
          reinterpret_cast<GameFill>(const_cast<uint8_t*>(base + COPIES[static_cast<size_t>(info.copy)].chain[0].from));
    }
  }
  char value[8] = {};
  check_enabled = (GetEnvironmentVariableA("RENODX_TW_FILL_CHECK", value, sizeof(value)) != 0 && value[0] == '1');
  found = to_build;
  renodx::utils::log::i("[Total War] Parallel Instance Fill: ", FILL_SITE_COUNT, " sites found",
                        (check_enabled ? ", check mode on" : ""));
  return true;
}

// The sites that change for `patched` (the sites to hold the new target): all or none of the found ones, as they stand.
inline uint32_t Changing(uint32_t patched) { return (patched ^ internal::written) & internal::found; }

// PausedWrite's check for one suspended thread: its instruction pointer is inside the bytes of a changing site (past
// its first byte, a place no instruction starts).
inline bool ThreadInside(uintptr_t eip, uint32_t changing) {
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    const auto start = reinterpret_cast<uintptr_t>(internal::sites[index].at);
    if ((changing >> index & 1) != 0 && eip > start && eip < start + internal::sites[index].size) {
      return true;
    }
  }
  return false;
}

// PausedWrite, with every other thread suspended and none inside a site: the changing sites get the new target (or the
// game's bytes back). A site whose bytes are not what this module left or found (another tool's) stands every changing
// site of its group down, and so does a page that cannot be made writable (a site going in); nothing of that group is
// written then. Returns the sites that stood down, by bit. Kept as functions of this module (not inline in PausedWrite,
// as the effect table's are): test/tw_instance_fill runs Rewrite alone on a read and execute page.
inline uint32_t Rewrite(uint32_t patched) {
  using namespace internal;
  const uint32_t changing = Changing(patched);
  uint32_t stood = 0;
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    const auto& expected = ((written >> index & 1) != 0 ? sites[index].patch : sites[index].game);
    if ((changing >> index & 1) != 0 && std::memcmp(sites[index].at, expected.data(), sites[index].size) != 0) {
      stood |= 1u << index;
    }
  }
  // The sites go in whole or not at all, by group: one whose bytes are not the game's stands its group down (R1's fills
  // and the fold's stubs never take each other).
  const auto stand_group_down = [&](size_t site) {
    for (const uint32_t group : GROUPS) {
      if ((group >> site & 1) != 0) {
        stood |= changing & group;
      }
    }
  };
  for (const uint32_t group : GROUPS) {
    if ((stood & patched & group) != 0) {
      stood |= changing & group;
    }
  }
  std::array<DWORD, SITE_COUNT> protection = {};
  uint32_t writable = 0;
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    if (((changing & ~stood) >> index & 1) == 0) {
      continue;
    }
#if defined(TWATTILA_INSTANCE_FILL_CONTROLS)
    const bool forced_failure = (control_fail_protect == 0);
    if (control_fail_protect >= 0) {
      --control_fail_protect;
    }
#else
    constexpr bool forced_failure = false;
#endif
    if (!forced_failure
        && VirtualProtect(sites[index].at, sites[index].size, PAGE_EXECUTE_READWRITE, &protection[index]) != FALSE) {
      writable |= 1u << index;
    } else if ((patched >> index & 1) != 0) {
      // No break: the loop goes on, so that every changing site ends written or stood (Update's bookkeeping needs it
      // when another group's sites change in the same window); the later sites of this group are skipped above.
      stand_group_down(index);
    } else {
      stood |= 1u << index;
    }
  }
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    if (((writable & ~stood) >> index & 1) == 0) {
      continue;
    }
    const auto& bytes = ((patched >> index & 1) != 0 ? sites[index].patch : sites[index].game);
    std::memcpy(sites[index].at, bytes.data(), sites[index].size);
  }
  // Last made, first put back: the sites share pages, and a later site saw the earlier ones' writable state as its old
  // protection.
  for (size_t index = SITE_COUNT; index-- > 0;) {
    DWORD ignored;
    if ((writable >> index & 1) != 0) {
      VirtualProtect(sites[index].at, sites[index].size, protection[index], &ignored);
    }
  }
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  return stood;
}

// Every present, with the row: the sites to ask for (the fills whole while the row is on and they are found, none
// otherwise), the dispatcher's own fill on or off at once (a site still in while the row is off runs the game's call),
// and what a trace window kept (one line per fill call) or check mode counted since the last present. The fold (private
// build): its stubs, a group of their own, are asked for only in a present after the fills were written and are still
// wanted (never in their window) and only as far as their cut is in (`cuts_written`, bit k for stub k, read before the
// window) and stays in (`cuts_target`): a cut or the fills going out takes their stubs out in the same window, a stub
// going in follows them by one present.
inline uint32_t Wanted(bool row, [[maybe_unused]] bool fold_row = false, [[maybe_unused]] uint32_t cuts_written = 0,
                       [[maybe_unused]] uint32_t cuts_target = 0) {
  using namespace internal;
  uint32_t wanted = ((row && (found & R1_SITES) == R1_SITES) ? R1_SITES : 0u);
  row_on.store(wanted != 0, std::memory_order_relaxed);
#if defined(TWATTILA_DRAW_RECORDING)
  const bool fold_ready =
      (fold_row && wanted != 0 && (written & R1_SITES) == R1_SITES && (found & FOLD_SITES) == FOLD_SITES);
  fold_on.store(fold_ready, std::memory_order_relaxed);
  if (fold_ready) {
    wanted |= (control_stubs_without_cut ? ~0u : cuts_written & cuts_target) << FILL_SITE_COUNT & FOLD_SITES;
  }
#endif
  const uint32_t records = std::min<uint32_t>(trace_count.exchange(0, std::memory_order_relaxed), TRACE_RECORDS);
  const double microseconds = 1e6 / static_cast<double>(worker_pool::qpc_frequency);
  for (uint32_t index = 0; index < records; ++index) {
    const TraceRecord& record = trace_records[index];
    if (record.stub_close != TraceRecord::StubClose::NONE) {
      constexpr std::array<const char*, 4> WHY = {"", "the pixel-selection path", "an empty round",
                                                  "another fold's ring"};
      renodx::utils::log::i(
          std::format("[Total War] Fill With Draw Recording (trace window {}): {}: closed at the stub ({})",
                      worker_pool::TraceWindow(), SITES[record.site].name,
                      WHY[static_cast<size_t>(record.stub_close)]));
      continue;
    }
    const std::string head = std::format("[Total War] Parallel Instance Fill (trace window {}): {}, {} items, ",
                                         worker_pool::TraceWindow(), SITES[record.site].name, record.count);
    if (record.asked != 0) {
      renodx::utils::log::i(head,
                            std::format("{} workers asked, {} entered, render {:.1f} us, wait {:.1f} us, chunks {}/{}",
                                        record.asked, record.entered, record.render_ticks * microseconds,
                                        record.wait_ticks * microseconds, record.chunks_done, record.chunks));
    } else {
      renodx::utils::log::i(
          head, std::format("alone ({}), render {:.1f} us", (record.pool_busy ? "no worker free" : "below the minimum"),
                            record.render_ticks * microseconds));
    }
  }
  // Check mode: one line per pool trace window (at the first present after it), not one per present.
  if (worker_pool::Tracing()) {
    if (check_window != worker_pool::TraceWindow()) {
      check_window = worker_pool::TraceWindow();
      check_window_start = checked.load(std::memory_order_relaxed);
    }
  } else if (check_window != 0) {
    if (const uint32_t compared = checked.load(std::memory_order_relaxed) - check_window_start; compared != 0) {
      renodx::utils::log::i("[Total War] Parallel Instance Fill check (trace window ", check_window, "): ", compared,
                            " fills compared, ", check_mismatches.load(std::memory_order_relaxed), " mismatches so far, ",
                            check_skipped.load(std::memory_order_relaxed), " skipped so far");
    }
    check_window = 0;
  }
  if (first_incomplete_kept.load(std::memory_order_acquire) && !first_incomplete_said) {
    first_incomplete_said = true;
    renodx::utils::log::w(std::format(
        "[Total War] Parallel Instance Fill: a fill ended with chunks not done ({}, {} items, chunks {}/{}, join rule {}); "
        "no more of these lines this run, the total is said at detach.",
        SITES[first_incomplete.site].name, first_incomplete.count, first_incomplete.chunks_done, first_incomplete.chunks,
        first_incomplete.join));
  }
  return wanted;
}

#endif  // !defined(_WIN64)

}  // namespace twattila::instance_fill
