/*
 * Copyright (C) 2026 RenoDX contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "../../utils/directx.hpp"
#include "../../utils/log.hpp"

namespace expeditionsrome::dlss {

// Copy the exact current GPU constants before TAA, then complete after the
// stock dispatch is submitted. Completion still synchronizes with the copies;
// no previous frame's jitter or camera-cut constants are ever substituted.
// It also decides when the CPU copy of the uploads (constant_mirror.hpp) can
// replace that wait: after TRUST_AFTER exact matches in a row, then checked
// every frame against GPU copies read later without waiting.
struct ConstantReadback {
  static constexpr uint32_t TRUST_AFTER = 30;
  static constexpr uint32_t MAX_MISMATCHES = 3;

  struct Check {
    std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, 2> staging;
    std::array<uint32_t, 2> byte_widths = {};
    std::array<uint64_t, 2> offsets = {};
    std::array<std::vector<std::byte>, 2> expected;
    bool pending = false;
  };

  Microsoft::WRL::ComPtr<ID3D11Device> device;
  std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, 2> staging;
  std::array<uint32_t, 2> byte_widths = {};
  std::string error;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> pending_context;
  std::array<uint64_t, 2> pending_offsets = {};
  std::array<size_t, 2> pending_sizes = {};

  std::array<Check, 4> checks;
  uint32_t matches = 0;  // exact matches in a row
  uint32_t mismatches = 0;
  uint64_t checked = 0;  // frames whose CPU copy matched the GPU
  uint64_t misses = 0;   // frames the CPU copy had no data for
  bool mirror_off = false;
  std::string mirror_reason;

  bool Trusted() const {
    return !mirror_off && matches >= TRUST_AFTER;
  }

  std::string Describe() const {
    if (mirror_off) return "GPU readback (waits). CPU copy off: " + mirror_reason;
    if (Trusted()) return std::format("CPU copy, no wait ({} frames matched the GPU, {} without data)", checked, misses);
    return std::format("GPU readback (waits) while checking the CPU copy: {}/{} ({} frames without data)", matches, TRUST_AFTER, misses);
  }

  // First differing constant, or "" when equal.
  static std::string Difference(const std::array<std::span<const std::byte>, 2>& cpu,
                                const std::array<std::span<const std::byte>, 2>& gpu) {
    for (uint32_t slot = 0; slot < 2; ++slot) {
      if (cpu[slot].size() != gpu[slot].size()) return std::format("b{} size differs", slot);
      for (size_t offset = 0; offset < cpu[slot].size(); offset += 16) {
        const size_t bytes = std::min<size_t>(16, cpu[slot].size() - offset);
        if (std::memcmp(cpu[slot].data() + offset, gpu[slot].data() + offset, bytes) == 0) continue;
        std::array<float, 4> a = {};
        std::array<float, 4> b = {};
        std::memcpy(a.data(), cpu[slot].data() + offset, bytes);
        std::memcpy(b.data(), gpu[slot].data() + offset, bytes);
        return std::format("b{}[{}] CPU ({}, {}, {}, {}) GPU ({}, {}, {}, {})",
                           slot, offset / 16, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
      }
    }
    return {};
  }

  void Disable(const std::string& reason) {
    if (!mirror_off) renodx::utils::log::w("[Rome AA constants] CPU copy disabled, GPU readback waits again: ", reason);
    mirror_off = true;
    matches = 0;
    mirror_reason = reason;
  }

  // Same frame: CPU copy versus the exact readback.
  void Compare(const std::array<std::span<const std::byte>, 2>& cpu,
               const std::array<std::span<const std::byte>, 2>& gpu) {
    if (mirror_off) return;
    const auto difference = Difference(cpu, gpu);
    if (difference.empty()) {
      ++checked;
      if (++matches == TRUST_AFTER) {
        renodx::utils::log::i("[Rome AA constants] CPU copy matched ", TRUST_AFTER, " exact readbacks; GPU wait removed.");
      }
      return;
    }
    matches = 0;
    if (++mismatches >= MAX_MISMATCHES) Disable(difference);
  }

  // Whole-buffer GPU copies of this frame's bindings, compared by PollChecks later.
  void QueueCheck(ID3D11DeviceContext* context,
                  const std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, 2>& buffers,
                  const std::array<uint32_t, 2>& first_constants,
                  const std::array<std::span<const std::byte>, 2>& cpu) {
    const auto check = std::ranges::find_if(checks, [](const Check& item) { return !item.pending; });
    if (check == checks.end()) return;  // The GPU is behind; skip this frame's check.
    Microsoft::WRL::ComPtr<ID3D11Device> current_device;
    context->GetDevice(current_device.GetAddressOf());
    if (current_device != device) return;  // Queue() owns device changes.
    for (uint32_t slot = 0; slot < 2; ++slot) {
      D3D11_BUFFER_DESC source_desc = {};
      buffers[slot]->GetDesc(&source_desc);
      if (!check->staging[slot] || check->byte_widths[slot] != source_desc.ByteWidth) {
        check->staging[slot].Reset();
        check->byte_widths[slot] = 0;
        const D3D11_BUFFER_DESC desc = {
            .ByteWidth = source_desc.ByteWidth,
            .Usage = D3D11_USAGE_STAGING,
            .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
        };
        if (FAILED(device->CreateBuffer(&desc, nullptr, check->staging[slot].GetAddressOf()))) return;
        check->byte_widths[slot] = source_desc.ByteWidth;
      }
    }
    for (uint32_t slot = 0; slot < 2; ++slot) {
      context->CopyResource(check->staging[slot].Get(), buffers[slot].Get());
      check->offsets[slot] = uint64_t{first_constants[slot]} * 16;
      check->expected[slot].assign(cpu[slot].begin(), cpu[slot].end());
    }
    check->pending = true;
  }

  // Compares finished checks; never waits for the GPU.
  void PollChecks(ID3D11DeviceContext* context) {
    for (auto& check : checks) {
      if (!check.pending) continue;
      std::array<D3D11_MAPPED_SUBRESOURCE, 2> mapped = {};
      HRESULT result = S_OK;
      uint32_t mapped_count = 0;
      for (; mapped_count < 2; ++mapped_count) {
        result = context->Map(check.staging[mapped_count].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped[mapped_count]);
        if (FAILED(result)) break;
      }
      std::string difference;
      if (mapped_count == 2) {
        std::array<std::span<const std::byte>, 2> gpu;
        for (uint32_t slot = 0; slot < 2; ++slot) {
          if (check.offsets[slot] + check.expected[slot].size() > check.byte_widths[slot]) {
            difference = std::format("b{} check range invalid", slot);
            break;
          }
          gpu[slot] = {static_cast<const std::byte*>(mapped[slot].pData) + check.offsets[slot], check.expected[slot].size()};
        }
        if (difference.empty()) difference = Difference({check.expected[0], check.expected[1]}, gpu);
      }
      for (uint32_t slot = 0; slot < mapped_count; ++slot) {
        context->Unmap(check.staging[slot].Get(), 0);
      }
      if (result == DXGI_ERROR_WAS_STILL_DRAWING) continue;
      check.pending = false;
      if (mapped_count != 2) continue;  // A failed Map drops the check.
      if (difference.empty()) {
        ++checked;
      } else {
        Disable(difference);
      }
    }
  }

  void Cancel() {
    pending_context.Reset();
    pending_offsets = {};
    pending_sizes = {};
  }

  bool Queue(ID3D11DeviceContext* context,
             const std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, 2>& buffers,
             const std::array<uint32_t, 2>& first_constants,
             const std::array<uint32_t, 2>& constant_counts,
             const std::array<size_t, 2>& destination_sizes) {
    error.clear();
    const auto fail = [this](const std::string& message) {
      Cancel();
      error = message;
      return false;
    };
    if (pending_context) return fail("a constant readback is already pending");
    if (context == nullptr || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
      return fail("readback requires a native immediate context");
    }
    Microsoft::WRL::ComPtr<ID3D11Device> current_device;
    context->GetDevice(current_device.GetAddressOf());
    if (!current_device) return fail("context device unavailable");
    if (device != current_device) {
      staging = {};
      byte_widths = {};
      checks = {};
      device = current_device;
    }

    std::array<uint64_t, 2> byte_offsets = {};
    for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
      if (!buffers[slot]) return fail(std::format("b{} is unbound", slot));
      if (destination_sizes[slot] == 0) return fail(std::format("b{} read destination is empty", slot));
      D3D11_BUFFER_DESC source_desc = {};
      buffers[slot]->GetDesc(&source_desc);
      Microsoft::WRL::ComPtr<ID3D11Device> source_device;
      buffers[slot]->GetDevice(source_device.GetAddressOf());
      // ReShade hooks resource GetDevice to return its proxy even when queried
      // through native buffers. Preserve the returned COM ownership and unwrap
      // only a borrowed comparison pointer; the context is already native.
      auto* native_source_device = source_device.Get();
      renodx::utils::directx::NativeFromReShadeProxy(&native_source_device);
      byte_offsets[slot] = uint64_t{first_constants[slot]} * 16;
      if (native_source_device != device.Get()) return fail(std::format("b{} belongs to a different native device", slot));
      if ((source_desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0) {
        return fail(std::format("b{} is not a constant buffer (bind flags 0x{:X})", slot, source_desc.BindFlags));
      }
      if (source_desc.ByteWidth == 0 || source_desc.ByteWidth > 64 * 1024
          || byte_offsets[slot] > source_desc.ByteWidth
          || destination_sizes[slot] > source_desc.ByteWidth - byte_offsets[slot]
          || destination_sizes[slot] > uint64_t{constant_counts[slot]} * 16) {
        return fail(std::format("b{} unsupported range: bytes={}, first={}, count={}, required={}",
                                slot, source_desc.ByteWidth, first_constants[slot], constant_counts[slot], destination_sizes[slot]));
      }

      if (!staging[slot] || byte_widths[slot] != source_desc.ByteWidth) {
        staging[slot].Reset();
        byte_widths[slot] = 0;
        const D3D11_BUFFER_DESC desc = {
            .ByteWidth = source_desc.ByteWidth,
            .Usage = D3D11_USAGE_STAGING,
            .BindFlags = 0,
            .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
            .MiscFlags = 0,
            .StructureByteStride = 0,
        };
        const HRESULT result = device->CreateBuffer(&desc, nullptr, staging[slot].GetAddressOf());
        if (FAILED(result)) return fail(std::format("b{} staging creation failed (0x{:08X})", slot, static_cast<uint32_t>(result)));
        byte_widths[slot] = source_desc.ByteWidth;
      }
    }

    // Baseline D3D11 requires whole constant-buffer copies. Later uploads or
    // deferred-list execution cannot change these already queued snapshots.
    for (uint32_t slot = 0; slot < buffers.size(); ++slot) {
      context->CopyResource(staging[slot].Get(), buffers[slot].Get());
    }
    pending_context = context;
    pending_offsets = byte_offsets;
    pending_sizes = destination_sizes;
    return true;
  }

  bool Complete(ID3D11DeviceContext* context, const std::array<std::span<std::byte>, 2>& destinations) {
    error.clear();
    const auto fail = [this](const std::string& message) {
      error = message;
      return false;
    };
    if (!pending_context) return fail("no constant readback is pending");

    // Consume before validating: every completion attempt, including a failed
    // one, retires its snapshot so a later frame can never read stale staging.
    auto queued_context = std::move(pending_context);
    const auto byte_offsets = std::exchange(pending_offsets, {});
    const auto destination_sizes = std::exchange(pending_sizes, {});
    if (context == nullptr || context != queued_context.Get()
        || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
      return fail("constant readback completion context changed");
    }
    Microsoft::WRL::ComPtr<ID3D11Device> current_device;
    context->GetDevice(current_device.GetAddressOf());
    if (!current_device || current_device != device) return fail("constant readback completion device changed");
    for (uint32_t slot = 0; slot < destinations.size(); ++slot) {
      if (destinations[slot].size() != destination_sizes[slot]) {
        return fail(std::format("b{} completion size changed: queued={}, requested={}",
                                slot, destination_sizes[slot], destinations[slot].size()));
      }
    }

    for (uint32_t slot = 0; slot < destinations.size(); ++slot) {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      const HRESULT result = context->Map(staging[slot].Get(), 0, D3D11_MAP_READ, 0, &mapped);
      if (FAILED(result)) return fail(std::format("b{} staging Map failed (0x{:08X})", slot, static_cast<uint32_t>(result)));
      const bool readable = mapped.pData != nullptr;
      if (readable) {
        std::memcpy(destinations[slot].data(), static_cast<const std::byte*>(mapped.pData) + byte_offsets[slot],
                    destinations[slot].size());
      }
      context->Unmap(staging[slot].Get(), 0);
      if (!readable) return fail(std::format("b{} staging Map returned no data", slot));
    }
    return true;
  }

  bool Read(ID3D11DeviceContext* context,
            const std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, 2>& buffers,
            const std::array<uint32_t, 2>& first_constants,
            const std::array<uint32_t, 2>& constant_counts,
            const std::array<std::span<std::byte>, 2>& destinations) {
    return Queue(context, buffers, first_constants, constant_counts,
                 {destinations[0].size(), destinations[1].size()})
           && Complete(context, destinations);
  }
};

}  // namespace expeditionsrome::dlss
