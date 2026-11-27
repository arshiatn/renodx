/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../loader_api.h"
#include "../plugin_block.h"
#include "../sites.h"
#include "../tweaks.h"

// The tests' stand-in for the 64-bit Attila loader: its plugin interface v1 written from its documented behaviour (an
// entry hook copies the first bytes to code near the game, jumps back after them, and enters through
// mov rax, imm64; jmp rax), its stack-check rewrite, the game's address range, the engine value objects as the game's
// static constructors leave them, and the plugin DLL's whole init.
namespace twattila::attila64_plugin::test {

inline constexpr uintptr_t GAME_RESERVE_END = 0x527d000;  // the last load segment's end, page-rounded

enum HookMode : uint8_t { HOOK_NORMAL, HOOK_NO_MEMORY, HOOK_WRITE_FAILS };

static int checks = 0;
static int failures = 0;
// The battle core's watchdog logs from a timer thread.
static std::shared_mutex log_mutex;
static std::vector<std::string> log_lines;
static HookMode hook_mode = HOOK_NORMAL;
// The calls FakePatch refuses of the ones still to come: bit 0 = the next, bit 1 = the one after; 0 = none.
static uint32_t failing_patch_calls = 0;
// The same for FakeHook: a refused call finds no memory near the game.
static uint32_t failing_hook_calls = 0;
// The same for FakeCodeNearGame's calls for more than no bytes.
static uint32_t failing_code_calls = 0;

static void Expect(bool ok, const char* what) {
  ++checks;
  if (ok) return;
  ++failures;
  std::printf("FAIL: %s\n", what);
}

static void FakeLog(const char* format, ...) noexcept {
  char line[1024];
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(line, sizeof(line), format, arguments);
  va_end(arguments);
  std::printf("  log: %s\n", line);
  const std::scoped_lock lock(log_mutex);
  log_lines.emplace_back(line);
}

static bool Logged(const char* text) {
  const std::shared_lock lock(log_mutex);
  for (const std::string& line : log_lines) {
    if (line.find(text) != std::string::npos) return true;
  }
  return false;
}

// The loader's own room near the game: one 8 KB page for a whole game run, its own stubs included.
inline constexpr size_t LOADER_NEAR_CODE_BYTES = 0x2000;
// Execute-read-write memory in rel32 reach of the game, handed out front to back: 32 of the loader's pages, since one
// test process stands for many game runs (near_code_used: all bytes handed out so far, for a test of one run's use).
static size_t near_code_used = 0;
static void* FakeCodeNearGame(size_t n) noexcept {
  static uint8_t* pages = nullptr;
  for (uintptr_t at = 0x5300000; pages == nullptr && at < 0x60000000; at += 0x10000) {
    pages = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(at), 32 * LOADER_NEAR_CODE_BYTES,
                                               MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
  }
  const bool refused = (n != 0 && (failing_code_calls & 1u) != 0);
  if (n != 0) {
    failing_code_calls >>= 1;
  }
  if (refused || pages == nullptr || hook_mode == HOOK_NO_MEMORY || near_code_used + n > 32 * LOADER_NEAR_CODE_BYTES) {
    return nullptr;
  }
  near_code_used += n;
  return pages + near_code_used - n;
}

static int FakePatch(uintptr_t at, const uint8_t* expect, const uint8_t* bytes, size_t len) noexcept {
  const bool refused = (failing_patch_calls & 1u) != 0;
  failing_patch_calls >>= 1;
  if (refused) return 0;
  if (std::memcmp(reinterpret_cast<void*>(at), expect, len) != 0) return 0;
  std::memcpy(reinterpret_cast<void*>(at), bytes, len);
  return 1;
}

// 32 bytes near the game: [0, len) the copied bytes, then jmp rel32 to at + len; at 16 the way in to fn. The site gets
// a jmp rel32 to the way in. Lengths 12..15 would overlap the way in, so this stand-in refuses them.
static void* FakeHook(uintptr_t at, const uint8_t* expect, int len, void* fn) noexcept {
  auto* site = reinterpret_cast<uint8_t*>(at);
  const bool refused = (failing_hook_calls & 1u) != 0;
  failing_hook_calls >>= 1;
  if (refused || hook_mode == HOOK_NO_MEMORY || len < 5 || len > 11 || std::memcmp(site, expect, len) != 0) return nullptr;
  if (hook_mode == HOOK_WRITE_FAILS) {
    site[0] ^= 0xff;  // someone else's write in between
    return nullptr;
  }
  auto* code = static_cast<uint8_t*>(FakeCodeNearGame(32));
  if (code == nullptr) return nullptr;
  std::memcpy(code, site, len);
  code[len] = 0xe9;
  const auto back = static_cast<int32_t>(reinterpret_cast<intptr_t>(site + len) - reinterpret_cast<intptr_t>(code + len + 5));
  std::memcpy(code + len + 1, &back, 4);
  uint8_t* way_in = code + 16;
  way_in[0] = 0x48;
  way_in[1] = 0xb8;
  std::memcpy(way_in + 2, &fn, 8);
  way_in[10] = 0xff;
  way_in[11] = 0xe0;
  const auto in = static_cast<int32_t>(reinterpret_cast<intptr_t>(way_in) - reinterpret_cast<intptr_t>(site + 5));
  site[0] = 0xe9;
  std::memcpy(site + 1, &in, 4);
  FlushInstructionCache(GetCurrentProcess(), site, len);
  return code;
}

static LoaderApi MakeApi(const char* plugin_dir, uintptr_t game_hi) {
  return {
      .api_version = LOADER_API_VERSION,
      .engine_version = "test",
      .game_lo = GAME_LO,
      .game_hi = game_hi,
      .plugin_dir = plugin_dir,
      .log = &FakeLog,
      .patch = &FakePatch,
      .hook = &FakeHook,
      .code_near_game = &FakeCodeNearGame,
  };
}

// The game's whole range, committed with `protect`; false when another mapping is there.
static bool ReserveGame(DWORD protect) {
  return VirtualAlloc(reinterpret_cast<void*>(GAME_LO), GAME_RESERVE_END - GAME_LO, MEM_RESERVE | MEM_COMMIT, protect)
         != nullptr;
}

// The loader's rewrite of the game's stack-check reads (fs:0x28 -> gs:0x28): 64 REX.W (mov | xor | sub | cmp)
// r64, [disp32] with disp32 0x28; the generator's windows hold the result.
static int ApplyStackRule(uint8_t* begin, const uint8_t* end) {
  int rewritten = 0;
  for (uint8_t* p = begin; p + 9 <= end; ++p) {
    if (p[0] != 0x64 || (p[1] & 0xf8) != 0x48) continue;
    if (p[2] != 0x8b && p[2] != 0x33 && p[2] != 0x2b && p[2] != 0x3b) continue;
    if ((p[3] & 0xc7) != 0x04 || p[4] != 0x25 || p[5] != 0x28 || p[6] != 0 || p[7] != 0 || p[8] != 0) continue;
    p[0] = 0x65;
    ++rewritten;
  }
  return rewritten;
}

// A test that needs the game's file and did not run (build.ps1 reports it as SKIPPED, not as a pass).
inline constexpr int EXIT_SKIPPED = 77;

// The game ELF's load segments as mapped into the reserved range (ReserveGame).
struct GameImage {
  uintptr_t end = 0;
  uintptr_t writable_lo = 0;  // the load segment the game writes: its data
  uintptr_t writable_hi = 0;
};

// The game ELF at `path` copied to its load addresses, with the loader's stack-check rewrite. EXIT_SKIPPED when the file
// cannot be opened, 1 when it cannot be read or a segment lies outside the range, else 0.
static int MapGameElf(const wchar_t* path, GameImage* image) {
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  LARGE_INTEGER size = {};
  if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size)) {
    std::printf("SKIPPED: the game ELF could not be opened (error %lu)\n", GetLastError());
    return EXIT_SKIPPED;
  }
  std::vector<uint8_t> elf(static_cast<size_t>(size.QuadPart));
  DWORD got = 0;
  for (size_t offset = 0; offset < elf.size(); offset += got) {
    const auto chunk = static_cast<DWORD>(std::min<size_t>(elf.size() - offset, 1 << 24));
    if (!ReadFile(file, elf.data() + offset, chunk, &got, nullptr) || got == 0) {
      std::printf("FAIL: reading the game ELF (error %lu)\n", GetLastError());
      CloseHandle(file);
      return 1;
    }
  }
  CloseHandle(file);

  // ELF64 program headers: e_phoff at 0x20, e_phentsize 0x36, e_phnum 0x38; p_type, p_flags 4, p_offset 8, p_vaddr 16,
  // p_filesz 32, p_memsz 40.
  const auto field = [&]<typename T>(size_t offset) {
    T value = {};
    if (offset + sizeof(T) <= elf.size()) {
      std::memcpy(&value, elf.data() + offset, sizeof(T));
    }
    return value;
  };
  const auto program_headers = field.operator()<uint64_t>(0x20);
  for (uint16_t index = 0; index < field.operator()<uint16_t>(0x38); ++index) {
    const size_t header = program_headers + (index * size_t{field.operator()<uint16_t>(0x36)});
    if (field.operator()<uint32_t>(header) != 1) continue;  // PT_LOAD
    const auto offset = field.operator()<uint64_t>(header + 8);
    const auto address = field.operator()<uint64_t>(header + 16);
    const auto file_bytes = field.operator()<uint64_t>(header + 32);
    const auto memory_bytes = field.operator()<uint64_t>(header + 40);
    if (address < GAME_LO || address + memory_bytes > GAME_RESERVE_END || offset + file_bytes > elf.size()) {
      std::printf("FAIL: load segment %#llx + %#llx outside the game range or the file\n", address, memory_bytes);
      return 1;
    }
    std::memcpy(reinterpret_cast<void*>(address), elf.data() + offset, file_bytes);
    image->end = std::max<uintptr_t>(image->end, address + memory_bytes);
    if ((field.operator()<uint32_t>(header + 4) & 2) != 0) {  // PF_W
      image->writable_lo = address;
      image->writable_hi = address + memory_bytes;
    }
  }
  const int rewritten = ApplyStackRule(reinterpret_cast<uint8_t*>(GAME_LO), reinterpret_cast<const uint8_t*>(image->end));
  std::printf("  mapped %#llx..%#llx, %d stack-check reads rewritten\n", uint64_t{GAME_LO}, uint64_t{image->end}, rewritten);
  return 0;
}

// Every tweak object of tweaks.h in the reserved game range, as the game builds it: its type's table pointer, its
// name as a counted UTF-16 string, its default.
static void WriteTweakObjects() {
  static std::vector<std::u16string> names;  // the objects point into these
  names.clear();
  names.reserve(TWEAKS.size());
  for (const TweakEntry& entry : TWEAKS) {
    if (entry.object == 0) continue;
    const TweakKind& kind = TWEAK_KINDS[entry.type];
    auto* object = reinterpret_cast<uint8_t*>(uintptr_t{entry.object});
    const std::string_view narrow = entry.name;
    const std::u16string& name = names.emplace_back(narrow.begin(), narrow.end());
    const uint64_t table = kind.table;
    const auto length = static_cast<uint32_t>(name.size());
    const char16_t* units = name.data();
    std::memcpy(object, &table, sizeof(table));
    std::memcpy(object + TWEAK_NAME_LENGTH_OFFSET, &length, sizeof(length));
    std::memcpy(object + TWEAK_NAME_UNITS_OFFSET, &units, sizeof(units));
    std::memcpy(object + kind.value_offset, &entry.default_bits, kind.value_bytes);
  }
}

// Every register a mid-function site can see or change, for a test bed (WriteMidBed).
struct Registers {
  // By register number: rax, rcx, rdx, rbx, rsp (stored only), rbp, rsi, rdi, r8-r15.
  std::array<uint64_t, 16> gpr = {};
  uint64_t flags = 0;
  // What the bed's tail stored for the test (a site's memory writes).
  uint64_t slot = 0;
  uint64_t pushed = 0;
  // The two qwords right below the site's rsp (the System V red zone), which the bed fills with in's rdi.
  std::array<uint64_t, 2> red_zone = {};
  std::array<std::array<uint8_t, 16>, 16> xmm = {};

  bool operator==(const Registers&) const = default;
};
static_assert(offsetof(Registers, flags) == 0x80 && offsetof(Registers, slot) == 0x88 && offsetof(Registers, pushed) == 0x90
              && offsetof(Registers, red_zone) == 0x98 && offsetof(Registers, xmm) == 0xa8);
using MidBed = void(ATTILA64_GAME_CALL*)(const Registers* in, Registers* out);
inline constexpr size_t MID_BED_CAPACITY = 0x340;  // without the site's bytes and the tail

// A test bed for a mid-function site, written at `code`: a function that loads every register and the flags from *in,
// writes rdi into the red zone, runs the site, and stores them all to *out with the red zone as the site left it (from
// below it, flags first: nothing in between changes one). The site is `site_bytes`
// right there; or, with `site_at` not 0, code elsewhere that the bed jumps to and that comes back with a jump to
// *capture. `reserve`: stack bytes below the bed's own frame (the stub at another stack alignment; room for a site's
// own [rsp + x] store). `site_stack`: what the site's instructions leave on the stack. `tail`: code run once the
// registers are stored, with rax = out and rsp as the site left it; it must take the site's stack bytes off again.
// Returns the site's address.
static uintptr_t WriteMidBed(uint8_t* code, std::span<const uint8_t> site_bytes, uintptr_t site_at, uint32_t reserve,
                             uint32_t site_stack, std::span<const uint8_t> tail, uintptr_t* capture) {
  uint8_t* start = code;
  const auto emit = [&](std::initializer_list<uint8_t> bytes) { code = std::copy(bytes.begin(), bytes.end(), code); };
  const auto emit_value = [&](uint64_t value, size_t size) {
    std::memcpy(code, &value, size);
    code += size;
  };
  // `opcode` with [`base` + displacement] and register `number` (a general or an xmm one).
  const auto emit_access = [&](bool wide, std::initializer_list<uint8_t> opcode, uint8_t number, uint8_t base, size_t displacement) {
    if (wide || number >= 8) {
      emit({static_cast<uint8_t>(0x40 | (wide ? 0x08 : 0) | ((number >= 8) ? 0x04 : 0))});
    }
    emit(opcode);
    emit({static_cast<uint8_t>(0x80 | ((number & 7) << 3) | base)});
    emit_value(displacement, 4);
  };
  constexpr uint8_t RAX = 0;
  constexpr uint8_t RCX = 1;
  constexpr uint8_t RSP = 4;
  constexpr uint8_t RDI = 7;
  emit({0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x56});  // push rbx, rbp, r12-r15; push rsi (out)
  emit({0x48, 0x81, 0xec});                                                  // sub rsp, reserve
  emit_value(reserve, 4);
  for (uint8_t number = 0; number < 16; ++number) {
    emit_access(false, {0x0f, 0x10}, number, RDI, offsetof(Registers, xmm) + (16 * number));  // movups xmm, [rdi + ..]
  }
  emit({0xff, 0xb7});  // push qword ptr [rdi + flags]; popfq
  emit_value(offsetof(Registers, flags), 4);
  emit({0x9d});
  for (const uint8_t number : {0, 1, 2, 3, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 7}) {
    emit_access(true, {0x8b}, number, RDI, 8 * number);  // mov reg, [rdi + ..]; rdi last
  }
  emit({0x48, 0x89, 0x7c, 0x24, 0xf8, 0x48, 0x89, 0x7c, 0x24, 0xf0});  // mov [rsp - 8], rdi; mov [rsp - 16], rdi
  uintptr_t site = reinterpret_cast<uintptr_t>(code);
  if (site_at == 0) {
    code = std::copy(site_bytes.begin(), site_bytes.end(), code);
  } else {
    site = site_at;
    emit({0xe9});  // jmp site_at
    emit_value(site_at - (reinterpret_cast<uintptr_t>(code) + 4), 4);
  }
  *capture = reinterpret_cast<uintptr_t>(code);
  emit({0x48, 0x8d, 0x64, 0x24, 0x80});        // lea rsp, [rsp - 0x80]
  emit({0x9c, 0x50, 0x48, 0x8b, 0x84, 0x24});  // pushfq; push rax; mov rax, [rsp + 16 + 0x80 + site_stack + reserve] (out)
  emit_value(16 + 0x80 + site_stack + reserve, 4);
  for (const uint8_t number : {1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}) {
    emit_access(true, {0x89}, number, RAX, 8 * number);  // mov [rax + ..], reg
  }
  for (uint8_t number = 0; number < 16; ++number) {
    emit_access(false, {0x0f, 0x11}, number, RAX, offsetof(Registers, xmm) + (16 * number));  // movups [rax + ..], xmm
  }
  emit({0x8f, 0x80, 0, 0, 0, 0});  // pop qword ptr [rax]: the site's rax
  emit({0x8f, 0x80});              // pop qword ptr [rax + flags]
  emit_value(offsetof(Registers, flags), 4);
  emit({0x48, 0x8b, 0x4c, 0x24, 0x78});  // mov rcx, [rsp + 0x78]; mov [rax + red_zone], rcx
  emit_access(true, {0x89}, RCX, RAX, offsetof(Registers, red_zone));
  emit({0x48, 0x8b, 0x4c, 0x24, 0x70});  // mov rcx, [rsp + 0x70]; mov [rax + red_zone + 8], rcx
  emit_access(true, {0x89}, RCX, RAX, offsetof(Registers, red_zone) + 8);
  emit({0x48, 0x8d, 0xa4, 0x24, 0x80, 0x00, 0x00, 0x00});  // lea rsp, [rsp + 0x80]
  emit_access(true, {0x89}, RSP, RAX, 8 * RSP);            // mov [rax + ..], rsp
  code = std::copy(tail.begin(), tail.end(), code);
  emit({0x48, 0x81, 0xc4});  // add rsp, reserve
  emit_value(reserve, 4);
  emit({0x58, 0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5d, 0x5b, 0xc3});  // pop rax (out), r15-r12, rbp, rbx; ret
  FlushInstructionCache(GetCurrentProcess(), start, static_cast<size_t>(code - start));
  return site;
}

// A bed's tail for the sprite site: the float the game's own next instruction stored at [rsp + 0xb0], into the bed's
// record. mov ecx, [rsp + 0xb0]; mov [rax + slot], rcx
inline constexpr std::array<uint8_t, 14> SPRITE_BED_TAIL = {0x8b, 0x8c, 0x24, 0xb0, 0x00, 0x00, 0x00,
                                                            0x48, 0x89, 0x88, 0x88, 0x00, 0x00, 0x00};

// One loaded copy of the plugin DLL (a copy at another path is another module with its own globals).
struct Plugin {
  int (*init)(const LoaderApi*) = nullptr;
  void (*game_started)(const LoaderApi*) = nullptr;
  Block* block = nullptr;
};

// The DLL loaded and its three exports found; all nullptr (and a failure) otherwise.
static Plugin LoadPlugin(const wchar_t* dll_path) {
  HMODULE dll = LoadLibraryW(dll_path);
  Expect(dll != nullptr, "the plugin DLL loads");
  if (dll == nullptr) return {};
  const auto block_export = reinterpret_cast<BlockExportFn>(GetProcAddress(dll, BLOCK_EXPORT));
  Plugin plugin = {
      .init = reinterpret_cast<int (*)(const LoaderApi*)>(GetProcAddress(dll, "attila64_plugin_init")),
      .game_started = reinterpret_cast<void (*)(const LoaderApi*)>(GetProcAddress(dll, "attila64_plugin_game_started")),
      .block = (block_export != nullptr) ? block_export() : nullptr,
  };
  if (plugin.init == nullptr || plugin.game_started == nullptr || plugin.block == nullptr) {
    Expect(false, "the plugin DLL has its three exports");
    return {};
  }
  return plugin;
}

// The plugin DLL's init and game start as the loader runs them, on a game image that holds every site's window and
// every tweak object (WriteTweakObjects), with no route note beside the test and no ReShade.ini keys but, with
// `game_code_key_off`, the game code row's install key 0 (an image without the game code that row stands on): every
// other row with a site goes in, and no patch site has other bytes to write. A hand-made stub's site is in with our
// jump, or only checked (its stub not built).
static Plugin ExpectPluginDll(const LoaderApi& api, const wchar_t* dll_path, bool game_code_key_off) {
  const Plugin plugin = LoadPlugin(dll_path);
  if (plugin.init == nullptr) return plugin;

  {
    const std::scoped_lock lock(log_mutex);
    log_lines.clear();
  }
  Expect(plugin.init(&api) == 0, "the plugin's init accepts the game");
  const Block* shared = plugin.block;
  Expect(shared->version == VERSION && shared->bytes == BlockBytes(VERSION), "the block's version and size");
  Expect(shared->status.load(std::memory_order_acquire) == STATUS_READY, "the block is READY after init");
  const uint32_t rows_off = (game_code_key_off ? RowBit(ROW_GAME_CODE) : 0);
  uint32_t rows = 0;
  for (const Site& site : SITES) {
    rows |= RowBit(static_cast<Row>(site.row)) & ~rows_off;
  }
  Expect(shared->rows_applied == rows && shared->rows_refused == rows_off, "every row with a site applied");
  // v2's initial values: the rows tried, the core default (no RenderCore key here), the battle core waiting, the render
  // core without its thread yet, the engine values waiting for the game's start, the other status words not built, the
  // scales neutral.
  bool scales_neutral = true;
  for (size_t scale = 0; scale < SCALE_COUNT; ++scale) {
    scales_neutral = scales_neutral && shared->scales[scale].load() == NEUTRAL_SCALES[scale];
  }
  Expect(shared->rows_tried == rows && shared->core_wanted.load() == CORE_WANTED_DEFAULT
             && shared->core_status.load() == (uint64_t{CORE_WAITING} << CORE_STATE_SHIFT)
             && shared->render_core_status.load() == (uint32_t{CORE_NO_THREAD} << CORE_STATE_SHIFT)
             && shared->engine_status.load() == 0
             && shared->tweaks_status.load() == TWEAKS_PENDING && shared->tweak_count == 0 && shared->tweaks == nullptr
             && shared->notices.load() == 0 && shared->scales_active.load() == 0 && scales_neutral
             && NEUTRAL_SCALES[SCALE_SPRITE_DISTANCE] == 0.0f && NEUTRAL_SCALES[SCALE_DEAD_UNITS] == 1.0f,
         "v2's initial values");
  Expect(Logged("renodx_attila64 2: game build ok") && Logged(", graphics ") && Logged("renodx_attila64: loader frame cap: ")
             && Logged("Changes drawing, sound memory and CPU core use only; battles, replays and multiplayer stay the same.")
             && Logged("PACK_ENTRY 0x3416d50: applied") && Logged("VIEW_DRAW_ENTRY 0x33f6120: applied")
             && Logged("STEP_ENTRY 0xbb4e00: applied") && Logged("renodx_attila64: shadow map: game's size")
             && Logged(game_code_key_off ? "grass on, grass cells on, campaign trees on, sprites on, game code off"
                                         : "grass on, grass cells on, campaign trees on, sprites on, game code on")
             && Logged("rows: bone flush on, pass draw on, battle core on, unit lod on, model detail on, shadow map on, "
                       "grass on, grass cells on, campaign trees on, sprites on, game code o"),
         "the init lines");
  size_t sites_in = 0;
  for (const Site& site : SITES) {
    char applied_line[96];
    std::snprintf(applied_line, sizeof(applied_line), "renodx_attila64: site %s %#x: applied%s", LABEL_WORDS[site.label],
                  site.address, ((site.kind == SITE_PATCH) ? " (game's bytes kept)" : ""));
    char unwritten_line[96];
    std::snprintf(unwritten_line, sizeof(unwritten_line), "renodx_attila64: site %s %#x: %s", LABEL_WORDS[site.label],
                  site.address, (game_code_key_off ? "skipped (OFF)" : "checked (check only)"));
    if (site.kind == SITE_RAW_STUB && !Logged(applied_line)) {
      Expect(Logged(unwritten_line)
                 && std::memcmp(reinterpret_cast<const void*>(uintptr_t{site.address}), site.window.data() + SITE_MARGIN,
                                site.length)
                        == 0,
             "a hand-made stub's site that is not in keeps the game's bytes and says why");
      continue;
    }
    ++sites_in;
    Expect(Logged(applied_line) && !Logged(unwritten_line), "each site has its applied line");
    if (site.kind != SITE_PATCH) {
      Expect(*reinterpret_cast<const uint8_t*>(site.address) == 0xe9, "each hook and stub site jumps to ours");
    } else {
      Expect(std::memcmp(reinterpret_cast<const void*>(uintptr_t{site.address}), site.window.data() + SITE_MARGIN, site.length) == 0,
             "each patch site keeps the game's bytes without a key");
    }
  }
  plugin.game_started(&api);
  // The count follows the sites table (a later site needs no edit here).
  char started[64];
  std::snprintf(started, sizeof(started), "game started: %zu sites unchanged", sites_in);
  Expect(shared->rows_changed.load() == 0 && Logged(started), "game start finds every site unchanged");
  // Every engine value checked once and published; nothing restart-only without its key.
  char tweaks_line[96];
  std::snprintf(tweaks_line, sizeof(tweaks_line), "renodx_attila64: tweaks: %zu checked, %zu ok, 2 absent, restart-only: 0 applied",
                TWEAKS.size() - 2, TWEAKS.size() - 2);
  Expect(shared->tweaks_status.load(std::memory_order_acquire) == TWEAKS_READY && shared->tweak_count == TWEAKS.size()
             && shared->tweaks != nullptr && Logged(tweaks_line) && !Logged(": refused ("),
         "game start checks and publishes the engine values");
  // The thread that runs the game's start is the render thread: captured once, with its own line.
  char render_thread[64];
  std::snprintf(render_thread, sizeof(render_thread), "renodx_attila64: render thread %lu", GetCurrentThreadId());
  const auto line_count = [] {
    const std::shared_lock lock(log_mutex);
    return log_lines.size();
  };
  const size_t lines_before = line_count();
  plugin.game_started(&api);
  const bool sites_line_last = [] {
    const std::shared_lock lock(log_mutex);
    return std::string_view(log_lines.back()).starts_with("renodx_attila64: game started: ");
  }();
  Expect(shared->render_core_status.load() == (uint32_t{CORE_WAITING} << CORE_STATE_SHIFT) && Logged(render_thread)
             && !Logged("render core:") && sites_line_last && line_count() == lines_before + 1,
         "game start captures the render thread once: a second start logs only its sites line (the engine values too)");
  return plugin;
}

}  // namespace twattila::attila64_plugin::test
