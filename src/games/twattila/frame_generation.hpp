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
// copied and the helper presents both (dlaa_bridge.hpp PresentFrame). Needs DLAA or DLSS and a windowed or borderless
// game. Streamline takes no scRGB: an scRGB swap chain's frames go to the helper as HDR10 (Install's shader).
namespace twattila::frame_generation {

using Microsoft::WRL::ComPtr;

struct Settings {
  uint32_t multiplier = 0;  // frames presented per rendered frame (2-6; 1: Dynamic, up to the display's refresh rate); 0: off
  bool reflex_boost = false;
  uint32_t frame_limit = 0;  // Reflex's frame rate limit, rendered frames per second (0: none)
  uint32_t preset = 0;       // NVIDIA's A-C as 1-3 (0: the driver's)
  bool hud_recomposition = true;
  uint32_t debug_view = 0;  // what the helper presents: 1 the HUD-less image, 2 the UI layer tinted (0: the frame)
  bool sdr_output = false;  // the frames are SDR (gamma 2.2), whatever the swap chain's format
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

// An scRGB swap chain's picture on its way to HDR10 (size 0: none): the back buffer's copy, or the HUD-less image.
struct ScrgbPicture {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11RenderTargetView> rtv;
  UINT width = 0;
  UINT height = 0;
};

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
  ComPtr<ID3D11PixelShader> hdr10_pixel_shader;
  ScrgbPicture scrgb;
};

struct State {
  std::shared_mutex mutex;
  Settings settings;
  // The addon's swap chain shaders and shader constants (Install, kept while installed).
  std::span<const uint8_t> vertex_code;
  std::span<const uint8_t> pixel_code;
  std::span<const uint8_t> hdr10_pixel_code;  // scRGB to HDR10
  const void* injection = nullptr;
  UINT injection_bytes = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;  // the frames' (the swap chain's; HDR10 for scRGB), seen at a present
  bool scrgb = false;                        // the swap chain is scRGB and its frames go as HDR10
  uint32_t frame_index = 0;
  bool kept = false;        // KeepFrame ran since the last present
  bool hudless = false;     // and drew the HUD-less image
  bool presenting = false;  // the helper's window shows (an off request hides it)
  bool overlay_open = false;  // ReShade's menu: only the frame has it, so no generated frames while it is open
  bool output_pass = false;   // the HUD-less images come from a later pass of the output (KeepOutput), not the tone mapper
  bool output_drawn = false;  // this frame's did
  Inputs inputs;
  DeviceObjects objects;
};

static State state;

// A full-screen draw of `source` into `target`, on the pass's context state (the caller swaps it in). The state's lock held.
inline void DrawPass(ID3D11DeviceContext* context, ID3D11PixelShader* pixel_shader, ID3D11ShaderResourceView* source,
                     ID3D11RenderTargetView* target, UINT width, UINT height) {
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(width), .Height = static_cast<float>(height), .MaxDepth = 1.f};
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(state.objects.vertex_shader.Get(), nullptr, 0);
  context->PSSetShader(pixel_shader, nullptr, 0);
  context->PSSetShaderResources(0, 1, &source);
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, &target, nullptr);
  context->Draw(3, 0);
  ID3D11ShaderResourceView* no_view = nullptr;
  context->PSSetShaderResources(0, 1, &no_view);
  context->OMSetRenderTargets(0, nullptr, nullptr);
}

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
    auto& objects = state.objects;
    state.scrgb = back_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    state.format = (state.scrgb ? DXGI_FORMAT_R10G10B10A2_UNORM : back_desc.Format);
    const bool kept = std::exchange(state.kept, false);
    // A kept frame without its later output pass: the next ones take the tone mapper's image again.
    if (kept && !state.output_drawn) state.output_pass = false;
    const Settings settings = state.settings;
    // Off: these go with the shared frames (Format(), dlaa_bridge::Prepare).
    if (settings.multiplier == 0) {
      objects.scrgb = {};
      objects.scene.Reset();
      objects.scene_srv.Reset();
    }
    auto& bridge = dlaa_bridge::bridge;
    // ReShade's menu open: the game's frames only (generated ones warped the menu, ATN 2026-10-28). Textures that no longer
    // fit (the size changed, no DLSS frame yet) too: the window hidden, not frozen on its last frame.
    const bool fits = bridge.fg_frame && bridge.fg_format == state.format && bridge.width == back_desc.Width && bridge.height == back_desc.Height;
    const uint32_t multiplier = ((state.overlay_open || !fits) ? 0u : settings.multiplier);
    // Unavailable here: nothing to ask (NVAPI Reflex back). Off with the window hidden already: neither.
    if (dlaa_bridge::FrameGenerationFailed()) {
      state.presenting = false;
      return;
    }
    if (multiplier == 0 && !state.presenting) return;
    // An scRGB swap chain's texture comes with the first HUD-less image (DrawHudless): the game's own image until then.
    ComPtr<ID3D11DeviceContext1> context1;
    if (multiplier != 0 && state.scrgb
        && (objects.scrgb.width != back_desc.Width || objects.scrgb.height != back_desc.Height
            || FAILED(native->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf()))))) {
      return;
    }
    if (multiplier != 0) {
      dlaa_bridge::BeforeSharedWrites(native);
      if (state.scrgb) {
        // Copied first: the game's back buffers cannot be read (its BufferUsage is render target only).
        native->CopyResource(objects.scrgb.texture.Get(), back_texture.Get());
        ComPtr<ID3DDeviceContextState> game_state;
        context1->SwapDeviceContextState(objects.context_state.Get(), game_state.GetAddressOf());
        DrawPass(native, objects.hdr10_pixel_shader.Get(), objects.scrgb.srv.Get(), bridge.fg_frame_rtv.Get(), back_desc.Width, back_desc.Height);
        context1->SwapDeviceContextState(game_state.Get(), nullptr);
      } else {
        native->CopyResource(bridge.fg_frame.Get(), back_texture.Get());
      }
    }
    const bool hudless = multiplier != 0 && kept && std::exchange(state.hudless, false);
    const Inputs& inputs = state.inputs;
    request.multiplier = multiplier;
    request.reflex_boost = settings.reflex_boost;
    request.frame_limit = settings.frame_limit;
    request.preset = settings.preset;
    request.hud_recomposition = settings.hud_recomposition;
    request.debug_view = settings.debug_view;
    request.sdr_output = settings.sdr_output;
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

// The format frame generation's textures take (the frames': unknown until a present; anti_aliasing.hpp passes it to
// dlaa_bridge::Prepare), when it is on.
inline DXGI_FORMAT Format() {
  const std::shared_lock lock(internal::state.mutex);
  if (internal::state.settings.multiplier == 0 && !internal::state.presenting) return DXGI_FORMAT_UNKNOWN;
  return internal::state.format;
}

namespace internal {

// `target` (the tone mapper's output, or a later pass's of the frame's output) as the frame's HUD-less image: the swap
// chain shader as the swap chain mod draws it, with the addon's shader constants, into the bridge's HUD-less frame (its
// format and the swap chain's size). False: the pass's objects could not be made. The state's lock held.
inline bool DrawHudless(ID3D11DeviceContext* context, ID3D11Texture2D* target) {
  auto& objects = state.objects;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  D3D11_TEXTURE2D_DESC desc = {};
  target->GetDesc(&desc);
  ComPtr<ID3D11Device1> device1;
  if (FAILED(device.As(&device1))) return false;
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
        || FAILED(device1->CreatePixelShader(state.hdr10_pixel_code.data(), state.hdr10_pixel_code.size(), nullptr,
                                             objects.hdr10_pixel_shader.GetAddressOf()))
        || FAILED(device1->CreateSamplerState(&sampler_desc, objects.sampler.GetAddressOf()))
        || FAILED(device1->CreateBuffer(&injection_desc, nullptr, objects.injection.GetAddressOf()))) {
      objects = {.device = device1};
      return false;
    }
  }
  // The image to draw from: the target itself when it can be read, else a copy. An unreadable one is not kept: an scRGB
  // swap chain's target is its back buffer, and a reference to that fails the game's ResizeBuffers.
  ID3D11ShaderResourceView* scene = nullptr;
  if (objects.target.Get() != target) {
    objects.target.Reset();
    objects.target_srv.Reset();
    if ((desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0 && desc.SampleDesc.Count == 1) {
      objects.target = target;
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
  state.hudless = false;
  auto& bridge = dlaa_bridge::bridge;
  ComPtr<ID3D11DeviceContext1> context1;
  if (scene == nullptr || !bridge.fg_hudless_rtv || bridge.width != desc.Width || bridge.height != desc.Height
      || FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) {
    return true;
  }
  // An scRGB swap chain: the shader's scRGB goes through this texture, as the frame does (OnReshadePresent), so both round
  // alike (else bright saturated pixels would differ: UI to fg_ui).
  if (state.scrgb && (objects.scrgb.width != desc.Width || objects.scrgb.height != desc.Height)) {
    const D3D11_TEXTURE2D_DESC scrgb_desc = {
        .Width = desc.Width,
        .Height = desc.Height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
    };
    objects.scrgb = {};
    if (FAILED(device1->CreateTexture2D(&scrgb_desc, nullptr, objects.scrgb.texture.GetAddressOf()))
        || FAILED(device1->CreateShaderResourceView(objects.scrgb.texture.Get(), nullptr, objects.scrgb.srv.GetAddressOf()))
        || FAILED(device1->CreateRenderTargetView(objects.scrgb.texture.Get(), nullptr, objects.scrgb.rtv.GetAddressOf()))) {
      objects.scrgb = {};
      return true;
    }
    objects.scrgb.width = desc.Width;
    objects.scrgb.height = desc.Height;
  }
  objects.constants.resize((state.injection_bytes + 15) / 16 * 16);
  std::memcpy(objects.constants.data(), state.injection, state.injection_bytes);
  context->UpdateSubresource(objects.injection.Get(), 0, nullptr, objects.constants.data(), 0, 0);
  dlaa_bridge::BeforeSharedWrites(context);
  ComPtr<ID3DDeviceContextState> game_state;
  context1->SwapDeviceContextState(objects.context_state.Get(), game_state.GetAddressOf());
  ID3D11SamplerState* sampler = objects.sampler.Get();
  ID3D11Buffer* injection = objects.injection.Get();
  context->PSSetSamplers(0, 1, &sampler);
  context->PSSetConstantBuffers(INJECTION_SLOT, 1, &injection);
  if (state.scrgb) {
    DrawPass(context, objects.pixel_shader.Get(), scene, objects.scrgb.rtv.Get(), desc.Width, desc.Height);
    DrawPass(context, objects.hdr10_pixel_shader.Get(), objects.scrgb.srv.Get(), bridge.fg_hudless_rtv.Get(), desc.Width, desc.Height);
  } else {
    DrawPass(context, objects.pixel_shader.Get(), scene, bridge.fg_hudless_rtv.Get(), desc.Width, desc.Height);
  }
  context1->SwapDeviceContextState(game_state.Get(), nullptr);
  state.hudless = true;
  return true;
}

}  // namespace internal

// DLAA ran on `target` (the tone mapper's output, before the UI): the frame's inputs kept, and its HUD-less image unless
// the last frames took that from a later pass of their output (KeepOutput).
inline void KeepFrame(ID3D11DeviceContext* context, ID3D11Texture2D* target, const Inputs& inputs) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (state.settings.multiplier == 0 || state.vertex_code.empty()) return;
  state.hudless = false;
  state.output_drawn = false;
  if (!state.output_pass && !internal::DrawHudless(context, target)) return;
  state.inputs = inputs;
  state.kept = true;
}

// A later pass of the kept frame's output drew `target` (Attila's, Thrones' and Rome II's hdr_to_screen after their
// tone mapper: the HDR output, grading and fog of war the presented frame has; from the tone mapper's the HUD-less image
// was much darker, so all of it was UI, ATN): the frame's HUD-less image from it.
inline void KeepOutput(ID3D11DeviceContext* context, ID3D11Texture2D* target) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (!state.kept || state.settings.multiplier == 0 || state.vertex_code.empty()) return;
  if (internal::DrawHudless(context, target)) {
    state.output_drawn = true;
    state.output_pass = true;
  }
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

// The addon's swap chain shaders and its shader constants (`injection_bytes` at `injection`: kept while installed), and its
// scRGB to HDR10 shader.
inline void Install(std::span<const uint8_t> vertex_code, std::span<const uint8_t> pixel_code, const void* injection, UINT injection_bytes,
                    std::span<const uint8_t> hdr10_pixel_code) {
  {
    const std::unique_lock lock(internal::state.mutex);
    internal::state.vertex_code = vertex_code;
    internal::state.pixel_code = pixel_code;
    internal::state.hdr10_pixel_code = hdr10_pixel_code;
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
  internal::state.hdr10_pixel_code = {};
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
