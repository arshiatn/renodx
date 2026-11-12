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
#include <format>
#include <shared_mutex>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include <embed/shaders.h>

#include "../../utils/command_action.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "./frame_history.hpp"
#include "./particle_history.hpp"

// Particle Resolution (Experimental): Attila's particles (particle_vfx_main: one vertex a particle, a geometry shader making
// its quad, blended straight into the light buffer at the render size against the scene's read-only depth; Attila's only
// traced frame with them, test_scripts/logs/upfake_tw_frame_trace.txt 3438) drawn into a half or quarter layer of the
// addon's, composited once after the last of them, at the light buffer's next reader (`readers`: the luma / bloom downsample,
// 3446, else the tone mappers). The frame's first particle draw opens it: the layer's depth (each texel the farthest of its
// render pixels, particle_depth), the layer cleared to (0, 0, 0, 1), a copy of the particles' camera with the layer's size
// as the target's (c30: their soft edges read the depth by it). Each particle draw: the layer and its depth as targets, the
// layer's viewport, its blend with the alpha keeping the share of the scene left (over: x (1 - alpha); added: x 1), the copy
// at b0 of every stage, the layer's constants at the pixel stage's b8 (vfx00 reads the rays' shadow by them); put back after.
// The composite (particle_composite; with Smooth Half-Size Particles the history first, particle_history.hpp) brings it up by
// depth: light x alpha + rgb. Full-Resolution Particle Edges (the idea of Cantlay's "High-Speed, Off-Screen Particles", GPU
// Gems 3 ch. 23, its edge pixels drawn again at full size): at the opening the render pixels whose layer texels were drawn
// against other depths (particle_edges: ParticleUpsample's edges) get stencil 1 and the scene's depth in a depth-stencil of
// the addon's at the light buffer's size; each particle draw is drawn again there right after, as the game drew it (its
// depth test, stencil 1), and the composite leaves them alone (stencil 0). A draw with any other blend, a scissor, more
// targets, the game's MSAA, the output stage's UAVs bound or (with the edges) its own stencil test stays at full size, and the
// frame's later ones (one log line for the blends, one for the rest). D3D11, the main view.
namespace twattila::particle_layer {

using Microsoft::WRL::ComPtr;

static float half_resolution_row = 0.f;  // Particle Resolution: 0 full, 1 half, 2 quarter
static float smoothing_row = 0.f;
static float edges_row = 1.f;

namespace internal {

constexpr UINT CAMERA_SLOT = 0;  // camera_VS_PS in the particle shaders (vertex, geometry, pixel)
// Its g_render_target_dimensions (c30: width, height, 1 / width, 1 / height).
constexpr UINT RENDER_TARGET_DIMENSIONS = 30 * 16;
// Full-Resolution Particle Edges' stencil test: the reference alone.
constexpr D3D11_DEPTH_STENCILOP_DESC STENCIL_EQUAL = {.StencilFailOp = D3D11_STENCIL_OP_KEEP, .StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
                                                      .StencilPassOp = D3D11_STENCIL_OP_KEEP, .StencilFunc = D3D11_COMPARISON_EQUAL};

struct DeviceObjects {
  ComPtr<ID3D11Device> device;
  bool failed = false;
  ComPtr<ID3D11PixelShader> depth_shader;
  ComPtr<ID3D11PixelShader> composite_shader;
  ComPtr<ID3D11PixelShader> edges_shader;
  ComPtr<ID3D11DepthStencilState> depth_write;
  ComPtr<ID3D11DepthStencilState> edges_mark;       // the edges: the depth written, stencil 1
  ComPtr<ID3D11DepthStencilState> composite_edges;  // the composite: stencil 0 alone
  ComPtr<ID3D11BlendState> composite_blend;
  ComPtr<ID3D11Buffer> dimensions;  // the layer's size as c30 holds the target's
  std::array<float, 4> written_dimensions = {};
  ComPtr<ID3D11Buffer> camera;      // the particles' camera with the layer's size
  // The layer's textures, for a light buffer of `light_width` x `light_height` at `scale` render pixels a texel.
  UINT light_width = 0;
  UINT light_height = 0;
  float scale = 0.f;
  UINT failed_light_width = 0;  // a light buffer's size the textures could not be made for: not tried again
  UINT failed_light_height = 0;
  frame_history::Target layer;
  ComPtr<ID3D11DepthStencilView> depth_target;
  ComPtr<ID3D11ShaderResourceView> depth_view;
  // The scene's depth (the particle draws' depth target) and a view of it.
  ComPtr<ID3D11Resource> scene_depth;
  ComPtr<ID3D11ShaderResourceView> scene_depth_view;
  // The game's particle blends and theirs into the layer (null: one the layer can't take).
  std::vector<std::pair<ComPtr<ID3D11BlendState>, ComPtr<ID3D11BlendState>>> blends;
  // Full-Resolution Particle Edges: the light buffer's size of depth-stencil (`edges_format`); the game's particle depth
  // states and theirs at the edges (its own with stencil 1; null: one with its own stencil test).
  ComPtr<ID3D11DepthStencilView> edges_target;
  DXGI_FORMAT edges_format = DXGI_FORMAT_UNKNOWN;
  std::vector<std::pair<ComPtr<ID3D11DepthStencilState>, ComPtr<ID3D11DepthStencilState>>> edge_states;
  particle_history::History history;
};

// What a particle draw into the layer replaced, put back after it.
struct GameBindings {
  ComPtr<ID3D11RenderTargetView> target;
  ComPtr<ID3D11DepthStencilView> depth;
  std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports = {};
  UINT viewport_count = 0;
  ComPtr<ID3D11BlendState> blend;
  std::array<float, 4> blend_factor = {};
  UINT sample_mask = 0;
  ComPtr<ID3D11DepthStencilState> depth_state;
  UINT stencil_ref = 0;
  ComPtr<ID3D11DepthStencilState> edge_state;  // the draw's again at the edges (null: none)
  std::array<ComPtr<ID3D11Buffer>, 3> cameras;  // vertex, geometry, pixel
  ComPtr<ID3D11Buffer> layer_constants;         // the pixel stage's b8
};

struct Runtime {
  std::shared_mutex mutex;
  uint64_t frame = 0;  // counted at each present
  UINT display_width = 0;
  UINT display_height = 0;
  bool smoothing = false;
  float scale = particle_history::LAYER_SCALE;  // render pixels per layer texel along a side (Particle Resolution)
  bool edges = true;                            // Full-Resolution Particle Edges
  bool edges_open = false;                      // the open layer's edges are marked
  bool restart = true;  // the history starts again at the next resolve
  uint64_t open_frame = UINT64_MAX;    // the frame whose layer is open
  uint64_t closed_frame = UINT64_MAX;  // the frame whose layer went in (its later particles stay at full size)
  uint64_t refused_frame = UINT64_MAX;
  bool stopped = false;  // a layer went in nowhere: nothing drawn into one until the row is turned off and on
  ComPtr<ID3D11RenderTargetView> light;  // the open layer's light buffer
  D3D11_VIEWPORT render_viewport = {};
  D3D11_VIEWPORT layer_viewport = {};
  uint32_t draws = 0;  // particle draws in the open layer
  GameBindings game;
  bool refusal_logged = false;        // a blend it can't take
  bool state_refusal_logged = false;  // MSAA, a scissor or UAVs
  bool stencil_refusal_logged = false;  // Full-Resolution Particle Edges: their own stencil test
  bool lost_logged = false;
  bool applied_logged = false;
  DeviceObjects objects;
};

static Runtime runtime;

// The game's particle blend `game` for the layer, made once: its colour as the game's, its alpha what is left of the scene
// (over: x (1 - alpha); added: x 1), so the composite's light x alpha + rgb gives the game's result. Null: another blend.
inline ID3D11BlendState* LayerBlend(ID3D11Device* device, ID3D11BlendState* game) {
  auto& blends = runtime.objects.blends;
  if (const auto known = std::ranges::find(blends, game, [](const auto& entry) { return entry.first.Get(); }); known != blends.end()) {
    return known->second.Get();
  }
  ComPtr<ID3D11BlendState> layer;
  D3D11_BLEND_DESC desc = {};
  if (game != nullptr) game->GetDesc(&desc);
  const auto& target = desc.RenderTarget[0];
  const bool source_ok = target.SrcBlend == D3D11_BLEND_ONE || target.SrcBlend == D3D11_BLEND_SRC_ALPHA;
  const bool over = target.DestBlend == D3D11_BLEND_INV_SRC_ALPHA;
  const bool added = target.DestBlend == D3D11_BLEND_ONE;
  if (game != nullptr && !desc.AlphaToCoverageEnable && target.BlendEnable && target.BlendOp == D3D11_BLEND_OP_ADD && source_ok
      && (over || added)) {
    D3D11_BLEND_DESC layer_desc = desc;
    layer_desc.IndependentBlendEnable = FALSE;
    auto& layer_target = layer_desc.RenderTarget[0];
    layer_target.SrcBlendAlpha = D3D11_BLEND_ZERO;
    layer_target.DestBlendAlpha = (over ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_ONE);
    layer_target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    layer_target.RenderTargetWriteMask = static_cast<UINT8>(target.RenderTargetWriteMask | D3D11_COLOR_WRITE_ENABLE_ALPHA);
    device->CreateBlendState(&layer_desc, layer.GetAddressOf());
  }
  if (!layer && !runtime.refusal_logged) {
    runtime.refusal_logged = true;
    renodx::utils::log::i(std::format("[Total War] Particles at Half Resolution: a particle blend it can't take (source {}, destination {}, "
                                      "operation {}): those stay at full size.",
                                      static_cast<int>(target.SrcBlend), static_cast<int>(target.DestBlend), static_cast<int>(target.BlendOp)));
  }
  blends.emplace_back(game, layer);
  return layer.Get();
}

// The game's particle depth state `game` at the edges (Full-Resolution Particle Edges), made once: its own with stencil 1
// alone. Null: one with its own stencil test (those draws stay at full size).
inline ID3D11DepthStencilState* EdgeState(ID3D11Device* device, ID3D11DepthStencilState* game) {
  auto& states = runtime.objects.edge_states;
  if (const auto known = std::ranges::find(states, game, [](const auto& entry) { return entry.first.Get(); }); known != states.end()) {
    return known->second.Get();
  }
  // Unbound: D3D11's default.
  D3D11_DEPTH_STENCIL_DESC desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL, .DepthFunc = D3D11_COMPARISON_LESS};
  if (game != nullptr) game->GetDesc(&desc);
  ComPtr<ID3D11DepthStencilState> edge;
  if (!desc.StencilEnable) {
    desc.StencilEnable = TRUE;
    desc.StencilReadMask = 0xFF;
    desc.StencilWriteMask = 0;
    desc.FrontFace = STENCIL_EQUAL;
    desc.BackFace = STENCIL_EQUAL;
    device->CreateDepthStencilState(&desc, edge.GetAddressOf());
  }
  if (!edge && !runtime.stencil_refusal_logged) {
    runtime.stencil_refusal_logged = true;
    renodx::utils::log::i("[Total War] Full-Resolution Particle Edges: particles with their own stencil test: those stay at full size.");
  }
  states.emplace_back(game, edge);
  return edge.Get();
}

// The layer's shaders and states on `device` (made once), its textures for a light buffer `light_desc` (and with the edges
// a depth-stencil of `edges_format`, else none). Under the lock.
inline bool Prepare(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& light_desc, DXGI_FORMAT edges_format) {
  auto& objects = runtime.objects;
  if (objects.device.Get() != device) {
    objects = {.device = device};
    runtime.restart = true;
    D3D11_DEPTH_STENCIL_DESC depth_write = {
        .DepthEnable = TRUE,
        .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL,
        .DepthFunc = D3D11_COMPARISON_ALWAYS,
    };
    D3D11_BLEND_DESC composite = {};
    composite.RenderTarget[0] = {
        .BlendEnable = TRUE,
        .SrcBlend = D3D11_BLEND_ONE,
        .DestBlend = D3D11_BLEND_SRC_ALPHA,
        .BlendOp = D3D11_BLEND_OP_ADD,
        .SrcBlendAlpha = D3D11_BLEND_ZERO,
        .DestBlendAlpha = D3D11_BLEND_ONE,
        .BlendOpAlpha = D3D11_BLEND_OP_ADD,
        .RenderTargetWriteMask = static_cast<UINT8>(D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN
                                                    | D3D11_COLOR_WRITE_ENABLE_BLUE),
    };
    constexpr D3D11_DEPTH_STENCILOP_DESC MARK = {.StencilFailOp = D3D11_STENCIL_OP_KEEP, .StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
                                                 .StencilPassOp = D3D11_STENCIL_OP_INCR_SAT, .StencilFunc = D3D11_COMPARISON_ALWAYS};
    const D3D11_DEPTH_STENCIL_DESC edges_mark = {
        .DepthEnable = TRUE,
        .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL,
        .DepthFunc = D3D11_COMPARISON_ALWAYS,
        .StencilEnable = TRUE,
        .StencilReadMask = 0xFF,
        .StencilWriteMask = 0xFF,
        .FrontFace = MARK,
        .BackFace = MARK,
    };
    const D3D11_DEPTH_STENCIL_DESC composite_edges = {
        .DepthEnable = FALSE,
        .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO,
        .DepthFunc = D3D11_COMPARISON_ALWAYS,
        .StencilEnable = TRUE,
        .StencilReadMask = 0xFF,
        .StencilWriteMask = 0,
        .FrontFace = STENCIL_EQUAL,
        .BackFace = STENCIL_EQUAL,
    };
    const D3D11_BUFFER_DESC dimensions = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    objects.failed =
        !particle_history::MakeObjects(device, &objects.history)
        || FAILED(device->CreatePixelShader(__particle_depth.data(), __particle_depth.size(), nullptr, objects.depth_shader.GetAddressOf()))
        || FAILED(device->CreatePixelShader(__particle_composite.data(), __particle_composite.size(), nullptr,
                                            objects.composite_shader.GetAddressOf()))
        || FAILED(device->CreatePixelShader(__particle_edges.data(), __particle_edges.size(), nullptr, objects.edges_shader.GetAddressOf()))
        || FAILED(device->CreateDepthStencilState(&edges_mark, objects.edges_mark.GetAddressOf()))
        || FAILED(device->CreateDepthStencilState(&composite_edges, objects.composite_edges.GetAddressOf()))
        || FAILED(device->CreateDepthStencilState(&depth_write, objects.depth_write.GetAddressOf()))
        || FAILED(device->CreateBlendState(&composite, objects.composite_blend.GetAddressOf()))
        || FAILED(device->CreateBuffer(&dimensions, nullptr, objects.dimensions.GetAddressOf()));
    if (objects.failed) renodx::utils::log::w("[Total War] Particles at Half Resolution: could not create its shaders: off.");
  }
  if (objects.failed) return false;
  if (!objects.layer.texture || objects.light_width != light_desc.Width || objects.light_height != light_desc.Height
      || objects.scale != runtime.scale) {
    if (objects.failed_light_width == light_desc.Width && objects.failed_light_height == light_desc.Height && objects.scale == runtime.scale) {
      return false;
    }
    objects.layer = {};
    objects.depth_target.Reset();
    objects.depth_view.Reset();
    objects.scale = runtime.scale;
    objects.edges_target.Reset();  // made again at the new size below
    objects.edges_format = DXGI_FORMAT_UNKNOWN;
    runtime.restart = true;
    const auto scale = static_cast<UINT>(runtime.scale);
    const UINT width = (light_desc.Width + scale - 1) / scale;
    const UINT height = (light_desc.Height + scale - 1) / scale;
    const D3D11_TEXTURE2D_DESC depth_desc = {
        .Width = width,
        .Height = height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
    };
    const D3D11_DEPTH_STENCIL_VIEW_DESC depth_target = {.Format = DXGI_FORMAT_D32_FLOAT, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D};
    const D3D11_SHADER_RESOURCE_VIEW_DESC depth_view = {
        .Format = DXGI_FORMAT_R32_FLOAT,
        .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1},
    };
    ComPtr<ID3D11Texture2D> depth;
    if (!frame_history::MakeTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, &objects.layer)
        || FAILED(device->CreateTexture2D(&depth_desc, nullptr, depth.GetAddressOf()))
        || FAILED(device->CreateDepthStencilView(depth.Get(), &depth_target, objects.depth_target.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(depth.Get(), &depth_view, objects.depth_view.GetAddressOf()))) {
      objects.layer = {};
      objects.failed_light_width = light_desc.Width;
      objects.failed_light_height = light_desc.Height;
      renodx::utils::log::w("[Total War] Particles at Half Resolution: could not create its textures (", width, " x ", height,
                            "): off at this size.");
      return false;
    }
    objects.light_width = light_desc.Width;
    objects.light_height = light_desc.Height;
  }
  if (edges_format != objects.edges_format) {
    objects.edges_target.Reset();
    objects.edges_format = edges_format;
    const D3D11_TEXTURE2D_DESC edges_desc = {
        .Width = light_desc.Width,
        .Height = light_desc.Height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = edges_format,
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_DEPTH_STENCIL,
    };
    ComPtr<ID3D11Texture2D> edges;
    if (edges_format != DXGI_FORMAT_UNKNOWN
        && (FAILED(device->CreateTexture2D(&edges_desc, nullptr, edges.GetAddressOf()))
            || FAILED(device->CreateDepthStencilView(edges.Get(), nullptr, objects.edges_target.GetAddressOf())))) {
      objects.edges_target.Reset();
      renodx::utils::log::w("[Total War] Full-Resolution Particle Edges: could not create its depth-stencil: off at this size.");
    }
  }
  return true;
}

// The frame's first particle draw: the layer opened for the light buffer `light` at `viewport`, against the scene's depth
// `scene_depth` (the draw's read-only depth target), with the particles' camera `camera`: its depth made, cleared, the camera
// copied. Under the lock.
inline bool Open(ID3D11DeviceContext* native, ID3D11Device* device, ID3D11RenderTargetView* light, ID3D11DepthStencilView* scene_depth,
                 ID3D11Buffer* camera, const D3D11_VIEWPORT& viewport) {
  auto& objects = runtime.objects;
  ComPtr<ID3D11Resource> depth_resource;
  scene_depth->GetResource(depth_resource.GetAddressOf());
  if (objects.scene_depth != depth_resource) {
    objects.scene_depth_view.Reset();
    objects.scene_depth = depth_resource;
    ComPtr<ID3D11Texture2D> depth_texture;
    D3D11_TEXTURE2D_DESC depth_desc = {};
    if (SUCCEEDED(depth_resource.As(&depth_texture))) depth_texture->GetDesc(&depth_desc);
    const D3D11_SHADER_RESOURCE_VIEW_DESC view = {
        .Format = frame_history::DepthFormats(depth_desc.Format).second,
        .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1},
    };
    if (view.Format != DXGI_FORMAT_UNKNOWN) {
      device->CreateShaderResourceView(depth_resource.Get(), &view, objects.scene_depth_view.GetAddressOf());
    }
  }
  D3D11_BUFFER_DESC camera_desc = {};
  camera->GetDesc(&camera_desc);
  if (!objects.scene_depth_view || camera_desc.ByteWidth < RENDER_TARGET_DIMENSIONS + 16) return false;

  D3D11_TEXTURE2D_DESC layer_desc = {};
  objects.layer.texture->GetDesc(&layer_desc);
  const std::array<float, 4> dimensions = {static_cast<float>(layer_desc.Width), static_cast<float>(layer_desc.Height),
                                           1.f / static_cast<float>(layer_desc.Width), 1.f / static_cast<float>(layer_desc.Height)};
  if (objects.written_dimensions != dimensions) {
    native->UpdateSubresource(objects.dimensions.Get(), 0, nullptr, dimensions.data(), 0, 0);
    objects.written_dimensions = dimensions;
  }
  frame_history::KeepCamera(device, native, camera, &objects.camera);
  if (!objects.camera) return false;
  const D3D11_BOX target_dimensions = {.left = 0, .top = 0, .front = 0, .right = 16, .bottom = 1, .back = 1};
  native->CopySubresourceRegion(objects.camera.Get(), 0, RENDER_TARGET_DIMENSIONS, 0, 0, objects.dimensions.Get(), 0, &target_dimensions);

  runtime.render_viewport = viewport;
  runtime.layer_viewport = {
      .TopLeftX = std::floor(viewport.TopLeftX / runtime.scale),
      .TopLeftY = std::floor(viewport.TopLeftY / runtime.scale),
      .Width = std::ceil(viewport.Width / runtime.scale),
      .Height = std::ceil(viewport.Height / runtime.scale),
      .MinDepth = 0.f,
      .MaxDepth = 1.f,
  };
  particle_history::Upload(native,
                           {.history_most = 0.f,
                            .camera_cut = particle_history::CAMERA_CUT,
                            .region_width = runtime.layer_viewport.Width,
                            .region_height = runtime.layer_viewport.Height,
                            .layer_scale = runtime.scale},
                           &objects.history);
  const std::array<ID3D11Buffer*, particle_history::CONSTANTS_COUNT> constants = {objects.history.constants.Get(), nullptr, nullptr,
                                                                                  objects.camera.Get()};
  {
    const particle_history::DrawState game_state(native);
    native->OMSetRenderTargets(0, nullptr, objects.depth_target.Get());
    particle_history::DrawFullScreen(native, objects.history, runtime.layer_viewport, objects.depth_shader.Get(),
                                     {objects.scene_depth_view.Get()}, constants, nullptr, objects.depth_write.Get());
    // Full-Resolution Particle Edges: where the layer's texels were drawn against other depths.
    runtime.edges_open = (objects.edges_target != nullptr);
    if (runtime.edges_open) {
      native->ClearDepthStencilView(objects.edges_target.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.f, 0);
      native->OMSetRenderTargets(0, nullptr, objects.edges_target.Get());
      particle_history::DrawFullScreen(native, objects.history, viewport, objects.edges_shader.Get(),
                                       {nullptr, nullptr, objects.depth_view.Get(), objects.scene_depth_view.Get()}, constants, nullptr,
                                       objects.edges_mark.Get());
    }
  }
  constexpr float NOTHING_DRAWN[4] = {0.f, 0.f, 0.f, 1.f};
  native->ClearRenderTargetView(objects.layer.rtv.Get(), NOTHING_DRAWN);
  runtime.light = light;
  runtime.draws = 0;
  runtime.open_frame = runtime.frame;
  return true;
}

// A particle draw (the particles' vertex shader): into the layer (opened at the frame's first), put back after.
constexpr auto OnParticleDraw = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  if (half_resolution_row == 0.f) return {};
  if (command.cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return {};
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  if (!frame_history::MainViewport(native)) return {};
  const std::unique_lock lock(runtime.mutex);
  if (runtime.stopped || runtime.closed_frame == runtime.frame || runtime.refused_frame == runtime.frame) return {};

  // One target, the light buffer (the display's size; its view at the render size), and the scene's depth. The output stage's
  // UAVs too (their slots read as no target): the layer's targets would unbind them.
  std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> bound = {};
  ComPtr<ID3D11DepthStencilView> scene_depth;
  std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> bound_output_views = {};
  native->OMGetRenderTargetsAndUnorderedAccessViews(static_cast<UINT>(bound.size()), bound.data(), scene_depth.GetAddressOf(), 0,
                                                    static_cast<UINT>(bound_output_views.size()), bound_output_views.data());
  std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets;
  for (size_t index = 0; index < bound.size(); ++index) targets[index].Attach(bound[index]);
  std::array<ComPtr<ID3D11UnorderedAccessView>, D3D11_PS_CS_UAV_REGISTER_COUNT> output_views;
  for (size_t index = 0; index < bound_output_views.size(); ++index) output_views[index].Attach(bound_output_views[index]);
  if (!targets[0] || !scene_depth || std::ranges::any_of(targets.begin() + 1, targets.end(), [](const auto& target) { return target != nullptr; })) {
    return {};
  }
  ComPtr<ID3D11Resource> light_resource;
  targets[0]->GetResource(light_resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> light_texture;
  if (FAILED(light_resource.As(&light_texture))) return {};
  D3D11_TEXTURE2D_DESC light_desc = {};
  light_texture->GetDesc(&light_desc);
  if (light_desc.Width != runtime.display_width || light_desc.Height != runtime.display_height) return {};
  if (runtime.open_frame == runtime.frame && runtime.light != targets[0]) return {};

  ComPtr<ID3D11BlendState> game_blend;
  std::array<float, 4> blend_factor = {};
  UINT sample_mask = 0;
  native->OMGetBlendState(game_blend.GetAddressOf(), blend_factor.data(), &sample_mask);
  ComPtr<ID3D11RasterizerState> rasterizer;
  native->RSGetState(rasterizer.GetAddressOf());
  D3D11_RASTERIZER_DESC rasterizer_desc = {};
  if (rasterizer) rasterizer->GetDesc(&rasterizer_desc);
  ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  if (light_desc.SampleDesc.Count != 1 || rasterizer_desc.ScissorEnable
      || std::ranges::any_of(output_views, [](const auto& view) { return view != nullptr; })) {
    if (!runtime.state_refusal_logged) {
      runtime.state_refusal_logged = true;
      renodx::utils::log::i("[Total War] Particles at Half Resolution: particles with the game's MSAA, a scissor or UAVs: those stay "
                            "at full size.");
    }
    runtime.refused_frame = runtime.frame;
    return {};
  }
  // Full-Resolution Particle Edges: a depth-stencil as precise as the scene's depth.
  D3D11_DEPTH_STENCIL_VIEW_DESC scene_depth_desc = {};
  scene_depth->GetDesc(&scene_depth_desc);
  const bool float_depth = scene_depth_desc.Format == DXGI_FORMAT_D32_FLOAT || scene_depth_desc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  const DXGI_FORMAT edges_format = !runtime.edges ? DXGI_FORMAT_UNKNOWN : (float_depth ? DXGI_FORMAT_D32_FLOAT_S8X24_UINT : DXGI_FORMAT_D24_UNORM_S8_UINT);
  if (!Prepare(device.Get(), light_desc, edges_format)) {
    runtime.refused_frame = runtime.frame;
    return {};
  }
  ComPtr<ID3D11DepthStencilState> depth_state;
  UINT stencil_ref = 0;
  native->OMGetDepthStencilState(depth_state.GetAddressOf(), &stencil_ref);
  ID3D11DepthStencilState* edge_state = (runtime.objects.edges_target != nullptr) ? EdgeState(device.Get(), depth_state.Get()) : nullptr;
  if (runtime.objects.edges_target != nullptr && edge_state == nullptr) {
    runtime.refused_frame = runtime.frame;
    return {};
  }
  ID3D11BlendState* layer_blend = LayerBlend(device.Get(), game_blend.Get());
  if (layer_blend == nullptr) {
    runtime.refused_frame = runtime.frame;
    return {};
  }
  auto& game = runtime.game;
  game.viewport_count = static_cast<UINT>(game.viewports.size());
  native->RSGetViewports(&game.viewport_count, game.viewports.data());
  if (game.viewport_count == 0) return {};
  ComPtr<ID3D11Buffer> camera;
  native->VSGetConstantBuffers(internal::CAMERA_SLOT, 1, camera.GetAddressOf());
  if (!camera) return {};
  if (runtime.open_frame != runtime.frame
      && !Open(native, device.Get(), targets[0].Get(), scene_depth.Get(), camera.Get(), game.viewports[0])) {
    runtime.refused_frame = runtime.frame;
    return {};
  }

  auto& objects = runtime.objects;
  game.target = targets[0];
  game.depth = scene_depth;
  game.blend = game_blend;
  game.blend_factor = blend_factor;
  game.sample_mask = sample_mask;
  game.depth_state = depth_state;
  game.stencil_ref = stencil_ref;
  game.edge_state = runtime.edges_open ? edge_state : nullptr;
  game.cameras[0] = camera;
  native->GSGetConstantBuffers(internal::CAMERA_SLOT, 1, game.cameras[1].ReleaseAndGetAddressOf());
  native->PSGetConstantBuffers(internal::CAMERA_SLOT, 1, game.cameras[2].ReleaseAndGetAddressOf());
  native->PSGetConstantBuffers(particle_history::CONSTANTS_SLOT, 1, game.layer_constants.ReleaseAndGetAddressOf());
  native->OMSetRenderTargets(1, objects.layer.rtv.GetAddressOf(), objects.depth_target.Get());
  native->RSSetViewports(1, &runtime.layer_viewport);
  native->OMSetBlendState(layer_blend, blend_factor.data(), sample_mask);
  native->VSSetConstantBuffers(internal::CAMERA_SLOT, 1, objects.camera.GetAddressOf());
  native->GSSetConstantBuffers(internal::CAMERA_SLOT, 1, objects.camera.GetAddressOf());
  native->PSSetConstantBuffers(internal::CAMERA_SLOT, 1, objects.camera.GetAddressOf());
  native->PSSetConstantBuffers(particle_history::CONSTANTS_SLOT, 1, objects.history.constants.GetAddressOf());
  ++runtime.draws;
  return {.finalize_callback = [](Context& post, const void*) {
    auto* post_native = reinterpret_cast<ID3D11DeviceContext*>(post.cmd_list->get_native());
    renodx::utils::directx::TryNativeFromReShadeProxy(&post_native);
    const std::unique_lock post_lock(runtime.mutex);
    auto& put_back = runtime.game;
    post_native->OMSetRenderTargets(1, put_back.target.GetAddressOf(), put_back.depth.Get());
    post_native->RSSetViewports(put_back.viewport_count, put_back.viewports.data());
    post_native->OMSetBlendState(put_back.blend.Get(), put_back.blend_factor.data(), put_back.sample_mask);
    post_native->VSSetConstantBuffers(internal::CAMERA_SLOT, 1, put_back.cameras[0].GetAddressOf());
    post_native->GSSetConstantBuffers(internal::CAMERA_SLOT, 1, put_back.cameras[1].GetAddressOf());
    post_native->PSSetConstantBuffers(internal::CAMERA_SLOT, 1, put_back.cameras[2].GetAddressOf());
    post_native->PSSetConstantBuffers(particle_history::CONSTANTS_SLOT, 1, put_back.layer_constants.GetAddressOf());
    // Full-Resolution Particle Edges: the draw again as the game drew it, at the edges alone.
    if (put_back.edge_state) {
      post_native->OMSetRenderTargets(1, put_back.target.GetAddressOf(), runtime.objects.edges_target.Get());
      post_native->OMSetDepthStencilState(put_back.edge_state.Get(), 1);
      const auto& arguments = post.arguments;
      if constexpr (std::is_same_v<typename Context::ArgumentType, renodx::utils::command_action::DrawIndexedArguments>) {
        post_native->DrawIndexedInstanced(arguments.index_count, arguments.instance_count, arguments.first_index, arguments.vertex_offset,
                                          arguments.first_instance);
      } else if constexpr (std::is_same_v<typename Context::ArgumentType, renodx::utils::command_action::DrawArguments>) {
        post_native->DrawInstanced(arguments.vertex_count, arguments.instance_count, arguments.first_vertex, arguments.first_instance);
      }
      post_native->OMSetRenderTargets(1, put_back.target.GetAddressOf(), put_back.depth.Get());
      post_native->OMSetDepthStencilState(put_back.depth_state.Get(), put_back.stencil_ref);
    }
    put_back = {};
  }};
};

// The light buffer's next reader after the particles: the open layer composited into it first (with Smooth Half-Size
// Particles, smoothed first).
constexpr auto OnLayerReader = []<typename Context>(Context& command) -> renodx::utils::command_action::CallbackResult<Context> {
  auto* native = reinterpret_cast<ID3D11DeviceContext*>(command.cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  if (native == nullptr || native->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return {};
  const std::unique_lock lock(runtime.mutex);
  if (runtime.open_frame != runtime.frame) return {};
  auto& objects = runtime.objects;
  ID3D11ShaderResourceView* layer = objects.layer.srv.Get();
  if (runtime.smoothing) {
    const auto smoothed = particle_history::Resolve(native,
                                                    {.colour = layer,
                                                     .depth = objects.depth_view.Get(),
                                                     .camera = objects.camera.Get(),
                                                     .viewport = runtime.layer_viewport,
                                                     .scale = runtime.scale},
                                                    runtime.frame, runtime.restart, &objects.history);
    runtime.restart = false;
    if (smoothed[0] != nullptr) layer = smoothed[0];
  }
  {
    const particle_history::DrawState game_state(native);
    native->OMSetRenderTargets(1, runtime.light.GetAddressOf(), runtime.edges_open ? objects.edges_target.Get() : nullptr);
    particle_history::DrawFullScreen(native, objects.history, runtime.render_viewport, objects.composite_shader.Get(),
                                     {layer, nullptr, objects.depth_view.Get(), objects.scene_depth_view.Get()},
                                     {objects.history.constants.Get(), nullptr, nullptr, objects.camera.Get()},
                                     objects.composite_blend.Get(), runtime.edges_open ? objects.composite_edges.Get() : objects.history.no_depth.Get());
  }
  if (!runtime.applied_logged) {
    runtime.applied_logged = true;
    renodx::utils::log::i(std::format("[Total War] Particles at Half Resolution: the first layer ({} particle draws) went in before shader {:08X}.",
                                      runtime.draws, command.matched_shader_hash));
  }
  runtime.open_frame = UINT64_MAX;
  runtime.closed_frame = runtime.frame;
  runtime.edges_open = false;
  runtime.light.Reset();
  return {};
};

}  // namespace internal

// The particles' vertex shader (`vertex_shader`) and the light buffer's readers after them (`readers`), kept while installed.
// Late: the other modules' callbacks on the particle draws see the game's state.
inline void Install(uint32_t vertex_shader, std::span<const uint32_t> readers) {
  const auto draws = renodx::utils::command_action::COMMAND_TYPE_DIRECT_DRAW;
  renodx::utils::command_action::Register(internal::OnParticleDraw, {.shader_hash = vertex_shader, .command_types = draws});
  for (const uint32_t reader : readers) {
    renodx::utils::command_action::Register(internal::OnLayerReader, {.shader_hash = reader, .command_types = draws});
  }
}

inline void Uninstall() {
  renodx::utils::command_action::Unregister(internal::OnLayerReader);
  renodx::utils::command_action::Unregister(internal::OnParticleDraw);
  const std::unique_lock lock(internal::runtime.mutex);
  internal::runtime.objects = {};
}

// Every present: the main view's size (the display's), the frame counted. A layer nothing composited (no reader after the
// particles): its particles were lost this frame, no layer from there until the row is turned off and on (one log line).
// Off: its memory back.
inline void Update(reshade::api::swapchain* swapchain) {
  auto& runtime = internal::runtime;
  reshade::api::resource_desc display = {};
  if (half_resolution_row != 0.f) display = swapchain->get_device()->get_resource_desc(swapchain->get_current_back_buffer());
  const std::unique_lock lock(runtime.mutex);
  if (runtime.open_frame == runtime.frame) {
    runtime.open_frame = UINT64_MAX;
    runtime.light.Reset();
    runtime.stopped = true;
    if (!runtime.lost_logged) {
      runtime.lost_logged = true;
      renodx::utils::log::w("[Total War] Particles at Half Resolution: nothing read the light buffer after the particles: stopped.");
    }
  }
  runtime.display_width = display.texture.width;
  runtime.display_height = display.texture.height;
  if (const float scale = (half_resolution_row >= 2.f) ? 4.f : particle_history::LAYER_SCALE; scale != runtime.scale) {
    runtime.scale = scale;
    runtime.restart = true;
  }
  runtime.edges = (edges_row != 0.f);
  const bool smoothing = (smoothing_row != 0.f);
  if (smoothing != runtime.smoothing) {
    runtime.smoothing = smoothing;
    runtime.restart = true;
    if (!smoothing) particle_history::ReleaseHistories(&runtime.objects.history);
  }
  if (half_resolution_row == 0.f) {
    runtime.stopped = false;
    if (runtime.objects.device) runtime.objects = {};
  }
  ++runtime.frame;
}

// The game's device is going away: the addon's objects on it with it.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  auto* native = reinterpret_cast<ID3D11Device*>(device->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&native);
  const std::unique_lock lock(internal::runtime.mutex);
  if (internal::runtime.objects.device.Get() != native) return;
  internal::runtime.objects = {};
  internal::runtime.open_frame = UINT64_MAX;
  internal::runtime.light.Reset();
  internal::runtime.game = {};
}

}  // namespace twattila::particle_layer
