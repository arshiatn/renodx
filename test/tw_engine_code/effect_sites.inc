// ====================================================================================================================
// 25. Parallel Effect Update's sites (src/games/twattila/effect_update_sites.hpp; the scheduler that uses them comes later), included inside tests.cpp's
// anonymous namespace after pending_sweep_split.inc (Address, Read32, Write32, Random and CHECK of the earlier parts are used). Attila only.
// 25a. The table: every site's bytes in the game's code (A) and in ours (B), the lengths, no two sites touching, none in the heap fix's presort, every E8's target and
//    head; the whole code section of the DLL scanned for a branch into the middle of a site, and every relocated pointer of the image (jump tables, vtables) for
//    one (the scan finds a planted branch: CONTROL); another mod's bytes in a site, a hooked entry: nothing is found, named once; the E9 list;
// 25b. Update with the row on: B differs from A only in the 27 sites, each holding its E9 / E8 and NOPs, the words (gates, limit, handler) as Prepare left them; off: the
//    game's bytes back; whole or not at all (another mod's bytes in one site on the way in: none written; on the way out: that site is left alone and the module pinned);
// 25c. The thunks as built (the same emitter as Prepare's, on this test's own page with the game's callee and resume replaced by landing pads that log the state), every site:
//    the original path (gates closed, or the direct thread's stack) against the game's own instructions run alone (every register, esp, xmm0-7, MXCSR, the stack above the
//    site's esp equal); the slow path with a handler that clobbers xmm0-7, x87 and MXCSR, ecx and edx, answering 0 (the same equalities), the Frame it was given, the count
//    it saw (1) and after (0); answering 1 at the sites that may (the function's return with its `ret N`, eax as written); the E8 sites (the walk's call, the two appends):
//    the callee called from the page only with the count raised, a copy of the argument, ecx and the result kept for the report, `ret 4`; the walk end's push and jump;
//    CONTROLs: no fxsave / fxrstor (the clobber must show), the stack range as two words (a torn pair must take the fast path wrongly);
// 25d. The fast path's one word: the thread on the direct stack, on another, nobody direct, a limit below the stack's size, a pair of words that two handovers tore;
// 25e. The stack the slow path uses, on a pattern-filled stack;
// 25f. The sites in the game's code (B): the gates closed, the real random triple and the spawn functions through them equal A's, with the gates open and nobody direct
//    (the slow path through the real thunk page, a handler that counts); 200 switches under 4 threads running them; a parked thread at each byte of each site (refused strictly
//    inside it), at the thunk page's first and last byte and random places (refused), the count above 0 (a thread inside a callee called from the page: refused), then written.
// ====================================================================================================================

namespace eu = twattila::effect_update;
namespace ei = twattila::effect_update::internal;

// What every landing pad and the handler log, one run at a time (the harness has no threads in 25c to 25e).
struct alignas(16) SiteRun {
  uint32_t gpr_in[7] = {};  // eax ecx edx ebx esi edi ebp
  uint32_t esp_in = 0;
  uint32_t target = 0;
  uint32_t harness_esp = 0;
  uint32_t gpr_out[7] = {};
  uint32_t esp_out = 0;
  uint32_t tag = 0;  // the pad reached: 1 the game's code after the site, 3 the function's caller
  uint32_t calls = 0;
  uint32_t seen_frame = 0;
  uint32_t seen_site = 0;
  uint32_t seen_active = 0;
  uint32_t seen_words[12] = {};
  uint32_t action = 0;     // what the handler answers
  uint32_t eax_value = 0;  // not 0: the handler writes it into Frame.eax
  uint32_t clobber_mxcsr = 0x7F80;
  uint32_t callee_ran = 0;
  uint32_t callee_active = 0;
  uint32_t callee_esp = 0;
  uint32_t callee_return = 0;
  uint32_t callee_arg = 0;
  uint32_t callee_ecx = 0;
  alignas(16) uint8_t fx_in[512] = {};
  alignas(16) uint8_t fx_out[512] = {};
  alignas(16) uint8_t fx_harness[512] = {};
};
static SiteRun site_run;

constexpr uint32_t CALLEE_RESULT = 0xCA11E4E5;
constexpr uint32_t CALLEE_ECX = 0xCA11EEC1;
constexpr uint32_t ANSWER_EAX = 0x600DF00D;

// The harness's code: the runner, the pads, the callee stubs, the two handlers and one thunk per site (the production emitter).
struct SiteHarness {
  uint8_t* code = nullptr;
  uint32_t runner = 0;
  uint32_t resume_pad = 0;
  uint32_t returned_pad = 0;
  std::array<uint32_t, 3> callees = {};  // ret 0, ret 4, ret 0xC
  uint32_t handler_plain = 0;
  uint32_t handler_clobber = 0;
  std::array<const uint8_t*, eu::SITE_COUNT> thunks = {};
  size_t thunk_bytes = 0;
  eu::Data* data = nullptr;
  bool fits = false;
};

SiteHarness BuildSiteHarness(const uint8_t* patched, const uint8_t* callee_game) {
  SiteHarness harness;
  harness.code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  harness.data = new eu::Data;
  ei::Assembler assembler(std::span<uint8_t>(harness.code, 0x10000));
  SiteRun& run = site_run;
  const auto at = [&](const void* field) { return Address(field); };
  // The runner: the harness's registers and vector state kept, the test's loaded, a jump to the thunk.
  harness.runner = Address(assembler.Cursor());
  assembler.Bytes({0x60});                                                  // pushad
  assembler.BytesAndOperand({0x89, 0x25}, at(&run.harness_esp));            // mov [harness_esp], esp
  assembler.BytesAndOperand({0x0F, 0xAE, 0x05}, at(run.fx_harness));        // fxsave [fx_harness]
  assembler.BytesAndOperand({0x0F, 0xAE, 0x0D}, at(run.fx_in));             // fxrstor [fx_in]
  assembler.BytesAndOperand({0x8B, 0x25}, at(&run.esp_in));                 // mov esp, [esp_in]
  assembler.BytesAndOperand({0xA1}, at(&run.gpr_in[0]));                    // mov eax, [..]
  const std::array<uint8_t, 6> load_opcodes = {0x0D, 0x15, 0x1D, 0x35, 0x3D, 0x2D};  // ecx edx ebx esi edi ebp (8B /r with a 32-bit address)
  for (size_t reg = 0; reg < load_opcodes.size(); ++reg) assembler.BytesAndOperand({0x8B, load_opcodes[reg]}, at(&run.gpr_in[1 + reg]));
  assembler.BytesAndOperand({0xFF, 0x25}, at(&run.target));                 // jmp [target]
  // A pad: every register and the vector state stored, the tag, then back to the harness.
  const auto pad = [&](uint32_t tag) {
    const uint32_t entry = Address(assembler.Cursor());
    assembler.BytesAndOperand({0xA3}, at(&run.gpr_out[0]));
    const std::array<uint8_t, 6> store_opcodes = {0x0D, 0x15, 0x1D, 0x35, 0x3D, 0x2D};
    for (size_t reg = 0; reg < store_opcodes.size(); ++reg) assembler.BytesAndOperand({0x89, store_opcodes[reg]}, at(&run.gpr_out[1 + reg]));
    assembler.BytesAndOperand({0x89, 0x25}, at(&run.esp_out));
    assembler.BytesAndOperand({0x0F, 0xAE, 0x05}, at(run.fx_out));          // fxsave [fx_out]
    assembler.BytesAndOperand({0xC7, 0x05}, at(&run.tag));                  // mov dword ptr [tag], imm32
    assembler.Bytes({static_cast<uint8_t>(tag), 0x00, 0x00, 0x00});
    assembler.BytesAndOperand({0x8B, 0x25}, at(&run.harness_esp));          // mov esp, [harness_esp]
    assembler.BytesAndOperand({0x0F, 0xAE, 0x0D}, at(run.fx_harness));      // fxrstor [fx_harness]
    assembler.Bytes({0x61, 0xC3});                                          // popad; ret
    return entry;
  };
  harness.resume_pad = pad(1);
  harness.returned_pad = pad(3);
  // The game's callee as the E8 sites and the walk end see it: logs the count, esp, the return address and its argument, changes ecx, answers, pops `bytes`.
  const std::array<uint16_t, 3> pops = {0, 4, 0xC};
  for (size_t kind = 0; kind < pops.size(); ++kind) {
    harness.callees[kind] = Address(assembler.Cursor());
    assembler.BytesAndOperand({0xA1}, Address(&harness.data->active));
    assembler.BytesAndOperand({0xA3}, at(&run.callee_active));
    assembler.BytesAndOperand({0x89, 0x25}, at(&run.callee_esp));
    assembler.Bytes({0x8B, 0x04, 0x24});                                    // mov eax, [esp]
    assembler.BytesAndOperand({0xA3}, at(&run.callee_return));
    assembler.Bytes({0x8B, 0x44, 0x24, 0x04});                              // mov eax, [esp + 4]
    assembler.BytesAndOperand({0xA3}, at(&run.callee_arg));
    assembler.BytesAndOperand({0x89, 0x0D}, at(&run.callee_ecx));
    assembler.BytesAndOperand({0xF0, 0xFF, 0x05}, at(&run.callee_ran));
    assembler.BytesAndOperand({0xB9}, CALLEE_ECX);
    assembler.BytesAndOperand({0xB8}, CALLEE_RESULT);
    if (pops[kind] == 0) {
      assembler.Bytes({0xC3});
    } else {
      assembler.Bytes({0xC2, static_cast<uint8_t>(pops[kind]), 0x00});
    }
  }
  // The handlers (cdecl: the Frame, the site): log, count, maybe write eax, answer; the second also clobbers xmm0-7, the x87 state, MXCSR, ecx and edx.
  const auto handler = [&](bool clobber) {
    const uint32_t entry = Address(assembler.Cursor());
    assembler.Bytes({0x8B, 0x44, 0x24, 0x04});                              // mov eax, [esp + 4]   the Frame
    assembler.BytesAndOperand({0xA3}, at(&run.seen_frame));
    assembler.Bytes({0x8B, 0x4C, 0x24, 0x08});                              // mov ecx, [esp + 8]   the site
    assembler.BytesAndOperand({0x89, 0x0D}, at(&run.seen_site));
    assembler.BytesAndOperand({0x8B, 0x0D}, Address(&harness.data->active));
    assembler.BytesAndOperand({0x89, 0x0D}, at(&run.seen_active));
    for (uint8_t word = 0; word < 12; ++word) {
      assembler.Bytes({0x8B, 0x48, static_cast<uint8_t>(4 * word)});        // mov ecx, [eax + 4 * word]
      assembler.BytesAndOperand({0x89, 0x0D}, at(&run.seen_words[word]));
    }
    assembler.BytesAndOperand({0xF0, 0xFF, 0x05}, at(&run.calls));
    assembler.BytesAndOperand({0x8B, 0x0D}, at(&run.eax_value));
    assembler.Bytes({0x85, 0xC9});                                          // test ecx, ecx
    uint8_t* skip = assembler.Branch(0x84);
    assembler.Bytes({0x89, 0x48, 0x1C});                                    // mov [eax + 28], ecx   Frame.eax
    assembler.Bind(skip);
    if (clobber) {
      assembler.Bytes({0x0F, 0x57, 0xC0, 0x0F, 0x57, 0xC9, 0x0F, 0x57, 0xD2, 0x0F, 0x57, 0xDB, 0x0F, 0x57, 0xE4, 0x0F, 0x57, 0xED, 0x0F, 0x57, 0xF6, 0x0F, 0x57, 0xFF});
      assembler.BytesAndOperand({0x0F, 0xAE, 0x15}, at(&run.clobber_mxcsr));  // ldmxcsr
      assembler.Bytes({0xDB, 0xE3, 0xD9, 0xEB, 0xD9, 0xE8});                // fninit; fldpi; fld1
      assembler.BytesAndOperand({0xB9}, 0xC10BBE12);
      assembler.BytesAndOperand({0xBA}, 0xC10BBE12);
    }
    assembler.BytesAndOperand({0xA1}, at(&run.action));
    assembler.Bytes({0xC3});
    return entry;
  };
  harness.handler_plain = handler(false);
  harness.handler_clobber = handler(true);
  harness.data->handler.store(harness.handler_clobber);
  // The thunks, built as Prepare builds them (the game's bytes as B has them while the sites are out).
  const uint8_t* const start = assembler.Cursor();
  for (size_t index = 0; index < eu::SITE_COUNT; ++index) {
    const eu::SiteInfo& info = eu::SITES[index];
    const uint8_t* callee = nullptr;
    switch (info.kind) {
      case eu::Kind::WALK_CALL: callee = reinterpret_cast<const uint8_t*>(harness.callees[2]); break;
      case eu::Kind::APPEND_CALL: callee = reinterpret_cast<const uint8_t*>(harness.callees[1]); break;
      case eu::Kind::WALK_END: callee = reinterpret_cast<const uint8_t*>(harness.callees[0]); break;
      default: break;
    }
    harness.thunks[index] = ei::EmitThunk(&assembler, index, harness.data, patched + info.rva, callee, reinterpret_cast<const uint8_t*>(harness.resume_pad));
  }
  harness.thunk_bytes = static_cast<size_t>(assembler.Cursor() - start);
  harness.fits = assembler.Fits();
  FlushInstructionCache(GetCurrentProcess(), harness.code, 0x10000);
  (void)callee_game;
  return harness;
}

// The game's own instructions at a site, alone, as a reference: they, then a jump to the resume pad.
const uint8_t* SiteReference(const SiteHarness& harness, const uint8_t* patched, size_t index) {
  static uint8_t* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  static size_t used = 0;
  uint8_t* at = memory + used;
  const size_t size = eu::SizeOf(eu::SITES[index]);
  std::memcpy(at, patched + eu::SITES[index].rva, size);
  at[size] = 0xE9;
  Write32(at + size + 1, harness.resume_pad - Address(at + size + 5));
  used += 0x20;
  return at;
}

// One stack: its words (the return address first, then pointers into `scratch`, so that whatever the site dereferences is readable).
struct SiteStack {
  uint8_t* low = nullptr;
  size_t size = 0;
};

void FillSiteRun(const SiteHarness& harness, const SiteStack& stack, size_t depth, const uint8_t* scratch, Random* random, uint32_t target, bool x87_value) {
  SiteRun& run = site_run;
  for (uint32_t& value : run.gpr_in) value = Address(scratch + 4 * (random->Next() % 0x3000));
  uint8_t* esp = stack.low + stack.size - depth;
  for (size_t word = 0; word < 64; ++word) Write32(esp + 4 * word, Address(scratch + 4 * (random->Next() % 0x3000)));
  Write32(esp, harness.returned_pad);
  run.esp_in = Address(esp);
  run.target = target;
  for (uint8_t& byte : run.fx_in) byte = static_cast<uint8_t>(random->Next());
  // A clean x87 state (empty tags, the default control word), random vector registers, MXCSR with every exception masked and a random rounding mode.
  std::memset(run.fx_in, 0, 32);
  run.fx_in[0] = 0x7F;
  run.fx_in[1] = 0x03;
  Write32(run.fx_in + 24, 0x1F80 | ((random->Next() & 3) << 13) | ((random->Next() & 1) << 6) | ((random->Next() & 1) << 15));
  Write32(run.fx_in + 28, 0xFFFF);
  if (x87_value) {
    run.fx_in[4] = 0x01;  // abbreviated tag: register 0 valid (it is ST0 when TOP is 0)
    std::memcpy(run.fx_in + 32, "\x11\x22\x33\x44\x55\x66\x77\x88\xFF\x3F", 10);
  }
  run.calls = 0;
  run.callee_ran = 0;
  run.tag = 0;
  run.seen_frame = 0;
  run.seen_site = 0xFFFF;
  run.seen_active = 0xFFFF;
  std::memset(run.seen_words, 0, sizeof(run.seen_words));
  std::memset(run.gpr_out, 0, sizeof(run.gpr_out));
  std::memset(run.fx_out, 0, sizeof(run.fx_out));
}

// The vector state the sites must keep: FCW, MXCSR, xmm0-7 (and ST0 when the run had one).
bool SiteVectorEqual(const uint8_t* a, const uint8_t* b, bool x87_value) {
  return std::memcmp(a, b, 4) == 0 && std::memcmp(a + 24, b + 24, 4) == 0 && std::memcmp(a + 160, b + 160, 128) == 0
         && (!x87_value || std::memcmp(a + 32, b + 32, 10) == 0);
}

// The handler the live sites call in 25f: counts per site, may block (a thread inside the handler), may answer 1 with eax written.
std::array<std::atomic<uint32_t>, eu::SITE_COUNT> live_calls;
std::atomic<uint32_t> live_action = 0;
std::atomic<uint32_t> live_eax = 0;
std::atomic<bool> live_block = false;
std::atomic<bool> live_blocked = false;

uint32_t __cdecl LiveHandler(eu::Frame* frame, uint32_t site) {
  ++live_calls[site];
  if (live_block.load()) {
    live_blocked = true;
    while (live_block.load()) Sleep(1);
  }
  if (live_eax.load() != 0) frame->eax = live_eax.load();
  return live_action.load();
}

int RunEffectSitesTests(const TestBuild& build, uint8_t* game, uint8_t* patched, uint32_t text_rva, uint32_t text_size, bool quick) {
  namespace ec = twattila::engine_code::internal;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  (void)quick;
  if (build.code != &ec::ATTILA_BUILD) {
    std::printf("25. effect update sites: Attila only (skipped)\n");
    return 0;
  }
  const auto rows = [&](bool on) {
    twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 1.f, 0, {.effect_update = on});
  };
  rows(false);
  CHECK(ei::written == 0);
  const std::vector<uint8_t> unpatched(patched + text_rva, patched + text_rva + text_size);
  const auto code_at = [&](const std::vector<uint8_t>& bytes, uint32_t rva) { return bytes.data() + (rva - text_rva); };

  // 25a. The table.
  size_t longest = 0;
  for (size_t index = 0; index < eu::SITE_COUNT; ++index) {
    const eu::SiteInfo& info = eu::SITES[index];
    const size_t size = eu::SizeOf(info);
    longest = std::max(longest, size);
    CHECK(size >= 5 && size <= eu::MAX_SITE_SIZE && info.rva >= text_rva && info.rva + size <= text_rva + text_size);
    CHECK(Matches(game + info.rva, size, info.pattern) == 1);  // the pattern holds at its place in A
    for (size_t other = index + 1; other < eu::SITE_COUNT; ++other) {
      const eu::SiteInfo& next = eu::SITES[other];
      CHECK(info.rva + size <= next.rva || next.rva + eu::SizeOf(next) <= info.rva);
    }
    CHECK(info.rva + size <= eu::PRESORT_BEGIN || info.rva >= eu::PRESORT_END);
    if (info.callee_rva != 0) {
      const uint32_t call_at = info.rva + (info.kind == eu::Kind::WALK_END ? 4 : 0);
      CHECK(game[call_at] == 0xE8 && Address(game + call_at + 5) + Read32(game + call_at + 1) == Address(game + info.callee_rva));
      if (!info.callee_head.empty()) CHECK(Matches(game + info.callee_rva, (info.callee_head.size() + 1) / 3, info.callee_head) == 1);
    }
    CHECK((info.kind == eu::Kind::ORDER || info.kind == eu::Kind::WALK_END) == (info.callee_rva == 0 || info.kind == eu::Kind::WALK_END));
  }
  for (const uint32_t rva : eu::HOOKED_ENTRIES) CHECK(game[rva] != 0xE9 && patched[rva] != 0xE9);
  // The whole code section: no branch into the middle of a site; a planted branch is found (CONTROL).
  std::vector<uint32_t> hits;
  const size_t branch_hits = ei::BranchesIntoSites({game + text_rva, text_size}, text_rva, &hits, ei::RelocatedWords(game));
  for (const uint32_t source : hits) std::printf("   a branch at +%X lands inside a site\n", source);
  CHECK(branch_hits == 0);
  {
    // CONTROL: a jmp rel8, a jmp rel32 and a jcc rel32 into the second, third and fourth byte of a site are found (three hits); a jcc to its first byte is not.
    const uint32_t site_rva = eu::SITES[static_cast<size_t>(eu::Site::SPAWN_QUERY)].rva;
    const uint32_t first = site_rva - 9;
    std::vector<uint8_t> window(0x40, 0x90);
    window[0] = 0xEB;
    window[1] = 8;
    window[0x10] = 0xE9;
    Write32(window.data() + 0x11, (site_rva + 2) - (first + 0x15));
    window[0x20] = 0x0F;
    window[0x21] = 0x84;
    Write32(window.data() + 0x22, (site_rva + 3) - (first + 0x26));
    window[0x30] = 0x0F;
    window[0x31] = 0x84;
    Write32(window.data() + 0x32, site_rva - (first + 0x36));
    std::vector<uint32_t> planted_hits;
    CHECK(ei::BranchesIntoSites(window, first, &planted_hits) == 3 && planted_hits[0] == first && planted_hits[1] == first + 0x10 && planted_hits[2] == first + 0x20);
    // CONTROL: the two whose bytes lie in a relocated word (the loader rewrites it: data) are skipped.
    const std::array<uint32_t, 2> relocated = {first - 2, first + 0x11};
    planted_hits.clear();
    CHECK(ei::BranchesIntoSites(window, first, &planted_hits, relocated) == 1 && planted_hits[0] == first + 0x20);
    // CONTROL: the rel32 forms only (Prepare's scan): the short jump is not looked at, the other two are found.
    planted_hits.clear();
    CHECK(ei::BranchesIntoSites(window, first, &planted_hits, {}, false) == 2 && planted_hits[0] == first + 0x10 && planted_hits[1] == first + 0x20);
  }
  {
    // Every relocated pointer of the image: none into a site (a jump table or a vtable would be one).
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(game + reinterpret_cast<const IMAGE_DOS_HEADER*>(game)->e_lfanew);
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    size_t pointers = 0;
    size_t into_sites = 0;
    for (uint32_t at = 0; at < directory.Size;) {
      const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(game + directory.VirtualAddress + at);
      if (block->SizeOfBlock == 0) break;
      const auto* entries = reinterpret_cast<const uint16_t*>(block + 1);
      for (size_t entry = 0; entry < (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2; ++entry) {
        if ((entries[entry] >> 12) != IMAGE_REL_BASED_HIGHLOW) continue;
        ++pointers;
        const uint32_t value = Read32(game + block->VirtualAddress + (entries[entry] & 0xFFF)) - Address(game);
        for (const eu::SiteInfo& info : eu::SITES) into_sites += (value > info.rva && value < info.rva + eu::SizeOf(info)) ? 1 : 0;
      }
      at += block->SizeOfBlock;
    }
    CHECK(pointers > 100000 && into_sites == 0);
    std::printf("25a. 27 sites (up to %zu bytes), their patterns once each in A, calls to their callees, none in the presort; no branch of the code section and none of %zu relocated pointers lands inside one\n",
                longest, pointers);
  }

  CHECK(eu::Prepare(reinterpret_cast<HMODULE>(patched)));
  CHECK(ei::found == eu::ALL_SITES && ei::written == 0 && ei::data != nullptr && ei::page != nullptr);
  for (size_t index = 0; index < eu::SITE_COUNT; ++index) {
    const eu::SiteInfo& info = eu::SITES[index];
    const ei::PatchedSite& site = ei::sites[index];
    CHECK(site.at == patched + info.rva && site.size == eu::SizeOf(info));
    CHECK(std::memcmp(site.game.data(), code_at(unpatched, info.rva), site.size) == 0);
    const bool call = (info.kind == eu::Kind::WALK_CALL || info.kind == eu::Kind::APPEND_CALL);
    CHECK(site.patch[0] == (call ? 0xE8 : 0xE9));
    CHECK(Address(site.at + 5) + Read32(site.patch.data() + 1) == Address(ei::thunks[index]));
    for (size_t byte = 5; byte < site.size; ++byte) CHECK(site.patch[byte] == 0x90);
    CHECK(ei::thunks[index] >= ei::page && ei::thunks[index] < ei::page_end);
  }
  MEMORY_BASIC_INFORMATION code_info = {};
  MEMORY_BASIC_INFORMATION data_info = {};
  CHECK(VirtualQuery(ei::page, &code_info, sizeof(code_info)) == sizeof(code_info) && VirtualQuery(ei::page + ei::CODE_SIZE, &data_info, sizeof(data_info)) == sizeof(data_info));
  CHECK(code_info.Protect == PAGE_EXECUTE_READ && code_info.RegionSize == ei::CODE_SIZE && data_info.Protect == PAGE_READWRITE && reinterpret_cast<uint8_t*>(ei::data) == ei::page + ei::CODE_SIZE);
  CHECK(ei::data->handler.load() == Address(reinterpret_cast<const void*>(&eu::PassThrough)) && ei::data->walk_armed.load() == 0 && ei::data->walk_active.load() == 0
        && ei::data->direct_high.load() == 0 && ei::data->limit.load() == 0 && ei::data->active.load() == 0);
  // No sentinel, no unfilled displacement left in the page: every jump and call of a thunk lands in the page, the game's image or the data page's handler.
  {
    size_t used = 0;
    for (size_t at = 0; at < ei::CODE_SIZE; ++at) {
      if (ei::page[at] != 0) used = at + 1;
    }
    std::printf("25a. thunks: %zu of %zu bytes\n", used, ei::CODE_SIZE);
    CHECK(used < ei::CODE_SIZE - 16);
  }

  // Another mod's bytes in a site, a hooked entry: nothing found.
  {
    const auto expect_refused = [&](uint8_t* at, uint8_t value, const char* what) {
      const uint8_t saved = *at;
      *at = value;
      CHECK(!eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::found == 0 && eu::Wanted(true) == 0);
      *at = saved;
      std::printf("25a. %s: refused\n", what);
      return 0;
    };
    if (expect_refused(patched + eu::SITES[static_cast<size_t>(eu::Site::SPAWN_WRITE)].rva + 1, 0x90, "another mod's byte in a site") != 0) return 1;
    if (expect_refused(patched + eu::HOOKED_ENTRIES[0], 0xE9, "an entry hooked by another tool") != 0) return 1;
    if (expect_refused(patched + eu::SITES[static_cast<size_t>(eu::Site::CHILD_APPEND_A)].rva + 1, 0x00, "an append call's target changed") != 0) return 1;
    if (expect_refused(patched + eu::SWEEP_CHECKS[0].rva + 3, 0x90, "another mod's byte in the sweep") != 0) return 1;
    if (expect_refused(patched + eu::SWEEP_CHECKS[1].rva + 8, 0x08, "the sweep's epilogue popping another count") != 0) return 1;
    CHECK(eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::found == eu::ALL_SITES);
    // The entry hooked after Prepare (at the turn-on): the feature stands down for the session.
    patched[eu::HOOKED_ENTRIES[1]] = 0xE9;
    CHECK(eu::Wanted(true) == 0 && ei::found == 0 && ei::refused);
    patched[eu::HOOKED_ENTRIES[1]] = unpatched[eu::HOOKED_ENTRIES[1] - text_rva];
    CHECK(eu::Prepare(reinterpret_cast<HMODULE>(patched)) && !ei::refused && eu::Wanted(true) == eu::ALL_SITES);
  }

  // 25b. Update with the row on and off.
  rows(true);
  CHECK(ei::written == eu::ALL_SITES && ei::data->walk_armed.load() == 0 && ei::data->walk_active.load() == 0);
  {
    size_t differing = 0;
    for (uint32_t at = text_rva; at < text_rva + text_size; ++at) {
      if (unpatched[at - text_rva] == patched[at]) continue;
      ++differing;
      bool in_site = false;
      for (const ei::PatchedSite& site : ei::sites) in_site |= (patched + at >= site.at && patched + at < site.at + site.size);
      CHECK(in_site);
    }
    for (const ei::PatchedSite& site : ei::sites) CHECK(std::memcmp(site.at, site.patch.data(), site.size) == 0);
    std::printf("25b. row on: %zu bytes differ from the game's, all in the 27 sites, each its jump and NOPs\n", differing);
  }
  rows(false);
  CHECK(ei::written == 0 && std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
  // Whole or not at all: another mod's byte in one site on the way in: none written, and the feature is off for the session (found 0).
  {
    const uint32_t at = eu::SITES[static_cast<size_t>(eu::Site::RIGID_ADD)].rva + 2;
    patched[at] ^= 0x55;
    rows(true);
    CHECK(ei::written == 0 && ei::found == 0);
    patched[at] ^= 0x55;
    CHECK(std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);  // every other site untouched too
    CHECK(eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::found == eu::ALL_SITES);
    std::printf("25b. another mod's byte in one site on the way in: none written\n");
  }
  // On the way out: one site's bytes changed (another mod hooked it): that site is left alone, the rest come out, the module is pinned.
  {
    ec::module_pinned = false;
    rows(true);
    CHECK(ei::written == eu::ALL_SITES);
    const ei::PatchedSite& victim = ei::sites[static_cast<size_t>(eu::Site::SOUND_PLAY)];
    victim.at[3] ^= 0x33;
    rows(false);
    CHECK(ei::written == 0 && (ei::found >> static_cast<size_t>(eu::Site::SOUND_PLAY) & 1) == 0 && ec::module_pinned);
    for (size_t index = 0; index < eu::SITE_COUNT; ++index) {
      const ei::PatchedSite& site = ei::sites[index];
      if (&site != &victim) CHECK(std::memcmp(site.at, site.game.data(), site.size) == 0);
    }
    CHECK(victim.at[3] == (victim.patch[3] ^ 0x33));  // the other mod's bytes are left as they were
    // The row back on with that site gone from `found`: nothing goes in again (no partial set), whatever the row asks.
    rows(true);
    CHECK(ei::written == 0 && eu::Wanted(true) == 0);
    std::memcpy(victim.at, victim.game.data(), victim.size);
    CHECK(std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
    std::printf("25b. another mod's bytes in a site on the way out: left alone, the rest out, the module pinned, and no partial set goes in again\n");
  }
  CHECK(eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::found == eu::ALL_SITES);

  // 25c. The thunks (the production emitter on a test page, the game's callee and resume replaced by landing pads).
  const auto no_ei = [&](SiteHarness* harness) {
    harness->data->walk_armed.store(0);
    harness->data->walk_active.store(0);
    harness->data->direct_high.store(0);
    harness->data->limit.store(0);
  };
  Random random = {.state = 0xE1F00DD1CEull};
  uint8_t* scratch = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  CHECK(scratch != nullptr);
  for (size_t at = 0; at < 0x10000; at += 4) Write32(scratch + at, Address(scratch + ((at * 7 + 12) & 0xFFFC)));  // every word points back into the page
  SiteStack stack = {.size = 0x80000};
  stack.low = static_cast<uint8_t*>(VirtualAlloc(nullptr, 3 * stack.size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  CHECK(stack.low != nullptr);
  const SiteStack stack_a = {stack.low, stack.size};                          // the thread under test
  const SiteStack stack_b = {stack.low + stack.size, stack.size};             // the direct thread's, above
  const SiteStack stack_c = {stack.low + 2 * stack.size, stack.size};         // a third
  SiteHarness harness = BuildSiteHarness(patched, nullptr);
  CHECK(harness.code != nullptr && harness.fits);
  std::printf("25c. the test page's thunks: %zu bytes for 27 sites\n", harness.thunk_bytes);
  const auto run = [&](const SiteHarness& h, const uint32_t target) {
    reinterpret_cast<void(__cdecl*)()>(h.runner)();
    (void)target;
  };
  // A run of site `index` from the state in site_run: the original path (the game's instructions alone at `reference`) is the expectation.
  const auto original_run = [&](size_t index, const SiteHarness& h, bool x87_value, uint32_t seed) {
    Random inputs = {.state = seed};
    FillSiteRun(h, stack_a, 0x300, scratch, &inputs, h.thunks[index] != nullptr ? Address(h.thunks[index]) : 0, x87_value);
    SiteRun saved = site_run;
    // The reference: the same inputs, the game's own instructions, then the resume pad.
    site_run.target = Address(SiteReference(h, patched, index));
    run(h, 0);
    SiteRun reference = site_run;
    site_run = saved;
    return reference;
  };
  const std::array<uint32_t, eu::SITE_COUNT> seeds = [] {
    std::array<uint32_t, eu::SITE_COUNT> values = {};
    for (size_t index = 0; index < values.size(); ++index) values[index] = 0x9E3779B1u * static_cast<uint32_t>(index + 1);
    return values;
  }();
  const auto stack_equal = [&](const SiteRun& a, const SiteRun& b) {
    return a.esp_out == b.esp_out && std::memcmp(a.gpr_out, b.gpr_out, sizeof(a.gpr_out)) == 0;
  };
  (void)stack_equal;
  int wrong = 0;
  size_t runs = 0;
  for (size_t index = 0; index < eu::SITE_COUNT; ++index) {
    const eu::SiteInfo& info = eu::SITES[index];
    const bool order = (info.kind == eu::Kind::ORDER);
    for (const bool x87_value : {false, true}) {
      // Closed gates: the original path. ORDER sites: the game's instructions; the others: the callee called as the game would, or the walk end's push and jump.
      no_ei(&harness);
      Random inputs = {.state = seeds[index] ^ (x87_value ? 0x55 : 0)};
      FillSiteRun(harness, stack_a, 0x300, scratch, &inputs, Address(harness.thunks[index]), x87_value);
      const SiteRun input = site_run;
      std::array<uint8_t, 0x100> stack_before = {};
      std::memcpy(stack_before.data(), reinterpret_cast<const uint8_t*>(site_run.esp_in), 0x100);
      run(harness, 0);
      const SiteRun closed = site_run;
      CHECK(closed.calls == 0 && harness.data->active.load() == 0);
      if (order) {
        site_run = input;
        site_run.target = Address(SiteReference(harness, patched, index));
        run(harness, 0);
        const SiteRun reference = site_run;
        wrong += (closed.tag != 1 || closed.esp_out != reference.esp_out || std::memcmp(closed.gpr_out, reference.gpr_out, sizeof(closed.gpr_out)) != 0
                  || !SiteVectorEqual(closed.fx_out, reference.fx_out, x87_value) || std::memcmp(stack_before.data(), reinterpret_cast<const uint8_t*>(input.esp_in), 0x100) != 0)
                     ? 1 : 0;
      } else if (info.kind == eu::Kind::WALK_END) {
        // lea ecx, [esp + 0x60]; the callee entered with the resume address on top, `ret` into the resume pad.
        wrong += (closed.tag != 1 || closed.callee_ran != 1 || closed.callee_ecx != input.esp_in + 0x60 || closed.callee_return != harness.resume_pad || closed.callee_esp != input.esp_in - 4) ? 1 : 0;
      } else {
        // The call's target reached by a jump: the same stack, the caller's return address, the arguments popped by the callee.
        const uint32_t pops = (info.kind == eu::Kind::WALK_CALL ? 0xC : 4);
        wrong += (closed.tag != 3 || closed.callee_ran != 1 || closed.callee_esp != input.esp_in || closed.callee_return != harness.returned_pad || closed.esp_out != input.esp_in + 4 + pops
                  || closed.gpr_out[0] != CALLEE_RESULT || closed.gpr_out[1] != CALLEE_ECX) ? 1 : 0;
      }
      ++runs;
    }
  }
  CHECK(wrong == 0);
  std::printf("25c. closed gates, %zu runs of 27 sites with and without an x87 value: the original path as the game's instructions alone (or its callee)\n", runs);

  // 25c (2). The gates open: which thread asks the handler, the Frame it is given, the state it leaves, the answers.
  const uint32_t top_a = Address(stack_a.low) + static_cast<uint32_t>(stack_a.size);
  const uint32_t top_b = Address(stack_b.low) + static_cast<uint32_t>(stack_b.size);
  const uint32_t top_c = Address(stack_c.low) + static_cast<uint32_t>(stack_c.size);
  const uint32_t stack_limit = static_cast<uint32_t>(stack.size);
  const auto gates = [](SiteHarness* h, uint32_t armed, uint32_t active, uint32_t direct_high, uint32_t limit) {
    h->data->walk_armed.store(armed);
    h->data->walk_active.store(active);
    h->data->direct_high.store(direct_high);
    h->data->limit.store(limit);
  };
  const auto thunk_run = [&](SiteHarness* h, size_t index, bool x87_value, const SiteStack& on) {
    Random inputs = {.state = seeds[index] ^ (x87_value ? 0x55u : 0u)};
    FillSiteRun(*h, on, 0x300, scratch, &inputs, Address(h->thunks[index]), x87_value);
    run(*h, 0);
    return site_run;
  };
  const auto same = [](const SiteRun& a, const SiteRun& b, bool x87_value) {
    return a.tag == b.tag && a.esp_out == b.esp_out && std::memcmp(a.gpr_out, b.gpr_out, sizeof(a.gpr_out)) == 0 && SiteVectorEqual(a.fx_out, b.fx_out, x87_value)
           && a.callee_ran == b.callee_ran && a.callee_ecx == b.callee_ecx && a.callee_return == b.callee_return && a.callee_arg == b.callee_arg && a.callee_esp == b.callee_esp;
  };
  int slow_wrong = 0;
  size_t slow_runs = 0;
  size_t reported = 0;
  size_t answered = 0;
  for (size_t index = 0; index < eu::SITE_COUNT; ++index) {
    const eu::SiteInfo& info = eu::SITES[index];
    const bool order = (info.kind == eu::Kind::ORDER);
    const bool append = (info.kind == eu::Kind::APPEND_CALL);
    const auto fail = [&](const char* what) {
      if (++slow_wrong <= 12) std::printf("   site %.*s: %s\n", static_cast<int>(info.name.size()), info.name.data(), what);
    };
    for (const bool x87_value : {false, true}) {
      for (const SiteStack* on : {&stack_a, &stack_b}) {
        const uint32_t own_top = (on == &stack_a ? top_a : top_b);
        const uint32_t other_top = (on == &stack_a ? top_b : top_a);
        harness.data->handler.store(harness.handler_clobber);
        site_run.action = 0;
        site_run.eax_value = 0;
        gates(&harness, 0, 0, 0, 0);
        const SiteRun closed = thunk_run(&harness, index, x87_value, *on);
        const uint32_t esp_in = closed.esp_in;
        const auto frame_ok = [&](const SiteRun& r) {
          const uint32_t expected[12] = {r.gpr_in[5], r.gpr_in[4], r.gpr_in[6], esp_in - 4, r.gpr_in[3], r.gpr_in[2], r.gpr_in[1], append ? CALLEE_RESULT : r.gpr_in[0],
                                         r.seen_words[8], Read32(reinterpret_cast<const uint8_t*>(esp_in)), Read32(reinterpret_cast<const uint8_t*>(esp_in) + 4),
                                         Read32(reinterpret_cast<const uint8_t*>(esp_in) + 8)};
          return r.calls == 1 && r.seen_site == index && r.seen_active == 1 && harness.data->active.load() == 0 && r.seen_frame == esp_in - 0x24
                 && std::memcmp(expected, r.seen_words, sizeof(expected)) == 0;
        };
        // The direct thread's own stack, another's, none, a third above, and (for the upper stack) one below: every gate open.
        const std::array<uint32_t, 4> directs = {own_top, other_top, 0u, top_c};
        for (const uint32_t direct : directs) {
          gates(&harness, 1, 1, direct, stack_limit);
          const SiteRun open = thunk_run(&harness, index, x87_value, *on);
          ++slow_runs;
          const bool fast = (order && direct == own_top && !eu::AsksAlways(static_cast<eu::Site>(index)));  // the forwarded and the enumeration sites ask every time
          if (append) {
            // The callee runs from the thunk with the count raised and a copy of the argument; ecx and the result come back; the thunk returns as the callee would.
            const uint32_t arg = Read32(reinterpret_cast<const uint8_t*>(esp_in) + 4);
            if (!frame_ok(open) || open.tag != 3 || open.callee_ran != 1 || open.callee_active != 1 || open.callee_arg != arg || open.callee_ecx != open.gpr_in[1]
                || open.callee_return - Address(harness.thunks[index]) >= 0x200 || open.callee_esp != esp_in - 12 || open.esp_out != esp_in + 8 || open.gpr_out[0] != CALLEE_RESULT
                || std::memcmp(open.gpr_out + 1, open.gpr_in + 1, 6 * sizeof(uint32_t)) != 0 || !SiteVectorEqual(open.fx_out, open.fx_in, x87_value)) {
              fail("the append call through the slow path");
            }
            ++reported;
            continue;
          }
          if (fast) {
            if (open.calls != 0 || !same(open, closed, x87_value)) fail("the direct thread's pass");
          } else {
            if (!frame_ok(open) || !same(open, closed, x87_value)) fail("the slow path with the handler answering 0");
            ++reported;
          }
        }
        if (order) {
          // The upper stack's thread with the direct thread's stack below it: the unsigned difference is huge, so slow.
          if (on == &stack_b) {
            gates(&harness, 1, 1, top_a, stack_limit);
            if (thunk_run(&harness, index, x87_value, *on).calls != 1) fail("a direct stack below this thread's");
          }
        }
        if (order) {
          // No direct thread (0) and a limit past any distance: `0 - esp` is below it for every stack (as it would for a stack at the top of the address space): the slow path.
          gates(&harness, 1, 1, 0, 0xFFFFFFFFu);
          if (thunk_run(&harness, index, x87_value, *on).calls != 1) fail("no direct thread, a huge limit");
        }
        // The gates alone: the walk's call asks while `walk_armed` is set; the others while `walk_active` is; both clear: the original path.
        for (const uint32_t armed : {0u, 1u}) {
          for (const uint32_t active : {0u, 1u}) {
            gates(&harness, armed, active, 0, stack_limit);
            const SiteRun gated = thunk_run(&harness, index, x87_value, *on);
            const bool asks = (info.kind == eu::Kind::WALK_CALL ? armed != 0 : active != 0);
            if (gated.calls != (asks ? 1u : 0u) || (!append && !same(gated, closed, x87_value))) fail("the gates");
          }
        }
        // The handler answers 1 (eax written): the function returns, its callee is not called; the sites that may not answer go on as the original.
        gates(&harness, 1, 1, 0, stack_limit);
        site_run.action = 1;
        site_run.eax_value = (info.return_bytes >= 0 || append ? ANSWER_EAX : 0);  // (a handler writes eax only where the answer is its own)
        const SiteRun yes = thunk_run(&harness, index, x87_value, *on);
        if (append) {
          if (yes.tag != 3 || yes.gpr_out[0] != ANSWER_EAX || yes.esp_out != esp_in + 8 || yes.callee_ran != 1) fail("an append call's answer");
        } else if (info.return_bytes >= 0) {
          if (yes.tag != 3 || yes.gpr_out[0] != ANSWER_EAX || yes.esp_out != esp_in + 4 + static_cast<uint32_t>(info.return_bytes) || yes.callee_ran != 0 || yes.calls != 1
              || std::memcmp(yes.gpr_out + 1, yes.gpr_in + 1, 6 * sizeof(uint32_t)) != 0 || !SiteVectorEqual(yes.fx_out, yes.fx_in, x87_value)) {
            fail("the function's return");
          }
          ++answered;
        } else if (!same(yes, closed, x87_value) || yes.calls != 1) {
          fail("an answer 1 at a site that may not answer");
        }
        site_run.action = 0;
        site_run.eax_value = 0;
        ++slow_runs;
      }
    }
  }
  gates(&harness, 0, 0, 0, 0);
  CHECK(slow_wrong == 0);
  std::printf("25c. gates open, %zu runs: the direct thread's pass (no handler call), %zu handler calls with the Frame, the count 1 inside and 0 after, xmm0-7 / x87 / MXCSR / registers kept\n"
              "     through a clobbering handler; %zu answers 1 (the function's `ret N`, eax as written); the E8 sites' callee called from the page with the count raised\n",
              slow_runs, reported, answered);

  // 25d. The fast path's one word. A thread on stack b, the direct thread's HighLimit read as one word, a limit.
  {
    const size_t probe = static_cast<size_t>(eu::Site::SLOT_TAKE);
    const auto calls = [&](SiteHarness* h, uint32_t armed_active, uint32_t direct_high, uint32_t limit, const SiteStack& on) {
      gates(h, armed_active, armed_active, direct_high, limit);
      return thunk_run(h, probe, false, on).calls;
    };
    int bad = 0;
    bad += (calls(&harness, 1, top_b, stack_limit, stack_b) != 0);  // the direct thread's own stack: fast
    bad += (calls(&harness, 1, top_a, stack_limit, stack_b) != 1);  // another stack below
    bad += (calls(&harness, 1, top_c, stack_limit, stack_b) != 1);  // another stack above
    bad += (calls(&harness, 1, 0, stack_limit, stack_b) != 1);      // nobody direct
    bad += (calls(&harness, 1, top_b, 0, stack_b) != 1);            // the default limit: always slow
    bad += (calls(&harness, 1, top_b, 0x304, stack_b) != 1);        // the thread's 0x300 bytes and the thunk's push: 0x304 is not below the limit
    bad += (calls(&harness, 1, top_b, 0x305, stack_b) != 0);        // one more byte: fast
    bad += (calls(&harness, 0, top_b, stack_limit, stack_b) != 0);  // the gates closed: no call at all
    CHECK(bad == 0);
    // CONTROL: the first design's two words (low and high, read apart) take the fast path when two handovers tore them; the one word does not.
    eu::control_torn_pair = true;
    SiteHarness torn = BuildSiteHarness(patched, nullptr);
    eu::control_torn_pair = false;
    CHECK(torn.fits);
    torn.data->handler.store(torn.handler_plain);
    torn.data->direct_low.store(Address(stack_a.low));  // the direct thread of one moment: stack a's low ...
    gates(&torn, 1, 1, top_c, stack_limit);             // ... and of another: stack c's high; a thread on b is in between
    const uint32_t torn_calls = thunk_run(&torn, probe, false, stack_b).calls;
    gates(&harness, 1, 1, top_c, stack_limit);
    const uint32_t real_calls = thunk_run(&harness, probe, false, stack_b).calls;
    CHECK(torn_calls == 0 && real_calls == 1);  // the control is wrong exactly where the one word is right
    torn.data->direct_low.store(Address(stack_b.low));  // the control, untorn, is right on a consistent pair
    gates(&torn, 1, 1, top_b, stack_limit);
    CHECK(thunk_run(&torn, probe, false, stack_b).calls == 0);
    CHECK(thunk_run(&torn, probe, false, stack_a).calls == 1);
    // CONTROL: no fxsave / fxrstor around the handler: a handler that clobbers xmm0-7, x87 and MXCSR shows in the result; with them it does not.
    eu::control_no_vector_save = true;
    SiteHarness unsaved = BuildSiteHarness(patched, nullptr);
    eu::control_no_vector_save = false;
    CHECK(unsaved.fits);
    unsaved.data->handler.store(unsaved.handler_clobber);
    gates(&unsaved, 1, 1, 0, stack_limit);
    const SiteRun seen = thunk_run(&unsaved, probe, true, stack_a);
    CHECK(seen.calls == 1 && !SiteVectorEqual(seen.fx_out, seen.fx_in, true));
    gates(&harness, 1, 1, 0, stack_limit);
    harness.data->handler.store(harness.handler_clobber);
    const SiteRun kept = thunk_run(&harness, probe, true, stack_a);
    CHECK(kept.calls == 1 && SiteVectorEqual(kept.fx_out, kept.fx_in, true));
    gates(&harness, 0, 0, 0, 0);
    std::printf("25d. the direct stack's one word: own stack fast; another stack, none, no limit, a limit 4 bytes short: slow; a torn low / high pair is taken wrongly by the control, not by the build; no fxsave: the clobber shows\n");
  }

  // 25e. The stack a slow path uses (a pattern-filled stack, the handler touches none): the budget the scheduler's handler adds to.
  {
    harness.data->handler.store(harness.handler_plain);
    size_t most = 0;
    for (size_t index = 0; index < eu::SITE_COUNT; ++index) {
      std::memset(stack_a.low, 0xA5, stack_a.size - 0x300);
      gates(&harness, 1, 1, 0, stack_limit);
      const SiteRun r = thunk_run(&harness, index, false, stack_a);
      const uint8_t* first = stack_a.low;
      while (first < reinterpret_cast<const uint8_t*>(r.esp_in) && *first == 0xA5) ++first;
      most = std::max(most, static_cast<size_t>(r.esp_in - Address(first)));
    }
    gates(&harness, 0, 0, 0, 0);
    std::printf("25e. the slow path's stack: %zu bytes below the site's esp (pushfd, pushad, 512 for fxsave, alignment, the handler's two words and call), before the handler's own\n", most);
    CHECK(most >= 548 && most <= 640);
  }

  // 25f. The sites in the game's code (B): through the real thunk page and a C++ handler.
  {
    eu::SetHandler(&LiveHandler);
    for (auto& count : live_calls) count.store(0);
    live_action.store(0);
    live_eax.store(0);
    live_block.store(false);
    rows(true);
    CHECK(ei::written == eu::ALL_SITES && ei::data->handler.load() == Address(reinterpret_cast<const void*>(&LiveHandler)));
    const auto open_gates = [&]() {
      ei::data->walk_armed.store(1);
      ei::data->walk_active.store(1);
      ei::data->direct_high.store(0);
      ei::data->limit.store(stack_limit);
    };
    const auto total_calls = [&]() {
      uint32_t total = 0;
      for (const auto& count : live_calls) total += count.load();
      return total;
    };
    // A fake object: [obj + 0x180] -> inner, whose first word is a vtable with the two stubs the spawn functions jump to.
    uint8_t* fake = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    CHECK(fake != nullptr);
    const std::array<uint8_t, 3> stub_query = {0xB0, 0x5A, 0xC3};                // mov al, 0x5A; ret
    const std::array<uint8_t, 5> stub_write = {0xB0, 0x33, 0xC2, 0x04, 0x00};    // mov al, 0x33; ret 4
    std::memcpy(fake, stub_query.data(), stub_query.size());
    std::memcpy(fake + 0x10, stub_write.data(), stub_write.size());
    uint32_t* vtable = reinterpret_cast<uint32_t*>(fake + 0x100);
    vtable[0x18 / 4] = Address(fake);
    vtable[0x1C / 4] = Address(fake + 0x10);
    uint32_t* inner = reinterpret_cast<uint32_t*>(fake + 0x200);
    inner[0] = Address(vtable);
    uint8_t* const outer = fake + 0x400;
    Write32(outer + 0x180, Address(inner));
    using Query = uint8_t(__thiscall*)(void*);
    using Write = uint8_t(__thiscall*)(void*, uint32_t);
    using Triple = void(__cdecl*)(float*);
    const auto query_at = [&](uint8_t* image) { return reinterpret_cast<Query>(image + eu::SITES[static_cast<size_t>(eu::Site::SPAWN_QUERY)].rva); };
    const auto write_at = [&](uint8_t* image) { return reinterpret_cast<Write>(image + eu::SITES[static_cast<size_t>(eu::Site::SPAWN_WRITE)].rva); };
    const auto triple_at = [&](uint8_t* image) { return reinterpret_cast<Triple>(image + eu::SITES[static_cast<size_t>(eu::Site::RANDOM_TRIPLE)].rva); };
    constexpr uint32_t RANDOM_STATE_RVA = 0x1DA7408;  // the game's random state, the one the triple's imul reads
    int wrong = 0;
    const auto triples = [&](int rounds, uint32_t expected_calls, bool answer_one) {
      for (int round = 0; round < rounds; ++round) {
        const uint32_t state = random.Next();
        Write32(game + RANDOM_STATE_RVA, state);
        Write32(patched + RANDOM_STATE_RVA, state);
        float out_a[3] = {1.f, 2.f, 3.f};
        float out_b[3] = {1.f, 2.f, 3.f};
        triple_at(game)(out_a);
        triple_at(patched)(out_b);
        if (answer_one) {
          // The handler returned from the function: nothing written, the state untouched.
          wrong += (std::memcmp(out_b, "\x00\x00\x80\x3F\x00\x00\x00\x40\x00\x00\x40\x40", 12) != 0 || Read32(patched + RANDOM_STATE_RVA) != state) ? 1 : 0;
        } else {
          wrong += (std::memcmp(out_a, out_b, sizeof(out_a)) != 0 || Read32(game + RANDOM_STATE_RVA) != Read32(patched + RANDOM_STATE_RVA)) ? 1 : 0;
        }
      }
      wrong += (total_calls() != expected_calls || ei::data->active.load() != 0) ? 1 : 0;
    };
    // Gates closed: the original path; open and nobody direct: every call through the slow path; answering 1: the function returns at once.
    ei::data->walk_active.store(0);
    wrong += (query_at(patched)(outer) != 0x5A || write_at(patched)(outer, 9) != 0x33) ? 1 : 0;
    triples(1000, 0, false);
    open_gates();
    wrong += (query_at(patched)(outer) != 0x5A || write_at(patched)(outer, 9) != 0x33) ? 1 : 0;
    wrong += (live_calls[static_cast<size_t>(eu::Site::SPAWN_QUERY)].load() != 1 || live_calls[static_cast<size_t>(eu::Site::SPAWN_WRITE)].load() != 1) ? 1 : 0;
    triples(1000, 2 + 1000, false);
    live_action.store(1);
    live_eax.store(0x77);
    wrong += (query_at(patched)(outer) != 0x77 || write_at(patched)(outer, 9) != 0x77) ? 1 : 0;
    triples(100, 2 + 1000 + 2 + 100, true);
    live_action.store(0);
    live_eax.store(0);
    CHECK(wrong == 0);
    std::printf("25f. the real sites (B): gates closed = the game's results; open: 1000 random triples, the spawn query and write through the slow path equal A's (state, floats); answering 1: returns at once\n");

    // 25f (2). A thread parked at each byte of each site, in the thunk page, past it; a thread inside the handler; the count above 0.
    std::atomic<bool> stop = false;
    std::thread parked([&] {
      while (!stop.load()) Sleep(1);
    });
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, GetThreadId(parked.native_handle()));
    CHECK(thread != nullptr);
    int checked = 0;
    wrong = 0;
    const auto try_off = [&](const uint8_t* where, bool inside) {
      wrong += (SuspendThread(thread) == static_cast<DWORD>(-1)) ? 1 : 0;
      CONTEXT saved = {.ContextFlags = CONTEXT_CONTROL};
      wrong += (GetThreadContext(thread, &saved) == FALSE) ? 1 : 0;
      CONTEXT moved = saved;
      moved.Eip = Address(where);
      wrong += (SetThreadContext(thread, &moved) == FALSE) ? 1 : 0;
      ec::failed_writes = 0;
      rows(false);
      const bool still_in = (ei::written == eu::ALL_SITES);
      wrong += (still_in != inside || (!still_in && ei::written != 0)) ? 1 : 0;
      wrong += (SetThreadContext(thread, &saved) == FALSE) ? 1 : 0;
      ResumeThread(thread);
      ++checked;
      ec::failed_writes = 0;
      rows(true);
      wrong += (ei::written != eu::ALL_SITES) ? 1 : 0;
    };
    for (size_t index = 0; index < eu::SITE_COUNT; ++index) {
      const uint8_t* start = ei::sites[index].at;
      for (size_t offset = 0; offset <= ei::sites[index].size; ++offset) try_off(start + offset, offset > 0 && offset < ei::sites[index].size);
    }
    try_off(ei::page, true);
    try_off(ei::page_end - 1, true);
    for (size_t index = 0; index < eu::SITE_COUNT; ++index) try_off(ei::thunks[index] + 3, true);
    try_off(reinterpret_cast<const uint8_t*>(ei::data), false);  // the data page: not code
    stop = true;
    parked.join();
    CloseHandle(thread);
    CHECK(wrong == 0);
    std::printf("25f. a parked thread at %d places (each byte of each site and the byte after: refused strictly inside; the thunk page's first, last and each thunk's 4th byte: refused; the data page: allowed)\n", checked);

    // The count above 0 (a thread inside a game call made from a thunk, here: inside the handler): the sites stay in, then come out.
    open_gates();
    live_block.store(true);
    live_blocked.store(false);
    std::atomic<uint32_t> held_result = 0;
    std::thread held([&] { held_result = query_at(patched)(outer); });
    while (!live_blocked.load()) Sleep(1);
    CHECK(ei::data->active.load() == 1);
    ec::failed_writes = 0;
    rows(false);
    CHECK(ei::written == eu::ALL_SITES && ei::data->walk_active.load() == 0);  // refused, the gates closed
    live_block.store(false);
    held.join();
    CHECK(held_result.load() == 0x5A && ei::data->active.load() == 0);
    ec::failed_writes = 0;
    rows(false);
    CHECK(ei::written == 0 && std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
    // The same count as a stuck word (an unwind through a thunk): Uninstall cannot take the sites out; the module is pinned, the gates closed.
    rows(true);
    ei::data->active.store(1);
    ec::module_pinned = false;
    twattila::engine_code::Uninstall();
    CHECK(ei::written == eu::ALL_SITES && ec::module_pinned && ei::data->walk_active.load() == 0);
    ei::data->active.store(0);
    ec::module_pinned = false;
    ec::failed_writes = 0;
    rows(false);
    CHECK(ei::written == 0 && !ec::module_pinned && std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
    std::printf("25f. a thread inside the handler (count 1): the sites stay in, the gates close, they come out once it is gone; a stuck count at Uninstall pins the module\n");

    // 25f (3). 200 switches under 4 threads calling the spawn functions through the sites (slow path while in).
    rows(true);
    open_gates();
    std::atomic<bool> halt = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> done = 0;
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        uint32_t counter = static_cast<uint32_t>(worker);
        while (!halt.load()) {
          bad += (query_at(patched)(outer) != 0x5A || write_at(patched)(outer, ++counter) != 0x33) ? 1 : 0;
          ++done;
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
        rows(on);
        if (on) {
          open_gates();
          Sleep(2);  // the threads run through the slow path for a while
        }
        ++tries;
      } while (((ei::written == eu::ALL_SITES) != on) && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      waited += tries > 1 ? 1 : 0;
      never += ((ei::written == eu::ALL_SITES) != on) ? 1 : 0;
    }
    rows(true);
    open_gates();
    halt = true;
    for (auto& thread : threads) thread.join();
    std::printf("25f. 200 switches while 4 threads made %llu spawn calls through the sites: %llu wrong (%d writes waited, %d switches never reached), %u handler calls\n", done.load(),
                bad.load(), waited, never, total_calls());
    CHECK(bad.load() == 0 && never == 0 && done.load() > 0 && ei::data->active.load() == 0 && ei::written == eu::ALL_SITES && !ec::module_pinned);
    rows(false);
    CHECK(ei::written == 0 && std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
    VirtualFree(fake, 0, MEM_RELEASE);
  }
  VirtualFree(harness.code, 0, MEM_RELEASE);
  return 0;
}
