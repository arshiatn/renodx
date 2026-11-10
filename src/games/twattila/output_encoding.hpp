/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

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

// Windows HDR for a display. Not known: Windows gave no answer for it.
struct WindowsHdr {
  bool known = false;
  bool on = true;
};

// Auto follows Windows HDR and takes SDR where Windows does not answer (as before). A chosen HDR output only gives way
// to a known off: on a desktop without HDR its picture is white.
inline int Encoding(int setting, const WindowsHdr& windows_hdr) {
  if (setting == SDR) return SDR;
  if (!windows_hdr.on && (setting == AUTO || windows_hdr.known)) return SDR;
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

static bool decided_once = false;

// A new swap chain for `window`, before the swapchain mod reads the output.
inline void Decide(HWND window) {
  WindowsHdr windows_hdr;
  // No display path for the window: HDR, as before.
  if (const auto path = renodx::utils::platform::GetPathInfo(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY)); path.has_value()) {
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 color_info_2 = {
        .header = {
            .type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2,
            .size = sizeof(color_info_2),
            .adapterId = path->targetInfo.adapterId,
            .id = path->targetInfo.id,
        },
    };
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color_info = {
        .header = {
            .type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO,
            .size = sizeof(color_info),
            .adapterId = path->targetInfo.adapterId,
            .id = path->targetInfo.id,
        },
    };
    if (DisplayConfigGetDeviceInfo(&color_info_2.header) == ERROR_SUCCESS) {
      windows_hdr = {.known = true, .on = color_info_2.highDynamicRangeUserEnabled != 0};
    } else if (DisplayConfigGetDeviceInfo(&color_info.header) == ERROR_SUCCESS) {  // Windows 10
      windows_hdr = {.known = true, .on = color_info.advancedColorEnabled != 0};
    } else {
      windows_hdr = {.known = false, .on = false};
    }
  }
  const int encoding = Encoding(config.setting, windows_hdr);
  Apply(encoding);
  constexpr const char* NAMES[] = {"HDR10", "scRGB", "SDR"};
  const char* windows_state = "on or unknown";
  if (windows_hdr.known) {
    windows_state = (windows_hdr.on ? "on" : "off");
  } else if (!windows_hdr.on) {
    windows_state = "not answered";
  }
  if (encoding == SDR && config.setting != SDR && config.setting != AUTO) {
    renodx::utils::log::w("[Total War] Output: Windows HDR is off for the game's display, SDR instead of ", NAMES[config.setting],
                          " for this swap chain.");
  } else {
    renodx::utils::log::i("[Total War] Output: ", NAMES[encoding], " (Windows HDR ", windows_state, ").");
  }
  if (!decided_once && config.first_start_without_hdr != nullptr && config.setting == AUTO && encoding == SDR) {
    config.first_start_without_hdr();
  }
  decided_once = true;
}

}  // namespace twattila::output_encoding
