/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "../twattila/dlaa_helper/rr_d3d12.hpp"
#include "./d3d12_device.hpp"
#include "./ngx_backend.hpp"

// DLSS Ray Reconstruction through D3D12 in the game's process: NVIDIA's D3D11 one gives black frames or crashes when
// the GPU is waited for right after it (the ray tracing's fences do). Its inputs are copied into shared textures, a D3D12
// queue of this add-on's own runs NVIDIA's D3D12 Ray Reconstruction (the DLAA helper's, twattila/dlaa_helper/rr_d3d12.hpp)
// between two values of a shared fence, and its output is copied back. Used as the backend: by the DLSS pass only.
namespace talesofarise::rr_d3d12 {

using Microsoft::WRL::ComPtr;
using twattila::dlaa_helper::Input;

// A typeless format's typed one (NVIDIA's D3D12 views need it); others as they are.
inline DXGI_FORMAT Typed(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32_TYPELESS:          return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16G16_TYPELESS:       return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS:          return DXGI_FORMAT_R16_FLOAT;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:     return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:  return DXGI_FORMAT_R10G10B10A2_UNORM;
    default:                                return format;
  }
}

class Reconstruction {
 public:
  Reconstruction() = default;
  Reconstruction(const Reconstruction&) = delete;
  Reconstruction& operator=(const Reconstruction&) = delete;
  Reconstruction(Reconstruction&&) = default;
  Reconstruction& operator=(Reconstruction&&) = default;

  // Its D3D12 work waited for (RayReconstruction12's destructor); at process exit NVIDIA's runtime is gone
  // (dlss::ShutdownInProgress): everything left to Windows.
  ~Reconstruction() {
    if (dlss::ShutdownInProgress()) static_cast<void>(gpu_.release());
  }

  // NVIDIA's D3D12 Ray Reconstruction from `evaluation`'s textures and `guides` into its output; `folder`:
  // nvngx_dlssd.dll's. False: it did not run (the caller then runs Super Resolution).
  bool Evaluate(ID3D11DeviceContext* context, const dlss::Evaluation& evaluation, const dlss::RayReconstruction& guides,
                const std::wstring& folder) {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Device5> device5;
    ComPtr<ID3D11DeviceContext4> context4;
    context->GetDevice(device.GetAddressOf());
    if (failed_ || FAILED(device.As(&device5)) || FAILED(context->QueryInterface(IID_PPV_ARGS(context4.GetAddressOf())))) {
      return false;
    }
    if (!gpu_) {
      gpu_ = std::make_unique<Gpu>();
      gpu_->device = device5;
      if (const auto error = d3d12_device::Create(device.Get(), &gpu_->device12); !error.empty()) return Fail(error);
      gpu_->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      if (FAILED(device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(gpu_->fence.GetAddressOf())))) {
        return Fail("no shared fence");
      }
      if (!gpu_->reconstruction.Initialize(gpu_->device12.native.Get(), folder)) return Fail(gpu_->reconstruction.Status());
    }
    auto& gpu = *gpu_;
    if (gpu.device != device5) return false;

    // The shared copies at each input's size and format, made (and opened in D3D12) again when one changes.
    const std::array<ID3D11Resource*, Input::INPUT_COUNT> sources = {
        evaluation.color, evaluation.depth, evaluation.motion_vectors, evaluation.output,
        guides.diffuse_albedo, guides.specular_albedo, guides.normals, guides.roughness};
    bool changed = false;
    for (size_t index = 0; index < sources.size(); ++index) {
      ComPtr<ID3D11Texture2D> texture;
      D3D11_TEXTURE2D_DESC desc = {};
      if (sources[index] == nullptr || FAILED(sources[index]->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) return false;
      texture->GetDesc(&desc);
      const auto& kept = gpu.sources[index];
      if (gpu.copies[index] && kept.Width == desc.Width && kept.Height == desc.Height && kept.Format == desc.Format) continue;
      // The queue may still read the old ones.
      if (!changed) WaitFor(gpu.value);
      changed = true;
      const D3D11_TEXTURE2D_DESC shared_desc = {
          .Width = desc.Width,
          .Height = desc.Height,
          .MipLevels = 1,
          .ArraySize = 1,
          .Format = Typed(desc.Format),
          .SampleDesc = {.Count = 1},
          .Usage = D3D11_USAGE_DEFAULT,
          .BindFlags = D3D11_BIND_SHADER_RESOURCE | (index == Input::OUTPUT ? D3D11_BIND_UNORDERED_ACCESS : 0u),
          .MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED,
      };
      if (FAILED(device5->CreateTexture2D(&shared_desc, nullptr, gpu.copies[index].ReleaseAndGetAddressOf()))) {
        return Fail("its shared textures could not be made");
      }
      gpu.sources[index] = desc;
    }
    if (changed) {
      std::array<uint64_t, Input::INPUT_COUNT> handles = {};
      HANDLE fence_handle = nullptr;
      bool shared = SUCCEEDED(gpu.fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &fence_handle));
      for (size_t index = 0; shared && index < handles.size(); ++index) {
        ComPtr<IDXGIResource1> resource;
        HANDLE handle = nullptr;
        shared = SUCCEEDED(gpu.copies[index].As(&resource))
                 && SUCCEEDED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                           nullptr, &handle));
        handles[index] = reinterpret_cast<uint64_t>(handle);
      }
      const bool opened = shared && gpu.reconstruction.Open(handles, reinterpret_cast<uint64_t>(fence_handle));
      for (const uint64_t handle : handles) {
        if (handle != 0) CloseHandle(reinterpret_cast<HANDLE>(handle));
      }
      if (fence_handle != nullptr) CloseHandle(fence_handle);
      if (!opened) return Fail("its shared textures could not be opened in D3D12");
    }

    for (size_t index = 0; index < sources.size(); ++index) {
      if (index != Input::OUTPUT) context->CopySubresourceRegion(gpu.copies[index].Get(), 0, 0, 0, 0, sources[index], 0, nullptr);
    }
    context4->Signal(gpu.fence.Get(), ++gpu.value);
    context->Flush();
    // The queue signals the next value whether or not it ran (the game never waits for nothing).
    const bool evaluated = gpu.reconstruction.Evaluate({
        .value = gpu.value,
        .width = evaluation.width,
        .height = evaluation.height,
        .output_width = evaluation.output_width,
        .output_height = evaluation.output_height,
        .jitter_x = evaluation.jitter_x,
        .jitter_y = evaluation.jitter_y,
        .reset = evaluation.reset,
        .preset = guides.preset,
        .world_to_view = guides.world_to_view,
        .view_to_clip = guides.view_to_clip,
        .create_flags = evaluation.create_flags,
        .pre_exposure = evaluation.pre_exposure,
    });
    context4->Wait(gpu.fence.Get(), ++gpu.value);
    status_ = gpu.reconstruction.Status();
    if (!evaluated) return false;
    context->CopySubresourceRegion(sources[Input::OUTPUT], 0, 0, 0, 0, gpu.copies[Input::OUTPUT].Get(), 0, nullptr);
    return true;
  }

  // Ray Reconstruction off: NVIDIA's feature freed (the next evaluation makes it again).
  void ReleaseFeature() {
    if (gpu_) gpu_->reconstruction.ReleaseFeature();
  }

  [[nodiscard]] const std::string& Status() const { return status_; }

 private:
  // Made on the first evaluation, on the game's device.
  struct Gpu {
    d3d12_device::Device device12;
    ComPtr<ID3D11Device5> device;
    ComPtr<ID3D11Fence> fence;
    HANDLE event = nullptr;
    uint64_t value = 0;
    // The inputs' shared copies (Input order) and what each copies (its size and format).
    std::array<ComPtr<ID3D11Texture2D>, Input::INPUT_COUNT> copies;
    std::array<D3D11_TEXTURE2D_DESC, Input::INPUT_COUNT> sources = {};
    twattila::dlaa_helper::RayReconstruction12 reconstruction;

    Gpu() = default;
    Gpu(const Gpu&) = delete;
    Gpu& operator=(const Gpu&) = delete;
    ~Gpu() {
      if (event != nullptr) CloseHandle(event);
    }
  };

  bool Fail(const std::string& status) {
    failed_ = true;
    status_ = "Ray Reconstruction: " + status;
    reshade::log::message(reshade::log::level::warning, ("[Tales DLSS] " + status_).c_str());
    return false;
  }

  // The CPU waits until the D3D12 queue passed the fence's `value`.
  void WaitFor(uint64_t value) {
    if (gpu_->fence->GetCompletedValue() >= value) return;
    gpu_->fence->SetEventOnCompletion(value, gpu_->event);
    WaitForSingleObject(gpu_->event, 5000);
  }

  std::unique_ptr<Gpu> gpu_;
  bool failed_ = false;
  std::string status_ = "Ray Reconstruction off";
};

}  // namespace talesofarise::rr_d3d12
