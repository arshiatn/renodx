/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

#include <embed/shaders.h>
#include <include/reshade.hpp>
#include <nvsdk_ngx_helpers.h>

#include "../../utils/directx.hpp"

#include "../twattila/frame_history.hpp"
#include "../twattila/object_motion.hpp"

// Ships, sails, ropes and water for DLSS's motion vectors. The game writes its own velocity (the G-buffer's rt3,
// R16G16_FLOAT pixels: gbuffer.hlsl calc_motion_vector) only for units, cloth, grass, leaves, branches, imposters and
// wind-blended rigid models (TAA_VELOCITY_PIXEL); the rest get the camera's from the depth in its motion vectors pass
// (motion_vectors.hlsl), and water drawn after that pass keeps what is under it. Attila's object motion
// (twattila/object_motion.hpp) draws the moving ones again: ships and the other rigid models (rigidstandard,
// rigidvertexwind: their instance rows, carried to the next frame by its placement table), sails, sail symbols and ropes
// (vertices the CPU moves: last frame's captured), the ocean and the water plane (the camera's motion at their own
// depth); its constants are copied from the draw's camera on the GPU (it has last frame's view-projection and time, and
// both jitters). Before DLSS runs (game_dlss's evaluation), velocity_merge.cs puts that motion, in the game's
// convention, where it is the final surface into a copy of the game's motion vectors, which DLSS then gets.
namespace twshogun2::object_velocity {

using Microsoft::WRL::ComPtr;
using twattila::object_motion::MotionConstants;
using twattila::object_motion::MotionKind;

namespace internal {

// The game's vertex shaders whose draws move on their own (tools\tw\attila_motion\port_motion.py, MOTION_DUMP=
// dump_shader_shogun2_pack).
constexpr std::array<twattila::object_motion::MotionShader, 7> MOTION_SHADERS = {{
    {.hash = 0x41913E64, .motion_shader = __motion_rigid_standard, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_standard},
    {.hash = 0x0798A33F, .motion_shader = __motion_rigid_vertex_wind, .kind = MotionKind::RIGID, .record_shader = __motion_record_rigid_vertex_wind},
    {.hash = 0xD5EA871E, .motion_shader = __motion_sail, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail},
    {.hash = 0xB6498207, .motion_shader = __motion_sail_symbol, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_sail_symbol},
    {.hash = 0xAAEF741B, .motion_shader = __motion_rope, .kind = MotionKind::DEFORMED, .record_shader = __motion_capture_rope},
    {.hash = 0xC1BE6CEE, .motion_shader = __motion_ocean},
    {.hash = 0xC883761A, .motion_shader = __motion_waterplane},
}};

// camera_VS_PS (camera.hlsl) into the motion constants: view_projection c10-c13 and prev_view_projection c14-c17 (the
// cameras, jittered), prev_time_in_sec c35.y, g_viewport_dimensions c38.xy (the render size), g_camera_jitter c42.zw
// and g_prev_camera_jitter c43.xy.
constexpr std::array<twattila::frame_history::CameraRun, 4> CAMERA_COPIES = {{
    {.destination = offsetof(MotionConstants, current), .source = 10 * 16, .bytes = 128},
    {.destination = offsetof(MotionConstants, times), .source = 35 * 16 + 4, .bytes = 4},
    {.destination = offsetof(MotionConstants, times) + 8, .source = 38 * 16, .bytes = 8},
    {.destination = offsetof(MotionConstants, jitter), .source = 42 * 16 + 8, .bytes = 16},
}};

// A game texture read through a view of the addon's (kept while the texture is the same).
struct ReadView {
  ComPtr<ID3D11Resource> resource;
  ComPtr<ID3D11ShaderResourceView> srv;
};

// The merge, its views of the game's motion vectors and depth, and the motion vectors DLSS gets instead.
struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11ComputeShader> merge;
  ReadView game_velocity;
  ReadView game_depth;
  ComPtr<ID3D11Texture2D> velocity;
  ComPtr<ID3D11UnorderedAccessView> velocity_uav;
};

struct State {
  std::shared_mutex mutex;
  uint64_t frame = 0;
  // The depth DLSS got last (the G-buffer's: the main view's) and its size.
  ComPtr<ID3D11Resource> depth;
  UINT width = 0;
  UINT height = 0;
  DeviceObjects objects;
};

static State state;
static std::atomic_bool enabled = true;

// Object motion's view of a draw: against the depth DLSS gets, with the draw's camera.
inline bool MotionView(ID3D11Buffer*, twattila::object_motion::View* view) {
  const std::shared_lock lock(state.mutex);
  if (!state.depth) return false;
  *view = {
      .frame = state.frame,
      .width = state.width,
      .height = state.height,
      .depth = state.depth.Get(),
      .camera_copies = CAMERA_COPIES,
  };
  return true;
}

// A view of `resource` in `format` (UNKNOWN: its own) through `*view`, made again when the resource changes.
inline ID3D11ShaderResourceView* ReadThrough(ID3D11Device* device, ID3D11Resource* resource, DXGI_FORMAT format, ReadView* view) {
  if (view->resource.Get() == resource) return view->srv.Get();
  *view = {.resource = resource};
  D3D11_SHADER_RESOURCE_VIEW_DESC desc = {.Format = format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D};
  desc.Texture2D.MipLevels = 1;
  if (FAILED(device->CreateShaderResourceView(resource, (format == DXGI_FORMAT_UNKNOWN ? nullptr : &desc), view->srv.GetAddressOf()))) {
    view->srv.Reset();
  }
  return view->srv.Get();
}

}  // namespace internal

// Present: the next frame, its moving draws drawn again while the game's DLSS runs.
inline void OnPresent(bool dlss_running) {
  {
    const std::unique_lock lock(internal::state.mutex);
    ++internal::state.frame;
  }
  twattila::object_motion::SetActive(dlss_running && internal::enabled.load(std::memory_order_relaxed));
}

// Before the game's DLSS runs: the frame's rigid objects matched, and its object motion merged into the motion vectors
// DLSS gets (`eval`'s replaced).
inline void OnEvaluate(ID3D11DeviceContext* context, NVSDK_NGX_D3D11_DLSS_Eval_Params* eval) {
  if (!internal::enabled.load(std::memory_order_relaxed) || eval->pInDepth == nullptr || eval->pInMotionVectors == nullptr) return;
  auto& state = internal::state;
  ComPtr<ID3D11Texture2D> depth_texture;
  if (FAILED(eval->pInDepth->QueryInterface(IID_PPV_ARGS(depth_texture.GetAddressOf())))) return;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_texture->GetDesc(&depth_desc);
  uint64_t frame = 0;
  {
    const std::unique_lock lock(state.mutex);
    state.depth = eval->pInDepth;
    state.width = depth_desc.Width;
    state.height = depth_desc.Height;
    frame = state.frame;
  }
  twattila::object_motion::Resolve(context, frame);
  const auto object_motion = twattila::object_motion::Target(frame).redrawn;
  if (!object_motion) return;

  const std::unique_lock lock(state.mutex);
  auto& objects = state.objects;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (objects.device.Get() != device.Get()) {
    objects = {.device = device};
    if (FAILED(device->CreateComputeShader(__velocity_merge.data(), __velocity_merge.size(), nullptr, objects.merge.GetAddressOf()))) {
      objects.merge.Reset();
    }
  }
  ComPtr<ID3D11Texture2D> game_velocity;
  if (!objects.merge || FAILED(eval->pInMotionVectors->QueryInterface(IID_PPV_ARGS(game_velocity.GetAddressOf())))) return;
  D3D11_TEXTURE2D_DESC velocity_desc = {};
  game_velocity->GetDesc(&velocity_desc);
  if (D3D11_TEXTURE2D_DESC current = {}; objects.velocity) {
    objects.velocity->GetDesc(&current);
    if (current.Width != velocity_desc.Width || current.Height != velocity_desc.Height || current.Format != velocity_desc.Format) {
      objects.velocity.Reset();
      objects.velocity_uav.Reset();
    }
  }
  if (!objects.velocity) {
    const D3D11_TEXTURE2D_DESC desc = {
        .Width = velocity_desc.Width,
        .Height = velocity_desc.Height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = velocity_desc.Format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
    };
    if (FAILED(device->CreateTexture2D(&desc, nullptr, objects.velocity.GetAddressOf()))
        || FAILED(device->CreateUnorderedAccessView(objects.velocity.Get(), nullptr, objects.velocity_uav.GetAddressOf()))) {
      objects.velocity.Reset();
      objects.velocity_uav.Reset();
      return;
    }
  }
  const std::array<ID3D11ShaderResourceView*, 3> inputs = {
      internal::ReadThrough(device.Get(), eval->pInMotionVectors, DXGI_FORMAT_UNKNOWN, &objects.game_velocity), object_motion.Get(),
      internal::ReadThrough(device.Get(), eval->pInDepth, twattila::frame_history::DepthFormats(depth_desc.Format).second,
                            &objects.game_depth)};
  if (inputs[0] == nullptr || inputs[2] == nullptr) return;

  // The game's compute bindings it changes, put back after.
  ComPtr<ID3D11ComputeShader> game_shader;
  context->CSGetShader(game_shader.GetAddressOf(), nullptr, nullptr);
  std::array<ID3D11ShaderResourceView*, 3> game_views = {};
  context->CSGetShaderResources(0, static_cast<UINT>(game_views.size()), game_views.data());
  ID3D11UnorderedAccessView* game_uav = nullptr;
  context->CSGetUnorderedAccessViews(0, 1, &game_uav);

  ID3D11UnorderedAccessView* output = objects.velocity_uav.Get();
  context->CSSetShader(objects.merge.Get(), nullptr, 0);
  context->CSSetShaderResources(0, static_cast<UINT>(inputs.size()), inputs.data());
  context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
  const UINT width = (eval->InRenderSubrectDimensions.Width != 0 ? eval->InRenderSubrectDimensions.Width : velocity_desc.Width);
  const UINT height = (eval->InRenderSubrectDimensions.Height != 0 ? eval->InRenderSubrectDimensions.Height : velocity_desc.Height);
  context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);

  context->CSSetShader(game_shader.Get(), nullptr, 0);
  context->CSSetShaderResources(0, static_cast<UINT>(game_views.size()), game_views.data());
  context->CSSetUnorderedAccessViews(0, 1, &game_uav, nullptr);
  for (auto* view : game_views) {
    if (view != nullptr) {
      view->Release();
    }
  }
  if (game_uav != nullptr) {
    game_uav->Release();
  }
  eval->pInMotionVectors = objects.velocity.Get();
}

// Overlay / ReShade.ini: the object motion on (default) or off.
inline void SetEnabled(bool value) {
  internal::enabled.store(value, std::memory_order_relaxed);
}

inline bool Enabled() {
  return internal::enabled.load(std::memory_order_relaxed);
}

// The game's device is going away: the addon's objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  twattila::object_motion::OnDestroyDevice(native);
  const std::unique_lock lock(internal::state.mutex);
  if (internal::state.objects.device.Get() != native) return;
  internal::state.objects = {};
  internal::state.depth.Reset();
}

inline void Install() {
  twattila::object_motion::Install(
      {
          .shaders = internal::MOTION_SHADERS,
          .wind_pixel_shader = __motion_wind,
          .capture_pixel_shader = __motion_capture,
          .rigid_compute_shader = __motion_rigid,
      },
      internal::MotionView);
}

inline void Uninstall() {
  twattila::object_motion::Uninstall();
  const std::unique_lock lock(internal::state.mutex);
  internal::state.depth.Reset();
  internal::state.objects = {};
}

}  // namespace twshogun2::object_velocity
