/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../twrome2/engine_tweaks.hpp"

// Troy's engine tweaks. Its static initializers copy each tweak's name into a heap string instead of passing the name's
// address (lea rdx, [name]: what tw::engine_tweaks::Find reads), so Find finds none of them here. Each object keeps the
// name's length at +0x48, the string at +0x50 and its value at +0x60 (Pharaoh's), built as
//   movups / movsd xmm0, [rip + name] (or mov r64, [rip + name]); then within 0x20 bytes mov [rip + object + 0x50], rax;
//   and within -0x10..+0x80 of the load: mov dword ptr [rip + object + 0x48], <the name's length> and the default into
//   object + 0x60 in the tweak's kind (movss: float, mov dword: int, mov byte: flag); within -0x40..+0xC0 its vtable stored.
// A tweak counts only with all of these and the length in its object as the code stores it.
// tools\tw\troy\par_t_troy_tweaks.py does the same on the decrypted image (167 tweaks); test\tw_troy_tweaks runs this.
namespace twtroy::tweak_objects {

inline void Find(const tw::engine_tweaks::Sections& sections, std::span<tw::engine_tweaks::Option*> options) {
  std::unordered_map<std::string_view, std::vector<tw::engine_tweaks::Tweak*>> by_name;
  std::array<bool, 256> first_letters = {};
  size_t longest = 0;
  for (tw::engine_tweaks::Option* option : options) {
    for (tw::engine_tweaks::Tweak& tweak : option->tweaks) {
      if (tweak.value != nullptr || tweak.name.empty() || !tweak.code_pattern.empty()) continue;
      by_name[tweak.name].push_back(&tweak);
      first_letters[static_cast<uint8_t>(tweak.name.front())] = true;
      longest = std::max(longest, tweak.name.size());
    }
  }
  if (by_name.empty()) return;
  // The names: zero-terminated, after another string's terminator (as Find reads them).
  std::unordered_map<const uint8_t*, std::string_view> names;
  for (const auto* memories : {&sections.data, &sections.code}) {
    for (const std::string_view memory : *memories) {
      for (size_t at = 1; at < memory.size(); ++at) {
        if (memory[at - 1] != '\0' || !first_letters[static_cast<uint8_t>(memory[at])]) continue;
        const std::string_view rest = memory.substr(at, longest + 1);
        const size_t length = rest.find('\0');
        if (length == std::string_view::npos) continue;
        if (const auto found = by_name.find(rest.substr(0, length)); found != by_name.end()) {
          names.emplace(reinterpret_cast<const uint8_t*>(memory.data() + at), found->first);
        }
        at += length;
      }
    }
  }
  if (names.empty()) return;
  const auto rip_target = [](const uint8_t* displacement, const uint8_t* end) {
    int32_t offset;
    std::memcpy(&offset, displacement, sizeof(offset));
    return end + offset;
  };
  for (const std::string_view memory : sections.code) {
    const auto* begin = reinterpret_cast<const uint8_t*>(memory.data());
    // Each load's window reaches 0x40 bytes back and 0xC0 ahead.
    for (size_t at = 0x40; at + 0xD0 < memory.size(); ++at) {
      const uint8_t* code = begin + at;
      size_t modrm = 0;
      if (code[0] == 0x0F && code[1] == 0x10) {
        modrm = 2;  // movups xmm, [rip + x]
      } else if (code[0] == 0xF2 && code[1] == 0x0F && code[2] == 0x10) {
        modrm = 3;  // movsd xmm, [rip + x]
      } else if ((code[0] == 0x48 || code[0] == 0x4C) && code[1] == 0x8B) {
        modrm = 2;  // mov r64, [rip + x]
      } else {
        continue;
      }
      if ((code[modrm] & 0xC7) != 0x05) continue;
      const uint8_t* load_end = code + modrm + 5;
      const auto name = names.find(rip_target(load_end - 4, load_end));
      if (name == names.end()) continue;
      const uint8_t* name_pointer = nullptr;
      for (const uint8_t* store = load_end; store < load_end + 0x20; ++store) {
        if (store[0] == 0x48 && store[1] == 0x89 && store[2] == 0x05) {
          name_pointer = rip_target(store + 3, store + 7);
          break;
        }
      }
      if (name_pointer == nullptr) continue;
      const uint8_t* value = name_pointer + 0x10;
      bool length_stored = false;
      bool float_default = false;
      bool int_default = false;
      bool flag_default = false;
      for (const uint8_t* store = code - 0x10; store < code + 0x80; ++store) {
        if (store[0] == 0xC7 && store[1] == 0x05) {
          const uint8_t* target = rip_target(store + 2, store + 10);
          uint32_t immediate;
          std::memcpy(&immediate, store + 6, sizeof(immediate));
          length_stored |= (target == name_pointer - 8 && immediate == name->second.size());
          int_default |= (target == value);
        } else if (store[0] == 0xC6 && store[1] == 0x05) {
          flag_default |= (rip_target(store + 2, store + 7) == value);
        } else if (store[0] == 0xF3 && store[1] == 0x0F && store[2] == 0x11 && (store[3] & 0xC7) == 0x05) {
          float_default |= (rip_target(store + 4, store + 8) == value);
        }
      }
      // Its class: a qword stored at the object itself (the vtable: mov qword ptr [rip + object], r64). Other named objects with
      // the same string layout (enums, file kinds: 155 in Troy) have none there.
      bool class_stored = false;
      for (const uint8_t* store = code - 0x40; store < code + 0xC0 && !class_stored; ++store) {
        class_stored = (store[0] == 0x48 || store[0] == 0x4C) && store[1] == 0x89 && (store[2] & 0xC7) == 0x05
                       && rip_target(store + 3, store + 7) == name_pointer - 0x50;
      }
      if (!length_stored || !class_stored) continue;
      // The object as the initializer left it (in the running game: its length set, its string allocated).
      uint32_t object_length;
      std::memcpy(&object_length, name_pointer - 8, sizeof(object_length));
      uint64_t object_string;
      std::memcpy(&object_string, name_pointer, sizeof(object_string));
      if (object_length != name->second.size() || object_string == 0) continue;
      for (tw::engine_tweaks::Tweak* tweak : by_name[name->second]) {
        const bool kind = tweak->flag ? flag_default : (tweak->integer ? int_default : float_default);
        if (tweak->value == nullptr && kind) tweak->value = const_cast<uint8_t*>(value);
      }
    }
  }
}

}  // namespace twtroy::tweak_objects
