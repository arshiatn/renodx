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
// 5 bytes (PausedWrite), and the functions stay in this module. While the row is off, or no worker is free, a site's
// function runs the game's own call.
namespace twattila::instance_fill {

#if !defined(_WIN64)

// The 4 fill sites, by the pass they are in. The visibility and folded fills read their items from the tracker, the
// depth and colour fills from a list.
enum class Site : uint8_t { WEIGHTED_VISIBLE_FILL, WEIGHTED_DEPTH_FILL, WEIGHTED_COLOUR_FILL, FOLDED_FILL, COUNT };
constexpr size_t SITE_COUNT = static_cast<size_t>(Site::COUNT);
constexpr uint32_t ALL_SITES = (1u << SITE_COUNT) - 1;
constexpr uint32_t SITE_SIZE = 5;

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
}};

// Each site's pattern has its call (E8) where `before` says, and no two sites' bytes overlap.
static_assert([] {
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    const SiteInfo& info = SITES[index];
    if (info.pattern.substr(info.before * 3, 2) != "E8" || info.before * 3 + 14 > info.pattern.size()) {
      return false;
    }
    for (size_t other = index + 1; other < SITE_COUNT; ++other) {
      const SiteInfo& next = SITES[other];
      if (info.rva - info.before < next.rva + SITE_SIZE && next.rva - next.before < info.rva + SITE_SIZE) {
        return false;
      }
    }
  }
  return true;
}());

// Whether a byte a site writes (`written`: the 5 bytes of each call) or Prepare reads to find the sites (the patterns,
// the links of each record copy's chain, the record copies' heads) lies in a range of `ranges`: rows with `.rva` and
// `.size`, as heapfix_ranges.hpp's tables and the other features' sites are. The heapfix clearance (engine_code.hpp,
// test/tw_instance_fill) and the no-overlap checks against the other sites tables are built on it.
template <typename Ranges>
constexpr bool TouchesAny(const Ranges& ranges, bool written) {
  const auto touches = [&](uint32_t rva, uint32_t size) {
    for (const auto& range : ranges) {
      if (rva < range.rva + range.size && range.rva < rva + size) {
        return true;
      }
    }
    return false;
  };
  for (const SiteInfo& info : SITES) {
    if (touches(written ? info.rva : info.rva - info.before,
                written ? SITE_SIZE : static_cast<uint32_t>(PatternSize(info.pattern)))) {
      return true;
    }
  }
  if (written) {
    return false;
  }
  for (const CopyInfo& copy : COPIES) {
    for (const Link& link : copy.chain) {
      if (touches(link.from, 5)) {
        return true;
      }
    }
    if (touches(copy.chain[2].to, static_cast<uint32_t>(PatternSize(copy.head)))) {
      return true;
    }
  }
  return false;
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
#else
constexpr bool control_overlap = false;
constexpr bool control_drop_last = false;
constexpr bool control_wrong_copy = false;
constexpr bool control_no_begin0 = false;
constexpr bool control_all_join = false;
constexpr bool control_skip_chunk = false;
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
  std::array<uint8_t, SITE_SIZE> game = {};
  std::array<uint8_t, SITE_SIZE> patch = {};
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
};
constexpr size_t TRACE_RECORDS = 256;

static std::array<PatchedSite, SITE_COUNT> sites;
static std::array<GameFill, SITE_COUNT> game_fills = {};
static std::array<RecordCopy, COPY_COUNT> record_copies = {};
static const uint8_t* base = nullptr;
static bool prepared = false;
static uint32_t found = 0;    // ALL_SITES, or 0: the sites go in whole or not at all
static uint32_t written = 0;  // the sites holding their call's new target
static uint32_t asked = 0;    // what the row last asked for
// The row is on and the sites are found: the dispatcher does its own fill; otherwise it runs the game's call.
static std::atomic<bool> row_on = false;
// Threads inside the dispatcher (the sites' functions): PausedWrite lets the sites out only while it is 0. It is
// counted from inside Dispatch: a thread between the retargeted call and the count is not seen, which row changes do
// not need (the code stays in this module); fills run on the present thread, which is the thread that runs PausedWrite
// and Uninstall.
static std::atomic<int32_t> active = 0;
static bool check_enabled = false;  // RENODX_TW_FILL_CHECK=1, read once by Prepare
static std::atomic<uint32_t> checked = 0;
static std::atomic<uint32_t> check_mismatches = 0;
static std::atomic<uint32_t> check_skipped = 0;
static std::atomic<uint32_t> incomplete_fills = 0;
static std::array<TraceRecord, TRACE_RECORDS> trace_records;
static std::atomic<uint32_t> trace_count = 0;

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
      if (const uint32_t slot = trace_count.fetch_add(1); slot < TRACE_RECORDS) {
        trace_records[slot] = {.site = static_cast<uint8_t>(site),
                               .asked = static_cast<uint8_t>(workers),
                               .entered = static_cast<uint8_t>(entered),
                               .pool_busy = (workers == 0 && count >= FILL_MIN_ITEMS),
                               .count = count,
                               .chunks_done = chunks_done,
                               .chunks = chunks,
                               .render_ticks = static_cast<uint32_t>(worker_pool::Qpc() - start),
                               .wait_ticks = static_cast<uint32_t>(wait_ticks)};
      }
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
          incomplete_fills.fetch_add(1, std::memory_order_relaxed);
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
      const worker_pool::JoinResult joined = fill(functor);
      if (tracing) {
        add_trace(joined.ran, chunks, chunks_done, joined.wait_ticks);
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
constexpr std::array<GameFill, SITE_COUNT> ENTRIES = {&Entry<0>, &Entry<1>, &Entry<2>, &Entry<3>};

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
                                bool short_branches, std::vector<uint32_t>* hits = nullptr) {
  size_t count = 0;
  for (size_t at = 0; at < code.size(); ++at) {
    const uint8_t opcode = code[at];
    size_t length = 0;
    int32_t displacement = 0;
    if ((opcode == 0xE8 || opcode == 0xE9) && at + 5 <= code.size()) {
      length = 5;
      std::memcpy(&displacement, code.data() + at + 1, sizeof(displacement));
    } else if (opcode == 0x0F && at + 6 <= code.size() && (code[at + 1] & 0xF0) == 0x80) {
      length = 6;
      std::memcpy(&displacement, code.data() + at + 2, sizeof(displacement));
    } else if (short_branches && at + 2 <= code.size()
               && (opcode == 0xEB || (opcode & 0xF0) == 0x70 || (opcode >= 0xE0 && opcode <= 0xE3))) {
      length = 2;
      displacement = static_cast<int8_t>(code[at + 1]);
    } else {
      continue;
    }
    const uint32_t source = first_rva + static_cast<uint32_t>(at);
    const auto word = std::ranges::lower_bound(relocated, source - 3);
    if (word != relocated.end() && *word < source + length) {
      continue;
    }
    const uint32_t target = source + static_cast<uint32_t>(length) + static_cast<uint32_t>(displacement);
    if (std::ranges::find(IGNORED_SOURCES, source) != IGNORED_SOURCES.end()) {
      continue;
    }
    for (const SiteInfo& info : SITES) {
      if (target <= info.rva || target >= info.rva + SITE_SIZE) {
        continue;
      }
      ++count;
      if (hits != nullptr) {
        hits->push_back(source);
      }
    }
  }
  return count;
}

// Once: each site found by its bytes and its call's target, each record copy by its chain and its head, no branch of
// the game into a site, each site's new bytes made (nothing written to the game). One log line. The sites are all found
// or none.
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
  for (const SiteInfo& info : SITES) {
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
  size_t branches = 0;
  const std::vector<uint32_t> relocated = effect_update::internal::RelocatedWords(base);
  for (const SiteInfo& info : SITES) {
    for (const std::string_view memory : sections.code) {
      const auto* begin = reinterpret_cast<const uint8_t*>(memory.data());
      const uint8_t* at = base + info.rva;
      if (at < begin || at >= begin + memory.size()) {
        continue;
      }
      const uint8_t* from = std::max(begin, at - 0x800);
      const uint8_t* to = std::min(begin + memory.size(), at + 0x800);
      branches += BranchesIntoSites({from, to}, static_cast<uint32_t>(from - base), relocated, false);
    }
  }
  if (branches != 0) {
    renodx::utils::log::w("[Total War] Parallel Instance Fill: left alone (the game jumps into the middle of a site)");
    return false;
  }
  for (size_t index = 0; index < COPY_COUNT; ++index) {
    record_copies[index] = reinterpret_cast<RecordCopy>(const_cast<uint8_t*>(base + COPIES[index].chain[2].to));
  }
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    const SiteInfo& info = SITES[index];
    PatchedSite& site = sites[index];
    site.at = const_cast<uint8_t*>(base) + info.rva;
    std::memcpy(site.game.data(), site.at, SITE_SIZE);
    const auto entry = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ENTRIES[index]));
    const uint32_t displacement = entry - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(site.at + SITE_SIZE));
    site.patch[0] = 0xE8;
    std::memcpy(site.patch.data() + 1, &displacement, sizeof(displacement));
    game_fills[index] =
        reinterpret_cast<GameFill>(const_cast<uint8_t*>(base + COPIES[static_cast<size_t>(info.copy)].chain[0].from));
  }
  char value[8] = {};
  check_enabled = (GetEnvironmentVariableA("RENODX_TW_FILL_CHECK", value, sizeof(value)) != 0 && value[0] == '1');
  found = ALL_SITES;
  renodx::utils::log::i("[Total War] Parallel Instance Fill: ", SITE_COUNT, " sites found",
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
    if ((changing >> index & 1) != 0 && eip > start && eip < start + SITE_SIZE) {
      return true;
    }
  }
  return false;
}

// PausedWrite, with every other thread suspended and none inside a site: the changing sites get the new target (or the
// game's bytes back). A site whose bytes are not what this module left or found (another tool's) stands every changing
// site down, and so does a page that cannot be made writable (a site going in); nothing is written then. The sites that
// stood down, by bit.
inline uint32_t Rewrite(uint32_t patched) {
  using namespace internal;
  const uint32_t changing = Changing(patched);
  uint32_t stood = 0;
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    const auto& expected = ((written >> index & 1) != 0 ? sites[index].patch : sites[index].game);
    if ((changing >> index & 1) != 0 && std::memcmp(sites[index].at, expected.data(), SITE_SIZE) != 0) {
      stood |= 1u << index;
    }
  }
  if ((stood & patched) != 0) {
    stood = changing;
  }
  std::array<DWORD, SITE_COUNT> protection = {};
  uint32_t writable = 0;
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    if (((changing & ~stood) >> index & 1) == 0) {
      continue;
    }
    if (VirtualProtect(sites[index].at, SITE_SIZE, PAGE_EXECUTE_READWRITE, &protection[index]) != FALSE) {
      writable |= 1u << index;
    } else if ((patched >> index & 1) != 0) {
      stood = changing;
      break;
    } else {
      stood |= 1u << index;
    }
  }
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    if (((writable & ~stood) >> index & 1) == 0) {
      continue;
    }
    const auto& bytes = ((patched >> index & 1) != 0 ? sites[index].patch : sites[index].game);
    std::memcpy(sites[index].at, bytes.data(), SITE_SIZE);
  }
  // Last made, first put back: the sites share pages, and a later site saw the earlier ones' writable state as its old
  // protection.
  for (size_t index = SITE_COUNT; index-- > 0;) {
    DWORD ignored;
    if ((writable >> index & 1) != 0) {
      VirtualProtect(sites[index].at, SITE_SIZE, protection[index], &ignored);
    }
  }
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  return stood;
}

// Every present, with the row: the sites to ask for (all of them while the row is on and they are found, none
// otherwise), the dispatcher's own fill on or off at once (a site still in while the row is off runs the game's call),
// and what a trace window kept (one line per fill call) or check mode counted since the last present.
inline uint32_t Wanted(bool row) {
  using namespace internal;
  const bool wanted = (row && found == ALL_SITES);
  row_on.store(wanted, std::memory_order_relaxed);
  const uint32_t records = std::min<uint32_t>(trace_count.exchange(0, std::memory_order_relaxed), TRACE_RECORDS);
  const double microseconds = 1e6 / static_cast<double>(worker_pool::qpc_frequency);
  for (uint32_t index = 0; index < records; ++index) {
    const TraceRecord& record = trace_records[index];
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
  if (const uint32_t compared = checked.exchange(0, std::memory_order_relaxed); compared != 0) {
    renodx::utils::log::i("[Total War] Parallel Instance Fill check: ", compared, " fills compared, ",
                          check_mismatches.load(std::memory_order_relaxed), " mismatches so far, ",
                          check_skipped.load(std::memory_order_relaxed), " skipped so far");
  }
  if (const uint32_t incomplete = incomplete_fills.exchange(0, std::memory_order_relaxed); incomplete != 0) {
    renodx::utils::log::w("[Total War] Parallel Instance Fill: ", incomplete, " fills ended with chunks not done");
  }
  return (wanted ? ALL_SITES : 0u);
}

#endif  // !defined(_WIN64)

}  // namespace twattila::instance_fill
