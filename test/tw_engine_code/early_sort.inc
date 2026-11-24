// ====================================================================================================================
// 31. Early Unit Sort, the game's code (Attila only; included inside tests.cpp's anonymous namespace after exit_drain.inc: Read32, Matches, CHECK are used). What the design of the early
// start of the weighted "instances sort" task rests on, read in the mapped DLL (never run):
// 31a. the four sites and the five checks: each pattern once in the code, at its RVA; the two entries of the draw-update pass are its only references (one E8, one E9, no conditional jump, no
//    relocated address); the Rigid draw update has one reference, the Rigid vtable's slot 0x30; the weighted start block, its start thunk and the start function behind it; the task wait;
// 31b. the node's layout: the weighted task block (task, node, owner, name, started flag) and the owner at node + 0xB8 in the node's constructor, the consumer's wait-and-clear;
// 31c. the one add writer: the weighted add has one return (ret 0x1C), 10 relocated references (vtable slots) and no direct caller; the weighted adder, the instance key and the record
//    allocation are called from inside it only; the per-thread push has four callers, all in the adders; the mesh factory adds the owner's offset 10 times, each before one of the 10 weighted
//    constructors; the owner field has one store, in the weighted base constructor, and the weighted add reads it;
// 31d. the door: the default parameters zero +4 and +0x1C, and the two weighted mesh-add bodies return on them;
// 31e. the writers that do not add: the list reset waits for the task; the node clear (one reference, the Rigid vtable's slot 0xCC) and the weighted mesh clear behind it (10 references) never
//    test the flag and never wait; the clear frees the whole record pool (one sized delete per block, the three zeroing stores); the pool's users; the global byte the list reset sets has no reader;
// 31f. the task's output: the push's size at +4, the consumer's read of node + 0xDC and node + 0xD8, the list reset's store to owner + 0x24;
// 31g. rule H's seven sites hold the game's bytes, and each is a row heapfix patches;
// 31h. the mesh key's two fields with constructor-only stores, the callback field's two stores, the weighted destructor's calls (no wait, no list unlink);
// compile time: the written and found ranges against heapfix's rows.
// ====================================================================================================================

// Every E8, E9 and 0F 8x rel32 of the code section that goes to `target_rva` (`opcode`: 0xE8, 0xE9, or 0x0F for a conditional jump).
struct RelativeReference {
  uint32_t rva;
  uint8_t opcode;
};
std::vector<RelativeReference> RelativeReferences(const uint8_t* game, uint32_t text_rva, uint32_t text_size, uint32_t target_rva) {
  std::vector<RelativeReference> references;
  for (uint32_t at = text_rva; at + 6 <= text_rva + text_size; ++at) {
    if ((game[at] == 0xE8 || game[at] == 0xE9) && at + 5 + Read32(game + at + 1) == target_rva) {
      references.push_back({.rva = at, .opcode = game[at]});
    } else if (game[at] == 0x0F && (game[at + 1] & 0xF0) == 0x80 && at + 6 + Read32(game + at + 2) == target_rva) {
      references.push_back({.rva = at, .opcode = 0x0F});
    }
  }
  return references;
}

// The RVA of every relocated dword of the image that holds the address of `target_rva` (a vtable slot, a table entry, an absolute operand), from the relocation entries as Map walks them.
std::vector<uint32_t> AbsoluteReferences(const uint8_t* game, uint32_t target_rva) {
  std::vector<uint32_t> references;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(game + reinterpret_cast<const IMAGE_DOS_HEADER*>(game)->e_lfanew);
  const auto& relocations = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  const uint32_t target = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(game)) + target_rva;
  for (uint32_t at = 0; at < relocations.Size;) {
    const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(game + relocations.VirtualAddress + at);
    if (block->SizeOfBlock == 0) break;
    const auto* entries = reinterpret_cast<const uint16_t*>(block + 1);
    for (size_t entry = 0; entry < (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2; ++entry) {
      const uint32_t rva = block->VirtualAddress + (entries[entry] & 0xFFF);
      if ((entries[entry] >> 12) == IMAGE_REL_BASED_HIGHLOW && Read32(game + rva) == target) references.push_back(rva);
    }
    at += block->SizeOfBlock;
  }
  return references;
}

// The four sites' patterns and the checks' (pk4 design 4.2); S3 moves them into engine_code.hpp.
constexpr std::string_view SORT_WEIGHTED_ADD = "83 EC 4C 56 57 8B 7C 24 6C 8B F1 8D 4C 24 20 51 8D 4C 24 28 83 BE 00 01 00 00 02";
constexpr std::string_view SORT_DRAW_PASS_CALLER = "8B 4E 08 50 8B 49 18 E8 ?? ?? ?? ?? 8D 4C 24 07 5E 83 C4 04 E9";
constexpr std::string_view SORT_DRAW_PASS_THUNK = "8B 49 1C 8B 01 FF 60 30 CC CC CC CC CC CC CC CC 8B 49 08 E9 ?? ?? ?? ??";
constexpr std::string_view SORT_NODE_CLEAR = "53 56 57 8B F9 33 F6 8B 9F A0 00 00 00 85 DB 74 16 8B 87 A4 00 00 00 8B 0C B0 8B 01 FF 90 2C 01 00 00";
constexpr std::string_view SORT_DRAW_PASS_NODE_LOOP = "8B 83 40 05 00 00 57 8B 0C B0 8B 01 FF 50 30 46 3B B3 3C 05 00 00";
constexpr std::string_view SORT_RIGID_DRAW_UPDATE = "56 8B F1 83 BE 6C 0C 00 00 00 8B 46 34 74 38";
constexpr std::string_view SORT_WEIGHTED_START = "80 BE 94 0A 00 00 00 75 17 6A 00 FF B6 84 0A 00 00 C6 86 94 0A 00 00 01 E8 ?? ?? ?? ?? 83 C4 08";
constexpr std::string_view SORT_TASK_START = "56 E8 ?? ?? ?? ?? 8B 74 24 08 BA 01 00 00 00 8D 4E 18 87 11";
constexpr std::string_view SORT_TASK_WAIT = "56 8B F1 56 E8 ?? ?? ?? ?? 83 C4 04 84 C0 75 09 8D 4E 1C 5E E9";
constexpr uint32_t SORT_WEIGHTED_ADD_RVA = 0x17F7FC0;
constexpr uint32_t SORT_WEIGHTED_ADD_END = 0x17F8423;  // after its one return
constexpr uint32_t SORT_DRAW_PASS_CALLER_RVA = 0x177C1B0;
constexpr uint32_t SORT_DRAW_PASS_CALL = 7;
constexpr uint32_t SORT_DRAW_PASS_THUNK_RVA = 0x1748810;
constexpr uint32_t SORT_DRAW_PASS_JUMP = 0x13;
constexpr uint32_t SORT_NODE_CLEAR_RVA = 0x1832CB0;
constexpr uint32_t SORT_DRAW_PASS_RVA = 0x1748A80;
constexpr uint32_t SORT_DRAW_PASS_NODE_LOOP_RVA = SORT_DRAW_PASS_RVA + 0x350;
constexpr uint32_t SORT_RIGID_DRAW_UPDATE_RVA = 0x1827A70;
constexpr uint32_t SORT_WEIGHTED_START_RVA = SORT_RIGID_DRAW_UPDATE_RVA + 0xF6;
constexpr uint32_t SORT_WEIGHTED_START_CALL = 0x18;
constexpr uint32_t SORT_TASK_START_THUNK_RVA = 0x16987C0;
constexpr uint32_t SORT_TASK_START_RVA = 0x16AF970;
constexpr uint32_t SORT_TASK_WAIT_RVA = 0x16B5500;
constexpr uint32_t SORT_RIGID_VTABLE_RVA = 0x1CBFC10;
// Rule H's seven sites (pk4 design 4.6): rows heapfix patches, with the game's five bytes.
struct SortReplacedSite {
  uint32_t rva;
  std::array<uint8_t, 5> game;
};
constexpr std::array<SortReplacedSite, 7> SORT_REPLACED_SITES = {{{0x17B92D0, {0x83, 0xEC, 0x08, 0x53, 0x55}},
                                                                  {0x17B9297, {0xE8, 0x34, 0x43, 0xFF, 0xFF}},
                                                                  {0x17B9197, {0xE8, 0xB4, 0x39, 0xFF, 0xFF}},
                                                                  {0x17AD5D0, {0x83, 0xEC, 0x14, 0x53, 0x55}},
                                                                  {0x1811F00, {0x53, 0x55, 0x56, 0x8B, 0xF1}},
                                                                  {0x18124E8, {0xE8, 0xB3, 0x94, 0x08, 0x00}},
                                                                  {0x192D2DA, {0xE8, 0x01, 0x7F, 0xFB, 0xFF}}}};
// Each of the seven is the first byte of a row heapfix patches.
static_assert(std::ranges::all_of(SORT_REPLACED_SITES, [](const SortReplacedSite& site) {
  return std::ranges::any_of(twattila::engine_code::internal::HEAPFIX_PATCHED,
                             [&](const twattila::engine_code::internal::HeapfixRange& range) { return range.rva == site.rva; });
}));
// The four written ranges touch no row heapfix patches, hooks for timing or checks; the nine found-by ranges touch no row it patches (the pass is found by its node loop: its head is a timing hook's row).
static_assert([] {
  namespace ec = twattila::engine_code::internal;
  const auto no_entry = [](uint32_t) { return false; };
  for (const uint32_t site : {SORT_WEIGHTED_ADD_RVA, SORT_DRAW_PASS_CALLER_RVA + SORT_DRAW_PASS_CALL, SORT_DRAW_PASS_THUNK_RVA + SORT_DRAW_PASS_JUMP, SORT_NODE_CLEAR_RVA}) {
    if (ec::TouchesPatched(site, ec::SITE_SIZE, no_entry) || ec::TouchesChecked(site, ec::SITE_SIZE)) return false;
  }
  const std::array<std::pair<uint32_t, std::string_view>, 9> found_by = {{{SORT_WEIGHTED_ADD_RVA, SORT_WEIGHTED_ADD},
                                                                          {SORT_DRAW_PASS_CALLER_RVA, SORT_DRAW_PASS_CALLER},
                                                                          {SORT_DRAW_PASS_THUNK_RVA, SORT_DRAW_PASS_THUNK},
                                                                          {SORT_NODE_CLEAR_RVA, SORT_NODE_CLEAR},
                                                                          {SORT_DRAW_PASS_NODE_LOOP_RVA, SORT_DRAW_PASS_NODE_LOOP},
                                                                          {SORT_RIGID_DRAW_UPDATE_RVA, SORT_RIGID_DRAW_UPDATE},
                                                                          {SORT_WEIGHTED_START_RVA, SORT_WEIGHTED_START},
                                                                          {SORT_TASK_START_RVA, SORT_TASK_START},
                                                                          {SORT_TASK_WAIT_RVA, SORT_TASK_WAIT}}};
  for (const auto& [rva, pattern] : found_by) {
    if (ec::TouchesPatched(rva, static_cast<uint32_t>(ec::PatternSize(pattern)), no_entry)) return false;
  }
  return true;
}());
static_assert(0xB8 + 0x24 == 0xDC && 0xB8 + 0x9DC == 0xA94);  // the owner (node + 0xB8): its output's size is node + 0xDC, the node's started flag is owner + 0x9DC

int RunEarlySortTests(const TestBuild& build, const uint8_t* game, uint32_t text_rva, uint32_t text_size) {
  namespace ec = twattila::engine_code::internal;
  const uint8_t* text = game + text_rva;
  if (build.code != &ec::ATTILA_BUILD) {
    std::printf("31. early unit sort: Attila only (skipped; here the weighted add's pattern matches %zu time(s), the node clear's %zu, the pass caller's %zu, the pass thunk's %zu)\n",
                Matches(text, text_size, SORT_WEIGHTED_ADD), Matches(text, text_size, SORT_NODE_CLEAR), Matches(text, text_size, SORT_DRAW_PASS_CALLER),
                Matches(text, text_size, SORT_DRAW_PASS_THUNK));
    return 0;
  }
  const uint32_t game_base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(game));
  const auto holds = [&](uint32_t rva, std::string_view pattern) { return Matches(game + rva, ec::PatternSize(pattern), pattern) == 1; };
  const auto count_in = [&](uint32_t begin, uint32_t end, std::string_view pattern) { return Matches(game + begin, end - begin, pattern); };
  const auto call_target = [&](uint32_t rva) { return rva + 5 + Read32(game + rva + 1); };
  // The E8 / E9 targets of [begin, end), in order.
  const auto calls_in = [&](uint32_t begin, uint32_t end) {
    std::vector<uint32_t> targets;
    for (uint32_t at = begin; at + 5 <= end; ++at) {
      const uint32_t target = call_target(at);
      if ((game[at] == 0xE8 || game[at] == 0xE9) && target >= text_rva && target < text_rva + text_size) targets.push_back(target);
    }
    return targets;
  };
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(game + reinterpret_cast<const IMAGE_DOS_HEADER*>(game)->e_lfanew);
  const auto section_of = [&](uint32_t rva) {
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
      if (rva >= section->VirtualAddress && rva < section->VirtualAddress + section->Misc.VirtualSize) {
        return std::string(reinterpret_cast<const char*>(section->Name), strnlen(reinterpret_cast<const char*>(section->Name), sizeof(section->Name)));
      }
    }
    return std::string();
  };
  const auto list = [](const std::vector<uint32_t>& rvas) {
    std::string text_list;
    for (const uint32_t rva : rvas) text_list += std::format("{}+0x{:X}", text_list.empty() ? "" : ", ", rva);
    return text_list;
  };
  const auto relative = [&](uint32_t target_rva) { return RelativeReferences(game, text_rva, text_size, target_rva); };
  const auto relative_rvas = [&](uint32_t target_rva) {
    std::vector<uint32_t> rvas;
    for (const RelativeReference& reference : relative(target_rva)) rvas.push_back(reference.rva);
    return rvas;
  };

  // 31a. The sites and the checks: each pattern once in the code, at its RVA.
  const std::array<std::pair<uint32_t, std::string_view>, 9> patterns = {{{SORT_WEIGHTED_ADD_RVA, SORT_WEIGHTED_ADD},
                                                                          {SORT_DRAW_PASS_CALLER_RVA, SORT_DRAW_PASS_CALLER},
                                                                          {SORT_DRAW_PASS_THUNK_RVA, SORT_DRAW_PASS_THUNK},
                                                                          {SORT_NODE_CLEAR_RVA, SORT_NODE_CLEAR},
                                                                          {SORT_DRAW_PASS_NODE_LOOP_RVA, SORT_DRAW_PASS_NODE_LOOP},
                                                                          {SORT_RIGID_DRAW_UPDATE_RVA, SORT_RIGID_DRAW_UPDATE},
                                                                          {SORT_WEIGHTED_START_RVA, SORT_WEIGHTED_START},
                                                                          {SORT_TASK_START_RVA, SORT_TASK_START},
                                                                          {SORT_TASK_WAIT_RVA, SORT_TASK_WAIT}}};
  for (const auto& [rva, pattern] : patterns) {
    const size_t matches = Matches(text, text_size, pattern);
    if (matches != 1 || !holds(rva, pattern)) std::printf("   the pattern of +0x%X: %zu matches in the code, at its RVA %d\n", rva, matches, holds(rva, pattern) ? 1 : 0);
    CHECK(matches == 1 && holds(rva, pattern));
  }
  const uint32_t pass_call_rva = SORT_DRAW_PASS_CALLER_RVA + SORT_DRAW_PASS_CALL;
  const uint32_t pass_jump_rva = SORT_DRAW_PASS_THUNK_RVA + SORT_DRAW_PASS_JUMP;
  CHECK(std::memcmp(game + SORT_WEIGHTED_ADD_RVA, "\x83\xEC\x4C\x56\x57", 5) == 0 && std::memcmp(game + pass_call_rva, "\xE8\xC4\xC8\xFC\xFF", 5) == 0
        && std::memcmp(game + pass_jump_rva, "\xE9\x58\x02\x00\x00", 5) == 0 && std::memcmp(game + SORT_NODE_CLEAR_RVA, "\x53\x56\x57\x8B\xF9", 5) == 0);
  {
    const std::vector<RelativeReference> pass = relative(SORT_DRAW_PASS_RVA);
    CHECK(pass.size() == 2 && pass[0].rva == pass_jump_rva && pass[0].opcode == 0xE9 && pass[1].rva == pass_call_rva && pass[1].opcode == 0xE8);
    CHECK(AbsoluteReferences(game, SORT_DRAW_PASS_RVA).empty());
    const std::vector<uint32_t> draw_update = AbsoluteReferences(game, SORT_RIGID_DRAW_UPDATE_RVA);
    CHECK(relative(SORT_RIGID_DRAW_UPDATE_RVA).empty() && draw_update.size() == 1 && draw_update[0] == SORT_RIGID_VTABLE_RVA + 0x30);
    // The start block's call goes to the thunk, an E9 to the start function.
    const uint32_t thunk = call_target(SORT_WEIGHTED_START_RVA + SORT_WEIGHTED_START_CALL);
    CHECK(thunk == SORT_TASK_START_THUNK_RVA && game[thunk] == 0xE9 && call_target(thunk) == SORT_TASK_START_RVA);
    std::printf("31. early unit sort: the 4 site patterns and the 5 checks once each at their RVAs; the pass +0x%X has 2 references (E9 at +0x%X, E8 at +0x%X), no relocated address; the Rigid draw "
                "update +0x%X has 1 (vtable +0x%X slot 0x30); the weighted start calls +0x%X, an E9 to +0x%X\n",
                SORT_DRAW_PASS_RVA, pass_jump_rva, pass_call_rva, SORT_RIGID_DRAW_UPDATE_RVA, SORT_RIGID_VTABLE_RVA, thunk, SORT_TASK_START_RVA);
  }

  // 31b. The node's layout.
  CHECK(holds(0x17C9BDB, "8D 9F 84 0A 00 00 89 7B 04 89 73 08 C7 43 0C ?? ?? ?? ?? C6 43 10 00") && holds(0x17C9C3F, "8D 87 B8 00 00 00"));
  CHECK(holds(0x186BE70, "56 8B F1 80 BE 94 0A 00 00 00 74 12 8B 8E 84 0A 00 00 E8 ?? ?? ?? ?? C6 86 94 0A 00 00 00") && holds(0x186BE7C, "8B 8E 84 0A 00 00 E8")
        && call_target(0x186BE82) == SORT_TASK_WAIT_RVA);
  std::printf("   the node: the weighted block at +0xA84 (task, node +4, owner +8, name +0xC, flag +0x10) and the owner node + 0xB8 in the constructor; the consumer +0x186BE70 waits (no push) and clears the flag\n");

  // 31c. The one add writer.
  {
    CHECK(count_in(SORT_WEIGHTED_ADD_RVA, SORT_WEIGHTED_ADD_END, "C2 1C 00") == 1 && holds(SORT_WEIGHTED_ADD_END - 3, "C2 1C 00"));
    const std::vector<uint32_t> add_references = AbsoluteReferences(game, SORT_WEIGHTED_ADD_RVA);
    if (add_references.size() != 10) std::printf("   weighted add references: %s\n", list(add_references).c_str());
    CHECK(relative(SORT_WEIGHTED_ADD_RVA).empty() && add_references.size() == 10);
    for (const uint32_t reference : add_references) CHECK(section_of(reference) == ".rdata");
    const auto inside_add = [](uint32_t rva) { return rva >= SORT_WEIGHTED_ADD_RVA && rva < SORT_WEIGHTED_ADD_END; };
    const std::vector<uint32_t> adder = relative_rvas(0x17AE420);
    const std::vector<uint32_t> key = relative_rvas(0x1833DC0);
    const std::vector<uint32_t> record = relative_rvas(0x1855CC0);
    const std::vector<uint32_t> push = relative_rvas(0x1870BD0);
    CHECK(adder.size() == 4 && std::ranges::all_of(adder, inside_add) && key.size() == 1 && key[0] == 0x17F8368 && record.size() == 2 && std::ranges::all_of(record, inside_add));
    // The per-thread push: both pushes of the weighted adder (+0x17AE420), and the second push of the two adders before it (+0x17AE370, +0x17AE3D0: their record goes through +0x176CED0).
    if (push.size() != 4) std::printf("   per-thread push callers: %s\n", list(push).c_str());
    CHECK(push == (std::vector<uint32_t>{0x17AE3BF, 0x17AE416, 0x17AE441, 0x17AE46F}) && holds(0x17AE3C6, "C2 14 00 CC") && holds(0x17AE41D, "C2 10 00 8B 44 24 10 56")
          && holds(0x17AE476, "C2 14 00 CC"));
    // The factory: the owner's offset added 10 times, each before one of the 10 weighted constructors.
    constexpr uint32_t FACTORY_RVA = 0x185B0C0;
    constexpr uint32_t FACTORY_END = 0x185CE40;
    constexpr std::array<uint32_t, 10> CONSTRUCTORS = {0x17D2D00, 0x17D38C0, 0x17D3CB0, 0x17D3AD0, 0x17D3A80, 0x17D3B20, 0x17D3B70, 0x17D3C10, 0x17D3BC0, 0x17D3C60};
    CHECK(count_in(FACTORY_RVA, FACTORY_END, "05 B8 00 00 00") == 9 && count_in(FACTORY_RVA, FACTORY_END, "81 C1 B8 00 00 00") == 1);
    std::vector<uint32_t> constructed;
    for (uint32_t at = FACTORY_RVA; at + 6 <= FACTORY_END; ++at) {
      const bool add_eax = holds(at, "05 B8 00 00 00");
      if (!add_eax && !holds(at, "81 C1 B8 00 00 00")) continue;
      // The next call after the add.
      uint32_t call = at + (add_eax ? 5 : 6);
      while (call + 5 <= FACTORY_END && !(game[call] == 0xE8 && std::ranges::find(CONSTRUCTORS, call_target(call)) != CONSTRUCTORS.end())) ++call;
      CHECK(call + 5 <= FACTORY_END);
      constructed.push_back(call_target(call));
    }
    std::ranges::sort(constructed);
    CHECK(constructed.size() == 10 && std::ranges::adjacent_find(constructed) == constructed.end());
    // The owner field: one store, in the weighted base constructor; the weighted add reads it (four pushes).
    CHECK(holds(0x17D396F, "89 83 44 02 00 00") && count_in(0x17D38C0, 0x17D3A80, "89 ?? 44 02 00 00") == 1
          && count_in(SORT_WEIGHTED_ADD_RVA, SORT_WEIGHTED_ADD_END, "FF B6 44 02 00 00") == 4);
    std::printf("   the one add writer: the weighted add has 1 return, 10 vtable references and no direct caller; the weighted adder (4 calls), the instance key (1) and the record allocation (2) "
                "are called inside it only; the per-thread push has 4 callers in 3 adders; the factory adds the owner's offset 10 times, once per weighted constructor; the owner field has 1 store\n");
  }

  // 31d. The door: the default parameters zero +4 and +0x1C; the two weighted mesh-add bodies return when they are null.
  CHECK(holds(0x1B8F68, "C7 41 04 00 00 00 00") && holds(0x1B8F92, "C7 41 1C 00 00 00 00"));
  CHECK(holds(0x17F7E02, "8B 74 24 18 85 F6 74 2D 8B 4E 1C 85 C9 74 26") && holds(0x17F7D1D, "8B B4 24 00 01 00 00 85 F6 0F 84 ?? ?? ?? ?? 8B 5E 04 85 DB 0F 84"));
  std::printf("   the door: the default parameters (+0x1B8F60) zero +4 and +0x1C; the weighted bodies test them at +0x17F7E02 and +0x17F7D1D\n");

  // 31e. The writers that do not add.
  {
    CHECK(holds(0x1811F0D, "80 BE 94 0A 00 00 00 74 12 8B 8E 84 0A 00 00 E8 ?? ?? ?? ?? C6 86 94 0A 00 00 00") && call_target(0x1811F1C) == SORT_TASK_WAIT_RVA);
    const std::vector<uint32_t> clear = AbsoluteReferences(game, SORT_NODE_CLEAR_RVA);
    CHECK(relative(SORT_NODE_CLEAR_RVA).empty() && clear.size() == 1 && clear[0] == SORT_RIGID_VTABLE_RVA + 0xCC);
    constexpr uint32_t MESH_CLEAR_RVA = 0x1832D30;
    constexpr uint32_t MESH_CLEAR_END = 0x1832D72;
    constexpr uint32_t POOL_RELEASE_RVA = 0x18325B0;
    constexpr uint32_t POOL_RELEASE_END = POOL_RELEASE_RVA + 66;
    constexpr uint32_t POOL_RVA = 0x1DA6C50;
    const std::vector<uint32_t> mesh_clear = AbsoluteReferences(game, MESH_CLEAR_RVA);
    CHECK(mesh_clear.size() == 10 && std::ranges::all_of(mesh_clear, [&](uint32_t reference) { return section_of(reference) == ".rdata"; }));
    CHECK(count_in(MESH_CLEAR_RVA, MESH_CLEAR_END, "80 ?? 94 0A 00 00") == 0
          && calls_in(MESH_CLEAR_RVA, MESH_CLEAR_END) == (std::vector<uint32_t>{POOL_RELEASE_RVA, 0x178FD90, 0x1810D30}));
    // The pool: the clear releases the whole of it.
    CHECK(game[MESH_CLEAR_RVA + 8] == 0xB9 && Read32(game + MESH_CLEAR_RVA + 9) == game_base + POOL_RVA && game[MESH_CLEAR_RVA + 0xE] == 0xE8 && call_target(MESH_CLEAR_RVA + 0xE) == POOL_RELEASE_RVA);
    CHECK(holds(POOL_RELEASE_RVA, "57 8B F9 8B 47 04 85 C0 74 21") && game[POOL_RELEASE_END - 1] == 0xC3 && game[POOL_RELEASE_END] == 0xCC);
    CHECK(count_in(POOL_RELEASE_RVA, POOL_RELEASE_END, "68 88 0F 00 00 50 E8") == 1 && holds(0x18325C6, "68 88 0F 00 00 50 E8") && call_target(0x18325CC) == 0x19EA6DB);
    CHECK(count_in(POOL_RELEASE_RVA, POOL_RELEASE_END, "8B B0 80 0F 00 00") == 1 && count_in(POOL_RELEASE_RVA, POOL_RELEASE_END, "C7 47 04 00 00 00 00") == 1
          && count_in(POOL_RELEASE_RVA, POOL_RELEASE_END, "C7 47 08 00 00 00 00") == 1 && count_in(POOL_RELEASE_RVA, POOL_RELEASE_END, "C7 47 0C 00 00 00 00") == 1);
    CHECK(calls_in(POOL_RELEASE_RVA, POOL_RELEASE_END) == std::vector<uint32_t>{0x19EA6DB});
    CHECK(holds(0x1855CCB, "68 F8 00 00 00 B9 ?? ?? ?? ?? E8") && Read32(game + 0x1855CD1) == game_base + POOL_RVA);
    // The pool's users: nine relocated addresses in five functions (the weighted base constructor +0x17D38C0, its destroy +0x17D4420, the mesh clear, the record allocation +0x1855CC0 and
    // a static initializer +0x1AE8D50).
    const std::vector<uint32_t> pool = AbsoluteReferences(game, POOL_RVA);
    if (pool.size() != 9) std::printf("   pool references: %s\n", list(pool).c_str());
    CHECK(pool == (std::vector<uint32_t>{0x17D3A3B, 0x17D454A, 0x1832D39, 0x1855CD1, 0x1855CF7, 0x1855D1F, 0x1855D24, 0x1AE8D54, 0x1AE8D5A}));
    // The global byte the list reset sets and the draw update clears: two references, both `mov byte [address], imm8` stores, so nothing reads it.
    const std::vector<uint32_t> flag_byte = AbsoluteReferences(game, 0x1DA698E);
    CHECK(flag_byte == (std::vector<uint32_t>{0x1811F07, 0x1827ABE}) && holds(0x1811F05, "C6 05 ?? ?? ?? ?? 01") && holds(0x1827ABC, "C6 05 ?? ?? ?? ?? 00"));
    std::printf("   the writers that do not add: the list reset waits; the node clear (1 reference, slot 0xCC) and the mesh clear (10) test no flag and call no wait; the clear frees the whole pool "
                "+0x%X (9 references in 5 functions); the byte +0x1DA698E has 2 stores and no reader\n",
                POOL_RVA);
  }

  // 31f. The task's output.
  CHECK(holds(0xD3424, "8B 7E 04") && holds(0xD3439, "FF 46 04") && holds(0x186BEBD, "FF B6 DC 00 00 00 8D 86 D8 00 00 00") && holds(0x1811F28, "8B 9E 8C 0A 00 00")
        && holds(0x1811F62, "C7 43 24 00 00 00 00"));

  // 31g. Rule H's seven sites hold the game's bytes.
  for (const SortReplacedSite& site : SORT_REPLACED_SITES) CHECK(std::memcmp(game + site.rva, site.game.data(), site.game.size()) == 0);
  std::printf("   the output: the push's size at +4, the consumer reads node + 0xDC and node + 0xD8, the list reset zeroes owner + 0x24; rule H's 7 sites hold the game's bytes\n");

  // 31h. The extra facts.
  {
    // Every store of the code section with the field's displacement, as a fixed list: a new hit is a new writer to look at. Of the weighted mesh's two key fields, the hits in the
    // renderer's mesh code are in the weighted base constructor [+0x17D38C0, +0x17D3A80) alone (+0x17D394E the byte, +0x17D3A54 the word as a dword store); the others are other
    // classes' fields at the same offset (by the decode). The node's callback field: zeroed in its constructor (+0x17CA2BF), stored by the setter (+0x188BC14).
    struct StoreScan {
      uint8_t first;
      std::string_view pattern;
      std::vector<uint32_t> hits;
    };
    const std::array<StoreScan, 8> scans = {{
        {0x66, "66 89 ?? 54 05 00 00", {}},
        {0x66, "66 C7 ?? 54 05 00 00", {}},
        {0x89, "89 ?? 54 05 00 00", {0x76D3B0, 0x17D3A54}},
        {0xC7, "C7 ?? 54 05 00 00", {0xD680A6, 0xD700F1, 0x148175F, 0x14831A4, 0x16D7E69, 0x17BD9AF, 0x17BDBFF, 0x17BDE4F, 0x17BE09F, 0x17BE2EF, 0x17BE4F7, 0x17BE73F, 0x18C3488}},
        {0x88, "88 ?? 3C 02 00 00", {0x12687E, 0x1290EB, 0x1298C9, 0x17D394E}},
        {0xC6, "C6 ?? 3C 02 00 00", {0x126C61, 0x1294CF, 0x129DB5, 0x1B74C7, 0x2CB9D3}},
        {0x89, "89 ?? 6C 0C 00 00", {0xE03229, 0x1640D83, 0x16F56F8, 0x188BC14}},
        {0xC7, "C7 ?? 6C 0C 00 00", {0x139D12, 0xDEC84D, 0xE04CB4, 0x161FFAA, 0x1676AD2, 0x17CA2BF}},
    }};
    for (const StoreScan& scan : scans) {
      std::vector<uint32_t> hits;
      for (uint32_t at = text_rva; at + ec::PatternSize(scan.pattern) <= text_rva + text_size; ++at) {
        if (game[at] == scan.first && holds(at, scan.pattern)) hits.push_back(at);
      }
      if (hits != scan.hits) std::printf("   %.*s: %zu hits: %s\n", static_cast<int>(scan.pattern.size()), scan.pattern.data(), hits.size(), list(hits).c_str());
      CHECK(hits == scan.hits);
    }
    CHECK(holds(0x17D394E, "88 83 3C 02 00 00") && holds(0x17D3A54, "89 83 54 05 00 00") && holds(0x17CA2BF, "C7 87 6C 0C 00 00 00 00 00 00") && holds(0x188BC14, "89 81 6C 0C 00 00 C2 04 00"));
    // The weighted destructor: three calls and a tail jump, none the task wait (the list unlink would be a fifth).
    CHECK(calls_in(0x17D8590, 0x17D85D8) == (std::vector<uint32_t>{0x17D4420, 0xE98C0, 0x17D5290, 0x17D5900}));
    std::printf("   the extra facts: the mesh key's byte (+0x23C) and word (+0x554) are stored in the weighted base constructor alone (4 + 5 and 2 + 13 stores with those offsets in the code, "
                "fixed); the callback field (+0xC6C): 4 + 6, the node's are the constructor's zero and the setter; the weighted destructor calls no wait\n");
  }
  return 0;
}
