// scale_census_replay (scale_census_build.ps1 builds it): view_share.hpp's Reflect + Patch on every pixel / compute
// shader of a dump, as the addon runs them when the game creates its shaders (view_share_hooked.hpp: the repo's
// view_share.hpp with a hook that hands over the sites).
// usage: scale_census_replay.exe <cso folder> [region table: lines "HASH SLOTS" (hex hash, slot bits as decimal or 0b...)]
//                                [whole texture names, comma separated] [1: viewport chains, the addon's SetViewportChains(true)]
// stdout, one TSV line per shader:
//   hash stage camera_slot screen target viewport render_scale slot_taken ndc projected region whole sites
// sites: kind@instruction:roots[!] separated by spaces, roots the camera registers and others the patched value comes
// from (all sources, every component: T texture size c27/c31, V viewport c29, P projection, I input, C other constant,
// R a texture read); "!" the over-patch rule: an NDC site whose uv also comes from the viewport (the game's own
// dynamic-resolution path, already the view's: the patch divides it again), or a projected site rescaled by the
// camera's texture / viewport sizes before its read.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "view_share_hooked.hpp"

extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(HMODULE, uint32_t) { return true; }
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(HMODULE) {}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

namespace vs = twattila::view_share::internal;

constexpr uint32_t ROOT_TEXTURE = 1, ROOT_VIEW = 2, ROOT_PROJECTION = 4, ROOT_INPUT = 8, ROOT_CONSTANT = 16, ROOT_READ = 32;
constexpr uint32_t TYPE_RESOURCE = 7;

static bool IsRead(uint32_t opcode) {
  switch (opcode) {
    case vs::OP_LD: case vs::OP_SAMPLE: case vs::OP_SAMPLE_C: case vs::OP_SAMPLE_C_LZ: case vs::OP_SAMPLE_L: case vs::OP_SAMPLE_D:
    case vs::OP_SAMPLE_B: case vs::OP_GATHER4: case vs::OP_GATHER4_C: case vs::OP_GATHER4_PO: case vs::OP_GATHER4_PO_C:
      return true;
    default:
      return false;
  }
}

static uint32_t CameraBits(const vs::Reflection& reflection, const vs::Operand& operand) {
  if (operand.type != vs::TYPE_CONSTANT_BUFFER || operand.relative) return 0;
  if (operand.index[0] != reflection.camera_slot) return ROOT_CONSTANT;
  switch (vs::CameraRegister(reflection, operand.index[1])) {
    case vs::Source::TEXTURE: return ROOT_TEXTURE;
    case vs::Source::VIEW: return ROOT_VIEW;
    case vs::Source::CAMERA_PROJECTION: return ROOT_PROJECTION;
    default: return ROOT_CONSTANT;
  }
}

// Every root of temp `reg`'s `components` as read at instruction `index` (each component to its own last writer).
static uint32_t Roots(const std::vector<vs::Instruction>& code, size_t index, uint32_t reg, uint32_t components, const vs::Reflection& reflection,
                      std::set<std::tuple<size_t, uint32_t, uint32_t>>* seen, int depth = 0) {
  if (depth > 48) return 0;
  uint32_t bits = 0;
  for (uint32_t component = 0; component < 4; ++component) {
    if ((components & (1u << component)) == 0) continue;
    if (!seen->insert({index, reg, component}).second) continue;
    for (size_t at = index; at-- > 0;) {
      const auto& instruction = code[at];
      if (instruction.operands.empty()) continue;
      const auto& destination = instruction.operands[0];
      if (destination.type != vs::TYPE_TEMP || destination.mode != 0 || destination.relative || destination.index[0] != reg
          || (destination.select & (1u << component)) == 0) {
        continue;
      }
      if (IsRead(instruction.opcode)) {
        bits |= ROOT_READ;
        break;
      }
      for (size_t source = 1; source < instruction.operands.size(); ++source) {
        const auto& operand = instruction.operands[source];
        if (operand.type == TYPE_RESOURCE) continue;
        bits |= CameraBits(reflection, operand);
        if (operand.type == vs::TYPE_INPUT || operand.type == 32 || operand.type == 33 || operand.type == 34) bits |= ROOT_INPUT;
        if (operand.type != vs::TYPE_TEMP || operand.relative) continue;
        // dp2-dp4 read their sources' first components whatever the destination.
        uint32_t read = 0;
        if (instruction.opcode >= 15 && instruction.opcode <= 17) {
          const uint32_t count = instruction.opcode - 13;  // dp2 15, dp3 16, dp4 17
          for (uint32_t c = 0; c < count; ++c) read |= 1u << vs::SourceComponent(operand, c);
        } else {
          read = 1u << vs::SourceComponent(operand, component);
        }
        bits |= Roots(code, at, operand.index[0], read, reflection, seen, depth + 1);
      }
      break;
    }
  }
  return bits;
}

// A projected site's value (components of its destination) rescaled by the camera's texture or viewport size before a
// texture read takes it.
static bool RescaledBeforeRead(const std::vector<vs::Instruction>& code, size_t index, uint32_t reg, uint32_t components,
                               const vs::Reflection& reflection) {
  std::map<uint32_t, uint32_t> tainted = {{reg, components}};
  for (size_t at = index + 1; at < code.size(); ++at) {
    const auto& instruction = code[at];
    if (instruction.operands.empty()) continue;
    bool reads_tainted = false;
    for (size_t source = 1; source < instruction.operands.size(); ++source) {
      const auto& operand = instruction.operands[source];
      if (operand.type != vs::TYPE_TEMP || operand.relative) continue;
      const auto found = tainted.find(operand.index[0]);
      if (found == tainted.end()) continue;
      for (uint32_t c = 0; c < 4; ++c) {
        if ((found->second & (1u << vs::SourceComponent(operand, c))) != 0) reads_tainted = true;
      }
    }
    if (IsRead(instruction.opcode)) {
      if (reads_tainted) return false;
      continue;
    }
    const auto& destination = instruction.operands[0];
    if (destination.type != vs::TYPE_TEMP || destination.mode != 0 || destination.relative) continue;
    if (reads_tainted) {
      // Its other sources: the camera's sizes directly or through a temp made from them (mul r2.xy, c29.xy, c31.zw).
      for (size_t source = 1; source < instruction.operands.size(); ++source) {
        const auto& operand = instruction.operands[source];
        const uint32_t camera = CameraBits(reflection, operand);
        if ((camera & (ROOT_TEXTURE | ROOT_VIEW)) != 0) return true;
        if (operand.type == vs::TYPE_TEMP && !operand.relative) {
          uint32_t read = 0;
          for (uint32_t c = 0; c < 4; ++c) {
            if ((destination.select & (1u << c)) != 0) read |= 1u << vs::SourceComponent(operand, c);
          }
          const auto marked = tainted.find(operand.index[0]);
          if (marked != tainted.end() && (marked->second & read) != 0) continue;  // the projected value itself
          std::set<std::tuple<size_t, uint32_t, uint32_t>> seen;
          const uint32_t bits = Roots(code, at, operand.index[0], read, reflection, &seen);
          if ((bits & (ROOT_TEXTURE | ROOT_VIEW)) != 0 && (bits & (ROOT_READ | ROOT_PROJECTION)) == 0) return true;
        }
      }
      tainted[destination.index[0]] |= destination.select;
    } else if (const auto found = tainted.find(destination.index[0]); found != tainted.end()) {
      found->second &= ~destination.select;
    }
  }
  return false;
}

static std::string RootText(uint32_t bits) {
  std::string text;
  if (bits & ROOT_TEXTURE) text += 'T';
  if (bits & ROOT_VIEW) text += 'V';
  if (bits & ROOT_PROJECTION) text += 'P';
  if (bits & ROOT_INPUT) text += 'I';
  if (bits & ROOT_CONSTANT) text += 'C';
  if (bits & ROOT_READ) text += 'R';
  return text.empty() ? "-" : text;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: scale_census_replay.exe <cso folder> [region table] [whole texture names] [1: viewport chains]\n");
    return 2;
  }
  std::map<uint32_t, uint64_t> region_table;
  if (argc > 2 && std::strlen(argv[2]) != 0) {
    std::ifstream table(argv[2]);
    std::string hash, slots;
    while (table >> hash >> slots) {
      const uint64_t bits = (slots.rfind("0b", 0) == 0) ? std::stoull(slots.substr(2), nullptr, 2) : std::stoull(slots, nullptr, 0);
      region_table[static_cast<uint32_t>(std::stoul(hash, nullptr, 16))] |= bits;
    }
  }
  std::vector<std::string> whole;
  if (argc > 3) {
    std::stringstream names(argv[3]);
    for (std::string name; std::getline(names, name, ',');) {
      if (!name.empty()) whole.push_back(name);
    }
  }
  const bool viewport_chains = argc > 4 && std::strcmp(argv[4], "1") == 0;
  const std::regex file_pattern(R"(0x([0-9A-Fa-f]{8})\.(ps|cs)_\d_\d\.cso)");
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(argv[1])) files.push_back(entry.path());
  std::sort(files.begin(), files.end());
  for (const auto& path : files) {
    std::smatch match;
    const std::string file = path.filename().string();
    if (!std::regex_match(file, match, file_pattern)) continue;
    const uint32_t hash = static_cast<uint32_t>(std::stoul(match[1].str(), nullptr, 16));
    std::ifstream stream(path, std::ios::binary);
    const std::vector<uint8_t> code{std::istreambuf_iterator<char>(stream), {}};
    const auto program = twattila::dxbc::Read(code);
    const auto reflection = vs::Reflect(code, program, whole);
    const auto region = region_table.count(hash) ? region_table[hash] : 0ull;
    std::string sites;
    vs::census_hook = [&](const std::vector<vs::Instruction>& instructions, const std::vector<vs::CensusSite>& found, const vs::Reflection& reflected) {
      for (const auto& site : found) {
        static const char* KINDS[] = {"NDC", "PROJECTED", "REGION", "SPLIT", "WHOLE"};
        const auto& instruction = instructions[site.instruction];
        uint32_t bits = 0;
        bool over = false;
        if ((site.kind == 0 || site.kind == 1) && instruction.operands.size() > 1 && instruction.operands[1].type == vs::TYPE_TEMP) {
          const auto& source = instruction.operands[1];
          const uint32_t read = (1u << vs::SourceComponent(source, site.components[0])) | (1u << vs::SourceComponent(source, site.components[1]));
          std::set<std::tuple<size_t, uint32_t, uint32_t>> seen;
          bits = Roots(instructions, site.instruction, source.index[0], read, reflected, &seen);
          if (site.kind == 0) {
            over = (bits & ROOT_VIEW) != 0;
          } else {
            over = (bits & ROOT_VIEW) != 0
                   || RescaledBeforeRead(instructions, site.instruction, instruction.operands[0].index[0],
                                         (1u << site.components[0]) | (1u << site.components[1]), reflected);
          }
        }
        if (!sites.empty()) sites += ' ';
        sites += std::string(KINDS[site.kind]) + "@" + std::to_string(site.instruction) + ":" + RootText(bits) + (over ? "!" : "");
      }
    };
    const auto patched = vs::Patch(code, reflection, region, viewport_chains);
    vs::census_hook = nullptr;
    const auto reg = [](uint32_t value) { return value == UINT32_MAX ? std::string("-") : std::to_string(value); };
    std::printf("%08X\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%u\t%u\t%u\t%u\t%s\n", hash, match[2].str().c_str(), reg(reflection.camera_slot).c_str(),
                reg(reflection.screen).c_str(), reg(reflection.target).c_str(), reg(reflection.viewport).c_str(), reflection.render_scale ? 1 : 0,
                reflection.slot_taken ? 1 : 0, patched.ndc, patched.projected, patched.region, patched.whole, sites.empty() ? "-" : sites.c_str());
  }
  return 0;
}
