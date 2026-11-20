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

constexpr uint32_t ALL_SITES = (1u << ec::RECORDING_SITE_COUNT) - 1;
constexpr uint64_t DEPTH_A_SITES = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::DEPTH_A)] | ec::RECORDING_ENTRY_BIT;
constexpr uint64_t DEPTH_A_FALLBACK = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::DEPTH_A)] | ec::RECORDING_FALLBACK[static_cast<size_t>(dr::Group::DEPTH_A)];
constexpr uint64_t COLOUR_A_OWN = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::COLOUR_A)];
constexpr uint64_t COLOUR_A_SITES = COLOUR_A_OWN | ec::RECORDING_ENTRY_BIT;
constexpr uint64_t COLOUR_A_FALLBACK = COLOUR_A_OWN | ec::RECORDING_FALLBACK[static_cast<size_t>(dr::Group::COLOUR_A)];

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
  CHECK(ec::recording_prepared && ec::recording_page != nullptr && ec::recording_cut_end > ec::recording_page);
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
  CHECK(ec::recording_active == &dr::SharedPage()->active && *ec::recording_arena_size == dr::SLOT_COUNT * dr::SLOT_SIZE);
  CHECK(LogHas("23 of 23 sites found") && !LogHas("left alone"));
  // The colour loop's pieces, decoded from the code: the call targets, the faction block's default and the cache flag.
  CHECK(calls.loops[static_cast<size_t>(dr::Group::COLOUR_A)] == ec::Address(Rva(ec::COLOUR_LOOP_RVA)));
  CHECK(calls.dirty_call == ec::Address(Rva(0x191d960)) && calls.srv_call == ec::Address(Rva(0x18d2b60)));
  CHECK(calls.faction_call == ec::Address(Rva(0x1885350)) && calls.view_call == ec::Address(Rva(ec::TEXTURE_VIEW_RVA)));
  CHECK(calls.cached_constants == Rva(0x1da661b) && calls.faction_default == reinterpret_cast<const float*>(Rva(0x1afbd74)));
  // The B groups' helpers are set at the end of Prepare, not left for the class table's step: no dispatcher call is made on 0.
  CHECK(calls.accessor == ec::Address(Rva(ec::ACCESSOR_RVA)) && calls.vf64_stub == ec::Address(Rva(ec::WEIGHTED_VF64)));
  CHECK(calls.module_shift == ec::Address(image) - 0x10000000);
  CHECK((calls.usable_groups & 3) == 3);
  std::printf("1. 23 sites found on the real bytes (7 flush call sites, the entry hook, the 2 cuts, the 2 inits, 3 shared-state calls, 2 animation calls, setup, 2 constants, 2 textures, faction)\n");
}

// ---------------------------------------------------------------- 2. what a group asks for

void TestWanted() {
  REQUIRE(Prepare());
  CHECK(ec::RecordingWanted(0) == 0);
  CHECK(ec::RecordingWanted(1u << static_cast<uint32_t>(dr::Group::DEPTH_A)) == DEPTH_A_SITES);
  CHECK(ec::RecordingWanted(1u << static_cast<uint32_t>(dr::Group::COLOUR_A)) == COLOUR_A_SITES);
  CHECK(ec::RecordingWanted(0b11) == (DEPTH_A_SITES | COLOUR_A_SITES));
  CHECK(ec::RecordingWanted(1u << static_cast<uint32_t>(dr::Group::DEPTH_B)) == 0);  // the B groups' own sites are later steps
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
                        || is_fixed(ec::ACCESSOR_HEAD, ec::ACCESSOR_RVA, at) || is_fixed(ec::VF64_STUB_HEAD, ec::WEIGHTED_VF64, at);
    for (const ec::WeightedClass& weighted : ec::WEIGHTED_CLASSES) {
      for (const uint32_t slot : SLOTS) shared_piece |= (at >= weighted.vtable + slot && at < weighted.vtable + slot + 4);
    }
    uint32_t expected = ALL_SITES;
    for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
      const ec::RecordingSiteInfo& site_info = ec::RECORDING_INFO[index];
      if (is_fixed(site_info.context, site_info.rva - site_info.context_offset, at)
          || (site_info.callee_rva != 0 && (is_fixed(site_info.callee_head, site_info.callee_rva, at) || (at > site_info.rva && at < site_info.rva + 5)))
          || (site_info.exit_rva != 0 && is_fixed(site_info.exit_head, site_info.exit_rva, at))) {
        expected &= ~(1u << index);
      }
    }
    // A colour piece (or the constants' two flag operands, which must be one address) takes the colour loop's own sites.
    bool colour_piece = false;
    for (const ec::ColourPieceInfo& piece : ec::COLOUR_PIECES) {
      colour_piece |= is_fixed(piece.context, piece.rva - piece.context_offset, at);
      colour_piece |= (piece.callee_rva != 0 && (is_fixed(piece.callee_head, piece.callee_rva, at) || (at > piece.rva && at < piece.rva + 5)));
    }
    for (const uint32_t flag_site : {ec::COLOUR_CONST1_RVA, ec::COLOUR_CONST2_RVA}) colour_piece |= (at >= flag_site + 2 && at < flag_site + 6);
    if (colour_piece) expected &= ~static_cast<uint32_t>(COLOUR_A_OWN);
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
  VirtualProtect(ec::recording_page, 0x1000, PAGE_EXECUTE_READWRITE, &protection);
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
  VirtualProtect(ec::recording_page, 0x1000, PAGE_EXECUTE_READ, &protection);
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
  REQUIRE(flush_thunk >= ec::recording_cut_end && flush_thunk + 4 <= ec::recording_page_end);
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
  // Every site is as it should be (a jump if in `in`, else the game's), but the ones in `skip`.
  const auto all_hold = [](uint64_t in, uint64_t skip = 0) {
    bool holds = true;
    for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
      holds = holds && (((skip >> index) & 1) != 0 || SiteHolds(index, ((in >> index) & 1) != 0));
    }
    return holds;
  };
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
  CHECK(ec::recording_written == COLOUR_A_SITES && shared->arena_size == arena_size && all_hold(COLOUR_A_SITES));
  CHECK(std::popcount(ec::recording_written) == 12 && ecp::DrawRecordingSitesIn());
  UpdateWith(colour_row | depth_row);
  CHECK(ec::recording_written == (COLOUR_A_SITES | DEPTH_A_SITES) && all_hold(COLOUR_A_SITES | DEPTH_A_SITES));
  // The row of one off: its own sites come out, the entry hook stays for the other, the arena size too.
  UpdateWith(depth_row);
  CHECK(ec::recording_written == DEPTH_A_SITES && shared->arena_size == arena_size && all_hold(DEPTH_A_SITES));
  UpdateWith(colour_row | depth_row);
  UpdateWith(colour_row);
  CHECK(ec::recording_written == COLOUR_A_SITES && all_hold(COLOUR_A_SITES));
  UpdateWith(0);
  CHECK(ec::recording_written == 0 && shared->arena_size == 0 && !ecp::DrawRecordingSitesIn() && DifferingBytes() == 0);
  // Another mod's bytes on one colour site: the colour group stays out, the depth group goes in, the log names the site.
  REQUIRE(Prepare());
  uint8_t* const modded = SiteAt(ec::RecordingSite::COLOUR_CONST2);
  modded[0] ^= 0x55;
  log_lines.clear();
  UpdateWith(colour_row | depth_row);
  const uint64_t modded_bit = ec::RecordingBit(ec::RecordingSite::COLOUR_CONST2);
  CHECK(ec::recording_written == DEPTH_A_SITES && (ec::recording_found & COLOUR_A_OWN) == 0 && all_hold(DEPTH_A_SITES, modded_bit));
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
  CHECK(all_hold(COLOUR_A_SITES, ec::RecordingBit(ec::RecordingSite::DEPTH_ANIM)) && differing_outside_written() == 1);
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
  const uint8_t* const vf68_thunk = ec::BuildSharedStateCall(&assembler, shared, known, dr::Site::DEPTH_VF68);
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
    const uint8_t* const vf68_thunk = ec::BuildSharedStateCall(&assembler, shared, known, site);
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
  TestThunks();
  TestColourThunks();
  TestAssembler();
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}

#endif
