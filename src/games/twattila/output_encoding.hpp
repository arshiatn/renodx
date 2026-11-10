/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <optional>

#include <include/reshade.hpp>

#include "../../mods/swapchain.hpp"
#include "../../utils/log.hpp"
#include "../../utils/platform.hpp"

// The Output setting of the Total War addons, decided for each new swap chain: a game that rebuilds its device (a GPU
// reset, a display change) gets the output its display takes then, not the one of the game's start.
namespace twattila::output_encoding {

// The setting's values; the first three are the shaders' swap_chain_encoding too.
constexpr int HDR10 = 0;
constexpr int SCRGB = 1;
constexpr int SDR = 2;  // 10-bit, gamma 2.2
constexpr int AUTO = 3;

struct Config {
  float* shader_encoding = nullptr;
  int setting = AUTO;
  // The HDR output of a setting other than SDR. Null: HDR10 where chosen, else scRGB.
  int (*hdr_encoding)(int setting) = nullptr;
  // A player's first start, on a display without HDR. Null: not a first start.
  void (*first_start_without_hdr)() = nullptr;
};

static Config config;

// Windows HDR for the display the game's window is on. Not known: Windows gave no answer for it.
struct WindowsHdr {
  bool known = false;
  bool on = true;
  // Known off there, and on for another active display: a game's window can still stand on another display than the one
  // it will show on when its swap chain is made.
  bool on_elsewhere = false;
};

// Auto follows Windows HDR; where Windows does not answer it keeps what the chain before got (`earlier`), SDR without
// one (as before). A chosen HDR output only gives way when Windows HDR is known off and no active display has it on: on
// a desktop without HDR its picture is white.
inline int Encoding(int setting, const WindowsHdr& windows_hdr, std::optional<int> earlier = std::nullopt) {
  if (setting == SDR) return SDR;
  if (setting == AUTO) {
    if (!windows_hdr.known && earlier.has_value()) return *earlier;
    if (!windows_hdr.on) return SDR;
  } else if (windows_hdr.known && !windows_hdr.on && !windows_hdr.on_elsewhere) {
    return SDR;
  }
  if (config.hdr_encoding != nullptr) return config.hdr_encoding(setting);
  return (setting == HDR10) ? HDR10 : SCRGB;
}

inline void Apply(int encoding) {
  *config.shader_encoding = static_cast<float>(encoding);
  if (encoding == SDR) {
    renodx::mods::swapchain::target_format = reshade::api::format::r10g10b10a2_unorm;
    renodx::mods::swapchain::target_color_space = reshade::api::color_space::srgb_nonlinear;
  } else {
    renodx::mods::swapchain::SetUseHDR10(encoding == HDR10);
  }
}

// At the addon's start, before any swap chain: the display is not known yet.
inline void Configure(const Config& game_config) {
  config = game_config;
  Apply(Encoding(config.setting, {}));
}

// The last new swap chain's output.
static std::optional<int> decided;

// A new swap chain for `window`, before the swapchain mod reads the output.
inline void Decide(HWND window) {
  const auto hdr_of = [](const DISPLAYCONFIG_PATH_INFO& path) {
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 color_info_2 = {
        .header = {
            .type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2,
            .size = sizeof(color_info_2),
            .adapterId = path.targetInfo.adapterId,
            .id = path.targetInfo.id,
        },
    };
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color_info = {
        .header = {
            .type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO,
            .size = sizeof(color_info),
            .adapterId = path.targetInfo.adapterId,
            .id = path.targetInfo.id,
        },
    };
    if (DisplayConfigGetDeviceInfo(&color_info_2.header) == ERROR_SUCCESS) {
      return WindowsHdr{.known = true, .on = color_info_2.highDynamicRangeUserEnabled != 0};
    }
    if (DisplayConfigGetDeviceInfo(&color_info.header) == ERROR_SUCCESS) {  // Windows 10
      return WindowsHdr{.known = true, .on = color_info.advancedColorEnabled != 0};
    }
    return WindowsHdr{.known = false, .on = false};
  };
  WindowsHdr windows_hdr;
  // No display path for the window: HDR, as before.
  if (const auto path = renodx::utils::platform::GetPathInfo(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY)); path.has_value()) {
    windows_hdr = hdr_of(*path);
    if (windows_hdr.known && !windows_hdr.on) {
      windows_hdr.on_elsewhere = std::ranges::any_of(renodx::utils::platform::GetPathInfos(), [&hdr_of](const DISPLAYCONFIG_PATH_INFO& other) {
        const WindowsHdr other_hdr = hdr_of(other);
        return other_hdr.known && other_hdr.on;
      });
    }
  }
  const int encoding = Encoding(config.setting, windows_hdr, decided);
  Apply(encoding);
  constexpr const char* NAMES[] = {"HDR10", "scRGB", "SDR"};
  const char* windows_state = "on or unknown";
  if (windows_hdr.known && windows_hdr.on) {
    windows_state = "on";
  } else if (windows_hdr.known && windows_hdr.on_elsewhere) {
    windows_state = "off for the window's display, on for another";
  } else if (windows_hdr.known) {
    windows_state = "off";
  } else if (!windows_hdr.on) {
    windows_state = "not answered";
  }
  if (encoding == SDR && config.setting != SDR && config.setting != AUTO) {
    renodx::utils::log::w("[Total War] Output: Windows HDR is off for every display, SDR instead of ", NAMES[config.setting],
                          " for this swap chain.");
  } else {
    renodx::utils::log::i("[Total War] Output: ", NAMES[encoding], " (Windows HDR ", windows_state, ").");
  }
  if (!decided.has_value() && config.first_start_without_hdr != nullptr && config.setting == AUTO && encoding == SDR) {
    config.first_start_without_hdr();
  }
  decided = encoding;
}

}  // namespace twattila::output_encoding
