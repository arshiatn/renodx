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
namespace twattila::test_hooks {

namespace internal {
static renodx::utils::settings::Settings* settings = nullptr;
static bool clock_at_first_present = true;
static std::atomic_uint64_t clock_start = 0;
// The log lines' prefix.
static const char* log_name = "[Total War]";

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
      std::wstring exe(32768, L'\0');
      exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
      std::ofstream(std::filesystem::path(exe).parent_path() / L"renodx_trace_now", std::ios::binary | std::ios::trunc);
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
