/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The battle step row (battle_step.hpp, through its hook in hooks.hpp) against the game's pair resolver: both run on
// the same fake managers, groups, soldiers and pairs in the game's layout, and must make the same calls in the same
// order and leave the same memory, byte for byte.
// - Without the game file: the game's resolver is this file's own transcription of it, its sort and append are fakes
//   at the game's addresses, and the two group kinds' join functions are models of the game's.
// - With the game file: the game's own code runs (mapped at its addresses, its allocator sent to this test's), the
//   group kinds are copies of the game's tables, and the transcription must agree with the game's resolver too. Also
//   there: every code range and table word the row checks, on the file's bytes and in memory, and that nothing jumps
//   into the bytes the hook replaces.
// Cases: seeded random worlds, the hot manager's shape, controls that must differ (an index that misses a holder,
// game code that writes what the row's reasoning says it never writes), each reason for the game's own loop, an entry
// while a call runs, the switch, the status lane and the row's log lines; and the text a batch tool looks for in the
// DLL file.
// The contact split row of the same file (ContactTick; its hook comes with the row's wiring) has its own part below:
// its tables without the game file, and with it the checked code and table entries byte by byte, the code check's
// lines, and the game's contact pass against the row on fake worlds.
// battle_step_test.exe <renodx_attila64.dll> [<game ELF>]; exit code = failures, or 77 with none when the game-file
// part did not run.

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../hooks.hpp"
#include "fake_loader.hpp"

namespace {

using namespace twattila::attila64_plugin;
using namespace twattila::attila64_plugin::battle_step;
using namespace twattila::attila64_plugin::test;

inline constexpr int EXIT_SKIPPED = 77;

// The game's fields that only the fakes read, and the fakes' own in bytes the game's code here does not touch.
inline constexpr size_t PAIR_KEY = 0x10;     // a float: the game sorts its pairs by it
inline constexpr size_t PAIR_ANSWER = 0x14;  // the fake pair test's answer
inline constexpr size_t PAIR_BYTES = 0x18;
inline constexpr size_t SOLDIER_VALID = 0x58;  // the fake manager's answer for this soldier
inline constexpr size_t SOLDIER_BYTES = 0x60;
inline constexpr size_t GROUP_MANAGER = 0x10;
inline constexpr size_t GROUP_LIMIT = 0x50;
inline constexpr size_t GROUP_RETIRE_ANSWER = 0x58;
inline constexpr size_t GROUP_BYTES = 0x60;
inline constexpr size_t LIST_CAPACITY = 0x8;
inline constexpr size_t MANAGER_SPARE_GROUPS = 0x100;  // the groups the fake create hands out, in order
inline constexpr size_t MANAGER_SPARE_NEXT = 0x108;
inline constexpr size_t MANAGER_BYTES = 0x110;
// The game's id table is 8196 bytes; the game reads it for any id, so the fake has room for ids up to 131071.
inline constexpr size_t ID_BITS_BYTES = 8196 + 8192 + 12;
inline constexpr uint32_t ID_BITS_LIMIT = 2 * ID_LIMIT;
inline constexpr size_t STORE_BYTES = STORE_USED + 8;
inline constexpr size_t GROUP_JOIN_ONE_SLOT = 0x70 / sizeof(void*);
inline constexpr size_t GROUP_STARTED_SLOT = 0x78 / sizeof(void*);
inline constexpr size_t TABLE_SLOTS = 32;
// A game bool is one byte of a register: the fakes fill the rest.
inline constexpr uintptr_t ANSWER_NOISE = 0x5a5a5a00;
inline constexpr uintptr_t GAME_MALLOC = 0x525f10;
inline constexpr uintptr_t GAME_FREE = 0x525fd0;
inline constexpr size_t ARENA_BYTES = size_t{128} << 20;

// Every fake object and every allocation of the code under test, front to back: one run's memory is the arena's bytes.
uint8_t* arena = nullptr;
size_t arena_used = 0;
size_t arena_dirty = 0;  // the bytes written since the last clear

void* Take(size_t bytes) {
  void* taken = arena + arena_used;
  arena_used += (bytes + 15) & ~size_t{15};
  if (arena_used > ARENA_BYTES) {
    std::printf("FAIL: the test's arena is full\n");
    ExitProcess(1);
  }
  arena_dirty = std::max(arena_dirty, arena_used);
  return taken;
}

enum CallKind : uint8_t {
  CALL_SORT,
  CALL_FINISH_SORT,
  CALL_APPEND,
  CALL_PAIR_TEST,
  CALL_VALID,
  CALL_CREATE,
  CALL_RETIRE,
  CALL_RETIRE_TEST,
  CALL_JOIN_TEST,
};
struct Call {
  CallKind kind = CALL_SORT;
  uintptr_t first = 0;
  uintptr_t second = 0;
  uintptr_t third = 0;
  uintptr_t result = 0;
  bool operator==(const Call&) const = default;
};
std::vector<Call> calls;
// The model join test's calls, logged or not: how many the row leaves out.
uint64_t join_tests = 0;

// What a case's fake game code does besides answering.
std::function<void(void* manager, void* pair)> on_pair_test;
std::function<void(void* manager, void* soldier)> on_valid;
std::function<void(void* group)> on_retire_test;

std::vector<std::string> step_lines;
void QuietLog(const char* format, ...) noexcept {
  char line[2048];
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(line, sizeof(line), format, arguments);
  va_end(arguments);
  step_lines.emplace_back(line);
}
size_t LinesWith(std::string_view text) {
  return static_cast<size_t>(
      std::ranges::count_if(step_lines, [&](const std::string& line) { return line.find(text) != std::string::npos; }));
}

LoaderApi api = {};
PairsFn game_resolver = nullptr;
bool hook_in = false;
uint32_t handled_calls = 0;
// What the plugin's hook holds as the game's function, and the managers it was called for.
std::vector<void*> passed_on;
ATTILA64_GAME_CALL void PassOn(void* manager) noexcept {
  passed_on.push_back(manager);
  game_resolver(manager);
}
// A call of the game's resolver as game code makes it: through the plugin's hook while the hook is in. A call the
// hook did not hand to the game's function was done by the row.
void Resolve(void* manager) {
  if (!hook_in) {
    game_resolver(manager);
    return;
  }
  const size_t passed_before = passed_on.size();
  PairsEntry(manager);
  handled_calls += ((std::find(passed_on.begin() + passed_before, passed_on.end(), manager) == passed_on.end()) ? 1 : 0);
}

ATTILA64_GAME_CALL uintptr_t FakePairTest(void* manager, void* pair) noexcept {
  if (on_pair_test) {
    on_pair_test(manager, pair);
  }
  const uintptr_t result = ANSWER_NOISE | At<uint8_t>(pair, PAIR_ANSWER);
  calls.push_back({CALL_PAIR_TEST, reinterpret_cast<uintptr_t>(manager), reinterpret_cast<uintptr_t>(pair), 0, result});
  return result;
}

ATTILA64_GAME_CALL uintptr_t FakeValid(void* manager, void* soldier) noexcept {
  if (on_valid) {
    on_valid(manager, soldier);
  }
  const uintptr_t result = ANSWER_NOISE | At<uint8_t>(soldier, SOLDIER_VALID);
  calls.push_back({CALL_VALID, reinterpret_cast<uintptr_t>(manager), reinterpret_cast<uintptr_t>(soldier), 0, result});
  return result;
}

// As the game's: the next unused group, empty, then both soldiers through the group's own single join.
ATTILA64_GAME_CALL void* FakeCreate(void* manager, void* first, void* second) noexcept {
  void* group = At<void**>(manager, MANAGER_SPARE_GROUPS)[At<uint32_t>(manager, MANAGER_SPARE_NEXT)];
  ++At<uint32_t>(manager, MANAGER_SPARE_NEXT);
  for (void* soldier : {first, second}) {
    Slot<ManagerTestFn>(group, GROUP_JOIN_ONE_SLOT)(group, soldier);
  }
  calls.push_back({CALL_CREATE, reinterpret_cast<uintptr_t>(manager), reinterpret_cast<uintptr_t>(first),
                   reinterpret_cast<uintptr_t>(second), reinterpret_cast<uintptr_t>(group)});
  return group;
}

ATTILA64_GAME_CALL void FakeRetire(void* manager, void* group) noexcept {
  calls.push_back({CALL_RETIRE, reinterpret_cast<uintptr_t>(manager), reinterpret_cast<uintptr_t>(group)});
}

ATTILA64_GAME_CALL uintptr_t FakeRetireTest(void* group) noexcept {
  if (on_retire_test) {
    on_retire_test(group);
  }
  const uintptr_t result = ANSWER_NOISE | At<uint8_t>(group, GROUP_RETIRE_ANSWER);
  calls.push_back({CALL_RETIRE_TEST, reinterpret_cast<uintptr_t>(group), 0, 0, result});
  return result;
}

// The game's join test of the two indexed kinds: loads only unless the group holds one of the two.
ATTILA64_GAME_CALL uintptr_t ModelJoinTest(void* group, void* first, void* second) noexcept {
  ++join_tests;
  for (uint32_t member = 0; member < At<uint32_t>(group, GROUP_MEMBER_COUNT); ++member) {
    void* held = At<void*>(group, GROUP_MEMBERS + (member * sizeof(void*)));
    if (held == first) return Slot<ManagerTestFn>(group, GROUP_JOIN_ONE_SLOT)(group, second);
    if (held == second) return Slot<ManagerTestFn>(group, GROUP_JOIN_ONE_SLOT)(group, first);
  }
  return ANSWER_NOISE;
}

// Another kind's join test: the same answers, and every call is in the log (the row never leaves one of these out).
ATTILA64_GAME_CALL uintptr_t LoggedJoinTest(void* group, void* first, void* second) noexcept {
  const uintptr_t result = ModelJoinTest(group, first, second);
  calls.push_back({CALL_JOIN_TEST, reinterpret_cast<uintptr_t>(group), reinterpret_cast<uintptr_t>(first),
                   reinterpret_cast<uintptr_t>(second), result});
  return result;
}

// The game's single join: a started group asks its table first (the game's answer is no), a full group says no, then
// the manager's soldier test; a yes appends the soldier and sets its bit. Not in the call log, as the game's own is
// not: what it does shows in the soldier test's call and in the memory.
ATTILA64_GAME_CALL uintptr_t ModelJoinOne(void* group, void* soldier) noexcept {
  uintptr_t result = 0;
  void* manager = At<void*>(group, GROUP_MANAGER);
  const uint32_t count = At<uint32_t>(group, GROUP_MEMBER_COUNT);
  if ((At<uint8_t>(group, GROUP_STARTED) == 0 || (Slot<GroupRetireTestFn>(group, GROUP_STARTED_SLOT)(group) & 0xff) != 0)
      && count < At<uint32_t>(group, GROUP_LIMIT)
      && (Slot<ManagerTestFn>(manager, MANAGER_VALID_SLOT)(manager, soldier) & 0xff) != 0) {
    At<void*>(group, GROUP_MEMBERS + (count * sizeof(void*))) = soldier;
    At<uint32_t>(group, GROUP_MEMBER_COUNT) = count + 1;
    const uint32_t id = At<uint32_t>(soldier, SOLDIER_ID);
    At<uint32_t*>(manager, MANAGER_ID_BITS)[id >> 5] |= 1u << (id & 31);
    result = 1;
  }
  return ANSWER_NOISE | result;
}

ATTILA64_GAME_CALL uintptr_t ModelStarted(void* /*group*/) noexcept { return 0; }

// What the game's code took from the test's allocator while a contact case ran: each block's place in the arena and
// its bytes.
bool note_game_memory = false;
std::vector<std::pair<size_t, size_t>> game_memory;
ATTILA64_GAME_CALL void* TestMalloc(size_t bytes, uint32_t /*heap*/) noexcept {
  void* taken = Take(bytes);
  if (note_game_memory) {
    game_memory.emplace_back(static_cast<size_t>(static_cast<uint8_t*>(taken) - arena), bytes);
  }
  return taken;
}
ATTILA64_GAME_CALL void TestFree(void* /*memory*/) noexcept {}

// The fakes at the game's addresses while the game file is not mapped: any fixed order of the pairs does for a sort.
ATTILA64_GAME_CALL void FakeSortPairs(void*** first, void*** last, uint64_t depth) noexcept {
  calls.push_back({CALL_SORT, reinterpret_cast<uintptr_t>(*first), reinterpret_cast<uintptr_t>(*last), depth});
  std::stable_sort(*first, *last, [](void* left, void* right) { return At<float>(left, PAIR_KEY) < At<float>(right, PAIR_KEY); });
}
ATTILA64_GAME_CALL void FakeFinishSort(void*** first, void*** last) noexcept {
  calls.push_back({CALL_FINISH_SORT, reinterpret_cast<uintptr_t>(*first), reinterpret_cast<uintptr_t>(*last)});
}
// The game's append: one slot at first, then double, the old memory freed.
ATTILA64_GAME_CALL void FakeAppend(void* list, void* group) noexcept {
  const uint32_t count = At<uint32_t>(list, LIST_COUNT);
  if (At<uint32_t>(list, LIST_CAPACITY) == 0) {
    At<uint32_t>(list, LIST_CAPACITY) = 1;
    At<void**>(list, LIST_GROUPS) = static_cast<void**>(TestMalloc(sizeof(void*), 0));
  } else if (count >= At<uint32_t>(list, LIST_CAPACITY)) {
    void** old_groups = At<void**>(list, LIST_GROUPS);
    At<uint32_t>(list, LIST_CAPACITY) *= 2;
    At<void**>(list, LIST_GROUPS) = static_cast<void**>(TestMalloc(At<uint32_t>(list, LIST_CAPACITY) * sizeof(void*), 0));
    std::copy_n(old_groups, count, At<void**>(list, LIST_GROUPS));
    TestFree(old_groups);
  }
  At<void**>(list, LIST_GROUPS)[count] = group;
  At<uint32_t>(list, LIST_COUNT) = count + 1;
  calls.push_back({CALL_APPEND, reinterpret_cast<uintptr_t>(list), reinterpret_cast<uintptr_t>(group)});
}

// The game's resolver, written from its code a second time and on its own: plain numbers, no word of battle_step.hpp
// but the call types and the three addresses. With the game file mapped it must agree with the game's own.
ATTILA64_GAME_CALL void Transcription(void* manager) noexcept {
  const auto yes = [](uintptr_t answer) { return (answer & 0xff) != 0; };
  if (const uint32_t pair_count = At<uint32_t>(manager, 0x1c); pair_count != 0) {
    void** const pairs = At<void**>(manager, 0x20);
    uint64_t depth = 0;
    for (uint32_t rest = pair_count; rest > 1; rest >>= 1) {
      depth += 2;
    }
    void** first_end = pairs;
    void** last_end = pairs + pair_count;
    reinterpret_cast<SortPairsFn>(0x7ea860)(&first_end, &last_end, depth);
    void** first_end_again = pairs;
    void** last_end_again = pairs + pair_count;
    reinterpret_cast<FinishPairSortFn>(0x7ea9c0)(&first_end_again, &last_end_again);
    if (At<uint32_t>(manager, 0x1c) != 0) {
      void** pair_slot = At<void**>(manager, 0x20);
      do {
        void* pair = *pair_slot;
        if (yes(Slot<ManagerTestFn>(manager, 8)(manager, pair))) {
          void* first = At<void*>(pair, 0);
          void* second = At<void*>(pair, 8);
          const uint32_t* bits = At<const uint32_t*>(manager, 0xd8);
          const uint32_t first_id = At<uint32_t>(first, 0x54);
          const uint32_t second_id = At<uint32_t>(second, 0x54);
          const uint32_t first_bit = bits[first_id >> 5] & (1u << (first_id & 31));
          const uint32_t second_bit = bits[second_id >> 5] & (1u << (second_id & 31));
          if (first_bit == 0 && second_bit == 0) {
            if (yes(Slot<ManagerTestFn>(manager, 5)(manager, first)) && yes(Slot<ManagerTestFn>(manager, 5)(manager, second))) {
              void* group = Slot<CreateGroupFn>(manager, 3)(manager, first, second);
              reinterpret_cast<AppendGroupFn>(0x7e0570)(static_cast<uint8_t*>(manager) + 0xc0, group);
            }
          } else if (first_bit == 0 || second_bit == 0) {
            for (const size_t list : {size_t{0xc0}, size_t{0xa8}}) {
              if (At<uint32_t>(manager, list + 0xc) == 0) continue;
              void** group_slot = At<void**>(manager, list + 0x10);
              bool joined = false;
              do {
                joined = yes(Slot<GroupJoinFn>(*group_slot, 6)(*group_slot, first, second));
                ++group_slot;
              } while (!joined && group_slot != At<void**>(manager, list + 0x10) + At<uint32_t>(manager, list + 0xc));
            }
          }
        }
        ++pair_slot;
      } while (pair_slot != At<void**>(manager, 0x20) + At<uint32_t>(manager, 0x1c));
    }
  }
  uint32_t position = At<uint32_t>(manager, 0xb4) - 1;
  if (position != UINT32_MAX) {
    uint32_t count = 0;
    do {
      void* group = At<void**>(manager, 0xb8)[position];
      if (yes(Slot<GroupRetireTestFn>(group, 4)(group))) {
        Slot<RetireGroupFn>(manager, 4)(manager, group);
        count = At<uint32_t>(manager, 0xb4) - 1;
        At<void**>(manager, 0xb8)[position] = At<void**>(manager, 0xb8)[count];
        At<uint32_t>(manager, 0xb4) = count;
      } else {
        count = At<uint32_t>(manager, 0xb4);
      }
      --position;
    } while (position < count);
  }
  if (At<uint32_t>(manager, 0xcc) != 0) {
    void** group_slot = At<void**>(manager, 0xd0);
    do {
      void* group = *group_slot;
      if (yes(Slot<GroupRetireTestFn>(group, 4)(group))) {
        Slot<RetireGroupFn>(manager, 4)(manager, group);
      } else {
        reinterpret_cast<AppendGroupFn>(0x7e0570)(static_cast<uint8_t*>(manager) + 0xa8, group);
      }
      ++group_slot;
    } while (group_slot != At<void**>(manager, 0xd0) + At<uint32_t>(manager, 0xcc));
  }
  At<uint32_t>(manager, 0x1c) = 0;
  void* store = At<void*>(manager, 0x30);
  At<void*>(manager, 0x38) = store;
  if (store != nullptr) {
    At<uint32_t>(store, 0x5dc8) = 0;
  }
  const uint32_t table_count = At<uint32_t>(manager, 0x6c);
  for (uint32_t entry = 0; entry != table_count; ++entry) {
    At<void**>(manager, 0x70)[entry] = nullptr;
  }
  At<uint32_t>(manager, 0xcc) = 0;
}

// The tables of the fake objects: the manager's (and a second one that is not the hot manager's), the two indexed
// group kinds' (models here, copies of the game's with the game file) and another kind's.
std::array<void*, TABLE_SLOTS> manager_table = {};
std::array<void*, TABLE_SLOTS> other_manager_table = {};
std::array<std::array<void*, TABLE_SLOTS>, 2> group_tables = {};
std::array<void*, TABLE_SLOTS> other_group_table = {};

// The fake objects, in the game's layout. Lists and pairs are filled in place: their memory holds `capacity` slots.
void* NewManager(void** table, uint32_t old_capacity, uint32_t new_capacity, uint32_t pair_capacity, uint32_t spare_groups) {
  void* manager = Take(MANAGER_BYTES);
  At<void**>(manager, 0) = table;
  At<void*>(manager, MANAGER_PAIRS) = Take(pair_capacity * sizeof(void*));
  for (const auto [list, capacity] : {std::pair{OLD_LIST, old_capacity}, std::pair{NEW_LIST, new_capacity}}) {
    At<uint32_t>(manager, MANAGER_LISTS[list] + LIST_CAPACITY) = capacity;
    At<void*>(manager, MANAGER_LISTS[list] + LIST_GROUPS) = ((capacity != 0) ? Take(capacity * sizeof(void*)) : nullptr);
  }
  At<void*>(manager, MANAGER_ID_BITS) = Take(ID_BITS_BYTES);
  At<void*>(manager, MANAGER_SPARE_GROUPS) = Take(spare_groups * sizeof(void*));
  // What the resolver resets at its end, set to something else.
  void* store = Take(STORE_BYTES);
  At<uint32_t>(store, STORE_USED) = 77;
  At<void*>(manager, MANAGER_STORE_FIRST) = store;
  At<void*>(manager, MANAGER_STORE_CURRENT) = manager;
  At<uint32_t>(manager, MANAGER_TABLE_COUNT) = 3;
  At<void**>(manager, MANAGER_TABLE) = static_cast<void**>(Take(3 * sizeof(void*)));
  std::fill_n(At<void**>(manager, MANAGER_TABLE), 3, manager);
  return manager;
}
void* NewSoldier(uint32_t id) {
  void* soldier = Take(SOLDIER_BYTES);
  At<uint32_t>(soldier, SOLDIER_ID) = id;
  At<uint8_t>(soldier, SOLDIER_VALID) = 1;
  return soldier;
}
void* NewGroup(void* manager, void** table, uint32_t limit) {
  void* group = Take(GROUP_BYTES);
  At<void**>(group, 0) = table;
  At<void*>(group, GROUP_MANAGER) = manager;
  At<uint32_t>(group, GROUP_LIMIT) = limit;
  return group;
}
void SetBit(void* manager, void* soldier) {
  const uint32_t id = At<uint32_t>(soldier, SOLDIER_ID);
  At<uint32_t*>(manager, MANAGER_ID_BITS)[id >> 5] |= 1u << (id & 31);
}
void AddMember(void* group, void* soldier) {
  At<void*>(group, GROUP_MEMBERS + (At<uint32_t>(group, GROUP_MEMBER_COUNT) * sizeof(void*))) = soldier;
  ++At<uint32_t>(group, GROUP_MEMBER_COUNT);
}
void AddToList(void* manager, uint32_t list, void* group) {
  At<void**>(manager, MANAGER_LISTS[list] + LIST_GROUPS)[At<uint32_t>(manager, MANAGER_LISTS[list] + LIST_COUNT)] = group;
  ++At<uint32_t>(manager, MANAGER_LISTS[list] + LIST_COUNT);
}
void* AddPair(void* manager, void* first, void* second, float key) {
  void* pair = Take(PAIR_BYTES);
  At<void*>(pair, 0) = first;
  At<void*>(pair, PAIR_SECOND) = second;
  At<float>(pair, PAIR_KEY) = key;
  At<uint8_t>(pair, PAIR_ANSWER) = 1;
  At<void**>(manager, MANAGER_PAIRS)[At<uint32_t>(manager, MANAGER_PAIR_COUNT)] = pair;
  ++At<uint32_t>(manager, MANAGER_PAIR_COUNT);
  return pair;
}
// A group of one indexed kind in the older list, its members with their bits set.
void* AddOldGroup(void* manager, std::initializer_list<void*> members, uint32_t limit = MEMBER_LIMIT) {
  void* group = NewGroup(manager, group_tables[0].data(), limit);
  for (void* soldier : members) {
    AddMember(group, soldier);
    SetBit(manager, soldier);
  }
  AddToList(manager, OLD_LIST, group);
  return group;
}

void ClearArena() {
  std::memset(arena, 0, arena_dirty);
  arena_used = 0;
  arena_dirty = 0;
}
// A small manager for a hand-made case: room for 64 groups a list and 64 pairs, 16 groups of limit 2 to hand out.
void* NewScene(void** table = manager_table.data()) {
  void* manager = NewManager(table, 64, 64, 64, 16);
  for (uint32_t spare = 0; spare < 16; ++spare) {
    At<void**>(manager, MANAGER_SPARE_GROUPS)[spare] = NewGroup(manager, group_tables[spare % 2].data(), 2);
  }
  return manager;
}

// One run of the resolver on the world as it was kept: the calls it made and the arena after it.
struct Outcome {
  std::vector<Call> calls;
  std::vector<uint8_t> memory;
  uint32_t reasons = 0;  // battle_step's reasons_logged after it
  uint32_t handled = 0;  // the calls the row did itself
};
std::vector<uint8_t> world;
void KeepWorld() { world.assign(arena, arena + arena_used); }
// `fresh`: the row's own state as at the game's start (its sticky status, its once-only lines, a stale `nested`).
Outcome Run(void* manager, PairsFn game, bool hooked, bool fresh = true) {
  std::memcpy(arena, world.data(), world.size());
  std::memset(arena + world.size(), 0, arena_dirty - world.size());
  arena_used = world.size();
  arena_dirty = world.size();
  calls.clear();
  if (fresh) {
    step_lines.clear();
    state.partly = false;
    state.reasons_logged = 0;
    state.active_logged = false;
    state.nested.store(false);
  }
  game_resolver = game;
  hook_in = hooked;
  handled_calls = 0;
  passed_on.clear();
  Resolve(manager);
  return {.calls = calls,
          .memory = std::vector<uint8_t>(arena, arena + arena_dirty),
          .reasons = state.reasons_logged,
          .handled = handled_calls};
}
bool Same(const Outcome& left, const Outcome& right) { return left.calls == right.calls && left.memory == right.memory; }

// The game's resolver first (with the game file its own code, then the transcription).
std::vector<PairsFn> games;
// The world as built now, through every game resolver without the hook and through the hook: all the same. Returns
// the hooked run.
Outcome ExpectSame(void* manager, const char* what) {
  KeepWorld();
  const Outcome game = Run(manager, games[0], false);
  bool same = true;
  for (size_t other = 1; other < games.size(); ++other) {
    same = same && Same(game, Run(manager, games[other], false));
  }
  Outcome hooked = Run(manager, games[0], true);
  Expect(same && Same(game, hooked), what);
  return hooked;
}
// A control on the world as it was kept: its fake game code breaks what the row stands on, so the hooked run must
// differ.
void ExpectDifferent(void* manager, const char* what) {
  const Outcome game = Run(manager, games[0], false);
  Expect(!Same(game, Run(manager, games[0], true)), what);
}

struct Shape {
  uint32_t soldiers = 0;
  uint32_t old_groups = 0;
  uint32_t new_groups = 0;
  uint32_t pairs = 0;
  uint32_t limit = 0;  // a group's member limit; 0: 1..5 each
  bool high_ids = false;
  bool shared_ids = false;
  bool other_kind_listed = false;
  bool other_kind_created = false;
  bool repeated_groups = false;
  bool over_full = false;
};

// A random world of `shape`: soldiers in and out of groups (in two groups, with and without their bit), groups of
// both indexed kinds (started ones, full ones, empty ones), pairs of every bit state with equal sort keys, and each
// answer byte 0, 1 or another value.
void* BuildWorld(std::mt19937_64* random, const Shape& shape) {
  const auto below = [&](uint32_t limit) { return static_cast<uint32_t>((*random)() % limit); };
  const auto chance = [&](uint32_t percent) { return below(100) < percent; };
  const auto answer = [&](uint32_t yes_percent) {
    return static_cast<uint8_t>(chance(yes_percent) ? 1 : (chance(50) ? 0 : 0xfe));
  };
  ClearArena();
  void* manager = NewManager(manager_table.data(), shape.old_groups + below(3), shape.new_groups + below(3), shape.pairs,
                             shape.pairs);
  if (chance(20)) {
    At<void*>(manager, MANAGER_STORE_FIRST) = nullptr;
  }
  At<uint32_t>(manager, MANAGER_TABLE_COUNT) = below(4);
  if (shape.new_groups == 0 && chance(50)) {
    At<uint32_t>(manager, MANAGER_LISTS[NEW_LIST] + LIST_CAPACITY) = 0;
    At<void*>(manager, MANAGER_LISTS[NEW_LIST] + LIST_GROUPS) = nullptr;
  }

  std::vector<void*> soldiers;
  const uint32_t id_offset = below(ID_LIMIT);
  for (uint32_t soldier = 0; soldier < shape.soldiers; ++soldier) {
    uint32_t id = ((soldier * 40503u) + id_offset) % ID_LIMIT;  // distinct below ID_LIMIT
    if (shape.shared_ids && soldier != 0 && chance(10)) {
      id = At<uint32_t>(soldiers[below(soldier)], SOLDIER_ID);
    }
    if (shape.high_ids && chance(10)) {
      id = std::array<uint32_t, 5>{ID_LIMIT - 1, ID_LIMIT, ID_LIMIT + 1, 100000 + below(1000), ID_BITS_LIMIT - 1}[below(5)];
    }
    soldiers.push_back(NewSoldier(id));
    At<uint8_t>(soldiers.back(), SOLDIER_VALID) = answer(85);
    if (chance(5)) {
      SetBit(manager, soldiers.back());
    }
  }
  const auto any_soldier = [&] { return soldiers[below(shape.soldiers)]; };

  std::vector<void*> groups;
  std::vector<void*> members;
  const auto new_group = [&](bool created) {
    void** table = group_tables[below(2)].data();
    if ((created ? shape.other_kind_created : shape.other_kind_listed) && chance(created ? 10 : 3)) {
      table = other_group_table.data();
    }
    void* group = NewGroup(manager, table, ((shape.limit != 0) ? shape.limit : 1 + below(MEMBER_LIMIT)));
    At<uint8_t>(group, GROUP_STARTED) = (chance(10) ? 1 : 0);
    At<uint8_t>(group, GROUP_RETIRE_ANSWER) = answer(20);
    return group;
  };
  for (const auto [list, count] : {std::pair{OLD_LIST, shape.old_groups}, std::pair{NEW_LIST, shape.new_groups}}) {
    for (uint32_t position = 0; position < count; ++position) {
      if (shape.repeated_groups && !groups.empty() && chance(5)) {
        AddToList(manager, list, groups[below(static_cast<uint32_t>(groups.size()))]);
        continue;
      }
      void* group = new_group(false);
      const uint32_t limit = At<uint32_t>(group, GROUP_LIMIT);
      const uint32_t member_count = (chance(70) ? limit : below(limit + 1));
      for (uint32_t member = 0; member < member_count; ++member) {
        void* soldier = any_soldier();
        AddMember(group, soldier);
        members.push_back(soldier);
        if (chance(95)) {
          SetBit(manager, soldier);
        }
      }
      // More members than the array holds: the count's own bytes are the sixth slot.
      if (shape.over_full && chance(3)) {
        At<uint32_t>(group, GROUP_MEMBER_COUNT) = MEMBER_LIMIT + 1;
      }
      AddToList(manager, list, group);
      groups.push_back(group);
    }
  }
  for (uint32_t spare = 0; spare < shape.pairs; ++spare) {
    At<void**>(manager, MANAGER_SPARE_GROUPS)[spare] = new_group(true);
  }
  for (uint32_t pair = 0; pair < shape.pairs; ++pair) {
    void* first = ((!members.empty() && chance(60)) ? members[below(static_cast<uint32_t>(members.size()))] : any_soldier());
    void* second = (chance(3) ? first : any_soldier());
    if (chance(50)) {
      std::swap(first, second);
    }
    At<uint8_t>(AddPair(manager, first, second, static_cast<float>(below(8))), PAIR_ANSWER) = answer(90);
  }
  return manager;
}

// Every case on the tables and resolvers set up now.
void RunCases(const char* label, uint32_t random_cases, uint32_t hot_cases, uint64_t seed) {
  std::printf("  %s: %u random worlds, %u of the hot manager's shape\n", label, random_cases, hot_cases);
  const int failures_before = failures;
  state.code_read.store(true);
  block.core_wanted.store(CORE_STEP);
  on_pair_test = nullptr;
  on_valid = nullptr;

  // Random worlds: mostly small, some with lists of hundreds, a few of 2000.
  std::mt19937_64 random(seed);
  const auto below = [&](uint32_t limit) { return static_cast<uint32_t>(random() % limit); };
  uint32_t whole = 0;
  std::array<uint32_t, REASON_COUNT> with_reason = {};
  join_tests = 0;
  uint64_t game_join_tests = 0;
  for (uint32_t world_index = 0; world_index < random_cases + hot_cases; ++world_index) {
    Shape shape;
    if (world_index >= random_cases) {
      // Limit 2, thousands of groups, most pairs with one soldier in a group.
      shape = {.soldiers = 12000, .old_groups = 5000, .new_groups = below(20), .pairs = 1000, .limit = 2};
    } else {
      const uint32_t size = below(200);
      const uint32_t most_groups = ((size == 0) ? 2000 : ((size < 10) ? 300 : 12));
      shape = {.soldiers = 2 + below(3 * most_groups),
               .old_groups = below(most_groups + 1),
               .new_groups = below((most_groups / 2) + 1),
               .pairs = below((size < 10) ? 200 : 30),
               .limit = ((below(3) == 0) ? 2u : 0u),
               .high_ids = below(100) < 15,
               .shared_ids = below(100) < 15,
               .other_kind_listed = below(100) < 5,
               .other_kind_created = below(100) < 10,
               .repeated_groups = below(100) < 10,
               .over_full = below(100) < 3};
    }
    void* manager = BuildWorld(&random, shape);
    KeepWorld();
    const uint64_t join_tests_before = join_tests;
    const Outcome game = Run(manager, games[0], false);
    game_join_tests += join_tests - join_tests_before;
    bool same = true;
    for (size_t other = 1; other < games.size(); ++other) {
      same = same && Same(game, Run(manager, games[other], false));
    }
    const uint64_t join_tests_between = join_tests;
    const Outcome hooked = Run(manager, games[0], true);
    join_tests -= join_tests_between - join_tests_before;  // only the hooked runs stay counted
    ++checks;
    if (!same || !Same(game, hooked)) {
      ++failures;
      std::printf("FAIL: %s world %u (seed %llu): %s\n", label, world_index, seed,
                  (same ? "the row differs from the game's resolver" : "the game's resolvers differ"));
    }
    whole += ((hooked.handled != 0 && hooked.reasons == 0) ? 1 : 0);
    for (uint32_t reason = 1; reason < REASON_COUNT; ++reason) {
      with_reason[reason] += (hooked.reasons >> reason) & 1u;
    }
  }
  std::printf("    done by the index alone: %u; with the game's loop for type %u, id %u, pool %u, changed members %u, nested %u\n",
              whole, with_reason[REASON_TYPE], with_reason[REASON_ID], with_reason[REASON_POOL], with_reason[REASON_MEMBERS],
              with_reason[REASON_NESTED]);
  // With the game's own join test in the tables only the other kind's calls are counted.
  if (group_tables[0][GROUP_JOIN_SLOT] == reinterpret_cast<void*>(&ModelJoinTest)) {
    std::printf("    model join tests: the game's resolver %llu, the row %llu\n", game_join_tests, join_tests);
    Expect(join_tests * 4 < game_join_tests, "the row leaves most join tests out");
  }
  Expect(whole * 2 > random_cases && with_reason[REASON_TYPE] != 0 && with_reason[REASON_ID] != 0,
         "the random worlds reach the index and the game's loop");

  // Controls that must differ. An index that misses a holder: X is in a group, the pair (X, E) joins E to it.
  {
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    AddOldGroup(manager, {held});
    AddPair(manager, held, NewSoldier(12), 0.f);
    ExpectSame(manager, "control base: the pair's second soldier joins the first one's group");
    on_pair_test = [](void*, void*) { index.entries[10].generation = 0; };
    ExpectDifferent(manager, "control: an index without a holder's entry is found");
    on_pair_test = nullptr;
  }
  // Game code that writes another group's member, and game code that writes a soldier's id: the row's reasoning says
  // neither happens, and the comparison finds both.
  for (const bool writes_id : {false, true}) {
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    void* other_held = NewSoldier(11);
    void* group = AddOldGroup(manager, {held});
    AddOldGroup(manager, {other_held});
    void* refused = NewSoldier(20);
    At<uint8_t>(refused, SOLDIER_VALID) = 0;
    void* stranger = NewSoldier(30);
    AddPair(manager, refused, NewSoldier(21), 0.f);
    AddPair(manager, (writes_id ? held : stranger), other_held, 1.f);
    on_valid = [=](void*, void* soldier) {
      if (soldier != refused) return;
      if (writes_id) {
        At<uint32_t>(held, SOLDIER_ID) = 22;
      } else {
        AddMember(group, stranger);
      }
    };
    KeepWorld();
    ExpectDifferent(manager, (writes_id ? "control: game code that writes a soldier's id is found"
                                        : "control: game code that writes another group's member is found"));
    on_valid = nullptr;
  }

  // Each reason for the game's own loop, with its line once however often it happens.
  // Also: the row's lane says partly, and the row is free again for the next call.
  const auto expect_reason = [&](void* manager, Reason reason, uint32_t handled, const char* what) {
    block.engine_status.store(uint32_t{ENGINE_NOT_USED_YET} << ENGINE_STEP_SHIFT);
    const Outcome hooked = ExpectSame(manager, what);
    char line[96];
    std::snprintf(line, sizeof(line), "renodx_attila64: battle step: partly (%s)", REASON_WORDS[reason]);
    const bool partly = block.engine_status.load() == (uint32_t{ENGINE_PARTLY} << ENGINE_STEP_SHIFT);
    const Outcome again = Run(manager, games[0], true, false);
    Expect(hooked.reasons == (1u << reason) && hooked.handled == handled && Same(hooked, again) && LinesWith(line) == 1
               && LinesWith("battle step: ") == 1 && state.partly && partly && !state.busy.load(),
           what);
  };
  {
    // Another kind of group in a list: the game's function.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    AddOldGroup(manager, {held});
    AddToList(manager, OLD_LIST, NewGroup(manager, other_group_table.data(), 2));
    AddPair(manager, held, NewSoldier(12), 0.f);
    expect_reason(manager, REASON_TYPE, 0, "type: another kind of group in a list leaves the call to the game");
  }
  {
    // The same group twice in a list, with a member.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    AddToList(manager, OLD_LIST, AddOldGroup(manager, {held}));
    AddPair(manager, held, NewSoldier(12), 0.f);
    expect_reason(manager, REASON_TYPE, 0, "type: a group with a member listed twice leaves the call to the game");
  }
  {
    // The same empty group twice: nothing to index, the row does the call.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    AddOldGroup(manager, {held});
    AddToList(manager, OLD_LIST, AddOldGroup(manager, {}));
    AddPair(manager, held, NewSoldier(12), 0.f);
    const Outcome hooked = ExpectSame(manager, "an empty group listed twice is no reason");
    Expect(hooked.reasons == 0 && hooked.handled == 1, "an empty group listed twice: done by the index");
  }
  {
    // A started group is not indexed and never asked: its join test changes nothing whatever it holds. Its members
    // are not read either: one here has an id past the table, the other is no soldier at all. The pair's soldier it
    // holds joins nothing; the next pair is the index's as ever.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    void* in_started = NewSoldier(11);
    void* started = AddOldGroup(manager, {NewSoldier(ID_LIMIT + 4464), in_started});
    AddMember(started, reinterpret_cast<void*>(uintptr_t{8}));
    At<uint8_t>(started, GROUP_STARTED) = 1;
    AddOldGroup(manager, {held});
    AddPair(manager, in_started, NewSoldier(12), 0.f);
    AddPair(manager, held, NewSoldier(13), 1.f);
    const Outcome hooked = ExpectSame(manager, "a started group is never asked");
    Expect(hooked.reasons == 0 && hooked.handled == 1, "a started group: not indexed, its members not read");
  }
  {
    // The manager hands out a group of another kind: the game's loop from there on, every call of that kind logged.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    AddOldGroup(manager, {held});
    At<void**>(manager, MANAGER_SPARE_GROUPS)[0] = NewGroup(manager, other_group_table.data(), 2);
    void* first_new = NewSoldier(20);
    AddPair(manager, first_new, NewSoldier(21), 0.f);
    AddPair(manager, first_new, NewSoldier(22), 1.f);
    AddPair(manager, held, NewSoldier(23), 2.f);
    expect_reason(manager, REASON_TYPE, 1, "type: a new group of another kind, the game's loop for the rest");
  }
  {
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(ID_LIMIT + 4464);
    AddOldGroup(manager, {held});
    AddPair(manager, held, NewSoldier(12), 0.f);
    expect_reason(manager, REASON_ID, 0, "id: a member's id past the table leaves the call to the game");
  }
  {
    // A soldier with such an id that is in no group: no reason, it has no candidates.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    AddOldGroup(manager, {held});
    void* distant = NewSoldier(ID_LIMIT + 4464);
    SetBit(manager, distant);
    AddPair(manager, distant, NewSoldier(12), 0.f);
    AddPair(manager, NewSoldier(13), held, 1.f);
    const Outcome hooked = ExpectSame(manager, "a pair soldier's id past the table is no reason");
    Expect(hooked.reasons == 0 && hooked.handled == 1, "a pair soldier's id past the table: done by the index");
  }
  {
    // After a reason the index takes nothing more in: a new group of another kind then is no second reason.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    AddOldGroup(manager, {held});
    At<void**>(manager, MANAGER_SPARE_GROUPS)[0] = NewGroup(manager, other_group_table.data(), 2);
    AddPair(manager, held, NewSoldier(ID_LIMIT + 4464), 0.f);
    AddPair(manager, NewSoldier(20), NewSoldier(21), 1.f);
    expect_reason(manager, REASON_ID, 1, "id: a soldier past the table joins, and a later new group is no second reason");
  }
  {
    // An entry while the manager makes the new group (its second question about the pair's first soldier, from the
    // group's single join): nested is the reason, and the new group, of another kind here, is not looked at.
    ClearArena();
    void* manager = NewScene();
    At<void**>(manager, MANAGER_SPARE_GROUPS)[0] = NewGroup(manager, other_group_table.data(), 2);
    void* first_new = NewSoldier(20);
    AddPair(manager, first_new, NewSoldier(21), 0.f);
    void* inner = NewScene();
    void* inner_held = NewSoldier(10);
    AddOldGroup(inner, {inner_held});
    AddPair(inner, inner_held, NewSoldier(12), 0.f);
    At<uint8_t>(first_new, SOLDIER_VALID) = 1;
    on_valid = [=](void* asked, void* soldier) {
      // The spare byte behind the answer counts the questions about this soldier.
      if (asked == manager && soldier == first_new && ++At<uint8_t>(first_new, SOLDIER_VALID + 1) == 2) {
        Resolve(inner);
      }
    };
    expect_reason(manager, REASON_NESTED, 1, "nested: an entry while the manager makes a new group");
    on_valid = nullptr;
  }
  {
    // An entry after the pairs, from a group's "take me back" answer: the call was the index's, and still says so.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    void* group = AddOldGroup(manager, {held});
    AddPair(manager, held, NewSoldier(12), 0.f);
    void* inner = NewScene();
    void* inner_held = NewSoldier(10);
    AddOldGroup(inner, {inner_held});
    AddPair(inner, inner_held, NewSoldier(12), 0.f);
    on_retire_test = [=](void* asked) {
      if (asked == group && At<uint32_t>(inner, MANAGER_PAIR_COUNT) != 0) {
        Resolve(inner);
      }
    };
    expect_reason(manager, REASON_NESTED, 1, "nested: an entry after the pairs, in the game's two loops over the lists");
    on_retire_test = nullptr;
  }
  {
    // Fake game code gives the older list other memory (the same groups) under one pair's loop: the second candidate
    // is not taken from it, the game's loop goes on in the memory it read.
    ClearArena();
    void* manager = NewScene();
    void* in_both = NewSoldier(10);
    AddOldGroup(manager, {in_both});
    AddOldGroup(manager, {in_both});
    void* joins = NewSoldier(12);
    At<uint8_t>(joins, SOLDIER_VALID) = 0;
    AddPair(manager, in_both, joins, 0.f);
    void** moved = static_cast<void**>(Take(64 * sizeof(void*)));
    std::copy_n(At<void**>(manager, MANAGER_LISTS[OLD_LIST] + LIST_GROUPS), 2, moved);
    on_valid = [=](void* asked, void* soldier) {
      if (asked != manager || soldier != joins) return;
      // The first group's question gets a no and moves the list; the second group's gets a yes.
      if (At<void**>(manager, MANAGER_LISTS[OLD_LIST] + LIST_GROUPS) == moved) {
        At<uint8_t>(joins, SOLDIER_VALID) = 1;
      } else {
        At<void**>(manager, MANAGER_LISTS[OLD_LIST] + LIST_GROUPS) = moved;
      }
    };
    expect_reason(manager, REASON_MEMBERS, 1, "changed members: the list's memory moved under a pair's loop");
    on_valid = nullptr;
  }
  {
    // An entry during the join test of a candidate at the list's end, which then says no: the game's loop is at its
    // end too. Behind the end, a group of another kind, as above.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    AddOldGroup(manager, {held});
    At<void**>(manager, MANAGER_LISTS[OLD_LIST] + LIST_GROUPS)[1] = NewGroup(manager, other_group_table.data(), 2);
    void* refused = NewSoldier(12);
    At<uint8_t>(refused, SOLDIER_VALID) = 0;
    AddPair(manager, held, refused, 0.f);
    void* inner = NewScene();
    void* inner_held = NewSoldier(10);
    AddOldGroup(inner, {inner_held});
    AddPair(inner, inner_held, NewSoldier(13), 0.f);
    on_valid = [=](void* asked, void* soldier) {
      if (asked == manager && soldier == refused && At<uint32_t>(inner, MANAGER_PAIR_COUNT) != 0) {
        Resolve(inner);
      }
    };
    expect_reason(manager, REASON_NESTED, 1, "nested: an entry during the last candidate's join test, which says no");
    on_valid = nullptr;
  }
  {
    // More groups than the pool is sure to hold: one empty group listed 52429 times.
    ClearArena();
    const uint32_t group_count = (NODE_CAPACITY / MEMBER_LIMIT) + 1;
    void* manager = NewManager(manager_table.data(), group_count, 0, 1, 1);
    void* group = NewGroup(manager, group_tables[0].data(), 2);
    for (uint32_t position = 0; position < group_count; ++position) {
      AddToList(manager, OLD_LIST, group);
    }
    At<void**>(manager, MANAGER_SPARE_GROUPS)[0] = NewGroup(manager, group_tables[0].data(), 2);
    AddPair(manager, NewSoldier(10), NewSoldier(12), 0.f);
    expect_reason(manager, REASON_POOL, 0, "pool: more groups than the pool holds leaves the call to the game");
  }
  {
    // The pool runs out during the call: 52428 full groups use all but four nodes, and the third new group does not
    // fit. The last pair then runs the game's loop over every group.
    ClearArena();
    const uint32_t group_count = NODE_CAPACITY / MEMBER_LIMIT;
    void* manager = NewManager(manager_table.data(), group_count, 4, 4, 4);
    std::array<void*, MEMBER_LIMIT - 1> in_every_group = {};
    for (uint32_t soldier = 0; soldier < in_every_group.size(); ++soldier) {
      in_every_group[soldier] = NewSoldier(60001 + soldier);
      SetBit(manager, in_every_group[soldier]);
    }
    for (uint32_t position = 0; position < group_count; ++position) {
      void* group = NewGroup(manager, group_tables[position % 2].data(), MEMBER_LIMIT);
      void* own = NewSoldier(position);
      AddMember(group, own);
      SetBit(manager, own);
      for (void* soldier : in_every_group) {
        AddMember(group, soldier);
      }
      AddToList(manager, OLD_LIST, group);
    }
    void* first_new = NewSoldier(61001);
    AddPair(manager, first_new, NewSoldier(61002), 0.f);
    for (uint32_t pair = 1; pair < 3; ++pair) {
      AddPair(manager, NewSoldier(61001 + (2 * pair)), NewSoldier(61002 + (2 * pair)), static_cast<float>(pair));
    }
    AddPair(manager, first_new, NewSoldier(61009), 3.f);
    for (uint32_t spare = 0; spare < 4; ++spare) {
      At<void**>(manager, MANAGER_SPARE_GROUPS)[spare] = NewGroup(manager, group_tables[0].data(), 2);
    }
    expect_reason(manager, REASON_POOL, 1, "pool: a pool that runs out during the call, the game's loop for the rest");
  }
  {
    // Fake game code shortens the older list under the loop: a candidate's place is past the end.
    ClearArena();
    void* manager = NewScene();
    AddOldGroup(manager, {NewSoldier(10)});
    void* last_held = NewSoldier(11);
    AddOldGroup(manager, {last_held});
    void* refused = NewSoldier(20);
    At<uint8_t>(refused, SOLDIER_VALID) = 0;
    AddPair(manager, refused, NewSoldier(21), 0.f);
    AddPair(manager, last_held, NewSoldier(22), 1.f);
    on_valid = [=](void*, void* soldier) {
      if (soldier == refused) {
        At<uint32_t>(manager, MANAGER_LISTS[OLD_LIST] + LIST_COUNT) = 1;
      }
    };
    expect_reason(manager, REASON_MEMBERS, 1, "changed members: a candidate past the list's end, the game's loop");
    on_valid = nullptr;
  }
  {
    // Fake game code takes a member out of the group whose join test runs (the manager's soldier test, called from
    // the single join).
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    void* leaves = NewSoldier(11);
    void* group = AddOldGroup(manager, {leaves, held});
    void* joins = NewSoldier(12);
    AddPair(manager, held, joins, 0.f);
    AddPair(manager, held, NewSoldier(13), 1.f);
    on_valid = [=](void*, void* soldier) {
      if (soldier == joins) {
        At<void*>(group, GROUP_MEMBERS) = held;
        At<uint32_t>(group, GROUP_MEMBER_COUNT) = 1;
      }
    };
    expect_reason(manager, REASON_MEMBERS, 1, "changed members: a called group lost a member, the game's loop");
    on_valid = nullptr;
  }
  // Fake game code changes only the member count of the group whose join test runs, and the join is refused: fewer
  // members, then more than the group's array holds.
  for (const uint32_t count_after : {1u, MEMBER_LIMIT + 1}) {
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    void* group = AddOldGroup(manager, {NewSoldier(11), held});
    void* refused = NewSoldier(12);
    At<uint8_t>(refused, SOLDIER_VALID) = 0;
    AddPair(manager, held, refused, 0.f);
    on_valid = [=](void*, void* soldier) {
      if (soldier == refused) {
        At<uint32_t>(group, GROUP_MEMBER_COUNT) = count_after;
      }
    };
    expect_reason(manager, REASON_MEMBERS, 1, "changed members: a called group's member count changed, the game's loop");
    on_valid = nullptr;
  }
  {
    // An entry during a candidate's join test that the candidate then refuses: the game's loop goes on behind that
    // candidate, where the entry (another manager that lists the next group) put the pair's soldier into that group.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    void* other_held = NewSoldier(11);
    AddOldGroup(manager, {held});
    void* next_group = AddOldGroup(manager, {other_held});
    void* joins_inside = NewSoldier(12);
    At<uint8_t>(joins_inside, SOLDIER_VALID) = 0;
    AddPair(manager, held, joins_inside, 0.f);
    void* inner = NewScene();
    AddToList(inner, OLD_LIST, next_group);
    SetBit(inner, other_held);
    AddPair(inner, other_held, joins_inside, 0.f);
    on_valid = [=](void* asked, void* soldier) {
      if (asked != manager || soldier != joins_inside || At<uint8_t>(joins_inside, SOLDIER_VALID) != 0
          || At<uint32_t>(inner, MANAGER_PAIR_COUNT) == 0) {
        return;
      }
      // The inner call's own question about the soldier gets a yes, the outer call's a no.
      At<uint8_t>(joins_inside, SOLDIER_VALID) = 1;
      Resolve(inner);
      At<uint8_t>(joins_inside, SOLDIER_VALID) = 0;
    };
    expect_reason(manager, REASON_NESTED, 1, "nested: an entry during a candidate's join test, the game's loop behind it");
    on_valid = nullptr;
  }

  // An entry while a call runs: the inner one runs the game's function, the outer one the game's loop for its rest.
  // The inner entry tells the outer one also when it fails an earlier test: the switch off, or another manager.
  // In the last variant the inner manager lists the outer one's group and joins a soldier to it, which the outer
  // call's index cannot know: its later pair with that soldier is right only through the game's loop. In the one after
  // it, the entry comes in that later pair's own test.
  for (const uint32_t variant : {0u, 1u, 2u, 3u, 4u}) {
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    void* group = AddOldGroup(manager, {held});
    void* entry_pair = AddPair(manager, held, NewSoldier(12), 0.f);
    AddPair(manager, held, NewSoldier(13), 1.f);
    AddPair(manager, NewSoldier(14), NewSoldier(15), 2.f);
    void* inner = NewScene((variant == 2) ? other_manager_table.data() : manager_table.data());
    if (variant >= 3) {
      void* joins_inside = NewSoldier(16);
      AddToList(inner, OLD_LIST, group);
      SetBit(inner, held);
      AddPair(inner, held, joins_inside, 0.f);
      void* later_pair = AddPair(manager, joins_inside, NewSoldier(17), 3.f);
      if (variant == 4) {
        entry_pair = later_pair;
      }
    } else {
      void* inner_held = NewSoldier(10);
      AddOldGroup(inner, {inner_held});
      AddPair(inner, inner_held, NewSoldier(12), 0.f);
    }
    on_pair_test = [=](void*, void* pair) {
      if (pair != entry_pair) return;
      if (variant == 1) {
        block.core_wanted.store(0);
      }
      Resolve(inner);
      block.core_wanted.store(CORE_STEP);
    };
    const Outcome hooked = ExpectSame(manager, "nested: an entry while a call runs");
    Expect(hooked.reasons == (1u << REASON_NESTED) && hooked.handled == 1 && !state.nested.load() && !state.busy.load()
               && LinesWith("renodx_attila64: battle step: partly (nested)") == 1,
           "nested: the outer call takes the game's loop, whatever test the inner one failed");
    on_pair_test = nullptr;
  }
  {
    // After an entry the index is not read again, also where it still names a group: the pair's soldier is in the
    // second group for the index, and since the entry (another manager that lists the first group) in the first one
    // too, which the game's loop asks first.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    void* in_second = NewSoldier(11);
    void* first_group = AddOldGroup(manager, {held});
    AddOldGroup(manager, {in_second});
    void* entry_pair = AddPair(manager, NewSoldier(14), NewSoldier(15), 0.f);
    AddPair(manager, in_second, NewSoldier(17), 1.f);
    void* inner = NewScene();
    AddToList(inner, OLD_LIST, first_group);
    SetBit(inner, held);
    AddPair(inner, held, in_second, 0.f);
    on_pair_test = [=](void*, void* pair) {
      if (pair == entry_pair) {
        Resolve(inner);
      }
    };
    expect_reason(manager, REASON_NESTED, 1, "nested: the index is not read after an entry, also where it names a group");
    on_pair_test = nullptr;
  }
  {
    // A second thread found the row busy: it runs the game's function and leaves its mark; the next call takes the
    // game's loop once.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    AddOldGroup(manager, {held});
    AddPair(manager, held, NewSoldier(12), 0.f);
    KeepWorld();
    const Outcome game = Run(manager, games[0], false);
    std::memcpy(arena, world.data(), world.size());
    calls.clear();
    state.busy.store(true);
    const bool refused = !ResolvePairs(manager, &block, api);
    state.busy.store(false);
    Expect(refused && state.nested.load() && calls.empty() && std::memcmp(arena, world.data(), world.size()) == 0,
           "busy: an entry while the row is busy does nothing but leave its mark");
    const Outcome hooked = Run(manager, games[0], true, false);
    Expect(Same(game, hooked) && hooked.handled == 1 && hooked.reasons == (1u << REASON_NESTED) && !state.nested.load(),
           "busy: a stale mark costs the next call the game's loop, once");
    const Outcome after = Run(manager, games[0], true);
    Expect(Same(game, after) && after.reasons == 0, "busy: the call after that is the index's again");
  }

  {
    // More candidates than the row sorts: that pair asks every group of the list, the index stays in use and takes
    // the join in (the next pair finds the soldier that joined).
    ClearArena();
    void* manager = NewScene();
    void* in_many = NewSoldier(10);
    for (uint32_t group = 0; group < CANDIDATE_LIMIT + 4; ++group) {
      AddOldGroup(manager, {in_many}, ((group < 3) ? 1 : MEMBER_LIMIT));
    }
    void* joins = NewSoldier(12);
    AddPair(manager, in_many, joins, 0.f);
    AddPair(manager, joins, NewSoldier(13), 1.f);
    const Outcome hooked = ExpectSame(manager, "many candidates: every group of the list is asked");
    Expect(hooked.reasons == 0 && hooked.handled == 1 && !state.partly, "many candidates: no reason, the index stays in use");
  }
  {
    // The generation counter wraps: the entries are cleared, so one left from the first generation is not read.
    // The group is full: with the old entry read, the pair would go on to that entry's place.
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    AddOldGroup(manager, {held}, 1);
    AddPair(manager, NewSoldier(11), held, 0.f);
    state.generation = UINT32_MAX;
    index.entries[11] = {.generation = 1, .head = NODE_CAPACITY - 1};
    index.nodes[NODE_CAPACITY - 1] = {.next = NO_NODE, .where = (OLD_LIST << WHERE_LIST_SHIFT) | 40};
    const Outcome hooked = ExpectSame(manager, "generation: a wrap clears the entries");
    Expect(state.generation == 1 && hooked.reasons == 0 && hooked.handled == 1, "generation: starts again at 1");
  }

  // The switch, the code check's flag, the manager and the pair count: false, with nothing read for writing.
  {
    ClearArena();
    void* manager = NewScene();
    void* held = NewSoldier(10);
    AddOldGroup(manager, {held});
    AddPair(manager, held, NewSoldier(12), 0.f);
    void* no_pairs = NewScene();
    AddOldGroup(no_pairs, {NewSoldier(10)});
    void* other_manager = NewScene(other_manager_table.data());
    AddPair(other_manager, NewSoldier(10), NewSoldier(12), 0.f);
    KeepWorld();
    calls.clear();
    const uint32_t other_lanes = 0x5a5b0000 | (uint32_t{ENGINE_PARTLY} << ENGINE_SPLIT_SHIFT);
    const auto lane_is = [&](EngineState lane) {
      return block.engine_status.load() == (other_lanes | (uint32_t{lane} << ENGINE_STEP_SHIFT));
    };
    const auto untouched = [&] { return calls.empty() && std::memcmp(arena, world.data(), world.size()) == 0; };
    block.engine_status.store(other_lanes | (uint32_t{ENGINE_NOT_USED_YET} << ENGINE_STEP_SHIFT));
    Expect(!ResolvePairs(no_pairs, &block, api) && !ResolvePairs(other_manager, &block, api) && untouched()
               && lane_is(ENGINE_NOT_USED_YET),
           "no pairs, or another manager: the game's function, the lane as it was");
    state.code_read.store(false);
    Expect(!ResolvePairs(manager, &block, api) && untouched() && lane_is(ENGINE_NOT_USED_YET),
           "the game's code not as read: always the game's function");
    state.code_read.store(true);
    block.core_wanted.store(CORE_BATTLE | CORE_RENDER | CORE_SPLIT);
    Expect(!ResolvePairs(manager, &block, api) && untouched() && lane_is(ENGINE_OFF),
           "the row's bit clear: the game's function, the lane off, the other lanes kept");
    block.core_wanted.store(CORE_STEP);
    Expect(!ResolvePairs(no_pairs, &block, api) && untouched() && lane_is(ENGINE_NOT_USED_YET),
           "the row's bit set again: the lane leaves off at the next call");

    // The lane and its line: active at the first call the index does alone, with that call's sizes, once.
    const Outcome first = Run(manager, games[0], true);
    Expect(first.handled == 1 && lane_is(ENGINE_ACTIVE)
               && LinesWith("renodx_attila64: battle step: active (new 0, active 1, pairs 1)") == 1 && step_lines.size() == 1,
           "lane: active with its line at the first call done by the index");
    const Outcome second = Run(manager, games[0], true, false);
    Expect(second.handled == 1 && lane_is(ENGINE_ACTIVE) && step_lines.size() == 1, "lane: a second such call logs nothing");
    block.core_wanted.store(0);
    Expect(!ResolvePairs(manager, &block, api) && lane_is(ENGINE_OFF), "lane: off again with the bit clear");
    block.core_wanted.store(CORE_STEP);
    Expect(Run(manager, games[0], true, false).handled == 1 && lane_is(ENGINE_ACTIVE) && step_lines.size() == 1,
           "lane: active again without a second line");
    // Partly is sticky: a later call the index does alone does not take it back.
    state.busy.store(true);
    Expect(!ResolvePairs(manager, &block, api), "lane: an entry while busy");
    state.busy.store(false);
    Expect(Run(manager, games[0], true, false).handled == 1 && lane_is(ENGINE_PARTLY), "lane: partly after a reason");
    Expect(Run(manager, games[0], true, false).handled == 1 && lane_is(ENGINE_PARTLY) && step_lines.size() == 2,
           "lane: partly stays, with one line for its reason");
    Expect(std::ranges::all_of(step_lines, [](const std::string& line) { return line.size() < 1024; }),
           "lines: each under 1 KB (the loader's log allocates above that)");
    block.engine_status.store(0);
  }
  std::printf("  %s: %d failure(s)\n", label, failures - failures_before);
}

// mov rax, target; jmp rax
void WriteJump(uintptr_t address, void* target) {
  auto* code = reinterpret_cast<uint8_t*>(address);
  code[0] = 0x48;
  code[1] = 0xb8;
  std::memcpy(code + 2, &target, sizeof(target));
  code[10] = 0xff;
  code[11] = 0xe0;
}

// The contact split row (ContactTick, CheckContactCode) against the game's contact pass. Both run on the same fake
// world in the game's layout (a manager with its environment, battle model, core, database and table; collision
// objects whose cell entries are their nodes in the grid cells' chains) and must make the same owner calls in the same
// order and leave the same memory. The game's pass is the game's own code, so these cases need the game file.
// - Every fake object's table has the logging fake owner at +0x98, so a walk's one indirect call (0x8045e8) never
//   enters game code: no case reaches 0x7856f0, and with it 0x785905 / 0x785914 / 0x785e60 / 0x272c070. The fake
//   game table is therefore only a non-null word; nothing reads it.
// - Shapes are null: 0x3002e20 returns 0 for two null shapes (0x3002e4b..0x300302e) and changes nothing.
// - The id vector's memory is left out of the comparison: the game pushes every visited id, the row only the kept
//   ones, and the count is 0 again after every object.

// The test's own fields of a fake object, in bytes the game's code here does not touch.
inline constexpr size_t OBJECT_ACTION = 0x120;  // what the fake owner does when this object is the walking one
inline constexpr size_t OBJECT_TARGET = 0x128;
inline constexpr size_t OBJECT_VALUE = 0x130;
inline constexpr size_t OBJECT_BYTES = 0x140;
inline constexpr size_t ID_SET_BYTES = 0x2020;
inline constexpr uintptr_t CONTACT_LOOP = 0x5df300;
enum OwnerAction : uint32_t {
  ACTION_NONE,
  ACTION_FENCE_PUSH,        // class H's path: the fence count goes up and the walking object's flags lose two bits
  ACTION_FENCE_APPEARS,     // the model's fence manager link turns from null to set
  ACTION_DROP_ENVIRONMENT,  // the walking object's environment link turns null
  ACTION_DROP_MODEL,        // the environment's model link turns null
  ACTION_MOVE_OTHER,        // game code that moves another object: the row's reasoning says it never happens
  ACTION_FENCE_PUSH_AND_MOVE,  // class H's path with a write the reasoning does not cover: what the fence is for
  ACTION_SWAP_LISTED,       // once: the object list's entry at OBJECT_VALUE changes places with the next
};

struct ContactScene {
  void* manager = nullptr;
  void* environment = nullptr;
  void* model = nullptr;
  void* core = nullptr;
  void* database = nullptr;
  void* fence = nullptr;
  void* task_manager = nullptr;
  size_t id_data = 0;  // the id vector's first memory: its place in the arena and its bytes
  size_t id_data_bytes = 0;
};
ContactScene scene;
uint8_t* const id_set_memory = reinterpret_cast<uint8_t*>(GAME_ID_SET);
std::vector<uint8_t> id_set_world;

struct OwnerCall {
  uintptr_t owner = 0;
  uintptr_t walking = 0;
  bool operator==(const OwnerCall&) const = default;
};
std::vector<OwnerCall> owner_calls;
ATTILA64_GAME_CALL void FakeOwner(void* owner, void* walking) noexcept {
  owner_calls.push_back({reinterpret_cast<uintptr_t>(owner), reinterpret_cast<uintptr_t>(walking)});
  switch (At<uint32_t>(walking, OBJECT_ACTION)) {
    case ACTION_FENCE_PUSH:
      ++At<uint32_t>(scene.fence, FENCE_COUNT);
      At<uint8_t>(walking, 0x60) &= 0xfc;
      break;
    case ACTION_FENCE_APPEARS:
      At<void*>(scene.model, MODEL_FENCE_MANAGER) = scene.fence;
      break;
    case ACTION_DROP_ENVIRONMENT:
      At<void*>(walking, OBJECT_ENVIRONMENT) = nullptr;
      break;
    case ACTION_DROP_MODEL:
      At<void*>(scene.environment, ENVIRONMENT_MODEL) = nullptr;
      break;
    case ACTION_FENCE_PUSH_AND_MOVE:
      ++At<uint32_t>(scene.fence, FENCE_COUNT);
      [[fallthrough]];
    case ACTION_MOVE_OTHER:
      At<float>(At<void*>(walking, OBJECT_TARGET), OBJECT_X) = At<float>(walking, OBJECT_VALUE);
      break;
    case ACTION_SWAP_LISTED: {
      void** listed = At<void**>(scene.manager, CONTACT_OBJECTS) + static_cast<uint32_t>(At<float>(walking, OBJECT_VALUE));
      std::swap(listed[0], listed[1]);
      At<uint32_t>(walking, OBJECT_ACTION) = ACTION_NONE;
      break;
    }
    default:
      break;
  }
}

// The fake objects' tables, each entered in the row's class table with the mode a case wants (the unknown one is not).
// The two fast ones lie in one pool: the second is picked so that both hash to one slot of the class table.
inline constexpr size_t TABLE_OWNER_CALL = 0x98 / sizeof(void*);
std::array<void*, 4096> fast_table_pool = {};
std::array<void**, 2> fast_tables = {};
std::array<void*, 64> fenced_table = {};
std::array<void*, 64> naval_table = {};
std::array<void*, 64> unknown_table = {};
void EnterClass(void** table, ContactMode mode) {
  table[TABLE_OWNER_CALL] = reinterpret_cast<void*>(&FakeOwner);
  uint8_t slot = ContactClassSlot(reinterpret_cast<uintptr_t>(table));
  while (contact.classes[slot].vptr != 0) {
    ++slot;
  }
  contact.classes[slot] = {.vptr = reinterpret_cast<uintptr_t>(table), .mode = mode};
}

// The row's seam for starting a region's helpers: the test's own threads.
uint32_t test_helpers = 0;
uint32_t regions = 0;  // the seam's calls
bool queue_lock_fails = false;
std::function<void()> on_region;  // runs with the region open, before any helper
std::vector<std::thread> helper_threads;
bool TestEnqueue(uint32_t helper_count) noexcept {
  ++regions;
  if (queue_lock_fails) return false;
  if (on_region) {
    on_region();
  }
  for (uint32_t helper = 0; helper < std::min(helper_count, test_helpers); ++helper) {
    helper_threads.emplace_back([helper] { RunRegionShare(helper); });
  }
  return true;
}

// A new world without objects: the chain a tick reads (manager -> environment -> model with its tick and fence
// manager; -> core -> database -> table), the task manager behind the game's getter, the game's statics as after the
// pass's first call, and an id vector of `id_capacity`.
void NewContactScene(uint32_t object_capacity, uint32_t id_capacity) {
  ClearArena();
  scene = {.manager = Take(0x408),
           .environment = Take(0x70),
           .model = Take(0x1e0),
           .core = Take(0x28),
           .database = Take(0x21a0),
           .fence = Take(0x50),
           .task_manager = Take(0x50)};
  At<void*>(scene.manager, CONTACT_ENVIRONMENT) = scene.environment;
  At<void*>(scene.manager, CONTACT_OBJECTS) = Take(object_capacity * sizeof(void*));
  At<void*>(scene.environment, ENVIRONMENT_CORE) = scene.core;
  At<void*>(scene.environment, ENVIRONMENT_MODEL) = scene.model;
  At<uint32_t>(scene.model, MODEL_TICK) = 101;
  At<void*>(scene.model, MODEL_FENCE_MANAGER) = scene.fence;
  At<void*>(scene.core, CORE_DATABASE) = scene.database;
  At<void*>(scene.database, DATABASE_TABLE) = Take(16);
  At<uint32_t>(scene.task_manager, TASK_MANAGER_THREADS) = 8;
  // The getter's own variable: mov rbx, [rip + rel32] at 0x3061e64.
  int32_t variable_offset = 0;
  std::memcpy(&variable_offset, reinterpret_cast<const void*>(TASK_MANAGER_GETTER + 7), sizeof(variable_offset));
  *reinterpret_cast<void**>(TASK_MANAGER_GETTER + 11 + variable_offset) = scene.task_manager;
  *reinterpret_cast<uint8_t*>(TASK_FLAG) = 1;
  *reinterpret_cast<float*>(CONTACT_RADIUS_ADDITION) = 0.f;
  std::memset(id_set_memory, 0, ID_SET_BYTES);
  At<uint8_t>(id_set_memory, ID_SET_GUARD) = 1;
  At<uint32_t>(id_set_memory, ID_SET_CAPACITY) = id_capacity;
  scene.id_data_bytes = id_capacity * sizeof(uint16_t);
  if (id_capacity != 0) {
    void* id_data = Take(scene.id_data_bytes);
    At<void*>(id_set_memory, ID_SET_DATA) = id_data;
    scene.id_data = static_cast<size_t>(static_cast<uint8_t*>(id_data) - arena);
  }
}
// An enabled object at the list's end, of a class and with room for `cell_capacity` cell entries: flags, masks,
// heights and the response's factors such that any neighbour of the same make within reach passes every filter.
void* NewObject(void** table, uint32_t id, float x, float z, uint32_t cell_capacity) {
  void* object = Take(OBJECT_BYTES);
  At<void**>(object, 0) = table;
  At<void*>(object, OBJECT_ENVIRONMENT) = scene.environment;
  At<uint32_t>(object, 0x54) = UINT32_MAX;
  At<uint8_t>(object, 0x58) = 1;
  At<uint32_t>(object, 0x60) = 0x80;
  At<uint32_t>(object, 0x68) = id;
  At<float>(object, OBJECT_X) = x;
  At<float>(object, OBJECT_Z) = z;
  At<float>(object, OBJECT_HEIGHT) = 2.f;
  At<float>(object, OBJECT_RADIUS) = 0.5f;
  for (const size_t factor : {0x9c, 0xa0, 0xa4, 0xa8}) {
    At<float>(object, factor) = 1.f;
  }
  At<void*>(object, OBJECT_CELLS) = Take(cell_capacity * CELL_BYTES);
  At<void**>(scene.manager, CONTACT_OBJECTS)[At<uint32_t>(scene.manager, CONTACT_OBJECT_COUNT)] = object;
  ++At<uint32_t>(scene.manager, CONTACT_OBJECT_COUNT);
  return object;
}
// The object's next cell entry, at the head of a grid cell's chain: the entry is the object's node there.
void AddToCell(void* object, void** head, uint32_t kind) {
  void* entry = At<uint8_t*>(object, OBJECT_CELLS) + (At<uint32_t>(object, OBJECT_CELL_COUNT) * CELL_BYTES);
  At<uint32_t>(entry, 0) = kind;
  At<uint32_t>(entry, 4) = At<uint32_t>(object, 0x68);
  At<void*>(entry, 8) = static_cast<uint8_t*>(object) + 0x58;
  At<void*>(entry, 0x10) = object;
  At<void*>(entry, 0x20) = *head;
  At<void**>(entry, CELL_HEAD) = head;
  *head = entry;
  ++At<uint32_t>(object, OBJECT_CELL_COUNT);
}
void** NewHead() { return static_cast<void**>(Take(sizeof(void*))); }
// `count` objects of the first fast class, each alone in its own grid cell, far from every other.
std::vector<void*> NewPlainScene(uint32_t count, uint32_t id_capacity = 8192) {
  NewContactScene(count + 8, id_capacity);
  std::vector<void*> objects;
  for (uint32_t object = 0; object < count; ++object) {
    objects.push_back(NewObject(fast_tables[0], object, 10.f * static_cast<float>(object), 0.f, 4));
    AddToCell(objects.back(), NewHead(), 8);
  }
  return objects;
}
// `other` moves next to `object` and into its grid cell: the earlier of the two in the list finds the other, every
// filter passes, the response is written and the other's table is called.
void MakeNeighbours(void* object, void* other) {
  At<float>(other, OBJECT_X) = At<float>(object, OBJECT_X) + 0.25f;
  AddToCell(other, At<void**>(At<void*>(object, OBJECT_CELLS), CELL_HEAD), 8);
}

void KeepContactWorld() {
  KeepWorld();
  id_set_world.assign(id_set_memory, id_set_memory + ID_SET_BYTES);
}
void RestoreContactWorld() {
  std::memcpy(arena, world.data(), world.size());
  std::memset(arena + world.size(), 0, arena_dirty - world.size());
  arena_used = world.size();
  arena_dirty = world.size();
  std::memcpy(id_set_memory, id_set_world.data(), ID_SET_BYTES);
}
// The row's own state as at the game's start, with the row on.
void FreshContact() {
  step_lines.clear();
  contact.reasons_logged.store(0);
  contact.original_ticks.store(0);
  contact.busy.store(false);
  contact.table_off = false;
  contact.skip_period = NO_PERIOD;
  contact.handled_ticks = 0;
  contact.fence_ticks = 0;
  contact.first_fence_tick = 0;
  contact.first_fence_index = 0;
  contact.region_time = 0;
  contact.longest_region = 0;
  contact.join_wait = 0;
  contact.own_chunks = 0;
  contact.chunks = 0;
  contact.visited = 0;
  contact.listed = 0;
  contact.plain_objects = 0;
  contact.objects_seen = 0;
  block.core_wanted.store(CORE_SPLIT);
  block.engine_status.store(uint32_t{ENGINE_NOT_USED_YET} << ENGINE_SPLIT_SHIFT);
}
bool LaneIs(EngineState lane) { return block.engine_status.load() == (uint32_t{lane} << ENGINE_SPLIT_SHIFT); }

// One tick on the world as it was kept: the owner calls it made and the memory after it.
struct ContactOutcome {
  bool handled = false;  // by the row; otherwise the game's function ran
  uint32_t regions = 0;
  std::vector<OwnerCall> calls;
  std::vector<uint8_t> memory;  // the arena, then the game's id set
};
// `with_row`: through the row as the hook will call it (the game's function on a false). `fresh`: the row's state as
// at the game's start. `threads`: the helpers a region gets.
ContactOutcome RunContact(bool with_row, uint32_t threads = 0, bool fresh = true) {
  RestoreContactWorld();
  owner_calls.clear();
  game_memory.clear();
  note_game_memory = true;
  regions = 0;
  test_helpers = threads;
  if (fresh) {
    FreshContact();
  }
  const bool handled = with_row && ContactTick(scene.manager, &block, api);
  for (std::thread& helper : helper_threads) {
    helper.join();
  }
  helper_threads.clear();
  if (!handled) {
    reinterpret_cast<PairsFn>(CONTACT_LOOP)(scene.manager);
  }
  note_game_memory = false;
  ContactOutcome outcome = {.handled = handled,
                            .regions = regions,
                            .calls = owner_calls,
                            .memory = std::vector<uint8_t>(arena, arena + arena_dirty)};
  std::fill_n(outcome.memory.begin() + scene.id_data, scene.id_data_bytes, uint8_t{0});
  for (const auto& [offset, bytes] : game_memory) {
    std::fill_n(outcome.memory.begin() + offset, bytes, uint8_t{0});
  }
  outcome.memory.insert(outcome.memory.end(), id_set_memory, id_set_memory + ID_SET_BYTES);
  return outcome;
}
bool Same(const ContactOutcome& left, const ContactOutcome& right) {
  return left.calls == right.calls && left.memory == right.memory;
}
// The world as built now through the game's pass, then through the row with the step's thread alone and with three
// helpers: all the same. Returns the run with the step's thread alone.
ContactOutcome ExpectContactSame(const char* what) {
  KeepContactWorld();
  const ContactOutcome game = RunContact(false);
  const ContactOutcome threaded = RunContact(true, 3);
  ContactOutcome alone = RunContact(true);
  Expect(Same(game, alone) && Same(game, threaded) && alone.handled == threaded.handled, what);
  return alone;
}
// A tick the row must refuse before it touches anything: false, no region, the arena and the id set as they were.
bool Refused() {
  RestoreContactWorld();
  regions = 0;
  const bool handled = ContactTick(scene.manager, &block, api);
  return !handled && regions == 0 && std::memcmp(arena, world.data(), world.size()) == 0
         && std::memcmp(id_set_memory, id_set_world.data(), ID_SET_BYTES) == 0;
}
// The kept world's tick: the next runs are that tick of the same battle.
void KeepTick(uint32_t tick) {
  RestoreContactWorld();
  At<uint32_t>(scene.model, MODEL_TICK) = tick;
  KeepContactWorld();
}

struct ContactShape {
  uint32_t objects = 0;
  uint32_t special_percent = 0;  // values 0, negative, NaN, infinite and huge in the geometry
  uint32_t boundary_percent = 0;  // objects placed at reach +- 1 ulp of the one before
  uint32_t fenced_per_mille = 0;
  uint32_t big_objects = 0;  // with more cells than the row swaps
  uint32_t id_capacity = 8192;
  float radius_addition = 0.f;
};
// A random world: objects on a grid of unit cells, about two a cell, each in up to six cells around its own; every
// byte the game's filters read takes each of its values somewhere.
void BuildContactWorld(std::mt19937_64* random, const ContactShape& shape) {
  const auto below = [&](uint32_t limit) { return static_cast<uint32_t>((*random)() % limit); };
  const auto chance = [&](uint32_t percent) { return below(100) < percent; };
  const auto unit = [&] { return static_cast<float>((*random)() >> 40) * 0x1p-24f; };
  const auto special = [&](float normal) {
    if (!chance(shape.special_percent)) return normal;
    return std::array<float, 8>{0.f,   -normal, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                -0.f, 1e30f,   -std::numeric_limits<float>::infinity(), 1e-30f}[below(8)];
  };
  NewContactScene(shape.objects + 8, shape.id_capacity);
  *reinterpret_cast<float*>(CONTACT_RADIUS_ADDITION) = shape.radius_addition;
  const uint32_t side = std::max<uint32_t>(1, static_cast<uint32_t>(std::sqrt(static_cast<float>(shape.objects) / 2.f)));
  std::vector<void**> heads(size_t{side} * side);
  for (void**& head : heads) {
    head = NewHead();
  }
  const std::array<void*, 3> groups = {nullptr, reinterpret_cast<void*>(uintptr_t{0x10}), reinterpret_cast<void*>(uintptr_t{0x20})};
  void* previous = nullptr;
  for (uint32_t index = 0; index < shape.objects; ++index) {
    const bool big = index < shape.big_objects;
    void** table = fast_tables[below(2)];
    const bool fenced = below(1000) < shape.fenced_per_mille;
    if (fenced) {
      table = fenced_table.data();
    }
    float x = unit() * static_cast<float>(side);
    float z = unit() * static_cast<float>(side);
    const float radius = 0.15f + (unit() * 0.45f);
    const bool at_boundary = previous != nullptr && chance(shape.boundary_percent);
    if (at_boundary) {
      // Reach as the game adds it (with the addition on neither, one or both), then one float up or down.
      const float reach = radius + At<float>(previous, OBJECT_RADIUS) + (static_cast<float>(below(3)) * shape.radius_addition);
      x = At<float>(previous, OBJECT_X) + reach;
      z = At<float>(previous, OBJECT_Z);
      x = std::array<float, 3>{x, std::nextafter(x, 0.f), std::nextafter(x, 1e30f)}[below(3)];
    }
    // Ids are 16 bits in the id vector; a few alias another id's bit.
    void* object = NewObject(table, index + (chance(2) ? 0x10000 : 0), special(x), special(z), (big ? 70 : 6));
    At<uint32_t>(object, 0x54) = (chance(10) ? below(shape.objects) : UINT32_MAX);
    At<uint8_t>(object, 0x58) = (chance(95) ? 1 : 0);
    At<uint8_t>(object, 0x59) = (chance(5) ? 1 : 0);
    At<uint8_t>(object, 0x5a) = (chance(10) ? 1 : 0);
    At<uint8_t>(object, 0x5b) = (chance(20) ? 1 : 0);
    uint32_t flags = (chance(60) ? 0x80 : 0);
    for (const uint32_t flag : {0x1u, 0x2u, 0x4u, 0x20u, 0x200u, 0x8000u, 0x10000u}) {
      flags |= (chance(30) ? flag : 0);
    }
    At<uint32_t>(object, 0x60) = (chance(4) ? 0 : flags);
    At<uint32_t>(object, 0x64) = (chance(80) ? 0 : std::array<uint32_t, 6>{0x1, 0x80, 0x800, 0x2000, 0x4000, 0x801}[below(6)]);
    At<void*>(object, 0x70) = groups[below(3)];
    At<float>(object, OBJECT_Y) = special(unit());
    At<float>(object, OBJECT_HEIGHT) = special(0.5f + (unit() * 2.5f));
    At<float>(object, OBJECT_RADIUS) = special(radius);
    At<uint16_t>(object, 0x98) = static_cast<uint16_t>(below(16));
    for (const size_t factor : {0x9c, 0xa0, 0xa4, 0xa8}) {
      At<float>(object, factor) = 0.5f + (unit() * 1.5f);
    }
    At<uint32_t>(object, 0xc4) = below(4);
    At<float>(object, 0xc8) = unit();
    At<float>(object, 0xd0) = unit();
    At<uint32_t>(object, 0xe4) = below(3);
    if (fenced && chance(50)) {
      At<uint32_t>(object, OBJECT_ACTION) = ACTION_FENCE_PUSH;
    }
    const auto cell_of = [&](float place) {
      return std::min(side - 1, static_cast<uint32_t>(std::max(0.f, std::isfinite(place) ? place : 0.f)));
    };
    const uint32_t cell_count = (big ? 70 : below(7));
    for (uint32_t cell = 0; cell < cell_count; ++cell) {
      const uint32_t column = std::min(side - 1, cell_of(x) + ((cell == 0) ? 0 : below(2)));
      const uint32_t row = std::min(side - 1, cell_of(z) + ((cell == 0) ? 0 : below(2)));
      AddToCell(object, heads[(size_t{row} * side) + column], (chance(50) ? 8 : below(10)));
    }
    if (at_boundary && At<uint32_t>(object, OBJECT_CELL_COUNT) < 6 && At<uint32_t>(previous, OBJECT_CELL_COUNT) != 0) {
      AddToCell(object, At<void**>(At<void*>(previous, OBJECT_CELLS), CELL_HEAD), 8);
    }
    previous = object;
  }
}

// T-B1 and T-B4's ticks: the game file is mapped, its allocator sent to the test's.
void RunContactCases() {
  std::printf("  contact split: the game's pass against the row\n");
  const int failures_before = failures;
  contact.code_read.store(true);
  contact.enqueue = &TestEnqueue;
  const auto active_line = [](uint32_t objects, uint32_t plain) {
    char line[96];
    std::snprintf(line, sizeof(line), "renodx_attila64: contact split: active (objects %u, plain %u, listed ", objects, plain);
    return LinesWith(line);
  };
  const auto cells_untouched = [] {
    bool untouched = contact_cells.none == nullptr && contact_cells.entries[0].head == &contact_cells.head;
    for (size_t entry = 0; entry < contact_cells.entries.size(); ++entry) {
      untouched = untouched && contact_cells.entries[entry].zero == decltype(ContactCell::zero){}
                  && (entry == 0 || contact_cells.entries[entry].head == &contact_cells.none);
    }
    return untouched;
  };

  // Random worlds: under the row's object minimum (the game's function), then big enough for a region; plain
  // geometry, special values, the reach boundary with and without the radius addition, class H objects, objects
  // with more cells than the row swaps, an id vector the walks outgrow.
  {
    std::mt19937_64 random(0x636f6e74);
    const auto below = [&](uint32_t limit) { return static_cast<uint32_t>(random() % limit); };
    uint32_t handled = 0;
    uint32_t with_calls = 0;
    uint32_t fence_worlds = 0;
    uint64_t listed = 0;
    uint64_t visited = 0;
    constexpr uint32_t WORLDS = 150;
    for (uint32_t world_index = 0; world_index < WORLDS; ++world_index) {
      ContactShape shape = {.objects = ((world_index < 20) ? below(MIN_OBJECTS) : MIN_OBJECTS + below(1000))};
      switch (world_index % 5) {
        case 1:
          shape.special_percent = 3;
          break;
        case 2:
          shape.boundary_percent = 30;
          shape.radius_addition = ((below(2) == 0) ? 0.f : 0.1f);
          break;
        case 3:
          shape.fenced_per_mille = 10;
          shape.big_objects = below(3);
          break;
        case 4:
          shape.id_capacity = std::array<uint32_t, 4>{0, 1, 8, 64}[below(4)];
          shape.special_percent = 1;
          shape.boundary_percent = 5;
          shape.radius_addition = 0.05f;
          break;
        default:
          break;
      }
      BuildContactWorld(&random, shape);
      KeepContactWorld();
      const ContactOutcome game = RunContact(false);
      const ContactOutcome threaded = RunContact(true, 3);
      const ContactOutcome alone = RunContact(true);
      ++checks;
      if (!Same(game, alone) || !Same(game, threaded)) {
        ++failures;
        std::printf("FAIL: contact world %u: the row differs from the game's pass (%s)\n", world_index,
                    (Same(game, alone) ? "with helpers" : "alone"));
      }
      handled += (alone.handled ? 1 : 0);
      with_calls += (game.calls.empty() ? 0 : 1);
      fence_worlds += contact.fence_ticks;
      listed += contact.listed;
      visited += contact.visited;
    }
    std::printf("    %u worlds: %u done by the row, %u with owner calls, %u with a fence fire; visited %llu, kept %llu\n", WORLDS,
                handled, with_calls, fence_worlds, visited, listed);
    Expect(handled * 2 > WORLDS && with_calls * 2 > WORLDS && fence_worlds != 0 && listed != 0 && listed < visited,
           "contact: the random worlds reach the row, the owner calls, the fence and a shorter list");
  }

  // The base case: neighbours every 40 objects. Done by the row, with its lane and its line once.
  const auto base_scene = [](uint32_t count = 2100) {
    std::vector<void*> objects = NewPlainScene(count);
    for (uint32_t object = 0; object + 1 < count; object += 40) {
      MakeNeighbours(objects[object], objects[object + 1]);
    }
    return objects;
  };
  {
    base_scene();
    const ContactOutcome alone = ExpectContactSame("contact base: neighbours pass the game's filters, the row does the tick");
    Expect(alone.handled && alone.regions == 1 && alone.calls.size() == 53 && LaneIs(ENGINE_ACTIVE) && active_line(2100, 0) == 1
               && step_lines.size() == 1 && LinesWith("listed 106, helpers 6)") == 1 && !contact.busy.load() && !contact.open,
           "contact base: active, one line with the tick's sizes, the row free again");
    Expect(cells_untouched(), "contact base: the swap's cell entries are zero but each head pointer, after a tick too");
    const ContactOutcome again = RunContact(true, 0, false);
    Expect(again.handled && step_lines.size() == 1, "contact base: a second tick logs nothing");
    // Control: detect drops one passing node (here in the seam, with the region open: the first helper's slot does
    // every chunk, then the first kept list is emptied).
    on_region = [] {
      RunRegionShare(0);
      for (ContactItem& item : contact_items) {
        if (item.node_count != 0) {
          item.node_count = 0;
          break;
        }
      }
    };
    const ContactOutcome game = RunContact(false);
    Expect(!Same(game, RunContact(true)), "control: a kept list without a passing node is found");
    on_region = nullptr;
  }
  {
    // Control: game code that moves another object. The first object's owner call moves a far object next to its
    // cell neighbour, which the neighbour's kept list (made before) does not hold.
    std::vector<void*> objects = base_scene();
    AddToCell(objects[900], At<void**>(At<void*>(objects[800], OBJECT_CELLS), CELL_HEAD), 8);
    KeepContactWorld();
    Expect(Same(RunContact(false), RunContact(true)), "control base: two far objects in one cell");
    RestoreContactWorld();
    At<uint32_t>(objects[0], OBJECT_ACTION) = ACTION_MOVE_OTHER;
    At<void*>(objects[0], OBJECT_TARGET) = objects[900];
    At<float>(objects[0], OBJECT_VALUE) = At<float>(objects[800], OBJECT_X) + 0.25f;
    KeepContactWorld();
    const ContactOutcome game = RunContact(false);
    Expect(!Same(game, RunContact(true)), "control: game code that moves another object is found");
  }

  // A stopped region: the tick is the game's, with the reason's line once, the row's lane partly, the row free, and
  // no further region in the same 100 ticks. `stopped_at`: the chunk the stop is in, with the step's thread alone.
  const auto expect_stop = [&](const char* line, uint32_t stopped_chunks, const char* what) {
    KeepContactWorld();
    for (uint32_t place = CHUNK_OBJECTS * stopped_chunks; place < 2100; ++place) {
      contact_items[place].object = &scene;
    }
    const ContactOutcome game = RunContact(false);
    const ContactOutcome alone = RunContact(true);
    bool later_items_unwritten = contact.next_chunk.load() == stopped_chunks;
    for (uint32_t place = CHUNK_OBJECTS * stopped_chunks; place < 2100; ++place) {
      later_items_unwritten = later_items_unwritten && contact_items[place].object == &scene;
    }
    Expect(Same(game, alone) && !alone.handled && alone.regions == 1 && LinesWith(line) == 1 && step_lines.size() == 1
               && LaneIs(ENGINE_PARTLY) && !contact.busy.load() && !contact.table_off && contact.skip_period == 1,
           what);
    Expect(later_items_unwritten, "stop: the claiming ends with the chunk that found it, later items stay unwritten");
    const ContactOutcome threaded = RunContact(true, 3);
    Expect(Same(game, threaded) && !threaded.handled && LinesWith(line) == 1, "stop: the same with helpers");
    // The skip latch: the period's other ticks run no region; the re-sort tick is the game's as ever; the tick
    // after it tries again.
    KeepTick(150);
    const ContactOutcome skipped = RunContact(true, 0, false);
    Expect(Same(RunContact(false, 0, false), skipped) && !skipped.handled && skipped.regions == 0 && step_lines.size() == 1,
           "skip latch: no region in the rest of the period");
    KeepTick(200);
    Expect(Refused(), "skip latch: the re-sort tick is the game's");
    KeepTick(201);
    Expect(RunContact(true, 0, false).regions == 1 && step_lines.size() == 1, "skip latch: the tick after the re-sort tick tries again");
  };
  {
    std::vector<void*> objects = base_scene();
    At<void**>(objects[5], 0) = unknown_table.data();
    char line[96];
    std::snprintf(line, sizeof(line), "renodx_attila64: contact split: partly (unknown class: vtable 0x%llx)",
                  reinterpret_cast<uint64_t>(unknown_table.data()));
    expect_stop(line, 1, "unknown class: the game's tick, the line with the table's pointer");
  }
  {
    // The NAVAL class without a tail entry.
    std::vector<void*> objects = base_scene();
    At<void**>(objects[300], 0) = naval_table.data();
    expect_stop("renodx_attila64: contact split: partly (ships)", 3, "naval class: the game's tick");
  }
  {
    // An object of another environment, in the first chunk.
    std::vector<void*> objects = base_scene();
    At<void*>(objects[3], OBJECT_ENVIRONMENT) = Take(0x70);
    expect_stop("renodx_attila64: contact split: partly (environment differs)", 1, "another environment: the game's tick");
  }
  {
    // The node pool: every object in one cell, at one place, with flags the game's filters reject at once. Each walk
    // keeps 2099 nodes, so the 126th object's does not fit.
    NewContactScene(2108, 8192);
    void** head = NewHead();
    for (uint32_t object = 0; object < 2100; ++object) {
      void* made = NewObject(fast_tables[0], object, 0.f, 0.f, 1);
      At<uint32_t>(made, 0x60) = 1;
      AddToCell(made, head, 8);
    }
    expect_stop("renodx_attila64: contact split: partly (list pool)", 1, "list pool: the game's tick");
  }

  // The table test: a null table or link. The game's own walk would build the table (a file read), so the game's
  // function is not run here: the tick is refused untouched, the row is off until the game ends.
  for (const uint32_t null_link : {0u, 1u, 2u}) {
    base_scene();
    void** const links[] = {&At<void*>(scene.database, DATABASE_TABLE), &At<void*>(scene.core, CORE_DATABASE),
                            &At<void*>(scene.environment, ENVIRONMENT_CORE)};
    void* const link_value = *links[null_link];
    *links[null_link] = nullptr;
    KeepContactWorld();
    FreshContact();
    Expect(Refused() && contact.table_off && LaneIs(ENGINE_PARTLY) && !contact.busy.load() && contact.skip_period == NO_PERIOD
               && LinesWith("renodx_attila64: contact split: partly (game table not loaded)") == 1 && step_lines.size() == 1,
           "table test: a null table or link refuses the tick and switches the row off");
    // Sticky: with the table back, also after a re-sort tick; no exchange of `busy` (a set one stays, without the
    // nested line); the lane comes back to partly after the player's off and on.
    RestoreContactWorld();
    *links[null_link] = link_value;
    KeepContactWorld();
    const ContactOutcome game = RunContact(false, 0, false);
    const ContactOutcome later = RunContact(true, 0, false);
    Expect(Same(game, later) && !later.handled && later.regions == 0, "table test: sticky with the table back");
    KeepTick(201);
    const ContactOutcome next_period = RunContact(true, 0, false);
    Expect(!next_period.handled && next_period.regions == 0, "table test: sticky after a re-sort tick");
    contact.busy.store(true);
    Expect(Refused() && contact.busy.load(), "table test: an off row does not touch `busy`");
    contact.busy.store(false);
    block.core_wanted.store(0);
    Expect(Refused() && LaneIs(ENGINE_OFF), "table test: the switch off shows off");
    block.core_wanted.store(CORE_SPLIT);
    Expect(Refused() && LaneIs(ENGINE_PARTLY) && contact.table_off && step_lines.size() == 1,
           "table test: the switch on again shows partly, without a second line");
  }

  // The fence around a class H object (the 11th here, with a neighbour so that its owner call comes).
  for (const OwnerAction action :
       {ACTION_FENCE_PUSH, ACTION_NONE, ACTION_FENCE_APPEARS, ACTION_DROP_ENVIRONMENT, ACTION_DROP_MODEL}) {
    std::vector<void*> objects = base_scene();
    At<void**>(objects[10], 0) = fenced_table.data();
    MakeNeighbours(objects[10], objects[11]);
    At<uint32_t>(objects[10], OBJECT_ACTION) = action;
    if (action == ACTION_NONE || action == ACTION_FENCE_APPEARS) {
      At<void*>(scene.model, MODEL_FENCE_MANAGER) = nullptr;
    }
    const ContactOutcome alone = ExpectContactSame("fence: a class H object, the tick as the game's");
    const bool fires = action != ACTION_NONE;
    Expect(alone.handled && contact.fence_ticks == (fires ? 1u : 0u) && contact.first_fence_tick == (fires ? 101u : 0u)
               && contact.first_fence_index == (fires ? 10u : 0u) && active_line(2100, 1) == 1 && LaneIs(ENGINE_ACTIVE),
           "fence: fires on a count or a link that changed, not on a link that stays null");
  }
  {
    // Class H's path with a write the reasoning does not cover (it moves a far object next to its cell neighbour):
    // the fence fires, and every later object takes the game's walk, so the tick is still the game's.
    std::vector<void*> objects = base_scene();
    At<void**>(objects[10], 0) = fenced_table.data();
    MakeNeighbours(objects[10], objects[11]);
    AddToCell(objects[900], At<void**>(At<void*>(objects[800], OBJECT_CELLS), CELL_HEAD), 8);
    At<uint32_t>(objects[10], OBJECT_ACTION) = ACTION_FENCE_PUSH_AND_MOVE;
    At<void*>(objects[10], OBJECT_TARGET) = objects[900];
    At<float>(objects[10], OBJECT_VALUE) = At<float>(objects[800], OBJECT_X) + 0.25f;
    const ContactOutcome alone = ExpectContactSame("fence: what class H's path wrote is seen by the later walks");
    Expect(alone.handled && contact.fence_ticks == 1 && alone.calls.size() == 56, "fence: the moved object's new neighbour answers");
  }
  {
    // Game code that changes the object list under the pass (the reasoning says none does): from the first place
    // that holds another object than detect saw, the game's walk.
    std::vector<void*> objects = base_scene();
    MakeNeighbours(objects[1500], objects[1700]);
    At<uint32_t>(objects[0], OBJECT_ACTION) = ACTION_SWAP_LISTED;
    At<float>(objects[0], OBJECT_VALUE) = 1500.f;
    const ContactOutcome alone = ExpectContactSame("a changed object list: the game's walk from there on");
    Expect(alone.handled && alone.calls.size() == 54, "a changed object list: the row's tick, the moved object's neighbour found");
  }
  {
    // Walks that visit more ids than the visited list holds: 2100 objects in one cell, in pairs far from each other.
    NewContactScene(2108, 8192);
    void** head = NewHead();
    for (uint32_t object = 0; object < 2100; ++object) {
      const float x = (10.f * static_cast<float>(object / 2)) + (0.25f * static_cast<float>(object % 2));
      AddToCell(NewObject(fast_tables[object % 2], object, x, 0.f, 1), head, 8);
    }
    const ContactOutcome alone = ExpectContactSame("one cell: every walk visits every other object");
    Expect(alone.handled && alone.calls.size() == 1050 && contact.visited == uint64_t{2100} * 2099 && contact.listed == 2100,
           "one cell: 2099 ids a walk, one kept");
  }
  {
    // An object with more cells than the row swaps takes the game's walk.
    std::vector<void*> objects = base_scene();
    void* big = NewObject(fast_tables[1], 3000, -50.f, 0.f, FAKE_ENTRY_COUNT + 1);
    for (uint32_t cell = 0; cell < FAKE_ENTRY_COUNT + 1; ++cell) {
      AddToCell(big, At<void**>(At<void*>(objects[cell], OBJECT_CELLS), CELL_HEAD), 8);
    }
    At<float>(big, OBJECT_X) = At<float>(objects[64], OBJECT_X) + 0.25f;
    const ContactOutcome alone = ExpectContactSame("many cells: the game's walk for that object");
    Expect(alone.handled && active_line(2101, 1) == 1, "many cells: one plain object in the row's tick");
    RestoreContactWorld();
    At<uint32_t>(big, OBJECT_CELL_COUNT) = FAKE_ENTRY_COUNT;
    Expect(ExpectContactSame("64 cells: swapped").handled && active_line(2101, 0) == 1, "64 cells: the row's own");
  }
  {
    // The capacity rule: an id vector of four, and one walk that visits six ids. That tick is the game's (its own
    // pushes grow the vector), with no latch and no change of the lane; the next one is the row's.
    std::vector<void*> objects = NewPlainScene(2100, 4);
    for (uint32_t other = 1; other < 7; ++other) {
      AddToCell(objects[other], At<void**>(At<void*>(objects[0], OBJECT_CELLS), CELL_HEAD), 8);
    }
    const ContactOutcome alone = ExpectContactSame("id set: a walk past the id vector's capacity, the game's tick");
    Expect(!alone.handled && alone.regions == 1 && contact.skip_period == NO_PERIOD && LaneIs(ENGINE_NOT_USED_YET)
               && !contact.busy.load() && LinesWith("renodx_attila64: contact split: partly (id set)") == 1 && step_lines.size() == 1
               && At<uint32_t>(id_set_memory, ID_SET_CAPACITY) == 8,
           "id set: one line, no latch, the lane as it was, the game grew the vector");
    Expect(ContactTick(scene.manager, &block, api) && LaneIs(ENGINE_ACTIVE) && step_lines.size() == 2,
           "id set: the next tick is the row's, and active");
    // Exactly the capacity is no reason: five objects in one cell, each walk visits the four others.
    objects = NewPlainScene(2100, 4);
    for (uint32_t other = 1; other < 5; ++other) {
      AddToCell(objects[other], At<void**>(At<void*>(objects[0], OBJECT_CELLS), CELL_HEAD), 8);
    }
    Expect(ExpectContactSame("id set: walks of exactly the capacity").handled && step_lines.size() == 1,
           "id set: exactly the capacity is the row's");
  }
  {
    // The queue lock error: no helper, no share, the region closed, the tick the game's; no latch.
    base_scene();
    queue_lock_fails = true;
    const ContactOutcome alone = ExpectContactSame("queue lock: the game's tick");
    Expect(!alone.handled && alone.regions == 1 && contact.skip_period == NO_PERIOD && LaneIs(ENGINE_PARTLY) && !contact.busy.load()
               && !contact.open && contact.next_chunk.load() == 17 && step_lines.size() == 1
               && LinesWith("renodx_attila64: contact split: partly (queue lock)") == 1,
           "queue lock: one line, partly, no latch, the row free and the region closed with no chunk left");
    queue_lock_fails = false;
    const ContactOutcome next = RunContact(true, 0, false);
    Expect(next.handled && LaneIs(ENGINE_PARTLY) && step_lines.size() == 2 && LinesWith("(nested)") == 0,
           "queue lock: the next tick is the row's; partly stays");
  }

  // The gate's rows: each refuses the tick untouched.
  {
    base_scene();
    KeepContactWorld();
    FreshContact();
    const auto row = [&](const std::function<void()>& set, EngineState lane, const char* line, const char* what) {
      FreshContact();
      RestoreContactWorld();
      set();
      const std::vector<uint8_t> kept_world = world;
      const std::vector<uint8_t> kept_id_set = id_set_world;
      KeepContactWorld();
      const bool refused = Refused();
      Expect(refused && LaneIs(lane) && step_lines.size() == ((line == nullptr) ? 0u : 1u) && (line == nullptr || LinesWith(line) == 1),
             what);
      Expect(Refused() && step_lines.size() == ((line == nullptr) ? 0u : 1u), "gate: the same row again logs nothing more");
      world = kept_world;
      id_set_world = kept_id_set;
    };
    row([] { block.core_wanted.store(CORE_BATTLE | CORE_STEP); }, ENGINE_OFF, nullptr, "gate: the row's bit clear, the lane off");
    row([] { contact.code_read.store(false); }, ENGINE_NOT_USED_YET, nullptr, "gate: the game's code not as read");
    contact.code_read.store(true);
    row([] { At<uint8_t>(id_set_memory, ID_SET_GUARD) = 0; }, ENGINE_NOT_USED_YET, nullptr, "gate: the pass's first call");
    row([] { At<uint8_t>(scene.manager, CONTACT_RESORT) = 1; }, ENGINE_NOT_USED_YET, nullptr, "gate: a re-sort asked for");
    row([] { At<uint32_t>(scene.model, MODEL_TICK) = 300; }, ENGINE_NOT_USED_YET, nullptr, "gate: a re-sort tick");
    row([] { At<uint32_t>(scene.manager, CONTACT_TAIL_COUNT) = 1; }, ENGINE_PARTLY, "renodx_attila64: contact split: partly (ships)",
        "gate: a tail entry");
    row([] { *reinterpret_cast<uint8_t*>(TASK_FLAG) = 0; }, ENGINE_PARTLY, "renodx_attila64: contact split: partly (too few task threads)",
        "gate: a step without tasks");
    *reinterpret_cast<uint8_t*>(TASK_FLAG) = 1;
    row([] { At<uint32_t>(scene.task_manager, TASK_MANAGER_THREADS) = MIN_TASK_THREADS - 1; }, ENGINE_PARTLY,
        "renodx_attila64: contact split: partly (too few task threads)", "gate: three task threads");
    row([] { At<uint32_t>(scene.manager, CONTACT_OBJECT_COUNT) = MIN_OBJECTS - 1; }, ENGINE_NOT_USED_YET, nullptr, "gate: a short list");
    row([] { At<uint32_t>(scene.manager, CONTACT_OBJECT_COUNT) = OBJECT_LIMIT + 1; }, ENGINE_NOT_USED_YET, nullptr,
        "gate: more objects than ids");
    row([] { contact.skip_period = 1; }, ENGINE_NOT_USED_YET, nullptr, "gate: the skip latch's period");
    row([] { contact.busy.store(true); }, ENGINE_PARTLY, "renodx_attila64: contact split: partly (nested)", "gate: an entry while a tick runs");
    Expect(contact.busy.load(), "gate: the entry that found the row busy leaves `busy` set");
    contact.busy.store(false);

    // The boundaries are the row's: four task threads, the shortest list, and a stale period of another battle.
    FreshContact();
    RestoreContactWorld();
    At<uint32_t>(scene.task_manager, TASK_MANAGER_THREADS) = MIN_TASK_THREADS;
    KeepContactWorld();
    Expect(RunContact(true).handled && LinesWith("helpers 2)") == 1, "gate: four task threads are enough, two helpers");
    KeepTick(250);
    contact.skip_period = 1;
    Expect(RunContact(true, 0, false).handled, "skip latch: a stale period skips only its own number");
    // The lane: off with the bit clear, back with it set, active again without a second line.
    block.core_wanted.store(0);
    Expect(Refused() && LaneIs(ENGINE_OFF), "lane: off with the bit clear");
    block.core_wanted.store(CORE_SPLIT);
    KeepTick(300);
    Expect(Refused() && LaneIs(ENGINE_NOT_USED_YET), "lane: the bit set again leaves off at the next tick, whatever the tick");
    KeepTick(301);
    const size_t lines_before = step_lines.size();
    Expect(RunContact(true, 0, false).handled && LaneIs(ENGINE_ACTIVE) && step_lines.size() == lines_before,
           "lane: active again without a second line");
    // The two summary lines, at the 1000th and the 3000th tick the row did.
    for (const uint32_t ticks : SUMMARY_TICKS) {
      contact.handled_ticks = ticks - 1;
      char line[96];
      std::snprintf(line, sizeof(line), "renodx_attila64: contact split: %u ticks (region mean ", ticks);
      Expect(RunContact(true, 0, false).handled && LinesWith(line) == 1 && LinesWith("own chunks 100 %, visited ") != 0
                 && LinesWith(", plain 0 %, fence ticks 0 (first at tick 0, index 0), original ticks 0)") != 0,
             "summary: one line at the 1000th and at the 3000th tick");
    }
    Expect(std::ranges::all_of(step_lines, [](const std::string& line) { return line.size() < 1024; }),
           "contact lines: each under 1 KB");
  }
  {
    // The smallest list the row takes, and a short last chunk.
    base_scene(MIN_OBJECTS);
    Expect(ExpectContactSame("the shortest list").handled, "gate: 2048 objects are enough");
    base_scene(MIN_OBJECTS + 1);
    Expect(ExpectContactSame("a last chunk of one object").handled && contact.chunks == 17, "a last chunk of one object");
  }
  contact.enqueue = nullptr;
  block.engine_status.store(0);
  block.core_wanted.store(CORE_STEP);
  std::printf("  contact split: %d failure(s)\n", failures - failures_before);
}

inline constexpr char CODE_DIFFERS_LINE[] = "renodx_attila64: contact split: off (game code differs at 0x%llx+0x%x)";
// One call of the row's code check as init makes its first: whether it held, and its line.
struct CodeCheck {
  bool held = false;
  std::vector<std::string> lines;
};
CodeCheck CheckCode() {
  contact.code_line_logged = false;
  step_lines.clear();
  const bool held = CheckContactCode(api);
  return {.held = held && contact.code_read.load(), .lines = step_lines};
}
bool Names(const CodeCheck& check, uint64_t address, uint32_t offset) {
  char line[96];
  std::snprintf(line, sizeof(line), CODE_DIFFERS_LINE, address, offset);
  return !check.held && !contact.code_read.load() && check.lines.size() == 1 && check.lines[0] == line;
}

// The contact split's tables, without the game file: the class table and its lookup, the ranges' order, the swap's
// cell entries, the row's default, and the code check on memory that is not the game's.
void RunContactTableChecks() {
  uint64_t class_value = FNV_OFFSET_BASIS;
  std::array<uint32_t, 4> with_mode = {};
  bool ascending = true;
  bool found = true;
  for (size_t index = 0; index < CONTACT_CLASSES.size(); ++index) {
    const ContactClass& known = CONTACT_CLASSES[index];
    std::array<uint8_t, sizeof(uint64_t) + 1> bytes = {};
    const uint64_t vptr = known.vptr;
    std::memcpy(bytes.data(), &vptr, sizeof(vptr));
    bytes.back() = known.mode;
    class_value = Fnv1a64(bytes.data(), bytes.size(), class_value);
    ++with_mode[known.mode];
    ascending = ascending && (index == 0 || CONTACT_CLASSES[index - 1].vptr < known.vptr);
    uint8_t slot = ContactClassSlot(known.vptr);
    while (contact.classes[slot].vptr != 0 && contact.classes[slot].vptr != known.vptr) {
      ++slot;
    }
    found = found && contact.classes[slot].vptr == known.vptr && contact.classes[slot].mode == known.mode;
  }
  Expect(CONTACT_CLASSES.size() == 55 && with_mode[CONTACT_FAST] == 53 && with_mode[CONTACT_FENCED] == 1
             && with_mode[CONTACT_NAVAL] == 1 && ascending && class_value == 0x3565fe60c67e4d69,
         "class table: 55 classes in pointer order, 53 fast, one fenced, one naval, the census's check value");
  Expect(found && std::ranges::count_if(contact.classes, [](const ContactClass& slot) { return slot.vptr != 0; }) == 55,
         "class table: the lookup finds each class with its mode and holds nothing else");

  bool ranges_in_order = true;
  for (size_t index = 0; index < CONTACT_RANGES.size(); ++index) {
    const ContactRange& range = CONTACT_RANGES[index];
    ranges_in_order = ranges_in_order && range.size != 0
                      && (index == 0 || CONTACT_RANGES[index - 1].start + CONTACT_RANGES[index - 1].size <= range.start);
    if (range.mask != CONTACT_NO_MASK) {
      ranges_in_order = ranges_in_order && range.mask < CONTACT_MASKS.size() && CONTACT_MASKS[range.mask].start >= range.start
                        && CONTACT_MASKS[range.mask].start + CONTACT_MASKS[range.mask].size <= range.start + range.size;
    }
  }
  bool entries_in_order = true;
  for (size_t index = 1; index < CONTACT_ENTRIES.size(); ++index) {
    const ContactEntry& before = CONTACT_ENTRIES[index - 1];
    const ContactEntry& entry = CONTACT_ENTRIES[index];
    entries_in_order = entries_in_order && (before.vtable < entry.vtable || (before.vtable == entry.vtable && before.slot < entry.slot));
  }
  Expect(ranges_in_order && CONTACT_RANGES.size() == 184 && CONTACT_RANGE_CHECKS.size() == 2441 && entries_in_order
             && CONTACT_ENTRIES.size() == 1415 && CONTACT_ENTRY_CHAIN == 0x43d70ccc80a103b8,
         "code tables: 184 ranges in address order without overlap, each mask inside its range; 1415 entries in order");
  Expect((CORE_WANTED_DEFAULT & CORE_SPLIT) == 0, "the contact split's bit is not in the build's default");

  bool cells_zero = contact_cells.head == nullptr && contact_cells.none == nullptr
                    && contact_cells.entries[0].head == &contact_cells.head;
  for (size_t entry = 0; entry < contact_cells.entries.size(); ++entry) {
    cells_zero = cells_zero && contact_cells.entries[entry].zero == decltype(ContactCell::zero){}
                 && (entry == 0 || contact_cells.entries[entry].head == &contact_cells.none);
  }
  Expect(cells_zero && contact_cells.entries.size() == FAKE_ENTRY_COUNT,
         "swap cells: 64 entries, zero but each head pointer, the first to the head slot and the others to the null one");

  // Memory that is not the game's: the first range is named, once however often the check runs.
  Expect(Names(CheckCode(), CONTACT_RANGES[0].start, 0), "code check: memory that is not the game's is refused, the first range named");
  step_lines.clear();
  Expect(!CheckContactCode(api) && step_lines.empty(), "code check: a second miss logs nothing");
}

// T-B3 and the code check's lines, on the game file as the plugin's init finds it (mapped, the stack-check rewrite
// done, the game's allocator still its own).
void RunContactByteChecks() {
  const auto at = [](uintptr_t address) { return reinterpret_cast<uint8_t*>(address); };
  const auto masked = [](const ContactRange& range, uint32_t offset) {
    return range.mask != CONTACT_NO_MASK && range.start + offset - CONTACT_MASKS[range.mask].start < CONTACT_MASKS[range.mask].size;
  };
  // Each range's value and check words again, byte by byte: a word after every 16th byte and after the last.
  // The census's own value is the 176 whole functions' bytes, unmasked, as one chain: every range but the eight
  // that are a function's part or outside the light closure.
  constexpr std::array<uint32_t, 8> NOT_CENSUS_FUNCTIONS = {0x5df305, 0x6e88f5, 0x803e70, 0xba51a0, 0xc501c0, 0x3061e60, 0x3062aa0, 0x3062d00};
  size_t check_index = 0;
  bool ranges_hold = true;
  uint64_t census_value = FNV_OFFSET_BASIS;
  uint32_t census_functions = 0;
  for (const ContactRange& range : CONTACT_RANGES) {
    uint64_t hash = FNV_OFFSET_BASIS;
    for (uint32_t offset = 0; offset < range.size; ++offset) {
      const uint8_t byte = (masked(range, offset) ? uint8_t{0} : *at(range.start + offset));
      hash = Fnv1a64(&byte, 1, hash);
      if (offset % 16 == 15 || offset + 1 == range.size) {
        ranges_hold = ranges_hold && check_index < CONTACT_RANGE_CHECKS.size() && static_cast<uint16_t>(hash) == CONTACT_RANGE_CHECKS[check_index];
        ++check_index;
      }
    }
    if (hash != range.value) {
      std::printf("  contact range %#x+%#x: not the table's value\n", range.start, range.size);
      ranges_hold = false;
    }
    if (std::ranges::find(NOT_CENSUS_FUNCTIONS, range.start) == NOT_CENSUS_FUNCTIONS.end()) {
      census_value = Fnv1a64(at(range.start), range.size, census_value);
      ++census_functions;
    }
  }
  Expect(ranges_hold && check_index == CONTACT_RANGE_CHECKS.size(),
         "contact code: every range's value (masked where the table masks) and every check word are the game's, in memory");
  Expect(census_functions == 176 && census_value == 0x9fb6fdf272eda086, "contact code: the census's value over its 176 functions, unmasked");
  // The design's named ranges with their values; the task-thread loop's is the masked one.
  constexpr std::array<CodeRange, 16> NAMED_RANGES = {{
      {.address = 0x5df305, .length = 0x353, .hash = 0xf1c67b3ac5c52819}, {.address = 0x803e70, .length = 0x7f7, .hash = 0xb4eeb581f66bba51},
      {.address = 0x6b7f50, .length = 0x122, .hash = 0x90b774c9bf1883e0}, {.address = 0xc501c0, .length = 0xf, .hash = 0x6228baa51b3be82c},
      {.address = 0x7856d0, .length = 0x15, .hash = 0x4dc09ac03f94d7aa},  {.address = 0x807630, .length = 0x2c, .hash = 0xac0085666de6660d},
      {.address = 0x7856f0, .length = 0x499, .hash = 0xdb37f23b20d93d1b}, {.address = 0x807660, .length = 0x223, .hash = 0x991769e1cc561873},
      {.address = 0xb29860, .length = 5, .hash = 0x5355f252f285f1e8},     {.address = 0x95c090, .length = 0xe6, .hash = 0x14aa7c24caa5e9e6},
      {.address = 0xb409e0, .length = 0xb7, .hash = 0x5dc12e54833395cd},  {.address = 0xba51a0, .length = 0xb1, .hash = 0x19afaa62477ed725},
      {.address = 0x6e88f5, .length = 0x108, .hash = 0x4042fdbb65bfeef4}, {.address = 0x3062d00, .length = 0xb8, .hash = 0xe829e488b475825f},
      {.address = 0x3062aa0, .length = 0x67, .hash = 0xdb25a6d158992c20}, {.address = 0x3061e60, .length = 0x5d, .hash = 0xa74e22f15a95c1c6},
  }};
  const auto range_at = [](uint32_t start) {
    const auto range = std::ranges::find(CONTACT_RANGES, start, &ContactRange::start);
    return ((range != CONTACT_RANGES.end()) ? &*range : nullptr);
  };
  bool named_hold = true;
  for (const CodeRange& named : NAMED_RANGES) {
    const ContactRange* range = range_at(named.address);
    named_hold = named_hold && range != nullptr && range->size == named.length && range->value == named.hash;
  }
  Expect(named_hold && Fnv1a64(at(0x3062d00), 0xb8) == 0x5ca2674289a8b5b7 && Fnv1a64(at(CONTACT_LOOP), 0x358) == 0x875d9d8f5e3c3893
             && range_at(0x3062d00)->mask != CONTACT_NO_MASK && CONTACT_MASKS[range_at(0x3062d00)->mask].start == 0x3062d92
             && CONTACT_MASKS[range_at(0x3062d00)->mask].size == 4 && CONTACT_MASKS.size() == 1,
         "contact code: the named ranges with the design's values; one mask, on the task-thread loop's four bytes");

  // The entries: the chain and each check word again; every word is the start of a function of the census's list
  // (a range, or one of the 15 math leaves that are not in the value).
  constexpr std::array<uint32_t, 15> MATH_LEAVES = {0x3002e20, 0x3003660, 0x30040f0, 0x3004a70, 0x3004c60, 0x3005d80, 0x3007cf0, 0x301d610,
                                                    0x301ff20, 0x30200f0, 0x3020980, 0x3025cf0, 0x3026090, 0x3027ec0, 0x30678e0};
  const auto entry_word = [&](const ContactEntry& entry) { return reinterpret_cast<uint64_t*>(at(uintptr_t{entry.vtable} + entry.slot)); };
  // The entries' chain on the memory as it is: its end value, and the first entry whose check word differs (the
  // last entry if none does).
  struct Chain {
    uint64_t value = 0;
    size_t named = 0;
  };
  const auto chain_now = [&] {
    Chain chain = {.value = FNV_OFFSET_BASIS, .named = CONTACT_ENTRIES.size()};
    for (size_t index = 0; index < CONTACT_ENTRIES.size(); ++index) {
      chain.value = Fnv1a64(reinterpret_cast<const uint8_t*>(entry_word(CONTACT_ENTRIES[index])), sizeof(uint64_t), chain.value);
      if (chain.named == CONTACT_ENTRIES.size() && static_cast<uint16_t>(chain.value) != CONTACT_ENTRIES[index].check) {
        chain.named = index;
      }
    }
    chain.named = std::min(chain.named, CONTACT_ENTRIES.size() - 1);
    return chain;
  };
  {
    bool words_are_functions = true;
    std::vector<uint32_t> vtables;
    std::vector<uint16_t> slots;
    for (const ContactEntry& entry : CONTACT_ENTRIES) {
      const uint64_t word = *entry_word(entry);
      words_are_functions = words_are_functions && word <= UINT32_MAX
                            && ((range_at(static_cast<uint32_t>(word)) != nullptr
                                 && std::ranges::find(NOT_CENSUS_FUNCTIONS, word) == NOT_CENSUS_FUNCTIONS.end())
                                || std::ranges::find(MATH_LEAVES, word) != MATH_LEAVES.end());
      vtables.push_back(entry.vtable);
      slots.push_back(entry.slot);
    }
    std::ranges::sort(vtables);
    std::ranges::sort(slots);
    const Chain chain = chain_now();
    Expect(chain.value == CONTACT_ENTRY_CHAIN && chain.named == CONTACT_ENTRIES.size() - 1
               && static_cast<uint16_t>(chain.value) == CONTACT_ENTRIES.back().check,
           "contact entries: the chain's end value and every check word are the game's");
    Expect(words_are_functions && std::ranges::unique(vtables).begin() - vtables.begin() == 235
               && std::ranges::unique(slots).begin() - slots.begin() == 37,
           "contact entries: every word is a function start of the census's list; 235 tables, 37 slots");
  }
  // The class table against the game: each class's mode from the two words of its table, by the census's rule.
  {
    bool modes_hold = true;
    for (const ContactClass& known : CONTACT_CLASSES) {
      const uint64_t first = *reinterpret_cast<const uint64_t*>(at(known.vptr + 0x98));
      const uint64_t second = *reinterpret_cast<const uint64_t*>(at(known.vptr + 0xa0));
      const bool first_fast = first == 0x7856d0 || first == 0x807630;
      ContactMode mode = CONTACT_PLAIN;
      if (first_fast && (second == 0x7856f0 || second == 0x807660 || second == 0xb29860)) {
        mode = CONTACT_FAST;
      } else if (first == 0x7856d0 && second == 0x95c090) {
        mode = CONTACT_FENCED;
      } else if (second == 0xc516d0) {
        mode = CONTACT_NAVAL;
      }
      modes_hold = modes_hold && mode == known.mode;
    }
    bool targets_hold = true;
    for (const uint32_t target : {0x7856d0u, 0x807630u, 0x7856f0u, 0x807660u, 0xb29860u, 0x95c090u, 0xc516d0u}) {
      targets_hold = targets_hold && range_at(target) != nullptr;
    }
    Expect(modes_hold && targets_hold, "class table: each mode follows from the game's two table words; the seven targets are function starts");
  }
  {
    // The engine's enqueue as read (the region's will be its transcription), and the table chain's source where the
    // row's table test says: the walk's loads at 0x785e74 and the accessor's at 0x272c07f, both inside checked ranges.
    constexpr std::array<uint8_t, 13> WALK_LOADS = {0x49, 0x8b, 0x45, 0x18, 0x48, 0x8b, 0x40, 0x58, 0x48, 0x8b, 0x78, 0x20, 0xe8};
    constexpr std::array<uint8_t, 7> ACCESSOR_LOAD = {0x48, 0x8b, 0x83, 0x98, 0x21, 0x00, 0x00};
    int32_t accessor_offset = 0;
    std::memcpy(&accessor_offset, at(0x785e81), sizeof(accessor_offset));
    Expect(Fnv1a64(at(0x5df9a5), 0x5dfb9c - 0x5df9a5) == 0x3af36b1c2f92753b, "contact code: the engine's enqueue is the one read");
    Expect(std::memcmp(at(0x785e74), WALK_LOADS.data(), WALK_LOADS.size()) == 0 && 0x785e85 + accessor_offset == 0x272c070
               && std::memcmp(at(0x272c07f), ACCESSOR_LOAD.data(), ACCESSOR_LOAD.size()) == 0 && range_at(0x785e60) != nullptr
               && range_at(0x785e60)->size == 0x50f && range_at(0x272c070) != nullptr && range_at(0x272c070)->size == 0x123
               && OBJECT_ENVIRONMENT == 0x18 && ENVIRONMENT_CORE == 0x58 && CORE_DATABASE == 0x20 && DATABASE_TABLE == 0x2198,
           "table test: the chain is the game's (object +0x18, +0x58, +0x20, the accessor's +0x2198), in checked code");
  }

  // The check itself: it holds without a line, also with a hook's jump in the pass's first five bytes.
  {
    const CodeCheck game = CheckCode();
    Expect(game.held && game.lines.empty(), "code check: the game file passes, without a line");
    std::array<uint8_t, HOOK_BYTES> game_bytes = {};
    std::memcpy(game_bytes.data(), at(CONTACT_LOOP), HOOK_BYTES);
    const std::array<uint8_t, HOOK_BYTES> jump = {0xe9, 0x12, 0x34, 0x56, 0x00};
    std::memcpy(at(CONTACT_LOOP), jump.data(), HOOK_BYTES);
    Expect(CheckCode().held, "code check: it passes with the hook's jump in the pass's five bytes");
    std::memcpy(at(CONTACT_LOOP), game_bytes.data(), HOOK_BYTES);
  }
  // A changed byte: the line names the range's start and the byte's 16-byte block. Under a mask: no miss.
  {
    bool named = true;
    for (const ContactRange& range : CONTACT_RANGES) {
      for (const uint32_t offset : {0u, range.size / 2u, range.size - 1u}) {
        if (masked(range, offset)) continue;
        *at(range.start + offset) ^= 0x40;
        named = named && Names(CheckCode(), range.start, offset & ~15u);
        *at(range.start + offset) ^= 0x40;
      }
    }
    Expect(named, "code check: a changed byte is found, the line names its range and block");
    bool masked_pass = true;
    for (uint32_t byte = 0; byte < CONTACT_MASKS[0].size; ++byte) {
      *at(CONTACT_MASKS[0].start + byte) ^= 0xff;
      masked_pass = masked_pass && CheckCode().held;
      *at(CONTACT_MASKS[0].start + byte) ^= 0xff;
    }
    *at(CONTACT_MASKS[0].start - 1) ^= 1;
    const bool before_mask = Names(CheckCode(), 0x3062d00, 0x90);
    *at(CONTACT_MASKS[0].start - 1) ^= 1;
    *at(CONTACT_MASKS[0].start + CONTACT_MASKS[0].size) ^= 1;
    Expect(masked_pass && before_mask && Names(CheckCode(), 0x3062d00, 0x90),
           "code check: a changed byte under the mask is no miss; the bytes beside the mask are checked");
    *at(CONTACT_MASKS[0].start + CONTACT_MASKS[0].size) ^= 1;
  }
  // A changed table word: the line names its table and slot.
  {
    bool named = true;
    for (size_t index = 0; index < CONTACT_ENTRIES.size(); index += 7) {
      const ContactEntry& entry = CONTACT_ENTRIES[(index == 0) ? CONTACT_ENTRIES.size() - 1 : index - 1];
      *entry_word(entry) ^= 0x10;
      named = named && Names(CheckCode(), entry.vtable, entry.slot);
      *entry_word(entry) ^= 0x10;
    }
    Expect(named, "code check: a changed table word is found, the line names its table and slot");
    // A changed word whose own check word still matches (found by search): the 64-bit end value still refuses it,
    // and the line names a later entry.
    constexpr size_t CHANGED = 100;
    const uint64_t game_word = *entry_word(CONTACT_ENTRIES[CHANGED]);
    uint64_t before_changed = FNV_OFFSET_BASIS;
    for (size_t index = 0; index < CHANGED; ++index) {
      before_changed = Fnv1a64(reinterpret_cast<const uint8_t*>(entry_word(CONTACT_ENTRIES[index])), sizeof(uint64_t), before_changed);
    }
    uint64_t other_word = game_word;
    do {
      other_word += uint64_t{1} << 32;
    } while (static_cast<uint16_t>(Fnv1a64(reinterpret_cast<const uint8_t*>(&other_word), sizeof(other_word), before_changed))
             != CONTACT_ENTRIES[CHANGED].check);
    *entry_word(CONTACT_ENTRIES[CHANGED]) = other_word;
    const Chain chain = chain_now();
    Expect(chain.value != CONTACT_ENTRY_CHAIN && chain.named > CHANGED
               && Names(CheckCode(), CONTACT_ENTRIES[chain.named].vtable, CONTACT_ENTRIES[chain.named].slot),
           "code check: a changed word whose check word matches is still refused, by the chain's 64-bit end value");
    *entry_word(CONTACT_ENTRIES[CHANGED]) = game_word;
  }
  // One line however many misses: at init and again at the game's start.
  {
    *at(CONTACT_RANGES[3].start) ^= 1;
    const CodeCheck first = CheckCode();
    step_lines.clear();
    Expect(!first.held && first.lines.size() == 1 && !CheckContactCode(api) && step_lines.empty(), "code check: a second miss logs no second line");
    *at(CONTACT_RANGES[3].start) ^= 1;
    const CodeCheck again = CheckCode();
    Expect(again.held && again.lines.empty() && contact.code_read.load(), "code check: the game's bytes back, it holds again");
  }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  // Unbuffered: a run that ends in the game's code (an access violation) still shows the lines before it.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  // First, before anything else can map there. Execute: the fakes at the game's addresses, then the game's own code.
  if (!ReserveGame(PAGE_EXECUTE_READWRITE)) {
    std::printf("FAIL: the game range 0x400000..0x527d000 is taken\n");
    return 1;
  }
  if (argc < 2) {
    std::printf("FAIL: battle_step_test.exe <renodx_attila64.dll> [<game ELF>]\n");
    return 1;
  }
  arena = static_cast<uint8_t*>(VirtualAlloc(nullptr, ARENA_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  if (arena == nullptr) {
    std::printf("FAIL: no memory for the test's arena (error %lu)\n", GetLastError());
    return 1;
  }
  api = MakeApi(".", GAME_HI_MIN);
  api.log = &QuietLog;
  // The plugin's hook as its init leaves it: the loader, and the game's function it passes on to.
  loader = &api;
  originals[LABEL_PAIRS_ENTRY] = reinterpret_cast<void*>(&PassOn);

  // The DLL file holds the row's word as one whole string of its own (a zero byte before and after): the batch tool
  // looks for exactly that. The bare bytes would not do, they are in every "battle step: ..." line too. The row's
  // three lines are whole strings as well, and the row's bit is in the build's default.
  {
    using std::string_view_literals::operator""sv;
    std::ifstream dll_file(argv[1], std::ios::binary);
    const std::string dll_bytes{std::istreambuf_iterator<char>(dll_file), std::istreambuf_iterator<char>()};
    Expect(dll_bytes.find("\0battle step\0"sv) != std::string::npos, "dll: the file holds the row's word as a whole string");
    Expect(dll_bytes.find("\0renodx_attila64: battle step: active (new %u, active %u, pairs %u)\0"sv) != std::string::npos
               && dll_bytes.find("\0renodx_attila64: battle step: partly (%s)\0"sv) != std::string::npos
               && dll_bytes.find("\0renodx_attila64: battle step: game code differs (%#x+%#x)\0"sv) != std::string::npos,
           "dll: the file holds the row's three lines as whole strings");
    Expect((CORE_WANTED_DEFAULT & CORE_STEP) != 0, "the row's bit is in the build's default");
  }

  manager_table[MANAGER_CREATE_SLOT] = reinterpret_cast<void*>(&FakeCreate);
  manager_table[MANAGER_RETIRE_SLOT] = reinterpret_cast<void*>(&FakeRetire);
  manager_table[MANAGER_VALID_SLOT] = reinterpret_cast<void*>(&FakeValid);
  manager_table[MANAGER_PAIR_TEST_SLOT] = reinterpret_cast<void*>(&FakePairTest);
  other_manager_table = manager_table;
  other_group_table[GROUP_RETIRE_TEST_SLOT] = reinterpret_cast<void*>(&FakeRetireTest);
  other_group_table[GROUP_JOIN_SLOT] = reinterpret_cast<void*>(&LoggedJoinTest);
  other_group_table[GROUP_JOIN_ONE_SLOT] = reinterpret_cast<void*>(&ModelJoinOne);
  other_group_table[GROUP_STARTED_SLOT] = reinterpret_cast<void*>(&ModelStarted);
  for (auto& table : group_tables) {
    table = other_group_table;
    table[GROUP_JOIN_SLOT] = reinterpret_cast<void*>(&ModelJoinTest);
  }
  state.manager_vptr = reinterpret_cast<uintptr_t>(manager_table.data());
  state.group_vptrs = {reinterpret_cast<uintptr_t>(group_tables[0].data()), reinterpret_cast<uintptr_t>(group_tables[1].data())};

  // Without the game file: the transcription is the game's resolver, on fakes at the game's three addresses.
  WriteJump(SORT_PAIRS, reinterpret_cast<void*>(&FakeSortPairs));
  WriteJump(FINISH_PAIR_SORT, reinterpret_cast<void*>(&FakeFinishSort));
  WriteJump(APPEND_GROUP, reinterpret_cast<void*>(&FakeAppend));
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  games = {&Transcription};
  RunCases("model", 10000, 1, 0x70616972);
  RunContactTableChecks();

  GameFile game;
  if (argc < 3 || MapGameFile(argv[2], &game) != MAP_DONE) {
    std::printf("SKIPPED: the game-file part (battle_step_test.exe <renodx_attila64.dll> <game ELF>)\n");
    std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
    return ((failures == 0) ? EXIT_SKIPPED : failures);
  }
  const auto at = [](uintptr_t address) { return reinterpret_cast<uint8_t*>(address); };
  // Each code range as the file holds it, then in memory as the plugin's init finds it (the loader's stack-check
  // rewrite): the row's hashes are the memory's.
  std::array<uint64_t, CODE_RANGES.size()> file_hashes = {};
  for (size_t range = 0; range < CODE_RANGES.size(); ++range) {
    file_hashes[range] = Fnv1a64(at(CODE_RANGES[range].address), CODE_RANGES[range].length);
  }
  const int rewritten = ApplyStackRule(at(GAME_LO), at(game.end));
  std::printf("  mapped %#llx..%#llx, %d stack-check reads rewritten\n", uint64_t{GAME_LO}, uint64_t{game.end}, rewritten);
  for (size_t range = 0; range < CODE_RANGES.size(); ++range) {
    const uint64_t memory_hash = Fnv1a64(at(CODE_RANGES[range].address), CODE_RANGES[range].length);
    if (memory_hash != file_hashes[range]) {
      std::printf("  code range %#x+%#x: the stack-check rewrite changes it\n", CODE_RANGES[range].address, CODE_RANGES[range].length);
    }
    Expect(memory_hash == CODE_RANGES[range].hash, "code: each range's hash is the game's, in memory");
  }
  for (const TableWord& word : TABLE_WORDS) {
    Expect(*reinterpret_cast<const uint64_t*>(uintptr_t{word.address}) == word.value, "code: each table word is the game's");
  }
  step_lines.clear();
  Expect(CheckGameCode(api) && step_lines.empty(), "code: the game file passes the row's check, without a line");
  {
    // The hook's own five bytes are not in the check: a jump there (ours, once the site is in) passes.
    const Site& site = SITES[LABEL_PAIRS_ENTRY];
    std::array<uint8_t, HOOK_BYTES> game_bytes = {};
    std::memcpy(game_bytes.data(), at(RESOLVER), HOOK_BYTES);
    const std::array<uint8_t, HOOK_BYTES> jump = {0xe9, 0x12, 0x34, 0x56, 0x00};
    std::memcpy(at(RESOLVER), jump.data(), HOOK_BYTES);
    Expect(site.address == RESOLVER && site.length == HOOK_BYTES && site.row == ROW_BATTLE_STEP && CheckGameCode(api),
           "code: the check passes with the hook's jump in the site's five bytes");
    std::memcpy(at(RESOLVER), game_bytes.data(), HOOK_BYTES);
  }
  // One byte of each range, and each table word, changed: refused with the row's line naming it.
  for (const CodeRange& range : CODE_RANGES) {
    for (const uint32_t offset : {0u, range.length - 1}) {
      *at(range.address + offset) ^= 1;
      step_lines.clear();
      char line[96];
      std::snprintf(line, sizeof(line), "renodx_attila64: battle step: game code differs (%#x+%#x)", range.address, range.length);
      Expect(!CheckGameCode(api) && step_lines.size() == 1 && LinesWith(line) == 1, "code: a changed byte in a range is found");
      *at(range.address + offset) ^= 1;
    }
  }
  for (const TableWord& word : TABLE_WORDS) {
    *at(word.address) ^= 1;
    step_lines.clear();
    char line[96];
    std::snprintf(line, sizeof(line), "renodx_attila64: battle step: game code differs (%#x+0x8)", word.address);
    Expect(!CheckGameCode(api) && step_lines.size() == 1 && LinesWith(line) == 1, "code: a changed table word is found");
    *at(word.address) ^= 1;
  }
  {
    // Nothing enters the resolver or the contact pass past its first byte: no rel32 call or jump (e8, e9, 0f 8x) and no rel8 jump (eb, 7x,
    // e0..e3) in the code segment ends in the hook's other four bytes, and no pointer in the data does. A raw scan:
    // a hit would be read by hand.
    uint32_t into_the_site = 0;
    const auto inside = [](uint64_t target) {
      return (target > RESOLVER && target < RESOLVER + HOOK_BYTES) || (target > CONTACT_LOOP && target < CONTACT_LOOP + HOOK_BYTES);
    };
    for (uintptr_t address = GAME_LO; address + 8 <= game.writable_lo; ++address) {
      const uint8_t* code = at(address);
      int32_t rel32 = 0;
      std::memcpy(&rel32, code + 1 + ((code[0] == 0x0f) ? 1 : 0), sizeof(rel32));
      const bool near_jump = code[0] == 0xe8 || code[0] == 0xe9;
      const bool near_branch = code[0] == 0x0f && (code[1] & 0xf0) == 0x80;
      const bool short_jump = code[0] == 0xeb || (code[0] & 0xf0) == 0x70 || (code[0] & 0xfc) == 0xe0;
      if ((near_jump && inside(address + 5 + rel32)) || (near_branch && inside(address + 6 + rel32))
          || (short_jump && inside(address + 2 + static_cast<int8_t>(code[1])))) {
        std::printf("  a jump into the site at %#llx\n", uint64_t{address});
        ++into_the_site;
      }
    }
    for (uintptr_t address = GAME_LO; address + 8 <= game.end; address += 8) {
      uint64_t word = 0;
      std::memcpy(&word, at(address), sizeof(word));
      if (inside(word)) {
        std::printf("  a pointer into the site at %#llx\n", uint64_t{address});
        ++into_the_site;
      }
    }
    Expect(into_the_site == 0, "code: nothing jumps or points into either hook's bytes past the first");
  }
  RunContactByteChecks();

  // The game's own code: its allocator's two entries go to the test's (the only functions outside the game file the
  // resolver's closure calls), the group kinds are copies of the game's tables with the fake "take me back" answer.
  WriteJump(GAME_MALLOC, reinterpret_cast<void*>(&TestMalloc));
  WriteJump(GAME_FREE, reinterpret_cast<void*>(&TestFree));
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  for (size_t kind = 0; kind < group_tables.size(); ++kind) {
    std::memcpy(group_tables[kind].data(), at(GROUP_VPTRS[kind]), sizeof(group_tables[kind]));
    Expect(group_tables[kind][GROUP_JOIN_SLOT] == reinterpret_cast<void*>(uintptr_t{0x79f090})
               && group_tables[kind][GROUP_JOIN_ONE_SLOT] == reinterpret_cast<void*>(uintptr_t{0x79f210})
               && group_tables[kind][GROUP_STARTED_SLOT] == reinterpret_cast<void*>(uintptr_t{0x7a84a0}),
           "game file: a group kind's copy holds the game's join test, single join and answer");
    group_tables[kind][GROUP_RETIRE_TEST_SLOT] = reinterpret_cast<void*>(&FakeRetireTest);
  }
  games = {reinterpret_cast<PairsFn>(uintptr_t{RESOLVER}), &Transcription};
  RunCases("game file", 4000, 3, 0x67616d65);
  fast_tables[0] = fast_table_pool.data();
  for (size_t place = 64; place + 64 < fast_table_pool.size() && fast_tables[1] == nullptr; ++place) {
    if (ContactClassSlot(reinterpret_cast<uintptr_t>(&fast_table_pool[place])) == ContactClassSlot(reinterpret_cast<uintptr_t>(fast_tables[0]))) {
      fast_tables[1] = &fast_table_pool[place];
    }
  }
  Expect(fast_tables[1] != nullptr, "contact: two fake tables that share a slot of the class table");
  for (void** table : fast_tables) {
    EnterClass(table, CONTACT_FAST);
  }
  EnterClass(fenced_table.data(), CONTACT_FENCED);
  EnterClass(naval_table.data(), CONTACT_NAVAL);
  unknown_table[TABLE_OWNER_CALL] = reinterpret_cast<void*>(&FakeOwner);
  RunContactCases();

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
