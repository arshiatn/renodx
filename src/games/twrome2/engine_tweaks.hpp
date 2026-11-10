/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <include/reshade.hpp>

#include "../../utils/detour.hpp"
#include "../../utils/log.hpp"
#include "../../utils/settings.hpp"
#include "../twattila/native_hook_lifetime.hpp"

// Graphics (Experimental) for the Warscape games (Rome II, Attila, Pharaoh, Warhammer II): engine values scaled by sliders.
// - Engine tweaks: named values the game keeps in static objects, found by their names. Each object is built by a
//   static initializer:
//     x86 (empire.retail.dll): push name; mov ecx, object; call constructor. The value is at object + 0x48.
//     x64 (Pharaoh.exe, Warhammer2.exe): lea rcx / rbx / rsi / rdi, [object]; ...; lea rdx, [name]; ...; call constructor;
//     movss [object + 0x60], xmm0 (the default). Warhammer: the object after the name, its value at +0x68. Warhammer
//     III: lea r8, [name]; ...; lea rcx, [object]; ...; call constructor (the value at +0x60).
// - Code constants: an immediate in the game's code, found by a byte pattern.
// A slider scales the game's value; when the game sets it again (its graphics settings), that is the new base.
namespace tw::engine_tweaks {

struct Tweak {
  std::string_view name;          // an engine tweak, found by its name
  std::string_view code_pattern;  // or a constant in the code: a byte pattern ("C7 87 ?? ..."), the value at code_offset
  size_t code_offset = 0;
  bool integer = false;   // int32 instead of float
  bool flag = false;      // a bool (1 byte): set while the slider is at 1 or more, else the game's
  bool three_way = false;  // a flag chosen by its slider: 0 the game's, 1 off, 2 on
  bool inverse = false;   // divided by the slider: a factor on the camera distance
  bool absolute = false;  // set to the slider's value (for a game value of 0, which no factor changes), except at keep
  float keep = 0.f;       // absolute: the slider value that leaves the game's
  void* value = nullptr;  // found by Find()
  float game = 0.f;       // the game's value
  uint32_t written = 0;   // the bits the addon wrote last
  bool seen = false;
};

// A Graphics slider and the tweaks it scales (1: the game's values).
struct Option {
  float scale = 1.f;
  std::vector<Tweak> tweaks;
  float pixels = 1.f;  // .inverse tweaks are counts of pixels: the render's pixels per pixel of the display, for a game drawing fewer
};

struct Sections {
  std::vector<std::string_view> code;
  std::vector<std::string_view> data;
};

inline Sections ModuleSections(HMODULE module) {
  auto* base = reinterpret_cast<uint8_t*>(module);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  Sections sections;
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0 || (section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE) != 0) continue;
    // A protection's payload (Warhammer II: a 430 MB section), not the game's code or names: not scanned. (Warhammer's
    // own code is a 300 MB section.)
    if (section->Misc.VirtualSize > 384u * 1024u * 1024u) continue;
    const std::string_view memory(reinterpret_cast<const char*>(base + section->VirtualAddress), section->Misc.VirtualSize);
    if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) {
      sections.code.push_back(memory);
    } else {
      sections.data.push_back(memory);
    }
  }
  return sections;
}

// The first match of a byte pattern ("48 8B ?? ...") in `memories` (a module's code or data sections), or null.
inline uint8_t* FindPattern(const std::vector<std::string_view>& memories, std::string_view pattern) {
  std::vector<int> bytes;
  for (size_t at = 0; at + 1 < pattern.size(); at += 3) {
    bytes.push_back(pattern[at] == '?' ? -1 : std::stoi(std::string(pattern.substr(at, 2)), nullptr, 16));
  }
  for (const std::string_view memory : memories) {
    const auto* begin = reinterpret_cast<const uint8_t*>(memory.data());
    for (size_t at = 0; at + bytes.size() <= memory.size(); ++at) {
      size_t matched = 0;
      while (matched < bytes.size() && (bytes[matched] < 0 || begin[at + matched] == bytes[matched])) ++matched;
      if (matched == bytes.size()) return const_cast<uint8_t*>(begin + at);
    }
  }
  return nullptr;
}

namespace internal {

inline uint8_t* RipTarget(uint8_t* displacement, uint8_t* end) {
  int32_t offset;
  std::memcpy(&offset, displacement, sizeof(offset));
  return end + offset;
}

#if defined(_WIN64)
// The value field of the object built around `lea rdx, [name]` at `name_lea`, checked by the default's store:
// movss [rip + value], xmm (float) or mov dword ptr [rip + value], imm32 (int). Warhammer: the object follows the name
// (lea rcx, [object]; ...; call constructor), its value at +0x68 (the getter: movss xmm0, [rcx + 0x68]).
inline void* ObjectValue(uint8_t* name_lea) {
  for (uint8_t* at = name_lea - 7; at >= name_lea - 0x20; --at) {
    if (at[0] != 0x48 || at[1] != 0x8D || (at[2] != 0x0D && at[2] != 0x1D && at[2] != 0x35 && at[2] != 0x3D)) continue;
    uint8_t* value = RipTarget(at + 3, at + 7) + 0x60;
    for (uint8_t* store = name_lea + 7; store < name_lea + 0x60; ++store) {
      const bool float_store = store[0] == 0xF3 && store[1] == 0x0F && store[2] == 0x11 && (store[3] & 0xC7) == 0x05 && RipTarget(store + 4, store + 8) == value;
      const bool int_store = store[0] == 0xC7 && store[1] == 0x05 && RipTarget(store + 2, store + 10) == value;
      const bool flag_store = store[0] == 0xC6 && store[1] == 0x05 && RipTarget(store + 2, store + 7) == value;
      if (float_store || int_store || flag_store) return value;
    }
    break;
  }
  // Warhammer (no object before the name, or another function's lea there).
  for (uint8_t* at = name_lea + 7; at < name_lea + 0x20; ++at) {
    if (at[0] != 0x48 || at[1] != 0x8D || at[2] != 0x0D) continue;
    for (uint8_t* call = at + 7; call < at + 0x18; ++call) {
      if (call[0] == 0xE8) return RipTarget(at + 3, at + 7) + 0x68;
    }
    return nullptr;
  }
  return nullptr;
}

// Warhammer III: the value field of the object built after `lea r8, [name]` at `name_lea`: lea rdx, [rsp + x] (the
// default, passed by address); ...; lea rcx, [object]; ...; call constructor, which stores the default at +0x60.
inline void* FollowingObjectValue(uint8_t* name_lea) {
  bool default_address = false;
  for (uint8_t* at = name_lea + 7; at < name_lea + 0x40; ++at) {
    default_address |= (at[0] == 0x48 && at[1] == 0x8D && at[2] == 0x54 && at[3] == 0x24);
    if (at[0] == 0x48 && at[1] == 0x8D && at[2] == 0x0D) return default_address ? RipTarget(at + 3, at + 7) + 0x60 : nullptr;
  }
  return nullptr;
}
#endif

// Every option's tweaks in the module.
inline void Find(const Sections& sections, std::span<Option*> options) {
  // Code constants: their pattern; the rest by name.
  std::vector<Tweak*> named;
  for (Option* option : options) {
    for (Tweak& tweak : option->tweaks) {
      if (tweak.code_pattern.empty()) {
        named.push_back(&tweak);
      } else if (uint8_t* match = FindPattern(sections.code, tweak.code_pattern); match != nullptr) {
        tweak.value = match + tweak.code_offset;
      }
    }
  }
#if defined(_WIN64)
  // The names: zero-terminated, after another string's terminator, in any readable section (Warhammer keeps them in an
  // executable one). One pass over the sections for all names, then one over every lea rdx, [rip + x] and one over
  // every lea r8, [rip + x] (Warhammer III), each a lookup (a search per name took minutes with 1000+ tests).
  std::unordered_map<std::string_view, std::vector<Tweak*>> by_name;
  std::array<bool, 256> first_letters = {};
  size_t longest = 0;
  for (Tweak* tweak : named) {
    if (tweak->name.empty()) continue;
    by_name[tweak->name].push_back(tweak);
    first_letters[static_cast<uint8_t>(tweak->name.front())] = true;
    longest = std::max(longest, tweak->name.size());
  }
  std::unordered_map<const uint8_t*, std::vector<Tweak*>*> by_address;
  for (const auto* memories : {&sections.data, &sections.code}) {
    for (const std::string_view memory : *memories) {
      for (size_t at = 1; at < memory.size(); ++at) {
        if (memory[at - 1] != '\0' || !first_letters[static_cast<uint8_t>(memory[at])]) continue;
        const std::string_view rest = memory.substr(at, longest + 1);
        const size_t length = rest.find('\0');
        if (length == std::string_view::npos) continue;
        if (const auto found = by_name.find(rest.substr(0, length)); found != by_name.end()) {
          by_address.emplace(reinterpret_cast<const uint8_t*>(memory.data() + at), &found->second);
        }
        at += length;
      }
    }
  }
  constexpr std::string_view LEA_RDX = "\x48\x8D\x15";
  constexpr std::string_view LEA_R8 = "\x4C\x8D\x05";
  for (const std::string_view lea : {LEA_RDX, LEA_R8}) {
    for (const std::string_view memory : sections.code) {
      for (size_t at = memory.find(lea); at != std::string_view::npos && at + 7 <= memory.size(); at = memory.find(lea, at + 1)) {
        auto* instruction = reinterpret_cast<uint8_t*>(const_cast<char*>(memory.data() + at));
        const auto found = by_address.find(RipTarget(instruction + 3, instruction + 7));
        if (found == by_address.end()) continue;
        for (Tweak* tweak : *found->second) {
          if (tweak->value == nullptr) {
            tweak->value = (lea == LEA_RDX) ? ObjectValue(instruction) : FollowingObjectValue(instruction);
          }
        }
      }
    }
  }
#else
  // One pass over the code for every static initializer (push name; mov ecx, object; call constructor), its name read
  // where it points: a search per name took seconds at the first present with 100+ names.
  std::unordered_map<std::string_view, std::vector<Tweak*>> by_name;
  for (Tweak* tweak : named) {
    by_name[tweak->name].push_back(tweak);
  }
  // The zero-terminated name at `address` in a readable section, or empty.
  const auto name_at = [&sections](uint32_t address) {
    for (const auto* memories : {&sections.data, &sections.code}) {
      for (const std::string_view memory : *memories) {
        const auto begin = reinterpret_cast<uintptr_t>(memory.data());
        if (address < begin || address - begin >= memory.size()) continue;
        const std::string_view rest = memory.substr(address - begin, 128);
        const size_t end = rest.find('\0');
        return (end == std::string_view::npos) ? std::string_view() : rest.substr(0, end);
      }
    }
    return std::string_view();
  };
  for (const std::string_view memory : sections.code) {
    for (size_t at = memory.find('\x68'); at != std::string_view::npos && at + 15 <= memory.size(); at = memory.find('\x68', at + 1)) {
      if (memory[at + 5] != '\xB9' || memory[at + 10] != '\xE8') continue;
      uint32_t name_address;
      std::memcpy(&name_address, memory.data() + at + 1, sizeof(name_address));
      const auto found = by_name.find(name_at(name_address));
      if (found == by_name.end()) continue;
      uint32_t object;
      std::memcpy(&object, memory.data() + at + 6, sizeof(object));
      for (Tweak* tweak : found->second) {
        if (tweak->value == nullptr) {
          tweak->value = reinterpret_cast<void*>(object + 0x48);
        }
      }
    }
  }
#endif
}

// The game's values x the sliders.
inline void Apply(std::span<Option*> options) {
  for (Option* option : options) {
    for (Tweak& tweak : option->tweaks) {
      if (tweak.value == nullptr) continue;
      if (tweak.flag) {
        auto* flag = static_cast<uint8_t*>(tweak.value);
        if (!tweak.seen || *flag != tweak.written) {
          tweak.game = *flag;
          tweak.seen = true;
        }
        tweak.written = static_cast<uint32_t>(tweak.game);
        if (tweak.three_way) {
          if (option->scale >= 1.5f) {
            tweak.written = 1u;
          } else if (option->scale >= 0.5f) {
            tweak.written = 0u;
          }
        } else if (option->scale >= 1.f) {
          tweak.written = 1u;
        }
        *flag = static_cast<uint8_t>(tweak.written);
        continue;
      }
      uint32_t bits;
      std::memcpy(&bits, tweak.value, sizeof(bits));
      // Anything but the addon's last value is the game's (its default, or set again by its graphics settings).
      if (!tweak.seen || bits != tweak.written) {
        tweak.game = tweak.integer ? static_cast<float>(std::bit_cast<int32_t>(bits)) : std::bit_cast<float>(bits);
        tweak.seen = true;
      }
      float scaled = tweak.game * option->scale;
      if (tweak.absolute) {
        scaled = (option->scale == tweak.keep) ? tweak.game : option->scale;
      } else if (tweak.inverse) {
        scaled = tweak.game / option->scale * option->pixels;
      }
      tweak.written = tweak.integer ? std::bit_cast<uint32_t>(static_cast<int32_t>(std::lround(scaled))) : std::bit_cast<uint32_t>(scaled);
      if (tweak.written == bits) continue;
      if (tweak.code_pattern.empty()) {
        std::memcpy(tweak.value, &tweak.written, sizeof(bits));
        continue;
      }
      // Code: writable only while it is patched.
      DWORD protection;
      if (VirtualProtect(tweak.value, sizeof(bits), PAGE_EXECUTE_READWRITE, &protection) != FALSE) {
        std::memcpy(tweak.value, &tweak.written, sizeof(bits));
        VirtualProtect(tweak.value, sizeof(bits), protection, &protection);
        FlushInstructionCache(GetCurrentProcess(), tweak.value, sizeof(bits));
      }
    }
  }
}

// Overlay (Advanced): which tweaks were found.
inline std::string Status(std::span<Option*> options) {
  std::string missing;
  int found = 0;
  int total = 0;
  for (const Option* option : options) {
    for (const Tweak& tweak : option->tweaks) {
      ++total;
      if (tweak.value != nullptr) {
        ++found;
      } else {
        missing += std::format("{}{}", missing.empty() ? "; not found: " : ", ", tweak.code_pattern.empty() ? tweak.name : "code constant");
      }
    }
  }
  return std::format("Engine values found: {} of {}{}", found, total, missing);
}

}  // namespace internal

#if !defined(_WIN64)
// Rome II / Attila object detail: Entry* __thiscall PickLod(Model* model, float distance_squared, Entry** next) returns
// the first LOD whose distance squared (entry + 0x38, set when the model loads) is beyond the camera's (the same code
// in both: +0x14f0710 Rome II, +0x183d570 Attila). The hook divides the camera's distance squared by the slider
// squared, live for every model that uses it: trees and other objects (ATN: not battle units, which have their own
// distances). The fade-out at the last LOD's distance (other functions) stays: detail farther, not draw distance.
namespace lod_pick {

namespace internal {

constexpr std::string_view PATTERN =
    "53 55 8B 6C 24 10 56 57 8B F9 C7 45 00 00 00 00 00 8B 47 28 85 C0 0F 84 ?? ?? ?? ?? 83 F8 01 75 ?? 8B 47 2C 8B 00 "
    "83 78 20 00 75 ?? 83 78 2C 00 77 ?? 8B 0D";

// __thiscall through __fastcall: the model in ecx, edx unused, the rest on the stack (callee pops).
using PickLod = void*(__fastcall*)(void* model, void* unused_edx, float distance_squared, void** next);

static PickLod original_pick_lod = nullptr;
static std::vector<renodx::utils::detour::Function> hooks;
static tw::native_hooks::Lifetime hook_lifetime;
static std::atomic<bool> hooks_active = false;
static std::atomic<float> distance_squared_scale = 1.f;

static void* __fastcall OnPickLod(void* model, void* unused_edx, float distance_squared, void** next) {
  if (!hooks_active.load(std::memory_order_relaxed)) return original_pick_lod(model, unused_edx, distance_squared, next);
  return original_pick_lod(model, unused_edx, distance_squared * distance_squared_scale.load(std::memory_order_relaxed), next);
}

inline std::string Install(const tw::engine_tweaks::Sections& sections) {
  if (!hooks.empty()) {
    hooks_active.store(true, std::memory_order_relaxed);
    return "object detail hooked";
  }
  uint8_t* pick_lod = tw::engine_tweaks::FindPattern(sections.code, PATTERN);
  if (pick_lod == nullptr) return "object detail: the game's LOD code was not found";
  original_pick_lod = reinterpret_cast<PickLod>(pick_lod);
  hooks = {renodx::utils::detour::Function(&original_pick_lod, &OnPickLod)};
  try {
    hook_lifetime.Install(hooks);
    hooks_active.store(true, std::memory_order_relaxed);
  } catch (const std::exception& error) {
    hooks.clear();
    return std::format("object detail hook failed: {}", error.what());
  }
  return "object detail hooked";
}

}  // namespace internal

}  // namespace lod_pick
#endif

// Every present: finds the tweaks (and, with object_detail, hooks the LOD pick: Rome II / Attila) once the module
// (null: the game's exe) is loaded, the result in `status`, then applies the sliders.
inline void Update(const wchar_t* module_name, std::span<Option*> options, [[maybe_unused]] const Option* object_detail,
                   bool* found, renodx::utils::settings::Setting* status) {
  if (!*found) {
    HMODULE module = GetModuleHandleW(module_name);
    if (module == nullptr) return;
    const auto sections = ModuleSections(module);
    internal::Find(sections, options);
    status->label = internal::Status(options);
#if !defined(_WIN64)
    if (object_detail != nullptr) status->label += "; " + lod_pick::internal::Install(sections);
#endif
    renodx::utils::log::i("[Total War] Graphics: ", status->label);
    *found = true;
  }
  internal::Apply(options);
#if !defined(_WIN64)
  if (object_detail != nullptr) {
    lod_pick::internal::distance_squared_scale.store(1.f / (object_detail->scale * object_detail->scale), std::memory_order_relaxed);
  }
#endif
}

// Values the game reads once as it starts (Attila's audio memory): `options` found in the module (null: the game's exe)
// and applied as soon as the game's static initializers have set them (all of them not 0); false: not yet. Any thread,
// before Update runs.
inline bool ApplyWhenSet(const wchar_t* module_name, std::span<Option*> options) {
  HMODULE module = GetModuleHandleW(module_name);
  if (module == nullptr) return false;
  internal::Find(ModuleSections(module), options);
  for (const Option* option : options) {
    for (const Tweak& tweak : option->tweaks) {
      if (tweak.value == nullptr) return false;
      uint32_t bits = 0;
      std::memcpy(&bits, tweak.value, sizeof(bits));
      if (bits == 0) return false;
    }
  }
  internal::Apply(options);
  return true;
}

// Graphics trace (a button): every option's engine values to ReShade.log: found or not, where (module + offset), the
// game's value, the addon's last value and the value there now (another than the addon's: the game set it again).
inline void Trace(std::span<Option*> options) {
  const auto value_text = [](const Tweak& tweak, uint32_t bits) {
    if (tweak.flag) return std::to_string(bits & 0xFFu);
    if (tweak.integer) return std::to_string(std::bit_cast<int32_t>(bits));
    return std::format("{:g}", std::bit_cast<float>(bits));
  };
  renodx::utils::log::i("[Total War] Graphics trace:");
  for (size_t index = 0; index < options.size(); ++index) {
    const Option* option = options[index];
    for (const Tweak& tweak : option->tweaks) {
      const std::string_view name = tweak.code_pattern.empty() ? tweak.name : std::string_view("code constant");
      if (tweak.value == nullptr) {
        renodx::utils::log::i(std::format("[Total War]   slider {} ({:g}x) {}: not found", index, option->scale, name));
        continue;
      }
      HMODULE module = nullptr;
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         static_cast<LPCWSTR>(tweak.value), &module);
      uint32_t now = 0;
      std::memcpy(&now, tweak.value, tweak.flag ? 1 : sizeof(now));
      renodx::utils::log::i(std::format(
          "[Total War]   slider {} ({:g}x) {}: +{:#x}, game {:g}, addon {}, now {}{}", index, option->scale, name,
          static_cast<uint8_t*>(tweak.value) - reinterpret_cast<uint8_t*>(module), tweak.game,
          value_text(tweak, tweak.written), value_text(tweak, now), (now == tweak.written) ? "" : " (changed by the game)"));
    }
  }
}

// Graphics watch (buttons): which game code reads up to 4 engine values. For a while, a hardware data breakpoint (any
// access, 4 bytes) on each in every other thread of the game (threads started meanwhile too), a vectored exception
// handler counting the instruction after each access (a getter's: its caller); each new reader to ReShade.log as it is
// found (a crash keeps them), then the counts (this addon's own reads every present included). Nothing runs otherwise.
// Either from a button or, after the other button, from the next game start (the addon's load at the game's device).
namespace watch {

namespace internal {

struct Hit {
  std::atomic<uintptr_t> address = 0;  // the instruction after the access; a getter's caller (the access then ret)
  std::atomic<uintptr_t> getter = 0;   // the getter's ret, or 0
  std::atomic<uint32_t> first_ms = 0;  // the first access, after the watch's start
  std::atomic<uint32_t> count = 0;     // counted after the fields above (a count > 0: they are written)
  std::atomic<uint32_t> values = 0;    // bit i: the i-th watched value
};
static std::array<Hit, 128> hits;
static std::atomic<bool> running = false;
static std::atomic<bool> stopping = false;
static std::atomic<uint64_t> start_tick = 0;

// Every access to a watched value: its instruction counted (no locks or allocations here: any thread, any time).
inline LONG CALLBACK OnException(EXCEPTION_POINTERS* info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  const auto triggered = static_cast<uint32_t>(info->ContextRecord->Dr6 & 0xF);
  if (triggered == 0) return EXCEPTION_CONTINUE_SEARCH;
  info->ContextRecord->Dr6 = 0;
  // Stopping: a thread caught between its access and this handler gets its breakpoints back from this context when it
  // continues, after they were cleared: cleared here too.
  if (stopping.load()) {
    info->ContextRecord->Dr7 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  auto address = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
  uintptr_t getter = 0;
  if (*reinterpret_cast<const uint8_t*>(address) == 0xC3) {
    getter = address;
#if defined(_WIN64)
    address = *reinterpret_cast<const uintptr_t*>(info->ContextRecord->Rsp);
#else
    address = *reinterpret_cast<const uintptr_t*>(info->ContextRecord->Esp);
#endif
  }
  for (Hit& hit : hits) {
    // This instruction's slot, or the first free one (claimed; another thread may claim it first).
    uintptr_t current = hit.address.load();
    if (current == 0 && hit.address.compare_exchange_strong(current, address)) {
      hit.getter.store(getter);
      hit.first_ms.store(static_cast<uint32_t>(GetTickCount64() - start_tick.load()));
    } else if (current != address) {
      continue;
    }
    hit.values.fetch_or(triggered);
    hit.count.fetch_add(1);
    break;
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

// Every other thread of the game not in `done` (then added): the watched values (`count` of them) as its data
// breakpoints, or none.
inline void SetBreakpoints(const std::array<uintptr_t, 4>& addresses, size_t count, std::vector<DWORD>* done) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return;
  THREADENTRY32 entry = {.dwSize = sizeof(THREADENTRY32)};
  for (BOOL more = Thread32First(snapshot, &entry); more != FALSE; more = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) continue;
    if (std::ranges::find(*done, entry.th32ThreadID) != done->end()) continue;
    done->push_back(entry.th32ThreadID);
    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
    if (thread == nullptr) continue;
    if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
      CONTEXT context = {.ContextFlags = CONTEXT_DEBUG_REGISTERS};
      if (GetThreadContext(thread, &context) != FALSE) {
        context.Dr0 = addresses[0];
        context.Dr1 = addresses[1];
        context.Dr2 = addresses[2];
        context.Dr3 = addresses[3];
        context.Dr6 = 0;
        context.Dr7 = 0;
        // Each: locally enabled, any access (0b11), 4 bytes (0b11).
        for (size_t index = 0; index < count; ++index) {
          context.Dr7 |= (1ull << (index * 2)) | (0b11ull << (16 + index * 4)) | (0b11ull << (18 + index * 4));
        }
        SetThreadContext(thread, &context);
      }
      ResumeThread(thread);
    }
    CloseHandle(thread);
  }
  CloseHandle(snapshot);
}

}  // namespace internal

// Watches the tweaks named `names` (the first 4 found among `options`) for `seconds`, on a thread of its own.
inline void Start(std::span<Option*> options, std::span<const std::string_view> names, int seconds) {
  if (internal::running.exchange(true)) return;
  std::array<uintptr_t, 4> addresses = {};
  std::array<std::string_view, 4> watched = {};
  size_t count = 0;
  for (const std::string_view name : names) {
    for (const Option* option : options) {
      for (const Tweak& tweak : option->tweaks) {
        if (count < addresses.size() && tweak.name == name && tweak.value != nullptr) {
          addresses[count] = reinterpret_cast<uintptr_t>(tweak.value);
          watched[count] = tweak.name;
          ++count;
        }
      }
    }
  }
  if (count == 0) {
    renodx::utils::log::i("[Total War] Graphics watch: none of the values found.");
    internal::running = false;
    return;
  }
  for (internal::Hit& hit : internal::hits) {
    hit.address = 0;
    hit.getter = 0;
    hit.first_ms = 0;
    hit.count = 0;
    hit.values = 0;
  }
  std::thread([addresses, watched, count, seconds]() {
    const auto module_offset = [](uintptr_t address) {
      HMODULE module = nullptr;
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(address), &module);
      std::array<char, MAX_PATH> path = {};
      GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
      const std::string_view file = std::string_view(path.data()).substr(std::string_view(path.data()).find_last_of("\\/") + 1);
      return std::format("{}+{:#x}", file, address - reinterpret_cast<uintptr_t>(module));
    };
    const auto describe = [&](const internal::Hit& hit) {
      std::string text = module_offset(hit.address.load());
      if (const uintptr_t getter = hit.getter.load(); getter != 0) {
        text += std::format(" (the caller of the getter {})", module_offset(getter));
      } else {
        text += " (the instruction after it)";
      }
      for (size_t index = 0; index < count; ++index) {
        if ((hit.values.load() & (1u << index)) != 0) text += std::format(" {}", watched[index]);
      }
      return text;
    };
    std::string names_text;
    for (size_t index = 0; index < count; ++index) names_text += std::format(" {}", watched[index]);
    renodx::utils::log::i(std::format("[Total War] Graphics watch: {} s:{}", seconds, names_text));
    internal::stopping = false;
    internal::start_tick = GetTickCount64();
    void* handler = AddVectoredExceptionHandler(1, internal::OnException);
    std::vector<DWORD> armed;
    std::array<bool, internal::hits.size()> logged = {};
    for (int tick = 0; tick < seconds * 4; ++tick) {
      internal::SetBreakpoints(addresses, count, &armed);
      Sleep(250);
      for (size_t index = 0; index < internal::hits.size(); ++index) {
        const internal::Hit& hit = internal::hits[index];
        if (logged[index] || hit.count.load() == 0) continue;
        logged[index] = true;
        renodx::utils::log::i(std::format("[Total War]   new reader at {:.1f} s: {}", hit.first_ms.load() / 1000.f, describe(hit)));
      }
    }
    internal::stopping = true;
    std::vector<DWORD> cleared;
    internal::SetBreakpoints({}, 0, &cleared);
    Sleep(500);
    cleared.clear();
    internal::SetBreakpoints({}, 0, &cleared);
    Sleep(100);
    RemoveVectoredExceptionHandler(handler);
    for (const internal::Hit& hit : internal::hits) {
      if (hit.count.load() == 0) continue;
      renodx::utils::log::i(std::format("[Total War]   {}: {} accesses from {:.1f} s", describe(hit), hit.count.load(), hit.first_ms.load() / 1000.f));
    }
    renodx::utils::log::i("[Total War] Graphics watch: done");
    internal::running = false;
  }).detach();
}

// The button for the next start: ReShade.ini [Total War] WatchAtStart.
inline void RequestAtStart() {
  reshade::set_config_value(nullptr, "Total War", "WatchAtStart", true);
  renodx::utils::log::i("[Total War] Graphics watch: at the next game start");
}

// The addon's start: the watch requested by RequestAtStart (once), the values found right away.
inline void StartIfRequested(std::span<Option*> options, std::span<const std::string_view> names, int seconds) {
  bool requested = false;
  reshade::get_config_value(nullptr, "Total War", "WatchAtStart", requested);
  if (!requested) return;
  reshade::set_config_value(nullptr, "Total War", "WatchAtStart", false);
  HMODULE module = GetModuleHandleW(nullptr);
  tw::engine_tweaks::internal::Find(ModuleSections(module), options);
  Start(options, names, seconds);
}

}  // namespace watch

// Addon detach: the LOD pick hook removed (the scaled values stay until the game sets them again).
inline void Uninstall() {
#if !defined(_WIN64)
  lod_pick::internal::hooks_active.store(false, std::memory_order_relaxed);
  if (lod_pick::internal::hooks.empty()) return;
  try {
    lod_pick::internal::hook_lifetime.Uninstall(&lod_pick::internal::hooks);
  } catch (const std::exception& error) {
    renodx::utils::log::e("[Total War] LOD hook could not be removed; addon retained until process exit: ", error.what());
  }
#endif
}

// The yellow note at the top of Graphics (Experimental).
inline renodx::utils::settings::Setting* Note() {
  return new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "Experimental: these may not work, and may crash the game.",
      .section = "Graphics (Experimental)",
      .tint = 0xFFD23C,
  };
}

// The Graphics (Experimental) status line, filled by Update: shown only when something was not found or failed (the
// full result is in ReShade.log).
inline renodx::utils::settings::Setting* StatusLine() {
  auto* status = new renodx::utils::settings::Setting{
      .value_type = renodx::utils::settings::SettingValueType::TEXT,
      .label = "Engine values: not searched yet",
      .section = "Graphics (Experimental)",
  };
  status->is_visible = [status] {
    return status->label.find("not found") != std::string::npos || status->label.find("failed") != std::string::npos;
  };
  return status;
}

// A Graphics (Experimental) slider: `min` to `max`, 1x the game's.
inline renodx::utils::settings::Setting* Slider(Option* option, const std::string& key, const std::string& label,
                                                 const std::string& tooltip, float max, float min = 1.f) {
  return new renodx::utils::settings::Setting{
      .key = key,
      .binding = &option->scale,
      .default_value = 1.f,
      .label = label,
      .section = "Graphics (Experimental)",
      .tooltip = tooltip,
      .min = min,
      .max = max,
      .format = "%.2fx",
  };
}

}  // namespace tw::engine_tweaks
