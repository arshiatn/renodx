/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <include/reshade.hpp>

#include "../../utils/directx.hpp"
#include "../twattila/dxbc.hpp"
#include "../../utils/resource.hpp"
#include "../../utils/state.hpp"

// Debug (Advanced button, from the Shogun 2 DLSS addon): every draw, dispatch, copy and render target clear of one
// frame with its shaders, targets (all 8 and depth), viewport, scissor, inputs (every texture slot, the slots each shader
// declares, the vertex buffers) and Unreal's view size, into <game>_frame_trace.txt next to the exe (OnPresent's name).
// For DLSS upscaling: at a render scale below 100%, which passes after the TAA
// run at the render size and which at the display size, where the game's spatial upscale is, and which formats they
// use. Its events are registered only for the traced frame.
namespace talesofarise::frame_trace {

// A game's, OnPresent's.
struct Options {
  // The View uniform buffer's ViewSizeAndInvSize row, BufferSizeAndInvSize the next: UE 4.21's c122 (the TAA's b1,
  // README); 4.23's c126 (ClipToWorld came before it).
  uint32_t view_size_row = 122;
};

namespace internal {

using Microsoft::WRL::ComPtr;

constexpr uint32_t MAX_LINES = 12000;

struct State {
  std::mutex mutex;
  std::atomic_bool requested = false;
  std::atomic_bool tracing = false;
  std::string text;
  uint32_t lines = 0;
  std::string last_view;  // the last view sizes written
  // Each traced shader's declared texture slots (" decl t0 t3"), its code saved once when first seen.
  std::unordered_map<uint32_t, std::string> declared;
  std::string game = "tales";  // the files' names: <game>_frame_trace.txt, <game>_frame_trace_shaders
  // Set before a trace starts and only read while it runs (D3D11's immediate context: no draw while the present runs).
  Options options;
  HANDLE folder_changes = INVALID_HANDLE_VALUE;  // the exe folder's file names (scripted runs: renodx_trace_now)
  std::string status = "No frame trace yet.";
};
static State state;

inline std::filesystem::path ExecutableFolder() {
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  return std::filesystem::path(exe).parent_path();
}

inline std::string DescribeResource(ID3D11Resource* resource) {
  if (resource == nullptr) return "-";
  if (ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) {
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    return std::format("{} {}x{}{}{} f{}", static_cast<const void*>(resource), desc.Width, desc.Height,
                       (desc.ArraySize > 1 ? std::format(" [{}]", desc.ArraySize) : std::string()),
                       (desc.MipLevels > 1 ? std::format(" {} mips", desc.MipLevels) : std::string()),
                       static_cast<int>(desc.Format));
  }
  if (ComPtr<ID3D11Texture3D> volume; SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(volume.GetAddressOf())))) {
    D3D11_TEXTURE3D_DESC desc = {};
    volume->GetDesc(&desc);
    return std::format("{} {}x{}x{} f{}", static_cast<const void*>(resource), desc.Width, desc.Height, desc.Depth,
                       static_cast<int>(desc.Format));
  }
  return std::format("{} (buffer)", static_cast<const void*>(resource));
}

// A RenoDX clone view (an upgraded resource) also names the game's own resource behind it, the handle upgrade logs print.
template <typename View>
std::string DescribeView(View* view) {
  if (view == nullptr) return "-";
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  std::string description = DescribeResource(resource.Get());
  reshade::api::resource original = {0u};
  if (renodx::utils::resource::shared.data != nullptr) {
    renodx::utils::resource::GetResourceViewInfo(
        reshade::api::resource_view{reinterpret_cast<uint64_t>(view)},
        [&original](const renodx::utils::resource::ResourceViewInfo& info) {
          if (info.is_clone && !info.destroyed) original = info.original_resource;
        });
  }
  if (original.handle != 0u) description += std::format(" clone of {}", reinterpret_cast<const void*>(original.handle));
  return description;
}

inline ID3D11DeviceContext* NativeContext(reshade::api::command_list* cmd_list) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  return context;
}

inline void Append(std::string line) {
  const std::lock_guard lock(state.mutex);
  if (state.lines++ >= MAX_LINES) return;
  state.text += line;
  state.text += '\n';
}

// Unreal's view and buffer size from the first bound constant buffer that holds a View uniform buffer (read back
// through a staging copy; a buffer counts when Options' view size row and the next are sizes with their inverses), else
// empty.
inline std::string DescribeUnrealView(ID3D11DeviceContext* context, const std::array<ID3D11Buffer*, 14>& buffers) {
  const uint32_t view_row = state.options.view_size_row;
  ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  for (ID3D11Buffer* buffer : buffers) {
    if (buffer == nullptr) continue;
    D3D11_BUFFER_DESC desc = {};
    buffer->GetDesc(&desc);
    if (desc.ByteWidth < (view_row + 2) * 16) continue;
    const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_STAGING,
                                            .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Buffer> staging;
    if (FAILED(device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf()))) continue;
    context->CopyResource(staging.Get(), buffer);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) continue;
    const auto* view = static_cast<const float*>(mapped.pData) + (view_row * 4);
    const auto* size = view + 4;
    const auto is_size = [](const float* c) {
      return c[0] >= 1.f && c[1] >= 1.f && c[0] <= 16384.f && c[1] <= 16384.f && std::abs(c[0] * c[2] - 1.f) < 1e-3f
             && std::abs(c[1] * c[3] - 1.f) < 1e-3f;
    };
    std::string text;
    if (is_size(view) && is_size(size)) {
      text = std::format(" | view {}x{} buffer {}x{}", view[0], view[1], size[0], size[1]);
    }
    context->Unmap(staging.Get(), 0);
    if (!text.empty()) return text;
  }
  return "";
}

// A traced shader's declared texture slots (dcl_resource, raw and structured; their register), as " decl t0 t3"; its
// bytecode saved once, as <game>_frame_trace_shaders\0x<hash>.cso next to the exe (when an addon keeps the game's shader
// code, as the DevKit does). Empty: no code kept, or none declared.
inline std::string Declared(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stage, uint32_t hash) {
  if (hash == 0u) return {};
  std::string game;
  {
    const std::lock_guard lock(state.mutex);
    if (const auto known = state.declared.find(hash); known != state.declared.end()) return known->second;
    game = state.game;
  }
  const reshade::api::shader_desc* desc = nullptr;
  if (const auto* info = renodx::utils::state::GetBoundPipelineInfo(cmd_list, stage); info != nullptr) {
    for (const auto& entry : info->shader_details) {
      if (entry.shader_hash != hash || entry.subobject_index >= info->details.subobjects.size()) continue;
      desc = static_cast<const reshade::api::shader_desc*>(info->details.subobjects[entry.subobject_index].data);
      break;
    }
  }
  std::string declared;
  if (desc != nullptr && desc->code_size != 0) {
    const auto folder = ExecutableFolder() / std::format("{}_frame_trace_shaders", game);
    std::filesystem::create_directories(folder);
    std::ofstream(folder / std::format("0x{:08X}.cso", hash), std::ios::binary | std::ios::trunc)
        .write(static_cast<const char*>(desc->code), static_cast<std::streamsize>(desc->code_size));
    const auto program = twattila::dxbc::Read({static_cast<const uint8_t*>(desc->code), desc->code_size});
    size_t temps_at = 0;
    const size_t declarations_end = program.tokens.empty() ? 0 : twattila::dxbc::internal::FirstInstruction(program.tokens, &temps_at);
    for (size_t at = 2; at < declarations_end;) {
      const size_t length = twattila::dxbc::Length(program.tokens, at);
      if (length == 0) break;
      // dcl_resource (88), dcl_resource_raw (161), dcl_resource_structured (162): the register after the operand token.
      if (const uint32_t opcode = program.tokens[at] & 0x7FF; opcode == 88 || opcode == 161 || opcode == 162) {
        if (const size_t operand = twattila::dxbc::internal::FirstOperand(program.tokens, at); operand + 1 < at + length) {
          declared += std::format(" t{}", program.tokens[operand + 1]);
        }
      }
      at += length;
    }
    if (!declared.empty()) declared = " decl" + declared;
  }
  const std::lock_guard lock(state.mutex);
  state.declared.emplace(hash, declared);
  return declared;
}

// Every texture slot bound (t0-t127).
template <typename GetViews>
std::string DescribeInputs(GetViews get_views) {
  std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> views = {};
  get_views(views.data());
  std::string inputs;
  for (uint32_t slot = 0; slot < views.size(); ++slot) {
    if (views[slot] == nullptr) continue;
    inputs += std::format(" t{} {}", slot, DescribeView(views[slot]));
    views[slot]->Release();
  }
  return inputs;
}

inline void TraceDraw(reshade::api::command_list* cmd_list, std::string_view kind, uint32_t count) {
  if (!state.tracing.load(std::memory_order_acquire)) return;
  ID3D11DeviceContext* context = NativeContext(cmd_list);
  if (context == nullptr) return;
  std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets = {};
  ID3D11DepthStencilView* depth = nullptr;
  context->OMGetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), &depth);
  std::string outputs;
  for (uint32_t slot = 0; slot < targets.size(); ++slot) {
    if (targets[slot] == nullptr) continue;
    outputs += std::format(" rt{} {}", slot, DescribeView(targets[slot]));
    targets[slot]->Release();
  }
  if (depth != nullptr) {
    outputs += std::format(" ds {}", DescribeView(depth));
    depth->Release();
  }
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  context->RSGetViewports(&viewport_count, &viewport);
  D3D11_RECT scissor = {};
  UINT scissor_count = 1;
  context->RSGetScissorRects(&scissor_count, &scissor);
  const std::string inputs = DescribeInputs([&](ID3D11ShaderResourceView** views) {
    context->PSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, views);
  });
  std::array<ID3D11Buffer*, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertex_buffers = {};
  std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> strides = {};
  std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> offsets = {};
  context->IAGetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertex_buffers.data(), strides.data(), offsets.data());
  std::string vertices;
  for (uint32_t slot = 0; slot < vertex_buffers.size(); ++slot) {
    if (vertex_buffers[slot] == nullptr) continue;
    D3D11_BUFFER_DESC desc = {};
    vertex_buffers[slot]->GetDesc(&desc);
    vertices += std::format(" vb{} {} bytes stride {} offset {}{}", slot, desc.ByteWidth, strides[slot], offsets[slot],
                            (desc.Usage == D3D11_USAGE_DYNAMIC ? " dynamic" : ""));
    vertex_buffers[slot]->Release();
  }
  // The view sizes once per change (they stay for most of a pass).
  std::array<ID3D11Buffer*, 14> constants = {};
  context->PSGetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.data());
  std::string view = DescribeUnrealView(context, constants);
  if (view.empty()) {
    for (auto*& buffer : constants) {
      if (buffer != nullptr) buffer->Release();
      buffer = nullptr;
    }
    context->VSGetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.data());
    view = DescribeUnrealView(context, constants);
  }
  for (auto* buffer : constants) {
    if (buffer != nullptr) buffer->Release();
  }
  {
    const std::lock_guard lock(state.mutex);
    if (view == state.last_view) {
      view.clear();
    } else {
      state.last_view = view;
    }
  }
  const uint32_t vertex_hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::vertex_shader);
  const uint32_t pixel_hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader);
  const std::string vertex_declared = Declared(cmd_list, reshade::api::pipeline_stage::vertex_shader, vertex_hash);
  const std::string pixel_declared = Declared(cmd_list, reshade::api::pipeline_stage::pixel_shader, pixel_hash);
  Append(std::format("{} {} vs {:08X}{} ps {:08X}{} |{} | vp {}x{} at {},{}{} |{} |{}{}", kind, count, vertex_hash, vertex_declared, pixel_hash,
                     pixel_declared, outputs, viewport.Width, viewport.Height, viewport.TopLeftX, viewport.TopLeftY,
                     (scissor_count != 0 ? std::format(" scissor {},{}-{},{}", scissor.left, scissor.top, scissor.right, scissor.bottom)
                                         : std::string()),
                     inputs, vertices, view));
}

inline bool OnDraw(reshade::api::command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t,
                   uint32_t) {
  TraceDraw(cmd_list, "draw", vertex_count * instance_count);
  return false;
}

inline bool OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t instance_count,
                          uint32_t, int32_t, uint32_t) {
  TraceDraw(cmd_list, "indexed", index_count * instance_count);
  return false;
}

inline void TraceDispatch(reshade::api::command_list* cmd_list, const std::string& kind) {
  if (!state.tracing.load(std::memory_order_acquire)) return;
  ID3D11DeviceContext* context = NativeContext(cmd_list);
  if (context == nullptr) return;
  std::array<ID3D11UnorderedAccessView*, 8> uavs = {};
  context->CSGetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data());
  std::string outputs;
  for (uint32_t slot = 0; slot < uavs.size(); ++slot) {
    if (uavs[slot] == nullptr) continue;
    outputs += std::format(" u{} {}", slot, DescribeView(uavs[slot]));
    uavs[slot]->Release();
  }
  const std::string inputs = DescribeInputs([&](ID3D11ShaderResourceView** views) {
    context->CSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, views);
  });
  std::array<ID3D11Buffer*, 14> constants = {};
  context->CSGetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.data());
  const std::string view = DescribeUnrealView(context, constants);
  for (auto* buffer : constants) {
    if (buffer != nullptr) buffer->Release();
  }
  const uint32_t compute_hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::compute_shader);
  const std::string declared = Declared(cmd_list, reshade::api::pipeline_stage::compute_shader, compute_hash);
  Append(std::format("{} cs {:08X}{} |{} |{}{}", kind, compute_hash, declared, outputs, inputs, view));
}

inline bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t x, uint32_t y, uint32_t z) {
  TraceDispatch(cmd_list, std::format("dispatch {}x{}x{}", x, y, z));
  return false;
}

inline bool OnIndirect(reshade::api::command_list* cmd_list, reshade::api::indirect_command type, reshade::api::resource,
                       uint64_t, uint32_t, uint32_t) {
  if (type == reshade::api::indirect_command::dispatch) {
    TraceDispatch(cmd_list, "dispatch indirect");
  } else {
    TraceDraw(cmd_list, "draw indirect", 0);
  }
  return false;
}

inline bool OnClearRenderTargetView(reshade::api::command_list*, reshade::api::resource_view view, const float color[4],
                                    uint32_t, const reshade::api::rect*) {
  if (!state.tracing.load(std::memory_order_relaxed)) return false;
  Append(std::format("clear {} to ({}, {}, {}, {})", DescribeView(reinterpret_cast<ID3D11RenderTargetView*>(view.handle)),
                     color[0], color[1], color[2], color[3]));
  return false;
}

inline bool OnCopyResource(reshade::api::command_list*, reshade::api::resource source, reshade::api::resource dest) {
  if (!state.tracing.load(std::memory_order_relaxed)) return false;
  Append(std::format("copy {} -> {}", DescribeResource(reinterpret_cast<ID3D11Resource*>(source.handle)),
                     DescribeResource(reinterpret_cast<ID3D11Resource*>(dest.handle))));
  return false;
}

inline bool OnCopyTextureRegion(reshade::api::command_list*, reshade::api::resource source, uint32_t,
                                const reshade::api::subresource_box*, reshade::api::resource dest, uint32_t,
                                const reshade::api::subresource_box*, reshade::api::filter_mode) {
  if (!state.tracing.load(std::memory_order_relaxed)) return false;
  Append(std::format("copy region {} -> {}", DescribeResource(reinterpret_cast<ID3D11Resource*>(source.handle)),
                     DescribeResource(reinterpret_cast<ID3D11Resource*>(dest.handle))));
  return false;
}

// The traced frame's events, registered only for it (otherwise no cost per draw).
inline void UseEvents(bool use) {
  if (use) {
    reshade::register_event<reshade::addon_event::draw>(OnDraw);
    reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnIndirect);
    reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
    reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::register_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
  } else {
    reshade::unregister_event<reshade::addon_event::draw>(OnDraw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::unregister_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnIndirect);
    reshade::unregister_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
    reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
  }
}

}  // namespace internal

inline void Request() {
  internal::state.requested.store(true);
}

// A game's own line in the traced frame (a pass's constants, read only then).
inline bool Tracing() {
  return internal::state.tracing.load(std::memory_order_relaxed);
}

inline void Note(std::string line) {
  internal::Append(std::move(line));
}

inline std::string Status() {
  const std::lock_guard lock(internal::state.mutex);
  return internal::state.status;
}

// Present: ends a traced frame (writes <game>_frame_trace.txt), starts a requested one. In a scripted run
// (twattila/test_hooks.hpp's RENODX_TW_SET, _SHOT or _PROFILE set, as every data run's renodx_test_hooks.txt sets them
// before the first present) renodx_trace_now next to the exe (the run's script, or RENODX_TW_SET's "trace" key) asks for
// the next frame, removed: looked for only when Windows says a file name in the exe's folder changed (a change
// notification, tested without a wait). Otherwise nothing is looked for: the buttons call Request.
inline void OnPresent(std::string_view game, const Options& options = {}) {
  auto& state = internal::state;
  static const bool scripted = GetEnvironmentVariableA("RENODX_TW_SET", nullptr, 0) != 0 || GetEnvironmentVariableA("RENODX_TW_SHOT", nullptr, 0) != 0
                               || GetEnvironmentVariableA("RENODX_TW_PROFILE", nullptr, 0) != 0;
  if (static bool watched = false; scripted && !std::exchange(watched, true)) {
    state.folder_changes = FindFirstChangeNotificationW(internal::ExecutableFolder().c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
  }
  if (state.folder_changes != INVALID_HANDLE_VALUE && WaitForSingleObject(state.folder_changes, 0) == WAIT_OBJECT_0) {
    FindNextChangeNotification(state.folder_changes);
    if (DeleteFileW((internal::ExecutableFolder() / L"renodx_trace_now").c_str()) != 0) Request();
  }
  if (state.tracing.exchange(false)) {
    internal::UseEvents(false);
    const std::lock_guard lock(state.mutex);
    const auto path = internal::ExecutableFolder() / std::format("{}_frame_trace.txt", state.game);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << state.text;
    state.status = std::format("Frame trace: {} lines saved to {}", state.lines, path.string());
    state.text.clear();
  }
  if (state.requested.exchange(false)) {
    {
      const std::lock_guard lock(state.mutex);
      state.text =
          "One frame: draws, dispatches, clears and copies in order (vs / ps / cs hashes, targets, viewport, scissor,\n"
          "inputs; Unreal's view and buffer size when they change). Formats: DXGI numbers (10 RGBA16F, 24 RGB10A2,\n"
          "26 R11G11B10F, 28 RGBA8, 29 RGBA8 sRGB, 87 BGRA8).\n";
      state.lines = 0;
      state.last_view.clear();
      state.declared.clear();
      state.game = game;
      state.options = options;
    }
    internal::UseEvents(true);
    state.tracing.store(true);
  }
}

// Unload: a frame being traced is dropped, the folder's notification closed.
inline void Stop() {
  if (internal::state.tracing.exchange(false)) {
    internal::UseEvents(false);
  }
  if (internal::state.folder_changes != INVALID_HANDLE_VALUE) FindCloseChangeNotification(std::exchange(internal::state.folder_changes, INVALID_HANDLE_VALUE));
}

}  // namespace talesofarise::frame_trace
