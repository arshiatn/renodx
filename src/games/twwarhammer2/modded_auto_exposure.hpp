/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <shared_mutex>
#include <utility>

#include <include/reshade.hpp>

#include "../../utils/log.hpp"
#include "../twattila/rt_core.hpp"

// Modded Auto Exposure (Experimental, off; Attila's rows) in Warhammer and Warhammer II: the game meters its exposure on the
// CPU from compute_luma_stats' minimum / maximum / sum (Warhammer 96BCFCBB, Warhammer II A50883CE: the same instructions;
// ATN's traces: the clear (166E783A / 242CA153), then compute_luma_stats, the buffer copied out after it; one buffer for every
// frame of a device). With the row on, the sum it still holds at the clear (last frame's) is copied into `sum` first, and
// our compute_luma_stats (shaders/autoexposure00) reads it at t1 to cap the metered maximum.
namespace tw::modded_auto_exposure {

namespace internal {
struct LumaLastSum {
  std::shared_mutex mutex;
  ID3D11Device* device = nullptr;  // its buffer's (compared only)
  Microsoft::WRL::ComPtr<ID3D11Buffer> sum;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
  bool logged = false;
};
static LumaLastSum luma_last_sum;
}  // namespace internal

// At the game's clear (row on): last frame's sum (bytes 8 - 11 of the statistics at u0) copied out.
inline void OnClear(reshade::api::command_list* cmd_list) {
  auto* native = twattila::rt_scene::internal::NativeContext(cmd_list);
  if (native == nullptr) return;
  Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> stats_view;
  native->CSGetUnorderedAccessViews(0, 1, stats_view.GetAddressOf());
  if (!stats_view) return;
  Microsoft::WRL::ComPtr<ID3D11Resource> resource;
  stats_view->GetResource(resource.GetAddressOf());
  Microsoft::WRL::ComPtr<ID3D11Buffer> stats;
  if (FAILED(resource.As(&stats))) return;
  D3D11_BUFFER_DESC stats_desc = {};
  stats->GetDesc(&stats_desc);
  if (stats_desc.ByteWidth < 12) return;
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  native->GetDevice(device.GetAddressOf());
  auto& state = internal::luma_last_sum;
  const std::unique_lock lock(state.mutex);
  if (state.device != device.Get()) {
    state.view.Reset();
    state.sum.Reset();
    state.device = nullptr;
    const D3D11_BUFFER_DESC desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                    .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS};
    const D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_R32_TYPELESS, .ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX,
                                                       .BufferEx = {.FirstElement = 0, .NumElements = 4, .Flags = D3D11_BUFFEREX_SRV_FLAG_RAW}};
    if (FAILED(device->CreateBuffer(&desc, nullptr, state.sum.GetAddressOf()))
        || FAILED(device->CreateShaderResourceView(state.sum.Get(), &view_desc, state.view.GetAddressOf()))) {
      state.view.Reset();
      state.sum.Reset();
      return;
    }
    state.device = device.Get();
  }
  const D3D11_BOX sum_bytes = {.left = 8, .top = 0, .front = 0, .right = 12, .bottom = 1, .back = 1};
  native->CopySubresourceRegion(state.sum.Get(), 0, 0, 0, 0, stats.Get(), 0, &sum_bytes);
  if (!std::exchange(state.logged, true)) {
    renodx::utils::log::i("[Total War] First run: Modded Auto Exposure: last frame's luma sum kept for the metered maximum.");
  }
}

// Around our compute_luma_stats (row on): last frame's sum bound at its t1 (nothing there in ATN's traces), unbound after.
inline void BindLastSum(reshade::api::command_list* cmd_list, bool bind) {
  auto* native = twattila::rt_scene::internal::NativeContext(cmd_list);
  if (native == nullptr) return;
  auto& state = internal::luma_last_sum;
  const std::shared_lock lock(state.mutex);
  ID3D11ShaderResourceView* view = bind ? state.view.Get() : nullptr;
  native->CSSetShaderResources(1, 1, &view);  // t1: t_luma_last_sum
}

// Its buffer's device (nullptr: the addon's end, DetachAddon).
inline void OnDestroyDevice(reshade::api::device* device) {
  if (device != nullptr && device->get_api() != reshade::api::device_api::d3d11) return;
  auto& state = internal::luma_last_sum;
  const std::unique_lock lock(state.mutex);
  if (device != nullptr && state.device != reinterpret_cast<ID3D11Device*>(device->get_native())) return;
  state.view.Reset();
  state.sum.Reset();
  state.device = nullptr;
}

}  // namespace tw::modded_auto_exposure
