/*
 * Copyright (C) 2026 RenoDX contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include <d3d11.h>
#include <wrl/client.h>

namespace rysesonofrome::dlss {

struct PerformanceSummary {
  // Stock TAA, preparation, NGX, and merge/copy GPU execution intervals.
  // The three gaps between timestamp pairs are deliberately excluded.
  std::array<double, 4> gpu_ms = {};
  double cpu_readback_ms = 0;
  double cpu_evaluate_ms = 0;
  uint64_t gpu_samples = 0;
  uint64_t cpu_samples = 0;
  uint64_t dropped = 0;
  uint64_t rejected = 0;
  uint32_t pending = 0;
  bool available = false;
};

// Also usable with recorded timestamps: malformed/disjoint samples never enter
// the averages. CPU submission duration is not a measurement of GPU model time.
struct PerformanceStatistics {
  PerformanceSummary summary;

  bool GpuSample(uint64_t frequency, const std::array<uint64_t, 8>& timestamps, bool disjoint) {
    if (disjoint || frequency == 0 || summary.gpu_samples == (std::numeric_limits<uint64_t>::max)()) {
      ++summary.rejected;
      return false;
    }
    for (uint32_t index = 1; index < timestamps.size(); ++index) {
      if (timestamps[index] < timestamps[index - 1]) {
        ++summary.rejected;
        return false;
      }
    }
    ++summary.gpu_samples;
    for (uint32_t index = 0; index < summary.gpu_ms.size(); ++index) {
      const double duration = static_cast<double>(timestamps[index * 2 + 1] - timestamps[index * 2]) * 1000.0
                              / static_cast<double>(frequency);
      summary.gpu_ms[index] += (duration - summary.gpu_ms[index]) / static_cast<double>(summary.gpu_samples);
    }
    return true;
  }

  bool CpuSample(double readback_ms, double evaluate_ms) {
    if (!std::isfinite(readback_ms) || !std::isfinite(evaluate_ms) || readback_ms < 0 || evaluate_ms < 0
        || summary.cpu_samples == (std::numeric_limits<uint64_t>::max)()) {
      ++summary.rejected;
      return false;
    }
    ++summary.cpu_samples;
    summary.cpu_readback_ms += (readback_ms - summary.cpu_readback_ms) / static_cast<double>(summary.cpu_samples);
    summary.cpu_evaluate_ms += (evaluate_ms - summary.cpu_evaluate_ms) / static_cast<double>(summary.cpu_samples);
    return true;
  }
};

// The adapter serializes access. Eight samples may be in flight; pressure drops
// instrumentation rather than waiting or flushing rendering work. Contexts must
// be native immediate contexts. No pipeline bindings or resources are modified.
class Performance {
 private:
  enum class SlotState : uint8_t {
    FREE,
    ACTIVE,
    PENDING,
  };

  struct Slot {
    Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
    std::array<Microsoft::WRL::ComPtr<ID3D11Query>, 8> timestamps;
    SlotState state = SlotState::FREE;
    uint32_t timestamp_count = 0;
    bool valid = false;
  };

  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> active_context_;
  std::array<Slot, 8> slots_;
  PerformanceStatistics statistics_;
  uint32_t active_ = 8;
  bool unavailable_ = false;

 public:
  Performance() = default;
  Performance(const Performance&) = delete;
  Performance& operator=(const Performance&) = delete;
  Performance(Performance&& other) noexcept {
    *this = std::move(other);
  }
  Performance& operator=(Performance&& other) noexcept {
    if (this == &other) return *this;
    End(nullptr);
    device_ = std::move(other.device_);
    active_context_ = std::move(other.active_context_);
    slots_ = std::move(other.slots_);
    statistics_ = std::move(other.statistics_);
    active_ = std::exchange(other.active_, static_cast<uint32_t>(slots_.size()));
    unavailable_ = std::exchange(other.unavailable_, false);
    other.slots_ = {};
    other.statistics_ = {};
    return *this;
  }
  ~Performance() {
    End(nullptr);
  }

  bool Begin(ID3D11DeviceContext* context) {
    if (context == nullptr || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
    // An interrupted/overlapping capture is closed before another can begin.
    End(nullptr);
    Microsoft::WRL::ComPtr<ID3D11Device> current_device;
    context->GetDevice(current_device.GetAddressOf());
    if (!current_device) return false;
    if (device_ != current_device) {
      slots_ = {};
      statistics_ = {};
      device_ = current_device;
      unavailable_ = false;
    }
    if (unavailable_) return false;
    uint32_t index = 0;
    while (index < slots_.size() && slots_[index].state != SlotState::FREE) ++index;
    if (index == slots_.size()) {
      ++statistics_.summary.dropped;
      return false;
    }
    auto& slot = slots_[index];
    if (!slot.disjoint) {
      const D3D11_QUERY_DESC desc = {.Query = D3D11_QUERY_TIMESTAMP_DISJOINT, .MiscFlags = 0};
      if (FAILED(device_->CreateQuery(&desc, slot.disjoint.GetAddressOf()))) {
        unavailable_ = true;
        statistics_.summary.available = false;
        return false;
      }
      for (auto& timestamp : slot.timestamps) {
        const D3D11_QUERY_DESC timestamp_desc = {.Query = D3D11_QUERY_TIMESTAMP, .MiscFlags = 0};
        if (FAILED(device_->CreateQuery(&timestamp_desc, timestamp.GetAddressOf()))) {
          unavailable_ = true;
          statistics_.summary.available = false;
          return false;
        }
      }
    }
    statistics_.summary.available = true;
    slot.state = SlotState::ACTIVE;
    slot.timestamp_count = 1;
    slot.valid = true;
    active_ = index;
    active_context_ = context;
    context->Begin(slot.disjoint.Get());
    context->End(slot.timestamps[0].Get());
    return true;
  }

  void Mark(ID3D11DeviceContext* context) {
    if (active_ == slots_.size()) return;
    auto& slot = slots_[active_];
    if (context != active_context_.Get() || slot.timestamp_count >= slot.timestamps.size() - 1) {
      slot.valid = false;
      return;
    }
    context->End(slot.timestamps[slot.timestamp_count].Get());
    ++slot.timestamp_count;
  }

  void End(ID3D11DeviceContext* context) {
    if (active_ == slots_.size()) return;
    auto& slot = slots_[active_];
    slot.valid = slot.valid && context == active_context_.Get() && slot.timestamp_count == 7;
    if (slot.valid) {
      active_context_->End(slot.timestamps[7].Get());
      slot.timestamp_count = 8;
    } else {
      ++statistics_.summary.rejected;
    }
    // The owned context also closes early-return or wrong-context captures.
    active_context_->End(slot.disjoint.Get());
    slot.state = SlotState::PENDING;
    active_ = static_cast<uint32_t>(slots_.size());
    active_context_.Reset();
  }

  void Poll(ID3D11DeviceContext* context) {
    if (context == nullptr || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
    Microsoft::WRL::ComPtr<ID3D11Device> current_device;
    context->GetDevice(current_device.GetAddressOf());
    if (current_device != device_) return;
    for (auto& slot : slots_) {
      if (slot.state != SlotState::PENDING) continue;
      D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
      const HRESULT result = context->GetData(slot.disjoint.Get(), &disjoint, sizeof(disjoint),
                                              D3D11_ASYNC_GETDATA_DONOTFLUSH);
      if (result == S_FALSE) continue;
      if (FAILED(result)) {
        if (slot.valid) ++statistics_.summary.rejected;
        slot.state = SlotState::FREE;
        continue;
      }
      if (!slot.valid) {
        slot.state = SlotState::FREE;
        continue;
      }
      std::array<uint64_t, 8> timestamps = {};
      bool ready = true;
      for (uint32_t index = 0; index < timestamps.size(); ++index) {
        const HRESULT timestamp_result = context->GetData(slot.timestamps[index].Get(), &timestamps[index],
                                                          sizeof(timestamps[index]), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (timestamp_result == S_FALSE) {
          ready = false;
          break;
        }
        if (FAILED(timestamp_result)) {
          ++statistics_.summary.rejected;
          slot.state = SlotState::FREE;
          ready = false;
          break;
        }
      }
      if (!ready) continue;
      statistics_.GpuSample(disjoint.Frequency, timestamps, disjoint.Disjoint != FALSE);
      slot.state = SlotState::FREE;
    }
  }

  void CpuSample(double readback_ms, double evaluate_ms) {
    statistics_.CpuSample(readback_ms, evaluate_ms);
  }

  PerformanceSummary Snapshot() const {
    auto summary = statistics_.summary;
    for (const auto& slot : slots_) {
      if (slot.state != SlotState::FREE) ++summary.pending;
    }
    return summary;
  }
};

}  // namespace rysesonofrome::dlss
