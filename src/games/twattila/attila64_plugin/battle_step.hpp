/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
//   (BpBroadPhaseShared.cpp PairManagerData, Apache-2.0), which answers "does this pair exist", not "which groups hold
//   this soldier", so it does not fit.
// - A generation stamp per entry instead of clearing the table each call is the timestamp form of Briggs and Torczon,
//   "An Efficient Representation for Sparse Sets" (1993).
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
// A group's members (MEMBER_LIMIT pointers) and their count, a soldier's id, a pair's two soldiers.
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
    {.address = 0x7e6180, .length = 3, .hash = 0xa3703b1a918cfbeb},
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

// Shared with the test, which pins the same hashes on the game file.
[[nodiscard]] static uint64_t Fnv1a64(const uint8_t* bytes, size_t count) noexcept {
  uint64_t hash = 0xcbf29ce484222325;
  for (size_t offset = 0; offset < count; ++offset) {
    hash = (hash ^ bytes[offset]) * 0x100000001b3;
  }
  return hash;
}

// The battle step's lane of engine_status: written only on a change, the other lanes kept.
static void SetLane(Block* block, EngineState lane) noexcept {
  uint32_t status = block->engine_status.load(std::memory_order_relaxed);
  do {
    if (((status >> ENGINE_STEP_SHIFT) & ENGINE_LANE_MASK) == lane) return;
  } while (!block->engine_status.compare_exchange_weak(
      status, (status & ~(ENGINE_LANE_MASK << ENGINE_STEP_SHIFT)) | (uint32_t{lane} << ENGINE_STEP_SHIFT),
      std::memory_order_relaxed));
}

// Whether the game's code and tables in memory are the ones read; the first that is not gets the row's one line.
[[nodiscard]] static bool CheckGameCode(const LoaderApi& loader) noexcept {
  for (const CodeRange& range : CODE_RANGES) {
    if (Fnv1a64(reinterpret_cast<const uint8_t*>(uintptr_t{range.address}), range.length) != range.hash) {
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
    for (uint32_t member = first_member; member < member_count; ++member) {
      const uint32_t id = At<uint32_t>(At<void*>(group, GROUP_MEMBERS + (member * sizeof(void*))), SOLDIER_ID);
      if (id >= ID_LIMIT) return REASON_ID;
      if (state.node_count == NODE_CAPACITY) return REASON_POOL;
      Entry& entry = index.entries[id];
      if (entry.generation != state.generation) {
        entry = {.generation = state.generation, .head = NO_NODE};
      }
      // A group's first member is in the index only if the group already has a place: the same group listed twice.
      // A join through one place would leave the other without the new member, so such lists are not indexed (the
      // game never builds them: it takes a group out of its list before it hands the group out again).
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
    after_game_call();
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
              const Reason reason =
                  ((group == nullptr)
                       ? REASON_TYPE
                       : index_group(group, (NEW_LIST << WHERE_LIST_SHIFT) | (list_count(NEW_LIST) - 1), 0));
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
                  const auto position = static_cast<uint32_t>(slot - groups);
                  const Reason reason = index_group(group, (list << WHERE_LIST_SHIFT) | position, count_before);
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

}  // namespace twattila::attila64_plugin::battle_step
