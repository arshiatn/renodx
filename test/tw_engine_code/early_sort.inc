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
// (The written and found ranges against heapfix's rows: engine_code.hpp's static_assert.)
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

// The game's places the tests name beside engine_code.hpp's (its Build's rvas, DRAW_PASS_RVA, TASK_START_RVA).
constexpr uint32_t SORT_WEIGHTED_ADD_RVA = twattila::engine_code::internal::ATTILA_BUILD.weighted_add_rva;
constexpr uint32_t SORT_WEIGHTED_ADD_END = 0x17F8423;  // after its one return
constexpr uint32_t SORT_NODE_CLEAR_RVA = twattila::engine_code::internal::ATTILA_BUILD.node_clear_rva;
constexpr uint32_t SORT_RIGID_DRAW_UPDATE_RVA = twattila::engine_code::internal::ATTILA_BUILD.rigid_draw_update_rva;
constexpr uint32_t SORT_TASK_WAIT_RVA = twattila::engine_code::internal::ATTILA_BUILD.task_wait_rva;
constexpr uint32_t SORT_TASK_START_THUNK_RVA = 0x16987C0;
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
static_assert(0xB8 + 0x24 == 0xDC);  // the owner is the node + 0xB8: its output's size (owner + 0x24, what the list reset zeroes) is the node + 0xDC the clear guard zeroes

int RunEarlySortTests(const TestBuild& build, const uint8_t* game, uint32_t text_rva, uint32_t text_size) {
  namespace ec = twattila::engine_code::internal;
  const uint8_t* text = game + text_rva;
  if (build.code != &ec::ATTILA_BUILD) {
    std::printf("31. early unit sort: Attila only (skipped; here the weighted add's pattern matches %zu time(s), the node clear's %zu, the pass caller's %zu, the pass thunk's %zu)\n",
                Matches(text, text_size, ec::WEIGHTED_ADD), Matches(text, text_size, ec::NODE_CLEAR), Matches(text, text_size, ec::DRAW_PASS_CALLER),
                Matches(text, text_size, ec::DRAW_PASS_THUNK));
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
  const uint32_t pass_call_rva = build.code->draw_pass_caller_rva + static_cast<uint32_t>(ec::DRAW_PASS_CALL);
  const uint32_t pass_jump_rva = build.code->draw_pass_thunk_rva + static_cast<uint32_t>(ec::DRAW_PASS_JUMP);
  const uint32_t weighted_start_rva = SORT_RIGID_DRAW_UPDATE_RVA + static_cast<uint32_t>(ec::WEIGHTED_START_AT);
  const std::array<std::pair<uint32_t, std::string_view>, 9> patterns = {{{SORT_WEIGHTED_ADD_RVA, ec::WEIGHTED_ADD},
                                                                          {build.code->draw_pass_caller_rva, ec::DRAW_PASS_CALLER},
                                                                          {build.code->draw_pass_thunk_rva, ec::DRAW_PASS_THUNK},
                                                                          {SORT_NODE_CLEAR_RVA, ec::NODE_CLEAR},
                                                                          {ec::DRAW_PASS_RVA + static_cast<uint32_t>(ec::DRAW_PASS_NODE_LOOP_AT), ec::DRAW_PASS_NODE_LOOP},
                                                                          {SORT_RIGID_DRAW_UPDATE_RVA, ec::RIGID_DRAW_UPDATE},
                                                                          {weighted_start_rva, ec::WEIGHTED_START},
                                                                          {ec::TASK_START_RVA, ec::TASK_START},
                                                                          {SORT_TASK_WAIT_RVA, ec::TASK_WAIT}}};
  for (const auto& [rva, pattern] : patterns) {
    const size_t matches = Matches(text, text_size, pattern);
    if (matches != 1 || !holds(rva, pattern)) std::printf("   the pattern of +0x%X: %zu matches in the code, at its RVA %d\n", rva, matches, holds(rva, pattern) ? 1 : 0);
    CHECK(matches == 1 && holds(rva, pattern));
  }
  CHECK(std::memcmp(game + SORT_WEIGHTED_ADD_RVA, "\x83\xEC\x4C\x56\x57", 5) == 0 && std::memcmp(game + pass_call_rva, "\xE8\xC4\xC8\xFC\xFF", 5) == 0
        && std::memcmp(game + pass_jump_rva, "\xE9\x58\x02\x00\x00", 5) == 0 && std::memcmp(game + SORT_NODE_CLEAR_RVA, "\x53\x56\x57\x8B\xF9", 5) == 0);
  {
    const std::vector<RelativeReference> pass = relative(ec::DRAW_PASS_RVA);
    CHECK(pass.size() == 2 && pass[0].rva == pass_jump_rva && pass[0].opcode == 0xE9 && pass[1].rva == pass_call_rva && pass[1].opcode == 0xE8);
    CHECK(AbsoluteReferences(game, ec::DRAW_PASS_RVA).empty());
    const std::vector<uint32_t> draw_update = AbsoluteReferences(game, SORT_RIGID_DRAW_UPDATE_RVA);
    CHECK(relative(SORT_RIGID_DRAW_UPDATE_RVA).empty() && draw_update.size() == 1 && draw_update[0] == SORT_RIGID_VTABLE_RVA + 0x30);
    // The start block's call goes to the thunk, an E9 to the start function.
    const uint32_t thunk = call_target(weighted_start_rva + static_cast<uint32_t>(ec::WEIGHTED_START_CALL));
    CHECK(thunk == SORT_TASK_START_THUNK_RVA && game[thunk] == 0xE9 && call_target(thunk) == ec::TASK_START_RVA);
    std::printf("31. early unit sort: the 4 site patterns and the 5 checks once each at their RVAs; the pass +0x%X has 2 references (E9 at +0x%X, E8 at +0x%X), no relocated address; the Rigid draw "
                "update +0x%X has 1 (vtable +0x%X slot 0x30); the weighted start calls +0x%X, an E9 to +0x%X\n",
                ec::DRAW_PASS_RVA, pass_jump_rva, pass_call_rva, SORT_RIGID_DRAW_UPDATE_RVA, SORT_RIGID_VTABLE_RVA, thunk, ec::TASK_START_RVA);
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
    // Each reference is a weighted class's vtable slot 0x14C (what the handler tells a weighted mesh by); the same vtable holds the mesh clear at 0x12C (the node
    // clear's call) and a sort key function at 0xD8 (what the check calls).
    for (const uint32_t reference : add_references) {
      const uint32_t vtable = reference - static_cast<uint32_t>(ec::MESH_ADD_SLOT);
      CHECK(section_of(reference) == ".rdata" && Read32(game + vtable + 0x12C) == game_base + 0x1832D30 && Read32(game + vtable + ec::MESH_KEY_SLOT) == game_base + 0x183E430);
    }
    std::printf("   the 10 weighted vtables hold the mesh clear at 0x12C and one key function, +0x183E430, at 0xD8\n");
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

// ====================================================================================================================
// 32. Early Unit Sort, the sites, the guards and the handler (engine_code.hpp 22.; Attila only). Copies and fakes: nothing of the game's code runs.
// 32a. As Prepare made it: the four sites, their jumps (the two pass sites into EarlySortPass, the other two into the guards on the stubs' second page), both guards' bytes with
//    the addresses in (the game's five bytes, the jump back, the guard byte, the counts, the task wait), 62 and 66 bytes, ending before that page's 0x90;
// 32b. in and out: only the four sites' bytes differ, the bits above 31 right in `found` and `written`, the mode follows the check field, the game's bytes back;
// 32c. all or none: one byte changed in each of the nine patterns, or a pass site that does not go to the pass: none is found, one line names the four;
// 32d. another mod's bytes in one site going in: none goes in, all four leave `found`; going out: that site stays, the others come out, the module is pinned for a pass site only;
// 32e. the cut: with a thread counted in the handler the sites stay in (the mode already OFF at that window), and after MAX_FAILED_WRITES the module is pinned;
// 32f. the add guard on a fake weighted add (thiscall, 7 stack arguments) with a fake mesh, owner and node, and the clear guard on a fake node clear with a fake task wait:
//    closed, open with the node's flag set and clear, the check; ebx esi edi ebp esp kept; 8 threads x 100 000 adds: the count is the calls when skipped or counted, 0
//    otherwise; ns per call closed against no guard;
// 32g. the handler through a call and through a jump, as the two sites make it, on a fake scene and a fake pass: one call of the pass with the same scene and context, the check's
//    totals (rigid nodes, callback nodes, flags set, the guards' counts, the keys: same, one changed, a mesh removed in the pass, more meshes than the buffer), a nested and a
//    second thread's entry (neither opens or closes the guard), a C++ unwind (closes it), the check's line, mode OFF (nothing but the pass);
// 32h. the stuck pass: a thread parked in the fake pass with the guard open: one Update changes nothing, a pass that came back between two Updates neither, two Updates with
//    none: the guard closed, tripped, mode OFF, the line once, the sites still in;
// 32i. the removal order: the mode is OFF at the window that takes the sites out, and a thread that then enters the handler only calls the pass.
// ====================================================================================================================

std::atomic<int> sort_fake_passes = 0;
std::atomic<uint8_t*> sort_fake_scene = nullptr;
std::atomic<void*> sort_fake_context = nullptr;
std::function<void()> sort_fake_pass_body;  // what the fake pass does besides recording (a nested entry, parking, changing a key, an exception)
void __fastcall SortFakePass(uint8_t* scene, void*, void* context) {
  ++sort_fake_passes;
  sort_fake_scene = scene;
  sort_fake_context = context;
  if (sort_fake_pass_body) sort_fake_pass_body();
}

// The task wait the clear guard calls (thiscall on the task): what the node's output size is at the wait.
std::atomic<int> sort_fake_waits = 0;
std::atomic<uint8_t*> sort_fake_wait_task = nullptr;
std::atomic<uint32_t> sort_fake_wait_size = 0;
uint8_t* sort_fake_wait_node = nullptr;
void __fastcall SortFakeWait(uint8_t* task, void*) {
  ++sort_fake_waits;
  sort_fake_wait_task = task;
  sort_fake_wait_size = Read32(sort_fake_wait_node + 0xDC);
}

// A fake mesh: its vtable, the key its slot 0xD8 answers, the calls of that slot.
struct SortFakeMesh {
  uint32_t vtable = 0;
  uint32_t key = 0;
  uint32_t key_calls = 0;
};
uint32_t __fastcall SortFakeKey(SortFakeMesh* mesh, void*) {
  ++mesh->key_calls;
  return mesh->key;
}

int RunEarlySortSiteTests(const TestBuild& build, uint8_t* patched, uint32_t text_rva, uint32_t text_size) {
  namespace ec = twattila::engine_code::internal;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  const auto rows = [&](bool on, bool check) {
    twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 1.f, 0, {.early_unit_sort = on, .early_unit_sort_check = check});
  };
  const auto lines_with = [&](size_t from, std::string_view text) {
    size_t count = 0;
    for (size_t line = from; line < log_lines.size(); ++line) count += (log_lines[line].find(text) != std::string::npos ? 1 : 0);
    return count;
  };
  if (build.code != &ec::ATTILA_BUILD) {
    const size_t lines_before = log_lines.size();
    rows(false, false);
    CHECK(build.code->weighted_add_rva == 0 && (ec::found & ec::EARLY_SORT_SITES) == 0 && lines_with(lines_before, "Early Unit Sort") == 0);
    std::printf("32. early unit sort: Attila only (this build has no such sites: none found, no line)\n");
    return 0;
  }
  constexpr ec::SiteMask SITES = ec::EARLY_SORT_SITES;
  constexpr size_t FIRST = ec::WEIGHTED_ADD_INDEX;
  constexpr int MAX_TRIES = static_cast<int>(ec::MAX_FAILED_WRITES);
  constexpr std::string_view NAMES = "weighted add guard, draw pass call, draw pass jump, node clear guard";
  rows(false, false);
  CHECK(ec::written == 0 && (ec::found & SITES) == SITES && ec::early_sort_data != nullptr);
  const std::vector<uint8_t> unpatched(patched + text_rva, patched + text_rva + text_size);
  const auto text_unpatched = [&] { return std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0; };
  const std::array<uint32_t, 4> site_rvas = {build.code->weighted_add_rva, build.code->draw_pass_caller_rva + static_cast<uint32_t>(ec::DRAW_PASS_CALL),
                                             build.code->draw_pass_thunk_rva + static_cast<uint32_t>(ec::DRAW_PASS_JUMP), build.code->node_clear_rva};
  const auto prepare = [&] {
    for (ec::Site& site : ec::sites) site.at = nullptr;
    ec::prepared = false;
    ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
  };
  // The two-present rule's memory, the trip and the pin as before any pass.
  const auto fresh_state = [&] {
    ec::EarlySortData* const data = ec::early_sort_data;
    data->inside = 0;
    data->guard = 0;
    data->tripped = false;
    ec::early_sort_busy = false;
    ec::early_sort_stuck = false;
    ec::early_sort_passes = data->passes_done;
    ec::module_pinned = false;
    ec::failed_writes = 0;
  };
  const auto in = [&](bool check) {
    for (int tries = 0; (ec::written & SITES) != SITES && tries < MAX_TRIES; ++tries) rows(true, check);
    return (ec::written & SITES) == SITES;
  };
  // Where the bytes at a site lead (its E8 / E9).
  const auto leads_to = [](const uint8_t* at, const uint8_t* bytes) { return Address(at + ec::SITE_SIZE) + Read32(bytes + 1); };

  // 32a. As Prepare made it.
  {
    const ec::EarlySortData* const data = ec::early_sort_data;
    const uint8_t* const add_stub = ec::page + ec::WEIGHTED_ADD_GUARD_PAGE;
    const uint8_t* const clear_stub = ec::page + ec::NODE_CLEAR_GUARD_PAGE;
    const std::array<uint32_t, 4> targets = {Address(add_stub), Address(reinterpret_cast<const void*>(&ec::EarlySortPass)), Address(reinterpret_cast<const void*>(&ec::EarlySortPass)),
                                             Address(clear_stub)};
    for (size_t index = 0; index < site_rvas.size(); ++index) {
      const ec::Site& site = ec::sites[FIRST + index];
      CHECK(site.at == patched + site_rvas[index] && std::memcmp(site.game.data(), unpatched.data() + (site_rvas[index] - text_rva), ec::SITE_SIZE) == 0);
      CHECK(site.jump[0] == (index == 1 ? 0xE8 : 0xE9) && leads_to(site.at, site.jump.data()) == targets[index]);
    }
    CHECK(reinterpret_cast<const uint8_t*>(ec::draw_pass) == patched + ec::DRAW_PASS_RVA && ec::draw_pass_call_return == patched + site_rvas[1] + ec::SITE_SIZE
          && ec::rigid_draw_update == patched + build.code->rigid_draw_update_rva && ec::weighted_add == patched + site_rvas[0]);
    CHECK(reinterpret_cast<const uint8_t*>(data) == ec::page + ec::STUB_CODE_SIZE + ec::EARLY_SORT_DATA_AT && data->guard == 0 && data->mode.load() == ec::EarlySortMode::OFF);
    // Both guards: the game's five bytes at +9, the jump back to the site + 5, the guard byte twice, the counts, the task wait.
    CHECK(ec::WEIGHTED_ADD_GUARD.size() == 62 && ec::NODE_CLEAR_GUARD.size() == 66 && clear_stub + ec::NODE_CLEAR_GUARD.size() <= ec::page + ec::PAGE_SIZE + 0x90
          && add_stub + ec::WEIGHTED_ADD_GUARD.size() <= clear_stub && add_stub >= ec::worker_cut.end);
    for (const auto& [stub, site] : {std::pair{add_stub, &ec::sites[FIRST]}, std::pair{clear_stub, &ec::sites[FIRST + 3]}}) {
      CHECK(std::memcmp(stub + 9, site->game.data(), ec::SITE_SIZE) == 0 && stub[0xE] == 0xE9 && Address(stub + 0x13) + Read32(stub + 0xF) == Address(site->at + ec::SITE_SIZE));
      CHECK(Read32(stub + 2) == Address(&data->guard) && Read32(stub + 0x15) == Address(&data->guard));
    }
    CHECK(Read32(add_stub + 0x2E) == Address(&data->add_hits) && Read32(add_stub + 0x38) == Address(&data->add_hits) && Read32(clear_stub + 0x3C) == Address(&data->clear_hits));
    CHECK(clear_stub[0x2C] == 0xE8 && Address(clear_stub + 0x31) + Read32(clear_stub + 0x2D) == Address(patched + build.code->task_wait_rva));
    // Outside the addresses the stubs are the listed bytes.
    const auto as_listed = [](const uint8_t* stub, std::span<const uint8_t> code, std::initializer_list<size_t> addresses) {
      for (size_t at = 0; at < code.size(); ++at) {
        const bool address = std::ranges::any_of(addresses, [&](size_t start) { return at >= start && at < start + 4; });
        if (!address && stub[at] != code[at]) return false;
      }
      return true;
    };
    CHECK(as_listed(add_stub, ec::WEIGHTED_ADD_GUARD, {0x02, 0x0F, 0x15, 0x2E, 0x38}) && as_listed(clear_stub, ec::NODE_CLEAR_GUARD, {0x02, 0x0F, 0x15, 0x2D, 0x3C}));
    std::printf("32. early unit sort: the four sites as Prepare made them (the pass's call and jump into the handler, the add's and the clear's heads into their guards on the second page), "
                "the guards 62 and 66 bytes with the addresses in\n");
  }

  // 32b. In and out.
  {
    const size_t lines_before = log_lines.size();
    CHECK(in(false) && ec::written == SITES && (ec::written >> 32) == 0b111 && (ec::written & 0xFFFFFFFFu) == 0x80000000u && (ec::found & SITES) == SITES);
    CHECK(lines_with(lines_before, "[Total War] Early Unit Sort: on") == 1 && log_lines.back().ends_with("Early Unit Sort: on"));
    size_t differing = 0;
    for (uint32_t at = text_rva; at < text_rva + text_size; ++at) {
      if (unpatched[at - text_rva] == patched[at]) continue;
      ++differing;
      CHECK(std::ranges::any_of(site_rvas, [&](uint32_t rva) { return at >= rva && at < rva + ec::SITE_SIZE; }));
    }
    for (size_t index = 0; index < site_rvas.size(); ++index) CHECK(std::memcmp(patched + site_rvas[index], ec::sites[FIRST + index].jump.data(), ec::SITE_SIZE) == 0);
    CHECK(differing >= 14 && patched[site_rvas[1]] == 0xE8 && patched[site_rvas[2]] == 0xE9);
    // The mode follows the check field while the sites are in (the early start itself is not built yet: the row alone leaves the handler at OFF).
    CHECK(ec::early_sort_data->mode.load() == ec::EarlySortMode::OFF && ec::early_sort_keys == nullptr);
    rows(true, true);
    CHECK(ec::early_sort_data->mode.load() == ec::EarlySortMode::CHECK && ec::early_sort_keys != nullptr && ec::written == SITES);
    rows(true, false);
    CHECK(ec::early_sort_data->mode.load() == ec::EarlySortMode::OFF && ec::written == SITES);
    rows(false, true);  // the check alone asks for the sites too
    CHECK(ec::early_sort_data->mode.load() == ec::EarlySortMode::CHECK && ec::written == SITES);
    rows(false, false);
    CHECK(ec::written == 0 && text_unpatched() && ec::early_sort_data->mode.load() == ec::EarlySortMode::OFF);
    CHECK(log_lines.back().ends_with("Early Unit Sort: off, the game's own code back"));
    std::printf("   in and out: %zu bytes differ, all in the four sites; written 0x%llX; the mode follows the check; the game's bytes back\n", differing,
                static_cast<unsigned long long>(SITES));
  }

  // 32c. All or none at Prepare: a byte of each pattern, then each pass site's target.
  {
    const auto left_alone = [&](const char* what) -> int {
      prepare();
      CHECK((ec::found & SITES) == 0);
      for (size_t index = 0; index < site_rvas.size(); ++index) CHECK(ec::sites[FIRST + index].at == nullptr);
      ec::prefetch_logged = 0;
      ec::asked = ~ec::SiteMask{0};
      ec::failed_writes = 0;
      const size_t lines_before = log_lines.size();
      rows(true, false);
      rows(true, false);
      CHECK((ec::written & SITES) == 0 && text_unpatched() == false);  // the changed byte is still there, no site was written
      for (const uint32_t rva : site_rvas) CHECK(patched[rva] != 0xE9 || unpatched[rva - text_rva] == 0xE9);
      CHECK(lines_with(lines_before, "Early Unit Sort: left alone") == 1);
      for (size_t line = lines_before; line < log_lines.size(); ++line) {
        if (log_lines[line].find("Early Unit Sort: left alone") == std::string::npos) continue;
        CHECK(log_lines[line].ends_with(std::string("Early Unit Sort: left alone (not the game's code as expected: another mod or build): ") + std::string(NAMES)));
      }
      rows(false, false);
      std::memcpy(patched + text_rva, unpatched.data(), text_size);
      std::printf("   %s changed: none of the four found, one line names them\n", what);
      return 0;
    };
    const uint32_t draw_update_rva = build.code->rigid_draw_update_rva;
    const std::array<std::pair<uint32_t, const char*>, 9> bytes = {{{site_rvas[0] + 10, "the weighted add's pattern"},
                                                                    {build.code->draw_pass_caller_rva, "the pass caller's pattern"},
                                                                    {build.code->draw_pass_thunk_rva + 2, "the pass thunk's pattern"},
                                                                    {site_rvas[3] + 8, "the node clear's pattern"},
                                                                    {ec::DRAW_PASS_RVA + static_cast<uint32_t>(ec::DRAW_PASS_NODE_LOOP_AT) + 3, "the pass's node loop"},
                                                                    {draw_update_rva + 1, "the rigid draw update's head"},
                                                                    {draw_update_rva + static_cast<uint32_t>(ec::WEIGHTED_START_AT) + 2, "the weighted start block"},
                                                                    {ec::TASK_START_RVA + 7, "the task start function"},
                                                                    {build.code->task_wait_rva + 1, "the task wait"}}};
    for (const auto& [rva, what] : bytes) {
      patched[rva] ^= 1;
      if (left_alone(what) != 0) return 1;
    }
    for (const size_t index : {size_t{1}, size_t{2}}) {
      Write32(patched + site_rvas[index] + 1, Read32(patched + site_rvas[index] + 1) + 0x10);
      if (left_alone(index == 1 ? "the call's target" : "the jump's target") != 0) return 1;
    }
    prepare();
    CHECK((ec::found & SITES) == SITES && text_unpatched());
  }

  // 32d. Another mod's bytes in one site.
  {
    const std::array<uint8_t, ec::SITE_SIZE> hook = {0xE9, 0x10, 0x20, 0x30, 0x40};
    const auto holds = [&](size_t index, const std::array<uint8_t, ec::SITE_SIZE>& bytes) { return std::memcmp(patched + site_rvas[index], bytes.data(), ec::SITE_SIZE) == 0; };
    fresh_state();
    for (size_t foreign = 0; foreign < site_rvas.size(); ++foreign) {
      // Going in: none goes in, all four leave `found` and are named.
      std::memcpy(patched + site_rvas[foreign], hook.data(), hook.size());
      ec::failed_writes = 0;
      size_t lines_before = log_lines.size();
      rows(true, false);
      rows(true, false);
      CHECK((ec::written & SITES) == 0 && (ec::found & SITES) == 0 && ec::early_sort_data->mode.load() == ec::EarlySortMode::OFF);
      for (size_t index = 0; index < site_rvas.size(); ++index) CHECK(holds(index, index == foreign ? hook : ec::sites[FIRST + index].game));
      CHECK(lines_with(lines_before, std::string(ec::sites[FIRST + foreign].name) + ": left alone (its bytes changed since the start: another mod)") == 1
            && lines_with(lines_before, "Early Unit Sort: on") == 0);
      rows(false, false);
      std::memcpy(patched + site_rvas[foreign], ec::sites[FIRST + foreign].game.data(), ec::SITE_SIZE);
      prepare();
      // Going out: that site stays the other mod's, the three others come out; a pass site that stays may still lead into the handler: the module is pinned.
      CHECK(in(true) && !ec::module_pinned);
      std::memcpy(patched + site_rvas[foreign], hook.data(), hook.size());
      ec::failed_writes = 0;
      rows(false, false);
      CHECK(ec::written == 0 && (ec::found & SITES) == (SITES & ~(ec::SiteMask{1} << (FIRST + foreign))) && ec::early_sort_data->mode.load() == ec::EarlySortMode::OFF);
      for (size_t index = 0; index < site_rvas.size(); ++index) CHECK(holds(index, index == foreign ? hook : ec::sites[FIRST + index].game));
      CHECK(ec::module_pinned == (foreign == 1 || foreign == 2));
      // With one out of `found` the three others are not asked for again.
      lines_before = log_lines.size();
      rows(true, false);
      CHECK((ec::written & SITES) == 0 && lines_with(lines_before, "Early Unit Sort: on") == 0);
      rows(false, false);
      std::memcpy(patched + site_rvas[foreign], ec::sites[FIRST + foreign].game.data(), ec::SITE_SIZE);
      prepare();
      fresh_state();
      CHECK((ec::found & SITES) == SITES && text_unpatched());
      std::printf("   another mod's bytes in the %s: going in none of the four goes in; going out it stays, the others come out%s\n", ec::sites[FIRST + foreign].name.data(),
                  (foreign == 1 || foreign == 2 ? ", the module pinned" : ""));
    }
  }

  // 32e. The cut: a thread counted in the handler keeps the sites in; the mode is OFF before the window looks.
  {
    CHECK(in(true) && ec::early_sort_data->mode.load() == ec::EarlySortMode::CHECK);
    ec::early_sort_data->inside = 1;
    ec::control_sort_mode_at_removal = -1;
    const size_t lines_before = log_lines.size();
    rows(false, false);
    CHECK(ec::written == SITES && ec::early_sort_data->mode.load() == ec::EarlySortMode::OFF && ec::control_sort_mode_at_removal == static_cast<int32_t>(ec::EarlySortMode::OFF));
    for (int tries = 1; tries < MAX_TRIES; ++tries) rows(false, false);
    CHECK(ec::written == SITES && ec::failed_writes == ec::MAX_FAILED_WRITES && ec::module_pinned
          && lines_with(lines_before, "Early Unit Sort: not written, a game thread kept running the code") == 1);
    for (size_t index = 0; index < site_rvas.size(); ++index) CHECK(std::memcmp(patched + site_rvas[index], ec::sites[FIRST + index].jump.data(), ec::SITE_SIZE) == 0);
    // (The count stuck over two presents also trips the two-present rule: 32h.)
    CHECK(lines_with(lines_before, "Early Unit Sort: off for this session (the pass did not come back).") == 1);
    fresh_state();
    rows(true, false);   // the rows change: the tries start again
    rows(false, false);
    CHECK(ec::written == 0 && text_unpatched());
    std::printf("   the cut: with a thread counted in the handler the sites stay in (%d tries, then the module pinned); the mode was OFF at the first window; out once it left\n", MAX_TRIES);
  }

  // 32f. The guards on fakes: the production builder, on memory of its own.
  uint8_t* const memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x3000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  CHECK(memory != nullptr);
  {
    uint8_t* const add_site = memory;
    uint8_t* const clear_site = memory + 0x40;
    uint8_t* const plain_add = memory + 0x80;
    uint8_t* const adapter = memory + 0xC0;
    uint8_t* const add_stub = memory + 0x200;
    uint8_t* const clear_stub = memory + 0x240;
    auto* const data = new (memory + 0x1000) ec::EarlySortData;
    auto* const cells = reinterpret_cast<uint32_t*>(memory + 0x1800);
    enum Cell : size_t { ADD_RUNS, ADD_ECX, CLEAR_RUNS, CLEAR_ECX, CLEAR_SIZE, CLEAR_FLAG, ESP_BEFORE, ESP_AFTER };
    // The fake weighted add: the game's five bytes, a count of its runs (locked: 8 threads), ecx, its epilogue and `ret 0x1C`.
    for (uint8_t* const at : {add_site, plain_add}) {
      ei::Assembler fake(std::span<uint8_t>(at, 0x40));
      fake.Bytes({0x83, 0xEC, 0x4C, 0x56, 0x57});
      fake.BytesAndOperand({0xF0, 0xFF, 0x05}, Address(&cells[ADD_RUNS]));
      fake.BytesAndOperand({0x89, 0x0D}, Address(&cells[ADD_ECX]));
      fake.Bytes({0x5F, 0x5E, 0x83, 0xC4, 0x4C, 0xC2, 0x1C, 0x00});
      CHECK(fake.Fits());
    }
    {
      // The fake node clear: the game's five bytes (edi = the node), a count of its runs, ecx, the node's output size and flag as the body finds them, `ret`.
      ei::Assembler fake(std::span<uint8_t>(clear_site, 0x40));
      fake.Bytes({0x53, 0x56, 0x57, 0x8B, 0xF9});
      fake.BytesAndOperand({0xFF, 0x05}, Address(&cells[CLEAR_RUNS]));
      fake.BytesAndOperand({0x89, 0x0D}, Address(&cells[CLEAR_ECX]));
      fake.Bytes({0x8B, 0x87, 0xDC, 0x00, 0x00, 0x00});
      fake.BytesAndOperand({0xA3}, Address(&cells[CLEAR_SIZE]));
      fake.Bytes({0x0F, 0xB6, 0x87, 0x94, 0x0A, 0x00, 0x00});
      fake.BytesAndOperand({0xA3}, Address(&cells[CLEAR_FLAG]));
      fake.Bytes({0x5F, 0x5E, 0x5B, 0xC3});
      CHECK(fake.Fits());
      // The add's caller: seven arguments and the call, esp before and after (put back, so a wrong `ret` shows as a number).
      ei::Assembler caller(std::span<uint8_t>(adapter, 0x40));
      caller.BytesAndOperand({0x89, 0x25}, Address(&cells[ESP_BEFORE]));
      caller.Bytes({0x6A, 7, 0x6A, 6, 0x6A, 5, 0x6A, 4, 0x6A, 3, 0x6A, 2, 0x6A, 1});
      caller.Relative(0xE8, add_site);
      caller.BytesAndOperand({0x89, 0x25}, Address(&cells[ESP_AFTER]));
      caller.BytesAndOperand({0x8B, 0x25}, Address(&cells[ESP_BEFORE]));
      caller.Bytes({0xC2, 0x04, 0x00});
      CHECK(caller.Fits());
    }
    ec::BuildEarlySortGuards(add_stub, clear_stub, data, add_site, clear_site, reinterpret_cast<const uint8_t*>(&SortFakeWait));
    JumpTo(add_site, add_stub);
    JumpTo(clear_site, clear_stub);
    std::vector<uint8_t> node(0xC80);
    std::vector<uint8_t> mesh(0x248);
    uint8_t* const task = memory + 0x2000;
    Write32(mesh.data() + 0x244, Address(node.data() + 0xB8));
    Write32(node.data() + ec::NODE_WEIGHTED_TASK, Address(task));
    Write32(node.data() + 0xD8, 0x11111111);
    Write32(node.data() + 0xE0, 0x22222222);
    sort_fake_wait_node = node.data();
    const auto call = [&](uint8_t* function, uint8_t* self) {
      RegisterCall registers = {.in = {0xA1A1A1A1, Address(self), 0xD1D1D1D1, 0xB1B1B1B1, 0x51515151, 0xD1D1D1D1, 0xB9B9B9B9}, .function = Address(function), .argument = 0x1234};
      CallWithRegisters(&registers);
      return registers;
    };
    const auto kept = [](const RegisterCall& registers) { return std::memcmp(registers.in + 3, registers.out + 3, 4 * sizeof(uint32_t)) == 0; };
    // The add: `guard`, the node's flag; the body's runs and the count this call must add.
    for (const auto& [guard, flag, runs, hits] : std::initializer_list<std::tuple<uint8_t, uint8_t, uint32_t, uint32_t>>{
             {0, 0, 1, 0}, {0, 1, 1, 0}, {ec::SORT_GUARD_OPEN, 1, 0, 1}, {ec::SORT_GUARD_OPEN, 0, 1, 0}, {ec::SORT_GUARD_COUNT, 0, 1, 1}, {ec::SORT_GUARD_COUNT, 1, 1, 1}}) {
      data->guard = guard;
      node[ec::NODE_WEIGHTED_STARTED] = flag;
      data->add_hits = 0;
      cells[ADD_RUNS] = 0;
      cells[ADD_ECX] = 0;
      const RegisterCall registers = call(adapter, mesh.data());
      CHECK(cells[ADD_RUNS] == runs && data->add_hits == hits && (runs == 0 || cells[ADD_ECX] == Address(mesh.data())));
      CHECK(kept(registers) && cells[ESP_AFTER] == cells[ESP_BEFORE] && registers.esp_after == registers.esp_before + 4);  // the stack as after `ret 0x1C`, body or skip
    }
    // 8 threads x 100 000 adds in each state.
    using AddCall = void(__thiscall*)(uint8_t* mesh, int, int, int, int, int, int, int);
    constexpr uint32_t THREADS = 8;
    constexpr uint32_t CALLS = 100000;
    for (const auto& [guard, flag, runs, hits] : std::initializer_list<std::tuple<uint8_t, uint8_t, uint32_t, uint32_t>>{
             {0, 1, THREADS * CALLS, 0}, {ec::SORT_GUARD_OPEN, 1, 0, THREADS * CALLS}, {ec::SORT_GUARD_OPEN, 0, THREADS * CALLS, 0}, {ec::SORT_GUARD_COUNT, 1, THREADS * CALLS, THREADS * CALLS}}) {
      data->guard = guard;
      node[ec::NODE_WEIGHTED_STARTED] = flag;
      data->add_hits = 0;
      cells[ADD_RUNS] = 0;
      std::vector<std::thread> threads;
      for (uint32_t thread = 0; thread < THREADS; ++thread) {
        threads.emplace_back([&] {
          for (uint32_t round = 0; round < CALLS; ++round) reinterpret_cast<AddCall>(add_site)(mesh.data(), 1, 2, 3, 4, 5, 6, 7);
        });
      }
      for (std::thread& thread : threads) thread.join();
      CHECK(cells[ADD_RUNS] == runs && data->add_hits == hits);
    }
    // ns per call, the guard closed against the same function without one.
    data->guard = 0;
    constexpr uint32_t TIMED = 20'000'000;
    std::array<double, 2> ns = {};
    for (size_t which = 0; which < ns.size(); ++which) {
      const auto function = reinterpret_cast<AddCall>(which == 0 ? add_site : plain_add);
      const auto start = std::chrono::steady_clock::now();
      for (uint32_t round = 0; round < TIMED; ++round) function(mesh.data(), 1, 2, 3, 4, 5, 6, 7);
      ns[which] = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / TIMED;
    }
    std::printf("   the add guard: closed the body runs, open it is skipped for a started node only (ret 0x1C, registers kept), the check counts and runs; 8 threads x %u: the counts exact; "
                "closed %.2f ns per call against %.2f without the guard\n",
                CALLS, ns[0], ns[1]);
    // The clear: `guard`, the node's flag; the waits and the count this call must add, the size the body must find.
    for (const auto& [guard, flag, waits, hits, size] : std::initializer_list<std::tuple<uint8_t, uint8_t, int, uint32_t, uint32_t>>{
             {0, 1, 0, 0, 77}, {ec::SORT_GUARD_OPEN, 0, 0, 0, 77}, {ec::SORT_GUARD_OPEN, 1, 1, 1, 0}, {ec::SORT_GUARD_COUNT, 1, 0, 1, 77}, {ec::SORT_GUARD_COUNT, 0, 0, 1, 77}}) {
      data->guard = guard;
      node[ec::NODE_WEIGHTED_STARTED] = flag;
      Write32(node.data() + 0xDC, 77);
      data->clear_hits = 0;
      sort_fake_waits = 0;
      sort_fake_wait_task = nullptr;
      sort_fake_wait_size = 0;
      cells[CLEAR_RUNS] = 0;
      const RegisterCall registers = call(clear_site, node.data());
      CHECK(cells[CLEAR_RUNS] == 1 && cells[CLEAR_ECX] == Address(node.data()) && cells[CLEAR_FLAG] == flag && node[ec::NODE_WEIGHTED_STARTED] == flag);
      CHECK(sort_fake_waits == waits && data->clear_hits == hits && cells[CLEAR_SIZE] == size && Read32(node.data() + 0xDC) == size);
      // The wait came first, with the node's task, and saw the output as the task left it: the size is zeroed after the wait, not before.
      if (waits != 0) CHECK(sort_fake_wait_task == task && sort_fake_wait_size == 77);
      CHECK(Read32(node.data() + 0xD8) == 0x11111111 && Read32(node.data() + 0xE0) == 0x22222222 && Read32(node.data() + ec::NODE_WEIGHTED_TASK) == Address(task));
      CHECK(kept(registers) && registers.esp_after == registers.esp_before);
    }
    std::printf("   the clear guard: closed and for a node not started the body runs alone; open on a started node the task wait first (the node's task), then the output's size zeroed, "
                "ecx the node again, the flag still set; the check counts and runs\n");
  }

  // 32g. The handler on a fake scene and pass, entered as the two sites enter it.
  CHECK(in(true));
  ec::EarlySortData* const data = ec::early_sort_data;
  const uint8_t* const handler = reinterpret_cast<const uint8_t*>(&ec::EarlySortPass);
  CHECK(patched[site_rvas[1]] == 0xE8 && leads_to(patched + site_rvas[1], patched + site_rvas[1]) == Address(handler) && patched[site_rvas[2]] == 0xE9
        && leads_to(patched + site_rvas[2], patched + site_rvas[2]) == Address(handler));
  using PassCall = void(__stdcall*)(uint8_t* scene, void* context);
  PassCall through_call = nullptr;
  PassCall through_jump = nullptr;
  {
    // The call: ecx the scene, the context pushed, the call, as +0x177C1B0 does; the jump: the same frame, then a jump (the thunk's).
    ei::Assembler code(std::span<uint8_t>(memory + 0x400, 0x80));
    through_call = reinterpret_cast<PassCall>(code.Cursor());
    code.Bytes({0x8B, 0x4C, 0x24, 0x04, 0xFF, 0x74, 0x24, 0x08});
    code.Relative(0xE8, handler);
    ec::draw_pass_call_return = code.Cursor();
    code.Bytes({0xC2, 0x08, 0x00});
    through_jump = reinterpret_cast<PassCall>(code.Cursor());
    code.Bytes({0x8B, 0x4C, 0x24, 0x04, 0x8B, 0x04, 0x24, 0x89, 0x44, 0x24, 0x04, 0x83, 0xC4, 0x04});
    code.Relative(0xE9, handler);
    CHECK(code.Fits());
  }
  ec::draw_pass = reinterpret_cast<decltype(ec::draw_pass)>(&SortFakePass);
  // The fake scene: node A rigid (weighted meshes 0 and 1, then one of another class), B rigid with a callback (weighted mesh 2), C rigid with its flag set (no mesh), D of
  // another class (weighted mesh 3: never walked).
  std::array<uint32_t, 0x10> rigid_vtable = {};
  std::array<uint32_t, 0x10> other_node_vtable = {};
  std::array<uint32_t, 0x54> weighted_vtable = {};
  std::array<uint32_t, 0x54> other_mesh_vtable = {};
  rigid_vtable[ec::NODE_DRAW_UPDATE_SLOT / 4] = Address(ec::rigid_draw_update);
  other_node_vtable[ec::NODE_DRAW_UPDATE_SLOT / 4] = Address(ec::rigid_draw_update) + 0x10;
  weighted_vtable[ec::MESH_ADD_SLOT / 4] = Address(ec::weighted_add);
  other_mesh_vtable[ec::MESH_ADD_SLOT / 4] = Address(ec::weighted_add) + 0x10;
  weighted_vtable[ec::MESH_KEY_SLOT / 4] = other_mesh_vtable[ec::MESH_KEY_SLOT / 4] = Address(reinterpret_cast<const void*>(&SortFakeKey));
  std::array<SortFakeMesh, 5> meshes = {};
  for (size_t index = 0; index < meshes.size(); ++index) meshes[index] = {.vtable = Address(index == 4 ? other_mesh_vtable.data() : weighted_vtable.data()), .key = 0x100 + static_cast<uint32_t>(index)};
  const std::array<uint32_t, 3> meshes_a = {Address(&meshes[0]), Address(&meshes[1]), Address(&meshes[4])};
  const std::array<uint32_t, 1> meshes_b = {Address(&meshes[2])};
  const std::array<uint32_t, 1> meshes_d = {Address(&meshes[3])};
  std::array<std::vector<uint8_t>, 4> nodes;
  std::array<uint32_t, 4> node_pointers = {};
  for (size_t index = 0; index < nodes.size(); ++index) {
    nodes[index].assign(0xC80, 0);
    Write32(nodes[index].data(), Address(index == 3 ? other_node_vtable.data() : rigid_vtable.data()));
    Write32(nodes[index].data() + ec::NODE_WEIGHTED_TASK, 0x7A5C0000 + static_cast<uint32_t>(index));
    node_pointers[index] = Address(nodes[index].data());
  }
  const auto set_meshes = [&](size_t node_index, const uint32_t* list, uint32_t count) {
    Write32(nodes[node_index].data() + ec::NODE_MESHES, Address(list));
    Write32(nodes[node_index].data() + ec::NODE_MESH_COUNT, count);
  };
  set_meshes(0, meshes_a.data(), 3);
  set_meshes(1, meshes_b.data(), 1);
  set_meshes(3, meshes_d.data(), 1);
  Write32(nodes[1].data() + ec::NODE_CALLBACK, 0x1234);
  nodes[2][ec::NODE_WEIGHTED_STARTED] = 1;
  std::vector<uint8_t> scene(0x600);
  Write32(scene.data() + ec::SCENE_NODES, Address(node_pointers.data()));
  Write32(scene.data() + ec::SCENE_NODE_COUNT, static_cast<uint32_t>(node_pointers.size()));
  void* const context = memory + 0x2800;
  // What the check has counted so far, and the handler's state between passes.
  struct Totals {
    uint64_t call = 0, jump = 0, rigid = 0, callback = 0, flags = 0, adds = 0, clears = 0, differences = 0, changed = 0, overflow = 0;
    uint32_t done = 0;
  } expected;
  expected.done = data->passes_done;
  const auto as_expected = [&] {
    return data->passes_call == expected.call && data->passes_jump == expected.jump && data->rigid_nodes == expected.rigid && data->callback_nodes == expected.callback
           && data->flags_set == expected.flags && data->adds_inside == expected.adds && data->clears_inside == expected.clears && data->key_differences == expected.differences
           && data->key_meshes_changed == expected.changed && data->key_overflow == expected.overflow && data->passes_done == expected.done && data->inside == 0 && data->guard == 0
           && data->add_hits == 0 && data->clear_hits == 0;
  };
  // One pass of the fake scene in check mode adds these.
  const auto one_pass = [&](bool call) {
    ++(call ? expected.call : expected.jump);
    expected.rigid += 3;
    ++expected.callback;
    ++expected.flags;
    ++expected.done;
  };
  std::atomic<uint8_t> guard_in_pass = 0xFF;
  std::atomic<int32_t> inside_in_pass = -1;
  const auto record = [&] {
    guard_in_pass = data->guard.load();
    inside_in_pass = data->inside.load();
  };
  fresh_state();
  CHECK(data->mode.load() == ec::EarlySortMode::CHECK && as_expected());
  // Through the call: one call of the pass, the same scene and context; the guard at 2 in the pass, closed after; the keys read before and after, all the same.
  sort_fake_passes = 0;
  sort_fake_pass_body = record;
  through_call(scene.data(), context);
  one_pass(true);
  CHECK(sort_fake_passes == 1 && sort_fake_scene == scene.data() && sort_fake_context == context && guard_in_pass == ec::SORT_GUARD_COUNT && inside_in_pass == 1 && as_expected());
  CHECK(meshes[0].key_calls == 2 && meshes[1].key_calls == 2 && meshes[2].key_calls == 2 && meshes[3].key_calls == 0 && meshes[4].key_calls == 0);
  // Through the jump, many times (a frame left wrong would walk the stack off): the same.
  for (int round = 0; round < 1000; ++round) {
    through_jump(scene.data(), context);
    one_pass(false);
  }
  CHECK(sort_fake_passes == 1001 && as_expected());
  for (int round = 0; round < 1000; ++round) {
    through_call(scene.data(), context);
    one_pass(true);
  }
  CHECK(sort_fake_passes == 2001 && as_expected());
  // The guards' counts and a key changed inside the pass.
  sort_fake_pass_body = [&] {
    data->add_hits += 2;
    data->clear_hits += 1;
    meshes[1].key ^= 0x8000;
  };
  through_jump(scene.data(), context);
  one_pass(false);
  expected.adds += 2;
  ++expected.clears;
  ++expected.differences;
  CHECK(as_expected());
  // A mesh taken out of its node inside the pass: not called again, counted as changed.
  sort_fake_pass_body = [&] { set_meshes(0, meshes_a.data(), 1); };
  const uint32_t calls_before = meshes[1].key_calls;
  through_call(scene.data(), context);
  one_pass(true);
  ++expected.changed;
  CHECK(as_expected() && meshes[1].key_calls == calls_before + 1);
  // A mesh that came in inside the pass: counted as changed too, not called.
  sort_fake_pass_body = [&] { set_meshes(0, meshes_a.data(), 3); };
  through_call(scene.data(), context);
  one_pass(true);
  ++expected.changed;
  CHECK(as_expected() && meshes[1].key_calls == calls_before + 1);
  // A nested entry: the pass again, nothing else; the outer entry closes the guard.
  bool nested = false;
  sort_fake_pass_body = [&] {
    if (nested) {
      record();
      return;
    }
    nested = true;
    through_call(scene.data(), context);
  };
  sort_fake_passes = 0;
  through_jump(scene.data(), context);
  one_pass(false);
  CHECK(sort_fake_passes == 2 && guard_in_pass == ec::SORT_GUARD_COUNT && inside_in_pass == 2 && as_expected());
  // A second thread's entry while the first is in the pass: it calls the pass and leaves the guard as it is.
  {
    std::atomic<bool> parked = false;
    std::atomic<bool> release = false;
    const DWORD first_thread = GetCurrentThreadId();
    sort_fake_pass_body = [&] {
      if (GetCurrentThreadId() == first_thread) return;
      parked = true;
      while (!release.load()) Sleep(1);
    };
    sort_fake_passes = 0;
    std::thread other([&] { through_call(scene.data(), context); });
    while (!parked.load()) Sleep(1);
    through_jump(scene.data(), context);
    CHECK(sort_fake_passes == 2 && data->guard == ec::SORT_GUARD_COUNT && data->inside == 1 && data->passes_done == expected.done);
    release = true;
    other.join();
    one_pass(true);
    CHECK(as_expected());
  }
  // A C++ exception through the pass: the guard closed, the count back, the pass counted.
  sort_fake_pass_body = [] { throw 7; };
  int caught = 0;
  try {
    through_call(scene.data(), context);
  } catch (int value) {
    caught = value;
  }
  one_pass(true);
  CHECK(caught == 7 && as_expected());
  // More weighted meshes than the buffer holds: the rest counted as not compared, none as changed.
  {
    constexpr uint32_t MANY = static_cast<uint32_t>(ec::EARLY_SORT_KEYS) + 8;
    std::vector<SortFakeMesh> many(MANY);
    std::vector<uint32_t> many_pointers(MANY);
    for (uint32_t index = 0; index < MANY; ++index) {
      many[index] = {.vtable = Address(weighted_vtable.data()), .key = index};
      many_pointers[index] = Address(&many[index]);
    }
    set_meshes(2, many_pointers.data(), MANY);
    sort_fake_pass_body = nullptr;
    through_call(scene.data(), context);
    one_pass(true);
    expected.overflow += 8 + 3;  // node A's and B's three come first in the walk and fill the buffer with the first of these
    CHECK(as_expected());
    set_meshes(2, nullptr, 0);
  }
  // The check's line.
  {
    const size_t lines_before = log_lines.size();
    twattila::engine_code::LogEarlySortCheck();
    CHECK(log_lines.size() == lines_before + 1
          && log_lines.back().ends_with(std::format(
              "[Total War] Early Unit Sort check: passes {} (call {}, jump {}), rigid nodes {}, with a callback {}, start flags already set {}, weighted adds inside the pass {}, "
              "node clears inside the pass {}, key differences {} (meshes changed {}, not compared {}).",
              expected.call + expected.jump, expected.call, expected.jump, expected.rigid, expected.callback, expected.flags, expected.adds, expected.clears, expected.differences,
              expected.changed, expected.overflow)));
    std::printf("   the handler in check mode, through the call and the jump: \"%s\"\n", log_lines.back().c_str());
  }
  // Mode OFF (the row without the check): the pass and nothing else.
  rows(true, false);
  CHECK(data->mode.load() == ec::EarlySortMode::OFF && ec::written == SITES);
  sort_fake_passes = 0;
  sort_fake_pass_body = record;
  const uint32_t key_calls_before = meshes[0].key_calls;
  through_call(scene.data(), context);
  ++expected.done;
  CHECK(sort_fake_passes == 1 && guard_in_pass == 0 && inside_in_pass == 1 && as_expected() && meshes[0].key_calls == key_calls_before);

  // 32h. The stuck pass.
  {
    rows(true, true);
    fresh_state();
    CHECK(data->mode.load() == ec::EarlySortMode::CHECK);
    std::atomic<int> parked = 0;
    std::atomic<int> release = 0;
    sort_fake_pass_body = [&] {
      const int mine = ++parked;
      while (release.load() < mine) Sleep(1);
    };
    const size_t lines_before = log_lines.size();
    const auto stuck_lines = [&] { return lines_with(lines_before, "[Total War] Early Unit Sort: off for this session (the pass did not come back)."); };
    std::thread first([&] { through_call(scene.data(), context); });
    while (parked.load() < 1) Sleep(1);
    rows(true, true);  // one Update: nothing changes
    CHECK(data->guard == ec::SORT_GUARD_COUNT && !data->tripped && data->mode.load() == ec::EarlySortMode::CHECK && stuck_lines() == 0);
    release = 1;
    first.join();
    one_pass(true);
    std::thread second([&] { through_call(scene.data(), context); });
    while (parked.load() < 2) Sleep(1);
    rows(true, true);  // busy at two presents, but a pass came back between them: nothing changes
    CHECK(data->guard == ec::SORT_GUARD_COUNT && !data->tripped && data->mode.load() == ec::EarlySortMode::CHECK && stuck_lines() == 0);
    rows(true, true);  // and now none did
    CHECK(data->guard == 0 && data->tripped && data->mode.load() == ec::EarlySortMode::OFF && stuck_lines() == 1 && ec::written == SITES);
    rows(true, true);
    rows(true, true);
    CHECK(data->guard == 0 && data->mode.load() == ec::EarlySortMode::OFF && stuck_lines() == 1 && ec::written == SITES);
    release = 2;
    second.join();
    one_pass(true);
    CHECK(data->inside == 0 && data->guard == 0);
    rows(true, true);  // off for the session: the check does not come back
    CHECK(data->mode.load() == ec::EarlySortMode::OFF);
    fresh_state();
    rows(true, true);
    CHECK(data->mode.load() == ec::EarlySortMode::CHECK && as_expected());
    std::printf("   the stuck pass: one Update, or two with a pass back between them: nothing; two with none: the guard closed, tripped, mode OFF, the line once, the sites in\n");
  }

  // 32i. The removal order: OFF is stored before the window that takes the sites out, and a thread that was at the handler's entry then only calls the pass.
  {
    ec::control_sort_mode_at_removal = -1;
    rows(false, false);
    CHECK(ec::written == 0 && text_unpatched() && ec::control_sort_mode_at_removal == static_cast<int32_t>(ec::EarlySortMode::OFF));
    sort_fake_passes = 0;
    sort_fake_pass_body = record;
    through_jump(scene.data(), context);
    ++expected.done;
    CHECK(sort_fake_passes == 1 && guard_in_pass == 0 && as_expected());
    std::printf("   the removal: the mode was OFF at the window that took the sites out; an entry after it called the pass once and opened nothing\n");
  }
  sort_fake_pass_body = nullptr;
  prepare();  // the game's pass and the call's return address again
  fresh_state();
  CHECK((ec::found & SITES) == SITES && ec::written == 0 && text_unpatched());
  VirtualFree(memory, 0, MEM_RELEASE);
  return 0;
}
