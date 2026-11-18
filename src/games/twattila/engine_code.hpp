/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../utils/log.hpp"
#include "../twrome2/engine_tweaks.hpp"

// Faster Game CPU Code: Attila's hot routines in empire.retail.dll (the truncation, the float to half, fmod, the ready
// check, the sprite row builder's half floats), each replaced by code with the same results (test/tw_engine_code runs the
// game's code and this one side by side: every register, the stack, the x87 state, the flags where the code after reads
// them); Faster Render Lists (its own row): the render list push the same way.
// Each is found by its exact bytes: code another mod changed (the Attila CPU Performance Fix patches 1277 x87 blocks,
// the first one below among them) or another build is left alone. A jump into the addon's own code page replaces the
// first instruction(s) of each; the page is never freed (a thread can still be in it after the game's bytes are back).
// RVAs are the Steam build's (1.6.0): there they are tried first, then the code section is searched.
// Thrones of Britannia and Rome II (Game::THRONES, Game::ROME2): the same routines, their builds' (internal::BUILDS);
// Rome II has no ready check. Faster Job Wake-Ups (its own row, Experimental): a sixth to eighth site, the job system's
// worker wake-ups (6. below). The sprite soldiers' row builder's 20 half floats at once (7. below) belong to Faster Game
// CPU Code. Faster Effect Prefetch (its own row, Experimental): the effect tree's child sum (8. below), a tenth site, the
// particle update loop (10. below, the thirteenth) and the emitter update loop (11. below, the fourteenth). The particle
// writer's two float4 reloads (9. below, the eleventh and twelfth sites) belong to Faster Game CPU Code.
namespace twattila::engine_code {

// Whose build of the routines to look for.
enum class Game : uint8_t { ATTILA, THRONES, ROME2 };

#if !defined(_WIN64)
namespace internal {

// 1. The soldiers' model draw (+0x17f61a0): MSVC's x87 truncation of one float (fld; fstcw; or 0xc00; fldcw; fistp;
// fldcw), 4.6 ms a frame at Unit Detail 8x, mostly two control word loads and a 16-bit store read as 32 bits. The stub
// writes the same stack slots (the control word, it | 0xc00, the integer) and converts with cvttss2si: the same integer
// for every float (NaN and out of range: 0x80000000, x87's integer indefinite), the control word untouched. Under
// unmasked x87 or SSE exceptions (never the game's) the flags raised would differ: masked, x87 sets its sticky flags and
// cvttss2si MXCSR's.
constexpr uint32_t TRUNCATION_RVA = 0x17f6507;
constexpr std::string_view TRUNCATION =
    "8B 84 24 6C 01 00 00 89 84 24 80 00 00 00 50 D9 44 24 20 9B D9 7C 24 18 8B 44 24 18 0D 00 0C 00 00 89 44 24 1C "
    "D9 6C 24 1C DB 5C 24 2C D9 6C 24 18 58 0F B7 44 24 28 66 0F 6E C0 0F 5B C0 F3 0F 11 84 24";
constexpr size_t TRUNCATION_SITE = 14;    // push eax; fld dword ptr [esp + 0x20]
constexpr size_t TRUNCATION_RESUME = 50;  // after pop eax
// The stub for a build's stack slots after its push eax (each game's soldier draw keeps them elsewhere): the control
// word, it | 0xc00, the float, the integer.
constexpr std::array<uint8_t, 44> TruncationStub(uint8_t control, uint8_t rounding, uint8_t value, uint8_t integer) {
  return {
      0x50,                                                        // push eax
      0x0F, 0xB7, 0x44, 0x24, static_cast<uint8_t>(control + 2),   // movzx eax, word ptr [esp + control + 2]  the slot's high
                                                                   //   half (fstcw leaves it)
      0x9B, 0xD9, 0x7C, 0x24, control,                             // fstcw word ptr [esp + control]
      0xC1, 0xE0, 0x10,                                            // shl eax, 0x10
      0x66, 0x8B, 0x44, 0x24, control,                             // mov ax, word ptr [esp + control]
      0x0D, 0x00, 0x0C, 0x00, 0x00,                                // or eax, 0xc00
      0x89, 0x44, 0x24, rounding,                                  // mov dword ptr [esp + rounding], eax
      0xF3, 0x0F, 0x2C, 0x44, 0x24, value,                         // cvttss2si eax, dword ptr [esp + value]
      0x89, 0x44, 0x24, integer,                                   // mov dword ptr [esp + integer], eax
      0x58,                                                        // pop eax
      0xE9, 0x00, 0x00, 0x00, 0x00,                                // jmp back (after the game's pop eax)
  };
}
constexpr std::array<uint8_t, 44> TRUNCATION_STUB = TruncationStub(0x18, 0x1C, 0x20, 0x2C);
// The same bytes as the Attila CPU Performance Fix's x87 fix leaves them (its x87fix.c, on the block at +14): both fldcw
// a 4-byte nop, fistp fisttp. Left to that mod as any other mod's code; only named in the log.
constexpr auto X87_FIX_TRUNCATION = [] {
  std::array<char, TRUNCATION.size()> pattern = {};
  std::ranges::copy(TRUNCATION, pattern.begin());
  const auto put = [&](size_t offset, std::string_view bytes) { std::ranges::copy(bytes, pattern.begin() + 3 * offset); };
  put(37, "0F 1F 40 00");  // fldcw word ptr [esp + 0x1c]
  put(42, "4C");           // fistp dword ptr [esp + 0x2c] -> fisttp
  put(45, "0F 1F 40 00");  // fldcw word ptr [esp + 0x18]
  return pattern;
}();

// 2. float to half (+0x169a220, 98 callers; the sprite soldiers' instance rows, 20 a row: ~4.3 ms a frame): uint16_t
// __cdecl(const float*) in software, its own rounding (half way down, NaN to infinity), and it leaves the pointer's
// high half in eax for the smallest values and the float's exponent in ecx. The stub computes the normal halves and
// the zeros as it does (eax, ecx and edx as it leaves them); infinities, NaNs, overflow and half denormals jump to a
// copy of the game's function after the stub.
constexpr uint32_t HALF_RVA = 0x169a220;
constexpr std::string_view HALF =
    "8B 44 24 04 53 56 57 8B 30 8B CE 8B FE C1 E9 17 C1 EF 10 0F B6 D9 81 E7 00 80 00 00 81 FB 8E 00 00 00 76 0C B8 "
    "00 7C 00 00 66 0B C7 5F 5E 5B C3 81 E6 FF FF FF 7F 83 FB 70 76 29 8D 46 FF 23 C6 25 FF 1F 00 00 03 F0 8B C6 C1 "
    "EE 0D C1 E8 17 81 E6 FF 03 00 00 83 C0 10 C1 E0 0A 0B C6 66 0B C7 5F 5E 5B C3 83 FB 66 76 42 81 E6 FF FF 7F 00 "
    "8D 4B 99 8B C6 D3 E0 25 FF FF 3F 00 3D 00 02 00 00 76 13 8D 4B 99 BA FF FF 7F 00 D3 FA 23 D6 8D 42 FF 23 C2 03 "
    "F0 B9 7E 00 00 00 8D 86 00 00 80 00 2B CB D3 E8 66 0B C7 5F 5E 5B C3 81 FE 00 04 00 33 76 04 66 83 CF 01 66 8B "
    "C7 5F 5E 5B C3";
constexpr size_t HALF_SIZE = 190;  // the whole function, position independent: copied after the stub
constexpr size_t HALF_SITE = 0;    // mov eax, dword ptr [esp + 4]; push ebx
constexpr std::array<uint8_t, 0x70> HALF_STUB = {
    0x8B, 0x44, 0x24, 0x04,                    // mov eax, dword ptr [esp + 4]
    0x52,                                      // push edx
    0x8B, 0x10,                                // mov edx, dword ptr [eax]       f
    0x89, 0xD1,                                // mov ecx, edx
    0xC1, 0xE9, 0x17,                          // shr ecx, 0x17                  ecx as the game's leaves it
    0x81, 0xE2, 0xFF, 0xFF, 0xFF, 0x7F,        // and edx, 0x7fffffff            |f|
    0x8D, 0x82, 0x00, 0x00, 0x80, 0xC7,        // lea eax, [edx - 0x38800000]
    0x3D, 0x00, 0x00, 0x00, 0x0F,              // cmp eax, 0xf000000             a normal half: exponent 113 to 142
    0x73, 0x1E,                                // jae not_normal
    0x8D, 0x42, 0xFF,                          // lea eax, [edx - 1]             the game's rounding: + the low 13 bits
    0x21, 0xD0,                                // and eax, edx                     without their lowest set bit
    0x25, 0xFF, 0x1F, 0x00, 0x00,              // and eax, 0x1fff
    0x01, 0xD0,                                // add eax, edx
    0xC1, 0xE8, 0x0D,                          // shr eax, 0xd
    0x0F, 0xB6, 0xD5,                          // movzx edx, ch                  the sign
    0xC1, 0xE2, 0x0F,                          // shl edx, 0xf
    0x8D, 0x84, 0x10, 0x00, 0x40, 0x00, 0x00,  // lea eax, [eax + edx + 0x4000]  (bit 17 set, as the game's)
    0x5A,                                      // pop edx
    0xC3,                                      // ret
    0x81, 0xFA, 0x00, 0x00, 0x80, 0x33,        // not_normal: cmp edx, 0x33800000  exponent 102 or less: 0 or 1
    0x73, 0x1D,                                // jae game
    0x8B, 0x44, 0x24, 0x08,                    // mov eax, dword ptr [esp + 8]   the pointer's high half
    0x25, 0x00, 0x00, 0xFF, 0xFF,              // and eax, 0xffff0000
    0x81, 0xFA, 0x01, 0x04, 0x00, 0x33,        // cmp edx, 0x33000401
    0xF5,                                      // cmc
    0x83, 0xD0, 0x00,                          // adc eax, 0                     1 above 0x33000400
    0x0F, 0xB6, 0xD5,                          // movzx edx, ch
    0xC1, 0xE2, 0x0F,                          // shl edx, 0xf
    0x09, 0xD0,                                // or eax, edx
    0x5A,                                      // pop edx
    0xC3,                                      // ret
    0x5A,                                      // game: pop edx
    0xEB, 0x0B,                                // jmp copy (0x70)
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
};

// 3. fmod (+0x1a05cc0, 81 callers; the sprite soldiers' animation frame, ~1.9 ms a frame): the CRT's _CIfmod, x87
// arguments (st1 x, st0 y), through its dispatcher (control word saved and set, both arguments classified with fxam, a
// table of special cases, the remainder by fprem, a double copy, the control word back). For two normal numbers, no
// user _matherr, the CRT's flag at FMOD_FINISH's cmp clear and a caller control word of round to nearest with every
// exception masked, the stub runs the same fprem loop and double copy and leaves eax, ecx, edx and the x87 state as
// the CRT does; everything else goes to the CRT.
constexpr uint32_t FMOD_RVA = 0x1a05cc0;
constexpr std::string_view FMOD = "BA ?? ?? ?? ?? E9 ?? ?? ?? ?? 8D 9B 00 00 00 00 D9 C9 D9 F8 9B DF E0 9B 9E 7A F7 DD D9 C3";
constexpr size_t FMOD_REMAINDER = 16;  // fxch; fprem loop; fstp st(1); ret: the table's entry for two normal numbers
// The dispatcher (FMOD's jump), with its calls: the _matherr check, the classification, the finish.
constexpr std::string_view FMOD_DISPATCH =
    "55 8B EC 81 C4 30 FD FF FF 53 9B D9 BD 5C FF FF FF 9B DB 7D AE DB 7D B8 50 51 52 E8 ?? ?? ?? ?? 3C 00 DB 6D B8 "
    "DB 6D AE 5A 59 58 75 14 E8 ?? ?? ?? ?? 80 8D 38 FD FF FF 03 E8 ?? ?? ?? ?? 5B C9 C3 D9 C9 DD 95 7A FF FF FF D9 "
    "C9 DD 55 82 EB DD";
constexpr size_t FMOD_DISPATCH_MATHERR = 27;
constexpr size_t FMOD_DISPATCH_CLASSIFY = 45;
constexpr size_t FMOD_DISPATCH_FINISH = 57;
constexpr std::string_view FMOD_MATHERR = "8B 0D ?? ?? ?? ?? 8B 15 ?? ?? ?? ?? 83 E1 1F 33 15 ?? ?? ?? ?? D3 CA 85 D2 0F 95 C0 C3";
constexpr std::string_view FMOD_CLASSIFY =
    "80 7A 0E 05 75 11 66 8B 9D 5C FF FF FF 80 CF 02 80 E7 FE B3 3F EB 04 66 BB 3F 13 66 89 9D 5E FF FF FF D9 AD 5E "
    "FF FF FF BB ?? ?? ?? ?? D9 E5 89 95 6C FF FF FF 9B DD BD 60 FF FF FF C6 85 70 FF FF FF 00 D9 C9 8A 8D 61 FF FF "
    "FF D9 E5 9B DD BD 60 FF FF FF D9 C9 8A AD 61 FF FF FF D0 E5 D0 FD D0 C5 8A C5 24 0F D7 8A E0 D0 E1 D0 F9 D0 C1 "
    "8A C1 24 0F D7 D0 E4 D0 E4 0A C4 0F BE C0 81 E1 04 04 00 00 8B DA 03 D8 83 C3 10 50 52 51 8B 0B FF 15 ?? ?? ?? "
    "?? 59 5A 58 FF 23";
constexpr size_t FMOD_CLASSIFY_CLASSES = 41;  // mov ebx, the fxam classes' table
constexpr std::string_view FMOD_FINISH =
    "83 3D ?? ?? ?? ?? 00 75 4F DD 95 30 FD FF FF 8A 85 70 FF FF FF 0A C0 74 1A 3C FF 74 5B 3C FE 74 3F 0A C0 74 33 0F "
    "BE C0 89 85 72 FF FF FF E9 ?? ?? ?? ?? 66 8B 85 5C FF FF FF 66 83 E0 20 75 18 9B DF E0 66 83 E0 20 74 0F C7 85 72 "
    "FF FF FF 08 00 00 00 E9 ?? ?? ?? ?? D9 AD 5C FF FF FF 9B C3";
constexpr size_t FMOD_FINISH_FLAG = 2;
constexpr size_t FMOD_STUB_FLAG = 0x02;
constexpr size_t FMOD_STUB_MATHERR = 0x11;
constexpr std::array<size_t, 2> FMOD_STUB_TABLE = {0x7b, 0x92};
constexpr size_t FMOD_STUB_DISPATCH = 0x97;
constexpr std::array<uint8_t, 155> FMOD_STUB = {
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,  // cmp dword ptr [the CRT's flag], 0
    0x0F, 0x85, 0x84, 0x00, 0x00, 0x00,        // jne crt
    0x50,                                      // push eax
    0x51,                                      // push ecx
    0x52,                                      // push edx
    0xE8, 0x00, 0x00, 0x00, 0x00,              // call the CRT's _matherr check
    0x84, 0xC0,                                // test al, al
    0x75, 0x75,                                // jne restore
    0x83, 0xEC, 0x08,                          // sub esp, 8
    0xD9, 0x3C, 0x24,                          // fnstcw word ptr [esp]
    0x0F, 0xB7, 0x04, 0x24,                    // movzx eax, word ptr [esp]
    0x25, 0x3F, 0x0C, 0x00, 0x00,              // and eax, 0xc3f
    0x83, 0xF8, 0x3F,                          // cmp eax, 0x3f                 nearest, every exception masked
    0x75, 0x5E,                                // jne free
    0xD9, 0xE5,                                // fxam                          y
    0xDF, 0xE0,                                // fnstsw ax
    0x89, 0xC1,                                // mov ecx, eax
    0xD9, 0xC9,                                // fxch st(1)
    0xD9, 0xE5,                                // fxam                          x
    0xDF, 0xE0,                                // fnstsw ax
    0x89, 0xC2,                                // mov edx, eax
    0x81, 0xE2, 0x00, 0x45, 0x00, 0x00,        // and edx, 0x4500               C3 C2 C0
    0x81, 0xFA, 0x00, 0x04, 0x00, 0x00,        // cmp edx, 0x400                normal
    0x75, 0x40,                                // jne swap
    0x89, 0xCA,                                // mov edx, ecx
    0x81, 0xE2, 0x00, 0x45, 0x00, 0x00,        // and edx, 0x4500
    0x81, 0xFA, 0x00, 0x04, 0x00, 0x00,        // cmp edx, 0x400
    0x75, 0x30,                                // jne swap
    0x25, 0x00, 0x02, 0x00, 0x00,              // and eax, 0x200                ecx as the CRT's classification leaves
    0x81, 0xE1, 0x00, 0x02, 0x00, 0x00,        // and ecx, 0x200                  it: x's sign at bit 10, y's at 2
    0xD1, 0xE0,                                // shl eax, 1
    0xC1, 0xE9, 0x07,                          // shr ecx, 7
    0x09, 0xC1,                                // or ecx, eax
    0xD9, 0xF8,                                // remainder: fprem
    0xDF, 0xE0,                                // fnstsw ax
    0x9E,                                      // sahf
    0x7A, 0xF9,                                // jp remainder
    0xDD, 0xD9,                                // fstp st(1)
    0xDD, 0x14, 0x24,                          // fst qword ptr [esp]           the CRT's double copy (its flags)
    0x83, 0xC4, 0x14,                          // add esp, 0x14
    0xBA, 0x00, 0x00, 0x00, 0x00,              // mov edx, the CRT's fmod table
    0xB8, 0x20, 0x00, 0x00, 0x00,              // mov eax, 0x20
    0x66, 0x83, 0xE0, 0x20,                    // and ax, 0x20                  eax and flags as the CRT's finish
    0xC3,                                      // ret
    0xD9, 0xC9,                                // swap: fxch st(1)
    0x83, 0xC4, 0x08,                          // free: add esp, 8
    0x5A,                                      // restore: pop edx
    0x59,                                      // pop ecx
    0x58,                                      // pop eax
    0xBA, 0x00, 0x00, 0x00, 0x00,              // crt: mov edx, the CRT's fmod table (FMOD's first instruction)
    0xE9, 0x00, 0x00, 0x00, 0x00,              // jmp the dispatcher
};

// 4. The render lists' push (+0x176ced0, every item drawn, on every thread; 6.2 ms of workers and 1 ms of the render
// thread a frame in cpl2's recording): after the thread's own vector took the item it sets the list's "has items" byte
// (+0x1c), a store into the list's own cache line on every push, so that line (the thread index provider at +0, the
// vectors' headers at +0x18) moves between the cores and every push's first loads miss. The stub stores only when the
// byte isn't 1 yet: the same bytes in memory under every order of the threads, the line stays shared. Its flags differ
// (cmp's), the function's add esp before its ret overwrites them all.
constexpr uint32_t LIST_PUSH_RVA = 0x176cfb9;
constexpr std::string_view LIST_PUSH = "8B 44 24 0C FF 44 BE 04 C6 40 1C 01 8B 4C BE 04 8B 44 24 10 49 40 5F C1 E0 18";
constexpr size_t LIST_PUSH_SITE = 8;     // mov byte ptr [eax + 0x1c], 1; mov ecx, dword ptr [esi + edi*4 + 4]
constexpr size_t LIST_PUSH_RESUME = 16;  // mov eax, dword ptr [esp + 0x10]
constexpr std::array<uint8_t, 19> LIST_PUSH_STUB = {
    0x80, 0x78, 0x1C, 0x01,        // cmp byte ptr [eax + 0x1c], 1
    0x74, 0x04,                    // je kept
    0xC6, 0x40, 0x1C, 0x01,        // mov byte ptr [eax + 0x1c], 1
    0x8B, 0x4C, 0xBE, 0x04,        // kept: mov ecx, dword ptr [esi + edi*4 + 4]
    0xE9, 0x00, 0x00, 0x00, 0x00,  // jmp back
};

// 5. The pending list's ready check (+0x18635a0; its one caller, the render thread's per-frame sweep +0x1893290, tests al
// alone; 2.1 ms a frame in the stress battle): a thiscall returning in al whether [this + 4] is set and every child (the
// shared_ptrs {object, control block} at [this + 0x44], [this + 0x40] of them) has its ready byte (+0x6e) set. The game
// copies each child (lock inc) and releases it (lock xadd) only to read that byte, and goes on to the end after a child
// that isn't ready. The vector's own references keep every count at 1 or more through the call, so the copy and release
// leave each count where it was and the release's dispose and destroy never run; the stub reads the same bytes without
// them (a subset of the game's addresses) and stops at the first child not ready. It leaves what the game's leaves: al
// [this + 4] as it is with no child, 1 after a full loop of ready children, else 0; eax's other bits the count's with
// no child, else the last child's control block's uses + 1 (the release's old value) or, without one, its object; ecx
// this with al in its top byte and the same 16 bytes below esp (the game's frame: the result kept in the pushed ecx's top
// byte; the stub builds ecx in the register instead of popping it); the flags of an equal compare, the game's last.
// Only eax's bits from the last child's uses can differ, while another thread changes them during the call (the game's
// value then depends on the timing too); without the game's locked instructions (full fences) a ready byte another
// thread sets can only be seen sooner (x86 keeps loads in order).
constexpr uint32_t READY_CHECK_RVA = 0x18635a0;
constexpr std::string_view READY_CHECK =
    "51 8A 41 04 53 56 57 8B 79 44 88 44 24 0F 8B 41 40 8D 1C C7 3B FB 74 55 8B 77 04 85 F6 74 07 F0 FF 46 04 8B 77 04 "
    "80 7C 24 0F 00 8B 07 74 0A 80 78 6E 00 74 04 B0 01 EB 02 32 C0 88 44 24 0F 85 F6 74 23 B8 FF FF FF FF F0 0F C1 46 "
    "04 75 17 8B 06 8B CE FF 10 83 C8 FF F0 0F C1 46 08 75 07 8B 06 8B CE FF 50 04 83 C7 08 3B FB 75 AB 8A 44 24 0F 5F "
    "5E 5B 59 C3";
constexpr std::array<uint8_t, 94> READY_CHECK_STUB = {
    0x51,                          // push ecx                          the game's frame: the result in the pushed
    0x8A, 0x41, 0x04,              // mov al, byte ptr [ecx + 4]          ecx's top byte
    0x53,                          // push ebx
    0x56,                          // push esi
    0x57,                          // push edi
    0x8B, 0x79, 0x44,              // mov edi, dword ptr [ecx + 0x44]   the children
    0x88, 0x44, 0x24, 0x0F,        // mov byte ptr [esp + 0xf], al
    0x8B, 0x41, 0x40,              // mov eax, dword ptr [ecx + 0x40]   the count
    0x8D, 0x1C, 0xC7,              // lea ebx, [edi + eax*8]            the end
    0x3B, 0xFB,                    // cmp edi, ebx
    0x74, 0x34,                    // je done                           no child: [this + 4], eax the count's
    0x80, 0x7C, 0x24, 0x0F, 0x00,  // cmp byte ptr [esp + 0xf], 0
    0x74, 0x1B,                    // je last                           not set: 0, no child read
    0x8B, 0x07,                    // next: mov eax, dword ptr [edi]
    0x80, 0x78, 0x6E, 0x00,        // cmp byte ptr [eax + 0x6e], 0
    0x74, 0x0E,                    // je not_ready
    0x83, 0xC7, 0x08,              // add edi, 8
    0x3B, 0xFB,                    // cmp edi, ebx
    0x75, 0xF1,                    // jne next
    0xC6, 0x44, 0x24, 0x0F, 0x01,  // mov byte ptr [esp + 0xf], 1       every child ready: 1
    0xEB, 0x05,                    // jmp last
    0xC6, 0x44, 0x24, 0x0F, 0x00,  // not_ready: mov byte ptr [esp + 0xf], 0
    0x8B, 0x43, 0xFC,              // last: mov eax, dword ptr [ebx - 4]  the last child's control block
    0x85, 0xC0,                    // test eax, eax
    0x74, 0x06,                    // je object
    0x8B, 0x40, 0x04,              // mov eax, dword ptr [eax + 4]
    0x40,                          // inc eax                           its uses + 1
    0xEB, 0x03,                    // jmp equal
    0x8B, 0x43, 0xF8,              // object: mov eax, dword ptr [ebx - 8]
    0x3B, 0xDB,                    // equal: cmp ebx, ebx
    0x8A, 0x44, 0x24, 0x0F,        // done: mov al, byte ptr [esp + 0xf]
    0x5F,                          // pop edi
    0x5E,                          // pop esi
    0x5B,                          // pop ebx
    0x0F, 0xC9,                    // bswap ecx                         the pushed ecx (this, never written) with al in
    0x88, 0xC1,                    // mov cl, al                          its top byte, built in the register: pop ecx's
    0x0F, 0xC9,                    // bswap ecx                           4-byte reload of the 1-byte store waits for it
    0x8D, 0x64, 0x24, 0x04,        // lea esp, [esp + 4]                  the slot dropped, its bytes kept below esp
    0xC3,                          // ret                               bswap and lea write no flag: the equal compare's stay
};

// 6. The job system's worker wake-ups (Faster Job Wake-Ups, its own row, Experimental: not byte for byte the game's
// protocol). Every worker sleeps on its own semaphore; the render thread's WakeAll (+0x16b5550, 67 callers, 33 to 38 a
// frame in a battle) readies all ~15 of them one after the other: 65 us a call, 2.5 to 2.8 ms of the render thread a
// frame, most of it SetEvent system calls. Here WakeAll wakes one sleeping worker and that worker runs the game's own
// WakeAll loop for the others, so the render thread goes on at once. The semaphore code, Wake and the worker loop stay the
// game's: only which thread runs the Wakes, and a few microseconds later, changes (every worker still gets the game's Wake
// after every request, except one still pending when the pool is destroyed: Stop drops it).
// Three sites, all thiscall like the game's: WakeAll, Sleep (+0x16b5520, replaced whole: the worker's wait) and the call of
// Stop (+0x1697490) in the worker's destructor. A side table in its own memory (never freed) holds the pool, `pending` (a
// WakeAll asked for a loop), `asleep` (bit i: worker i is inside our Sleep) and `running` (loops in progress), each on its
// own cache line; one pool of up to 32 workers, any other uses the game's code. Dekker pairs: WakeAll sets `pending` with a
// locked instruction and then reads `asleep`; a worker sets its bit (locked) and reads `pending` before waiting, clears it
// (locked) after and reads `pending` again: a request is either seen by the worker or sees its bit. Whoever finds `pending`
// takes it (one locked exchange) and runs one loop, which starts after every request taken. Stop drops the pending request
// and waits for the loops in progress before the worker's memory goes: at most 2^28 pauses (about 3 s, 0.8 to 9 s by CPU),
// so a loop's thread that was killed can't hang the exit and one preempted for longer than that is not waited for. Sleep
// reads the pool's array only below the pool's size (the array may still be growing while the first workers start).
// Checked by test/tw_engine_code (the game's own
// functions, 32 threads) and a model check (every interleaving of 2 to 4 workers: no lost wake-up, no deadlock, no use after
// free). Thrones of Britannia: the same bytes at other addresses; Rome II has none of them.
constexpr std::string_view JOB_WAKE_ALL =
    "53 56 57 8B F9 33 F6 8B 5F 04 85 DB 74 12 66 90 8B 4F 08 8B 0C B1 E8 ?? ?? ?? ?? 46 3B F3 72 F0 5F 5E 5B C3";
constexpr size_t JOB_WAKE_ALL_WAKE_CALL = 22;  // call the game's Wake
constexpr std::string_view JOB_WAKE =
    "56 8B F1 B8 01 00 00 00 F0 0F C1 46 50 40 83 E8 01 75 08 8D 4E 1C E8 ?? ?? ?? ?? 83 7E 18 03 74 07 C7 46 18 02 00 00 "
    "00 5E C3";
constexpr std::string_view JOB_SLEEP =
    "56 8B F1 83 7E 18 03 74 16 8D 4E 1C C7 46 18 01 00 00 00 E8 ?? ?? ?? ?? 33 C9 8D 46 50 87 08 5E C3";
constexpr size_t JOB_SLEEP_ACQUIRE_CALL = 19;  // call the semaphore's Acquire(INFINITE)
constexpr std::string_view JOB_WORKER_DESTRUCTOR =
    "56 8B F1 C7 06 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? 8D 4E 1C E8 ?? ?? ?? ?? 8B 46 0C 5E 85 C0 74 07 50 FF "
    "15 ?? ?? ?? ?? C3";
constexpr size_t JOB_WORKER_DESTRUCTOR_STOP_CALL = 9;  // call the game's Stop
constexpr std::string_view JOB_STOP =
    "53 56 8B F1 8D 4E 1C C7 46 18 03 00 00 00 E8 ?? ?? ?? ?? 8D 5E 0C 53 E8 ?? ?? ?? ?? 8B 03 83 C4 04 85 C0 74 09 50 FF "
    "15 ?? ?? ?? ?? 33 C0 C7 03 00 00 00 00 85 C0 74 07 50 FF 15 ?? ?? ?? ?? C7 46 14 00 00 00 00 5E 5B C3";
constexpr size_t JOB_WAKE_ALL_STUB = 0x00;
constexpr size_t JOB_SLEEP_STUB = 0x73;
constexpr size_t JOB_STOP_STUB = 0xF2;
// The stubs' sentinels, replaced in Prepare: absolute fields (the job stub's side table: key, pending, asleep, running,
// each one cache line on), absolute constants (the half rows' table, +0 .. +0xff) and relative calls (the job stub: Wake,
// Acquire(INFINITE), WakeAll + 5, Stop; the half rows': the float to half, the region's start + 5, its end).
constexpr uint32_t STUB_FIELD_SENTINEL = 0x5A5A5A01;
constexpr uint32_t STUB_CALL_SENTINEL = 0x5A5A5A11;
constexpr uint32_t STUB_CONSTANT_SENTINEL = 0x5A5A5B00;
constexpr size_t JOB_TABLE_SIZE = 256;
constexpr size_t JOB_TABLE_LINE = 64;
constexpr std::array<uint8_t, 285> JOB_STUB = {
    0xA1, 0x01, 0x5A, 0x5A, 0x5A,                    // wake_all: mov eax, dword ptr [key]
    0x39, 0xC8,                                      // cmp eax, ecx
    0x74, 0x10,                                      // je claimed
    0x85, 0xC0,                                      // test eax, eax
    0x75, 0x5C,                                      // jne game_loop
    0x31, 0xC0,                                      // xor eax, eax
    0xF0, 0x0F, 0xB1, 0x0D, 0x01, 0x5A, 0x5A, 0x5A,  // lock cmpxchg dword ptr [key], ecx  the first pool claims the table
    0x75, 0x50,                                      // jne game_loop
    0x83, 0x79, 0x04, 0x20,                          // claimed: cmp dword ptr [ecx + 4], 32  more workers than mask bits: the game's loop
    0x77, 0x4A,                                      // ja game_loop
    0xF0, 0x83, 0x0D, 0x02, 0x5A, 0x5A, 0x5A, 0x01,  // lock or dword ptr [pending], 1  locked: no later load passes it
    0xA1, 0x03, 0x5A, 0x5A, 0x5A,                    // mov eax, dword ptr [asleep]
    0x85, 0xC0,                                      // test eax, eax
    0x74, 0x19,                                      // je delegate  nobody asleep in our Sleep: run the loop here, as the game does
    0x0F, 0xBC, 0xD0,                                // bsf edx, eax  the lowest sleeping worker
    0x8B, 0x41, 0x08,                                // mov eax, dword ptr [ecx + 8]
    0x8B, 0x04, 0x90,                                // mov eax, dword ptr [eax + edx*4]
    0x85, 0xC0,                                      // test eax, eax
    0x74, 0x0C,                                      // je delegate  a failed head check takes the request itself (table clean)
    0x39, 0x50, 0x08,                                // cmp dword ptr [eax + 8], edx  its index in the pool
    0x75, 0x07,                                      // jne delegate
    0x89, 0xC1,                                      // mov ecx, eax
    0xE9, 0x11, 0x5A, 0x5A, 0x5A,                    // jmp Wake  jmp the game's Wake: the sleeper runs the loop
    0xF0, 0xFF, 0x05, 0x04, 0x5A, 0x5A, 0x5A,        // delegate: lock inc dword ptr [running]
    0x31, 0xC0,                                      // xor eax, eax
    0x87, 0x05, 0x02, 0x5A, 0x5A, 0x5A,              // xchg dword ptr [pending], eax
    0x85, 0xC0,                                      // test eax, eax
    0x74, 0x05,                                      // je delegate_done
    0xE8, 0x08, 0x00, 0x00, 0x00,                    // call game_loop
    0xF0, 0xFF, 0x0D, 0x04, 0x5A, 0x5A, 0x5A,        // delegate_done: lock dec dword ptr [running]
    0xC3,                                            // ret
    0x53,                                            // game_loop: push ebx
    0x56,                                            // push esi
    0x57,                                            // push edi
    0x89, 0xCF,                                      // mov edi, ecx
    0xE9, 0x13, 0x5A, 0x5A, 0x5A,                    // jmp WakeAll + 5
    0x56,                                            // sleep: push esi
    0x89, 0xCE,                                      // mov esi, ecx
    0x83, 0x7E, 0x18, 0x03,                          // cmp dword ptr [esi + 0x18], 3
    0x74, 0x6A,                                      // je sleep_out
    0xC7, 0x46, 0x18, 0x01, 0x00, 0x00, 0x00,        // mov dword ptr [esi + 0x18], 1
    0x8B, 0x46, 0x04,                                // mov eax, dword ptr [esi + 4]  pool
    0x3B, 0x05, 0x01, 0x5A, 0x5A, 0x5A,              // cmp eax, dword ptr [key]
    0x75, 0x5A,                                      // jne sleep_game
    0x8B, 0x4E, 0x08,                                // mov ecx, dword ptr [esi + 8]  index
    0x83, 0xF9, 0x20,                                // cmp ecx, 32
    0x73, 0x52,                                      // jae sleep_game
    0x3B, 0x48, 0x04,                                // cmp ecx, dword ptr [eax + 4]  within the pool's size (the array may still be growing)
    0x73, 0x4D,                                      // jae sleep_game
    0x8B, 0x50, 0x08,                                // mov edx, dword ptr [eax + 8]
    0x39, 0x34, 0x8A,                                // cmp dword ptr [edx + ecx*4], esi
    0x75, 0x45,                                      // jne sleep_game
    0xF0, 0x0F, 0xAB, 0x0D, 0x03, 0x5A, 0x5A, 0x5A,  // lock bts dword ptr [asleep], ecx
    0x83, 0x3D, 0x02, 0x5A, 0x5A, 0x5A, 0x00,        // cmp dword ptr [pending], 0  a request that missed the bit
    0x74, 0x07,                                      // je acquire
    0x89, 0xC1,                                      // mov ecx, eax
    0xE8, 0x8E, 0xFF, 0xFF, 0xFF,                    // call delegate
    0x8D, 0x4E, 0x1C,                                // acquire: lea ecx, [esi + 0x1c]
    0xE8, 0x12, 0x5A, 0x5A, 0x5A,                    // call AcquireInf  call AcquireInf
    0x8B, 0x4E, 0x08,                                // mov ecx, dword ptr [esi + 8]
    0xF0, 0x0F, 0xB3, 0x0D, 0x03, 0x5A, 0x5A, 0x5A,  // lock btr dword ptr [asleep], ecx
    0x83, 0x3D, 0x02, 0x5A, 0x5A, 0x5A, 0x00,        // cmp dword ptr [pending], 0  the request that woke us
    0x74, 0x08,                                      // je reset
    0x8B, 0x4E, 0x04,                                // mov ecx, dword ptr [esi + 4]
    0xE8, 0x6A, 0xFF, 0xFF, 0xFF,                    // call delegate
    0x31, 0xC9,                                      // reset: xor ecx, ecx
    0x8D, 0x46, 0x50,                                // lea eax, [esi + 0x50]
    0x87, 0x08,                                      // xchg dword ptr [eax], ecx
    0x5E,                                            // sleep_out: pop esi
    0xC3,                                            // ret
    0x8D, 0x4E, 0x1C,                                // sleep_game: lea ecx, [esi + 0x1c]
    0xE8, 0x12, 0x5A, 0x5A, 0x5A,                    // call AcquireInf
    0xEB, 0xED,                                      // jmp reset
    0x8B, 0x41, 0x04,                                // stop: mov eax, dword ptr [ecx + 4]
    0x3B, 0x05, 0x01, 0x5A, 0x5A, 0x5A,              // cmp eax, dword ptr [key]
    0x75, 0x1B,                                      // jne stop_game
    0x31, 0xC0,                                      // xor eax, eax
    0x87, 0x05, 0x02, 0x5A, 0x5A, 0x5A,              // xchg dword ptr [pending], eax  a request that no worker will run any more is dropped
    0xB8, 0x00, 0x00, 0x00, 0x10,                    // mov eax, 0x10000000  2^28 pauses (3 s on Zen 4, 0.8 to 9 s by CPU): a delegate preempted for longer is not waited for
    0x83, 0x3D, 0x04, 0x5A, 0x5A, 0x5A, 0x00,        // drain: cmp dword ptr [running], 0
    0x74, 0x05,                                      // je stop_game
    0xF3, 0x90,                                      // pause
    0x48,                                            // dec eax
    0x75, 0xF2,                                      // jne drain
    0xE9, 0x14, 0x5A, 0x5A, 0x5A,                    // stop_game: jmp Stop  jmp the game's Stop
};

// 7. The sprite soldiers' row builder (+0x1906fa0; 4 rows of 0x38 bytes a call): between its row loop's maths it converts 20
// floats to halves by 20 calls of the float to half above (the halves are the row's words +8 .. +0x2f), 8 ms of the workers a
// frame at Unit Detail 8x. The stub for the region from the first of those calls to the one after the last (+0x1907511 ..
// +0x19077e2; the first 5 bytes are the site) reads the 20 floats from the same places (the frame's slots, two objects,
// xmm0, two products with their operands in the game's order), converts them four at a time with the same integer rounding
// (zero and tiny values, normals; any infinity, NaN, overflow or half denormal among the 20 and the game's code runs, as it
// does when a store of the row could reach an input: five range checks), stores the halves at once (all but the last, which
// the game's code stores after the region from the real call's ax), leaves the frame's three temporaries (+0x38, +0x70,
// +0x74) as the game does, and makes the last conversion by a real call of the game's float to half with its argument slot:
// eax (its high half too) and ecx come out as that function leaves them; edx, which only that function writes, is kept in
// xmm3 and put back before the call and before the fallback. What differs from the game's code, all dead after the region:
// the stack from 0x50 bytes below the frame (where the game's calls leave the half function's frames: return addresses, saved
// registers, the 16 pushed argument slots) up to the frame (no live object lies on the stack below the frame; the memory
// from the frame up is the game's), xmm1 to xmm7 (nothing in the loop or the way out
// reads them first: the head and the code after the region are held to exact bytes, the jb goes back to that head; the
// function's callers read none before they return), and the order of the reads against the halves' stores (the checks) and
// of a fault: an invalid pointer faults in both (a bad source pointer at another first address, and without the halves the
// game stored before it). The flags at the end are the game's when the float to half site is off; with its stub in they are
// that stub's (dead after a call).
constexpr std::string_view HALF_ROWS =
    "8D 44 24 38 50 F3 0F 11 44 24 3C E8 ?? ?? ?? ?? 8B 4C 24 6C F3 0F 10 44 24 28 F3 0F 11 44 24 3C 66 89 41 08 8D 44 24 "
    "3C 50 E8 ?? ?? ?? ?? 8B 4C 24 70 66 89 41 0A 8D 44 24 40 F3 0F 10 46 1C 50 F3 0F 11 44 24 44 E8 ?? ?? ?? ?? 8B 4C 24 "
    "74 66 0F 6E 44 24 2C 0F 5B C0 66 89 41 0C 8D 44 24 44 50 F3 0F 11 44 24 48 E8 ?? ?? ?? ?? 8B 4C 24 78 F3 0F 10 84 24 "
    "80 00 00 00 F3 0F 11 84 24 80 00 00 00 66 89 41 0E 8D 84 24 80 00 00 00 50 E8 ?? ?? ?? ?? 8B 4C 24 7C F3 0F 10 44 24 "
    "6C F3 0F 11 84 24 84 00 00 00 66 89 41 10 8D 84 24 84 00 00 00 50 E8 ?? ?? ?? ?? 8B 8C 24 80 00 00 00 F3 0F 10 44 24 "
    "68 F3 0F 59 84 24 8C 00 00 00 66 89 41 12 8D 84 24 88 00 00 00 50 F3 0F 11 84 24 8C 00 00 00 E8 ?? ?? ?? ?? 8B 8C 24 "
    "84 00 00 00 F3 0F 10 84 24 90 00 00 00 F3 0F 59 44 24 68 66 89 41 14 8D 84 24 90 00 00 00 50 F3 0F 11 84 24 94 00 00 "
    "00 E8 ?? ?? ?? ?? 8B 8C 24 88 00 00 00 F3 0F 10 44 24 68 F3 0F 11 84 24 94 00 00 00 66 89 41 18 8D 84 24 94 00 00 00 "
    "50 E8 ?? ?? ?? ?? 8B 8C 24 8C 00 00 00 C7 84 24 98 00 00 00 00 00 00 00 66 89 41 1A 8D 84 24 98 00 00 00 50 E8 ?? ?? "
    "?? ?? 8B 8C 24 90 00 00 00 F3 0F 10 44 24 6C F3 0F 11 84 24 9C 00 00 00 66 89 41 1C 8D 84 24 9C 00 00 00 50 E8 ?? ?? "
    "?? ?? 8B 8C 24 94 00 00 00 66 89 41 1E 8D 84 24 A0 00 00 00 F3 0F 10 46 28 50 F3 0F 11 84 24 A4 00 00 00 E8 ?? ?? ?? "
    "?? 8B 8C 24 98 00 00 00 66 89 41 20 8D 84 24 A4 00 00 00 F3 0F 10 46 2C 50 F3 0F 11 84 24 A8 00 00 00 E8 ?? ?? ?? ?? "
    "8B 8C 24 9C 00 00 00 66 89 41 22 8D 84 24 A8 00 00 00 F3 0F 10 46 30 50 F3 0F 11 84 24 AC 00 00 00 E8 ?? ?? ?? ?? 8B "
    "8C 24 A0 00 00 00 66 89 41 24 8D 84 24 AC 00 00 00 F3 0F 10 46 34 50 F3 0F 11 84 24 B0 00 00 00 E8 ?? ?? ?? ?? 8B 8C "
    "24 A4 00 00 00 66 89 41 26 8D 84 24 B0 00 00 00 F3 0F 10 46 38 50 F3 0F 11 84 24 B4 00 00 00 E8 ?? ?? ?? ?? 8B 8C 24 "
    "A8 00 00 00 83 C4 40 66 89 41 28 8D 44 24 74 F3 0F 10 46 3C 50 F3 0F 11 44 24 78 E8 ?? ?? ?? ?? 8B 4C 24 6C 66 89 41 "
    "2A 8D 44 24 78 F3 0F 10 46 40 50 F3 0F 11 44 24 7C E8 ?? ?? ?? ?? 8B 4C 24 70 66 89 41 16 8B 44 24 64 F3 0F 10 40 48 "
    "8D 44 24 7C 50 F3 0F 11 84 24 80 00 00 00 E8 ?? ?? ?? ?? 8B 4C 24 74 66 89 41 2C 8B 44 24 68 F3 0F 10 40 4C 8D 84 24 "
    "80 00 00 00 50 F3 0F 11 84 24 84 00 00 00 E8 ?? ?? ?? ?? 8B 4C 24 78 47";
constexpr size_t HALF_ROWS_END = 721;  // mov ecx, dword ptr [esp + 0x78]: where the region ends
// The code the stub's deadness argument also stands on, held to exact bytes: the loop's head (xmm1 to xmm5 are written before they
// are read, xmm6 and xmm7 never read) and the code after the region (the pop of the 16 argument slots, the loop's bottom whose
// jb goes back to that head, the exit). The same in Attila's and Thrones' builds (the three addresses in the head are wildcards).
constexpr std::string_view HALF_ROWS_HEAD =
    "8B CF F3 0F 10 15 ?? ?? ?? ?? D1 E9 8B D7 0F 57 C9 83 E2 01 66 0F 6E C1 8D 04 49 F3 0F 10 5C 84 08 F3 0F 10 64 84 0C F3 0F 10 6C "
    "84 10 F3 0F 58 5C 24 34 F3 0F 58 64 24 30 F3 0F 58 6C 24 2C F3 0F E6 C0 C1 E9 1F F2 0F 58 04 CD ?? ?? ?? ?? 66 0F 5A C0 0F 2F C1 "
    "76 08 F3 0F 11 54 24 58 EB 06 F3 0F 11 4C 24 58 F3 0F 10 44 24 54 85 D2 74 08 F3 0F 11 4C 24 70 EB 0D 0F 57 05 ?? ?? ?? ?? F3 0F "
    "11 54 24 70 8B 44 24 68 F3 0F 11 44 24 74 F3 0F 10 44 24 6C F3 0F 11 40 04 F3 0F 10 44 24 28 F3 0F 11 58 F8 F3 0F 11 60 FC F3 0F "
    "11 28";
constexpr size_t HALF_ROWS_HEAD_AT = 174;  // the head starts this many bytes before the region
constexpr std::string_view HALF_ROWS_TAIL = "8B 4C 24 78 47 83 C4 10 66 89 41 2E 83 C1 38 89 4C 24 68 83 FF 04 0F 82 65 FC FF FF 5F 5E 83 C4 5C C2 10";
constexpr std::array<uint16_t, 20> HALF_ROWS_CALLS = {11, 41, 70, 103, 142, 178, 226, 274, 313, 348, 387, 425, 463, 501, 539, 577, 612, 641, 677, 716};
constexpr size_t HALF_ROWS_CONSTANTS_AT = 0x4D0;
constexpr std::array<uint8_t, 1222> HALF_ROWS_STUB = {
    0x66, 0x0F, 0x6E, 0xDA,                          // region: movd xmm3, edx
    0x8B, 0x4C, 0x24, 0x68,                          // mov ecx, dword ptr [esp + 0x68]
    0x8B, 0x54, 0x24, 0x5C,                          // mov edx, dword ptr [esp + 0x5c]
    0x89, 0xC8,                                      // mov eax, ecx
    0x29, 0xF0,                                      // sub eax, esi
    0x83, 0xC0, 0x13,                                // add eax, 0x13
    0x83, 0xF8, 0x4F,                                // cmp eax, 0x4f
    0x0F, 0x82, 0x9C, 0x04, 0x00, 0x00,              // jb 0x4b8
    0x89, 0xC8,                                      // mov eax, ecx
    0x29, 0xD0,                                      // sub eax, edx
    0x83, 0xE8, 0x19,                                // sub eax, 0x19
    0x83, 0xF8, 0x2F,                                // cmp eax, 0x2f
    0x0F, 0x82, 0x8C, 0x04, 0x00, 0x00,              // jb 0x4b8
    0x8D, 0x41, 0x6F,                                // lea eax, [ecx + 0x6f]
    0x29, 0xE0,                                      // sub eax, esp
    0x3D, 0xDF, 0x00, 0x00, 0x00,                    // cmp eax, 0xdf
    0x0F, 0x82, 0x7C, 0x04, 0x00, 0x00,              // jb 0x4b8
    0x8D, 0x46, 0x0B,                                // lea eax, [esi + 0xb]
    0x29, 0xE0,                                      // sub eax, esp
    0x83, 0xF8, 0x67,                                // cmp eax, 0x67
    0x0F, 0x82, 0x6E, 0x04, 0x00, 0x00,              // jb 0x4b8
    0x8D, 0x42, 0x17,                                // lea eax, [edx + 0x17]
    0x29, 0xE0,                                      // sub eax, esp
    0x83, 0xF8, 0x47,                                // cmp eax, 0x47
    0x0F, 0x82, 0x60, 0x04, 0x00, 0x00,              // jb 0x4b8
    0xF3, 0x0F, 0x10, 0x6C, 0x24, 0x24,              // movss xmm5, dword ptr [esp + 0x24]
    0x0F, 0x28, 0xE0,                                // movaps xmm4, xmm0
    0x0F, 0x14, 0xE5,                                // unpcklps xmm4, xmm5
    0xF3, 0x0F, 0x10, 0x6E, 0x1C,                    // movss xmm5, dword ptr [esi + 0x1c]
    0x66, 0x0F, 0x6E, 0x74, 0x24, 0x20,              // movd xmm6, dword ptr [esp + 0x20]
    0x0F, 0x5B, 0xF6,                                // cvtdq2ps xmm6, xmm6
    0x0F, 0x14, 0xEE,                                // unpcklps xmm5, xmm6
    0x0F, 0x16, 0xE5,                                // movlhps xmm4, xmm5
    0x66, 0x0F, 0x6F, 0xEC,                          // movdqa xmm5, xmm4
    0x66, 0x0F, 0xDB, 0x2D, 0x00, 0x5B, 0x5A, 0x5A,  // pand xmm5, xmmword ptr [table + 0x0]
    0x66, 0x0F, 0x6F, 0xF5,                          // movdqa xmm6, xmm5
    0x66, 0x0F, 0x66, 0x35, 0x60, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm6, xmmword ptr [table + 0x60]
    0x66, 0x0F, 0xFA, 0x2D, 0x70, 0x5B, 0x5A, 0x5A,  // psubd xmm5, xmmword ptr [table + 0x70]
    0x66, 0x0F, 0xEF, 0x2D, 0x80, 0x5B, 0x5A, 0x5A,  // pxor xmm5, xmmword ptr [table + 0x80]
    0x66, 0x0F, 0x66, 0x2D, 0x90, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm5, xmmword ptr [table + 0x90]
    0x66, 0x0F, 0xDF, 0xF5,                          // pandn xmm6, xmm5
    0x0F, 0x50, 0xC6,                                // movmskps eax, xmm6
    0x83, 0xF8, 0x0F,                                // cmp eax, 0xf
    0x0F, 0x85, 0x00, 0x04, 0x00, 0x00,              // jne 0x4b8
    0x66, 0x0F, 0x6F, 0xFC,                          // movdqa xmm7, xmm4
    0x66, 0x0F, 0x72, 0xD7, 0x1F,                    // psrld xmm7, 0x1f
    0x66, 0x0F, 0x72, 0xF7, 0x0F,                    // pslld xmm7, 0xf
    0x66, 0x0F, 0xDB, 0x25, 0x00, 0x5B, 0x5A, 0x5A,  // pand xmm4, xmmword ptr [table + 0x0]
    0x66, 0x0F, 0x6F, 0xEC,                          // movdqa xmm5, xmm4
    0x66, 0x0F, 0x66, 0x2D, 0x20, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm5, xmmword ptr [table + 0x20]
    0x66, 0x0F, 0x72, 0xD5, 0x1F,                    // psrld xmm5, 0x1f
    0x66, 0x0F, 0x6F, 0xF4,                          // movdqa xmm6, xmm4
    0x66, 0x0F, 0xFA, 0x35, 0x30, 0x5B, 0x5A, 0x5A,  // psubd xmm6, xmmword ptr [table + 0x30]
    0x66, 0x0F, 0xDB, 0xF4,                          // pand xmm6, xmm4
    0x66, 0x0F, 0xDB, 0x35, 0x40, 0x5B, 0x5A, 0x5A,  // pand xmm6, xmmword ptr [table + 0x40]
    0x66, 0x0F, 0xFE, 0xF4,                          // paddd xmm6, xmm4
    0x66, 0x0F, 0x72, 0xD6, 0x0D,                    // psrld xmm6, 0xd
    0x66, 0x0F, 0xFE, 0x35, 0x50, 0x5B, 0x5A, 0x5A,  // paddd xmm6, xmmword ptr [table + 0x50]
    0x66, 0x0F, 0x66, 0x25, 0x10, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm4, xmmword ptr [table + 0x10]
    0x66, 0x0F, 0xDB, 0xF4,                          // pand xmm6, xmm4
    0x66, 0x0F, 0xDF, 0xE5,                          // pandn xmm4, xmm5
    0x66, 0x0F, 0xEB, 0xF4,                          // por xmm6, xmm4
    0x66, 0x0F, 0xFE, 0xF7,                          // paddd xmm6, xmm7
    0x66, 0x0F, 0x72, 0xF6, 0x10,                    // pslld xmm6, 0x10
    0x66, 0x0F, 0x72, 0xE6, 0x10,                    // psrad xmm6, 0x10
    0x66, 0x0F, 0x6F, 0xCE,                          // movdqa xmm1, xmm6
    0xF3, 0x0F, 0x10, 0x64, 0x24, 0x70,              // movss xmm4, dword ptr [esp + 0x70]
    0xF3, 0x0F, 0x10, 0x6C, 0x24, 0x58,              // movss xmm5, dword ptr [esp + 0x58]
    0x0F, 0x14, 0xE5,                                // unpcklps xmm4, xmm5
    0xF3, 0x0F, 0x10, 0x6C, 0x24, 0x50,              // movss xmm5, dword ptr [esp + 0x50]
    0xF3, 0x0F, 0x59, 0x6C, 0x24, 0x74,              // mulss xmm5, dword ptr [esp + 0x74]
    0xF3, 0x0F, 0x10, 0x76, 0x40,                    // movss xmm6, dword ptr [esi + 0x40]
    0x0F, 0x14, 0xEE,                                // unpcklps xmm5, xmm6
    0x0F, 0x16, 0xE5,                                // movlhps xmm4, xmm5
    0x66, 0x0F, 0x6F, 0xEC,                          // movdqa xmm5, xmm4
    0x66, 0x0F, 0xDB, 0x2D, 0x00, 0x5B, 0x5A, 0x5A,  // pand xmm5, xmmword ptr [table + 0x0]
    0x66, 0x0F, 0x6F, 0xF5,                          // movdqa xmm6, xmm5
    0x66, 0x0F, 0x66, 0x35, 0x60, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm6, xmmword ptr [table + 0x60]
    0x66, 0x0F, 0xFA, 0x2D, 0x70, 0x5B, 0x5A, 0x5A,  // psubd xmm5, xmmword ptr [table + 0x70]
    0x66, 0x0F, 0xEF, 0x2D, 0x80, 0x5B, 0x5A, 0x5A,  // pxor xmm5, xmmword ptr [table + 0x80]
    0x66, 0x0F, 0x66, 0x2D, 0x90, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm5, xmmword ptr [table + 0x90]
    0x66, 0x0F, 0xDF, 0xF5,                          // pandn xmm6, xmm5
    0x0F, 0x50, 0xC6,                                // movmskps eax, xmm6
    0x83, 0xF8, 0x0F,                                // cmp eax, 0xf
    0x0F, 0x85, 0x24, 0x03, 0x00, 0x00,              // jne 0x4b8
    0x66, 0x0F, 0x6F, 0xFC,                          // movdqa xmm7, xmm4
    0x66, 0x0F, 0x72, 0xD7, 0x1F,                    // psrld xmm7, 0x1f
    0x66, 0x0F, 0x72, 0xF7, 0x0F,                    // pslld xmm7, 0xf
    0x66, 0x0F, 0xDB, 0x25, 0x00, 0x5B, 0x5A, 0x5A,  // pand xmm4, xmmword ptr [table + 0x0]
    0x66, 0x0F, 0x6F, 0xEC,                          // movdqa xmm5, xmm4
    0x66, 0x0F, 0x66, 0x2D, 0x20, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm5, xmmword ptr [table + 0x20]
    0x66, 0x0F, 0x72, 0xD5, 0x1F,                    // psrld xmm5, 0x1f
    0x66, 0x0F, 0x6F, 0xF4,                          // movdqa xmm6, xmm4
    0x66, 0x0F, 0xFA, 0x35, 0x30, 0x5B, 0x5A, 0x5A,  // psubd xmm6, xmmword ptr [table + 0x30]
    0x66, 0x0F, 0xDB, 0xF4,                          // pand xmm6, xmm4
    0x66, 0x0F, 0xDB, 0x35, 0x40, 0x5B, 0x5A, 0x5A,  // pand xmm6, xmmword ptr [table + 0x40]
    0x66, 0x0F, 0xFE, 0xF4,                          // paddd xmm6, xmm4
    0x66, 0x0F, 0x72, 0xD6, 0x0D,                    // psrld xmm6, 0xd
    0x66, 0x0F, 0xFE, 0x35, 0x50, 0x5B, 0x5A, 0x5A,  // paddd xmm6, xmmword ptr [table + 0x50]
    0x66, 0x0F, 0x66, 0x25, 0x10, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm4, xmmword ptr [table + 0x10]
    0x66, 0x0F, 0xDB, 0xF4,                          // pand xmm6, xmm4
    0x66, 0x0F, 0xDF, 0xE5,                          // pandn xmm4, xmm5
    0x66, 0x0F, 0xEB, 0xF4,                          // por xmm6, xmm4
    0x66, 0x0F, 0xFE, 0xF7,                          // paddd xmm6, xmm7
    0x66, 0x0F, 0x72, 0xF6, 0x10,                    // pslld xmm6, 0x10
    0x66, 0x0F, 0x72, 0xE6, 0x10,                    // psrad xmm6, 0x10
    0x66, 0x0F, 0x6B, 0xCE,                          // packssdw xmm1, xmm6
    0xF3, 0x0F, 0x10, 0x64, 0x24, 0x74,              // movss xmm4, dword ptr [esp + 0x74]
    0xF3, 0x0F, 0x59, 0x64, 0x24, 0x4C,              // mulss xmm4, dword ptr [esp + 0x4c]
    0xF3, 0x0F, 0x10, 0x6C, 0x24, 0x48,              // movss xmm5, dword ptr [esp + 0x48]
    0x0F, 0x14, 0xE5,                                // unpcklps xmm4, xmm5
    0x0F, 0x57, 0xED,                                // xorps xmm5, xmm5
    0xF3, 0x0F, 0x10, 0x74, 0x24, 0x44,              // movss xmm6, dword ptr [esp + 0x44]
    0x0F, 0x14, 0xEE,                                // unpcklps xmm5, xmm6
    0x0F, 0x16, 0xE5,                                // movlhps xmm4, xmm5
    0x66, 0x0F, 0x6F, 0xEC,                          // movdqa xmm5, xmm4
    0x66, 0x0F, 0xDB, 0x2D, 0x00, 0x5B, 0x5A, 0x5A,  // pand xmm5, xmmword ptr [table + 0x0]
    0x66, 0x0F, 0x6F, 0xF5,                          // movdqa xmm6, xmm5
    0x66, 0x0F, 0x66, 0x35, 0x60, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm6, xmmword ptr [table + 0x60]
    0x66, 0x0F, 0xFA, 0x2D, 0x70, 0x5B, 0x5A, 0x5A,  // psubd xmm5, xmmword ptr [table + 0x70]
    0x66, 0x0F, 0xEF, 0x2D, 0x80, 0x5B, 0x5A, 0x5A,  // pxor xmm5, xmmword ptr [table + 0x80]
    0x66, 0x0F, 0x66, 0x2D, 0x90, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm5, xmmword ptr [table + 0x90]
    0x66, 0x0F, 0xDF, 0xF5,                          // pandn xmm6, xmm5
    0x0F, 0x50, 0xC6,                                // movmskps eax, xmm6
    0x83, 0xF8, 0x0F,                                // cmp eax, 0xf
    0x0F, 0x85, 0x4A, 0x02, 0x00, 0x00,              // jne 0x4b8
    0x66, 0x0F, 0x6F, 0xFC,                          // movdqa xmm7, xmm4
    0x66, 0x0F, 0x72, 0xD7, 0x1F,                    // psrld xmm7, 0x1f
    0x66, 0x0F, 0x72, 0xF7, 0x0F,                    // pslld xmm7, 0xf
    0x66, 0x0F, 0xDB, 0x25, 0x00, 0x5B, 0x5A, 0x5A,  // pand xmm4, xmmword ptr [table + 0x0]
    0x66, 0x0F, 0x6F, 0xEC,                          // movdqa xmm5, xmm4
    0x66, 0x0F, 0x66, 0x2D, 0x20, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm5, xmmword ptr [table + 0x20]
    0x66, 0x0F, 0x72, 0xD5, 0x1F,                    // psrld xmm5, 0x1f
    0x66, 0x0F, 0x6F, 0xF4,                          // movdqa xmm6, xmm4
    0x66, 0x0F, 0xFA, 0x35, 0x30, 0x5B, 0x5A, 0x5A,  // psubd xmm6, xmmword ptr [table + 0x30]
    0x66, 0x0F, 0xDB, 0xF4,                          // pand xmm6, xmm4
    0x66, 0x0F, 0xDB, 0x35, 0x40, 0x5B, 0x5A, 0x5A,  // pand xmm6, xmmword ptr [table + 0x40]
    0x66, 0x0F, 0xFE, 0xF4,                          // paddd xmm6, xmm4
    0x66, 0x0F, 0x72, 0xD6, 0x0D,                    // psrld xmm6, 0xd
    0x66, 0x0F, 0xFE, 0x35, 0x50, 0x5B, 0x5A, 0x5A,  // paddd xmm6, xmmword ptr [table + 0x50]
    0x66, 0x0F, 0x66, 0x25, 0x10, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm4, xmmword ptr [table + 0x10]
    0x66, 0x0F, 0xDB, 0xF4,                          // pand xmm6, xmm4
    0x66, 0x0F, 0xDF, 0xE5,                          // pandn xmm4, xmm5
    0x66, 0x0F, 0xEB, 0xF4,                          // por xmm6, xmm4
    0x66, 0x0F, 0xFE, 0xF7,                          // paddd xmm6, xmm7
    0x66, 0x0F, 0x72, 0xF6, 0x10,                    // pslld xmm6, 0x10
    0x66, 0x0F, 0x72, 0xE6, 0x10,                    // psrad xmm6, 0x10
    0x66, 0x0F, 0x6F, 0xD6,                          // movdqa xmm2, xmm6
    0x0F, 0x10, 0x66, 0x28,                          // movups xmm4, xmmword ptr [esi + 0x28]
    0x66, 0x0F, 0x6F, 0xEC,                          // movdqa xmm5, xmm4
    0x66, 0x0F, 0xDB, 0x2D, 0x00, 0x5B, 0x5A, 0x5A,  // pand xmm5, xmmword ptr [table + 0x0]
    0x66, 0x0F, 0x6F, 0xF5,                          // movdqa xmm6, xmm5
    0x66, 0x0F, 0x66, 0x35, 0x60, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm6, xmmword ptr [table + 0x60]
    0x66, 0x0F, 0xFA, 0x2D, 0x70, 0x5B, 0x5A, 0x5A,  // psubd xmm5, xmmword ptr [table + 0x70]
    0x66, 0x0F, 0xEF, 0x2D, 0x80, 0x5B, 0x5A, 0x5A,  // pxor xmm5, xmmword ptr [table + 0x80]
    0x66, 0x0F, 0x66, 0x2D, 0x90, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm5, xmmword ptr [table + 0x90]
    0x66, 0x0F, 0xDF, 0xF5,                          // pandn xmm6, xmm5
    0x0F, 0x50, 0xC6,                                // movmskps eax, xmm6
    0x83, 0xF8, 0x0F,                                // cmp eax, 0xf
    0x0F, 0x85, 0x90, 0x01, 0x00, 0x00,              // jne 0x4b8
    0x66, 0x0F, 0x6F, 0xFC,                          // movdqa xmm7, xmm4
    0x66, 0x0F, 0x72, 0xD7, 0x1F,                    // psrld xmm7, 0x1f
    0x66, 0x0F, 0x72, 0xF7, 0x0F,                    // pslld xmm7, 0xf
    0x66, 0x0F, 0xDB, 0x25, 0x00, 0x5B, 0x5A, 0x5A,  // pand xmm4, xmmword ptr [table + 0x0]
    0x66, 0x0F, 0x6F, 0xEC,                          // movdqa xmm5, xmm4
    0x66, 0x0F, 0x66, 0x2D, 0x20, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm5, xmmword ptr [table + 0x20]
    0x66, 0x0F, 0x72, 0xD5, 0x1F,                    // psrld xmm5, 0x1f
    0x66, 0x0F, 0x6F, 0xF4,                          // movdqa xmm6, xmm4
    0x66, 0x0F, 0xFA, 0x35, 0x30, 0x5B, 0x5A, 0x5A,  // psubd xmm6, xmmword ptr [table + 0x30]
    0x66, 0x0F, 0xDB, 0xF4,                          // pand xmm6, xmm4
    0x66, 0x0F, 0xDB, 0x35, 0x40, 0x5B, 0x5A, 0x5A,  // pand xmm6, xmmword ptr [table + 0x40]
    0x66, 0x0F, 0xFE, 0xF4,                          // paddd xmm6, xmm4
    0x66, 0x0F, 0x72, 0xD6, 0x0D,                    // psrld xmm6, 0xd
    0x66, 0x0F, 0xFE, 0x35, 0x50, 0x5B, 0x5A, 0x5A,  // paddd xmm6, xmmword ptr [table + 0x50]
    0x66, 0x0F, 0x66, 0x25, 0x10, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm4, xmmword ptr [table + 0x10]
    0x66, 0x0F, 0xDB, 0xF4,                          // pand xmm6, xmm4
    0x66, 0x0F, 0xDF, 0xE5,                          // pandn xmm4, xmm5
    0x66, 0x0F, 0xEB, 0xF4,                          // por xmm6, xmm4
    0x66, 0x0F, 0xFE, 0xF7,                          // paddd xmm6, xmm7
    0x66, 0x0F, 0x72, 0xF6, 0x10,                    // pslld xmm6, 0x10
    0x66, 0x0F, 0x72, 0xE6, 0x10,                    // psrad xmm6, 0x10
    0x66, 0x0F, 0x6B, 0xD6,                          // packssdw xmm2, xmm6
    0x0F, 0x12, 0x66, 0x38,                          // movlps xmm4, qword ptr [esi + 0x38]
    0x0F, 0x16, 0x62, 0x48,                          // movhps xmm4, qword ptr [edx + 0x48]
    0x66, 0x0F, 0x6F, 0xEC,                          // movdqa xmm5, xmm4
    0x66, 0x0F, 0xDB, 0x2D, 0x00, 0x5B, 0x5A, 0x5A,  // pand xmm5, xmmword ptr [table + 0x0]
    0x66, 0x0F, 0x6F, 0xF5,                          // movdqa xmm6, xmm5
    0x66, 0x0F, 0x66, 0x35, 0x60, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm6, xmmword ptr [table + 0x60]
    0x66, 0x0F, 0xFA, 0x2D, 0x70, 0x5B, 0x5A, 0x5A,  // psubd xmm5, xmmword ptr [table + 0x70]
    0x66, 0x0F, 0xEF, 0x2D, 0x80, 0x5B, 0x5A, 0x5A,  // pxor xmm5, xmmword ptr [table + 0x80]
    0x66, 0x0F, 0x66, 0x2D, 0x90, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm5, xmmword ptr [table + 0x90]
    0x66, 0x0F, 0xDF, 0xF5,                          // pandn xmm6, xmm5
    0x0F, 0x50, 0xC6,                                // movmskps eax, xmm6
    0x83, 0xF8, 0x0F,                                // cmp eax, 0xf
    0x0F, 0x85, 0xD2, 0x00, 0x00, 0x00,              // jne 0x4b8
    0x66, 0x0F, 0x6F, 0xFC,                          // movdqa xmm7, xmm4
    0x66, 0x0F, 0x72, 0xD7, 0x1F,                    // psrld xmm7, 0x1f
    0x66, 0x0F, 0x72, 0xF7, 0x0F,                    // pslld xmm7, 0xf
    0x66, 0x0F, 0xDB, 0x25, 0x00, 0x5B, 0x5A, 0x5A,  // pand xmm4, xmmword ptr [table + 0x0]
    0x66, 0x0F, 0x6F, 0xEC,                          // movdqa xmm5, xmm4
    0x66, 0x0F, 0x66, 0x2D, 0x20, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm5, xmmword ptr [table + 0x20]
    0x66, 0x0F, 0x72, 0xD5, 0x1F,                    // psrld xmm5, 0x1f
    0x66, 0x0F, 0x6F, 0xF4,                          // movdqa xmm6, xmm4
    0x66, 0x0F, 0xFA, 0x35, 0x30, 0x5B, 0x5A, 0x5A,  // psubd xmm6, xmmword ptr [table + 0x30]
    0x66, 0x0F, 0xDB, 0xF4,                          // pand xmm6, xmm4
    0x66, 0x0F, 0xDB, 0x35, 0x40, 0x5B, 0x5A, 0x5A,  // pand xmm6, xmmword ptr [table + 0x40]
    0x66, 0x0F, 0xFE, 0xF4,                          // paddd xmm6, xmm4
    0x66, 0x0F, 0x72, 0xD6, 0x0D,                    // psrld xmm6, 0xd
    0x66, 0x0F, 0xFE, 0x35, 0x50, 0x5B, 0x5A, 0x5A,  // paddd xmm6, xmmword ptr [table + 0x50]
    0x66, 0x0F, 0x66, 0x25, 0x10, 0x5B, 0x5A, 0x5A,  // pcmpgtd xmm4, xmmword ptr [table + 0x10]
    0x66, 0x0F, 0xDB, 0xF4,                          // pand xmm6, xmm4
    0x66, 0x0F, 0xDF, 0xE5,                          // pandn xmm4, xmm5
    0x66, 0x0F, 0xEB, 0xF4,                          // por xmm6, xmm4
    0x66, 0x0F, 0xFE, 0xF7,                          // paddd xmm6, xmm7
    0x66, 0x0F, 0x72, 0xF6, 0x10,                    // pslld xmm6, 0x10
    0x66, 0x0F, 0x72, 0xE6, 0x10,                    // psrad xmm6, 0x10
    0x66, 0x0F, 0x6B, 0xF6,                          // packssdw xmm6, xmm6
    0xF3, 0x0F, 0x7F, 0x49, 0x08,                    // movdqu xmmword ptr [ecx + 8], xmm1
    0xF3, 0x0F, 0x7F, 0x51, 0x18,                    // movdqu xmmword ptr [ecx + 0x18], xmm2
    0x66, 0x0F, 0x7E, 0x71, 0x28,                    // movd dword ptr [ecx + 0x28], xmm6
    0x66, 0x0F, 0xC5, 0xC6, 0x02,                    // pextrw eax, xmm6, 2
    0x66, 0x89, 0x41, 0x2C,                          // mov word ptr [ecx + 0x2c], ax
    0x66, 0x0F, 0x6E, 0x7C, 0x24, 0x20,              // movd xmm7, dword ptr [esp + 0x20]
    0x0F, 0x5B, 0xFF,                                // cvtdq2ps xmm7, xmm7
    0xF3, 0x0F, 0x11, 0x7C, 0x24, 0x38,              // movss dword ptr [esp + 0x38], xmm7
    0xF3, 0x0F, 0x10, 0x7C, 0x24, 0x50,              // movss xmm7, dword ptr [esp + 0x50]
    0xF3, 0x0F, 0x59, 0x7C, 0x24, 0x74,              // mulss xmm7, dword ptr [esp + 0x74]
    0xF3, 0x0F, 0x11, 0x7C, 0x24, 0x70,              // movss dword ptr [esp + 0x70], xmm7
    0xF3, 0x0F, 0x10, 0x42, 0x4C,                    // movss xmm0, dword ptr [edx + 0x4c]
    0xF3, 0x0F, 0x11, 0x44, 0x24, 0x74,              // movss dword ptr [esp + 0x74], xmm0
    0x66, 0x0F, 0x7E, 0xDA,                          // movd edx, xmm3
    0x8D, 0x44, 0x24, 0x74,                          // lea eax, [esp + 0x74]
    0x83, 0xEC, 0x10,                                // sub esp, 0x10
    0x89, 0x04, 0x24,                                // mov dword ptr [esp], eax
    0xE8, 0x11, 0x5A, 0x5A, 0x5A,                    // call 0x5a5a5ec4
    0xE9, 0x13, 0x5A, 0x5A, 0x5A,                    // jmp 0x5a5a5ecb
    0x66, 0x0F, 0x7E, 0xDA,                          // fallback: movd edx, xmm3
    0x8D, 0x44, 0x24, 0x38,                          // lea eax, [esp + 0x38]
    0x50,                                            // push eax
    0xE9, 0x12, 0x5A, 0x5A, 0x5A,                    // jmp 0x5a5a5ed8
};
// The stub's constants, 16-byte vectors: |f|, 0x387fffff, 0x33000400, 1, 0x1fff, 0x4000, 0x477fffff, 0x33800000, 0x80000000,
// 0x84ffffff.
constexpr std::array<uint32_t, 40> HALF_ROWS_CONSTANTS = {
    0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
    0x387FFFFF, 0x387FFFFF, 0x387FFFFF, 0x387FFFFF,
    0x33000400, 0x33000400, 0x33000400, 0x33000400,
    0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00001FFF, 0x00001FFF, 0x00001FFF, 0x00001FFF,
    0x00004000, 0x00004000, 0x00004000, 0x00004000,
    0x477FFFFF, 0x477FFFFF, 0x477FFFFF, 0x477FFFFF,
    0x33800000, 0x33800000, 0x33800000, 0x33800000,
    0x80000000, 0x80000000, 0x80000000, 0x80000000,
    0x84FFFFFF, 0x84FFFFFF, 0x84FFFFFF, 0x84FFFFFF,
};

// 8. The effect tree's child sum (+0x1890b90; E3P of the CPU plan): `sum += child->vtable[0x1c]()` over the vector at
// [this+0x68] of [this+0x64] pointers, the bounds read again after every call. The children are scattered heap objects, so
// the loop waits for each one's first line. The stub at the loop's bottom (the `lea ecx, [ecx + edx*4]; cmp esi, ecx` that
// the `jne` tests; esi is the next item's slot, ecx the vector's begin, edx its size) prefetches the children K - 1 and K / 2 - 1
// places after the next one (K = CHILD_SUM_AHEAD): only when the far slot's 4 bytes [esi + 4K - 4, esi + 4K) lie wholly inside
// [begin, end) as the vector is right now, with no wrap of the address space (a vector the call reallocated puts the old esi
// outside it: no load; esi need not be 4 apart from the begin). The near slot [esi + 2K - 4, esi + 2K) is then inside too:
// begin <= esi <= esi + 2K - 4 and esi + 2K <= esi + 4K <= end, no wrap. `prefetcht0` itself never faults. Same results: the
// flags the `jne` sees are the game's `cmp esi, ecx`, ecx is the end as the game computes it, edx is not touched, and eax, which
// the game has added to the sum and next overwrites (the loop's top, `mov eax, edi` at the exit), is the one register that
// differs. No store, and loads only inside the vector's size * 4 bytes, which any vector keeps committed after a call returns
// (the one assumption): every register, the flags and the memory at the function's return are the game's.
constexpr std::string_view CHILD_SUM =
    "53 8B D9 56 57 33 FF 8B 73 68 8B 43 64 8D 04 86 3B F0 74 19 8B 0E 8B 01 FF 50 1C 8B 53 64 83 C6 04 8B 4B 68 03 F8 8D 0C 91 "
    "3B F1 75 E7 8B C7 5F 5E 5B C3";
constexpr size_t CHILD_SUM_SITE = 38;    // lea ecx, [ecx + edx*4]; cmp esi, ecx
constexpr size_t CHILD_SUM_RESUME = 43;  // jne to the loop's top
constexpr uint8_t CHILD_SUM_AHEAD = 4;
static_assert(CHILD_SUM_AHEAD >= 2 && CHILD_SUM_AHEAD % 2 == 0 && CHILD_SUM_AHEAD * 4 <= 127);  // K / 2 a whole slot; the add's immediate a signed byte
// The stub for K = `ahead`: the children in the slots [esi + 4K - 4, esi + 4K) and [esi + 2K - 4, esi + 2K), K - 1 and K / 2 - 1 places after the
// next one, both behind the one check of the far slot (the test builds the stub for other K and the other forms for its timing).
constexpr std::array<uint8_t, 37> ChildSumStub(uint8_t ahead) {
  return {
      0x39, 0xCE,                                       // cmp esi, ecx                    esi against the begin
      0x8D, 0x0C, 0x91,                                 // lea ecx, [ecx + edx*4]          the end (no flags)
      0x72, 0x17,                                       // jb skip                         esi below the begin: stale, no load
      0x89, 0xF0,                                       // mov eax, esi
      0x83, 0xC0, static_cast<uint8_t>(ahead * 4),      // add eax, 4K
      0x72, 0x10,                                       // jc skip                         esi + 4K past 2^32
      0x39, 0xC8,                                       // cmp eax, ecx
      0x77, 0x0C,                                       // ja skip                         the far slot is not wholly inside
      0x8B, 0x40, 0xFC,                                 // mov eax, dword ptr [eax - 4]     the child in the far slot
      0x0F, 0x18, 0x08,                                 // prefetcht0 byte ptr [eax]
      0x8B, 0x46, static_cast<uint8_t>(ahead * 2 - 4),  // mov eax, dword ptr [esi + 2K - 4]  the child in the near slot
      0x0F, 0x18, 0x08,                                 // prefetcht0 byte ptr [eax]
      0x39, 0xCE,                                       // skip: cmp esi, ecx              the flags the game's jne tests
      0xE9, 0x00, 0x00, 0x00, 0x00,                     // jmp the loop's jne (patched in Prepare)
  };
}
constexpr std::array<uint8_t, 37> CHILD_SUM_STUB = ChildSumStub(CHILD_SUM_AHEAD);

// 9. The particle writer's float4 reloads (+0x18960d0, thiscall(float, const float*), ret 8; the effect update calls it once for
// every particle: 4.9 ms of the render thread a frame in the stress battle, 1.77 ms of it the two reloads). It builds two float4s
// (a colour and a vector) with four 4-byte `movss` stores each into the frame at [esp + 0x40 .. 0x4f] and reloads each with one 16-byte
// `movups xmm0, [esp + 0x40]` to store it into a vertex stream. A load that spans four stores can't be forwarded from the store buffer:
// it waits until they have reached the cache, and stores drain in program order, so it also waits for every older store (the
// streams' other rows). The stub loads each lane from the address and size of the store that wrote it: `movss xmm0, [esp + 0x40]`, then
// `insertps xmm0, [esp + 0x44 / 0x48 / 0x4c], 0x10 / 0x20 / 0x30` (SSE4.1: without it the sites are not looked for). Same results:
// xmm0 holds the same 128 bits (moves only: NaN payloads, subnormals and the sign of zero as they are; MXCSR untouched), no flag and
// no other register changes, no store, and the four loads read the same 16 stack bytes after the same stores. Found by the function's
// tail (+0x18967db, its call's displacement wild), at the Steam build's place first, then anywhere in the code: another mod's change
// in the tail, a site included, leaves both alone. No branch lands inside either site (the whole DLL's relative branches and absolute
// pointers, Attila's and Thrones').
constexpr std::string_view FLOAT4_RELOAD =
    "8D 44 24 40 8B CF 50 E8 ?? ?? ?? ?? 8B 47 10 8B 40 78 80 78 2C 00 74 07 F3 0F 10 4F 78 EB 03 0F 57 C9 8B 54 24 7C "
    "F3 0F 10 44 24 4C F3 0F 59 C1 8B 02 F3 0F 11 44 24 4C F3 0F 10 45 08 F3 0F 11 40 0C 8B 42 04 F3 0F 10 84 24 80 00 "
    "00 00 F3 0F 11 40 0C 8B 47 14 8B 40 08 66 0F 6E 40 18 8B 42 0C 0F 5B C0 F3 0F 11 00 8B 47 0C F3 0F 10 40 70 F3 0F "
    "59 47 70 8B 42 0C F3 0F 11 40 04 8B 42 08 0F 10 44 24 40 0F 11 00 8B 02 F3 0F 7E 47 18 8B 4F 20 66 0F D6 00 F3 0F "
    "7E 84 24 90 00 00 00 89 48 08 8B 4A 04 8B 84 24 98 00 00 00 66 0F D6 01 F3 0F 10 44 24 14 F3 0F 11 44 24 40 F3 0F "
    "10 44 24 10 F3 0F 11 44 24 44 F3 0F 10 44 24 18 89 41 08 8B 42 10 F3 0F 11 44 24 48 F3 0F 10 44 24 1C F3 0F 11 44 "
    "24 4C 0F 10 44 24 40 0F 11 00 8B 4F 0C 8B 42 14 0F 10 81 80 00 00 00 0F 11 00 5F 5E 8B E5 5D C2 08 00";
constexpr std::array<size_t, 2> FLOAT4_RELOAD_AT = {128, 230};  // movups xmm0, xmmword ptr [esp + 0x40]: the colour's, the vector's
constexpr std::array<uint8_t, 35> FLOAT4_RELOAD_STUB = {
    0xF3, 0x0F, 0x10, 0x44, 0x24, 0x40,                    // movss xmm0, dword ptr [esp + 0x40]
    0x66, 0x0F, 0x3A, 0x21, 0x44, 0x24, 0x44, 0x10,        // insertps xmm0, dword ptr [esp + 0x44], 0x10
    0x66, 0x0F, 0x3A, 0x21, 0x44, 0x24, 0x48, 0x20,        // insertps xmm0, dword ptr [esp + 0x48], 0x20
    0x66, 0x0F, 0x3A, 0x21, 0x44, 0x24, 0x4C, 0x30,        // insertps xmm0, dword ptr [esp + 0x4c], 0x30
    0xE9, 0x00, 0x00, 0x00, 0x00,                          // jmp back (the game's movups [eax], xmm0 after the site)
};
// Whether the CPU has insertps; the test turns it off to see the sites left out.
static bool sse4_1 = (IsProcessorFeaturePresent(PF_SSE4_1_INSTRUCTIONS_AVAILABLE) != FALSE);

// 10. The particle update loop's prefetch (the emitter update +0x1893470, one caller, its loop +0x1893820 .. +0x1893888; Faster Effect Prefetch). The
// loop takes the particle [[edi + 0x68] + ebx*4] while ebx < [edi + 0x64] (the vector's array and size read again after every call), stores to it
// (+0x18, +0x20, +0x30), calls its update through the vtable (+0xc) and ends with `cmp ebx, dword ptr [edi + 0x64]; jb the top`: 1.8 ms of the render
// thread a frame in the stress battle, 0.84 of it the first load of the particle (scattered heap objects of four lines) and 0.24 the array. The stub at
// that bottom, where ebx is already the next particle's index, loads the slot K = PARTICLE_PREFETCH_AHEAD particles after the next one and prefetches the
// particle's four lines, only when the slot is inside the size as it is right now: the array the game's next load reads holds size * 4 bytes (the one
// assumption, as in 8.), and the lea wraps like the load's address does, so an index past 2^32 loads from inside the vector too. The prefetch never
// faults. The stub spends esi, which is dead there (the loop's top loads it, the way out only pops it and calls a function that saves it: checked in the
// DLL's bytes, Attila's and Thrones'); the flags the `jb` tests are the game's `cmp`; no store, no other register: every register but esi, the flags and
// all memory at the loop's end are the game's. Found by the loop's 104 bytes and the 36 after it (the way out the esi argument stands on, its call wild), at
// the Steam build's place first, then anywhere in the code; no branch lands inside the site (the whole DLL's relative branches and absolute pointers,
// Attila's and Thrones'), no site or hook of the Attila CPU Performance Fix 7.0 lies in the loop.
constexpr std::string_view PARTICLE_LOOP =
    "8B 47 68 51 F3 0F 11 04 24 51 8B 34 98 FF 75 0C 66 0F D6 6E 18 89 56 20 8B 16 0F 11 76 30 83 7F 08 01 0F 94 C0 0F B6 C0 50 51 8B CE F3 0F 11 24 24 "
    "FF 52 0C 80 7E 64 00 75 08 C6 47 6C 00 32 C0 EB 03 8A 47 6C F3 0F 10 44 24 10 43 F3 0F 10 64 24 0C 0F 28 6C 24 40 0F 10 74 24 30 8B 4D 10 8B 54 24 14 "
    "3B 5F 64 72 98 84 C0 74 10 80 7F 6D 00 74 0A 80 7F 04 00 74 04 C6 47 6E 01 8B CF E8 ?? ?? ?? ?? 5F 5E 5B 8B E5 5D C2 0C 00";
constexpr size_t PARTICLE_LOOP_SITE = 99;   // cmp ebx, dword ptr [edi + 0x64]; jb the loop's top
constexpr size_t PARTICLE_LOOP_EXIT = 104;  // test al, al: the code after the loop
constexpr uint8_t PARTICLE_PREFETCH_AHEAD = 2;
static_assert(PARTICLE_PREFETCH_AHEAD >= 1 && PARTICLE_PREFETCH_AHEAD * 4 <= 127);  // the slot's displacement is a signed byte
// The stub for the particle `ahead` places after the next one (the test builds the others for its timing).
constexpr std::array<uint8_t, 50> ParticlePrefetchStub(uint8_t ahead) {
  return {
      0x8D, 0x73, ahead,                                  // lea esi, [ebx + ahead]
      0x3B, 0x77, 0x64,                                   // cmp esi, dword ptr [edi + 0x64]  inside the size as it is now
      0x73, 0x1C,                                         // jae skip
      0x8B, 0x77, 0x68,                                   // mov esi, dword ptr [edi + 0x68]  the array as it is now
      0x8B, 0x74, 0x9E, static_cast<uint8_t>(ahead * 4),  // mov esi, dword ptr [esi + ebx*4 + ahead*4]
      0x0F, 0x18, 0x0E,                                   // prefetcht0 byte ptr [esi]
      0x0F, 0x18, 0x4E, 0x40,                             // prefetcht0 byte ptr [esi + 0x40]
      0x0F, 0x18, 0x8E, 0x80, 0x00, 0x00, 0x00,           // prefetcht0 byte ptr [esi + 0x80]
      0x0F, 0x18, 0x8E, 0xC0, 0x00, 0x00, 0x00,           // prefetcht0 byte ptr [esi + 0xc0]
      0x3B, 0x5F, 0x64,                                   // skip: cmp ebx, dword ptr [edi + 0x64]  the flags the game's jb tests
      0x0F, 0x82, 0x00, 0x00, 0x00, 0x00,                 // jb the loop's top (patched in Prepare)
      0xE9, 0x00, 0x00, 0x00, 0x00,                       // jmp the code after the loop (patched in Prepare)
  };
}
constexpr std::array<uint8_t, 50> PARTICLE_PREFETCH_STUB = ParticlePrefetchStub(PARTICLE_PREFETCH_AHEAD);

// 11. The emitter update loop's prefetch (the effect update +0x1893330, one caller, the loop of its serial arm +0x18933c0 .. +0x18933e5; Faster Effect
// Prefetch). The loop calls the emitter update (+0x1893470, thiscall, three stack arguments) with the emitter [[container + 8] + i*8] of the vector
// at [this + 0x14] (+4 its size, +8 its elements, 8 bytes each: the pointer of a shared_ptr first) for i from 0 on while `inc esi; cmp esi, [esp + 0x34];
// je the way out` says so: the count was read once before the loop ([esp + 0x34] = [this + 0x18]), and the container's address sits in the slot
// [esp + 0x20] the loop reloads ebx from. Each update starts on lines nobody touched since the last frame (the emitters and their definitions are
// scattered heap objects): 1.1 ms of the render thread a frame in the stress battle is the first load of the definition, [[emitter + 0x58] + 0x2c] (in
// 0x1840520, which the update calls with the emitter). The stub at the loop's bottom (the 5 bytes `inc esi; cmp esi, [esp + 0x34]`) takes the slot K =
// EMITTER_PREFETCH_AHEAD after the next emitter's and prefetches that emitter's first two lines and its definition's first line, only when the slot is
// below the count the loop runs to and holds a pointer.
// Same results: only ecx is written, which both ways on from the site write before they read it (the loop's top `mov ecx, [ebx + 8]`, the way out
// `mov ecx, [edi + 0x18]`); the flags the `je` tests are the game's `cmp esi, [esp + 0x34]`, run again at the end; no store. The loads are the game's own
// next loads or inside what it loads: [esp + 0x34] and [esp + 0x20] (the next round reads both, and there is one: the slot after the next is below the
// count), [container + 8], the slot (below the count: the loop reads it itself), and one real load of the emitter, [emitter + 0x58]. That load cannot
// fault, and the prefetches never do: the vector holds a reference to every emitter below the cached count (the shared_ptr in the slot) for the whole
// loop, because the loop only reads slots below that count, the elements are removed after the loop (+0x18933eb ..: the ones the emitter's byte +0x6e
// marks, swapped with the last, size decremented), and the update cannot change the vector (the parallel arm calls the same update from worker
// threads over slots of this vector, +0x17bc0d1 / +0x17bc179 / +0x182c951: a change would be a data race in the game itself). A non-null slot below the
// count therefore points at a live emitter, whose shared_ptr of the definition at +0x58 / +0x5c (read by every update) lies inside it; a null slot is
// skipped before the load. Reading the container from [esp + 0x20], not from ebx, keeps the stub independent of the registers the call keeps. Found by
// the whole function's bytes (its five calls and two `mov ecx, imm32` wild: the same bytes in Thrones' copy), at the Steam build's place first, then
// anywhere in the code; no branch lands inside the site (the whole DLL's relative branches and absolute pointers, Attila's and Thrones'), no site or
// hook of the Attila CPU Performance Fix 7.0 lies in the function.
constexpr std::string_view EMITTER_LOOP =
    "83 EC 20 57 8B F9 B9 ?? ?? ?? ?? 89 7C 24 04 E8 ?? ?? ?? ?? 84 C0 0F 84 19 01 00 00 8B 44 24 2C B9 ?? ?? ?? ?? 53 55 8B "
    "6C 24 30 8D 5F 14 56 8B 77 18 89 44 24 28 8B 44 24 3C 89 5C 24 20 89 6C 24 24 89 44 24 2C C7 44 24 14 00 00 00 00 89 74 "
    "24 34 89 74 24 18 C7 44 24 1C 01 00 00 00 E8 ?? ?? ?? ?? 84 C0 74 14 8D 44 24 20 50 8D 44 24 18 50 E8 ?? ?? ?? ?? 83 C4 "
    "08 EB 40 85 F6 74 3C 8B 44 24 38 33 F6 8B 7C 24 3C 0F 1F 80 00 00 00 00 8B 4B 08 57 50 55 8B 0C F1 E8 ?? ?? ?? ?? 46 3B "
    "74 24 34 74 12 8B 7C 24 2C 8B 44 24 28 8B 6C 24 24 8B 5C 24 20 EB D9 8B 7C 24 10 8B 4F 18 8D 59 FF 3B D9 73 6D 8B 57 1C "
    "8B 04 DA 80 78 6E 00 74 59 8D 04 CA 8B 48 FC 85 C9 74 07 F0 FF 41 04 8B 48 FC 8B 74 DA 04 8B 40 F8 89 04 DA 89 4C DA 04 "
    "85 F6 74 21 83 C8 FF F0 0F C1 46 04 75 17 8B 06 8B CE FF 10 83 C8 FF F0 0F C1 46 08 75 07 8B 06 8B CE FF 50 04 FF 4F 18 "
    "8B 4F 18 8B 47 1C 8D 04 C8 50 E8 ?? ?? ?? ?? 83 C4 04 8B 4F 18 4B 3B D9 72 93 5E 5D 5B 5F 83 C4 20 C2 0C 00";
constexpr size_t EMITTER_LOOP_SITE = 158;  // inc esi; cmp esi, dword ptr [esp + 0x34]; the je after it is where the stub goes on
constexpr uint8_t EMITTER_PREFETCH_AHEAD = 1;
static_assert(EMITTER_PREFETCH_AHEAD >= 1 && EMITTER_PREFETCH_AHEAD * 8 <= 127);  // the slot's displacement is a signed byte
// The stub for the emitter `ahead` places after the next one (the test builds the others for its timing).
constexpr std::array<uint8_t, 48> EmitterPrefetchStub(uint8_t ahead) {
  return {
      0x46,                                                // inc esi                          the next emitter's index
      0x8D, 0x4E, ahead,                                   // lea ecx, [esi + ahead]
      0x3B, 0x4C, 0x24, 0x34,                              // cmp ecx, dword ptr [esp + 0x34]  below the count the loop runs to
      0x73, 0x1D,                                          // jae skip
      0x8B, 0x4C, 0x24, 0x20,                              // mov ecx, dword ptr [esp + 0x20]  the container, as the game reloads it
      0x8B, 0x49, 0x08,                                    // mov ecx, dword ptr [ecx + 8]     the elements as they are now
      0x8B, 0x4C, 0xF1, static_cast<uint8_t>(ahead * 8),   // mov ecx, dword ptr [ecx + esi*8 + ahead*8]
      0x85, 0xC9,                                          // test ecx, ecx
      0x74, 0x0E,                                          // je skip
      0x0F, 0x18, 0x09,                                    // prefetcht0 byte ptr [ecx]
      0x0F, 0x18, 0x49, 0x40,                              // prefetcht0 byte ptr [ecx + 0x40]
      0x8B, 0x49, 0x58,                                    // mov ecx, dword ptr [ecx + 0x58]  the definition: the stub's one real load
      0x0F, 0x18, 0x49, 0x2C,                              // prefetcht0 byte ptr [ecx + 0x2c]
      0x3B, 0x74, 0x24, 0x34,                              // skip: cmp esi, dword ptr [esp + 0x34]  the flags the game's je tests
      0xE9, 0x00, 0x00, 0x00, 0x00,                        // jmp the game's je (patched in Prepare)
  };
}
constexpr std::array<uint8_t, 48> EMITTER_PREFETCH_STUB = EmitterPrefetchStub(EMITTER_PREFETCH_AHEAD);

// Thrones of Britannia's build (empire.retail.dll 64A47C87; test/tw_engine_code with its DLL): float to half, fmod and the
// ready check the same bytes at other RVAs; the soldier draw's truncation the same instructions at other stack slots
// (control word 0x20, float 0x24, its control word | 0xc00 0x28, integer 0x1c after the push), so its own stub with the
// same steps; the CRT's _matherr check compiled otherwise (eax decoded, not edx: the stub only calls it and tests al, eax
// ecx edx saved).
constexpr uint32_t THRONES_TRUNCATION_RVA = 0x186aa3a;
constexpr std::string_view THRONES_TRUNCATION =
    "8B 84 24 7C 01 00 00 89 84 24 84 00 00 00 50 D9 44 24 24 9B D9 7C 24 20 8B 44 24 20 0D 00 0C 00 00 89 44 24 28 "
    "D9 6C 24 28 DB 5C 24 1C D9 6C 24 20 58 0F B7 44 24 18 66 0F 6E C0 0F 5B C0 F3 0F 11 84 24";
constexpr std::array<uint8_t, 44> THRONES_TRUNCATION_STUB = TruncationStub(0x20, 0x28, 0x24, 0x1C);
constexpr std::string_view THRONES_FMOD_MATHERR = "A1 ?? ?? ?? ?? 8B C8 33 05 ?? ?? ?? ?? 83 E1 1F D3 C8 85 C0 0F 95 C0 C3";

// Rome II's build (empire.retail.dll 6AB978BD; test/tw_engine_code with its DLL): float to half and fmod the same bytes at
// other RVAs, its _matherr check as Thrones'; the soldier draw's truncation at other stack slots (control word 0x18,
// float 0x20, its control word | 0xc00 0x28, integer 0x24 after the push).
constexpr std::string_view ROME2_TRUNCATION =
    "8B 84 24 38 02 00 00 89 84 24 84 00 00 00 50 D9 44 24 20 9B D9 7C 24 18 8B 44 24 18 0D 00 0C 00 00 89 44 24 28 "
    "D9 6C 24 28 DB 5C 24 24 D9 6C 24 18 58 0F B7 44 24 20 66 0F 6E C0 0F 5B C0 F3 0F 11 84 24";
constexpr std::array<uint8_t, 44> ROME2_TRUNCATION_STUB = TruncationStub(0x18, 0x28, 0x20, 0x24);

// What differs between the games' builds. Thrones' render list push and ready check the same bytes; Rome II has neither
// (rva 0: not looked for).
struct Build {
  uint32_t truncation_rva;
  std::string_view truncation;
  std::span<const uint8_t> truncation_stub;
  uint32_t half_rva;
  uint32_t fmod_rva;
  std::string_view fmod_matherr;
  uint32_t list_push_rva;
  uint32_t ready_check_rva;
  uint32_t job_wake_all_rva;
  uint32_t job_sleep_rva;
  uint32_t job_worker_destructor_rva;
  uint32_t half_rows_rva;
  uint32_t child_sum_rva;
  uint32_t float4_reload_rva;  // the particle writer's tail (FLOAT4_RELOAD's first byte)
  uint32_t particle_loop_rva;  // the particle update loop (PARTICLE_LOOP's first byte)
  uint32_t emitter_loop_rva;   // the emitter update loop's function (EMITTER_LOOP's first byte)
};
constexpr Build ATTILA_BUILD = {TRUNCATION_RVA, TRUNCATION, TRUNCATION_STUB, HALF_RVA, FMOD_RVA, FMOD_MATHERR, LIST_PUSH_RVA, READY_CHECK_RVA, 0x16b5550, 0x16b5520, 0x1689b90, 0x1907511, 0x1890b90, 0x18967db, 0x1893820, 0x1893330};
constexpr Build THRONES_BUILD = {THRONES_TRUNCATION_RVA, THRONES_TRUNCATION, THRONES_TRUNCATION_STUB, 0x1707eb0, 0x1b0a030, THRONES_FMOD_MATHERR, 0x17df0a9, 0x18d8400, 0x1722e60, 0x1722e30, 0x16f7460, 0x197d841, 0x1905ef0, 0x190bb3b, 0x1908b80, 0x1908690};
constexpr Build ROME2_BUILD = {0x14b433c, ROME2_TRUNCATION, ROME2_TRUNCATION_STUB, 0x1368030, 0x169ca30, THRONES_FMOD_MATHERR, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::array<Build, 3> BUILDS = {ATTILA_BUILD, THRONES_BUILD, ROME2_BUILD};  // by Game

// Where each stub sits in the page.
constexpr size_t TRUNCATION_PAGE = 0x000;
constexpr size_t HALF_PAGE = 0x040;
constexpr size_t FMOD_PAGE = 0x180;
constexpr size_t LIST_PUSH_PAGE = 0x240;
constexpr size_t READY_CHECK_PAGE = 0x280;
constexpr size_t JOB_PAGE = 0x300;
constexpr size_t HALF_ROWS_PAGE = 0x500;
constexpr size_t FLOAT4_RELOAD_PAGE = 0xB20;    // one stub for each of the two reloads, a stride apart
constexpr size_t FLOAT4_RELOAD_STRIDE = 0x30;
constexpr size_t PARTICLE_PREFETCH_PAGE = 0xB80;
constexpr size_t EMITTER_PREFETCH_PAGE = 0xBC0;
constexpr size_t CHILD_SUM_PAGE = 0xC00;
constexpr size_t PAGE_SIZE = 0x1000;
static_assert(TRUNCATION_PAGE + TRUNCATION_STUB.size() <= HALF_PAGE  // the three games' truncation stubs are one type
              && HALF_PAGE + HALF_STUB.size() + HALF_SIZE <= FMOD_PAGE
              && FMOD_PAGE + FMOD_STUB.size() <= LIST_PUSH_PAGE
              && LIST_PUSH_PAGE + LIST_PUSH_STUB.size() <= READY_CHECK_PAGE
              && READY_CHECK_PAGE + READY_CHECK_STUB.size() <= JOB_PAGE
              && JOB_PAGE + JOB_STUB.size() <= HALF_ROWS_PAGE
              && HALF_ROWS_PAGE + HALF_ROWS_CONSTANTS_AT + sizeof(HALF_ROWS_CONSTANTS) <= FLOAT4_RELOAD_PAGE
              && FLOAT4_RELOAD_STUB.size() <= FLOAT4_RELOAD_STRIDE
              && FLOAT4_RELOAD_PAGE + FLOAT4_RELOAD_AT.size() * FLOAT4_RELOAD_STRIDE <= PARTICLE_PREFETCH_PAGE
              && PARTICLE_PREFETCH_PAGE + PARTICLE_PREFETCH_STUB.size() <= EMITTER_PREFETCH_PAGE
              && EMITTER_PREFETCH_PAGE + EMITTER_PREFETCH_STUB.size() <= CHILD_SUM_PAGE
              && CHILD_SUM_PAGE + CHILD_SUM_STUB.size() <= PAGE_SIZE);

// Which sites each row writes (bits by `sites` index).
constexpr uint32_t FLOAT4_RELOAD_SITES = 0b110000000000;  // the particle writer's two reloads
constexpr uint32_t CPU_CODE_SITES = 0b100010111 | FLOAT4_RELOAD_SITES;  // Faster Game CPU Code
constexpr uint32_t LIST_SITES = 0b01000;      // Faster Render Lists
constexpr uint32_t JOB_SITES = 0b11100000;    // Faster Job Wake-Ups: WakeAll, Sleep, the destructor's call of Stop
constexpr uint32_t PARTICLE_PREFETCH_BIT = 0b1000000000000;  // the particle update loop's prefetch
constexpr uint32_t EMITTER_PREFETCH_BIT = 0b10000000000000;  // the emitter update loop's prefetch
constexpr uint32_t PREFETCH_SITES = 0b1000000000 | PARTICLE_PREFETCH_BIT | EMITTER_PREFETCH_BIT;  // Faster Effect Prefetch: the effect tree's child sum, the particle and emitter loops
constexpr uint32_t HALF_FUNCTION_BIT = 0b10;  // the half function, which the half rows' stub calls as the game does
constexpr uint32_t HALF_ROWS_BIT = 0b100000000;
// The rows by name, for the log lines.
constexpr std::array<std::pair<uint32_t, std::string_view>, 4> ROWS = {{{CPU_CODE_SITES, "Faster Game CPU Code"},
                                                                         {LIST_SITES, "Faster Render Lists"},
                                                                         {JOB_SITES, "Faster Job Wake-Ups"},
                                                                         {PREFETCH_SITES, "Faster Effect Prefetch"}}};

// The first instruction (or two or three, whole) of each site: 5 bytes that become a jump into the page (the render list
// push's: a 4-byte instruction and the first byte of the next, both run by its stub).
constexpr size_t SITE_SIZE = 5;

struct Site {
  std::string_view name;
  uint8_t* at = nullptr;  // null: not found, or not the game's code as expected
  std::array<uint8_t, SITE_SIZE> game = {};
  std::array<uint8_t, SITE_SIZE> jump = {};
  uint8_t opcode = 0xE9;  // jmp; the worker destructor's call of Stop stays a call (E8)
};

static std::array<Site, 14> sites = {{{.name = "soldier draw rounding"},
                                     {.name = "half floats"},
                                     {.name = "fmod"},
                                     {.name = "render list pushes"},
                                     {.name = "ready checks"},
                                     {.name = "job wake-ups"},
                                     {.name = "job sleeps"},
                                     {.name = "job stops", .opcode = 0xE8},
                                     {.name = "half rows"},
                                     {.name = "child prefetch"},
                                     {.name = "particle colour reload"},
                                     {.name = "particle vector reload"},
                                     {.name = "particle prefetch"},
                                     {.name = "emitter prefetch"}}};
static uint8_t* job_table = nullptr;
static uint8_t* page = nullptr;
static bool prepared = false;
static uint32_t found = 0;    // the sites found (bits by index)
static uint32_t written = 0;  // the sites holding their jump
static uint32_t asked = 0;    // the sites the rows last asked for
static uint32_t failed_writes = 0;
static bool job_missing_logged = false;
static bool prefetch_missing_logged = false;
// Presents with a thread inside a site before giving up (until the row changes); each try snapshots the system's threads
// (~0.1 s with some antivirus hooks; 3 of 200 writes waited once in the test, four threads running the sites nonstop).
constexpr uint32_t MAX_FAILED_WRITES = 30;

// The jumps of the sites in `patched` and the game's bytes in the others, written (where that changes `written`) while
// every other thread of the process is suspended and none stands inside a changing site past its first byte (it would go
// on in the middle of the jump). False: one did (or its position is unknown), nothing written. From the first suspension
// to the last resume nothing allocates, logs or takes a lock: a suspended thread may hold it. A thread started after the
// snapshot isn't held (as with Detours). A site whose bytes are not what our last write left (another mod hooked or patched
// it since Prepare) is not touched: `stood_down` names it, and with it the sites that go in on the strength of it (the three job
// sites work together; the half rows' stub calls the half function as the game does).
inline bool PausedWrite(uint32_t patched, uint32_t* stood_down) {
  const uint32_t changing = (patched ^ written) & found;
  *stood_down = 0;
  std::vector<HANDLE> threads;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  THREADENTRY32 entry = {.dwSize = sizeof(THREADENTRY32)};
  for (BOOL more = Thread32First(snapshot, &entry); more != FALSE; more = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) continue;
    if (HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID); thread != nullptr) {
      threads.push_back(thread);
    }
  }
  CloseHandle(snapshot);
  size_t suspended = 0;
  bool clear = true;
  while (clear && suspended < threads.size()) {
    if (SuspendThread(threads[suspended]) == static_cast<DWORD>(-1)) {
      // Ended meanwhile: the last one takes its place (pop_back never allocates).
      CloseHandle(threads[suspended]);
      threads[suspended] = threads.back();
      threads.pop_back();
      continue;
    }
    // GetThreadContext also waits until the suspension has taken effect.
    CONTEXT context = {.ContextFlags = CONTEXT_CONTROL};
    clear = (GetThreadContext(threads[suspended], &context) != FALSE);
    ++suspended;
    for (size_t index = 0; index < sites.size(); ++index) {
      const auto start = reinterpret_cast<uintptr_t>(sites[index].at);
      if ((changing >> index & 1) != 0 && context.Eip > start && context.Eip < start + SITE_SIZE) clear = false;
    }
  }
  if (clear) {
    for (size_t index = 0; index < sites.size(); ++index) {
      const Site& site = sites[index];
      if ((changing >> index & 1) != 0 && std::memcmp(site.at, ((written >> index & 1) != 0 ? site.jump : site.game).data(), SITE_SIZE) != 0) {
        *stood_down |= 1u << index;
      }
    }
    const uint32_t going_in = changing & patched;
    if ((*stood_down & JOB_SITES) != 0) *stood_down |= JOB_SITES & going_in;
    if ((*stood_down & HALF_FUNCTION_BIT) != 0) *stood_down |= HALF_ROWS_BIT & going_in;
    for (size_t index = 0; index < sites.size(); ++index) {
      const Site& site = sites[index];
      if (((changing & ~*stood_down) >> index & 1) == 0) continue;
      DWORD protection;
      if (VirtualProtect(site.at, SITE_SIZE, PAGE_EXECUTE_READWRITE, &protection) == FALSE) {
        *stood_down |= 1u << index;
        continue;
      }
      const auto& bytes = ((patched >> index & 1) != 0 ? site.jump : site.game);
      std::memcpy(site.at, bytes.data(), SITE_SIZE);
      VirtualProtect(site.at, SITE_SIZE, protection, &protection);
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }
  for (size_t index = 0; index < threads.size(); ++index) {
    if (index < suspended) ResumeThread(threads[index]);
    CloseHandle(threads[index]);
  }
  return clear;
}

// Once: the routines found and checked, the stubs built in the page, each site's jump made (nothing written to the
// game yet). One log line: which are faster, which left alone.
inline void Prepare(HMODULE module, const Build& build = ATTILA_BUILD) {
  prepared = true;
  found = 0;
  auto* base = reinterpret_cast<uint8_t*>(module);
  const auto* headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
  const uint8_t* image_end = base + headers->OptionalHeader.SizeOfImage;
  const auto sections = tw::engine_tweaks::ModuleSections(module);
  // Inside the code sections (an address read from the game's code may point anywhere in another build).
  const auto in_code = [&](const uint8_t* at, size_t size) {
    for (const std::string_view memory : sections.code) {
      const auto* begin = reinterpret_cast<const uint8_t*>(memory.data());
      if (at >= begin && size <= memory.size() && at <= begin + memory.size() - size) return true;
    }
    return false;
  };
  const auto holds = [&](const uint8_t* at, std::string_view pattern) {
    const size_t size = (pattern.size() + 1) / 3;
    return in_code(at, size)
           && tw::engine_tweaks::FindPattern({std::string_view(reinterpret_cast<const char*>(at), size)}, pattern) == at;
  };
  // At the Steam build's place, else anywhere in the code.
  const auto find = [&](std::string_view pattern, uint32_t rva) {
    return (holds(base + rva, pattern) ? base + rva : tw::engine_tweaks::FindPattern(sections.code, pattern));
  };
  const auto read32 = [](const uint8_t* at) {
    uint32_t value;
    std::memcpy(&value, at, sizeof(value));
    return value;
  };
  const auto target = [&](const uint8_t* instruction) { return instruction + 5 + static_cast<int32_t>(read32(instruction + 1)); };
  const auto write32 = [](uint8_t* at, uint32_t value) { std::memcpy(at, &value, sizeof(value)); };
  const auto relative = [](const uint8_t* from_end, const uint8_t* to) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(to) - reinterpret_cast<uintptr_t>(from_end));
  };

  // A stub's sentinels: STUB_FIELD_SENTINEL + i the fields[i] (absolute), STUB_CONSTANT_SENTINEL + n the constants' + n, STUB_CALL_SENTINEL + i
  // the callees[i] (relative to the call's end).
  const auto resolve = [&](uint8_t* stub, size_t size, std::span<const uint8_t* const> fields, const uint8_t* constants,
                           std::span<const uint8_t* const> callees) {
    for (size_t at = 0; at + sizeof(uint32_t) <= size; ++at) {
      const uint32_t value = read32(stub + at);
      if (value - STUB_FIELD_SENTINEL < fields.size()) {
        write32(stub + at, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fields[value - STUB_FIELD_SENTINEL])));
      } else if (constants != nullptr && value - STUB_CONSTANT_SENTINEL < 0x100) {
        write32(stub + at, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(constants)) + (value - STUB_CONSTANT_SENTINEL));
      } else if (value - STUB_CALL_SENTINEL < callees.size()) {
        write32(stub + at, relative(stub + at + sizeof(uint32_t), callees[value - STUB_CALL_SENTINEL]));
      }
    }
  };

  page = static_cast<uint8_t*>(VirtualAlloc(nullptr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  std::array<uint8_t*, std::tuple_size_v<decltype(sites)>> stubs = {};
  if (page != nullptr) {
    if (uint8_t* match = find(build.truncation, build.truncation_rva); match != nullptr) {
      uint8_t* stub = page + TRUNCATION_PAGE;
      std::memcpy(stub, build.truncation_stub.data(), build.truncation_stub.size());
      uint8_t* stub_end = stub + build.truncation_stub.size();
      write32(stub_end - 4, relative(stub_end, match + TRUNCATION_RESUME));
      sites[0].at = match + TRUNCATION_SITE;
      stubs[0] = stub;
    }
    if (uint8_t* match = find(HALF, build.half_rva); match != nullptr) {
      uint8_t* stub = page + HALF_PAGE;
      std::memcpy(stub, HALF_STUB.data(), HALF_STUB.size());
      std::memcpy(stub + HALF_STUB.size(), match, HALF_SIZE);
      sites[1].at = match + HALF_SITE;
      stubs[1] = stub;
    }
    // fmod only with every piece of the CRT the stub stands in for as expected.
    if (uint8_t* match = find(FMOD, build.fmod_rva); match != nullptr) {
      const uint8_t* dispatch = target(match + 5);
      const uint8_t* classify = (holds(dispatch, FMOD_DISPATCH) ? target(dispatch + FMOD_DISPATCH_CLASSIFY) : nullptr);
      const uint8_t* finish = (classify != nullptr ? target(dispatch + FMOD_DISPATCH_FINISH) : nullptr);
      const uint8_t* matherr_check = (classify != nullptr ? target(dispatch + FMOD_DISPATCH_MATHERR) : nullptr);
      const uint32_t table = read32(match + 1);
      const auto* table_bytes = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(table));
      bool expected = classify != nullptr && holds(classify, FMOD_CLASSIFY) && holds(finish, FMOD_FINISH)
                      && holds(matherr_check, build.fmod_matherr);
      if (expected) {
        // The table (in the module's data): its control word kind (+0xe) and its entry for two normal numbers (+0x10);
        // the fxam classes of a normal number of either sign: 0 (that entry).
        const auto* classes = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(read32(classify + FMOD_CLASSIFY_CLASSES)));
        const auto in_module = [&](const uint8_t* at, size_t size) { return at >= base && at <= image_end && size <= static_cast<size_t>(image_end - at); };
        expected = in_module(table_bytes, 0x14) && in_module(classes, 16) && table_bytes[0xe] != 5
                   && read32(table_bytes + 0x10) == reinterpret_cast<uintptr_t>(match + FMOD_REMAINDER)
                   && classes[8] == 0 && classes[12] == 0;
      }
      if (expected) {
        uint8_t* stub = page + FMOD_PAGE;
        std::memcpy(stub, FMOD_STUB.data(), FMOD_STUB.size());
        write32(stub + FMOD_STUB_FLAG, read32(finish + FMOD_FINISH_FLAG));
        write32(stub + FMOD_STUB_MATHERR, relative(stub + FMOD_STUB_MATHERR + 4, matherr_check));
        for (const size_t at : FMOD_STUB_TABLE) write32(stub + at, table);
        write32(stub + FMOD_STUB_DISPATCH, relative(stub + FMOD_STUB_DISPATCH + 4, dispatch));
        sites[2].at = match;
        stubs[2] = stub;
      }
    }
    if (uint8_t* match = (build.list_push_rva != 0 ? find(LIST_PUSH, build.list_push_rva) : nullptr); match != nullptr) {
      uint8_t* stub = page + LIST_PUSH_PAGE;
      std::memcpy(stub, LIST_PUSH_STUB.data(), LIST_PUSH_STUB.size());
      uint8_t* stub_end = stub + LIST_PUSH_STUB.size();
      write32(stub_end - 4, relative(stub_end, match + LIST_PUSH_RESUME));
      sites[3].at = match + LIST_PUSH_SITE;
      stubs[3] = stub;
    }
    // Found by the whole routine's bytes: the stub stands in for all of it and returns to the caller itself.
    if (uint8_t* match = (build.ready_check_rva != 0 ? find(READY_CHECK, build.ready_check_rva) : nullptr); match != nullptr) {
      std::memcpy(page + READY_CHECK_PAGE, READY_CHECK_STUB.data(), READY_CHECK_STUB.size());
      sites[4].at = match;
      stubs[4] = page + READY_CHECK_PAGE;
    }
    // Faster Job Wake-Ups: all three sites, with the game's Wake and Stop as the stub expects, or none.
    if (build.job_wake_all_rva != 0) {
      uint8_t* wake_all = find(JOB_WAKE_ALL, build.job_wake_all_rva);
      uint8_t* worker_sleep = find(JOB_SLEEP, build.job_sleep_rva);
      uint8_t* destructor = find(JOB_WORKER_DESTRUCTOR, build.job_worker_destructor_rva);
      const uint8_t* wake = (wake_all != nullptr ? target(wake_all + JOB_WAKE_ALL_WAKE_CALL) : nullptr);
      const uint8_t* stop = (destructor != nullptr ? target(destructor + JOB_WORKER_DESTRUCTOR_STOP_CALL) : nullptr);
      if (worker_sleep != nullptr && wake != nullptr && stop != nullptr && holds(wake, JOB_WAKE) && holds(stop, JOB_STOP)
          && (job_table = static_cast<uint8_t*>(VirtualAlloc(nullptr, JOB_TABLE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))) != nullptr) {
        uint8_t* stub = page + JOB_PAGE;
        std::memcpy(stub, JOB_STUB.data(), JOB_STUB.size());
        const std::array<const uint8_t*, 4> fields = {job_table, job_table + JOB_TABLE_LINE, job_table + 2 * JOB_TABLE_LINE, job_table + 3 * JOB_TABLE_LINE};
        const std::array<const uint8_t*, 4> callees = {wake, target(worker_sleep + JOB_SLEEP_ACQUIRE_CALL), wake_all + SITE_SIZE, stop};
        resolve(stub, JOB_STUB.size(), fields, nullptr, callees);
        sites[5].at = wake_all;
        sites[6].at = worker_sleep;
        sites[7].at = destructor + JOB_WORKER_DESTRUCTOR_STOP_CALL;
        stubs[5] = stub + JOB_WAKE_ALL_STUB;
        stubs[6] = stub + JOB_SLEEP_STUB;
        stubs[7] = stub + JOB_STOP_STUB;
      }
    }
    // The sprite row builder's 20 conversions, only with the game's float to half the target of every one of its calls.
    if (uint8_t* match = (build.half_rows_rva != 0 ? find(HALF_ROWS, build.half_rows_rva) : nullptr); match != nullptr && sites[1].at != nullptr) {
      if (holds(match - HALF_ROWS_HEAD_AT, HALF_ROWS_HEAD) && holds(match + HALF_ROWS_END, HALF_ROWS_TAIL)
          && std::ranges::all_of(HALF_ROWS_CALLS, [&](uint16_t call) { return target(match + call) == sites[1].at; })) {
        uint8_t* stub = page + HALF_ROWS_PAGE;
        std::memcpy(stub, HALF_ROWS_STUB.data(), HALF_ROWS_STUB.size());
        std::memcpy(stub + HALF_ROWS_CONSTANTS_AT, HALF_ROWS_CONSTANTS.data(), sizeof(HALF_ROWS_CONSTANTS));
        const std::array<const uint8_t*, 3> callees = {sites[1].at, match + SITE_SIZE, match + HALF_ROWS_END};
        resolve(stub, HALF_ROWS_STUB.size(), {}, stub + HALF_ROWS_CONSTANTS_AT, callees);
        sites[8].at = match;
        stubs[8] = stub;
      }
    }
    // The effect tree's child sum, found by the whole function's bytes.
    if (uint8_t* match = (build.child_sum_rva != 0 ? find(CHILD_SUM, build.child_sum_rva) : nullptr); match != nullptr) {
      uint8_t* stub = page + CHILD_SUM_PAGE;
      std::memcpy(stub, CHILD_SUM_STUB.data(), CHILD_SUM_STUB.size());
      uint8_t* stub_end = stub + CHILD_SUM_STUB.size();
      write32(stub_end - 4, relative(stub_end, match + CHILD_SUM_RESUME));
      sites[9].at = match + CHILD_SUM_SITE;
      stubs[9] = stub;
    }
    // The particle writer's two reloads (insertps is SSE4.1), found by the function's tail.
    if (uint8_t* match = (build.float4_reload_rva != 0 && sse4_1 ? find(FLOAT4_RELOAD, build.float4_reload_rva) : nullptr); match != nullptr) {
      for (size_t reload = 0; reload < FLOAT4_RELOAD_AT.size(); ++reload) {
        uint8_t* stub = page + FLOAT4_RELOAD_PAGE + reload * FLOAT4_RELOAD_STRIDE;
        std::memcpy(stub, FLOAT4_RELOAD_STUB.data(), FLOAT4_RELOAD_STUB.size());
        uint8_t* stub_end = stub + FLOAT4_RELOAD_STUB.size();
        uint8_t* site = match + FLOAT4_RELOAD_AT[reload];
        write32(stub_end - 4, relative(stub_end, site + SITE_SIZE));
        sites[10 + reload].at = site;
        stubs[10 + reload] = stub;
      }
    }
    // The particle update loop's prefetch, found by the loop's bytes and those of its way out.
    if (uint8_t* match = (build.particle_loop_rva != 0 ? find(PARTICLE_LOOP, build.particle_loop_rva) : nullptr); match != nullptr) {
      uint8_t* stub = page + PARTICLE_PREFETCH_PAGE;
      std::memcpy(stub, PARTICLE_PREFETCH_STUB.data(), PARTICLE_PREFETCH_STUB.size());
      uint8_t* stub_end = stub + PARTICLE_PREFETCH_STUB.size();
      write32(stub_end - 9, relative(stub_end - 5, match));
      write32(stub_end - 4, relative(stub_end, match + PARTICLE_LOOP_EXIT));
      sites[12].at = match + PARTICLE_LOOP_SITE;
      stubs[12] = stub;
    }
    // The emitter update loop's prefetch, found by the whole function's bytes; the stub goes on at the `je` after the site.
    if (uint8_t* match = (build.emitter_loop_rva != 0 ? find(EMITTER_LOOP, build.emitter_loop_rva) : nullptr); match != nullptr) {
      uint8_t* stub = page + EMITTER_PREFETCH_PAGE;
      std::memcpy(stub, EMITTER_PREFETCH_STUB.data(), EMITTER_PREFETCH_STUB.size());
      uint8_t* stub_end = stub + EMITTER_PREFETCH_STUB.size();
      write32(stub_end - 4, relative(stub_end, match + EMITTER_LOOP_SITE + SITE_SIZE));
      sites[13].at = match + EMITTER_LOOP_SITE;
      stubs[13] = stub;
    }
    DWORD protection;
    VirtualProtect(page, PAGE_SIZE, PAGE_EXECUTE_READ, &protection);
    FlushInstructionCache(GetCurrentProcess(), page, PAGE_SIZE);
  }
  std::string faster;
  std::string left;
  std::string x87_fixed;
  // Attila's truncation block rewritten by the Attila CPU Performance Fix is named as such, not as an unknown mod.
  const bool truncation_x87_fixed = (sites[0].at == nullptr && build.truncation_rva == TRUNCATION_RVA
                                     && holds(base + TRUNCATION_RVA, {X87_FIX_TRUNCATION.data(), X87_FIX_TRUNCATION.size()}));
  // A site the build doesn't have (rva 0) isn't named.
  const uint32_t reload_rva = (sse4_1 ? build.float4_reload_rva : 0);
  const std::array<uint32_t, 14> rvas = {build.truncation_rva, build.half_rva, build.fmod_rva, build.list_push_rva, build.ready_check_rva,
                                         0, 0, 0, build.half_rows_rva, 0, reload_rva, reload_rva, 0, 0};
  static_assert(std::tuple_size_v<decltype(rvas)> == std::tuple_size_v<decltype(sites)>);
  for (size_t index = 0; index < sites.size(); ++index) {
    Site& site = sites[index];
    // The job sites aren't listed here: their row says when it turns them on.
    if (rvas[index] != 0) {
      std::string* names = &left;
      if (site.at != nullptr) {
        names = &faster;
      } else if (index == 0 && truncation_x87_fixed) {
        names = &x87_fixed;
      }
      *names += std::format("{}{}", names->empty() ? "" : ", ", site.name);
    }
    if (site.at == nullptr) continue;
    found |= 1u << index;
    std::memcpy(site.game.data(), site.at, SITE_SIZE);
    site.jump[0] = site.opcode;
    write32(site.jump.data() + 1, relative(site.at + SITE_SIZE, stubs[index]));
  }
  renodx::utils::log::i("[Total War] Faster Game CPU Code: ", (faster.empty() ? "nothing" : faster), " faster",
                        (left.empty() ? "" : "; left alone (not the game's code as expected: another mod or build): "), left,
                        (x87_fixed.empty() ? "" : "; left alone (the Attila CPU Performance Fix's x87 fix rewrote it): "), x87_fixed);
}

}  // namespace internal

// Every present: `enabled` (Faster Game CPU Code), `lists` (Faster Render Lists), `jobs` (Faster Job Wake-Ups) and `prefetch`
// (Faster Effect Prefetch) once empire.retail.dll is loaded, the first time any is on prepared (`game`'s build); then the
// jumps go in or the game's bytes back, while no thread stands inside them.
inline void Update(bool enabled, Game game = Game::ATTILA, bool lists = false, bool jobs = false, bool prefetch = false) {
  const uint32_t wanted = (enabled ? internal::CPU_CODE_SITES : 0u) | (lists ? internal::LIST_SITES : 0u) | (jobs ? internal::JOB_SITES : 0u)
                          | (prefetch ? internal::PREFETCH_SITES : 0u);
  if (wanted != 0 && !internal::prepared) {
    HMODULE module = GetModuleHandleW(L"empire.retail.dll");
    if (module == nullptr) return;
    internal::Prepare(module, internal::BUILDS[static_cast<size_t>(game)]);
  }
  if (wanted != internal::asked) {
    internal::asked = wanted;
    internal::failed_writes = 0;
  }
  if (jobs && internal::prepared && (internal::found & internal::JOB_SITES) == 0 && !internal::job_missing_logged) {
    internal::job_missing_logged = true;
    renodx::utils::log::w("[Total War] Faster Job Wake-Ups: left alone (not the game's code as expected: another mod or build)");
  }
  if (prefetch && internal::prepared && (internal::found & internal::PREFETCH_SITES) != internal::PREFETCH_SITES && !internal::prefetch_missing_logged) {
    internal::prefetch_missing_logged = true;
    std::string missing;
    for (size_t index = 0; index < internal::sites.size(); ++index) {
      if (((internal::PREFETCH_SITES & ~internal::found) >> index & 1) != 0) missing += std::format("{}{}", missing.empty() ? "" : ", ", internal::sites[index].name);
    }
    renodx::utils::log::w("[Total War] Faster Effect Prefetch: left alone (not the game's code as expected: another mod or build): ", missing);
  }
  const uint32_t target = wanted & internal::found;
  if (target == internal::written || internal::failed_writes >= internal::MAX_FAILED_WRITES) return;
  uint32_t stood_down = 0;
  if (internal::PausedWrite(target, &stood_down)) {
    const uint32_t changing = target ^ internal::written;
    const uint32_t now = (internal::written & ~changing) | (target & changing & ~stood_down);
    const uint32_t changed = now ^ internal::written;
    internal::written = now;
    internal::found &= ~stood_down;
    internal::failed_writes = 0;
    for (const auto& [row_sites, name] : internal::ROWS) {
      if ((changed & row_sites) != 0) renodx::utils::log::i("[Total War] ", name, ": ", ((now & row_sites) != 0 ? "on" : "off, the game's own code back"));
    }
    for (size_t index = 0; index < internal::sites.size(); ++index) {
      if ((stood_down >> index & 1) != 0) {
        renodx::utils::log::w("[Total War] ", internal::sites[index].name, ": left alone (its bytes changed since the start: another mod)");
      }
    }
  } else if (++internal::failed_writes == internal::MAX_FAILED_WRITES) {
    for (const auto& [row_sites, name] : internal::ROWS) {
      if (((target ^ internal::written) & row_sites) != 0) {
        renodx::utils::log::w("[Total War] ", name, ": not written, a game thread kept running the code");
      }
    }
  }
}

// Addon detach: the game's bytes back. The page stays (a thread may still be in it), so the jumps are safe to leave in
// the game's code if no attempt finds the threads clear: the stubs need nothing of this module.
inline void Uninstall() {
  for (int attempt = 0; internal::written != 0 && attempt < 100; ++attempt) {
    if (uint32_t stood_down; internal::PausedWrite(0, &stood_down)) {
      internal::written = 0;  // a site another mod changed is that mod's now
    } else {
      Sleep(1);
    }
  }
  if (internal::written != 0) renodx::utils::log::w("[Total War] Faster Game CPU Code: left in at detach (a game thread kept running it)");
}

#else

inline void Update(bool, Game = Game::ATTILA, bool = false, bool = false, bool = false) {}
inline void Uninstall() {}

#endif

}  // namespace twattila::engine_code
