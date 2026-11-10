/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <windows.h>

#include <cstdint>
#ifdef RENODX_TW_TEST_GPU_WAITS
#include <array>
#include <cstdlib>
#endif

namespace twattila::dlaa_helper {

#ifdef RENODX_TW_TEST_GPU_WAITS
// A test build of the helper only (build.ps1 run with CL=-DRENODX_TW_TEST_GPU_WAITS, never shipped): `variable`=<n> in
// the environment makes every n-th wait of its kind in this process count as not done in time on a live device, which
// no harness can bring about on demand. RENODX_TW_TEST_GPU_TIMEOUT: WaitForGpu (a request's wait; one before a release
// then sees the fence done and goes on). RENODX_TW_TEST_GPU_RELEASE_STALL: WaitForGpuBeforeRelease, to its end.
inline bool TestWaitFails(const wchar_t* variable, uint32_t* waits) {
  std::array<wchar_t, 16> value = {};
  if (GetEnvironmentVariableW(variable, value.data(), static_cast<DWORD>(value.size())) == 0) return false;
  const uint32_t every = std::wcstoul(value.data(), nullptr, 10);
  return every != 0 && ++*waits % every == 0;
}
#endif

// True: nothing submitted up to `value` can still run. The fence passed it, or its device was removed (the fence then
// reads all ones: a GPU reset, and the owner makes a new device), or there is no fence (nothing was ever submitted).
// False: not proven within `timeout` on a live device; the caller must not reset or free what that work uses.
// Fence is a native D3D12 fence in production; the narrow template also permits deterministic registration-failure
// and removed-device controls in the test.
template <typename Fence>
bool WaitForGpu(Fence* fence, HANDLE event, uint64_t value, DWORD timeout) {
#ifdef RENODX_TW_TEST_GPU_WAITS
  if (static uint32_t waits = 0; fence != nullptr && TestWaitFails(L"RENODX_TW_TEST_GPU_TIMEOUT", &waits)) return false;
#endif
  if (fence == nullptr || fence->GetCompletedValue() >= value) return true;
  if (event == nullptr) return false;
  const ULONGLONG started = GetTickCount64();
  // A removal between the read above and here fails the registration.
  if (FAILED(fence->SetEventOnCompletion(value, event))) return fence->GetCompletedValue() >= value;
  for (;;) {
    if (fence->GetCompletedValue() >= value) return true;
    const ULONGLONG elapsed = GetTickCount64() - started;
    if (elapsed >= timeout) return false;
    if (WaitForSingleObject(event, static_cast<DWORD>(timeout - elapsed)) != WAIT_OBJECT_0) return false;
    // An older registration can wake this event. Only the fence proves completion.
  }
}

// Called when work that is about to lose its resources was not proven done on a live device. The helper exe sets it
// (main.cpp: one log line, then the process ends without unwinding its GPU owners). It stays null in a game's process
// (Tales of Arise's DLSS add-on runs rr_d3d12.hpp there), which nothing here may end: the owner then goes on.
inline void (*unproven_gpu_completion)(const char* owner, uint64_t completed, uint64_t wanted) = nullptr;

// The wait before an owner frees what its submitted work uses (Close, a feature's release, a destructor).
template <typename Fence>
void WaitForGpuBeforeRelease(Fence* fence, HANDLE event, uint64_t value, DWORD timeout, const char* owner) {
#ifdef RENODX_TW_TEST_GPU_WAITS
  if (static uint32_t releases = 0; unproven_gpu_completion != nullptr && TestWaitFails(L"RENODX_TW_TEST_GPU_RELEASE_STALL", &releases)) {
    unproven_gpu_completion(owner, 0, value);
  }
#endif
  if (WaitForGpu(fence, event, value, timeout) || unproven_gpu_completion == nullptr) return;
  // Done or removed meanwhile: nothing to stop for.
  if (const uint64_t completed = fence->GetCompletedValue(); completed < value) {
    unproven_gpu_completion(owner, completed, value);
  }
}

// The game's GPU waits on a shared fence for values the helper that stopped will never signal: each of the helper's
// (even) signalled from the CPU once the game's own before it (odd) arrived, up to `value`. The game's queued work stays
// in order, and a CPU signal cannot queue behind the wait it frees. True: the fence is at `value` (also a removed
// device's, at all ones); false: the game's signal did not come within `timeout`.
template <typename Fence>
bool SignalAbandoned(Fence* fence, uint64_t value, DWORD timeout) {
  const ULONGLONG started = GetTickCount64();
  for (uint64_t completed = fence->GetCompletedValue(); completed < value; completed = fence->GetCompletedValue()) {
    if (GetTickCount64() - started >= timeout) return false;
    if ((completed & 1u) != 0 && FAILED(fence->Signal(completed + 1))) return false;
    Sleep(1);
  }
  return true;
}

}  // namespace twattila::dlaa_helper
