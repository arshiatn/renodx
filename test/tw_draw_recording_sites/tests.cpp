// Threaded Draw Recording's sites (twattila/draw_recording_sites.hpp, included by engine_code.hpp with TWATTILA_DRAW_RECORDING),
// x86 only. empire.retail.dll is only READ from disk: its bytes are laid out in a data copy (headers and sections at their RVAs,
// PAGE_READWRITE; never relocated, loaded or run), and engine_code's Prepare, Update and Uninstall work on that copy. The
// only machine code this test runs is its own: the thunks the header builds, called from a hand-assembled harness, with
// synthetic stand-ins for the game's flush, shared-state call, animation call, dispatcher and Record.
// 1. The 23 sites on the real bytes: found, their addresses, the game's bytes, the call targets (the flush's 7 call sites
//    and the loops' calls), the jumps into the page, the page's protection, GameCalls (the colour replays' calls and
//    constants decoded from the code, the B groups' accessor, stub and module shift).
// 2. Which sites a group asks for (the entry hook, else the call-site fallback; never both), for depth A and colour A.
// 3. Every byte of every pattern, of the flush's head and exit, of the 10 vtables' slots, of the colour pieces, the
//    landings' heads, the accessor and the stub, and of each call's displacement, flipped one at a time: exactly the sites
//    that read that byte are left out (a shared piece: the whole feature; a colour piece: the colour loop's own sites).
// 4. A hooked entry (an E9 on any of the 13 passtime entries): Prepare refuses; one appearing later stops the turn-on.
// 5. The one window for both tables (Update): in, out, the arena size, a dispatch in flight (refused), a thread in the cut
//    stub (refused), another mod's bytes on one site (going in: none written; coming out: that one left), Uninstall giving up
//    and then succeeding; per group (5f: depth A, colour A): one group's own sites in or out leave the other group's alone, and no
//    two groups' sites overlap.
// 6. The thunks on synthetic code: the flush entry (direct, record, no instances), a flush call site (direct, record), the
//    shared-state call (direct, known vtable, unknown vtable), the animation call, the animation init, the cut: results,
//    stack balance, ebx esi edi kept, the frame Record sees; and the colour loop's: the mesh setup (known / unknown class),
//    the two constants' guards (the game's compare and flags on the direct path), the faction block, the texture lookups,
//    all run on the game's own covered bytes.
// 7. The assembler's bounds.
// tests.exe [empire.retail.dll]
#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <vector>

extern "C" __declspec(dllexport) const char* const NAME = "Draw recording sites test";

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
}

#if defined(_WIN64)

int main() {
  std::printf("SKIP: Attila is 32-bit; the sites are x86 only.\n");
  return 0;
}

#else

#include "src/games/twattila/engine_code.hpp"

namespace dr = twattila::draw_recording;
namespace ec = twattila::engine_code::internal;
namespace ecp = twattila::engine_code;

namespace {

int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

std::vector<uint8_t> file;
uint8_t* image = nullptr;
uint32_t image_size = 0;
std::vector<uint8_t> pristine;

uint8_t* LayoutImage() {
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data())->e_lfanew);
  image_size = nt->OptionalHeader.SizeOfImage;
  auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  std::memcpy(memory, file.data(), nt->OptionalHeader.SizeOfHeaders);
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    const size_t size = std::min<size_t>(section->SizeOfRawData, image_size - section->VirtualAddress);
    if (section->PointerToRawData + size <= file.size()) std::memcpy(memory + section->VirtualAddress, file.data() + section->PointerToRawData, size);
  }
  // The loader's relocation, as data: every absolute address (the vtables' slots among them) as it would be at this address.
  const uint32_t delta = reinterpret_cast<uint32_t>(memory) - nt->OptionalHeader.ImageBase;
  const IMAGE_DATA_DIRECTORY& relocations = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  for (uint32_t offset = 0; offset < relocations.Size;) {
    const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(memory + relocations.VirtualAddress + offset);
    const auto* entries = reinterpret_cast<const uint16_t*>(block + 1);
    for (uint32_t entry = 0; entry < (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2; ++entry) {
      if ((entries[entry] >> 12) != IMAGE_REL_BASED_HIGHLOW) continue;
      auto* at = memory + block->VirtualAddress + (entries[entry] & 0xFFF);
      uint32_t value;
      std::memcpy(&value, at, sizeof(value));
      value += delta;
      std::memcpy(at, &value, sizeof(value));
    }
    offset += block->SizeOfBlock;
  }
  return memory;
}

// A new Prepare on the data copy, from nothing: the page of the last one freed, no site written.
bool Prepare() {
  if (ec::recording_page != nullptr) VirtualFree(ec::recording_page, 0, MEM_RELEASE);
  ec::recording_page = nullptr;
  ec::recording_found = 0;
  ec::recording_written = 0;
  ec::recording_asked = 0;
  ec::failed_writes = 0;
  ec::recording_prepared = false;
  ec::recording_refused = false;
  ec::recording_left_in = false;
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  ec::recording_fail_protect = -1;
#endif
  log_lines.clear();
  return ec::PrepareRecording(reinterpret_cast<HMODULE>(image));
}

uint8_t* Rva(uint32_t rva) { return image + rva; }

uint32_t Read32(const uint8_t* at) {
  uint32_t value;
  std::memcpy(&value, at, sizeof(value));
  return value;
}

constexpr uint64_t ALL_SITES = (uint64_t{1} << ec::RECORDING_SITE_COUNT) - 1;
constexpr uint64_t DEPTH_A_SITES = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::DEPTH_A)] | ec::RECORDING_ENTRY_BIT;
constexpr uint64_t DEPTH_A_FALLBACK = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::DEPTH_A)] | ec::RECORDING_FALLBACK[static_cast<size_t>(dr::Group::DEPTH_A)];
constexpr uint64_t COLOUR_A_OWN = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::COLOUR_A)];
constexpr uint64_t COLOUR_A_SITES = COLOUR_A_OWN | ec::RECORDING_ENTRY_BIT;
constexpr uint64_t COLOUR_A_FALLBACK = COLOUR_A_OWN | ec::RECORDING_FALLBACK[static_cast<size_t>(dr::Group::COLOUR_A)];
constexpr uint64_t DEPTH_B_OWN = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::DEPTH_B)];
constexpr uint64_t DEPTH_B_SITES = DEPTH_B_OWN | ec::RECORDING_ENTRY_BIT;
constexpr uint64_t COLOUR_B_OWN = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::COLOUR_B)];
constexpr uint64_t COLOUR_B_SITES = COLOUR_B_OWN | ec::RECORDING_ENTRY_BIT;
constexpr uint64_t COLOUR_B_ONLY = COLOUR_B_OWN & ~DEPTH_B_OWN;  // its 20 sites (the cut is Depth B's too)

// The positions of the non-wildcard bytes of a pattern.
std::vector<size_t> FixedBytes(std::string_view pattern) {
  std::vector<size_t> positions;
  for (size_t at = 0; at + 1 < pattern.size(); at += 3) {
    if (pattern[at] != '?') positions.push_back(at / 3);
  }
  return positions;
}

bool Mentions(const std::string& text, const char* word) { return text.find(word) != std::string::npos; }

bool LogHas(const char* word) {
  for (const std::string& line : log_lines) {
    if (Mentions(line, word)) return true;
  }
  return false;
}

// Every byte of the image outside the sites (and the given extra spans) as it was.
size_t DifferingBytes() {
  size_t count = 0;
  for (uint32_t at = 0; at < image_size; ++at) {
    if (image[at] != pristine[at]) ++count;
  }
  return count;
}

// ---------------------------------------------------------------- 1. the real bytes

void TestRealBytes() {
  REQUIRE(Prepare());
  CHECK(ec::recording_found == ALL_SITES);
  CHECK(ec::recording_prepared && ec::recording_page != nullptr && ec::recording_cut.end > ec::recording_page);
  MEMORY_BASIC_INFORMATION info = {};
  VirtualQuery(ec::recording_page, &info, sizeof(info));
  CHECK(info.Protect == PAGE_EXECUTE_READ);
  CHECK(DifferingBytes() == 0);  // nothing written to the game
  // The game's first 5 bytes at the first six sites (the other flush call sites read through their targets below).
  constexpr std::array<std::array<uint8_t, 5>, 6> GAME = {{
      {0x83, 0xEC, 0x1C, 0x83, 0x7C},  // the flush's entry: sub esp, 0x1c; the first 2 bytes of cmp dword ptr [esp + 0x30], 0
      {0xE8, 0xA2, 0x2F, 0x00, 0x00},  // the cut: call of the depth loop
      {0xC7, 0x44, 0x24, 0x18, 0xFF},  // mov dword ptr [esp + 0x18], -1
      {0x8B, 0x40, 0x68, 0xFF, 0xD0},  // mov eax, [eax + 0x68]; call eax
      {0xE8, 0xF8, 0x18, 0x0D, 0x00},  // the animation call
      {0xE8, 0x02, 0x6B, 0xFC, 0xFF},  // the boundary flush call
  }};
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
    const ec::RecordingSiteInfo& site_info = ec::RECORDING_INFO[index];
    const ec::Site& site = ec::recording_sites[index];
    CHECK(site.at == Rva(site_info.rva));
    CHECK(site.name == site_info.name);
    CHECK(site.opcode == site_info.opcode);
    CHECK(std::memcmp(site.game.data(), &pristine[site_info.rva], ec::SITE_SIZE) == 0);
    if (index < GAME.size()) CHECK(std::memcmp(site.game.data(), GAME[index].data(), 5) == 0);
    CHECK(site.jump[0] == site_info.opcode);
    const uint8_t* const thunk = site.at + ec::SITE_SIZE + static_cast<int32_t>(Read32(site.jump.data() + 1));
    CHECK(thunk >= ec::recording_page && thunk < ec::recording_page + ec::RECORDING_CODE_SIZE);
    // A call site's target in the game is the function the table names.
    if (site_info.callee_rva != 0) {
      CHECK(site.game[0] == 0xE8 && site.at + 5 + static_cast<int32_t>(Read32(site.game.data() + 1)) == Rva(site_info.callee_rva));
    }
  }
  // The cut is built first (a removal waits for threads in it); the flush's 7 call sites all call the one entry.
  const uint8_t* const cut_thunk = ec::recording_sites[1].at + 5 + static_cast<int32_t>(Read32(ec::recording_sites[1].jump.data() + 1));
  CHECK(cut_thunk == ec::recording_page);
  size_t flush_calls = 0;
  for (const ec::RecordingSiteInfo& site_info : ec::RECORDING_INFO) {
    if (site_info.kind == ec::RecordingKind::FALLBACK) {
      ++flush_calls;
      CHECK(site_info.callee_rva == ec::FLUSH_RVA && site_info.opcode == 0xE8);
    }
  }
  CHECK(flush_calls == 7);
  CHECK(ec::RECORDING_ENTRY_BIT == 1);
  // GameCalls.
  const dr::GameCalls& calls = dr::internal::game_calls;
  CHECK(calls.usable);
  CHECK(calls.loops[static_cast<size_t>(dr::Group::DEPTH_A)] == ec::Address(Rva(ec::DEPTH_LOOP_RVA)));
  CHECK(calls.flush == ec::Address(Rva(ec::FLUSH_RVA)) && calls.anim_call == ec::Address(Rva(ec::ANIM_CALL_RVA)));
  for (size_t index = 0; index < ec::WEIGHTED_CLASSES.size(); ++index) {
    CHECK(calls.weighted_vtables[index] == ec::Address(Rva(ec::WEIGHTED_CLASSES[index].vtable)));
  }
  CHECK(ec::recording_cut.active == &dr::SharedPage()->active && *ec::recording_arena_size == dr::SLOT_COUNT * dr::SLOT_SIZE);
  const bool all_found = LogHas("59 of 59 sites found") && !LogHas("left alone");
  if (!all_found) {
    for (const std::string& line : log_lines) std::printf("   log: %s\n", line.c_str());
  }
  CHECK(all_found);
  // The colour loop's pieces, decoded from the code: the call targets, the faction block's default and the cache flag.
  CHECK(calls.loops[static_cast<size_t>(dr::Group::COLOUR_A)] == ec::Address(Rva(ec::COLOUR_LOOP_RVA)));
  CHECK(calls.dirty_call == ec::Address(Rva(0x191d960)) && calls.srv_call == ec::Address(Rva(0x18d2b60)));
  CHECK(calls.faction_call == ec::Address(Rva(0x1885350)) && calls.view_call == ec::Address(Rva(ec::TEXTURE_VIEW_RVA)));
  CHECK(calls.cached_constants == Rva(0x1da661b) && calls.faction_default == reinterpret_cast<const float*>(Rva(0x1afbd74)));
  // The B groups' helpers are set at the end of Prepare, not left for the class table's step: no dispatcher call is made on 0.
  CHECK(calls.accessor == ec::Address(Rva(ec::ACCESSOR_RVA)) && calls.vf64_stub == ec::Address(Rva(ec::WEIGHTED_VF64)));
  CHECK(calls.module_shift == ec::Address(image) - 0x10000000);
  CHECK((calls.usable_groups & 3) == 3);
  // The B groups: Depth B usable (the cut and 15 calls), Colour B too (the same cut and its 20 sites).
  CHECK(std::popcount(DEPTH_B_OWN) == 16 && ((calls.usable_groups >> static_cast<size_t>(dr::Group::DEPTH_B)) & 1) == 1);
  CHECK(std::popcount(COLOUR_B_OWN) == 21 && ((calls.usable_groups >> static_cast<size_t>(dr::Group::COLOUR_B)) & 1) == 1);
  CHECK((DEPTH_B_OWN & COLOUR_B_OWN) == ec::RecordingBit(ec::RecordingSite::DEPTH_B_CUT));
  CHECK(calls.loops[static_cast<size_t>(dr::Group::DEPTH_B)] == 0);
  constexpr std::array<uint8_t, 5> VF68_BYTES = {0x8B, 0x40, 0x68, 0xFF, 0xD0};
  constexpr std::array<uint8_t, 5> VF64_BYTES = {0x8B, 0x40, 0x64, 0xFF, 0xD0};
  constexpr std::array<uint8_t, 5> CUT_BYTES = {0x8B, 0x5C, 0x24, 0x34, 0x8B};  // mov ebx, [esp + 0x34]; the first byte of mov edi, [esp + 0x1c]
  size_t b_calls = 0;
  size_t colour_calls = 0;
  size_t state_blocks = 0;
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
    const ec::RecordingSiteInfo& site_info = ec::RECORDING_INFO[index];
    if (site_info.group != dr::Group::DEPTH_B && site_info.group != dr::Group::COLOUR_B) continue;
    const ec::Site& site = ec::recording_sites[index];
    if (site_info.record_site == dr::Site::COUNT) {
      ++b_calls;
      CHECK(site_info.opcode == 0xE9 && site_info.covered == 8 && std::memcmp(site.game.data(), CUT_BYTES.data(), 5) == 0);
    } else if (site_info.record_site == dr::Site::COLOUR_B_STATE) {
      // The state blocks: a cmp byte [esp + local], 0 (5 bytes), or body 2's test bh, bh; je; mov [esi + 0x10], bl (7).
      ++state_blocks;
      CHECK(site_info.opcode == 0xE9);
      CHECK(site_info.covered == (index == static_cast<size_t>(ec::RecordingSite::COLOUR_B2_STATE) ? 7u : 5u));
      CHECK(site_info.covered == 7 ? std::memcmp(site.game.data(), "\x84\xFF\x74\x3A\x88", 5) == 0
                                   : (site.game[0] == 0x80 && site.game[1] == 0x7C && site.game[2] == 0x24 && site.game[4] == 0x00));
    } else {
      const bool depth = (site_info.group == dr::Group::DEPTH_B);
      ++(depth ? b_calls : colour_calls);
      const bool vf68 = (site_info.record_site == (depth ? dr::Site::DEPTH_B_VF68 : dr::Site::COLOUR_B_VF68));
      CHECK(site_info.opcode == 0xE8 && site_info.covered == 5
            && (vf68 || site_info.record_site == (depth ? dr::Site::DEPTH_B_VF64 : dr::Site::COLOUR_B_VF64)));
      CHECK(std::memcmp(site.game.data(), (vf68 ? VF68_BYTES : VF64_BYTES).data(), 5) == 0);
    }
  }
  CHECK(b_calls == 16 && colour_calls == 15 && state_blocks == 5 && ec::RecordingBit(ec::RecordingSite::DEPTH_B_CUT) == (DEPTH_B_OWN & ec::RECORDING_CUTS));
  // The shared-state thunks of one form are one thunk: 2 forms in depth, 2 in colour.
  std::set<uint32_t> shared_forms;
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
    const ec::RecordingSiteInfo& site_info = ec::RECORDING_INFO[index];
    if (site_info.group != dr::Group::DEPTH_B && site_info.group != dr::Group::COLOUR_B) continue;
    if (site_info.record_site == dr::Site::COUNT || site_info.record_site == dr::Site::COLOUR_B_STATE) continue;
    shared_forms.insert(ec::Address(ec::recording_sites[index].at) + 5 + Read32(ec::recording_sites[index].jump.data() + 1));
  }
  CHECK(shared_forms.size() == 4);
  // The cut is built after the A groups' (page order), inside the cut range a removal waits on.
  const uint8_t* const b_cut_thunk = ec::recording_sites[static_cast<size_t>(ec::RecordingSite::DEPTH_B_CUT)].at + 5
                                     + static_cast<int32_t>(Read32(ec::recording_sites[static_cast<size_t>(ec::RecordingSite::DEPTH_B_CUT)].jump.data() + 1));
  CHECK(b_cut_thunk > ec::recording_page && b_cut_thunk < ec::recording_cut.end);
  // The family and class tables, decoded from the thunks and dispatchers, against the census (design 3.2 and 3.3), written out here
  // on its own: thunk, list base, family, depth body, colour body.
  struct ExpectedFamily {
    uint32_t thunk, base, family, depth_body, colour_body;
  };
  constexpr std::array<ExpectedFamily, 9> EXPECTED = {{{0x1827830, 0x244, 0, 0x17b09f0, 0x17aef40},
                                                       {0x1827860, 0x244, 1, 0x17b09f0, 0x17aef40},
                                                       {0x1827890, 0x244, 2, 0x17b0cb0, 0x17af2b0},
                                                       {0x18278c0, 0x244, 3, 0x17b0f80, 0x17af720},
                                                       {0x1827a10, 0x10c, 4, 0x17b1250, 0x17afc10},
                                                       {0x18278f0, 0x244, 5, 0x17b09f0, 0x17aef40},
                                                       {0x1827920, 0x244, 6, 0x17b1520, 0x17aff90},
                                                       {0x1827950, 0x108, 6, 0x17b1520, 0x17aff90},
                                                       {0x1827980, 0x244, 5, 0x17b09f0, 0x17aef40}}};
  REQUIRE(calls.class_count == ec::ROUTED_CLASSES.size() && ec::ROUTED_CLASSES.size() == 23);
  uint32_t depth_direct = 0;
  uint32_t colour_direct = 0;
  for (size_t index = 0; index < ec::ROUTED_CLASSES.size(); ++index) {
    const ec::RoutedClass& routed = ec::ROUTED_CLASSES[index];
    const dr::MeshClass& decoded = calls.classes[index];
    const auto expected = std::find_if(EXPECTED.begin(), EXPECTED.end(), [&](const ExpectedFamily& family) { return family.thunk == routed.thunk; });
    REQUIRE(expected != EXPECTED.end());
    CHECK(decoded.vtable == ec::Address(Rva(routed.vtable)) && decoded.base == expected->base && decoded.family == expected->family);
    CHECK(decoded.depth_body == ec::Address(Rva(expected->depth_body)) && decoded.colour_body == ec::Address(Rva(expected->colour_body)));
    // The flags: the three classes that run direct in both, the one that does in colour only (M3), the rest record.
    const uint32_t listing = routed.vtable + 0x10000000;
    const bool direct_both = (listing == 0x11cbdc84 || listing == 0x11cbddd0 || listing == 0x11cbcb80);
    CHECK(decoded.depth == !direct_both && decoded.colour == !(direct_both || listing == 0x11cbd9d4));
    depth_direct += (decoded.depth ? 0 : 1);
    colour_direct += (decoded.colour ? 0 : 1);
    CHECK(Read32(Rva(routed.vtable + ec::MESH_VF138)) == ec::Address(Rva(routed.thunk)));
  }
  CHECK(depth_direct == 3 && colour_direct == 4);
  CHECK(calls.accessor == ec::Address(Rva(ec::ACCESSOR_RVA)));
  CHECK(ec::recording_code_used <= ec::RECORDING_CODE_SIZE * 3 / 4);
  std::printf("1. 59 sites found on the real bytes (7 flush call sites, the entry hook, the 2 A cuts, the 2 inits, 3 shared-state calls, 2 animation calls, setup, 2 constants, 2 textures, faction, the B cut, 15 depth-body calls, 15 colour-body calls and 5 state blocks); 23 routed classes decoded (depth direct %u, colour direct %u); %zu of %zu code bytes used\n",
              depth_direct, colour_direct, ec::recording_code_used, ec::RECORDING_CODE_SIZE);
}

// ---------------------------------------------------------------- 2. what a group asks for

void TestWanted() {
  REQUIRE(Prepare());
  CHECK(ec::RecordingWanted(0) == 0);
  CHECK(ec::RecordingWanted(1u << static_cast<uint32_t>(dr::Group::DEPTH_A)) == DEPTH_A_SITES);
  CHECK(ec::RecordingWanted(1u << static_cast<uint32_t>(dr::Group::COLOUR_A)) == COLOUR_A_SITES);
  CHECK(ec::RecordingWanted(0b11) == (DEPTH_A_SITES | COLOUR_A_SITES));
  // Depth B: the cut and its 15 calls with the entry hook (it has no fallback); Colour B: the same cut and its 20 sites.
  CHECK(ec::RecordingWanted(1u << static_cast<uint32_t>(dr::Group::DEPTH_B)) == DEPTH_B_SITES);
  CHECK(ec::RecordingWanted(1u << static_cast<uint32_t>(dr::Group::COLOUR_B)) == COLOUR_B_SITES);
  CHECK(ec::RecordingWanted(0b0101) == (DEPTH_A_SITES | DEPTH_B_SITES));
  CHECK(ec::RecordingWanted(0b1100) == (DEPTH_B_SITES | COLOUR_B_SITES));
  CHECK(ec::RECORDING_FALLBACK[static_cast<size_t>(dr::Group::DEPTH_B)] == 0);
  CHECK((DEPTH_A_SITES & DEPTH_A_FALLBACK & ec::RECORDING_ENTRY_BIT) == 0);
  CHECK((COLOUR_A_SITES & COLOUR_A_FALLBACK & ec::RECORDING_ENTRY_BIT) == 0);
  CHECK((DEPTH_A_FALLBACK & ec::RECORDING_ENTRY_BIT) == 0 && std::popcount(DEPTH_A_FALLBACK) == 4 + 3);
  CHECK(std::popcount(ec::RECORDING_FALLBACK[static_cast<size_t>(dr::Group::COLOUR_A)]) == 4 && std::popcount(COLOUR_A_OWN) == 11);
  // Without the entry hook (its bytes were not as expected): the call sites, all three (depth) or four (colour).
  ec::recording_found &= ~ec::RECORDING_ENTRY_BIT;
  CHECK(ec::RecordingWanted(1) == DEPTH_A_FALLBACK);
  CHECK(ec::RecordingWanted(2) == COLOUR_A_FALLBACK);
  // A missing call site, or a missing own site, and the group asks for nothing.
  const uint64_t found = ec::recording_found;
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
    ec::recording_found = found & ~(uint64_t{1} << index);
    const uint64_t depth = ec::RecordingWanted(1);
    const uint64_t colour = ec::RecordingWanted(2);
    CHECK(ec::recording_found == (found & ~(uint64_t{1} << index)));
    CHECK(depth == (((DEPTH_A_FALLBACK >> index) & 1) != 0 ? uint64_t{0} : DEPTH_A_FALLBACK));
    CHECK(colour == (((COLOUR_A_FALLBACK >> index) & 1) != 0 ? uint64_t{0} : COLOUR_A_FALLBACK));
  }
  // The B groups have no call-site fallback: without the hook, or with any of their own sites (Depth B's 16, Colour B's 21) missing,
  // each asks for nothing.
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
    ec::recording_found = (found | ec::RECORDING_ENTRY_BIT) & ~(uint64_t{1} << index);
    CHECK(ec::RecordingWanted(1u << static_cast<uint32_t>(dr::Group::DEPTH_B)) == (((DEPTH_B_SITES >> index) & 1) != 0 ? uint64_t{0} : DEPTH_B_SITES));
    CHECK(ec::RecordingWanted(1u << static_cast<uint32_t>(dr::Group::COLOUR_B)) == (((COLOUR_B_SITES >> index) & 1) != 0 ? uint64_t{0} : COLOUR_B_SITES));
  }
  ec::recording_found = found;
  CHECK(ec::RecordingWanted(1u << static_cast<uint32_t>(dr::Group::DEPTH_B)) == 0);
  ec::recording_found = found | ec::RECORDING_ENTRY_BIT;
  std::printf("2. group A asks for the entry hook and its 4 own sites, colour A for the hook and its 11; the call sites only without the hook\n");
}

// ---------------------------------------------------------------- 3. every byte flipped

void TestFlips() {
  REQUIRE(Prepare());
  // Every byte Prepare reads: a site's context (its fixed bytes), its callee's head, its call's displacement, its landing's head,
  // the colour pieces' (contexts, callee heads, displacements, the two constants' flag operands), the flush's exit, the `je` to
  // it, the accessor's and the stub's heads, the vtables' slots.
  std::set<uint32_t> positions;
  const auto add_fixed = [&](std::string_view pattern, uint32_t start) {
    for (const size_t fixed : FixedBytes(pattern)) positions.insert(start + static_cast<uint32_t>(fixed));
  };
  for (const ec::RecordingSiteInfo& site_info : ec::RECORDING_INFO) {
    add_fixed(site_info.context, site_info.rva - site_info.context_offset);
    if (site_info.callee_rva != 0) {
      add_fixed(site_info.callee_head, site_info.callee_rva);
      for (uint32_t displacement = 1; displacement < 5; ++displacement) positions.insert(site_info.rva + displacement);
    }
    if (site_info.exit_rva != 0) add_fixed(site_info.exit_head, site_info.exit_rva);
  }
  for (const ec::ColourPieceInfo& piece : ec::COLOUR_PIECES) {
    add_fixed(piece.context, piece.rva - piece.context_offset);
    if (piece.callee_rva != 0) {
      add_fixed(piece.callee_head, piece.callee_rva);
      for (uint32_t displacement = 1; displacement < 5; ++displacement) positions.insert(piece.rva + displacement);
    }
  }
  for (const uint32_t flag_site : {ec::COLOUR_CONST1_RVA, ec::COLOUR_CONST2_RVA}) {
    for (uint32_t byte = 2; byte < 6; ++byte) positions.insert(flag_site + byte);
  }
  add_fixed(ec::ACCESSOR_HEAD, ec::ACCESSOR_RVA);
  add_fixed(ec::VF64_STUB_HEAD, ec::WEIGHTED_VF64);
  add_fixed(ec::FLUSH_EMPTY, ec::FLUSH_EMPTY_RVA);
  for (uint32_t displacement = 0; displacement < 4; ++displacement) positions.insert(ec::FLUSH_RVA + ec::FLUSH_EMPTY_JUMP + 2 + displacement);
  constexpr std::array<uint32_t, 6> SLOTS = {0x54, 0x58, 0x60, 0x64, 0x68, 0x6c};
  for (const ec::WeightedClass& weighted : ec::WEIGHTED_CLASSES) {
    for (const uint32_t slot : SLOTS) {
      for (uint32_t byte = 0; byte < 4; ++byte) positions.insert(weighted.vtable + slot + byte);
    }
  }
  const auto is_fixed = [](std::string_view pattern, uint32_t start, uint32_t at) {
    for (const size_t fixed : FixedBytes(pattern)) {
      if (start + fixed == at) return true;
    }
    return false;
  };
  for (const uint32_t at : positions) {
    // The shared pieces take the whole feature: the flush's head and exit, the `je` between them, the vtables' slots.
    bool shared_piece = is_fixed(ec::FLUSH_HEAD, ec::FLUSH_RVA, at) || is_fixed(ec::FLUSH_EMPTY, ec::FLUSH_EMPTY_RVA, at)
                        || (at >= ec::FLUSH_RVA + ec::FLUSH_EMPTY_JUMP + 2 && at < ec::FLUSH_RVA + ec::FLUSH_EMPTY_JUMP + 6)
                        || is_fixed(ec::VF64_STUB_HEAD, ec::WEIGHTED_VF64, at);
    for (const ec::WeightedClass& weighted : ec::WEIGHTED_CLASSES) {
      for (const uint32_t slot : SLOTS) shared_piece |= (at >= weighted.vtable + slot && at < weighted.vtable + slot + 4);
    }
    uint64_t expected = ALL_SITES;
    for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
      const ec::RecordingSiteInfo& site_info = ec::RECORDING_INFO[index];
      if (is_fixed(site_info.context, site_info.rva - site_info.context_offset, at)
          || (site_info.callee_rva != 0 && (is_fixed(site_info.callee_head, site_info.callee_rva, at) || (at > site_info.rva && at < site_info.rva + 5)))
          || (site_info.exit_rva != 0 && is_fixed(site_info.exit_head, site_info.exit_rva, at))) {
        expected &= ~(uint64_t{1} << index);
      }
    }
    // A colour piece (or the constants' two flag operands, which must be one address) takes the colour loop's own sites.
    bool colour_piece = false;
    for (const ec::ColourPieceInfo& piece : ec::COLOUR_PIECES) {
      colour_piece |= is_fixed(piece.context, piece.rva - piece.context_offset, at);
      colour_piece |= (piece.callee_rva != 0 && (is_fixed(piece.callee_head, piece.callee_rva, at) || (at > piece.rva && at < piece.rva + 5)));
    }
    for (const uint32_t flag_site : {ec::COLOUR_CONST1_RVA, ec::COLOUR_CONST2_RVA}) colour_piece |= (at >= flag_site + 2 && at < flag_site + 6);
    if (colour_piece) expected &= ~(COLOUR_A_OWN | COLOUR_B_ONLY);
    // The accessor is the B groups' alone: its bytes take their sites out, not the feature.
    if (is_fixed(ec::ACCESSOR_HEAD, ec::ACCESSOR_RVA, at)) expected &= ~(DEPTH_B_OWN | COLOUR_B_OWN);
    *Rva(at) ^= 0xFF;
    const bool ok = Prepare();
    CHECK(ec::recording_found == (shared_piece ? 0 : expected));
    CHECK(ok == !shared_piece && (ec::recording_page == nullptr) == shared_piece);
    CHECK(expected != ALL_SITES || shared_piece);  // every byte in the table is read by some site
    CHECK(LogHas("left alone"));
    CHECK(std::count_if(log_lines.begin(), log_lines.end(), [](const std::string& line) { return Mentions(line, "Threaded Draw Recording"); }) == 1);
    *Rva(at) ^= 0xFF;
  }
  CHECK(DifferingBytes() == 0);
  REQUIRE(Prepare());
  CHECK(ec::recording_found == ALL_SITES);
  std::printf("3. %zu single-byte flips: only the sites that read the byte were left out, a shared piece took the whole feature\n", positions.size());
}

// ---------------------------------------------------------------- 4. hooked entries

void TestHookedEntries() {
  for (const uint32_t entry : ec::HOOKED_ENTRIES) {
    const uint8_t saved = *Rva(entry);
    *Rva(entry) = 0xE9;
    CHECK(!Prepare());
    CHECK(ec::recording_found == 0 && ec::recording_page == nullptr);
    CHECK(LogHas("hooked by another tool"));
    *Rva(entry) = saved;
  }
  REQUIRE(Prepare());
  // Passtime starts after Prepare: going in is refused for good, the log names it once.
  for (const uint32_t entry : {0x17b0400u, 0x17b17f0u, 0x17b1520u}) {
    REQUIRE(Prepare());
    *Rva(entry) = 0xE9;
    CHECK(ec::RecordingWanted(1) == 0 && ec::recording_found == 0);
    CHECK(LogHas("hooked by another tool"));
    CHECK(ec::RecordingWanted(1) == 0);
    *Rva(entry) = pristine[entry];
  }
  REQUIRE(Prepare());
  // Our own hook is the E9 of the flush entry once written.
  *Rva(ec::FLUSH_RVA) = 0xE9;
  CHECK(!ec::RecordingEntriesFree(false) && ec::RecordingEntriesFree(true));
  *Rva(ec::FLUSH_RVA) = pristine[ec::FLUSH_RVA];
  CHECK(ec::RecordingEntriesFree(false));
  CHECK(DifferingBytes() == 0);
  std::printf("4. an E9 on any of the 13 entries: Prepare refuses; one after Prepare stops the turn-on; the flush entry's E9 is ours once written\n");
}

// ---------------------------------------------------------------- 5. the window

// Bytes of the image that differ from the game's and are not in the 5 bytes at `site`.
size_t DifferingOutside(const uint8_t* site) {
  size_t count = 0;
  for (uint32_t at = 0; at < image_size; ++at) {
    if (image[at] != pristine[at] && !(image + at >= site && image + at < site + 5)) ++count;
  }
  return count;
}

uint32_t Groups() { return 1u << static_cast<uint32_t>(dr::Group::DEPTH_A); }

void UpdateWith(uint32_t groups) { ecp::Update(false, ecp::Game::ATTILA, false, false, false, false, false, false, 1.f, groups); }

bool SiteHolds(size_t index, bool jump) {
  const ec::Site& site = ec::recording_sites[index];
  return std::memcmp(site.at, (jump ? site.jump : site.game).data(), ec::SITE_SIZE) == 0;
}

// Every site is as it should be (a jump if in `in`, else the game's), but the ones in `skip`.
bool AllHold(uint64_t in, uint64_t skip = 0) {
  bool holds = true;
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
    holds = holds && (((skip >> index) & 1) != 0 || SiteHolds(index, ((in >> index) & 1) != 0));
  }
  return holds;
}

// A one-step fake of the dispatcher, for the thread parked in the cut stub.
std::atomic<uint32_t> fake_dispatches = 0;
uint32_t __fastcall FakeDispatch(void*, void*, void*, void*, uint32_t, uint32_t) {
  fake_dispatches.fetch_add(1);
  return 0xD15;
}

std::atomic<bool> cut_thread_started = false;
DWORD WINAPI CallCut(void* stub) {
  cut_thread_started = true;
  reinterpret_cast<uint32_t(__fastcall*)(void*, void*, void*, void*, uint32_t, uint32_t)>(stub)(nullptr, nullptr, nullptr, nullptr, 0, 0);
  return 0;
}

DWORD WINAPI CallCode(void* code) {
  cut_thread_started = true;
  reinterpret_cast<void(__cdecl*)()>(code)();
  return 0;
}

void TestWindow() {
  REQUIRE(Prepare());
  dr::Shared* const shared = dr::SharedPage();
  const uint32_t arena_size = dr::SLOT_COUNT * dr::SLOT_SIZE;
  const uint64_t in = DEPTH_A_SITES;
  CHECK(shared->arena_size == arena_size);
  // In.
  UpdateWith(Groups());
  CHECK(ec::recording_written == in && ecp::DrawRecordingSitesIn() && shared->arena_size == arena_size);
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) CHECK(SiteHolds(index, ((in >> index) & 1) != 0));
  CHECK(LogHas("sites in"));
  size_t differing = 0;
  for (uint32_t at = 0; at < image_size; ++at) differing += (image[at] != pristine[at] ? 1 : 0);
  CHECK(differing > 0 && differing <= 5 * std::popcount(in));
  // Nothing changes while the row stays on; the 7 call sites stay out beside the hook.
  UpdateWith(Groups());
  CHECK(ec::recording_written == in);
  CHECK(std::all_of(ec::RECORDING_INFO.begin(), ec::RECORDING_INFO.end(), [](const ec::RecordingSiteInfo& info) { return info.kind != ec::RecordingKind::FALLBACK || SiteHolds(&info - ec::RECORDING_INFO.data(), false); }));
  // Out: the game's bytes, the arena size 0.
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && !ecp::DrawRecordingSitesIn() && shared->arena_size == 0 && DifferingBytes() == 0);
  UpdateWith(Groups());
  CHECK(ec::recording_written == in && shared->arena_size == arena_size);
  // A dispatch in flight: the sites stay in until it ends.
  shared->active.store(1);
  UpdateWith(0);
  CHECK(ec::recording_written == in && ec::failed_writes == 1 && shared->arena_size == arena_size);
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) CHECK(SiteHolds(index, ((in >> index) & 1) != 0));
  shared->active.store(0);
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && DifferingBytes() == 0 && shared->arena_size == 0);
  // A thread in the cut stub (it is about to dispatch): the same. The stub's first bytes become a spin (the page is the
  // test's own), and the thread is let go by putting the instruction back.
  UpdateWith(Groups());
  CHECK(ec::recording_written == in);
  const uint32_t saved_dispatch = shared->dispatch_depth;
  shared->dispatch_depth = ec::Address(reinterpret_cast<const void*>(&FakeDispatch));
  DWORD protection;
  VirtualProtect(ec::recording_page, ec::RECORDING_CODE_SIZE, PAGE_EXECUTE_READWRITE, &protection);
  uint16_t original_pair;
  std::memcpy(&original_pair, ec::recording_page, 2);
  const uint16_t spin = 0xFEEB;
  std::memcpy(ec::recording_page, &spin, 2);
  cut_thread_started = false;
  HANDLE thread = CreateThread(nullptr, 0, CallCut, ec::recording_page, 0, nullptr);
  while (!cut_thread_started) Sleep(1);
  Sleep(50);
  ec::failed_writes = 0;
  UpdateWith(0);
  CHECK(ec::recording_written == in && ec::failed_writes == 1);
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) CHECK(SiteHolds(index, ((in >> index) & 1) != 0));
  std::memcpy(ec::recording_page, &original_pair, 2);  // one aligned 16-bit store
  CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
  CloseHandle(thread);
  CHECK(fake_dispatches == 1 && shared->active.load() == 0);
  VirtualProtect(ec::recording_page, ec::RECORDING_CODE_SIZE, PAGE_EXECUTE_READ, &protection);
  shared->dispatch_depth = saved_dispatch;
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && DifferingBytes() == 0);
  // Another mod's bytes on a site: going in writes none of the group, the feature is left out for good.
  uint8_t* const modded = ec::recording_sites[static_cast<size_t>(ec::RecordingSite::DEPTH_VF68)].at;
  modded[0] ^= 0x55;
  log_lines.clear();
  UpdateWith(Groups());
  CHECK(ec::recording_written == 0 && (ec::recording_found & in) == 0 && shared->arena_size == 0);
  CHECK(LogHas("left alone"));
  CHECK(DifferingBytes() == 1);
  UpdateWith(Groups());
  CHECK(ec::recording_written == 0);
  modded[0] ^= 0x55;
  // Coming out with one site changed by another mod since: that one is left, the others go back, the arena size is 0.
  REQUIRE(Prepare());
  UpdateWith(Groups());
  CHECK(ec::recording_written == in);
  uint8_t* const anim = ec::recording_sites[static_cast<size_t>(ec::RecordingSite::DEPTH_ANIM)].at;
  anim[1] ^= 0x55;
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && shared->arena_size == 0);
  CHECK(DifferingOutside(anim) == 0 && !SiteHolds(static_cast<size_t>(ec::RecordingSite::DEPTH_ANIM), false));
  std::memcpy(anim, &pristine[ec::DEPTH_ANIM_RVA], 5);
  // A thread inside a site past its first byte (here a spin the test puts in the site's own bytes, in its own copy): nothing is
  // written, as for the 25 sites; once it has left the sites go in.
  REQUIRE(Prepare());
  uint8_t* const spin_site = ec::recording_sites[static_cast<size_t>(ec::RecordingSite::DEPTH_INIT)].at;
  VirtualProtect(spin_site, 8, PAGE_EXECUTE_READWRITE, &protection);
  constexpr std::array<uint8_t, 4> SPIN_CODE = {0x90, 0xEB, 0xFE, 0xC3};  // nop; jmp $; ret
  std::memcpy(spin_site, SPIN_CODE.data(), SPIN_CODE.size());
  cut_thread_started = false;
  thread = CreateThread(nullptr, 0, CallCode, spin_site, 0, nullptr);
  while (!cut_thread_started) Sleep(1);
  Sleep(50);
  UpdateWith(Groups());
  CHECK(ec::recording_written == 0 && ec::failed_writes == 1 && DifferingOutside(spin_site) == 0);
  const uint16_t jump_on = 0x00EB;  // jmp +0: falls through to the ret
  std::memcpy(spin_site + 1, &jump_on, 2);
  CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
  CloseHandle(thread);
  std::memcpy(spin_site, &pristine[ec::DEPTH_LOOP_RVA + ec::DEPTH_INIT_OFFSET], 5);
  VirtualProtect(spin_site, 8, protection, &protection);
  CHECK(DifferingBytes() == 0);
  UpdateWith(Groups());
  CHECK(ec::recording_written == in);
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && DifferingBytes() == 0);
  // Uninstall that cannot finish (a dispatch never ends) gives up and leaves the sites in; then it finishes.
  REQUIRE(Prepare());
  UpdateWith(Groups());
  CHECK(ec::recording_written == in);
  shared->active.store(1);
  log_lines.clear();
  ecp::Uninstall();
  CHECK(ec::recording_written == in && ecp::DrawRecordingSitesIn() && LogHas("left in at detach"));
  shared->active.store(0);
  ecp::Uninstall();
  CHECK(ec::recording_written == 0 && ecp::DrawRecordingSitesIn() && DifferingBytes() == 0);  // the give-up stays noted
  std::printf("5. window: in, out, arena size, dispatch in flight, thread in the cut stub, thread in a site, another mod's bytes (in / out), Uninstall\n");
}

// A thread parked on 4 bytes of the test's own copy of the page or the game's data (`nop; jmp $; ret` over them) until Release.
struct Parked {
  uint8_t* code = nullptr;
  std::array<uint8_t, 4> saved = {};
  DWORD protection = 0;
  HANDLE thread = nullptr;
};

Parked Park(uint8_t* code) {
  constexpr std::array<uint8_t, 4> SPIN_CODE = {0x90, 0xEB, 0xFE, 0xC3};
  Parked parked = {.code = code};
  VirtualProtect(code, 4, PAGE_EXECUTE_READWRITE, &parked.protection);
  std::memcpy(parked.saved.data(), code, 4);
  std::memcpy(code, SPIN_CODE.data(), 4);
  cut_thread_started = false;
  parked.thread = CreateThread(nullptr, 0, CallCode, code, 0, nullptr);
  while (!cut_thread_started) Sleep(1);
  Sleep(50);
  return parked;
}

void Release(Parked* parked) {
  const uint16_t jump_on = 0x00EB;  // jmp +0: falls through to the ret
  std::memcpy(parked->code + 1, &jump_on, sizeof(jump_on));
  CHECK(WaitForSingleObject(parked->thread, 5000) == WAIT_OBJECT_0);
  CloseHandle(parked->thread);
  std::memcpy(parked->code, parked->saved.data(), 4);
  VirtualProtect(parked->code, 4, parked->protection, &parked->protection);
}

uint8_t* ThunkOf(ec::RecordingSite recording_site) {
  const ec::Site& site = ec::recording_sites[static_cast<size_t>(recording_site)];
  return reinterpret_cast<uint8_t*>(ec::Address(site.at + ec::SITE_SIZE) + Read32(site.jump.data() + 1));
}

uint8_t* SiteAt(ec::RecordingSite recording_site) { return ec::recording_sites[static_cast<size_t>(recording_site)].at; }

DWORD ProtectionAt(const void* at) {
  MEMORY_BASIC_INFORMATION information = {};
  VirtualQuery(at, &information, sizeof(information));
  return information.Protect;
}

#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
// M1: every page of the sites going in is made writable first; one that cannot be stands the whole table down with nothing written
// and the protections of the ones already changed back.
void TestProtectFailure() {
  const uint64_t in = DEPTH_A_SITES;
  const uint32_t count = std::popcount(in);
  DWORD ignored;
  const auto set_protection = [&](DWORD protection) {
    for (const ec::Site& site : ec::recording_sites) VirtualProtect(site.at, ec::SITE_SIZE, protection, &ignored);
  };
  const auto protections_are = [&](DWORD protection) {
    return std::all_of(ec::recording_sites.begin(), ec::recording_sites.end(),
                       [&](const ec::Site& site) { return ProtectionAt(site.at) == protection; });
  };
  for (const uint32_t failing : {0u, 2u, count - 1}) {
    REQUIRE(Prepare());
    set_protection(PAGE_READONLY);
    ec::recording_fail_protect = static_cast<int32_t>(failing);
    UpdateWith(Groups());
    CHECK(ec::recording_written == 0 && (ec::recording_found & in) == 0 && DifferingBytes() == 0 && ec::failed_writes == 0);
    CHECK(LogHas("left alone") && LogHas("could not be made writable") && !LogHas("sites in"));
    CHECK(protections_are(PAGE_READONLY) && !ecp::DrawRecordingSitesIn() && ec::recording_fail_protect == -1);
    UpdateWith(Groups());
    CHECK(ec::recording_written == 0);
    set_protection(PAGE_READWRITE);
  }
  // The control: no call fails, the sites go in, and every page is back as it was (sites sharing a page too).
  REQUIRE(Prepare());
  set_protection(PAGE_READONLY);
  ec::recording_fail_protect = static_cast<int32_t>(count);
  UpdateWith(Groups());
  CHECK(ec::recording_written == in && protections_are(PAGE_READONLY));
  ec::recording_fail_protect = -1;
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && DifferingBytes() == 0 && protections_are(PAGE_READONLY));
  set_protection(PAGE_READWRITE);
  std::printf("5b. a VirtualProtect that fails (first, middle, last site going in): nothing written, protections back, the table stood down\n");
}
#endif

// M2: a thread in a thunk's direct path (any thunk past the cut stubs) blocks the last removal, not a partial one.
void TestThreadInThunks() {
  REQUIRE(Prepare());
  dr::Shared* const shared = dr::SharedPage();
  const uint32_t arena_size = ec::recording_arena_size_value;
  REQUIRE(arena_size != 0);
  const uint64_t in = DEPTH_A_SITES;
  const uint64_t entry = ec::RECORDING_ENTRY_BIT;
  UpdateWith(Groups());
  CHECK(ec::recording_written == in);
  uint8_t* const flush_thunk = ThunkOf(ec::RecordingSite::FLUSH_ENTRY);
  REQUIRE(flush_thunk >= ec::recording_cut.end && flush_thunk + 4 <= ec::recording_page_end);
  Parked parked = Park(flush_thunk);
  // Partial: the entry hook alone comes out (the call Update makes for it); the thread is not in a cut stub.
  uint32_t stood_down = 0;
  uint64_t recording_stood_down = 0;
  CHECK(ec::PausedWrite(0, &stood_down, in & ~entry, &recording_stood_down) && recording_stood_down == 0);
  CHECK(SiteHolds(static_cast<size_t>(ec::RecordingSite::FLUSH_ENTRY), false) && shared->arena_size == arena_size);
  ec::recording_written = in & ~entry;
  // Last: nothing stays in, so a thread anywhere in the page blocks it.
  ec::failed_writes = 0;
  UpdateWith(0);
  CHECK(ec::recording_written == (in & ~entry) && ec::failed_writes == 1 && shared->arena_size == arena_size);
  CHECK(ecp::DrawRecordingSitesIn());
  Release(&parked);
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && DifferingBytes() == 0 && shared->arena_size == 0 && !ecp::DrawRecordingSitesIn());
  // Going in is not held by a thread in the page.
  REQUIRE(Prepare());
  parked = Park(ThunkOf(ec::RecordingSite::DEPTH_VF68));
  UpdateWith(Groups());
  CHECK(ec::recording_written == in);
  Release(&parked);
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && DifferingBytes() == 0);
  std::printf("5c. a thread in a thunk's direct path: blocks the last removal, not a partial one, not going in\n");
}

// S1 and S3: the arena size stays while a site stays in; a site that stood down on the way out keeps the feature reachable.
// S5: a mismatch stands down its own group, the entry hook goes on while a group is in; the log names the sites that changed.
void TestStandDown() {
  REQUIRE(Prepare());
  dr::Shared* const shared = dr::SharedPage();
  const uint32_t arena_size = ec::recording_arena_size_value;
  REQUIRE(arena_size != 0);
  const uint64_t in = DEPTH_A_SITES;
  const uint64_t entry = ec::RECORDING_ENTRY_BIT;
  const uint64_t first = ec::RecordingBit(ec::RecordingSite::DEPTH_CUT) | ec::RecordingBit(ec::RecordingSite::DEPTH_INIT);
  const uint64_t second = ec::RecordingBit(ec::RecordingSite::DEPTH_VF68) | ec::RecordingBit(ec::RecordingSite::DEPTH_ANIM);
  // Two groups split the depth loop's sites for this test (the real second group arrives with colour).
  const std::array<uint64_t, 4> real_groups = ec::recording_group_sites;
  ec::recording_group_sites = {first, second, 0, 0};
  uint8_t* const modded = SiteAt(ec::RecordingSite::DEPTH_VF68);
  modded[0] ^= 0x55;
  log_lines.clear();
  UpdateWith(Groups());
  CHECK(ec::recording_written == (first | entry) && (ec::recording_found & second) == 0 && (ec::recording_found & (first | entry)) == (first | entry));
  CHECK(shared->arena_size == arena_size && ecp::DrawRecordingSitesIn());
  CHECK(LogHas("depth loop shared-state call") && !LogHas("depth loop cut") && !LogHas("depth loop animation call"));
  // Out: the group that stayed comes out, and the arena size is 0 only then.
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && shared->arena_size == 0 && !ecp::DrawRecordingSitesIn());
  modded[0] ^= 0x55;
  // Both groups changed: the entry hook has no group left and stands down with them.
  REQUIRE(Prepare());
  ec::recording_group_sites = {first, second, 0, 0};
  SiteAt(ec::RecordingSite::DEPTH_CUT)[0] ^= 0x55;
  SiteAt(ec::RecordingSite::DEPTH_ANIM)[0] ^= 0x55;
  UpdateWith(Groups());
  CHECK(ec::recording_written == 0 && (ec::recording_found & in) == 0);
  SiteAt(ec::RecordingSite::DEPTH_CUT)[0] ^= 0x55;
  SiteAt(ec::RecordingSite::DEPTH_ANIM)[0] ^= 0x55;
  CHECK(DifferingBytes() == 0);
  // The entry hook changed: every group stands down with it.
  REQUIRE(Prepare());
  ec::recording_group_sites = {first, second, 0, 0};
  uint8_t* const hook = SiteAt(ec::RecordingSite::FLUSH_ENTRY);
  hook[1] ^= 0x55;
  log_lines.clear();
  UpdateWith(Groups());
  CHECK(ec::recording_written == 0 && (ec::recording_found & in) == 0 && LogHas("flush entry") && !LogHas("depth loop cut"));
  hook[1] ^= 0x55;
  // Coming out with one site changed since: the others go back, and the feature stays reachable.
  REQUIRE(Prepare());
  UpdateWith(Groups());
  CHECK(ec::recording_written == in && !LogHas("left alone"));
  uint8_t* const anim = SiteAt(ec::RecordingSite::DEPTH_ANIM);
  anim[1] ^= 0x55;
  log_lines.clear();
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && shared->arena_size == 0 && ecp::DrawRecordingSitesIn() && LogHas("depth loop animation call"));
  CHECK(!LogHas("depth loop cut"));
  UpdateWith(0);
  CHECK(ecp::DrawRecordingSitesIn());  // sticky until the next Prepare
  std::memcpy(anim, &pristine[ec::DEPTH_ANIM_RVA], ec::SITE_SIZE);
  REQUIRE(Prepare());
  CHECK(!ecp::DrawRecordingSitesIn() && DifferingBytes() == 0);
  ec::recording_group_sites = real_groups;
  std::printf("5d. stand-down: by group, the entry hook with its groups, the arena size, the sticky left-in flag, the log names the changed sites\n");
}

// S2: a hooked entry found at a turn-on refuses for good, and what is written can still come out.
void TestRefusedTurnOn() {
  REQUIRE(Prepare());
  dr::Shared* const shared = dr::SharedPage();
  const uint64_t in = DEPTH_A_SITES;
  UpdateWith(Groups());
  CHECK(ec::recording_written == in);
  // One site is not written yet (as for a group joining later), and passtime has started.
  const ec::Site& anim = ec::recording_sites[static_cast<size_t>(ec::RecordingSite::DEPTH_ANIM)];
  std::memcpy(anim.at, anim.game.data(), ec::SITE_SIZE);
  ec::recording_written &= ~ec::RecordingBit(ec::RecordingSite::DEPTH_ANIM);
  *Rva(0x17b0400) = 0xE9;
  log_lines.clear();
  CHECK(ec::RecordingWanted(1) == 0);
  CHECK(ec::recording_found == ec::recording_written && ec::recording_refused && LogHas("hooked by another tool"));
  CHECK(ecp::DrawRecordingSitesIn());
  UpdateWith(Groups());  // asks for nothing now: what is written comes out
  CHECK(ec::recording_written == 0 && shared->arena_size == 0 && !ecp::DrawRecordingSitesIn());
  *Rva(0x17b0400) = pristine[0x17b0400];
  CHECK(DifferingBytes() == 0);
  UpdateWith(Groups());
  CHECK(ec::recording_written == 0);  // refused for the session
  std::printf("5e. a hooked entry at a turn-on: nothing goes in again, the sites written can still come out\n");
}

// Colour A joins depth A through the same window: its 11 own sites and the one entry hook; each group goes in and out on its own,
// and a mismatch stands down only the group it is in.
void TestColourGroup() {
  const uint32_t depth_row = 1u << static_cast<uint32_t>(dr::Group::DEPTH_A);
  const uint32_t colour_row = 1u << static_cast<uint32_t>(dr::Group::COLOUR_A);
  REQUIRE(Prepare());
  dr::Shared* const shared = dr::SharedPage();
  const uint32_t arena_size = ec::recording_arena_size_value;
  // The bytes that differ from the game's outside the sites written (a site's 5 bytes).
  const auto differing_outside_written = [] {
    size_t count = 0;
    for (uint32_t at = 0; at < image_size; ++at) {
      bool in_site = false;
      for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
        const uint8_t* const site = ec::recording_sites[index].at;
        in_site = in_site || (((ec::recording_written >> index) & 1) != 0 && image + at >= site && image + at < site + ec::SITE_SIZE);
      }
      count += (image[at] != pristine[at] && !in_site ? 1 : 0);
    }
    return count;
  };
  UpdateWith(colour_row);
  CHECK(ec::recording_written == COLOUR_A_SITES && shared->arena_size == arena_size && AllHold(COLOUR_A_SITES));
  CHECK(std::popcount(ec::recording_written) == 12 && ecp::DrawRecordingSitesIn());
  UpdateWith(colour_row | depth_row);
  CHECK(ec::recording_written == (COLOUR_A_SITES | DEPTH_A_SITES) && AllHold(COLOUR_A_SITES | DEPTH_A_SITES));
  // The row of one off: its own sites come out, the entry hook stays for the other, the arena size too.
  UpdateWith(depth_row);
  CHECK(ec::recording_written == DEPTH_A_SITES && shared->arena_size == arena_size && AllHold(DEPTH_A_SITES));
  UpdateWith(colour_row | depth_row);
  UpdateWith(colour_row);
  CHECK(ec::recording_written == COLOUR_A_SITES && AllHold(COLOUR_A_SITES));
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && shared->arena_size == 0 && !ecp::DrawRecordingSitesIn() && DifferingBytes() == 0);
  // Another mod's bytes on one colour site: the colour group stays out, the depth group goes in, the log names the site.
  REQUIRE(Prepare());
  uint8_t* const modded = SiteAt(ec::RecordingSite::COLOUR_CONST2);
  modded[0] ^= 0x55;
  log_lines.clear();
  UpdateWith(colour_row | depth_row);
  const uint64_t modded_bit = ec::RecordingBit(ec::RecordingSite::COLOUR_CONST2);
  CHECK(ec::recording_written == DEPTH_A_SITES && (ec::recording_found & COLOUR_A_OWN) == 0 && AllHold(DEPTH_A_SITES, modded_bit));
  CHECK(LogHas("colour loop second constant") && !LogHas("depth loop") && differing_outside_written() == 1);
  modded[0] ^= 0x55;
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && DifferingBytes() == 0);
  // And the other way: one depth site changed, colour goes in alone with the entry hook.
  REQUIRE(Prepare());
  uint8_t* const depth_modded = SiteAt(ec::RecordingSite::DEPTH_ANIM);
  depth_modded[0] ^= 0x55;
  UpdateWith(colour_row | depth_row);
  CHECK(ec::recording_written == COLOUR_A_SITES && (ec::recording_found & DEPTH_A_SITES & ~ec::RECORDING_ENTRY_BIT) == 0);
  CHECK(AllHold(COLOUR_A_SITES, ec::RecordingBit(ec::RecordingSite::DEPTH_ANIM)) && differing_outside_written() == 1);
  depth_modded[0] ^= 0x55;
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && DifferingBytes() == 0);
  // The sites, each with the bytes it covers, overlap none other (the covered spans are copied into the thunks whole).
  for (size_t first = 0; first < ec::RECORDING_SITE_COUNT; ++first) {
    for (size_t second = first + 1; second < ec::RECORDING_SITE_COUNT; ++second) {
      const ec::RecordingSiteInfo& a = ec::RECORDING_INFO[first];
      const ec::RecordingSiteInfo& b = ec::RECORDING_INFO[second];
      CHECK(a.rva + std::max<uint32_t>(a.covered, ec::SITE_SIZE) <= b.rva || b.rva + std::max<uint32_t>(b.covered, ec::SITE_SIZE) <= a.rva);
    }
  }
  std::printf("5f. colour A: its 11 sites and the hook in and out beside depth A, a mismatch stands down its own group only, no two sites overlap\n");
}

// ---------------------------------------------------------------- 6. the thunks on synthetic code

struct Seen {
  uint32_t calls = 0;
  uint32_t site = 0;
  dr::Slot* slot = nullptr;
  uint32_t ecx = 0;
  uint32_t esi = 0;
  uint32_t eax = 0;
  uint32_t xmm0 = 0;
  uint32_t edx = 0;
  uint32_t edi = 0;
  uint32_t ebp = 0;
  uint32_t ebx = 0;
  uint32_t eflags = 0;
  std::array<uint32_t, 12> stack = {};
};
Seen seen;
uint32_t record_sets_eax = 0x600D;

void __fastcall FakeRecord(dr::Slot* slot, dr::Frame* frame) {
  ++seen.calls;
  seen.site = frame->site;
  seen.slot = slot;
  seen.ecx = frame->ecx;
  seen.esi = frame->esi;
  seen.eax = frame->eax;
  seen.xmm0 = frame->xmm0;
  seen.edx = frame->edx;
  seen.edi = frame->edi;
  seen.ebp = frame->ebp;
  seen.ebx = frame->ebx;
  seen.eflags = frame->eflags;
  std::memcpy(seen.stack.data(), frame + 1, sizeof(seen.stack));
  if (frame->site < static_cast<uint32_t>(dr::Site::COUNT)) frame->eax = record_sets_eax;
}

std::atomic<uint32_t> direct_calls = 0;
std::array<uint32_t, 12> direct_args = {};
uint32_t direct_ecx = 0;
uint32_t direct_edx = 0;
uint32_t direct_flags = 0;
uint32_t __fastcall FakeFlush(void* ecx, void* edx, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8,
                              uint32_t a9, uint32_t a10) {
  ++direct_calls;
  direct_ecx = reinterpret_cast<uint32_t>(ecx);
  direct_edx = reinterpret_cast<uint32_t>(edx);
  direct_flags = __readeflags();
  direct_args = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10};
  return 0xF105;
}
uint32_t __fastcall FakeVf68(void* ecx, void* edx, uint32_t a1, uint32_t a2) {
  ++direct_calls;
  direct_ecx = reinterpret_cast<uint32_t>(ecx);
  direct_edx = reinterpret_cast<uint32_t>(edx);
  direct_flags = __readeflags();
  direct_args = {a1, a2};
  return 0x6868;
}
uint32_t __fastcall FakeAnimation(void* ecx, void* edx, uint32_t a1) {
  ++direct_calls;
  direct_ecx = reinterpret_cast<uint32_t>(ecx);
  direct_edx = reinterpret_cast<uint32_t>(edx);
  direct_flags = __readeflags();
  direct_args = {a1};
  return 0xA111;
}

struct HarnessOut {
  uint32_t balance = 0xFFFFFFFF;
  uint32_t esi = 0;
  uint32_t ebx = 0;
  uint32_t edi = 0;
  uint32_t eax = 0;
};
HarnessOut harness_out;
// What the harness puts in edx and sets (DF) before the call, and what it keeps for the checks of the registers that must come
// through a thunk: the frame pointer, the arguments' address.
constexpr uint32_t EDX_IN = 0xED5ED5ED;
constexpr uint32_t DIRECTION_FLAG = 0x400;
uint32_t harness_ebp = 0;
const uint32_t* harness_args = nullptr;
// The low float of xmm0 at the call (a constant's value), and which of the game's places after a call the thunk went on at:
// 1 the call's return address, 2 the landing (only a harness built with one has it).
uint32_t harness_xmm0 = 0;
uint32_t harness_path = 0;

// cdecl: uint32_t(esi, target, args, ecx, eax). Pushes `pushed` dwords of args (args[0] ends lowest: the first the callee
// sees), calls target (the callee pops `popped` of them), then drops the rest and reports esp's balance and ebx esi edi.
// `landing`: where the game's code goes on past the call (a thunk may land there instead of at the return address, with
// the stack as it was before the call); harness_path tells which it was.
using HarnessFn = uint32_t(__cdecl*)(uint32_t, const void*, const uint32_t*, uint32_t, uint32_t);
HarnessFn BuildHarness(ec::Assembler* assembler, uint32_t pushed, uint32_t popped, const uint8_t** landing = nullptr) {
  auto* const entry = reinterpret_cast<HarnessFn>(assembler->Cursor());
  assembler->Bytes({0x55, 0x8B, 0xEC, 0x56, 0x53, 0x57});
  assembler->Bytes({0x8B, 0x75, 0x08, 0x8B, 0x5D, 0x10, 0x8B, 0x4D, 0x14, 0x8B, 0x45, 0x18});
  assembler->BytesAndOperand({0xBF}, 0xDEADBEEF);
  for (uint32_t index = pushed; index > 0; --index) assembler->Bytes({0xFF, 0x73, static_cast<uint8_t>(4 * (index - 1))});
  assembler->BytesAndOperand({0x89, 0x2D}, ec::Address(&harness_ebp));
  assembler->BytesAndOperand({0xF3, 0x0F, 0x10, 0x05}, ec::Address(&harness_xmm0));  // movss xmm0, [harness_xmm0]
  assembler->BytesAndOperand({0xBA}, EDX_IN);
  assembler->Bytes({0xFD, 0xFF, 0x55, 0x0C, 0xFC});  // std; call [ebp + 0xC]; cld
  if (landing != nullptr) {
    assembler->BytesAndOperand({0xC7, 0x05}, ec::Address(&harness_path));
    assembler->Bytes({1, 0, 0, 0});
    uint8_t* const done = assembler->Short(0xEB);
    *landing = assembler->Cursor();
    assembler->Bytes({0xFC});
    assembler->BytesAndOperand({0xC7, 0x05}, ec::Address(&harness_path));
    assembler->Bytes({2, 0, 0, 0});
    assembler->Bind(done);
  }
  if (pushed > popped) assembler->Bytes({0x83, 0xC4, static_cast<uint8_t>(4 * (pushed - popped))});
  assembler->Bytes({0x8D, 0x55, 0xF4, 0x2B, 0xD4});
  assembler->BytesAndOperand({0x89, 0x15}, ec::Address(&harness_out.balance));
  assembler->BytesAndOperand({0x89, 0x35}, ec::Address(&harness_out.esi));
  assembler->BytesAndOperand({0x89, 0x1D}, ec::Address(&harness_out.ebx));
  assembler->BytesAndOperand({0x89, 0x3D}, ec::Address(&harness_out.edi));
  assembler->Bytes({0x5F, 0x5B, 0x5E, 0x5D, 0xC3});
  return entry;
}

uint8_t* test_code = nullptr;

// `ebx_out`: ebx after the call when the thunk's path changes it (else the arguments' address, as it went in).
uint32_t Call(HarnessFn harness, uint32_t esi, const void* target, const std::vector<uint32_t>& args, uint32_t ecx, uint32_t eax = 0,
              uint32_t ebx_out = 0) {
  harness_out = {};
  seen = {};
  direct_calls = 0;
  harness_path = 0;
  harness_args = args.data();
  const uint32_t result = harness(esi, target, args.data(), ecx, eax);
  harness_out.eax = result;
  CHECK(harness_out.balance == 0);
  CHECK(harness_out.edi == 0xDEADBEEF);
  CHECK(harness_out.ebx == (ebx_out != 0 ? ebx_out : reinterpret_cast<uint32_t>(args.data())));
  CHECK(harness_out.esi == esi);
  return result;
}

// edx and DF come through a thunk's direct path; a record path hands Record every register and the flags as the game had them.
void CheckDirect() { CHECK(direct_edx == EDX_IN && (direct_flags & DIRECTION_FLAG) != 0); }
void CheckRecordFrame(uint32_t esi) {
  CHECK(seen.edx == EDX_IN && seen.edi == 0xDEADBEEF && seen.ebx == reinterpret_cast<uint32_t>(harness_args) && seen.ebp == harness_ebp);
  CHECK(seen.esi == esi && (seen.eflags & DIRECTION_FLAG) != 0);
}

void TestThunks() {
  REQUIRE(Prepare());
  dr::Shared* const shared = dr::SharedPage();
  shared->arena_size = ec::recording_arena_size_value;  // the window left it 0: no site in
  const uint32_t saved_record = shared->record;
  shared->record = ec::Address(reinterpret_cast<const void*>(&FakeRecord));
  const uint32_t arena = shared->arena;
  const uint32_t arena_size = shared->arena_size;
  test_code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  ec::Assembler assembler(std::span<uint8_t>(test_code, 0x4000));
  const HarnessFn harness10 = BuildHarness(&assembler, 10, 10);
  const HarnessFn harness2 = BuildHarness(&assembler, 2, 2);
  const HarnessFn harness4 = BuildHarness(&assembler, 4, 4);
  const HarnessFn harness_anim = BuildHarness(&assembler, 10, 1);
  // Not in the arena: a pointer on the stack, one below the arena, the first byte past it. In: the first slot, the last, any
  // byte of a slot.
  std::array<uint8_t, 0x200> local_tracker = {};
  const uint32_t outside = reinterpret_cast<uint32_t>(local_tracker.data());
  CHECK(outside - arena >= arena_size);
  const std::array<uint32_t, 2> outsides = {arena - 1, arena + arena_size};
  const std::array<uint32_t, 3> insides = {arena, arena + 5 * dr::SLOT_SIZE + 0x40, arena + arena_size - dr::SLOT_SIZE};

  // The flush's entry. Direct: the game's two instructions and its code on (here a stand-in that reports the flags of the
  // compare and returns as the game does); record: Record sees the slot and the frame, the call returns at once.
  uint32_t resume_flags = 0;
  uint32_t resume_edx = 0;
  uint8_t* const resume = assembler.Cursor();
  assembler.BytesAndOperand({0x89, 0x15}, ec::Address(&resume_edx));
  assembler.Bytes({0x9C, 0x58});  // pushfd; pop eax
  assembler.BytesAndOperand({0xA3}, ec::Address(&resume_flags));
  assembler.Bytes({0xB8, 0x45, 0xE5, 0x00, 0x00, 0x83, 0xC4, 0x1C, 0xC2, 0x28, 0x00});  // mov eax, 0xE545; add esp, 0x1c; ret 0x28
  const uint8_t* const entry_thunk = ec::BuildFlushEntry(&assembler, shared, resume);
  REQUIRE(assembler.Fits());
  const auto flush_args = [](uint32_t any, uint32_t count) { return std::vector<uint32_t>{0xA1, 0xA2, 0xA3, any, count, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA}; };
  const std::array<uint32_t, 3> entry_outsides = {outside, arena - 0xA0, arena + arena_size};  // whole objects: their +0x69 is outside too
  for (const uint32_t tracker : entry_outsides) {
    resume_flags = 0;
    CHECK(Call(harness10, 0x5151, entry_thunk, flush_args(tracker + 0x69, 7), 0xEC0, 0) == 0xE545);
    CHECK(seen.calls == 0 && (resume_flags & 0x40) == 0 && resume_edx == EDX_IN && (resume_flags & DIRECTION_FLAG) != 0);
    resume_flags = 0;
    Call(harness10, 0x5151, entry_thunk, flush_args(tracker + 0x69, 0), 0xEC0, 0);
    CHECK(seen.calls == 0 && (resume_flags & 0x40) != 0);  // the game's own compare: no instances
  }
  for (const uint32_t tracker : insides) {
    resume_flags = 0;
    Call(harness10, 0x5151, entry_thunk, flush_args(tracker + 0x69, 3), 0xEC0, 0);
    CHECK(seen.calls == 1 && resume_flags == 0);
    CheckRecordFrame(0x5151);
    CHECK(seen.site == static_cast<uint32_t>(dr::Site::FLUSH_ENTRY) && reinterpret_cast<uint32_t>(seen.slot) == tracker && seen.ecx == 0xEC0);
    CHECK(seen.stack[0] != 0);
    for (uint32_t argument = 1; argument <= 10; ++argument) CHECK(seen.stack[argument] == flush_args(tracker + 0x69, 3)[argument - 1]);
    // Count 0 in a slot: returns at once, Record is not called.
    Call(harness10, 0x5151, entry_thunk, flush_args(tracker + 0x69, 0), 0xEC0, 0);
    CHECK(seen.calls == 0 && resume_flags == 0);
  }

  // A flush's call site: esi is the tracker.
  const uint8_t* const call_thunk = ec::BuildFlushCall(&assembler, shared, reinterpret_cast<const uint8_t*>(&FakeFlush), dr::Site::DEPTH_FLUSH_END);
  for (const uint32_t tracker : {outside, outsides[0], outsides[1]}) {
    CHECK(Call(harness10, tracker, call_thunk, flush_args(tracker + 0x69, 3), 0xEC0, 0) == 0xF105);
    CHECK(seen.calls == 0 && direct_calls == 1 && direct_ecx == 0xEC0 && direct_args == (std::array<uint32_t, 12>{0xA1, 0xA2, 0xA3, tracker + 0x69, 3, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0, 0}));
    CheckDirect();
  }
  for (const uint32_t tracker : insides) {
    Call(harness10, tracker, call_thunk, flush_args(tracker + 0x69, 3), 0xEC0, 0);
    CHECK(direct_calls == 0 && seen.calls == 1 && seen.site == static_cast<uint32_t>(dr::Site::DEPTH_FLUSH_END) && reinterpret_cast<uint32_t>(seen.slot) == tracker);
    CHECK(seen.ecx == 0xEC0 && seen.esi == tracker && seen.stack[4] == tracker + 0x69 && seen.stack[5] == 3);
    CheckRecordFrame(tracker);
    Call(harness10, tracker, call_thunk, flush_args(tracker + 0x69, 0), 0xEC0, 0);
    CHECK(seen.calls == 1);  // Record is called; it drops a flush of no instances itself
  }

  // The shared-state call: eax is the mesh's vtable.
  std::array<std::array<uint32_t, 0x20>, 11> fake_vtables = {};
  std::array<uint32_t, 10> known = {};
  for (size_t index = 0; index < fake_vtables.size(); ++index) {
    fake_vtables[index][0x68 / 4] = ec::Address(reinterpret_cast<const void*>(&FakeVf68));
    if (index < known.size()) known[index] = ec::Address(fake_vtables[index].data());
  }
  const uint8_t* const vf68_thunk = ec::BuildSharedStateCall(&assembler, shared, &known, 0x68, 8, dr::Site::DEPTH_VF68);
  for (const uint32_t tracker : {outside, outsides[0], outsides[1]}) {
    CHECK(Call(harness2, tracker, vf68_thunk, {0x11, 0x22}, 0xBEEF, ec::Address(fake_vtables[10].data())) == 0x6868);
    CHECK(seen.calls == 0 && direct_calls == 1 && direct_ecx == 0xBEEF && direct_args[0] == 0x11 && direct_args[1] == 0x22);
    CheckDirect();
  }
  for (const uint32_t tracker : insides) {
    for (size_t index = 0; index < known.size(); ++index) {
      CHECK(Call(harness2, tracker, vf68_thunk, {0x11, 0x22}, 0xBEEF, known[index]) == 0x600D);
      CHECK(seen.calls == 1 && direct_calls == 0 && seen.site == static_cast<uint32_t>(dr::Site::DEPTH_VF68) && reinterpret_cast<uint32_t>(seen.slot) == tracker);
      CHECK(seen.ecx == 0xBEEF && seen.eax == known[index] && seen.stack[1] == 0x11 && seen.stack[2] == 0x22);
      CheckRecordFrame(tracker);
    }
    // A vtable that is not one of the 10: Record is told by an id it does not know (the chunk is run again), eax is 0.
    CHECK(Call(harness2, tracker, vf68_thunk, {0x11, 0x22}, 0xBEEF, ec::Address(fake_vtables[10].data())) == 0);
    CHECK(seen.calls == 1 && seen.site == static_cast<uint32_t>(dr::Site::COUNT) && direct_calls == 0);
    CHECK(Call(harness2, tracker, vf68_thunk, {0x11, 0x22}, 0xBEEF, 0) == 0);
  }

  // The animation call: the direct path notes the index (the 9th dword from the return address).
  const uint8_t* const anim_thunk = ec::BuildAnimationCall(&assembler, shared, reinterpret_cast<const uint8_t*>(&FakeAnimation), dr::Site::DEPTH_ANIM);
  shared->last_anim = -1;
  for (const uint32_t tracker : {outside, outsides[0], outsides[1]}) {
    const std::vector<uint32_t> words = {0xAA, 1, 2, 3, 4, 5, 6, 7, 0x1D0, 9};
    CHECK(Call(harness_anim, tracker, anim_thunk, words, 0x57A7, 0x7777) == 0xA111);
    CHECK(seen.calls == 0 && direct_calls == 1 && direct_ecx == 0x57A7 && direct_args[0] == 0xAA && shared->last_anim == 0x1D0);
    CheckDirect();
    shared->last_anim = -1;
  }
  for (const uint32_t tracker : insides) {
    const std::vector<uint32_t> words = {0xAA, 1, 2, 3, 4, 5, 6, 7, 0x1D1, 9};
    Call(harness_anim, tracker, anim_thunk, words, 0x57A7, 0x7777);
    CHECK(seen.calls == 1 && direct_calls == 0 && seen.site == static_cast<uint32_t>(dr::Site::DEPTH_ANIM) && reinterpret_cast<uint32_t>(seen.slot) == tracker);
    CHECK(seen.ecx == 0x57A7 && seen.stack[1] == 0xAA && seen.stack[9] == 0x1D1 && shared->last_anim == -1);
    CheckRecordFrame(tracker);
  }

  // The cut: the loop's four arguments go again to the dispatcher (a fastcall that pops them); active counts the dispatch.
  static uint32_t cut_active_seen = 0;
  static std::array<uint32_t, 4> cut_args = {};
  struct CutHook {
    static uint32_t __fastcall Dispatch(void* node, void*, void* tracker, void* list, uint32_t begin, uint32_t end) {
      cut_active_seen = static_cast<uint32_t>(dr::SharedPage()->active.load());
      cut_args = {reinterpret_cast<uint32_t>(node), reinterpret_cast<uint32_t>(tracker) ^ reinterpret_cast<uint32_t>(list), begin, end};
      return 0xC07;
    }
  };
  const uint32_t saved_dispatch = shared->dispatch_depth;
  shared->dispatch_depth = ec::Address(reinterpret_cast<const void*>(&CutHook::Dispatch));
  const uint8_t* const cut_thunk = ec::BuildCut(&assembler, shared, 4, 0x10, &shared->dispatch_depth);
  CHECK(Call(harness4, 0x5151, cut_thunk, {0x1000, 0x2000, 7, 99}, 0x40DE, 0) == 0xC07);
  CHECK(cut_active_seen == 1 && shared->active.load() == 0 && cut_args == (std::array<uint32_t, 4>{0x40DE, 0x1000 ^ 0x2000, 7, 99}));
  shared->dispatch_depth = saved_dispatch;
  // The same builder for a cut of five arguments, `ret 0x14` and another dispatcher slot (the colour loop's).
  static std::array<uint32_t, 5> colour_args = {};
  struct ColourHook {
    static uint32_t __fastcall Dispatch(void* node, void*, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
      colour_args = {reinterpret_cast<uint32_t>(node), a1 ^ a2, a3, a4, a5};
      return 0xC01;
    }
  };
  const uint32_t saved_colour = shared->dispatch_colour;
  shared->dispatch_colour = ec::Address(reinterpret_cast<const void*>(&ColourHook::Dispatch));
  const HarnessFn harness5 = BuildHarness(&assembler, 5, 5);
  const uint8_t* const colour_cut = ec::BuildCut(&assembler, shared, 5, 0x14, &shared->dispatch_colour);
  REQUIRE(assembler.Fits());
  CHECK(Call(harness5, 0x5151, colour_cut, {0x1000, 0x2000, 7, 99, 5}, 0x40DE, 0) == 0xC01);
  CHECK(shared->active.load() == 0 && colour_args == (std::array<uint32_t, 5>{0x40DE, 0x1000 ^ 0x2000, 7, 99, 5}));
  shared->dispatch_colour = saved_colour;

  // The depth init: jumped to from the loop with esi the tracker and the local at [esp + 0x18]; goes on at `resume`.
  uint32_t init_index = 0;
  uint32_t init_eax = 0;
  uint8_t* const init_resume = assembler.Cursor();
  assembler.Bytes({0x8B, 0x4C, 0x24, 0x18});
  assembler.BytesAndOperand({0x89, 0x0D}, ec::Address(&init_index));
  assembler.BytesAndOperand({0xA3}, ec::Address(&init_eax));
  assembler.Bytes({0x83, 0xC4, 0x20, 0x5F, 0x5B, 0x5E, 0x5D, 0xC3});
  const uint8_t* const init_thunk = ec::BuildAnimationInit(&assembler, shared, init_resume, 0x18, false);
  uint8_t* const init_harness = assembler.Cursor();
  assembler.Bytes({0x55, 0x8B, 0xEC, 0x56, 0x53, 0x57, 0x8B, 0x75, 0x08, 0x83, 0xEC, 0x20, 0xC7, 0x44, 0x24, 0x18, 0x55, 0x55, 0x55, 0x55});
  assembler.BytesAndOperand({0xB8}, 0xE4E4);
  assembler.Relative(0xE9, init_thunk);
  REQUIRE(assembler.Fits());
  const auto init = reinterpret_cast<void(__cdecl*)(uint32_t)>(init_harness);
  shared->seed_anim = 0x5EED;
  for (const uint32_t tracker : {outside, outsides[0], outsides[1]}) {
    init_index = 0;
    init_eax = 0;
    init(tracker);
    CHECK(init_index == 0x5EED && init_eax == 0xE4E4);
  }
  for (const uint32_t tracker : insides) {
    reinterpret_cast<dr::Slot*>(tracker & ~(dr::SLOT_SIZE - 1))->anim = 0x51A1;
    init_index = 0;
    init_eax = 0;
    init(tracker & ~(dr::SLOT_SIZE - 1));
    CHECK(init_index == 0x51A1 && init_eax == 0xE4E4);
  }
  shared->seed_anim = -1;
  shared->record = saved_record;
  VirtualFree(test_code, 0, MEM_RELEASE);
  std::printf("6. thunks: flush entry, flush call site, shared-state call (10 vtables + unknown), animation call, cut, depth init: direct and record\n");
}

// ---------------------------------------------------------------- 6b. the colour loop's thunks on the game's own covered bytes

// A stand-in for the game's code a thunk goes on at: keeps the registers and the flags it was reached with (the flags first),
// counts the arrivals and returns.
struct Reached {
  uint32_t count = 0;
  uint32_t eax = 0;
  uint32_t ecx = 0;
  uint32_t edx = 0;
  uint32_t ebx = 0;
  uint32_t flags = 0;
  uint32_t xmm0 = 0;
};
Reached resume_reached;
Reached landing_reached;

uint8_t* BuildStub(ec::Assembler* assembler, Reached* reached) {
  uint8_t* const entry = assembler->Cursor();
  assembler->BytesAndOperand({0xA3}, ec::Address(&reached->eax));
  assembler->Bytes({0x9C, 0x58});  // pushfd; pop eax
  assembler->BytesAndOperand({0xA3}, ec::Address(&reached->flags));
  assembler->BytesAndOperand({0x89, 0x0D}, ec::Address(&reached->ecx));
  assembler->BytesAndOperand({0x89, 0x15}, ec::Address(&reached->edx));
  assembler->BytesAndOperand({0x89, 0x1D}, ec::Address(&reached->ebx));
  assembler->BytesAndOperand({0xF3, 0x0F, 0x11, 0x05}, ec::Address(&reached->xmm0));
  assembler->BytesAndOperand({0xFF, 0x05}, ec::Address(&reached->count));
  assembler->Bytes({0xC3});
  return entry;
}

void TestColourThunks() {
  REQUIRE(Prepare());
  dr::Shared* const shared = dr::SharedPage();
  shared->arena_size = ec::recording_arena_size_value;
  const uint32_t saved_record = shared->record;
  shared->record = ec::Address(reinterpret_cast<const void*>(&FakeRecord));
  const uint32_t arena = shared->arena;
  const uint32_t arena_size = shared->arena_size;
  test_code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  ec::Assembler assembler(std::span<uint8_t>(test_code, 0x4000));
  const HarnessFn harness14 = BuildHarness(&assembler, 14, 0);
  const uint8_t* landing = nullptr;
  const HarnessFn harness_landing = BuildHarness(&assembler, 0, 0, &landing);
  const HarnessFn harness_anim = BuildHarness(&assembler, 12, 1);
  const HarnessFn harness2 = BuildHarness(&assembler, 2, 2);
  const uint8_t* const resume_stub = BuildStub(&assembler, &resume_reached);
  const uint8_t* const landing_stub = BuildStub(&assembler, &landing_reached);
  // Trackers the thunks may read and write (the page past the arena's last slot is committed): outside the arena, its first byte
  // past, and slots.
  std::array<uint8_t, 0x200> local_tracker = {};
  const uint32_t outside = reinterpret_cast<uint32_t>(local_tracker.data());
  const std::array<uint32_t, 2> outsides = {outside, arena + arena_size};
  const std::array<uint32_t, 3> insides = {arena, arena + 5 * dr::SLOT_SIZE + 0x40, arena + arena_size - dr::SLOT_SIZE};
  const auto reset = [] {
    resume_reached = {};
    landing_reached = {};
  };
  const auto write32 = [](uint32_t at, uint32_t value) { std::memcpy(reinterpret_cast<void*>(at), &value, sizeof(value)); };
  const auto read32 = [](uint32_t at) {
    uint32_t value;
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof(value));
    return value;
  };
  const auto args_address = [](const std::vector<uint32_t>& args) { return reinterpret_cast<uint32_t>(args.data()); };
  const auto info_of = [](ec::RecordingSite site) -> const ec::RecordingSiteInfo& { return ec::RECORDING_INFO[static_cast<size_t>(site)]; };

  // The mesh setup: the game's two instructions on the direct path; on a slot the record path asks Record, which answers 0 (go
  // on as the game does) or 1 (a class it does not know: the loop skips the mesh).
  const ec::RecordingSiteInfo& setup = info_of(ec::RecordingSite::COLOUR_SETUP);
  const uint8_t* const setup_thunk = ec::BuildMeshSetup(&assembler, shared, Rva(setup.rva), setup.covered, resume_stub, landing_stub);
  REQUIRE(assembler.Fits());
  std::vector<uint32_t> args(14, 0);
  for (const uint32_t tracker : {outsides[0], outsides[1], insides[0], insides[1], insides[2]}) {
    write32(tracker + 0x20, 0x20202020);
    write32(tracker + 0x74, 0x7474);
  }
  for (const uint32_t tracker : outsides) {
    reset();
    Call(harness14, tracker, setup_thunk, args, 0xEC0, 0);
    CHECK(seen.calls == 0 && resume_reached.count == 1 && landing_reached.count == 0);
    CHECK(resume_reached.eax == 0x20202020 && resume_reached.ecx == args_address(args) && resume_reached.edx == EDX_IN);
    CHECK((resume_reached.flags & DIRECTION_FLAG) != 0 && read32(tracker + 0x74) == 0x7474);
  }
  for (const uint32_t tracker : insides) {
    for (const uint32_t answer : {0u, 1u}) {
      reset();
      record_sets_eax = answer;
      write32(tracker + 0x74, 0x7474);
      Call(harness14, tracker, setup_thunk, args, 0xEC0, 0);
      CHECK(seen.calls == 1 && seen.site == static_cast<uint32_t>(dr::Site::COLOUR_SETUP) && reinterpret_cast<uint32_t>(seen.slot) == tracker);
      CheckRecordFrame(tracker);
      CHECK(seen.ebx == args_address(args) && seen.ecx == 0xEC0);
      if (answer == 0) {
        CHECK(resume_reached.count == 1 && landing_reached.count == 0 && resume_reached.eax == 0x20202020);
        CHECK(resume_reached.ecx == args_address(args) && resume_reached.edx == EDX_IN && read32(tracker + 0x74) == 0x7474);
      } else {
        // The loop's skip: no technique for this mesh (+0x74 = 0), then on at the setup's landing.
        CHECK(resume_reached.count == 0 && landing_reached.count == 1 && landing_reached.edx == EDX_IN && read32(tracker + 0x74) == 0);
      }
    }
  }
  record_sets_eax = 0x600D;

  // A constant's guard (`cmp byte ptr [the cache flag], 0`): the direct path is the game's compare, flags and xmm0 as it left them;
  // the record path hands Record ecx (the first's block) and xmm0 (the second's value) and lands after the write.
  const ec::RecordingSiteInfo& const1 = info_of(ec::RecordingSite::COLOUR_CONST1);
  const ec::RecordingSiteInfo& const2 = info_of(ec::RecordingSite::COLOUR_CONST2);
  const auto flag = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(read32(reinterpret_cast<uint32_t>(Rva(const1.rva)) + 2)));
  const uint8_t* const const1_thunk =
      ec::BuildConstant(&assembler, shared, dr::Site::COLOUR_CONST1, Rva(const1.rva), const1.covered, resume_stub, landing_stub, {});
  const uint8_t* const const2_thunk = ec::BuildConstant(&assembler, shared, dr::Site::COLOUR_CONST2, Rva(const2.rva), const2.covered,
                                                         resume_stub, landing_stub, {0x8B, 0x5C, 0x24, 0x14});
  REQUIRE(assembler.Fits());
  harness_xmm0 = 0x40490FDB;
  args.assign(14, 0);
  args[4] = 0xB0B0;
  for (const uint8_t flag_value : {uint8_t{0}, uint8_t{1}}) {
    *flag = flag_value;
    for (const uint32_t tracker : outsides) {
      for (const uint8_t* const thunk : {const1_thunk, const2_thunk}) {
        reset();
        Call(harness14, tracker, thunk, args, 0xEC0, 0);
        CHECK(seen.calls == 0 && resume_reached.count == 1 && landing_reached.count == 0);
        CHECK(((resume_reached.flags & 0x40) != 0) == (flag_value == 0) && resume_reached.edx == EDX_IN && (resume_reached.flags & DIRECTION_FLAG) != 0);
        CHECK(resume_reached.xmm0 == 0x40490FDB && resume_reached.ecx == 0xEC0 && resume_reached.ebx == args_address(args));
      }
    }
  }
  for (const uint32_t tracker : insides) {
    reset();
    Call(harness14, tracker, const1_thunk, args, 0xEC0, 0);
    CHECK(seen.calls == 1 && seen.site == static_cast<uint32_t>(dr::Site::COLOUR_CONST1) && reinterpret_cast<uint32_t>(seen.slot) == tracker);
    CHECK(seen.ecx == 0xEC0 && resume_reached.count == 0 && landing_reached.count == 1 && landing_reached.edx == EDX_IN);
    CheckRecordFrame(tracker);
    reset();
    // The second also runs the instruction the guard's path would have run (the state, `mov ebx, [esp + 0x14]`), and Record sees xmm0.
    Call(harness14, tracker, const2_thunk, args, 0xEC0, 0, 0xB0B0);
    CHECK(seen.calls == 1 && seen.site == static_cast<uint32_t>(dr::Site::COLOUR_CONST2) && seen.xmm0 == 0x40490FDB && seen.stack[5] == 0xB0B0);
    CHECK(resume_reached.count == 0 && landing_reached.count == 1 && landing_reached.ebx == 0xB0B0 && landing_reached.edx == EDX_IN);
  }
  harness_xmm0 = 0;
  *flag = 0;

  // The faction block: the game's two instructions (the faction into the tracker) run on both paths.
  const ec::RecordingSiteInfo& faction = info_of(ec::RecordingSite::COLOUR_FACTION);
  const uint8_t* const faction_thunk = ec::BuildFaction(&assembler, shared, Rva(faction.rva), faction.covered, resume_stub, landing_stub);
  REQUIRE(assembler.Fits());
  args.assign(14, 0);
  args[11] = 0xFAC7;
  for (const uint32_t tracker : outsides) {
    reset();
    write32(tracker, 0);
    Call(harness14, tracker, faction_thunk, args, 0xEC0, 0);
    CHECK(seen.calls == 0 && resume_reached.count == 1 && landing_reached.count == 0 && resume_reached.ecx == 0xFAC7 && resume_reached.edx == EDX_IN);
    CHECK(read32(tracker) == 0xFAC7);
  }
  for (const uint32_t tracker : insides) {
    reset();
    write32(tracker, 0);
    Call(harness14, tracker, faction_thunk, args, 0xEC0, 0);
    CHECK(seen.calls == 1 && seen.site == static_cast<uint32_t>(dr::Site::COLOUR_FACTION) && reinterpret_cast<uint32_t>(seen.slot) == tracker);
    CHECK(seen.ecx == 0xFAC7 && read32(tracker) == 0xFAC7 && resume_reached.count == 0 && landing_reached.count == 1 && landing_reached.edx == EDX_IN);
    CheckRecordFrame(tracker);
  }

  // The texture lookups: a tail jump on the direct path; the record path drops the return address and lands after the bind.
  uint8_t* const lookup = assembler.Cursor();
  assembler.Bytes({0xB8, 0x77, 0x7E, 0x00, 0x00, 0xC3});  // mov eax, 0x7E77; ret
  const std::array<std::pair<const uint8_t*, dr::Site>, 2> textures = {{
      {ec::BuildTextureCall(&assembler, shared, lookup, dr::Site::COLOUR_SRV_DIFFUSE, landing), dr::Site::COLOUR_SRV_DIFFUSE},
      {ec::BuildTextureCall(&assembler, shared, lookup, dr::Site::COLOUR_SRV_NORMAL, landing), dr::Site::COLOUR_SRV_NORMAL},
  }};
  REQUIRE(assembler.Fits());
  args.assign(2, 0);
  for (const auto& [thunk, site] : textures) {
    for (const uint32_t tracker : outsides) {
      CHECK(Call(harness_landing, tracker, thunk, args, 0x7E87, 0) == 0x7E77);
      CHECK(seen.calls == 0 && harness_path == 1);
    }
    for (const uint32_t tracker : insides) {
      Call(harness_landing, tracker, thunk, args, 0x7E87, 0);
      CHECK(seen.calls == 1 && seen.site == static_cast<uint32_t>(site) && reinterpret_cast<uint32_t>(seen.slot) == tracker);
      CHECK(seen.ecx == 0x7E87 && harness_path == 2);
      CheckRecordFrame(tracker);
    }
  }

  // The colour loop's animation call (the index is the 11th dword from the return address), its two shared-state calls, its
  // init (the local at [esp + 0x24], eax -1 after), as the depth loop's.
  const uint8_t* const anim_thunk =
      ec::BuildAnimationCall(&assembler, shared, reinterpret_cast<const uint8_t*>(&FakeAnimation), dr::Site::COLOUR_ANIM);
  shared->last_anim = -1;
  for (const uint32_t tracker : outsides) {
    const std::vector<uint32_t> words = {0xAA, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x1D0, 11};
    CHECK(Call(harness_anim, tracker, anim_thunk, words, 0x57A7, 0x7777) == 0xA111);
    CHECK(seen.calls == 0 && direct_calls == 1 && direct_args[0] == 0xAA && shared->last_anim == 0x1D0);
    shared->last_anim = -1;
  }
  for (const uint32_t tracker : insides) {
    const std::vector<uint32_t> words = {0xAA, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x1D1, 11};
    Call(harness_anim, tracker, anim_thunk, words, 0x57A7, 0x7777);
    CHECK(seen.calls == 1 && seen.site == static_cast<uint32_t>(dr::Site::COLOUR_ANIM) && seen.stack[11] == 0x1D1 && shared->last_anim == -1);
    CheckRecordFrame(tracker);
  }
  std::array<std::array<uint32_t, 0x20>, 10> fake_vtables = {};
  std::array<uint32_t, 10> known = {};
  for (size_t index = 0; index < known.size(); ++index) {
    fake_vtables[index][0x68 / 4] = ec::Address(reinterpret_cast<const void*>(&FakeVf68));
    known[index] = ec::Address(fake_vtables[index].data());
  }
  for (const dr::Site site : {dr::Site::COLOUR_VF68_SETUP, dr::Site::COLOUR_VF68_VARIATION}) {
    const uint8_t* const vf68_thunk = ec::BuildSharedStateCall(&assembler, shared, &known, 0x68, 8, site);
    REQUIRE(assembler.Fits());
    for (const uint32_t tracker : outsides) {
      CHECK(Call(harness2, tracker, vf68_thunk, {0x11, 0x22}, 0xBEEF, known[3]) == 0x6868);
      CHECK(seen.calls == 0 && direct_calls == 1);
    }
    for (const uint32_t tracker : insides) {
      CHECK(Call(harness2, tracker, vf68_thunk, {0x11, 0x22}, 0xBEEF, known[3]) == 0x600D);
      CHECK(seen.calls == 1 && seen.site == static_cast<uint32_t>(site) && seen.ecx == 0xBEEF && seen.stack[1] == 0x11);
    }
  }
  uint32_t init_index = 0;
  uint32_t init_eax = 0;
  uint8_t* const init_resume = assembler.Cursor();
  assembler.Bytes({0x8B, 0x4C, 0x24, 0x24});
  assembler.BytesAndOperand({0x89, 0x0D}, ec::Address(&init_index));
  assembler.BytesAndOperand({0xA3}, ec::Address(&init_eax));
  assembler.Bytes({0x83, 0xC4, 0x30, 0x5F, 0x5B, 0x5E, 0x5D, 0xC3});
  const uint8_t* const init_thunk = ec::BuildAnimationInit(&assembler, shared, init_resume, 0x24, true);
  uint8_t* const init_harness = assembler.Cursor();
  assembler.Bytes({0x55, 0x8B, 0xEC, 0x56, 0x53, 0x57, 0x8B, 0x75, 0x08, 0x83, 0xEC, 0x30, 0xC7, 0x44, 0x24, 0x24, 0x55, 0x55, 0x55, 0x55});
  assembler.BytesAndOperand({0xB8}, 0xE4E4);
  assembler.Relative(0xE9, init_thunk);
  REQUIRE(assembler.Fits());
  const auto init = reinterpret_cast<void(__cdecl*)(uint32_t)>(init_harness);
  shared->seed_anim = 0x5EED;
  for (const uint32_t tracker : {outsides[0], outsides[1], arena - 1}) {
    init_index = 0;
    init_eax = 0;
    init(tracker);
    CHECK(init_index == 0x5EED && init_eax == 0xFFFFFFFF);
  }
  for (const uint32_t tracker : insides) {
    reinterpret_cast<dr::Slot*>(tracker & ~(dr::SLOT_SIZE - 1))->anim = 0x51A1;
    init_index = 0;
    init_eax = 0;
    init(tracker & ~(dr::SLOT_SIZE - 1));
    CHECK(init_index == 0x51A1 && init_eax == 0xFFFFFFFF);
  }
  shared->seed_anim = -1;
  shared->record = saved_record;
  VirtualFree(test_code, 0, MEM_RELEASE);
  std::printf("6b. colour thunks on the game's covered bytes: setup (go on / skip), both constants (flags, xmm0), faction, both textures, animation, shared-state calls, init\n");
}

// ---------------------------------------------------------------- 5g. the B groups: class table and Depth B

// The family and class tables are decoded from the game's bytes: every byte they read, flipped one at a time, takes out exactly the
// classes it belongs to (a vtable slot: that class; a thunk's bytes: the classes that go through it; a dispatcher's: its family's
// classes; the list helper's head: all of them, and with the last depth class the B sites); the A groups never notice.
void TestClassTable() {
  REQUIRE(Prepare());
  const dr::GameCalls reference = dr::internal::game_calls;
  REQUIRE(reference.class_count == ec::ROUTED_CLASSES.size());
  const size_t class_count = ec::ROUTED_CLASSES.size();
  uint32_t flips = 0;
  // `out`: the classes (by index in the table) that must be left out. `base_xor`: what the list base of the others must change by.
  const auto flip = [&](uint32_t at, const std::vector<size_t>& out, uint32_t base_xor = 0, uint32_t base_thunk = 0) {
    *Rva(at) ^= 0xFF;
    REQUIRE(Prepare());
    const dr::GameCalls& calls = dr::internal::game_calls;
    std::vector<size_t> kept;
    for (size_t index = 0; index < class_count; ++index) {
      if (std::find(out.begin(), out.end(), index) == out.end()) kept.push_back(index);
    }
    const bool depth_left = std::any_of(kept.begin(), kept.end(), [](size_t index) { return ec::ROUTED_CLASSES[index].depth; });
    CHECK(calls.class_count == (depth_left ? kept.size() : 0));
    for (size_t position = 0; depth_left && position < kept.size() && position < calls.class_count; ++position) {
      const dr::MeshClass& decoded = calls.classes[position];
      const dr::MeshClass& expected = reference.classes[kept[position]];
      CHECK(decoded.vtable == expected.vtable && decoded.family == expected.family && decoded.depth_body == expected.depth_body);
      CHECK(decoded.base == (ec::ROUTED_CLASSES[kept[position]].thunk == base_thunk ? expected.base ^ base_xor : expected.base));
    }
    // The B sites go in with the accessor and a class that records in depth; without one they stay out. The A groups' never move.
    CHECK(((ec::recording_found & DEPTH_B_OWN) == DEPTH_B_OWN) == depth_left && (depth_left || (ec::recording_found & DEPTH_B_OWN) == 0));
    CHECK(((ec::recording_found & COLOUR_B_OWN) == COLOUR_B_OWN) == depth_left && (depth_left || (ec::recording_found & COLOUR_B_ONLY) == 0));
    CHECK((calls.accessor != 0) == depth_left && (((calls.usable_groups >> static_cast<size_t>(dr::Group::DEPTH_B)) & 1) != 0) == depth_left);
    CHECK((((calls.usable_groups >> static_cast<size_t>(dr::Group::COLOUR_B)) & 1) != 0) == depth_left);
    CHECK((ec::recording_found & (DEPTH_A_SITES | COLOUR_A_SITES)) == (DEPTH_A_SITES | COLOUR_A_SITES) && (calls.usable_groups & 3) == 3);
    CHECK(out.empty() || LogHas("class 0x") || !depth_left);
    CHECK(!out.empty() || base_xor != 0 || !LogHas("left alone"));
    *Rva(at) ^= 0xFF;
    ++flips;
  };
  // A class's vtable slots (the technique and shared-state slots, the common ones, and the slot the thunk is in).
  constexpr std::array<uint32_t, 7> SLOTS = {0x54, 0x58, 0x60, 0x64, 0x68, 0x6c, ec::MESH_VF138};
  for (size_t index = 0; index < class_count; ++index) {
    for (const uint32_t slot : SLOTS) {
      for (uint32_t byte = 0; byte < 4; ++byte) flip(ec::ROUTED_CLASSES[index].vtable + slot + byte, {index});
    }
  }
  // The thunks: their fixed bytes, the dispatcher call's displacement and the list base's bytes (the lowest byte changes the base
  // the class is decoded with, a higher one makes it absurd and the class is left out).
  std::set<uint32_t> thunks;
  for (const ec::RoutedClass& routed : ec::ROUTED_CLASSES) thunks.insert(routed.thunk);
  CHECK(thunks.size() == 9);
  for (const uint32_t thunk : thunks) {
    const bool special = (thunk == 0x1827980);
    std::vector<size_t> out;
    for (size_t index = 0; index < class_count; ++index) {
      if (ec::ROUTED_CLASSES[index].thunk == thunk) out.push_back(index);
    }
    const uint32_t call = (special ? ec::SLOT138_SPECIAL_CALL : ec::SLOT138_THUNK_CALL);
    const uint32_t base_at = thunk + (special ? ec::SLOT138_SPECIAL_BASE : ec::SLOT138_THUNK_BASE);
    for (const size_t fixed : FixedBytes(special ? ec::SLOT138_SPECIAL_THUNK : ec::SLOT138_THUNK)) flip(thunk + static_cast<uint32_t>(fixed), out);
    for (uint32_t byte = 1; byte < 5; ++byte) flip(thunk + call + byte, out);
    for (uint32_t byte = 1; byte < 4; ++byte) flip(base_at + byte, out);
    flip(base_at, {}, 0xFF, thunk);
  }
  // The dispatchers: bytes, the three calls the tables read (the list helper, the depth body, the colour body); the visibility
  // call's displacement is read by nothing, and family 7 has no class.
  for (uint32_t family = 0; family < ec::DISPATCHER_COUNT; ++family) {
    const uint32_t dispatcher = ec::DISPATCHER_FIRST_RVA + family * ec::DISPATCHER_STRIDE;
    std::vector<size_t> out;
    for (size_t index = 0; index < class_count; ++index) {
      if (reference.classes[index].family == family) out.push_back(index);
    }
    CHECK(family == 7 ? out.empty() : !out.empty());
    for (const size_t fixed : FixedBytes(ec::DISPATCHER_BYTES)) flip(dispatcher + static_cast<uint32_t>(fixed), out);
    for (const uint32_t call : {ec::DISPATCHER_LIST_CALL, ec::DISPATCHER_DEPTH_CALL, ec::DISPATCHER_COLOUR_CALL}) {
      for (uint32_t byte = 1; byte < 5; ++byte) flip(dispatcher + call + byte, out);
    }
    for (uint32_t byte = 1; byte < 5; ++byte) flip(dispatcher + 0x52 + byte, {});
  }
  // The list helper: every dispatcher's first call, so every class goes.
  std::vector<size_t> all(class_count);
  for (size_t index = 0; index < class_count; ++index) all[index] = index;
  for (const size_t fixed : FixedBytes(ec::LIST_HELPER_HEAD)) flip(ec::LIST_HELPER_RVA + static_cast<uint32_t>(fixed), all);
  CHECK(DifferingBytes() == 0);
  REQUIRE(Prepare());
  CHECK(dr::internal::game_calls.class_count == class_count && ec::recording_found == ALL_SITES);
  std::printf("5g. class table: %u single-byte flips of the 23 classes' slots, the 9 thunks, the 8 dispatchers and the list helper: exactly the classes that read the byte are left out (named in the log), the B sites need the accessor and a depth class, the A groups never move\n", flips);
}

// Depth B goes in and out through the same window as the A groups: its cut and 15 calls with the entry hook, beside either A group;
// a mismatch stands down only its own group; a thread in the B cut stub blocks the last removal.
void TestBGroup() {
  const uint32_t depth_row = 1u << static_cast<uint32_t>(dr::Group::DEPTH_A);
  const uint32_t colour_row = 1u << static_cast<uint32_t>(dr::Group::COLOUR_A);
  const uint32_t b_row = 1u << static_cast<uint32_t>(dr::Group::DEPTH_B);
  const uint32_t colour_b_row = 1u << static_cast<uint32_t>(dr::Group::COLOUR_B);
  REQUIRE(Prepare());
  dr::Shared* const shared = dr::SharedPage();
  const uint32_t arena_size = ec::recording_arena_size_value;
  UpdateWith(b_row);
  CHECK(ec::recording_written == DEPTH_B_SITES && std::popcount(ec::recording_written) == 17 && shared->arena_size == arena_size);
  CHECK(AllHold(DEPTH_B_SITES) && ecp::DrawRecordingSitesIn());
  CHECK(LogHas("sites in"));
  UpdateWith(b_row | depth_row | colour_row);
  CHECK(ec::recording_written == (DEPTH_B_SITES | DEPTH_A_SITES | COLOUR_A_SITES) && AllHold(DEPTH_B_SITES | DEPTH_A_SITES | COLOUR_A_SITES));
  // One row off: its own sites come out, the entry hook and the arena size stay for the others.
  UpdateWith(depth_row | colour_row);
  CHECK(ec::recording_written == (DEPTH_A_SITES | COLOUR_A_SITES) && AllHold(DEPTH_A_SITES | COLOUR_A_SITES) && shared->arena_size == arena_size);
  UpdateWith(b_row | depth_row);
  CHECK(ec::recording_written == (DEPTH_B_SITES | DEPTH_A_SITES) && AllHold(DEPTH_B_SITES | DEPTH_A_SITES));
  UpdateWith(b_row);
  CHECK(ec::recording_written == DEPTH_B_SITES && AllHold(DEPTH_B_SITES) && shared->arena_size == arena_size);
  // Colour B: its 20 sites and the cut with the hook. Beside Depth B it adds its own; either row alone keeps the cut for itself.
  UpdateWith(b_row | colour_b_row);
  CHECK(ec::recording_written == (DEPTH_B_SITES | COLOUR_B_SITES) && std::popcount(ec::recording_written) == 37 && AllHold(DEPTH_B_SITES | COLOUR_B_SITES));
  UpdateWith(colour_b_row);
  CHECK(ec::recording_written == COLOUR_B_SITES && std::popcount(ec::recording_written) == 22 && AllHold(COLOUR_B_SITES));
  UpdateWith(b_row);
  CHECK(ec::recording_written == DEPTH_B_SITES && AllHold(DEPTH_B_SITES));
  UpdateWith(colour_b_row | colour_row | depth_row);
  CHECK(ec::recording_written == (COLOUR_B_SITES | COLOUR_A_SITES | DEPTH_A_SITES) && AllHold(COLOUR_B_SITES | COLOUR_A_SITES | DEPTH_A_SITES));
  UpdateWith(b_row | colour_b_row | colour_row | depth_row);
  constexpr uint64_t EVERY_ROW = DEPTH_A_SITES | COLOUR_A_SITES | DEPTH_B_SITES | COLOUR_B_SITES;
  CHECK(ec::recording_written == EVERY_ROW && AllHold(EVERY_ROW) && std::popcount(EVERY_ROW) == 59 - 7);
  // The groups wholly in (what addon.cpp hands the dispatcher): the four loop groups here.
  constexpr uint32_t DEPTH_A_BIT = 1u << static_cast<uint32_t>(dr::Group::DEPTH_A);
  constexpr uint32_t COLOUR_A_BIT = 1u << static_cast<uint32_t>(dr::Group::COLOUR_A);
  constexpr uint32_t DEPTH_B_BIT = 1u << static_cast<uint32_t>(dr::Group::DEPTH_B);
  constexpr uint32_t COLOUR_B_BIT = 1u << static_cast<uint32_t>(dr::Group::COLOUR_B);
  CHECK(ecp::DrawRecordingGroupsIn() == (DEPTH_A_BIT | COLOUR_A_BIT | DEPTH_B_BIT | COLOUR_B_BIT));
  UpdateWith(colour_b_row);
  CHECK(ec::recording_written == COLOUR_B_SITES && shared->arena_size == arena_size);
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && shared->arena_size == 0 && DifferingBytes() == 0);
  // Another mod's bytes on a Colour B site (a call, a state block): Colour B's own sites stay out, the cut stays for Depth B, and
  // with Colour B alone the cut stays out too. The log names the site.
  for (const ec::RecordingSite modded_site : {ec::RecordingSite::COLOUR_B1_VARIATION, ec::RecordingSite::COLOUR_B2_STATE, ec::RecordingSite::COLOUR_B4_VF64,
                                              ec::RecordingSite::COLOUR_B0_STATE}) {
    REQUIRE(Prepare());
    uint8_t* const modded = SiteAt(modded_site);
    modded[0] ^= 0x55;
    log_lines.clear();
    UpdateWith(b_row | colour_b_row | depth_row);
    CHECK(ec::recording_written == (DEPTH_B_SITES | DEPTH_A_SITES) && AllHold(DEPTH_B_SITES | DEPTH_A_SITES, ec::RecordingBit(modded_site)));
    // Colour B's row is still on, its 20 sites are out and the cut is in: only Depth B (and A) are groups wholly in.
    CHECK(ecp::DrawRecordingGroupsIn() == (DEPTH_B_BIT | DEPTH_A_BIT));
    CHECK(LogHas(ec::RECORDING_INFO[static_cast<size_t>(modded_site)].name.data()));
    UpdateWith(0);
    CHECK(ec::recording_written == 0 && ecp::DrawRecordingGroupsIn() == 0);
    UpdateWith(colour_b_row);
    CHECK(ec::recording_written == 0 && AllHold(0, ec::RecordingBit(modded_site)) && ecp::DrawRecordingGroupsIn() == 0);
    modded[0] ^= 0x55;
    UpdateWith(0);
    CHECK(ec::recording_written == 0 && DifferingBytes() == 0);
  }
  // The cut changed by another mod stands both B groups down.
  {
    REQUIRE(Prepare());
    uint8_t* const modded = SiteAt(ec::RecordingSite::DEPTH_B_CUT);
    modded[0] ^= 0x55;
    UpdateWith(b_row | colour_b_row | depth_row);
    CHECK(ec::recording_written == DEPTH_A_SITES && AllHold(DEPTH_A_SITES, ec::RecordingBit(ec::RecordingSite::DEPTH_B_CUT)));
    modded[0] ^= 0x55;
    UpdateWith(0);
    CHECK(ec::recording_written == 0 && DifferingBytes() == 0);
  }
  // Another mod's bytes on one of the 15 calls, or on the cut: Depth B stays out whole, the A groups go in, the log names the site.
  for (const ec::RecordingSite modded_site : {ec::RecordingSite::DEPTH_B2_VARIATION, ec::RecordingSite::DEPTH_B4_VF64, ec::RecordingSite::DEPTH_B_CUT}) {
    REQUIRE(Prepare());
    uint8_t* const modded = SiteAt(modded_site);
    modded[0] ^= 0x55;
    log_lines.clear();
    UpdateWith(b_row | depth_row);
    CHECK(ec::recording_written == DEPTH_A_SITES && (ec::recording_found & DEPTH_B_OWN) == 0 && AllHold(DEPTH_A_SITES, ec::RecordingBit(modded_site)));
    CHECK(LogHas(ec::RECORDING_INFO[static_cast<size_t>(modded_site)].name.data()) && !LogHas("depth loop"));
    modded[0] ^= 0x55;
    UpdateWith(0);
    CHECK(ec::recording_written == 0 && DifferingBytes() == 0);
  }
  // And the other way: a depth A site changed by another mod, Depth B goes in alone with the entry hook.
  REQUIRE(Prepare());
  uint8_t* const depth_modded = SiteAt(ec::RecordingSite::DEPTH_ANIM);
  depth_modded[0] ^= 0x55;
  UpdateWith(b_row | depth_row);
  CHECK(ec::recording_written == DEPTH_B_SITES && AllHold(DEPTH_B_SITES, ec::RecordingBit(ec::RecordingSite::DEPTH_ANIM)));
  depth_modded[0] ^= 0x55;
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && DifferingBytes() == 0);
  // Coming out with one B site changed since: the others go back, the arena size is 0, the feature stays reachable.
  REQUIRE(Prepare());
  UpdateWith(b_row);
  CHECK(ec::recording_written == DEPTH_B_SITES);
  uint8_t* const vf64 = SiteAt(ec::RecordingSite::DEPTH_B3_VF64);
  vf64[1] ^= 0x55;
  log_lines.clear();
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && shared->arena_size == 0 && ecp::DrawRecordingSitesIn() && LogHas("depth body 3 slot-0x64 call"));
  CHECK(DifferingOutside(vf64) == 0);
  std::memcpy(vf64, &pristine[0x17b1503], ec::SITE_SIZE);
  // A thread in the B cut stub (it is about to dispatch): the last removal waits, then goes.
  REQUIRE(Prepare());
  UpdateWith(b_row);
  CHECK(ec::recording_written == DEPTH_B_SITES);
  uint8_t* const b_cut_stub = ThunkOf(ec::RecordingSite::DEPTH_B_CUT);
  REQUIRE(b_cut_stub >= ec::recording_page && b_cut_stub + 4 <= ec::recording_cut.end);
  Parked parked = Park(b_cut_stub);
  ec::failed_writes = 0;
  UpdateWith(0);
  CHECK(ec::recording_written == DEPTH_B_SITES && ec::failed_writes == 1 && shared->arena_size == arena_size && AllHold(DEPTH_B_SITES));
  Release(&parked);
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && DifferingBytes() == 0 && shared->arena_size == 0);
  // A dispatch in flight: the same.
  UpdateWith(b_row);
  shared->active.store(1);
  UpdateWith(0);
  CHECK(ec::recording_written == DEPTH_B_SITES);
  shared->active.store(0);
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && DifferingBytes() == 0);
  std::printf("5h. B groups: Depth B's cut and 15 calls, Colour B's 20 sites and the same cut, with the hook in and out beside both A groups and each other; a mismatch stands down its own group (a call, a state block) and the cut only with no group left that needs it, the cut itself both; a changed site coming out, a thread in the B cut stub, a dispatch in flight\n");
}

// ---------------------------------------------------------------- 6c. the B thunks

uint32_t __fastcall FakeVf64(void* ecx, void* edx, uint32_t a1, uint32_t a2, uint32_t a3) {
  ++direct_calls;
  direct_ecx = reinterpret_cast<uint32_t>(ecx);
  direct_edx = reinterpret_cast<uint32_t>(edx);
  direct_flags = __readeflags();
  direct_args = {a1, a2, a3};
  return 0x6464;
}

// What the per-mesh cut hands DispatchMesh, and where the game's code goes on afterwards.
struct CutSeen {
  uint32_t calls = 0;
  uint32_t active = 0;
  std::array<uint32_t, 8> args = {};
};
CutSeen cut_seen;
struct CutOut {
  uint32_t esi = 0;
  uint32_t edi = 0;
  uint32_t ebx = 0;
  uint32_t ebp = 0;
  uint32_t esp = 0;
  uint32_t path = 0;
};
CutOut cut_out;
uint32_t cut_saved_esp = 0;
uint32_t cut_tracker = 0;
void __stdcall FakeDispatchMesh(void* tracker, void* device, void* view, uint32_t list_type, void* node, void* sorted, uint32_t first, uint32_t last) {
  ++cut_seen.calls;
  cut_seen.active = static_cast<uint32_t>(dr::SharedPage()->active.load());
  cut_seen.args = {reinterpret_cast<uint32_t>(tracker), reinterpret_cast<uint32_t>(device), reinterpret_cast<uint32_t>(view), list_type,
                   reinterpret_cast<uint32_t>(node), reinterpret_cast<uint32_t>(sorted), first, last};
}

void TestBThunks() {
  REQUIRE(Prepare());
  dr::Shared* const shared = dr::SharedPage();
  shared->arena_size = ec::recording_arena_size_value;
  const uint32_t saved_record = shared->record;
  shared->record = ec::Address(reinterpret_cast<const void*>(&FakeRecord));
  const uint32_t saved_dispatch = shared->dispatch_mesh;
  shared->dispatch_mesh = ec::Address(reinterpret_cast<const void*>(&FakeDispatchMesh));
  const uint32_t arena = shared->arena;
  const uint32_t arena_size = shared->arena_size;
  test_code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x4000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  ec::Assembler assembler(std::span<uint8_t>(test_code, 0x4000));
  const HarnessFn harness2 = BuildHarness(&assembler, 2, 2);
  const HarnessFn harness3 = BuildHarness(&assembler, 3, 3);
  std::array<uint8_t, 0x200> local_tracker = {};
  const uint32_t outside = reinterpret_cast<uint32_t>(local_tracker.data());
  const std::array<uint32_t, 3> outsides = {outside, arena - 1, arena + arena_size};
  const std::array<uint32_t, 3> insides = {arena, arena + 5 * dr::SLOT_SIZE + 0x40, arena + arena_size - dr::SLOT_SIZE};

  // The depth bodies' shared-state call (2 arguments, `ret 8`) and slot-0x64 call (3 arguments, `ret 0xc`): the direct path is
  // the game's `mov eax, [eax + slot]; jmp eax`; the record path asks Record whatever the vtable is (the planner chose the class)
  // and Record's answer is the call's.
  std::array<uint32_t, 0x20> fake_vtable = {};
  fake_vtable[0x68 / 4] = ec::Address(reinterpret_cast<const void*>(&FakeVf68));
  fake_vtable[0x64 / 4] = ec::Address(reinterpret_cast<const void*>(&FakeVf64));
  const uint32_t vtable = ec::Address(fake_vtable.data());
  // The colour bodies' two calls are the same thunks with their own site ids (one thunk of each per id, shared by the 5 bodies).
  for (const auto& [vf68_site, vf64_site] : {std::pair{dr::Site::DEPTH_B_VF68, dr::Site::DEPTH_B_VF64}, std::pair{dr::Site::COLOUR_B_VF68, dr::Site::COLOUR_B_VF64}}) {
    const uint8_t* const vf68_thunk = ec::BuildSharedStateCall(&assembler, shared, nullptr, 0x68, 8, vf68_site);
    const uint8_t* const vf64_thunk = ec::BuildSharedStateCall(&assembler, shared, nullptr, 0x64, 0xc, vf64_site);
    REQUIRE(assembler.Fits());
    for (const uint32_t tracker : outsides) {
      CHECK(Call(harness2, tracker, vf68_thunk, {0x11, 0x22}, 0xBEEF, vtable) == 0x6868);
      CHECK(seen.calls == 0 && direct_calls == 1 && direct_ecx == 0xBEEF && direct_args[0] == 0x11 && direct_args[1] == 0x22);
      CheckDirect();
      CHECK(Call(harness3, tracker, vf64_thunk, {0x31, 0x32, 0x33}, 0xBEEF, vtable) == 0x6464);
      CHECK(seen.calls == 0 && direct_calls == 1 && direct_ecx == 0xBEEF && direct_args[0] == 0x31 && direct_args[1] == 0x32 && direct_args[2] == 0x33);
      CheckDirect();
    }
    for (const uint32_t tracker : insides) {
      for (const uint32_t eax : {vtable, 0u, 0x12345678u}) {
        CHECK(Call(harness2, tracker, vf68_thunk, {0x11, 0x22}, 0xBEEF, eax) == 0x600D);
        CHECK(seen.calls == 1 && direct_calls == 0 && seen.site == static_cast<uint32_t>(vf68_site) && reinterpret_cast<uint32_t>(seen.slot) == tracker);
        CHECK(seen.ecx == 0xBEEF && seen.stack[1] == 0x11 && seen.stack[2] == 0x22);
        CheckRecordFrame(tracker);
        CHECK(Call(harness3, tracker, vf64_thunk, {0x31, 0x32, 0x33}, 0xBEEF, eax) == 0x600D);
        CHECK(seen.calls == 1 && direct_calls == 0 && seen.site == static_cast<uint32_t>(vf64_site) && reinterpret_cast<uint32_t>(seen.slot) == tracker);
        CHECK(seen.ecx == 0xBEEF && seen.stack[1] == 0x31 && seen.stack[2] == 0x32 && seen.stack[3] == 0x33);
        CheckRecordFrame(tracker);
      }
    }
  }

  // The per-mesh cut on the game's own covered bytes: a frame the harness builds with the loop's locals at the game's offsets (the
  // other words are marked: a thunk that reads another place hands DispatchMesh a mark). The game's code after the loop is a stand-in
  // that notes esi, edi, ebx and ebp and leaves; the empty round goes to another one.
  const auto build_tail = [&](uint32_t path) {
    uint8_t* const tail = assembler.Cursor();
    assembler.BytesAndOperand({0x89, 0x35}, ec::Address(&cut_out.esi));
    assembler.BytesAndOperand({0x89, 0x3D}, ec::Address(&cut_out.edi));
    assembler.BytesAndOperand({0x89, 0x1D}, ec::Address(&cut_out.ebx));
    assembler.BytesAndOperand({0x89, 0x2D}, ec::Address(&cut_out.ebp));
    assembler.BytesAndOperand({0x89, 0x25}, ec::Address(&cut_out.esp));
    assembler.BytesAndOperand({0xC7, 0x05}, ec::Address(&cut_out.path));
    assembler.Bytes({static_cast<uint8_t>(path), 0, 0, 0});
    assembler.BytesAndOperand({0x8B, 0x25}, ec::Address(&cut_saved_esp));
    assembler.Bytes({0x5F, 0x5B, 0x5E, 0x5D, 0xC3});
    return tail;
  };
  const uint8_t* const loop_tail = build_tail(1);
  const uint8_t* const empty_tail = build_tail(2);
  const ec::RecordingSiteInfo& cut = ec::RECORDING_INFO[static_cast<size_t>(ec::RecordingSite::DEPTH_B_CUT)];
  const uint8_t* const cut_thunk = ec::BuildMeshCut(&assembler, shared, Rva(cut.rva), cut.covered, empty_tail, loop_tail);
  // cdecl void(const uint32_t* frame): push ebp esi ebx edi, make a 0x90-byte frame of the 36 words at the argument, ebp the tracker,
  // marks in the other registers, jump to the thunk.
  uint8_t* const cut_harness = assembler.Cursor();
  assembler.Bytes({0x55, 0x56, 0x53, 0x57, 0x8B, 0x74, 0x24, 0x14});
  assembler.BytesAndOperand({0x89, 0x25}, ec::Address(&cut_saved_esp));
  assembler.Bytes({0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x8B, 0xFC, 0xB9, 0x24, 0x00, 0x00, 0x00, 0xFC, 0xF3, 0xA5});
  assembler.BytesAndOperand({0x8B, 0x2D}, ec::Address(&cut_tracker));
  assembler.BytesAndOperand({0xBE}, 0x5151);
  assembler.BytesAndOperand({0xBF}, 0xD1D1);
  assembler.BytesAndOperand({0xBB}, 0xB1B1);
  assembler.Relative(0xE9, cut_thunk);
  REQUIRE(assembler.Fits());
  const auto run_cut = reinterpret_cast<void(__cdecl*)(const uint32_t*)>(cut_harness);
  const auto cut_round = [&](uint32_t first, uint32_t last) {
    std::array<uint32_t, 36> frame = {};
    for (uint32_t word = 0; word < frame.size(); ++word) frame[word] = 0xBAD00000 + word;
    frame[0x34 / 4] = first;
    frame[0x1c / 4] = last;
    frame[0x28 / 4] = 0x50A7ED;  // the sorted list
    frame[0x30 / 4] = 0x40DE;    // the node
    frame[0x10 / 4] = 0x1157;    // the list type
    frame[0x74 / 4] = 0x71E3;    // the view
    frame[0x70 / 4] = 0xDE71CE;  // the device
    cut_tracker = 0x7AC4E5;
    cut_seen = {};
    cut_out = {};
    cut_saved_esp = 0;
    run_cut(frame.data());
  };
  for (const std::array<uint32_t, 2> round : {std::array<uint32_t, 2>{3, 9}, {0, 0}, {7, 7}, {0, 0x7FFFFFFF}}) {
    cut_round(round[0], round[1]);
    CHECK(cut_seen.calls == 1 && cut_seen.active == 1 && shared->active.load() == 0);
    CHECK((cut_seen.args == std::array<uint32_t, 8>{0x7AC4E5, 0xDE71CE, 0x71E3, 0x1157, 0x40DE, 0x50A7ED, round[0], round[1]}));
    // The game's state after its loop: esi the list type, edi the last mesh, ebx past it, ebp and the stack as they were.
    CHECK(cut_out.path == 1 && cut_out.esi == 0x1157 && cut_out.edi == round[1] && cut_out.ebx == round[1] + 1 && cut_out.ebp == 0x7AC4E5);
    CHECK(cut_out.esp == cut_saved_esp - 0x90);
  }
  // An empty round (first past last): the game's own `ja`, esi untouched, nothing dispatched, active never raised.
  cut_round(5, 4);
  CHECK(cut_seen.calls == 0 && cut_out.path == 2 && cut_out.esi == 0x5151 && cut_out.edi == 4 && cut_out.ebx == 5 && cut_out.esp == cut_saved_esp - 0x90);
  cut_round(0x80000000u, 0x7FFFFFFFu);  // unsigned: the first is past the last
  CHECK(cut_seen.calls == 0 && cut_out.path == 2);
  shared->dispatch_mesh = saved_dispatch;
  shared->record = saved_record;
  VirtualFree(test_code, 0, MEM_RELEASE);
  std::printf("6c. B thunks: the shared-state call (ret 8) and the slot-0x64 call (ret 0xc) with the depth and the colour ids, direct and record; the per-mesh cut on the game's covered bytes hands DispatchMesh its 8 values from the loop's own places and leaves as the loop does (esi, edi, ebx), an empty round takes the game's `ja`\n");
}

// ---------------------------------------------------------------- 8. no branch lands in a B site

// The length of one 32-bit x86 instruction (0: an opcode this decoder does not know: the scan fails rather than guess) and the
// relative branch it makes (its displacement's size, 0 for none).
struct Decoded {
  uint32_t length = 0;
  uint32_t displacement = 0;
};

uint32_t ModrmSize(const uint8_t* at) {
  const uint32_t mod = at[0] >> 6;
  const uint32_t rm = at[0] & 7;
  uint32_t size = 1;
  if (mod != 3 && rm == 4) {
    ++size;
    if (mod == 0 && (at[1] & 7) == 5) size += 4;
  }
  if (mod == 0 && rm == 5) {
    size += 4;
  } else if (mod == 1) {
    size += 1;
  } else if (mod == 2) {
    size += 4;
  }
  return size;
}

Decoded Decode(const uint8_t* at) {
  uint32_t length = 0;
  bool operand16 = false;
  for (;; ++length) {
    const uint8_t prefix = at[length];
    if (prefix == 0x66) {
      operand16 = true;
    } else if (prefix != 0xF0 && prefix != 0xF2 && prefix != 0xF3 && prefix != 0x2E && prefix != 0x36 && prefix != 0x3E && prefix != 0x26
               && prefix != 0x64 && prefix != 0x65) {
      break;
    }
  }
  const uint8_t op = at[length++];
  const uint32_t immediate_z = (operand16 ? 2 : 4);
  uint32_t modrm = 0;
  uint32_t immediate = 0;
  uint32_t displacement = 0;
  if (op == 0x0F) {
    const uint8_t op2 = at[length++];
    if (op2 >= 0x80 && op2 <= 0x8F) {
      displacement = 4;
    } else if (op2 == 0x38) {
      ++length;
      modrm = ModrmSize(at + length);
    } else if (op2 == 0x3A) {
      ++length;
      modrm = ModrmSize(at + length);
      immediate = 1;
    } else if ((op2 >= 0x40 && op2 <= 0x4F) || (op2 >= 0x90 && op2 <= 0x9F) || (op2 >= 0x10 && op2 <= 0x17) || (op2 >= 0x28 && op2 <= 0x2F)
               || (op2 >= 0x50 && op2 <= 0x6F) || op2 == 0x7E || op2 == 0x7F || op2 == 0x1F || op2 == 0xA3 || op2 == 0xAB || op2 == 0xAF
               || op2 == 0xB0 || op2 == 0xB1 || op2 == 0xB3 || op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBB || op2 == 0xBC || op2 == 0xBD
               || op2 == 0xBE || op2 == 0xBF || op2 == 0xC0 || op2 == 0xC1 || op2 == 0x18 || op2 == 0x0D || op2 == 0xAE || op2 >= 0xD0) {
      modrm = ModrmSize(at + length);
    } else if (op2 == 0x70 || op2 == 0x71 || op2 == 0x72 || op2 == 0x73 || op2 == 0xA4 || op2 == 0xAC || op2 == 0xBA || op2 == 0xC2
               || op2 == 0xC4 || op2 == 0xC5 || op2 == 0xC6) {
      modrm = ModrmSize(at + length);
      immediate = 1;
    } else if (op2 != 0x31 && op2 != 0xA2 && op2 != 0x0B && !(op2 >= 0xC8 && op2 <= 0xCF)) {
      return {};
    }
  } else if (op < 0x40) {
    if ((op & 7) < 4) {
      modrm = ModrmSize(at + length);
    } else if ((op & 7) == 4) {
      immediate = 1;
    } else if ((op & 7) == 5) {
      immediate = immediate_z;
    }  // 6 and 7: push / pop of a segment register, or a prefix / decimal-adjust: one byte
  } else if (op < 0x60 || op == 0x60 || op == 0x61 || (op >= 0x90 && op <= 0x99) || (op >= 0x9B && op <= 0x9F) || (op >= 0xA4 && op <= 0xA7)
             || (op >= 0xAA && op <= 0xAF) || op == 0xC3 || op == 0xC9 || op == 0xCB || op == 0xCC || op == 0xCE || op == 0xCF || op == 0xD6
             || op == 0xD7 || (op >= 0xEC && op <= 0xEF) || op == 0xF1 || op == 0xF4 || op == 0xF5 || (op >= 0xF8 && op <= 0xFD)
             || (op >= 0x6C && op <= 0x6F)) {
    // one byte
  } else if (op == 0x62 || op == 0x63 || (op >= 0x84 && op <= 0x8F) || op == 0xC4 || op == 0xC5 || (op >= 0xD0 && op <= 0xD3)
             || (op >= 0xD8 && op <= 0xDF) || op == 0xFE || op == 0xFF) {
    modrm = ModrmSize(at + length);
  } else if (op == 0x68) {
    immediate = immediate_z;
  } else if (op == 0x69) {
    modrm = ModrmSize(at + length);
    immediate = immediate_z;
  } else if (op == 0x6A) {
    immediate = 1;
  } else if (op == 0x6B) {
    modrm = ModrmSize(at + length);
    immediate = 1;
  } else if (op >= 0x70 && op <= 0x7F) {
    displacement = 1;
  } else if (op == 0x80 || op == 0x82 || op == 0x83 || op == 0xC0 || op == 0xC1 || op == 0xC6) {
    modrm = ModrmSize(at + length);
    immediate = 1;
  } else if (op == 0x81 || op == 0xC7) {
    modrm = ModrmSize(at + length);
    immediate = immediate_z;
  } else if (op >= 0xA0 && op <= 0xA3) {
    immediate = 4;
  } else if (op == 0xA8 || (op >= 0xB0 && op <= 0xB7) || op == 0xCD || op == 0xD4 || op == 0xD5 || (op >= 0xE4 && op <= 0xE7)) {
    immediate = 1;
  } else if (op == 0xA9 || (op >= 0xB8 && op <= 0xBF)) {
    immediate = immediate_z;
  } else if (op == 0xC2 || op == 0xCA) {
    immediate = 2;
  } else if (op == 0xC8) {
    immediate = 3;
  } else if (op == 0xE8 || op == 0xE9) {
    displacement = 4;
  } else if (op == 0xEB || (op >= 0xE0 && op <= 0xE3)) {
    displacement = 1;
  } else if (op == 0xF6 || op == 0xF7) {
    modrm = ModrmSize(at + length);
    immediate = (((at[length] >> 3) & 7) < 2 ? (op == 0xF6 ? 1 : immediate_z) : 0);
  } else {
    return {};
  }
  return {.length = length + modrm + immediate + displacement, .displacement = displacement};
}

// The bytes at `rva` that a branch in the game could land inside a B site (its first byte excluded: a call or jump to the site itself
// is the game's own entry into the instruction the thunk stands for).
void TestJumpScan() {
  REQUIRE(Prepare());
  // The functions that hold the B sites, swept from their entries to the end of their code (the padding after the last return, or the
  // driver's jump tables): an instruction-aligned sweep, so that a branch is a real one. The end each must reach is checked.
  struct Function {
    uint32_t begin;
    uint32_t end;
  };
  constexpr std::array<Function, 11> FUNCTIONS = {{{0x17ae940, 0x17aee84},
                                                   {0x17aef40, 0x17af2ac},
                                                   {0x17af2b0, 0x17af71a},
                                                   {0x17af720, 0x17afc0c},
                                                   {0x17afc10, 0x17aff8e},
                                                   {0x17aff90, 0x17b03fa},
                                                  {0x17b09f0, 0x17b0cab},
                                                  {0x17b0cb0, 0x17b0f78},
                                                  {0x17b0f80, 0x17b124d},
                                                  {0x17b1250, 0x17b151d},
                                                  {0x17b1520, 0x17b17e8}}};
  std::set<uint32_t> starts;
  std::set<uint32_t> targets;
  std::vector<std::pair<uint32_t, uint32_t>> branches;  // (the branch's own address, its target)
  uint32_t instructions = 0;
  for (const Function& function : FUNCTIONS) {
    uint32_t rva = function.begin;
    while (rva < function.end && image[rva] != 0xCC) {
      const Decoded decoded = Decode(image + rva);
      if (decoded.length == 0) {
        std::printf("   unknown opcode %02X at +0x%X\n", image[rva], rva);
        REQUIRE(false);
      }
      starts.insert(rva);
      if (decoded.displacement != 0) {
        const uint32_t at = rva + decoded.length;
        const int32_t displacement = (decoded.displacement == 1 ? static_cast<int8_t>(image[at - 1]) : static_cast<int32_t>(Read32(image + at - 4)));
        targets.insert(at + displacement);
        branches.emplace_back(rva, at + displacement);
      }
      rva += decoded.length;
      ++instructions;
    }
    CHECK(rva == function.end);
  }
  uint32_t sites = 0;
  uint32_t blocks = 0;
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
    const ec::RecordingSiteInfo& site_info = ec::RECORDING_INFO[index];
    if (site_info.group != dr::Group::DEPTH_B && site_info.group != dr::Group::COLOUR_B) continue;
    const uint32_t site = site_info.rva;
    const uint32_t covered = std::max<uint32_t>(site_info.covered, ec::SITE_SIZE);
    ++sites;
    // The site starts an instruction and the covered bytes end on an instruction's end.
    CHECK(starts.contains(site) && starts.contains(site + covered));
    for (uint32_t inside = site + 1; inside < site + covered; ++inside) {
      CHECK(!targets.contains(inside));
    }
    if (site_info.record_site == dr::Site::COLOUR_B_STATE) {
      // A state block: its record path skips the game's instructions up to the exit, so the exit starts an instruction and no branch
      // from outside the block lands inside it (past its first byte).
      ++blocks;
      CHECK(starts.contains(site_info.exit_rva) && site_info.exit_rva > site + covered);
      for (const auto& [from, to] : branches) {
        CHECK((from >= site && from < site_info.exit_rva) || to <= site || to >= site_info.exit_rva);
      }
    }
  }
  CHECK(sites == 36 && blocks == 5);
  // Beyond the swept functions: every byte offset of every executable section read as a rel32 call, jump or conditional jump, and every
  // relocated dword (the jump tables' entries among them): none points into a site's covered bytes.
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
  uint32_t lowest = 0xFFFFFFFF;
  uint32_t highest = 0;
  // A site's covered bytes (past the first), and a state block's inside up to its exit, which the block's own branches may reach.
  struct Window {
    uint32_t first;
    uint32_t last;
    uint32_t own_first;
    uint32_t own_last;
  };
  std::vector<Window> windows;
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
    const ec::RecordingSiteInfo& site_info = ec::RECORDING_INFO[index];
    if (site_info.group != dr::Group::DEPTH_B && site_info.group != dr::Group::COLOUR_B) continue;
    const bool block = (site_info.record_site == dr::Site::COLOUR_B_STATE);
    windows.push_back({site_info.rva + 1, (block ? site_info.exit_rva : site_info.rva + std::max<uint32_t>(site_info.covered, ec::SITE_SIZE)),
                       (block ? site_info.rva : 0), (block ? site_info.exit_rva : 0)});
    lowest = std::min(lowest, windows.back().first);
    highest = std::max(highest, windows.back().last);
  }
  const auto in_window = [&](uint32_t target, uint32_t from) {
    return target >= lowest && target < highest
           && std::any_of(windows.begin(), windows.end(), [&](const Window& window) {
                return target >= window.first && target < window.last && !(from >= window.own_first && from < window.own_last);
              });
  };
  uint64_t candidates = 0;
  uint32_t hits = 0;
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
    const uint32_t end = section->VirtualAddress + std::min<uint32_t>(section->Misc.VirtualSize, section->SizeOfRawData);
    for (uint32_t at = section->VirtualAddress; at + 6 < end; ++at) {
      const uint8_t first = image[at];
      if (first == 0xE8 || first == 0xE9) {
        ++candidates;
        hits += (in_window(at + 5 + Read32(image + at + 1), at) ? 1 : 0);
      } else if (first == 0x0F && (image[at + 1] & 0xF0) == 0x80) {
        ++candidates;
        hits += (in_window(at + 6 + Read32(image + at + 2), at) ? 1 : 0);
      }
    }
  }
  CHECK(hits == 0);
  const IMAGE_DATA_DIRECTORY& relocations = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  uint32_t relocated = 0;
  uint32_t relocated_hits = 0;
  for (uint32_t offset = 0; offset < relocations.Size;) {
    const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(image + relocations.VirtualAddress + offset);
    const auto* entries = reinterpret_cast<const uint16_t*>(block + 1);
    for (uint32_t entry = 0; entry < (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2; ++entry) {
      if ((entries[entry] >> 12) != IMAGE_REL_BASED_HIGHLOW) continue;
      ++relocated;
      relocated_hits += (in_window(Read32(image + block->VirtualAddress + (entries[entry] & 0xFFF)) - ec::Address(image), 0xFFFFFFFF) ? 1 : 0);
    }
    offset += block->SizeOfBlock;
  }
  CHECK(relocated_hits == 0);
  std::printf("8. jump scan: the cut, the 15 depth-body calls and the 20 colour-body sites (36 sites, 5 of them state blocks): %u instructions swept in the driver and the 10 bodies (each sweep ends at its function's end), %zu branch targets, none inside a site's bytes after its first and each site starts an instruction; no branch from outside a state block lands inside it, and its exit starts an instruction; %llu rel32 call / jump / conditional jump candidates over every byte of the executable sections, %u hit; %u relocated dwords, %u hit\n",
              instructions, targets.size(), static_cast<unsigned long long>(candidates), hits, relocated, relocated_hits);
}

// The heapfix clearance: the nearest range heapfix patches or checks to each site (the table's compile-time check is the proof; this
// prints the distances of the B sites, and re-checks them against the real tables).
void TestHeapfixClearance() {
  const auto gap = [](uint32_t rva, uint32_t size, const ec::HeapfixRange& range) {
    return range.rva >= rva + size ? range.rva - (rva + size) : rva - (range.rva + range.size);
  };
  const auto hooked_entry = [](const ec::HeapfixRange& range) {
    return std::find(ec::HOOKED_ENTRIES.begin(), ec::HOOKED_ENTRIES.end(), range.rva) != ec::HOOKED_ENTRIES.end();
  };
  uint32_t smallest = 0xFFFFFFFF;
  uint32_t on_entries = 0;
  std::string report;
  for (const ec::RecordingSiteInfo& site_info : ec::RECORDING_INFO) {
    // A colour state block's bytes run to its exit (the record path skips them all).
    const bool state_block = (site_info.record_site == dr::Site::COLOUR_B_STATE);
    const uint32_t size = (state_block ? site_info.exit_rva - site_info.rva : std::max<uint32_t>(site_info.covered, ec::SITE_SIZE));
    CHECK(!state_block || size > 100);
    uint32_t patched_gap = 0xFFFFFFFF;
    uint32_t checked_gap = 0xFFFFFFFF;
    const ec::HeapfixRange* patched_near = nullptr;
    const ec::HeapfixRange* checked_near = nullptr;
    // The patched ranges, the timing hooks among them: a timing hook may touch a site only on one of our entries (refused at run time).
    const auto scan_patched = [&](const auto& table, bool timing) {
      for (const ec::HeapfixRange& range : table) {
        if (ec::Touches(site_info.rva, size, range)) {
          CHECK(timing && hooked_entry(range));
          ++on_entries;
          continue;
        }
        if (gap(site_info.rva, size, range) < patched_gap) {
          patched_gap = gap(site_info.rva, size, range);
          patched_near = &range;
        }
      }
    };
    scan_patched(ec::HEAPFIX_PATCHED, false);
    scan_patched(ec::HEAPFIX_TIMING_HOOKS, true);
    for (const ec::HeapfixRange& range : ec::HEAPFIX_CHECKED) {
      CHECK(!ec::Touches(site_info.rva, size, range));
      if (gap(site_info.rva, size, range) < checked_gap) {
        checked_gap = gap(site_info.rva, size, range);
        checked_near = &range;
      }
    }
    smallest = std::min({smallest, patched_gap, checked_gap});
    CHECK(ec::SiteClearOfHeapfix(site_info));
    if (site_info.group == dr::Group::DEPTH_B || site_info.group == dr::Group::COLOUR_B) {
      char line[220];
      std::snprintf(line, sizeof(line), "   +0x%X (%u bytes) %-42s patched +0x%X/%u gap %u, checked +0x%X/%u gap %u\n", site_info.rva, size, std::string(site_info.name).c_str(),
                    patched_near->rva, patched_near->size, patched_gap, checked_near->rva, checked_near->size, checked_gap);
      report += line;
    }
  }
  // The rows of every heapfix module that writes into or checks the game's code (heapfix_ranges.hpp), by their module's names: the
  // patched ones, the timing hooks and the checked ones.
  struct Row {
    uint32_t rva;
    uint32_t size;
  };
  constexpr std::array<Row, 20> PATCHED_ROWS = {{{0x18F9150, 6}, {0xD79AC8, 36}, {0xD79B12, 36}, {0x189B363, 36}, {0x174A8BD, 86}, {0x18E8390, 5}, {0x17AD5D0, 5},
                                                 {0x17ACB50, 5}, {0x174CD88, 5}, {0x1A27CA0, 6}, {0xC1950, 10}, {0x17DABAF, 5}, {0x182E0DA, 9}, {0x17DAB90, 8},
                                                 {0x112D80, 10}, {0x112730, 8}, {0x113040, 5}, {0x1126E0, 7}, {0x7B6F60, 5}, {0x7B7070, 5}}};
  constexpr std::array<Row, 6> TIMING_ROWS = {{{0x17FA310, 6}, {0x186BE70, 10}, {0x1893CC0, 6}, {0x1722420, 7}, {0x176A450, 5}, {0x177BB10, 5}}};
  constexpr std::array<Row, 21> CHECKED_ROWS = {{{0x18E8390, 0x342}, {0x192EEE0, 0x22A}, {0x17AD5D0, 630}, {0x17ACB50, 630}, {0x174CCA0, 0xED}, {0x174CC20, 0x27}, {0x175D5D0, 0xEC},
                                                 {0x1626190, 10}, {0x16238A0, 6}, {0x112D80, 12}, {0x112730, 12}, {0x113040, 12}, {0x1126E0, 12},
                                                 {0x7B6F60, 267}, {0x7B6800, 308}, {0x7B6E40, 279}, {0x7B6A80, 310}, {0x7B7070, 311}, {0x7B6BC0, 311}, {0x7B6D00, 309},
                                                 {0x7B6940, 309}}};
  const auto present = [](const auto& table, const std::span<const Row> rows) {
    for (const Row& row : rows) {
      CHECK(std::any_of(table.begin(), table.end(), [&](const ec::HeapfixRange& range) { return range.rva == row.rva && range.size == row.size; }));
    }
  };
  present(ec::HEAPFIX_PATCHED, PATCHED_ROWS);
  present(ec::HEAPFIX_TIMING_HOOKS, TIMING_ROWS);
  present(ec::HEAPFIX_CHECKED, CHECKED_ROWS);
  // Heapfix's timing hooks sit on the entries our groups read (the flush, the two loops, the bodies): 12 of the 13 guarded entries
  // have a row (the depth body at +0x17B0CB0 is guarded at run time though no timing hook is there).
  CHECK(std::count_if(ec::HEAPFIX_TIMING_HOOKS.begin(), ec::HEAPFIX_TIMING_HOOKS.end(), hooked_entry) == 12 && ec::HOOKED_ENTRIES.size() == 13);
  std::printf("9. heapfix clearance: no site of the %zu touches a range heapfix patches (%zu rows of every module) or checks (%zu) or a timing hook (%zu rows: %u touches, all on our own hooked entries, which a run refuses), and none of the bytes a site, its callee or its exit is found by touches a patched one; smallest gap of any site %u bytes; the B sites' nearest ranges:\n%s",
              ec::RECORDING_SITE_COUNT, ec::HEAPFIX_PATCHED.size(), ec::HEAPFIX_CHECKED.size(), ec::HEAPFIX_TIMING_HOOKS.size(), on_entries, smallest,
              report.c_str());
}

// ---------------------------------------------------------------- 7. the assembler's bounds

void TestAssembler() {
  std::array<uint8_t, 24> memory;
  memory.fill(0xCC);
  ec::Assembler assembler(std::span<uint8_t>(memory.data() + 4, 12));
  for (int count = 0; count < 20; ++count) assembler.Bytes({0x90});
  CHECK(!assembler.Fits() && assembler.Cursor() == memory.data() + 16);
  CHECK(std::all_of(memory.begin(), memory.begin() + 4, [](uint8_t byte) { return byte == 0xCC; }));
  CHECK(std::all_of(memory.begin() + 16, memory.end(), [](uint8_t byte) { return byte == 0xCC; }));
  memory.fill(0xCC);
  ec::Assembler jumps(std::span<uint8_t>(memory.data() + 4, 16));
  uint8_t* const near_jump = jumps.Short(0xEB);
  for (int count = 0; count < 12; ++count) jumps.Bytes({0x90});
  jumps.Bind(near_jump);
  CHECK(jumps.Fits() && *near_jump == 12);
  ec::Assembler far_jumps(std::span<uint8_t>(memory.data() + 4, 20));
  uint8_t* const far_jump = far_jumps.Short(0xEB);
  far_jumps.Bytes({0x90});
  far_jumps.Bind(far_jump);
  CHECK(far_jumps.Fits() && *far_jump == 1);
  memory.fill(0xCC);
  ec::Assembler overflow(std::span<uint8_t>(memory.data() + 4, 4));
  overflow.BytesAndOperand({0xB8}, 1);
  CHECK(!overflow.Fits() && memory[8] == 0xCC);
  std::printf("7. assembler stays inside its page\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const char* path = (argc > 1 ? argv[1] : R"(C:\Program Files (x86)\Steam\steamapps\common\Total War Attila\empire.retail.dll)");
  std::ifstream stream(path, std::ios::binary);
  REQUIRE(stream.good());
  file.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data())->e_lfanew);
  REQUIRE(nt->OptionalHeader.ImageBase == 0x10000000);
  image = LayoutImage();
  REQUIRE(image != nullptr);
  pristine.assign(image, image + image_size);
  std::printf("empire.retail.dll build %08lX, %zu bytes: laid out as data, never run\n", nt->FileHeader.TimeDateStamp, file.size());
  TestRealBytes();
  TestWanted();
  TestFlips();
  TestHookedEntries();
  TestWindow();
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
  TestProtectFailure();
#endif
  TestThreadInThunks();
  TestStandDown();
  TestRefusedTurnOn();
  TestColourGroup();
  TestClassTable();
  TestBGroup();
  TestThunks();
  TestColourThunks();
  TestBThunks();
  TestJumpScan();
  TestHeapfixClearance();
  TestAssembler();
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}

#endif
