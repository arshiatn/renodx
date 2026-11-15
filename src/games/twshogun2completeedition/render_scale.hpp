/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <include/reshade.hpp>

// Render scale (experimental): the 3D image drawn into the top-left render size of the game's display-size targets,
// for the game's DLSS to upscale (game_dlss.hpp creates and evaluates it with that render subrect). The game's 3D
// passes run from the frame's start to its DLSS evaluation (traces debug1 / debug2: shadows, G-buffer, lighting, fog,
// transparents, depth of field); bloom, tonemap and UI come after and stay at the display size. Its shaders already
// tell viewport and render target apart (camera.hlsl: g_viewport_dimensions for clip space and pixel velocities,
// screen_size and g_render_target_dimensions for texture coordinates, compute shaders clamp to the viewport), so in
// that span: viewports and scissors at the display size (or half of it) at 0, 0 are scaled, and the camera constants'
// g_viewport_dimensions (c38, when it equals the display size) become the render size. Compute dispatches keep their
// sizes (their threads past the viewport clamp or write unused texels).
// v18: the particles' reduced size too (VFX dynamic resolution: the effects view's int(display x s), s from its minimum
// to 1 (raised with the render scale, particle_resolution.hpp), its viewport, scissor and camera c38), to the same
// fraction of the render size: left display-sized, smoke filling the screen was drawn about 3x too large and stretched
// from the corner (ATN, a burning house below 100%).
// Frame generation: the same uploads give the main view's camera (its constants as written), with or without scaling.
namespace twshogun2::render_scale {

// The overlay's view of the last frame.
struct FrameCounts {
  uint64_t viewports = 0;  // binds with a scaled viewport
  uint64_t cameras = 0;    // camera constants patched
  float particle_scale = 1.f;  // the particles' lowest VFX dynamic resolution scale
};

namespace internal {

constexpr uint32_t CAMERA_MIN_SIZE = 44 * 16;
constexpr uint32_t CAMERA_MAX_SIZE = 1024;
constexpr size_t VIEWPORT_DIMENSIONS = 38 * 4;  // float index of g_viewport_dimensions
constexpr size_t SCREEN_SIZE = 37 * 4;
// camera_VS_PS's floats (44 float4).
constexpr size_t CAMERA_FLOATS = 44 * 4;

struct State {
  std::mutex mutex;
  std::atomic_bool scaling = false;  // in the 3D span of a frame with a render scale
  std::atomic_bool main_view = false;  // and its last viewport the main view's (the display size, scaled)
  uint32_t display[2] = {};
  uint32_t render[2] = {};
  std::unordered_map<uint64_t, float*> mapped;  // camera-sized constant buffers mapped for writing
  std::atomic_bool any_mapped = false;           // mapped is not empty (each unmap checks it without the lock)
  bool counting = false;   // this frame's counts kept (the overlay's debug view asked for them)
  uint64_t viewports = 0;  // scaled this frame
  uint64_t cameras = 0;
  float particle_scale = 1.f;  // the particles' lowest VFX dynamic resolution scale this frame
  FrameCounts last_counts = {};  // the last frame's (overlay)
  // Frame generation: the main view's camera in this frame's 3D span (captured while `capturing`).
  std::atomic_bool capturing = false;
  std::array<float, CAMERA_FLOATS> camera = {};
  bool camera_found = false;
  // The last camera-sized buffer's screen and viewport sizes while capturing (the log, when none matched).
  std::array<float, 4> last_seen = {};
  uint32_t candidates = 0;
};
static State state;
static std::atomic_bool counts_requested = false;

// A viewport or rect length to scale: the display's or half of it.
inline bool ScaledLength(float length, uint32_t display, uint32_t render, float* scaled) {
  if (length == static_cast<float>(display)) {
    *scaled = static_cast<float>(render);
    return true;
  }
  if (length == static_cast<float>(display / 2)) {
    *scaled = static_cast<float>(render / 2);
    return true;
  }
  return false;
}

// The particles' reduced size (VFX dynamic resolution: int(display x s) per axis, s 0.3-1): the same fraction of the
// render size, unrounded (viewports take fractions; the effects overlay reads the particles back at s x the pixel).
inline bool ScaledParticleSize(float width, float height, const uint32_t display[2], const uint32_t render[2],
                               float* scaled_width, float* scaled_height) {
  const float fraction_x = width / static_cast<float>(display[0]);
  const float fraction_y = height / static_cast<float>(display[1]);
  if (fraction_x < 0.29f || fraction_x >= 1.f || std::abs(fraction_x - fraction_y) > 1.5f / static_cast<float>(display[1])) {
    return false;
  }
  *scaled_width = width * static_cast<float>(render[0]) / static_cast<float>(display[0]);
  *scaled_height = height * static_cast<float>(render[1]) / static_cast<float>(display[1]);
  return true;
}

}  // namespace internal

// Present: the next frame's 3D span scales (scale below 1 and the game's DLSS running) or not; `capture`: its main
// view's camera kept (MainCamera).
inline void BeginFrame(bool scaling, std::array<uint32_t, 2> display, std::array<uint32_t, 2> render, bool capture) {
  auto& state = internal::state;
  const std::lock_guard lock(state.mutex);
  state.display[0] = display[0];
  state.display[1] = display[1];
  state.render[0] = render[0];
  state.render[1] = render[1];
  state.last_counts = {
      .viewports = state.viewports,
      .cameras = state.cameras,
      .particle_scale = state.particle_scale,
  };
  state.counting = internal::counts_requested.exchange(false);
  state.viewports = 0;
  state.cameras = 0;
  state.particle_scale = 1.f;
  state.scaling.store(scaling && display[0] != 0 && render[0] != 0);
  state.main_view.store(false);
  state.capturing.store(capture && display[0] != 0);
  state.camera_found = false;
}

// The game's DLSS evaluation: the 3D span ends.
inline void EndScaling() {
  internal::state.scaling.store(false);
  internal::state.main_view.store(false);
  internal::state.capturing.store(false);
}

// The main view's camera constants of this frame's 3D span (camera_VS_PS as written); false: none captured.
inline bool MainCamera(std::array<float, internal::CAMERA_FLOATS>* camera) {
  const std::lock_guard lock(internal::state.mutex);
  if (!internal::state.camera_found) {
    static std::atomic_uint32_t failures = 0;
    if (const uint32_t count = ++failures; count == 1 || count == 100 || count == 1000) {
      const auto& seen = internal::state.last_seen;
      renodx::utils::log::i("[Shogun 2 frame generation] No main camera this frame: ", internal::state.candidates,
                            " camera buffers, the last screen ", seen[0], "x", seen[1], ", viewport ", seen[2], "x", seen[3],
                            ", display ", internal::state.display[0], "x", internal::state.display[1], ".");
    }
    return false;
  }
  *camera = internal::state.camera;
  return true;
}

// In the 3D span with the main view's viewport bound (not shadow maps, reflections' or half-size passes).
inline bool MainView() {
  return internal::state.main_view.load(std::memory_order_relaxed);
}

inline void OnBindViewports(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count,
                            const reshade::api::viewport* viewports) {
  auto& state = internal::state;
  if (!state.scaling.load(std::memory_order_relaxed) || count == 0 || count > 16) return;
  std::array<reshade::api::viewport, 16> scaled = {};
  bool changed = false;
  {
    const std::lock_guard lock(state.mutex);
    for (uint32_t index = 0; index < count; ++index) {
      scaled[index] = viewports[index];
      auto& viewport = scaled[index];
      float width = 0.f;
      float height = 0.f;
      if (viewport.x != 0.f || viewport.y != 0.f) continue;
      if (internal::ScaledLength(viewport.width, state.display[0], state.render[0], &width)
          && internal::ScaledLength(viewport.height, state.display[1], state.render[1], &height)) {
        viewport.width = width;
        viewport.height = height;
        changed = true;
      } else if (internal::ScaledParticleSize(viewport.width, viewport.height, state.display, state.render, &width,
                                              &height)) {
        if (state.counting) {
          state.particle_scale = std::min(state.particle_scale, viewport.width / static_cast<float>(state.display[0]));
        }
        viewport.width = width;
        viewport.height = height;
        changed = true;
      }
    }
    if (changed && state.counting) {
      ++state.viewports;
    }
    state.main_view.store(first == 0 && viewports[0].x == 0.f && viewports[0].y == 0.f
                          && viewports[0].width == static_cast<float>(state.display[0])
                          && viewports[0].height == static_cast<float>(state.display[1]));
  }
  if (changed) cmd_list->bind_viewports(first, count, scaled.data());
}

inline void OnBindScissorRects(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count,
                               const reshade::api::rect* rects) {
  auto& state = internal::state;
  if (!state.scaling.load(std::memory_order_relaxed) || count == 0 || count > 16) return;
  std::array<reshade::api::rect, 16> scaled = {};
  bool changed = false;
  {
    const std::lock_guard lock(state.mutex);
    for (uint32_t index = 0; index < count; ++index) {
      scaled[index] = rects[index];
      auto& rect = scaled[index];
      float right = 0.f;
      float bottom = 0.f;
      if (rect.left != 0 || rect.top != 0) continue;
      if ((internal::ScaledLength(static_cast<float>(rect.right), state.display[0], state.render[0], &right)
           && internal::ScaledLength(static_cast<float>(rect.bottom), state.display[1], state.render[1], &bottom))
          || internal::ScaledParticleSize(static_cast<float>(rect.right), static_cast<float>(rect.bottom), state.display,
                                          state.render, &right, &bottom)) {
        rect.right = static_cast<int32_t>(std::ceil(right));
        rect.bottom = static_cast<int32_t>(std::ceil(bottom));
        changed = true;
      }
    }
  }
  if (changed) cmd_list->bind_scissor_rects(first, count, scaled.data());
}

// Camera constants written by the game (Map / Unmap of a dynamic constant buffer): patched before the unmap.
inline void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t, uint64_t,
                              reshade::api::map_access access, void** data) {
  auto& state = internal::state;
  if ((!state.scaling.load(std::memory_order_relaxed) && !state.capturing.load(std::memory_order_relaxed)) || data == nullptr
      || *data == nullptr || access == reshade::api::map_access::read_only) {
    return;
  }
  const auto desc = device->get_resource_desc(resource);
  if (desc.type != reshade::api::resource_type::buffer
      || (desc.usage & reshade::api::resource_usage::constant_buffer) == 0
      || desc.buffer.size < internal::CAMERA_MIN_SIZE || desc.buffer.size > internal::CAMERA_MAX_SIZE) {
    return;
  }
  const std::lock_guard lock(state.mutex);
  state.mapped[resource.handle] = static_cast<float*>(*data);
  state.any_mapped.store(true, std::memory_order_relaxed);
}

inline void OnUnmapBufferRegion(reshade::api::device*, reshade::api::resource resource) {
  auto& state = internal::state;
  if (!state.any_mapped.load(std::memory_order_relaxed)) return;
  const std::lock_guard lock(state.mutex);
  const auto found = state.mapped.find(resource.handle);
  if (found == state.mapped.end()) return;
  float* constants = found->second;
  state.mapped.erase(found);
  state.any_mapped.store(!state.mapped.empty(), std::memory_order_relaxed);
  // The main view's camera: screen_size and g_viewport_dimensions both the display size; the particles' view's
  // g_viewport_dimensions their reduced size.
  const auto display_width = static_cast<float>(state.display[0]);
  const auto display_height = static_cast<float>(state.display[1]);
  float* viewport = constants + internal::VIEWPORT_DIMENSIONS;
  const float* screen = constants + internal::SCREEN_SIZE;
  if (state.capturing.load(std::memory_order_relaxed)) {
    state.last_seen = {screen[0], screen[1], viewport[0], viewport[1]};
    ++state.candidates;
  }
  if (screen[0] != display_width || screen[1] != display_height) return;
  if (state.capturing.load(std::memory_order_relaxed) && viewport[0] == display_width && viewport[1] == display_height) {
    std::copy_n(constants, internal::CAMERA_FLOATS, state.camera.begin());
    state.camera_found = true;
  }
  if (!state.scaling.load(std::memory_order_relaxed)) return;
  float width = static_cast<float>(state.render[0]);
  float height = static_cast<float>(state.render[1]);
  if ((viewport[0] != display_width || viewport[1] != display_height)
      && !internal::ScaledParticleSize(viewport[0], viewport[1], state.display, state.render, &width, &height)) {
    return;
  }
  viewport[0] = width;
  viewport[1] = height;
  viewport[2] = 1.f / width;
  viewport[3] = 1.f / height;
  if (state.counting) {
    ++state.cameras;
  }
}

// Overlay (debug view): count what the next frame scales.
inline void RequestCounts() {
  internal::counts_requested.store(true, std::memory_order_relaxed);
}

// Overlay: what was scaled in the last counted frame.
inline FrameCounts Counts() {
  const std::lock_guard lock(internal::state.mutex);
  return internal::state.last_counts;
}

}  // namespace twshogun2::render_scale
