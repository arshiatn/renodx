/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace twattila::dlaa_helper {

// Do not unwind GPU owners after an unproven completion. The bridge recovers its
// fences only after observing helper process exit, so no old producer survives.
[[noreturn]] inline void StopGpuProcess(const char* operation) {
  OutputDebugStringA(operation);
  TerminateProcess(GetCurrentProcess(), ERROR_DEVICE_HARDWARE_ERROR);
  std::_Exit(ERROR_DEVICE_HARDWARE_ERROR);
}

inline void RequireGpuResult(HRESULT result, const char* operation) {
  if (FAILED(result)) StopGpuProcess(operation);
}

// Fence is a native D3D12 fence in production; the narrow template also permits
// deterministic registration-failure and removed-device controls in the test.
template <typename Fence>
bool WaitForGpu(Fence* fence, HANDLE event, uint64_t value, DWORD timeout) {
  if (fence == nullptr || event == nullptr) return false;
  auto completed = fence->GetCompletedValue();
  if (completed == std::numeric_limits<uint64_t>::max()) return false;
  if (completed >= value) return true;
  const ULONGLONG started = GetTickCount64();
  if (FAILED(fence->SetEventOnCompletion(value, event))) return false;
  for (;;) {
    completed = fence->GetCompletedValue();
    if (completed == std::numeric_limits<uint64_t>::max()) return false;
    if (completed >= value) return true;
    const ULONGLONG elapsed = GetTickCount64() - started;
    if (elapsed >= timeout) return false;
    if (WaitForSingleObject(event, static_cast<DWORD>(timeout - elapsed)) != WAIT_OBJECT_0) return false;
    // An older registration can wake this event. Only the fence proves completion.
  }
}

template <typename Fence>
void RequireGpuCompletion(Fence* fence, HANDLE event, uint64_t value, DWORD timeout) {
  if (!WaitForGpu(fence, event, value, timeout)) StopGpuProcess("Total War helper: GPU completion failed\n");
}

}  // namespace twattila::dlaa_helper
