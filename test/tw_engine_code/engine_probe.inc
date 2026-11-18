// ====================================================================================================================
// 21. The probe build (engine_probe.hpp, test/tw_engine_code/stubs/probes.s; compiled and run only with TWATTILA_ENGINE_PROBE: `engine_code.exe <dll> [quick] probe`),
// included inside tests.cpp's anonymous namespace after sweep_prefetch.inc (LoopCall, EnterParticleLoop, ParticleLoopReturn, JumpTo, Random, Address, Read32 and
// Write32 of the earlier parts are used):
// 21a. every probed site's jump goes to its probe stub, whose last jump goes on to the production stub, whose first bytes are the production array's (the production
//    stubs untouched);
// 21b. each probe stub alone on random registers (a private copy whose last jump returns to the harness): every register, the flags, esp and the frame above esp
//    as they were, the counters moved exactly as an independent model says;
// 21c. parts 19 and 20 quick with the probes in front of the stubs (their site checks follow the probe's jump): the same results as without, and the counters of the
//    item push loops, the ready check and the sweep counted;
// 21d. the dump (every histogram line) and the jump bytes log.
// ====================================================================================================================

namespace probe_test {

namespace ec = twattila::engine_code::internal;

// A probe stub alone: `code` its bytes (a private copy), the registers `in`, the flags, and the frame above esp random but for `slots` (offset above esp, value).
struct ProbeRun {
  std::array<uint32_t, 7> in;
  uint32_t flags;
  std::vector<std::pair<uint32_t, uint32_t>> slots;
};

// "" if the stub kept every register, the flags, esp and the frame, else what it changed.
const char* Run(const uint8_t* code, size_t size, const ProbeRun& run, Random* random) {
  auto* copy = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  std::memcpy(copy, code, size);
  JumpTo(copy + size - 5, reinterpret_cast<const void*>(&ParticleLoopReturn));
  auto call = std::make_unique<LoopCall>();
  std::memset(call.get(), 0, sizeof(LoopCall));
  uint8_t* esp = call->frame + (LOOP_ESP - offsetof(LoopCall, frame));
  for (uint32_t at = 0; at < 0x100; at += 4) Write32(esp + at, random->Next());
  for (const auto& [offset, value] : run.slots) Write32(esp + offset, value);
  const std::vector<uint8_t> frame_before(esp, esp + 0x100);
  std::memcpy(call->in, run.in.data(), sizeof(call->in));
  call->flags_in = run.flags;
  call->target = Address(copy);
  EnterParticleLoop(call.get());
  const char* what = "";
  const uint32_t expected_flags = (((run.flags & 0xD5) | 2) << 8) | ((run.flags >> 11) & 1);  // lahf's AH (bit 1 always set) and OF
  if (std::memcmp(call->out, run.in.data(), sizeof(call->out)) != 0) what = "a register";
  else if (call->flags_out != expected_flags) what = "the flags";
  else if (call->esp_after != Address(esp)) what = "esp";
  else if (std::memcmp(frame_before.data(), esp, 0x100) != 0) what = "the frame above esp";
  VirtualFree(copy, 0, MEM_RELEASE);
  return what;
}

uint32_t Log2Bucket(uint32_t value, uint32_t cap) {  // 1 + floor(log2(value)) capped, 0 for 0
  if (value == 0) return 0;
  return std::min<uint32_t>(cap, 1 + 31 - std::countl_zero(value));
}

}  // namespace probe_test

int RunProbeTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  namespace probe = ec::probe;
  CHECK(probe::counters != nullptr && probe::code != nullptr);

  // 21a. The chain.
  struct Production {
    size_t site;
    const uint8_t* stub;
    std::array<uint8_t, 8> first;
  };
  const std::array<Production, 7> chain = {{{ec::ITEM_PUSH_FIRST_SITE, ec::page + ec::ITEM_PUSH_PAGE + ec::ITEM_PUSH_STUB_AT[0], {}},
                                            {ec::ITEM_PUSH_FIRST_SITE + 1, ec::page + ec::ITEM_PUSH_PAGE + ec::ITEM_PUSH_STUB_AT[1], {}},
                                            {4, ec::page + ec::READY_CHECK_PAGE, {}},
                                            {ec::SWEEP_PREFETCH_INDEX, ec::page + ec::SWEEP_PAGE, {}},
                                            {ec::PARTICLE_PREFETCH_INDEX, ec::page + ec::PARTICLE_PREFETCH_PAGE, {}},
                                            {ec::CHILD_SUM_INDEX, ec::page + ec::CHILD_SUM_PAGE, {}},
                                            {ec::ENTRY_PREFETCH_INDEX, ec::page + ec::ENTRY_PREFETCH_PAGE, {}}}};
  const std::array<const uint8_t*, 7> arrays = {ec::ITEM_PUSH_STUB.data() + ec::ITEM_PUSH_STUB_AT[0], ec::ITEM_PUSH_STUB.data() + ec::ITEM_PUSH_STUB_AT[1], ec::READY_CHECK_STUB.data(),
                                                ec::SWEEP_STUB.data(), ec::PARTICLE_PREFETCH_STUB.data(), ec::CHILD_SUM_STUB.data(), ec::ENTRY_FETCH_STUB.data()};
  size_t probed = 0;
  for (size_t index = 0; index < chain.size(); ++index) {
    const ec::Site& site = ec::sites[chain[index].site];
    if (site.at == nullptr) continue;
    ++probed;
    const uint8_t* stub = probe::stub_of[chain[index].site];
    CHECK(stub != nullptr && ((ec::probe::installed >> chain[index].site) & 1) != 0);
    // The site's jump (as Prepare made it) goes to the probe stub; the probe stub's last jump goes to the production stub; its first bytes are the array's.
    CHECK(site.jump[0] == 0xE9 && Address(site.at + 5) + Read32(site.jump.data() + 1) == Address(stub));
    const uint8_t* end = stub + probe::size_of[chain[index].site];
    CHECK(end[-5] == 0xE9 && Address(end) + Read32(end - 4) == Address(chain[index].stub));
    CHECK(std::memcmp(chain[index].stub, arrays[index], 8) == 0);
    // The probe stub is the array but where the counters' addresses and the last jump were put in.
    const probe::Probe& entry = probe::PROBES[index];
    CHECK(entry.site == chain[index].site && probe::size_of[chain[index].site] == entry.size);
    size_t different = 0;
    for (size_t at = 0; at < entry.size - 5; ++at) different += (stub[at] != entry.code[at]) ? 1 : 0;
    CHECK(different >= 1 && different <= 4 * 8);  // at most the sentinels' bytes (the counters' addresses, up to seven of them in a stub)
  }
  std::printf("21. probe build: %zu probe stubs in front of their production stubs (the jumps chain site -> probe -> production, the production stubs' first bytes the arrays')\n", probed);
  CHECK(probed >= 5);

  // 21b. Each probe alone.
  Random random = {.state = 0x21B};
  const uint64_t runs = (quick ? 3000 : 20000);
  const auto stub_bytes = [&](size_t site) { return std::pair<const uint8_t*, size_t>(probe::stub_of[site], probe::size_of[site]); };
  const auto registers = [&] {
    probe_test::ProbeRun run = {};
    for (uint32_t& value : run.in) value = random.Next();
    run.flags = (random.Next() & 0x8D5) | 2;
    return run;
  };
  uint64_t wrong = 0;
  const auto compare = [&](const probe::Counters& model, const char* what, const char* name) {
    if (std::memcmp(probe::counters, &model, sizeof(probe::Counters)) != 0 || what[0] != 0) {
      if (++wrong <= 5) std::printf("   probe %s: %s\n", name, what[0] != 0 ? what : "the counters differ from the model");
    }
  };
  // Item push loop A / B: the remaining items (end - ebp) at the head, edx.
  for (const bool b : {false, true}) {
    const size_t site = ec::ITEM_PUSH_FIRST_SITE + (b ? 1 : 0);
    if (ec::sites[site].at == nullptr) continue;
    const auto [code, size] = stub_bytes(site);
    for (uint64_t index = 0; index < runs; ++index) {
      probe_test::ProbeRun run = registers();
      const uint32_t pick = random.Next() % 4;
      const uint32_t ebp = (pick == 0 ? random.Next() : random.Next() % 100000);
      const uint32_t end = (pick == 1 ? ebp - random.Next() % 4 : (pick == 2 ? ebp + (1u << (random.Next() % 31)) + random.Next() % 3 : ebp + random.Next() % 5000));
      run.in[6] = ebp;
      run.in[2] = (random.Next() % 3 == 0 ? 0 : random.Next());
      if (b) run.in[3] = end; else run.slots.push_back({0x34, end});
      probe::Counters model = *probe::counters;
      ++model.evec_run[b ? 1 : 0][ebp >= end ? 0 : probe_test::Log2Bucket(end - ebp, 16)];
      if (run.in[2] != 0) ++model.evec_calls[b ? 1 : 0];
      compare(model, probe_test::Run(code, size, run, &random), b ? "item push B" : "item push A");
    }
  }
  // The ready check: the children and the first one not ready.
  if (ec::sites[4].at != nullptr) {
    const auto [code, size] = stub_bytes(4);
    for (uint64_t index = 0; index < runs; ++index) {
      probe_test::ProbeRun run = registers();
      std::array<uint8_t, 0x50> list = {};
      const uint32_t count = (random.Next() % 4 == 0 ? random.Next() % 40 : random.Next() % 8);
      std::vector<uint32_t> children(2 * std::max<uint32_t>(count, 1), 0);
      std::vector<std::array<uint8_t, 0x80>> objects(std::max<uint32_t>(count, 1));
      const uint32_t not_ready = (random.Next() % 3 == 0 ? count : random.Next() % std::max<uint32_t>(count, 1));
      for (uint32_t child = 0; child < count; ++child) {
        objects[child][0x6E] = (child == not_ready ? 0 : static_cast<uint8_t>(1 + random.Next() % 255));
        children[2 * child] = Address(objects[child].data());
      }
      Write32(list.data() + 0x40, count);
      Write32(list.data() + 0x44, Address(children.data()));
      run.in[1] = Address(list.data());
      probe::Counters model = *probe::counters;
      ++model.ready_children[std::min<uint32_t>(count, 16)];
      uint32_t first = 17;
      for (uint32_t child = 0; child < std::min<uint32_t>(count, 16); ++child) {
        if (objects[child][0x6E] == 0) {
          first = child;
          break;
        }
      }
      ++model.ready_first[first];
      compare(model, probe_test::Run(code, size, run, &random), "ready check");
    }
  }
  // The sweep: the iterations and the count at a sweep's start (an iteration starts a sweep when ebx does not fall).
  if (ec::sites[ec::SWEEP_PREFETCH_INDEX].at != nullptr) {
    const auto [code, size] = stub_bytes(ec::SWEEP_PREFETCH_INDEX);
    for (uint64_t index = 0; index < runs; ++index) {
      probe_test::ProbeRun run = registers();
      std::array<uint8_t, 0x40> self = {};
      const uint32_t count = (random.Next() % 5 == 0 ? random.Next() % 70000 : random.Next() % 40);
      Write32(self.data() + 0x18, count);
      run.in[3] = random.Next() % 50;
      run.in[4] = Address(self.data());
      probe::Counters model = *probe::counters;
      ++model.sweep_iterations;
      if (run.in[3] >= model.sweep_last) ++model.sweep_len[31 - std::countl_zero(count | 1)];
      model.sweep_last = run.in[3];
      compare(model, probe_test::Run(code, size, run, &random), "sweep");
    }
  }
  // The particle loop: the slot two on inside the size, or the exit.
  if (ec::sites[ec::PARTICLE_PREFETCH_INDEX].at != nullptr) {
    const auto [code, size] = stub_bytes(ec::PARTICLE_PREFETCH_INDEX);
    for (uint64_t index = 0; index < runs; ++index) {
      probe_test::ProbeRun run = registers();
      std::array<uint8_t, 0x80> emitter = {};
      const uint32_t particles = (random.Next() % 6 == 0 ? random.Next() % 100 : random.Next() % 8);
      Write32(emitter.data() + 0x64, particles);
      run.in[5] = Address(emitter.data());
      run.in[3] = (random.Next() % 4 == 0 ? random.Next() % 120 : (random.Next() % 2 == 0 ? particles : (particles == 0 ? 0 : random.Next() % particles)));
      probe::Counters model = *probe::counters;
      ++model.particle_exec;
      if (run.in[3] >= particles) ++model.particle_size[std::min<uint32_t>(particles, 31)];
      else if (run.in[3] + 2 < particles) ++model.particle_taken;
      compare(model, probe_test::Run(code, size, run, &random), "particle loop");
    }
  }
  // The child sum: the exit when the next slot reaches the end.
  if (ec::sites[ec::CHILD_SUM_INDEX].at != nullptr) {
    const auto [code, size] = stub_bytes(ec::CHILD_SUM_INDEX);
    for (uint64_t index = 0; index < runs; ++index) {
      probe_test::ProbeRun run = registers();
      const uint32_t begin = random.Next();
      const uint32_t children = (random.Next() % 5 == 0 ? random.Next() % 70 : random.Next() % 10);
      const uint32_t end = begin + 4 * children;
      const uint32_t slot = (random.Next() % 2 == 0 ? end : (random.Next() % 2 == 0 ? begin + 4 * (random.Next() % (children + 1)) : random.Next()));
      run.in[1] = begin;
      run.in[2] = children;
      run.in[4] = slot;
      probe::Counters model = *probe::counters;
      ++model.sum_exec;
      if (slot >= end) ++model.sum_children[std::min<uint32_t>(children, 31)];
      compare(model, probe_test::Run(code, size, run, &random), "child sum");
    }
  }
  // The particle writer's entry: null, the next one, the same, another.
  if (ec::sites[ec::ENTRY_PREFETCH_INDEX].at != nullptr) {
    const auto [code, size] = stub_bytes(ec::ENTRY_PREFETCH_INDEX);
    for (uint64_t index = 0; index < runs; ++index) {
      probe_test::ProbeRun run = registers();
      const uint32_t pick = random.Next() % 5;
      const uint32_t previous = probe::counters->entry_prev;
      run.in[0] = (pick == 0 ? 0 : (pick == 1 ? previous + 24 : (pick == 2 ? previous : random.Next() | 1)));
      probe::Counters model = *probe::counters;
      ++model.entry_calls;
      if (run.in[0] == 0) {
        ++model.entry_null;
      } else {
        const uint32_t delta = run.in[0] - model.entry_prev;
        model.entry_prev = run.in[0];
        if (delta == 24) ++model.entry_one; else if (delta == 0) ++model.entry_zero; else ++model.entry_other;
      }
      compare(model, probe_test::Run(code, size, run, &random), "particle writer entry");
    }
  }
  std::printf("   each probe stub alone: %llu runs each on random registers and flags: registers, flags, esp and frame kept, every counter as the model says: %llu wrong\n", runs, wrong);
  CHECK(wrong == 0);

  // 21c. The probed sites' own parts with the probes in front, quick (their site checks follow the probe's jump): the item push loops (19), the sweep with the ready
  // check (20), the child sum (12), the particle loop (15) and the particle writer's entry (18).
  const auto update = [](bool enabled) {
    twattila::engine_code::Update(enabled, twattila::engine_code::Game::ATTILA, false, false, enabled, enabled, enabled, enabled);
  };
  *probe::counters = {};
  update(true);
  if (const int failed = RunPushTests(build, game, patched, true); failed != 0) return failed;
  update(true);
  if (const int failed = RunSweepTests(build, game, patched, true); failed != 0) return failed;
  update(true);
  if (const int failed = RunSumTests(build, game, patched, true); failed != 0) return failed;
  update(true);
  if (const int failed = RunParticleTests(build, game, patched, true); failed != 0) return failed;
  update(true);
  if (const int failed = RunEntryTests(build, game, patched, true); failed != 0) return failed;
  const probe::Counters& c = *probe::counters;
  uint64_t evec_a = 0, evec_b = 0, ready = 0, first = 0;
  for (size_t bucket = 0; bucket < 17; ++bucket) {
    evec_a += c.evec_run[0][bucket];
    evec_b += c.evec_run[1][bucket];
    ready += c.ready_children[bucket];
  }
  for (size_t bucket = 0; bucket < 18; ++bucket) first += c.ready_first[bucket];
  uint64_t sweeps = 0;
  for (size_t bucket = 0; bucket < 32; ++bucket) sweeps += c.sweep_len[bucket];
  uint64_t sum_children = 0, particles = 0;
  for (size_t bucket = 0; bucket < 32; ++bucket) {
    sum_children += c.sum_children[bucket];
    particles += c.particle_size[bucket];
  }
  // A ready check that faults inside its probe (the sweep's corrupt scenarios: the game's check faults there too) has counted its children but not its first child not ready.
  std::printf("   parts 19, 20, 12, 15 and 18 (quick) through the probes, every result the game's: item push entries A %llu, B %llu (%u / %u with a per-item call), ready checks %llu (%llu with the "
              "first child counted, the %llu others faulted in the probe), sweep iterations %u in %llu sweeps, child sum bottoms %u (%llu exits), particle loop bottoms %u (%llu exits), "
              "particle writer entries %u\n",
              evec_a, evec_b, c.evec_calls[0], c.evec_calls[1], ready, first, ready - first, c.sweep_iterations, sweeps, c.sum_exec, sum_children, c.particle_exec, particles,
              c.entry_calls);
  CHECK(evec_a > 1000 && evec_b > 1000 && ready > 1000 && first <= ready && (ready - first) * 1000 <= ready && c.sweep_iterations > 1000 && sweeps > 100);
  CHECK(c.sum_exec > 1000 && sum_children > 100 && c.particle_exec > 1000 && particles > 100 && c.entry_calls > 1000 && c.entry_null + c.entry_one + c.entry_zero + c.entry_other <= c.entry_calls);

  // 21d. The dump, armed for 3 presents (the ReShade.ini key's way): none at the first two Updates with the sites written, every line at the third, none after; and the
  // sites' jump bytes logged at each change (off: the game's bytes, on: the jumps).
  const auto dumps = [] {
    return std::count_if(log_lines.begin(), log_lines.end(), [](const std::string& line) { return line.find("probe: item push loop A") != std::string::npos; });
  };
  const auto before = dumps();
  const size_t first_line = log_lines.size();
  probe::Arm(3);
  update(true);
  update(true);
  CHECK(dumps() == before);
  update(true);
  CHECK(dumps() == before + 1);
  update(true);
  update(true);
  CHECK(dumps() == before + 1);
  update(false);
  update(true);
  size_t game_lines = 0, jump_lines = 0;
  for (size_t index = first_line; index < log_lines.size(); ++index) {
    const std::string& line = log_lines[index];
    if (line.find("probe: ") != std::string::npos) std::printf("   %s\n", line.c_str());
    game_lines += line.find("probe: site ") != std::string::npos && line.find("(the game's bytes)") != std::string::npos;
    jump_lines += line.find("probe: site ") != std::string::npos && line.find("(the jump)") != std::string::npos;
  }
  CHECK(game_lines > 0 && jump_lines > 0);
  return 0;
}
