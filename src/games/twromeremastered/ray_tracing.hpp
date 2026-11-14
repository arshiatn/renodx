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
#include <mutex>
#include <shared_mutex>

#include <embed/shaders.h>
#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../talesofarise_dlss/native_state.hpp"
#include "../twattila/rt_core.hpp"
#include "../twattila/rt_scene.hpp"
#include "./dlaa.hpp"
#include "./trace_reads.hpp"

// Ray tracing (Graphics (Experimental), off; tools\tw\romeremastered\plan_1114.md items 6 / 6b): Rome Remastered's scene
// through the Total War helper (twattila/rt_core.hpp; renodx-tw-dlaa64.exe next to the addon, its own D3D12 device with
// DXR). ATN's 2026-11-14 traces (level A): a depth prepass into the scene's D32 depth (no target, depth written) draws the
// terrain, buildings, trees and units with the main camera (VS b5), the cascades go into their own D16 atlas. The scene:
// those prepass draws (the units, the grass, the far trees' camera-facing cards and the terrain's coplanar overlays left
// out: the game's cascades keep shadowing them) streamed out through the game's own vertex shaders every frame (Feral's
// object matrix at VS b7 is one buffer written again for each draw: a kept capture's key would stand for every object of
// its mesh). At ASSAO's apply (ps 6A73BA10, before the cascades; the shadow mask's resolve if it has none) the sun from
// the frame constants (PS b8 c2: rt_frame_sun) and the trace from the scene's depth and the main camera (dlaa.hpp keeps
// both); after the apply the traced ambient visibility multiplies ASSAO's target, after the resolve (CSMResolvePS) the
// traced sun visibility multiplies the mask's red (rt_multiply): the game's shadows and AO stay, the rays add theirs. The
// resolve's compute form (CSMResolveCS, not seen in a trace) is left as it is. Its scene shaders' code is kept from the
// game's start: Install only when a row is on at launch.
namespace twromeremastered::ray_tracing {

using Microsoft::WRL::ComPtr;

// The rows (rt_scene::AppendSettings', on the graphics page; restart after turning sun shadows or AO on).
static twattila::rt_scene::Sliders sliders;
// Ray Tracing Battle Objects (Test): the untested draws let in (SceneDraw): 0 none, 1 solid ones, 2 all (v11).
static float battle_scene = 0.f;

namespace internal {

// Left out of the scene: the terrain's overlays (decals on it, coplanar), the far trees' cards (VegetationBillboard), the
// grass (dx11_*.hlsl names in the dump), the units' banners (UnitBanner: their own matrix at b0, not the main camera the
// captures' clip positions go back to the world with).
constexpr std::array<uint32_t, 21> LEFT_OUT_VERTEX_SHADERS = {
    0x125EEF55, 0x7B3C7C53,                                                                          // Terrain VSMain_Overlay
    0x06B8B162, 0x877C08D1,                                                                          // VegetationBillboard
    0x042BA9A4, 0x04E771DF, 0x1839A2B1, 0x4924C5B3, 0x52F0A4C0, 0x5BEFF427, 0x5ED0AC07, 0x5FCD386C,  // Grass
    0x66EEA96B, 0x671B29EC, 0x826801BF, 0x94FB8B52, 0xA89883E8, 0xCC8EBFC5, 0xDAE92850, 0xEB0971B3,
    0xB6E1E42E,                                                                                      // UnitBanner
};

// Traced without a GPU reset (ATN's 2026-11-14 run at 67%: 1.5 min of the campaign map, its terrain, trees and
// buildings). The same run's battle reset the helper's device twice within seconds of its start (its own terrain, trees,
// props and alpha-tested plants in the scene, none of them in the campaign's); the others stay out unless
// battle_scene lets them in.
constexpr std::array<uint32_t, 4> TESTED_VERTEX_SHADERS = {
    0x8A00B527,  // AerialMap
    0xCDDA64F8,  // AerialMapTrees
    0x9594545F,  // PBR
    0x77472F17,  // BasicLit
};

struct Objects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3D11ComputeShader> sun;
  ComPtr<ID3D11VertexShader> full_screen;
  ComPtr<ID3D11PixelShader> multiply;
  ComPtr<ID3D11BlendState> multiply_red;   // the target's red x the source
  ComPtr<ID3D11BlendState> unshadow_red;   // the target's red x (1 - the source): an occlusion (contact_shadows.hpp)
  ComPtr<ID3D11RasterizerState> no_cull;
  bool failed = false;
};

struct State {
  std::shared_mutex mutex;
  Objects objects;  // on the game's device
};
static State state;
static bool installed = false;

// A prepass draw into the scene's depth (no target, depth written), its vertex shader not left out: its pixel shader's
// coverage (rt_core's SceneCoverage), captured every frame; an untested one's only as battle_scene lets it in.
inline bool SceneDraw(ID3D11DeviceContext* native, twattila::rt_scene::SceneShader* shader) {
  if (std::ranges::find(trace_reads::UNIT_VERTEX_SHADERS, shader->hash) != trace_reads::UNIT_VERTEX_SHADERS.end()
      || std::ranges::find(LEFT_OUT_VERTEX_SHADERS, shader->hash) != LEFT_OUT_VERTEX_SHADERS.end()) {
    return false;
  }
  ComPtr<ID3D11RenderTargetView> target;
  ComPtr<ID3D11DepthStencilView> depth_view;
  native->OMGetRenderTargets(1, target.GetAddressOf(), depth_view.GetAddressOf());
  if (target || !depth_view) return false;
  const ComPtr<ID3D11ShaderResourceView> scene_depth = dlaa::SceneDepth();
  if (!scene_depth) return false;
  ComPtr<ID3D11Resource> depth_resource;
  depth_view->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Resource> scene_resource;
  scene_depth->GetResource(scene_resource.GetAddressOf());
  if (depth_resource != scene_resource) return false;
  ComPtr<ID3D11DepthStencilState> depth_state;
  UINT stencil_reference = 0;
  native->OMGetDepthStencilState(depth_state.GetAddressOf(), &stencil_reference);
  D3D11_DEPTH_STENCIL_DESC depth_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL};
  if (depth_state) depth_state->GetDesc(&depth_desc);
  if (!depth_desc.DepthEnable || depth_desc.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ALL) return false;
  if (!twattila::rt_scene::SceneCoverage(native, shader)) return false;
  if (std::ranges::find(TESTED_VERTEX_SHADERS, shader->hash) == TESTED_VERTEX_SHADERS.end() && battle_scene < (shader->foliage ? 2.f : 1.f)) {
    return false;
  }
  shader->dynamic = true;
  return true;
}

inline void OnPresent(reshade::api::command_queue*, reshade::api::swapchain*, const reshade::api::rect*, const reshade::api::rect*,
                      uint32_t, const reshade::api::rect*) {
  // The rows as Attila's (they need an upscaler: DLSS), the render's scale the game's.
  const twattila::rt_scene::Settings settings = twattila::rt_scene::SettingsOf(sliders, dlaa::sliders, 0.f, dlaa::RenderScale());
  dlaa::rays = settings.sun_shadows || settings.ambient_occlusion;
  twattila::rt_scene::Update(settings);
}

// The game's `device` is going away: the objects on it with it (OnDestroyDevice, contact_shadows.hpp).
inline void ReleaseObjects(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(state.mutex);
  if (state.objects.device.Get() == native) state.objects = {};
}

inline void OnDestroyDevice(reshade::api::device* device) {
  twattila::rt_scene::OnDestroyDevice(device);
  ReleaseObjects(device);
}

// The objects on the game's `device` (made again when it changes; Trace, contact_shadows.hpp); false: not available.
inline bool MakeObjects(ID3D11Device* device) {
  const std::unique_lock lock(state.mutex);
  auto& objects = state.objects;
  if (objects.device.Get() == device) return !objects.failed;
  objects = {.device = device};
  ComPtr<ID3D11Device1> device1;
  const D3D_FEATURE_LEVEL feature_level = device->GetFeatureLevel();
  D3D11_BLEND_DESC blend_desc = {};
  blend_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ZERO, .DestBlend = D3D11_BLEND_SRC_COLOR,
                                .BlendOp = D3D11_BLEND_OP_ADD, .SrcBlendAlpha = D3D11_BLEND_ZERO, .DestBlendAlpha = D3D11_BLEND_ONE,
                                .BlendOpAlpha = D3D11_BLEND_OP_ADD, .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED};
  D3D11_BLEND_DESC unshadow_desc = blend_desc;
  unshadow_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_COLOR;
  const D3D11_RASTERIZER_DESC raster_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE, .DepthClipEnable = TRUE};
  objects.failed = FAILED(device->QueryInterface(IID_PPV_ARGS(device1.GetAddressOf())))
                   || FAILED(device1->CreateDeviceContextState(0, &feature_level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                                               objects.private_state.GetAddressOf()))
                   || FAILED(device->CreateComputeShader(__rt_frame_sun.data(), __rt_frame_sun.size(), nullptr, objects.sun.GetAddressOf()))
                   || FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr, objects.full_screen.GetAddressOf()))
                   || FAILED(device->CreatePixelShader(__rt_multiply.data(), __rt_multiply.size(), nullptr, objects.multiply.GetAddressOf()))
                   || FAILED(device->CreateBlendState(&blend_desc, objects.multiply_red.GetAddressOf()))
                   || FAILED(device->CreateBlendState(&unshadow_desc, objects.unshadow_red.GetAddressOf()))
                   || FAILED(device->CreateRasterizerState(&raster_desc, objects.no_cull.GetAddressOf()));
  return !objects.failed;
}

// The multiply draw (Multiply, contact_shadows.hpp; test/tw_romer_rays): `target`'s red times `visibility` (`occlusion`:
// times 1 - it), in the addon's own context state (the game's put back after).
inline void DrawMultiply(ID3D11DeviceContext1* native, ID3D11RenderTargetView* target, ID3D11ShaderResourceView* visibility, bool occlusion) {
  ComPtr<ID3D11Resource> target_resource;
  target->GetResource(target_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> target_texture;
  if (FAILED(target_resource.As(&target_texture))) return;
  D3D11_TEXTURE2D_DESC target_desc = {};
  target_texture->GetDesc(&target_desc);
  const std::shared_lock lock(state.mutex);
  const auto& objects = state.objects;
  if (objects.failed || !objects.multiply) return;
  const talesofarise::dlss::ContextStateScope scope(native, objects.private_state.Get());
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(target_desc.Width), .Height = static_cast<float>(target_desc.Height), .MaxDepth = 1.f};
  native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  native->VSSetShader(objects.full_screen.Get(), nullptr, 0);
  native->PSSetShader(objects.multiply.Get(), nullptr, 0);
  native->PSSetShaderResources(0, 1, &visibility);
  native->RSSetState(objects.no_cull.Get());
  native->RSSetViewports(1, &viewport);
  native->OMSetBlendState((occlusion ? objects.unshadow_red : objects.multiply_red).Get(), nullptr, 0xFFFFFFFF);
  native->OMSetRenderTargets(1, &target, nullptr);
  native->Draw(3, 0);
}

inline ID3D11DeviceContext* ImmediateContext(reshade::api::command_list* cmd_list) {
  if (!dlaa::rays || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return nullptr;
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  return (native->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE ? native : nullptr);
}

}  // namespace internal

inline const twattila::rt_scene::Host HOST = {
    .render_size = [] { return std::array<UINT, 2>{twattila::dlaa_bridge::bridge.width, twattila::dlaa_bridge::bridge.height}; },
    .guides_written = [] { return false; },
    // The captures' clip positions go back to the world with the trace's camera (the prepass's: one camera a frame).
    .capture_camera = [](ID3D11DeviceContext*, twattila::rt_scene::Camera*) { return true; },
    .scene_draw = internal::SceneDraw,
};

// Before ASSAO's apply or the shadow mask's resolve (PS b8 bound: the frame constants): the sun from them, then this
// frame's trace (once a frame).
inline void Trace(reshade::api::command_list* cmd_list) {
  auto& state = internal::state;
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  dlaa::internal::Camera camera;
  const ComPtr<ID3D11ShaderResourceView> depth = dlaa::SceneDepth();
  ComPtr<ID3D11Buffer> frame_constants;
  native->PSGetConstantBuffers(8, 1, frame_constants.GetAddressOf());
  ComPtr<ID3D11DeviceContext1> native1;
  if (!depth || !frame_constants || !dlaa::MainCamera(&camera) || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) {
    return;
  }
  ComPtr<ID3D11Resource> depth_resource;
  depth->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  if (FAILED(depth_resource.As(&depth_texture))) return;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_texture->GetDesc(&depth_desc);
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (!internal::MakeObjects(device.Get())) return;
  const auto& objects = state.objects;
  auto& bridge = twattila::dlaa_bridge::bridge;
  if (!dlaa::PrepareBridge(device.Get(), depth_desc.Width, depth_desc.Height) || !bridge.rt_sun.uav) return;
  {
    const talesofarise::dlss::ContextStateScope scope(native1.Get(), objects.private_state.Get());
    native->CSSetShader(objects.sun.Get(), nullptr, 0);
    native->CSSetConstantBuffers(0, 1, frame_constants.GetAddressOf());
    native->CSSetUnorderedAccessViews(0, 1, bridge.rt_sun.uav.GetAddressOf(), nullptr);
    native->Dispatch(1, 1, 1);
  }
  twattila::rt_scene::Trace(native, depth.Get(),
                            {
                                .view_projection = dlaa::internal::Transposed(camera.jittered_view_projection),
                                .inverse_view_projection = dlaa::internal::Transposed(camera.inverse_view_projection),
                                .position = camera.position,
                            });
}

// After ASSAO's apply (`ambient`) or the shadow mask's resolve: this frame's traced visibility multiplies the target's red.
inline void Multiply(reshade::api::command_list* cmd_list, bool ambient) {
  if ((ambient ? sliders.ambient_occlusion : sliders.sun_shadows) == 0.f) return;
  ID3D11DeviceContext* native = internal::ImmediateContext(cmd_list);
  if (native == nullptr) return;
  ID3D11ShaderResourceView* visibility = twattila::rt_scene::Visibilities()[ambient ? 1 : 0];
  ComPtr<ID3D11RenderTargetView> target;
  native->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
  ComPtr<ID3D11DeviceContext1> native1;
  if (visibility == nullptr || !target || FAILED(native->QueryInterface(IID_PPV_ARGS(native1.GetAddressOf())))) return;
  internal::DrawMultiply(native1.Get(), target.Get(), visibility, false);
}

// At the addon's start, with a row on (the scene's shaders kept from the game's start).
inline void Install() {
  internal::installed = true;
  twattila::rt_scene::InstallScene({}, HOST);
  reshade::register_event<reshade::addon_event::present>(internal::OnPresent);
  reshade::register_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
}

inline void Uninstall() {
  if (!internal::installed) return;
  internal::installed = false;
  dlaa::rays = false;
  twattila::rt_scene::UninstallScene();
  reshade::unregister_event<reshade::addon_event::present>(internal::OnPresent);
  reshade::unregister_event<reshade::addon_event::destroy_device>(internal::OnDestroyDevice);
}

}  // namespace twromeremastered::ray_tracing
