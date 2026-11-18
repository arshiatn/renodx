// ====================================================================================================================
// 20. The pending list sweep's chain prefetch (Attila +0x1893290, loop top +0x18932b1, Thrones +0x19085f0; Faster Sweep Prefetch; engine_code.hpp 16.),
// included inside tests.cpp's anonymous namespace after item_push.inc (LoopCall, EnterParticleLoop, ParticleLoopReturn, JumpTo, HoldSites, ParkedAtSites, Random,
// PushWordsEqual of the earlier parts are used). The sweep (the render thread's, once a frame) walks a vector of {list object, control block} from the last entry
// down, asks the game's ready check (+0x18635a0) about each list object and erases a ready entry by moving the last one in; the stub at the loop's top (the
// `mov eax, [esi + 0x1c]; mov ecx, [eax + ebx*8]` the jump replaces) runs both after three stages: for the entries A, B and C places below ebx that are inside the
// vector right now (unsigned `ebx - d < count`) it prefetches the list object (A), loads its children array pointer and prefetches the array (B, a null list
// object skipped), loads the first child's pointer (the count first: 0 skipped) and prefetches the child's ready byte (C). The loop is entered by itself on a fake
// frame (the saved edi, ebx, esi and the return address above esp as the function's three pushes leave them), its way out at +0x1893325 hooked to the harness, and
// runs the game's own ready check and release code in both copies of the DLL:
// 20a. the site and the stub as Prepare made them (jumps, bytes);
// 20b. whole sweeps, both copies: random vectors of 1 to 28 entries (null list objects, lists without children whose array pointer is garbage, children not
//    ready, ready ones erased by the swap, blocks with use counts 1 to 3 so that the release's dispose and destroy run), the vector at the start or the end of
//    a page between no-access pages, the lists' children arrays or the list objects packed to a page's end: every register but eax / ecx / edx (the stub's), the
//    flags, esp, xmm, the frame above esp and every byte of the world and the count of dispose and destroy calls equal, or both copies fault with the same memory.
//    With the game's ready check in both (the stub's early stop reads fewer addresses than the game's check, which a corrupt child makes a difference
//    outside the sweep's) for the corrupt children and blocks;
// 20c. CONTROLs: mutants of the stub, each must be seen;
// 20d. 200 switches under 4 threads that run whole sweeps in both copies, a parked thread at each byte of the site;
// 20e. timing: 4,000 lists of 1 to 4 and of 5 to 8 children over 512 MB, flushed from the caches before each sweep: ns a list for the game / the ready check's
//    stub / the stub plus the chain with (A, B, C) = (8, 4, 2) (the page's), (4, 2, 1), (6, 3, 1).
// To reproduce a failing scenario: SWEEP_SEED (hex), SWEEP_FIRST / SWEEP_LAST run 20b's range alone, SWEEP_EXPLAIN prints what differs.
// ====================================================================================================================

constexpr uint32_t SWEEP_LISTS = 28;
constexpr uint32_t SWEEP_KIDS = 10;
constexpr uint32_t SWEEP_LOCALS = 0x100;
constexpr uint32_t SWEEP_EDGE_BYTES = 0x1000;
constexpr uint32_t SWEEP_LIST_BYTES = 0x48;  // a list object's fields: [+4] the flag byte, [+0x40] the count, [+0x44] the children array

thread_local uint32_t sweep_counter = 0;  // the fake control block's dispose (+1) and destroy (+0x10000)
void __fastcall SweepDispose(void*, void*) { sweep_counter += 1; }
void __fastcall SweepDestroy(void*, void*) { sweep_counter += 0x10000; }
const std::array<uint32_t, 2> SWEEP_VTABLE = {Address(reinterpret_cast<const void*>(&SweepDispose)), Address(reinterpret_cast<const void*>(&SweepDestroy))};

struct SweepWorld {
  LoopCall call;
  alignas(16) uint8_t self[0x40];  // `this`: +0x18 the count, +0x1c the entries
  uint32_t disposed;               // sweep_counter after the run
  uint32_t unused[3];
  uint32_t entries[2 * SWEEP_LISTS + 2];  // {list object, control block}
  alignas(16) uint8_t lists[SWEEP_LISTS][0x50];
  uint32_t kids[SWEEP_LISTS][2 * SWEEP_KIDS];  // {child object, control block}
  alignas(16) uint8_t objects[SWEEP_LISTS * SWEEP_KIDS][0x80];
  uint32_t blocks[SWEEP_LISTS * (SWEEP_KIDS + 1)][4];  // {vtable, uses, weaks, unused}
  alignas(16) uint8_t edges[3][SWEEP_EDGE_BYTES];       // the pages between no-access pages, as the run starts and ends
};

struct SweepEdges {
  std::array<uint8_t*, 3> page;
};

SweepEdges SweepAllocEdges() {
  SweepEdges edges = {};
  for (uint8_t*& page : edges.page) {
    auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, 3 * SWEEP_EDGE_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    DWORD protection;
    VirtualProtect(base, SWEEP_EDGE_BYTES, PAGE_NOACCESS, &protection);
    VirtualProtect(base + 2 * SWEEP_EDGE_BYTES, SWEEP_EDGE_BYTES, PAGE_NOACCESS, &protection);
    page = base + SWEEP_EDGE_BYTES;
  }
  return edges;
}
void SweepFreeEdges(const SweepEdges& edges) {
  for (uint8_t* page : edges.page) VirtualFree(page - SWEEP_EDGE_BYTES, 0, MEM_RELEASE);
}

enum SweepLayout : uint32_t {
  SWEEP_ENTRIES_FRONT = 1,  // the vector's first entry at the start of a page (a no-access page before it)
  SWEEP_KIDS_END = 2,       // the children arrays packed to the end of a page
  SWEEP_LISTS_END = 4,      // the list objects packed to the end of a page
  SWEEP_ENTRIES_END = 8,    // the vector's last entry at the end of a page
};

struct SweepSpec {
  uint32_t entries;
  uint32_t start;
  uint32_t layout;
  bool corrupt;  // garbage and null children and blocks: the game's ready check in both copies
  uint32_t shift;
};

SweepSpec SweepRandomSpec(Random* random, bool corrupt) {
  SweepSpec spec = {.corrupt = corrupt};
  const uint32_t pick = random->Next() % 100;
  spec.entries = (pick < 30 ? 1 + random->Next() % 4 : (pick < 80 ? 5 + random->Next() % 10 : 15 + random->Next() % (SWEEP_LISTS - 14)));
  spec.start = (random->Next() % 100 < 85 ? spec.entries - 1 : random->Next() % spec.entries);
  const uint32_t layout = random->Next() % 100;
  spec.layout = (layout < 40 ? 0 : (layout < 55 ? SWEEP_ENTRIES_FRONT : (layout < 65 ? SWEEP_ENTRIES_END : 0)));
  if (random->Next() % 100 < 35) spec.layout |= SWEEP_KIDS_END;
  if (random->Next() % 100 < 35) spec.layout |= SWEEP_LISTS_END;
  spec.shift = random->Next();
  return spec;
}

thread_local SweepWorld* sweep_world_now = nullptr;
thread_local SweepSpec sweep_spec_now = {};

// The world as the loop's top sees it. `edges`: the thread's pages (the snapshot in the world goes there before the run), `frame_esp` the run's esp.
void SweepSetup(SweepWorld* world, const SweepEdges& edges, const SweepSpec& spec, Random* random) {
  std::memset(world->self, 0, sizeof(SweepWorld) - offsetof(SweepWorld, self));
  uint8_t* esp = world->call.frame + SWEEP_LOCALS;
  for (uint32_t at = 0; at < SWEEP_LOCALS; at += 4) Write32(esp + at, random->Next());
  const uint32_t n = spec.entries;
  std::array<uint32_t, SWEEP_LISTS> kids = {};
  std::array<bool, SWEEP_LISTS> null_list = {};
  uint32_t kids_total = 0;
  for (uint32_t list = 0; list < n; ++list) {
    const uint32_t pick = random->Next() % 100;
    kids[list] = (pick < 15 ? 0 : (pick < 65 ? 1 + random->Next() % 3 : 4 + random->Next() % 6));
    kids_total += kids[list];
    null_list[list] = (random->Next() % 100 < 3);
  }
  // Where each thing is: the write pointer into the world (the snapshot of a page) and the address the game sees.
  const auto page_at = [&](size_t page, uint32_t offset) { return std::pair<uint8_t*, uint32_t>(world->edges[page] + offset, Address(edges.page[page]) + offset); };
  uint32_t kids_after = kids_total;  // children from list `list` on
  for (uint32_t list = 0; list < n; ++list) {
    uint8_t* list_w = world->lists[list];
    uint32_t list_at = Address(list_w);
    if ((spec.layout & SWEEP_LISTS_END) != 0) std::tie(list_w, list_at) = page_at(2, SWEEP_EDGE_BYTES - SWEEP_LIST_BYTES - 0x50 * (n - 1 - list));
    uint32_t kids_w_at = Address(world->kids[list]);
    uint8_t* kids_w = reinterpret_cast<uint8_t*>(world->kids[list]);
    if ((spec.layout & SWEEP_KIDS_END) != 0) std::tie(kids_w, kids_w_at) = page_at(1, SWEEP_EDGE_BYTES - 8 * kids_after);
    const uint32_t count = kids[list];
    const uint32_t ready_kind = random->Next() % 3;  // all ready / mostly / half
    for (uint32_t kid = 0; kid < count; ++kid) {
      uint8_t* object = world->objects[list * SWEEP_KIDS + kid];
      for (uint32_t at = 0; at < 0x80; at += 4) Write32(object + at, random->Next());
      const bool ready = (ready_kind == 0 || random->Next() % 100 < (ready_kind == 1 ? 90u : 45u));
      object[0x6E] = (ready ? static_cast<uint8_t>(1 + random->Next() % 255) : 0);
      uint32_t object_pointer = Address(object);
      uint32_t* block = world->blocks[list * (SWEEP_KIDS + 1) + 1 + kid];
      block[0] = Address(SWEEP_VTABLE.data());
      block[1] = 1 + random->Next() % 3;
      block[2] = 1 + random->Next() % 2;
      uint32_t block_pointer = Address(block);
      if (spec.corrupt) {
        const uint32_t corrupt = random->Next() % 100;
        if (corrupt < 4) object_pointer = 0x80000000u | random->Next();  // unmapped
        else if (corrupt < 6) object_pointer = 0;
        if (random->Next() % 100 < 12) block_pointer = 0;
      }
      Write32(kids_w + 8 * kid, object_pointer);
      Write32(kids_w + 8 * kid + 4, block_pointer);
    }
    kids_after -= count;
    // The list object: the flag byte, the count, the array (garbage when there is no child: the game never reads it).
    for (uint32_t at = 0; at < SWEEP_LIST_BYTES; at += 4) Write32(list_w + at, random->Next());
    list_w[4] = (random->Next() % 100 < 85 ? static_cast<uint8_t>(1 + random->Next() % 255) : 0);
    Write32(list_w + 0x40, count);
    Write32(list_w + 0x44, count != 0 || (spec.layout & SWEEP_KIDS_END) != 0 ? kids_w_at : (0x80000000u | random->Next()));
    // The entry.
    uint32_t* entry_block = world->blocks[list * (SWEEP_KIDS + 1)];
    entry_block[0] = Address(SWEEP_VTABLE.data());
    entry_block[1] = 1 + random->Next() % 3;
    entry_block[2] = 1 + random->Next() % 2;
    world->entries[2 * list] = (null_list[list] ? 0 : list_at);
    world->entries[2 * list + 1] = (random->Next() % 100 < 12 ? 0u : Address(entry_block));
  }
  // The vector itself.
  uint32_t entries_at = Address(world->entries);
  if ((spec.layout & SWEEP_ENTRIES_FRONT) != 0) {
    const auto [at_w, at] = page_at(0, 0);
    std::memcpy(at_w, world->entries, 8 * n);
    entries_at = at;
  } else if ((spec.layout & SWEEP_ENTRIES_END) != 0) {
    const auto [at_w, at] = page_at(0, SWEEP_EDGE_BYTES - 8 * n);
    std::memcpy(at_w, world->entries, 8 * n);
    entries_at = at;
  }
  for (uint32_t at = 0; at < sizeof(world->self); at += 4) Write32(world->self + at, random->Next());
  Write32(world->self + 0x18, n);
  Write32(world->self + 0x1C, entries_at);
  for (uint32_t& value : world->call.in) value = random->Next();
  world->call.in[3] = spec.start;
  world->call.in[4] = Address(world->self);
  world->call.flags_in = (random->Next() & 0x8D5) | 2;
  world->disposed = 0;
}

// Scenario `index` of the seed made in `world`.
SweepSpec SweepScenario(SweepWorld* world, const SweepEdges& edges, uint64_t seed, uint64_t index, bool corrupt) {
  Random random = {.state = (seed + index) * 0x9E3779B97F4A7C15ull | 1};
  for (int warm = 0; warm < 4; ++warm) random.Next();
  const SweepSpec spec = SweepRandomSpec(&random, corrupt);
  sweep_spec_now = spec;
  sweep_world_now = world;
  SweepSetup(world, edges, spec, &random);
  return spec;
}

// "" if the two runs agree: every register but eax, ecx and edx (the stub's), esp, the flags and xmm where both finished, the frame, the world and the count of
// the fakes' calls.
const char* SweepCompare(const SweepWorld& a, const SweepWorld& b, bool a_done, bool b_done, uint32_t copy_distance) {
  if (a_done != b_done) return "ONE of the copies faulted";
  if (a_done) {
    for (size_t reg = 3; reg < 7; ++reg) {
      if (a.call.out[reg] != b.call.out[reg]) return "a register (ebx esi edi ebp)";
    }
    if (a.call.esp_after != b.call.esp_after) return "esp";
    if (a.call.flags_out != b.call.flags_out) return "the flags";
    if (std::memcmp(a.call.xmm_out, b.call.xmm_out, sizeof(a.call.xmm_out)) != 0) return "xmm";
  }
  if (!PushWordsEqual(a.call.frame + SWEEP_LOCALS, b.call.frame + SWEEP_LOCALS, SWEEP_LOCALS, copy_distance)) return "the frame above esp";
  if (!PushWordsEqual(a.self, b.self, sizeof(SweepWorld) - offsetof(SweepWorld, self), copy_distance)) return "memory (the vector, the lists, the blocks or the count of calls)";
  return "";
}

void SweepExplain(const SweepWorld& a, const SweepWorld& b) {
  const std::array<const char*, 7> names = {"eax", "ecx", "edx", "ebx", "esi", "edi", "ebp"};
  for (size_t reg = 3; reg < 7; ++reg) {
    if (a.call.out[reg] != b.call.out[reg]) std::fprintf(stderr, "    %s: game %08X ours %08X\n", names[reg], a.call.out[reg], b.call.out[reg]);
  }
  std::fprintf(stderr, "    flags game %08X ours %08X esp game %08X ours %08X; finished: game %u ours %u; disposed game %08X ours %08X\n", a.call.flags_out, b.call.flags_out,
               a.call.esp_after, b.call.esp_after, a.unused[0], b.unused[0], a.disposed, b.disposed);
  const auto* x = reinterpret_cast<const uint8_t*>(&a);
  const auto* y = reinterpret_cast<const uint8_t*>(&b);
  int shown = 0;
  for (size_t at = offsetof(SweepWorld, call.frame); at + 4 <= sizeof(SweepWorld) && shown < 16; at += 4) {
    if (Read32(x + at) == Read32(y + at)) continue;
    std::fprintf(stderr, "    +0x%zX: game %08X ours %08X\n", at, Read32(x + at), Read32(y + at));
    ++shown;
  }
}

// Scenario `index` in both copies (as PushBoth: the frame in an array on this function's stack, the faults caught). `world` ends as the patched copy's run left
// it, `result` is the game's. Returns "" if the two agree, else what differs; `faulted`: the game's run faulted.
const char* SweepBoth(SweepWorld* world, SweepWorld* backup, SweepWorld* result, const SweepEdges& edges, uint64_t seed, uint64_t index, bool corrupt, uint32_t game_top,
                      uint32_t patched_top, SweepSpec* spec_out, bool* faulted) {
  alignas(16) uint8_t arena[PUSH_ARENA_BYTES];
  auto* on_stack = reinterpret_cast<LoopCall*>(arena + PUSH_ARENA_CALL);
  *spec_out = SweepScenario(world, edges, seed, index, corrupt);
  *backup = *world;
  bool done[2] = {true, true};
  for (int copy = 0; copy < 2; ++copy) {
    *world = *backup;
    sweep_world_now = world;
    for (size_t page = 0; page < 3; ++page) std::memcpy(edges.page[page], world->edges[page], SWEEP_EDGE_BYTES);
    std::memcpy(on_stack, &world->call, sizeof(LoopCall));
    on_stack->target = (copy == 0 ? game_top : patched_top);
    sweep_counter = 0;
    __try {
      EnterParticleLoop(on_stack);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
      done[copy] = false;
    }
    std::memcpy(&world->call, on_stack, sizeof(LoopCall));
    world->unused[0] = done[copy] ? 1 : 0;
    world->disposed = sweep_counter;
    if (done[copy]) world->call.esp_after -= Address(on_stack);
    for (size_t page = 0; page < 3; ++page) std::memcpy(world->edges[page], edges.page[page], SWEEP_EDGE_BYTES);
    if (copy == 0) *result = *world;
  }
  *faulted = !done[0];
  return SweepCompare(*result, *world, done[0], done[1], patched_top - game_top);
}

// A mutant of the page's stub: `bytes` written at `at` (an offset in the stub) while it runs, put back after.
struct SweepMutant {
  const char* what;
  size_t at;
  std::array<uint8_t, 4> bytes;
  size_t size;
};

struct SweepFinding {
  uint64_t scenarios = 0;
  uint64_t different = 0;
  uint64_t faulted = 0;
  uint64_t first_different = ~0ull;
};

// Whole sweeps of scenarios [first, last) of `seed`, with the sites as they are; counts the runs that differ.
SweepFinding SweepRange(SweepWorld* world, SweepWorld* backup, SweepWorld* result, const SweepEdges& edges, uint64_t seed, uint64_t first, uint64_t last, bool corrupt,
                        uint32_t game_top, uint32_t patched_top, bool report, std::array<uint64_t, 16>* layouts = nullptr) {
  SweepFinding finding;
  for (uint64_t index = first; index < last; ++index) {
    bool faulted = false;
    SweepSpec spec;
    const char* what = SweepBoth(world, backup, result, edges, seed, index, corrupt, game_top, patched_top, &spec, &faulted);
    ++finding.scenarios;
    finding.faulted += faulted ? 1 : 0;
    if (layouts != nullptr) ++(*layouts)[spec.layout & 15];
    if (what[0] == 0) continue;
    ++finding.different;
    finding.first_different = std::min(finding.first_different, index);
    if (report) {
      Report("  sweep: scenario %llu (%u entries from %u, layout %u, corrupt %d): %s differs\n", index, spec.entries, spec.start, spec.layout, spec.corrupt ? 1 : 0, what);
      if (std::getenv("SWEEP_EXPLAIN") != nullptr) SweepExplain(*result, *world);
    }
  }
  return finding;
}

// "median (min-max)" of the values.
std::string SweepSpread(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  return std::format("{:.1f} ({:.1f}-{:.1f})", values[values.size() / 2], values.front(), values.back());
}

// Timing: `LISTS` lists of `low` to `high` children over 512 MB (each list object, children array and child object in its own random 128-byte slot), none ready (the
// first not ready child at a random place: the sweep erases nothing), every cache line the sweep reaches flushed before each sweep; ns a list. Columns, rounds
// interleaved: the game's ready check / the ready check's stub / the stub plus the chain (A, B, C) of the page, (6, 3, 1) and (8, 4, 2) from private stubs.
std::string SweepTimingLines(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  constexpr uint32_t LISTS = 4000;
  constexpr size_t REGION_BYTES = 512u << 20;
  constexpr uint32_t SLOTS = REGION_BYTES / 128;
  auto* region = static_cast<uint8_t*>(VirtualAlloc(nullptr, REGION_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (region == nullptr || code == nullptr) return "   sweep timing: no memory\n";
  const uint32_t site_rva = build.code->sweep_rva + static_cast<uint32_t>(ec::SWEEP_SITE);
  struct Alternative {
    const char* name;
    std::array<uint8_t, 3> distances;
  };
  const std::array<Alternative, 3> alternatives = {{{"(8, 4, 2)", {8, 4, 2}}, {"(4, 2, 1)", {4, 2, 1}}, {"(6, 3, 1)", {6, 3, 1}}}};
  // The private stubs (the page's (8, 4, 2) is the production one).
  std::array<uint32_t, 3> private_stub = {};
  for (size_t which = 1; which < alternatives.size(); ++which) {
    const auto bytes = ec::SweepPrefetchStub(alternatives[which].distances[0], alternatives[which].distances[1], alternatives[which].distances[2]);
    uint8_t* at = code + 0x80 * which;
    std::memcpy(at, bytes.data(), bytes.size());
    Write32(at + bytes.size() - 4, Address(patched + site_rva + ec::SWEEP_SIZE) - Address(at + bytes.size()));
    private_stub[which] = Address(at);
  }
  auto world = std::make_unique<SweepWorld>();
  auto* entries = static_cast<uint32_t*>(VirtualAlloc(nullptr, 8 * LISTS + 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  std::vector<uint32_t> blocks(4 * (LISTS * 10 + LISTS), 0);
  const uint32_t copy_game = Address(game + site_rva);
  const uint32_t copy_patched = Address(patched + site_rva);
  const uint8_t* const site_game = ec::sites[ec::SWEEP_PREFETCH_INDEX].game.data();
  std::string text;
  Random random = {.state = 0x5EE9};
  const int rounds = (quick ? 7 : 9);
  for (const auto& [low, high] : {std::pair<uint32_t, uint32_t>{1, 4}, std::pair<uint32_t, uint32_t>{5, 8}}) {
    // The structures: slots picked at random without repeats (a stride that a random odd multiplier permutes).
    uint32_t slot_cursor = random.Next() % SLOTS;
    const uint32_t stride = (random.Next() | 1) % SLOTS | 1;
    const auto next_slot = [&] {
      slot_cursor = static_cast<uint32_t>((static_cast<uint64_t>(slot_cursor) + stride) % SLOTS);
      return region + 128 * static_cast<size_t>(slot_cursor);
    };
    std::vector<uint8_t*> flush;  // the lines the sweep reads
    uint32_t block_used = 0;
    for (uint32_t list = 0; list < LISTS; ++list) {
      uint8_t* object = next_slot();
      uint8_t* array = next_slot();
      const uint32_t count = low + random.Next() % (high - low + 1);
      const uint32_t first_not_ready = random.Next() % count;
      std::memset(object, 0, 0x50);
      object[4] = 1;
      Write32(object + 0x40, count);
      Write32(object + 0x44, Address(array));
      flush.push_back(object);
      flush.push_back(object + 0x40);
      flush.push_back(array);
      for (uint32_t kid = 0; kid < count; ++kid) {
        uint8_t* child = next_slot();
        std::memset(child, 0, 0x80);
        child[0x6E] = (kid == first_not_ready ? 0 : 1);
        uint32_t* block = &blocks[4 * block_used++];
        block[0] = Address(SWEEP_VTABLE.data());
        block[1] = 2;
        block[2] = 1;
        Write32(array + 8 * kid, Address(child));
        Write32(array + 8 * kid + 4, Address(block));
        flush.push_back(child + 0x40);
      }
      uint32_t* entry_block = &blocks[4 * block_used++];
      entry_block[0] = Address(SWEEP_VTABLE.data());
      entry_block[1] = 2;
      entry_block[2] = 1;
      entries[2 * list] = Address(object);
      entries[2 * list + 1] = Address(entry_block);
    }
    Write32(world->self + 0x18, LISTS);
    Write32(world->self + 0x1C, Address(entries));
    const auto run = [&](uint32_t column) {
      // columns: 0 game, 1 ready check's stub, 2.. the chains
      HoldSites(column >= 1 ? (1u << 4) : 0, 1u << 4);
      std::memcpy(patched + site_rva, site_game, ec::SITE_SIZE);
      if (column == 2) std::memcpy(patched + site_rva, ec::sites[ec::SWEEP_PREFETCH_INDEX].jump.data(), ec::SITE_SIZE);
      if (column >= 3) JumpTo(patched + site_rva, reinterpret_cast<const void*>(static_cast<uintptr_t>(private_stub[column - 2])));
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      for (uint8_t* line : flush) _mm_clflush(line);
      _mm_mfence();
      for (uint32_t& value : world->call.in) value = 0;
      world->call.in[3] = LISTS - 1;
      world->call.in[4] = Address(world->self);
      world->call.flags_in = 2;
      world->call.target = (column == 0 ? copy_game : copy_patched);
      std::memset(world->call.frame, 0, sizeof(world->call.frame));
      const auto start = std::chrono::steady_clock::now();
      EnterParticleLoop(&world->call);
      return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / LISTS;
    };
    std::array<std::vector<double>, 5> times;
    for (int round = 0; round < rounds; ++round) {
      for (uint32_t column = 0; column < 5; ++column) {
        run(column);  // once to bring the entries array and the stub in, the lines flushed again by the timed run
        times[column].push_back(run(column));
      }
    }
    text += std::format("   timing, {} to {} children a list (ns a list, medians of {} interleaved rounds (min-max), 4,000 lists over 512 MB flushed before each sweep): game's check {} / its stub {} / "
                        "stub + chain {} {} / {} {} / {} {}\n",
                        low, high, rounds, SweepSpread(times[0]), SweepSpread(times[1]), alternatives[0].name, SweepSpread(times[2]), alternatives[1].name, SweepSpread(times[3]),
                        alternatives[2].name, SweepSpread(times[4]));
    // Keep the numbers for the decision: the page's chain against the check's stub.
    const auto median = [](std::vector<double> values) {
      std::sort(values.begin(), values.end());
      return values[values.size() / 2];
    };
    text += std::format("   cut by the page's chain: {:.0f} % against the check's stub, {:.0f} % against the game's check\n", 100.0 * (1.0 - median(times[2]) / median(times[1])),
                        100.0 * (1.0 - median(times[2]) / median(times[0])));
  }
  std::memcpy(patched + site_rva, site_game, ec::SITE_SIZE);
  HoldSites(ec::SWEEP_PREFETCH_BIT | (1u << 4), ec::SWEEP_PREFETCH_BIT | (1u << 4));
  VirtualFree(region, 0, MEM_RELEASE);
  VirtualFree(code, 0, MEM_RELEASE);
  VirtualFree(entries, 0, MEM_RELEASE);
  return text;
}

int RunSweepTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  constexpr uint32_t READY_BIT = 1u << 4;
  const uint32_t function_rva = build.code->sweep_rva;
  const uint32_t site_rva = function_rva + static_cast<uint32_t>(ec::SWEEP_SITE);
  const size_t function_bytes = (ec::SWEEP.size() + 1) / 3;
  const uint32_t exit_rva = function_rva + static_cast<uint32_t>(function_bytes) - 4;  // `pop edi` after the loop
  constexpr std::array<uint8_t, 6> GAME_SITE = {0x8B, 0x46, 0x1C, 0x8B, 0x0C, 0xD8};  // mov eax, [esi + 0x1c]; mov ecx, [eax + ebx*8]
  const uint8_t* stub = ec::page + ec::SWEEP_PAGE;

  // 20a. The site and the stub as Prepare made them.
  {
    const ec::Site& site = ec::sites[ec::SWEEP_PREFETCH_INDEX];
    CHECK(site.at == patched + site_rva && ((ec::written >> ec::SWEEP_PREFETCH_INDEX) & 1) != 0);
    CHECK(std::memcmp(game + site_rva, GAME_SITE.data(), GAME_SITE.size()) == 0 && std::memcmp(site.game.data(), GAME_SITE.data(), 5) == 0);
    CHECK(patched[site_rva] == 0xE9 && Address(patched + site_rva + 5) + Read32(patched + site_rva + 1) == Address(stub));
    CHECK(patched[site_rva + 5] == GAME_SITE[5]);  // the rest of the second instruction is the game's, never run
    CHECK(stub[ec::SWEEP_STUB.size() - 5] == 0xE9 && Address(stub + ec::SWEEP_STUB.size()) + Read32(stub + ec::SWEEP_STUB.size() - 4) == Address(patched + site_rva + ec::SWEEP_SIZE));
    CHECK(std::memcmp(stub, ec::SWEEP_STUB.data(), ec::SWEEP_STUB.size() - 4) == 0);
    // The loop's last instruction is the `jb` to its top; the game's code goes on at the `pop edi`.
    CHECK(game[exit_rva - 2] == 0x72 && Address(game + exit_rva) + static_cast<int8_t>(game[exit_rva - 1]) == Address(game + site_rva));
    CHECK(std::memcmp(game + exit_rva, "\x5F\x5B\x5E\xC3", 4) == 0);
    std::printf("20. sweep chain prefetch: the site (+0x%zX in the function's %zu bytes) found and written, the stub (%zu bytes) in the page at 0x%zX\n", ec::SWEEP_SITE, function_bytes,
                ec::SWEEP_STUB.size(), ec::SWEEP_PAGE);
  }

  // The loop's way out, in both copies (after Prepare: its pattern reads these bytes), put back at the end.
  std::array<std::array<uint8_t, 5>, 2> saved_exit = {};
  std::memcpy(saved_exit[0].data(), game + exit_rva, 5);
  std::memcpy(saved_exit[1].data(), patched + exit_rva, 5);
  JumpTo(game + exit_rva, reinterpret_cast<const void*>(&ParticleLoopReturn));
  JumpTo(patched + exit_rva, reinterpret_cast<const void*>(&ParticleLoopReturn));
  const uint32_t game_top = Address(game + site_rva);
  const uint32_t patched_top = Address(patched + site_rva);
  const SweepEdges edges = SweepAllocEdges();
  auto world = std::make_unique<SweepWorld>();
  auto backup = std::make_unique<SweepWorld>();
  auto result = std::make_unique<SweepWorld>();

  // 20b. Whole sweeps.
  const bool only_threads = (std::getenv("SWEEP_THREADS_ONLY") != nullptr);
  const bool one_range = (std::getenv("SWEEP_FIRST") != nullptr);
  const uint64_t seed = (std::getenv("SWEEP_SEED") != nullptr ? std::strtoull(std::getenv("SWEEP_SEED"), nullptr, 16) : 0x20B0);
  HoldSites(ec::SWEEP_PREFETCH_BIT | READY_BIT, ec::SWEEP_PREFETCH_BIT | READY_BIT);
  if (!only_threads) {
    const uint64_t first = (one_range ? std::strtoull(std::getenv("SWEEP_FIRST"), nullptr, 10) : 0);
    const uint64_t last = (std::getenv("SWEEP_LAST") != nullptr ? std::strtoull(std::getenv("SWEEP_LAST"), nullptr, 10) + 1 : (quick ? 20'000 : 250'000));
    std::array<uint64_t, 16> layouts = {};
    const SweepFinding well_formed = SweepRange(world.get(), backup.get(), result.get(), edges, seed, first, last, false, game_top, patched_top, true, &layouts);
    // The corrupt children and blocks with the game's ready check in both copies.
    HoldSites(ec::SWEEP_PREFETCH_BIT, ec::SWEEP_PREFETCH_BIT | READY_BIT);
    const SweepFinding corrupt = SweepRange(world.get(), backup.get(), result.get(), edges, seed + 0x1000000, first, last, true, game_top, patched_top, true);
    HoldSites(ec::SWEEP_PREFETCH_BIT | READY_BIT, ec::SWEEP_PREFETCH_BIT | READY_BIT);
    std::string layout_text;
    for (uint32_t layout = 0; layout < 16; ++layout) layout_text += std::format("{}{}", layout == 0 ? "" : ", ", layouts[layout]);
    std::printf("   whole sweeps: %llu well-formed (the ready check's stub in the patched copy; %llu faulted) and %llu corrupt (the game's check in both; %llu faulted) scenarios, by layout "
                "(bit 1 the vector's start at a page start, 2 the children arrays at a page's end, 4 the list objects at a page's end, 8 the vector's end at a page's end): %s: NOT equal %llu\n",
                well_formed.scenarios, well_formed.faulted, corrupt.scenarios, corrupt.faulted, layout_text.c_str(), well_formed.different + corrupt.different);
    CHECK(well_formed.different == 0 && corrupt.different == 0);
    CHECK(one_range || (well_formed.faulted > well_formed.scenarios / 20 && corrupt.faulted > corrupt.scenarios / 10));
  }

  // 20c. CONTROLs.
  if (!only_threads && !one_range) {
    DWORD protection;
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection) != FALSE);
    uint8_t* const code = ec::page + ec::SWEEP_PAGE;
    const std::array<SweepMutant, 10> mutants = {{
        {"no entry bound at stage A (the vector's first entry at a page start)", 0x09, {0x90, 0x90}, 2},
        {"no entry bound at stage C", 0x30, {0x90, 0x90}, 2},
        {"an inverted bound at stage B (jb for jae)", 0x1B, {0x72}, 1},
        {"no count test at stage C (a list without children whose array pointer is garbage)", 0x3D, {0x90, 0x90}, 2},
        {"no null test at stage B (a null list object)", 0x22, {0x90, 0x90}, 2},
        {"no null test at stage C", 0x37, {0x90, 0x90}, 2},
        {"a load for stage A's prefetch (mov edx, [ecx]; nop: a null list object)", 0x0E, {0x8B, 0x11, 0x90}, 3},
        {"stage C takes the count for the children array pointer", 0x41, {0x40}, 1},
        {"ebx written (lea ebx, [ebx - 1] for lea edx, [ebx - 1])", 0x2A, {0x8D, 0x5B, 0xFF}, 3},
        {"the game's second instruction not run (no reload of ecx)", 0x48, {0x90, 0x90, 0x90}, 3},
    }};
    for (const SweepMutant& mutant : mutants) {
      std::array<uint8_t, 4> saved = {};
      std::memcpy(saved.data(), code + mutant.at, mutant.size);
      std::memcpy(code + mutant.at, mutant.bytes.data(), mutant.size);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      const SweepFinding seen = SweepRange(world.get(), backup.get(), result.get(), edges, seed + 0x2000000, 0, quick ? 4000 : 12000, false, game_top, patched_top, false);
      std::memcpy(code + mutant.at, saved.data(), mutant.size);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      std::printf("   CONTROL, %s: %llu of %llu scenarios seen as different\n", mutant.what, seen.different, seen.scenarios);
      if (seen.different < 1) {
        VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READ, &protection);
        CHECK(seen.different >= 1);
      }
    }
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READ, &protection) != FALSE);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }

  // 20d. The site switched with its row 200 times while 4 threads run whole sweeps of both copies on their own worlds, then a parked thread at each byte.
  if (!one_range) {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> scenarios_run = 0;
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        const SweepEdges thread_edges = SweepAllocEdges();
        auto thread_world = std::make_unique<SweepWorld>();
        auto thread_backup = std::make_unique<SweepWorld>();
        auto thread_result = std::make_unique<SweepWorld>();
        for (uint64_t index = 0; !stop.load(); ++index) {
          bool faulted;
          SweepSpec spec;
          const char* what = SweepBoth(thread_world.get(), thread_backup.get(), thread_result.get(), thread_edges, 0x20D0 + static_cast<uint64_t>(worker) * 0x1000000ull, index, false,
                                       game_top, patched_top, &spec, &faulted);
          if (what[0] != 0 && ++bad <= 5) {
            std::printf("   thread %d scenario %llu (%u entries from %u, layout %u): %s differs\n", worker, index, spec.entries, spec.start, spec.layout, what);
            SweepExplain(*thread_result, *thread_world);
          }
          ++scenarios_run;
        }
        SweepFreeEdges(thread_edges);
      });
    }
    int busy = 0;
    bool on = true;
    for (int switches = 0; switches < 200; ++switches) {
      on = !on;
      int tries = 0;
      do {
        twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true, on);
        ++tries;
      } while ((((ec::written >> ec::SWEEP_PREFETCH_INDEX) & 1) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
    }
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true, true);
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("   200 switches while 4 threads made %llu sweeps in both copies on their own worlds: %llu wrong (%d writes waited for a thread inside a site)\n", scenarios_run.load(), bad.load(),
                busy);
    CHECK(bad.load() == 0 && (ec::written & ec::SWEEP_PREFETCH_BIT) == ec::SWEEP_PREFETCH_BIT);
    if (const int failed = ParkedAtSites(ec::SWEEP_PREFETCH_BIT, 6, false, true); failed != 0) return failed;
    std::printf("   a parked thread at each site -1 .. +6: refused at +1 .. +4 and written at the others\n");

    // 20e. Timing.
    HoldSites(ec::SWEEP_PREFETCH_BIT | READY_BIT, ec::SWEEP_PREFETCH_BIT | READY_BIT);
    std::printf("%s", SweepTimingLines(build, game, patched, quick).c_str());
    HoldSites(ec::SWEEP_PREFETCH_BIT | READY_BIT, ec::SWEEP_PREFETCH_BIT | READY_BIT);
  }

  std::memcpy(game + exit_rva, saved_exit[0].data(), 5);
  std::memcpy(patched + exit_rva, saved_exit[1].data(), 5);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  SweepFreeEdges(edges);
  return 0;
}
