/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/detour.hpp"
#include "../../utils/log.hpp"
#include "./engine_memory.hpp"

// Debug (Advanced): the enemy attack manager's data as the game reads it. Who attacks the player when is no cvar: the
// attack manager loads Scripts/AI/AttackPatterns/*.xml (SequenceDefinitions: Attackers, AttackTypes, Sequences,
// AttackerConstraints) from the encrypted Scripts.pak, once when it starts (its Init, vtable +0x1dff058 slot 1), each
// file through ATTACK_PATTERN_LOADER (this, file name; it calls ISystem::LoadXmlFromFile, vtable +0x348). Its reload
// command (ai_reload_attack_manager) is VF_CHEAT: the console refuses it ("[EXECUTE] command %s is marked
// [VF_CHEAT]"). So the hook goes in at the game's device creation, before the game starts its systems, and copies each
// file the loader reads through the game's CryPak (it decrypts): gEnv->pCryPak sits 0x78 before gEnv->pSystem (the
// loader's mov rcx at +0x10); FOpen vtable +0x110 (name, mode, flags), FReadRaw +0x120 (data, 1, count, file), FClose
// +0x1a8 (as the game's own file writers and readers call them).
namespace rysesonofrome::attack_patterns {

namespace internal {

// push rbp, r12, r13; mov rbp, rsp; sub rsp, 0x40; mov r12, rcx; mov rcx, [rip + gEnv->pSystem]; mov r13, rdx; ...;
// call [rax + 0x348] (LoadXmlFromFile)
constexpr std::string_view ATTACK_PATTERN_LOADER =
    "40 55 41 54 41 55 48 8B EC 48 83 EC 40 4C 8B E1 48 8B 0D ?? ?? ?? ?? 4C 8B EA 48 8B 01 4C 8B C2 48 8D 55 30 45 33 C9 "
    "FF 90 48 03 00 00";
constexpr size_t LOADER_SYSTEM_MOV = 0x10;
constexpr size_t RIP_MOV_SIZE = 7;
constexpr ptrdiff_t PAK_FROM_SYSTEM = -0x78;  // gEnv->pCryPak's place from gEnv->pSystem's
constexpr size_t PAK_FOPEN = 0x110;
constexpr size_t PAK_FREAD_RAW = 0x120;
constexpr size_t PAK_FCLOSE = 0x1a8;

using Loader = bool (*)(void* manager, const char* file_name);
using PakOpen = void* (*)(void* pak, const char* name, const char* mode, uint32_t flags);
using PakReadRaw = size_t (*)(void* pak, void* data, size_t length, size_t count, void* file);
using PakClose = int (*)(void* pak, void* file);

struct State {
  std::shared_mutex mutex;
  bool searched = false;
  std::vector<renodx::utils::detour::Function> hooks;
  void* const* pak = nullptr;  // &gEnv->pCryPak
  std::filesystem::path folder;
  uint32_t saved = 0;
  std::string status;
};
static State state;
static Loader original_loader = nullptr;

// The game's main thread, while its attack manager starts: the file as the loader reads it, copied first.
inline bool OnLoad(void* manager, const char* file_name) {
  if (file_name != nullptr) {
    const std::unique_lock lock(state.mutex);
    void* pak = *state.pak;
    void* file = nullptr;
    if (pak != nullptr) {
      file = (*reinterpret_cast<PakOpen const* const*>(pak))[PAK_FOPEN / sizeof(void*)](pak, file_name, "rb", 0);
    }
    if (file == nullptr) {
      state.status = std::format("could not open {}", file_name);
    } else {
      std::vector<char> text;
      std::array<char, 65536> chunk;
      const auto read = (*reinterpret_cast<PakReadRaw const* const*>(pak))[PAK_FREAD_RAW / sizeof(void*)];
      for (size_t count = 0; (count = read(pak, chunk.data(), 1, chunk.size(), file)) != 0;) {
        text.insert(text.end(), chunk.data(), chunk.data() + count);
        if (count < chunk.size()) break;
      }
      (*reinterpret_cast<PakClose const* const*>(pak))[PAK_FCLOSE / sizeof(void*)](pak, file);
      const std::filesystem::path path = state.folder / std::filesystem::path(file_name).relative_path();
      std::error_code error;
      std::filesystem::create_directories(path.parent_path(), error);
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      out.write(text.data(), static_cast<std::streamsize>(text.size()));
      if (out) {
        ++state.saved;
        state.status = std::format("saved {} files to {}", state.saved, state.folder.string());
      } else {
        state.status = std::format("could not write {}", path.string());
      }
      renodx::utils::log::i("[Ryse DLSS] Attack patterns: ", file_name, " (", text.size(), " bytes)");
    }
  }
  return original_loader(manager, file_name);
}

}  // namespace internal

// Render device creation (the game's code runs: decrypted), before the game starts its systems: hooks the loader so
// the files are saved into `folder` when the attack manager starts.
inline void SaveAtStart(const std::filesystem::path& folder) {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (state.searched) return;
  state.searched = true;
  state.folder = folder;
  std::string found_at;
  uint8_t* code = engine_memory::FindPattern(internal::ATTACK_PATTERN_LOADER, &found_at);
  if (code == nullptr) {
    state.status = std::format("the attack pattern loader was not found ({})", found_at);
  } else {
    int32_t displacement;
    std::memcpy(&displacement, code + internal::LOADER_SYSTEM_MOV + 3, sizeof(displacement));
    const uint8_t* system = code + internal::LOADER_SYSTEM_MOV + internal::RIP_MOV_SIZE + displacement;
    state.pak = reinterpret_cast<void* const*>(system + internal::PAK_FROM_SYSTEM);
    internal::original_loader = reinterpret_cast<internal::Loader>(code);
    state.hooks = {renodx::utils::detour::Function(&internal::original_loader, &internal::OnLoad)};
    try {
      renodx::utils::detour::Install(state.hooks);
      state.status = "waiting for the game to load them";
    } catch (const std::exception& error) {
      state.hooks.clear();
      state.status = std::format("hook failed: {}", error.what());
    }
  }
  renodx::utils::log::i("[Ryse DLSS] Attack patterns (loader ", found_at, "): ", state.status);
}

// The files saved at this start (0: none yet).
inline uint32_t Saved() {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.saved;
}

inline std::string Status() {
  const std::shared_lock lock(internal::state.mutex);
  return internal::state.status;
}

inline void Uninstall() {
  auto& state = internal::state;
  const std::unique_lock lock(state.mutex);
  if (state.hooks.empty()) return;
  try {
    renodx::utils::detour::Uninstall(state.hooks);
  } catch (const std::exception&) {
    // No-op: the game is closing; Detours left its code as it was.
  }
  state.hooks.clear();
}

}  // namespace rysesonofrome::attack_patterns
