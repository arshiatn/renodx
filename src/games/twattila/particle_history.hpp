/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <memory>

#include <embed/shaders.h>

#include "../../utils/log.hpp"
#include "./frame_history.hpp"
#include "./particle_layer.h"

// Smooth Half-Size Particles (Experimental): the history of a half-size particle layer, shared by the Total War games that
// have one (Warhammer III's own: twwarhammer3/vfx_particles.hpp; Attila's, the addon's: particle_layer.hpp). Once a frame,
// before the layer is composited, the resolve (particle_history.hlsli) with the addon's full-screen triangle over the layer's
// viewport: this frame's layer and the last frames' at the same place into one of two histories, reading the other; the
// game's camera at b11, the last frame's (a copy made after each resolve) at b10, the layer's constants at b8. The caller
// composites what it returns. Also the passes' common objects and state (particle_layer.hpp's passes use them too). D3D11.
namespace twattila::particle_history {

using Microsoft::WRL::ComPtr;

// The passes' views at t50-t55 (the pixel stage's views below them are kept too: a pass's target may be one the game's draw
// reads), constants at b8-b11, sampler at s12 (particle_layer.hlsli, particle_history.hlsli).
constexpr UINT VIEWS_SLOT = 50;
constexpr UINT VIEW_COUNT = 6;
constexpr UINT KEPT_VIEWS = VIEWS_SLOT + VIEW_COUNT;
constexpr UINT CONSTANTS_SLOT = 8;
constexpr UINT CONSTANTS_COUNT = 4;  // b8 the layer's, b9 none, b10 the last frame's camera, b11 the camera
constexpr UINT SAMPLER_SLOT = 12;
constexpr float HISTORY_MOST = 0.9f;  // about the last 10 frames at most (the upscaler keeps its own after it)
constexpr float CAMERA_CUT = 25.f;    // metres in a frame: farther is a cut
constexpr float LAYER_SCALE = 2.f;  // a half layer's (Warhammer III's)

// The graphics state a full-screen pass of the particle modules sets, kept and put back: the shaders, input, viewports,
// rasterizer, targets and the output stage's UAVs (their counters kept), blend, depth, and the pixel stage's views t0-t55,
// constants b8-b11 and sampler s12.
struct DrawState {
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11HullShader> hull_shader;
  ComPtr<ID3D11DomainShader> domain_shader;
  ComPtr<ID3D11GeometryShader> geometry_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  ComPtr<ID3D11InputLayout> layout;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
  UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  ComPtr<ID3D11RasterizerState> rasterizer;
  std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets = {};
  ComPtr<ID3D11DepthStencilView> depth;
  std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> output_views = {};  // by slot, above the targets
  ComPtr<ID3D11BlendState> blend;
  std::array<float, 4> blend_factor = {};
  UINT sample_mask = 0;
  ComPtr<ID3D11DepthStencilState> depth_state;
  UINT stencil_reference = 0;
  std::array<ID3D11ShaderResourceView*, KEPT_VIEWS> views = {};
  std::array<ID3D11Buffer*, CONSTANTS_COUNT> constants = {};
  ComPtr<ID3D11SamplerState> sampler;

  explicit DrawState(ID3D11DeviceContext* native) : native_(native) {
    native->VSGetShader(vertex_shader.GetAddressOf(), nullptr, nullptr);
    native->HSGetShader(hull_shader.GetAddressOf(), nullptr, nullptr);
    native->DSGetShader(domain_shader.GetAddressOf(), nullptr, nullptr);
    native->GSGetShader(geometry_shader.GetAddressOf(), nullptr, nullptr);
    native->PSGetShader(pixel_shader.GetAddressOf(), nullptr, nullptr);
    native->IAGetInputLayout(layout.GetAddressOf());
    native->IAGetPrimitiveTopology(&topology);
    native->RSGetViewports(&viewport_count, viewports.data());
    native->RSGetState(rasterizer.GetAddressOf());
    native->OMGetRenderTargetsAndUnorderedAccessViews(static_cast<UINT>(targets.size()), targets.data(), depth.GetAddressOf(), 0,
                                                      static_cast<UINT>(output_views.size()), output_views.data());
    native->OMGetBlendState(blend.GetAddressOf(), blend_factor.data(), &sample_mask);
    native->OMGetDepthStencilState(depth_state.GetAddressOf(), &stencil_reference);
    native->PSGetShaderResources(0, KEPT_VIEWS, views.data());
    native->PSGetConstantBuffers(CONSTANTS_SLOT, CONSTANTS_COUNT, constants.data());
    native->PSGetSamplers(SAMPLER_SLOT, 1, sampler.GetAddressOf());
  }
  ~DrawState() {
    native_->VSSetShader(vertex_shader.Get(), nullptr, 0);
    native_->HSSetShader(hull_shader.Get(), nullptr, 0);
    native_->DSSetShader(domain_shader.Get(), nullptr, 0);
    native_->GSSetShader(geometry_shader.Get(), nullptr, 0);
    native_->PSSetShader(pixel_shader.Get(), nullptr, 0);
    native_->IASetInputLayout(layout.Get());
    native_->IASetPrimitiveTopology(topology);
    native_->RSSetViewports(viewport_count, viewports.data());
    native_->RSSetState(rasterizer.Get());
    // The targets before the views: a view of one of the pass's targets is bound again only once that is no target. A pass's
    // OMSetRenderTargets unbinds the output stage's UAVs: they go back above the last target, their counters as they were.
    if (std::ranges::any_of(output_views, [](auto* view) { return view != nullptr; })) {
      UINT target_count = 0;
      for (UINT slot = 0; slot < targets.size(); ++slot) {
        if (targets[slot] != nullptr) target_count = slot + 1;
      }
      std::array<UINT, D3D11_PS_CS_UAV_REGISTER_COUNT> keep_counts = {};
      keep_counts.fill(UINT_MAX);
      native_->OMSetRenderTargetsAndUnorderedAccessViews(target_count, targets.data(), depth.Get(), target_count,
                                                         static_cast<UINT>(output_views.size()) - target_count,
                                                         output_views.data() + target_count, keep_counts.data());
    } else {
      native_->OMSetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth.Get());
    }
    native_->OMSetBlendState(blend.Get(), blend_factor.data(), sample_mask);
    native_->OMSetDepthStencilState(depth_state.Get(), stencil_reference);
    native_->PSSetShaderResources(0, KEPT_VIEWS, views.data());
    native_->PSSetConstantBuffers(CONSTANTS_SLOT, CONSTANTS_COUNT, constants.data());
    native_->PSSetSamplers(SAMPLER_SLOT, 1, sampler.GetAddressOf());
    for (auto* target : targets) {
      if (target != nullptr) target->Release();
    }
    for (auto* view : output_views) {
      if (view != nullptr) view->Release();
    }
    for (auto* view : views) {
      if (view != nullptr) view->Release();
    }
    for (auto* buffer : constants) {
      if (buffer != nullptr) buffer->Release();
    }
  }
  DrawState(const DrawState&) = delete;
  DrawState& operator=(const DrawState&) = delete;

 private:
  ID3D11DeviceContext* native_;
};

// What the resolve reads (none owned).
struct Layer {
  ID3D11ShaderResourceView* colour = nullptr;         // this frame's layer
  ID3D11ShaderResourceView* optical_depth = nullptr;  // its zeroth moment (Warhammer III's MBOIT); null: its alpha says
  ID3D11ShaderResourceView* depth = nullptr;          // the depth it was drawn against, at its size
  ID3D11Buffer* camera = nullptr;                     // the game's camera (the includer's light_bounce.hlsli layout)
  D3D11_VIEWPORT viewport = {};                       // the layer's part of its textures
  float scale = LAYER_SCALE;                          // render pixels per layer texel along a side
};

// Made on the game's device, again when it changes; the histories again when the layer's size or viewport changes.
struct History {
  ComPtr<ID3D11Device> device;
  bool failed = false;
  ComPtr<ID3D11VertexShader> full_screen;
  ComPtr<ID3D11PixelShader> resolve;
  ComPtr<ID3D11SamplerState> linear;
  ComPtr<ID3D11RasterizerState> rasterizer;
  ComPtr<ID3D11DepthStencilState> no_depth;
  ComPtr<ID3D11Buffer> constants;  // the layer's (b8)
  ParticleLayerConstants uploaded = {};
  ComPtr<ID3D11Buffer> previous_camera;
  UINT width = 0;
  UINT height = 0;
  D3D11_VIEWPORT viewport = {};
  std::array<frame_history::Target, 2> colours;
  std::array<frame_history::Target, 2> extras;  // the optical depth and the frames
  std::array<frame_history::Target, 2> means;   // the layer's 3 x 3 mean: luminance and coverage
  UINT failed_width = 0;                        // a size the histories could not be made at: not tried again
  UINT failed_height = 0;
  size_t index = 0;                            // the one the last resolve wrote
  uint64_t resolved_frame = UINT64_MAX;
};

// The passes' objects on `device` (made once; another device: all made again). False: they could not be made.
inline bool MakeObjects(ID3D11Device* device, History* history) {
  if (history->device.Get() == device) return !history->failed;
  *history = {.device = device};
  const D3D11_SAMPLER_DESC linear = {
      .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
      .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
      .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
      .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
      .ComparisonFunc = D3D11_COMPARISON_NEVER,
      .MaxLOD = D3D11_FLOAT32_MAX,
  };
  const D3D11_RASTERIZER_DESC rasterizer = {
      .FillMode = D3D11_FILL_SOLID,
      .CullMode = D3D11_CULL_NONE,
      .DepthClipEnable = TRUE,
  };
  const D3D11_DEPTH_STENCIL_DESC no_depth = {};
  const D3D11_BUFFER_DESC constants = {
      .ByteWidth = sizeof(ParticleLayerConstants),
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
  };
  history->failed =
      FAILED(device->CreateVertexShader(__light_bounce_vs.data(), __light_bounce_vs.size(), nullptr, history->full_screen.GetAddressOf()))
      || FAILED(device->CreatePixelShader(__particle_history.data(), __particle_history.size(), nullptr, history->resolve.GetAddressOf()))
      || FAILED(device->CreateSamplerState(&linear, history->linear.GetAddressOf()))
      || FAILED(device->CreateRasterizerState(&rasterizer, history->rasterizer.GetAddressOf()))
      || FAILED(device->CreateDepthStencilState(&no_depth, history->no_depth.GetAddressOf()))
      || FAILED(device->CreateBuffer(&constants, nullptr, history->constants.GetAddressOf()));
  if (history->failed) renodx::utils::log::w("[Total War] Half-size particles: could not create their shaders: off.");
  return !history->failed;
}

// The layer's constants (b8) as `constants` (written only when they changed).
inline void Upload(ID3D11DeviceContext* context, const ParticleLayerConstants& constants, History* history) {
  if (std::memcmp(&history->uploaded, &constants, sizeof(ParticleLayerConstants)) == 0) return;
  context->UpdateSubresource(history->constants.Get(), 0, nullptr, &constants, 0, 0);
  history->uploaded = constants;
}

// The histories' memory back (made again, empty, at the next resolve; a size that failed is tried again).
inline void ReleaseHistories(History* history) {
  history->colours = {};
  history->extras = {};
  history->means = {};
  history->width = 0;
  history->height = 0;
  history->failed_width = 0;
  history->failed_height = 0;
}

// One full-screen triangle over `viewport` with `shader`, its views at t50 and constants at b8 (the targets the caller's).
inline void DrawFullScreen(ID3D11DeviceContext* context, const History& history, const D3D11_VIEWPORT& viewport,
                           ID3D11PixelShader* shader, const std::array<ID3D11ShaderResourceView*, VIEW_COUNT>& views,
                           const std::array<ID3D11Buffer*, CONSTANTS_COUNT>& constants, ID3D11BlendState* blend,
                           ID3D11DepthStencilState* depth_state) {
  context->VSSetShader(history.full_screen.Get(), nullptr, 0);
  context->HSSetShader(nullptr, nullptr, 0);
  context->DSSetShader(nullptr, nullptr, 0);
  context->GSSetShader(nullptr, nullptr, 0);
  context->IASetInputLayout(nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->RSSetState(history.rasterizer.Get());
  context->RSSetViewports(1, &viewport);
  context->OMSetBlendState(blend, nullptr, 0xFFFFFFFFu);
  context->OMSetDepthStencilState(depth_state, 0);
  context->PSSetShader(shader, nullptr, 0);
  context->PSSetShaderResources(VIEWS_SLOT, VIEW_COUNT, views.data());
  context->PSSetConstantBuffers(CONSTANTS_SLOT, CONSTANTS_COUNT, constants.data());
  context->PSSetSamplers(SAMPLER_SLOT, 1, history.linear.GetAddressOf());
  context->Draw(3, 0);
}

// The resolve of `layer` for frame `frame` (once a frame; `restart`: the last frames left out, e.g. after a row changed what
// the layer holds): the smoothed layer and its optical depth and frames, or none (the caller composites the layer as it is).
// Under the caller's lock.
inline std::array<ID3D11ShaderResourceView*, 2> Resolve(ID3D11DeviceContext* context, const Layer& layer, uint64_t frame,
                                                       bool restart, History* history) {
  if (layer.colour == nullptr || layer.depth == nullptr || layer.camera == nullptr) return {};
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  if (!MakeObjects(device.Get(), history)) return {};
  ComPtr<ID3D11Resource> resource;
  layer.colour->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  if (FAILED(resource.As(&texture))) return {};
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  const bool same_viewport = std::memcmp(&history->viewport, &layer.viewport, sizeof(D3D11_VIEWPORT)) == 0;
  if (history->width != desc.Width || history->height != desc.Height || !same_viewport) {
    if (history->failed_width == desc.Width && history->failed_height == desc.Height) return {};
    ReleaseHistories(history);
    for (size_t index = 0; index < 2; ++index) {
      if (!frame_history::MakeTarget(device.Get(), desc.Width, desc.Height, DXGI_FORMAT_R16G16B16A16_FLOAT, &history->colours[index])
          || !frame_history::MakeTarget(device.Get(), desc.Width, desc.Height, DXGI_FORMAT_R32G32_FLOAT, &history->extras[index])
          || !frame_history::MakeTarget(device.Get(), desc.Width, desc.Height, DXGI_FORMAT_R16G16_FLOAT, &history->means[index])) {
        ReleaseHistories(history);
        history->failed_width = desc.Width;
        history->failed_height = desc.Height;
        renodx::utils::log::w("[Total War] Smooth Half-Size Particles: could not create its textures (", desc.Width, " x ",
                              desc.Height, "): off at this size.");
        return {};
      }
    }
    // No history until the first resolve (no frames counted).
    constexpr float NOTHING[4] = {0.f, 0.f, 0.f, 0.f};
    for (size_t index = 0; index < 2; ++index) {
      context->ClearRenderTargetView(history->colours[index].rtv.Get(), NOTHING);
      context->ClearRenderTargetView(history->extras[index].rtv.Get(), NOTHING);
      context->ClearRenderTargetView(history->means[index].rtv.Get(), NOTHING);
    }
    history->width = desc.Width;
    history->height = desc.Height;
    history->viewport = layer.viewport;
    restart = true;
  }
  const bool kept = !restart && history->resolved_frame + 1 == frame;
  Upload(context,
         {.history_most = (kept ? HISTORY_MOST : 0.f),
          .camera_cut = CAMERA_CUT,
          .region_width = layer.viewport.Width,
          .region_height = layer.viewport.Height,
          .layer_scale = layer.scale},
         history);
  const size_t read = history->index;
  const size_t written = read ^ 1;
  {
    const DrawState game_state(context);
    const std::array<ID3D11RenderTargetView*, 3> targets = {history->colours[written].rtv.Get(), history->extras[written].rtv.Get(),
                                                            history->means[written].rtv.Get()};
    context->OMSetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), nullptr);
    DrawFullScreen(context, *history, layer.viewport, history->resolve.Get(),
                   {layer.colour, layer.optical_depth, layer.depth, history->colours[read].srv.Get(), history->extras[read].srv.Get(),
                    history->means[read].srv.Get()},
                   {history->constants.Get(), nullptr, history->previous_camera.Get(), layer.camera}, nullptr, history->no_depth.Get());
  }
  // This frame's camera for the next resolve.
  frame_history::KeepCamera(device.Get(), context, layer.camera, std::addressof(history->previous_camera));
  history->index = written;
  history->resolved_frame = frame;
  return {history->colours[written].srv.Get(), history->extras[written].srv.Get()};
}

}  // namespace twattila::particle_history
