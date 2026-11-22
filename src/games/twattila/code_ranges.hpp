/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <format>

#include "../../utils/log.hpp"

// The add-on's own code, for the fault, unhandled-exception and stall lines of device_loss.hpp: its image (set by Install) and
// up to 4 pages outside it (AddCodeRange: stubs and thunks that engine_code, effect_update_sites and draw_recording_sites make
// at run time). Here and not in device_loss.hpp so that the sites that make the pages need no ReShade headers. The namespace is
// device_loss's: its lines and the tests name AddCodeRange there. A range is published by its `end`, stored last: a handler on
// any thread reads `end` first and skips a range without one.
namespace twattila::device_loss {

inline uintptr_t own_image_begin = 0;
inline uintptr_t own_image_end = 0;
inline std::array<char, MAX_PATH> own_image_name = {};
struct CodeRange {
  std::atomic<const char*> name = nullptr;  // claims the place
  std::atomic<uintptr_t> begin = 0;
  std::atomic<uintptr_t> end = 0;
};
inline std::array<CodeRange, 4> code_ranges;

// A page of the add-on's code outside its image (engine-code stubs, thunks), named like the image in the fault,
// unhandled-exception and stall lines. For a page that stays for the image's life: added once it is executable and
// before anything jumps there. `name` is a literal. False: the 4 places are taken.
inline bool AddCodeRange(const void* begin, const void* end, const char* name) {
  for (CodeRange& range : code_ranges) {
    const char* free_place = nullptr;
    if (!range.name.compare_exchange_strong(free_place, name)) continue;
    range.begin.store(reinterpret_cast<uintptr_t>(begin), std::memory_order_relaxed);
    range.end.store(reinterpret_cast<uintptr_t>(end), std::memory_order_release);
    return true;
  }
  return false;
}

// The name of the add-on's code that holds `address` (its image's file or a range's name), its start in `start`; null:
// not the add-on's. No allocation, no lock: exception handlers call it.
inline const char* OwnCode(uintptr_t address, uintptr_t* start) {
  if (address - own_image_begin < own_image_end - own_image_begin) {
    *start = own_image_begin;
    return own_image_name.data();
  }
  for (const CodeRange& range : code_ranges) {
    const uintptr_t end = range.end.load(std::memory_order_acquire);
    if (end == 0) continue;
    const uintptr_t begin = range.begin.load(std::memory_order_relaxed);
    if (address - begin < end - begin) {
      *start = begin;
      return range.name.load(std::memory_order_relaxed);
    }
  }
  return nullptr;
}

// AddCodeRange for a render_mt part's page, and one line for the bench's crash pin (the gates' part_range):
// "[Total War] render_mt range: {part} stub {name}+0x0..+0x{size} (0x{begin}..0x{end})". `name` is a literal without a space or
// '+'; a fault inside the page prints "{name}+0x{offset}". The 4 places taken: a warning says the page is not named.
inline void AddPartRange(const char* part, const char* name, const void* begin, size_t size) {
  const auto end = static_cast<const uint8_t*>(begin) + size;
  if (AddCodeRange(begin, end, name)) {
    renodx::utils::log::i(std::format("[Total War] render_mt range: {} stub {}+0x0..+0x{:X} (0x{:08X}..0x{:08X})", part, name, size,
                                      reinterpret_cast<uintptr_t>(begin), reinterpret_cast<uintptr_t>(end)));
  } else {
    renodx::utils::log::w(std::format("[Total War] render_mt range: {} page {} is not named in fault lines (all 4 places are taken).", part, name));
  }
}

}  // namespace twattila::device_loss
