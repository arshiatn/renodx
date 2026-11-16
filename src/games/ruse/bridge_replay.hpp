/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <include/reshade.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <map>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../utils/data.hpp"
#include "../../utils/device_proxy.hpp"
#include "../../utils/state.hpp"
#include "../twattila/rt_core.hpp"
#include "./bridge.hpp"
#include "./replay_shaders.h"

// R.U.S.E.'s draw replay (tools/nontw/ruse/plan.md 10b A1, A3, A4): the game's scene draws again on the DX11 proxy's
// device, each through its vertex shader transpiled to vs_5_0 (replay_shaders.h), the clip positions streamed out:
// the geometry ray tracing needs (A5 gives it to rt_core). A1: every buffer the game fills (map / unmap) mirrored on the
// CPU and uploaded to a D3D11 buffer the first time a replayed draw reads it after a change. A3: the game's vertex
// declarations as D3D11 input layouts, one per declaration and shader, the integer formats ReShade reports read
// normalized and scaled back (the shaders' cbuffer b1). A4: each scene draw recorded (shader, declaration, streams,
// indices, the vertex constants it reads), replayed at the game's present into one stream output buffer. A rigid draw
// seen at its place in an earlier frame (its placement) isn't replayed again: about 1,800 of the B0 trace's 2,200 scene
// draws a frame (tools/nontw/ruse/scripts/ru_trace_static.py), so a frame replays its skinned and moved draws alone.
namespace ruse::replay {

using Microsoft::WRL::ComPtr;

constexpr UINT STREAM_OUTPUT_BYTES = 64u << 20;  // 4 M clip positions a frame
constexpr UINT STREAMS = 4;
// A placement's model is the camera pass's inverse view-projection (0x88292FC6's c4-c7) x the draw's c0-c3: that float
// inverse at world coordinates near 2e6 is a few units off, differently as the camera moves, so a placement keeps its
// place while its translation stays within PLACE_TOLERANCE world units; unseen for PLACE_RELEASE_FRAMES it is forgotten.
constexpr double PLACE_TOLERANCE = 32.0;
constexpr uint64_t PLACE_RELEASE_FRAMES = 600;
// CaptureScene's b2 slices (16 constants each), one a placement in turn: rt_core's key holds b1-b4 and their offsets.
constexpr UINT IDENTITY_SLOTS = 4096;

struct Placement {
  std::array<double, 3> translation = {};
  uint64_t frame = 0;      // the last frame a draw had it
  uint64_t scene_key = 0;  // ray tracing's key for its capture (0: not captured yet)
  uint32_t slot = 0;       // its b2 slice
};

struct Mirror {
  std::vector<uint8_t> bytes;
  uint64_t written = 0;   // the game's unmaps so far
  uint64_t uploaded = 0;  // the one `buffer` holds
  ComPtr<ID3D11Buffer> buffer;
};

struct Mapping {
  uint64_t offset = 0;
  uint64_t size = 0;
  void* data = nullptr;
};

struct Element {
  std::string semantic;
  uint32_t semantic_index = 0;
  uint32_t stream = 0;
  uint32_t offset = 0;
  reshade::api::format format = reshade::api::format::unknown;
};

// A vertex declaration; `serial` tells a declaration made again at a freed handle from the one before.
struct Declaration {
  std::vector<Element> elements;
  uint64_t serial = 0;
};

struct Layout {
  ComPtr<ID3D11InputLayout> layout;
  ComPtr<ID3D11Buffer> conversion;  // b1: input_scale[16], input_bias[16]
  uint32_t streams = 0;             // the streams its elements read (bits)
  uint64_t serial = 0;
  bool usable = false;
};

struct Draw {
  const ReplayShader* shader = nullptr;
  uint64_t declaration = 0;
  std::array<uint64_t, STREAMS> streams = {};
  std::array<uint32_t, STREAMS> offsets = {};
  std::array<uint32_t, STREAMS> strides = {};
  uint64_t index_buffer = 0;
  uint32_t index_offset = 0;
  uint32_t index_size = 2;
  uint32_t index_count = 0;
  uint32_t first_index = 0;
  int32_t vertex_offset = 0;
  uint32_t topology = 4;  // reshade::api::primitive_topology (= D3D11's for lists and strips)
  size_t constants = 0;   // into State::constants, `shader->registers` x 4 floats
  // Its placement (State::placements[place_key][place_index]; place_key 0: none, replayed every frame) and whether an
  // earlier frame had it there already.
  uint64_t place_key = 0;
  uint32_t place_index = 0;
  bool placed = false;
  bool skinned = false;  // bone inputs (BLENDINDICES): never placed, its own capture each frame
};

// Why recorded draws were not replayed (logged with Trace One Frame).
struct Counts {
  uint32_t recorded = 0;
  uint32_t placed = 0;
  uint32_t replayed = 0;
  uint32_t no_shader = 0;
  uint32_t no_layout = 0;
  uint32_t no_buffer = 0;
  uint32_t fan = 0;
};

struct State {
  bool enabled = false;  // the Scene Replay row (restart)
  bool rays = false;     // ray_tracing.hpp's row (restart): the draws go to the next frame's trace (CaptureScene)
  bool log_next = false;
  bool log_capture = false;
  renodx::utils::data::ParallelNodeHashMap<uint64_t, Mirror, std::shared_mutex> mirrors;
  renodx::utils::data::ParallelFlatHashMap<uint64_t, Mapping, std::shared_mutex> mappings;
  renodx::utils::data::ParallelNodeHashMap<uint64_t, Declaration, std::shared_mutex> declarations;
  std::atomic<uint64_t> declarations_made = 0;
  // The render thread's: the bound declaration, streams, indices and topology, the game's vertex float constants.
  uint64_t declaration = 0;
  std::array<uint64_t, STREAMS> streams = {};
  std::array<uint32_t, STREAMS> offsets = {};
  std::array<uint32_t, STREAMS> strides = {};
  uint64_t index_buffer = 0;
  uint32_t index_offset = 0;
  uint32_t index_size = 2;
  uint32_t topology = 4;
  std::array<float, 1024> vertex_constants = {};
  std::vector<Draw> draws;
  std::vector<float> constants;
  Counts counts;
  uint64_t frame = 0;
  std::unordered_map<uint64_t, std::vector<Placement>> placements;
  uint32_t next_slot = 0;
  // The last frame's draws, constants and inverse view-projection, for this frame's trace (rays).
  std::vector<Draw> previous;
  std::vector<float> previous_constants;
  std::array<float, 16> previous_inverse = {};
  bool previous_valid = false;
  // The proxy's objects (made on the first replay).
  std::unordered_map<uint32_t, std::pair<ComPtr<ID3D11VertexShader>, ComPtr<ID3D11GeometryShader>>> shaders;
  std::map<std::pair<uint64_t, uint32_t>, Layout> layouts;  // by declaration and shader hash
  ComPtr<ID3D11Buffer> game_constants;  // b0: c0-c255
  ComPtr<ID3D11Buffer> positions;      // the stream output
  ComPtr<ID3D11Query> statistics;
  ComPtr<ID3D11Buffer> identity;                   // CaptureScene's b2 (never read)
  ComPtr<ID3D11DepthStencilView> capture_depth;    // bound for rt_core's test of the main view (nothing drawn into it)
};
inline State state;

// A1: the game's writes (map_buffer_region fires after Lock with its pointer, unmap before Unlock).
inline void OnMapBuffer(reshade::api::device*, reshade::api::resource resource, uint64_t offset, uint64_t size,
                        reshade::api::map_access access, void** data) {
  if (access == reshade::api::map_access::read_only || data == nullptr || *data == nullptr) return;
  state.mappings.insert_or_assign(resource.handle, Mapping{.offset = offset, .size = size, .data = *data});
}

inline void OnUnmapBuffer(reshade::api::device* device, reshade::api::resource resource) {
  Mapping mapping;
  if (!state.mappings.erase_if(resource.handle, [&](const auto& entry) {
        mapping = entry.second;
        return true;
      })) {
    return;
  }
  const uint64_t buffer_size = device->get_resource_desc(resource).buffer.size;
  const uint64_t size = (mapping.size == 0 || mapping.size == UINT64_MAX) ? (buffer_size - mapping.offset) : mapping.size;
  if (mapping.offset + size > buffer_size) return;
  const auto write = [&](Mirror* mirror) {
    mirror->bytes.resize(buffer_size);
    std::memcpy(mirror->bytes.data() + mapping.offset, mapping.data, size);
    ++mirror->written;
  };
  state.mirrors.lazy_emplace_l(
      resource.handle,
      [&](auto& entry) {
        auto& [handle, mirror] = entry;
        write(&mirror);
      },
      [&](const auto& constructor) {
        Mirror mirror;
        write(&mirror);
        constructor(resource.handle, std::move(mirror));
      });
}

inline void OnDestroyResource(reshade::api::device*, reshade::api::resource resource) {
  state.mirrors.erase(resource.handle);
  state.mappings.erase(resource.handle);
}

// A3: the game's vertex declarations (ReShade's input_layout subobjects), their semantics copied.
inline void OnInitPipeline(reshade::api::device*, reshade::api::pipeline_layout, uint32_t subobject_count,
                           const reshade::api::pipeline_subobject* subobjects, reshade::api::pipeline pipeline) {
  for (uint32_t index = 0; index < subobject_count; ++index) {
    if (subobjects[index].type != reshade::api::pipeline_subobject_type::input_layout) continue;
    const auto* elements = static_cast<const reshade::api::input_element*>(subobjects[index].data);
    std::vector<Element> copied;
    for (uint32_t element = 0; element < subobjects[index].count; ++element) {
      copied.push_back({
          .semantic = (elements[element].semantic != nullptr) ? elements[element].semantic : "",
          .semantic_index = elements[element].semantic_index,
          .stream = elements[element].buffer_binding,
          .offset = elements[element].offset,
          .format = elements[element].format,
      });
    }
    state.declarations.insert_or_assign(pipeline.handle, Declaration{.elements = std::move(copied), .serial = ++state.declarations_made});
  }
}

inline void OnDestroyPipeline(reshade::api::device*, reshade::api::pipeline pipeline) {
  state.declarations.erase(pipeline.handle);
}

inline void OnBindPipeline(reshade::api::command_list*, reshade::api::pipeline_stage stages, reshade::api::pipeline pipeline) {
  if ((stages & reshade::api::pipeline_stage::input_assembler) == reshade::api::pipeline_stage::input_assembler) {
    state.declaration = pipeline.handle;
  }
}

inline void OnBindVertexBuffers(reshade::api::command_list*, uint32_t first, uint32_t count, const reshade::api::resource* buffers,
                                const uint64_t* offsets, const uint32_t* strides) {
  for (uint32_t index = 0; index < count && first + index < STREAMS; ++index) {
    state.streams[first + index] = buffers[index].handle;
    state.offsets[first + index] = static_cast<uint32_t>(offsets[index]);
    state.strides[first + index] = strides[index];
  }
}

inline void OnBindIndexBuffer(reshade::api::command_list*, reshade::api::resource buffer, uint64_t offset, uint32_t index_size) {
  state.index_buffer = buffer.handle;
  state.index_offset = static_cast<uint32_t>(offset);
  state.index_size = index_size;
}

inline void OnBindPipelineStates(reshade::api::command_list*, uint32_t count, const reshade::api::dynamic_state* states, const uint32_t* values) {
  for (uint32_t index = 0; index < count; ++index) {
    if (states[index] == reshade::api::dynamic_state::primitive_topology) state.topology = values[index];
  }
}

// ReShade's D3D9 global layout: parameter 2 the vertex float constants (offsets in floats).
inline void OnPushConstants(uint32_t layout_param, uint32_t first, uint32_t count, const void* values) {
  if (layout_param != 2 || first >= state.vertex_constants.size()) return;
  std::memcpy(state.vertex_constants.data() + first, values, std::min<size_t>(count, state.vertex_constants.size() - first) * sizeof(float));
}

// A4: a scene draw (the bridge's test: back-buffer-sized target, an INTZ depth) recorded with what it reads.
inline void Record(reshade::api::command_list* cmd_list, uint32_t index_count, uint32_t first_index, int32_t vertex_offset) {
  ++state.counts.recorded;
  const uint32_t hash = renodx::utils::state::GetCurrentShaderHash(cmd_list, reshade::api::pipeline_stage::vertex_shader);
  const auto found = std::ranges::lower_bound(SHADERS, hash, {}, &ReplayShader::hash);
  if (found == SHADERS.end() || found->hash != hash) {
    ++state.counts.no_shader;
    return;
  }
  const ReplayShader* shader = &*found;
  const size_t constants = state.constants.size();
  state.constants.insert(state.constants.end(), state.vertex_constants.begin(), state.vertex_constants.begin() + (shader->registers * 4));
  state.draws.push_back({
      .shader = shader,
      .declaration = state.declaration,
      .streams = state.streams,
      .offsets = state.offsets,
      .strides = state.strides,
      .index_buffer = state.index_buffer,
      .index_offset = state.index_offset,
      .index_size = state.index_size,
      .index_count = index_count,
      .first_index = first_index,
      .vertex_offset = vertex_offset,
      .topology = state.topology,
      .constants = constants,
  });
}

// The declaration's elements for `shader`'s inputs as a D3D11 input layout and b1's conversions (made once a pair).
inline Layout* GetLayout(ID3D11Device* device, const Draw& draw) {
  Declaration declaration;
  state.declarations.if_contains(draw.declaration, [&](const auto& entry) {
    const auto& [handle, declared] = entry;
    declaration = declared;
  });
  Layout& made = state.layouts[{draw.declaration, draw.shader->hash}];
  if (made.serial == declaration.serial && made.serial != 0) return &made;
  made = {.serial = declaration.serial};
  const std::vector<Element>& elements = declaration.elements;
  std::array<std::array<float, 4>, 32> conversion = {};
  for (auto& scale : std::span(conversion).first(16)) scale = {1.f, 1.f, 1.f, 1.f};
  std::vector<D3D11_INPUT_ELEMENT_DESC> descs;
  const auto shader_inputs = std::span(draw.shader->inputs).first(draw.shader->input_count);
  for (const auto& element : elements) {
    const auto input = std::ranges::find_if(shader_inputs, [&](const ShaderInput& shader_input) {
      return element.semantic == shader_input.semantic && element.semantic_index == shader_input.semantic_index;
    });
    if (input == shader_inputs.end()) continue;
    auto format = static_cast<DXGI_FORMAT>(element.format);
    std::array<float, 4> scale = {1.f, 1.f, 1.f, 1.f};
    std::array<float, 4> bias = {};
    switch (element.format) {
      case reshade::api::format::r8g8b8a8_uint:  // UBYTE4
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scale = {255.f, 255.f, 255.f, 255.f};
        break;
      case reshade::api::format::r16g16_sint:  // SHORT2: (x, y, 0, 1)
        format = DXGI_FORMAT_R16G16_SNORM;
        scale = {32767.f, 32767.f, 1.f, 1.f};
        break;
      case reshade::api::format::r16g16b16a16_sint:  // SHORT4
        format = DXGI_FORMAT_R16G16B16A16_SNORM;
        scale = {32767.f, 32767.f, 32767.f, 32767.f};
        break;
      case reshade::api::format::r10g10b10a2_uint:  // UDEC3: (x, y, z, 1)
        format = DXGI_FORMAT_R10G10B10A2_UNORM;
        scale = {1023.f, 1023.f, 1023.f, 0.f};
        bias = {0.f, 0.f, 0.f, 1.f};
        break;
      case reshade::api::format::r10g10b10a2_unorm:  // DEC3N (signed): no D3D11 format
        return &made;
      default:
        break;
    }
    made.streams |= 1u << element.stream;
    conversion[input->input_register] = scale;
    conversion[16 + input->input_register] = bias;
    descs.push_back({
        .SemanticName = input->semantic,
        .SemanticIndex = element.semantic_index,
        .Format = format,
        .InputSlot = element.stream,
        .AlignedByteOffset = element.offset,
        .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA,
    });
  }
  const D3D11_BUFFER_DESC desc = {.ByteWidth = sizeof(conversion), .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA data = {.pSysMem = conversion.data()};
  made.usable = !descs.empty()
                && SUCCEEDED(device->CreateInputLayout(descs.data(), static_cast<UINT>(descs.size()), draw.shader->code.data(),
                                                       draw.shader->code.size(), made.layout.GetAddressOf()))
                && SUCCEEDED(device->CreateBuffer(&desc, &data, made.conversion.GetAddressOf()));
  return &made;
}

// A mirrored buffer on the proxy, uploaded again when the game wrote it since (null: never written by the game).
inline ID3D11Buffer* GetBuffer(ID3D11Device* device, ID3D11DeviceContext* context, uint64_t handle) {
  ID3D11Buffer* result = nullptr;
  state.mirrors.modify_if(handle, [&](auto& entry) {
    auto& [key, mirror] = entry;
    if (mirror.buffer == nullptr || mirror.uploaded != mirror.written) {
      D3D11_BUFFER_DESC desc = {};
      if (mirror.buffer != nullptr) mirror.buffer->GetDesc(&desc);
      if (mirror.buffer == nullptr || desc.ByteWidth != mirror.bytes.size()) {
        const D3D11_BUFFER_DESC made = {.ByteWidth = static_cast<UINT>((mirror.bytes.size() + 15) & ~size_t{15}), .Usage = D3D11_USAGE_DEFAULT,
                                        .BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER};
        mirror.buffer.Reset();
        if (FAILED(device->CreateBuffer(&made, nullptr, mirror.buffer.GetAddressOf()))) return;
      }
      const D3D11_BOX box = {.left = 0, .top = 0, .front = 0, .right = static_cast<UINT>(mirror.bytes.size()), .bottom = 1, .back = 1};
      context->UpdateSubresource(mirror.buffer.Get(), 0, &box, mirror.bytes.data(), 0, 0);
      mirror.uploaded = mirror.written;
    }
    result = mirror.buffer.Get();
  });
  return result;
}

// A4 at the game's present: this frame's recorded draws, each rigid one given its placement (`inverse_view_projection`:
// the camera pass's c4-c7, rows; empty: unknown, none placed); those not placed in an earlier frame through their replay
// shaders into `positions` (triangles of clip positions in draw order), the proxy's state as it was after.
inline void Replay(std::span<const float> inverse_view_projection) {
  auto* proxy_reshade = renodx::utils::device_proxy::proxy_device_reshade;
  if (!state.enabled || proxy_reshade == nullptr || state.draws.empty()) {
    state.draws.clear();
    state.constants.clear();
    state.previous.clear();
    state.counts = {};
    return;
  }
  ++state.frame;
  const auto written = [](uint64_t handle) {
    uint64_t count = 0;
    state.mirrors.if_contains(handle, [&](const auto& entry) {
      const auto& [key, mirror] = entry;
      count = mirror.written;
    });
    return count;
  };
  for (auto& draw : state.draws) {
    const auto inputs = std::span(draw.shader->inputs).first(draw.shader->input_count);
    draw.skinned = std::ranges::any_of(inputs, [](const ShaderInput& input) { return std::strcmp(input.semantic, "BLENDINDICES") == 0; });
    if (draw.skinned || inverse_view_projection.size() != 16 || draw.shader->registers < 4) continue;
    // Its model's rows 0-2 (the inverse x its c0-c3, rows acting on column vectors), in doubles.
    const float* registers = state.constants.data() + draw.constants;
    std::array<double, 12> model = {};
    for (int row = 0; row < 3; ++row) {
      for (int column = 0; column < 4; ++column) {
        for (int k = 0; k < 4; ++k) {
          model[(row * 4) + column] += static_cast<double>(inverse_view_projection[(row * 4) + k]) * registers[(k * 4) + column];
        }
      }
    }
    uint64_t key = 14695981039346656037ull;
    const auto mix = [&key](uint64_t value) { key = (key ^ value) * 1099511628211ull; };
    mix(draw.shader->hash);
    mix(draw.declaration);
    for (UINT stream = 0; stream < STREAMS; ++stream) {
      mix(draw.streams[stream]);
      mix((uint64_t{draw.strides[stream]} << 32) | draw.offsets[stream]);
      mix(written(draw.streams[stream]));
    }
    mix(draw.index_buffer);
    mix(written(draw.index_buffer));
    mix((uint64_t{draw.index_offset} << 32) | draw.index_size);
    mix((uint64_t{draw.index_count} << 32) | draw.first_index);
    mix((uint64_t{static_cast<uint32_t>(draw.vertex_offset)} << 32) | draw.topology);
    for (int row = 0; row < 3; ++row) {
      for (int column = 0; column < 3; ++column) {
        mix(static_cast<uint64_t>(std::llround(model[(row * 4) + column] * 1024.0)));
      }
    }
    for (uint32_t index = 16; index < draw.shader->registers * 4u; ++index) mix(std::bit_cast<uint32_t>(registers[index]));
    const std::array<double, 3> translation = {model[3], model[7], model[11]};
    auto& candidates = state.placements[key];
    const auto nearby = [&](const Placement& placement) {
      return placement.frame != state.frame && std::abs(placement.translation[0] - translation[0]) <= PLACE_TOLERANCE
             && std::abs(placement.translation[1] - translation[1]) <= PLACE_TOLERANCE
             && std::abs(placement.translation[2] - translation[2]) <= PLACE_TOLERANCE;
    };
    auto found = std::ranges::find_if(candidates, nearby);
    draw.placed = found != candidates.end();
    if (draw.placed) {
      found->frame = state.frame;
      ++state.counts.placed;
    } else {
      // A new place, in a placement unseen for PLACE_RELEASE_FRAMES if the key has one.
      found = std::ranges::find_if(candidates, [](const Placement& placement) { return placement.frame + PLACE_RELEASE_FRAMES < state.frame; });
      if (found == candidates.end()) found = candidates.insert(candidates.end(), Placement{});
      *found = {.translation = translation, .frame = state.frame, .slot = (state.next_slot++) % IDENTITY_SLOTS};
    }
    draw.place_key = key;
    draw.place_index = static_cast<uint32_t>(found - candidates.begin());
  }
  if ((state.frame % 64) == 0) {
    std::erase_if(state.placements, [](const auto& entry) {
      const auto& [key, candidates] = entry;
      return std::ranges::all_of(candidates, [](const Placement& placement) { return placement.frame + PLACE_RELEASE_FRAMES < state.frame; });
    });
  }
  // With ray tracing the next frame's trace takes them (CaptureScene, in its camera): nothing replayed here.
  if (state.rays) {
    state.log_capture = state.log_next;
    state.log_next = false;
    state.previous_valid = inverse_view_projection.size() == 16;
    if (state.previous_valid) std::ranges::copy(inverse_view_projection, state.previous_inverse.begin());
    state.previous = std::move(state.draws);
    state.previous_constants = std::move(state.constants);
    state.draws.clear();
    state.constants.clear();
    state.counts = {};
    return;
  }
  auto* device = reinterpret_cast<ID3D11Device*>(proxy_reshade->get_native());
  ComPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(context.GetAddressOf());
  if (state.positions == nullptr) {
    const D3D11_BUFFER_DESC positions = {.ByteWidth = STREAM_OUTPUT_BYTES, .Usage = D3D11_USAGE_DEFAULT,
                                         .BindFlags = D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_SHADER_RESOURCE,
                                         .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS};
    const D3D11_BUFFER_DESC constants = {.ByteWidth = 256 * 16, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                         .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
    const D3D11_QUERY_DESC statistics = {.Query = D3D11_QUERY_SO_STATISTICS};
    if (FAILED(device->CreateBuffer(&positions, nullptr, state.positions.GetAddressOf()))
        || FAILED(device->CreateBuffer(&constants, nullptr, state.game_constants.GetAddressOf()))
        || FAILED(device->CreateQuery(&statistics, state.statistics.GetAddressOf()))) {
      reshade::log::message(reshade::log::level::warning, "[R.U.S.E.] Scene Replay: no stream output buffer: off");
      state.enabled = false;
      return;
    }
  }
  ID3D11Buffer* target = state.positions.Get();
  const UINT start = 0;
  context->SOSetTargets(1, &target, &start);
  context->PSSetShader(nullptr, nullptr, 0);
  ID3D11Buffer* const game_constants = state.game_constants.Get();
  if (state.log_next) context->Begin(state.statistics.Get());
  for (const auto& draw : state.draws) {
    if (draw.placed) continue;
    if (draw.topology != static_cast<uint32_t>(reshade::api::primitive_topology::triangle_list)
        && draw.topology != static_cast<uint32_t>(reshade::api::primitive_topology::triangle_strip)) {
      ++state.counts.fan;
      continue;
    }
    auto& [vertex_shader, stream_out] = state.shaders[draw.shader->hash];
    if (vertex_shader == nullptr) {
      constexpr D3D11_SO_DECLARATION_ENTRY ENTRY = {.Stream = 0, .SemanticName = "SV_Position", .SemanticIndex = 0, .StartComponent = 0,
                                                    .ComponentCount = 4, .OutputSlot = 0};
      const UINT stride = 16;
      device->CreateVertexShader(draw.shader->code.data(), draw.shader->code.size(), nullptr, vertex_shader.GetAddressOf());
      device->CreateGeometryShaderWithStreamOutput(draw.shader->code.data(), draw.shader->code.size(), &ENTRY, 1, &stride, 1,
                                                   D3D11_SO_NO_RASTERIZED_STREAM, nullptr, stream_out.GetAddressOf());
    }
    const Layout* layout = GetLayout(device, draw);
    if (!layout->usable || vertex_shader == nullptr || stream_out == nullptr) {
      ++state.counts.no_layout;
      continue;
    }
    std::array<ID3D11Buffer*, STREAMS> streams = {};
    bool buffers_found = (GetBuffer(device, context.Get(), draw.index_buffer) != nullptr) && (layout->streams >> STREAMS) == 0;
    for (UINT stream = 0; stream < STREAMS; ++stream) {
      if ((layout->streams & (1u << stream)) == 0) continue;
      streams[stream] = GetBuffer(device, context.Get(), draw.streams[stream]);
      buffers_found = buffers_found && streams[stream] != nullptr;
    }
    if (!buffers_found) {
      ++state.counts.no_buffer;
      continue;
    }
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(game_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) continue;
    std::memcpy(mapped.pData, state.constants.data() + draw.constants, draw.shader->registers * 4 * sizeof(float));
    context->Unmap(game_constants, 0);
    const std::array<ID3D11Buffer*, 2> constant_buffers = {game_constants, layout->conversion.Get()};
    context->VSSetConstantBuffers(0, 2, constant_buffers.data());
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->GSSetShader(stream_out.Get(), nullptr, 0);
    context->IASetInputLayout(layout->layout.Get());
    context->IASetVertexBuffers(0, STREAMS, streams.data(), draw.strides.data(), draw.offsets.data());
    context->IASetIndexBuffer(GetBuffer(device, context.Get(), draw.index_buffer),
                              (draw.index_size == 4) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, draw.index_offset);
    context->IASetPrimitiveTopology(static_cast<D3D11_PRIMITIVE_TOPOLOGY>(draw.topology));
    context->DrawIndexed(draw.index_count, draw.first_index, draw.vertex_offset);
    ++state.counts.replayed;
  }
  if (state.log_next) context->End(state.statistics.Get());
  ID3D11Buffer* const no_target = nullptr;
  context->SOSetTargets(1, &no_target, &start);
  context->GSSetShader(nullptr, nullptr, 0);
  context->VSSetShader(nullptr, nullptr, 0);
  context->IASetInputLayout(nullptr);
  const std::array<ID3D11Buffer*, STREAMS> no_streams = {};
  const std::array<UINT, STREAMS> zeros = {};
  context->IASetVertexBuffers(0, STREAMS, no_streams.data(), zeros.data(), zeros.data());
  context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
  if (state.log_next) {
    // Trace One Frame: what the replay did (the wait for its statistics is this frame's only).
    D3D11_QUERY_DATA_SO_STATISTICS written = {};
    while (context->GetData(state.statistics.Get(), &written, sizeof(written), 0) == S_FALSE) {
      YieldProcessor();
    }
    const Counts& counts = state.counts;
    reshade::log::message(reshade::log::level::info,
                          std::format("[R.U.S.E.] Scene Replay: {} scene draws, {} at their earlier place, {} replayed ({} triangles, {} "
                                      "needed), skipped: {} unknown shader, {} no layout, {} unmirrored buffer, {} fan; {} mirrored "
                                      "buffers, {} placement keys",
                                      counts.recorded, counts.placed, counts.replayed, written.NumPrimitivesWritten,
                                      written.PrimitivesStorageNeeded, counts.no_shader, counts.no_layout, counts.no_buffer, counts.fan,
                                      state.mirrors.size(), state.placements.size())
                              .c_str());
    state.log_next = false;
  }
  state.draws.clear();
  state.constants.clear();
  state.counts = {};
}

// A5, at this frame's trace (ray_tracing.hpp; rays): the last frame's scene draws into rt_core's scene in this frame's
// camera (their c0-c3 x the camera's move: `view_projection`, rows acting on column vectors, x the last frame's inverse),
// bound natively on the proxy's device. A rigid draw at a placement rt_core still has is only marked drawn; the others are
// captured, each at its own key: b2 holds its placement's slice of `identity` (one mesh at two places is two meshes),
// and a skinned draw is its own capture (its bones are in b0, outside rt_core's key).
inline void CaptureScene(reshade::api::command_list* cmd_list, const std::array<double, 16>& view_projection) {
  auto* proxy_reshade = renodx::utils::device_proxy::proxy_device_reshade;
  if (!state.rays || proxy_reshade == nullptr || !state.previous_valid || state.previous.empty()) return;
  auto* device = reinterpret_cast<ID3D11Device*>(proxy_reshade->get_native());
  ComPtr<ID3D11DeviceContext> context;
  device->GetImmediateContext(context.GetAddressOf());
  ComPtr<ID3D11DeviceContext1> context1;
  if (FAILED(context.As(&context1))) return;
  if (state.capture_depth == nullptr) {
    const D3D11_BUFFER_DESC constants = {.ByteWidth = 256 * 16, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                         .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
    const D3D11_BUFFER_DESC identity = {.ByteWidth = IDENTITY_SLOTS * 16 * 16, .Usage = D3D11_USAGE_DEFAULT,
                                        .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_TEXTURE2D_DESC depth = {.Width = twattila::dlaa_bridge::bridge.width, .Height = twattila::dlaa_bridge::bridge.height,
                                        .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_D32_FLOAT, .SampleDesc = {.Count = 1},
                                        .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_DEPTH_STENCIL};
    ComPtr<ID3D11Texture2D> depth_texture;
    if ((state.game_constants == nullptr && FAILED(device->CreateBuffer(&constants, nullptr, state.game_constants.GetAddressOf())))
        || FAILED(device->CreateBuffer(&identity, nullptr, state.identity.GetAddressOf()))
        || FAILED(device->CreateTexture2D(&depth, nullptr, depth_texture.GetAddressOf()))
        || FAILED(device->CreateDepthStencilView(depth_texture.Get(), nullptr, state.capture_depth.GetAddressOf()))) {
      reshade::log::message(reshade::log::level::warning, "[R.U.S.E.] Ray tracing: the scene's objects weren't made: off");
      state.capture_depth.Reset();
      state.rays = false;
      return;
    }
  }
  std::array<double, 16> move = {};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      for (int k = 0; k < 4; ++k) {
        move[(row * 4) + column] += view_projection[(row * 4) + k] * state.previous_inverse[(k * 4) + column];
      }
    }
  }
  context->OMSetRenderTargets(0, nullptr, state.capture_depth.Get());
  context->PSSetShader(nullptr, nullptr, 0);
  ID3D11Buffer* const game_constants = state.game_constants.Get();
  ID3D11Buffer* const identity = state.identity.Get();
  constexpr UINT IDENTITY_CONSTANTS = 16;
  uint32_t kept = 0;
  uint32_t captured = 0;
  uint32_t skipped = 0;
  std::array<float, 1024> registers = {};
  for (const auto& draw : state.previous) {
    Placement* placement = nullptr;
    if (const auto found = state.placements.find(draw.place_key); found != state.placements.end() && draw.place_index < found->second.size()) {
      placement = &found->second[draw.place_index];
    }
    if (draw.placed && placement != nullptr && placement->scene_key != 0 && twattila::rt_scene::MarkDrawn(placement->scene_key)) {
      ++kept;
      continue;
    }
    const Layout* layout = GetLayout(device, draw);
    std::array<ID3D11Buffer*, STREAMS> streams = {};
    ID3D11Buffer* indices = GetBuffer(device, context.Get(), draw.index_buffer);
    bool usable = draw.topology == static_cast<uint32_t>(reshade::api::primitive_topology::triangle_list) && layout->usable
                  && indices != nullptr && (layout->streams >> STREAMS) == 0;
    for (UINT stream = 0; usable && stream < STREAMS; ++stream) {
      if ((layout->streams & (1u << stream)) == 0) continue;
      streams[stream] = GetBuffer(device, context.Get(), draw.streams[stream]);
      usable = streams[stream] != nullptr;
    }
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (!usable || FAILED(context->Map(game_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
      ++skipped;
      continue;
    }
    // c0-c3 in this frame's camera, the other registers as the game set them.
    const float* game = state.previous_constants.data() + draw.constants;
    const size_t floats = draw.shader->registers * 4u;
    std::copy_n(game, floats, registers.begin());
    for (int row = 0; row < 4; ++row) {
      for (int column = 0; column < 4; ++column) {
        double moved = 0.0;
        for (int k = 0; k < 4; ++k) moved += move[(row * 4) + k] * game[(k * 4) + column];
        registers[(row * 4) + column] = static_cast<float>(moved);
      }
    }
    std::memcpy(mapped.pData, registers.data(), floats * sizeof(float));
    context->Unmap(game_constants, 0);
    const std::array<ID3D11Buffer*, 2> constant_buffers = {game_constants, layout->conversion.Get()};
    context->VSSetConstantBuffers(0, 2, constant_buffers.data());
    const UINT first_constant = ((placement != nullptr) ? placement->slot : 0u) * IDENTITY_CONSTANTS;
    context1->VSSetConstantBuffers1(2, 1, &identity, &first_constant, &IDENTITY_CONSTANTS);
    context->IASetInputLayout(layout->layout.Get());
    context->IASetVertexBuffers(0, STREAMS, streams.data(), draw.strides.data(), draw.offsets.data());
    context->IASetIndexBuffer(indices, (draw.index_size == 4) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, draw.index_offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const uint64_t key = twattila::rt_scene::CaptureReplayed(
        cmd_list,
        {.hash = draw.shader->hash, .capture_shader = draw.shader->code,
         .terrain = draw.shader->hash == 0xF01502EA || draw.shader->hash == 0xF28418A2, .dynamic = draw.skinned, .every_draw = draw.skinned},
        {.index_count = draw.index_count, .instance_count = 1, .first_index = draw.first_index, .vertex_offset = draw.vertex_offset});
    if (placement != nullptr && !draw.skinned) placement->scene_key = key;
    ++captured;
  }
  const std::array<ID3D11Buffer*, 3> no_constants = {};
  context->VSSetConstantBuffers(0, 3, no_constants.data());
  context->OMSetRenderTargets(0, nullptr, nullptr);
  context->IASetInputLayout(nullptr);
  const std::array<ID3D11Buffer*, STREAMS> no_streams = {};
  const std::array<UINT, STREAMS> zeros = {};
  context->IASetVertexBuffers(0, STREAMS, no_streams.data(), zeros.data(), zeros.data());
  context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
  if (state.log_capture) {
    reshade::log::message(reshade::log::level::info,
                          std::format("[R.U.S.E.] Ray tracing: {} scene draws of the last frame, {} kept in the scene, {} captured, {} "
                                      "skipped (strip or fan, no layout, unmirrored buffer); {} placement keys",
                                      state.previous.size(), kept, captured, skipped, state.placements.size())
                              .c_str());
    state.log_capture = false;
  }
}

// The game's D3D9 device going: its buffers and declarations with it; the proxy's: its objects (made again on the next
// replay); no other device's.
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device == bridge::state.host_device) {
    state.mirrors.clear();
    state.mappings.clear();
    state.declarations.clear();
    state.draws.clear();
    state.constants.clear();
    state.previous.clear();
    state.placements.clear();
    return;
  }
  if (!renodx::utils::device_proxy::IsProxyDevice(device)) return;
  state.shaders.clear();
  state.layouts.clear();
  state.game_constants.Reset();
  state.positions.Reset();
  state.statistics.Reset();
  state.identity.Reset();
  state.capture_depth.Reset();
  state.mirrors.for_each_m([](auto& entry) {
    auto& [handle, mirror] = entry;
    mirror.buffer.Reset();
    mirror.uploaded = 0;
  });
}

}  // namespace ruse::replay
