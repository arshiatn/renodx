/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <type_traits>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "./depth_levels.hpp"
#include "./frame_history.hpp"

// The visibility bitmask ambient occlusion in place of a game's AO pixel shader draw (Attila's, Thrones of Britannia's
// and Rome II's ssao.hlsl ps_pixel_resolve, bitmask_ssao.hlsli; Troy's and Pharaoh's ssao2.hlsl HBAO,
// twwarhammer2/bitmask_ao.hlsli): the replacement is the game's code only (Ambient Occlusion Game's costs what it did),
// so the addon's own shader (compiled from the same file with AO_BITMASK) is drawn instead, with the game's bindings and
// the addon's settings at b13:
// - Depth Levels (depth_levels.hpp): before it, of the game's depth (its t0), bound at depth_levels::SLOT.
// - Below full resolution, with Temporal Smoothing or the Denoiser, it draws into the addon's target (the result and its
//   distance from the camera; below full resolution 1 / scale of the size, each pixel the one at its texel x scale) with
//   the game's
//   draw and a viewport of that size, then the pass's other builds the same way, each reading the one before at t40:
//   the resolve (Temporal Smoothing, at the frame's first draw: into one of two histories, reading the other at t42 and
//   the camera of the last frame at b10, a copy of its b0 made after the draw), the denoiser (into the denoised
//   target), and the game's draw at its size with the upsample (ao_upsample.hlsli at Half resolution) into the game's
//   target; the game's blur after it as before.
// The game's bindings are put back after it. D3D11, no MSAA.
namespace twattila::ao_pass {

using Microsoft::WRL::ComPtr;

// A game's AO pass (its pixel shader's hash) and the addon's Visibility Bitmask shader drawn in its place.
struct AmbientOcclusionDraw {
  uint32_t shader_hash = 0u;
  std::span<const uint8_t> bitmask;
  std::span<const uint8_t> upsample;  // into the game's target from the addon's (none: no Half resolution)
  std::span<const uint8_t> resolve;   // Temporal Smoothing (none: not offered)
  std::span<const uint8_t> denoise;   // the Denoiser (none: not offered)
};

namespace internal {

constexpr UINT SETTINGS_SLOT = 13;
constexpr UINT DEPTH_SLOT = 0;
constexpr UINT INPUT_SLOT = 40;
constexpr UINT HISTORY_SLOT = 42;
constexpr UINT PREVIOUS_CAMERA_SLOT = 10;
constexpr UINT CAMERA_SLOT = 0;

// The addon's shader made from a pass's bytecode.
struct OwnShader {
  const uint8_t* bytecode = nullptr;
  ComPtr<ID3D11PixelShader> shader;
};

// Made on the game's device, again when it changes (the targets again when the ambient occlusion's size does; each
// when first needed). R16G16: the result and its distance; the histories R16G16B16A16 with their frame counts.
struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  std::vector<OwnShader> shaders;
  ComPtr<ID3D11Buffer> settings;
  UINT width = 0;  // the targets'
  UINT height = 0;
  frame_history::Target raw;
  std::array<frame_history::Target, 2> history;
  frame_history::Target denoised;
  size_t history_index = 0;  // the one the last resolve wrote
  ComPtr<ID3D11Buffer> previous_camera;
};

struct Runtime {
  std::shared_mutex mutex;
  // Set by the addon every present.
  bool bitmask = false;
  bool temporal = false;
  float scale = 1.f;  // the game's size over the ambient occlusion's
  bool denoise = false;
  uint64_t frame = 0;
  uint64_t drawn_frame = UINT64_MAX;
  // The addon's shader settings (Install).
  const void* settings = nullptr;
  UINT settings_size = 0;
  DeviceObjects objects;
  // The game's bindings during its draw, put back after it.
  ComPtr<ID3D11PixelShader> game_shader;
  ComPtr<ID3D11Buffer> game_settings;
  std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_targets;
  ComPtr<ID3D11DepthStencilView> game_depth;
  ComPtr<ID3D11BlendState> game_blend;
  std::array<FLOAT, 4> game_blend_factor = {};
  UINT game_sample_mask = 0;
  ComPtr<ID3D11ShaderResourceView> game_input_view;
  ComPtr<ID3D11ShaderResourceView> game_history_view;
  ComPtr<ID3D11ShaderResourceView> game_levels_view;
  ComPtr<ID3D11Buffer> game_constants;
};

static Runtime runtime;

inline ID3D11DeviceContext* NativeContext(reshade::api::command_list* cmd_list) {
  if (cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return nullptr;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return nullptr;
  return native;
}

}  // namespace internal

constexpr auto OnAmbientOcclusionDraw = []<typename T, typename Context>(
                                            Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    auto* native = internal::NativeContext(command.cmd_list);
    if (native == nullptr) return {};
    auto& runtime = internal::runtime;
    const std::unique_lock lock(runtime.mutex);
    if (!runtime.bitmask || runtime.settings == nullptr) return {};

    std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_targets;
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_target_pointers = {};
    ComPtr<ID3D11DepthStencilView> game_depth;
    native->OMGetRenderTargets(static_cast<UINT>(game_target_pointers.size()), game_target_pointers.data(),
                               game_depth.GetAddressOf());
    for (size_t index = 0; index < game_targets.size(); ++index) game_targets[index].Attach(game_target_pointers[index]);
    // Its one target (the ambient occlusion), single-sampled.
    if (!game_targets[0] || game_targets[1]) return {};
    ComPtr<ID3D11Resource> target_resource;
    game_targets[0]->GetResource(target_resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> target_texture;
    if (FAILED(target_resource.As(&target_texture))) return {};
    D3D11_TEXTURE2D_DESC target_desc = {};
    target_texture->GetDesc(&target_desc);
    if (target_desc.SampleDesc.Count != 1) return {};
    ComPtr<ID3D11BlendState> game_blend;
    std::array<FLOAT, 4> game_blend_factor = {};
    UINT game_sample_mask = 0;
    native->OMGetBlendState(game_blend.GetAddressOf(), game_blend_factor.data(), &game_sample_mask);

    ComPtr<ID3D11Device> device;
    native->GetDevice(device.GetAddressOf());
    auto& objects = runtime.objects;
    if (objects.device != device) {
      objects = {};
      objects.device = device;
    }
    // The addon's shaders for this pass and its settings buffer (made once on the device; without them the game's AO).
    const auto& draw = command.template GetCallbackData<T>();
    const auto own_shader = [&](std::span<const uint8_t> bytecode) {
      auto own = std::ranges::find(objects.shaders, bytecode.data(), &internal::OwnShader::bytecode);
      if (own == objects.shaders.end()) {
        own = objects.shaders.insert(objects.shaders.end(), {.bytecode = bytecode.data()});
        device->CreatePixelShader(bytecode.data(), bytecode.size(), nullptr, own->shader.GetAddressOf());
      }
      return own->shader;
    };
    const ComPtr<ID3D11PixelShader> bitmask = own_shader(draw.bitmask);
    const bool own_targets = runtime.scale > 1.f || runtime.temporal || runtime.denoise;
    const ComPtr<ID3D11PixelShader> upsample = (own_targets ? own_shader(draw.upsample) : nullptr);
    const ComPtr<ID3D11PixelShader> resolve = (runtime.temporal ? own_shader(draw.resolve) : nullptr);
    const ComPtr<ID3D11PixelShader> denoise = (runtime.denoise ? own_shader(draw.denoise) : nullptr);
    if (!objects.settings) {
      const D3D11_BUFFER_DESC settings_desc = {
          .ByteWidth = (runtime.settings_size + 15u) & ~15u,
          .Usage = D3D11_USAGE_DEFAULT,
          .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
      };
      device->CreateBuffer(&settings_desc, nullptr, objects.settings.GetAddressOf());
    }
    // The shaders read these from the settings: without their shaders or targets the game's AO.
    if (!bitmask || !objects.settings || (own_targets && !upsample) || (runtime.temporal && !resolve)
        || (runtime.denoise && !denoise)) {
      return {};
    }
    const auto ao_width = static_cast<UINT>(std::ceil(target_desc.Width / runtime.scale));
    const auto ao_height = static_cast<UINT>(std::ceil(target_desc.Height / runtime.scale));
    if (own_targets) {
      if (objects.width != ao_width || objects.height != ao_height) {
        objects.raw = {};
        objects.history = {};
        objects.denoised = {};
        objects.width = ao_width;
        objects.height = ao_height;
      }
      const auto make = [&](frame_history::Target* target, DXGI_FORMAT format = DXGI_FORMAT_R16G16_FLOAT) {
        return target->rtv || frame_history::MakeTarget(device.Get(), ao_width, ao_height, format, target);
      };
      bool made = make(&objects.raw) && (!runtime.denoise || make(&objects.denoised));
      if (made && runtime.temporal && !objects.history[1].rtv) {
        made = make(&objects.history[0], DXGI_FORMAT_R16G16B16A16_FLOAT)
               && make(&objects.history[1], DXGI_FORMAT_R16G16B16A16_FLOAT);
        // No history yet (distance 0: nothing there).
        constexpr float NOTHING[4] = {0.f, 0.f, 0.f, 0.f};
        for (const frame_history::Target& history : objects.history) {
          if (history.rtv) native->ClearRenderTargetView(history.rtv.Get(), NOTHING);
        }
      }
      if (!made) {
        objects.width = 0;
        return {};
      }
    }

    native->PSGetShader(runtime.game_shader.ReleaseAndGetAddressOf(), nullptr, nullptr);
    native->PSGetConstantBuffers(internal::SETTINGS_SLOT, 1, runtime.game_settings.ReleaseAndGetAddressOf());
    native->UpdateSubresource(objects.settings.Get(), 0, nullptr, runtime.settings, 0, 0);
    native->PSSetShader(bitmask.Get(), nullptr, 0);
    native->PSSetConstantBuffers(internal::SETTINGS_SLOT, 1, objects.settings.GetAddressOf());

    runtime.game_targets = game_targets;
    runtime.game_depth = game_depth;
    runtime.game_blend = game_blend;
    runtime.game_blend_factor = game_blend_factor;
    runtime.game_sample_mask = game_sample_mask;
    native->PSGetShaderResources(internal::INPUT_SLOT, 1, runtime.game_input_view.ReleaseAndGetAddressOf());
    native->PSGetShaderResources(internal::HISTORY_SLOT, 1, runtime.game_history_view.ReleaseAndGetAddressOf());
    native->PSGetShaderResources(depth_levels::SLOT, 1, runtime.game_levels_view.ReleaseAndGetAddressOf());
    native->PSGetConstantBuffers(internal::PREVIOUS_CAMERA_SLOT, 1, runtime.game_constants.ReleaseAndGetAddressOf());

    ComPtr<ID3D11ShaderResourceView> depth_view;
    native->PSGetShaderResources(internal::DEPTH_SLOT, 1, depth_view.GetAddressOf());
    if (depth_view) {
      const ComPtr<ID3D11ShaderResourceView> levels = depth_levels::Build(native, depth_view.Get());
      native->PSSetShaderResources(depth_levels::SLOT, 1, levels.GetAddressOf());
    }

    if (own_targets) {
      // The addon's passes at the ambient occlusion's size (no depth / stencil, no blending), each target set before its
      // input (no texture bound as both); then the game's draw with the upsample reading the last at t40, with the game's
      // viewports, targets and blending.
      std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> game_viewports = {};
      UINT game_viewport_count = static_cast<UINT>(game_viewports.size());
      native->RSGetViewports(&game_viewport_count, game_viewports.data());
      D3D11_VIEWPORT viewport = game_viewports[0];
      viewport = {
          .TopLeftX = std::floor(viewport.TopLeftX / runtime.scale),
          .TopLeftY = std::floor(viewport.TopLeftY / runtime.scale),
          .Width = std::ceil(viewport.Width / runtime.scale),
          .Height = std::ceil(viewport.Height / runtime.scale),
          .MinDepth = viewport.MinDepth,
          .MaxDepth = viewport.MaxDepth,
      };
      native->RSSetViewports(1, &viewport);
      native->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
      const auto pass = [&](const frame_history::Target& target, ID3D11PixelShader* shader,
                            ID3D11ShaderResourceView* input) {
        ID3D11RenderTargetView* targets[] = {target.rtv.Get()};
        native->OMSetRenderTargets(1, targets, nullptr);
        native->PSSetShaderResources(internal::INPUT_SLOT, 1, &input);
        native->PSSetShader(shader, nullptr, 0);
        frame_history::DrawAgain(native, command.arguments);
      };
      pass(objects.raw, bitmask.Get(), nullptr);
      ID3D11ShaderResourceView* current = objects.raw.srv.Get();
      if (runtime.temporal && runtime.drawn_frame != runtime.frame) {
        const size_t previous_index = objects.history_index;
        objects.history_index ^= 1;
        ID3D11ShaderResourceView* history[] = {objects.history[previous_index].srv.Get()};
        native->PSSetShaderResources(internal::HISTORY_SLOT, 1, history);
        ID3D11Buffer* previous_camera[] = {objects.previous_camera.Get()};
        native->PSSetConstantBuffers(internal::PREVIOUS_CAMERA_SLOT, 1, previous_camera);
        pass(objects.history[objects.history_index], resolve.Get(), current);
        current = objects.history[objects.history_index].srv.Get();
      }
      if (runtime.denoise) {
        pass(objects.denoised, denoise.Get(), current);
        current = objects.denoised.srv.Get();
      }

      native->RSSetViewports(game_viewport_count, game_viewports.data());
      native->OMSetRenderTargets(static_cast<UINT>(game_target_pointers.size()), game_target_pointers.data(),
                                 game_depth.Get());
      native->OMSetBlendState(game_blend.Get(), game_blend_factor.data(), game_sample_mask);
      native->PSSetShaderResources(internal::INPUT_SLOT, 1, &current);
      native->PSSetShader(upsample.Get(), nullptr, 0);
    }
    runtime.drawn_frame = runtime.frame;

    // After the draw: this frame's camera for the next (Temporal Smoothing), and the game's bindings back.
    return {
        .post_callback = [](Context& post, const void*) {
          auto* post_native = internal::NativeContext(post.cmd_list);
          if (post_native == nullptr) return;
          auto& post_runtime = internal::runtime;
          const std::unique_lock post_lock(post_runtime.mutex);
          auto& post_objects = post_runtime.objects;
          if (post_runtime.temporal && post_objects.device) {
            ComPtr<ID3D11Buffer> camera;
            post_native->PSGetConstantBuffers(internal::CAMERA_SLOT, 1, camera.GetAddressOf());
            if (camera) {
              frame_history::KeepCamera(post_objects.device.Get(), post_native, camera.Get(),
                                        &post_objects.previous_camera);
            }
          }
          std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> restored = {};
          for (size_t index = 0; index < restored.size(); ++index) {
            restored[index] = post_runtime.game_targets[index].Get();
          }
          post_native->OMSetRenderTargets(static_cast<UINT>(restored.size()), restored.data(),
                                          post_runtime.game_depth.Get());
          post_native->OMSetBlendState(post_runtime.game_blend.Get(), post_runtime.game_blend_factor.data(),
                                       post_runtime.game_sample_mask);
          post_native->PSSetShader(post_runtime.game_shader.Get(), nullptr, 0);
          post_native->PSSetConstantBuffers(internal::SETTINGS_SLOT, 1, post_runtime.game_settings.GetAddressOf());
          post_native->PSSetShaderResources(internal::INPUT_SLOT, 1, post_runtime.game_input_view.GetAddressOf());
          post_native->PSSetShaderResources(internal::HISTORY_SLOT, 1, post_runtime.game_history_view.GetAddressOf());
          post_native->PSSetShaderResources(depth_levels::SLOT, 1, post_runtime.game_levels_view.GetAddressOf());
          post_native->PSSetConstantBuffers(internal::PREVIOUS_CAMERA_SLOT, 1,
                                            post_runtime.game_constants.GetAddressOf());
          post_runtime.game_targets = {};
          post_runtime.game_depth.Reset();
          post_runtime.game_blend.Reset();
          post_runtime.game_shader.Reset();
          post_runtime.game_settings.Reset();
          post_runtime.game_input_view.Reset();
          post_runtime.game_history_view.Reset();
          post_runtime.game_levels_view.Reset();
          post_runtime.game_constants.Reset();
        },
    };
  }
};

// The Resolution setting's item (Full, 3/4, Half, 1/4) as the game's size over the ambient occlusion's; with Effects at
// Output Resolution x the render share (at least full), the detail of 100%.
inline float ResolutionScale(float resolution) {
  constexpr std::array<float, 4> SCALES = {1.f, 4.f / 3.f, 2.f, 4.f};
  return std::max(SCALES[std::clamp(static_cast<int>(resolution), 0, 3)]
                      * frame_history::effect_share.load(std::memory_order_relaxed),
                  1.f);
}

// Every present: the visibility bitmask ambient occlusion on or off, its Temporal Smoothing, its resolution (`scale`: the
// game's size over its own, 1 full) and Denoiser (the shaders read them from the settings too); returns the noise's frame
// for the shader (0-63; 0 without Temporal Smoothing: the noise stands still).
inline float Update(bool bitmask, bool temporal, float scale, bool denoise) {
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.bitmask = bitmask;
  internal::runtime.temporal = bitmask && temporal;
  internal::runtime.scale = std::max(scale, 1.f);
  internal::runtime.denoise = bitmask && denoise;
  // Off: its memory and its depth levels' back (made again with its next pass).
  if (!bitmask && internal::runtime.objects.device) {
    internal::runtime.objects = {};
    const std::unique_lock levels_lock(depth_levels::internal::runtime.mutex);
    depth_levels::internal::runtime.objects = {};
  }
  ++internal::runtime.frame;
  return internal::runtime.temporal ? static_cast<float>(internal::runtime.frame % 64) : 0.f;
}

// The game's AO passes (`draws`: the addon's, kept for as long as it is installed) and the addon's shader settings
// (`settings`, `settings_size` bytes: its shader_injection, for its own shaders). After renodx::mods::shader::Use: the
// replacements' own callbacks (their injection) first.
inline void Install(std::span<const AmbientOcclusionDraw> draws, const void* settings, UINT settings_size) {
  {
    const std::unique_lock lock(internal::runtime.mutex);
    internal::runtime.settings = settings;
    internal::runtime.settings_size = settings_size;
  }
  for (const AmbientOcclusionDraw& draw : draws) {
    renodx::utils::command_action::Register(
        OnAmbientOcclusionDraw,
        {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW},
        &draw);
  }
}

// The game's device is going away: the addon's objects on it with it (the depth levels' too).
inline void OnDestroyDevice(reshade::api::device* device) {
  depth_levels::OnDestroyDevice(device);
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  if (runtime.objects.device.Get() != native) return;
  runtime.objects = {};
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(OnAmbientOcclusionDraw);
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  runtime.objects = {};
}

}  // namespace twattila::ao_pass
