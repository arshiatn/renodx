/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 *
 * From an x64 Visual Studio developer CMD prompt at the repository root:
 * (Adapt the absolute runtime path if the repository is located elsewhere.)
 *
 * clang-cl /nologo /std:c++20 /MTd /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=0 /DNOMINMAX ^
 *   /DRENODX_NGX_RUNTIME_PATH=L\"D:/Dev/renodx/external/DLSS/lib/Windows_x86_64/rel\" ^
 *   /I external/DLSS/include /I external/reshade ^
 *   src/games/expeditionsrome_dlss/tests/ngx_backend_smoke.cpp ^
 *   /Fo"%TEMP%/rome_ngx_backend_smoke.obj" /Fe"%TEMP%/rome_ngx_backend_smoke.exe" ^
 *   /link external/DLSS/lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg_iterator0.lib ^
 *   d3d11.lib dxgi.lib advapi32.lib user32.lib
 * "%TEMP%/rome_ngx_backend_smoke.exe"
 *
 * The test does not launch the game, install runtime DLLs, or change NVIDIA settings.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include "../ngx_backend.hpp"

extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  std::cout << message << '\n';
}

struct SyntheticFrame {
  static constexpr std::array<uint16_t, 4> COLOR_VALUE = {0x3400, 0x3800, 0x3A00, 0x3C00};
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t output_width = 0;
  uint32_t output_height = 0;
  expeditionsrome::dlss::Mode mode = expeditionsrome::dlss::Mode::DLAA;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> color;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> motion;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> output;

  bool Create(ID3D11Device* device, uint32_t new_width, uint32_t new_height,
              uint32_t new_output_width = 0, uint32_t new_output_height = 0,
              expeditionsrome::dlss::Mode new_mode = expeditionsrome::dlss::Mode::DLAA,
              uint32_t allocated_width = 0, uint32_t allocated_height = 0) {
    width = new_width;
    height = new_height;
    output_width = (new_output_width == 0 ? width : new_output_width);
    output_height = (new_output_height == 0 ? height : new_output_height);
    mode = new_mode;
    allocated_width = (allocated_width == 0 ? width : allocated_width);
    allocated_height = (allocated_height == 0 ? height : allocated_height);
    if (allocated_width < width || allocated_height < height) return false;
    // Poison the allocation padding so a renderer accidentally using allocated dimensions fails the output check.
    std::vector<std::array<uint16_t, 4>> color_data(allocated_width * allocated_height, {0x4800, 0x4800, 0x4800, 0x3C00});
    for (uint32_t row = 0; row < height; ++row) {
      std::fill_n(color_data.begin() + row * allocated_width, width, COLOR_VALUE);
    }
    std::vector<float> depth_data(width * height, 0.5f);
    std::vector<std::array<uint16_t, 2>> motion_data(width * height, {0, 0});
    const D3D11_TEXTURE2D_DESC color_desc = {
        .Width = allocated_width,
        .Height = allocated_height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
    };
    const D3D11_SUBRESOURCE_DATA color_initial = {.pSysMem = color_data.data(), .SysMemPitch = allocated_width * 8};
    if (FAILED(device->CreateTexture2D(&color_desc, &color_initial, color.GetAddressOf()))) return false;
    auto depth_desc = color_desc;
    depth_desc.Width = width;
    depth_desc.Height = height;
    depth_desc.Format = DXGI_FORMAT_R32_FLOAT;
    const D3D11_SUBRESOURCE_DATA depth_initial = {.pSysMem = depth_data.data(), .SysMemPitch = width * 4};
    if (FAILED(device->CreateTexture2D(&depth_desc, &depth_initial, depth.GetAddressOf()))) return false;
    auto motion_desc = depth_desc;
    motion_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
    const D3D11_SUBRESOURCE_DATA motion_initial = {.pSysMem = motion_data.data(), .SysMemPitch = width * 4};
    if (FAILED(device->CreateTexture2D(&motion_desc, &motion_initial, motion.GetAddressOf()))) return false;
    auto output_desc = color_desc;
    output_desc.Width = output_width;
    output_desc.Height = output_height;
    output_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    return SUCCEEDED(device->CreateTexture2D(&output_desc, nullptr, output.GetAddressOf()));
  }

  expeditionsrome::dlss::Evaluation Inputs(bool reset) const {
    return {
        .color = color.Get(),
        .depth = depth.Get(),
        .motion_vectors = motion.Get(),
        .output = output.Get(),
        .width = width,
        .height = height,
        .reset = reset,
        .output_width = output_width,
        .output_height = output_height,
        .mode = mode,
    };
  }

  bool Validate(ID3D11Device* device, ID3D11DeviceContext* context, bool indicator_enabled) const {
    D3D11_TEXTURE2D_DESC staging_desc = {};
    output->GetDesc(&staging_desc);
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf()))) return false;
    context->ClearState();
    context->CopyResource(staging.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    uint32_t invalid_channels = 0;
    uint32_t black_pixels = 0;
    uint32_t outside_black_pixels = 0;
    uint32_t mismatched_channels = 0;
    uint32_t outside_mismatched_channels = 0;
    uint32_t min_black_x = output_width;
    uint32_t min_black_y = output_height;
    uint32_t max_black_x = 0;
    uint32_t max_black_y = 0;
    uint16_t max_scene_distance = 0;
    for (uint32_t row = 0; row < output_height; ++row) {
      const auto* pixels = reinterpret_cast<const uint16_t*>(
          static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch);
      for (uint32_t column = 0; column < output_width; ++column) {
        // Conservative bottom-left region for the local 310.7 release runtime indicator.
        // Never exclude this region unless the user's existing registry setting enables it.
        const bool inside_indicator = column < 1024 && row >= output_height - 128;
        const bool black = (pixels[column * 4] & 0x7FFF) == 0
                           && (pixels[column * 4 + 1] & 0x7FFF) == 0
                           && (pixels[column * 4 + 2] & 0x7FFF) == 0;
        if (black) {
          ++black_pixels;
          outside_black_pixels += !inside_indicator;
          min_black_x = std::min(min_black_x, column);
          min_black_y = std::min(min_black_y, row);
          max_black_x = std::max(max_black_x, column);
          max_black_y = std::max(max_black_y, row);
        }
        for (uint32_t channel = 0; channel < 3; ++channel) {
          const uint16_t value = pixels[column * 4 + channel];
          invalid_channels += (value & 0x7C00) == 0x7C00;
          const uint16_t expected = COLOR_VALUE[channel];
          const uint16_t distance = value > expected ? value - expected : expected - value;
          mismatched_channels += distance > 32;
          if (!inside_indicator || !indicator_enabled) {
            outside_mismatched_channels += distance > 32;
            max_scene_distance = std::max(max_scene_distance, distance);
          }
        }
      }
    }
    context->Unmap(staging.Get(), 0);
    const bool exclude_indicator = indicator_enabled && black_pixels != 0 && outside_black_pixels == 0;
    std::cout << "AA mode " << static_cast<unsigned int>(mode) << ' '
              << width << 'x' << height << " -> " << output_width << 'x' << output_height
              << ": nonfinite RGB channels=" << invalid_channels
              << " black pixels=" << black_pixels << " black bounds=(" << min_black_x << ',' << min_black_y
              << ")-(" << max_black_x << ',' << max_black_y << ") indicator region excluded=" << exclude_indicator
              << " scene channels beyond 32 half-ULPs=" << outside_mismatched_channels
              << " max scene half-ULPs=" << max_scene_distance << '\n';
    return invalid_channels == 0 && (exclude_indicator ? outside_mismatched_channels == 0 : mismatched_channels == 0);
  }
};

int main() {
  reshade::internal::get_reshade_module_handle(GetModuleHandleW(nullptr));
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) return 1;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0;; ++index) {
    const auto result = factory->EnumAdapters1(index, adapter.ReleaseAndGetAddressOf());
    if (result == DXGI_ERROR_NOT_FOUND) {
      std::cout << "No NVIDIA hardware adapter found; no software DLAA substitute is used.\n";
      return 1;
    }
    if (FAILED(result)) return 1;
    DXGI_ADAPTER_DESC1 desc = {};
    if (FAILED(adapter->GetDesc1(&desc))) return 1;
    if (desc.VendorId == 0x10DE && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) break;
  }
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1, D3D11_SDK_VERSION,
                               device.GetAddressOf(), nullptr, context.GetAddressOf()))) return 1;
  DWORD indicator_value = 0;
  DWORD indicator_size = sizeof(indicator_value);
  const bool indicator_enabled = RegGetValueW(
                                     HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore",
                                     L"ShowDlssIndicator", RRF_RT_REG_DWORD, nullptr,
                                     &indicator_value, &indicator_size)
                                     == ERROR_SUCCESS
                                 && indicator_value == 1024;
  std::cout << "Read-only NVIDIA release indicator check: enabled=" << indicator_enabled << '\n';
  expeditionsrome::dlss::Backend backend;
  struct Scenario {
    uint32_t width;
    uint32_t height;
    uint32_t output_width = 0;
    uint32_t output_height = 0;
    expeditionsrome::dlss::Mode mode = expeditionsrome::dlss::Mode::DLAA;
    uint32_t allocated_width = 0;
    uint32_t allocated_height = 0;
    bool query_supported_range = false;
  };
  struct RuntimeBounds {
    NVSDK_NGX_PerfQuality_Value quality;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int max_width = 0;
    unsigned int max_height = 0;
    unsigned int min_width = 0;
    unsigned int min_height = 0;
  };
  std::array<RuntimeBounds, 4> runtime_bounds = {{
      {.quality = NVSDK_NGX_PerfQuality_Value_MaxQuality},
      {.quality = NVSDK_NGX_PerfQuality_Value_Balanced},
      {.quality = NVSDK_NGX_PerfQuality_Value_MaxPerf},
      {.quality = NVSDK_NGX_PerfQuality_Value_UltraPerformance},
  }};
  uint32_t scenario = 0;
  for (const auto& dimensions : std::array<Scenario, 38>{
           Scenario{.width = 1280, .height = 720},
           Scenario{.width = 1024, .height = 576},
           Scenario{.width = 1024, .height = 576},
           Scenario{.width = 2573, .height = 1448, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_QUALITY, .allocated_width = 2576},
           Scenario{.width = 2573, .height = 1448, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_QUALITY, .allocated_width = 2576},
           Scenario{.width = 1920, .height = 1080, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_QUALITY},
           Scenario{.width = 960, .height = 540, .output_width = 1920, .output_height = 1080, .mode = expeditionsrome::dlss::Mode::DLSS_QUALITY},
           Scenario{.width = 1920, .height = 1080},
           Scenario{.width = 2227, .height = 1253, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_BALANCED, .allocated_width = 2232},
           Scenario{.width = 2228, .height = 1253, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_BALANCED, .allocated_width = 2232},
           Scenario{.width = 1920, .height = 1080, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_PERFORMANCE},
           Scenario{.width = 960, .height = 540, .output_width = 1920, .output_height = 1080, .mode = expeditionsrome::dlss::Mode::DLSS_PERFORMANCE},
           Scenario{.width = 1920, .height = 1080, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM},
           Scenario{.width = 2036, .height = 1145, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM, .allocated_width = 2036, .allocated_height = 1148},
           Scenario{.width = 2074, .height = 1167, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM, .allocated_width = 2076, .allocated_height = 1168},
           Scenario{.width = 2151, .height = 1210, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM, .allocated_width = 2152, .allocated_height = 1212},
           Scenario{.width = 2650, .height = 1491, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM, .allocated_width = 2652, .allocated_height = 1492},
           Scenario{.width = 2228, .height = 1253, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM, .allocated_width = 2232},
           Scenario{.width = 2573, .height = 1448, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM, .allocated_width = 2576},
           Scenario{.width = 2880, .height = 1620, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM},
           Scenario{.width = 3264, .height = 1836, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM},
           Scenario{.width = 3802, .height = 2139, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM, .allocated_width = 3808},
           Scenario{.width = 3840, .height = 2160, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM},
           Scenario{.width = 1920, .height = 1080, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM},
           Scenario{.width = 1920, .height = 1080, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_PERFORMANCE},
           Scenario{.width = 1280, .height = 720, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_ULTRA_PERFORMANCE},
           Scenario{.width = 1268, .height = 713, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_ULTRA_PERFORMANCE, .allocated_width = 1268, .allocated_height = 716, .query_supported_range = true},
           Scenario{.width = 1268, .height = 713, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM, .allocated_width = 1268, .allocated_height = 716, .query_supported_range = true},
           Scenario{.width = 1280, .height = 720, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM},
           Scenario{.width = 1536, .height = 864, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM},
           Scenario{.width = 1882, .height = 1059, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM, .allocated_width = 1884, .allocated_height = 1060},
           Scenario{.width = 3840, .height = 2160, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM},
           Scenario{.width = 1280, .height = 720, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_ULTRA_PERFORMANCE},
           Scenario{.width = 1920, .height = 1080, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_PERFORMANCE},
           Scenario{.width = 1280, .height = 720, .output_width = 3840, .output_height = 2160, .mode = expeditionsrome::dlss::Mode::DLSS_ULTRA_PERFORMANCE},
           Scenario{.width = 640, .height = 360, .output_width = 1920, .output_height = 1080, .mode = expeditionsrome::dlss::Mode::DLSS_ULTRA_PERFORMANCE},
           Scenario{.width = 640, .height = 360, .output_width = 1920, .output_height = 1080, .mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM},
           Scenario{.width = 1280, .height = 720}}) {
    SyntheticFrame frame;
    if (!frame.Create(device.Get(), dimensions.width, dimensions.height,
                      dimensions.output_width, dimensions.output_height, dimensions.mode,
                      dimensions.allocated_width, dimensions.allocated_height)) return 1;
    auto evaluation = frame.Inputs(scenario == 0);
    if (backend.Evaluate(nullptr, evaluation)) return 4;
    auto rejected = evaluation;
    rejected.output = rejected.color;
    if (backend.Evaluate(context.Get(), rejected)) return 4;
    rejected = evaluation;
    rejected.width = 0;
    if (backend.Evaluate(context.Get(), rejected)) return 4;
    rejected = evaluation;
    rejected.output_height = 0;
    if (backend.Evaluate(context.Get(), rejected)) return 4;
    rejected = evaluation;
    rejected.mode = static_cast<expeditionsrome::dlss::Mode>(255);
    if (backend.Evaluate(context.Get(), rejected)) return 4;
    rejected = evaluation;
    rejected.mode = expeditionsrome::dlss::Mode::DLSS_CUSTOM;
    rejected.width = rejected.output_width * 32 / 100;
    rejected.height = rejected.output_height * 32 / 100;
    if (backend.Evaluate(context.Get(), rejected) || backend.Failed()) return 4;
    if (scenario == 3) {
      rejected.mode = expeditionsrome::dlss::Mode::DLSS_QUALITY;
      if (backend.Evaluate(context.Get(), rejected) || backend.Failed()) return 4;
    }
    rejected = evaluation;
    rejected.mode = expeditionsrome::dlss::Mode::DLSS_ULTRA_PERFORMANCE;
    rejected.width = rejected.output_width * 40 / 100;
    rejected.height = rejected.output_height * 40 / 100;
    if (backend.Evaluate(context.Get(), rejected) || backend.Failed()) return 4;
    rejected.width = rejected.output_width * 33 / 100;
    rejected.height = rejected.output_height * 35 / 100;
    if (backend.Evaluate(context.Get(), rejected) || backend.Failed()) return 4;
    rejected = evaluation;
    rejected.mode = (dimensions.width == dimensions.output_width && dimensions.height == dimensions.output_height
                             || dimensions.output_width == 0
                         ? expeditionsrome::dlss::Mode::DLSS_QUALITY
                         : expeditionsrome::dlss::Mode::DLAA);
    if (backend.Evaluate(context.Get(), rejected)) return 4;
    rejected = evaluation;
    ++rejected.output_width;
    if (backend.Evaluate(context.Get(), rejected)) return 4;
    rejected = evaluation;
    ++rejected.width;
    if (backend.Evaluate(context.Get(), rejected)) return 4;
    if (dimensions.mode == expeditionsrome::dlss::Mode::DLAA) {
      // Exercise unchanged callers that omit the newly appended output fields.
      evaluation.output_width = 0;
      evaluation.output_height = 0;
    }
    if (scenario == 2 || scenario == 4) backend.Reset();
    context->ClearState();
    const bool expected_supported = !dimensions.query_supported_range
                                    || std::any_of(runtime_bounds.begin(), runtime_bounds.end(), [&dimensions](const auto& bounds) {
                                         return (dimensions.mode == expeditionsrome::dlss::Mode::DLSS_CUSTOM
                                                 || bounds.quality == NVSDK_NGX_PerfQuality_Value_UltraPerformance)
                                                && dimensions.width >= bounds.min_width && dimensions.width <= bounds.max_width
                                                && dimensions.height >= bounds.min_height && dimensions.height <= bounds.max_height;
                                       });
    if (backend.Evaluate(context.Get(), evaluation) != expected_supported) {
      std::cout << backend.Status() << '\n';
      return 2;
    }
    if (backend.Failed()) return 4;
    if (!expected_supported) {
      std::cout << "Scenario " << scenario << " safely rejected " << dimensions.width << 'x' << dimensions.height
                << " outside queried NVIDIA limits: " << backend.Status() << '\n';
      ++scenario;
      continue;
    }
    if (scenario == 0) {
      NVSDK_NGX_Parameter* query_parameters = nullptr;
      if (NVSDK_NGX_FAILED(NVSDK_NGX_D3D11_GetCapabilityParameters(&query_parameters)) || query_parameters == nullptr) return 6;
      bool valid_ranges = true;
      for (auto& bounds : runtime_bounds) {
        float sharpness = 0.f;
        if (NVSDK_NGX_FAILED(NGX_DLSS_GET_OPTIMAL_SETTINGS(
                query_parameters, 3840, 2160, bounds.quality, &bounds.width, &bounds.height,
                &bounds.max_width, &bounds.max_height, &bounds.min_width, &bounds.min_height, &sharpness))) {
          valid_ranges = false;
          break;
        }
        std::cout << "Queried SDK mode " << bounds.quality << " optimal " << bounds.width << 'x' << bounds.height
                  << " supported " << bounds.min_width << 'x' << bounds.min_height << ".."
                  << bounds.max_width << 'x' << bounds.max_height << '\n';
        valid_ranges &= bounds.min_width != 0 && bounds.min_height != 0
                        && bounds.min_width <= bounds.width && bounds.min_height <= bounds.height
                        && bounds.width <= bounds.max_width && bounds.height <= bounds.max_height;
      }
      if (NVSDK_NGX_FAILED(NVSDK_NGX_D3D11_DestroyParameters(query_parameters)) || !valid_ranges) return 6;
    }
    evaluation.reset = false;
    if (scenario == 0 || scenario == 3 || scenario == 31) {
      expeditionsrome::dlss::Backend moved(std::move(backend));
      context->ClearState();
      if (!moved.Evaluate(context.Get(), evaluation)) return 2;
      backend = std::move(moved);
      context->ClearState();
      if (!backend.Evaluate(context.Get(), evaluation)) return 2;
    }
    for (uint32_t frame_number = 0; frame_number < 8; ++frame_number) {
      context->ClearState();
      if (!backend.Evaluate(context.Get(), evaluation)) return 2;
    }
    if (!frame.Validate(device.Get(), context.Get(), indicator_enabled)) return 3;
    std::cout << "Scenario " << scenario << " status: " << backend.Status() << '\n';
    ++scenario;
  }
  Microsoft::WRL::ComPtr<ID3D11Device> unsupported_device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> unsupported_context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                               &level, 1, D3D11_SDK_VERSION, unsupported_device.GetAddressOf(),
                               nullptr, unsupported_context.GetAddressOf()))) return 1;
  SyntheticFrame unsupported_frame;
  if (!unsupported_frame.Create(unsupported_device.Get(), 1280, 720)) return 1;
  if (backend.Evaluate(context.Get(), unsupported_frame.Inputs(true))) return 5;
  if (backend.Evaluate(unsupported_context.Get(), unsupported_frame.Inputs(true)) || !backend.Failed()) return 5;
  const auto* unsupported_status = backend.Status();
  unsupported_context->ClearState();
  if (backend.Evaluate(unsupported_context.Get(), unsupported_frame.Inputs(false))
      || backend.Status() != unsupported_status) return 5;
  std::cout << "Unsupported WARP device keeps stock TAA: " << unsupported_status << '\n';
  backend.Reset();
  if (backend.Failed()) return 5;
  std::cout << "Public SDK hardware DLAA, DLSS Quality/Balanced/Performance/Ultra Performance, Custom 33/40/49/50/53/54/56/58/67/69/75/85/99/100%, "
               "4- and 8-pixel-padded active render rectangles, 4K output, move ownership, resize/mode change, reset, "
               "cross-device and rejected-input smoke checks passed.\n";
  return 0;
}
