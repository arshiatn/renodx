// ====================================================================================================================
// 28. Cached Particle Count (Attila +0x192D290 the node's update, +0x1929E60 the visible particle sum; engine_code.hpp 28.), included inside tests.cpp's anonymous namespace after
// effect_update.inc (Address, Read32, Write32, Random, RegisterCall, CallWithRegisters, Matches, CHECK, the ei / eu aliases of part 25 are used). Attila only. The row's own two sites,
// found both or neither; the epoch's stub bumps a generation word, the sum's stub answers from a one-slot cache keyed by node and generation, and in check mode also runs the game's body.
// 28a. The sites in the game's code (B): both patterns once each in A, the places, no other site (of the effect update, or the other rows) overlapping them, no branch of the code section
//    and none of the relocated pointers lands inside one; the row on: B differs from A only in the two jumps (the epoch's sixth byte is the game's), the jumps into the stubs' range
//    of the page, the stubs holding the game's own bytes before their jumps back; the check word follows the row's check; off: the game's bytes back; another mod's byte in one site: neither
//    goes in and the row is named once; the effect update accepts an E9 into the stubs' page at the two entries as ours;
// 28b. The stubs on a fake game, built by the production emitter: the registers the game's convention keeps (ebx esi edi ebp, esp) on a miss, a hit, a hit in check mode and the epoch;
// 28c. CachedCountEqualsFresh: a random sequence of node updates (the epoch, then the nodes' sums change) and sum calls over three nodes, the answer always the fresh sum, the game's body
//    run once per miss (check off) or once per call (check on), no mismatch; the cache of one node survives a call of another only as a miss; the generation's wrap;
//    CONTROLs: no epoch before the sums change: check off answers a stale sum (the control must show it), check on counts the mismatches and answers the fresh sum.
// ====================================================================================================================

struct CountNode {  // a fake scene node: the fake sum answers value
  uint32_t padding = 0;
  uint32_t value = 0;
};

int RunParticleCountTests(const TestBuild& build, uint8_t* game, uint8_t* patched, uint32_t text_rva, uint32_t text_size, bool quick) {
  namespace ec = twattila::engine_code::internal;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  if (build.code != &ec::ATTILA_BUILD) {
    std::printf("28. cached particle count: Attila only (skipped)\n");
    return 0;
  }
  const auto rows = [&](bool on, bool check) {
    twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 1.f, 0, {.particle_count = on, .particle_count_check = check});
  };
  rows(false, false);
  CHECK(ec::written == 0 && ei::written == 0 && (ec::found & ec::PARTICLE_COUNT_SITES) == ec::PARTICLE_COUNT_SITES && ec::count_data != nullptr);
  const uint32_t epoch_rva = build.code->particle_epoch_rva;
  const uint32_t count_rva = build.code->particle_count_rva;
  const std::vector<uint8_t> unpatched(patched + text_rva, patched + text_rva + text_size);

  // 28a. The sites in the game's code.
  CHECK(epoch_rva == ec::PARTICLE_EPOCH_RVA && count_rva == ec::PARTICLE_COUNT_RVA);
  CHECK(Matches(game + text_rva, text_size, ec::PARTICLE_EPOCH) == 1 && Matches(game + epoch_rva, ec::PatternSize(ec::PARTICLE_EPOCH), ec::PARTICLE_EPOCH) == 1);
  CHECK(Matches(game + text_rva, text_size, ec::PARTICLE_COUNT) == 1 && Matches(game + count_rva, ec::PatternSize(ec::PARTICLE_COUNT), ec::PARTICLE_COUNT) == 1);
  CHECK(ec::sites[ec::PARTICLE_EPOCH_INDEX].at == patched + epoch_rva && ec::sites[ec::PARTICLE_COUNT_INDEX].at == patched + count_rva);
  // Our own sites and the effect update's do not overlap (the sites' bytes, their callees' heads, the sweep's span).
  const auto overlaps_ours = [&](uint32_t rva, uint32_t size) {
    return (rva < epoch_rva + ec::PARTICLE_EPOCH_SIZE && epoch_rva < rva + size) || (rva < count_rva + ec::PARTICLE_COUNT_HEAD && count_rva < rva + size);
  };
  for (const eu::SiteInfo& info : eu::SITES) {
    CHECK(!overlaps_ours(info.rva, static_cast<uint32_t>(eu::SizeOf(info))));
    if (info.callee_rva != 0) CHECK(!overlaps_ours(info.callee_rva, static_cast<uint32_t>(ec::PatternSize(info.callee_head))));
  }
  CHECK(!overlaps_ours(eu::SWEEP_RVA, eu::SWEEP_CHECKS[1].rva + static_cast<uint32_t>(ec::PatternSize(eu::SWEEP_CHECKS[1].pattern)) - eu::SWEEP_RVA));
  // No branch of the code section and no relocated pointer lands inside the bytes a jump replaces (a rel32 form or a short one: the whole section's known).
  {
    const std::span<const uint8_t> code(game + text_rva, text_size);
    size_t branches = 0;
    for (size_t at = 0; at < code.size(); ++at) {
      const uint8_t opcode = code[at];
      size_t length = 0;
      int32_t displacement = 0;
      if ((opcode == 0xE8 || opcode == 0xE9) && at + 5 <= code.size()) {
        length = 5;
        displacement = static_cast<int32_t>(Read32(code.data() + at + 1));
      } else if (opcode == 0x0F && at + 6 <= code.size() && (code[at + 1] & 0xF0) == 0x80) {
        length = 6;
        displacement = static_cast<int32_t>(Read32(code.data() + at + 2));
      } else if ((opcode == 0xEB || (opcode & 0xF0) == 0x70) && at + 2 <= code.size()) {
        length = 2;
        displacement = static_cast<int8_t>(code[at + 1]);
      } else {
        continue;
      }
      const uint32_t target = text_rva + static_cast<uint32_t>(at + length) + static_cast<uint32_t>(displacement);
      // The inside of the replaced bytes: past the first (the jump's own byte is where the game enters), up to the end of the instruction the jump replaces.
      branches += ((target > epoch_rva && target < epoch_rva + ec::PARTICLE_EPOCH_SIZE) || (target > count_rva && target < count_rva + ec::PARTICLE_COUNT_HEAD)) ? 1 : 0;
    }
    CHECK(branches == 0);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(game + reinterpret_cast<const IMAGE_DOS_HEADER*>(game)->e_lfanew);
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    size_t pointers = 0;
    size_t inside = 0;
    for (uint32_t at = 0; at < directory.Size;) {
      const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(game + directory.VirtualAddress + at);
      if (block->SizeOfBlock == 0) break;
      const auto* entries = reinterpret_cast<const uint16_t*>(block + 1);
      for (size_t entry = 0; entry < (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2; ++entry) {
        if ((entries[entry] >> 12) != IMAGE_REL_BASED_HIGHLOW) continue;
        ++pointers;
        const uint32_t value = Read32(game + block->VirtualAddress + (entries[entry] & 0xFFF)) - Address(game);
        inside += ((value > epoch_rva && value < epoch_rva + ec::PARTICLE_EPOCH_SIZE) || (value > count_rva && value < count_rva + ec::PARTICLE_COUNT_HEAD)) ? 1 : 0;
      }
      at += block->SizeOfBlock;
    }
    CHECK(pointers > 100000 && inside == 0);
  }
  std::printf("28a. both patterns once each in A at the places, clear of the effect update's sites, no branch of the code section and no relocated pointer inside a replaced instruction\n");

  // The row on: the two jumps only, into the stubs.
  const uint8_t* const stubs_begin = ec::page + ec::PARTICLE_COUNT_PAGE;
  rows(true, false);
  CHECK(ec::written == ec::PARTICLE_COUNT_SITES && ei::written == 0 && ec::count_data->check == 0);
  {
    size_t differing = 0;
    for (uint32_t at = text_rva; at < text_rva + text_size; ++at) {
      if (unpatched[at - text_rva] == patched[at]) continue;
      ++differing;
      CHECK((at >= epoch_rva && at < epoch_rva + 5) || (at >= count_rva && at < count_rva + 5));
    }
    CHECK(differing <= 10);
    const uint8_t* const epoch_site = patched + epoch_rva;
    const uint8_t* const count_site = patched + count_rva;
    CHECK(epoch_site[0] == 0xE9 && count_site[0] == 0xE9 && epoch_site[5] == unpatched[epoch_rva + 5 - text_rva]);
    const uint8_t* const epoch_stub = epoch_site + 5 + static_cast<int32_t>(Read32(epoch_site + 1));
    const uint8_t* const count_stub = count_site + 5 + static_cast<int32_t>(Read32(count_site + 1));
    CHECK(epoch_stub == stubs_begin && count_stub > epoch_stub && count_stub < stubs_begin + ec::PARTICLE_COUNT_SIZE);
    // The epoch stub: inc [generation]; the game's `sub esp, 0xBC`; a jump to the instruction after it. Before the sum's stub, the trampoline: the game's first five bytes, a jump into the body.
    CHECK(epoch_stub[0] == 0xFF && epoch_stub[1] == 0x05 && Read32(epoch_stub + 2) == Address(&ec::count_data->generation));
    CHECK(std::memcmp(epoch_stub + 6, game + epoch_rva, ec::PARTICLE_EPOCH_SIZE) == 0 && epoch_stub[12] == 0xE9);
    CHECK(epoch_stub + 17 + static_cast<int32_t>(Read32(epoch_stub + 13)) == patched + epoch_rva + ec::PARTICLE_EPOCH_SIZE);
    const uint8_t* const trampoline = epoch_stub + 17;
    CHECK(std::memcmp(trampoline, game + count_rva, ec::PARTICLE_COUNT_HEAD) == 0 && trampoline[5] == 0xE9);
    CHECK(trampoline + 10 + static_cast<int32_t>(Read32(trampoline + 6)) == patched + count_rva + ec::PARTICLE_COUNT_HEAD && trampoline + 10 == count_stub);
    CHECK(ec::sites[ec::PARTICLE_EPOCH_INDEX].game[0] == game[epoch_rva] && ec::sites[ec::PARTICLE_COUNT_INDEX].game[0] == game[count_rva]);
    CHECK(ei::sibling_page.data() == ec::page && ei::sibling_page.size() == ec::PAGE_SIZE);
  }
  rows(true, true);
  CHECK(ec::written == ec::PARTICLE_COUNT_SITES && ec::count_data->check == 1);
  rows(true, false);
  CHECK(ec::count_data->check == 0);
  {
    const twattila::engine_code::ParticleCountStats stats = twattila::engine_code::ParticleCount();
    CHECK(stats.hits == ec::count_data->hits && stats.misses == ec::count_data->misses && stats.mismatches == 0);
  }
  rows(false, false);
  CHECK(ec::written == 0 && std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
  std::printf("28a. row on: B differs from A only in the two jumps, into the stubs, the game's bytes in the stubs; the check word follows; off: the game's bytes back\n");

  // The effect update accepts an E9 into the stubs' page at the two entries (this row's), and still refuses another tool's.
  {
    rows(true, false);
    CHECK(eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::found == eu::ALL_SITES);
    patched[eu::HOOKED_ENTRIES[0]] = 0xE9;
    CHECK(!eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::found == 0);
    patched[eu::HOOKED_ENTRIES[0]] = unpatched[eu::HOOKED_ENTRIES[0] - text_rva];
    CHECK(eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::found == eu::ALL_SITES);
    // CONTROL: without the page (no sibling known) the same two jumps are another tool's hooks: refused.
    const std::span<const uint8_t> sibling = ei::sibling_page;
    ei::sibling_page = {};
    CHECK(!eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::found == 0);
    ei::sibling_page = sibling;
    CHECK(eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::found == eu::ALL_SITES);
    rows(false, false);
    CHECK(ec::written == 0);
    std::printf("28a. the effect update takes this row's E9 at its two entries as ours (and an E9 elsewhere still refuses it)\n");
  }

  // Another mod's byte in one of the two sites: neither goes in (both or neither), and the row is named once.
  {
    const auto prepare_again = [&] {
      for (ec::Site& site : ec::sites) site.at = nullptr;
      ec::prepared = false;
      ec::written = 0;
      ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
    };
    for (const uint32_t rva : {count_rva + 3, epoch_rva + 2}) {
      const uint8_t saved = patched[rva];
      patched[rva] = 0x90;
      prepare_again();
      CHECK((ec::found & ec::PARTICLE_COUNT_SITES) == 0 && ec::sites[ec::PARTICLE_EPOCH_INDEX].at == nullptr && ec::sites[ec::PARTICLE_COUNT_INDEX].at == nullptr);
      ec::prefetch_logged &= ~ec::PARTICLE_COUNT_SITES;
      const size_t before = log_lines.size();
      rows(true, false);
      CHECK((ec::written & ec::PARTICLE_COUNT_SITES) == 0 && patched[rva] == 0x90);
      bool named = false;
      for (size_t line = before; line < log_lines.size(); ++line) named |= (log_lines[line].find("Cached Particle Count: left alone") != std::string::npos);
      CHECK(named);
      rows(true, false);
      const size_t again = log_lines.size();
      rows(true, false);
      CHECK(log_lines.size() == again);  // named once
      rows(false, false);
      patched[rva] = saved;
      prepare_again();
      CHECK((ec::found & ec::PARTICLE_COUNT_SITES) == ec::PARTICLE_COUNT_SITES);
    }
    std::printf("28a. another mod's byte in either site: neither goes in, the row named once; restored: both found again\n");
  }
  CHECK(std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);

  // 28b and 28c on a fake game: the production emitter, on a page of its own.
  uint8_t* const memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  CHECK(memory != nullptr);
  uint8_t* const epoch_site = memory;
  uint8_t* const count_site = memory + 0x40;
  uint8_t* const stub_memory = memory + 0x200;
  auto* const data = new (memory + 0x1000) ec::CountData;
  auto* const calls = reinterpret_cast<uint32_t*>(memory + 0x1100);
  // The fake game: the epoch is `sub esp, 0xBC` (six bytes, the jump takes five), `add esp, 0xBC`, `ret`; the sum is the game's prologue (sub esp, 0x50; push esi; push edi), the node's
  // value in eax and a count of its runs, its epilogue and `ret`.
  constexpr std::array<uint8_t, 13> FAKE_EPOCH = {0x81, 0xEC, 0xBC, 0x00, 0x00, 0x00, 0x81, 0xC4, 0xBC, 0x00, 0x00, 0x00, 0xC3};
  std::memcpy(epoch_site, FAKE_EPOCH.data(), FAKE_EPOCH.size());
  {
    ei::Assembler fake(std::span<uint8_t>(count_site, 0x40));
    fake.Bytes({0x83, 0xEC, 0x50, 0x56, 0x57, 0x8B, 0x41, 0x04});  // the body: mov eax, [ecx + 4]
    fake.BytesAndOperand({0xFF, 0x05}, Address(calls));             // inc dword ptr [calls]
    fake.Bytes({0x5F, 0x5E, 0x83, 0xC4, 0x50, 0xC3});
    CHECK(fake.Fits());
  }
  uint8_t* epoch_stub = nullptr;
  uint8_t* count_stub = nullptr;
  {
    ei::Assembler assembler(std::span<uint8_t>(stub_memory, ec::PARTICLE_COUNT_SIZE));
    ec::BuildParticleCount(&assembler, data, epoch_site, count_site, &epoch_stub, &count_stub);
    CHECK(assembler.Fits());
    for (uint8_t* const site : {epoch_site, count_site}) {
      uint8_t* const stub = (site == epoch_site ? epoch_stub : count_stub);
      site[0] = 0xE9;
      Write32(site + 1, Address(stub) - Address(site + 5));
    }
  }
  using CountCall = uint32_t(__fastcall*)(void* node);
  using EpochCall = void(__fastcall*)(void* node);
  const CountCall count = reinterpret_cast<CountCall>(count_site);
  const EpochCall epoch = reinterpret_cast<EpochCall>(epoch_site);
  std::array<CountNode, 3> nodes;

  // 28b. The registers and the stack.
  {
    const auto call = [&](uint8_t* function, CountNode* node, uint32_t* result) {
      RegisterCall registers = {};
      registers.in[0] = 0xAAAA0001;
      registers.in[1] = Address(node);
      registers.in[2] = 0xAAAA0003;
      registers.in[3] = 0x11111111;
      registers.in[4] = 0x22222222;
      registers.in[5] = 0x33333333;
      registers.in[6] = 0x44444444;
      registers.function = Address(function);
      CallWithRegisters(&registers);
      *result = registers.out[0];
      return registers.out[3] == 0x11111111 && registers.out[4] == 0x22222222 && registers.out[5] == 0x33333333 && registers.out[6] == 0x44444444
             && registers.esp_after == registers.esp_before;
    };
    *data = ec::CountData{};
    *calls = 0;
    nodes[0].value = 123;
    uint32_t result = 0;
    CHECK(call(count_site, &nodes[0], &result) && result == 123 && data->misses == 1 && data->hits == 0 && *calls == 1);  // a miss
    CHECK(call(count_site, &nodes[0], &result) && result == 123 && data->misses == 1 && data->hits == 1 && *calls == 1);  // a hit: the body does not run
    data->check = 1;
    CHECK(call(count_site, &nodes[0], &result) && result == 123 && data->hits == 2 && *calls == 2 && data->mismatches == 0);  // a hit in check mode
    nodes[0].value = 456;                                                                                                        // no epoch: a mismatch
    CHECK(call(count_site, &nodes[0], &result) && result == 456 && data->mismatches == 1 && data->last_fresh == 456 && data->last_cached == 123 && *calls == 3);
    data->check = 0;
    CHECK(call(epoch_site, &nodes[0], &result) && data->generation == 2);
    CHECK(call(count_site, &nodes[0], &result) && result == 456 && data->misses == 2 && *calls == 4);  // the epoch's bump: a miss, the fresh sum kept
    CHECK(call(count_site, &nodes[0], &result) && result == 456 && data->hits == 4 && *calls == 4);
    std::printf("28b. the stubs on a fake game: ebx esi edi ebp and esp kept on a miss, a hit, a hit in check mode (the body runs, a mismatch counted) and the epoch\n");
  }

  // 28c. CachedCountEqualsFresh.
  {
    // Node updates (the epoch, then the sums change: only after an epoch does a node's sum change) and calls of the sum, over three nodes.
    const auto soak = [&](uint64_t seed, bool check, bool epochs, uint32_t operations, uint32_t* wrong, uint32_t* counted) {
      Random random{seed};
      *data = ec::CountData{};
      data->check = (check ? 1u : 0u);
      *calls = 0;
      for (CountNode& node : nodes) node.value = random.Next() % 1000;
      *wrong = 0;
      *counted = 0;
      for (uint32_t operation = 0; operation < operations; ++operation) {
        CountNode* const node = &nodes[random.Next() % nodes.size()];
        if (random.Next() % 10 < 2) {
          if (epochs) epoch(node);
          for (CountNode& changing : nodes) {
            if ((random.Next() & 1) != 0) changing.value = random.Next() % 1000;
          }
        } else {
          ++*counted;
          *wrong += (count(node) != node->value) ? 1 : 0;
        }
      }
    };
    const uint32_t operations = (quick ? 20000 : 400000);
    uint32_t wrong = 0;
    uint32_t counted = 0;
    soak(0x1234567, false, true, operations, &wrong, &counted);
    CHECK(wrong == 0 && data->mismatches == 0 && data->hits + data->misses == counted && *calls == data->misses && data->hits > counted / 10 && data->misses > counted / 10);
    const uint32_t hits = data->hits;
    soak(0x7654321, true, true, operations, &wrong, &counted);
    CHECK(wrong == 0 && data->mismatches == 0 && data->hits + data->misses == counted && *calls == counted && data->hits > counted / 10);
    // Two nodes in turn inside one update: every call is a miss (the cache holds one), every answer fresh.
    *data = ec::CountData{};
    *calls = 0;
    epoch(&nodes[0]);
    nodes[0].value = 11;
    nodes[1].value = 22;
    uint32_t alternating_wrong = 0;
    for (uint32_t turn = 0; turn < 100; ++turn) {
      alternating_wrong += (count(&nodes[turn & 1]) != nodes[turn & 1].value) ? 1 : 0;
    }
    CHECK(alternating_wrong == 0 && data->misses == 100 && data->hits == 0 && *calls == 100);
    // The same node over and over inside one update: the last call of the turns above (node 1) left it cached, so every one is a hit.
    for (uint32_t turn = 0; turn < 100; ++turn) alternating_wrong += (count(&nodes[1]) != 22) ? 1 : 0;
    CHECK(alternating_wrong == 0 && data->misses == 100 && data->hits == 100);
    // The generation's wrap: the bump past 0xFFFFFFFF reaches 0, which is still a different generation from the cached one.
    data->generation = 0xFFFFFFFFu;
    CHECK(count(&nodes[1]) == 22);
    nodes[1].value = 33;
    epoch(&nodes[1]);
    CHECK(data->generation == 0 && count(&nodes[1]) == 33 && data->misses == 102);
    std::printf("28c. CachedCountEqualsFresh: %u calls (%u hits at the first), the answer always the fresh sum, the body run once per miss, once per call in check mode, no mismatch; alternating nodes: all misses; the wrap\n",
                counted, hits);

    // CONTROL: no epoch before the sums change. Check off: stale answers (the test must see them); check on: the mismatches counted, the fresh sum answered.
    soak(0x1234567, false, false, operations, &wrong, &counted);
    CHECK(wrong > 0 && data->mismatches == 0);
    soak(0x1234567, true, false, operations, &wrong, &counted);
    CHECK(wrong == 0 && data->mismatches > 0 && data->last_fresh != data->last_cached);
    std::printf("28c. CONTROL no epoch: check off answers stale sums; check on counts %u mismatches and answers the fresh one\n", data->mismatches);
  }
  VirtualFree(memory, 0, MEM_RELEASE);
  std::printf("28. Cached Particle Count: ok\n");
  return 0;
}
