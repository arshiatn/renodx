/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include <include/reshade.hpp>

#include "../twattila/depth_levels.hpp"

// Ambient Occlusion: Visibility Bitmask in a line-sweep AO's collect (Shogun 2, Warhammer II, Warhammer III), whose
// shader computes the AO from the depth and camera this binds into it, putting the collect's own bindings back after it
// (hooks need the passes' replacement code: hooks on passes without one never run). Shogun 2: the camera of its sweep
// (OnBlur). Warhammer II: the depth downsample's input and the sweep's camera (its blur does not run at every
// setting), into the addon's own collect shader (DispatchInstead: the game's stays its own code). Warhammer III: the depth from its downsample (this frame's), the camera from its tonemap (a pixel shader).
// Warhammer, Troy and Pharaoh compute it in place of their blur (ao_bitmask_blur.hlsli through DispatchInstead).
// Half resolution (DispatchInstead with `half`): the addon's shader over half the output's size into the addon's
// half-size target at u1 (each pixel the top-left one of a 2 x 2: its result and distance), then the pass's upsample
// build (AO_UPSAMPLE: twattila/ao_upsample.hlsli, reading that target at t40) over the output.
// Depth Levels (twattila/depth_levels.hpp) of the depth it reads, bound for it at depth_levels::SLOT.
namespace tw::line_sweep_ao {

namespace internal {
// Held (one reference each) from the blur to the next collect; never released at exit, when D3D may be gone already.
static ID3D11ShaderResourceView* depth = nullptr;
static ID3D11Buffer* camera = nullptr;
// The collect's own views at the slots used, put back after it.
static ID3D11ShaderResourceView* previous_depth = nullptr;
static ID3D11Buffer* previous_camera = nullptr;
static ID3D11ShaderResourceView* previous_levels = nullptr;
static bool bound = false;
static bool levels_bound = false;
static bool depth_bound = false;
static UINT depth_slot = 1;
static UINT camera_slot = 0;
// The addon's own passes (DispatchInstead): made on the device of their first dispatch (again when the device changes).
struct OwnShader {
  const uint8_t* bytecode = nullptr;
  ID3D11ComputeShader* shader = nullptr;
};
static ID3D11Device* own_device = nullptr;
static std::vector<OwnShader> own_shaders;
static ID3D11Buffer* own_settings = nullptr;
// Half resolution: the result and distance at half the output's size (R16G16).
static ID3D11Texture2D* half_texture = nullptr;
static ID3D11UnorderedAccessView* half_uav = nullptr;
static ID3D11ShaderResourceView* half_srv = nullptr;
static UINT half_width = 0;
static UINT half_height = 0;
constexpr UINT HALF_UAV_SLOT = 1;
constexpr UINT HALF_SRV_SLOT = 40;

template <typename T>
inline void Release(T** object) {
  if (*object != nullptr) (*object)->Release();
  *object = nullptr;
}

// The addon's shader made from `bytecode` on own_device (null if it could not be made).
inline ID3D11ComputeShader* OwnComputeShader(std::span<const uint8_t> bytecode) {
  auto own = std::ranges::find(own_shaders, bytecode.data(), &OwnShader::bytecode);
  if (own == own_shaders.end()) {
    own = own_shaders.insert(own_shaders.end(), {.bytecode = bytecode.data()});
    own_device->CreateComputeShader(bytecode.data(), bytecode.size(), nullptr, &own->shader);
  }
  return own->shader;
}

// Keeps `fresh` (a reference the caller owns) in `*kept`, dropping the one kept before.
template <typename T>
inline void Keep(T** kept, T* fresh) {
  if (fresh == *kept) {
    if (fresh != nullptr) fresh->Release();
    return;
  }
  if (*kept != nullptr) (*kept)->Release();
  *kept = fresh;
}

// The Depth Levels of the depth at the compute shader's `slot`, bound at their slot (the view there kept for
// UnbindLevels).
inline void BindLevels(ID3D11DeviceContext* context, UINT slot) {
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depth;
  context->CSGetShaderResources(slot, 1, depth.GetAddressOf());
  if (!depth) return;
  const Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> levels = twattila::depth_levels::Build(context, depth.Get());
  context->CSGetShaderResources(twattila::depth_levels::SLOT, 1, &previous_levels);
  context->CSSetShaderResources(twattila::depth_levels::SLOT, 1, levels.GetAddressOf());
  levels_bound = true;
}

inline void UnbindLevels(ID3D11DeviceContext* context) {
  if (!levels_bound) return;
  levels_bound = false;
  context->CSSetShaderResources(twattila::depth_levels::SLOT, 1, &previous_levels);
  if (previous_levels != nullptr) {
    previous_levels->Release();
  }
  previous_levels = nullptr;
}
}  // namespace internal

// The blur (Shogun 2: its sweep): its depth (t0) and camera (b0) kept.
inline bool OnBlur(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  ID3D11ShaderResourceView* depth = nullptr;
  context->CSGetShaderResources(0, 1, &depth);
  internal::Keep(&internal::depth, depth);
  ID3D11Buffer* camera = nullptr;
  context->CSGetConstantBuffers(0, 1, &camera);
  internal::Keep(&internal::camera, camera);
  return true;
}

// Warhammer III: its AO depth, the downsample's output (u0), kept as a shader resource view for the collect.
inline void KeepUavDepth(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  ID3D11UnorderedAccessView* output = nullptr;
  context->CSGetUnorderedAccessViews(0, 1, &output);
  if (output == nullptr) return;
  ID3D11Resource* texture = nullptr;
  output->GetResource(&texture);
  output->Release();
  // The same texture as last frame: its view kept.
  if (internal::depth != nullptr) {
    ID3D11Resource* kept = nullptr;
    internal::depth->GetResource(&kept);
    kept->Release();
    if (kept == texture) {
      texture->Release();
      return;
    }
  }
  ID3D11Device* device = nullptr;
  context->GetDevice(&device);
  ID3D11ShaderResourceView* view = nullptr;
  device->CreateShaderResourceView(texture, nullptr, &view);
  device->Release();
  texture->Release();
  internal::Keep(&internal::depth, view);
}

// Warhammer II: the frame's depth, a compute pass's view at `slot` (its depth downsample's input), kept for the collect.
inline void KeepComputeDepth(reshade::api::command_list* cmd_list, UINT slot) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  ID3D11ShaderResourceView* depth = nullptr;
  reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())->CSGetShaderResources(slot, 1, &depth);
  internal::Keep(&internal::depth, depth);
}

// Warhammer II: the camera of a compute pass's `slot` (the sweep's b0, still bound at the collect) kept for it.
inline void KeepComputeCamera(reshade::api::command_list* cmd_list, UINT slot) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  ID3D11Buffer* camera = nullptr;
  reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())->CSGetConstantBuffers(slot, 1, &camera);
  internal::Keep(&internal::camera, camera);
}

// The Visibility Bitmask in place of a pass of the game's, whose replacement stays the game's code (Game's ambient
// occlusion costs what it did; its hook returns this negated: skip the game's): `bytecode` (8 x 8 groups) over the
// pass's output (u0, bound) with the pass's bindings, `settings` (the addon's shader settings, `settings_size` bytes)
// at b13 and, with `kept` (Warhammer II's collect), the kept depth at t2 and camera at b0, and the Depth Levels of its
// depth (t2 with `kept`, else the pass's t0), unless OnCollect bound them; with `half` (the shaders read it from the
// settings too) at half size, then `upsample`. Everything it binds put back after. False without them (the first
// frame: the game's pass runs) or its objects.
inline bool DispatchInstead(reshade::api::command_list* cmd_list, std::span<const uint8_t> bytecode, const void* settings,
                            UINT settings_size, bool kept, std::span<const uint8_t> upsample = {}, bool half = false) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return false;
  if (kept && (internal::depth == nullptr || internal::camera == nullptr)) return false;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  ID3D11Device* device = nullptr;
  context->GetDevice(&device);
  device->Release();
  if (internal::own_device != device) {
    for (auto& own : internal::own_shaders) internal::Release(&own.shader);
    internal::own_shaders.clear();
    internal::Release(&internal::own_settings);
    internal::Release(&internal::half_uav);
    internal::Release(&internal::half_srv);
    internal::Release(&internal::half_texture);
    internal::half_width = 0;
    internal::half_height = 0;
    internal::own_device = device;
    const D3D11_BUFFER_DESC settings_desc = {
        .ByteWidth = (settings_size + 15u) & ~15u,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
    };
    device->CreateBuffer(&settings_desc, nullptr, &internal::own_settings);
  }
  ID3D11ComputeShader* own_shader = internal::OwnComputeShader(bytecode);
  ID3D11ComputeShader* upsample_shader = (half ? internal::OwnComputeShader(upsample) : nullptr);
  if (own_shader == nullptr || internal::own_settings == nullptr || (half && upsample_shader == nullptr)) return false;
  ID3D11UnorderedAccessView* output = nullptr;
  context->CSGetUnorderedAccessViews(0, 1, &output);
  if (output == nullptr) return false;
  ID3D11Resource* resource = nullptr;
  output->GetResource(&resource);
  output->Release();
  ID3D11Texture2D* texture = nullptr;
  resource->QueryInterface(&texture);
  resource->Release();
  if (texture == nullptr) return false;
  D3D11_TEXTURE2D_DESC output_desc = {};
  texture->GetDesc(&output_desc);
  texture->Release();
  const UINT width = (half ? (output_desc.Width + 1u) / 2u : output_desc.Width);
  const UINT height = (half ? (output_desc.Height + 1u) / 2u : output_desc.Height);
  if (half && (internal::half_width != width || internal::half_height != height)) {
    internal::Release(&internal::half_uav);
    internal::Release(&internal::half_srv);
    internal::Release(&internal::half_texture);
    internal::half_width = 0;
    internal::half_height = 0;
    const D3D11_TEXTURE2D_DESC half_desc = {
        .Width = width,
        .Height = height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R16G16_FLOAT,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
    };
    if (FAILED(device->CreateTexture2D(&half_desc, nullptr, &internal::half_texture))
        || FAILED(device->CreateUnorderedAccessView(internal::half_texture, nullptr, &internal::half_uav))
        || FAILED(device->CreateShaderResourceView(internal::half_texture, nullptr, &internal::half_srv))) {
      internal::Release(&internal::half_uav);
      internal::Release(&internal::half_srv);
      internal::Release(&internal::half_texture);
      return false;
    }
    internal::half_width = width;
    internal::half_height = height;
  }

  ID3D11ComputeShader* game_shader = nullptr;
  context->CSGetShader(&game_shader, nullptr, nullptr);
  ID3D11ShaderResourceView* game_depth = nullptr;
  context->CSGetShaderResources(2, 1, &game_depth);
  ID3D11Buffer* game_camera = nullptr;
  context->CSGetConstantBuffers(0, 1, &game_camera);
  ID3D11Buffer* game_settings = nullptr;
  context->CSGetConstantBuffers(13, 1, &game_settings);

  ID3D11UnorderedAccessView* game_half_uav = nullptr;
  ID3D11ShaderResourceView* game_half_srv = nullptr;
  if (half) {
    context->CSGetUnorderedAccessViews(internal::HALF_UAV_SLOT, 1, &game_half_uav);
    context->CSGetShaderResources(internal::HALF_SRV_SLOT, 1, &game_half_srv);
    context->CSSetUnorderedAccessViews(internal::HALF_UAV_SLOT, 1, &internal::half_uav, nullptr);
  }

  context->UpdateSubresource(internal::own_settings, 0, nullptr, settings, 0, 0);
  context->CSSetShader(own_shader, nullptr, 0);
  if (kept) {
    context->CSSetShaderResources(2, 1, &internal::depth);
    context->CSSetConstantBuffers(0, 1, &internal::camera);
  }
  context->CSSetConstantBuffers(13, 1, &internal::own_settings);
  const bool own_levels = !internal::levels_bound;
  if (own_levels) {
    internal::BindLevels(context, kept ? 2 : 0);
  }
  context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);
  if (own_levels) {
    internal::UnbindLevels(context);
  }
  if (half) {
    ID3D11UnorderedAccessView* no_uav = nullptr;
    context->CSSetUnorderedAccessViews(internal::HALF_UAV_SLOT, 1, &no_uav, nullptr);
    context->CSSetShaderResources(internal::HALF_SRV_SLOT, 1, &internal::half_srv);
    context->CSSetShader(upsample_shader, nullptr, 0);
    context->Dispatch((output_desc.Width + 7u) / 8u, (output_desc.Height + 7u) / 8u, 1);
    context->CSSetShaderResources(internal::HALF_SRV_SLOT, 1, &game_half_srv);
    context->CSSetUnorderedAccessViews(internal::HALF_UAV_SLOT, 1, &game_half_uav, nullptr);
    if (game_half_srv != nullptr) game_half_srv->Release();
    if (game_half_uav != nullptr) game_half_uav->Release();
  }

  context->CSSetShader(game_shader, nullptr, 0);
  context->CSSetShaderResources(2, 1, &game_depth);
  context->CSSetConstantBuffers(0, 1, &game_camera);
  context->CSSetConstantBuffers(13, 1, &game_settings);
  if (game_shader != nullptr) game_shader->Release();
  if (game_depth != nullptr) game_depth->Release();
  if (game_camera != nullptr) game_camera->Release();
  if (game_settings != nullptr) game_settings->Release();
  return true;
}

// Warhammer III: the camera of a pixel shader's `slot` (its tonemap's b1) kept for the next collect.
inline void KeepPixelCamera(reshade::api::command_list* cmd_list, UINT slot) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  ID3D11Buffer* camera = nullptr;
  reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())->PSGetConstantBuffers(slot, 1, &camera);
  internal::Keep(&internal::camera, camera);
}

// The collect: with the bitmask, the kept depth at `depth_slot` (`with_depth`; else the collect's own there) and camera
// at `camera_slot` (skipped without them: the first frame), and the Depth Levels of that depth.
inline bool OnCollect(reshade::api::command_list* cmd_list, bool bitmask, bool with_depth = true, UINT camera_slot = 0,
                      UINT depth_slot = 1) {
  if (!bitmask || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return true;
  if ((with_depth && internal::depth == nullptr) || internal::camera == nullptr) return false;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  internal::depth_bound = with_depth;
  internal::depth_slot = depth_slot;
  if (with_depth) {
    context->CSGetShaderResources(depth_slot, 1, &internal::previous_depth);
    context->CSSetShaderResources(depth_slot, 1, &internal::depth);
  }
  internal::camera_slot = camera_slot;
  context->CSGetConstantBuffers(camera_slot, 1, &internal::previous_camera);
  context->CSSetConstantBuffers(camera_slot, 1, &internal::camera);
  internal::BindLevels(context, depth_slot);
  internal::bound = true;
  return true;
}

inline void OnCollectDone(reshade::api::command_list* cmd_list) {
  if (!internal::bound) return;
  internal::bound = false;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  internal::UnbindLevels(context);
  if (internal::depth_bound) {
    context->CSSetShaderResources(internal::depth_slot, 1, &internal::previous_depth);
    if (internal::previous_depth != nullptr) internal::previous_depth->Release();
  }
  context->CSSetConstantBuffers(internal::camera_slot, 1, &internal::previous_camera);
  if (internal::previous_camera != nullptr) internal::previous_camera->Release();
  internal::previous_depth = nullptr;
  internal::previous_camera = nullptr;
}

}  // namespace tw::line_sweep_ao
