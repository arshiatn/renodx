/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../utils/log.hpp"
#include "../twrome2/engine_tweaks.hpp"
#include "./animation_rows.hpp"
#include "./code_ranges.hpp"
#include "./effect_update_sites.hpp"
#include "./instance_fill.hpp"
#include "./pending_sweep.hpp"
#if defined(TWATTILA_DRAW_RECORDING)
#include "./draw_recording.hpp"
#endif

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
// CPU Code. Faster Effect Prefetch (its own row, Experimental): the effect tree's child sum (8. below, the tenth site)
// and the particle update loop (10. below, the thirteenth). Faster Emitter Prefetch (its own row, Experimental, so that
// a bench can decide it apart from the other two): the emitter update loop (11. below, the fourteenth). Faster Particle
// Prefetch (its own row, Experimental): the particle writer's table entry (14. below, the twenty-first). The particle
// writer's two float4 reloads (9. below, the eleventh and twelfth sites) belong to Faster Game CPU Code, as do the
// soldier draw's three matrix row reloads (12. below, the fifteenth to seventeenth sites) and the matrix product's
// three (13. below, the eighteenth to twentieth), and the render lists' two item push loops (15. below, the twenty-second and
// twenty-third sites). Faster Sweep Prefetch (its own row, Experimental): the pending list sweep's chain prefetch (16. below,
// the twenty-fourth). A private probe build (compiled only with TWATTILA_ENGINE_PROBE: engine_probe.hpp, 17. below; no
// row, no release) counts histograms of the sites' loops in front of the production stubs, which it leaves as they are.
// Model Detail Distance (its own row, the slider's value in Update's last argument): the game's LOD pick (18. below, the twenty-fifth site), scaled live,
// the game's own bytes back at 1x.
// Animation Rows on Workers and the Pending Sweep trace (Attila; 19. and 20. below, the twenty-sixth and twenty-seventh sites): two sites whose stubs call into
// this module (animation_rows.hpp, pending_sweep.hpp) through words of a read-write page, and come out only when no thread is in them (CutRange).
// Exit Job Drain (Attila; 21. below, the thirtieth and thirty-first sites; a bug fix, always on, no row): the game's end waits for its job workers.
namespace twattila::engine_code {

// Whose build of the routines to look for.
enum class Game : uint8_t { ATTILA, THRONES, ROME2 };

// What the worker sites are asked for (Update's last argument): the animation update's call of the animation texture
// fill goes through animation_rows::Dispatch while `animation` is not OFF; the pending list sweep's entry goes through
// pending_sweep::Arm while `pending_sweep_trace` is set (a bench's RENODX_TW_WORKER_TRACE, never a player's); the effect
// update's 27 sites (effect_update_sites.hpp: their own table, written in the same window) while `effect_update` is set; the
// two sites of Cached Particle Count while `particle_count` is set (`particle_count_check`: the game's sum is also computed
// on every call and compared with the cache, a bench's RENODX_TW_COUNT_CHECK, never a player's); the 4 fill sites of Parallel
// Instance Fill (instance_fill.hpp: their own table too) while `instance_fill` is set, and (private build) its 3 Unmap stubs while
// `fill_fold` is set too; the Exit Job Drain's sites (21. below, Attila; a bug fix: the add-on sets it unless a bench's RENODX_TW_EXIT_DRAIN=0, no row) while `exit_job_drain`
// is set.
struct WorkerRows {
  animation_rows::Stage animation = animation_rows::Stage::OFF;
  bool pending_sweep_trace = false;
  bool effect_update = false;
  bool particle_count = false;
  bool particle_count_check = false;
  bool instance_fill = false;
  bool fill_fold = false;
  bool exit_job_drain = false;
};

// What the cache of Cached Particle Count counted (the check's mismatches are what the row's end-of-soak line reports).
struct ParticleCountStats {
  uint32_t hits = 0;
  uint32_t misses = 0;
  uint32_t mismatches = 0;
};

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
// The same bytes as the Attila CPU Performance Fix's x87 fix leaves them (its x87fix.c, on the block at +14): the form of
// 7.0 (both fldcw a 4-byte nop, fistp fisttp), the split form of the later builds (the control word reload split: the
// fwait through the fistp are 30 bytes of integer code, with fisttp and no control word loads) and the form of its x87c
// (the whole 36-byte block: push eax, cvttss2si, the store, pop eax, a short jump over 22 int3). Left to that mod as any
// other mod's code; only named in the log.
constexpr auto X87_FIX_TRUNCATIONS = [] {
  std::array<std::array<char, TRUNCATION.size()>, 3> patterns = {};
  for (std::array<char, TRUNCATION.size()>& pattern : patterns) {
    std::ranges::copy(TRUNCATION, pattern.begin());
  }
  const auto put = [&](size_t form, size_t offset, std::string_view bytes) {
    std::ranges::copy(bytes, patterns[form].begin() + 3 * offset);
  };
  put(0, 37, "0F 1F 40 00");  // fldcw word ptr [esp + 0x1c]
  put(0, 42, "4C");           // fistp dword ptr [esp + 0x2c] -> fisttp
  put(0, 45, "0F 1F 40 00");  // fldcw word ptr [esp + 0x18]
  // movzx eax, word ptr [esp + 0x1a]; fstcw [esp + 0x18]; shl eax, 0x10; mov ax, [esp + 0x18]; or eax, 0xc00;
  // mov [esp + 0x1c], eax; fisttp dword ptr [esp + 0x2c]
  put(1, 19, "0F B7 44 24 1A D9 7C 24 18 C1 E0 10 66 8B 44 24 18 0D 00 0C 00 00 89 44 24 1C DB 4C 24 2C");
  put(2, 14, "50 F3 0F 2C 44 24 20 89 44 24 2C 58 EB 16");
  for (size_t offset = 28; offset < 50; ++offset) {
    put(2, offset, "CC");
  }
  return patterns;
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
static_assert(HALF_ROWS_STUB.size() <= HALF_ROWS_CONSTANTS_AT);  // the constants after the code
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

// 8. The effect tree's child sum (+0x1890b90; E3P of the CPU plan; Faster Effect Prefetch): `sum +=
// child->vtable[0x1c]()` over the vector at [this+0x68] of [this+0x64] pointers, the bounds read again after every
// call. The children are scattered heap objects, so the loop waits for each one's first line. The stub at the loop's
// bottom (the `lea ecx, [ecx + edx*4]; cmp esi, ecx` that the `jne` tests; esi is the next item's slot, ecx the
// vector's begin, edx its size) prefetches the children K / 2 - 1 and K - 1 places after the next one (K =
// CHILD_SUM_AHEAD), each behind its own check: a slot is read only when its 4 bytes lie wholly inside [begin, end) as
// the vector is right now, with no wrap of the address space (the near slot [esi + 2K - 4, esi + 2K), the far slot [esi
// + 4K - 4, esi + 4K)). A call that replaced the vector may leave the old esi outside the new one (no load) or inside
// it (a load inside the new block); esi need not be 4 apart from the begin. Each check is exact by itself: esi >= begin
// and K >= 2 put the slot's first byte at or above the begin, the add's carry rules out the wrap, the compare puts its
// last byte below the end. A near slot that fails fails the far one too (esi + 2K past the end or past 2^32 puts esi +
// 4K there), so the stub skips the second check. `prefetcht0` itself never faults. Same results: the flags the `jne`
// sees are the game's `cmp esi, ecx`, ecx is the end as the game computes it, edx is not touched, and eax, which the
// game has added to the sum and next overwrites (the loop's top, `mov eax, edi` at the exit), is the one register that
// differs. No store, and loads only inside the vector's size * 4 bytes, which any vector keeps committed after a call
// returns (the one assumption): every register, the flags and the memory at the function's return are the game's.
constexpr std::string_view CHILD_SUM =
    "53 8B D9 56 57 33 FF 8B 73 68 8B 43 64 8D 04 86 3B F0 74 19 8B 0E 8B 01 FF 50 1C 8B 53 64 83 C6 04 8B 4B 68 03 F8 8D 0C 91 "
    "3B F1 75 E7 8B C7 5F 5E 5B C3";
constexpr size_t CHILD_SUM_SITE = 38;    // lea ecx, [ecx + edx*4]; cmp esi, ecx
constexpr size_t CHILD_SUM_RESUME = 43;  // jne to the loop's top
constexpr uint8_t CHILD_SUM_AHEAD = 4;
// The stub for K = AHEAD: the children in the slots [esi + 2K - 4, esi + 2K) and [esi + 4K - 4, esi + 4K), K / 2 - 1 and
// K - 1 places after the next one. K >= 2: the near slot starts at esi or above (2K - 4 >= 0); K even: K / 2 a whole slot;
// 4K <= 127: the add's immediate fits a signed byte. The test builds it for K = 4, 8 and 16 for its timing.
template <uint8_t AHEAD>
constexpr std::array<uint8_t, 48> ChildSumStub() {
  static_assert(AHEAD >= 2 && AHEAD % 2 == 0 && AHEAD * 4 <= 127);
  return {
      0x39, 0xCE,                                   // cmp esi, ecx                 esi against the begin
      0x8D, 0x0C, 0x91,                             // lea ecx, [ecx + edx*4]       the end (no flags)
      0x72, 0x22,                                   // jb skip                      esi below the begin: stale, no load
      0x89, 0xF0,                                   // mov eax, esi
      0x83, 0xC0, static_cast<uint8_t>(AHEAD * 2),  // add eax, 2K
      0x72, 0x1B,                                   // jc skip                      esi + 2K past 2^32 (esi + 4K too)
      0x39, 0xC8,                                   // cmp eax, ecx
      0x77, 0x17,                                   // ja skip                      the near slot is not wholly inside (the far one neither)
      0x8B, 0x40, 0xFC,                             // mov eax, dword ptr [eax - 4]  the child in the near slot
      0x0F, 0x18, 0x08,                             // prefetcht0 byte ptr [eax]
      0x89, 0xF0,                                   // mov eax, esi
      0x83, 0xC0, static_cast<uint8_t>(AHEAD * 4),  // add eax, 4K
      0x72, 0x0A,                                   // jc skip                      esi + 4K past 2^32
      0x39, 0xC8,                                   // cmp eax, ecx
      0x77, 0x06,                                   // ja skip                      the far slot is not wholly inside
      0x8B, 0x40, 0xFC,                             // mov eax, dword ptr [eax - 4]  the child in the far slot
      0x0F, 0x18, 0x08,                             // prefetcht0 byte ptr [eax]
      0x39, 0xCE,                                   // skip: cmp esi, ecx           the flags the game's jne tests
      0xE9, 0x00, 0x00, 0x00, 0x00,                 // jmp the loop's jne (patched in Prepare)
  };
}
constexpr std::array<uint8_t, 48> CHILD_SUM_STUB = ChildSumStub<CHILD_SUM_AHEAD>();

// 9. The particle writer's float4 reloads (+0x18960d0, thiscall(float, const float*), ret 8; the effect update calls it once for
// every particle). It builds two float4s (a colour and a vector) with four 4-byte `movss` stores each into the frame at
// [esp + 0x40 .. 0x4f] and reloads each with one 16-byte `movups xmm0, [esp + 0x40]` to store it into a vertex stream. A load that
// spans four stores can't be forwarded from the store buffer: it waits until they have reached the cache, and stores drain in
// program order, so it also waits for every older store (the streams' other rows). The stub loads each lane from the address and
// size of the store that wrote it: `movss xmm0, [esp + 0x40]`, then `insertps xmm0, [esp + 0x44 / 0x48 / 0x4c], 0x10 / 0x20 /
// 0x30` (SSE4.1: without it the sites are not looked for). Same results: xmm0 holds the same 128 bits (moves only: NaN payloads,
// subnormals and the sign of zero as they are; MXCSR untouched), no flag and no other register changes, no store, and the four
// loads read the same 16 stack bytes after the same stores. Found by the function's tail (+0x18967db, its call's displacement
// wild), at the Steam build's place first, then anywhere in the code: another mod's change in the tail, a site included, leaves
// both alone. No branch lands inside either site (the whole DLL's relative branches and absolute pointers, Attila's and Thrones').
constexpr std::string_view FLOAT4_RELOAD =
    "8D 44 24 40 8B CF 50 E8 ?? ?? ?? ?? 8B 47 10 8B 40 78 80 78 2C 00 74 07 F3 0F 10 4F 78 EB 03 0F 57 C9 8B 54 24 7C "
    "F3 0F 10 44 24 4C F3 0F 59 C1 8B 02 F3 0F 11 44 24 4C F3 0F 10 45 08 F3 0F 11 40 0C 8B 42 04 F3 0F 10 84 24 80 00 "
    "00 00 F3 0F 11 40 0C 8B 47 14 8B 40 08 66 0F 6E 40 18 8B 42 0C 0F 5B C0 F3 0F 11 00 8B 47 0C F3 0F 10 40 70 F3 0F "
    "59 47 70 8B 42 0C F3 0F 11 40 04 8B 42 08 0F 10 44 24 40 0F 11 00 8B 02 F3 0F 7E 47 18 8B 4F 20 66 0F D6 00 F3 0F "
    "7E 84 24 90 00 00 00 89 48 08 8B 4A 04 8B 84 24 98 00 00 00 66 0F D6 01 F3 0F 10 44 24 14 F3 0F 11 44 24 40 F3 0F "
    "10 44 24 10 F3 0F 11 44 24 44 F3 0F 10 44 24 18 89 41 08 8B 42 10 F3 0F 11 44 24 48 F3 0F 10 44 24 1C F3 0F 11 44 "
    "24 4C 0F 10 44 24 40 0F 11 00 8B 4F 0C 8B 42 14 0F 10 81 80 00 00 00 0F 11 00 5F 5E 8B E5 5D C2 08 00";
constexpr std::array<size_t, 2> FLOAT4_RELOAD_AT = {128, 230};  // movups xmm0, xmmword ptr [esp + 0x40]: the colour's, the vector's
// The stub for the 5-byte `movups xmm0, [esp + AT]`, lane by lane (13. uses it for other sites).
template <uint8_t AT>
constexpr std::array<uint8_t, 35> Float4ReloadStub() {
  static_assert(AT + 12 <= 0x7F);  // the four disp8 fit a signed byte
  return {
      0xF3, 0x0F, 0x10, 0x44, 0x24, AT,                                              // movss xmm0, dword ptr [esp + AT]
      0x66, 0x0F, 0x3A, 0x21, 0x44, 0x24, static_cast<uint8_t>(AT + 4), 0x10,        // insertps xmm0, dword ptr [esp + AT + 4], 0x10
      0x66, 0x0F, 0x3A, 0x21, 0x44, 0x24, static_cast<uint8_t>(AT + 8), 0x20,        // insertps xmm0, dword ptr [esp + AT + 8], 0x20
      0x66, 0x0F, 0x3A, 0x21, 0x44, 0x24, static_cast<uint8_t>(AT + 12), 0x30,       // insertps xmm0, dword ptr [esp + AT + 12], 0x30
      0xE9, 0x00, 0x00, 0x00, 0x00,                                                  // jmp back (the instruction after the site)
  };
}
constexpr std::array<uint8_t, 35> FLOAT4_RELOAD_STUB = Float4ReloadStub<0x40>();
// Whether the CPU has insertps; the test turns it off to see the sites left out.
static bool sse4_1 = (IsProcessorFeaturePresent(PF_SSE4_1_INSTRUCTIONS_AVAILABLE) != FALSE);

// 10. The particle update loop's prefetch (the emitter update +0x1893470, which four places call: the effect update's
// serial loop (11.) and three job slice loops, so the stub runs on the workers too; its particle loop +0x1893820 ..
// +0x1893888; Faster Effect Prefetch). The loop takes the particle [[edi + 0x68] + ebx*4] while ebx < [edi + 0x64] (the
// vector's array and size, read again after every call), stores to it (+0x18, +0x20, +0x30), calls its update through
// the vtable (+0xc) and ends with `cmp ebx, dword ptr [edi + 0x64]; jb the top`. The particles are scattered heap
// objects of four lines: the first load of each, and the array's, waits for the memory. The stub at that bottom, where
// ebx is already the next particle's index, loads the slot K = PARTICLE_PREFETCH_AHEAD particles after the next one and
// prefetches the particle's four lines, only when the slot is inside the size as it is right now. Its loads:
// [edi + 0x64] is the site's own `cmp` operand; [edi + 0x68] lies beside it in the same object (the loop top loads it
// next when the `jb` is taken); the slot lies inside the array's size * 4 bytes (the one assumption, as in 8.); the lea
// wraps like the load's address does, so an index past 2^32 loads from inside the vector too. The prefetch never faults.
// The stub spends esi, which is dead there: the loop's top loads it, and the way out only pops it and calls a function
// that never reads esi as an input. That last step was checked in the bytes of Attila's and Thrones' builds only: the
// call's rel32 is wild in the pattern (in both builds its target is that function). The flags the `jb` tests are the
// game's `cmp`; no store, no other register: every register but esi, the flags and all memory at the loop's end are the
// game's. Found by the loop's 104 bytes and the 36 after it (the way out the esi argument stands on, its call wild), at
// the Steam build's place first, then anywhere in the code; no branch lands inside the site (the whole DLL's relative
// branches and absolute pointers, Attila's and Thrones'), no site or hook of the Attila CPU Performance Fix 7.0 lies in
// the loop.
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

// 11. The emitter update loop's prefetch (the effect update +0x1893330, one caller, the loop of its serial arm +0x18933c0
// .. +0x18933e5; Faster Emitter Prefetch). The loop calls the emitter update (+0x1893470, thiscall, three stack
// arguments) with the emitter [[container + 8] + i*8] of the vector at [this + 0x14] (+4 its size, +8 its elements, 8
// bytes each: the pointer of a shared_ptr first) for i from 0 on while `inc esi; cmp esi, [esp + 0x34]; je the way out`
// says so: the count was read once before the loop ([esp + 0x34] = [this + 0x18]), and the container's address sits in
// the slot [esp + 0x20] the loop reloads ebx from. The emitters and their definitions are scattered heap objects: each
// update starts on lines nobody touched since the last frame, the first load of the definition [[emitter + 0x58] + 0x2c]
// (in 0x1840520, which the update calls with the emitter) among them.
// The stub at the loop's bottom (the 5 bytes `inc esi; cmp esi, [esp + 0x34]`) works in two stages on the slots K =
// EMITTER_PREFETCH_AHEAD and K + 1 after the next emitter's. It prefetches the lines at +0, +0x40 and +0x70 of the emitter
// in slot K + 1 (the three lines of its first 0x78 bytes, where the update reads and writes from +0x14 through +0x77; for
// an 8-aligned object the line at +0x70 holds all of +0x70 .. +0x77); and for the emitter in slot K, whose lines the
// previous round prefetched, it loads the definition pointer [emitter + 0x58] and prefetches the line of [definition +
// 0x2c] (the update reads +0x28 .. +0x34: one line unless the definition straddles two). Both stages run behind one check
// of the slot K + 1: below the count the loop runs to, and below the size the vector has right now ([container + 4]).
// esi + K + 1 cannot wrap: esi never passes the count the loop started with, a vector's size under 2^29 (8 bytes a
// slot); only at esi = 0xFFFFFFFF - K, which the loop never has, would K + 1 wrap to 0 and pass both bounds, and the slot
// K read lie one slot below the array. A null slot K is skipped; a null slot K + 1 only sends prefetches to nowhere (a
// prefetch never faults).
// Same results: the stub writes ecx and edx, which are dead at the site. ecx: both ways on write it before they read it
// (the loop's top `mov ecx, [ebx + 8]`, the way out `mov ecx, [edi + 0x18]`). edx: the update, which the loop's top calls,
// never reads it before writing it; the way out writes it (`mov edx, [edi + 0x1c]`) or, with the vector empty, returns
// without, and the one caller writes it before it reads it (checked in the bytes of Attila's and Thrones' builds). The
// flags the `je` tests are the game's `cmp esi, [esp + 0x34]`, run again at the end; no store. The loads: [esp + 0x34] and
// [esp + 0x20] (the next round reads both), [container + 4] and [container + 8] (the loop reads the second itself), the two
// slots, and one real load of an emitter, [emitter + 0x58]. The slots lie below the size the vector has right now (a call
// that changed the vector changed that size too), so they hold the vector's own shared_ptrs: a non-null slot points to an
// emitter the vector keeps alive, whose +0x58 lies inside the object the update reads and writes (the way out after the
// loop reads +0x6e of every emitter of the vector). The premise: no other thread changes the vector while the loop runs
// (the game's own load of the slot would race the same way). If it failed, the worst case is that one load reading freed
// small-heap memory: its value only feeds a prefetch, so the results stay the game's, and it faults only if the page was
// decommitted meanwhile. Reading the container from [esp + 0x20], not from ebx, keeps the stub independent of the registers
// the call keeps. Found by the whole function's bytes (its five calls and two `mov ecx, imm32` wild: the same bytes in
// Thrones' copy), at the Steam build's place first, then anywhere in the code; no branch lands inside the site (the whole
// DLL's relative branches and absolute pointers, Attila's and Thrones'), no site or hook of the Attila CPU Performance Fix
// 7.0 lies in the function.
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
static_assert(EMITTER_PREFETCH_AHEAD >= 1 && (EMITTER_PREFETCH_AHEAD + 1) * 8 <= 127);  // the slots' displacements are signed bytes
// The stub for the slots `ahead` and `ahead` + 1 after the next emitter's (the test builds the others for its timing).
constexpr std::array<uint8_t, 61> EmitterPrefetchStub(uint8_t ahead) {
  return {
      0x46,                                                   // inc esi                          the next emitter's index
      0x8D, 0x4E, static_cast<uint8_t>(ahead + 1),            // lea ecx, [esi + ahead + 1]       the slot `ahead` + 1 after it
      0x3B, 0x4C, 0x24, 0x34,                                 // cmp ecx, dword ptr [esp + 0x34]  below the count the loop runs to
      0x73, 0x2A,                                             // jae skip
      0x8B, 0x54, 0x24, 0x20,                                 // mov edx, dword ptr [esp + 0x20]  the container, as the game reloads it
      0x3B, 0x4A, 0x04,                                       // cmp ecx, dword ptr [edx + 4]     below the size the vector has now
      0x73, 0x21,                                             // jae skip
      0x8B, 0x52, 0x08,                                       // mov edx, dword ptr [edx + 8]     the elements as they are now
      0x8B, 0x4C, 0xF2, static_cast<uint8_t>(ahead * 8),      // mov ecx, dword ptr [edx + esi*8 + ahead*8]  the emitter `ahead` after the next
      0x85, 0xC9,                                             // test ecx, ecx
      0x74, 0x07,                                             // je lines
      0x8B, 0x49, 0x58,                                       // mov ecx, dword ptr [ecx + 0x58]  its definition: the stub's one real load
      0x0F, 0x18, 0x49, 0x2C,                                 // prefetcht0 byte ptr [ecx + 0x2c]
      0x8B, 0x4C, 0xF2, static_cast<uint8_t>(ahead * 8 + 8),  // lines: mov ecx, dword ptr [edx + esi*8 + ahead*8 + 8]  the emitter after it
      0x0F, 0x18, 0x09,                                       // prefetcht0 byte ptr [ecx]
      0x0F, 0x18, 0x49, 0x40,                                 // prefetcht0 byte ptr [ecx + 0x40]
      0x0F, 0x18, 0x49, 0x70,                                 // prefetcht0 byte ptr [ecx + 0x70]
      0x3B, 0x74, 0x24, 0x34,                                 // skip: cmp esi, dword ptr [esp + 0x34]  the flags the game's je tests
      0xE9, 0x00, 0x00, 0x00, 0x00,                           // jmp the game's je (patched in Prepare)
  };
}
constexpr std::array<uint8_t, 61> EMITTER_PREFETCH_STUB = EmitterPrefetchStub(EMITTER_PREFETCH_AHEAD);

// 12. More float4 reloads of four 4-byte stores, done lane by lane as in 9. (Faster Game CPU Code, SSE4.1). The soldiers' model draw
// (+0x17f61a0, 1. above) builds three rows of a matrix at [esp + 0x90 .. 0xbf] with twelve `movss` stores on either of its two ways (the
// way with a table entry falls through to the reloads, the other jumps to the first) and reloads each row with the 8-byte
// `movups xmm0, [esp + 0x90 / 0xa0 / 0xb0]`. On every way into each reload each lane was written by one 4-byte store. Each stub loads
// the lanes from those addresses (a 4-byte displacement) and jumps back after the game's 8-byte movups (a site's last three bytes stay
// and are never run). The same results as 9.: xmm0's 128 bits, no flag, no other register, no store. Found by the rows' stores and
// reloads (+0x17f689d .. +0x17f6970, the jump at the end included: four stack slots of other instructions and of the two stores after
// the reloads wild, which Thrones' copy keeps elsewhere; Rome II's copy stores in another order, its own pattern), at the Steam build's
// place first, then anywhere in the code; another mod's change in the pattern, a site included, leaves the three alone. The pattern
// stays clear of the truncation block the Attila CPU Performance Fix rewrites (+0x17f6507 .. +0x17f6550). No branch lands inside a
// site (the whole DLL's relative branches and absolute pointers, the three games'), no relocation lies inside the pattern.
constexpr std::string_view SOLDIER_ROWS =
    "F3 0F 11 AC 24 B0 00 00 00 F3 0F 10 84 24 C0 00 00 00 F3 0F 59 83 F0 00 00 00 F3 0F 11 BC 24 94 00 00 00 F3 0F 11 94 24 "
    "AC 00 00 00 F3 0F 58 C8 F3 0F 11 A4 24 BC 00 00 00 0F 10 84 24 ?? 00 00 00 F3 0F 11 84 24 90 00 00 00 F3 0F 10 44 24 ?? "
    "F3 0F 58 8C 24 ?? 00 00 00 F3 0F 11 84 24 B4 00 00 00 0F 10 84 24 C0 00 00 00 F3 0F 11 84 24 98 00 00 00 F3 0F 10 44 24 "
    "18 F3 0F 11 84 24 A8 00 00 00 F3 0F 10 44 24 ?? F3 0F 11 84 24 B8 00 00 00 F3 0F 11 8C 24 9C 00 00 00 0F 10 84 24 90 00 "
    "00 00 F3 0F 11 B4 24 A4 00 00 00 0F 11 44 24 ?? F3 0F 11 9C 24 A0 00 00 00 0F 10 84 24 A0 00 00 00 0F 11 44 24 ?? 0F 10 "
    "84 24 B0 00 00 00 E9 53 01 00 00";
constexpr std::array<size_t, 3> SOLDIER_ROWS_AT = {154, 185, 198};  // movups xmm0, xmmword ptr [esp + 0x90 / 0xa0 / 0xb0]
constexpr std::string_view ROME2_SOLDIER_ROWS =
    "F3 0F 11 9C 24 A0 00 00 00 F3 0F 59 86 E0 00 00 00 0F 28 D7 F3 0F 59 D1 0F 28 CE F3 0F 59 8E E4 00 00 00 F3 0F 58 D0 F3 "
    "0F 11 AC 24 B0 00 00 00 F3 0F 10 44 24 10 F3 0F 59 86 E8 00 00 00 F3 0F 11 B4 24 94 00 00 00 F3 0F 11 BC 24 A4 00 00 00 "
    "F3 0F 58 D0 F3 0F 11 A4 24 BC 00 00 00 F3 0F 10 86 E0 00 00 00 F3 0F 59 44 24 14 F3 0F 58 94 24 80 00 00 00 F3 0F 58 C8 "
    "F3 0F 10 44 24 20 F3 0F 59 86 E8 00 00 00 F3 0F 11 94 24 AC 00 00 00 F3 0F 58 C8 F3 0F 10 44 24 14 F3 0F 11 84 24 90 00 "
    "00 00 F3 0F 10 44 24 34 F3 0F 11 84 24 B4 00 00 00 F3 0F 10 44 24 20 F3 0F 58 4C 24 1C F3 0F 11 84 24 98 00 00 00 F3 0F "
    "10 44 24 10 F3 0F 11 84 24 A8 00 00 00 F3 0F 10 44 24 18 F3 0F 11 84 24 B8 00 00 00 F3 0F 11 8C 24 9C 00 00 00 0F 10 84 "
    "24 90 00 00 00 0F 11 44 24 44 0F 10 84 24 A0 00 00 00 0F 11 44 24 54 0F 10 84 24 B0 00 00 00 E9 69 01 00 00";
constexpr std::array<size_t, 3> ROME2_SOLDIER_ROWS_AT = {237, 250, 263};
constexpr size_t SOLDIER_ROW_LOAD_SIZE = 8;  // movups xmm0, xmmword ptr [esp + disp32]
// The stub for the 8-byte `movups xmm0, [esp + at]`, lane by lane.
constexpr std::array<uint8_t, 47> Float4ReloadFarStub(uint32_t at) {
  const auto byte = [](uint32_t value, int shift) { return static_cast<uint8_t>(value >> shift); };
  return {
      // movss xmm0, dword ptr [esp + at]
      0xF3, 0x0F, 0x10, 0x84, 0x24, byte(at, 0), byte(at, 8), byte(at, 16), byte(at, 24),
      // insertps xmm0, dword ptr [esp + at + 4], 0x10
      0x66, 0x0F, 0x3A, 0x21, 0x84, 0x24, byte(at + 4, 0), byte(at + 4, 8), byte(at + 4, 16), byte(at + 4, 24), 0x10,
      // insertps xmm0, dword ptr [esp + at + 8], 0x20
      0x66, 0x0F, 0x3A, 0x21, 0x84, 0x24, byte(at + 8, 0), byte(at + 8, 8), byte(at + 8, 16), byte(at + 8, 24), 0x20,
      // insertps xmm0, dword ptr [esp + at + 12], 0x30
      0x66, 0x0F, 0x3A, 0x21, 0x84, 0x24, byte(at + 12, 0), byte(at + 12, 8), byte(at + 12, 16), byte(at + 12, 24), 0x30,
      // jmp back (the instruction after the site)
      0xE9, 0x00, 0x00, 0x00, 0x00,
  };
}
constexpr std::array<std::array<uint8_t, 47>, 3> SOLDIER_ROWS_STUBS = {Float4ReloadFarStub(0x90), Float4ReloadFarStub(0xA0), Float4ReloadFarStub(0xB0)};

// 13. The matrix product's three row reloads, the same way (Faster Game CPU Code, SSE4.1). The product (+0x168d570, thiscall(const
// matrices*), ret 4; one caller, +0x17db161) multiplies each of this' 3x4 matrices ([this + 8], min([this + 4], [argument + 8]) of them)
// with the argument's ([argument + 0xc]) in place, then sets the byte at this + 1. It builds each product's three rows at [esp + 0x18 ..
// 0x47] with twelve `movss` stores, each lane by one 4-byte store on the loop's one way, and reloads them with the 5-byte `movups xmm0,
// [esp + 0x18 / 0x28 / 0x38]` (+0x168d7f0 / +0x168d803 / +0x168d812) to store them over this' matrix. Float4ReloadStub's stubs, each
// jumping back after its site. The same results as 9. Found by the whole function (no wildcard: the same bytes in the three games), at
// the Steam build's place first, then anywhere in the code. No branch lands inside a site (the whole DLL's relative branches and
// absolute pointers, the three games'), no relocation lies in the function, no hook of the Attila CPU Performance Fix (its pass timer
// takes the caller's first 7 bytes, +0x17db0f0).
constexpr std::string_view MATRIX_PRODUCT =
    "83 EC 3C 56 8B F1 80 7E 01 00 0F 85 AA 02 00 00 57 8B 7C 24 48 8B 57 08 3B 56 04 0F 43 56 04 85 D2 0F 84 8E 02 00 00 53 "
    "33 DB 66 0F 1F 44 00 00 8B 4E 08 8B 47 0C 03 CB 03 C3 F3 0F 10 09 F3 0F 10 40 0C F3 0F 10 20 F3 0F 10 50 10 F3 0F 11 44 "
    "24 0C F3 0F 10 40 1C F3 0F 11 44 24 10 F3 0F 10 40 2C F3 0F 10 58 20 F3 0F 11 44 24 14 F3 0F 10 41 04 F3 0F 59 CC F3 0F "
    "59 C2 F3 0F 10 69 20 F3 0F 10 78 14 F3 0F 58 C8 F3 0F 10 71 28 F3 0F 10 41 08 F3 0F 59 C3 F3 0F 58 C8 F3 0F 10 41 14 F3 "
    "0F 59 C2 F3 0F 11 4C 24 18 F3 0F 10 49 10 F3 0F 59 CC F3 0F 59 E5 F3 0F 58 C8 F3 0F 10 41 18 F3 0F 59 C3 F3 0F 59 DE F3 "
    "0F 58 C8 F3 0F 10 41 24 F3 0F 59 C2 F3 0F 10 50 24 F3 0F 11 4C 24 28 F3 0F 58 E0 F3 0F 10 09 F3 0F 59 48 04 F3 0F 10 41 "
    "04 F3 0F 59 C7 F3 0F 58 E3 F3 0F 10 59 18 F3 0F 58 C8 F3 0F 10 41 08 F3 0F 59 C2 F3 0F 11 64 24 38 F3 0F 10 61 14 F3 0F "
    "58 C8 0F 28 C4 F3 0F 59 C7 F3 0F 11 4C 24 1C F3 0F 10 49 10 F3 0F 59 48 04 F3 0F 58 C8 0F 28 C3 F3 0F 59 C2 F3 0F 59 D6 "
    "F3 0F 58 C8 F3 0F 10 41 24 F3 0F 59 C7 F3 0F 10 79 08 F3 0F 11 4C 24 2C F3 0F 10 48 04 F3 0F 59 CD F3 0F 58 C8 F3 0F 10 "
    "41 04 F3 0F 59 40 18 F3 0F 58 CA F3 0F 10 51 10 F3 0F 11 4C 24 3C F3 0F 10 09 F3 0F 59 48 08 F3 0F 58 C8 0F 28 C7 F3 0F "
    "59 40 28 F3 0F 58 C8 F3 0F 11 4C 24 20 0F 28 CA F3 0F 59 7C 24 14 83 C3 30 F3 0F 59 48 08 0F 28 C4 F3 0F 59 40 18 F3 0F "
    "59 64 24 10 F3 0F 58 C8 0F 28 C3 F3 0F 59 40 28 F3 0F 58 C8 F3 0F 10 40 08 F3 0F 59 C5 F3 0F 11 44 24 4C F3 0F 10 6C 24 "
    "4C F3 0F 11 4C 24 30 F3 0F 10 49 24 0F 28 C1 F3 0F 59 4C 24 10 F3 0F 59 40 18 F3 0F 58 E8 F3 0F 10 40 28 F3 0F 59 C6 F3 "
    "0F 58 E8 F3 0F 10 01 F3 0F 59 44 24 0C F3 0F 11 44 24 4C F3 0F 10 41 04 F3 0F 59 44 24 10 F3 0F 11 6C 24 40 F3 0F 10 6C "
    "24 4C F3 0F 58 E8 F3 0F 11 6C 24 4C 0F 28 C5 F3 0F 58 C7 F3 0F 10 7C 24 0C F3 0F 59 D7 F3 0F 59 79 20 F3 0F 58 D4 F3 0F "
    "10 64 24 14 F3 0F 59 DC F3 0F 59 E6 F3 0F 58 CF F3 0F 58 D3 F3 0F 58 41 0C F3 0F 58 CC F3 0F 58 51 1C F3 0F 11 44 24 24 "
    "0F 10 44 24 18 F3 0F 58 49 2C F3 0F 11 54 24 34 0F 11 01 0F 10 44 24 28 F3 0F 11 4C 24 44 0F 11 41 10 0F 10 44 24 38 0F "
    "11 41 20 83 EA 01 0F 85 7C FD FF FF 5B C6 46 01 01 5F 5E 83 C4 3C C2 04 00";
constexpr std::array<size_t, 3> MATRIX_PRODUCT_AT = {640, 659, 674};  // movups xmm0, xmmword ptr [esp + 0x18 / 0x28 / 0x38]
constexpr std::array<std::array<uint8_t, 35>, 3> MATRIX_PRODUCT_STUBS = {Float4ReloadStub<0x18>(), Float4ReloadStub<0x28>(), Float4ReloadStub<0x38>()};

// 14. The particle writer's table entry (the function of 9., +0x18960d0; Faster Particle Prefetch, its own row, Experimental).
// Its first calls find the entry of the table that holds the six vertex streams' pointers (a thiscall that returns the
// table's base + 24 * index and loads nothing of the entry), store the address into the frame and leave if it is null:
// `mov [esp + 0x7c], eax; test eax, eax; je the way out`. The entry's first load is some 1,000 instructions on (the first
// stream store, +0x189680b), so with the table cold the whole writer waits for the entry's line there. The stub runs the
// game's `mov` and `test` and prefetches the entry's first and last byte (24 bytes: one line or two) on the way, then goes
// on at the `je`. Same results: the store is the game's, no register is touched, the flags the `je` tests are those of the
// game's `test` (run again), and a prefetch of null, of garbage or of a no-access page never faults. The site is the `mov`
// and the first byte of the `test` (5 bytes); the second byte never runs. Found by the function's first 49 bytes (the call and
// the `je` wild), at the Steam build's place first, then anywhere in the code: Thrones' copy has the same bytes, Rome II has
// none. No branch lands inside the site (the whole DLL's relative branches and absolute pointers, Attila's and Thrones'); the
// call's return lands on its first byte.
constexpr std::string_view ENTRY_FETCH =
    "55 8B EC 83 E4 F0 81 EC B8 00 00 00 56 57 8B F9 89 7C 24 78 FF B7 D0 00 00 00 8B 8F CC 00 00 00 E8 ?? ?? ?? ?? 89 44 24 7C 85 C0 "
    "0F 84 ?? ?? ?? ??";
constexpr size_t ENTRY_FETCH_SITE = 37;    // mov dword ptr [esp + 0x7c], eax; test eax, eax (its first byte)
constexpr size_t ENTRY_FETCH_RESUME = 43;  // the je after the test
constexpr std::array<uint8_t, 18> ENTRY_FETCH_STUB = {
    0x89, 0x44, 0x24, 0x7C,        // mov dword ptr [esp + 0x7c], eax
    0x0F, 0x18, 0x08,              // prefetcht0 byte ptr [eax]           the entry's first line
    0x0F, 0x18, 0x48, 0x17,        // prefetcht0 byte ptr [eax + 0x17]    and its last byte's (the entry is 24 bytes)
    0x85, 0xC0,                    // test eax, eax                       the flags the game's je tests
    0xE9, 0x00, 0x00, 0x00, 0x00,  // jmp the game's je (patched in Prepare)
};

// 15. The render lists' item push loops (Faster Game CPU Code). Two builders of the render lists (Attila +0x17b83a0, loop head
// +0x17b8490, and +0x17b7bc0, head +0x17b7cb0; Thrones' copies are the same bytes) push the address of an item, `item + 0x50`
// (A) or `item + 0x38` (B), a round into a vector of 4-byte slots {+0x7c capacity, +0x80 size, +0x84 data} at edi (the loop's
// argument [esp + 0x40]): `push_back` inlined, its growth (a doubling copy) in the loop. The size lives in memory (`mov ecx,
// [edi + 0x80]` at the top, `inc dword ptr [edi + 0x80]` at the bottom: a store-forward chain of ~7 cycles an item) and the list
// ([esp + 0x1c], its item array +8), the capacity, the data pointer and the end are loaded again every round: 75-80 % of the
// loop's user time in the recordings (0.2-0.35 ms of the render thread's 0.5-0.7 ms; the doubling copy and its page faults are
// the rest). The site is the head's `mov eax, [esp + 0x1c]; mov ecx, [edi + 0x80]` (4 + 6 bytes: the jump takes the first 5, the
// stub goes on after the second). The stub runs both, then, with edx == 0 (no per-item call), the size below the capacity (a
// push that doesn't grow), more than ITEM_PUSH_MIN_RUN items left and room for them, runs the rounds but the last with the size
// in a register: the same loads of the list and its item array every round (two L1 loads, not on the size's chain), the same
// stores in the game's order (the scratch slots [esp + 0x30] (A) and [esp + 0x14], the slot, the size field), and none of the
// loads of the capacity, the data pointer and the end, which only the loop's own stores could change (the guard shows that they
// don't). The game's code then does the last round, so every register that is live afterwards, the flags and every byte of
// memory are the game's (edx is 0 again, eax the list again; ebx (A), esi and ecx are written before they are read at the top,
// the flags are set by the push's capacity compare before they are read). The comparisons are the game's unsigned ones (`cmp
// ebp, end; jb`: the remaining count is only taken when ebp is below the end). A guard (once a run) shows that no store of the
// run can reach what the run keeps in registers or reads from the frame: the vector object's three fields, the stack from the
// stub's own pushes (B's limit) to the loop's slots; it also refuses a store on the list's item array field and on the list's
// slot, which the run loads again every round now (cautious, not needed); otherwise the game's code runs the round.
// The premise: no other thread changes the output vector (edi) while a run is in it, as in 11. (the emitter loop) but with a
// worse failure (there a violation only feeds a prefetch, here it would be a store through a kept pointer or size). The
// loops' own `inc dword ptr [edi + 0x80]` is a non-atomic read-modify-write of that vector's size, so a second thread pushing to
// the same vector would already corrupt the game; both builders are reached only through vtable thunks (+0x1863340, +0x1863300,
// +0x1902B70, +0x1902B90; the owner thread was not traced). The source list is not part of the premise: it is the builder's
// second argument + 4 (the list getter's `lea eax, [ecx + 4]`), another object that another thread may grow, and the stub
// loads it and its item array every round like the game, so an array that moves between rounds is read where it is then (an
// item array pointer kept for the run would read the freed old array). The stub is exact for one thread pushing to the output
// vector. Fault order matters only if something caught an access violation and went on (nothing does): the
// stub reads [edi + 0x7c] and [edi + 0x84] before [list + 8] and the item array's slot (the game's order: list item array, slot,
// capacity, data), so a bad list or item array and an unreadable header word together fault in another order; the kept
// [edi + 0x84] can fault one round's two scratch stores earlier (an unreadable data field next to a readable size field); at a
// fault edx, ebx, eax and esi are not the game's, B's esp is 4 lower and 8 to 12 bytes below esp were written (the call, the
// pushes). Found by the whole loop's bytes (calls and the jump out wild); no branch lands inside the site (the whole DLLs'
// relative branches and absolute pointers, Attila's and Thrones': only the loop's own back-edge at +0; one rel8 hit at B's +5
// is the operand byte of the `cmp ebp, ebx` before the loop). Rome II has neither (rva 0).
constexpr std::string_view ITEM_PUSH_A =
    "3B E9 0F 83 ?? ?? ?? ?? 0F 1F 44 00 00 8B 44 24 1C 8B 8F 80 00 00 00 8B 40 08 8B 1C A8 8B 47 7C 89 5C 24 30 8D 73 50 89 "
    "74 24 14 3B C8 73 12 8B 87 84 00 00 00 89 34 88 E9 CA 00 00 00 33 D2 EB 9F 85 C0 75 26 50 6A 01 C7 47 7C 01 00 00 00 E8 "
    "?? ?? ?? ?? 8B 8F 80 00 00 00 83 C4 08 89 87 84 00 00 00 89 34 88 E9 98 00 00 00 8B 9F 84 00 00 00 03 C0 89 47 7C 74 13 "
    "6A 00 50 E8 ?? ?? ?? ?? 8B 8F 80 00 00 00 83 C4 08 EB 02 33 C0 33 D2 89 87 84 00 00 00 85 C9 74 1B 0F 1F 40 00 8B 8F 84 "
    "00 00 00 8B 04 93 89 04 91 42 8B 8F 80 00 00 00 3B D1 72 E9 8D 44 24 14 3B C3 72 2D 8D 14 8D 00 00 00 00 8D 7C 24 14 8D "
    "04 1A 3B F8 8B 7C 24 40 73 17 8B 8F 84 00 00 00 8D 44 24 14 2B C3 C1 F8 02 8B 04 81 89 04 0A EB 09 8B 87 84 00 00 00 89 "
    "34 88 85 DB 74 09 53 E8 ?? ?? ?? ?? 83 C4 04 8B 5C 24 30 8B 54 24 3C FF 87 80 00 00 00 85 D2 74 0E FF 73 38 8B CA E8 ?? "
    "?? ?? ?? 8B 54 24 3C 45 3B 6C 24 34 0F 82 E3 FE FF FF";
constexpr std::string_view ITEM_PUSH_B =
    "3B EB 0F 83 ?? ?? ?? ?? 0F 1F 44 00 00 8B 44 24 1C 8B 8F 80 00 00 00 8B 70 08 8B 47 7C 8B 34 AE 83 C6 38 89 74 24 14 3B "
    "C8 73 12 8B 87 84 00 00 00 89 34 88 E9 CE 00 00 00 33 D2 EB A3 85 C0 75 26 50 6A 01 C7 47 7C 01 00 00 00 E8 ?? ?? ?? ?? "
    "8B 8F 80 00 00 00 83 C4 08 89 87 84 00 00 00 89 34 88 E9 9C 00 00 00 8B 9F 84 00 00 00 03 C0 89 47 7C 74 13 6A 00 50 E8 "
    "?? ?? ?? ?? 8B 8F 80 00 00 00 83 C4 08 EB 02 33 C0 33 D2 89 87 84 00 00 00 85 C9 74 1F 0F 1F 84 00 00 00 00 00 8B 8F 84 "
    "00 00 00 8B 04 93 89 04 91 42 8B 8F 80 00 00 00 3B D1 72 E9 8D 44 24 14 3B C3 72 2D 8D 14 8D 00 00 00 00 8D 7C 24 14 8D "
    "04 1A 3B F8 8B 7C 24 40 73 17 8B 8F 84 00 00 00 8D 44 24 14 2B C3 C1 F8 02 8B 04 81 89 04 0A EB 09 8B 87 84 00 00 00 89 "
    "34 88 85 DB 74 09 53 E8 ?? ?? ?? ?? 83 C4 04 8B 5C 24 30 8B 54 24 3C FF 87 80 00 00 00 85 D2 74 0D 6A FF 8B CA E8 ?? ?? "
    "?? ?? 8B 54 24 3C 45 3B EB 0F 82 E6 FE FF FF";
constexpr size_t ITEM_PUSH_SITE = 13;  // mov eax, dword ptr [esp + 0x1c]; mov ecx, dword ptr [edi + 0x80] (the jump takes the first 5 bytes)
constexpr size_t ITEM_PUSH_SIZE = 10;  // both instructions: where each stub goes on
constexpr uint8_t ITEM_PUSH_MIN_RUN = 4;  // rounds of a run (the stub's compares are against MIN_RUN + 1 items left)
// The guard (first), then stub A, then stub B in one array: the stubs call the guard by a relative call.
constexpr std::array<size_t, 3> ITEM_PUSH_STUB_AT = {0x6E, 0xEA, 361};  // the stubs' starts and the end
constexpr std::array<uint8_t, 361> ITEM_PUSH_STUB = {
    // guard:
    0x53,                                          // +0x00 push ebx
    0x81, 0xFF, 0x00, 0xFF, 0xFF, 0xFF,            // +0x01 cmp edi, 0xffffff00
    0x77, 0x5E,                                    // +0x07 ja bad
    0x3D, 0x00, 0xFF, 0xFF, 0xFF,                  // +0x09 cmp eax, 0xffffff00
    0x77, 0x57,                                    // +0x0E ja bad
    0x8D, 0x1C, 0xB2,                              // +0x10 lea ebx, [edx + esi*4]
    0x39, 0xD3,                                    // +0x13 cmp ebx, edx
    0x72, 0x50,                                    // +0x15 jb bad
    0x8D, 0x77, 0x7C,                              // +0x17 lea esi, [edi + 0x7c]
    0x39, 0xF3,                                    // +0x1A cmp ebx, esi
    0x76, 0x0A,                                    // +0x1C jbe hdr_clear
    0x8D, 0xB7, 0x88, 0x00, 0x00, 0x00,            // +0x1E lea esi, [edi + 0x88]
    0x39, 0xF2,                                    // +0x24 cmp edx, esi
    0x72, 0x3F,                                    // +0x26 jb bad
    // hdr_clear:
    0x8D, 0x70, 0x08,                              // +0x28 lea esi, [eax + 8]
    0x39, 0xF3,                                    // +0x2B cmp ebx, esi
    0x76, 0x07,                                    // +0x2D jbe field_clear
    0x8D, 0x70, 0x0C,                              // +0x2F lea esi, [eax + 0xc]
    0x39, 0xF2,                                    // +0x32 cmp edx, esi
    0x72, 0x31,                                    // +0x34 jb bad
    // field_clear:
    0x8D, 0x77, 0x7B,                              // +0x36 lea esi, [edi + 0x7b]
    0x29, 0xC6,                                    // +0x39 sub esi, eax
    0x83, 0xFE, 0x07,                              // +0x3B cmp esi, 7
    0x72, 0x27,                                    // +0x3E jb bad
    0x39, 0xE3,                                    // +0x40 cmp ebx, esp
    0x76, 0x08,                                    // +0x42 jbe frame_clear
    0x8D, 0x74, 0x24, 0x40,                        // +0x44 lea esi, [esp + 0x40]
    0x39, 0xF2,                                    // +0x48 cmp edx, esi
    0x72, 0x1B,                                    // +0x4A jb bad
    // frame_clear:
    0x8D, 0xB7, 0x87, 0x00, 0x00, 0x00,            // +0x4C lea esi, [edi + 0x87]
    0x29, 0xE6,                                    // +0x52 sub esi, esp
    0x83, 0xFE, 0x4C,                              // +0x54 cmp esi, 0x4c
    0x72, 0x0E,                                    // +0x57 jb bad
    0x8D, 0x70, 0x0B,                              // +0x59 lea esi, [eax + 0xb]
    0x29, 0xE6,                                    // +0x5C sub esi, esp
    0x83, 0xFE, 0x44,                              // +0x5E cmp esi, 0x44
    0x72, 0x04,                                    // +0x61 jb bad
    0x31, 0xF6,                                    // +0x63 xor esi, esi
    0x5B,                                          // +0x65 pop ebx
    0xC3,                                          // +0x66 ret
    // bad:
    0xBE, 0x01, 0x00, 0x00, 0x00,                  // +0x67 mov esi, 1
    0x5B,                                          // +0x6C pop ebx
    0xC3,                                          // +0x6D ret
    // stub_a:
    0x8B, 0x44, 0x24, 0x1C,                        // +0x6E mov eax, dword ptr [esp + 0x1c]
    0x8B, 0x8F, 0x80, 0x00, 0x00, 0x00,            // +0x72 mov ecx, dword ptr [edi + 0x80]
    0x85, 0xD2,                                    // +0x78 test edx, edx
    0x75, 0x65,                                    // +0x7A jne keep_a
    0x8B, 0x5C, 0x24, 0x34,                        // +0x7C mov ebx, dword ptr [esp + 0x34]
    0x39, 0xDD,                                    // +0x80 cmp ebp, ebx
    0x73, 0x5D,                                    // +0x82 jae keep_a
    0x29, 0xEB,                                    // +0x84 sub ebx, ebp
    0x83, 0xFB, 0x05,                              // +0x86 cmp ebx, 5
    0x72, 0x56,                                    // +0x89 jb keep_a
    0x8B, 0x77, 0x7C,                              // +0x8B mov esi, dword ptr [edi + 0x7c]
    0x39, 0xF1,                                    // +0x8E cmp ecx, esi
    0x73, 0x4F,                                    // +0x90 jae keep_a
    0x81, 0xFE, 0x00, 0x00, 0x10, 0x00,            // +0x92 cmp esi, 0x100000
    0x73, 0x47,                                    // +0x98 jae keep_a
    0x8B, 0x97, 0x84, 0x00, 0x00, 0x00,            // +0x9A mov edx, dword ptr [edi + 0x84]
    0xE8, 0x5B, 0xFF, 0xFF, 0xFF,                  // +0xA0 call guard
    0x85, 0xF6,                                    // +0xA5 test esi, esi
    0x75, 0x36,                                    // +0xA7 jne game_a
    0x8B, 0x77, 0x7C,                              // +0xA9 mov esi, dword ptr [edi + 0x7c]
    0x29, 0xCE,                                    // +0xAC sub esi, ecx
    0x39, 0xF3,                                    // +0xAE cmp ebx, esi
    0x0F, 0x47, 0xDE,                              // +0xB0 cmova ebx, esi
    0x83, 0xFB, 0x05,                              // +0xB3 cmp ebx, 5
    0x72, 0x27,                                    // +0xB6 jb game_a
    0x4B,                                          // +0xB8 dec ebx
    0x01, 0xEB,                                    // +0xB9 add ebx, ebp
    // run_a:
    0x8B, 0x44, 0x24, 0x1C,                        // +0xBB mov eax, dword ptr [esp + 0x1c]    the list and its item array every round
    0x8B, 0x40, 0x08,                              // +0xBF mov eax, dword ptr [eax + 8]
    0x8B, 0x34, 0xA8,                              // +0xC2 mov esi, dword ptr [eax + ebp*4]
    0x89, 0x74, 0x24, 0x30,                        // +0xC5 mov dword ptr [esp + 0x30], esi
    0x83, 0xC6, 0x50,                              // +0xC9 add esi, 0x50
    0x89, 0x74, 0x24, 0x14,                        // +0xCC mov dword ptr [esp + 0x14], esi
    0x89, 0x34, 0x8A,                              // +0xD0 mov dword ptr [edx + ecx*4], esi
    0x41,                                          // +0xD3 inc ecx
    0x89, 0x8F, 0x80, 0x00, 0x00, 0x00,            // +0xD4 mov dword ptr [edi + 0x80], ecx
    0x45,                                          // +0xDA inc ebp
    0x39, 0xDD,                                    // +0xDB cmp ebp, ebx
    0x72, 0xDC,                                    // +0xDD jb run_a
    // game_a:
    0x31, 0xD2,                                    // +0xDF xor edx, edx
    // keep_a:
    0x8B, 0x44, 0x24, 0x1C,                        // +0xE1 mov eax, dword ptr [esp + 0x1c]
    0xE9, 0x00, 0x00, 0x00, 0x00,                  // +0xE5 jmp the game's code after the site's two instructions (patched in Prepare)
    // stub_b:
    0x8B, 0x44, 0x24, 0x1C,                        // +0xEA mov eax, dword ptr [esp + 0x1c]
    0x8B, 0x8F, 0x80, 0x00, 0x00, 0x00,            // +0xEE mov ecx, dword ptr [edi + 0x80]
    0x85, 0xD2,                                    // +0xF4 test edx, edx
    0x75, 0x68,                                    // +0xF6 jne keep_b
    0x39, 0xDD,                                    // +0xF8 cmp ebp, ebx
    0x73, 0x64,                                    // +0xFA jae keep_b
    0x89, 0xDE,                                    // +0xFC mov esi, ebx
    0x29, 0xEE,                                    // +0xFE sub esi, ebp
    0x83, 0xFE, 0x05,                              // +0x100 cmp esi, 5
    0x72, 0x5B,                                    // +0x103 jb keep_b
    0x8B, 0x77, 0x7C,                              // +0x105 mov esi, dword ptr [edi + 0x7c]
    0x39, 0xF1,                                    // +0x108 cmp ecx, esi
    0x73, 0x54,                                    // +0x10A jae keep_b
    0x81, 0xFE, 0x00, 0x00, 0x10, 0x00,            // +0x10C cmp esi, 0x100000
    0x73, 0x4C,                                    // +0x112 jae keep_b
    0x8B, 0x97, 0x84, 0x00, 0x00, 0x00,            // +0x114 mov edx, dword ptr [edi + 0x84]
    0xE8, 0xE1, 0xFE, 0xFF, 0xFF,                  // +0x11A call guard
    0x85, 0xF6,                                    // +0x11F test esi, esi
    0x75, 0x3B,                                    // +0x121 jne game_b
    0x8B, 0x77, 0x7C,                              // +0x123 mov esi, dword ptr [edi + 0x7c]
    0x29, 0xCE,                                    // +0x126 sub esi, ecx
    0x89, 0xD8,                                    // +0x128 mov eax, ebx
    0x29, 0xE8,                                    // +0x12A sub eax, ebp
    0x39, 0xF0,                                    // +0x12C cmp eax, esi
    0x0F, 0x47, 0xC6,                              // +0x12E cmova eax, esi
    0x83, 0xF8, 0x05,                              // +0x131 cmp eax, 5
    0x72, 0x28,                                    // +0x134 jb game_b
    0x48,                                          // +0x136 dec eax
    0x01, 0xE8,                                    // +0x137 add eax, ebp
    0x50,                                          // +0x139 push eax
    // run_b:
    0x8B, 0x44, 0x24, 0x20,                        // +0x13A mov eax, dword ptr [esp + 0x20]    the list and its item array every round
    0x8B, 0x40, 0x08,                              // +0x13E mov eax, dword ptr [eax + 8]
    0x8B, 0x34, 0xA8,                              // +0x141 mov esi, dword ptr [eax + ebp*4]
    0x83, 0xC6, 0x38,                              // +0x144 add esi, 0x38
    0x89, 0x74, 0x24, 0x18,                        // +0x147 mov dword ptr [esp + 0x18], esi
    0x89, 0x34, 0x8A,                              // +0x14B mov dword ptr [edx + ecx*4], esi
    0x41,                                          // +0x14E inc ecx
    0x89, 0x8F, 0x80, 0x00, 0x00, 0x00,            // +0x14F mov dword ptr [edi + 0x80], ecx
    0x45,                                          // +0x155 inc ebp
    0x3B, 0x2C, 0x24,                              // +0x156 cmp ebp, dword ptr [esp]
    0x72, 0xDF,                                    // +0x159 jb run_b
    0x83, 0xC4, 0x04,                              // +0x15B add esp, 4
    // game_b:
    0x31, 0xD2,                                    // +0x15E xor edx, edx
    // keep_b:
    0x8B, 0x44, 0x24, 0x1C,                        // +0x160 mov eax, dword ptr [esp + 0x1c]
    0xE9, 0x00, 0x00, 0x00, 0x00,                  // +0x164 jmp the game's code after the site's two instructions (patched in Prepare)
};
static_assert(ITEM_PUSH_STUB.size() == ITEM_PUSH_STUB_AT[2] && ITEM_PUSH_STUB[ITEM_PUSH_STUB_AT[1] - 5] == 0xE9
              && ITEM_PUSH_STUB[ITEM_PUSH_STUB_AT[2] - 5] == 0xE9);  // each stub ends with its jump into the game's code
static_assert(ITEM_PUSH_STUB[0x88] == ITEM_PUSH_MIN_RUN + 1 && ITEM_PUSH_STUB[0xB5] == ITEM_PUSH_MIN_RUN + 1
              && ITEM_PUSH_STUB[0x102] == ITEM_PUSH_MIN_RUN + 1 && ITEM_PUSH_STUB[0x133] == ITEM_PUSH_MIN_RUN + 1);  // the stubs' `cmp reg, MIN_RUN + 1`

// 16. The pending list sweep's chain prefetch (Faster Sweep Prefetch, its own row, Experimental, off). The render thread's
// per-frame sweep over the pending lists (Attila +0x1893290, Thrones +0x19085f0, one caller; thiscall, `this` in esi) walks a
// vector of {list object, control block} (the count at [this + 0x18], the entries at [this + 0x1c]) from the last entry down
// (ebx = count - 1 .. 0), asks the ready check of 5. (+0x18635a0, ecx the list object) about each and erases a ready entry by
// moving the last one in (only entry ebx and the count change: the entries below ebx stay as they are). The check reads the
// list object's flag byte [+4], its children array [+0x44] and count [+0x40] and the children's ready bytes: a chain of
// dependent misses per list (list object, array, first child) that the sweep never overlaps with the next list's. The site is
// the loop's top, `mov eax, [esi + 0x1c]; mov ecx, [eax + ebx*8]` (3 + 3 bytes: the jump takes the first 5, the stub goes on
// at the call, +6). The stub runs both after three stages for the entries A, B and C places below ebx (each `ebx - d` inside
// the vector as it is right now: the unsigned compare with the count also skips the wrap below 0): stage A prefetches the list
// object's first two lines; stage B (a null list object skipped) loads its children array pointer (that line was prefetched
// two entries ago) and prefetches the array's line; stage C (null skipped) loads the count (0: skipped, the game never reads
// the array then) and the array's first child pointer and prefetches that child's ready byte. eax, ecx and edx are the only
// registers it changes and the game's reload of ecx comes last (edx is dead: written before it is read in the erase path,
// never read on the not-ready one; the flags are dead too: the call's result test sets them); it stores nothing. Every real
// load is of an address the game's own check of that entry reads later in the same sweep (the entry slots inside [0, count),
// [list + 0x40], [list + 0x44]) or that lies in the block it reads: [array] with count > 0, which with the ready check's stub
// on and the list's flag clear nobody reads (the stub reads [array + 8 * count - 4]), but it is the same block, so the load is
// safe all the same; the child's pointer is only prefetched. The premise (that of 8., the child sum): nothing but the sweep
// itself removes or changes the pending lists below ebx while it is in the entry above, and no other thread frees a children
// array meanwhile. Known limit: a non-null list object whose +0x40 / +0x44 cannot be read makes the stub fault up to 4
// entries' checks earlier than the game (stage B reads [list + 0x44] four entries ahead, stage C [list + 0x40] two ahead; the
// game faults at its own check of that entry, so it is a crash either way; the stub stores nothing, so only what the entries
// in between did first is missing). Found by the whole function's bytes (the calls and the mov's constant wild); Thrones'
// copy is the same bytes at +0x19085f0; Rome II has none.
constexpr std::string_view SWEEP =
    "56 8B F1 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 0F 84 82 00 00 00 8B 46 18 53 8D 58 FF 3B D8 73 76 57 8B 46 1C 8B 0C D8 E8 ?? ?? ?? ?? 84 "
    "C0 74 5F 8B 4E 1C 8B 46 18 8D 14 D9 8D 04 C1 8B 48 FC 85 C9 74 07 F0 FF 41 04 8B 48 FC 8B 7A 04 8B 40 F8 89 02 89 4A 04 85 FF 74 21 "
    "83 C8 FF F0 0F C1 47 04 75 17 8B 07 8B CF FF 10 83 C8 FF F0 0F C1 47 08 75 07 8B 07 8B CF FF 50 04 FF 4E 18 8B 4E 18 8B 46 1C 8D 04 "
    "C8 50 E8 ?? ?? ?? ?? 83 C4 04 4B 3B 5E 18 72 8C 5F 5B 5E C3";
constexpr size_t SWEEP_SITE = 33;  // mov eax, dword ptr [esi + 0x1c]; mov ecx, dword ptr [eax + ebx*8] (the jump takes the first 5 bytes)
constexpr size_t SWEEP_SIZE = 6;   // both instructions: where the stub goes on
// The stub for the three distances A > B > C (the entries `ebx - A`, `ebx - B`, `ebx - C` are looked at; each 1 to 127). The
// page's is (8, 4, 2): the harness's 4,000 cold lists of 1 to 4 children cost 34.2 ns a list with the ready check's stub, 29.0
// with (4, 2, 1), 26.3 with (6, 3, 1) and 24.9 with (8, 4, 2) (full length, medians of 9; the quick runs under more load:
// 76.5 / 45.3 / 38.7 / 33.6, 37.2 / 30.3 / 26.8 / 25.1).
constexpr std::array<uint8_t, 80> SweepPrefetchStub(uint8_t a, uint8_t b, uint8_t c) {
  if (c == 0 || a > 127 || a <= b || b <= c) {
    throw std::invalid_argument("SweepPrefetchStub needs 127 >= A > B > C >= 1");  // a compile error in a constant
  }
  return {
      0x8B, 0x46, 0x1C,                      // mov eax, dword ptr [esi + 0x1c]
      0x8D, 0x53, static_cast<uint8_t>(-a),  // lea edx, [ebx - A]
      0x3B, 0x56, 0x18,                      // cmp edx, dword ptr [esi + 0x18]
      0x73, 0x0A,                            // jae skip_a                     not inside the vector (unsigned: the wrap below 0 too)
      0x8B, 0x0C, 0xD0,                      // mov ecx, dword ptr [eax + edx*8]   the entry's list object
      0x0F, 0x18, 0x09,                      // prefetcht0 [ecx]
      0x0F, 0x18, 0x49, 0x40,                // prefetcht0 [ecx + 0x40]
      0x8D, 0x53, static_cast<uint8_t>(-b),  // skip_a: lea edx, [ebx - B]
      0x3B, 0x56, 0x18,                      // cmp edx, dword ptr [esi + 0x18]
      0x73, 0x0D,                            // jae skip_b
      0x8B, 0x0C, 0xD0,                      // mov ecx, dword ptr [eax + edx*8]
      0x85, 0xC9,                            // test ecx, ecx
      0x74, 0x06,                            // je skip_b                      a null list object
      0x8B, 0x49, 0x44,                      // mov ecx, dword ptr [ecx + 0x44]    the children array
      0x0F, 0x18, 0x09,                      // prefetcht0 [ecx]
      0x8D, 0x53, static_cast<uint8_t>(-c),  // skip_b: lea edx, [ebx - C]
      0x3B, 0x56, 0x18,                      // cmp edx, dword ptr [esi + 0x18]
      0x73, 0x16,                            // jae skip_c
      0x8B, 0x0C, 0xD0,                      // mov ecx, dword ptr [eax + edx*8]
      0x85, 0xC9,                            // test ecx, ecx
      0x74, 0x0F,                            // je skip_c
      0x83, 0x79, 0x40, 0x00,                // cmp dword ptr [ecx + 0x40], 0
      0x74, 0x09,                            // je skip_c                      no child: the game never reads the array
      0x8B, 0x49, 0x44,                      // mov ecx, dword ptr [ecx + 0x44]
      0x8B, 0x09,                            // mov ecx, dword ptr [ecx]           the first child's object
      0x0F, 0x18, 0x49, 0x6E,                // prefetcht0 [ecx + 0x6e]            its ready byte
      0x8B, 0x0C, 0xD8,                      // skip_c: mov ecx, dword ptr [eax + ebx*8]   the game's second instruction
      0xE9, 0x00, 0x00, 0x00, 0x00,          // jmp the call (+6; patched in Prepare)
  };
}
constexpr std::array<uint8_t, 80> SWEEP_STUB = SweepPrefetchStub(8, 4, 2);

// 18. Model Detail Distance (its own row; Attila +0x183d570, Rome II +0x14f0710, Thrones +0x18b2060: the same bytes): the game's LOD pick
// `Entry* __thiscall PickLod(Model* model, float distance_squared, Entry** next)` returns the first LOD whose distance squared (entry + 0x38) is beyond the
// camera's, for trees and other objects (not battle units). The stub multiplies `distance_squared` by 1 / slider squared, so they keep their detail farther away.
// The jump goes in while the slider is above 1x and the game's bytes are back at 1x, at any time (a detour can't go in while the game runs); a slider move
// writes only the float.
// The site is the first three instructions, whole and position independent (`push ebx; push ebp; mov ebp, [esp + 0x10]`); the stub runs them and goes on at +6.
// The product goes over the argument's own slot: the call is callee-pop (`ret 8`), the callers never read the slot after it (part 22 reads every direct caller),
// and PickLod reads it once, `movss xmm1, [esp + 0x14]`, unless an early return (no LOD, or one LOD that passes the +0x20 / +0x2c test) comes first. Registers and
// flags stay (lea, never add), xmm0 is saved below the new esp and loaded back, MXCSR's control bits are not touched. The multiply leaves its sticky exception flags
// set, as the old hook's did (volatile across a call; PickLod's comiss sets them too): saving and loading them back would cost as much as the function. The
// scale is a float in a page of its own (this one is read-execute), never freed: a thread can be in the stub after the module is gone.
constexpr std::string_view PICK_LOD =
    "53 55 8B 6C 24 10 56 57 8B F9 C7 45 00 00 00 00 00 8B 47 28 85 C0 0F 84 ?? ?? ?? ?? 83 F8 01 75 ?? 8B 47 2C 8B 00 "
    "83 78 20 00 75 ?? 83 78 2C 00 77 ?? 8B 0D";
constexpr size_t PICK_LOD_RESUME = 6;  // after the three instructions the stub runs
constexpr uint32_t UNSCALED_BITS = 0x3F800000;  // 1.f
constexpr std::array<uint8_t, 47> MODEL_DETAIL_STUB = {
    0x8D, 0x64, 0x24, 0xF0,                    // lea esp, [esp - 16]
    0x0F, 0x11, 0x04, 0x24,                    // movups [esp], xmm0
    0xF3, 0x0F, 0x10, 0x44, 0x24, 0x14,        // movss xmm0, [esp + 20]             the distance squared (the argument at the entry's esp + 4)
    0xF3, 0x0F, 0x59, 0x05, 0x01, 0x5A, 0x5A, 0x5A,  // mulss xmm0, [the scale]      (STUB_FIELD_SENTINEL: Prepare puts the float's address in)
    0xF3, 0x0F, 0x11, 0x44, 0x24, 0x14,        // movss [esp + 20], xmm0             over its slot
    0x0F, 0x10, 0x04, 0x24,                    // movups xmm0, [esp]
    0x8D, 0x64, 0x24, 0x10,                    // lea esp, [esp + 16]
    0x53,                                      // push ebx                           the game's three instructions
    0x55,                                      // push ebp
    0x8B, 0x6C, 0x24, 0x10,                    // mov ebp, [esp + 0x10]
    0xE9, 0x00, 0x00, 0x00, 0x00,              // jmp PickLod + 6 (patched in Prepare)
};

// 19. Animation Rows on Workers (Attila only; animation_rows.hpp) and 20. the Pending Sweep trace (pending_sweep.hpp):
// the two cut sites. 19. is the animation update's call of the animation texture fill (+0x1893d2a, `call` with seven
// arguments on the stack, the cache in ecx, the callee pops them: `ret 0x1c`); the pattern is the instructions around
// it, and its target must be the build's fill with the head below. 20. is the pending list sweep's first instructions
// (+0x18932a5 in SWEEP: `mov eax, [esi + 0x18]; push ebx` and the first byte of `lea ebx, [eax - 1]`, run again by the
// stub; the sweep goes on at the `cmp`; no branch of the game lands in between). Both stubs read their function from a
// word of the data page (0: the row is off, or the site is leaving), raise `cut_active` around the call and read the
// word again after raising it: a turn-off zeroes the word, then waits for `cut_active` to be 0 and for every thread to
// be out of the stubs (PausedWrite's cut rule) before the site is written back, so no thread is ever in a function of
// this module with the site gone. A stack unwind through a stub (an exception from the game's fill) leaves `cut_active`
// raised: the site then never comes out and the module is pinned.
constexpr std::string_view FILL_CALL_CONTEXT = "8D 44 24 10 8B CE 50 53 55 57 FF 74 24 30 FF 74 24 30 FF 74 24 30 E8 ?? ?? ?? ?? 84 C0 74 3D 39 5D 00";
constexpr size_t FILL_CALL_AT = 22;  // the call's E8 in the pattern
constexpr std::string_view FILL_HEAD = "83 EC 30 53 8B 5C 24 48 55 56 8B 74 24 54 2B 33 57 8B F9";
constexpr size_t SWEEP_ENTRY_AT = 21;       // in SWEEP: the first of the three instructions
constexpr size_t SWEEP_ENTRY_RESUME = 28;   // in SWEEP: the `cmp` after them
// The data page's words, as fields of the stubs: STUB_FIELD_SENTINEL + 0 the count, + 1 the fill's word, + 2 the
// sweep's; the callee (sentinel + 0) is the game's fill, or the sweep's `cmp`.
constexpr std::array<uint8_t, 77> FILL_CALL_STUB = {
    0xA1, 0x02, 0x5A, 0x5A, 0x5A,              // mov eax, dword ptr [fill word]
    0x85, 0xC0,                                // test eax, eax
    0x74, 0x3F,                                // jz game                          off, or leaving
    0xF0, 0xFF, 0x05, 0x01, 0x5A, 0x5A, 0x5A,  // lock inc dword ptr [cut active]
    0xA1, 0x02, 0x5A, 0x5A, 0x5A,              // mov eax, dword ptr [fill word]   again, after the count went up
    0x85, 0xC0,                                // test eax, eax
    0x74, 0x28,                                // jz undo
    0xFF, 0x74, 0x24, 0x1C,                    // push dword ptr [esp + 0x1c]      the seven arguments again (each push moves esp by 4)
    0xFF, 0x74, 0x24, 0x1C,
    0xFF, 0x74, 0x24, 0x1C,
    0xFF, 0x74, 0x24, 0x1C,
    0xFF, 0x74, 0x24, 0x1C,
    0xFF, 0x74, 0x24, 0x1C,
    0xFF, 0x74, 0x24, 0x1C,
    0xFF, 0xD0,                                // call eax                         __fastcall Dispatch(ecx = the cache), pops the seven
    0xF0, 0xFF, 0x0D, 0x01, 0x5A, 0x5A, 0x5A,  // lock dec dword ptr [cut active]  eax is the result
    0xC2, 0x1C, 0x00,                          // ret 0x1c
    0xF0, 0xFF, 0x0D, 0x01, 0x5A, 0x5A, 0x5A,  // undo: lock dec dword ptr [cut active]
    0xE9, 0x11, 0x5A, 0x5A, 0x5A,              // game: jmp the game's fill        the same stack and ecx: it returns to the caller itself
};
constexpr std::array<uint8_t, 48> SWEEP_ENTRY_STUB = {
    0xA1, 0x03, 0x5A, 0x5A, 0x5A,              // mov eax, dword ptr [sweep word]
    0x85, 0xC0,                                // test eax, eax
    0x74, 0x1B,                                // jz resume
    0xF0, 0xFF, 0x05, 0x01, 0x5A, 0x5A, 0x5A,  // lock inc dword ptr [cut active]
    0xA1, 0x03, 0x5A, 0x5A, 0x5A,              // mov eax, dword ptr [sweep word]  again, after the count went up
    0x85, 0xC0,                                // test eax, eax
    0x74, 0x04,                                // jz undo
    0x8B, 0xCE,                                // mov ecx, esi                     the list being swept
    0xFF, 0xD0,                                // call eax                         __fastcall Arm(list)
    0xF0, 0xFF, 0x0D, 0x01, 0x5A, 0x5A, 0x5A,  // undo: lock dec dword ptr [cut active]
    0x8B, 0x46, 0x18,                          // resume: mov eax, dword ptr [esi + 0x18]   the game's three instructions (eax, ecx, edx are dead here)
    0x53,                                      // push ebx
    0x8D, 0x58, 0xFF,                          // lea ebx, [eax - 1]
    0xE9, 0x11, 0x5A, 0x5A, 0x5A,              // jmp the sweep's cmp
};
// 28. Cached Particle Count (the effect update's small item, Attila; its own row): the effects render's visible particle sum (+0x1929E60, a thiscall on the scene node
// with no stack argument and a plain `ret`) walks every effect once per view. Two stubs cache it per node and per update of the node. The epoch (+0x192D290, the
// node's update, entered once per update) bumps a generation word and runs the game's own `sub esp, 0xBC`. The sum's entry answers from the cache when the node and the
// generation are the cached ones, else runs the game's body (a call of the trampoline: the five bytes the jump took, then a jump into the body) and keeps the result.
// With `check` set the body also runs on a hit and its result is compared with the cache (a mismatch is counted, the game's sum is returned).
constexpr uint32_t PARTICLE_EPOCH_RVA = 0x192D290;
constexpr std::string_view PARTICLE_EPOCH = "81 EC BC 00 00 00 53 55 56 57 8D 44 24 3C C7 44 24 70 00 00 00 00 50 8D 44 24 74 C7 44 24 78 00 00 00 00 8B D9";
constexpr size_t PARTICLE_EPOCH_SIZE = 6;  // `sub esp, 0xBC`: the jump takes 5 of its bytes, the stub runs it whole and goes on after it
constexpr uint32_t PARTICLE_COUNT_RVA = 0x1929E60;
constexpr std::string_view PARTICLE_COUNT = "83 EC 50 56 57 8B F1 33 FF E8 ?? ?? ?? ?? 84 C0 74 0F 8B 86 80 01 00 00 5F 5E 8B 40 0C 83 C4 50 C3";
constexpr size_t PARTICLE_COUNT_HEAD = 5;  // sub esp, 0x50; push esi; push edi: the trampoline's copy
// The cache's words (the data page): one node's sum for one generation of the update.
struct CountData {
  uint32_t generation = 1;         // bumped by the epoch stub; the cache is good for one value of it
  uint32_t node = 0;               // the node the cached sum is of
  uint32_t cached_generation = 0;  // 0: nothing cached
  uint32_t value = 0;
  uint32_t check = 0;  // set by Update: the game's body also runs on a hit and is compared
  uint32_t hits = 0;
  uint32_t misses = 0;
  uint32_t mismatches = 0;
  uint32_t last_fresh = 0;  // the last mismatch: the game's sum and the cached one
  uint32_t last_cached = 0;
};

// The two stubs of Cached Particle Count and the trampoline into the game's body, assembled at the cursor: `epoch_site` and `count_site` the game's code, the stubs' entries
// out. The stubs run on the render thread only: the node's update (the epoch: two call sites, one call per update, none inside a render) and the effects render (the sum:
// called from render paths only), so the cache's words need no atomics.
inline void BuildParticleCount(effect_update::internal::Assembler* assembler, CountData* data, const uint8_t* epoch_site, const uint8_t* count_site, uint8_t** epoch_stub,
                               uint8_t** count_stub) {
  const auto field = [](const uint32_t* word) { return effect_update::internal::Address(word); };
  *epoch_stub = assembler->Cursor();
  assembler->BytesAndOperand({0xFF, 0x05}, field(&data->generation));  // inc dword ptr [generation]
  assembler->Copy(epoch_site, PARTICLE_EPOCH_SIZE);                    // sub esp, 0xBC
  assembler->Relative(0xE9, epoch_site + PARTICLE_EPOCH_SIZE);
  const uint8_t* const original = assembler->Cursor();
  assembler->Copy(count_site, PARTICLE_COUNT_HEAD);
  assembler->Relative(0xE9, count_site + PARTICLE_COUNT_HEAD);
  *count_stub = assembler->Cursor();
  assembler->BytesAndOperand({0x3B, 0x0D}, field(&data->node));  // cmp ecx, [node]
  uint8_t* const wrong_node = assembler->Branch(0x85);           // jne miss
  assembler->BytesAndOperand({0xA1}, field(&data->generation));  // mov eax, [generation]
  assembler->BytesAndOperand({0x3B, 0x05}, field(&data->cached_generation));
  uint8_t* const wrong_generation = assembler->Branch(0x85);
  assembler->BytesAndOperand({0x83, 0x3D}, field(&data->check));  // cmp dword ptr [check], 0
  assembler->Bytes({0x00});
  uint8_t* const checking = assembler->Branch(0x85);
  assembler->BytesAndOperand({0xFF, 0x05}, field(&data->hits));
  assembler->BytesAndOperand({0xA1}, field(&data->value));  // mov eax, [value]
  assembler->Bytes({0xC3});
  assembler->Bind(checking);
  assembler->BytesAndOperand({0xFF, 0x35}, field(&data->value));  // push dword ptr [value]
  assembler->Bytes({0x51});                                       // push ecx
  assembler->Relative(0xE8, original);
  assembler->Bytes({0x59, 0x5A, 0x3B, 0xC2});  // pop ecx; pop edx (the cached sum); cmp eax, edx
  uint8_t* const same = assembler->Branch(0x84);
  assembler->BytesAndOperand({0xFF, 0x05}, field(&data->mismatches));
  assembler->BytesAndOperand({0xA3}, field(&data->last_fresh));
  assembler->BytesAndOperand({0x89, 0x15}, field(&data->last_cached));
  assembler->Bind(same);
  assembler->BytesAndOperand({0xFF, 0x05}, field(&data->hits));
  assembler->Bytes({0xC3});
  assembler->Bind(wrong_node);
  assembler->Bind(wrong_generation);
  assembler->Bytes({0x51});  // push ecx
  assembler->Relative(0xE8, original);
  assembler->Bytes({0x59});  // pop ecx
  assembler->BytesAndOperand({0x89, 0x0D}, field(&data->node));
  assembler->BytesAndOperand({0x8B, 0x15}, field(&data->generation));  // mov edx, [generation]
  assembler->BytesAndOperand({0x89, 0x15}, field(&data->cached_generation));
  assembler->BytesAndOperand({0xA3}, field(&data->value));
  assembler->BytesAndOperand({0xFF, 0x05}, field(&data->misses));
  assembler->Bytes({0xC3});
}

// The words the cut stubs use, on the allocation's data page (read-write, never executable), a cache line for the
// count and one for the slots.
struct WorkerData {
  alignas(64) std::atomic<int32_t> cut_active = 0;  // +0x00: the threads in a cut stub past its first test
  alignas(64) std::atomic<uint32_t> fill_slot = 0;   // +0x40: animation_rows::Dispatch while on, else 0
  std::atomic<uint32_t> sweep_slot = 0;              // +0x44: pending_sweep::Arm while on, else 0
};
static_assert(offsetof(WorkerData, fill_slot) == 0x40 && offsetof(WorkerData, sweep_slot) == 0x44 && sizeof(WorkerData) <= 0x80);
constexpr size_t COUNT_DATA_AT = 0x80;  // Cached Particle Count's words, in the data page after WorkerData

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
  uint32_t soldier_rows_rva;   // the soldier draw's matrix rows (soldier_rows' first byte)
  std::string_view soldier_rows;
  std::array<size_t, 3> soldier_rows_at;  // the three reloads in soldier_rows
  uint32_t matrix_product_rva;  // the matrix product (MATRIX_PRODUCT's first byte)
  uint32_t entry_fetch_rva;     // the particle writer's top (ENTRY_FETCH's first byte)
  uint32_t push_loop_a_rva;     // the render list builders' item push loops (ITEM_PUSH_A's / _B's first byte)
  uint32_t push_loop_b_rva;
  uint32_t sweep_rva;           // the pending list sweep (SWEEP's first byte)
  uint32_t pick_lod_rva;        // the LOD pick (PICK_LOD's first byte)
  uint32_t fill_call_rva;       // the animation update's call of the animation texture fill: FILL_CALL_CONTEXT's first byte (rva 0: not looked for)
  uint32_t fill_rva;            // the fill (FILL_HEAD's first byte): the call's target
  uint32_t particle_count_rva;  // the visible particle sum (PARTICLE_COUNT's first byte; rva 0: not looked for)
  uint32_t particle_epoch_rva;  // the node's update (PARTICLE_EPOCH's first byte)
  uint32_t exit_destroy_rva;    // the function that deletes the app at the game's end (EXIT_DESTROY's first byte; rva 0: not looked for)
  uint32_t exit_teardown_rva;   // the outer loop's call of the root teardown (EXIT_TEARDOWN_CALLER's first byte; rva 0: not looked for)
  uint32_t job_pool_rva;        // the job system's accessor (JOB_POOL_ACCESSOR's first byte)
};
constexpr Build ATTILA_BUILD = {
    .truncation_rva = TRUNCATION_RVA,
    .truncation = TRUNCATION,
    .truncation_stub = TRUNCATION_STUB,
    .half_rva = HALF_RVA,
    .fmod_rva = FMOD_RVA,
    .fmod_matherr = FMOD_MATHERR,
    .list_push_rva = LIST_PUSH_RVA,
    .ready_check_rva = READY_CHECK_RVA,
    .job_wake_all_rva = 0x16b5550,
    .job_sleep_rva = 0x16b5520,
    .job_worker_destructor_rva = 0x1689b90,
    .half_rows_rva = 0x1907511,
    .child_sum_rva = 0x1890b90,
    .float4_reload_rva = 0x18967db,
    .particle_loop_rva = 0x1893820,
    .emitter_loop_rva = 0x1893330,
    .soldier_rows_rva = 0x17f689d,
    .soldier_rows = SOLDIER_ROWS,
    .soldier_rows_at = SOLDIER_ROWS_AT,
    .matrix_product_rva = 0x168d570,
    .entry_fetch_rva = 0x18960d0,
    .push_loop_a_rva = 0x17b8483,
    .push_loop_b_rva = 0x17b7ca3,
    .sweep_rva = 0x1893290,
    .pick_lod_rva = 0x183d570,
    .fill_call_rva = 0x1893d14,
    .fill_rva = 0x182e080,
    .particle_count_rva = PARTICLE_COUNT_RVA,
    .particle_epoch_rva = PARTICLE_EPOCH_RVA,
    .exit_destroy_rva = 0xc9a90,
    .exit_teardown_rva = 0xd5d5b,
    .job_pool_rva = 0x169d350,
};
// Rome II: no such job system (see 6.). Thrones of Britannia: the same site is not proven there (no function of the +0xc9a90 shape, no job system accessor of the shape
// JOB_POOL_ACCESSOR has; the functions with the bytes of Attila's +0x1374230 are +0x173c970, with no E8 caller, and +0x17ae260, called from
// a shutdown routine of another shape). Its rvas stay 0: left alone.
constexpr Build THRONES_BUILD = {
    .truncation_rva = THRONES_TRUNCATION_RVA,
    .truncation = THRONES_TRUNCATION,
    .truncation_stub = THRONES_TRUNCATION_STUB,
    .half_rva = 0x1707eb0,
    .fmod_rva = 0x1b0a030,
    .fmod_matherr = THRONES_FMOD_MATHERR,
    .list_push_rva = 0x17df0a9,
    .ready_check_rva = 0x18d8400,
    .job_wake_all_rva = 0x1722e60,
    .job_sleep_rva = 0x1722e30,
    .job_worker_destructor_rva = 0x16f7460,
    .half_rows_rva = 0x197d841,
    .child_sum_rva = 0x1905ef0,
    .float4_reload_rva = 0x190bb3b,
    .particle_loop_rva = 0x1908b80,
    .emitter_loop_rva = 0x1908690,
    .soldier_rows_rva = 0x186ade2,
    .soldier_rows = SOLDIER_ROWS,
    .soldier_rows_at = SOLDIER_ROWS_AT,
    .matrix_product_rva = 0x16fac90,
    .entry_fetch_rva = 0x190b430,
    .push_loop_a_rva = 0x182c213,
    .push_loop_b_rva = 0x182ba33,
    .sweep_rva = 0x19085f0,
    .pick_lod_rva = 0x18b2060,
};
// Rome II has none of the list push, ready check, job, half rows, child sum, float4 reload, effect loop and item push loop
// sites: their rvas stay 0.
constexpr Build ROME2_BUILD = {
    .truncation_rva = 0x14b433c,
    .truncation = ROME2_TRUNCATION,
    .truncation_stub = ROME2_TRUNCATION_STUB,
    .half_rva = 0x1368030,
    .fmod_rva = 0x169ca30,
    .fmod_matherr = THRONES_FMOD_MATHERR,
    .soldier_rows_rva = 0x14b46e5,
    .soldier_rows = ROME2_SOLDIER_ROWS,
    .soldier_rows_at = ROME2_SOLDIER_ROWS_AT,
    .matrix_product_rva = 0x135b530,
    .pick_lod_rva = 0x14f0710,
};
constexpr std::array<Build, 3> BUILDS = {ATTILA_BUILD, THRONES_BUILD, ROME2_BUILD};  // by Game

// Where each stub sits in the page.
constexpr size_t TRUNCATION_PAGE = 0x000;
constexpr size_t HALF_PAGE = 0x040;
constexpr size_t FMOD_PAGE = 0x180;
constexpr size_t LIST_PUSH_PAGE = 0x240;
constexpr size_t READY_CHECK_PAGE = 0x280;
constexpr size_t JOB_PAGE = 0x300;
constexpr size_t PARTICLE_COUNT_PAGE = 0x420;  // Cached Particle Count's two stubs and the trampoline of 28.
constexpr size_t PARTICLE_COUNT_SIZE = 0xE0;
constexpr size_t HALF_ROWS_PAGE = 0x500;
constexpr size_t FLOAT4_RELOAD_PAGE = 0xB20;    // one stub for each of the two reloads, a stride apart
constexpr size_t FLOAT4_RELOAD_STRIDE = 0x30;
constexpr size_t PARTICLE_PREFETCH_PAGE = 0xB80;
constexpr size_t EMITTER_PREFETCH_PAGE = 0xBC0;  // 0x40 bytes to the next stub
constexpr size_t CHILD_SUM_PAGE = 0xC00;
constexpr size_t CHILD_SUM_SLOT = 0x40;       // the child sum's stub and room for it to grow
constexpr size_t SOLDIER_ROWS_PAGE = 0xC40;   // one stub for each of the three reloads, FLOAT4_RELOAD_STRIDE apart
constexpr size_t MATRIX_PRODUCT_PAGE = 0xCD0;  // the same for the product's three
constexpr size_t ENTRY_PREFETCH_PAGE = 0xD60;
constexpr size_t ITEM_PUSH_PAGE = 0xD80;  // the guard and the two stubs of 15.
constexpr size_t SWEEP_PAGE = 0xEF0;      // the sweep's stub of 16.
constexpr size_t MODEL_DETAIL_PAGE = 0xF40;  // the LOD pick's stub of 18.
constexpr size_t WORKER_FILL_PAGE = 0xF80;   // the animation fill call's stub of 19.
constexpr size_t WORKER_SWEEP_PAGE = 0xFD0;  // the pending sweep entry's stub of 20.
constexpr size_t PAGE_SIZE = 0x1000;
static_assert(TRUNCATION_PAGE + TRUNCATION_STUB.size() <= HALF_PAGE  // the three games' truncation stubs are one type
              && HALF_PAGE + HALF_STUB.size() + HALF_SIZE <= FMOD_PAGE
              && FMOD_PAGE + FMOD_STUB.size() <= LIST_PUSH_PAGE
              && LIST_PUSH_PAGE + LIST_PUSH_STUB.size() <= READY_CHECK_PAGE
              && READY_CHECK_PAGE + READY_CHECK_STUB.size() <= JOB_PAGE
              && JOB_PAGE + JOB_STUB.size() <= PARTICLE_COUNT_PAGE
              && PARTICLE_COUNT_PAGE + PARTICLE_COUNT_SIZE <= HALF_ROWS_PAGE
              && HALF_ROWS_PAGE + HALF_ROWS_CONSTANTS_AT + sizeof(HALF_ROWS_CONSTANTS) <= FLOAT4_RELOAD_PAGE
              && FLOAT4_RELOAD_STUB.size() <= FLOAT4_RELOAD_STRIDE
              && FLOAT4_RELOAD_PAGE + FLOAT4_RELOAD_AT.size() * FLOAT4_RELOAD_STRIDE <= PARTICLE_PREFETCH_PAGE
              && PARTICLE_PREFETCH_PAGE + PARTICLE_PREFETCH_STUB.size() <= EMITTER_PREFETCH_PAGE
              && EMITTER_PREFETCH_PAGE + EMITTER_PREFETCH_STUB.size() <= CHILD_SUM_PAGE
              && CHILD_SUM_STUB.size() <= CHILD_SUM_SLOT
              && CHILD_SUM_PAGE + CHILD_SUM_SLOT <= SOLDIER_ROWS_PAGE
              && SOLDIER_ROWS_STUBS[0].size() <= FLOAT4_RELOAD_STRIDE
              && SOLDIER_ROWS_PAGE + SOLDIER_ROWS_STUBS.size() * FLOAT4_RELOAD_STRIDE <= MATRIX_PRODUCT_PAGE
              && MATRIX_PRODUCT_STUBS[0].size() <= FLOAT4_RELOAD_STRIDE
              && MATRIX_PRODUCT_PAGE + MATRIX_PRODUCT_STUBS.size() * FLOAT4_RELOAD_STRIDE <= ENTRY_PREFETCH_PAGE
              && ENTRY_PREFETCH_PAGE + ENTRY_FETCH_STUB.size() <= ITEM_PUSH_PAGE
              && ITEM_PUSH_PAGE + ITEM_PUSH_STUB.size() <= SWEEP_PAGE
              && SWEEP_PAGE + SWEEP_STUB.size() <= MODEL_DETAIL_PAGE
              && MODEL_DETAIL_PAGE + MODEL_DETAIL_STUB.size() <= WORKER_FILL_PAGE
              && WORKER_FILL_PAGE + FILL_CALL_STUB.size() <= WORKER_SWEEP_PAGE
              && WORKER_SWEEP_PAGE + SWEEP_ENTRY_STUB.size() <= PAGE_SIZE);
// The stubs' memory: [code 0][code 1][data]. The stubs above are all on code page 0 (the worker sites' cut ends with it); the data page
// (WorkerData, CountData) starts at `page + 2 * PAGE_SIZE`.
constexpr size_t STUB_CODE_SIZE = 2 * PAGE_SIZE;
static_assert(COUNT_DATA_AT + sizeof(CountData) <= PAGE_SIZE);

// A mask of the sites below (bits by `sites` index). The effect update's and the fill's own tables and the probe's `installed` keep 32 bits.
using SiteMask = uint64_t;
// Which sites each row writes.
constexpr size_t FLOAT4_RELOAD_FIRST_SITE = 10;  // the colour's reload; the vector's is the next
constexpr SiteMask FLOAT4_RELOAD_SITES = SiteMask{0b11} << FLOAT4_RELOAD_FIRST_SITE;  // the particle writer's two reloads
static_assert(FLOAT4_RELOAD_AT.size() == 2);
constexpr size_t SOLDIER_ROWS_FIRST_SITE = 14;  // the soldier draw's first row reload; the second and third are the next
constexpr SiteMask SOLDIER_ROWS_SITES = SiteMask{0b111} << SOLDIER_ROWS_FIRST_SITE;
constexpr size_t MATRIX_PRODUCT_FIRST_SITE = 17;  // the matrix product's first row reload; the second and third are the next
constexpr SiteMask MATRIX_PRODUCT_SITES = SiteMask{0b111} << MATRIX_PRODUCT_FIRST_SITE;
constexpr size_t ENTRY_PREFETCH_INDEX = 20;  // the particle writer's table entry prefetch (after the matrix product's rows)
constexpr size_t ITEM_PUSH_FIRST_SITE = 21;  // the render list builders' item push loop A; B is the next
constexpr SiteMask ITEM_PUSH_SITES = SiteMask{0b11} << ITEM_PUSH_FIRST_SITE;
constexpr size_t SWEEP_PREFETCH_INDEX = 23;  // the pending list sweep's chain prefetch (after the item push loops)
constexpr SiteMask SWEEP_PREFETCH_BIT = SiteMask{1} << SWEEP_PREFETCH_INDEX;  // Faster Sweep Prefetch: its own row
constexpr size_t MODEL_DETAIL_INDEX = 24;  // the LOD pick's scale (after the sweep prefetch)
constexpr SiteMask MODEL_DETAIL_BIT = SiteMask{1} << MODEL_DETAIL_INDEX;  // Model Detail Distance: its own row, on while the slider is above 1x
constexpr size_t WORKER_FILL_INDEX = 25;   // the animation update's call of the animation texture fill (after the LOD pick)
constexpr size_t WORKER_SWEEP_INDEX = 26;  // the pending list sweep's entry
constexpr size_t PARTICLE_EPOCH_INDEX = 27;  // the node's update (Cached Particle Count: the generation's bump)
constexpr size_t PARTICLE_COUNT_INDEX = 28;  // the visible particle sum (Cached Particle Count: the cache)
constexpr size_t EXIT_DRAIN_INDEX = 29;      // the app destruction's call at the game's end (Exit Job Drain, 21.)
constexpr size_t EXIT_TEARDOWN_INDEX = 30;   // the outer loop's call of the root teardown at the game's end (Exit Job Drain, 21.)
constexpr SiteMask EXIT_DRAIN_BIT = SiteMask{1} << EXIT_DRAIN_INDEX;
constexpr SiteMask EXIT_TEARDOWN_BIT = SiteMask{1} << EXIT_TEARDOWN_INDEX;
constexpr SiteMask EXIT_DRAIN_SITES = EXIT_DRAIN_BIT | EXIT_TEARDOWN_BIT;  // a bug fix: asked for by the add-on (WorkerRows::exit_job_drain) unless RENODX_TW_EXIT_DRAIN=0, no row; each site on its own
constexpr SiteMask PARTICLE_EPOCH_BIT = SiteMask{1} << PARTICLE_EPOCH_INDEX;
constexpr SiteMask PARTICLE_COUNT_BIT = SiteMask{1} << PARTICLE_COUNT_INDEX;
constexpr SiteMask PARTICLE_COUNT_SITES = PARTICLE_EPOCH_BIT | PARTICLE_COUNT_BIT;  // Cached Particle Count: its own row, both sites or neither
constexpr SiteMask WORKER_FILL_BIT = SiteMask{1} << WORKER_FILL_INDEX;    // Animation Rows on Workers: its own row
constexpr SiteMask WORKER_SWEEP_BIT = SiteMask{1} << WORKER_SWEEP_INDEX;  // Pending Sweep Trace: its own row, a bench's
constexpr SiteMask WORKER_SITES = WORKER_FILL_BIT | WORKER_SWEEP_BIT;  // the cut sites: they come out only under PausedWrite's cut rule
constexpr SiteMask CPU_CODE_SITES = 0b100010111 | FLOAT4_RELOAD_SITES | SOLDIER_ROWS_SITES | MATRIX_PRODUCT_SITES | ITEM_PUSH_SITES;  // Faster Game CPU Code
constexpr SiteMask LIST_SITES = 0b01000;      // Faster Render Lists
constexpr SiteMask JOB_SITES = 0b11100000;    // Faster Job Wake-Ups: WakeAll, Sleep, the destructor's call of Stop
constexpr size_t CHILD_SUM_INDEX = 9;  // the effect tree's child sum
constexpr size_t PARTICLE_PREFETCH_INDEX = 12;  // the particle update loop's prefetch
constexpr size_t EMITTER_PREFETCH_INDEX = 13;   // the emitter update loop's prefetch
constexpr SiteMask CHILD_SUM_BIT = SiteMask{1} << CHILD_SUM_INDEX;
constexpr SiteMask PARTICLE_PREFETCH_BIT = SiteMask{1} << PARTICLE_PREFETCH_INDEX;
constexpr SiteMask EMITTER_PREFETCH_BIT = SiteMask{1} << EMITTER_PREFETCH_INDEX;  // Faster Emitter Prefetch: its own row
constexpr SiteMask ENTRY_PREFETCH_BIT = SiteMask{1} << ENTRY_PREFETCH_INDEX;      // Faster Particle Prefetch: its own row
constexpr SiteMask PREFETCH_SITES = CHILD_SUM_BIT | PARTICLE_PREFETCH_BIT;  // Faster Effect Prefetch: the child sum, the particle loop
constexpr SiteMask ALL_PREFETCH_SITES = PREFETCH_SITES | EMITTER_PREFETCH_BIT | ENTRY_PREFETCH_BIT | SWEEP_PREFETCH_BIT;  // every prefetch row's sites
constexpr SiteMask NAMED_ONCE_SITES = ALL_PREFETCH_SITES | WORKER_SITES | PARTICLE_COUNT_SITES | EXIT_DRAIN_SITES;  // the sites named once by their row when asked for and not found
constexpr SiteMask MODULE_CALL_SITES = WORKER_SITES | EXIT_DRAIN_SITES;  // the sites whose stubs or call lead into this module: one that cannot come out pins it
constexpr SiteMask HALF_FUNCTION_BIT = 0b10;  // the half function, which the half rows' stub calls as the game does
constexpr SiteMask HALF_ROWS_BIT = 0b100000000;
// The rows by name, for the log lines.
constexpr std::array<std::pair<SiteMask, std::string_view>, 12> ROWS = {{{CPU_CODE_SITES, "Faster Game CPU Code"},
                                                                         {LIST_SITES, "Faster Render Lists"},
                                                                         {JOB_SITES, "Faster Job Wake-Ups"},
                                                                         {PREFETCH_SITES, "Faster Effect Prefetch"},
                                                                         {EMITTER_PREFETCH_BIT, "Faster Emitter Prefetch"},
                                                                         {ENTRY_PREFETCH_BIT, "Faster Particle Prefetch"},
                                                                         {SWEEP_PREFETCH_BIT, "Faster Sweep Prefetch"},
                                                                         {MODEL_DETAIL_BIT, "Model Detail Distance"},
                                                                         {WORKER_FILL_BIT, "Animation Rows on Workers"},
                                                                         {WORKER_SWEEP_BIT, "Pending Sweep Trace"},
                                                                         {PARTICLE_COUNT_SITES, "Cached Particle Count"},
                                                                         {EXIT_DRAIN_SITES, "Exit Job Drain"}}};

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

static std::array<Site, 31> sites = {{{.name = "soldier draw rounding"},
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
                                     {.name = "emitter prefetch"},
                                     {.name = "soldier matrix row 1"},
                                     {.name = "soldier matrix row 2"},
                                     {.name = "soldier matrix row 3"},
                                     {.name = "matrix product row 1"},
                                     {.name = "matrix product row 2"},
                                     {.name = "matrix product row 3"},
                                     {.name = "particle entry prefetch"},
                                     {.name = "item push loop 1"},
                                     {.name = "item push loop 2"},
                                     {.name = "sweep chain prefetch"},
                                     {.name = "model detail"},
                                     {.name = "animation fill call", .opcode = 0xE8},
                                     {.name = "pending sweep entry"},
                                     {.name = "particle count epoch"},
                                     {.name = "particle count"},
                                     {.name = "exit job drain", .opcode = 0xE8},
                                     {.name = "exit teardown drain", .opcode = 0xE8}}};
// The reload rows' sites follow each other; every site has a bit of the SiteMask masks.
static_assert(SOLDIER_ROWS_FIRST_SITE + SOLDIER_ROWS_STUBS.size() == MATRIX_PRODUCT_FIRST_SITE
              && MATRIX_PRODUCT_FIRST_SITE + MATRIX_PRODUCT_STUBS.size() == ENTRY_PREFETCH_INDEX
              && ENTRY_PREFETCH_INDEX + 1 == ITEM_PUSH_FIRST_SITE && ITEM_PUSH_FIRST_SITE + 2 == SWEEP_PREFETCH_INDEX
              && SWEEP_PREFETCH_INDEX + 1 == MODEL_DETAIL_INDEX && MODEL_DETAIL_INDEX + 1 == WORKER_FILL_INDEX
              && WORKER_FILL_INDEX + 1 == WORKER_SWEEP_INDEX && WORKER_SWEEP_INDEX + 1 == PARTICLE_EPOCH_INDEX
              && PARTICLE_EPOCH_INDEX + 1 == PARTICLE_COUNT_INDEX && PARTICLE_COUNT_INDEX + 1 == EXIT_DRAIN_INDEX
              && EXIT_DRAIN_INDEX + 1 == EXIT_TEARDOWN_INDEX && EXIT_TEARDOWN_INDEX + 1 == std::tuple_size_v<decltype(sites)>
              && std::tuple_size_v<decltype(sites)> <= sizeof(SiteMask) * 8);
// Every site belongs to exactly one row: the rows' masks are disjoint and cover every site.
static_assert([] {
  SiteMask covered = 0;
  for (const auto& [row_sites, name] : ROWS) {
    if ((covered & row_sites) != 0) return false;
    covered |= row_sites;
  }
  // The low `size` bits, without a shift by the mask's whole width at a full table.
  return covered == ~SiteMask{0} >> (sizeof(SiteMask) * 8 - std::tuple_size_v<decltype(sites)>);
}());
// The loop stubs of the effect update (8., 10., 11.) go on at the instruction after their 5-byte site.
static_assert(CHILD_SUM_RESUME == CHILD_SUM_SITE + SITE_SIZE && PARTICLE_LOOP_EXIT == PARTICLE_LOOP_SITE + SITE_SIZE);
// A page's cut stubs (a stub that calls into this module or into other code that may run long): a site of a cut comes
// out only while `*active` is 0 and no thread is between `begin` and `end` (PausedWrite).
struct CutRange {
  const uint8_t* begin = nullptr;
  const uint8_t* end = nullptr;
  const std::atomic<int32_t>* active = nullptr;
};
// 21. Exit Job Drain (a bug fix: always on, no row; the environment RENODX_TW_EXIT_DRAIN=0 takes both sites out; Attila). When the frame
// function +0xd4e20 returns 2 (on WM_QUIT, which the window procedure +0xd5db0 posts on WM_DESTROY, before any frame work; or on one of
// the game's own quit checks, after frame work), the outer loop calls the root teardown +0xc9800 at +0xd5d74 (thiscall: the loop object on
// its stack) without waiting for the game's job workers. Before +0xc9ac1 the teardown deletes the context's [+4] (+0xc982e; the context
// is +0xcfc50's object) and [+8], then calls +0xcae90 and +0xcf190 (the list at +0x874, from its end). Battle objects go in this span,
// and a battle update job (one-shot, +0x2f8e90 -> +0x316960, started by a frame) may still be using them on a worker. It faults:
// about 2 % of the exits with the window closed in a battle; in one, the fault came 73 ms before +0xc9ac1 was reached.
//
// Two sites:
// - +0xd5d74, the call of +0xc9800. ExitDrainBeforeTeardown (fastcall with edx unused: the game's thiscall) drains on its first call,
//   then makes the call with the same loop object. Nothing is destroyed yet: this is the drain that closes the race.
// - +0xc9ac1, in +0xc9a90, the destructor of the +0x874 list's token at index 8 (0-based; also destroyed on the frame function's restart
//   return, 1). It is the call of the app delete +0x1374230, which ends the device (a Sleep(100) in its destructor). ExitDrain drains
//   before it on its first call: a backstop for a job started during the teardown, and the restart's only drain. It is not reached
//   while the context's [+0x20] is 0.
// Not patched:
// - the app delete's other E8 caller (+0x137414c, the creation's failure path);
// - +0xc9800's other caller (+0xc9ff3, its deleting destructor);
// - +0xc9aaf, which runs after the same deletes as +0xc9ac1, and only while the context's [+0xc] is set.
//
// A drain waits until every worker of the game's job pool is asleep (state 1, no wake pending: the fields Faster Job Wake-Ups decodes)
// and the pool's queue is empty, in two polls of 1 ms in a row, up to its limit (EXIT_TEARDOWN_DRAIN_LIMIT at +0xd5d74, EXIT_DRAIN_LIMIT
// at +0xc9ac1), and says so in one line. The pool is the job system singleton the accessor (found by its bytes) loads. Its own destroy is
// in the later list at +0x864 (+0xcace0), so it is alive at both sites. Both drains pin this image first: the app delete ends the device,
// ReShade frees the add-on inside it, and the game's calls return into ExitDrain and ExitDrainBeforeTeardown.
//
// A "gave up" line with "job workers still running" and no job running is possible but rare. The game's Wake stores state 2 after its
// release, so a worker that wakes, finds nothing and sleeps again before that store keeps state 2 with no wake pending until its next
// Wake; it costs the full limit. One such line alone is not a stuck job.
constexpr std::string_view EXIT_DESTROY =
    "56 8B F1 C7 06 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 78 20 00 74 ?? E8 ?? ?? ?? ?? 83 78 0C 00 74 ?? E8 ?? ?? ?? ?? 6A 00 8B 48 0C 8B 01 "
    "FF 90 A8 03 00 00 E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? C7 40 20 00 00 00 00 C7 06 ?? ?? ?? ?? 5E C3";
constexpr size_t EXIT_DESTROY_CALL = 49;  // call the app's delete
constexpr uint32_t EXIT_APP_DELETE_RVA = 0x1374230;  // the game's function the site calls (checked at runtime by its bytes)
constexpr std::string_view EXIT_APP_DELETE = "8B 0D ?? ?? ?? ?? 85 C9 74 10 8B 01 6A 01 FF 10 C7 05 ?? ?? ?? ?? 00 00 00 00 C3";
// The outer loop's test of the frame function's return value (2: quit, 1: restart) and its two calls: the restart's (+0xcf190) and the root teardown's.
constexpr std::string_view EXIT_TEARDOWN_CALLER =
    "83 F8 02 74 10 83 F8 01 75 DB 8D 4C 24 7C E8 ?? ?? ?? ?? EB B0 8D 4C 24 7C E8 ?? ?? ?? ?? 8D 4C 24 44";
constexpr size_t EXIT_TEARDOWN_CALL = 25;  // call the root teardown (+0xd5d74)
constexpr uint32_t EXIT_ROOT_TEARDOWN_RVA = 0xc9800;  // the function that call goes to (the heapfix check below and the tests; checked at runtime by its bytes)
constexpr std::string_view EXIT_ROOT_TEARDOWN = "56 8B F1 8B 86 A0 08 00 00 C7 06 ?? ?? ?? ?? 8B 48 24 8B 01 FF 50 68 8B C8 8B 10 FF 52 28 E8";
constexpr std::string_view JOB_POOL_ACCESSOR = "A1 ?? ?? ?? ?? 85 C0 75 ?? 68 FC 10 00 00 E8";  // mov eax, [the pool]; ...; push 0x10fc (its size)
constexpr size_t JOB_POOL_MAX_WORKERS = 64;
// The job pool's first words, and the words of a worker up to [+0x50].
constexpr size_t POOL_WORKER_COUNT = 1;
constexpr size_t POOL_WORKERS = 2;  // the array of the workers
constexpr size_t POOL_QUEUE = 4;    // the head of the queued jobs
constexpr size_t POOL_WORDS = POOL_QUEUE + 1;
constexpr size_t JOB_WORKER_POOL = 1;
constexpr size_t JOB_WORKER_STATE = 6;
constexpr size_t JOB_WORKER_WAKES = 0x14;  // the pending wakes
constexpr size_t JOB_WORKER_WORDS = JOB_WORKER_WAKES + 1;
constexpr uint32_t JOB_WORKER_ASLEEP = 1;
constexpr uint32_t JOB_WORKER_STOPPED = 3;
constexpr std::chrono::milliseconds EXIT_DRAIN_LIMIT{1000};
// +0xd5d74's own cap: about 7 times the heaviest frame measured in battles (about 150 ms); see the bench rule for raising it.
constexpr std::chrono::milliseconds EXIT_TEARDOWN_DRAIN_LIMIT{1000};
static void (*exit_app_delete)() = nullptr;  // the game's function the site called
static void(__thiscall* exit_root_teardown)(void* loop_object) = nullptr;  // the game's function the +0xd5d74 site called
static uintptr_t job_pool_global = 0;        // where the game keeps its job system pointer
static std::atomic<bool> game_exiting = false;
static std::atomic<bool> teardown_drained = false;

// Both sites' drain (21.): waits until every worker of the game's job pool is asleep or stopped and its queue is empty, in two polls of 1 ms in a row, at most
// `limit`, and says so in one line that starts with `drain_name`. It pins this image first: the game's call after it ends the device, ReShade frees the add-on
// inside that call, and the call returns into the stub that called here. The one catch is the boundary of the log lines (strings that can throw bad_alloc); the
// outer loop has no exception handling, and the polls allocate nothing.
static void DrainJobPool(std::string_view drain_name, std::chrono::milliseconds limit) {
  try {
    HMODULE pinned = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&DrainJobPool), &pinned) == FALSE) {
      renodx::utils::log::w("[Total War] ", drain_name, ": could not pin this add-on (error ", GetLastError(), ").");
    }
    const auto start = std::chrono::steady_clock::now();
    const auto read = [](uintptr_t from, void* to, size_t size) {
      return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(from), to, size, nullptr) != FALSE;
    };
    uint32_t pool = 0;
    std::array<uint32_t, POOL_WORDS> pool_words = {};
    std::array<uint32_t, JOB_POOL_MAX_WORKERS> workers = {};
    const bool pointer_read = read(job_pool_global, &pool, sizeof(pool));
    if (pointer_read && pool == 0) {
      renodx::utils::log::i("[Total War] ", drain_name, ": the game built no job system: nothing to wait for.");
    } else if (pointer_read && read(pool, pool_words.data(), sizeof(pool_words)) && pool_words[POOL_WORKER_COUNT] - 1 < JOB_POOL_MAX_WORKERS
               && read(pool_words[POOL_WORKERS], workers.data(), pool_words[POOL_WORKER_COUNT] * sizeof(uint32_t))) {
      const uint32_t worker_count = pool_words[POOL_WORKER_COUNT];
      const auto worker_list = [&](uint64_t mask) {  // the workers of `mask` (bit i: worker i), by index
        std::string list;
        for (uint32_t index = 0; index < worker_count; ++index) {
          if ((mask >> index & 1) != 0) {
            list += std::format("{}{}", (list.empty() ? "" : ", "), index);
          }
        }
        return list;
      };
      uint64_t busy = 0;  // bit i: worker i was not asleep in the last poll
      uint64_t busy_at_first_poll = 0;
      bool queued = false;
      bool as_expected = true;
      uint32_t quiet_polls = 0;
      for (uint32_t polls = 0; quiet_polls < 2 && as_expected; ++polls) {
        busy = 0;
        as_expected = read(pool, pool_words.data(), sizeof(pool_words));
        queued = (pool_words[POOL_QUEUE] != 0);
        for (uint32_t index = 0; index < worker_count && as_expected; ++index) {
          std::array<uint32_t, JOB_WORKER_WORDS> worker = {};
          as_expected = read(workers[index], worker.data(), sizeof(worker)) && worker[JOB_WORKER_POOL] == pool;
          if (as_expected && worker[JOB_WORKER_STATE] != JOB_WORKER_STOPPED && (worker[JOB_WORKER_STATE] != JOB_WORKER_ASLEEP || worker[JOB_WORKER_WAKES] != 0)) {
            busy |= uint64_t{1} << index;
          }
        }
        if (polls == 0) {
          busy_at_first_poll = busy;
        }
        const bool quiet = (busy == 0 && !queued);
        quiet_polls = (quiet ? quiet_polls + 1 : 0);
        if (!quiet && std::chrono::steady_clock::now() - start >= limit) {
          break;
        }
        if (quiet_polls < 2 && as_expected) {
          Sleep(1);
        }
      }
      const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
      if (as_expected && quiet_polls >= 2) {
        renodx::utils::log::i("[Total War] ", drain_name, ": drained ", std::popcount(busy_at_first_poll), " of ", worker_count, " job workers in ", waited, " ms",
                              (busy_at_first_poll != 0 ? std::format(" (busy at the first poll: {})", worker_list(busy_at_first_poll)) : ""), ".");
      } else if (as_expected) {
        const std::string running = worker_list(busy);
        renodx::utils::log::w("[Total War] ", drain_name, ": gave up after ", waited, " ms, job workers still running: ", (running.empty() ? "none" : running), " (of ", worker_count, ")",
                              (queued ? ", jobs still queued." : "."));
      } else {
        renodx::utils::log::w("[Total War] ", drain_name, ": the job workers were not as expected at the game's end (pool 0x", std::format("{:X}", pool), "): not waited for.");
      }
    } else {
      renodx::utils::log::w("[Total War] ", drain_name, ": the job system was not as expected at the game's end (pool 0x", std::format("{:X}", pool), "): not waited for.");
    }
  } catch (...) {
    // No line: a log that could not be built must not stop the game's own call, which follows.
  }
}

// The +0xc9ac1 site's call at the game's end: once, on the game's main thread, the drain; then the game's own call (it reads no register: it loads the app itself).
static void ExitDrain() {
  if (!game_exiting.exchange(true)) {
    DrainJobPool("Exit Job Drain", EXIT_DRAIN_LIMIT);
  }
  exit_app_delete();
}

// The +0xd5d74 site's call: the outer loop calls the root teardown thiscall (ecx = the loop object on its stack, no stack argument). __fastcall with edx unused is the
// same convention (as animation_rows::Dispatch); MSVC rejects __thiscall on a free function.
static void __fastcall ExitDrainBeforeTeardown(void* loop_object, void*) {
  if (!teardown_drained.exchange(true)) {
    DrainJobPool("Exit Job Drain before the teardown", EXIT_TEARDOWN_DRAIN_LIMIT);
  }
  exit_root_teardown(loop_object);
}

static uint8_t* job_table = nullptr;
static std::atomic<uint32_t>* model_detail_scale = nullptr;  // the stub's float (18.), as bits; null: the site isn't there
static uint8_t* page = nullptr;
static WorkerData* worker_data = nullptr;  // the allocation's data page
static CountData* count_data = nullptr;    // Cached Particle Count's words (the data page, COUNT_DATA_AT)
static bool count_mismatch_logged = false;
static uint32_t count_window = 0;  // the pool trace window whose hits and misses are being counted (0: none open)
static uint32_t count_window_hits = 0;
static uint32_t count_window_misses = 0;
static CutRange worker_cut;                // the worker sites' stubs, up to the end of the stubs' page
#if defined(TWATTILA_WORKER_CONTROLS)
// Test switch: the next removal that would wait for a cut's `active` count goes on regardless (once; the control "a cut site removed
// while cut_active > 0").
static std::atomic<bool> bypass_cut_active_once = false;
#endif
static bool module_pinned = false;
static bool prepared = false;
static SiteMask found = 0;    // the sites found (bits by index)
static SiteMask written = 0;  // the sites holding their jump
static SiteMask asked = 0;    // the sites the rows last asked for
static uint32_t failed_writes = 0;
// The thread that last kept PausedWrite from writing, and where it was stopped (0: none was found inside a site).
static DWORD blocked_thread = 0;
static uintptr_t blocked_eip = 0;   // 0 with a thread: its context could not be read
static bool blocked_snapshot = false;  // the last attempt could not read the thread list
static bool snapshot_failure_logged = false;
#if defined(TWATTILA_ENGINE_CODE_CONTROLS)
static bool control_fail_snapshot = false;  // the thread list cannot be read
static bool control_fail_context = false;   // a thread's context cannot be read
static uint32_t control_fail_page = 0;      // Prepare's page: 1 cannot be allocated, 2 cannot be made executable
#endif
static bool job_missing_logged = false;
static SiteMask prefetch_logged = 0;  // the prefetch and worker sites already named as left alone: each is named once
// Threaded Draw Recording's table (draw_recording_sites.hpp, compiled only with TWATTILA_DRAW_RECORDING): the same window writes it
// with the 27 sites above, its masks have 64 bits. Without the feature nothing is ever found in it.
constexpr size_t RECORDING_SITES = 59;
static std::array<Site, RECORDING_SITES> recording_sites = {};
static bool recording_prepared = false;
static uint64_t recording_found = 0;
static uint64_t recording_written = 0;
static uint64_t recording_asked = 0;
static uint8_t* recording_page = nullptr;
// The page's start up to `end` holds the cut stubs: a thread in one is about to dispatch.
static CutRange recording_cut;
// The page's end: with every site coming out, no thread may be anywhere in it (a thunk's direct path runs on every
// flush).
static uint8_t* recording_page_end = nullptr;
static uint32_t* recording_arena_size = nullptr;                 // Shared::arena_size: 0 sends every thunk the direct way
static uint32_t recording_arena_size_value = 0;
// The sites each loop group (draw_recording::Group, the four loop groups) turns on besides the entry hook, and the
// entry hook's bit: a mismatch stands down the group's sites together, the entry hook's every group's.
static std::array<uint64_t, 4> recording_group_sites = {};
static uint64_t recording_entry_bit = 0;
// A turn-on refused for the session (passtime hooked an entry): nothing goes in again, what is written can still come
// out.
static bool recording_refused = false;
// A site stood down on the way out (a page that could not be written back, another mod's bytes over our jump) or
// Uninstall gave up: a thunk can still be reached, so draw_recording keeps its memory and its module for the life of
// the process.
static bool recording_left_in = false;
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
// The n-th (0 first) VirtualProtect that a window makes for the sites going in fails; negative: none.
static int32_t recording_fail_protect = -1;
#endif
// Presents with a thread inside a site before giving up (until the row changes); each try snapshots the system's threads
// (~0.1 s with some antivirus hooks; 3 of 200 writes waited once in the test, four threads running the sites nonstop).
constexpr uint32_t MAX_FAILED_WRITES = 30;
// The bytes of the game's code the Attila CPU Performance Fix patches or checks: every feature that writes into the
// game's code keeps clear of them.
#include "heapfix_ranges.hpp"
// Whether [rva, rva + size) touches a row heapfix patches or a timing hook of it (but not the hook of an entry `own_entry` names), or a row it only checks.
template <typename OwnEntry>
constexpr bool TouchesPatched(uint32_t rva, uint32_t size, OwnEntry own_entry) {
  for (const HeapfixRange& range : HEAPFIX_PATCHED) {
    if (Touches(rva, size, range)) return true;
  }
  for (const HeapfixRange& range : HEAPFIX_TIMING_HOOKS) {
    if (Touches(rva, size, range) && !own_entry(range.rva)) return true;
  }
  return false;
}
constexpr bool TouchesChecked(uint32_t rva, uint32_t size) {
  for (const HeapfixRange& range : HEAPFIX_CHECKED) {
    if (Touches(rva, size, range)) return true;
  }
  return false;
}
// The Parallel Effect Update's sites and Cached Particle Count's clear heapfix's rows (one table for every feature): what a site writes touches no row that heapfix patches
// or checks, and what it reads (the bytes it is found by, a callee's head, the sweep's span, which it jumps into) touches no patched one; ranges that only touch each other are
// clear (Touches is half-open: the epoch's 6 bytes end where heapfix's check row +0x192D296 starts). A timing hook on one of our own entries is refused at run time (the
// effect update's EntriesFree, in Prepare and at every turn-on), so it is not held against the entry.
constexpr bool EffectSitesClearOfHeapfix() {
  namespace eu = effect_update;
  const auto own_entry = [](uint32_t rva) {
    for (const eu::SiteInfo& info : eu::SITES) {
      if (info.rva == rva) return true;
    }
    return rva == PARTICLE_EPOCH_RVA || rva == PARTICLE_COUNT_RVA;
  };
  const auto patched = [&](uint32_t rva, uint32_t size) { return TouchesPatched(rva, size, own_entry); };
  const auto checked = TouchesChecked;
  for (const eu::SiteInfo& info : eu::SITES) {
    const uint32_t size = static_cast<uint32_t>(eu::SizeOf(info));
    if (patched(info.rva, size) || checked(info.rva, size) || patched(info.callee_rva, static_cast<uint32_t>(PatternSize(info.callee_head)))) return false;
  }
  const uint32_t sweep_end = eu::SWEEP_CHECKS[1].rva + static_cast<uint32_t>(PatternSize(eu::SWEEP_CHECKS[1].pattern));
  if (patched(eu::SWEEP_RVA, sweep_end - eu::SWEEP_RVA)) return false;
  for (const eu::ByteCheck& check : eu::SWEEP_CHECKS) {
    if (patched(check.rva, static_cast<uint32_t>(PatternSize(check.pattern)))) return false;
  }
  // Cached Particle Count: the bytes the jump replaces (the epoch's whole instruction) and the bytes each is found by.
  const auto found_site = [&](uint32_t rva, uint32_t size, std::string_view pattern) {
    return !patched(rva, static_cast<uint32_t>(PatternSize(pattern))) && !checked(rva, size);
  };
  if (!found_site(PARTICLE_EPOCH_RVA, PARTICLE_EPOCH_SIZE, PARTICLE_EPOCH) || !found_site(PARTICLE_COUNT_RVA, PARTICLE_COUNT_HEAD, PARTICLE_COUNT)) return false;
  return true;
}
static_assert(EffectSitesClearOfHeapfix());
// Parallel Instance Fill's sites (instance_fill.hpp) the same way: what a site writes touches no patched or checked row, and what it is found by (the
// patterns, the links of each record copy's chain, the record copies' heads) touches no patched one. It has no timing hook of its own.
constexpr bool FillSitesClearOfHeapfix() {
  bool clear = true;
  const auto no_entry = [](uint32_t) { return false; };
  instance_fill::VisitRanges(true, [&](uint32_t rva, uint32_t size) { clear &= !TouchesPatched(rva, size, no_entry) && !TouchesChecked(rva, size); });
  instance_fill::VisitRanges(false, [&](uint32_t rva, uint32_t size) { clear &= !TouchesPatched(rva, size, no_entry); });
  return clear;
}
static_assert(FillSitesClearOfHeapfix());
// Exit Job Drain: its two sites (the 5 bytes each rewrites) clear of every row heapfix patches or checks, and the functions they are found by clear of the patched ones.
static_assert([] {
  const auto no_entry = [](uint32_t) { return false; };
  const uint32_t site = ATTILA_BUILD.exit_destroy_rva + EXIT_DESTROY_CALL;
  const uint32_t teardown_site = ATTILA_BUILD.exit_teardown_rva + EXIT_TEARDOWN_CALL;
  return !TouchesPatched(site, SITE_SIZE, no_entry) && !TouchesChecked(site, SITE_SIZE)
         && !TouchesPatched(teardown_site, SITE_SIZE, no_entry) && !TouchesChecked(teardown_site, SITE_SIZE)
         && !TouchesPatched(ATTILA_BUILD.exit_destroy_rva, static_cast<uint32_t>(PatternSize(EXIT_DESTROY)), no_entry)
         && !TouchesPatched(ATTILA_BUILD.exit_teardown_rva, static_cast<uint32_t>(PatternSize(EXIT_TEARDOWN_CALLER)), no_entry)
         && !TouchesPatched(EXIT_ROOT_TEARDOWN_RVA, static_cast<uint32_t>(PatternSize(EXIT_ROOT_TEARDOWN)), no_entry)
         && !TouchesPatched(EXIT_APP_DELETE_RVA, static_cast<uint32_t>(PatternSize(EXIT_APP_DELETE)), no_entry)
         && !TouchesPatched(ATTILA_BUILD.job_pool_rva, static_cast<uint32_t>(PatternSize(JOB_POOL_ACCESSOR)), no_entry);
}());
// The check row that starts where the epoch's instruction ends is clear of the 6 bytes and touched by a seventh.
static_assert([] {
  for (const HeapfixRange& range : HEAPFIX_CHECKED) {
    if (range.rva == PARTICLE_EPOCH_RVA + PARTICLE_EPOCH_SIZE) {
      return !Touches(PARTICLE_EPOCH_RVA, PARTICLE_EPOCH_SIZE, range) && Touches(PARTICLE_EPOCH_RVA, PARTICLE_EPOCH_SIZE + 1, range);
    }
  }
  return false;
}());
#if defined(TWATTILA_ENGINE_PROBE)
// 17. The probe build (private, compiled only with TWATTILA_ENGINE_PROBE, no row): a histogram stub in front of each probed site's production stub.
#include "engine_probe.hpp"
#endif
#if defined(TWATTILA_DRAW_RECORDING)
#include "draw_recording_sites.hpp"
#endif

// The jumps of the sites in `patched` and the game's bytes in the others, written (where that changes `written`) while
// every other thread of the process is suspended and none stands inside a changing site past its first byte (it would
// go on in the middle of the jump). False: one did (or its position is unknown), nothing written. From the first
// suspension to the last resume nothing allocates, logs or takes a lock: a suspended thread may hold it. A thread
// started after the snapshot isn't held (as with Detours). A site whose bytes are not what our last write left (another
// mod hooked or patched it since Prepare) is not touched: `stood_down` names it, and with it the sites that go in on
// the strength of it (the three job sites work together; the half rows' stub calls the half function as the game does).
// Threaded Draw Recording's sites (`recording_patched` the same way, `recording_stood_down` the same way) are written
// in the same window. A loop group's sites go in whole or not at all (a loop run on a copy of its tracker with one site
// missing would call the game's own flush on a worker): one site's bytes that are not the game's stand the group down,
// the entry hook's the whole feature, and so does a page that cannot be made writable (every page of the sites going in
// is made writable first, and put back if one fails: nothing of them is written). Sites come out only while no dispatch
// is in flight and no thread is about to start one; the last ones to come out also wait for every thread to be out of
// the thunks' page (the arena size, 0 once none is in, sends every thunk the direct way). The worker sites (WORKER_SITES) are
// cut sites too: one comes out only while no call through its stub is in flight and no thread is in the stubs.
// Parallel Instance Fill's sites (`fill_patched` / `fill_stood_down`, instance_fill.hpp) are written in the same window, last: a group of
// them goes in whole or not at all, and comes out only while no thread is inside the dispatcher (the cut on its `active` count) or past the
// first byte of a site. In the private draw recording build, a fold open on a thread other than the caller's (a render thread between a fill site and
// its cut: workers fill the ring and the game's Unmap is noted for its close) refuses every attempt in which the fill's or the recording's sites
// go out, read with every thread stopped; the caller's own fold does not (that thread closes it).
inline bool PausedWrite(SiteMask patched, SiteMask* stood_down, uint64_t recording_patched = 0,
                        uint64_t* recording_stood_down = nullptr, uint32_t effect_patched = 0, uint32_t* effect_stood_down = nullptr,
                        uint32_t fill_patched = 0, uint32_t* fill_stood_down = nullptr) {
  namespace eu = effect_update::internal;
  const std::array<std::span<Site>, 2> tables = {std::span<Site>(sites), std::span<Site>(recording_sites)};
  const std::array<uint64_t, 2> wanted = {patched, recording_patched};
  const std::array<uint64_t, 2> changing = {(patched ^ written) & found,
                                            (recording_patched ^ recording_written) & recording_found};
  const bool recording_out = (changing[1] & ~wanted[1]) != 0;
  // The effect update's sites: their own table (sites of up to 11 bytes), whole or not at all.
  const uint32_t effect_changing = (effect_patched ^ eu::written) & eu::found;
  const bool effect_out = (effect_changing & ~effect_patched) != 0;
  // Parallel Instance Fill's sites: their own table (instance_fill.hpp), whole or not at all; its dispatcher counts the threads in it.
  const uint32_t fill_changing = instance_fill::Changing(fill_patched);
  const bool fill_out = (fill_changing & ~fill_patched) != 0;
  // The cuts with a site coming out. The thunks' direct paths run on every flush of the game, on any thread, and
  // draw_recording frees its memory and releases its module after the last removal: a thread anywhere in the page then
  // is not clear. A partial removal only waits for the cut stubs.
  // The effect update's thunks count the threads in their slow paths (and in the game's calls they make), and any thread
  // anywhere in the page may be about to enter one: the whole page.
  const std::array<std::pair<CutRange, bool>, 4> cuts = {{
      {worker_cut, (changing[0] & ~wanted[0] & WORKER_SITES) != 0},
      {{recording_cut.begin, (recording_patched == 0 ? recording_page_end : recording_cut.end), recording_cut.active}, recording_out},
      {{eu::page, eu::page_end, (eu::data != nullptr ? &eu::data->active : nullptr)}, effect_out},
      {{nullptr, nullptr, &instance_fill::internal::active}, fill_out}}};
  std::array<uint64_t, 2> stood = {};
  uint32_t effect_stood = 0;
  uint32_t fill_stood = 0;
  *stood_down = 0;
  if (effect_stood_down != nullptr) *effect_stood_down = 0;
  if (fill_stood_down != nullptr) *fill_stood_down = 0;
  std::vector<HANDLE> threads;
  std::vector<DWORD> ids;  // the thread ids from the snapshot, beside the handles
  blocked_thread = 0;
  blocked_eip = 0;
  blocked_snapshot = false;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
#if defined(TWATTILA_ENGINE_CODE_CONTROLS)
  if (control_fail_snapshot) {
    if (snapshot != INVALID_HANDLE_VALUE) {
      CloseHandle(snapshot);
    }
    snapshot = INVALID_HANDLE_VALUE;
    SetLastError(ERROR_ACCESS_DENIED);
  }
#endif
  if (snapshot == INVALID_HANDLE_VALUE) {
    blocked_snapshot = true;
    if (!snapshot_failure_logged) {
      snapshot_failure_logged = true;
      renodx::utils::log::w(
          std::format("[Total War] Engine code: the thread list could not be read (error {}); its sites wait.", GetLastError()));
    }
    return false;
  }
  THREADENTRY32 entry = {.dwSize = sizeof(THREADENTRY32)};
  for (BOOL more = Thread32First(snapshot, &entry); more != FALSE; more = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) continue;
    if (HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID); thread != nullptr) {
      threads.push_back(thread);
      ids.push_back(entry.th32ThreadID);
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
      ids[suspended] = ids.back();
      ids.pop_back();
      continue;
    }
    // GetThreadContext also waits until the suspension has taken effect.
    CONTEXT context = {.ContextFlags = CONTEXT_CONTROL};
    clear = (GetThreadContext(threads[suspended], &context) != FALSE);
#if defined(TWATTILA_ENGINE_CODE_CONTROLS)
    if (control_fail_context) {
      clear = false;
      context.Eip = 0;
    }
#endif
    ++suspended;
    for (size_t table = 0; table < tables.size(); ++table) {
      for (size_t index = 0; index < tables[table].size(); ++index) {
        const auto start = reinterpret_cast<uintptr_t>(tables[table][index].at);
        if ((changing[table] >> index & 1) != 0 && context.Eip > start && context.Eip < start + SITE_SIZE) {
          clear = false;
        }
      }
    }
    for (size_t index = 0; index < effect_update::SITE_COUNT; ++index) {
      const auto start = reinterpret_cast<uintptr_t>(eu::sites[index].at);
      if ((effect_changing >> index & 1) != 0 && context.Eip > start && context.Eip < start + eu::sites[index].size) {
        clear = false;
      }
    }
    if (instance_fill::ThreadInside(context.Eip, fill_changing)) {
      clear = false;
    }
    for (const auto& [cut, out] : cuts) {
      if (out && context.Eip >= reinterpret_cast<uintptr_t>(cut.begin) && context.Eip < reinterpret_cast<uintptr_t>(cut.end)) {
        clear = false;
      }
    }
    // Which thread stopped the write, and where (a thread id and a word: nothing here may allocate or log).
    if (!clear) {
      blocked_thread = ids[suspended - 1];
      blocked_eip = context.Eip;
    }
  }
  for (const auto& [cut, out] : cuts) {
    // The stubs raise the count and then read their slot; Update stores the slot (seq_cst) and then PausedWrite loads
    // the count (seq_cst).
    if (clear && out && cut.active != nullptr && cut.active->load() != 0) {
      clear = false;
#if defined(TWATTILA_WORKER_CONTROLS)
      clear = bypass_cut_active_once.exchange(false);
#endif
    }
  }
#if defined(TWATTILA_DRAW_RECORDING)
  // A fold open on another thread (a render thread between a fill site and its cut) has workers filling the ring and the game's Unmap
  // noted for its close: the fill sites and stubs going out would let that thread make the game's own Unmap under the fill, and a cut
  // going out would leave the fold nothing to close it. Read here, with every thread stopped, beside the dispatcher's count above.
  if (clear && (fill_out || recording_out)) {
    const DWORD fold_owner = draw_recording::internal::fold.thread.load();
    clear = (fold_owner == 0 || fold_owner == GetCurrentThreadId());
  }
#endif
  if (clear) {
    for (size_t table = 0; table < tables.size(); ++table) {
      const uint64_t now = (table == 0 ? written : recording_written);
      for (size_t index = 0; index < tables[table].size(); ++index) {
        const Site& site = tables[table][index];
        const auto& expected = ((now >> index & 1) != 0 ? site.jump : site.game);
        if ((changing[table] >> index & 1) != 0 && std::memcmp(site.at, expected.data(), SITE_SIZE) != 0) {
          stood[table] |= uint64_t{1} << index;
        }
      }
    }
    for (size_t index = 0; index < effect_update::SITE_COUNT; ++index) {
      const eu::PatchedSite& site = eu::sites[index];
      const auto& expected = ((eu::written >> index & 1) != 0 ? site.patch : site.game);
      if ((effect_changing >> index & 1) != 0 && std::memcmp(site.at, expected.data(), site.size) != 0) {
        effect_stood |= 1u << index;
      }
    }
    // The sites go in whole or not at all: one site's bytes that are not the game's stand them all down.
    if ((effect_stood & effect_changing & effect_patched) != 0) {
      effect_stood |= effect_changing;
    }
    const std::array<uint64_t, 2> going_in = {changing[0] & wanted[0], changing[1] & wanted[1]};
    if ((stood[0] & JOB_SITES) != 0) stood[0] |= JOB_SITES & going_in[0];
    if ((stood[0] & HALF_FUNCTION_BIT) != 0) stood[0] |= HALF_ROWS_BIT & going_in[0];
    // Cached Particle Count: the epoch and the sum go in and stay in together (the sum alone would answer the first value for the session). One that stands down going in
    // keeps the other out; the sum that stands down going out (another mod's bytes over our jump) keeps the epoch in (its stub needs nothing of the module).
    if ((stood[0] & PARTICLE_COUNT_SITES) != 0) {
      stood[0] |= PARTICLE_COUNT_SITES & going_in[0];
      if ((stood[0] & PARTICLE_COUNT_BIT) != 0) stood[0] |= PARTICLE_EPOCH_BIT & changing[0];
    }
    // The recording sites that go in stand down by group; the entry hook is every group's, so it takes them all, and
    // with no group left it is no use alone.
    const uint64_t entry_in = going_in[1] & recording_entry_bit;
    const auto staying_in = [&]() { return (recording_written & ~changing[1]) | (going_in[1] & ~stood[1]); };
    if ((stood[1] & entry_in) != 0) {
      stood[1] |= going_in[1];
    } else {
      // A group with a site stood down stands down whole, except the sites another group that stays in needs (the
      // per-mesh cut is both B groups'; when the cut itself is the stood site both groups stand down).
      for (bool again = true; again;) {
        again = false;
        for (const uint64_t group_sites : recording_group_sites) {
          if ((stood[1] & going_in[1] & group_sites) == 0) continue;
          uint64_t needed = 0;
          for (const uint64_t other : recording_group_sites) {
            if ((stood[1] & going_in[1] & other) == 0 && (going_in[1] & other & ~group_sites) != 0) {
              needed |= going_in[1] & other;
            }
          }
          const uint64_t standing = going_in[1] & group_sites & ~needed & ~stood[1];
          stood[1] |= standing;
          again |= (standing != 0);
        }
      }
      // (A lone cut with the entry hook may stay written with no group when one B group goes out in the present another
      // goes in with a stood site: harmless, RecordingGroupsIn keeps the dispatcher off it, and both come out with the
      // rows or the stage.)
      if (entry_in != 0 && (staying_in() & ~recording_entry_bit) == 0) {
        stood[1] |= entry_in;
      }
    }
    // Every page of the recording sites going in is made writable before any is written: one that fails puts back the
    // ones made so far and stands the whole table's going-in sites down (a loop with one site missing would run the
    // game's own flush on a worker).
    std::array<DWORD, RECORDING_SITES> recording_protection = {};
    uint64_t made_writable = 0;
    // Last made, first put back: sites on one page see the earlier ones' writable state as their old protection.
    const auto restore_protections = [&]() {
      for (size_t done = RECORDING_SITES; done-- > 0;) {
        DWORD ignored;
        if ((made_writable >> done & 1) != 0) {
          VirtualProtect(recording_sites[done].at, SITE_SIZE, recording_protection[done], &ignored);
        }
      }
    };
    for (size_t index = 0; index < RECORDING_SITES; ++index) {
      if ((going_in[1] & ~stood[1] & (uint64_t{1} << index)) == 0) continue;
#if defined(TWATTILA_DRAW_RECORDING_CONTROLS)
      const bool forced_failure = (recording_fail_protect == 0);
      if (recording_fail_protect >= 0) --recording_fail_protect;
#else
      constexpr bool forced_failure = false;
#endif
      if (!forced_failure && VirtualProtect(recording_sites[index].at, SITE_SIZE, PAGE_EXECUTE_READWRITE,
                                            &recording_protection[index]) != FALSE) {
        made_writable |= uint64_t{1} << index;
        continue;
      }
      restore_protections();
      made_writable = 0;
      stood[1] |= going_in[1];
      break;
    }
    // The arena size is 0 only when no site is left in (a group still in needs its thunks on slots).
    if (recording_arena_size != nullptr) {
      if ((going_in[1] & ~stood[1]) != 0) {
        *recording_arena_size = recording_arena_size_value;
      } else if (recording_out && staying_in() == 0) {
        *recording_arena_size = 0;
      }
    }
    for (size_t table = 0; table < tables.size(); ++table) {
      for (size_t index = 0; index < tables[table].size(); ++index) {
        const Site& site = tables[table][index];
        if (((changing[table] & ~stood[table]) >> index & 1) == 0) continue;
        // The recording sites going in were made writable above, and are put back after the last write.
        const bool prepared_page = (table == 1 && (made_writable >> index & 1) != 0);
        DWORD protection;
        if (!prepared_page && VirtualProtect(site.at, SITE_SIZE, PAGE_EXECUTE_READWRITE, &protection) == FALSE) {
          stood[table] |= uint64_t{1} << index;
          continue;
        }
        const auto& bytes = ((wanted[table] >> index & 1) != 0 ? site.jump : site.game);
        std::memcpy(site.at, bytes.data(), SITE_SIZE);
        if (!prepared_page) VirtualProtect(site.at, SITE_SIZE, protection, &protection);
      }
    }
    // The effect update's sites: every page made writable first, and a failure going in puts back the ones made (none is written).
    std::array<DWORD, effect_update::SITE_COUNT> effect_protection = {};
    uint32_t effect_writable = 0;
    const auto restore_effect_protections = [&]() {
      for (size_t done = effect_update::SITE_COUNT; done-- > 0;) {
        DWORD ignored;
        if ((effect_writable >> done & 1) != 0) {
          VirtualProtect(eu::sites[done].at, eu::sites[done].size, effect_protection[done], &ignored);
        }
      }
    };
    for (size_t index = 0; index < effect_update::SITE_COUNT; ++index) {
      if (((effect_changing & ~effect_stood) >> index & 1) == 0) continue;
      if (VirtualProtect(eu::sites[index].at, eu::sites[index].size, PAGE_EXECUTE_READWRITE, &effect_protection[index]) != FALSE) {
        effect_writable |= 1u << index;
        continue;
      }
      if ((effect_patched >> index & 1) != 0) {
        restore_effect_protections();
        effect_writable = 0;
        effect_stood |= effect_changing;
        break;
      }
      effect_stood |= 1u << index;
    }
    for (size_t index = 0; index < effect_update::SITE_COUNT; ++index) {
      if ((effect_writable >> index & 1) == 0) continue;
      const eu::PatchedSite& site = eu::sites[index];
      const auto& bytes = ((effect_patched >> index & 1) != 0 ? site.patch : site.game);
      std::memcpy(site.at, bytes.data(), site.size);
    }
    restore_effect_protections();
    restore_protections();
    fill_stood = instance_fill::Rewrite(fill_patched);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }
  for (size_t index = 0; index < threads.size(); ++index) {
    if (index < suspended) ResumeThread(threads[index]);
    CloseHandle(threads[index]);
  }
  *stood_down = stood[0];
  if (recording_stood_down != nullptr) *recording_stood_down = stood[1];
  if (effect_stood_down != nullptr) *effect_stood_down = effect_stood;
  if (fill_stood_down != nullptr) *fill_stood_down = fill_stood;
  return clear;
}

// The cut stubs' words for the worker sites in `live` (bits by index: sites that hold their jump and that the rows
// still ask for): the function while live, else 0. A site leaving gets its word zeroed first (a stub that sees 0 runs
// the game's own code), and one going in gets its word after its jump is written; the zeroing is the sequentially
// consistent store the stubs' raise-then-read pairs with (PausedWrite's sequentially consistent load of `cut_active`
// follows it).
inline void SyncWorkerSlots(SiteMask live) {
  if (worker_data == nullptr) return;
  const auto set = [](std::atomic<uint32_t>* slot, bool on, uint32_t function) {
    const uint32_t value = (on ? function : 0);
    if (slot->load(std::memory_order_relaxed) != value) {
      slot->store(value);
    }
  };
  set(&worker_data->fill_slot, (live & WORKER_FILL_BIT) != 0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&animation_rows::Dispatch)));
  set(&worker_data->sweep_slot, (live & WORKER_SWEEP_BIT) != 0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&pending_sweep::Arm)));
}

// A worker or effect update site that did not come out cleanly (a game thread kept running it, or another mod's bytes
// are over our jump and its hook may still lead to our stub) keeps the stubs and this module's Dispatch, Arm and handler
// reachable: the module stays loaded for the life of the process (the pages are never freed either).
inline void PinModule() {
  if (module_pinned) return;
  module_pinned = true;
  HMODULE ignored = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&PinModule), &ignored);
  renodx::utils::log::w(
      "[Total War] Sites that call into this module: one could not be taken out cleanly (a game thread kept running it, or another "
      "mod's hook may still lead to our stub), so the module stays loaded.");
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

  // Three pages: the stubs' two, made read-execute once built, and the cut stubs' words (WorkerData), read-write for good:
  // nothing is ever both.
  page = static_cast<uint8_t*>(VirtualAlloc(nullptr, STUB_CODE_SIZE + PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#if defined(TWATTILA_ENGINE_CODE_CONTROLS)
  if (control_fail_page == 1 && page != nullptr) {
    VirtualFree(page, 0, MEM_RELEASE);
    page = nullptr;
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  }
#endif
  const char* page_failure = "allocated";
  DWORD page_error = (page == nullptr ? GetLastError() : 0);
  worker_data = (page != nullptr ? new (page + STUB_CODE_SIZE) WorkerData : nullptr);
  count_data = (page != nullptr ? new (page + STUB_CODE_SIZE + COUNT_DATA_AT) CountData : nullptr);
  worker_cut = (page != nullptr ? CutRange{page + WORKER_FILL_PAGE, page + PAGE_SIZE, &worker_data->cut_active} : CutRange{});
  std::array<uint8_t*, std::tuple_size_v<decltype(sites)>> stubs = {};
  // The stub `code` of a site (8. to 13.: a loop's bottom, or a float4 reload) at `stub` for the site at `site`: its last
  // jump goes back `resume_size` bytes after the site (the 5-byte site itself, or the game's movups of 5 or 8 bytes).
  const auto place_stub = [&](size_t index, uint8_t* site, size_t resume_size, std::span<const uint8_t> code, uint8_t* stub) {
    std::memcpy(stub, code.data(), code.size());
    write32(stub + code.size() - 4, relative(stub + code.size(), site + resume_size));
    sites[index].at = site;
    stubs[index] = stub;
  };
  // A cut stub (19., 20.): the data page's words as fields, `callee` the game's code its last jump goes to.
  std::array<const uint8_t*, 3> worker_fields = {};
  const auto place_cut_stub = [&](size_t index, uint8_t* site, std::span<const uint8_t> code, uint8_t* stub, const uint8_t* callee) {
    std::memcpy(stub, code.data(), code.size());
    resolve(stub, code.size(), worker_fields, nullptr, std::array<const uint8_t*, 1>{callee});
    sites[index].at = site;
    stubs[index] = stub;
  };
  if (page != nullptr) {
    worker_fields = {reinterpret_cast<const uint8_t*>(&worker_data->cut_active), reinterpret_cast<const uint8_t*>(&worker_data->fill_slot),
                     reinterpret_cast<const uint8_t*>(&worker_data->sweep_slot)};
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
      place_stub(CHILD_SUM_INDEX, match + CHILD_SUM_SITE, SITE_SIZE, CHILD_SUM_STUB, page + CHILD_SUM_PAGE);
    }
    // The particle writer's two reloads (insertps is SSE4.1), found by the function's tail.
    if (uint8_t* match = ((build.float4_reload_rva != 0 && sse4_1) ? find(FLOAT4_RELOAD, build.float4_reload_rva) : nullptr); match != nullptr) {
      for (size_t reload = 0; reload < FLOAT4_RELOAD_AT.size(); ++reload) {
        place_stub(FLOAT4_RELOAD_FIRST_SITE + reload, match + FLOAT4_RELOAD_AT[reload], SITE_SIZE, FLOAT4_RELOAD_STUB,
                   page + FLOAT4_RELOAD_PAGE + reload * FLOAT4_RELOAD_STRIDE);
      }
    }
    // The soldier draw's three matrix row reloads, found by the rows' stores and reloads.
    if (uint8_t* match = ((build.soldier_rows_rva != 0 && sse4_1) ? find(build.soldier_rows, build.soldier_rows_rva) : nullptr);
        match != nullptr) {
      for (size_t row = 0; row < SOLDIER_ROWS_STUBS.size(); ++row) {
        place_stub(SOLDIER_ROWS_FIRST_SITE + row, match + build.soldier_rows_at[row], SOLDIER_ROW_LOAD_SIZE, SOLDIER_ROWS_STUBS[row],
                   page + SOLDIER_ROWS_PAGE + row * FLOAT4_RELOAD_STRIDE);
      }
    }
    // The matrix product's three row reloads, found by the whole function.
    if (uint8_t* match = ((build.matrix_product_rva != 0 && sse4_1) ? find(MATRIX_PRODUCT, build.matrix_product_rva) : nullptr);
        match != nullptr) {
      for (size_t row = 0; row < MATRIX_PRODUCT_STUBS.size(); ++row) {
        place_stub(MATRIX_PRODUCT_FIRST_SITE + row, match + MATRIX_PRODUCT_AT[row], SITE_SIZE, MATRIX_PRODUCT_STUBS[row],
                   page + MATRIX_PRODUCT_PAGE + row * FLOAT4_RELOAD_STRIDE);
      }
    }
    // The particle update loop's prefetch, found by the loop's bytes and those of its way out; the stub's `jb` goes
    // back to the loop's top.
    if (uint8_t* match = (build.particle_loop_rva != 0 ? find(PARTICLE_LOOP, build.particle_loop_rva) : nullptr); match != nullptr) {
      uint8_t* stub = page + PARTICLE_PREFETCH_PAGE;
      place_stub(PARTICLE_PREFETCH_INDEX, match + PARTICLE_LOOP_SITE, SITE_SIZE, PARTICLE_PREFETCH_STUB, stub);
      uint8_t* stub_end = stub + PARTICLE_PREFETCH_STUB.size();
      write32(stub_end - 9, relative(stub_end - 5, match));
    }
    // The emitter update loop's prefetch, found by the whole function's bytes; the stub goes on at the `je` after the site.
    if (uint8_t* match = (build.emitter_loop_rva != 0 ? find(EMITTER_LOOP, build.emitter_loop_rva) : nullptr); match != nullptr) {
      place_stub(EMITTER_PREFETCH_INDEX, match + EMITTER_LOOP_SITE, SITE_SIZE, EMITTER_PREFETCH_STUB, page + EMITTER_PREFETCH_PAGE);
    }
    // The particle writer's table entry prefetch, found by the function's top; the stub goes on at the `je` after the `test`.
    if (uint8_t* match = (build.entry_fetch_rva != 0 ? find(ENTRY_FETCH, build.entry_fetch_rva) : nullptr); match != nullptr) {
      place_stub(ENTRY_PREFETCH_INDEX, match + ENTRY_FETCH_SITE, ENTRY_FETCH_RESUME - ENTRY_FETCH_SITE, ENTRY_FETCH_STUB,
                 page + ENTRY_PREFETCH_PAGE);
    }
    // The render list builders' two item push loops, found by the whole loop's bytes: the guard first, each stub's last jump goes on after the site's two
    // instructions.
    std::memcpy(page + ITEM_PUSH_PAGE, ITEM_PUSH_STUB.data(), ITEM_PUSH_STUB_AT[0]);
    const std::array<std::pair<std::string_view, uint32_t>, 2> push_loops = {{{ITEM_PUSH_A, build.push_loop_a_rva}, {ITEM_PUSH_B, build.push_loop_b_rva}}};
    for (size_t loop = 0; loop < push_loops.size(); ++loop) {
      const auto& [loop_bytes, loop_rva] = push_loops[loop];
      if (uint8_t* match = (loop_rva != 0 ? find(loop_bytes, loop_rva) : nullptr); match != nullptr) {
        place_stub(ITEM_PUSH_FIRST_SITE + loop, match + ITEM_PUSH_SITE, ITEM_PUSH_SIZE,
                   std::span(ITEM_PUSH_STUB).subspan(ITEM_PUSH_STUB_AT[loop], ITEM_PUSH_STUB_AT[loop + 1] - ITEM_PUSH_STUB_AT[loop]),
                   page + ITEM_PUSH_PAGE + ITEM_PUSH_STUB_AT[loop]);
      }
    }
    // The pending list sweep, found by the whole function's bytes; the stub goes on at the call after the site's two instructions.
    if (uint8_t* match = (build.sweep_rva != 0 ? find(SWEEP, build.sweep_rva) : nullptr); match != nullptr) {
      place_stub(SWEEP_PREFETCH_INDEX, match + SWEEP_SITE, SWEEP_SIZE, SWEEP_STUB, page + SWEEP_PAGE);
      place_cut_stub(WORKER_SWEEP_INDEX, match + SWEEP_ENTRY_AT, SWEEP_ENTRY_STUB, page + WORKER_SWEEP_PAGE, match + SWEEP_ENTRY_RESUME);
    }
    // The animation update's call of the animation texture fill, found by the bytes around it: its target must be the
    // build's fill, with the head the stub's own jump (the game's call) expects.
    if (uint8_t* match = (build.fill_call_rva != 0 ? find(FILL_CALL_CONTEXT, build.fill_call_rva) : nullptr); match != nullptr) {
      const uint8_t* fill = target(match + FILL_CALL_AT);
      if (fill == base + build.fill_rva && holds(fill, FILL_HEAD)) {
        place_cut_stub(WORKER_FILL_INDEX, match + FILL_CALL_AT, FILL_CALL_STUB, page + WORKER_FILL_PAGE, fill);
        animation_rows::SetGame(fill);
      }
    }
    // The LOD pick, found by its first bytes and the start of its body; its stub reads the scale from a page of its own (this one is read-execute).
    if (uint8_t* match = find(PICK_LOD, build.pick_lod_rva); match != nullptr) {
      if (void* cell = VirtualAlloc(nullptr, sizeof(uint32_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); cell != nullptr) {
        model_detail_scale = new (cell) std::atomic<uint32_t>(UNSCALED_BITS);
        place_stub(MODEL_DETAIL_INDEX, match, PICK_LOD_RESUME, MODEL_DETAIL_STUB, page + MODEL_DETAIL_PAGE);
        resolve(page + MODEL_DETAIL_PAGE, MODEL_DETAIL_STUB.size(), std::array<const uint8_t*, 1>{static_cast<const uint8_t*>(cell)}, nullptr, {});
      }
    }
    // Cached Particle Count: the sum's entry and the node's update, found by their bytes: both or neither (the cache is only ever invalidated by the epoch's bump).
    if (build.particle_count_rva != 0) {
      uint8_t* const epoch = find(PARTICLE_EPOCH, build.particle_epoch_rva);
      uint8_t* const count = find(PARTICLE_COUNT, build.particle_count_rva);
      if (epoch != nullptr && count != nullptr) {
        effect_update::internal::Assembler assembler(std::span<uint8_t>(page + PARTICLE_COUNT_PAGE, PARTICLE_COUNT_SIZE));
        BuildParticleCount(&assembler, count_data, epoch, count, &stubs[PARTICLE_EPOCH_INDEX], &stubs[PARTICLE_COUNT_INDEX]);
        if (assembler.Fits()) {
          sites[PARTICLE_EPOCH_INDEX].at = epoch;
          sites[PARTICLE_COUNT_INDEX].at = count;
        } else {
          stubs[PARTICLE_EPOCH_INDEX] = stubs[PARTICLE_COUNT_INDEX] = nullptr;
        }
      }
    }
    // Exit Job Drain: the job system's pointer, then each site on its own, as expected or left alone: the app destruction's call with the game's function it calls,
    // and the outer loop's call of the root teardown with the function it calls.
    if (build.exit_destroy_rva != 0 || build.exit_teardown_rva != 0) {
      const uint8_t* pool_accessor = find(JOB_POOL_ACCESSOR, build.job_pool_rva);
      const auto* pool_global = (pool_accessor != nullptr ? reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(read32(pool_accessor + 1))) : nullptr);
      if (pool_global != nullptr && pool_global >= base && pool_global + sizeof(uint32_t) <= image_end) {
        job_pool_global = reinterpret_cast<uintptr_t>(pool_global);
        if (uint8_t* destroy = (build.exit_destroy_rva != 0 ? find(EXIT_DESTROY, build.exit_destroy_rva) : nullptr); destroy != nullptr) {
          const uint8_t* app_delete = target(destroy + EXIT_DESTROY_CALL);
          if (holds(app_delete, EXIT_APP_DELETE)) {
            exit_app_delete = reinterpret_cast<void (*)()>(const_cast<uint8_t*>(app_delete));
            sites[EXIT_DRAIN_INDEX].at = destroy + EXIT_DESTROY_CALL;
            stubs[EXIT_DRAIN_INDEX] = reinterpret_cast<uint8_t*>(&ExitDrain);
          }
        }
        if (uint8_t* caller = (build.exit_teardown_rva != 0 ? find(EXIT_TEARDOWN_CALLER, build.exit_teardown_rva) : nullptr); caller != nullptr) {
          const uint8_t* teardown = target(caller + EXIT_TEARDOWN_CALL);
          if (holds(teardown, EXIT_ROOT_TEARDOWN)) {
            exit_root_teardown = reinterpret_cast<decltype(exit_root_teardown)>(const_cast<uint8_t*>(teardown));
            sites[EXIT_TEARDOWN_INDEX].at = caller + EXIT_TEARDOWN_CALL;
            stubs[EXIT_TEARDOWN_INDEX] = reinterpret_cast<uint8_t*>(&ExitDrainBeforeTeardown);
          }
        }
      }
    }
    DWORD protection;
    bool executable = (VirtualProtect(page, STUB_CODE_SIZE, PAGE_EXECUTE_READ, &protection) != FALSE);
#if defined(TWATTILA_ENGINE_CODE_CONTROLS)
    if (control_fail_page == 2) {
      executable = false;
      SetLastError(ERROR_ACCESS_DENIED);
    }
#endif
    if (!executable) {
      page_failure = "made executable";
      page_error = GetLastError();
      // As if there were no page (nothing is written to the game yet): a site's jump would lead into memory that cannot run.
      for (Site& site : sites) site.at = nullptr;
      stubs = {};
      VirtualFree(page, 0, MEM_RELEASE);
      page = nullptr;
      worker_data = nullptr;
      count_data = nullptr;
      worker_cut = {};
      if (job_table != nullptr) VirtualFree(job_table, 0, MEM_RELEASE);
      job_table = nullptr;
    } else {
      FlushInstructionCache(GetCurrentProcess(), page, STUB_CODE_SIZE);
      device_loss::AddPartRange("B3b", "engine_stubs", page, STUB_CODE_SIZE);
      // The effect update takes an E9 at these two of its entries, to these stubs, as ours.
      effect_update::internal::sibling_entries =
          (sites[PARTICLE_EPOCH_INDEX].at != nullptr && sites[PARTICLE_COUNT_INDEX].at != nullptr
               ? std::array<effect_update::internal::SiblingEntry, 2>{{{PARTICLE_EPOCH_RVA, stubs[PARTICLE_EPOCH_INDEX]}, {PARTICLE_COUNT_RVA, stubs[PARTICLE_COUNT_INDEX]}}}
               : std::array<effect_update::internal::SiblingEntry, 2>{});
    }
  }
  if (page == nullptr) {
    // No page (not allocated, or not made executable): that is the reason, not another mod or build, so no site is
    // named as left alone, here or by Update's lines.
    renodx::utils::log::w("[Total War] Faster Game CPU Code: left alone ",
                          std::format("(the stubs' page could not be {}, error {})", page_failure, page_error));
    job_missing_logged = true;
    prefetch_logged = NAMED_ONCE_SITES;
    return;
  }
  std::string found_names;
  std::string left;
  std::string x87_fixed;
  // Attila's truncation block rewritten by the Attila CPU Performance Fix is named as such, not as an unknown mod.
  const bool truncation_x87_fixed =
      (sites[0].at == nullptr && build.truncation_rva == TRUNCATION_RVA
       && std::ranges::any_of(X87_FIX_TRUNCATIONS, [&](const auto& pattern) {
            return holds(base + TRUNCATION_RVA, {pattern.data(), pattern.size()});
          }));
  // A site the build doesn't have (rva 0) isn't named.
  const uint32_t reload_rva = (sse4_1 ? build.float4_reload_rva : 0);
  const uint32_t rows_rva = (sse4_1 ? build.soldier_rows_rva : 0);
  const uint32_t product_rva = (sse4_1 ? build.matrix_product_rva : 0);
  // The worker sites aren't listed either (like the other sites of their own rows): NAMED_ONCE_SITES names them when
  // their row asks.
  const std::array<uint32_t, 31> rvas = {build.truncation_rva, build.half_rva, build.fmod_rva, build.list_push_rva, build.ready_check_rva,
                                         0, 0, 0, build.half_rows_rva, 0, reload_rva, reload_rva, 0, 0, rows_rva, rows_rva, rows_rva,
                                         product_rva, product_rva, product_rva, 0, build.push_loop_a_rva, build.push_loop_b_rva, 0, 0, 0, 0, 0, 0, 0, 0};
  static_assert(std::tuple_size_v<decltype(rvas)> == std::tuple_size_v<decltype(sites)>);
#if defined(TWATTILA_ENGINE_PROBE)
  // The probe build: each probed site's jump goes to its probe stub first (a copy of the probe's bytes with the counters' addresses put in), whose last jump goes on
  // to the production stub the site would have jumped to; the production stubs stay as they are.
  if (probe::Allocate()) {
    const auto production_stubs = stubs;
    const std::array<const uint8_t*, probe::FIELDS> fields = probe::Fields();
    size_t slot = 0;
    for (const probe::Probe& entry : probe::PROBES) {
      if (sites[entry.site].at == nullptr || stubs[entry.site] == nullptr) continue;
      uint8_t* stub = probe::code + probe::SLOT * slot++;
      std::memcpy(stub, entry.code, entry.size);
      resolve(stub, entry.size, fields, nullptr, {});
      write32(stub + entry.size - 4, relative(stub + entry.size, stubs[entry.site]));
      stubs[entry.site] = stub;
      probe::stub_of[entry.site] = stub;
      probe::size_of[entry.site] = entry.size;
      probe::installed |= 1u << entry.site;
    }
    DWORD protection;
    if (VirtualProtect(probe::code, PAGE_SIZE, PAGE_EXECUTE_READ, &protection) == FALSE) {
      // No probes: the sites go to the production stubs again (a jump into the read-write page would crash the game).
      stubs = production_stubs;
      probe::installed = 0;
      probe::stub_of = {};
      probe::size_of = {};
    } else {
      FlushInstructionCache(GetCurrentProcess(), probe::code, PAGE_SIZE);
    }
  }
#endif
  for (size_t index = 0; index < sites.size(); ++index) {
    Site& site = sites[index];
    // The job sites aren't listed here: their row says when it turns them on.
    if (rvas[index] != 0) {
      std::string* names = &left;
      if (site.at != nullptr) {
        names = &found_names;
      } else if (index == 0 && truncation_x87_fixed) {
        names = &x87_fixed;
      }
      *names += std::format("{}{}", names->empty() ? "" : ", ", site.name);
    }
    if (site.at == nullptr) continue;
    found |= SiteMask{1} << index;
    std::memcpy(site.game.data(), site.at, SITE_SIZE);
    site.jump[0] = site.opcode;
    write32(site.jump.data() + 1, relative(site.at + SITE_SIZE, stubs[index]));
  }
  renodx::utils::log::i("[Total War] Faster Game CPU Code: ", (found_names.empty() ? "nothing" : found_names), " found",
                        (left.empty() ? "" : "; left alone (not the game's code as expected: another mod or build): "), left,
                        (x87_fixed.empty() ? "" : "; left alone (the Attila CPU Performance Fix's x87 fix rewrote it): "), x87_fixed);
}

}  // namespace internal

// Every present: `enabled` (Faster Game CPU Code), `lists` (Faster Render Lists), `jobs` (Faster Job Wake-Ups),
// `prefetch` (Faster Effect Prefetch), `emitter_prefetch` (Faster Emitter Prefetch), `particle_prefetch` (Faster
// Particle Prefetch), `sweep_prefetch` (Faster Sweep Prefetch) and `model_detail` (the Model Detail Distance slider: the
// LOD pick's site is in while it is not 1) once empire.retail.dll is loaded, the first time any is on prepared (`game`'s
// build); then the jumps go in or the game's bytes back, while no thread stands inside them. A slider move writes only the
// stub's float. `draw_recording_groups` (bits of draw_recording::Group; the private TWATTILA_DRAW_RECORDING build, Attila): the
// sites of Threaded Draw Recording's loop groups, written in the same window. `worker_rows`: the worker sites (Attila; WorkerRows).
// A worker site goes in with its jump first and its stub's function word after, and comes out with the word zeroed first (internal::SyncWorkerSlots).
inline void Update(bool enabled, Game game = Game::ATTILA, bool lists = false, bool jobs = false, bool prefetch = false,
                   bool emitter_prefetch = false, bool particle_prefetch = false, bool sweep_prefetch = false, float model_detail = 1.f,
                   [[maybe_unused]] uint32_t draw_recording_groups = 0, const WorkerRows& worker_rows = {}) {
  pending_sweep::Update();
  const internal::SiteMask wanted = (enabled ? internal::CPU_CODE_SITES : 0u) | (lists ? internal::LIST_SITES : 0u) | (jobs ? internal::JOB_SITES : 0u)
                                    | (prefetch ? internal::PREFETCH_SITES : 0u) | (emitter_prefetch ? internal::EMITTER_PREFETCH_BIT : 0u)
                                    | (particle_prefetch ? internal::ENTRY_PREFETCH_BIT : 0u) | (sweep_prefetch ? internal::SWEEP_PREFETCH_BIT : 0u)
                                    | (model_detail != 1.f ? internal::MODEL_DETAIL_BIT : 0u)
                                    | (worker_rows.animation != animation_rows::Stage::OFF ? internal::WORKER_FILL_BIT : 0u)
                                    | (worker_rows.pending_sweep_trace ? internal::WORKER_SWEEP_BIT : 0u)
                                    | (worker_rows.particle_count ? internal::PARTICLE_COUNT_SITES : 0u)
                                    | (worker_rows.exit_job_drain ? internal::EXIT_DRAIN_SITES : 0u);
  if (wanted != 0 && !internal::prepared) {
    HMODULE module = GetModuleHandleW(L"empire.retail.dll");
    if (module == nullptr) return;
    internal::Prepare(module, internal::BUILDS[static_cast<size_t>(game)]);
  }
  namespace eu = effect_update::internal;
  if (worker_rows.effect_update && game == Game::ATTILA && !eu::prepared) {
    if (HMODULE module = GetModuleHandleW(L"empire.retail.dll"); module != nullptr) {
      effect_update::Prepare(module);
    }
  }
  namespace fi = instance_fill::internal;
  if (worker_rows.instance_fill && game == Game::ATTILA && !fi::prepared) {
    if (HMODULE module = GetModuleHandleW(L"empire.retail.dll"); module != nullptr) {
      instance_fill::Prepare(module);
    }
  }
  const uint32_t effect_wanted = (game == Game::ATTILA ? effect_update::Wanted(worker_rows.effect_update) : 0u);
#if defined(TWATTILA_DRAW_RECORDING)
  if (draw_recording_groups != 0 && game == Game::ATTILA && !internal::recording_prepared) {
    if (HMODULE module = GetModuleHandleW(L"empire.retail.dll"); module != nullptr) {
      internal::PrepareRecording(module);
    }
  }
  const uint64_t recording_wanted = (internal::recording_prepared ? internal::RecordingWanted(draw_recording_groups) : 0);
#else
  const uint64_t recording_wanted = 0;
#endif
  const uint64_t recording_target = recording_wanted & internal::recording_found;
  // The fill's Wanted after the recording's target is known: the fold's stubs follow their cut (a stub is asked for only with its
  // cut written before this window and still wanted), all read before the window.
  uint32_t cuts_written = 0;
  uint32_t cuts_target = 0;
#if defined(TWATTILA_DRAW_RECORDING)
  const std::array<uint64_t, 3> fold_cuts = {internal::RecordingBit(internal::RecordingSite::DEPTH_CUT),
                                             internal::RecordingBit(internal::RecordingSite::COLOUR_CUT),
                                             internal::RecordingBit(internal::RecordingSite::DEPTH_B_CUT)};
  for (size_t stub = 0; stub < fold_cuts.size(); ++stub) {
    cuts_written |= ((internal::recording_written & fold_cuts[stub]) != 0 ? 1u : 0u) << stub;
    cuts_target |= ((recording_target & fold_cuts[stub]) != 0 ? 1u : 0u) << stub;
  }
#endif
  const uint32_t fill_wanted =
      (game == Game::ATTILA ? instance_fill::Wanted(worker_rows.instance_fill, worker_rows.fill_fold, cuts_written, cuts_target) : 0u);
  if (internal::count_data != nullptr) {
    internal::count_data->check = (worker_rows.particle_count_check ? 1u : 0u);
    if (internal::count_data->mismatches != 0 && !internal::count_mismatch_logged) {
      internal::count_mismatch_logged = true;
      renodx::utils::log::w("[Total War] Cached Particle Count: the cached sum differed from the game's ", internal::count_data->mismatches, " times (the last: the game's ",
                            internal::count_data->last_fresh, ", the cache's ", internal::count_data->last_cached, ").");
    }
    // One line per pool trace window (at the first present after it): what the cache answered and what it had to count in it.
    if (worker_pool::Tracing()) {
      if (internal::count_window != worker_pool::TraceWindow()) {
        internal::count_window = worker_pool::TraceWindow();
        internal::count_window_hits = internal::count_data->hits;
        internal::count_window_misses = internal::count_data->misses;
      }
    } else if (internal::count_window != 0) {
      const uint32_t hits = internal::count_data->hits - internal::count_window_hits;
      const uint32_t misses = internal::count_data->misses - internal::count_window_misses;
      if (hits + misses != 0) {
        renodx::utils::log::i("[Total War] Cached Particle Count (trace window ", internal::count_window, "): ", hits, " hits, ", misses, " misses.");
      }
      internal::count_window = 0;
    }
  }
  if (internal::model_detail_scale != nullptr) {
    const uint32_t bits = std::bit_cast<uint32_t>(1.f / (model_detail * model_detail));
    if (internal::model_detail_scale->load(std::memory_order_relaxed) != bits) {
      internal::model_detail_scale->store(bits, std::memory_order_relaxed);
    }
  }
#if defined(TWATTILA_ENGINE_PROBE)
  internal::probe::Tick(internal::written);
#endif
  if (wanted != internal::asked || recording_wanted != internal::recording_asked || effect_wanted != eu::asked
      || fill_wanted != fi::asked) {
    internal::asked = wanted;
    internal::recording_asked = recording_wanted;
    eu::asked = effect_wanted;
    fi::asked = fill_wanted;
    internal::failed_writes = 0;
  }
  if (jobs && internal::prepared && (internal::found & internal::JOB_SITES) == 0 && !internal::job_missing_logged) {
    internal::job_missing_logged = true;
    renodx::utils::log::w("[Total War] Faster Job Wake-Ups: left alone (not the game's code as expected: another mod or build)");
  }
  // A prefetch row's sites asked for and not found (another mod's bytes, or another build): each is named once, by its row.
  for (const auto& [row_sites, name] : internal::ROWS) {
    const internal::SiteMask missing = wanted & row_sites & internal::NAMED_ONCE_SITES & ~internal::found & ~internal::prefetch_logged;
    if (!internal::prepared || missing == 0) continue;
    internal::prefetch_logged |= missing;
    std::string names;
    for (size_t index = 0; index < internal::sites.size(); ++index) {
      if ((missing >> index & 1) != 0) names += std::format("{}{}", names.empty() ? "" : ", ", internal::sites[index].name);
    }
    renodx::utils::log::w("[Total War] ", name, ": left alone (not the game's code as expected: another mod or build): ", names);
  }
  // A group of sites that only work together is asked for only while all of them are found: one that stood down (another mod's bytes over our jump on the way out, a page that
  // could not be made writable) leaves the others out too, and one that went in alone comes out. Cached Particle Count (the sum alone would answer the first value for the session)
  // and the job sites (Stop's call keeps the pool's destruction safe).
  internal::SiteMask target = wanted & internal::found;
  for (const internal::SiteMask group : {internal::PARTICLE_COUNT_SITES, internal::JOB_SITES}) {
    if ((internal::found & group) != group) {
      target &= ~group;
    }
  }
  // The half rows call the half function: out of found (another mod's hook over it), they stay out.
  if ((internal::found & internal::HALF_FUNCTION_BIT) == 0) {
    target &= ~internal::HALF_ROWS_BIT;
  }
  internal::SyncWorkerSlots(internal::written & target);  // a site leaving: its word first
  if (effect_wanted == 0 && eu::written != 0) {
    effect_update::Disarm();  // the effect update's sites leaving: no thread enters a slow path from now on
  }
  if ((target == internal::written && recording_target == internal::recording_written && effect_wanted == eu::written
       && fill_wanted == fi::written)
      || internal::failed_writes >= internal::MAX_FAILED_WRITES) {
    return;
  }
  internal::SiteMask stood_down = 0;
  uint64_t recording_stood_down = 0;
  uint32_t effect_stood_down = 0;
  // Cached Particle Count going in: the epoch did not run while the row was off, so what the cache holds is of an older update; no stub runs yet, so a plain bump
  // (a failed write bumps again: harmless).
  if (internal::count_data != nullptr && (target & ~internal::written & internal::PARTICLE_COUNT_SITES) != 0) ++internal::count_data->generation;
  uint32_t fill_stood_down = 0;
  if (internal::PausedWrite(target, &stood_down, recording_target, &recording_stood_down, effect_wanted, &effect_stood_down, fill_wanted,
                            &fill_stood_down)) {
    const internal::SiteMask changing = target ^ internal::written;
    const internal::SiteMask now = (internal::written & ~changing) | (target & changing & ~stood_down);
    const internal::SiteMask changed = now ^ internal::written;
    // A worker site that stood down on its way out (another mod's bytes over our jump) may still chain to our stub.
    if ((stood_down & changing & ~target & internal::written & internal::MODULE_CALL_SITES) != 0) {
      internal::PinModule();
    }
    internal::written = now;
    internal::SyncWorkerSlots(now & target);  // a site that went in: its word now
    // The effect update's sites (all found or none): a site that stood down on its way out may still chain to our thunk.
    const uint32_t effect_changing = (effect_wanted ^ eu::written) & eu::found;
    const uint32_t effect_now = (eu::written & ~effect_changing) | (effect_wanted & effect_changing & ~effect_stood_down);
    if ((effect_stood_down & effect_changing & ~effect_wanted & eu::written) != 0) {
      internal::PinModule();
    }
    if (effect_now != eu::written) {
      renodx::utils::log::i("[Total War] Parallel Effect Update sites: ", (effect_now != 0 ? "in" : "out, the game's own code back"));
    }
    if (effect_stood_down != 0) {
      renodx::utils::log::w("[Total War] Parallel Effect Update sites: left alone (their bytes changed since the start: another mod, ",
                            "or a page that could not be made writable)");
    }
    eu::written = effect_now;
    eu::found &= ~effect_stood_down;
    // The fill's sites (a group whole or not at all): a site that stood down on its way out may still call into this module.
    const uint32_t fill_changing = instance_fill::Changing(fill_wanted);
    const uint32_t fill_now = (fi::written & ~fill_changing) | (fill_wanted & fill_changing & ~fill_stood_down);
    if ((fill_stood_down & fill_changing & ~fill_wanted & fi::written) != 0) {
      internal::PinModule();
    }
    if (fill_now != fi::written) {
      renodx::utils::log::i("[Total War] Parallel Instance Fill sites: ", (fill_now != 0 ? "in" : "out, the game's own code back"));
    }
    if (fill_stood_down != 0) {
      renodx::utils::log::w("[Total War] Parallel Instance Fill sites: left alone (their bytes changed since the start: another mod, ",
                            "or a page that could not be made writable)");
    }
    fi::written = fill_now;
    fi::found &= ~fill_stood_down;
#if defined(TWATTILA_ENGINE_PROBE)
    internal::probe::LogSites(internal::sites, now);
#endif
    internal::found &= ~stood_down;
    internal::prefetch_logged |= stood_down & internal::NAMED_ONCE_SITES;  // named below, once
    const uint64_t recording_changing = recording_target ^ internal::recording_written;
    const uint64_t recording_now = (internal::recording_written & ~recording_changing)
                                   | (recording_target & recording_changing & ~recording_stood_down);
    const uint64_t recording_changed = recording_now ^ internal::recording_written;
    // A site that stood down on its way out is still ours in the game's code (a page that could not be written back) or
    // hooked by another mod, and its thunk can still be reached.
    if ((recording_stood_down & recording_changing & ~recording_target) != 0) {
      internal::recording_left_in = true;
    }
    internal::recording_written = recording_now;
    // The entry hook that stood down GOING IN (its bytes were not the game's) leaves found with the others: a group asked
    // for later then takes its call-site flush fallback (RecordingWanted). One that stood down on the way out is still in
    // the game's code and covered by recording_left_in.
    internal::recording_found &= ~recording_stood_down;
    internal::failed_writes = 0;
    for (const auto& [row_sites, name] : internal::ROWS) {
      if ((changed & row_sites) != 0) renodx::utils::log::i("[Total War] ", name, ": ", ((now & row_sites) != 0 ? "on" : "off, the game's own code back"));
    }
    for (size_t index = 0; index < internal::sites.size(); ++index) {
      if ((stood_down >> index & 1) != 0) {
        renodx::utils::log::w("[Total War] ", internal::sites[index].name, ": left alone (its bytes changed since the start: another mod)");
      }
    }
    if (recording_changed != 0) {
      renodx::utils::log::i("[Total War] Threaded Draw Recording: ", (recording_now != 0 ? "sites in" : "off, the game's own code back"));
    }
    if (recording_stood_down != 0) {
      // The sites whose bytes are not what this window expected (the game's going in, ours coming out): another mod's.
      // The others stood down with them (their group's, or the whole table's after a page that could not be made
      // writable).
      std::string changed_sites;
      for (size_t index = 0; index < internal::recording_sites.size(); ++index) {
        const internal::Site& site = internal::recording_sites[index];
        const auto& expected = ((recording_target >> index & 1) != 0 ? site.game : site.jump);
        const bool differs = (std::memcmp(site.at, expected.data(), internal::SITE_SIZE) != 0);
        if ((recording_stood_down >> index & 1) != 0 && differs) {
          changed_sites += std::format("{}{}", changed_sites.empty() ? "" : ", ", site.name);
        }
      }
      renodx::utils::log::w(
          "[Total War] Threaded Draw Recording: left alone, with the sites that go with it",
          (changed_sites.empty() ? " (a page of the game's code could not be made writable)"
                                 : std::format(" (its bytes changed since the start: another mod): {}", changed_sites)));
    }
  } else if (++internal::failed_writes == internal::MAX_FAILED_WRITES) {
    // The thread that stopped the last attempt: its id and where it was (the module and offset when the address is in one).
    std::string last = (internal::blocked_snapshot ? " (last: the thread list could not be read)"
                                                   : " (last: no thread was found inside a site)");
    if (internal::blocked_thread != 0 && internal::blocked_eip == 0) {
      last = std::format(" (last: thread {}, its context could not be read)", internal::blocked_thread);
    } else if (internal::blocked_thread != 0) {
      HMODULE owner = nullptr;
      std::array<char, MAX_PATH> path = {};
      if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(internal::blocked_eip), &owner) != FALSE
          && GetModuleFileNameA(owner, path.data(), MAX_PATH) != 0) {
        const std::string_view module_path = path.data();
        last = std::format(" (last: thread {} at {}+0x{:X})", internal::blocked_thread,
                           module_path.substr(module_path.find_last_of('\\') + 1),
                           internal::blocked_eip - reinterpret_cast<uintptr_t>(owner));
      } else {
        last = std::format(" (last: thread {} at 0x{:X})", internal::blocked_thread, internal::blocked_eip);
      }
    }
    for (const auto& [row_sites, name] : internal::ROWS) {
      if (((target ^ internal::written) & row_sites) != 0) {
        renodx::utils::log::w("[Total War] ", name, ": not written, a game thread kept running the code", last);
      }
    }
    // A worker site that cannot come out stays reachable.
    if (((target ^ internal::written) & internal::written & internal::MODULE_CALL_SITES) != 0) {
      internal::PinModule();
    }
    if (recording_target != internal::recording_written) {
      renodx::utils::log::w("[Total War] Threaded Draw Recording: not written, a game thread kept running the code", last);
    }
    if (effect_wanted != eu::written) {
      renodx::utils::log::w("[Total War] Parallel Effect Update sites: not written, a game thread kept running the code", last);
      // A site that cannot come out stays reachable.
      if (((effect_wanted ^ eu::written) & eu::written) != 0) {
        internal::PinModule();
      }
    }
    if (fill_wanted != fi::written) {
      renodx::utils::log::w("[Total War] Parallel Instance Fill sites: not written, a game thread kept running the code", last);
      // A site that cannot come out stays reachable.
      if (((fill_wanted ^ fi::written) & fi::written) != 0) {
        internal::PinModule();
      }
    }
  }
}

// What Cached Particle Count's stubs counted since the sites went in (all zero while the row was never on).
inline ParticleCountStats ParticleCount() {
  const internal::CountData* data = internal::count_data;
  return (data == nullptr ? ParticleCountStats{} : ParticleCountStats{.hits = data->hits, .misses = data->misses, .mismatches = data->mismatches});
}

// True while any site of Threaded Draw Recording is written or may still be reached (one that stood down on its way
// out, or that Uninstall gave up on): draw_recording::Update and Uninstall take it as `sites_in` and then keep their
// memory and module.
inline bool DrawRecordingSitesIn() { return internal::recording_written != 0 || internal::recording_left_in; }

// The loop groups (bits of draw_recording::Group) whose sites and flush path are all written now
// (draw_recording::Settings::groups_in): the dispatcher runs the rounds of these groups only.
inline uint32_t DrawRecordingGroupsIn() {
#if defined(TWATTILA_DRAW_RECORDING)
  return internal::RecordingGroupsIn();
#else
  return 0;
#endif
}

// The Advanced status's words for the LOD pick's site, for the slider `model_detail` now; null: not decided yet (Prepare has not run).
inline const char* ModelDetailStatus(float model_detail) {
  if ((internal::written & internal::MODEL_DETAIL_BIT) != 0) return "object detail hooked";
  if (model_detail == 1.f) return "object detail at 1x: the game's LOD pick left alone";
  if (!internal::prepared) return nullptr;
  return ((internal::found & internal::MODEL_DETAIL_BIT) == 0 ? "object detail: the game's LOD code was not found" : "object detail: not hooked yet");
}

// Addon detach: the game's bytes back. The page stays (a thread may still be in it), so the jumps are safe to leave in
// the game's code if no attempt finds the threads clear: the stubs need nothing of this module.
inline void Uninstall() {
  namespace eu = effect_update::internal;
  namespace fi = instance_fill::internal;
  internal::SyncWorkerSlots(0);
  effect_update::Disarm();
  instance_fill::Wanted(false);  // the fill's sites run the game's own call from now on
  // A fold open on another thread (a render thread between a fill site and its cut; this thread's own is closed by CloseFoldAtPresent,
  // right after this) refuses the attempt in PausedWrite while the fill or recording sites go out (the stubs and cuts that fold needs
  // stay in until it has closed): one that closes within the attempts lets the sites out, one still open leaves every site in below
  // (the module pinned, recording_left_in set: the cut's dispatcher closes the fold and makes the noted Unmap, and
  // draw_recording::Uninstall keeps Shared).
  for (int attempt = 0;
       (internal::written != 0 || internal::recording_written != 0 || eu::written != 0 || fi::written != 0) && attempt < 100;
       ++attempt) {
    internal::SiteMask stood_down;
    uint64_t recording_stood_down;
    uint32_t effect_stood_down;
    uint32_t fill_stood_down;
    if (internal::PausedWrite(0, &stood_down, 0, &recording_stood_down, 0, &effect_stood_down, 0, &fill_stood_down)) {
      if ((stood_down & internal::written & internal::MODULE_CALL_SITES) != 0 || (effect_stood_down & eu::written) != 0
          || (fill_stood_down & fi::written) != 0) {
        internal::PinModule();
      }
      internal::written = 0;  // a site another mod changed is that mod's now
      eu::written = 0;
      fi::written = 0;
      // The recording thunks need this module and Shared: a site left behind keeps them alive.
      if ((recording_stood_down & internal::recording_written) != 0) {
        internal::recording_left_in = true;
      }
      internal::recording_written = 0;
    } else {
      Sleep(1);
    }
  }
  if (internal::written != 0 || eu::written != 0) {
    if ((internal::written & internal::MODULE_CALL_SITES) != 0 || eu::written != 0) {
      internal::PinModule();
    }
    renodx::utils::log::w("[Total War] Faster Game CPU Code: left in at detach (a game thread kept running it)");
  }
  if (fi::written != 0) {
    internal::PinModule();
    renodx::utils::log::w("[Total War] Parallel Instance Fill: left in at detach (a game thread kept running it)");
  }
  if (const uint32_t incomplete = fi::incomplete_fills.load(); incomplete != 0) {
    renodx::utils::log::w("[Total War] Parallel Instance Fill: ", incomplete, " fills ended with chunks not done this run.");
  }
  if (internal::recording_written != 0) {
    internal::recording_left_in = true;
    renodx::utils::log::w("[Total War] Threaded Draw Recording: left in at detach (a game thread kept running it)");
  }
}

#if defined(TWATTILA_ENGINE_PROBE)
// The probe build's histogram dump: N presents after the first site was written (the ReShade.ini key the addon reads; 0: never).
inline void ProbeArm(uint32_t presents) { internal::probe::Arm(presents); }
#endif

#else

inline void Update(bool, Game = Game::ATTILA, bool = false, bool = false, bool = false, bool = false, bool = false, bool = false, float = 1.f,
                   uint32_t = 0, const WorkerRows& = {}) {}
inline ParticleCountStats ParticleCount() { return {}; }
inline bool DrawRecordingSitesIn() { return false; }
inline uint32_t DrawRecordingGroupsIn() { return 0; }
inline const char* ModelDetailStatus(float) { return nullptr; }
inline void Uninstall() {}
#if defined(TWATTILA_ENGINE_PROBE)
inline void ProbeArm(uint32_t) {}
#endif

#endif

}  // namespace twattila::engine_code
