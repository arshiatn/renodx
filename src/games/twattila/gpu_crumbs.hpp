#pragma once

// RENODX_TW_WATCHDOG (tests): where the game's GPU got to, read by a watchdog while it hangs. A fence signalled before each draw of the
// immediate context, with what comes next in a ring.

#include <windows.h>
#include <d3d11_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <vector>

namespace twattila::gpu_crumbs {

struct Entry {
  uint32_t kind = 0;
  uint32_t first = 0;
  uint32_t second = 0;
  uint32_t count = 0;
};

inline constexpr uint64_t RING = 1 << 16;
inline std::array<Entry, RING> ring = {};
inline std::atomic<uint64_t> next = 0;
inline Microsoft::WRL::ComPtr<ID3D11Fence> fence;
inline Microsoft::WRL::ComPtr<ID3D11DeviceContext4> immediate;
inline const bool on = GetEnvironmentVariableW(L"RENODX_TW_WATCHDOG", nullptr, 0) != 0;
inline uint32_t EnvNumber(const wchar_t* name, uint32_t fallback) {
  std::array<wchar_t, 16> text = {};
  return (GetEnvironmentVariableW(name, text.data(), static_cast<DWORD>(text.size())) != 0) ? static_cast<uint32_t>(_wtoi(text.data())) : fallback;
}
inline const std::vector<uint32_t> only = [] {
  std::vector<uint32_t> found;
  std::array<wchar_t, 512> text = {};
  if (GetEnvironmentVariableW(L"RENODX_TW_CRUMB_ONLY", text.data(), static_cast<DWORD>(text.size())) != 0) {
    for (const wchar_t* at = text.data(); *at != L'\0';) {
      wchar_t* end = nullptr;
      found.push_back(static_cast<uint32_t>(wcstoul(at, &end, 16)));
      at = (*end == L'+') ? end + 1 : end;
    }
  }
  return found;
}();
inline const uint32_t signals = EnvNumber(L"RENODX_TW_CRUMB_SIGNAL", 1);
inline const uint32_t flush_period = EnvNumber(L"RENODX_TW_CRUMB_FLUSH", 8);
inline const uint32_t kinds_low = EnvNumber(L"RENODX_TW_CRUMB_LOW", 0);
inline const uint32_t kinds_high = EnvNumber(L"RENODX_TW_CRUMB_HIGH", 1000);

inline void Mark(ID3D11DeviceContext* context, uint32_t kind, uint32_t first = 0, uint32_t second = 0, uint32_t count = 0) {
  if (!on || context == nullptr || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
  if (!fence) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Device5> device5;
    if (FAILED(device.As(&device5)) || FAILED(context->QueryInterface(IID_PPV_ARGS(immediate.GetAddressOf())))
        || FAILED(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf())))) {
      return;
    }
  }
  if (kind < kinds_low || kind > kinds_high) return;
  const uint64_t value = next.fetch_add(1) + 1;
  ring[value % RING] = {.kind = kind, .first = first, .second = second, .count = count};
  const bool listed = !only.empty() && std::ranges::find(only, first) != only.end();
  if (only.empty() ? (signals != 0 && value % signals == 0) : listed) immediate->Signal(fence.Get(), value);
  if (flush_period != 0 && (only.empty() ? value % flush_period == 0 : listed)) context->Flush();
}

}  // namespace twattila::gpu_crumbs
