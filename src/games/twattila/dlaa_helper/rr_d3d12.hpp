/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <vector>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_dlssd.h>

#include "../../talesofarise_dlss/ngx_backend.hpp"

// The DLAA helper's Ray Reconstruction (NVIDIA's denoising DLAA / DLSS) through D3D12: D3D11's crashes in NVIDIA's D3D11
// driver when the GPU is waited for right after it (as the bridge's fence makes the game wait), so this runs on a D3D12
// queue of its own, which waits for the addon's fence value and signals the next. The addon's shared textures and
// fence are opened in D3D12 as well (states: COMMON between the APIs). Tales' DLSS add-on runs it in the game's process
// (talesofarise_dlss/rr_d3d12.hpp) for the same reason.
namespace twattila::dlaa_helper {

using Microsoft::WRL::ComPtr;

// The textures as the bridge orders them, opened in D3D12.
enum Input : size_t {
  COLOR,
  DEPTH,
  MOTION,
  OUTPUT,
  DIFFUSE_ALBEDO,
  SPECULAR_ALBEDO,
  NORMALS,
  ROUGHNESS,
  INPUT_COUNT,
};
// What FSR and XeSS read of them (upscale_d3d12.hpp).
constexpr size_t UPSCALE_INPUTS = OUTPUT + 1;
// Ray Reconstruction's optional inputs, in the bridge's order (RR_EXTRA_*).
enum ExtraInput : size_t { PARTICLE_LAYER, COLOR_BEFORE, DOF_GUIDE, EXTRA_INPUTS };

// Requests in flight: each with its own command allocator and list.
constexpr size_t SLOTS = 3;

// A D3D12 device on the game's GPU of its own, apart from D3D12CreateDevice's one of the process that Streamline holds. While
// anything holds a device a GPU reset removed no new device can be made in this process (D3D12CreateDevice and the factories
// answer with the removal), and Streamline does not let go (ATN 2026-11-03: Ray Reconstruction's hang took Streamline's device
// with it, off by then: no Ray Reconstruction, FSR or XeSS until the game ended). RENODX_TW_D3D12_DEBUG (tests; D3D12's debug
// layer): its messages in a debugger's output.
inline HRESULT MakeOwnDevice(IDXGIAdapter1* adapter, ComPtr<ID3D12Device>* device) {
  constexpr GUID DEVICE_FACTORY = {0x114863bf, 0xc386, 0x4aee, {0xb3, 0x9d, 0x8f, 0x0b, 0xbb, 0x06, 0x29, 0x55}};  // CLSID_D3D12DeviceFactory
  constexpr GUID DEBUG_LAYER = {0xf2352aeb, 0xdd84, 0x49fe, {0xb9, 0x7b, 0xa9, 0xdc, 0xfd, 0xcc, 0x1b, 0x4f}};      // CLSID_D3D12Debug
  if (ComPtr<ID3D12DeviceFactory> factory; SUCCEEDED(D3D12GetInterface(DEVICE_FACTORY, IID_PPV_ARGS(factory.GetAddressOf())))) {
    factory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON);
    if (ComPtr<ID3D12Debug> debug; GetEnvironmentVariableW(L"RENODX_TW_D3D12_DEBUG", nullptr, 0) != 0
                                   && SUCCEEDED(factory->GetConfigurationInterface(DEBUG_LAYER, IID_PPV_ARGS(debug.GetAddressOf())))) {
      debug->EnableDebugLayer();
    }
    if (SUCCEEDED(factory->CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device->GetAddressOf())))) return S_OK;
  }
  return D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device->GetAddressOf()));
}

// One request's settings.
struct Request {
  uint64_t value = 0;  // the addon's fence value its inputs are written at (the next is signalled after)
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t output_width = 0;
  uint32_t output_height = 0;
  float jitter_x = 0.f;
  float jitter_y = 0.f;
  bool reset = false;
  uint32_t preset = 0;  // NVSDK_NGX_RayReconstruction_Hint_Render_Preset
  std::array<float, 16> world_to_view = {};
  std::array<float, 16> view_to_clip = {};
  // The addon's specular hit distance written this frame (Attila's v17: by its ray-traced reflections).
  bool hit_distance = false;
  uint32_t extra_inputs = 0;  // bits of ExtraInput: written for this evaluation
  // NVSDK_NGX_DLSS_Feature_Flags (Tales: reversed depth too) and the colour's exposure.
  int create_flags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes
                     | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
  float pre_exposure = 1.f;
};

class RayReconstruction12 {
 public:
  // A D3D12 device on the game's GPU and NVIDIA's D3D12 runtime from `folder` (nvngx_dlssd.dll); false: unavailable.
  bool Initialize(IDXGIAdapter1* adapter, const std::wstring& folder) {
    if (failed_) return false;
    if (device_) return true;
    // Not for good: tried again while it is asked for (MakeOwnDevice).
    ComPtr<ID3D12Device> device;
    if (const HRESULT made = MakeOwnDevice(adapter, &device); FAILED(made)) {
      status_ = std::format("Ray Reconstruction: no D3D12 device on the game's GPU (0x{:08X})", static_cast<uint32_t>(made));
      return false;
    }
    return Initialize(device.Get(), folder);
  }

  // As above on `device` (Tales: its add-on's own, beside the game's D3D11 one).
  bool Initialize(ID3D12Device* device, const std::wstring& folder) {
    if (failed_) return false;
    if (device_) return true;
    device_ = device;
    const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    if (FAILED(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(queue_.GetAddressOf())))
        || FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(own_fence_.GetAddressOf())))) {
      return Fail("Ray Reconstruction: no D3D12 queue");
    }
    for (Slot& slot : slots_) {
      if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(slot.allocator.GetAddressOf())))
          || FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                               IID_PPV_ARGS(slot.list.GetAddressOf())))
          || FAILED(slot.list->Close())) {
        return Fail("Ray Reconstruction: no D3D12 command lists");
      }
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::wstring cache(MAX_PATH, L'\0');
    cache.resize(GetTempPathW(MAX_PATH, cache.data()));
    cache += L"RenoDX_TW_DLAA";
    CreateDirectoryW(cache.c_str(), nullptr);
    const wchar_t* paths[] = {folder.c_str()};
    const NVSDK_NGX_FeatureCommonInfo common_info = {.PathListInfo = {.Path = paths, .Length = 1}};
    int available = 0;
    if (NVSDK_NGX_FAILED(NVSDK_NGX_D3D12_Init_with_ProjectID(talesofarise::dlss::PROJECT_ID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0",
                                                             cache.c_str(), device_.Get(), &common_info))) {
      return Fail("Ray Reconstruction: NVIDIA's D3D12 runtime did not start");
    }
    initialized_ = true;
    if (NVSDK_NGX_FAILED(NVSDK_NGX_D3D12_GetCapabilityParameters(&parameters_)) || parameters_ == nullptr
        || NVSDK_NGX_FAILED(NVSDK_NGX_Parameter_GetI(parameters_, NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &available))
        || available == 0) {
      return Fail("Ray Reconstruction unavailable (nvngx_dlssd.dll next to nvngx_dlss.dll?)");
    }
    status_ = "Ray Reconstruction ready";
    return true;
  }

  // The addon's textures (bridge order; handles still open), fence and specular hit distance (0: none); false: not all
  // could be opened (the hit distance may stay closed).
  bool Open(const std::array<uint64_t, INPUT_COUNT>& textures, uint64_t fence, uint64_t hit_distance = 0,
            const std::array<uint64_t, EXTRA_INPUTS>& extra = {}) {
    Close();
    if (!device_) return false;
    for (size_t index = 0; index < INPUT_COUNT; ++index) {
      if (FAILED(device_->OpenSharedHandle(reinterpret_cast<HANDLE>(textures[index]), IID_PPV_ARGS(inputs_[index].GetAddressOf())))) {
        Close();
        return false;
      }
    }
    if (FAILED(device_->OpenSharedHandle(reinterpret_cast<HANDLE>(fence), IID_PPV_ARGS(fence_.GetAddressOf())))) {
      Close();
      return false;
    }
    if (hit_distance != 0) device_->OpenSharedHandle(reinterpret_cast<HANDLE>(hit_distance), IID_PPV_ARGS(hit_distance_.GetAddressOf()));
    for (size_t index = 0; index < EXTRA_INPUTS; ++index) {
      if (extra[index] != 0) device_->OpenSharedHandle(reinterpret_cast<HANDLE>(extra[index]), IID_PPV_ARGS(extra_[index].GetAddressOf()));
    }
    return true;
  }

  // Its commands done, the textures let go (the game's GPU can be frames behind: releasing them under a request still
  // waiting for its value removed the device, 2026-11-03).
  void Close() {
    if (own_fence_) WaitFor(own_value_);
    inputs_ = {};
    hit_distance_.Reset();
    extra_ = {};
    fence_.Reset();
  }

  // One request: the queue waits for the addon's fence value, Ray Reconstruction runs, the next value is signalled
  // (also when it could not run, so the game never waits for nothing). False: it did not run.
  bool Evaluate(const Request& request) {
    if (!fence_ || !queue_) return false;
    // The GPU harness's RENODX_TW_TEST_DEVICE_REMOVAL=<evaluations>: the device removed at the process's evaluation of that
    // number, as a GPU hang removes it (main.cpp makes a new one).
    static const int test_removal_at = [] {
      std::array<wchar_t, 16> value = {};
      return (GetEnvironmentVariableW(L"RENODX_TW_TEST_DEVICE_REMOVAL", value.data(), static_cast<DWORD>(value.size())) != 0) ? _wtoi(value.data()) : 0;
    }();
    static int test_evaluations = 0;
    if (++test_evaluations == test_removal_at) {
      if (ComPtr<ID3D12Device5> device5; SUCCEEDED(device_.As(&device5))) device5->RemoveDevice();
    }
    Slot& slot = slots_[next_slot_];
    next_slot_ = (next_slot_ + 1) % SLOTS;
    evaluated_ = false;
    const bool recorded = Record(request, &slot);
    queue_->Wait(fence_.Get(), request.value);
    if (recorded) {
      ID3D12CommandList* lists[] = {slot.list.Get()};
      queue_->ExecuteCommandLists(1, lists);
    }
    queue_->Signal(fence_.Get(), request.value + 1);
    queue_->Signal(own_fence_.Get(), ++own_value_);
    slot.value = own_value_;
    return evaluated_;
  }

  // Its feature freed (Super Resolution runs instead); the next request makes it again.
  void ReleaseFeature() {
    if (feature_ == nullptr) return;
    WaitFor(own_value_);
    NVSDK_NGX_D3D12_ReleaseFeature(feature_);
    feature_ = nullptr;
  }

  [[nodiscard]] bool Ready() const { return fence_ != nullptr; }
  // A GPU reset removed its device (the helper makes a new one).
  [[nodiscard]] HRESULT RemovedReason() const { return device_ ? device_->GetDeviceRemovedReason() : S_OK; }
  [[nodiscard]] bool Lost() const { return FAILED(RemovedReason()); }
  [[nodiscard]] const std::string& Status() const { return status_; }

  ~RayReconstruction12() {
    if (own_fence_) WaitFor(own_value_);
    if (feature_ != nullptr) NVSDK_NGX_D3D12_ReleaseFeature(feature_);
    if (parameters_ != nullptr) NVSDK_NGX_D3D12_DestroyParameters(parameters_);
    if (initialized_) NVSDK_NGX_D3D12_Shutdown1(device_.Get());
    if (event_ != nullptr) CloseHandle(event_);
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

  // The request's commands into the slot's list (the feature made again when its size, mode or preset changes); false:
  // nothing recorded.
  bool Record(const Request& request, Slot* slot) {
    WaitFor(slot->value);
    ID3D12GraphicsCommandList* list = slot->list.Get();
    if (FAILED(slot->allocator->Reset()) || FAILED(list->Reset(slot->allocator.Get(), nullptr))) {
      return Fail("Ray Reconstruction: D3D12 stopped");
    }
    const bool upscaling = request.output_width > request.width;
    if (feature_ == nullptr || width_ != request.width || height_ != request.height || output_width_ != request.output_width
        || output_height_ != request.output_height || preset_ != request.preset || create_flags_ != request.create_flags) {
      if (feature_ != nullptr) {
        WaitFor(own_value_);
        NVSDK_NGX_D3D12_ReleaseFeature(feature_);
        feature_ = nullptr;
      }
      // DLAA at the output's size, else the quality whose input range holds this one (dynamic resolution within it).
      NVSDK_NGX_PerfQuality_Value quality = NVSDK_NGX_PerfQuality_Value_DLAA;
      if (upscaling) {
        quality = NVSDK_NGX_PerfQuality_Value_UltraPerformance;
        for (const auto candidate : {NVSDK_NGX_PerfQuality_Value_MaxQuality, NVSDK_NGX_PerfQuality_Value_Balanced,
                                     NVSDK_NGX_PerfQuality_Value_MaxPerf, NVSDK_NGX_PerfQuality_Value_UltraPerformance}) {
          unsigned int optimal_width = 0, optimal_height = 0, max_width = 0, max_height = 0, min_width = 0, min_height = 0;
          float sharpness = 0.f;
          if (NVSDK_NGX_SUCCEED(NGX_DLSSD_GET_OPTIMAL_SETTINGS(parameters_, request.output_width, request.output_height, candidate,
                                                              &optimal_width, &optimal_height, &max_width, &max_height, &min_width,
                                                              &min_height, &sharpness))
              && request.width >= min_width && request.width <= max_width && request.height >= min_height && request.height <= max_height) {
            quality = candidate;
            break;
          }
        }
      }
      for (const char* preset_name : {NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA,
                                      NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality,
                                      NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced,
                                      NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance,
                                      NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance,
                                      NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraQuality}) {
        NVSDK_NGX_Parameter_SetUI(parameters_, preset_name, request.preset);
      }
      // ReleaseFeature then really frees (else NGX caches its memory until Shutdown).
      NVSDK_NGX_Parameter_SetI(parameters_, NVSDK_NGX_Parameter_FreeMemOnReleaseFeature, 1);
      NVSDK_NGX_DLSSD_Create_Params create = {
          .InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified,
          .InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Unpacked,
          .InUseHWDepth = NVSDK_NGX_DLSS_Depth_Type_HW,
          .InWidth = upscaling ? request.width : request.output_width,
          .InHeight = upscaling ? request.height : request.output_height,
          .InTargetWidth = request.output_width,
          .InTargetHeight = request.output_height,
          .InPerfQualityValue = quality,
          .InFeatureCreateFlags = request.create_flags,
          .InEnableOutputSubrects = false,
      };
      if (NVSDK_NGX_FAILED(NGX_D3D12_CREATE_DLSSD_EXT(list, 1, 1, &feature_, parameters_, &create)) || feature_ == nullptr) {
        list->Close();
        feature_ = nullptr;
        status_ = "Ray Reconstruction could not be created";
        return false;
      }
      width_ = request.width;
      height_ = request.height;
      output_width_ = request.output_width;
      output_height_ = request.output_height;
      preset_ = request.preset;
      create_flags_ = request.create_flags;
      first_frame_ = true;
    }
    // The inputs, then the specular hit distance and the optional inputs when written this frame.
    ID3D12Resource* hit_distance = request.hit_distance ? hit_distance_.Get() : nullptr;
    std::array<ID3D12Resource*, EXTRA_INPUTS> extra = {};
    for (size_t index = 0; index < EXTRA_INPUTS; ++index) {
      if ((request.extra_inputs & (1u << index)) != 0u) extra[index] = extra_[index].Get();
    }
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    for (size_t index = 0; index <= size_t{INPUT_COUNT} + EXTRA_INPUTS; ++index) {
      ID3D12Resource* resource = hit_distance;
      if (index < INPUT_COUNT) {
        resource = inputs_[index].Get();
      } else if (index > INPUT_COUNT) {
        resource = extra[index - INPUT_COUNT - 1];
      }
      if (resource == nullptr) continue;
      barriers.push_back({
          .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
          .Transition = {
              .pResource = resource,
              .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
              .StateBefore = D3D12_RESOURCE_STATE_COMMON,
              .StateAfter = (index == OUTPUT ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
          },
      });
    }
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    // NVIDIA takes the matrices as writable pointers.
    auto world_to_view = request.world_to_view;
    auto view_to_clip = request.view_to_clip;
    NVSDK_NGX_D3D12_DLSSD_Eval_Params parameters = {
        .pInDiffuseAlbedo = inputs_[DIFFUSE_ALBEDO].Get(),
        .pInSpecularAlbedo = inputs_[SPECULAR_ALBEDO].Get(),
        .pInNormals = inputs_[NORMALS].Get(),
        .pInRoughness = inputs_[ROUGHNESS].Get(),
        .pInColor = inputs_[COLOR].Get(),
        .pInOutput = inputs_[OUTPUT].Get(),
        .pInDepth = inputs_[DEPTH].Get(),
        .pInMotionVectors = inputs_[MOTION].Get(),
        .InJitterOffsetX = request.jitter_x,
        .InJitterOffsetY = request.jitter_y,
        .InRenderSubrectDimensions = {.Width = request.width, .Height = request.height},
        .InReset = (request.reset || first_frame_) ? 1 : 0,
        .InMVScaleX = 1.f,
        .InMVScaleY = 1.f,
        .pInColorBeforeTransparency = extra[COLOR_BEFORE],
        .pInDepthOfFieldGuide = extra[DOF_GUIDE],
        .pInSpecularHitDistance = hit_distance,
        .pInWorldToViewMatrix = world_to_view.data(),
        .pInViewToClipMatrix = view_to_clip.data(),
        .InPreExposure = request.pre_exposure,
        .InExposureScale = 1.f,
        .pInTransparencyLayer = extra[PARTICLE_LAYER],
    };
    evaluated_ = NVSDK_NGX_SUCCEED(NGX_D3D12_EVALUATE_DLSSD_EXT(list, feature_, parameters_, &parameters));
    for (auto& barrier : barriers) std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    if (FAILED(list->Close())) return Fail("Ray Reconstruction: D3D12 stopped");
    first_frame_ = false;
    status_ = evaluated_ ? "DLSS Ray Reconstruction active (D3D12)" : "Ray Reconstruction evaluation failed";
    return true;
  }

  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  std::array<Slot, SLOTS> slots_;
  size_t next_slot_ = 0;
  ComPtr<ID3D12Fence> own_fence_;  // this queue's progress (the slots' reuse)
  uint64_t own_value_ = 0;
  bool evaluated_ = false;
  HANDLE event_ = nullptr;
  std::array<ComPtr<ID3D12Resource>, INPUT_COUNT> inputs_;
  ComPtr<ID3D12Resource> hit_distance_;  // the specular hit distance (none: not shared)
  std::array<ComPtr<ID3D12Resource>, EXTRA_INPUTS> extra_;  // the optional inputs (none: not shared)
  ComPtr<ID3D12Fence> fence_;  // the addon's
  NVSDK_NGX_Parameter* parameters_ = nullptr;
  NVSDK_NGX_Handle* feature_ = nullptr;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t output_width_ = 0;
  uint32_t output_height_ = 0;
  uint32_t preset_ = 0;
  int create_flags_ = 0;
  bool initialized_ = false;
  bool failed_ = false;
  bool first_frame_ = true;
  std::string status_ = "Ray Reconstruction off";
};

}  // namespace twattila::dlaa_helper
