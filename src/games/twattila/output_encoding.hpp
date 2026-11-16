/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

#include <include/reshade.hpp>

#include "../../mods/swapchain.hpp"
#include "../../utils/device_proxy.hpp"
#include "../../utils/log.hpp"
#include "../../utils/platform.hpp"
#include "../../utils/settings.hpp"

// The Output setting of the Total War addons (and Ryse's, Tales of Arise's, Expeditions: Rome's), decided for each new swap chain: a game that rebuilds its device (a GPU
// reset, a display change) gets the output its display takes then, not the one of the game's start.
namespace twattila::output_encoding {

// The setting's values; the first three are the shaders' swap_chain_encoding too.
constexpr int HDR10 = 0;
constexpr int SCRGB = 1;
constexpr int SDR = 2;  // 10-bit, gamma 2.2
constexpr int AUTO = 3;

struct Config {
  // Null: never configured (an addon without this Output setting, as Divinity 2's on the game's own HDR10 chain with
  // device_loss): Decide and Apply leave the output as the addon set it.
  float* shader_encoding = nullptr;
  int setting = AUTO;
  // The HDR output of a setting other than SDR. Null: HDR10 where chosen, else scRGB.
  int (*hdr_encoding)(int setting) = nullptr;
  // The game's SDR preset: a player's first start (no look rows saved yet) on a display without HDR takes it. Null: none.
  void (*first_start_without_hdr)() = nullptr;
  // The Rendering Mode row's key: a start whose first preset holds no value for it is a first start.
  const char* first_start_key = "tonemapper";
  // The game's Recommended preset. After a first start without HDR, Auto switches between the two presets when Windows HDR
  // comes or goes, until the player changes a row (PRESET_SWITCH_KEY). Null: no switch.
  void (*recommended_preset)() = nullptr;
  // The addon's log prefix.
  const char* log_name = "[Total War]";
};

static Config config;

// The last new swap chain's output.
static std::optional<int> decided;

// No look rows saved at the addon's start (the game's first preset holds no Rendering Mode).
static bool first_start = false;

// The global ini key of Auto's preset switch: "sdr <hash>" or "hdr <hash>", the preset Auto applied last and the hash of
// every non-global row right after it; "off": a first start with Windows HDR on, or a row changed since Auto's preset,
// never again. No key: nothing decided (a first start decides).
constexpr const char* PRESET_SWITCH_KEY = "OutputAutoPreset";

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
  if (config.shader_encoding == nullptr) return;
  *config.shader_encoding = static_cast<float>(encoding);
  if (encoding == SDR) {
    renodx::mods::swapchain::target_format = reshade::api::format::r10g10b10a2_unorm;
    renodx::mods::swapchain::target_color_space = reshade::api::color_space::srgb_nonlinear;
    // A D3D9 game's D3D11 proxy reads its own target (SetUseHDR10 sets it for the HDR outputs); unused without a proxy.
    renodx::utils::device_proxy::SetTargetFormat(renodx::mods::swapchain::target_format);
    renodx::utils::device_proxy::SetTargetColorSpace(renodx::mods::swapchain::target_color_space);
  } else {
    renodx::mods::swapchain::SetUseHDR10(encoding == HDR10);
  }
}

// At the addon's start, before any swap chain: the display is not known yet.
inline void Configure(const Config& game_config) {
  config = game_config;
  if (config.first_start_without_hdr != nullptr) {
    float saved_mode = 0.f;
    first_start = !reshade::get_config_value(nullptr, (renodx::utils::settings::global_name + "-preset1").c_str(), config.first_start_key, saved_mode);
  }
  Apply(Encoding(config.setting, {}));
}

// A new swap chain, with what Windows says of the display its window is on.
inline void Decide(const WindowsHdr& windows_hdr) {
  if (config.shader_encoding == nullptr) return;
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
    renodx::utils::log::w(config.log_name, " Output: Windows HDR is off for every display, SDR instead of ", NAMES[config.setting],
                          " for this swap chain.");
  } else {
    renodx::utils::log::i(config.log_name, " Output: ", NAMES[encoding], " (Windows HDR ", windows_state, ").");
  }

  // Auto's preset switch. A player whose first start had Windows HDR on is never given a preset; one whose first start
  // had it off gets the SDR preset, the Recommended one when Windows HDR comes on, the SDR one again when it goes, each
  // once per change of Windows HDR and only while no row changed since the last.
  if (config.setting == AUTO && config.first_start_without_hdr != nullptr && !renodx::utils::settings::IsPresetOff()) {
    const std::string state = renodx::utils::settings::ReadGlobalString(PRESET_SWITCH_KEY);
    const auto state_of = [](const char* side) {
      uint32_t hash = 2166136261u;  // FNV-1a
      const auto add = [&hash](std::span<const std::byte> bytes) {
        for (const std::byte byte : bytes) {
          hash = (hash ^ std::to_integer<uint32_t>(byte)) * 16777619u;
        }
      };
      for (const auto* setting : *renodx::utils::settings::settings) {
        if (setting->key.empty() || setting->is_global || !setting->HasNumericValue()) continue;
        const float value = setting->GetValue();
        add(std::as_bytes(std::span(setting->key.c_str(), setting->key.size() + 1)));
        add(std::as_bytes(std::span(&value, 1)));
      }
      return std::format("{} {:08x}", side, hash);
    };
    const auto apply = [&state_of](void (*preset)(), const char* side) {
      preset();
      // Saved and read back as the next start reads them (clamped to each row's range): the hash is that start's.
      const std::string section = renodx::utils::settings::GetCurrentPresetName();
      renodx::utils::settings::SaveSettings(section);
      renodx::utils::settings::LoadSettings(section);
      renodx::utils::settings::WriteGlobalString(PRESET_SWITCH_KEY, state_of(side));
    };
    if (state.empty()) {
      // Only Windows HDR known off: without an answer the output falls to SDR for the chain, the look waits for one.
      if (!decided.has_value() && first_start && windows_hdr.known && !windows_hdr.on) {
        apply(config.first_start_without_hdr, "sdr");
        renodx::utils::log::i(config.log_name, " Output Auto: first start without Windows HDR, the SDR preset applied.");
      } else if (!decided.has_value() && first_start && windows_hdr.known) {
        renodx::utils::settings::WriteGlobalString(PRESET_SWITCH_KEY, "off");
      }
    } else if (state != "off") {
      const bool sdr_applied = (state == state_of("sdr"));
      if (!sdr_applied && state != state_of("hdr")) {
        renodx::utils::settings::WriteGlobalString(PRESET_SWITCH_KEY, "off");
        renodx::utils::log::i(config.log_name, " Output Auto: a row changed since the automatic preset, no automatic preset again.");
      } else if (windows_hdr.known && config.recommended_preset != nullptr && sdr_applied && encoding != SDR) {
        apply(config.recommended_preset, "hdr");
        renodx::utils::log::i(config.log_name, " Output Auto: Windows HDR on since the SDR preset, the Recommended preset applied.");
      } else if (windows_hdr.known && config.recommended_preset != nullptr && !sdr_applied && encoding == SDR) {
        apply(config.first_start_without_hdr, "sdr");
        renodx::utils::log::i(config.log_name, " Output Auto: Windows HDR off since the Recommended preset, the SDR preset applied.");
      }
    }
  }
  decided = encoding;
}

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
  Decide(windows_hdr);
}

}  // namespace twattila::output_encoding
