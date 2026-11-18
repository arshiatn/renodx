// ====================================================================================================================
// 18. The particle writer's table entry prefetch (the function of 14., +0x18960d0, Thrones +0x190b430; site +0x18960f5; Faster Particle Prefetch;
// engine_code.hpp 14.), included inside tests.cpp's anonymous namespace after more_reloads.inc (ReloadWorld, ReloadRandom, ReloadDiff, ReloadPath,
// FnCall, CallFn and FnHandler of the earlier parts are used). The writer looks up an entry of 24 bytes (six stream pointers: the table's base + 24 *
// [this + 0xd0], null for the index -1 and when the chain to the table has no link) and runs `mov [esp + 0x7c], eax; test eax, eax; je the way out`; the
// stub runs both with two prefetches between them:
// 18a. the site and the stub as Prepare made them (jumps, bytes);
// 18b. the whole function on a fake object graph (the earlier part 14's), with the site alone and with the reload sites too: every register, x87 / MXCSR /
//    xmm, the flags, esp, the frame (return addresses apart), the six streams and the objects equal, both sides of the je;
// 18c. entries in odd places (null, a no-access page, an entry across its edge, the top of the address space, random addresses): both copies fault at the
//    same instruction with the same memory, or neither does and everything is equal (a prefetch of any address never faults);
// 18d. CONTROLs: no test (the je then reads the lookup's flags), a write to ecx, no store of [esp + 0x7c], a load for the prefetch, the jump to the wrong
//    place: each must be seen;
// 18e. 200 switches under 4 threads that run both copies, a parked thread at each byte of the site;
// 18f. timing: the three micro-benchmarks (the table entry hot / sequential / cold; the stale slot [esp + 0x4c] of the multiply at +0x1896807 normal /
//    subnormal; the definition chain [[this + 0x10] + 0x78] + 0x2c hot / cold / chain-prefetched).
// ====================================================================================================================

// The entry the writer looks up is the table's base (the world's table, or `entry`) + 24 * 0.
void EntryAt(ReloadWorld* world, uint32_t entry) {
  uint8_t* bytes = world->bytes.data();
  Write32(bytes + RELOAD_THIS + 0xD0, 0);
  Write32(bytes + RELOAD_X + 0x180, Address(bytes + RELOAD_Y));
  Write32(bytes + RELOAD_Z + 8, entry);
}

// The writer with an access violation caught (FnHandler records the faulting Eip + 1 in the call and lands back in CallFn's caller): false if it faulted.
bool RunReloadGuarded(ReloadWorld* world, const ReloadWorld& backup, ReloadWorld* out) {
  *world = backup;
  world->call.faulted = 0;
  fn_call = &world->call;
  CallFn(&world->call);
  fn_call = nullptr;
  *out = *world;
  return world->call.faulted == 0;
}

// What differs between the two copies' runs that both faulted: the instruction (the same place in each copy), the objects and the streams, the stack.
const char* EntryFaultDiff(const ReloadWorld& a, const ReloadWorld& b, uint32_t copy_distance) {
  if (b.call.faulted - a.call.faulted != copy_distance) return "the place of the fault";
  if (a.bytes != b.bytes) return "memory (the streams or the objects)";
  for (uint32_t at = FN_STACK_E - 0x400; at < FN_STACK_E + 0x20; at += 4) {
    const uint32_t x = Read32(a.call.stack + at);
    const uint32_t y = Read32(b.call.stack + at);
    if (x != y && y - x != copy_distance && !ProbeDeadStack(at, FN_STACK_E - 0xE0)) return "stack";
  }
  return "";
}

// The game's run and the run of the function at `patched_function` on the same world: "" if equal (or both faulted in the same place with the same memory),
// else what differs. `faulted` counts the runs that faulted.
const char* EntryCompare(ReloadWorld* world, ReloadWorld* backup, ReloadWorld* a, ReloadWorld* b, uint32_t game_function, uint32_t patched_function,
                         uint32_t copy_distance, uint64_t* faulted) {
  backup->call.function = game_function;
  const bool a_done = RunReloadGuarded(world, *backup, a);
  backup->call.function = patched_function;
  const bool b_done = RunReloadGuarded(world, *backup, b);
  if (a_done != b_done) return "ONE of the copies faulted";
  if (a_done) return ReloadDiff(*a, *b, copy_distance);
  ++*faulted;
  return EntryFaultDiff(*a, *b, copy_distance);
}

// "median (min-max)" of the values.
std::string EntrySpread(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  return std::format("{:.1f} ({:.1f}-{:.1f})", values[values.size() / 2], values.front(), values.back());
}

// One scenario of the timing: ns a call of `writer` for `calls` calls; `before` runs ahead of each call (it moves the world to the next entry or object).
template <typename Before>
double EntryTimeCalls(ReloadWriter writer, uint8_t* bytes, float first, int calls, Before before) {
  const auto start = std::chrono::steady_clock::now();
  for (int call = 0; call < calls; ++call) {
    before(call);
    writer(bytes, nullptr, first, reinterpret_cast<const float*>(bytes + RELOAD_SIX));
  }
  return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / calls;
}

// Timing: (i) the table entry (256 MB of entries: hot = the same entry, sequential, cold = at random), game / the patched copy with the entry site as the game's
// bytes / with the stub, the matrix path and the path without the matrix; (ii) the multiply's stale operand [esp + 0x4c] (the path without the matrix leaves
// it as the last call stored it: stale * [this + 0x78]) normal / subnormal / subnormal with DAZ + FTZ / a factor of 0; (iii) the definition chain
// [[this + 0x10] + 0x78] + 0x2c hot / cold (2 x 128 MB of objects, the whole function in the game's copy) / cold with the chain prefetched two calls ahead as
// an emitter-loop stage would (W's line two calls ahead, D's line one ahead). Rounds interleave the columns; medians of `ROUNDS`.
std::string EntryTimingLines(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  constexpr size_t TABLE_BYTES = 256u << 20;
  constexpr uint32_t ENTRIES = TABLE_BYTES / 24;
  constexpr size_t OBJECT_BYTES = 256;
  constexpr size_t OBJECTS_BYTES = 128u << 20;
  constexpr uint32_t OBJECTS = OBJECTS_BYTES / OBJECT_BYTES;
  const int rounds = (quick ? 5 : 9);
  auto* table = static_cast<uint8_t*>(VirtualAlloc(nullptr, TABLE_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  auto* w_objects = static_cast<uint8_t*>(VirtualAlloc(nullptr, OBJECTS_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  auto* d_objects = static_cast<uint8_t*>(VirtualAlloc(nullptr, OBJECTS_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (table == nullptr || w_objects == nullptr || d_objects == nullptr) return "   entry timing: no memory\n";
  auto world = std::make_unique<ReloadWorld>();
  auto timing_call = std::make_unique<ReloadWorld>();
  uint8_t* bytes = world->bytes.data();
  Random random = {.state = 0x18F};
  const uint32_t argument = ReloadFillWorld(bytes, &random, 0);
  float first;
  std::memcpy(&first, &argument, 4);
  const uint32_t base = Address(bytes);
  const auto put = [&](uint32_t at, uint32_t value) { Write32(bytes + at, value); };
  put(RELOAD_X + 0x180, base + RELOAD_Y);
  put(RELOAD_THIS + 0xD0, 0);
  put(RELOAD_Z + 8, Address(table));
  for (uint32_t entry = 0; entry < ENTRIES; ++entry) {
    for (uint32_t stream = 0; stream < 6; ++stream) Write32(table + 24 * static_cast<size_t>(entry) + 4 * stream, base + RELOAD_STREAMS + RELOAD_STREAM_STRIDE * stream);
  }
  for (uint32_t object = 0; object < OBJECTS; ++object) {
    uint8_t* w = w_objects + OBJECT_BYTES * object;
    uint8_t* d = d_objects + OBJECT_BYTES * object;
    std::memcpy(w, bytes + RELOAD_W, 0x80);
    std::memcpy(d, bytes + RELOAD_D, 0x100);
    Write32(w + 0x78, Address(d));
  }
  const uint32_t function_rva = build.code->entry_fetch_rva;
  const auto game_writer = reinterpret_cast<ReloadWriter>(game + function_rva);
  const auto patched_writer = reinterpret_cast<ReloadWriter>(patched + function_rva);
  const auto entry_site = [&](bool on) { HoldSites(on ? ec::ENTRY_PREFETCH_BIT : 0, ec::ENTRY_PREFETCH_BIT); };
  const auto set_path = [&](bool matrix) {
    bytes[RELOAD_D + 0x1C] = (matrix ? 1 : 0);
    bytes[RELOAD_D + 0x2C] = 1;
    for (uint32_t object = 0; object < OBJECTS; ++object) {
      d_objects[OBJECT_BYTES * object + 0x1C] = bytes[RELOAD_D + 0x1C];
      d_objects[OBJECT_BYTES * object + 0x2C] = 1;
    }
  };
  std::string text;
  std::string load;
  // (i) the table entry.
  constexpr std::array<const char*, 3> MODES = {"same entry", "sequential entries", "cold entries"};
  for (const bool matrix : {true, false}) {
    set_path(matrix);
    for (int mode = 0; mode < 3; ++mode) {
      std::array<std::vector<double>, 3> times;
      for (int round = 0; round < rounds; ++round) {
        for (int column = 0; column < 3; ++column) {
          entry_site(column == 2);
          Random pick = {.state = 0x5EED + static_cast<uint64_t>(round)};
          const int calls = (mode == 2 ? (quick ? 40'000 : 100'000) : 200'000);
          uint32_t* index = reinterpret_cast<uint32_t*>(bytes + RELOAD_THIS + 0xD0);
          times[column].push_back(EntryTimeCalls(column == 0 ? game_writer : patched_writer, bytes, first, calls, [&](int call) {
            *index = (mode == 0 ? 0u : (mode == 1 ? static_cast<uint32_t>(call) % ENTRIES : pick.Next() % ENTRIES));
          }));
        }
      }
      text += std::format("   (i) {} path, {}: game {} / stub off {} / stub on {}\n", matrix ? "matrix" : "no-matrix", MODES[mode], EntrySpread(times[0]),
                          EntrySpread(times[1]), EntrySpread(times[2]));
    }
  }
  entry_site(true);
  put(RELOAD_Z + 8, base + RELOAD_TABLE);
  // (ii) the multiply's stale operand, through CallFn (its entry and exit cost is in every number: measured with an empty function).
  {
    auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    code[0] = 0xC3;  // ret
    struct StaleCase {
      const char* name;
      uint32_t stale;
      uint32_t factor;
      bool factor_read;
      uint32_t mxcsr;
    };
    const std::array<StaleCase, 5> cases = {{{"normal (1.0) x 1.0", 0x3F800000, 0x3F800000, true, 0x1F80},
                                             {"subnormal (1e-40) x 1.0", 0x000116C2, 0x3F800000, true, 0x1F80},
                                             {"subnormal x 1.0, DAZ + FTZ", 0x000116C2, 0x3F800000, true, 0x9FC0},
                                             {"normal x 0 (no factor read)", 0x3F800000, 0, false, 0x1F80},
                                             {"normal x 0.5 (decays to subnormal and zero)", 0x3F800000, 0x3F000000, true, 0x1F80}}};
    std::array<std::vector<double>, 5> times;
    std::vector<double> empty_times;
    const uint32_t game_function = Address(game + function_rva);
    for (int round = 0; round < rounds; ++round) {
      for (size_t which = 0; which <= cases.size(); ++which) {
        ReloadRandom(timing_call.get(), &random, 0, which == cases.size() ? Address(code) : game_function, 0x1F80, true);
        uint8_t* object = timing_call->bytes.data();
        Write32(object + RELOAD_THIS + 0xD0, 0);
        Write32(object + RELOAD_X + 0x180, Address(object + RELOAD_Y));
        Write32(object + RELOAD_Z + 8, Address(object + RELOAD_TABLE));
        object[RELOAD_D + 0x1C] = 0;  // the path without the matrix
        if (which < cases.size()) {
          const StaleCase& stale = cases[which];
          object[RELOAD_D + 0x2C] = (stale.factor_read ? 1 : 0);
          Write32(object + RELOAD_THIS + 0x78, stale.factor);
          Write32(timing_call->call.stack + FN_STACK_E - 0xE0 + 0x4C, stale.stale);
          Write32(timing_call->call.fx_in + 24, stale.mxcsr);
        }
        const int calls = (quick ? 100'000 : 300'000);
        const auto start = std::chrono::steady_clock::now();
        for (int call = 0; call < calls; ++call) CallFn(&timing_call->call);
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / calls;
        (which == cases.size() ? empty_times : times[which]).push_back(ns);
      }
    }
    VirtualFree(code, 0, MEM_RELEASE);
    text += std::format("   (ii) the function through CallFn, the path without the matrix, ns a call (an empty function through CallFn: {}): ", EntrySpread(empty_times));
    for (size_t which = 0; which < cases.size(); ++which) {
      text += std::format("{}{} {}", which == 0 ? "" : "; ", cases[which].name, EntrySpread(times[which]));
    }
    text += "\n";
  }
  // (iii) the definition chain.
  {
    put(RELOAD_THIS + 0xD0, 0);
    const auto set_object = [&](uint32_t object) { put(RELOAD_THIS + 0x10, Address(w_objects + OBJECT_BYTES * static_cast<size_t>(object))); };
    for (const bool matrix : {true, false}) {
      set_path(matrix);
      std::array<std::vector<double>, 3> times;
      for (int round = 0; round < rounds; ++round) {
        for (int column = 0; column < 3; ++column) {
          Random pick = {.state = 0xC0DE + static_cast<uint64_t>(round)};
          std::array<uint32_t, 3> ahead = {pick.Next() % OBJECTS, pick.Next() % OBJECTS, pick.Next() % OBJECTS};  // calls i, i + 1, i + 2
          const int calls = (quick ? 40'000 : 100'000);
          times[column].push_back(EntryTimeCalls(game_writer, bytes, first, calls, [&](int) {
            if (column == 0) {
              set_object(0);  // hot: the same W and D
              return;
            }
            ahead = {ahead[1], ahead[2], pick.Next() % OBJECTS};
            if (column == 2) {
              _mm_prefetch(reinterpret_cast<const char*>(w_objects + OBJECT_BYTES * static_cast<size_t>(ahead[2]) + 0x78), _MM_HINT_T0);
              const uint8_t* next_w = w_objects + OBJECT_BYTES * static_cast<size_t>(ahead[1]);
              _mm_prefetch(reinterpret_cast<const char*>(static_cast<uintptr_t>(Read32(next_w + 0x78))) + 0x2C, _MM_HINT_T0);
            }
            set_object(ahead[0]);
          }));
        }
      }
      text += std::format("   (iii) {} path, the whole function in the game's copy: W and D hot {} / cold {} / cold, chain prefetched two calls ahead {}\n",
                          matrix ? "matrix" : "no-matrix", EntrySpread(times[0]), EntrySpread(times[1]), EntrySpread(times[2]));
    }
  }
  VirtualFree(table, 0, MEM_RELEASE);
  VirtualFree(w_objects, 0, MEM_RELEASE);
  VirtualFree(d_objects, 0, MEM_RELEASE);
  return text;
}

int RunEntryTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  const uint32_t function_rva = build.code->entry_fetch_rva;
  const uint32_t site_rva = function_rva + ec::ENTRY_FETCH_SITE;
  const uint32_t copy_distance = static_cast<uint32_t>(patched - game);
  const uint32_t game_function = Address(game + function_rva);
  const uint32_t patched_function = Address(patched + function_rva);
  uint8_t* const stub = ec::page + ec::ENTRY_PREFETCH_PAGE;
  constexpr size_t STUB_SIZE = ec::ENTRY_FETCH_STUB.size();
  constexpr std::array<uint8_t, 6> GAME_SITE = {0x89, 0x44, 0x24, 0x7C, 0x85, 0xC0};  // mov [esp + 0x7c], eax; test eax, eax
  constexpr uint32_t ENTRY_VARIANTS_REST = ec::FLOAT4_RELOAD_SITES;

  // 18a. The site and the stub as Prepare made them.
  CHECK(ec::sites[ec::ENTRY_PREFETCH_INDEX].at == patched + site_rva && (ec::written >> ec::ENTRY_PREFETCH_INDEX & 1) != 0);
  CHECK(std::memcmp(game + site_rva, GAME_SITE.data(), 6) == 0 && std::memcmp(ec::sites[ec::ENTRY_PREFETCH_INDEX].game.data(), GAME_SITE.data(), 5) == 0);
  CHECK(game[function_rva + 0x20] == 0xE8 && function_rva + 0x25 == site_rva);  // the lookup's call returns to the site's first byte
  CHECK(game[site_rva + 6] == 0x0F && game[site_rva + 7] == 0x84);              // the je
  CHECK(patched[site_rva] == 0xE9 && SiteJumpTarget(ec::ENTRY_PREFETCH_INDEX, patched + site_rva) == Address(stub) && patched[site_rva + 5] == 0xC0);
  CHECK(stub[STUB_SIZE - 5] == 0xE9 && Address(stub + STUB_SIZE) + Read32(stub + STUB_SIZE - 4) == Address(patched + site_rva + 6));
  CHECK(std::memcmp(stub, ec::ENTRY_FETCH_STUB.data(), STUB_SIZE - 5) == 0);
  std::printf("18. particle writer table entry prefetch: the site (+0x%X in the writer) found and written, the stub (%zu bytes) jumps back to the je\n",
              static_cast<uint32_t>(ec::ENTRY_FETCH_SITE), STUB_SIZE);

  auto world = std::make_unique<ReloadWorld>();
  auto backup = std::make_unique<ReloadWorld>();
  auto result_a = std::make_unique<ReloadWorld>();
  auto result_b = std::make_unique<ReloadWorld>();
  Random random = {.state = 0x18B};
  const PVOID handler = AddVectoredExceptionHandler(1, FnHandler);
  CHECK(handler != nullptr);

  // 18b. The whole function on the fake graph.
  {
    const int calls = (quick ? 60'000 : 400'000);
    std::array<uint64_t, 4> paths = {};
    constexpr std::array<uint32_t, 2> VARIANTS = {ec::ENTRY_PREFETCH_BIT, ec::ENTRY_PREFETCH_BIT | ENTRY_VARIANTS_REST};  // the site alone, with the reloads
    std::array<uint64_t, 2> wrong = {};
    uint64_t faulted = 0;
    for (int round = 0; round < calls; ++round) {
      const bool canary = (round % 4 != 3);
      ReloadRandom(world.get(), &random, round % 4 < 2 ? 0 : (round % 4 == 2 ? 1 : 2), game_function, ReloadMxcsr(&random, round), canary);
      *backup = *world;
      for (size_t variant = 0; variant < VARIANTS.size(); ++variant) {
        HoldSites(VARIANTS[variant], ec::ENTRY_PREFETCH_BIT | ENTRY_VARIANTS_REST);
        if (const char* what = EntryCompare(world.get(), backup.get(), result_a.get(), result_b.get(), game_function, patched_function, copy_distance, &faulted);
            what[0] != 0) {
          ++wrong[variant];
          Report("  entry prefetch whole function: round %d, variant %zu: %s differs\n", round, variant, what);
        }
      }
      if (canary) ++paths[ReloadPath(*result_a)];
    }
    HoldSites(VARIANTS[1], ec::ENTRY_PREFETCH_BIT | ENTRY_VARIANTS_REST);
    std::printf("   the whole function: %d calls (no table entry %llu, no matrix %llu, matrix and the comiss's jb %llu, matrix and the square root %llu; %llu faulted): NOT equal with the site alone %llu, with the reload sites too %llu\n",
                calls, paths[0], paths[1], paths[2], paths[3], faulted, wrong[0], wrong[1]);
    CHECK(wrong[0] == 0 && wrong[1] == 0 && faulted == 0);
    const uint64_t at_least = (quick ? 500 : 3000);
    CHECK(paths[0] > at_least && paths[1] > at_least && paths[2] > at_least && paths[3] > at_least);
  }

  // 18c. Entries in odd places. A page of the readable memory, a no-access page, a readable one: entries across and next to the no-access page.
  auto* guard = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x3000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  CHECK(guard != nullptr);
  DWORD guard_protection;
  CHECK(VirtualProtect(guard + 0x1000, 0x1000, PAGE_NOACCESS, &guard_protection) != FALSE);
  const auto stream_pointers = [&](uint8_t* entry_at, size_t words, const ReloadWorld& from) {
    for (size_t word = 0; word < words; ++word) Write32(entry_at + 4 * word, Address(from.bytes.data()) + RELOAD_STREAMS + RELOAD_STREAM_STRIDE * (word % 6));
  };
  struct Place {
    const char* what;
    uint32_t entry;
    size_t readable_words;  // of the entry's six, the first ones that hold valid stream pointers (0: the entry isn't readable)
  };
  const std::array<Place, 12> places = {{{"null", 0, 0},
                                         {"0x18", 0x18, 0},
                                         {"the last 24 bytes before the no-access page", Address(guard) + 0x1000 - 24, 6},
                                         {"an entry across the page edge, 8 bytes readable", Address(guard) + 0x1000 - 8, 2},
                                         {"an entry across the page edge, 20 bytes readable", Address(guard) + 0x1000 - 20, 5},
                                         {"the no-access page", Address(guard) + 0x1000, 0},
                                         {"the no-access page's last 8 bytes", Address(guard) + 0x2000 - 8, 0},
                                         {"the top of the address space, 0xFFFFFFF0", 0xFFFFFFF0, 0},
                                         {"0xFFFFFFE8 (the entry's last byte wraps)", 0xFFFFFFE8, 0},
                                         {"0xFFFFFFFF", 0xFFFFFFFF, 0},
                                         {"0x7FFFFFF0", 0x7FFFFFF0, 0},
                                         {"0x80000000", 0x80000000, 0}}};
  // A world with the entry at `place` (the readable words of it hold the world's stream pointers).
  const auto world_at = [&](const Place& place, int run) {
    ReloadRandom(world.get(), &random, run % 3, game_function, ReloadMxcsr(&random, run), (run % 4) != 3);
    EntryAt(world.get(), place.entry);
    if (place.readable_words != 0) stream_pointers(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(place.entry)), place.readable_words, *world);
    *backup = *world;
  };
  // Random entries that can't hurt the test: the null page, above 2 GB (no process of 32 bits reaches it), a zero page (the pointers read from it are
  // null: the game's stores to them fault), the no-access page.
  auto* zeros = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  CHECK(zeros != nullptr);
  const auto random_entry = [&](Random* pick) {
    switch (pick->Next() % 4) {
      case 0: return pick->Next() & 0xFFFF;
      case 1: return 0x80000000u | pick->Next();
      case 2: return Address(zeros) + (pick->Next() & 0xFFFF);
      default: return Address(guard) + 0x1000 + (pick->Next() & 0xFFF);
    }
  };
  {
    HoldSites(ec::ENTRY_PREFETCH_BIT | ENTRY_VARIANTS_REST, ec::ENTRY_PREFETCH_BIT | ENTRY_VARIANTS_REST);
    const int per_place = (quick ? 400 : 4000);
    std::string line;
    uint64_t total_wrong = 0;
    uint64_t total_faulted = 0;
    for (const Place& place : places) {
      uint64_t wrong = 0;
      uint64_t faulted = 0;
      for (int run = 0; run < per_place; ++run) {
        world_at(place, run);
        if (const char* what = EntryCompare(world.get(), backup.get(), result_a.get(), result_b.get(), game_function, patched_function, copy_distance, &faulted);
            what[0] != 0) {
          ++wrong;
          Report("  entry prefetch, %s: run %d: %s differs\n", place.what, run, what);
        }
      }
      line += std::format("{}{}: {} of {}", line.empty() ? "" : "; ", place.what, faulted, per_place);
      total_wrong += wrong;
      total_faulted += faulted;
    }
    uint64_t random_faulted = 0;
    uint64_t random_wrong = 0;
    const int random_runs = (quick ? 20'000 : 200'000);
    for (int run = 0; run < random_runs; ++run) {
      world_at({.what = "random", .entry = random_entry(&random), .readable_words = 0}, run);
      if (const char* what = EntryCompare(world.get(), backup.get(), result_a.get(), result_b.get(), game_function, patched_function, copy_distance, &random_faulted);
          what[0] != 0) {
        ++random_wrong;
        Report("  entry prefetch, random entry: run %d: %s differs\n", run, what);
      }
    }
    std::printf("   odd entries, faulted of %d each: %s;\n   %d random entries (null page, above 2 GB, zeros, no-access), %llu faulted: NOT equal %llu (places) + %llu (random)\n",
                per_place, line.c_str(), random_runs, random_faulted, total_wrong, random_wrong);
    CHECK(total_wrong == 0 && random_wrong == 0 && total_faulted > 0 && random_faulted > 0);
  }

  // 18d. CONTROLs: each variant of the stub must be seen on worlds with the odd entries and the plain table. The two copies' code read-only meanwhile (a CONTROL's
  // wrong stub can leave a garbage pointer, and a store through it into a copy's code would stay: ProtectCode).
  {
    DWORD protection;
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection) != FALSE);
    CHECK(ProtectCode(game, PAGE_EXECUTE_READ) && ProtectCode(patched, PAGE_EXECUTE_READ));
    struct Mutant {
      const char* what;
      size_t at;
      std::array<uint8_t, 6> bytes;
      size_t size;
    };
    // The jump's rel32 sits at the stub's last 4 bytes; the last mutant sends it 6 bytes on, past the je (a null entry then goes on instead of leaving).
    const std::array<Mutant, 5> mutants = {{{"no test (the je reads the lookup's flags)", 11, {0x90, 0x90}, 2},
                                            {"a write to ecx (mov ecx, eax for the first prefetch)", 4, {0x8B, 0xC8, 0x90}, 3},
                                            {"no store of [esp + 0x7c]", 0, {0x90, 0x90, 0x90, 0x90}, 4},
                                            {"a load (mov edx, [eax]) for the first prefetch", 4, {0x8B, 0x10, 0x90}, 3},
                                            {"the jump past the je", STUB_SIZE - 4, {0, 0, 0, 0}, 4}}};
    for (size_t index = 0; index < mutants.size(); ++index) {
      const Mutant& mutant = mutants[index];
      std::array<uint8_t, STUB_SIZE> saved;
      std::memcpy(saved.data(), stub, STUB_SIZE);
      std::array<uint8_t, 6> bytes = mutant.bytes;
      if (index == 4) Write32(bytes.data(), Read32(stub + mutant.at) + 6);
      std::memcpy(stub + mutant.at, bytes.data(), mutant.size);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      int seen = 0;
      uint64_t faulted = 0;
      constexpr int RUNS = 3000;
      for (int run = 0; run < RUNS; ++run) {
        if (run % 3 == 2) {
          world_at(places[run / 3 % places.size()], run);
        } else {
          ReloadRandom(world.get(), &random, run % 3, game_function, 0x1F80, true);  // the world's own table
          *backup = *world;
        }
        seen += EntryCompare(world.get(), backup.get(), result_a.get(), result_b.get(), game_function, patched_function, copy_distance, &faulted)[0] != 0 ? 1 : 0;
      }
      std::memcpy(stub, saved.data(), STUB_SIZE);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      std::printf("   CONTROL, %s: %d of %d calls seen as different\n", mutant.what, seen, RUNS);
      CHECK(seen > RUNS / 20);
    }
    CHECK(ProtectCode(game, PAGE_EXECUTE_READWRITE) && ProtectCode(patched, PAGE_EXECUTE_READWRITE));
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READ, &protection) != FALSE);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }
  VirtualFree(guard, 0, MEM_RELEASE);
  VirtualFree(zeros, 0, MEM_RELEASE);
  RemoveVectoredExceptionHandler(handler);

  // 18e. The site switched with its row 200 times while 4 threads call both copies on their own graphs, a parked thread at each byte of the site.
  {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> calls = 0;
    const auto game_writer = reinterpret_cast<ReloadWriter>(game + function_rva);
    const auto patched_writer = reinterpret_cast<ReloadWriter>(patched + function_rva);
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        struct alignas(16) Graph {
          std::array<uint8_t, RELOAD_WORLD_SIZE> bytes;
        };
        auto graph = std::make_unique<Graph>();
        auto before = std::make_unique<Graph>();
        auto after_game = std::make_unique<Graph>();
        Random thread_random = {.state = 0xE1E0ull + static_cast<uint64_t>(worker)};
        while (!stop.load()) {
          const uint32_t argument = ReloadFillWorld(graph->bytes.data(), &thread_random, static_cast<int>(thread_random.Next() % 3));
          float first_argument;
          std::memcpy(&first_argument, &argument, 4);
          *before = *graph;
          for (int repeat = 0; repeat < 32; ++repeat) {
            game_writer(graph->bytes.data(), nullptr, first_argument, reinterpret_cast<const float*>(graph->bytes.data() + RELOAD_SIX));
          }
          *after_game = *graph;
          *graph = *before;
          for (int repeat = 0; repeat < 32; ++repeat) {
            patched_writer(graph->bytes.data(), nullptr, first_argument, reinterpret_cast<const float*>(graph->bytes.data() + RELOAD_SIX));
          }
          if (graph->bytes != after_game->bytes) ++bad;
          calls += 32;
        }
      });
    }
    int busy = 0;
    int never = 0;
    bool on = true;
    for (int switches = 0; switches < 200; ++switches) {
      on = !on;
      int tries = 0;
      do {
        twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, on);
        ++tries;
      } while ((((ec::written >> ec::ENTRY_PREFETCH_INDEX) & 1) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
      never += (((ec::written >> ec::ENTRY_PREFETCH_INDEX) & 1) != 0) != on ? 1 : 0;
    }
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true);
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("   200 switches while 4 threads made %llu calls of each copy on their own graphs: %llu wrong (%d writes waited for a thread inside the site, %d switches never reached)\n",
                calls.load(), bad.load(), busy, never);
    CHECK(bad.load() == 0 && never == 0 && calls.load() > 0 && ((ec::written >> ec::ENTRY_PREFETCH_INDEX) & 1) != 0);
  }
  if (const int failed = ParkedAtSites(ec::ENTRY_PREFETCH_BIT, 6, true); failed != 0) return failed;
  std::printf("   a parked thread at the site -1 .. +6: refused at +1 .. +4 and written at the others\n");

  // 18f. Timing (the entry site back on afterwards).
  std::printf("%s", EntryTimingLines(build, game, patched, quick).c_str());
  HoldSites(ec::ENTRY_PREFETCH_BIT | ENTRY_VARIANTS_REST, ec::ENTRY_PREFETCH_BIT | ENTRY_VARIANTS_REST);
  return 0;
}
