/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The plugin's unit test on a fake game image at the game's addresses (no game file needed): identity, the site checks
// and their refusals, the loader's hook and the re-check, the stack-check rule, the hook bodies on synthetic game
// functions at the real sites (this file's copy of hooks.hpp and the built DLL's own) with the add-on's callbacks and
// one live battle-core reserve on this process's own CPU sets, the callback drain, and the built DLL's init with each
// refusal and with the RenderCore key (copies of the DLL under dll_cases\, a ReShade.ini next to this exe for the ini
// keys), and the text a batch tool looks for in the DLL file.
// patcher_test.exe <renodx_attila64.dll>; exit code = failures.

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "../hooks.hpp"
#include "../patcher.hpp"
#include "fake_loader.hpp"

namespace {

using namespace twattila::attila64_plugin;
using namespace twattila::attila64_plugin::test;

inline constexpr uintptr_t PACK_RECORD = 0x610000;  // the synthetic pack stores its 8 arguments here and counts
inline constexpr uintptr_t DRAW_RECORD = 0x630000;  // the synthetic draw stores its 3 arguments here and counts
inline constexpr uintptr_t RULE_SITE = 0x640000;
inline constexpr uintptr_t STEP_RECORD = 0x650000;  // the synthetic step stores what its task holds here and counts
inline constexpr uint64_t STEP_RESULT = 0x5eed0000c0de;

struct PackRecord {
  std::array<uint64_t, 8> arguments;
  uint32_t calls;
};
struct DrawRecord {
  std::array<uint64_t, 3> arguments;
  uint32_t calls;
};
struct StepRecord {
  uint64_t task_field;
  uint32_t calls;
};

// The synthetic game functions are the real sites' bytes (each window's site and its 16 bytes after) followed by these
// tails, Linux calling convention. The pack window ends inside mov [rsp+disp8], r9: its tail starts with the disp8.
// Pack: push rbp / r15 / r14 / r13 / r12 / rbx, sub rsp 0x128, so the 7th and 8th arguments sit at rsp+0x160 / 0x168.
inline constexpr std::array<uint8_t, 81> PACK_TAIL = {
    0x08,                                                  // the window's mov [rsp+8], r9
    0x48, 0xb8, 0x00, 0x00, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, PACK_RECORD
    0x48, 0x89, 0x38,                                      // mov [rax], rdi
    0x48, 0x89, 0x70, 0x08,                                // mov [rax+8], rsi
    0x48, 0x89, 0x50, 0x10,                                // mov [rax+16], rdx
    0x48, 0x89, 0x48, 0x18,                                // mov [rax+24], rcx
    0x4c, 0x89, 0x40, 0x20,                                // mov [rax+32], r8
    0x4c, 0x89, 0x48, 0x28,                                // mov [rax+40], r9
    0x4c, 0x8b, 0xb4, 0x24, 0x60, 0x01, 0x00, 0x00,        // mov r14, [rsp+0x160]: the 7th argument
    0x4c, 0x89, 0x70, 0x30,                                // mov [rax+48], r14
    0x4c, 0x8b, 0xb4, 0x24, 0x68, 0x01, 0x00, 0x00,        // mov r14, [rsp+0x168]: the 8th
    0x4c, 0x89, 0x70, 0x38,                                // mov [rax+56], r14
    0xff, 0x40, 0x40,                                      // inc dword [rax+64]
    0x48, 0x81, 0xc4, 0x28, 0x01, 0x00, 0x00,              // add rsp, 0x128
    0x5b, 0x41, 0x5c, 0x41, 0x5d, 0x41, 0x5e, 0x41, 0x5f, 0x5d,  // pop rbx / r12 / r13 / r14 / r15 / rbp
    0xb0, 0x01, 0xc3,                                      // mov al, 1; ret
};
// Draw: push r15 / r14 / r13 / r12 / rbx, sub rsp 0x130, mov r15, rdx, mov r14, rsi (all in the window).
inline constexpr std::array<uint8_t, 42> DRAW_TAIL = {
    0x48, 0xb8, 0x00, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, DRAW_RECORD
    0x48, 0x89, 0x38,                                      // mov [rax], rdi
    0x4c, 0x89, 0x70, 0x08,                                // mov [rax+8], r14 (rsi)
    0x4c, 0x89, 0x78, 0x10,                                // mov [rax+16], r15 (rdx)
    0xff, 0x40, 0x18,                                      // inc dword [rax+24]
    0x48, 0x81, 0xc4, 0x30, 0x01, 0x00, 0x00,              // add rsp, 0x130
    0x5b, 0x41, 0x5c, 0x41, 0x5d, 0x41, 0x5e, 0x41, 0x5f,  // pop rbx / r12 / r13 / r14 / r15
    0xc3, 0xcc,                                            // ret
};

// Step: push rbp / rbx / rax and mov rbx, [task + 0x38] (the site), then mov ebp, 1, mov edi, 1 and a call (the
// window; its target gets a ret) and the first byte of the next instruction, a REX.W prefix. The tail returns
// STEP_RESULT (the game's returns 0).
inline constexpr size_t STEP_TASK_FIELD = 0x38;
inline constexpr std::array<uint8_t, 34> STEP_TAIL = {
    0x89, 0xd8,                                            // the window's 48: mov rax, rbx
    0x48, 0xb9, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rcx, STEP_RECORD
    0x48, 0x89, 0x01,                                      // mov [rcx], rax
    0xff, 0x41, 0x08,                                      // inc dword [rcx+8]
    0x48, 0xb8, 0xde, 0xc0, 0x00, 0x00, 0xed, 0x5e, 0x00, 0x00,  // mov rax, STEP_RESULT
    0x48, 0x83, 0xc4, 0x08,                                // add rsp, 8
    0x5b, 0x5d,                                            // pop rbx / rbp
};
inline constexpr uint8_t RET = 0xc3;

// The fake view: its vtable's pass-kind slot returns `kind`.
struct FakeView {
  void* const* vtable;
  uint32_t kind;
};
ATTILA64_GAME_CALL uint32_t FakeViewKind(void* view) noexcept { return static_cast<FakeView*>(view)->kind; }

// The add-on's side. Atomics: the drain runs them on two game threads.
Block* active_block = nullptr;  // the block whose hooks run (this exe's or a DLL copy's)
std::atomic<uint32_t> before_pack_calls = 0;
std::atomic<uint32_t> pass_begin_calls = 0;
std::atomic<uint32_t> pass_end_calls = 0;
std::atomic<uint32_t> last_kind = 0;
std::atomic_bool end_measure_ok = false;
std::atomic<PassAction> next_action = PASS_DRAW;
const Callbacks* swap_in_begin = nullptr;  // stored into the block by pass_begin when set (a re-register in between)
std::atomic_bool drained = false;
std::atomic<uint32_t> calls_after_drain = 0;

void BeforePack() noexcept {
  if (drained.load()) ++calls_after_drain;
  ++before_pack_calls;
}

PassAction PassBegin(uint32_t pass_kind, PassMeasure* measure) noexcept {
  if (drained.load()) ++calls_after_drain;
  ++pass_begin_calls;
  last_kind = pass_kind;
  *measure = {.sun_binds_before = 11, .other_binds_before = 22, .sun_at_start = 33, .reserved = 0};
  if (swap_in_begin != nullptr) active_block->callbacks.store(swap_in_begin);
  return next_action.load();
}

void PassEnd(uint32_t pass_kind, const PassMeasure& measure) noexcept {
  if (drained.load()) ++calls_after_drain;
  ++pass_end_calls;
  end_measure_ok = pass_kind == last_kind && measure.sun_binds_before == 11 && measure.other_binds_before == 22
                   && measure.sun_at_start == 33;
}

const Callbacks CALLBACKS = {.before_pack = &BeforePack, .pass_begin = &PassBegin, .pass_end = &PassEnd};
const Callbacks OTHER_CALLBACKS = CALLBACKS;

inline constexpr uint32_t ALL_ROWS = RowBit(ROW_BONE_FLUSH) | RowBit(ROW_PASS_DRAW) | RowBit(ROW_CPU_PLACEMENT);

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (!ReserveGame(PAGE_EXECUTE_READWRITE)) {
    std::printf("FAIL: the game range 0x400000..0x527d000 is taken\n");
    return 1;
  }
  if (argc < 2) {
    std::printf("FAIL: patcher_test.exe <renodx_attila64.dll>\n");
    return 1;
  }
  char exe_dir[MAX_PATH];
  GetModuleFileNameA(nullptr, exe_dir, MAX_PATH);
  *std::strrchr(exe_dir, '\\') = '\0';
  wchar_t exe_dir_wide[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_dir_wide, MAX_PATH);
  *std::wcsrchr(exe_dir_wide, L'\\') = L'\0';
  LoaderApi api = MakeApi(exe_dir, GAME_HI_MIN);
  loader = &api;

  const auto at = [](uintptr_t address) { return reinterpret_cast<uint8_t*>(address); };
  const auto write_game = [&] {
    std::memcpy(at(GAME_LO), ELF_MAGIC.data(), ELF_MAGIC.size());
    std::memcpy(at(BUILD_ID_NOTE_ADDRESS), BUILD_ID_NOTE.data(), BUILD_ID_NOTE.size());
    for (const Site& site : SITES) {
      std::memcpy(at(site.address - SITE_MARGIN), site.window.data(), (2 * SITE_MARGIN) + site.length);
    }
    const uintptr_t pack_end = SITES[ROW_BONE_FLUSH].address + SITES[ROW_BONE_FLUSH].length + SITE_MARGIN;
    const uintptr_t draw_end = SITES[ROW_PASS_DRAW].address + SITES[ROW_PASS_DRAW].length + SITE_MARGIN;
    const uintptr_t step_end = SITES[ROW_CPU_PLACEMENT].address + SITES[ROW_CPU_PLACEMENT].length + SITE_MARGIN;
    std::memcpy(at(pack_end), PACK_TAIL.data(), PACK_TAIL.size());
    std::memcpy(at(draw_end), DRAW_TAIL.data(), DRAW_TAIL.size());
    std::memcpy(at(step_end), STEP_TAIL.data(), STEP_TAIL.size());
    *at(step_end + STEP_TAIL.size()) = RET;
    // The window's call rel32 ends one byte before the window does.
    int32_t step_call = 0;
    std::memcpy(&step_call, at(step_end - 5), sizeof(step_call));
    *at(step_end - 1 + step_call) = RET;
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  };
  const auto site_untouched = [&](const Site& site) {
    return std::memcmp(at(site.address), site.window.data() + SITE_MARGIN, site.length) == 0;
  };
  const auto every_site_untouched = [&] { return std::ranges::all_of(SITES, site_untouched); };
  write_game();
  WriteTweakObjects();

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

  // Block v2's size, and the per-site tables: one label per site, each label with its own hook (a swapped pair would
  // put a hook with another signature at a game site).
  Expect(VERSION == 2 && BlockBytes(0) == 0 && BlockBytes(1) == 40 && BlockBytes(2) == 216 && sizeof(Block) == 216,
         "block: v1 is 40 bytes, v2 216");
  {
    uint32_t labels_seen = 0;
    bool labels_ok = true;
    for (const Site& site : SITES) {
      labels_ok = labels_ok && site.label < LABEL_COUNT && (labels_seen & (1u << site.label)) == 0
                  && REPLACEMENTS[site.label] != nullptr;
      labels_seen |= 1u << site.label;
    }
    Expect(labels_ok && labels_seen == (1u << LABEL_COUNT) - 1u, "labels: one per site, each with its hook");
    Expect(REPLACEMENTS[LABEL_PACK_ENTRY] == reinterpret_cast<void*>(&PackEntry)
               && REPLACEMENTS[LABEL_VIEW_DRAW_ENTRY] == reinterpret_cast<void*>(&ViewDrawEntry)
               && REPLACEMENTS[LABEL_STEP_ENTRY] == reinterpret_cast<void*>(&StepTaskEntry),
           "labels: each holds its own hook");
    Expect(LABEL_STEP_ENTRY == 2 && SITES[LABEL_STEP_ENTRY].row == ROW_CPU_PLACEMENT && SITES[LABEL_STEP_ENTRY].label == LABEL_STEP_ENTRY
               && SITES[ROW_BONE_FLUSH].label == LABEL_PACK_ENTRY && SITES[ROW_PASS_DRAW].label == LABEL_VIEW_DRAW_ENTRY,
           "labels: the sites are in row order");
  }

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
    Expect(ApplySite(api, site, REPLACEMENTS[site.label], &applied) == REFUSAL_NO_MEMORY, "apply: no memory");
    hook_mode = HOOK_WRITE_FAILS;
    Expect(ApplySite(api, site, REPLACEMENTS[site.label], &applied) == REFUSAL_WRITE_FAILED, "apply: write failed");
    hook_mode = HOOK_NORMAL;
    write_game();
    Expect(ApplySite(api, site, REPLACEMENTS[site.label], &applied) == REFUSAL_NONE && applied.original != nullptr,
           "apply: hooked");
    Expect(applied.bytes[0] == 0xe9 && SiteUnchanged(site, applied), "apply: snapshot of the jump");
    Expect(CheckSite(site, {}).refusal == REFUSAL_DIFFERS, "apply: a second check sees our bytes");
    *at(site.address + 4) ^= 1;
    Expect(!SiteUnchanged(site, applied), "apply: a later change is seen");
  }

  // The stack-check rule: the windows hold its result.
  {
    constexpr std::array<uint8_t, 16> before = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x64,
                                                0x48, 0x8b, 0x04, 0x25, 0x28, 0x00, 0x00, 0x00};
    constexpr std::array<uint8_t, 9> left_alone = {0x64, 0x48, 0x8b, 0x04, 0x25, 0x30, 0x00, 0x00, 0x00};
    std::memcpy(at(RULE_SITE - 16), before.data(), before.size());
    std::memcpy(at(RULE_SITE), SITES[0].window.data() + SITE_MARGIN, 5);
    std::memcpy(at(RULE_SITE + 5), left_alone.data(), left_alone.size());
    Site site = {.address = static_cast<uint32_t>(RULE_SITE), .length = 5, .row = ROW_BONE_FLUSH, .label = LABEL_PACK_ENTRY};
    std::memcpy(site.window.data(), at(RULE_SITE - SITE_MARGIN), (2 * SITE_MARGIN) + 5);
    site.window[7] = 0x65;
    Expect(CheckSite(site, {}).refusal == REFUSAL_NEIGHBOUR, "stack rule: the raw file bytes differ from the window");
    Expect(ApplyStackRule(at(RULE_SITE - 16), at(RULE_SITE + 16 + 5)) == 1, "stack rule: one read rewritten");
    Expect(CheckSite(site, {}).refusal == REFUSAL_NONE, "stack rule: the rewritten bytes match");
  }

  // The hook bodies, called through the real sites as the game calls them.
  auto* pack_record = reinterpret_cast<PackRecord*>(PACK_RECORD);
  auto* draw_record = reinterpret_cast<DrawRecord*>(DRAW_RECORD);
  const auto pack = reinterpret_cast<PackFn>(SITES[ROW_BONE_FLUSH].address);
  const auto draw = reinterpret_cast<ViewDrawFn>(SITES[ROW_PASS_DRAW].address);
  auto* step_record = reinterpret_cast<StepRecord*>(STEP_RECORD);
  const auto step = reinterpret_cast<StepTaskFn>(SITES[ROW_CPU_PLACEMENT].address);
  std::array<uint64_t, (STEP_TASK_FIELD / sizeof(uint64_t)) + 1> task = {};
  task.back() = 0x7a5c;
  const auto step_once = [&] {
    step_record->task_field = 0;
    return step(task.data()) == reinterpret_cast<void*>(STEP_RESULT) && step_record->task_field == task.back();
  };
  const auto default_cpu_sets = [] {
    std::array<ULONG, 256> ids = {};
    ULONG count = 0;
    GetProcessDefaultCpuSets(GetCurrentProcess(), ids.data(), static_cast<ULONG>(ids.size()), &count);
    return std::pair{ids, count};
  };
  const auto core_state_of = [](const Block* shared) {
    return static_cast<uint32_t>(shared->core_status.load() >> CORE_STATE_SHIFT) & CORE_FIELD_MASK;
  };
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
  std::array<void*, VIEW_KIND_SLOT + 1> slots = {};
  slots[VIEW_KIND_SLOT] = reinterpret_cast<void*>(&FakeViewKind);
  FakeView view = {.vtable = slots.data(), .kind = 7};
  std::array<uint8_t, 0x40> node = {};
  const auto draw_once = [&] {
    draw(node.data(), reinterpret_cast<void*>(0x2222), &view);
    return draw_record->arguments[0] == reinterpret_cast<uint64_t>(node.data()) && draw_record->arguments[1] == 0x2222
           && draw_record->arguments[2] == reinterpret_cast<uint64_t>(&view);
  };
  // Every path of the bodies, through the given block (this exe's hooks.hpp copy or a DLL copy's).
  const auto expect_hook_bodies = [&](Block* shared) {
    active_block = shared;
    *pack_record = {};
    *draw_record = {};
    *step_record = {};
    before_pack_calls = 0;
    pass_begin_calls = 0;
    pass_end_calls = 0;
    shared->hooks_wanted.store(0);
    shared->callbacks.store(nullptr);

    Expect(pack_once() && pack_record->calls == 1 && before_pack_calls == 0, "pack: bit clear, original with its arguments");
    Expect(Logged("PACK_ENTRY: first call") && !Logged("VIEW_DRAW_ENTRY: first call"), "pack: its own first-call line only");
    shared->hooks_wanted.store(RowBit(ROW_BONE_FLUSH));
    Expect(pack_once() && pack_record->calls == 2 && before_pack_calls == 0, "pack: no callbacks registered");
    shared->callbacks.store(&CALLBACKS);
    Expect(pack_once() && pack_record->calls == 3 && before_pack_calls == 1, "pack: before_pack, then the original");
    Expect(shared->calls_in_flight.load() == 0, "pack: guard closed");

    view.kind = 7;
    node[NODE_ENABLED_OFFSET] = 1;
    Expect(draw_once() && draw_record->calls == 1 && pass_begin_calls == 0, "draw: bit clear");
    shared->hooks_wanted.store(RowBit(ROW_BONE_FLUSH) | RowBit(ROW_PASS_DRAW));
    shared->callbacks.store(nullptr);
    Expect(draw_once() && draw_record->calls == 2 && pass_begin_calls == 0, "draw: bit set, no callbacks registered");
    shared->callbacks.store(&CALLBACKS);
    node[NODE_ENABLED_OFFSET] = 0;
    Expect(draw_once() && draw_record->calls == 3 && pass_begin_calls == 0, "draw: a disabled node never reaches the add-on");
    node[NODE_ENABLED_OFFSET] = 1;
    next_action = PASS_SKIP;
    draw(node.data(), nullptr, &view);
    Expect(draw_record->calls == 3 && pass_begin_calls == 1 && last_kind == 7 && pass_end_calls == 0, "draw: skipped");
    next_action = PASS_DRAW;
    Expect(draw_once() && draw_record->calls == 4 && pass_begin_calls == 2 && pass_end_calls == 0, "draw: drawn");
    next_action = PASS_DRAW_MEASURED;
    view.kind = 9;
    Expect(draw_once() && draw_record->calls == 5 && pass_end_calls == 1 && end_measure_ok && last_kind == 9,
           "draw: measured, PassMeasure round trip");
    swap_in_begin = &OTHER_CALLBACKS;
    Expect(draw_once() && draw_record->calls == 6 && pass_begin_calls == 4 && pass_end_calls == 1,
           "draw: another Callbacks registered between begin and end, no end");
    swap_in_begin = nullptr;
    shared->callbacks.store(&CALLBACKS);
    Expect(draw_once() && pass_end_calls == 2 && shared->calls_in_flight.load() == 0, "draw: measured again, guard closed");
    Expect(Logged("VIEW_DRAW_ENTRY: first call"), "draw: first-call line");
    shared->hooks_wanted.store(0);
    shared->callbacks.store(nullptr);

    // The step: its bit clear, the placement does nothing and the game's step runs with its task; its bit set, one
    // step reserves a core on this process (or finds why not, on a small machine), and the bit cleared releases it.
    const auto default_before = default_cpu_sets();
    const uint64_t status_before = shared->core_status.load();
    shared->core_wanted.fetch_and(~CORE_BATTLE);
    Expect(!Logged("STEP_ENTRY: first call") && step_once() && step_record->calls == 1 && Logged("STEP_ENTRY: first call"),
           "step: the task reaches the game's step, its result comes back, its own first-call line");
    Expect(shared->core_status.load() == status_before && default_cpu_sets() == default_before && !Logged("battle core:"),
           "step: bit clear, nothing placed");
    shared->core_wanted.store(CORE_WANTED_DEFAULT);
    Expect(step_once() && step_record->calls == 2 && core_state_of(shared) != CORE_WAITING && core_state_of(shared) != 0
               && Logged("battle core:"),
           "step: bit set, the placement ran once before the game's step");
    if (core_state_of(shared) == CORE_ACTIVE) {
      Expect(default_cpu_sets() != default_before && Logged("battle core: logical"), "step: a core reserved");
      shared->core_wanted.fetch_and(~CORE_BATTLE);
      Expect(step_once() && step_record->calls == 3 && core_state_of(shared) == CORE_WAITING
                 && default_cpu_sets() == default_before && Logged("battle core: released (turned off)"),
             "step: bit cleared, released and the CPU sets put back");
    } else {
      std::printf("  step: no core on this machine (state %u): the release is not run\n", core_state_of(shared));
    }
    shared->core_wanted.store(CORE_WANTED_DEFAULT);
  };

  // This exe's copy of hooks.hpp.
  std::printf("  hooks: the test's copy\n");
  write_game();
  for (const Site& site : SITES) {
    AppliedSite applied;
    Expect(ApplySite(api, site, REPLACEMENTS[site.label], &applied) == REFUSAL_NONE, "hooks: site hooked");
    originals[site.label] = applied.original;
  }
  expect_hook_bodies(&block);

  // Drain: two game threads (a pack and a measured pass) while the add-on unregisters as plugin_block.h says: store
  // nullptr (the bits stay set here, so the hooks keep entering the guard), then wait for the first 0. On x86 this would
  // also pass with a release store; the seq_cst rule is the header's, this shows the first-zero wait.
  {
    block.hooks_wanted.store(RowBit(ROW_BONE_FLUSH) | RowBit(ROW_PASS_DRAW));
    next_action = PASS_DRAW_MEASURED;
    std::atomic_bool running = true;
    std::thread pack_thread([&] {
      while (running.load()) {
        pack_once();
      }
    });
    std::thread draw_thread([&] {
      while (running.load()) {
        draw_once();
      }
    });
    int undrained = 0;
    for (int cycle = 0; cycle < 200; ++cycle) {
      drained.store(false);
      block.callbacks.store(&CALLBACKS);
      std::this_thread::sleep_for(std::chrono::microseconds(200));
      block.callbacks.store(nullptr);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      bool zero_seen = false;
      while (!zero_seen && std::chrono::steady_clock::now() < deadline) {
        zero_seen = (block.calls_in_flight.load() == 0);
        if (!zero_seen) {
          Sleep(0);
        }
      }
      if (!zero_seen) {
        ++undrained;
      }
      drained.store(true);
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    running.store(false);
    pack_thread.join();
    draw_thread.join();
    Expect(undrained == 0, "drain: every wait ended within 1 s");
    Expect(calls_after_drain.load() == 0, "drain: no callback after the wait");
    std::printf("  drain: %u before_pack, %u pass_begin, %u pass_end calls over 200 cycles\n", before_pack_calls.load(),
                pass_begin_calls.load(), pass_end_calls.load());
    drained.store(false);
    block.hooks_wanted.store(0);
  }

  // The built DLL: copies under dll_cases\<case>\ (each its own module and globals).
  const std::wstring cases_dir = std::wstring(exe_dir_wide) + L"\\dll_cases";
  const std::wstring ini_path = std::wstring(exe_dir_wide) + L"\\ReShade.ini";
  CreateDirectoryW(cases_dir.c_str(), nullptr);
  const auto copy_plugin = [&](const wchar_t* folder, const wchar_t* file_name) {
    const std::wstring directory = cases_dir + L"\\" + folder;
    CreateDirectoryW(directory.c_str(), nullptr);
    std::wstring target = directory + L"\\" + file_name;
    Expect(CopyFileW(argv[1], target.c_str(), FALSE) != FALSE, "a copy of the plugin DLL");
    return target;
  };
  const auto set_ini_key_off = [&](const wchar_t* key) {
    DeleteFileW(ini_path.c_str());
    if (key != nullptr) {
      WritePrivateProfileStringW(INI_SECTION, key, L"0", ini_path.c_str());
    }
  };
  const auto init_copy = [&](const wchar_t* folder, const wchar_t* file_name, const LoaderApi& case_api) {
    const Plugin plugin = LoadPlugin(copy_plugin(folder, file_name).c_str());
    log_lines.clear();
    return std::pair{plugin, (plugin.init != nullptr) ? plugin.init(&case_api) : -1};
  };
  const auto status_of = [](const Plugin& plugin) {
    return (plugin.block != nullptr) ? plugin.block->status.load(std::memory_order_acquire) : uint32_t{STATUS_LOADING};
  };
  set_ini_key_off(nullptr);

  // The DLL file holds the render part's reserve text as one whole string of its own (a zero byte before and after):
  // the batch tool looks for exactly that and refuses a render-core arm on a DLL without it.
  {
    using std::string_view_literals::operator""sv;
    std::ifstream dll_file(argv[1], std::ios::binary);
    const std::string dll_bytes{std::istreambuf_iterator<char>(dll_file), std::istreambuf_iterator<char>()};
    Expect(dll_bytes.find("\0render core: logical %u + %u (render thread %lu), scheduling class %u\0"sv) != std::string::npos,
           "dll: the file holds the render core's reserve text as a whole string");
    Expect(dll_bytes.find("\0renodx_attila64: render thread %lu\0"sv) != std::string::npos,
           "dll: the file holds the render thread's line as a whole string");
  }

  const auto render_state_of = [](const Block* shared) {
    return (shared->render_core_status.load() >> CORE_STATE_SHIFT) & CORE_FIELD_MASK;
  };
  const auto selected_count = [] {
    ULONG count = 0;
    GetThreadSelectedCpuSets(GetCurrentThread(), nullptr, 0, &count);
    return count;
  };

  std::printf("  hooks: the DLL's own\n");
  write_game();
  const Plugin main_plugin = ExpectPluginDll(api, copy_plugin(L"main", MODULE_NAME).c_str());
  if (main_plugin.block != nullptr) {
    // This thread ran the DLL's game start, so it is the DLL's render thread: its steps are never selected onto the
    // battle core (the test's own copy above, with no render thread, selects this thread).
    expect_hook_bodies(main_plugin.block);
    Expect(step_once() && (core_state_of(main_plugin.block) != CORE_ACTIVE || selected_count() == 0),
           "step: the render thread is not selected onto the battle core");
    main_plugin.block->core_wanted.fetch_and(~CORE_BATTLE);
    Expect(step_once() && core_state_of(main_plugin.block) != CORE_ACTIVE, "step: released again");
    main_plugin.block->core_wanted.store(CORE_WANTED_DEFAULT);
  }

  // The RenderCore key. 1: the render part's bit is set at init, and one step reserves both cores (this thread is the
  // render thread and holds the render core). 0: off for the process, whatever the bit says later.
  WritePrivateProfileStringW(INI_SECTION, INI_RENDER_CORE, L"1", ini_path.c_str());
  write_game();
  if (const auto [plugin, result] = init_copy(L"render_on", MODULE_NAME, api); result == 0) {
    Expect(plugin.block->core_wanted.load() == (CORE_WANTED_DEFAULT | CORE_RENDER) && render_state_of(plugin.block) == CORE_NO_THREAD,
           "dll: RenderCore=1 sets the render part's bit at init");
    plugin.game_started(&api);
    Expect(render_state_of(plugin.block) == CORE_WAITING && Logged("renodx_attila64: render thread "),
           "dll: RenderCore=1, the render thread is captured at the game's start");
    const auto default_before = default_cpu_sets();
    Expect(step_once() && core_state_of(plugin.block) != CORE_WAITING, "dll: RenderCore=1, a step places");
    if (core_state_of(plugin.block) == CORE_ACTIVE && render_state_of(plugin.block) == CORE_ACTIVE) {
      char render_piece[64];
      std::snprintf(render_piece, sizeof(render_piece), " (render thread %lu), scheduling class ", GetCurrentThreadId());
      Expect(selected_count() == 2 && default_cpu_sets() != default_before
                 && Logged("renodx_attila64: battle core: logical ") && Logged(", render core: logical ") && Logged(render_piece),
             "dll: RenderCore=1, both cores reserved in one line, this thread on the render core");
      plugin.block->core_wanted.fetch_and(~(CORE_BATTLE | CORE_RENDER));
      Expect(step_once() && core_state_of(plugin.block) == CORE_WAITING && render_state_of(plugin.block) == CORE_WAITING
                 && selected_count() == 0 && default_cpu_sets() == default_before,
             "dll: RenderCore=1, released: the render thread cleared, the CPU sets put back");
    } else {
      std::printf("  dll: no render core on this machine (states %u / %u)\n", core_state_of(plugin.block),
                  render_state_of(plugin.block));
      plugin.block->core_wanted.fetch_and(~(CORE_BATTLE | CORE_RENDER));
      step_once();
    }
  } else {
    Expect(false, "dll: init for the RenderCore=1 case");
  }
  set_ini_key_off(INI_RENDER_CORE);
  write_game();
  if (const auto [plugin, result] = init_copy(L"render_off", MODULE_NAME, api); result == 0) {
    Expect(plugin.block->core_wanted.load() == (CORE_WANTED_DEFAULT & ~CORE_RENDER) && render_state_of(plugin.block) == CORE_OFF_INI,
           "dll: RenderCore=0 is off at init");
    plugin.game_started(&api);
    plugin.block->core_wanted.fetch_or(CORE_RENDER);
    Expect(step_once() && render_state_of(plugin.block) == CORE_OFF_INI && !Logged("renodx_attila64: render thread ")
               && Logged("render core: off in ReShade.ini, RenderCore=0 (render thread 0, error 0)") && !Logged("render core: logical"),
           "dll: RenderCore=0, nothing captured and no render core even with its bit set later");
    plugin.block->core_wanted.fetch_and(~(CORE_BATTLE | CORE_RENDER));
    Expect(step_once() && core_state_of(plugin.block) != CORE_ACTIVE, "dll: RenderCore=0, released again");
  } else {
    Expect(false, "dll: init for the RenderCore=0 case");
  }
  set_ini_key_off(nullptr);

  write_game();
  if (const auto [plugin, result] = init_copy(L"changed", MODULE_NAME, api); result == 0) {
    Expect(plugin.block != main_plugin.block, "dll: each copy has its own block");
    *at(SITES[ROW_PASS_DRAW].address + 1) ^= 1;
    plugin.game_started(&api);
    Expect(plugin.block->rows_changed.load() == RowBit(ROW_PASS_DRAW) && Logged("VIEW_DRAW_ENTRY changed after we patched it")
               && Logged("game started: 2 sites unchanged"),
           "dll: game start finds a site changed after init");
  } else {
    Expect(false, "dll: init for the changed-later case");
  }

  set_ini_key_off(INI_ENABLED);
  write_game();
  {
    const auto [plugin, result] = init_copy(L"off", MODULE_NAME, api);
    Expect(result == 1 && status_of(plugin) == STATUS_OFF && Logged("Enabled=0") && every_site_untouched()
               && !Logged("loader frame cap"),
           "dll: Enabled=0 refuses and writes nothing");
  }
  for (const Row row : {ROW_BONE_FLUSH, ROW_PASS_DRAW, ROW_CPU_PLACEMENT}) {
    set_ini_key_off(ROW_INI_KEYS[row]);
    write_game();
    const auto [plugin, result] = init_copy(ROW_INI_KEYS[row], MODULE_NAME, api);
    const uint32_t others = ALL_ROWS & ~RowBit(row);
    bool only_its_site_untouched = true;
    for (const Site& site : SITES) {
      only_its_site_untouched = only_its_site_untouched && site_untouched(site) == (site.row == row);
    }
    Expect(result == 0 && status_of(plugin) == STATUS_READY && plugin.block->rows_applied == others
               && plugin.block->rows_refused == RowBit(row) && plugin.block->rows_tried == others
               && Logged("skipped (OFF)") && only_its_site_untouched,
           "dll: a row's install key 0 leaves that row out, and out of the rows tried");
    // Both core status words are built only with the row in (without it the RenderCore key is not read either).
    const bool built = (row != ROW_CPU_PLACEMENT);
    Expect(plugin.block->core_status.load() == (built ? (uint64_t{CORE_WAITING} << CORE_STATE_SHIFT) : 0)
               && plugin.block->render_core_status.load() == (built ? (uint32_t{CORE_NO_THREAD} << CORE_STATE_SHIFT) : 0),
           "dll: the core status words are built only with the CPU row applied");
    plugin.game_started(&api);
    Expect(Logged("renodx_attila64: render thread ") == built
               && plugin.block->render_core_status.load() == (built ? (uint32_t{CORE_WAITING} << CORE_STATE_SHIFT) : 0),
           "dll: the render thread is captured only with the CPU row applied");
  }
  set_ini_key_off(nullptr);

  write_game();
  *at(SITES[ROW_BONE_FLUSH].address) ^= 1;
  {
    const auto [plugin, result] = init_copy(L"differs", MODULE_NAME, api);
    Expect(result == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_BONE_FLUSH))
               && plugin.block->rows_refused == RowBit(ROW_BONE_FLUSH) && plugin.block->rows_tried == ALL_ROWS
               && Logged("PACK_ENTRY 0x3416d50: skipped (DIFFERS)"),
           "dll: a site whose bytes differ is left out (still tried)");
  }

  write_game();
  *at(SITES[ROW_CPU_PLACEMENT].address + SITES[ROW_CPU_PLACEMENT].length) ^= 1;
  {
    const auto [plugin, result] = init_copy(L"step_neighbour", MODULE_NAME, api);
    Expect(result == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_CPU_PLACEMENT))
               && plugin.block->rows_refused == RowBit(ROW_CPU_PLACEMENT) && plugin.block->rows_tried == ALL_ROWS
               && plugin.block->core_status.load() == 0 && plugin.block->render_core_status.load() == 0
               && site_untouched(SITES[ROW_CPU_PLACEMENT])
               && Logged("STEP_ENTRY 0xbb4e00: skipped (NEIGHBOUR)"),
           "dll: a patch next to the step site leaves the battle core out (tried, status not built)");
  }

  write_game();
  hook_mode = HOOK_NO_MEMORY;
  {
    const auto [plugin, result] = init_copy(L"no_memory", MODULE_NAME, api);
    Expect(result == 0 && status_of(plugin) == STATUS_READY && plugin.block->rows_applied == 0
               && plugin.block->rows_refused == ALL_ROWS && plugin.block->core_status.load() == 0
               && Logged("skipped (NO_MEMORY)"),
           "dll: no memory near the game leaves every row out");
  }
  hook_mode = HOOK_NORMAL;

  write_game();
  *at(BUILD_ID_NOTE_ADDRESS + 20) ^= 1;
  {
    const auto [plugin, result] = init_copy(L"wrong_game", MODULE_NAME, api);
    Expect(result == 1 && status_of(plugin) == STATUS_WRONG_GAME && every_site_untouched() && !Logged("loader frame cap"),
           "dll: another game build refuses and writes nothing");
  }

  write_game();
  {
    LoaderApi short_range = api;
    short_range.game_hi = GAME_HI_MIN - 1;
    const auto [plugin, result] = init_copy(L"wrong_loader", MODULE_NAME, short_range);
    Expect(result == 1 && status_of(plugin) == STATUS_WRONG_LOADER && site_untouched(SITES[0]) && !Logged("loader frame cap"),
           "dll: a short game range refuses");
  }
  {
    const auto [plugin, result] = init_copy(L"renamed", L"renodx_attila64_old.dll", api);
    Expect(result == 1 && status_of(plugin) == STATUS_WRONG_LOADER && Logged("renamed") && every_site_untouched()
               && !Logged("loader frame cap"),
           "dll: a renamed file refuses and writes nothing");
  }
  set_ini_key_off(nullptr);

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
