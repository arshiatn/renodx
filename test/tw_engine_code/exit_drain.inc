// ====================================================================================================================
// 30. Exit Job Drain (Attila +0xC9AC1 the app destruction's call; engine_code.hpp 21.), included inside tests.cpp's anonymous namespace after instance_fill.inc (Read32, Write32, Matches,
// CHECK, log_lines are used). Attila only; the other builds have no such site (their rvas are 0). A bug fix: always on (WorkerRows::exit_job_drain), no row.
// 30a. In the game's code (A): the function's pattern once, the app delete's callers exactly two (the site and the creation's failure path, outside the function), the pool accessor's two copies
//    loading one global; as Prepare made it (B): the site, its E8 and its jump into ExitDrain, the pattern's bytes;
// 30b. No patch: the signature missing, the app delete not as expected, the pool's accessor missing, the pool's global outside the image: the site not found, one log line when asked for, written 0;
// 30c. The site written and taken out, the game's bytes back;
// 30d. The drain on a fake job pool, the call made as the game's code makes it (the site's call): workers running and a wake pending are waited for (the game's delete sees every worker
//    asleep), everyone asleep: no wait, a stopped worker: no wait, a worker that never sleeps: the cap (1000 ms), no pool / a pool that is not as expected: no wait; always the game's own
//    delete after (it clears the app's global), the drain only once (a second call goes straight on).
// ====================================================================================================================

std::atomic<int> exit_fake_deletes = 0;
std::atomic<int> exit_fake_busy_at_delete = -1;
std::array<uint8_t*, 8> exit_fake_workers = {};
uint32_t exit_fake_count = 0;

// The app object's destructor entry (the game's delete calls [[object]] with the flag 1, thiscall).
void __thiscall ExitFakeDelete(void*, int) {
  int busy = 0;
  for (uint32_t index = 0; index < exit_fake_count; ++index) {
    const uint32_t state = Read32(exit_fake_workers[index] + 0x18);
    if (state != 3 && (state != 1 || Read32(exit_fake_workers[index] + 0x50) != 0)) ++busy;
  }
  exit_fake_busy_at_delete = busy;
  ++exit_fake_deletes;
}

int RunExitDrainTests(const TestBuild& build, uint8_t* game, uint8_t* patched, uint32_t text_rva, uint32_t text_size) {
  namespace ec = twattila::engine_code::internal;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  if (build.code != &ec::ATTILA_BUILD) {
    CHECK(build.code->exit_destroy_rva == 0 && Matches(game + text_rva, text_size, ec::EXIT_DESTROY) == 0);
    std::printf("30. exit job drain: Attila only (this build has no such site: skipped)\n");
    return 0;
  }
  const auto drain = [&](bool on) { twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 1.f, 0, {.exit_job_drain = on}); };
  drain(false);
  CHECK(ec::written == 0);
  const std::vector<uint8_t> unpatched(patched + text_rva, patched + text_rva + text_size);
  const uint32_t function_rva = build.code->exit_destroy_rva;
  const uint32_t site_rva = function_rva + ec::EXIT_DESTROY_CALL;
  const std::array<uint32_t, 2> accessor_rvas = {build.code->job_pool_rva, 0x16b1400};
  const uint32_t app_delete_rva = site_rva + 5 + Read32(game + site_rva + 1);
  const auto prepare = [&] {
    for (ec::Site& site : ec::sites) site.at = nullptr;
    ec::prepared = false;
    ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
  };
  const auto restore = [&] { std::memcpy(patched + text_rva, unpatched.data(), text_size); };

  // 30a. The game's code.
  CHECK(Matches(game + text_rva, text_size, ec::EXIT_DESTROY) == 1 && Matches(game + function_rva, ec::PatternSize(ec::EXIT_DESTROY), ec::EXIT_DESTROY) == 1);
  CHECK(game[site_rva] == 0xE8 && app_delete_rva == 0x1374230 && Matches(game + app_delete_rva, ec::PatternSize(ec::EXIT_APP_DELETE), ec::EXIT_APP_DELETE) == 1);
  CHECK(Matches(game + text_rva, text_size, ec::JOB_POOL_ACCESSOR) == 2);
  for (const uint32_t rva : accessor_rvas) CHECK(Matches(game + rva, ec::PatternSize(ec::JOB_POOL_ACCESSOR), ec::JOB_POOL_ACCESSOR) == 1);
  CHECK(Read32(game + accessor_rvas[0] + 1) == Read32(game + accessor_rvas[1] + 1));
  {
    // Every E8 of the code section into the app's delete: the site and one more, in the creation's failure path (not in the destroy function).
    std::vector<uint32_t> callers;
    for (uint32_t at = text_rva; at + 5 <= text_rva + text_size; ++at) {
      if (game[at] == 0xE8 && at + 5 + Read32(game + at + 1) == app_delete_rva) callers.push_back(at);
    }
    CHECK(callers.size() == 2 && (callers[0] == site_rva || callers[1] == site_rva));
    const uint32_t other = (callers[0] == site_rva ? callers[1] : callers[0]);
    CHECK(other < function_rva || other >= function_rva + ec::PatternSize(ec::EXIT_DESTROY));
    std::printf("30. exit job drain: the app delete +0x%X has 2 E8 callers: the site +0x%X and +0x%X (the creation's failure path, not patched)\n", app_delete_rva, site_rva, other);
  }
  prepare();
  CHECK(ec::sites[ec::EXIT_DRAIN_INDEX].at == patched + site_rva && ec::sites[ec::EXIT_DRAIN_INDEX].opcode == 0xE8 && (ec::found & ec::EXIT_DRAIN_BIT) != 0);
  CHECK(std::memcmp(ec::sites[ec::EXIT_DRAIN_INDEX].game.data(), patched + site_rva, ec::SITE_SIZE) == 0);
  CHECK(ec::sites[ec::EXIT_DRAIN_INDEX].jump[0] == 0xE8
        && Read32(ec::sites[ec::EXIT_DRAIN_INDEX].jump.data() + 1)
               == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ec::ExitDrain) - reinterpret_cast<uintptr_t>(patched + site_rva + ec::SITE_SIZE)));
  CHECK(ec::job_pool_global == Read32(patched + accessor_rvas[0] + 1) && reinterpret_cast<const uint8_t*>(ec::exit_app_delete) == patched + app_delete_rva);

  // 30b. Not the game's code as expected: the site is not found, nothing is written, one line says so.
  const auto left_alone = [&](const char* why) {
    prepare();
    CHECK(ec::sites[ec::EXIT_DRAIN_INDEX].at == nullptr && (ec::found & ec::EXIT_DRAIN_BIT) == 0);
    ec::prefetch_logged = 0;
    ec::asked = ~0u;
    const std::vector<uint8_t> before(patched + site_rva, patched + site_rva + 5);
    drain(true);
    CHECK(ec::written == 0 && std::memcmp(before.data(), patched + site_rva, 5) == 0);
    CHECK(log_lines.back().ends_with("Exit Job Drain: left alone (not the game's code as expected: another mod or build): exit job drain"));
    std::printf("   %s: left alone, one line\n", why);
    restore();
    return 0;
  };
  patched[function_rva + 3] ^= 1;
  if (left_alone("the function's signature changed") != 0) return 1;
  patched[app_delete_rva + 8] ^= 1;
  if (left_alone("the app's delete changed") != 0) return 1;
  for (const uint32_t rva : accessor_rvas) patched[rva + 9] ^= 1;
  if (left_alone("no job system accessor") != 0) return 1;
  for (const uint32_t rva : accessor_rvas) Write32(patched + rva + 1, 0x1000);
  if (left_alone("the job system's pointer outside the image") != 0) return 1;
  prepare();
  CHECK(ec::sites[ec::EXIT_DRAIN_INDEX].at == patched + site_rva);

  // 30c. Written and taken out.
  bool in = false;
  for (int tries = 0; !in && tries < static_cast<int>(ec::MAX_FAILED_WRITES); ++tries) {
    drain(true);
    in = (ec::written == ec::EXIT_DRAIN_BIT);
  }
  CHECK(in && std::memcmp(patched + site_rva, ec::sites[ec::EXIT_DRAIN_INDEX].jump.data(), 5) == 0);
  CHECK(log_lines.back().find("Exit Job Drain: on") != std::string::npos);
  size_t differing = 0;
  for (uint32_t at = text_rva; at < text_rva + text_size; ++at) {
    if (unpatched[at - text_rva] == patched[at]) continue;
    ++differing;
    CHECK(at > site_rva && at < site_rva + ec::SITE_SIZE);  // the rel32 only: the E8 stays
  }
  CHECK(differing >= 1);
  drain(false);
  CHECK(ec::written == 0 && std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
  for (int tries = 0; ec::written == 0 && tries < static_cast<int>(ec::MAX_FAILED_WRITES); ++tries) drain(true);
  CHECK(ec::written == ec::EXIT_DRAIN_BIT);
  std::printf("   written (%zu bytes differ, all the call's rel32) and taken out: the game's bytes back\n", differing);

  // 30d. The drain on a fake pool: the pool at 0, its workers' array at 0x100, the workers 0x60 bytes each from 0x200.
  auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  CHECK(memory != nullptr);
  const auto address = [](const void* at) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(at)); };
  const auto world = [&](uint32_t count, std::initializer_list<std::pair<uint32_t, uint32_t>> states) {  // the pairs: worker, state
    std::memset(memory, 0, 0x1000);
    Write32(memory + 4, count);
    Write32(memory + 8, address(memory + 0x100));
    exit_fake_count = count;
    for (uint32_t index = 0; index < count; ++index) {
      uint8_t* worker = memory + 0x200 + 0x60 * index;
      Write32(memory + 0x100 + 4 * index, address(worker));
      Write32(worker + 4, address(memory));
      Write32(worker + 8, index);
      Write32(worker + 0x18, 1);
      exit_fake_workers[index] = worker;
    }
    for (const auto& [index, state] : states) Write32(exit_fake_workers[index] + 0x18, state);
    Write32(reinterpret_cast<uint8_t*>(ec::job_pool_global), address(memory));
  };
  uint32_t* app_global = reinterpret_cast<uint32_t*>(Read32(reinterpret_cast<const uint8_t*>(ec::exit_app_delete) + 2));
  const std::array<uint32_t, 1> vtable = {address(reinterpret_cast<const void*>(&ExitFakeDelete))};
  const std::array<uint32_t, 1> app = {address(vtable.data())};
  // The game's call at the site, as the game's code makes it; the time it took in ms.
  const auto at_exit = [&] {
    uint8_t* site = ec::sites[ec::EXIT_DRAIN_INDEX].at;  // an E8 (checked in 30a)
    const auto call = reinterpret_cast<void (*)()>(site + 5 + static_cast<int32_t>(Read32(site + 1)));
    exit_fake_deletes = 0;
    exit_fake_busy_at_delete = -1;
    *app_global = address(app.data());
    const auto start = std::chrono::steady_clock::now();
    call();
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
  };
  const auto line = [&] { return log_lines.back(); };
  const auto deleted_once = [&] { return exit_fake_deletes.load() == 1 && *app_global == 0; };  // the game's delete ran and cleared the global

  // Everyone asleep: no wait.
  world(5, {});
  ec::game_exiting = false;
  int took = at_exit();
  CHECK(deleted_once() && exit_fake_busy_at_delete == 0 && took < 500);
  CHECK(line().find("Exit Job Drain: drained 0 of 5 job workers in ") != std::string::npos);
  // A stopped worker (3) is no wait.
  world(5, {{2, 3}});
  ec::game_exiting = false;
  took = at_exit();
  CHECK(deleted_once() && exit_fake_busy_at_delete == 0 && took < 500 && line().find("drained 0 of 5") != std::string::npos);
  // Workers 1 and 3 running (state 2) and worker 2 woken (state 1, a wake pending): waited for.
  world(5, {{1, 2}, {3, 2}});
  Write32(exit_fake_workers[2] + 0x50, 1);
  ec::game_exiting = false;
  {
    std::thread releaser([&] {
      Sleep(60);
      Write32(exit_fake_workers[1] + 0x18, 1);
      Sleep(30);
      Write32(exit_fake_workers[3] + 0x18, 1);
      Write32(exit_fake_workers[2] + 0x50, 0);
    });
    took = at_exit();
    releaser.join();
  }
  CHECK(deleted_once() && exit_fake_busy_at_delete == 0 && took >= 80 && took < 900);
  CHECK(line().find("Exit Job Drain: drained 3 of 5 job workers in ") != std::string::npos && line().ends_with(" ms."));
  std::printf("   a drain of 3 busy workers: waited %d ms, the game's delete saw all asleep; \"%s\"\n", took, line().c_str());
  // The drain only once: a second call goes straight on to the game's delete, no line.
  const size_t lines_before = log_lines.size();
  world(5, {{4, 2}});
  took = at_exit();
  CHECK(deleted_once() && exit_fake_busy_at_delete == 1 && took < 500 && log_lines.size() == lines_before);
  // A worker that never sleeps: the cap.
  world(5, {{4, 2}});
  ec::game_exiting = false;
  took = at_exit();
  CHECK(deleted_once() && exit_fake_busy_at_delete == 1 && took >= 1000 && took < 3000);
  CHECK(line().find("Exit Job Drain: gave up after ") != std::string::npos && line().find("still running: 4 (of 5).") != std::string::npos);
  std::printf("   the cap: waited %d ms, the game's delete went on; \"%s\"\n", took, line().c_str());
  // No pool / a pool not as expected: no wait.
  world(5, {{1, 2}});
  Write32(reinterpret_cast<uint8_t*>(ec::job_pool_global), 0);
  ec::game_exiting = false;
  took = at_exit();
  CHECK(deleted_once() && took < 500 && line().find("Exit Job Drain: no job system to wait for") != std::string::npos);
  world(5, {{1, 2}});
  Write32(exit_fake_workers[3] + 4, 0x1234);
  ec::game_exiting = false;
  took = at_exit();
  CHECK(deleted_once() && took < 500 && line().find("Exit Job Drain: the job workers were not as expected") != std::string::npos);
  world(0, {});
  ec::game_exiting = false;
  took = at_exit();
  CHECK(deleted_once() && took < 500 && line().find("no job system to wait for") != std::string::npos);
  std::printf("   no pool, a worker of another pool, no workers: no wait, the game's delete runs\n");

  drain(false);
  CHECK(ec::written == 0 && std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
  *app_global = 0;
  VirtualFree(memory, 0, MEM_RELEASE);
  std::printf("30. exit job drain: the site as Prepare made it, the four no-patch cases, in and out, the drain on a fake pool (waited for, the cap, no pool): PASS\n");
  return 0;
}
