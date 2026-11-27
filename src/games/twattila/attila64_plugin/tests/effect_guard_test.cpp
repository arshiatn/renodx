/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The parallel effects row without a game: the check that an entry holds the loader's own hook (patcher.hpp
// LoaderHooked), on hooks written as the loader writes them in both of its layouts.
// effect_guard_test.exe; exit code = failures.

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

#include "../patcher.hpp"
#include "fake_loader.hpp"

int wmain() {
  using namespace twattila::attila64_plugin;
  using namespace twattila::attila64_plugin::test;

  if (!ReserveGame(PAGE_EXECUTE_READWRITE)) {
    std::printf("FAIL: the game range 0x400000..0x527d000 is taken\n");
    return 1;
  }
  const auto at = [](uintptr_t address) { return reinterpret_cast<uint8_t*>(address); };

  // The loader's entry hook at `entry`: a jmp rel32 to its way in (`way_in_offset` bytes into a 32-byte block near the
  // game), which is mov rax, target; jmp rax. Returns the way in.
  const auto write_hook = [&](uintptr_t entry, size_t way_in_offset, uint64_t target) {
    uint8_t* way_in = static_cast<uint8_t*>(FakeCodeNearGame(32)) + way_in_offset;
    way_in[0] = 0x48;
    way_in[1] = 0xb8;
    std::memcpy(way_in + 2, &target, sizeof(target));
    way_in[10] = 0xff;
    way_in[11] = 0xe0;
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

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
