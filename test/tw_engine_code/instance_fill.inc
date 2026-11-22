// ====================================================================================================================
// 29. Parallel Instance Fill's hook-up (src/games/twattila/instance_fill.hpp, engine_code.hpp PausedWrite's fourth table; Attila only), included inside tests.cpp's anonymous
// namespace after particle_count.inc (Address, Read32, CHECK, log_lines and the ei / eu aliases of part 25 are used). The 4 call sites of the ring fills (the dispatcher itself and the
// fills it makes are test/tw_instance_fill's); here the row's Update, PausedWrite and Uninstall around them, on the mapped game B:
// 29a. The sites in B as Prepare made them (found whole, the game's bytes, the E8 to the module's own entry); the row on: B differs from A only in the 4 calls' 5 bytes each, the
//    dispatcher's flag follows the row; off: the game's bytes back, the flag off, the module not pinned;
// 29b. Another mod's byte in a site going in: none written, the group stands down (found 0), nothing else touched, named once; the sites found again by a new Prepare go in;
//    another mod's byte over our E8 going out: that site stays (the other mod's), the rest come out, found loses it (no partial set goes in again), the module is pinned;
// 29c. A page that cannot be made writable (the control, Rewrite's n-th protect fails): going in, none is written and the group stands down; going out, only that site stays and the
//    module is pinned; the page's protection is as it was after every Rewrite (read and execute, checked with VirtualQuery); a forced failure with no stand-down changes nothing;
// 29d. A thread parked at each byte of each site (SuspendThread / SetThreadContext, as 25f): refused strictly inside (past the first byte, before the byte after the call), going in
//    and going out; the dispatcher's count above 0 (a thread inside Dispatch): the sites stay in, the flag follows the row at once, they come out once the count is 0; going in is
//    not held by it; a stuck count at Uninstall leaves the sites in, pins the module and says so;
// 29e. Both tables of one window (the effect update's and this one's) go in and come out together, the page protections kept; Uninstall takes the sites out and the module is not
//    pinned;
// 29f. The fold's 3 Unmap stubs through the same Update, PausedWrite and Uninstall (the private build's table of 7 sites: a second compile, TWATTILA_DRAW_RECORDING, with 29a to 29e left to
//    the plain build).
// ====================================================================================================================

#if defined(TWATTILA_DRAW_RECORDING)
// 29f. The fold's 3 Unmap stubs (the private build's 7-site table; 29a to 29e are the plain build's) through the real Update, PausedWrite and Uninstall on the mapped game B, with
// Threaded Draw Recording's sites prepared there (a second compile of this test: /DTWATTILA_DRAW_RECORDING, `quick fill`):
// - the stubs go in one present after the fills and their cuts; a cut out takes its stub out in the same window (stub k follows DEPTH_CUT, COLOUR_CUT, DEPTH_B_CUT), the fold row off
//   takes all three, the fills' row off everything; a cut back in is followed by its stub one present later;
// - a thread parked at each byte of each stub (11 bytes): refused strictly inside, going in and going out;
// - a thread inside the game's Unmap through a stub (the stub's `active` count): the stubs stay in, then come out;
// - Uninstall takes the stubs out, the module not pinned; a fold open on another thread keeps them in (and pins the module) unless it closes within the wait.
int RunFoldStubTests(uint8_t* patched, uint32_t text_rva, uint32_t text_size) {
  namespace ec = twattila::engine_code::internal;
  namespace fill = twattila::instance_fill;
  namespace fi = twattila::instance_fill::internal;
  namespace dr = twattila::draw_recording::internal;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  constexpr uint32_t ALL_GROUPS = 0xF;
  constexpr size_t STUBS = fill::SITE_COUNT - fill::FIRST_UNMAP;
  const std::array<uint64_t, STUBS> cuts = {ec::RecordingBit(ec::RecordingSite::DEPTH_CUT), ec::RecordingBit(ec::RecordingSite::COLOUR_CUT),
                                            ec::RecordingBit(ec::RecordingSite::DEPTH_B_CUT)};
  // The groups (draw_recording::Group bits) whose sites hold each cut: a cut is asked for while one of them is.
  std::array<uint32_t, STUBS> cut_groups = {};
  for (size_t stub = 0; stub < STUBS; ++stub) {
    for (size_t group = 0; group < ec::RECORDING_OWN.size(); ++group) {
      cut_groups[stub] |= ((ec::RECORDING_OWN[group] & cuts[stub]) != 0 ? 1u : 0u) << group;
    }
    CHECK(cut_groups[stub] != 0);
  }
  const auto update = [&](uint32_t groups, bool fill_on, bool fold_on) {
    ec::failed_writes = 0;
    twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 1.f, groups,
                                  {.instance_fill = fill_on, .fill_fold = fold_on});
  };
  update(0, false, false);
  CHECK(fill::Prepare(reinterpret_cast<HMODULE>(patched)) && fi::found == fill::ALL_SITES && fi::written == 0);
  CHECK(ec::PrepareRecording(reinterpret_cast<HMODULE>(patched)) && ec::recording_found == (uint64_t{1} << ec::RECORDING_SITE_COUNT) - 1);
  const std::vector<uint8_t> unpatched(patched + text_rva, patched + text_rva + text_size);
  const auto game_is_back = [&] { return std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0; };
  const auto stub_bit = [](size_t stub) { return 1u << (fill::FIRST_UNMAP + stub); };
  const auto stub_is = [&](size_t stub, bool ours) {
    const fill::PatchedSite& site = fi::sites[fill::FIRST_UNMAP + stub];
    return std::memcmp(site.at, (ours ? site.patch : site.game).data(), site.size) == 0;
  };
  const auto cut_in = [&](size_t stub) { return (ec::recording_written & cuts[stub]) != 0; };
  const auto said = [&](std::string_view text) {
    size_t count = 0;
    for (const std::string& line : log_lines) count += (line.find(text) != std::string::npos ? 1 : 0);
    return count;
  };

  // The stubs go in one present after the fills and the cuts.
  update(ALL_GROUPS, true, true);
  CHECK(fi::written == fill::R1_SITES && fi::asked == fill::R1_SITES && cut_in(0) && cut_in(1) && cut_in(2) && !fi::fold_on.load());
  for (size_t stub = 0; stub < STUBS; ++stub) CHECK(stub_is(stub, false));
  update(ALL_GROUPS, true, true);
  CHECK(fi::written == fill::ALL_SITES && fi::asked == fill::ALL_SITES && fi::fold_on.load() && !ec::module_pinned);
  for (size_t stub = 0; stub < STUBS; ++stub) {
    CHECK(stub_is(stub, true) && fi::sites[fill::FIRST_UNMAP + stub].size == 11);
  }
  update(ALL_GROUPS, true, true);
  CHECK(fi::written == fill::ALL_SITES);

  // A cut out takes its stub out in the same window (its own stub only), a cut back in is followed by its stub one present later.
  for (size_t stub = 0; stub < STUBS; ++stub) {
    const uint32_t groups = ALL_GROUPS & ~cut_groups[stub];
    uint32_t expected = fill::ALL_SITES;
    for (size_t other = 0; other < STUBS; ++other) {
      expected &= ~((cut_groups[other] & groups) == 0 ? stub_bit(other) : 0u);
    }
    update(groups, true, true);
    CHECK(!cut_in(stub) && fi::written == expected && (fi::written & stub_bit(stub)) == 0);
    for (size_t other = 0; other < STUBS; ++other) CHECK(stub_is(other, (expected & stub_bit(other)) != 0));
    update(ALL_GROUPS, true, true);
    CHECK(cut_in(stub) && (fi::written & stub_bit(stub)) == 0);  // the cut is back, its stub follows
    update(ALL_GROUPS, true, true);
    CHECK(fi::written == fill::ALL_SITES);
  }
  // The fold row off takes the stubs only; on again, they follow at once (the fills and the cuts were in before the window).
  update(ALL_GROUPS, true, false);
  CHECK(fi::written == fill::R1_SITES && !fi::fold_on.load() && cut_in(0) && cut_in(1) && cut_in(2));
  update(ALL_GROUPS, true, true);
  CHECK(fi::written == fill::ALL_SITES);
  // The fills' row off takes everything out in one window.
  update(ALL_GROUPS, false, true);
  CHECK(fi::written == 0 && !fi::row_on.load() && !fi::fold_on.load());
  for (size_t stub = 0; stub < STUBS; ++stub) CHECK(stub_is(stub, false));
  update(ALL_GROUPS, true, true);
  update(ALL_GROUPS, true, true);
  CHECK(fi::written == fill::ALL_SITES);
  std::printf("29f. the stubs go in a present after the fills and their cuts; a cut out takes its stub out in the same window (stub k = DEPTH_CUT, COLOUR_CUT, DEPTH_B_CUT), back in one present later; the fold row off: the stubs only; the fills' row off: all\n");

  // A thread parked at each byte of each stub (SuspendThread / SetThreadContext, as 29d): refused strictly inside, going in and going out.
  {
    std::atomic<bool> stop = false;
    std::thread parked([&] {
      while (!stop.load()) Sleep(1);
    });
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, GetThreadId(parked.native_handle()));
    CHECK(thread != nullptr);
    int wrong = 0;
    int checked = 0;
    // Update with the thread's Eip at `where`.
    const auto update_parked = [&](const uint8_t* where, bool fold_on) {
      wrong += (SuspendThread(thread) == static_cast<DWORD>(-1)) ? 1 : 0;
      CONTEXT saved = {.ContextFlags = CONTEXT_CONTROL};
      wrong += (GetThreadContext(thread, &saved) == FALSE) ? 1 : 0;
      CONTEXT moved = saved;
      moved.Eip = Address(where);
      wrong += (SetThreadContext(thread, &moved) == FALSE) ? 1 : 0;
      update(ALL_GROUPS, true, fold_on);
      wrong += (SetThreadContext(thread, &saved) == FALSE) ? 1 : 0;
      ResumeThread(thread);
      ++checked;
    };
    for (size_t stub = 0; stub < STUBS; ++stub) {
      const uint8_t* const start = fi::sites[fill::FIRST_UNMAP + stub].at;
      const uint32_t size = fi::sites[fill::FIRST_UNMAP + stub].size;
      for (uint32_t offset = 0; offset <= size; ++offset) {
        const bool inside = (offset > 0 && offset < size);
        update(ALL_GROUPS, true, false);
        wrong += (fi::written != fill::R1_SITES) ? 1 : 0;
        update_parked(start + offset, true);  // going in
        wrong += ((fi::written == fill::ALL_SITES) == inside) ? 1 : 0;
        update(ALL_GROUPS, true, true);
        wrong += (fi::written != fill::ALL_SITES) ? 1 : 0;
        update_parked(start + offset, false);  // going out
        wrong += ((fi::written == fill::R1_SITES) == inside) ? 1 : 0;
        update(ALL_GROUPS, true, true);
        wrong += (fi::written != fill::ALL_SITES) ? 1 : 0;
      }
    }
    stop = true;
    parked.join();
    CloseHandle(thread);
    CHECK(wrong == 0 && fi::written == fill::ALL_SITES);
    std::printf("29f. a thread parked at %d places (each byte of each 11-byte stub and the byte after, going in and going out): refused strictly inside, free at the ends\n", checked);
  }

  // A thread inside the game's Unmap through a stub (count above 0): the stubs stay in whatever the rows ask, they come out once it has left.
  {
    struct FakeBuffer {
      void* const* vtable;
    };
    static std::atomic<bool> inside_unmap = false;
    static std::atomic<bool> let_go = false;
    struct Hook {
      static void __thiscall Unmap(FakeBuffer*, void*) {
        inside_unmap.store(true);
        while (!let_go.load()) Sleep(1);
      }
    };
    static void* const vtable[5] = {nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void*>(&Hook::Unmap)};
    FakeBuffer buffer = {vtable};
    std::array<uint8_t, 0x80> ring = {};
    FakeBuffer* const buffer_pointer = &buffer;
    std::memcpy(ring.data() + dr::RING_BUFFER, &buffer_pointer, sizeof(buffer_pointer));
    for (size_t stub = 0; stub < STUBS; ++stub) {
      inside_unmap.store(false);
      let_go.store(false);
      std::thread unmapping([&] { fi::UNMAP_ENTRIES[stub](ring.data(), nullptr, nullptr); });
      while (!inside_unmap.load()) Sleep(1);
      CHECK(fi::active.load() == 1);
      update(ALL_GROUPS, true, false);
      CHECK(fi::written == fill::ALL_SITES && !fi::fold_on.load() && ec::failed_writes == 1);  // refused: the stubs and the fold flag
      update(0, false, false);
      CHECK(fi::written == fill::ALL_SITES && !fi::row_on.load() && ec::failed_writes == 1);
      let_go.store(true);
      unmapping.join();
      CHECK(fi::active.load() == 0);
      update(ALL_GROUPS, true, false);
      CHECK(fi::written == fill::R1_SITES);
      update(ALL_GROUPS, true, true);
      CHECK(fi::written == fill::ALL_SITES);
    }
    std::printf("29f. a thread inside the game's Unmap through each stub: the count holds the stubs in (the flag follows the row at once), they come out once it has left\n");
  }

  // Uninstall: everything out, no pin.
  log_lines.clear();
  twattila::engine_code::Uninstall();
  CHECK(fi::written == 0 && ec::recording_written == 0 && game_is_back() && !ec::module_pinned && !ec::recording_left_in);
  CHECK(said("left in at detach") == 0);
  // A fold open on another thread at detach (a render thread between a fill site and its stub): it closes within the wait, the stubs come out;
  // one that stays open keeps the stubs in and pins the module (taking them out would let that thread's game Unmap run under the fill's workers).
  {
    update(ALL_GROUPS, true, true);
    update(ALL_GROUPS, true, true);
    CHECK(fi::written == fill::ALL_SITES);
    std::atomic<bool> stop = false;
    std::thread other([&] {
      while (!stop.load()) Sleep(1);
    });
    const DWORD other_id = GetThreadId(other.native_handle());
    dr::fold.thread.store(other_id);
    std::thread closer([&] {
      Sleep(20);
      dr::fold.thread.store(0);
    });
    twattila::engine_code::Uninstall();
    closer.join();
    CHECK(fi::written == 0 && game_is_back() && !ec::module_pinned);
    update(ALL_GROUPS, true, true);
    update(ALL_GROUPS, true, true);
    CHECK(fi::written == fill::ALL_SITES);
    dr::fold.thread.store(other_id);
    const uint64_t recording_before = ec::recording_written;
    log_lines.clear();
    twattila::engine_code::Uninstall();
    CHECK(fi::written == fill::FOLD_SITES && ec::module_pinned && said("left in at detach") == 2 && said("Threaded Draw Recording: left in at detach") == 1);
    for (size_t stub = 0; stub < STUBS; ++stub) CHECK(stub_is(stub, true));
    // The recording sites stay in with them, the cuts among them (the cut's dispatcher closes the fold and makes the noted Unmap), and the
    // sites are marked left in so draw_recording::Uninstall keeps Shared.
    CHECK(recording_before != 0 && ec::recording_written == recording_before && ec::recording_left_in && twattila::engine_code::DrawRecordingSitesIn());
    for (size_t stub = 0; stub < STUBS; ++stub) CHECK(cut_in(stub));
    for (size_t index = 0; index < ec::recording_sites.size(); ++index) {
      if ((recording_before >> index & 1) != 0) CHECK(!std::memcmp(ec::recording_sites[index].at, ec::recording_sites[index].jump.data(), ec::SITE_SIZE));
    }
    for (size_t site = 0; site < fill::FILL_SITE_COUNT; ++site) CHECK(!std::memcmp(fi::sites[site].at, fi::sites[site].game.data(), fi::sites[site].size));
    dr::fold.thread.store(0);
    stop = true;
    other.join();
    ec::module_pinned = false;
    ec::recording_left_in = false;
    update(0, false, false);
    CHECK(fi::written == 0 && ec::recording_written == 0 && game_is_back());
  }
  std::printf("29f. Uninstall takes the stubs out, the module not pinned; a fold open on another thread keeps the stubs and the recording sites (the cuts) in, pins the module and marks them left in, unless it closes within the wait\n");
  return 0;
}
#endif

int RunInstanceFillTests(const TestBuild& build, uint8_t* game, uint8_t* patched, uint32_t text_rva, uint32_t text_size) {
  namespace ec = twattila::engine_code::internal;
  namespace fill = twattila::instance_fill;
  namespace fi = twattila::instance_fill::internal;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  if (build.code != &ec::ATTILA_BUILD) {
    std::printf("29. parallel instance fill: Attila only (skipped)\n");
    return 0;
  }
#if defined(TWATTILA_DRAW_RECORDING)
  return RunFoldStubTests(patched, text_rva, text_size);
#else
  const auto rows = [&](bool fill_on, bool effect_on = false) {
    twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 1.f, 0,
                                  {.effect_update = effect_on, .instance_fill = fill_on});
  };
  const auto said = [&](std::string_view text) {
    size_t count = 0;
    for (const std::string& line : log_lines) count += (line.find(text) != std::string::npos ? 1 : 0);
    return count;
  };
  rows(false);
  CHECK(fill::Prepare(reinterpret_cast<HMODULE>(patched)) && fi::found == fill::ALL_SITES && fi::written == 0);
  const std::vector<uint8_t> unpatched(patched + text_rva, patched + text_rva + text_size);
  const auto game_is_back = [&] { return std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0; };
  const auto site_is = [&](size_t index, bool ours) {
    return std::memcmp(fi::sites[index].at, (ours ? fi::sites[index].patch : fi::sites[index].game).data(), fill::SITE_SIZE) == 0;
  };
  // The page the 4 sites share (with 6 recording sites): read and execute while a Rewrite runs, checked after it.
  uint8_t* const page = patched + (fill::SITES[0].rva & ~0xFFFu);
  const auto page_protection = [&] {
    MEMORY_BASIC_INFORMATION info = {};
    return (VirtualQuery(page, &info, sizeof(info)) == sizeof(info) ? info.Protect : 0ul);
  };
  const auto set_page = [&](DWORD protection) {
    DWORD ignored;
    return VirtualProtect(page, 0x1000, protection, &ignored) != FALSE;
  };
  // Update with the page read and execute: true when it is the same afterwards (the test's own edits need it writable in between).
  const auto rows_on_rx_page = [&](bool fill_on, bool effect_on = false) {
    set_page(PAGE_EXECUTE_READ);
    rows(fill_on, effect_on);
    const bool same = (page_protection() == PAGE_EXECUTE_READ);
    set_page(PAGE_EXECUTE_READWRITE);
    return same;
  };

  // 29a. The sites as Prepare made them, and the row on and off.
  for (size_t index = 0; index < fill::SITE_COUNT; ++index) {
    CHECK(fi::sites[index].at == patched + fill::SITES[index].rva && site_is(index, false));
    CHECK(fi::sites[index].patch[0] == 0xE8
          && Address(fi::sites[index].at + fill::SITE_SIZE) + Read32(fi::sites[index].patch.data() + 1) == Address(reinterpret_cast<const void*>(fi::ENTRIES[index])));
  }
  CHECK(rows_on_rx_page(true));
  CHECK(fi::written == fill::ALL_SITES && fi::asked == fill::ALL_SITES && fi::row_on.load() && !ec::module_pinned);
  {
    size_t differing = 0;
    for (uint32_t at = text_rva; at < text_rva + text_size; ++at) {
      if (unpatched[at - text_rva] == patched[at]) continue;
      ++differing;
      bool in_site = false;
      for (const auto& site : fi::sites) in_site |= (patched + at >= site.at && patched + at < site.at + fill::SITE_SIZE);
      CHECK(in_site);
    }
    for (size_t index = 0; index < fill::SITE_COUNT; ++index) CHECK(site_is(index, true));
    CHECK(differing <= fill::SITE_COUNT * 4);  // each call's displacement (the E8 stays)
  }
  CHECK(rows_on_rx_page(false));
  CHECK(fi::written == 0 && fi::asked == 0 && !fi::row_on.load() && game_is_back() && !ec::module_pinned && fi::found == fill::ALL_SITES);
  std::printf("29a. 4 sites as Prepare made them; the row on: B differs from A only in the calls' displacements, the flag on; off: the game's bytes back, the flag off, not pinned\n");

  // 29b. Another mod's bytes.
  {
    log_lines.clear();
    const uint32_t at = fill::SITES[2].rva + 1;
    patched[at] ^= 0x04;
    rows(true);
    CHECK(fi::written == 0 && fi::found == 0 && said("Parallel Instance Fill sites: left alone") == 1);
    patched[at] ^= 0x04;
    CHECK(game_is_back());  // every other site untouched
    rows(true);
    CHECK(fi::written == 0 && fi::found == 0 && said("Parallel Instance Fill sites: left alone") == 1);  // not asked again, not said again
    CHECK(fill::Prepare(reinterpret_cast<HMODULE>(patched)) && fi::found == fill::ALL_SITES);
    rows(true);
    CHECK(fi::written == fill::ALL_SITES && fi::row_on.load());
    // Going out: the other mod's bytes over our call at site 1: that site stays, the rest come out, found loses it, the module is pinned.
    ec::module_pinned = false;
    patched[fill::SITES[1].rva + 2] ^= 0x33;
    rows(false);
    CHECK(fi::written == 0 && fi::found == (fill::ALL_SITES & ~(1u << 1)) && ec::module_pinned && !fi::row_on.load());
    for (size_t index = 0; index < fill::SITE_COUNT; ++index) CHECK(site_is(index, false) == (index != 1));
    CHECK(patched[fill::SITES[1].rva + 2] == (fi::sites[1].patch[2] ^ 0x33));  // the other mod's bytes are as they were
    // The row back on with that site gone from found: nothing goes in again (no partial set), whatever the row asks.
    rows(true);
    CHECK(fi::written == 0 && fill::Wanted(true) == 0 && !fi::row_on.load());
    std::memcpy(patched + fill::SITES[1].rva, fi::sites[1].game.data(), fill::SITE_SIZE);
    CHECK(game_is_back());
    ec::module_pinned = false;
    CHECK(fill::Prepare(reinterpret_cast<HMODULE>(patched)) && fi::found == fill::ALL_SITES);
    rows(false);
    std::printf("29b. another mod's byte in a site going in: none written, the group stands down, said once; over our call going out: that site stays, the rest out, found loses it, the module pinned\n");
  }

#if defined(TWATTILA_INSTANCE_FILL_CONTROLS)
  // 29c. A page that cannot be made writable (the control: Rewrite's n-th protect fails).
  {
    ec::module_pinned = false;
    CHECK(set_page(PAGE_EXECUTE_READ));
    fill::control_fail_protect = 0;  // going in: the first
    log_lines.clear();
    rows(true);
    CHECK(fill::control_fail_protect == -1 && page_protection() == PAGE_EXECUTE_READ);
    CHECK(set_page(PAGE_EXECUTE_READWRITE));
    CHECK(fi::written == 0 && fi::found == 0 && game_is_back() && !ec::module_pinned && said("could not be made writable") == 1);
    CHECK(fill::Prepare(reinterpret_cast<HMODULE>(patched)) && fi::found == fill::ALL_SITES);
    fill::control_fail_protect = 2;  // going in: the third (the first two were made writable and are put back)
    CHECK(set_page(PAGE_EXECUTE_READ));
    rows(true);
    CHECK(page_protection() == PAGE_EXECUTE_READ);
    CHECK(set_page(PAGE_EXECUTE_READWRITE));
    CHECK(fi::written == 0 && fi::found == 0 && game_is_back());
    fill::control_fail_protect = -1;
    CHECK(fill::Prepare(reinterpret_cast<HMODULE>(patched)) && fi::found == fill::ALL_SITES);
    rows(true);
    CHECK(fi::written == fill::ALL_SITES);
    // Going out: the second protect fails: that site stays (ours still), the others come out, the module is pinned, found loses it.
    fill::control_fail_protect = 1;
    CHECK(set_page(PAGE_EXECUTE_READ));
    rows(false);
    CHECK(page_protection() == PAGE_EXECUTE_READ);
    CHECK(set_page(PAGE_EXECUTE_READWRITE));
    CHECK(fi::written == 0 && fi::found == (fill::ALL_SITES & ~(1u << 1)) && ec::module_pinned);
    for (size_t index = 0; index < fill::SITE_COUNT; ++index) CHECK(site_is(index, index == 1));
    fill::control_fail_protect = -1;
    std::memcpy(patched + fill::SITES[1].rva, fi::sites[1].game.data(), fill::SITE_SIZE);
    CHECK(game_is_back());
    ec::module_pinned = false;
    CHECK(fill::Prepare(reinterpret_cast<HMODULE>(patched)) && fi::found == fill::ALL_SITES);
    // A failure armed past the last protect changes nothing.
    fill::control_fail_protect = 10;
    CHECK(rows_on_rx_page(true) && fi::written == fill::ALL_SITES);
    CHECK(rows_on_rx_page(false) && fi::written == 0 && game_is_back());
    fill::control_fail_protect = -1;
    std::printf("29c. a page that cannot be made writable: going in none is written and the group stands down (the earlier pages put back); going out only that site stays, the module pinned; the page as it was\n");
  }
#endif

  // 29d. A thread at each byte of each site; the dispatcher's count.
  {
    std::atomic<bool> stop = false;
    std::thread parked([&] {
      while (!stop.load()) Sleep(1);
    });
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, GetThreadId(parked.native_handle()));
    CHECK(thread != nullptr);
    int wrong = 0;
    int checked = 0;
    // Update (the row `on`) with the thread's Eip at `where`: true when the sites changed.
    const auto try_row = [&](const uint8_t* where, bool on) {
      wrong += (SuspendThread(thread) == static_cast<DWORD>(-1)) ? 1 : 0;
      CONTEXT saved = {.ContextFlags = CONTEXT_CONTROL};
      wrong += (GetThreadContext(thread, &saved) == FALSE) ? 1 : 0;
      CONTEXT moved = saved;
      moved.Eip = Address(where);
      wrong += (SetThreadContext(thread, &moved) == FALSE) ? 1 : 0;
      ec::failed_writes = 0;
      rows(on);
      const bool changed = (fi::written == (on ? fill::ALL_SITES : 0u));
      wrong += (SetThreadContext(thread, &saved) == FALSE) ? 1 : 0;
      ResumeThread(thread);
      ++checked;
      return changed;
    };
    for (size_t index = 0; index < fill::SITE_COUNT; ++index) {
      const uint8_t* const start = fi::sites[index].at;
      for (size_t offset = 0; offset <= fill::SITE_SIZE; ++offset) {
        const bool inside = (offset > 0 && offset < fill::SITE_SIZE);
        // Going in (the sites out): refused strictly inside.
        wrong += (try_row(start + offset, true) == inside) ? 1 : 0;
        ec::failed_writes = 0;
        rows(true);
        wrong += (fi::written != fill::ALL_SITES) ? 1 : 0;
        // Going out: the same.
        wrong += (try_row(start + offset, false) == inside) ? 1 : 0;
        ec::failed_writes = 0;
        rows(false);
        wrong += (fi::written != 0 || !game_is_back()) ? 1 : 0;
      }
    }
    // A thread elsewhere in the game's code: free.
    wrong += (!try_row(patched + text_rva + 0x100, true)) ? 1 : 0;
    ec::failed_writes = 0;
    wrong += (!try_row(patched + text_rva + 0x100, false)) ? 1 : 0;
    stop = true;
    parked.join();
    CloseHandle(thread);
    ec::failed_writes = 0;
    CHECK(wrong == 0 && fi::written == 0 && game_is_back());
    std::printf("29d. a thread parked at %d places (each byte of each site and the byte after, going in and going out): refused strictly inside, free elsewhere\n", checked);

    // The dispatcher's count above 0 (a thread inside Dispatch): the sites stay in, the flag follows the row, they come out once it is 0; going in is not held by it.
    rows(true);
    CHECK(fi::written == fill::ALL_SITES && fi::row_on.load());
    fi::active.store(1);
    ec::failed_writes = 0;
    rows(false);
    CHECK(fi::written == fill::ALL_SITES && !fi::row_on.load() && ec::failed_writes == 1);  // refused; the dispatcher runs the game's call from now on
    fi::active.store(0);
    ec::failed_writes = 0;
    rows(false);
    CHECK(fi::written == 0 && game_is_back() && !ec::module_pinned);
    fi::active.store(1);
    rows(true);
    CHECK(fi::written == fill::ALL_SITES);  // the count holds only the way out
    fi::active.store(0);
    // The same count as a stuck word: Uninstall cannot take the sites out; the module is pinned and the left-in line said.
    log_lines.clear();
    fi::active.store(1);
    ec::module_pinned = false;
    twattila::engine_code::Uninstall();
    CHECK(fi::written == fill::ALL_SITES && ec::module_pinned && !fi::row_on.load() && said("Parallel Instance Fill: left in at detach") == 1);
    fi::active.store(0);
    ec::module_pinned = false;
    ec::failed_writes = 0;
    rows(false);
    CHECK(fi::written == 0 && game_is_back() && !ec::module_pinned);
    std::printf("29d. the dispatcher's count above 0: the sites stay in, the flag follows the row, they come out once it is 0, going in is not held; a stuck count at Uninstall pins the module\n");
  }

  // 29e. Both tables in one window (the effect update's and this one's), and Uninstall.
  {
    CHECK(eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::found == eu::ALL_SITES);
    CHECK(rows_on_rx_page(true, true));
    CHECK(fi::written == fill::ALL_SITES && ei::written == eu::ALL_SITES);
    for (size_t index = 0; index < fill::SITE_COUNT; ++index) CHECK(site_is(index, true));
    CHECK(rows_on_rx_page(false, true));  // the fill alone comes out
    CHECK(fi::written == 0 && ei::written == eu::ALL_SITES);
    CHECK(rows_on_rx_page(true, true));
    CHECK(rows_on_rx_page(false, false));
    CHECK(fi::written == 0 && ei::written == 0 && game_is_back());
    rows(true);
    twattila::engine_code::Uninstall();
    CHECK(fi::written == 0 && !fi::row_on.load() && game_is_back() && !ec::module_pinned);
    std::printf("29e. both tables in one window, in and out, the page as it was; Uninstall takes the sites out, the module not pinned\n");

    // Another mod's byte over one of our calls at detach: that site stays (not ours to take out), the others come out, and the module is pinned
    // (a game thread may still be on its way through our function).
    CHECK(fill::Prepare(reinterpret_cast<HMODULE>(patched)) && fi::found == fill::ALL_SITES);
    rows(true);
    CHECK(fi::written == fill::ALL_SITES);
    patched[fill::SITES[1].rva + 2] ^= 0x33;
    ec::module_pinned = false;
    twattila::engine_code::Uninstall();
    CHECK(ec::module_pinned && fi::written == 0 && site_is(0, false) && !site_is(1, false) && !site_is(1, true) && site_is(2, false) && site_is(3, false));
    std::memcpy(patched + fill::SITES[1].rva, fi::sites[1].game.data(), fill::SITE_SIZE);
    ec::module_pinned = false;
    CHECK(game_is_back());
    std::printf("29e. another mod's byte over our call at detach: that site stays, the others come out, the module is pinned\n");
  }
  CHECK(fill::Prepare(reinterpret_cast<HMODULE>(patched)) && fi::found == fill::ALL_SITES);
  CHECK(ec::failed_writes == 0);
  (void)game;
  return 0;
#endif
}
