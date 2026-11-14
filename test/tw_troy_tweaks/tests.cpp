// Troy's engine tweaks: twtroy::tweak_objects::Find (src/games/twtroy/tweak_objects.hpp) on Troy's decrypted image (a
// memory-layout copy of Troy.exe saved from the running game, only read: its size is SizeOfImage, every section at its RVA).
// 1. Every tweak object its constructors give (tools\tw\troy\par_t_troy_tweaks.txt: name, value RVA, kind) found at that
//    RVA with its kind; the same names asked with another kind stay unfound; names Troy keeps only as strings or not at all
//    (Attila's, Pharaoh's) stay unfound.
// 2. The shared tw::engine_tweaks::Find (lea rdx / lea r8, [name]) on the same names: what Troy's rows had before.
// 3. Troy's Graphics 2 of 2 rows as ConfigureAddon builds them (Attila's tests in ATTILA_TESTS, then MoreTests()): every
//    engine value found, none read at the game's start (no at-start thread).
// 4. Shadow Map Size (src/games/twtroy/shadow_map.hpp): its pattern names the shadow table at RVA 0x2e81920, the top row
//    (quality 4) 4 cascades, 6144, 3072 and the row below 4096; Set writes the size and half of it there (the row below
//    unchanged), Uninstall the game's bytes back. CONTROL: Pharaoh's pattern (twpharaohdynasties/shadow_map.hpp) finds nothing.
//    Two Shadow Cascades Under Rays: the split distances table (RVA 0x2e81970, both readers' pattern) and the lowest row (2,
//    1024, 1024); on, the top row 2 cascades with the tile the size, its battle and campaign split rows quality 1's, nothing
//    else; off and Uninstall the game's bytes; CONTROL the lowest row 3 cascades: the size alone.
// tests.exe <troy.exe.memory.bin> <par_t_troy_tweaks.txt>
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "src/games/twtroy/more_tests.h"
#include "src/games/twtroy/shadow_map.hpp"
#include "src/games/twtroy/tweak_objects.hpp"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Troy tweak test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { std::printf("  log: %s\n", message); }

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: troy_tweaks.exe <troy.exe.memory.bin> <par_t_troy_tweaks.txt>\n");
    return 2;
  }
  std::ifstream image_file(argv[1], std::ios::binary | std::ios::ate);
  CHECK(image_file.good());
  const auto image_size = static_cast<size_t>(image_file.tellg());
  auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  CHECK(base != nullptr);
  image_file.seekg(0);
  image_file.read(reinterpret_cast<char*>(base), static_cast<std::streamsize>(image_size));
  CHECK(image_file.good());
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  CHECK(nt->OptionalHeader.SizeOfImage == image_size);
  const auto sections = tw::engine_tweaks::ModuleSections(reinterpret_cast<HMODULE>(base));
  std::printf("image %zu MB: %zu code and %zu data sections scanned\n", image_size >> 20, sections.code.size(), sections.data.size());

  struct Expected {
    std::string name;
    size_t value_rva = 0;
    std::string kind;
  };
  std::deque<Expected> expected;
  std::ifstream list(argv[2]);
  for (std::string line; std::getline(list, line);) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream fields(line);
    Expected row;
    std::string object_word, object, value_word, value;
    std::getline(fields, row.name, '\t');
    fields >> object_word >> object >> value_word >> value >> row.kind;
    row.value_rva = std::stoull(value, nullptr, 16);
    expected.push_back(row);
  }
  CHECK(expected.size() > 150);

  // 1. Each name with its kind, again with another kind, and names Troy has no object for.
  const auto tweak_of = [](std::string_view name, const std::string& kind) {
    return tw::engine_tweaks::Tweak{.name = name, .integer = kind == "int", .flag = kind == "bool"};
  };
  std::deque<tw::engine_tweaks::Option> right;
  std::deque<tw::engine_tweaks::Option> wrong;
  for (const Expected& row : expected) {
    right.push_back({.tweaks = {tweak_of(row.name, row.kind)}});
    const std::string other = (row.kind == "float") ? "bool" : ((row.kind == "int") ? "float" : "int");
    wrong.push_back({.tweaks = {tweak_of(row.name, other)}});
  }
  // Attila's / Pharaoh's names (absent from the image), two Troy holds only as strings (a shader constant's name, an audio
  // setting's) and two named objects with a tweak's string layout and an int-like default but no class (no tweak).
  tw::engine_tweaks::Option absent = {.tweaks = {{.name = "battle_building_lod0_distance"}, {.name = "OIT_GRASS_MAX_DISTANCE"},
                                                 {.name = "vegetation_hstrength"}, {.name = "grass_alpha_mul"},
                                                 {.name = "SOUND_DEFAULT_BUFFER_SIZE_IN_KB", .integer = true},
                                                 {.name = "shallow_water", .integer = true}, {.name = "ALLOWED_DELTA_SCALE", .integer = true}}};
  std::vector<tw::engine_tweaks::Option*> options;
  for (auto& option : right) options.push_back(&option);
  for (auto& option : wrong) options.push_back(&option);
  options.push_back(&absent);
  const DWORD started = GetTickCount();
  twtroy::tweak_objects::Find(sections, options);
  std::printf("Find: %lu ms\n", GetTickCount() - started);
  size_t found = 0;
  for (size_t index = 0; index < expected.size(); ++index) {
    const void* value = right[index].tweaks[0].value;
    if (value != base + expected[index].value_rva) {
      std::printf("FAIL %s (%s): %p, expected RVA 0x%zx\n", expected[index].name.c_str(), expected[index].kind.c_str(), value,
                  expected[index].value_rva);
      return 1;
    }
    ++found;
    CHECK(wrong[index].tweaks[0].value == nullptr);
  }
  for (const auto& tweak : absent.tweaks) CHECK(tweak.value == nullptr);
  std::printf("1. %zu of %zu tweak objects at their constructors' RVAs; another kind and %zu absent names: none found\n", found,
              expected.size(), absent.tweaks.size());

  // 2. The shared Find on the same names (each with its kind): what Troy's rows had until now.
  std::deque<tw::engine_tweaks::Option> shared;
  std::vector<tw::engine_tweaks::Option*> shared_options;
  for (const Expected& row : expected) {
    shared.push_back({.tweaks = {tweak_of(row.name, row.kind)}});
    shared_options.push_back(&shared.back());
  }
  tw::engine_tweaks::internal::Find(sections, shared_options);
  size_t shared_found = 0;
  for (const auto& option : shared) shared_found += (option.tweaks[0].value != nullptr) ? 1 : 0;
  std::printf("2. tw::engine_tweaks::Find on the same names: %zu found\n", shared_found);
  CHECK(shared_found == 0);

  // 3. Troy's Graphics 2 of 2 rows (ConfigureAddon's order).
  auto tests = twattila::engine_tests::Only(twattila::engine_tests::Attila(), more_tests::ATTILA_TESTS);
  CHECK(tests.size() == more_tests::ATTILA_TESTS.size());
  {
    auto more = more_tests::MoreTests();
    tests.insert(tests.end(), more.begin(), more.end());
  }
  std::vector<tw::engine_tweaks::Option*> page;
  for (auto& test : tests) {
    CHECK(!test.at_start);
    page.push_back(&test.option);
  }
  twtroy::tweak_objects::Find(sections, page);
  size_t values = 0;
  for (const auto& test : tests) {
    for (const auto& tweak : test.option.tweaks) {
      if (tweak.value == nullptr) {
        std::printf("FAIL row %s: %.*s not found\n", test.key.c_str(), static_cast<int>(tweak.name.size()), tweak.name.data());
        return 1;
      }
      ++values;
    }
  }
  std::printf("3. Troy's %zu Graphics 2 of 2 rows: all %zu engine values found, none at start\n", tests.size(), values);

  // 4. Shadow Map Size and Two Shadow Cascades Under Rays.
  const auto read_ints = [](const uint8_t* at, size_t count) {
    std::vector<uint32_t> ints(count);
    std::memcpy(ints.data(), at, count * sizeof(uint32_t));
    return ints;
  };
  namespace shadow = twtroy::shadow_map::internal;
  CHECK(tw::engine_tweaks::FindPattern(sections.code, "48 8D 05 ?? ?? ?? ?? 48 03 D0 8B 42 04 F3 48 0F 2A C0 F3 0F 11 83 50 2A 00 00") == nullptr);
  uint8_t* rows = twtroy::shadow_map::FindRows(sections);
  CHECK(rows == base + 0x2e81920);
  uint8_t* top = rows + (4 * 16);
  CHECK(read_ints(top, 3) == (std::vector<uint32_t>{4, 6144, 3072}));
  CHECK(read_ints(top - 16, 4) == (std::vector<uint32_t>{4, 4096, 2048, 0x3F800000}));
  CHECK(read_ints(rows + 16, 3) == (std::vector<uint32_t>{2, 1024, 1024}));  // the lowest: 2 cascades, the tile the size
  // The split distances: every hit of the pattern (the two readers) names the table right after the sizes.
  uint8_t* splits = twtroy::shadow_map::FindSplits(sections);
  CHECK(splits == base + 0x2e81970);
  {
    std::vector<std::string_view> after;
    for (const std::string_view memory : sections.code) {
      const auto* begin = reinterpret_cast<const uint8_t*>(memory.data());
      const uint8_t* first = tw::engine_tweaks::FindPattern({memory}, shadow::SPLITS_READ);
      if (first != nullptr) after.push_back(memory.substr(first - begin + 1));
    }
    const auto second = twtroy::shadow_map::FindSplits({.code = after});
    CHECK(second == splits);
  }
  const auto read_floats = [](const uint8_t* at) {
    std::array<float, 4> floats;
    std::memcpy(floats.data(), at, sizeof(floats));
    return floats;
  };
  CHECK((read_floats(splits + (4 * 16)) == std::array<float, 4>{30.f, 60.f, 150.f, 300.f}));  // battle, quality 4
  CHECK((read_floats(splits + (1 * 16)) == std::array<float, 4>{30.f, 120.f, 160.f, 160.f}));  // battle, quality 1
  CHECK((read_floats(splits + (9 * 16)) == std::array<float, 4>{4.f, 9.f, 18.f, 35.f}));  // campaign, quality 4
  CHECK((read_floats(splits + (6 * 16)) == std::array<float, 4>{6.f, 11.f, 11.f, 11.f}));  // campaign, quality 1
  const std::vector<uint8_t> game_rows(rows, rows + (6 * 16));
  const std::vector<uint8_t> game_splits(splits, splits + (10 * 16));
  const auto changed = [](const uint8_t* now, const std::vector<uint8_t>& game) {
    std::vector<size_t> at;
    for (size_t index = 0; index < game.size(); ++index) {
      if (now[index] != game[index]) at.push_back(index);
    }
    return at;
  };
  shadow::rows = rows;
  shadow::splits = splits;
  std::memcpy(shadow::original.data(), top, shadow::original.size());
  std::memcpy(shadow::original_splits.data(), splits + (4 * 16), 16);
  std::memcpy(shadow::original_splits.data() + 16, splits + (9 * 16), 16);
  // Off: the size alone, as before the row.
  twtroy::shadow_map::Set(16384, false);
  CHECK(read_ints(top, 3) == (std::vector<uint32_t>{4, 16384, 8192}));
  CHECK(read_ints(top - 16, 3) == (std::vector<uint32_t>{4, 4096, 2048}));
  CHECK(changed(splits, game_splits).empty());
  // On: the top row 2 cascades with the tile the size; its split rows quality 1's; nothing else.
  twtroy::shadow_map::Set(16384, true);
  CHECK(read_ints(top, 3) == (std::vector<uint32_t>{2, 16384, 16384}));
  CHECK((read_floats(splits + (4 * 16)) == std::array<float, 4>{30.f, 120.f, 160.f, 160.f}));
  CHECK((read_floats(splits + (9 * 16)) == std::array<float, 4>{6.f, 11.f, 11.f, 11.f}));
  for (const size_t index : changed(rows, game_rows)) CHECK(index >= 4 * 16 && index < 4 * 16 + 12);
  for (const size_t index : changed(splits, game_splits)) CHECK((index >= 4 * 16 && index < 5 * 16) || (index >= 9 * 16 && index < 10 * 16));
  twtroy::shadow_map::Set(0, true);
  CHECK(read_ints(top, 3) == (std::vector<uint32_t>{2, 6144, 6144}));
  // Off again, and Uninstall: the game's bytes.
  twtroy::shadow_map::Set(0, false);
  CHECK(changed(rows, game_rows).empty() && changed(splits, game_splits).empty());
  twtroy::shadow_map::Set(16384, true);
  twtroy::shadow_map::Uninstall();
  CHECK(changed(rows, game_rows).empty() && changed(splits, game_splits).empty());
  // CONTROL: the lowest row not 2 cascades (another build or a mod): on writes the size alone.
  std::array<uint8_t, 4> lowest_count;
  std::memcpy(lowest_count.data(), rows + 16, 4);
  const uint32_t three = 3;
  std::memcpy(rows + 16, &three, 4);
  twtroy::shadow_map::Set(16384, true);
  CHECK(read_ints(top, 3) == (std::vector<uint32_t>{4, 16384, 8192}));
  CHECK(changed(splits, game_splits).empty());
  twtroy::shadow_map::Uninstall();
  std::memcpy(rows + 16, lowest_count.data(), 4);
  CHECK(changed(rows, game_rows).empty());
  std::printf("4. Shadow Map Size: the sizes table at RVA 0x%zx (top row 4, 6144, 3072; the lowest 2, 1024, 1024); off: 16384 written "
              "as 16384 / 8192, the row below and the split distances unchanged. Two cascades: the top row 2, 16384, 16384 (the game's "
              "size: 2, 6144, 6144), its split rows (RVA 0x%zx, both readers) quality 1's (battle 30 / 120 m, campaign 6 / 11), nothing "
              "else; back: the game's bytes; CONTROL the lowest row 3 cascades: the size alone\n",
              static_cast<size_t>(rows - base), static_cast<size_t>(splits - base));
  std::printf("PASS\n");
  return 0;
}
