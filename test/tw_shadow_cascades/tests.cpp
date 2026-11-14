// Two Shadow Cascades Under Rays in Warhammer and Warhammer II (src/games/twwarhammer2/shadow_cascades.hpp) over the shipped
// executables (read from disk, laid out as the loader maps them: every section at its RVA; only read), through the engine_tweaks
// code the add-ons run (Find, then Apply after Set, as every present):
// 1. The setup's pattern once in the executable; every tweak found inside it.
// 2. Off (and before any present): the game's bytes, Warhammer II's Shadow Map Size alone as before the row (1.5: its two
//    sizes and the tile x 1.5, nothing else).
// 3. On: Shadows 4's count, split row and tile become Shadows 1's own (2, 0, the tile the size: Warhammer II at both Shadow
//    Map Sizes; Warhammer both of its texture limits' branches), read from the same bytes; no other byte changes.
// 4. Off again, then the game's sizes: the game's bytes.
// 5. CONTROL: Shadows 1's count not 2 (another build or a mod): nothing found, nothing written with the row on.
// tests.exe <warhammer.exe> <Warhammer2.exe>
#include <windows.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "src/games/twwarhammer2/shadow_cascades.hpp"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Shadow cascades test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { std::printf("  log: %s\n", message); }

namespace cascades = twwarhammer2::shadow_cascades;

namespace {

struct Image {
  uint8_t* base = nullptr;
  size_t size = 0;
};

// The file as the loader lays it out (headers, then each section's raw data at its RVA); null base if it can't.
Image Map(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  const std::string file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if (file.size() < sizeof(IMAGE_DOS_HEADER)) return {};
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data())->e_lfanew);
  Image image = {.size = nt->OptionalHeader.SizeOfImage};
  image.base = static_cast<uint8_t*>(VirtualAlloc(nullptr, image.size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (image.base == nullptr) return {};
  std::memcpy(image.base, file.data(), nt->OptionalHeader.SizeOfHeaders);
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    const size_t bytes = std::min<size_t>(section->SizeOfRawData, image.size - section->VirtualAddress);
    std::memcpy(image.base + section->VirtualAddress, file.data() + section->PointerToRawData, bytes);
  }
  return image;
}

uint32_t Read(const uint8_t* at) {
  uint32_t value;
  std::memcpy(&value, at, sizeof(value));
  return value;
}

// One game: its pattern's span, the offsets of Shadows 1's count / tile inside it, Shadows 4's tiles.
int Check(const char* path, cascades::Game game) {
  const bool warhammer_2 = (game == cascades::Game::WARHAMMER_2);
  const std::string_view setup = warhammer_2 ? cascades::WARHAMMER_2_SETUP : cascades::WARHAMMER_SETUP;
  const char* name = warhammer_2 ? "Warhammer II" : "Warhammer";
  Image image = Map(path);
  CHECK(image.base != nullptr);
  auto sections = tw::engine_tweaks::ModuleSections(reinterpret_cast<HMODULE>(image.base));
  uint8_t* found = tw::engine_tweaks::FindPattern(sections.code, setup);
  CHECK(found != nullptr);
  const size_t span = (setup.size() + 1) / 3;
  {
    // Once: nothing past the first match.
    std::vector<std::string_view> after;
    for (const std::string_view memory : sections.code) {
      const auto* begin = reinterpret_cast<const uint8_t*>(memory.data());
      if (found >= begin && found < begin + memory.size()) after.push_back(memory.substr(found - begin + 1));
    }
    CHECK(tw::engine_tweaks::FindPattern(after, setup) == nullptr);
  }
  const std::vector<uint8_t> game_bytes(found, found + span);
  // Shadows 1's own: its count (2), its tile and size (Warhammer II: both multiplier << 11; Warhammer: 1024 and 1024.f).
  const size_t lowest_count = warhammer_2 ? 245 : 296;
  CHECK(Read(found + lowest_count) == 2);
  if (!warhammer_2) CHECK(Read(found + 286) == 1024 && Read(found + 259) == 0x44800000);

  // The add-on's options: Warhammer II's Shadow Map Size floats (its addon.cpp) and the cascades'.
  tw::engine_tweaks::Option shadow_map = {.tweaks = {{.code_pattern = setup, .code_offset = cascades::WARHAMMER_2_WIDTH},
                                                     {.code_pattern = setup, .code_offset = cascades::WARHAMMER_2_HEIGHT}}};
  cascades::Options options = cascades::Make(game);
  std::vector<tw::engine_tweaks::Option*> list = {&options.count, &options.split_row, &options.tile};
  if (warhammer_2) list.push_back(&shadow_map);
  tw::engine_tweaks::internal::Find(sections, list);
  for (const auto* option : list) {
    for (const auto& tweak : option->tweaks) {
      CHECK(tweak.value >= found && tweak.value < found + span);
    }
  }
  const auto differing = [&](const std::vector<uint8_t>& expected) {
    std::vector<size_t> at;
    for (size_t index = 0; index < span; ++index) {
      if (found[index] != expected[index]) at.push_back(index);
    }
    return at;
  };
  const auto present = [&](bool two, float size_scale) {
    shadow_map.scale = size_scale;
    cascades::Set(&options, two, size_scale);
    tw::engine_tweaks::internal::Apply(list);
  };
  const auto write = [](std::vector<uint8_t>* bytes, size_t at, uint32_t value) { std::memcpy(bytes->data() + at, &value, sizeof(value)); };
  const std::vector<size_t> tiles = warhammer_2 ? std::vector<size_t>{26} : std::vector<size_t>{29, 44};
  const size_t count = warhammer_2 ? 36 : 74;
  const size_t split_row = warhammer_2 ? 46 : 84;

  // 2. Off.
  present(false, 1.f);
  CHECK(differing(game_bytes).empty());
  std::vector<uint8_t> larger = game_bytes;  // Warhammer II's Shadow Map Size 12288
  if (warhammer_2) {
    present(false, 1.5f);
    write(&larger, cascades::WARHAMMER_2_WIDTH, std::bit_cast<uint32_t>(8192.f * 1.5f));
    write(&larger, cascades::WARHAMMER_2_HEIGHT, std::bit_cast<uint32_t>(8192.f * 1.5f));
    write(&larger, 26, 6144);
    CHECK(differing(larger).empty());
    present(false, 1.f);
    CHECK(differing(game_bytes).empty());
  }

  // 3. On: Shadows 1's count and split row (0: its register is zeroed at the function's start), each tile the size.
  std::vector<uint8_t> two = game_bytes;
  write(&two, count, Read(found + lowest_count));
  write(&two, split_row, 0);
  for (const size_t tile : tiles) write(&two, tile, Read(game_bytes.data() + tile) * 2);
  present(true, 1.f);
  const auto changed = differing(game_bytes);
  CHECK(differing(two).empty());
  for (const size_t index : changed) {
    bool inside = (index >= count && index < count + 4) || (index >= split_row && index < split_row + 4);
    for (const size_t tile : tiles) inside = inside || (index >= tile && index < tile + 4);
    CHECK(inside);
  }
  if (warhammer_2) {
    CHECK(Read(found + 26) == 8192);  // the size's 8192.f
    present(true, 1.5f);
    std::vector<uint8_t> two_larger = larger;
    write(&two_larger, count, 2);
    write(&two_larger, split_row, 0);
    write(&two_larger, 26, 12288);
    CHECK(differing(two_larger).empty());
  } else {
    CHECK(Read(found + 29) == 8192 && Read(found + 44) == 4096);  // the constants' 8192.f and 4096.f
  }

  // 4. Off, the game's size: the game's bytes.
  present(false, 1.f);
  CHECK(differing(game_bytes).empty());
  std::printf("%s: setup pattern once (%zu bytes); off: the game's bytes%s; on: %zu bytes differ, Shadows 4's count 2, split "
              "row 0, %zu tile(s) x 2 (Shadows 1's own: count %u); back: the game's bytes\n",
              name, span, warhammer_2 ? ", Shadow Map Size 12288 alone as before (sizes, tile 6144)" : "", changed.size(), tiles.size(),
              Read(found + lowest_count));

  // 5. CONTROL: Shadows 1's count 3: nothing found, nothing written.
  found[lowest_count] = 3;
  std::vector<uint8_t> other = game_bytes;
  other[lowest_count] = 3;
  cascades::Options control = cascades::Make(game);
  std::vector<tw::engine_tweaks::Option*> control_list = {&control.count, &control.split_row, &control.tile};
  tw::engine_tweaks::internal::Find(sections, control_list);
  for (const auto* option : control_list) {
    for (const auto& tweak : option->tweaks) CHECK(tweak.value == nullptr);
  }
  cascades::Set(&control, true, 1.f);
  tw::engine_tweaks::internal::Apply(control_list);
  CHECK(differing(other).empty());
  std::printf("%s CONTROL: Shadows 1's count 3: no tweak found, nothing written with the row on\n", name);
  VirtualFree(image.base, 0, MEM_RELEASE);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: shadow_cascades.exe <warhammer.exe> <Warhammer2.exe>\n");
    return 2;
  }
  if (Check(argv[1], cascades::Game::WARHAMMER) != 0) return 1;
  if (Check(argv[2], cascades::Game::WARHAMMER_2) != 0) return 1;
  std::printf("PASS\n");
  return 0;
}
