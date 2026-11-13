/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "../twattila/frame_trace.hpp"

namespace twwarhammer2::trace_reads {

// The data run's reads (frame_trace's draw_reads: ReShade.ini [renodx] FrameTraceReads=1; tools\tw\wh2\plan_1114.md): the point
// and spot lights' instances (pointlight* VS D2A0FF23: POSITION0 the mesh, POSITION1 / 2 xyzw; spotlight / gobospotlight VS
// 5E0BC81E: POSITION0 / 1, POSITION2-5 xyzw; their RDEFs), each per-instance stream's rows as floats, also the frame after
// (whether they move). Their reach and colour scale decide the torch shadows' ranges (game-facts: world scale, light draws).
inline std::string Read(const twattila::frame_trace::Draw& draw) {
  if (draw.vertex_shader != 0xD2A0FF23 && draw.vertex_shader != 0x5E0BC81E) return "";
  std::string layout = " | layout";
  std::vector<uint32_t> slots;
  for (const auto& element : draw.layout) {
    layout += std::format(" {}{} f{} vb{}+{}{}", element.semantic, element.index, static_cast<int>(element.format), element.slot,
                          element.offset, (element.per_instance ? " instanced" : ""));
    if (element.per_instance && std::ranges::find(slots, element.slot) == slots.end()) slots.push_back(element.slot);
  }
  if (slots.empty()) return std::format("\n  light instances: no per-instance stream known{}", layout);
  std::string text;
  for (const uint32_t slot : slots) {
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    UINT stride = 0;
    UINT offset = 0;
    draw.context->IAGetVertexBuffers(slot, 1, buffer.GetAddressOf(), &stride, &offset);
    if (!buffer || stride < 4) {
      text += std::format("\n  vb{} instances: none bound", slot);
      continue;
    }
    const auto bytes = twattila::frame_trace::ReadBuffer<uint8_t>(draw.context, buffer.Get(), offset + (draw.first_instance * stride),
                                                                  draw.instances * stride);
    text += std::format("\n  vb{} instances ({} from {}, stride {}{}):", slot, draw.instances, draw.first_instance, stride, layout);
    for (size_t at = 0; at + stride <= bytes.size(); at += stride) {
      text += "\n   ";
      for (size_t word = at; word + 4 <= at + stride; word += 4) {
        float value = 0.f;
        std::memcpy(&value, bytes.data() + word, 4);
        text += std::format(" {}", value);
      }
    }
  }
  return text;
}

}  // namespace twwarhammer2::trace_reads
