/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_dlssd.h>
#include <include/reshade.hpp>

#include "../../utils/directx.hpp"

namespace talesofarise::dlss {

// NVIDIA's DLSS library: presets L and M need 310.5 or newer.
constexpr std::array<uint32_t, 2> MINIMUM_RUNTIME_VERSION = {310, 5};

struct DlssLibrary {
  std::wstring folder;  // empty: not found
  const char* place = "";
  std::array<uint32_t, 3> version = {};  // file version; {0, 0, 0} if unreadable
};

// nvngx_dlss.dll next to this addon, next to the game, or (development builds) in the SDK.
inline DlssLibrary FindDlssLibrary() {
  const auto module_folder = [](HMODULE module) {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length < path.size() ? length : 0);
    return std::filesystem::path(path).parent_path();
  };
  static const int ANCHOR = 0;
  HMODULE addon = nullptr;
  std::vector<std::pair<std::filesystem::path, const char*>> folders;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&ANCHOR), &addon)) {
    folders.emplace_back(module_folder(addon), "next to the addon");
  }
  folders.emplace_back(module_folder(nullptr), "next to the game");
#ifdef RENODX_NGX_RUNTIME_PATH
  folders.emplace_back(RENODX_NGX_RUNTIME_PATH, "in the development SDK");
#endif
  for (const auto& [folder, place] : folders) {
    const auto file = folder / L"nvngx_dlss.dll";
    const auto attributes = GetFileAttributesW(file.c_str());
    if (folder.empty() || attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
    DlssLibrary library = {.folder = folder.wstring(), .place = place};
    // VS_FIXEDFILEINFO from the version resource (type 16, RT_VERSION), without loading the library's code.
    if (HMODULE data = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) {
      if (HRSRC resource = FindResourceW(data, MAKEINTRESOURCEW(VS_VERSION_INFO), MAKEINTRESOURCEW(16))) {
        const auto* bytes = static_cast<const uint8_t*>(LockResource(LoadResource(data, resource)));
        const DWORD size = SizeofResource(data, resource);
        for (DWORD offset = 0; bytes != nullptr && offset + sizeof(VS_FIXEDFILEINFO) <= size; offset += 4) {
          VS_FIXEDFILEINFO info;
          std::memcpy(&info, bytes + offset, sizeof(info));
          if (info.dwSignature != VS_FFI_SIGNATURE) continue;
          library.version = {HIWORD(info.dwFileVersionMS), LOWORD(info.dwFileVersionMS), HIWORD(info.dwFileVersionLS)};
          break;
        }
      }
      FreeLibrary(data);
    }
    return library;
  }
  return {};
}

enum class Mode : uint8_t {
  DLAA,
  DLSS_QUALITY,
  DLSS_BALANCED,
  DLSS_PERFORMANCE,
  DLSS_CUSTOM,
  DLSS_ULTRA_PERFORMANCE,
  DLSS_ULTRA_QUALITY,  // about 77%, on NVIDIA's Quality model
};

// DLSS Ray Reconstruction's guides (the render size; world-space normals, read through `world_to_view`): with them
// rr_d3d12.hpp runs NVIDIA's denoising upscaler (nvngx_dlssd.dll next to nvngx_dlss.dll) in place of Super Resolution.
struct RayReconstruction {
  ID3D11Resource* diffuse_albedo = nullptr;
  ID3D11Resource* specular_albedo = nullptr;
  ID3D11Resource* normals = nullptr;
  ID3D11Resource* roughness = nullptr;
  std::array<float, 16> world_to_view = {};  // row-major (row vectors); view x right, y up, z forward
  std::array<float, 16> view_to_clip = {};
  // NVSDK_NGX_RayReconstruction_Hint_Render_Preset (D, E; Default lets NVIDIA choose).
  uint32_t preset = NVSDK_NGX_RayReconstruction_Hint_Render_Preset_Default;
};

struct Evaluation {
  ID3D11Resource* color = nullptr;
  ID3D11Resource* depth = nullptr;
  ID3D11Resource* motion_vectors = nullptr;
  ID3D11Resource* output = nullptr;
  // Active input rectangle, not its padded allocation.
  uint32_t width = 0;
  uint32_t height = 0;
  float jitter_x = 0.f;
  float jitter_y = 0.f;
  float pre_exposure = 1.f;
  bool reset = false;
  // Zero output dimensions preserve the native-resolution DLAA caller contract.
  uint32_t output_width = 0;
  uint32_t output_height = 0;
  Mode mode = Mode::DLAA;
  // NVSDK_NGX_DLSS_Hint_Render_Preset; Default lets NVIDIA choose per mode.
  uint32_t preset = NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
  // NVSDK_NGX_DLSS_Feature_Flags (UE's: HDR colour, reversed depth, render-size motion vectors, auto exposure).
  int create_flags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_DepthInverted
                     | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
};

// The NGX project DLSS (here) and DLSS Frame Generation (Streamline, frame_generation.hpp) run under: they must match.
inline constexpr char PROJECT_ID[] = "1a688a59-20b4-48a1-9b9b-fd28bd910ec8";

// At process exit (addons unload, the game never released its device) NVIDIA's threads and module state are gone:
// releasing crashes inside NGX. The process frees everything anyway. (Ryse DLSS.)
inline bool ShutdownInProgress() {
  using DllShutdownInProgress = BOOLEAN(NTAPI*)();
  static const auto shutdown_in_progress = reinterpret_cast<DllShutdownInProgress>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlDllShutdownInProgress"));
  return shutdown_in_progress != nullptr && shutdown_in_progress() != FALSE;
}

// Evaluate/Reset must be serialized by the adapter, outside the loader lock.
// The caller owns complete D3D11 state restoration and a private output resource.
class Backend {
 private:
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  NVSDK_NGX_Parameter* parameters_ = nullptr;
  NVSDK_NGX_Handle* feature_ = nullptr;
  std::atomic<const char*> status_ = "Not initialized; game TAA";
  std::wstring cache_path_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t output_width_ = 0;
  uint32_t output_height_ = 0;
  Mode mode_ = Mode::DLAA;
  uint32_t preset_ = NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
  int create_flags_ = 0;
  bool initialized_ = false;
  bool failed_ = false;
  bool first_frame_ = true;

  static void LogResult(const char* operation, const NVSDK_NGX_Result result) {
    std::ostringstream message;
    message << "[Tales DLSS] " << operation << ": NGX result 0x"
            << std::hex << static_cast<uint32_t>(result);
    reshade::log::message(
        (NVSDK_NGX_FAILED(result) ? reshade::log::level::warning : reshade::log::level::info),
        message.str().c_str());
  }

 public:
  Backend() = default;
  Backend(const Backend&) = delete;
  Backend& operator=(const Backend&) = delete;

  Backend(Backend&& other) { *this = std::move(other); }

  Backend& operator=(Backend&& other) {
    if (this == &other) return *this;
    Reset();
    device_ = std::move(other.device_);
    parameters_ = std::exchange(other.parameters_, nullptr);
    feature_ = std::exchange(other.feature_, nullptr);
    status_.store(other.status_.exchange("Not initialized; game TAA", std::memory_order_relaxed),
                  std::memory_order_relaxed);
    cache_path_ = std::move(other.cache_path_);
    width_ = std::exchange(other.width_, 0);
    height_ = std::exchange(other.height_, 0);
    output_width_ = std::exchange(other.output_width_, 0);
    output_height_ = std::exchange(other.output_height_, 0);
    mode_ = std::exchange(other.mode_, Mode::DLAA);
    preset_ = std::exchange(other.preset_, NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
    create_flags_ = std::exchange(other.create_flags_, 0);
    initialized_ = std::exchange(other.initialized_, false);
    failed_ = std::exchange(other.failed_, false);
    first_frame_ = std::exchange(other.first_frame_, true);
    return *this;
  }

  ~Backend() { Reset(); }

  void Reset() {
    if (ShutdownInProgress()) {
      feature_ = nullptr;
      parameters_ = nullptr;
      initialized_ = false;
      device_.Detach();
    }
    if (feature_ != nullptr) {
      const auto result = NVSDK_NGX_D3D11_ReleaseFeature(feature_);
      if (NVSDK_NGX_FAILED(result)) {
        LogResult("ReleaseFeature", result);
      }
      feature_ = nullptr;
    }
    if (parameters_ != nullptr) {
      const auto result = NVSDK_NGX_D3D11_DestroyParameters(parameters_);
      if (NVSDK_NGX_FAILED(result)) {
        LogResult("DestroyParameters", result);
      }
      parameters_ = nullptr;
    }
    if (initialized_) {
      const auto result = NVSDK_NGX_D3D11_Shutdown1(device_.Get());
      if (NVSDK_NGX_FAILED(result)) {
        LogResult("Shutdown1", result);
      }
    }
    initialized_ = false;
    failed_ = false;
    first_frame_ = true;
    width_ = 0;
    height_ = 0;
    output_width_ = 0;
    output_height_ = 0;
    mode_ = Mode::DLAA;
    preset_ = NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
    cache_path_.clear();
    device_.Reset();
    status_.store("Not initialized; game TAA", std::memory_order_relaxed);
  }

  const char* Status() const {
    return status_.load(std::memory_order_relaxed);
  }

  // The feature freed (Ray Reconstruction runs instead: its memory); the next evaluation makes it again.
  void ReleaseFeature() {
    if (feature_ == nullptr) return;
    if (const auto result = NVSDK_NGX_D3D11_ReleaseFeature(feature_); NVSDK_NGX_FAILED(result)) {
      LogResult("ReleaseFeature", result);
    }
    feature_ = nullptr;
  }

  bool Failed() const {
    return failed_;
  }

  bool Evaluate(ID3D11DeviceContext* context, const Evaluation& evaluation) {
    if (context == nullptr || evaluation.color == nullptr || evaluation.depth == nullptr
        || evaluation.motion_vectors == nullptr || evaluation.output == nullptr
        || evaluation.output == evaluation.color || evaluation.width == 0 || evaluation.height == 0
        || (evaluation.output_width == 0) != (evaluation.output_height == 0)
        || !std::isfinite(evaluation.jitter_x) || !std::isfinite(evaluation.jitter_y)
        || !std::isfinite(evaluation.pre_exposure) || evaluation.pre_exposure <= 0.f) {
      status_.store("Invalid temporal inputs; game TAA", std::memory_order_relaxed);
      return false;
    }
    const auto output_width = (evaluation.output_width == 0 ? evaluation.width : evaluation.output_width);
    const auto output_height = (evaluation.output_height == 0 ? evaluation.height : evaluation.output_height);
    switch (evaluation.mode) {
      case Mode::DLAA:
        if (evaluation.width != output_width || evaluation.height != output_height) {
          status_.store("DLAA requires native rendering dimensions; game TAA", std::memory_order_relaxed);
          return false;
        }
        break;
      case Mode::DLSS_QUALITY:
      case Mode::DLSS_BALANCED:
      case Mode::DLSS_PERFORMANCE:
        if (evaluation.width >= output_width || evaluation.height >= output_height) {
          status_.store("DLSS requires lower-resolution input; game TAA", std::memory_order_relaxed);
          return false;
        }
        break;
      case Mode::DLSS_CUSTOM:
        if (evaluation.width > output_width || evaluation.height > output_height
            || static_cast<uint64_t>(evaluation.width) * 100 < output_width
            || static_cast<uint64_t>(evaluation.height) * 100 < output_height
            || (evaluation.width == output_width) != (evaluation.height == output_height)
            || std::abs(static_cast<float>(evaluation.width) / output_width
                        - static_cast<float>(evaluation.height) / output_height)
                   > 2.f / std::min(output_width, output_height)) {
          status_.store("Custom DLSS requires 1-100% rendering dimensions; game TAA", std::memory_order_relaxed);
          return false;
        }
        break;
      case Mode::DLSS_ULTRA_PERFORMANCE:
        if (static_cast<float>(evaluation.width) / output_width < .32f
            || static_cast<float>(evaluation.width) / output_width > .35f
            || static_cast<float>(evaluation.height) / output_height < .32f
            || static_cast<float>(evaluation.height) / output_height > .35f
            || std::abs(static_cast<float>(evaluation.width) / output_width
                        - static_cast<float>(evaluation.height) / output_height)
                   > 2.f / std::min(output_width, output_height)) {
          status_.store("Ultra Performance requires about 33% rendering dimensions; game TAA", std::memory_order_relaxed);
          return false;
        }
        break;
      case Mode::DLSS_ULTRA_QUALITY:
        if (static_cast<float>(evaluation.width) / output_width < .74f
            || static_cast<float>(evaluation.width) / output_width > .80f
            || static_cast<float>(evaluation.height) / output_height < .74f
            || static_cast<float>(evaluation.height) / output_height > .80f
            || std::abs(static_cast<float>(evaluation.width) / output_width
                        - static_cast<float>(evaluation.height) / output_height)
                   > 2.f / std::min(output_width, output_height)) {
          status_.store("Ultra Quality requires about 77% rendering dimensions; game TAA", std::memory_order_relaxed);
          return false;
        }
        break;
      default:
        status_.store("Unknown anti-aliasing mode; game TAA", std::memory_order_relaxed);
        return false;
    }
    renodx::utils::directx::NativeFromReShadeProxy(&context);
    if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
      status_.store("Immediate context required; game TAA", std::memory_order_relaxed);
      return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    const std::array<ID3D11Resource*, 4> resources = {
        evaluation.color, evaluation.depth, evaluation.motion_vectors, evaluation.output};
    constexpr std::array<DXGI_FORMAT, 4> FORMATS = {
        DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT};
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 4> textures;
    for (uint32_t index = 0; index < resources.size(); ++index) {
      if (FAILED(resources[index]->QueryInterface(IID_PPV_ARGS(textures[index].GetAddressOf())))) {
        status_.store("Texture2D temporal inputs required; game TAA", std::memory_order_relaxed);
        return false;
      }
      D3D11_TEXTURE2D_DESC desc = {};
      textures[index]->GetDesc(&desc);
      const bool output = index == 3;
      if (desc.Format != FORMATS[index] || desc.MipLevels != 1 || desc.ArraySize != 1
          || desc.SampleDesc.Count != 1 || (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0
          || (output && ((desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) == 0 || desc.Width != output_width || desc.Height != output_height))
          || (!output && (desc.Width < evaluation.width || desc.Height < evaluation.height))) {
        constexpr std::array<const char*, 4> STATUSES = {
            "Temporal color format or dimensions invalid; game TAA",
            "Temporal depth format or dimensions invalid; game TAA",
            "Temporal motion format or dimensions invalid; game TAA",
            "Temporal output format or dimensions invalid; game TAA"};
        status_.store(STATUSES[index], std::memory_order_relaxed);
        return false;
      }
      Microsoft::WRL::ComPtr<ID3D11Device> owner;
      textures[index]->GetDevice(owner.GetAddressOf());
      // ReShade hooks native resources' GetDevice to return its proxy; unwrap only a borrowed identity.
      auto* native_owner = owner.Get();
      renodx::utils::directx::NativeFromReShadeProxy(&native_owner);
      if (native_owner != device.Get()) {
        status_.store("Temporal textures belong to another device; game TAA", std::memory_order_relaxed);
        return false;
      }
    }
    if (textures[3] == textures[0] || textures[3] == textures[1] || textures[3] == textures[2]) {
      status_.store("Private non-aliasing output required; game TAA", std::memory_order_relaxed);
      return false;
    }
    if (device_ != nullptr && device_.Get() != device.Get()) {
      Reset();
    }
    if (failed_) return false;
    if (device_ == nullptr) {
      device_ = device;
    }

    auto fail = [this](const char* operation, const NVSDK_NGX_Result result, const char* status) {
      LogResult(operation, result);
      // Keep the device identity so unsupported initialization is not retried every frame.
      auto failed_device = device_;
      Reset();
      device_ = failed_device;
      failed_ = true;
      status_.store(status, std::memory_order_relaxed);
      return false;
    };

    if (!initialized_) {
      // nvngx_dlss.dll next to this addon, next to the game, or (development builds) in the SDK.
      const auto library = FindDlssLibrary();
      const auto& runtime_folder = library.folder;
      if (runtime_folder.empty()) {
        return fail("nvngx_dlss.dll not found", NVSDK_NGX_Result_FAIL_FeatureNotFound,
                    "nvngx_dlss.dll not found next to the addon or the game; game TAA");
      }
      reshade::log::message(reshade::log::level::info,
                            ("[Tales DLSS] Using nvngx_dlss.dll " + std::to_string(library.version[0]) + "."
                             + std::to_string(library.version[1]) + "." + std::to_string(library.version[2]) + " from "
                             + std::string(reinterpret_cast<const char*>(std::filesystem::path(runtime_folder).u8string().c_str())))
                                .c_str());
      std::array<wchar_t, MAX_PATH + 1> temp_path = {};
      const auto temp_length = GetTempPathW(static_cast<DWORD>(temp_path.size()), temp_path.data());
      if (temp_length == 0 || temp_length >= temp_path.size()) {
        return fail("GetTempPathW", NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath,
                    "NGX cache path unavailable; game TAA");
      }
      cache_path_.assign(temp_path.data(), temp_length);
      cache_path_ += L"RenoDX_TalesOfArise_DLSS";
      if (!CreateDirectoryW(cache_path_.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return fail("CreateDirectoryW NGX cache", NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath,
                    "NGX cache directory unavailable; game TAA");
      }
      const auto cache_attributes = GetFileAttributesW(cache_path_.c_str());
      if (cache_attributes == INVALID_FILE_ATTRIBUTES || (cache_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return fail("NGX cache is not a directory", NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath,
                    "NGX cache directory invalid; game TAA");
      }
      const wchar_t* runtime_paths[] = {runtime_folder.c_str()};
      const NVSDK_NGX_FeatureCommonInfo common_info = {
          .PathListInfo = {.Path = runtime_paths, .Length = 1},
          .InternalData = nullptr,
          .LoggingInfo = {.LoggingCallback = nullptr, .MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF, .DisableOtherLoggingSinks = false},
      };
      // The injected renderer owns this identity, not the game's UE project or another title's app ID.
      // OTA updates are opt-in through UpdateFeature; this backend never calls it.
      auto result = NVSDK_NGX_D3D11_Init_with_ProjectID(
          PROJECT_ID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", cache_path_.c_str(), device_.Get(), &common_info);
      if (NVSDK_NGX_FAILED(result)) {
        return fail("Init_with_ProjectID", result, "NGX initialization failed; game TAA");
      }
      initialized_ = true;
      result = NVSDK_NGX_D3D11_GetCapabilityParameters(&parameters_);
      if (NVSDK_NGX_FAILED(result)) {
        return fail("GetCapabilityParameters", result, "NGX capabilities unavailable; game TAA");
      }
      if (parameters_ == nullptr) {
        return fail("GetCapabilityParameters returned null", NVSDK_NGX_Result_FAIL_InvalidParameter,
                    "NGX capabilities unavailable; game TAA");
      }
      int available = 0;
      result = NVSDK_NGX_Parameter_GetI(parameters_, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
      if (NVSDK_NGX_FAILED(result) || available == 0) {
        return fail("SuperSampling capability", NVSDK_NGX_Result_FAIL_FeatureNotSupported,
                    "DLAA/DLSS not supported; game TAA");
      }
      int needs_driver_update = 0;
      if (NVSDK_NGX_SUCCEED(NVSDK_NGX_Parameter_GetI(
              parameters_, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver_update))
          && needs_driver_update != 0) {
        return fail("Driver capability", NVSDK_NGX_Result_FAIL_OutOfDate,
                    "DLAA/DLSS requires newer driver; game TAA");
      }
      LogResult("Initialized public D3D11 NGX SDK", NVSDK_NGX_Result_Success);
    }

    if (feature_ == nullptr || width_ != evaluation.width || height_ != evaluation.height
        || output_width_ != output_width || output_height_ != output_height || mode_ != evaluation.mode
        || preset_ != evaluation.preset || create_flags_ != evaluation.create_flags) {
      struct Settings {
        NVSDK_NGX_PerfQuality_Value quality = NVSDK_NGX_PerfQuality_Value_DLAA;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int max_width = 0;
        unsigned int max_height = 0;
        unsigned int min_width = 0;
        unsigned int min_height = 0;
      } selected;
      NVSDK_NGX_PerfQuality_Value quality = NVSDK_NGX_PerfQuality_Value_DLAA;
      switch (evaluation.mode) {
        case Mode::DLAA:
          break;
        case Mode::DLSS_QUALITY:
        case Mode::DLSS_ULTRA_QUALITY:
          quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
          break;
        case Mode::DLSS_BALANCED:
          quality = NVSDK_NGX_PerfQuality_Value_Balanced;
          break;
        case Mode::DLSS_PERFORMANCE:
          quality = NVSDK_NGX_PerfQuality_Value_MaxPerf;
          break;
        case Mode::DLSS_ULTRA_PERFORMANCE:
          quality = NVSDK_NGX_PerfQuality_Value_UltraPerformance;
          break;
        case Mode::DLSS_CUSTOM:
          if (evaluation.width != output_width || evaluation.height != output_height) {
            quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
          }
          break;
      }
      const bool custom_upscaling = evaluation.mode == Mode::DLSS_CUSTOM
                                    && quality != NVSDK_NGX_PerfQuality_Value_DLAA;
      // Check only during reconfiguration, not on the per-frame evaluation path.
      constexpr std::array<NVSDK_NGX_PerfQuality_Value, 4> CUSTOM_QUALITIES = {
          NVSDK_NGX_PerfQuality_Value_MaxQuality, NVSDK_NGX_PerfQuality_Value_Balanced,
          NVSDK_NGX_PerfQuality_Value_MaxPerf, NVSDK_NGX_PerfQuality_Value_UltraPerformance};
      float closest_distance = std::numeric_limits<float>::infinity();
      bool settings_found = false;
      // NVIDIA reports no dynamic range below 50% (Ultra Performance min = max = optimal),
      // and the game's whole-number percentages miss 1/3 (33% -> 1268x713 at 4K).
      // Outside every reported range, the closest mode is created at the active size instead.
      Settings closest_outside;
      float closest_outside_distance = std::numeric_limits<float>::infinity();
      for (size_t index = 0; index < (custom_upscaling ? CUSTOM_QUALITIES.size() : 1); ++index) {
        Settings settings = {.quality = (custom_upscaling ? CUSTOM_QUALITIES[index] : quality)};
        float sharpness = 0.f;
        const auto result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
            parameters_, output_width, output_height, settings.quality, &settings.width, &settings.height,
            &settings.max_width, &settings.max_height, &settings.min_width, &settings.min_height, &sharpness);
        if (NVSDK_NGX_FAILED(result)) {
          LogResult("GetOptimalSettings", result);
          if (custom_upscaling) continue;
          status_.store("DLSS mode settings unavailable; game TAA", std::memory_order_relaxed);
          return false;
        }
        if (settings.width == 0 || settings.height == 0 || settings.min_width == 0 || settings.min_height == 0
            || settings.min_width > settings.width || settings.min_height > settings.height
            || settings.width > settings.max_width || settings.height > settings.max_height
            || settings.max_width > output_width || settings.max_height > output_height) {
          LogResult("Invalid optimal settings", NVSDK_NGX_Result_FAIL_InvalidParameter);
          if (custom_upscaling) continue;
          status_.store("DLSS mode settings invalid; game TAA", std::memory_order_relaxed);
          return false;
        }
        const float distance = std::abs(static_cast<float>(evaluation.width) / output_width
                                        - static_cast<float>(settings.width) / output_width)
                               + std::abs(static_cast<float>(evaluation.height) / output_height
                                          - static_cast<float>(settings.height) / output_height);
        if (evaluation.width < settings.min_width || evaluation.height < settings.min_height
            || evaluation.width > settings.max_width || evaluation.height > settings.max_height) {
          if (distance < closest_outside_distance) {
            closest_outside = settings;
            closest_outside_distance = distance;
          }
          continue;
        }
        if (distance < closest_distance) {
          selected = settings;
          closest_distance = distance;
          settings_found = true;
        }
      }
      const bool create_at_active = !settings_found;
      if (create_at_active) {
        if (closest_outside_distance == std::numeric_limits<float>::infinity()) {
          status_.store("DLSS mode settings unavailable; game TAA", std::memory_order_relaxed);
          return false;
        }
        selected = closest_outside;
        std::ostringstream message;
        message << "[Tales DLSS] SDK mode " << selected.quality << "; supported input "
                << selected.min_width << 'x' << selected.min_height << ".."
                << selected.max_width << 'x' << selected.max_height << "; creating at active input "
                << evaluation.width << 'x' << evaluation.height << " -> " << output_width << 'x' << output_height;
        reshade::log::message(reshade::log::level::info, message.str().c_str());
      }
      if (feature_ != nullptr) {
        const auto result = NVSDK_NGX_D3D11_ReleaseFeature(feature_);
        feature_ = nullptr;
        if (NVSDK_NGX_FAILED(result)) {
          return fail("ReleaseFeature during resize/mode change", result, "DLAA/DLSS reconfiguration failed; game TAA");
        }
        first_frame_ = true;
      }
      // Inside NVIDIA's range: the SDK's creation dimensions (the active subrectangle may differ).
      // Outside it: exactly the active size. Scale changes recreate the feature either way.
      NVSDK_NGX_DLSS_Create_Params create = {
          .Feature = {.InWidth = (create_at_active ? evaluation.width : selected.width),
                      .InHeight = (create_at_active ? evaluation.height : selected.height),
                      .InTargetWidth = output_width,
                      .InTargetHeight = output_height,
                      .InPerfQualityValue = selected.quality},
          .InFeatureCreateFlags = evaluation.create_flags,
          .InEnableOutputSubrects = false,
      };
      const char* feature_name = "Created native-resolution DLAA";
      switch (selected.quality) {
        case NVSDK_NGX_PerfQuality_Value_MaxQuality:
          feature_name = "Created DLSS Quality";
          break;
        case NVSDK_NGX_PerfQuality_Value_Balanced:
          feature_name = "Created DLSS Balanced";
          break;
        case NVSDK_NGX_PerfQuality_Value_MaxPerf:
          feature_name = "Created DLSS Performance";
          break;
        case NVSDK_NGX_PerfQuality_Value_UltraPerformance:
          feature_name = "Created DLSS Ultra Performance";
          break;
        default:
          break;
      }
      // Every mode's hint: NVIDIA picks the mode's preset by the input / output ratio, which can differ from the
      // selected one (Custom; Ryse v20: 40% used its default M). A preset forced in the NVIDIA app or Profile
      // Inspector overrides these.
      for (const char* preset_name : {NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
                                      NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
                                      NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
                                      NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality}) {
        NVSDK_NGX_Parameter_SetUI(parameters_, preset_name, evaluation.preset);
      }
      NVSDK_NGX_Parameter_SetI(parameters_, NVSDK_NGX_Parameter_FreeMemOnReleaseFeature, 1);
      const auto result = NGX_D3D11_CREATE_DLSS_EXT(context, &feature_, parameters_, &create);
      if (NVSDK_NGX_FAILED(result)) {
        return fail("Create DLAA/DLSS", result, "DLAA/DLSS creation failed; game TAA");
      }
      if (feature_ == nullptr) {
        return fail("CreateFeature returned null", NVSDK_NGX_Result_FAIL_InvalidParameter,
                    "DLAA/DLSS creation failed; game TAA");
      }
      width_ = evaluation.width;
      height_ = evaluation.height;
      output_width_ = output_width;
      output_height_ = output_height;
      mode_ = evaluation.mode;
      preset_ = evaluation.preset;
      create_flags_ = evaluation.create_flags;
      first_frame_ = true;
      LogResult(feature_name, result);
      std::ostringstream message;
      message << "[Tales DLSS] SDK mode " << selected.quality << "; preset "
              << (preset_ == NVSDK_NGX_DLSS_Hint_Render_Preset_Default ? std::string("NVIDIA default")
                                                                       : std::string(1, static_cast<char>('A' + preset_ - 1)))
              << "; optimal "
              << selected.width << 'x' << selected.height << "; supported input "
              << selected.min_width << 'x' << selected.min_height << ".."
              << selected.max_width << 'x' << selected.max_height << "; active input "
              << width_ << 'x' << height_ << " -> " << output_width_ << 'x' << output_height_;
      reshade::log::message(reshade::log::level::info, message.str().c_str());
    }

    NVSDK_NGX_D3D11_DLSS_Eval_Params parameters = {
        .Feature = {.pInColor = evaluation.color, .pInOutput = evaluation.output, .InSharpness = 0.f},
        .pInDepth = evaluation.depth,
        .pInMotionVectors = evaluation.motion_vectors,
        .InJitterOffsetX = evaluation.jitter_x,
        .InJitterOffsetY = evaluation.jitter_y,
        .InRenderSubrectDimensions = {.Width = evaluation.width, .Height = evaluation.height},
        .InReset = (evaluation.reset || first_frame_ ? 1 : 0),
        .InMVScaleX = 1.f,
        .InMVScaleY = 1.f,
        .InPreExposure = evaluation.pre_exposure,
        .InExposureScale = 1.f,
    };
    const auto result = NGX_D3D11_EVALUATE_DLSS_EXT(context, feature_, parameters_, &parameters);
    if (NVSDK_NGX_FAILED(result)) {
      return fail("Evaluate DLAA/DLSS", result, "DLAA/DLSS evaluation failed; game TAA");
    }
    first_frame_ = false;
    switch (evaluation.mode) {
      case Mode::DLAA:
        status_.store("DLAA active (native resolution)", std::memory_order_relaxed);
        break;
      case Mode::DLSS_QUALITY:
        status_.store("DLSS Quality active (upscaling)", std::memory_order_relaxed);
        break;
      case Mode::DLSS_BALANCED:
        status_.store("DLSS Balanced active (upscaling)", std::memory_order_relaxed);
        break;
      case Mode::DLSS_PERFORMANCE:
        status_.store("DLSS Performance active (upscaling)", std::memory_order_relaxed);
        break;
      case Mode::DLSS_ULTRA_PERFORMANCE:
        status_.store("DLSS Ultra Performance active (upscaling)", std::memory_order_relaxed);
        break;
      case Mode::DLSS_ULTRA_QUALITY:
        status_.store("DLSS Ultra Quality active (upscaling)", std::memory_order_relaxed);
        break;
      case Mode::DLSS_CUSTOM:
        status_.store((evaluation.width == output_width && evaluation.height == output_height
                           ? "Custom active (native-resolution DLAA)"
                           : "Custom DLSS active (upscaling)"),
                      std::memory_order_relaxed);
        break;
    }
    return true;
  }
};

}  // namespace talesofarise::dlss
