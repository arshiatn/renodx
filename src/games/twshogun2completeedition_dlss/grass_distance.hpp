/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/data.hpp"
#include "../../utils/detour.hpp"
#include "../../utils/log.hpp"
#include "../twattila/native_hook_lifetime.hpp"
#include "./engine.hpp"

// Grass distance (graphics option): the game's grass ("Scrub Node", set up at +0x1b06e00) reaches its
// grass_lod_distance, an integer at node +0xd0 (450 m). Its update (+0x1b15f20) draws the grass cells within that
// distance of the camera (box and squared distance), and the grass shaders get inv_lod_distance = 1 / it (+0x1b48f51):
// rigidgrass shrinks and thins the blades from the camera toward it (vertex height, dissolve). The update's hook
// scales it: grass farther away, and fuller with smoother changes nearer.
namespace twshogun2::grass_distance {

namespace internal {

// void Update(ScrubNode* node): mov rax, rsp; push rbp; push rsi; ...; cmp byte [rcx + 0x28], 0; mov rsi, rcx; je;
// mov rcx, [rcx + 0xd8]; test rcx, rcx; je. +0x1b15f20 (2026-10-13 build).
constexpr std::string_view UPDATE =
    "48 8B C4 55 56 48 8D 68 A8 48 81 EC 48 01 00 00 80 79 28 00 48 8B F1 0F 84 ?? ?? ?? ?? 48 8B 89 D8 00 00 00 "
    "48 85 C9 0F 84";
constexpr size_t LOD_DISTANCE = 0xd0;

// The other argument registers are passed through untouched.
using Update = uint64_t (*)(uint8_t* node, void* second, void* third, void* fourth);

struct NodeDistance {
  int32_t game = 0;     // the game's grass_lod_distance
  int32_t written = 0;  // the scaled one the addon wrote into the node
};

static Update original_update = nullptr;
static std::vector<renodx::utils::detour::Function> hooks;
static tw::native_hooks::Lifetime hook_lifetime;
static std::string status = "not searched";  // Install / overlay (render thread)
static std::atomic<float> scale = 1.f;
static renodx::utils::data::ParallelFlatHashMap<uintptr_t, NodeDistance, std::shared_mutex> nodes;
static std::atomic_int32_t last_game = 0;
static std::atomic_int32_t last_written = 0;

inline uint64_t OnUpdate(uint8_t* node, void* second, void* third, void* fourth) {
  auto* distance = reinterpret_cast<int32_t*>(node + LOD_DISTANCE);
  const auto key = reinterpret_cast<uintptr_t>(node);
  NodeDistance known = {.game = *distance, .written = *distance};
  nodes.if_contains(key, [&](const auto& entry) { known = entry.second; });
  // Anything but the addon's last value is the game's (set again, or a new node at the same address).
  const int32_t game = (*distance == known.written ? known.game : *distance);
  const auto scaled = static_cast<int32_t>(std::lround(static_cast<float>(game) * scale.load(std::memory_order_relaxed)));
  *distance = scaled;
  nodes.insert_or_assign(key, NodeDistance{.game = game, .written = scaled});
  last_game.store(game, std::memory_order_relaxed);
  last_written.store(scaled, std::memory_order_relaxed);
  return original_update(node, second, third, fourth);
}

}  // namespace internal

// Device creation (the engine module is loaded by then).
inline void Install() {
  if (internal::original_update != nullptr || !internal::hooks.empty()) return;
  uint8_t* update = engine::FindPattern(internal::UPDATE);
  if (update == nullptr) {
    internal::status = "the game's grass code was not found";
  } else {
    internal::original_update = reinterpret_cast<internal::Update>(update);
    internal::hooks = {renodx::utils::detour::Function(&internal::original_update, &internal::OnUpdate)};
    try {
      internal::hook_lifetime.Install(internal::hooks);
      internal::status = std::format("hooked (update +{:#x})", update - reinterpret_cast<uint8_t*>(engine::Module()));
    } catch (const std::exception& error) {
      internal::hooks.clear();
      internal::status = std::format("hook failed: {}", error.what());
    }
  }
  renodx::utils::log::i("[Shogun 2 DLSS] Grass distance: ", internal::status);
}

inline void Uninstall() {
  if (internal::hooks.empty()) return;
  try {
    internal::hook_lifetime.Uninstall(&internal::hooks);
  } catch (const std::exception& error) {
    renodx::utils::log::e("[Shogun 2 DLSS] Grass distance hooks could not be removed; addon retained until process exit: ", error.what());
  }
}

// Overlay: the game's grass distance x scale (1: the game's).
inline void SetScale(float scale) {
  internal::scale.store(scale, std::memory_order_relaxed);
}

// Overlay (Advanced): the hook's state and the last distance, the game's -> the one in use.
inline std::string Status() {
  if (const int32_t game = internal::last_game.load(std::memory_order_relaxed); game != 0) {
    return std::format("{}; {} m -> {} m", internal::status, game, internal::last_written.load(std::memory_order_relaxed));
  }
  return internal::status + "; no grass seen yet";
}

}  // namespace twshogun2::grass_distance
