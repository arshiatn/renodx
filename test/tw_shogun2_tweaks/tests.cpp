// Shogun 2's engine tweaks: twshogun2::tweak_objects::Find (src/games/twshogun2completeedition/tweak_objects.hpp) on the
// game's engine DLL (empire.retail.x64.dll mapped as an image resource: only read, nothing of it runs; its static
// initializers have not run, so the values themselves are 0 and the defaults are in their constructors' code).
// 1. Every tweak in tools\shogun2\pkg_1114\s2_tweaks_1114.txt (s2_tweaks.py, capstone: name, value RVA, default bits) found at
//    that RVA, the default stored there by its initializer (mov dword ptr [rip + value], default) as listed; names the DLL
//    has no tweak for (Attila's, a C runtime function's name) stay unfound.
// 2. The shared tw::engine_tweaks::internal::Find (lea rdx / lea r8, [name], the value at +0x60) on the same names: what
//    Shogun's rows would have had.
// 3. Shogun 2's engine value rows (Attila's tests in tweak_objects.hpp's ATTILA_TESTS): every value found but Rope Distance's
//    rigid_rope_culling_threshold (absent), none read at the game's start.
// 4. The engine's byte patterns (engine.hpp FindPattern: the hooks and patches of game_dlss, grass_distance, model_detail,
//    particle_resolution, shadow_resolution, terrain_detail and unit_detail): the DLL mapped as an image too (its imports
//    unresolved, its entry point never called: nothing of it runs) so FindPattern finds it by its name; each pattern
//    matches its executable sections exactly once, where FindPattern points. CONTROL: each with its first fixed byte
//    changed matches nowhere and FindPattern gives null.
// 5. The game's DLSS release (game_dlss RELEASE_FEATURE, R21) frees the DLSS pass's own holder and its parameters.
// 6. Terrain detail off gives the image's own values back (detach calls it too).
// tests.exe <empire.retail.x64.dll> <s2_tweaks_1114.txt>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/games/twattila/engine_tests.hpp"
#include "src/games/twshogun2completeedition/engine.hpp"
#include "src/games/twshogun2completeedition/game_dlss.hpp"
#include "src/games/twshogun2completeedition/grass_distance.hpp"
#include "src/games/twshogun2completeedition/model_detail.hpp"
#include "src/games/twshogun2completeedition/particle_resolution.hpp"
#include "src/games/twshogun2completeedition/shadow_resolution.hpp"
#include "src/games/twshogun2completeedition/terrain_detail.hpp"
#include "src/games/twshogun2completeedition/tweak_objects.hpp"
#include "src/games/twshogun2completeedition/unit_detail.hpp"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Shogun 2 tweak test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { std::printf("  log: %s\n", message); }

// Part 4: how often `pattern` ("48 8B ?? ...") matches the executable sections of the image at `base`, and the first match.
static std::pair<size_t, const uint8_t*> Matches(const uint8_t* base, std::string_view pattern) {
  std::vector<int> bytes;
  for (size_t at = 0; at + 1 < pattern.size(); at += 3) {
    bytes.push_back(pattern[at] == '?' ? -1 : std::stoi(std::string(pattern.substr(at, 2)), nullptr, 16));
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  const auto* section = IMAGE_FIRST_SECTION(nt);
  size_t count = 0;
  const uint8_t* first = nullptr;
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
    const uint8_t* begin = base + section->VirtualAddress;
    for (size_t at = 0; at + bytes.size() <= section->Misc.VirtualSize; ++at) {
      size_t matched = 0;
      while (matched < bytes.size() && (bytes[matched] < 0 || begin[at + matched] == bytes[matched])) ++matched;
      if (matched != bytes.size()) continue;
      if (count++ == 0) first = begin + at;
    }
  }
  return {count, first};
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: shogun2_tweaks.exe <empire.retail.x64.dll> <s2_tweaks_1114.txt>\n");
    return 2;
  }
  const std::string dll = argv[1];
  const HMODULE mapped = LoadLibraryExW(std::wstring(dll.begin(), dll.end()).c_str(), nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  CHECK(mapped != nullptr);
  // An image resource's handle carries flags in its low bits.
  auto* base = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(mapped) & ~uintptr_t{3});
  const auto sections = tw::engine_tweaks::ModuleSections(reinterpret_cast<HMODULE>(base));
  std::printf("%zu code and %zu data sections\n", sections.code.size(), sections.data.size());

  struct Expected {
    std::string name;
    size_t value_rva = 0;
    uint32_t default_bits = 0;
  };
  std::deque<Expected> expected;
  std::ifstream list(argv[2]);
  for (std::string line; std::getline(list, line);) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream fields(line);
    Expected row;
    std::string value, bits;
    std::getline(fields, row.name, '\t');
    fields >> value >> bits;
    row.value_rva = std::stoull(value, nullptr, 16);
    row.default_bits = static_cast<uint32_t>(std::stoul(bits, nullptr, 16));
    expected.push_back(row);
  }
  CHECK(expected.size() >= 8);

  // 1. Each listed name; names with no tweak in the DLL: Attila's (absent), a C runtime function's (a string only).
  std::deque<tw::engine_tweaks::Option> listed;
  std::vector<tw::engine_tweaks::Option*> options;
  for (const Expected& row : expected) {
    listed.push_back({.tweaks = {{.name = row.name}}});
    options.push_back(&listed.back());
  }
  tw::engine_tweaks::Option absent = {.tweaks = {{.name = "GRASS_ANGLE_CULL_AMOUNT"}, {.name = "OIT_GRASS_MAX_DISTANCE"},
                                                 {.name = "rigid_rope_culling_threshold"}, {.name = "fmod"}}};
  options.push_back(&absent);
  const DWORD started = GetTickCount();
  twshogun2::tweak_objects::Find(sections, options);
  std::printf("Find: %lu ms\n", GetTickCount() - started);
  for (size_t index = 0; index < expected.size(); ++index) {
    auto* value = static_cast<uint8_t*>(listed[index].tweaks[0].value);
    if (value != base + expected[index].value_rva) {
      std::printf("FAIL %s: %p, expected RVA 0x%zx\n", expected[index].name.c_str(), static_cast<void*>(value), expected[index].value_rva);
      return 1;
    }
    // Its initializer's default: mov dword ptr [rip + value], imm32 somewhere in the code.
    size_t defaults = 0;
    for (const std::string_view memory : sections.code) {
      for (size_t at = memory.find("\xC7\x05"); at != std::string_view::npos && at + 10 <= memory.size(); at = memory.find("\xC7\x05", at + 1)) {
        auto* store = reinterpret_cast<uint8_t*>(const_cast<char*>(memory.data() + at));
        uint32_t bits = 0;
        std::memcpy(&bits, store + 6, sizeof(bits));
        defaults += (tw::engine_tweaks::internal::RipTarget(store + 2, store + 10) == value && bits == expected[index].default_bits) ? 1 : 0;
      }
    }
    CHECK(defaults == 1);
    float as_float = 0.f;
    std::memcpy(&as_float, &expected[index].default_bits, sizeof(as_float));
    std::printf("  %s at +0x%zx, default %g (0x%08x)\n", expected[index].name.c_str(), expected[index].value_rva, as_float,
                expected[index].default_bits);
  }
  for (const auto& tweak : absent.tweaks) CHECK(tweak.value == nullptr);
  std::printf("1. %zu of %zu tweak objects at their RVAs with their defaults; %zu names with no tweak: none found\n", expected.size(),
              expected.size(), absent.tweaks.size());

  // 2. The shared Find on the same names.
  std::deque<tw::engine_tweaks::Option> shared;
  std::vector<tw::engine_tweaks::Option*> shared_options;
  for (const Expected& row : expected) {
    shared.push_back({.tweaks = {{.name = row.name}}});
    shared_options.push_back(&shared.back());
  }
  tw::engine_tweaks::internal::Find(sections, shared_options);
  size_t shared_found = 0;
  for (const auto& option : shared) shared_found += (option.tweaks[0].value != nullptr) ? 1 : 0;
  std::printf("2. tw::engine_tweaks::Find on the same names: %zu found\n", shared_found);
  CHECK(shared_found == 0);

  // 3. Shogun 2's rows.
  auto tests = twattila::engine_tests::Only(twattila::engine_tests::Attila(), twshogun2::tweak_objects::ATTILA_TESTS);
  CHECK(tests.size() == twshogun2::tweak_objects::ATTILA_TESTS.size());
  std::vector<tw::engine_tweaks::Option*> page;
  for (auto& test : tests) {
    CHECK(!test.at_start);
    page.push_back(&test.option);
  }
  twshogun2::tweak_objects::Find(sections, page);
  size_t values = 0;
  for (const auto& test : tests) {
    for (const auto& tweak : test.option.tweaks) {
      if (tweak.name == "rigid_rope_culling_threshold") {
        CHECK(tweak.value == nullptr);
        continue;
      }
      if (tweak.value == nullptr) {
        std::printf("FAIL row %s: %.*s not found\n", test.key.c_str(), static_cast<int>(tweak.name.size()), tweak.name.data());
        return 1;
      }
      CHECK(!tweak.integer && !tweak.flag);
      ++values;
    }
  }
  std::printf("3. Shogun 2's %zu engine value rows: %zu float values found (rigid_rope_culling_threshold absent), none at start\n",
              tests.size(), values);

  // 4. The byte patterns.
  const HMODULE image = LoadLibraryExW(std::wstring(dll.begin(), dll.end()).c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
  CHECK(image != nullptr && twshogun2::engine::Module() == image);
  const std::pair<const char*, std::string_view> patterns[] = {
      {"game_dlss CREATE_FEATURE", twshogun2::game_dlss::internal::CREATE_FEATURE},
      {"game_dlss EVALUATE", twshogun2::game_dlss::internal::EVALUATE},
      {"game_dlss JITTER_CONDITION", twshogun2::game_dlss::internal::JITTER_CONDITION},
      {"game_dlss JITTER_FUNCTION", twshogun2::game_dlss::internal::JITTER_FUNCTION},
      {"game_dlss DLSS_PASS", twshogun2::game_dlss::internal::DLSS_PASS},
      {"game_dlss RELEASE_FEATURE", twshogun2::game_dlss::internal::RELEASE_FEATURE},
      {"grass_distance UPDATE", twshogun2::grass_distance::internal::UPDATE},
      {"model_detail SELECT_LOD", twshogun2::model_detail::internal::SELECT_LOD},
      {"model_detail LOD_FADE", twshogun2::model_detail::internal::LOD_FADE},
      {"particle_resolution MINIMUM_READ", twshogun2::particle_resolution::internal::MINIMUM_READ},
      {"shadow_resolution HIGHEST_SIZES", twshogun2::shadow_resolution::internal::HIGHEST_SIZES},
      {"terrain_detail FORCE_LOD_READ", twshogun2::terrain_detail::internal::FORCE_LOD_READ},
      {"unit_detail UNIT_DISTANCES", twshogun2::unit_detail::internal::UNIT_DISTANCES},
  };
  const auto* image_base = reinterpret_cast<const uint8_t*>(image);
  for (const auto& [name, pattern] : patterns) {
    const auto [count, first] = Matches(image_base, pattern);
    const uint8_t* found = twshogun2::engine::FindPattern(pattern);
    if (count != 1 || found != first) {
      std::printf("FAIL %s: %zu matches, FindPattern %p, first %p\n", name, count, static_cast<const void*>(found), static_cast<const void*>(first));
      return 1;
    }
    // CONTROL: its first fixed byte changed.
    std::string changed(pattern);
    const size_t fixed = changed.find_first_not_of("? ");
    CHECK(fixed != std::string::npos);
    changed.replace(fixed, 2, std::format("{:02X}", std::stoi(changed.substr(fixed, 2), nullptr, 16) ^ 0xFF));
    const auto [changed_count, changed_first] = Matches(image_base, changed);
    CHECK(changed_count == 0 && changed_first == nullptr && twshogun2::engine::FindPattern(changed) == nullptr);
    std::printf("  %s: once, at +0x%zx (FindPattern's); CONTROL changed (%.2s -> %s): none\n", name,
                static_cast<size_t>(found - image_base), pattern.data() + fixed, changed.substr(fixed, 2).c_str());
  }
  std::printf("4. %zu byte patterns: each once in the executable sections, where FindPattern points; CONTROL each with a changed byte: "
              "none\n", std::size(patterns));

  // 5. The game's DLSS release (R21) releases the DLSS pass's own holder: RELEASE_FEATURE's "mov rcx, [rip + x]" and
  //    DLSS_PASS's "cmp qword [rip + x], 0" name one address, its second load holder + 8 (the parameters). CONTROL: the
  //    displacement read one byte off names another address.
  {
    auto* pass = twshogun2::engine::FindPattern(twshogun2::game_dlss::internal::DLSS_PASS);
    auto* release = twshogun2::engine::FindPattern(twshogun2::game_dlss::internal::RELEASE_FEATURE);
    CHECK(pass != nullptr && release != nullptr);
    const uint8_t* holder = twshogun2::engine::RipTarget(pass + 3, pass + 8);
    constexpr size_t at = twshogun2::game_dlss::internal::RELEASE_HOLDER;
    const uint8_t* released = twshogun2::engine::RipTarget(release + at, release + at + 4);
    const uint8_t* released_parameters = twshogun2::engine::RipTarget(release + 24, release + 28);
    CHECK(released == holder && released_parameters == holder + 8);
    // CONTROL: the displacement read one byte off names another address.
    CHECK(twshogun2::engine::RipTarget(release + at + 1, release + at + 5) != holder);
    std::printf("5. the game's DLSS release at +0x%zx: handle +0x%zx and parameters +0x%zx = DLSS_PASS's holder and holder + 8; "
                "CONTROL one byte off: another address\n",
                static_cast<size_t>(release - image_base), static_cast<size_t>(holder - image_base),
                static_cast<size_t>(released_parameters - image_base));
  }

  // 6. Terrain detail gives the game's values back (off, and at detach): its two values as the image holds them, the
  //    finest level written, then off. CONTROL: on alone leaves them changed.
  {
    twshogun2::terrain_detail::Install();
    auto* enable = twshogun2::terrain_detail::internal::enable_force_lod;
    auto* level = twshogun2::terrain_detail::internal::force_lod;
    CHECK(enable != nullptr && level != nullptr);
    const uint8_t game_enable = *enable;
    const int32_t game_level = *level;
    twshogun2::terrain_detail::Apply(true);
    CHECK(*enable == 1 && *level == 0);
    CHECK(game_enable != 1);  // CONTROL: on differs from the game's
    twshogun2::terrain_detail::Apply(false);
    CHECK(*enable == game_enable && *level == game_level);
    std::printf("6. terrain detail: the game's enable_force_lod %u / force_lod %d, on 1 / 0, off back to the game's; "
                "CONTROL on differs\n", game_enable, game_level);
  }
  FreeLibrary(image);
  FreeLibrary(mapped);
  std::printf("PASS\n");
  return 0;
}
