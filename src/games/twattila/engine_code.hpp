/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <tlhelp32.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/log.hpp"
#include "../twrome2/engine_tweaks.hpp"

// Faster Game CPU Code: three of Attila's hot routines in empire.retail.dll, each replaced by code with the same results
// (test/tw_engine_code runs the game's code and this one side by side: every register, the stack, the x87 state).
// Each is found by its exact bytes: code another mod changed (the Attila CPU Performance Fix patches 1277 x87 blocks,
// the first one below among them) or another build is left alone. A jump into the addon's own code page replaces the
// first instruction(s) of each; the page is never freed (a thread can still be in it after the game's bytes are back).
// RVAs are the Steam build's (1.6.0): there they are tried first, then the code section is searched.
// Thrones of Britannia (Game::THRONES): the same three routines, its build's (internal::THRONES_BUILD).
namespace twattila::engine_code {

// Whose build of the routines to look for.
enum class Game : uint8_t { ATTILA, THRONES };

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
constexpr std::array<uint8_t, 44> TRUNCATION_STUB = {
    0x50,                                      // push eax
    0x0F, 0xB7, 0x44, 0x24, 0x1A,              // movzx eax, word ptr [esp + 0x1a]  the slot's high half (fstcw leaves it)
    0x9B, 0xD9, 0x7C, 0x24, 0x18,              // fstcw word ptr [esp + 0x18]
    0xC1, 0xE0, 0x10,                          // shl eax, 0x10
    0x66, 0x8B, 0x44, 0x24, 0x18,              // mov ax, word ptr [esp + 0x18]
    0x0D, 0x00, 0x0C, 0x00, 0x00,              // or eax, 0xc00
    0x89, 0x44, 0x24, 0x1C,                    // mov dword ptr [esp + 0x1c], eax
    0xF3, 0x0F, 0x2C, 0x44, 0x24, 0x20,        // cvttss2si eax, dword ptr [esp + 0x20]
    0x89, 0x44, 0x24, 0x2C,                    // mov dword ptr [esp + 0x2c], eax
    0x58,                                      // pop eax
    0xE9, 0x00, 0x00, 0x00, 0x00,              // jmp back (after the game's pop eax)
};

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

// Thrones of Britannia's build (empire.retail.dll 64A47C87; test/tw_engine_code with its DLL): float to half and fmod the
// same bytes at other RVAs; the soldier draw's truncation the same instructions at other stack slots (control word 0x20,
// float 0x24, its control word | 0xc00 0x28, integer 0x1c after the push), so its own stub with the same steps; the CRT's
// _matherr check compiled otherwise (eax decoded, not edx: the stub only calls it and tests al, eax ecx edx saved).
constexpr uint32_t THRONES_TRUNCATION_RVA = 0x186aa3a;
constexpr std::string_view THRONES_TRUNCATION =
    "8B 84 24 7C 01 00 00 89 84 24 84 00 00 00 50 D9 44 24 24 9B D9 7C 24 20 8B 44 24 20 0D 00 0C 00 00 89 44 24 28 "
    "D9 6C 24 28 DB 5C 24 1C D9 6C 24 20 58 0F B7 44 24 18 66 0F 6E C0 0F 5B C0 F3 0F 11 84 24";
constexpr std::array<uint8_t, 44> THRONES_TRUNCATION_STUB = {
    0x50,                                      // push eax
    0x0F, 0xB7, 0x44, 0x24, 0x22,              // movzx eax, word ptr [esp + 0x22]  the slot's high half (fstcw leaves it)
    0x9B, 0xD9, 0x7C, 0x24, 0x20,              // fstcw word ptr [esp + 0x20]
    0xC1, 0xE0, 0x10,                          // shl eax, 0x10
    0x66, 0x8B, 0x44, 0x24, 0x20,              // mov ax, word ptr [esp + 0x20]
    0x0D, 0x00, 0x0C, 0x00, 0x00,              // or eax, 0xc00
    0x89, 0x44, 0x24, 0x28,                    // mov dword ptr [esp + 0x28], eax
    0xF3, 0x0F, 0x2C, 0x44, 0x24, 0x24,        // cvttss2si eax, dword ptr [esp + 0x24]
    0x89, 0x44, 0x24, 0x1C,                    // mov dword ptr [esp + 0x1c], eax
    0x58,                                      // pop eax
    0xE9, 0x00, 0x00, 0x00, 0x00,              // jmp back (after the game's pop eax)
};
constexpr std::string_view THRONES_FMOD_MATHERR = "A1 ?? ?? ?? ?? 8B C8 33 05 ?? ?? ?? ?? 83 E1 1F D3 C8 85 C0 0F 95 C0 C3";

// What differs between the games' builds.
struct Build {
  uint32_t truncation_rva;
  std::string_view truncation;
  std::span<const uint8_t> truncation_stub;
  uint32_t half_rva;
  uint32_t fmod_rva;
  std::string_view fmod_matherr;
};
constexpr Build ATTILA_BUILD = {TRUNCATION_RVA, TRUNCATION, TRUNCATION_STUB, HALF_RVA, FMOD_RVA, FMOD_MATHERR};
constexpr Build THRONES_BUILD = {THRONES_TRUNCATION_RVA, THRONES_TRUNCATION, THRONES_TRUNCATION_STUB, 0x1707eb0, 0x1b0a030, THRONES_FMOD_MATHERR};

// Where each stub sits in the page.
constexpr size_t TRUNCATION_PAGE = 0x000;
constexpr size_t HALF_PAGE = 0x040;
constexpr size_t FMOD_PAGE = 0x180;
constexpr size_t PAGE_SIZE = 0x1000;

// The first instruction (or two, whole) of each site: 5 bytes that become a jump into the page.
constexpr size_t SITE_SIZE = 5;

struct Site {
  std::string_view name;
  uint8_t* at = nullptr;  // null: not found, or not the game's code as expected
  std::array<uint8_t, SITE_SIZE> game = {};
  std::array<uint8_t, SITE_SIZE> jump = {};
};

static std::array<Site, 3> sites = {{{.name = "soldier draw rounding"}, {.name = "half floats"}, {.name = "fmod"}}};
static uint8_t* page = nullptr;
static bool prepared = false;
static bool written = false;
static bool asked = false;  // the row's last value
static uint32_t failed_writes = 0;
// Presents with a thread inside a site before giving up (until the row changes); each try snapshots the system's threads
// (~0.1 s with some antivirus hooks; 3 of 200 writes waited once in the test, four threads running the sites nonstop).
constexpr uint32_t MAX_FAILED_WRITES = 30;

// The sites' jumps (`patch`) or the game's bytes back, written while every other thread of the process is suspended and
// none stands inside a site past its first byte (it would go on in the middle of the jump). False: one did (or its
// position is unknown), nothing written. From the first suspension to the last resume nothing allocates, logs or takes a
// lock: a suspended thread may hold it. A thread started after the snapshot isn't held (as with Detours).
inline bool PausedWrite(bool patch) {
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
    for (const Site& site : sites) {
      const auto start = reinterpret_cast<uintptr_t>(site.at);
      if (site.at != nullptr && context.Eip > start && context.Eip < start + SITE_SIZE) clear = false;
    }
  }
  if (clear) {
    for (const Site& site : sites) {
      if (site.at == nullptr) continue;
      DWORD protection;
      if (VirtualProtect(site.at, SITE_SIZE, PAGE_EXECUTE_READWRITE, &protection) == FALSE) continue;
      const auto& bytes = (patch ? site.jump : site.game);
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

  page = static_cast<uint8_t*>(VirtualAlloc(nullptr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  std::array<uint8_t*, 3> stubs = {};
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
    DWORD protection;
    VirtualProtect(page, PAGE_SIZE, PAGE_EXECUTE_READ, &protection);
    FlushInstructionCache(GetCurrentProcess(), page, PAGE_SIZE);
  }
  std::string faster;
  std::string left;
  for (size_t index = 0; index < sites.size(); ++index) {
    Site& site = sites[index];
    std::string& names = (site.at != nullptr ? faster : left);
    names += std::format("{}{}", names.empty() ? "" : ", ", site.name);
    if (site.at == nullptr) continue;
    std::memcpy(site.game.data(), site.at, SITE_SIZE);
    site.jump[0] = 0xE9;
    write32(site.jump.data() + 1, relative(site.at + SITE_SIZE, stubs[index]));
  }
  renodx::utils::log::i("[Total War] Faster Game CPU Code: ", (faster.empty() ? "nothing" : faster), " faster",
                        (left.empty() ? "" : "; left alone (not the game's code as expected: another mod or build): "), left);
}

}  // namespace internal

// Every present: `enabled` (the row) once empire.retail.dll is loaded, the first time on prepared (`game`'s build); then
// the jumps go in or the game's bytes back, while no thread stands inside them.
inline void Update(bool enabled, Game game = Game::ATTILA) {
  if (enabled && !internal::prepared) {
    HMODULE module = GetModuleHandleW(L"empire.retail.dll");
    if (module == nullptr) return;
    internal::Prepare(module, game == Game::THRONES ? internal::THRONES_BUILD : internal::ATTILA_BUILD);
  }
  if (enabled != internal::asked) {
    internal::asked = enabled;
    internal::failed_writes = 0;
  }
  if (enabled == internal::written || !internal::prepared || internal::failed_writes >= internal::MAX_FAILED_WRITES) return;
  if (internal::PausedWrite(enabled)) {
    internal::written = enabled;
    internal::failed_writes = 0;
    renodx::utils::log::i("[Total War] Faster Game CPU Code: ", (enabled ? "on" : "off, the game's own code back"));
  } else if (++internal::failed_writes == internal::MAX_FAILED_WRITES) {
    renodx::utils::log::w("[Total War] Faster Game CPU Code: not written, a game thread kept running the code");
  }
}

// Addon detach: the game's bytes back. The page stays (a thread may still be in it), so the jumps are safe to leave in
// the game's code if no attempt finds the threads clear: the stubs need nothing of this module.
inline void Uninstall() {
  for (int attempt = 0; internal::written && attempt < 100; ++attempt) {
    if (internal::PausedWrite(false)) {
      internal::written = false;
    } else {
      Sleep(1);
    }
  }
  if (internal::written) renodx::utils::log::w("[Total War] Faster Game CPU Code: left in at detach (a game thread kept running it)");
}

#else

inline void Update(bool, Game = Game::ATTILA) {}
inline void Uninstall() {}

#endif

}  // namespace twattila::engine_code
