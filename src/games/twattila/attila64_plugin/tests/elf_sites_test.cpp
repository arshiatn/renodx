/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The plugin's sites against the game file itself: maps the Linux build's load segments at their addresses (read and
// write; executed only around the sprite site, last), applies the loader's stack-check rewrite, and checks the build
// id and every site's window, the hook, the patch or the mid-function stub through the fake loader, controls that
// must be refused, the shadow row's instructions, the rest pack's call, the grass and sprite rows' layout facts and
// camera getters, each engine value's static constructor call (tweaks.h: the object, its name and its default), and
// the built DLL's init.
// elf_sites_test.exe <renodx_attila64.dll> <game ELF>; SKIPPED (exit 77, reported by build.ps1 as not run)
// without the ELF or when the game's range is taken.

#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "../hooks.hpp"
#include "../patcher.hpp"
#include "../tweaks.h"
#include "fake_loader.hpp"

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
  GameImage image;
  if (const int mapped = MapGameElf(argv[2], &image); mapped != 0) return mapped;

  char exe_dir[MAX_PATH];
  GetModuleFileNameA(nullptr, exe_dir, MAX_PATH);
  *std::strrchr(exe_dir, '\\') = '\0';
  LoaderApi api = MakeApi(exe_dir, image.end);
  loader = &api;
  const auto at = [](uintptr_t address) { return reinterpret_cast<uint8_t*>(address); };

  Expect(image.end == GAME_HI_MIN, "the last load segment ends where sites.h says");
  Expect(CheckIdentity(api) == STATUS_READY, "the game file is the build sites.h was made for");
  *at(BUILD_ID_NOTE_ADDRESS + 16) ^= 1;
  Expect(CheckIdentity(api) == STATUS_WRONG_GAME, "control: a wrong build id");
  *at(BUILD_ID_NOTE_ADDRESS + 16) ^= 1;

  const auto word_at = [&](uintptr_t address) {
    uint32_t word = 0;
    std::memcpy(&word, at(address), sizeof(word));
    return word;
  };
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
    if (site.kind == SITE_MID) {
      Expect(ApplyMidSite(api, site, reinterpret_cast<trampoline::MidHandler>(REPLACEMENTS[site.label]), &block.scales_active,
                          MID_SITE_SCALES[site.label], &applied)
                 == REFUSAL_NONE,
             "the site's stub is built near the game and the fake loader writes the jump");
      // The stub's trampoline: the site's own bytes, then a jump back to the instruction after them.
      const auto* copied = static_cast<const uint8_t*>(applied.original);
      Expect(std::memcmp(copied, site.window.data() + SITE_MARGIN, site.length) == 0 && copied[site.length] == 0xe9
                 && reinterpret_cast<uintptr_t>(copied) + site.length + 5 + static_cast<int32_t>(word_at(reinterpret_cast<uintptr_t>(copied) + site.length + 1))
                        == site.address + site.length,
             "the stub runs the site's own bytes and jumps back behind them");
      Expect(std::all_of(at(site.address + 5), at(site.address + site.length), [](uint8_t byte) { return byte == 0xcc; }),
             "the site's bytes after the jump are int3");
    } else {
      Expect(ApplySite(api, site, REPLACEMENTS[site.label], &applied) == REFUSAL_NONE, "the fake loader hooks the site");
    }
    Expect(*at(site.address) == 0xe9 && SiteUnchanged(site, applied), "the site jumps to ours, snapshot matches");
    std::memcpy(at(site.address), site.window.data() + SITE_MARGIN, site.length);
    Expect(CheckSite(site, FOREIGN).refusal == REFUSAL_NONE, "the site restored");
  }

  // The shadow row in the game's own code: each size is the immediate of a mov rax, imm64 that holds one float twice
  // (width and height: 2048, or Medium's 1024), each cascade number the immediate of a mov dword [r13 + disp32], imm32.
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
           {0xa50411, {0x49, 0x8b, 0x85, 0xc8, 0x00, 0x00, 0x00}, "sprite: the soldier's display tables at +0xc8, in rax"},
           {0xa5045a, {0x48, 0x8d, 0x88, 0x70, 0x03, 0x00, 0x00, 0x48, 0x05, 0x80, 0x03, 0x00, 0x00, 0xeb, 0x2b},
            "sprite: the first size's distance at +0x370 in rcx, its fade width 0x10 on in rax"},
           {0xa50487, {0x48, 0x8d, 0x88, 0x7c, 0x03, 0x00, 0x00, 0x48, 0x05, 0x8c, 0x03, 0x00, 0x00, 0xf3, 0x0f, 0x58, 0x05},
            "sprite: the fourth size's two floats, then the constant added to the random value"},
           {0xa504a4, {0xf3, 0x0f, 0x11, 0x84, 0x24, 0xb0, 0x00, 0x00, 0x00}, "sprite: the game stores xmm0 right after the site"},
           {0xa50541, {0x41, 0x8a, 0x85, 0x1c, 0x01, 0x00, 0x00}, "sprite: the soldier's flags byte at +0x11c"},
           {0xa56725, {0x48, 0x89, 0xd3}, "sprite: the reader of the alive bit keeps the soldier's record (rdx) in rbx"},
           {0xa56786, {0xf6, 0x83, 0x1c, 0x01, 0x00, 0x00, 0x01, 0x75, 0x3a, 0xf3, 0x0f, 0x10, 0x8b, 0x18, 0x01, 0x00, 0x00},
            "sprite: only with bit 0 of the flags byte clear it reads the record's float at +0x118"},
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
           // The walk's second way in: a two-byte jump to it, 16 bytes before (the hook comment's note).
           {0x32fbf50, {0xeb, 0x0e}, "effect walk: the jump to the walk before it"},
           // The key compare: lengths (a uint32_t at 0) first, then the bytes at +8; it only reads and returns in al.
           {0x50def0, {0x44, 0x8b, 0x07, 0x44, 0x3b, 0x06, 0x75, 0x2a, 0x45, 0x85, 0xc0, 0xb0, 0x01, 0x74, 0x22, 0x48, 0x8b,
                       0x57, 0x08, 0x48, 0x8b, 0x76, 0x08},
            "effect walk: the key compare reads two counted strings"},
       }) {
    Expect(std::equal(fact.bytes.begin(), fact.bytes.end(), at(fact.address)), fact.what);
  }
  // addss xmm0, [rip + disp32] at 0xa50494: the random value comes to the site less 0.5.
  Expect(word_at(0xa5049c + static_cast<int32_t>(word_at(0xa50498))) == 0xbf000000, "sprite: the constant before the site is -0.5");
  Expect(std::memcmp(at(GET_SCENE_CAMERA), GET_SCENE_CAMERA_CODE.data(), GET_SCENE_CAMERA_CODE.size()) == 0
             && std::memcmp(at(GET_CAMERA_POSITION), GET_CAMERA_POSITION_CODE.data(), GET_CAMERA_POSITION_CODE.size()) == 0,
         "grass draw: the two camera getters hold the code the hook expects");
  {
    // The hash init takes: over [entry, the instruction after the no-LOD jump), with the hook's site in it. One byte
    // of it changed is another hash.
    uint8_t* draw = at(SITES[LABEL_GRASS_DRAW_ENTRY].address);
    Expect(battle_step::Fnv1a<uint32_t>(draw, GRASS_DRAW_NO_LOD_BYTES) == GRASS_DRAW_NO_LOD_HASH && draw[GRASS_DRAW_NO_LOD_BYTES - 5] == 0xe9,
           "grass draw: its code up to the no-LOD return is the code that was read");
    draw[0x80] ^= 1;
    Expect(battle_step::Fnv1a<uint32_t>(draw, GRASS_DRAW_NO_LOD_BYTES) != GRASS_DRAW_NO_LOD_HASH, "grass draw: a patched byte is found");
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
    bool ok = entry.object >= image.writable_lo && entry.object + kind.value_offset + kind.value_bytes <= image.writable_hi;
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
                    || (text >= GAME_LO && text + TWEAK_NAME_CAPACITY <= image.end
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
             && EFFECT_FLAG == TWEAKS[tweaks::LOADER_FLAG_TWEAK].object + TWEAK_KINDS[TWEAK_BOOL].value_offset,
         "effects: the game's read of the flag is the code that was read, and the flag is the value of the tweak the loader "
         "writes");
  Expect(word_at(0x32fc384 + 4) == EFFECT_TABLE_SLOTS && word_at(0x32fc38c + 4) == EFFECT_TABLE_CAPACITY
             && word_at(0x32fc2f6 + 4) == EFFECT_EMPTY_KEY && word_at(0x32fc303 + 4) == EFFECT_DELETED_KEY
             && *at(0x32fc32c + 3) == EFFECT_SLOT_VALUE && *at(0x32fc354 + 3) == EFFECT_SLOT_BYTES && EFFECT_SLOT_BYTES == 24
             && *at(0x34ad1c6 + 3) == EFFECT_TYPE_COUNT,
         "effect walk: each offset of sites.h is the operand of the walk's own instruction");
  Expect(0x32fc36a + 5 + word_at(0x32fc36a + 1) == EFFECT_KEY_EQUAL && 0x32fc37b + 5 + word_at(0x32fc37b + 1) == EFFECT_KEY_EQUAL,
         "effect walk: both key compares call the function sites.h names");

  WriteTweakObjects();
  // Every row in, the game code row's stubs with their gap too: one game run's code near the game fits what the
  // loader's own code leaves of its page at the loader's worst (LOADER_OWN_NEAR_CODE_BYTES, read from loader 1.3.28's
  // source: about 0x720 bytes of its own requests and 32 bytes for each of its 77 hooks, 0x10c0 with every one on;
  // about 3 KiB in a default start). So 0xf40 are ours; a row that needs more could leave the game code row, the last
  // to ask, without memory at a real start.
  constexpr size_t LOADER_OWN_NEAR_CODE_BYTES = 0x10c0;
  WriteLoaderGuards();
  const size_t near_code_before = near_code_used;
  // The battle step row reads the game's own code here: as it expects, with no line. The contact split row too, at
  // init and again at the game's start, with every site of ours in; its bit is not in the build's default, so its
  // lane says off.
  const Plugin plugin = ExpectPluginDll(api, argv[1], false, ENGINE_NOT_USED_YET, ENGINE_OFF);
  Expect(!Logged("battle step: "), "battle step: init finds the code that was read");
  Expect(!Logged("contact split: ") && plugin.block != nullptr
             && plugin.block->engine_status.load()
                    == ((uint32_t{ENGINE_NOT_USED_YET} << ENGINE_STEP_SHIFT) | (uint32_t{ENGINE_OFF} << ENGINE_SPLIT_SHIFT)),
         "contact split: init and the game's start find the code that was read");
  std::printf("  code near the game: %#zx bytes\n", near_code_used - near_code_before);
  Expect(near_code_used - near_code_before <= LOADER_NEAR_CODE_BYTES - LOADER_OWN_NEAR_CODE_BYTES,
         "dll: one game run's code near the game, the game code row's too, fits the loader's page");
  Expect(!Logged("grass draw: "), "grass draw: init finds the code that was read, the early return is on");

  // The sprite site in the game's own code, with the DLL's stub and handler: a bed enters where the game loads the
  // soldier's display tables (0xa50411), the game's code picks the size's two floats through its own jump table, makes
  // the random value - 0.5, runs into the site and stores the result; the instruction after the store is replaced by
  // a jump back to the bed. The soldier's record is r13; with a linked record at +0xd8 the size and the random value
  // come from that record's +0x20, the tables still from the soldier's.
  constexpr uintptr_t SPRITE_ENTER = 0xa50411;
  constexpr uintptr_t SPRITE_LEAVE = 0xa504ad;
  constexpr size_t RECORD_SIZE = 0xc;
  constexpr size_t RECORD_RANDOM = 0xe0;
  constexpr size_t SOLDIER_LINK = 0xd8;
  constexpr size_t LINK_RECORD = 0x20;
  auto* bed_code = static_cast<uint8_t*>(api.code_near_game(MID_BED_CAPACITY + SPRITE_BED_TAIL.size()));
  DWORD protection = 0;
  Expect(plugin.block != nullptr && bed_code != nullptr
             && VirtualProtect(at(SPRITE_ENTER), SPRITE_LEAVE + 5 - SPRITE_ENTER, PAGE_EXECUTE_READWRITE, &protection) != FALSE,
         "sprite run: the bed's memory, the game's code executable");
  if (plugin.block != nullptr && bed_code != nullptr && protection != 0) {
    uintptr_t capture = 0;
    WriteMidBed(bed_code, {}, SPRITE_ENTER, 0x100, 0, SPRITE_BED_TAIL, &capture);
    const auto back = static_cast<uint32_t>(capture - (SPRITE_LEAVE + 5));
    *at(SPRITE_LEAVE) = 0xe9;
    std::memcpy(at(SPRITE_LEAVE + 1), &back, sizeof(back));
    FlushInstructionCache(GetCurrentProcess(), at(SPRITE_ENTER), SPRITE_LEAVE + 5 - SPRITE_ENTER);

    const std::array<float, 4> distances = {90.f, 110.f, 200.f, 1000.f};
    const std::array<float, 4> fade_widths = {22.5f, 27.5f, 50.f, 250.f};
    std::array<uint8_t, 0x400> display = {};
    std::array<uint8_t, 0x120> soldier = {};
    std::array<uint8_t, 0x120> linked = {};
    std::array<uint8_t, 0x28> link = {};
    std::memcpy(display.data() + DISPLAY_SPRITE_DISTANCES, distances.data(), sizeof(distances));
    std::memcpy(display.data() + DISPLAY_SPRITE_DISTANCES + 0x10, fade_widths.data(), sizeof(fade_widths));
    const auto put_pointer = [](uint8_t* to, const uint8_t* pointer) { std::memcpy(to, &pointer, sizeof(pointer)); };
    put_pointer(soldier.data() + SOLDIER_DISPLAY, display.data());
    put_pointer(link.data() + LINK_RECORD, linked.data());
    Block* shared = plugin.block;
    // One soldier through the game's code: true when the game stored `expected` bit for bit (and holds it in xmm0),
    // rax and rcx point at the size's two floats, and every other register and the red zone are as they went in.
    const auto sprite_once = [&](bool through_link, uint32_t size, float random, bool alive, float expected) {
      // The record the game must not read holds another size and random value.
      const uint32_t other_size = (size + 1) % 4;
      const float other_random = random + 0.125f;
      std::memcpy((through_link ? linked : soldier).data() + RECORD_SIZE, &size, sizeof(size));
      std::memcpy((through_link ? linked : soldier).data() + RECORD_RANDOM, &random, sizeof(random));
      std::memcpy((through_link ? soldier : linked).data() + RECORD_SIZE, &other_size, sizeof(other_size));
      std::memcpy((through_link ? soldier : linked).data() + RECORD_RANDOM, &other_random, sizeof(other_random));
      put_pointer(soldier.data() + SOLDIER_LINK, (through_link ? link.data() : nullptr));
      soldier[SOLDIER_FLAGS] = static_cast<uint8_t>(0x52 | (alive ? SOLDIER_ALIVE : 0));
      Registers in;
      for (size_t number = 0; number < in.gpr.size(); ++number) {
        in.gpr[number] = 0x0101010101010101 * (number + 1);
      }
      in.gpr[13] = reinterpret_cast<uintptr_t>(soldier.data());
      in.flags = 0x202;
      for (size_t number = 0; number < in.xmm.size(); ++number) {
        in.xmm[number].fill(static_cast<uint8_t>(0x30 + number));
      }
      Registers out;
      reinterpret_cast<MidBed>(bed_code)(&in, &out);
      Registers kept = in;
      const size_t table_entry = DISPLAY_SPRITE_DISTANCES + (((size < 4) ? size : 0) * sizeof(float));
      kept.gpr[0] = reinterpret_cast<uintptr_t>(display.data() + table_entry + 0x10);
      kept.gpr[1] = reinterpret_cast<uintptr_t>(display.data() + table_entry);
      kept.gpr[4] = out.gpr[4];  // rsp: the bed's own
      kept.flags = out.flags;    // the game's size compare
      kept.slot = std::bit_cast<uint32_t>(expected);
      kept.red_zone.fill(in.gpr[7]);
      kept.xmm[0] = {};  // the game's movss load clears the rest of xmm0
      std::memcpy(kept.xmm[0].data(), &expected, sizeof(expected));
      return out == kept;
    };
    bool neutral = sprite_once(false, 1, 0.75f, true, ((0.75f + -0.5f) * fade_widths[1]) + distances[1])
                   && !Logged("SPRITE_DISTANCE: first call");
    bool capped = true;
    bool dead_scaled = true;
    for (const bool through_link : {false, true}) {
      for (const uint32_t size : {0u, 1u, 2u, 3u, 7u}) {
        const size_t entry = (size < 4) ? size : 0;
        const float spread = (0.75f + -0.5f) * fade_widths[entry];
        const float game = spread + distances[entry];
        const float cap = std::min(game, (SOLDIER_HEIGHTS[entry] * 20.f) + spread);
        neutral = neutral && sprite_once(through_link, size, 0.75f, true, game) && sprite_once(through_link, size, 0.75f, false, game);
        shared->scales[SCALE_SPRITE_DISTANCE].store(20.f);
        shared->scales_active.fetch_or(1u << SCALE_SPRITE_DISTANCE);
        capped = capped && cap < game && sprite_once(through_link, size, 0.75f, true, cap)
                 && sprite_once(through_link, size, 0.75f, false, cap);
        shared->scales[SCALE_DEAD_UNITS].store(0.5f);
        shared->scales_active.fetch_or(1u << SCALE_DEAD_UNITS);
        dead_scaled = dead_scaled && sprite_once(through_link, size, 0.75f, false, cap * 0.5f)
                      && sprite_once(through_link, size, 0.75f, true, cap);
        shared->scales_active.fetch_and(~(1u << SCALE_SPRITE_DISTANCE));
        dead_scaled = dead_scaled && sprite_once(through_link, size, 0.75f, false, game * 0.5f)
                      && sprite_once(through_link, size, 0.75f, true, game);
        shared->scales_active.store(0);
        shared->scales[SCALE_SPRITE_DISTANCE].store(NEUTRAL_SCALES[SCALE_SPRITE_DISTANCE]);
        shared->scales[SCALE_DEAD_UNITS].store(NEUTRAL_SCALES[SCALE_DEAD_UNITS]);
      }
    }
    Expect(neutral, "sprite run: neutral, the game's code makes the game's distance for sizes 0-3 and a size past 3, "
                    "from the soldier's record and from its linked one");
    Expect(capped && Logged("SPRITE_DISTANCE: first call"),
           "sprite run: the cap by the size's height, the size found from the game's own rcx; the first-call line");
    Expect(dead_scaled, "sprite run: a dead soldier's distance x its scale, with the cap and alone; a living one's not");
  }

  // The engine rows at the game's start, each case on another copy of the DLL (its own module and globals, in its own
  // folder beside this exe) with the game file mapped again.
  wchar_t exe_dir_wide[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_dir_wide, MAX_PATH);
  *std::wcsrchr(exe_dir_wide, L'\\') = L'\0';
  const auto copy_on_the_game_again = [&](const wchar_t* folder) {
    Expect(MapGameElf(argv[2], &image) == 0, "engine rows: the game file maps again");
    WriteTweakObjects();
    WriteLoaderGuards();
    const std::wstring copy_dir = std::wstring(exe_dir_wide) + L"\\" + folder;
    CreateDirectoryW(copy_dir.c_str(), nullptr);
    const std::wstring copy = copy_dir + L"\\" + MODULE_NAME;
    Expect(CopyFileW(argv[1], copy.c_str(), FALSE) != FALSE, "engine rows: a copy of the plugin DLL");
    return LoadPlugin(copy.c_str());
  };
  // The battle step row reads the game's code again at the game's start: one byte of that code changed between the
  // copy's init and its game start. The contact split row the same, here with its install key 1 (a ReShade.ini beside
  // this exe for the one init): the row's bit is set and its lane waits for the first tick; its changed byte is the
  // per-object function's first.
  {
    const Plugin second = copy_on_the_game_again(L"game_start_case");
    const std::wstring ini_path = std::wstring(exe_dir_wide) + L"\\ReShade.ini";
    WritePrivateProfileStringW(INI_SECTION, ROW_INI_KEYS[ROW_CONTACT_SPLIT], L"1", ini_path.c_str());
    Expect(second.init != nullptr && second.init(&api) == 0
               && second.block->engine_status.load()
                      == ((uint32_t{ENGINE_NOT_USED_YET} << ENGINE_STEP_SHIFT)
                          | (uint32_t{ENGINE_NOT_USED_YET} << ENGINE_SPLIT_SHIFT)),
           "battle step, contact split: a second copy's init on the game file mapped again");
    DeleteFileW(ini_path.c_str());
    Expect(second.block != nullptr && second.block->core_wanted.load() == (CORE_WANTED_DEFAULT | CORE_SPLIT)
               && !Logged("contact split: "),
           "contact split: its install key 1 sets the row's bit, and only that");
    if (second.init != nullptr) {
      {
        const std::scoped_lock lock(log_mutex);
        log_lines.clear();
      }
      *at(battle_step::CODE_RANGES.back().address) ^= 1;
      *at(battle_step::CONTACT_OBJECT_STEP) ^= 1;
      second.game_started(&api);
      Expect(((second.block->engine_status.load() >> ENGINE_STEP_SHIFT) & ENGINE_LANE_MASK) == ENGINE_CODE_DIFFERS
                 && Logged("renodx_attila64: battle step: game code differs (") && second.block->rows_changed.load() == 0,
             "battle step: code changed after init is found at the game's start, with the row's line");
      Expect(second.block->engine_status.load()
                     == ((uint32_t{ENGINE_CODE_DIFFERS} << ENGINE_STEP_SHIFT)
                         | (uint32_t{ENGINE_CODE_DIFFERS} << ENGINE_SPLIT_SHIFT))
                 && Logged("renodx_attila64: contact split: off (game code differs at 0x803e70+0x0)"),
             "contact split: code changed after init is found at the game's start, with the row's line and the place");
    }
  }
  // A byte of each engine row's own jump changed between init and the game's start: the site's line, the row off
  // from there on, and no code line, since the game's code is still the code that was read.
  {
    const Plugin third = copy_on_the_game_again(L"site_changed_case");
    Expect(third.init != nullptr && third.init(&api) == 0, "engine rows: a third copy's init");
    if (third.init != nullptr) {
      {
        const std::scoped_lock lock(log_mutex);
        log_lines.clear();
      }
      *at(SITES[LABEL_PAIRS_ENTRY].address + 1) ^= 1;
      *at(SITES[LABEL_CONTACT_ENTRY].address + 1) ^= 1;
      third.game_started(&api);
      Expect(third.block->rows_changed.load() == (RowBit(ROW_BATTLE_STEP) | RowBit(ROW_CONTACT_SPLIT))
                 && third.block->engine_status.load()
                        == ((uint32_t{ENGINE_CODE_DIFFERS} << ENGINE_STEP_SHIFT)
                            | (uint32_t{ENGINE_CODE_DIFFERS} << ENGINE_SPLIT_SHIFT))
                 && Logged("renodx_attila64: site PAIRS_ENTRY changed after we patched it")
                 && Logged("renodx_attila64: site CONTACT_ENTRY changed after we patched it")
                 && !Logged("battle step: ") && !Logged("contact split: "),
             "engine rows: a site changed after init turns its row off at the game's start, without a code line");
    }
  }

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
