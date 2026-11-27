/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The plugin's engine values on fake objects at the game's addresses (no game file needed): each verdict, the
// restart-only range at both slider ends, a NaN, the write and its DirectX 11 gate, the key's text, the graphics route
// with the loader's note at several ages, the loader's frame cap, and the built DLL's init and game start with
// [Attila64Plugin.Values] keys (copies of the DLL under dll_cases\, a ReShade.ini and a dx11_starting.txt next to this
// exe, both removed at the end). Built with the C library as a DLL (build.ps1), as the loader exe is: this exe imports
// getenv, so the frame cap is set through this exe's own C library and read back with this exe's getenv. This exe also
// exports a _putenv and a getenv of its own that do nothing, as a C library whose read-back fails.
// tweaks_test.exe <renodx_attila64.dll>; exit code = failures.

// This test reads the C library's environment with getenv on purpose (the call the loader makes).
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "../tweaks.h"
#include "../tweaks.hpp"
#include "fake_loader.hpp"

// GetProcAddress on this exe finds these under the C library's names.
extern "C" int DeafPutEnvironment(const char* /*text*/) { return 0; }
extern "C" char* DeafGetEnvironment(const char* /*name*/) { return nullptr; }
#pragma comment(linker, "/EXPORT:_putenv=DeafPutEnvironment")
#pragma comment(linker, "/EXPORT:getenv=DeafGetEnvironment")

int wmain(int argc, wchar_t** argv) {
  using namespace twattila::attila64_plugin;
  using namespace twattila::attila64_plugin::test;
  using namespace twattila::attila64_plugin::tweaks;

  if (!ReserveGame(PAGE_READWRITE)) {
    std::printf("FAIL: the game range 0x400000..0x527d000 is taken\n");
    return 1;
  }
  if (argc < 2) {
    std::printf("FAIL: tweaks_test.exe <renodx_attila64.dll>\n");
    return 1;
  }
  char exe_dir[MAX_PATH];
  GetModuleFileNameA(nullptr, exe_dir, MAX_PATH);
  *std::strrchr(exe_dir, '\\') = '\0';
  wchar_t exe_dir_wide[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_dir_wide, MAX_PATH);
  *std::wcsrchr(exe_dir_wide, L'\\') = L'\0';
  const LoaderApi api = MakeApi(exe_dir, GAME_HI_MIN);
  const auto at = [](uintptr_t address) { return reinterpret_cast<uint8_t*>(address); };
  const auto entry_named = [](std::string_view name) -> const TweakEntry& {
    for (const TweakEntry& entry : TWEAKS) {
      if (name == entry.name) return entry;
    }
    Expect(false, "the table holds the entry the test names");
    return TWEAKS[0];
  };
  const auto value_of = [&](const TweakEntry& entry) {
    uint32_t value = 0;
    std::memcpy(&value, at(entry.object + TWEAK_KINDS[entry.type].value_offset), TWEAK_KINDS[entry.type].value_bytes);
    return value;
  };

  // The key's text.
  {
    uint32_t bits = 7;
    Expect(ParseKeyBits(L"00005da4", &bits) && bits == 0x5da4, "key: 8 hex digits");
    Expect(ParseKeyBits(L"BF8000aB", &bits) && bits == 0xbf8000ab, "key: both cases, the sign bit");
    bits = 7;
    Expect(!ParseKeyBits(L"5da4", &bits) && !ParseKeyBits(L"00005da4 ", &bits) && !ParseKeyBits(L"0x005da4", &bits)
               && !ParseKeyBits(L"", &bits) && !ParseKeyBits(L"000005da4", &bits) && bits == 7,
           "key: anything else is refused and leaves the bits");
  }

  // Every object as the game builds it.
  WriteTweakObjects();
  {
    size_t ok = 0;
    size_t absent = 0;
    size_t restart_only = 0;
    bool addresses = true;
    for (const TweakEntry& entry : TWEAKS) {
      TweakSlot slot;
      CheckTweak(entry, &slot);
      ok += (slot.verdict == VERDICT_OK) ? 1 : 0;
      absent += (slot.verdict == VERDICT_ABSENT && slot.value_address == 0) ? 1 : 0;
      restart_only += slot.restart_only;
      if (slot.verdict != VERDICT_OK) continue;
      addresses = addresses
                  && slot.value_address
                         == (entry.loader_writes ? 0 : (entry.object + ((entry.type == TWEAK_BOOL) ? 0x62u : 0x64u)))
                  && slot.default_bits == entry.default_bits && slot.written_bits == entry.default_bits
                  && slot.type == entry.type && slot.restart_state == RESTART_NONE
                  && std::string_view(slot.name) == entry.name;
    }
    Expect(ok == 82 && absent == 2 && restart_only == 7 && addresses, "check: 82 ok with their value addresses, 2 absent");
  }
  // The flag the loader stores before the game's start: 0 and 1 are both as found, anything else is not the game's.
  {
    const TweakEntry& entry = TWEAKS[LOADER_FLAG_TWEAK];
    TweakSlot slot;
    *at(entry.object + TWEAK_KINDS[TWEAK_BOOL].value_offset) = 1;
    CheckTweak(entry, &slot);
    Expect(entry.loader_writes && !entry.game_writes && slot.verdict == VERDICT_OK && slot.default_bits == 1
               && slot.value_address == 0,
           "check: a flag the loader stores is taken as found when it holds 1, and published without its address");
    *at(entry.object + TWEAK_KINDS[TWEAK_BOOL].value_offset) = 2;
    CheckTweak(entry, &slot);
    Expect(slot.verdict == VERDICT_CHANGED && slot.value_address == 0, "check: a flag the loader stores holds 0 or 1");
    WriteTweakObjects();
  }

  // Each refusal, on a float and on a flag.
  for (const char* name : {"tree_lod_scaler", "cindy_shadows", "terrain_enable_shadows", "grass_alpha_mul"}) {
    const TweakEntry& entry = entry_named(name);
    const TweakKind& kind = TWEAK_KINDS[entry.type];
    const auto verdict = [&] {
      TweakSlot slot;
      CheckTweak(entry, &slot);
      return std::pair{static_cast<TweakVerdict>(slot.verdict), slot};
    };
    const uint64_t other_table = TWEAK_KINDS[(entry.type == TWEAK_UINT) ? TWEAK_BOOL : TWEAK_UINT].table;
    std::memcpy(at(entry.object), &other_table, sizeof(other_table));
    Expect(verdict().first == VERDICT_WRONG_TYPE && verdict().second.value_address == 0, "check: another type's table");
    std::memset(at(entry.object), 0, sizeof(other_table));
    Expect(verdict().first == VERDICT_WRONG_TYPE, "check: an object the game has not built");
    WriteTweakObjects();

    uint32_t length = 0;
    std::memcpy(&length, at(entry.object + TWEAK_NAME_LENGTH_OFFSET), sizeof(length));
    const uint32_t longer = length + 1;
    std::memcpy(at(entry.object + TWEAK_NAME_LENGTH_OFFSET), &longer, sizeof(longer));
    Expect(verdict().first == VERDICT_WRONG_NAME, "check: a name of another length");
    WriteTweakObjects();
    char16_t* units = nullptr;
    std::memcpy(&units, at(entry.object + TWEAK_NAME_UNITS_OFFSET), sizeof(units));
    units[length - 1] ^= 1;
    Expect(verdict().first == VERDICT_WRONG_NAME, "check: a name with another last letter");
    std::memset(at(entry.object + TWEAK_NAME_UNITS_OFFSET), 0, sizeof(units));
    Expect(verdict().first == VERDICT_WRONG_NAME, "check: no name");
    WriteTweakObjects();

    const uint32_t changed = entry.default_bits ^ 1;
    std::memcpy(at(entry.object + kind.value_offset), &changed, kind.value_bytes);
    if (entry.game_writes) {
      Expect(verdict().first == VERDICT_OK && verdict().second.default_bits == changed && verdict().second.value_address != 0,
             "check: a value the game stores itself is taken as found");
    } else {
      Expect(verdict().first == VERDICT_CHANGED && verdict().second.default_bits == changed
                 && verdict().second.value_address == 0,
             "check: not the game's default");
    }
    WriteTweakObjects();
  }

  // The restart-only range: an integer and a float, at both slider ends, and the DirectX 11 gate.
  {
    const TweakEntry& sound = entry_named("SOUND_DEFAULT_BUFFER_SIZE_IN_KB");  // 1x .. 4x of 23972
    const TweakEntry& voice_limit = entry_named("SOUND_LOWER_ENGINE_DEFAULT_BUFFER_LOW_RATIO");  // 0.5x .. 1.43x of 0.7
    Expect(sound.restart_only && sound.low_bits == 23972 && sound.high_bits == 95888 && sound.type == TWEAK_UINT,
           "range: the integer's ends are the slider's (1x and 4x)");
    Expect(voice_limit.restart_only && voice_limit.type == TWEAK_FLOAT
               && voice_limit.low_bits == std::bit_cast<uint32_t>(0.7f * 0.5f)
               && voice_limit.high_bits == std::bit_cast<uint32_t>(0.7f * 1.43f),
           "range: the float's ends are the single-precision products (0.5x and 1.43x)");
    const auto write = [&](const TweakEntry& entry, uint32_t bits, bool dx11) {
      WriteTweakObjects();
      TweakSlot slot;
      CheckTweak(entry, &slot);
      const RestartState state = WriteRestartValue(entry, bits, dx11, &slot);
      const bool stored = (value_of(entry) == bits && slot.written_bits == bits);
      const bool kept = (value_of(entry) == entry.default_bits && slot.written_bits == entry.default_bits);
      // The state, the slot and the game's memory agree.
      if (slot.restart_state != state || (state == RESTART_APPLIED ? !stored : !kept)) return RESTART_NONE;
      return state;
    };
    Expect(write(sound, sound.low_bits, true) == RESTART_APPLIED && write(sound, sound.high_bits, true) == RESTART_APPLIED
               && write(sound, 2 * 23972, true) == RESTART_APPLIED,
           "range: the integer at both ends and inside is written");
    Expect(write(sound, sound.low_bits - 1, true) == RESTART_OUT_OF_RANGE
               && write(sound, sound.high_bits + 1, true) == RESTART_OUT_OF_RANGE
               && write(sound, 0, true) == RESTART_OUT_OF_RANGE && write(sound, 0xffffffff, true) == RESTART_OUT_OF_RANGE,
           "range: the integer one past each end is refused");
    const float low = std::bit_cast<float>(voice_limit.low_bits);
    const float high = std::bit_cast<float>(voice_limit.high_bits);
    const auto bits_of = [](float value) { return std::bit_cast<uint32_t>(value); };
    Expect(write(voice_limit, voice_limit.low_bits, true) == RESTART_APPLIED
               && write(voice_limit, voice_limit.high_bits, true) == RESTART_APPLIED
               && write(voice_limit, bits_of(0.7f * 1.2f), true) == RESTART_APPLIED,
           "range: the float at both ends and inside is written");
    Expect(write(voice_limit, bits_of(std::nextafter(low, 0.0f)), true) == RESTART_OUT_OF_RANGE
               && write(voice_limit, bits_of(std::nextafter(high, 2.0f)), true) == RESTART_OUT_OF_RANGE
               && write(voice_limit, bits_of(1.8f), true) == RESTART_OUT_OF_RANGE
               && write(voice_limit, bits_of(0.0f), true) == RESTART_OUT_OF_RANGE
               && write(voice_limit, bits_of(-0.5f), true) == RESTART_OUT_OF_RANGE,
           "range: the float one step past each end, 1.8, 0 and a negative are refused");
    Expect(write(voice_limit, bits_of(std::numeric_limits<float>::quiet_NaN()), true) == RESTART_OUT_OF_RANGE
               && write(voice_limit, 0xffc00000, true) == RESTART_OUT_OF_RANGE
               && write(voice_limit, bits_of(std::numeric_limits<float>::infinity()), true) == RESTART_OUT_OF_RANGE,
           "range: a NaN (either sign) and infinity are refused");
    Expect(write(sound, 2 * 23972, false) == RESTART_NOT_DX11 && write(voice_limit, voice_limit.high_bits, false) == RESTART_NOT_DX11
               && write(sound, 0, false) == RESTART_OUT_OF_RANGE,
           "range: off DirectX 11 nothing is written, and the range is still said first");
    WriteTweakObjects();

    // A signed value compares signed (no entry of today's table is one).
    constexpr uintptr_t SIGNED_VALUE = 0x700000;
    const TweakEntry signed_entry = {.name = "signed", .low_bits = static_cast<uint32_t>(-10), .high_bits = 5, .type = TWEAK_INT};
    const auto write_signed = [&](int32_t value) {
      std::memset(at(SIGNED_VALUE), 0, sizeof(value));
      TweakSlot slot = {.value_address = SIGNED_VALUE};
      const RestartState state = WriteRestartValue(signed_entry, static_cast<uint32_t>(value), true, &slot);
      int32_t stored = 0;
      std::memcpy(&stored, at(SIGNED_VALUE), sizeof(stored));
      return std::pair{state, stored};
    };
    Expect(write_signed(-10) == std::pair{RESTART_APPLIED, -10} && write_signed(5) == std::pair{RESTART_APPLIED, 5}
               && write_signed(-1) == std::pair{RESTART_APPLIED, -1},
           "range: a signed integer at both ends and at -1 is written");
    Expect(write_signed(-11) == std::pair{RESTART_OUT_OF_RANGE, 0} && write_signed(6) == std::pair{RESTART_OUT_OF_RANGE, 0}
               && write_signed(std::numeric_limits<int32_t>::min()) == std::pair{RESTART_OUT_OF_RANGE, 0}
               && write_signed(std::numeric_limits<int32_t>::max()) == std::pair{RESTART_OUT_OF_RANGE, 0},
           "range: a signed integer one past each end is refused");
  }

  // The graphics route: the loader's test switch, else its note no older than this process (2 s of slack).
  const std::wstring note_path = std::wstring(exe_dir_wide) + L"\\" + ROUTE_NOTE_FILE;
  const std::wstring ini_path = std::wstring(exe_dir_wide) + L"\\ReShade.ini";
  const auto write_note = [&](int64_t seconds_before_start) {
    HANDLE note = CreateFileW(note_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(note != INVALID_HANDLE_VALUE, "route: the note file is written");
    if (note == INVALID_HANDLE_VALUE) return;
    FILETIME created = {};
    FILETIME exited = {};
    FILETIME kernel = {};
    FILETIME user = {};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    const uint64_t ticks =
        ((uint64_t{created.dwHighDateTime} << 32) | created.dwLowDateTime) - (seconds_before_start * 10'000'000);
    const FILETIME written = {.dwLowDateTime = static_cast<DWORD>(ticks), .dwHighDateTime = static_cast<DWORD>(ticks >> 32)};
    Expect(SetFileTime(note, nullptr, nullptr, &written) != FALSE, "route: the note's time is set");
    CloseHandle(note);
  };
  {
    char switch_before[16] = {};
    const DWORD switch_length = GetEnvironmentVariableA(ROUTE_VARIABLE, switch_before, sizeof(switch_before));
    SetEnvironmentVariableA(ROUTE_VARIABLE, nullptr);
    DeleteFileW(note_path.c_str());
    Expect(FindRoute(note_path.c_str()) == ROUTE_NO_NOTE, "route: no note is not DX11");
    write_note(-1);
    Expect(FindRoute(note_path.c_str()) == ROUTE_DX11_NOTE, "route: a note written after this process started is DX11");
    write_note(1);
    Expect(FindRoute(note_path.c_str()) == ROUTE_DX11_NOTE, "route: a note 1 s older than the process is DX11 (the slack)");
    write_note(10);
    Expect(FindRoute(note_path.c_str()) == ROUTE_OLD_NOTE, "route: a note 10 s older than the process is an earlier start's");
    SetEnvironmentVariableA(ROUTE_VARIABLE, "1");
    Expect(FindRoute(note_path.c_str()) == ROUTE_DX11_SWITCH, "route: ATTILA_DX11=1 is DX11 whatever the note");
    DeleteFileW(note_path.c_str());
    Expect(FindRoute(note_path.c_str()) == ROUTE_DX11_SWITCH, "route: ATTILA_DX11=1 needs no note");
    for (const char* other : {"0", "11", "", "true"}) {
      SetEnvironmentVariableA(ROUTE_VARIABLE, other);
      Expect(FindRoute(note_path.c_str()) == ROUTE_NO_NOTE, "route: only exactly 1 is the test switch");
    }
    SetEnvironmentVariableA(ROUTE_VARIABLE, ((switch_length != 0 && switch_length < sizeof(switch_before)) ? switch_before : nullptr));
  }

  // The loader's frame cap, through this exe's own C library (the module its import of getenv is bound to).
  const auto clear_cap = [] {
    SetEnvironmentVariableA(FRAME_CAP_VARIABLE, nullptr);
    _putenv("ATTILA_FPS_CAP=");
  };
  const auto clear_log = [] {
    const std::scoped_lock lock(log_mutex);
    log_lines.clear();
  };
  const auto process_cap = [] {
    char value[16] = "(none)";
    GetEnvironmentVariableA(FRAME_CAP_VARIABLE, value, sizeof(value));
    return std::string(value);
  };
  const auto cap_is_unset = [] {
    SetLastError(ERROR_SUCCESS);
    return GetEnvironmentVariableA(FRAME_CAP_VARIABLE, nullptr, 0) == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND
           && std::getenv(FRAME_CAP_VARIABLE) == nullptr;
  };
  constexpr char CAP_OFF_LINE[] = "renodx_attila64: loader frame cap: off (ATTILA_FPS_CAP=0, read back by the loader's getenv)";
  constexpr char CAP_ON_LINE[] = "renodx_attila64: loader frame cap: on (the loader's 240)";
  constexpr char CAP_LEFT_LINE[] = "renodx_attila64: loader frame cap: left on (the loader's C library not found)";
  const HMODULE c_library = ImportedFrom(GetModuleHandleW(nullptr), "getenv");
  {
    HMODULE getenv_module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&std::getenv), &getenv_module);
    Expect(c_library != nullptr && c_library == getenv_module && c_library != GetModuleHandleW(nullptr),
           "cap: this exe's import of getenv is bound to its C library's module");
    Expect(ImportedFrom(GetModuleHandleW(nullptr), "renodx_no_such_import") == nullptr
               && ImportedFrom(GetModuleHandleW(L"kernel32.dll"), "getenv") == nullptr,
           "cap: an image that does not import the name has no such module");

    clear_cap();
    clear_log();
    SetLoaderFrameCap(false, c_library, api);
    Expect(Logged(CAP_OFF_LINE) && process_cap() == "0" && std::getenv(FRAME_CAP_VARIABLE) != nullptr
               && std::string_view(std::getenv(FRAME_CAP_VARIABLE)) == "0",
           "cap: off is set in the process and read back as 0 by this exe's getenv");

    clear_cap();
    clear_log();
    SetLoaderFrameCap(true, c_library, api);
    Expect(Logged(CAP_ON_LINE) && cap_is_unset(), "cap: on writes nothing");

    for (const char* players : {"144", "0", ""}) {
      clear_cap();
      SetEnvironmentVariableA(FRAME_CAP_VARIABLE, players);
      clear_log();
      SetLoaderFrameCap(false, c_library, api);
      const std::string kept = std::string("renodx_attila64: loader frame cap: the player's ATTILA_FPS_CAP=") + players + " kept";
      Expect(Logged(kept.c_str()) && !Logged(CAP_OFF_LINE) && process_cap() == players,
             "cap: a value the player set is kept, even an empty one");
    }
    clear_cap();
    SetEnvironmentVariableA(FRAME_CAP_VARIABLE, std::string(100, '9').c_str());
    clear_log();
    SetLoaderFrameCap(false, c_library, api);
    Expect(Logged("the player's ATTILA_FPS_CAP (too long to show) kept"), "cap: a long value of the player's is kept");

    for (const HMODULE without : {HMODULE{nullptr}, GetModuleHandleW(L"kernel32.dll")}) {
      clear_cap();
      clear_log();
      SetLoaderFrameCap(false, without, api);
      Expect(Logged(CAP_LEFT_LINE) && cap_is_unset(), "cap: without the C library's two functions nothing is written");
    }
    // This exe's own exports: the write is made, the read-back gives nothing, and the line says so.
    clear_cap();
    clear_log();
    SetLoaderFrameCap(false, GetModuleHandleW(nullptr), api);
    Expect(Logged("renodx_attila64: loader frame cap: left on (the loader's getenv did not read ATTILA_FPS_CAP=0 back)")
               && !Logged(CAP_OFF_LINE) && std::getenv(FRAME_CAP_VARIABLE) == nullptr,
           "cap: a read-back that fails is said, never the off line");
    clear_cap();
  }

  // The built DLL, as the loader runs it: the image holds the game's identity and every object; no site is the game's
  // here, so no row is applied.
  std::memcpy(at(GAME_LO), ELF_MAGIC.data(), ELF_MAGIC.size());
  std::memcpy(at(BUILD_ID_NOTE_ADDRESS), BUILD_ID_NOTE.data(), BUILD_ID_NOTE.size());
  const std::wstring cases_dir = std::wstring(exe_dir_wide) + L"\\dll_cases";
  CreateDirectoryW(cases_dir.c_str(), nullptr);
  const auto start_copy = [&](const wchar_t* folder, const char* players_cap = nullptr, int init_result = 0) {
    const std::wstring directory = cases_dir + L"\\" + folder;
    CreateDirectoryW(directory.c_str(), nullptr);
    const std::wstring target = directory + L"\\" + MODULE_NAME;
    Expect(CopyFileW(argv[1], target.c_str(), FALSE) != FALSE, "a copy of the plugin DLL");
    const Plugin plugin = LoadPlugin(target.c_str());
    WriteTweakObjects();
    clear_cap();
    if (players_cap != nullptr) {
      SetEnvironmentVariableA(FRAME_CAP_VARIABLE, players_cap);
    }
    clear_log();
    if (plugin.init == nullptr || plugin.init(&api) != init_result) {
      Expect(false, "dll: init");
      return Plugin{};
    }
    return plugin;
  };
  const auto set_value = [&](const wchar_t* key, const wchar_t* text) {
    WritePrivateProfileStringW(INI_VALUES_SECTION, key, text, ini_path.c_str());
  };
  const auto slot_of = [](const Plugin& plugin, std::string_view name) {
    for (uint32_t index = 0; index < plugin.block->tweak_count; ++index) {
      if (name == plugin.block->tweaks[index].name) return plugin.block->tweaks[index];
    }
    return TweakSlot{};
  };
  const TweakEntry& sound = entry_named("SOUND_DEFAULT_BUFFER_SIZE_IN_KB");
  const TweakEntry& sound_limit = entry_named("SOUND_DEFAULT_BUFFER_LOW_RATIO");
  const TweakEntry& voice = entry_named("SOUND_LOWER_ENGINE_DEFAULT_BUFFER_SIZE_IN_KB");
  const TweakEntry& stream = entry_named("SOUND_STREAM_IO_BUFFER_SIZE_IN_KB");
  const TweakEntry& files = entry_named("SOUND_WAVEFILE_BUFFER_SIZE_IN_KB");
  const auto set_keys = [&] {
    DeleteFileW(ini_path.c_str());
    set_value(L"SOUND_DEFAULT_BUFFER_SIZE_IN_KB", L"0000bb48");                // 2x: written
    set_value(L"SOUND_DEFAULT_BUFFER_LOW_RATIO", L"3f7d70a4");                 // 0.99 = 1.1x: written
    set_value(L"SOUND_LOWER_ENGINE_DEFAULT_BUFFER_SIZE_IN_KB", L"00000001");  // below 1x
    set_value(L"SOUND_STREAM_IO_BUFFER_SIZE_IN_KB", L"2048");                  // not 8 hex digits
    set_value(L"tree_lod_scaler", L"40000000");                                // not a restart-only value: never read
  };

  // DirectX 11 (this start's note), no SolFrameCap key: the keys in range are written, the cap is off.
  set_keys();
  write_note(-1);
  if (const Plugin plugin = start_copy(L"values_dx11"); plugin.block != nullptr) {
    Expect(Logged("renodx_attila64 2: game build ok") && Logged(", graphics DX11 (dx11_starting.txt from this start), perffix64 "),
           "dll: the init line names the route and its rule");
    Expect(Logged(CAP_OFF_LINE) && process_cap() == "0" && std::getenv(FRAME_CAP_VARIABLE) != nullptr
               && std::string_view(std::getenv(FRAME_CAP_VARIABLE)) == "0",
           "dll: no SolFrameCap key turns the loader's cap off, read back by this exe's getenv");
    Expect(plugin.block->tweaks_status.load() == TWEAKS_PENDING && value_of(sound) == sound.default_bits,
           "dll: nothing is written before the game's start");
    plugin.game_started(&api);
    Expect(plugin.block->tweaks_status.load(std::memory_order_acquire) == TWEAKS_READY && plugin.block->tweak_count == TWEAKS.size()
               && Logged("renodx_attila64: tweaks: 81 checked, 81 ok, 3 absent, restart-only: 2 applied")
               && Logged("renodx_attila64: parallel effects: not offered (its row is off)"),
           "dll: the tweaks line");
    Expect(value_of(sound) == 0xbb48 && slot_of(plugin, sound.name).restart_state == RESTART_APPLIED
               && slot_of(plugin, sound.name).written_bits == 0xbb48 && slot_of(plugin, sound.name).default_bits == sound.default_bits
               && Logged("renodx_attila64: tweak SOUND_DEFAULT_BUFFER_SIZE_IN_KB: 0x0000bb48 written (restart-only)")
               && value_of(sound_limit) == 0x3f7d70a4 && slot_of(plugin, sound_limit.name).restart_state == RESTART_APPLIED
               && Logged("renodx_attila64: tweak SOUND_DEFAULT_BUFFER_LOW_RATIO: 0x3f7d70a4 written (restart-only)"),
           "dll: an integer and a float key in range are written at the game's start");
    Expect(value_of(voice) == voice.default_bits && slot_of(plugin, voice.name).restart_state == RESTART_OUT_OF_RANGE
               && slot_of(plugin, voice.name).written_bits == voice.default_bits
               && Logged("renodx_attila64: tweak SOUND_LOWER_ENGINE_DEFAULT_BUFFER_SIZE_IN_KB: 0x00000001 out of range, the "
                         "game's value kept"),
           "dll: a key out of range keeps the game's value");
    Expect(value_of(stream) == stream.default_bits && slot_of(plugin, stream.name).restart_state == RESTART_OUT_OF_RANGE
               && Logged("renodx_attila64: tweak SOUND_STREAM_IO_BUFFER_SIZE_IN_KB: key not 8 hex digits, the game's value kept"),
           "dll: a key that is not 8 hex digits keeps the game's value");
    const TweakEntry& live = entry_named("tree_lod_scaler");
    Expect(value_of(live) == live.default_bits && slot_of(plugin, live.name).restart_state == RESTART_NONE
               && slot_of(plugin, live.name).value_address == live.object + 0x64u
               && slot_of(plugin, files.name).restart_state == RESTART_NONE && slot_of(plugin, files.name).restart_only == 1
               && slot_of(plugin, "CINDY_POINT_LIGHT_SHADOWS_ENABLED").verdict == VERDICT_ABSENT,
           "dll: a live value's key is never read; a restart-only value without a key stays the game's");
  }

  // Not DirectX 11 (an earlier start's note), SolFrameCap=1, one object not at its default: nothing is written, the
  // loader's cap stays.
  set_keys();
  set_value(INI_SOL_FRAME_CAP, L"1");
  write_note(10);
  if (const Plugin plugin = start_copy(L"values_not_dx11"); plugin.block != nullptr) {
    Expect(Logged(", graphics not DX11 (dx11_starting.txt from an earlier start), perffix64 ") && Logged(CAP_ON_LINE)
               && cap_is_unset(),
           "dll: SolFrameCap=1 keeps the loader's cap; the route is not DX11");
    const TweakEntry& changed = entry_named("battle_entity_lod0_distance");
    const uint32_t other_value = changed.default_bits + 1;
    std::memcpy(at(changed.object + 0x64), &other_value, sizeof(other_value));
    plugin.game_started(&api);
    char refused[128];
    std::snprintf(refused, sizeof(refused), "renodx_attila64: tweak battle_entity_lod0_distance: refused (not the game's default, value 0x%x)",
                  other_value);
    Expect(Logged("renodx_attila64: tweaks: 81 checked, 80 ok, 3 absent, restart-only: 0 applied") && Logged(refused)
               && slot_of(plugin, changed.name).verdict == VERDICT_CHANGED && slot_of(plugin, changed.name).value_address == 0,
           "dll: an object not at its default is refused by name and counted");
    Expect(value_of(sound) == sound.default_bits && slot_of(plugin, sound.name).restart_state == RESTART_NOT_DX11
               && Logged("renodx_attila64: tweak SOUND_DEFAULT_BUFFER_SIZE_IN_KB: 0x0000bb48 not written (not DX11)")
               && slot_of(plugin, voice.name).restart_state == RESTART_OUT_OF_RANGE,
           "dll: off DirectX 11 a key in range is not written");
  }

  // Tweaks=0: nothing checked or published; the cap is still set (SolFrameCap=0 is off, as no key is).
  set_keys();
  set_value(INI_SOL_FRAME_CAP, L"0");
  WritePrivateProfileStringW(INI_SECTION, INI_TWEAKS, L"0", ini_path.c_str());
  write_note(-1);
  if (const Plugin plugin = start_copy(L"values_off"); plugin.block != nullptr) {
    plugin.game_started(&api);
    Expect(plugin.block->tweaks_status.load() == TWEAKS_NONE && plugin.block->tweak_count == 0 && plugin.block->tweaks == nullptr
               && !Logged("renodx_attila64: tweak") && value_of(sound) == sound.default_bits && Logged(CAP_OFF_LINE)
               && Logged("renodx_attila64: parallel effects: not offered (Tweaks=0)"),
           "dll: Tweaks=0 checks, writes and publishes nothing");
  }

  // SolFrameCap: only exactly 1 keeps the cap. With a restart-only object that is not the game's while its key is
  // there: refused by name, nothing written to it, the other key still written.
  for (const wchar_t* other : {L"2", L"yes"}) {
    set_keys();
    set_value(INI_SOL_FRAME_CAP, other);
    write_note(-1);
    const Plugin plugin = start_copy((std::wstring(L"values_cap_") + other).c_str());
    if (plugin.block == nullptr) continue;
    Expect(Logged(CAP_OFF_LINE) && !Logged(CAP_ON_LINE) && process_cap() == "0", "dll: SolFrameCap 2 or text is off");
    const uint64_t other_table = TWEAK_KINDS[TWEAK_FLOAT].table;
    std::memcpy(at(sound.object), &other_table, sizeof(other_table));
    plugin.game_started(&api);
    Expect(Logged("renodx_attila64: tweak SOUND_DEFAULT_BUFFER_SIZE_IN_KB: refused (another type, value 0x0)")
               && !Logged("renodx_attila64: tweak SOUND_DEFAULT_BUFFER_SIZE_IN_KB: 0x")
               && Logged("renodx_attila64: tweaks: 81 checked, 80 ok, 3 absent, restart-only: 1 applied")
               && value_of(sound) == sound.default_bits && slot_of(plugin, sound.name).verdict == VERDICT_WRONG_TYPE
               && slot_of(plugin, sound.name).restart_state == RESTART_NONE && slot_of(plugin, sound.name).value_address == 0
               && value_of(sound_limit) == 0x3f7d70a4,
           "dll: a refused restart-only value is not written, whatever its key");
  }

  // Enabled=0: the plugin does nothing at all, so the loader's cap stays and no value is checked.
  set_keys();
  WritePrivateProfileStringW(INI_SECTION, INI_ENABLED, L"0", ini_path.c_str());
  write_note(-1);
  if (const Plugin plugin = start_copy(L"values_disabled", nullptr, 1); plugin.block != nullptr) {
    plugin.game_started(&api);
    Expect(plugin.block->status.load() == STATUS_OFF && Logged("Enabled=0") && !Logged("loader frame cap") && cap_is_unset()
               && plugin.block->tweaks_status.load() == TWEAKS_NONE && !Logged("renodx_attila64: tweak")
               && value_of(sound) == sound.default_bits,
           "dll: Enabled=0 leaves the loader's cap and every value alone");
  }

  // A player's own value wins over the key.
  set_keys();
  if (const Plugin plugin = start_copy(L"values_player", "120"); plugin.block != nullptr) {
    Expect(Logged("renodx_attila64: loader frame cap: the player's ATTILA_FPS_CAP=120 kept") && process_cap() == "120",
           "dll: the player's own ATTILA_FPS_CAP is kept");
  }
  clear_cap();
  DeleteFileW(ini_path.c_str());
  DeleteFileW(note_path.c_str());

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
