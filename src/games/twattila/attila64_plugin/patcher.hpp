/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "loader_api.h"
#include "plugin_block.h"
#include "sites.h"

// The plugin's checks on the game in memory and its writes there: the game build, each site's bytes and neighbours,
// the foreign table, the hook through the loader, and the re-check of what was written. Everything is read at the
// game's fixed addresses; nothing here writes except through the loader.
namespace twattila::attila64_plugin {

enum Refusal : uint8_t {
  REFUSAL_NONE,
  REFUSAL_DIFFERS,       // the site's own bytes are not the game's
  REFUSAL_NEIGHBOUR,     // someone patched within SITE_MARGIN of it
  REFUSAL_FOREIGN,       // the foreign table names code there
  REFUSAL_NO_MEMORY,     // the loader had no code memory near the game left
  REFUSAL_WRITE_FAILED,  // the loader refused, and the bytes changed under us
  REFUSAL_OFF,           // its row's install key is 0
  REFUSAL_COUNT
};
inline constexpr std::array<const char*, REFUSAL_COUNT> REFUSAL_WORDS = {
    "", "DIFFERS", "NEIGHBOUR", "FOREIGN", "NO_MEMORY", "WRITE_FAILED", "OFF"};

struct SiteCheck {
  Refusal refusal = REFUSAL_NONE;
  uint8_t owner = OWNER_COUNT;  // the ForeignOwner of a FOREIGN refusal
};

// The loader's entry hook copies the first bytes into 32 bytes near the game, the jump back right after them and its
// way in at +16: lengths 12..15 would overlap that (a sites.h row of that length needs the plugin's own trampoline).
static_assert([] {
  for (const Site& site : SITES) {
    if (site.length < 5 || site.length > 11) return false;
  }
  return true;
}(), "every site is 5..11 bytes, what the loader's entry hook can take");

// What the loader's hook left at a site: what to call for the original, and the site's bytes right after (its jump
// plus any stale tail byte), for the re-check at game start.
struct AppliedSite {
  void* original = nullptr;
  std::array<uint8_t, SITE_MAX_LENGTH> bytes = {};
};

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
  return REFUSAL_NONE;
}

[[nodiscard]] inline bool SiteUnchanged(const Site& site, const AppliedSite& applied) {
  return std::memcmp(reinterpret_cast<const void*>(site.address), applied.bytes.data(), site.length) == 0;
}

}  // namespace twattila::attila64_plugin
