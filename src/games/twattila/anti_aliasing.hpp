/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./debug_timing.hpp"
#include "./dlaa_bridge.hpp"
#include "./frame_generation.hpp"
#include "./frame_history.hpp"
#include "./frame_trace.hpp"
#include "./light_bounce.hpp"
#include "./object_motion.hpp"
#include "./smaa/AreaTex.h"
#include "./smaa/SearchTex.h"
#include "./texture_lod_bias.hpp"
#include "./view_share.hpp"

// Anti-aliasing (Experimental): SMAA 1x (smaa/SMAA.hlsl, MIT, Jimenez et al. 2013) or CMAA2 (cmaa2/CMAA2.hlsl, Apache
// 2.0, Copyright (c) 2018 Intel Corporation), as compute shaders right after the game's tone mapper (its replaced
// shaders' on_drawn: before the UI), on its output at the display's size. That target (no unordered access) is copied,
// anti-aliased into the addon's result and copied back. SMAA works on the output as it is (2.0 encoded: perceptual);
// CMAA2 on a linear BT.2020 copy (aa_cmaa2_prepare: it stores colours without sign), only the pixels it changed
// converted back (aa_cmaa2_resolve). Every compute binding is put back after. D3D11.
// TAA and SMAA T2x (ATN: edge AA cannot fix grass): the main view's camera is jittered as the game uploads its
// constants (camera_VS_PS: a perspective camera with the display's viewport, written through Map / Unmap or
// UpdateSubresource; its projection, view-projection and their inverses moved by the frame's sub-pixel offset: TAA
// Halton (2, 3) over 8 frames, SMAA T2x SMAA's two); the sun light draws give the camera the scene was lit with (the
// buffer they read) and its depth, and after the tone mapper this frame (SMAA T2x: SMAA'd with that frame's sub-sample)
// is blended with the history reprojected by the camera (aa_taa_resolve), then sharpened (aa_taa_sharpen). Cameras
// uploaded after the tone mapper (the UI's) are left as they are; a frame without a main view (menus) ends the jitter.
// DLAA's motion: the camera's from the depth, and for what moves on its own its own (object_motion.hpp).
namespace twattila::anti_aliasing {

using Microsoft::WRL::ComPtr;

enum class Method : uint8_t {
  OFF = 0,
  SMAA = 1,
  CMAA2 = 2,
  SMAA_T2X = 3,
  TAA = 4,
  DLAA = 5,  // NVIDIA's, through the 64-bit helper (dlaa_bridge.hpp)
};

// The object motion's types under their old names (the addons' MOTION_SHADERS).
using object_motion::MotionKind;
using object_motion::MotionShader;
using ObjectMotion = object_motion::Shaders;

struct Settings {
  Method method = Method::OFF;
  int quality = 2;               // 0 low, 1 medium, 2 high, 3 ultra
  bool extra_sharpness = false;  // CMAA2's
  float history_weight = 0.9f;   // TAA: the history's share each frame
  float sharpening = 0.25f;      // TAA, SMAA T2x
  uint32_t dlaa_preset = 0;      // DLAA: NVSDK_NGX_DLSS_Hint_Render_Preset (0: NVIDIA's)
  float render_scale = 1.f;      // DLAA below 1: DLSS (the 3D image drawn at this fraction of the display's size)
  bool ray_reconstruction = false;          // DLAA / DLSS: NVIDIA's Ray Reconstruction (guides from the G-buffer)
  uint32_t ray_reconstruction_preset = 0;  // NVSDK_NGX_RayReconstruction_Hint_Render_Preset (0: NVIDIA's)
  bool ray_tracing = false;                // DLAA / DLSS: the bridge's ray tracing textures (rt_scene.hpp)
  // DLAA / DLSS: the G-buffer guides for ray tracing's paths (path tracing, reflections) and the reflections' textures.
  bool ray_traced_guides = false;
  bool ray_traced_reflections = false;
  uint32_t frame_generation = 0;           // DLAA / DLSS: frames shown per rendered frame (2-6; 1 Dynamic; 0 off: frame_generation.hpp)
  bool reflex_boost = false;
  uint32_t frame_limit = 0;   // frame generation's Reflex limit, rendered frames per second (0: none)
  uint32_t frame_preset = 0;  // frame generation's NVIDIA preset A-C as 1-3 (0: the driver's)
  bool hud_recomposition = true;
  // DLSS: the textures' levels below 100% (texture_lod_bias.hpp): 0 the game's, 1 as sharp as at the display size, 2
  // one step sharper (NVIDIA's advice), 3 two.
  int texture_lod_bias = 2;
  // DLAA / DLSS: the units', props' and cloth's own motion (object_motion.hpp redraws them); off, the camera's alone.
  bool object_motion = true;
  // DLAA / DLSS's Debug View: 0 off, 1 the motion NVIDIA gets, 2 the objects' own (aa_dlaa_finish), 3 no camera motion
  // (the objects' alone: aa_dlaa_prepare).
  uint32_t debug_view = 0;
  uint32_t motion_test = 0;  // object_motion.hpp SetTest (debug)
};

namespace internal {

// aa.hlsli's aa_settings.
struct ShaderSettings {
  std::array<float, 4> metrics = {};
  float smaa_threshold = 0.f;
  float smaa_search_steps = 0.f;
  float smaa_search_steps_diag = 0.f;
  float smaa_corner_rounding = 0.f;
  float cmaa2_edge_threshold = 0.f;
  float cmaa2_local_contrast = 0.f;
  float cmaa2_simple_shape_blur = 0.f;
  float padding = 0.f;
  std::array<float, 4> smaa_subsample_indices = {};
  std::array<float, 4> dlaa_debug = {};  // the view, the input size, the colour's encoding for NVIDIA
};

// aa.hlsli's aa_taa: the cameras in the game's register layout.
struct TaaConstants {
  std::array<float, 16> view_projection = {};
  std::array<float, 16> inverse_view_projection = {};
  std::array<float, 16> previous_view_projection = {};
  std::array<float, 2> projection_z = {};
  float history_weight = 0.f;
  float reset = 1.f;
  float sharpening = 0.f;
  std::array<float, 2> jitter = {};
  float padding = 0.f;
};

// camera_VS_PS's registers (16 bytes each): projection c5-c8, view_projection c9-c12, inv_projection c17-c20,
// inv_view_projection c21-c24, g_screen_size c27, g_viewport_dimensions c29 (size, its reciprocal); the first 30 read
// (Attila's buffer holds 36, Rome II's 35).
constexpr size_t CAMERA_BYTES = 30 * 16;
constexpr size_t PROJECTION = 5;
constexpr size_t VIEW_PROJECTION = 9;
constexpr size_t INVERSE_PROJECTION = 17;
constexpr size_t INVERSE_VIEW_PROJECTION = 21;
constexpr size_t TIME = 26;  // time_in_sec (.x)
constexpr size_t SCREEN_SIZE = 27;
constexpr size_t VIEWPORT = 29;
constexpr uint32_t TAA_PHASES = 8;

// A main view camera.
struct Camera {
  std::array<float, 16> view_projection = {};          // without the jitter
  std::array<float, 16> inverse_view_projection = {};  // with it (as the depth was drawn)
  std::array<float, 2> projection_z = {};             // projection _m22, _m32
  std::array<float, 3> position = {};                 // camera_position (c0)
  std::array<float, 16> projection = {};               // without the jitter (Ray Reconstruction's camera)
  std::array<float, 16> inverse_projection = {};
  float time = 0.f;                                   // time_in_sec (the wind's)
  std::array<float, 2> jitter_clip = {};              // the jitter its uploaded rows got: clip x / y per w (0: none)
  bool valid = false;
};

// A constant buffer that has held the main view's camera, and its last upload's (not valid: another camera since).
struct Upload {
  ID3D11Buffer* buffer = nullptr;
  Camera camera;
};

// SMAA's presets, low to ultra: threshold, search steps, diagonal search steps (none below high), corner rounding.
constexpr std::array<std::array<float, 4>, 4> SMAA_PRESETS = {{
    {0.15f, 4.f, 0.f, 25.f},
    {0.1f, 8.f, 0.f, 25.f},
    {0.1f, 16.f, 8.f, 25.f},
    {0.05f, 32.f, 16.f, 25.f},
}};
// CMAA2's presets' edge thresholds, low to ultra.
constexpr std::array<float, 4> CMAA2_THRESHOLDS = {0.15f, 0.1f, 0.07f, 0.05f};
constexpr UINT UAV_COUNT = 8;  // CMAA2's u0-u7
constexpr UINT SRV_COUNT = 7;  // DLAA's Debug View: t0-t6

struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11Buffer> settings;
  ComPtr<ID3D11SamplerState> linear_clamp;
  ComPtr<ID3D11SamplerState> point_clamp;
  ShaderSettings uploaded = {};
  ComPtr<ID3D11ComputeShader> smaa_edges;
  ComPtr<ID3D11ComputeShader> smaa_weights;
  ComPtr<ID3D11ComputeShader> smaa_blend;
  ComPtr<ID3D11ShaderResourceView> area_srv;
  ComPtr<ID3D11ShaderResourceView> search_srv;
  ComPtr<ID3D11ComputeShader> cmaa2_prepare;
  ComPtr<ID3D11ComputeShader> cmaa2_edges;
  ComPtr<ID3D11ComputeShader> cmaa2_args;
  ComPtr<ID3D11ComputeShader> cmaa2_candidates;
  ComPtr<ID3D11ComputeShader> cmaa2_apply;
  ComPtr<ID3D11ComputeShader> cmaa2_resolve;
  ComPtr<ID3D11ComputeShader> taa_resolve;
  ComPtr<ID3D11ComputeShader> taa_sharpen;
  ComPtr<ID3D11ComputeShader> dlaa_prepare;
  ComPtr<ID3D11ComputeShader> dlaa_finish;
  ComPtr<ID3D11ComputeShader> rr_guides;  // Ray Reconstruction's guides, written in `guides_frame`
  uint64_t guides_frame = UINT64_MAX;
  ComPtr<ID3D11Buffer> taa_constants;
  // The target's size and format and the method: its copy, the result and the method's working storage.
  UINT width = 0;
  UINT height = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  Method method = Method::OFF;
  ComPtr<ID3D11Texture2D> color;
  ComPtr<ID3D11ShaderResourceView> color_srv;
  // DLAA: the tone mapper's output read as it is (no copy into `color`) where it can be: its view, by its texture.
  ComPtr<ID3D11Texture2D> frame_texture;
  ComPtr<ID3D11ShaderResourceView> frame_srv;
  ComPtr<ID3D11Texture2D> result;
  ComPtr<ID3D11UnorderedAccessView> result_uav;
  ComPtr<ID3D11UnorderedAccessView> smaa_edges_uav;
  ComPtr<ID3D11ShaderResourceView> smaa_edges_srv;
  ComPtr<ID3D11UnorderedAccessView> smaa_weights_uav;
  ComPtr<ID3D11ShaderResourceView> smaa_weights_srv;
  ComPtr<ID3D11UnorderedAccessView> work_uav;
  ComPtr<ID3D11ShaderResourceView> work_srv;
  // CMAA2's u1-u7: edges, shape candidates, blend locations, blend items, list heads, control, dispatch arguments.
  std::array<ComPtr<ID3D11UnorderedAccessView>, UAV_COUNT - 1> cmaa2_storage;
  ComPtr<ID3D11Buffer> cmaa2_arguments;
  // TAA / SMAA T2x: the results (this frame's and the last, in turn), their distances, the scene's depth copied.
  std::array<ComPtr<ID3D11UnorderedAccessView>, 2> history_uavs;
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> history_srvs;
  std::array<ComPtr<ID3D11UnorderedAccessView>, 2> distance_uavs;
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> distance_srvs;
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11ShaderResourceView> depth_srv;
  DXGI_FORMAT depth_format = DXGI_FORMAT_UNKNOWN;
  size_t history_index = 0;  // the one written this frame
  bool history_valid = false;
};

struct Runtime {
  std::shared_mutex mutex;
  Settings settings = {};
  UINT display_width = 0;
  UINT display_height = 0;
  bool logged_unusable = false;
  bool logged_msaa = false;
  bool logged_camera = false;
  uint64_t frame = 0;
  std::array<float, 2> jitter = {};        // pixels, x right, y up
  UINT render_width = 0;                   // DLSS: the 3D image's size (else the display's)
  UINT render_height = 0;
  uint64_t camera_frame = UINT64_MAX;      // the frame `camera` and `scene_depth` are from (a sun light draw's)
  uint64_t tonemapped_frame = UINT64_MAX;  // after it: no jitter
  uint64_t applied_frame = UINT64_MAX;     // the last frame TAA / SMAA T2x ran
  Camera camera;
  Camera previous_camera;
  std::vector<Upload> uploads;
  ComPtr<ID3D11Texture2D> scene_depth;
  DeviceObjects objects;
};

static Runtime runtime;
// TAA or SMAA T2x on: the camera uploads are watched (else every event returns at once).
static std::atomic_bool temporal = false;
// DLSS: this frame's 3D span (present to the tone mapper) draws at the render size: viewports and scissors at the
// display's size (or a power-of-two fraction of it) at 0, 0 scaled, and the main camera's viewport dimensions. The
// game's post passes take their texture coordinates from the pixel position and the screen size (the tone mapper:
// g_screen_size.zw x vpos), so the image stays in the targets' top-left corner, which DLSS then upscales.
static std::atomic_bool scaling = false;
// DLSS: the main view's viewport bound now (scaled from the display's size; texture_lod_bias.hpp).
static std::atomic_bool main_view = false;
// DLSS: the last viewports and scissor rectangles scaled and the game's own (put back after the tone mapper: the game
// draws on without binding them again, Rome II's lens flares; Attila's lens flare and unit markers kept the scissor).
template <typename T>
struct BoundScaled {
  std::mutex mutex;
  uint32_t count = 0;
  std::array<T, 16> scaled = {};
  std::array<T, 16> original = {};
};
static BoundScaled<reshade::api::viewport> bound_viewports;
static BoundScaled<reshade::api::rect> bound_scissors;
// A camera buffer this thread mapped for writing, and where.
static thread_local ID3D11Buffer* mapped_buffer = nullptr;
static thread_local std::byte* mapped_data = nullptr;

inline float Halton(uint32_t index, uint32_t base) {
  float result = 0.f;
  float fraction = 1.f / static_cast<float>(base);
  for (; index > 0; index /= base, fraction /= static_cast<float>(base)) {
    result += fraction * static_cast<float>(index % base);
  }
  return result;
}

// DLSS: a viewport or scissor length in the 3D span, the display's or a power-of-two fraction of it (half-size and
// smaller passes), as the same fraction of the render size.
inline bool ScaledLength(float length, UINT display, UINT render, float* scaled) {
  for (UINT shift = 0; shift <= 4; ++shift) {
    if (length == static_cast<float>(display >> shift)) {
      *scaled = static_cast<float>(std::max(render >> shift, 1u));
      return true;
    }
  }
  return false;
}

// A constant buffer that may hold a camera.
inline bool CameraSized(ID3D11Buffer* buffer) {
  if (buffer == nullptr) return false;
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  return (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0 && desc.ByteWidth >= CAMERA_BYTES && desc.ByteWidth <= 4096;
}

// An upload into `buffer` (`data`: at least CAMERA_BYTES): the main view's camera noted and jittered; true: `data`
// changed.
inline bool JitterCamera(ID3D11Buffer* buffer, std::byte* data) {
  const debug_timing::Timed timed{debug_timing::CAMERAS};
  std::array<float, CAMERA_BYTES / 4> c;
  std::memcpy(c.data(), data, CAMERA_BYTES);
  const auto reg = [&](size_t index, size_t component) -> float& { return c[index * 4 + component]; };
  const std::unique_lock lock(runtime.mutex);
  auto upload = std::ranges::find(runtime.uploads, buffer, &Upload::buffer);
  const bool display_viewport = runtime.display_width != 0
                                && reg(VIEWPORT, 0) == static_cast<float>(runtime.display_width)
                                && reg(VIEWPORT, 1) == static_cast<float>(runtime.display_height);
  // DLSS: in the 3D span every camera of the display's screen with its viewport at the display's size or a
  // power-of-two fraction of it (the main view's, its passes', half-size views') gets that fraction of the render size,
  // as its viewports (so the main camera's jitter below is in render pixels too).
  float scaled_width = 0.f;
  float scaled_height = 0.f;
  const bool scaled = scaling.load(std::memory_order_relaxed) && runtime.tonemapped_frame != runtime.frame
                      && reg(SCREEN_SIZE, 0) == static_cast<float>(runtime.display_width)
                      && reg(SCREEN_SIZE, 1) == static_cast<float>(runtime.display_height)
                      && ScaledLength(reg(VIEWPORT, 0), runtime.display_width, runtime.render_width, &scaled_width)
                      && ScaledLength(reg(VIEWPORT, 1), runtime.display_height, runtime.render_height, &scaled_height);
  if (scaled) {
    reg(VIEWPORT, 0) = scaled_width;
    reg(VIEWPORT, 1) = scaled_height;
    reg(VIEWPORT, 2) = 1.f / scaled_width;
    reg(VIEWPORT, 3) = 1.f / scaled_height;
  }
  // A perspective projection (its last column 0, 0, 1, 0) with the display's viewport.
  if (reg(PROJECTION + 3, 0) != 0.f || reg(PROJECTION + 3, 1) != 0.f || reg(PROJECTION + 3, 2) != 1.f
      || reg(PROJECTION + 3, 3) != 0.f || !display_viewport) {
    if (upload != runtime.uploads.end()) upload->camera.valid = false;
    if (scaled) {
      std::memcpy(data, c.data(), CAMERA_BYTES);
    }
    return scaled;
  }
  if (upload == runtime.uploads.end()) {
    upload = runtime.uploads.insert(runtime.uploads.end(), Upload{.buffer = buffer});
    if (!runtime.logged_camera) {
      renodx::utils::log::i("[Total War] Anti-aliasing: the main view's camera found for TAA.");
      runtime.logged_camera = true;
    }
  }
  auto& camera = upload->camera;
  std::copy_n(c.begin() + VIEW_PROJECTION * 4, 16, camera.view_projection.begin());
  camera.position = {reg(0, 0), reg(0, 1), reg(0, 2)};
  std::copy_n(c.begin() + PROJECTION * 4, 16, camera.projection.begin());
  std::copy_n(c.begin() + INVERSE_PROJECTION * 4, 16, camera.inverse_projection.begin());
  camera.projection_z = {reg(PROJECTION + 2, 2), reg(PROJECTION + 2, 3)};
  camera.time = reg(TIME, 0);
  camera.valid = true;
  camera.jitter_clip = {};
  if (runtime.tonemapped_frame == runtime.frame || (runtime.jitter[0] == 0.f && runtime.jitter[1] == 0.f)) {
    std::copy_n(c.begin() + INVERSE_VIEW_PROJECTION * 4, 16, camera.inverse_view_projection.begin());
    if (scaled) {
      std::memcpy(data, c.data(), CAMERA_BYTES);
    }
    return scaled;
  }
  // Clip x / y move by the jitter x w: in the projection (column 0 / 1, row 2) and the view-projection (+ x its w).
  const float jitter_x = 2.f * runtime.jitter[0] / reg(VIEWPORT, 0);
  const float jitter_y = 2.f * runtime.jitter[1] / reg(VIEWPORT, 1);
  reg(PROJECTION, 2) += jitter_x;
  reg(PROJECTION + 1, 2) += jitter_y;
  for (size_t component = 0; component < 4; ++component) {
    reg(VIEW_PROJECTION, component) += jitter_x * reg(VIEW_PROJECTION + 3, component);
    reg(VIEW_PROJECTION + 1, component) += jitter_y * reg(VIEW_PROJECTION + 3, component);
  }
  camera.jitter_clip = {jitter_x, jitter_y};
  // The inverses again (the registers hold the transposes: their inverse is the inverse's transpose).
  const auto invert = [&](size_t from, size_t to) {
    const float* m = &c[from * 4];
    std::array<float, 16> inverse;
    inverse[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inverse[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inverse[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inverse[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inverse[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inverse[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inverse[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inverse[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inverse[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inverse[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inverse[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inverse[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inverse[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inverse[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inverse[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inverse[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float determinant = m[0] * inverse[0] + m[1] * inverse[4] + m[2] * inverse[8] + m[3] * inverse[12];
    if (determinant == 0.f) return;
    for (size_t index = 0; index < 16; ++index) {
      c[to * 4 + index] = inverse[index] / determinant;
    }
  };
  invert(PROJECTION, INVERSE_PROJECTION);
  invert(VIEW_PROJECTION, INVERSE_VIEW_PROJECTION);
  std::copy_n(c.begin() + INVERSE_VIEW_PROJECTION * 4, 16, camera.inverse_view_projection.begin());
  std::memcpy(data, c.data(), CAMERA_BYTES);
  return true;
}

inline void OnMapBufferRegion(reshade::api::device*, reshade::api::resource resource, uint64_t offset, uint64_t,
                              reshade::api::map_access access, void** data) {
  const debug_timing::Timed timed{debug_timing::MAP};
  if (!temporal.load(std::memory_order_relaxed) || access == reshade::api::map_access::read_only || offset != 0
      || data == nullptr || *data == nullptr) {
    return;
  }
  auto* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
  if (!CameraSized(buffer)) return;
  mapped_buffer = buffer;
  mapped_data = static_cast<std::byte*>(*data);
}

// Before the native Unmap, while the pointer is still valid.
inline void OnUnmapBufferRegion(reshade::api::device*, reshade::api::resource resource) {
  const debug_timing::Timed timed{debug_timing::UNMAP};
  if (mapped_buffer == nullptr || mapped_buffer != reinterpret_cast<ID3D11Buffer*>(resource.handle)) return;
  JitterCamera(mapped_buffer, mapped_data);
  mapped_buffer = nullptr;
  mapped_data = nullptr;
}

// UpdateSubresource: the main camera's uploaded jittered instead (true: the game's update skipped).
inline bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource,
                                 uint64_t offset, uint64_t size) {
  const debug_timing::Timed timed{debug_timing::UPDATE};
  if (!temporal.load(std::memory_order_relaxed) || data == nullptr || offset != 0) return false;
  auto* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
  if (!CameraSized(buffer)) return false;
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  if (size != UINT64_MAX && size < desc.ByteWidth) return false;
  std::vector<std::byte> copy(desc.ByteWidth);
  std::memcpy(copy.data(), data, copy.size());
  if (!JitterCamera(buffer, copy.data())) return false;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  ComPtr<ID3D11DeviceContext> context;
  native->GetImmediateContext(context.GetAddressOf());
  context->UpdateSubresource(buffer, 0, nullptr, copy.data(), 0, 0);
  return true;
}

inline void OnBindViewports(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count,
                            const reshade::api::viewport* viewports) {
  const debug_timing::Timed timed{debug_timing::VIEWPORTS};
  if (!scaling.load(std::memory_order_relaxed) || count == 0 || count > 16) {
    main_view.store(false, std::memory_order_relaxed);
    return;
  }
  std::array<reshade::api::viewport, 16> scaled = {};
  bool changed = false;
  bool main = false;
  {
    const std::shared_lock lock(runtime.mutex);
    // The main view: its first viewport the display's (scaled to the render size below).
    main = first == 0 && viewports[0].x == 0.f && viewports[0].y == 0.f
           && viewports[0].width == static_cast<float>(runtime.display_width)
           && viewports[0].height == static_cast<float>(runtime.display_height);
    for (uint32_t index = 0; index < count; ++index) {
      scaled[index] = viewports[index];
      auto& viewport = scaled[index];
      float width = 0.f;
      float height = 0.f;
      if (viewport.x == 0.f && viewport.y == 0.f
          && ScaledLength(viewport.width, runtime.display_width, runtime.render_width, &width)
          && ScaledLength(viewport.height, runtime.display_height, runtime.render_height, &height)) {
        viewport.width = width;
        viewport.height = height;
        changed = true;
      }
    }
  }
  main_view.store(main, std::memory_order_relaxed);
  if (changed) {
    cmd_list->bind_viewports(first, count, scaled.data());
    if (first == 0) {
      const std::unique_lock viewport_lock(bound_viewports.mutex);
      bound_viewports.count = count;
      std::copy_n(scaled.begin(), count, bound_viewports.scaled.begin());
      std::copy_n(viewports, count, bound_viewports.original.begin());
    }
  }
}

// Every present: the texture LOD bias for the next frames' main view (log2 of render / display width, minus 1 or 2
// for sharper; none at 100% or with it off), and whether the module tracks the samplers.
inline void OnPresent(reshade::api::command_queue* queue, reshade::api::swapchain*, const reshade::api::rect*,
                      const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  float bias = 0.f;
  {
    const std::shared_lock lock(runtime.mutex);
    const int choice = runtime.settings.texture_lod_bias;
    if (choice != 0 && runtime.render_width < runtime.display_width && runtime.display_width != 0) {
      bias = std::log2(static_cast<float>(runtime.render_width) / static_cast<float>(runtime.display_width))
             - static_cast<float>(choice - 1);
    }
  }
  texture_lod_bias::SetTracking(queue, bias != 0.f);
  texture_lod_bias::SetBias(bias);
  debug_timing::OnPresent();
}

inline void OnBindScissorRects(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count,
                               const reshade::api::rect* rects) {
  const debug_timing::Timed timed{debug_timing::SCISSORS};
  if (!scaling.load(std::memory_order_relaxed) || count == 0 || count > 16) return;
  std::array<reshade::api::rect, 16> scaled = {};
  bool changed = false;
  {
    const std::shared_lock lock(runtime.mutex);
    for (uint32_t index = 0; index < count; ++index) {
      scaled[index] = rects[index];
      auto& rect = scaled[index];
      float right = 0.f;
      float bottom = 0.f;
      if (rect.left == 0 && rect.top == 0
          && ScaledLength(static_cast<float>(rect.right), runtime.display_width, runtime.render_width, &right)
          && ScaledLength(static_cast<float>(rect.bottom), runtime.display_height, runtime.render_height, &bottom)) {
        rect.right = static_cast<int32_t>(right);
        rect.bottom = static_cast<int32_t>(bottom);
        changed = true;
      }
    }
  }
  if (changed) {
    cmd_list->bind_scissor_rects(first, count, scaled.data());
    if (first == 0) {
      const std::unique_lock scissor_lock(bound_scissors.mutex);
      bound_scissors.count = count;
      std::copy_n(scaled.begin(), count, bound_scissors.scaled.begin());
      std::copy_n(rects, count, bound_scissors.original.begin());
    }
  }
}

// The sun light draws (the main view's lighting): the camera they read (one the uploads noted) and the depth-stencil
// buffer (the display's size, one sample) for this frame.
constexpr auto OnSunDraw = []<typename T, typename Context>(
                               Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    if (!temporal.load(std::memory_order_relaxed)) return {};
    if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&native);
    if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
    const auto& sun_draw = command.template GetCallbackData<T>();
    ComPtr<ID3D11Buffer> camera;
    native->PSGetConstantBuffers(sun_draw.camera_slot, 1, camera.GetAddressOf());
    ComPtr<ID3D11DepthStencilView> depth_stencil;
    native->OMGetRenderTargets(0, nullptr, depth_stencil.GetAddressOf());
    if (!camera || !depth_stencil) return {};
    ComPtr<ID3D11Resource> resource;
    depth_stencil->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) return {};
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    const std::unique_lock lock(runtime.mutex);
    const auto upload = std::ranges::find(runtime.uploads, camera.Get(), &Upload::buffer);
    if (desc.SampleDesc.Count != 1 && !runtime.logged_msaa) {
      renodx::utils::log::w("[Total War] Anti-aliasing: the game's MSAA is on: turn the in-game anti-aliasing off for "
                            "TAA / SMAA T2x / DLAA / DLSS.");
      runtime.logged_msaa = true;
    }
    if (upload == runtime.uploads.end() || !upload->camera.valid || desc.Width != runtime.display_width
        || desc.Height != runtime.display_height || desc.SampleDesc.Count != 1) {
      return {};
    }
    if (runtime.camera_frame != runtime.frame) {
      runtime.previous_camera = runtime.camera;
      runtime.camera_frame = runtime.frame;
    }
    runtime.camera = upload->camera;
    runtime.scene_depth = texture;
    // Ray Reconstruction's guides from the G-buffer (normal, colour at the draw's first slots), once a frame.
    auto& objects = runtime.objects;
    auto& bridge = dlaa_bridge::bridge;
    if (runtime.settings.method != Method::DLAA || !(runtime.settings.ray_reconstruction || runtime.settings.ray_traced_guides)
        || objects.guides_frame == runtime.frame || !objects.rr_guides || !bridge.guides) {
      return {};
    }
    // Channels 1-3 (normal, colour, constants: each game's aa_rr_guides reads what its G-buffer keeps there).
    std::array<ID3D11ShaderResourceView*, 3> gbuffer = {};
    native->PSGetShaderResources(sun_draw.gbuffer_slot, 3, gbuffer.data());
    const bool single_sample = std::ranges::all_of(gbuffer, [](ID3D11ShaderResourceView* view) {
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
      if (view != nullptr) view->GetDesc(&view_desc);
      return view != nullptr && view_desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D;
    });
    if (single_sample) {
      ComPtr<ID3D11ComputeShader> game_shader;
      native->CSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
      std::array<ID3D11ShaderResourceView*, 3> game_views = {};
      native->CSGetShaderResources(0, 3, game_views.data());
      std::array<ID3D11UnorderedAccessView*, 4> game_uavs = {};
      native->CSGetUnorderedAccessViews(0, 4, game_uavs.data());
      const std::array<ID3D11UnorderedAccessView*, 4> guides = {bridge.diffuse_albedo.uav.Get(), bridge.specular_albedo.uav.Get(),
                                                                bridge.normals.uav.Get(), bridge.roughness.uav.Get()};
      constexpr std::array<UINT, 4> KEEP_COUNTS = {UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX};
      native->CSSetShader(objects.rr_guides.Get(), nullptr, 0);
      native->CSSetShaderResources(0, 3, gbuffer.data());
      native->CSSetUnorderedAccessViews(0, 4, guides.data(), KEEP_COUNTS.data());
      native->Dispatch((bridge.width + 7) / 8, (bridge.height + 7) / 8, 1);
      native->CSSetShader(game_shader.Get(), nullptr, 0);
      native->CSSetShaderResources(0, 3, game_views.data());
      native->CSSetUnorderedAccessViews(0, 4, game_uavs.data(), KEEP_COUNTS.data());
      for (auto* view : game_views) {
        if (view != nullptr) view->Release();
      }
      for (auto* uav : game_uavs) {
        if (uav != nullptr) uav->Release();
      }
      objects.guides_frame = runtime.frame;
    }
    for (auto* view : gbuffer) {
      if (view != nullptr) view->Release();
    }
    return {};
  }
};

// The device's shaders, samplers and SMAA's textures; false: not available.
inline bool PrepareDevice(ID3D11Device* device) {
  auto& objects = runtime.objects;
  if (objects.device.Get() == device) return static_cast<bool>(objects.settings);
  objects = {};
  objects.device = device;
  const auto compute = [&](const auto& code, ComPtr<ID3D11ComputeShader>* shader) {
    return SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader->GetAddressOf()));
  };
  const auto sampler = [&](D3D11_FILTER filter, ComPtr<ID3D11SamplerState>* state) {
    const D3D11_SAMPLER_DESC desc = {
        .Filter = filter,
        .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
        .ComparisonFunc = D3D11_COMPARISON_NEVER,
        .MaxLOD = D3D11_FLOAT32_MAX,
    };
    return SUCCEEDED(device->CreateSamplerState(&desc, state->GetAddressOf()));
  };
  const auto lookup = [&](UINT width, UINT height, DXGI_FORMAT format, const unsigned char* bytes, UINT pitch,
                          ComPtr<ID3D11ShaderResourceView>* srv) {
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = width,
        .Height = height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_IMMUTABLE,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
    };
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = bytes, .SysMemPitch = pitch};
    ComPtr<ID3D11Texture2D> texture;
    return SUCCEEDED(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()))
           && SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, srv->GetAddressOf()));
  };
  const D3D11_BUFFER_DESC settings_desc = {.ByteWidth = sizeof(ShaderSettings), .Usage = D3D11_USAGE_DEFAULT,
                                           .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_BUFFER_DESC taa_desc = {.ByteWidth = sizeof(TaaConstants), .Usage = D3D11_USAGE_DEFAULT,
                                      .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const bool made = compute(__aa_smaa_edges, &objects.smaa_edges) && compute(__aa_smaa_weights, &objects.smaa_weights)
                    && compute(__aa_smaa_blend, &objects.smaa_blend) && compute(__aa_cmaa2_prepare, &objects.cmaa2_prepare)
                    && compute(__aa_cmaa2_edges, &objects.cmaa2_edges) && compute(__aa_cmaa2_args, &objects.cmaa2_args)
                    && compute(__aa_cmaa2_candidates, &objects.cmaa2_candidates)
                    && compute(__aa_cmaa2_apply, &objects.cmaa2_apply)
                    && compute(__aa_cmaa2_resolve, &objects.cmaa2_resolve)
                    && compute(__aa_taa_resolve, &objects.taa_resolve) && compute(__aa_taa_sharpen, &objects.taa_sharpen)
                    && compute(__aa_dlaa_prepare, &objects.dlaa_prepare) && compute(__aa_dlaa_finish, &objects.dlaa_finish)
                    && compute(__aa_rr_guides, &objects.rr_guides)
                    && sampler(D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, &objects.linear_clamp)
                    && sampler(D3D11_FILTER_MIN_MAG_MIP_POINT, &objects.point_clamp)
                    && lookup(AREATEX_WIDTH, AREATEX_HEIGHT, DXGI_FORMAT_R8G8_UNORM, areaTexBytes, AREATEX_PITCH,
                              &objects.area_srv)
                    && lookup(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, DXGI_FORMAT_R8_UNORM, searchTexBytes, SEARCHTEX_PITCH,
                              &objects.search_srv)
                    && SUCCEEDED(device->CreateBuffer(&settings_desc, nullptr, objects.settings.GetAddressOf()))
                    && SUCCEEDED(device->CreateBuffer(&taa_desc, nullptr, objects.taa_constants.GetAddressOf()));
  if (!made) {
    renodx::utils::log::w("[Total War] Anti-aliasing: could not create its shaders or textures.");
    objects = {};
    objects.device = device;
  }
  return made;
}

// The storage for `method` on a `width` x `height` target of `format` (RGBA16); false: not available.
inline bool PrepareSize(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, Method method) {
  auto& objects = runtime.objects;
  if (objects.width == width && objects.height == height && objects.format == format && objects.method == method) {
    return true;
  }
  objects.width = 0;
  objects.color.Reset();
  objects.color_srv.Reset();
  objects.frame_texture.Reset();
  objects.frame_srv.Reset();
  objects.result.Reset();
  objects.result_uav.Reset();
  objects.smaa_edges_uav.Reset();
  objects.smaa_edges_srv.Reset();
  objects.smaa_weights_uav.Reset();
  objects.smaa_weights_srv.Reset();
  objects.work_uav.Reset();
  objects.work_srv.Reset();
  objects.cmaa2_storage = {};
  objects.cmaa2_arguments.Reset();
  objects.history_uavs = {};
  objects.history_srvs = {};
  objects.distance_uavs = {};
  objects.distance_srvs = {};
  objects.depth.Reset();
  objects.depth_srv.Reset();
  objects.depth_format = DXGI_FORMAT_UNKNOWN;
  objects.history_valid = false;
  // A texture with a float view (`uav` / `srv` null: no such view).
  const auto texture = [&](UINT texture_width, UINT texture_height, DXGI_FORMAT texture_format, DXGI_FORMAT view_format,
                           ComPtr<ID3D11Texture2D>* made, ComPtr<ID3D11UnorderedAccessView>* uav,
                           ComPtr<ID3D11ShaderResourceView>* srv) {
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = texture_width,
        .Height = texture_height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = texture_format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = static_cast<UINT>((uav != nullptr ? D3D11_BIND_UNORDERED_ACCESS : 0)
                                       | (srv != nullptr ? D3D11_BIND_SHADER_RESOURCE : 0)),
    };
    ComPtr<ID3D11Texture2D> local;
    ComPtr<ID3D11Texture2D>* target = (made != nullptr ? made : std::addressof(local));
    if (FAILED(device->CreateTexture2D(&desc, nullptr, target->GetAddressOf()))) return false;
    if (uav != nullptr) {
      const D3D11_UNORDERED_ACCESS_VIEW_DESC view = {.Format = view_format, .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D};
      if (FAILED(device->CreateUnorderedAccessView(target->Get(), &view, uav->GetAddressOf()))) return false;
    }
    if (srv != nullptr) {
      const D3D11_SHADER_RESOURCE_VIEW_DESC view = {
          .Format = view_format,
          .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
          .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1},
      };
      if (FAILED(device->CreateShaderResourceView(target->Get(), &view, srv->GetAddressOf()))) return false;
    }
    return true;
  };
  // A buffer of `count` elements of `stride` bytes (raw: 4-byte words) for unordered access.
  const auto buffer = [&](UINT count, UINT stride, bool raw, UINT extra_flags, const void* initial,
                          ComPtr<ID3D11UnorderedAccessView>* uav, ComPtr<ID3D11Buffer>* made) {
    const D3D11_BUFFER_DESC desc = {
        .ByteWidth = count * stride,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
        .MiscFlags = (raw ? D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS : D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) | extra_flags,
        .StructureByteStride = (raw ? 0 : stride),
    };
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = initial};
    ComPtr<ID3D11Buffer> local;
    ComPtr<ID3D11Buffer>* target = (made != nullptr ? made : std::addressof(local));
    if (FAILED(device->CreateBuffer(&desc, (initial != nullptr ? &data : nullptr), target->GetAddressOf()))) return false;
    const D3D11_UNORDERED_ACCESS_VIEW_DESC view = {
        .Format = (raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN),
        .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
        .Buffer = {.FirstElement = 0, .NumElements = (raw ? count * stride / 4 : count),
                   .Flags = static_cast<UINT>(raw ? D3D11_BUFFER_UAV_FLAG_RAW : 0)},
    };
    return SUCCEEDED(device->CreateUnorderedAccessView(target->Get(), &view, uav->GetAddressOf()));
  };
  // CMAA2's sizes as its sample's (enough for dense foliage), its edges two pixels per texel.
  const UINT half_width = (width + 1) / 2;
  const UINT pixels = width * height;
  constexpr std::array<UINT, 16> ZEROS = {};
  auto& storage = objects.cmaa2_storage;
  bool made =
      texture(width, height, format, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.color, nullptr, &objects.color_srv)
      && texture(width, height, format, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.result, &objects.result_uav, nullptr);
  if (method == Method::SMAA || method == Method::SMAA_T2X) {
    made = made
           && texture(width, height, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, nullptr, &objects.smaa_edges_uav,
                      &objects.smaa_edges_srv)
           && texture(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr,
                      &objects.smaa_weights_uav, &objects.smaa_weights_srv);
  }
  if (method == Method::SMAA_T2X || method == Method::TAA) {
    // SMAA T2x's SMAA result (the working copy), the histories and distances.
    made = made
           && texture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr,
                      &objects.work_uav, &objects.work_srv);
    for (size_t index = 0; made && index < 2; ++index) {
      made = texture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr,
                     &objects.history_uavs[index], &objects.history_srvs[index])
             && texture(width, height, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, nullptr, &objects.distance_uavs[index],
                        &objects.distance_srvs[index]);
    }
  } else if (method == Method::CMAA2) {
    made = made
           && texture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr,
                      &objects.work_uav, &objects.work_srv)
           && texture(half_width, height, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_UINT, nullptr, &storage[0], nullptr)
           && buffer(pixels / 4, 4, false, 0, nullptr, &storage[1], nullptr)
           && buffer((pixels + 3) / 6, 4, false, 0, nullptr, &storage[2], nullptr)
           && buffer(pixels / 2, 8, false, 0, nullptr, &storage[3], nullptr)
           && texture(half_width, (height + 1) / 2, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_UINT, nullptr, &storage[4],
                      nullptr)
           && buffer(16, 4, true, 0, ZEROS.data(), &storage[5], nullptr)
           && buffer(4, 4, true, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, nullptr, &storage[6], &objects.cmaa2_arguments);
  }
  if (!made) {
    renodx::utils::log::w("[Total War] Anti-aliasing: could not create its textures (", width, " x ", height, ").");
    return false;
  }
  objects.width = width;
  objects.height = height;
  objects.format = format;
  objects.method = method;
  return true;
}

// The scene's depth copied (`source`: the main view's depth-stencil buffer) for TAA / SMAA T2x; null: unusable.
inline ID3D11ShaderResourceView* CopyDepth(ID3D11Device* device, ID3D11DeviceContext* native, ID3D11Texture2D* source) {
  auto& objects = runtime.objects;
  D3D11_TEXTURE2D_DESC desc = {};
  source->GetDesc(&desc);
  if (desc.Width != objects.width || desc.Height != objects.height) return nullptr;
  if (objects.depth_format != desc.Format) {
    objects.depth.Reset();
    objects.depth_srv.Reset();
    objects.depth_format = desc.Format;
    const auto [typeless, view_format] = frame_history::DepthFormats(desc.Format);
    D3D11_TEXTURE2D_DESC copy_desc = desc;
    copy_desc.Format = typeless;
    copy_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copy_desc.CPUAccessFlags = 0;
    copy_desc.MiscFlags = 0;
    copy_desc.Usage = D3D11_USAGE_DEFAULT;
    const D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {
        .Format = view_format,
        .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1},
    };
    const bool made = (typeless != DXGI_FORMAT_UNKNOWN)
                      && SUCCEEDED(device->CreateTexture2D(&copy_desc, nullptr, objects.depth.GetAddressOf()))
                      && SUCCEEDED(device->CreateShaderResourceView(objects.depth.Get(), &view_desc, objects.depth_srv.GetAddressOf()));
    renodx::utils::log::i("[Total War] Anti-aliasing: the scene's depth (format ", static_cast<int>(desc.Format), ") ",
                          (made ? "copied for TAA." : "unusable."));
    if (!made) {
      objects.depth.Reset();
      objects.depth_srv.Reset();
    }
  }
  if (!objects.depth_srv) return nullptr;
  native->CopyResource(objects.depth.Get(), source);
  return objects.depth_srv.Get();
}

// SMAA, CMAA2, SMAA T2x, TAA or DLAA / DLSS on `target` (the tone mapper's; `scaled`: DLSS, its top-left render size
// holds the frame), the compute stage's bindings put back after.
inline void Apply(ID3D11DeviceContext* native, ID3D11Texture2D* target, bool scaled) {
  auto& objects = runtime.objects;
  const int quality = std::clamp(runtime.settings.quality, 0, 3);
  const bool sharp = runtime.settings.extra_sharpness;
  const Method method = runtime.settings.method;
  const bool t2x = (method == Method::SMAA_T2X);
  // SMAA T2x's sub-sample this frame (its jitter: Update).
  std::array<float, 4> subsample_indices = {};
  if (t2x) {
    subsample_indices = ((runtime.frame & 1) == 0) ? std::array<float, 4>{1.f, 1.f, 1.f, 0.f}
                                                   : std::array<float, 4>{2.f, 2.f, 2.f, 0.f};
  }
  // TAA / SMAA T2x / DLAA need this frame's camera and depth (once a frame); the history only right after a frame they
  // ran.
  ID3D11ShaderResourceView* depth_view = nullptr;
  bool continued = false;
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (method == Method::SMAA_T2X || method == Method::TAA || method == Method::DLAA) {
    if (runtime.camera_frame != runtime.frame || !runtime.scene_depth || runtime.applied_frame == runtime.frame) return;
    depth_view = CopyDepth(device.Get(), native, runtime.scene_depth.Get());
    if (depth_view == nullptr) return;
    if (method == Method::DLAA
        && !dlaa_bridge::Prepare(device.Get(), objects.width, objects.height,
                                 runtime.settings.ray_reconstruction || runtime.settings.ray_traced_guides, runtime.settings.ray_tracing,
                                 frame_generation::Format(), runtime.settings.ray_traced_reflections)) {
      return;
    }
    continued = (objects.history_valid || method == Method::DLAA) && runtime.applied_frame + 1 == runtime.frame
                && runtime.previous_camera.valid;
    const TaaConstants taa = {
        .view_projection = runtime.camera.view_projection,
        .inverse_view_projection = runtime.camera.inverse_view_projection,
        .previous_view_projection = (continued ? runtime.previous_camera : runtime.camera).view_projection,
        .projection_z = runtime.camera.projection_z,
        .history_weight = (t2x ? 0.5f : std::clamp(runtime.settings.history_weight, 0.f, 0.98f)),
        .reset = (continued ? 0.f : 1.f),
        .sharpening = runtime.settings.sharpening,
        // The camera's jitter (y up) as it moves the picture (y down).
        .jitter = {runtime.jitter[0], -runtime.jitter[1]},
    };
    native->UpdateSubresource(objects.taa_constants.Get(), 0, nullptr, &taa, 0, 0);
  }
  // DLAA's input: DLSS, the render size.
  const UINT input_width = (scaled ? runtime.render_width : objects.width);
  const UINT input_height = (scaled ? runtime.render_height : objects.height);
  // Presets L and M (always auto-exposed) flatten dark tones given linear colour (ATN debug11: shadows one flat grey with
  // coloured fringes): they get its square root, as the tone mapper's own encoding (Ray Reconstruction linear colour).
  const bool square_root = method == Method::DLAA && (runtime.settings.dlaa_preset == 12u || runtime.settings.dlaa_preset == 13u)
                           && !(runtime.settings.ray_reconstruction && objects.guides_frame == runtime.frame);
  const ShaderSettings settings = {
      .metrics = {1.f / static_cast<float>(objects.width), 1.f / static_cast<float>(objects.height),
                  static_cast<float>(objects.width), static_cast<float>(objects.height)},
      .smaa_threshold = SMAA_PRESETS[quality][0],
      .smaa_search_steps = SMAA_PRESETS[quality][1],
      .smaa_search_steps_diag = SMAA_PRESETS[quality][2],
      .smaa_corner_rounding = SMAA_PRESETS[quality][3],
      .cmaa2_edge_threshold = CMAA2_THRESHOLDS[quality],
      .cmaa2_local_contrast = (sharp ? 0.15f : 0.1f),
      .cmaa2_simple_shape_blur = (sharp ? 0.07f : 0.1f),
      .smaa_subsample_indices = subsample_indices,
      .dlaa_debug = {static_cast<float>(runtime.settings.debug_view), static_cast<float>(input_width),
                     static_cast<float>(input_height), (square_root ? 1.f : 0.f)},
  };
  if (std::memcmp(&settings, &objects.uploaded, sizeof(ShaderSettings)) != 0) {
    native->UpdateSubresource(objects.settings.Get(), 0, nullptr, &settings, 0, 0);
    objects.uploaded = settings;
  }

  ComPtr<ID3D11ComputeShader> game_shader;
  native->CSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
  std::array<ID3D11ShaderResourceView*, SRV_COUNT> game_views = {};
  native->CSGetShaderResources(0, SRV_COUNT, game_views.data());
  std::array<ID3D11UnorderedAccessView*, UAV_COUNT> game_uavs = {};
  native->CSGetUnorderedAccessViews(0, UAV_COUNT, game_uavs.data());
  std::array<ID3D11Buffer*, 2> game_constants = {};
  native->CSGetConstantBuffers(0, 2, game_constants.data());
  std::array<ID3D11SamplerState*, 2> game_samplers = {};
  native->CSGetSamplers(0, 2, game_samplers.data());

  constexpr std::array<UINT, UAV_COUNT> KEEP_COUNTS = {UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX,
                                                       UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX};
  constexpr std::array<ID3D11ShaderResourceView*, SRV_COUNT> NO_VIEWS = {};
  // The views off first, then the unordered access views, then the views: D3D11 drops a binding whose resource is still
  // bound the other way.
  const auto bind = [&](ID3D11ComputeShader* shader, std::array<ID3D11ShaderResourceView*, SRV_COUNT> views,
                        std::array<ID3D11UnorderedAccessView*, UAV_COUNT> uavs) {
    native->CSSetShader(shader, nullptr, 0);
    native->CSSetShaderResources(0, SRV_COUNT, NO_VIEWS.data());
    native->CSSetUnorderedAccessViews(0, UAV_COUNT, uavs.data(), KEEP_COUNTS.data());
    native->CSSetShaderResources(0, SRV_COUNT, views.data());
  };
  const UINT groups_x = (objects.width + 7) / 8;
  const UINT groups_y = (objects.height + 7) / 8;
  const std::array<ID3D11Buffer*, 2> constants = {objects.settings.Get(), objects.taa_constants.Get()};
  native->CSSetConstantBuffers(0, 2, constants.data());
  const std::array<ID3D11SamplerState*, 2> samplers = {objects.linear_clamp.Get(), objects.point_clamp.Get()};
  native->CSSetSamplers(0, 2, samplers.data());
  // The frame: DLAA reads the tone mapper's output itself where it can be read (its render targets off meanwhile: D3D11
  // drops a view of a texture still bound for output), the rest a copy (a 4K copy less: ~0.1 ms).
  ID3D11ShaderResourceView* frame_view = objects.color_srv.Get();
  std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_targets = {};
  ComPtr<ID3D11DepthStencilView> game_depth_target;
  bool targets_off = false;
  if (method == Method::DLAA) {
    if (objects.frame_texture.Get() != target) {
      objects.frame_texture = target;
      objects.frame_srv.Reset();
      D3D11_TEXTURE2D_DESC target_desc = {};
      target->GetDesc(&target_desc);
      const D3D11_SHADER_RESOURCE_VIEW_DESC frame_desc = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                          .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                          .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1}};
      if ((target_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0) {
        device->CreateShaderResourceView(target, &frame_desc, objects.frame_srv.GetAddressOf());
      }
    }
    if (objects.frame_srv) {
      native->OMGetRenderTargets(static_cast<UINT>(game_targets.size()), game_targets.data(), game_depth_target.GetAddressOf());
      native->OMSetRenderTargets(0, nullptr, nullptr);
      targets_off = true;
      frame_view = objects.frame_srv.Get();
    }
  }
  if (!targets_off) {
    native->CopyResource(objects.color.Get(), target);
  }

  if (method == Method::SMAA || t2x) {
    bind(objects.smaa_edges.Get(), {objects.color_srv.Get()}, {objects.smaa_edges_uav.Get()});
    native->Dispatch(groups_x, groups_y, 1);
    bind(objects.smaa_weights.Get(), {objects.smaa_edges_srv.Get(), objects.area_srv.Get(), objects.search_srv.Get()},
         {objects.smaa_weights_uav.Get()});
    native->Dispatch(groups_x, groups_y, 1);
    // SMAA T2x: into the working copy, then blended with the last frame below.
    ID3D11UnorderedAccessView* blended = (t2x ? objects.work_uav : objects.result_uav).Get();
    bind(objects.smaa_blend.Get(), {objects.color_srv.Get(), objects.smaa_weights_srv.Get()}, {blended});
    native->Dispatch(groups_x, groups_y, 1);
  }
  if (method == Method::SMAA_T2X || method == Method::TAA) {
    // This frame (SMAA T2x: SMAA'd) with the history into this frame's history, sharpened into the result.
    const size_t current = objects.history_index;
    const size_t last = 1 - current;
    ID3D11ShaderResourceView* frame = (t2x ? objects.work_srv : objects.color_srv).Get();
    bind(objects.taa_resolve.Get(),
         {frame, objects.history_srvs[last].Get(), depth_view, objects.distance_srvs[last].Get()},
         {objects.history_uavs[current].Get(), objects.distance_uavs[current].Get()});
    native->Dispatch(groups_x, groups_y, 1);
    bind(objects.taa_sharpen.Get(), {objects.history_srvs[current].Get()}, {objects.result_uav.Get()});
    native->Dispatch(groups_x, groups_y, 1);
    objects.history_index = last;
    objects.history_valid = true;
    runtime.applied_frame = runtime.frame;
  } else if (method == Method::CMAA2) {
    // CMAA2's passes as its DX11 sample's: the edges and candidates, the indirect arguments, the shapes, the arguments
    // again, the deferred colours into the working copy (now written, no longer read).
    std::array<ID3D11UnorderedAccessView*, UAV_COUNT> storage = {};
    for (size_t index = 0; index < objects.cmaa2_storage.size(); ++index) {
      storage[index + 1] = objects.cmaa2_storage[index].Get();
    }
    bind(objects.cmaa2_prepare.Get(), {objects.color_srv.Get()}, {objects.work_uav.Get()});
    native->Dispatch(groups_x, groups_y, 1);
    bind(objects.cmaa2_edges.Get(), {objects.work_srv.Get()}, storage);
    native->Dispatch((objects.width + 27) / 28, (objects.height + 27) / 28, 1);
    native->CSSetShader(objects.cmaa2_args.Get(), nullptr, 0);
    native->Dispatch(2, 1, 1);
    native->CSSetShader(objects.cmaa2_candidates.Get(), nullptr, 0);
    native->DispatchIndirect(objects.cmaa2_arguments.Get(), 0);
    native->CSSetShader(objects.cmaa2_args.Get(), nullptr, 0);
    native->Dispatch(1, 2, 1);
    storage[0] = objects.work_uav.Get();
    bind(objects.cmaa2_apply.Get(), {}, storage);
    native->DispatchIndirect(objects.cmaa2_arguments.Get(), 0);
    bind(objects.cmaa2_resolve.Get(), {objects.work_srv.Get(), objects.color_srv.Get()}, {objects.result_uav.Get()});
    native->Dispatch(groups_x, groups_y, 1);
  }
  bool resolved = true;
  std::optional<frame_generation::Inputs> generation;
  if (method == Method::DLAA) {
    // NVIDIA's inputs into the shared textures (DLSS: the render size's top-left, its metrics for the preparation), its
    // DLAA / DLSS in the helper (the GPU waits for it), back into the result; no result (helper not ready or gone): the
    // frame as it is.
    if (scaled) {
      ShaderSettings input_settings = settings;
      input_settings.metrics = {1.f / static_cast<float>(input_width), 1.f / static_cast<float>(input_height),
                                static_cast<float>(input_width), static_cast<float>(input_height)};
      native->UpdateSubresource(objects.settings.Get(), 0, nullptr, &input_settings, 0, 0);
      objects.uploaded = input_settings;
    }
    // Rigid objects: which meshes move (object_motion.hpp).
    object_motion::Resolve(native, runtime.frame);
    const auto object_motion_views = object_motion::Target(runtime.frame);
    dlaa_bridge::BeforeSharedWrites(native);
    bind(objects.dlaa_prepare.Get(),
         {frame_view, depth_view, object_motion_views.redrawn.Get(), object_motion_views.drawn_motion.Get(),
          object_motion_views.drawn_depth.Get()},
         {dlaa_bridge::bridge.color.uav.Get(), dlaa_bridge::bridge.depth.uav.Get(), dlaa_bridge::bridge.motion.uav.Get()});
    native->Dispatch((input_width + 7) / 8, (input_height + 7) / 8, 1);
    bind(nullptr, {}, {});
    if (scaled) {
      native->UpdateSubresource(objects.settings.Get(), 0, nullptr, &settings, 0, 0);
      objects.uploaded = settings;
    }
    // Ray Reconstruction (its guides written this frame): its camera, view = inverse projection x view-projection (the
    // registers' matrices act on column vectors), given NVIDIA as row-vector matrices (their transposes).
    dlaa_bridge::RayReconstruction reconstruction = {.preset = runtime.settings.ray_reconstruction_preset};
    const bool reconstruct = runtime.settings.ray_reconstruction && objects.guides_frame == runtime.frame;
    if (reconstruct) {
      const Camera& camera = runtime.camera;
      for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
          float sum = 0.f;
          for (size_t k = 0; k < 4; ++k) sum += camera.inverse_projection[row * 4 + k] * camera.view_projection[k * 4 + column];
          reconstruction.world_to_view[column * 4 + row] = sum;
          reconstruction.view_to_clip[column * 4 + row] = camera.projection[row * 4 + column];
        }
      }
    }
    // The camera's jitter (y up) as it moves the picture (y down), as TAA's.
    resolved = dlaa_bridge::Evaluate(native, input_width, input_height, runtime.jitter[0], -runtime.jitter[1], !continued,
                                     runtime.settings.dlaa_preset, reconstruct ? &reconstruction : nullptr);
    if (resolved) {
      bind(objects.dlaa_finish.Get(),
           {dlaa_bridge::bridge.output.srv.Get(), frame_view, dlaa_bridge::bridge.motion.srv.Get(),
            object_motion_views.redrawn.Get(), depth_view, object_motion_views.drawn_motion.Get(),
            object_motion_views.drawn_depth.Get()},
           {objects.result_uav.Get()});
      native->Dispatch(groups_x, groups_y, 1);
    }
    // Frame generation: the frame's DLAA inputs and camera (row vectors: the registers' matrices transposed; view =
    // inverse projection x view-projection, its rows the camera's axes; near and far from the projection's z row).
    if (resolved && frame_generation::Wanted()) {
      const Camera& camera = runtime.camera;
      const Camera& previous = (continued ? runtime.previous_camera : runtime.camera);
      frame_generation::Inputs inputs = {
          .width = input_width,
          .height = input_height,
          .jitter_x = runtime.jitter[0],
          .jitter_y = -runtime.jitter[1],
          .reset = !continued,
      };
      std::array<float, 16> view = {};
      for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
          for (size_t k = 0; k < 4; ++k) view[row * 4 + column] += camera.inverse_projection[row * 4 + k] * camera.view_projection[k * 4 + column];
          inputs.view_to_clip[column * 4 + row] = camera.projection[row * 4 + column];
          inputs.view_projection[column * 4 + row] = camera.view_projection[row * 4 + column];
          inputs.previous_view_projection[column * 4 + row] = previous.view_projection[row * 4 + column];
        }
      }
      inputs.camera = {camera.position[0], camera.position[1], camera.position[2], view[0], view[1], view[2],
                       view[4],            view[5],            view[6],            view[8], view[9], view[10]};
      const float depth_scale = camera.projection[10];
      const float depth_offset = camera.projection[11];
      inputs.near_plane = (depth_scale != 0.f ? -depth_offset / depth_scale : 0.1f);
      inputs.far_plane = (depth_scale != 1.f ? depth_scale * inputs.near_plane / (depth_scale - 1.f) : 100000.f);
      generation = inputs;
    }
    runtime.applied_frame = runtime.frame;
  }
  bind(nullptr, {}, {});
  if (resolved) native->CopyResource(target, objects.result.Get());
  if (generation) {
    frame_generation::KeepFrame(native, target, *generation);
  }
  if (targets_off) {
    native->OMSetRenderTargets(static_cast<UINT>(game_targets.size()), game_targets.data(), game_depth_target.Get());
    for (auto* game_target : game_targets) {
      if (game_target != nullptr) game_target->Release();
    }
  }

  native->CSSetShader(game_shader.Get(), nullptr, 0);
  native->CSSetUnorderedAccessViews(0, UAV_COUNT, game_uavs.data(), KEEP_COUNTS.data());
  native->CSSetShaderResources(0, SRV_COUNT, game_views.data());
  native->CSSetConstantBuffers(0, 2, game_constants.data());
  native->CSSetSamplers(0, 2, game_samplers.data());
  for (auto* view : game_views) {
    if (view != nullptr) view->Release();
  }
  for (auto* uav : game_uavs) {
    if (uav != nullptr) uav->Release();
  }
  for (auto* buffer : game_constants) {
    if (buffer != nullptr) buffer->Release();
  }
  for (auto* sampler : game_samplers) {
    if (sampler != nullptr) sampler->Release();
  }
}

// Object motion's view of a draw with `camera`: DLAA ran last frame, `camera` holds the main view's camera uploaded
// this frame, and last frame's is known (the sun light draws': this frame's once they ran).
inline bool MotionView(ID3D11Buffer* camera, object_motion::View* view) {
  const std::shared_lock lock(runtime.mutex);
  if (runtime.settings.method != Method::DLAA || runtime.applied_frame + 1 != runtime.frame) return false;
  const auto upload = std::ranges::find(runtime.uploads, camera, &Upload::buffer);
  const Camera& previous = (runtime.camera_frame == runtime.frame ? runtime.previous_camera : runtime.camera);
  if (upload == runtime.uploads.end() || !upload->camera.valid || !previous.valid) return false;
  *view = {
      .frame = runtime.frame,
      .width = runtime.display_width,
      .height = runtime.display_height,
      .constants = {.current = upload->camera.view_projection,
                    .previous = previous.view_projection,
                    .times = {previous.time, upload->camera.time, static_cast<float>(runtime.render_width),
                              static_cast<float>(runtime.render_height)},
                    .game_jitter = {upload->camera.jitter_clip[0], upload->camera.jitter_clip[1], 0.f, 0.f}},
  };
  return true;
}

}  // namespace internal

// The tone mapper drew (its replaced shaders' on_drawn): its target anti-aliased when it is the display's size.
inline void OnTonemapDrawn(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (runtime.settings.method == Method::OFF) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  ComPtr<ID3D11RenderTargetView> target_view;
  native->OMGetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  if (!target_view) return;
  ComPtr<ID3D11Resource> resource;
  target_view->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> target;
  if (FAILED(resource.As(&target))) return;
  D3D11_TEXTURE2D_DESC desc = {};
  target->GetDesc(&desc);
  // Tone mapper draws into other targets (portholes, the campaign's small views, Rome II's R11G11B10 pass before the
  // main one) are not the main view's.
  if (desc.Width != runtime.display_width || desc.Height != runtime.display_height || desc.SampleDesc.Count != 1
      || desc.ArraySize != 1) {
    return;
  }
  if (desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS) {
    if (!runtime.logged_unusable) {
      renodx::utils::log::i("[Total War] Anti-aliasing: a tone mapper draw into format ", static_cast<int>(desc.Format),
                            " (not RGBA16) skipped.");
      runtime.logged_unusable = true;
    }
    return;
  }
  runtime.tonemapped_frame = runtime.frame;
  frame_history::tonemapped.store(true, std::memory_order_relaxed);
  // DLSS: the 3D span ends with the tone mapper (the UI after it at the display's size).
  const bool scaled = internal::scaling.exchange(false);
  internal::main_view.store(false, std::memory_order_relaxed);
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!internal::PrepareDevice(device.Get())) return;
  if (!internal::PrepareSize(device.Get(), desc.Width, desc.Height, desc.Format, runtime.settings.method)) return;
  frame_trace::Mark(scaled ? "tone mapper drawn: DLSS (render scale) after it" : "tone mapper drawn: DLAA after it");
  {
    const debug_timing::Timed timed{debug_timing::ANTI_ALIASING};
    internal::Apply(native, target.Get(), scaled);
  }
  if (scaled) {
    // The game's viewports and scissor rectangles back if the scaled ones are still bound.
    std::array<D3D11_VIEWPORT, 16> current = {};
    UINT count = static_cast<UINT>(current.size());
    native->RSGetViewports(&count, current.data());
    {
      const std::unique_lock viewport_lock(internal::bound_viewports.mutex);
      const auto& bound = internal::bound_viewports;
      bool still_scaled = count == bound.count && count != 0;
      for (UINT index = 0; index < count && still_scaled; ++index) {
        const auto& viewport = bound.scaled[index];
        still_scaled = current[index].TopLeftX == viewport.x && current[index].TopLeftY == viewport.y
                       && current[index].Width == viewport.width && current[index].Height == viewport.height;
      }
      if (still_scaled) {
        std::array<D3D11_VIEWPORT, 16> original = {};
        for (UINT index = 0; index < count; ++index) {
          const auto& viewport = bound.original[index];
          original[index] = {viewport.x, viewport.y, viewport.width, viewport.height, viewport.min_depth, viewport.max_depth};
        }
        native->RSSetViewports(count, original.data());
      }
    }
    std::array<D3D11_RECT, 16> rects = {};
    UINT rect_count = static_cast<UINT>(rects.size());
    native->RSGetScissorRects(&rect_count, rects.data());
    const std::unique_lock scissor_lock(internal::bound_scissors.mutex);
    const auto& bound = internal::bound_scissors;
    bool still_scaled = rect_count == bound.count && rect_count != 0;
    for (UINT index = 0; index < rect_count && still_scaled; ++index) {
      const auto& rect = bound.scaled[index];
      still_scaled = rects[index].left == rect.left && rects[index].top == rect.top && rects[index].right == rect.right
                     && rects[index].bottom == rect.bottom;
    }
    if (still_scaled) {
      for (UINT index = 0; index < rect_count; ++index) {
        const auto& rect = bound.original[index];
        rects[index] = {rect.left, rect.top, rect.right, rect.bottom};
      }
      native->RSSetScissorRects(rect_count, rects.data());
    }
  }
  frame_trace::Mark("anti-aliasing done");
}

// Every present: the settings, the display's size, a new frame and its jitter (TAA: Halton (2, 3) over 8 frames, centred;
// SMAA T2x: its two sub-samples) while they run (none after a frame without).
inline void Update(reshade::api::swapchain* swapchain, const Settings& settings) {
  reshade::api::resource_desc display = {};
  if (settings.method != Method::OFF) {
    display = swapchain->get_device()->get_resource_desc(swapchain->get_current_back_buffer());
  }
  const bool temporal =
      (settings.method == Method::SMAA_T2X || settings.method == Method::TAA || settings.method == Method::DLAA);
  internal::temporal.store(temporal, std::memory_order_relaxed);
  object_motion::SetActive(settings.method == Method::DLAA && settings.object_motion);
  object_motion::SetTest(settings.motion_test);
  frame_generation::Update({
      .multiplier = (settings.method == Method::DLAA ? settings.frame_generation : 0u),
      .reflex_boost = settings.reflex_boost,
      .frame_limit = settings.frame_limit,
      .preset = settings.frame_preset,
      .hud_recomposition = settings.hud_recomposition,
  });
  const std::unique_lock lock(internal::runtime.mutex);
  auto& runtime = internal::runtime;
  runtime.settings = settings;
  runtime.display_width = display.texture.width;
  runtime.display_height = display.texture.height;
  frame_trace::OnPresent(runtime.render_width != 0 && runtime.render_width < runtime.display_width);
  ++runtime.frame;
  frame_history::tonemapped.store(false, std::memory_order_relaxed);
  runtime.scene_depth.Reset();
  const bool ran = (runtime.applied_frame + 1 == runtime.frame);
  // DLSS: below 100% once DLAA ran last frame (a frame without it, a menu, ends the scaling), at the display's exact
  // aspect (whole aspect units).
  const float render_scale = std::clamp(settings.render_scale, 0.01f, 1.f);
  const bool scaled = settings.method == Method::DLAA && render_scale < 0.999f && ran && display.texture.width != 0;
  const auto render = (scaled ? frame_history::AspectSize(display.texture.width, display.texture.height, render_scale)
                              : std::array<uint32_t, 2>{display.texture.width, display.texture.height});
  runtime.render_width = render[0];
  runtime.render_height = render[1];
  internal::scaling.store(scaled);
  {
    // The patched shaders' render scale (view_share.hpp): render / display.
    auto* device = reinterpret_cast<ID3D11Device*>(swapchain->get_device()->get_native());
    renodx::utils::directx::NativeFromReShadeProxy(&device);
    ComPtr<ID3D11DeviceContext> context;
    if (device != nullptr) device->GetImmediateContext(context.GetAddressOf());
    view_share::Update(context.Get(), scaled, render, {display.texture.width, display.texture.height});
  }
  frame_history::render_width.store(scaled ? runtime.render_width : 0u);
  frame_history::render_height.store(scaled ? runtime.render_height : 0u);
  if ((settings.method == Method::TAA || settings.method == Method::DLAA) && ran) {
    // NVIDIA: 8 phases at native size, 8 / scale^2 below it.
    const uint32_t phases = (scaled ? std::max(internal::TAA_PHASES, static_cast<uint32_t>(std::ceil(8.f / (render_scale * render_scale))))
                                    : internal::TAA_PHASES);
    const auto phase = static_cast<uint32_t>(runtime.frame % phases) + 1;
    runtime.jitter = {internal::Halton(phase, 2) - 0.5f, internal::Halton(phase, 3) - 0.5f};
  } else if (settings.method == Method::SMAA_T2X && ran) {
    runtime.jitter = ((runtime.frame & 1) == 0) ? std::array<float, 2>{0.25f, -0.25f} : std::array<float, 2>{-0.25f, 0.25f};
  } else {
    runtime.jitter = {};
  }
  if (!temporal) runtime.uploads.clear();
}

// The camera uploads watched and the sun light draws' depth noted (`draws`: the addon's, kept while installed); the
// moving draws drawn again for DLAA's motion (`motion`: the addon's, kept while installed); the game's passes that read
// render-size textures at the display's size below 100% (`region_reads`) and the vertex shaders the addon replaces with
// ones that read the render share (`vertex_shares`).
inline void Install(std::span<const light_bounce::SunDraw> draws, const ObjectMotion& motion = {},
                    std::span<const view_share::RegionRead> region_reads = {}, std::span<const uint32_t> vertex_shares = {}) {
  frame_trace::Install();
  view_share::Install(region_reads, vertex_shares);
  reshade::register_event<reshade::addon_event::map_buffer_region>(internal::OnMapBufferRegion);
  reshade::register_event<reshade::addon_event::unmap_buffer_region>(internal::OnUnmapBufferRegion);
  reshade::register_event<reshade::addon_event::update_buffer_region>(internal::OnUpdateBufferRegion);
  reshade::register_event<reshade::addon_event::bind_viewports>(internal::OnBindViewports);
  reshade::register_event<reshade::addon_event::bind_scissor_rects>(internal::OnBindScissorRects);
  // The texture LOD bias below 100% (after the viewport scaling: it knows the main view).
  texture_lod_bias::SetMainView([] { return internal::main_view.load(std::memory_order_relaxed); });
  reshade::register_event<reshade::addon_event::bind_viewports>(texture_lod_bias::OnBindViewports);
  reshade::register_event<reshade::addon_event::init_sampler>(texture_lod_bias::OnInitSampler);
  reshade::register_event<reshade::addon_event::destroy_sampler>(texture_lod_bias::OnDestroySampler);
  reshade::register_event<reshade::addon_event::push_descriptors>(texture_lod_bias::OnPushDescriptors);
  reshade::register_event<reshade::addon_event::present>(internal::OnPresent);
  for (const auto& draw : draws) {
    renodx::utils::command_action::Register(
        internal::OnSunDraw,
        {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
  }
  object_motion::Install(motion, internal::MotionView);
}

inline void Uninstall() {
  view_share::Uninstall();
  reshade::unregister_event<reshade::addon_event::map_buffer_region>(internal::OnMapBufferRegion);
  reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(internal::OnUnmapBufferRegion);
  reshade::unregister_event<reshade::addon_event::update_buffer_region>(internal::OnUpdateBufferRegion);
  reshade::unregister_event<reshade::addon_event::bind_viewports>(internal::OnBindViewports);
  reshade::unregister_event<reshade::addon_event::bind_scissor_rects>(internal::OnBindScissorRects);
  reshade::unregister_event<reshade::addon_event::bind_viewports>(texture_lod_bias::OnBindViewports);
  reshade::unregister_event<reshade::addon_event::init_sampler>(texture_lod_bias::OnInitSampler);
  reshade::unregister_event<reshade::addon_event::destroy_sampler>(texture_lod_bias::OnDestroySampler);
  reshade::unregister_event<reshade::addon_event::push_descriptors>(texture_lod_bias::OnPushDescriptors);
  reshade::unregister_event<reshade::addon_event::present>(internal::OnPresent);
  renodx::utils::command_action::Unregister(internal::OnSunDraw);
  object_motion::Uninstall();
  internal::scaling.store(false);
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.scene_depth.Reset();
}

// The main view's camera last uploaded in `buffer` (DLAA, TAA, SMAA T2x: the uploads are watched); false: it holds none.
inline bool MainCamera(ID3D11Buffer* buffer, internal::Camera* camera) {
  const std::unique_lock lock(internal::runtime.mutex);
  const auto upload = std::ranges::find(internal::runtime.uploads, buffer, &internal::Upload::buffer);
  if (upload == internal::runtime.uploads.end() || !upload->camera.valid) return false;
  *camera = upload->camera;
  return true;
}

// Whether the G-buffer guides (Ray Reconstruction's, also ray tracing's paths') were written this frame.
inline bool GuidesWritten() {
  const std::unique_lock lock(internal::runtime.mutex);
  return internal::runtime.objects.guides_frame == internal::runtime.frame;
}

// DLAA's render size this frame (DLSS: below the display's).
inline std::array<UINT, 2> RenderSize() {
  const std::unique_lock lock(internal::runtime.mutex);
  return {internal::runtime.render_width, internal::runtime.render_height};
}

// The game's device is going away: the addon's objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  object_motion::OnDestroyDevice(native);
  frame_generation::OnDestroyDevice(native);
  view_share::OnDestroyDevice(native);
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.objects.device.Get() != native) return;
  internal::runtime.objects = {};
  dlaa_bridge::Release();
  internal::runtime.scene_depth.Reset();
  internal::runtime.uploads.clear();
}

}  // namespace twattila::anti_aliasing
