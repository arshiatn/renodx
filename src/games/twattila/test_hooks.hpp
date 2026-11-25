/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

#include <include/reshade.hpp>

#include "../../utils/device_proxy.hpp"
#include "../../utils/log.hpp"
#include "../../utils/mutex.hpp"
#include "../../utils/settings.hpp"
#include "./device_end.hpp"
// RENODX_TW_PROFILE's action: Record Ray Tracing Profile, unless the add-on (one without the Total War ray-tracing
// scene) defines its own before including this.
#ifndef RENODX_TEST_HOOKS_PROFILE
#include "./rt_scene.hpp"
#define RENODX_TEST_HOOKS_PROFILE twattila::rt_scene::StartProfile
#endif

// Tests without input, that many seconds after the clock started (Attila: the first present; a game that passes
// clock_at_first_present = false: StartClock):
// RENODX_TW_SHOT=<seconds>[,<count>[,<gap>]] ReShade screenshots (its save path, "auto"; by default a second 5 s later),
// RENODX_TW_PROFILE=<seconds> Record Ray Tracing Profile,
// RENODX_TW_SET=<seconds>:<key>=<value>[;<seconds>:<key>=<value>...] settings changed then, as their sliders would; a
// button's label as the key presses it (every button with that label: several pages have a Reset), "<group>/<label>"
// only that group's (page-presets/Reset); the key "shot" takes a screenshot, the key "trace" writes renodx_trace_now
// next to the exe (the frame traces' on-demand marker), the key "pass_dump" writes its value ("<frames>[ <label>]") into
// renodx_pass_dump_now next to the exe (pass_dump.hpp's trigger: a temp file renamed), the key "memory" logs the address space (free, the largest free
// block: one walk) and, when the add-on defines RENODX_TEST_HOOKS_MEMORY (a function returning text) before including
// this, its own reservations (Attila: the constant buffer ring's).
// RENODX_TW_SET_FILE=<path>: the same entries from a file, one "<seconds>:<key>=<value>;" per line in time order, without
// RENODX_TW_SET's 8 KB limit (with both set, neither is used). Two keys need the add-on's LiveHooks: "env.<NAME>" sets
// that process variable (an empty value clears it) and bumps the add-on's env generation, "mt_trace" opens one trace
// window of that many presents.
// A game that restarts itself through Steam (Tales of Arise's SteamStub) doesn't inherit the launching shell's
// environment: the same NAME=value lines in renodx_test_hooks.txt next to the exe set them for the process (and the helper
// it starts). The first present deletes the file, so a stub process that relaunches leaves it for the game and the next
// start runs without it.
namespace twattila::test_hooks {

// What the set keys "env." and "mt_trace" reach in the add-on (Attila: its worker pool, which this header shared with
// the other games cannot include). Without them those keys change nothing.
struct LiveHooks {
  // Called after an "env." key changed the process environment; returns the add-on's new env generation.
  uint32_t (*env_changed)() = nullptr;
  void (*arm_trace)(uint32_t frames) = nullptr;
};

namespace internal {
static renodx::utils::settings::Settings* settings = nullptr;
static LiveHooks live_hooks = {};
static bool clock_at_first_present = true;
static std::atomic_uint64_t clock_start = 0;
// The log lines' prefix.
static const char* log_name = "[Total War]";
static std::filesystem::path exe_folder;
constexpr const wchar_t* HOOKS_FILE = L"renodx_test_hooks.txt";

inline void Change(const std::string& key, float value) {
  size_t found = 0;
  for (auto* setting : *settings) {
    const bool button = setting->value_type == renodx::utils::settings::SettingValueType::BUTTON;
    if (button ? (setting->label != key && setting->group + "/" + setting->label != key) : setting->key != key) continue;
    ++found;
    if (button) {
      setting->on_change();
      renodx::utils::log::i(std::format("{} Test: {} pressed.", log_name, key));
      continue;
    }
    const float previous = setting->GetValue();
    setting->Set(value);
    setting->on_change();
    {
      const std::unique_lock lock(renodx::utils::mutex::global_mutex);
      setting->Write();
      setting->on_change_value(previous, setting->GetValue());
    }
    renodx::utils::log::i(std::format("{} Test: {} set to {}{}.", log_name, key, setting->GetValue(), (setting->binding == nullptr ? " (it has no binding)" : "")));
  }
  if (found == 0) renodx::utils::log::w(std::format("{} Test: no setting {}, nothing changed.", log_name, key));
  if (found > 1) renodx::utils::log::w(std::format("{} Test: {} matched {} buttons (one alone: <group>/<label>).", log_name, key, found));
}

inline void OnReshadePresent(reshade::api::effect_runtime* runtime) {
  // A D3D9 game behind the D3D11 proxy has a runtime for each device: only the proxy's shows the player's picture (the
  // game's own one again if the proxy could not be made).
  if (renodx::utils::device_proxy::UseProxyRequested() && !renodx::utils::device_proxy::device_proxy_creation_failed
      && !renodx::utils::device_proxy::IsProxyDevice(runtime->get_device())) {
    return;
  }
  static bool hooks_file_removed = false;
  if (!hooks_file_removed) {
    std::error_code ignored;
    std::filesystem::remove(exe_folder / HOOKS_FILE, ignored);
    hooks_file_removed = true;
  }
  if (clock_at_first_present) {
    uint64_t not_started = 0;
    clock_start.compare_exchange_strong(not_started, GetTickCount64());
  }
  const uint64_t started = clock_start.load();
  if (started == 0) return;
  const auto seconds = [](const char* name) {
    std::array<char, 32> value = {};
    return (GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size())) != 0) ? std::strtoull(value.data(), nullptr, 10) * 1000ull
                                                                                                : 0ull;
  };
  static const uint64_t shot_ms = seconds("RENODX_TW_SHOT");
  static const auto [shot_count, shot_gap_ms] = [] {
    std::array<char, 32> value = {};
    GetEnvironmentVariableA("RENODX_TW_SHOT", value.data(), static_cast<DWORD>(value.size()));
    const char* comma = std::strchr(value.data(), ',');
    const char* gap = (comma != nullptr) ? std::strchr(comma + 1, ',') : nullptr;
    return std::pair<int, uint64_t>{(comma != nullptr) ? std::atoi(comma + 1) : 2, (gap != nullptr) ? std::strtoull(gap + 1, nullptr, 10) * 1000ull : 5000ull};
  }();
  static const uint64_t profile_ms = seconds("RENODX_TW_PROFILE");
  static std::string changes = [] {
    const DWORD path_size = GetEnvironmentVariableA("RENODX_TW_SET_FILE", nullptr, 0);
    if (path_size == 0) {
      std::array<char, 8192> value = {};
      const DWORD length = GetEnvironmentVariableA("RENODX_TW_SET", value.data(), static_cast<DWORD>(value.size()));
      return std::string(value.data(), length < value.size() ? length : 0);
    }
    // Two lists would interleave by time only by chance: a run that sets both is a script's mistake.
    if (GetEnvironmentVariableA("RENODX_TW_SET", nullptr, 0) != 0) {
      renodx::utils::log::e(std::format("{} Test: RENODX_TW_SET and RENODX_TW_SET_FILE are both set; neither is used.", log_name));
      return std::string();
    }
    std::string path(path_size, '\0');
    path.resize(GetEnvironmentVariableA("RENODX_TW_SET_FILE", path.data(), path_size));
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      renodx::utils::log::e(std::format("{} Test: RENODX_TW_SET_FILE {} could not be read; no set entries.", log_name, path));
      return std::string();
    }
    std::string list;
    int entries = 0;
    int number = 0;
    for (std::string line; std::getline(file, line);) {
      ++number;
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      // PowerShell 5.1 writes utf8 with a byte order mark and Unicode as UTF-16: either would garble times and keys.
      if (line.starts_with("\xEF\xBB\xBF") || line.find('\0') != std::string::npos) {
        renodx::utils::log::e(std::format("{} Test: RENODX_TW_SET_FILE {} is not plain text (a byte order mark or UTF-16); no set entries.", log_name, path));
        return std::string();
      }
      if (const size_t end = line.find(';'); end != std::string::npos) {
        if (line.find_first_not_of(" \t", end + 1) != std::string::npos) {
          renodx::utils::log::w(std::format("{} Test: RENODX_TW_SET_FILE {} line {} has text after its entry's ';', which is not used.", log_name, path, number));
        }
        line.resize(end);
      }
      if (line.empty()) continue;
      list += line + ';';
      ++entries;
    }
    renodx::utils::log::i(std::format("{} Test: RENODX_TW_SET_FILE {} read ({} entries).", log_name, path, entries));
    return list;
  }();
  static int taken = 0;
  static bool profiled = false;
  const uint64_t now = GetTickCount64();
  if (profile_ms != 0 && !profiled && now >= started + profile_ms) {
    RENODX_TEST_HOOKS_PROFILE();
    profiled = true;
  }
  for (size_t colon = changes.find(':'), equals = changes.find('=');
       colon != std::string::npos && equals > colon && equals != std::string::npos
       && now >= started + (std::strtoull(changes.c_str(), nullptr, 10) * 1000ull);
       colon = changes.find(':'), equals = changes.find('=')) {
    const size_t next = changes.find(';');
    const std::string value = changes.substr(equals + 1, next - equals - 1);
    if (const std::string key = changes.substr(colon + 1, equals - colon - 1); key == "shot") {
      runtime->save_screenshot("auto");
      renodx::utils::log::i(std::format("{} Test: shot pressed.", log_name));
    } else if (key == "trace") {
      std::ofstream(exe_folder / L"renodx_trace_now", std::ios::binary | std::ios::trunc);
      renodx::utils::log::i(std::format("{} Test: renodx_trace_now written.", log_name));
    } else if (key == "pass_dump") {
      // pass_dump.hpp reads the marker every 30 presents, maybe while this writes it: a temp file renamed over it.
      std::error_code error;
      const bool replaced = std::filesystem::exists(exe_folder / L"renodx_pass_dump_now", error);
      std::ofstream(exe_folder / L"renodx_pass_dump_now.tmp", std::ios::binary | std::ios::trunc) << value;
      std::filesystem::rename(exe_folder / L"renodx_pass_dump_now.tmp", exe_folder / L"renodx_pass_dump_now", error);
      if (error) {
        renodx::utils::log::w(std::format("{} Test: renodx_pass_dump_now could not be written ({}).", log_name, error.message()));
      } else {
        renodx::utils::log::i(std::format("{} Test: renodx_pass_dump_now written ({}){}.", log_name, value, (replaced ? ", over one not read yet" : "")));
      }
    } else if (key == "memory") {
      std::string line = std::format("{} Test: memory: {}", log_name, device_end::AddressSpace());
#ifdef RENODX_TEST_HOOKS_MEMORY
      line += RENODX_TEST_HOOKS_MEMORY();
#endif
      renodx::utils::log::i(line + ".");
    } else if (key == "mt_trace") {
      uint32_t frames = 0;
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), frames);
      if (live_hooks.arm_trace == nullptr) {
        renodx::utils::log::w(std::format("{} Test: mt_trace: no trace window in this add-on, nothing armed.", log_name));
      } else if (error != std::errc() || end != value.data() + value.size() || frames == 0) {
        renodx::utils::log::w(std::format("{} Test: mt_trace={} is not a frame count, nothing armed.", log_name, value));
      } else {
        live_hooks.arm_trace(frames);
        // The pool numbers the window in its own header line at the next present.
        renodx::utils::log::i(std::format("{} Test: mt_trace armed {} frames.", log_name, frames));
      }
    } else if (key.starts_with("env.")) {
      const std::string name = key.substr(4);
      if (live_hooks.env_changed == nullptr) {
        renodx::utils::log::w(std::format("{} Test: env {}: no live env in this add-on, nothing changed.", log_name, name));
      } else {
        // An empty value unsets the variable; clearing one that was never set is no failure.
        const bool clear = value.empty();
        const bool set = SetEnvironmentVariableA(name.c_str(), clear ? nullptr : value.c_str()) != 0;
        const DWORD error = set ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_SUCCESS || (clear && error == ERROR_ENVVAR_NOT_FOUND)) {
          const uint32_t generation = live_hooks.env_changed();
          if (clear) {
            renodx::utils::log::i(std::format("{} Test: env {} cleared (generation {}).", log_name, name, generation));
          } else {
            renodx::utils::log::i(std::format("{} Test: env {} set to {} (generation {}).", log_name, name, value, generation));
          }
        } else {
          renodx::utils::log::w(std::format("{} Test: env {} could not be set (error {}), nothing changed.", log_name, name, error));
        }
      }
    } else {
      Change(key, std::strtof(value.c_str(), nullptr));
    }
    changes.erase(0, (next == std::string::npos) ? next : next + 1);
  }
  if (shot_ms == 0 || taken >= shot_count || now < started + shot_ms + (taken * shot_gap_ms)) return;
  runtime->save_screenshot("auto");
  ++taken;
  renodx::utils::log::i(std::format("{} Test: screenshot {}.", log_name, taken));
}
}  // namespace internal

// The clock's start in a game whose loads vary; later calls change nothing.
inline void StartClock() {
  uint64_t not_started = 0;
  internal::clock_start.compare_exchange_strong(not_started, GetTickCount64());
}

inline void Install(renodx::utils::settings::Settings* settings, bool clock_at_first_present, const char* log_name = "[Total War]",
                    LiveHooks live_hooks = {}) {
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  internal::exe_folder = std::filesystem::path(exe).parent_path();
  if (std::ifstream file(internal::exe_folder / internal::HOOKS_FILE); file) {
    int lines = 0;
    for (std::string line; std::getline(file, line);) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      const size_t equals = line.find('=');
      if (!line.starts_with("RENODX_TW_") || equals == std::string::npos) continue;
      SetEnvironmentVariableA(line.substr(0, equals).c_str(), line.substr(equals + 1).c_str());
      ++lines;
    }
    renodx::utils::log::i(std::format("{} Test: renodx_test_hooks.txt read ({} lines).", log_name, lines));
  }
  if (GetEnvironmentVariableA("RENODX_TW_SHOT", nullptr, 0) == 0 && GetEnvironmentVariableA("RENODX_TW_PROFILE", nullptr, 0) == 0
      && GetEnvironmentVariableA("RENODX_TW_SET", nullptr, 0) == 0 && GetEnvironmentVariableA("RENODX_TW_SET_FILE", nullptr, 0) == 0) {
    return;
  }
  internal::settings = settings;
  internal::live_hooks = live_hooks;
  internal::clock_at_first_present = clock_at_first_present;
  internal::log_name = log_name;
  reshade::register_event<reshade::addon_event::reshade_present>(internal::OnReshadePresent);
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::reshade_present>(internal::OnReshadePresent);
}

}  // namespace twattila::test_hooks
