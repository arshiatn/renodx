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

// Requests in flight: each with its own command allocator and list.
constexpr size_t SLOTS = 3;

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
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.GetAddressOf())))) {
      return Fail("Ray Reconstruction: no D3D12 device on the game's GPU");
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
  bool Open(const std::array<uint64_t, INPUT_COUNT>& textures, uint64_t fence, uint64_t hit_distance = 0) {
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
    return true;
  }

  void Close() {
    inputs_ = {};
    hit_distance_.Reset();
    fence_.Reset();
  }

  // One request: the queue waits for the addon's fence value, Ray Reconstruction runs, the next value is signalled
  // (also when it could not run, so the game never waits for nothing). False: it did not run.
  bool Evaluate(const Request& request) {
    if (!fence_ || !queue_) return false;
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
  [[nodiscard]] const std::string& Status() const { return status_; }

  ~RayReconstruction12() {
    if (own_fence_) WaitFor(own_value_);
    if (feature_ != nullptr) NVSDK_NGX_D3D12_ReleaseFeature(feature_);
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
    // The inputs, then the specular hit distance when written this frame.
    ID3D12Resource* hit_distance = request.hit_distance ? hit_distance_.Get() : nullptr;
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    for (size_t index = 0; index <= INPUT_COUNT; ++index) {
      ID3D12Resource* resource = (index < INPUT_COUNT) ? inputs_[index].Get() : hit_distance;
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
        .pInSpecularHitDistance = hit_distance,
        .pInWorldToViewMatrix = world_to_view.data(),
        .pInViewToClipMatrix = view_to_clip.data(),
        .InPreExposure = request.pre_exposure,
        .InExposureScale = 1.f,
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
