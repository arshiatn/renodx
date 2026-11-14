/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>
#include <dxgiformat.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <vector>

#include "../../utils/detour.hpp"
#include "../../utils/log.hpp"
#include "../twattila/native_hook_lifetime.hpp"

// The game's swap chain build after a Resolution Scale change (ATN's crash 2026-11-14 10:22:26, crash_1022.dmp: an
// AddRef on a null view at exe + 0x6ee594). The exe's Renderer::Resize(width, height, format, ...) (RVA 0xFB420) resizes
// the swap chain in `format`, makes its back buffer's RTV in it and an sRGB RTV beside it, but only for the formats its
// switch knows (28, 87, 88: R8G8B8A8, B8G8R8A8, B8G8R8X8); any other leaves the sRGB view null (renderer + 0x160), and the
// render-size targets' build right after AddRefs it. The game passes the format its back buffer's RTV GetDesc gives
// (exe + 0x6ebf27), and the swapchain mod's upgrade made that R16G16B16A16_FLOAT (scRGB; R10G10B10A2_UNORM for HDR10 and
// SDR): ResizeBuffers asked for 10, no sRGB view. Here the game's own 8-bit format goes back in for either, so the resize
// runs as at the start (ResizeBuffers 87, the mod upgrading the chain and both views). Found by its bytes in the Steam
// build (TimeDateStamp 0x61E596FC); another build is left alone (logged).
namespace twromeremastered::swap_chain_format {

namespace internal {

using Resize = uint64_t(__fastcall*)(void* renderer, uint32_t width, uint32_t height, uint32_t format, uint32_t fifth, uint32_t sixth);

constexpr uintptr_t RESIZE_RVA = 0xFB420;
constexpr std::array<uint8_t, 25> PROLOGUE = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                                              0x48, 0x8D, 0x6C, 0x24, 0xB8, 0x48, 0x81, 0xEC, 0x48, 0x01, 0x00, 0x00};
// At +0x4EB: lea r9, [rbx + 0x158]; lea r8, [rbp - 0x80]; mov rdx, [rbx + 0x150]; call [rax + 0x48] (the RTV); then the
// sRGB switch on the view's format: sub ecx, 0x1C; je; sub ecx, 0x3B; je; cmp ecx, 1; jne (none).
constexpr uintptr_t SWITCH_OFFSET = 0x4EB;
constexpr std::array<uint8_t, 39> SWITCH = {0x4C, 0x8D, 0x8B, 0x58, 0x01, 0x00, 0x00, 0x4C, 0x8D, 0x45, 0x80, 0x48, 0x8B,
                                            0x93, 0x50, 0x01, 0x00, 0x00, 0xFF, 0x50, 0x48, 0x8B, 0x4D, 0x80, 0x83, 0xE9,
                                            0x1C, 0x74, 0x1C, 0x83, 0xE9, 0x3B, 0x74, 0x0E, 0x83, 0xF9, 0x01, 0x75, 0x38};

static Resize original_resize = nullptr;
static std::vector<renodx::utils::detour::Function> hooks;
static tw::native_hooks::Lifetime hook_lifetime;
// The game's own format: its last 8-bit request (the start's B8G8R8A8 until one is seen).
static std::atomic<uint32_t> game_format = DXGI_FORMAT_B8G8R8A8_UNORM;
static std::atomic_bool logged = false;

inline uint64_t __fastcall OnResize(void* renderer, uint32_t width, uint32_t height, uint32_t format, uint32_t fifth, uint32_t sixth) {
  switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
      game_format.store(format, std::memory_order_relaxed);
      break;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
      if (!logged.exchange(true, std::memory_order_relaxed)) {
        renodx::utils::log::i(std::format("[Rome Remastered] Swap chain rebuilt at {}x{}: the game's format {} for the upgraded {}.", width,
                                          height, game_format.load(std::memory_order_relaxed), format));
      }
      format = game_format.load(std::memory_order_relaxed);
      break;
    default:
      break;
  }
  return original_resize(renderer, width, height, format, fifth, sixth);
}

// The game's Renderer::Resize in `module` (the exe mapped as an image), or null: not this build.
inline uint8_t* Find(HMODULE module) {
  auto* base = reinterpret_cast<uint8_t*>(module);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage < RESIZE_RVA + SWITCH_OFFSET + SWITCH.size()) return nullptr;
  uint8_t* resize = base + RESIZE_RVA;
  if (std::memcmp(resize, PROLOGUE.data(), PROLOGUE.size()) != 0 || std::memcmp(resize + SWITCH_OFFSET, SWITCH.data(), SWITCH.size()) != 0) {
    return nullptr;
  }
  return resize;
}

// `resize` (the game's, or a test's function of its kind) hooked; false: the hook failed (logged).
inline bool Hook(void* resize) {
  original_resize = reinterpret_cast<Resize>(resize);
  hooks = {renodx::utils::detour::Function(&original_resize, &OnResize, renodx::utils::detour::ThreadPolicy::CALLER_GUARANTEES_QUIESCENCE)};
  try {
    hook_lifetime.Install(hooks);
  } catch (const std::exception& error) {
    hooks.clear();
    renodx::utils::log::w(std::format("[Rome Remastered] Swap chain format hook failed: {}", error.what()));
    return false;
  }
  return true;
}

}  // namespace internal

// At the addon's start (in the game's D3D11CreateDevice, before its swap chain: nothing else runs the function then).
inline void Install() {
  if (!internal::hooks.empty()) return;
  uint8_t* resize = internal::Find(GetModuleHandleW(nullptr));
  if (resize == nullptr) {
    renodx::utils::log::w("[Rome Remastered] Swap chain format: the game's resize code was not found (another build): left alone.");
    return;
  }
  if (internal::Hook(resize)) renodx::utils::log::i("[Rome Remastered] Swap chain format: the game's resize hooked.");
}

inline void Uninstall() {
  try {
    internal::hook_lifetime.Uninstall(&internal::hooks);
  } catch (const std::exception& error) {
    renodx::utils::log::w(std::format("[Rome Remastered] Swap chain format unhook failed: {}", error.what()));
  }
}

}  // namespace twromeremastered::swap_chain_format
