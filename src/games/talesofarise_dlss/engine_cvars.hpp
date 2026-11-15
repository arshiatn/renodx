/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include "./code_patch.hpp"

// Unreal 4's console variables by name, through the engine's own lookup (Tales of Arise's UE 4.21, Expeditions: Rome's
// UE 4.23; each exe's slots in its Engine table). Every IConsoleManager::Get().FindConsoleVariable(TEXT("...")) site
// loads the manager's singleton and calls its lookup slot:
//   4.21: mov rcx, [rip + singleton]; mov rax, [rcx]; lea rdx, [rip + name]; call [rax + 0x90]
//   4.23: ... lea rdx, [rip + name]; mov r8b, bTrackFrequentCalls; call [rax + 0xA0]
// A TConsoleVariable<int32 / float> keeps its {game-thread, render-thread} values at the offset its AsVariableInt /
// AsVariableFloat return: lea rax, [rcx + offset]; ret. A FConsoleVariableRef<int32 / float> (a C++ global the engine
// reads, e.g. r.ForceLOD) keeps {T& RefValue, T MainValue}, MainValue at the offset its GetInt / GetFloat load:
// mov eax, [rcx + offset] / movss xmm0, [rcx + offset]; ret; a bool one's GetInt: movzx eax, byte ptr [rcx + offset];
// ret. Values are read and written there directly, as r.ScreenPercentage (engine_scale.hpp): settings the engine reads
// every frame apply live, no change callback runs.
namespace talesofarise::engine_cvars {

// One engine version's layout, read from its exe (never copied between versions).
struct Engine {
  std::string_view lookup_pattern;         // a FindConsoleVariable call site (code_patch::FindPatterns)
  uint32_t find_console_variable = 0;      // IConsoleManager's vtable, bytes
  bool track_frequent_calls = false;       // FindConsoleVariable's third argument (bool bTrackFrequentCalls)
  uint32_t call_all_sinks = 0;             // FConsoleManager::CallAllConsoleVariableSinks in the vtable, bytes
  uint32_t as_variable_int = 0;            // IConsoleVariable vtable slots
  uint32_t as_variable_float = 0;
  uint32_t ref_get_int = 0;
  uint32_t ref_get_float = 0;
  // Refs whose global is in another loaded module, and bool Refs: Arise-SDK's sdk.* settings (Tales). Else the global
  // must be in the exe and bool Refs are not used.
  bool module_refs = false;
};

// UE 4.21 (Tales of Arise).
inline constexpr Engine UE_4_21 = {
    .lookup_pattern = "48 8B 0D ?? ?? ?? ?? 48 8B 01 48 8D 15 ?? ?? ?? ?? FF 90 90 00 00 00",
    .find_console_variable = 0x90,
    .track_frequent_calls = false,
    .call_all_sinks = 0x38,
    .as_variable_int = 7,
    .as_variable_float = 8,
    .ref_get_int = 13,
    .ref_get_float = 14,
    .module_refs = true,
};

// UE 4.23 (Expeditions: Rome; IsVariableFloat / IsVariableString added before the getters, no bool getters).
inline constexpr Engine UE_4_23 = {
    .lookup_pattern = "48 8B 0D ?? ?? ?? ?? 48 8B 01 48 8D 15 ?? ?? ?? ?? 41 B0 ?? FF 90 A0 00 00 00",
    .find_console_variable = 0xA0,
    .track_frequent_calls = true,
    .call_all_sinks = 0x40,
    .as_variable_int = 9,
    .as_variable_float = 10,
    .ref_get_int = 15,
    .ref_get_float = 16,
    .module_refs = false,
};

// One int, float or bool console variable's two copies: {game thread, render thread}, or a Ref's {MainValue, the
// global}.
struct Cvar {
  std::array<void*, 2> values = {};  // null: not found, or not an int / float / bool variable
  bool is_int = false;
  bool is_bool = false;  // a bool Ref (0 / 1)

  float Get() const {
    if (is_bool) return (std::atomic_ref(*static_cast<uint8_t*>(values[0])).load(std::memory_order_relaxed) != 0 ? 1.f : 0.f);
    if (is_int) return static_cast<float>(std::atomic_ref(*static_cast<int32_t*>(values[0])).load(std::memory_order_relaxed));
    return std::atomic_ref(*static_cast<float*>(values[0])).load(std::memory_order_relaxed);
  }

  void Set(float value) const {
    for (void* copy : values) {
      if (is_bool) {
        std::atomic_ref(*static_cast<uint8_t*>(copy)).store((value >= 0.5f ? 1 : 0), std::memory_order_relaxed);
      } else if (is_int) {
        std::atomic_ref(*static_cast<int32_t*>(copy)).store(static_cast<int32_t>(std::lround(value)), std::memory_order_relaxed);
      } else {
        std::atomic_ref(*static_cast<float*>(copy)).store(value, std::memory_order_relaxed);
      }
    }
  }
};

class Console {
 public:
  // Finds the manager from its lookup sites, then checks the lookup and the value layout on r.ScreenPercentage.
  bool Start(const Engine& engine, std::string* details) {
    engine_ = &engine;
    const auto sites = code_patch::FindPatterns(engine.lookup_pattern);
    uintptr_t singleton = 0;
    for (const uint8_t* site : sites) {
      int32_t displacement = 0;
      std::memcpy(&displacement, site + 3, sizeof(displacement));
      const uintptr_t target = reinterpret_cast<uintptr_t>(site) + 7 + displacement;
      if (singleton != 0 && target != singleton) {
        *details = std::format("{} lookups disagree on the console manager", sites.size());
        return false;
      }
      singleton = target;
    }
    uintptr_t vtable = 0;
    if (sites.size() < 16 || !Read(singleton, &manager_) || manager_ == 0 || !Read(manager_, &vtable) || !InImage(vtable)
        || !Read(vtable + engine.find_console_variable, &find_) || !InImage(find_)) {
      *details = std::format("console manager not found ({} lookups)", sites.size());
      manager_ = 0;
      return false;
    }
    std::string check;
    const Cvar screen_percentage = Find(L"r.ScreenPercentage", &check);
    if (screen_percentage.values[0] == nullptr || screen_percentage.is_int || !(screen_percentage.Get() >= 1.f)
        || screen_percentage.Get() > 400.f) {
      *details = "unexpected console layout (r.ScreenPercentage: " + check + ")";
      manager_ = 0;
      return false;
    }
    // FConsoleManager::CallAllConsoleVariableSinks (called every frame on the game thread) starts with
    // cmp byte ptr [rcx + flag], 0: it runs the sinks only while the flag is set.
    uintptr_t call_sinks = 0;
    std::array<uint8_t, 16> sinks_code = {};
    if (Read(vtable + engine.call_all_sinks, &call_sinks) && InImage(call_sinks) && Read(call_sinks, &sinks_code)) {
      for (size_t at = 0; at + 7 <= sinks_code.size(); ++at) {
        if (sinks_code[at] != 0x80 || sinks_code[at + 1] != 0xB9 || sinks_code[at + 6] != 0x00) continue;
        int32_t offset = 0;
        std::memcpy(&offset, sinks_code.data() + at + 2, sizeof(offset));
        if (offset >= 0x40 && offset < 0x1000) sinks_flag_ = manager_ + offset;
        break;
      }
    }
    *details = std::format("console manager at exe+0x{:X} ({} lookups); r.ScreenPercentage {}; {}", singleton - ImageBase(),
                           sites.size(), check,
                           (sinks_flag_ != 0 ? std::format("settings refresh flag at +0x{:X}", sinks_flag_ - manager_)
                                              : std::string("settings refresh not found: shadow resolution, draw distance, detail and anisotropy apply when the game changes a setting")));
    return true;
  }

  // Unreal keeps a copy of some settings (shadow resolutions, draw and object detail distance, detail mode,
  // anisotropy: its cached scalability values) that it refreshes in a sink. Direct writes do not run it: this raises
  // the flag setting a variable through the console raises, so the game thread refreshes the copy on its next frame.
  void Refresh() const {
    if (sinks_flag_ != 0) std::atomic_ref(*reinterpret_cast<bool*>(sinks_flag_)).store(true, std::memory_order_release);
  }

  // `details`: int / float and its value, or why it is unusable.
  Cvar Find(const wchar_t* name, std::string* details) const {
    if (manager_ == 0) {
      *details = "no console";
      return {};
    }
    const Engine& engine = *engine_;
    uintptr_t object = 0;
    if (engine.track_frequent_calls) {
      using FindConsoleVariable = void* (*)(uintptr_t manager, const wchar_t* name, bool track_frequent_calls);
      object = reinterpret_cast<uintptr_t>(reinterpret_cast<FindConsoleVariable>(find_)(manager_, name, false));
    } else {
      using FindConsoleVariable = void* (*)(uintptr_t manager, const wchar_t* name);
      object = reinterpret_cast<uintptr_t>(reinterpret_cast<FindConsoleVariable>(find_)(manager_, name));
    }
    uintptr_t vtable = 0;
    // The vtable up to the last getter read.
    std::array<uintptr_t, 17> slots = {};
    const size_t slot_bytes = (engine.ref_get_float + 1) * sizeof(uintptr_t);
    if (object == 0 || !Read(object, &vtable) || !InImage(vtable) || slot_bytes > sizeof(slots) || !Read(vtable, slots.data(), slot_bytes)) {
      *details = "not found";
      return {};
    }
    // TConsoleVariable: both values at the AsVariable* offset.
    for (const bool is_int : {false, true}) {
      if (const auto offset = Offset(slots[is_int ? engine.as_variable_int : engine.as_variable_float], {0x48, 0x8D, 0x41}, {0x48, 0x8D, 0x81})) {
        const auto values = reinterpret_cast<int32_t*>(object + *offset);
        const Cvar cvar = {.values = {values, values + 1}, .is_int = is_int};
        *details = std::format("{} {:g}", (is_int ? "int" : "float"), cvar.Get());
        return cvar;
      }
    }
    // FConsoleVariableRef: MainValue at the Get* offset, the global it mirrors (in the exe, or another module) right
    // before it.
    const auto in_place = [&engine](uintptr_t global) {
      return (engine.module_refs ? InModule(global) : InImage(global));
    };
    for (const bool is_int : {false, true}) {
      const auto offset = (is_int ? Offset(slots[engine.ref_get_int], {0x8B, 0x41}, {0x8B, 0x81})
                                  : Offset(slots[engine.ref_get_float], {0xF3, 0x0F, 0x10, 0x41}, {0xF3, 0x0F, 0x10, 0x81}));
      uintptr_t global = 0;
      std::array<uint32_t, 2> bits = {};
      if (!offset || *offset < 8 || !Read(object + *offset - 8, &global) || !in_place(global) || !Read(global, &bits[0])
          || !Read(object + *offset, &bits[1]) || bits[0] != bits[1]) {
        continue;
      }
      const Cvar cvar = {.values = {reinterpret_cast<void*>(object + *offset), reinterpret_cast<void*>(global)}, .is_int = is_int};
      *details = std::format("{} {:g} (ref)", (is_int ? "int" : "float"), cvar.Get());
      return cvar;
    }
    // A bool FConsoleVariableRef (Arise-SDK's on / off settings): the same, one byte.
    if (engine.module_refs) {
      if (const auto offset = Offset(slots[engine.ref_get_int], {0x0F, 0xB6, 0x41}, {0x0F, 0xB6, 0x81})) {
        uintptr_t global = 0;
        std::array<uint8_t, 2> bytes = {};
        if (*offset >= 8 && Read(object + *offset - 8, &global) && InModule(global) && Read(global, &bytes[0])
            && Read(object + *offset, &bytes[1]) && bytes[0] == bytes[1] && bytes[0] <= 1) {
          const Cvar cvar = {.values = {reinterpret_cast<void*>(object + *offset), reinterpret_cast<void*>(global)}, .is_bool = true};
          *details = std::format("bool {:g} (ref)", cvar.Get());
          return cvar;
        }
      }
    }
    // For support: the getters' first bytes.
    *details = (engine.module_refs ? "not an int / float / bool variable; getters" : "not an int / float variable; getters");
    for (const size_t slot : {engine.as_variable_int, engine.as_variable_float, engine.ref_get_int, engine.ref_get_float}) {
      std::array<uint8_t, 8> code = {};
      if (!InImage(slots[slot]) || !Read(slots[slot], &code)) continue;
      *details += std::format(" [{}]", slot);
      for (const uint8_t byte : code) *details += std::format(" {:02X}", byte);
    }
    return {};
  }

 private:
  const Engine* engine_ = &UE_4_21;
  uintptr_t manager_ = 0;
  uintptr_t find_ = 0;
  uintptr_t sinks_flag_ = 0;  // FConsoleManager's bCallAllConsoleVariableSinks; 0: not found

  static bool Read(uintptr_t address, void* destination, size_t size) {
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), destination, size, &read) && read == size;
  }

  template <typename T>
  static bool Read(uintptr_t address, T* destination) {
    return Read(address, destination, sizeof(T));
  }

  static uintptr_t ImageBase() {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  }

  static bool InImage(uintptr_t address) {
    const uintptr_t base = ImageBase();
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    return address >= base && address < base + nt->OptionalHeader.SizeOfImage;
  }

  // Inside a loaded module's image (the exe's globals, or another mod's DLL).
  static bool InModule(uintptr_t address) {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(address), &module)
           && module != nullptr;
  }

  // The offset of a getter that is only `op [rcx + offset]; ret`: `short_op` with a disp8, `long_op` with a disp32.
  // Any other code (the base class's null, a conversion, a bool) is not one.
  static std::optional<uint32_t> Offset(uintptr_t getter, std::initializer_list<uint8_t> short_op,
                                        std::initializer_list<uint8_t> long_op) {
    std::array<uint8_t, 12> code = {};
    if (!InImage(getter) || !Read(getter, &code)) return std::nullopt;
    const size_t size = short_op.size();
    if (std::equal(short_op.begin(), short_op.end(), code.begin()) && code[size + 1] == 0xC3 && code[size] < 0x80) {
      return code[size];
    }
    if (!std::equal(long_op.begin(), long_op.end(), code.begin()) || code[size + 4] != 0xC3) return std::nullopt;
    int32_t offset = 0;
    std::memcpy(&offset, code.data() + size, sizeof(offset));
    if (offset <= 0 || offset > 4096) return std::nullopt;
    return static_cast<uint32_t>(offset);
  }
};

}  // namespace talesofarise::engine_cvars
