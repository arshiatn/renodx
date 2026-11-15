/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <d3d9.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <include/reshade.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <unordered_set>
#include <utility>

#include "../../utils/device_proxy.hpp"
#include "../twattila/dlaa_bridge.hpp"

// R.U.S.E.'s D3D9 bridge (tools/nontw/ruse/plan.md 10b W2, W3, D2-D4): DLAA through the TW helper on the DX11 proxy's
// device. The game's D3D9Ex device (the swap chain mod's upgrade) and the proxy share four textures (D3D11 makes them
// with a KMT handle, D3D9Ex opens it). Each frame, before the bloom reads the resolved main target: one D3D9 pass writes
// the scene as light, the INTZ depth and the camera's motion into them; the proxy copies them into the helper's
// textures, DLAA runs, and a second D3D9 pass writes the result back into the resolved target in the game's encoding.
// The scene draws (back-buffer-sized target, an INTZ depth) get the jitter in their camera registers c0-c3. CPU waits on
// both sides (D3D9 has no fences the proxy shares yet).
namespace ruse::bridge {

using Microsoft::WRL::ComPtr;

struct SharedTexture {
  ComPtr<ID3D11Texture2D> proxy;
  ComPtr<IDirect3DTexture9> host;
  ComPtr<IDirect3DSurface9> host_surface;
};

struct State {
  bool enabled = false;  // the DLAA row (restart)
  bool failed = false;   // a step failed for good: no jitter, the game's picture
  bool ready = false;
  bool in_scene = false;  // between the first scene draw and the bloom
  bool jitter_written = false;
  bool reset = true;
  uint32_t frame = 0;
  UINT width = 0;
  UINT height = 0;
  std::array<float, 2> jitter = {};  // pixels, x right, y down
  std::array<float, 16> vertex_camera = {};  // the game's c0-c3 as it set them
  std::array<float, 48> pixel_registers = {};  // the game's PS c0-c11 as last set
  std::array<float, 36> camera = {};  // 0x88292FC6's c0-c8: previous view-projection, inverse view-projection, RenderDt
  bool camera_valid = false;
  float hdr_param = 1.f;  // the final post's c11.x (PrincipalMap_HDRParam), last frame's
  uint64_t render_target = 0;
  uint64_t depth_stencil = 0;
  uint64_t scene_depth = 0;  // the INTZ depth the last scene draw used
  uint64_t pixel_texture_0 = 0;  // the pixel shader's s0 as last bound
  bool render_target_is_frame_sized = false;
  std::unordered_set<uint64_t> intz_depths;
  SharedTexture color;
  SharedTexture depth;
  SharedTexture motion;
  SharedTexture output;
  ComPtr<IDirect3DVertexShader9> quad;
  ComPtr<IDirect3DPixelShader9> prepare;
  ComPtr<IDirect3DPixelShader9> writeback;
  ComPtr<IDirect3DVertexDeclaration9> quad_layout;
};
inline State state;

inline float Halton(uint32_t index, uint32_t base) {
  float result = 0.f;
  float fraction = 1.f / static_cast<float>(base);
  for (uint32_t i = index; i > 0; i /= base, fraction /= static_cast<float>(base)) {
    result += fraction * static_cast<float>(i % base);
  }
  return result;
}

inline bool MakeShared(ID3D11Device* proxy, IDirect3DDevice9Ex* host, DXGI_FORMAT proxy_format, D3DFORMAT host_format,
                       SharedTexture* texture) {
  const D3D11_TEXTURE2D_DESC desc = {
      .Width = state.width,
      .Height = state.height,
      .MipLevels = 1,
      .ArraySize = 1,
      .Format = proxy_format,
      .SampleDesc = {.Count = 1},
      .Usage = D3D11_USAGE_DEFAULT,
      .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
      .MiscFlags = D3D11_RESOURCE_MISC_SHARED,
  };
  ComPtr<IDXGIResource> resource;
  HANDLE handle = nullptr;
  return SUCCEEDED(proxy->CreateTexture2D(&desc, nullptr, texture->proxy.GetAddressOf()))
         && SUCCEEDED(texture->proxy.As(&resource)) && SUCCEEDED(resource->GetSharedHandle(&handle))
         && SUCCEEDED(host->CreateTexture(state.width, state.height, 1, D3DUSAGE_RENDERTARGET, host_format, D3DPOOL_DEFAULT,
                                          texture->host.GetAddressOf(), &handle))
         && SUCCEEDED(texture->host->GetSurfaceLevel(0, texture->host_surface.GetAddressOf()));
}

// W3: the D3D9 queue drained (an event query), before D3D11 reads what it wrote, or the other way round.
inline void WaitHost(IDirect3DDevice9* host) {
  ComPtr<IDirect3DQuery9> query;
  if (FAILED(host->CreateQuery(D3DQUERYTYPE_EVENT, query.GetAddressOf()))) return;
  query->Issue(D3DISSUE_END);
  while (query->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
    YieldProcessor();
  }
}

inline void WaitProxy(ID3D11Device* proxy, ID3D11DeviceContext* context) {
  const D3D11_QUERY_DESC desc = {.Query = D3D11_QUERY_EVENT};
  ComPtr<ID3D11Query> query;
  if (FAILED(proxy->CreateQuery(&desc, query.GetAddressOf()))) return;
  context->End(query.Get());
  BOOL done = FALSE;
  while (context->GetData(query.Get(), &done, sizeof(done), 0) == S_FALSE || done == FALSE) {
    YieldProcessor();
  }
}

// One full-screen D3D9 pass (W2): the game's state kept as ReShade's own D3D9 passes keep it (a state block plus the
// targets, depth and viewport, which it doesn't hold).
inline void DrawQuad(IDirect3DDevice9* host, std::span<IDirect3DSurface9* const> targets, IDirect3DPixelShader9* shader,
                     std::span<IDirect3DBaseTexture9* const> textures, std::span<const float> pixel_constants) {
  ComPtr<IDirect3DStateBlock9> saved;
  if (FAILED(host->CreateStateBlock(D3DSBT_ALL, saved.GetAddressOf()))) return;
  saved->Capture();
  std::array<ComPtr<IDirect3DSurface9>, 3> saved_targets;
  for (DWORD i = 0; i < saved_targets.size(); ++i) {
    host->GetRenderTarget(i, saved_targets[i].GetAddressOf());
  }
  ComPtr<IDirect3DSurface9> saved_depth;
  host->GetDepthStencilSurface(saved_depth.GetAddressOf());
  D3DVIEWPORT9 saved_viewport = {};
  host->GetViewport(&saved_viewport);

  for (DWORD i = 0; i < saved_targets.size(); ++i) {
    host->SetRenderTarget(i, (i < targets.size()) ? targets[i] : nullptr);
  }
  host->SetDepthStencilSurface(nullptr);
  const D3DVIEWPORT9 viewport = {.X = 0, .Y = 0, .Width = state.width, .Height = state.height, .MinZ = 0.f, .MaxZ = 1.f};
  host->SetViewport(&viewport);
  for (const auto [render_state, value] : std::initializer_list<std::pair<D3DRENDERSTATETYPE, DWORD>>{
           {D3DRS_ZENABLE, FALSE}, {D3DRS_ZWRITEENABLE, FALSE}, {D3DRS_ALPHABLENDENABLE, FALSE}, {D3DRS_ALPHATESTENABLE, FALSE},
           {D3DRS_CULLMODE, D3DCULL_NONE}, {D3DRS_STENCILENABLE, FALSE}, {D3DRS_SCISSORTESTENABLE, FALSE},
           {D3DRS_SRGBWRITEENABLE, FALSE}, {D3DRS_COLORWRITEENABLE, 0xF}, {D3DRS_COLORWRITEENABLE1, 0xF},
           {D3DRS_COLORWRITEENABLE2, 0xF}}) {
    host->SetRenderState(render_state, value);
  }
  for (DWORD i = 0; i < textures.size(); ++i) {
    host->SetTexture(i, textures[i]);
    host->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    host->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    host->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    host->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    host->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    host->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, FALSE);
  }
  const std::array<float, 4> half_pixel = {-1.f / static_cast<float>(state.width), 1.f / static_cast<float>(state.height), 0.f, 0.f};
  host->SetVertexShader(state.quad.Get());
  host->SetVertexShaderConstantF(0, half_pixel.data(), 1);
  host->SetPixelShader(shader);
  host->SetPixelShaderConstantF(0, pixel_constants.data(), static_cast<UINT>(pixel_constants.size() / 4));
  host->SetVertexDeclaration(state.quad_layout.Get());
  constexpr std::array<float, 8> STRIP = {-1.f, 1.f, 1.f, 1.f, -1.f, -1.f, 1.f, -1.f};
  host->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, STRIP.data(), 2 * sizeof(float));

  saved->Apply();
  for (DWORD i = 0; i < saved_targets.size(); ++i) {
    host->SetRenderTarget(i, saved_targets[i].Get());
  }
  host->SetDepthStencilSurface(saved_depth.Get());
  host->SetViewport(&saved_viewport);
}

// Lazily, on the first evaluation: the shaders, the shared textures and the helper's (Prepare starts the helper).
inline bool Ready(IDirect3DDevice9* host, std::span<const uint8_t> quad, std::span<const uint8_t> prepare,
                  std::span<const uint8_t> writeback) {
  if (state.ready || state.failed) return state.ready;
  state.failed = true;
  auto* proxy_reshade = renodx::utils::device_proxy::proxy_device_reshade;
  ComPtr<IDirect3DDevice9Ex> host_ex;
  if (proxy_reshade == nullptr || FAILED(host->QueryInterface(IID_PPV_ARGS(host_ex.GetAddressOf())))) return false;
  auto* proxy = reinterpret_cast<ID3D11Device*>(proxy_reshade->get_native());
  constexpr std::array<D3DVERTEXELEMENT9, 2> LAYOUT = {
      {{0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()}};
  const bool made = SUCCEEDED(host->CreateVertexShader(reinterpret_cast<const DWORD*>(quad.data()), state.quad.GetAddressOf()))
                    && SUCCEEDED(host->CreatePixelShader(reinterpret_cast<const DWORD*>(prepare.data()), state.prepare.GetAddressOf()))
                    && SUCCEEDED(host->CreatePixelShader(reinterpret_cast<const DWORD*>(writeback.data()), state.writeback.GetAddressOf()))
                    && SUCCEEDED(host->CreateVertexDeclaration(LAYOUT.data(), state.quad_layout.GetAddressOf()))
                    && MakeShared(proxy, host_ex.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, D3DFMT_A16B16G16R16F, &state.color)
                    && MakeShared(proxy, host_ex.Get(), DXGI_FORMAT_R32_FLOAT, D3DFMT_R32F, &state.depth)
                    && MakeShared(proxy, host_ex.Get(), DXGI_FORMAT_R16G16_FLOAT, D3DFMT_G16R16F, &state.motion)
                    && MakeShared(proxy, host_ex.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, D3DFMT_A16B16G16R16F, &state.output)
                    && twattila::dlaa_bridge::Prepare(proxy, state.width, state.height);
  if (!made) {
    reshade::log::message(reshade::log::level::warning, "[R.U.S.E.] DLAA: the bridge didn't start (helper missing?): off");
    return false;
  }
  state.failed = false;
  state.ready = true;
  return true;
}

// D4, before the bloom draw: the resolved main target (the bloom's s0) through DLAA and back; any failure leaves the
// game's picture.
inline void Evaluate(reshade::api::command_list* cmd_list, std::span<const uint8_t> quad, std::span<const uint8_t> prepare,
                     std::span<const uint8_t> writeback) {
  state.in_scene = false;
  if (!state.enabled || state.failed || !state.camera_valid || state.pixel_texture_0 == 0 || state.scene_depth == 0) return;
  auto* host = reinterpret_cast<IDirect3DDevice9*>(cmd_list->get_device()->get_native());
  if (!Ready(host, quad, prepare, writeback)) return;
  auto* scene = reinterpret_cast<IDirect3DTexture9*>(state.pixel_texture_0);
  ComPtr<IDirect3DTexture9> depth;
  ComPtr<IDirect3DSurface9> scene_surface;
  if (FAILED(reinterpret_cast<IDirect3DSurface9*>(state.scene_depth)->GetContainer(IID_PPV_ARGS(depth.GetAddressOf())))
      || FAILED(scene->GetSurfaceLevel(0, scene_surface.GetAddressOf()))) {
    return;
  }
  std::array<float, 40> constants = {};
  std::memcpy(constants.data(), state.camera.data(), sizeof(state.camera));
  constants[36] = static_cast<float>(state.width);
  constants[37] = static_cast<float>(state.height);
  constants[38] = state.hdr_param;
  const std::array<IDirect3DSurface9*, 3> inputs = {state.color.host_surface.Get(), state.depth.host_surface.Get(),
                                                    state.motion.host_surface.Get()};
  const std::array<IDirect3DBaseTexture9*, 2> sources = {scene, depth.Get()};
  DrawQuad(host, inputs, state.prepare.Get(), sources, constants);
  WaitHost(host);

  auto* proxy = reinterpret_cast<ID3D11Device*>(renodx::utils::device_proxy::proxy_device_reshade->get_native());
  ComPtr<ID3D11DeviceContext> context;
  proxy->GetImmediateContext(context.GetAddressOf());
  auto& helper = twattila::dlaa_bridge::bridge;
  context->CopyResource(helper.color.texture.Get(), state.color.proxy.Get());
  context->CopyResource(helper.depth.texture.Get(), state.depth.proxy.Get());
  context->CopyResource(helper.motion.texture.Get(), state.motion.proxy.Get());
  const bool evaluated = twattila::dlaa_bridge::Evaluate(context.Get(), state.width, state.height, state.jitter[0], state.jitter[1],
                                                         state.reset, 0u);
  state.reset = !evaluated;
  if (!evaluated) return;
  context->CopyResource(state.output.proxy.Get(), helper.output.texture.Get());
  WaitProxy(proxy, context.Get());

  const std::array<IDirect3DSurface9*, 1> target = {scene_surface.Get()};
  const std::array<IDirect3DBaseTexture9*, 1> result = {state.output.host.Get()};
  DrawQuad(host, target, state.writeback.Get(), result, constants);
}

// D3: the scene draws' camera (c0-c3, row-major MVP: clip_i = row_i . position) moved by the jitter: rows 0 and 1 +=
// offset x row 3 (clip.xy += offset x clip.w), the game's values back on every other draw.
inline void OnDraw(reshade::api::command_list* cmd_list) {
  if (!state.enabled || state.failed) return;
  const bool scene_draw = state.render_target_is_frame_sized && state.intz_depths.contains(state.depth_stencil);
  if (scene_draw) {
    state.in_scene = true;
    state.scene_depth = state.depth_stencil;
  }
  auto* host = reinterpret_cast<IDirect3DDevice9*>(cmd_list->get_device()->get_native());
  if (scene_draw && state.ready) {
    std::array<float, 16> jittered = state.vertex_camera;
    const float offset_x = 2.f * state.jitter[0] / static_cast<float>(state.width);
    const float offset_y = -2.f * state.jitter[1] / static_cast<float>(state.height);
    for (int column = 0; column < 4; ++column) {
      jittered[column] += offset_x * state.vertex_camera[12 + column];
      jittered[4 + column] += offset_y * state.vertex_camera[12 + column];
    }
    host->SetVertexShaderConstantF(0, jittered.data(), 4);
    state.jitter_written = true;
  } else if (state.jitter_written) {
    host->SetVertexShaderConstantF(0, state.vertex_camera.data(), 4);
    state.jitter_written = false;
  }
}

// The game's float constants as it sets them: ReShade's D3D9 global layout has them at parameter 2 (vertex) and 5
// (pixel), offsets in floats (registers x 4); int and bool constants are other parameters.
inline void OnPushConstants(uint32_t layout_param, uint32_t first, uint32_t count, const void* values) {
  std::span<float> registers;
  if (layout_param == 2) {
    registers = state.vertex_camera;
  } else if (layout_param == 5) {
    registers = state.pixel_registers;
  } else {
    return;
  }
  const auto* floats = static_cast<const float*>(values);
  for (uint32_t i = 0; i < count && first + i < registers.size(); ++i) {
    registers[first + i] = floats[i];
  }
}

inline void OnPresent() {
  state.in_scene = false;
  ++state.frame;
  state.jitter = {Halton((state.frame % 8) + 1, 2) - 0.5f, Halton((state.frame % 8) + 1, 3) - 0.5f};
}

}  // namespace ruse::bridge
