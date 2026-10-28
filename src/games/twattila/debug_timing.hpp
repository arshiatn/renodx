/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

#include "../../utils/log.hpp"

// Debug (temporary): the CPU time and calls of the anti-aliasing's hooks a frame, logged every 1000 presents (the
// render thread's; counts across threads approximate).
namespace twattila::debug_timing {

enum Hook : size_t { MAP, UNMAP, CAMERAS, UPDATE, VIEWPORTS, SCISSORS, LOD_SAMPLERS, LOD_VIEWPORTS, ANTI_ALIASING, HOOKS };
constexpr std::array<const char*, HOOKS> NAMES = {"map",       "unmap",        "camera checks", "update",       "viewports",
                                                  "scissors",  "lod samplers", "lod viewports", "anti-aliasing"};

struct Entry {
  std::chrono::steady_clock::duration spent = {};
  uint64_t calls = 0;
};
static std::array<Entry, HOOKS> entries;
static uint64_t presents = 0;

struct Timed {
  Hook hook;
  std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
  ~Timed() {
    entries[hook].spent += std::chrono::steady_clock::now() - started;
    ++entries[hook].calls;
  }
};

inline void OnPresent() {
  if (++presents < 1000) return;
  std::string line = "[Total War] Hooks (debug, a frame):";
  for (size_t hook = 0; hook < HOOKS; ++hook) {
    line += std::format(" {} {:.0f} us / {:.0f} calls;", NAMES[hook],
                        std::chrono::duration<double, std::micro>(entries[hook].spent).count() / static_cast<double>(presents),
                        static_cast<double>(entries[hook].calls) / static_cast<double>(presents));
  }
  renodx::utils::log::i(line);
  entries = {};
  presents = 0;
}

}  // namespace twattila::debug_timing
