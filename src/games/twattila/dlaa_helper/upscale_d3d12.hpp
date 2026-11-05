/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "../dlaa_bridge.h"
#include "./rr_d3d12.hpp"
// AMD's FSR SDK (its signed DLLs and these headers: MIT; build.ps1 adds them when build\downloads\fsr holds it): loaded
// at run time, its amd_fidelityfx_loader_dx12.dll and amd_fidelityfx_upscaler_dx12.dll next to the helper.
#if __has_include(<ffx_api.h>)
#include <dx12/ffx_api_dx12.h>
#include <ffx_api_loader.h>
#include <ffx_upscale.h>
#define RENODX_FSR_SDK 1
#endif
// Intel's XeSS SDK (Intel Simplified Software License; build.ps1 adds it when build\downloads\xess holds it): loaded at run
// time, its libxess.dll next to the helper, which needs the Visual C++ 2015-2022 runtime.
#if __has_include(<xess/xess_d3d12.h>)
#include <xess/xess_d3d12.h>
#define RENODX_XESS_SDK 1
#endif

// The DLAA helper's other upscalers, on the shared textures DLSS reads (linear colour, depth 0 near, motion in render
// pixels toward the last frame, un-jittered), as Ray Reconstruction runs (rr_d3d12.hpp): a D3D12 queue of its own waits for
// the addon's fence value, runs them and signals the next. The jitter is the bridge's as it is: the picture's move in
// pixels (x right, y down), the formula AMD and Intel both document (a shear of +2 x / width, -2 y / height in clip space).
namespace twattila::dlaa_helper {

class Upscaler12 {
 public:
  // A D3D12 device on the game's GPU and `backend`'s DLL (dlaa_bridge::UPSCALER_*) from `folder`; false: unavailable (the
  // status says why).
  bool Initialize(IDXGIAdapter1* adapter, const std::wstring& folder, uint32_t backend) {
    if (failed_) return false;
    if (device_) return true;
    backend_ = backend;
    // Not for good: tried again while it is asked for (MakeOwnDevice).
    ComPtr<ID3D12Device> device;
    if (const HRESULT made = MakeOwnDevice(adapter, &device); FAILED(made)) {
      status_ = std::format("Upscaling: no D3D12 device on the game's GPU (0x{:08X})", static_cast<uint32_t>(made));
      return false;
    }
    const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue_.GetAddressOf())))
        || FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(own_fence_.GetAddressOf())))) {
      return Fail("Upscaling: no D3D12 queue");
    }
    for (Slot& slot : slots_) {
      if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(slot.allocator.GetAddressOf())))
          || FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                              IID_PPV_ARGS(slot.list.GetAddressOf())))
          || FAILED(slot.list->Close())) {
        return Fail("Upscaling: no D3D12 command lists");
      }
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (backend == dlaa_bridge::UPSCALER_FSR) {
#if defined(RENODX_FSR_SDK)
      library_ = LoadLibraryExW((folder + L"\\amd_fidelityfx_loader_dx12.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (library_ == nullptr) return Fail("AMD FSR Upscaling: amd_fidelityfx_loader_dx12.dll not found next to the helper");
      ffxLoadFunctions(&fsr_, library_);
      if (fsr_.CreateContext == nullptr || fsr_.DestroyContext == nullptr || fsr_.Query == nullptr || fsr_.Dispatch == nullptr) {
        return Fail("AMD FSR Upscaling: amd_fidelityfx_loader_dx12.dll is not the expected one");
      }
#else
      return Fail("AMD FSR Upscaling is not in this helper's build");
#endif
    } else if (backend == dlaa_bridge::UPSCALER_XESS) {
#if defined(RENODX_XESS_SDK)
      const std::wstring path = folder + L"\\libxess.dll";
      library_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (library_ == nullptr) {
        // Its file there, what it imports not.
        return Fail(GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES ? "XeSS-SR: install the Visual C++ 2015-2022 x64 runtime"
                                                                                : "XeSS-SR: libxess.dll not found next to the helper");
      }
      const auto load = [this](auto* function, const char* name) {
        *function = reinterpret_cast<std::remove_pointer_t<decltype(function)>>(GetProcAddress(library_, name));
        return *function != nullptr;
      };
      if (!load(&xess_.create, "xessD3D12CreateContext") || !load(&xess_.build, "xessD3D12BuildPipelines") || !load(&xess_.init, "xessD3D12Init")
          || !load(&xess_.execute, "xessD3D12Execute") || !load(&xess_.destroy, "xessDestroyContext")
          || !load(&xess_.optimal, "xessGetOptimalInputResolution") || !load(&xess_.version, "xessGetVersion")) {
        return Fail("XeSS-SR: libxess.dll is not the expected one");
      }
#else
      return Fail("XeSS-SR is not in this helper's build");
#endif
    } else {
      return Fail("Upscaling: an unknown upscaler");
    }
    device_ = std::move(device);
    status_ = "Upscaling ready";
    return true;
  }

  // The addon's textures (handles still open) and fence; false: not all could be opened.
  bool Open(const std::array<uint64_t, UPSCALE_INPUTS>& textures, uint64_t fence) {
    Close();
    if (!device_) return false;
    for (size_t index = 0; index < UPSCALE_INPUTS; ++index) {
      if (FAILED(device_->OpenSharedHandle(reinterpret_cast<HANDLE>(textures[index]), IID_PPV_ARGS(inputs_[index].GetAddressOf())))) {
        Close();
        return false;
      }
    }
    if (FAILED(device_->OpenSharedHandle(reinterpret_cast<HANDLE>(fence), IID_PPV_ARGS(fence_.GetAddressOf())))) {
      Close();
      return false;
    }
    first_frame_ = true;
    return true;
  }

  // Its commands done, the textures let go; the context stays for a next set of the same output size.
  void Close() {
    if (own_fence_) WaitFor(own_value_);
    inputs_ = {};
    fence_.Reset();
  }

  // One request: the queue waits for the addon's fence value, the upscaler runs, the next value is signalled (also when it
  // could not run, so the game never waits for nothing). False: it did not run; the first requests don't (its context is made
  // beside them: a cold start of FSR 4 or of XeSS's kernels is long, and over 20 s the addon would give up on the helper).
  bool Evaluate(const Request& request) {
    if (!fence_ || !queue_) return false;
    Slot& slot = slots_[next_slot_];
    next_slot_ = (next_slot_ + 1) % SLOTS;
    WaitFor(slot.value);
    bool evaluated = false;
    const bool recorded = [&] {
      if (failed_) return false;
      if (maker_.joinable()) {
        if (making_.load()) return false;
        maker_.join();
        if (!make_error_.empty()) return Fail(make_error_.c_str());
#if defined(RENODX_FSR_SDK)
        if (backend_ == dlaa_bridge::UPSCALER_FSR) {
          ffxQueryGetProviderVersion provider = {.header = {.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION}};
          fsr_.Query(&fsr_context_, &provider.header);
          status_ = std::format("AMD FSR Upscaling {} active (started in {:.1f} s)", (provider.versionName != nullptr ? provider.versionName : "?"), make_seconds_);
        }
#endif
        first_frame_ = true;
      }
      // FSR: a context for each output size (any render size up to it). XeSS: one context, initialized for each output size
      // and the setting that takes the render size (below).
      if (made_width_ == 0 || (backend_ == dlaa_bridge::UPSCALER_FSR && (made_width_ != request.output_width || made_height_ != request.output_height))) {
        WaitFor(own_value_);
        DestroyContext();
        made_width_ = request.output_width;
        made_height_ = request.output_height;
        status_ = "Upscaling starting";
        making_.store(true);
        maker_ = std::thread([this] {
          const auto started = std::chrono::steady_clock::now();
#if defined(RENODX_XESS_SDK)
          if (backend_ == dlaa_bridge::UPSCALER_XESS) {
            if (const xess_result_t made = xess_.create(device_.Get(), &xess_context_); made < XESS_RESULT_SUCCESS) {
              make_error_ = std::format("XeSS-SR could not start on this GPU or driver (code {})", static_cast<int>(made));
            } else if (const xess_result_t built = xess_.build(xess_context_, nullptr, true, XESS_FLAGS); built < XESS_RESULT_SUCCESS) {
              make_error_ = std::format("XeSS-SR could not build its pipelines (code {})", static_cast<int>(built));
            }
          }
#endif
#if defined(RENODX_FSR_SDK)
          ffxCreateBackendDX12Desc backend = {.header = {.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12}, .device = device_.Get()};
          ffxCreateContextDescUpscaleVersion version = {
              .header = {.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION, .pNext = &backend.header},
              .version = FFX_UPSCALER_VERSION,
          };
          ffxCreateContextDescUpscale create = {
              .header = {.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE, .pNext = &version.header},
              // No depth flags: 0 near, a far plane. Dynamic resolution: any render size up to the output's.
              .flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION,
              .maxRenderSize = {.width = made_width_, .height = made_height_},
              .maxUpscaleSize = {.width = made_width_, .height = made_height_},
          };
          if (backend_ == dlaa_bridge::UPSCALER_FSR) {
            if (const ffxReturnCode_t made = fsr_.CreateContext(&fsr_context_, &create.header, nullptr); made != FFX_API_RETURN_OK) {
              make_error_ = std::format("AMD FSR Upscaling could not start (code {})", made);
            }
          }
#endif
          make_seconds_ = std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count();
          making_.store(false);
        });
        return false;
      }
#if defined(RENODX_XESS_SDK)
      if (backend_ == dlaa_bridge::UPSCALER_XESS) {
        const xess_2d_t output = {.x = request.output_width, .y = request.output_height};
        const auto takes = [&](xess_quality_settings_t quality) {
          xess_2d_t optimal = {};
          xess_2d_t least = {};
          xess_2d_t most = {};
          return xess_.optimal(xess_context_, &output, quality, &optimal, &least, &most) >= XESS_RESULT_SUCCESS && request.width >= least.x
                 && request.width <= most.x && request.height >= least.y && request.height <= most.y;
        };
        const bool new_output = inited_width_ != request.output_width || inited_height_ != request.output_height;
        if (new_output || !takes(quality_)) {
          // The first setting that takes it, the last when none does.
          xess_quality_settings_t quality = XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
          for (const auto candidate : {XESS_QUALITY_SETTING_AA, XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS, XESS_QUALITY_SETTING_ULTRA_QUALITY,
                                       XESS_QUALITY_SETTING_QUALITY, XESS_QUALITY_SETTING_BALANCED, XESS_QUALITY_SETTING_PERFORMANCE}) {
            if (takes(candidate)) {
              quality = candidate;
              break;
            }
          }
          if (new_output || quality != quality_) {
            // Here, not beside the requests (no frame without it: the render scale would go back to 100%, where another
            // setting is needed, for good): the kernels are built, this takes milliseconds.
            WaitFor(own_value_);
            const xess_d3d12_init_params_t init = {.outputResolution = output, .qualitySetting = quality, .initFlags = XESS_FLAGS};
            if (const xess_result_t inited = xess_.init(xess_context_, &init); inited < XESS_RESULT_SUCCESS) {
              return Fail(std::format("XeSS-SR could not start at this size (code {})", static_cast<int>(inited)).c_str());
            }
            quality_ = quality;
            inited_width_ = request.output_width;
            inited_height_ = request.output_height;
            first_frame_ = true;
            xess_version_t version = {};
            xess_.version(&version);
            status_ = std::format("XeSS-SR {}.{}.{} active (quality setting {}, started in {:.1f} s)", version.major, version.minor, version.patch,
                                  static_cast<int>(quality), make_seconds_);
          }
        }
      }
#endif
      ID3D12GraphicsCommandList* list = slot.list.Get();
      if (FAILED(slot.allocator->Reset()) || FAILED(list->Reset(slot.allocator.Get(), nullptr))) return Fail("Upscaling: D3D12 stopped");
      std::array<D3D12_RESOURCE_BARRIER, UPSCALE_INPUTS> barriers;
      for (size_t index = 0; index < UPSCALE_INPUTS; ++index) {
        barriers[index] = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Transition = {
                .pResource = inputs_[index].Get(),
                .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                .StateBefore = D3D12_RESOURCE_STATE_COMMON,
                .StateAfter = (index == OUTPUT ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            },
        };
      }
      list->ResourceBarrier(UPSCALE_INPUTS, barriers.data());
      const bool reset = request.reset || first_frame_ || last_width_ != request.width || last_height_ != request.height;
#if defined(RENODX_XESS_SDK)
      if (backend_ == dlaa_bridge::UPSCALER_XESS) {
        const xess_d3d12_execute_params_t execute = {
            .pColorTexture = inputs_[COLOR].Get(),
            .pVelocityTexture = inputs_[MOTION].Get(),
            .pDepthTexture = inputs_[DEPTH].Get(),
            .pOutputTexture = inputs_[OUTPUT].Get(),
            .jitterOffsetX = request.jitter_x,
            .jitterOffsetY = request.jitter_y,
            .exposureScale = 1.f,
            .resetHistory = (reset ? 1u : 0u),
            .inputWidth = request.width,
            .inputHeight = request.height,
        };
        const xess_result_t executed = xess_.execute(xess_context_, list, &execute);
        evaluated = executed >= XESS_RESULT_SUCCESS;
        if (!evaluated) status_ = std::format("XeSS-SR: its evaluation failed at {} x {} (code {})", request.width, request.height, static_cast<int>(executed));
      }
#endif
#if defined(RENODX_FSR_SDK)
      // The camera from the row-vector matrix (the jitter only moves its column 2): near and far from its z row.
      const auto& projection = request.view_to_clip;
      const float near_plane = (projection[10] != 0.f ? -projection[14] / projection[10] : 0.09997847f);
      const auto now = std::chrono::steady_clock::now();
      const ffxDispatchDescUpscale dispatch = {
          .header = {.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE},
          .commandList = list,
          .color = ffxApiGetResourceDX12(inputs_[COLOR].Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ),
          .depth = ffxApiGetResourceDX12(inputs_[DEPTH].Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ),
          .motionVectors = ffxApiGetResourceDX12(inputs_[MOTION].Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ),
          .output = ffxApiGetResourceDX12(inputs_[OUTPUT].Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS),
          .jitterOffset = {.x = request.jitter_x, .y = request.jitter_y},
          .motionVectorScale = {.x = 1.f, .y = 1.f},
          .renderSize = {.width = request.width, .height = request.height},
          .upscaleSize = {.width = request.output_width, .height = request.output_height},
          .enableSharpening = false,
          .frameTimeDelta = (first_frame_ ? 16.698658f : std::chrono::duration<float, std::milli>(now - last_time_).count()),
          .preExposure = 1.f,
          .reset = reset,
          .cameraNear = near_plane,
          .cameraFar = (projection[10] != 1.f ? projection[10] * near_plane / (projection[10] - 1.f) : 100028.21f),
          .cameraFovAngleVertical = 2.f * std::atan(1.f / projection[5]),
          .viewSpaceToMetersFactor = 1.f,
      };
      last_time_ = now;
      if (backend_ == dlaa_bridge::UPSCALER_FSR) {
        evaluated = fsr_.Dispatch(&fsr_context_, &dispatch.header) == FFX_API_RETURN_OK;
        if (!evaluated) status_ = "AMD FSR Upscaling: its evaluation failed";
      }
#endif
      for (auto& barrier : barriers) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      list->ResourceBarrier(UPSCALE_INPUTS, barriers.data());
      if (FAILED(list->Close())) return Fail("Upscaling: D3D12 stopped");
      first_frame_ = false;
      last_width_ = request.width;
      last_height_ = request.height;
      return true;
    }();
    queue_->Wait(fence_.Get(), request.value);
    if (recorded) {
      ID3D12CommandList* lists[] = {slot.list.Get()};
      queue_->ExecuteCommandLists(1, lists);
    }
    queue_->Signal(fence_.Get(), request.value + 1);
    queue_->Signal(own_fence_.Get(), ++own_value_);
    slot.value = own_value_;
    return evaluated;
  }

  // A GPU reset removed its device (the helper makes a new one).
  [[nodiscard]] HRESULT RemovedReason() const { return device_ ? device_->GetDeviceRemovedReason() : S_OK; }
  [[nodiscard]] bool Lost() const { return FAILED(RemovedReason()); }
  [[nodiscard]] uint32_t Backend() const { return backend_; }
  [[nodiscard]] const std::string& Status() const { return status_; }

  ~Upscaler12() {
    if (maker_.joinable()) maker_.join();
    if (own_fence_) WaitFor(own_value_);
    DestroyContext();
    if (event_ != nullptr) CloseHandle(event_);
    if (library_ != nullptr) FreeLibrary(library_);
  }

 private:
  struct Slot {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    uint64_t value = 0;  // own_fence_'s value after its last use
  };

  bool Fail(const char* status) {
    failed_ = true;
    status_ = status;
    return false;
  }

  // The CPU waits until this queue passed `value` (a slot's last use: SLOTS requests back, normally long done).
  void WaitFor(uint64_t value) {
    if (own_fence_->GetCompletedValue() >= value) return;
    own_fence_->SetEventOnCompletion(value, event_);
    WaitForSingleObject(event_, 5000);
  }

  void DestroyContext() {
#if defined(RENODX_FSR_SDK)
    if (fsr_context_ != nullptr) fsr_.DestroyContext(&fsr_context_, nullptr);
    fsr_context_ = nullptr;
#endif
#if defined(RENODX_XESS_SDK)
    if (xess_context_ != nullptr) xess_.destroy(xess_context_);
    xess_context_ = nullptr;
#endif
  }

  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  std::array<Slot, SLOTS> slots_;
  size_t next_slot_ = 0;
  ComPtr<ID3D12Fence> own_fence_;  // this queue's progress
  uint64_t own_value_ = 0;
  HANDLE event_ = nullptr;
  std::array<ComPtr<ID3D12Resource>, UPSCALE_INPUTS> inputs_;
  ComPtr<ID3D12Fence> fence_;  // the addon's
  uint32_t backend_ = dlaa_bridge::UPSCALER_DLSS;
  HMODULE library_ = nullptr;
#if defined(RENODX_FSR_SDK)
  ffxFunctions fsr_ = {};
  ffxContext fsr_context_ = nullptr;
#endif
#if defined(RENODX_XESS_SDK)
  // No depth flag (0 near), no high-resolution or jittered motion; HDR colour is its default.
  static constexpr uint32_t XESS_FLAGS = XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
  struct {
    decltype(&xessD3D12CreateContext) create = nullptr;
    decltype(&xessD3D12BuildPipelines) build = nullptr;
    decltype(&xessD3D12Init) init = nullptr;
    decltype(&xessD3D12Execute) execute = nullptr;
    decltype(&xessDestroyContext) destroy = nullptr;
    decltype(&xessGetOptimalInputResolution) optimal = nullptr;
    decltype(&xessGetVersion) version = nullptr;
  } xess_;
  xess_context_handle_t xess_context_ = nullptr;
  xess_quality_settings_t quality_ = XESS_QUALITY_SETTING_AA;
  uint32_t inited_width_ = 0;
  uint32_t inited_height_ = 0;
#endif
  // The context's maker: it writes the context, make_error_ and make_seconds_; read after the join.
  std::thread maker_;
  std::atomic<bool> making_ = false;
  std::string make_error_;
  float make_seconds_ = 0.f;
  uint32_t made_width_ = 0;
  uint32_t made_height_ = 0;
  uint32_t last_width_ = 0;
  uint32_t last_height_ = 0;
  std::chrono::steady_clock::time_point last_time_;
  bool failed_ = false;
  bool first_frame_ = true;
  std::string status_ = "Upscaling off";
};

}  // namespace twattila::dlaa_helper
