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
#include <format>
#include <mutex>
#include <string>
#include <utility>

#include <include/reshade.hpp>

#include "../../utils/log.hpp"
#include "../../utils/mutex.hpp"
#include "../../utils/settings.hpp"
#include "./rt_scene.hpp"

// Tests without input, that many seconds after the clock started (Attila: the first present; a game that passes
// clock_at_first_present = false: StartClock):
// RENODX_TW_SHOT=<seconds>[,<count>[,<gap>]] ReShade screenshots (its save path, "auto"; by default a second 5 s later),
// RENODX_TW_PROFILE=<seconds> Record Ray Tracing Profile,
// RENODX_TW_SET=<seconds>:<key>=<value>[;<seconds>:<key>=<value>...] settings changed then, as their sliders would; a
// button's label as the key presses it.
namespace twattila::test_hooks {

namespace internal {
static renodx::utils::settings::Settings* settings = nullptr;
static bool clock_at_first_present = true;
static std::atomic_uint64_t clock_start = 0;

inline void Change(const std::string& key, float value) {
  bool found = false;
  for (auto* setting : *settings) {
    const bool button = setting->value_type == renodx::utils::settings::SettingValueType::BUTTON;
    if (button ? setting->label != key : setting->key != key) continue;
    found = true;
    if (button) {
      setting->on_change();
      renodx::utils::log::i(std::format("[Total War] Test: {} pressed.", key));
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
    renodx::utils::log::i(std::format("[Total War] Test: {} set to {}{}.", key, setting->GetValue(), (setting->binding == nullptr ? " (it has no binding)" : "")));
  }
  if (!found) renodx::utils::log::w(std::format("[Total War] Test: no setting {}, nothing changed.", key));
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
    rt_scene::StartProfile();
    profiled = true;
  }
  for (size_t colon = changes.find(':'), equals = changes.find('=');
       colon != std::string::npos && equals > colon && equals != std::string::npos
       && now >= started + (std::strtoull(changes.c_str(), nullptr, 10) * 1000ull);
       colon = changes.find(':'), equals = changes.find('=')) {
    Change(changes.substr(colon + 1, equals - colon - 1), std::strtof(changes.c_str() + equals + 1, nullptr));
    const size_t next = changes.find(';');
    changes.erase(0, (next == std::string::npos) ? next : next + 1);
  }
  if (shot_ms == 0 || taken >= shot_count || now < started + shot_ms + (taken * shot_gap_ms)) return;
  runtime->save_screenshot("auto");
  ++taken;
  renodx::utils::log::i(std::format("[Total War] Test: screenshot {}.", taken));
}
}  // namespace internal

// The clock's start in a game whose loads vary; later calls change nothing.
inline void StartClock() {
  uint64_t not_started = 0;
  internal::clock_start.compare_exchange_strong(not_started, GetTickCount64());
}

inline void Install(renodx::utils::settings::Settings* settings, bool clock_at_first_present) {
  if (GetEnvironmentVariableA("RENODX_TW_SHOT", nullptr, 0) == 0 && GetEnvironmentVariableA("RENODX_TW_PROFILE", nullptr, 0) == 0
      && GetEnvironmentVariableA("RENODX_TW_SET", nullptr, 0) == 0) {
    return;
  }
  internal::settings = settings;
  internal::clock_at_first_present = clock_at_first_present;
  reshade::register_event<reshade::addon_event::reshade_present>(internal::OnReshadePresent);
}

inline void Uninstall() {
  reshade::unregister_event<reshade::addon_event::reshade_present>(internal::OnReshadePresent);
}

}  // namespace twattila::test_hooks
