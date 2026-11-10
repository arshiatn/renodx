/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gpu_completion.hpp"

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
#include "./fg_d3d12.hpp"
// AMD's FSR SDK (its signed DLLs and these headers: MIT; build.ps1 adds them when build\downloads\fsr holds it): loaded at
// run time, amd_fidelityfx_loader_dx12.dll and amd_fidelityfx_framegeneration_dx12.dll next to the helper.
#if __has_include(<ffx_framegeneration.h>)
#include <dx12/ffx_api_dx12.h>
#include <dx12/ffx_api_framegeneration_dx12.h>
#include <ffx_api_loader.h>
#include <ffx_framegeneration.h>
#define RENODX_FSR_FG 1
#endif
// Intel's XeSS SDK (Intel Simplified Software License; build.ps1 adds it when build\downloads\xess holds it): loaded at run
// time, libxess_fg.dll and libxell.dll next to the helper, which need the Visual C++ 2015-2022 runtime.
#if __has_include(<xess_fg/xefg_swapchain_d3d12.h>)
#include <xell/xell_d3d12.h>
#include <xess_fg/xefg_swapchain_d3d12.h>
#include <xess_fg/xefg_swapchain_debug.h>
#define RENODX_XESS_FG 1
#endif

// The DLAA helper's other frame generations (bridge v23): AMD's FSR and Intel's XeSS, each through its proxy swap chain on a
// D3D12 queue of this class, into a child window over the game's as fg_d3d12.hpp does for DLSS. Each request: the queue waits
// for the addon's fence value, copies the frame into the proxy's back buffer, hands over the depth, motion and HUD-less image,
// signals the next value and presents (the proxy adds its frame there, on this queue). XeSS copies its inputs in that list;
// FSR's prepare pass reads depth and motion in it, but it reads the HUD-less image at the present, after the game may have
// written the next one: hence the copy. The chain is made beside the requests (a cold start is long, and over 20 s the addon
// would give up on the helper): the game's own image shows until then, and whenever a size or format needs a new chain.
namespace twattila::dlaa_helper {

class ProxyFrameGeneration12 {
 public:
  // A D3D12 device on the game's GPU and `backend`'s DLLs (dlaa_bridge::FG_BACKEND_FSR or _XESS) from `folder`; false:
  // unavailable (the status says why).
  bool Initialize(IDXGIAdapter1* adapter, const std::wstring& folder, uint32_t backend) {
    if (failed_) return false;
    if (device_) return true;
    backend_ = backend;
    // Not for good: Windows refuses a process new devices for a while after GPU resets (tried again at the next set).
    ComPtr<ID3D12Device> device;
    if (const HRESULT made = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.GetAddressOf())); FAILED(made)) {
      status_ = std::format("Frame generation: no D3D12 device on the game's GPU (0x{:08X})", static_cast<uint32_t>(made));
      return false;
    }
    const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    // Before the fence: a start that fails later leaves no fence without its event (WaitIdle).
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event_ == nullptr) return Fail("Frame generation: no completion event");
    if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue_.GetAddressOf())))
        || FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(own_fence_.GetAddressOf())))) {
      return Fail("Frame generation: no D3D12 queue");
    }
    for (Slot& slot : slots_) {
      if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(slot.allocator.GetAddressOf())))
          || FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                              IID_PPV_ARGS(slot.list.GetAddressOf())))
          || FAILED(slot.list->Close())) {
        return Fail("Frame generation: no D3D12 command lists");
      }
    }
    if (backend == dlaa_bridge::FG_BACKEND_FSR) {
#if defined(RENODX_FSR_FG)
      library_ = LoadLibraryExW((folder + L"\\amd_fidelityfx_loader_dx12.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (library_ == nullptr) return Fail("AMD FSR Frame Generation: amd_fidelityfx_loader_dx12.dll not found next to the helper");
      if (GetFileAttributesW((folder + L"\\amd_fidelityfx_framegeneration_dx12.dll").c_str()) == INVALID_FILE_ATTRIBUTES) {
        return Fail("AMD FSR Frame Generation: amd_fidelityfx_framegeneration_dx12.dll not found next to the helper");
      }
      ffxLoadFunctions(&fsr_, library_);
      if (fsr_.CreateContext == nullptr || fsr_.DestroyContext == nullptr || fsr_.Configure == nullptr || fsr_.Query == nullptr || fsr_.Dispatch == nullptr) {
        return Fail("AMD FSR Frame Generation: amd_fidelityfx_loader_dx12.dll is not the expected one");
      }
#else
      return Fail("AMD FSR Frame Generation is not in this helper's build");
#endif
    } else if (backend == dlaa_bridge::FG_BACKEND_XESS) {
#if defined(RENODX_XESS_FG)
      const std::wstring path = folder + L"\\libxess_fg.dll";
      library_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      xell_library_ = LoadLibraryExW((folder + L"\\libxell.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (library_ == nullptr || xell_library_ == nullptr) {
        // Its files there, what they import not.
        return Fail(GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW((folder + L"\\libxell.dll").c_str()) != INVALID_FILE_ATTRIBUTES
                        ? "XeSS Frame Generation: install the Visual C++ 2015-2022 x64 runtime"
                        : "XeSS Frame Generation: libxess_fg.dll or libxell.dll not found next to the helper");
      }
      const auto load = [](HMODULE module, auto* function, const char* name) {
        *function = reinterpret_cast<std::remove_pointer_t<decltype(function)>>(GetProcAddress(module, name));
        return *function != nullptr;
      };
      if (!load(library_, &xefg_.create, "xefgSwapChainD3D12CreateContext") || !load(library_, &xefg_.init, "xefgSwapChainD3D12InitFromSwapChainDesc")
          || !load(library_, &xefg_.chain, "xefgSwapChainD3D12GetSwapChainPtr") || !load(library_, &xefg_.tag, "xefgSwapChainD3D12TagFrameResource")
          || !load(library_, &xefg_.constants, "xefgSwapChainTagFrameConstants") || !load(library_, &xefg_.enable, "xefgSwapChainSetEnabled")
          || !load(library_, &xefg_.present_id, "xefgSwapChainSetPresentId") || !load(library_, &xefg_.status, "xefgSwapChainGetLastPresentStatus")
          || !load(library_, &xefg_.properties, "xefgSwapChainGetProperties") || !load(library_, &xefg_.frames, "xefgSwapChainSetNumInterpolatedFrames")
          || !load(library_, &xefg_.ui_state, "xefgSwapChainSetUiCompositionState") || !load(library_, &xefg_.latency, "xefgSwapChainSetLatencyReduction")
          || !load(library_, &xefg_.logging, "xefgSwapChainSetLoggingCallback") || !load(library_, &xefg_.version, "xefgSwapChainGetVersion")
          || !load(library_, &xefg_.debug, "xefgSwapChainEnableDebugFeature") || !load(library_, &xefg_.destroy, "xefgSwapChainDestroy")
          || !load(xell_library_, &xell_.create, "xellD3D12CreateContext") || !load(xell_library_, &xell_.mode, "xellSetSleepMode")
          || !load(xell_library_, &xell_.sleep, "xellSleep") || !load(xell_library_, &xell_.marker, "xellAddMarkerData")
          || !load(xell_library_, &xell_.destroy, "xellDestroyContext")) {
        return Fail("XeSS Frame Generation: libxess_fg.dll or libxell.dll is not the expected one");
      }
      if (const xell_result_t made = xell_.create(device.Get(), &xell_context_); made != XELL_RESULT_SUCCESS || xell_context_ == nullptr) {
        return Fail(std::format("XeSS Frame Generation: Xe Low Latency could not start on this GPU or driver (code {})", static_cast<int>(made)));
      }
      const xell_sleep_params_t sleep_mode = {.bLowLatencyMode = 1};
      xell_.mode(xell_context_, &sleep_mode);
#else
      return Fail("XeSS Frame Generation is not in this helper's build");
#endif
    } else {
      return Fail("Frame generation: an unknown backend");
    }
    device_ = std::move(device);
    status_ = "Frame generation ready";
    return true;
  }

  // The addon's frame, HUD-less image, depth and motion (handles still open) and fence; false: not all could be opened.
  bool Open(const dlaa_bridge::Shared& shared) {
    Close();
    if (!device_) return false;
    const auto open = [&](uint64_t handle, REFIID id, void** object) {
      return SUCCEEDED(device_->OpenSharedHandle(reinterpret_cast<HANDLE>(handle), id, object));
    };
    if (!open(shared.fg_frame, IID_PPV_ARGS(frame_.GetAddressOf())) || !open(shared.fg_hudless, IID_PPV_ARGS(hudless_.GetAddressOf()))
        || !open(shared.depth, IID_PPV_ARGS(depth_.GetAddressOf())) || !open(shared.motion, IID_PPV_ARGS(motion_.GetAddressOf()))
        || !open(shared.fence, IID_PPV_ARGS(fence_.GetAddressOf()))) {
      Close();
      // The addon stops asking: the game's own image shows, not the window's last frame.
      window_.Hide();
      return false;
    }
    const D3D12_RESOURCE_DESC frame_desc = frame_->GetDesc();
    width_ = static_cast<UINT>(frame_desc.Width);
    height_ = frame_desc.Height;
    format_ = frame_desc.Format;
    if (backend_ == dlaa_bridge::FG_BACKEND_FSR) {
      const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
      const D3D12_RESOURCE_DESC copy_desc = {
          .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
          .Width = width_,
          .Height = height_,
          .DepthOrArraySize = 1,
          .MipLevels = 1,
          .Format = format_,
          .SampleDesc = {.Count = 1},
      };
      if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &copy_desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                  IID_PPV_ARGS(hudless_copy_.GetAddressOf())))) {
        Close();
        window_.Hide();
        return false;
      }
    }
    return true;
  }

  // The chain and window kept (made again at the next frame of another size or format).
  void Close() {
    WaitIdle();
    frame_.Reset();
    hudless_.Reset();
    hudless_copy_.Reset();
    depth_.Reset();
    motion_.Reset();
    fence_.Reset();
  }

  // One present (the request's value: the frame is written; the next is signalled once copied). False: not presented (the
  // game's own image shows).
  bool Present(dlaa_bridge::Shared* shared) {
    const uint64_t value = shared->value;
    if (!fence_ || !frame_ || failed_ || shared->fg_window == 0) {
      SignalOnly(value);
      return false;
    }
    const auto game_window = reinterpret_cast<HWND>(static_cast<uintptr_t>(shared->fg_window));
    if (shared->fg_multiplier == 0 || !IsWindow(game_window)) {
      // Off: the window hidden (the game's image shows), its chain kept for later; the next frame has no connection to the last.
      after_hidden_ = true;
      window_.Hide();
      SignalOnly(value);
      return false;
    }
    if (maker_.joinable()) {
      if (making_.load()) {
        SignalOnly(value);
        return false;
      }
      maker_.join();
      if (!make_error_.empty()) {
        SignalOnly(value);
        return Fail(make_error_);
      }
      presents_ = 0;
      fsr_present_count_ = 0;
      failure_.clear();
      started_ = std::format(" (started in {:.1f} s)", make_seconds_);
    }
    const bool sdr = shared->fg_sdr != 0;
    if (window_.Stale(game_window) || !chain_ || chain_width_ != width_ || chain_height_ != height_ || chain_format_ != format_ || chain_sdr_ != sdr) {
      window_.Hide();
      DestroyChain();
      if (window_.Stale(game_window)) {
        window_.Close();
        if (!window_.Make(game_window)) {
          SignalOnly(value);
          return Fail("Frame generation: its window over the game's could not be made");
        }
      }
      chain_width_ = width_;
      chain_height_ = height_;
      chain_format_ = format_;
      chain_sdr_ = sdr;
      status_ = "Frame generation starting";
      MakeChain();
      SignalOnly(value);
      return false;
    }
    window_.Fit();
    const uint32_t frame = shared->fg_frame_index;
    // Its own GPU work not proven done on a live device: nothing is executed for this present, the game's own image shows
    // and the next present asks again (the game's GPU can be seconds behind; giving up here left frame generation off
    // until the game restarted).
    const auto unfinished = [&] {
      status_ = "Frame generation: its GPU work did not finish";
      after_hidden_ = true;
      window_.Hide();
      SignalOnly(value);
      return false;
    };

#if defined(RENODX_XESS_FG)
    if (backend_ == dlaa_bridge::FG_BACKEND_XESS && shared->fg_frame_limit != frame_limit_) {
      // Its guide: the GPU idle first. Nothing is freed here: not WaitIdle's wait before a release, which ends the helper.
      queue_->Signal(own_fence_.Get(), ++own_value_);
      if (!WaitForGpu(own_fence_.Get(), event_, own_value_, FENCE_TIMEOUT)) return unfinished();
      frame_limit_ = shared->fg_frame_limit;
      const xell_sleep_params_t sleep_mode = {.minimumIntervalUs = (frame_limit_ != 0 ? 1000000u / std::min(frame_limit_, 1000u) : 0u), .bLowLatencyMode = 1};
      xell_.mode(xell_context_, &sleep_mode);
    }
#endif

    Slot& slot = slots_[next_slot_];
    next_slot_ = (next_slot_ + 1) % slots_.size();
    // The slot's last use: its allocator still feeds the GPU. No recording: the backend gives up (main.cpp: Auto's next;
    // after a GPU reset a new device).
    if (!WaitForGpu(own_fence_.Get(), event_, slot.done, FENCE_TIMEOUT)) return unfinished();
    if (FAILED(slot.allocator->Reset()) || FAILED(slot.list->Reset(slot.allocator.Get(), nullptr))) {
      SignalOnly(value);
      return Fail("Frame generation: D3D12 stopped");
    }
    ComPtr<ID3D12Resource> target;
    if (FAILED(chain_->GetBuffer(chain_->GetCurrentBackBufferIndex(), IID_PPV_ARGS(target.GetAddressOf())))) {
      slot.list->Close();
      SignalOnly(value);
      return Fail("Frame generation: the swap chain has no back buffer");
    }
    const bool inputs = shared->fg_inputs != 0 && shared->width != 0 && shared->height != 0;
    const bool hudless_valid = inputs && shared->fg_hudless_valid != 0;
    const bool reset = shared->reset != 0 || std::exchange(after_hidden_, false);
    const auto now = std::chrono::steady_clock::now();
    const float frame_ms = (last_time_ == std::chrono::steady_clock::time_point{} ? 16.699656f : std::chrono::duration<float, std::milli>(now - last_time_).count());
    last_time_ = now;

    // The shared textures decay to COMMON. FSR's prepare pass reads depth and motion in its list, XeSS copies them there.
    const D3D12_RESOURCE_STATES input_state = (backend_ == dlaa_bridge::FG_BACKEND_FSR ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::array<D3D12_RESOURCE_BARRIER, 6> barriers;
    UINT barrier_count = 0;
    const auto transition = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
      barriers[barrier_count++] = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                                   .Transition = {.pResource = resource, .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                  .StateBefore = before, .StateAfter = after}};
    };
    transition(frame_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(hudless_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(target.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    if (inputs) {
      transition(depth_.Get(), D3D12_RESOURCE_STATE_COMMON, input_state);
      transition(motion_.Get(), D3D12_RESOURCE_STATE_COMMON, input_state);
    }
    if (hudless_copy_) transition(hudless_copy_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    slot.list->ResourceBarrier(barrier_count, barriers.data());
    slot.list->CopyResource(target.Get(), frame_.Get());

#if defined(RENODX_FSR_FG)
    if (backend_ == dlaa_bridge::FG_BACKEND_FSR) {
      if (hudless_valid) slot.list->CopyResource(hudless_copy_.Get(), hudless_.Get());
      // Its order: configured, prepared, then the present dispatches. The ID goes up by one each present, else it resets.
      const ffxConfigureDescFrameGeneration configure = {
          .header = {.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION},
          .swapChain = chain_.Get(),
          .frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* self) -> ffxReturnCode_t {
            auto* generation = static_cast<ProxyFrameGeneration12*>(self);
            return generation->fsr_.Dispatch(&generation->fsr_generation_, &params->header);
          },
          .frameGenerationCallbackUserContext = this,
          .frameGenerationEnabled = inputs,
          .HUDLessColor = (hudless_valid ? ffxApiGetResourceDX12(hudless_copy_.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ) : FfxApiResource{}),
          .onlyPresentGenerated = ONLY_GENERATED,
          .generationRect = {.left = 0, .top = 0, .width = static_cast<int32_t>(width_), .height = static_cast<int32_t>(height_)},
          .frameID = frame,
      };
      if (const ffxReturnCode_t configured = fsr_.Configure(&fsr_generation_, &configure.header); configured != FFX_API_RETURN_OK) {
        failure_ = std::format("its settings failed (code {})", configured);
      }
      if (inputs) {
        const float* projection = shared->fg_view_to_clip;
        const float* camera = shared->fg_camera;
        ffxDispatchDescFrameGenerationPrepareV2 prepare = {
            .header = {.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2},
            .frameID = frame,
            .commandList = slot.list.Get(),
            .renderSize = {.width = shared->width, .height = shared->height},
            .jitterOffset = {.x = shared->jitter_x, .y = shared->jitter_y},
            .motionVectorScale = {.x = 1.f, .y = 1.f},
            .frameTimeDelta = frame_ms,
            .reset = reset,
            .cameraNear = shared->fg_near,
            .cameraFar = shared->fg_far,
            .cameraFovAngleVertical = 2.f * std::atan(1.f / projection[5]),
            .viewSpaceToMetersFactor = 1.f,
            .depth = ffxApiGetResourceDX12(depth_.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ),
            .motionVectors = ffxApiGetResourceDX12(motion_.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ),
        };
        std::copy_n(camera, 3, prepare.cameraPosition);
        std::copy_n(camera + 3, 3, prepare.cameraRight);
        std::copy_n(camera + 6, 3, prepare.cameraUp);
        std::copy_n(camera + 9, 3, prepare.cameraForward);
        if (const ffxReturnCode_t prepared = fsr_.Dispatch(&fsr_generation_, &prepare.header); prepared != FFX_API_RETURN_OK) {
          failure_ = std::format("its prepare pass failed (code {})", prepared);
        }
      }
    }
#endif
#if defined(RENODX_XESS_FG)
    if (backend_ == dlaa_bridge::FG_BACKEND_XESS) {
      if (inputs) {
        const auto tag = [&](xefg_swapchain_resource_type_t type, ID3D12Resource* resource, uint32_t tag_width, uint32_t tag_height) {
          const xefg_swapchain_d3d12_resource_data_t data = {
              .type = type,
              .validity = XEFG_SWAPCHAIN_RV_ONLY_NOW,
              .resourceSize = {.x = tag_width, .y = tag_height},
              .pResource = resource,
              .incomingState = D3D12_RESOURCE_STATE_COPY_SOURCE,
          };
          if (const xefg_swapchain_result_t tagged = xefg_.tag(xefg_context_, slot.list.Get(), frame, &data); tagged < XEFG_SWAPCHAIN_RESULT_SUCCESS) {
            failure_ = std::format("its inputs failed (code {})", static_cast<int>(tagged));
          }
        };
        tag(XEFG_SWAPCHAIN_RES_DEPTH, depth_.Get(), shared->width, shared->height);
        tag(XEFG_SWAPCHAIN_RES_MOTION_VECTOR, motion_.Get(), shared->width, shared->height);
        if (hudless_valid) tag(XEFG_SWAPCHAIN_RES_HUDLESS_COLOR, hudless_.Get(), width_, height_);
        // The view from the camera's matrices (row vectors): view x projection x inverse projection.
        const auto inverse_projection = InverseMatrix(shared->fg_view_to_clip);
        xefg_swapchain_frame_constant_data_t constants = {
            .jitterOffsetX = shared->jitter_x,
            .jitterOffsetY = shared->jitter_y,
            .motionVectorScaleX = 1.f,
            .motionVectorScaleY = 1.f,
            .resetHistory = (reset ? 1u : 0u),
            .frameRenderTime = frame_ms,
        };
        for (int row = 0; row < 4; ++row) {
          for (int column = 0; column < 4; ++column) {
            for (int k = 0; k < 4; ++k) {
              constants.viewMatrix[(row * 4) + column] += shared->fg_view_projection[(row * 4) + k] * inverse_projection[(k * 4) + column];
            }
          }
        }
        std::copy_n(shared->fg_view_to_clip, 16, constants.projectionMatrix);
        if (const xefg_swapchain_result_t tagged = xefg_.constants(xefg_context_, frame, &constants); tagged < XEFG_SWAPCHAIN_RESULT_SUCCESS) {
          failure_ = std::format("its camera failed (code {})", static_cast<int>(tagged));
        }
      }
      // Off on frames without the inputs (menus, loading). The HUD laid over the generated frames as DLSS's recomposition does.
      if (inputs != xefg_enabled_) {
        xefg_enabled_ = inputs;
        xefg_.enable(xefg_context_, inputs ? 1u : 0u);
      }
      if (const bool composed = hudless_valid && shared->fg_ui != 0; composed != xefg_composed_ && inputs) {
        xefg_composed_ = composed;
        xefg_.ui_state(xefg_context_, composed ? XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_ENABLED : XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_DISABLED);
      }
      // Intel's GPUs: 3x and 4x too, Dynamic the most (this call can compile shaders: only on a change). Others: 2x.
      const uint32_t wanted = (shared->fg_multiplier == 1 ? xefg_most_ : std::clamp(shared->fg_multiplier - 1, 1u, xefg_most_));
      if (wanted != xefg_frames_) {
        xefg_frames_ = wanted;
        xefg_.frames(xefg_context_, wanted);
      }
    }
#endif
    for (UINT index = 0; index < barrier_count; ++index) std::swap(barriers[index].Transition.StateBefore, barriers[index].Transition.StateAfter);
    slot.list->ResourceBarrier(barrier_count, barriers.data());
    if (FAILED(slot.list->Close())) {
      SignalOnly(value);
      return Fail("Frame generation: D3D12 stopped");
    }
    queue_->Wait(fence_.Get(), value);
    ID3D12CommandList* lists[] = {slot.list.Get()};
    queue_->ExecuteCommandLists(1, lists);
    queue_->Signal(fence_.Get(), value + 1);
    slot.done = ++own_value_;
    queue_->Signal(own_fence_.Get(), slot.done);

    // The status line every 30 presents.
    if (presents_++ % 30 == 0) {
      const char* no_hudless = (hudless_valid ? "" : " (no HUD-less image: the UI can warp)");
#if defined(RENODX_FSR_FG)
      // The lines between don't read its count: the next one starts over.
      if (!failure_.empty() || !inputs) fsr_present_count_ = 0;
#endif
      if (!failure_.empty()) {
        status_ = "Frame generation: " + std::exchange(failure_, {});
      } else if (!inputs) {
        status_ = "Frame generation: paused (no upscaler on this frame)";
      } else {
#if defined(RENODX_FSR_FG)
        if (backend_ == dlaa_bridge::FG_BACKEND_FSR) {
          ffxQueryGetProviderVersion provider = {.header = {.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION}};
          fsr_.Query(&fsr_generation_, &provider.header);
          // Its Present counts the frames it shows; nothing to read on the first line, or if it doesn't count.
          UINT present_count = 0;
          const bool counted = SUCCEEDED(chain_->GetLastPresentCount(&present_count)) && present_count != 0 && fsr_present_count_ != 0;
          const std::string frames = (counted ? std::format("{:.1f} frames presented per rendered frame", static_cast<float>(present_count - fsr_present_count_) / 30.f) : std::string("started"));
          fsr_present_count_ = present_count;
          status_ = std::format("AMD FSR Frame Generation {}: {}{}{}", (provider.versionName != nullptr ? provider.versionName : "?"), frames, no_hudless, started_);
        }
#endif
#if defined(RENODX_XESS_FG)
        if (backend_ == dlaa_bridge::FG_BACKEND_XESS) {
          xefg_swapchain_present_status_t last = {};
          xefg_swapchain_version_t version = {};
          xefg_.version(&version);
          xefg_.status(xefg_context_, &last);
          if (last.frameGenResult < XEFG_SWAPCHAIN_RESULT_SUCCESS) {
            status_ = std::format("XeSS Frame Generation: not generating (code {})", static_cast<int>(last.frameGenResult));
          } else {
            status_ = std::format("XeSS Frame Generation {}.{}.{}: {} frames presented per rendered frame{}{}{}", version.major, version.minor, version.patch,
                                  last.framesPresented, no_hudless, (xefg_composed_ ? ", HUD recomposed" : ""), started_);
          }
        }
#endif
      }
    }

#if defined(RENODX_XESS_FG)
    const bool marked = backend_ == dlaa_bridge::FG_BACKEND_XESS && marked_frame_ == frame;
    if (marked) {
      xell_.marker(xell_context_, frame, XELL_RENDERSUBMIT_END);
      xell_.marker(xell_context_, frame, XELL_PRESENT_START);
    }
    if (backend_ == dlaa_bridge::FG_BACKEND_XESS) xefg_.present_id(xefg_context_, frame);
#endif
    const HRESULT presented = TEST_PRESENT_FAILS ? E_FAIL : chain_->Present(0, tearing_ != 0 ? DXGI_PRESENT_ALLOW_TEARING : 0);
    if (SUCCEEDED(presented)) {
      failed_presents_ = 0;
      window_.Show();
    } else if (++failed_presents_ >= FAILED_PRESENTS_LIMIT && !Lost()) {
      // Not a GPU reset (main.cpp replaces the device): the game's own image, not a frozen window over it.
      Fail(std::format("Frame generation: Present keeps failing (0x{:08X})", static_cast<uint32_t>(presented)));
    }
#if defined(RENODX_XESS_FG)
    if (marked) xell_.marker(xell_context_, frame, XELL_PRESENT_END);
    // The next frame's sleep, simulation and submission start (the game waits for this answer).
    if (backend_ == dlaa_bridge::FG_BACKEND_XESS) {
      marked_frame_ = frame + 1;
      xell_.sleep(xell_context_, marked_frame_);
      xell_.marker(xell_context_, marked_frame_, XELL_SIMULATION_START);
      xell_.marker(xell_context_, marked_frame_, XELL_SIMULATION_END);
      xell_.marker(xell_context_, marked_frame_, XELL_RENDERSUBMIT_START);
    }
#endif
    return SUCCEEDED(presented);
  }

  // Off for long, or another backend takes over: the chain, its contexts and the window gone. The next present with it on
  // makes them again.
  void FreeResources() {
    if (maker_.joinable()) maker_.join();
    window_.Hide();
    DestroyChain();
    window_.Close();
  }

  ~ProxyFrameGeneration12() {
    FreeResources();
    frame_.Reset();
#if defined(RENODX_XESS_FG)
    // After XeSS's context, by Intel's guide.
    if (xell_context_ != nullptr) xell_.destroy(xell_context_);
    if (xell_library_ != nullptr) FreeLibrary(xell_library_);
#endif
    if (event_ != nullptr) CloseHandle(event_);
    if (library_ != nullptr) FreeLibrary(library_);
  }

  [[nodiscard]] uint32_t Backend() const { return backend_; }
  [[nodiscard]] const std::string& Status() const { return status_; }
  // It gave up on this device or driver (its chain could not be made, ...).
  [[nodiscard]] bool Failed() const { return failed_; }
  // A GPU reset removed its device (the helper makes a new one).
  [[nodiscard]] HRESULT RemovedReason() const { return device_ ? device_->GetDeviceRemovedReason() : S_OK; }
  [[nodiscard]] bool Lost() const { return FAILED(RemovedReason()); }

 private:
  struct Slot {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    uint64_t done = 0;
  };

  static constexpr DWORD FENCE_TIMEOUT = 10'000;
  static constexpr UINT BUFFER_COUNT = 3;
  static constexpr uint32_t FAILED_PRESENTS_LIMIT = 60;
  // Test switch: every Present fails (fg_bridge_test: Auto moves on).
  inline static const bool TEST_PRESENT_FAILS = GetEnvironmentVariableW(L"RENODX_TW_TEST_FG_PROXY_PRESENT_FAILS", nullptr, 0) != 0;
  // Test switch: only generated frames shown, for looking at their colours.
  inline static const bool ONLY_GENERATED = GetEnvironmentVariableW(L"RENODX_TW_TEST_FG_ONLY_GENERATED", nullptr, 0) != 0;

  bool Fail(const std::string& status) {
    failed_ = true;
    status_ = status;
    // The game's own image shows, not the window's last frame.
    window_.Hide();
    return false;
  }

  // The copy queue's fence `value` signalled (the game never waits for nothing).
  void SignalOnly(uint64_t value) {
    if (!fence_ || !queue_) return;
    queue_->Wait(fence_.Get(), value);
    queue_->Signal(fence_.Get(), value + 1);
  }

  void WaitIdle() {
    if (!queue_ || !own_fence_) return;
    queue_->Signal(own_fence_.Get(), ++own_value_);
    WaitForGpuBeforeRelease(own_fence_.Get(), event_, own_value_, FENCE_TIMEOUT, "frame generation (FSR / XeSS)");
  }

  // The chain at chain_width_ x chain_height_ in chain_format_ on the window, beside the requests.
  void MakeChain() {
    make_error_.clear();
    making_.store(true);
    maker_ = std::thread([this, window = window_.window] {
      const auto started = std::chrono::steady_clock::now();
      ComPtr<IDXGIFactory5> factory;
      if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf())))) {
        make_error_ = "Frame generation: no DXGI factory";
      } else {
        BOOL tearing = FALSE;
        tearing_ = (SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        DXGI_SWAP_CHAIN_DESC1 desc = {
            .Width = chain_width_,
            .Height = chain_height_,
            .Format = chain_format_,
            .SampleDesc = {.Count = 1},
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = BUFFER_COUNT,
            .Scaling = DXGI_SCALING_STRETCH,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
            .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
            .Flags = tearing_,
        };
#if defined(RENODX_FSR_FG)
        if (backend_ == dlaa_bridge::FG_BACKEND_FSR) {
          IDXGISwapChain4* proxy = nullptr;
          ffxCreateContextDescFrameGenerationSwapChainVersionDX12 chain_version = {
              .header = {.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12},
              .version = FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION,
          };
          ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 create_chain = {
              .header = {.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12, .pNext = &chain_version.header},
              .swapchain = &proxy,
              .hwnd = window,
              .desc = &desc,
              .dxgiFactory = factory.Get(),
              .gameQueue = queue_.Get(),
          };
          if (const ffxReturnCode_t made = fsr_.CreateContext(&fsr_chain_, &create_chain.header, nullptr); made != FFX_API_RETURN_OK || proxy == nullptr) {
            make_error_ = std::format("AMD FSR Frame Generation: its swap chain could not be made (code {})", made);
          } else {
            chain_.Attach(proxy);
            // HDR10 as the game's chain shows it; SDR keeps the chain's default (fg_d3d12.hpp). Its luminance range: the display's.
            if (chain_format_ == DXGI_FORMAT_R10G10B10A2_UNORM && !chain_sdr_) chain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
            ffxCreateBackendDX12Desc create_backend = {.header = {.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12}, .device = device_.Get()};
            ffxCreateContextDescFrameGenerationVersion version = {
                .header = {.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION, .pNext = &create_backend.header},
                .version = FFX_FRAMEGENERATION_VERSION,
            };
            ffxCreateContextDescFrameGeneration create = {
                .header = {.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION, .pNext = &version.header},
                // No depth flags: 0 near, a far plane.
                .flags = (chain_sdr_ ? 0u : static_cast<uint32_t>(FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE)),
                .displaySize = {.width = chain_width_, .height = chain_height_},
                .maxRenderSize = {.width = chain_width_, .height = chain_height_},
                .backBufferFormat = ffxApiGetSurfaceFormatDX12(chain_format_),
            };
            if (const ffxReturnCode_t started_context = fsr_.CreateContext(&fsr_generation_, &create.header, nullptr); started_context != FFX_API_RETURN_OK) {
              make_error_ = std::format("AMD FSR Frame Generation could not start (code {})", started_context);
            }
          }
        }
#endif
#if defined(RENODX_XESS_FG)
        if (backend_ == dlaa_bridge::FG_BACKEND_XESS) {
          const xefg_swapchain_d3d12_init_params_t init = {.maxInterpolatedFrames = XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES, .uiMode = XEFG_SWAPCHAIN_UI_MODE_AUTO};
          xefg_swapchain_properties_t properties = {};
          if (const xefg_swapchain_result_t made = xefg_.create(device_.Get(), &xefg_context_); made < XEFG_SWAPCHAIN_RESULT_SUCCESS) {
            make_error_ = std::format("XeSS Frame Generation could not start on this GPU or driver (code {})", static_cast<int>(made));
          } else if (xefg_.logging(
                         xefg_context_, XEFG_SWAPCHAIN_LOGGING_LEVEL_ERROR,
                         [](const char* message, xefg_swapchain_logging_level_t, void*) { reshade::log::message(reshade::log::level::warning, message); }, nullptr);
                     xefg_.latency(xefg_context_, xell_context_) < XEFG_SWAPCHAIN_RESULT_SUCCESS) {
            make_error_ = "XeSS Frame Generation: Xe Low Latency was not taken";
          } else if (const xefg_swapchain_result_t inited = xefg_.init(xefg_context_, window, &desc, nullptr, queue_.Get(), factory.Get(), &init); inited < XEFG_SWAPCHAIN_RESULT_SUCCESS) {
            make_error_ = std::format("XeSS Frame Generation: its swap chain could not be made (code {})", static_cast<int>(inited));
          } else if (FAILED(xefg_.chain(xefg_context_, IID_PPV_ARGS(chain_.GetAddressOf())))) {
            make_error_ = "XeSS Frame Generation: no swap chain";
          } else {
            if (chain_format_ == DXGI_FORMAT_R10G10B10A2_UNORM && !chain_sdr_) chain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
            xefg_.properties(xefg_context_, &properties);
            xefg_most_ = std::max(properties.maxSupportedInterpolations, 1u);
            xefg_frames_ = xefg_most_;
            if (ONLY_GENERATED) xefg_.debug(xefg_context_, XEFG_SWAPCHAIN_DEBUG_FEATURE_SHOW_ONLY_INTERPOLATION, 1, nullptr);
          }
        }
#endif
      }
      make_seconds_ = std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count();
      making_.store(false);
    });
  }

  // Its orders: FSR's generation off (which waits for its presents), its contexts, the last reference; XeSS's GPU work done,
  // the reference, its context.
  void DestroyChain() {
    WaitIdle();
#if defined(RENODX_FSR_FG)
    if (fsr_generation_ != nullptr) {
      const ffxConfigureDescFrameGeneration configure = {.header = {.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION}, .swapChain = chain_.Get()};
      fsr_.Configure(&fsr_generation_, &configure.header);
      fsr_.DestroyContext(&fsr_generation_, nullptr);
      fsr_generation_ = nullptr;
    }
    if (fsr_chain_ != nullptr) {
      fsr_.DestroyContext(&fsr_chain_, nullptr);
      fsr_chain_ = nullptr;
    }
#endif
    chain_.Reset();
#if defined(RENODX_XESS_FG)
    if (xefg_context_ != nullptr) {
      xefg_.destroy(xefg_context_);
      xefg_context_ = nullptr;
      xefg_enabled_ = false;
      xefg_composed_ = false;
    }
#endif
  }

  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  ComPtr<ID3D12Fence> own_fence_;
  uint64_t own_value_ = 0;
  std::array<Slot, 3> slots_;
  size_t next_slot_ = 0;
  HANDLE event_ = nullptr;
  uint32_t backend_ = dlaa_bridge::FG_BACKEND_FSR;
  HMODULE library_ = nullptr;
  bool failed_ = false;
  uint32_t failed_presents_ = 0;  // in a row
  std::string status_ = "Frame generation not started";
  std::string failure_;
  std::string started_;  // how long its start took, for the status line
  uint64_t presents_ = 0;
  UINT fsr_present_count_ = 0;  // at the last status line

  // The addon's objects, FSR's copy of the HUD-less image.
  ComPtr<ID3D12Resource> frame_;
  ComPtr<ID3D12Resource> hudless_;
  ComPtr<ID3D12Resource> hudless_copy_;
  ComPtr<ID3D12Resource> depth_;
  ComPtr<ID3D12Resource> motion_;
  ComPtr<ID3D12Fence> fence_;
  UINT width_ = 0;
  UINT height_ = 0;
  DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;

  // The window, the proxy swap chain and what it was made for. Its maker writes the chain, the contexts, make_error_ and
  // make_seconds_ (read after the join) and reads the chain_ settings, which wait for the join.
  FgWindow window_;
  ComPtr<IDXGISwapChain4> chain_;
  UINT chain_width_ = 0;
  UINT chain_height_ = 0;
  DXGI_FORMAT chain_format_ = DXGI_FORMAT_UNKNOWN;
  bool chain_sdr_ = false;
  UINT tearing_ = 0;
  bool after_hidden_ = false;  // the next frame has no connection to the last one presented
  std::chrono::steady_clock::time_point last_time_;
  std::thread maker_;
  std::atomic<bool> making_ = false;
  std::string make_error_;
  float make_seconds_ = 0.f;

#if defined(RENODX_FSR_FG)
  ffxFunctions fsr_ = {};
  ffxContext fsr_chain_ = nullptr;
  ffxContext fsr_generation_ = nullptr;
#endif
#if defined(RENODX_XESS_FG)
  struct {
    decltype(&xefgSwapChainD3D12CreateContext) create = nullptr;
    decltype(&xefgSwapChainD3D12InitFromSwapChainDesc) init = nullptr;
    decltype(&xefgSwapChainD3D12GetSwapChainPtr) chain = nullptr;
    decltype(&xefgSwapChainD3D12TagFrameResource) tag = nullptr;
    decltype(&xefgSwapChainTagFrameConstants) constants = nullptr;
    decltype(&xefgSwapChainSetEnabled) enable = nullptr;
    decltype(&xefgSwapChainSetPresentId) present_id = nullptr;
    decltype(&xefgSwapChainGetLastPresentStatus) status = nullptr;
    decltype(&xefgSwapChainGetProperties) properties = nullptr;
    decltype(&xefgSwapChainSetNumInterpolatedFrames) frames = nullptr;
    decltype(&xefgSwapChainSetUiCompositionState) ui_state = nullptr;
    decltype(&xefgSwapChainSetLatencyReduction) latency = nullptr;
    decltype(&xefgSwapChainSetLoggingCallback) logging = nullptr;
    decltype(&xefgSwapChainGetVersion) version = nullptr;
    decltype(&xefgSwapChainEnableDebugFeature) debug = nullptr;
    decltype(&xefgSwapChainDestroy) destroy = nullptr;
  } xefg_;
  struct {
    decltype(&xellD3D12CreateContext) create = nullptr;
    decltype(&xellSetSleepMode) mode = nullptr;
    decltype(&xellSleep) sleep = nullptr;
    decltype(&xellAddMarkerData) marker = nullptr;
    decltype(&xellDestroyContext) destroy = nullptr;
  } xell_;
  HMODULE xell_library_ = nullptr;
  xefg_swapchain_handle_t xefg_context_ = nullptr;
  xell_context_handle_t xell_context_ = nullptr;
  bool xefg_enabled_ = false;
  bool xefg_composed_ = false;
  uint32_t xefg_most_ = 1;    // generated frames per rendered frame: the GPU's most (others than Intel's: 1)
  uint32_t xefg_frames_ = 1;  // as asked
  uint32_t frame_limit_ = 0;  // Xe Low Latency's, rendered frames per second (0: none)
  uint32_t marked_frame_ = UINT32_MAX;  // the frame whose sleep and first markers were given
#endif
};

}  // namespace twattila::dlaa_helper
