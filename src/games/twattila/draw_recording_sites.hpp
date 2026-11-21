/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// Threaded Draw Recording's sites in empire.retail.dll (Steam 1.6.0, x86; private build: compiled only with
// TWATTILA_DRAW_RECORDING, included from the middle of engine_code.hpp's internal namespace like engine_probe.hpp,
// after engine_code's own `Site` and table). draw_recording.hpp owns the types the thunks use (Shared, Frame, Slot,
// Record, the dispatchers); this file owns the table of sites, the thunks and their checks.
//
// What the thunks do. The game's draw loops run on copies of their tracker (a Slot of draw_recording.hpp's arena) while
// the feature records them. Each site is a 5-byte call or jump into a thunk of the code page. A thunk is DIRECT when
// the tracker is not in the arena (`tracker - Shared::arena >= Shared::arena_size`, unsigned): it runs the game's own
// instructions or tail jumps to the game's call, so a game thread never runs add-on code. Otherwise it builds the Frame
// (pushfd, pushad, xmm0, the site id) and calls `Shared::record`, the one `Record(Slot*, Frame*)`, then leaves as the
// game's call would have:
//   the flush (10 arguments, `ret 0x28`), the shared-state virtual call (2 arguments, `ret 8`, eax = 0), the animation
//   call (1 argument, `ret 4`). The loop's own state writes are made by the loop itself on the copy.
// The colour loop has more of them: the mesh setup (a site of 5 bytes inside the loop: the record path tells the loop
// whether to skip the mesh), the two constant writes, the two shader resource calls and the faction colours block (the
// game's own code on the record path is jumped over to the block's end: Record keeps what the loop reads of it and the
// replay does the calls).
// Group A's flush is one hook on the flush's entry (every caller in the game passes through it: the slot is `&any -
// 0x69`, the 4th argument); the 7 call-site flush sites of the two loops are its fallback, never written together with
// the hook. Everything is found by exact bytes at the Steam RVAs: any mismatch leaves the site (or, for the shared
// pieces, the whole feature) out and is logged once. None of these bytes belongs to the heapfix patches (loopfetch,
// batchfix) or to engine_code's own 25 sites; passtime's dev hooks start with an E9 at the loops' and the flush's
// entries: Prepare and every turn-on refuse them.

enum class RecordingSite : uint8_t {
  FLUSH_ENTRY,
  DEPTH_CUT,
  DEPTH_INIT,
  DEPTH_VF68,
  DEPTH_ANIM,
  DEPTH_FLUSH_BOUNDARY,
  DEPTH_FLUSH_END,
  DEPTH_FLUSH_ANIM,
  COLOUR_FLUSH_BOUNDARY,
  COLOUR_FLUSH_END,
  COLOUR_FLUSH_VARIATION,
  COLOUR_FLUSH_CHANGE,
  COLOUR_CUT,
  COLOUR_INIT,
  COLOUR_SETUP,
  COLOUR_VF68_SETUP,
  COLOUR_VF68_VARIATION,
  COLOUR_ANIM,
  COLOUR_CONST1,
  COLOUR_CONST2,
  COLOUR_SRV_DIFFUSE,
  COLOUR_SRV_NORMAL,
  COLOUR_FACTION,
  COUNT,
};
constexpr size_t RECORDING_SITE_COUNT = static_cast<size_t>(RecordingSite::COUNT);
static_assert(RECORDING_SITE_COUNT <= 64);

constexpr uint64_t RecordingBit(RecordingSite site) { return uint64_t{1} << static_cast<size_t>(site); }

// How a site takes part: the loop's own sites (cut, state, shared-state call, animation) go in with the group; the
// entry hook is every group's flush; the call-site flushes are one group's fallback for it.
enum class RecordingKind : uint8_t { OWN, ENTRY, FALLBACK };

constexpr uint32_t FLUSH_RVA = 0x1778390;
constexpr std::string_view FLUSH_HEAD = "83 EC 1C 83 7C 24 30 00 53 55 56 8B 74 24 4C 8B D9 57 0F 84 ?? ?? ?? ??";
constexpr uint32_t FLUSH_EMPTY_RVA = 0x1778527;  // where `je` goes for a count of 0: the pops and `ret 0x28`
constexpr std::string_view FLUSH_EMPTY = "5F 5E 5D 5B 83 C4 1C C2 28 00";
constexpr size_t FLUSH_EMPTY_JUMP = 18;  // the `je` in FLUSH_HEAD

constexpr uint32_t DEPTH_LOOP_RVA = 0x17b17f0;
constexpr std::string_view DEPTH_LOOP_HEAD =
    "83 EC 08 55 8B 6C 24 14 56 8B 74 24 14 89 4C 24 08 83 7D 04 00 0F 84 3B 01 00 00 8B 0D ?? ?? ?? ?? 53 "
    "C7 44 24 18 FF FF FF FF E8 ?? ?? ?? ?? 8B 5C 24 20 05 50 29 00 00";
// mov dword ptr [esp + 0x18], -1 (8 bytes): the animation index starts the call at -1
constexpr uint32_t DEPTH_INIT_OFFSET = 34;
constexpr uint32_t DEPTH_INIT_SIZE = 8;
constexpr uint32_t DEPTH_CUT_RVA = 0x17ae849;
constexpr std::string_view DEPTH_CUT_CONTEXT =
    "8B 4C 24 24 55 53 FF B4 24 B4 00 00 00 C7 47 58 00 00 00 00 57 E8 ?? ?? ?? ?? E9 C2 00 00 00";
constexpr uint32_t DEPTH_CUT_OFFSET = 21;
constexpr uint32_t DEPTH_VF68_RVA = 0x17b1968;
constexpr std::string_view DEPTH_VF68_CONTEXT =
    "8B 07 8B CF FF 50 54 8B C8 8B 10 FF 52 14 89 46 50 8B CF 8B 07 6A 00 FF 76 1C 8B 40 68 FF D0 08 46 69 8B 56 74";
constexpr uint32_t DEPTH_VF68_OFFSET = 26;  // mov eax, dword ptr [eax + 0x68]; call eax
constexpr uint32_t DEPTH_ANIM_RVA = 0x17b19d3;
constexpr std::string_view DEPTH_ANIM_CONTEXT =
    "8B 4C 24 10 C7 46 78 08 00 00 00 8B 45 5C 50 89 44 24 20 E8 "
    "?? ?? ?? ?? 8B 4C 24 14 50 E8 ?? ?? ?? ?? 8B 4E 58 85 C9";
constexpr uint32_t DEPTH_ANIM_OFFSET = 29;
constexpr uint32_t ANIM_CALL_RVA = 0x18832d0;
constexpr std::string_view ANIM_CALL_HEAD = "83 EC 08 56 8B 74 24 10 57 8B F9 56 8D 8F 4C 03 00 00 E8 ?? ?? ?? ??";

// The flush's 7 call sites: the same 10 arguments each.
constexpr uint32_t DEPTH_FLUSH_BOUNDARY_RVA = 0x17b1889;
constexpr std::string_view DEPTH_FLUSH_BOUNDARY_CONTEXT =
    "FF 76 24 8D 46 69 51 8B 4E 70 50 FF 76 74 FF 76 1C FF 76 18 "
    "E8 ?? ?? ?? ?? C7 46 58 00 00 00 00 C7 46 78 00 00 00 00";
constexpr uint32_t DEPTH_FLUSH_END_RVA = 0x17b1933;
constexpr std::string_view DEPTH_FLUSH_END_CONTEXT =
    "FF 76 24 8D 46 69 51 8B 4E 6C 50 FF 76 74 FF 76 1C FF 76 18 "
    "E8 ?? ?? ?? ?? C7 46 58 00 00 00 00 C7 46 78 00 00 00 00 "
    "5E 5D 83 C4 08 C2 10 00";
constexpr uint32_t DEPTH_FLUSH_ANIM_RVA = 0x17b19aa;
constexpr std::string_view DEPTH_FLUSH_ANIM_CONTEXT =
    "FF 76 24 8D 46 69 51 8B 4E 6C 50 52 FF 76 1C FF 76 18 E8 ?? ?? ?? ?? C7 46 58 00 00 00 00 8B 4C 24 10";
constexpr uint32_t COLOUR_FLUSH_BOUNDARY_RVA = 0x17b04a5;
constexpr std::string_view COLOUR_FLUSH_BOUNDARY_CONTEXT =
    "FF 76 24 8D 46 69 51 50 FF 76 74 8B CF FF 76 1C FF 76 18 E8 "
    "?? ?? ?? ?? 8A 44 24 0C C7 46 58 00 00 00 00 C7 46 78 00 00 00 00";
constexpr uint32_t COLOUR_FLUSH_END_RVA = 0x17b054d;
constexpr std::string_view COLOUR_FLUSH_END_CONTEXT =
    "FF 76 24 8D 46 69 51 8B 4E 6C 50 FF 76 74 FF 76 1C FF 76 18 "
    "E8 ?? ?? ?? ?? C7 46 58 00 00 00 00 C7 46 78 00 00 00 00 "
    "5F 5E 5B 8B E5 5D C2 14 00";
constexpr uint32_t COLOUR_FLUSH_VARIATION_RVA = 0x17b05f1;
constexpr std::string_view COLOUR_FLUSH_VARIATION_CONTEXT =
    "FF 76 24 8D 56 69 51 8B 4E 70 52 FF 76 74 FF 76 1C FF 76 18 "
    "E8 ?? ?? ?? ?? C7 46 58 00 00 00 00 C7 46 78 00 00 00 00 "
    "3B 7C 24 1C";
constexpr uint32_t COLOUR_FLUSH_CHANGE_RVA = 0x17b0736;
constexpr std::string_view COLOUR_FLUSH_CHANGE_CONTEXT =
    "FF 76 24 8D 46 69 51 8B 4E 6C 50 FF 76 74 FF 76 1C FF 76 18 "
    "E8 ?? ?? ?? ?? C7 46 58 00 00 00 00 89 7E 78 C6 46 69 01";

// The colour loop's own sites (esi is the tracker everywhere in it). The loop's head ends at the call that fetches the
// animation state's owner; the cut is the driver's call of the loop with its 5 stack arguments.
constexpr uint32_t COLOUR_LOOP_RVA = 0x17b0400;
constexpr std::string_view COLOUR_LOOP_HEAD =
    "55 8B EC 83 E4 F8 81 EC 94 00 00 00 53 56 8B 75 08 57 8B 7D 0C 89 4C 24 28 83 7F 04 00 0F 84 ?? ?? ?? ?? "
    "8B 0D ?? ?? ?? ?? 83 C8 FF 89 44 24 24 E8 ?? ?? ?? ??";
// or eax, -1; mov dword ptr [esp + 0x24], eax (7 bytes): the animation index starts the call at -1
constexpr uint32_t COLOUR_INIT_OFFSET = 41;
constexpr uint32_t COLOUR_INIT_SIZE = 7;
constexpr uint32_t COLOUR_CUT_RVA = 0x17ae910;
constexpr std::string_view COLOUR_CUT_CONTEXT =
    "8B 4C 24 24 55 53 FF B4 24 AC 00 00 00 C7 47 58 00 00 00 00 FF B4 24 B8 00 00 00 57 E8 ?? ?? ?? ?? "
    "8B 54 24 10 8B AC 24 B4 00 00 00";
constexpr uint32_t COLOUR_CUT_OFFSET = 28;
// mov eax, [esi + 0x20]; mov ecx, ebx (5 bytes): the mesh is set up (ebx); eax 1 from Record skips it.
constexpr uint32_t COLOUR_SETUP_RVA = 0x17b04ce;
constexpr std::string_view COLOUR_SETUP_CONTEXT =
    "84 C0 0F 84 ?? ?? ?? ?? 8B 46 20 8B CB 89 43 40 8A 43 5C 88 46 68 8B 03 FF 50 58";
constexpr std::string_view COLOUR_SETUP_EXIT = "83 7E 74 00 0F 84 ?? ?? ?? ??";
constexpr uint32_t COLOUR_SETUP_EXIT_RVA = 0x17b058b;
constexpr uint32_t COLOUR_VF68_SETUP_RVA = 0x17b0583;
constexpr std::string_view COLOUR_VF68_SETUP_CONTEXT =
    "89 46 50 8B CB 8B 03 6A 00 FF 76 1C 8B 40 68 FF D0 08 46 69 83 7E 74 00";
constexpr uint32_t COLOUR_VF68_VARIATION_RVA = 0x17b0633;
constexpr std::string_view COLOUR_VF68_VARIATION_CONTEXT =
    "8B 4E 70 89 46 74 6A 00 FF 76 1C 8B 01 8B 40 68 FF D0 08 46 69 8B 7C 24 10";
constexpr uint32_t COLOUR_ANIM_RVA = 0x17b076d;
constexpr std::string_view COLOUR_ANIM_CONTEXT =
    "8B 4C 24 14 50 E8 ?? ?? ?? ?? 80 7C 24 0C 00 74 31 8B 4C 24 14 81 C1 AC 02 00 00";
// cmp byte ptr [the cache flag], 0 (7 bytes) in front of each constant write; the record path goes on after the write.
constexpr uint32_t COLOUR_CONST1_RVA = 0x17b0787;
constexpr std::string_view COLOUR_CONST1_CONTEXT =
    "8B 4C 24 14 81 C1 AC 02 00 00 C6 46 10 00 80 3D ?? ?? ?? ?? 00 8B 51 14 74 0C 0F 57 C0";
constexpr std::string_view COLOUR_CONST1_EXIT = "0F 57 C0 83 7B 10 71";
constexpr uint32_t COLOUR_CONST1_EXIT_RVA = 0x17b07aa;
constexpr uint32_t COLOUR_CONST2_RVA = 0x17b07bb;
constexpr std::string_view COLOUR_CONST2_CONTEXT =
    "F3 0F 10 05 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 8B 5C 24 14 8B 93 E8 02 00 00 8D 8B D4 02 00 00 74 09";
constexpr std::string_view COLOUR_CONST2_EXIT = "80 7C 24 0D 00 74 20";
constexpr uint32_t COLOUR_CONST2_EXIT_RVA = 0x17b07e6;
// The shader resource slots' texture lookups (a call of a 2-instruction getter each).
constexpr uint32_t TEXTURE_VIEW_RVA = 0xd5c20;
constexpr std::string_view TEXTURE_VIEW_HEAD = "8B 41 04 C3";
constexpr uint32_t COLOUR_SRV_DIFFUSE_RVA = 0x17b07f4;
constexpr std::string_view COLOUR_SRV_DIFFUSE_CONTEXT =
    "8B 4C 24 1C 89 4E 04 E8 ?? ?? ?? ?? 8D 8B F4 41 00 00 85 C0 74 03 50 EB 02 6A 00 E8 ?? ?? ?? ?? 80 7C 24 0E 00";
constexpr std::string_view COLOUR_SRV_DIFFUSE_EXIT = "80 7C 24 0E 00 74 20";
constexpr uint32_t COLOUR_SRV_DIFFUSE_EXIT_RVA = 0x17b080d;
constexpr uint32_t COLOUR_SRV_NORMAL_RVA = 0x17b081b;
constexpr std::string_view COLOUR_SRV_NORMAL_CONTEXT =
    "8B 4C 24 2C 89 4E 08 E8 ?? ?? ?? ?? 8D 8B 1C 42 00 00 85 C0 74 03 50 EB 02 6A 00 E8 ?? ?? ?? ?? 80 7C 24 0F 00";
constexpr std::string_view COLOUR_SRV_NORMAL_EXIT = "80 7C 24 0F 00 0F 84 ?? ?? ?? ??";
constexpr uint32_t COLOUR_SRV_NORMAL_EXIT_RVA = 0x17b0834;
// mov ecx, [esp + 0x30]; mov [esi], ecx (6 bytes): the faction goes into the tracker in both paths, then the block.
constexpr uint32_t COLOUR_FACTION_RVA = 0x17b083f;
constexpr std::string_view COLOUR_FACTION_CONTEXT =
    "80 7C 24 0F 00 0F 84 ?? ?? ?? ?? 8B 4C 24 30 89 0E 8B 47 54 85 C0 74 05 8B 40 04 EB 03 83 C8 FF 89 46 0C 83 F9 FF";
constexpr std::string_view COLOUR_FACTION_EXIT = "8B 4E 58 85 C9 75 20";
constexpr uint32_t COLOUR_FACTION_EXIT_RVA = 0x17b09b2;

// What the colour replays call and read (draw_recording::GameCalls), found beside the sites: the dirty call after a constant
// write, the shader resource call, the faction colours call and the faction block's default value (the operand of its
// movss).
struct ColourPieceInfo {
  std::string_view name;
  uint32_t rva;
  std::string_view context;
  uint32_t context_offset;
  // The call's target and its first bytes; 0 for the default (a movss, whose absolute operand follows its 4 opcode bytes).
  uint32_t callee_rva;
  std::string_view callee_head;
};
enum class ColourPiece : uint8_t { DIRTY_CALL, SHADER_RESOURCE_CALL, FACTION_CALL, FACTION_DEFAULT, COUNT };
constexpr std::array<ColourPieceInfo, static_cast<size_t>(ColourPiece::COUNT)> COLOUR_PIECES = {{
    {.name = "colour loop dirty call",
     .rva = 0x17b07a5,
     .context = "C7 02 00 00 00 00 E8 ?? ?? ?? ?? 0F 57 C0 83 7B 10 71",
     .context_offset = 6,
     .callee_rva = 0x191d960,
     .callee_head = "8B 41 0C FF 41 10 FF 40 24 C3"},
    {.name = "colour loop shader resource call",
     .rva = 0x17b0808,
     .context = "74 03 50 EB 02 6A 00 E8 ?? ?? ?? ?? 80 7C 24 0E 00",
     .context_offset = 7,
     .callee_rva = 0x18d2b60,
     .callee_head = "56 8B F1 57 8B 7C 24 0C 89 7E 18 85 FF 0F 84 ?? ?? ?? ??"},
    {.name = "colour loop faction call",
     .rva = 0x17b0912,
     .context = "0F 14 C1 50 66 0F D6 44 24 68 E8 ?? ?? ?? ?? E9 96 00 00 00",
     .context_offset = 10,
     .callee_rva = 0x1885350,
     .callee_head = "83 EC 18 53 8B 59 50 55 8D 69 2C 8B 4D 14 56 57 8B 7C 24 2C"},
    {.name = "colour loop faction default",
     .rva = 0x17b087f,
     .context = "F3 0F 10 0D ?? ?? ?? ?? 8B 4C 24 14 0F 28 C1 0F 14 C1"},
}};

// The B groups' sorted list accessor and the do-nothing virtual call (thiscall `mov ecx, [ecx + 0xc]; lea eax, [ecx + eax *
// 4]`, `xor al, al`).
constexpr uint32_t ACCESSOR_RVA = 0x1719160;
constexpr std::string_view ACCESSOR_HEAD = "8B 49 0C 8B 44 24 04 8D 04 81 C2 04 00";
constexpr std::string_view VF64_STUB_HEAD = "32 C0 C2 0C 00";

struct RecordingSiteInfo {
  std::string_view name;
  draw_recording::Group group;
  RecordingKind kind;
  uint32_t rva;
  // The bytes around the site, as the game has them, and where the site is in them.
  std::string_view context;
  uint32_t context_offset;
  uint8_t opcode;
  // The bytes of the game's instruction(s) the site replaces: the thunk goes on after them (the 5 bytes written are the
  // first of them).
  uint32_t covered;
  // A call site's target, and its first bytes.
  uint32_t callee_rva;
  std::string_view callee_head;
  // Where the record path goes on instead of the game's code the site stands for (the colour sites that skip a block), and
  // its first bytes.
  uint32_t exit_rva;
  std::string_view exit_head;
  // The id Record gets for it.
  draw_recording::Site record_site;
};

constexpr std::array<RecordingSiteInfo, RECORDING_SITE_COUNT> RECORDING_INFO = {{
    {.name = "flush entry",
     .group = draw_recording::Group::SHARED,
     .kind = RecordingKind::ENTRY,
     .rva = FLUSH_RVA,
     .context = FLUSH_HEAD,
     .opcode = 0xE9,
     .covered = 8,
     .record_site = draw_recording::Site::FLUSH_ENTRY},
    {.name = "depth loop cut",
     .group = draw_recording::Group::DEPTH_A,
     .kind = RecordingKind::OWN,
     .rva = DEPTH_CUT_RVA,
     .context = DEPTH_CUT_CONTEXT,
     .context_offset = DEPTH_CUT_OFFSET,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = DEPTH_LOOP_RVA,
     .callee_head = DEPTH_LOOP_HEAD,
     .record_site = draw_recording::Site::COUNT},
    {.name = "depth loop animation start",
     .group = draw_recording::Group::DEPTH_A,
     .kind = RecordingKind::OWN,
     .rva = DEPTH_LOOP_RVA + DEPTH_INIT_OFFSET,
     .context = DEPTH_LOOP_HEAD,
     .context_offset = DEPTH_INIT_OFFSET,
     .opcode = 0xE9,
     .covered = DEPTH_INIT_SIZE,
     .record_site = draw_recording::Site::COUNT},
    {.name = "depth loop shared-state call",
     .group = draw_recording::Group::DEPTH_A,
     .kind = RecordingKind::OWN,
     .rva = DEPTH_VF68_RVA,
     .context = DEPTH_VF68_CONTEXT,
     .context_offset = DEPTH_VF68_OFFSET,
     .opcode = 0xE8,
     .covered = 5,
     .record_site = draw_recording::Site::DEPTH_VF68},
    {.name = "depth loop animation call",
     .group = draw_recording::Group::DEPTH_A,
     .kind = RecordingKind::OWN,
     .rva = DEPTH_ANIM_RVA,
     .context = DEPTH_ANIM_CONTEXT,
     .context_offset = DEPTH_ANIM_OFFSET,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = ANIM_CALL_RVA,
     .callee_head = ANIM_CALL_HEAD,
     .record_site = draw_recording::Site::DEPTH_ANIM},
    {.name = "depth loop boundary flush",
     .group = draw_recording::Group::DEPTH_A,
     .kind = RecordingKind::FALLBACK,
     .rva = DEPTH_FLUSH_BOUNDARY_RVA,
     .context = DEPTH_FLUSH_BOUNDARY_CONTEXT,
     .context_offset = 20,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = FLUSH_RVA,
     .callee_head = FLUSH_HEAD,
     .record_site = draw_recording::Site::DEPTH_FLUSH_BOUNDARY},
    {.name = "depth loop end flush",
     .group = draw_recording::Group::DEPTH_A,
     .kind = RecordingKind::FALLBACK,
     .rva = DEPTH_FLUSH_END_RVA,
     .context = DEPTH_FLUSH_END_CONTEXT,
     .context_offset = 20,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = FLUSH_RVA,
     .callee_head = FLUSH_HEAD,
     .record_site = draw_recording::Site::DEPTH_FLUSH_END},
    {.name = "depth loop animation flush",
     .group = draw_recording::Group::DEPTH_A,
     .kind = RecordingKind::FALLBACK,
     .rva = DEPTH_FLUSH_ANIM_RVA,
     .context = DEPTH_FLUSH_ANIM_CONTEXT,
     .context_offset = 18,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = FLUSH_RVA,
     .callee_head = FLUSH_HEAD,
     .record_site = draw_recording::Site::DEPTH_FLUSH_ANIM},
    {.name = "colour loop boundary flush",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::FALLBACK,
     .rva = COLOUR_FLUSH_BOUNDARY_RVA,
     .context = COLOUR_FLUSH_BOUNDARY_CONTEXT,
     .context_offset = 19,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = FLUSH_RVA,
     .callee_head = FLUSH_HEAD,
     .record_site = draw_recording::Site::COLOUR_FLUSH_BOUNDARY},
    {.name = "colour loop end flush",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::FALLBACK,
     .rva = COLOUR_FLUSH_END_RVA,
     .context = COLOUR_FLUSH_END_CONTEXT,
     .context_offset = 20,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = FLUSH_RVA,
     .callee_head = FLUSH_HEAD,
     .record_site = draw_recording::Site::COLOUR_FLUSH_END},
    {.name = "colour loop variation flush",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::FALLBACK,
     .rva = COLOUR_FLUSH_VARIATION_RVA,
     .context = COLOUR_FLUSH_VARIATION_CONTEXT,
     .context_offset = 20,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = FLUSH_RVA,
     .callee_head = FLUSH_HEAD,
     .record_site = draw_recording::Site::COLOUR_FLUSH_VARIATION},
    {.name = "colour loop change flush",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::FALLBACK,
     .rva = COLOUR_FLUSH_CHANGE_RVA,
     .context = COLOUR_FLUSH_CHANGE_CONTEXT,
     .context_offset = 20,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = FLUSH_RVA,
     .callee_head = FLUSH_HEAD,
     .record_site = draw_recording::Site::COLOUR_FLUSH_CHANGE},
    {.name = "colour loop cut",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::OWN,
     .rva = COLOUR_CUT_RVA,
     .context = COLOUR_CUT_CONTEXT,
     .context_offset = COLOUR_CUT_OFFSET,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = COLOUR_LOOP_RVA,
     .callee_head = COLOUR_LOOP_HEAD,
     .record_site = draw_recording::Site::COUNT},
    {.name = "colour loop animation start",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::OWN,
     .rva = COLOUR_LOOP_RVA + COLOUR_INIT_OFFSET,
     .context = COLOUR_LOOP_HEAD,
     .context_offset = COLOUR_INIT_OFFSET,
     .opcode = 0xE9,
     .covered = COLOUR_INIT_SIZE,
     .record_site = draw_recording::Site::COUNT},
    {.name = "colour loop mesh setup",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::OWN,
     .rva = COLOUR_SETUP_RVA,
     .context = COLOUR_SETUP_CONTEXT,
     .context_offset = 8,
     .opcode = 0xE9,
     .covered = 5,
     .exit_rva = COLOUR_SETUP_EXIT_RVA,
     .exit_head = COLOUR_SETUP_EXIT,
     .record_site = draw_recording::Site::COLOUR_SETUP},
    {.name = "colour loop setup shared-state call",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::OWN,
     .rva = COLOUR_VF68_SETUP_RVA,
     .context = COLOUR_VF68_SETUP_CONTEXT,
     .context_offset = 12,
     .opcode = 0xE8,
     .covered = 5,
     .record_site = draw_recording::Site::COLOUR_VF68_SETUP},
    {.name = "colour loop variation shared-state call",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::OWN,
     .rva = COLOUR_VF68_VARIATION_RVA,
     .context = COLOUR_VF68_VARIATION_CONTEXT,
     .context_offset = 13,
     .opcode = 0xE8,
     .covered = 5,
     .record_site = draw_recording::Site::COLOUR_VF68_VARIATION},
    {.name = "colour loop animation call",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::OWN,
     .rva = COLOUR_ANIM_RVA,
     .context = COLOUR_ANIM_CONTEXT,
     .context_offset = 5,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = ANIM_CALL_RVA,
     .callee_head = ANIM_CALL_HEAD,
     .record_site = draw_recording::Site::COLOUR_ANIM},
    {.name = "colour loop first constant",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::OWN,
     .rva = COLOUR_CONST1_RVA,
     .context = COLOUR_CONST1_CONTEXT,
     .context_offset = 14,
     .opcode = 0xE9,
     .covered = 7,
     .exit_rva = COLOUR_CONST1_EXIT_RVA,
     .exit_head = COLOUR_CONST1_EXIT,
     .record_site = draw_recording::Site::COLOUR_CONST1},
    {.name = "colour loop second constant",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::OWN,
     .rva = COLOUR_CONST2_RVA,
     .context = COLOUR_CONST2_CONTEXT,
     .context_offset = 8,
     .opcode = 0xE9,
     .covered = 7,
     .exit_rva = COLOUR_CONST2_EXIT_RVA,
     .exit_head = COLOUR_CONST2_EXIT,
     .record_site = draw_recording::Site::COLOUR_CONST2},
    {.name = "colour loop diffuse texture",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::OWN,
     .rva = COLOUR_SRV_DIFFUSE_RVA,
     .context = COLOUR_SRV_DIFFUSE_CONTEXT,
     .context_offset = 7,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = TEXTURE_VIEW_RVA,
     .callee_head = TEXTURE_VIEW_HEAD,
     .exit_rva = COLOUR_SRV_DIFFUSE_EXIT_RVA,
     .exit_head = COLOUR_SRV_DIFFUSE_EXIT,
     .record_site = draw_recording::Site::COLOUR_SRV_DIFFUSE},
    {.name = "colour loop normal texture",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::OWN,
     .rva = COLOUR_SRV_NORMAL_RVA,
     .context = COLOUR_SRV_NORMAL_CONTEXT,
     .context_offset = 7,
     .opcode = 0xE8,
     .covered = 5,
     .callee_rva = TEXTURE_VIEW_RVA,
     .callee_head = TEXTURE_VIEW_HEAD,
     .exit_rva = COLOUR_SRV_NORMAL_EXIT_RVA,
     .exit_head = COLOUR_SRV_NORMAL_EXIT,
     .record_site = draw_recording::Site::COLOUR_SRV_NORMAL},
    {.name = "colour loop faction",
     .group = draw_recording::Group::COLOUR_A,
     .kind = RecordingKind::OWN,
     .rva = COLOUR_FACTION_RVA,
     .context = COLOUR_FACTION_CONTEXT,
     .context_offset = 11,
     .opcode = 0xE9,
     .covered = 6,
     .exit_rva = COLOUR_FACTION_EXIT_RVA,
     .exit_head = COLOUR_FACTION_EXIT,
     .record_site = draw_recording::Site::COLOUR_FACTION},
}};

// The sites a group's rows write: its own, and its fallback flush (the entry hook is every group's otherwise).
constexpr uint64_t GroupSites(draw_recording::Group group, RecordingKind kind) {
  uint64_t sites = 0;
  for (size_t index = 0; index < RECORDING_INFO.size(); ++index) {
    if (RECORDING_INFO[index].group == group && RECORDING_INFO[index].kind == kind) {
      sites |= uint64_t{1} << index;
    }
  }
  return sites;
}

// The 10 classes of the weighted loops: their vtables and the slots the loops call on a worker or defer (the
// technique-type slot vf58, the technique-pass slot vf60, the shared-state slot vf68); vf54, vf64 and vf6c are the same
// in all of them. Read only in the data: a thunk that finds another vtable at the shared-state call flags the chunk,
// which is run again serially.
struct WeightedClass {
  uint32_t vtable;
  uint32_t vf58;
  uint32_t vf60;
  uint32_t vf68;
};
constexpr std::array<WeightedClass, 10> WEIGHTED_CLASSES = {{
    {0x1cbe5f0, 0x1890650, 0x18444f0, 0x1887aa0},
    {0x1cbe744, 0x1890850, 0x18449f0, 0x18896f0},
    {0x1cbe894, 0x1890650, 0x18447f0, 0x18888b0},
    {0x1cbe9e4, 0x1890650, 0x1844960, 0x18892b0},
    {0x1cbeb34, 0x1890650, 0x18448f0, 0x1888fa0},
    {0x1cbec84, 0x1890650, 0x1844880, 0x1888bd0},
    {0x1cbedd4, 0x1890650, 0x18446a0, 0x1888510},
    {0x1cbef24, 0x1890650, 0x1844610, 0x18881c0},
    {0x1cbf074, 0x1890650, 0x1844580, 0x1887d10},
    {0x1cbf1c4, 0x1890650, 0x18444c0, 0x1887930},
}};
constexpr uint32_t WEIGHTED_VF54 = 0x1855aa0;
constexpr uint32_t WEIGHTED_VF64 = 0x170060;  // xor al, al; ret 0xc
constexpr uint32_t WEIGHTED_VF6C = 0x174ebd0;

// The entries of the functions that passtime (the CPU Performance Fix's dev timing) hooks with an E9 and a stack of its
// own that workers would share: an E9 on any of them stands the whole feature down (the flush entry's own hook, once
// written, aside).
constexpr std::array<uint32_t, 13> HOOKED_ENTRIES = {FLUSH_RVA, 0x17b0400, DEPTH_LOOP_RVA, 0x17aef40, 0x17af2b0,
                                                     0x17af720, 0x17afc10, 0x17aff90, 0x17b09f0, 0x17b0cb0,
                                                     0x17b0f80, 0x17b1250, 0x17b1520};

// The thunks' code page. Never freed: a thread can still be in it after the game's bytes are back.
constexpr size_t RECORDING_CODE_SIZE = 0x1000;

// Machine code written into the page. A site's thunk that does not fit, or a short jump that does not reach, fails the
// whole preparation (the page is then not used).
class Assembler {
 public:
  explicit Assembler(std::span<uint8_t> memory) : cursor_(memory.data()), limit_(memory.data() + memory.size()) {}

  uint8_t* Cursor() const { return cursor_; }
  bool Fits() const { return fits_; }

  void Bytes(std::initializer_list<uint8_t> bytes) {
    if (cursor_ + bytes.size() > limit_) {
      fits_ = false;
      return;
    }
    for (const uint8_t byte : bytes) {
      *cursor_++ = byte;
    }
  }

  // The game's own instructions, as the loaded image has them (absolute operands relocated).
  void Copy(const uint8_t* bytes, size_t size) {
    if (cursor_ + size > limit_) {
      fits_ = false;
      return;
    }
    std::memcpy(cursor_, bytes, size);
    cursor_ += size;
  }

  // The bytes and then a 32-bit operand: an absolute address or an immediate.
  void BytesAndOperand(std::initializer_list<uint8_t> bytes, uint32_t operand) {
    Bytes(bytes);
    if (cursor_ + sizeof(operand) > limit_) {
      fits_ = false;
      return;
    }
    std::memcpy(cursor_, &operand, sizeof(operand));
    cursor_ += sizeof(operand);
  }

  // A call or jump (opcode E8 / E9) to `target`.
  void Relative(uint8_t opcode, const void* target) {
    Bytes({opcode});
    if (cursor_ + 4 > limit_) {
      fits_ = false;
      return;
    }
    const uint32_t offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target))
                            - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cursor_ + 4));
    std::memcpy(cursor_, &offset, sizeof(offset));
    cursor_ += 4;
  }

  // A short jump forward (opcode 7x / EB): the displacement byte, to hand to Bind where the jump lands.
  uint8_t* Short(uint8_t opcode) {
    Bytes({opcode, 0});
    return (fits_ ? cursor_ - 1 : nullptr);
  }

  void Bind(uint8_t* displacement) {
    if (!fits_) return;
    const ptrdiff_t distance = cursor_ - (displacement + 1);
    if (distance < 0 || distance > 127) {
      fits_ = false;
      return;
    }
    *displacement = static_cast<uint8_t>(distance);
  }

 private:
  uint8_t* cursor_;
  uint8_t* limit_;
  bool fits_ = true;
};

inline uint32_t Address(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

// push scratch; <load>; sub scratch, [arena]; cmp scratch, [arena_size]; pop scratch: the flags are those of the
// compare, the scratch register is back, `jb` takes the slot's path. `scratch` is the register's number (0 eax, 2 edx),
// `load` puts the tracker (or its `&any`) into it.
inline void EmitSlotTest(Assembler* assembler, const draw_recording::Shared* shared, uint8_t scratch,
                         std::initializer_list<uint8_t> load) {
  assembler->Bytes({static_cast<uint8_t>(0x50 + scratch)});
  assembler->Bytes(load);
  assembler->BytesAndOperand({0x2B, static_cast<uint8_t>(0x05 | (scratch << 3))}, Address(&shared->arena));
  assembler->BytesAndOperand({0x3B, static_cast<uint8_t>(0x05 | (scratch << 3))}, Address(&shared->arena_size));
  assembler->Bytes({static_cast<uint8_t>(0x58 + scratch)});
}

// pushfd; pushad; sub esp, 4; movss [esp], xmm0: Frame's words above the site id.
inline void EmitFrameOpen(Assembler* assembler) {
  assembler->Bytes({0x9C, 0x60, 0x83, 0xEC, 0x04, 0xF3, 0x0F, 0x11, 0x04, 0x24});
}

// push the site id (in edx); edx = frame; call Record; pop the site id and xmm0; popad; popfd. `ecx` holds the slot.
inline void EmitRecordCall(Assembler* assembler, const draw_recording::Shared* shared) {
  assembler->Bytes({0x52, 0x8B, 0xD4});
  assembler->BytesAndOperand({0xFF, 0x15}, Address(&shared->record));
  assembler->Bytes({0x83, 0xC4, 0x08, 0x61, 0x9D});
}

// Entry of the flush (thiscall, 10 arguments, ret 0x28): arguments at [esp + 4 * n]; the 4th is `&any`. `resume` is the
// game's code after the instructions the hook's 5 bytes split (the site's `covered`).
inline uint8_t* BuildFlushEntry(Assembler* assembler, const draw_recording::Shared* shared, const uint8_t* resume) {
  uint8_t* const entry = assembler->Cursor();
  EmitSlotTest(assembler, shared, 0, {0x8B, 0x44, 0x24, 0x14});
  uint8_t* const to_record = assembler->Short(0x72);
  // The game's own first two instructions (the hook's 5 bytes cover the first and 2 bytes of the second), then on.
  assembler->Bytes({0x83, 0xEC, 0x1C, 0x83, 0x7C, 0x24, 0x30, 0x00});
  assembler->Relative(0xE9, resume);
  assembler->Bind(to_record);
  // A flush of no instances returns at once in the game.
  assembler->Bytes({0x83, 0x7C, 0x24, 0x14, 0x00});
  uint8_t* const has_instances = assembler->Short(0x75);
  assembler->Bytes({0xC2, 0x28, 0x00});
  assembler->Bind(has_instances);
  EmitFrameOpen(assembler);
  // The slot is `&any - 0x69`: the 4th argument, past the 40 bytes the frame has so far (eflags, 8 registers, xmm0).
  assembler->BytesAndOperand({0xBA}, static_cast<uint32_t>(draw_recording::Site::FLUSH_ENTRY));
  assembler->Bytes({0x8B, 0x4C, 0x24, 0x38, 0x83, 0xE9, 0x69});
  EmitRecordCall(assembler, shared);
  assembler->Bytes({0xC2, 0x28, 0x00});
  return entry;
}

// A call site of the flush (the fallback): the tracker is esi.
inline uint8_t* BuildFlushCall(Assembler* assembler, const draw_recording::Shared* shared, const uint8_t* flush,
                               draw_recording::Site site) {
  uint8_t* const entry = assembler->Cursor();
  EmitSlotTest(assembler, shared, 2, {0x8B, 0xD6});
  uint8_t* const to_record = assembler->Short(0x72);
  assembler->Relative(0xE9, flush);
  assembler->Bind(to_record);
  EmitFrameOpen(assembler);
  assembler->BytesAndOperand({0xBA}, static_cast<uint32_t>(site));
  assembler->Bytes({0x8B, 0xCE});
  EmitRecordCall(assembler, shared);
  assembler->Bytes({0xC2, 0x28, 0x00});
  return entry;
}

// The shared-state virtual call (`mov eax, [eax + 0x68]; call eax` with eax the mesh's vtable, replaced by a call of
// this thunk: the callee's stack, `ret 8`): the 10 classes' vtables are checked in the record path; another one is a
// site id Record does not know, which flags the chunk (it is run again). `site` is the loop's site id.
inline uint8_t* BuildSharedStateCall(Assembler* assembler, const draw_recording::Shared* shared,
                                     const std::array<uint32_t, WEIGHTED_CLASSES.size()>& vtables,
                                     draw_recording::Site site) {
  uint8_t* const entry = assembler->Cursor();
  EmitSlotTest(assembler, shared, 2, {0x8B, 0xD6});
  uint8_t* const to_record = assembler->Short(0x72);
  assembler->Bytes({0x8B, 0x40, 0x68, 0xFF, 0xE0});
  assembler->Bind(to_record);
  EmitFrameOpen(assembler);
  assembler->BytesAndOperand({0xBA}, static_cast<uint32_t>(site));
  std::array<uint8_t*, WEIGHTED_CLASSES.size()> known = {};
  for (size_t index = 0; index < vtables.size(); ++index) {
    assembler->BytesAndOperand({0x3D}, vtables[index]);
    known[index] = assembler->Short(0x74);
  }
  assembler->BytesAndOperand({0xBA}, static_cast<uint32_t>(draw_recording::Site::COUNT));
  // The saved eax (above xmm0 and the 8 registers of pushad): 0, so that the loop's `or [esi + 0x69], al` adds nothing.
  assembler->Bytes({0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00});
  for (uint8_t* const displacement : known) {
    assembler->Bind(displacement);
  }
  assembler->Bytes({0x8B, 0xCE});
  EmitRecordCall(assembler, shared);
  assembler->Bytes({0xC2, 0x08, 0x00});
  return entry;
}

// The animation call (thiscall, 1 argument): the direct path notes the index, which is where Record reads it (the
// site's `operand`, in dwords from the return address).
inline uint8_t* BuildAnimationCall(Assembler* assembler, const draw_recording::Shared* shared, const uint8_t* call,
                                   draw_recording::Site site) {
  const uint32_t anim_stack = draw_recording::internal::SITES[static_cast<size_t>(site)].operand;
  uint8_t* const entry = assembler->Cursor();
  EmitSlotTest(assembler, shared, 2, {0x8B, 0xD6});
  uint8_t* const to_record = assembler->Short(0x72);
  // push eax; mov eax, [esp + 4 * (the index's slot + 1)]; mov [last_anim], eax; pop eax
  assembler->Bytes({0x50, 0x8B, 0x44, 0x24, static_cast<uint8_t>(4 * (anim_stack + 1))});
  assembler->BytesAndOperand({0xA3}, Address(&shared->last_anim));
  assembler->Bytes({0x58});
  assembler->Relative(0xE9, call);
  assembler->Bind(to_record);
  EmitFrameOpen(assembler);
  assembler->BytesAndOperand({0xBA}, static_cast<uint32_t>(site));
  assembler->Bytes({0x8B, 0xCE});
  EmitRecordCall(assembler, shared);
  assembler->Bytes({0xC2, 0x04, 0x00});
  return entry;
}

// The animation index's start of a loop (the depth loop's `mov dword ptr [esp + 0x18], -1`, the colour loop's `or eax, -1;
// mov [esp + 0x24], eax`, whose eax is -1 afterwards): a chunk on a slot starts with the slot's index, a direct call with
// Shared::seed_anim. `local` is the index's place on the game's stack (esp-relative at the site); registers are kept (eax
// is the scratch).
inline uint8_t* BuildAnimationInit(Assembler* assembler, const draw_recording::Shared* shared, const uint8_t* resume,
                                   uint8_t local, bool sets_eax) {
  uint8_t* const entry = assembler->Cursor();
  if (sets_eax) assembler->Bytes({0x83, 0xC8, 0xFF});
  assembler->Bytes({0x50, 0x8B, 0xC6});
  assembler->BytesAndOperand({0x2B, 0x05}, Address(&shared->arena));
  assembler->BytesAndOperand({0x3B, 0x05}, Address(&shared->arena_size));
  uint8_t* const outside = assembler->Short(0x73);
  assembler->Bytes({0x8B, 0x86, 0xA4, 0x00, 0x00, 0x00});
  uint8_t* const store = assembler->Short(0xEB);
  assembler->Bind(outside);
  assembler->BytesAndOperand({0xA1}, Address(&shared->seed_anim));
  assembler->Bind(store);
  // The local is past the pushed eax.
  assembler->Bytes({0x89, 0x44, 0x24, static_cast<uint8_t>(local + 4), 0x58});
  assembler->Relative(0xE9, resume);
  return entry;
}

// The colour loop's sites inside the loop (E9 jumps, esi the tracker): `covered` are the game's instructions the 5 bytes
// replace, `resume` the game's code after them, `landing` where the record path goes on.
// The mesh setup (ebx): the direct path is the game's two instructions; the record path asks Record, which sets eax to 1 for
// a mesh of a class it does not know (the loop then skips the mesh as one with no technique: `mov [esi + 0x74], 0` and on).
inline uint8_t* BuildMeshSetup(Assembler* assembler, const draw_recording::Shared* shared, const uint8_t* covered,
                               size_t covered_size, const uint8_t* resume, const uint8_t* landing) {
  uint8_t* const entry = assembler->Cursor();
  EmitSlotTest(assembler, shared, 2, {0x8B, 0xD6});
  uint8_t* const to_record = assembler->Short(0x72);
  uint8_t* const direct = assembler->Cursor();
  assembler->Copy(covered, covered_size);
  assembler->Relative(0xE9, resume);
  assembler->Bind(to_record);
  EmitFrameOpen(assembler);
  assembler->BytesAndOperand({0xBA}, static_cast<uint32_t>(draw_recording::Site::COLOUR_SETUP));
  assembler->Bytes({0x8B, 0xCE});
  EmitRecordCall(assembler, shared);
  // eax: 0 sets the mesh up as the game does, 1 skips it.
  assembler->Bytes({0x85, 0xC0});
  uint8_t* const skip = assembler->Short(0x75);
  assembler->Relative(0xE9, direct);
  assembler->Bind(skip);
  assembler->Bytes({0xC7, 0x46, 0x74, 0x00, 0x00, 0x00, 0x00});
  assembler->Relative(0xE9, landing);
  return entry;
}

// A constant write's guard (`cmp byte ptr [the cache flag], 0`): the direct path is that compare and on (its flags are
// the game's); the record path records the write (ecx the block of the first, xmm0 and the state at [esp + 0x14] of the
// second) and goes on after it, with `landing_bytes` (the second's `mov ebx, [esp + 0x14]` that the guard's path would have
// run) first.
inline uint8_t* BuildConstant(Assembler* assembler, const draw_recording::Shared* shared, draw_recording::Site site,
                              const uint8_t* covered, size_t covered_size, const uint8_t* resume, const uint8_t* landing,
                              std::initializer_list<uint8_t> landing_bytes) {
  uint8_t* const entry = assembler->Cursor();
  EmitSlotTest(assembler, shared, 2, {0x8B, 0xD6});
  uint8_t* const to_record = assembler->Short(0x72);
  assembler->Copy(covered, covered_size);
  assembler->Relative(0xE9, resume);
  assembler->Bind(to_record);
  EmitFrameOpen(assembler);
  assembler->BytesAndOperand({0xBA}, static_cast<uint32_t>(site));
  assembler->Bytes({0x8B, 0xCE});
  EmitRecordCall(assembler, shared);
  assembler->Bytes(landing_bytes);
  assembler->Relative(0xE9, landing);
  return entry;
}

// The faction block (the faction in ecx, the item in edi): the game's two instructions run first on both paths (the
// faction goes into the tracker), then the direct path goes on into the block and the record path records it and jumps to
// its end.
inline uint8_t* BuildFaction(Assembler* assembler, const draw_recording::Shared* shared, const uint8_t* covered,
                             size_t covered_size, const uint8_t* resume, const uint8_t* landing) {
  uint8_t* const entry = assembler->Cursor();
  assembler->Copy(covered, covered_size);
  EmitSlotTest(assembler, shared, 2, {0x8B, 0xD6});
  uint8_t* const to_record = assembler->Short(0x72);
  assembler->Relative(0xE9, resume);
  assembler->Bind(to_record);
  EmitFrameOpen(assembler);
  assembler->BytesAndOperand({0xBA}, static_cast<uint32_t>(draw_recording::Site::COLOUR_FACTION));
  assembler->Bytes({0x8B, 0xCE});
  EmitRecordCall(assembler, shared);
  assembler->Relative(0xE9, landing);
  return entry;
}

// A call of the texture lookup (thiscall, ecx the texture): the direct path is a jump to it; the record path records the
// shader resource bind (the state in ebx, the texture in ecx), drops the call's return address and goes on after the bind.
inline uint8_t* BuildTextureCall(Assembler* assembler, const draw_recording::Shared* shared, const uint8_t* lookup,
                                 draw_recording::Site site, const uint8_t* landing) {
  uint8_t* const entry = assembler->Cursor();
  EmitSlotTest(assembler, shared, 2, {0x8B, 0xD6});
  uint8_t* const to_record = assembler->Short(0x72);
  assembler->Relative(0xE9, lookup);
  assembler->Bind(to_record);
  EmitFrameOpen(assembler);
  assembler->BytesAndOperand({0xBA}, static_cast<uint32_t>(site));
  assembler->Bytes({0x8B, 0xCE});
  EmitRecordCall(assembler, shared);
  assembler->Bytes({0x83, 0xC4, 0x04});
  assembler->Relative(0xE9, landing);
  return entry;
}

// The cut: the loop's call, with its `arguments` stack arguments pushed again for the dispatcher (a fastcall that pops
// them) at `*dispatcher` (a Shared slot: dispatch_depth, dispatch_colour, ...), and the game's `ret` of `return_bytes`;
// `active` counts the dispatches in flight (PausedWrite takes sites out only while it is 0). The caller's `ecx` goes
// through.
inline uint8_t* BuildCut(Assembler* assembler, const draw_recording::Shared* shared, uint8_t arguments,
                         uint16_t return_bytes, const uint32_t* dispatcher) {
  uint8_t* const entry = assembler->Cursor();
  assembler->BytesAndOperand({0xF0, 0xFF, 0x05}, Address(&shared->active));
  // Each push shifts esp by 4, so the same offset takes the arguments from the last to the first.
  for (uint8_t argument = 0; argument < arguments; ++argument) {
    assembler->Bytes({0xFF, 0x74, 0x24, static_cast<uint8_t>(4 * arguments)});
  }
  assembler->BytesAndOperand({0xFF, 0x15}, Address(dispatcher));
  assembler->BytesAndOperand({0xF0, 0xFF, 0x0D}, Address(&shared->active));
  assembler->Bytes({0xC2, static_cast<uint8_t>(return_bytes & 0xFF), static_cast<uint8_t>(return_bytes >> 8)});
  return entry;
}

// The loop groups' cuts are built first, at the start of the page: a removal waits while a thread is in it (it is about to run
// the dispatcher, whose chunks need the flush hook).
constexpr uint64_t RECORDING_CUTS = RecordingBit(RecordingSite::DEPTH_CUT) | RecordingBit(RecordingSite::COLOUR_CUT);

// The group's own sites and its fallback flush sites, by draw_recording::Group.
constexpr std::array<uint64_t, static_cast<size_t>(draw_recording::Group::COUNT)> RECORDING_OWN = {
    GroupSites(draw_recording::Group::DEPTH_A, RecordingKind::OWN),
    GroupSites(draw_recording::Group::COLOUR_A, RecordingKind::OWN),
    GroupSites(draw_recording::Group::DEPTH_B, RecordingKind::OWN),
    GroupSites(draw_recording::Group::COLOUR_B, RecordingKind::OWN),
    0};
constexpr std::array<uint64_t, static_cast<size_t>(draw_recording::Group::COUNT)> RECORDING_FALLBACK = {
    GroupSites(draw_recording::Group::DEPTH_A, RecordingKind::FALLBACK),
    GroupSites(draw_recording::Group::COLOUR_A, RecordingKind::FALLBACK),
    GroupSites(draw_recording::Group::DEPTH_B, RecordingKind::FALLBACK),
    GroupSites(draw_recording::Group::COLOUR_B, RecordingKind::FALLBACK),
    0};
constexpr uint64_t RECORDING_ENTRY_BIT = RecordingBit(RecordingSite::FLUSH_ENTRY);
static_assert(RECORDING_SITE_COUNT == RECORDING_SITES);

static const uint8_t* recording_base = nullptr;

// No entry that the Attila CPU Performance Fix's dev timing (passtime) hooks carries an E9: it would run on a stack of
// its own that workers would share. `flush_hooked`: the flush entry's E9 is our own hook.
inline bool RecordingEntriesFree(bool flush_hooked) {
  for (const uint32_t entry : HOOKED_ENTRIES) {
    if (recording_base[entry] == 0xE9 && !(flush_hooked && entry == FLUSH_RVA)) return false;
  }
  return true;
}

// The sites the loop groups in `groups` (bits of draw_recording::Group) ask for: each one's own and its flush path (the
// entry hook, else its call sites), only when every one of them was found. A group these sites are not here for yet
// asks for nothing. Going in with a hooked entry (passtime started since Prepare) stands the feature down for the
// session: nothing is asked for again, and what is written stays known (found) so that it can come out.
inline uint64_t RecordingWanted(uint32_t groups) {
  if (recording_refused) return 0;
  uint64_t wanted = 0;
  for (size_t group = 0; group < RECORDING_OWN.size(); ++group) {
    const uint64_t flush =
        ((recording_found & RECORDING_ENTRY_BIT) != 0 ? RECORDING_ENTRY_BIT : RECORDING_FALLBACK[group]);
    const uint64_t group_sites = RECORDING_OWN[group] | flush;
    if ((groups >> group & 1) != 0 && RECORDING_OWN[group] != 0 && flush != 0
        && (group_sites & ~recording_found) == 0) {
      wanted |= group_sites;
    }
  }
  if ((wanted & ~recording_written) != 0 && !RecordingEntriesFree((recording_written & RECORDING_ENTRY_BIT) != 0)) {
    recording_found &= recording_written;
    recording_refused = true;
    renodx::utils::log::w(
        "[Total War] Threaded Draw Recording: left alone (the game's draw functions are hooked by another tool)");
    return 0;
  }
  return wanted;
}

// Once: the sites found and checked by their bytes, the thunks built in a page, each site's bytes made (nothing written
// to the game). One log line names what was left alone. False: Threaded Draw Recording cannot be used (nothing is
// found, the dispatchers run the loops directly).
inline bool PrepareRecording(HMODULE module) {
  recording_prepared = true;
  recording_found = 0;
  recording_refused = false;
  for (size_t group = 0; group < recording_group_sites.size(); ++group) {
    recording_group_sites[group] = RECORDING_OWN[group] | RECORDING_FALLBACK[group];
  }
  recording_entry_bit = RECORDING_ENTRY_BIT;
  draw_recording::GameCalls calls;
  draw_recording::Shared* const shared = draw_recording::SharedPage();
  if (shared == nullptr) {
    draw_recording::SetGame(calls);
    return false;
  }
  auto* base = reinterpret_cast<uint8_t*>(module);
  recording_base = base;
  const auto* headers =
      reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  const uint8_t* image_end = base + headers->OptionalHeader.SizeOfImage;
  const auto sections = tw::engine_tweaks::ModuleSections(module);
  const auto holds = [&](const uint8_t* at, std::string_view pattern) {
    const size_t size = (pattern.size() + 1) / 3;
    for (const std::string_view memory : sections.code) {
      const auto* begin = reinterpret_cast<const uint8_t*>(memory.data());
      if (at >= begin && size <= memory.size() && at <= begin + memory.size() - size) {
        const std::string_view bytes(reinterpret_cast<const char*>(at), size);
        return tw::engine_tweaks::FindPattern({bytes}, pattern) == at;
      }
    }
    return false;
  };
  const auto read32 = [](const uint8_t* at) {
    uint32_t value;
    std::memcpy(&value, at, sizeof(value));
    return value;
  };
  // The target of the call or jump whose opcode byte is at `instruction`.
  const auto target = [&](const uint8_t* instruction) {
    return instruction + 5 + static_cast<int32_t>(read32(instruction + 1));
  };
  constexpr std::string_view NOT_AS_EXPECTED = "not the game's code as expected: another mod or build";
  std::string refused;
  const auto refuse = [&](std::string_view name) {
    refused += std::format("{}{}", refused.empty() ? "" : ", ", name);
  };
  for (size_t index = 0; index < RECORDING_SITE_COUNT; ++index) {
    recording_sites[index] = {.name = RECORDING_INFO[index].name, .opcode = RECORDING_INFO[index].opcode};
  }

  // The pieces every site needs: the flush's head and its exit for no instances (its `je` must go there), no hooked
  // entry, the 10 classes' vtables (each slot the loops call).
  const uint8_t* const flush = base + FLUSH_RVA;
  bool shared_ok = holds(flush, FLUSH_HEAD) && holds(base + FLUSH_EMPTY_RVA, FLUSH_EMPTY)
                   && target(flush + FLUSH_EMPTY_JUMP + 1) == base + FLUSH_EMPTY_RVA;
  if (!shared_ok) refuse("flush");
  if (!RecordingEntriesFree(false)) {
    shared_ok = false;
    refuse("an entry hooked by another tool");
  }
  // The B groups' two helpers (the sorted list's accessor, the do-nothing virtual call): needed by every dispatcher, whichever
  // group it runs for.
  if (shared_ok && !(holds(base + ACCESSOR_RVA, ACCESSOR_HEAD) && holds(base + WEIGHTED_VF64, VF64_STUB_HEAD))) {
    shared_ok = false;
    refuse("the sorted list's accessor or the virtual call that does nothing");
  }
  std::array<uint32_t, WEIGHTED_CLASSES.size()> vtables = {};
  for (size_t index = 0; index < WEIGHTED_CLASSES.size() && shared_ok; ++index) {
    const WeightedClass& weighted = WEIGHTED_CLASSES[index];
    const uint8_t* const vtable = base + weighted.vtable;
    const auto slot = [&](uint32_t offset) { return read32(vtable + offset); };
    const auto at = [&](uint32_t rva) { return Address(base + rva); };
    if (vtable + 0x70 > image_end || slot(0x54) != at(WEIGHTED_VF54) || slot(0x58) != at(weighted.vf58)
        || slot(0x60) != at(weighted.vf60) || slot(0x64) != at(WEIGHTED_VF64) || slot(0x68) != at(weighted.vf68)
        || slot(0x6c) != at(WEIGHTED_VF6C)) {
      shared_ok = false;
      refuse("a weighted class's vtable");
    }
    vtables[index] = Address(vtable);
  }

  // The colour replays' calls and constants, each checked by its bytes: without all of them the colour loop's own sites stay out.
  bool colour_ok = shared_ok;
  for (const ColourPieceInfo& piece : COLOUR_PIECES) {
    const uint8_t* const at = base + piece.rva;
    if (colour_ok && !(holds(at - piece.context_offset, piece.context)
                       && (piece.callee_rva == 0 || (target(at) == base + piece.callee_rva
                                                     && holds(base + piece.callee_rva, piece.callee_head))))) {
      colour_ok = false;
      refuse(piece.name);
    }
  }
  if (colour_ok) {
    const auto piece_at = [&](ColourPiece piece) { return base + COLOUR_PIECES[static_cast<size_t>(piece)].rva; };
    calls.dirty_call = Address(target(piece_at(ColourPiece::DIRTY_CALL)));
    calls.srv_call = Address(target(piece_at(ColourPiece::SHADER_RESOURCE_CALL)));
    calls.faction_call = Address(target(piece_at(ColourPiece::FACTION_CALL)));
    calls.faction_default =
        reinterpret_cast<const float*>(static_cast<uintptr_t>(read32(piece_at(ColourPiece::FACTION_DEFAULT) + 4)));
    calls.view_call = Address(base + TEXTURE_VIEW_RVA);
    // The cache flag is the operand of both constants' guards (`cmp byte ptr [flag], 0`: 2 opcode bytes, then the address).
    calls.cached_constants =
        reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(read32(base + COLOUR_CONST1_RVA + 2)));
    if (read32(base + COLOUR_CONST2_RVA + 2) != Address(calls.cached_constants)) {
      colour_ok = false;
      refuse("colour loop constants' cache flag");
    }
  }

  recording_page = nullptr;
  if (shared_ok) {
    recording_page = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, RECORDING_CODE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  }
  Assembler assembler(std::span<uint8_t>(recording_page, recording_page == nullptr ? 0 : RECORDING_CODE_SIZE));
  std::array<const uint8_t*, RECORDING_SITE_COUNT> thunks = {};
  uint64_t built = 0;
  for (int pass = 0; pass < 2 && recording_page != nullptr; ++pass) {
    for (size_t index = 0; index < RECORDING_SITE_COUNT; ++index) {
      if (((RECORDING_CUTS >> index & 1) != 0) != (pass == 0)) continue;
      const RecordingSiteInfo& info = RECORDING_INFO[index];
      const uint8_t* const at = base + info.rva;
      const uint8_t* const callee = base + info.callee_rva;
      const uint8_t* const landing = base + info.exit_rva;
      if (!holds(base + info.rva - info.context_offset, info.context)
          || (info.callee_rva != 0 && (target(at) != callee || !holds(callee, info.callee_head)))
          || (info.exit_rva != 0 && !holds(landing, info.exit_head))
          || (info.group == draw_recording::Group::COLOUR_A && info.kind == RecordingKind::OWN && !colour_ok)) {
        refuse(info.name);
        continue;
      }
      switch (static_cast<RecordingSite>(index)) {
        case RecordingSite::FLUSH_ENTRY:
          thunks[index] = BuildFlushEntry(&assembler, shared, at + info.covered);
          break;
        case RecordingSite::DEPTH_CUT:
          thunks[index] = BuildCut(&assembler, shared, 4, 0x10, &shared->dispatch_depth);
          calls.loops[static_cast<size_t>(draw_recording::Group::DEPTH_A)] = Address(target(at));
          break;
        case RecordingSite::DEPTH_INIT:
          thunks[index] = BuildAnimationInit(&assembler, shared, at + info.covered, 0x18, false);
          break;
        case RecordingSite::COLOUR_INIT:
          thunks[index] = BuildAnimationInit(&assembler, shared, at + info.covered, 0x24, true);
          break;
        case RecordingSite::DEPTH_VF68:
        case RecordingSite::COLOUR_VF68_SETUP:
        case RecordingSite::COLOUR_VF68_VARIATION:
          thunks[index] = BuildSharedStateCall(&assembler, shared, vtables, info.record_site);
          break;
        case RecordingSite::DEPTH_ANIM:
        case RecordingSite::COLOUR_ANIM:
          thunks[index] = BuildAnimationCall(&assembler, shared, target(at), info.record_site);
          calls.anim_call = Address(target(at));
          break;
        case RecordingSite::COLOUR_CUT:
          thunks[index] = BuildCut(&assembler, shared, 5, 0x14, &shared->dispatch_colour);
          calls.loops[static_cast<size_t>(draw_recording::Group::COLOUR_A)] = Address(target(at));
          break;
        case RecordingSite::COLOUR_SETUP:
          thunks[index] = BuildMeshSetup(&assembler, shared, at, info.covered, at + info.covered, landing);
          break;
        case RecordingSite::COLOUR_CONST1:
          thunks[index] =
              BuildConstant(&assembler, shared, info.record_site, at, info.covered, at + info.covered, landing, {});
          break;
        case RecordingSite::COLOUR_CONST2:
          // The guard's path would have run `mov ebx, [esp + 0x14]` (the state) before the write.
          thunks[index] = BuildConstant(&assembler, shared, info.record_site, at, info.covered, at + info.covered,
                                        landing, {0x8B, 0x5C, 0x24, 0x14});
          break;
        case RecordingSite::COLOUR_SRV_DIFFUSE:
        case RecordingSite::COLOUR_SRV_NORMAL:
          thunks[index] = BuildTextureCall(&assembler, shared, target(at), info.record_site, landing);
          break;
        case RecordingSite::COLOUR_FACTION:
          thunks[index] = BuildFaction(&assembler, shared, at, info.covered, at + info.covered, landing);
          break;
        default:
          thunks[index] = BuildFlushCall(&assembler, shared, flush, info.record_site);
          break;
      }
      built |= uint64_t{1} << index;
    }
    if (pass == 0) recording_cut.end = assembler.Cursor();
  }
  DWORD protection;
  if (recording_page == nullptr || !assembler.Fits()
      || VirtualProtect(recording_page, RECORDING_CODE_SIZE, PAGE_EXECUTE_READ, &protection) == FALSE) {
    // As if there were nothing (no write has been made): a jump into a page that cannot run would crash the game.
    const bool page_failed = shared_ok;
    if (recording_page != nullptr) VirtualFree(recording_page, 0, MEM_RELEASE);
    recording_page = nullptr;
    recording_cut = {};
    recording_page_end = nullptr;
    draw_recording::SetGame({});
    if (page_failed) {
      renodx::utils::log::w("[Total War] Threaded Draw Recording: left alone (its code page could not be made)");
    } else {
      renodx::utils::log::w("[Total War] Threaded Draw Recording: left alone (", NOT_AS_EXPECTED, ")",
                            (refused.empty() ? "" : ": "), refused);
    }
    return false;
  }
  FlushInstructionCache(GetCurrentProcess(), recording_page, RECORDING_CODE_SIZE);
  recording_page_end = recording_page + RECORDING_CODE_SIZE;
  for (size_t index = 0; index < RECORDING_SITE_COUNT; ++index) {
    if ((built >> index & 1) == 0) continue;
    Site& site = recording_sites[index];
    site.at = const_cast<uint8_t*>(base + RECORDING_INFO[index].rva);
    std::memcpy(site.game.data(), site.at, SITE_SIZE);
    site.jump[0] = site.opcode;
    const uint32_t offset = Address(thunks[index]) - Address(site.at + SITE_SIZE);
    std::memcpy(site.jump.data() + 1, &offset, sizeof(offset));
  }
  recording_found = built;
  calls.flush = Address(flush);
  calls.weighted_vtables = vtables;
  // The B groups' dispatcher runs the game's meshes through the accessor on every path, its own direct one included: every
  // address its code calls is set here (the class table follows with the B sites).
  calls.accessor = Address(base + ACCESSOR_RVA);
  calls.vf64_stub = Address(base + WEIGHTED_VF64);
  calls.module_shift = Address(base) - 0x10000000;
  // A group is usable with all of its own sites and its flush path (the entry hook, else its call sites): the others
  // run directly.
  calls.usable_groups = 0;
  for (size_t group = 0; group < recording_group_sites.size(); ++group) {
    const uint64_t flush_path = ((built & RECORDING_ENTRY_BIT) != 0 ? RECORDING_ENTRY_BIT : RECORDING_FALLBACK[group]);
    if (RECORDING_OWN[group] != 0 && flush_path != 0 && ((RECORDING_OWN[group] | flush_path) & ~built) == 0) {
      calls.usable_groups |= 1u << group;
    }
  }
  calls.usable = (calls.usable_groups != 0);
  draw_recording::SetGame(calls);
  recording_cut.begin = recording_page;
  recording_cut.active = &shared->active;
  recording_arena_size = &shared->arena_size;
  if (shared->arena_size != 0) recording_arena_size_value = shared->arena_size;  // 0 while no site is in
  renodx::utils::log::i("[Total War] Threaded Draw Recording: ", std::popcount(built), " of ", RECORDING_SITE_COUNT,
                        " sites found", (refused.empty() ? "" : std::format("; left alone ({}): ", NOT_AS_EXPECTED)),
                        refused);
  return true;
}
