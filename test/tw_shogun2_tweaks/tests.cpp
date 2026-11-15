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
// tests.exe <empire.retail.x64.dll> <s2_tweaks_1114.txt>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "src/games/twattila/engine_tests.hpp"
#include "src/games/twshogun2completeedition/tweak_objects.hpp"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Shogun 2 tweak test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { std::printf("  log: %s\n", message); }

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
  FreeLibrary(mapped);
  std::printf("PASS\n");
  return 0;
}
