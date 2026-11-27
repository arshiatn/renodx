/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The plugin's sites against the game file itself: maps the Linux build's load segments at their addresses (read and
// write, never executed), applies the loader's stack-check rewrite, and checks the build id and every site's window,
// the hook or the patch through the fake loader, controls that must be refused, the shadow row's instructions, the
// rest pack's call, the grass rows' layout facts and camera getters, each engine value's static constructor call
// (tweaks.h: the object, its name and its default), and the built DLL's init.
// elf_sites_test.exe <renodx_attila64.dll> <game ELF>; SKIPPED (exit 77, reported by build.ps1 as not run)
// without the ELF or when the game's range is taken.

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "../hooks.hpp"
#include "../patcher.hpp"
#include "../tweaks.h"
#include "fake_loader.hpp"

inline constexpr int EXIT_SKIPPED = 77;

int wmain(int argc, wchar_t** argv) {
  using namespace twattila::attila64_plugin;
  using namespace twattila::attila64_plugin::test;

  // First, before anything else can map there.
  if (!ReserveGame(PAGE_READWRITE)) {
    std::printf("SKIPPED: the game range 0x400000..0x527d000 is taken\n");
    return EXIT_SKIPPED;
  }
  if (argc < 3) {
    std::printf("SKIPPED: elf_sites_test.exe <renodx_attila64.dll> <game ELF>\n");
    return EXIT_SKIPPED;
  }
  HANDLE file = CreateFileW(argv[2], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
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
      return 1;
    }
  }
  CloseHandle(file);

  // ELF64 program headers: e_phoff at 0x20, e_phentsize 0x36, e_phnum 0x38; p_type, p_offset 8, p_vaddr 16,
  // p_filesz 32, p_memsz 40.
  const auto field = [&]<typename T>(size_t offset) {
    T value = {};
    if (offset + sizeof(T) <= elf.size()) {
      std::memcpy(&value, elf.data() + offset, sizeof(T));
    }
    return value;
  };
  uintptr_t game_end = 0;
  uintptr_t writable_lo = 0;  // the load segment the game writes: its data
  uintptr_t writable_hi = 0;
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
    game_end = std::max<uintptr_t>(game_end, address + memory_bytes);
    if ((field.operator()<uint32_t>(header + 4) & 2) != 0) {  // PF_W
      writable_lo = address;
      writable_hi = address + memory_bytes;
    }
  }
  elf = {};
  const int rewritten = ApplyStackRule(reinterpret_cast<uint8_t*>(GAME_LO), reinterpret_cast<const uint8_t*>(game_end));
  std::printf("  mapped %#llx..%#llx, %d stack-check reads rewritten\n", uint64_t{GAME_LO}, uint64_t{game_end}, rewritten);

  char exe_dir[MAX_PATH];
  GetModuleFileNameA(nullptr, exe_dir, MAX_PATH);
  *std::strrchr(exe_dir, '\\') = '\0';
  LoaderApi api = MakeApi(exe_dir, game_end);
  loader = &api;
  const auto at = [](uintptr_t address) { return reinterpret_cast<uint8_t*>(address); };

  Expect(game_end == GAME_HI_MIN, "the last load segment ends where sites.h says");
  Expect(CheckIdentity(api) == STATUS_READY, "the game file is the build sites.h was made for");
  *at(BUILD_ID_NOTE_ADDRESS + 16) ^= 1;
  Expect(CheckIdentity(api) == STATUS_WRONG_GAME, "control: a wrong build id");
  *at(BUILD_ID_NOTE_ADDRESS + 16) ^= 1;

  for (const Site& site : SITES) {
    std::printf("  site %s %#x\n", LABEL_WORDS[site.label], site.address);
    Expect(CheckSite(site, FOREIGN).refusal == REFUSAL_NONE, "the site's window is the game's");
    const uint32_t window_last = site.address + site.length + SITE_MARGIN - 1;
    for (const uint32_t address : {site.address - SITE_MARGIN, site.address + site.length - 1, window_last}) {
      *at(address) ^= 1;
      Expect(CheckSite(site, FOREIGN).refusal != REFUSAL_NONE, "control: one byte changed in the window");
      *at(address) ^= 1;
    }
    Site moved = site;
    ++moved.address;
    Expect(CheckSite(moved, FOREIGN).refusal != REFUSAL_NONE, "control: the site moved by one");

    AppliedSite applied;
    if (site.kind == SITE_PATCH) {
      // Any other bytes do for the write and its way back.
      std::array<uint8_t, SITE_MAX_LENGTH> other = {};
      for (size_t index = 0; index < site.length; ++index) {
        other[index] = static_cast<uint8_t>(~site.window[SITE_MARGIN + index]);
      }
      Expect(ApplyPatch(api, site, other.data(), &applied) == REFUSAL_NONE && SiteUnchanged(site, applied)
                 && std::memcmp(at(site.address), other.data(), site.length) == 0,
             "the fake loader writes the patch site, snapshot matches");
      Expect(RevertPatch(api, site, &applied) && CheckSite(site, FOREIGN).refusal == REFUSAL_NONE, "the patch site put back");
      continue;
    }
    Expect(ApplySite(api, site, REPLACEMENTS[site.label], &applied) == REFUSAL_NONE, "the fake loader hooks the site");
    Expect(*at(site.address) == 0xe9 && SiteUnchanged(site, applied), "the site jumps to the hook, snapshot matches");
    std::memcpy(at(site.address), site.window.data() + SITE_MARGIN, site.length);
    Expect(CheckSite(site, FOREIGN).refusal == REFUSAL_NONE, "the site restored");
  }

  // The shadow row in the game's own code: each size is the immediate of a mov rax, imm64 that holds one float twice
  // (width and height: 2048, or Medium's 1024), each cascade number the immediate of a mov dword [r13 + disp32], imm32.
  const auto word_at = [&](uintptr_t address) {
    uint32_t word = 0;
    std::memcpy(&word, at(address), sizeof(word));
    return word;
  };
  for (const Site& site : SITES) {
    if (site.row != ROW_SHADOW_MAP) continue;
    if (IsShadowSizeSite(site)) {
      std::array<float, 2> size = {};
      std::memcpy(size.data(), at(site.address), sizeof(size));
      const float game_size = ((site.label == LABEL_SHADOW_SIZE_MEDIUM) ? 1024.f : 2048.f);
      Expect(*at(site.address - 2) == 0x48 && *at(site.address - 1) == 0xb8 && size[0] == game_size && size[1] == game_size,
             "shadow: a size site is a mov rax, imm64 of the game's size twice");
    } else {
      Expect(site.length == sizeof(uint32_t) && *at(site.address - 7) == 0x41 && *at(site.address - 6) == 0xc7
                 && *at(site.address - 5) == 0x85 && word_at(site.address) <= 4,
             "shadow: a cascade site is the immediate of a mov dword [r13 + disp32], imm32");
    }
  }
  // The rest pack's return address follows a call of the pack function.
  Expect(*at(REST_PACK_RETURN - 5) == 0xe8 && REST_PACK_RETURN + static_cast<int32_t>(word_at(REST_PACK_RETURN - 4)) == SITES[LABEL_PACK_ENTRY].address,
         "rest pack: its return address is right after a call of the pack site");

  // The grass and campaign tree rows in the game's own code: each instruction that holds a field offset the hooks and
  // model_lods.hpp read, the two camera getters' whole code, and the draw's code from its entry to its no-LOD return
  // (reads, the two getter calls and writes to its own frame only: what the hook's own early return stands on).
  struct CodeFact {
    uint32_t address;
    std::initializer_list<uint8_t> bytes;
    const char* what;
  };
  for (const CodeFact& fact : std::initializer_list<CodeFact>{
           {0x33fb1c5, {0xf3, 0x0f, 0x2a, 0x83, 0xd0, 0x00, 0x00, 0x00}, "grass update: the radius is an int at +0xd0"},
           {0x33fb27d, {0x8b, 0x83, 0xd0, 0x00, 0x00, 0x00, 0x0f, 0xaf, 0xc0}, "grass update: the cells get the radius squared"},
           {0x33fd634, {0x45, 0x8b, 0x66, 0x24}, "grass cell: the record count at +0x24"},
           {0x33fd660, {0x49, 0x8b, 0x56, 0x28}, "grass cell: the records at +0x28"},
           {0x33fd6e4, {0xf3, 0x0f, 0x59, 0x95, 0xd4, 0x00, 0x00, 0x00}, "grass cell: the bias is a float at +0xd4"},
           {0x33fd962, {0x48, 0x8b, 0xbd, 0xe8, 0x00, 0x00, 0x00}, "grass cell: the model node at +0xe8"},
           {0x33fd969, {0x48, 0x8b, 0x85, 0xf8, 0x00, 0x00, 0x00}, "grass cell: the slots at +0xf8"},
           {0x33fd97a, {0x42, 0x0f, 0xb6, 0x4c, 0x2a, 0xff, 0x8b, 0x04, 0x88}, "grass cell: a record's slot byte picks a uint32_t"},
           {0x33fd9ac, {0x49, 0x83, 0xc5, 0x12}, "grass cell: a record is 18 bytes"},
           {0x33fd9d0, {0x8b, 0x8f, 0xf4, 0x00, 0x00, 0x00}, "scrub: the slot count at +0xf4"},
           {0x33fd9e8, {0x4c, 0x8b, 0x87, 0xf8, 0x00, 0x00, 0x00}, "scrub: the slots at +0xf8 again"},
           {0x33f3e2f, {0x3b, 0x47, 0x34}, "grass draw: the node's model count at +0x34"},
           {0x33f3e34, {0x48, 0x8b, 0x4f, 0x38, 0x4c, 0x8b, 0x3c, 0xc1}, "grass draw: the node's models at +0x38"},
           {0x33f3e62, {0x48, 0x8b, 0x7d, 0x50, 0x48, 0x8b, 0x07, 0xff, 0x50, 0x58}, "grass draw: the scene at +0x50, its slot 0x58"},
           {0x33f3e77, {0xff, 0x91, 0x80, 0x00, 0x00, 0x00}, "grass draw: the camera's slot 0x80"},
           {0x33f3e80, {0x41, 0x8b, 0x47, 0x44}, "grass draw: the model's LOD count at +0x44"},
           {0x33f3ec9, {0xf3, 0x41, 0x0f, 0x10, 0x46, 0x18}, "grass draw: the instance's bias at +0x18"},
           {0x33f3ecf, {0xf3, 0x0f, 0x58, 0x87, 0x98, 0x0e, 0x00, 0x00}, "grass draw: the node's offset at +0xe98"},
           {0x33f3edb, {0x49, 0x8b, 0x4f, 0x48}, "grass draw: the model's LODs at +0x48"},
           {0x33f3ef4, {0xf3, 0x0f, 0x10, 0x4b, 0x50, 0x0f, 0x2e, 0xc8, 0x76, 0x06}, "grass draw: a LOD's distance at +0x50, above the key"},
           {0x33f3efe, {0x80, 0x7b, 0x5c, 0x00}, "grass draw: a LOD's enabled byte at +0x5c"},
           {0x33f42a2, {0x3b, 0x45, 0x34}, "campaign tree: the node's model count at +0x34"},
           {0x33f42a7, {0x48, 0x8b, 0x4d, 0x38, 0x4c, 0x8b, 0x2c, 0xc1}, "campaign tree: the node's models at +0x38"},
           {0x33f4436, {0x41, 0x8b, 0x45, 0x44}, "campaign tree: the model's LOD count at +0x44"},
           {0x33f4442, {0x49, 0x8b, 0x4d, 0x48}, "campaign tree: the model's LODs at +0x48"},
           {0x33f4454, {0xf3, 0x0f, 0x10, 0x45, 0x50}, "campaign tree: a LOD's distance at +0x50"},
           {0x33f445e, {0x80, 0x7d, 0x5c, 0x00}, "campaign tree: a LOD's enabled byte at +0x5c"},
           // The parallel effects row: the bytes the loader checks before it hooks its five guards, and each
           // instruction of the walk and of the per-type update whose number sites.h holds.
           {0x34e0130, {0x8b, 0x47, 0x14, 0x85, 0xc0}, "effects: the pool take's first bytes"},
           {0x34e0170, {0x8b, 0x46, 0x04, 0x85, 0xc0}, "effects: the pool's list take's first bytes"},
           {0x34e01a0, {0x53, 0x48, 0x83, 0xec, 0x10}, "effects: the pool return's first bytes"},
           {0x32ffa60, {0x48, 0x8b, 0xbf, 0x28, 0x02, 0x00, 0x00, 0x48, 0x8b, 0x07, 0x48, 0x8b, 0x40, 0x40, 0xff, 0xe0},
            "effects: the spawn thunk"},
           {0x35314b0, {0x41, 0x57, 0x41, 0x56, 0x53, 0x49, 0x89, 0xfe, 0x41, 0x8b, 0x5e, 0x30, 0x41, 0x3b, 0x5e, 0x34, 0x74, 0x3c},
            "effects: the piece body's first bytes"},
           {0x32fc384, {0x49, 0x8b, 0x84, 0x24}, "effect walk: mov rax, [r12 + the table's slots]"},
           {0x32fc38c, {0x41, 0x8b, 0x8c, 0x24}, "effect walk: mov ecx, [r12 + the table's capacity]"},
           {0x32fc394, {0x48, 0x8d, 0x0c, 0x49, 0x48, 0x8d, 0x0c, 0xc8}, "effect walk: the end is slots + capacity x 24"},
           {0x32fc2f6, {0x49, 0x8d, 0x84, 0x24}, "effect walk: lea rax, [r12 + the empty key]"},
           {0x32fc303, {0x49, 0x8d, 0x84, 0x24}, "effect walk: lea rax, [r12 + the deleted key]"},
           {0x32fc32c, {0x48, 0x8b, 0x7e}, "effect walk: mov rdi, [rsi + the slot's value]"},
           {0x32fc330, {0x48, 0x85, 0xff}, "effect walk: a null value is skipped"},
           {0x32fc354, {0x48, 0x83, 0xc6}, "effect walk: add rsi, a slot's bytes"},
           {0x32fc36a, {0xe8}, "effect walk: the first key compare is a direct call"},
           {0x32fc37b, {0xe8}, "effect walk: the second key compare is a direct call"},
           {0x34ad19e, {0x49, 0x89, 0xfe}, "effect type update: r14 is the type"},
           {0x34ad1c6, {0x41, 0x8b, 0x5e}, "effect type update: mov ebx, [r14 + the instance count]"},
           // The key compare: lengths (a uint32_t at 0) first, then the bytes at +8; it only reads and returns in al.
           {0x50def0, {0x44, 0x8b, 0x07, 0x44, 0x3b, 0x06, 0x75, 0x2a, 0x45, 0x85, 0xc0, 0xb0, 0x01, 0x74, 0x22, 0x48, 0x8b,
                       0x57, 0x08, 0x48, 0x8b, 0x76, 0x08},
            "effect walk: the key compare reads two counted strings"},
       }) {
    Expect(std::equal(fact.bytes.begin(), fact.bytes.end(), at(fact.address)), fact.what);
  }
  Expect(std::memcmp(at(GET_SCENE_CAMERA), GET_SCENE_CAMERA_CODE.data(), GET_SCENE_CAMERA_CODE.size()) == 0
             && std::memcmp(at(GET_CAMERA_POSITION), GET_CAMERA_POSITION_CODE.data(), GET_CAMERA_POSITION_CODE.size()) == 0,
         "grass draw: the two camera getters hold the code the hook expects");
  {
    // The hash init takes: over [entry, the instruction after the no-LOD jump), with the hook's site in it. One byte
    // of it changed is another hash.
    uint8_t* draw = at(SITES[LABEL_GRASS_DRAW_ENTRY].address);
    Expect(Fnv1a(draw, GRASS_DRAW_NO_LOD_BYTES) == GRASS_DRAW_NO_LOD_HASH && draw[GRASS_DRAW_NO_LOD_BYTES - 5] == 0xe9,
           "grass draw: its code up to the no-LOD return is the code that was read");
    draw[0x80] ^= 1;
    Expect(Fnv1a(draw, GRASS_DRAW_NO_LOD_BYTES) != GRASS_DRAW_NO_LOD_HASH, "grass draw: a patched byte is found");
    draw[0x80] ^= 1;
  }

  // Each engine value in the game's own code: its static constructor call passes the object, the name and a plain
  // default stored just before (mov byte / dword [rsp + n], imm). The objects are only built when the game runs.
  for (const TweakKind& kind : TWEAK_KINDS) {
    // mov qword [register], imm32: REX.W (with or without B), c7, a register-indirect operand, the table.
    bool stores_table = false;
    for (uintptr_t address = kind.constructor; address < kind.constructor + 0x100; ++address) {
      const uint8_t operand = *at(address + 2);
      stores_table = stores_table
                     || ((*at(address) == 0x48 || *at(address) == 0x49) && *at(address + 1) == 0xc7
                         && (operand & 0xf8) == 0 && (operand & 7) != 4 && (operand & 7) != 5
                         && word_at(address + 3) == kind.table);
    }
    Expect(stores_table, "tweaks: each type's constructor stores its table");
  }
  size_t tweaks_in_code = 0;
  for (const TweakEntry& entry : TWEAKS) {
    if (entry.object == 0) continue;
    const TweakKind& kind = TWEAK_KINDS[entry.type];
    bool ok = entry.object >= writable_lo && entry.object + kind.value_offset + kind.value_bytes <= writable_hi;
    ok = ok && *at(entry.constructor_call) == 0xe8
         && entry.constructor_call + 5 + word_at(entry.constructor_call + 1) == kind.constructor;
    ok = ok && entry.default_store < entry.constructor_call && entry.constructor_call - entry.default_store < 0x80;
    // c6 (byte) or c7 (dword), [rsp], [rsp + disp8] or [rsp + disp32], then the immediate.
    const uint8_t mode = *at(entry.default_store + 1);
    const uintptr_t immediate = entry.default_store + 3 + ((mode == 0x44) ? 1 : ((mode == 0x84) ? 4 : 0));
    ok = ok && *at(entry.default_store) == ((kind.value_bytes == 1) ? 0xc6 : 0xc7)
         && (mode == 0x04 || mode == 0x44 || mode == 0x84) && *at(entry.default_store + 2) == 0x24;
    uint32_t stored = 0;
    std::memcpy(&stored, at(immediate), kind.value_bytes);
    ok = ok && stored == entry.default_bits;
    bool passes_object = false;
    bool passes_name = false;
    for (uintptr_t address = entry.default_store; ok && address < entry.constructor_call; ++address) {
      passes_object = passes_object || (*at(address) == 0xbf && word_at(address + 1) == entry.object);  // mov edi
      if (*at(address) != 0xbe) continue;  // mov esi
      const uintptr_t text = word_at(address + 1);
      passes_name = passes_name
                    || (text >= GAME_LO && text + TWEAK_NAME_CAPACITY <= game_end
                        && std::strcmp(reinterpret_cast<const char*>(text), entry.name) == 0);
    }
    if (ok && passes_object && passes_name) {
      ++tweaks_in_code;
    } else {
      std::printf("  tweak %s: not as tweaks.h says\n", entry.name);
    }
  }
  std::printf("  tweaks: %zu constructor calls match\n", tweaks_in_code);
  Expect(tweaks_in_code == TWEAKS.size() - 2, "tweaks: every object's constructor call passes its name and default");

  // The parallel effects row's numbers in the game's own code (sites.h's effect block).
  Expect(std::memcmp(at(EFFECT_FLAG_READ_ADDRESS), EFFECT_FLAG_READ.data(), EFFECT_FLAG_READ.size()) == 0
             && EFFECT_FLAG_READ_ADDRESS + EFFECT_FLAG_READ.size() + word_at(EFFECT_FLAG_READ_ADDRESS + 2) == EFFECT_FLAG
             && EFFECT_FLAG == TWEAKS.back().object + TWEAK_KINDS[TWEAK_BOOL].value_offset && TWEAKS.back().loader_writes,
         "effects: the game's read of the flag is the code that was read, and the flag is the last tweak's value");
  Expect(word_at(0x32fc384 + 4) == EFFECT_TABLE_SLOTS && word_at(0x32fc38c + 4) == EFFECT_TABLE_CAPACITY
             && word_at(0x32fc2f6 + 4) == EFFECT_EMPTY_KEY && word_at(0x32fc303 + 4) == EFFECT_DELETED_KEY
             && *at(0x32fc32c + 3) == EFFECT_SLOT_VALUE && *at(0x32fc354 + 3) == EFFECT_SLOT_BYTES && EFFECT_SLOT_BYTES == 24
             && *at(0x34ad1c6 + 3) == EFFECT_TYPE_COUNT,
         "effect walk: each offset of sites.h is the operand of the walk's own instruction");
  Expect(0x32fc36a + 5 + word_at(0x32fc36a + 1) == EFFECT_KEY_EQUAL && 0x32fc37b + 5 + word_at(0x32fc37b + 1) == EFFECT_KEY_EQUAL,
         "effect walk: both key compares call the function sites.h names");

  WriteTweakObjects();
  WriteLoaderGuards();
  ExpectPluginDll(api, argv[1]);
  Expect(!Logged("grass draw: "), "grass draw: init finds the code that was read, the early return is on");

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
