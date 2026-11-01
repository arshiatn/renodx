/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_security.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// NVIDIA's NVAPI (MIT; build.ps1 adds it when build\downloads\nvapi holds it): the frame generation preset's driver profile.
#if __has_include(<nvapi.h>)
#include <nvapi.h>
#endif

#include "../../talesofarise_dlss/ngx_backend.hpp"
#include "../dlaa_bridge.h"
#include "fg_ui.h"  // dxc -Fh (build.ps1)

// The DLAA helper's frame generation (bridge v5): NVIDIA's DLSS Frame Generation through Streamline (sl.interposer.dll,
// sl.common.dll, sl.dlss_g.dll, sl.reflex.dll, sl.pcl.dll and nvngx_dlssg.dll next to the helper) on a D3D12 device of
// its own. Attila is 32-bit and Streamline is 64-bit only, so the helper presents: into a child window it puts over the
// game's window (disabled: the game's window keeps the mouse and keyboard; Streamline counts a window of its parent
// process's foreground window as focused, tools\tw\fg_child), stretched to its client area, with the game's own swap
// chain still presenting underneath. Each request (a game present): the copy queue waits for the addon's fence value,
// copies the frame into the swap chain's back buffer and lets Streamline copy the DLAA depth and motion and the HUD-less
// image with the camera, then signals the next value; the present queue waits for that and presents (frame generation
// adds its frames there), then Reflex paces the next frame. As in talesofarise_dlss/frame_generation.hpp (Phantom FX's
// route by Bustanity, MIT). Multiplier 0 hides the window (the game's own image shows) and turns generation off.
// Bridge v12: the HUD's recomposition (a UI layer from the frame and its HUD-less image, fg_ui.cs_6_0.hlsl, which DLSS-G
// lays over the frames it makes) and NVIDIA's preset, written into this exe's driver profile before DLSS-G starts.
// Bridge v13: the debug view (the HUD-less image, or the frame with its UI layer tinted, presented instead).
namespace twattila::dlaa_helper {

using Microsoft::WRL::ComPtr;

class FrameGeneration12 {
 public:
  // Streamline from `folder` and a D3D12 device on the game's GPU; false: unavailable (Status() says why).
  bool Initialize(IDXGIAdapter1* adapter, const std::wstring& folder) {
    if (failed_) return false;
    if (device_) return true;
    // Streamline once; a device that could not be made is tried again at the next set (Windows refuses a process new
    // devices for a while after GPU resets).
    if (!initialized_) {
      const std::wstring interposer = folder + L"\\sl.interposer.dll";
      if (GetFileAttributesW(interposer.c_str()) == INVALID_FILE_ATTRIBUTES) return Fail("sl.interposer.dll is not next to the helper");
      if (!sl::security::verifyEmbeddedSignature(interposer.c_str())) return Fail("sl.interposer.dll is not signed by NVIDIA");
      module_ = LoadLibraryExW(interposer.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (module_ == nullptr) return Fail("sl.interposer.dll did not load");
      const bool resolved = Resolve(&init_, "slInit") && Resolve(&shutdown_, "slShutdown") && Resolve(&is_supported_, "slIsFeatureSupported")
                            && Resolve(&set_device_, "slSetD3DDevice") && Resolve(&upgrade_, "slUpgradeInterface")
                            && Resolve(&native_, "slGetNativeInterface") && Resolve(&feature_function_, "slGetFeatureFunction")
                            && Resolve(&frame_token_, "slGetNewFrameToken") && Resolve(&set_tags_, "slSetTagForFrame")
                            && Resolve(&set_constants_, "slSetConstants") && Resolve(&free_resources_, "slFreeResources");
      if (!resolved) return Fail("sl.interposer.dll lacks Streamline's functions");

      std::array<const wchar_t*, 1> paths = {folder.c_str()};
      const std::array<sl::Feature, 3> features = {sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
      sl::Preferences preferences = {};
      preferences.pathsToPlugins = paths.data();
      preferences.numPathsToPlugins = static_cast<uint32_t>(paths.size());
      preferences.pathToLogsAndData = paths[0];
      preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseManualHooking
                          | sl::PreferenceFlags::eUseDXGIFactoryProxy | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
      preferences.featuresToLoad = features.data();
      preferences.numFeaturesToLoad = static_cast<uint32_t>(features.size());
      preferences.engine = sl::EngineType::eCustom;
      preferences.engineVersion = "1.0";
      preferences.projectId = talesofarise::dlss::PROJECT_ID;
      if (const sl::Result result = init_(preferences, sl::kSDKVersion); result != sl::Result::eOk) {
        return Fail(std::format("Streamline did not start ({})", sl::getResultAsStr(result)));
      }
      initialized_ = true;
      DXGI_ADAPTER_DESC1 adapter_desc = {};
      adapter->GetDesc1(&adapter_desc);
      sl::AdapterInfo info = {};
      info.deviceLUID = reinterpret_cast<uint8_t*>(&adapter_desc.AdapterLuid);
      info.deviceLUIDSizeInBytes = sizeof(LUID);
      if (const sl::Result supported = is_supported_(sl::kFeatureDLSS_G, info); supported != sl::Result::eOk) {
        return Fail(std::format("DLSS Frame Generation is not supported here ({}): an RTX 40 or newer GPU, a recent driver and "
                                "hardware-accelerated GPU scheduling",
                                sl::getResultAsStr(supported)));
      }
    }
    const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    if (const HRESULT made = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device_.GetAddressOf())); FAILED(made)) {
      status_ = std::format("Frame generation: no D3D12 device on the game's GPU (0x{:08X})", static_cast<uint32_t>(made));
      return false;
    }
    if (set_device_(device_.Get()) != sl::Result::eOk) return Fail("Streamline refused the D3D12 device");
    if (!Resolve(sl::kFeatureDLSS_G, &set_options_, "slDLSSGSetOptions") || !Resolve(sl::kFeatureDLSS_G, &get_state_, "slDLSSGGetState")
        || !Resolve(sl::kFeatureReflex, &set_reflex_, "slReflexSetOptions") || !Resolve(sl::kFeatureReflex, &sleep_, "slReflexSleep")
        || !Resolve(sl::kFeaturePCL, &set_marker_, "slPCLSetMarker")) {
      return Fail("Streamline lacks frame generation, Reflex or PC Latency");
    }
    sl::ReflexOptions reflex = {};
    reflex.mode = sl::ReflexMode::eLowLatency;
    set_reflex_(reflex);
    // Streamline's proxy of the device and its queue make the swap chain; the copies run on a native queue of their own.
    void* upgraded = device_.Get();
    if (upgrade_(&upgraded) != sl::Result::eOk) return Fail("Streamline did not take the D3D12 device");
    if (upgraded == device_.Get()) device_->AddRef();
    streamline_device_.Attach(static_cast<ID3D12Device*>(upgraded));
    void* native_queue = nullptr;
    if (FAILED(streamline_device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(streamline_queue_.GetAddressOf())))
        || native_(streamline_queue_.Get(), &native_queue) != sl::Result::eOk || native_queue == nullptr) {
      return Fail("no D3D12 present queue");
    }
    present_queue_.Attach(static_cast<ID3D12CommandQueue*>(native_queue));
    if (FAILED(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(copy_queue_.GetAddressOf())))
        || FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(own_fence_.GetAddressOf())))) {
      return Fail("no D3D12 copy queue");
    }
    for (Slot& slot : slots_) {
      if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(slot.allocator.GetAddressOf())))
          || FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                               IID_PPV_ARGS(slot.list.GetAddressOf())))
          || FAILED(slot.list->Close())) {
        return Fail("no D3D12 command lists");
      }
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    // The UI layer's pass (fg_ui.cs_6_0.hlsl): the frame and the HUD-less image in, the layer (and the debug view) out;
    // its size, threshold and whether the debug view is written. Without it the HUD is not recomposed.
    const std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges = {{
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = 2, .BaseShaderRegister = 0, .OffsetInDescriptorsFromTableStart = 0},
        {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = 2, .BaseShaderRegister = 0, .OffsetInDescriptorsFromTableStart = 2},
    }};
    const std::array<D3D12_ROOT_PARAMETER, 2> parameters = {{
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
         .DescriptorTable = {.NumDescriptorRanges = static_cast<UINT>(ranges.size()), .pDescriptorRanges = ranges.data()}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, .Constants = {.ShaderRegister = 0, .Num32BitValues = 4}},
    }};
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {.NumParameters = static_cast<UINT>(parameters.size()), .pParameters = parameters.data()};
    const D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .NumDescriptors = 4,
                                                  .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    descriptor_step_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ComPtr<ID3DBlob> serialized;
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), nullptr))
        || FAILED(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                               IID_PPV_ARGS(ui_signature_.GetAddressOf())))
        || FAILED(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(ui_heap_.GetAddressOf())))) {
      ui_signature_.Reset();
    } else {
      const D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {.pRootSignature = ui_signature_.Get(),
                                                               .CS = {.pShaderBytecode = FG_UI, .BytecodeLength = sizeof(FG_UI)}};
      device_->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(ui_pipeline_.GetAddressOf()));
    }
    status_ = "Frame generation ready";
    return true;
  }

  // The addon's frame, HUD-less image, depth and motion (bridge v5; handles still open) and fence, its bridge version;
  // false: not all could be opened.
  bool Open(const dlaa_bridge::Shared& shared, uint32_t version) {
    Close();
    if (!device_) return false;
    version_ = version;
    const auto open = [&](uint64_t handle, REFIID id, void** object) {
      return SUCCEEDED(device_->OpenSharedHandle(reinterpret_cast<HANDLE>(handle), id, object));
    };
    if (!open(shared.fg_frame, IID_PPV_ARGS(frame_.GetAddressOf())) || !open(shared.fg_hudless, IID_PPV_ARGS(hudless_.GetAddressOf()))
        || !open(shared.depth, IID_PPV_ARGS(depth_.GetAddressOf())) || !open(shared.motion, IID_PPV_ARGS(motion_.GetAddressOf()))
        || !open(shared.fence, IID_PPV_ARGS(fence_.GetAddressOf()))) {
      Close();
      return false;
    }
    const D3D12_RESOURCE_DESC frame_desc = frame_->GetDesc();
    width_ = static_cast<UINT>(frame_desc.Width);
    height_ = frame_desc.Height;
    format_ = frame_desc.Format;
    // The UI layer (DLSS-G's UI alpha: one byte a pixel, NVIDIA's faster UI input) and its pass's views; the debug
    // view's slot empty until it is asked for.
    if (ui_pipeline_) {
      const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
      const D3D12_RESOURCE_DESC ui_desc = {
          .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
          .Width = width_,
          .Height = height_,
          .DepthOrArraySize = 1,
          .MipLevels = 1,
          .Format = DXGI_FORMAT_R8_UNORM,
          .SampleDesc = {.Count = 1},
          .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      };
      if (SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &ui_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                     IID_PPV_ARGS(ui_.GetAddressOf())))) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = ui_heap_->GetCPUDescriptorHandleForHeapStart();
        device_->CreateShaderResourceView(frame_.Get(), nullptr, handle);
        handle.ptr += descriptor_step_;
        device_->CreateShaderResourceView(hudless_.Get(), nullptr, handle);
        handle.ptr += descriptor_step_;
        device_->CreateUnorderedAccessView(ui_.Get(), nullptr, nullptr, handle);
        handle.ptr += descriptor_step_;
        const D3D12_UNORDERED_ACCESS_VIEW_DESC empty_desc = {.Format = format_, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D};
        device_->CreateUnorderedAccessView(nullptr, nullptr, &empty_desc, handle);
      }
    }
    return true;
  }

  // The swap chain and window kept (made again at the next frame of another size or format).
  void Close() {
    WaitIdle();
    frame_.Reset();
    hudless_.Reset();
    depth_.Reset();
    motion_.Reset();
    fence_.Reset();
    ui_.Reset();
    debug_view_.Reset();
  }

  // One present (the request's value: the frame is written; the next is signalled once copied). False: not presented
  // (the game's own image shows).
  bool Present(dlaa_bridge::Shared* shared) {
    const uint64_t value = shared->value;
    if (!fence_ || !frame_ || shared->fg_window == 0) {
      SignalOnly(value);
      return false;
    }
    const auto game_window = reinterpret_cast<HWND>(static_cast<uintptr_t>(shared->fg_window));
    if (shared->fg_multiplier == 0 || !IsWindow(game_window)) {
      // Off: generation off, the window hidden (the game's image shows); its swap chain kept for later.
      SetGeneration(0, false, false, false);
      if (window_ != nullptr) ShowWindowAsync(window_, SW_HIDE);
      SignalOnly(value);
      status_ = "Frame generation off";
      return false;
    }
    // NVIDIA's preset into the driver profile (DLSS-G reads it when it starts: a new one applies at the next start), on
    // a thread of its own (loading the driver's profiles takes long).
    if (version_ >= 12 && shared->fg_preset != preset_) {
      preset_ = shared->fg_preset;
      std::thread([this, preset = preset_] { preset_result_.store(SavePreset(preset)); }).detach();
    }
    if (!PrepareWindow(game_window, shared->fg_sdr != 0)) {
      SignalOnly(value);
      return false;
    }
    if (shared->fg_reflex_boost != reflex_boost_ || shared->fg_frame_limit != frame_limit_) {
      reflex_boost_ = shared->fg_reflex_boost;
      frame_limit_ = shared->fg_frame_limit;
      sl::ReflexOptions reflex = {};
      reflex.mode = (reflex_boost_ != 0 ? sl::ReflexMode::eLowLatencyWithBoost : sl::ReflexMode::eLowLatency);
      reflex.frameLimitUs = (frame_limit_ != 0 ? 1000000u / std::min(frame_limit_, 1000u) : 0u);
      set_reflex_(reflex);
    }
    const uint32_t frame = shared->fg_frame_index;
    sl::FrameToken* token = nullptr;
    uint32_t frame_index = frame;
    frame_token_(token, &frame_index);

    // The copy into the swap chain's back buffer and Streamline's copies of the inputs, after the addon's value.
    Slot& slot = slots_[next_slot_];
    next_slot_ = (next_slot_ + 1) % slots_.size();
    WaitFor(slot.done);
    slot.allocator->Reset();
    slot.list->Reset(slot.allocator.Get(), nullptr);
    ComPtr<ID3D12Resource> target;
    if (FAILED(chain_->GetBuffer(chain_->GetCurrentBackBufferIndex(), IID_PPV_ARGS(target.GetAddressOf())))) {
      slot.list->Close();
      SignalOnly(value);
      return Fail("the swap chain has no back buffer");
    }
    const bool inputs = shared->fg_inputs != 0 && token != nullptr && shared->width != 0 && shared->height != 0;
    const bool hudless_valid = inputs && shared->fg_hudless_valid != 0;
    // The debug view (v13): 1 the HUD-less image presented (no UI layer then), 2 the frame with its UI layer tinted.
    debug_ = (version_ >= 13 && hudless_valid ? shared->fg_debug : 0u);
    // The UI layer from the frame and its HUD-less image (v12, HUD Recomposition), before the frame's copy.
    const bool ui = hudless_valid && ui_ && version_ >= 12 && shared->fg_ui != 0 && debug_ != 1;
    // The debug view's frame (the swap chain's format, the pass's last view), made at its first use.
    if (ui && debug_ == 2 && !debug_view_) {
      const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_DEFAULT};
      const D3D12_RESOURCE_DESC debug_desc = {
          .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
          .Width = width_,
          .Height = height_,
          .DepthOrArraySize = 1,
          .MipLevels = 1,
          .Format = format_,
          .SampleDesc = {.Count = 1},
          .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      };
      if (SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &debug_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                     IID_PPV_ARGS(debug_view_.GetAddressOf())))) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = ui_heap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += 3 * static_cast<SIZE_T>(descriptor_step_);
        device_->CreateUnorderedAccessView(debug_view_.Get(), nullptr, nullptr, handle);
      }
    }
    const bool tinted = ui && debug_ == 2 && debug_view_;
    ID3D12Resource* source = frame_.Get();
    if (debug_ == 1) {
      source = hudless_.Get();
    } else if (tinted) {
      source = debug_view_.Get();
    }
    const auto transition = [](ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
      return D3D12_RESOURCE_BARRIER{.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                                    .Transition = {.pResource = resource, .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                   .StateBefore = before, .StateAfter = after}};
    };
    // The frame and the HUD-less image are read by the pass and the copy; the shared textures decay to COMMON.
    constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    std::array<D3D12_RESOURCE_BARRIER, 5> barriers = {
        transition(frame_.Get(), D3D12_RESOURCE_STATE_COMMON, READ),
        transition(hudless_.Get(), D3D12_RESOURCE_STATE_COMMON, READ),
        transition(target.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST),
        transition(ui_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        transition(debug_view_.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
    };
    slot.list->ResourceBarrier((tinted ? 5u : (ui ? 4u : 3u)), barriers.data());
    if (ui) {
      ID3D12DescriptorHeap* heap = ui_heap_.Get();
      slot.list->SetDescriptorHeaps(1, &heap);
      slot.list->SetComputeRootSignature(ui_signature_.Get());
      slot.list->SetPipelineState(ui_pipeline_.Get());
      slot.list->SetComputeRootDescriptorTable(0, ui_heap_->GetGPUDescriptorHandleForHeapStart());
      // 2.5 of the swap chain format's steps (HDR10 10 bits, SDR 8).
      const float threshold = 2.5f / (format_ == DXGI_FORMAT_R10G10B10A2_UNORM ? 1023.f : 255.f);
      const std::array<uint32_t, 4> constants = {width_, height_, std::bit_cast<uint32_t>(threshold), (tinted ? 1u : 0u)};
      slot.list->SetComputeRoot32BitConstants(1, static_cast<UINT>(constants.size()), constants.data(), 0);
      slot.list->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
      barriers[0] = transition(ui_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
      barriers[1] = transition(debug_view_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
      slot.list->ResourceBarrier((tinted ? 2u : 1u), barriers.data());
    }
    slot.list->CopyResource(target.Get(), source);
    barriers = {
        transition(frame_.Get(), READ, D3D12_RESOURCE_STATE_COMMON),
        transition(hudless_.Get(), READ, D3D12_RESOURCE_STATE_COMMON),
        transition(target.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT),
        transition(debug_view_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
    };
    slot.list->ResourceBarrier((tinted ? 4u : 3u), barriers.data());
    if (inputs) {
      sl::Resource depth = {sl::ResourceType::eTex2d, depth_.Get(), D3D12_RESOURCE_STATE_COMMON};
      sl::Resource motion = {sl::ResourceType::eTex2d, motion_.Get(), D3D12_RESOURCE_STATE_COMMON};
      sl::Resource hudless = {sl::ResourceType::eTex2d, hudless_.Get(), D3D12_RESOURCE_STATE_COMMON};
      sl::Resource ui_layer = {sl::ResourceType::eTex2d, ui_.Get(), D3D12_RESOURCE_STATE_COMMON};
      const sl::Extent render = {.top = 0, .left = 0, .width = shared->width, .height = shared->height};
      const std::array<sl::ResourceTag, 4> tags = {
          sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &render),
          sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &render),
          sl::ResourceTag(&hudless, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eOnlyValidNow),
          sl::ResourceTag(&ui_layer, sl::kBufferTypeUIAlpha, sl::ResourceLifecycle::eOnlyValidNow),
      };
      const uint32_t tag_count = ui ? 4u : hudless_valid ? 3u : 2u;
      set_tags_(*token, VIEWPORT, tags.data(), tag_count, slot.list.Get());
    }
    slot.list->Close();
    copy_queue_->Wait(fence_.Get(), value);
    ID3D12CommandList* lists[] = {slot.list.Get()};
    copy_queue_->ExecuteCommandLists(1, lists);
    copy_queue_->Signal(fence_.Get(), value + 1);
    slot.done = ++own_value_;
    copy_queue_->Signal(own_fence_.Get(), slot.done);
    present_queue_->Wait(fence_.Get(), value + 1);

    // What to generate: off on frames without the inputs (menus, loading), keeping its memory meanwhile. Dynamic (1):
    // up to the most, as many as the display's refresh rate takes (else 2x).
    const bool dynamic = shared->fg_multiplier == 1 && dynamic_supported_;
    const uint32_t frames = dynamic ? std::max(max_frames_, 2u) : std::clamp(shared->fg_multiplier, 2u, (max_frames_ != 0 ? max_frames_ : 4u));
    SetGeneration(inputs ? frames - 1 : 0, shared->fg_hudless_valid != 0, dynamic, ui);
    if (inputs) SetConstants(*shared, *token);
    Marker(sl::PCLMarker::eRenderSubmitEnd, token);
    Marker(sl::PCLMarker::ePresentStart, token);
    const HRESULT presented = chain_->Present(0, tearing_ != 0 ? DXGI_PRESENT_ALLOW_TEARING : 0);
    Marker(sl::PCLMarker::ePresentEnd, token);
    if (!IsWindowVisible(window_)) ShowWindowAsync(window_, SW_SHOWNA);
    // The next frame's Reflex sleep, simulation and submission start (the game waits for this answer).
    uint32_t next_index = frame + 1;
    sl::FrameToken* next = nullptr;
    if (frame_token_(next, &next_index) == sl::Result::eOk && next != nullptr) {
      sleep_(*next);
      Marker(sl::PCLMarker::eSimulationStart, next);
      Marker(sl::PCLMarker::eSimulationEnd, next);
      Marker(sl::PCLMarker::eRenderSubmitStart, next);
    }
    UpdateStatus(inputs, shared->fg_hudless_valid != 0);
    return SUCCEEDED(presented);
  }

  // Off for long (main.cpp's IDLE_RELEASE): as NVIDIA's guide asks of a feature turned off in the settings, its resources
  // freed (eRetainResourcesWhenOff keeps them through menus: MFG's video memory did not come back, ATN 2026-11-01), the
  // window and swap chain gone too. The next present with it on makes them again. Streamline stays: stopping it here (v126)
  // left the helper silent at the first Ray Reconstruction + ray tracing start after it (ATN's log, 2026-11-01).
  void FreeResources() {
    WaitIdle();
    SetGeneration(0, false, false, false);
    if (free_resources_ != nullptr) free_resources_(sl::kFeatureDLSS_G, VIEWPORT);
    chain_.Reset();
    if (window_ != nullptr) PostMessageW(window_, WM_CLOSE, 0, 0);
    if (window_thread_.joinable()) window_thread_.join();
    window_ = nullptr;
  }

  // The window and swap chain gone, Streamline stopped (the helper quits, or a GPU reset).
  void Shutdown() {
    WaitIdle();
    chain_.Reset();
    if (window_ != nullptr) PostMessageW(window_, WM_CLOSE, 0, 0);
    if (window_thread_.joinable()) window_thread_.join();
    streamline_queue_.Reset();
    streamline_device_.Reset();
    if (initialized_) shutdown_();
    initialized_ = false;
  }

  const std::string& Status() const { return status_; }
  // A GPU reset removed its device (the helper starts Streamline again on a new one).
  [[nodiscard]] bool Lost() const { return device_ && FAILED(device_->GetDeviceRemovedReason()); }

 private:
  struct Slot {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    uint64_t done = 0;
  };

  inline static const sl::ViewportHandle VIEWPORT = {0u};
  static constexpr int PRESET_HELD = 0;
  static constexpr int PRESET_SAVED = 1;
  static constexpr int PRESET_FAILED = 2;
  static constexpr DWORD FENCE_TIMEOUT = 10'000;

  bool Fail(const std::string& status) {
    failed_ = true;
    status_ = "Frame generation: " + status;
    return false;
  }

  template <typename Function>
  bool Resolve(Function** function, const char* name) {
    *function = reinterpret_cast<Function*>(GetProcAddress(module_, name));
    return *function != nullptr;
  }

  template <typename Function>
  bool Resolve(sl::Feature feature, Function** function, const char* name) {
    void* address = nullptr;
    if (feature_function_(feature, name, address) != sl::Result::eOk || address == nullptr) return false;
    *function = reinterpret_cast<Function*>(address);
    return true;
  }

  void Marker(sl::PCLMarker marker, sl::FrameToken* token) {
    if (token != nullptr) set_marker_(marker, *token);
  }

  // The copy queue's fence `value` signalled (the game never waits for nothing).
  void SignalOnly(uint64_t value) {
    if (!fence_ || !copy_queue_) return;
    copy_queue_->Wait(fence_.Get(), value);
    copy_queue_->Signal(fence_.Get(), value + 1);
  }

  void WaitFor(uint64_t value) {
    if (!own_fence_ || own_fence_->GetCompletedValue() >= value) return;
    own_fence_->SetEventOnCompletion(value, event_);
    WaitForSingleObject(event_, FENCE_TIMEOUT);
  }

  void WaitIdle() {
    if (!present_queue_ || !own_fence_) return;
    const uint64_t copied = ++own_value_;
    copy_queue_->Signal(own_fence_.Get(), copied);
    present_queue_->Wait(own_fence_.Get(), copied);
    const uint64_t presented = ++own_value_;
    present_queue_->Signal(own_fence_.Get(), presented);
    WaitFor(presented);
  }

  // Frame generation's mode: `generated` frames per rendered frame (0: off; Dynamic: at most), with the HUD-less image
  // or not, the UI layer recomposed or not (kept while off: no new allocation for each menu).
  void SetGeneration(uint32_t generated, bool hudless, bool dynamic, bool ui) {
    if (generated == 0) ui = applied_ui_;
    if (!set_options_
        || (generated == applied_generated_ && hudless == applied_hudless_ && dynamic == applied_dynamic_ && ui == applied_ui_)) {
      return;
    }
    applied_generated_ = generated;
    applied_hudless_ = hudless;
    applied_dynamic_ = dynamic;
    applied_ui_ = ui;
    sl::DLSSGOptions options = {};
    options.mode = (generated == 0 ? sl::DLSSGMode::eOff : dynamic ? sl::DLSSGMode::eDynamic : sl::DLSSGMode::eOn);
    options.numFramesToGenerate = std::max(generated, 1u);
    options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
    options.hudLessBufferFormat = (hudless ? static_cast<uint32_t>(format_) : 0u);
    options.enableUserInterfaceRecomposition = (ui ? sl::Boolean::eTrue : sl::Boolean::eFalse);
    set_options_(VIEWPORT, options);
  }

#if __has_include(<nvapi.h>)
  // NVIDIA's frame generation preset in the driver profile of this exe (DLSS-G's key 0x10E41DF1, which it reads from
  // there when it starts: 1-3 A-C; 0 the driver's own, the key removed). A, B and C as nvngx_dlssg 310.9 applies them:
  // A plain, B the HUD recomposed, C "FG Lite" candidate blending. PRESET_HELD (the profile had it), PRESET_SAVED or
  // PRESET_FAILED (NVAPI or the driver refused).
  static int SavePreset(uint32_t preset) {
    static std::mutex saving;
    const std::unique_lock lock(saving);
    constexpr NvU32 RENDER_PRESET = 0x10E41DF1;
    const auto unicode = [](const std::wstring& text, NvAPI_UnicodeString* out) {
      const size_t length = std::min(text.size(), static_cast<size_t>(NVAPI_UNICODE_STRING_MAX - 1));
      for (size_t index = 0; index < length; ++index) (*out)[index] = static_cast<NvU16>(text[index]);
      (*out)[length] = 0;
    };
    if (NvAPI_Initialize() != NVAPI_OK) return PRESET_FAILED;
    NvDRSSessionHandle session = nullptr;
    if (NvAPI_DRS_CreateSession(&session) != NVAPI_OK) return PRESET_FAILED;
    int saved = PRESET_FAILED;
    if (NvAPI_DRS_LoadSettings(session) == NVAPI_OK) {
      std::wstring path(MAX_PATH, L'\0');
      path.resize(GetModuleFileNameW(nullptr, path.data(), MAX_PATH));
      const std::wstring exe = path.substr(path.find_last_of(L"\\/") + 1);
      auto application = std::make_unique<NVDRS_APPLICATION>();
      application->version = NVDRS_APPLICATION_VER;
      NvAPI_UnicodeString exe_name = {};
      unicode(exe, &exe_name);
      NvDRSProfileHandle profile = nullptr;
      if (NvAPI_DRS_FindApplicationByName(session, exe_name, &profile, application.get()) != NVAPI_OK) {
        profile = nullptr;
        // A profile of its own for this exe, only to hold a preset.
        if (preset != 0) {
          auto profile_info = std::make_unique<NVDRS_PROFILE>();
          profile_info->version = NVDRS_PROFILE_VER;
          unicode(L"RenoDX Total War DLSS helper", &profile_info->profileName);
          if (NvAPI_DRS_CreateProfile(session, profile_info.get(), &profile) != NVAPI_OK
              && NvAPI_DRS_FindProfileByName(session, profile_info->profileName, &profile) != NVAPI_OK) {
            profile = nullptr;
          }
          auto created = std::make_unique<NVDRS_APPLICATION>();
          created->version = NVDRS_APPLICATION_VER;
          unicode(exe, &created->appName);
          if (profile != nullptr && NvAPI_DRS_CreateApplication(session, profile, created.get()) != NVAPI_OK) profile = nullptr;
        }
      }
      if (profile == nullptr) {
        saved = (preset == 0 ? PRESET_HELD : PRESET_FAILED);
      } else {
        auto setting = std::make_unique<NVDRS_SETTING>();
        setting->version = NVDRS_SETTING_VER;
        const bool held = NvAPI_DRS_GetSetting(session, profile, RENDER_PRESET, setting.get()) == NVAPI_OK
                          && setting->settingLocation == NVDRS_CURRENT_PROFILE_LOCATION;
        if (preset == 0 ? !held : (held && setting->u32CurrentValue == preset)) {
          saved = PRESET_HELD;
        } else {
          NvAPI_Status status = NVAPI_OK;
          if (preset == 0) {
            status = NvAPI_DRS_DeleteProfileSetting(session, profile, RENDER_PRESET);
          } else {
            *setting = {};
            setting->version = NVDRS_SETTING_VER;
            setting->settingId = RENDER_PRESET;
            setting->settingType = NVDRS_DWORD_TYPE;
            setting->u32CurrentValue = preset;
            status = NvAPI_DRS_SetSetting(session, profile, setting.get());
          }
          saved = (status == NVAPI_OK && NvAPI_DRS_SaveSettings(session) == NVAPI_OK) ? PRESET_SAVED : PRESET_FAILED;
        }
      }
    }
    NvAPI_DRS_DestroySession(session);
    return saved;
  }
#else
  static int SavePreset(uint32_t preset) { return preset == 0 ? PRESET_HELD : PRESET_FAILED; }
#endif

  // The window over the game's client area (made with its swap chain at the frame's size and format; resized with the
  // game's window). False: no window or swap chain.
  bool PrepareWindow(HWND game_window, bool sdr) {
    if (window_ != nullptr && !IsWindow(window_)) {
      window_ = nullptr;
      chain_.Reset();
    }
    if (window_ == nullptr || parent_ != game_window) {
      if (window_ != nullptr) {
        chain_.Reset();
        PostMessageW(window_, WM_CLOSE, 0, 0);
        if (window_thread_.joinable()) window_thread_.join();
        window_ = nullptr;
      }
      parent_ = game_window;
      std::atomic<HWND> made = nullptr;
      std::atomic_bool ready = false;
      // Its own thread keeps its messages flowing: the game's window thread sends to its child (and their input
      // queues are attached), so the helper's waits must never hold them up.
      window_thread_ = std::thread([game_window, &made, &ready] {
        const WNDCLASSW window_class = {.lpfnWndProc = WindowProcedure, .hInstance = GetModuleHandleW(nullptr), .lpszClassName = L"RenoDX_TW_FG"};
        RegisterClassW(&window_class);
        RECT client = {};
        GetClientRect(game_window, &client);
        HWND window = CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"RenoDX_TW_FG", L"RenoDX frame generation", WS_CHILD | WS_DISABLED, 0, 0,
                                      std::max(client.right, 1L), std::max(client.bottom, 1L), game_window, nullptr,
                                      window_class.hInstance, nullptr);
        made.store(window);
        ready.store(true);
        if (window == nullptr) return;
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
          DispatchMessageW(&message);
        }
      });
      while (!ready.load()) {
        Sleep(1);
      }
      window_ = made.load();
      if (window_ == nullptr) {
        window_thread_.join();
        return Fail("its window over the game's could not be made");
      }
    }
    // The client area's size: the window follows it (the swap chain stretches the frame to it).
    RECT client = {};
    GetClientRect(parent_, &client);
    const int client_width = std::max(static_cast<int>(client.right), 1);
    const int client_height = std::max(static_cast<int>(client.bottom), 1);
    if (client_width != window_width_ || client_height != window_height_) {
      window_width_ = client_width;
      window_height_ = client_height;
      SetWindowPos(window_, HWND_TOP, 0, 0, client_width, client_height, SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    }
    if (chain_ && chain_width_ == width_ && chain_height_ == height_ && chain_format_ == format_) return true;
    if (chain_) {
      // Generation off while the buffers change (NVIDIA: Present can deadlock otherwise).
      SetGeneration(0, false, false, false);
      WaitIdle();
      const HRESULT resized = chain_->ResizeBuffers(0, width_, height_, format_, tearing_);
      if (SUCCEEDED(resized)) {
        chain_width_ = width_;
        chain_height_ = height_;
        chain_format_ = format_;
        return true;
      }
      chain_.Reset();
    }
    ComPtr<IDXGIFactory5> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf())))) return Fail("no DXGI factory");
    BOOL tearing = FALSE;
    if (SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing) {
      tearing_ = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
    void* upgraded = factory.Get();
    if (upgrade_(&upgraded) != sl::Result::eOk) return Fail("Streamline did not take the DXGI factory");
    if (upgraded == factory.Get()) factory->AddRef();
    ComPtr<IDXGIFactory2> streamline_factory;
    streamline_factory.Attach(static_cast<IDXGIFactory2*>(upgraded));
    const DXGI_SWAP_CHAIN_DESC1 desc = {
        .Width = width_,
        .Height = height_,
        .Format = format_,
        .SampleDesc = {.Count = 1},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 3,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = tearing_,
    };
    ComPtr<IDXGISwapChain1> created;
    if (FAILED(streamline_factory->CreateSwapChainForHwnd(streamline_queue_.Get(), window_, &desc, nullptr, nullptr, created.GetAddressOf()))
        || FAILED(created.As(&chain_))) {
      return Fail("its swap chain could not be made");
    }
    // HDR10 (R10G10B10A2) and scRGB (RGBA16F) frames as the game's swap chain shows them. SDR frames (gamma 2.2, in
    // R10G10B10A2 too) keep the chain's default: taken for HDR10 they came out red (ATN 2026-11-01).
    if (format_ == DXGI_FORMAT_R10G10B10A2_UNORM && !sdr) chain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
    if (format_ == DXGI_FORMAT_R16G16B16A16_FLOAT) chain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
    chain_width_ = width_;
    chain_height_ = height_;
    chain_format_ = format_;
    return true;
  }

  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w_parameter, LPARAM l_parameter) {
    switch (message) {
      // The game's window under it takes the mouse.
      case WM_NCHITTEST:
        return HTTRANSPARENT;
      case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
      case WM_CLOSE:
        DestroyWindow(window);
        return 0;
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
      default:
        return DefWindowProcW(window, message, w_parameter, l_parameter);
    }
  }

  // The camera as Streamline takes it (row vectors, without the jitter; standard depth, 0 near).
  void SetConstants(const dlaa_bridge::Shared& shared, sl::FrameToken& token) {
    const auto to_matrix = [](const float* values) {
      sl::float4x4 result;
      for (int row = 0; row < 4; ++row) {
        result[row] = sl::float4(values[row * 4], values[(row * 4) + 1], values[(row * 4) + 2], values[(row * 4) + 3]);
      }
      return result;
    };
    // Gauss-Jordan with partial pivoting, in doubles.
    const auto inverse = [](const float* matrix) {
      std::array<std::array<double, 8>, 4> rows = {};
      for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) rows[row][column] = matrix[(row * 4) + column];
        rows[row][4 + row] = 1.;
      }
      for (int column = 0; column < 4; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 4; ++row) {
          if (std::abs(rows[row][column]) > std::abs(rows[pivot][column])) pivot = row;
        }
        std::swap(rows[pivot], rows[column]);
        const double scale = (rows[column][column] != 0. ? 1. / rows[column][column] : 0.);
        for (double& value : rows[column]) value *= scale;
        for (int row = 0; row < 4; ++row) {
          if (row == column) continue;
          const double factor = rows[row][column];
          for (int index = 0; index < 8; ++index) rows[row][index] -= factor * rows[column][index];
        }
      }
      std::array<float, 16> result = {};
      for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) result[(row * 4) + column] = static_cast<float>(rows[row][4 + column]);
      }
      return result;
    };
    const float* projection = shared.fg_view_to_clip;
    constexpr std::array<float, 16> IDENTITY = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    sl::Constants constants = {};
    constants.cameraViewToClip = to_matrix(projection);
    constants.clipToCameraView = to_matrix(inverse(projection).data());
    constants.clipToLensClip = to_matrix(IDENTITY.data());
    // Clip to last frame's clip: back to the world with this frame's camera, through last frame's.
    const auto clip_to_world = inverse(shared.fg_view_projection);
    std::array<float, 16> clip_to_previous_clip = {};
    for (int row = 0; row < 4; ++row) {
      for (int column = 0; column < 4; ++column) {
        for (int k = 0; k < 4; ++k) {
          clip_to_previous_clip[(row * 4) + column] += clip_to_world[(row * 4) + k] * shared.fg_previous_view_projection[(k * 4) + column];
        }
      }
    }
    constants.clipToPrevClip = to_matrix(clip_to_previous_clip.data());
    constants.prevClipToClip = to_matrix(inverse(clip_to_previous_clip.data()).data());
    constants.jitterOffset = sl::float2(shared.jitter_x, shared.jitter_y);
    constants.mvecScale = sl::float2(1.f / static_cast<float>(shared.width), 1.f / static_cast<float>(shared.height));
    constants.cameraPinholeOffset = sl::float2(0.f, 0.f);
    const float* camera = shared.fg_camera;
    constants.cameraPos = sl::float3(camera[0], camera[1], camera[2]);
    constants.cameraRight = sl::float3(camera[3], camera[4], camera[5]);
    constants.cameraUp = sl::float3(camera[6], camera[7], camera[8]);
    constants.cameraFwd = sl::float3(camera[9], camera[10], camera[11]);
    constants.cameraNear = shared.fg_near;
    constants.cameraFar = shared.fg_far;
    constants.cameraFOV = 2.f * std::atan(1.f / projection[5]);
    constants.cameraAspectRatio = projection[5] / projection[0];
    constants.depthInverted = sl::Boolean::eFalse;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.reset = (shared.reset != 0 ? sl::Boolean::eTrue : sl::Boolean::eFalse);
    constants.motionVectorsJittered = sl::Boolean::eFalse;
    set_constants_(constants, token, VIEWPORT);
  }

  // The status line every 30 presents.
  void UpdateStatus(bool inputs, bool hudless) {
    sl::DLSSGState state = {};
    if (++presents_ % 30 != 0 || get_state_(VIEWPORT, state, nullptr) != sl::Result::eOk) return;
    max_frames_ = state.numFramesToGenerateMax + 1;
    dynamic_supported_ = state.bIsDynamicMFGSupported == sl::Boolean::eTrue;
    if (format_ == DXGI_FORMAT_R16G16B16A16_FLOAT) {
      status_ = "Frame generation: not generating (it takes no scRGB: set Output to HDR10 or SDR)";
    } else if (state.status != sl::DLSSGStatus::eOk) {
      status_ = std::format("Frame generation: not generating (status {:#x})", static_cast<uint32_t>(state.status));
    } else if (!inputs) {
      status_ = "Frame generation: paused (DLAA is not running on this frame)";
    } else {
      std::string preset;
      const int preset_result = preset_result_.load();
      if (preset_result == PRESET_FAILED) {
        preset = " (preset not saved: the NVIDIA driver profile could not be written)";
      } else if (preset_result == PRESET_SAVED) {
        preset = " (the preset applies after restarting the game)";
      } else if (preset_ >= 1 && preset_ <= 3) {
        preset = std::format(" (preset {})", "ABC"[preset_ - 1]);
      }
      const char* debug_view = "";
      if (debug_ == 1) {
        debug_view = ", debug view: the HUD-less image";
      } else if (debug_ == 2) {
        debug_view = ", debug view: the UI layer in magenta";
      }
      status_ = std::format("Frame generation: {}{} frames presented per rendered frame{}{}{}{}", (applied_dynamic_ ? "Dynamic, " : ""),
                            state.numFramesActuallyPresented, (hudless ? "" : " (no HUD-less image: the UI can warp)"),
                            (applied_ui_ ? ", HUD recomposed" : ""), preset, debug_view);
    }
  }

  HMODULE module_ = nullptr;
  PFun_slInit* init_ = nullptr;
  PFun_slShutdown* shutdown_ = nullptr;
  PFun_slIsFeatureSupported* is_supported_ = nullptr;
  PFun_slSetD3DDevice* set_device_ = nullptr;
  PFun_slUpgradeInterface* upgrade_ = nullptr;
  PFun_slGetNativeInterface* native_ = nullptr;
  PFun_slGetFeatureFunction* feature_function_ = nullptr;
  PFun_slGetNewFrameToken* frame_token_ = nullptr;
  PFun_slSetTagForFrame* set_tags_ = nullptr;
  PFun_slSetConstants* set_constants_ = nullptr;
  PFun_slFreeResources* free_resources_ = nullptr;
  PFun_slDLSSGSetOptions* set_options_ = nullptr;
  PFun_slDLSSGGetState* get_state_ = nullptr;
  PFun_slReflexSetOptions* set_reflex_ = nullptr;
  PFun_slReflexSleep* sleep_ = nullptr;
  PFun_slPCLSetMarker* set_marker_ = nullptr;
  bool initialized_ = false;
  bool failed_ = false;
  std::string status_ = "Frame generation not started";

  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12Device> streamline_device_;
  ComPtr<ID3D12CommandQueue> streamline_queue_;
  ComPtr<ID3D12CommandQueue> present_queue_;
  ComPtr<ID3D12CommandQueue> copy_queue_;
  ComPtr<ID3D12Fence> own_fence_;
  uint64_t own_value_ = 0;
  std::array<Slot, 3> slots_;
  size_t next_slot_ = 0;
  HANDLE event_ = nullptr;

  // The addon's objects and bridge version.
  ComPtr<ID3D12Resource> frame_;
  ComPtr<ID3D12Resource> hudless_;
  ComPtr<ID3D12Resource> depth_;
  ComPtr<ID3D12Resource> motion_;
  ComPtr<ID3D12Fence> fence_;
  UINT width_ = 0;
  UINT height_ = 0;
  DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
  uint32_t version_ = 0;

  // The UI layer (HUD Recomposition) and its pass; the debug view's frame (the layer tinted: made at its first use).
  ComPtr<ID3D12RootSignature> ui_signature_;
  ComPtr<ID3D12PipelineState> ui_pipeline_;
  ComPtr<ID3D12DescriptorHeap> ui_heap_;
  UINT descriptor_step_ = 0;
  ComPtr<ID3D12Resource> ui_;
  ComPtr<ID3D12Resource> debug_view_;
  uint32_t debug_ = 0;  // this present's debug view (bridge v13's fg_debug)

  // NVIDIA's preset: last asked (UINT32_MAX: not yet) and its save (SavePreset's, from its thread).
  uint32_t preset_ = UINT32_MAX;
  std::atomic_int preset_result_ = PRESET_HELD;

  // The window over the game's, its thread and swap chain.
  HWND parent_ = nullptr;
  HWND window_ = nullptr;
  std::thread window_thread_;
  int window_width_ = 0;
  int window_height_ = 0;
  ComPtr<IDXGISwapChain4> chain_;
  UINT chain_width_ = 0;
  UINT chain_height_ = 0;
  DXGI_FORMAT chain_format_ = DXGI_FORMAT_UNKNOWN;
  UINT tearing_ = 0;
  uint32_t reflex_boost_ = 0;
  uint32_t frame_limit_ = 0;  // Reflex's frame rate limit as last set (rendered frames per second; 0: none)
  uint32_t applied_generated_ = UINT32_MAX;
  bool applied_hudless_ = false;
  bool applied_dynamic_ = false;
  bool applied_ui_ = false;
  uint32_t max_frames_ = 0;
  bool dynamic_supported_ = false;  // Dynamic frame generation (DLSS 4.5, RTX 50)
  uint64_t presents_ = 0;
};

}  // namespace twattila::dlaa_helper
