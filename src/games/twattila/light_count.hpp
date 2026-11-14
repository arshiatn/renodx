/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// Log Details' light count, once (a frame trace gives a light draw's indices times its instances, not its lights): one whole
// frame's draws, instances and indices (or vertices) of the point lights (a light an instance, POSITION1-4) and of the
// particle lights (their layout unknown). Logged the first frame either draws, again the first frame point lights draw; with
// Log Details off a draw reads one flag. Pharaoh's (E318CE77, 5AD46115) and Troy's (143A02ED, 643FC156).

#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <mutex>
#include <shared_mutex>

#include "../../utils/command_action.hpp"
#include "../../utils/log.hpp"

namespace twattila::light_count {

namespace internal {

struct LightCount {
  std::shared_mutex mutex;
  std::array<uint32_t, 2> draws = {};  // point lights, particle lights
  std::array<uint64_t, 2> instances = {};
  std::array<uint64_t, 2> indices = {};
  bool logged = false;
  bool points_logged = false;
};
inline LightCount light_count;
inline std::atomic<bool> light_counting = false;  // from a present on: the whole frame to the next
inline std::array<uint32_t, 2> shader_hashes = {};  // the point lights' pixel shader, the particle lights'
constexpr auto OnLightCountDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (!light_counting.load(std::memory_order_relaxed)) return {};
  if constexpr (requires { command.arguments.instance_count; }) {
    const size_t family = (command.matched_shader_hash == shader_hashes[0]) ? 0 : 1;
    const std::unique_lock lock(light_count.mutex);
    ++light_count.draws[family];
    light_count.instances[family] += command.arguments.instance_count;
    if constexpr (requires { command.arguments.index_count; }) {
      light_count.indices[family] += command.arguments.index_count;
    } else {
      light_count.indices[family] += command.arguments.vertex_count;
    }
  }
  return {};
};

}  // namespace internal

inline void Install(uint32_t point_lights, uint32_t particle_lights) {
  internal::shader_hashes = {point_lights, particle_lights};
  for (const uint32_t shader_hash : internal::shader_hashes) {
    renodx::utils::command_action::Register(
        internal::OnLightCountDraw, {.shader_hash = shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
  }
}

// At each present: the frame's count logged (the first time), counting on for the next frame while `log_details`.
inline void OnPresent(bool log_details) {
  auto& light_count = internal::light_count;
  const std::unique_lock lock(light_count.mutex);
  const bool points = light_count.draws[0] != 0 && !light_count.points_logged;
  if (internal::light_counting.load() && (points || (light_count.draws[1] != 0 && !light_count.logged))) {
    renodx::utils::log::i(std::format("[Total War] Light draws in a frame: point lights ({:08X}) {} draws, {} instances, {} indices; "
                                      "particle lights ({:08X}) {} draws, {} instances, {} indices.",
                                      internal::shader_hashes[0], light_count.draws[0], light_count.instances[0], light_count.indices[0],
                                      internal::shader_hashes[1], light_count.draws[1], light_count.instances[1], light_count.indices[1]));
    light_count.logged = true;
    light_count.points_logged = light_count.points_logged || points;
  }
  light_count.draws = {};
  light_count.instances = {};
  light_count.indices = {};
  internal::light_counting.store(log_details && !light_count.points_logged);
}

inline void Uninstall() { renodx::utils::command_action::Unregister(internal::OnLightCountDraw); }

}  // namespace twattila::light_count
