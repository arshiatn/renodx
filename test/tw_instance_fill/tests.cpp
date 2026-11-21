// Parallel Instance Fill (twattila/instance_fill.hpp) on the real shared worker pool (twattila/worker_pool.hpp) and the game's own record
// copies (empire.retail.dll +0x17dab80 and +0x17db270), x86 only. The DLL is only READ from disk and mapped twice, relocated, executable
// and writable, never loaded as a DLL (as test/tw_draw_recording_loop does): copy A as the game ships it (the serial reference, the
// record copies run in it), copy B prepared by instance_fill (the sites' bytes, the dispatcher's record copies).
// 1. The sites: all 4 contexts and the calls' chains match the image; the bytes the sites would write; no overlap with heapfix's tables
//    (patched, timing hooks, checked) or with the other features' sites (engine_code's, Threaded Draw Recording's, the effect update's);
//    no branch of the game into a site (the whole code section, the short forms too); Prepare refuses each broken byte; the sites go in
//    and out (Rewrite), a site that is another tool's stands every site down, a thread inside a site is seen.
// 2. The fills: the dispatcher (the sites' functions, called as the game's call does) against the game's record copy run once on the
//    whole range, on the real pool with 1 to 8 threads, 1 to 20000 items, random begin0, cached and write-combined destinations, all 4
//    sites: equal bytes, nothing written past the end.
// 3. The partition: a counting copy sees every item exactly once, by more than one thread; the runs that must stay alone (row off,
//    below the minimum, no worker free, another thread, inside a job, on a worker) and the trace and check lines.
// 4. CONTROLs, each must differ: overlapping chunks, the last chunk dropped, a return before the join, the wrong record copy for a site,
//    the chunks' ranges without begin0, a refused Start that runs no part; the check mode on a copy that depends on its thread.
// tests.exe [empire.retail.dll]
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

extern "C" __declspec(dllexport) const char* const NAME = "Instance fill test";

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
#include "src/games/twattila/instance_fill.hpp"

namespace fill = twattila::instance_fill;
namespace fi = twattila::instance_fill::internal;
namespace wp = twattila::worker_pool;
namespace ec = twattila::engine_code::internal;

namespace {

int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

std::vector<uint8_t> file;
uint8_t* game = nullptr;     // A: as the game ships it
uint8_t* patched = nullptr;  // B: prepared by instance_fill
uint32_t image_size = 0;
DWORD main_thread = 0;
uint32_t env_generation = 0;

uint32_t Read32(const uint8_t* at) {
  uint32_t value;
  std::memcpy(&value, at, sizeof(value));
  return value;
}

void Write32(uint8_t* at, uint32_t value) { std::memcpy(at, &value, sizeof(value)); }

// The DLL's sections at a new address, relocated. Executable and writable (a test).
uint8_t* Map(uint32_t* size) {
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
  *size = nt->OptionalHeader.SizeOfImage;
  auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, *size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (base == nullptr) return nullptr;
  std::memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    const size_t bytes = std::min<size_t>(section->SizeOfRawData, section->Misc.VirtualSize);
    if (bytes != 0) std::memcpy(base + section->VirtualAddress, file.data() + section->PointerToRawData, bytes);
  }
  const uint32_t delta = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base)) - nt->OptionalHeader.ImageBase;
  const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  for (uint32_t at = 0; at < directory.Size;) {
    const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(base + directory.VirtualAddress + at);
    if (block->SizeOfBlock == 0) break;
    const auto* entries = reinterpret_cast<const uint16_t*>(block + 1);
    const size_t count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
    for (size_t entry = 0; entry < count; ++entry) {
      if ((entries[entry] >> 12) != IMAGE_REL_BASED_HIGHLOW) continue;
      uint8_t* fixed = base + block->VirtualAddress + (entries[entry] & 0xFFF);
      Write32(fixed, Read32(fixed) + delta);
    }
    at += block->SizeOfBlock;
  }
  return base;
}

// One present of the add-on: the pool with `threads` (the render thread included), the user's row on.
void UpdatePool(uint32_t threads) { wp::Update({.threads = threads, .in_use = true, .env_generation = env_generation}); }

// ---------------------------------------------------------------- the game's data the record copies read

// The tracker: +0x44 the record size, +0x48 where the records go, +0x84 the items of the tracker's own list (the sources themselves).
struct Tracker {
  uint8_t pad0[0x44];
  uint32_t stride;
  uint8_t* destination;
  uint8_t pad1[0x84 - 0x4c];
  uint8_t** items;
  uint8_t pad2[8];
};
static_assert(sizeof(Tracker) == 0x90 && offsetof(Tracker, stride) == 0x44 && offsetof(Tracker, destination) == 0x48
              && offsetof(Tracker, items) == 0x84);

// The list's +8 holds the items; an item's record is at +0x74.
struct List {
  uint32_t pad;
  uint32_t count;
  uint8_t** items;
};
static_assert(offsetof(List, items) == 8);

// The depth and colour fills' functor; the visibility and folded fills read the first word only (the others are valid anyway, so the
// wrong record copy of a control runs on data it can read).
struct Functor {
  Tracker* tracker;
  List* list;
  uint32_t begin0;
};

constexpr uint32_t ITEM_HEAD = 0x74;

struct Scenario {
  size_t site = 0;
  uint32_t n = 0;
  uint32_t stride = 0;
  uint32_t begin0 = 0;
  bool write_combined = false;
  std::vector<uint8_t> storage;
  std::vector<uint8_t*> list_items;
  std::vector<uint8_t*> tracker_items;
  List list = {};
  std::array<Tracker, 2> trackers = {};  // 0: the dispatcher's, 1: the serial reference's
  std::array<Functor, 2> functors = {};
  std::array<uint8_t*, 2> destinations = {};
  size_t destination_bytes = 0;
  std::vector<uint8_t> expected;

  ~Scenario() {
    for (uint8_t* destination : destinations) {
      if (destination != nullptr) VirtualFree(destination, 0, MEM_RELEASE);
    }
  }
  fill::Range Range() const { return {.begin = begin0, .end = begin0 + n, .grain = std::max<uint32_t>(1, n / 4)}; }
};

constexpr uint8_t SENTINEL = 0xCD;
// The destinations have a margin before the records too: the controls that count from the wrong item write before them.
constexpr size_t FRONT_MARGIN = 1u << 20;
std::mt19937 shared_random(1);

std::unique_ptr<Scenario> Build(size_t site, uint32_t n, uint32_t stride, uint32_t begin0, bool write_combined, std::mt19937* random) {
  auto scenario = std::make_unique<Scenario>();
  const bool list_copy = (fill::SITES[site].copy == fill::Copy::LIST_ITEMS);
  scenario->site = site;
  scenario->n = n;
  scenario->stride = stride;
  scenario->begin0 = (list_copy ? begin0 : 0);
  scenario->write_combined = write_combined;
  const uint32_t items = scenario->begin0 + n;
  const size_t item_bytes = ITEM_HEAD + stride;
  scenario->storage.resize(item_bytes * items);
  for (uint8_t& byte : scenario->storage) byte = static_cast<uint8_t>((*random)());
  for (uint32_t i = 0; i < items; ++i) {
    scenario->list_items.push_back(scenario->storage.data() + item_bytes * i);
    scenario->tracker_items.push_back(scenario->storage.data() + item_bytes * i + ITEM_HEAD);
  }
  scenario->list = {.pad = 0, .count = items, .items = scenario->list_items.data()};
  // Room for what the wrong copy writes (it counts from item 0), and a margin that must stay untouched.
  scenario->destination_bytes = FRONT_MARGIN + static_cast<size_t>(stride) * (items + 64);
  for (size_t side = 0; side < 2; ++side) {
    const DWORD protection = PAGE_READWRITE | (write_combined && side == 0 ? PAGE_WRITECOMBINE : 0);
    scenario->destinations[side] =
        static_cast<uint8_t*>(VirtualAlloc(nullptr, scenario->destination_bytes, MEM_COMMIT | MEM_RESERVE, protection));
    REQUIRE(scenario->destinations[side] != nullptr);
    std::memset(scenario->destinations[side], SENTINEL, scenario->destination_bytes);
    scenario->trackers[side] = {.stride = stride, .destination = scenario->destinations[side] + FRONT_MARGIN, .items = scenario->tracker_items.data()};
    scenario->functors[side] = {.tracker = &scenario->trackers[side], .list = &scenario->list, .begin0 = scenario->begin0};
  }
  scenario->expected.resize(static_cast<size_t>(n) * stride);
  for (uint32_t k = 0; k < n; ++k) {
    std::memcpy(scenario->expected.data() + static_cast<size_t>(k) * stride, scenario->tracker_items[scenario->begin0 + k], stride);
  }
  return scenario;
}

// Side 0 (the dispatcher's) or 1 (the serial reference's) holds the expected records and the margins around them are as they were.
bool Matches(const Scenario& scenario, size_t side) {
  const uint8_t* destination = scenario.destinations[side];
  if (std::memcmp(destination + FRONT_MARGIN, scenario.expected.data(), scenario.expected.size()) != 0) return false;
  const auto untouched = [&](size_t from, size_t to) {
    return std::all_of(destination + from, destination + to, [](uint8_t byte) { return byte == SENTINEL; });
  };
  return untouched(0, FRONT_MARGIN) && untouched(FRONT_MARGIN + scenario.expected.size(), scenario.destination_bytes);
}

std::array<fill::RecordCopy, fill::COPY_COUNT> real_copies = {};

void RestoreCopies() { fi::record_copies = real_copies; }

// The game's call of a site, as the game makes it: cdecl through the site's function.
void CallSite(size_t site, fill::Range* range, Functor* functor) { fi::ENTRIES[site](range, functor, nullptr); }

// ---------------------------------------------------------------- fake record copies (thiscall: ecx = the functor, one stack argument)

constexpr uint32_t COUNTING_ITEMS = 1u << 16;
std::array<std::atomic<uint32_t>, COUNTING_ITEMS> hits;
std::mutex thread_mutex;
std::set<DWORD> hit_threads;

void BurnMicroseconds(uint32_t microseconds) {
  const uint64_t until = wp::Qpc() + wp::qpc_frequency * microseconds / 1000000;
  while (wp::Qpc() < until) _mm_pause();
}

// Counts every item it is given when its chunk is done; a chunk takes the render thread 20 us and any other thread 200 us (the other
// threads need time to wake, and a return before the join finds one of them in the middle of its chunk).
void __fastcall CountingCopy(void*, void*, fill::Range* range) {
  BurnMicroseconds(GetCurrentThreadId() == main_thread ? 20 : 200);
  for (uint32_t i = range->begin; i < range->end; ++i) hits[i].fetch_add(1);
  const std::unique_lock lock(thread_mutex);
  hit_threads.insert(GetCurrentThreadId());
}

// Writes one byte for each item, the thread's own: a copy whose bytes depend on where it runs.
void __fastcall ThreadCopy(void* functor, void*, fill::Range* range) {
  const auto* words = static_cast<const Functor*>(functor);
  for (uint32_t i = range->begin; i < range->end; ++i) {
    words->tracker->destination[static_cast<size_t>(i - words->begin0) * words->tracker->stride] = (GetCurrentThreadId() == main_thread ? 1 : 2);
  }
  BurnMicroseconds(20);
}

void UseFakeCopy(void(__fastcall* copy)(void*, void*, fill::Range*)) {
  fi::record_copies.fill(reinterpret_cast<fill::RecordCopy>(copy));
}

void ResetHits() {
  for (auto& hit : hits) hit.store(0);
  const std::unique_lock lock(thread_mutex);
  hit_threads.clear();
}

// Every item of [begin, end) was copied once and no other; true when so.
bool HitsExactly(uint32_t begin, uint32_t end) {
  for (uint32_t i = 0; i < COUNTING_ITEMS; ++i) {
    if (hits[i].load() != (i >= begin && i < end ? 1u : 0u)) return false;
  }
  return true;
}

struct GameCall {
  fill::Range* range = nullptr;
  void* functor = nullptr;
  void* manager = nullptr;
  uint32_t calls = 0;
};
GameCall game_call;

void __cdecl FakeGameFill(fill::Range* range, void* functor, void* manager) { game_call = {range, functor, manager, game_call.calls + 1}; }

// ---------------------------------------------------------------- 1. the sites

struct Row {
  uint32_t rva;
  uint32_t size;
};

// Whether a byte the sites write (`written`) or are found by lies in a range of `ranges` (rows with `.rva` and `.size`).
template <typename Ranges>
bool TouchesAny(const Ranges& ranges, bool written) {
  bool touches = false;
  fill::VisitRanges(written, [&](uint32_t rva, uint32_t size) {
    for (const auto& range : ranges) {
      touches |= twattila::effect_update::Overlaps(rva, size, range.rva, range.size);
    }
  });
  return touches;
}

void TestSites() {
  REQUIRE(fill::Prepare(reinterpret_cast<HMODULE>(patched)));
  CHECK(fi::found == fill::ALL_SITES);
  for (size_t site = 0; site < fill::SITE_COUNT; ++site) {
    const uint32_t rva = fill::SITES[site].rva;
    CHECK(fi::sites[site].at == patched + rva);
    CHECK(std::memcmp(fi::sites[site].game.data(), game + rva, fill::SITE_SIZE) == 0);
    CHECK(fi::sites[site].patch[0] == 0xE8);
    // The new call goes to the site's function, as the game's went to its wrapper.
    CHECK(rva + 5 + Read32(fi::sites[site].patch.data() + 1) + static_cast<uint32_t>(reinterpret_cast<uintptr_t>(patched)) ==
          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fi::ENTRIES[site])));
    CHECK(game[rva] == 0xE8 && rva + 5 + Read32(game + rva + 1) == fill::COPIES[static_cast<size_t>(fill::SITES[site].copy)].chain[0].from);
    CHECK(fi::game_fills[site] == reinterpret_cast<fill::GameFill>(patched + fill::COPIES[static_cast<size_t>(fill::SITES[site].copy)].chain[0].from));
  }
  CHECK(fi::record_copies[0] == reinterpret_cast<fill::RecordCopy>(patched + 0x17DAB80));
  CHECK(fi::record_copies[1] == reinterpret_cast<fill::RecordCopy>(patched + 0x17DB270));
  real_copies = fi::record_copies;
  // The wrappers the sites call are the only callers of the loops, and the two record copies are different functions.
  CHECK(fill::SITES[0].copy == fill::Copy::TRACKER_ITEMS && fill::SITES[1].copy == fill::Copy::LIST_ITEMS
        && fill::SITES[2].copy == fill::Copy::LIST_ITEMS && fill::SITES[3].copy == fill::Copy::TRACKER_ITEMS);

  // No overlap with heapfix's tables (the shared ones of engine_code.hpp), nor with the other features' sites.
  CHECK(!TouchesAny(ec::HEAPFIX_PATCHED, true) && !TouchesAny(ec::HEAPFIX_PATCHED, false));
  CHECK(!TouchesAny(ec::HEAPFIX_TIMING_HOOKS, true) && !TouchesAny(ec::HEAPFIX_TIMING_HOOKS, false));
  CHECK(!TouchesAny(ec::HEAPFIX_CHECKED, true));
  const std::array<ec::HeapfixRange, 1> over_site = {{{fill::SITES[2].rva + 4, 8}}};
  const std::array<ec::HeapfixRange, 1> over_context = {{{fill::SITES[3].rva - 3, 1}}};
  const std::array<ec::HeapfixRange, 1> over_chain = {{{0x17BA78F + 2, 1}}};
  const std::array<ec::HeapfixRange, 1> over_head = {{{0x17DB270 + 9, 4}}};
  CHECK(TouchesAny(over_site, true) && TouchesAny(over_site, false));
  CHECK(!TouchesAny(over_context, true) && TouchesAny(over_context, false));
  CHECK(!TouchesAny(over_chain, true) && TouchesAny(over_chain, false));
  CHECK(!TouchesAny(over_head, true) && TouchesAny(over_head, false));

  std::vector<Row> others;
  ec::Prepare(reinterpret_cast<HMODULE>(patched), ec::BUILDS[static_cast<size_t>(twattila::engine_code::Game::ATTILA)]);
  for (const ec::Site& site : ec::sites) {
    if (site.at != nullptr) others.push_back({static_cast<uint32_t>(site.at - patched), static_cast<uint32_t>(ec::SITE_SIZE)});
  }
  const size_t engine_rows = others.size();
  REQUIRE(ec::PrepareRecording(reinterpret_cast<HMODULE>(patched)));
  std::vector<Row> context_windows;
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
    const ec::RecordingSiteInfo& info = ec::RECORDING_INFO[index];
    const uint32_t size = (info.record_site == twattila::draw_recording::Site::COLOUR_B_STATE ? info.exit_rva - info.rva
                                                                                              : std::max<uint32_t>(info.covered, ec::SITE_SIZE));
    others.push_back({info.rva, size});
    context_windows.push_back({info.rva - info.context_offset, static_cast<uint32_t>(ec::PatternSize(info.context))});
  }
  const size_t recording_rows = others.size() - engine_rows;
  for (const auto& info : twattila::effect_update::SITES) {
    others.push_back({info.rva, static_cast<uint32_t>(twattila::effect_update::SizeOf(info))});
  }
  CHECK(engine_rows >= 20 && recording_rows == ec::RECORDING_SITE_COUNT && others.size() == engine_rows + recording_rows + twattila::effect_update::SITE_COUNT);
  CHECK(!TouchesAny(others, true) && !TouchesAny(others, false));
  // Our written bytes against the other tables' find-by windows.
  for (const fill::SiteInfo& info : fill::SITES) {
    for (const Row& window : context_windows) {
      CHECK(!(info.rva < window.rva + window.size && window.rva < info.rva + fill::SITE_SIZE));
    }
  }
  // The other tables prepared the same image: the sites are still ours to find.
  REQUIRE(fill::Prepare(reinterpret_cast<HMODULE>(patched)));
  CHECK(fi::found == fill::ALL_SITES);

  // No branch of the game into a site, anywhere in the code (the short forms too), and the 2 KB scan Prepare makes finds none.
  const auto sections = tw::engine_tweaks::ModuleSections(reinterpret_cast<HMODULE>(patched));
  const std::vector<uint32_t> relocated = twattila::effect_update::internal::RelocatedWords(patched);
  std::vector<uint32_t> hits_in_code;
  for (const std::string_view memory : sections.code) {
    const auto* begin = reinterpret_cast<const uint8_t*>(memory.data());
    fill::BranchesIntoSites({begin, memory.size()}, static_cast<uint32_t>(begin - patched), relocated, true, &hits_in_code);
  }
  std::printf("branches into the sites over the whole code: %zu\n", hits_in_code.size());
  for (const uint32_t source : hits_in_code) std::printf("  from +%X\n", source);
  CHECK(hits_in_code.empty());

  // Prepare refuses each broken byte, and finds nothing then; a jump into a site (from outside it) too.
  const auto refuses = [&](uint32_t rva, uint8_t xor_mask, const char* what) {
    patched[rva] ^= xor_mask;
    const bool prepared = fill::Prepare(reinterpret_cast<HMODULE>(patched));
    patched[rva] ^= xor_mask;
    if (prepared || fi::found != 0) {
      std::printf("FAIL: Prepare accepted %s\n", what);
      ++failures;
    }
  };
  for (size_t site = 0; site < fill::SITE_COUNT; ++site) {
    const fill::SiteInfo& info = fill::SITES[site];
    refuses(info.rva - info.before, 0x01, "a changed byte before the call");
    refuses(info.rva + 1, 0x10, "a call to another function");
    refuses(info.rva + 5 + 2, 0x01, "a changed byte after the call");
    refuses(info.rva, 0x01, "no call");
  }
  for (const fill::CopyInfo& copy : fill::COPIES) {
    for (const fill::Link& link : copy.chain) refuses(link.from + 1, 0x01, "a changed link of the chain");
    refuses(copy.chain[2].to + 1, 0x01, "a changed record copy head");
  }
  const uint32_t jump_at = fill::SITES[1].rva + 0x40;
  const std::array<uint8_t, 5> saved = {patched[jump_at], patched[jump_at + 1], patched[jump_at + 2], patched[jump_at + 3], patched[jump_at + 4]};
  patched[jump_at] = 0xE9;
  Write32(patched + jump_at + 1, fill::SITES[1].rva + 2 - (jump_at + 5));
  CHECK(!fill::Prepare(reinterpret_cast<HMODULE>(patched)) && fi::found == 0);
  std::memcpy(patched + jump_at, saved.data(), saved.size());
  REQUIRE(fill::Prepare(reinterpret_cast<HMODULE>(patched)));
  CHECK(fi::found == fill::ALL_SITES);
  std::printf("sites: found, clear of heapfix and of %zu other sites (%zu engine code, %zu recording, %zu effect update)\n", others.size(),
              engine_rows, recording_rows, twattila::effect_update::SITE_COUNT);
}

void TestRewrite() {
  const auto bytes_of = [&](size_t site, bool patched_bytes) {
    return std::memcmp(patched + fill::SITES[site].rva, (patched_bytes ? fi::sites[site].patch : fi::sites[site].game).data(),
                       fill::SITE_SIZE) == 0;
  };
  // The 4 sites share one page (with 6 recording sites): Rewrite makes it writable once per site and must leave it as it found
  // it, which it does only if the protections go back in the reverse order (a later site saw the earlier ones' writable state as
  // its old protection). Every call runs on a read and execute page, and the page is checked after it; the test's own edits
  // need it writable in between.
  uint8_t* const page = patched + (fill::SITES[0].rva & ~0xFFFu);
  for (const auto& info : fill::SITES) {
    CHECK((info.rva & ~0xFFFu) == (fill::SITES[0].rva & ~0xFFFu) && (info.rva + fill::SITE_SIZE - 1 & ~0xFFFu) == (info.rva & ~0xFFFu));
  }
  const auto protect_page = [&](DWORD protection) {
    DWORD ignored;
    return VirtualProtect(page, 0x1000, protection, &ignored) != FALSE;
  };
  const auto rewrite = [&](uint32_t to) {
    REQUIRE(protect_page(PAGE_EXECUTE_READ));
    const uint32_t stood = fill::Rewrite(to);
    MEMORY_BASIC_INFORMATION info = {};
    REQUIRE(VirtualQuery(page, &info, sizeof(info)) != 0);
    if (info.Protect != PAGE_EXECUTE_READ) {
      std::printf("FAIL: the page is %lX after Rewrite(%X), not read and execute\n", info.Protect, to);
      ++failures;
    }
    REQUIRE(protect_page(PAGE_EXECUTE_READWRITE));
    return stood;
  };
  // What Update does with a Rewrite's answer: nothing is ours any more (`written` 0), and the sites that stood down leave `found`.
  const auto stand_down = [&](uint32_t stood) {
    fi::written = 0;
    fi::found &= ~stood;
    CHECK(fill::Wanted(true) == 0);
  };
  const auto find_again = [&] {
    REQUIRE(fill::Prepare(reinterpret_cast<HMODULE>(patched)));
    CHECK(fi::found == fill::ALL_SITES && fill::Wanted(true) == fill::ALL_SITES);
  };
  CHECK(fill::Changing(fill::ALL_SITES) == fill::ALL_SITES && fill::Changing(0) == 0);
  CHECK(rewrite(fill::ALL_SITES) == 0);
  fi::written = fill::ALL_SITES;
  for (size_t site = 0; site < fill::SITE_COUNT; ++site) {
    CHECK(bytes_of(site, true));
  }
  CHECK(fill::Changing(fill::ALL_SITES) == 0 && fill::Changing(0) == fill::ALL_SITES);
  // Out again: the game's own bytes.
  CHECK(rewrite(0) == 0);
  fi::written = 0;
  for (size_t site = 0; site < fill::SITE_COUNT; ++site) {
    CHECK(bytes_of(site, false));
  }
  CHECK(std::memcmp(patched + 0x17AE700, game + 0x17AE700, 0x20) == 0 && std::memcmp(patched + 0x17AED00, game + 0x17AED00, 0x40) == 0);

  // A site that is another tool's: every site stands down going in, and nothing is written.
  patched[fill::SITES[2].rva + 1] ^= 0x04;
  const uint32_t stood_in = rewrite(fill::ALL_SITES);
  CHECK(stood_in == fill::ALL_SITES);
  patched[fill::SITES[2].rva + 1] ^= 0x04;
  for (size_t site = 0; site < fill::SITE_COUNT; ++site) {
    CHECK(bytes_of(site, false));
  }
  stand_down(stood_in);
  find_again();
  // Going out: only the changed site stays (it is the other tool's), the others come out.
  CHECK(rewrite(fill::ALL_SITES) == 0);
  fi::written = fill::ALL_SITES;
  patched[fill::SITES[1].rva + 2] ^= 0x04;
  const uint32_t stood_out = rewrite(0);
  CHECK(stood_out == 1u << 1);
  patched[fill::SITES[1].rva + 2] ^= 0x04;
  for (size_t site = 0; site < fill::SITE_COUNT; ++site) {
    CHECK(bytes_of(site, site == 1));
  }
  stand_down(stood_out);
  // The other tool's bytes stay there; the game's come back for the rest of the test.
  std::memcpy(patched + fill::SITES[1].rva, fi::sites[1].game.data(), fill::SITE_SIZE);
  find_again();
  for (size_t site = 0; site < fill::SITE_COUNT; ++site) {
    CHECK(bytes_of(site, false));
  }

  // A thread is inside a site only past its first byte.
  const auto start = reinterpret_cast<uintptr_t>(patched + fill::SITES[3].rva);
  CHECK(!fill::ThreadInside(start, 1u << 3) && fill::ThreadInside(start + 1, 1u << 3) && fill::ThreadInside(start + 4, 1u << 3));
  CHECK(!fill::ThreadInside(start + 5, 1u << 3) && !fill::ThreadInside(start + 1, 1u << 2));
}

// ---------------------------------------------------------------- 2. the fills

constexpr std::array<uint32_t, 13> COUNTS = {1, 63, 64, 65, 511, 512, 513, 767, 768, 769, 1000, 4097, 20000};
constexpr std::array<uint32_t, 6> STRIDES = {16, 48, 64, 100, 128, 176};
constexpr std::array<uint32_t, 4> BEGINS = {0, 1, 37, 1000};

// The dispatcher against the game's record copy run serially on the whole range, then the bytes against the test's own copy.
bool RunExact(size_t site, uint32_t n, uint32_t stride, uint32_t begin0, bool write_combined, std::mt19937* random) {
  const auto scenario = Build(site, n, stride, begin0, write_combined, random);
  fill::Range serial = scenario->Range();
  real_copies[static_cast<size_t>(fill::SITES[site].copy)](&scenario->functors[1], &serial);
  fill::Range range = scenario->Range();
  CallSite(site, &range, &scenario->functors[0]);
  return range.begin == scenario->Range().begin && range.end == scenario->Range().end && Matches(*scenario, 0) && Matches(*scenario, 1);
}

void TestFills() {
  std::mt19937 random(12345);
  size_t runs = 0;
  size_t bad = 0;
  fi::row_on.store(true);
  for (uint32_t threads = 1; threads <= 8; ++threads) {
    UpdatePool(threads);
    for (size_t site = 0; site < fill::SITE_COUNT; ++site) {
      for (const uint32_t n : COUNTS) {
        for (const bool write_combined : {false, true}) {
          const uint32_t stride = STRIDES[random() % STRIDES.size()];
          const uint32_t begin0 = BEGINS[random() % BEGINS.size()];
          ++runs;
          if (!RunExact(site, n, stride, begin0, write_combined, &random)) {
            ++bad;
            std::printf("FAIL: threads %u site %zu n %u stride %u begin0 %u%s\n", threads, site, n, stride, begin0, write_combined ? " (write-combined)" : "");
          }
        }
      }
    }
  }
  CHECK(bad == 0);
  std::printf("fills: %zu runs (threads 1..8, n 1..20000, 4 sites, cached and write-combined), %zu different\n", runs, bad);
  // Random sizes and begin0s on 4 threads.
  size_t random_bad = 0;
  UpdatePool(4);
  for (int run = 0; run < 150; ++run) {
    const size_t site = random() % fill::SITE_COUNT;
    random_bad += (RunExact(site, 1 + random() % 6000, STRIDES[random() % STRIDES.size()], random() % 3000, (random() & 1) != 0, &random) ? 0 : 1);
  }
  CHECK(random_bad == 0);
  CHECK(fi::incomplete_fills.load() == 0);
  std::printf("fills: 150 random runs, %zu different\n", random_bad);
}

// ---------------------------------------------------------------- 3. the partition and the paths

struct Count {
  uint32_t n;
  bool other_threads;
};

// One call with the counting copy on the 4 sites; every item once, and at n >= 2000 with workers more than one thread.
Count RunCounting(size_t site, uint32_t n, uint32_t begin0) {
  ResetHits();
  UseFakeCopy(&CountingCopy);
  const auto scenario = Build(site, n, 16, begin0, false, &shared_random);
  fill::Range range = scenario->Range();
  CallSite(site, &range, &scenario->functors[0]);
  RestoreCopies();
  const std::unique_lock lock(thread_mutex);
  return {.n = n, .other_threads = hit_threads.size() > 1};
}

void TestPartition() {
  UpdatePool(5);
  bool any_workers = false;
  for (size_t site = 0; site < fill::SITE_COUNT; ++site) {
    for (const uint32_t n : {1u, 100u, 511u, 512u, 513u, 1000u, 2000u, 20000u}) {
      const uint32_t begin0 = (fill::SITES[site].copy == fill::Copy::LIST_ITEMS ? 3000 : 0);
      const Count count = RunCounting(site, n, begin0);
      CHECK(HitsExactly(begin0, begin0 + n));
      CHECK(n < fill::FILL_MIN_ITEMS ? !count.other_threads : (n < 20000 || count.other_threads));
      any_workers |= count.other_threads;
    }
  }
  CHECK(any_workers);

  // The runs that stay alone: the row off (the game's own call, with the game's arguments), below the minimum, no worker free (one
  // thread), another thread, inside a job (the render thread's own part and a worker's).
  auto scenario = Build(1, 5000, 16, 7, false, &shared_random);
  fill::Range range = scenario->Range();
  const auto original = fi::game_fills;
  fi::game_fills.fill(&FakeGameFill);
  fill::Wanted(false);
  game_call = {};
  CallSite(1, &range, &scenario->functors[0]);
  CHECK(game_call.calls == 1 && game_call.range == &range && game_call.functor == &scenario->functors[0] && game_call.manager == nullptr);
  CHECK(std::all_of(scenario->destinations[0], scenario->destinations[0] + scenario->destination_bytes, [](uint8_t byte) { return byte == SENTINEL; }));
  fi::game_fills = original;
  CHECK(fill::Wanted(true) == fill::ALL_SITES);
  CHECK(fi::row_on.load());

  const auto alone_equal = [&](size_t site, uint32_t n, const char* what) {
    ResetHits();
    UseFakeCopy(&CountingCopy);
    auto next = Build(site, n, 16, 9, false, &shared_random);
    fill::Range next_range = next->Range();
    CallSite(site, &next_range, &next->functors[0]);
    RestoreCopies();
    const std::unique_lock lock(thread_mutex);
    if (!HitsExactly(next->begin0, next->begin0 + n) || hit_threads.size() != 1) {
      std::printf("FAIL: %s\n", what);
      ++failures;
    }
  };
  alone_equal(2, 100, "below the minimum");
  UpdatePool(1);
  alone_equal(2, 20000, "no worker free");
  UpdatePool(5);
  // Another thread: it is not the pool's owner.
  std::thread([&] { alone_equal(2, 20000, "another thread"); }).join();
  // Inside a job: the owner's part (the pool is busy) and a worker's part, one after the other (the hit counts are global).
  const wp::Job idle_job = {.work = [](void*, uint32_t) {}, .context = nullptr, .user = wp::User::EFFECT_UPDATE, .join = wp::JoinRule::ALL};
  wp::Run(idle_job, 1, [&] { alone_equal(2, 20000, "inside a job, the render thread's part"); });
  const wp::Job nested_job = {.work = [](void*, uint32_t) {
                                ResetHits();
                                UseFakeCopy(&CountingCopy);
                                auto nested = Build(1, 20000, 16, 5, false, &shared_random);
                                fill::Range nested_range = nested->Range();
                                CallSite(1, &nested_range, &nested->functors[0]);
                                RestoreCopies();
                                const std::unique_lock lock(thread_mutex);
                                if (!HitsExactly(nested->begin0, nested->begin0 + 20000) || hit_threads.size() != 1) ++failures;
                              },
                              .context = nullptr,
                              .user = wp::User::EFFECT_UPDATE,
                              .join = wp::JoinRule::ALL};
  wp::Run(nested_job, 1, [] {});

  // Trace: one window of one present.
  wp::ArmTrace(1);
  UpdatePool(5);
  CHECK(wp::Tracing());
  fi::trace_count.store(0);
  {
    const auto big = Build(1, 6000, 64, 11, false, &shared_random);
    fill::Range big_range = big->Range();
    CallSite(1, &big_range, &big->functors[0]);
    const auto few = Build(0, 100, 64, 0, false, &shared_random);
    fill::Range few_range = few->Range();
    CallSite(0, &few_range, &few->functors[0]);
    // No worker free: inside a job.
    const auto busy = Build(3, 5000, 64, 0, false, &shared_random);
    fill::Range busy_range = busy->Range();
    wp::Run(idle_job, 1, [&] { CallSite(3, &busy_range, &busy->functors[0]); });
  }
  CHECK(fi::trace_count.load() == 3);
  const fi::TraceRecord big = fi::trace_records[0];
  CHECK(big.site == 1 && big.count == 6000 && big.asked == 4 && big.chunks == 94 && big.chunks_done == 94 && big.entered <= big.asked);
  CHECK(fi::trace_records[1].asked == 0 && !fi::trace_records[1].pool_busy);
  CHECK(fi::trace_records[2].asked == 0 && fi::trace_records[2].pool_busy);
  log_lines.clear();
  fill::Wanted(true);
  size_t asked_lines = 0;
  size_t alone_lines = 0;
  for (const std::string& line : log_lines) {
    asked_lines += (line.find("Parallel Instance Fill (trace window") != std::string::npos && line.find("workers asked") != std::string::npos);
    alone_lines += (line.find("Parallel Instance Fill (trace window") != std::string::npos && line.find("alone (") != std::string::npos);
  }
  CHECK(asked_lines == 1 && alone_lines == 2);
  CHECK(fi::incomplete_fills.load() == 0);
  UpdatePool(5);
  std::printf("paths: counting copy, alone paths and the trace: ok\n");
}

// ---------------------------------------------------------------- 4. controls and the check mode

enum class Where { SAME_THREAD, JOIN_AFTER, OTHER_THREAD };

// Runs the counting copy on the 4 sites with the control set: true when anything but every item once was seen (after the call
// returned; JOIN_AFTER joins the job the early return left open, OTHER_THREAD makes the call from another thread).
bool CountingSeesDifference(bool* control, Where where) {
  UpdatePool(4);
  bool different = false;
  *control = true;
  for (size_t site = 0; site < fill::SITE_COUNT; ++site) {
    const uint32_t begin0 = (fill::SITES[site].copy == fill::Copy::LIST_ITEMS ? 3000 : 0);
    ResetHits();
    UseFakeCopy(&CountingCopy);
    const auto scenario = Build(site, 6000, 16, begin0, false, &shared_random);
    fill::Range range = scenario->Range();
    if (where == Where::OTHER_THREAD) {
      std::thread([&] { CallSite(site, &range, &scenario->functors[0]); }).join();
    } else {
      CallSite(site, &range, &scenario->functors[0]);
    }
    different |= !HitsExactly(begin0, begin0 + 6000);
    if (where == Where::JOIN_AFTER) wp::Join();
    RestoreCopies();
  }
  *control = false;
  return different;
}

// The 4 sites with the real copies, 3000 items from a begin0 of 500 on 4 threads: every one exact.
bool AllExact() {
  UpdatePool(4);
  std::mt19937 random(99);
  bool exact = true;
  for (size_t site = 0; site < fill::SITE_COUNT; ++site) {
    exact &= RunExact(site, 3000, 64, 500, false, &random);
  }
  return exact;
}

#if defined(TWATTILA_INSTANCE_FILL_CONTROLS) && defined(TWATTILA_WORKER_POOL_CONTROLS)
struct LateRun {
  double milliseconds;      // how long the call took
  bool exact_after_call;    // every item once when it returned
  bool exact_after_late;    // and still once after the late worker was let go
  uint32_t late_parked;     // the times the late worker was held
};

// A fill of 6000 items on 4 threads with worker 1 held between waking and counting itself in, and let go after `hold_ms`.
LateRun RunWithLateWorker(uint32_t hold_ms) {
  UpdatePool(4);
  ResetHits();
  UseFakeCopy(&CountingCopy);
  const auto scenario = Build(1, 6000, 16, 0, false, &shared_random);
  fill::Range range = scenario->Range();
  wp::internal::controls.late_parked.store(0);
  wp::internal::controls.late_worker = 1;
  wp::internal::controls.release_late.store(false);
  std::thread releaser([&] {
    Sleep(hold_ms);
    wp::internal::controls.release_late.store(true);
  });
  const auto before = std::chrono::steady_clock::now();
  CallSite(1, &range, &scenario->functors[0]);
  LateRun run = {};
  run.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
  run.exact_after_call = HitsExactly(0, 6000);
  releaser.join();
  Sleep(100);  // the late worker finds the job closed and skips it
  run.exact_after_late = HitsExactly(0, 6000);
  run.late_parked = wp::internal::controls.late_parked.load();
  wp::internal::controls.late_worker = 0;
  RestoreCopies();
  return run;
}
#endif

void TestControls() {
#if defined(TWATTILA_INSTANCE_FILL_CONTROLS)
  // Nothing set: no difference.
  CHECK(AllExact());
  CHECK(CountingSeesDifference(&fill::control_overlap, Where::SAME_THREAD));
  CHECK(CountingSeesDifference(&fill::control_drop_last, Where::SAME_THREAD));
  CHECK(CountingSeesDifference(&fill::control_early_return, Where::JOIN_AFTER));
  CHECK(CountingSeesDifference(&fill::control_refused_skips_part, Where::OTHER_THREAD));
  // A chunk claimed and not copied: the dispatcher sees the chunks not done, counts the fill and says so at the next present.
  {
    fi::incomplete_fills.store(0);
    CHECK(CountingSeesDifference(&fill::control_skip_chunk, Where::SAME_THREAD));
    CHECK(fi::incomplete_fills.load() == fill::SITE_COUNT);
    log_lines.clear();
    fill::Wanted(true);
    size_t said_not_done = 0;
    for (const std::string& line : log_lines) {
      said_not_done += (line.find("4 fills ended with chunks not done") != std::string::npos);
    }
    CHECK(said_not_done == 1 && fi::incomplete_fills.load() == 0);
  }
#if defined(TWATTILA_WORKER_POOL_CONTROLS)
  // The join waits only for the workers that entered the job: a worker still asleep when the fill is done is left behind, and
  // skips the closed job when it wakes (the late worker, held for 400 ms here). Joining every woken worker waits for it.
  {
    const LateRun entered = RunWithLateWorker(400);
    CHECK(entered.milliseconds < 200 && entered.exact_after_call && entered.exact_after_late && entered.late_parked == 1);
    fill::control_all_join = true;
    const LateRun all = RunWithLateWorker(400);
    fill::control_all_join = false;
    CHECK(all.milliseconds >= 300 && all.exact_after_call && all.exact_after_late);
    CHECK(AllExact());
  }
#endif
  fill::control_wrong_copy = true;
  CHECK(!AllExact());
  fill::control_wrong_copy = false;
  fill::control_no_begin0 = true;
  CHECK(!AllExact());
  fill::control_no_begin0 = false;
  CHECK(AllExact());
  std::printf("controls: each broken rule is seen\n");
#else
  std::printf("controls: skipped (TWATTILA_INSTANCE_FILL_CONTROLS)\n");
#endif
}

void TestCheckMode() {
  UpdatePool(4);
  fi::check_enabled = true;
  fi::checked.store(0);
  fi::check_mismatches.store(0);
  fi::check_skipped.store(0);
  std::mt19937 random(7);
  for (size_t site = 0; site < fill::SITE_COUNT; ++site) {
    CHECK(RunExact(site, 3000, 64, 12, false, &random));
  }
  CHECK(fi::checked.load() == 4 && fi::check_mismatches.load() == 0 && fi::check_skipped.load() == 0);
  // A copy whose bytes depend on its thread: the dispatched fill differs from the serial one, and the check says so.
  UseFakeCopy(&ThreadCopy);
  log_lines.clear();
  {
    const auto scenario = Build(1, 20000, 16, 5, false, &random);
    fill::Range range = scenario->Range();
    CallSite(1, &range, &scenario->functors[0]);
  }
  RestoreCopies();
  CHECK(fi::check_mismatches.load() >= 1);
  bool said = false;
  for (const std::string& line : log_lines) said |= (line.find("Parallel Instance Fill check") != std::string::npos);
  CHECK(said);
  // A fill whose range does not start at begin0 is not compared (the scratch memory would be too small).
  {
    const auto scenario = Build(1, 3000, 16, 5, false, &random);
    scenario->functors[0].begin0 = 4;
    fill::Range range = scenario->Range();
    CallSite(1, &range, &scenario->functors[0]);
  }
  CHECK(fi::check_skipped.load() == 1);
  fi::check_enabled = false;
  std::printf("check mode: equal fills pass, a thread-dependent copy is seen\n");
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
  game = Map(&image_size);
  patched = Map(&image_size);
  REQUIRE(game != nullptr && patched != nullptr);
  main_thread = GetCurrentThreadId();
  std::printf("empire.retail.dll build %08lX, %zu bytes: mapped twice (relocated, never loaded)\n", nt->FileHeader.TimeDateStamp, file.size());

  UpdatePool(2);  // this thread is the pool's owner from here on
  TestSites();
  TestRewrite();
  TestFills();
  TestPartition();
  TestControls();
  TestCheckMode();
  wp::Update({.threads = 1, .in_use = false});
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}

#endif
