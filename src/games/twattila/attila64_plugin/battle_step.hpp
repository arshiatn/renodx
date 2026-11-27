/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <type_traits>

#include "loader_api.h"
#include "plugin_block.h"

// ROW_BATTLE_STEP, "Faster Battle Step": the battle step's pair resolver with the same results and fewer calls.
//
// Each battle step the game hands a group manager the pairs of soldiers that came close. For a pair with exactly one
// soldier already in a group, the game asks every group of the manager's two lists (the groups made this step, then
// the older ones) whether the pair joins it, one virtual call per group, until one says yes. A group of the two kinds
// below answers no with loads only unless it holds one of the two soldiers, and almost every group holds neither: in
// a big battle that search is most of the resolver's time.
//
// ResolvePairs runs the game's resolver as transcribed from its code (every sort, virtual call, append and store the
// game makes, in its order, each field read again where the game reads it again) with one difference: for such a pair
// it calls only the groups that hold one of the two soldiers, in list order, found through an index from soldier id to
// the groups that hold it. A call left out is a call that changes nothing, so the state after the function is the
// game's, bit for bit. The index is this plugin's static memory: the game's heap sees the game's own allocations in
// the game's order. Whatever the reasoning does not cover runs the game's own loop over every group instead (`plain`
// below, or the game's function itself): another manager or group kind, an id past the game's own id table, a full
// node pool, a group whose members changed in a way the game's code cannot, and a call that came in while one runs.
//
// What the reasoning stands on is checked: CheckGameCode reads the game's functions and tables it is about, at init
// and at the game's start; with any of them different the hook only calls the game's function.
//
// The index (prior art, and where this differs):
// - A chain per soldier of the groups that hold it is Box2D's per-body contact edge list (b2ContactManager::AddPair
//   walks one body's edges instead of every contact; Erin Catto, MIT). PhysX's broad phase keys a hash by the pair
//   (BpBroadPhaseShared.cpp PairManagerData, BSD-3-Clause), which answers "does this pair exist", not "which groups hold
//   this soldier", so it does not fit.
// - A generation stamp per entry instead of clearing the table each call is ours, and common practice (Briggs and
//   Torczon's sparse sets, 1993, reach the same end by cross-checking two arrays).
// - Ours: the candidates are visited in ascending list position, the new list before the older one, because the
//   game's order of calls is part of the result; and nothing is allocated (fixed arrays, rebuilt per call).
namespace twattila::attila64_plugin::battle_step {

// The hot manager's table, and the two group kinds whose join test is the function read (CODE_RANGES below).
inline constexpr uintptr_t MANAGER_VPTR = 0x37cf250;
inline constexpr std::array<uintptr_t, 2> GROUP_VPTRS = {0x37cf110, 0x37cf1b0};
// The game's: its id table holds this many bits, and a group's member array holds five.
inline constexpr uint32_t ID_LIMIT = 65536;
inline constexpr uint32_t MEMBER_LIMIT = 5;
// Ours; both decide speed only. A pair with more candidate groups in a list runs the game's loop for that list. The
// pool is 2 MB and holds a battle of 52428 groups at the start of a call (five nodes a group at most, then two a pair);
// past it the call runs the game's loop or the game's function.
inline constexpr uint32_t CANDIDATE_LIMIT = 16;
inline constexpr uint32_t NODE_CAPACITY = 1u << 18;

// The manager.
inline constexpr size_t MANAGER_PAIR_COUNT = 0x1c;  // a uint32_t
inline constexpr size_t MANAGER_PAIRS = 0x20;       // pair pointers
// What the function resets at its end: the pair storage's first block and current block, a block's used count, and a
// table of pointers with its count.
inline constexpr size_t MANAGER_STORE_FIRST = 0x30;
inline constexpr size_t MANAGER_STORE_CURRENT = 0x38;
inline constexpr size_t STORE_USED = 0x5dc8;
inline constexpr size_t MANAGER_TABLE_COUNT = 0x6c;
inline constexpr size_t MANAGER_TABLE = 0x70;
// Its two group lists, the new one first as the game asks them; a list's count and group pointers.
inline constexpr std::array<size_t, 2> MANAGER_LISTS = {0xc0, 0xa8};
inline constexpr uint32_t NEW_LIST = 0;
inline constexpr uint32_t OLD_LIST = 1;
inline constexpr size_t LIST_COUNT = 0xc;
inline constexpr size_t LIST_GROUPS = 0x10;
inline constexpr size_t MANAGER_ID_BITS = 0xd8;  // a pointer to the bits of the soldiers that are in a group
// A group's started mark, its members (MEMBER_LIMIT pointers) and their count, a soldier's id, a pair's two soldiers.
inline constexpr size_t GROUP_STARTED = 0x18;  // a byte: a started group takes no more members
inline constexpr size_t GROUP_MEMBERS = 0x20;
inline constexpr size_t GROUP_MEMBER_COUNT = 0x48;
inline constexpr size_t SOLDIER_ID = 0x54;
inline constexpr size_t PAIR_SECOND = 0x8;
// Virtual slots: the manager's new group for a pair, taking a group back, whether a soldier may be in a group and
// whether a pair counts; a group's own "take me back" answer and its join test.
inline constexpr size_t MANAGER_CREATE_SLOT = 0x18 / sizeof(void*);
inline constexpr size_t MANAGER_RETIRE_SLOT = 0x20 / sizeof(void*);
inline constexpr size_t MANAGER_VALID_SLOT = 0x28 / sizeof(void*);
inline constexpr size_t MANAGER_PAIR_TEST_SLOT = 0x40 / sizeof(void*);
inline constexpr size_t GROUP_RETIRE_TEST_SLOT = 0x20 / sizeof(void*);
inline constexpr size_t GROUP_JOIN_SLOT = 0x30 / sizeof(void*);
// The game's functions the resolver calls directly: the pair sort's two parts and a list's append.
inline constexpr uintptr_t SORT_PAIRS = 0x7ea860;
inline constexpr uintptr_t FINISH_PAIR_SORT = 0x7ea9c0;
inline constexpr uintptr_t APPEND_GROUP = 0x7e0570;

// Typed noexcept like hooks.hpp's: a C++ exception in game code cannot pass a frame of ours, and no frame above the
// resolver catches (the generator's catch gate on its site). A game bool comes back in the low byte of a register
// whose other bits are not set by the game, so each is taken as a whole register and its low byte tested.
using SortPairsFn = void(ATTILA64_GAME_CALL*)(void*** first, void*** last, uint64_t depth) noexcept;
using FinishPairSortFn = void(ATTILA64_GAME_CALL*)(void*** first, void*** last) noexcept;
using AppendGroupFn = void(ATTILA64_GAME_CALL*)(void* list, void* group) noexcept;
using ManagerTestFn = uintptr_t(ATTILA64_GAME_CALL*)(void* manager, void* pair_or_soldier) noexcept;
using CreateGroupFn = void*(ATTILA64_GAME_CALL*)(void* manager, void* first, void* second) noexcept;
using RetireGroupFn = void(ATTILA64_GAME_CALL*)(void* manager, void* group) noexcept;
using GroupRetireTestFn = uintptr_t(ATTILA64_GAME_CALL*)(void* group) noexcept;
using GroupJoinFn = uintptr_t(ATTILA64_GAME_CALL*)(void* group, void* first, void* second) noexcept;

// The game code the reasoning is about, as FNV-1a (64 bits) of its bytes in memory: the resolver after the five bytes
// the hook replaces, the join test, the single join it calls, the "may join once started" answer, the new group's
// creation and constructor, the manager's soldier test with its two getters, the list append and the pair test.
struct CodeRange {
  uint32_t address = 0;
  uint32_t length = 0;
  uint64_t hash = 0;
};
inline constexpr uint32_t RESOLVER = 0x79a690;
inline constexpr uint32_t HOOK_BYTES = 5;
inline constexpr std::array<CodeRange, 11> CODE_RANGES = {{
    {.address = RESOLVER + HOOK_BYTES, .length = 0x328, .hash = 0x27126785e1b2538d},
    {.address = 0x79f090, .length = 0x40, .hash = 0x02b50d57564898bb},
    {.address = 0x79f210, .length = 0x76, .hash = 0x53709d1f15bb11d3},
    {.address = 0x7a84a0, .length = 3, .hash = 0x437eaa18167ba675},
    {.address = 0x79ab10, .length = 0x242, .hash = 0x500327722dc233b1},
    {.address = 0x796110, .length = 0x114, .hash = 0xcc5f5f0935766b67},
    {.address = 0x79a240, .length = 0xba, .hash = 0x650474557d6cf5c1},
    {.address = 0x7e0570, .length = 0xb1, .hash = 0xd2aabb0a40df7f5a},
    {.address = 0x7e6070, .length = 3, .hash = 0x437eaa18167ba675},
    {.address = 0x7e6d50, .length = 4, .hash = 0xda93a4da712ddb7f},
    {.address = 0x7e6180, .length = 3, .hash = 0xa3703b1a918cfbeb},  // the pair test: ours, the design lists ten
}};
// The table words that lead to that code: both group kinds' join test, single join and answer, the constructor's own
// table's single join, and the manager's create, retire, soldier test and pair test.
struct TableWord {
  uint32_t address = 0;
  uint32_t value = 0;
};
inline constexpr std::array<TableWord, 11> TABLE_WORDS = {{
    {.address = 0x37cf110 + 0x30, .value = 0x79f090},
    {.address = 0x37cf1b0 + 0x30, .value = 0x79f090},
    {.address = 0x37cf110 + 0x70, .value = 0x79f210},
    {.address = 0x37cf1b0 + 0x70, .value = 0x79f210},
    {.address = 0x37cf110 + 0x78, .value = 0x7a84a0},
    {.address = 0x37cf1b0 + 0x78, .value = 0x7a84a0},
    {.address = 0x37cf3b0 + 0x70, .value = 0x79f210},
    {.address = 0x37cf250 + 0x18, .value = 0x79ab10},
    {.address = 0x37cf250 + 0x20, .value = 0x79ad60},
    {.address = 0x37cf250 + 0x28, .value = 0x79a240},
    {.address = 0x37cf250 + 0x40, .value = 0x7e6180},
}};

// Why a call, or the rest of one, ran the game's own loop over every group; each has one log line, once.
enum Reason : uint8_t {
  REASON_NONE,
  REASON_TYPE,     // a group of another kind, with more members than the member array holds, or listed twice
  REASON_ID,       // a member's id past the game's id table
  REASON_POOL,     // more groups than the node pool holds
  REASON_MEMBERS,  // a called group lost or changed a member, or a list changed under the loop
  REASON_NESTED,   // the resolver was entered again while a call ran
  REASON_COUNT
};
inline constexpr std::array<const char*, REASON_COUNT> REASON_WORDS = {"", "type", "id", "pool", "changed members",
                                                                       "nested"};

// The groups that hold a soldier: a chain of nodes per id, each naming a list and a position in it.
struct Entry {
  uint32_t generation = 0;  // State::generation of the call that wrote it; any other: no chain
  uint32_t head = 0;
};
struct Node {
  uint32_t next = 0;
  uint32_t where = 0;  // the list << 31 | the position
};
inline constexpr uint32_t NO_NODE = UINT32_MAX;
inline constexpr uint32_t WHERE_LIST_SHIFT = 31;
struct Index {
  std::array<Entry, ID_LIMIT> entries = {};
  std::array<Node, NODE_CAPACITY> nodes = {};
};
// `busy` is held by the one call that runs the transcription; the other fields but the two atomics beside it are
// that call's. Two atomics, not a lock: `busy` must tell a second entry by the same thread (the resolver calling game
// code that calls the resolver), which a try-lock by a lock's own holder cannot, and `nested` is that entry's whole
// message to the call that runs. `code_read` is written at init and at the game's start, before the game's threads.
struct State {
  std::atomic<bool> busy = false;
  std::atomic<bool> nested = false;     // set by any entry that found `busy`; read after each game call
  std::atomic<bool> code_read = false;  // CheckGameCode held, at init and at the game's start
  // The tests' fake tables stand in here.
  uintptr_t manager_vptr = MANAGER_VPTR;
  std::array<uintptr_t, 2> group_vptrs = GROUP_VPTRS;
  uint32_t generation = 0;
  uint32_t node_count = 0;
  uint32_t reasons_logged = 0;  // bit per Reason
  bool partly = false;          // sticky: a call ran the game's loop for a Reason
  bool active_logged = false;
};
static State state;
static Index index;  // 2.5 MB, zero until used
static_assert(std::is_trivially_destructible_v<State> && std::is_trivially_destructible_v<Index>);

template <typename T>
[[nodiscard]] static T& At(void* base, size_t offset) noexcept {
  return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}
template <typename Fn>
[[nodiscard]] static Fn Slot(void* object, size_t slot) noexcept {
  return reinterpret_cast<Fn>(At<void**>(object, 0)[slot]);
}

// FNV-1a, 32 or 64 bits, with the reference's parameters (Fowler, Noll and Vo; public domain): the value of `count`
// bytes from the running value `hash` on. One copy for every code check of the plugin (the grass draw's is the 32-bit
// one, both rows' here the 64-bit one) and for the tests, which pin the same values on the game file.
// No other width has parameters here: a narrower one would cut the prime short.
template <typename Hash>
concept FnvHash = std::same_as<Hash, uint32_t> || std::same_as<Hash, uint64_t>;
template <FnvHash Hash>
inline constexpr Hash FNV_OFFSET_BASIS =
    static_cast<Hash>((sizeof(Hash) == sizeof(uint64_t)) ? 0xcbf29ce484222325 : 0x811c9dc5);
template <FnvHash Hash>
inline constexpr Hash FNV_PRIME = static_cast<Hash>((sizeof(Hash) == sizeof(uint64_t)) ? 0x100000001b3 : 0x01000193);
template <FnvHash Hash>
[[nodiscard]] static Hash Fnv1a(const uint8_t* bytes, size_t count, Hash hash = FNV_OFFSET_BASIS<Hash>) noexcept {
  for (size_t offset = 0; offset < count; ++offset) {
    hash = (hash ^ bytes[offset]) * FNV_PRIME<Hash>;
  }
  return hash;
}

// A row's lane of engine_status (the battle step's unless the contact split names its own): written only on a change,
// the other lanes kept.
static void SetLane(Block* block, EngineState lane, uint32_t shift = ENGINE_STEP_SHIFT) noexcept {
  uint32_t status = block->engine_status.load(std::memory_order_relaxed);
  do {
    if (((status >> shift) & ENGINE_LANE_MASK) == lane) return;
  } while (!block->engine_status.compare_exchange_weak(
      status, (status & ~(ENGINE_LANE_MASK << shift)) | (uint32_t{lane} << shift), std::memory_order_relaxed));
}

// Whether the game's code and tables in memory are the ones read; the first that is not gets the row's one line.
[[nodiscard]] static bool CheckGameCode(const LoaderApi& loader) noexcept {
  for (const CodeRange& range : CODE_RANGES) {
    if (Fnv1a<uint64_t>(reinterpret_cast<const uint8_t*>(uintptr_t{range.address}), range.length) != range.hash) {
      loader.log("renodx_attila64: battle step: game code differs (%#x+%#x)", range.address, range.length);
      return false;
    }
  }
  for (const TableWord& word : TABLE_WORDS) {
    if (*reinterpret_cast<const uint64_t*>(uintptr_t{word.address}) != word.value) {
      loader.log("renodx_attila64: battle step: game code differs (%#x+%#x)", word.address,
                 static_cast<uint32_t>(sizeof(uint64_t)));
      return false;
    }
  }
  return true;
}

// The pair resolver for `manager`: true = done here with the game's results, false = the caller runs the game's
// function (nothing of the game was read for writing or called before a false).
[[nodiscard]] static bool ResolvePairs(void* manager, Block* block, const LoaderApi& loader) noexcept {
  // First, before any other test: an entry that fails one below still tells the call that runs.
  if (state.busy.load(std::memory_order_relaxed)) {
    state.nested.store(true);
  }
  if (!state.code_read.load(std::memory_order_relaxed)) return false;
  if ((block->core_wanted.load(std::memory_order_relaxed) & CORE_STEP) == 0) {
    SetLane(block, ENGINE_OFF);
    return false;
  }
  if (((block->engine_status.load(std::memory_order_relaxed) >> ENGINE_STEP_SHIFT) & ENGINE_LANE_MASK) == ENGINE_OFF) {
    SetLane(block, ENGINE_NOT_USED_YET);
  }
  // Without pairs the function is its two loops over the lists and its resets: nothing to leave out.
  if (At<uintptr_t>(manager, 0) != state.manager_vptr || At<uint32_t>(manager, MANAGER_PAIR_COUNT) == 0) return false;
  if (state.busy.exchange(true, std::memory_order_acquire)) {
    // A second thread may have read `busy` before the running call's exchange.
    state.nested.store(true);
    return false;
  }

  // True: the game's own loop over every group from here on (the index is not read again).
  bool plain = false;
  const auto fall_back = [&](Reason reason) {
    plain = true;
    state.partly = true;
    if ((state.reasons_logged & (1u << reason)) == 0) {
      state.reasons_logged |= 1u << reason;
      loader.log("renodx_attila64: battle step: partly (%s)", REASON_WORDS[reason]);
    }
  };
  const auto list_count = [&](uint32_t list) { return At<uint32_t>(manager, MANAGER_LISTS[list] + LIST_COUNT); };
  const auto list_groups = [&](uint32_t list) { return At<void**>(manager, MANAGER_LISTS[list] + LIST_GROUPS); };
  // The members of `group` from `first_member` on into the index, under the group's place `where`; the Reason it
  // cannot be indexed for, or REASON_NONE.
  const auto index_group = [&](void* group, uint32_t where, uint32_t first_member) {
    const uint32_t member_count = At<uint32_t>(group, GROUP_MEMBER_COUNT);
    if (std::ranges::find(state.group_vptrs, At<uintptr_t>(group, 0)) == state.group_vptrs.end()
        || member_count > MEMBER_LIMIT) {
      return REASON_TYPE;
    }
    // A started group is not indexed, so no pair asks it: its join test is loads only whatever it holds (the single
    // join asks the group's "may join once started" answer first, which is no for both kinds; CheckGameCode reads
    // that code and both table words). Its members are also never read here: nothing in this call shows that they
    // still are soldiers, while the game's own code reads every member of the other groups in the same call.
    if (At<uint8_t>(group, GROUP_STARTED) != 0) return REASON_NONE;
    for (uint32_t member = first_member; member < member_count; ++member) {
      const uint32_t id = At<uint32_t>(At<void*>(group, GROUP_MEMBERS + (member * sizeof(void*))), SOLDIER_ID);
      if (id >= ID_LIMIT) return REASON_ID;
      if (state.node_count == NODE_CAPACITY) return REASON_POOL;
      Entry& entry = index.entries[id];
      if (entry.generation != state.generation) {
        entry = {.generation = state.generation, .head = NO_NODE};
      }
      // A group's first member is in the index only if the group already has a place: the same group listed twice.
      // A join through one place would leave the other without the new member, so the call goes to the game's
      // function, or to the game's loop when the second place comes up during the call (the game never builds such
      // lists: it takes a group out of its list before it hands the group out again). A group without members, or a
      // started one, has nothing in the index and may be listed twice.
      for (uint32_t node = entry.head; member == 0 && node != NO_NODE; node = index.nodes[node].next) {
        const uint32_t other = index.nodes[node].where;
        if (list_groups(other >> WHERE_LIST_SHIFT)[other & ~(1u << WHERE_LIST_SHIFT)] == group) return REASON_TYPE;
      }
      index.nodes[state.node_count] = {.next = entry.head, .where = where};
      entry.head = state.node_count;
      ++state.node_count;
    }
    return REASON_NONE;
  };

  // The index, before anything of the game's is called or written: a list that cannot be indexed leaves the call to
  // the game's function.
  ++state.generation;
  if (state.generation == 0) {
    index.entries.fill({});
    state.generation = 1;
  }
  state.node_count = 0;
  const uint32_t new_groups_at_start = list_count(NEW_LIST);
  const uint32_t old_groups_at_start = list_count(OLD_LIST);
  Reason unindexed = REASON_NONE;
  if ((uint64_t{new_groups_at_start} + old_groups_at_start) * MEMBER_LIMIT > NODE_CAPACITY) {
    unindexed = REASON_POOL;
  }
  for (const uint32_t list : {NEW_LIST, OLD_LIST}) {
    for (uint32_t position = 0; unindexed == REASON_NONE && position < list_count(list); ++position) {
      unindexed = index_group(list_groups(list)[position], (list << WHERE_LIST_SHIFT) | position, 0);
    }
  }
  if (unindexed != REASON_NONE) {
    fall_back(unindexed);
    SetLane(block, ENGINE_PARTLY);
    state.busy.store(false, std::memory_order_release);
    return false;
  }

  // An entry while a game call ran: it ran the game's function, which may have changed any group.
  const auto after_game_call = [&] {
    if (!plain && state.nested.load(std::memory_order_relaxed)) {
      fall_back(REASON_NESTED);
    }
  };
  const auto manager_test = [&](size_t slot, void* pair_or_soldier) {
    const bool yes = (Slot<ManagerTestFn>(manager, slot)(manager, pair_or_soldier) & 0xff) != 0;
    after_game_call();
    return yes;
  };

  // The game sorts its pairs first: both parts get their own copies of the two ends, as the game passes them.
  const uint32_t pair_count = At<uint32_t>(manager, MANAGER_PAIR_COUNT);
  {
    void** const pairs = At<void**>(manager, MANAGER_PAIRS);
    void** first = pairs;
    void** last = pairs + pair_count;
    // The sort's depth limit: twice the whole part of log2 of the count.
    reinterpret_cast<SortPairsFn>(SORT_PAIRS)(&first, &last,
                                              2 * (static_cast<uint64_t>(std::bit_width(pair_count)) - 1));
    first = pairs;
    last = pairs + pair_count;
    reinterpret_cast<FinishPairSortFn>(FINISH_PAIR_SORT)(&first, &last);
    // No look at `nested` here: it stays set, and each pair's own test comes before the index is read.
  }

  if (At<uint32_t>(manager, MANAGER_PAIR_COUNT) != 0) {
    void** pair_slot = At<void**>(manager, MANAGER_PAIRS);
    do {
      void* const pair = *pair_slot;
      if (manager_test(MANAGER_PAIR_TEST_SLOT, pair)) {
        void* const first = At<void*>(pair, 0);
        void* const second = At<void*>(pair, PAIR_SECOND);
        // The game reads its table for any id, also one past the table.
        const uint32_t* const id_bits = At<const uint32_t*>(manager, MANAGER_ID_BITS);
        const uint32_t first_id = At<uint32_t>(first, SOLDIER_ID);
        const uint32_t second_id = At<uint32_t>(second, SOLDIER_ID);
        const bool first_in_group = (id_bits[first_id >> 5] & (1u << (first_id & 31))) != 0;
        const bool second_in_group = (id_bits[second_id >> 5] & (1u << (second_id & 31))) != 0;
        if (!first_in_group && !second_in_group) {
          // A new group for the two, at the end of the new list.
          if (manager_test(MANAGER_VALID_SLOT, first) && manager_test(MANAGER_VALID_SLOT, second)) {
            void* const group = Slot<CreateGroupFn>(manager, MANAGER_CREATE_SLOT)(manager, first, second);
            reinterpret_cast<AppendGroupFn>(APPEND_GROUP)(static_cast<uint8_t*>(manager) + MANAGER_LISTS[NEW_LIST],
                                                          group);
            after_game_call();
            if (!plain) {
              // No group: appended as the game does, and the game's next read of it fails in the game's code.
              Reason reason = REASON_TYPE;
              if (group != nullptr) {
                reason = index_group(group, (NEW_LIST << WHERE_LIST_SHIFT) | (list_count(NEW_LIST) - 1), 0);
              }
              if (reason != REASON_NONE) {
                fall_back(reason);
              }
            }
          }
        } else if (!first_in_group || !second_in_group) {
          // One of the two is in a group: the pair asks the new list's groups, then always the older list's, each
          // until one says yes.
          for (const uint32_t list : {NEW_LIST, OLD_LIST}) {
            if (list_count(list) == 0) continue;
            // As the game's loop: the groups read once, the end read again after every call.
            void** const groups = list_groups(list);
            void** next = groups;  // the group the game's loop calls next
            const auto at_end = [&] {
              return reinterpret_cast<uintptr_t>(next)
                     == reinterpret_cast<uintptr_t>(list_groups(list) + list_count(list));
            };
            // One group's join test. While the index is in use, the group's members are compared around the call (a
            // join only appends) and the new ones go into the index.
            const auto join = [&](void** slot) {
              void* const group = *slot;
              const bool indexed = !plain;
              std::array<void*, MEMBER_LIMIT> members_before = {};
              uint32_t count_before = 0;
              if (indexed) {
                count_before = At<uint32_t>(group, GROUP_MEMBER_COUNT);
                std::memcpy(members_before.data(), &At<void*>(group, GROUP_MEMBERS), sizeof(members_before));
              }
              const bool joined = (Slot<GroupJoinFn>(group, GROUP_JOIN_SLOT)(group, first, second) & 0xff) != 0;
              after_game_call();
              if (indexed && !plain) {
                const uint32_t count_after = At<uint32_t>(group, GROUP_MEMBER_COUNT);
                if (count_after < count_before || count_after > MEMBER_LIMIT
                    || std::memcmp(members_before.data(), &At<void*>(group, GROUP_MEMBERS),
                                   count_before * sizeof(void*))
                           != 0) {
                  fall_back(REASON_MEMBERS);
                } else {
                  const Reason reason = index_group(
                      group, (list << WHERE_LIST_SHIFT) | static_cast<uint32_t>(slot - groups), count_before);
                  if (reason != REASON_NONE) {
                    fall_back(reason);
                  }
                }
              }
              return joined;
            };

            // The groups that hold one of the two, in list order. A soldier with an id past the table is in none
            // (every member's id was checked).
            std::array<uint32_t, CANDIDATE_LIMIT> candidates = {};
            uint32_t candidate_count = 0;
            // Starting at `plain` only saves collecting candidates that the loop below would not call anyway (its
            // own `!plain` test stops every read of the index): speed, not correctness.
            bool every_group = plain;
            for (const uint32_t id : {first_id, second_id}) {
              if (every_group || id >= ID_LIMIT || index.entries[id].generation != state.generation) continue;
              for (uint32_t node = index.entries[id].head; node != NO_NODE && !every_group;
                   node = index.nodes[node].next) {
                const uint32_t where = index.nodes[node].where;
                if ((where >> WHERE_LIST_SHIFT) != list) continue;
                if (candidate_count == CANDIDATE_LIMIT) {
                  every_group = true;
                } else {
                  candidates[candidate_count] = where & ~(1u << WHERE_LIST_SHIFT);
                  ++candidate_count;
                }
              }
            }
            bool joined = false;
            bool called = false;
            if (!every_group) {
              std::ranges::sort(candidates.begin(), candidates.begin() + candidate_count);
              uint32_t last_position = UINT32_MAX;
              for (uint32_t candidate = 0; candidate < candidate_count && !joined && !plain; ++candidate) {
                const uint32_t position = candidates[candidate];
                if (position == last_position) continue;
                last_position = position;
                // A place past the list's end, or other list memory: the list changed under the loop.
                if (list_groups(list) != groups || position >= list_count(list)) {
                  fall_back(REASON_MEMBERS);
                } else {
                  joined = join(groups + position);
                  next = groups + position + 1;
                  called = true;
                }
              }
              // With the index still whole, the calls left are join tests of groups that hold neither soldier.
              every_group = plain;
            }
            // The game's loop from `next` on: after a call it ends at the list's end.
            if (every_group && !joined && !(called && at_end())) {
              do {
                joined = join(next);
                ++next;
              } while (!joined && !at_end());
            }
          }
        }
      }
      ++pair_slot;
    } while (reinterpret_cast<uintptr_t>(pair_slot)
             != reinterpret_cast<uintptr_t>(At<void**>(manager, MANAGER_PAIRS)
                                            + At<uint32_t>(manager, MANAGER_PAIR_COUNT)));
  }

  // The older list, backward: a group that asks for it goes back to the manager, and the list's last group takes its
  // place. The index is past its last use: the rest is the game's code line by line.
  {
    uint32_t position = list_count(OLD_LIST) - 1;
    uint32_t count = position + 1;
    while (position < count) {
      void* const group = list_groups(OLD_LIST)[position];
      if ((Slot<GroupRetireTestFn>(group, GROUP_RETIRE_TEST_SLOT)(group) & 0xff) != 0) {
        Slot<RetireGroupFn>(manager, MANAGER_RETIRE_SLOT)(manager, group);
        count = list_count(OLD_LIST) - 1;
        list_groups(OLD_LIST)[position] = list_groups(OLD_LIST)[count];
        At<uint32_t>(manager, MANAGER_LISTS[OLD_LIST] + LIST_COUNT) = count;
      } else {
        count = list_count(OLD_LIST);
      }
      --position;
    }
  }
  // The new list, forward: each group goes back to the manager or into the older list.
  if (list_count(NEW_LIST) != 0) {
    void** slot = list_groups(NEW_LIST);
    do {
      void* const group = *slot;
      if ((Slot<GroupRetireTestFn>(group, GROUP_RETIRE_TEST_SLOT)(group) & 0xff) != 0) {
        Slot<RetireGroupFn>(manager, MANAGER_RETIRE_SLOT)(manager, group);
      } else {
        reinterpret_cast<AppendGroupFn>(APPEND_GROUP)(static_cast<uint8_t*>(manager) + MANAGER_LISTS[OLD_LIST], group);
      }
      ++slot;
    } while (reinterpret_cast<uintptr_t>(slot)
             != reinterpret_cast<uintptr_t>(list_groups(NEW_LIST) + list_count(NEW_LIST)));
  }
  // The resets, in the game's order.
  At<uint32_t>(manager, MANAGER_PAIR_COUNT) = 0;
  void* const store = At<void*>(manager, MANAGER_STORE_FIRST);
  At<void*>(manager, MANAGER_STORE_CURRENT) = store;
  if (store != nullptr) {
    At<uint32_t>(store, STORE_USED) = 0;
  }
  if (const uint32_t table_count = At<uint32_t>(manager, MANAGER_TABLE_COUNT); table_count != 0) {
    uint32_t entry = 0;
    do {
      At<void**>(manager, MANAGER_TABLE)[entry] = nullptr;
      ++entry;
    } while (entry != table_count);
  }
  At<uint32_t>(manager, MANAGER_LISTS[NEW_LIST] + LIST_COUNT) = 0;

  // An entry during the two loops above ran the game's function too: exact either way, and the status says so.
  if (state.nested.load(std::memory_order_relaxed)) {
    fall_back(REASON_NESTED);
  }
  if (state.partly) {
    SetLane(block, ENGINE_PARTLY);
  } else {
    SetLane(block, ENGINE_ACTIVE);
    if (!state.active_logged) {
      state.active_logged = true;
      loader.log("renodx_attila64: battle step: active (new %u, active %u, pairs %u)", new_groups_at_start,
                 old_groups_at_start, pair_count);
    }
  }
  state.nested.store(false);
  state.busy.store(false, std::memory_order_release);
  return true;
}

// ROW_CONTACT_SPLIT, "Multithreaded Collisions": the battle step's contact pass with the same results on more threads.
//
// Each battle step the game walks every collision object's grid cells, and for every other object listed there runs a
// chain of filters and, for the few that pass, the response. The response and one object class's path write what later
// filters read, so the order of the walk is part of the result, and the pass stays serial.
//
// ContactTick splits the pass in two. Detect, on the game's task threads: for each object the nodes its walk would
// visit first, kept when a wider test than the game's circle and height filters passes (plugin memory only is
// written). Apply, on the step's thread, in the game's order: the game's own per-object function runs on each object
// with its cell list swapped for the kept nodes, so every filter, response and call is the game's code on the game's
// data; a node left out is one the game's own filters reject without a store. Whatever the reasoning does not cover
// runs the game's loop instead, as a whole tick decided before anything of the game's is written, or from one object
// on (`plain_rest`).
//
// Prior art, and where this differs:
// - Detect in parallel into per-thread lists, then a serial pass in a fixed order, is the broad phase / narrow phase
//   split of Box2D (b2ContactManager::FindNewContacts, Erin Catto, MIT) and PhysX (BSD-3-Clause). Ours: the serial
//   pass is the game's own function, not a second implementation, so the lists only have to be a superset.
// - The claim loop (one atomic add per chunk of objects) is the usual chunked parallel-for; the engine's own
//   parallel-for makes one task per range instead.
// - The class table's hash is Fibonacci hashing (Knuth, TAOCP 3, 6.4) with linear probing.
// - Ours: the closed region (a shared lock per participant, the step's thread closes it with the exclusive one), the
//   swap's pinned store order, the fence around the one class whose path writes filter inputs.

// The contact pass and what it calls or reads: the per-object function, the tail's call, the task manager's pointer
// (the variable of the game's getter 0x3061e60, which builds the manager when it is null: the pass never does);
// the static id set (2048 words of bits, then the id vector's capacity, count and data, then the guard of the pass's
// first call), the radius addition and the game's "tasks in the battle step" byte.
inline constexpr uintptr_t CONTACT_OBJECT_STEP = 0x803e70;
inline constexpr uintptr_t CONTACT_TAIL_STEP = 0xc501c0;
inline constexpr uintptr_t TASK_MANAGER_POINTER = 0x4c0c630;
inline constexpr uintptr_t GAME_ID_SET = 0x4c1cb00;
inline constexpr size_t ID_SET_WORDS = 2048;
inline constexpr size_t ID_SET_CAPACITY = 0x2008;
inline constexpr size_t ID_SET_COUNT = 0x200c;
inline constexpr size_t ID_SET_DATA = 0x2010;  // uint16_t ids
inline constexpr size_t ID_SET_GUARD = 0x2018;
inline constexpr uintptr_t CONTACT_RADIUS_ADDITION = 0x4e453ac;  // a float
inline constexpr uintptr_t TASK_FLAG = 0x4bf3acd;                // a byte
// The contact manager: its environment, the tail's list with its count, the sorted object list with its count, and
// the byte that asks for a re-sort.
inline constexpr size_t CONTACT_ENVIRONMENT = 0x0;
inline constexpr size_t CONTACT_TAIL_COUNT = 0x15c;
inline constexpr size_t CONTACT_TAIL_LIST = 0x160;
inline constexpr size_t CONTACT_OBJECT_COUNT = 0x3f4;
inline constexpr size_t CONTACT_OBJECTS = 0x3f8;
inline constexpr size_t CONTACT_RESORT = 0x400;
// The environment's core link and battle model; the model's tick and fence manager (the manager class H's path always
// pushes to, and that vector's count); the core's database and its lazily built table.
inline constexpr size_t ENVIRONMENT_CORE = 0x58;
inline constexpr size_t ENVIRONMENT_MODEL = 0x68;
inline constexpr size_t MODEL_TICK = 0x90;
inline constexpr size_t MODEL_FENCE_MANAGER = 0x1d8;
inline constexpr size_t FENCE_COUNT = 0x4c;
inline constexpr size_t CORE_DATABASE = 0x20;
inline constexpr size_t DATABASE_TABLE = 0x2198;
// A collision object: its environment, position (x, height, z), height range and radius, its cell entries and their
// count. A cell entry's pointer to the head of its node chain. A node. Another object's record (the object + 0x58).
inline constexpr size_t OBJECT_ENVIRONMENT = 0x18;
inline constexpr size_t OBJECT_X = 0x78;
inline constexpr size_t OBJECT_Y = 0x7c;
inline constexpr size_t OBJECT_Z = 0x80;
inline constexpr size_t OBJECT_HEIGHT = 0x84;
inline constexpr size_t OBJECT_RADIUS = 0x88;
inline constexpr size_t OBJECT_CELLS = 0x108;
inline constexpr size_t OBJECT_CELL_COUNT = 0x110;
inline constexpr size_t CELL_BYTES = 0x30;
inline constexpr size_t CELL_HEAD = 0x28;
inline constexpr size_t RECORD_X = 0x20;
inline constexpr size_t RECORD_Y = 0x24;
inline constexpr size_t RECORD_Z = 0x28;
inline constexpr size_t RECORD_HEIGHT = 0x2c;
inline constexpr size_t RECORD_RADIUS = 0x30;
inline constexpr size_t TASK_MANAGER_THREADS = 0x38;  // a uint32_t
struct ContactNode {
  uint32_t kind = 0;
  uint32_t id = 0;
  void* record = nullptr;
  void* owner = nullptr;  // read by the game only with kind 8
  uint64_t unused = 0;
  ContactNode* next = nullptr;
};
static_assert(sizeof(ContactNode) == 0x28 && offsetof(ContactNode, record) == 0x8 && offsetof(ContactNode, owner) == 0x10
              && offsetof(ContactNode, next) == 0x20);
struct ContactCell {
  std::array<uint64_t, CELL_HEAD / sizeof(uint64_t)> zero = {};  // a reader of a cell's kind and owner finds none
  ContactNode** head = nullptr;
};
static_assert(sizeof(ContactCell) == CELL_BYTES && offsetof(ContactCell, head) == CELL_HEAD);

// Ours. Under MIN_OBJECTS the walk is too short to gain; ids are 16 bits. A chunk is the unit a thread claims. The
// helpers of the game's task threads, each with its own slot, and the step's thread with the last. Two task threads
// stay out: the step's own, and one that keeps taking the engine's other tasks.
inline constexpr uint32_t MIN_OBJECTS = 2048;
inline constexpr uint32_t OBJECT_LIMIT = 65536;
inline constexpr uint32_t CHUNK_OBJECTS = 128;
inline constexpr uint32_t HELPER_COUNT = 15;
inline constexpr uint32_t SLOT_COUNT = 16;
inline constexpr uint32_t OWNER_SLOT = HELPER_COUNT;
static_assert(SLOT_COUNT == HELPER_COUNT + 1);
inline constexpr uint32_t KEPT_OUT_THREADS = 2;
inline constexpr uint32_t MIN_TASK_THREADS = 4;
// An object with more cells takes the game's walk. A walk that visits more ids clears its whole private set.
inline constexpr uint32_t FAKE_ENTRY_COUNT = 64;
inline constexpr uint32_t VISITED_LIMIT = 2048;
inline constexpr uint32_t NODE_BLOCK_COUNT = 256;
inline constexpr uint32_t NODE_BLOCK_NODES = 1024;
inline constexpr uint32_t CLASS_SLOTS = 256;
inline constexpr uint32_t RESORT_PERIOD = 100;  // the game's: every 100th tick re-sorts the list
inline constexpr uint32_t NO_PERIOD = UINT32_MAX;
// No 32-bit count has this value: a fence chain with a null link.
inline constexpr uint64_t FENCE_NO_MANAGER = uint64_t{1} << 32;
// Detect's margin over the game's float compares: far above their rounding (2^-21 relative), plus underflow.
inline constexpr double REACH_MARGIN = 1.0 + 0x1p-10;
inline constexpr double REACH_FLOOR = 1e-30;
inline constexpr std::array<uint32_t, 2> SUMMARY_TICKS = {1000, 3000};

using ContactObjectFn = void(ATTILA64_GAME_CALL*)(void* object, void* id_set) noexcept;
using ContactTailFn = void(ATTILA64_GAME_CALL*)(void* listed) noexcept;

// How the apply runs an object: the game's walk, the game's function on the kept nodes, or the game's walk with the
// fence read around it. The classes of the collision-object base by table pointer: FAST when a walk's two calls
// through the object's table stay in the code read, FENCED for the one class whose path writes filter inputs, NAVAL for
// the one whose path leaves the code read (a tick that meets it is the game's).
enum ContactMode : uint8_t { CONTACT_PLAIN, CONTACT_FAST, CONTACT_FENCED, CONTACT_NAVAL };
struct ContactClass {
  uintptr_t vptr = 0;  // 0: an empty slot of the lookup table
  ContactMode mode = CONTACT_PLAIN;
};
// The 55 classes, by table pointer (the class census; the test derives each mode again from the table's two words).
inline constexpr auto CONTACT_CLASSES = std::to_array<ContactClass>({
    {.vptr = 0x37ce980, .mode = CONTACT_FAST},
    {.vptr = 0x37df730, .mode = CONTACT_FAST},
    {.vptr = 0x37dfe60, .mode = CONTACT_FAST},
    {.vptr = 0x37e0590, .mode = CONTACT_FAST},
    {.vptr = 0x37e0cc0, .mode = CONTACT_FAST},
    {.vptr = 0x37e13f0, .mode = CONTACT_FAST},
    {.vptr = 0x37e1b20, .mode = CONTACT_FAST},
    {.vptr = 0x37e2250, .mode = CONTACT_FAST},
    {.vptr = 0x37e2980, .mode = CONTACT_FAST},
    {.vptr = 0x37e30b0, .mode = CONTACT_FAST},
    {.vptr = 0x37e33c0, .mode = CONTACT_FAST},
    {.vptr = 0x37e90f0, .mode = CONTACT_FAST},
    {.vptr = 0x37e9820, .mode = CONTACT_FAST},
    {.vptr = 0x37e9f50, .mode = CONTACT_FAST},
    {.vptr = 0x37eaf50, .mode = CONTACT_FAST},
    {.vptr = 0x37f2ff0, .mode = CONTACT_FAST},
    {.vptr = 0x37f4360, .mode = CONTACT_FAST},
    {.vptr = 0x37f73b0, .mode = CONTACT_FENCED},
    {.vptr = 0x37fa4a0, .mode = CONTACT_FAST},
    {.vptr = 0x3800a70, .mode = CONTACT_FAST},
    {.vptr = 0x38011c0, .mode = CONTACT_FAST},
    {.vptr = 0x3801910, .mode = CONTACT_FAST},
    {.vptr = 0x3802500, .mode = CONTACT_FAST},
    {.vptr = 0x3802c60, .mode = CONTACT_FAST},
    {.vptr = 0x3835670, .mode = CONTACT_FAST},
    {.vptr = 0x38358e0, .mode = CONTACT_FAST},
    {.vptr = 0x3836010, .mode = CONTACT_FAST},
    {.vptr = 0x3836740, .mode = CONTACT_FAST},
    {.vptr = 0x3836e70, .mode = CONTACT_FAST},
    {.vptr = 0x38379d0, .mode = CONTACT_FAST},
    {.vptr = 0x38381c0, .mode = CONTACT_FAST},
    {.vptr = 0x3838910, .mode = CONTACT_FAST},
    {.vptr = 0x3839000, .mode = CONTACT_FAST},
    {.vptr = 0x38396f0, .mode = CONTACT_FAST},
    {.vptr = 0x3839de0, .mode = CONTACT_FAST},
    {.vptr = 0x383a4d0, .mode = CONTACT_FAST},
    {.vptr = 0x383abc0, .mode = CONTACT_FAST},
    {.vptr = 0x383b2b0, .mode = CONTACT_FAST},
    {.vptr = 0x383ba20, .mode = CONTACT_FAST},
    {.vptr = 0x383c4e0, .mode = CONTACT_FAST},
    {.vptr = 0x3848d90, .mode = CONTACT_FAST},
    {.vptr = 0x3849470, .mode = CONTACT_FAST},
    {.vptr = 0x3849b50, .mode = CONTACT_FAST},
    {.vptr = 0x384a230, .mode = CONTACT_FAST},
    {.vptr = 0x384a910, .mode = CONTACT_FAST},
    {.vptr = 0x384aff0, .mode = CONTACT_FAST},
    {.vptr = 0x384ba30, .mode = CONTACT_FAST},
    {.vptr = 0x3852b00, .mode = CONTACT_NAVAL},
    {.vptr = 0x3874b00, .mode = CONTACT_FAST},
    {.vptr = 0x38765c0, .mode = CONTACT_FAST},
    {.vptr = 0x387f2a0, .mode = CONTACT_FAST},
    {.vptr = 0x387f9f0, .mode = CONTACT_FAST},
    {.vptr = 0x3886670, .mode = CONTACT_FAST},
    {.vptr = 0x38873f0, .mode = CONTACT_FAST},
    {.vptr = 0x38892d0, .mode = CONTACT_FAST},
});

// GENERATED by the contact range generator (a private tool, PK8 Stage B step 2): do not edit by hand. Numbers only.
// Every fingerprinted range of the contact split (pk8_stage_b_design.md 3.6): the named ranges and the light closure's
// functions, in address order. value = FNV-1a 64 over the range as it is in memory at the plugin's init, masked
// bytes read as zero; one check word per 16-byte block (the last, partial block too) = the low 16 bits of the running
// value after that block, so a miss can name its first differing block. The entries: one FNV-1a 64 chain over the
// 8-byte words at vtable + slot, in this order; the end value is the pass / fail, each check word (the low 16 bits
// of the running value after that entry) only names the first differing entry.
// A pinned mask (value_count != 0): FNV-1a 64 over its own bytes in memory must be one of its allowed values, which
// follow those of the masks before it in CONTACT_MASK_VALUES.
struct ContactMask {
  uint32_t start = 0;
  uint16_t size = 0;
  uint16_t value_count = 0;
};

// A range's check words follow those of the ranges before it in CONTACT_RANGE_CHECKS, (size + 15) / 16 of them;
// its masks follow those of the ranges before it in CONTACT_MASKS, each whole inside the range.
struct ContactRange {
  uint64_t value = 0;
  uint32_t start = 0;
  uint16_t size = 0;
  uint16_t mask_count = 0;
};

struct ContactEntry {
  uint32_t vtable = 0;
  uint16_t slot = 0;
  uint16_t check = 0;
};

// The masks (each one a coordinator's decision, with its reason), in address order.
// ours (deviation: from 3.6's one line per mask, because the 18 reasons are one sentence): a reason stands once,
// above the run of masks it holds for.
inline constexpr auto CONTACT_MASKS = std::to_array<ContactMask>({
    // another mod (PerfFix 7.5, x87fix) replaces the float -> int block here, in place; same integer for every float32 input, own stack slots only
    {.start = 0x773ba6, .size = 36, .value_count = 2},
    {.start = 0x773bf4, .size = 36, .value_count = 2},
    {.start = 0x776c6c, .size = 36, .value_count = 2},
    {.start = 0x776cbb, .size = 36, .value_count = 2},
    {.start = 0x78c91d, .size = 36, .value_count = 2},
    {.start = 0x78c96b, .size = 36, .value_count = 2},
    {.start = 0x78c9b1, .size = 36, .value_count = 2},
    {.start = 0x78c9f6, .size = 36, .value_count = 2},
    {.start = 0x796ce1, .size = 60, .value_count = 2},
    {.start = 0x796d4b, .size = 60, .value_count = 2},
    {.start = 0x796db5, .size = 60, .value_count = 2},
    {.start = 0x796e1b, .size = 60, .value_count = 2},
    {.start = 0x7dfe8d, .size = 36, .value_count = 2},
    {.start = 0x7dfedc, .size = 36, .value_count = 2},
    {.start = 0x7dff2a, .size = 36, .value_count = 2},
    {.start = 0x7dff75, .size = 36, .value_count = 2},
    {.start = 0x7e0683, .size = 36, .value_count = 2},
    {.start = 0x7e06cd, .size = 36, .value_count = 2},
    // the probe (-Probe arms only) rewrites the rel32 of the task fetch call at 0x3062d91 to its timing wrapper; the e8 byte stays; 3.6 masks these 4 bytes
    {.start = 0x3062d92, .size = 4},
});

// The pin (ours; ATN 11-27): a pinned mask's allowed values, in the masks' order. First the game's (from the game
// file), then PerfFix 7.5's (from a run's log line, never from its files); that one is not in yet, the game's stands twice.
inline constexpr auto CONTACT_MASK_VALUES = std::to_array<uint64_t>({
    0x55531ace933876c4, 0x55531ace933876c4,  // 0x773ba6
    0x55531ace933876c4, 0x55531ace933876c4,  // 0x773bf4
    0xf5c052d4f067c394, 0xf5c052d4f067c394,  // 0x776c6c
    0xf5c052d4f067c394, 0xf5c052d4f067c394,  // 0x776cbb
    0x7e941fe1e1ecbb64, 0x7e941fe1e1ecbb64,  // 0x78c91d
    0x7e941fe1e1ecbb64, 0x7e941fe1e1ecbb64,  // 0x78c96b
    0x7e941fe1e1ecbb64, 0x7e941fe1e1ecbb64,  // 0x78c9b1
    0x7e941fe1e1ecbb64, 0x7e941fe1e1ecbb64,  // 0x78c9f6
    0x6b0d2025cb862f06, 0x6b0d2025cb862f06,  // 0x796ce1
    0x6b0d2025cb862f06, 0x6b0d2025cb862f06,  // 0x796d4b
    0x6b0d2025cb862f06, 0x6b0d2025cb862f06,  // 0x796db5
    0x6b0d2025cb862f06, 0x6b0d2025cb862f06,  // 0x796e1b
    0x3d919fdcfa3ceca4, 0x3d919fdcfa3ceca4,  // 0x7dfe8d
    0x3d919fdcfa3ceca4, 0x3d919fdcfa3ceca4,  // 0x7dfedc
    0x3d919fdcfa3ceca4, 0x3d919fdcfa3ceca4,  // 0x7dff2a
    0x3d919fdcfa3ceca4, 0x3d919fdcfa3ceca4,  // 0x7dff75
    0x22f775a34fce1104, 0x22f775a34fce1104,  // 0x7e0683
    0x22f775a34fce1104, 0x22f775a34fce1104,  // 0x7e06cd
});
static_assert([] {
  size_t value_count = 0;
  for (const auto& mask : CONTACT_MASKS) {
    value_count += mask.value_count;
  }
  return value_count;
}() == CONTACT_MASK_VALUES.size());

inline constexpr auto CONTACT_RANGES = std::to_array<ContactRange>({
    {.value = 0xa3703b1a918cfbeb, .start = 0x4e8a70, .size = 0x3},
    {.value = 0x2ecc7c5561560ee1, .start = 0x525f10, .size = 0xbe},
    {.value = 0x3a481e695ee6d917, .start = 0x525fd0, .size = 0x27},
    {.value = 0xaf893c3a3e50c0d2, .start = 0x52b0d0, .size = 0xb},
    {.value = 0x8c16f86afb2c05d5, .start = 0x5cbe20, .size = 0x94},
    {.value = 0xb35ded4ff8545005, .start = 0x5d7bf0, .size = 0x17},
    {.value = 0xf1c67b3ac5c52819, .start = 0x5df305, .size = 0x353},
    {.value = 0xbcddc5881561a24d, .start = 0x5e5750, .size = 0x2f},
    {.value = 0x1b8046f96a42874c, .start = 0x5e5780, .size = 0x2b},
    {.value = 0xc1871b3f701827d5, .start = 0x5e57b0, .size = 0xa9},
    {.value = 0xc45111034ecc9ee9, .start = 0x6435f0, .size = 0xb},
    {.value = 0xa5846c5cbdf06bed, .start = 0x654010, .size = 0x81},
    {.value = 0x2d8e242793e83e46, .start = 0x668ab0, .size = 0x40},
    {.value = 0xf909f9e7e1ef28bf, .start = 0x668af0, .size = 0x4b},
    {.value = 0xd751eb1f7d72921c, .start = 0x681660, .size = 0x120},
    {.value = 0x7e4e3b1815cf5340, .start = 0x6877b0, .size = 0xb1},
    {.value = 0x2ec1a8f7095e528a, .start = 0x68ab30, .size = 0x4},
    {.value = 0xd7272999297b49e5, .start = 0x6a4c20, .size = 0x155},
    {.value = 0x66545aee1b1e05be, .start = 0x6a8010, .size = 0x12e},
    {.value = 0x90b774c9bf1883e0, .start = 0x6b7f50, .size = 0x122},
    {.value = 0x06b8a57ec70d3767, .start = 0x6b8080, .size = 0x13c},
    {.value = 0x927e1a6bdc7697f2, .start = 0x6e6030, .size = 0x13c},
    {.value = 0x4042fdbb65bfeef4, .start = 0x6e88f5, .size = 0x108},
    {.value = 0x4172ff1404e7092a, .start = 0x6fd400, .size = 0xd},
    {.value = 0x7b7b45b8e33d3bd6, .start = 0x701cf0, .size = 0x8},
    {.value = 0x7cc7776910fb5d8b, .start = 0x713590, .size = 0x1af},
    {.value = 0xc63d7bd3b57259dd, .start = 0x745d80, .size = 0xef},
    {.value = 0x437eaa18167ba675, .start = 0x74ce90, .size = 0x3},
    {.value = 0x023fd7ba8c3f0636, .start = 0x773b90, .size = 0x1ab, .mask_count = 2},
    {.value = 0x98d2defe598da71b, .start = 0x773ef0, .size = 0x5e},
    {.value = 0x98d44a1a9cb4ba29, .start = 0x776810, .size = 0x182},
    {.value = 0x5b90225fc7762ef3, .start = 0x7769c0, .size = 0x4fb, .mask_count = 2},
    {.value = 0x903d0b83a7ae85d7, .start = 0x77d330, .size = 0x385},
    {.value = 0xe5bc600f0270e2cb, .start = 0x77d6c0, .size = 0x30e},
    {.value = 0xc3b729474c71c030, .start = 0x77da10, .size = 0x8},
    {.value = 0x829330c3728896fa, .start = 0x785660, .size = 0x48},
    {.value = 0x4dc09ac03f94d7aa, .start = 0x7856d0, .size = 0x15},
    {.value = 0xdb37f23b20d93d1b, .start = 0x7856f0, .size = 0x499},
    {.value = 0x5516a798a42bf827, .start = 0x785b90, .size = 0x218},
    {.value = 0x26ca0de18be44aa2, .start = 0x785e60, .size = 0x50f},
    {.value = 0x6abf362dee5e31df, .start = 0x7863a0, .size = 0x1bc},
    {.value = 0x787e99331d0253ca, .start = 0x786560, .size = 0x1b5},
    {.value = 0x8154d1c8d1ecce44, .start = 0x786720, .size = 0x154},
    {.value = 0x75ae197626bc5f2f, .start = 0x788260, .size = 0xe},
    {.value = 0x875b832086cbbe13, .start = 0x788450, .size = 0x25f},
    {.value = 0x3afeb5495025b85e, .start = 0x78c7c0, .size = 0x539, .mask_count = 4},
    {.value = 0x31b5d0af35a4361f, .start = 0x78dfa0, .size = 0x6},
    {.value = 0x46dfe5504691706d, .start = 0x78f310, .size = 0x288},
    {.value = 0x1f4750bcff0c777e, .start = 0x793570, .size = 0x34},
    {.value = 0x1f4750bcff0c777e, .start = 0x7935b0, .size = 0x34},
    {.value = 0xfc2a865d751c5c59, .start = 0x7969f0, .size = 0x8ea, .mask_count = 4},
    {.value = 0x6483d8f00007ef70, .start = 0x7972e0, .size = 0x51f},
    {.value = 0x0fa2875f65bbade7, .start = 0x79a030, .size = 0x9},
    {.value = 0x5c6ec4849fa6c9fc, .start = 0x79a0f0, .size = 0xa6},
    {.value = 0xdb51eeb9857166a3, .start = 0x79a1a0, .size = 0x9c},
    {.value = 0x650474557d6cf5c1, .start = 0x79a240, .size = 0xba},
    {.value = 0x482a706e68cfc138, .start = 0x79a3a0, .size = 0xe2},
    {.value = 0xa94832f2a039af4b, .start = 0x7a1ac0, .size = 0x4cc},
    {.value = 0x1728af151ec23eb3, .start = 0x7a4c80, .size = 0x165},
    {.value = 0x4abf9283bcc940bf, .start = 0x7a8150, .size = 0x9d},
    {.value = 0x8d344e3af95c11d4, .start = 0x7de900, .size = 0x165},
    {.value = 0xbbce348f89d8c6e2, .start = 0x7dfe30, .size = 0x37b, .mask_count = 4},
    {.value = 0x42175c3689c3bdbb, .start = 0x7e03c0, .size = 0xb1},
    {.value = 0xcc54b5bbe57614f5, .start = 0x7e0480, .size = 0xec},
    {.value = 0x28fcf29cd776d03c, .start = 0x7e0650, .size = 0x218, .mask_count = 2},
    {.value = 0x700df352907a1144, .start = 0x7e1490, .size = 0xb1},
    {.value = 0xda93a4da712ddb7f, .start = 0x7e4b20, .size = 0x4},
    {.value = 0x437eaa18167ba675, .start = 0x7e4b70, .size = 0x3},
    {.value = 0xd1ce33f2d99459ad, .start = 0x7e4bb0, .size = 0x8},
    {.value = 0x437eaa18167ba675, .start = 0x7e4c40, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e4c50, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e4cd0, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e4ce0, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e4cf0, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e4d00, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e4d10, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e4d30, .size = 0x3},
    {.value = 0xa3703b1a918cfbeb, .start = 0x7e4d50, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e4d60, .size = 0x3},
    {.value = 0x7979ecc0a56d71cb, .start = 0x7e4e80, .size = 0x8},
    {.value = 0x7979ecc0a56d71cb, .start = 0x7e4e90, .size = 0x8},
    {.value = 0x437eaa18167ba675, .start = 0x7e5980, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e59b0, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e6070, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e61f0, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e6220, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e62c0, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x7e62d0, .size = 0x3},
    {.value = 0xa3703b1a918cfbeb, .start = 0x7e6370, .size = 0x3},
    {.value = 0x52e4f611a3040ae6, .start = 0x7e67f0, .size = 0x5},
    {.value = 0x24ef1ce9e3aa52a3, .start = 0x7e68e0, .size = 0x35},
    {.value = 0x3cb81e2bc68f1ec3, .start = 0x7e6940, .size = 0x35},
    {.value = 0xda93a4da712ddb7f, .start = 0x7e6d50, .size = 0x4},
    {.value = 0xa3703b1a918cfbeb, .start = 0x7e6ec0, .size = 0x3},
    {.value = 0x9281ed123ef055a1, .start = 0x7ec370, .size = 0x22f},
    {.value = 0xf9b74338bbab4a32, .start = 0x7eccc0, .size = 0xdf},
    {.value = 0x03425e115d6347b6, .start = 0x7ecda0, .size = 0x192},
    {.value = 0x154e98ceb4de0672, .start = 0x7f1050, .size = 0x1b},
    {.value = 0xb6afdd0cce585513, .start = 0x7f1090, .size = 0x1e},
    {.value = 0x970c777a0bab3fd7, .start = 0x7f1110, .size = 0x13},
    {.value = 0x9e079af616749a89, .start = 0x7f3330, .size = 0xb9},
    {.value = 0xe3929d4b7d8cdb53, .start = 0x7f3b00, .size = 0x71e},
    {.value = 0xb4eeb581f66bba51, .start = 0x803e70, .size = 0x7f7},
    {.value = 0xac0085666de6660d, .start = 0x807630, .size = 0x2c},
    {.value = 0x991769e1cc561873, .start = 0x807660, .size = 0x223},
    {.value = 0x2af2fe5b91b7d30a, .start = 0x807890, .size = 0x195},
    {.value = 0x54f9d1dfea08e815, .start = 0x809f30, .size = 0x1e1},
    {.value = 0xf6f346dcf64a7998, .start = 0x81bc20, .size = 0x299},
    {.value = 0x117e17a2e36e0c63, .start = 0x81bec0, .size = 0x1f8},
    {.value = 0xebfe6d5686f35a52, .start = 0x839070, .size = 0x3e},
    {.value = 0xcaaf9025acb26e13, .start = 0x83a350, .size = 0xf1},
    {.value = 0xd055364bdcb50601, .start = 0x843cb0, .size = 0xb9},
    {.value = 0xc64fea2f976b31fe, .start = 0x846ee0, .size = 0x70f},
    {.value = 0x45cf90fccdf68ba3, .start = 0x891aa0, .size = 0x4},
    {.value = 0x437eaa18167ba675, .start = 0x8994c0, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x899810, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x899830, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x8998a0, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0x8998c0, .size = 0x3},
    {.value = 0xa3703b1a918cfbeb, .start = 0x899ef0, .size = 0x3},
    {.value = 0xa3703b1a918cfbeb, .start = 0x899f80, .size = 0x3},
    {.value = 0xe2e173c1a978c817, .start = 0x89a040, .size = 0x7},
    {.value = 0x52c94611a2ec181e, .start = 0x89a050, .size = 0x5},
    {.value = 0x52c94611a2ec181e, .start = 0x89a8b0, .size = 0x5},
    {.value = 0xda93a4da712ddb7f, .start = 0x89e320, .size = 0x4},
    {.value = 0xda93a4da712ddb7f, .start = 0x89e420, .size = 0x4},
    {.value = 0xf1c6c19366c9fbc9, .start = 0x8ae4b0, .size = 0xc7},
    {.value = 0x3de27e317620dcd9, .start = 0x901430, .size = 0x44},
    {.value = 0xfabb6ed935ee20be, .start = 0x9348e0, .size = 0xc},
    {.value = 0x686bd3b8e780f870, .start = 0x934900, .size = 0x16},
    {.value = 0xdf2b5c1b244cc86d, .start = 0x95b7e0, .size = 0x43},
    {.value = 0x14aa7c24caa5e9e6, .start = 0x95c090, .size = 0xe6},
    {.value = 0x437eaa18167ba675, .start = 0x986b00, .size = 0x3},
    {.value = 0xa3703b1a918cfbeb, .start = 0x986f50, .size = 0x3},
    {.value = 0x9327bd8e3eae84f0, .start = 0x9886d0, .size = 0x8},
    {.value = 0x9327bd8e3eae84f0, .start = 0x9886e0, .size = 0x8},
    {.value = 0xda93a4da712ddb7f, .start = 0x9886f0, .size = 0x4},
    {.value = 0xda93a4da712ddb7f, .start = 0x988700, .size = 0x4},
    {.value = 0x338d1b1e33acc184, .start = 0x988710, .size = 0xc},
    {.value = 0xa3703b1a918cfbeb, .start = 0x988720, .size = 0x3},
    {.value = 0x5355f252f285f1e8, .start = 0xb29860, .size = 0x5},
    {.value = 0xf10e6b0bbcb6870b, .start = 0xb317d0, .size = 0x4c},
    {.value = 0x5dc12e54833395cd, .start = 0xb409e0, .size = 0xb7},
    {.value = 0x2505ef84b8531c22, .start = 0xb436f0, .size = 0x6},
    {.value = 0x19afaa62477ed725, .start = 0xba51a0, .size = 0xb1},
    {.value = 0xa3703b1a918cfbeb, .start = 0xbab070, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0xbabce0, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0xbabdc0, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0xbac1a0, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0xbac440, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0xbb0ac0, .size = 0x3},
    {.value = 0xa3703b1a918cfbeb, .start = 0xbb7010, .size = 0x3},
    {.value = 0xa3703b1a918cfbeb, .start = 0xbb7020, .size = 0x3},
    {.value = 0xf6c5ad62ccfe8e1f, .start = 0xc4ac60, .size = 0x7},
    {.value = 0x6cf6df3f3a026caf, .start = 0xc4eff0, .size = 0x16},
    {.value = 0x6228baa51b3be82c, .start = 0xc501c0, .size = 0xf},
    {.value = 0x2505ef84b8531c22, .start = 0xc51500, .size = 0x6},
    {.value = 0xc9b35377228aaf87, .start = 0xc516d0, .size = 0x6d},
    {.value = 0xda93a4da712ddb7f, .start = 0xc67c50, .size = 0x4},
    {.value = 0xda93a4da712ddb7f, .start = 0xc67c60, .size = 0x4},
    {.value = 0x437eaa18167ba675, .start = 0xc67c70, .size = 0x3},
    {.value = 0xcf5af1fe6827d71c, .start = 0xd27fc0, .size = 0x17},
    {.value = 0x0dd67861283c3ad0, .start = 0xd4ead0, .size = 0x7},
    {.value = 0x437eaa18167ba675, .start = 0xdcee70, .size = 0x3},
    {.value = 0xda93a4da712ddb7f, .start = 0xdf10b0, .size = 0x4},
    {.value = 0xa3703b1a918cfbeb, .start = 0xdf18b0, .size = 0x3},
    {.value = 0xa3703b1a918cfbeb, .start = 0xdf2210, .size = 0x3},
    {.value = 0x437eaa18167ba675, .start = 0xebb6f0, .size = 0x3},
    {.value = 0xa3703b1a918cfbeb, .start = 0xebb700, .size = 0x3},
    {.value = 0x25f3d28708059189, .start = 0xebb710, .size = 0x5},
    {.value = 0x1518cbc05f5cbcb9, .start = 0xebb720, .size = 0x5},
    {.value = 0xa3703b1a918cfbeb, .start = 0xebc7b0, .size = 0x3},
    {.value = 0x7b705cff09124a6b, .start = 0x272c070, .size = 0x123},
    {.value = 0xd7037d10212138a2, .start = 0x2dbf3c0, .size = 0xdd},
    {.value = 0x57e7e5912f5c3ac0, .start = 0x2def660, .size = 0x163},
    {.value = 0x50d2abae405ff2c2, .start = 0x2defbb0, .size = 0xb},
    {.value = 0x53871611a38cef96, .start = 0x2defbd0, .size = 0x5},
    {.value = 0x1d772c39aeaa0b01, .start = 0x2defbe0, .size = 0x8},
    {.value = 0x48300e7922a15a37, .start = 0x2e08570, .size = 0x6},
    {.value = 0xc364729744debd10, .start = 0x3005d60, .size = 0x20},
    {.value = 0x263e79dbc369bcf0, .start = 0x301fa50, .size = 0x26},
    {.value = 0xa74e22f15a95c1c6, .start = 0x3061e60, .size = 0x5d},
    {.value = 0xdb25a6d158992c20, .start = 0x3062aa0, .size = 0x67},
    {.value = 0xe829e488b475825f, .start = 0x3062d00, .size = 0xb8, .mask_count = 1},
});
static_assert(CONTACT_RANGES.size() == 184);
static_assert([] {
  size_t mask_count = 0;
  for (const auto& range : CONTACT_RANGES) {
    mask_count += range.mask_count;
  }
  return mask_count;
}() == CONTACT_MASKS.size());

inline constexpr std::array<uint16_t, 2441> CONTACT_RANGE_CHECKS = {
    0xfbeb, 0x2380, 0x7941, 0x9841, 0x4457, 0xdc02, 0xe34a, 0x8e08, 0xa9de, 0x98c4, 0xff94, 0x402f,
    0x0ee1, 0xd744, 0x6b3d, 0xd917, 0xc0d2, 0x2047, 0x6885, 0x3127, 0xf482, 0x9454, 0x61ab, 0xd1c1,
    0x69ab, 0xd1f6, 0x05d5, 0xb93c, 0x5005, 0x3e4c, 0x5fd9, 0xed0c, 0x571b, 0x6e2f, 0x5770, 0x502b,
    0x4090, 0x1bc4, 0xd7ac, 0x71ef, 0x7de5, 0xc93f, 0xde25, 0xc10c, 0xc575, 0x3304, 0x3532, 0x2ffc,
    0xa252, 0xad64, 0x4cef, 0x109d, 0xaa44, 0x2ff4, 0xeb40, 0x3c51, 0x9761, 0x99ba, 0xec04, 0x0db1,
    0x188f, 0x5518, 0x158f, 0x360e, 0xd184, 0x6c11, 0x965e, 0x29bd, 0xc55e, 0xe1cc, 0xbb54, 0x766b,
    0x3b15, 0x876a, 0x7c8e, 0xf779, 0xbee2, 0xf3d4, 0xbb1b, 0xae33, 0x5004, 0xd493, 0x2819, 0x513a,
    0x19af, 0xa24d, 0xbfa9, 0xc6d8, 0x874c, 0x2702, 0x1eab, 0xede6, 0x6e06, 0x1ff1, 0x03e8, 0x00d9,
    0xf9d4, 0x5d1d, 0x1635, 0x27d5, 0x9ee9, 0xcd24, 0xdea8, 0xd8bc, 0x492b, 0x7cb0, 0x3025, 0x524b,
    0xcb1c, 0x6bed, 0xfe59, 0x3678, 0x80dc, 0x3e46, 0xa539, 0xe0bf, 0x5eae, 0x9302, 0x28bf, 0xe556,
    0x1328, 0xefee, 0xf322, 0xc13f, 0x89c2, 0x0625, 0x1ac0, 0xfaa5, 0xca2f, 0x962a, 0xaad8, 0xde6a,
    0x427e, 0x4e1a, 0x7ce3, 0x42b2, 0x921c, 0x480f, 0xac71, 0x54ca, 0xc5b0, 0xbc08, 0x836b, 0x1328,
    0x055e, 0x5db3, 0xfcc4, 0x3f03, 0x5340, 0x528a, 0x998e, 0x6e3f, 0x7f3b, 0x31f4, 0x984d, 0x023a,
    0x8769, 0xf044, 0x3ad7, 0x8581, 0x9c60, 0x3208, 0xdc90, 0xc0c4, 0x0c9f, 0x9ed0, 0x7fd9, 0xcffb,
    0x12da, 0xcae7, 0xa740, 0x49e5, 0xe556, 0x05e8, 0x617b, 0xafff, 0xc7e3, 0xb4fc, 0x3e97, 0x9e2e,
    0x5fce, 0x73f0, 0x87b0, 0x9bdd, 0x6d84, 0xdb2d, 0x3f55, 0x2b20, 0x716b, 0xc8bf, 0x05be, 0x8873,
    0xc98f, 0x057d, 0x5fde, 0xa197, 0x88a8, 0x4372, 0xc188, 0x6c63, 0x8b06, 0xc9ea, 0x7fb6, 0x2def,
    0x3b91, 0x5b4c, 0x107c, 0x1216, 0x22ce, 0x83e0, 0xe556, 0x301b, 0xfdc5, 0xa0da, 0x443f, 0xe2af,
    0x32ea, 0x4ab1, 0xd509, 0x9786, 0x6d3b, 0x04f0, 0x104f, 0xa6ff, 0xb20a, 0x8936, 0xd674, 0x5906,
    0x1512, 0x3767, 0xe556, 0x301b, 0xfdc5, 0xa0da, 0x443f, 0x4c3f, 0x299b, 0xe080, 0xf2b4, 0x84d7,
    0xd312, 0xfa6d, 0x3132, 0x482e, 0xfbcb, 0x53fa, 0x6238, 0xef9b, 0x3083, 0x97f2, 0xbbb2, 0x3e8d,
    0xd679, 0x03d6, 0xbd63, 0x97c7, 0xe1f5, 0x97e5, 0xc9e8, 0x3850, 0x57d5, 0xf2b2, 0x7965, 0x81f3,
    0x9092, 0xc022, 0xeef4, 0x092a, 0x3bd6, 0x4226, 0x1137, 0x493c, 0x29d1, 0xd4c1, 0x423d, 0xb95e,
    0xcd29, 0x3ed2, 0x69dd, 0x6c30, 0xadf1, 0x2c8c, 0x2212, 0x9c4e, 0x272e, 0x45c7, 0xfecf, 0x7eea,
    0x2817, 0x90e5, 0x344a, 0x1601, 0xc8b6, 0x26ac, 0x25d6, 0x5d8b, 0x3034, 0xfd3b, 0x0ead, 0xf517,
    0xdd4f, 0x976e, 0xbc15, 0xeb7e, 0x92e0, 0x2f9c, 0x9eba, 0xbaa7, 0x1152, 0xdca3, 0x59dd, 0xa675,
    0xca24, 0x4b55, 0x1895, 0x04b4, 0x7088, 0x4f8c, 0x2702, 0x1782, 0xbe9f, 0xcd0d, 0xdd20, 0x719b,
    0x413c, 0x1419, 0x96e0, 0x9637, 0xce8b, 0x457a, 0x9f4a, 0xd3c4, 0xbc61, 0x3a65, 0x55ae, 0x6a23,
    0xd36e, 0x5741, 0x0636, 0xaacf, 0x03b5, 0x52d1, 0xde35, 0xfd09, 0xa71b, 0x998e, 0x6fe8, 0xc424,
    0xacb4, 0x392e, 0x8862, 0x7c85, 0x613c, 0xf593, 0xe244, 0x326d, 0xa84d, 0xe182, 0x76fe, 0x6349,
    0x45b7, 0xe563, 0x484b, 0x74a1, 0x501c, 0x2225, 0x2297, 0xf2e6, 0xdf8d, 0xba29, 0x0186, 0xa1d0,
    0xdbbf, 0xcc55, 0xd360, 0x7897, 0x9ff4, 0x2246, 0x1bfd, 0x60fa, 0x8019, 0xff3b, 0xef80, 0x716e,
    0xae49, 0x4ec5, 0x8a5d, 0x257a, 0xd4ee, 0x85ae, 0x5a5d, 0x0837, 0x2178, 0xf397, 0x7c33, 0x01cc,
    0x964e, 0xd96e, 0x4944, 0xdf95, 0x9705, 0x55ec, 0xa03f, 0x63d5, 0x57ca, 0x5267, 0x246c, 0x10df,
    0x6cd0, 0x24db, 0xd030, 0x95ef, 0x9f1c, 0x061c, 0x2d1c, 0x44ab, 0xa496, 0x3a56, 0xdfd6, 0xcabb,
    0x8ee8, 0xc1ba, 0xe88d, 0x5559, 0x50bb, 0x5984, 0x4ce4, 0x8457, 0xa79e, 0x1a70, 0xc528, 0xe862,
    0x4f9f, 0x3bba, 0x4122, 0x2cb0, 0x0853, 0xd20f, 0xd560, 0x146f, 0x5a00, 0x6a31, 0x70f5, 0xc30a,
    0x39b9, 0x7451, 0xefab, 0x7dc0, 0xaab7, 0x2ef3, 0x998e, 0x9f8e, 0xfdd2, 0xd8c8, 0x5004, 0x4572,
    0xfe99, 0x6e3d, 0x48b0, 0x10c0, 0x5e1e, 0x2850, 0x7336, 0xd50e, 0x77b4, 0x79c5, 0x239d, 0xe95a,
    0x57f8, 0x1158, 0x4d3c, 0x4178, 0x2670, 0x9662, 0xfb11, 0xeb4a, 0x7526, 0xb8bd, 0x833f, 0x2e63,
    0x4d75, 0x1110, 0xe03d, 0xe1ae, 0x4dde, 0x15d1, 0x76e2, 0xceef, 0x45a0, 0xd729, 0xb158, 0x9402,
    0x7d6b, 0xb2e1, 0xa337, 0x076e, 0xc143, 0x2845, 0xfe78, 0x962f, 0xf65a, 0x0760, 0x3016, 0xee79,
    0x21e5, 0x406e, 0x85d7, 0x59fd, 0x58a7, 0xb9b6, 0x53a8, 0x4103, 0xe732, 0xd6d0, 0x4c04, 0xf455,
    0x339d, 0x4c19, 0x3142, 0xe93a, 0x594f, 0x5899, 0x4d00, 0xa917, 0xb625, 0x19e1, 0xe172, 0xd464,
    0x11f9, 0x13ae, 0xf710, 0x72eb, 0xfd82, 0x9952, 0x2b60, 0xe11c, 0xd23d, 0x616a, 0xb3ad, 0x6292,
    0xe971, 0x4796, 0x263c, 0x0ba7, 0x079c, 0xe193, 0x4d66, 0xcf4a, 0x3306, 0xa5dc, 0xa352, 0xf900,
    0x22a8, 0xe709, 0xea65, 0xe2cb, 0xc030, 0x28d4, 0xaff7, 0x6e79, 0xbf10, 0x96fa, 0x4aa3, 0xd7aa,
    0x0510, 0xaf76, 0xbc59, 0x773f, 0x7acf, 0x732c, 0x6578, 0x5fec, 0x7c9a, 0xc57a, 0x8ccc, 0x8616,
    0x29f1, 0x4668, 0x70b8, 0x604d, 0x93b2, 0xfa5f, 0x7f87, 0x4319, 0x6778, 0xe003, 0x8304, 0x9bae,
    0x3e79, 0x04e4, 0x0ac8, 0x3e56, 0x6133, 0xa2dd, 0xfbea, 0x3ab5, 0x28fa, 0x36d5, 0x6955, 0xa969,
    0x286b, 0x8e84, 0x0be9, 0x1d80, 0xaa31, 0xb32f, 0x7170, 0xcc0a, 0x7056, 0x5245, 0xe14f, 0xc17b,
    0xc084, 0x24f1, 0xbc85, 0x90c6, 0xb780, 0x454b, 0x0dfe, 0x4a2a, 0x6b65, 0xb9df, 0x5c9d, 0x009e,
    0xfd2c, 0x0ff8, 0x996e, 0xfc04, 0x06d4, 0x8add, 0x77ce, 0x6dea, 0xdec0, 0x2a99, 0x7195, 0xcda2,
    0x250b, 0x3d1b, 0x23f2, 0xd23f, 0x6e7c, 0x5b5a, 0x44a9, 0x31a9, 0xb328, 0xbd74, 0x9be8, 0x6865,
    0x5d93, 0x9e69, 0x14f5, 0x25fe, 0xbffe, 0x1b46, 0x5af7, 0x529e, 0x0e5b, 0x7553, 0x384f, 0x7ff9,
    0x7928, 0xe86e, 0xb5e0, 0xfbdc, 0xa0e1, 0x7ead, 0x86c1, 0xfcd2, 0xa16e, 0xe48c, 0xb961, 0xf827,
    0xeceb, 0x2fce, 0x609f, 0x0064, 0x10c6, 0xa6ee, 0x5a15, 0xc077, 0xc330, 0x7c94, 0xd10f, 0x14c4,
    0x9698, 0x749e, 0x2b67, 0x60c9, 0x86db, 0x312a, 0x8f3e, 0xf1ea, 0xbbe4, 0x6709, 0x5ac3, 0xad75,
    0x8e89, 0xe25d, 0xa6ca, 0x2af0, 0x4ff6, 0xb30a, 0x179f, 0x620d, 0xf242, 0x9c0e, 0x659d, 0xdd03,
    0x5fa6, 0xa3c5, 0x4762, 0xc42e, 0x0ab4, 0xec8d, 0x017f, 0x9c97, 0x8cca, 0x9070, 0xf1cc, 0xf8c0,
    0x4af5, 0x00d7, 0x1bec, 0x160e, 0xf208, 0x3072, 0x2fc1, 0x2fa5, 0x6c87, 0xe9d3, 0xf8bd, 0x34cf,
    0x335b, 0x97a7, 0x38ac, 0x42ac, 0xeb1e, 0xad7a, 0x331e, 0xd0f3, 0xb8c1, 0x525d, 0x79a1, 0x267b,
    0x4506, 0x8209, 0x80e4, 0xc588, 0x6aaf, 0x846a, 0x00eb, 0x6e34, 0x4aa2, 0xca1d, 0x0d09, 0xd217,
    0x49dd, 0x6306, 0xf512, 0xe11c, 0xe799, 0xc623, 0x2fe2, 0xc7a9, 0x60f5, 0x6926, 0x3f8a, 0x598c,
    0x1307, 0x03ba, 0x49d9, 0x5dae, 0x7876, 0x0d72, 0x3986, 0xe519, 0x8491, 0x32df, 0xf9eb, 0x5fe8,
    0x31df, 0xe99c, 0x0ff4, 0x97a0, 0xacec, 0xe391, 0x93fc, 0x6da6, 0x4cfe, 0xbb61, 0x8d4b, 0x972d,
    0x72b5, 0x8868, 0xced1, 0xb423, 0x37dc, 0x65ac, 0xcbe2, 0x26ff, 0x1f4a, 0x90f3, 0x1cf8, 0x076f,
    0xd76b, 0xd8a8, 0x5c0d, 0x18a6, 0x53ca, 0xca1d, 0x7698, 0x99ed, 0xaaf9, 0xa47d, 0xdd09, 0xafb8,
    0xf394, 0xf9fe, 0x0fbd, 0x09e0, 0x9800, 0x0314, 0xff7c, 0x7302, 0xdefd, 0x5de7, 0x381e, 0x78bf,
    0x7368, 0xabeb, 0xce44, 0x5f2f, 0x6ed0, 0x80bb, 0xe3df, 0xca98, 0xeaca, 0x69f0, 0xce42, 0x4356,
    0x70eb, 0x86d5, 0xdc76, 0xa869, 0x0083, 0xff93, 0xc945, 0x68ba, 0xaf90, 0xe76c, 0x719a, 0x54a6,
    0x9e1b, 0x8686, 0x06d3, 0x034e, 0x6303, 0x7243, 0x4337, 0xad64, 0xbbed, 0x8192, 0xbfad, 0xf1e8,
    0x3806, 0x3a16, 0x6bf9, 0x031d, 0x24f6, 0xbe13, 0x0c9e, 0xda69, 0xfbf3, 0x4e37, 0x45cc, 0xbc40,
    0x3a1c, 0xdbeb, 0xa6c5, 0xe1d3, 0xeed7, 0x3796, 0xd7ed, 0x290a, 0xac14, 0xe492, 0x5d10, 0x918c,
    0xdf76, 0x4770, 0xd731, 0xe60b, 0x70cb, 0xab8b, 0xdcb9, 0x960b, 0x252b, 0x77eb, 0xee37, 0x573e,
    0xa7fd, 0xfe39, 0xe479, 0xccb7, 0xc64b, 0x8e3e, 0xedbe, 0x1c63, 0xb063, 0xc610, 0x9edd, 0xeab7,
    0x0d48, 0x0cb1, 0x2ceb, 0x5d17, 0x4b72, 0xb000, 0x6bef, 0xf0b5, 0x60ee, 0x7a0c, 0x71a0, 0xe629,
    0x8b87, 0xb2db, 0xbb0f, 0x5ef2, 0x6149, 0xfa05, 0xd658, 0xe222, 0xd4fb, 0x1f61, 0x0ce0, 0x6d21,
    0x09df, 0x36aa, 0x7180, 0xeba2, 0x5b21, 0x4399, 0xb042, 0xc94f, 0xccb2, 0x5448, 0xee87, 0x9597,
    0x4c5b, 0x8730, 0xff96, 0x651e, 0xbbb7, 0xb85e, 0x361f, 0x998e, 0xabbb, 0x948d, 0xd909, 0xf0b0,
    0x0037, 0x6fb1, 0xbc22, 0xf4b0, 0x2d85, 0x4fe7, 0xefd4, 0x9c5f, 0xd95a, 0x12a4, 0xd5b6, 0xb496,
    0x7c28, 0x588b, 0x86b8, 0x8f9c, 0x5a2a, 0x6a52, 0x13c1, 0x8d12, 0xa548, 0x86f2, 0x60a2, 0x4b21,
    0xd831, 0x201f, 0x59d5, 0x859b, 0x0d4a, 0xeea2, 0xe189, 0x8360, 0x25b7, 0x8851, 0x9975, 0x706d,
    0x0291, 0x84b2, 0xa9de, 0x777e, 0x0291, 0x84b2, 0xa9de, 0x777e, 0x1296, 0x7d91, 0xf44c, 0x7fba,
    0x3741, 0xb905, 0x392a, 0x487b, 0xb0fc, 0x676e, 0x6612, 0x5730, 0xdfa3, 0x904f, 0xfd89, 0x6362,
    0xd6f1, 0x1203, 0x71c6, 0x6006, 0x1d98, 0x430d, 0xa26b, 0x20b7, 0x3c07, 0x441c, 0xba2b, 0x47aa,
    0x4561, 0xa560, 0x0437, 0x835a, 0x4048, 0xbd16, 0x4f1a, 0x924d, 0x1578, 0x4f64, 0x24db, 0x2e19,
    0x8359, 0x22b5, 0xab89, 0xf12c, 0xe6d8, 0x1082, 0xebd8, 0x82dc, 0xd9dc, 0xf0dc, 0xb24d, 0xc9da,
    0xc739, 0x257e, 0x54fe, 0x647e, 0x53fe, 0xb58f, 0x1b95, 0x5498, 0x9d5c, 0x945c, 0x4b5c, 0xc25c,
    0x02f8, 0xd3be, 0x1d71, 0x11b1, 0x15f1, 0x2a31, 0xaf7c, 0x712c, 0xe2d8, 0x7dcd, 0xd199, 0x61c0,
    0xa479, 0x887f, 0xfe39, 0x7cdb, 0x7550, 0x175a, 0x1664, 0xbb5a, 0x8151, 0xcf9c, 0x435b, 0x6989,
    0x96e5, 0xae59, 0x5a64, 0x1033, 0xefbc, 0x5a15, 0xb7dc, 0x86f1, 0xbdb6, 0x9df1, 0x9408, 0xf583,
    0x00fc, 0xb733, 0x8941, 0x4384, 0x2f80, 0xc88d, 0x7980, 0x9d3d, 0x3259, 0x3cba, 0x99ea, 0xe7cf,
    0x306a, 0xf091, 0x3842, 0xbff8, 0xed36, 0xdc17, 0x4168, 0x3a98, 0x84d7, 0x6566, 0xfa9d, 0x23c6,
    0xab6c, 0xee31, 0x42e8, 0x51e7, 0xbc14, 0x92bc, 0x3253, 0xd3a9, 0xa198, 0xb780, 0xf007, 0x6b56,
    0x9c12, 0x7a2a, 0x66e1, 0x65a8, 0xe0df, 0x2981, 0x5c59, 0x4aa6, 0xf7c7, 0x994d, 0x6b58, 0xf0a6,
    0xd987, 0x83c0, 0x2caa, 0xa446, 0x0b6b, 0x9bcd, 0xce8a, 0xd359, 0xb316, 0x2d49, 0xb6fa, 0x41f9,
    0x0bc6, 0x20a3, 0x564f, 0xb5c1, 0xfc69, 0xa474, 0x9a0d, 0x56ef, 0xe3df, 0x5906, 0x5ea1, 0x023e,
    0x086b, 0x258c, 0xf607, 0x9df3, 0xce41, 0xab27, 0x0c6c, 0xd70e, 0x72ef, 0x8bc4, 0x65d6, 0xf007,
    0xe7c9, 0x3cfe, 0x9b5b, 0x26a0, 0x3fc5, 0x5f8d, 0xcb89, 0x788b, 0x9dfb, 0x1bb1, 0xf064, 0xe7e4,
    0x8b3a, 0xd472, 0x33f8, 0xb638, 0xdce8, 0xebe4, 0xfe21, 0x774d, 0x0950, 0xe7d7, 0x3127, 0x75d3,
    0xd7cc, 0x2b89, 0xe808, 0x586d, 0x47bb, 0xbdc0, 0x92dd, 0x6625, 0xd72f, 0x03a7, 0x5bce, 0x6ef3,
    0x176a, 0xce82, 0xde9a, 0xe382, 0xef70, 0xade7, 0xd44d, 0xac95, 0x50f9, 0xa625, 0x7b25, 0x0fa2,
    0xcb56, 0x5258, 0x000c, 0x47ec, 0xc9fc, 0xa86c, 0x5bdb, 0x44ed, 0xbdc4, 0xbf6a, 0x94c1, 0xbe7d,
    0x4652, 0x922e, 0x66a3, 0x3a41, 0xac5f, 0x50a2, 0x9b11, 0x2834, 0x9c67, 0x6409, 0x6201, 0x7469,
    0x7c5a, 0xc114, 0xf5c1, 0x53ed, 0x59a1, 0xca8d, 0xe018, 0xd1bf, 0x7266, 0x2f57, 0x1687, 0xbc34,
    0x5e9a, 0xa015, 0xb372, 0x7055, 0x7af7, 0xc138, 0xfd46, 0x71e4, 0x2d91, 0x70b1, 0xe98e, 0x8ca4,
    0xfaf7, 0x3fca, 0x95d1, 0x231f, 0x1d03, 0x392e, 0xec06, 0xaad7, 0x35e7, 0xa6f9, 0x0150, 0xf316,
    0xee70, 0x0cac, 0xad24, 0x5547, 0x9172, 0x617d, 0xd0b9, 0x636f, 0x7b0e, 0x5778, 0x5077, 0xd3fb,
    0x8727, 0xb121, 0xeb68, 0x3d74, 0xd027, 0x3078, 0x893a, 0xc887, 0x0fcd, 0xbac1, 0x5f18, 0x36d4,
    0x2ab3, 0x4615, 0x81c0, 0x3186, 0xfb18, 0x1048, 0x3105, 0x61c6, 0xae72, 0x0431, 0x1af6, 0xe91a,
    0x2d0c, 0xd47c, 0x8d84, 0x6d49, 0x256a, 0xdb24, 0x7e15, 0x24df, 0x6d7a, 0xb47b, 0xdad7, 0xefe6,
    0xf648, 0x89b3, 0x4367, 0xd5aa, 0x09cf, 0xed64, 0x08fd, 0x2a52, 0x59bf, 0x8194, 0xaf4b, 0x0a8e,
    0x293f, 0x2134, 0x8321, 0x74e3, 0x5a16, 0xa8a4, 0x6200, 0x6685, 0xc9a1, 0x4eae, 0x4c41, 0x2e1e,
    0x2280, 0x5553, 0x6066, 0x9b45, 0xdfaf, 0x1c52, 0xcf7b, 0xc79d, 0x005f, 0x3eb3, 0xf6c6, 0x3dcd,
    0x164b, 0x9dd5, 0x2fad, 0x2fa2, 0xa6be, 0x61bf, 0xd6e0, 0x40bf, 0x9177, 0x21b8, 0x0dcc, 0x90ac,
    0xe615, 0xf574, 0x2597, 0x753f, 0xf753, 0x5734, 0x5bb5, 0x81b6, 0xd24c, 0xfe3b, 0xc9c9, 0xd867,
    0x53f6, 0xa768, 0x8992, 0x3d85, 0x85b1, 0x036d, 0x11d4, 0x23ab, 0x34dd, 0x0c87, 0x31a3, 0xa7df,
    0xee7d, 0x45bd, 0x6cfd, 0xb143, 0x1a90, 0xb803, 0x00c3, 0x7983, 0xf62e, 0xa649, 0xc34e, 0xe6ce,
    0x7b59, 0xf271, 0xdb35, 0x549b, 0x035b, 0xcd3e, 0xb2f7, 0x710a, 0xcc28, 0x26cf, 0xb591, 0x9d8e,
    0xeb8b, 0x0919, 0x445b, 0x1890, 0x906c, 0x4bcb, 0xbcce, 0x103d, 0xa9b9, 0x8771, 0x3561, 0x26c5,
    0x2445, 0xd6b9, 0xeece, 0xf4aa, 0x8359, 0x7f5c, 0x46cb, 0xd0cb, 0x1d1b, 0xdeae, 0xadc5, 0xfa56,
    0x8697, 0x3001, 0xc6e2, 0x480f, 0xac71, 0x54ca, 0x05e3, 0x2f8f, 0xd36c, 0xd2fe, 0xf144, 0x8492,
    0x5401, 0xff1a, 0xbdbb, 0x8662, 0x8dca, 0x26d0, 0xbf6e, 0x1dca, 0x8b5d, 0x929d, 0x5995, 0xb7f1,
    0xfbda, 0x974f, 0xe6a2, 0xe285, 0x58ae, 0x14f5, 0x767e, 0x3962, 0x4a6e, 0x15c0, 0x85c0, 0x5db1,
    0x570b, 0x1121, 0x7161, 0xe1a1, 0x3624, 0xbd55, 0x3557, 0x4e3f, 0x446b, 0x1033, 0x39cf, 0x43ed,
    0x4f8e, 0x2fc7, 0x9bf5, 0x471c, 0xd8d8, 0x3f27, 0x01d4, 0x8ff2, 0x555d, 0x682d, 0x6596, 0x132b,
    0xca07, 0x7598, 0x0142, 0xd03c, 0x480f, 0xac71, 0x54ca, 0xb306, 0x4dd6, 0xa5dd, 0xdfe0, 0x7fb6,
    0xd7df, 0x8a38, 0xdf6f, 0x1144, 0xdb7f, 0xa675, 0x59ad, 0xa675, 0xa675, 0xa675, 0xa675, 0xa675,
    0xa675, 0xa675, 0xa675, 0xfbeb, 0xa675, 0x71cb, 0x71cb, 0xa675, 0xa675, 0xa675, 0xa675, 0xa675,
    0xa675, 0xa675, 0xfbeb, 0x0ae6, 0x74c8, 0x1395, 0xb6ae, 0x52a3, 0x74c8, 0x1395, 0xfc4e, 0x1ec3,
    0xdb7f, 0xfbeb, 0x8179, 0xe154, 0x15cf, 0x01c6, 0x3149, 0xc285, 0x35c6, 0xa073, 0x4f92, 0xc4bc,
    0xe12c, 0x0f50, 0x7288, 0xab37, 0x5d1e, 0x1dd7, 0x4216, 0xaec4, 0x3c4a, 0x37a7, 0x0bc4, 0x8476,
    0x3ce9, 0xfba8, 0x68b1, 0xfc73, 0xde1a, 0xfce6, 0x7e9e, 0x9411, 0xaee2, 0xf104, 0xb955, 0x1e8f,
    0x55a1, 0xe6c4, 0x5f7b, 0xf054, 0xe90b, 0xc366, 0x6b1c, 0x4444, 0x9c10, 0x06e1, 0x635c, 0x7b2a,
    0xa18d, 0x03b6, 0x4a32, 0x243a, 0x3b51, 0xb055, 0x2450, 0x4adc, 0x90ed, 0xc69d, 0x64b6, 0xcfeb,
    0x652a, 0xd494, 0x540e, 0x62b9, 0xb2b1, 0xba34, 0x9fa1, 0xec6b, 0xf815, 0x84fd, 0x4b08, 0xe907,
    0xe2d0, 0x47a4, 0xecee, 0x6a7e, 0x47b6, 0x57d3, 0x0672, 0x4ba4, 0x5513, 0xecbf, 0x3fd7, 0x58fa,
    0xf3d6, 0x86a5, 0xb48f, 0x6f34, 0x32ec, 0x9700, 0xb419, 0x8493, 0xe6ca, 0xd433, 0x9a89, 0xeceb,
    0x0e95, 0x67e4, 0x7bd8, 0x48da, 0xcda3, 0x2dad, 0x5242, 0x430d, 0xe775, 0xc8e0, 0xa6dc, 0xcb41,
    0xbc8e, 0x0c7c, 0xc1f0, 0x8607, 0x5d0e, 0xd50e, 0xc0c1, 0xd193, 0xe476, 0x6a80, 0x88f5, 0xd490,
    0x9ffd, 0x25e6, 0x8746, 0x613c, 0x6af5, 0x5e3a, 0xb11f, 0x511d, 0x0e2b, 0x09b5, 0x8a6d, 0x92ac,
    0xf83a, 0x7a06, 0xbbe2, 0x43a4, 0xf757, 0x98b6, 0x87ed, 0x2900, 0x057d, 0xde69, 0x9f93, 0xaa5f,
    0xf1e8, 0xdecd, 0x5842, 0x6441, 0xce71, 0x030c, 0x5346, 0x2c28, 0x2b4e, 0xbc7e, 0xdfa1, 0x128e,
    0x4a72, 0xb153, 0x1bad, 0x4b77, 0x0970, 0x605a, 0x6165, 0x7378, 0xeb7c, 0x9d89, 0x8b32, 0x2cc4,
    0x7831, 0x54b0, 0x4fec, 0xd815, 0x8386, 0x4f39, 0xce26, 0x5679, 0xc191, 0x23f4, 0xd4f7, 0x29bc,
    0xcb35, 0x7489, 0xbc11, 0x5c1a, 0xf68b, 0x8a38, 0x00d0, 0x17cc, 0x02f0, 0x5a2b, 0x3e71, 0x0861,
    0x66e7, 0x5a03, 0xec3a, 0x1fd0, 0xe0de, 0x1720, 0xe1b7, 0x0fd2, 0x3dcd, 0x198e, 0x7729, 0x7489,
    0xd831, 0x152c, 0x80f0, 0x5066, 0xdb53, 0xf90b, 0x93a0, 0x873f, 0x6be2, 0xa412, 0xbd27, 0x9775,
    0x928f, 0xce46, 0x3118, 0x748a, 0x6f5b, 0x0fe5, 0xb427, 0x01e1, 0xc541, 0xc575, 0x3fbd, 0x2bf9,
    0xc16d, 0xa7d1, 0xff46, 0x4a63, 0x1e1c, 0xafc0, 0x9278, 0x0acc, 0xf16f, 0xf33b, 0xedef, 0x472b,
    0x6a5a, 0x8181, 0xb146, 0xc6a9, 0xab8a, 0x4fc3, 0xadd6, 0x72e8, 0xa878, 0x9bd2, 0x65ce, 0xf41e,
    0x0f34, 0x24b6, 0x47b3, 0xd35a, 0x2f6c, 0x4ad4, 0x68d1, 0xffb5, 0xf204, 0x3290, 0x1cac, 0xa41c,
    0x8e03, 0x1e34, 0x75b5, 0xfdd0, 0xcbd4, 0x4112, 0x9b8a, 0x0b93, 0x57c1, 0xe9e2, 0x5ecd, 0x099a,
    0x48bc, 0xacc1, 0x39e4, 0x8b1e, 0xe09b, 0xfb89, 0x8304, 0xb51f, 0x2b01, 0x70d9, 0x2b41, 0x2758,
    0x8080, 0xc828, 0x82c9, 0x16ee, 0x8d55, 0x1063, 0x9e5b, 0x136b, 0x2c2b, 0x51b1, 0xb3e5, 0x58ab,
    0x345d, 0x7739, 0xdb53, 0x51d4, 0x5856, 0x6126, 0x4c2d, 0x7055, 0xd30e, 0xa3dc, 0xc637, 0x02a8,
    0x2def, 0x13bb, 0x9132, 0x1276, 0xd189, 0xbab6, 0x0f81, 0x1c0a, 0x151c, 0xda7a, 0xacad, 0xb287,
    0xb35d, 0xbe94, 0xf0ae, 0xeb2d, 0x27bc, 0xeb8c, 0x10af, 0xa645, 0xe07a, 0x7c24, 0x524a, 0x5a57,
    0xba51, 0x1ebd, 0xc510, 0x660d, 0x0cad, 0x41c7, 0x9f32, 0xd459, 0xc731, 0xc556, 0x3662, 0x4554,
    0xb6e9, 0x5ace, 0xf477, 0x7fd7, 0x9d08, 0x8cf4, 0xea78, 0x1d76, 0xecb4, 0x0878, 0x0041, 0x6a28,
    0x7619, 0xd3c7, 0xb75c, 0xf875, 0x2d54, 0x8cec, 0x585e, 0x1ad4, 0x305c, 0xa92b, 0x4873, 0xe763,
    0x8dee, 0x91f6, 0x1873, 0xb14d, 0xd38e, 0x6980, 0xd55d, 0x9782, 0x9087, 0xc85e, 0xc6c0, 0x8f6c,
    0xa296, 0xdd25, 0x77dc, 0x1596, 0x69e1, 0xab40, 0xd4fb, 0xfbd7, 0xa892, 0xc91e, 0xbd6c, 0x6877,
    0x8288, 0x2000, 0x658a, 0x2018, 0xd30a, 0x3251, 0xc54c, 0x88fe, 0x706c, 0xa264, 0xe5ad, 0x0f66,
    0x90ba, 0x8a71, 0x9fe2, 0x2df1, 0x64f3, 0x96f0, 0x3a3c, 0x3982, 0xb8e7, 0x5489, 0xc1bc, 0xa052,
    0xa1ef, 0xeb78, 0x66ff, 0x30ae, 0x4bb1, 0x2cea, 0x0f96, 0x8e3c, 0x62a4, 0x3fa0, 0xbbed, 0xe815,
    0x7eba, 0xe9f9, 0x0e10, 0xa066, 0x09c7, 0x7118, 0xed7a, 0xd380, 0xc7b4, 0xee71, 0xcf40, 0x9e3a,
    0x0c72, 0xb3ba, 0x3acc, 0x16ab, 0xf88b, 0x583b, 0xa0c9, 0xf002, 0x6173, 0x2617, 0xb952, 0xab9b,
    0xf2e0, 0xb92c, 0xd487, 0xe9ff, 0xb836, 0xa4d7, 0xd300, 0xf22f, 0xf076, 0x23c9, 0xd8ad, 0x97a8,
    0xff3d, 0xb0e3, 0x69dd, 0xa2e1, 0x9fc3, 0x7998, 0x3215, 0x6b0d, 0xb2f5, 0x4f92, 0x0102, 0x78a8,
    0xa131, 0x6c2a, 0x748a, 0xe031, 0xc731, 0x667f, 0x9eb2, 0xee4a, 0xc9ee, 0x7098, 0x4459, 0x897f,
    0x4ecb, 0x2d8e, 0x652f, 0xbd24, 0xb3c3, 0x19a8, 0x5046, 0x0d47, 0xda2b, 0xc685, 0x2a55, 0x1901,
    0xd00e, 0x0c63, 0x25ce, 0x4feb, 0x600d, 0x5a52, 0xbd90, 0xb3fd, 0x829b, 0x2b14, 0x3390, 0x3ba4,
    0x5fdf, 0x1dc8, 0xdd4d, 0x5074, 0x0c50, 0x396d, 0xfe67, 0xa8d5, 0xf2e2, 0x6e13, 0x0bc2, 0xdfbe,
    0x71cd, 0x27c7, 0xd92c, 0x9ad4, 0x6c18, 0x6f51, 0x2f7b, 0x7ac2, 0x6e2b, 0x0601, 0x05b3, 0xd20c,
    0xfbf1, 0x3f05, 0x9a71, 0x7d7b, 0xcd9f, 0xaa44, 0x4bff, 0xd335, 0xecd6, 0x4433, 0x744e, 0xccfc,
    0x9509, 0xf1f1, 0x8cf3, 0x2043, 0x94f4, 0x8fcd, 0xd399, 0x58d1, 0x05b1, 0x4e9c, 0x3c79, 0x5f76,
    0xcf9e, 0xa09d, 0x1d6e, 0x4c45, 0x8c0a, 0xa76b, 0x93ef, 0x2745, 0x17b4, 0xabc0, 0xc212, 0xb733,
    0x9790, 0x1169, 0x0766, 0x0c86, 0xe3d0, 0x9f2e, 0x55b7, 0x206f, 0x3cd3, 0x6d43, 0x390c, 0x1537,
    0x19fa, 0x8da6, 0x9a9d, 0x7e02, 0x4d0d, 0xee7e, 0xce0e, 0xf8ec, 0xda90, 0xe6db, 0x5be2, 0x51e8,
    0x5c15, 0x7c9e, 0xbe1d, 0x166a, 0x27d2, 0x4d82, 0x6d91, 0x9113, 0x24b7, 0xf40c, 0x0e11, 0x9279,
    0x0211, 0xd305, 0x221f, 0x5dcb, 0xb618, 0x83b6, 0x2bef, 0xe159, 0x650f, 0x7d4e, 0x495a, 0x9e00,
    0x0242, 0x9c23, 0xac89, 0xd282, 0x5c1c, 0x737e, 0xe984, 0x24d1, 0xe9b2, 0x7850, 0x6a54, 0xd968,
    0xc41f, 0x2d08, 0x214e, 0xce0a, 0x2a39, 0x1820, 0x5973, 0x7c06, 0x1835, 0x2cba, 0x7dd0, 0x6bad,
    0x58d0, 0xe2ae, 0x31fe, 0x8ba3, 0xa675, 0xa675, 0xa675, 0xa675, 0xa675, 0xfbeb, 0xfbeb, 0xc817,
    0x181e, 0x181e, 0xdb7f, 0xdb7f, 0x3ecb, 0xde11, 0x51bc, 0x2f86, 0x8820, 0x871d, 0xab82, 0xcc89,
    0x9219, 0x179c, 0x5e5d, 0xfb39, 0xfbc9, 0x8e70, 0x5d2a, 0x3f81, 0xf40a, 0xdcd9, 0x20be, 0x6722,
    0xf870, 0x4162, 0xce42, 0xe41b, 0x833c, 0xc86d, 0x723e, 0x97af, 0xb13c, 0x4ead, 0x17a9, 0xb2fa,
    0x5969, 0x1b52, 0x2ad1, 0xdccb, 0xae6d, 0x9d3c, 0x6cc5, 0x83f3, 0xe9e6, 0xa675, 0xfbeb, 0x84f0,
    0x84f0, 0xdb7f, 0xdb7f, 0xc184, 0xfbeb, 0xf1e8, 0x8f67, 0x0bab, 0x2c60, 0x1097, 0x870b, 0xc2d1,
    0xe8fe, 0x4429, 0xab5d, 0x6fe7, 0x46dd, 0x2b4b, 0xfe69, 0xaa0b, 0x8269, 0xf097, 0x95cd, 0x1c22,
    0x480f, 0xac71, 0x54ca, 0x20a5, 0x8a91, 0x5c3a, 0x6a3a, 0xfc08, 0x6118, 0x0627, 0xe704, 0xd725,
    0xfbeb, 0xa675, 0xa675, 0xa675, 0xa675, 0xa675, 0xfbeb, 0xfbeb, 0x8e1f, 0x85de, 0x6caf, 0xe82c,
    0x1c22, 0xfcc1, 0x4766, 0xeb3b, 0x789c, 0x4744, 0x9442, 0xaf87, 0xdb7f, 0xdb7f, 0xa675, 0xbb27,
    0xd71c, 0x3ad0, 0xa675, 0xdb7f, 0xfbeb, 0xfbeb, 0xa675, 0xfbeb, 0x9189, 0xbcb9, 0xfbeb, 0xa234,
    0x8573, 0x0645, 0x3868, 0x3920, 0x72fb, 0xb5a5, 0xc37b, 0x4dca, 0x2f87, 0x9688, 0xb888, 0x75ea,
    0xaa2e, 0x05be, 0xcb0f, 0x0314, 0x0d5e, 0x4a6b, 0x85e8, 0x8fe6, 0xab2f, 0xc645, 0x8bb9, 0x4e9b,
    0x8718, 0x82df, 0xda4e, 0x3e97, 0x58af, 0xbf41, 0x6043, 0x38a2, 0x5fd8, 0xa897, 0x3f51, 0xdd2e,
    0x0926, 0x1f33, 0x2463, 0x68df, 0xfe91, 0x03a5, 0x9a79, 0x6c2c, 0x2061, 0xf773, 0xce2b, 0x5e10,
    0x83e9, 0x62f6, 0x659b, 0x8340, 0x861c, 0xe9ed, 0x3ac0, 0xf2c2, 0xef96, 0x0b01, 0x5a37, 0xf6ef,
    0xbd10, 0x9d22, 0x4bf7, 0xbcf0, 0x00e2, 0x69a5, 0x78b8, 0xe099, 0x9737, 0xc1c6, 0x0b65, 0x73c2,
    0x410e, 0xa509, 0xab4e, 0x4339, 0x2c20, 0xbcd8, 0x8898, 0x5b0c, 0xdd99, 0xbb79, 0xc235, 0x9c47,
    0x233f, 0x9c4d, 0x86b1, 0xf7d6, 0x825f,
};
static_assert([] {
  size_t checks = 0;
  for (const auto& range : CONTACT_RANGES) {
    checks += (range.size + 15) / 16;
  }
  return checks;
}() == CONTACT_RANGE_CHECKS.size());

inline constexpr auto CONTACT_ENTRIES = std::to_array<ContactEntry>({
    {.vtable = 0x3794430, .slot = 0x10, .check = 0xc684},
    {.vtable = 0x37a2e40, .slot = 0x18, .check = 0x5cfa},
    {.vtable = 0x37b9360, .slot = 0x1c8, .check = 0x89e4},
    {.vtable = 0x37b9690, .slot = 0x1c8, .check = 0x484f},
    {.vtable = 0x37b99c0, .slot = 0x1c8, .check = 0xd9e5},
    {.vtable = 0x37b9cf0, .slot = 0x1c8, .check = 0xaa4f},
    {.vtable = 0x37b9fc0, .slot = 0x1c8, .check = 0x7be5},
    {.vtable = 0x37ba2f0, .slot = 0x1c8, .check = 0x8c4f},
    {.vtable = 0x37ba610, .slot = 0x1c8, .check = 0x9de5},
    {.vtable = 0x37ba970, .slot = 0x1c8, .check = 0xee4f},
    {.vtable = 0x37bacd0, .slot = 0x1c8, .check = 0x3fe5},
    {.vtable = 0x37bb030, .slot = 0x1c8, .check = 0xd04f},
    {.vtable = 0x37bb300, .slot = 0x1c8, .check = 0x61e5},
    {.vtable = 0x37ce980, .slot = 0x38, .check = 0x6752},
    {.vtable = 0x37ce980, .slot = 0x68, .check = 0xaae1},
    {.vtable = 0x37ce980, .slot = 0x88, .check = 0x336f},
    {.vtable = 0x37ce980, .slot = 0x98, .check = 0x7cd1},
    {.vtable = 0x37ce980, .slot = 0xa0, .check = 0xeacf},
    {.vtable = 0x37ce980, .slot = 0xa8, .check = 0xb6ee},
    {.vtable = 0x37ce980, .slot = 0xb0, .check = 0x7255},
    {.vtable = 0x37ce980, .slot = 0x1b0, .check = 0x724f},
    {.vtable = 0x37ce980, .slot = 0x1b8, .check = 0x1405},
    {.vtable = 0x37ce980, .slot = 0x1f8, .check = 0x630f},
    {.vtable = 0x37ce980, .slot = 0x200, .check = 0xb535},
    {.vtable = 0x37ce980, .slot = 0x208, .check = 0xb69f},
    {.vtable = 0x37ce980, .slot = 0x210, .check = 0x83ee},
    {.vtable = 0x37ce980, .slot = 0x218, .check = 0x2893},
    {.vtable = 0x37ce980, .slot = 0x228, .check = 0x9d12},
    {.vtable = 0x37ce980, .slot = 0x238, .check = 0xdaf7},
    {.vtable = 0x37ce980, .slot = 0x240, .check = 0x2f46},
    {.vtable = 0x37ce980, .slot = 0x2b0, .check = 0x9208},
    {.vtable = 0x37ce980, .slot = 0x360, .check = 0x2d7c},
    {.vtable = 0x37ce980, .slot = 0x368, .check = 0x57c0},
    {.vtable = 0x37cf070, .slot = 0x0, .check = 0xec3e},
    {.vtable = 0x37cf110, .slot = 0x0, .check = 0x2ee0},
    {.vtable = 0x37cf1b0, .slot = 0x0, .check = 0x1a1e},
    {.vtable = 0x37cf250, .slot = 0x28, .check = 0x5469},
    {.vtable = 0x37cf250, .slot = 0x30, .check = 0x58b9},
    {.vtable = 0x37cf250, .slot = 0x48, .check = 0xa1a8},
    {.vtable = 0x37cf2b0, .slot = 0x0, .check = 0x7639},
    {.vtable = 0x37cf350, .slot = 0x28, .check = 0x6bc4},
    {.vtable = 0x37cf3b0, .slot = 0x0, .check = 0x7b3a},
    {.vtable = 0x37cf4b0, .slot = 0x40, .check = 0x5213},
    {.vtable = 0x37cf4b0, .slot = 0x58, .check = 0x5c2e},
    {.vtable = 0x37cf580, .slot = 0x40, .check = 0xe09f},
    {.vtable = 0x37cf580, .slot = 0x58, .check = 0x0c22},
    {.vtable = 0x37cf650, .slot = 0x40, .check = 0xc1fb},
    {.vtable = 0x37cf650, .slot = 0x58, .check = 0xb0e8},
    {.vtable = 0x37cf720, .slot = 0x40, .check = 0x2605},
    {.vtable = 0x37cf720, .slot = 0x58, .check = 0xb2fc},
    {.vtable = 0x37cf7f0, .slot = 0x40, .check = 0xbcc1},
    {.vtable = 0x37cf7f0, .slot = 0x58, .check = 0xd410},
    {.vtable = 0x37cf8c0, .slot = 0x40, .check = 0x622d},
    {.vtable = 0x37cf8c0, .slot = 0x58, .check = 0xf4e4},
    {.vtable = 0x37cf990, .slot = 0x40, .check = 0xfda9},
    {.vtable = 0x37cf990, .slot = 0x58, .check = 0xb4f8},
    {.vtable = 0x37cfa60, .slot = 0x40, .check = 0x0115},
    {.vtable = 0x37cfa60, .slot = 0x58, .check = 0xbbec},
    {.vtable = 0x37cfb30, .slot = 0x40, .check = 0xf471},
    {.vtable = 0x37cfb30, .slot = 0x58, .check = 0x34a0},
    {.vtable = 0x37cfc00, .slot = 0x40, .check = 0x08bd},
    {.vtable = 0x37cfc00, .slot = 0x58, .check = 0x2c54},
    {.vtable = 0x37cfcd0, .slot = 0x40, .check = 0xef19},
    {.vtable = 0x37cfcd0, .slot = 0x58, .check = 0x2988},
    {.vtable = 0x37cfe00, .slot = 0x40, .check = 0x6465},
    {.vtable = 0x37cfe00, .slot = 0x58, .check = 0xf95c},
    {.vtable = 0x37cfed0, .slot = 0x40, .check = 0xd0e1},
    {.vtable = 0x37cfed0, .slot = 0x58, .check = 0xb230},
    {.vtable = 0x37cffa0, .slot = 0x40, .check = 0xef0d},
    {.vtable = 0x37cffa0, .slot = 0x58, .check = 0x7e44},
    {.vtable = 0x37d0070, .slot = 0x40, .check = 0x56c9},
    {.vtable = 0x37d0070, .slot = 0x58, .check = 0xf618},
    {.vtable = 0x37d0140, .slot = 0x40, .check = 0x38f5},
    {.vtable = 0x37d0140, .slot = 0x58, .check = 0xe5cc},
    {.vtable = 0x37d0290, .slot = 0x40, .check = 0x6f91},
    {.vtable = 0x37d0290, .slot = 0x58, .check = 0xb6c0},
    {.vtable = 0x37d0360, .slot = 0x40, .check = 0x809d},
    {.vtable = 0x37d0360, .slot = 0x58, .check = 0xecb4},
    {.vtable = 0x37d0430, .slot = 0x40, .check = 0x4239},
    {.vtable = 0x37d0430, .slot = 0x58, .check = 0xb928},
    {.vtable = 0x37d0500, .slot = 0x40, .check = 0x8645},
    {.vtable = 0x37d0500, .slot = 0x58, .check = 0x113c},
    {.vtable = 0x37d05d0, .slot = 0x40, .check = 0x14af},
    {.vtable = 0x37d05d0, .slot = 0x58, .check = 0x3c52},
    {.vtable = 0x37d06a0, .slot = 0x40, .check = 0x992b},
    {.vtable = 0x37d06a0, .slot = 0x58, .check = 0x7426},
    {.vtable = 0x37d0770, .slot = 0x40, .check = 0x1657},
    {.vtable = 0x37d0770, .slot = 0x58, .check = 0x8131},
    {.vtable = 0x37d0840, .slot = 0x40, .check = 0x7050},
    {.vtable = 0x37d0840, .slot = 0x58, .check = 0x955d},
    {.vtable = 0x37d0910, .slot = 0x40, .check = 0x9a24},
    {.vtable = 0x37d0910, .slot = 0x58, .check = 0x0ad9},
    {.vtable = 0x37d0a00, .slot = 0x40, .check = 0x7438},
    {.vtable = 0x37d0a00, .slot = 0x58, .check = 0x1fe5},
    {.vtable = 0x37d0ad0, .slot = 0x40, .check = 0x62ac},
    {.vtable = 0x37d0ad0, .slot = 0x58, .check = 0x1221},
    {.vtable = 0x37d0ba0, .slot = 0x40, .check = 0xca40},
    {.vtable = 0x37d0ba0, .slot = 0x58, .check = 0x054d},
    {.vtable = 0x37d0c70, .slot = 0x40, .check = 0xd814},
    {.vtable = 0x37d0c70, .slot = 0x58, .check = 0x3ae9},
    {.vtable = 0x37d0d40, .slot = 0x40, .check = 0xc248},
    {.vtable = 0x37d0d40, .slot = 0x58, .check = 0xdf95},
    {.vtable = 0x37d0e10, .slot = 0x40, .check = 0xc59c},
    {.vtable = 0x37d0e10, .slot = 0x58, .check = 0x2c31},
    {.vtable = 0x37d0fc0, .slot = 0x40, .check = 0x7b50},
    {.vtable = 0x37d0fc0, .slot = 0x58, .check = 0x527d},
    {.vtable = 0x37d1170, .slot = 0x40, .check = 0x3f84},
    {.vtable = 0x37d1170, .slot = 0x58, .check = 0xf539},
    {.vtable = 0x37d1240, .slot = 0x40, .check = 0x7358},
    {.vtable = 0x37d1240, .slot = 0x58, .check = 0x5985},
    {.vtable = 0x37d1310, .slot = 0x40, .check = 0x4d8c},
    {.vtable = 0x37d1310, .slot = 0x58, .check = 0xa581},
    {.vtable = 0x37d13e0, .slot = 0x40, .check = 0xf6e0},
    {.vtable = 0x37d13e0, .slot = 0x58, .check = 0xeaed},
    {.vtable = 0x37d14b0, .slot = 0x40, .check = 0x6ff4},
    {.vtable = 0x37d14b0, .slot = 0x58, .check = 0x5749},
    {.vtable = 0x37d1580, .slot = 0x40, .check = 0x4768},
    {.vtable = 0x37d1580, .slot = 0x58, .check = 0xbc35},
    {.vtable = 0x37d16a0, .slot = 0x40, .check = 0x08fc},
    {.vtable = 0x37d16a0, .slot = 0x58, .check = 0xf291},
    {.vtable = 0x37d17c0, .slot = 0x40, .check = 0x3af0},
    {.vtable = 0x37d17c0, .slot = 0x58, .check = 0x849d},
    {.vtable = 0x37d18e0, .slot = 0x40, .check = 0x4664},
    {.vtable = 0x37d18e0, .slot = 0x58, .check = 0x4919},
    {.vtable = 0x37d19b0, .slot = 0x40, .check = 0x9178},
    {.vtable = 0x37d19b0, .slot = 0x58, .check = 0x1425},
    {.vtable = 0x37d1a80, .slot = 0x40, .check = 0x2eec},
    {.vtable = 0x37d1a80, .slot = 0x58, .check = 0x9561},
    {.vtable = 0x37d1b50, .slot = 0x40, .check = 0xab80},
    {.vtable = 0x37d1b50, .slot = 0x58, .check = 0xf78d},
    {.vtable = 0x37d1c20, .slot = 0x40, .check = 0xfb54},
    {.vtable = 0x37d1c20, .slot = 0x58, .check = 0x65a8},
    {.vtable = 0x37d1cf0, .slot = 0x40, .check = 0xa1c5},
    {.vtable = 0x37d1cf0, .slot = 0x58, .check = 0x7bbc},
    {.vtable = 0x37d1dc0, .slot = 0x40, .check = 0x9381},
    {.vtable = 0x37d1dc0, .slot = 0x58, .check = 0xb1d0},
    {.vtable = 0x37d1e90, .slot = 0x1c8, .check = 0x724e},
    {.vtable = 0x37d2620, .slot = 0x1c8, .check = 0x06a8},
    {.vtable = 0x37d2960, .slot = 0x40, .check = 0x62c5},
    {.vtable = 0x37d2960, .slot = 0x58, .check = 0x5cbc},
    {.vtable = 0x37d2a30, .slot = 0x70, .check = 0x9879},
    {.vtable = 0x37d2a30, .slot = 0x88, .check = 0xd15d},
    {.vtable = 0x37d2a30, .slot = 0xd8, .check = 0x6459},
    {.vtable = 0x37d2a30, .slot = 0xe0, .check = 0x13ed},
    {.vtable = 0x37d2b40, .slot = 0x70, .check = 0x5a8c},
    {.vtable = 0x37d2b40, .slot = 0x88, .check = 0x628c},
    {.vtable = 0x37d2b40, .slot = 0xd8, .check = 0xaa2c},
    {.vtable = 0x37d2b40, .slot = 0xe0, .check = 0xcc7c},
    {.vtable = 0x37d9a10, .slot = 0x40, .check = 0x2741},
    {.vtable = 0x37d9a10, .slot = 0x58, .check = 0xaf90},
    {.vtable = 0x37dbe90, .slot = 0x0, .check = 0xb8f1},
    {.vtable = 0x37dc3a0, .slot = 0x40, .check = 0x5010},
    {.vtable = 0x37dc3a0, .slot = 0x58, .check = 0xb53d},
    {.vtable = 0x37dca60, .slot = 0x40, .check = 0x0444},
    {.vtable = 0x37dca60, .slot = 0x58, .check = 0x7cf9},
    {.vtable = 0x37dcb30, .slot = 0x40, .check = 0xdc18},
    {.vtable = 0x37dcb30, .slot = 0x58, .check = 0x0945},
    {.vtable = 0x37dcc00, .slot = 0x40, .check = 0xe74c},
    {.vtable = 0x37dcc00, .slot = 0x58, .check = 0xa241},
    {.vtable = 0x37dccd0, .slot = 0x40, .check = 0xa2a0},
    {.vtable = 0x37dccd0, .slot = 0x58, .check = 0xdfad},
    {.vtable = 0x37dce10, .slot = 0x40, .check = 0x2bb4},
    {.vtable = 0x37dce10, .slot = 0x58, .check = 0xba09},
    {.vtable = 0x37dcee0, .slot = 0x40, .check = 0x3128},
    {.vtable = 0x37dcee0, .slot = 0x58, .check = 0xd3f5},
    {.vtable = 0x37dcfb0, .slot = 0x40, .check = 0xedbc},
    {.vtable = 0x37dcfb0, .slot = 0x58, .check = 0x9e51},
    {.vtable = 0x37dd080, .slot = 0x40, .check = 0x50b0},
    {.vtable = 0x37dd080, .slot = 0x58, .check = 0x5c5d},
    {.vtable = 0x37dd150, .slot = 0x70, .check = 0xdbbc},
    {.vtable = 0x37dd150, .slot = 0x88, .check = 0x961c},
    {.vtable = 0x37dd150, .slot = 0xd8, .check = 0xaf5c},
    {.vtable = 0x37dd150, .slot = 0xe0, .check = 0xce6c},
    {.vtable = 0x37dd260, .slot = 0x70, .check = 0x6469},
    {.vtable = 0x37dd260, .slot = 0x88, .check = 0x90e6},
    {.vtable = 0x37dd260, .slot = 0xd8, .check = 0xa9be},
    {.vtable = 0x37dd260, .slot = 0xe0, .check = 0x50b6},
    {.vtable = 0x37dd370, .slot = 0x70, .check = 0x65bf},
    {.vtable = 0x37dd370, .slot = 0x88, .check = 0xb5eb},
    {.vtable = 0x37dd370, .slot = 0xd8, .check = 0x021f},
    {.vtable = 0x37dd370, .slot = 0xe0, .check = 0x83bb},
    {.vtable = 0x37dd480, .slot = 0x70, .check = 0xf05e},
    {.vtable = 0x37dd480, .slot = 0x88, .check = 0x5a66},
    {.vtable = 0x37dd480, .slot = 0xd8, .check = 0xc43e},
    {.vtable = 0x37dd480, .slot = 0xe0, .check = 0xdc36},
    {.vtable = 0x37dd590, .slot = 0x70, .check = 0x823f},
    {.vtable = 0x37dd590, .slot = 0x88, .check = 0x626b},
    {.vtable = 0x37dd590, .slot = 0xd8, .check = 0x5f9f},
    {.vtable = 0x37dd590, .slot = 0xe0, .check = 0x703b},
    {.vtable = 0x37dd6a0, .slot = 0x70, .check = 0x8dde},
    {.vtable = 0x37dd6a0, .slot = 0x88, .check = 0xa7e6},
    {.vtable = 0x37dd6a0, .slot = 0xd8, .check = 0xa0be},
    {.vtable = 0x37dd6a0, .slot = 0xe0, .check = 0x27b6},
    {.vtable = 0x37dd7b0, .slot = 0x70, .check = 0x019e},
    {.vtable = 0x37dd7b0, .slot = 0x88, .check = 0xc2a6},
    {.vtable = 0x37dd7b0, .slot = 0xd8, .check = 0x52fe},
    {.vtable = 0x37dd7b0, .slot = 0xe0, .check = 0xe276},
    {.vtable = 0x37dd8c0, .slot = 0x70, .check = 0x49ff},
    {.vtable = 0x37dd8c0, .slot = 0x88, .check = 0x182b},
    {.vtable = 0x37dd8c0, .slot = 0xd8, .check = 0x0cdf},
    {.vtable = 0x37dd8c0, .slot = 0xe0, .check = 0xe3fb},
    {.vtable = 0x37dd9d0, .slot = 0x70, .check = 0x191e},
    {.vtable = 0x37dd9d0, .slot = 0x88, .check = 0xca26},
    {.vtable = 0x37dd9d0, .slot = 0xd8, .check = 0x6b7e},
    {.vtable = 0x37dd9d0, .slot = 0xe0, .check = 0x2bf6},
    {.vtable = 0x37ddae0, .slot = 0x70, .check = 0xa27f},
    {.vtable = 0x37ddae0, .slot = 0x88, .check = 0x80ab},
    {.vtable = 0x37ddae0, .slot = 0xd8, .check = 0x645f},
    {.vtable = 0x37ddae0, .slot = 0xe0, .check = 0x4c7b},
    {.vtable = 0x37ddbf0, .slot = 0x70, .check = 0xb29e},
    {.vtable = 0x37ddbf0, .slot = 0x88, .check = 0x93a6},
    {.vtable = 0x37ddbf0, .slot = 0xd8, .check = 0x43fe},
    {.vtable = 0x37ddbf0, .slot = 0xe0, .check = 0xf376},
    {.vtable = 0x37ddd00, .slot = 0x70, .check = 0x7aff},
    {.vtable = 0x37ddd00, .slot = 0x88, .check = 0x692b},
    {.vtable = 0x37ddd00, .slot = 0xd8, .check = 0x7ddf},
    {.vtable = 0x37ddd00, .slot = 0xe0, .check = 0x74fb},
    {.vtable = 0x37dde10, .slot = 0x70, .check = 0xca1e},
    {.vtable = 0x37dde10, .slot = 0x88, .check = 0x9b26},
    {.vtable = 0x37dde10, .slot = 0xd8, .check = 0x5c7e},
    {.vtable = 0x37dde10, .slot = 0xe0, .check = 0x3cf6},
    {.vtable = 0x37ddf20, .slot = 0x70, .check = 0xd37f},
    {.vtable = 0x37ddf20, .slot = 0x88, .check = 0xd1ab},
    {.vtable = 0x37ddf20, .slot = 0xd8, .check = 0xd55f},
    {.vtable = 0x37ddf20, .slot = 0xe0, .check = 0xdd7b},
    {.vtable = 0x37de030, .slot = 0x70, .check = 0x639e},
    {.vtable = 0x37de030, .slot = 0x88, .check = 0x95af},
    {.vtable = 0x37de030, .slot = 0xd8, .check = 0x23ce},
    {.vtable = 0x37de030, .slot = 0xe0, .check = 0x8b06},
    {.vtable = 0x37de140, .slot = 0x70, .check = 0xe2cf},
    {.vtable = 0x37de140, .slot = 0x88, .check = 0xecdb},
    {.vtable = 0x37de140, .slot = 0xd8, .check = 0x366f},
    {.vtable = 0x37de140, .slot = 0xe0, .check = 0x300b},
    {.vtable = 0x37de250, .slot = 0x70, .check = 0x1f6e},
    {.vtable = 0x37de250, .slot = 0x88, .check = 0xd896},
    {.vtable = 0x37de250, .slot = 0xd8, .check = 0xe84e},
    {.vtable = 0x37de250, .slot = 0xe0, .check = 0x0086},
    {.vtable = 0x37de3c0, .slot = 0x70, .check = 0xe74f},
    {.vtable = 0x37de3c0, .slot = 0x88, .check = 0x815b},
    {.vtable = 0x37de3c0, .slot = 0xd8, .check = 0x7bef},
    {.vtable = 0x37de3c0, .slot = 0xe0, .check = 0x048b},
    {.vtable = 0x37de520, .slot = 0x70, .check = 0x62ee},
    {.vtable = 0x37de520, .slot = 0x88, .check = 0x8c16},
    {.vtable = 0x37de520, .slot = 0xd8, .check = 0x2cce},
    {.vtable = 0x37de520, .slot = 0xe0, .check = 0xb406},
    {.vtable = 0x37de630, .slot = 0x70, .check = 0x2bcf},
    {.vtable = 0x37de630, .slot = 0x88, .check = 0x55db},
    {.vtable = 0x37de630, .slot = 0xd8, .check = 0xbf6f},
    {.vtable = 0x37de630, .slot = 0xe0, .check = 0xd90b},
    {.vtable = 0x37de740, .slot = 0x70, .check = 0xe86e},
    {.vtable = 0x37de740, .slot = 0x88, .check = 0xc196},
    {.vtable = 0x37de740, .slot = 0xd8, .check = 0xf14e},
    {.vtable = 0x37de740, .slot = 0xe0, .check = 0x2986},
    {.vtable = 0x37de850, .slot = 0x70, .check = 0x304f},
    {.vtable = 0x37de850, .slot = 0x88, .check = 0xea5b},
    {.vtable = 0x37de850, .slot = 0xd8, .check = 0x04ef},
    {.vtable = 0x37de850, .slot = 0xe0, .check = 0xad8b},
    {.vtable = 0x37de960, .slot = 0x70, .check = 0x2bee},
    {.vtable = 0x37de960, .slot = 0x88, .check = 0x7516},
    {.vtable = 0x37de960, .slot = 0xd8, .check = 0x35ce},
    {.vtable = 0x37de960, .slot = 0xe0, .check = 0xdd06},
    {.vtable = 0x37dea70, .slot = 0x70, .check = 0x74cf},
    {.vtable = 0x37dea70, .slot = 0x88, .check = 0xbedb},
    {.vtable = 0x37dea70, .slot = 0xd8, .check = 0x486f},
    {.vtable = 0x37dea70, .slot = 0xe0, .check = 0x820b},
    {.vtable = 0x37deb80, .slot = 0x70, .check = 0xb16e},
    {.vtable = 0x37deb80, .slot = 0x88, .check = 0xaa96},
    {.vtable = 0x37deb80, .slot = 0xd8, .check = 0xfa4e},
    {.vtable = 0x37deb80, .slot = 0xe0, .check = 0x5286},
    {.vtable = 0x37dec90, .slot = 0x70, .check = 0x794f},
    {.vtable = 0x37dec90, .slot = 0x88, .check = 0x535b},
    {.vtable = 0x37dec90, .slot = 0xd8, .check = 0x8def},
    {.vtable = 0x37dec90, .slot = 0xe0, .check = 0x568b},
    {.vtable = 0x37deda0, .slot = 0x70, .check = 0xf4ee},
    {.vtable = 0x37deda0, .slot = 0x88, .check = 0x5e16},
    {.vtable = 0x37deda0, .slot = 0xd8, .check = 0x3ece},
    {.vtable = 0x37deda0, .slot = 0xe0, .check = 0x0606},
    {.vtable = 0x37deeb0, .slot = 0x70, .check = 0xbdcf},
    {.vtable = 0x37deeb0, .slot = 0x88, .check = 0x27db},
    {.vtable = 0x37deeb0, .slot = 0xd8, .check = 0xd16f},
    {.vtable = 0x37deeb0, .slot = 0xe0, .check = 0x2b0b},
    {.vtable = 0x37defc0, .slot = 0x70, .check = 0x7a6e},
    {.vtable = 0x37defc0, .slot = 0x88, .check = 0x9396},
    {.vtable = 0x37defc0, .slot = 0xd8, .check = 0x034e},
    {.vtable = 0x37defc0, .slot = 0xe0, .check = 0x7b86},
    {.vtable = 0x37df0d0, .slot = 0x70, .check = 0xc24f},
    {.vtable = 0x37df0d0, .slot = 0x88, .check = 0xbc5b},
    {.vtable = 0x37df0d0, .slot = 0xd8, .check = 0x16ef},
    {.vtable = 0x37df0d0, .slot = 0xe0, .check = 0xff8b},
    {.vtable = 0x37df1e0, .slot = 0x70, .check = 0xbdee},
    {.vtable = 0x37df1e0, .slot = 0x88, .check = 0x4716},
    {.vtable = 0x37df1e0, .slot = 0xd8, .check = 0x47ce},
    {.vtable = 0x37df1e0, .slot = 0xe0, .check = 0x2f06},
    {.vtable = 0x37df2f0, .slot = 0x70, .check = 0x06cf},
    {.vtable = 0x37df2f0, .slot = 0x88, .check = 0x90db},
    {.vtable = 0x37df2f0, .slot = 0xd8, .check = 0x5a6f},
    {.vtable = 0x37df2f0, .slot = 0xe0, .check = 0xd40b},
    {.vtable = 0x37df400, .slot = 0x70, .check = 0x436e},
    {.vtable = 0x37df400, .slot = 0x88, .check = 0x7c96},
    {.vtable = 0x37df400, .slot = 0xd8, .check = 0x0c4e},
    {.vtable = 0x37df400, .slot = 0xe0, .check = 0x4b47},
    {.vtable = 0x37df510, .slot = 0x70, .check = 0x4c72},
    {.vtable = 0x37df510, .slot = 0x88, .check = 0xa092},
    {.vtable = 0x37df510, .slot = 0xd8, .check = 0x7492},
    {.vtable = 0x37df510, .slot = 0xe0, .check = 0xc0e2},
    {.vtable = 0x37df620, .slot = 0x70, .check = 0xe433},
    {.vtable = 0x37df620, .slot = 0x88, .check = 0xf7b7},
    {.vtable = 0x37df620, .slot = 0xd8, .check = 0xc813},
    {.vtable = 0x37df620, .slot = 0xe0, .check = 0xfd07},
    {.vtable = 0x37df730, .slot = 0x38, .check = 0x9214},
    {.vtable = 0x37df730, .slot = 0x68, .check = 0x33a3},
    {.vtable = 0x37df730, .slot = 0x88, .check = 0x2c1d},
    {.vtable = 0x37df730, .slot = 0x98, .check = 0xc833},
    {.vtable = 0x37df730, .slot = 0xa0, .check = 0x611d},
    {.vtable = 0x37df730, .slot = 0xa8, .check = 0x3c90},
    {.vtable = 0x37df730, .slot = 0xb0, .check = 0x21b7},
    {.vtable = 0x37df730, .slot = 0x1b0, .check = 0x157d},
    {.vtable = 0x37df730, .slot = 0x1b8, .check = 0xc507},
    {.vtable = 0x37df730, .slot = 0x1f8, .check = 0xdd1d},
    {.vtable = 0x37df730, .slot = 0x200, .check = 0x1d37},
    {.vtable = 0x37df730, .slot = 0x208, .check = 0xc7ad},
    {.vtable = 0x37df730, .slot = 0x210, .check = 0x62f0},
    {.vtable = 0x37df730, .slot = 0x218, .check = 0xdac1},
    {.vtable = 0x37df730, .slot = 0x228, .check = 0xbcd4},
    {.vtable = 0x37df730, .slot = 0x238, .check = 0xd0a5},
    {.vtable = 0x37df730, .slot = 0x240, .check = 0xcd08},
    {.vtable = 0x37df730, .slot = 0x2b0, .check = 0x49c6},
    {.vtable = 0x37df730, .slot = 0x360, .check = 0x203a},
    {.vtable = 0x37df730, .slot = 0x368, .check = 0x55de},
    {.vtable = 0x37dfe60, .slot = 0x38, .check = 0xb7b5},
    {.vtable = 0x37dfe60, .slot = 0x68, .check = 0x7d52},
    {.vtable = 0x37dfe60, .slot = 0x88, .check = 0x01d8},
    {.vtable = 0x37dfe60, .slot = 0x98, .check = 0x7362},
    {.vtable = 0x37dfe60, .slot = 0xa0, .check = 0x7ff8},
    {.vtable = 0x37dfe60, .slot = 0xa8, .check = 0x8585},
    {.vtable = 0x37dfe60, .slot = 0xb0, .check = 0x1fe2},
    {.vtable = 0x37dfe60, .slot = 0x1b0, .check = 0xec80},
    {.vtable = 0x37dfe60, .slot = 0x1b8, .check = 0xbeba},
    {.vtable = 0x37dfe60, .slot = 0x1f8, .check = 0x65e8},
    {.vtable = 0x37dfe60, .slot = 0x200, .check = 0x66b2},
    {.vtable = 0x37dfe60, .slot = 0x208, .check = 0x5f60},
    {.vtable = 0x37dfe60, .slot = 0x210, .check = 0x6741},
    {.vtable = 0x37dfe60, .slot = 0x218, .check = 0xea74},
    {.vtable = 0x37dfe60, .slot = 0x228, .check = 0x8765},
    {.vtable = 0x37dfe60, .slot = 0x238, .check = 0x7898},
    {.vtable = 0x37dfe60, .slot = 0x240, .check = 0x16d9},
    {.vtable = 0x37dfe60, .slot = 0x2b0, .check = 0x616b},
    {.vtable = 0x37dfe60, .slot = 0x360, .check = 0xcafb},
    {.vtable = 0x37dfe60, .slot = 0x368, .check = 0x437b},
    {.vtable = 0x37e0590, .slot = 0x38, .check = 0x37e0},
    {.vtable = 0x37e0590, .slot = 0x68, .check = 0x1fc7},
    {.vtable = 0x37e0590, .slot = 0x88, .check = 0x9669},
    {.vtable = 0x37e0590, .slot = 0x98, .check = 0xccb7},
    {.vtable = 0x37e0590, .slot = 0xa0, .check = 0xf649},
    {.vtable = 0x37e0590, .slot = 0xa8, .check = 0x1704},
    {.vtable = 0x37e0590, .slot = 0xb0, .check = 0xd853},
    {.vtable = 0x37e0590, .slot = 0x1b0, .check = 0xc639},
    {.vtable = 0x37e0590, .slot = 0x1b8, .check = 0x2d23},
    {.vtable = 0x37e0590, .slot = 0x1f8, .check = 0x2659},
    {.vtable = 0x37e0590, .slot = 0x200, .check = 0x0cd3},
    {.vtable = 0x37e0590, .slot = 0x208, .check = 0x8e29},
    {.vtable = 0x37e0590, .slot = 0x210, .check = 0x594c},
    {.vtable = 0x37e0590, .slot = 0x218, .check = 0x261d},
    {.vtable = 0x37e0590, .slot = 0x228, .check = 0x9170},
    {.vtable = 0x37e0590, .slot = 0x238, .check = 0xc701},
    {.vtable = 0x37e0590, .slot = 0x240, .check = 0xb7e4},
    {.vtable = 0x37e0590, .slot = 0x2b0, .check = 0x6a62},
    {.vtable = 0x37e0590, .slot = 0x360, .check = 0xc076},
    {.vtable = 0x37e0590, .slot = 0x368, .check = 0x769a},
    {.vtable = 0x37e0cc0, .slot = 0x38, .check = 0xeb79},
    {.vtable = 0x37e0cc0, .slot = 0x68, .check = 0xb00e},
    {.vtable = 0x37e0cc0, .slot = 0x88, .check = 0x1fac},
    {.vtable = 0x37e0cc0, .slot = 0x98, .check = 0xb50e},
    {.vtable = 0x37e0cc0, .slot = 0xa0, .check = 0x6cfc},
    {.vtable = 0x37e0cc0, .slot = 0xa8, .check = 0x7241},
    {.vtable = 0x37e0cc0, .slot = 0xb0, .check = 0x98f6},
    {.vtable = 0x37e0cc0, .slot = 0x1b0, .check = 0x6e14},
    {.vtable = 0x37e0cc0, .slot = 0x1b8, .check = 0x26ce},
    {.vtable = 0x37e0cc0, .slot = 0x1f8, .check = 0xee5c},
    {.vtable = 0x37e0cc0, .slot = 0x200, .check = 0x5126},
    {.vtable = 0x37e0cc0, .slot = 0x208, .check = 0x6a74},
    {.vtable = 0x37e0cc0, .slot = 0x210, .check = 0xd135},
    {.vtable = 0x37e0cc0, .slot = 0x218, .check = 0xba28},
    {.vtable = 0x37e0cc0, .slot = 0x228, .check = 0x2db9},
    {.vtable = 0x37e0cc0, .slot = 0x238, .check = 0x200c},
    {.vtable = 0x37e0cc0, .slot = 0x240, .check = 0x4e8d},
    {.vtable = 0x37e0cc0, .slot = 0x2b0, .check = 0x227f},
    {.vtable = 0x37e0cc0, .slot = 0x360, .check = 0xaaef},
    {.vtable = 0x37e0cc0, .slot = 0x368, .check = 0x226f},
    {.vtable = 0x37e13f0, .slot = 0x38, .check = 0xac1c},
    {.vtable = 0x37e13f0, .slot = 0x68, .check = 0xdd6b},
    {.vtable = 0x37e13f0, .slot = 0x88, .check = 0x1155},
    {.vtable = 0x37e13f0, .slot = 0x98, .check = 0xd77b},
    {.vtable = 0x37e13f0, .slot = 0xa0, .check = 0x5bd5},
    {.vtable = 0x37e13f0, .slot = 0xa8, .check = 0xe008},
    {.vtable = 0x37e13f0, .slot = 0xb0, .check = 0x974f},
    {.vtable = 0x37e13f0, .slot = 0x1b0, .check = 0x9095},
    {.vtable = 0x37e13f0, .slot = 0x1b8, .check = 0xf19f},
    {.vtable = 0x37e13f0, .slot = 0x1f8, .check = 0x3cd5},
    {.vtable = 0x37e13f0, .slot = 0x200, .check = 0x322f},
    {.vtable = 0x37e13f0, .slot = 0x208, .check = 0x1bc5},
    {.vtable = 0x37e13f0, .slot = 0x210, .check = 0x7808},
    {.vtable = 0x37e13f0, .slot = 0x218, .check = 0xabb9},
    {.vtable = 0x37e13f0, .slot = 0x228, .check = 0x036c},
    {.vtable = 0x37e13f0, .slot = 0x238, .check = 0x5a3d},
    {.vtable = 0x37e13f0, .slot = 0x240, .check = 0x43c0},
    {.vtable = 0x37e13f0, .slot = 0x2b0, .check = 0x1a8e},
    {.vtable = 0x37e13f0, .slot = 0x360, .check = 0x9e22},
    {.vtable = 0x37e13f0, .slot = 0x368, .check = 0x7626},
    {.vtable = 0x37e1b20, .slot = 0x38, .check = 0xb9fd},
    {.vtable = 0x37e1b20, .slot = 0x68, .check = 0xfcda},
    {.vtable = 0x37e1b20, .slot = 0x88, .check = 0xd270},
    {.vtable = 0x37e1b20, .slot = 0x98, .check = 0x9cea},
    {.vtable = 0x37e1b20, .slot = 0xa0, .check = 0x7310},
    {.vtable = 0x37e1b20, .slot = 0xa8, .check = 0x4cdd},
    {.vtable = 0x37e1b20, .slot = 0xb0, .check = 0x39fa},
    {.vtable = 0x37e1b20, .slot = 0x1b0, .check = 0xc098},
    {.vtable = 0x37e1b20, .slot = 0x1b8, .check = 0x3072},
    {.vtable = 0x37e1b20, .slot = 0x1f8, .check = 0x1d80},
    {.vtable = 0x37e1b20, .slot = 0x200, .check = 0x80aa},
    {.vtable = 0x37e1b20, .slot = 0x208, .check = 0x1f78},
    {.vtable = 0x37e1b20, .slot = 0x210, .check = 0x8d59},
    {.vtable = 0x37e1b20, .slot = 0x218, .check = 0xee2c},
    {.vtable = 0x37e1b20, .slot = 0x228, .check = 0x5cdd},
    {.vtable = 0x37e1b20, .slot = 0x238, .check = 0x1690},
    {.vtable = 0x37e1b20, .slot = 0x240, .check = 0x7c31},
    {.vtable = 0x37e1b20, .slot = 0x2b0, .check = 0x80d3},
    {.vtable = 0x37e1b20, .slot = 0x360, .check = 0x69c3},
    {.vtable = 0x37e1b20, .slot = 0x368, .check = 0x72c3},
    {.vtable = 0x37e2250, .slot = 0x38, .check = 0x5008},
    {.vtable = 0x37e2250, .slot = 0x68, .check = 0xf50f},
    {.vtable = 0x37e2250, .slot = 0x88, .check = 0x1581},
    {.vtable = 0x37e2250, .slot = 0x98, .check = 0x1e3f},
    {.vtable = 0x37e2250, .slot = 0xa0, .check = 0xb921},
    {.vtable = 0x37e2250, .slot = 0xa8, .check = 0x7e9c},
    {.vtable = 0x37e2250, .slot = 0xb0, .check = 0xdd2b},
    {.vtable = 0x37e2250, .slot = 0x1b0, .check = 0xcd31},
    {.vtable = 0x37e2250, .slot = 0x1b8, .check = 0x1b7b},
    {.vtable = 0x37e2250, .slot = 0x1f8, .check = 0xdd71},
    {.vtable = 0x37e2250, .slot = 0x200, .check = 0xbc4b},
    {.vtable = 0x37e2250, .slot = 0x208, .check = 0x0be1},
    {.vtable = 0x37e2250, .slot = 0x210, .check = 0x8c64},
    {.vtable = 0x37e2250, .slot = 0x218, .check = 0x3335},
    {.vtable = 0x37e2250, .slot = 0x228, .check = 0x6d48},
    {.vtable = 0x37e2250, .slot = 0x238, .check = 0x40b9},
    {.vtable = 0x37e2250, .slot = 0x240, .check = 0x259c},
    {.vtable = 0x37e2250, .slot = 0x2b0, .check = 0x9eea},
    {.vtable = 0x37e2250, .slot = 0x360, .check = 0x5c9e},
    {.vtable = 0x37e2250, .slot = 0x368, .check = 0x2022},
    {.vtable = 0x37e2980, .slot = 0x38, .check = 0x9501},
    {.vtable = 0x37e2980, .slot = 0x68, .check = 0x0b76},
    {.vtable = 0x37e2980, .slot = 0x88, .check = 0x8a04},
    {.vtable = 0x37e2980, .slot = 0x98, .check = 0xd0b6},
    {.vtable = 0x37e2980, .slot = 0xa0, .check = 0xcf34},
    {.vtable = 0x37e2980, .slot = 0xa8, .check = 0xbeb9},
    {.vtable = 0x37e2980, .slot = 0xb0, .check = 0xe30e},
    {.vtable = 0x37e2980, .slot = 0x1b0, .check = 0x49ac},
    {.vtable = 0x37e2980, .slot = 0x1b8, .check = 0xe566},
    {.vtable = 0x37e2980, .slot = 0x1f8, .check = 0x5b14},
    {.vtable = 0x37e2980, .slot = 0x200, .check = 0x551e},
    {.vtable = 0x37e2980, .slot = 0x208, .check = 0x8d8c},
    {.vtable = 0x37e2980, .slot = 0x210, .check = 0xa3ed},
    {.vtable = 0x37e2980, .slot = 0x218, .check = 0x05a0},
    {.vtable = 0x37e2980, .slot = 0x228, .check = 0x5e11},
    {.vtable = 0x37e2980, .slot = 0x238, .check = 0x7904},
    {.vtable = 0x37e2980, .slot = 0x240, .check = 0x77c5},
    {.vtable = 0x37e2980, .slot = 0x2b0, .check = 0xd0c7},
    {.vtable = 0x37e2980, .slot = 0x360, .check = 0xb377},
    {.vtable = 0x37e2980, .slot = 0x368, .check = 0xbcd7},
    {.vtable = 0x37e30b0, .slot = 0x38, .check = 0x616a},
    {.vtable = 0x37e30b0, .slot = 0x98, .check = 0xd788},
    {.vtable = 0x37e30b0, .slot = 0xa0, .check = 0xe73a},
    {.vtable = 0x37e30b0, .slot = 0xb0, .check = 0x7e29},
    {.vtable = 0x37e3300, .slot = 0x20, .check = 0xed02},
    {.vtable = 0x37e33c0, .slot = 0x38, .check = 0x0a47},
    {.vtable = 0x37e33c0, .slot = 0x98, .check = 0x2ad1},
    {.vtable = 0x37e33c0, .slot = 0xa0, .check = 0x0657},
    {.vtable = 0x37e33c0, .slot = 0xb0, .check = 0xddd4},
    {.vtable = 0x37e3710, .slot = 0x10, .check = 0x09a3},
    {.vtable = 0x37e37a0, .slot = 0x20, .check = 0x517a},
    {.vtable = 0x37e37a0, .slot = 0x30, .check = 0x8803},
    {.vtable = 0x37e37a0, .slot = 0x68, .check = 0xd80a},
    {.vtable = 0x37e37a0, .slot = 0x78, .check = 0x1383},
    {.vtable = 0x37e3870, .slot = 0x10, .check = 0x9b30},
    {.vtable = 0x37e38a0, .slot = 0x20, .check = 0x2219},
    {.vtable = 0x37e38a0, .slot = 0x30, .check = 0x5620},
    {.vtable = 0x37e38a0, .slot = 0x68, .check = 0xf579},
    {.vtable = 0x37e38a0, .slot = 0x78, .check = 0xf450},
    {.vtable = 0x37e3970, .slot = 0x10, .check = 0xb2bf},
    {.vtable = 0x37e4600, .slot = 0x10, .check = 0x779c},
    {.vtable = 0x37e5700, .slot = 0x70, .check = 0x9c99},
    {.vtable = 0x37e5700, .slot = 0x88, .check = 0xdcbd},
    {.vtable = 0x37e5700, .slot = 0xd8, .check = 0x4639},
    {.vtable = 0x37e5700, .slot = 0xe0, .check = 0xbd4d},
    {.vtable = 0x37e90f0, .slot = 0x38, .check = 0xcafa},
    {.vtable = 0x37e90f0, .slot = 0x68, .check = 0xc129},
    {.vtable = 0x37e90f0, .slot = 0x88, .check = 0x7947},
    {.vtable = 0x37e90f0, .slot = 0x98, .check = 0xd959},
    {.vtable = 0x37e90f0, .slot = 0xa0, .check = 0xb2e7},
    {.vtable = 0x37e90f0, .slot = 0xa8, .check = 0x8106},
    {.vtable = 0x37e90f0, .slot = 0xb0, .check = 0x58cd},
    {.vtable = 0x37e90f0, .slot = 0x1b0, .check = 0x3027},
    {.vtable = 0x37e90f0, .slot = 0x1b8, .check = 0xf3fd},
    {.vtable = 0x37e90f0, .slot = 0x1f8, .check = 0x6307},
    {.vtable = 0x37e90f0, .slot = 0x200, .check = 0xbc8d},
    {.vtable = 0x37e90f0, .slot = 0x208, .check = 0xcbb7},
    {.vtable = 0x37e90f0, .slot = 0x210, .check = 0x88a6},
    {.vtable = 0x37e90f0, .slot = 0x218, .check = 0xc2ab},
    {.vtable = 0x37e90f0, .slot = 0x228, .check = 0x7e6a},
    {.vtable = 0x37e90f0, .slot = 0x238, .check = 0xf9af},
    {.vtable = 0x37e90f0, .slot = 0x240, .check = 0xb7be},
    {.vtable = 0x37e90f0, .slot = 0x2b0, .check = 0xa050},
    {.vtable = 0x37e90f0, .slot = 0x360, .check = 0xdc04},
    {.vtable = 0x37e90f0, .slot = 0x368, .check = 0x1748},
    {.vtable = 0x37e9820, .slot = 0x38, .check = 0x2e9f},
    {.vtable = 0x37e9820, .slot = 0x68, .check = 0xfe7c},
    {.vtable = 0x37e9820, .slot = 0x88, .check = 0x196e},
    {.vtable = 0x37e9820, .slot = 0x98, .check = 0x3ebc},
    {.vtable = 0x37e9820, .slot = 0xa0, .check = 0xd3de},
    {.vtable = 0x37e9820, .slot = 0xa8, .check = 0xb94f},
    {.vtable = 0x37e9820, .slot = 0xb0, .check = 0x56ec},
    {.vtable = 0x37e9820, .slot = 0x1b0, .check = 0xde36},
    {.vtable = 0x37e9820, .slot = 0x1b8, .check = 0xf464},
    {.vtable = 0x37e9820, .slot = 0x1f8, .check = 0x34de},
    {.vtable = 0x37e9820, .slot = 0x200, .check = 0x723c},
    {.vtable = 0x37e9820, .slot = 0x208, .check = 0x9d96},
    {.vtable = 0x37e9820, .slot = 0x210, .check = 0xc1cb},
    {.vtable = 0x37e9820, .slot = 0x218, .check = 0x6faa},
    {.vtable = 0x37e9820, .slot = 0x228, .check = 0x654f},
    {.vtable = 0x37e9820, .slot = 0x238, .check = 0x026e},
    {.vtable = 0x37e9820, .slot = 0x240, .check = 0xd403},
    {.vtable = 0x37e9820, .slot = 0x2b0, .check = 0xe031},
    {.vtable = 0x37e9820, .slot = 0x360, .check = 0x24e1},
    {.vtable = 0x37e9820, .slot = 0x368, .check = 0xb1a1},
    {.vtable = 0x37e9f50, .slot = 0x38, .check = 0x8266},
    {.vtable = 0x37e9f50, .slot = 0x68, .check = 0xb36d},
    {.vtable = 0x37e9f50, .slot = 0x88, .check = 0xe773},
    {.vtable = 0x37e9f50, .slot = 0x98, .check = 0xaa3d},
    {.vtable = 0x37e9f50, .slot = 0xa0, .check = 0x0633},
    {.vtable = 0x37e9f50, .slot = 0xa8, .check = 0x27ca},
    {.vtable = 0x37e9f50, .slot = 0xb0, .check = 0xaa39},
    {.vtable = 0x37e9f50, .slot = 0x1b0, .check = 0xd993},
    {.vtable = 0x37e9f50, .slot = 0x1b8, .check = 0xc189},
    {.vtable = 0x37e9f50, .slot = 0x1f8, .check = 0x1ab3},
    {.vtable = 0x37e9f50, .slot = 0x200, .check = 0x5a79},
    {.vtable = 0x37e9f50, .slot = 0x208, .check = 0xf703},
    {.vtable = 0x37e9f50, .slot = 0x210, .check = 0xf332},
    {.vtable = 0x37e9f50, .slot = 0x218, .check = 0xcd97},
    {.vtable = 0x37e9f50, .slot = 0x228, .check = 0xefb6},
    {.vtable = 0x37e9f50, .slot = 0x238, .check = 0xd37b},
    {.vtable = 0x37e9f50, .slot = 0x240, .check = 0x9eea},
    {.vtable = 0x37e9f50, .slot = 0x2b0, .check = 0xb0ec},
    {.vtable = 0x37e9f50, .slot = 0x360, .check = 0x2a40},
    {.vtable = 0x37e9f50, .slot = 0x368, .check = 0xa604},
    {.vtable = 0x37ead20, .slot = 0x10, .check = 0x3433},
    {.vtable = 0x37ead50, .slot = 0x20, .check = 0x915f},
    {.vtable = 0x37ead50, .slot = 0x30, .check = 0xc7c6},
    {.vtable = 0x37ead50, .slot = 0x68, .check = 0xb7bf},
    {.vtable = 0x37ead50, .slot = 0x78, .check = 0xab36},
    {.vtable = 0x37eae20, .slot = 0x10, .check = 0x4a19},
    {.vtable = 0x37eae50, .slot = 0x20, .check = 0x7ad9},
    {.vtable = 0x37eae50, .slot = 0x30, .check = 0xc3a4},
    {.vtable = 0x37eae50, .slot = 0x68, .check = 0xeccd},
    {.vtable = 0x37eae50, .slot = 0x78, .check = 0xf1d4},
    {.vtable = 0x37eaf20, .slot = 0x10, .check = 0x9da3},
    {.vtable = 0x37eaf50, .slot = 0x38, .check = 0xc20e},
    {.vtable = 0x37eaf50, .slot = 0x98, .check = 0xf624},
    {.vtable = 0x37eaf50, .slot = 0xa0, .check = 0xe45e},
    {.vtable = 0x37eaf50, .slot = 0xb0, .check = 0xde45},
    {.vtable = 0x37f2ff0, .slot = 0x38, .check = 0x29b2},
    {.vtable = 0x37f2ff0, .slot = 0x68, .check = 0xe0c1},
    {.vtable = 0x37f2ff0, .slot = 0x88, .check = 0xdf4f},
    {.vtable = 0x37f2ff0, .slot = 0x98, .check = 0xf2f1},
    {.vtable = 0x37f2ff0, .slot = 0xa0, .check = 0x6aef},
    {.vtable = 0x37f2ff0, .slot = 0xa8, .check = 0x5c0e},
    {.vtable = 0x37f2ff0, .slot = 0xb0, .check = 0x8c75},
    {.vtable = 0x37f2ff0, .slot = 0x1b0, .check = 0x822f},
    {.vtable = 0x37f2ff0, .slot = 0x1b8, .check = 0x1125},
    {.vtable = 0x37f2ff0, .slot = 0x1f8, .check = 0xeeef},
    {.vtable = 0x37f2ff0, .slot = 0x200, .check = 0xc795},
    {.vtable = 0x37f2ff0, .slot = 0x208, .check = 0xe6ff},
    {.vtable = 0x37f2ff0, .slot = 0x210, .check = 0x997e},
    {.vtable = 0x37f2ff0, .slot = 0x218, .check = 0xa5a3},
    {.vtable = 0x37f2ff0, .slot = 0x228, .check = 0xdf22},
    {.vtable = 0x37f2ff0, .slot = 0x238, .check = 0xb1b7},
    {.vtable = 0x37f2ff0, .slot = 0x240, .check = 0xe486},
    {.vtable = 0x37f2ff0, .slot = 0x2b0, .check = 0x10a2},
    {.vtable = 0x37f2ff0, .slot = 0x360, .check = 0x3cb6},
    {.vtable = 0x37f2ff0, .slot = 0x368, .check = 0x2ada},
    {.vtable = 0x37f4360, .slot = 0x38, .check = 0x1db9},
    {.vtable = 0x37f4360, .slot = 0x68, .check = 0x0bce},
    {.vtable = 0x37f4360, .slot = 0x88, .check = 0xecec},
    {.vtable = 0x37f4360, .slot = 0x98, .check = 0x0fce},
    {.vtable = 0x37f4360, .slot = 0xa0, .check = 0xe43c},
    {.vtable = 0x37f4360, .slot = 0xa8, .check = 0xc381},
    {.vtable = 0x37f4360, .slot = 0xb0, .check = 0x2036},
    {.vtable = 0x37f4360, .slot = 0x1b0, .check = 0xf7d4},
    {.vtable = 0x37f4360, .slot = 0x1b8, .check = 0x070e},
    {.vtable = 0x37f4360, .slot = 0x1f8, .check = 0x121c},
    {.vtable = 0x37f4360, .slot = 0x200, .check = 0xce66},
    {.vtable = 0x37f4360, .slot = 0x208, .check = 0xae34},
    {.vtable = 0x37f4360, .slot = 0x210, .check = 0x3539},
    {.vtable = 0x37f4360, .slot = 0x218, .check = 0xe04c},
    {.vtable = 0x37f4360, .slot = 0x228, .check = 0xe97d},
    {.vtable = 0x37f4360, .slot = 0x238, .check = 0xf91c},
    {.vtable = 0x37f4360, .slot = 0x240, .check = 0x227d},
    {.vtable = 0x37f4360, .slot = 0x2b0, .check = 0xedc1},
    {.vtable = 0x37f4360, .slot = 0x360, .check = 0x9c51},
    {.vtable = 0x37f4360, .slot = 0x368, .check = 0x3bd1},
    {.vtable = 0x37f4d70, .slot = 0x40, .check = 0x7d30},
    {.vtable = 0x37f4d70, .slot = 0x58, .check = 0x39dd},
    {.vtable = 0x37f4e40, .slot = 0x40, .check = 0xada4},
    {.vtable = 0x37f4e40, .slot = 0x58, .check = 0x6d59},
    {.vtable = 0x37f6690, .slot = 0x10, .check = 0x0bea},
    {.vtable = 0x37f6780, .slot = 0x10, .check = 0xe63d},
    {.vtable = 0x37f6800, .slot = 0x20, .check = 0xa8f4},
    {.vtable = 0x37f6800, .slot = 0x30, .check = 0x1e8d},
    {.vtable = 0x37f6800, .slot = 0x68, .check = 0xc8ee},
    {.vtable = 0x37f6800, .slot = 0x78, .check = 0xb71a},
    {.vtable = 0x37f68d0, .slot = 0x10, .check = 0xdc0d},
    {.vtable = 0x37f73b0, .slot = 0x38, .check = 0xb7ba},
    {.vtable = 0x37f73b0, .slot = 0x68, .check = 0x2769},
    {.vtable = 0x37f73b0, .slot = 0x88, .check = 0x8d07},
    {.vtable = 0x37f73b0, .slot = 0x98, .check = 0xa499},
    {.vtable = 0x37f73b0, .slot = 0xa0, .check = 0x8184},
    {.vtable = 0x37f73b0, .slot = 0xa8, .check = 0xd249},
    {.vtable = 0x37f73b0, .slot = 0xb0, .check = 0xf19e},
    {.vtable = 0x37f73b0, .slot = 0x190, .check = 0x911c},
    {.vtable = 0x37f73b0, .slot = 0x198, .check = 0x5bfe},
    {.vtable = 0x37f73b0, .slot = 0x1b0, .check = 0xb2dc},
    {.vtable = 0x37f73b0, .slot = 0x1b8, .check = 0xb449},
    {.vtable = 0x37f73b0, .slot = 0x1f8, .check = 0x6bf3},
    {.vtable = 0x37f73b0, .slot = 0x200, .check = 0xb939},
    {.vtable = 0x37f73b0, .slot = 0x208, .check = 0x4843},
    {.vtable = 0x37f73b0, .slot = 0x210, .check = 0x2ac2},
    {.vtable = 0x37f73b0, .slot = 0x218, .check = 0x85e7},
    {.vtable = 0x37f73b0, .slot = 0x228, .check = 0x6b6e},
    {.vtable = 0x37f73b0, .slot = 0x238, .check = 0x1413},
    {.vtable = 0x37f73b0, .slot = 0x240, .check = 0x7842},
    {.vtable = 0x37f73b0, .slot = 0x2b0, .check = 0xd2e6},
    {.vtable = 0x37f73b0, .slot = 0x360, .check = 0xb81a},
    {.vtable = 0x37f73b0, .slot = 0x368, .check = 0x37fe},
    {.vtable = 0x37f7b90, .slot = 0x10, .check = 0xc061},
    {.vtable = 0x37fa4a0, .slot = 0x38, .check = 0x9826},
    {.vtable = 0x37fa4a0, .slot = 0x68, .check = 0x62ad},
    {.vtable = 0x37fa4a0, .slot = 0x88, .check = 0xbe33},
    {.vtable = 0x37fa4a0, .slot = 0x98, .check = 0x737d},
    {.vtable = 0x37fa4a0, .slot = 0xa0, .check = 0xb7f3},
    {.vtable = 0x37fa4a0, .slot = 0xa8, .check = 0xd58a},
    {.vtable = 0x37fa4a0, .slot = 0xb0, .check = 0x19f9},
    {.vtable = 0x37fa4a0, .slot = 0x1b0, .check = 0x5fd3},
    {.vtable = 0x37fa4a0, .slot = 0x1b8, .check = 0xd049},
    {.vtable = 0x37fa4a0, .slot = 0x1f8, .check = 0x07f3},
    {.vtable = 0x37fa4a0, .slot = 0x200, .check = 0xd539},
    {.vtable = 0x37fa4a0, .slot = 0x208, .check = 0xe443},
    {.vtable = 0x37fa4a0, .slot = 0x210, .check = 0x7e72},
    {.vtable = 0x37fa4a0, .slot = 0x218, .check = 0x91d7},
    {.vtable = 0x37fa4a0, .slot = 0x228, .check = 0x0cf6},
    {.vtable = 0x37fa4a0, .slot = 0x238, .check = 0xec1b},
    {.vtable = 0x37fa4a0, .slot = 0x240, .check = 0xa00a},
    {.vtable = 0x37fa4a0, .slot = 0x2b0, .check = 0x3d0c},
    {.vtable = 0x37fa4a0, .slot = 0x360, .check = 0x9fa0},
    {.vtable = 0x37fa4a0, .slot = 0x368, .check = 0x6224},
    {.vtable = 0x3800a70, .slot = 0x38, .check = 0xa903},
    {.vtable = 0x3800a70, .slot = 0x68, .check = 0xc3f8},
    {.vtable = 0x3800a70, .slot = 0x88, .check = 0xa3f2},
    {.vtable = 0x3800a70, .slot = 0x98, .check = 0xf688},
    {.vtable = 0x3800a70, .slot = 0xa0, .check = 0x8112},
    {.vtable = 0x3800a70, .slot = 0xa8, .check = 0x267b},
    {.vtable = 0x3800a70, .slot = 0xb0, .check = 0x42f0},
    {.vtable = 0x3800a70, .slot = 0x1b0, .check = 0x8c5a},
    {.vtable = 0x3800a70, .slot = 0x1b8, .check = 0x2bc8},
    {.vtable = 0x3800a70, .slot = 0x1f8, .check = 0x5a22},
    {.vtable = 0x3800a70, .slot = 0x200, .check = 0xe3e0},
    {.vtable = 0x3800a70, .slot = 0x208, .check = 0x86fa},
    {.vtable = 0x3800a70, .slot = 0x210, .check = 0xe843},
    {.vtable = 0x3800a70, .slot = 0x218, .check = 0x83e2},
    {.vtable = 0x3800a70, .slot = 0x228, .check = 0xfee7},
    {.vtable = 0x3800a70, .slot = 0x238, .check = 0x6be2},
    {.vtable = 0x3800a70, .slot = 0x240, .check = 0xa037},
    {.vtable = 0x3800a70, .slot = 0x2b0, .check = 0xd03b},
    {.vtable = 0x3800a70, .slot = 0x360, .check = 0x502b},
    {.vtable = 0x3800a70, .slot = 0x368, .check = 0xd02b},
    {.vtable = 0x38011c0, .slot = 0x38, .check = 0xe530},
    {.vtable = 0x38011c0, .slot = 0x68, .check = 0xa097},
    {.vtable = 0x38011c0, .slot = 0x88, .check = 0x3af9},
    {.vtable = 0x38011c0, .slot = 0x98, .check = 0x2367},
    {.vtable = 0x38011c0, .slot = 0xa0, .check = 0xc359},
    {.vtable = 0x38011c0, .slot = 0xa8, .check = 0xf6d4},
    {.vtable = 0x38011c0, .slot = 0xb0, .check = 0x17a3},
    {.vtable = 0x38011c0, .slot = 0x1b0, .check = 0xe2e9},
    {.vtable = 0x38011c0, .slot = 0x1b8, .check = 0x48d3},
    {.vtable = 0x38011c0, .slot = 0x1f8, .check = 0xc8c9},
    {.vtable = 0x38011c0, .slot = 0x200, .check = 0x70e3},
    {.vtable = 0x38011c0, .slot = 0x208, .check = 0x5e39},
    {.vtable = 0x38011c0, .slot = 0x210, .check = 0xc4b4},
    {.vtable = 0x38011c0, .slot = 0x218, .check = 0xfc05},
    {.vtable = 0x38011c0, .slot = 0x228, .check = 0x8d98},
    {.vtable = 0x38011c0, .slot = 0x238, .check = 0x3e19},
    {.vtable = 0x38011c0, .slot = 0x240, .check = 0x88fc},
    {.vtable = 0x38011c0, .slot = 0x2b0, .check = 0x3580},
    {.vtable = 0x38011c0, .slot = 0x360, .check = 0x2754},
    {.vtable = 0x38011c0, .slot = 0x368, .check = 0xf998},
    {.vtable = 0x3801910, .slot = 0x38, .check = 0xd04f},
    {.vtable = 0x3801910, .slot = 0x68, .check = 0x062c},
    {.vtable = 0x3801910, .slot = 0x88, .check = 0x9bde},
    {.vtable = 0x3801910, .slot = 0x98, .check = 0x490c},
    {.vtable = 0x3801910, .slot = 0xa0, .check = 0x2f8e},
    {.vtable = 0x3801910, .slot = 0xa8, .check = 0xff3f},
    {.vtable = 0x3801910, .slot = 0xb0, .check = 0x275c},
    {.vtable = 0x3801910, .slot = 0x1b0, .check = 0x5146},
    {.vtable = 0x3801910, .slot = 0x1b8, .check = 0x80b4},
    {.vtable = 0x3801910, .slot = 0x1f8, .check = 0xc12e},
    {.vtable = 0x3801910, .slot = 0x200, .check = 0xf82c},
    {.vtable = 0x3801910, .slot = 0x208, .check = 0x5106},
    {.vtable = 0x3801910, .slot = 0x210, .check = 0x7497},
    {.vtable = 0x3801910, .slot = 0x218, .check = 0x87d6},
    {.vtable = 0x3801910, .slot = 0x228, .check = 0x6b7b},
    {.vtable = 0x3801910, .slot = 0x238, .check = 0x6bd6},
    {.vtable = 0x3801910, .slot = 0x240, .check = 0x05ab},
    {.vtable = 0x3801910, .slot = 0x2b0, .check = 0x4727},
    {.vtable = 0x3801910, .slot = 0x360, .check = 0xf797},
    {.vtable = 0x3801910, .slot = 0x368, .check = 0xe137},
    {.vtable = 0x3802500, .slot = 0x38, .check = 0x88a4},
    {.vtable = 0x3802500, .slot = 0x68, .check = 0x8cb3},
    {.vtable = 0x3802500, .slot = 0x88, .check = 0xd72d},
    {.vtable = 0x3802500, .slot = 0x98, .check = 0x4da3},
    {.vtable = 0x3802500, .slot = 0xa0, .check = 0xa7ad},
    {.vtable = 0x3802500, .slot = 0xa8, .check = 0xb7a0},
    {.vtable = 0x3802500, .slot = 0xb0, .check = 0x8f47},
    {.vtable = 0x3802500, .slot = 0x1b0, .check = 0x8fed},
    {.vtable = 0x3802500, .slot = 0x1b8, .check = 0xd7f7},
    {.vtable = 0x3802500, .slot = 0x1f8, .check = 0x8dcd},
    {.vtable = 0x3802500, .slot = 0x200, .check = 0x3d07},
    {.vtable = 0x3802500, .slot = 0x208, .check = 0xb17d},
    {.vtable = 0x3802500, .slot = 0x210, .check = 0xaed0},
    {.vtable = 0x3802500, .slot = 0x218, .check = 0x8361},
    {.vtable = 0x3802500, .slot = 0x228, .check = 0x8674},
    {.vtable = 0x3802500, .slot = 0x238, .check = 0xbb85},
    {.vtable = 0x3802500, .slot = 0x240, .check = 0xcde8},
    {.vtable = 0x3802500, .slot = 0x2b0, .check = 0xf1e4},
    {.vtable = 0x3802500, .slot = 0x360, .check = 0x3b78},
    {.vtable = 0x3802500, .slot = 0x368, .check = 0x511c},
    {.vtable = 0x3802c60, .slot = 0x38, .check = 0x1a41},
    {.vtable = 0x3802c60, .slot = 0x98, .check = 0xdb97},
    {.vtable = 0x3802c60, .slot = 0xa0, .check = 0x6551},
    {.vtable = 0x3802c60, .slot = 0xb0, .check = 0xa386},
    {.vtable = 0x3803110, .slot = 0x10, .check = 0x7849},
    {.vtable = 0x3835670, .slot = 0x38, .check = 0x53c4},
    {.vtable = 0x3835670, .slot = 0x98, .check = 0x7cae},
    {.vtable = 0x3835670, .slot = 0xa0, .check = 0x9a84},
    {.vtable = 0x3835670, .slot = 0xb0, .check = 0xaad3},
    {.vtable = 0x38358e0, .slot = 0x38, .check = 0xf578},
    {.vtable = 0x38358e0, .slot = 0x68, .check = 0xaaff},
    {.vtable = 0x38358e0, .slot = 0x88, .check = 0x1df1},
    {.vtable = 0x38358e0, .slot = 0x98, .check = 0xabcf},
    {.vtable = 0x38358e0, .slot = 0xa0, .check = 0xe451},
    {.vtable = 0x38358e0, .slot = 0xa8, .check = 0x14cc},
    {.vtable = 0x38358e0, .slot = 0xb0, .check = 0x205b},
    {.vtable = 0x38358e0, .slot = 0x1b0, .check = 0xf081},
    {.vtable = 0x38358e0, .slot = 0x1b8, .check = 0x418b},
    {.vtable = 0x38358e0, .slot = 0x1f8, .check = 0x43c1},
    {.vtable = 0x38358e0, .slot = 0x200, .check = 0x1a3b},
    {.vtable = 0x38358e0, .slot = 0x208, .check = 0x5441},
    {.vtable = 0x38358e0, .slot = 0x210, .check = 0x2f04},
    {.vtable = 0x38358e0, .slot = 0x218, .check = 0x6e15},
    {.vtable = 0x38358e0, .slot = 0x228, .check = 0xf728},
    {.vtable = 0x38358e0, .slot = 0x238, .check = 0x5109},
    {.vtable = 0x38358e0, .slot = 0x240, .check = 0xb38c},
    {.vtable = 0x38358e0, .slot = 0x2b0, .check = 0xc39a},
    {.vtable = 0x38358e0, .slot = 0x360, .check = 0xdaee},
    {.vtable = 0x38358e0, .slot = 0x368, .check = 0x0132},
    {.vtable = 0x3836010, .slot = 0x38, .check = 0x1771},
    {.vtable = 0x3836010, .slot = 0x68, .check = 0x97e6},
    {.vtable = 0x3836010, .slot = 0x88, .check = 0x4f74},
    {.vtable = 0x3836010, .slot = 0x98, .check = 0x5586},
    {.vtable = 0x3836010, .slot = 0xa0, .check = 0xdc24},
    {.vtable = 0x3836010, .slot = 0xa8, .check = 0x7a69},
    {.vtable = 0x3836010, .slot = 0xb0, .check = 0xe3be},
    {.vtable = 0x3836010, .slot = 0x1b0, .check = 0x23fc},
    {.vtable = 0x3836010, .slot = 0x1b8, .check = 0x4b36},
    {.vtable = 0x3836010, .slot = 0x1f8, .check = 0x11e4},
    {.vtable = 0x3836010, .slot = 0x200, .check = 0x508e},
    {.vtable = 0x3836010, .slot = 0x208, .check = 0x5ec4},
    {.vtable = 0x3836010, .slot = 0x210, .check = 0x8be5},
    {.vtable = 0x3836010, .slot = 0x218, .check = 0x0b58},
    {.vtable = 0x3836010, .slot = 0x228, .check = 0x8be9},
    {.vtable = 0x3836010, .slot = 0x238, .check = 0xba58},
    {.vtable = 0x3836010, .slot = 0x240, .check = 0x8a19},
    {.vtable = 0x3836010, .slot = 0x2b0, .check = 0xd12f},
    {.vtable = 0x3836010, .slot = 0x360, .check = 0xba3f},
    {.vtable = 0x3836010, .slot = 0x368, .check = 0xc33f},
    {.vtable = 0x3836740, .slot = 0x38, .check = 0xa34c},
    {.vtable = 0x3836740, .slot = 0x68, .check = 0xb61b},
    {.vtable = 0x3836740, .slot = 0x88, .check = 0xf085},
    {.vtable = 0x3836740, .slot = 0x98, .check = 0xb64b},
    {.vtable = 0x3836740, .slot = 0xa0, .check = 0x27c5},
    {.vtable = 0x3836740, .slot = 0xa8, .check = 0xd4f8},
    {.vtable = 0x3836740, .slot = 0xb0, .check = 0x683f},
    {.vtable = 0x3836740, .slot = 0x1b0, .check = 0xb725},
    {.vtable = 0x3836740, .slot = 0x1b8, .check = 0x446f},
    {.vtable = 0x3836740, .slot = 0x1f8, .check = 0xec65},
    {.vtable = 0x3836740, .slot = 0x200, .check = 0xf45f},
    {.vtable = 0x3836740, .slot = 0x208, .check = 0x6765},
    {.vtable = 0x3836740, .slot = 0x210, .check = 0xe968},
    {.vtable = 0x3836740, .slot = 0x218, .check = 0x9759},
    {.vtable = 0x3836740, .slot = 0x228, .check = 0xb98c},
    {.vtable = 0x3836740, .slot = 0x238, .check = 0xae7d},
    {.vtable = 0x3836740, .slot = 0x240, .check = 0x0580},
    {.vtable = 0x3836740, .slot = 0x2b0, .check = 0x3a06},
    {.vtable = 0x3836740, .slot = 0x360, .check = 0x437a},
    {.vtable = 0x3836740, .slot = 0x368, .check = 0xb21e},
    {.vtable = 0x3836e70, .slot = 0x38, .check = 0x91f5},
    {.vtable = 0x3836e70, .slot = 0x68, .check = 0x8512},
    {.vtable = 0x3836e70, .slot = 0x88, .check = 0x1c18},
    {.vtable = 0x3836e70, .slot = 0x98, .check = 0xfd22},
    {.vtable = 0x3836e70, .slot = 0xa0, .check = 0x8138},
    {.vtable = 0x3836e70, .slot = 0xa8, .check = 0xd9c5},
    {.vtable = 0x3836e70, .slot = 0xb0, .check = 0xf222},
    {.vtable = 0x3836e70, .slot = 0x1b0, .check = 0x2140},
    {.vtable = 0x3836e70, .slot = 0x1b8, .check = 0x84fa},
    {.vtable = 0x3836e70, .slot = 0x1f8, .check = 0x12a8},
    {.vtable = 0x3836e70, .slot = 0x200, .check = 0xb1f2},
    {.vtable = 0x3836e70, .slot = 0x208, .check = 0x38d8},
    {.vtable = 0x3836e70, .slot = 0x210, .check = 0x8e79},
    {.vtable = 0x3836e70, .slot = 0x218, .check = 0x908c},
    {.vtable = 0x3836e70, .slot = 0x228, .check = 0x97bd},
    {.vtable = 0x3836e70, .slot = 0x238, .check = 0x5cdc},
    {.vtable = 0x3836e70, .slot = 0x240, .check = 0xe3bd},
    {.vtable = 0x3836e70, .slot = 0x2b0, .check = 0xecfb},
    {.vtable = 0x3836e70, .slot = 0x360, .check = 0xedeb},
    {.vtable = 0x3836e70, .slot = 0x368, .check = 0x0eeb},
    {.vtable = 0x38375a0, .slot = 0x40, .check = 0x0eb6},
    {.vtable = 0x38375a0, .slot = 0x58, .check = 0x29f7},
    {.vtable = 0x3837670, .slot = 0x40, .check = 0x69aa},
    {.vtable = 0x3837670, .slot = 0x58, .check = 0x39b3},
    {.vtable = 0x3837740, .slot = 0x40, .check = 0x44be},
    {.vtable = 0x3837740, .slot = 0x58, .check = 0x94bf},
    {.vtable = 0x3837930, .slot = 0x10, .check = 0x999c},
    {.vtable = 0x38379d0, .slot = 0x38, .check = 0x7c9b},
    {.vtable = 0x38379d0, .slot = 0x68, .check = 0x0ed0},
    {.vtable = 0x38379d0, .slot = 0x88, .check = 0xb7ba},
    {.vtable = 0x38379d0, .slot = 0x98, .check = 0x8420},
    {.vtable = 0x38379d0, .slot = 0xa0, .check = 0x6f1a},
    {.vtable = 0x38379d0, .slot = 0xa8, .check = 0x2443},
    {.vtable = 0x38379d0, .slot = 0xb0, .check = 0xaf78},
    {.vtable = 0x38379d0, .slot = 0x1b0, .check = 0x8182},
    {.vtable = 0x38379d0, .slot = 0x1b8, .check = 0x5850},
    {.vtable = 0x38379d0, .slot = 0x1f8, .check = 0xd18a},
    {.vtable = 0x38379d0, .slot = 0x200, .check = 0xd8e8},
    {.vtable = 0x38379d0, .slot = 0x208, .check = 0xa722},
    {.vtable = 0x38379d0, .slot = 0x210, .check = 0x0197},
    {.vtable = 0x38379d0, .slot = 0x218, .check = 0xb4d6},
    {.vtable = 0x38379d0, .slot = 0x228, .check = 0x387b},
    {.vtable = 0x38379d0, .slot = 0x238, .check = 0x001e},
    {.vtable = 0x38379d0, .slot = 0x240, .check = 0x3513},
    {.vtable = 0x38379d0, .slot = 0x2b0, .check = 0x3c21},
    {.vtable = 0x38379d0, .slot = 0x360, .check = 0x9371},
    {.vtable = 0x38379d0, .slot = 0x368, .check = 0xc8b1},
    {.vtable = 0x38381c0, .slot = 0x38, .check = 0xe6d6},
    {.vtable = 0x38381c0, .slot = 0x68, .check = 0xf05d},
    {.vtable = 0x38381c0, .slot = 0x88, .check = 0x77e3},
    {.vtable = 0x38381c0, .slot = 0x98, .check = 0x5bcd},
    {.vtable = 0x38381c0, .slot = 0xa0, .check = 0x42e3},
    {.vtable = 0x38381c0, .slot = 0xa8, .check = 0x71f1},
    {.vtable = 0x38381c0, .slot = 0xb0, .check = 0x6926},
    {.vtable = 0x38381c0, .slot = 0x1b0, .check = 0xdf64},
    {.vtable = 0x38381c0, .slot = 0x1b8, .check = 0x6f5e},
    {.vtable = 0x38381c0, .slot = 0x1f8, .check = 0x8cec},
    {.vtable = 0x38381c0, .slot = 0x200, .check = 0xd4d6},
    {.vtable = 0x38381c0, .slot = 0x208, .check = 0xac24},
    {.vtable = 0x38381c0, .slot = 0x210, .check = 0xd385},
    {.vtable = 0x38381c0, .slot = 0x218, .check = 0x9f38},
    {.vtable = 0x38381c0, .slot = 0x228, .check = 0x4549},
    {.vtable = 0x38381c0, .slot = 0x238, .check = 0xce77},
    {.vtable = 0x38381c0, .slot = 0x240, .check = 0x92c6},
    {.vtable = 0x38381c0, .slot = 0x2b0, .check = 0x4088},
    {.vtable = 0x38381c0, .slot = 0x360, .check = 0x8afc},
    {.vtable = 0x38381c0, .slot = 0x368, .check = 0x4440},
    {.vtable = 0x3838910, .slot = 0x38, .check = 0x02f7},
    {.vtable = 0x3838910, .slot = 0x68, .check = 0x91b4},
    {.vtable = 0x3838910, .slot = 0x88, .check = 0xb7b6},
    {.vtable = 0x3838910, .slot = 0x98, .check = 0x4c14},
    {.vtable = 0x3838910, .slot = 0xa0, .check = 0x29c6},
    {.vtable = 0x3838910, .slot = 0xa8, .check = 0x8e37},
    {.vtable = 0x3838910, .slot = 0xb0, .check = 0x9db4},
    {.vtable = 0x3838910, .slot = 0x1b0, .check = 0xd71e},
    {.vtable = 0x3838910, .slot = 0x1b8, .check = 0xbdac},
    {.vtable = 0x3838910, .slot = 0x1f8, .check = 0xc8e6},
    {.vtable = 0x3838910, .slot = 0x200, .check = 0x6a44},
    {.vtable = 0x3838910, .slot = 0x208, .check = 0x6ade},
    {.vtable = 0x3838910, .slot = 0x210, .check = 0x39b3},
    {.vtable = 0x3838910, .slot = 0x218, .check = 0xce12},
    {.vtable = 0x3838910, .slot = 0x228, .check = 0x0897},
    {.vtable = 0x3838910, .slot = 0x238, .check = 0x8572},
    {.vtable = 0x3838910, .slot = 0x240, .check = 0x3227},
    {.vtable = 0x3838910, .slot = 0x2b0, .check = 0x0795},
    {.vtable = 0x3838910, .slot = 0x360, .check = 0xbe05},
    {.vtable = 0x3838910, .slot = 0x368, .check = 0x6585},
    {.vtable = 0x3839000, .slot = 0x38, .check = 0x8ef2},
    {.vtable = 0x3839000, .slot = 0x68, .check = 0xd381},
    {.vtable = 0x3839000, .slot = 0x88, .check = 0x308f},
    {.vtable = 0x3839000, .slot = 0x98, .check = 0x51b1},
    {.vtable = 0x3839000, .slot = 0xa0, .check = 0xa82f},
    {.vtable = 0x3839000, .slot = 0xa8, .check = 0x7d4e},
    {.vtable = 0x3839000, .slot = 0xb0, .check = 0xdfb5},
    {.vtable = 0x3839000, .slot = 0x1b0, .check = 0x7cef},
    {.vtable = 0x3839000, .slot = 0x1b8, .check = 0x2465},
    {.vtable = 0x3839000, .slot = 0x1f8, .check = 0x2aaf},
    {.vtable = 0x3839000, .slot = 0x200, .check = 0x95d5},
    {.vtable = 0x3839000, .slot = 0x208, .check = 0x62bf},
    {.vtable = 0x3839000, .slot = 0x210, .check = 0x65ce},
    {.vtable = 0x3839000, .slot = 0x218, .check = 0x94b3},
    {.vtable = 0x3839000, .slot = 0x228, .check = 0x40b2},
    {.vtable = 0x3839000, .slot = 0x238, .check = 0xe173},
    {.vtable = 0x3839000, .slot = 0x240, .check = 0x8aa2},
    {.vtable = 0x3839000, .slot = 0x2b0, .check = 0xdcb4},
    {.vtable = 0x3839000, .slot = 0x360, .check = 0xf5a8},
    {.vtable = 0x3839000, .slot = 0x368, .check = 0x70cc},
    {.vtable = 0x38396f0, .slot = 0x38, .check = 0x31eb},
    {.vtable = 0x38396f0, .slot = 0x68, .check = 0xe420},
    {.vtable = 0x38396f0, .slot = 0x88, .check = 0x944a},
    {.vtable = 0x38396f0, .slot = 0x98, .check = 0x7b90},
    {.vtable = 0x38396f0, .slot = 0xa0, .check = 0x67aa},
    {.vtable = 0x38396f0, .slot = 0xa8, .check = 0x48d3},
    {.vtable = 0x38396f0, .slot = 0xb0, .check = 0xd588},
    {.vtable = 0x38396f0, .slot = 0x1b0, .check = 0xe572},
    {.vtable = 0x38396f0, .slot = 0x1b8, .check = 0x6200},
    {.vtable = 0x38396f0, .slot = 0x1f8, .check = 0xf4ba},
    {.vtable = 0x38396f0, .slot = 0x200, .check = 0x1378},
    {.vtable = 0x38396f0, .slot = 0x208, .check = 0xe532},
    {.vtable = 0x38396f0, .slot = 0x210, .check = 0xda87},
    {.vtable = 0x38396f0, .slot = 0x218, .check = 0x2f46},
    {.vtable = 0x38396f0, .slot = 0x228, .check = 0x4b6b},
    {.vtable = 0x38396f0, .slot = 0x238, .check = 0xcb0e},
    {.vtable = 0x38396f0, .slot = 0x240, .check = 0x17a3},
    {.vtable = 0x38396f0, .slot = 0x2b0, .check = 0xddd1},
    {.vtable = 0x38396f0, .slot = 0x360, .check = 0xd4c1},
    {.vtable = 0x38396f0, .slot = 0x368, .check = 0xabc1},
    {.vtable = 0x3839de0, .slot = 0x38, .check = 0xa986},
    {.vtable = 0x3839de0, .slot = 0x68, .check = 0x930d},
    {.vtable = 0x3839de0, .slot = 0x88, .check = 0x2213},
    {.vtable = 0x3839de0, .slot = 0x98, .check = 0xfb1d},
    {.vtable = 0x3839de0, .slot = 0xa0, .check = 0x9a93},
    {.vtable = 0x3839de0, .slot = 0xa8, .check = 0x87aa},
    {.vtable = 0x3839de0, .slot = 0xb0, .check = 0x5619},
    {.vtable = 0x3839de0, .slot = 0x1b0, .check = 0x4fb3},
    {.vtable = 0x3839de0, .slot = 0x1b8, .check = 0x1ae9},
    {.vtable = 0x3839de0, .slot = 0x1f8, .check = 0x7053},
    {.vtable = 0x3839de0, .slot = 0x200, .check = 0xcb19},
    {.vtable = 0x3839de0, .slot = 0x208, .check = 0xe2a3},
    {.vtable = 0x3839de0, .slot = 0x210, .check = 0x9712},
    {.vtable = 0x3839de0, .slot = 0x218, .check = 0x3bb7},
    {.vtable = 0x3839de0, .slot = 0x228, .check = 0x2ed6},
    {.vtable = 0x3839de0, .slot = 0x238, .check = 0x1c5f},
    {.vtable = 0x3839de0, .slot = 0x240, .check = 0x4f4e},
    {.vtable = 0x3839de0, .slot = 0x2b0, .check = 0x5e00},
    {.vtable = 0x3839de0, .slot = 0x360, .check = 0x80d4},
    {.vtable = 0x3839de0, .slot = 0x368, .check = 0x6218},
    {.vtable = 0x383a4d0, .slot = 0x38, .check = 0x48cf},
    {.vtable = 0x383a4d0, .slot = 0x68, .check = 0x8eac},
    {.vtable = 0x383a4d0, .slot = 0x88, .check = 0x345e},
    {.vtable = 0x383a4d0, .slot = 0x98, .check = 0x128c},
    {.vtable = 0x383a4d0, .slot = 0xa0, .check = 0x080e},
    {.vtable = 0x383a4d0, .slot = 0xa8, .check = 0x2dbf},
    {.vtable = 0x383a4d0, .slot = 0xb0, .check = 0x04dc},
    {.vtable = 0x383a4d0, .slot = 0x1b0, .check = 0xdec6},
    {.vtable = 0x383a4d0, .slot = 0x1b8, .check = 0xbe34},
    {.vtable = 0x383a4d0, .slot = 0x1f8, .check = 0xcfae},
    {.vtable = 0x383a4d0, .slot = 0x200, .check = 0xb5ac},
    {.vtable = 0x383a4d0, .slot = 0x208, .check = 0x9d86},
    {.vtable = 0x383a4d0, .slot = 0x210, .check = 0x545b},
    {.vtable = 0x383a4d0, .slot = 0x218, .check = 0xd0ba},
    {.vtable = 0x383a4d0, .slot = 0x228, .check = 0x9ddf},
    {.vtable = 0x383a4d0, .slot = 0x238, .check = 0x96ba},
    {.vtable = 0x383a4d0, .slot = 0x240, .check = 0x6a8f},
    {.vtable = 0x383a4d0, .slot = 0x2b0, .check = 0xeaed},
    {.vtable = 0x383a4d0, .slot = 0x360, .check = 0x62fd},
    {.vtable = 0x383a4d0, .slot = 0x368, .check = 0xdafd},
    {.vtable = 0x383abc0, .slot = 0x38, .check = 0xbd8a},
    {.vtable = 0x383abc0, .slot = 0x68, .check = 0x10b9},
    {.vtable = 0x383abc0, .slot = 0x88, .check = 0xaa17},
    {.vtable = 0x383abc0, .slot = 0x98, .check = 0xa4c9},
    {.vtable = 0x383abc0, .slot = 0xa0, .check = 0x8a37},
    {.vtable = 0x383abc0, .slot = 0xa8, .check = 0xef16},
    {.vtable = 0x383abc0, .slot = 0xb0, .check = 0x195d},
    {.vtable = 0x383abc0, .slot = 0x1b0, .check = 0x6e17},
    {.vtable = 0x383abc0, .slot = 0x1b8, .check = 0x282d},
    {.vtable = 0x383abc0, .slot = 0x1f8, .check = 0xc837},
    {.vtable = 0x383abc0, .slot = 0x200, .check = 0x401d},
    {.vtable = 0x383abc0, .slot = 0x208, .check = 0x72c7},
    {.vtable = 0x383abc0, .slot = 0x210, .check = 0x5816},
    {.vtable = 0x383abc0, .slot = 0x218, .check = 0x5edb},
    {.vtable = 0x383abc0, .slot = 0x228, .check = 0x1a1a},
    {.vtable = 0x383abc0, .slot = 0x238, .check = 0x135b},
    {.vtable = 0x383abc0, .slot = 0x240, .check = 0x94ca},
    {.vtable = 0x383abc0, .slot = 0x2b0, .check = 0x41cc},
    {.vtable = 0x383abc0, .slot = 0x360, .check = 0x2560},
    {.vtable = 0x383abc0, .slot = 0x368, .check = 0xb6e4},
    {.vtable = 0x383b2b0, .slot = 0x38, .check = 0x9fc3},
    {.vtable = 0x383b2b0, .slot = 0x68, .check = 0x8c38},
    {.vtable = 0x383b2b0, .slot = 0x88, .check = 0x8eb2},
    {.vtable = 0x383b2b0, .slot = 0x98, .check = 0x1ac8},
    {.vtable = 0x383b2b0, .slot = 0xa0, .check = 0x26d2},
    {.vtable = 0x383b2b0, .slot = 0xa8, .check = 0x593b},
    {.vtable = 0x383b2b0, .slot = 0xb0, .check = 0x9eb0},
    {.vtable = 0x383b2b0, .slot = 0x1b0, .check = 0x9f9a},
    {.vtable = 0x383b2b0, .slot = 0x1b8, .check = 0xc188},
    {.vtable = 0x383b2b0, .slot = 0x1f8, .check = 0xc262},
    {.vtable = 0x383b2b0, .slot = 0x200, .check = 0x39a0},
    {.vtable = 0x383b2b0, .slot = 0x208, .check = 0x9b3a},
    {.vtable = 0x383b2b0, .slot = 0x210, .check = 0xb6af},
    {.vtable = 0x383b2b0, .slot = 0x218, .check = 0x39ce},
    {.vtable = 0x383b2b0, .slot = 0x228, .check = 0x9e93},
    {.vtable = 0x383b2b0, .slot = 0x238, .check = 0xf845},
    {.vtable = 0x383b2b0, .slot = 0x240, .check = 0xc128},
    {.vtable = 0x383b2b0, .slot = 0x2b0, .check = 0xa9e6},
    {.vtable = 0x383b2b0, .slot = 0x360, .check = 0x6f1a},
    {.vtable = 0x383b2b0, .slot = 0x368, .check = 0xcefe},
    {.vtable = 0x383ba20, .slot = 0x38, .check = 0x9ad5},
    {.vtable = 0x383ba20, .slot = 0x68, .check = 0x63f2},
    {.vtable = 0x383ba20, .slot = 0x88, .check = 0x6ff8},
    {.vtable = 0x383ba20, .slot = 0x98, .check = 0xe9c2},
    {.vtable = 0x383ba20, .slot = 0xa0, .check = 0xedd8},
    {.vtable = 0x383ba20, .slot = 0xa8, .check = 0xfbe5},
    {.vtable = 0x383ba20, .slot = 0xb0, .check = 0x9e42},
    {.vtable = 0x383ba20, .slot = 0x1b0, .check = 0x8520},
    {.vtable = 0x383ba20, .slot = 0x1b8, .check = 0x561a},
    {.vtable = 0x383ba20, .slot = 0x1f8, .check = 0x5888},
    {.vtable = 0x383ba20, .slot = 0x200, .check = 0x1452},
    {.vtable = 0x383ba20, .slot = 0x208, .check = 0x8180},
    {.vtable = 0x383ba20, .slot = 0x210, .check = 0x7f21},
    {.vtable = 0x383ba20, .slot = 0x218, .check = 0xdf14},
    {.vtable = 0x383ba20, .slot = 0x228, .check = 0x6685},
    {.vtable = 0x383ba20, .slot = 0x238, .check = 0xe040},
    {.vtable = 0x383ba20, .slot = 0x240, .check = 0xf601},
    {.vtable = 0x383ba20, .slot = 0x2b0, .check = 0x15c3},
    {.vtable = 0x383ba20, .slot = 0x360, .check = 0x5653},
    {.vtable = 0x383ba20, .slot = 0x368, .check = 0xc7d3},
    {.vtable = 0x383c4e0, .slot = 0x38, .check = 0xb278},
    {.vtable = 0x383c4e0, .slot = 0x68, .check = 0x07ff},
    {.vtable = 0x383c4e0, .slot = 0x88, .check = 0x1af1},
    {.vtable = 0x383c4e0, .slot = 0x98, .check = 0x48cf},
    {.vtable = 0x383c4e0, .slot = 0xa0, .check = 0x2151},
    {.vtable = 0x383c4e0, .slot = 0xa8, .check = 0xf1cc},
    {.vtable = 0x383c4e0, .slot = 0xb0, .check = 0x9d5b},
    {.vtable = 0x383c4e0, .slot = 0x1b0, .check = 0x0d81},
    {.vtable = 0x383c4e0, .slot = 0x1b8, .check = 0xfe8b},
    {.vtable = 0x383c4e0, .slot = 0x1f8, .check = 0xa0c1},
    {.vtable = 0x383c4e0, .slot = 0x200, .check = 0x173b},
    {.vtable = 0x383c4e0, .slot = 0x208, .check = 0xa1d1},
    {.vtable = 0x383c4e0, .slot = 0x210, .check = 0x74f4},
    {.vtable = 0x383c4e0, .slot = 0x218, .check = 0xe345},
    {.vtable = 0x383c4e0, .slot = 0x228, .check = 0x52d8},
    {.vtable = 0x383c4e0, .slot = 0x238, .check = 0x68c9},
    {.vtable = 0x383c4e0, .slot = 0x240, .check = 0x83cc},
    {.vtable = 0x383c4e0, .slot = 0x2b0, .check = 0x3d3a},
    {.vtable = 0x383c4e0, .slot = 0x360, .check = 0x5b4e},
    {.vtable = 0x383c4e0, .slot = 0x368, .check = 0xfcd2},
    {.vtable = 0x3846470, .slot = 0x10, .check = 0x56a5},
    {.vtable = 0x38464d0, .slot = 0x10, .check = 0x09e6},
    {.vtable = 0x3846530, .slot = 0x10, .check = 0xe269},
    {.vtable = 0x38488a0, .slot = 0x40, .check = 0x7ac8},
    {.vtable = 0x38488a0, .slot = 0x58, .check = 0x8715},
    {.vtable = 0x3848d90, .slot = 0x38, .check = 0x61e2},
    {.vtable = 0x3848d90, .slot = 0x68, .check = 0x87f1},
    {.vtable = 0x3848d90, .slot = 0x88, .check = 0x077f},
    {.vtable = 0x3848d90, .slot = 0x98, .check = 0x9ec1},
    {.vtable = 0x3848d90, .slot = 0xa0, .check = 0xa3df},
    {.vtable = 0x3848d90, .slot = 0xa8, .check = 0xe6fe},
    {.vtable = 0x3848d90, .slot = 0xb0, .check = 0x8e65},
    {.vtable = 0x3848d90, .slot = 0x1b0, .check = 0x7bbf},
    {.vtable = 0x3848d90, .slot = 0x1b8, .check = 0xcd35},
    {.vtable = 0x3848d90, .slot = 0x1f8, .check = 0xbd7f},
    {.vtable = 0x3848d90, .slot = 0x200, .check = 0xcf85},
    {.vtable = 0x3848d90, .slot = 0x208, .check = 0x5c6f},
    {.vtable = 0x3848d90, .slot = 0x210, .check = 0x201e},
    {.vtable = 0x3848d90, .slot = 0x218, .check = 0xb203},
    {.vtable = 0x3848d90, .slot = 0x228, .check = 0x9f02},
    {.vtable = 0x3848d90, .slot = 0x238, .check = 0x60dc},
    {.vtable = 0x3848d90, .slot = 0x240, .check = 0x67bd},
    {.vtable = 0x3848d90, .slot = 0x2b0, .check = 0x478f},
    {.vtable = 0x3848d90, .slot = 0x360, .check = 0x29df},
    {.vtable = 0x3848d90, .slot = 0x368, .check = 0x251f},
    {.vtable = 0x3849470, .slot = 0x38, .check = 0xfb2c},
    {.vtable = 0x3849470, .slot = 0x68, .check = 0xc3fb},
    {.vtable = 0x3849470, .slot = 0x88, .check = 0xe5e5},
    {.vtable = 0x3849470, .slot = 0x98, .check = 0x02eb},
    {.vtable = 0x3849470, .slot = 0xa0, .check = 0xf465},
    {.vtable = 0x3849470, .slot = 0xa8, .check = 0x9c98},
    {.vtable = 0x3849470, .slot = 0xb0, .check = 0x7a5f},
    {.vtable = 0x3849470, .slot = 0x1b0, .check = 0x1c05},
    {.vtable = 0x3849470, .slot = 0x1b8, .check = 0x5a8f},
    {.vtable = 0x3849470, .slot = 0x1f8, .check = 0x3345},
    {.vtable = 0x3849470, .slot = 0x200, .check = 0xd13f},
    {.vtable = 0x3849470, .slot = 0x208, .check = 0x2d55},
    {.vtable = 0x3849470, .slot = 0x210, .check = 0x26f8},
    {.vtable = 0x3849470, .slot = 0x218, .check = 0x1769},
    {.vtable = 0x3849470, .slot = 0x228, .check = 0xcc1c},
    {.vtable = 0x3849470, .slot = 0x238, .check = 0xd23a},
    {.vtable = 0x3849470, .slot = 0x240, .check = 0x160f},
    {.vtable = 0x3849470, .slot = 0x2b0, .check = 0x2b6d},
    {.vtable = 0x3849470, .slot = 0x360, .check = 0xd47d},
    {.vtable = 0x3849470, .slot = 0x368, .check = 0x9d7d},
    {.vtable = 0x3849b50, .slot = 0x38, .check = 0xd00a},
    {.vtable = 0x3849b50, .slot = 0x68, .check = 0x7339},
    {.vtable = 0x3849b50, .slot = 0x88, .check = 0x5c97},
    {.vtable = 0x3849b50, .slot = 0x98, .check = 0x8649},
    {.vtable = 0x3849b50, .slot = 0xa0, .check = 0xbcb7},
    {.vtable = 0x3849b50, .slot = 0xa8, .check = 0x2b96},
    {.vtable = 0x3849b50, .slot = 0xb0, .check = 0xc4dd},
    {.vtable = 0x3849b50, .slot = 0x1b0, .check = 0x8997},
    {.vtable = 0x3849b50, .slot = 0x1b8, .check = 0xb3ad},
    {.vtable = 0x3849b50, .slot = 0x1f8, .check = 0xa2b7},
    {.vtable = 0x3849b50, .slot = 0x200, .check = 0x499d},
    {.vtable = 0x3849b50, .slot = 0x208, .check = 0x8b47},
    {.vtable = 0x3849b50, .slot = 0x210, .check = 0x8096},
    {.vtable = 0x3849b50, .slot = 0x218, .check = 0x975b},
    {.vtable = 0x3849b50, .slot = 0x228, .check = 0x629a},
    {.vtable = 0x3849b50, .slot = 0x238, .check = 0xb644},
    {.vtable = 0x3849b50, .slot = 0x240, .check = 0x1d85},
    {.vtable = 0x3849b50, .slot = 0x2b0, .check = 0xa287},
    {.vtable = 0x3849b50, .slot = 0x360, .check = 0xec37},
    {.vtable = 0x3849b50, .slot = 0x368, .check = 0x3c97},
    {.vtable = 0x384a230, .slot = 0x38, .check = 0x9c84},
    {.vtable = 0x384a230, .slot = 0x68, .check = 0xd693},
    {.vtable = 0x384a230, .slot = 0x88, .check = 0xe28d},
    {.vtable = 0x384a230, .slot = 0x98, .check = 0x5fc3},
    {.vtable = 0x384a230, .slot = 0xa0, .check = 0x43cd},
    {.vtable = 0x384a230, .slot = 0xa8, .check = 0x2e40},
    {.vtable = 0x384a230, .slot = 0xb0, .check = 0xaae7},
    {.vtable = 0x384a230, .slot = 0x1b0, .check = 0xd14d},
    {.vtable = 0x384a230, .slot = 0x1b8, .check = 0x2617},
    {.vtable = 0x384a230, .slot = 0x1f8, .check = 0xf0ad},
    {.vtable = 0x384a230, .slot = 0x200, .check = 0xb5e7},
    {.vtable = 0x384a230, .slot = 0x208, .check = 0xbe5d},
    {.vtable = 0x384a230, .slot = 0x210, .check = 0x27c0},
    {.vtable = 0x384a230, .slot = 0x218, .check = 0xf751},
    {.vtable = 0x384a230, .slot = 0x228, .check = 0xe7e4},
    {.vtable = 0x384a230, .slot = 0x238, .check = 0x5572},
    {.vtable = 0x384a230, .slot = 0x240, .check = 0x0227},
    {.vtable = 0x384a230, .slot = 0x2b0, .check = 0xd795},
    {.vtable = 0x384a230, .slot = 0x360, .check = 0x8e05},
    {.vtable = 0x384a230, .slot = 0x368, .check = 0x3585},
    {.vtable = 0x384a910, .slot = 0x38, .check = 0x5ef2},
    {.vtable = 0x384a910, .slot = 0x68, .check = 0xa381},
    {.vtable = 0x384a910, .slot = 0x88, .check = 0x008f},
    {.vtable = 0x384a910, .slot = 0x98, .check = 0x21b1},
    {.vtable = 0x384a910, .slot = 0xa0, .check = 0x782f},
    {.vtable = 0x384a910, .slot = 0xa8, .check = 0x4d4e},
    {.vtable = 0x384a910, .slot = 0xb0, .check = 0xafb5},
    {.vtable = 0x384a910, .slot = 0x1b0, .check = 0x4cef},
    {.vtable = 0x384a910, .slot = 0x1b8, .check = 0xf465},
    {.vtable = 0x384a910, .slot = 0x1f8, .check = 0xfaaf},
    {.vtable = 0x384a910, .slot = 0x200, .check = 0x65d5},
    {.vtable = 0x384a910, .slot = 0x208, .check = 0x32bf},
    {.vtable = 0x384a910, .slot = 0x210, .check = 0x35ce},
    {.vtable = 0x384a910, .slot = 0x218, .check = 0x64b3},
    {.vtable = 0x384a910, .slot = 0x228, .check = 0x10b2},
    {.vtable = 0x384a910, .slot = 0x238, .check = 0x932c},
    {.vtable = 0x384a910, .slot = 0x240, .check = 0x32ad},
    {.vtable = 0x384a910, .slot = 0x2b0, .check = 0x729f},
    {.vtable = 0x384a910, .slot = 0x360, .check = 0xfd4f},
    {.vtable = 0x384a910, .slot = 0x368, .check = 0xf70f},
    {.vtable = 0x384aff0, .slot = 0x38, .check = 0xc4bc},
    {.vtable = 0x384aff0, .slot = 0x68, .check = 0x500b},
    {.vtable = 0x384aff0, .slot = 0x88, .check = 0x9875},
    {.vtable = 0x384aff0, .slot = 0x98, .check = 0xf45b},
    {.vtable = 0x384aff0, .slot = 0xa0, .check = 0xceb5},
    {.vtable = 0x384aff0, .slot = 0xa8, .check = 0xc5e8},
    {.vtable = 0x384aff0, .slot = 0xb0, .check = 0xc1af},
    {.vtable = 0x384aff0, .slot = 0x1b0, .check = 0xd335},
    {.vtable = 0x384aff0, .slot = 0x1b8, .check = 0x72ff},
    {.vtable = 0x384aff0, .slot = 0x1f8, .check = 0x8175},
    {.vtable = 0x384aff0, .slot = 0x200, .check = 0x71cf},
    {.vtable = 0x384aff0, .slot = 0x208, .check = 0xa8e5},
    {.vtable = 0x384aff0, .slot = 0x210, .check = 0x5ae8},
    {.vtable = 0x384aff0, .slot = 0x218, .check = 0x38d9},
    {.vtable = 0x384aff0, .slot = 0x228, .check = 0x8b0c},
    {.vtable = 0x384aff0, .slot = 0x238, .check = 0x03ca},
    {.vtable = 0x384aff0, .slot = 0x240, .check = 0xe6bf},
    {.vtable = 0x384aff0, .slot = 0x2b0, .check = 0x30fd},
    {.vtable = 0x384aff0, .slot = 0x360, .check = 0xd66d},
    {.vtable = 0x384aff0, .slot = 0x368, .check = 0x4ced},
    {.vtable = 0x384ba30, .slot = 0x38, .check = 0x271a},
    {.vtable = 0x384ba30, .slot = 0x68, .check = 0x3cc9},
    {.vtable = 0x384ba30, .slot = 0x88, .check = 0x75e7},
    {.vtable = 0x384ba30, .slot = 0x98, .check = 0x8d39},
    {.vtable = 0x384ba30, .slot = 0xa0, .check = 0xd5c7},
    {.vtable = 0x384ba30, .slot = 0xa8, .check = 0x94e6},
    {.vtable = 0x384ba30, .slot = 0xb0, .check = 0xdead},
    {.vtable = 0x384ba30, .slot = 0x1b0, .check = 0xd0c7},
    {.vtable = 0x384ba30, .slot = 0x1b8, .check = 0x49dd},
    {.vtable = 0x384ba30, .slot = 0x1f8, .check = 0x30a7},
    {.vtable = 0x384ba30, .slot = 0x200, .check = 0x2a2d},
    {.vtable = 0x384ba30, .slot = 0x208, .check = 0x88d7},
    {.vtable = 0x384ba30, .slot = 0x210, .check = 0x9b86},
    {.vtable = 0x384ba30, .slot = 0x218, .check = 0x7fcb},
    {.vtable = 0x384ba30, .slot = 0x228, .check = 0xa38a},
    {.vtable = 0x384ba30, .slot = 0x238, .check = 0x9bef},
    {.vtable = 0x384ba30, .slot = 0x240, .check = 0xc07e},
    {.vtable = 0x384ba30, .slot = 0x2b0, .check = 0x3910},
    {.vtable = 0x384ba30, .slot = 0x360, .check = 0x96c4},
    {.vtable = 0x384ba30, .slot = 0x368, .check = 0x1308},
    {.vtable = 0x384f680, .slot = 0x20, .check = 0x6af1},
    {.vtable = 0x384f680, .slot = 0x30, .check = 0xd798},
    {.vtable = 0x384f680, .slot = 0x68, .check = 0xd6ab},
    {.vtable = 0x384f680, .slot = 0x78, .check = 0xd488},
    {.vtable = 0x384f750, .slot = 0x10, .check = 0xded7},
    {.vtable = 0x38529d0, .slot = 0x0, .check = 0x9c59},
    {.vtable = 0x3852b00, .slot = 0x38, .check = 0xeb1e},
    {.vtable = 0x3852b00, .slot = 0x68, .check = 0xb4c5},
    {.vtable = 0x3852b00, .slot = 0x88, .check = 0x905b},
    {.vtable = 0x3852b00, .slot = 0x98, .check = 0x34d5},
    {.vtable = 0x3852b00, .slot = 0xa0, .check = 0xe8be},
    {.vtable = 0x3852b00, .slot = 0xa8, .check = 0x6fb8},
    {.vtable = 0x3852b00, .slot = 0xb0, .check = 0x64ff},
    {.vtable = 0x3852b00, .slot = 0x1b0, .check = 0x0c65},
    {.vtable = 0x3852b00, .slot = 0x1b8, .check = 0x022f},
    {.vtable = 0x3852b00, .slot = 0x1f8, .check = 0xda1d},
    {.vtable = 0x3852b00, .slot = 0x200, .check = 0xadaf},
    {.vtable = 0x3852b00, .slot = 0x208, .check = 0xe645},
    {.vtable = 0x3852b00, .slot = 0x210, .check = 0x9288},
    {.vtable = 0x3852b00, .slot = 0x218, .check = 0x1639},
    {.vtable = 0x3852b00, .slot = 0x228, .check = 0xbdec},
    {.vtable = 0x3852b00, .slot = 0x238, .check = 0xd00e},
    {.vtable = 0x3852b00, .slot = 0x240, .check = 0xbca3},
    {.vtable = 0x3852b00, .slot = 0x2b0, .check = 0x22d1},
    {.vtable = 0x3852b00, .slot = 0x360, .check = 0xb9c1},
    {.vtable = 0x3852b00, .slot = 0x368, .check = 0x30c1},
    {.vtable = 0x3861b80, .slot = 0x1c8, .check = 0x74eb},
    {.vtable = 0x3870460, .slot = 0x1c8, .check = 0x88c1},
    {.vtable = 0x3870890, .slot = 0x1c8, .check = 0xcceb},
    {.vtable = 0x3870cc0, .slot = 0x1c8, .check = 0xe0c1},
    {.vtable = 0x38710f0, .slot = 0x1c8, .check = 0x24eb},
    {.vtable = 0x3871520, .slot = 0x1c8, .check = 0x38c1},
    {.vtable = 0x3871950, .slot = 0x1c8, .check = 0x7ceb},
    {.vtable = 0x3871d80, .slot = 0x1c8, .check = 0x90c1},
    {.vtable = 0x3872b10, .slot = 0x1c8, .check = 0xd4eb},
    {.vtable = 0x3872e40, .slot = 0x1c8, .check = 0xe8c1},
    {.vtable = 0x3873170, .slot = 0x1c8, .check = 0x2ceb},
    {.vtable = 0x38734a0, .slot = 0x1c8, .check = 0x40c1},
    {.vtable = 0x38737d0, .slot = 0x1c8, .check = 0x84eb},
    {.vtable = 0x3873b00, .slot = 0x1c8, .check = 0x98c1},
    {.vtable = 0x3873e30, .slot = 0x40, .check = 0x4820},
    {.vtable = 0x3873e30, .slot = 0x58, .check = 0x562d},
    {.vtable = 0x3873f70, .slot = 0x40, .check = 0x9334},
    {.vtable = 0x3873f70, .slot = 0x58, .check = 0x3289},
    {.vtable = 0x3874040, .slot = 0x40, .check = 0x98a8},
    {.vtable = 0x3874040, .slot = 0x58, .check = 0x0a75},
    {.vtable = 0x3874110, .slot = 0x70, .check = 0x89e4},
    {.vtable = 0x3874110, .slot = 0x88, .check = 0x8af4},
    {.vtable = 0x3874110, .slot = 0xd8, .check = 0x5304},
    {.vtable = 0x3874110, .slot = 0xe0, .check = 0xd5a4},
    {.vtable = 0x3874220, .slot = 0x70, .check = 0x3b71},
    {.vtable = 0x3874220, .slot = 0x88, .check = 0x229e},
    {.vtable = 0x3874220, .slot = 0xd8, .check = 0x7c46},
    {.vtable = 0x3874220, .slot = 0xe0, .check = 0x438e},
    {.vtable = 0x3874b00, .slot = 0x38, .check = 0xe605},
    {.vtable = 0x3874b00, .slot = 0x68, .check = 0xc6e2},
    {.vtable = 0x3874b00, .slot = 0x88, .check = 0xd428},
    {.vtable = 0x3874b00, .slot = 0x98, .check = 0x2f92},
    {.vtable = 0x3874b00, .slot = 0xa0, .check = 0xdbc8},
    {.vtable = 0x3874b00, .slot = 0xa8, .check = 0x8bd5},
    {.vtable = 0x3874b00, .slot = 0xb0, .check = 0x76b2},
    {.vtable = 0x3874b00, .slot = 0x1b0, .check = 0x10b0},
    {.vtable = 0x3874b00, .slot = 0x1b8, .check = 0xe42a},
    {.vtable = 0x3874b00, .slot = 0x1f8, .check = 0xc9d8},
    {.vtable = 0x3874b00, .slot = 0x200, .check = 0xdb82},
    {.vtable = 0x3874b00, .slot = 0x208, .check = 0x0530},
    {.vtable = 0x3874b00, .slot = 0x210, .check = 0xaebd},
    {.vtable = 0x3874b00, .slot = 0x218, .check = 0x29f0},
    {.vtable = 0x3874b00, .slot = 0x228, .check = 0x5e61},
    {.vtable = 0x3874b00, .slot = 0x238, .check = 0x0cb0},
    {.vtable = 0x3874b00, .slot = 0x240, .check = 0x7c51},
    {.vtable = 0x3874b00, .slot = 0x2b0, .check = 0xb04d},
    {.vtable = 0x3874b00, .slot = 0x360, .check = 0x9d1d},
    {.vtable = 0x3874b00, .slot = 0x368, .check = 0x2bdd},
    {.vtable = 0x38765c0, .slot = 0x38, .check = 0x76ea},
    {.vtable = 0x38765c0, .slot = 0x68, .check = 0x3599},
    {.vtable = 0x38765c0, .slot = 0x88, .check = 0xbc4b},
    {.vtable = 0x38765c0, .slot = 0x98, .check = 0xfee5},
    {.vtable = 0x38765c0, .slot = 0xa0, .check = 0x860b},
    {.vtable = 0x38765c0, .slot = 0xa8, .check = 0x21e2},
    {.vtable = 0x38765c0, .slot = 0xb0, .check = 0x35b1},
    {.vtable = 0x38765c0, .slot = 0x1b0, .check = 0xdc6b},
    {.vtable = 0x38765c0, .slot = 0x1b8, .check = 0x1b61},
    {.vtable = 0x38765c0, .slot = 0x1f8, .check = 0x702b},
    {.vtable = 0x38765c0, .slot = 0x200, .check = 0x1ad1},
    {.vtable = 0x38765c0, .slot = 0x208, .check = 0xd13b},
    {.vtable = 0x38765c0, .slot = 0x210, .check = 0x12ca},
    {.vtable = 0x38765c0, .slot = 0x218, .check = 0x668f},
    {.vtable = 0x38765c0, .slot = 0x228, .check = 0xe9ce},
    {.vtable = 0x38765c0, .slot = 0x238, .check = 0x431f},
    {.vtable = 0x38765c0, .slot = 0x240, .check = 0x8d8e},
    {.vtable = 0x38765c0, .slot = 0x2b0, .check = 0x1140},
    {.vtable = 0x38765c0, .slot = 0x360, .check = 0x9214},
    {.vtable = 0x38765c0, .slot = 0x368, .check = 0xb258},
    {.vtable = 0x387f2a0, .slot = 0x38, .check = 0x970f},
    {.vtable = 0x387f2a0, .slot = 0x68, .check = 0x7258},
    {.vtable = 0x387f2a0, .slot = 0x88, .check = 0x7ed2},
    {.vtable = 0x387f2a0, .slot = 0x98, .check = 0x7ba8},
    {.vtable = 0x387f2a0, .slot = 0xa0, .check = 0x5db2},
    {.vtable = 0x387f2a0, .slot = 0xa8, .check = 0x4e9b},
    {.vtable = 0x387f2a0, .slot = 0xb0, .check = 0xd910},
    {.vtable = 0x387f2a0, .slot = 0x1b0, .check = 0x3b3a},
    {.vtable = 0x387f2a0, .slot = 0x1b8, .check = 0x82e8},
    {.vtable = 0x387f2a0, .slot = 0x1f8, .check = 0xb482},
    {.vtable = 0x387f2a0, .slot = 0x200, .check = 0x2340},
    {.vtable = 0x387f2a0, .slot = 0x208, .check = 0x8d5a},
    {.vtable = 0x387f2a0, .slot = 0x210, .check = 0xfce3},
    {.vtable = 0x387f2a0, .slot = 0x218, .check = 0x70c2},
    {.vtable = 0x387f2a0, .slot = 0x228, .check = 0xe847},
    {.vtable = 0x387f2a0, .slot = 0x238, .check = 0x8a82},
    {.vtable = 0x387f2a0, .slot = 0x240, .check = 0x2e57},
    {.vtable = 0x387f2a0, .slot = 0x2b0, .check = 0xea9b},
    {.vtable = 0x387f2a0, .slot = 0x360, .check = 0xe3cb},
    {.vtable = 0x387f2a0, .slot = 0x368, .check = 0x0f0b},
    {.vtable = 0x387f9f0, .slot = 0x38, .check = 0xba10},
    {.vtable = 0x387f9f0, .slot = 0x68, .check = 0x0b57},
    {.vtable = 0x387f9f0, .slot = 0x88, .check = 0xe339},
    {.vtable = 0x387f9f0, .slot = 0x98, .check = 0x3427},
    {.vtable = 0x387f9f0, .slot = 0xa0, .check = 0x2b99},
    {.vtable = 0x387f9f0, .slot = 0xa8, .check = 0x6114},
    {.vtable = 0x387f9f0, .slot = 0xb0, .check = 0xf4e3},
    {.vtable = 0x387f9f0, .slot = 0x1b0, .check = 0x86a9},
    {.vtable = 0x387f9f0, .slot = 0x1b8, .check = 0x4613},
    {.vtable = 0x387f9f0, .slot = 0x1f8, .check = 0xce89},
    {.vtable = 0x387f9f0, .slot = 0x200, .check = 0x3523},
    {.vtable = 0x387f9f0, .slot = 0x208, .check = 0xe5f9},
    {.vtable = 0x387f9f0, .slot = 0x210, .check = 0x2174},
    {.vtable = 0x387f9f0, .slot = 0x218, .check = 0x1fc5},
    {.vtable = 0x387f9f0, .slot = 0x228, .check = 0x1f58},
    {.vtable = 0x387f9f0, .slot = 0x238, .check = 0xbc59},
    {.vtable = 0x387f9f0, .slot = 0x240, .check = 0xb4bc},
    {.vtable = 0x387f9f0, .slot = 0x2b0, .check = 0x2040},
    {.vtable = 0x387f9f0, .slot = 0x360, .check = 0x8114},
    {.vtable = 0x387f9f0, .slot = 0x368, .check = 0x8158},
    {.vtable = 0x3881640, .slot = 0x20, .check = 0xa878},
    {.vtable = 0x3881640, .slot = 0x30, .check = 0x3ec1},
    {.vtable = 0x3881640, .slot = 0x68, .check = 0x9838},
    {.vtable = 0x3881640, .slot = 0x78, .check = 0x47d1},
    {.vtable = 0x3881710, .slot = 0x10, .check = 0x22e2},
    {.vtable = 0x3886670, .slot = 0x38, .check = 0xf9c1},
    {.vtable = 0x3886670, .slot = 0x68, .check = 0x26b6},
    {.vtable = 0x3886670, .slot = 0x88, .check = 0x27c4},
    {.vtable = 0x3886670, .slot = 0x98, .check = 0xeaf6},
    {.vtable = 0x3886670, .slot = 0xa0, .check = 0xf1f4},
    {.vtable = 0x3886670, .slot = 0xa8, .check = 0x4779},
    {.vtable = 0x3886670, .slot = 0xb0, .check = 0x75ce},
    {.vtable = 0x3886670, .slot = 0x1b0, .check = 0x52ec},
    {.vtable = 0x3886670, .slot = 0x1b8, .check = 0x3526},
    {.vtable = 0x3886670, .slot = 0x1f8, .check = 0xbd54},
    {.vtable = 0x3886670, .slot = 0x200, .check = 0x1dde},
    {.vtable = 0x3886670, .slot = 0x208, .check = 0x0dcc},
    {.vtable = 0x3886670, .slot = 0x210, .check = 0xfd2d},
    {.vtable = 0x3886670, .slot = 0x218, .check = 0x90e0},
    {.vtable = 0x3886670, .slot = 0x228, .check = 0x8051},
    {.vtable = 0x3886670, .slot = 0x238, .check = 0x1c94},
    {.vtable = 0x3886670, .slot = 0x240, .check = 0x0eca},
    {.vtable = 0x3886670, .slot = 0x2b0, .check = 0xfbcc},
    {.vtable = 0x3886670, .slot = 0x360, .check = 0x74c2},
    {.vtable = 0x3886670, .slot = 0x368, .check = 0x38e8},
    {.vtable = 0x38873f0, .slot = 0x38, .check = 0x58e5},
    {.vtable = 0x38873f0, .slot = 0x98, .check = 0x1363},
    {.vtable = 0x38873f0, .slot = 0xa0, .check = 0x0a75},
    {.vtable = 0x38873f0, .slot = 0xb0, .check = 0x3052},
    {.vtable = 0x3887e80, .slot = 0x70, .check = 0xd223},
    {.vtable = 0x3887e80, .slot = 0x88, .check = 0x3847},
    {.vtable = 0x3887e80, .slot = 0xd8, .check = 0x9683},
    {.vtable = 0x3887e80, .slot = 0xe0, .check = 0xc2f7},
    {.vtable = 0x3887f90, .slot = 0x70, .check = 0x0222},
    {.vtable = 0x3887f90, .slot = 0x88, .check = 0xfca2},
    {.vtable = 0x3887f90, .slot = 0xd8, .check = 0x9902},
    {.vtable = 0x3887f90, .slot = 0xe0, .check = 0x9e12},
    {.vtable = 0x38884a0, .slot = 0x40, .check = 0x3ceb},
    {.vtable = 0x38884a0, .slot = 0x58, .check = 0x3fe6},
    {.vtable = 0x3888570, .slot = 0x40, .check = 0x6817},
    {.vtable = 0x3888570, .slot = 0x58, .check = 0x959a},
    {.vtable = 0x3888b20, .slot = 0x40, .check = 0xe5b3},
    {.vtable = 0x3888b20, .slot = 0x58, .check = 0xf64e},
    {.vtable = 0x3888c50, .slot = 0x40, .check = 0xa97f},
    {.vtable = 0x3888c50, .slot = 0x58, .check = 0x2902},
    {.vtable = 0x3888d30, .slot = 0x70, .check = 0x0513},
    {.vtable = 0x3888d30, .slot = 0x88, .check = 0x88d7},
    {.vtable = 0x3888d30, .slot = 0xd8, .check = 0xf1b3},
    {.vtable = 0x3888d30, .slot = 0xe0, .check = 0xbea7},
    {.vtable = 0x38892d0, .slot = 0x38, .check = 0xcdb4},
    {.vtable = 0x38892d0, .slot = 0x68, .check = 0x96c3},
    {.vtable = 0x38892d0, .slot = 0x88, .check = 0x393d},
    {.vtable = 0x38892d0, .slot = 0x98, .check = 0x9893},
    {.vtable = 0x38892d0, .slot = 0xa0, .check = 0x7cfd},
    {.vtable = 0x38892d0, .slot = 0xa8, .check = 0x95f0},
    {.vtable = 0x38892d0, .slot = 0xb0, .check = 0x5097},
    {.vtable = 0x38892d0, .slot = 0x1b0, .check = 0xc81d},
    {.vtable = 0x38892d0, .slot = 0x1b8, .check = 0x8de7},
    {.vtable = 0x38892d0, .slot = 0x1f8, .check = 0xdebd},
    {.vtable = 0x38892d0, .slot = 0x200, .check = 0xac57},
    {.vtable = 0x38892d0, .slot = 0x208, .check = 0x134d},
    {.vtable = 0x38892d0, .slot = 0x210, .check = 0xd450},
    {.vtable = 0x38892d0, .slot = 0x218, .check = 0xa7fe},
    {.vtable = 0x38892d0, .slot = 0x228, .check = 0x3bc3},
    {.vtable = 0x38892d0, .slot = 0x238, .check = 0xa47e},
    {.vtable = 0x38892d0, .slot = 0x240, .check = 0x90f3},
    {.vtable = 0x38892d0, .slot = 0x2b0, .check = 0xd201},
    {.vtable = 0x38892d0, .slot = 0x360, .check = 0xfa91},
    {.vtable = 0x38892d0, .slot = 0x368, .check = 0x1211},
    {.vtable = 0x3889ee0, .slot = 0x70, .check = 0x1e48},
    {.vtable = 0x3889ee0, .slot = 0x88, .check = 0x3390},
    {.vtable = 0x3889ee0, .slot = 0xd8, .check = 0xcf28},
    {.vtable = 0x3889ee0, .slot = 0xe0, .check = 0x8280},
    {.vtable = 0x3b91320, .slot = 0x0, .check = 0x8d9d},
    {.vtable = 0x3b91320, .slot = 0x8, .check = 0x0a24},
    {.vtable = 0x3b91320, .slot = 0x18, .check = 0xd289},
    {.vtable = 0x3b91320, .slot = 0x20, .check = 0x03b8},
});
static_assert(CONTACT_ENTRIES.size() == 1415);
inline constexpr uint64_t CONTACT_ENTRY_CHAIN = 0x43d70ccc80a103b8;

// Fibonacci hashing (Knuth, TAOCP 3, 6.4): the top eight bits of the product with 2^64 / phi.
static_assert(CLASS_SLOTS == 256);
[[nodiscard]] constexpr uint8_t ContactClassSlot(uintptr_t vptr) noexcept {
  return static_cast<uint8_t>((uint64_t{vptr} * 0x9e3779b97f4a7c15) >> 56);
}

// Why a tick, or every tick from one on, ran the game's loop; each has one log line, once.
enum ContactReason : uint8_t {
  CONTACT_REASON_THREADS,      // the game runs the step without tasks, or with fewer task threads than MIN_TASK_THREADS
  CONTACT_REASON_SHIPS,        // the tail has entries, or an object is of the NAVAL class
  CONTACT_REASON_POOL,         // more kept nodes than the node pool holds
  CONTACT_REASON_ID_SET,       // a walk would grow the game's id vector: that tick only, no change of the lane
  CONTACT_REASON_QUEUE_LOCK,   // the engine's task queue could not be locked
  CONTACT_REASON_CLASS,        // an object whose table is not in the class table
  CONTACT_REASON_NESTED,       // the pass was entered again while a tick ran
  CONTACT_REASON_TABLE,        // the game table a walk reads is not built: off until the game ends
  CONTACT_REASON_OTHER_WORLD,  // an object of another environment than the manager's
  CONTACT_REASON_COUNT
};
inline constexpr std::array<const char*, CONTACT_REASON_COUNT> CONTACT_REASON_WORDS = {
    "too few task threads", "ships", "list pool", "id set", "queue lock", "unknown class", "nested",
    "game table not loaded", "environment differs"};
// A region that ends with one of these set leaves its tick to the game's loop.
inline constexpr uint32_t STOP_OTHER_WORLD = 1u << 0;
inline constexpr uint32_t STOP_NAVAL = 1u << 1;
inline constexpr uint32_t STOP_CLASS = 1u << 2;
inline constexpr uint32_t STOP_POOL = 1u << 3;

// Detect's result for the object at the same place of the list.
struct ContactItem {
  void* object = nullptr;
  uint32_t first_node = 0;  // into contact_nodes; the chain runs through ContactNode::next
  uint32_t node_count = 0;
  uint32_t mark_count = 0;  // the ids the game's walk would push
  ContactMode mode = CONTACT_PLAIN;
};
// A participant's own: the id bits of the walk it runs, and the words it set.
struct ContactSlot {
  std::array<uint32_t, ID_SET_WORDS> marks = {};
  std::array<uint16_t, VISITED_LIMIT> visited = {};
};
// The cell entries an object has during its apply: the first leads to `head`, the others to `none`. Every other byte
// stays zero and nothing but `head`'s value is written after the build, so another thread that reads an object's cell
// list in the middle of the swap finds valid memory whichever of the two fields it read first.
struct ContactCells {
  ContactNode* head = nullptr;
  ContactNode* none = nullptr;
  std::array<ContactCell, FAKE_ENTRY_COUNT> entries = {};
};

// `gate` closes the region: a participant holds it shared while it reads game memory, the step's thread takes it
// exclusively to open and to close, so no helper reads game memory once the apply writes it. The claim counters and
// `stop_bits` are each one independent atomic operation a claim. `busy` and `reasons_logged` tell a second entry.
// The rest is the step thread's.
struct ContactState {
  std::shared_mutex gate;
  bool open = false;
  void** objects = nullptr;
  uint32_t object_count = 0;
  uint32_t chunk_count = 0;
  float radius_addition = 0.f;
  void* environment = nullptr;

  std::atomic<uint32_t> next_chunk = 0;
  std::atomic<uint32_t> next_block = 0;
  std::atomic<uint32_t> stop_bits = 0;
  uintptr_t unknown_vptr = 0;  // written by the participant that set STOP_CLASS first, read after the close

  std::atomic<bool> busy = false;
  std::atomic<bool> code_read = false;         // CheckContactCode held, at init and at the game's start
  std::atomic<uint32_t> reasons_logged = 0;    // bit per ContactReason
  std::atomic<uint32_t> original_ticks = 0;    // the ticks a reason or the skip latch left to the game's loop
  bool code_line_logged = false;
  bool other_mod = false;                      // the last code check's only misses were pinned masks
  bool table_off = false;                      // never cleared
  uint32_t skip_period = NO_PERIOD;            // tick / RESORT_PERIOD of a region that ended with a stop bit
  // The summary lines' sums over the handled ticks.
  uint32_t handled_ticks = 0;
  uint32_t fence_ticks = 0;
  uint32_t first_fence_tick = 0;
  uint32_t first_fence_index = 0;
  uint64_t region_time = 0;  // clock ticks
  uint64_t longest_region = 0;
  uint64_t join_wait = 0;
  uint64_t own_chunks = 0;
  uint64_t chunks = 0;
  uint64_t visited = 0;
  uint64_t listed = 0;
  uint64_t plain_objects = 0;
  uint64_t objects_seen = 0;

  // The tests' fake tables stand in here, and a margin that is too small for one control.
  double reach_margin = REACH_MARGIN;
  std::array<uint64_t, CONTACT_MASK_VALUES.size()> mask_values = CONTACT_MASK_VALUES;
  std::array<ContactClass, CLASS_SLOTS> classes = [] {
    std::array<ContactClass, CLASS_SLOTS> slots = {};
    for (const ContactClass& known : CONTACT_CLASSES) {
      uint8_t slot = ContactClassSlot(known.vptr);
      while (slots[slot].vptr != 0) {
        ++slot;
      }
      slots[slot] = known;
    }
    return slots;
  }();
};
static ContactState contact;
static std::array<ContactItem, OBJECT_LIMIT> contact_items;                             // 1.6 MB, zero until used
static std::array<ContactNode, size_t{NODE_BLOCK_COUNT} * NODE_BLOCK_NODES> contact_nodes;  // 10.5 MB, the same
static std::array<ContactSlot, SLOT_COUNT> contact_slots;
// Not constexpr-built: the entries hold the object's own addresses.
static constinit ContactCells contact_cells = [] {
  ContactCells cells;
  for (ContactCell& entry : cells.entries) {
    entry.head = &contact_cells.none;
  }
  cells.entries[0].head = &contact_cells.head;
  return cells;
}();
static_assert(std::is_trivially_destructible_v<ContactItem> && std::is_trivially_destructible_v<ContactNode>
              && std::is_trivially_destructible_v<ContactSlot> && std::is_trivially_destructible_v<ContactCells>);

// Whether the game's code and table entries the contact split's reasoning is about are the ones read: every range of
// CONTACT_RANGES, then the entries' chain. The first that is not gets the row's one line: a range's start and
// the offset of its first 16-byte block whose check word differs (the last block if none does), or an entry's table
// and slot. The 64-bit values decide; the check words only name the place. Ours: a range's masked bytes are read as zero
// whatever they hold: another patcher's known spots, each with its reason at CONTACT_MASKS.
// Ours (the pin; ATN 11-27, so that the log alone says what changed): a pinned mask's own bytes must hash to one of its
// allowed values, the two contents the row's reasoning was checked for. With anything else there the row is off as
// for any other miss, and every such mask gets its own line where the rest of its range is the game's (in a range
// that differs it is the game's code that is not the one read, not another mod's spot). The lines are written by the
// first check that misses only.
[[nodiscard]] static bool CheckContactCode(const LoaderApi& loader) noexcept {
  const bool log_misses = !contact.code_line_logged;
  bool differs = false;
  bool other_mod = false;
  const auto differs_at = [&](uint64_t address, uint32_t offset) {
    differs = true;
    if (log_misses) {
      loader.log("renodx_attila64: contact split: off (game code differs at 0x%llx+0x%x)", address, offset);
    }
  };
  constexpr uint32_t BLOCK_BYTES = 16;
  size_t check_index = 0;
  size_t mask_index = 0;
  size_t value_index = 0;
  for (const ContactRange& range : CONTACT_RANGES) {
    const uint32_t block_count = (range.size + BLOCK_BYTES - 1) / BLOCK_BYTES;
    uint64_t hash = FNV_OFFSET_BASIS<uint64_t>;
    uint32_t first_differing = block_count;
    for (uint32_t block = 0; block < block_count; ++block) {
      const uint32_t offset = block * BLOCK_BYTES;
      const uint32_t count = std::min(BLOCK_BYTES, range.size - offset);
      std::array<uint8_t, BLOCK_BYTES> bytes = {};
      std::memcpy(bytes.data(), reinterpret_cast<const void*>(uintptr_t{range.start} + offset), count);
      for (size_t index = mask_index; index < mask_index + range.mask_count; ++index) {
        const ContactMask& mask = CONTACT_MASKS[index];
        for (uint32_t byte = 0; byte < count; ++byte) {
          if (range.start + offset + byte - mask.start < mask.size) {
            bytes[byte] = 0;
          }
        }
      }
      hash = Fnv1a(bytes.data(), count, hash);
      if (first_differing == block_count && static_cast<uint16_t>(hash) != CONTACT_RANGE_CHECKS[check_index + block]) {
        first_differing = block;
      }
    }
    check_index += block_count;
    for (const size_t end = mask_index + range.mask_count; mask_index < end; ++mask_index) {
      const ContactMask& mask = CONTACT_MASKS[mask_index];
      const std::span allowed(contact.mask_values.data() + value_index, mask.value_count);
      value_index += mask.value_count;
      if (mask.value_count == 0) continue;
      const uint64_t mask_hash = Fnv1a<uint64_t>(reinterpret_cast<const uint8_t*>(uintptr_t{mask.start}), mask.size);
      if (std::ranges::find(allowed, mask_hash) != allowed.end()) continue;
      other_mod = true;
      if (log_misses && hash == range.value) {
        loader.log("renodx_attila64: contact split: off (another mod changed the game code at 0x%llx: hash 0x%016llx is not the game's or PerfFix 7.5's)",
                   uint64_t{mask.start}, mask_hash);
      }
    }
    // One line for the code outside the pinned masks: the first range's.
    if (hash != range.value && !differs) {
      differs_at(range.start, std::min(first_differing, block_count - 1) * BLOCK_BYTES);
    }
  }
  if (!differs) {
    uint64_t chain = FNV_OFFSET_BASIS<uint64_t>;
    const ContactEntry* named = &CONTACT_ENTRIES.back();
    bool found = false;
    for (const ContactEntry& entry : CONTACT_ENTRIES) {
      chain = Fnv1a(reinterpret_cast<const uint8_t*>(uintptr_t{entry.vtable} + entry.slot), sizeof(uint64_t), chain);
      if (!found && static_cast<uint16_t>(chain) != entry.check) {
        named = &entry;
        found = true;
      }
    }
    if (chain != CONTACT_ENTRY_CHAIN) {
      differs_at(named->vtable, named->slot);
    }
  }
  const bool code_read = !differs && !other_mod;
  contact.code_line_logged = contact.code_line_logged || !code_read;
  contact.other_mod = other_mod && !differs;
  contact.code_read.store(code_read, std::memory_order_relaxed);
  return code_read;
}

// One participant's share of the open region: chunks of the object list, claimed one by one, each object's kept nodes
// into contact_items and contact_nodes. It reads game memory, writes plugin memory, takes no lock but the region's and
// calls nothing of the game's. The step's thread waits for the region; a helper that cannot enter at once has nothing
// to do there. Ours: it returns the chunks it did, for the summary line's share of the step's thread (a helper's
// count is dropped).
static uint32_t RunRegionShare(uint32_t slot) noexcept {
  std::shared_lock lock(contact.gate, std::defer_lock);
  if (slot == OWNER_SLOT) {
    lock.lock();
  } else if (!lock.try_lock()) {
    return 0;
  }
  if (!contact.open) return 0;
  ContactSlot& own = contact_slots[slot];
  const double twice_addition = 2.0 * std::fabs(double{contact.radius_addition});
  const double margin = contact.reach_margin;
  uint32_t next_node = 0;  // the next free node of this participant's block, and the block's end
  uint32_t block_end = 0;
  uint32_t chunks_done = 0;
  // A set stop bit: this tick is the game's already.
  while (contact.stop_bits.load(std::memory_order_relaxed) == 0) {
    const uint32_t chunk = contact.next_chunk.fetch_add(1, std::memory_order_relaxed);
    if (chunk >= contact.chunk_count) break;
    ++chunks_done;
    const uint32_t chunk_end = std::min((chunk + 1) * CHUNK_OBJECTS, contact.object_count);
    for (uint32_t place = chunk * CHUNK_OBJECTS; place < chunk_end; ++place) {
      void* const object = contact.objects[place];
      ContactItem& item = contact_items[place];
      item = {.object = object};
      // The table test read the manager's environment; a walk reads the object's.
      if (At<void*>(object, OBJECT_ENVIRONMENT) != contact.environment) {
        contact.stop_bits.fetch_or(STOP_OTHER_WORLD, std::memory_order_relaxed);
        continue;
      }
      const uintptr_t vptr = At<uintptr_t>(object, 0);
      const ContactClass* known = nullptr;
      for (uint8_t class_slot = ContactClassSlot(vptr); contact.classes[class_slot].vptr != 0 && known == nullptr;
           ++class_slot) {
        if (contact.classes[class_slot].vptr == vptr) {
          known = &contact.classes[class_slot];
        }
      }
      if (known == nullptr) {
        // Code that was not read would run in this object's walk: no later object could rely on its kept nodes.
        if ((contact.stop_bits.fetch_or(STOP_CLASS, std::memory_order_relaxed) & STOP_CLASS) == 0) {
          contact.unknown_vptr = vptr;
        }
        continue;
      }
      if (known->mode == CONTACT_NAVAL) {
        contact.stop_bits.fetch_or(STOP_NAVAL, std::memory_order_relaxed);
        continue;
      }
      if (known->mode == CONTACT_FENCED) {
        item.mode = CONTACT_FENCED;
        continue;
      }
      const uint32_t cell_count = At<uint32_t>(object, OBJECT_CELL_COUNT);
      if (cell_count > FAKE_ENTRY_COUNT) continue;

      // The game's walk (its cells in order, each chain in order, the object's own node skipped, an id's first node
      // only), with a test the game's circle and height filters cannot pass without: all in double, on the stored
      // values' sizes, so neither rounding nor the order of the game's float operations matters. A NaN keeps the node.
      // The object's own terms are read once: nothing writes game memory while the region is open.
      const double object_radius = std::fabs(double{At<float>(object, OBJECT_RADIUS)});
      const double object_height = std::fabs(double{At<float>(object, OBJECT_HEIGHT)});
      const float object_x = At<float>(object, OBJECT_X);
      const float object_y = At<float>(object, OBJECT_Y);
      const float object_z = At<float>(object, OBJECT_Z);
      ContactNode* last_kept = nullptr;
      bool pool_full = false;
      for (uint32_t cell = 0; cell < cell_count && !pool_full; ++cell) {
        void* const entry = At<uint8_t*>(object, OBJECT_CELLS) + (cell * CELL_BYTES);
        for (void* node = *At<void**>(entry, CELL_HEAD); node != nullptr && !pool_full;
             node = At<void*>(node, offsetof(ContactNode, next))) {
          if (node == entry) continue;
          const uint32_t id = At<uint32_t>(node, offsetof(ContactNode, id));
          const uint32_t word = (id >> 5) % ID_SET_WORDS;
          const uint32_t bit = 1u << (id & 31);
          if ((own.marks[word] & bit) != 0) continue;
          own.marks[word] |= bit;
          if (item.mark_count < VISITED_LIMIT) {
            own.visited[item.mark_count] = static_cast<uint16_t>(word);
          }
          ++item.mark_count;

          void* const record = At<void*>(node, offsetof(ContactNode, record));
          const double reach = object_radius + std::fabs(double{At<float>(record, RECORD_RADIUS)}) + twice_addition;
          const float x_distance = At<float>(record, RECORD_X) - object_x;
          const float z_distance = At<float>(record, RECORD_Z) - object_z;
          const double distance_squared = (double{x_distance} * x_distance) + (double{z_distance} * z_distance);
          const float y_distance = object_y - At<float>(record, RECORD_Y);
          const double height = std::max(object_height, std::fabs(double{At<float>(record, RECORD_HEIGHT)}));
          if (distance_squared > (reach * reach * margin) + REACH_FLOOR
              || std::fabs(double{y_distance}) > (height * margin) + REACH_FLOOR) {
            continue;
          }
          if (next_node == block_end) {
            const uint32_t block = contact.next_block.fetch_add(1, std::memory_order_relaxed);
            if (block >= NODE_BLOCK_COUNT) {
              contact.stop_bits.fetch_or(STOP_POOL, std::memory_order_relaxed);
              pool_full = true;
              continue;
            }
            next_node = block * NODE_BLOCK_NODES;
            block_end = next_node + NODE_BLOCK_NODES;
          }
          // Kind and owner together, as the pair a node's constructor wrote.
          ContactNode& kept = contact_nodes[next_node];
          kept = {.kind = At<uint32_t>(node, offsetof(ContactNode, kind)),
                  .id = id,
                  .record = record,
                  .owner = At<void*>(node, offsetof(ContactNode, owner))};
          if (last_kept == nullptr) {
            item.first_node = next_node;
          } else {
            last_kept->next = &kept;
          }
          last_kept = &kept;
          ++next_node;
          ++item.node_count;
        }
      }
      if (item.mark_count > VISITED_LIMIT) {
        own.marks.fill(0);
      } else {
        for (uint32_t visit = 0; visit < item.mark_count; ++visit) {
          own.marks[own.visited[visit]] = 0;
        }
      }
      item.mode = CONTACT_FAST;
    }
  }
  return chunks_done;
}

// The engine's task queue, as the game's own parallel-for in this pass's caller writes it (0x5df9a5..0x5dfb9c): the
// task manager's list with its count, the counter the game keeps beside its lock, the lock (the game's import stubs,
// 0 = locked) and the wake. A task is 0x48 bytes; a helper is one with its state behind.
inline constexpr uintptr_t QUEUE_LOCK = 0x409d30;
inline constexpr uintptr_t QUEUE_UNLOCK = 0x409d20;
inline constexpr uintptr_t QUEUE_WAKE = 0x3062aa0;
inline constexpr size_t TASK_MANAGER_HEAD = 0x18;
inline constexpr size_t TASK_MANAGER_TAIL = 0x20;
inline constexpr size_t TASK_MANAGER_COUNT = 0x28;
inline constexpr size_t TASK_MANAGER_INSIDE = 0x40;
inline constexpr size_t TASK_MANAGER_LOCK = 0x48;
using QueueLockFn = int(ATTILA64_GAME_CALL*)(void* lock) noexcept;
using QueueWakeFn = void(ATTILA64_GAME_CALL*)(void* task_manager) noexcept;

enum HelperState : uint32_t { HELPER_FREE, HELPER_QUEUED };
struct HelperTask {
  const struct HelperTable* table = nullptr;
  void* previous = nullptr;  // the queue's links: any task of the engine's
  void* next = nullptr;
  uint8_t in_queue = 0;      // the fetch unlinks a task only with this set
  void* parent = nullptr;    // null: no consumer touches the task after its second table call
  uint8_t child = 0;         // set: only task threads take it
  uint32_t range_first = 0;  // the range, grain and body of the engine's parallel-for: not used
  uint32_t range_last = 0;
  uint64_t grain = 0;
  void* body = nullptr;
  std::atomic<uint32_t> state = HELPER_FREE;
};
static_assert(offsetof(HelperTask, next) == 0x10 && offsetof(HelperTask, in_queue) == 0x18
              && offsetof(HelperTask, parent) == 0x20 && offsetof(HelperTask, child) == 0x28
              && offsetof(HelperTask, state) == 0x48 && std::is_trivially_destructible_v<HelperTask>);
// Helper k uses slot k. Between QUEUED and FREE a helper is run at most once, and FREE is the last thing its run
// does, so a slot has one user at a time even when a helper left over from an earlier region is queued again in
// the same one.
static std::array<HelperTask, HELPER_COUNT> contact_helpers;

ATTILA64_GAME_CALL static void RunHelper(HelperTask* task) noexcept {
  const auto helper = static_cast<size_t>(task - contact_helpers.data());
  assert(helper < HELPER_COUNT);  // the queue hands back our own objects only
  RunRegionShare(static_cast<uint32_t>(helper));
}
// The release pairs with the enqueue's compare-exchange: the one order between this run's writes to the slot and the
// next run's reads (two shared holders of the gate are not ordered).
ATTILA64_GAME_CALL static void FreeHelper(HelperTask* task, uint32_t /*flag*/) noexcept {
  task->state.store(HELPER_FREE, std::memory_order_release);
}
ATTILA64_GAME_CALL static void IgnoreHelperCall() noexcept {}
// A task's table as the engine calls it: run, then "done" (after which the task is its owner's again); the engine
// calls the other three only for a task with a parent.
struct HelperTable {
  void (ATTILA64_GAME_CALL* run)(HelperTask* task) noexcept = &RunHelper;
  void (ATTILA64_GAME_CALL* done)(HelperTask* task, uint32_t flag) noexcept = &FreeHelper;
  std::array<void (ATTILA64_GAME_CALL*)() noexcept, 3> others = {&IgnoreHelperCall, &IgnoreHelperCall, &IgnoreHelperCall};
};
inline constexpr HelperTable HELPER_TABLE;

// Queues a region's helpers: the FREE ones among the first `helper_count` (one still queued from an earlier region
// is skipped, and no higher index takes its place, so stale and new helpers together stay within the count). The
// game's enqueue line by line (tail insertion, the wake only from an empty queue), with our own task objects, no
// parent and the child byte set. Everything happens under the queue's lock: false = the lock call failed, and neither
// a helper nor the queue was written. Ours, against the engine: it prints and goes on without the lock.
[[nodiscard]] static bool StartHelpers(void* task_manager, uint32_t helper_count) noexcept {
  void* const lock = static_cast<uint8_t*>(task_manager) + TASK_MANAGER_LOCK;
  if (reinterpret_cast<QueueLockFn>(QUEUE_LOCK)(lock) != 0) return false;
  ++At<uint32_t>(task_manager, TASK_MANAGER_INSIDE);
  const uint32_t count_before = At<uint32_t>(task_manager, TASK_MANAGER_COUNT);
  for (uint32_t helper = 0; helper < helper_count; ++helper) {
    HelperTask& task = contact_helpers[helper];
    uint32_t free_state = HELPER_FREE;
    if (!task.state.compare_exchange_strong(free_state, HELPER_QUEUED, std::memory_order_acq_rel)) continue;
    task.table = &HELPER_TABLE;
    task.in_queue = 0;
    task.parent = nullptr;
    task.child = 1;
    if (At<void*>(task_manager, TASK_MANAGER_HEAD) == nullptr) {
      At<void*>(task_manager, TASK_MANAGER_HEAD) = &task;
      At<void*>(task_manager, TASK_MANAGER_TAIL) = &task;
      task.previous = nullptr;
      task.next = nullptr;
    } else {
      void* const tail = At<void*>(task_manager, TASK_MANAGER_TAIL);
      At<void*>(tail, offsetof(HelperTask, next)) = &task;
      task.previous = tail;
      task.next = nullptr;
      At<void*>(task_manager, TASK_MANAGER_TAIL) = &task;
    }
    // Before the unlock on every path: the fetch hands out a task without this byte still linked.
    task.in_queue = 1;
    ++At<uint32_t>(task_manager, TASK_MANAGER_COUNT);
  }
  --At<uint32_t>(task_manager, TASK_MANAGER_INSIDE);
  reinterpret_cast<QueueLockFn>(QUEUE_UNLOCK)(lock);
  if (count_before == 0) {
    reinterpret_cast<QueueWakeFn>(QUEUE_WAKE)(task_manager);
  }
  return true;
}

// The contact pass for `manager`: true = done here with the game's results, false = the caller runs the game's
// function (nothing of the game's was written or called before a false, and the region is closed).
[[nodiscard]] static bool ContactTick(void* manager, Block* block, const LoaderApi& loader) noexcept {
  if (!contact.code_read.load(std::memory_order_relaxed)) return false;
  if ((block->core_wanted.load(std::memory_order_relaxed) & CORE_SPLIT) == 0) {
    SetLane(block, ENGINE_OFF, ENGINE_SPLIT_SHIFT);
    return false;
  }
  if (((block->engine_status.load(std::memory_order_relaxed) >> ENGINE_SPLIT_SHIFT) & ENGINE_LANE_MASK) == ENGINE_OFF) {
    SetLane(block, ENGINE_NOT_USED_YET, ENGINE_SPLIT_SHIFT);
  }
  if (contact.table_off) {
    // Also after the player switched the row off and on.
    SetLane(block, ENGINE_PARTLY, ENGINE_SPLIT_SHIFT);
    return false;
  }
  // Each reason's line once; the lane says partly from the first on, but for the one-tick reason.
  const auto original = [&](ContactReason reason) {
    contact.original_ticks.fetch_add(1, std::memory_order_relaxed);
    if ((contact.reasons_logged.fetch_or(1u << reason, std::memory_order_relaxed) & (1u << reason)) == 0) {
      if (reason == CONTACT_REASON_CLASS) {
        loader.log("renodx_attila64: contact split: partly (unknown class: vtable 0x%llx)",
                   uint64_t{contact.unknown_vptr});
      } else {
        loader.log("renodx_attila64: contact split: partly (%s)", CONTACT_REASON_WORDS[reason]);
      }
    }
    if (reason != CONTACT_REASON_ID_SET) {
      SetLane(block, ENGINE_PARTLY, ENGINE_SPLIT_SHIFT);
    }
    return false;
  };

  // The ticks that are the game's by their state, each test on what the game's own function reads first: its
  // first call (its static init), a re-sort tick, a tail with entries, a step without tasks, a list too short to
  // gain or too long for the ids, and the rest of a period in which a region was stopped.
  void* const id_set = reinterpret_cast<void*>(GAME_ID_SET);
  if (At<uint8_t>(id_set, ID_SET_GUARD) == 0) return false;
  void* const environment = At<void*>(manager, CONTACT_ENVIRONMENT);
  const uint32_t tick = At<uint32_t>(At<void*>(environment, ENVIRONMENT_MODEL), MODEL_TICK);
  if (At<uint8_t>(manager, CONTACT_RESORT) != 0 || tick % RESORT_PERIOD == 0) return false;
  if (At<uint32_t>(manager, CONTACT_TAIL_COUNT) != 0) return original(CONTACT_REASON_SHIPS);
  if (*reinterpret_cast<const uint8_t*>(TASK_FLAG) == 0) return original(CONTACT_REASON_THREADS);
  // Not the game's getter: with no manager yet it would build one here, which the game's pass never does.
  void* const task_manager = *reinterpret_cast<void* const*>(TASK_MANAGER_POINTER);
  if (task_manager == nullptr) return original(CONTACT_REASON_THREADS);
  const uint32_t task_threads = At<uint32_t>(task_manager, TASK_MANAGER_THREADS);
  if (task_threads < MIN_TASK_THREADS) return original(CONTACT_REASON_THREADS);
  const uint32_t object_count = At<uint32_t>(manager, CONTACT_OBJECT_COUNT);
  if (object_count < MIN_OBJECTS || object_count > OBJECT_LIMIT) return false;
  if (tick / RESORT_PERIOD == contact.skip_period) {
    contact.original_ticks.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  if (contact.busy.exchange(true, std::memory_order_acquire)) return original(CONTACT_REASON_NESTED);
  // Every false from here on frees the row for the next tick.
  const auto original_after_busy = [&](ContactReason reason) {
    contact.busy.store(false, std::memory_order_release);
    return original(reason);
  };

  // The table test, last: a walk reads a lazily built game table through the object's environment, and builds it
  // when it is not there (a file read, in the middle of the pass). The chain is the game's, from the manager's
  // environment; detect compares each object's with it. The inner links are tested: a null one is "no table".
  {
    void* const core = At<void*>(environment, ENVIRONMENT_CORE);
    void* const database = ((core != nullptr) ? At<void*>(core, CORE_DATABASE) : nullptr);
    if (database == nullptr || At<void*>(database, DATABASE_TABLE) == nullptr) {
      contact.table_off = true;
      return original_after_busy(CONTACT_REASON_TABLE);
    }
  }

  // The region: opened, the helpers started, the step thread's own share, closed. The exclusive lock of the close
  // waits until every helper has left, so every claimed chunk is done before anything below reads the items.
  const uint32_t chunk_count = (object_count + CHUNK_OBJECTS - 1) / CHUNK_OBJECTS;
  const uint32_t helper_count = std::min({task_threads - KEPT_OUT_THREADS, HELPER_COUNT, chunk_count - 1});
  const auto region_start = std::chrono::steady_clock::now();
  {
    const std::unique_lock lock(contact.gate);
    contact.objects = At<void**>(manager, CONTACT_OBJECTS);
    contact.object_count = object_count;
    contact.chunk_count = chunk_count;
    contact.radius_addition = *reinterpret_cast<const float*>(CONTACT_RADIUS_ADDITION);
    contact.environment = environment;
    contact.next_chunk.store(0, std::memory_order_relaxed);
    contact.next_block.store(0, std::memory_order_relaxed);
    contact.stop_bits.store(0, std::memory_order_relaxed);
    contact.open = true;
  }
  const bool started = StartHelpers(task_manager, helper_count);
  uint32_t own_chunks = 0;
  if (started) {
    own_chunks = RunRegionShare(OWNER_SLOT);
  } else {
    // A helper of an earlier region that is inside finds no further chunk.
    contact.next_chunk.store(chunk_count, std::memory_order_relaxed);
  }
  const auto share_done = std::chrono::steady_clock::now();
  {
    const std::unique_lock lock(contact.gate);
    contact.open = false;
  }
  const auto region_end = std::chrono::steady_clock::now();
  if (!started) return original_after_busy(CONTACT_REASON_QUEUE_LOCK);

  // A stopped region: the claiming ended early, so the items are not read; no further region in this period.
  if (const uint32_t stop_bits = contact.stop_bits.load(std::memory_order_relaxed); stop_bits != 0) {
    contact.skip_period = tick / RESORT_PERIOD;
    if ((stop_bits & STOP_OTHER_WORLD) != 0) return original_after_busy(CONTACT_REASON_OTHER_WORLD);
    if ((stop_bits & STOP_NAVAL) != 0) return original_after_busy(CONTACT_REASON_SHIPS);
    if ((stop_bits & STOP_CLASS) != 0) return original_after_busy(CONTACT_REASON_CLASS);
    return original_after_busy(CONTACT_REASON_POOL);
  }
  // The capacity rule: the game pushes every id a walk visits, the apply only the kept ones. With no walk past the
  // id vector's capacity as it is now, the game would not allocate for these objects either (the vector never
  // shrinks), so the heap sees the same calls.
  uint64_t visited = 0;
  uint64_t listed = 0;
  uint32_t plain_objects = 0;
  bool grows = false;
  for (uint32_t place = 0; place < object_count; ++place) {
    const ContactItem& item = contact_items[place];
    if (item.mode == CONTACT_FAST) {
      visited += item.mark_count;
      listed += item.node_count;
      grows = grows || item.mark_count > At<uint32_t>(id_set, ID_SET_CAPACITY);
    } else {
      ++plain_objects;
    }
  }
  if (grows) return original_after_busy(CONTACT_REASON_ID_SET);

  // The apply: the game's loop, line by line, each field read again where the game reads it again. The one addition
  // is how an object's function is called.
  const auto clear_id_set = [&] {
    if (At<uint32_t>(id_set, ID_SET_COUNT) != 0) {
      uint32_t listed_id = 0;
      do {
        At<uint32_t>(id_set, (At<uint16_t*>(id_set, ID_SET_DATA)[listed_id] >> 5) * sizeof(uint32_t)) = 0;
        ++listed_id;
      } while (listed_id < At<uint32_t>(id_set, ID_SET_COUNT));
    }
    At<uint32_t>(id_set, ID_SET_COUNT) = 0;
  };
  // The count of the vector class H's path always pushes to, through the chain the game's code walks there. A null
  // link: the game's push would fault, so none happened; the value is outside the counts, so a link that appears
  // during a call still differs.
  const auto fence_count = [](void* object) {
    void* link = At<void*>(object, OBJECT_ENVIRONMENT);
    for (const size_t offset : {ENVIRONMENT_MODEL, MODEL_FENCE_MANAGER}) {
      if (link == nullptr) return FENCE_NO_MANAGER;
      link = At<void*>(link, offset);
    }
    return ((link != nullptr) ? uint64_t{At<uint32_t>(link, FENCE_COUNT)} : FENCE_NO_MANAGER);
  };
  const auto object_step = reinterpret_cast<ContactObjectFn>(CONTACT_OBJECT_STEP);
  bool plain_rest = false;  // the game's walk for every object from here on
  bool fence_fired = false;
  if (At<uint32_t>(manager, CONTACT_OBJECT_COUNT) != 0) {
    void** cursor = At<void**>(manager, CONTACT_OBJECTS);
    uint32_t place = 0;
    do {
      void* const object = *cursor;
      const bool known = !plain_rest && place < object_count && contact_items[place].object == object;
      if (known && contact_items[place].mode == CONTACT_FAST) {
        const ContactItem& item = contact_items[place];
        void* const cells = At<void*>(object, OBJECT_CELLS);
        const uint32_t cell_count = At<uint32_t>(object, OBJECT_CELL_COUNT);
        contact_cells.head = ((item.node_count != 0) ? &contact_nodes[item.first_node] : nullptr);
        // Four stores another thread may see, in this order: with the count first on the way in and the pointer
        // first on the way out, any pair of the two fields indexes valid memory. std::atomic_ref keeps them apart
        // and in order (plain stores may be merged or reordered); each is one mov.
        std::atomic_ref<uint32_t>(At<uint32_t>(object, OBJECT_CELL_COUNT))
            .store(((item.node_count != 0) ? 1 : 0), std::memory_order_release);
        std::atomic_ref<void*>(At<void*>(object, OBJECT_CELLS))
            .store(contact_cells.entries.data(), std::memory_order_release);
        object_step(object, id_set);
        std::atomic_ref<void*>(At<void*>(object, OBJECT_CELLS)).store(cells, std::memory_order_release);
        std::atomic_ref<uint32_t>(At<uint32_t>(object, OBJECT_CELL_COUNT)).store(cell_count, std::memory_order_release);
      } else {
        const bool fenced = known && contact_items[place].mode == CONTACT_FENCED;
        const uint64_t fence_before = (fenced ? fence_count(object) : 0);
        object_step(object, id_set);
        if (fenced && fence_count(object) != fence_before) {
          plain_rest = true;
          if (!fence_fired && contact.fence_ticks == 0) {
            contact.first_fence_tick = tick;
            contact.first_fence_index = place;
          }
          fence_fired = true;
        } else if (!known) {
          plain_rest = true;
        }
      }
      clear_id_set();
      ++cursor;
      ++place;
    } while (reinterpret_cast<uintptr_t>(cursor)
             != reinterpret_cast<uintptr_t>(At<void**>(manager, CONTACT_OBJECTS)
                                            + At<uint32_t>(manager, CONTACT_OBJECT_COUNT)));
  }
  if (At<uint32_t>(manager, CONTACT_TAIL_COUNT) != 0) {
    uint32_t listed_place = 0;
    do {
      reinterpret_cast<ContactTailFn>(CONTACT_TAIL_STEP)(At<void**>(manager, CONTACT_TAIL_LIST)[listed_place]);
      ++listed_place;
    } while (listed_place < At<uint32_t>(manager, CONTACT_TAIL_COUNT));
  }
  clear_id_set();

  contact.fence_ticks += (fence_fired ? 1 : 0);
  const auto clock_ticks = [](std::chrono::steady_clock::duration time) { return static_cast<uint64_t>(time.count()); };
  contact.region_time += clock_ticks(region_end - region_start);
  contact.longest_region = std::max(contact.longest_region, clock_ticks(region_end - region_start));
  contact.join_wait += clock_ticks(region_end - share_done);
  contact.own_chunks += own_chunks;
  contact.chunks += chunk_count;
  contact.visited += visited;
  contact.listed += listed;
  contact.plain_objects += plain_objects;
  contact.objects_seen += object_count;
  ++contact.handled_ticks;
  // Partly is sticky, as the battle step row's: a reason's line stays true of this game start.
  const bool partly = (contact.reasons_logged.load(std::memory_order_relaxed) & ~(1u << CONTACT_REASON_ID_SET)) != 0;
  SetLane(block, (partly ? ENGINE_PARTLY : ENGINE_ACTIVE), ENGINE_SPLIT_SHIFT);
  if (contact.handled_ticks == 1) {
    loader.log("renodx_attila64: contact split: active (objects %u, plain %u, listed %llu, helpers %u)", object_count,
               plain_objects, listed, helper_count);
  }
  if (std::ranges::find(SUMMARY_TICKS, contact.handled_ticks) != SUMMARY_TICKS.end()) {
    const auto microseconds = [](uint64_t time) {
      return static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::duration(time)).count());
    };
    loader.log("renodx_attila64: contact split: %u ticks (region mean %llu us, max %llu us, join wait mean %llu us, own "
               "chunks %llu %%, visited %llu, listed %llu, plain %llu %%, fence ticks %u (first at tick %u, index %u), "
               "original ticks %u)",
               contact.handled_ticks, microseconds(contact.region_time / contact.handled_ticks),
               microseconds(contact.longest_region), microseconds(contact.join_wait / contact.handled_ticks),
               (contact.own_chunks * 100) / contact.chunks, contact.visited, contact.listed,
               (contact.plain_objects * 100) / contact.objects_seen, contact.fence_ticks, contact.first_fence_tick,
               contact.first_fence_index, contact.original_ticks.load(std::memory_order_relaxed));
  }
  contact.busy.store(false, std::memory_order_release);
  return true;
}

}  // namespace twattila::attila64_plugin::battle_step
