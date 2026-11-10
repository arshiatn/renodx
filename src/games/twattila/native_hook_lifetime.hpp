/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <windows.h>

#include <system_error>
#include <utility>
#include <vector>

#include "../../utils/detour.hpp"

namespace tw::native_hooks {

// Each owner covers hooks whose replacements all reside in this addon. The host
// must still own its load reference while calling Install/Uninstall.
struct Lifetime {
  HMODULE module = nullptr;
  bool retain_until_exit = false;

  void Install(const std::vector<renodx::utils::detour::Function>& hooks) {
    if (hooks.empty()) return;
    if (module == nullptr
        && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               reinterpret_cast<LPCWSTR>(hooks.front().replacement), &module)) {
      throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "retain native hook module");
    }
    try {
      renodx::utils::detour::Install(hooks);
    } catch (...) {
      if (!retain_until_exit) {
        FreeLibrary(std::exchange(module, nullptr));
      }
      throw;
    }
  }

  void Uninstall(std::vector<renodx::utils::detour::Function>* hooks) {
    if (hooks->empty()) return;
    try {
      renodx::utils::detour::Uninstall(*hooks);
    } catch (...) {
      // Keep records and code alive even if the host calls FreeLibrary. A later
      // retry may detach the hooks, but cannot prove all old callbacks returned.
      retain_until_exit = true;
      throw;
    }
    hooks->clear();
    if (!retain_until_exit) {
      FreeLibrary(std::exchange(module, nullptr));
    }
  }

  // Intentionally no destructor releasing module: after a failed detach its
  // reference is a process-lifetime safety hold, including across addon reload.
};

}  // namespace tw::native_hooks
