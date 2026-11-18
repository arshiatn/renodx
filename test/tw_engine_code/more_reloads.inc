// ====================================================================================================================
// 17. More float4 reloads done lane by lane (Faster Game CPU Code; engine_code.hpp 12.), included after particle_reload.inc (ReloadAlone,
// the block call, ReloadFloat, HoldSites, ParkedAtSites). The soldier draw's three matrix row reloads, each an 8-byte `movups xmm0, [esp +
// 0x90 / 0xa0 / 0xb0]` (Attila +0x17f6937 / +0x17f6956 / +0x17f6963, Thrones +0x186ae7c / +0x186ae9b / +0x186aea8, Rome II +0x14b47d2 /
// +0x14b47df / +0x14b47ec):
// 17a. the sites and stubs as Prepare made them;
// 17b. each reload alone (ReloadAlone: random lanes of every float class, any MXCSR; CONTROLs with a lane 4 bytes off);
// 17c. both ways of the draw into the reloads: the game's code from each way's start (after its last branch) to the jump after the third
//    reload, which goes to the harness in both copies while this part runs, entered with random registers (eax the table index 0 .. 3,
//    ebx esi edi at random floats), a frame of random floats and any MXCSR: every register, xmm0-7, the flags, MXCSR, esp and the frame
//    equal with the three sites, and with each alone; CONTROL: each stub with each lane 4 bytes off must be seen;
// 17d. 200 switches while 4 threads run both ways in both copies;
// 17e. a parked thread at each byte of each site;
// 17f. timing of both ways, game / ours / each site alone.
// 17g .. 17m. The matrix product's three row reloads (RunMatrixProductTests): the same parts, and the whole function on random matrices
//    instead of the ways.
// The whole draw is not called: it walks the scene's nodes through virtual calls, the thread's TLS block and globals a fake graph can't
// stand in for; its two ways hold every store the three reloads read (engine_code.hpp 12.).
// ====================================================================================================================

// A way's inputs: three 0x200-byte areas of floats that ebx, esi and edi point at (the node's matrices, the transform, the object's
// floats at +0xe0 .. +0xf4), every third round with some of every class.
struct RowsData {
  alignas(16) std::array<uint8_t, 0x600> bytes;
};

// One way's run as the harness enters it: every register, the flags, MXCSR, xmm0-7 and the frame (floats) at random; eax the table index.
void RowsRandom(ReloadBlockCall* call, RowsData* data, Random* random, int round) {
  const bool nasty = (round % 3 == 0);
  const auto value = [&] { return nasty ? ReloadFloat(random) : ReloadOrdinary(random); };
  for (size_t at = 0; at < data->bytes.size(); at += 4) Write32(data->bytes.data() + at, value());
  for (uint32_t& reg : call->in) reg = random->Next();
  call->in[0] = random->Next() % 4;
  call->in[3] = Address(data->bytes.data());
  call->in[4] = Address(data->bytes.data() + 0x200);
  call->in[5] = Address(data->bytes.data() + 0x400);
  call->flags_in = (random->Next() & 0x8D5) | 2;
  call->mxcsr_in = ReloadMxcsr(random, round);
  call->lanes_at = 0x90;  // the way's own stores overwrite these before the reloads
  for (uint32_t& lane : call->lanes) lane = value();
  for (size_t at = 0; at < sizeof(call->frame); at += 4) Write32(call->frame + at, value());
  for (uint8_t& byte : std::span(&call->xmm_in[0][0], 128)) byte = static_cast<uint8_t>(random->Next());
}

int RunSoldierRowsTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  const uint32_t rows_rva = build.code->soldier_rows_rva;
  const uint32_t end_rva = rows_rva + static_cast<uint32_t>((build.code->soldier_rows.size() + 1) / 3) - 5;  // the jump after the third reload
  constexpr std::array<size_t, 3> ROWS = {ec::SOLDIER_ROWS_FIRST_SITE, ec::SOLDIER_ROWS_FIRST_SITE + 1, ec::SOLDIER_ROWS_FIRST_SITE + 2};

  // 17a. The sites and stubs as Prepare made them: the game's `movups xmm0, [esp + 0x90 / 0xa0 / 0xb0]`, found and written (the jump in
  // the first 5 bytes, the last 3 as the game's), each stub the generator's bytes in its slot, its jump back after the 8 bytes.
  CHECK(game[end_rva] == 0xE9);
  for (size_t row = 0; row < ROWS.size(); ++row) {
    const ec::Site& site = ec::sites[ROWS[row]];
    const uint32_t site_rva = rows_rva + build.code->soldier_rows_at[row];
    const uint8_t* stub = ec::page + ec::SOLDIER_ROWS_PAGE + row * ec::FLOAT4_RELOAD_STRIDE;
    const std::array<uint8_t, 8> movups = {0x0F, 0x10, 0x84, 0x24, static_cast<uint8_t>(0x90 + 0x10 * row), 0x00, 0x00, 0x00};
    const size_t code_size = ec::SOLDIER_ROWS_STUBS[row].size() - 5;
    CHECK(site.at == patched + site_rva && ((ec::written >> ROWS[row]) & 1) != 0);
    CHECK(std::memcmp(game + site_rva, movups.data(), movups.size()) == 0 && std::memcmp(site.game.data(), movups.data(), 5) == 0);
    CHECK(patched[site_rva] == 0xE9 && Address(patched + site_rva + 5) + Read32(patched + site_rva + 1) == Address(stub));
    CHECK(std::memcmp(patched + site_rva + 5, movups.data() + 5, 3) == 0);
    CHECK(std::memcmp(stub, ec::SOLDIER_ROWS_STUBS[row].data(), code_size) == 0);
    CHECK(stub[code_size] == 0xE9 && Address(stub + code_size + 5) + Read32(stub + code_size + 1) == Address(patched + site_rva + 8));
  }
  std::printf("17. soldier draw matrix row reloads: the three sites (+%X, +%X, +%X in the rows) found and written, the stubs jump back after them\n",
              build.code->soldier_rows_at[0], build.code->soldier_rows_at[1], build.code->soldier_rows_at[2]);

  // 17b. Each reload alone.
  Random random = {.state = 0x17B};
  for (size_t row = 0; row < ROWS.size(); ++row) {
    const uint32_t site_rva = rows_rva + build.code->soldier_rows_at[row];
    if (const int failed = ReloadAlone(ROWS[row], game + site_rva, ec::SOLDIER_ROW_LOAD_SIZE, ec::SOLDIER_ROWS_STUBS[row].size(), 0x90 + 0x10 * static_cast<uint32_t>(row),
                                       RELOAD_FAR_DISPLACEMENT_AT, quick ? 200'000 : 1'000'000, quick ? 100 : 500, &random);
        failed != 0) {
      return failed;
    }
  }

  // 17c. Both ways in both copies, the jump after the third reload to the harness.
  std::array<uint8_t, 5> game_end = {};
  std::array<uint8_t, 5> patched_end = {};
  std::memcpy(game_end.data(), game + end_rva, 5);
  std::memcpy(patched_end.data(), patched + end_rva, 5);
  JumpTo(game + end_rva, reinterpret_cast<const void*>(&ReloadBlockReturn));
  JumpTo(patched + end_rva, reinterpret_cast<const void*>(&ReloadBlockReturn));
  auto input = std::make_unique<ReloadBlockCall>();
  auto game_call = std::make_unique<ReloadBlockCall>();
  auto ours_call = std::make_unique<ReloadBlockCall>();
  auto data = std::make_unique<RowsData>();
  auto data_before = std::make_unique<RowsData>();
  CHECK(Address(input.get()) % 16 == 0 && Address(game_call.get()) % 16 == 0 && Address(ours_call.get()) % 16 == 0);
  // One run of a way: the game's copy into game_call, ours into ours_call; true if equal and the inputs untouched.
  const auto way_run = [&](int way, ReloadBlockCall* game_side, ReloadBlockCall* ours_side, const ReloadBlockCall& from) {
    *game_side = from;
    *ours_side = from;
    game_side->target = Address(game + build.soldier_ways[way]);
    ours_side->target = Address(patched + build.soldier_ways[way]);
    EnterReloadBlock(game_side);
    EnterReloadBlock(ours_side);
    return ReloadBlockEqual(*game_side, *ours_side);
  };
  {
    constexpr std::array<uint32_t, 4> VARIANTS = {ec::SOLDIER_ROWS_SITES, 1u << ROWS[0], 1u << ROWS[1], 1u << ROWS[2]};  // all three, each alone
    const int rounds = (quick ? 40'000 : 200'000);
    std::array<uint64_t, 4> wrong = {};
    uint64_t touched = 0;
    for (int round = 0; round < rounds; ++round) {
      RowsRandom(input.get(), data.get(), &random, round);
      *data_before = *data;
      for (size_t variant = 0; variant < VARIANTS.size(); ++variant) {
        HoldSites(VARIANTS[variant], ec::SOLDIER_ROWS_SITES);
        if (!way_run(round % 2, game_call.get(), ours_call.get(), *input)) {
          ++wrong[variant];
          Report("  soldier rows way %d: round %d, sites %X NOT equal\n", round % 2, round, VARIANTS[variant] >> ec::SOLDIER_ROWS_FIRST_SITE);
        }
      }
      touched += (data->bytes != data_before->bytes) ? 1 : 0;
    }
    HoldSites(ec::SOLDIER_ROWS_SITES, ec::SOLDIER_ROWS_SITES);
    std::printf("   both ways into the reloads (%d runs, each way half): NOT equal with the three sites %llu, the first alone %llu, the second %llu, the third %llu; inputs changed %llu\n",
                rounds, wrong[0], wrong[1], wrong[2], wrong[3], touched);
    CHECK(wrong[0] == 0 && wrong[1] == 0 && wrong[2] == 0 && wrong[3] == 0 && touched == 0);
  }
  // CONTROL: each stub with each lane 4 bytes off, both ways, must be seen.
  {
    DWORD protection;
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection) != FALSE);
    std::string line;
    for (size_t row = 0; row < ROWS.size(); ++row) {
      uint8_t* stub = ec::page + ec::SOLDIER_ROWS_PAGE + row * ec::FLOAT4_RELOAD_STRIDE;
      for (size_t lane = 0; lane < 4; ++lane) {
        stub[RELOAD_FAR_DISPLACEMENT_AT[lane]] += 4;
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        int seen = 0;
        for (int round = 0; round < 1000; ++round) {
          RowsRandom(input.get(), data.get(), &random, 1);
          seen += way_run(round % 2, game_call.get(), ours_call.get(), *input) ? 0 : 1;
        }
        stub[RELOAD_FAR_DISPLACEMENT_AT[lane]] -= 4;
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        line += std::format("{}{}", line.empty() ? "" : " ", seen);
        CHECK(seen > 950);
      }
    }
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READ, &protection) != FALSE);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    std::printf("   CONTROL, each stub's lane 0 .. 3 from the wrong address (rows 1, 2, 3): seen in %s of 1000 runs\n", line.c_str());
  }

  // 17d. The rows switched with the row (CPU code) on and off 200 times while 4 threads run both ways in both copies.
  {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> runs = 0;
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        auto thread_input = std::make_unique<ReloadBlockCall>();
        auto thread_game = std::make_unique<ReloadBlockCall>();
        auto thread_ours = std::make_unique<ReloadBlockCall>();
        auto thread_data = std::make_unique<RowsData>();
        Random thread_random = {.state = 0x17D0ull + static_cast<uint64_t>(worker)};
        for (int round = 0; !stop.load(); ++round) {
          RowsRandom(thread_input.get(), thread_data.get(), &thread_random, round);
          if (!way_run(round % 2, thread_game.get(), thread_ours.get(), *thread_input)) ++bad;
          ++runs;
        }
      });
    }
    int busy = 0;
    bool on = true;
    for (int switches = 0; switches < 200; ++switches) {
      on = !on;
      int tries = 0;
      do {
        twattila::engine_code::Update(on, twattila::engine_code::Game::ATTILA, false, false, true, true);
        ++tries;
      } while ((((ec::written >> ROWS[0]) & 1) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
    }
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("   200 switches while 4 threads made %llu runs of both ways in each copy: %llu wrong (%d writes waited for a thread inside a site)\n", runs.load(), bad.load(), busy);
    CHECK(bad.load() == 0 && (ec::written & ec::SOLDIER_ROWS_SITES) == ec::SOLDIER_ROWS_SITES);
  }

  // 17e. A parked thread at each byte of each site (-1 .. +9: the 8-byte movups and the byte after it).
  if (const int failed = ParkedAtSites(ec::SOLDIER_ROWS_SITES, 9); failed != 0) return failed;
  std::printf("   a parked thread at each site -1 .. +9: refused at +1 .. +4 and written at the others\n");

  // 17f. Timing of each way (the block call's own cost included), the best of 5: game / ours / each site alone.
  {
    RowsRandom(input.get(), data.get(), &random, 1);
    input->mxcsr_in = 0x1F80;
    std::string line;
    constexpr std::array<uint32_t, 5> VARIANTS = {0, ec::SOLDIER_ROWS_SITES, 1u << ROWS[0], 1u << ROWS[1], 1u << ROWS[2]};
    for (int way = 0; way < 2; ++way) {
      std::array<double, VARIANTS.size()> best = {1e9, 1e9, 1e9, 1e9, 1e9};
      for (int round = 0; round < 5; ++round) {
        for (size_t variant = 0; variant < VARIANTS.size(); ++variant) {
          HoldSites(VARIANTS[variant], ec::SOLDIER_ROWS_SITES);
          *ours_call = *input;
          ours_call->target = Address(patched + build.soldier_ways[way]);
          constexpr int RUNS = 200'000;
          const auto start = std::chrono::steady_clock::now();
          for (int run = 0; run < RUNS; ++run) EnterReloadBlock(ours_call.get());
          best[variant] = std::min(best[variant], std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / RUNS);
        }
      }
      line += std::format("{}way {}: {:.1f} / {:.1f} / {:.1f} / {:.1f} / {:.1f}", line.empty() ? "" : ", ", way + 1, best[0], best[1], best[2], best[3], best[4]);
    }
    HoldSites(ec::SOLDIER_ROWS_SITES, ec::SOLDIER_ROWS_SITES);
    std::printf("   ns a run of a way, game / ours / row 1 alone / row 2 alone / row 3 alone: %s\n", line.c_str());
  }

  std::memcpy(game + end_rva, game_end.data(), 5);
  std::memcpy(patched + end_rva, patched_end.data(), 5);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  return 0;
}

// ---- the matrix product (engine_code.hpp 13.) ----
// The product's world in a ReloadWorld's bytes: this at 0 (+1 the done byte, +4 its count, +8 its matrices), the argument at 0x20 (+8 its
// count, +0xc its matrices), this' matrices at 0x100, the argument's at 0x600, up to PRODUCT_MAX of 0x30 bytes each.
constexpr uint32_t PRODUCT_THIS = 0x000, PRODUCT_ARGUMENT = 0x020, PRODUCT_LEFT = 0x100, PRODUCT_RIGHT = 0x600, PRODUCT_MAX = 24;
static_assert(PRODUCT_LEFT + PRODUCT_MAX * 0x30 <= PRODUCT_RIGHT && PRODUCT_RIGHT + PRODUCT_MAX * 0x30 <= RELOAD_WORLD_SIZE);

// One call on random matrices (every third round some of every float class), counts 0 .. PRODUCT_MAX (mostly the same two), the done
// byte set one call in 8; returns the way the game's call takes: 0 done already, 1 no matrix, 2 the loop.
int ProductRandom(ReloadWorld* world, Random* random, uint32_t function, int round, bool canary) {
  uint8_t* bytes = world->bytes.data();
  const uint32_t base = Address(bytes);
  const bool nasty = (round % 3 == 0);
  for (uint32_t at = 0; at < RELOAD_WORLD_SIZE; at += 4) Write32(bytes + at, nasty ? ReloadFloat(random) : ReloadOrdinary(random));
  const uint32_t count = random->Next() % (PRODUCT_MAX + 1);
  const uint32_t other = (random->Next() % 4 == 0 ? random->Next() % (PRODUCT_MAX + 1) : count);
  const bool done = (random->Next() % 8 == 0);
  bytes[PRODUCT_THIS + 1] = (done ? static_cast<uint8_t>(1 + random->Next() % 255) : 0);
  Write32(bytes + PRODUCT_THIS + 4, count);
  Write32(bytes + PRODUCT_THIS + 8, base + PRODUCT_LEFT);
  Write32(bytes + PRODUCT_ARGUMENT + 8, other);
  Write32(bytes + PRODUCT_ARGUMENT + 0xC, base + PRODUCT_RIGHT);
  FnCall* call = &world->call;
  call->function = function;
  call->this_ptr = base + PRODUCT_THIS;
  call->args[0] = base + PRODUCT_ARGUMENT;
  call->args[1] = random->Next();
  call->args[2] = random->Next();
  call->args[3] = random->Next();
  ReloadCallRandom(call, random, ReloadMxcsr(random, round), canary);
  if (done) return 0;
  return (std::min(count, other) == 0 ? 1 : 2);
}

// The product on a thread's own world (fastcall carries this in ecx; the argument goes on the stack, popped by ret 4).
using ProductCall = void(__fastcall*)(void* self, void* unused_edx, const void* argument);

// 17g .. 17m: the matrix product's three row reloads, each an `movups xmm0, [esp + 0x18 / 0x28 / 0x38]` (Attila +0x168d7f0 / +0x168d803 /
// +0x168d812, Thrones +0x16faf10 .., Rome II +0x135b7b0 ..): the sites; each alone; the whole function on random matrices with the three
// sites and each alone (registers, x87 / MXCSR / xmm, flags, esp, the stack, the matrices and the objects equal); CONTROLs per stub and
// lane; 200 switches under 4 threads; a parked thread at each byte; timing.
int RunMatrixProductTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  const uint32_t function_rva = build.code->matrix_product_rva;
  const uint32_t copy_distance = static_cast<uint32_t>(patched - game);
  constexpr std::array<size_t, 3> ROWS = {ec::MATRIX_PRODUCT_FIRST_SITE, ec::MATRIX_PRODUCT_FIRST_SITE + 1, ec::MATRIX_PRODUCT_FIRST_SITE + 2};

  // 17g. The sites and stubs as Prepare made them.
  for (size_t row = 0; row < ROWS.size(); ++row) {
    const ec::Site& site = ec::sites[ROWS[row]];
    const uint32_t site_rva = function_rva + static_cast<uint32_t>(ec::MATRIX_PRODUCT_AT[row]);
    const uint8_t* stub = ec::page + ec::MATRIX_PRODUCT_PAGE + row * ec::FLOAT4_RELOAD_STRIDE;
    const std::array<uint8_t, 5> movups = {0x0F, 0x10, 0x44, 0x24, static_cast<uint8_t>(0x18 + 0x10 * row)};
    const size_t code_size = ec::MATRIX_PRODUCT_STUBS[row].size() - 5;
    CHECK(site.at == patched + site_rva && ((ec::written >> ROWS[row]) & 1) != 0);
    CHECK(std::memcmp(game + site_rva, movups.data(), movups.size()) == 0 && std::memcmp(site.game.data(), movups.data(), 5) == 0);
    CHECK(patched[site_rva] == 0xE9 && Address(patched + site_rva + 5) + Read32(patched + site_rva + 1) == Address(stub));
    CHECK(std::memcmp(stub, ec::MATRIX_PRODUCT_STUBS[row].data(), code_size) == 0);
    CHECK(stub[code_size] == 0xE9 && Address(stub + code_size + 5) + Read32(stub + code_size + 1) == Address(patched + site_rva + 5));
  }
  std::printf("17. matrix product row reloads: the three sites (+%zX, +%zX, +%zX in the function) found and written, the stubs jump back after them\n",
              ec::MATRIX_PRODUCT_AT[0], ec::MATRIX_PRODUCT_AT[1], ec::MATRIX_PRODUCT_AT[2]);

  // 17h. Each reload alone.
  Random random = {.state = 0x17E};
  for (size_t row = 0; row < ROWS.size(); ++row) {
    if (const int failed = ReloadAlone(ROWS[row], game + function_rva + ec::MATRIX_PRODUCT_AT[row], ec::SITE_SIZE, ec::MATRIX_PRODUCT_STUBS[row].size(),
                                       0x18 + 0x10 * static_cast<uint32_t>(row), RELOAD_NEAR_DISPLACEMENT_AT, quick ? 200'000 : 1'000'000, quick ? 100 : 500, &random);
        failed != 0) {
      return failed;
    }
  }

  // 17i. The whole function, the game's copy against the patched copy with the three sites, and with each alone.
  auto world = std::make_unique<ReloadWorld>();
  auto backup = std::make_unique<ReloadWorld>();
  auto result_a = std::make_unique<ReloadWorld>();
  auto result_b = std::make_unique<ReloadWorld>();
  const uint32_t game_function = Address(game + function_rva);
  const uint32_t patched_function = Address(patched + function_rva);
  {
    constexpr std::array<uint32_t, 4> VARIANTS = {ec::MATRIX_PRODUCT_SITES, 1u << ROWS[0], 1u << ROWS[1], 1u << ROWS[2]};  // all three, each alone
    const int calls = (quick ? 60'000 : 400'000);
    std::array<uint64_t, 3> ways = {};
    std::array<uint64_t, 4> wrong = {};
    for (int round = 0; round < calls; ++round) {
      ++ways[ProductRandom(world.get(), &random, game_function, round, round % 4 != 3)];
      *backup = *world;
      RunReload(world.get(), *backup, result_a.get());
      backup->call.function = patched_function;
      for (size_t variant = 0; variant < VARIANTS.size(); ++variant) {
        HoldSites(VARIANTS[variant], ec::MATRIX_PRODUCT_SITES);
        RunReload(world.get(), *backup, result_b.get());
        if (const char* what = ReloadDiff(*result_a, *result_b, copy_distance); what[0] != 0) {
          ++wrong[variant];
          Report("  matrix product: round %d, sites %X: %s differs\n", round, VARIANTS[variant] >> ec::MATRIX_PRODUCT_FIRST_SITE, what);
        }
      }
    }
    HoldSites(ec::MATRIX_PRODUCT_SITES, ec::MATRIX_PRODUCT_SITES);
    std::printf("   the whole function: %d calls (done already %llu, no matrix %llu, the loop %llu): NOT equal with the three sites %llu, the first alone %llu, the second %llu, the third %llu\n",
                calls, ways[0], ways[1], ways[2], wrong[0], wrong[1], wrong[2], wrong[3]);
    CHECK(wrong[0] == 0 && wrong[1] == 0 && wrong[2] == 0 && wrong[3] == 0);
    const uint64_t at_least = (quick ? 1500 : 10000);
    CHECK(ways[0] > at_least && ways[1] > at_least && ways[2] > at_least);
  }

  // 17j. CONTROL for the whole function: each stub with each lane 4 bytes off, on calls that take the loop, must be seen.
  {
    DWORD protection;
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection) != FALSE);
    std::string line;
    for (size_t row = 0; row < ROWS.size(); ++row) {
      uint8_t* stub = ec::page + ec::MATRIX_PRODUCT_PAGE + row * ec::FLOAT4_RELOAD_STRIDE;
      for (size_t lane = 0; lane < 4; ++lane) {
        stub[RELOAD_NEAR_DISPLACEMENT_AT[lane]] += 4;
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        int seen = 0;
        for (int round = 0; round < 1000; ++round) {
          while (ProductRandom(world.get(), &random, game_function, 1, true) != 2) {
          }
          *backup = *world;
          RunReload(world.get(), *backup, result_a.get());
          backup->call.function = patched_function;
          RunReload(world.get(), *backup, result_b.get());
          seen += (ReloadDiff(*result_a, *result_b, copy_distance)[0] != 0) ? 1 : 0;
        }
        stub[RELOAD_NEAR_DISPLACEMENT_AT[lane]] -= 4;
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        line += std::format("{}{}", line.empty() ? "" : " ", seen);
        CHECK(seen > 950);
      }
    }
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READ, &protection) != FALSE);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    std::printf("   CONTROL, each stub's lane 0 .. 3 from the wrong address (rows 1, 2, 3), calls that take the loop: seen in %s of 1000\n", line.c_str());
  }

  // 17k. The product switched with the row (CPU code) on and off 200 times while 4 threads call both copies on their own matrices.
  {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> calls = 0;
    const auto game_product = reinterpret_cast<ProductCall>(game + function_rva);
    const auto patched_product = reinterpret_cast<ProductCall>(patched + function_rva);
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        auto thread_world = std::make_unique<ReloadWorld>();
        auto before = std::make_unique<ReloadWorld>();
        auto after_game = std::make_unique<ReloadWorld>();
        Random thread_random = {.state = 0x17F0ull + static_cast<uint64_t>(worker)};
        uint8_t* bytes = thread_world->bytes.data();
        for (int round = 0; !stop.load(); ++round) {
          ProductRandom(thread_world.get(), &thread_random, 0, round, false);
          before->bytes = thread_world->bytes;
          game_product(bytes + PRODUCT_THIS, nullptr, bytes + PRODUCT_ARGUMENT);
          after_game->bytes = thread_world->bytes;
          thread_world->bytes = before->bytes;
          patched_product(bytes + PRODUCT_THIS, nullptr, bytes + PRODUCT_ARGUMENT);
          if (thread_world->bytes != after_game->bytes) ++bad;
          ++calls;
        }
      });
    }
    int busy = 0;
    bool on = true;
    for (int switches = 0; switches < 200; ++switches) {
      on = !on;
      int tries = 0;
      do {
        twattila::engine_code::Update(on, twattila::engine_code::Game::ATTILA, false, false, true, true);
        ++tries;
      } while ((((ec::written >> ROWS[0]) & 1) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
    }
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("   200 switches while 4 threads made %llu calls of each copy on their own matrices: %llu wrong (%d writes waited for a thread inside a site)\n", calls.load(), bad.load(), busy);
    CHECK(bad.load() == 0 && (ec::written & ec::MATRIX_PRODUCT_SITES) == ec::MATRIX_PRODUCT_SITES);
  }

  // 17l. A parked thread at each byte of each site.
  if (const int failed = ParkedAtSites(ec::MATRIX_PRODUCT_SITES, 6); failed != 0) return failed;
  std::printf("   a parked thread at each site -1 .. +6: refused at +1 .. +4 and written at the others\n");

  // 17m. Timing: PRODUCT_MAX matrices times identities (the values stay as they are call after call), the done byte cleared before each call;
  // the patched copy with the game's bytes / ours / each site alone, the best of 5.
  {
    ProductRandom(world.get(), &random, 0, 1, false);
    uint8_t* bytes = world->bytes.data();
    Write32(bytes + PRODUCT_THIS + 4, PRODUCT_MAX);
    Write32(bytes + PRODUCT_ARGUMENT + 8, PRODUCT_MAX);
    for (uint32_t matrix = 0; matrix < PRODUCT_MAX; ++matrix) {
      for (uint32_t element = 0; element < 12; ++element) {
        Write32(bytes + PRODUCT_RIGHT + 0x30 * matrix + 4 * element, (element % 5 == 0 ? 0x3F800000u : 0u));
      }
    }
    const auto patched_product = reinterpret_cast<ProductCall>(patched + function_rva);
    constexpr std::array<uint32_t, 5> VARIANTS = {0, ec::MATRIX_PRODUCT_SITES, 1u << ROWS[0], 1u << ROWS[1], 1u << ROWS[2]};
    std::array<double, VARIANTS.size()> best = {1e9, 1e9, 1e9, 1e9, 1e9};
    for (int round = 0; round < 5; ++round) {
      for (size_t variant = 0; variant < VARIANTS.size(); ++variant) {
        HoldSites(VARIANTS[variant], ec::MATRIX_PRODUCT_SITES);
        constexpr int CALLS = 200'000;
        const auto start = std::chrono::steady_clock::now();
        for (int call = 0; call < CALLS; ++call) {
          bytes[PRODUCT_THIS + 1] = 0;
          patched_product(bytes + PRODUCT_THIS, nullptr, bytes + PRODUCT_ARGUMENT);
        }
        best[variant] = std::min(best[variant], std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / CALLS);
      }
    }
    HoldSites(ec::MATRIX_PRODUCT_SITES, ec::MATRIX_PRODUCT_SITES);
    std::printf("   ns a call of %u products, game / ours / row 1 alone / row 2 alone / row 3 alone: %.1f / %.1f / %.1f / %.1f / %.1f\n", PRODUCT_MAX, best[0], best[1], best[2],
                best[3], best[4]);
  }
  return 0;
}
