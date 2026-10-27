/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/log.hpp"

// AO trace (Advanced button): the next frame's AO passes (bound textures, UAVs and constants) written to ReShade.log.
// The hooked passes need replacement code (hooks on passes without one never run). Nothing runs per frame otherwise.
namespace tw::ao_trace {

namespace internal {
static std::atomic<bool> trace_requested = false;
static bool tracing = false;  // this frame's passes are written
}  // namespace internal

// An AO pass (draw or dispatch, `compute` for a dispatch): written while tracing.
inline void OnPass(reshade::api::command_list* cmd_list, uint32_t hash, bool compute) {
  if (!internal::tracing || cmd_list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
  auto* context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  context->GetDevice(device.GetAddressOf());
  const auto describe = [](ID3D11View* view) -> std::string {
    if (view == nullptr) return "none";
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (SUCCEEDED(resource.As(&texture))) {
      D3D11_TEXTURE2D_DESC desc;
      texture->GetDesc(&desc);
      return std::format("{}x{} format {} mips {}", desc.Width, desc.Height, static_cast<int>(desc.Format), desc.MipLevels);
    }
    return "not a 2D texture";
  };
  std::string text = std::format("[Total War] AO trace: pass {:#010x} ({})", hash, compute ? "compute" : "pixel");
  for (UINT slot = 0; slot < 4; ++slot) {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    if (compute) {
      context->CSGetShaderResources(slot, 1, view.GetAddressOf());
    } else {
      context->PSGetShaderResources(slot, 1, view.GetAddressOf());
    }
    text += std::format("; t{} {}", slot, describe(view.Get()));
  }
  if (compute) {
    for (UINT slot = 0; slot < 2; ++slot) {
      Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> view;
      context->CSGetUnorderedAccessViews(slot, 1, view.GetAddressOf());
      text += std::format("; u{} {}", slot, describe(view.Get()));
    }
  } else {
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
    context->OMGetRenderTargets(1, view.GetAddressOf(), nullptr);
    text += std::format("; o0 {}", describe(view.Get()));
  }
  renodx::utils::log::i(text);
  // The constant buffers: every float, in rows of 4 (c0, c1, ...).
  for (UINT slot = 0; slot < 3; ++slot) {
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    if (compute) {
      context->CSGetConstantBuffers(slot, 1, buffer.GetAddressOf());
    } else {
      context->PSGetConstantBuffers(slot, 1, buffer.GetAddressOf());
    }
    if (buffer == nullptr) continue;
    D3D11_BUFFER_DESC desc;
    buffer->GetDesc(&desc);
    const D3D11_BUFFER_DESC staging_desc = {
        .ByteWidth = desc.ByteWidth,
        .Usage = D3D11_USAGE_STAGING,
        .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
    };
    Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
    if (FAILED(device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf()))) continue;
    context->CopyResource(staging.Get(), buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) continue;
    std::vector<float> values(desc.ByteWidth / sizeof(float));
    std::memcpy(values.data(), mapped.pData, values.size() * sizeof(float));
    context->Unmap(staging.Get(), 0);
    std::string rows = std::format("[Total War] AO trace: pass {:#010x} b{} ({} bytes):", hash, slot, desc.ByteWidth);
    for (size_t index = 0; index + 3 < values.size(); index += 4) {
      rows += std::format(" c{} {:g} {:g} {:g} {:g};", index / 4, values[index], values[index + 1], values[index + 2],
                          values[index + 3]);
    }
    renodx::utils::log::i(rows);
  }
}

// Advanced button: trace the next frame.
inline void RequestTrace() {
  internal::trace_requested.store(true, std::memory_order_relaxed);
}

// Every present: the traced frame's bounds (an atomic exchange, nothing else).
inline void OnPresent() {
  internal::tracing = internal::trace_requested.exchange(false, std::memory_order_relaxed);
  if (internal::tracing) renodx::utils::log::i("[Total War] AO trace: next frame");
}

}  // namespace tw::ao_trace
