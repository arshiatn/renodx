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
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>

// The game's (UE 4.26) console variables by name, through the engine's own lookup. Every
// IConsoleManager::Get().FindConsoleVariable(TEXT("..."), bTrackFrequentCalls) site loads the manager's singleton
// and calls its slot 0xA0:
//   mov rcx, [rip + singleton]; mov rax, [rcx]; lea rdx, [rip + name]; mov r8b, track; call [rax + 0xA0]
// A TConsoleVariable<int32 / float> keeps its {game-thread, render-thread} values at the offset its AsVariableInt /
// AsVariableFloat (slots 9 / 10) return: lea rax, [rcx + offset]; ret. A FConsoleVariableRef<int32 / float> (a C++
// global the engine reads) keeps {T& RefValue, T MainValue}, MainValue at the offset its GetInt / GetFloat (slots
// 15 / 16) load: mov eax, [rcx + offset] / movss xmm0, [rcx + offset]; ret. (Slots read from the exe: Tales of
// Arise's UE 4.21 has two fewer.) Values are read and written there directly, as r.ScreenPercentage
// (engine_scale.hpp): settings the engine reads every frame apply live, no change callback runs.
namespace expeditionsrome::engine_cvars {

// One int or float console variable's two copies: {game thread, render thread}, or a Ref's {MainValue, the global}.
struct Cvar {
  std::array<void*, 2> values = {};  // null: not found, or not an int / float variable
  bool is_int = false;

  float Get() const {
    if (is_int) return static_cast<float>(std::atomic_ref(*static_cast<int32_t*>(values[0])).load(std::memory_order_relaxed));
    return std::atomic_ref(*static_cast<float*>(values[0])).load(std::memory_order_relaxed);
  }

  void Set(float value) const {
    for (void* copy : values) {
      if (is_int) {
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
  bool Start(std::string* details) {
    // 48 8B 0D <singleton> 48 8B 01 48 8D 15 <name> 41 B0 <track> FF 90 A0 00 00 00, searched by its call.
    constexpr std::array<uint8_t, 6> CALL = {0xFF, 0x90, 0xA0, 0x00, 0x00, 0x00};
    constexpr std::array<uint8_t, 6> LOAD_VTABLE_AND_NAME = {0x48, 0x8B, 0x01, 0x48, 0x8D, 0x15};
    constexpr size_t CALL_AT = 20;
    const std::boyer_moore_horspool_searcher searcher(CALL.begin(), CALL.end());
    const uintptr_t base = ImageBase();
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    constexpr DWORD EXECUTABLE = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    size_t sites = 0;
    uintptr_t singleton = 0;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
      if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
      const auto* begin = reinterpret_cast<const uint8_t*>(base + section->VirtualAddress);
      const uint8_t* end = begin + (section->Misc.VirtualSize != 0 ? section->Misc.VirtualSize : section->SizeOfRawData);
      bool readable = true;
      for (const uint8_t* at = begin; readable && at < end;) {
        MEMORY_BASIC_INFORMATION info = {};
        readable = VirtualQuery(at, &info, sizeof(info)) != 0 && info.State == MEM_COMMIT && (info.Protect & EXECUTABLE) != 0
                   && (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
        at = static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize;
      }
      if (!readable || end - begin < static_cast<ptrdiff_t>(CALL_AT)) continue;
      for (auto hit = std::search(begin + CALL_AT, end, searcher); hit != end; hit = std::search(hit + 1, end, searcher)) {
        const uint8_t* site = hit - CALL_AT;
        if (site[0] != 0x48 || site[1] != 0x8B || site[2] != 0x0D || site[17] != 0x41 || site[18] != 0xB0
            || !std::equal(LOAD_VTABLE_AND_NAME.begin(), LOAD_VTABLE_AND_NAME.end(), site + 7)) {
          continue;
        }
        int32_t displacement = 0;
        std::memcpy(&displacement, site + 3, sizeof(displacement));
        const uintptr_t target = reinterpret_cast<uintptr_t>(site) + 7 + displacement;
        ++sites;
        if (singleton != 0 && target != singleton) {
          *details = std::format("lookups disagree on the console manager (after {})", sites);
          return false;
        }
        singleton = target;
      }
    }
    uintptr_t vtable = 0;
    if (sites < 16 || !Read(singleton, &manager_) || manager_ == 0 || !Read(manager_, &vtable) || !InImage(vtable)
        || !Read(vtable + 0xA0, &find_) || !InImage(find_)) {
      *details = std::format("console manager not found ({} lookups)", sites);
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
    // FConsoleManager::CallAllConsoleVariableSinks (slot 8, called every frame on the game thread) starts with
    // cmp byte ptr [rcx + flag], 0: it runs the sinks only while the flag is set.
    uintptr_t call_sinks = 0;
    std::array<uint8_t, 16> sinks_code = {};
    if (Read(vtable + 0x40, &call_sinks) && InImage(call_sinks) && Read(call_sinks, &sinks_code)) {
      for (size_t at = 0; at + 7 <= sinks_code.size(); ++at) {
        if (sinks_code[at] != 0x80 || sinks_code[at + 1] != 0xB9 || sinks_code[at + 6] != 0x00) continue;
        int32_t offset = 0;
        std::memcpy(&offset, sinks_code.data() + at + 2, sizeof(offset));
        if (offset >= 0x40 && offset < 0x1000) sinks_flag_ = manager_ + offset;
        break;
      }
    }
    *details = std::format("console manager at exe+0x{:X} ({} lookups); r.ScreenPercentage {}; {}", singleton - base, sites, check,
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
    using FindConsoleVariable = void* (*)(uintptr_t manager, const wchar_t* name, bool track_frequent_calls);
    const auto object = reinterpret_cast<uintptr_t>(reinterpret_cast<FindConsoleVariable>(find_)(manager_, name, false));
    uintptr_t vtable = 0;
    std::array<uintptr_t, 17> slots = {};
    if (object == 0 || !Read(object, &vtable) || !InImage(vtable) || !Read(vtable, &slots)) {
      *details = "not found";
      return {};
    }
    // TConsoleVariable: both values at the AsVariable* offset.
    for (const bool is_int : {false, true}) {
      if (const auto offset = Offset(slots[is_int ? 9 : 10], {0x48, 0x8D, 0x41}, {0x48, 0x8D, 0x81})) {
        const auto values = reinterpret_cast<int32_t*>(object + *offset);
        const Cvar cvar = {.values = {values, values + 1}, .is_int = is_int};
        *details = std::format("{} {:g}", (is_int ? "int" : "float"), cvar.Get());
        return cvar;
      }
    }
    // FConsoleVariableRef: MainValue at the Get* offset, the global it mirrors (in the exe) right before it.
    for (const bool is_int : {false, true}) {
      const auto offset = (is_int ? Offset(slots[15], {0x8B, 0x41}, {0x8B, 0x81})
                                  : Offset(slots[16], {0xF3, 0x0F, 0x10, 0x41}, {0xF3, 0x0F, 0x10, 0x81}));
      uintptr_t global = 0;
      std::array<uint32_t, 2> bits = {};
      if (!offset || *offset < 8 || !Read(object + *offset - 8, &global) || !InImage(global) || !Read(global, &bits[0])
          || !Read(object + *offset, &bits[1]) || bits[0] != bits[1]) {
        continue;
      }
      const Cvar cvar = {.values = {reinterpret_cast<void*>(object + *offset), reinterpret_cast<void*>(global)}, .is_int = is_int};
      *details = std::format("{} {:g} (ref)", (is_int ? "int" : "float"), cvar.Get());
      return cvar;
    }
    // For support: the getters' first bytes (slots 9, 10, 15, 16).
    *details = "not an int / float variable; getters";
    for (const size_t slot : {9u, 10u, 15u, 16u}) {
      std::array<uint8_t, 8> code = {};
      if (!InImage(slots[slot]) || !Read(slots[slot], &code)) continue;
      *details += std::format(" [{}]", slot);
      for (const uint8_t byte : code) *details += std::format(" {:02X}", byte);
    }
    return {};
  }

 private:
  uintptr_t manager_ = 0;
  uintptr_t find_ = 0;
  uintptr_t sinks_flag_ = 0;  // FConsoleManager's bCallAllConsoleVariableSinks; 0: not found

  template <typename T>
  static bool Read(uintptr_t address, T* destination) {
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), destination, sizeof(T), &read)
           && read == sizeof(T);
  }

  static uintptr_t ImageBase() {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  }

  static bool InImage(uintptr_t address) {
    const uintptr_t base = ImageBase();
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    return address >= base && address < base + nt->OptionalHeader.SizeOfImage;
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

}  // namespace expeditionsrome::engine_cvars
