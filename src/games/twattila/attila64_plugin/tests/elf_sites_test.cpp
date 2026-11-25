/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The plugin's sites against the game file itself: maps the Linux build's load segments at their addresses (read and
// write, never executed), applies the loader's stack-check rewrite, and checks the build id and every site's window,
// the hook through the fake loader, controls that must be refused, and the built DLL's init.
// elf_sites_test.exe <renodx_attila64.dll> <game ELF>; SKIPPED (exit 0) without the ELF or when the range is taken.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../hooks.hpp"
#include "../patcher.hpp"
#include "fake_loader.hpp"

int wmain(int argc, wchar_t** argv) {
  using namespace twattila::attila64_plugin;
  using namespace twattila::attila64_plugin::test;

  // First, before anything else can map there.
  if (!ReserveGame(PAGE_READWRITE)) {
    std::printf("SKIPPED: the game range 0x400000..0x527d000 is taken\n");
    return 0;
  }
  if (argc < 3) {
    std::printf("SKIPPED: elf_sites_test.exe <renodx_attila64.dll> <game ELF>\n");
    return 0;
  }
  HANDLE file = CreateFileW(argv[2], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  LARGE_INTEGER size = {};
  if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size)) {
    std::printf("SKIPPED: the game ELF could not be opened (error %lu)\n", GetLastError());
    return 0;
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
    if (offset + sizeof(T) <= elf.size()) std::memcpy(&value, elf.data() + offset, sizeof(T));
    return value;
  };
  uintptr_t game_end = 0;
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
    for (const uint32_t address : {site.address - SITE_MARGIN, site.address + site.length - 1, site.address + site.length + SITE_MARGIN - 1}) {
      *at(address) ^= 1;
      Expect(CheckSite(site, FOREIGN).refusal != REFUSAL_NONE, "control: one byte changed in the window");
      *at(address) ^= 1;
    }
    Site moved = site;
    ++moved.address;
    Expect(CheckSite(moved, FOREIGN).refusal != REFUSAL_NONE, "control: the site moved by one");

    AppliedSite applied;
    Expect(ApplySite(api, site, REPLACEMENTS[site.row], &applied) == REFUSAL_NONE, "the fake loader hooks the site");
    Expect(*at(site.address) == 0xe9 && SiteUnchanged(site, applied), "the site jumps to the hook, snapshot matches");
    std::memcpy(at(site.address), site.window.data() + SITE_MARGIN, site.length);
    Expect(CheckSite(site, FOREIGN).refusal == REFUSAL_NONE, "the site restored");
  }

  ExpectPluginDll(api, argv[1]);

  std::printf("%s: %d failure(s)\n", (failures == 0) ? "PASS" : "FAIL", failures);
  return failures;
}
