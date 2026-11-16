// twattila/graphics_hooks.hpp's code patterns against the games' own empire.retail.dll (read from disk, never loaded):
// tw::engine_tweaks::FindPattern hooks the first match in the module's code sections, so a pattern found twice could hook
// the wrong function. For each DLL its patterns (Attila and Thrones of Britannia: units, the dead units' call and
// ImposterDistance, trees, grass, grass cells and their radius, the big battles' call and PackAnimations; Rome II: its
// units, dead units' call and ImposterDistance):
// A. each matches exactly once in the code sections ModuleSections scans (executable, readable, not discardable, at most
//    384 MB), counted at every byte.
// B. CONTROL: the same sections with the first fixed byte of that match changed: the pattern matches nowhere (the count
//    finds what is there, no false match).
// tests.exe <attila|thrones|rome2> <empire.retail.dll> [<game> <dll> ...]
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/games/twattila/graphics_hooks.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

namespace hooks = twattila::graphics_hooks::internal;

extern "C" __declspec(dllexport) const char* const NAME = "Graphics hooks patterns test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

// The pattern's bytes (-1: any).
static std::vector<int> Bytes(std::string_view pattern) {
  std::vector<int> bytes;
  for (size_t at = 0; at + 1 < pattern.size(); at += 3) {
    bytes.push_back(pattern[at] == '?' ? -1 : std::stoi(std::string(pattern.substr(at, 2)), nullptr, 16));
  }
  return bytes;
}

// Every match of `bytes` in `code`: its offsets.
static std::vector<size_t> Matches(const std::string& code, const std::vector<int>& bytes) {
  std::vector<size_t> found;
  const auto* begin = reinterpret_cast<const uint8_t*>(code.data());
  for (size_t at = 0; at + bytes.size() <= code.size(); ++at) {
    size_t matched = 0;
    while (matched < bytes.size() && (bytes[matched] < 0 || begin[at + matched] == bytes[matched])) ++matched;
    if (matched == bytes.size()) found.push_back(at);
  }
  return found;
}

int main(int argc, char** argv) {
  const std::vector<std::pair<const char*, std::string_view>> attila = {
      {"units", hooks::UNIT_DISTANCES},
      {"dead units' call", hooks::IMPOSTER_DISTANCE_CALL},
      {"dead units", hooks::IMPOSTER_DISTANCE},
      {"trees", hooks::DRAW_TREE},
      {"grass", hooks::DRAW_SCRUB},
      {"grass cells", hooks::SCRUB_UPDATE},
      {"grass cells' radius", hooks::SCRUB_CELL},
      {"big battles' call", hooks::FIRST_PACK_CALL},
      {"big battles", hooks::PACK_ANIMATIONS},
  };
  const std::vector<std::pair<const char*, std::string_view>> rome2 = {
      {"units", hooks::UNIT_DISTANCES_ROME2},
      {"dead units' call", hooks::IMPOSTER_DISTANCE_CALL_ROME2},
      {"dead units", hooks::IMPOSTER_DISTANCE_ROME2},
  };
  int failures = 0;
  for (int arg = 1; arg + 1 < argc; arg += 2) {
    const std::string game = argv[arg];
    std::ifstream stream(argv[arg + 1], std::ios::binary);
    const std::string file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    // The code sections as ModuleSections takes them in memory, from their raw bytes in the file (the in-memory tail past
    // the raw size is zeros).
    std::vector<std::string> sections;
    if (file.size() > sizeof(IMAGE_DOS_HEADER)) {
      const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
      const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
      const auto* section = IMAGE_FIRST_SECTION(nt);
      for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0 || (section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE) != 0
            || (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 || section->Misc.VirtualSize > 384u * 1024u * 1024u) {
          continue;
        }
        const size_t raw = std::min<size_t>(section->SizeOfRawData, section->Misc.VirtualSize);
        sections.push_back(file.substr(section->PointerToRawData, raw) + std::string(section->Misc.VirtualSize - raw, '\0'));
      }
    }
    if (sections.empty()) {
      std::printf("FAIL %s: no code sections in %s\n", game.c_str(), argv[arg + 1]);
      ++failures;
      continue;
    }
    size_t code_bytes = 0;
    for (const std::string& section : sections) code_bytes += section.size();
    std::printf("%s: %s, %zu code sections, %zu bytes\n", game.c_str(), argv[arg + 1], sections.size(), code_bytes);
    for (const auto& [name, pattern] : (game == "rome2" ? rome2 : attila)) {
      const std::vector<int> bytes = Bytes(pattern);
      size_t count = 0;
      size_t first_section = 0;
      size_t first_at = 0;
      for (size_t index = 0; index < sections.size(); ++index) {
        const std::vector<size_t> found = Matches(sections[index], bytes);
        if (count == 0 && !found.empty()) {
          first_section = index;
          first_at = found.front();
        }
        count += found.size();
      }
      size_t control = 0;
      if (count != 0) {
        size_t fixed = 0;
        while (bytes[fixed] < 0) ++fixed;
        std::string changed = sections[first_section];
        changed[first_at + fixed] = static_cast<char>(bytes[fixed] ^ 0xFF);
        for (size_t index = 0; index < sections.size(); ++index) {
          control += Matches(index == first_section ? changed : sections[index], bytes).size();
        }
      }
      const bool pass = (count == 1 && control == 0);
      std::printf("  %s %-20s %zu bytes: %zu matches (the first at +0x%zx); CONTROL (one of its bytes changed): %zu\n",
                  pass ? "ok  " : "FAIL", name, bytes.size(), count, first_at, control);
      failures += pass ? 0 : 1;
    }
  }
  std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
  return failures == 0 ? 0 : 1;
}
