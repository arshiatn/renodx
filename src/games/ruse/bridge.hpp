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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>

#include "../../utils/device_proxy.hpp"
#include "../../utils/gpu_fence.hpp"
#include "../twattila/dlaa_bridge.hpp"
#include "../twattila/frame_generation.hpp"

// R.U.S.E.'s D3D9 bridge (tools/nontw/ruse/plan.md 10b W2, W3, D2-D5): DLAA, FSR or XeSS (native) through the TW helper
// on the DX11 proxy's device. The game's D3D9Ex device (the swap chain mod's upgrade) and the proxy share four textures
// (D3D9Ex makes them with a KMT handle, D3D11 opens it). Each frame, before the bloom reads the resolved main target: one
// D3D9 pass writes the scene as light, the INTZ depth and the camera's motion into them; the proxy copies them into the
// helper's textures, the upscaler runs, and a second D3D9 pass writes the result back into the resolved target in the
// game's encoding.
// The scene draws (back-buffer-sized target, an INTZ depth) get the jitter in their camera registers c0-c3. Each side
// waits for the other's writes on the CPU, or (the GPU Sync row) on the GPU through utils/gpu_fence's shared fences.
// Frame generation (twattila/frame_generation.hpp, the helper presents): the final post's target (the back buffer's FP16
// clone) copied before the first draw after it (the UI's) into a fifth shared texture, the HUD-less image the proxy draws
// through its swap chain shader at its present; the frame's inputs are the upscaler's.
namespace ruse::bridge {

using Microsoft::WRL::ComPtr;

struct SharedTexture {
  ComPtr<ID3D11Texture2D> proxy;
  ComPtr<IDirect3DTexture9> host;
  ComPtr<IDirect3DSurface9> host_surface;
  // Written by compute on the proxy (MakeShared's unordered_access): there into its own texture, copied into `proxy`
  // before the game's device reads it (the shared texture opens on D3D11 without unordered access).
  ComPtr<ID3D11Texture2D> work;
  ComPtr<ID3D11UnorderedAccessView> work_uav;
};

struct State {
  bool enabled = false;  // the Anti-Aliasing row (restart)
  uint32_t upscaler = twattila::dlaa_bridge::UPSCALER_DLSS;
  bool gpu_sync = false;  // the GPU Sync row (restart): gpu_fence's attached before the game's device
  bool ray_tracing = false;  // the helper started with its ray tracing (ray_tracing.hpp's row, restart)
  bool guides = false;  // with the helper's guides (ray_tracing.hpp's traced bounce or reflections, restart)
  bool reflections = false;  // with the helper's reflections (ray_tracing.hpp's traced reflections, restart)
  bool failed = false;   // a step failed for good: no jitter, the game's picture
  // The game's own anti-aliasing (MSAA) at this start: its scene depth's samples (0: single-sample). Its main pass then
  // has no readable depth (only the ground pre-pass's is single-sample): no scene draws, the game's picture.
  uint32_t game_samples = 0;
  bool ready = false;
  bool scene_closed = false;  // from the bloom to the present: no scene draws (the final post's and the UI's)
  bool jitter_written = false;
  bool reset = true;
  uint32_t changes_logged = 0;  // the result's comings and goings logged this start (a few: never a line a frame)
  uint32_t frame = 0;
  uint32_t depth_frame = UINT32_MAX;  // the frame whose main depth `depth` holds (Evaluate's prepare pass)
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
  // The final post's depth (the back buffer's, INTZ too: back-buffer-sized), never a scene depth: the water simulation
  // draws with it into the frame-sized pre-pass target at the frame's start (B0 trace).
  uint64_t post_depth = 0;
  uint64_t pixel_texture_0 = 0;  // the pixel shader's s0 as last bound
  bool render_target_is_frame_sized = false;
  std::unordered_set<uint64_t> intz_depths;
  reshade::api::device* host_device = nullptr;  // the game's D3D9 device (its INTZ depths' maker)
  SharedTexture color;
  SharedTexture depth;
  SharedTexture motion;
  SharedTexture output;
  // Frame generation: the final post's target until its HUD-less image is copied (at the next draw or the present), the
  // copy (made with the first), the frames it and Evaluate's inputs are of, and whether the proxy's present takes them.
  ComPtr<IDirect3DSurface9> post_surface;
  SharedTexture hudless;
  bool hudless_failed = false;
  uint32_t hudless_frame = UINT32_MAX;
  twattila::frame_generation::Inputs generation_inputs;
  uint32_t generation_frame = UINT32_MAX;
  bool generation_ready = false;
  ComPtr<IDirect3DVertexShader9> quad;
  ComPtr<IDirect3DPixelShader9> prepare;
  ComPtr<IDirect3DPixelShader9> writeback;
  ComPtr<IDirect3DVertexDeclaration9> quad_layout;
  // GPU Sync: D3D9 signals, D3D11 waits (`host_fence`, a KMT fence of the game's D3D9Ex device), and back (`proxy_fence`).
  std::unique_ptr<renodx::utils::gpu_fence::Fence> host_fence;
  std::unique_ptr<renodx::utils::gpu_fence::Fence> proxy_fence;
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

// D3D9Ex makes the shared texture and the proxy opens it (a render target and shader resource there): one the proxy
// makes opens in D3D9Ex only in surface sharing's few formats (RGBA16F, RGB10A2, BGRA8; R32F and RG16F refused,
// E_INVALIDARG: test/tw_ruse_bridge H1, 2026-11-16).
inline bool MakeShared(ID3D11Device* proxy, IDirect3DDevice9Ex* host, D3DFORMAT host_format, SharedTexture* texture,
                       bool unordered_access = false) {
  HANDLE handle = nullptr;
  if (FAILED(host->CreateTexture(state.width, state.height, 1, D3DUSAGE_RENDERTARGET, host_format, D3DPOOL_DEFAULT,
                                 texture->host.ReleaseAndGetAddressOf(), &handle))
      || FAILED(texture->host->GetSurfaceLevel(0, texture->host_surface.ReleaseAndGetAddressOf()))
      || FAILED(proxy->OpenSharedResource(handle, IID_PPV_ARGS(texture->proxy.ReleaseAndGetAddressOf())))) {
    return false;
  }
  if (!unordered_access) return true;
  D3D11_TEXTURE2D_DESC desc = {};
  texture->proxy->GetDesc(&desc);
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
  desc.MiscFlags = 0;
  return SUCCEEDED(proxy->CreateTexture2D(&desc, nullptr, texture->work.ReleaseAndGetAddressOf()))
         && SUCCEEDED(proxy->CreateUnorderedAccessView(texture->work.Get(), nullptr, texture->work_uav.ReleaseAndGetAddressOf()));
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
  // S_OK: done; an error (a removed device) never becomes S_OK.
  BOOL done = FALSE;
  while (context->GetData(query.Get(), &done, sizeof(done), 0) == S_FALSE) {
    YieldProcessor();
  }
}

// One full-screen D3D9 pass (W2) over the first target's size: the game's state kept as ReShade's own D3D9 passes keep it
// (a state block plus the targets, depth and viewport, which it doesn't hold); `states` after the pass's own (a blend).
inline void DrawQuad(IDirect3DDevice9* host, std::span<IDirect3DSurface9* const> targets, IDirect3DPixelShader9* shader,
                     std::span<IDirect3DBaseTexture9* const> textures, std::span<const float> pixel_constants,
                     std::initializer_list<std::pair<D3DRENDERSTATETYPE, DWORD>> states = {}) {
  D3DSURFACE_DESC target_desc = {};
  if (targets.empty() || FAILED(targets[0]->GetDesc(&target_desc))) return;
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
  const D3DVIEWPORT9 viewport = {.X = 0, .Y = 0, .Width = target_desc.Width, .Height = target_desc.Height, .MinZ = 0.f, .MaxZ = 1.f};
  host->SetViewport(&viewport);
  for (const auto [render_state, value] : std::initializer_list<std::pair<D3DRENDERSTATETYPE, DWORD>>{
           {D3DRS_ZENABLE, FALSE}, {D3DRS_ZWRITEENABLE, FALSE}, {D3DRS_ALPHABLENDENABLE, FALSE}, {D3DRS_ALPHATESTENABLE, FALSE},
           {D3DRS_CULLMODE, D3DCULL_NONE}, {D3DRS_STENCILENABLE, FALSE}, {D3DRS_SCISSORTESTENABLE, FALSE},
           {D3DRS_SRGBWRITEENABLE, FALSE}, {D3DRS_COLORWRITEENABLE, 0xF}, {D3DRS_COLORWRITEENABLE1, 0xF},
           {D3DRS_COLORWRITEENABLE2, 0xF}}) {
    host->SetRenderState(render_state, value);
  }
  for (const auto [render_state, value] : states) {
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
  const std::array<float, 4> half_pixel = {-1.f / static_cast<float>(target_desc.Width), 1.f / static_cast<float>(target_desc.Height), 0.f, 0.f};
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

// The Anti-Aliasing row's upscaler, as the log names it.
inline const char* UpscalerName() {
  switch (state.upscaler) {
    case twattila::dlaa_bridge::UPSCALER_FSR:  return "FSR Native AA";
    case twattila::dlaa_bridge::UPSCALER_XESS: return "XeSS Native AA";
    default:                                   return "DLAA";
  }
}

// The helper's set on the proxy's device (the first starts the helper), frame generation's frames in the format its
// present saw (none while it is off) and its backend the helper's choice; again each evaluation (made again when the
// format changes).
inline bool PrepareHelper(ID3D11Device* proxy) {
  return twattila::dlaa_bridge::Prepare(proxy, state.width, state.height, state.guides, state.ray_tracing, twattila::frame_generation::Format(),
                                        state.reflections, state.upscaler, twattila::dlaa_bridge::FG_BACKEND_AUTO);
}

// A perspective view-projection's projection from its rows (acting on column vectors, a unit view: P00 x right, P11 x up,
// A x forward + B, forward): P00, P11, A and B (depth = A + B / view z), the rows' lengths.
inline std::array<float, 4> ProjectionTerms(std::span<const double, 16> view_projection) {
  const auto length = [&](size_t row) {
    const double* values = view_projection.data() + (row * 4);
    return std::sqrt((values[0] * values[0]) + (values[1] * values[1]) + (values[2] * values[2]));
  };
  const double depth_scale = length(2) / std::max(length(3), 1e-30);
  return {static_cast<float>(length(0)), static_cast<float>(length(1)), static_cast<float>(depth_scale),
          static_cast<float>(view_projection[11] - (depth_scale * view_projection[15]))};
}

// This frame's camera from the camera pass's inverse view-projection (c4-c7, rows acting on column vectors): the
// view-projection (its inverse in doubles, Gauss-Jordan) and the eye, where it sends w to 0 (the inverse's third column).
// False: singular.
inline bool CurrentCamera(std::array<double, 16>* view_projection, std::array<double, 3>* eye) {
  const float* inverse = state.camera.data() + 16;
  std::array<std::array<double, 8>, 4> rows = {};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) rows[row][column] = inverse[(row * 4) + column];
    rows[row][4 + row] = 1.0;
  }
  for (int column = 0; column < 4; ++column) {
    int pivot = column;
    for (int row = column + 1; row < 4; ++row) {
      if (std::abs(rows[row][column]) > std::abs(rows[pivot][column])) pivot = row;
    }
    if (std::abs(rows[pivot][column]) < 1e-300) return false;
    std::swap(rows[column], rows[pivot]);
    const double scale = rows[column][column];
    for (double& value : rows[column]) value /= scale;
    for (int row = 0; row < 4; ++row) {
      if (row == column) continue;
      const double factor = rows[row][column];
      for (int other = 0; other < 8; ++other) rows[row][other] -= factor * rows[column][other];
    }
  }
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) (*view_projection)[(row * 4) + column] = rows[row][4 + column];
  }
  const double eye_w = inverse[14];
  if (std::abs(eye_w) < 1e-30) return false;
  *eye = {inverse[2] / eye_w, inverse[6] / eye_w, inverse[10] / eye_w};
  return true;
}

// The D3D9 passes' shaders and the shared textures, once (the ground's ambient occlusion without rays uses them without the
// helper: ray_tracing.hpp).
inline bool MakeObjects(IDirect3DDevice9* host, std::span<const uint8_t> quad, std::span<const uint8_t> prepare,
                        std::span<const uint8_t> writeback) {
  if (state.output.host) return true;
  auto* proxy_reshade = renodx::utils::device_proxy::proxy_device_reshade;
  ComPtr<IDirect3DDevice9Ex> host_ex;
  if (proxy_reshade == nullptr || FAILED(host->QueryInterface(IID_PPV_ARGS(host_ex.GetAddressOf())))) return false;
  auto* proxy = reinterpret_cast<ID3D11Device*>(proxy_reshade->get_native());
  constexpr std::array<D3DVERTEXELEMENT9, 2> LAYOUT = {
      {{0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()}};
  const bool made = SUCCEEDED(host->CreateVertexShader(reinterpret_cast<const DWORD*>(quad.data()), state.quad.ReleaseAndGetAddressOf()))
                    && SUCCEEDED(host->CreatePixelShader(reinterpret_cast<const DWORD*>(prepare.data()), state.prepare.ReleaseAndGetAddressOf()))
                    && SUCCEEDED(host->CreatePixelShader(reinterpret_cast<const DWORD*>(writeback.data()), state.writeback.ReleaseAndGetAddressOf()))
                    && SUCCEEDED(host->CreateVertexDeclaration(LAYOUT.data(), state.quad_layout.ReleaseAndGetAddressOf()))
                    && MakeShared(proxy, host_ex.Get(), D3DFMT_A16B16G16R16F, &state.color)
                    && MakeShared(proxy, host_ex.Get(), D3DFMT_R32F, &state.depth)
                    && MakeShared(proxy, host_ex.Get(), D3DFMT_G16R16F, &state.motion)
                    && MakeShared(proxy, host_ex.Get(), D3DFMT_A16B16G16R16F, &state.output);
  if (!made) state.output = {};
  return made;
}

// Lazily, on the first evaluation: the shaders, the shared textures and the helper's (Prepare starts the helper).
inline bool Ready(IDirect3DDevice9* host, std::span<const uint8_t> quad, std::span<const uint8_t> prepare,
                  std::span<const uint8_t> writeback) {
  if (state.ready || state.failed) return state.ready;
  state.failed = true;
  if (!MakeObjects(host, quad, prepare, writeback)
      || !PrepareHelper(reinterpret_cast<ID3D11Device*>(renodx::utils::device_proxy::proxy_device_reshade->get_native()))) {
    reshade::log::message(reshade::log::level::warning, "[R.U.S.E.] Anti-aliasing: the bridge didn't start (helper missing?): off");
    return false;
  }
  ComPtr<IDirect3DDevice9Ex> host_ex;
  host->QueryInterface(IID_PPV_ARGS(host_ex.GetAddressOf()));
  auto* proxy = reinterpret_cast<ID3D11Device*>(renodx::utils::device_proxy::proxy_device_reshade->get_native());
  if (state.gpu_sync) {
    try {
      state.host_fence = std::make_unique<renodx::utils::gpu_fence::Fence>(host_ex.Get());
      state.proxy_fence = std::make_unique<renodx::utils::gpu_fence::Fence>(proxy);
    } catch (const std::exception& error) {
      reshade::log::message(reshade::log::level::warning,
                            (std::string("[R.U.S.E.] Anti-aliasing: no GPU sync (") + error.what() + "): the CPU waits").c_str());
      state.host_fence.reset();
      state.proxy_fence.reset();
    }
  }
  state.failed = false;
  state.ready = true;
  reshade::log::message(reshade::log::level::info,
                        std::format("[R.U.S.E.] Anti-aliasing: {} ready at {}x{} ({})", UpscalerName(), state.width, state.height,
                                    (state.host_fence != nullptr ? "GPU sync" : "CPU waits"))
                            .c_str());
  return true;
}

// D4, before the bloom draw: the resolved main target (the bloom's s0) through the upscaler and back; any failure leaves
// the game's picture.
inline void Evaluate(reshade::api::command_list* cmd_list, std::span<const uint8_t> quad, std::span<const uint8_t> prepare,
                     std::span<const uint8_t> writeback) {
  state.scene_closed = true;
  if (!state.enabled || state.failed || !state.camera_valid || state.pixel_texture_0 == 0 || state.scene_depth == 0) return;
  auto* host = reinterpret_cast<IDirect3DDevice9*>(cmd_list->get_device()->get_native());
  if (!Ready(host, quad, prepare, writeback)) return;
  auto* scene = reinterpret_cast<IDirect3DTexture9*>(state.pixel_texture_0);
  ComPtr<IDirect3DTexture9> depth;
  ComPtr<IDirect3DSurface9> scene_surface;
  ComPtr<IDirect3DDevice9Ex> host_ex;
  if (FAILED(reinterpret_cast<IDirect3DSurface9*>(state.scene_depth)->GetContainer(IID_PPV_ARGS(depth.GetAddressOf())))
      || FAILED(scene->GetSurfaceLevel(0, scene_surface.GetAddressOf()))
      || FAILED(host->QueryInterface(IID_PPV_ARGS(host_ex.GetAddressOf())))) {
    return;
  }
  auto* proxy = reinterpret_cast<ID3D11Device*>(renodx::utils::device_proxy::proxy_device_reshade->get_native());
  ComPtr<ID3D11DeviceContext> context;
  proxy->GetImmediateContext(context.GetAddressOf());
  // The result coming or going (at most a few lines a start).
  const auto log_change = [](const char* change) {
    if (state.changes_logged > 8) return;
    ++state.changes_logged;
    reshade::log::message(reshade::log::level::info,
                          std::format("[R.U.S.E.] Anti-aliasing: {} {}{}", UpscalerName(), change,
                                      (state.changes_logged > 8 ? " (no more of these lines this start)" : ""))
                              .c_str());
  };
  // The helper gone: no result, so no jitter from the next frame (OnPresent) until one comes again.
  if (!PrepareHelper(proxy)) {
    if (!state.reset) {
      log_change("stopped (the helper is gone): the game's picture until it is back");
    }
    state.reset = true;
    return;
  }
  // W3: the destination's GPU waits for the source's writes (GPU Sync), else the CPU for the source's queue.
  const auto hand_over = [](auto* source, renodx::utils::gpu_fence::Fence* fence, auto* destination) {
    if (fence == nullptr) return false;
    const uint64_t value = renodx::utils::gpu_fence::QueueSignal(source, fence);
    return value != renodx::utils::gpu_fence::NO_SIGNAL && renodx::utils::gpu_fence::QueueWait(destination, fence, value);
  };

  std::array<float, 40> constants = {};
  std::memcpy(constants.data(), state.camera.data(), sizeof(state.camera));
  constants[36] = static_cast<float>(state.width);
  constants[37] = static_cast<float>(state.height);
  constants[38] = state.hdr_param;
  const std::array<IDirect3DSurface9*, 3> inputs = {state.color.host_surface.Get(), state.depth.host_surface.Get(),
                                                    state.motion.host_surface.Get()};
  const std::array<IDirect3DBaseTexture9*, 2> sources = {scene, depth.Get()};
  DrawQuad(host, inputs, state.prepare.Get(), sources, constants);
  if (!hand_over(host_ex.Get(), state.host_fence.get(), context.Get())) WaitHost(host);
  state.depth_frame = state.frame;

  auto& helper = twattila::dlaa_bridge::bridge;
  twattila::dlaa_bridge::BeforeSharedWrites(context.Get());
  context->CopyResource(helper.color.texture.Get(), state.color.proxy.Get());
  context->CopyResource(helper.depth.texture.Get(), state.depth.proxy.Get());
  context->CopyResource(helper.motion.texture.Get(), state.motion.proxy.Get());
  // FSR and XeSS take near, far and the field of view from a row-vector projection: the camera pass's view-projection
  // (rows P00 x view.x, P11 x view.y, P22 x view.z + P23, view.z: a unit view) holds them as its rows' lengths.
  std::array<double, 16> previous_view_projection = {};
  std::ranges::copy(std::span(state.camera).first<16>(), previous_view_projection.begin());
  const auto [p00, p11, z_scale, z_offset] = ProjectionTerms(previous_view_projection);
  std::array<float, 16> view_to_clip = {};
  view_to_clip[0] = p00;
  view_to_clip[5] = p11;
  view_to_clip[10] = z_scale;
  view_to_clip[11] = 1.f;
  view_to_clip[14] = z_offset;
  const bool reset = state.reset;
  const bool evaluated = twattila::dlaa_bridge::Evaluate(context.Get(), state.width, state.height, state.jitter[0], state.jitter[1],
                                                         reset, 0u, nullptr, &view_to_clip);
  if (evaluated == reset) {
    log_change(evaluated ? "applied (its result written back into the scene)" : "gave no result: the game's picture until one comes");
  }
  state.reset = !evaluated;
  if (!evaluated) return;
  // Frame generation's inputs (the helper's row vectors: the registers' transposes, without the jitter): this frame's
  // view-projection and last frame's (c0-c3), the camera's axes from the view-projection's rows (P00 x right, P11 x up,
  // the w row forward), near and far from the projection's depth (a + b / z: 0 at near, 1 at far).
  std::array<double, 16> view_projection = {};
  std::array<double, 3> eye = {};
  if (twattila::frame_generation::Wanted() && CurrentCamera(&view_projection, &eye)) {
    auto& inputs = state.generation_inputs;
    inputs = {.width = state.width, .height = state.height, .jitter_x = state.jitter[0], .jitter_y = state.jitter[1], .reset = reset,
              .view_to_clip = view_to_clip};
    for (size_t row = 0; row < 4; ++row) {
      for (size_t column = 0; column < 4; ++column) {
        inputs.view_projection[(column * 4) + row] = static_cast<float>(view_projection[(row * 4) + column]);
        inputs.previous_view_projection[(column * 4) + row] = state.camera[(row * 4) + column];
      }
    }
    for (size_t axis = 0; axis < 3; ++axis) {
      inputs.camera[axis] = static_cast<float>(eye[axis]);
    }
    for (const auto [row, slot] : {std::pair{0, 3}, std::pair{1, 6}, std::pair{3, 9}}) {
      const double* values = view_projection.data() + (row * 4);
      const double row_length = std::max(std::sqrt((values[0] * values[0]) + (values[1] * values[1]) + (values[2] * values[2])), 1e-30);
      for (int axis = 0; axis < 3; ++axis) {
        inputs.camera[slot + axis] = static_cast<float>(values[axis] / row_length);
      }
    }
    inputs.near_plane = (z_scale != 0.f ? -z_offset / z_scale : 0.f);
    inputs.far_plane = (z_scale != 1.f ? z_scale * inputs.near_plane / (z_scale - 1.f) : 0.f);
    state.generation_frame = state.frame;
  }
  context->CopyResource(state.output.proxy.Get(), helper.output.texture.Get());
  if (!hand_over(context.Get(), state.proxy_fence.get(), host_ex.Get())) WaitProxy(proxy, context.Get());

  const std::array<IDirect3DSurface9*, 1> target = {scene_surface.Get()};
  const std::array<IDirect3DBaseTexture9*, 1> result = {state.output.host.Get()};
  DrawQuad(host, target, state.writeback.Get(), result, constants);
}

// Readable Scene Depth's INTZ depths by the handle the binds give: ReShade's init_resource names an INTZ depth by its
// texture (the surface's replacement), bind_render_targets_and_depth_stencil by its level-0 surface, as its view
// (d3d9_device.cpp: CreateDepthStencilSurface, the auto depth-stencil, SetDepthStencilSurface).
inline void OnInitDepthView(reshade::api::device* device, reshade::api::resource, reshade::api::resource_usage usage_type,
                            const reshade::api::resource_view_desc& desc, reshade::api::resource_view view) {
  if (usage_type != reshade::api::resource_usage::depth_stencil || desc.format != reshade::api::format::intz) return;
  state.intz_depths.insert(view.handle);
  state.host_device = device;
}

inline void OnDestroyDepthView(reshade::api::device*, reshade::api::resource_view view) {
  state.intz_depths.erase(view.handle);
  if (view.handle == state.post_depth) {
    state.post_depth = 0;
  }
}

// The main pass's and the ground pre-pass's draws: into a back-buffer-sized target with an INTZ depth, before the bloom;
// none with the game's MSAA on (the pre-pass alone would be jittered and read).
inline bool IsSceneDraw() {
  return state.game_samples == 0 && state.render_target_is_frame_sized && !state.scene_closed && state.depth_stencil != state.post_depth
         && state.intz_depths.contains(state.depth_stencil);
}

// D3: the scene draws' camera (c0-c3, row-major MVP: clip_i = row_i . position) moved by the jitter: rows 0 and 1 +=
// offset x row 3 (clip.xy += offset x clip.w), the game's values back on every other draw. None after a reset (no
// result yet, or the helper lost): the game's picture is shown then.
inline void OnDraw(reshade::api::command_list* cmd_list) {
  if (!state.enabled || state.failed) return;
  const bool scene_draw = IsSceneDraw();
  if (scene_draw) {
    state.scene_depth = state.depth_stencil;
  }
  auto* host = reinterpret_cast<IDirect3DDevice9*>(cmd_list->get_device()->get_native());
  if (scene_draw && state.ready && !state.reset) {
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

// The bridge's objects on both devices (its passes, the shared textures at its size, the fences) with either device, and
// at the game's swap chain's end (a D3D9 Reset ends it, not the device: the size may change); Ready makes them again.
inline void Release() {
  state.host_fence.reset();
  state.proxy_fence.reset();
  state.color = {};
  state.depth = {};
  state.motion = {};
  state.output = {};
  state.post_surface.Reset();
  state.hudless = {};
  state.hudless_failed = false;
  state.generation_ready = false;
  state.quad.Reset();
  state.prepare.Reset();
  state.writeback.Reset();
  state.quad_layout.Reset();
  state.ready = false;
  state.failed = false;
  state.reset = true;
  state.camera_valid = false;
  state.pixel_texture_0 = 0;
  state.scene_depth = 0;
  state.depth_frame = UINT32_MAX;
}

// The game's D3D9 device (its INTZ depths with it) or the proxy's D3D11 device going (no other device: overlays and
// helpers make their own): the bridge's objects go too, the helper's set with the proxy's. host_device stays for the
// replay's and the rays' handlers after this one (the next device's first INTZ depth replaces it).
inline void OnDestroyDevice(reshade::api::device* device) {
  const bool proxy = renodx::utils::device_proxy::IsProxyDevice(device);
  if (!proxy && device != state.host_device) return;
  Release();
  if (proxy) {
    twattila::dlaa_bridge::Release();
  } else {
    state.intz_depths.clear();
    state.game_samples = 0;
  }
}

// The final post's draw: its depth (unless the scene's own); with frame generation its target, the back buffer's clone
// the swap chain mod binds in the game's back buffer's place, as the device holds it.
inline void OnFinalPost(reshade::api::command_list* cmd_list) {
  if (state.depth_stencil != state.scene_depth) {
    state.post_depth = state.depth_stencil;
  }
  if (!state.ready || state.hudless_failed || !twattila::frame_generation::Wanted()) return;
  state.post_surface.Reset();
  reinterpret_cast<IDirect3DDevice9*>(cmd_list->get_device()->get_native())->GetRenderTarget(0, state.post_surface.GetAddressOf());
}

// The first draw after the final post (the UI's), or the present: the post's picture into the shared HUD-less texture
// (made with the first copy, the clone's FP16 format: the proxy's swap chain shader reads it as it reads the clone).
inline void KeepHudless(IDirect3DDevice9* host) {
  const ComPtr<IDirect3DSurface9> source = std::move(state.post_surface);
  if (!source) return;
  if (!state.hudless.host) {
    D3DSURFACE_DESC desc = {};
    ComPtr<IDirect3DDevice9Ex> host_ex;
    auto* proxy = reinterpret_cast<ID3D11Device*>(renodx::utils::device_proxy::proxy_device_reshade->get_native());
    if (FAILED(source->GetDesc(&desc)) || desc.Format != D3DFMT_A16B16G16R16F || desc.Width != state.width || desc.Height != state.height
        || FAILED(host->QueryInterface(IID_PPV_ARGS(host_ex.GetAddressOf())))
        || !MakeShared(proxy, host_ex.Get(), D3DFMT_A16B16G16R16F, &state.hudless)) {
      state.hudless = {};
      state.hudless_failed = true;
      reshade::log::message(reshade::log::level::warning,
                            std::format("[R.U.S.E.] Frame generation: no HUD-less image (the back buffer's copy is {}x{}, format {})",
                                        desc.Width, desc.Height, static_cast<uint32_t>(desc.Format))
                                .c_str());
      return;
    }
    reshade::log::message(reshade::log::level::info,
                          std::format("[R.U.S.E.] Frame generation: the HUD-less image ready ({}x{})", desc.Width, desc.Height).c_str());
  }
  if (SUCCEEDED(host->StretchRect(source.Get(), nullptr, state.hudless.host_surface.Get(), nullptr, D3DTEXF_NONE))) {
    state.hudless_frame = state.frame;
  }
}

// The proxy's present (its frame is the game's last; its D3D9 work done: the proxy's copy waited for it): the HUD-less
// image and the inputs to frame generation when both are that frame's.
inline void KeepFrame(ID3D11DeviceContext* context) {
  if (!std::exchange(state.generation_ready, false)) return;
  twattila::frame_generation::KeepFrame(context, state.hudless.proxy.Get(), state.generation_inputs);
}

// The game's present, before the proxy's: a HUD-less image still waiting is the whole frame (no UI drawn).
inline void OnPresent(IDirect3DDevice9* host) {
  KeepHudless(host);
  state.generation_ready = state.hudless_frame == state.frame && state.generation_frame == state.frame;
  state.scene_closed = false;
  state.scene_depth = 0;  // this frame's: the next Evaluate takes the next frame's scene draws' (none in a menu)
  ++state.frame;
  // After a reset the next frame draws unjittered (OnDraw): Evaluate, the rays and the motion take 0 then.
  state.jitter = (state.reset ? std::array<float, 2>{}
                              : std::array<float, 2>{Halton((state.frame % 8) + 1, 2) - 0.5f, Halton((state.frame % 8) + 1, 3) - 0.5f});
}

}  // namespace ruse::bridge
