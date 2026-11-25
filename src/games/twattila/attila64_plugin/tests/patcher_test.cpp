/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The plugin's unit test on a fake game image at the game's addresses (no game file needed): identity, the site checks
// and their refusals, the loader's hook and the re-check, the stack-check rule, the shipped hook bodies on synthetic
// game functions with the add-on's callbacks, the callback drain, and the built DLL's init.
// patcher_test.exe <renodx_attila64.dll>; exit code = failures.

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <thread>
#include <tuple>
#include <vector>

#include "../hooks.hpp"
#include "../patcher.hpp"
#include "fake_loader.hpp"

namespace {

using namespace twattila::attila64_plugin;
using namespace twattila::attila64_plugin::test;

// Synthetic game functions, Linux calling convention, with the real sites' prologues.
inline constexpr uintptr_t PACK_FUNCTION = 0x600000;  // stores its 8 arguments at PACK_RECORD, counts, returns true
inline constexpr uintptr_t PACK_RECORD = 0x610000;
inline constexpr uintptr_t DRAW_FUNCTION = 0x620000;  // stores its 3 arguments at DRAW_RECORD, counts
inline constexpr uintptr_t DRAW_RECORD = 0x630000;
inline constexpr uintptr_t RULE_SITE = 0x640000;

struct PackRecord {
  std::array<uint64_t, 8> arguments;
  uint32_t calls;
};
struct DrawRecord {
  std::array<uint64_t, 3> arguments;
  uint32_t calls;
};

std::vector<uint8_t> PackFunctionBytes() {
  std::vector<uint8_t> code = {0x55, 0x41, 0x57, 0x41, 0x56, 0x48, 0xb8};  // push rbp / r15 / r14; mov rax, PACK_RECORD
  for (int i = 0; i < 8; ++i) code.push_back(static_cast<uint8_t>(PACK_RECORD >> (8 * i)));
  code.insert(code.end(), {
                              0x48, 0x89, 0x38,              // mov [rax], rdi
                              0x48, 0x89, 0x70, 0x08,        // mov [rax+8], rsi
                              0x48, 0x89, 0x50, 0x10,        // mov [rax+16], rdx
                              0x48, 0x89, 0x48, 0x18,        // mov [rax+24], rcx
                              0x4c, 0x89, 0x40, 0x20,        // mov [rax+32], r8
                              0x4c, 0x89, 0x48, 0x28,        // mov [rax+40], r9
                              0x4c, 0x8b, 0x74, 0x24, 0x20,  // mov r14, [rsp+32]: the 7th argument
                              0x4c, 0x89, 0x70, 0x30,        // mov [rax+48], r14
                              0x4c, 0x8b, 0x74, 0x24, 0x28,  // mov r14, [rsp+40]: the 8th
                              0x4c, 0x89, 0x70, 0x38,        // mov [rax+56], r14
                              0xff, 0x40, 0x40,              // inc dword [rax+64]
                              0x41, 0x5e, 0x41, 0x5f, 0x5d,  // pop r14 / r15 / rbp
                              0xb0, 0x01, 0xc3,              // mov al, 1; ret
                          });
  return code;
}

std::vector<uint8_t> DrawFunctionBytes() {
  std::vector<uint8_t> code = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x48, 0xb8};  // push r15 / r14 / r13; mov rax, ..
  for (int i = 0; i < 8; ++i) code.push_back(static_cast<uint8_t>(DRAW_RECORD >> (8 * i)));
  code.insert(code.end(), {
                              0x48, 0x89, 0x38,                    // mov [rax], rdi
                              0x48, 0x89, 0x70, 0x08,              // mov [rax+8], rsi
                              0x48, 0x89, 0x50, 0x10,              // mov [rax+16], rdx
                              0xff, 0x40, 0x18,                    // inc dword [rax+24]
                              0x41, 0x5d, 0x41, 0x5e, 0x41, 0x5f,  // pop r13 / r14 / r15
                              0xc3,
                          });
  return code;
}

// A site record for code in the fake image, its window read from memory as the generator would.
Site SiteAt(uintptr_t address, uint8_t length, Row row, SiteLabel label) {
  Site site = {.address = static_cast<uint32_t>(address), .length = length, .row = static_cast<uint8_t>(row), .label = label};
  std::memcpy(site.window.data(), reinterpret_cast<const void*>(address - SITE_MARGIN), (2 * SITE_MARGIN) + length);
  return site;
}

// The fake view: its vtable's pass-kind slot returns `kind`.
struct FakeView {
  void* const* vtable;
  uint32_t kind;
};
ATTILA64_GAME_CALL uint32_t FakeViewKind(void* view) noexcept { return static_cast<FakeView*>(view)->kind; }

// The add-on's side.
std::atomic<uint32_t> before_pack_calls = 0;
uint32_t pass_begin_calls = 0;
uint32_t pass_end_calls = 0;
uint32_t last_kind = 0;
bool end_measure_ok = false;
PassAction next_action = PASS_DRAW;
const Callbacks* swap_in_begin = nullptr;  // stored into the block by pass_begin when set (a re-register in between)
std::atomic_bool drained = false;
std::atomic<uint32_t> calls_after_drain = 0;

void BeforePack() noexcept {
  if (drained.load()) ++calls_after_drain;
  ++before_pack_calls;
}

PassAction PassBegin(uint32_t pass_kind, PassMeasure* measure) noexcept {
  ++pass_begin_calls;
  last_kind = pass_kind;
  *measure = {.sun_binds_before = 11, .other_binds_before = 22, .sun_at_start = 33, .reserved = 0};
  if (swap_in_begin != nullptr) block.callbacks.store(swap_in_begin);
  return next_action;
}

void PassEnd(uint32_t pass_kind, const PassMeasure& measure) noexcept {
  ++pass_end_calls;
  end_measure_ok = pass_kind == last_kind && measure.sun_binds_before == 11 && measure.other_binds_before == 22
                   && measure.sun_at_start == 33;
}

const Callbacks CALLBACKS = {.before_pack = &BeforePack, .pass_begin = &PassBegin, .pass_end = &PassEnd};
const Callbacks OTHER_CALLBACKS = CALLBACKS;

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (!ReserveGame(PAGE_EXECUTE_READWRITE)) {
    std::printf("FAIL: the game range 0x400000..0x527d000 is taken\n");
    return 1;
  }
  char exe_dir[MAX_PATH];
  GetModuleFileNameA(nullptr, exe_dir, MAX_PATH);
  *std::strrchr(exe_dir, '\\') = '\0';
  LoaderApi api = MakeApi(exe_dir, GAME_HI_MIN);
  loader = &api;

  const auto write_game = [] {
    std::memcpy(reinterpret_cast<void*>(GAME_LO), ELF_MAGIC.data(), ELF_MAGIC.size());
    std::memcpy(reinterpret_cast<void*>(BUILD_ID_NOTE_ADDRESS), BUILD_ID_NOTE.data(), BUILD_ID_NOTE.size());
    for (const Site& site : SITES) {
      std::memcpy(reinterpret_cast<void*>(site.address - SITE_MARGIN), site.window.data(), (2 * SITE_MARGIN) + site.length);
    }
  };
  write_game();
  const auto at = [](uintptr_t address) { return reinterpret_cast<uint8_t*>(address); };

  // Identity.
  Expect(CheckIdentity(api) == STATUS_READY, "identity: accepted");
  {
    LoaderApi other = api;
    other.api_version = 0;
    Expect(CheckIdentity(other) == STATUS_WRONG_LOADER, "identity: api version 0");
    other = api;
    other.game_lo = 0x10000;
    Expect(CheckIdentity(other) == STATUS_WRONG_LOADER, "identity: another game start");
    other = api;
    other.game_hi = GAME_HI_MIN - 1;
    Expect(CheckIdentity(other) == STATUS_WRONG_LOADER, "identity: game range too short");
    other = api;
    other.api_version = 2;
    other.game_hi = GAME_HI_MIN + 0x1000;
    Expect(CheckIdentity(other) == STATUS_READY, "identity: a newer api and a longer range");
  }
  *at(BUILD_ID_NOTE_ADDRESS + 35) ^= 1;
  Expect(CheckIdentity(api) == STATUS_WRONG_GAME, "identity: another build id");
  *at(BUILD_ID_NOTE_ADDRESS + 35) ^= 1;
  *at(GAME_LO + 1) ^= 1;
  Expect(CheckIdentity(api) == STATUS_WRONG_GAME, "identity: no ELF magic");
  *at(GAME_LO + 1) ^= 1;

  // Site checks on the real sites' windows.
  for (const Site& site : SITES) {
    Expect(CheckSite(site, FOREIGN).refusal == REFUSAL_NONE, "site: accepted with the shipped foreign table");
    for (const int64_t offset : {int64_t{-16}, int64_t{-1}, int64_t{site.length}, int64_t{site.length} + 15}) {
      *at(site.address + offset) ^= 0x5a;
      Expect(CheckSite(site, {}).refusal == REFUSAL_NEIGHBOUR, "site: a neighbour byte changed");
      *at(site.address + offset) ^= 0x5a;
    }
    *at(site.address + site.length - 1) ^= 0x5a;
    Expect(CheckSite(site, {}).refusal == REFUSAL_DIFFERS, "site: its own byte changed");
    *at(site.address + site.length - 1) ^= 0x5a;
    Site moved = site;
    ++moved.address;
    Expect(CheckSite(moved, {}).refusal != REFUSAL_NONE, "site: moved by one");

    const auto foreign_at = [&](uint32_t address, uint8_t length, ForeignKind kind) {
      const std::array<Foreign, 1> entry = {{{.address = address, .length = length, .owner = OWNER_OTHER, .kind = kind}}};
      return CheckSite(site, entry);
    };
    const SiteCheck before = foreign_at(site.address - 8, 5, FOREIGN_RANGE);
    Expect(before.refusal == REFUSAL_FOREIGN && before.owner == OWNER_OTHER, "foreign: a patch 8 bytes before");
    Expect(foreign_at(site.address + site.length + 8, 5, FOREIGN_RANGE).refusal == REFUSAL_FOREIGN, "foreign: 8 bytes after");
    Expect(foreign_at(site.address + site.length + 17, 5, FOREIGN_RANGE).refusal == REFUSAL_NONE, "foreign: 17 bytes after");
    Expect(foreign_at(site.address - 16 - 5, 5, FOREIGN_RANGE).refusal == REFUSAL_NONE, "foreign: ends where the window starts");
    Expect(foreign_at(site.address + 2, 0, FOREIGN_TARGET).refusal == REFUSAL_FOREIGN, "foreign: a return target inside");
    Expect(foreign_at(site.address, 0, FOREIGN_TARGET).refusal == REFUSAL_NONE, "foreign: a target at the entry");
    Expect(foreign_at(site.address + site.length, 0, FOREIGN_TARGET).refusal == REFUSAL_NONE, "foreign: a target after");
  }

  // Apply, snapshot, re-check; the loader's two failures.
  {
    const Site& site = SITES[ROW_BONE_FLUSH];
    AppliedSite applied;
    hook_mode = HOOK_NO_MEMORY;
    Expect(ApplySite(api, site, REPLACEMENTS[ROW_BONE_FLUSH], &applied) == REFUSAL_NO_MEMORY, "apply: no memory");
    hook_mode = HOOK_WRITE_FAILS;
    Expect(ApplySite(api, site, REPLACEMENTS[ROW_BONE_FLUSH], &applied) == REFUSAL_WRITE_FAILED, "apply: write failed");
    hook_mode = HOOK_NORMAL;
    write_game();
    Expect(ApplySite(api, site, REPLACEMENTS[ROW_BONE_FLUSH], &applied) == REFUSAL_NONE && applied.original != nullptr,
           "apply: hooked");
    Expect(applied.bytes[0] == 0xe9 && SiteUnchanged(site, applied), "apply: snapshot of the jump");
    Expect(CheckSite(site, {}).refusal == REFUSAL_DIFFERS, "apply: a second check sees our bytes");
    *at(site.address + 4) ^= 1;
    Expect(!SiteUnchanged(site, applied), "apply: a later change is seen");
    write_game();
  }

  // The stack-check rule: the windows hold its result.
  {
    constexpr std::array<uint8_t, 16> before = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x64,
                                                0x48, 0x8b, 0x04, 0x25, 0x28, 0x00, 0x00, 0x00};
    constexpr std::array<uint8_t, 9> left_alone = {0x64, 0x48, 0x8b, 0x04, 0x25, 0x30, 0x00, 0x00, 0x00};
    std::memcpy(at(RULE_SITE - 16), before.data(), before.size());
    std::memcpy(at(RULE_SITE), SITES[0].window.data() + SITE_MARGIN, 5);
    std::memcpy(at(RULE_SITE + 5), left_alone.data(), left_alone.size());
    Site site = SiteAt(RULE_SITE, 5, ROW_BONE_FLUSH, LABEL_PACK_ENTRY);
    site.window[7] = 0x65;
    Expect(CheckSite(site, {}).refusal == REFUSAL_NEIGHBOUR, "stack rule: the raw file bytes differ from the window");
    Expect(ApplyStackRule(at(RULE_SITE - 16), at(RULE_SITE + 16 + 5)) == 1, "stack rule: one read rewritten");
    Expect(CheckSite(site, {}).refusal == REFUSAL_NONE, "stack rule: the rewritten bytes match");
  }

  // The shipped hook bodies on synthetic game functions.
  const std::vector<uint8_t> pack_code = PackFunctionBytes();
  const std::vector<uint8_t> draw_code = DrawFunctionBytes();
  std::memcpy(at(PACK_FUNCTION), pack_code.data(), pack_code.size());
  std::memcpy(at(DRAW_FUNCTION), draw_code.data(), draw_code.size());
  for (const auto& [address, row, label, length] :
       {std::tuple{PACK_FUNCTION, ROW_BONE_FLUSH, LABEL_PACK_ENTRY, uint8_t{5}},
        std::tuple{DRAW_FUNCTION, ROW_PASS_DRAW, LABEL_VIEW_DRAW_ENTRY, uint8_t{6}}}) {
    const Site site = SiteAt(address, length, row, label);
    Expect(CheckSite(site, {}).refusal == REFUSAL_NONE, "hooks: synthetic site accepted");
    AppliedSite applied;
    Expect(ApplySite(api, site, REPLACEMENTS[row], &applied) == REFUSAL_NONE, "hooks: synthetic site hooked");
    originals[row] = applied.original;
  }
  const auto* pack_record = reinterpret_cast<const PackRecord*>(PACK_RECORD);
  const auto* draw_record = reinterpret_cast<const DrawRecord*>(DRAW_RECORD);
  const auto pack = reinterpret_cast<PackFn>(PACK_FUNCTION);
  const auto draw = reinterpret_cast<ViewDrawFn>(DRAW_FUNCTION);
  const auto pack_once = [&] {
    uint32_t cursor = 0;
    uint32_t written = 0;
    const bool result = pack(reinterpret_cast<void*>(0x1111), reinterpret_cast<void*>(0x2222), true,
                             reinterpret_cast<void*>(0x4444), 0x5555, &cursor, 0x7777, &written);
    return result && pack_record->arguments[0] == 0x1111 && pack_record->arguments[1] == 0x2222
           && (pack_record->arguments[2] & 0xff) == 1 && pack_record->arguments[3] == 0x4444
           && static_cast<uint32_t>(pack_record->arguments[4]) == 0x5555
           && pack_record->arguments[5] == reinterpret_cast<uint64_t>(&cursor)
           && static_cast<uint32_t>(pack_record->arguments[6]) == 0x7777
           && pack_record->arguments[7] == reinterpret_cast<uint64_t>(&written);
  };

  // Bone flush: bit clear, bit set without callbacks, bit set with them.
  Expect(pack_once() && pack_record->calls == 1 && before_pack_calls == 0, "pack: bit clear, original with its arguments");
  Expect(Logged("PACK_ENTRY: first call"), "pack: first-call line");
  block.hooks_wanted.store(RowBit(ROW_BONE_FLUSH));
  Expect(pack_once() && pack_record->calls == 2 && before_pack_calls == 0, "pack: no callbacks registered");
  block.callbacks.store(&CALLBACKS);
  Expect(pack_once() && pack_record->calls == 3 && before_pack_calls == 1, "pack: before_pack, then the original");
  Expect(block.calls_in_flight.load() == 0, "pack: guard closed");

  // Pass draw.
  std::array<void*, VIEW_KIND_SLOT + 1> slots = {};
  slots[VIEW_KIND_SLOT] = reinterpret_cast<void*>(&FakeViewKind);
  FakeView view = {.vtable = slots.data(), .kind = 7};
  std::array<uint8_t, 0x40> node = {};
  node[NODE_ENABLED_OFFSET] = 1;
  const auto draw_once = [&] {
    draw(node.data(), reinterpret_cast<void*>(0x2222), &view);
    return draw_record->arguments[0] == reinterpret_cast<uint64_t>(node.data()) && draw_record->arguments[1] == 0x2222
           && draw_record->arguments[2] == reinterpret_cast<uint64_t>(&view);
  };
  Expect(draw_once() && draw_record->calls == 1 && pass_begin_calls == 0, "draw: bit clear");
  block.hooks_wanted.store(RowBit(ROW_BONE_FLUSH) | RowBit(ROW_PASS_DRAW));
  node[NODE_ENABLED_OFFSET] = 0;
  Expect(draw_once() && draw_record->calls == 2 && pass_begin_calls == 0, "draw: a disabled node never reaches the add-on");
  node[NODE_ENABLED_OFFSET] = 1;
  next_action = PASS_SKIP;
  draw(node.data(), nullptr, &view);
  Expect(draw_record->calls == 2 && pass_begin_calls == 1 && last_kind == 7 && pass_end_calls == 0, "draw: skipped");
  next_action = PASS_DRAW;
  Expect(draw_once() && draw_record->calls == 3 && pass_begin_calls == 2 && pass_end_calls == 0, "draw: drawn");
  next_action = PASS_DRAW_MEASURED;
  view.kind = 9;
  Expect(draw_once() && draw_record->calls == 4 && pass_end_calls == 1 && end_measure_ok && last_kind == 9,
         "draw: measured, PassMeasure round trip");
  swap_in_begin = &OTHER_CALLBACKS;
  Expect(draw_once() && draw_record->calls == 5 && pass_begin_calls == 4 && pass_end_calls == 1,
         "draw: re-registered between begin and end, no end");
  swap_in_begin = nullptr;
  block.callbacks.store(&CALLBACKS);
  Expect(draw_once() && pass_end_calls == 2 && block.calls_in_flight.load() == 0, "draw: measured again, guard closed");
  Expect(Logged("VIEW_DRAW_ENTRY: first call"), "draw: first-call line");

  // Drain: a game thread packs while the add-on unregisters (store nullptr, then a bounded wait for no calls in flight).
  {
    std::atomic_bool running = true;
    std::thread game_thread([&] {
      while (running.load()) pack_once();
    });
    int undrained = 0;
    for (int cycle = 0; cycle < 200; ++cycle) {
      drained.store(false);
      block.callbacks.store(&CALLBACKS);
      std::this_thread::sleep_for(std::chrono::microseconds(200));
      block.callbacks.store(nullptr);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      // The first 0 seen ends the wait: a hook that enters after it loads nullptr and calls nothing.
      bool zero_seen = false;
      while (!zero_seen && std::chrono::steady_clock::now() < deadline) {
        zero_seen = block.calls_in_flight.load() == 0;
        if (!zero_seen) Sleep(0);
      }
      if (!zero_seen) ++undrained;
      drained.store(true);
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    running.store(false);
    game_thread.join();
    Expect(undrained == 0, "drain: every wait ended within 1 s");
    Expect(calls_after_drain.load() == 0, "drain: no callback after the wait");
    std::printf("  drain: %u before_pack calls over 200 cycles\n", before_pack_calls.load());
  }

  // The built DLL's init on the real sites.
  if (argc < 2) {
    Expect(false, "patcher_test.exe <renodx_attila64.dll>: no DLL given");
  } else {
    write_game();
    ExpectPluginDll(api, argv[1]);
  }

  std::printf("%s: %d failure(s)\n", (failures == 0) ? "PASS" : "FAIL", failures);
  return failures;
}
