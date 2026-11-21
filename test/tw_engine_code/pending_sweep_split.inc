// ====================================================================================================================
// 24. The pending list sweep's trace (Attila +0x1893290, entry site +0x18932a5, Thrones +0x19085f0; engine_code.hpp 20., pending_sweep.hpp), included inside tests.cpp's
// anonymous namespace after animation_rows.inc (Random, Address, Read32, Write32, CHECK, log_lines and the 23 helpers are used). The entry's stub calls
// pending_sweep::Arm with the list being swept only while the add-on asked for it (RENODX_TW_WORKER_TRACE); inside a trace window of the worker pool the call copies the
// entry count into a fixed array and the next present prints it. The sweep runs in both copies of the DLL on fake groups of 0 to 12 lists (lists that are never ready, so
// the game's own code reads them and removes nothing):
// 24a. the whole sweep in both copies, the site off and on, no window open: the groups equal, nothing recorded, nothing printed;
// 24b. a window open: the printed line holds exactly the groups' counts in order (object:entries), the sweeps of another thread are counted and not listed, a window of
//    one frame closes at the present that prints;
// 24c. more calls than the array holds: the first MAX_CALLS listed, the rest counted.
// ====================================================================================================================

struct SweepGroup {
  alignas(16) uint8_t self[0x40];  // `this`: +0x18 the count, +0x1c the entries
  uint32_t entries[2 * 12];        // {list object, control block}
  alignas(16) uint8_t lists[12][0x50];
};

void SweepGroupFill(SweepGroup* group, uint32_t count) {
  std::memset(group, 0, sizeof(*group));
  Write32(group->self + 0x18, count);
  Write32(group->self + 0x1C, Address(group->entries));
  for (uint32_t index = 0; index < count; ++index) {
    group->entries[2 * index] = Address(group->lists[index]);  // a list with no children and its flag clear: never ready
  }
}

// The sweep's rotating tweak (one byte at +0x48 of the object the sweep's first `mov ecx` names), on in both copies while the tests run.
void SweepTweak(uint8_t* image, uint32_t sweep_rva, bool on) {
  *reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(Read32(image + sweep_rva + 4)) + 0x48) = (on ? 1 : 0);
}

int RunPendingSweepTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  namespace wp = twattila::worker_pool;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  const uint32_t sweep_rva = build.code->sweep_rva;
  if (sweep_rva == 0) {
    std::printf("24. pending sweep trace: this build has no sweep (skipped)\n");
    return 0;
  }
  const auto update_rows = [&](bool sweep) {
    twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 1.f, 0, {.pending_sweep_trace = sweep});
  };
  const auto trace_lines = [&] {
    const std::unique_lock lock(log_mutex);
    size_t count = 0;
    for (const std::string& line : log_lines) count += (line.find("Pending Sweep Trace") != std::string::npos ? 1 : 0);
    return count;
  };
  const auto last_trace_line = [&] {
    const std::unique_lock lock(log_mutex);
    for (size_t index = log_lines.size(); index-- > 0;) {
      if (log_lines[index].find("Pending Sweep Trace") != std::string::npos) return log_lines[index];
    }
    return std::string();
  };
  using SweepFunction = void(__fastcall*)(void* list);
  const auto sweep_a = reinterpret_cast<SweepFunction>(game + sweep_rva);
  const auto sweep_b = reinterpret_cast<SweepFunction>(patched + sweep_rva);
  SweepTweak(game, sweep_rva, true);
  SweepTweak(patched, sweep_rva, true);
  auto groups = std::make_unique<std::array<SweepGroup, 8>>();
  const uint32_t arm = Address(reinterpret_cast<const void*>(&twattila::pending_sweep::Arm));
  Random random = {.state = 0x5EE9C0FFEEull};

  // The pool's owner is this thread (the add-on's present thread), no window open.
  wp::Update({.threads = 1});
  CHECK(!wp::Tracing());
  update_rows(false);
  CHECK((ec::written & ec::WORKER_SWEEP_BIT) == 0 && ec::worker_data->sweep_slot.load() == 0);

  // 24a. Whole sweeps in both copies, the site off and on; no window open.
  uint64_t wrong = 0;
  const int rounds = (quick ? 200 : 1500);
  for (int round = 0; round < rounds; ++round) {
    update_rows((round & 1) != 0);
    CHECK(((ec::written & ec::WORKER_SWEEP_BIT) != 0) == ((round & 1) != 0) && ec::worker_data->sweep_slot.load() == (((round & 1) != 0) ? arm : 0));
    SweepGroup& group = (*groups)[0];
    SweepGroupFill(&group, random.Next() % 13);
    const SweepGroup saved = group;
    sweep_a(group.self);
    const SweepGroup a = group;
    group = saved;
    sweep_b(group.self);
    wrong += (std::memcmp(&a, &group, sizeof(group)) != 0) ? 1 : 0;
  }
  CHECK(wrong == 0);
  const size_t printed_before = trace_lines();
  update_rows(true);
  CHECK(trace_lines() == printed_before);
  CHECK((ec::written & ec::WORKER_SWEEP_BIT) != 0 && ec::worker_data->sweep_slot.load() == arm && std::memcmp(patched + sweep_rva + ec::SWEEP_ENTRY_AT, ec::sites[ec::WORKER_SWEEP_INDEX].jump.data(), 5) == 0);
  std::printf("24. pending sweep trace: %d whole sweeps of 0 to 12 lists in both copies, the entry site off and on, no window open: the groups equal, nothing printed\n", rounds);

  // 24b. A window of one frame: the printed line has each sweep's count in order.
  wp::ArmTrace(1);
  wp::Update({.threads = 1});  // the header; Tracing() from here
  CHECK(wp::Tracing() && wp::TraceFramesLeft() == 1);
  const std::array<uint32_t, 6> counts = {0, 1, 5, 12, 3, 7};
  uint64_t total = 0;
  std::string expected;
  for (size_t index = 0; index < counts.size(); ++index) {
    SweepGroup& group = (*groups)[index];
    SweepGroupFill(&group, counts[index]);
    const SweepGroup saved = group;
    sweep_b(group.self);
    wrong += (std::memcmp(&saved, &group, sizeof(group)) != 0) ? 1 : 0;  // the sweep left it alone, as the game's does (lists never ready)
    total += counts[index];
    expected += std::format(" {:x}:{}", Address(group.self), counts[index]);
  }
  CHECK(wrong == 0);
  // Another thread's sweep: counted, not listed.
  std::thread other([&] {
    SweepGroup& group = (*groups)[6];
    SweepGroupFill(&group, 4);
    sweep_b(group.self);
    SweepGroupFill(&group, 2);
    sweep_b(group.self);
  });
  other.join();
  wp::Update({.threads = 1});  // the window's last present has gone: Tracing() is off
  CHECK(!wp::Tracing());
  update_rows(true);  // prints the frame's records
  CHECK(trace_lines() == printed_before + 1);
  const std::string line = last_trace_line();
  const std::string header = std::format("window {}: 6 sweeps, {} entries, 2 on other threads, 0 over the limit; object:entries", wp::TraceWindow(), total);
  CHECK(line.find(header) != std::string::npos && line.substr(line.find(header) + header.size()) == expected);
  update_rows(true);
  CHECK(trace_lines() == printed_before + 1);  // cleared: no line without records
  std::printf("   a window of one frame: the line lists 6 sweeps (%llu entries) in order, 2 sweeps of another thread counted, then nothing printed\n", total);

  // 24c. More calls than the array holds.
  wp::ArmTrace(1);
  wp::Update({.threads = 1});
  CHECK(wp::Tracing());
  SweepGroupFill(&(*groups)[0], 0);
  const uint32_t limit = static_cast<uint32_t>(twattila::pending_sweep::internal::MAX_CALLS);
  for (uint32_t call = 0; call < limit + 4; ++call) sweep_b((*groups)[0].self);
  wp::Update({.threads = 1});
  update_rows(true);
  CHECK(trace_lines() == printed_before + 2);
  const std::string overflow = last_trace_line();
  CHECK(overflow.find(std::format(": {} sweeps, 0 entries, 0 on other threads, 4 over the limit;", limit)) != std::string::npos);
  std::printf("   %u sweeps and 4 more: the array's %u listed, 4 counted as over the limit\n", limit + 4, limit);

  // Outside a window nothing is recorded, and the printed lines stay as they are.
  for (int call = 0; call < 20; ++call) sweep_b((*groups)[0].self);
  update_rows(true);
  CHECK(trace_lines() == printed_before + 2 && !wp::Tracing());

  update_rows(false);
  CHECK((ec::written & ec::WORKER_SWEEP_BIT) == 0 && ec::worker_data->sweep_slot.load() == 0);
  SweepTweak(game, sweep_rva, false);
  SweepTweak(patched, sweep_rva, false);
  return 0;
}
