/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <array>
#include <climits>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./frame_history.hpp"
#include "./light_bounce.hpp"
#include "./rt_scene.hpp"
#include "./screen_space_shadows.hpp"

// Smoke shadows without rays (any GPU, no helper): last frame's lit particles (rt_core.hpp CaptureParticles; they are drawn
// after the sun draws) as soft splats into a small map seen from the sun, RGBA32F: optical depth and the moments of its
// height (smoke_map_list), so what stands above the smoke stays lit. At the frame's first sun draw, in
// a context state of its own: the map's view (smoke_map_setup), the splats (smoke_map_list), the map (smoke_map_splat), then
// the sun's occlusion at t49 with the smoke's dimming multiplied in (smoke_map_apply). The patched sun shaders multiply their
// shadow by 1 - that: the sun's light alone, with the game's shadows, Bend's or the rays'. D3D11.
namespace twattila::smoke_map {

using Microsoft::WRL::ComPtr;

// As the addon's sliders hold them.
struct Sliders {
  float strength = 0.f;  // 0: off
  float softness = 2.f;  // metres
};

namespace internal {

constexpr UINT MAP_SIZE = 1024;  // smoke_map.hlsli SMOKE_MAP_SIZE
constexpr UINT MAX_SPLATS = 1u << 17;

// smoke_map.hlsli's smoke_map_settings.
struct ShaderSettings {
  float sun_direction = 0.f;
  float strength = 0.f;
  float softness = 0.f;
  uint32_t particles = 0;
  float render_width = 0.f;
  float render_height = 0.f;
  std::array<float, 2> padding = {};
};

struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3DDeviceContextState> state;
  ComPtr<ID3D11ComputeShader> setup;
  ComPtr<ID3D11ComputeShader> list;
  ComPtr<ID3D11ComputeShader> apply;
  ComPtr<ID3D11ComputeShader> apply_ms;
  ComPtr<ID3D11VertexShader> splat_vertex_shader;
  ComPtr<ID3D11PixelShader> splat_pixel_shader;
  ComPtr<ID3D11Buffer> settings;
  ComPtr<ID3D11Buffer> camera;  // the sun draw's in Attila's layout (frame_history::AttilaCamera)
  ComPtr<ID3D11UnorderedAccessView> view_uav;
  ComPtr<ID3D11ShaderResourceView> view_srv;
  ComPtr<ID3D11UnorderedAccessView> anchor_uav;
  ComPtr<ID3D11UnorderedAccessView> splats_uav;
  ComPtr<ID3D11ShaderResourceView> splats_srv;
  ComPtr<ID3D11Buffer> arguments;  // DrawInstancedIndirect's
  ComPtr<ID3D11UnorderedAccessView> arguments_uav;
  ComPtr<ID3D11RenderTargetView> map_rtv;  // 32 bits: at 16, the moments' small variance rounds away and the look-at height shows
  ComPtr<ID3D11ShaderResourceView> map_srv;
  ComPtr<ID3D11BlendState> additive;
  ComPtr<ID3D11RasterizerState> rasterizer;
  ComPtr<ID3D11SamplerState> linear_clamp;
  // The sun draws' t49 (the G-buffer's size). 16 bits: under the smoke 8 bits would show as bands in HDR.
  UINT width = 0;
  UINT height = 0;
  ComPtr<ID3D11UnorderedAccessView> dimmed_uav;
  ComPtr<ID3D11ShaderResourceView> dimmed_srv;
};

struct Runtime {
  std::shared_mutex mutex;
  Sliders sliders = {};
  uint64_t frame = 0;
  uint64_t computed_frame = UINT64_MAX;
  bool dimmed = false;  // this frame's t49 is the map's (computed_frame)
  DeviceObjects objects;
};

static Runtime runtime;

}  // namespace internal

// A patched sun draw's occlusion (screen_space_shadows::SetSmokeOcclusion): `occlusion` (Bend's or the rays', null: none)
// with the smoke in it, or null (off, or no particles last frame: `occlusion` as it is). `depth`: the G-buffer's depth
// channel, the draw's own.
inline ID3D11ShaderResourceView* Occlusion(ID3D11DeviceContext* native, ID3D11ShaderResourceView* occlusion, const light_bounce::SunDraw& draw,
                                           ID3D11ShaderResourceView* depth) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  auto& objects = runtime.objects;
  if (runtime.sliders.strength <= 0.f) return nullptr;
  if (runtime.computed_frame == runtime.frame) return runtime.dimmed ? objects.dimmed_srv.Get() : nullptr;
  runtime.computed_frame = runtime.frame;
  runtime.dimmed = false;

  ComPtr<ID3D11ShaderResourceView> particles;
  ComPtr<ID3D11Buffer> behaviours;
  UINT particle_count = 0;
  {
    auto& scene = rt_scene::internal::runtime;
    const std::unique_lock scene_lock(scene.mutex);
    if (scene.particles_frame + 1 < scene.frame) return nullptr;
    particles = scene.particle_stream.srv;
    behaviours = scene.particle_stream.behaviours;
    particle_count = scene.particles;
  }
  ComPtr<ID3D11Buffer> game_camera;
  ComPtr<ID3D11Buffer> lighting;
  native->PSGetConstantBuffers(draw.camera_slot, 1, game_camera.GetAddressOf());
  native->PSGetConstantBuffers(draw.lighting_slot, 1, lighting.GetAddressOf());
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  ComPtr<ID3D11Device1> device1;
  ComPtr<ID3D11DeviceContext1> context1;
  if (depth == nullptr || !particles || particle_count == 0 || !game_camera || !lighting || FAILED(device.As(&device1))
      || FAILED(native->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf())))) {
    return nullptr;
  }

  if (objects.device.Get() != device.Get()) {
    objects = {.device = device};
    const D3D_FEATURE_LEVEL level = device1->GetFeatureLevel();
    const UINT state_flags = ((device1->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0
                                  ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                                  : 0);
    const auto compute = [&](const auto& code, ComPtr<ID3D11ComputeShader>* shader) {
      return SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, shader->GetAddressOf()));
    };
    const auto structured = [&](UINT stride, UINT count, ComPtr<ID3D11UnorderedAccessView>* uav, ComPtr<ID3D11ShaderResourceView>* srv) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = stride * count, .Usage = D3D11_USAGE_DEFAULT,
                                      .BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
                                      .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = stride};
      const std::vector<uint8_t> zeros(desc.ByteWidth);
      const D3D11_SUBRESOURCE_DATA data = {.pSysMem = zeros.data()};
      ComPtr<ID3D11Buffer> buffer;
      return SUCCEEDED(device->CreateBuffer(&desc, &data, buffer.GetAddressOf()))
             && SUCCEEDED(device->CreateUnorderedAccessView(buffer.Get(), nullptr, uav->GetAddressOf()))
             && (srv == nullptr || SUCCEEDED(device->CreateShaderResourceView(buffer.Get(), nullptr, srv->GetAddressOf())));
    };
    const D3D11_BUFFER_DESC settings_desc = {.ByteWidth = sizeof(internal::ShaderSettings), .Usage = D3D11_USAGE_DEFAULT,
                                             .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_BUFFER_DESC arguments_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                              .MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS};
    const D3D11_UNORDERED_ACCESS_VIEW_DESC arguments_view = {.Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
                                                             .Buffer = {.FirstElement = 0, .NumElements = 4, .Flags = D3D11_BUFFER_UAV_FLAG_RAW}};
    const D3D11_TEXTURE2D_DESC map_desc = {.Width = internal::MAP_SIZE, .Height = internal::MAP_SIZE, .MipLevels = 1, .ArraySize = 1,
                                           .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                           .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE};
    D3D11_BLEND_DESC additive_desc = {};
    additive_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = D3D11_BLEND_ONE, .DestBlend = D3D11_BLEND_ONE, .BlendOp = D3D11_BLEND_OP_ADD,
                                     .SrcBlendAlpha = D3D11_BLEND_ONE, .DestBlendAlpha = D3D11_BLEND_ONE, .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                                     .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL};
    const D3D11_RASTERIZER_DESC rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE};
    const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                             .MaxLOD = D3D11_FLOAT32_MAX};
    ComPtr<ID3D11Texture2D> map;
    const bool made = SUCCEEDED(device1->CreateDeviceContextState(state_flags, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                                                  objects.state.GetAddressOf()))
                      && compute(__smoke_map_setup, &objects.setup) && compute(__smoke_map_list, &objects.list)
                      && compute(__smoke_map_apply, &objects.apply) && compute(__smoke_map_apply_ms, &objects.apply_ms)
                      && SUCCEEDED(device->CreateVertexShader(__smoke_map_splat_vs.data(), __smoke_map_splat_vs.size(), nullptr,
                                                              objects.splat_vertex_shader.GetAddressOf()))
                      && SUCCEEDED(device->CreatePixelShader(__smoke_map_splat.data(), __smoke_map_splat.size(), nullptr,
                                                             objects.splat_pixel_shader.GetAddressOf()))
                      && SUCCEEDED(device->CreateBuffer(&settings_desc, nullptr, objects.settings.GetAddressOf()))
                      && structured(64, 1, &objects.view_uav, &objects.view_srv)
                      && structured(16, 2, &objects.anchor_uav, nullptr)
                      && structured(32, internal::MAX_SPLATS, &objects.splats_uav, &objects.splats_srv)
                      && SUCCEEDED(device->CreateBuffer(&arguments_desc, nullptr, objects.arguments.GetAddressOf()))
                      && SUCCEEDED(device->CreateUnorderedAccessView(objects.arguments.Get(), &arguments_view, objects.arguments_uav.GetAddressOf()))
                      && SUCCEEDED(device->CreateTexture2D(&map_desc, nullptr, map.GetAddressOf()))
                      && SUCCEEDED(device->CreateRenderTargetView(map.Get(), nullptr, objects.map_rtv.GetAddressOf()))
                      && SUCCEEDED(device->CreateShaderResourceView(map.Get(), nullptr, objects.map_srv.GetAddressOf()))
                      && SUCCEEDED(device->CreateBlendState(&additive_desc, objects.additive.GetAddressOf()))
                      && SUCCEEDED(device->CreateRasterizerState(&rasterizer_desc, objects.rasterizer.GetAddressOf()))
                      && SUCCEEDED(device->CreateSamplerState(&sampler_desc, objects.linear_clamp.GetAddressOf()));
    if (!made) {
      renodx::utils::log::w("[Total War] Smoke shadows: could not create their shaders or textures.");
      objects = {.device = device};
    }
  }
  if (!objects.state) return nullptr;

  D3D11_SHADER_RESOURCE_VIEW_DESC depth_view = {};
  depth->GetDesc(&depth_view);
  ComPtr<ID3D11Resource> depth_resource;
  depth->GetResource(depth_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> depth_texture;
  if (FAILED(depth_resource.As(&depth_texture))) return nullptr;
  D3D11_TEXTURE2D_DESC depth_desc = {};
  depth_texture->GetDesc(&depth_desc);
  if (objects.width != depth_desc.Width || objects.height != depth_desc.Height) {
    objects.width = 0;
    objects.dimmed_uav.Reset();
    objects.dimmed_srv.Reset();
    const D3D11_TEXTURE2D_DESC desc = {.Width = depth_desc.Width, .Height = depth_desc.Height, .MipLevels = 1, .ArraySize = 1,
                                       .Format = DXGI_FORMAT_R16_UNORM, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                       .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf()))
        || FAILED(device->CreateUnorderedAccessView(texture.Get(), nullptr, objects.dimmed_uav.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, objects.dimmed_srv.GetAddressOf()))) {
      return nullptr;
    }
    objects.width = depth_desc.Width;
    objects.height = depth_desc.Height;
  }

  const auto render = frame_history::RenderSize(depth_desc.Width, depth_desc.Height);
  const internal::ShaderSettings settings = {
      .sun_direction = static_cast<float>(draw.sun_direction),
      .strength = runtime.sliders.strength,
      .softness = runtime.sliders.softness,
      .particles = particle_count,
      .render_width = static_cast<float>(render[0]),
      .render_height = static_cast<float>(render[1]),
  };
  native->UpdateSubresource(objects.settings.Get(), 0, nullptr, &settings, 0, 0);
  // Six vertices a splat, none yet (smoke_map_list counts them).
  constexpr std::array<UINT, 4> NO_SPLATS = {6, 0, 0, 0};
  native->UpdateSubresource(objects.arguments.Get(), 0, nullptr, NO_SPLATS.data(), 0, 0);
  ID3D11Buffer* camera = frame_history::AttilaCamera(device.Get(), native, game_camera.Get(), draw.camera_layout, std::addressof(objects.camera));
  // Not bound for the pixel shaders while written.
  ID3D11ShaderResourceView* no_view = nullptr;
  native->PSSetShaderResources(screen_space_shadows::internal::MASK_SLOT, 1, &no_view);

  ComPtr<ID3DDeviceContextState> game_state;
  context1->SwapDeviceContextState(objects.state.Get(), game_state.GetAddressOf());
  const std::array<ID3D11Buffer*, 4> constants = {camera, lighting.Get(), objects.settings.Get(), behaviours.Get()};
  native->CSSetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.data());
  native->CSSetSamplers(0, 1, objects.linear_clamp.GetAddressOf());
  const auto run = [&](ID3D11ComputeShader* shader, std::array<ID3D11ShaderResourceView*, 4> views, std::array<ID3D11UnorderedAccessView*, 2> uavs,
                       UINT groups_x, UINT groups_y) {
    constexpr std::array<UINT, 2> KEEP_COUNTS = {UINT_MAX, UINT_MAX};
    constexpr std::array<ID3D11ShaderResourceView*, 4> NO_VIEWS = {};
    constexpr std::array<ID3D11UnorderedAccessView*, 2> NO_UAVS = {};
    native->CSSetShader(shader, nullptr, 0);
    native->CSSetUnorderedAccessViews(0, 2, uavs.data(), KEEP_COUNTS.data());
    native->CSSetShaderResources(0, 4, views.data());
    native->Dispatch(groups_x, groups_y, 1);
    native->CSSetShaderResources(0, 4, NO_VIEWS.data());
    native->CSSetUnorderedAccessViews(0, 2, NO_UAVS.data(), KEEP_COUNTS.data());
  };
  run(objects.setup.Get(), {}, {objects.view_uav.Get(), objects.anchor_uav.Get()}, 1, 1);
  run(objects.list.Get(), {particles.Get(), objects.view_srv.Get()}, {objects.splats_uav.Get(), objects.arguments_uav.Get()}, (particle_count + 63) / 64, 1);

  constexpr std::array<FLOAT, 4> NO_SMOKE = {};
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(internal::MAP_SIZE), .Height = static_cast<float>(internal::MAP_SIZE), .MaxDepth = 1.f};
  ID3D11RenderTargetView* map_target = objects.map_rtv.Get();
  native->ClearRenderTargetView(map_target, NO_SMOKE.data());
  native->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  native->VSSetShader(objects.splat_vertex_shader.Get(), nullptr, 0);
  native->VSSetShaderResources(0, 1, objects.splats_srv.GetAddressOf());
  native->PSSetShader(objects.splat_pixel_shader.Get(), nullptr, 0);
  native->RSSetState(objects.rasterizer.Get());
  native->RSSetViewports(1, &viewport);
  native->OMSetBlendState(objects.additive.Get(), nullptr, 0xFFFFFFFF);
  native->OMSetRenderTargets(1, &map_target, nullptr);
  native->DrawInstancedIndirect(objects.arguments.Get(), 0);
  native->OMSetRenderTargets(0, nullptr, nullptr);
  native->VSSetShaderResources(0, 1, &no_view);

  const ComPtr<ID3D11ComputeShader>& apply = (depth_view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DMS ? objects.apply_ms : objects.apply);
  run(apply.Get(), {objects.map_srv.Get(), depth, occlusion, objects.view_srv.Get()}, {objects.dimmed_uav.Get(), objects.anchor_uav.Get()},
      (objects.width + 7) / 8, (objects.height + 7) / 8);
  context1->SwapDeviceContextState(game_state.Get(), nullptr);
  runtime.dimmed = true;
  return objects.dimmed_srv.Get();
}

// Every present: the settings and a new frame. Off: its memory back.
inline void Update(const Sliders& sliders) {
  auto& runtime = internal::runtime;
  const std::unique_lock lock(runtime.mutex);
  runtime.sliders = sliders;
  ++runtime.frame;
  if (sliders.strength <= 0.f) runtime.objects = {};
}

// The game's device is going away: the addon's objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.objects.device.Get() == native) internal::runtime.objects = {};
}

inline void Uninstall() {
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.objects = {};
}

}  // namespace twattila::smoke_map
