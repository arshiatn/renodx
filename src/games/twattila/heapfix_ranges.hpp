/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// The bytes of empire.retail.dll (Steam 1.6.0, x86) that the Attila CPU Performance Fix (heapfix) patches or checks, by
// RVA: one table for every engine-code feature that writes into the game's code (Threaded Draw Recording's sites, the
// effect update's). Included from the middle of engine_code.hpp's internal namespace (after its own includes). A
// feature's written and covered bytes may touch none of these ranges, and the bytes a feature finds its sites by may
// touch no patched one (heapfix may already be in when the feature prepares). The rows come from every module of
// heapfix (the game's heap, the pool lookup, the instance, loop, effect and sort fixes, the timing hooks, the script
// extension) that writes or checks the game's code; the ones that scan for their bytes at run time (x87fix) have no
// fixed rows. The ranges are heapfix's own: it checks them once, when the game's DLL loads, before any feature
// prepares.

struct HeapfixRange {
  uint32_t rva;
  uint32_t size;
};

// The bytes heapfix writes in every build it runs in (the jump over the first instructions of a function or the
// replaced call).
constexpr std::array<HeapfixRange, 42> HEAPFIX_PATCHED = {{
    // The six sites: the instance prepare, the two sort calls, the list reset and its last call, the effect walk's
    // call.
    {0x17B92D0, 5}, {0x17B9297, 5}, {0x17B9197, 5}, {0x1811F00, 5}, {0x18124E8, 5}, {0x192D2DA, 5},
    // loopfetch: the loops' fetch of the next item, the animation batching and the two record copies.
    {0x17B1840, 9}, {0x17B0450, 6}, {0x17B1560, 6}, {0x17B0FC0, 6}, {0x17B0065, 6}, {0x17AFCE2, 6}, {0x17AF7F5, 6},
    {0x182E0DA, 9}, {0x17DAB90, 8}, {0x182C9A3, 8},
    // batchfix, per loop: the compare (6 bytes patched) and the reset (7).
    {0x17B0466, 6}, {0x17B04BF, 7}, {0x17B0FD9, 6}, {0x17B1032, 7}, {0x17B12A9, 6}, {0x17B1302, 7}, {0x17B1579, 6},
    {0x17B15D2, 7},
    // heightcache (the height query's entry), memfast (the memcpy's), fps (the frame function's), texfix (the
    // compression's), the two sortfix sorts, uikeep (a call in the queue step) and the bridge's record copy call.
    {0x18F9150, 6}, {0x1A27CA0, 6}, {0xC1950, 10}, {0x18E8390, 5}, {0x17AD5D0, 5}, {0x17ACB50, 5}, {0x174CD88, 5},
    {0x17DABAF, 5},
    // campfix: the two fog tests, the terrain height and the curve table (the whole range it compares and rewrites).
    {0xD79AC8, 36}, {0xD79B12, 36}, {0x189B363, 36}, {0x174A8BD, 86},
    // The game's heap (dlmalloc, dlfree, dlrealloc, dltrim: the length is the instructions moved into the trampoline)
    // and the two entries of the pool lookup.
    {0x112D80, 10}, {0x112730, 8}, {0x113040, 5}, {0x1126E0, 7}, {0x7B6F60, 5}, {0x7B7070, 5},
}};
// The timing hooks (passtime): a jump over the first bytes of each of 53 functions, and the scene node probe's (the
// length is the instructions copied into the trampoline). Dev builds put them on every row; a release build with the
// DLSS add-on's bridge only on the stage driver (+0x176A450) and the camera (+0x177BB10). A feature that reads or hooks
// one of these entries must refuse the jump at run time: the rows are kept apart so that a clearance check can name
// them.
constexpr std::array<HeapfixRange, 54> HEAPFIX_TIMING_HOOKS = {{
    {0xC1C10, 10}, {0x4552F0, 5}, {0x3FEA50, 6}, {0x1766B0, 7}, {0x42D9C0, 5}, {0x4790D0, 6}, {0x1897A30, 6},
    {0x1768FC0, 6}, {0x1778E90, 5}, {0x1768A00, 6}, {0x176A450, 5}, {0x19237A0, 5}, {0x179E890, 8}, {0x192B640, 6},
    {0x16862B0, 5}, {0x170AF80, 5}, {0x1748A80, 5}, {0x179A6B0, 7}, {0x186BE70, 10}, {0x1893CC0, 6}, {0x182E080, 8},
    {0x17DB0F0, 7}, {0x17293B0, 9}, {0x173EED0, 6}, {0x172B570, 6}, {0x172B690, 10}, {0x1785C60, 6}, {0x1749620, 7},
    {0x16FCD40, 7}, {0x176DF00, 5}, {0x1779170, 6}, {0x8F91C0, 6}, {0x1779250, 8}, {0x17FA310, 6}, {0x177BB10, 5},
    {0x17AEF40, 6}, {0x17AF2B0, 6}, {0x17AF720, 6}, {0x17AFC10, 6}, {0x17AFF90, 6}, {0x17B0400, 6}, {0x17B0F80, 7},
    {0x17B1250, 7}, {0x17B1520, 7}, {0x17B17F0, 8}, {0x17B1A10, 6}, {0x17B20E0, 8}, {0x17B2C80, 8}, {0x17B3280, 8},
    {0x17B3880, 8}, {0x17B4480, 8}, {0x17B09F0, 7}, {0x1778390, 8},
    {0x1722420, 7},
}};
// The bytes checked and not patched: the instances rows and the effect rows (the old 26, then the 10 new ones),
// batchfix's checks beside its patches (the compare's other 17 bytes, the 2 after the reset, the item load, the field
// load), the whole functions texfix, sortfix and uikeep compare (their patches are above), the Lua functions and the
// two reads of the script extension, and the first 12 bytes of the four heap functions and the 8 pool functions hashed
// whole.
constexpr std::array<HeapfixRange, 82> HEAPFIX_CHECKED = {{
    {0x17B92D0, 171}, {0x17B9270, 53}, {0x17B92B0, 19}, {0x182C810, 17}, {0x186BC80, 94}, {0x186BDC0, 25},
    {0x183E430, 69},
    {0x184BAF0, 21}, {0x178FD90, 264}, {0xD3420, 144}, {0xCFBB0, 4}, {0xCFCD0, 4}, {0x1719160, 13}, {0x17B9170, 53},
    {0x17B90F0, 53}, {0x17B9130, 53}, {0x17B91F0, 53}, {0x186BD20, 25}, {0x186BD60, 25}, {0x186BCE0, 25},
    {0x186BD00, 25},
    {0x186BD40, 25}, {0x1906300, 27}, {0x1906320, 21}, {0xC46B0, 3}, {0x183E3F0, 54},
    {0x1811F0C, 1500}, {0x189B9A0, 180}, {0x192D296, 73}, {0x1870BD0, 56}, {0x17AE420, 89}, {0x176CED0, 241},
    {0x17AE370, 89},
    {0x17F8355, 127}, {0x1833DC0, 194}, {0x1833D00, 158},
    {0x17B046C, 0x11}, {0x17B04C6, 2}, {0x17B0456, 3}, {0x17B05A3, 6},
    {0x17B0FDF, 0x11}, {0x17B1039, 2}, {0x17B0FCB, 3}, {0x17B1117, 3},
    {0x17B12AF, 0x11}, {0x17B1309, 2}, {0x17B129B, 3}, {0x17B13E7, 3},
    {0x17B157F, 0x11}, {0x17B15D9, 2}, {0x17B156B, 3}, {0x17B16AD, 3},
    // texfix: the compression function and its index function. sortfix: the two sorts (630 bytes each). uikeep: the
    // queue step, the delete it calls and the acquire whose count handling it relies on.
    {0x18E8390, 0x342}, {0x192EEE0, 0x22A}, {0x17AD5D0, 630}, {0x17ACB50, 630}, {0x174CCA0, 0xED}, {0x174CC20, 0x27},
    {0x175D5D0, 0xEC},
    // The script extension: the Lua functions it calls that it checks by their first bytes (gettop, settop, pushstring,
    // getfield, setfield, pushcclosure, pcall, tolstring, loadbuffer).
    {0x12C67B0, 10}, {0x12C7210, 8}, {0x12C6D20, 8}, {0x12C66D0, 10}, {0x12C70D0, 10}, {0x12C6BA0, 10}, {0x12C6B00, 10},
    {0x12C7380, 10}, {0x12C7E20, 10},
    // The script extension's two functions that read its state (the shared lua_State pointer, the environment list's
    // first node).
    {0x1626190, 10}, {0x16238A0, 6},
    // The heap functions' first 12 bytes, and the pool functions (hashed whole, 267 to 311 bytes each).
    {0x112D80, 12}, {0x112730, 12}, {0x113040, 12}, {0x1126E0, 12},
    {0x7B6F60, 267}, {0x7B6800, 308}, {0x7B6E40, 279}, {0x7B6A80, 310}, {0x7B7070, 311}, {0x7B6BC0, 311},
    {0x7B6D00, 309},
    {0x7B6940, 309},
}};

constexpr bool Touches(uint32_t rva, uint32_t size, const HeapfixRange& range) {
  return rva < range.rva + range.size && range.rva < rva + size;
}
using effect_update::PatternSize;  // the byte count of a pattern (the effect update's sites own the formula)
// Every row is filled in (a short list would leave empty rows at the end).
static_assert([] {
  for (const HeapfixRange& range : HEAPFIX_PATCHED) {
    if (range.size == 0) return false;
  }
  for (const HeapfixRange& range : HEAPFIX_TIMING_HOOKS) {
    if (range.size == 0) return false;
  }
  for (const HeapfixRange& range : HEAPFIX_CHECKED) {
    if (range.size == 0) return false;
  }
  return true;
}());
