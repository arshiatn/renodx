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
// battle_step_test.exe <renodx_attila64.dll> [<game ELF>]; exit code = failures, or 77 with none when the game-file
// part did not run.

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
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

ATTILA64_GAME_CALL void* TestMalloc(size_t bytes, uint32_t /*heap*/) noexcept { return Take(bytes); }
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
    // Nothing enters the resolver past its first byte: no rel32 call or jump (e8, e9, 0f 8x) and no rel8 jump (eb, 7x,
    // e0..e3) in the code segment ends in the hook's other four bytes, and no pointer in the data does. A raw scan:
    // a hit would be read by hand.
    uint32_t into_the_site = 0;
    const auto inside = [](uint64_t target) { return target > RESOLVER && target < RESOLVER + HOOK_BYTES; };
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
    Expect(into_the_site == 0, "code: nothing jumps or points into the hook's bytes past the first");
  }

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

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
