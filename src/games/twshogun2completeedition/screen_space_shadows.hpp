/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/frame_history.hpp"
#include "../twattila/light_bounce.hpp"
#include "./shared.h"

// Attila's SSS Softness and Debug View (Experimental; the DLSS addon's ScreenSpaceShadowsSoftness / DebugView) for Shogun 2's
// own screen-space shadows (sss00 / sss01, Bend's, and Remastered CE's builds), whose pass writes the sun's mask with the
// cascades in it. With either on, the pass gets this module's texture at u1: shaders/sss.hlsli keeps the cascades there and
// gives Bend 1 for them, so the mask holds Bend's shadows alone. At the first sun light draw after it, sss_soft_mask puts the
// darker of the cascades and Bend's shadows (softened by Softness, on the same surface) back into the mask, from a copy of it
// that the tone mappers show with the Debug View (shaders/tonemap.hlsli). Softer Away from the Caster (ScreenSpaceShadows-
// DistanceSoftness) also swaps in the pass's extended version (sss_distance*: Bend's FindHit), which writes each shadowed
// pixel's penumbra there for sss_soft_mask. Light Through Banners (ScreenSpaceShadowsBannerLight) swaps it in too, with the
// banners' own depth (Attila's InstallThin: the cloth's G-buffer draws drawn again), and Coloured Shadows (ScreenSpace-
// ShadowsColoured) with their colour filter, which the sun light draws read at t50 where the rays bound none. All off:
// nothing bound, the game's pass.
namespace twshogun2::screen_space_shadows {

using Microsoft::WRL::ComPtr;

namespace internal {

constexpr UINT THIN_SLOT = 5;    // sss.hlsli t_sss_thin
constexpr UINT COLOUR_SLOT = 6;  // t_sss_colour
constexpr UINT TINT_TARGET_SLOT = 2;  // t_rw_sss_tint
constexpr UINT TINT_SLOT = 50;   // the sun light draws' (rt_sun.hlsli t_rt_tint)

struct State {
  std::mutex mutex;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> shader;
  ComPtr<ID3D11ComputeShader> distance_cascades;  // sss00's extended version
  ComPtr<ID3D11ComputeShader> distance_plain;     // sss01's
  ComPtr<ID3D11ComputeShader> game_pass;          // the pass's own (or Light Through Foliage's), put back after it
  ComPtr<ID3D11Buffer> constants;
  ComPtr<ID3D11Buffer> pass_constants;  // the pass's sss constants (b2 / b1) copied at it: sss_soft_mask's b1 (X30)
  ComPtr<ID3D11Texture2D> split;  // R8G8B8A8: the cascades (r), the Screen Edge share (b)
  ComPtr<ID3D11UnorderedAccessView> split_target;
  ComPtr<ID3D11ShaderResourceView> split_view;
  ComPtr<ID3D11Texture2D> bend;  // the mask's copy: Bend's shadows alone
  ComPtr<ID3D11ShaderResourceView> bend_view;
  D3D11_TEXTURE2D_DESC mask_desc = {};
  ComPtr<ID3D11UnorderedAccessView> mask;   // the pass's u0
  ComPtr<ID3D11ShaderResourceView> depth;   // the pass's depth (Bend's)
  // Light Through Banners: the banners' depth (R32F at the scene depth's size, the far plane where none), drawn by sss_thin
  // against the game's depth with this depth state and its rasterizer states a little nearer (each kept with the game's it
  // copies); the G-buffer's colour from the same draws (their target 1); Coloured Shadows' filter at the mask's size.
  ComPtr<ID3D11PixelShader> thin_pixel_shader;
  ComPtr<ID3D11DepthStencilState> thin_depth_state;
  std::unordered_map<ID3D11RasterizerState*, std::pair<ComPtr<ID3D11RasterizerState>, ComPtr<ID3D11RasterizerState>>> thin_rasterizers;
  ComPtr<ID3D11Texture2D> thin;
  ComPtr<ID3D11RenderTargetView> thin_target;
  ComPtr<ID3D11ShaderResourceView> thin_view;
  ComPtr<ID3D11ShaderResourceView> colour;
  ComPtr<ID3D11Texture2D> tint;
  ComPtr<ID3D11UnorderedAccessView> tint_target;
  ComPtr<ID3D11ShaderResourceView> tint_view;
  float softness = 0.f;
  float banner_light = 0.f;  // the rows as the last pass had them (the cloth draws come before it)
  bool coloured = false;
  bool distance = false;
  bool split_bound = false;   // during the pass
  bool thin_bound = false;    // during the pass
  bool tint_bound = false;    // during the pass
  bool written = false;       // a pass wrote the split since the last sun draw
  bool thin_open = false;     // the banners' depth holds this frame's draws (cleared at the first, closed at a sun draw)
  bool tint_cleared = false;  // this frame's first pass cleared the filter
  bool tint_ready = false;    // a pass wrote the filter this frame
  bool debug_bound = false;   // at a tone mapper
  bool debug_ready = false;   // this frame's copy made
  bool failed = false;
};
inline State state;

inline void ReleaseObjects() {
  state.device.Reset();
  state.private_state.Reset();
  state.shader.Reset();
  state.distance_cascades.Reset();
  state.distance_plain.Reset();
  state.game_pass.Reset();
  state.constants.Reset();
  state.pass_constants.Reset();
  state.split.Reset();
  state.split_target.Reset();
  state.split_view.Reset();
  state.bend.Reset();
  state.bend_view.Reset();
  state.mask_desc = {};
  state.mask.Reset();
  state.depth.Reset();
  state.thin_pixel_shader.Reset();
  state.thin_depth_state.Reset();
  state.thin_rasterizers.clear();
  state.thin.Reset();
  state.thin_target.Reset();
  state.thin_view.Reset();
  state.colour.Reset();
  state.tint.Reset();
  state.tint_target.Reset();
  state.tint_view.Reset();
  state.written = false;
  state.thin_open = false;
  state.tint_ready = false;
  state.debug_ready = false;
  state.failed = false;
}

// The objects on `native`'s device (made again for a new one); false: they failed.
inline bool PrepareDevice(ID3D11DeviceContext* native) {
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (state.device != device) {
    ReleaseObjects();
    state.device = device;
    ComPtr<ID3D11Device1> device1;
    const auto feature_level = device->GetFeatureLevel();
    const UINT state_flags = ((device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0)
                                 ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                                 : 0;
    const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_DEPTH_STENCIL_DESC thin_depth_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO,
                                                      .DepthFunc = D3D11_COMPARISON_LESS_EQUAL};
    state.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                   || FAILED(device1->CreateDeviceContextState(state_flags, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                                               nullptr, state.private_state.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__sss_soft_mask.data(), __sss_soft_mask.size(), nullptr, state.shader.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__sss_distance_cascades.data(), __sss_distance_cascades.size(), nullptr,
                                                         state.distance_cascades.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__sss_distance.data(), __sss_distance.size(), nullptr, state.distance_plain.GetAddressOf()))
                   || FAILED(device->CreateBuffer(&constants_desc, nullptr, state.constants.GetAddressOf()))
                   || FAILED(device->CreatePixelShader(__sss_thin.data(), __sss_thin.size(), nullptr, state.thin_pixel_shader.GetAddressOf()))
                   || FAILED(device->CreateDepthStencilState(&thin_depth_desc, state.thin_depth_state.GetAddressOf()));
  }
  return !state.failed;
}

// Each sun light draw: the frame's banners' depth closed (the cloth draws come before the pass, the pass before the sun
// draws). The first after a pass with the split: the mask copied, then the cascades and Bend's (softened) shadows put back
// into it. Coloured Shadows' filter at t50 where the rays bound none (their callback runs first) and this draw reads the mask
// the pass wrote (t_shadow, the G-buffer's fifth slot); taken off after.
constexpr auto OnMaskSunDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  const auto& draw = command.template GetCallbackData<twattila::light_bounce::SunDraw>();
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(state.mutex);
  state.thin_open = false;
  state.tint_cleared = false;
  if (native == nullptr) return {};
  if (state.written) {
    state.written = false;
    ComPtr<ID3D11DeviceContext1> native1;
    ComPtr<ID3D11Resource> mask_resource;
    state.mask->GetResource(mask_resource.GetAddressOf());
    D3D11_VIEWPORT viewport = {};
    UINT viewport_count = 1;
    native->RSGetViewports(&viewport_count, &viewport);
    if (viewport_count != 0 && SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) {
      const talesofarise::dlss::ContextStateScope scope(native1.Get(), state.private_state.Get());
      native->CopyResource(state.bend.Get(), mask_resource.Get());
      native->UpdateSubresource(state.constants.Get(), 0, nullptr, std::array{viewport.Width, viewport.Height, state.softness,
                                                                             state.distance ? SSS_MOST_PENUMBRA_2160 * viewport.Height / 2160.f : 0.f}.data(), 0, 0);
      ID3D11ShaderResourceView* sources[] = {state.depth.Get(), state.bend_view.Get(), state.split_view.Get()};
      native->CSSetShader(state.shader.Get(), nullptr, 0);
      const std::array<ID3D11Buffer*, 2> constants = {state.constants.Get(), state.pass_constants.Get()};
      native->CSSetConstantBuffers(0, 2, constants.data());
      native->CSSetShaderResources(0, 3, sources);
      native->CSSetUnorderedAccessViews(0, 1, state.mask.GetAddressOf(), nullptr);
      native->Dispatch((static_cast<UINT>(viewport.Width) + 7) / 8, (static_cast<UINT>(viewport.Height) + 7) / 8, 1);
      native->ClearState();
      state.debug_ready = true;
    }
  }
  if (!state.tint_ready || !state.mask) return {};
  ComPtr<ID3D11ShaderResourceView> bound;
  ComPtr<ID3D11ShaderResourceView> shadow;
  native->PSGetShaderResources(TINT_SLOT, 1, bound.GetAddressOf());
  native->PSGetShaderResources(draw.gbuffer_slot + 4, 1, shadow.GetAddressOf());
  if (bound || !shadow) return {};
  ComPtr<ID3D11Resource> shadow_resource;
  ComPtr<ID3D11Resource> mask_resource;
  shadow->GetResource(shadow_resource.GetAddressOf());
  state.mask->GetResource(mask_resource.GetAddressOf());
  if (shadow_resource != mask_resource) return {};
  native->PSSetShaderResources(TINT_SLOT, 1, state.tint_view.GetAddressOf());
  return {.finalize_callback = [](Context& drawn, const void*) {
    auto* native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&native);
    ID3D11ShaderResourceView* none = nullptr;
    if (native != nullptr) native->PSSetShaderResources(TINT_SLOT, 1, &none);
  }};
};

// Light Through Banners (Attila's OnThinDraw): a cloth G-buffer draw (banners: 3 or 5 targets, the scene's depth) drawn
// again after it (finalize: also when another callback drew it) by sss_thin into the banners' depth (cleared to the far plane
// at the frame's first), against the game's depth (less-equal, a little nearer, not written), the game's bindings put back
// after. Pixels later draws cover keep a depth the scene's no longer is: the march takes a pixel as a banner's only where both
// are the same. Its target 1 is the G-buffer's colour (the sun draws' and sss00's, 2026-11-14 traces) for Coloured Shadows.
constexpr auto OnBannerDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  using Arguments = typename Context::ArgumentType;
  if constexpr (!std::is_same_v<Arguments, renodx::utils::command_action::DrawArguments>
                && !std::is_same_v<Arguments, renodx::utils::command_action::DrawIndexedArguments>) {
    return {};
  } else {
    {
      const std::unique_lock lock(state.mutex);
      if (state.banner_light <= 0.f) return {};
    }
    return {.finalize_callback = [](Context& drawn, const void*) {
      auto* native = reinterpret_cast<ID3D11DeviceContext*>(drawn.cmd_list->get_native());
      renodx::utils::directx::TryNativeFromReShadeProxy(&native);
      if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
      const std::unique_lock lock(state.mutex);
      // The game's targets, depth and output views (a draw writing views is left alone: setting targets drops them).
      std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> game_targets = {};
      ComPtr<ID3D11DepthStencilView> depth_stencil;
      std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> game_output_views = {};
      native->OMGetRenderTargetsAndUnorderedAccessViews(static_cast<UINT>(game_targets.size()), game_targets.data(), depth_stencil.GetAddressOf(), 0,
                                                        static_cast<UINT>(game_output_views.size()), game_output_views.data());
      const bool output_views = std::ranges::any_of(game_output_views, [](auto* view) { return view != nullptr; });
      for (auto* view : game_output_views) {
        if (view != nullptr) {
          view->Release();
        }
      }
      const auto release_targets = [&] {
        for (auto* target : game_targets) {
          if (target != nullptr) {
            target->Release();
          }
        }
      };
      ComPtr<ID3D11Resource> depth_resource;
      if (depth_stencil) {
        depth_stencil->GetResource(depth_resource.GetAddressOf());
      }
      ComPtr<ID3D11Texture2D> depth_texture;
      D3D11_TEXTURE2D_DESC depth_desc = {};
      if (depth_resource && SUCCEEDED(depth_resource.As(&depth_texture))) {
        depth_texture->GetDesc(&depth_desc);
      }
      if (output_views || game_targets[1] == nullptr || game_targets[2] == nullptr || depth_desc.Width == 0 || depth_desc.ArraySize != 1
          || depth_desc.SampleDesc.Count != 1 || !PrepareDevice(native)) {
        release_targets();
        return;
      }
      D3D11_TEXTURE2D_DESC thin_desc = {};
      if (state.thin) {
        state.thin->GetDesc(&thin_desc);
      }
      if (thin_desc.Width != depth_desc.Width || thin_desc.Height != depth_desc.Height) {
        state.thin.Reset();
        state.thin_target.Reset();
        state.thin_view.Reset();
        const D3D11_TEXTURE2D_DESC desc = {.Width = depth_desc.Width, .Height = depth_desc.Height, .MipLevels = 1, .ArraySize = 1,
                                           .Format = DXGI_FORMAT_R32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                           .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE};
        if (FAILED(state.device->CreateTexture2D(&desc, nullptr, state.thin.GetAddressOf()))
            || FAILED(state.device->CreateRenderTargetView(state.thin.Get(), nullptr, state.thin_target.GetAddressOf()))
            || FAILED(state.device->CreateShaderResourceView(state.thin.Get(), nullptr, state.thin_view.GetAddressOf()))) {
          state.thin.Reset();
          state.thin_target.Reset();
          state.thin_view.Reset();
          release_targets();
          return;
        }
      }
      // Coloured Shadows: a view of the G-buffer's colour (made again when the game's target changes; none: no filter).
      if (state.coloured) {
        ComPtr<ID3D11Resource> colour_resource;
        game_targets[1]->GetResource(colour_resource.GetAddressOf());
        ComPtr<ID3D11Resource> viewed;
        if (state.colour) {
          state.colour->GetResource(viewed.GetAddressOf());
        }
        if (viewed != colour_resource) {
          state.colour.Reset();
          if (FAILED(state.device->CreateShaderResourceView(colour_resource.Get(), nullptr, state.colour.GetAddressOf()))) {
            state.colour.Reset();
          }
        }
      }
      // The game's rasterizer state with the replay a little nearer: 4 steps of a 24-bit depth (within the march's match).
      ComPtr<ID3D11RasterizerState> game_rasterizer;
      native->RSGetState(game_rasterizer.GetAddressOf());
      auto& [held, nearer] = state.thin_rasterizers[game_rasterizer.Get()];
      if (!nearer) {
        D3D11_RASTERIZER_DESC rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_BACK, .DepthClipEnable = TRUE};
        if (game_rasterizer) {
          game_rasterizer->GetDesc(&rasterizer_desc);
        }
        rasterizer_desc.DepthBias -= 4;
        held = game_rasterizer;
        if (FAILED(state.device->CreateRasterizerState(&rasterizer_desc, nearer.GetAddressOf()))) {
          release_targets();
          return;
        }
      }
      if (!state.thin_open) {
        constexpr std::array<FLOAT, 4> FAR_PLANE = {1.f, 1.f, 1.f, 1.f};
        native->ClearRenderTargetView(state.thin_target.Get(), FAR_PLANE.data());
        state.thin_open = true;
      }
      ComPtr<ID3D11PixelShader> game_pixel_shader;
      native->PSGetShader(game_pixel_shader.GetAddressOf(), nullptr, nullptr);
      ComPtr<ID3D11BlendState> game_blend;
      std::array<FLOAT, 4> blend_factor = {};
      UINT sample_mask = 0;
      native->OMGetBlendState(game_blend.GetAddressOf(), blend_factor.data(), &sample_mask);
      ComPtr<ID3D11DepthStencilState> game_depth_state;
      UINT stencil_reference = 0;
      native->OMGetDepthStencilState(game_depth_state.GetAddressOf(), &stencil_reference);

      native->OMSetRenderTargets(1, state.thin_target.GetAddressOf(), depth_stencil.Get());
      native->PSSetShader(state.thin_pixel_shader.Get(), nullptr, 0);
      native->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
      native->OMSetDepthStencilState(state.thin_depth_state.Get(), 0);
      native->RSSetState(nearer.Get());
      twattila::frame_history::DrawAgain(native, drawn.arguments);

      native->OMSetRenderTargets(static_cast<UINT>(game_targets.size()), game_targets.data(), depth_stencil.Get());
      native->PSSetShader(game_pixel_shader.Get(), nullptr, 0);
      native->OMSetBlendState(game_blend.Get(), blend_factor.data(), sample_mask);
      native->OMSetDepthStencilState(game_depth_state.Get(), stencil_reference);
      native->RSSetState(game_rasterizer.Get());
      release_targets();
    }};
  }
};

}  // namespace internal

// The screen-space shadow pass (Bend's depth at `depth_slot`: 3 with the cascades, 0 without; after foliage_light's): with
// Softness (render pixels), the Debug View or Softer Away from the Caster (the sun's degrees) on, the split texture bound at
// u1; with the last or Light Through Banners (the sun's share through them) and the frame's banners drawn, the pass's
// extended version, with their depth at t5 and, with Coloured Shadows, the G-buffer's colour at t6 and the filter at u2.
inline void OnShadowPass(reshade::api::command_list* cmd_list, UINT depth_slot, float softness, float debug_view, float distance,
                         float banner_light, bool coloured) {
  auto& state = internal::state;
  const bool split = softness > 0.f || debug_view > 0.f || distance > 0.f;
  {
    // A row turned off: what only it uses let go (made again when it is turned on).
    const std::unique_lock lock(state.mutex);
    state.banner_light = banner_light;
    state.coloured = coloured;
    if (banner_light <= 0.f) {
      state.thin.Reset();
      state.thin_target.Reset();
      state.thin_view.Reset();
      state.thin_rasterizers.clear();
      state.thin_open = false;
    }
    if (banner_light <= 0.f || !coloured) {
      state.colour.Reset();
      state.tint.Reset();
      state.tint_target.Reset();
      state.tint_view.Reset();
      state.tint_ready = false;
    }
  }
  if (!split && banner_light <= 0.f) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr) return;
  const std::unique_lock lock(state.mutex);
  ComPtr<ID3D11UnorderedAccessView> mask;
  native->CSGetUnorderedAccessViews(0, 1, mask.GetAddressOf());
  ComPtr<ID3D11ShaderResourceView> depth;
  native->CSGetShaderResources(depth_slot, 1, depth.GetAddressOf());
  if (!mask || !depth) return;
  ComPtr<ID3D11Resource> resource;
  mask->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  D3D11_TEXTURE2D_DESC desc = {};
  D3D11_UNORDERED_ACCESS_VIEW_DESC mask_view_desc = {};
  mask->GetDesc(&mask_view_desc);
  if (FAILED(resource.As(&texture))) return;
  texture->GetDesc(&desc);
  if (desc.SampleDesc.Count != 1 || desc.MipLevels != 1 || desc.ArraySize != 1 || !internal::PrepareDevice(native)) return;
  // The split textures at the mask's size and format (the copy viewed as its UAV's).
  if (split && !(state.split && state.mask_desc.Width == desc.Width && state.mask_desc.Height == desc.Height && state.mask_desc.Format == desc.Format)) {
    state.split.Reset();
    state.split_target.Reset();
    state.split_view.Reset();
    state.bend.Reset();
    state.bend_view.Reset();
    state.mask_desc = {};
    const D3D11_TEXTURE2D_DESC split_desc = {.Width = desc.Width, .Height = desc.Height, .MipLevels = 1, .ArraySize = 1,
                                             .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                             .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
    const D3D11_TEXTURE2D_DESC bend_desc = {.Width = desc.Width, .Height = desc.Height, .MipLevels = 1, .ArraySize = 1, .Format = desc.Format,
                                            .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SHADER_RESOURCE_VIEW_DESC bend_view_desc = {.Format = mask_view_desc.Format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                            .Texture2D = {.MipLevels = 1}};
    if (FAILED(state.device->CreateTexture2D(&split_desc, nullptr, state.split.GetAddressOf()))
        || FAILED(state.device->CreateUnorderedAccessView(state.split.Get(), nullptr, state.split_target.GetAddressOf()))
        || FAILED(state.device->CreateShaderResourceView(state.split.Get(), nullptr, state.split_view.GetAddressOf()))
        || FAILED(state.device->CreateTexture2D(&bend_desc, nullptr, state.bend.GetAddressOf()))
        || FAILED(state.device->CreateShaderResourceView(state.bend.Get(), &bend_view_desc, state.bend_view.GetAddressOf()))) {
      state.split.Reset();
      return;
    }
    state.mask_desc = desc;
  }
  // The view's 2D size (0 x 0: none, or not a 2D texture).
  const auto texture_size = [](ID3D11View* view) -> std::array<UINT, 2> {
    ComPtr<ID3D11Resource> viewed;
    ComPtr<ID3D11Texture2D> viewed_texture;
    if (view != nullptr) view->GetResource(viewed.GetAddressOf());
    if (!viewed || FAILED(viewed.As(&viewed_texture))) return {};
    D3D11_TEXTURE2D_DESC viewed_desc = {};
    viewed_texture->GetDesc(&viewed_desc);
    return {viewed_desc.Width, viewed_desc.Height};
  };
  // The banners drawn this frame at the depth's size; their colour's filter at the mask's.
  const bool thin = banner_light > 0.f && state.thin_open && texture_size(depth.Get()) == texture_size(state.thin_view.Get());
  bool tint = thin && coloured && state.colour;
  if (tint && texture_size(state.tint_view.Get()) != std::array{desc.Width, desc.Height}) {
    state.tint.Reset();
    state.tint_target.Reset();
    state.tint_view.Reset();
    const D3D11_TEXTURE2D_DESC tint_desc = {.Width = desc.Width, .Height = desc.Height, .MipLevels = 1, .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                            .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
    tint = SUCCEEDED(state.device->CreateTexture2D(&tint_desc, nullptr, state.tint.GetAddressOf()))
           && SUCCEEDED(state.device->CreateUnorderedAccessView(state.tint.Get(), nullptr, state.tint_target.GetAddressOf()))
           && SUCCEEDED(state.device->CreateShaderResourceView(state.tint.Get(), nullptr, state.tint_view.GetAddressOf()));
    state.tint_cleared = false;
  }
  if (!split && !thin) return;
  if (split) {
    // X30: the pass's sss constants (shaders/sss.hlsli: b2 with the cascades, else b1), whose light coordinate's z (the
    // depth at infinity) sss_soft_mask's same-surface test reads; copied, the game may write them again before it runs.
    ComPtr<ID3D11Buffer> pass_constants;
    native->CSGetConstantBuffers((depth_slot != 0) ? 2 : 1, 1, pass_constants.GetAddressOf());
    D3D11_BUFFER_DESC pass_desc = {};
    if (pass_constants) pass_constants->GetDesc(&pass_desc);
    D3D11_BUFFER_DESC copy_desc = {};
    if (state.pass_constants) state.pass_constants->GetDesc(&copy_desc);
    if (pass_constants && copy_desc.ByteWidth != pass_desc.ByteWidth) {
      state.pass_constants.Reset();
      const D3D11_BUFFER_DESC desc = {.ByteWidth = pass_desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      if (FAILED(state.device->CreateBuffer(&desc, nullptr, state.pass_constants.GetAddressOf()))) state.pass_constants.Reset();
    }
    if (!pass_constants || !state.pass_constants) return;
    native->CopyResource(state.pass_constants.Get(), pass_constants.Get());
    native->CSSetUnorderedAccessViews(1, 1, state.split_target.GetAddressOf(), nullptr);
  }
  if (thin) {
    native->CSSetShaderResources(internal::THIN_SLOT, 1, state.thin_view.GetAddressOf());
  }
  if (tint) {
    // Bend leaves the pixels it skips (the sky) as they were: cleared at the frame's first pass.
    if (!state.tint_cleared) {
      constexpr std::array<FLOAT, 4> NONE = {};
      native->ClearUnorderedAccessViewFloat(state.tint_target.Get(), NONE.data());
      state.tint_cleared = true;
    }
    native->CSSetShaderResources(internal::COLOUR_SLOT, 1, state.colour.GetAddressOf());
    native->CSSetUnorderedAccessViews(internal::TINT_TARGET_SLOT, 1, state.tint_target.GetAddressOf(), nullptr);
  }
  state.mask = std::move(mask);
  state.depth = std::move(depth);
  state.softness = softness;
  state.distance = distance > 0.f;
  state.split_bound = split;
  state.thin_bound = thin;
  state.tint_bound = tint;
  state.game_pass.Reset();
  if (state.distance || thin) {
    native->CSGetShader(state.game_pass.GetAddressOf(), nullptr, nullptr);
    native->CSSetShader((depth_slot != 0) ? state.distance_cascades.Get() : state.distance_plain.Get(), nullptr, 0);
  }
}

// After the pass (before foliage_light's): what it bound taken off, the split written; the pass's shader put back.
inline void OnShadowPassDone(reshade::api::command_list* cmd_list) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (!state.split_bound && !state.thin_bound) return;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  state.written = state.written || state.split_bound;
  state.tint_ready = state.tint_ready || state.tint_bound;
  if (native != nullptr) {
    ID3D11UnorderedAccessView* no_target = nullptr;
    ID3D11ShaderResourceView* no_view = nullptr;
    if (state.split_bound) native->CSSetUnorderedAccessViews(1, 1, &no_target, nullptr);
    if (state.thin_bound) native->CSSetShaderResources(internal::THIN_SLOT, 1, &no_view);
    if (state.tint_bound) {
      native->CSSetShaderResources(internal::COLOUR_SLOT, 1, &no_view);
      native->CSSetUnorderedAccessViews(internal::TINT_TARGET_SLOT, 1, &no_target, nullptr);
    }
    if (state.game_pass) native->CSSetShader(state.game_pass.Get(), nullptr, 0);
  }
  state.split_bound = false;
  state.thin_bound = false;
  state.tint_bound = false;
  state.game_pass.Reset();
}

// A tone mapper's draw: with the Debug View on, this frame's copy and split bound at t60 / t61; unbound after it.
inline void OnTonemap(reshade::api::command_list* cmd_list, float debug_view) {
  auto& state = internal::state;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(state.mutex);
  if (debug_view <= 0.f || !state.debug_ready || native == nullptr) return;
  ID3D11ShaderResourceView* views[] = {state.bend_view.Get(), state.split_view.Get()};
  native->PSSetShaderResources(60, 2, views);
  state.debug_bound = true;
}

// The frame's end: its banners and their filter done with.
inline void OnTonemapDrawn(reshade::api::command_list* cmd_list) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  state.debug_ready = false;
  state.thin_open = false;
  state.tint_ready = false;
  if (!state.debug_bound) return;
  state.debug_bound = false;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  ID3D11ShaderResourceView* none[] = {nullptr, nullptr};
  if (native != nullptr) native->PSSetShaderResources(60, 2, none);
}

// The sun light draws (`sun_draws`: kept while installed) and the cloth's G-buffer vertex shader (Light Through Banners).
inline void Install(std::span<const twattila::light_bounce::SunDraw> sun_draws, uint32_t cloth_vertex_shader) {
  for (const auto& draw : sun_draws) {
    renodx::utils::command_action::Register(
        internal::OnMaskSunDraw, {.shader_hash = draw.shader_hash, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW}, &draw);
  }
  renodx::utils::command_action::Register(
      internal::OnBannerDraw, {.shader_hash = cloth_vertex_shader, .command_types = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW});
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnMaskSunDraw);
  renodx::utils::command_action::Unregister(internal::OnBannerDraw);
}

inline void OnDestroyDevice(reshade::api::device*) {
  const std::unique_lock lock(internal::state.mutex);
  internal::ReleaseObjects();
}

}  // namespace twshogun2::screen_space_shadows
