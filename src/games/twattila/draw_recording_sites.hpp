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
//
// The B groups (the per-mesh loops of the folded driver) have no call-site fallback: they need the entry hook. Their cut is
// one E9 in DispatchMesh (the 8 covered bytes are the game's two loads; the thunk's DispatchMesh runs inside `active`).
// The depth and colour bodies' shared-state calls are the same thunk as A's without the vtable check (vf68 `ret 8`, vf64
// `ret 0xc`): the planner chose the class, and the thunks of one form are one thunk. The class table is decoded from the
// slot-0x138 thunks and the dispatchers. The colour bodies have the same three calls and a state block each (the snow
// constant, the shader resources, the faction colours: an E9 whose record path lays the block's operands on the stack for
// Record and goes on after the block; the direct path is the game's covered instructions). Colour B needs the cut and the
// entry hook as well.

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
  // The B groups' (the per-mesh loops of the folded driver): the cut, then the five depth bodies' three shared-state calls each
  // (the first form, the variation, the slot-0x64 call).
  DEPTH_B_CUT,
  DEPTH_B0_VF68,
  DEPTH_B0_VARIATION,
  DEPTH_B0_VF64,
  DEPTH_B1_VF68,
  DEPTH_B1_VARIATION,
  DEPTH_B1_VF64,
  DEPTH_B2_VF68,
  DEPTH_B2_VARIATION,
  DEPTH_B2_VF64,
  DEPTH_B3_VF68,
  DEPTH_B3_VARIATION,
  DEPTH_B3_VF64,
  DEPTH_B4_VF68,
  DEPTH_B4_VARIATION,
  DEPTH_B4_VF64,
  // Colour B's: the colour bodies' shared-state calls, slot-0x64 call and state block (per body).
  COLOUR_B0_VF68,
  COLOUR_B0_VARIATION,
  COLOUR_B0_VF64,
  COLOUR_B0_STATE,
  COLOUR_B1_VF68,
  COLOUR_B1_VARIATION,
  COLOUR_B1_VF64,
  COLOUR_B1_STATE,
  COLOUR_B2_VF68,
  COLOUR_B2_VARIATION,
  COLOUR_B2_VF64,
  COLOUR_B2_STATE,
  COLOUR_B3_VF68,
  COLOUR_B3_VARIATION,
  COLOUR_B3_VF64,
  COLOUR_B3_STATE,
  COLOUR_B4_VF68,
  COLOUR_B4_VARIATION,
  COLOUR_B4_VF64,
  COLOUR_B4_STATE,
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
// The first form of the shared-state call (the colour loop's after its mesh setup, and the first of each depth body).
constexpr std::string_view VF68_FIRST_CONTEXT =
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
constexpr uint32_t WEIGHTED_VF64 = 0x170060;  // xor al, al; ret 0xc: the do-nothing virtual call (slot 0x64) of nearly every class

// The B groups' cut: the folded driver's loop over a round's meshes (`for ebx = first .. edi = last: mesh = *accessor(sorted, ebx);
// mesh->vf138(tracker = ebp, device, view, list type = esi, node)`), right after its `test al, al; jne` over the pixel-selection
// path. The site is the loop's first two loads (8 bytes, 5 written); the thunk runs the round through DispatchMesh and leaves
// as the loop does (esi the list type, edi the last mesh, ebx past it), at the game's code after the loop.
constexpr uint32_t DEPTH_B_CUT_RVA = 0x17aed4a;
constexpr std::string_view DEPTH_B_CUT_CONTEXT =
    "75 42 8B 5C 24 34 8B 7C 24 1C 3B DF 0F 87 12 01 00 00 8B 74 24 10 66 90 8B 4C 24 28 53 E8 ?? ?? ?? ?? "
    "FF 74 24 30 56 8B 08 FF 74 24 7C FF 74 24 7C 8B 01 55 FF 90 38 01 00 00 43 3B DF 76 D9 E9 DC 00 00 00";
constexpr uint32_t DEPTH_B_CUT_OFFSET = 2;
constexpr uint32_t DEPTH_B_CUT_COVERED = 8;
// Where the game goes on after the loop (its `mov al, [esp + 0x78]`), and where its `ja` for an empty round lands (4 bytes on).
constexpr uint32_t DEPTH_B_CUT_EXIT_RVA = 0x17aee68;
constexpr std::string_view DEPTH_B_CUT_EXIT = "8A 44 24 78 80 7C 24 24 00 0F 85 21 FD FF FF 8B 5C 24 74 89 7C 24 34 E9 BC FC FF FF";
constexpr uint32_t DEPTH_B_CUT_EMPTY_OFFSET = 4;

// The five depth bodies (thiscall, ecx the mesh: tracker, list, begin, end): every one has the same three calls of the mesh's
// vtable, which record on a slot. Each is the call `mov eax, [eax + 0x68 | 0x64]; call eax` (5 bytes, replaced by a call of a
// thunk with the callee's stack: 2 arguments (vf68) or 3 (vf64)).
constexpr std::string_view DEPTH_B_VF68_VARIATION_CONTEXT =
    "8B 4E 70 89 46 74 57 FF 76 1C 8B 01 8B 40 68 FF D0 08 46 69 8B 7E 58";
// The slot-0x64 call, after a `push [esp + 0x18]` or after `mov ecx, [esi + 0x6c]; push ebp`.
constexpr std::string_view DEPTH_B_VF64_STACK_CONTEXT =
    "FF 74 24 18 FF 76 1C 8B 01 FF 76 18 8B 40 64 FF D0 08 46 69 8B 7E 58";
constexpr std::string_view DEPTH_B_VF64_EBP_CONTEXT =
    "8B 4E 6C 55 FF 76 1C 8B 01 FF 76 18 8B 40 64 FF D0 08 46 69 8B 7E 58";
// The colour bodies' calls: the same three of the mesh's vtable (the first form after `lea ebx, [esi + 0x69]`, the variation with
// the previous mesh in [esi + 0x70], the slot-0x64 call after `mov ecx, [esi + 0x6c]` or three pushes); the bodies differ in
// what follows the `or`, and in the stack local pushed by the slot-0x64 call (wildcarded: Record reads the pushed arguments).
constexpr std::string_view COLOUR_B_VF68_CONTEXT =
    "8D 5E 69 8B 07 8B CF 6A 00 FF 76 1C 8B 40 68 FF D0 08 03 8B 4D 14 8B 7E 58";
constexpr std::string_view COLOUR_B_VF68_VARIATION_CONTEXT = "8B 4E 70 89 46 74 6A 00 FF 76 1C 8B 01 8B 40 68 FF D0 08 ??";
constexpr std::string_view COLOUR_B_VF64_STACK_CONTEXT =
    "FF 74 24 ?? FF 76 1C 8B 01 FF 76 18 8B 40 64 FF D0 08 46 69 8D 5E 69 8B 7E 58";
constexpr std::string_view COLOUR_B_VF64_REGISTER_CONTEXT =
    "8B 4E 6C 53 FF 76 1C 8B 01 FF 76 18 8B 40 64 FF D0 08 46 69 8D 5E 69 8B 7E 58";
// The state blocks: the cmp (or test) that starts each, found by the flush call before it and the instructions after; the
// record path goes on at the block's end, whose first bytes are the exit.
constexpr std::string_view COLOUR_B0_STATE_CONTEXT =
    "E8 ?? ?? ?? ?? C7 46 58 00 00 00 00 C6 07 01 8B 7C 24 18 89 5E 78 80 7C 24 0F 00 74 31 8B 4C 24 10 81 C1 AC 02 00 00";
constexpr std::string_view COLOUR_B1_STATE_CONTEXT =
    "E8 ?? ?? ?? ?? 8A 64 24 12 C7 46 58 00 00 00 00 89 7E 78 C6 46 69 01 80 7C 24 11 00 74 35 8B 4C 24 18 88 66 10";
constexpr std::string_view COLOUR_B2_STATE_CONTEXT =
    "FF 76 18 E8 ?? ?? ?? ?? C7 46 58 00 00 00 00 89 7E 78 C6 46 69 01 84 FF 74 3A 88 5E 10 80 3D ?? ?? ?? ?? 00 0F B6 C3 8B 5C 24 18 "
    "8B 93 C0 02 00 00";
constexpr std::string_view COLOUR_B3_STATE_CONTEXT =
    "E8 ?? ?? ?? ?? C7 46 58 00 00 00 00 8A 4C 24 0F 89 5E 78 C6 07 01 80 7C 24 0E 00 74 35 88 4E 10 0F B6 C1 8B 4C 24 10 81 C1 AC 02 00 00";
// Bodies 1 and 4 are the same code.
constexpr std::string_view COLOUR_B_STATE_EXIT = "8B 7E 58 85 FF 75 25 80 7E 68 00 74 1F 8B 4E 6C";
constexpr std::string_view COLOUR_B_STATE_RELOAD_EXIT = "8B 5C 24 1C 8B 7E 58 85 FF 75 22 80 7E 68 00";
constexpr std::array<uint32_t, 5> DEPTH_B_BODIES = {0x17b09f0, 0x17b0cb0, 0x17b0f80, 0x17b1250, 0x17b1520};
constexpr std::array<uint32_t, 5> COLOUR_B_BODIES = {0x17aef40, 0x17af2b0, 0x17af720, 0x17afc10, 0x17aff90};

// The 8 dispatchers (families 0..7 in address order, DISPATCHER_STRIDE apart) the slot-0x138 thunks call: the view's type picks
// the visibility body (0x49), the depth body ({0, 0x55, 0x60}) or the colour body; the list is `mesh + base + 32 * list type`
// plus 4 (the helper 0xcfbb0), the slice the mesh's +0xac / +0xb0. The 4 calls are at +0x2c (the list), +0x52 (visibility), +0x67
// (depth), +0x7c (colour).
constexpr uint32_t DISPATCHER_FIRST_RVA = 0x17b5a90;
constexpr uint32_t DISPATCHER_STRIDE = 0x90;
constexpr uint32_t DISPATCHER_COUNT = 8;
constexpr std::string_view DISPATCHER_BYTES =
    "53 55 8B 6C 24 14 56 57 8B F9 8B CD 8B 45 00 FF 50 3C 8B F0 85 F6 74 0E 83 FE 55 74 09 83 FE 60 74 04 32 DB EB 02 B3 01 "
    "8B 4C 24 24 E8 ?? ?? ?? ?? 8B 97 B0 00 00 00 85 D2 74 46 8B 8F AC 00 00 00 03 D1 52 51 8B CF 83 FE 49 75 12 55 50 FF 74 24 24 "
    "E8 ?? ?? ?? ?? 5F 5E 5D 5B C2 14 00 84 DB 74 11 50 FF 74 24 20 E8 ?? ?? ?? ?? 5F 5E 5D 5B C2 14 00 "
    "FF 74 24 28 50 FF 74 24 24 E8 ?? ?? ?? ?? 5F 5E 5D 5B C2 14 00";
constexpr uint32_t DISPATCHER_LIST_CALL = 0x2c;
constexpr uint32_t DISPATCHER_DEPTH_CALL = 0x67;
constexpr uint32_t DISPATCHER_COLOUR_CALL = 0x7c;
constexpr uint32_t LIST_HELPER_RVA = 0xcfbb0;
constexpr std::string_view LIST_HELPER_HEAD = "8D 41 04 C3";
// The thunks: the standard one (the list's base operand at +8, the dispatcher call at +0x1f) and the one class that sends its
// colour rounds to a loop of its own (the base at +47, the call to the dispatcher for the depth types at +0x4c).
constexpr std::string_view SLOT138_THUNK =
    "8B 44 24 10 C1 E0 05 05 ?? ?? ?? ?? 03 C1 50 FF 74 24 18 FF 74 24 14 FF 74 24 14 FF 74 24 14 E8 ?? ?? ?? ?? C2 14 00";
constexpr uint32_t SLOT138_THUNK_BASE = 8;
constexpr uint32_t SLOT138_THUNK_CALL = 0x1f;
constexpr std::string_view SLOT138_SPECIAL_THUNK =
    "53 56 57 8B 7C 24 18 8B F1 8B CF 8B 07 FF 50 3C 85 C0 74 13 83 F8 49 74 0E 83 F8 55 74 09 83 F8 60 74 04 32 C9 EB 02 B1 01 "
    "8B 44 24 1C 8D 9E ?? ?? ?? ?? C1 E0 05 03 D8 84 C9 74 1B 53 FF 74 24 24 8B CE 57 FF 74 24 20 FF 74 24 20 E8 ?? ?? ?? ?? "
    "5F 5E 5B C2 14 00";
constexpr uint32_t SLOT138_SPECIAL_BASE = 47;
constexpr uint32_t SLOT138_SPECIAL_CALL = 0x4c;
constexpr uint32_t MESH_VF138 = 0x138;

// The 23 routed classes (the meshes the dispatchers hand to a body): each vtable's slots as the game builds them (a class whose
// slot differs, or whose thunk or dispatcher is not as decoded, is left out and its meshes run directly). vf54 is the same in
// all but two, vf6c in all; a technique-pass slot (vf60) that is the do-nothing call and a shared-state slot (vf68) that is the
// no-op are the classes with no technique; `depth` / `colour` are the groups the class records in (the classes whose technique
// calls reach objects the census could not clear run directly in both).
constexpr uint32_t ROUTED_VF54 = 0x18559c0;
constexpr uint32_t ROUTED_VF54_ALT = 0x18559e0;  // the same getter for the smaller layout (this + 0x3f4 instead of + 0x52c), pure
constexpr uint32_t ROUTED_VF6C = 0x174ebd0;
constexpr uint32_t ROUTED_NO_TECHNIQUE = 0x449760;
constexpr uint32_t ROUTED_NO_SHARED_STATE = 0x29ecb0;
struct RoutedClass {
  uint32_t vtable;
  uint32_t thunk;
  uint32_t vf58;
  uint32_t vf60;
  uint32_t vf68;
  uint32_t vf54 = ROUTED_VF54;
  uint32_t vf64 = WEIGHTED_VF64;
  bool depth = true;
  bool colour = true;
};
constexpr std::array<RoutedClass, 23> ROUTED_CLASSES = {{
    {.vtable = 0x1cbdb38, .thunk = 0x1827830, .vf58 = 0x188fc40, .vf60 = ROUTED_NO_TECHNIQUE, .vf68 = ROUTED_NO_SHARED_STATE},
    {.vtable = 0x1cbdc84, .thunk = 0x1827830, .vf58 = 0x18901d0, .vf60 = 0x1843f90, .vf68 = 0x1885aa0, .depth = false, .colour = false},
    {.vtable = 0x1cbddd0, .thunk = 0x1827830, .vf58 = 0x188ffd0, .vf60 = 0x1843ef0, .vf68 = 0x1885770, .depth = false, .colour = false},
    {.vtable = 0x1cbdf34, .thunk = 0x1827860, .vf58 = 0x188fc40, .vf60 = ROUTED_NO_TECHNIQUE, .vf68 = ROUTED_NO_SHARED_STATE},
    {.vtable = 0x1cbe080, .thunk = 0x1827860, .vf58 = 0x188ff60, .vf60 = 0x1843ed0, .vf68 = 0x1885680},
    {.vtable = 0x1cbf71c, .thunk = 0x1827890, .vf58 = 0x188fc40, .vf60 = ROUTED_NO_TECHNIQUE, .vf68 = ROUTED_NO_SHARED_STATE},
    {.vtable = 0x1cbf868, .thunk = 0x1827890, .vf58 = 0x1890400, .vf60 = 0x1843ff0, .vf68 = 0x1885cb0, .vf64 = 0x188a070},
    {.vtable = 0x1cbd34c, .thunk = 0x18278c0, .vf58 = 0x188fc40, .vf60 = ROUTED_NO_TECHNIQUE, .vf68 = ROUTED_NO_SHARED_STATE},
    {.vtable = 0x1cbd498, .thunk = 0x18278c0, .vf58 = 0x188fc40, .vf60 = 0x1844120, .vf68 = 0x18867c0},
    {.vtable = 0x1cbd5e0, .thunk = 0x18278c0, .vf58 = 0x188fc40, .vf60 = 0x18440d0, .vf68 = 0x18864a0},
    {.vtable = 0x1cc19d0, .thunk = 0x1827a10, .vf58 = 0x1890470, .vf60 = 0x18441c0, .vf68 = 0x1886ca0, .vf54 = ROUTED_VF54_ALT},
    {.vtable = 0x1cbd888, .thunk = 0x18278f0, .vf58 = 0x188fc40, .vf60 = ROUTED_NO_TECHNIQUE, .vf68 = ROUTED_NO_SHARED_STATE},
    {.vtable = 0x1cbd9d4, .thunk = 0x1827980, .vf58 = 0x18900c0, .vf60 = 0x1843f60, .vf68 = 0x18858a0, .colour = false},
    {.vtable = 0x1cbca34, .thunk = 0x1827920, .vf58 = 0x188fc40, .vf60 = ROUTED_NO_TECHNIQUE, .vf68 = ROUTED_NO_SHARED_STATE},
    {.vtable = 0x1cbcb80, .thunk = 0x1827920, .vf58 = 0x1890560, .vf60 = 0x1844390, .vf68 = 0x1887240, .depth = false, .colour = false},
    {.vtable = 0x1cbcccc, .thunk = 0x1827920, .vf58 = 0x188fc40, .vf60 = 0x1844010, .vf68 = 0x1885e30, .vf64 = 0x188a120},
    {.vtable = 0x1cbce14, .thunk = 0x1827920, .vf58 = 0x188fc40, .vf60 = 0x1844060, .vf68 = 0x1886140},
    {.vtable = 0x1cbcf5c, .thunk = 0x1827920, .vf58 = 0x1890460, .vf60 = 0x18440b0, .vf68 = 0x1886450},
    {.vtable = 0x1cbd0a4, .thunk = 0x1827920, .vf58 = 0x188fc40, .vf60 = 0x1844080, .vf68 = 0x18862a0},
    {.vtable = 0x1cbd1ec, .thunk = 0x1827920, .vf58 = 0x188fc40, .vf60 = 0x1843fc0, .vf68 = 0x1885b40},
    {.vtable = 0x1cbd728, .thunk = 0x1827920, .vf58 = 0x188fc40, .vf60 = 0x1844170, .vf68 = 0x1886a10},
    {.vtable = 0x1cbe1e4, .thunk = 0x1827920, .vf58 = 0x188fc40, .vf60 = ROUTED_NO_TECHNIQUE, .vf68 = ROUTED_NO_SHARED_STATE},
    {.vtable = 0x1cbf5b8, .thunk = 0x1827950, .vf58 = 0x18902f0, .vf60 = 0x1843f20, .vf68 = 0x1885810, .vf54 = ROUTED_VF54_ALT},
}};

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

// A shared-state call of a depth or colour body (`mov eax, [eax + 0x68 | 0x64]; call eax`): 5 bytes after 12 of context (13 in
// the colour variation).
constexpr RecordingSiteInfo BodyCall(draw_recording::Group group, std::string_view name, uint32_t rva, std::string_view context,
                                     draw_recording::Site record_site, uint32_t context_offset = 12) {
  return {.name = name,
          .group = group,
          .kind = RecordingKind::OWN,
          .rva = rva,
          .context = context,
          .context_offset = context_offset,
          .opcode = 0xE8,
          .covered = 5,
          .record_site = record_site};
}
constexpr RecordingSiteInfo DepthCall(std::string_view name, uint32_t rva, std::string_view context,
                                      draw_recording::Site record_site) {
  return BodyCall(draw_recording::Group::DEPTH_B, name, rva, context, record_site);
}
constexpr RecordingSiteInfo ColourCall(std::string_view name, uint32_t rva, std::string_view context,
                                       draw_recording::Site record_site, uint32_t context_offset = 12) {
  return BodyCall(draw_recording::Group::COLOUR_B, name, rva, context, record_site, context_offset);
}

// A colour body's state block: the instruction(s) its first 5 bytes (the cmp, or for body 2 the test, its short jump and the
// store) stand for, found by the bytes before; the exit is the block's end.
constexpr RecordingSiteInfo ColourStateBlock(std::string_view name, uint32_t rva, std::string_view context,
                                             uint32_t context_offset, uint32_t covered, uint32_t exit_rva,
                                             std::string_view exit_head) {
  return {.name = name,
          .group = draw_recording::Group::COLOUR_B,
          .kind = RecordingKind::OWN,
          .rva = rva,
          .context = context,
          .context_offset = context_offset,
          .opcode = 0xE9,
          .covered = covered,
          .exit_rva = exit_rva,
          .exit_head = exit_head,
          .record_site = draw_recording::Site::COLOUR_B_STATE};
}

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
     .context = VF68_FIRST_CONTEXT,
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
    {.name = "per-mesh loop cut",
     .group = draw_recording::Group::DEPTH_B,
     .kind = RecordingKind::OWN,
     .rva = DEPTH_B_CUT_RVA,
     .context = DEPTH_B_CUT_CONTEXT,
     .context_offset = DEPTH_B_CUT_OFFSET,
     .opcode = 0xE9,
     .covered = DEPTH_B_CUT_COVERED,
     .exit_rva = DEPTH_B_CUT_EXIT_RVA,
     .exit_head = DEPTH_B_CUT_EXIT,
     .record_site = draw_recording::Site::COUNT},
    DepthCall("depth body 0 shared-state call", 0x17b0b6b, VF68_FIRST_CONTEXT, draw_recording::Site::DEPTH_B_VF68),
    DepthCall("depth body 0 variation shared-state call", 0x17b0c06, DEPTH_B_VF68_VARIATION_CONTEXT,
                  draw_recording::Site::DEPTH_B_VF68),
    DepthCall("depth body 0 slot-0x64 call", 0x17b0c97, DEPTH_B_VF64_STACK_CONTEXT, draw_recording::Site::DEPTH_B_VF64),
    DepthCall("depth body 1 shared-state call", 0x17b0e2b, VF68_FIRST_CONTEXT, draw_recording::Site::DEPTH_B_VF68),
    DepthCall("depth body 1 variation shared-state call", 0x17b0ed2, DEPTH_B_VF68_VARIATION_CONTEXT,
                  draw_recording::Site::DEPTH_B_VF68),
    DepthCall("depth body 1 slot-0x64 call", 0x17b0f62, DEPTH_B_VF64_EBP_CONTEXT, draw_recording::Site::DEPTH_B_VF64),
    DepthCall("depth body 2 shared-state call", 0x17b10f7, VF68_FIRST_CONTEXT, draw_recording::Site::DEPTH_B_VF68),
    DepthCall("depth body 2 variation shared-state call", 0x17b11a2, DEPTH_B_VF68_VARIATION_CONTEXT,
                  draw_recording::Site::DEPTH_B_VF68),
    DepthCall("depth body 2 slot-0x64 call", 0x17b1233, DEPTH_B_VF64_STACK_CONTEXT, draw_recording::Site::DEPTH_B_VF64),
    DepthCall("depth body 3 shared-state call", 0x17b13c7, VF68_FIRST_CONTEXT, draw_recording::Site::DEPTH_B_VF68),
    DepthCall("depth body 3 variation shared-state call", 0x17b1472, DEPTH_B_VF68_VARIATION_CONTEXT,
                  draw_recording::Site::DEPTH_B_VF68),
    DepthCall("depth body 3 slot-0x64 call", 0x17b1503, DEPTH_B_VF64_STACK_CONTEXT, draw_recording::Site::DEPTH_B_VF64),
    DepthCall("depth body 4 shared-state call", 0x17b169b, VF68_FIRST_CONTEXT, draw_recording::Site::DEPTH_B_VF68),
    DepthCall("depth body 4 variation shared-state call", 0x17b1742, DEPTH_B_VF68_VARIATION_CONTEXT,
                  draw_recording::Site::DEPTH_B_VF68),
    DepthCall("depth body 4 slot-0x64 call", 0x17b17d2, DEPTH_B_VF64_EBP_CONTEXT, draw_recording::Site::DEPTH_B_VF64),
    ColourCall("colour body 0 shared-state call", 0x17aefe7, COLOUR_B_VF68_CONTEXT, draw_recording::Site::COLOUR_B_VF68),
    ColourCall("colour body 0 variation shared-state call", 0x17af0ab, COLOUR_B_VF68_VARIATION_CONTEXT,
               draw_recording::Site::COLOUR_B_VF68, 13),
    ColourCall("colour body 0 slot-0x64 call", 0x17af241, COLOUR_B_VF64_STACK_CONTEXT, draw_recording::Site::COLOUR_B_VF64),
    ColourStateBlock("colour body 0 state block", 0x17af139, COLOUR_B0_STATE_CONTEXT, 22, 5, 0x17af225, COLOUR_B_STATE_EXIT),
    ColourCall("colour body 1 shared-state call", 0x17af362, COLOUR_B_VF68_CONTEXT, draw_recording::Site::COLOUR_B_VF68),
    ColourCall("colour body 1 variation shared-state call", 0x17af42a, COLOUR_B_VF68_VARIATION_CONTEXT,
               draw_recording::Site::COLOUR_B_VF68, 13),
    ColourCall("colour body 1 slot-0x64 call", 0x17af6af, COLOUR_B_VF64_REGISTER_CONTEXT, draw_recording::Site::COLOUR_B_VF64),
    ColourStateBlock("colour body 1 state block", 0x17af4e4, COLOUR_B1_STATE_CONTEXT, 23, 5, 0x17af692,
                     COLOUR_B_STATE_RELOAD_EXIT),
    ColourCall("colour body 2 shared-state call", 0x17af7d2, COLOUR_B_VF68_CONTEXT, draw_recording::Site::COLOUR_B_VF68),
    ColourCall("colour body 2 variation shared-state call", 0x17af88e, COLOUR_B_VF68_VARIATION_CONTEXT,
               draw_recording::Site::COLOUR_B_VF68, 13),
    ColourCall("colour body 2 slot-0x64 call", 0x17afba1, COLOUR_B_VF64_STACK_CONTEXT, draw_recording::Site::COLOUR_B_VF64),
    ColourStateBlock("colour body 2 state block", 0x17af986, COLOUR_B2_STATE_CONTEXT, 22, 7, 0x17afb85, COLOUR_B_STATE_EXIT),
    ColourCall("colour body 3 shared-state call", 0x17afcbf, COLOUR_B_VF68_CONTEXT, draw_recording::Site::COLOUR_B_VF68),
    ColourCall("colour body 3 variation shared-state call", 0x17afd7b, COLOUR_B_VF68_VARIATION_CONTEXT,
               draw_recording::Site::COLOUR_B_VF68, 13),
    ColourCall("colour body 3 slot-0x64 call", 0x17aff23, COLOUR_B_VF64_STACK_CONTEXT, draw_recording::Site::COLOUR_B_VF64),
    ColourStateBlock("colour body 3 state block", 0x17afe15, COLOUR_B3_STATE_CONTEXT, 22, 5, 0x17aff07, COLOUR_B_STATE_EXIT),
    ColourCall("colour body 4 shared-state call", 0x17b0042, COLOUR_B_VF68_CONTEXT, draw_recording::Site::COLOUR_B_VF68),
    ColourCall("colour body 4 variation shared-state call", 0x17b010a, COLOUR_B_VF68_VARIATION_CONTEXT,
               draw_recording::Site::COLOUR_B_VF68, 13),
    ColourCall("colour body 4 slot-0x64 call", 0x17b038f, COLOUR_B_VF64_REGISTER_CONTEXT, draw_recording::Site::COLOUR_B_VF64),
    ColourStateBlock("colour body 4 state block", 0x17b01c4, COLOUR_B1_STATE_CONTEXT, 23, 5, 0x17b0372,
                     COLOUR_B_STATE_RELOAD_EXIT),
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
// this thunk: the callee's stack, `ret return_bytes`; the weighted loops' slot 0x68 and the depth bodies' 0x68 and 0x64). With
// `vtables` (group A) the 10 classes' vtables are checked in the record path; another one is a site id Record does not know,
// which flags the chunk (it is run again). With none (the B groups: the planner has already chosen the classes) Record takes the
// call as it is. `site` is the loop's site id.
inline uint8_t* BuildSharedStateCall(Assembler* assembler, const draw_recording::Shared* shared,
                                     const std::array<uint32_t, WEIGHTED_CLASSES.size()>* vtables, uint8_t slot,
                                     uint8_t return_bytes, draw_recording::Site site) {
  uint8_t* const entry = assembler->Cursor();
  EmitSlotTest(assembler, shared, 2, {0x8B, 0xD6});
  uint8_t* const to_record = assembler->Short(0x72);
  assembler->Bytes({0x8B, 0x40, slot, 0xFF, 0xE0});
  assembler->Bind(to_record);
  EmitFrameOpen(assembler);
  assembler->BytesAndOperand({0xBA}, static_cast<uint32_t>(site));
  if (vtables != nullptr) {
    std::array<uint8_t*, WEIGHTED_CLASSES.size()> known = {};
    for (size_t index = 0; index < vtables->size(); ++index) {
      assembler->BytesAndOperand({0x3D}, (*vtables)[index]);
      known[index] = assembler->Short(0x74);
    }
    assembler->BytesAndOperand({0xBA}, static_cast<uint32_t>(draw_recording::Site::COUNT));
    // The saved eax (above xmm0 and the 8 registers of pushad): 0, so that the loop's `or [esi + 0x69], al` adds nothing.
    assembler->Bytes({0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00});
    for (uint8_t* const displacement : known) {
      assembler->Bind(displacement);
    }
  }
  assembler->Bytes({0x8B, 0xCE});
  EmitRecordCall(assembler, shared);
  assembler->Bytes({0xC2, return_bytes, 0x00});
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

// Where an operand of a colour body's state block is at its site: one of the game's stack locals (esp-relative at the site), or
// the thunk's own frame (a word of a register the game holds: edi 4, ebx 20, ecx 28, eax 32; its second byte is bh / ah).
struct StateSource {
  bool frame;
  uint8_t offset;
};
// The operands of one colour body's block, decoded from the game's bytes. The block sets the snow constant (when its flag is on,
// from a byte: the tracker's flag byte and the constant's float), the shader resources (body 2), and the faction colours (the
// default ones when the body's old faction is not -1, or the item's faction when its flag is on); `cfg` is the game's stack local
// that holds the colour state the constants, resources and colours go to.
struct StateVariant {
  StateSource snow_flag;
  bool has_snow_value;
  StateSource snow_value;
  uint8_t cfg;
  bool has_textures;
  StateSource diffuse_flag;
  StateSource diffuse;
  StateSource normal_flag;
  StateSource normal;
  bool faction_flagged;
  StateSource faction_test;
  StateSource faction_value;
  StateSource item;
};
constexpr uint32_t STATE_SNOW_OBJECT = 0x2ac;
constexpr std::array<StateVariant, 5> STATE_VARIANTS = {{
    {.snow_flag = {false, 0x0f}, .has_snow_value = false, .cfg = 0x10, .faction_test = {true, 4}},
    {.snow_flag = {false, 0x11},
     .has_snow_value = true,
     .snow_value = {true, 33},
     .cfg = 0x18,
     .faction_flagged = true,
     .faction_test = {false, 0x13},
     .faction_value = {false, 0x20},
     .item = {true, 20}},
    {.snow_flag = {true, 21},
     .has_snow_value = true,
     .snow_value = {true, 20},
     .cfg = 0x18,
     .has_textures = true,
     .diffuse_flag = {false, 0x13},
     .diffuse = {false, 0x24},
     .normal_flag = {false, 0x10},
     .normal = {false, 0x1c},
     .faction_flagged = true,
     .faction_test = {false, 0x11},
     .faction_value = {false, 0x28},
     .item = {false, 0x20}},
    {.snow_flag = {false, 0x0e}, .has_snow_value = true, .snow_value = {true, 28}, .cfg = 0x10, .faction_test = {false, 0x1c}},
    {.snow_flag = {false, 0x11},
     .has_snow_value = true,
     .snow_value = {true, 33},
     .cfg = 0x18,
     .faction_flagged = true,
     .faction_test = {false, 0x13},
     .faction_value = {false, 0x20},
     .item = {true, 20}},
}};
static_assert(static_cast<size_t>(RecordingSite::COLOUR_B4_STATE) - static_cast<size_t>(RecordingSite::COLOUR_B0_STATE)
              == 4 * (STATE_VARIANTS.size() - 1));

// A colour body's state block (the site's covered instructions start it). The direct path is the game's covered instructions
// and on. The record path lays the block's operands on the stack above the frame for Record (a draw_recording::StateBlock: which
// parts are on, what the tracker takes, what the replay calls with) and goes on at the block's end (`resume`); the registers it
// leaves are the site's, which the game's code after the block does not read (it rewrites each first, checked per body). The
// game's esp is in ebx, the frame's words are esp-relative; eax is the scratch.
inline uint8_t* BuildState(Assembler* assembler, const draw_recording::Shared* shared, const StateVariant& variant,
                           const uint8_t* at, uint32_t covered, const uint8_t* resume) {
  using draw_recording::StateBlock;
  constexpr uint8_t BLOCK = 40;  // the block's place above the frame (the xmm0 word, 8 registers, eflags)
  constexpr uint8_t BLOCK_SIZE = sizeof(StateBlock);
  const auto field = [](size_t offset) { return static_cast<uint8_t>(BLOCK + offset); };
  // `opcode`, then the operand (ModRM with `reg`) at the source.
  const auto operand = [&](std::initializer_list<uint8_t> opcode, uint8_t reg, const StateSource& source) {
    assembler->Bytes(opcode);
    if (source.frame) {
      assembler->Bytes({static_cast<uint8_t>(0x44 | reg << 3), 0x24, source.offset});
    } else {
      assembler->Bytes({static_cast<uint8_t>(0x43 | reg << 3), source.offset});
    }
  };
  const auto set_bits = [&](size_t offset, uint8_t bits) { assembler->Bytes({0x83, 0x4C, 0x24, field(offset), bits}); };
  const auto store = [&](size_t offset) { assembler->Bytes({0x89, 0x44, 0x24, field(offset)}); };
  // eax = the colour state + `add`, stored.
  const auto store_cfg = [&](size_t offset, uint32_t add) {
    operand({0x8B}, 0, {false, variant.cfg});
    assembler->BytesAndOperand({0x05}, add);
    store(offset);
  };
  uint8_t* const entry = assembler->Cursor();
  EmitSlotTest(assembler, shared, 2, {0x8B, 0xD6});
  uint8_t* const to_record = assembler->Short(0x72);
  if (covered == 5) {
    assembler->Copy(at, covered);
  } else {
    // test bh, bh; its short je (made long); mov [esi + 0x10], bl
    assembler->Copy(at, 2);
    assembler->Bytes({0x0F});
    assembler->Relative(0x84, at + 4 + static_cast<int8_t>(at[3]));
    assembler->Copy(at + 4, 3);
  }
  assembler->Relative(0xE9, at + covered);
  assembler->Bind(to_record);
  assembler->Bytes({0x83, 0xEC, BLOCK_SIZE});
  EmitFrameOpen(assembler);
  // lea ebx, [esp + the game's esp]; the block zeroed (lea edi; xor eax, eax; mov ecx, 13; rep stosd).
  assembler->Bytes({0x8D, 0x5C, 0x24, static_cast<uint8_t>(BLOCK + BLOCK_SIZE)});
  assembler->Bytes({0x8D, 0x7C, 0x24, BLOCK, 0x33, 0xC0, 0xB9, BLOCK_SIZE / 4, 0x00, 0x00, 0x00, 0xF3, 0xAB});
  // The snow constant: the tracker's flag byte and the constant's float are the byte (none: 0).
  operand({0x80}, 7, variant.snow_flag);
  assembler->Bytes({0x00});
  uint8_t* const no_snow = assembler->Short(0x74);
  set_bits(offsetof(StateBlock, parts), draw_recording::STATE_SNOW);
  set_bits(offsetof(StateBlock, writes), draw_recording::STATE_WRITE_FLAGS);
  if (variant.has_snow_value) {
    operand({0x0F, 0xB6}, 0, variant.snow_value);
    store(offsetof(StateBlock, flags));
    // cvtsi2ss xmm0, eax; movss [snow_bits], xmm0
    assembler->Bytes({0xF3, 0x0F, 0x2A, 0xC0, 0xF3, 0x0F, 0x11, 0x44, 0x24, field(offsetof(StateBlock, snow_bits))});
  }
  store_cfg(offsetof(StateBlock, snow_object), STATE_SNOW_OBJECT);
  assembler->Bind(no_snow);
  if (variant.has_textures) {
    // The tracker takes both textures when either is bound: the one that is not keeps the tracker's.
    assembler->Bytes({0x8B, 0x46, 0x04});
    store(offsetof(StateBlock, diffuse));
    assembler->Bytes({0x8B, 0x46, 0x08});
    store(offsetof(StateBlock, normal));
    const auto texture = [&](const StateSource& flag, const StateSource& value, uint8_t part, size_t slot_field, size_t value_field,
                             draw_recording::Site slot_site) {
      operand({0x80}, 7, flag);
      assembler->Bytes({0x00});
      uint8_t* const unbound = assembler->Short(0x74);
      set_bits(offsetof(StateBlock, parts), part);
      set_bits(offsetof(StateBlock, writes), draw_recording::STATE_WRITE_TEXTURES);
      operand({0x8B}, 0, value);
      store(value_field);
      store_cfg(slot_field, draw_recording::internal::SITES[static_cast<size_t>(slot_site)].operand);
      assembler->Bind(unbound);
    };
    texture(variant.diffuse_flag, variant.diffuse, draw_recording::STATE_DIFFUSE, offsetof(StateBlock, diffuse_slot),
            offsetof(StateBlock, diffuse), draw_recording::Site::COLOUR_SRV_DIFFUSE);
    texture(variant.normal_flag, variant.normal, draw_recording::STATE_NORMAL, offsetof(StateBlock, normal_slot),
            offsetof(StateBlock, normal), draw_recording::Site::COLOUR_SRV_NORMAL);
  }
  // The faction colours: the default ones (faction -1) when the old faction is not -1, or the item's faction when its flag is on.
  if (variant.faction_flagged) {
    operand({0x80}, 7, variant.faction_test);
    assembler->Bytes({0x00});
  } else {
    operand({0x83}, 7, variant.faction_test);
    assembler->Bytes({0xFF});
  }
  uint8_t* const no_faction = assembler->Short(0x74);
  set_bits(offsetof(StateBlock, parts), draw_recording::STATE_FACTION);
  set_bits(offsetof(StateBlock, writes), draw_recording::STATE_WRITE_FACTION);
  if (variant.faction_flagged) {
    operand({0x8B}, 0, variant.faction_value);
    store(offsetof(StateBlock, faction));
    // The item's mask: mov eax, [item + 0x34]; the node: mov eax, [ebp + 0x10]
    operand({0x8B}, 0, variant.item);
    assembler->Bytes({0x8B, 0x40, 0x34});
    store(offsetof(StateBlock, mask));
    assembler->Bytes({0x8B, 0x45, 0x10});
    store(offsetof(StateBlock, node));
  } else {
    assembler->Bytes({0xC7, 0x44, 0x24, field(offsetof(StateBlock, faction)), 0xFF, 0xFF, 0xFF, 0xFF});
  }
  store_cfg(offsetof(StateBlock, state), 0);
  assembler->Bind(no_faction);
  assembler->BytesAndOperand({0xBA}, static_cast<uint32_t>(draw_recording::Site::COLOUR_B_STATE));
  assembler->Bytes({0x8B, 0xCE});
  EmitRecordCall(assembler, shared);
  assembler->Bytes({0x83, 0xC4, BLOCK_SIZE});
  assembler->Relative(0xE9, resume);
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

// The B groups' cut (the folded driver's per-mesh loop): the loop's two loads (`covered`, 8 bytes, 5 written over), the game's
// empty-round test (`ja` to `empty`), then DispatchMesh with the 8 values the loop calls each mesh with (right to left, each
// offset at the site's esp and shifted by the pushes before it), and the game's state after its loop (esi the list type, edi the
// last mesh, ebx past it) at `after_loop`.
inline uint8_t* BuildMeshCut(Assembler* assembler, const draw_recording::Shared* shared, const uint8_t* covered,
                             size_t covered_size, const uint8_t* empty, const uint8_t* after_loop) {
  uint8_t* const entry = assembler->Cursor();
  assembler->Copy(covered, covered_size);
  assembler->Bytes({0x3B, 0xDF, 0x0F});
  assembler->Relative(0x87, empty);
  assembler->BytesAndOperand({0xF0, 0xFF, 0x05}, Address(&shared->active));
  // push edi (the last), ebx (the first), the sorted list, the node, the list type, the view, the device, the tracker (ebp).
  assembler->Bytes({0x57, 0x53, 0xFF, 0x74, 0x24, 0x30, 0xFF, 0x74, 0x24, 0x3C, 0xFF, 0x74, 0x24, 0x20});
  assembler->Bytes({0xFF, 0xB4, 0x24, 0x88, 0x00, 0x00, 0x00, 0xFF, 0xB4, 0x24, 0x88, 0x00, 0x00, 0x00, 0x55});
  assembler->BytesAndOperand({0xFF, 0x15}, Address(&shared->dispatch_mesh));
  assembler->BytesAndOperand({0xF0, 0xFF, 0x0D}, Address(&shared->active));
  assembler->Bytes({0x8B, 0x74, 0x24, 0x10, 0x8B, 0x7C, 0x24, 0x1C, 0x8D, 0x5F, 0x01});
  assembler->Relative(0xE9, after_loop);
  return entry;
}

// The loop groups' cuts are built first, at the start of the page: a removal waits while a thread is in it (it is about to run
// the dispatcher, whose chunks need the flush hook).
constexpr uint64_t RECORDING_CUTS = RecordingBit(RecordingSite::DEPTH_CUT) | RecordingBit(RecordingSite::COLOUR_CUT)
                                    | RecordingBit(RecordingSite::DEPTH_B_CUT);

// The group's own sites and its fallback flush sites, by draw_recording::Group. The per-mesh cut is both B groups' (it is
// written when either is on, and stands down with both).
constexpr std::array<uint64_t, static_cast<size_t>(draw_recording::Group::COUNT)> RECORDING_OWN = {
    GroupSites(draw_recording::Group::DEPTH_A, RecordingKind::OWN),
    GroupSites(draw_recording::Group::COLOUR_A, RecordingKind::OWN),
    GroupSites(draw_recording::Group::DEPTH_B, RecordingKind::OWN),
    GroupSites(draw_recording::Group::COLOUR_B, RecordingKind::OWN) | RecordingBit(RecordingSite::DEPTH_B_CUT),
    0};
constexpr std::array<uint64_t, static_cast<size_t>(draw_recording::Group::COUNT)> RECORDING_FALLBACK = {
    GroupSites(draw_recording::Group::DEPTH_A, RecordingKind::FALLBACK),
    GroupSites(draw_recording::Group::COLOUR_A, RecordingKind::FALLBACK),
    GroupSites(draw_recording::Group::DEPTH_B, RecordingKind::FALLBACK),
    GroupSites(draw_recording::Group::COLOUR_B, RecordingKind::FALLBACK),
    0};
constexpr uint64_t RECORDING_ENTRY_BIT = RecordingBit(RecordingSite::FLUSH_ENTRY);
static_assert(RECORDING_SITE_COUNT == RECORDING_SITES);

// The bytes heapfix patches or checks are in heapfix_ranges.hpp. No site's bytes (what it writes and the instructions it covers) may
// touch any of them, and none of the bytes a site, its callee or its exit is found by may touch a patched one (it may be in when
// Prepare runs). The ranges of the ROWS (checked, not written) are read by our own pieces (the accessor, the list helper): those
// may lie in them.
constexpr size_t PatternSize(std::string_view pattern) { return (pattern.size() + 1) / 3; }
// The timing hooks on our own entries (HOOKED_ENTRIES) are refused at run time (RecordingEntriesFree, in Prepare and every turn-on),
// so they are not held against the sites; every other timing hook is.
constexpr std::array<bool, HEAPFIX_TIMING_HOOKS.size()> TIMING_HOOK_REFUSED = [] {
  std::array<bool, HEAPFIX_TIMING_HOOKS.size()> refused = {};
  for (size_t row = 0; row < refused.size(); ++row) {
    for (const uint32_t entry : HOOKED_ENTRIES) {
      refused[row] |= (entry == HEAPFIX_TIMING_HOOKS[row].rva);
    }
  }
  return refused;
}();
constexpr bool TouchesTimingHook(uint32_t rva, uint32_t size) {
  for (size_t row = 0; row < HEAPFIX_TIMING_HOOKS.size(); ++row) {
    if (!TIMING_HOOK_REFUSED[row] && Touches(rva, size, HEAPFIX_TIMING_HOOKS[row])) return true;
  }
  return false;
}
// A site clears heapfix when its bytes touch no range and the bytes it is found by touch no patched range.
constexpr bool SiteClearOfHeapfix(const RecordingSiteInfo& info) {
  const uint32_t site_size = std::max<uint32_t>(info.covered, SITE_SIZE);
  const uint32_t found_by[3][2] = {
      {info.rva - info.context_offset, static_cast<uint32_t>(PatternSize(info.context))},
      {info.callee_rva, static_cast<uint32_t>(PatternSize(info.callee_head))},
      {info.exit_rva, static_cast<uint32_t>(PatternSize(info.exit_head))}};
  for (const HeapfixRange& range : HEAPFIX_PATCHED) {
    if (Touches(info.rva, site_size, range)) return false;
    for (const auto& window : found_by) {
      if (window[1] != 0 && Touches(window[0], window[1], range)) return false;
    }
  }
  if (TouchesTimingHook(info.rva, site_size)) return false;
  for (const auto& window : found_by) {
    if (window[1] != 0 && TouchesTimingHook(window[0], window[1])) return false;
  }
  for (const HeapfixRange& range : HEAPFIX_CHECKED) {
    if (Touches(info.rva, site_size, range)) return false;
  }
  return true;
}
static_assert([] {
  for (const RecordingSiteInfo& info : RECORDING_INFO) {
    if (!SiteClearOfHeapfix(info)) return false;
  }
  return true;
}());
// The other bytes C reads: the slot-0x138 thunks and the dispatchers the class tables are decoded from touch no patched or
// checked range; the rest (the accessor and the list helper lie in heapfix's rows, which only checks them) touch no patched one.
static_assert([] {
  const auto patched = [](uint32_t rva, uint32_t size) {
    for (const HeapfixRange& range : HEAPFIX_PATCHED) {
      if (Touches(rva, size, range)) return true;
    }
    return TouchesTimingHook(rva, size);
  };
  const auto checked = [](uint32_t rva, uint32_t size) {
    for (const HeapfixRange& range : HEAPFIX_CHECKED) {
      if (Touches(rva, size, range)) return true;
    }
    return false;
  };
  const std::array<HeapfixRange, 2> decoded = {{{0x1827830, 0x1827a40 - 0x1827830}, {DISPATCHER_FIRST_RVA, DISPATCHER_COUNT * DISPATCHER_STRIDE}}};
  for (const HeapfixRange& window : decoded) {
    if (patched(window.rva, window.size) || checked(window.rva, window.size)) return false;
  }
  const std::array<HeapfixRange, 6> read = {{{ACCESSOR_RVA, PatternSize(ACCESSOR_HEAD)},
                                             {LIST_HELPER_RVA, PatternSize(LIST_HELPER_HEAD)},
                                             {WEIGHTED_VF64, PatternSize(VF64_STUB_HEAD)},
                                             {FLUSH_RVA, PatternSize(FLUSH_HEAD)},
                                             {FLUSH_EMPTY_RVA, PatternSize(FLUSH_EMPTY)},
                                             {TEXTURE_VIEW_RVA, PatternSize(TEXTURE_VIEW_HEAD)}}};
  for (const HeapfixRange& window : read) {
    if (patched(window.rva, window.size)) return false;
  }
  for (const ColourPieceInfo& piece : COLOUR_PIECES) {
    if (patched(piece.rva - piece.context_offset, PatternSize(piece.context))
        || (piece.callee_rva != 0 && patched(piece.callee_rva, PatternSize(piece.callee_head)))) {
      return false;
    }
  }
  return true;
}());

static const uint8_t* recording_base = nullptr;
// The bytes of the page the thunks take (the test reads it: RECORDING_CODE_SIZE must hold with room to spare).
static size_t recording_code_used = 0;

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

// The loop groups (bits of draw_recording::Group) whose own sites and flush path (the entry hook, else the call sites) are
// all written now: the dispatcher runs only these groups' rounds, whatever the rows say (a group that stood down at run
// time, a failed write, a pause with no clear moment leaves its sites out, and a round the dispatcher records then would
// run a loop whose sites are not there).
inline uint32_t RecordingGroupsIn() {
  uint32_t groups = 0;
  for (size_t group = 0; group < RECORDING_OWN.size(); ++group) {
    const uint64_t flush =
        ((recording_written & RECORDING_ENTRY_BIT) != 0 ? RECORDING_ENTRY_BIT : RECORDING_FALLBACK[group]);
    if (RECORDING_OWN[group] != 0 && flush != 0 && ((RECORDING_OWN[group] | flush) & ~recording_written) == 0) {
      groups |= 1u << group;
    }
  }
  return groups;
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
    const size_t size = PatternSize(pattern);
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
  // The virtual call that does nothing: the weighted classes' vf64, and the B classes' (it stays shared: group A needs it).
  if (shared_ok && !holds(base + WEIGHTED_VF64, VF64_STUB_HEAD)) {
    shared_ok = false;
    refuse("the virtual call that does nothing");
  }
  std::array<uint32_t, WEIGHTED_CLASSES.size()> vtables = {};
  const auto address_at = [&](uint32_t rva) { return Address(base + rva); };
  for (size_t index = 0; index < WEIGHTED_CLASSES.size() && shared_ok; ++index) {
    const WeightedClass& weighted = WEIGHTED_CLASSES[index];
    const uint8_t* const vtable = base + weighted.vtable;
    const auto slot = [&](uint32_t offset) { return read32(vtable + offset); };
    if (vtable + 0x70 > image_end || slot(0x54) != address_at(WEIGHTED_VF54) || slot(0x58) != address_at(weighted.vf58)
        || slot(0x60) != address_at(weighted.vf60) || slot(0x64) != address_at(WEIGHTED_VF64)
        || slot(0x68) != address_at(weighted.vf68) || slot(0x6c) != address_at(WEIGHTED_VF6C)) {
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

  // The B groups: the sorted list's accessor (every dispatcher walks its meshes with it, its direct run included) and the table of
  // the routed classes, decoded from the slot-0x138 thunks and the dispatchers they call (the list's base from the thunk, the
  // family from the dispatcher's place, its depth and colour bodies from its calls). A class whose vtable, thunk or dispatcher is
  // not as expected is left out (its meshes run directly) and named; the B sites go in only with the accessor and at least one
  // class that records in depth.
  bool b_ok = shared_ok;
  if (b_ok && !holds(base + ACCESSOR_RVA, ACCESSOR_HEAD)) {
    b_ok = false;
    refuse("the sorted list's accessor");
  }
  uint32_t depth_classes = 0;
  const auto is_body = [&](const uint8_t* call, const std::array<uint32_t, 5>& bodies) {
    return std::ranges::find(bodies, static_cast<uint32_t>(target(call) - base)) != bodies.end();
  };
  for (const RoutedClass& routed : ROUTED_CLASSES) {
    if (!b_ok) break;
    const uint8_t* const vtable = base + routed.vtable;
    const auto slot = [&](uint32_t offset) { return read32(vtable + offset); };
    const uint8_t* const thunk = base + routed.thunk;
    const bool standard = holds(thunk, SLOT138_THUNK);
    const bool decodable = standard || holds(thunk, SLOT138_SPECIAL_THUNK);
    const uint8_t* const dispatcher =
        (decodable ? target(thunk + (standard ? SLOT138_THUNK_CALL : SLOT138_SPECIAL_CALL)) : nullptr);
    const uint32_t offset = (decodable ? Address(dispatcher) - Address(base + DISPATCHER_FIRST_RVA) : 0);
    const uint32_t list_base = (decodable ? read32(thunk + (standard ? SLOT138_THUNK_BASE : SLOT138_SPECIAL_BASE)) : 0);
    const bool dispatcher_ok =
        decodable && offset % DISPATCHER_STRIDE == 0 && offset / DISPATCHER_STRIDE < DISPATCHER_COUNT
        && holds(dispatcher, DISPATCHER_BYTES)
        && target(dispatcher + DISPATCHER_LIST_CALL) == base + LIST_HELPER_RVA
        && holds(base + LIST_HELPER_RVA, LIST_HELPER_HEAD)
        && is_body(dispatcher + DISPATCHER_DEPTH_CALL, DEPTH_B_BODIES)
        && is_body(dispatcher + DISPATCHER_COLOUR_CALL, COLOUR_B_BODIES);
    if (!dispatcher_ok || list_base >= 0x400 || vtable + MESH_VF138 + 4 > image_end
        || slot(0x54) != address_at(routed.vf54) || slot(0x58) != address_at(routed.vf58)
        || slot(0x60) != address_at(routed.vf60) || slot(0x64) != address_at(routed.vf64)
        || slot(0x68) != address_at(routed.vf68) || slot(0x6c) != address_at(ROUTED_VF6C)
        || slot(MESH_VF138) != address_at(routed.thunk) || calls.class_count >= calls.classes.size()) {
      refuse(std::format("class {:#x}", routed.vtable + 0x10000000));
      continue;
    }
    calls.classes[calls.class_count++] = {.vtable = Address(vtable),
                                          .base = list_base,
                                          .depth_body = Address(target(dispatcher + DISPATCHER_DEPTH_CALL)),
                                          .colour_body = Address(target(dispatcher + DISPATCHER_COLOUR_CALL)),
                                          .family = offset / DISPATCHER_STRIDE,
                                          .depth = routed.depth,
                                          .colour = routed.colour};
    depth_classes += (routed.depth ? 1 : 0);
  }
  if (b_ok && depth_classes == 0) {
    b_ok = false;
    refuse("the routed classes");
  }
  if (!b_ok) {
    calls.class_count = 0;
  }

  recording_page = nullptr;
  if (shared_ok) {
    recording_page = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, RECORDING_CODE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  }
  Assembler assembler(std::span<uint8_t>(recording_page, recording_page == nullptr ? 0 : RECORDING_CODE_SIZE));
  std::array<const uint8_t*, RECORDING_SITE_COUNT> thunks = {};
  // The B groups' shared-state calls of one form (a slot, the bytes it returns with, the id Record gets) are one thunk.
  std::array<const uint8_t*, static_cast<size_t>(draw_recording::Site::COUNT)> shared_state_thunks = {};
  const auto shared_state_call = [&](uint8_t slot, uint8_t return_bytes, draw_recording::Site site) {
    const uint8_t*& thunk = shared_state_thunks[static_cast<size_t>(site)];
    if (thunk == nullptr) {
      thunk = BuildSharedStateCall(&assembler, shared, nullptr, slot, return_bytes, site);
    }
    return thunk;
  };
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
          || ((info.group == draw_recording::Group::COLOUR_A || info.group == draw_recording::Group::COLOUR_B)
              && info.kind == RecordingKind::OWN && !colour_ok)
          || ((info.group == draw_recording::Group::DEPTH_B || info.group == draw_recording::Group::COLOUR_B) && !b_ok)) {
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
          thunks[index] = BuildSharedStateCall(&assembler, shared, &vtables, 0x68, 8, info.record_site);
          break;
        case RecordingSite::DEPTH_B_CUT:
          thunks[index] = BuildMeshCut(&assembler, shared, at, info.covered, landing + DEPTH_B_CUT_EMPTY_OFFSET, landing);
          break;
        case RecordingSite::DEPTH_B0_VF68:
        case RecordingSite::DEPTH_B0_VARIATION:
        case RecordingSite::DEPTH_B1_VF68:
        case RecordingSite::DEPTH_B1_VARIATION:
        case RecordingSite::DEPTH_B2_VF68:
        case RecordingSite::DEPTH_B2_VARIATION:
        case RecordingSite::DEPTH_B3_VF68:
        case RecordingSite::DEPTH_B3_VARIATION:
        case RecordingSite::DEPTH_B4_VF68:
        case RecordingSite::DEPTH_B4_VARIATION:
        case RecordingSite::COLOUR_B0_VF68:
        case RecordingSite::COLOUR_B0_VARIATION:
        case RecordingSite::COLOUR_B1_VF68:
        case RecordingSite::COLOUR_B1_VARIATION:
        case RecordingSite::COLOUR_B2_VF68:
        case RecordingSite::COLOUR_B2_VARIATION:
        case RecordingSite::COLOUR_B3_VF68:
        case RecordingSite::COLOUR_B3_VARIATION:
        case RecordingSite::COLOUR_B4_VF68:
        case RecordingSite::COLOUR_B4_VARIATION:
          thunks[index] = shared_state_call(0x68, 8, info.record_site);
          break;
        case RecordingSite::DEPTH_B0_VF64:
        case RecordingSite::DEPTH_B1_VF64:
        case RecordingSite::DEPTH_B2_VF64:
        case RecordingSite::DEPTH_B3_VF64:
        case RecordingSite::DEPTH_B4_VF64:
        case RecordingSite::COLOUR_B0_VF64:
        case RecordingSite::COLOUR_B1_VF64:
        case RecordingSite::COLOUR_B2_VF64:
        case RecordingSite::COLOUR_B3_VF64:
        case RecordingSite::COLOUR_B4_VF64:
          thunks[index] = shared_state_call(0x64, 0xc, info.record_site);
          break;
        case RecordingSite::COLOUR_B0_STATE:
        case RecordingSite::COLOUR_B1_STATE:
        case RecordingSite::COLOUR_B2_STATE:
        case RecordingSite::COLOUR_B3_STATE:
        case RecordingSite::COLOUR_B4_STATE:
          thunks[index] = BuildState(
              &assembler, shared, STATE_VARIANTS[(index - static_cast<size_t>(RecordingSite::COLOUR_B0_STATE)) / 4], at,
              info.covered, landing);
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
  recording_code_used = static_cast<size_t>(assembler.Cursor() - recording_page);
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
  // The B groups' dispatcher runs the game's meshes through the accessor on every path, its own direct one included: it is set
  // with the class table, only when the B sites can go in (their cut is the only way to it).
  calls.accessor = (b_ok ? Address(base + ACCESSOR_RVA) : 0);
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
