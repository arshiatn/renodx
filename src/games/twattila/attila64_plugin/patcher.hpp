/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "loader_api.h"
#include "plugin_block.h"
#include "sites.h"

// The plugin's checks on the game in memory and its writes there: the game build, each site's bytes and neighbours,
// the foreign table, the hook or the patch through the loader, a patch's way back, and the re-check of what was
// written. Everything is read at the game's fixed addresses; nothing here writes except through the loader.
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
  REFUSAL_COUNT
};
inline constexpr std::array<const char*, REFUSAL_COUNT> REFUSAL_WORDS = {
    "", "DIFFERS", "NEIGHBOUR", "FOREIGN", "NO_MEMORY", "WRITE_FAILED", "OFF", "ROW"};

struct SiteCheck {
  Refusal refusal = REFUSAL_NONE;
  uint8_t owner = OWNER_COUNT;  // the ForeignOwner of a FOREIGN refusal
};

// The loader's entry hook copies the first bytes into 32 bytes near the game, the jump back right after them and its
// way in at +16: lengths 12..15 would overlap that (a sites.h row of that length needs the plugin's own trampoline).
// A patch is one instruction's immediate.
static_assert([] {
  for (const Site& site : SITES) {
    const uint8_t shortest = ((site.kind == SITE_PATCH) ? 1 : 5);
    const uint8_t longest = ((site.kind == SITE_PATCH) ? 8 : 11);
    if (site.length < shortest || site.length > longest) return false;
  }
  return true;
}(), "a hook site is 5..11 bytes, what the loader's entry hook can take; a patch site 1..8");

// What is at a site once it is in: for a hook what to call for the original, and the site's bytes right after (the
// loader's jump plus any stale tail byte; a patch's new bytes, or the game's when nothing was to write), for the
// re-check at game start.
struct AppliedSite {
  void* original = nullptr;
  std::array<uint8_t, SITE_MAX_LENGTH> bytes = {};
  bool applied = false;
};

// The shadow row's size sites hold the map's width and height as two floats; its cascade sites hold one number each.
[[nodiscard]] constexpr bool IsShadowSizeSite(const Site& site) {
  return site.row == ROW_SHADOW_MAP && site.kind == SITE_PATCH && site.length == 2 * sizeof(float);
}

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

// An applied patch site out again (a later site of its row failed): the game's bytes back where ours were written.
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
