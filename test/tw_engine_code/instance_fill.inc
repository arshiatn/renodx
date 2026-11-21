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
//    pinned.
// ====================================================================================================================

int RunInstanceFillTests(const TestBuild& build, uint8_t* game, uint8_t* patched, uint32_t text_rva, uint32_t text_size) {
  namespace ec = twattila::engine_code::internal;
  namespace fill = twattila::instance_fill;
  namespace fi = twattila::instance_fill::internal;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  if (build.code != &ec::ATTILA_BUILD) {
    std::printf("29. parallel instance fill: Attila only (skipped)\n");
    return 0;
  }
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
    // A thread elsewhere in the game's code, and in this module's own dispatcher (no thread of the fill's own is running here: the bytes of a site's dispatcher are not a cut): free.
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
  }
  CHECK(fill::Prepare(reinterpret_cast<HMODULE>(patched)) && fi::found == fill::ALL_SITES);
  CHECK(ec::failed_writes == 0);
  (void)game;
  return 0;
}
