/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The plugin's unit test on a fake game image at the game's addresses (no game file needed): identity, the site checks
// and their refusals, the loader's hook and the re-check, the stack-check rule, the hook bodies on synthetic game
// functions at the real sites (this file's copy of hooks.hpp and the built DLL's own) with the add-on's callbacks, the
// callback drain, and the built DLL's init with each refusal (copies of the DLL under dll_cases\, a ReShade.ini next to
// this exe for the ini keys).
// patcher_test.exe <renodx_attila64.dll>; exit code = failures.

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
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

struct PackRecord {
  std::array<uint64_t, 8> arguments;
  uint32_t calls;
};
struct DrawRecord {
  std::array<uint64_t, 3> arguments;
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
    std::memcpy(at(pack_end), PACK_TAIL.data(), PACK_TAIL.size());
    std::memcpy(at(draw_end), DRAW_TAIL.data(), DRAW_TAIL.size());
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  };
  const auto site_untouched = [&](const Site& site) {
    return std::memcmp(at(site.address), site.window.data() + SITE_MARGIN, site.length) == 0;
  };
  write_game();

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
  // Every path of both bodies, through the given block (this exe's hooks.hpp copy or a DLL copy's).
  const auto expect_hook_bodies = [&](Block* shared) {
    active_block = shared;
    *pack_record = {};
    *draw_record = {};
    before_pack_calls = 0;
    pass_begin_calls = 0;
    pass_end_calls = 0;
    shared->hooks_wanted.store(0);
    shared->callbacks.store(nullptr);

    Expect(pack_once() && pack_record->calls == 1 && before_pack_calls == 0, "pack: bit clear, original with its arguments");
    Expect(Logged("PACK_ENTRY: first call"), "pack: first-call line");
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
  };

  // This exe's copy of hooks.hpp.
  std::printf("  hooks: the test's copy\n");
  write_game();
  for (const Site& site : SITES) {
    AppliedSite applied;
    Expect(ApplySite(api, site, REPLACEMENTS[site.row], &applied) == REFUSAL_NONE, "hooks: site hooked");
    originals[site.row] = applied.original;
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

  std::printf("  hooks: the DLL's own\n");
  write_game();
  const Plugin main_plugin = ExpectPluginDll(api, copy_plugin(L"main", MODULE_NAME).c_str());
  if (main_plugin.block != nullptr) {
    expect_hook_bodies(main_plugin.block);
  }

  write_game();
  if (const auto [plugin, result] = init_copy(L"changed", MODULE_NAME, api); result == 0) {
    Expect(plugin.block != main_plugin.block, "dll: each copy has its own block");
    *at(SITES[ROW_PASS_DRAW].address + 1) ^= 1;
    plugin.game_started(&api);
    Expect(plugin.block->rows_changed.load() == RowBit(ROW_PASS_DRAW) && Logged("VIEW_DRAW_ENTRY changed after we patched it")
               && Logged("game started: 1 sites unchanged"),
           "dll: game start finds a site changed after init");
  } else {
    Expect(false, "dll: init for the changed-later case");
  }

  set_ini_key_off(INI_ENABLED);
  write_game();
  {
    const auto [plugin, result] = init_copy(L"off", MODULE_NAME, api);
    Expect(result == 1 && status_of(plugin) == STATUS_OFF && Logged("Enabled=0") && site_untouched(SITES[0])
               && site_untouched(SITES[1]),
           "dll: Enabled=0 refuses and writes nothing");
  }
  for (const Row row : {ROW_BONE_FLUSH, ROW_PASS_DRAW}) {
    set_ini_key_off(ROW_INI_KEYS[row]);
    write_game();
    const auto [plugin, result] = init_copy(ROW_INI_KEYS[row], MODULE_NAME, api);
    const Row other = (row == ROW_BONE_FLUSH) ? ROW_PASS_DRAW : ROW_BONE_FLUSH;
    Expect(result == 0 && status_of(plugin) == STATUS_READY && plugin.block->rows_applied == RowBit(other)
               && plugin.block->rows_refused == RowBit(row) && Logged("skipped (OFF)") && site_untouched(SITES[row])
               && !site_untouched(SITES[other]),
           "dll: a row's install key 0 leaves that row out");
  }
  set_ini_key_off(nullptr);

  write_game();
  *at(SITES[ROW_BONE_FLUSH].address) ^= 1;
  {
    const auto [plugin, result] = init_copy(L"differs", MODULE_NAME, api);
    Expect(result == 0 && plugin.block->rows_applied == RowBit(ROW_PASS_DRAW)
               && plugin.block->rows_refused == RowBit(ROW_BONE_FLUSH) && Logged("PACK_ENTRY 0x3416d50: skipped (DIFFERS)"),
           "dll: a site whose bytes differ is left out");
  }

  write_game();
  hook_mode = HOOK_NO_MEMORY;
  {
    const auto [plugin, result] = init_copy(L"no_memory", MODULE_NAME, api);
    Expect(result == 0 && status_of(plugin) == STATUS_READY && plugin.block->rows_applied == 0
               && plugin.block->rows_refused == (RowBit(ROW_BONE_FLUSH) | RowBit(ROW_PASS_DRAW)) && Logged("skipped (NO_MEMORY)"),
           "dll: no memory near the game leaves both rows out");
  }
  hook_mode = HOOK_NORMAL;

  write_game();
  *at(BUILD_ID_NOTE_ADDRESS + 20) ^= 1;
  {
    const auto [plugin, result] = init_copy(L"wrong_game", MODULE_NAME, api);
    Expect(result == 1 && status_of(plugin) == STATUS_WRONG_GAME && site_untouched(SITES[0]) && site_untouched(SITES[1]),
           "dll: another game build refuses and writes nothing");
  }

  write_game();
  {
    LoaderApi short_range = api;
    short_range.game_hi = GAME_HI_MIN - 1;
    const auto [plugin, result] = init_copy(L"wrong_loader", MODULE_NAME, short_range);
    Expect(result == 1 && status_of(plugin) == STATUS_WRONG_LOADER && site_untouched(SITES[0]),
           "dll: a short game range refuses");
  }
  {
    const auto [plugin, result] = init_copy(L"renamed", L"renodx_attila64_old.dll", api);
    Expect(result == 1 && status_of(plugin) == STATUS_WRONG_LOADER && Logged("renamed") && site_untouched(SITES[0])
               && site_untouched(SITES[1]),
           "dll: a renamed file refuses and writes nothing");
  }
  set_ini_key_off(nullptr);

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
