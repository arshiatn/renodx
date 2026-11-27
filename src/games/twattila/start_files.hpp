/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

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

// Sol's settings_get (S28 picker.c:54-70): `name`'s value in a file of his format: the rest of the last line that
// starts with the name (in either case) and '=', not trimmed. None: no such line, or no file.
inline std::optional<std::string> Value(const std::filesystem::path& file, std::string_view name) {
  std::optional<std::string> value;
  ForEachLine(file, [name, &value](std::string_view line) {
    if (line.size() > name.size() && line[name.size()] == '=' && StartsWithNoCase(line, name)) {
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

}  // namespace twattila::start_files
