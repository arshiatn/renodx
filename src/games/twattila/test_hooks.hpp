/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <array>
#include <atomic>
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
// button's label as the key presses it; the key "shot" takes a screenshot, the key "trace" writes renodx_trace_now next to
// the exe (the frame traces' on-demand marker).
// A game that restarts itself through Steam (Tales of Arise's SteamStub) doesn't inherit the launching shell's
// environment: the same NAME=value lines in renodx_test_hooks.txt next to the exe set them for the process (and the helper
// it starts). The first present deletes the file, so a stub process that relaunches leaves it for the game and the next
// start runs without it.
namespace twattila::test_hooks {

namespace internal {
static renodx::utils::settings::Settings* settings = nullptr;
static bool clock_at_first_present = true;
static std::atomic_uint64_t clock_start = 0;
// The log lines' prefix.
static const char* log_name = "[Total War]";
static std::filesystem::path exe_folder;
constexpr const wchar_t* HOOKS_FILE = L"renodx_test_hooks.txt";

inline void Change(const std::string& key, float value) {
  bool found = false;
  for (auto* setting : *settings) {
    const bool button = setting->value_type == renodx::utils::settings::SettingValueType::BUTTON;
    if (button ? setting->label != key : setting->key != key) continue;
    found = true;
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
  if (!found) renodx::utils::log::w(std::format("{} Test: no setting {}, nothing changed.", log_name, key));
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
    std::array<char, 8192> value = {};
    const DWORD length = GetEnvironmentVariableA("RENODX_TW_SET", value.data(), static_cast<DWORD>(value.size()));
    return std::string(value.data(), length < value.size() ? length : 0);
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
    if (const std::string key = changes.substr(colon + 1, equals - colon - 1); key == "shot") {
      runtime->save_screenshot("auto");
      renodx::utils::log::i(std::format("{} Test: shot pressed.", log_name));
    } else if (key == "trace") {
      std::ofstream(exe_folder / L"renodx_trace_now", std::ios::binary | std::ios::trunc);
      renodx::utils::log::i(std::format("{} Test: renodx_trace_now written.", log_name));
    } else {
      Change(key, std::strtof(changes.c_str() + equals + 1, nullptr));
    }
    const size_t next = changes.find(';');
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

inline void Install(renodx::utils::settings::Settings* settings, bool clock_at_first_present, const char* log_name = "[Total War]") {
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
  internal::exe_folder = std::filesystem::path(exe).parent_path();
  if (std::ifstream file(internal::exe_folder / internal::HOOKS_FILE); file) {
    int lines = 0;
    for (std::string line; std::getline(file, line);) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const size_t equals = line.find('=');
      if (!line.starts_with("RENODX_TW_") || equals == std::string::npos) continue;
      SetEnvironmentVariableA(line.substr(0, equals).c_str(), line.substr(equals + 1).c_str());
      ++lines;
    }
    renodx::utils::log::i(std::format("{} Test: renodx_test_hooks.txt read ({} lines).", log_name, lines));
  }
  if (GetEnvironmentVariableA("RENODX_TW_SHOT", nullptr, 0) == 0 && GetEnvironmentVariableA("RENODX_TW_PROFILE", nullptr, 0) == 0
      && GetEnvironmentVariableA("RENODX_TW_SET", nullptr, 0) == 0) {
    return;
  }
  internal::settings = settings;
  internal::clock_at_first_present = clock_at_first_present;
  internal::log_name = log_name;
  reshade::register_event<reshade::addon_event::reshade_present>(internal::OnReshadePresent);
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::reshade_present>(internal::OnReshadePresent);
}

}  // namespace twattila::test_hooks
