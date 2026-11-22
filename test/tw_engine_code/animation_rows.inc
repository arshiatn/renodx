// ====================================================================================================================
// 23. Animation Rows on Workers: the animation update's call of the animation texture fill (Attila +0x1893d2a in the update +0x1893cc0; engine_code.hpp 19.),
// included inside tests.cpp's anonymous namespace after lod_pick.inc (JumpTo, Random, Address, Read32 and CHECK of the earlier parts are used). The call goes through a
// cut stub to animation_rows::Dispatch, which for now runs the game's own fill (pass-through), so a row that is on changes nothing. The game's fill itself is replaced in both
// copies of the DLL by a fake (FakeFill: logs its arguments, takes a few items of the row, returns a result with the upper bits set), after Prepare has found it:
// 23a. the site and stub as Prepare made them: the game's bytes, the jump into the stub, the stub's three words on the second page (the count, the two slots) and its last
//    jump to the fill at this copy's base + the build's RVA (the copies map at other bases than the DLL's own), the pages' protections (the stubs' page read-execute, the
//    data page read-write, one allocation), no sentinel left, the sweep entry's stub the same way (the displaced instructions, the jump back at the sweep's `cmp`);
// 23b. the stub alone (the seven arguments again, the callee pops them, eax the result) against the fill's own call, with the slots on and off: the result and the log equal;
//    the whole update (+0x1893cc0) in both copies on random worlds (1 to 6 textures, the rotating tweak on and off, starts and ends of the row, a fill that returns 0): the
//    result, the world and the log of the fill's calls equal;
// 23c. the cut rule: a parked thread at each byte of each stub, the rows switched off each time: nothing comes out, the slots are 0 (the stubs run the game's call), and
//    everything comes out once the thread is gone; a thread in a function the stub called (the count above 0): the sites stay in, then come out; threads just outside the
//    stubs do not hold anything;
// 23d. another mod's bytes over a site on its way out: that site is left alone and the module is pinned; a thread kept in the stub for MAX_FAILED_WRITES presents: pinned; an
//    Uninstall with the count stuck above 0 (an unwind through a stub): the sites stay in, the slots are 0, pinned;
// 23e. 200 switches under 4 threads that run whole updates in both copies on their own worlds.
// ====================================================================================================================

struct FillCall {
  uint32_t cache = 0;
  uint32_t device = 0;
  uint32_t force = 0;
  uint32_t list = 0;
  uint32_t texture = 0;
  uint32_t row_pointer = 0;
  uint32_t row = 0;
  uint32_t end = 0;
  uint32_t out_before = 0;
  bool operator==(const FillCall&) const = default;
};

struct FillScript {
  uint32_t seed = 0;
  uint32_t calls = 0;
  uint32_t fail_at = ~0u;  // the call that returns 0
};

thread_local FillScript fill_script;
thread_local std::vector<FillCall> fill_log;

// The fill's own signature (thiscall, ecx = the cache, seven arguments, the callee pops them) as a fastcall with an unused edx.
using FillFunction = uint32_t(__fastcall*)(void* cache, void*, void* device, uint32_t force, void* list, uint32_t texture, uint32_t* row, uint32_t end, uint32_t* out);

uint32_t __fastcall FakeFill(void* cache, void*, void* device, uint32_t force, void* list, uint32_t texture, uint32_t* row, uint32_t end, uint32_t* out) {
  fill_log.push_back({Address(cache), Address(device), force, Address(list), texture, Address(row), *row, end, *out});
  const uint32_t take = std::min(end - *row, (fill_script.seed + fill_script.calls * 5) % 4 + (fill_script.calls % 3 == 0 ? 1u : 0u));
  *row += take;
  *out += take;
  Write32(static_cast<uint8_t*>(cache) + 0x20, take);
  // The upper bits of eax are the fill's too: the stub and Dispatch keep them (the update reads only al).
  return 0x12345600u | (fill_script.calls++ != fill_script.fail_at ? 1u : 0u);
}

// The animation update's frame: thiscall (ecx = the cache), five stack arguments, callee pops: (device, force, list, row, end).
using UpdateFunction = uint32_t(__fastcall*)(void* cache, void*, void* device, uint32_t force, void* list, uint32_t* row, uint32_t end);
constexpr uint32_t ANIMATION_UPDATE_RVA = 0x1893cc0;
constexpr uint32_t ANIMATION_UPDATE_TWEAK_AT = 0x1C;  // the `mov ecx, imm32` of the rotating tweak's object, in the update

struct AnimationWorld {
  alignas(16) uint8_t cache[0x40];  // [+4] the texture count, [+0x28] the frame number, [+0x2c] the current texture
  uint32_t row;
  uint8_t device[16];
  uint8_t list[16];
  uint32_t padding[4];
  // The fields' bytes, not the struct's tail padding.
  bool operator==(const AnimationWorld& other) const { return std::memcmp(this, &other, offsetof(AnimationWorld, padding) + sizeof(padding)) == 0; }
};

struct AnimationRun {
  uint32_t result = 0;
  AnimationWorld world = {};
  std::vector<FillCall> log;
};

void AnimationRotate(uint8_t* image, bool on) {
  *reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(Read32(image + ANIMATION_UPDATE_RVA + ANIMATION_UPDATE_TWEAK_AT)) + 0x48) = (on ? 1 : 0);
}

struct AnimationSpec {
  uint32_t textures = 1;
  uint32_t current = 0;
  uint32_t start = 0;
  uint32_t end = 0;
  uint32_t force = 0;
  uint32_t seed = 0;
  uint32_t fail_at = ~0u;
  bool rotate = false;
};

AnimationSpec AnimationRandom(Random* random) {
  AnimationSpec spec;
  spec.textures = 1 + random->Next() % 6;
  spec.current = random->Next() % spec.textures;
  spec.end = random->Next() % 40;
  spec.start = (spec.end == 0 ? 0 : random->Next() % (spec.end + 1));
  spec.force = random->Next();
  spec.seed = random->Next();
  spec.fail_at = (random->Next() % 4 == 0 ? random->Next() % 5 : ~0u);
  spec.rotate = (random->Next() & 1) != 0;
  return spec;
}

// The update of `image` on a copy of `world`'s state set from `spec`; `world` is left as it was found.
AnimationRun AnimationUpdate(uint8_t* image, AnimationWorld* world, const AnimationSpec& spec) {
  AnimationWorld saved = *world;
  Write32(world->cache + 4, spec.textures);
  Write32(world->cache + 0x28, 7);
  Write32(world->cache + 0x2c, spec.current);
  world->row = spec.start;
  fill_log.clear();
  fill_script = {.seed = spec.seed, .calls = 0, .fail_at = spec.fail_at};
  AnimationRotate(image, spec.rotate);
  AnimationRun run;
  run.result = reinterpret_cast<UpdateFunction>(image + ANIMATION_UPDATE_RVA)(world->cache, nullptr, world->device, spec.force, world->list, &world->row, spec.end) & 0xFF;
  run.world = *world;
  run.log = fill_log;
  *world = saved;
  return run;
}

// Both copies on the same world: the result's al, every byte of the world and the fill's calls equal. Empty: equal.
const char* AnimationBoth(uint8_t* game, uint8_t* patched, AnimationWorld* world, const AnimationSpec& spec) {
  const AnimationRun a = AnimationUpdate(game, world, spec);
  const AnimationRun b = AnimationUpdate(patched, world, spec);
  if (a.result != b.result) return "the result";
  if (!(a.world == b.world)) return "the world";
  if (a.log != b.log) return "the fill's calls";
  return "";
}

// A thread in a function the fill's stub called: it stays there until `release`.
std::atomic<bool> slow_entered = false;
std::atomic<bool> slow_release = false;
uint32_t __fastcall SlowDispatch(void* cache, void*, void* device, uint32_t force, void* list, uint32_t texture, uint32_t* row, uint32_t end, uint32_t* out) {
  slow_entered = true;
  while (!slow_release.load()) Sleep(1);
  return twattila::animation_rows::Dispatch(cache, nullptr, device, force, list, texture, row, end, out);
}

int RunAnimationRowsTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  using twattila::animation_rows::Stage;
  const uint32_t fill_call_rva = build.code->fill_call_rva;
  const uint32_t fill_rva = build.code->fill_rva;
  CHECK((fill_call_rva != 0) == (build.code->fill_rva != 0));
  const auto update_rows = [&](bool fill, bool sweep) {
    twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 1.f, 0,
                                  {.animation = (fill ? Stage::ON : Stage::OFF), .pending_sweep_trace = sweep});
  };
  const auto fill_slot = [] { return ec::worker_data->fill_slot.load(); };
  const auto sweep_slot = [] { return ec::worker_data->sweep_slot.load(); };
  const uint32_t dispatch = Address(reinterpret_cast<const void*>(&twattila::animation_rows::Dispatch));
  const uint32_t arm = Address(reinterpret_cast<const void*>(&twattila::pending_sweep::Arm));
  if (build.code->sweep_rva == 0) {
    std::printf("23. worker sites: this build has neither the animation fill call nor the sweep (skipped)\n");
    CHECK((ec::found & ec::WORKER_SITES) == 0);
    return 0;
  }
  update_rows(false, false);
  CHECK((ec::written & ec::WORKER_SITES) == 0 && fill_slot() == 0 && sweep_slot() == 0);
  const bool has_fill = fill_call_rva != 0;

  // 23a. The sites and stubs as Prepare made them.
  CHECK(reinterpret_cast<uintptr_t>(patched) != PREFERRED_BASE && reinterpret_cast<uintptr_t>(game) != PREFERRED_BASE);  // the copies are relocated
  CHECK(ec::worker_data != nullptr && ec::worker_cut.begin == ec::page + ec::WORKER_FILL_PAGE && ec::worker_cut.end == ec::page + ec::PAGE_SIZE
        && ec::worker_cut.active == &ec::worker_data->cut_active);
  MEMORY_BASIC_INFORMATION code_info = {};
  MEMORY_BASIC_INFORMATION data_info = {};
  CHECK(VirtualQuery(ec::page, &code_info, sizeof(code_info)) == sizeof(code_info) && VirtualQuery(ec::page + ec::PAGE_SIZE, &data_info, sizeof(data_info)) == sizeof(data_info));
  CHECK(code_info.Protect == PAGE_EXECUTE_READ && code_info.RegionSize == ec::PAGE_SIZE && data_info.Protect == PAGE_READWRITE && data_info.RegionSize == ec::PAGE_SIZE
        && code_info.AllocationBase == ec::page && data_info.AllocationBase == ec::page && reinterpret_cast<uint8_t*>(ec::worker_data) == ec::page + ec::PAGE_SIZE);
  CHECK(::Address(&ec::worker_data->fill_slot) == ::Address(ec::worker_data) + 0x40 && ::Address(&ec::worker_data->sweep_slot) == ::Address(ec::worker_data) + 0x44);
  const auto no_sentinel = [](const uint8_t* stub, size_t size) {
    for (size_t at = 0; at + 3 <= size; ++at) {
      if (stub[at] == 0x5A && stub[at + 1] == 0x5A && stub[at + 2] == 0x5A) return false;
    }
    return true;
  };
  // A stub's absolute operands at their offsets, its last jump at `jump_at` leads to `callee`.
  const auto stub_holds = [&](const uint8_t* stub, size_t size, std::span<const std::pair<size_t, const void*>> words, size_t jump_at, const uint8_t* callee) {
    for (const auto& [at, expected] : words) {
      if (Read32(stub + at) != Address(expected)) return false;
    }
    return no_sentinel(stub, size) && stub[jump_at - 1] == 0xE9 && Address(stub + jump_at + 4) + Read32(stub + jump_at) == Address(callee);
  };
  // The stubs of the page now (Prepare again, in 23d, makes another).
  const auto fill_stub = [] { return static_cast<const uint8_t*>(ec::page + ec::WORKER_FILL_PAGE); };
  const auto sweep_stub = [] { return static_cast<const uint8_t*>(ec::page + ec::WORKER_SWEEP_PAGE); };
  CHECK(ec::sites[ec::WORKER_FILL_INDEX].opcode == 0xE8 && ec::sites[ec::WORKER_SWEEP_INDEX].opcode == 0xE9);
  if (has_fill) {
    const uint8_t* const site = patched + fill_call_rva + ec::FILL_CALL_AT;
    CHECK(ec::sites[ec::WORKER_FILL_INDEX].at == site && (ec::found >> ec::WORKER_FILL_INDEX & 1) != 0);
    CHECK(site[0] == 0xE8 && Address(site + 5) + Read32(site + 1) == Address(patched + fill_rva) && std::memcmp(site, game + fill_call_rva + ec::FILL_CALL_AT, 5) == 0);
    CHECK(std::memcmp(ec::sites[ec::WORKER_FILL_INDEX].game.data(), site, 5) == 0);
    const std::array<std::pair<size_t, const void*>, 5> words = {{{1, &ec::worker_data->fill_slot}, {12, &ec::worker_data->cut_active}, {17, &ec::worker_data->fill_slot},
                                                                  {58, &ec::worker_data->cut_active}, {68, &ec::worker_data->cut_active}}};
    CHECK(stub_holds(fill_stub(), ec::FILL_CALL_STUB.size(), words, 73, patched + fill_rva));
    CHECK(std::memcmp(fill_stub() + 25, ec::FILL_CALL_STUB.data() + 25, 30) == 0 && std::memcmp(fill_stub() + 62, "\xC2\x1C\x00", 3) == 0);
  }
  CHECK(ec::sites[ec::WORKER_SWEEP_INDEX].at == patched + build.code->sweep_rva + ec::SWEEP_ENTRY_AT && (ec::found >> ec::WORKER_SWEEP_INDEX & 1) != 0);
  {
    constexpr std::array<uint8_t, 5> ENTRY = {0x8B, 0x46, 0x18, 0x53, 0x8D};  // mov eax, [esi + 0x18]; push ebx; the first byte of lea ebx, [eax - 1]
    const uint8_t* const site = patched + build.code->sweep_rva + ec::SWEEP_ENTRY_AT;
    CHECK(std::memcmp(site, ENTRY.data(), 5) == 0 && std::memcmp(game + build.code->sweep_rva + ec::SWEEP_ENTRY_AT, ENTRY.data(), 5) == 0);
    CHECK(site[5] == 0x58 && site[6] == 0xFF && site[7] == 0x3B && site[8] == 0xD8);  // the rest of the lea, then the cmp the stub goes back to
    CHECK(ec::SWEEP_ENTRY_RESUME == ec::SWEEP_ENTRY_AT + 7);
    const std::array<std::pair<size_t, const void*>, 4> words = {{{1, &ec::worker_data->sweep_slot}, {12, &ec::worker_data->cut_active}, {17, &ec::worker_data->sweep_slot},
                                                                  {32, &ec::worker_data->cut_active}}};
    CHECK(stub_holds(sweep_stub(), ec::SWEEP_ENTRY_STUB.size(), words, 44, patched + build.code->sweep_rva + ec::SWEEP_ENTRY_RESUME));
    CHECK(std::memcmp(sweep_stub() + 36, ENTRY.data(), 3) == 0 && sweep_stub()[39] == 0x53 && std::memcmp(sweep_stub() + 40, "\x8D\x58\xFF", 3) == 0);
  }
  update_rows(has_fill, true);
  CHECK((ec::written & ec::WORKER_SITES) == (has_fill ? ec::WORKER_SITES : ec::WORKER_SWEEP_BIT));
  CHECK(fill_slot() == (has_fill ? dispatch : 0) && sweep_slot() == arm);
  if (has_fill) {
    const uint8_t* const site = patched + fill_call_rva + ec::FILL_CALL_AT;
    CHECK(site[0] == 0xE8 && Address(site + 5) + Read32(site + 1) == Address(fill_stub()));
  }
  {
    const uint8_t* const site = patched + build.code->sweep_rva + ec::SWEEP_ENTRY_AT;
    CHECK(site[0] == 0xE9 && Address(site + 5) + Read32(site + 1) == Address(sweep_stub()));
  }
  std::printf("23. worker sites: found, written and read-execute / read-write pages as the design has them (the copies at %p and %p, not the DLL's own base)\n",
              static_cast<void*>(game), static_cast<void*>(patched));
  if (!has_fill) {
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == 0 && sweep_slot() == 0);
    std::printf("   (this build has no animation fill call: only the sweep entry is looked for)\n");
    return 0;
  }

  // The game's fill is the fake in both copies from here (after Prepare found it); the harness puts the bytes back at the end.
  std::array<std::array<uint8_t, 5>, 2> saved_fill = {};
  std::memcpy(saved_fill[0].data(), game + fill_rva, 5);
  std::memcpy(saved_fill[1].data(), patched + fill_rva, 5);
  JumpTo(game + fill_rva, reinterpret_cast<const void*>(&FakeFill));
  JumpTo(patched + fill_rva, reinterpret_cast<const void*>(&FakeFill));
  const auto restore_fill = [&] {
    std::memcpy(game + fill_rva, saved_fill[0].data(), 5);
    std::memcpy(patched + fill_rva, saved_fill[1].data(), 5);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  };
  const auto reprepare = [&] {  // the sites found again (the fill's head must be the game's for that)
    restore_fill();
    for (ec::Site& site : ec::sites) site.at = nullptr;
    ec::prepared = false;
    ec::written = 0;
    ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
    JumpTo(game + fill_rva, reinterpret_cast<const void*>(&FakeFill));
    JumpTo(patched + fill_rva, reinterpret_cast<const void*>(&FakeFill));
  };
  const auto foreign = [&](size_t index, const std::array<uint8_t, ec::SITE_SIZE>& bytes) {
    DWORD protection;
    VirtualProtect(ec::sites[index].at, ec::SITE_SIZE, PAGE_EXECUTE_READWRITE, &protection);
    std::memcpy(ec::sites[index].at, bytes.data(), ec::SITE_SIZE);
    VirtualProtect(ec::sites[index].at, ec::SITE_SIZE, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  };
  auto world = std::make_unique<AnimationWorld>();
  Random random = {.state = 0x23A1D00D5EEDull};
  for (uint8_t& byte : world->cache) byte = static_cast<uint8_t>(random.Next());

  // 23b. The stub alone against the fill's own call, then whole updates.
  {
    const auto stub_function = reinterpret_cast<FillFunction>(const_cast<uint8_t*>(fill_stub()));
    const auto game_function = reinterpret_cast<FillFunction>(game + fill_rva);
    uint64_t bad = 0;
    const int rounds = (quick ? 2000 : 20000);
    for (int round = 0; round < rounds; ++round) {
      const AnimationSpec spec = AnimationRandom(&random);
      const bool on = (round & 1) != 0;
      ec::worker_data->fill_slot.store(on ? dispatch : 0);  // a slot off runs the game's call from the stub; on, through Dispatch
      std::array<AnimationRun, 2> runs;
      for (int side = 0; side < 2; ++side) {
        AnimationWorld saved = *world;
        fill_log.clear();
        fill_script = {.seed = spec.seed, .calls = 0, .fail_at = spec.fail_at};
        world->row = spec.start;
        uint32_t out = 0x77;
        const FillFunction function = (side == 0 ? game_function : stub_function);
        runs[side].result = function(world->cache, nullptr, world->device, spec.force, world->list, spec.current, &world->row, spec.end, &out);
        runs[side].world = *world;
        runs[side].log = fill_log;
        runs[side].world.padding[0] = out;
        *world = saved;
      }
      if (runs[0].result != runs[1].result || !(runs[0].world == runs[1].world) || runs[0].log != runs[1].log) {
        if (++bad <= 5) std::printf("   round %d: the stub differs from the fill's own call (result %08X / %08X)\n", round, runs[0].result, runs[1].result);
      }
    }
    CHECK(bad == 0);
    ec::worker_data->fill_slot.store(dispatch);
    std::printf("   the stub against the game's call, %d random calls with the slot on and off: results (eax whole), world, out and the fill's log equal\n", rounds);
    uint64_t wrong = 0;
    const int updates = (quick ? 4000 : 40000);
    for (int round = 0; round < updates; ++round) {
      const AnimationSpec spec = AnimationRandom(&random);
      if (const char* what = AnimationBoth(game, patched, world.get(), spec); what[0] != 0 && ++wrong <= 5) {
        std::printf("   update %d (%u textures, rows %u to %u, rotate %d): %s differs\n", round, spec.textures, spec.start, spec.end, spec.rotate ? 1 : 0, what);
      }
    }
    CHECK(wrong == 0);
    std::printf("   %d whole updates in both copies (rotating tweak on and off, a failing fill, any start and end): the result, the world and the fill's calls equal\n", updates);
  }

  // 23c. The cut rule with a parked thread at each byte of each stub.
  {
    std::atomic<bool> stop = false;
    std::thread parked([&] {
      while (!stop.load()) Sleep(1);
    });
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, GetThreadId(parked.native_handle()));
    CHECK(thread != nullptr);
    int wrong = 0;
    int checked = 0;
    // Eip at `at`: the rows go off; where the thread counts as inside the stubs nothing may come out.
    const auto try_off = [&](const uint8_t* at, bool inside) {
      wrong += (SuspendThread(thread) == static_cast<DWORD>(-1)) ? 1 : 0;
      CONTEXT saved = {.ContextFlags = CONTEXT_CONTROL};
      wrong += (GetThreadContext(thread, &saved) == FALSE) ? 1 : 0;
      CONTEXT moved = saved;
      moved.Eip = Address(at);
      wrong += (SetThreadContext(thread, &moved) == FALSE) ? 1 : 0;
      ec::failed_writes = 0;
      update_rows(false, false);
      const bool still_in = (ec::written & ec::WORKER_SITES) == ec::WORKER_SITES;
      wrong += (still_in != inside || (inside && (fill_slot() != 0 || sweep_slot() != 0))) ? 1 : 0;
      wrong += (SetThreadContext(thread, &saved) == FALSE) ? 1 : 0;
      ResumeThread(thread);
      ++checked;
      ec::failed_writes = 0;
      update_rows(true, true);  // back on
      wrong += ((ec::written & ec::WORKER_SITES) != ec::WORKER_SITES || fill_slot() != dispatch || sweep_slot() != arm) ? 1 : 0;
    };
    for (size_t offset = 0; offset < ec::FILL_CALL_STUB.size(); ++offset) try_off(fill_stub() + offset, true);
    for (size_t offset = 0; offset < ec::SWEEP_ENTRY_STUB.size(); ++offset) try_off(sweep_stub() + offset, true);
    try_off(fill_stub() - 1, false);           // the byte before the stubs: another stub's
    try_off(ec::page + ec::PAGE_SIZE, false);  // the data page's first byte
    stop = true;
    parked.join();
    CloseHandle(thread);
    CHECK(wrong == 0);
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == 0 && fill_slot() == 0 && sweep_slot() == 0);
    update_rows(true, true);
    CHECK((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES && fill_slot() == dispatch);
    std::printf("   a parked thread at each of the %d bytes of the two stubs: nothing comes out, the slots read 0 meanwhile; just outside, both come out\n", checked - 2);
  }

  // 23c (2). A thread inside a function the stub called (the count above 0): the sites stay in, then come out.
  {
    ec::worker_data->fill_slot.store(Address(reinterpret_cast<const void*>(&SlowDispatch)));
    slow_entered = false;
    slow_release = false;
    std::atomic<bool> caller_ok = false;
    std::thread caller([&] {
      auto own = std::make_unique<AnimationWorld>();
      const AnimationSpec spec = {.textures = 1, .current = 0, .start = 0, .end = 9, .force = 0, .seed = 3, .fail_at = ~0u, .rotate = false};
      const AnimationRun run = AnimationUpdate(patched, own.get(), spec);
      caller_ok = (run.result == 1 && !run.log.empty());
    });
    while (!slow_entered.load()) Sleep(1);
    CHECK(ec::worker_data->cut_active.load() == 1);
    ec::failed_writes = 0;
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES && fill_slot() == 0 && sweep_slot() == 0);
    // The same with the sweep's row alone off: the fill's site is wanted, nothing changes there; the sweep entry's stub is cut too.
    ec::failed_writes = 0;
    update_rows(true, false);
    CHECK((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES && fill_slot() == dispatch);  // asked again: its word is back (the slow thread is past its test)
    ec::failed_writes = 0;
    update_rows(false, false);
    slow_release = true;
    caller.join();
    CHECK(caller_ok.load() && ec::worker_data->cut_active.load() == 0);
    ec::failed_writes = 0;
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == 0 && fill_slot() == 0 && sweep_slot() == 0 && !ec::module_pinned);
    update_rows(true, true);
    CHECK((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES && fill_slot() == dispatch && sweep_slot() == arm);
    std::printf("   a thread inside the called function (count 1): both sites stay in, the slots read 0 or as asked, and both come out once it is gone\n");
  }

  // 23d. The pin: another mod's bytes over a site on its way out, a thread kept in the stub, an Uninstall with the count stuck above 0.
  {
    const std::array<uint8_t, ec::SITE_SIZE> hook = {0xE9, 0x10, 0x20, 0x30, 0x40};
    foreign(ec::WORKER_FILL_INDEX, hook);
    ec::failed_writes = 0;
    update_rows(false, false);
    CHECK(ec::module_pinned && (ec::written & ec::WORKER_SITES) == 0 && (ec::found & ec::WORKER_FILL_BIT) == 0
          && std::memcmp(ec::sites[ec::WORKER_FILL_INDEX].at, hook.data(), 5) == 0 && fill_slot() == 0);
    ec::module_pinned = false;
    foreign(ec::WORKER_FILL_INDEX, ec::sites[ec::WORKER_FILL_INDEX].game);
    reprepare();
    update_rows(true, true);
    CHECK((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES);

    // A thread kept in the stub for MAX_FAILED_WRITES presents: pinned, the sites still in.
    std::atomic<bool> stop = false;
    std::thread parked([&] {
      while (!stop.load()) Sleep(1);
    });
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, GetThreadId(parked.native_handle()));
    CHECK(thread != nullptr && SuspendThread(thread) != static_cast<DWORD>(-1));
    CONTEXT saved = {.ContextFlags = CONTEXT_CONTROL};
    CHECK(GetThreadContext(thread, &saved) != FALSE);
    CONTEXT moved = saved;
    moved.Eip = Address(fill_stub() + 20);
    CHECK(SetThreadContext(thread, &moved) != FALSE);
    ec::failed_writes = 0;
    for (uint32_t present = 0; present < ec::MAX_FAILED_WRITES; ++present) update_rows(false, false);
    CHECK(ec::module_pinned && ec::failed_writes == ec::MAX_FAILED_WRITES && (ec::written & ec::WORKER_SITES) == ec::WORKER_SITES);
    CHECK(SetThreadContext(thread, &saved) != FALSE);
    ResumeThread(thread);
    stop = true;
    parked.join();
    CloseHandle(thread);
    ec::module_pinned = false;
    update_rows(true, true);  // the rows asked for again: the count starts over
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == 0 && !ec::module_pinned);
    update_rows(true, true);

    // An unwind through a stub left the count raised: Uninstall cannot take the sites out; the module is pinned, the words are 0.
    ec::worker_data->cut_active.store(1);
    twattila::engine_code::Uninstall();
    CHECK((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES && ec::module_pinned && fill_slot() == 0 && sweep_slot() == 0);
    ec::worker_data->cut_active.store(0);
    ec::module_pinned = false;
    ec::failed_writes = 0;
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == 0 && !ec::module_pinned);
    std::printf("   another mod's bytes over a site, a thread kept in a stub, a stuck count at Uninstall: each leaves the site in, pins the module and zeroes the words\n");
  }

  // 23e. 200 switches under 4 threads running whole updates in both copies on their own worlds.
  {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> run_count = 0;
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        auto own = std::make_unique<AnimationWorld>();
        Random thread_random = {.state = 0xA11CE0000ull + static_cast<uint64_t>(worker) * 0x9E3779B97F4Aull};
        while (!stop.load()) {
          AnimationSpec spec = AnimationRandom(&thread_random);
          spec.rotate = false;  // the tweak is one byte of the copy: the threads agree on it
          if (const char* what = AnimationBoth(game, patched, own.get(), spec); what[0] != 0 && ++bad <= 5) {
            std::printf("   thread %d: the update differs in %s\n", worker, what);
          }
          ++run_count;
        }
      });
    }
    int waited = 0;
    int never = 0;
    bool on = true;
    for (int switches = 0; switches < 200; ++switches) {
      on = !on;
      int tries = 0;
      do {
        update_rows(on, on);
        ++tries;
      } while ((((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES) != on) && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      waited += tries > 1 ? 1 : 0;
      never += (((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES) != on) ? 1 : 0;
    }
    update_rows(true, true);
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("   200 switches while 4 threads made %llu whole updates in both copies: %llu wrong (%d writes waited for a thread inside a stub, %d switches never reached)\n",
                run_count.load(), bad.load(), waited, never);
    CHECK(bad.load() == 0 && never == 0 && run_count.load() > 0 && (ec::written & ec::WORKER_SITES) == ec::WORKER_SITES && fill_slot() == dispatch && !ec::module_pinned);
  }

  update_rows(false, false);
  CHECK((ec::written & ec::WORKER_SITES) == 0 && fill_slot() == 0 && sweep_slot() == 0);
  restore_fill();
  AnimationRotate(game, false);
  AnimationRotate(patched, false);
  return 0;
}
