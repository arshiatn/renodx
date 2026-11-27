/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The plugin's sites against the game file itself: maps the Linux build's load segments at their addresses (read and
// write, never executed), applies the loader's stack-check rewrite, and checks the build id and every site's window,
// the hook through the fake loader, controls that must be refused, each engine value's static constructor call
// (tweaks.h: the object, its name and its default), and the built DLL's init.
// elf_sites_test.exe <renodx_attila64.dll> <game ELF>; SKIPPED (exit 77, reported by build.ps1 as not run)
// without the ELF or when the game's range is taken.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
    Expect(ApplySite(api, site, REPLACEMENTS[site.label], &applied) == REFUSAL_NONE, "the fake loader hooks the site");
    Expect(*at(site.address) == 0xe9 && SiteUnchanged(site, applied), "the site jumps to the hook, snapshot matches");
    std::memcpy(at(site.address), site.window.data() + SITE_MARGIN, site.length);
    Expect(CheckSite(site, FOREIGN).refusal == REFUSAL_NONE, "the site restored");
  }

  // Each engine value in the game's own code: its static constructor call passes the object, the name and a plain
  // default stored just before (mov byte / dword [rsp + n], imm). The objects are only built when the game runs.
  const auto word_at = [&](uintptr_t address) {
    uint32_t word = 0;
    std::memcpy(&word, at(address), sizeof(word));
    return word;
  };
  for (const TweakKind& kind : TWEAK_KINDS) {
    bool stores_table = false;
    for (uintptr_t address = kind.constructor; address < kind.constructor + 0x100; ++address) {
      stores_table = stores_table || word_at(address) == kind.table;
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

  WriteTweakObjects();
  ExpectPluginDll(api, argv[1]);

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
