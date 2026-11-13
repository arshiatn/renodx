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
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/data.hpp"
#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"
#include "../../utils/state.hpp"

// Debug (ReShade.ini [renodx] FrameTrace=1, read at start): one frame, 600 presents after the DLSS render scale began
// (Options' at_100: after the 3D view began, also at 100%), or the next frame when renodx_trace_now sits next to the exe
// (Options' on_demand), every draw, dispatch (direct and indirect), clear and copy with its shaders, targets, viewport,
// inputs and the camera's screen / viewport sizes (the camera at b0, its registers from Install) into tw_frame_trace.txt
// next to the exe, with the anti-aliasing's marks (tone mapper, DLSS). For the render scale audit
// (tools\tw\scale_audit.py): which passes read the 3D image's part of a display-size texture as if it filled it. Its
// events are registered only for the traced frame (with FrameTraceReads=1 also the frame after it, and the input layouts'
// from the start).
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
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  const std::lock_guard lock(internal::state.mutex);
  internal::state.wanted = wanted;
  internal::state.reads = reads;
  internal::state.options = std::move(options);
  internal::state.folder = std::filesystem::path(exe).parent_path();
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
// presents (at_100: of the 3D view), or at once when the marker is there (on_demand).
inline void OnPresent(bool scaled, bool view_3d = false) {
  auto& state = internal::state;
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
