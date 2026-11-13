/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <include/reshade.hpp>

#include "../twattila/frame_trace.hpp"

namespace twpharaohdynasties::trace_reads {

// The data run's reads (frame_trace's draw_reads: ReShade.ini [renodx] FrameTraceReads=1;
// tools\tw\pharaoh\open_items\plan.md B / C) in the game's layouts (109B79CE's RDEF, the input layouts): the lit smoke's
// sorted particles (t2 SORTDATA {dist, pid}, t6 PARTICLE_INSTANCE_UNPACKED at each pid, t1 EMITTER_INSTANCE_UNPACKED's
// emc_id there, t4 EMITTER_CONSTANTS_VS's rows: what the CPU sorts and culls), the point lights' instances (E318CE77's vb1,
// also the frame after: whether they move), the water reflection apply's quad (A0EA5C9E's vb0 at DE4F92CD: TEXCOORD0's
// range) and the terrain's world bounds (377DE0D5: x / z of vb0 through its instance rows POSITION1 / 3, y from the height
// map left out; 026CBC61: vb0 x (1, 1, 1.15476), its asm).
inline std::string Read(const twattila::frame_trace::Draw& draw) {
  using twattila::frame_trace::ReadBuffer;
  ID3D11DeviceContext* context = draw.context;
  const bool particles = draw.vertex_shader == 0x109B79CE && (draw.pixel_shader == 0x3F1B86E1 || draw.pixel_shader == 0xE8524BB1);
  const bool lights = draw.pixel_shader == 0xE318CE77;
  const bool quad = draw.vertex_shader == 0xA0EA5C9E && draw.pixel_shader == 0xDE4F92CD;
  const bool terrain = draw.vertex_shader == 0x377DE0D5 || draw.vertex_shader == 0x026CBC61;
  if (!lights && (draw.next_frame || !(particles || quad || terrain))) return "";

  std::string layout = " | layout";
  for (const auto& element : draw.layout) {
    layout += std::format(" {}{} f{} vb{}+{}{}", element.semantic, element.index, static_cast<int>(element.format), element.slot,
                          element.offset, (element.per_instance ? " instanced" : ""));
  }
  struct Stream {
    std::vector<uint8_t> bytes;  // empty: none bound or past its end
    UINT stride = 0;
  };
  // A vertex stream's `elements` (its stride's) from `first` on, read back.
  const auto stream = [&](UINT slot, UINT first, UINT elements) {
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    Stream read;
    UINT offset = 0;
    context->IAGetVertexBuffers(slot, 1, buffer.GetAddressOf(), &read.stride, &offset);
    if (buffer && elements > 0) read.bytes = ReadBuffer<uint8_t>(context, buffer.Get(), offset + (first * read.stride), elements * read.stride);
    return read;
  };
  // A stream's elements as floats, a line each.
  const auto rows = [](const Stream& read) {
    std::string text;
    for (size_t at = 0; read.stride >= 4 && at + read.stride <= read.bytes.size(); at += read.stride) {
      text += "\n   ";
      for (size_t word = at; word + 4 <= at + read.stride; word += 4) {
        float value = 0.f;
        std::memcpy(&value, read.bytes.data() + word, 4);
        text += std::format(" {}", value);
      }
    }
    return text;
  };
  // The draw's vertices (an indexed draw's through its index buffer and base vertex).
  const auto vertex_ids = [&] {
    std::vector<uint32_t> ids;
    if (!draw.indexed) {
      for (uint32_t vertex = 0; vertex < draw.count; ++vertex) ids.push_back(draw.first + vertex);
      return ids;
    }
    Microsoft::WRL::ComPtr<ID3D11Buffer> indices;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT offset = 0;
    context->IAGetIndexBuffer(indices.GetAddressOf(), &format, &offset);
    if (!indices) return ids;
    if (format == DXGI_FORMAT_R16_UINT) {
      for (const uint16_t index : ReadBuffer<uint16_t>(context, indices.Get(), offset + (draw.first * 2), draw.count)) ids.push_back(index + draw.base_vertex);
    } else {
      for (const uint32_t index : ReadBuffer<uint32_t>(context, indices.Get(), offset + (draw.first * 4), draw.count)) ids.push_back(index + draw.base_vertex);
    }
    return ids;
  };

  std::string text;
  if (lights) {
    const Stream instances = stream(1, draw.first_instance, draw.instances);
    text += std::format("\n  vb1 instances ({} from {}, stride {}{}):{}", draw.instances, draw.first_instance, instances.stride, layout, rows(instances));
  }
  if (quad) {
    const auto ids = vertex_ids();
    if (!ids.empty()) {
      const auto [lowest, highest] = std::ranges::minmax(ids);
      const Stream vertices = stream(0, lowest, highest - lowest + 1);
      text += std::format("\n  vb0 vertices {} to {} (stride {}{}):{}", lowest, highest, vertices.stride, layout, rows(vertices));
    }
  }
  if (terrain) {
    // An element's value from a vertex's (or an instance's) bytes; nullopt: a format not read here.
    const auto decode = [](reshade::api::format format, const uint8_t* bytes) -> std::optional<std::array<float, 4>> {
      std::array<float, 4> value = {0.f, 0.f, 0.f, 1.f};
      std::array<uint16_t, 4> halves = {};
      std::array<int16_t, 4> signed_halves = {};
      switch (format) {
        case reshade::api::format::r32g32b32a32_float:
          std::memcpy(value.data(), bytes, 16);
          return value;
        case reshade::api::format::r32g32b32_float:
          std::memcpy(value.data(), bytes, 12);
          return value;
        case reshade::api::format::r32g32_float:
          std::memcpy(value.data(), bytes, 8);
          return value;
        case reshade::api::format::r16g16b16a16_float:
          std::memcpy(halves.data(), bytes, 8);
          for (size_t c = 0; c < 4; ++c) value[c] = DirectX::PackedVector::XMConvertHalfToFloat(halves[c]);
          return value;
        case reshade::api::format::r16g16_float:
          std::memcpy(halves.data(), bytes, 4);
          for (size_t c = 0; c < 2; ++c) value[c] = DirectX::PackedVector::XMConvertHalfToFloat(halves[c]);
          return value;
        case reshade::api::format::r16g16b16a16_unorm:
          std::memcpy(halves.data(), bytes, 8);
          for (size_t c = 0; c < 4; ++c) value[c] = halves[c] / 65535.f;
          return value;
        case reshade::api::format::r16g16b16a16_uint:
          std::memcpy(halves.data(), bytes, 8);
          for (size_t c = 0; c < 4; ++c) value[c] = halves[c];
          return value;
        case reshade::api::format::r16g16b16a16_snorm:
          std::memcpy(signed_halves.data(), bytes, 8);
          for (size_t c = 0; c < 4; ++c) value[c] = std::max(signed_halves[c] / 32767.f, -1.f);
          return value;
        case reshade::api::format::r16g16b16a16_sint:
          std::memcpy(signed_halves.data(), bytes, 8);
          for (size_t c = 0; c < 4; ++c) value[c] = signed_halves[c];
          return value;
        default:
          return std::nullopt;
      }
    };
    const auto element = [&](uint32_t index) {
      return std::ranges::find_if(draw.layout, [&](const auto& found) { return found.semantic == "POSITION" && found.index == index; });
    };
    const auto position = element(0);
    const auto x_row = element(1);
    const auto z_row = element(3);
    const bool rows_known = x_row != draw.layout.end() && z_row != draw.layout.end() && x_row->slot == z_row->slot;
    const auto ids = vertex_ids();
    if (position == draw.layout.end() || ids.empty() || (draw.vertex_shader == 0x377DE0D5 && !rows_known)) {
      return text + std::format("\n  world bounds: no POSITION0 (or 377DE0D5's rows) or no vertices read{}", layout);
    }
    const auto [lowest, highest] = std::ranges::minmax(ids);
    const Stream vertices = stream(position->slot, lowest, highest - lowest + 1);
    Stream instances;
    if (draw.vertex_shader == 0x377DE0D5) {
      instances = stream(x_row->slot, draw.first_instance, draw.instances);
    }
    constexpr float LARGEST = std::numeric_limits<float>::max();
    std::array<float, 3> low = {LARGEST, LARGEST, LARGEST};
    std::array<float, 3> high = {-LARGEST, -LARGEST, -LARGEST};
    // An element within its stream's stride (else its bytes would be read past the read-back).
    const auto fits = [](const twattila::frame_trace::LayoutElement& field, UINT stride) {
      return field.offset + reshade::api::format_row_pitch(field.format, 1) <= stride;
    };
    bool read = !vertices.bytes.empty() && fits(*position, vertices.stride);
    if (draw.vertex_shader == 0x377DE0D5) {
      read = read && fits(*x_row, instances.stride) && fits(*z_row, instances.stride);
    }
    for (const uint32_t id : ids) {
      if (!read) break;
      const auto vertex = decode(position->format, vertices.bytes.data() + ((id - lowest) * vertices.stride) + position->offset);
      if (!vertex.has_value()) {
        read = false;
        break;
      }
      const auto [x, y, z, w] = *vertex;
      if (draw.vertex_shader == 0x026CBC61) {
        const std::array<float, 3> world = {x, y, z * 1.15476f};
        for (size_t c = 0; c < 3; ++c) {
          low[c] = std::min(low[c], world[c]);
          high[c] = std::max(high[c], world[c]);
        }
        continue;
      }
      for (size_t at = 0; at + instances.stride <= instances.bytes.size() && instances.stride > 0; at += instances.stride) {
        const auto x_weights = decode(x_row->format, instances.bytes.data() + at + x_row->offset);
        const auto z_weights = decode(z_row->format, instances.bytes.data() + at + z_row->offset);
        if (!x_weights.has_value() || !z_weights.has_value()) {
          read = false;
          break;
        }
        const auto& a = *x_weights;
        const auto& b = *z_weights;
        const std::array<float, 3> world = {(x * a[0]) + (y * a[1]) + (z * a[2]) + a[3], 0.f, (x * b[0]) + (y * b[1]) + (z * b[2]) + b[3]};
        for (const size_t c : {size_t{0}, size_t{2}}) {
          low[c] = std::min(low[c], world[c]);
          high[c] = std::max(high[c], world[c]);
        }
      }
    }
    const std::string counts = std::format("{} indices, vertices {} to {}, {} instances{}", ids.size(), lowest, highest, draw.instances, layout);
    if (!read) {
      text += std::format("\n  world bounds: a buffer or format not read ({})", counts);
    } else if (draw.vertex_shader == 0x026CBC61) {
      text += std::format("\n  world bounds: x {} {} y {} {} z {} {} ({})", low[0], high[0], low[1], high[1], low[2], high[2], counts);
    } else {
      text += std::format("\n  world bounds: x {} {} z {} {}, y from the height map ({})", low[0], high[0], low[2], high[2], counts);
    }
  }
  if (particles) {
    struct StructuredView {
      Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
      UINT first = 0;
      UINT elements = 0;
    };
    const auto view = [&](UINT slot) {
      StructuredView structured;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resource_view;
      context->VSGetShaderResources(slot, 1, resource_view.GetAddressOf());
      if (!resource_view) return structured;
      D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
      resource_view->GetDesc(&desc);
      Microsoft::WRL::ComPtr<ID3D11Resource> resource;
      resource_view->GetResource(resource.GetAddressOf());
      if (FAILED(resource.As(&structured.buffer))) return structured;
      const bool extended = desc.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX;
      structured.first = (extended ? desc.BufferEx.FirstElement : desc.Buffer.FirstElement);
      structured.elements = (extended ? desc.BufferEx.NumElements : desc.Buffer.NumElements);
      return structured;
    };
    const StructuredView sorted_view = view(2);
    const StructuredView particle_view = view(6);
    const StructuredView emitter_view = view(1);
    const StructuredView constants_view = view(4);
    if (!sorted_view.buffer || !particle_view.buffer || !emitter_view.buffer || !constants_view.buffer) return text + "\n  particles: a buffer not bound";
    // The RDEF's strides: SORTDATA 8, PARTICLE_INSTANCE_UNPACKED 64, EMITTER_INSTANCE_UNPACKED 128, EMITTER_CONSTANTS_VS 112.
    const auto sorted = ReadBuffer<std::array<uint32_t, 2>>(context, sorted_view.buffer.Get(), sorted_view.first * 8,
                                                            std::min(draw.instances, sorted_view.elements));
    uint32_t lowest_particle = UINT32_MAX;
    uint32_t highest_particle = 0;
    for (const auto& [dist, pid] : sorted) {
      if (pid >= particle_view.elements) continue;
      lowest_particle = std::min(lowest_particle, pid);
      highest_particle = std::max(highest_particle, pid);
    }
    std::vector<std::array<uint32_t, 16>> particle_rows;
    if (lowest_particle <= highest_particle) {
      particle_rows = ReadBuffer<std::array<uint32_t, 16>>(context, particle_view.buffer.Get(), (particle_view.first + lowest_particle) * 64,
                                                           highest_particle - lowest_particle + 1);
    }
    uint32_t lowest_emitter = UINT32_MAX;
    uint32_t highest_emitter = 0;
    for (const auto& particle : particle_rows) {
      if (particle[0] >= emitter_view.elements) continue;  // -1 (no emitter) too
      lowest_emitter = std::min(lowest_emitter, particle[0]);
      highest_emitter = std::max(highest_emitter, particle[0]);
    }
    std::vector<std::array<uint32_t, 32>> emitter_rows;
    if (lowest_emitter <= highest_emitter) {
      emitter_rows = ReadBuffer<std::array<uint32_t, 32>>(context, emitter_view.buffer.Get(), (emitter_view.first + lowest_emitter) * 128,
                                                          highest_emitter - lowest_emitter + 1);
    }
    const auto constant_rows = ReadBuffer<std::array<uint32_t, 28>>(context, constants_view.buffer.Get(), constants_view.first * 112, constants_view.elements);
    const auto as_float = [](uint32_t bits) { return std::bit_cast<float>(bits); };
    text += std::format("\n  particles ({} of {} sorted: index, dist, pid | t6 emitter instance, position x y z | t1 emc_id):", sorted.size(), draw.instances);
    std::vector<uint32_t> emitter_constants;
    for (size_t index = 0; index < sorted.size(); ++index) {
      const auto& [dist, pid] = sorted[index];
      if (pid < lowest_particle || pid - lowest_particle >= particle_rows.size()) {
        text += std::format("\n    {} {} {} | -", index, as_float(dist), pid);
        continue;
      }
      const auto& particle = particle_rows[pid - lowest_particle];
      std::string emc = "-";
      if (particle[0] >= lowest_emitter && particle[0] - lowest_emitter < emitter_rows.size()) {
        const uint32_t emc_id = emitter_rows[particle[0] - lowest_emitter][2];
        emitter_constants.push_back(emc_id);
        emc = std::format("{}", emc_id);
      }
      text += std::format("\n    {} {} {} | {} {} {} {} | {}", index, as_float(dist), pid, static_cast<int32_t>(particle[0]), as_float(particle[4]),
                          as_float(particle[5]), as_float(particle[6]), emc);
    }
    std::ranges::sort(emitter_constants);
    const auto repeated = std::ranges::unique(emitter_constants);
    emitter_constants.erase(repeated.begin(), repeated.end());
    text += "\n  emitter constants (t4 rows: emc_id, behaviour mask, diffuse, normal, draw flags, culling distances x y z w, viewport culling expansion):";
    for (const uint32_t emc_id : emitter_constants) {
      if (emc_id >= constant_rows.size()) continue;
      const auto& row = constant_rows[emc_id];
      text += std::format("\n    {} {:x} {} {} {:x} {} {} {} {} {}", emc_id, row[0], row[1], row[2], row[3], as_float(row[4]), as_float(row[5]),
                          as_float(row[6]), as_float(row[7]), as_float(row[8]));
    }
  }
  return text;
}

}  // namespace twpharaohdynasties::trace_reads
