/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>

#include "loader_api.h"
#include "plugin_block.h"
#include "sites.h"
#include "trampoline.hpp"

// The plugin's checks on the game in memory and its writes there: the game build, each site's bytes and neighbours,
// the foreign table, the hook, the patch or the mid-function stub through the loader, a patch's way back, and the
// re-check of what was written. Everything is read at the game's fixed addresses; game code is written only through the
// loader, and a mid-function stub or a hand-made one goes into code memory the loader hands out.
namespace twattila::attila64_plugin {

enum Refusal : uint8_t {
  REFUSAL_NONE,
  REFUSAL_DIFFERS,       // the site's own bytes are not the game's
  REFUSAL_NEIGHBOUR,     // someone patched within SITE_MARGIN of it
  REFUSAL_FOREIGN,       // the foreign table names code there
  REFUSAL_NO_MEMORY,     // the loader had no code memory near the game left
  REFUSAL_WRITE_FAILED,  // the loader refused: a hook whose bytes changed under us, or a patch
  REFUSAL_OFF,           // its row's install key is 0
  REFUSAL_ROW,           // another site of its row was refused: a row goes in whole or not at all
  REFUSAL_HELD,          // game code its row's stubs stand on (sites.h HELD_RANGES) is not the game's
  REFUSAL_COUNT
};
inline constexpr std::array<const char*, REFUSAL_COUNT> REFUSAL_WORDS = {
    "", "DIFFERS", "NEIGHBOUR", "FOREIGN", "NO_MEMORY", "WRITE_FAILED", "OFF", "ROW", "HELD"};

struct SiteCheck {
  Refusal refusal = REFUSAL_NONE;
  uint8_t owner = OWNER_COUNT;  // the ForeignOwner of a FOREIGN refusal
};

// The loader's entry hook copies the first bytes into 32 bytes near the game, the jump back right after them and its
// way in at +16: lengths 12..15 would overlap that (a sites.h row of that length needs the plugin's own trampoline).
// A patch is one instruction's immediate. A mid-function site holds the plugin's own jump, a hand-made stub's site too.
// SITES is indexed by label (main.cpp's applied sites too).
static_assert([] {
  for (size_t index = 0; index < SITES.size(); ++index) {
    if (SITES[index].label != index) return false;
  }
  return true;
}());
inline constexpr uint8_t PATCH_SITE_LONGEST = 8;
inline constexpr uint8_t RAW_STUB_SITE_LONGEST = 8;
static_assert([] {
  for (const Site& site : SITES) {
    const uint8_t shortest = ((site.kind == SITE_PATCH) ? 1 : trampoline::JUMP_BYTES);
    uint8_t longest = 11;
    switch (site.kind) {
      case SITE_PATCH:    longest = PATCH_SITE_LONGEST; break;
      case SITE_MID:      longest = SITE_MAX_LENGTH; break;
      case SITE_RAW_STUB: longest = RAW_STUB_SITE_LONGEST; break;
      default:            break;
    }
    if (site.length < shortest || site.length > longest) return false;
  }
  return true;
}(), "a hook site is 5..11 bytes, what the loader's entry hook can take; a patch site 1..8; a mid-function site 5..15; "
     "a hand-made stub's site 5..8");
// A held range is its own site's: the site inside it, on the same row.
static_assert(std::ranges::all_of(HELD_RANGES, [](const HeldRange& range) {
  const Site& site = SITES[range.label];
  return site.label == range.label && site.row == range.row && site.kind == SITE_RAW_STUB && range.lo <= site.address
         && site.address + site.length <= range.hi;
}), "each held range names a hand-made stub's site of its row, inside the range");

// What is at a site once it is in: for a hook what to call for the original (a mid-function site: its trampoline; a
// hand-made stub's site: the stub), and the site's bytes right after (the loader's jump plus any stale tail byte; a
// patch's new bytes, or the game's when nothing was to write; a mid-function or hand-made stub's site: its jump and
// int3), for the re-check at game start.
struct AppliedSite {
  void* original = nullptr;
  std::array<uint8_t, SITE_MAX_LENGTH> bytes = {};
  bool applied = false;
};

// The shadow row's size sites hold the map's width and height as two floats; its cascade sites hold one number each.
[[nodiscard]] constexpr bool IsShadowSizeSite(const Site& site) {
  return site.row == ROW_SHADOW_MAP && site.kind == SITE_PATCH && site.length == 2 * sizeof(float);
}
// Medium's own two cascade writes, right after its size (mov dword [r13 + 0xcc], 2; mov dword [r13 + 0xb8], 0): the
// numbers Two Shadow Cascades Under Rays writes at High and Max, and the shadow row's check that the game still writes
// them itself. src: the 32-bit add-on's MEDIUM_CASCADES (graphics_hooks.hpp shadow_map); the tests pin the bytes on
// the game file.
inline constexpr uintptr_t MEDIUM_CASCADES_ADDRESS = 0x30e4751;
inline constexpr std::array<uint8_t, 22> MEDIUM_CASCADES = {
    0x41, 0xc7, 0x85, 0xcc, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x41, 0xc7, 0x85, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
inline constexpr size_t MEDIUM_COUNT_OFFSET = 7;   // the first write's number, what a COUNT site holds
inline constexpr size_t MEDIUM_EXTRA_OFFSET = 18;  // the second write's, what an EXTRA site holds

// READY when the loader and the game build are the ones the sites were made for.
[[nodiscard]] inline Status CheckIdentity(const LoaderApi& api) {
  if (api.api_version < LOADER_API_VERSION || api.game_lo != GAME_LO || api.game_hi < GAME_HI_MIN) {
    return STATUS_WRONG_LOADER;
  }
  if (std::memcmp(reinterpret_cast<const void*>(GAME_LO), ELF_MAGIC.data(), ELF_MAGIC.size()) != 0
      || std::memcmp(reinterpret_cast<const void*>(BUILD_ID_NOTE_ADDRESS), BUILD_ID_NOTE.data(), BUILD_ID_NOTE.size()) != 0) {
    return STATUS_WRONG_GAME;
  }
  return STATUS_READY;
}

[[nodiscard]] inline SiteCheck CheckSite(const Site& site, std::span<const Foreign> foreign) {
  const uintptr_t window_start = site.address - SITE_MARGIN;
  const uintptr_t window_end = site.address + site.length + SITE_MARGIN;
  if (std::memcmp(reinterpret_cast<const void*>(site.address), site.window.data() + SITE_MARGIN, site.length) != 0) {
    return {.refusal = REFUSAL_DIFFERS};
  }
  if (std::memcmp(reinterpret_cast<const void*>(window_start), site.window.data(), window_end - window_start) != 0) {
    return {.refusal = REFUSAL_NEIGHBOUR};
  }
  for (const Foreign& entry : foreign) {
    const bool hit = ((entry.kind == FOREIGN_RANGE)
                          ? (entry.length != 0 && entry.address < window_end && entry.address + entry.length > window_start)
                          : (entry.address > site.address && entry.address < site.address + site.length));
    if (hit) return {.refusal = REFUSAL_FOREIGN, .owner = entry.owner};
  }
  return {};
}

// A held range's memory against its hash (FNV-1a 64, Fowler / Noll / Vo's public-domain hash): the whole range while
// its site holds the game's bytes, the range without the site's bytes once our jump is written there (the site's own
// bytes are CheckSite's before the write and SiteUnchanged's after it).
[[nodiscard]] inline bool HeldRangeUnchanged(const HeldRange& range, bool site_written) {
  const Site& site = SITES[range.label];
  uint64_t hash = 0xcbf29ce484222325;
  for (uint32_t address = range.lo; address < range.hi; ++address) {
    if (site_written && address >= site.address && address < site.address + site.length) continue;
    hash = (hash ^ *reinterpret_cast<const uint8_t*>(uintptr_t{address})) * 0x100000001b3;
  }
  return hash == (site_written ? range.span_hash : range.whole_hash);
}

// The loader's hook on a site CheckSite accepted; on success *applied holds the original and the bytes written.
[[nodiscard]] inline Refusal ApplySite(const LoaderApi& api, const Site& site, void* replacement, AppliedSite* applied) {
  const uint8_t* expect = site.window.data() + SITE_MARGIN;
  applied->original = api.hook(site.address, expect, site.length, replacement);
  if (applied->original == nullptr) {
    const bool unchanged = std::memcmp(reinterpret_cast<const void*>(site.address), expect, site.length) == 0;
    return (unchanged ? REFUSAL_NO_MEMORY : REFUSAL_WRITE_FAILED);
  }
  std::memcpy(applied->bytes.data(), reinterpret_cast<const void*>(site.address), site.length);
  applied->applied = true;
  return REFUSAL_NONE;
}

// `bytes` into code memory of the loader's at `code`. Memory the loader maps without write access is opened for the
// copy and closed again (the loader's own writes look after themselves); execute access stays on, since the loader's
// own stubs share the page. False when it could not be opened, or when its first and last byte have different access
// (one access could not be put back for both): nothing is written. *close_error: the Win32 error when it could not be
// closed again (the bytes are in and the memory stays writable), else 0.
[[nodiscard]] inline bool WriteNearCode(uint8_t* code, std::span<const uint8_t> bytes, DWORD* close_error) {
  *close_error = 0;
  MEMORY_BASIC_INFORMATION page = {};
  MEMORY_BASIC_INFORMATION last_page = {};
  if (VirtualQuery(code, &page, sizeof(page)) == 0
      || VirtualQuery(code + bytes.size() - 1, &last_page, sizeof(last_page)) == 0 || page.Protect != last_page.Protect) {
    return false;
  }
  // Writable as it is when the last byte is in the same writable region as the first (it may cross a page).
  const bool writable = (page.Protect & (PAGE_EXECUTE_READWRITE | PAGE_READWRITE)) != 0
                        && reinterpret_cast<uintptr_t>(page.BaseAddress) + page.RegionSize
                               >= reinterpret_cast<uintptr_t>(code) + bytes.size();
  DWORD protection = 0;
  if (!writable && VirtualProtect(code, bytes.size(), PAGE_EXECUTE_READWRITE, &protection) == FALSE) return false;
  std::memcpy(code, bytes.data(), bytes.size());
  if (!writable && VirtualProtect(code, bytes.size(), protection, &protection) == FALSE) {
    *close_error = GetLastError();
  }
  FlushInstructionCache(GetCurrentProcess(), code, bytes.size());
  return true;
}

// A site CheckSite accepted becomes, through the loader, a jump to `target` with int3 after it. `target` is code near
// the game that is whole before the site points at it: a mid-function site's stub or a hand-made one (game_code.hpp).
// On success *applied holds `target` and the bytes written.
[[nodiscard]] inline Refusal ApplyJump(const LoaderApi& api, const Site& site, uintptr_t target, AppliedSite* applied) {
  std::array<uint8_t, SITE_MAX_LENGTH> jump = {};
  jump.fill(0xcc);  // int3
  jump[0] = 0xe9;   // jmp rel32
  const auto displacement = static_cast<uint32_t>(target - (site.address + trampoline::JUMP_BYTES));
  std::memcpy(jump.data() + 1, &displacement, sizeof(displacement));
  if (api.patch(site.address, site.window.data() + SITE_MARGIN, jump.data(), site.length) == 0) {
    return REFUSAL_WRITE_FAILED;
  }
  applied->original = reinterpret_cast<void*>(target);
  std::memcpy(applied->bytes.data(), reinterpret_cast<const void*>(uintptr_t{site.address}), site.length);
  applied->applied = true;
  return REFUSAL_NONE;
}

// A mid-function site CheckSite accepted: its stub (trampoline.hpp) into code memory of the loader's, then, through the
// loader, the site's jump to it (ApplyJump). On success *applied holds the trampoline (the site's instructions and the
// jump back) and the bytes written. `handler` runs only while a bit of `gate_mask` is set in *gate. A refused site
// keeps the game's bytes; the code memory taken for it is not given back (the loader has no call for that).
[[nodiscard]] inline Refusal ApplyMidSite(const LoaderApi& api, const Site& site, trampoline::MidHandler handler,
                                          const std::atomic<uint32_t>* gate, uint32_t gate_mask, AppliedSite* applied) {
  auto* code = static_cast<uint8_t*>(api.code_near_game(trampoline::MID_STUB_FIXED_BYTES + site.length));
  const auto code_address = reinterpret_cast<uintptr_t>(code);
  // The site's jump to the stub and the trampoline's jump back.
  if (code == nullptr || !trampoline::InJumpReach(site.address, code_address)
      || !trampoline::InJumpReach(code_address + trampoline::MID_GATE_BYTES + site.length,
                                  uintptr_t{site.address} + site.length)) {
    return REFUSAL_NO_MEMORY;
  }
  const uint8_t* expect = site.window.data() + SITE_MARGIN;
  std::array<uint8_t, trampoline::MID_STUB_CAPACITY> stub = {};
  const size_t stub_bytes =
      trampoline::BuildMidStub(code_address, site.address, {expect, site.length}, handler, gate, gate_mask, &stub);
  DWORD close_error = 0;
  if (!WriteNearCode(code, {stub.data(), stub_bytes}, &close_error)) return REFUSAL_WRITE_FAILED;
  if (close_error != 0) {
    api.log("renodx_attila64: site %s %#x: its code near the game stays writable (error %lu)", LABEL_WORDS[site.label],
            site.address, close_error);
  }
  const Refusal refusal = ApplyJump(api, site, code_address, applied);
  if (refusal == REFUSAL_NONE) {
    applied->original = code + trampoline::MID_GATE_BYTES;
  }
  return refusal;
}

// A patch site CheckSite accepted: `bytes` (the site's length) written over the game's through the loader; with
// nullptr nothing is written and the site is in with the game's bytes.
[[nodiscard]] inline Refusal ApplyPatch(const LoaderApi& api, const Site& site, const uint8_t* bytes, AppliedSite* applied) {
  if (bytes != nullptr && api.patch(site.address, site.window.data() + SITE_MARGIN, bytes, site.length) == 0) {
    return REFUSAL_WRITE_FAILED;
  }
  std::memcpy(applied->bytes.data(), reinterpret_cast<const void*>(site.address), site.length);
  applied->applied = true;
  return REFUSAL_NONE;
}

// An applied patch site or hand-made stub's site out again (a later site of its row failed): the game's bytes back
// where ours were written.
// False when the loader could not write them back; the site counts as out either way.
[[nodiscard]] inline bool RevertPatch(const LoaderApi& api, const Site& site, AppliedSite* applied) {
  const uint8_t* expect = site.window.data() + SITE_MARGIN;
  applied->applied = false;
  if (std::memcmp(applied->bytes.data(), expect, site.length) == 0) return true;
  return api.patch(site.address, applied->bytes.data(), expect, site.length) != 0;
}

[[nodiscard]] inline bool SiteUnchanged(const Site& site, const AppliedSite& applied) {
  return std::memcmp(reinterpret_cast<const void*>(site.address), applied.bytes.data(), site.length) == 0;
}

// True when every entry holds the loader's own entry hook: a jmp rel32 to its way in, `mov rax, imm64; jmp rax`, whose
// target lies in [image_lo, image_hi), the loader's image. The jump is followed to the way in, so both of the loader's
// layouts hold (the way in 16 bytes into its block, or at its start). A plugin's hook through the loader has the same
// shape and targets its own DLL. No entries: false. The jump's target is read without a range check: a jump written by
// a working tool lands in mapped code, and a broken one would crash the game at that function's first call anyway.
// The game's parallel effect update corrupts its heap without these guards (the loader's own notes).
[[nodiscard]] inline bool LoaderHooked(std::span<const uintptr_t> entries, uintptr_t image_lo, uintptr_t image_hi) {
  return !entries.empty() && std::ranges::all_of(entries, [&](uintptr_t entry) {
    const auto* jump = reinterpret_cast<const uint8_t*>(entry);
    if (jump[0] != 0xE9) return false;
    int32_t displacement = 0;
    std::memcpy(&displacement, jump + 1, sizeof(displacement));
    const auto* way_in = reinterpret_cast<const uint8_t*>(entry + 5 + static_cast<intptr_t>(displacement));
    uint64_t target = 0;
    std::memcpy(&target, way_in + 2, sizeof(target));
    return way_in[0] == 0x48 && way_in[1] == 0xB8 && way_in[10] == 0xFF && way_in[11] == 0xE0 && target >= image_lo
           && target < image_hi;
  });
}

}  // namespace twattila::attila64_plugin
