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
#include <cstdlib>
#include <format>
#include <limits>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "../dlaa_bridge.h"
#include "./gpu_completion.hpp"
#include "./rr_d3d12.hpp"
// AMD's FSR SDK (build.ps1 adds it when build\downloads\fsr holds it): loaded at run time, its amd_fidelityfx_loader_dx12.dll
// and amd_fidelityfx_upscaler_dx12.dll next to the helper. Its Kits\FidelityFX\docs\license.md names these headers and its
// signedbin DLLs under MIT (the files it does not name: AMD's binary-only licence); it ships with the DLLs.
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
    if (event_ == nullptr) return Fail("Upscaling: no completion event");
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
      // A test path, off by default (xae_ 11-27): RENODX_TW_XESS_EXPOSURE, read here (auto exposure is an init flag: no live
      // toggle). A number s > 0: no auto exposure, exposureScale s (Intel's SR guide :321-332: auto exposure is for colour
      // not yet adjusted for the exposure; t00 is after the tone mapper). Anything else ("auto"): XESS_FLAGS as they are.
      // Either way XeSS's GPU time is stamped beside its dispatch and its mean goes into the status.
      if (std::array<char, 32> asked = {};
          GetEnvironmentVariableA("RENODX_TW_XESS_EXPOSURE", asked.data(), static_cast<DWORD>(asked.size())) != 0) {
        const float scale = std::strtof(asked.data(), nullptr);
        if (scale > 0.f) {
          xess_flags_ = XESS_FLAGS & ~static_cast<uint32_t>(XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE);
          xess_exposure_scale_ = scale;
        }
        const D3D12_QUERY_HEAP_DESC heap_desc = {.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP, .Count = static_cast<UINT>(SLOTS * 2)};
        const D3D12_HEAP_PROPERTIES readback_heap = {.Type = D3D12_HEAP_TYPE_READBACK};
        const D3D12_RESOURCE_DESC readback_desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, .Width = SLOTS * 2 * sizeof(uint64_t),
                                                   .Height = 1, .DepthOrArraySize = 1, .MipLevels = 1, .SampleDesc = {.Count = 1},
                                                   .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
        if (FAILED(device->CreateQueryHeap(&heap_desc, IID_PPV_ARGS(xess_timing_.stamps.GetAddressOf())))
            || FAILED(device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                      nullptr, IID_PPV_ARGS(xess_timing_.readback.GetAddressOf())))
            || FAILED(queue_->GetTimestampFrequency(&xess_timing_.ticks_per_second))) {
          return Fail("XeSS-SR: no GPU timestamps for RENODX_TW_XESS_EXPOSURE");
        }
        xess_timing_.mode = ((scale > 0.f) ? std::format("fixed exposure {:.3f}", scale) : std::string("auto exposure"));
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
    WaitForGpuBeforeRelease(own_fence_.Get(), event_, own_value_, GPU_WAIT_TIMEOUT, "FSR / XeSS upscaling");
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
    bool evaluated = false;
    const bool recorded = [&] {
      if (failed_) return false;
      // Its last use not proven done on a live device: its allocator still feeds the GPU, nothing is recorded. This request
      // only (the next asks again, its history new: the game's GPU can be seconds behind); the status stays, nothing here
      // writes the running one again.
      const auto unfinished = [&] {
        first_frame_ = true;
        return false;
      };
      if (!WaitForGpu(own_fence_.Get(), event_, slot.value, GPU_WAIT_TIMEOUT)) return unfinished();
#if defined(RENODX_XESS_SDK)
      // RENODX_TW_XESS_EXPOSURE: this slot's last XeSS dispatch is done (above), its two stamps are readable.
      if (std::exchange(slot.timed, false)) {
        const UINT stamp = static_cast<UINT>(&slot - slots_.data()) * 2;
        const D3D12_RANGE read = {.Begin = stamp * sizeof(uint64_t), .End = (stamp + 2) * sizeof(uint64_t)};
        if (void* mapped = nullptr; SUCCEEDED(xess_timing_.readback->Map(0, &read, &mapped))) {
          const uint64_t* ticks = static_cast<const uint64_t*>(mapped) + stamp;
          const double ms = 1000.0 * static_cast<double>(ticks[1] - ticks[0]) / static_cast<double>(xess_timing_.ticks_per_second);
          const D3D12_RANGE written = {};
          xess_timing_.readback->Unmap(0, &written);
          // The first dispatches after an init (its kernels' first runs) are left out.
          if (++xess_timing_.dispatches > XESS_TIMING_WARM_UP) {
            const uint32_t counted = xess_timing_.dispatches - XESS_TIMING_WARM_UP;
            xess_timing_.sum_ms += ms;
            xess_timing_.least_ms = ((counted == 1) ? ms : std::min(xess_timing_.least_ms, ms));
            xess_timing_.most_ms = ((counted == 1) ? ms : std::max(xess_timing_.most_ms, ms));
            status_ = std::format("{} | XeSS GPU ms {:.4f} mean of {} (min {:.4f}, max {:.4f}), {}", xess_timing_.active_status,
                                  xess_timing_.sum_ms / counted, counted, xess_timing_.least_ms, xess_timing_.most_ms, xess_timing_.mode);
          }
        }
      }
#endif
      if (maker_.joinable()) {
        if (making_.load()) return false;
        maker_.join();
        if (!make_error_.empty()) return Fail(make_error_.c_str());
#if defined(RENODX_FSR_SDK)
        if (backend_ == dlaa_bridge::UPSCALER_FSR) {
          ffxQueryGetProviderVersion provider = {.header = {.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION}};
          fsr_.Query(&fsr_context_, &provider.header);
          status_ = std::format("AMD FSR Upscaling {} active{} (started in {:.1f} s)", (provider.versionName != nullptr ? provider.versionName : "?"),
                                (made_infinite_far_ ? ", infinite depth" : ""), make_seconds_);
        }
#endif
        first_frame_ = true;
      }
      // FSR: a context for each output size (any render size up to it) and depth kind (UE's infinite far plane: the
      // projection's [10] is 1; it changes at load only). XeSS: one context, initialized for each output size and the setting
      // that takes the render size (below).
      const bool infinite_far = request.view_to_clip[10] == 1.f;
      if (made_width_ == 0
          || (backend_ == dlaa_bridge::UPSCALER_FSR
              && (made_width_ != request.output_width || made_height_ != request.output_height || made_infinite_far_ != infinite_far))) {
        if (!WaitForGpu(own_fence_.Get(), event_, own_value_, GPU_WAIT_TIMEOUT)) return unfinished();
        DestroyContext();
        made_width_ = request.output_width;
        made_height_ = request.output_height;
        made_infinite_far_ = infinite_far;
        status_ = "Upscaling starting";
        making_.store(true);
        maker_ = std::thread([this] {
          const auto started = std::chrono::steady_clock::now();
#if defined(RENODX_XESS_SDK)
          if (backend_ == dlaa_bridge::UPSCALER_XESS) {
            if (const xess_result_t made = xess_.create(device_.Get(), &xess_context_); made < XESS_RESULT_SUCCESS) {
              make_error_ = std::format("XeSS-SR could not start on this GPU or driver (code {})", static_cast<int>(made));
            } else if (const xess_result_t built = xess_.build(xess_context_, nullptr, true, xess_flags_); built < XESS_RESULT_SUCCESS) {
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
              // Depth 0 near (no DEPTH_INVERTED); DEPTH_INFINITE for an infinite far plane (src: ffx_upscale.h:53, FSR SDK
              // 2.4.0). Dynamic resolution: any render size up to the output's.
              .flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION
                       | (made_infinite_far_ ? static_cast<uint32_t>(FFX_UPSCALE_ENABLE_DEPTH_INFINITE) : 0u),
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
            if (!WaitForGpu(own_fence_.Get(), event_, own_value_, GPU_WAIT_TIMEOUT)) return unfinished();
            const xess_d3d12_init_params_t init = {.outputResolution = output, .qualitySetting = quality, .initFlags = xess_flags_};
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
            if (xess_timing_.stamps) {
              xess_timing_.active_status = status_;
              xess_timing_.dispatches = 0;
              xess_timing_.sum_ms = 0.0;
            }
          }
        }
      }
#endif
      ID3D12GraphicsCommandList* list = slot.list.Get();
      HRESULT list_reset = slot.allocator->Reset();
      if (SUCCEEDED(list_reset)) list_reset = list->Reset(slot.allocator.Get(), nullptr);
      if (FAILED(list_reset)) return Fail(std::format("Upscaling: D3D12 stopped (its list's reset 0x{:08X})", static_cast<uint32_t>(list_reset)));
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
            .exposureScale = xess_exposure_scale_,
            .resetHistory = (reset ? 1u : 0u),
            .inputWidth = request.width,
            .inputHeight = request.height,
        };
        const UINT stamp = static_cast<UINT>(&slot - slots_.data()) * 2;
        if (xess_timing_.stamps) {
          list->EndQuery(xess_timing_.stamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, stamp);
        }
        const xess_result_t executed = xess_.execute(xess_context_, list, &execute);
        if (xess_timing_.stamps) {
          list->EndQuery(xess_timing_.stamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, stamp + 1);
          list->ResolveQueryData(xess_timing_.stamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, stamp, 2, xess_timing_.readback.Get(),
                                 stamp * sizeof(uint64_t));
          slot.timed = true;
        }
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
          // Infinite: no far plane, not a made-up 100 km one (src: ffx_fsr3upscaler.cpp:717-756, its depth to view depth
          // under DEPTH_INFINITE takes the near plane alone).
          .cameraFar = (made_infinite_far_ ? std::numeric_limits<float>::max()
                                           : projection[10] * near_plane / (projection[10] - 1.f)),
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
      if (const HRESULT closed = list->Close(); FAILED(closed)) {
        return Fail(std::format("Upscaling: D3D12 stopped (its list's close 0x{:08X})", static_cast<uint32_t>(closed)));
      }
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
  // Its context still being made on its own thread: the destructor would wait for it.
  [[nodiscard]] bool Making() const { return making_.load(); }

  ~Upscaler12() {
    if (maker_.joinable()) maker_.join();
    WaitForGpuBeforeRelease(own_fence_.Get(), event_, own_value_, GPU_WAIT_TIMEOUT, "FSR / XeSS upscaling");
    DestroyContext();
    if (event_ != nullptr) CloseHandle(event_);
    if (library_ != nullptr) FreeLibrary(library_);
  }

 private:
  struct Slot {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    uint64_t value = 0;  // own_fence_'s value after its last use
    bool timed = false;  // RENODX_TW_XESS_EXPOSURE: its last list stamped XeSS's dispatch
  };

  bool Fail(std::string status) {
    failed_ = true;
    status_ = std::move(status);
    return false;
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
  // RENODX_TW_XESS_EXPOSURE (Initialize; a test path): unset, these stay XESS_FLAGS and 1 and no stamps are made.
  uint32_t xess_flags_ = XESS_FLAGS;
  float xess_exposure_scale_ = 1.f;
  static constexpr uint32_t XESS_TIMING_WARM_UP = 8;
  struct {
    ComPtr<ID3D12QueryHeap> stamps;  // two a slot: before and after XeSS's dispatch
    ComPtr<ID3D12Resource> readback;
    uint64_t ticks_per_second = 0;
    std::string mode;           // "auto exposure" / "fixed exposure <s>"
    std::string active_status;  // the status at its last init
    uint32_t dispatches = 0;    // read since that init
    double sum_ms = 0.0;
    double least_ms = 0.0;
    double most_ms = 0.0;
  } xess_timing_;
#endif
  // The context's maker: it writes the context, make_error_ and make_seconds_; read after the join.
  std::thread maker_;
  std::atomic<bool> making_ = false;
  std::string make_error_;
  float make_seconds_ = 0.f;
  uint32_t made_width_ = 0;
  uint32_t made_height_ = 0;
  bool made_infinite_far_ = false;  // its FSR context's DEPTH_INFINITE
  uint32_t last_width_ = 0;
  uint32_t last_height_ = 0;
  std::chrono::steady_clock::time_point last_time_;
  bool failed_ = false;
  bool first_frame_ = true;
  std::string status_ = "Upscaling off";
};

}  // namespace twattila::dlaa_helper
