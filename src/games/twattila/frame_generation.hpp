/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "./dlaa_bridge.hpp"

// Frame generation (Experimental) for the 32-bit Total War games: NVIDIA's DLSS Frame Generation runs only in 64-bit
// processes, so the 64-bit helper presents (dlaa_helper/fg_d3d12.hpp: a child window over the game's, the game's own
// swap chain still presenting underneath). DLAA (anti_aliasing.hpp) keeps each frame's HUD-less image (the tone mapper's
// output after DLAA, before the UI, drawn there through the addon's swap chain shader into the swap chain's format:
// KeepFrame) with its depth, motion and camera; after ReShade drew its overlay (reshade_present) the back buffer is
// copied and the helper presents both (dlaa_bridge.hpp PresentFrame). Needs DLAA or DLSS, a windowed or
// borderless game, and HDR10 or SDR output (frame generation takes no scRGB).
namespace twattila::frame_generation {

using Microsoft::WRL::ComPtr;

struct Settings {
  uint32_t multiplier = 0;  // frames presented per rendered frame (2-6; 1: Dynamic, up to the display's refresh rate); 0: off
  bool reflex_boost = false;
  uint32_t frame_limit = 0;  // Reflex's frame rate limit, rendered frames per second (0: none)
  uint32_t preset = 0;       // NVIDIA's A-C as 1-3 (0: the driver's)
  bool hud_recomposition = true;
  uint32_t debug_view = 0;  // what the helper presents: 1 the HUD-less image, 2 the UI layer tinted (0: the frame)
};

// A frame's DLAA inputs (their size and jitter, the depth and motion are the bridge's) and its camera (row vectors,
// without the jitter; position, right, up, forward; near and far).
struct Inputs {
  uint32_t width = 0;
  uint32_t height = 0;
  float jitter_x = 0.f;
  float jitter_y = 0.f;
  bool reset = false;
  std::array<float, 16> view_to_clip = {};
  std::array<float, 16> view_projection = {};
  std::array<float, 16> previous_view_projection = {};
  std::array<float, 12> camera = {};
  float near_plane = 0.f;
  float far_plane = 0.f;
};

namespace internal {

constexpr UINT INJECTION_SLOT = 13;

// The HUD-less image's pass: the swap chain shader in a context state of its own, its constants (and their copy padded
// to the buffer's size), the tone mapper's target read in place (its view kept while it is the same texture), or a copy
// of it where it cannot be read.
struct DeviceObjects {
  ComPtr<ID3D11Device1> device;
  ComPtr<ID3DDeviceContextState> context_state;
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11Buffer> injection;
  std::vector<uint8_t> constants;
  ComPtr<ID3D11Resource> target;
  ComPtr<ID3D11ShaderResourceView> target_srv;
  ComPtr<ID3D11Texture2D> scene;
  ComPtr<ID3D11ShaderResourceView> scene_srv;
};

struct State {
  std::shared_mutex mutex;
  Settings settings;
  // The addon's swap chain shaders and shader constants (Install, kept while installed).
  std::span<const uint8_t> vertex_code;
  std::span<const uint8_t> pixel_code;
  const void* injection = nullptr;
  UINT injection_bytes = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;  // the swap chain's, seen at a present
  uint32_t frame_index = 0;
  bool kept = false;        // KeepFrame ran since the last present
  bool hudless = false;     // and drew the HUD-less image
  bool presenting = false;  // the helper's window shows (an off request hides it)
  bool overlay_open = false;  // ReShade's menu: only the frame has it, so the debug views wait while it is open
  Inputs inputs;
  DeviceObjects objects;
};

static State state;

// After ReShade's overlay: the HUD-less image into the swap chain's format, the frame copied, the helper presents.
inline void OnReshadePresent(reshade::api::effect_runtime* runtime) {
  auto* device = runtime->get_device();
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(runtime->get_command_queue()->get_immediate_command_list()->get_native());
  renodx::utils::directx::NativeFromReShadeProxy(&native);
  auto* back_buffer = reinterpret_cast<ID3D11Resource*>(runtime->get_current_back_buffer().handle);
  ComPtr<ID3D11Texture2D> back_texture;
  if (native == nullptr || back_buffer == nullptr || FAILED(back_buffer->QueryInterface(IID_PPV_ARGS(back_texture.GetAddressOf())))) return;
  D3D11_TEXTURE2D_DESC back_desc = {};
  back_texture->GetDesc(&back_desc);

  dlaa_bridge::FrameRequest request = {.window = static_cast<HWND>(runtime->get_hwnd())};
  {
    const std::unique_lock lock(state.mutex);
    state.format = back_desc.Format;
    const bool kept = std::exchange(state.kept, false);
    const Settings settings = state.settings;
    // Off with the window hidden already, or unavailable here: nothing to ask.
    if ((settings.multiplier == 0 && !state.presenting) || dlaa_bridge::FrameGenerationFailed()) return;
    auto& bridge = dlaa_bridge::bridge;
    if (!bridge.fg_frame || bridge.fg_format != back_desc.Format || bridge.width != back_desc.Width || bridge.height != back_desc.Height) return;
    if (settings.multiplier != 0) {
      dlaa_bridge::BeforeSharedWrites(native);
      native->CopyResource(bridge.fg_frame.Get(), back_texture.Get());
    }
    const bool hudless = settings.multiplier != 0 && kept && std::exchange(state.hudless, false);
    const Inputs& inputs = state.inputs;
    request.multiplier = settings.multiplier;
    request.reflex_boost = settings.reflex_boost;
    request.frame_limit = settings.frame_limit;
    request.preset = settings.preset;
    request.hud_recomposition = settings.hud_recomposition;
    request.debug_view = (state.overlay_open ? 0u : settings.debug_view);
    request.hudless = hudless;
    request.inputs = kept;
    request.frame_index = ++state.frame_index;
    if (kept) {
      request.width = inputs.width;
      request.height = inputs.height;
      request.jitter_x = inputs.jitter_x;
      request.jitter_y = inputs.jitter_y;
      request.reset = inputs.reset;
      request.view_to_clip = inputs.view_to_clip;
      request.view_projection = inputs.view_projection;
      request.previous_view_projection = inputs.previous_view_projection;
      request.camera = inputs.camera;
      request.near_plane = inputs.near_plane;
      request.far_plane = inputs.far_plane;
    }
  }
  // The helper's answer (it presents, then Reflex paces the next frame) outside the lock.
  const bool presented = dlaa_bridge::PresentFrame(native, request);
  const std::unique_lock lock(state.mutex);
  state.presenting = (request.multiplier != 0 && presented);
}

inline bool OnOpenOverlay(reshade::api::effect_runtime*, bool open, reshade::api::input_source) {
  const std::unique_lock lock(state.mutex);
  state.overlay_open = open;
  return false;
}

}  // namespace internal

// The swap chain's format frame generation's textures take (unknown until a present; anti_aliasing.hpp passes it to
// dlaa_bridge::Prepare), when it is on.
inline DXGI_FORMAT Format() {
  const std::shared_lock lock(internal::state.mutex);
  if (internal::state.settings.multiplier == 0 && !internal::state.presenting) return DXGI_FORMAT_UNKNOWN;
  return internal::state.format;
}

// DLAA ran on `target` (the tone mapper's output, before the UI): it is kept as the frame's HUD-less image with its
// inputs.
inline void KeepFrame(ID3D11DeviceContext* context, ID3D11Texture2D* target, const Inputs& inputs) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (state.settings.multiplier == 0 || state.vertex_code.empty()) return;
  auto& objects = state.objects;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  D3D11_TEXTURE2D_DESC desc = {};
  target->GetDesc(&desc);
  ComPtr<ID3D11Device1> device1;
  if (FAILED(device.As(&device1))) return;
  if (objects.device != device1) {
    objects = {.device = device1};
    const D3D_FEATURE_LEVEL level = device1->GetFeatureLevel();
    const D3D11_SAMPLER_DESC sampler_desc = {
        .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
        .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
        .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
        .ComparisonFunc = D3D11_COMPARISON_NEVER,
        .MaxLOD = D3D11_FLOAT32_MAX,
    };
    const D3D11_BUFFER_DESC injection_desc = {.ByteWidth = (state.injection_bytes + 15) / 16 * 16, .Usage = D3D11_USAGE_DEFAULT,
                                              .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const UINT state_flags = ((device1->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0
                                  ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                                  : 0);
    if (FAILED(device1->CreateDeviceContextState(state_flags, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                                 objects.context_state.GetAddressOf()))
        || FAILED(device1->CreateVertexShader(state.vertex_code.data(), state.vertex_code.size(), nullptr, objects.vertex_shader.GetAddressOf()))
        || FAILED(device1->CreatePixelShader(state.pixel_code.data(), state.pixel_code.size(), nullptr, objects.pixel_shader.GetAddressOf()))
        || FAILED(device1->CreateSamplerState(&sampler_desc, objects.sampler.GetAddressOf()))
        || FAILED(device1->CreateBuffer(&injection_desc, nullptr, objects.injection.GetAddressOf()))) {
      objects = {.device = device1};
      return;
    }
  }
  // The image to draw from: the target itself when it can be read, else a copy.
  ID3D11ShaderResourceView* scene = nullptr;
  if (objects.target.Get() != target) {
    objects.target = target;
    objects.target_srv.Reset();
    if ((desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 && desc.SampleDesc.Count == 1) {
      device1->CreateShaderResourceView(target, nullptr, objects.target_srv.GetAddressOf());
    }
  }
  if (objects.target_srv) {
    scene = objects.target_srv.Get();
  } else {
    if (D3D11_TEXTURE2D_DESC kept = {}; objects.scene) {
      objects.scene->GetDesc(&kept);
      if (kept.Width != desc.Width || kept.Height != desc.Height || kept.Format != desc.Format) {
        objects.scene.Reset();
        objects.scene_srv.Reset();
      }
    }
    if (!objects.scene) {
      D3D11_TEXTURE2D_DESC scene_desc = desc;
      scene_desc.MipLevels = 1;
      scene_desc.ArraySize = 1;
      scene_desc.SampleDesc = {.Count = 1};
      scene_desc.Usage = D3D11_USAGE_DEFAULT;
      scene_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      scene_desc.CPUAccessFlags = 0;
      scene_desc.MiscFlags = 0;
      if (desc.SampleDesc.Count != 1 || FAILED(device1->CreateTexture2D(&scene_desc, nullptr, objects.scene.GetAddressOf()))
          || FAILED(device1->CreateShaderResourceView(objects.scene.Get(), nullptr, objects.scene_srv.GetAddressOf()))) {
        objects.scene.Reset();
        objects.scene_srv.Reset();
      }
    }
    if (objects.scene) {
      context->CopyResource(objects.scene.Get(), target);
      scene = objects.scene_srv.Get();
    }
  }
  state.inputs = inputs;
  state.kept = true;
  state.hudless = false;
  // The swap chain shader as the swap chain mod draws it, on the HUD-less image, with the addon's shader constants, into
  // the bridge's HUD-less frame (the swap chain's format and size).
  auto& bridge = dlaa_bridge::bridge;
  ComPtr<ID3D11DeviceContext1> context1;
  if (scene == nullptr || !bridge.fg_hudless_rtv || bridge.width != desc.Width || bridge.height != desc.Height
      || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) {
    return;
  }
  objects.constants.resize((state.injection_bytes + 15) / 16 * 16);
  std::memcpy(objects.constants.data(), state.injection, state.injection_bytes);
  context->UpdateSubresource(objects.injection.Get(), 0, nullptr, objects.constants.data(), 0, 0);
  dlaa_bridge::BeforeSharedWrites(context);
  ComPtr<ID3DDeviceContextState> game_state;
  context1->SwapDeviceContextState(objects.context_state.Get(), game_state.GetAddressOf());
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(desc.Width), .Height = static_cast<float>(desc.Height), .MaxDepth = 1.f};
  ID3D11RenderTargetView* hudless = bridge.fg_hudless_rtv.Get();
  ID3D11SamplerState* sampler = objects.sampler.Get();
  ID3D11Buffer* injection = objects.injection.Get();
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(objects.vertex_shader.Get(), nullptr, 0);
  context->PSSetShader(objects.pixel_shader.Get(), nullptr, 0);
  context->PSSetShaderResources(0, 1, &scene);
  context->PSSetSamplers(0, 1, &sampler);
  context->PSSetConstantBuffers(internal::INJECTION_SLOT, 1, &injection);
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, &hudless, nullptr);
  context->Draw(3, 0);
  ID3D11ShaderResourceView* no_view = nullptr;
  context->PSSetShaderResources(0, 1, &no_view);
  context->OMSetRenderTargets(0, nullptr, nullptr);
  context1->SwapDeviceContextState(game_state.Get(), nullptr);
  state.hudless = true;
}

inline void Update(const Settings& settings) {
  const std::unique_lock lock(internal::state.mutex);
  internal::state.settings = settings;
}

// Whether frame generation is asked for (anti_aliasing.hpp keeps the frames then).
inline bool Wanted() {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.settings.multiplier != 0;
}

// Whether the helper presents the game's frames (its Reflex paces the game then: reflex.hpp steps aside).
inline bool Presenting() {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.presenting;
}

// The helper's frame generation status (one line).
inline std::string Status() {
  return dlaa_bridge::FrameGenerationStatus();
}

// The addon's swap chain shaders and its shader constants (`injection_bytes` at `injection`: kept while installed).
inline void Install(std::span<const uint8_t> vertex_code, std::span<const uint8_t> pixel_code, const void* injection, UINT injection_bytes) {
  {
    const std::unique_lock lock(internal::state.mutex);
    internal::state.vertex_code = vertex_code;
    internal::state.pixel_code = pixel_code;
    internal::state.injection = injection;
    internal::state.injection_bytes = injection_bytes;
  }
  reshade::register_event<reshade::addon_event::reshade_present>(internal::OnReshadePresent);
  reshade::register_event<reshade::addon_event::reshade_open_overlay>(internal::OnOpenOverlay);
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::reshade_present>(internal::OnReshadePresent);
  reshade::unregister_event<reshade::addon_event::reshade_open_overlay>(internal::OnOpenOverlay);
  const std::unique_lock lock(internal::state.mutex);
  internal::state.objects = {};
  internal::state.vertex_code = {};
  internal::state.pixel_code = {};
  internal::state.injection = nullptr;
}

// The game's device is going away: the objects on it with it.
inline void OnDestroyDevice(ID3D11Device* device) {
  const std::unique_lock lock(internal::state.mutex);
  ComPtr<ID3D11Device> objects_device = internal::state.objects.device;
  if (objects_device.Get() == device) {
    internal::state.objects = {};
  }
}

}  // namespace twattila::frame_generation
