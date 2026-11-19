/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/data.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/state.hpp"
#include "./context_hooks.hpp"

// Debug (ReShade.ini [renodx] FrameTrace=1, read at start): one frame, 600 presents after the DLSS render scale began
// (Options' at_100: after the 3D view began, also at 100%), or the next frame when renodx_trace_now sits next to the exe
// (Options' on_demand), every draw, dispatch (direct and indirect), clear and copy with its shaders, targets, viewport,
// inputs and the camera's screen / viewport sizes (the camera at b0, its registers from Install) into tw_frame_trace.txt
// next to the exe, with the anti-aliasing's marks (tone mapper, DLSS). For the render scale audit
// (tools\tw\scale_audit.py): which passes read the 3D image's part of a display-size texture as if it filled it. Its
// events are registered only for the traced frame (with FrameTraceReads=1 also the frame after it, and the input layouts'
// from the start).
// Debug (env RENODX_TW_MAP_TRACE=<seconds after the first present>[,<frames>[,hash]], default 3 frames, read at
// Install): the map trace, what the game's Map calls wait for. Each traced frame's maps, unmaps, buffer updates and copies
// into staging resources (the resource, the access, the time since the previous traced event on that thread ended, the
// first return addresses into the game's code) and its draws and dispatches (the time and shaders only: no Gets, no GPU
// reads), kept as structs and written to tw_map_trace.txt next to the exe after the last traced present
// (tools\tw\map_trace.py). Armed, its callbacks are registered at Install (gated off outside the traced frames); unarmed,
// none. ReShade reports only the Maps that succeed: a failed DO_NOT_WAIT poll is not seen (its time is in the gap of the
// Map that succeeds), and the Map flags are not recorded. A map's unmap is the next unmap of its resource and subresource
// on any thread or context. A gap leaves the trace's own time out but holds the native call of the event before it when
// that callback runs first (a draw's, an unmap's, an update's, a copy's: gap_from names it).
// Version 3: while the traced frames run, the context hooks (context_hooks.hpp: the game's immediate context's d3d11.dll
// bodies, below ReShade, which calls them from its proxy; they survive the add-on's SwapDeviceContextState passes, which
// v2's table-slot hook did not) time every Map and Unmap on that context around the native body: the game's through
// ReShade's proxy, the add-ons' through ReShade's API or their own, the failed DO_NOT_WAIT polls too, with the Map type
// and flags, the result, the resource's class (constant / vertex / index / other buffer, texture) and the bytes it maps,
// the native call's direct caller (module file name + offset: ReShade's proxy and ReShade's API both sit in ReShade's
// module; only a v1 map right after a native one marks it the game's, tools\tw\map_trace.py), and a map's game return
// addresses; per frame the totals (DISCARD bytes, counts and native time per class), as v2's lines in tw_map_trace.txt.
// v3 also records the context's constant and vertex buffer binds (Set, Set1, IASetVertexBuffers, the bindings held at
// the start), its swaps and ClearStates, each draw's arguments, and the constant buffers the game makes while armed with
// their threads: one stream in call order, each draw with the bindings that changed since the one before, in
// tw_map_trace_v3.txt (its lines at its writer, OnPresent). With `hash`, also a written constant buffer's content hash at
// its unmap (it reads write-combined memory on the render thread: timed, the header's hash_ms; off for timing runs). The
// context comes from the present callback while armed (a start without one waits one present); the hooks go in at the
// trace's first frame (only on the render thread, with one single-threaded D3D11 device: Attila, Rome II, Thrones,
// Shogun 2; logged otherwise) and out after its last, at the device's end and at Uninstall. Measure only: every call goes
// on to the native body unchanged (a swap settles d3d11's table inside its hook, context_hooks.hpp). A v1 map's gap on
// that context then holds the hook's own time too (its callers' scan, an unmap's hash). Each file is written to
// <name>.part and renamed when whole.
namespace twattila::frame_trace {

// `count` values of a buffer from `offset` bytes on, read back (it waits for the GPU: for traced frames); empty: past the
// buffer's end or no read here (a deferred context).
template <typename T>
std::vector<T> ReadBuffer(ID3D11DeviceContext* context, ID3D11Buffer* buffer, UINT offset, UINT count) {
  const UINT bytes = count * static_cast<UINT>(sizeof(T));
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  if (offset + bytes > desc.ByteWidth) return {};
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_STAGING,
                                          .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
  if (FAILED(device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf()))) return {};
  const D3D11_BOX box = {.left = offset, .top = 0, .front = 0, .right = offset + bytes, .bottom = 1, .back = 1};
  context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, buffer, 0, &box);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return {};
  const auto* values = static_cast<const T*>(mapped.pData);
  std::vector<T> result(values, values + count);
  context->Unmap(staging.Get(), 0);
  return result;
}

// An input layout's element (its offset resolved where D3D11's was appended).
struct LayoutElement {
  std::string semantic;
  uint32_t index = 0;
  reshade::api::format format = reshade::api::format::unknown;
  uint32_t slot = 0;
  uint32_t offset = 0;
  bool per_instance = false;
};

// A traced draw for Options' draw_reads: its arguments as the game gave them (an indirect draw's from its buffer).
struct Draw {
  ID3D11DeviceContext* context = nullptr;
  uint32_t vertex_shader = 0;
  uint32_t pixel_shader = 0;
  bool indexed = false;
  uint32_t count = 0;  // an instance's indices (or vertices)
  uint32_t instances = 0;
  uint32_t first = 0;  // the first index (or vertex)
  int32_t base_vertex = 0;
  uint32_t first_instance = 0;
  std::vector<LayoutElement> layout;  // empty: not known
  bool next_frame = false;            // the frame after the traced one
};

// Install's.
struct Options {
  // The camera's g_screen_size and g_viewport_dimensions registers (Attila's camera_VS_PS).
  uint32_t camera_screen_register = 27;
  uint32_t camera_viewport_register = 29;
  std::vector<uint32_t> value_hashes;  // a draw of these shaders, once each in a trace, its constants too:
  uint32_t value_slots = 4;            // VS and PS b0 to b(value_slots - 1),
  uint32_t value_floats = 64;          // the first floats of each
  bool on_demand = false;             // renodx_trace_now next to the exe (looked for each 60 presents, removed): the next frame
  bool at_100 = false;                 // FrameTrace's 600 presents count the 3D view at any scale (OnPresent's view_3d)
  // A draw of these pixel shaders, once each in a trace: its blend state in full (each target's blend and write mask, the
  // blend factor and sample mask; D3D11_BLEND and D3D11_BLEND_OP values).
  std::vector<uint32_t> blend_hashes;
  // Each draw of these vertex shaders: its VS shader resources (a buffer's size, stride and flags, its view's format, first
  // element and count).
  std::vector<uint32_t> vertex_input_hashes;
  bool instances_apart = false;  // an instanced draw's count an instance and its instances (else only their product)
  // With [renodx] FrameTraceReads=1 (read at start; else none of this runs or hooks): a game's own reads at each traced
  // draw (its buffers' layouts, ReadBuffer), their text after the draw's, and at each draw of the frame after it (lines of
  // their own: values the game writes each frame).
  std::function<std::string(const Draw&)> draw_reads;
};

namespace internal {

using Microsoft::WRL::ComPtr;

constexpr uint32_t MAX_LINES = 20000;
constexpr uint32_t SCALED_PRESENTS = 600;

struct State {
  std::mutex mutex;
  bool wanted = false;  // a trace to come this run (the ini's, or the marker's)
  uint32_t scaled_presents = 0;
  std::atomic_bool tracing = false;
  std::string text;
  uint32_t lines = 0;
  Options options;
  std::filesystem::path folder;  // the exe's: the trace and the marker
  uint32_t presents = 0;         // on demand: the marker looked for at every 60th
  std::vector<uint32_t> values_written;
  std::vector<uint32_t> blends_written;
  bool indirect_values_written = false;
  bool reads = false;                   // FrameTraceReads with Options' draw_reads (set at Install only)
  std::atomic_bool next_frame = false;  // the frame after the traced one, for the draw reads
};
static State state;
static renodx::utils::data::ParallelFlatHashMap<uint64_t, std::vector<LayoutElement>, std::shared_mutex> layouts;

// The game's input layouts (with FrameTraceReads, from the start: it makes them while loading).
inline void OnInitPipeline(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t subobject_count,
                           const reshade::api::pipeline_subobject* subobjects, reshade::api::pipeline pipeline) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  for (const auto& subobject : std::span(subobjects, subobject_count)) {
    if (subobject.type != reshade::api::pipeline_subobject_type::input_layout) continue;
    std::vector<LayoutElement> elements;
    std::array<uint32_t, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> appended = {};
    for (const auto& element : std::span(static_cast<const reshade::api::input_element*>(subobject.data), subobject.count)) {
      if (element.buffer_binding >= appended.size()) continue;
      const uint32_t offset = (element.offset == D3D11_APPEND_ALIGNED_ELEMENT ? appended[element.buffer_binding] : element.offset);
      appended[element.buffer_binding] = offset + reshade::api::format_row_pitch(element.format, 1);
      elements.push_back({.semantic = (element.semantic != nullptr ? element.semantic : ""),
                          .index = element.semantic_index,
                          .format = element.format,
                          .slot = element.buffer_binding,
                          .offset = offset,
                          .per_instance = element.instance_step_rate != 0});
    }
    layouts.insert_or_assign(pipeline.handle, std::move(elements));
  }
}

inline void OnDestroyPipeline(reshade::api::device*, reshade::api::pipeline pipeline) {
  layouts.erase(pipeline.handle);
}

inline std::string DescribeResource(ID3D11Resource* resource) {
  if (resource == nullptr) return "-";
  if (ComPtr<ID3D11Texture2D> texture; SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) {
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    return std::format("{} {}x{}{}{} f{}", static_cast<const void*>(resource), desc.Width, desc.Height,
                       (desc.ArraySize > 1 ? std::format(" [{}]", desc.ArraySize) : std::string()),
                       (desc.SampleDesc.Count > 1 ? std::format(" ms{}", desc.SampleDesc.Count) : std::string()),
                       static_cast<int>(desc.Format));
  }
  return std::format("{} buffer", static_cast<const void*>(resource));
}

template <typename View>
std::string DescribeView(View* view) {
  if (view == nullptr) return "-";
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  return DescribeResource(resource.Get());
}

inline ID3D11DeviceContext* NativeContext(reshade::api::command_list* cmd_list) {
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  renodx::utils::directx::TryNativeFromReShadeProxy(&context);
  return context;
}

inline void Append(const std::string& line) {
  const std::lock_guard lock(state.mutex);
  if (state.lines++ >= MAX_LINES) return;
  state.text += line;
  state.text += '\n';
}

// The camera at b0, read back; empty if b0 is not camera-sized.
inline std::string DescribeCamera(ID3D11DeviceContext* context, ID3D11Buffer* buffer) {
  if (buffer == nullptr) return "";
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  if (desc.ByteWidth > 4096) return "";
  const UINT screen = state.options.camera_screen_register * 4;
  const UINT viewport = state.options.camera_viewport_register * 4;
  const auto c = ReadBuffer<float>(context, buffer, 0, std::max(screen, viewport) + 4);
  if (c.empty()) return "";
  return std::format(" | camera screen {}x{} viewport {}x{}", c[screen], c[screen + 1], c[viewport], c[viewport + 1]);
}

template <typename GetViews>
std::string DescribeInputs(GetViews get_views) {
  std::array<ID3D11ShaderResourceView*, 24> views = {};
  get_views(static_cast<UINT>(views.size()), views.data());
  std::string inputs;
  for (uint32_t slot = 0; slot < views.size(); ++slot) {
    if (views[slot] == nullptr) continue;
    inputs += std::format(" t{} {}", slot, DescribeView(views[slot]));
    views[slot]->Release();
  }
  return inputs;
}

// What a draw reads and how it blends and tests, for the trace's next line: vertex buffers (slot, stride, whether the CPU
// writes it), other stages' shaders, each target's write mask and blend, stencil.
inline std::string DescribeDrawState(reshade::api::command_list* cmd_list, ID3D11DeviceContext* context) {
  std::string text = " | state";
  for (const auto [name, stage] : {std::pair{"gs", reshade::api::pipeline_stage::geometry_shader}, std::pair{"hs", reshade::api::pipeline_stage::hull_shader},
                                   std::pair{"ds", reshade::api::pipeline_stage::domain_shader}}) {
    if (const uint32_t hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, stage); hash != 0) text += std::format(" {} {:08X}", name, hash);
  }
  std::array<ID3D11Buffer*, 4> buffers = {};
  std::array<UINT, 4> strides = {};
  std::array<UINT, 4> offsets = {};
  context->IAGetVertexBuffers(0, static_cast<UINT>(buffers.size()), buffers.data(), strides.data(), offsets.data());
  for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
    if (buffers[slot] == nullptr) continue;
    D3D11_BUFFER_DESC desc = {};
    buffers[slot]->GetDesc(&desc);
    text += std::format(" vb{} stride {}{}", slot, strides[slot], (desc.Usage == D3D11_USAGE_DYNAMIC ? " dynamic" : ""));
    buffers[slot]->Release();
  }
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  context->IAGetPrimitiveTopology(&topology);
  text += std::format(" topology {}", static_cast<int>(topology));
  ComPtr<ID3D11BlendState> blend;
  context->OMGetBlendState(blend.GetAddressOf(), nullptr, nullptr);
  D3D11_BLEND_DESC blend_desc = {};
  if (blend) {
    blend->GetDesc(&blend_desc);
    for (uint32_t slot = 0; slot < 8; ++slot) {
      const auto& target = blend_desc.RenderTarget[blend_desc.IndependentBlendEnable != FALSE ? slot : 0];
      text += std::format(" m{}={:x}{}", slot, target.RenderTargetWriteMask, (target.BlendEnable != FALSE ? "b" : ""));
    }
  } else {
    text += " m=default";
  }
  ComPtr<ID3D11DepthStencilState> depth_state;
  UINT stencil_ref = 0;
  context->OMGetDepthStencilState(depth_state.GetAddressOf(), &stencil_ref);
  D3D11_DEPTH_STENCIL_DESC depth_desc = {};
  if (depth_state) depth_state->GetDesc(&depth_desc);
  if (depth_desc.StencilEnable != FALSE) text += std::format(" stencil ref {} read {:x} write {:x}", stencil_ref, depth_desc.StencilReadMask, depth_desc.StencilWriteMask);
  // Which faces the draw shows (D3D11_CULL_MODE: 1 both, 2 culls the front, 3 the back): a mesh the picture hides by its facing still stands in the rays.
  ComPtr<ID3D11RasterizerState> rasterizer;
  context->RSGetState(rasterizer.GetAddressOf());
  if (rasterizer) {
    D3D11_RASTERIZER_DESC rasterizer_desc = {};
    rasterizer->GetDesc(&rasterizer_desc);
    text += std::format(" cull {}{} bias {}", static_cast<int>(rasterizer_desc.CullMode), (rasterizer_desc.FrontCounterClockwise != FALSE ? " ccw" : ""), rasterizer_desc.DepthBias);
  }
  return text;
}

// The first floats of a draw's constant buffers (VS and PS; Install's value_slots and value_floats).
inline std::string DescribeConstants(ID3D11DeviceContext* context) {
  using Buffers = std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT>;
  const UINT slots = std::min<UINT>(state.options.value_slots, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
  std::string text;
  const auto read = [&](const char* stage, const Buffers& buffers) {
    for (uint32_t slot = 0; slot < slots; ++slot) {
      if (buffers[slot] == nullptr) continue;
      D3D11_BUFFER_DESC desc = {};
      buffers[slot]->GetDesc(&desc);
      text += std::format("\n  {} b{} {} bytes:", stage, slot, desc.ByteWidth);
      for (const float value : ReadBuffer<float>(context, buffers[slot], 0, std::min<UINT>(desc.ByteWidth / 4, state.options.value_floats))) text += std::format(" {}", value);
      buffers[slot]->Release();
    }
  };
  Buffers buffers = {};
  context->VSGetConstantBuffers(0, slots, buffers.data());
  read("vs", buffers);
  buffers = {};
  context->PSGetConstantBuffers(0, slots, buffers.data());
  read("ps", buffers);
  return text;
}

inline void TraceDraw(reshade::api::command_list* cmd_list, std::string_view kind, Draw draw) {
  const bool next_frame = state.next_frame.load(std::memory_order_relaxed);
  if (!state.tracing.load(std::memory_order_relaxed) && !next_frame) return;
  ID3D11DeviceContext* context = NativeContext(cmd_list);
  if (context == nullptr) return;
  const uint32_t vertex_hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::vertex_shader);
  const uint32_t pixel_hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader);
  std::string reads;
  if (state.reads) {
    draw.context = context;
    draw.vertex_shader = vertex_hash;
    draw.pixel_shader = pixel_hash;
    draw.next_frame = next_frame;
    ComPtr<ID3D11InputLayout> input_layout;
    context->IAGetInputLayout(input_layout.GetAddressOf());
    layouts.if_contains(reinterpret_cast<uint64_t>(input_layout.Get()), [&](const auto& entry) {
      const auto& [layout_handle, elements] = entry;
      draw.layout = elements;
    });
    reads = state.options.draw_reads(draw);
  }
  if (next_frame) {
    if (!reads.empty()) Append(std::format("next frame: {} {} vs {:08X} ps {:08X}{}", kind, draw.count * draw.instances, vertex_hash, pixel_hash, reads));
    return;
  }
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
    // The depth test's comparison (0: off) and w: depth written (D3D11's default state without one bound).
    ComPtr<ID3D11DepthStencilState> depth_state;
    context->OMGetDepthStencilState(depth_state.GetAddressOf(), nullptr);
    D3D11_DEPTH_STENCIL_DESC depth_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL, .DepthFunc = D3D11_COMPARISON_LESS};
    if (depth_state) depth_state->GetDesc(&depth_desc);
    outputs += std::format(" ds {} z{}{}", DescribeView(depth), depth_desc.DepthEnable ? static_cast<int>(depth_desc.DepthFunc) : 0,
                           depth_desc.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL ? "w" : "");
    depth->Release();
  }
  D3D11_VIEWPORT viewport = {};
  UINT viewport_count = 1;
  context->RSGetViewports(&viewport_count, &viewport);
  const std::string inputs = DescribeInputs([&](UINT count, ID3D11ShaderResourceView** views) { context->PSGetShaderResources(0, count, views); });
  ComPtr<ID3D11Buffer> constants;
  context->PSGetConstantBuffers(0, 1, constants.GetAddressOf());
  if (!constants) context->VSGetConstantBuffers(0, 1, constants.GetAddressOf());
  // The trace's first indirect draw (the game's particles draw so) and Install's shaders, once each: their constants; and
  // Options' blend and vertex input shaders.
  bool values = false;
  bool blend = false;
  bool vertex_inputs = false;
  {
    const std::lock_guard lock(state.mutex);
    if (kind.ends_with("indirect") && !state.indirect_values_written) {
      state.indirect_values_written = true;
      values = true;
    }
    for (const uint32_t hash : {vertex_hash, pixel_hash}) {
      if (std::ranges::find(state.options.value_hashes, hash) != state.options.value_hashes.end() && std::ranges::find(state.values_written, hash) == state.values_written.end()) {
        state.values_written.push_back(hash);
        values = true;
      }
    }
    if (std::ranges::find(state.options.blend_hashes, pixel_hash) != state.options.blend_hashes.end()
        && std::ranges::find(state.blends_written, pixel_hash) == state.blends_written.end()) {
      state.blends_written.push_back(pixel_hash);
      blend = true;
    }
    vertex_inputs = std::ranges::find(state.options.vertex_input_hashes, vertex_hash) != state.options.vertex_input_hashes.end();
  }
  // After the line's state, in this order: the instances apart, then lines of their own: the constants, the blend, the inputs.
  std::string details;
  if (state.options.instances_apart && draw.instances > 1) details += std::format(" | instanced {} x {}", draw.count, draw.instances);
  if (values) details += DescribeConstants(context);
  if (blend) {
    ComPtr<ID3D11BlendState> blend_state;
    std::array<float, 4> factor = {};
    UINT sample_mask = 0;
    context->OMGetBlendState(blend_state.GetAddressOf(), factor.data(), &sample_mask);
    // D3D11's default state without one bound: no blending, every channel written.
    D3D11_BLEND_DESC blend_desc = {};
    blend_desc.RenderTarget[0] = {.BlendEnable = FALSE,
                                  .SrcBlend = D3D11_BLEND_ONE,
                                  .DestBlend = D3D11_BLEND_ZERO,
                                  .BlendOp = D3D11_BLEND_OP_ADD,
                                  .SrcBlendAlpha = D3D11_BLEND_ONE,
                                  .DestBlendAlpha = D3D11_BLEND_ZERO,
                                  .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                                  .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL};
    if (blend_state) blend_state->GetDesc(&blend_desc);
    details += std::format("\n  blend{}: alpha to coverage {}, factor {} {} {} {}, sample mask {:x}", (blend_state ? "" : " (default)"),
                           blend_desc.AlphaToCoverageEnable, factor[0], factor[1], factor[2], factor[3], sample_mask);
    const UINT blended_targets = (blend_desc.IndependentBlendEnable != FALSE) ? D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT : 1;
    for (UINT slot = 0; slot < blended_targets; ++slot) {
      const auto& target = blend_desc.RenderTarget[slot];
      details += std::format(" | rt{} {} colour src {} dest {} op {}, alpha src {} dest {} op {}, mask {:x}", slot,
                             (target.BlendEnable != FALSE ? "on" : "off"), static_cast<int>(target.SrcBlend), static_cast<int>(target.DestBlend),
                             static_cast<int>(target.BlendOp), static_cast<int>(target.SrcBlendAlpha), static_cast<int>(target.DestBlendAlpha),
                             static_cast<int>(target.BlendOpAlpha), target.RenderTargetWriteMask);
    }
  }
  if (vertex_inputs) {
    std::array<ID3D11ShaderResourceView*, 16> vertex_views = {};
    context->VSGetShaderResources(0, static_cast<UINT>(vertex_views.size()), vertex_views.data());
    details += "\n  vs inputs";
    for (uint32_t slot = 0; slot < vertex_views.size(); ++slot) {
      if (vertex_views[slot] == nullptr) continue;
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
      vertex_views[slot]->GetDesc(&view_desc);
      ComPtr<ID3D11Resource> resource;
      vertex_views[slot]->GetResource(resource.GetAddressOf());
      if (ComPtr<ID3D11Buffer> buffer; SUCCEEDED(resource.As(&buffer))) {
        D3D11_BUFFER_DESC buffer_desc = {};
        buffer->GetDesc(&buffer_desc);
        const bool extended = view_desc.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX;
        const UINT first = extended ? view_desc.BufferEx.FirstElement : view_desc.Buffer.FirstElement;
        const UINT elements = extended ? view_desc.BufferEx.NumElements : view_desc.Buffer.NumElements;
        const bool raw = extended && (view_desc.BufferEx.Flags & D3D11_BUFFEREX_SRV_FLAG_RAW) != 0;
        details += std::format(" | t{} buffer {} bytes, stride {}, misc {:x}{}, view f{} {} elements from {}{}", slot, buffer_desc.ByteWidth,
                               buffer_desc.StructureByteStride, buffer_desc.MiscFlags, (buffer_desc.Usage == D3D11_USAGE_DYNAMIC ? " dynamic" : ""),
                               static_cast<int>(view_desc.Format), elements, first, (raw ? " raw" : ""));
      } else {
        details += std::format(" | t{} {}", slot, DescribeResource(resource.Get()));
      }
      vertex_views[slot]->Release();
    }
  }
  Append(std::format("{} {} vs {:08X} ps {:08X} |{} | vp {}x{} at {},{} |{}{}{}{}{}", kind, draw.count * draw.instances, vertex_hash,
                     pixel_hash, outputs, viewport.Width, viewport.Height, viewport.TopLeftX, viewport.TopLeftY, inputs,
                     DescribeCamera(context, constants.Get()), DescribeDrawState(cmd_list, context), details, reads));
}

inline bool OnDraw(reshade::api::command_list* cmd_list, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
                   uint32_t first_instance) {
  TraceDraw(cmd_list, "draw", {.count = vertex_count, .instances = instance_count, .first = first_vertex, .first_instance = first_instance});
  return false;
}

inline bool OnDrawIndexed(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                          int32_t vertex_offset, uint32_t first_instance) {
  TraceDraw(cmd_list, "indexed",
            {.indexed = true, .count = index_count, .instances = instance_count, .first = first_index, .base_vertex = vertex_offset,
             .first_instance = first_instance});
  return false;
}

inline void TraceDispatch(reshade::api::command_list* cmd_list, std::string_view kind, uint32_t x, uint32_t y, uint32_t z) {
  if (!state.tracing.load(std::memory_order_relaxed)) return;
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
  const std::string inputs = DescribeInputs([&](UINT count, ID3D11ShaderResourceView** views) { context->CSGetShaderResources(0, count, views); });
  ComPtr<ID3D11Buffer> constants;
  context->CSGetConstantBuffers(0, 1, constants.GetAddressOf());
  Append(std::format("{} {}x{}x{} cs {:08X} |{} |{}{}", kind, x, y, z,
                     renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::compute_shader),
                     outputs, inputs, DescribeCamera(context, constants.Get())));
}

inline bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t x, uint32_t y, uint32_t z) {
  TraceDispatch(cmd_list, "dispatch", x, y, z);
  return false;
}

// D3D11's one command at `offset`, its numbers read from the GPU's buffer (0: no read here).
inline bool OnDrawOrDispatchIndirect(reshade::api::command_list* cmd_list, reshade::api::indirect_command type, reshade::api::resource buffer,
                                     uint64_t offset, uint32_t, uint32_t) {
  if (!state.tracing.load(std::memory_order_relaxed)) return false;
  ID3D11DeviceContext* context = NativeContext(cmd_list);
  if (context == nullptr) return false;
  // D3D11's argument structs: a dispatch's 3 counts, a draw's 4 and an indexed draw's 5 (its base vertex signed).
  const UINT words = (type == reshade::api::indirect_command::dispatch ? 3 : (type == reshade::api::indirect_command::draw_indexed ? 5 : 4));
  auto arguments = ReadBuffer<uint32_t>(context, reinterpret_cast<ID3D11Buffer*>(buffer.handle), static_cast<UINT>(offset), words);
  arguments.resize(5);
  switch (type) {
    case reshade::api::indirect_command::dispatch:
      TraceDispatch(cmd_list, "dispatch indirect", arguments[0], arguments[1], arguments[2]);
      break;
    case reshade::api::indirect_command::draw_indexed:
      TraceDraw(cmd_list, "indexed indirect",
                {.indexed = true, .count = arguments[0], .instances = arguments[1], .first = arguments[2],
                 .base_vertex = static_cast<int32_t>(arguments[3]), .first_instance = arguments[4]});
      break;
    default:
      TraceDraw(cmd_list, "draw indirect", {.count = arguments[0], .instances = arguments[1], .first = arguments[2], .first_instance = arguments[3]});
      break;
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

inline bool OnResolveTextureRegion(reshade::api::command_list*, reshade::api::resource source, uint32_t,
                                   const reshade::api::subresource_box*, reshade::api::resource dest, uint32_t, uint32_t,
                                   uint32_t, uint32_t, reshade::api::format) {
  if (!state.tracing.load(std::memory_order_relaxed)) return false;
  Append(std::format("resolve {} -> {}", DescribeResource(reinterpret_cast<ID3D11Resource*>(source.handle)),
                     DescribeResource(reinterpret_cast<ID3D11Resource*>(dest.handle))));
  return false;
}

inline void UseEvents(bool use) {
  if (use) {
    reshade::register_event<reshade::addon_event::draw>(OnDraw);
    reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);
    reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
    reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::register_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
    reshade::register_event<reshade::addon_event::resolve_texture_region>(OnResolveTextureRegion);
  } else {
    reshade::unregister_event<reshade::addon_event::draw>(OnDraw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
    reshade::unregister_event<reshade::addon_event::dispatch>(OnDispatch);
    reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);
    reshade::unregister_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
    reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyResource);
    reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
    reshade::unregister_event<reshade::addon_event::resolve_texture_region>(OnResolveTextureRegion);
  }
}

// ---- The map trace (RENODX_TW_MAP_TRACE).

constexpr uint32_t MAP_TRACE_FRAMES = 3;
constexpr uint32_t MAX_MAP_TRACE_FRAMES = 100;
// Each kind's storage: <frames> x a frame's room (W2's stress frame: ~4,200 draws, ~1,780 maps and as many unmaps),
// capped. Reserved at Install (armed) and held until the trace's file is written; x86 bytes a record: draw 32, event
// 144, native call 112 (12 frames: 3.1 + 9.4 + 7.3 MB at the caps; 3 frames: about 5.5 MB). A record past its room is
// counted as dropped.
constexpr size_t MAP_DRAWS_A_FRAME = 8192;
constexpr size_t MAP_EVENTS_A_FRAME = 6144;
constexpr size_t NATIVE_CALLS_A_FRAME = 6144;
constexpr size_t MAX_MAP_DRAWS = size_t{1} << 17;
constexpr size_t MAX_MAP_EVENTS = size_t{1} << 16;
constexpr size_t MAP_CALLERS = 4;
constexpr uintptr_t MAP_STACK_SCAN_BYTES = 16384;
constexpr size_t MAP_RECENT_PRESENTS = 128;  // the presents before the trace, for the parser's clock alignment
constexpr size_t MAX_NATIVE_CALLS = size_t{1} << 16;
// v3's binds (Set, Set1, IASetVertexBuffers, the seeds, swaps, ClearStates) and their slots, and the constant and vertex
// buffers the game makes while armed. x86 bytes: a bind 56, a slot 12, a buffer 72 (3 frames: 2.1 + 1.2 + 2.4 MB at
// the buffers' cap).
constexpr size_t BINDS_A_FRAME = 12288;
constexpr size_t MAX_BINDS = size_t{1} << 17;
constexpr size_t BIND_SLOTS_A_FRAME = 32768;
constexpr size_t MAX_BIND_SLOTS = size_t{1} << 18;
constexpr size_t MAX_MAP_BUFFERS = size_t{1} << 15;

enum class MapEventKind : uint8_t { MAP, UNMAP, UPDATE, COPY };
enum class MapDrawKind : uint8_t { DRAW, INDEXED, DISPATCH, DRAW_INDIRECT, DISPATCH_INDIRECT };

struct MapDraw {
  uint64_t sequence = 0;  // its place among the native calls and binds (v3)
  int64_t qpc = 0;        // the callback's start (before the native draw)
  int64_t done = 0;       // its end: the next event's gap starts here
  uint32_t thread = 0;
  MapDrawKind kind = MapDrawKind::DRAW;
  uint32_t vertex_shader = 0;  // a dispatch's compute shader
  uint32_t pixel_shader = 0;
  // ReShade's arguments: a draw's vertices, instances, first vertex, first instance; indexed: indices, instances, first
  // index, base vertex (int32), first instance; a dispatch's groups; indirect: offset (low 32 bits), count, stride.
  std::array<uint32_t, 5> args = {};
  uintptr_t indirect_buffer = 0;
};

// A resource as the map trace prints it (a buffer's width in bytes; depth: a 3D texture's, else the array size).
struct MapResource {
  uintptr_t pointer = 0;
  D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depth = 0;
  uint32_t mips = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
  uint32_t bind_flags = 0;
  uint32_t cpu_access_flags = 0;
  uint32_t misc_flags = 0;
};

struct MapEvent {
  int64_t qpc = 0;   // the callback's start (a map's: after the D3D11 runtime's Map returned; the others' before theirs)
  int64_t done = 0;  // its end: the next event's gap starts here (the trace's own time left out)
  uint32_t thread = 0;
  MapEventKind kind = MapEventKind::MAP;
  reshade::api::map_access access = reshade::api::map_access::read_only;
  uint32_t subresource = 0;
  uint64_t bytes = 0;    // an update's or a buffer copy's size (0: the whole resource)
  MapResource resource;  // a copy's destination
  MapResource source;    // a copy's
  std::array<uintptr_t, MAP_CALLERS> callers = {};
};

// A Map or Unmap on the game's immediate context, timed around the native body (the context hooks, below ReShade).
struct NativeMapCall {
  uint64_t sequence = 0;
  int64_t qpc = 0;   // before the native call
  int64_t done = 0;  // after it
  uint32_t thread = 0;
  bool unmap = false;
  D3D11_MAP map_type = D3D11_MAP_READ;
  uint32_t map_flags = 0;
  HRESULT result = S_OK;
  uint32_t subresource = 0;
  uint32_t mapped_bytes = 0;     // a buffer's width, a texture's mapped depth pitch (one slice); 0: the map failed
  uint32_t nested = 0;           // d3d11's own hook entries inside it
  bool content_known = false;    // an unmap of a constant buffer written through its map: `content`, its bytes' hash
  uint64_t content = 0;
  int64_t hash_ticks = 0;        // the hash's time (before the native call, outside qpc / done)
  uintptr_t direct_caller = 0;   // the native call's return address (ReShade's proxy for the game's own maps)
  MapResource resource;          // an unmap's: the pointer only
  std::array<uintptr_t, MAP_CALLERS> callers = {};
};

// A bind slot (v3): a constant buffer with its window (first constant and count in constants; 0, 0: the whole buffer),
// or a vertex buffer with its stride and offset. A null slot: all 0.
struct BindSlot {
  uintptr_t buffer = 0;
  uint32_t first_or_stride = 0;
  uint32_t count_or_offset = 0;
  bool operator==(const BindSlot&) const = default;
};

// A bind (Set, Set1, IASetVertexBuffers, a seed), a swap or a ClearState on the game's immediate context (v3, the render
// thread's): `count` slots from `first_slot` in bind_slots.
struct NativeBind {
  uint64_t sequence = 0;
  int64_t qpc = 0;
  int64_t done = 0;
  context_hooks::Kind kind = context_hooks::Kind::CLEAR_STATE;
  context_hooks::Stage stage = context_hooks::Stage::VERTEX_SHADER;
  uint32_t start_slot = 0;
  uint32_t count = 0;
  uint32_t nested = 0;
  uint32_t first_slot = 0;
  uintptr_t state = 0;  // a swap's new state and the one it replaced
  uintptr_t previous_state = 0;
};

// A constant buffer the game made while armed (ReShade's init_resource), and its end (destroy_resource; destroy_qpc 0:
// not while armed). Constant buffers only: the vertex buffers a load makes would fill the room first.
struct MapBuffer {
  int64_t qpc = 0;
  uint32_t thread = 0;
  MapResource resource;
  int64_t destroy_qpc = 0;
  uint32_t destroy_thread = 0;
};

struct MapModule {
  std::string name;
  uintptr_t base = 0;
};

// An executable section of a MapModule.
struct MapCode {
  uintptr_t begin = 0;
  uintptr_t end = 0;
  size_t module = 0;
};

struct MapTrace {
  std::shared_mutex mutex;
  std::atomic_bool armed = false;    // RENODX_TW_MAP_TRACE set, its file not yet written
  std::atomic_bool tracing = false;  // inside the traced frames (the callbacks' gate)
  bool registered = false;           // the callbacks (Install's, when armed)
  std::string setting;
  double delay_seconds = 0.;
  uint32_t frames = MAP_TRACE_FRAMES;
  int64_t first_present = 0;
  std::array<int64_t, MAP_RECENT_PRESENTS> recent_presents = {};
  size_t recent_count = 0;
  std::vector<int64_t> presents;  // each traced frame's start, then the last one's end
  std::vector<MapDraw> draws;
  std::vector<MapEvent> events;
  std::vector<NativeMapCall> native_calls;
  std::vector<NativeBind> binds;
  std::vector<BindSlot> bind_slots;
  std::vector<MapBuffer> buffers;
  std::unordered_map<uintptr_t, size_t> live_buffers;  // a buffer's record while it lives
  uint32_t dropped_draws = 0;
  uint32_t dropped_events = 0;
  uint32_t dropped_native_calls = 0;
  uint32_t dropped_binds = 0;
  uint32_t dropped_buffers = 0;
  uint64_t sequence = 0;  // the next draw's, native call's or bind's
  // The sequence from which a native call or bind was dropped (UINT64_MAX: none): v3's bindings and contents from it
  // on are not known.
  uint64_t first_drop = UINT64_MAX;
  bool hash_contents = false;  // RENODX_TW_MAP_TRACE's third field `hash`: the unmaps' content hashes
  // The game's immediate context below ReShade's proxy (the present callback's while armed), the context hooks' target.
  // Each context has its own table inside the context object, which d3d11.dll rewrites at every SwapDeviceContextState:
  // hence the hooks on the bodies, not the table.
  std::atomic<ID3D11DeviceContext*> immediate_context = nullptr;
  const char* native_hook = "no_context";  // the trace line's native_hook: on, no_context, failed
  bool waited_for_context = false;  // the trace's start held back one present for the present callback's context
  // The exe; the engine (empire.retail.dll) where the game has one. Written once, before `tracing` is first set.
  std::vector<MapModule> modules;
  std::vector<MapCode> code;
  // The caller scan's stack limits (a test seam: test/tw_map_trace points it at limits that exclude the callback's
  // frame; set only while no callback runs).
  void(WINAPI* stack_limits)(PULONG_PTR, PULONG_PTR) = GetCurrentThreadStackLimits;
};
static MapTrace map_trace;

inline bool IsDispatch(MapDrawKind kind) {
  return kind == MapDrawKind::DISPATCH || kind == MapDrawKind::DISPATCH_INDIRECT;
}

using context_hooks::internal::Qpc;

// The map, unmap and update callbacks' gate (a D3D11 device only: GetType / GetDesc on its handles).
inline bool MapTracing(reshade::api::device* device) {
  return map_trace.tracing.load(std::memory_order_relaxed) && device->get_api() == reshade::api::device_api::d3d11;
}

inline void RecordMapDraw(reshade::api::command_list* cmd_list, MapDrawKind kind, const std::array<uint32_t, 5>& args,
                          uintptr_t indirect_buffer) {
  if (!map_trace.tracing.load(std::memory_order_relaxed)) return;
  const int64_t qpc = Qpc();
  const bool compute = IsDispatch(kind);
  const uint32_t first_shader = renodx::utils::state::GetCurrentShaderHash(
      cmd_list, (compute ? reshade::api::pipeline_stage::compute_shader : reshade::api::pipeline_stage::vertex_shader));
  const uint32_t pixel_shader =
      (compute ? 0u : renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::pixel_shader));
  const std::unique_lock lock(map_trace.mutex);
  if (!map_trace.tracing.load(std::memory_order_relaxed)) return;
  if (map_trace.draws.size() >= map_trace.draws.capacity()) {
    ++map_trace.dropped_draws;
    return;
  }
  map_trace.draws.push_back({.sequence = map_trace.sequence++, .qpc = qpc, .done = Qpc(), .thread = GetCurrentThreadId(),
                             .kind = kind, .vertex_shader = first_shader, .pixel_shader = pixel_shader, .args = args,
                             .indirect_buffer = indirect_buffer});
}

// The game's return addresses above this call: stack values inside the exe's or the engine's code just after a call
// instruction (E8 rel32, or FF /2 in its 2, 3, 4, 6 and 7-byte forms), nearest first, each once (x64 frames keep
// spilled copies): the first is the game's call into the D3D11 runtime. A heuristic: a stale value can match. Only on
// the thread's own stack (a job system's stack that the TEB doesn't describe: no scan). While `tracing` (`code` set).
inline void ScanCallers(std::array<uintptr_t, MAP_CALLERS>* callers) {
  ULONG_PTR stack_low = 0;
  ULONG_PTR stack_high = 0;
  map_trace.stack_limits(&stack_low, &stack_high);
  uintptr_t slot = reinterpret_cast<uintptr_t>(&stack_low) & ~(uintptr_t{sizeof(uintptr_t)} - 1);
  if (slot < stack_low || slot >= stack_high) return;
  const uintptr_t scan_end = std::min<uintptr_t>(stack_high, slot + MAP_STACK_SCAN_BYTES);
  size_t found = 0;
  for (; slot + sizeof(uintptr_t) <= scan_end && found < callers->size(); slot += sizeof(uintptr_t)) {
    const uintptr_t value = *reinterpret_cast<const uintptr_t*>(slot);
    const bool in_code = std::ranges::any_of(map_trace.code, [value](const MapCode& code) {
      return value >= code.begin + 7 && value <= code.end;
    });
    if (!in_code) continue;
    const auto* after = reinterpret_cast<const uint8_t*>(value);
    const auto indirect_call = [after](int length) {
      return after[-length] == 0xFF && (after[-length + 1] & 0x38) == 0x10;
    };
    const auto found_end = callers->begin() + found;
    if (std::ranges::find(callers->begin(), found_end, value) == found_end
        && (after[-5] == 0xE8 || indirect_call(2) || indirect_call(3) || indirect_call(4) || indirect_call(6)
            || indirect_call(7))) {
      (*callers)[found++] = value;
    }
  }
}

// One traced map, unmap, update or staging copy (`event.qpc` taken at its callback's start), with the game's return
// addresses above this call (`with_callers`).
inline void RecordMapEvent(MapEvent event, bool with_callers) {
  // Acquire: `code` was written before `tracing` was set.
  if (!map_trace.tracing.load(std::memory_order_acquire)) return;
  if (with_callers) {
    ScanCallers(&event.callers);
  }
  const std::unique_lock lock(map_trace.mutex);
  if (!map_trace.tracing.load(std::memory_order_relaxed)) return;
  if (map_trace.events.size() >= map_trace.events.capacity()) {
    ++map_trace.dropped_events;
    return;
  }
  event.done = Qpc();
  map_trace.events.push_back(event);
}

inline MapResource DescribeMapResource(reshade::api::resource handle) {
  MapResource result = {.pointer = static_cast<uintptr_t>(handle.handle)};
  auto* resource = reinterpret_cast<ID3D11Resource*>(handle.handle);
  if (resource == nullptr) return result;
  const auto flags = [&result](const auto& desc) {
    result.usage = desc.Usage;
    result.bind_flags = desc.BindFlags;
    result.cpu_access_flags = desc.CPUAccessFlags;
    result.misc_flags = desc.MiscFlags;
  };
  resource->GetType(&result.dimension);
  switch (result.dimension) {
    case D3D11_RESOURCE_DIMENSION_BUFFER: {
      D3D11_BUFFER_DESC desc = {};
      static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
      flags(desc);
      result.width = desc.ByteWidth;
      break;
    }
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D: {
      D3D11_TEXTURE1D_DESC desc = {};
      static_cast<ID3D11Texture1D*>(resource)->GetDesc(&desc);
      flags(desc);
      result.width = desc.Width;
      result.height = 1;
      result.depth = desc.ArraySize;
      result.mips = desc.MipLevels;
      result.format = desc.Format;
      break;
    }
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D: {
      D3D11_TEXTURE2D_DESC desc = {};
      static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
      flags(desc);
      result.width = desc.Width;
      result.height = desc.Height;
      result.depth = desc.ArraySize;
      result.mips = desc.MipLevels;
      result.format = desc.Format;
      break;
    }
    case D3D11_RESOURCE_DIMENSION_TEXTURE3D: {
      D3D11_TEXTURE3D_DESC desc = {};
      static_cast<ID3D11Texture3D*>(resource)->GetDesc(&desc);
      flags(desc);
      result.width = desc.Width;
      result.height = desc.Height;
      result.depth = desc.Depth;
      result.mips = desc.MipLevels;
      result.format = desc.Format;
      break;
    }
    default:
      break;
  }
  return result;
}

// The context hooks' measure path (v3): every call on the game's immediate context (the render thread's). A map or unmap
// as v2's native call (a map's resource described and its game callers scanned), the rest as binds.
inline void OnNativeCall(const context_hooks::Call& call) {
  // Acquire: `code` (the callers' scan) was written before `tracing` was set.
  if (!map_trace.tracing.load(std::memory_order_acquire)) return;
  if (call.kind == context_hooks::Kind::MAP || call.kind == context_hooks::Kind::UNMAP) {
    const bool unmap = (call.kind == context_hooks::Kind::UNMAP);
    NativeMapCall record = {.qpc = call.qpc, .done = call.done, .thread = GetCurrentThreadId(), .unmap = unmap,
                            .map_type = call.map_type, .map_flags = call.map_flags, .result = call.result,
                            .subresource = call.subresource, .nested = call.nested, .content_known = call.content_known,
                            .content = call.content, .hash_ticks = call.hash_ticks,
                            .direct_caller = call.direct_caller,
                            .resource = {.pointer = reinterpret_cast<uintptr_t>(call.resource)}};
    if (!unmap) {
      record.resource = DescribeMapResource({reinterpret_cast<uintptr_t>(call.resource)});
      if (SUCCEEDED(call.result)) {
        record.mapped_bytes =
            (record.resource.dimension == D3D11_RESOURCE_DIMENSION_BUFFER ? record.resource.width : call.depth_pitch);
      }
      // The game's return addresses: the maps' only (an unmap is matched to its map; its own scan would be thrown away).
      ScanCallers(&record.callers);
    }
    const std::unique_lock lock(map_trace.mutex);
    if (!map_trace.tracing.load(std::memory_order_relaxed)) return;
    if (map_trace.native_calls.size() >= map_trace.native_calls.capacity()) {
      ++map_trace.dropped_native_calls;
      map_trace.first_drop = std::min(map_trace.first_drop, map_trace.sequence);
      return;
    }
    record.sequence = map_trace.sequence++;
    map_trace.native_calls.push_back(record);
    return;
  }
  const bool vertex = (call.kind == context_hooks::Kind::SET_VERTEX_BUFFERS
                       || call.kind == context_hooks::Kind::SEED_VERTEX_BUFFERS);
  const UINT slot_count =
      (vertex ? D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT : D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
  const UINT count = (call.start_slot < slot_count ? std::min(call.count, slot_count - call.start_slot) : 0);
  const std::unique_lock lock(map_trace.mutex);
  if (!map_trace.tracing.load(std::memory_order_relaxed)) return;
  if (map_trace.binds.size() >= map_trace.binds.capacity()
      || map_trace.bind_slots.size() + count > map_trace.bind_slots.capacity()) {
    ++map_trace.dropped_binds;
    map_trace.first_drop = std::min(map_trace.first_drop, map_trace.sequence);
    return;
  }
  map_trace.binds.push_back({.sequence = map_trace.sequence++, .qpc = call.qpc, .done = call.done, .kind = call.kind,
                             .stage = call.stage, .start_slot = call.start_slot, .count = count, .nested = call.nested,
                             .first_slot = static_cast<uint32_t>(map_trace.bind_slots.size()),
                             .state = reinterpret_cast<uintptr_t>(call.state),
                             .previous_state = reinterpret_cast<uintptr_t>(call.previous_state)});
  for (UINT index = 0; index < count; ++index) {
    BindSlot slot = {.buffer = (call.buffers != nullptr ? reinterpret_cast<uintptr_t>(call.buffers[index]) : 0)};
    // A null slot stays all zero (Get1 reports a window for it too); 0 + 4096 constants (Get1's answer for a Set bind)
    // is the whole buffer, as Set's.
    if (slot.buffer != 0 && vertex) {
      slot.first_or_stride = (call.strides != nullptr ? call.strides[index] : 0);
      slot.count_or_offset = (call.offsets != nullptr ? call.offsets[index] : 0);
    } else if (slot.buffer != 0 && call.first_constants != nullptr && call.constant_counts != nullptr
               && (call.first_constants[index] != 0
                   || call.constant_counts[index] != D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT)) {
      slot.first_or_stride = call.first_constants[index];
      slot.count_or_offset = call.constant_counts[index];
    }
    map_trace.bind_slots.push_back(slot);
  }
}

// While armed (from Install, registered there: ReShade tells a buffer's end only to handlers present at its creation):
// the constant buffers the game makes, with their threads.
inline void OnMapTraceInitResource(reshade::api::device* device, const reshade::api::resource_desc& desc,
                                   const reshade::api::subresource_data*, reshade::api::resource_usage,
                                   reshade::api::resource resource) {
  if (!map_trace.armed.load(std::memory_order_relaxed) || desc.type != reshade::api::resource_type::buffer
      || (desc.usage & reshade::api::resource_usage::constant_buffer) == reshade::api::resource_usage::undefined
      || device->get_api() != reshade::api::device_api::d3d11) {
    return;
  }
  MapBuffer record = {.qpc = Qpc(), .thread = GetCurrentThreadId(), .resource = DescribeMapResource(resource)};
  const std::unique_lock lock(map_trace.mutex);
  if (!map_trace.armed.load(std::memory_order_relaxed)) return;
  if (map_trace.buffers.size() >= map_trace.buffers.capacity()) {
    ++map_trace.dropped_buffers;
    return;
  }
  map_trace.live_buffers.insert_or_assign(record.resource.pointer, map_trace.buffers.size());
  map_trace.buffers.push_back(record);
}

inline void OnMapTraceDestroyResource(reshade::api::device*, reshade::api::resource resource) {
  if (!map_trace.armed.load(std::memory_order_relaxed)) return;
  const int64_t qpc = Qpc();
  const std::unique_lock lock(map_trace.mutex);
  if (const auto live = map_trace.live_buffers.find(static_cast<uintptr_t>(resource.handle));
      live != map_trace.live_buffers.end()) {
    auto& record = map_trace.buffers[live->second];
    record.destroy_qpc = qpc;
    record.destroy_thread = GetCurrentThreadId();
    map_trace.live_buffers.erase(live);
  }
}

// While armed, a device's end: the context hooks out with the game's device, under the trace's lock (the trace's end
// and Uninstall remove them under it; Install runs on the render thread at a present, not beside its device's end), and
// the immediate context let go (a trace start must not use the freed one).
inline void OnMapTraceDestroyDevice(reshade::api::device* device) {
  if (device->get_api() != reshade::api::device_api::d3d11) return;
  const std::unique_lock lock(map_trace.mutex);
  context_hooks::OnDestroyDevice(device);
  if (auto* context = map_trace.immediate_context.load(std::memory_order_relaxed); context != nullptr) {
    Microsoft::WRL::ComPtr<ID3D11Device> context_device;
    context->GetDevice(context_device.GetAddressOf());
    if (context_device.Get() == reinterpret_cast<ID3D11Device*>(device->get_native())) {
      map_trace.immediate_context.store(nullptr, std::memory_order_relaxed);
    }
  }
}

// While armed: the game's immediate context, the real one below ReShade's proxy (its native).
inline void OnMapTracePresent(reshade::api::command_queue* queue, reshade::api::swapchain*, const reshade::api::rect*,
                              const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
  if (!map_trace.armed.load(std::memory_order_relaxed)) return;
  if (queue->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  map_trace.immediate_context.store(
      reinterpret_cast<ID3D11DeviceContext*>(queue->get_immediate_command_list()->get_native()),
      std::memory_order_relaxed);
}

// A copy into a staging resource (the game's readbacks); other copies are not traced. `bytes`: ReShade's UINT64_MAX
// (the whole buffer) as 0.
inline void RecordStagingCopy(reshade::api::command_list* cmd_list, reshade::api::resource source,
                              reshade::api::resource dest, uint64_t bytes) {
  // The gate before the virtual get_device(): every copy of the run comes through here.
  if (!map_trace.tracing.load(std::memory_order_relaxed)) return;
  if (!MapTracing(cmd_list->get_device())) return;
  const int64_t qpc = Qpc();
  const MapResource destination = DescribeMapResource(dest);
  if (destination.usage != D3D11_USAGE_STAGING) return;
  RecordMapEvent({.qpc = qpc, .thread = GetCurrentThreadId(), .kind = MapEventKind::COPY,
                  .bytes = (bytes == UINT64_MAX ? 0 : bytes), .resource = destination,
                  .source = DescribeMapResource(source)},
                 true);
}

inline bool OnMapTraceDraw(reshade::api::command_list* cmd_list, uint32_t vertices, uint32_t instances, uint32_t first_vertex,
                           uint32_t first_instance) {
  RecordMapDraw(cmd_list, MapDrawKind::DRAW, {vertices, instances, first_vertex, first_instance}, 0);
  return false;
}

inline bool OnMapTraceDrawIndexed(reshade::api::command_list* cmd_list, uint32_t indices, uint32_t instances,
                                  uint32_t first_index, int32_t base_vertex, uint32_t first_instance) {
  RecordMapDraw(cmd_list, MapDrawKind::INDEXED,
                {indices, instances, first_index, static_cast<uint32_t>(base_vertex), first_instance}, 0);
  return false;
}

inline bool OnMapTraceDispatch(reshade::api::command_list* cmd_list, uint32_t x, uint32_t y, uint32_t z) {
  RecordMapDraw(cmd_list, MapDrawKind::DISPATCH, {x, y, z}, 0);
  return false;
}

inline bool OnMapTraceIndirect(reshade::api::command_list* cmd_list, reshade::api::indirect_command type,
                               reshade::api::resource buffer, uint64_t offset, uint32_t count, uint32_t stride) {
  RecordMapDraw(cmd_list,
                (type == reshade::api::indirect_command::dispatch ? MapDrawKind::DISPATCH_INDIRECT
                                                                   : MapDrawKind::DRAW_INDIRECT),
                {static_cast<uint32_t>(offset), count, stride}, static_cast<uintptr_t>(buffer.handle));
  return false;
}

inline void OnMapTraceMapBuffer(reshade::api::device* device, reshade::api::resource resource, uint64_t, uint64_t,
                                reshade::api::map_access access, void**) {
  if (!MapTracing(device)) return;
  RecordMapEvent({.qpc = Qpc(), .thread = GetCurrentThreadId(), .kind = MapEventKind::MAP, .access = access,
                  .resource = DescribeMapResource(resource)},
                 true);
}

inline void OnMapTraceMapTexture(reshade::api::device* device, reshade::api::resource resource, uint32_t subresource,
                                 const reshade::api::subresource_box*, reshade::api::map_access access,
                                 reshade::api::subresource_data*) {
  if (!MapTracing(device)) return;
  RecordMapEvent({.qpc = Qpc(), .thread = GetCurrentThreadId(), .kind = MapEventKind::MAP, .access = access,
                  .subresource = subresource, .resource = DescribeMapResource(resource)},
                 true);
}

inline void OnMapTraceUnmapBuffer(reshade::api::device* device, reshade::api::resource resource) {
  if (!MapTracing(device)) return;
  RecordMapEvent({.qpc = Qpc(), .thread = GetCurrentThreadId(), .kind = MapEventKind::UNMAP,
                  .resource = {.pointer = static_cast<uintptr_t>(resource.handle)}},
                 false);
}

inline void OnMapTraceUnmapTexture(reshade::api::device* device, reshade::api::resource resource, uint32_t subresource) {
  if (!MapTracing(device)) return;
  RecordMapEvent({.qpc = Qpc(), .thread = GetCurrentThreadId(), .kind = MapEventKind::UNMAP, .subresource = subresource,
                  .resource = {.pointer = static_cast<uintptr_t>(resource.handle)}},
                 false);
}

inline bool OnMapTraceUpdateBuffer(reshade::api::device* device, const void*, reshade::api::resource dest, uint64_t,
                                   uint64_t size) {
  if (!MapTracing(device)) return false;
  RecordMapEvent({.qpc = Qpc(), .thread = GetCurrentThreadId(), .kind = MapEventKind::UPDATE,
                  .bytes = (size == UINT64_MAX ? 0 : size), .resource = DescribeMapResource(dest)},
                 true);
  return false;
}

inline bool OnMapTraceCopyResource(reshade::api::command_list* cmd_list, reshade::api::resource source,
                                   reshade::api::resource dest) {
  RecordStagingCopy(cmd_list, source, dest, 0);
  return false;
}

inline bool OnMapTraceCopyBuffer(reshade::api::command_list* cmd_list, reshade::api::resource source, uint64_t,
                                 reshade::api::resource dest, uint64_t, uint64_t size) {
  RecordStagingCopy(cmd_list, source, dest, size);
  return false;
}

inline bool OnMapTraceCopyTexture(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t,
                                  const reshade::api::subresource_box*, reshade::api::resource dest, uint32_t,
                                  const reshade::api::subresource_box*, reshade::api::filter_mode) {
  RecordStagingCopy(cmd_list, source, dest, 0);
  return false;
}

// At Install (armed) and Uninstall only: ReShade's event lists are not safe to change while other threads draw.
inline void UseMapEvents(bool use) {
  if (use) {
    reshade::register_event<reshade::addon_event::draw>(OnMapTraceDraw);
    reshade::register_event<reshade::addon_event::draw_indexed>(OnMapTraceDrawIndexed);
    reshade::register_event<reshade::addon_event::dispatch>(OnMapTraceDispatch);
    reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnMapTraceIndirect);
    reshade::register_event<reshade::addon_event::map_buffer_region>(OnMapTraceMapBuffer);
    reshade::register_event<reshade::addon_event::map_texture_region>(OnMapTraceMapTexture);
    reshade::register_event<reshade::addon_event::unmap_buffer_region>(OnMapTraceUnmapBuffer);
    reshade::register_event<reshade::addon_event::unmap_texture_region>(OnMapTraceUnmapTexture);
    reshade::register_event<reshade::addon_event::update_buffer_region>(OnMapTraceUpdateBuffer);
    reshade::register_event<reshade::addon_event::copy_resource>(OnMapTraceCopyResource);
    reshade::register_event<reshade::addon_event::copy_buffer_region>(OnMapTraceCopyBuffer);
    reshade::register_event<reshade::addon_event::copy_texture_region>(OnMapTraceCopyTexture);
    reshade::register_event<reshade::addon_event::present>(OnMapTracePresent);
    reshade::register_event<reshade::addon_event::init_resource>(OnMapTraceInitResource);
    reshade::register_event<reshade::addon_event::destroy_resource>(OnMapTraceDestroyResource);
    reshade::register_event<reshade::addon_event::destroy_device>(OnMapTraceDestroyDevice);
  } else {
    reshade::unregister_event<reshade::addon_event::draw>(OnMapTraceDraw);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(OnMapTraceDrawIndexed);
    reshade::unregister_event<reshade::addon_event::dispatch>(OnMapTraceDispatch);
    reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnMapTraceIndirect);
    reshade::unregister_event<reshade::addon_event::map_buffer_region>(OnMapTraceMapBuffer);
    reshade::unregister_event<reshade::addon_event::map_texture_region>(OnMapTraceMapTexture);
    reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(OnMapTraceUnmapBuffer);
    reshade::unregister_event<reshade::addon_event::unmap_texture_region>(OnMapTraceUnmapTexture);
    reshade::unregister_event<reshade::addon_event::update_buffer_region>(OnMapTraceUpdateBuffer);
    reshade::unregister_event<reshade::addon_event::copy_resource>(OnMapTraceCopyResource);
    reshade::unregister_event<reshade::addon_event::copy_buffer_region>(OnMapTraceCopyBuffer);
    reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnMapTraceCopyTexture);
    reshade::unregister_event<reshade::addon_event::present>(OnMapTracePresent);
    reshade::unregister_event<reshade::addon_event::init_resource>(OnMapTraceInitResource);
    reshade::unregister_event<reshade::addon_event::destroy_resource>(OnMapTraceDestroyResource);
    reshade::unregister_event<reshade::addon_event::destroy_device>(OnMapTraceDestroyDevice);
  }
}

}  // namespace internal

// At the addon's start: whether this run traces a frame (FrameTrace), and how; whether with the draw reads
// (FrameTraceReads).
inline void Install(Options options = {}) {
  bool wanted = false;
  reshade::get_config_value(nullptr, "renodx", "FrameTrace", wanted);
  bool reads = false;
  reshade::get_config_value(nullptr, "renodx", "FrameTraceReads", reads);
  reads = reads && options.draw_reads != nullptr;
  if (reads) {
    reshade::register_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
    reshade::register_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyPipeline);
    renodx::utils::log::i("[Total War] Frame trace: the draw reads on (FrameTraceReads).");
  }
  // The map trace: <seconds after the first present>[,<frames>[,hash]], parsed without the C runtime's locale.
  std::array<char, 64> setting = {};
  const DWORD setting_length =
      GetEnvironmentVariableA("RENODX_TW_MAP_TRACE", setting.data(), static_cast<DWORD>(setting.size()));
  if (setting_length != 0) {
    const char* setting_end = setting.data() + std::min<size_t>(setting_length, setting.size() - 1);
    double delay_seconds = -1.;
    uint32_t frames = internal::MAP_TRACE_FRAMES;
    bool hash_contents = false;
    const auto [delay_end, delay_error] = std::from_chars(setting.data(), setting_end, delay_seconds);
    bool valid = setting_length < setting.size() && delay_error == std::errc{} && delay_seconds >= 0.
                 && delay_seconds <= 86400. && (delay_end == setting_end || *delay_end == ',');
    if (valid && delay_end != setting_end) {
      const auto [frames_end, frames_error] = std::from_chars(delay_end + 1, setting_end, frames);
      hash_contents = (std::string_view(frames_end, setting_end) == ",hash");
      valid = frames_error == std::errc{} && (frames_end == setting_end || hash_contents) && frames >= 1
              && frames <= internal::MAX_MAP_TRACE_FRAMES;
    }
    auto& trace = internal::map_trace;
    if (valid) {
      const std::unique_lock lock(trace.mutex);
      // Its storage now, not inside a present.
      trace.tracing.store(false);
      trace.presents.clear();
      trace.draws.clear();
      trace.events.clear();
      trace.native_calls.clear();
      trace.binds.clear();
      trace.bind_slots.clear();
      trace.buffers.clear();
      trace.live_buffers.clear();
      trace.modules.clear();
      trace.code.clear();
      trace.dropped_draws = 0;
      trace.dropped_events = 0;
      trace.dropped_native_calls = 0;
      trace.dropped_binds = 0;
      trace.dropped_buffers = 0;
      trace.sequence = 0;
      trace.first_drop = UINT64_MAX;
      trace.native_hook = "no_context";
      trace.waited_for_context = false;
      trace.immediate_context.store(nullptr);
      try {
        trace.presents.reserve(frames + 1);
        trace.draws.reserve(std::min(internal::MAX_MAP_DRAWS, frames * internal::MAP_DRAWS_A_FRAME));
        trace.events.reserve(std::min(internal::MAX_MAP_EVENTS, frames * internal::MAP_EVENTS_A_FRAME));
        trace.native_calls.reserve(std::min(internal::MAX_NATIVE_CALLS, frames * internal::NATIVE_CALLS_A_FRAME));
        trace.binds.reserve(std::min(internal::MAX_BINDS, frames * internal::BINDS_A_FRAME));
        trace.bind_slots.reserve(std::min(internal::MAX_BIND_SLOTS, frames * internal::BIND_SLOTS_A_FRAME));
        trace.buffers.reserve(internal::MAX_MAP_BUFFERS);
      } catch (const std::bad_alloc&) {
        trace.presents = {};
        trace.draws = {};
        trace.events = {};
        trace.native_calls = {};
        trace.binds = {};
        trace.bind_slots = {};
        trace.buffers = {};
        valid = false;
        renodx::utils::log::w("[Total War] Map trace: no memory for its storage: off.");
      }
      if (valid) {
        trace.setting = setting.data();
        trace.delay_seconds = delay_seconds;
        trace.frames = frames;
        trace.hash_contents = hash_contents;
        trace.first_present = 0;
        trace.recent_count = 0;
        trace.registered = true;
        trace.armed.store(true);
        renodx::utils::log::i("[Total War] Map trace (RENODX_TW_MAP_TRACE=", trace.setting, "): ", frames, " frames from ",
                              delay_seconds, " s after the first present, into tw_map_trace.txt and tw_map_trace_v3.txt",
                              (hash_contents ? ", constant buffer contents hashed." : "."));
      }
    } else {
      renodx::utils::log::w("[Total War] Map trace: RENODX_TW_MAP_TRACE=", setting.data(),
                            " is not <seconds after the first present>[,<frames 1-100>[,hash]]: off.");
    }
    if (valid) {
      internal::UseMapEvents(true);
    }
  }
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  const std::lock_guard lock(internal::state.mutex);
  internal::state.wanted = wanted;
  internal::state.reads = reads;
  internal::state.options = std::move(options);
  internal::state.folder = std::filesystem::path(exe).parent_path();
}

// At the addon's end: the events Install (FrameTraceReads), a frame being traced and the map trace registered.
inline void Uninstall() {
  internal::state.tracing.store(false);
  internal::state.next_frame.store(false);
  internal::UseEvents(false);
  const bool reads = [] {
    const std::lock_guard lock(internal::state.mutex);
    return std::exchange(internal::state.reads, false);
  }();
  if (reads) {
    reshade::unregister_event<reshade::addon_event::init_pipeline>(internal::OnInitPipeline);
    reshade::unregister_event<reshade::addon_event::destroy_pipeline>(internal::OnDestroyPipeline);
  }
  const bool map_registered = [] {
    const std::unique_lock lock(internal::map_trace.mutex);
    internal::map_trace.armed.store(false);
    internal::map_trace.tracing.store(false);
    context_hooks::Uninstall();
    return std::exchange(internal::map_trace.registered, false);
  }();
  if (map_registered) {
    internal::UseMapEvents(false);
  }
}

// Whether a trace is still to come this run (FrameTrace, the marker).
inline bool Wanted() {
  const std::lock_guard lock(internal::state.mutex);
  return internal::state.wanted;
}

// Whether this frame is traced (marks that cost something to build are made only then).
inline bool Tracing() {
  return internal::state.tracing.load(std::memory_order_relaxed);
}

// A mark in the traced frame (the anti-aliasing's tone mapper end, DLSS).
inline void Mark(std::string_view text) {
  if (internal::state.tracing.load(std::memory_order_relaxed)) internal::Append(std::string("MARK ") + std::string(text));
}

// Every present (`scaled`: the DLSS render scale ran this frame; `view_3d`: the 3D view drew, at any scale): ends a
// traced frame (writes the file; with the draw reads after the frame after it); starts one after SCALED_PRESENTS scaled
// presents (at_100: of the 3D view), or at once when the marker is there (on_demand). The map trace: starts at the first
// present its delay after the first one, ends its frames later (its file written then).
inline void OnPresent(bool scaled, bool view_3d = false) {
  auto& state = internal::state;
  if (auto& trace = internal::map_trace; trace.armed.load(std::memory_order_relaxed)) {
    // A module's file name, ASCII ('?' for any other letter).
    const auto module_name = [](HMODULE module) {
      std::wstring path(32768, L'\0');
      path.resize(GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size())));
      // Named: a range-for over filename()'s temporary would dangle.
      const std::filesystem::path file_name = std::filesystem::path(path).filename();
      std::string name;
      for (const wchar_t letter : file_name.native()) {
        name += (letter < 128 ? static_cast<char>(letter) : '?');
      }
      return name;
    };
    const int64_t now = internal::Qpc();
    LARGE_INTEGER frequency = {};
    QueryPerformanceFrequency(&frequency);
    std::unique_lock lock(trace.mutex);
    if (!trace.tracing.load(std::memory_order_relaxed)) {
      if (trace.first_present == 0) {
        trace.first_present = now;
      }
      const bool before_start =
          static_cast<double>(now - trace.first_present) < trace.delay_seconds * static_cast<double>(frequency.QuadPart);
      // No context yet at the start (Attila's own present callback, this one's caller, runs before the map trace's
      // present callback that gives it): one present later, once.
      const bool waiting_for_context = !before_start && !trace.waited_for_context
                                       && trace.immediate_context.load(std::memory_order_relaxed) == nullptr;
      if (waiting_for_context) {
        trace.waited_for_context = true;
      }
      if (before_start || waiting_for_context) {
        trace.recent_presents[trace.recent_count % trace.recent_presents.size()] = now;
        ++trace.recent_count;
      } else {
        // The game's code for the return addresses: the exe, and the engine where the game has one (Attila's, Rome II's);
        // a module whose headers don't look like a PE image's is left out.
        try {
          for (HMODULE module : {GetModuleHandleW(nullptr), GetModuleHandleW(L"empire.retail.dll")}) {
            if (module == nullptr) continue;
            const auto base = reinterpret_cast<uintptr_t>(module);
            const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            if (dos_header->e_magic != IMAGE_DOS_SIGNATURE || dos_header->e_lfanew <= 0
                || dos_header->e_lfanew > 4096 - static_cast<LONG>(sizeof(IMAGE_NT_HEADERS))) {
              continue;
            }
            const auto* headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos_header->e_lfanew);
            if (headers->Signature != IMAGE_NT_SIGNATURE) continue;
            for (const auto& section : std::span(IMAGE_FIRST_SECTION(headers), headers->FileHeader.NumberOfSections)) {
              if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 || section.Misc.VirtualSize == 0
                  || uint64_t{section.VirtualAddress} + section.Misc.VirtualSize > headers->OptionalHeader.SizeOfImage) {
                continue;
              }
              trace.code.push_back({.begin = base + section.VirtualAddress,
                                    .end = base + section.VirtualAddress + section.Misc.VirtualSize,
                                    .module = trace.modules.size()});
            }
            trace.modules.push_back({.name = module_name(module), .base = base});
          }
        } catch (const std::bad_alloc&) {
          trace.code.clear();
          renodx::utils::log::w("[Total War] Map trace: no memory for the game's module list: the callers left out.");
        }
        auto* context = trace.immediate_context.load(std::memory_order_relaxed);
        trace.native_hook = (context == nullptr ? "no_context" : "failed");
        trace.presents.assign(1, now);
        trace.tracing.store(true, std::memory_order_release);
        // The context hooks on the game's immediate context (the present callback's), after the lock: their seeds come
        // through OnNativeCall, which takes it.
        lock.unlock();
        if (context != nullptr && context_hooks::Install(context, internal::OnNativeCall, trace.hash_contents)) {
          trace.native_hook = "on";
        }
      }
    } else {
      trace.presents.push_back(now);
      if (trace.presents.size() > trace.frames) {
        trace.tracing.store(false);
        trace.armed.store(false);
        // A call from another thread switched the hooks to pass-through (logged): the header says so.
        if (std::string_view(trace.native_hook) == "on"
            && context_hooks::internal::state.foreign_qpc.load(std::memory_order_relaxed) != 0) {
          trace.native_hook = "foreign_thread";
        }
        context_hooks::Uninstall();
        // The callbacks stay registered (until Uninstall), gated off.
        try {
          const double ms_per_tick = 1000. / static_cast<double>(frequency.QuadPart);
          const size_t frames = trace.presents.size() - 1;
          // An event timed just after the last present but recorded before the present took the lock: the last frame's.
          const auto frame_of = [&](int64_t qpc) {
            const auto after = static_cast<size_t>(std::ranges::upper_bound(trace.presents, qpc) - trace.presents.begin());
            return std::clamp(after, size_t{1}, frames) - 1;
          };
          // Each thread's draws in order, for an event's draw index, last draw and gap.
          std::unordered_map<uint32_t, std::vector<const internal::MapDraw*>> thread_draws;
          std::vector<std::array<uint32_t, 6>> counts(frames);  // draws, dispatches, maps, unmaps, updates, copies
          for (const auto& draw : trace.draws) {
            thread_draws[draw.thread].push_back(&draw);
            ++counts[frame_of(draw.qpc)][internal::IsDispatch(draw.kind) ? 1 : 0];
          }
          // Each map's unmap: the next unmap of its resource and subresource (on any thread or context).
          std::vector<size_t> unmap_of(trace.events.size(), SIZE_MAX);
          std::vector<bool> unmap_matched(trace.events.size(), false);
          std::map<std::pair<uintptr_t, uint32_t>, size_t> open_maps;
          for (size_t index = 0; index < trace.events.size(); ++index) {
            const auto& event = trace.events[index];
            const auto key = std::pair{event.resource.pointer, event.subresource};
            ++counts[frame_of(event.qpc)][2 + static_cast<size_t>(event.kind)];
            if (event.kind == internal::MapEventKind::MAP) {
              open_maps[key] = index;
            } else if (event.kind == internal::MapEventKind::UNMAP) {
              if (const auto open = open_maps.find(key); open != open_maps.end()) {
                unmap_of[open->second] = index;
                unmap_matched[index] = true;
                open_maps.erase(open);
              }
            }
          }
          // Each file written in parts of about 4 MB (a 12-frame trace is tens of MB: one string would need that much of
          // an x86 game's address space in one block, twice while it grows), into <name>.part, renamed when whole: a
          // write that fails half way leaves no file that looks complete.
          std::filesystem::path path;
          std::filesystem::path part_path;
          std::ofstream file;
          std::string text;
          uint32_t parts = 0;
          const auto open_file = [&](const wchar_t* name) {
            path = state.folder / name;
            part_path = path;
            part_path += L".part";
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            file.open(part_path, std::ios::binary | std::ios::trunc);
          };
          const auto write_part = [&file, &text, &parts](bool last) {
            if (!last && text.size() < (size_t{1} << 22)) return;
            file << text;
            text.clear();
            ++parts;
          };
          // The last part and the rename: true when the file is whole.
          const auto close_file = [&] {
            write_part(true);
            file.close();
            std::error_code renamed;
            if (file) {
              std::filesystem::rename(part_path, path, renamed);
            }
            return file && !renamed;
          };
          open_file(L"tw_map_trace.txt");
          text = std::format(
              "trace setting={} qpc_frequency={} frames={} end_qpc={} draws={} events={} dropped_draws={}"
              " dropped_events={} native_calls={} dropped_native_calls={} native_hook={}\n",
              trace.setting, frequency.QuadPart, frames, trace.presents.back(), trace.draws.size(), trace.events.size(),
              trace.dropped_draws, trace.dropped_events, trace.native_calls.size(), trace.dropped_native_calls,
              trace.native_hook);
          for (const auto& module : trace.modules) {
            text += std::format("module name={} base=0x{:x}\n", module.name, module.base);
          }
          // Every present the trace saw, oldest first: the ones before it (up to MAP_RECENT_PRESENTS), then its own.
          const size_t recent = std::min(trace.recent_count, trace.recent_presents.size());
          for (size_t index = trace.recent_count - recent; index < trace.recent_count; ++index) {
            text += std::format("present qpc={}\n", trace.recent_presents[index % trace.recent_presents.size()]);
          }
          for (const int64_t present : trace.presents) {
            text += std::format("present qpc={}\n", present);
          }
          for (size_t frame = 0; frame < frames; ++frame) {
            const auto& count = counts[frame];
            text += std::format(
                "frame index={} present_qpc={} ms={:.3f} draws={} dispatches={} maps={} unmaps={} updates={} copies={}\n",
                frame, trace.presents[frame],
                static_cast<double>(trace.presents[frame + 1] - trace.presents[frame]) * ms_per_tick, count[0], count[1], count[2], count[3], count[4], count[5]);
          }
          const auto describe = [](std::string_view prefix, const internal::MapResource& resource) {
            constexpr std::array<const char*, 4> USAGES = {"default", "immutable", "dynamic", "staging"};
            std::string description = std::format(
                " {0}res=0x{1:x} {0}usage={2} {0}bind=0x{3:x} {0}cpu=0x{4:x} {0}misc=0x{5:x}", prefix, resource.pointer,
                (static_cast<size_t>(resource.usage) < USAGES.size() ? USAGES[resource.usage] : "?"), resource.bind_flags,
                resource.cpu_access_flags, resource.misc_flags);
            switch (resource.dimension) {
              case D3D11_RESOURCE_DIMENSION_BUFFER:
                description += std::format(" {0}type=buffer {0}bytes={1}", prefix, resource.width);
                break;
              case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
              case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
              case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
                description += std::format(" {0}type=tex{1}d {0}size={2}x{3}x{4} {0}mips={5} {0}format={6}", prefix,
                                           static_cast<int>(resource.dimension)
                                               - static_cast<int>(D3D11_RESOURCE_DIMENSION_BUFFER),
                                           resource.width, resource.height, resource.depth, resource.mips,
                                           static_cast<int>(resource.format));
                break;
              default:
                description += std::format(" {}type=?", prefix);
                break;
            }
            return description;
          };
          constexpr std::array<const char*, 4> EVENT_KINDS = {"map", "unmap", "update", "copy"};
          constexpr std::array<const char*, 5> DRAW_KINDS = {"draw", "indexed", "dispatch", "draw_indirect",
                                                             "dispatch_indirect"};
          // " callers=..." and the line's end: the first return addresses into the game's code (module+RVA).
          const auto callers_text = [&trace](const std::array<uintptr_t, internal::MAP_CALLERS>& addresses) {
            std::string callers;
            for (const uintptr_t caller : addresses) {
              if (caller == 0) break;
              // Found: ScanCallers took it inside one of these sections.
              const auto code = std::ranges::find_if(trace.code, [caller](const internal::MapCode& range) {
                return caller > range.begin && caller <= range.end;
              });
              const auto& module = trace.modules[code->module];
              callers += std::format("{}{}+0x{:x}", (callers.empty() ? "" : ","), module.name, caller - module.base);
            }
            return std::format(" callers={}\n", (callers.empty() ? std::string("-") : callers));
          };
          // Each thread's last traced event: its end and its kind (where the next one's gap starts).
          std::unordered_map<uint32_t, std::pair<int64_t, const char*>> thread_last;
          for (size_t index = 0; index < trace.events.size(); ++index) {
            write_part(false);
            const auto& event = trace.events[index];
            const size_t frame = frame_of(event.qpc);
            const auto ms_of = [&](int64_t qpc) { return static_cast<double>(qpc - trace.presents[frame]) * ms_per_tick; };
            std::pair<int64_t, const char*> previous = {0, "-"};
            if (const auto last = thread_last.find(event.thread); last != thread_last.end()) {
              previous = last->second;
            }
            thread_last[event.thread] = {event.done, EVENT_KINDS[static_cast<size_t>(event.kind)]};
            uint32_t draw_index = 0;
            std::string last_draw = "-";
            if (const auto draws = thread_draws.find(event.thread); draws != thread_draws.end()) {
              const auto& list = draws->second;
              const auto draw_qpc = [](const internal::MapDraw* draw) { return draw->qpc; };
              const auto after = std::ranges::upper_bound(list, event.qpc, {}, draw_qpc);
              const auto frame_start = std::ranges::lower_bound(list, trace.presents[frame], {}, draw_qpc);
              draw_index = static_cast<uint32_t>(after - frame_start);
              if (after != list.begin()) {
                const auto& draw = **(after - 1);
                if (draw.done > previous.first) {
                  previous = {draw.done, "draw"};
                }
                const char* draw_kind = DRAW_KINDS[static_cast<size_t>(draw.kind)];
                last_draw = (internal::IsDispatch(draw.kind)
                                 ? std::format("{}/cs:{:08X}", draw_kind, draw.vertex_shader)
                                 : std::format("{}/vs:{:08X}/ps:{:08X}", draw_kind, draw.vertex_shader, draw.pixel_shader));
              }
            }
            // A matched unmap is its map's unmap_ms (still the next event's previous one).
            if (event.kind == internal::MapEventKind::UNMAP && unmap_matched[index]) continue;
            text += std::format(
                "{} frame={} qpc={} ms={:.3f} gap_ms={} gap_from={} thread={} draw={} last={}",
                EVENT_KINDS[static_cast<size_t>(event.kind)], frame, event.qpc, ms_of(event.qpc),
                (previous.first != 0 ? std::format("{:.3f}", static_cast<double>(event.qpc - previous.first) * ms_per_tick)
                                     : std::string("-")),
                previous.second, event.thread, draw_index, last_draw);
            switch (event.kind) {
              case internal::MapEventKind::MAP: {
                const char* access = "?";
                switch (event.access) {
                  case reshade::api::map_access::read_only:
                    access = "read";
                    break;
                  case reshade::api::map_access::write_only:
                    // D3D11's WRITE_NO_OVERWRITE (a dynamic resource) and WRITE (a staging one) both arrive as write_only.
                    access = (event.resource.usage == D3D11_USAGE_DYNAMIC ? "no_overwrite" : "write");
                    break;
                  case reshade::api::map_access::read_write:
                    access = "read_write";
                    break;
                  case reshade::api::map_access::write_discard:
                    access = "discard";
                    break;
                }
                const size_t unmap = unmap_of[index];
                text += std::format(
                    " access={} sub={} unmap_ms={}", access, event.subresource,
                    (unmap != SIZE_MAX ? std::format("{:.3f}", ms_of(trace.events[unmap].qpc)) : std::string("-")));
                text += describe("", event.resource);
                break;
              }
              case internal::MapEventKind::UNMAP:
                text += std::format(" sub={} res=0x{:x}", event.subresource, event.resource.pointer);
                break;
              case internal::MapEventKind::UPDATE:
                text += std::format(" data_bytes={}", event.bytes);
                text += describe("", event.resource);
                break;
              case internal::MapEventKind::COPY:
                text += std::format(" data_bytes={}", event.bytes);
                text += describe("", event.resource);
                text += describe("src_", event.source);
                break;
            }
            text += callers_text(event.callers);
          }
          // The native calls (the hook's): each map with its unmap (the next one of its resource and subresource),
          // the unmaps without a map in the trace, then each frame's totals by class.
          constexpr std::array<const char*, 5> CLASSES = {"constant", "vertex", "index", "other", "texture"};
          constexpr std::array<const char*, 6> MAP_TYPES = {"?", "read", "write", "read_write", "discard", "no_overwrite"};
          struct ClassTotals {
            uint32_t maps = 0;
            int64_t ticks = 0;
            uint32_t discards = 0;
            uint64_t discard_bytes = 0;
          };
          struct NativeTotals {
            uint32_t maps = 0;
            uint32_t failed = 0;
            uint32_t unmaps = 0;
            int64_t map_ticks = 0;
            int64_t unmap_ticks = 0;
            std::array<uint32_t, 6> types = {};
            uint64_t discard_bytes = 0;
            std::array<ClassTotals, 5> classes = {};
          };
          std::vector<NativeTotals> native_totals(frames);
          std::vector<size_t> native_unmap_of(trace.native_calls.size(), SIZE_MAX);
          std::vector<bool> native_unmap_matched(trace.native_calls.size(), false);
          std::map<std::pair<uintptr_t, uint32_t>, size_t> native_open;
          for (size_t index = 0; index < trace.native_calls.size(); ++index) {
            const auto& call = trace.native_calls[index];
            const auto key = std::pair{call.resource.pointer, call.subresource};
            if (call.unmap) {
              if (const auto open = native_open.find(key); open != native_open.end()) {
                native_unmap_of[open->second] = index;
                native_unmap_matched[index] = true;
                native_open.erase(open);
              }
            } else if (SUCCEEDED(call.result)) {
              native_open[key] = index;
            }
          }
          // Direct callers by module (ReShade's proxy, an add-on), named once each.
          std::unordered_map<uintptr_t, std::string> direct_names;
          const auto direct_text = [&direct_names, &module_name](uintptr_t address) {
            HMODULE module = nullptr;
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(address), &module)
                == FALSE) {
              return std::format("0x{:x}", address);
            }
            const auto base = reinterpret_cast<uintptr_t>(module);
            auto [named, added] = direct_names.try_emplace(base);
            if (added) {
              named->second = module_name(module);
            }
            return std::format("{}+0x{:x}", named->second, address - base);
          };
          for (size_t index = 0; index < trace.native_calls.size(); ++index) {
            write_part(false);
            const auto& call = trace.native_calls[index];
            const size_t frame = frame_of(call.qpc);
            const auto ms_of = [&](int64_t qpc) { return static_cast<double>(qpc - trace.presents[frame]) * ms_per_tick; };
            auto& totals = native_totals[frame];
            if (call.unmap) {
              ++totals.unmaps;
              totals.unmap_ticks += call.done - call.qpc;
              if (native_unmap_matched[index]) continue;
              text += std::format("native_unmap frame={} qpc={} ms={:.3f} native_ms={:.4f} thread={} sub={} res=0x{:x}"
                                  " direct={}",
                                  frame, call.qpc, ms_of(call.qpc), static_cast<double>(call.done - call.qpc) * ms_per_tick,
                                  call.thread, call.subresource, call.resource.pointer, direct_text(call.direct_caller));
              text += callers_text(call.callers);
              continue;
            }
            // Its class from its description: a buffer by its bind flags (constant first, then vertex, then index).
            size_t map_class = 4;
            if (call.resource.dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
              if ((call.resource.bind_flags & D3D11_BIND_CONSTANT_BUFFER) != 0) {
                map_class = 0;
              } else if ((call.resource.bind_flags & D3D11_BIND_VERTEX_BUFFER) != 0) {
                map_class = 1;
              } else if ((call.resource.bind_flags & D3D11_BIND_INDEX_BUFFER) != 0) {
                map_class = 2;
              } else {
                map_class = 3;
              }
            }
            const auto map_type_index = static_cast<size_t>(call.map_type);
            const size_t map_type = (map_type_index < MAP_TYPES.size() ? map_type_index : 0);
            const int64_t ticks = call.done - call.qpc;
            auto& class_totals = totals.classes[map_class];
            ++totals.maps;
            ++class_totals.maps;
            totals.map_ticks += ticks;
            class_totals.ticks += ticks;
            if (FAILED(call.result)) {
              ++totals.failed;
            } else {
              ++totals.types[map_type];
              if (call.map_type == D3D11_MAP_WRITE_DISCARD) {
                totals.discard_bytes += call.mapped_bytes;
                ++class_totals.discards;
                class_totals.discard_bytes += call.mapped_bytes;
              }
            }
            std::string unmap_ms = "-";
            std::string unmap_native_ms = "-";
            if (const size_t unmap = native_unmap_of[index]; unmap != SIZE_MAX) {
              const auto& unmap_call = trace.native_calls[unmap];
              unmap_ms = std::format("{:.3f}", ms_of(unmap_call.qpc));
              unmap_native_ms = std::format("{:.4f}", static_cast<double>(unmap_call.done - unmap_call.qpc) * ms_per_tick);
            }
            text += std::format(
                "native_map frame={} qpc={} done_qpc={} ms={:.3f} native_ms={:.4f} thread={} map={} map_flags=0x{:x} hr=0x{:08x}"
                " sub={} map_bytes={} class={} unmap_ms={} unmap_native_ms={} direct={}",
                frame, call.qpc, call.done, ms_of(call.qpc), static_cast<double>(ticks) * ms_per_tick, call.thread,
                MAP_TYPES[map_type], call.map_flags, static_cast<uint32_t>(call.result), call.subresource, call.mapped_bytes,
                CLASSES[map_class], unmap_ms, unmap_native_ms, direct_text(call.direct_caller));
            text += describe("", call.resource);
            text += callers_text(call.callers);
          }
          for (size_t frame = 0; frame < frames; ++frame) {
            const auto& totals = native_totals[frame];
            text += std::format(
                "native_frame index={} maps={} failed={} unmaps={} native_ms={:.4f} unmap_native_ms={:.4f} reads={}"
                " writes={} read_writes={} discards={} no_overwrites={} discard_bytes={}",
                frame, totals.maps, totals.failed, totals.unmaps, static_cast<double>(totals.map_ticks) * ms_per_tick,
                static_cast<double>(totals.unmap_ticks) * ms_per_tick, totals.types[1], totals.types[2], totals.types[3],
                totals.types[4], totals.types[5], totals.discard_bytes);
            for (size_t map_class = 0; map_class < CLASSES.size(); ++map_class) {
              const auto& class_totals = totals.classes[map_class];
              text += std::format(" {0}_maps={1} {0}_ms={2:.4f} {0}_discards={3} {0}_discard_bytes={4}", CLASSES[map_class],
                                  class_totals.maps, static_cast<double>(class_totals.ticks) * ms_per_tick,
                                  class_totals.discards, class_totals.discard_bytes);
            }
            text += '\n';
          }
          const bool written = close_file();
          // tw_map_trace_v3.txt, the context hooks' stream. Its lines:
          //   v3 setting= qpc_frequency= frames= end_qpc= hook= native_calls= dropped_native_calls= binds=
          //     dropped_binds= draws= dropped_draws= buffers= dropped_buffers= first_drop_seq= hash= hash_ms=
          //     hook: on, no_context, failed, foreign_thread (a call from another thread: pass-through from then on);
          //     first_drop_seq: the seq from which a native call or bind was dropped (-: none); hash: on with the
          //     setting's third field `hash`; hash_ms: each frame's time in the unmaps' content hashes (render thread,
          //     outside native_ms), comma-separated.
          //   present qpc=: each traced frame's start, then the last one's end.
          //   buffer create_qpc= thread= destroy_qpc= destroy_thread= res= usage= bind= cpu= misc= type= bytes=: a
          //     constant buffer made while armed (destroy_qpc=0 destroy_thread=0: not destroyed while armed).
          // Then the render thread's stream in call order (seq=), each line with seq= frame= qpc= ms= (since its frame's
          // start); the hooks' calls also native_ms= nested= (d3d11's own hook entries inside the call, not recorded):
          //   map res= sub= map= flags= hr= bytes=
          //   unmap res= sub= content=: a constant buffer written through its map, its bytes' FNV-1a hash (-: none or
          //     hashing off)
          //   seed_cb stage= slots=, seed_vb slots=: the bindings held when the hooks went in
          //   set stage= slots=, set1 stage= slots=: <slot>:<buffer>[@<first constant>+<constants>], comma-separated
          //     (no window: the whole buffer; 0x0: null)
          //   vb slots=: <slot>:<buffer>/<stride>/<offset>
          //   swap state= previous=; clear
          //   draw kind= index= vs= ps= args= indirect= state= cbs= vbs=: index in its frame, a dispatch's cs in vs=, args as
          //     MapDraw's (indexed: the base vertex as int32 bits); state: the device context state its bindings belong to
          //     (0x0: the one active when the hooks went in); cbs (a draw's VS..PS, a dispatch's CS) and vbs (a draw's): the
          //     bindings changed since the render thread's draw before (the first draw's: every one bound), as
          //     <stage><slot>:<buffer>[@<first>+<count>][#<content>] (the buffer's last hashed unmap; an UpdateSubresource is
          //     not seen) and <slot>:<buffer>/<stride>/<offset>; <stage><slot>:? and <slot>:?: not known (bound before the
          //     hooks or in a state not seen yet); -: none changed. From first_drop_seq on, and after the foreign
          //     thread's call, cbs=? vbs=?: a dropped or passed-on bind makes every later binding unknown.
          open_file(L"tw_map_trace_v3.txt");
          std::vector<int64_t> hash_ticks(frames, 0);
          for (const auto& call : trace.native_calls) {
            hash_ticks[frame_of(call.qpc)] += call.hash_ticks;
          }
          std::string hash_ms;
          for (const int64_t ticks : hash_ticks) {
            hash_ms += std::format("{}{:.4f}", (hash_ms.empty() ? "" : ","), static_cast<double>(ticks) * ms_per_tick);
          }
          text = std::format(
              "v3 setting={} qpc_frequency={} frames={} end_qpc={} hook={} native_calls={} dropped_native_calls={}"
              " binds={} dropped_binds={} draws={} dropped_draws={} buffers={} dropped_buffers={} first_drop_seq={}"
              " hash={} hash_ms={}\n",
              trace.setting, frequency.QuadPart, frames, trace.presents.back(), trace.native_hook,
              trace.native_calls.size(), trace.dropped_native_calls, trace.binds.size(), trace.dropped_binds,
              trace.draws.size(), trace.dropped_draws, trace.buffers.size(), trace.dropped_buffers,
              (trace.first_drop == UINT64_MAX ? std::string("-") : std::to_string(trace.first_drop)),
              (trace.hash_contents ? "on" : "off"), hash_ms);
          for (const int64_t present : trace.presents) {
            text += std::format("present qpc={}\n", present);
          }
          for (const auto& buffer : trace.buffers) {
            text += std::format("buffer create_qpc={} thread={} destroy_qpc={} destroy_thread={}", buffer.qpc,
                                buffer.thread, buffer.destroy_qpc, buffer.destroy_thread);
            text += describe("", buffer.resource);
            text += '\n';
          }
          constexpr std::array<const char*, context_hooks::STAGES> STAGE_NAMES = {"vs", "hs", "ds", "gs", "ps", "cs"};
          constexpr std::array<const char*, 9> CALL_KINDS = {"map",   "unmap", "set",     "set1",   "vb",
                                                             "swap",  "clear", "seed_cb", "seed_vb"};
          // Indexed by context_hooks' Stage and Kind.
          static_assert(static_cast<size_t>(context_hooks::Stage::COMPUTE_SHADER) + 1 == STAGE_NAMES.size());
          static_assert(static_cast<size_t>(context_hooks::Kind::SEED_VERTEX_BUFFERS) + 1 == CALL_KINDS.size());
          static_assert(std::string_view(CALL_KINDS[static_cast<size_t>(context_hooks::Kind::SWAP_STATE)]) == "swap");
          // A device context state's bindings as the stream sets them (nullopt: not known).
          struct Bindings {
            std::array<std::array<std::optional<internal::BindSlot>, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT>,
                       context_hooks::STAGES>
                constant_buffers;
            std::array<std::optional<internal::BindSlot>, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertex_buffers;
          };
          Bindings cleared;
          for (auto& stage : cleared.constant_buffers) {
            stage.fill(internal::BindSlot{});
          }
          cleared.vertex_buffers.fill(internal::BindSlot{});
          std::map<uintptr_t, Bindings> states;
          uintptr_t active = 0;
          // The render thread's draw before: its bindings and constant buffer contents.
          Bindings drawn = cleared;
          std::array<std::array<std::optional<uint64_t>, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT>,
                     context_hooks::STAGES>
              drawn_contents = {};
          std::unordered_map<uintptr_t, uint64_t> contents;  // a buffer's last hashed unmap
          std::vector<uint32_t> frame_draws(frames, 0);
          const uint32_t render_thread = GetCurrentThreadId();
          // A call from another thread switched the hooks to pass-through at this time (0: none).
          const int64_t foreign_qpc = context_hooks::internal::state.foreign_qpc.load(std::memory_order_relaxed);
          const auto slot_text = [](size_t slot, const internal::BindSlot& bound, bool vertex) {
            if (vertex) {
              return std::format("{}:0x{:x}/{}/{}", slot, bound.buffer, bound.first_or_stride, bound.count_or_offset);
            }
            if (bound.count_or_offset == 0) return std::format("{}:0x{:x}", slot, bound.buffer);
            return std::format("{}:0x{:x}@{}+{}", slot, bound.buffer, bound.first_or_stride, bound.count_or_offset);
          };
          const auto call_line = [&](context_hooks::Kind kind, uint64_t sequence, int64_t qpc, int64_t done,
                                     uint32_t nested) {
            const size_t frame = frame_of(qpc);
            text += std::format("{} seq={} frame={} qpc={} ms={:.3f} native_ms={:.4f} nested={}",
                                CALL_KINDS[static_cast<size_t>(kind)], sequence, frame, qpc,
                                static_cast<double>(qpc - trace.presents[frame]) * ms_per_tick,
                                static_cast<double>(done - qpc) * ms_per_tick, nested);
          };
          size_t next_call = 0;
          size_t next_bind = 0;
          size_t next_draw = 0;
          while (true) {
            write_part(false);
            const uint64_t call_sequence =
                (next_call < trace.native_calls.size() ? trace.native_calls[next_call].sequence : UINT64_MAX);
            const uint64_t bind_sequence =
                (next_bind < trace.binds.size() ? trace.binds[next_bind].sequence : UINT64_MAX);
            const uint64_t draw_sequence =
                (next_draw < trace.draws.size() ? trace.draws[next_draw].sequence : UINT64_MAX);
            if (call_sequence == UINT64_MAX && bind_sequence == UINT64_MAX && draw_sequence == UINT64_MAX) break;
            if (call_sequence < bind_sequence && call_sequence < draw_sequence) {
              const auto& call = trace.native_calls[next_call++];
              call_line((call.unmap ? context_hooks::Kind::UNMAP : context_hooks::Kind::MAP), call.sequence, call.qpc,
                        call.done, call.nested);
              if (!call.unmap) {
                const auto map_type = static_cast<size_t>(call.map_type);
                text += std::format(" res=0x{:x} sub={} map={} flags=0x{:x} hr=0x{:08x} bytes={}\n",
                                    call.resource.pointer, call.subresource,
                                    MAP_TYPES[map_type < MAP_TYPES.size() ? map_type : 0], call.map_flags,
                                    static_cast<uint32_t>(call.result), call.mapped_bytes);
              } else if (call.content_known) {
                text += std::format(" res=0x{:x} sub={} content={:016x}\n", call.resource.pointer, call.subresource,
                                    call.content);
                contents[call.resource.pointer] = call.content;
              } else {
                text += std::format(" res=0x{:x} sub={} content=-\n", call.resource.pointer, call.subresource);
                contents.erase(call.resource.pointer);
              }
            } else if (bind_sequence < draw_sequence) {
              const auto& bind = trace.binds[next_bind++];
              call_line(bind.kind, bind.sequence, bind.qpc, bind.done, bind.nested);
              switch (bind.kind) {
                case context_hooks::Kind::SWAP_STATE:
                  text += std::format(" state=0x{:x} previous=0x{:x}\n", bind.state, bind.previous_state);
                  states[bind.previous_state] = states[active];
                  active = bind.state;
                  break;
                case context_hooks::Kind::CLEAR_STATE:
                  text += '\n';
                  states[active] = cleared;
                  break;
                default: {
                  const bool vertex = (bind.kind == context_hooks::Kind::SET_VERTEX_BUFFERS
                                       || bind.kind == context_hooks::Kind::SEED_VERTEX_BUFFERS);
                  const auto stage = static_cast<size_t>(bind.stage);
                  auto& bindings = states[active];
                  std::string slots;
                  for (uint32_t index = 0; index < bind.count; ++index) {
                    const auto& bound = trace.bind_slots[bind.first_slot + index];
                    const size_t slot = bind.start_slot + index;
                    slots += (slots.empty() ? "" : ",") + slot_text(slot, bound, vertex);
                    auto& target = (vertex ? bindings.vertex_buffers[slot] : bindings.constant_buffers[stage][slot]);
                    target = bound;
                  }
                  if (slots.empty()) {
                    slots = "-";
                  }
                  text += (vertex ? std::format(" slots={}\n", slots)
                                  : std::format(" stage={} slots={}\n", STAGE_NAMES[stage], slots));
                  break;
                }
              }
            } else {
              const auto& draw = trace.draws[next_draw++];
              if (draw.thread != render_thread) continue;
              const size_t frame = frame_of(draw.qpc);
              const bool compute = internal::IsDispatch(draw.kind);
              const bool lost = (draw.sequence >= trace.first_drop || (foreign_qpc != 0 && draw.qpc >= foreign_qpc));
              const auto& bindings = states[active];
              std::string constant_buffers = (lost ? "?" : "");
              for (size_t stage = (compute ? context_hooks::STAGES - 1 : 0);
                   !lost && stage < (compute ? context_hooks::STAGES : context_hooks::STAGES - 1); ++stage) {
                for (size_t slot = 0; slot < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++slot) {
                  const auto& bound = bindings.constant_buffers[stage][slot];
                  std::optional<uint64_t> content;
                  if (const auto found = (bound.has_value() ? contents.find(bound->buffer) : contents.end());
                      found != contents.end()) {
                    content = found->second;
                  }
                  if (bound == drawn.constant_buffers[stage][slot] && content == drawn_contents[stage][slot]) continue;
                  drawn.constant_buffers[stage][slot] = bound;
                  drawn_contents[stage][slot] = content;
                  constant_buffers += std::format(
                      "{}{}{}{}", (constant_buffers.empty() ? "" : ","), STAGE_NAMES[stage],
                      (bound.has_value() ? slot_text(slot, *bound, false) : std::format("{}:?", slot)),
                      (content.has_value() ? std::format("#{:016x}", *content) : std::string()));
                }
              }
              std::string vertex_buffers = (lost ? "?" : "");
              for (size_t slot = 0; !lost && !compute && slot < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++slot) {
                const auto& bound = bindings.vertex_buffers[slot];
                if (bound == drawn.vertex_buffers[slot]) continue;
                drawn.vertex_buffers[slot] = bound;
                vertex_buffers += std::format(
                    "{}{}", (vertex_buffers.empty() ? "" : ","),
                    (bound.has_value() ? slot_text(slot, *bound, true) : std::format("{}:?", slot)));
              }
              text += std::format(
                  "draw seq={} frame={} qpc={} ms={:.3f} kind={} index={} vs={:08X} ps={:08X} args={},{},{},{},{}"
                  " indirect=0x{:x} state=0x{:x} cbs={} vbs={}\n",
                  draw.sequence, frame, draw.qpc, static_cast<double>(draw.qpc - trace.presents[frame]) * ms_per_tick,
                  DRAW_KINDS[static_cast<size_t>(draw.kind)], frame_draws[frame]++, draw.vertex_shader,
                  draw.pixel_shader, draw.args[0], draw.args[1], draw.args[2], draw.args[3], draw.args[4],
                  draw.indirect_buffer, active,
                  (constant_buffers.empty() ? std::string("-") : constant_buffers),
                  (vertex_buffers.empty() ? std::string("-") : vertex_buffers));
            }
          }
          if (close_file() && written) {
            renodx::utils::log::i("[Total War] Map trace: ", frames, " frames, ", trace.draws.size(), " draws, ",
                                  trace.events.size(), " events, ", trace.native_calls.size(), " native calls, ",
                                  trace.binds.size(),
                                  " binds saved to tw_map_trace.txt and tw_map_trace_v3.txt next to the exe (", parts,
                                  " parts).");
          } else {
            renodx::utils::log::w("[Total War] Map trace: tw_map_trace.txt or tw_map_trace_v3.txt next to the exe could not be"
                                  " written.");
          }
        } catch (const std::exception& error) {
          renodx::utils::log::w("[Total War] Map trace: not written (", error.what(), ").");
        }
        trace.draws = {};
        trace.events = {};
        trace.native_calls = {};
        trace.binds = {};
        trace.bind_slots = {};
        trace.buffers = {};
        trace.live_buffers = {};
      }
    }
  }
  bool counted = scaled || (state.options.at_100 && view_3d);
  if (state.options.on_demand) {
    ++state.presents;
    if (state.presents % 60 == 0 && DeleteFileW((state.folder / L"renodx_trace_now").c_str()) != 0) {
      const std::lock_guard lock(state.mutex);
      state.wanted = true;
      state.scaled_presents = internal::SCALED_PRESENTS - 1;
      counted = true;
    }
  }
  const auto save = [&] {
    internal::UseEvents(false);
    const auto path = state.folder / L"tw_frame_trace.txt";
    const std::lock_guard lock(state.mutex);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << state.text;
    renodx::utils::log::i("[Total War] Frame trace: ", state.lines, " lines saved to ", path.string());
    state.text.clear();
  };
  if (state.reads && state.next_frame.exchange(false)) {
    save();
    return;
  }
  if (state.tracing.exchange(false)) {
    if (state.reads) {
      internal::Append("NEXT FRAME: the draw reads (FrameTraceReads)");
      state.next_frame.store(true);
      return;
    }
    save();
    return;
  }
  bool start = false;
  {
    const std::lock_guard lock(state.mutex);
    if (!state.wanted) return;
    state.scaled_presents = counted ? state.scaled_presents + 1 : 0;
    if (state.scaled_presents >= internal::SCALED_PRESENTS) {
      state.wanted = false;
      state.text = "One frame at the DLSS render scale: draws, dispatches, copies (vs / ps / cs, targets, viewport, inputs, camera).\n";
      state.lines = 0;
      state.values_written.clear();
      state.blends_written.clear();
      state.indirect_values_written = false;
      start = true;
    }
  }
  if (start) {
    internal::UseEvents(true);
    state.tracing.store(true);
  }
}

}  // namespace twattila::frame_trace
