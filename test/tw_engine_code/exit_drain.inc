// ====================================================================================================================
// 30. Exit Job Drain (Attila: +0xC9AC1 the app destruction's call, +0xD5D74 the outer loop's call of the root teardown +0xC9800; engine_code.hpp 21.), included inside tests.cpp's anonymous namespace after
// instance_fill.inc (Read32, Write32, Matches, CHECK, log_lines are used). Attila only; the other builds have no such sites (their rvas are 0). A bug fix: always on (WorkerRows::exit_job_drain), no row.
// 30a. In the game's code (A): each function's pattern once, the app delete's callers exactly two (the site and the creation's failure path, outside the function), the pool accessor's two copies
//    loading one global; the root teardown's callers: its pattern once in the code and once at +0xD5D5B, exactly two E8 (+0xD5D74 and the deleting destructor's +0xC9FF3), no E9, no relocated
//    absolute reference (the relocation entries of the mapped copy, with a control: the deleting destructor has one, the vtable's slot 0); as Prepare made it (B and A): the sites, their E8 and their
//    jump into ExitDrain / ExitDrainBeforeTeardown, the pattern's bytes, the root teardown as the game's function A calls;
// 30b. No patch: each site's own signature missing, the app delete not as expected, the root teardown's head changed (that site alone is not found, the other stands), the pool's accessor missing or
//    its global outside the image (neither is found): the sites not found are not written, one log line names them when asked for;
// 30c. Both sites written and taken out, the game's bytes back; another mod's bytes over either call while it goes out: they stay, that site stands down, the module is pinned (30e);
// 30d. The drain on a fake job pool, the call made as the game's code makes it (the site's call): workers running and a wake pending are waited for (the game's delete / the root teardown sees every
//    worker asleep), a job still queued (the pool's [0x10]) with every worker asleep is waited for too, everyone asleep and nothing queued: no wait, a stopped worker: no wait, a worker that never
//    sleeps / a job that stays queued: the cap (EXIT_DRAIN_LIMIT at +0xC9AC1, EXIT_TEARDOWN_DRAIN_LIMIT at +0xD5D74), no pool / a pool that is not as expected: no wait; always the game's own call after
//    (the delete clears the app's global; the teardown gets the loop object the outer loop passed), the drain only once (a second call goes straight on); the line of a drain that waited names
//    the workers busy at its first poll;
// 30f. The race, deterministic: a battle job that reads the battle until the end of its window while the fake teardown frees it. Control: no drain at +0xD5D74 (the teardown called directly): the job
//    sees the free and the later drain waits for it; through +0xD5D74: the job never sees the free, the drain at +0xD5D74 waited for it and the later drain found every worker asleep. On the fake
//    pool, then with Faster Job Wake-Ups on, on five of the game's own workers (its constructor, semaphores, Sleep through the row's stub, the job queued and the pool woken by its WakeAll).
// ====================================================================================================================

std::atomic<int> exit_fake_deletes = 0;
std::atomic<int> exit_fake_busy_at_delete = -1;
std::atomic<int> exit_fake_teardowns = 0;
std::atomic<int> exit_fake_busy_at_teardown = -1;
std::atomic<void*> exit_fake_teardown_self = nullptr;
std::function<void()> exit_fake_teardown_extra;  // what the fake root teardown does after recording (30f: frees the battle, then makes the app delete's call)
std::array<uint8_t*, 8> exit_fake_workers = {};
uint32_t exit_fake_count = 0;

// 30f with Faster Job Wake-Ups on: a pool of the game's own workers (its constructor and semaphores), one thread each that runs the game's Sleep (ours: the row's stub), then the queued jobs, until the game's
// Stop (state 3).
struct ExitJobPool {
  const JobCopy* copy = nullptr;
  std::array<uint8_t*, 8> workers = {};
  std::mutex lock;
  std::deque<std::function<void()>> jobs;
};
struct ExitJobThread {
  ExitJobPool* pool;
  uint32_t index;
};
DWORD WINAPI ExitJobThreadMain(void* parameter) {
  const auto* context = static_cast<const ExitJobThread*>(parameter);
  ExitJobPool* pool = context->pool;
  uint8_t* worker = pool->workers[context->index];
  for (;;) {
    pool->copy->sleep(worker, nullptr);
    if (*reinterpret_cast<volatile uint32_t*>(worker + 0x18) == 3) return 0;
    for (;;) {
      std::function<void()> job;
      {
        const std::unique_lock guard(pool->lock);
        if (pool->jobs.empty()) break;
        job = std::move(pool->jobs.front());
        pool->jobs.pop_front();
      }
      job();
    }
  }
}

// The fake pool's workers that are neither asleep with no wake pending (state 1, [+0x50] 0) nor stopped (3).
int ExitFakeBusy() {
  int busy = 0;
  for (uint32_t index = 0; index < exit_fake_count; ++index) {
    const uint32_t state = Read32(exit_fake_workers[index] + 0x18);
    if (state != 3 && (state != 1 || Read32(exit_fake_workers[index] + 0x50) != 0)) ++busy;
  }
  return busy;
}

// The app object's destructor entry (the game's delete calls [[object]] with the flag 1, thiscall).
void __thiscall ExitFakeDelete(void*, int) {
  exit_fake_busy_at_delete = ExitFakeBusy();
  ++exit_fake_deletes;
}

// The root teardown (the outer loop calls it thiscall with the loop object).
void __thiscall ExitFakeTeardown(void* self) {
  exit_fake_busy_at_teardown = ExitFakeBusy();
  exit_fake_teardown_self = self;
  ++exit_fake_teardowns;
  if (exit_fake_teardown_extra) exit_fake_teardown_extra();
}

int RunExitDrainTests(const TestBuild& build, uint8_t* game, uint8_t* patched, uint32_t text_rva, uint32_t text_size) {
  namespace ec = twattila::engine_code::internal;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  if (build.code != &ec::ATTILA_BUILD) {
    CHECK(build.code->exit_destroy_rva == 0 && Matches(game + text_rva, text_size, ec::EXIT_DESTROY) == 0);
    // The outer loop's pattern can match here (Thrones of Britannia has it once): the site is Attila's alone, so only the build's own rva must be 0.
    CHECK(build.code->exit_teardown_rva == 0);
    std::printf("30. exit job drain: Attila only (this build has no such sites: skipped; the outer loop's pattern matches %zu time(s) here)\n", Matches(game + text_rva, text_size, ec::EXIT_TEARDOWN_CALLER));
    return 0;
  }
  const auto drain = [&](bool on) { twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 1.f, 0, {.exit_job_drain = on}); };
  drain(false);
  CHECK(ec::written == 0);
  const std::vector<uint8_t> unpatched(patched + text_rva, patched + text_rva + text_size);
  const uint32_t function_rva = build.code->exit_destroy_rva;
  const uint32_t site_rva = function_rva + ec::EXIT_DESTROY_CALL;
  const uint32_t teardown_caller_rva = build.code->exit_teardown_rva;
  const uint32_t teardown_site_rva = teardown_caller_rva + ec::EXIT_TEARDOWN_CALL;
  const uint32_t teardown_rva = ec::EXIT_ROOT_TEARDOWN_RVA;
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
  {
    // The root teardown: the outer loop's pattern once in the code and once at its place, the call's target is the teardown, which holds its pattern.
    CHECK(Matches(game + text_rva, text_size, ec::EXIT_TEARDOWN_CALLER) == 1
          && Matches(game + teardown_caller_rva, ec::PatternSize(ec::EXIT_TEARDOWN_CALLER), ec::EXIT_TEARDOWN_CALLER) == 1);
    CHECK(game[teardown_site_rva] == 0xE8 && teardown_site_rva + 5 + Read32(game + teardown_site_rva + 1) == teardown_rva
          && Matches(game + teardown_rva, ec::PatternSize(ec::EXIT_ROOT_TEARDOWN), ec::EXIT_ROOT_TEARDOWN) == 1);
    // Every way into the teardown: the E8 calls (the site and the deleting destructor's), no E9 and no 0F 80..8F (conditional rel32) jump, and no relocated address of it
    // (a vtable or table entry).
    constexpr uint32_t DELETING_DESTRUCTOR_RVA = 0xC9FF0;
    constexpr uint32_t DELETING_DESTRUCTOR_VTABLE_RVA = 0x1AFCFA8;  // slot 0 of the vtable at 0x11AFCFA8
    std::vector<uint32_t> calls;
    size_t jumps = 0;
    for (uint32_t at = text_rva; at + 5 <= text_rva + text_size; ++at) {
      if (game[at] == 0xE8 && at + 5 + Read32(game + at + 1) == teardown_rva) {
        calls.push_back(at);
      } else if (game[at] == 0xE9 && at + 5 + Read32(game + at + 1) == teardown_rva) {
        ++jumps;
      } else if (game[at] == 0x0F && (game[at + 1] & 0xF0) == 0x80 && at + 6 + Read32(game + at + 2) == teardown_rva) {
        ++jumps;
      }
    }
    CHECK(calls.size() == 2 && jumps == 0 && (calls[0] == teardown_site_rva || calls[1] == teardown_site_rva));
    const uint32_t destructor_call = (calls[0] == teardown_site_rva ? calls[1] : calls[0]);
    CHECK(destructor_call == DELETING_DESTRUCTOR_RVA + 3);
    // The relocated copy's base, never the preferred one: the relocation entries (as Map walks them) give every dword that holds an address of the image.
    const uint32_t game_base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(game));
    const auto* game_nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(game + reinterpret_cast<const IMAGE_DOS_HEADER*>(game)->e_lfanew);
    const auto& relocations = game_nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    const auto absolute_references = [&](uint32_t rva) {
      size_t count = 0;
      for (uint32_t at = 0; at < relocations.Size;) {
        const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(game + relocations.VirtualAddress + at);
        if (block->SizeOfBlock == 0) break;
        const auto* entries = reinterpret_cast<const uint16_t*>(block + 1);
        for (size_t entry = 0; entry < (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2; ++entry) {
          if ((entries[entry] >> 12) == IMAGE_REL_BASED_HIGHLOW && Read32(game + block->VirtualAddress + (entries[entry] & 0xFFF)) == game_base + rva) ++count;
        }
        at += block->SizeOfBlock;
      }
      return count;
    };
    CHECK(Read32(game + DELETING_DESTRUCTOR_VTABLE_RVA) == game_base + DELETING_DESTRUCTOR_RVA);
    CHECK(absolute_references(DELETING_DESTRUCTOR_RVA) >= 1);  // the control: the scan finds the vtable's slot, so a zero below means none
    CHECK(absolute_references(teardown_rva) == 0);
    std::printf("30. exit job drain: the root teardown +0x%X: the pattern once, 2 E8 callers (the site +0x%X, the deleting destructor's +0x%X), no E9 or 0F 8x jump, no relocated address (the destructor +0x%X has %zu)\n",
                teardown_rva, teardown_site_rva, destructor_call, DELETING_DESTRUCTOR_RVA, absolute_references(DELETING_DESTRUCTOR_RVA));
  }
  prepare();
  CHECK(ec::sites[ec::EXIT_DRAIN_INDEX].at == patched + site_rva && ec::sites[ec::EXIT_DRAIN_INDEX].opcode == 0xE8 && (ec::found & ec::EXIT_DRAIN_BIT) != 0);
  CHECK(std::memcmp(ec::sites[ec::EXIT_DRAIN_INDEX].game.data(), patched + site_rva, ec::SITE_SIZE) == 0);
  CHECK(ec::sites[ec::EXIT_DRAIN_INDEX].jump[0] == 0xE8
        && Read32(ec::sites[ec::EXIT_DRAIN_INDEX].jump.data() + 1)
               == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ec::ExitDrain) - reinterpret_cast<uintptr_t>(patched + site_rva + ec::SITE_SIZE)));
  CHECK(ec::job_pool_global == Read32(patched + accessor_rvas[0] + 1) && reinterpret_cast<const uint8_t*>(ec::exit_app_delete) == patched + app_delete_rva);
  CHECK(ec::sites[ec::EXIT_TEARDOWN_INDEX].at == patched + teardown_site_rva && ec::sites[ec::EXIT_TEARDOWN_INDEX].opcode == 0xE8 && (ec::found & ec::EXIT_TEARDOWN_BIT) != 0);
  CHECK(std::memcmp(ec::sites[ec::EXIT_TEARDOWN_INDEX].game.data(), patched + teardown_site_rva, ec::SITE_SIZE) == 0);
  CHECK(ec::sites[ec::EXIT_TEARDOWN_INDEX].jump[0] == 0xE8
        && Read32(ec::sites[ec::EXIT_TEARDOWN_INDEX].jump.data() + 1)
               == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ec::ExitDrainBeforeTeardown) - reinterpret_cast<uintptr_t>(patched + teardown_site_rva + ec::SITE_SIZE)));
  CHECK(reinterpret_cast<const uint8_t*>(ec::exit_root_teardown) == patched + teardown_rva);

  // 30b. Not the game's code as expected: the sites that are not found are not written, one line names them. A site whose own function is not as expected leaves the other to stand
  // (`stays`: the bits still found); the accessor or the pool's pointer wrong leaves neither.
  const auto left_alone = [&](const char* why, uint32_t stays, const std::string& names) {
    prepare();
    CHECK((ec::found & ec::EXIT_DRAIN_SITES) == stays);
    const std::array<std::pair<size_t, uint32_t>, 2> exit_sites = {{{ec::EXIT_DRAIN_INDEX, site_rva}, {ec::EXIT_TEARDOWN_INDEX, teardown_site_rva}}};
    for (const auto& [index, rva] : exit_sites) CHECK((ec::sites[index].at == nullptr) == ((stays >> index & 1) == 0));
    ec::prefetch_logged = 0;
    ec::asked = ~0u;
    ec::failed_writes = 0;
    const size_t lines_before = log_lines.size();
    drain(true);
    for (int tries = 1; ec::written != stays && tries < static_cast<int>(ec::MAX_FAILED_WRITES); ++tries) drain(true);
    CHECK(ec::written == stays);
    for (const auto& [index, rva] : exit_sites) {
      if ((stays >> index & 1) == 0) CHECK(std::memcmp(patched + rva, unpatched.data() + (rva - text_rva), ec::SITE_SIZE) == 0);
    }
    int named = 0;
    for (size_t line_index = lines_before; line_index < log_lines.size(); ++line_index) {
      if (log_lines[line_index].find("Exit Job Drain: left alone") == std::string::npos) continue;
      ++named;
      CHECK(log_lines[line_index].ends_with("Exit Job Drain: left alone (not the game's code as expected: another mod or build): " + names));
    }
    CHECK(named == 1);
    drain(false);  // the sites that did go in come out
    CHECK(ec::written == 0);
    for (const auto& [index, rva] : exit_sites) CHECK(std::memcmp(patched + rva, unpatched.data() + (rva - text_rva), ec::SITE_SIZE) == 0);
    std::printf("   %s: left alone (%s), one line%s\n", why, names.c_str(), (stays != 0 ? ", the other site stands" : ""));
    restore();
    return 0;
  };
  patched[function_rva + 3] ^= 1;
  if (left_alone("the +0xC9AC1 function's signature changed", ec::EXIT_TEARDOWN_BIT, "exit job drain") != 0) return 1;
  patched[app_delete_rva + 8] ^= 1;
  if (left_alone("the app's delete changed", ec::EXIT_TEARDOWN_BIT, "exit job drain") != 0) return 1;
  patched[teardown_caller_rva + 3] ^= 1;
  if (left_alone("the outer loop's signature changed", ec::EXIT_DRAIN_BIT, "exit teardown drain") != 0) return 1;
  patched[teardown_rva + 3] ^= 1;
  if (left_alone("the root teardown's head changed", ec::EXIT_DRAIN_BIT, "exit teardown drain") != 0) return 1;
  for (const uint32_t rva : accessor_rvas) patched[rva + 9] ^= 1;
  if (left_alone("no job system accessor", 0, "exit job drain, exit teardown drain") != 0) return 1;
  for (const uint32_t rva : accessor_rvas) Write32(patched + rva + 1, 0x1000);
  if (left_alone("the job system's pointer outside the image", 0, "exit job drain, exit teardown drain") != 0) return 1;
  prepare();
  CHECK(ec::sites[ec::EXIT_DRAIN_INDEX].at == patched + site_rva && ec::sites[ec::EXIT_TEARDOWN_INDEX].at == patched + teardown_site_rva);

  // 30c. Both sites written and taken out.
  bool in = false;
  for (int tries = 0; !in && tries < static_cast<int>(ec::MAX_FAILED_WRITES); ++tries) {
    drain(true);
    in = (ec::written == ec::EXIT_DRAIN_SITES);
  }
  CHECK(in && std::memcmp(patched + site_rva, ec::sites[ec::EXIT_DRAIN_INDEX].jump.data(), 5) == 0
        && std::memcmp(patched + teardown_site_rva, ec::sites[ec::EXIT_TEARDOWN_INDEX].jump.data(), 5) == 0);
  CHECK(log_lines.back().find("Exit Job Drain: on") != std::string::npos);
  size_t differing = 0;
  for (uint32_t at = text_rva; at < text_rva + text_size; ++at) {
    if (unpatched[at - text_rva] == patched[at]) continue;
    ++differing;
    CHECK((at > site_rva && at < site_rva + ec::SITE_SIZE) || (at > teardown_site_rva && at < teardown_site_rva + ec::SITE_SIZE));  // the rel32 only: the E8 stays
  }
  CHECK(differing >= 2);
  drain(false);
  CHECK(ec::written == 0 && std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
  for (int tries = 0; ec::written == 0 && tries < static_cast<int>(ec::MAX_FAILED_WRITES); ++tries) drain(true);
  CHECK(ec::written == ec::EXIT_DRAIN_SITES);
  std::printf("   both written (%zu bytes differ, all the calls' rel32) and taken out: the game's bytes back\n", differing);

  // 30e. Another mod's bytes over one of our calls while both are in: switching off leaves them, that site stands down and the module is pinned (the foreign code may jump back into the
  // stub); the other site comes out.
  for (const size_t index : {ec::EXIT_DRAIN_INDEX, ec::EXIT_TEARDOWN_INDEX}) {
    const size_t other = (index == ec::EXIT_DRAIN_INDEX ? ec::EXIT_TEARDOWN_INDEX : ec::EXIT_DRAIN_INDEX);
    const std::array<uint8_t, ec::SITE_SIZE> hook = {0xE9, 0x10, 0x20, 0x30, 0x40};
    uint8_t* const site = ec::sites[index].at;
    CHECK(!ec::module_pinned);
    std::memcpy(site, hook.data(), hook.size());
    ec::failed_writes = 0;
    drain(false);
    CHECK(ec::module_pinned && ec::written == 0 && (ec::found & ec::EXIT_DRAIN_SITES) == (ec::EXIT_DRAIN_SITES & ~(1u << index)) && std::memcmp(site, hook.data(), hook.size()) == 0);
    CHECK(std::memcmp(ec::sites[other].at, ec::sites[other].game.data(), ec::SITE_SIZE) == 0);
    ec::module_pinned = false;
    std::memcpy(site, ec::sites[index].game.data(), ec::SITE_SIZE);
    prepare();
    CHECK(ec::sites[ec::EXIT_DRAIN_INDEX].at == patched + site_rva && ec::sites[ec::EXIT_TEARDOWN_INDEX].at == patched + teardown_site_rva
          && (ec::found & ec::EXIT_DRAIN_SITES) == ec::EXIT_DRAIN_SITES);
    ec::failed_writes = 0;
    for (int tries = 0; ec::written == 0 && tries < static_cast<int>(ec::MAX_FAILED_WRITES); ++tries) drain(true);
    CHECK(ec::written == ec::EXIT_DRAIN_SITES && !ec::module_pinned);
    std::printf("   another mod's bytes over the %s call going out: they stay, the site stood down, the module pinned, the other site out\n", ec::sites[index].name.data());
  }

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
  constexpr int DRAIN_CAP = static_cast<int>(ec::EXIT_DRAIN_LIMIT.count());
  constexpr int TEARDOWN_CAP = static_cast<int>(ec::EXIT_TEARDOWN_DRAIN_LIMIT.count());

  // Everyone asleep: no wait.
  world(5, {});
  ec::game_exiting = false;
  int took = at_exit();
  CHECK(deleted_once() && exit_fake_busy_at_delete == 0 && took < 500);
  CHECK(line().find("Exit Job Drain: drained 0 of 5 job workers in ") != std::string::npos && line().ends_with(" ms."));
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
  CHECK(line().find("Exit Job Drain: drained 3 of 5 job workers in ") != std::string::npos && line().ends_with(" ms (busy at the first poll: 1, 2, 3)."));
  std::printf("   a drain of 3 busy workers: waited %d ms, the game's delete saw all asleep; \"%s\"\n", took, line().c_str());
  // Every worker asleep but a job queued (the pool's [0x10], before a worker pops it): waited for until it is taken.
  world(5, {});
  Write32(memory + 0x10, address(memory + 0x300));
  ec::game_exiting = false;
  {
    std::thread taker([&] {
      Sleep(80);
      Write32(memory + 0x10, 0);
    });
    took = at_exit();
    taker.join();
  }
  CHECK(deleted_once() && exit_fake_busy_at_delete == 0 && took >= 70 && took < 900);
  CHECK(line().find("Exit Job Drain: drained 0 of 5 job workers in ") != std::string::npos);
  std::printf("   a queued job with every worker asleep: waited %d ms\n", took);
  // The drain only once: a second call goes straight on to the game's delete, no line.
  const size_t lines_before = log_lines.size();
  world(5, {{4, 2}});
  took = at_exit();
  CHECK(deleted_once() && exit_fake_busy_at_delete == 1 && took < 500 && log_lines.size() == lines_before);
  // A worker that never sleeps: the cap.
  world(5, {{4, 2}});
  ec::game_exiting = false;
  took = at_exit();
  CHECK(deleted_once() && exit_fake_busy_at_delete == 1 && took >= DRAIN_CAP && took < DRAIN_CAP + 2000);
  CHECK(line().find("Exit Job Drain: gave up after ") != std::string::npos && line().find("still running: 4 (of 5).") != std::string::npos);
  std::printf("   the cap: waited %d ms, the game's delete went on; \"%s\"\n", took, line().c_str());
  world(5, {});
  Write32(memory + 0x10, address(memory + 0x300));
  ec::game_exiting = false;
  took = at_exit();
  CHECK(deleted_once() && took >= DRAIN_CAP && took < DRAIN_CAP + 2000);
  CHECK(line().find("gave up after ") != std::string::npos && line().find("still running: none (of 5), jobs still queued.") != std::string::npos);
  std::printf("   a job that stays queued: waited %d ms; \"%s\"\n", took, line().c_str());
  // No pool / a pool not as expected: no wait.
  world(5, {{1, 2}});
  Write32(reinterpret_cast<uint8_t*>(ec::job_pool_global), 0);
  ec::game_exiting = false;
  took = at_exit();
  CHECK(deleted_once() && took < 500 && line().find("Exit Job Drain: the game built no job system") != std::string::npos);
  world(5, {{1, 2}});
  Write32(exit_fake_workers[3] + 4, 0x1234);
  ec::game_exiting = false;
  took = at_exit();
  CHECK(deleted_once() && took < 500 && line().find("Exit Job Drain: the job workers were not as expected") != std::string::npos);
  world(0, {});
  ec::game_exiting = false;
  took = at_exit();
  CHECK(deleted_once() && took < 500 && line().find("Exit Job Drain: the job system was not as expected") != std::string::npos);
  std::printf("   no pool, a worker of another pool, no workers: no wait, the game's delete runs\n");

  // 30d+. The same at +0xD5D74: the root teardown (set after the last Prepare above, which put the game's) is a fake that records the loop object it gets and the workers still running; the call is
  // the outer loop's (thiscall, ecx = the loop object).
  uint32_t fake_loop_object = 0;
  ec::exit_root_teardown = &ExitFakeTeardown;
  const auto forget_teardown = [] {
    exit_fake_teardowns = 0;
    exit_fake_busy_at_teardown = -1;
    exit_fake_teardown_self = nullptr;
  };
  const auto at_teardown = [&] {
    uint8_t* site = ec::sites[ec::EXIT_TEARDOWN_INDEX].at;  // an E8 (checked in 30a)
    const auto call = reinterpret_cast<void(__thiscall*)(void*)>(site + 5 + static_cast<int32_t>(Read32(site + 1)));
    forget_teardown();
    const auto start = std::chrono::steady_clock::now();
    call(&fake_loop_object);
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
  };
  const auto teardown_done = [&] { return exit_fake_teardowns.load() == 1 && exit_fake_teardown_self.load() == &fake_loop_object; };  // the teardown ran once, with the loop object
  const std::string teardown_line = "Exit Job Drain before the teardown: ";
  // Everyone asleep: no wait.
  world(5, {});
  ec::teardown_drained = false;
  took = at_teardown();
  CHECK(teardown_done() && exit_fake_busy_at_teardown == 0 && took < 500);
  CHECK(line().find(teardown_line + "drained 0 of 5 job workers in ") != std::string::npos && line().ends_with(" ms."));
  // Workers 1 and 3 running and worker 2 woken: waited for, the busy ones named, the teardown sees every worker asleep.
  world(5, {{1, 2}, {3, 2}});
  Write32(exit_fake_workers[2] + 0x50, 1);
  ec::teardown_drained = false;
  {
    std::thread releaser([&] {
      Sleep(60);
      Write32(exit_fake_workers[1] + 0x18, 1);
      Sleep(30);
      Write32(exit_fake_workers[3] + 0x18, 1);
      Write32(exit_fake_workers[2] + 0x50, 0);
    });
    took = at_teardown();
    releaser.join();
  }
  CHECK(teardown_done() && exit_fake_busy_at_teardown == 0 && took >= 80 && took < 900);
  CHECK(line().find(teardown_line + "drained 3 of 5 job workers in ") != std::string::npos && line().ends_with(" ms (busy at the first poll: 1, 2, 3)."));
  std::printf("   +0xD5D74: a drain of 3 busy workers: waited %d ms, the teardown saw all asleep with the loop object; \"%s\"\n", took, line().c_str());
  // Once: a second call writes no line and the teardown still gets the loop object.
  const size_t teardown_lines_before = log_lines.size();
  world(5, {{4, 2}});
  took = at_teardown();
  CHECK(teardown_done() && exit_fake_busy_at_teardown == 1 && took < 500 && log_lines.size() == teardown_lines_before);
  // A worker that never sleeps: the cap, then the teardown.
  world(5, {{4, 2}});
  ec::teardown_drained = false;
  took = at_teardown();
  CHECK(teardown_done() && exit_fake_busy_at_teardown == 1 && took >= TEARDOWN_CAP && took < TEARDOWN_CAP + 2000);
  CHECK(line().find(teardown_line + "gave up after ") != std::string::npos && line().find("still running: 4 (of 5).") != std::string::npos);
  std::printf("   +0xD5D74: the cap: waited %d ms, the teardown went on; \"%s\"\n", took, line().c_str());
  // No pool: no wait, the teardown runs.
  world(5, {{1, 2}});
  Write32(reinterpret_cast<uint8_t*>(ec::job_pool_global), 0);
  ec::teardown_drained = false;
  took = at_teardown();
  CHECK(teardown_done() && took < 500 && line().find(teardown_line + "the game built no job system") != std::string::npos);
  std::printf("   +0xD5D74: no pool: no wait, the teardown runs\n");

  // 30f. The race. A job on a worker reads the battle for its window and only then ends, the worker asleep again (a fake worker: a release store of state 1 after the last read, so a drain that saw the sleep sees
  // every read; a worker of the game's: its Sleep). The fake teardown records the workers still running, frees the battle, then makes the app delete's call (the nested call of +0xC9AC1).
  // Two pools: five fake workers, then, with Faster Job Wake-Ups on (its three sites in), five of the game's own workers: the job is queued and the pool woken by the game's WakeAll, as a frame does.
  {
    std::atomic<uint32_t> fake_battle = 1;
    std::atomic<bool> started = false;
    std::atomic<bool> job_done = false;
    std::atomic<bool> saw_freed = false;
    exit_fake_teardown_extra = [&] {
      fake_battle.store(0, std::memory_order_release);
      at_exit();
    };
    // The job: the control (no drain at +0xD5D74: the teardown called as it is) runs until it has seen the free, at most 5 s, and ends 30 ms after; through +0xD5D74 its window is 80 ms.
    const auto job_reads_battle = [&](bool through_drain) {
      started = true;
      const auto begin = std::chrono::steady_clock::now();
      const auto window = std::chrono::milliseconds(through_drain ? 80 : 5000);
      while (std::chrono::steady_clock::now() - begin < window) {
        if (fake_battle.load(std::memory_order_acquire) == 0) {
          saw_freed = true;
          if (!through_drain) {
            Sleep(30);
            break;
          }
        }
        std::this_thread::yield();
      }
    };
    // One case: `start_job` puts the job on a worker (it returns the thread of a fake worker, joined at the end, or none); the exit then runs without a drain at +0xD5D74 (the control) or through it.
    const auto race = [&](bool through_drain, const std::function<std::thread(bool)>& start_job) {
      fake_battle = 1;
      started = false;
      job_done = false;
      saw_freed = false;
      ec::game_exiting = false;
      ec::teardown_drained = false;
      std::thread job_thread = start_job(through_drain);
      while (!started) std::this_thread::yield();
      if (through_drain) {
        at_teardown();
      } else {
        forget_teardown();
        ExitFakeTeardown(&fake_loop_object);
      }
      while (!job_done) std::this_thread::yield();
      if (job_thread.joinable()) job_thread.join();
    };
    // A run in which the test thread stalled past the job's window (or its 30 ms) before the drain's first poll shows that drain no busy worker and proves nothing: it is run again (3 times at most).
    // `busy_workers`: how many workers that first poll must name (0: any).
    const auto waited_for_job = [&](size_t index, const std::string& drain_name, int busy_workers) {
      const std::string prefix = drain_name + ": drained ";
      const size_t at = log_lines[index].find(prefix);
      const int busy = (at != std::string::npos ? std::atoi(log_lines[index].c_str() + at + prefix.size()) : 0);
      return busy >= 1 && (busy_workers == 0 || busy == busy_workers) && log_lines[index].find(" job workers in ") != std::string::npos
             && log_lines[index].find(" ms (busy at the first poll: ") != std::string::npos && log_lines[index].ends_with(").");
    };
    const auto both_races = [&](const char* pool_name, const std::function<std::thread(bool)>& start_job, int busy_workers) -> int {
      // Control: the job sees the free; the drain at +0xC9AC1 waited for it after the free.
      bool waited = false;
      for (int attempt = 0; attempt < 3 && !waited; ++attempt) {
        const size_t first_line = log_lines.size();
        race(false, start_job);
        CHECK(saw_freed && teardown_done() && exit_fake_busy_at_teardown >= 1 && (busy_workers == 0 || exit_fake_busy_at_teardown == busy_workers) && deleted_once() && exit_fake_busy_at_delete == 0);
        CHECK(log_lines.size() == first_line + 1);
        waited = waited_for_job(first_line, "Exit Job Drain", busy_workers);
        if (waited) std::printf("   the race%s, no drain at +0xD5D74 (attempt %d of 3): the job saw the battle freed; \"%s\"\n", pool_name, attempt + 1, log_lines[first_line].c_str());
      }
      CHECK(waited);
      // Through +0xD5D74: the drain waits for the job before the free; the later drain finds every worker asleep.
      waited = false;
      for (int attempt = 0; attempt < 3 && !waited; ++attempt) {
        const size_t first_line = log_lines.size();
        race(true, start_job);
        CHECK(!saw_freed && teardown_done() && exit_fake_busy_at_teardown == 0 && deleted_once() && exit_fake_busy_at_delete == 0);
        CHECK(log_lines.size() == first_line + 2 && log_lines[first_line + 1].find("Exit Job Drain: drained 0 of 5 job workers in ") != std::string::npos
              && log_lines[first_line + 1].ends_with(" ms."));
        waited = waited_for_job(first_line, "Exit Job Drain before the teardown", busy_workers);
        if (waited) {
          std::printf("   the race%s, through +0xD5D74 (attempt %d of 3): the job never saw the battle freed; \"%s\", then \"%s\"\n", pool_name, attempt + 1, log_lines[first_line].c_str(),
                      log_lines[first_line + 1].c_str());
        }
      }
      CHECK(waited);
      return 0;
    };
    // The fake pool: worker 1 is in state 2 before any call and the job's thread puts it to sleep.
    if (both_races("", [&](bool through_drain) {
          world(5, {{1, 2}});
          return std::thread([&, through_drain] {
            job_reads_battle(through_drain);
            std::atomic_ref<uint32_t>(*reinterpret_cast<uint32_t*>(exit_fake_workers[1] + 0x18)).store(1, std::memory_order_release);
            job_done = true;
          });
        }, 1) != 0) {
      return 1;
    }

    // Faster Job Wake-Ups on: the same race on the game's own workers (five threads in its Sleep, the stub's side table keyed to the pool).
    const auto wake_ups = [&](bool on) -> int {
      ec::failed_writes = 0;
      const uint32_t wanted = ec::EXIT_DRAIN_SITES | (on ? ec::JOB_SITES : 0u);
      for (int tries = 0; ec::written != wanted && tries < static_cast<int>(ec::MAX_FAILED_WRITES); ++tries) {
        twattila::engine_code::Update(false, ATTILA_GAME, false, on, false, false, false, false, 1.f, 0, {.exit_job_drain = true});
      }
      CHECK(ec::written == wanted);
      return 0;
    };
    CHECK((ec::found & ec::JOB_SITES) == ec::JOB_SITES);
    if (wake_ups(true) != 0) return 1;
    FillKernelImports(patched);
    const uint32_t make_semaphore_rva = SemaphoreWrapperRva(game);
    CHECK(make_semaphore_rva != 0);
    const JobCopy job_copy = MakeJobCopy(build, make_semaphore_rva, "ours", patched, nullptr);
    CHECK(job_copy.pool != nullptr);
    constexpr uint32_t JOB_WORKERS = 5;
    ExitJobPool job_pool;
    job_pool.copy = &job_copy;
    std::array<ExitJobThread, JOB_WORKERS> contexts = {};
    std::array<HANDLE, JOB_WORKERS> threads = {};
    for (uint32_t index = 0; index < JOB_WORKERS; ++index) {
      contexts[index] = {&job_pool, index};
      threads[index] = CreateThread(nullptr, 0, &ExitJobThreadMain, &contexts[index], CREATE_SUSPENDED, nullptr);
      job_pool.workers[index] = NewWorker(job_copy, index, JOB_WORKERS, threads[index]);
      exit_fake_workers[index] = job_pool.workers[index];
    }
    exit_fake_count = JOB_WORKERS;
    Write32(reinterpret_cast<uint8_t*>(ec::job_pool_global), address(job_copy.pool));
    for (const HANDLE thread : threads) ResumeThread(thread);
    const int woken_races = both_races(" with Faster Job Wake-Ups on, five of the game's workers", [&](bool through_drain) {
      for (int wait = 0; wait < 5000 && ExitFakeBusy() != 0; ++wait) Sleep(1);  // every worker asleep again after the last run
      {
        const std::unique_lock guard(job_pool.lock);
        job_pool.jobs.push_back([&, through_drain] {
          job_reads_battle(through_drain);
          job_done = true;
        });
      }
      job_copy.wake_all(job_copy.pool, nullptr);  // after the push, as a frame does
      return std::thread();
    }, 0);
    for (uint32_t index = 0; index < JOB_WORKERS; ++index) {
      job_copy.destroy(job_pool.workers[index], nullptr);  // the game's destructor: Stop (state 3, the semaphore released, the thread waited for and closed), then the semaphore
      DecommitWorker(job_pool.workers[index]);
    }
    exit_fake_count = 0;
    Write32(reinterpret_cast<uint8_t*>(ec::job_pool_global), 0);
    VirtualFree(job_copy.pool, 0, MEM_RELEASE);
    if (woken_races != 0 || wake_ups(false) != 0) return 1;
    CHECK(JobTableField(0) == address(job_copy.pool) && JobTableField(1) == 0 && JobTableField(3) == 0);  // the row's stubs ran on this pool (the table's key), no request left, no loop running
    exit_fake_teardown_extra = nullptr;
  }
  ec::exit_root_teardown = reinterpret_cast<decltype(ec::exit_root_teardown)>(patched + teardown_rva);
  ec::game_exiting = false;
  ec::teardown_drained = false;

  drain(false);
  CHECK(ec::written == 0 && std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
  *app_global = 0;
  VirtualFree(memory, 0, MEM_RELEASE);
  std::printf("30. exit job drain: both sites as Prepare made them, the signatures (each alone, the accessor, the pool's pointer), the root teardown's callers, in and out, the drain on a fake pool at both sites (waited for, the busy workers named, a queued job, the cap, no pool, once), the race with its control (on the fake pool and with Faster Job Wake-Ups on), another mod's bytes over either call: PASS\n");
  return 0;
}
