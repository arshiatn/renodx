/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// The 64-bit loader's start files (Sol's settings.txt format: one "name=value" per line) and the files of the plugins
// it starts, read the way their owners read them. Their owners read them only at the game's start.
namespace twattila::start_files {

// _strnicmp(text, prefix, prefix length) == 0: the ASCII letters of the prefix in either case.
inline bool StartsWithNoCase(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size()
         && std::ranges::equal(text.substr(0, prefix.size()), prefix, [](char left, char right) {
              return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
            });
}

// Each line of `file` in order, cut at its first CR or LF, to `visit` (none: no such file).
template <typename Visit>
void ForEachLine(const std::filesystem::path& file, Visit visit) {
  std::ifstream stream(file);
  for (std::string line; std::getline(stream, line);) {
    line.resize(std::min(line.find('\r'), line.size()));
    visit(std::string_view(line));
  }
}

// Sol's name test (S28 picker.c:63, :82): the line starts with the name in either case, then '='.
inline bool LineNames(std::string_view line, std::string_view name) {
  return line.size() > name.size() && line[name.size()] == '=' && StartsWithNoCase(line, name);
}

// Sol's settings_get (S28 picker.c:54-70): `name`'s value in a file of his format: the rest of the last line that
// starts with the name (in either case) and '=', not trimmed. None: no such line, or no file.
inline std::optional<std::string> Value(const std::filesystem::path& file, std::string_view name) {
  std::optional<std::string> value;
  ForEachLine(file, [name, &value](std::string_view line) {
    if (LineNames(line, name)) {
      value = line.substr(name.size() + 1);
    }
  });
  return value;
}

// FrameBridge's settings.txt switches it off: its last addon= line starts with "off" in either case (Sol's addon=off
// prefix, S28 picker.c:253-263). The loader leaves the last addon= line as the start window's tick (S28
// plugins.inc:93-118). No file or no such line: on.
inline bool FrameBridgeOff(const std::filesystem::path& settings) {
  const std::optional<std::string> addon = Value(settings, "addon");
  return addon.has_value() && StartsWithNoCase(*addon, "off");
}

// The CPU page's rows of the two start files (64-bit only): settings.txt (Sol's start window) and heapfix.ini (the
// Engine Performance Fix's). Semi-automatic (ATN 11-27): the keys of the tables below, then every other key found in
// the file as a plain row named by its key (Off / On for 0 / 1, else read-only).

// A value a row can write and its name on the page. The labels are ImGui formats ("%%" shows "%").
struct Choice {
  const char* label = nullptr;
  const char* value = nullptr;
};

inline constexpr std::array<Choice, 2> OFF_ON = {{{.label = "Off", .value = "0"}, {.label = "On", .value = "1"}}};

// How Sol's loader reads a settings.txt key, copied from his readers (S28 picker.c).
enum class StartTest : uint8_t {
  // settings_size_index (:94-101): Auto with no line or a value starting with "like1080p" / "auto" (either case); else
  // atoi(value) > 0 matched against the sizes' percents, no match Auto.
  INTERFACE_SIZE,
  // settings_shadow_every (:271-275): exactly one character '1'..'4'; else 1.
  SHADOW_REFRESH,
  // settings_fast_task_waits / settings_full_uploads (:184-191): exactly the second choice's word (strcmp); anything
  // else or no line: the first choice.
  EXACT_SECOND,
  // settings_perf_fix / settings_flag_wave (:205-212): off only when the value starts with '0'; no line: on.
  OFF_IF_ZERO,
  // settings_upscaling (:253-269): on unless the value starts with '0'; no line: off only when a line of
  // <exe dir>\..\FrameBridge\settings.txt starts with "addon=off" (either case).
  UPSCALING,
};

// The start window has the Engine Performance Fix's boxes only while the fix is installed (S28 picker.c:1267-1283).
enum class FixPart : uint8_t {
  NONE,
  SWITCH,  // perf_fix
  PART,    // flag_wave, upscaling: greyed while perf_fix reads off (:1268-1270)
};

struct StartKey {
  const char* name = nullptr;  // as Sol writes it
  const char* label = nullptr;
  const char* tooltip = nullptr;
  StartTest test = StartTest::OFF_IF_ZERO;
  std::span<const Choice> choices;
  FixPart fix_part = FixPart::NONE;
  // The first loader (attila64.exe's file version) that reads the key this way; zero: every loader.
  std::array<uint16_t, 4> first_loader = {};
};

// The start window's boxes (S28 picker.c:29-31, :1257) and what it writes (:103-107, :1398-1405); task_waits and
// texture_uploads have no box: "fast" is Sol's word, "game" and "rows" are his "anything else".
inline constexpr std::array<Choice, 7> INTERFACE_SIZES = {{
    {.label = "100%%", .value = "100"},
    {.label = "Auto (like 1080p)", .value = "like1080p"},
    {.label = "125%%", .value = "125"},
    {.label = "150%%", .value = "150"},
    {.label = "175%%", .value = "175"},
    {.label = "200%%", .value = "200"},
    {.label = "250%%", .value = "250"},
}};
inline constexpr std::array<Choice, 4> SHADOW_REFRESHES = {{
    {.label = "Every frame", .value = "1"},
    {.label = "Every 2nd frame", .value = "2"},
    {.label = "Every 3rd frame", .value = "3"},
    {.label = "Every 4th frame", .value = "4"},
}};
inline constexpr std::array<Choice, 2> TASK_WAITS = {{{.label = "Off", .value = "game"}, {.label = "On", .value = "fast"}}};
inline constexpr std::array<Choice, 2> TEXTURE_UPLOADS = {
    {{.label = "Changed Rows", .value = "rows"}, {.label = "Whole Textures", .value = "full"}}};

// perf_fix comes before its two parts (StartWindowRows reads it first).
inline constexpr std::array<StartKey, 7> START_KEYS = {{
    {
        .name = "interface_size",
        .label = "Interface Size",
        .tooltip = "The size of the game's interface. Auto: as big as at 1080p.",
        .test = StartTest::INTERFACE_SIZE,
        .choices = INTERFACE_SIZES,
    },
    {
        .name = "shadow_refresh",
        .label = "Shadows Redrawn",
        .tooltip = "Less often is faster: the shadows of a still camera are redrawn every few frames.",
        .test = StartTest::SHADOW_REFRESH,
        .choices = SHADOW_REFRESHES,
    },
    // Before 1.3.24 the key read the other way: "game" was the game's waits, anything else fast (1.3.20
    // picker.c:177-180).
    {
        .name = "task_waits",
        .label = "Task Waits",
        .tooltip = "Off: the game's own waits. On uses much more CPU time on PCs with 4-6 cores and on laptops, with "
                   "no gain in Sol's tests.",
        .test = StartTest::EXACT_SECOND,
        .choices = TASK_WAITS,
        .first_loader = {1, 3, 24, 0},
    },
    {
        .name = "texture_uploads",
        .label = "Texture Uploads",
        .tooltip = "Changed Rows sends only the changed part of each animation texture (faster).",
        .test = StartTest::EXACT_SECOND,
        .choices = TEXTURE_UPLOADS,
    },
    {
        .name = "perf_fix",
        .label = "Engine Performance Fix",
        .tooltip = "Starts the Engine Performance Fix with the game.",
        .test = StartTest::OFF_IF_ZERO,
        .choices = OFF_ON,
        .fix_part = FixPart::SWITCH,
    },
    {
        .name = "flag_wave",
        .label = "Waving Flags",
        .tooltip = "The Engine Performance Fix's waving flags. Flags and props smear with DLSS, FSR, XeSS and TAA.",
        .test = StartTest::OFF_IF_ZERO,
        .choices = OFF_ON,
        .fix_part = FixPart::PART,
    },
    {
        .name = "upscaling",
        .label = "Upscaling (FrameBridge)",
        .tooltip = "FrameBridge's upscaling and frame generation. It clashes with this mod's DLSS and frame generation.",
        .test = StartTest::UPSCALING,
        .choices = OFF_ON,
        .fix_part = FixPart::PART,
    },
}};

// settings.txt keys never shown or written: the graphics route (a wrong value can leave a PC on OpenGL) and the start
// window's other boxes (not CPU).
inline constexpr std::array<std::string_view, 7> START_KEYS_NOT_SHOWN = {
    "graphics", "dx11_mode", "cursor_lock", "show_fps", "theme", "texture_sharpness", "discord_button"};

// How a heapfix.ini value reads on the page (E: the fix's outputs do not say how it reads its values).
enum class FixValue : uint8_t {
  SWITCH,   // OFF_ON
  CULLING,  // FIX_CULLINGS, either case
  COUNT,    // 1 .. FIX_COUNT_MAX
};

struct FixKey {
  const char* section = nullptr;  // nullptr: not known, the row read-only until the file has the key
  const char* name = nullptr;
  const char* label = nullptr;
  const char* tooltip = "";
  FixValue kind = FixValue::SWITCH;
  int default_value = -1;  // the row's value with the key missing (a choice's index or the count); -1: not known
};

inline constexpr std::array<Choice, 4> FIX_CULLINGS = {{
    {.label = "Off", .value = "off"},
    {.label = "Light", .value = "light"},
    {.label = "Normal", .value = "normal"},
    {.label = "Strong", .value = "strong"},
}};
inline constexpr int FIX_COUNT_MAX = 1024;

// M: the keys of the file the fix wrote in ATN's 32-bit game folder, each fix on with the key missing (a file with only
// [mod] self_update=0 ran every fix, cull "light" included). E: parallel_sort, battle_ui_cache, sound_crash_guard and
// [tuning] culling from our 11-24 notes.
inline constexpr std::array<FixKey, 20> FIX_KEYS = {{
    {.section = "fixes", .name = "memory_allocator", .label = "Memory Allocator", .default_value = 1},
    {.section = "fixes", .name = "small_object_pools", .label = "Small Object Pools", .default_value = 1},
    {.section = "fixes", .name = "render_list_sorts", .label = "Render List Sorts", .default_value = 1},
    {.section = "fixes", .name = "battle_draw_batching", .label = "Battle Draw Batching", .default_value = 1},
    {.section = "fixes", .name = "campaign_rounding", .label = "Campaign Rounding", .default_value = 1},
    {.section = "fixes", .name = "x87_rounding", .label = "x87 Rounding", .default_value = 1},
    {.section = "fixes", .name = "terrain_height_cache", .label = "Terrain Height Cache", .default_value = 1},
    {.section = "fixes", .name = "small_memory_copies", .label = "Small Memory Copies", .default_value = 1},
    {.section = "fixes", .name = "render_loop_prefetch", .label = "Render Loop Prefetch", .default_value = 1},
    {.section = "fixes", .name = "file_reading", .label = "File Reading", .default_value = 1},
    {.section = "fixes", .name = "missing_file_cache", .label = "Missing File Cache", .default_value = 1},
    {.section = "fixes", .name = "battle_textures", .label = "Battle Textures", .default_value = 1},
    {.section = "fixes", .name = "menu_textures", .label = "Menu Textures", .default_value = 1},
    {.name = "parallel_sort", .label = "Parallel Sort", .default_value = 1},
    {.name = "battle_ui_cache", .label = "Battle UI Cache", .tooltip = "Draws the battle unit cards from a cache.",
     .default_value = 1},
    {.name = "sound_crash_guard", .label = "Sound Crash Guard", .default_value = 1},
    {
        .section = "tuning",
        .name = "culling",
        .label = "Unit Detail Culling",
        .tooltip = "Skips drawing very small parts of distant units. Off: every part, as the game draws them.",
        .kind = FixValue::CULLING,
        .default_value = 1,
    },
    {.section = "tuning", .name = "menu_textures_kept", .label = "Menu Textures Kept", .kind = FixValue::COUNT},
    {.section = "mod", .name = "self_update", .label = "Self-Update",
     .tooltip = "Updates the Engine Performance Fix from its Steam Workshop item.", .default_value = 1},
    {.section = "mod", .name = "crash_logging", .label = "Crash Logging"},
}};

// heapfix.ini keys never shown: the start window's Waving Flags owns flag_wave (one switch, one owner).
inline constexpr std::array<std::string_view, 1> FIX_KEYS_NOT_SHOWN = {"flag_wave"};

// One row of the CPU page: a start file's key as its owner reads it.
struct FileRow {
  // heapfix.ini's section. Empty: settings.txt's row (no sections), or a heapfix.ini key whose section is not known
  // (read-only, never written).
  std::string section;
  std::string key;
  std::string label;
  std::string tooltip;
  std::span<const Choice> choices;  // empty: a count, 1 .. FIX_COUNT_MAX
  int value = -1;                   // a choice's index or the count; -1: no choice fits the file's value
  std::optional<std::string> text;  // the file's value; none: not in the file
  bool editable = false;

  // The same reading as `other` (the pending note: the file now against this start's): another spelling of the same
  // choice reads the same, a value no choice fits by its text.
  [[nodiscard]] bool ReadsAs(const FileRow& other) const {
    return value == other.value && (value >= 0 || text == other.text);
  }
};

// A row's tooltip, "(default)" added when the file lacks the key (the row shows its owner's default).
inline std::string Tooltip(std::string_view tooltip, bool in_file) {
  std::string text(tooltip);
  if (!in_file) {
    text += (text.empty() ? "(default)" : "\n(default)");
  }
  return text;
}

// A test for names equal to `name` in either case (Sol's names and the profile API's keys).
inline auto NamedLike(std::string_view name) {
  return [name](std::string_view other) { return other.size() == name.size() && StartsWithNoCase(other, name); };
}

// <exe dir>\settings.txt's rows: the keys of START_KEYS the running loader reads (`loader_version`: attila64.exe's
// file version; the fix's three only while it is installed, Upscaling only with FrameBridge, as the start window),
// each as Sol's loader reads it, then every other key of the file but START_KEYS_NOT_SHOWN, in the file's order.
inline std::vector<FileRow> StartWindowRows(const std::filesystem::path& exe_dir,
                                            const std::optional<std::array<uint16_t, 4>>& loader_version,
                                            bool fix_installed) {
  const std::filesystem::path file = exe_dir / L"settings.txt";
  // Sol's framebridge_available (S28 picker.c:236-251): helper64.exe in <exe dir>\..\FrameBridge or the Workshop copy
  // (the loader copies it in at the next start).
  std::error_code error;
  const bool framebridge_installed =
      std::filesystem::exists(exe_dir / L"..\\FrameBridge\\helper64.exe", error)
      || std::filesystem::exists(exe_dir / L"..\\..\\..\\workshop\\content\\325610\\3828791891\\FrameBridge\\helper64.exe", error);
  std::vector<FileRow> rows;
  bool fix_on = true;
  for (const StartKey& key : START_KEYS) {
    if (loader_version.value_or(std::array<uint16_t, 4>{}) < key.first_loader) continue;
    if (key.fix_part != FixPart::NONE && !fix_installed) continue;
    if (key.test == StartTest::UPSCALING && !framebridge_installed) continue;
    const std::optional<std::string> text = Value(file, key.name);
    size_t choice = 0;
    switch (key.test) {
      case StartTest::INTERFACE_SIZE: {
        const auto percent = [](const Choice& size) { return std::atoi(size.value); };
        choice = std::ranges::find(key.choices, 0, percent) - key.choices.begin();  // Auto: like1080p
        if (text.has_value() && !StartsWithNoCase(*text, "like1080p") && !StartsWithNoCase(*text, "auto")) {
          const int text_percent = std::atoi(text->c_str());
          if (const auto size = std::ranges::find(key.choices, text_percent, percent);
              text_percent > 0 && size != key.choices.end()) {
            choice = size - key.choices.begin();
          }
        }
        break;
      }
      case StartTest::SHADOW_REFRESH:
        if (text.has_value() && text->size() == 1 && (*text)[0] >= '1' && (*text)[0] <= '4') {
          choice = (*text)[0] - '1';
        }
        break;
      case StartTest::EXACT_SECOND:
        choice = (text == key.choices[1].value ? 1 : 0);
        break;
      case StartTest::OFF_IF_ZERO:
        choice = (text.has_value() && text->starts_with('0') ? 0 : 1);
        break;
      case StartTest::UPSCALING:
        if (text.has_value()) {
          choice = (text->starts_with('0') ? 0 : 1);
        } else {
          // Sol's framebridge_says_off (:253-263): any line, unlike FrameBridgeOff (the file as this start has it).
          bool says_off = false;
          ForEachLine(exe_dir / L"..\\FrameBridge\\settings.txt", [&says_off](std::string_view line) {
            says_off = says_off || StartsWithNoCase(line, "addon=off");
          });
          choice = (says_off ? 0 : 1);
        }
        break;
    }
    if (key.fix_part == FixPart::SWITCH) {
      fix_on = (choice == 1);
    }
    rows.push_back({
        .section = {},
        .key = key.name,
        .label = key.label,
        .tooltip = Tooltip(key.tooltip, text.has_value()),
        .choices = key.choices,
        .value = static_cast<int>(choice),
        .text = text,
        .editable = (key.fix_part != FixPart::PART || fix_on),
    });
  }
  // The other keys, each once, as the file first spells them.
  std::vector<std::string> names;
  ForEachLine(file, [&](std::string_view line) {
    const std::string_view name = line.substr(0, line.find('='));
    if (name.empty() || name.size() == line.size()) return;
    if (std::ranges::any_of(START_KEYS, NamedLike(name), [](const StartKey& key) { return std::string_view(key.name); })
        || std::ranges::any_of(START_KEYS_NOT_SHOWN, NamedLike(name)) || std::ranges::any_of(names, NamedLike(name))) {
      return;
    }
    names.emplace_back(name);
  });
  for (const std::string& name : names) {
    std::optional<std::string> text = Value(file, name);
    const auto choice = std::ranges::find_if(OFF_ON, [&text](const Choice& off_on) { return text == off_on.value; });
    const int value = (choice == OFF_ON.end() ? -1 : static_cast<int>(choice - OFF_ON.begin()));
    rows.push_back({
        .section = {},
        .key = name,
        .label = name,
        .tooltip = {},
        .choices = OFF_ON,
        .value = value,
        .text = std::move(text),
        .editable = (value >= 0),
    });
  }
  return rows;
}

// heapfix.ini's rows (`ini`, read with the profile API): the keys of FIX_KEYS (a key in the file in the file's
// section), then every other key of the file but FIX_KEYS_NOT_SHOWN, in the file's order. Read-only without the file:
// the fix writes it with its defaults at its first start, and a write here must not stand in for that.
inline std::vector<FileRow> FixRows(const std::filesystem::path& ini) {
  // The profile API's NUL-separated lists (section names with no section, a section's keys with no key) and values.
  const auto read = [&ini](const wchar_t* section, const wchar_t* key) {
    std::wstring text(256, L'\0');
    for (;;) {
      const DWORD length =
          GetPrivateProfileStringW(section, key, L"", text.data(), static_cast<DWORD>(text.size()), ini.c_str());
      // Cut short: size - 2 for a list, size - 1 for a value.
      if (length + 2 < text.size()) {
        text.resize(length);
        return text;
      }
      text.resize(text.size() * 2);
    }
  };
  const auto split = [](const std::wstring& list) {
    std::vector<std::wstring> names;
    for (size_t at = 0; at < list.size();) {
      const size_t end = std::min(list.find(L'\0', at), list.size());
      if (end > at) {
        names.emplace_back(list, at, end - at);
      }
      at = end + 1;
    }
    return names;
  };
  const auto narrow = [](const std::wstring& wide) {
    const int wide_size = static_cast<int>(wide.size());
    std::string text(WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_size, nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_size, text.data(), static_cast<int>(text.size()), nullptr, nullptr);
    return text;
  };
  struct Entry {
    std::string section;
    std::string key;
    std::string value;
  };
  std::vector<Entry> entries;
  for (const std::wstring& section : split(read(nullptr, nullptr))) {
    for (const std::wstring& key : split(read(section.c_str(), nullptr))) {
      entries.push_back({
          .section = narrow(section),
          .key = narrow(key),
          .value = narrow(read(section.c_str(), key.c_str())),
      });
    }
  }
  std::error_code error;
  const bool found = std::filesystem::exists(ini, error);
  std::vector<FileRow> rows;
  const auto add = [&rows](std::string section, std::string key, std::string label, const char* tooltip, FixValue kind,
                           int default_value, const std::optional<std::string>& text, bool writable) {
    std::span<const Choice> choices;
    if (kind == FixValue::SWITCH) {
      choices = OFF_ON;
    } else if (kind == FixValue::CULLING) {
      choices = FIX_CULLINGS;
    }
    int value = default_value;
    if (text.has_value()) {
      value = -1;
      if (choices.empty()) {
        int count = 0;
        const char* end = text->data() + text->size();
        if (const auto [at, result] = std::from_chars(text->data(), end, count);
            result == std::errc{} && at == end && count >= 1 && count <= FIX_COUNT_MAX) {
          value = count;
        }
      } else if (const auto choice = std::ranges::find_if(choices, NamedLike(*text), &Choice::value);
                 choice != choices.end()) {
        value = static_cast<int>(choice - choices.begin());
      }
    }
    rows.push_back({
        .section = std::move(section),
        .key = std::move(key),
        .label = std::move(label),
        .tooltip = Tooltip(tooltip, text.has_value()),
        .choices = choices,
        .value = value,
        .text = text,
        .editable = (writable && value >= 0),
    });
  };
  for (const FixKey& key : FIX_KEYS) {
    const auto entry = std::ranges::find_if(entries, NamedLike(key.name), &Entry::key);
    if (entry != entries.end()) {
      add(entry->section, key.name, key.label, key.tooltip, key.kind, key.default_value, entry->value, found);
    } else {
      add((key.section == nullptr ? "" : key.section), key.name, key.label, key.tooltip, key.kind, key.default_value,
          std::nullopt, found && key.section != nullptr);
    }
  }
  for (const Entry& entry : entries) {
    if (std::ranges::any_of(FIX_KEYS, NamedLike(entry.key), [](const FixKey& key) { return std::string_view(key.name); })
        || std::ranges::any_of(FIX_KEYS_NOT_SHOWN, NamedLike(entry.key))) {
      continue;
    }
    add(entry.section, entry.key, entry.key, "", FixValue::SWITCH, -1, entry.value, found);
  }
  return rows;
}

// Writes `row`'s value (row.value: a choice's index or the count) into `file`, its owner's way; false: the write
// failed. Only a player's change writes, and nothing is ever removed.
// - settings.txt (row.section empty): Sol's settings_put (S28 picker.c:72-92): no write when the value is already
//   the same (strcmp); else every non-empty line not naming the key kept in order, `key=value` appended. Sol's own
//   write keeps 31 lines; this keeps every line. Through settings.txt.tmp, moved over the file, so a reader never sees
//   half a file.
// - heapfix.ini: WritePrivateProfileStringW for the one key: every comment and other key kept, the key added to its
//   section when missing.
[[nodiscard]] inline bool Write(const std::filesystem::path& file, const FileRow& row) {
  const std::string value = (row.choices.empty() ? std::to_string(row.value) : row.choices[row.value].value);
  if (row.section.empty()) {
    if (Value(file, row.key) == value) return true;
    std::vector<std::string> kept;
    ForEachLine(file, [&](std::string_view line) {
      if (!line.empty() && !LineNames(line, row.key)) {
        kept.emplace_back(line);
      }
    });
    kept.push_back(row.key + "=" + value);
    const std::filesystem::path temporary = file.native() + L".tmp";
    std::ofstream stream(temporary, std::ios::trunc);
    for (const std::string& line : kept) {
      stream << line << '\n';
    }
    stream.close();
    return !stream.fail() && MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
  }
  const auto widen = [](std::string_view text) {
    std::wstring wide(MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), static_cast<int>(wide.size()));
    return wide;
  };
  return WritePrivateProfileStringW(widen(row.section).c_str(), widen(row.key).c_str(), widen(value).c_str(),
                                    file.c_str())
         != FALSE;
}

}  // namespace twattila::start_files
