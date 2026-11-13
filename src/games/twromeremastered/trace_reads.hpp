/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include "../twattila/frame_trace.hpp"

// The data run's reads (frame_trace's draw_reads, ReShade.ini [renodx] FrameTraceReads=1;
// tools\tw\romeremastered\plan_1114.md, data run) in Feral's layouts (the dump's bytes: no RDEF in its shaders):
// - the camera every scene VS reads at b5 (rows 8-11 its view-projection, DepthOnly 6EC7EFAD): the buffer's identity,
//   usage and range, and its 12 rows once per buffer and range in each frame read;
// - the frame constants the forward PS read at b8 (row 0's zw the inverse screen size, PBR's asm): 4 rows, the same way;
// - a unit draw's rows (VS b0's first row: row bases; t9 the instance's 64 bytes at b0.y + its first instance, t10 its
//   first bone's 32 bytes at b0.z): in the traced frame and the frame after, whether the game rewrites them in place.
namespace twromeremastered::trace_reads {

// The Unit family's vertex shaders (24 in the dump, names.tsv dx11_Unit.hlsl ... vs_VSMain).
constexpr std::array<uint32_t, 24> UNIT_VERTEX_SHADERS = {
    0x01BC74B8, 0x061E3461, 0x1C3CB126, 0x24CFFC18, 0x25BC528B, 0x2B6DE2E1, 0x50BB3A95, 0x55E0867C,
    0x57AB1233, 0x6589DEF9, 0x65AEF9ED, 0x6D6BF58D, 0x6DC88A9C, 0x755F73A6, 0x798FF28F, 0x7CA1B532,
    0x7E454B94, 0x9C4BF134, 0xB31241CA, 0xC03461BF, 0xE4F329E6, 0xE83EE952, 0xF14418F8, 0xF72BF632,
};

namespace internal {

using Microsoft::WRL::ComPtr;

struct State {
  std::shared_mutex mutex;
  bool next_frame = false;                                  // the frame the read buffers belong to
  std::vector<std::pair<ID3D11Buffer*, UINT>> rows_written;  // buffer and first constant, this frame
};
static State state;

}  // namespace internal

inline std::string Read(const twattila::frame_trace::Draw& draw) {
  using internal::ComPtr;
  using twattila::frame_trace::ReadBuffer;
  ComPtr<ID3D11DeviceContext1> context;
  if (FAILED(draw.context->QueryInterface(IID_PPV_ARGS(context.GetAddressOf())))) return "";

  // A bound constant buffer's identity, range and usage, and its first `rows` rows when not yet written this frame.
  const auto constants = [&](const char* name, ID3D11Buffer* buffer, UINT first, UINT count, UINT rows) {
    if (buffer == nullptr) return std::string();
    D3D11_BUFFER_DESC desc = {};
    buffer->GetDesc(&desc);
    std::string text = std::format(" | {} {:08X} +{}x{} {} bytes usage {} cpu {:x}", name, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer)),
                                   first, count, desc.ByteWidth, static_cast<int>(desc.Usage), desc.CPUAccessFlags);
    {
      const std::unique_lock lock(internal::state.mutex);
      if (internal::state.next_frame != draw.next_frame) {
        internal::state.next_frame = draw.next_frame;
        internal::state.rows_written.clear();
      }
      const std::pair key = {buffer, first};
      if (std::ranges::find(internal::state.rows_written, key) != internal::state.rows_written.end()) return text;
      internal::state.rows_written.push_back(key);
    }
    const auto values = ReadBuffer<float>(draw.context, buffer, first * 16, rows * 4);
    for (size_t row = 0; row + 4 <= values.size(); row += 4) {
      text += std::format("\n   {} c{}: {} {} {} {}", name, row / 4, values[row], values[row + 1], values[row + 2], values[row + 3]);
    }
    return text;
  };

  std::string text;
  {
    ComPtr<ID3D11Buffer> camera;
    UINT first = 0;
    UINT count = 0;
    context->VSGetConstantBuffers1(5, 1, camera.GetAddressOf(), &first, &count);
    text += constants("vs b5", camera.Get(), first, count, 12);
  }
  {
    ComPtr<ID3D11Buffer> frame;
    UINT first = 0;
    UINT count = 0;
    context->PSGetConstantBuffers1(8, 1, frame.GetAddressOf(), &first, &count);
    text += constants("ps b8", frame.Get(), first, count, 4);
  }
  if (std::ranges::find(UNIT_VERTEX_SHADERS, draw.vertex_shader) == UNIT_VERTEX_SHADERS.end()) return text;

  ComPtr<ID3D11Buffer> bases_buffer;
  context->VSGetConstantBuffers(0, 1, bases_buffer.GetAddressOf());
  if (!bases_buffer) return text;
  const auto bases = ReadBuffer<uint32_t>(draw.context, bases_buffer.Get(), 0, 4);
  if (bases.size() < 4) return text;
  float position_w = 0.f;
  std::memcpy(&position_w, bases.data(), 4);
  text += std::format(" | unit b0 {} {} {} {}", position_w, bases[1], bases[2], bases[3]);
  // A structured row of the VS's t`slot` at `element` (its view's first element added).
  const auto row = [&](UINT slot, UINT element, const char* name) {
    ComPtr<ID3D11ShaderResourceView> view;
    context->VSGetShaderResources(slot, 1, view.GetAddressOf());
    if (!view) return std::string();
    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
    view->GetDesc(&view_desc);
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Buffer> buffer;
    if (FAILED(resource.As(&buffer))) return std::string();
    D3D11_BUFFER_DESC desc = {};
    buffer->GetDesc(&desc);
    const UINT stride = desc.StructureByteStride;
    std::string line = std::format("\n   {} t{} {:08X} {} bytes stride {} usage {} cpu {:x} first {} at {}:", name, slot,
                                   static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer.Get())), desc.ByteWidth, stride,
                                   static_cast<int>(desc.Usage), desc.CPUAccessFlags, view_desc.Buffer.FirstElement, element);
    if (stride == 0) return line;
    for (const float value : ReadBuffer<float>(draw.context, buffer.Get(), (view_desc.Buffer.FirstElement + element) * stride, stride / 4)) {
      line += std::format(" {}", value);
    }
    return line;
  };
  text += row(9, bases[1] + draw.first_instance, "instance");
  text += row(10, bases[2], "bone");
  return text;
}

}  // namespace twromeremastered::trace_reads
