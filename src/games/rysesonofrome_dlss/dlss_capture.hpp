/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include "../../utils/log.hpp"

// Debug (Advanced button): DLSS's inputs of the next CAPTURE_FRAMES evaluations, raw (color, motion vectors in pixels,
// depth; their active rectangle), at DLAA too. For what flickers while paused (ATN): a still image's pixels should all
// have 0 motion; the ones that do not show which surface DLSS drops every frame. As twshogun2_dlss's capture (its trees
// and banners), the same files: ryse_dlss_capture_<frame>_<color / motion / depth>.bin (header: width, height, DXGI
// format, bytes per texel, uint32 each; then the rows) and ryse_dlss_capture.txt (each frame's parameters), next to the
// exe; tools\shogun2\capture_decode.py reads them.
namespace rysesonofrome::dlss_capture {

namespace internal {

constexpr int CAPTURE_FRAMES = 4;

struct State {
  std::mutex mutex;
  std::atomic_int frames_left = 0;  // evaluations still to capture
  std::string status;
};
static State state;

}  // namespace internal

inline void Request() {
  internal::state.frames_left.store(internal::CAPTURE_FRAMES);
  const std::lock_guard lock(internal::state.mutex);
  internal::state.status = "Waiting for the next DLAA / DLSS frames";
}

inline std::string Status() {
  const std::lock_guard lock(internal::state.mutex);
  return internal::state.status;
}

// Render thread, after an evaluation (the inputs as NVIDIA got them).
inline void OnEvaluate(ID3D11DeviceContext* context, ID3D11Resource* color, ID3D11Resource* motion,
                       ID3D11Resource* depth, uint32_t width, uint32_t height, float jitter_x, float jitter_y,
                       bool reset) {
  auto& state = internal::state;
  const int frames_left = state.frames_left.load();
  if (frames_left <= 0 || context == nullptr) return;
  state.frames_left.store(frames_left - 1);
  const int frame = internal::CAPTURE_FRAMES - frames_left;
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  const auto folder = std::filesystem::path(exe).parent_path();

  // One input's active rectangle, raw (a staging copy of the whole texture).
  auto save = [&](ID3D11Resource* resource, std::string_view name) -> std::string {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (resource == nullptr || FAILED(resource->QueryInterface(IID_PPV_ARGS(texture.GetAddressOf())))) {
      return std::format("{} -", name);
    }
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    uint32_t texel_size = 0;
    switch (desc.Format) {
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
        texel_size = 8;
        break;
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R32_FLOAT:
        texel_size = 4;
        break;
      default:
        return std::format("{} f{} not saved", name, static_cast<int>(desc.Format));
    }
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf()))) {
      return std::format("{} staging failed", name);
    }
    context->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return std::format("{} map failed", name);
    const std::array<uint32_t, 4> header = {std::min(width, desc.Width), std::min(height, desc.Height),
                                            static_cast<uint32_t>(desc.Format), texel_size};
    std::ofstream file(folder / std::format("ryse_dlss_capture_{}_{}.bin", frame, name), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
    for (uint32_t row = 0; row < header[1]; ++row) {
      file.write(static_cast<const char*>(mapped.pData) + (static_cast<size_t>(row) * mapped.RowPitch),
                 static_cast<std::streamsize>(header[0]) * texel_size);
    }
    context->Unmap(staging.Get(), 0);
    return std::format("{} {}x{} f{}", name, header[0], header[1], static_cast<int>(desc.Format));
  };
  const std::string color_text = save(color, "color");
  const std::string motion_text = save(motion, "motion");
  const std::string depth_text = save(depth, "depth");
  const auto mode = (frame == 0 ? std::ios::trunc : std::ios::app);
  std::ofstream(folder / L"ryse_dlss_capture.txt", std::ios::binary | mode) << std::format(
      "frame {}: input {}x{} jitter ({}, {}) MV scale (1, 1) reset {} | {} | {} | {}\n", frame, width, height, jitter_x,
      jitter_y, reset ? 1 : 0, color_text, motion_text, depth_text);
  if (frames_left == 1) {
    const std::lock_guard lock(state.mutex);
    state.status = std::format("DLSS inputs: {} frames saved to {}", internal::CAPTURE_FRAMES,
                               (folder / L"ryse_dlss_capture_*").string());
    renodx::utils::log::i("[Ryse DLSS] ", state.status);
  }
}

}  // namespace rysesonofrome::dlss_capture
