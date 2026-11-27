/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The parallel effects row without a game: the check that an entry holds the loader's own hook (patcher.hpp
// LoaderHooked), on hooks written as the loader writes them in both of its layouts; then the walk hook as shipped
// (hooks.hpp EffectWalkEntry) on fake effect tables, with a fake walk for the game's and a fake key compare at the
// game's address: the modes, the Automatic rule, the limits, the stores of the game's flag and the log lines.
// effect_guard_test.exe; exit code = failures.

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include "../hooks.hpp"
#include "../patcher.hpp"
#include "fake_loader.hpp"

namespace twattila::attila64_plugin::test {
namespace {

// A counted string as the game's compare reads it.
struct Key {
  uint32_t length;
  const char* bytes;
};
static_assert(sizeof(Key) == 16 && offsetof(Key, bytes) == 8);
constexpr Key EMPTY_KEY = {5, "empty"};
constexpr Key DELETED_KEY = {4, "dead"};

// The game's key compare: lengths first, then the bytes.
ATTILA64_GAME_CALL bool KeyEqual(const void* key, const void* other) noexcept {
  const auto* first = static_cast<const Key*>(key);
  const auto* second = static_cast<const Key*>(other);
  return first->length == second->length && std::memcmp(first->bytes, second->bytes, first->length) == 0;
}

// The game's walk: what it got, and the flag as its update would read it.
struct WalkRecord {
  uint32_t calls = 0;
  uint8_t flag = 0;
  const uint8_t* node = nullptr;
  float first_float = 0.f;
  float second_float = 0.f;
};
WalkRecord walk_record;
ATTILA64_GAME_CALL void GameWalk(uint8_t* node, float first_float, float second_float) noexcept {
  walk_record = {.calls = walk_record.calls + 1,
                 .flag = *reinterpret_cast<const uint8_t*>(EFFECT_FLAG),
                 .node = node,
                 .first_float = first_float,
                 .second_float = second_float};
}

// A scene node with an effect table of five slots: a type, a slot with the table's empty key, one with its deleted key
// (both with a value that points into `dead`, a page that cannot be read), a key with no type, a second type with seven
// instances. SetInstances gives the two types `instances` together. It points into itself: built in place.
struct FakeNode {
  std::array<uint8_t, EFFECT_DELETED_KEY + sizeof(Key)> node = {};
  std::array<uint8_t, 5 * EFFECT_SLOT_BYTES> slots = {};
  std::array<std::array<uint8_t, EFFECT_TYPE_COUNT + sizeof(uint32_t)>, 2> types = {};

  void Build(const uint8_t* dead) {
    const std::array<Key, 5> keys = {Key{4, "fire"}, EMPTY_KEY, DELETED_KEY, Key{5, "smoke"}, Key{4, "dust"}};
    const std::array<const uint8_t*, 5> values = {types[0].data(), dead, dead + 0x100, nullptr, types[1].data()};
    for (size_t index = 0; index < keys.size(); ++index) {
      std::memcpy(slots.data() + (index * EFFECT_SLOT_BYTES), &keys[index], sizeof(Key));
      std::memcpy(slots.data() + (index * EFFECT_SLOT_BYTES) + EFFECT_SLOT_VALUE, &values[index], sizeof(void*));
    }
    const uint8_t* table = slots.data();
    std::memcpy(node.data() + EFFECT_TABLE_SLOTS, &table, sizeof(table));
    SetCapacity(static_cast<uint32_t>(keys.size()));
    std::memcpy(node.data() + EFFECT_EMPTY_KEY, &EMPTY_KEY, sizeof(Key));
    std::memcpy(node.data() + EFFECT_DELETED_KEY, &DELETED_KEY, sizeof(Key));
    SetInstances(7);
  }
  void SetCapacity(uint32_t capacity) { std::memcpy(node.data() + EFFECT_TABLE_CAPACITY, &capacity, sizeof(capacity)); }
  void SetInstances(uint32_t instances) {
    const std::array<uint32_t, 2> counts = {instances - 7, 7};
    std::memcpy(types[0].data() + EFFECT_TYPE_COUNT, &counts[0], sizeof(uint32_t));
    std::memcpy(types[1].data() + EFFECT_TYPE_COUNT, &counts[1], sizeof(uint32_t));
  }
};

size_t LoggedCount(std::string_view text) {
  return static_cast<size_t>(
      std::ranges::count_if(log_lines, [&](const std::string& line) { return line.find(text) != std::string::npos; }));
}

}  // namespace
}  // namespace twattila::attila64_plugin::test

int wmain() {
  using namespace twattila::attila64_plugin;
  using namespace twattila::attila64_plugin::test;

  if (!ReserveGame(PAGE_EXECUTE_READWRITE)) {
    std::printf("FAIL: the game range 0x400000..0x527d000 is taken\n");
    return 1;
  }
  const auto at = [](uintptr_t address) { return reinterpret_cast<uint8_t*>(address); };

  // `mov rax, target; jmp rax` at `code`.
  const auto write_jump = [](uint8_t* code, uint64_t target) {
    code[0] = 0x48;
    code[1] = 0xb8;
    std::memcpy(code + 2, &target, sizeof(target));
    code[10] = 0xff;
    code[11] = 0xe0;
  };
  // The loader's entry hook at `entry`: a jmp rel32 to its way in (`way_in_offset` bytes into a 32-byte block near the
  // game), which is that jump. Returns the way in.
  const auto write_hook = [&](uintptr_t entry, size_t way_in_offset, uint64_t target) {
    uint8_t* way_in = static_cast<uint8_t*>(FakeCodeNearGame(32)) + way_in_offset;
    write_jump(way_in, target);
    const auto displacement = static_cast<int32_t>(reinterpret_cast<intptr_t>(way_in) - static_cast<intptr_t>(entry + 5));
    *at(entry) = 0xe9;
    std::memcpy(at(entry + 1), &displacement, sizeof(displacement));
    return way_in;
  };
  // A fake loader image: the hooks' targets lie inside it, a plugin's outside.
  constexpr uintptr_t IMAGE_LO = 0x140000000;
  constexpr uintptr_t IMAGE_HI = 0x140200000;
  std::array<uint8_t*, EFFECT_GUARD_ENTRIES.size()> ways_in = {};
  for (const size_t way_in_offset : {size_t{16}, size_t{0}}) {
    for (size_t index = 0; index < EFFECT_GUARD_ENTRIES.size(); ++index) {
      ways_in[index] = write_hook(EFFECT_GUARD_ENTRIES[index], way_in_offset, IMAGE_LO + 0x1000 + (index * 0x40));
    }
    Expect(LoaderHooked(EFFECT_GUARD_ENTRIES, IMAGE_LO, IMAGE_HI), "guards: five loader hooks, in either layout of its block");
  }
  Expect(!LoaderHooked({}, IMAGE_LO, IMAGE_HI), "guards: no entries is not offered");
  Expect(LoaderHooked(std::span(EFFECT_GUARD_ENTRIES).first(1), IMAGE_LO, IMAGE_HI), "guards: one entry alone");
  for (size_t index = 0; index < EFFECT_GUARD_ENTRIES.size(); ++index) {
    *at(EFFECT_GUARD_ENTRIES[index]) = 0xe8;
    Expect(!LoaderHooked(EFFECT_GUARD_ENTRIES, IMAGE_LO, IMAGE_HI), "guards: one entry without the jump");
    *at(EFFECT_GUARD_ENTRIES[index]) = 0xe9;
    for (const size_t byte : {size_t{0}, size_t{1}, size_t{10}, size_t{11}}) {
      ways_in[index][byte] ^= 1;
      Expect(!LoaderHooked(EFFECT_GUARD_ENTRIES, IMAGE_LO, IMAGE_HI), "guards: a way in that is not mov rax, imm64; jmp rax");
      ways_in[index][byte] ^= 1;
    }
    // A plugin's hook through the loader has the same shape and targets its own DLL.
    uint64_t target = 0;
    std::memcpy(&target, ways_in[index] + 2, sizeof(target));
    for (const uint64_t outside : {uint64_t{IMAGE_HI}, uint64_t{IMAGE_LO} - 1, uint64_t{0x7ff800001000}}) {
      std::memcpy(ways_in[index] + 2, &outside, sizeof(outside));
      Expect(!LoaderHooked(EFFECT_GUARD_ENTRIES, IMAGE_LO, IMAGE_HI), "guards: a target outside the loader's image");
    }
    for (const uint64_t inside : {uint64_t{IMAGE_LO}, uint64_t{IMAGE_HI} - 1}) {
      std::memcpy(ways_in[index] + 2, &inside, sizeof(inside));
      Expect(LoaderHooked(EFFECT_GUARD_ENTRIES, IMAGE_LO, IMAGE_HI), "guards: the image's first and last byte are inside");
    }
    std::memcpy(ways_in[index] + 2, &target, sizeof(target));
  }
  Expect(LoaderHooked(EFFECT_GUARD_ENTRIES, IMAGE_LO, IMAGE_HI), "guards: all five again");

  // The walk hook. The game's walk is GameWalk, its key compare (at the game's address) jumps to KeyEqual.
  static_assert(EffectsModeOf(0) == EFFECTS_OFF && EffectsModeOf(CORE_EFFECTS | CORE_EFFECTS_AUTO) == EFFECTS_AUTOMATIC
                && EffectsModeOf(CORE_EFFECTS) == EFFECTS_ON && EffectsModeOf(CORE_EFFECTS_AUTO) == EFFECTS_OFF
                && EffectsModeOf(~(CORE_EFFECTS | CORE_EFFECTS_AUTO)) == EFFECTS_OFF
                && (CORE_WANTED_DEFAULT & (CORE_EFFECTS | CORE_EFFECTS_AUTO)) == 0);
  constexpr uint32_t OTHER_BITS = CORE_BATTLE | CORE_RENDER | CORE_STEP | CORE_SPLIT;
  constexpr uint32_t AUTOMATIC_BITS = CORE_EFFECTS | CORE_EFFECTS_AUTO;
  constexpr uint8_t UNTOUCHED = 0x5a;  // a value no store writes
  const LoaderApi api = MakeApi("", GAME_HI_MIN);
  loader = &api;
  originals[LABEL_EFFECT_WALK_ENTRY] = reinterpret_cast<void*>(&GameWalk);
  write_jump(at(EFFECT_KEY_EQUAL), reinterpret_cast<uint64_t>(&KeyEqual));
  auto* dead =
      static_cast<const uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS));
  uint8_t* flag = at(EFFECT_FLAG);
  std::array<FakeNode, EFFECT_NODE_LIMIT + 1> nodes = {};
  for (FakeNode& node : nodes) {
    node.Build(dead);
  }
  // A fresh start of the game: the loader left `loader_value`, the flag holds UNTOUCHED (so that a store shows).
  const auto start = [&](uint32_t wanted, uint8_t loader_value) {
    effect_walk = {.loader_value = loader_value, .stored = loader_value};
    effect_walk_thread.store(0);
    effect_thread_logged.store(false);
    effects_offered.store(true);
    block.core_wanted.store(wanted);
    log_lines.clear();
    *flag = UNTOUCHED;
    for (FakeNode& node : nodes) {
      node.SetCapacity(5);
      node.SetInstances(7);
    }
  };
  // One walk of nodes[index]; true when the game's walk ran once with the hook's arguments. walk_record.flag is the
  // flag the game's update would then read.
  const auto walk = [&](size_t index) {
    const uint32_t calls_before = walk_record.calls;
    EffectWalkEntry(nodes[index].node.data(), 1.25f, -7.5f);
    return walk_record.calls == calls_before + 1 && walk_record.node == nodes[index].node.data()
           && walk_record.first_float == 1.25f && walk_record.second_float == -7.5f;
  };
  const auto walk_with = [&](size_t index, uint32_t instances) {
    nodes[index].SetInstances(instances);
    return walk(index);
  };

  // Not offered: the game's walk alone, in every mode.
  for (const uint32_t wanted : {OTHER_BITS, OTHER_BITS | AUTOMATIC_BITS, OTHER_BITS | CORE_EFFECTS}) {
    start(wanted, 0);
    effects_offered.store(false);
    Expect(walk_with(0, 30000) && *flag == UNTOUCHED && effect_walk.walks == 0
               && LoggedCount("renodx_attila64: EFFECT_WALK_ENTRY: first call (thread ") == ((wanted == OTHER_BITS) ? 1u : 0u)
               && log_lines.size() == ((wanted == OTHER_BITS) ? 1u : 0u),
           "walk: not offered, the game's walk alone and no store; its first call's line once");
  }

  // Off (no bit, or the qualifier alone): never a store, also after the loader's own switch flips the byte.
  for (const uint32_t wanted : {OTHER_BITS, OTHER_BITS | CORE_EFFECTS_AUTO}) {
    start(wanted, 1);
    Expect(walk_with(0, 30000) && *flag == UNTOUCHED && walk_record.flag == UNTOUCHED, "walk: Off stores nothing");
    *flag = 0;
    Expect(walk(0) && walk(1) && *flag == 0 && log_lines.empty(), "walk: Off leaves another writer's flip alone");
  }

  // On: 1 at the first walk even when the plugin's last store equals it (the byte may have been changed meanwhile),
  // then no store while nothing changes. Off after On: the loader's value once.
  for (const uint8_t loader_value : {uint8_t{0}, uint8_t{1}}) {
    start(OTHER_BITS | CORE_EFFECTS, loader_value);
    Expect(walk(0) && walk_record.flag == 1 && *flag == 1, "walk: On stores 1 at its first walk");
    *flag = UNTOUCHED;
    Expect(walk(0) && walk(1) && *flag == UNTOUCHED, "walk: On stores once");
    block.core_wanted.fetch_and(~CORE_EFFECTS);
    Expect(walk(0) && walk_record.flag == ((loader_value == 1) ? UNTOUCHED : 0), "walk: Off after On puts the loader's value back");
    *flag = UNTOUCHED;
    Expect(walk(0) && *flag == UNTOUCHED && log_lines.empty() && block.core_wanted.load() == OTHER_BITS,
           "walk: Off after On stores at most once");
  }

  // A walk on another thread than the first offered walk's: the game's walk alone, the flag and the state as they
  // are, one line. The first thread's walks go on as before.
  for (const uint32_t wanted : {OTHER_BITS | CORE_EFFECTS, OTHER_BITS | AUTOMATIC_BITS}) {
    start(wanted, 0);
    Expect(walk_with(0, 30000) && walk_record.flag == 1 && effect_walk_thread.load() == GetCurrentThreadId(),
           "thread: the first offered walk's thread is the walks' thread");
    block.core_wanted.fetch_and(~CORE_EFFECTS);
    const EffectWalk before = effect_walk;
    *flag = UNTOUCHED;
    bool other_walks = false;
    std::thread([&] { other_walks = walk(0) && walk(1) && walk_record.flag == UNTOUCHED; }).join();
    Expect(other_walks && *flag == UNTOUCHED && effect_walk.walks == before.walks && effect_walk.mode == before.mode
               && effect_walk.stored == 1 && LoggedCount("renodx_attila64: parallel effects: a walk on thread ") == 1
               && LoggedCount(": left to the game there") == 1,
           "thread: a walk on another thread is the game's alone, with one line");
    Expect(walk(0) && walk_record.flag == 0, "thread: the walks' thread goes on (Off after On here)");
  }
  // Not offered, no thread is taken.
  start(OTHER_BITS | CORE_EFFECTS, 0);
  effects_offered.store(false);
  std::thread([&] { walk(0); }).join();
  effects_offered.store(true);
  Expect(effect_walk_thread.load() == 0 && walk(0) && walk_record.flag == 1 && effect_walk_thread.load() == GetCurrentThreadId(),
         "thread: only an offered walk sets the walks' thread");

  // Entries age by Automatic's walks only: after a long stretch in On, a new node does not take a live node's entry.
  start(OTHER_BITS | AUTOMATIC_BITS, 0);
  Expect(walk(0) && walk(1) && walk(2) && walk(3), "ageing: four nodes in Automatic");
  block.core_wanted.fetch_and(~CORE_EFFECTS_AUTO);
  bool on_walks = true;
  for (uint64_t count = 0; count < 2 * EFFECT_NODE_STALE_WALKS; ++count) {
    on_walks = on_walks && walk(0);
  }
  block.core_wanted.fetch_or(CORE_EFFECTS_AUTO);
  *flag = UNTOUCHED;
  Expect(on_walks && walk(4) && *flag == UNTOUCHED && effect_walk.nodes[0].node == nodes[0].node.data()
             && Logged("more than 4 nodes"),
         "ageing: walks in On age no entry");

  // Automatic, the sum on the fake table: the two types' instances, without a read of the empty and the deleted
  // slot's values (they would fault) or of the null one's.
  start(OTHER_BITS | AUTOMATIC_BITS, 0);
  Expect(walk_with(0, 19999) && walk_record.flag == 0 && log_lines.empty(),
         "walk: Automatic stores at its first walk even when the target is the last store; 19999 is serial");
  *flag = UNTOUCHED;
  Expect(walk(0) && *flag == UNTOUCHED, "walk: Automatic, nothing changed, no store");
  Expect(walk_with(0, 20000) && walk_record.flag == 1
             && Logged("renodx_attila64: parallel effects: node 0 on (20000 instances, from 20000), table capacity 5, sum ")
             && log_lines.size() == 1 && log_lines[0].ends_with(" us"),
         "walk: 20000 instances turn the node on; the first change's line has the capacity and the sum's time");
  // The same node again in the same frame: the same sum, no second store and no line.
  *flag = UNTOUCHED;
  Expect(walk(0) && *flag == UNTOUCHED && log_lines.size() == 1 && effect_walk.nodes[0].parallel,
         "walk: a node walked twice in a frame, no second store and no line");
  *flag = 1;
  Expect(walk_with(0, 16000) && *flag == 1 && walk_with(0, 19999) && *flag == 1 && log_lines.size() == 1,
         "walk: on stays on from 16000");
  Expect(walk_with(0, 15999) && walk_record.flag == 0
             && Logged("renodx_attila64: parallel effects: node 0 off (15999, below 16000)") && log_lines.size() == 2
             && LoggedCount("table capacity") == 1,
         "walk: below 16000 turns the node off; only the first change's line has the capacity");
  Expect(walk_with(0, 16000) && walk_record.flag == 0 && walk_with(0, 19999) && walk_record.flag == 0 && log_lines.size() == 2,
         "walk: off stays off below 20000");
  Expect(walk_with(0, 20001) && walk_record.flag == 1 && LoggedCount("node 0 on (20001 instances, from 20000)") == 1
             && log_lines.size() == 3,
         "walk: on again from 20000");
  // The floor: with these numbers it lies below both thresholds, so 255 and 256 are both serial.
  static_assert(AUTO_FLOOR == 256 && AUTO_FLOOR < AUTO_OFF && AUTO_OFF < AUTO_ON);
  Expect(walk_with(0, 256) && walk_record.flag == 0 && walk_with(0, 255) && walk_record.flag == 0 && log_lines.size() == 4,
         "walk: 256 and 255 instances are serial");

  // A table above the slot limit: no sum (its slots are the dead page here), the node's state and the flag kept, one
  // line. Back under the limit the node decides again.
  start(OTHER_BITS | AUTOMATIC_BITS, 0);
  Expect(walk_with(0, 20000) && walk_record.flag == 1 && effect_walk.nodes[0].parallel, "walk: a node that is on");
  std::memcpy(nodes[0].node.data() + EFFECT_TABLE_SLOTS, &dead, sizeof(dead));
  nodes[0].SetCapacity(TABLE_SLOT_LIMIT + 1);
  *flag = UNTOUCHED;
  Expect(walk(0) && walk(0) && *flag == UNTOUCHED && effect_walk.nodes[0].parallel
             && LoggedCount("renodx_attila64: parallel effects: a table of 4097 slots (above 4096) is not summed") == 1
             && log_lines.size() == 2,
         "walk: 4097 slots, no sum, the state and the flag kept, one line");
  nodes[0].Build(dead);
  Expect(walk_with(0, 100) && walk_record.flag == 0 && Logged("node 0 off (100, below 16000)"),
         "walk: a table back under the limit is summed again");
  // A mode's first store waits for a walk that decides.
  start(OTHER_BITS | AUTOMATIC_BITS, 1);
  nodes[0].SetCapacity(TABLE_SLOT_LIMIT + 1);
  Expect(walk(0) && *flag == UNTOUCHED && effect_walk.mode == EFFECTS_OFF, "walk: a walk that cannot decide stores nothing");
  nodes[0].SetCapacity(TABLE_SLOT_LIMIT);
  std::array<uint8_t, TABLE_SLOT_LIMIT * EFFECT_SLOT_BYTES> big_table = {};
  for (size_t index = 0; index < TABLE_SLOT_LIMIT; ++index) {
    std::memcpy(big_table.data() + (index * EFFECT_SLOT_BYTES), ((index % 2 == 0) ? &EMPTY_KEY : &DELETED_KEY), sizeof(Key));
  }
  const uint8_t* big_slots = big_table.data();
  std::memcpy(nodes[0].node.data() + EFFECT_TABLE_SLOTS, &big_slots, sizeof(big_slots));
  Expect(walk(0) && walk_record.flag == 0 && effect_walk.mode == EFFECTS_AUTOMATIC && log_lines.size() == 1,
         "walk: 4096 slots are summed; the mode's first store comes with the first walk that decides");
  nodes[0].Build(dead);

  // Two nodes on opposite sides of the threshold: each walk sets its node's arm, one line per node's change.
  start(OTHER_BITS | AUTOMATIC_BITS, 0);
  nodes[0].SetInstances(25000);
  nodes[1].SetInstances(300);
  bool arms = true;
  for (int frame = 0; frame < 8; ++frame) {
    arms = arms && walk(0) && walk_record.flag == 1 && walk(1) && walk_record.flag == 0;
  }
  Expect(arms && log_lines.size() == 1 && Logged("node 0 on (25000 instances, from 20000), table capacity 5"),
         "walk: two nodes, each walk with its own arm, one line");
  Expect(walk_with(1, 20000) && walk_record.flag == 1 && walk(0) && walk_record.flag == 1 && log_lines.size() == 2
             && Logged("renodx_attila64: parallel effects: node 1 on (20000 instances, from 20000)") && LoggedCount("capacity") == 1,
         "walk: the second node's own change, its own line");

  // A fifth node: its flag is left as it is, one line; the four keep their arms. After EFFECT_NODE_STALE_WALKS walks
  // without the first node, the fifth takes its entry (a node dies with its battle).
  start(OTHER_BITS | AUTOMATIC_BITS, 0);
  nodes[4].SetInstances(30000);
  Expect(walk(0) && walk(1) && walk(2) && walk(3) && walk_record.flag == 0, "walk: four nodes");
  *flag = UNTOUCHED;
  Expect(walk(4) && walk(4) && *flag == UNTOUCHED
             && LoggedCount("renodx_attila64: parallel effects: more than 4 nodes, the others' flag is left as it is") == 1
             && log_lines.size() == 1,
         "walk: a fifth node, the flag untouched and one line");
  Expect(walk_with(3, 20000) && walk_record.flag == 1 && Logged("node 3 on"), "walk: the four nodes still decide");
  bool others = true;
  while (effect_walk.walks - effect_walk.nodes[0].walked <= EFFECT_NODE_STALE_WALKS) {
    *flag = UNTOUCHED;
    others = others && walk(4) && *flag == UNTOUCHED && walk(1) && walk(2);
  }
  Expect(others && walk(4) && walk_record.flag == 1 && effect_walk.nodes[0].node == nodes[4].node.data()
             && Logged("node 0 on (30000 instances, from 20000)") && LoggedCount("more than 4 nodes") == 1,
         "walk: an entry no walk used for long goes to the next new node");

  // The stops' changes, each one atomic operation on a word that holds the other parts' bits: only bits 4 and 5 move,
  // and the mode is the new stop's at once.
  start(OTHER_BITS, 0);
  const auto wanted_is = [&](uint32_t effect_bits, EffectsMode mode) {
    return block.core_wanted.load() == (OTHER_BITS | effect_bits) && EffectsModeOf(block.core_wanted.load()) == mode;
  };
  block.core_wanted.fetch_or(CORE_EFFECTS | CORE_EFFECTS_AUTO);
  Expect(wanted_is(AUTOMATIC_BITS, EFFECTS_AUTOMATIC) && walk(0) && effect_walk.mode == EFFECTS_AUTOMATIC, "stops: Off to Automatic");
  block.core_wanted.fetch_and(~CORE_EFFECTS_AUTO);
  Expect(wanted_is(CORE_EFFECTS, EFFECTS_ON) && walk(0) && walk_record.flag == 1, "stops: Automatic to On");
  block.core_wanted.fetch_or(CORE_EFFECTS_AUTO);
  Expect(wanted_is(AUTOMATIC_BITS, EFFECTS_AUTOMATIC) && walk(0) && walk_record.flag == 0, "stops: On to Automatic");
  block.core_wanted.fetch_and(~(CORE_EFFECTS | CORE_EFFECTS_AUTO));
  Expect(wanted_is(0, EFFECTS_OFF) && walk(0) && walk_record.flag == 0, "stops: Automatic to Off");
  block.core_wanted.fetch_or(CORE_EFFECTS);
  Expect(wanted_is(CORE_EFFECTS, EFFECTS_ON) && walk(0) && walk_record.flag == 1, "stops: Off to On");
  block.core_wanted.fetch_and(~CORE_EFFECTS);
  Expect(wanted_is(0, EFFECTS_OFF) && walk(0) && walk_record.flag == 0, "stops: On to Off");

  // The plugin's EffectsMode key at init.
  const auto key_gives = [&](uint32_t before, int key, uint32_t after, const char* mode_line) {
    block.core_wanted.store(OTHER_BITS | before);
    log_lines.clear();
    ApplyEffectsModeKey(&block, key);
    return block.core_wanted.load() == (OTHER_BITS | after) && Logged(mode_line)
           && log_lines.size() == ((key == 7) ? 2u : 1u);
  };
  Expect(key_gives(0, -1, 0, "renodx_attila64: parallel effects mode Off (ini EffectsMode)")
             && key_gives(AUTOMATIC_BITS, -1, AUTOMATIC_BITS, "renodx_attila64: parallel effects mode Automatic (ini EffectsMode)"),
         "key: absent, no change");
  Expect(key_gives(AUTOMATIC_BITS, 0, 0, "parallel effects mode Off (ini EffectsMode)"), "key: 0 clears both bits");
  Expect(key_gives(0, 1, AUTOMATIC_BITS, "parallel effects mode Automatic (ini EffectsMode)"), "key: 1 sets both bits");
  Expect(key_gives(0, 2, CORE_EFFECTS, "renodx_attila64: parallel effects mode On (ini EffectsMode)")
             && key_gives(AUTOMATIC_BITS, 2, CORE_EFFECTS, "parallel effects mode On (ini EffectsMode)"),
         "key: 2 sets the wanted bit alone");
  Expect(key_gives(0, 7, 0, "parallel effects mode Off (ini EffectsMode)")
             && Logged("renodx_attila64: parallel effects: EffectsMode=7 is not 0, 1 or 2 (no change)"),
         "key: an unknown value changes nothing and says so");

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
