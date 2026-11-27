/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The plugin's unit test on a fake game image at the game's addresses (no game file needed): identity, the site checks
// and their refusals, the loader's hook and the re-check, the stack-check rule, the hook bodies on synthetic game
// functions at the real sites (this file's copy of hooks.hpp and the built DLL's own) with the add-on's callbacks,
// the scale rows at neutral and active (the grass and campaign tree rows on a fake scrub), and one live battle-core
// reserve on this process's own CPU sets, the callback drain, the patch kind and its way back, and the built DLL's
// init with each refusal, with the RenderCore key and with the ShadowMapSize key (copies of the DLL under dll_cases\,
// a ReShade.ini next to this exe for the ini keys), and the text a batch tool looks for in the DLL file.
// patcher_test.exe <renodx_attila64.dll>; exit code = failures.

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../hooks.hpp"
#include "../patcher.hpp"
#include "../tweaks.hpp"
#include "fake_loader.hpp"

namespace {

using namespace twattila::attila64_plugin;
using namespace twattila::attila64_plugin::test;

inline constexpr uintptr_t PACK_RECORD = 0x610000;  // the synthetic pack stores its 8 arguments here and counts
inline constexpr uintptr_t DRAW_RECORD = 0x630000;  // the synthetic draw stores its 3 arguments here and counts
inline constexpr uintptr_t RULE_SITE = 0x640000;
inline constexpr uintptr_t STEP_RECORD = 0x650000;  // the synthetic step stores what its task holds here and counts
inline constexpr uint64_t STEP_RESULT = 0x5eed0000c0de;
inline constexpr uintptr_t UNIT_LOD_RECORD = 0x660000;   // the synthetic unit LOD function's record and its 18 floats
inline constexpr uintptr_t MODEL_LOD_RECORD = 0x670000;  // the synthetic model LOD pick's record
inline constexpr uintptr_t CALLER_THUNKS = 0x680000;     // callers with known return addresses (WriteCaller)
inline constexpr uintptr_t CALLER_SAVED = 0x690000;      // the six callee-saved registers after a caller's call
inline constexpr uintptr_t GRASS_UPDATE_RECORD = 0x6a0000;   // the four synthetic grass and campaign tree functions'
inline constexpr uintptr_t GRASS_DRAW_RECORD = 0x6b0000;     // records
inline constexpr uintptr_t GRASS_CELL_RECORD = 0x6c0000;
inline constexpr uintptr_t CAMPAIGN_TREE_RECORD = 0x6d0000;
inline constexpr uintptr_t SPRITE_BED = 0x6e0000;  // the sprite site's test bed (fake_loader.hpp WriteMidBed)
inline constexpr uintptr_t PAIRS_RECORD = 0x6f0000;  // the synthetic pair resolver's record
inline constexpr uintptr_t CONTACT_RECORD = 0x6f0010;  // the synthetic contact pass's, a PairsRecord too
// What a caller loads into rbx, rbp, r12, r13, r14 and r15 before its call.
inline constexpr std::array<uint64_t, 6> CALLER_SENTINELS = {0xb0b0b0b0b0b0b0b0, 0xb5b5b5b5b5b5b5b5, 0xc0c0c0c0c0c0c0c0,
                                                             0xd0d0d0d0d0d0d0d0, 0xe0e0e0e0e0e0e0e0, 0xf0f0f0f0f0f0f0f0};
inline constexpr size_t CALLER_CALL_OFFSET = 74;  // a caller's call instruction, from its entry

struct PackRecord {
  std::array<uint64_t, 8> arguments;
  uint32_t calls;
};
struct DrawRecord {
  std::array<uint64_t, 3> arguments;
  uint32_t calls;
};
struct StepRecord {
  uint64_t task_field;
  uint32_t calls;
};
struct UnitLodRecord {
  std::array<uint64_t, 6> arguments;  // rdi (through rbx), rsi, rdx, rcx, r8, r9 as the function got them
  uint32_t calls;
  uint32_t unused;
  uint64_t return_address;            // what the function would return to
  std::array<float, 18> distances;    // copied into the caller's table
};
static_assert(offsetof(UnitLodRecord, calls) == 0x30 && offsetof(UnitLodRecord, return_address) == 0x38
              && offsetof(UnitLodRecord, distances) == 0x40);
struct ModelLodRecord {
  uint64_t entry;
  uint64_t next_lod;
  float distance_squared;
  uint32_t calls;
  uint64_t return_address;
};
static_assert(offsetof(ModelLodRecord, distance_squared) == 0x10 && offsetof(ModelLodRecord, calls) == 0x14
              && offsetof(ModelLodRecord, return_address) == 0x18);
struct GrassUpdateRecord {
  uint64_t scrub;
  int32_t radius;  // the scrub's, as the function finds them
  uint32_t bias;
  uint32_t calls;
  uint32_t unused;
  uint64_t return_address;
};
static_assert(offsetof(GrassUpdateRecord, radius) == 8 && offsetof(GrassUpdateRecord, bias) == 12
              && offsetof(GrassUpdateRecord, calls) == 16 && offsetof(GrassUpdateRecord, return_address) == 24);
struct GrassDrawRecord {
  std::array<uint64_t, 6> arguments;  // rdi, rsi, rdx, rcx, r8 (through r14), r9
  uint32_t calls;
  uint32_t unused;
  uint64_t return_address;
};
static_assert(offsetof(GrassDrawRecord, calls) == 0x30 && offsetof(GrassDrawRecord, return_address) == 0x38);
struct GrassCellRecord {
  std::array<uint64_t, 4> arguments;  // rdi, rsi, rdx, rcx
  float radius_squared;
  uint32_t calls;
  uint64_t return_address;
  std::array<uint64_t, 6> clip;       // the 48 bytes on the stack
};
static_assert(offsetof(GrassCellRecord, radius_squared) == 0x20 && offsetof(GrassCellRecord, calls) == 0x24
              && offsetof(GrassCellRecord, return_address) == 0x28 && offsetof(GrassCellRecord, clip) == 0x30);
struct CampaignTreeRecord {
  std::array<uint64_t, 6> arguments;  // rdi, rsi, rdx, rcx, r8, r9
  float first_float;
  float second_float;
  uint32_t calls;
  uint32_t unused;
  uint64_t return_address;
};
static_assert(offsetof(CampaignTreeRecord, first_float) == 0x30 && offsetof(CampaignTreeRecord, second_float) == 0x34
              && offsetof(CampaignTreeRecord, calls) == 0x38 && offsetof(CampaignTreeRecord, return_address) == 0x40);

// The synthetic game functions are the real sites' bytes (each window's site and its 16 bytes after) followed by these
// tails, Linux calling convention. The pack window ends inside mov [rsp+disp8], r9: its tail starts with the disp8.
// Pack: push rbp / r15 / r14 / r13 / r12 / rbx, sub rsp 0x128, so the 7th and 8th arguments sit at rsp+0x160 / 0x168.
inline constexpr std::array<uint8_t, 81> PACK_TAIL = {
    0x08,                                                  // the window's mov [rsp+8], r9
    0x48, 0xb8, 0x00, 0x00, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, PACK_RECORD
    0x48, 0x89, 0x38,                                      // mov [rax], rdi
    0x48, 0x89, 0x70, 0x08,                                // mov [rax+8], rsi
    0x48, 0x89, 0x50, 0x10,                                // mov [rax+16], rdx
    0x48, 0x89, 0x48, 0x18,                                // mov [rax+24], rcx
    0x4c, 0x89, 0x40, 0x20,                                // mov [rax+32], r8
    0x4c, 0x89, 0x48, 0x28,                                // mov [rax+40], r9
    0x4c, 0x8b, 0xb4, 0x24, 0x60, 0x01, 0x00, 0x00,        // mov r14, [rsp+0x160]: the 7th argument
    0x4c, 0x89, 0x70, 0x30,                                // mov [rax+48], r14
    0x4c, 0x8b, 0xb4, 0x24, 0x68, 0x01, 0x00, 0x00,        // mov r14, [rsp+0x168]: the 8th
    0x4c, 0x89, 0x70, 0x38,                                // mov [rax+56], r14
    0xff, 0x40, 0x40,                                      // inc dword [rax+64]
    0x48, 0x81, 0xc4, 0x28, 0x01, 0x00, 0x00,              // add rsp, 0x128
    0x5b, 0x41, 0x5c, 0x41, 0x5d, 0x41, 0x5e, 0x41, 0x5f, 0x5d,  // pop rbx / r12 / r13 / r14 / r15 / rbp
    0xb0, 0x01, 0xc3,                                      // mov al, 1; ret
};
// Draw: push r15 / r14 / r13 / r12 / rbx, sub rsp 0x130, mov r15, rdx, mov r14, rsi (all in the window).
inline constexpr std::array<uint8_t, 42> DRAW_TAIL = {
    0x48, 0xb8, 0x00, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, DRAW_RECORD
    0x48, 0x89, 0x38,                                      // mov [rax], rdi
    0x4c, 0x89, 0x70, 0x08,                                // mov [rax+8], r14 (rsi)
    0x4c, 0x89, 0x78, 0x10,                                // mov [rax+16], r15 (rdx)
    0xff, 0x40, 0x18,                                      // inc dword [rax+24]
    0x48, 0x81, 0xc4, 0x30, 0x01, 0x00, 0x00,              // add rsp, 0x130
    0x5b, 0x41, 0x5c, 0x41, 0x5d, 0x41, 0x5e, 0x41, 0x5f,  // pop rbx / r12 / r13 / r14 / r15
    0xc3, 0xcc,                                            // ret
};

// Step: push rbp / rbx / rax and mov rbx, [task + 0x38] (the site), then mov ebp, 1, mov edi, 1 and a call (the
// window; its target gets a ret) and the first byte of the next instruction, a REX.W prefix. The tail returns
// STEP_RESULT (the game's returns 0).
inline constexpr size_t STEP_TASK_FIELD = 0x38;
inline constexpr std::array<uint8_t, 34> STEP_TAIL = {
    0x89, 0xd8,                                            // the window's 48: mov rax, rbx
    0x48, 0xb9, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rcx, STEP_RECORD
    0x48, 0x89, 0x01,                                      // mov [rcx], rax
    0xff, 0x41, 0x08,                                      // inc dword [rcx+8]
    0x48, 0xb8, 0xde, 0xc0, 0x00, 0x00, 0xed, 0x5e, 0x00, 0x00,  // mov rax, STEP_RESULT
    0x48, 0x83, 0xc4, 0x08,                                // add rsp, 8
    0x5b, 0x5d,                                            // pop rbx / rbp
};
inline constexpr uint8_t RET = 0xc3;

// Unit LOD: push rbp / r14 / rbx, sub rsp 0x50 (the site), then mov r14d, ecx, mov ebp, edx, mov rbx, rdi,
// test r9b, r9b, je +0x2a and the first three bytes of movaps xmm7, [rip + disp32] (the window). The test puts that
// disp32 (to UNIT_LOD_RECORD, 16-aligned) right after the window, then UNIT_LOD_SKIP, and this body where the je lands.
inline constexpr std::array<uint8_t, 2> UNIT_LOD_SKIP = {0xeb, 0x21};  // jmp to the body, as the je does
inline constexpr size_t UNIT_LOD_BODY_OFFSET = 0x3f;                   // the je's target, from the function's entry
inline constexpr std::array<uint8_t, 99> UNIT_LOD_BODY = {
    0x48, 0xb8, 0x00, 0x00, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, UNIT_LOD_RECORD
    0x48, 0x89, 0x18,                                      // mov [rax], rbx (rdi)
    0x48, 0x89, 0x70, 0x08,                                // mov [rax+8], rsi
    0x48, 0x89, 0x50, 0x10,                                // mov [rax+16], rdx
    0x48, 0x89, 0x48, 0x18,                                // mov [rax+24], rcx
    0x4c, 0x89, 0x40, 0x20,                                // mov [rax+32], r8
    0x4c, 0x89, 0x48, 0x28,                                // mov [rax+40], r9
    0x48, 0x8b, 0x4c, 0x24, 0x68,                          // mov rcx, [rsp+0x68]: the return address
    0x48, 0x89, 0x48, 0x38,                                // mov [rax+0x38], rcx
    0xff, 0x40, 0x30,                                      // inc dword [rax+0x30]
    0x0f, 0x10, 0x40, 0x40, 0x0f, 0x11, 0x03,              // movups xmm0, [rax+0x40]; movups [rbx], xmm0
    0x0f, 0x10, 0x40, 0x50, 0x0f, 0x11, 0x43, 0x10,        // the next 16 bytes
    0x0f, 0x10, 0x40, 0x60, 0x0f, 0x11, 0x43, 0x20,
    0x0f, 0x10, 0x40, 0x70, 0x0f, 0x11, 0x43, 0x30,
    0x48, 0x8b, 0x88, 0x80, 0x00, 0x00, 0x00,              // mov rcx, [rax+0x80]: entries 16 and 17
    0x48, 0x89, 0x4b, 0x40,                                // mov [rbx+0x40], rcx
    0x48, 0x89, 0xd8,                                      // mov rax, rbx: the table is the result
    0x48, 0x83, 0xc4, 0x50,                                // add rsp, 0x50
    0x5b, 0x41, 0x5e, 0x5d,                                // pop rbx / r14 / rbp
    0xc3,                                                  // ret
};
// Model LOD: push r14 / rbx / rax, mov r14, rsi (the site), then mov rbx, rdi, mov qword [r14], 0,
// mov ecx, [rbx + 0x44], xor eax, eax and the first byte of test ecx, ecx (the window). The tail returns the entry.
inline constexpr std::array<uint8_t, 46> MODEL_LOD_TAIL = {
    0xc9,                                                  // the window's 85: test ecx, ecx
    0x48, 0xb8, 0x00, 0x00, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, MODEL_LOD_RECORD
    0x48, 0x89, 0x18,                                      // mov [rax], rbx (rdi)
    0x4c, 0x89, 0x70, 0x08,                                // mov [rax+8], r14 (rsi)
    0xf3, 0x0f, 0x11, 0x40, 0x10,                          // movss [rax+16], xmm0
    0xff, 0x40, 0x14,                                      // inc dword [rax+20]
    0x48, 0x8b, 0x4c, 0x24, 0x18,                          // mov rcx, [rsp+0x18]: the return address
    0x48, 0x89, 0x48, 0x18,                                // mov [rax+24], rcx
    0x48, 0x89, 0xd8,                                      // mov rax, rbx
    0x48, 0x83, 0xc4, 0x08,                                // add rsp, 8
    0x5b, 0x41, 0x5e,                                      // pop rbx / r14
    0xc3,                                                  // ret
};

// Grass update: push rbp / r15 / r14 (the site), then push r13 / r12 / rbx, sub rsp 0xa8, mov rbx, rdi and the first
// byte of cmp byte [rbx + 0x28], 0 (the window). The tail stores what the scrub holds when the game's update starts.
inline constexpr std::array<uint8_t, 68> GRASS_UPDATE_TAIL = {
    0x7b, 0x28, 0x00,                                      // the window's 80: cmp byte [rbx+0x28], 0
    0x48, 0xb8, 0x00, 0x00, 0x6a, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, GRASS_UPDATE_RECORD
    0x48, 0x89, 0x18,                                      // mov [rax], rbx (rdi)
    0x8b, 0x8b, 0xd0, 0x00, 0x00, 0x00,                    // mov ecx, [rbx+0xd0]: the radius
    0x89, 0x48, 0x08,                                      // mov [rax+8], ecx
    0x8b, 0x8b, 0xd4, 0x00, 0x00, 0x00,                    // mov ecx, [rbx+0xd4]: the bias
    0x89, 0x48, 0x0c,                                      // mov [rax+12], ecx
    0xff, 0x40, 0x10,                                      // inc dword [rax+16]
    0x48, 0x8b, 0x8c, 0x24, 0xd8, 0x00, 0x00, 0x00,        // mov rcx, [rsp+0xd8]: the return address
    0x48, 0x89, 0x48, 0x18,                                // mov [rax+24], rcx
    0x48, 0x81, 0xc4, 0xa8, 0x00, 0x00, 0x00,              // add rsp, 0xa8
    0x5b, 0x41, 0x5c, 0x41, 0x5d, 0x41, 0x5e, 0x41, 0x5f, 0x5d,  // pop rbx / r12 / r13 / r14 / r15 / rbp
    0xc3,                                                  // ret
};
// Grass draw: the same pushes, sub rsp 0x78, mov [rsp + 8], r9d and the first two bytes of mov r14, r8 (the window).
inline constexpr std::array<uint8_t, 65> GRASS_DRAW_TAIL = {
    0xc6,                                                  // the window's 4d 89: mov r14, r8
    0x48, 0xb8, 0x00, 0x00, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, GRASS_DRAW_RECORD
    0x48, 0x89, 0x38,                                      // mov [rax], rdi
    0x48, 0x89, 0x70, 0x08,                                // mov [rax+8], rsi
    0x48, 0x89, 0x50, 0x10,                                // mov [rax+16], rdx
    0x48, 0x89, 0x48, 0x18,                                // mov [rax+24], rcx
    0x4c, 0x89, 0x70, 0x20,                                // mov [rax+32], r14 (r8)
    0x4c, 0x89, 0x48, 0x28,                                // mov [rax+40], r9
    0xff, 0x40, 0x30,                                      // inc dword [rax+0x30]
    0x48, 0x8b, 0x8c, 0x24, 0xa8, 0x00, 0x00, 0x00,        // mov rcx, [rsp+0xa8]: the return address
    0x48, 0x89, 0x48, 0x38,                                // mov [rax+0x38], rcx
    0x48, 0x83, 0xc4, 0x78,                                // add rsp, 0x78
    0x5b, 0x41, 0x5c, 0x41, 0x5d, 0x41, 0x5e, 0x41, 0x5f, 0x5d,  // pop rbx / r12 / r13 / r14 / r15 / rbp
    0xc3,                                                  // ret
};
// Grass cell: the same pushes, sub rsp 0x178 and the first four bytes of movss [rsp + 4], xmm0 (the window). The clip
// state the caller passed by value sits right above the return address.
inline constexpr std::array<uint8_t, 102> GRASS_CELL_TAIL = {
    0x24, 0x04,                                            // the window's f3 0f 11 44: movss [rsp+4], xmm0
    0x48, 0xb8, 0x00, 0x00, 0x6c, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, GRASS_CELL_RECORD
    0x48, 0x89, 0x38,                                      // mov [rax], rdi
    0x48, 0x89, 0x70, 0x08,                                // mov [rax+8], rsi
    0x48, 0x89, 0x50, 0x10,                                // mov [rax+16], rdx
    0x48, 0x89, 0x48, 0x18,                                // mov [rax+24], rcx
    0xf3, 0x0f, 0x11, 0x40, 0x20,                          // movss [rax+0x20], xmm0
    0xff, 0x40, 0x24,                                      // inc dword [rax+0x24]
    0x48, 0x8b, 0x8c, 0x24, 0xa8, 0x01, 0x00, 0x00,        // mov rcx, [rsp+0x1a8]: the return address
    0x48, 0x89, 0x48, 0x28,                                // mov [rax+0x28], rcx
    0x0f, 0x10, 0x84, 0x24, 0xb0, 0x01, 0x00, 0x00,        // movups xmm0, [rsp+0x1b0]: the clip state
    0x0f, 0x11, 0x40, 0x30,                                // movups [rax+0x30], xmm0
    0x0f, 0x10, 0x84, 0x24, 0xc0, 0x01, 0x00, 0x00,        // the next 16 bytes
    0x0f, 0x11, 0x40, 0x40,
    0x0f, 0x10, 0x84, 0x24, 0xd0, 0x01, 0x00, 0x00,
    0x0f, 0x11, 0x40, 0x50,
    0x48, 0x81, 0xc4, 0x78, 0x01, 0x00, 0x00,              // add rsp, 0x178
    0x5b, 0x41, 0x5c, 0x41, 0x5d, 0x41, 0x5e, 0x41, 0x5f, 0x5d,  // pop rbx / r12 / r13 / r14 / r15 / rbp
    0xc3,                                                  // ret
};
// Effect walk: push rbp / r15 / r14 / r13 / r12 / rbx, sub rsp 0x178 and the first four bytes of movss [rsp + 0x28],
// xmm1 (the window). The tail only returns: what the walk's hook does shows in the game's flag.
inline constexpr std::array<uint8_t, 20> EFFECT_WALK_TAIL = {
    0x24, 0x28,                                            // the window's f3 0f 11 4c: movss [rsp+0x28], xmm1
    0x48, 0x81, 0xc4, 0x78, 0x01, 0x00, 0x00,              // add rsp, 0x178
    0x5b, 0x41, 0x5c, 0x41, 0x5d, 0x41, 0x5e, 0x41, 0x5f, 0x5d,  // pop rbx / r12 / r13 / r14 / r15 / rbp
    0xc3,                                                  // ret
};
// Campaign tree: the same pushes, sub rsp 0x68, mov r12d, r9d and the first four bytes of mov [rsp + 0x20], r8 (the
// window). The tail returns its record's address.
inline constexpr std::array<uint8_t, 75> CAMPAIGN_TREE_TAIL = {
    0x20,                                                  // the window's 4c 89 44 24: mov [rsp+0x20], r8
    0x48, 0xb8, 0x00, 0x00, 0x6d, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, CAMPAIGN_TREE_RECORD
    0x48, 0x89, 0x38,                                      // mov [rax], rdi
    0x48, 0x89, 0x70, 0x08,                                // mov [rax+8], rsi
    0x48, 0x89, 0x50, 0x10,                                // mov [rax+16], rdx
    0x48, 0x89, 0x48, 0x18,                                // mov [rax+24], rcx
    0x4c, 0x89, 0x40, 0x20,                                // mov [rax+32], r8
    0x4c, 0x89, 0x48, 0x28,                                // mov [rax+40], r9
    0xf3, 0x0f, 0x11, 0x40, 0x30,                          // movss [rax+0x30], xmm0
    0xf3, 0x0f, 0x11, 0x48, 0x34,                          // movss [rax+0x34], xmm1
    0xff, 0x40, 0x38,                                      // inc dword [rax+0x38]
    0x48, 0x8b, 0x8c, 0x24, 0x98, 0x00, 0x00, 0x00,        // mov rcx, [rsp+0x98]: the return address
    0x48, 0x89, 0x48, 0x40,                                // mov [rax+0x40], rcx
    0x48, 0x83, 0xc4, 0x68,                                // add rsp, 0x68
    0x5b, 0x41, 0x5c, 0x41, 0x5d, 0x41, 0x5e, 0x41, 0x5f, 0x5d,  // pop rbx / r12 / r13 / r14 / r15 / rbp
    0xc3,                                                  // ret
};
// Pair resolver: the same pushes, sub rsp 0x28, mov r13, rdi and mov eax, [r13 + 0x1c] (the window): its argument must
// be readable there.
struct PairsRecord {
  void* manager;
  uint32_t calls;
};
inline constexpr std::array<uint8_t, 31> PAIRS_TAIL = {
    0x48, 0xb8, 0x00, 0x00, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, PAIRS_RECORD
    0x48, 0x89, 0x38,                                      // mov [rax], rdi
    0xff, 0x40, 0x08,                                      // inc dword [rax+8]
    0x48, 0x83, 0xc4, 0x28,                                // add rsp, 0x28
    0x5b, 0x41, 0x5c, 0x41, 0x5d, 0x41, 0x5e, 0x41, 0x5f, 0x5d,  // pop rbx / r12 / r13 / r14 / r15 / rbp
    0xc3,                                                  // ret
};
// Contact pass: push rbp / r15 / r14 (the site), push r12 / rbx, sub rsp 0x2010, mov r15, rdi and the start of
// mov al, [rip + a displacement] (the window, with the displacement's first byte, a zero): the tail's zeros end that
// load on the byte after it.
inline constexpr std::array<uint8_t, 35> CONTACT_TAIL = {
    0x00, 0x00, 0x00,                                      // the load's displacement, its last three bytes
    0x48, 0xb8, 0x10, 0x00, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00,  // mov rax, CONTACT_RECORD
    0x48, 0x89, 0x38,                                      // mov [rax], rdi
    0xff, 0x40, 0x08,                                      // inc dword [rax+8]
    0x48, 0x81, 0xc4, 0x10, 0x20, 0x00, 0x00,              // add rsp, 0x2010
    0x5b, 0x41, 0x5c, 0x41, 0x5e, 0x41, 0x5f, 0x5d,        // pop rbx / r12 / r14 / r15 / rbp
    0xc3,                                                  // ret
};

// A caller in the fake game whose call instruction sits at `call_at` (so the callee sees call_at + 5 as its return
// address): it loads CALLER_SENTINELS into the callee-saved registers, calls `target` with the arguments it got itself
// (16-aligned stack, as the game's calls are), and stores those registers at CALLER_SAVED before it returns the
// callee's result. Returns its entry.
uintptr_t WriteCaller(uintptr_t call_at, uintptr_t target) {
  auto* code = reinterpret_cast<uint8_t*>(call_at - CALLER_CALL_OFFSET);
  const auto emit = [&](std::initializer_list<uint8_t> bytes) { code = std::copy(bytes.begin(), bytes.end(), code); };
  const auto emit_value = [&](uint64_t value, size_t size) {
    std::memcpy(code, &value, size);
    code += size;
  };
  emit({0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});  // push rbx / rbp / r12 / r13 / r14 / r15
  emit({0x48, 0x83, 0xec, 0x08});                                      // sub rsp, 8
  const std::array<std::array<uint8_t, 2>, 6> load_register = {{{0x48, 0xbb}, {0x48, 0xbd}, {0x49, 0xbc}, {0x49, 0xbd},
                                                                {0x49, 0xbe}, {0x49, 0xbf}}};  // mov reg, imm64
  for (size_t index = 0; index < load_register.size(); ++index) {
    emit({load_register[index][0], load_register[index][1]});
    emit_value(CALLER_SENTINELS[index], 8);
  }
  emit({0xe8});  // call target
  emit_value(static_cast<uint32_t>(target - (call_at + 5)), 4);
  emit({0x49, 0xbb});  // mov r11, CALLER_SAVED
  emit_value(CALLER_SAVED, 8);
  emit({0x49, 0x89, 0x1b, 0x49, 0x89, 0x6b, 0x08, 0x4d, 0x89, 0x63, 0x10});  // mov [r11], rbx; [r11+8], rbp; [r11+16], r12
  emit({0x4d, 0x89, 0x6b, 0x18, 0x4d, 0x89, 0x73, 0x20, 0x4d, 0x89, 0x7b, 0x28});  // [r11+24], r13; +32, r14; +40, r15
  emit({0x48, 0x83, 0xc4, 0x08});                                      // add rsp, 8
  emit({0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5d, 0x5b});  // pop r15 / r14 / r13 / r12 / rbp / rbx
  emit({RET});
  return call_at - CALLER_CALL_OFFSET;
}

// The fake view: its vtable's pass-kind slot returns `kind`.
struct FakeView {
  void* const* vtable;
  uint32_t kind;
};
ATTILA64_GAME_CALL uint32_t FakeViewKind(void* view) noexcept { return static_cast<FakeView*>(view)->kind; }

// The add-on's side. Atomics: the drain runs them on two game threads.
Block* active_block = nullptr;  // the block whose hooks run (this exe's or a DLL copy's)
std::atomic<uint32_t> before_pack_calls = 0;
std::atomic<uint32_t> pass_begin_calls = 0;
std::atomic<uint32_t> pass_end_calls = 0;
std::atomic<uint32_t> last_kind = 0;
std::atomic_bool end_measure_ok = false;
std::atomic<PassAction> next_action = PASS_DRAW;
const Callbacks* swap_in_begin = nullptr;  // stored into the block by pass_begin when set (a re-register in between)
std::atomic_bool drained = false;
std::atomic<uint32_t> calls_after_drain = 0;

void BeforePack() noexcept {
  if (drained.load()) ++calls_after_drain;
  ++before_pack_calls;
}

PassAction PassBegin(uint32_t pass_kind, PassMeasure* measure) noexcept {
  if (drained.load()) ++calls_after_drain;
  ++pass_begin_calls;
  last_kind = pass_kind;
  *measure = {.sun_binds_before = 11, .other_binds_before = 22, .sun_at_start = 33, .reserved = 0};
  if (swap_in_begin != nullptr) active_block->callbacks.store(swap_in_begin);
  return next_action.load();
}

void PassEnd(uint32_t pass_kind, const PassMeasure& measure) noexcept {
  if (drained.load()) ++calls_after_drain;
  ++pass_end_calls;
  end_measure_ok = pass_kind == last_kind && measure.sun_binds_before == 11 && measure.other_binds_before == 22
                   && measure.sun_at_start == 33;
}

const Callbacks CALLBACKS = {.before_pack = &BeforePack, .pass_begin = &PassBegin, .pass_end = &PassEnd};
const Callbacks OTHER_CALLBACKS = CALLBACKS;

// The rows of this test's fake game: every row with a site but the game code row. The fake game does not hold the
// game code that row's stubs stand on, so every plugin init here runs with that row's install key 0
// (tests\game_code_test.cpp has the row, on the game's file).
inline constexpr uint32_t ALL_ROWS = [] {
  uint32_t rows = 0;
  for (const Site& site : SITES) {
    rows |= RowBit(static_cast<Row>(site.row));
  }
  return rows & ~RowBit(ROW_GAME_CODE);
}();
inline constexpr uint32_t HOOK_SITE_COUNT = [] {
  uint32_t count = 0;
  for (const Site& site : SITES) {
    count += (site.kind == SITE_HOOK) ? 1 : 0;
  }
  return count;
}();
// The sites outside the shadow row: the hooks and the mid-function site.
inline constexpr uint32_t JUMP_SITE_COUNT = [] {
  uint32_t count = 0;
  for (const Site& site : SITES) {
    count += (site.kind == SITE_HOOK || site.kind == SITE_MID) ? 1 : 0;
  }
  return count;
}();
// The sites of ALL_ROWS: what a plugin init here puts in.
inline constexpr size_t FAKE_GAME_SITE_COUNT = std::ranges::count_if(SITES, [](const Site& site) {
  return (ALL_ROWS & RowBit(static_cast<Row>(site.row))) != 0;
});
// The fake game after the sprite site's window, which ends inside lea rsi, [rsp + 0x3d0]: that instruction's last four
// bytes, then a jump back to the bed.
inline constexpr std::array<uint8_t, 5> SPRITE_SITE_TAIL = {0xd0, 0x03, 0x00, 0x00, 0xe9};

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (!ReserveGame(PAGE_EXECUTE_READWRITE)) {
    std::printf("FAIL: the game range 0x400000..0x527d000 is taken\n");
    return 1;
  }
  if (argc < 2) {
    std::printf("FAIL: patcher_test.exe <renodx_attila64.dll>\n");
    return 1;
  }
  char exe_dir[MAX_PATH];
  GetModuleFileNameA(nullptr, exe_dir, MAX_PATH);
  *std::strrchr(exe_dir, '\\') = '\0';
  wchar_t exe_dir_wide[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_dir_wide, MAX_PATH);
  *std::wcsrchr(exe_dir_wide, L'\\') = L'\0';
  LoaderApi api = MakeApi(exe_dir, GAME_HI_MIN);
  loader = &api;

  const auto at = [](uintptr_t address) { return reinterpret_cast<uint8_t*>(address); };
  // Where the fake game's callers of the two LOD functions call them.
  constexpr uintptr_t UNIT_LOD_CALL = CALLER_THUNKS + CALLER_CALL_OFFSET;
  constexpr uintptr_t MODEL_LOD_CALL = CALLER_THUNKS + 0x100 + CALLER_CALL_OFFSET;
  constexpr uintptr_t GRASS_UPDATE_CALL = CALLER_THUNKS + 0x200 + CALLER_CALL_OFFSET;
  constexpr uintptr_t GRASS_DRAW_CALL = CALLER_THUNKS + 0x300 + CALLER_CALL_OFFSET;
  constexpr uintptr_t CAMPAIGN_TREE_CALL = CALLER_THUNKS + 0x400 + CALLER_CALL_OFFSET;
  // The cell's caller is its own: it passes the 48 bytes of clip state it got on the stack on (six qwords copied down).
  constexpr uintptr_t GRASS_CELL_CALLER = CALLER_THUNKS + 0x500;
  constexpr uintptr_t GRASS_CELL_CALL = GRASS_CELL_CALLER + 4 + (6 * 10);
  const auto write_game = [&] {
    std::memcpy(at(GAME_LO), ELF_MAGIC.data(), ELF_MAGIC.size());
    std::memcpy(at(BUILD_ID_NOTE_ADDRESS), BUILD_ID_NOTE.data(), BUILD_ID_NOTE.size());
    for (const Site& site : SITES) {
      std::memcpy(at(site.address - SITE_MARGIN), site.window.data(), (2 * SITE_MARGIN) + site.length);
    }
    const uintptr_t pack_end = SITES[ROW_BONE_FLUSH].address + SITES[ROW_BONE_FLUSH].length + SITE_MARGIN;
    const uintptr_t draw_end = SITES[ROW_PASS_DRAW].address + SITES[ROW_PASS_DRAW].length + SITE_MARGIN;
    const uintptr_t step_end = SITES[ROW_CPU_PLACEMENT].address + SITES[ROW_CPU_PLACEMENT].length + SITE_MARGIN;
    std::memcpy(at(pack_end), PACK_TAIL.data(), PACK_TAIL.size());
    std::memcpy(at(draw_end), DRAW_TAIL.data(), DRAW_TAIL.size());
    std::memcpy(at(step_end), STEP_TAIL.data(), STEP_TAIL.size());
    *at(step_end + STEP_TAIL.size()) = RET;
    const Site& unit_lod_site = SITES[LABEL_UNIT_LOD_ENTRY];
    const uintptr_t unit_lod_end = unit_lod_site.address + unit_lod_site.length + SITE_MARGIN;
    const auto record_from_load = static_cast<int32_t>(UNIT_LOD_RECORD - (unit_lod_end + sizeof(int32_t)));
    std::memcpy(at(unit_lod_end), &record_from_load, sizeof(record_from_load));
    std::memcpy(at(unit_lod_end + sizeof(int32_t)), UNIT_LOD_SKIP.data(), UNIT_LOD_SKIP.size());
    std::memcpy(at(unit_lod_site.address + UNIT_LOD_BODY_OFFSET), UNIT_LOD_BODY.data(), UNIT_LOD_BODY.size());
    const Site& model_lod_site = SITES[LABEL_MODEL_LOD_ENTRY];
    std::memcpy(at(model_lod_site.address + model_lod_site.length + SITE_MARGIN), MODEL_LOD_TAIL.data(),
                MODEL_LOD_TAIL.size());
    WriteCaller(REST_PACK_RETURN - 5, SITES[LABEL_PACK_ENTRY].address);
    WriteCaller(UNIT_LOD_CALL, unit_lod_site.address);
    WriteCaller(MODEL_LOD_CALL, model_lod_site.address);
    const auto write_tail = [&](SiteLabel label, std::span<const uint8_t> tail) {
      std::memcpy(at(SITES[label].address + SITES[label].length + SITE_MARGIN), tail.data(), tail.size());
    };
    write_tail(LABEL_GRASS_UPDATE_ENTRY, GRASS_UPDATE_TAIL);
    write_tail(LABEL_GRASS_DRAW_ENTRY, GRASS_DRAW_TAIL);
    write_tail(LABEL_GRASS_CELL_ENTRY, GRASS_CELL_TAIL);
    write_tail(LABEL_CAMPAIGN_TREE_ENTRY, CAMPAIGN_TREE_TAIL);
    write_tail(LABEL_EFFECT_WALK_ENTRY, EFFECT_WALK_TAIL);
    {
      // The sprite site is inside a function: its bed jumps to the site, and the game's instructions after it (the
      // store of the result, two loads) run on into the jump back.
      const Site& sprite_site = SITES[LABEL_SPRITE_DISTANCE];
      const uintptr_t sprite_end = sprite_site.address + sprite_site.length + SITE_MARGIN;
      uintptr_t capture = 0;
      WriteMidBed(at(SPRITE_BED), {}, sprite_site.address, 0x100, 0, SPRITE_BED_TAIL, &capture);
      const auto back = static_cast<uint32_t>(capture - (sprite_end + SPRITE_SITE_TAIL.size() + sizeof(uint32_t)));
      std::memcpy(at(sprite_end), SPRITE_SITE_TAIL.data(), SPRITE_SITE_TAIL.size());
      std::memcpy(at(sprite_end + SPRITE_SITE_TAIL.size()), &back, sizeof(back));
    }
    write_tail(LABEL_PAIRS_ENTRY, PAIRS_TAIL);
    write_tail(LABEL_CONTACT_ENTRY, CONTACT_TAIL);
    {
      // The plugin's init takes the grass draw for the game's by the hash of its first bytes: five bytes past the fake
      // draw's code are set so that the fake one hashes the same. FNV-1a's steps can be undone, so the last two bytes
      // are run back from the wanted hash and met by the two before them (a first byte is tried until they meet).
      uint8_t* draw = at(SITES[LABEL_GRASS_DRAW_ENTRY].address);
      uint8_t* fill = draw + GRASS_DRAW_NO_LOD_BYTES - 5;
      constexpr uint32_t PRIME = 0x01000193;
      uint32_t inverse = PRIME;  // of PRIME modulo 2^32, by Newton's steps
      for (int step = 0; step < 5; ++step) {
        inverse *= 2u - (PRIME * inverse);
      }
      std::vector<uint32_t> wanted(size_t{1} << 16);  // the hash wanted after the third byte, by the last two
      for (uint32_t last = 0; last < wanted.size(); ++last) {
        const uint32_t before_fifth = (GRASS_DRAW_NO_LOD_HASH * inverse) ^ (last >> 8);
        wanted[last] = (before_fifth * inverse) ^ (last & 0xff);
      }
      std::ranges::sort(wanted);
      bool met = false;
      for (uint32_t first = 0; first < 256 && !met; ++first) {
        fill[0] = static_cast<uint8_t>(first);
        const uint32_t before = battle_step::Fnv1a<uint32_t>(draw, GRASS_DRAW_NO_LOD_BYTES - 4);
        for (uint32_t middle = 0; middle < (1u << 16) && !met; ++middle) {
          const uint32_t after_third = ((((before ^ (middle >> 8)) * PRIME) ^ (middle & 0xff)) * PRIME);
          if (!std::ranges::binary_search(wanted, after_third)) continue;
          fill[1] = static_cast<uint8_t>(middle >> 8);
          fill[2] = static_cast<uint8_t>(middle);
          for (uint32_t last = 0; last < (1u << 16) && !met; ++last) {
            fill[3] = static_cast<uint8_t>(last);
            fill[4] = static_cast<uint8_t>(last >> 8);
            met = (battle_step::Fnv1a<uint32_t>(draw, GRASS_DRAW_NO_LOD_BYTES) == GRASS_DRAW_NO_LOD_HASH);
          }
        }
      }
    }
    WriteCaller(GRASS_UPDATE_CALL, SITES[LABEL_GRASS_UPDATE_ENTRY].address);
    WriteCaller(GRASS_DRAW_CALL, SITES[LABEL_GRASS_DRAW_ENTRY].address);
    WriteCaller(CAMPAIGN_TREE_CALL, SITES[LABEL_CAMPAIGN_TREE_ENTRY].address);
    std::vector<uint8_t> cell_caller = {0x48, 0x83, 0xec, 0x38};  // sub rsp, 0x38
    for (uint8_t qword = 0; qword < 6; ++qword) {
      cell_caller.insert(cell_caller.end(),
                         {0x48, 0x8b, 0x44, 0x24, static_cast<uint8_t>(0x40 + (8 * qword)),  // mov rax, [rsp+0x40+8n]
                          0x48, 0x89, 0x44, 0x24, static_cast<uint8_t>(8 * qword)});         // mov [rsp+8n], rax
    }
    const auto cell_target = static_cast<uint32_t>(SITES[LABEL_GRASS_CELL_ENTRY].address - (GRASS_CELL_CALL + 5));
    cell_caller.push_back(0xe8);  // call the cell site
    cell_caller.insert(cell_caller.end(), {static_cast<uint8_t>(cell_target), static_cast<uint8_t>(cell_target >> 8),
                                           static_cast<uint8_t>(cell_target >> 16), static_cast<uint8_t>(cell_target >> 24)});
    cell_caller.insert(cell_caller.end(), {0x48, 0x83, 0xc4, 0x38, RET});  // add rsp, 0x38; ret
    std::memcpy(at(GRASS_CELL_CALLER), cell_caller.data(), cell_caller.size());
    // Medium's own cascade writes, which the shadow row's check reads.
    std::memcpy(at(MEDIUM_CASCADES_ADDRESS), MEDIUM_CASCADES.data(), MEDIUM_CASCADES.size());
    // The two camera getters the grass draw's no-LOD return reads.
    std::memcpy(at(GET_SCENE_CAMERA), GET_SCENE_CAMERA_CODE.data(), GET_SCENE_CAMERA_CODE.size());
    std::memcpy(at(GET_CAMERA_POSITION), GET_CAMERA_POSITION_CODE.data(), GET_CAMERA_POSITION_CODE.size());
    // The window's call rel32 ends one byte before the window does.
    int32_t step_call = 0;
    std::memcpy(&step_call, at(step_end - 5), sizeof(step_call));
    *at(step_end - 1 + step_call) = RET;
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  };
  const auto site_untouched = [&](const Site& site) {
    return std::memcmp(at(site.address), site.window.data() + SITE_MARGIN, site.length) == 0;
  };
  const auto every_site_untouched = [&] { return std::ranges::all_of(SITES, site_untouched); };
  write_game();
  WriteTweakObjects();
  WriteLoaderGuards();

  // Identity.
  Expect(CheckIdentity(api) == STATUS_READY, "identity: accepted");
  {
    LoaderApi other = api;
    other.api_version = 0;
    Expect(CheckIdentity(other) == STATUS_WRONG_LOADER, "identity: api version 0");
    other = api;
    other.game_lo = 0x10000;
    Expect(CheckIdentity(other) == STATUS_WRONG_LOADER, "identity: another game start");
    other = api;
    other.game_hi = GAME_HI_MIN - 1;
    Expect(CheckIdentity(other) == STATUS_WRONG_LOADER, "identity: game range too short");
    other = api;
    other.api_version = 2;
    other.game_hi = GAME_HI_MIN + 0x1000;
    Expect(CheckIdentity(other) == STATUS_READY, "identity: a newer api and a longer range");
  }
  *at(BUILD_ID_NOTE_ADDRESS + 35) ^= 1;
  Expect(CheckIdentity(api) == STATUS_WRONG_GAME, "identity: another build id");
  *at(BUILD_ID_NOTE_ADDRESS + 35) ^= 1;
  *at(GAME_LO + 1) ^= 1;
  Expect(CheckIdentity(api) == STATUS_WRONG_GAME, "identity: no ELF magic");
  *at(GAME_LO + 1) ^= 1;

  // Block v2's size, and the per-site tables: one label per site, each label with its own hook (a swapped pair would
  // put a hook with another signature at a game site).
  Expect(VERSION == 2 && BlockBytes(0) == 0 && BlockBytes(1) == 40 && BlockBytes(2) == 216 && sizeof(Block) == 216,
         "block: v1 is 40 bytes, v2 216");
  {
    uint32_t labels_seen = 0;
    bool labels_ok = true;
    for (size_t index = 0; index < SITES.size(); ++index) {
      const Site& site = SITES[index];
      labels_ok = labels_ok && site.label == index && (labels_seen & (1u << site.label)) == 0
                  && (REPLACEMENTS[site.label] != nullptr) == (site.kind == SITE_HOOK || site.kind == SITE_MID)
                  && (MID_SITE_SCALES[site.label] != 0) == (site.kind == SITE_MID);
      labels_seen |= 1u << site.label;
    }
    Expect(labels_ok && labels_seen == (1u << LABEL_COUNT) - 1u && SITES.size() == LABEL_COUNT,
           "labels: one per site in label order, a hook for each hook and mid-function site (its scales for that "
           "one) and none for a patch site or a hand-made stub's");
    Expect(REPLACEMENTS[LABEL_PACK_ENTRY] == reinterpret_cast<void*>(&PackEntry)
               && REPLACEMENTS[LABEL_VIEW_DRAW_ENTRY] == reinterpret_cast<void*>(&ViewDrawEntry)
               && REPLACEMENTS[LABEL_STEP_ENTRY] == reinterpret_cast<void*>(&StepTaskEntry)
               && REPLACEMENTS[LABEL_UNIT_LOD_ENTRY] == reinterpret_cast<void*>(&UnitLodEntry)
               && REPLACEMENTS[LABEL_MODEL_LOD_ENTRY] == reinterpret_cast<void*>(&ModelLodEntry)
               && REPLACEMENTS[LABEL_GRASS_UPDATE_ENTRY] == reinterpret_cast<void*>(&GrassUpdateEntry)
               && REPLACEMENTS[LABEL_GRASS_DRAW_ENTRY] == reinterpret_cast<void*>(&GrassDrawEntry)
               && REPLACEMENTS[LABEL_GRASS_CELL_ENTRY] == reinterpret_cast<void*>(&GrassCellEntry)
               && REPLACEMENTS[LABEL_CAMPAIGN_TREE_ENTRY] == reinterpret_cast<void*>(&CampaignTreeEntry)
               && REPLACEMENTS[LABEL_SPRITE_DISTANCE] == reinterpret_cast<void*>(&SpriteDistanceHandler)
               && MID_SITE_SCALES[LABEL_SPRITE_DISTANCE] == ((1u << SCALE_SPRITE_DISTANCE) | (1u << SCALE_DEAD_UNITS))
               && REPLACEMENTS[LABEL_EFFECT_WALK_ENTRY] == reinterpret_cast<void*>(&EffectWalkEntry)
               && REPLACEMENTS[LABEL_PAIRS_ENTRY] == reinterpret_cast<void*>(&PairsEntry)
               && REPLACEMENTS[LABEL_CONTACT_ENTRY] == reinterpret_cast<void*>(&ContactEntry),
           "labels: each holds its own hook");
    Expect(LABEL_STEP_ENTRY == 2 && SITES[LABEL_STEP_ENTRY].row == ROW_CPU_PLACEMENT
               && SITES[ROW_BONE_FLUSH].label == LABEL_PACK_ENTRY && SITES[ROW_PASS_DRAW].label == LABEL_VIEW_DRAW_ENTRY
               && SITES[LABEL_UNIT_LOD_ENTRY].row == ROW_UNIT_LOD && SITES[LABEL_MODEL_LOD_ENTRY].row == ROW_MODEL_DETAIL
               && SITES[LABEL_GRASS_UPDATE_ENTRY].row == ROW_GRASS && SITES[LABEL_GRASS_DRAW_ENTRY].row == ROW_GRASS
               && SITES[LABEL_GRASS_CELL_ENTRY].row == ROW_GRASS_CELLS
               && SITES[LABEL_CAMPAIGN_TREE_ENTRY].row == ROW_CAMPAIGN_TREES
               && SITES[LABEL_SPRITE_DISTANCE].row == ROW_SPRITES
               && SITES[LABEL_EFFECT_WALK_ENTRY].row == ROW_PARALLEL_EFFECTS,
           "labels: the first sites are in row order");
    uint32_t shadow_sites = 0;
    for (const Site& site : SITES) {
      Expect((site.kind == SITE_PATCH) == (site.row == ROW_SHADOW_MAP), "kinds: the shadow row's sites are the patches");
      shadow_sites += (site.row == ROW_SHADOW_MAP) ? 1 : 0;
    }
    Expect(shadow_sites == 7 && SITES[LABEL_SPRITE_DISTANCE].kind == SITE_MID && JUMP_SITE_COUNT == HOOK_SITE_COUNT + 1
               && JUMP_SITE_COUNT == FAKE_GAME_SITE_COUNT - shadow_sites,
           "kinds: the shadow row's 7 immediates, the sprite row's mid-function site, every other site of the fake game "
           "a hook");
  }

  // Site checks on the real sites' windows.
  for (const Site& site : SITES) {
    Expect(CheckSite(site, FOREIGN).refusal == REFUSAL_NONE, "site: accepted with the shipped foreign table");
    for (const int64_t offset : {int64_t{-16}, int64_t{-1}, int64_t{site.length}, int64_t{site.length} + 15}) {
      *at(site.address + offset) ^= 0x5a;
      Expect(CheckSite(site, {}).refusal == REFUSAL_NEIGHBOUR, "site: a neighbour byte changed");
      *at(site.address + offset) ^= 0x5a;
    }
    *at(site.address + site.length - 1) ^= 0x5a;
    Expect(CheckSite(site, {}).refusal == REFUSAL_DIFFERS, "site: its own byte changed");
    *at(site.address + site.length - 1) ^= 0x5a;
    Site moved = site;
    ++moved.address;
    Expect(CheckSite(moved, {}).refusal != REFUSAL_NONE, "site: moved by one");

    const auto foreign_at = [&](uint32_t address, uint8_t length, ForeignKind kind) {
      const std::array<Foreign, 1> entry = {{{.address = address, .length = length, .owner = OWNER_OTHER, .kind = kind}}};
      return CheckSite(site, entry);
    };
    const SiteCheck before = foreign_at(site.address - 8, 5, FOREIGN_RANGE);
    Expect(before.refusal == REFUSAL_FOREIGN && before.owner == OWNER_OTHER, "foreign: a patch 8 bytes before");
    Expect(foreign_at(site.address + site.length + 8, 5, FOREIGN_RANGE).refusal == REFUSAL_FOREIGN, "foreign: 8 bytes after");
    Expect(foreign_at(site.address + site.length + 17, 5, FOREIGN_RANGE).refusal == REFUSAL_NONE, "foreign: 17 bytes after");
    Expect(foreign_at(site.address - 16 - 5, 5, FOREIGN_RANGE).refusal == REFUSAL_NONE, "foreign: ends where the window starts");
    Expect(foreign_at(site.address + 2, 0, FOREIGN_TARGET).refusal == REFUSAL_FOREIGN, "foreign: a return target inside");
    Expect(foreign_at(site.address, 0, FOREIGN_TARGET).refusal == REFUSAL_NONE, "foreign: a target at the entry");
    Expect(foreign_at(site.address + site.length, 0, FOREIGN_TARGET).refusal == REFUSAL_NONE, "foreign: a target after");
  }

  // Apply, snapshot, re-check; the loader's two failures.
  {
    const Site& site = SITES[ROW_BONE_FLUSH];
    AppliedSite applied;
    hook_mode = HOOK_NO_MEMORY;
    Expect(ApplySite(api, site, REPLACEMENTS[site.label], &applied) == REFUSAL_NO_MEMORY, "apply: no memory");
    hook_mode = HOOK_WRITE_FAILS;
    Expect(ApplySite(api, site, REPLACEMENTS[site.label], &applied) == REFUSAL_WRITE_FAILED, "apply: write failed");
    hook_mode = HOOK_NORMAL;
    write_game();
    Expect(ApplySite(api, site, REPLACEMENTS[site.label], &applied) == REFUSAL_NONE && applied.original != nullptr,
           "apply: hooked");
    Expect(applied.bytes[0] == 0xe9 && SiteUnchanged(site, applied), "apply: snapshot of the jump");
    Expect(CheckSite(site, {}).refusal == REFUSAL_DIFFERS, "apply: a second check sees our bytes");
    *at(site.address + 4) ^= 1;
    Expect(!SiteUnchanged(site, applied), "apply: a later change is seen");
  }

  // The patch kind: nothing to write, a write, the loader's refusal, and the way back.
  {
    write_game();
    const Site& site = SITES[LABEL_SHADOW_SIZE_MAX];
    const uint8_t* expect = site.window.data() + SITE_MARGIN;
    const std::array<float, 2> doubled = {4096.f, 4096.f};
    const auto* doubled_bytes = reinterpret_cast<const uint8_t*>(doubled.data());
    AppliedSite applied;
    Expect(ApplyPatch(api, site, nullptr, &applied) == REFUSAL_NONE && applied.applied && applied.original == nullptr
               && site_untouched(site) && SiteUnchanged(site, applied),
           "patch: nothing to write, the site is in with the game's bytes");
    Expect(RevertPatch(api, site, &applied) && !applied.applied && site_untouched(site), "patch: out again, nothing to put back");
    failing_patch_calls = 1u << 0;
    applied = {};
    Expect(ApplyPatch(api, site, doubled_bytes, &applied) == REFUSAL_WRITE_FAILED && !applied.applied && site_untouched(site),
           "patch: the loader's refusal leaves the game's bytes");
    Expect(ApplyPatch(api, site, doubled_bytes, &applied) == REFUSAL_NONE && applied.applied
               && std::memcmp(at(site.address), doubled_bytes, site.length) == 0 && SiteUnchanged(site, applied),
           "patch: written, snapshot of the new bytes");
    Expect(CheckSite(site, {}).refusal == REFUSAL_DIFFERS, "patch: a second check sees our bytes");
    Expect(CheckSite(SITES[LABEL_SHADOW_EXTRA_HIGH], {}).refusal == REFUSAL_NEIGHBOUR,
           "patch: a written size lies in the next cascade site's window (why every site is checked first)");
    *at(site.address + 3) ^= 1;
    Expect(!SiteUnchanged(site, applied), "patch: a later change is seen");
    *at(site.address + 3) ^= 1;
    Expect(RevertPatch(api, site, &applied) && !applied.applied && site_untouched(site)
               && std::memcmp(at(site.address), expect, site.length) == 0,
           "patch: the way back puts the game's bytes in again");
    Expect(ApplyPatch(api, site, doubled_bytes, &applied) == REFUSAL_NONE, "patch: written again");
    failing_patch_calls = 1u << 0;
    Expect(!RevertPatch(api, site, &applied) && !applied.applied && !site_untouched(site),
           "patch: a refused way back is reported, the site counts as out");
  }

  // The stack-check rule: the windows hold its result.
  {
    constexpr std::array<uint8_t, 16> before = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x64,
                                                0x48, 0x8b, 0x04, 0x25, 0x28, 0x00, 0x00, 0x00};
    constexpr std::array<uint8_t, 9> left_alone = {0x64, 0x48, 0x8b, 0x04, 0x25, 0x30, 0x00, 0x00, 0x00};
    std::memcpy(at(RULE_SITE - 16), before.data(), before.size());
    std::memcpy(at(RULE_SITE), SITES[0].window.data() + SITE_MARGIN, 5);
    std::memcpy(at(RULE_SITE + 5), left_alone.data(), left_alone.size());
    Site site = {.address = static_cast<uint32_t>(RULE_SITE), .length = 5, .row = ROW_BONE_FLUSH, .label = LABEL_PACK_ENTRY};
    std::memcpy(site.window.data(), at(RULE_SITE - SITE_MARGIN), (2 * SITE_MARGIN) + 5);
    site.window[7] = 0x65;
    Expect(CheckSite(site, {}).refusal == REFUSAL_NEIGHBOUR, "stack rule: the raw file bytes differ from the window");
    Expect(ApplyStackRule(at(RULE_SITE - 16), at(RULE_SITE + 16 + 5)) == 1, "stack rule: one read rewritten");
    Expect(CheckSite(site, {}).refusal == REFUSAL_NONE, "stack rule: the rewritten bytes match");
  }

  // The hook bodies, called through the real sites as the game calls them.
  auto* pack_record = reinterpret_cast<PackRecord*>(PACK_RECORD);
  auto* draw_record = reinterpret_cast<DrawRecord*>(DRAW_RECORD);
  const auto pack = reinterpret_cast<PackFn>(SITES[ROW_BONE_FLUSH].address);
  const auto draw = reinterpret_cast<ViewDrawFn>(SITES[ROW_PASS_DRAW].address);
  auto* step_record = reinterpret_cast<StepRecord*>(STEP_RECORD);
  const auto step = reinterpret_cast<StepTaskFn>(SITES[ROW_CPU_PLACEMENT].address);
  std::array<uint64_t, (STEP_TASK_FIELD / sizeof(uint64_t)) + 1> task = {};
  task.back() = 0x7a5c;
  const auto step_once = [&] {
    step_record->task_field = 0;
    return step(task.data()) == reinterpret_cast<void*>(STEP_RESULT) && step_record->task_field == task.back();
  };
  const auto default_cpu_sets = [] {
    std::array<ULONG, 256> ids = {};
    ULONG count = 0;
    GetProcessDefaultCpuSets(GetCurrentProcess(), ids.data(), static_cast<ULONG>(ids.size()), &count);
    return std::pair{ids, count};
  };
  const auto core_state_of = [](const Block* shared) {
    return static_cast<uint32_t>(shared->core_status.load() >> CORE_STATE_SHIFT) & CORE_FIELD_MASK;
  };
  const auto pack_once = [&] {
    uint32_t cursor = 0;
    uint32_t written = 0;
    const bool result = pack(reinterpret_cast<void*>(0x1111), reinterpret_cast<void*>(0x2222), true,
                             reinterpret_cast<void*>(0x4444), 0x5555, &cursor, 0x7777, &written);
    return result && pack_record->arguments[0] == 0x1111 && pack_record->arguments[1] == 0x2222
           && (pack_record->arguments[2] & 0xff) == 1 && pack_record->arguments[3] == 0x4444
           && static_cast<uint32_t>(pack_record->arguments[4]) == 0x5555
           && pack_record->arguments[5] == reinterpret_cast<uint64_t>(&cursor)
           && static_cast<uint32_t>(pack_record->arguments[6]) == 0x7777
           && pack_record->arguments[7] == reinterpret_cast<uint64_t>(&written);
  };
  std::array<void*, VIEW_KIND_SLOT + 1> slots = {};
  slots[VIEW_KIND_SLOT] = reinterpret_cast<void*>(&FakeViewKind);
  FakeView view = {.vtable = slots.data(), .kind = 7};
  auto* unit_lod_record = reinterpret_cast<UnitLodRecord*>(UNIT_LOD_RECORD);
  auto* model_lod_record = reinterpret_cast<ModelLodRecord*>(MODEL_LOD_RECORD);
  const auto rest_pack = reinterpret_cast<PackFn>(REST_PACK_RETURN - 5 - CALLER_CALL_OFFSET);
  const auto unit_lod = reinterpret_cast<UnitLodFn>(CALLER_THUNKS);
  const auto model_lod = reinterpret_cast<ModelLodFn>(CALLER_THUNKS + 0x100);
  const auto forget_callee_saved = [&] { std::memset(at(CALLER_SAVED), 0, sizeof(CALLER_SENTINELS)); };
  const auto callee_saved_kept = [&] {
    return std::memcmp(at(CALLER_SAVED), CALLER_SENTINELS.data(), sizeof(CALLER_SENTINELS)) == 0;
  };
  const auto same_bits = [](float left, float right) { return std::memcmp(&left, &right, sizeof(left)) == 0; };
  // The game's 18 distances: plain values, a NaN, a huge one and an infinity.
  const std::array<float, 18> game_distances = {
      120.f, 240.f, 360.f, 480.f, 5.f, 6.f, std::numeric_limits<float>::quiet_NaN(), 8.f, 900.f,
      1000.f, 1100.f, 1200.f, 13.f, 14.f, 15.f, 3.0e38f, 10000.f, std::numeric_limits<float>::infinity()};
  // Whole registers: the high halves must reach the game too. `fourth`'s low byte picks the game's first branch.
  const std::array<uint64_t, 4> lod_settings = {0x1111000000000003, 0x2222000000000004, 0x3333000000000005, 0x4444000000000000};
  std::array<float, 18> lod_table = {};
  // One call through the fake game's caller; true when the function got its arguments and the caller its result.
  const auto unit_lod_once = [&](uint64_t fourth_low_byte) {
    forget_callee_saved();
    unit_lod_record->distances = game_distances;
    unit_lod_record->return_address = 0;
    lod_table = {};
    const float* result = unit_lod(lod_table.data(), reinterpret_cast<void*>(0x7777), lod_settings[0], lod_settings[1],
                                   lod_settings[2], lod_settings[3] | fourth_low_byte);
    return result == lod_table.data() && callee_saved_kept()
           && unit_lod_record->arguments
                  == std::array<uint64_t, 6>{reinterpret_cast<uint64_t>(lod_table.data()), 0x7777, lod_settings[0],
                                             lod_settings[1], lod_settings[2], lod_settings[3] | fourth_low_byte};
  };
  std::array<uint8_t, 0x60> lod_entry = {};
  uint64_t next_lod = 0;
  const auto model_lod_once = [&](float distance_squared) {
    forget_callee_saved();
    next_lod = 0x5a5a;
    model_lod_record->return_address = 0;
    const void* result = model_lod(lod_entry.data(), &next_lod, distance_squared);
    // The game's own first instructions ran (they clear the next-LOD slot) and it got both pointers.
    return result == lod_entry.data() && callee_saved_kept() && next_lod == 0
           && model_lod_record->entry == reinterpret_cast<uint64_t>(lod_entry.data())
           && model_lod_record->next_lod == reinterpret_cast<uint64_t>(&next_lod);
  };
  const auto set_scale = [](Block* shared, uint32_t scale, float value) {
    shared->scales[scale].store(value);
    if (value == NEUTRAL_SCALES[scale]) {
      shared->scales_active.fetch_and(~(1u << scale));
    } else {
      shared->scales_active.fetch_or(1u << scale);
    }
  };
  std::array<uint8_t, 0x40> node = {};
  const auto draw_once = [&] {
    draw(node.data(), reinterpret_cast<void*>(0x2222), &view);
    return draw_record->arguments[0] == reinterpret_cast<uint64_t>(node.data()) && draw_record->arguments[1] == 0x2222
           && draw_record->arguments[2] == reinterpret_cast<uint64_t>(&view);
  };
  // The fake scrub for the grass and campaign tree rows: a model node with four models (0: a grass clump with LODs at
  // 50 and 100 m, 1: a plant with a LOD at 20 m and a switched-off one at 1000 m, 2: a campaign tree with one LOD at
  // 300 m, 3: no LODs), a scene with its camera at (10, 20, 30), and the scrub (radius 200 m, bias 200) whose two slots
  // are the models 0 and 1.
  constexpr std::array<float, 5> GAME_LODS = {2500.f, 10000.f, 400.f, 1.0e6f, 90000.f};
  std::array<std::array<uint8_t, 0x60>, 5> lod_entries = {};
  std::array<uint8_t*, 2> clump_lods = {lod_entries[0].data(), lod_entries[1].data()};
  std::array<uint8_t*, 2> plant_lods = {lod_entries[2].data(), lod_entries[3].data()};
  std::array<uint8_t*, 1> tree_lods = {lod_entries[4].data()};
  std::array<std::array<uint8_t, 0x50>, 4> models = {};
  std::array<uint8_t*, 4> model_list = {models[0].data(), models[1].data(), models[2].data(), models[3].data()};
  std::array<uint8_t, 0xea0> model_node = {};
  std::array<uintptr_t, SCENE_CAMERA_SLOT + 1> scene_slots = {};
  std::array<uintptr_t, CAMERA_POSITION_SLOT + 1> camera_slots = {};
  std::array<uint8_t, 0xa0> scene = {};
  std::array<uint32_t, 2> scrub_slots = {0, 1};
  std::array<uint8_t, 0x100> scrub = {};
  const std::array<float, 3> camera_position = {10.f, 20.f, 30.f};
  const auto put = [](uint8_t* object, size_t offset, auto value) { std::memcpy(object + offset, &value, sizeof(value)); };
  const auto reset_scrub = [&] {
    for (size_t entry = 0; entry < lod_entries.size(); ++entry) {
      lod_entries[entry] = {};
      put(lod_entries[entry].data(), model_lods::LOD_DISTANCE_SQUARED, GAME_LODS[entry]);
      lod_entries[entry][model_lods::LOD_ENABLED] = ((entry == 3) ? 0 : 1);
    }
    models = {};
    put(models[0].data(), model_lods::MODEL_LOD_COUNT, uint32_t{2});
    put(models[0].data(), model_lods::MODEL_LODS, clump_lods.data());
    put(models[1].data(), model_lods::MODEL_LOD_COUNT, uint32_t{2});
    put(models[1].data(), model_lods::MODEL_LODS, plant_lods.data());
    put(models[2].data(), model_lods::MODEL_LOD_COUNT, uint32_t{1});
    put(models[2].data(), model_lods::MODEL_LODS, tree_lods.data());
    model_node = {};
    put(model_node.data(), model_lods::NODE_MODEL_COUNT, static_cast<uint32_t>(model_list.size()));
    put(model_node.data(), model_lods::NODE_MODELS, model_list.data());
    put(model_node.data(), NODE_SCENE, scene.data());
    scene_slots[SCENE_CAMERA_SLOT] = GET_SCENE_CAMERA;
    camera_slots[CAMERA_POSITION_SLOT] = GET_CAMERA_POSITION;
    scene = {};
    put(scene.data(), 0, scene_slots.data());
    put(scene.data(), SCENE_CAMERA, camera_slots.data());
    put(scene.data(), SCENE_CAMERA + CAMERA_POSITION, camera_position);
    scrub = {};
    put(scrub.data(), SCRUB_RADIUS, int32_t{200});
    put(scrub.data(), SCRUB_BIAS, 200.f);
    put(scrub.data(), SCRUB_NODE, model_node.data());
    put(scrub.data(), SCRUB_SLOT_COUNT, static_cast<uint32_t>(scrub_slots.size()));
    put(scrub.data(), SCRUB_SLOTS, scrub_slots.data());
  };
  const auto lods_are = [&](const std::array<float, 5>& expected) {
    for (size_t entry = 0; entry < lod_entries.size(); ++entry) {
      if (std::memcmp(lod_entries[entry].data() + model_lods::LOD_DISTANCE_SQUARED, &expected[entry], sizeof(float)) != 0) return false;
    }
    return true;
  };
  const auto scrub_holds = [&](int32_t radius, float bias) {
    return std::memcmp(scrub.data() + SCRUB_RADIUS, &radius, sizeof(radius)) == 0
           && std::memcmp(scrub.data() + SCRUB_BIAS, &bias, sizeof(bias)) == 0;
  };
  auto* grass_update_record = reinterpret_cast<GrassUpdateRecord*>(GRASS_UPDATE_RECORD);
  auto* grass_draw_record = reinterpret_cast<GrassDrawRecord*>(GRASS_DRAW_RECORD);
  auto* grass_cell_record = reinterpret_cast<GrassCellRecord*>(GRASS_CELL_RECORD);
  auto* campaign_tree_record = reinterpret_cast<CampaignTreeRecord*>(CAMPAIGN_TREE_RECORD);
  const auto grass_update = reinterpret_cast<GrassUpdateFn>(GRASS_UPDATE_CALL - CALLER_CALL_OFFSET);
  const auto grass_draw = reinterpret_cast<GrassDrawFn>(GRASS_DRAW_CALL - CALLER_CALL_OFFSET);
  const auto grass_cell = reinterpret_cast<GrassCellFn>(GRASS_CELL_CALLER);
  const auto campaign_tree = reinterpret_cast<CampaignTreeFn>(CAMPAIGN_TREE_CALL - CALLER_CALL_OFFSET);
  // One update through the fake game's caller; true when the game's update ran as a tail call, with the scrub, and saw
  // the radius and the bias the scrub holds now.
  const auto grass_update_once = [&] {
    forget_callee_saved();
    grass_update_record->return_address = 0;
    grass_update(scrub.data());
    return callee_saved_kept() && grass_update_record->scrub == reinterpret_cast<uint64_t>(scrub.data())
           && grass_update_record->return_address == GRASS_UPDATE_CALL + 5
           && std::memcmp(&grass_update_record->radius, scrub.data() + SCRUB_RADIUS, 2 * sizeof(uint32_t)) == 0;
  };
  // One instance of model `index`, `distance` metres from the camera; true when the game's draw ran as a tail call with
  // its whole registers (`reaches_game`), or did not run at all.
  const auto grass_draw_once = [&](uint32_t index, float distance, float bias, bool reaches_game) {
    forget_callee_saved();
    const uint32_t calls_before = grass_draw_record->calls;
    grass_draw_record->arguments = {};
    grass_draw_record->return_address = 0;
    std::array<float, 12> transform = {};
    transform[3] = camera_position[0] + distance;
    transform[7] = camera_position[1];
    transform[11] = camera_position[2];
    std::array<uint8_t, 0x20> params = {};
    put(params.data(), PARAMS_BIAS, bias);
    grass_draw(model_node.data(), &index, transform.data(), reinterpret_cast<void*>(0x4444), params.data(), 0x6666000000000001);
    if (!reaches_game) return callee_saved_kept() && grass_draw_record->calls == calls_before;
    return callee_saved_kept() && grass_draw_record->calls == calls_before + 1
           && grass_draw_record->return_address == GRASS_DRAW_CALL + 5
           && grass_draw_record->arguments
                  == std::array<uint64_t, 6>{reinterpret_cast<uint64_t>(model_node.data()), reinterpret_cast<uint64_t>(&index),
                                             reinterpret_cast<uint64_t>(transform.data()), 0x4444,
                                             reinterpret_cast<uint64_t>(params.data()), 0x6666000000000001};
  };
  // One cell whose records have these slots; true when the game's loop ran as a tail call with the registers and the
  // clip state as they came and `expected` as its radius squared, bit for bit.
  const auto grass_cell_once = [&](const uint8_t* cell_scrub, std::initializer_list<uint8_t> record_slots, float radius_squared,
                                   float expected) {
    std::vector<uint8_t> records(record_slots.size() * RECORD_BYTES);
    for (size_t record = 0; record < record_slots.size(); ++record) {
      records[(record * RECORD_BYTES) + RECORD_SLOT] = record_slots.begin()[record];
    }
    std::array<uint8_t, 0x30> cell = {};
    put(cell.data(), CELL_RECORD_COUNT, static_cast<uint32_t>(record_slots.size()));
    put(cell.data(), CELL_RECORDS, records.data());
    const CellClip clip = {{0x1111111111111111, 0x2222222222222222, 0x3333333333333333, 0x4444444444444444,
                            0x5555555555555555, 0x6666666666666666}};
    const uint32_t calls_before = grass_cell_record->calls;
    grass_cell_record->return_address = 0;
    grass_cell_record->clip = {};
    grass_cell(cell_scrub, cell.data(), camera_position.data(), clip, radius_squared, 0x7777000000000001);
    return grass_cell_record->calls == calls_before + 1 && grass_cell_record->return_address == GRASS_CELL_CALL + 5
           && same_bits(grass_cell_record->radius_squared, expected) && grass_cell_record->clip == clip.state
           && grass_cell_record->arguments
                  == std::array<uint64_t, 4>{reinterpret_cast<uint64_t>(cell_scrub), reinterpret_cast<uint64_t>(cell.data()),
                                             reinterpret_cast<uint64_t>(camera_position.data()), 0x7777000000000001};
  };
  // One campaign tree (model 2); true when the game's draw ran as a tail call with its registers and both floats, and
  // its result came back.
  const auto campaign_tree_once = [&] {
    forget_callee_saved();
    const uint32_t tree = 2;
    const uint32_t calls_before = campaign_tree_record->calls;
    campaign_tree_record->return_address = 0;
    const void* result = campaign_tree(model_node.data(), &tree, reinterpret_cast<void*>(0x3333), 0x4444000000000004,
                                       0x5555000000000005, 0x6666000000000006, 1.25f, -7.5f);
    return result == campaign_tree_record && callee_saved_kept() && campaign_tree_record->calls == calls_before + 1
           && campaign_tree_record->return_address == CAMPAIGN_TREE_CALL + 5
           && same_bits(campaign_tree_record->first_float, 1.25f) && same_bits(campaign_tree_record->second_float, -7.5f)
           && campaign_tree_record->arguments
                  == std::array<uint64_t, 6>{reinterpret_cast<uint64_t>(model_node.data()), reinterpret_cast<uint64_t>(&tree),
                                             0x3333, 0x4444000000000004, 0x5555000000000005, 0x6666000000000006};
  };
  // Every path of the bodies, through the given block (this exe's hooks.hpp copy or a DLL copy's).
  const auto expect_hook_bodies = [&](Block* shared) {
    active_block = shared;
    *pack_record = {};
    *draw_record = {};
    *step_record = {};
    before_pack_calls = 0;
    pass_begin_calls = 0;
    pass_end_calls = 0;
    shared->hooks_wanted.store(0);
    shared->callbacks.store(nullptr);

    Expect(pack_once() && pack_record->calls == 1 && before_pack_calls == 0, "pack: bit clear, original with its arguments");
    Expect(Logged("PACK_ENTRY: first call") && !Logged("VIEW_DRAW_ENTRY: first call"), "pack: its own first-call line only");
    shared->hooks_wanted.store(RowBit(ROW_BONE_FLUSH));
    Expect(pack_once() && pack_record->calls == 2 && before_pack_calls == 0, "pack: no callbacks registered");
    shared->callbacks.store(&CALLBACKS);
    Expect(pack_once() && pack_record->calls == 3 && before_pack_calls == 1, "pack: before_pack, then the original");
    Expect(shared->calls_in_flight.load() == 0, "pack: guard closed");

    view.kind = 7;
    node[NODE_ENABLED_OFFSET] = 1;
    Expect(draw_once() && draw_record->calls == 1 && pass_begin_calls == 0, "draw: bit clear");
    shared->hooks_wanted.store(RowBit(ROW_BONE_FLUSH) | RowBit(ROW_PASS_DRAW));
    shared->callbacks.store(nullptr);
    Expect(draw_once() && draw_record->calls == 2 && pass_begin_calls == 0, "draw: bit set, no callbacks registered");
    shared->callbacks.store(&CALLBACKS);
    node[NODE_ENABLED_OFFSET] = 0;
    Expect(draw_once() && draw_record->calls == 3 && pass_begin_calls == 0, "draw: a disabled node never reaches the add-on");
    node[NODE_ENABLED_OFFSET] = 1;
    next_action = PASS_SKIP;
    draw(node.data(), nullptr, &view);
    Expect(draw_record->calls == 3 && pass_begin_calls == 1 && last_kind == 7 && pass_end_calls == 0, "draw: skipped");
    next_action = PASS_DRAW;
    Expect(draw_once() && draw_record->calls == 4 && pass_begin_calls == 2 && pass_end_calls == 0, "draw: drawn");
    next_action = PASS_DRAW_MEASURED;
    view.kind = 9;
    Expect(draw_once() && draw_record->calls == 5 && pass_end_calls == 1 && end_measure_ok && last_kind == 9,
           "draw: measured, PassMeasure round trip");
    swap_in_begin = &OTHER_CALLBACKS;
    Expect(draw_once() && draw_record->calls == 6 && pass_begin_calls == 4 && pass_end_calls == 1,
           "draw: another Callbacks registered between begin and end, no end");
    swap_in_begin = nullptr;
    shared->callbacks.store(&CALLBACKS);
    Expect(draw_once() && pass_end_calls == 2 && shared->calls_in_flight.load() == 0, "draw: measured again, guard closed");
    Expect(Logged("VIEW_DRAW_ENTRY: first call"), "draw: first-call line");
    shared->hooks_wanted.store(0);
    shared->callbacks.store(nullptr);

    // The rest pack: the same hook, called from the game's second pack call, leaves its sticky notice.
    {
      uint32_t cursor = 0;
      const uint32_t packs_before = pack_record->calls;
      forget_callee_saved();
      Expect(shared->notices.load() == 0, "rest pack: no notice from the first pack's calls");
      Expect(rest_pack(reinterpret_cast<void*>(0x1111), reinterpret_cast<void*>(0x2222), true, reinterpret_cast<void*>(0x4444),
                       0x5555, &cursor, 0, nullptr)
                 && pack_record->calls == packs_before + 1 && pack_record->arguments[0] == 0x1111 && callee_saved_kept()
                 && shared->notices.load() == NOTICE_REST_PACK,
             "rest pack: the notice bit, and the game's pack still runs");
      Expect(pack_once() && shared->notices.load() == NOTICE_REST_PACK, "rest pack: the bit stays");
    }

    // The unit LOD table. Neutral: the game's call as it came (its return address is the caller's, so no frame of ours),
    // its 18 floats bit for bit. Active: only the entries whose own bit is set change.
    *unit_lod_record = {};
    Expect(!Logged("UNIT_LOD_ENTRY: first call") && unit_lod_once(1) && unit_lod_record->calls == 1
               && Logged("UNIT_LOD_ENTRY: first call"),
           "unit lod: neutral, the game's function with its whole registers, its result back, its own first-call line");
    Expect(unit_lod_record->return_address == UNIT_LOD_CALL + 5
               && std::memcmp(lod_table.data(), game_distances.data(), sizeof(lod_table)) == 0,
           "unit lod: neutral is a tail call and the game's bits");
    Expect(unit_lod_once(0) && unit_lod_record->return_address == UNIT_LOD_CALL + 5
               && std::memcmp(lod_table.data(), game_distances.data(), sizeof(lod_table)) == 0,
           "unit lod: neutral on the game's other branch");
    // A scale without its bit is not applied; a bit of another row leaves this one neutral.
    shared->scales[3].store(2.f);
    set_scale(shared, SCALE_MODEL_DETAIL, 0.25f);
    Expect(unit_lod_once(1) && unit_lod_record->return_address == UNIT_LOD_CALL + 5
               && std::memcmp(lod_table.data(), game_distances.data(), sizeof(lod_table)) == 0,
           "unit lod: another row's bit and a scale without its bit change nothing");
    shared->scales[3].store(1.f);
    set_scale(shared, SCALE_MODEL_DETAIL, 1.f);
    set_scale(shared, 0, 8.f);
    set_scale(shared, 6, 2.f);
    set_scale(shared, 12, 0.5f);
    set_scale(shared, 15, 8.f);
    set_scale(shared, 16, 0.f);
    {
      const bool called = unit_lod_once(1);
      bool entries_ok = true;
      for (size_t index = 0; index < lod_table.size(); ++index) {
        float expected = game_distances[index];
        if (index == 0) expected = game_distances[index] * 8.f;
        if (index == 12) expected = game_distances[index] * 0.5f;
        if (index == 15) expected = FLT_MAX;
        if (index == 16) expected = 0.f;
        // Entry 6 is the game's NaN x 2: still its NaN.
        entries_ok = entries_ok && same_bits(lod_table[index], expected);
      }
      Expect(called && entries_ok && unit_lod_record->calls == 4 && unit_lod_record->return_address != UNIT_LOD_CALL + 5,
             "unit lod: active, min(distance x scale, FLT_MAX) on the entries with a bit, the others the game's bits");
    }
    for (const uint32_t scale : {0u, 6u, 12u, 15u, 16u}) {
      set_scale(shared, scale, 1.f);
    }
    Expect(shared->scales_active.load() == 0 && unit_lod_once(1) && unit_lod_record->return_address == UNIT_LOD_CALL + 5
               && std::memcmp(lod_table.data(), game_distances.data(), sizeof(lod_table)) == 0,
           "unit lod: back at 1x, the game's bits again");

    // The model LOD pick: only the distance changes, and only with its bit; always a tail call.
    *model_lod_record = {};
    Expect(!Logged("MODEL_LOD_ENTRY: first call") && model_lod_once(1234.5f) && model_lod_record->calls == 1
               && same_bits(model_lod_record->distance_squared, 1234.5f) && model_lod_record->return_address == MODEL_LOD_CALL + 5
               && Logged("MODEL_LOD_ENTRY: first call"),
           "model lod: neutral, the game's pick as it came, its own first-call line");
    shared->scales[SCALE_MODEL_DETAIL].store(0.25f);
    set_scale(shared, 0, 8.f);
    Expect(model_lod_once(1234.5f) && same_bits(model_lod_record->distance_squared, 1234.5f)
               && model_lod_record->return_address == MODEL_LOD_CALL + 5,
           "model lod: another row's bit and a scale without its bit change nothing");
    set_scale(shared, 0, 1.f);
    set_scale(shared, SCALE_MODEL_DETAIL, 0.25f);
    Expect(model_lod_once(1234.5f) && same_bits(model_lod_record->distance_squared, 1234.5f * 0.25f)
               && model_lod_record->return_address == MODEL_LOD_CALL + 5 && model_lod_record->calls == 3,
           "model lod: active, the distance x the scale, still a tail call");
    set_scale(shared, SCALE_MODEL_DETAIL, 1.f);
    Expect(model_lod_once(1234.5f) && same_bits(model_lod_record->distance_squared, 1234.5f) && shared->scales_active.load() == 0,
           "model lod: back at 1x");

    // The grass and campaign tree rows on the fake scrub. Neutral: each call goes on as it came (its return address is
    // the caller's, so no frame of ours) and nothing of the game's is written.
    reset_scrub();
    *grass_update_record = {};
    *grass_draw_record = {};
    *grass_cell_record = {};
    *campaign_tree_record = {};
    grass_draw_code_read = true;  // this file's copy has no init; the DLL's own found the fake draw's hash
    const uint32_t rows_applied_before = shared->rows_applied;
    shared->rows_applied |= RowBit(ROW_GRASS);
    Expect(!Logged("GRASS_UPDATE_ENTRY: first call") && grass_update_once() && grass_update_record->calls == 1
               && scrub_holds(200, 200.f) && lods_are(GAME_LODS) && Logged("GRASS_UPDATE_ENTRY: first call"),
           "grass update: neutral, the game's update as it came, its own first-call line");
    Expect(!Logged("GRASS_DRAW_ENTRY: first call") && grass_draw_once(1, 50.f, 0.f, true) && lods_are(GAME_LODS)
               && Logged("GRASS_DRAW_ENTRY: first call"),
           "grass draw: neutral, an instance past every LOD still reaches the game's draw");
    Expect(!Logged("GRASS_CELL_ENTRY: first call") && grass_cell_once(scrub.data(), {1, 1}, 40401.5f, 40401.5f)
               && Logged("GRASS_CELL_ENTRY: first call"),
           "grass cell: neutral, the radius, the clip state on the stack and the registers as they came");
    Expect(!Logged("CAMPAIGN_TREE_ENTRY: first call") && campaign_tree_once() && lods_are(GAME_LODS)
               && Logged("CAMPAIGN_TREE_ENTRY: first call"),
           "campaign tree: neutral, the game's draw as it came, its result back");
    shared->scales[SCALE_GRASS_LOD].store(4.f);
    shared->scales[SCALE_CAMPAIGN_TREES].store(4.f);
    set_scale(shared, SCALE_MODEL_DETAIL, 0.25f);
    Expect(grass_update_once() && scrub_holds(200, 200.f) && grass_draw_once(1, 50.f, 0.f, true)
               && grass_cell_once(scrub.data(), {1}, 40401.f, 40401.f) && campaign_tree_once() && lods_are(GAME_LODS),
           "grass, campaign tree: another row's bit and scales without their bits change nothing");
    shared->scales[SCALE_GRASS_LOD].store(1.f);
    shared->scales[SCALE_CAMPAIGN_TREES].store(1.f);
    set_scale(shared, SCALE_MODEL_DETAIL, 1.f);

    // The row out of rows_applied (its second hook was refused at init, its first one stays in): its scales active,
    // and neither hook does anything, nor does it stay switched on for later.
    shared->rows_applied &= ~RowBit(ROW_GRASS);
    set_scale(shared, SCALE_GRASS_LOD, 4.f);
    set_scale(shared, SCALE_GRASS_RADIUS, 2.f);
    set_scale(shared, SCALE_GRASS_DENSITY, 4.f);
    Expect(grass_update_once() && scrub_holds(200, 200.f) && grass_draw_once(1, 50.f, 0.f, true) && lods_are(GAME_LODS)
               && grass_cell_once(scrub.data(), {1}, 40401.f, 40401.f),
           "grass: active scales with the row not applied write nothing and skip no draw");
    for (const uint32_t scale : {SCALE_GRASS_LOD, SCALE_GRASS_RADIUS, SCALE_GRASS_DENSITY}) {
      set_scale(shared, scale, 1.f);
    }
    shared->rows_applied |= RowBit(ROW_GRASS);
    Expect(grass_update_once() && scrub_holds(200, 200.f) && grass_draw_once(1, 50.f, 0.f, true) && lods_are(GAME_LODS),
           "grass: the row applied and neutral again, still the game's calls (the refused calls left nothing switched on)");

    // Active: Grass Distance 2x (LOD scale 4, radius scale 2), Grass Density 4x. The radius is 400 m cut to the grass
    // model's reach (200 m + 1), the bias 200 x 2 / 4, the scrub's models' LODs x 4; the tree's stay.
    set_scale(shared, SCALE_GRASS_LOD, 4.f);
    set_scale(shared, SCALE_GRASS_RADIUS, 2.f);
    set_scale(shared, SCALE_GRASS_DENSITY, 4.f);
    Expect(grass_update_once() && scrub_holds(201, 100.f) && lods_are({10000.f, 40000.f, 1600.f, 4.0e6f, 90000.f}),
           "grass update: active, radius and bias from the game's values, the scrub's model LODs scaled, a tail call");
    Expect(grass_cell_once(scrub.data(), {1, 1}, 40401.f, 1681.f) && grass_cell_once(scrub.data(), {1}, 100.f, 100.f),
           "grass cell: a cell of the near plant alone is cut to its reach (40 m + 1), never raised");
    Expect(grass_cell_once(scrub.data(), {1, 0}, 40401.f, 40401.f) && grass_cell_once(scrub.data(), {5}, 40401.f, 40401.f)
               && grass_cell_once(model_node.data(), {1}, 40401.f, 40401.f),
           "grass cell: a cell with the far model, an unknown slot or another scrub keeps the game's radius");
    Expect(grass_draw_once(1, 30.f, 0.f, true) && grass_draw_once(1, 50.f, 0.f, false) && grass_draw_once(1, 40.f, 0.f, false)
               && grass_draw_once(1, 30.f, 800.f, false),
           "grass draw: drawn inside the scaled LOD; past it, at it, or pushed past it by the bias the call is left out");
    put(model_node.data(), NODE_LOD_OFFSET, -200.f);
    Expect(grass_draw_once(1, 30.f, 800.f, true), "grass draw: the node's offset is part of the key");
    put(model_node.data(), NODE_LOD_OFFSET, 0.f);
    {
      // A model without LODs: the game returns before it reads the instance's bias, so no bias is read here either.
      const uint32_t no_lods = 3;
      const uint32_t out_of_range = 99;
      const std::array<float, 12> transform = {};
      const uint32_t draws_before = grass_draw_record->calls;
      grass_draw(model_node.data(), &no_lods, transform.data(), nullptr, nullptr, 0);
      Expect(grass_draw_record->calls == draws_before, "grass draw: a model without LODs draws nothing");
      grass_draw(model_node.data(), &out_of_range, transform.data(), nullptr, nullptr, 0);
      Expect(grass_draw_record->calls == draws_before + 1, "grass draw: no model at the index is the game's to handle");
    }
    camera_slots[CAMERA_POSITION_SLOT] = 0;
    Expect(grass_draw_once(1, 50.f, 0.f, true), "grass draw: another camera position getter, every call reaches the game");
    camera_slots[CAMERA_POSITION_SLOT] = GET_CAMERA_POSITION;
    scene_slots[SCENE_CAMERA_SLOT] = 0;
    Expect(grass_draw_once(1, 50.f, 0.f, true), "grass draw: another scene camera getter, every call reaches the game");
    scene_slots[SCENE_CAMERA_SLOT] = GET_SCENE_CAMERA;
    *at(GET_CAMERA_POSITION + 4) ^= 1;
    Expect(grass_update_once() && grass_draw_once(1, 50.f, 0.f, true),
           "grass draw: a getter whose code changed (read at the update), every call reaches the game");
    *at(GET_CAMERA_POSITION + 4) ^= 1;
    Expect(grass_update_once() && grass_draw_once(1, 50.f, 0.f, false), "grass draw: the code back, the call left out again");
    if (shared == &block) {
      grass_draw_code_read = false;
      Expect(grass_draw_once(1, 50.f, 0.f, true) && grass_draw_once(1, 30.f, 0.f, true),
             "grass draw: the draw's own code not as read at init, every call reaches the game");
      grass_draw_code_read = true;
    }

    // The game sets its own values again: anything but the last written value is the game's.
    put(scrub.data(), SCRUB_BIAS, 50.f);
    put(lod_entries[2].data(), model_lods::LOD_DISTANCE_SQUARED, 900.f);
    Expect(grass_update_once() && scrub_holds(201, 25.f) && lods_are({10000.f, 40000.f, 3600.f, 4.0e6f, 90000.f}),
           "grass update: a value the game set again is the new base");
    // Density alone moves no reach: the game's radius, uncut.
    set_scale(shared, SCALE_GRASS_LOD, 1.f);
    set_scale(shared, SCALE_GRASS_RADIUS, 1.f);
    Expect(grass_update_once() && scrub_holds(200, 12.5f) && lods_are({2500.f, 10000.f, 900.f, 1.0e6f, 90000.f})
               && grass_cell_once(scrub.data(), {1, 1}, 40401.f, 40401.f),
           "grass: density alone, the game's radius and LODs, the bias / density, no cell cut");
    set_scale(shared, SCALE_GRASS_DENSITY, 1.f);
    Expect(shared->scales_active.load() == 0 && grass_update_once() && scrub_holds(200, 50.f)
               && lods_are({2500.f, 10000.f, 900.f, 1.0e6f, 90000.f}) && grass_cell_once(scrub.data(), {1, 1}, 40401.f, 40401.f),
           "grass: back at 1x, the game's bits in the scrub and in every LOD");
    Expect(grass_draw_once(1, 20.f, 0.f, true) && grass_draw_once(1, 50.f, 0.f, false) && lods_are({2500.f, 10000.f, 900.f, 1.0e6f, 90000.f}),
           "grass draw: back at 1x, the game's own pick; a draw the game would return from is still left out");

    // Two scrubs updating in turn, each with its own state: neither's written radius or bias is ever taken for the
    // game's (the bias would grow by scale / density at every turn).
    {
      std::array<uint8_t, 0x100> second_scrub = scrub;
      put(second_scrub.data(), SCRUB_RADIUS, int32_t{300});
      put(second_scrub.data(), SCRUB_BIAS, 80.f);
      const auto both_hold = [&](int32_t radius, float bias, int32_t second_radius, float second_bias) {
        bool ok = true;
        for (int turn = 0; turn < 3; ++turn) {
          grass_update(scrub.data());
          grass_update(second_scrub.data());
          ok = ok && scrub_holds(radius, bias) && std::memcmp(second_scrub.data() + SCRUB_RADIUS, &second_radius, 4) == 0
               && std::memcmp(second_scrub.data() + SCRUB_BIAS, &second_bias, 4) == 0;
        }
        return ok;
      };
      set_scale(shared, SCALE_GRASS_LOD, 4.f);
      set_scale(shared, SCALE_GRASS_RADIUS, 2.f);
      Expect(both_hold(201, 100.f, 201, 160.f), "two scrubs at 2x: each from its own game values, turn after turn");
      Expect(grass_cell_once(scrub.data(), {1, 1}, 40401.f, 3721.f) && grass_cell_once(second_scrub.data(), {1, 1}, 40401.f, 3721.f),
             "two scrubs: the cells of both are cut");
      set_scale(shared, SCALE_GRASS_LOD, 1.f);
      set_scale(shared, SCALE_GRASS_RADIUS, 1.f);
      set_scale(shared, SCALE_GRASS_DENSITY, 4.f);
      Expect(both_hold(200, 12.5f, 300, 20.f), "two scrubs, density alone: the game's radius, each bias / density once");
      set_scale(shared, SCALE_GRASS_DENSITY, 1.f);
      Expect(both_hold(200, 50.f, 300, 80.f), "two scrubs: back at 1x, the game's values in both");

      // The cell hook with the grass row out: no search, no cut, whatever the states hold.
      set_scale(shared, SCALE_GRASS_LOD, 4.f);
      set_scale(shared, SCALE_GRASS_RADIUS, 2.f);
      grass_update(scrub.data());
      shared->rows_applied &= ~RowBit(ROW_GRASS);
      Expect(grass_cell_once(scrub.data(), {1, 1}, 40401.f, 40401.f), "grass cell: the grass row out, no cell is cut");
      shared->rows_applied |= RowBit(ROW_GRASS);
      Expect(grass_cell_once(scrub.data(), {1, 1}, 40401.f, 3721.f), "grass cell: the grass row in again, the cut again");
      set_scale(shared, SCALE_GRASS_LOD, 1.f);
      set_scale(shared, SCALE_GRASS_RADIUS, 1.f);
      Expect(both_hold(200, 50.f, 300, 80.f), "two scrubs: at 1x before the crowd");

      // More scrubs than one window's states, through the hooks alone. A scrub's state lies within SCRUB_WINDOW states
      // of its address's home, so the crowd is picked from a block of memory by its home: every member's window is the
      // same, and far from the two scrubs' above. (This file's ScrubHome is the DLL's: the same header.)
      constexpr size_t CANDIDATES = 1u << 16;  // 64 a home
      auto* region = static_cast<uint8_t*>(
          VirtualAlloc(nullptr, CANDIDATES * scrub.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
      const auto apart = [](size_t home, size_t other) {
        const size_t distance = (home + SCRUB_STATES - other) % SCRUB_STATES;
        return std::min(distance, SCRUB_STATES - distance) >= SCRUB_WINDOW;
      };
      const auto far_from_the_two = [&](size_t home) {
        return apart(home, ScrubHome(scrub.data())) && apart(home, ScrubHome(second_scrub.data()));
      };
      size_t crowd_home = 0;
      while (!far_from_the_two(crowd_home)) {
        ++crowd_home;
      }
      // Two last written at 1x, the crowd of one window's size at 2x (its member PAGED's page is freed below), and
      // two new ones for later.
      constexpr size_t CROWD = SCRUB_WINDOW;
      constexpr size_t PAGED = 6;
      std::vector<uint8_t*> same_home;
      for (size_t candidate = 0; candidate < CANDIDATES && same_home.size() < CROWD + 4; ++candidate) {
        uint8_t* object = region + (candidate * scrub.size());
        if (ScrubHome(object) != crowd_home) continue;
        // Each in a page of its own (the paged member's is freed below).
        if (!same_home.empty() && (object - same_home.back()) < 0x1000) continue;
        std::memcpy(object, ((same_home.size() >= CROWD + 2) ? second_scrub.data() : scrub.data()), scrub.size());
        same_home.push_back(object);
      }
      Expect(same_home.size() == CROWD + 4, "scrubs of one home found");
      const auto member = [&](size_t index) { return same_home[2 + index]; };
      uint8_t* third_scrub = same_home[CROWD + 2];
      uint8_t* fourth_scrub = same_home[CROWD + 3];
      uint8_t* paged_page = region + ((member(PAGED) - region) / 0x1000 * 0x1000);
      const auto holds_after_update = [&](uint8_t* object, int32_t radius, float bias) {
        grass_update(object);
        grass_update(object);
        return std::memcmp(object + SCRUB_RADIUS, &radius, 4) == 0 && std::memcmp(object + SCRUB_BIAS, &bias, 4) == 0;
      };
      const auto updates_pass = [&](uint64_t count) {
        for (uint64_t update = 0; update < count; ++update) {
          grass_update(member(0));
        }
      };
      Expect(holds_after_update(same_home[0], 200, 50.f) && holds_after_update(same_home[1], 200, 50.f),
             "two scrubs of the crowd's home at 1x");
      set_scale(shared, SCALE_GRASS_LOD, 4.f);
      set_scale(shared, SCALE_GRASS_RADIUS, 2.f);
      bool crowd_ok = true;
      for (size_t index = 0; index < CROWD; ++index) {
        crowd_ok = crowd_ok && holds_after_update(member(index), ((index < CROWD - 2) ? 201 : 200),
                                                  ((index < CROWD - 2) ? 100.f : 50.f));
      }
      Expect(crowd_ok && Logged("grass: no state left for a grass area, it keeps the game's radius"),
             "scrubs past their window: the game's radius and bias, the line");
      // Every state but one without an update for long.
      updates_pass(STALE_SCRUB_UPDATES + 1);
      Expect(holds_after_update(member(CROWD - 2), 201, 100.f) && holds_after_update(member(CROWD - 1), 201, 100.f),
             "a stale state last written at 1x is taken (the two first scrubs')");
      Expect(holds_after_update(third_scrub, 300, 80.f) && grass_cell_once(third_scrub, {1, 1}, 40401.f, 40401.f),
             "a stale state whose scrub still holds the 2x values is not taken: the new scrub keeps the game's");
      put(member(5), SCRUB_RADIUS, int32_t{999});
      updates_pass(SCRUB_SEARCH_INTERVAL);
      Expect(holds_after_update(third_scrub, 300, 80.f), "a stale state whose scrub still holds one written value is not taken");
      // A refused scrub's window is searched again only SCRUB_SEARCH_INTERVAL update calls after the search that
      // found nothing: a state that may be taken by now is not seen before.
      put(member(5), SCRUB_BIAS, 5.f);
      updates_pass(SCRUB_SEARCH_INTERVAL - 4);
      Expect(holds_after_update(third_scrub, 300, 80.f), "a refused scrub: no new search before the interval is over");
      updates_pass(1);
      Expect(holds_after_update(third_scrub, 201, 160.f), "a stale state whose scrub holds other values is taken, at the next search");
      Expect(holds_after_update(fourth_scrub, 300, 80.f), "no other stale state may be taken");
      VirtualFree(paged_page, 0x1000, MEM_DECOMMIT);
      Expect(holds_after_update(fourth_scrub, 300, 80.f), "a refused scrub: its memory gone state is not seen before the interval either");
      updates_pass(SCRUB_SEARCH_INTERVAL);
      Expect(holds_after_update(fourth_scrub, 201, 160.f), "a stale state whose scrub's memory is gone is taken");

      // A long session: many more short-lived scrubs than the 64 states of before, each updated a few times at 2x and
      // left holding its 2x values, as a freed scrub's memory does. Every one is scaled (their homes all differ and lie
      // away from the windows used above, so that the test does not depend on where its memory is).
      constexpr size_t SESSION_SCRUBS = 200;
      std::array<bool, SCRUB_STATES> home_used = {};
      size_t session_scrubs = 0;
      bool session_ok = true;
      for (size_t candidate = 0; candidate < CANDIDATES && session_scrubs < SESSION_SCRUBS; ++candidate) {
        uint8_t* object = region + (candidate * scrub.size());
        const size_t home = ScrubHome(object);
        if (home_used[home] || !apart(home, crowd_home) || !far_from_the_two(home)
            || object - paged_page < 0x1000u) {
          continue;
        }
        home_used[home] = true;
        ++session_scrubs;
        std::memcpy(object, scrub.data(), scrub.size());
        session_ok = session_ok && holds_after_update(object, 201, 100.f) && grass_cell_once(object, {1, 1}, 40401.f, 3721.f);
      }
      Expect(session_scrubs == SESSION_SCRUBS && session_ok, "a long session: 200 scrubs one after another, every one scaled");
      // Back at 1x: a scrub that was idle all the time and kept its state comes back to the game's exact values, and
      // so do the others.
      set_scale(shared, SCALE_GRASS_LOD, 1.f);
      set_scale(shared, SCALE_GRASS_RADIUS, 1.f);
      Expect(holds_after_update(member(1), 200, 50.f) && holds_after_update(member(CROWD - 3), 200, 50.f)
                 && holds_after_update(third_scrub, 300, 80.f) && holds_after_update(fourth_scrub, 300, 80.f)
                 && holds_after_update(member(0), 200, 50.f) && holds_after_update(member(CROWD - 1), 200, 50.f),
             "after a refused take: the idle scrub is exactly the game's at 1x, as every other");
      VirtualFree(region, 0, MEM_RELEASE);
    }

    set_scale(shared, SCALE_CAMPAIGN_TREES, 4.f);
    Expect(campaign_tree_once() && lods_are({2500.f, 10000.f, 900.f, 1.0e6f, 360000.f}),
           "campaign tree: active, its model's LOD x the scale, the game's draw as it came");
    set_scale(shared, SCALE_CAMPAIGN_TREES, 1.f);
    Expect(campaign_tree_once() && lods_are({2500.f, 10000.f, 900.f, 1.0e6f, 90000.f}) && shared->scales_active.load() == 0,
           "campaign tree: back at 1x, the game's bits");
    Expect((shared->notices.load() & NOTICE_MODEL_TABLE_FULL) == 0, "grass, campaign tree: no table notice");
    shared->rows_applied = rows_applied_before;

    // The pair resolver: this image is not the game's code, so every call is the game's function with its argument,
    // and nothing of the manager is read by the row.
    {
      auto* pairs_record = reinterpret_cast<PairsRecord*>(PAIRS_RECORD);
      *pairs_record = {};
      std::array<uint8_t, 0x20> fake_manager = {};
      reinterpret_cast<PairsFn>(SITES[LABEL_PAIRS_ENTRY].address)(fake_manager.data());
      reinterpret_cast<PairsFn>(SITES[LABEL_PAIRS_ENTRY].address)(fake_manager.data());
      Expect(pairs_record->manager == fake_manager.data() && pairs_record->calls == 2
                 && Logged("renodx_attila64: PAIRS_ENTRY: first call (thread "),
             "pairs: game code the row does not know, the game's function with its argument");
      // The contact pass the same, and its own function: the pair resolver's record stays.
      auto* contact_record = reinterpret_cast<PairsRecord*>(CONTACT_RECORD);
      *contact_record = {};
      reinterpret_cast<PairsFn>(SITES[LABEL_CONTACT_ENTRY].address)(fake_manager.data());
      reinterpret_cast<PairsFn>(SITES[LABEL_CONTACT_ENTRY].address)(fake_manager.data());
      Expect(contact_record->manager == fake_manager.data() && contact_record->calls == 2 && pairs_record->calls == 2
                 && Logged("renodx_attila64: CONTACT_ENTRY: first call (thread "),
             "contact: game code the row does not know, the game's function with its argument");
    }

    // The sprite site, through its bed: the soldier's record in r13, its display tables' entries in rax and rcx, its
    // random value - 0.5 in xmm0; the game's own next instructions store the result. Neutral: the game's two
    // instructions, no call of ours. Active: the cap by the soldier's height, the dead soldiers' scale, each alone and
    // both; everything but the result as the game had it.
    {
      const auto sprite_bed = reinterpret_cast<MidBed>(SPRITE_BED);
      const std::array<float, 4> distances = {90.f, 110.f, 200.f, 1000.f};
      const std::array<float, 4> fade_widths = {22.5f, 27.5f, 50.f, 250.f};
      std::array<uint8_t, 0x400> display = {};
      std::array<uint8_t, 0x120> soldier = {};
      std::memcpy(display.data() + DISPLAY_SPRITE_DISTANCES, distances.data(), sizeof(distances));
      std::memcpy(display.data() + DISPLAY_SPRITE_DISTANCES + 0x10, fade_widths.data(), sizeof(fade_widths));
      put(soldier.data(), SOLDIER_DISPLAY, display.data());
      const uint64_t after_site_load = 0x7ab1e0000000beef;  // what the game's mov rax, [rbx] after the site reads
      // One soldier's pass over the site: true when the stored result is `expected` bit for bit and every register
      // and flag and the red zone is as it went in, but for the result in xmm0 and what the game's next instructions
      // load.
      const auto sprite_once = [&](size_t size, float dither, bool alive, float expected) {
        soldier[SOLDIER_FLAGS] = static_cast<uint8_t>(0x52 | (alive ? SOLDIER_ALIVE : 0));
        Registers in;
        for (size_t number = 0; number < in.gpr.size(); ++number) {
          in.gpr[number] = 0x0101010101010101 * (number + 1);
        }
        in.gpr[0] = reinterpret_cast<uintptr_t>(display.data() + DISPLAY_SPRITE_DISTANCES + 0x10 + (size * sizeof(float)));
        in.gpr[1] = reinterpret_cast<uintptr_t>(display.data() + DISPLAY_SPRITE_DISTANCES + (size * sizeof(float)));
        in.gpr[3] = reinterpret_cast<uintptr_t>(&after_site_load);
        in.gpr[13] = reinterpret_cast<uintptr_t>(soldier.data());
        in.flags = 0x202 | 0x8d5;  // every status flag set
        for (size_t number = 0; number < in.xmm.size(); ++number) {
          in.xmm[number].fill(static_cast<uint8_t>(0x30 + number));
        }
        std::memcpy(in.xmm[0].data(), &dither, sizeof(dither));
        Registers out;
        sprite_bed(&in, &out);
        Registers kept = in;
        kept.gpr[0] = after_site_load;
        kept.gpr[4] = out.gpr[4];  // rsp: the bed's own
        kept.gpr[6] = out.gpr[4] + 0x3d0;  // the game's lea rsi, [rsp + 0x3d0]
        kept.slot = std::bit_cast<uint32_t>(expected);
        kept.red_zone.fill(in.gpr[7]);
        std::memcpy(kept.xmm[0].data(), &expected, sizeof(expected));
        return out == kept;
      };
      const auto game_distance = [&](size_t size, float dither) { return (dither * fade_widths[size]) + distances[size]; };
      Expect(!Logged("SPRITE_DISTANCE: first call") && sprite_once(0, 0.25f, true, 95.625f) && sprite_once(3, -0.5f, false, 875.f)
                 && !Logged("SPRITE_DISTANCE: first call"),
             "sprite: neutral, the game's distance, every register and flag kept, no call of ours");
      set_scale(shared, SCALE_GRASS_LOD, 4.f);
      set_scale(shared, SCALE_UNIT_LOD, 2.f);
      shared->scales[SCALE_SPRITE_DISTANCE].store(20.f);
      shared->scales[SCALE_DEAD_UNITS].store(0.5f);
      Expect(sprite_once(1, 0.25f, false, game_distance(1, 0.25f)) && !Logged("SPRITE_DISTANCE: first call"),
             "sprite: other rows' bits and its scales without their bits change nothing");
      set_scale(shared, SCALE_GRASS_LOD, 1.f);
      set_scale(shared, SCALE_UNIT_LOD, 1.f);
      shared->scales[SCALE_DEAD_UNITS].store(1.f);

      // Small Soldiers as Sprites: 20 m of distance per metre of height, + the game's spread.
      set_scale(shared, SCALE_SPRITE_DISTANCE, 20.f);
      Expect(sprite_once(0, 0.25f, true, 41.625f) && Logged("SPRITE_DISTANCE: first call"),
             "sprite: a foot soldier turns into a sprite at 1.8 m x the scale + the spread, the first-call line");
      Expect(sprite_once(1, 0.25f, true, 58.875f) && sprite_once(2, 0.25f, true, 92.5f) && sprite_once(3, 0.25f, true, 122.5f)
                 && sprite_once(0, -0.5f, true, 24.75f),
             "sprite: each size by its own height, the spread below the middle too");
      shared->scales[SCALE_DEAD_UNITS].store(0.5f);
      Expect(sprite_once(0, 0.25f, false, 41.625f), "sprite: a dead soldier as a living one while its own scale's bit is clear");
      shared->scales[SCALE_DEAD_UNITS].store(1.f);
      // A scale below 0 (not one the add-on sends) is no cap, as in the 32-bit add-on.
      set_scale(shared, SCALE_SPRITE_DISTANCE, -20.f);
      Expect(sprite_once(0, 0.25f, true, game_distance(0, 0.25f)) && sprite_once(2, 0.25f, false, game_distance(2, 0.25f)),
             "sprite: a scale below 0 leaves the game's distance");
      set_scale(shared, SCALE_SPRITE_DISTANCE, 1000.f);
      Expect(sprite_once(0, 0.25f, true, game_distance(0, 0.25f)) && sprite_once(3, 0.25f, true, game_distance(3, 0.25f)),
             "sprite: a cap past the game's distance leaves the game's bits");
      // Dead Unit Detail with the cap, then alone.
      set_scale(shared, SCALE_SPRITE_DISTANCE, 20.f);
      set_scale(shared, SCALE_DEAD_UNITS, 0.5f);
      Expect(sprite_once(0, 0.25f, false, 20.8125f) && sprite_once(0, 0.25f, true, 41.625f),
             "sprite: a dead soldier's capped distance x its scale, a living one's not");
      set_scale(shared, SCALE_SPRITE_DISTANCE, 0.f);
      Expect(sprite_once(2, 0.25f, false, 106.25f) && sprite_once(2, 0.25f, true, game_distance(2, 0.25f))
                 && sprite_once(1, -0.25f, true, game_distance(1, -0.25f)),
             "sprite: the dead scale alone, the game's distance x it; a living soldier keeps the game's bits");
      shared->scales[SCALE_SPRITE_DISTANCE].store(20.f);
      Expect(sprite_once(0, 0.25f, true, game_distance(0, 0.25f)), "sprite: no cap from a scale whose bit is clear");
      shared->scales[SCALE_SPRITE_DISTANCE].store(0.f);
      set_scale(shared, SCALE_DEAD_UNITS, 1.f);
      Expect(shared->scales_active.load() == 0 && sprite_once(0, 0.25f, false, 95.625f), "sprite: back at neutral, the game's distance");
    }

    // The step: its bit clear, the placement does nothing and the game's step runs with its task; its bit set, one
    // step reserves a core on this process (or finds why not, on a small machine), and the bit cleared releases it.
    const auto default_before = default_cpu_sets();
    const uint64_t status_before = shared->core_status.load();
    shared->core_wanted.fetch_and(~CORE_BATTLE);
    Expect(!Logged("STEP_ENTRY: first call") && step_once() && step_record->calls == 1 && Logged("STEP_ENTRY: first call"),
           "step: the task reaches the game's step, its result comes back, its own first-call line");
    Expect(shared->core_status.load() == status_before && default_cpu_sets() == default_before && !Logged("battle core:"),
           "step: bit clear, nothing placed");
    shared->core_wanted.store(CORE_WANTED_DEFAULT);
    Expect(step_once() && step_record->calls == 2 && core_state_of(shared) != CORE_WAITING && core_state_of(shared) != 0
               && Logged("battle core:"),
           "step: bit set, the placement ran once before the game's step");
    if (core_state_of(shared) == CORE_ACTIVE) {
      Expect(default_cpu_sets() != default_before && Logged("battle core: logical"), "step: a core reserved");
      shared->core_wanted.fetch_and(~CORE_BATTLE);
      Expect(step_once() && step_record->calls == 3 && core_state_of(shared) == CORE_WAITING
                 && default_cpu_sets() == default_before && Logged("battle core: released (turned off)"),
             "step: bit cleared, released and the CPU sets put back");
    } else {
      std::printf("  step: no core on this machine (state %u): the release is not run\n", core_state_of(shared));
    }
    shared->core_wanted.store(CORE_WANTED_DEFAULT);
  };

  // This exe's copy of hooks.hpp.
  std::printf("  hooks: the test's copy\n");
  write_game();
  for (const Site& site : SITES) {
    if (site.kind == SITE_PATCH) continue;
    AppliedSite applied;
    const Refusal refusal =
        ((site.kind == SITE_MID)
             ? ApplyMidSite(api, site, reinterpret_cast<trampoline::MidHandler>(REPLACEMENTS[site.label]),
                            &block.scales_active, MID_SITE_SCALES[site.label], &applied)
             : ApplySite(api, site, REPLACEMENTS[site.label], &applied));
    Expect(refusal == REFUSAL_NONE, "hooks: site hooked");
    originals[site.label] = applied.original;
  }
  expect_hook_bodies(&block);

  // Drain: two game threads (a pack and a measured pass) while the add-on unregisters as plugin_block.h says: store
  // nullptr (the bits stay set here, so the hooks keep entering the guard), then wait for the first 0. On x86 this would
  // also pass with a release store; the seq_cst rule is the header's, this shows the first-zero wait.
  {
    block.hooks_wanted.store(RowBit(ROW_BONE_FLUSH) | RowBit(ROW_PASS_DRAW));
    next_action = PASS_DRAW_MEASURED;
    std::atomic_bool running = true;
    std::thread pack_thread([&] {
      while (running.load()) {
        pack_once();
      }
    });
    std::thread draw_thread([&] {
      while (running.load()) {
        draw_once();
      }
    });
    int undrained = 0;
    for (int cycle = 0; cycle < 200; ++cycle) {
      drained.store(false);
      block.callbacks.store(&CALLBACKS);
      std::this_thread::sleep_for(std::chrono::microseconds(200));
      block.callbacks.store(nullptr);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      bool zero_seen = false;
      while (!zero_seen && std::chrono::steady_clock::now() < deadline) {
        zero_seen = (block.calls_in_flight.load() == 0);
        if (!zero_seen) {
          Sleep(0);
        }
      }
      if (!zero_seen) {
        ++undrained;
      }
      drained.store(true);
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    running.store(false);
    pack_thread.join();
    draw_thread.join();
    Expect(undrained == 0, "drain: every wait ended within 1 s");
    Expect(calls_after_drain.load() == 0, "drain: no callback after the wait");
    std::printf("  drain: %u before_pack, %u pass_begin, %u pass_end calls over 200 cycles\n", before_pack_calls.load(),
                pass_begin_calls.load(), pass_end_calls.load());
    drained.store(false);
    block.hooks_wanted.store(0);
  }

  // The built DLL: copies under dll_cases\<case>\ (each its own module and globals).
  const std::wstring cases_dir = std::wstring(exe_dir_wide) + L"\\dll_cases";
  const std::wstring ini_path = std::wstring(exe_dir_wide) + L"\\ReShade.ini";
  CreateDirectoryW(cases_dir.c_str(), nullptr);
  const auto copy_plugin = [&](const wchar_t* folder, const wchar_t* file_name) {
    const std::wstring directory = cases_dir + L"\\" + folder;
    CreateDirectoryW(directory.c_str(), nullptr);
    std::wstring target = directory + L"\\" + file_name;
    Expect(CopyFileW(argv[1], target.c_str(), FALSE) != FALSE, "a copy of the plugin DLL");
    return target;
  };
  const auto set_ini_key_off = [&](const wchar_t* key) {
    DeleteFileW(ini_path.c_str());
    WritePrivateProfileStringW(INI_SECTION, ROW_INI_KEYS[ROW_GAME_CODE], L"0", ini_path.c_str());
    if (key != nullptr) {
      WritePrivateProfileStringW(INI_SECTION, key, L"0", ini_path.c_str());
    }
  };
  const auto init_copy = [&](const wchar_t* folder, const wchar_t* file_name, const LoaderApi& case_api) {
    const Plugin plugin = LoadPlugin(copy_plugin(folder, file_name).c_str());
    log_lines.clear();
    return std::pair{plugin, (plugin.init != nullptr) ? plugin.init(&case_api) : -1};
  };
  const auto status_of = [](const Plugin& plugin) {
    return (plugin.block != nullptr) ? plugin.block->status.load(std::memory_order_acquire) : uint32_t{STATUS_LOADING};
  };
  set_ini_key_off(nullptr);

  // The DLL file holds the render part's reserve text as one whole string of its own (a zero byte before and after):
  // the batch tool looks for exactly that and refuses a render-core arm on a DLL without it.
  {
    using std::string_view_literals::operator""sv;
    std::ifstream dll_file(argv[1], std::ios::binary);
    const std::string dll_bytes{std::istreambuf_iterator<char>(dll_file), std::istreambuf_iterator<char>()};
    Expect(dll_bytes.find("\0render core: logical %u + %u (render thread %lu), scheduling class %u\0"sv) != std::string::npos,
           "dll: the file holds the render core's reserve text as a whole string");
    Expect(dll_bytes.find("\0renodx_attila64: render thread %lu\0"sv) != std::string::npos,
           "dll: the file holds the render thread's line as a whole string");
    // The game code row's word, for the batch tool's game-code arm (tests\game_code_test.cpp has the row itself).
    Expect(dll_bytes.find("\0game code\0"sv) != std::string::npos,
           "dll: the file holds the row's word \"game code\" as a whole string");
    Expect(dll_bytes.find("\0renodx_attila64: site %s %#x: applied\0"sv) != std::string::npos
               && dll_bytes.find("\0renodx_attila64: site %s %#x: applied (game's bytes kept)\0"sv) != std::string::npos
               && dll_bytes.find("\0renodx_attila64: shadow map %u: patched\0"sv) != std::string::npos
               && dll_bytes.find("\0renodx_attila64: shadow map: game's size\0"sv) != std::string::npos,
           "dll: the file holds the two applied lines and the shadow row's two lines as whole strings");
    // The parallel effects row's word marks a DLL that reads the EffectsMode key: a whole string of its own (no line
    // of the row ends in it), and the key's name is in the file too (a wide string).
    const std::wstring_view mode_key(INI_EFFECTS_MODE);
    Expect(dll_bytes.find("\0parallel effects\0"sv) != std::string::npos
               && dll_bytes.find(std::string_view(reinterpret_cast<const char*>(mode_key.data()),
                                                  (mode_key.size() + 1) * sizeof(wchar_t)))
                      != std::string::npos,
           "dll: the file holds the parallel effects row's word as a whole string, and the EffectsMode key");
  }

  const auto render_state_of = [](const Block* shared) {
    return (shared->render_core_status.load() >> CORE_STATE_SHIFT) & CORE_FIELD_MASK;
  };
  const auto selected_count = [] {
    ULONG count = 0;
    GetThreadSelectedCpuSets(GetCurrentThread(), nullptr, 0, &count);
    return count;
  };

  std::printf("  hooks: the DLL's own\n");
  write_game();
  // The test's game image holds the sites' windows only: the battle step row's check of the game's code refuses it,
  // with its one line, and the row's hook passes every call on. The contact split row the same, and its line names
  // its first range (the lowest address) and that range's first block.
  const Plugin main_plugin = ExpectPluginDll(api, copy_plugin(L"main", MODULE_NAME).c_str(), true, ENGINE_CODE_DIFFERS,
                                             ENGINE_CODE_DIFFERS);
  Expect(std::ranges::count_if(log_lines, [](const std::string& line) { return line.find("battle step: ") != std::string::npos; }) == 1
             && Logged("renodx_attila64: battle step: game code differs (") && Logged(", battle step on")
             && Logged("renodx_attila64: site PAIRS_ENTRY 0x79a690: applied"),
         "battle step: one line for the game code it does not know");
  {
    char differs_line[96];
    std::snprintf(differs_line, sizeof(differs_line), "renodx_attila64: contact split: off (game code differs at 0x%x+0x0)",
                  battle_step::CONTACT_RANGES.front().start);
    Expect(std::ranges::count_if(log_lines, [](const std::string& line) { return line.find("contact split: ") != std::string::npos; }) == 1
               && Logged(differs_line) && Logged(", contact split on"),
           "contact split: one line for the game code it does not know, with the place");
  }
  if (main_plugin.block != nullptr) {
    // This thread ran the DLL's game start, so it is the DLL's render thread: its steps are never selected onto the
    // battle core (the test's own copy above, with no render thread, selects this thread).
    expect_hook_bodies(main_plugin.block);
    Expect(step_once() && (core_state_of(main_plugin.block) != CORE_ACTIVE || selected_count() == 0),
           "step: the render thread is not selected onto the battle core");
    main_plugin.block->core_wanted.fetch_and(~CORE_BATTLE);
    Expect(step_once() && core_state_of(main_plugin.block) != CORE_ACTIVE, "step: released again");
    main_plugin.block->core_wanted.store(CORE_WANTED_DEFAULT);
  }

  // The RenderCore key. 1: the render part's bit is set at init, and one step reserves both cores (this thread is the
  // render thread and holds the render core). 0: off for the process, whatever the bit says later.
  WritePrivateProfileStringW(INI_SECTION, INI_RENDER_CORE, L"1", ini_path.c_str());
  write_game();
  if (const auto [plugin, result] = init_copy(L"render_on", MODULE_NAME, api); result == 0) {
    Expect(plugin.block->core_wanted.load() == (CORE_WANTED_DEFAULT | CORE_RENDER) && render_state_of(plugin.block) == CORE_NO_THREAD,
           "dll: RenderCore=1 sets the render part's bit at init");
    plugin.game_started(&api);
    Expect(render_state_of(plugin.block) == CORE_WAITING && Logged("renodx_attila64: render thread "),
           "dll: RenderCore=1, the render thread is captured at the game's start");
    const auto default_before = default_cpu_sets();
    Expect(step_once() && core_state_of(plugin.block) != CORE_WAITING, "dll: RenderCore=1, a step places");
    if (core_state_of(plugin.block) == CORE_ACTIVE && render_state_of(plugin.block) == CORE_ACTIVE) {
      char render_piece[64];
      std::snprintf(render_piece, sizeof(render_piece), " (render thread %lu), scheduling class ", GetCurrentThreadId());
      Expect(selected_count() == 2 && default_cpu_sets() != default_before
                 && Logged("renodx_attila64: battle core: logical ") && Logged(", render core: logical ") && Logged(render_piece),
             "dll: RenderCore=1, both cores reserved in one line, this thread on the render core");
      plugin.block->core_wanted.fetch_and(~(CORE_BATTLE | CORE_RENDER));
      Expect(step_once() && core_state_of(plugin.block) == CORE_WAITING && render_state_of(plugin.block) == CORE_WAITING
                 && selected_count() == 0 && default_cpu_sets() == default_before,
             "dll: RenderCore=1, released: the render thread cleared, the CPU sets put back");
    } else {
      std::printf("  dll: no render core on this machine (states %u / %u)\n", core_state_of(plugin.block),
                  render_state_of(plugin.block));
      plugin.block->core_wanted.fetch_and(~(CORE_BATTLE | CORE_RENDER));
      step_once();
    }
  } else {
    Expect(false, "dll: init for the RenderCore=1 case");
  }
  set_ini_key_off(INI_RENDER_CORE);
  write_game();
  if (const auto [plugin, result] = init_copy(L"render_off", MODULE_NAME, api); result == 0) {
    Expect(plugin.block->core_wanted.load() == (CORE_WANTED_DEFAULT & ~CORE_RENDER) && render_state_of(plugin.block) == CORE_OFF_INI,
           "dll: RenderCore=0 is off at init");
    plugin.game_started(&api);
    plugin.block->core_wanted.fetch_or(CORE_RENDER);
    Expect(step_once() && render_state_of(plugin.block) == CORE_OFF_INI && !Logged("renodx_attila64: render thread ")
               && Logged("render core: off in ReShade.ini, RenderCore=0 (render thread 0, error 0)") && !Logged("render core: logical"),
           "dll: RenderCore=0, nothing captured and no render core even with its bit set later");
    plugin.block->core_wanted.fetch_and(~(CORE_BATTLE | CORE_RENDER));
    Expect(step_once() && core_state_of(plugin.block) != CORE_ACTIVE, "dll: RenderCore=0, released again");
  } else {
    Expect(false, "dll: init for the RenderCore=0 case");
  }
  set_ini_key_off(nullptr);

  write_game();
  if (const auto [plugin, result] = init_copy(L"changed", MODULE_NAME, api); result == 0) {
    Expect(plugin.block != main_plugin.block, "dll: each copy has its own block");
    *at(SITES[ROW_PASS_DRAW].address + 1) ^= 1;
    plugin.game_started(&api);
    char others_unchanged[64];
    std::snprintf(others_unchanged, sizeof(others_unchanged), "game started: %zu sites unchanged", FAKE_GAME_SITE_COUNT - 1);
    Expect(plugin.block->rows_changed.load() == RowBit(ROW_PASS_DRAW) && Logged("VIEW_DRAW_ENTRY changed after we patched it")
               && Logged(others_unchanged),
           "dll: game start finds a site changed after init");
  } else {
    Expect(false, "dll: init for the changed-later case");
  }

  // The EffectsMode key: the two effect bits at init with the mode's line, and the DLL's walk hook through the site.
  // 2 (On): the first walk stores 1 in the game's flag; 0 then puts the loader's value back, once.
  for (const int mode_key : {1, 2, 0, 7}) {
    DeleteFileW(ini_path.c_str());
    WritePrivateProfileStringW(INI_SECTION, INI_EFFECTS_MODE, std::to_wstring(mode_key).c_str(), ini_path.c_str());
    write_game();
    const auto [plugin, result] = init_copy((std::wstring(L"effects_mode_") + std::to_wstring(mode_key)).c_str(), MODULE_NAME, api);
    if (result != 0) {
      Expect(false, "dll: init for an EffectsMode case");
      continue;
    }
    const uint32_t effect_bits = ((mode_key == 1) ? (CORE_EFFECTS | CORE_EFFECTS_AUTO) : ((mode_key == 2) ? CORE_EFFECTS : 0));
    char mode_line[96];
    std::snprintf(mode_line, sizeof(mode_line), "renodx_attila64: parallel effects mode %s (ini EffectsMode)",
                  EFFECTS_MODE_WORDS[EffectsModeOf(effect_bits)]);
    Expect(plugin.block->core_wanted.load() == (CORE_WANTED_DEFAULT | effect_bits) && Logged(mode_line)
               && Logged("EffectsMode=7 is not 0, 1 or 2 (no change)") == (mode_key == 7),
           "dll: the EffectsMode key sets the two effect bits at init and says the mode");
    plugin.game_started(&api);
    if (mode_key == 1) continue;  // Automatic reads a node's table: effect_guard_test
    const auto walk_site = reinterpret_cast<EffectWalkFn>(uintptr_t{SITES[LABEL_EFFECT_WALK_ENTRY].address});
    *at(EFFECT_FLAG) = 0;
    walk_site(nullptr, 0.f, 0.f);
    Expect(Logged("renodx_attila64: parallel effects: offered (loader guards in place)")
               && Logged("renodx_attila64: EFFECT_WALK_ENTRY: first call (thread ") && *at(EFFECT_FLAG) == ((mode_key == 2) ? 1 : 0),
           "dll: the walk's first call says so, and On stores 1 in the game's flag");
    plugin.block->core_wanted.fetch_xor(CORE_EFFECTS);
    walk_site(nullptr, 0.f, 0.f);
    Expect(*at(EFFECT_FLAG) == ((mode_key == 2) ? 0 : 1), "dll: the add-on's change of the mode shows at the next walk");
    WriteTweakObjects();
  }
  set_ini_key_off(nullptr);

  // The game's parallel effect flag is offered only behind the loader's guards: with one guard entry that is not the
  // loader's hook, or with the game's read of the flag changed, its slot is ABSENT (the row's hook is in and inert).
  for (const bool guard_case : {true, false}) {
    uint8_t* changed = at(guard_case ? EFFECT_GUARD_ENTRIES[1] : (EFFECT_FLAG_READ_ADDRESS + 2));
    write_game();
    *changed ^= 1;
    const auto [plugin, result] = init_copy((guard_case ? L"effect_guard" : L"effect_flag_read"), MODULE_NAME, api);
    if (result == 0) {
      plugin.game_started(&api);
    }
    *changed ^= 1;
    char absent_line[96];
    std::snprintf(absent_line, sizeof(absent_line), "renodx_attila64: tweaks: %zu checked, %zu ok, 3 absent", TWEAKS.size() - 3,
                  TWEAKS.size() - 3);
    Expect(result == 0 && (plugin.block->rows_applied & RowBit(ROW_PARALLEL_EFFECTS)) != 0
               && plugin.block->tweaks[tweaks::LOADER_FLAG_TWEAK].verdict == VERDICT_ABSENT
               && plugin.block->tweaks[tweaks::LOADER_FLAG_TWEAK].value_address == 0 && Logged(absent_line)
               && Logged(guard_case ? "renodx_attila64: parallel effects: not offered (0x34e0170 not the loader's)"
                                    : "renodx_attila64: parallel effects: not offered (flag read differs)")
               && !Logged("parallel effects: offered"),
           "dll: the parallel effect flag is not offered without the loader's guards or with another flag read");
  }

  set_ini_key_off(INI_ENABLED);
  write_game();
  {
    const auto [plugin, result] = init_copy(L"off", MODULE_NAME, api);
    Expect(result == 1 && status_of(plugin) == STATUS_OFF && Logged("Enabled=0") && every_site_untouched()
               && !Logged("loader frame cap"),
           "dll: Enabled=0 refuses and writes nothing");
  }
  // Every row of the fake game (the sites table says which).
  for (uint32_t row_index = 0; row_index < ROW_COUNT; ++row_index) {
    const auto row = static_cast<Row>(row_index);
    if ((ALL_ROWS & RowBit(row)) == 0) continue;
    set_ini_key_off(ROW_INI_KEYS[row]);
    write_game();
    const auto [plugin, result] = init_copy(ROW_INI_KEYS[row], MODULE_NAME, api);
    const uint32_t others = ALL_ROWS & ~RowBit(row);
    // A patch site keeps the game's bytes without a key, in or out.
    bool only_its_site_untouched = true;
    for (const Site& site : SITES) {
      only_its_site_untouched = only_its_site_untouched
                                && site_untouched(site)
                                       == (site.row == row || site.row == ROW_GAME_CODE || site.kind == SITE_PATCH);
    }
    Expect(result == 0 && status_of(plugin) == STATUS_READY && plugin.block->rows_applied == others
               && plugin.block->rows_refused == (RowBit(row) | RowBit(ROW_GAME_CODE)) && plugin.block->rows_tried == others
               && Logged("skipped (OFF)") && only_its_site_untouched,
           "dll: a row's install key 0 leaves that row out, and out of the rows tried");
    // The battle step row's own key 0 is its lane's word, with the game's code not read; with the row in, this image
    // is game code it does not know.
    // The contact split row's the same, in its own lane.
    Expect(plugin.block->engine_status.load()
                   == ((uint32_t{(row == ROW_BATTLE_STEP) ? ENGINE_OFF_INI : ENGINE_CODE_DIFFERS} << ENGINE_STEP_SHIFT)
                       | (uint32_t{(row == ROW_CONTACT_SPLIT) ? ENGINE_OFF_INI : ENGINE_CODE_DIFFERS} << ENGINE_SPLIT_SHIFT))
               && Logged("battle step: ") == (row != ROW_BATTLE_STEP) && Logged("contact split: ") == (row != ROW_CONTACT_SPLIT),
           "dll: an engine row's key 0 is its lane's off, without a line");
    Expect(Logged("renodx_attila64: site CONTACT_ENTRY 0x5df300: skipped (OFF)") == (row == ROW_CONTACT_SPLIT)
               && Logged(", contact split off") == (row == ROW_CONTACT_SPLIT)
               && Logged(", contact split on") == (row != ROW_CONTACT_SPLIT)
               && plugin.block->core_wanted.load() == CORE_WANTED_DEFAULT,
           "dll: the contact split row's key 0 is its site's skipped (OFF) line and its word off in the rows line");
    Expect(Logged("renodx_attila64: site PAIRS_ENTRY 0x79a690: skipped (OFF)") == (row == ROW_BATTLE_STEP)
               && Logged(", battle step off") == (row == ROW_BATTLE_STEP)
               && Logged(", battle step on") == (row != ROW_BATTLE_STEP),
           "dll: the battle step row's key 0 is its site's skipped (OFF) line and its word off in the rows line");
    Expect(Logged("renodx_attila64: site EFFECT_WALK_ENTRY 0x32fbf60: skipped (OFF)") == (row == ROW_PARALLEL_EFFECTS)
               && Logged(", parallel effects off") == (row == ROW_PARALLEL_EFFECTS)
               && Logged("parallel effects mode ") == (row != ROW_PARALLEL_EFFECTS),
           "dll: without the parallel effects row its site is skipped, its row is off and the mode key is not read");
    // Both core status words are built only with the row in (without it the RenderCore key is not read either).
    const bool built = (row != ROW_CPU_PLACEMENT);
    Expect(plugin.block->core_status.load() == (built ? (uint64_t{CORE_WAITING} << CORE_STATE_SHIFT) : 0)
               && plugin.block->render_core_status.load() == (built ? (uint32_t{CORE_NO_THREAD} << CORE_STATE_SHIFT) : 0),
           "dll: the core status words are built only with the CPU row applied");
    plugin.game_started(&api);
    Expect(Logged("renodx_attila64: parallel effects: not offered (its row is off)") == (row == ROW_PARALLEL_EFFECTS)
               && Logged("parallel effects: offered") == (row != ROW_PARALLEL_EFFECTS)
               && (plugin.block->tweaks[tweaks::LOADER_FLAG_TWEAK].verdict == VERDICT_ABSENT) == (row == ROW_PARALLEL_EFFECTS),
           "dll: without the parallel effects row the game's flag is not offered");
    Expect(Logged("renodx_attila64: render thread ") == built
               && plugin.block->render_core_status.load() == (built ? (uint32_t{CORE_WAITING} << CORE_STATE_SHIFT) : 0),
           "dll: the render thread is captured only with the CPU row applied");
  }
  set_ini_key_off(nullptr);

  write_game();
  *at(SITES[ROW_BONE_FLUSH].address) ^= 1;
  {
    const auto [plugin, result] = init_copy(L"differs", MODULE_NAME, api);
    Expect(result == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_BONE_FLUSH))
               && plugin.block->rows_refused == (RowBit(ROW_BONE_FLUSH) | RowBit(ROW_GAME_CODE)) && plugin.block->rows_tried == ALL_ROWS
               && Logged("PACK_ENTRY 0x3416d50: skipped (DIFFERS)"),
           "dll: a site whose bytes differ is left out (still tried)");
  }

  write_game();
  *at(SITES[ROW_CPU_PLACEMENT].address + SITES[ROW_CPU_PLACEMENT].length) ^= 1;
  {
    const auto [plugin, result] = init_copy(L"step_neighbour", MODULE_NAME, api);
    Expect(result == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_CPU_PLACEMENT))
               && plugin.block->rows_refused == (RowBit(ROW_CPU_PLACEMENT) | RowBit(ROW_GAME_CODE)) && plugin.block->rows_tried == ALL_ROWS
               && plugin.block->core_status.load() == 0 && plugin.block->render_core_status.load() == 0
               && site_untouched(SITES[ROW_CPU_PLACEMENT])
               && Logged("STEP_ENTRY 0xbb4e00: skipped (NEIGHBOUR)"),
           "dll: a patch next to the step site leaves the battle core out (tried, status not built)");
  }

  write_game();
  hook_mode = HOOK_NO_MEMORY;
  {
    const auto [plugin, result] = init_copy(L"no_memory", MODULE_NAME, api);
    Expect(result == 0 && status_of(plugin) == STATUS_READY && plugin.block->rows_applied == RowBit(ROW_SHADOW_MAP)
               && plugin.block->rows_refused == ((ALL_ROWS & ~RowBit(ROW_SHADOW_MAP)) | RowBit(ROW_GAME_CODE)) && plugin.block->core_status.load() == 0
               && Logged("PACK_ENTRY 0x3416d50: skipped (NO_MEMORY)") && Logged("SPRITE_DISTANCE 0xa5049c: skipped (NO_MEMORY)")
               && site_untouched(SITES[LABEL_SPRITE_DISTANCE]),
           "dll: no memory near the game leaves every hook and stub row out (the patch row needs none)");
    // An engine row whose site is not in reads no game code: its lane says so without the row's code line.
    Expect(plugin.block->engine_status.load()
                   == ((uint32_t{ENGINE_CODE_DIFFERS} << ENGINE_STEP_SHIFT) | (uint32_t{ENGINE_CODE_DIFFERS} << ENGINE_SPLIT_SHIFT))
               && !Logged("battle step: ") && !Logged("contact split: "),
           "dll: an engine row without its site is off, with no code line");
  }
  hook_mode = HOOK_NORMAL;

  // The loader refuses the sprite site's jump (the only write of this init: the shadow sizes stay the game's): the
  // site keeps the game's bytes, its row is out and the other rows go in.
  write_game();
  failing_patch_calls = 1;
  {
    const auto [plugin, result] = init_copy(L"sprite_write", MODULE_NAME, api);
    Expect(result == 0 && failing_patch_calls == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_SPRITES))
               && plugin.block->rows_refused == (RowBit(ROW_SPRITES) | RowBit(ROW_GAME_CODE)) && plugin.block->rows_tried == ALL_ROWS
               && Logged("site SPRITE_DISTANCE 0xa5049c: skipped (WRITE_FAILED)") && Logged("campaign trees on, sprites off")
               && site_untouched(SITES[LABEL_SPRITE_DISTANCE]),
           "sprite: its jump refused leaves the game's bytes and the row out, the other rows in");
    plugin.game_started(&api);
    char sites_in[64];
    std::snprintf(sites_in, sizeof(sites_in), "game started: %zu sites unchanged", FAKE_GAME_SITE_COUNT - 1);
    Expect(plugin.block->rows_changed.load() == 0 && Logged(sites_in), "sprite: a refused site is not re-checked");
  }

  // The grass row's second hook finds no memory: its first one is in and stays in (a hook cannot come out), the row is
  // refused, and the hook that stayed does nothing whatever the scales say. The other rows go in.
  write_game();
  failing_hook_calls = 1u << static_cast<uint32_t>(std::ranges::count_if(
      SITES.begin(), SITES.begin() + LABEL_GRASS_DRAW_ENTRY, [](const Site& site) { return site.kind == SITE_HOOK; }));
  {
    const auto [plugin, result] = init_copy(L"grass_second_hook", MODULE_NAME, api);
    Expect(result == 0 && failing_hook_calls == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_GRASS))
               && plugin.block->rows_refused == (RowBit(ROW_GRASS) | RowBit(ROW_GAME_CODE)) && plugin.block->rows_tried == ALL_ROWS
               && Logged("site GRASS_UPDATE_ENTRY 0x33fb140: applied")
               && Logged("site GRASS_DRAW_ENTRY 0x33f3e00: skipped (NO_MEMORY)")
               && Logged("grass off, grass cells on, campaign trees on"),
           "grass: its second hook refused leaves the row out, the other rows in");
    Expect(*at(SITES[LABEL_GRASS_UPDATE_ENTRY].address) == 0xe9 && site_untouched(SITES[LABEL_GRASS_DRAW_ENTRY]),
           "grass: the first hook stays in, the second site is the game's");
    reset_scrub();
    *grass_update_record = {};
    *grass_cell_record = {};
    for (const uint32_t scale : {SCALE_GRASS_LOD, SCALE_GRASS_RADIUS, SCALE_GRASS_DENSITY}) {
      set_scale(plugin.block, scale, 4.f);
    }
    Expect(grass_update_once() && grass_update_record->calls == 1 && scrub_holds(200, 200.f) && lods_are(GAME_LODS)
               && grass_cell_once(scrub.data(), {1, 1}, 40401.f, 40401.f),
           "grass: the hook of a refused row writes nothing with its scales active, and no cell is cut");
    plugin.game_started(&api);
    char sites_in[64];
    std::snprintf(sites_in, sizeof(sites_in), "game started: %zu sites unchanged", FAKE_GAME_SITE_COUNT - 1);
    Expect(plugin.block->rows_changed.load() == 0 && Logged(sites_in), "grass: the hook that stayed in is still re-checked");
  }

  // One byte of the grass draw's code is not the game's (not in the site's window): init says so once, and with the row
  // in and its scales active the hook scales the model and leaves no draw out.
  write_game();
  *at(SITES[LABEL_GRASS_DRAW_ENTRY].address + GRASS_DRAW_NO_LOD_BYTES - 8) ^= 1;
  {
    const auto [plugin, result] = init_copy(L"grass_draw_code", MODULE_NAME, api);
    reset_scrub();
    *grass_update_record = {};
    *grass_draw_record = {};
    set_scale(plugin.block, SCALE_GRASS_LOD, 4.f);
    Expect(result == 0 && plugin.block->rows_applied == ALL_ROWS
               && Logged("grass draw: the game's code is not the code that was read, every draw goes to the game"),
           "grass draw: a patched byte at init leaves its line");
    Expect(grass_update_once() && grass_draw_once(1, 50.f, 0.f, true) && lods_are({10000.f, 40000.f, 1600.f, 4.0e6f, 90000.f})
               && grass_draw_once(1, 30.f, 0.f, true),
           "grass draw: a patched byte at init, the model scaled and every draw reaches the game");
    set_scale(plugin.block, SCALE_GRASS_LOD, 1.f);
    Expect(grass_update_once() && lods_are(GAME_LODS), "grass draw: a patched byte at init, back at 1x");
  }
  // The same byte written after init (the loader's late writes): game start finds it.
  write_game();
  {
    const auto [plugin, result] = init_copy(L"grass_draw_code_late", MODULE_NAME, api);
    reset_scrub();
    *grass_update_record = {};
    *grass_draw_record = {};
    set_scale(plugin.block, SCALE_GRASS_LOD, 4.f);
    Expect(result == 0 && !Logged("grass draw: ") && grass_update_once() && grass_draw_once(1, 50.f, 0.f, false),
           "grass draw: the code as read at init, a draw past the last LOD is left out");
    *at(SITES[LABEL_GRASS_DRAW_ENTRY].address + GRASS_DRAW_NO_LOD_BYTES - 8) ^= 1;
    plugin.game_started(&api);
    char sites_in[64];
    std::snprintf(sites_in, sizeof(sites_in), "game started: %zu sites unchanged", FAKE_GAME_SITE_COUNT);
    Expect(Logged(GRASS_DRAW_CODE_LINE) && Logged(sites_in) && grass_draw_once(1, 50.f, 0.f, true),
           "grass draw: a byte patched before game start leaves the line, every draw reaches the game");
    set_scale(plugin.block, SCALE_GRASS_LOD, 1.f);
    Expect(grass_update_once() && lods_are(GAME_LODS), "grass draw: a byte patched before game start, back at 1x");
  }

  // Shadow Map Size: the ShadowMapSize key of [Attila64Plugin.Values] on the DirectX 11 route (the loader's test
  // switch here). The three size sites get the game's floats x 2^key; the four cascade sites are never written.
  const auto shadow_sizes = [&](float max_and_high, float medium) {
    const auto holds = [&](SiteLabel label, float size) {
      const std::array<float, 2> pair = {size, size};
      return std::memcmp(at(SITES[label].address), pair.data(), sizeof(pair)) == 0;
    };
    bool cascades_untouched = true;
    for (const SiteLabel label : {LABEL_SHADOW_COUNT_HIGH, LABEL_SHADOW_EXTRA_HIGH, LABEL_SHADOW_COUNT_MAX, LABEL_SHADOW_EXTRA_MAX}) {
      cascades_untouched = cascades_untouched && site_untouched(SITES[label]);
    }
    return holds(LABEL_SHADOW_SIZE_HIGH, max_and_high) && holds(LABEL_SHADOW_SIZE_MAX, max_and_high)
           && holds(LABEL_SHADOW_SIZE_MEDIUM, medium) && cascades_untouched;
  };
  const auto shadow_case = [&](const wchar_t* folder, const wchar_t* key_text, bool dx11) {
    SetEnvironmentVariableA(tweaks::ROUTE_VARIABLE, (dx11 ? "1" : nullptr));
    set_ini_key_off(nullptr);
    WritePrivateProfileStringW(INI_VALUES_SECTION, INI_SHADOW_MAP_SIZE, key_text, ini_path.c_str());
    write_game();
    return init_copy(folder, MODULE_NAME, api);
  };
  char every_site_unchanged[64];
  std::snprintf(every_site_unchanged, sizeof(every_site_unchanged), "game started: %zu sites unchanged", FAKE_GAME_SITE_COUNT);
  Expect(shadow_sizes(2048.f, 1024.f), "shadow: the fake game holds the game's sizes");
  {
    const size_t near_code_before = near_code_used;
    const auto [plugin, result] = shadow_case(L"shadow_1", L"1", true);
    // Every site in: the hooks' stubs and the sprite site's own, with 1 KB left of the loader's page for the loader.
    Expect(near_code_used - near_code_before <= LOADER_NEAR_CODE_BYTES - 0x400,
           "dll: one game run's code near the game fits the loader's page");
    Expect(result == 0 && plugin.block->rows_applied == ALL_ROWS && plugin.block->rows_refused == RowBit(ROW_GAME_CODE)
               && shadow_sizes(4096.f, 2048.f) && Logged("renodx_attila64: shadow map 4096: patched")
               && Logged("renodx_attila64: site SHADOW_SIZE_MAX 0x30e46eb: applied")
               && !Logged("site SHADOW_SIZE_MAX 0x30e46eb: applied (")
               && Logged("renodx_attila64: site SHADOW_EXTRA_MAX 0x30e4717: applied (game's bytes kept)")
               && !Logged("game's size"),
           "shadow: ShadowMapSize=1 doubles the three sizes, the cascade sites are only checked");
    plugin.game_started(&api);
    Expect(plugin.block->rows_changed.load() == 0 && Logged(every_site_unchanged),
           "shadow: game start finds the written sizes unchanged");
    *at(SITES[LABEL_SHADOW_SIZE_MEDIUM].address + 3) ^= 1;
    plugin.game_started(&api);
    Expect(plugin.block->rows_changed.load() == RowBit(ROW_SHADOW_MAP) && Logged("SHADOW_SIZE_MEDIUM changed after we patched it"),
           "shadow: a later change of a written size is seen");
  }
  {
    const auto [plugin, result] = shadow_case(L"shadow_2", L"2", true);
    Expect(result == 0 && plugin.block->rows_applied == ALL_ROWS && shadow_sizes(8192.f, 4096.f)
               && Logged("renodx_attila64: shadow map 8192: patched"),
           "shadow: ShadowMapSize=2 makes them four times the game's");
  }
  {
    const auto [plugin, result] = shadow_case(L"shadow_0", L"0", true);
    Expect(result == 0 && plugin.block->rows_applied == ALL_ROWS && shadow_sizes(2048.f, 1024.f)
               && Logged("renodx_attila64: shadow map: game's size") && !Logged("game's size ("),
           "shadow: ShadowMapSize=0 writes nothing, the row is in");
  }
  {
    const auto [plugin, result] = shadow_case(L"shadow_not_dx11", L"1", false);
    Expect(result == 0 && plugin.block->rows_applied == ALL_ROWS && shadow_sizes(2048.f, 1024.f)
               && Logged("renodx_attila64: shadow map: game's size (ShadowMapSize=1 not written: not DX11)"),
           "shadow: nothing is written off the DirectX 11 route");
  }
  for (const auto& [folder, key_text] : {std::pair{L"shadow_3", L"3"}, std::pair{L"shadow_minus_1", L"-1"}}) {
    const auto [plugin, result] = shadow_case(folder, key_text, true);
    Expect(result == 0 && plugin.block->rows_applied == ALL_ROWS && shadow_sizes(2048.f, 1024.f)
               && Logged("renodx_attila64: shadow map: game's size (ShadowMapSize=") && Logged(" out of range)"),
           "shadow: a key outside 0..2 writes nothing");
  }
  // Two Shadow Cascades Under Rays: the ShadowCascades key of the same section on the DirectX 11 route. 1 writes
  // Medium's numbers (2 cascades, 0) over High's (3, 1) and Max's (4, 2); the sizes follow their own key.
  constexpr std::array<SiteLabel, 4> CASCADE_LABELS = {LABEL_SHADOW_COUNT_HIGH, LABEL_SHADOW_EXTRA_HIGH, LABEL_SHADOW_COUNT_MAX,
                                                       LABEL_SHADOW_EXTRA_MAX};
  const auto cascades_are = [&](const std::array<uint32_t, 4>& numbers) {
    bool match = true;
    for (size_t index = 0; index < CASCADE_LABELS.size(); ++index) {
      match = match && std::memcmp(at(SITES[CASCADE_LABELS[index]].address), &numbers[index], sizeof(uint32_t)) == 0;
    }
    return match;
  };
  constexpr std::array<uint32_t, 4> GAME_CASCADES = {3, 1, 4, 2};
  constexpr std::array<uint32_t, 4> TWO_CASCADES = {2, 0, 2, 0};
  const auto cascade_case = [&](const wchar_t* folder, const wchar_t* cascades_text, const wchar_t* size_text, bool dx11,
                                uintptr_t changed_byte) {
    SetEnvironmentVariableA(tweaks::ROUTE_VARIABLE, (dx11 ? "1" : nullptr));
    set_ini_key_off(nullptr);
    WritePrivateProfileStringW(INI_VALUES_SECTION, INI_SHADOW_CASCADES, cascades_text, ini_path.c_str());
    WritePrivateProfileStringW(INI_VALUES_SECTION, INI_SHADOW_MAP_SIZE, size_text, ini_path.c_str());
    write_game();
    if (changed_byte != 0) {
      *at(changed_byte) ^= 1;
    }
    return init_copy(folder, MODULE_NAME, api);
  };
  Expect(cascades_are(GAME_CASCADES), "cascades: the fake game holds the game's counts");
  {
    const auto [plugin, result] = cascade_case(L"cascades_1", L"1", L"0", true, 0);
    Expect(result == 0 && plugin.block->rows_applied == ALL_ROWS && plugin.block->rows_refused == RowBit(ROW_GAME_CODE)
               && cascades_are(TWO_CASCADES) && site_untouched(SITES[LABEL_SHADOW_SIZE_HIGH])
               && site_untouched(SITES[LABEL_SHADOW_SIZE_MAX]) && site_untouched(SITES[LABEL_SHADOW_SIZE_MEDIUM]),
           "cascades: ShadowCascades=1 writes 2, 0, 2, 0 at the four cascade sites, the sizes stay the game's");
    Expect(Logged("renodx_attila64: shadow cascades 2: patched") && Logged("renodx_attila64: shadow map: game's size")
               && Logged("renodx_attila64: site SHADOW_EXTRA_MAX 0x30e4717: applied")
               && !Logged("site SHADOW_EXTRA_MAX 0x30e4717: applied (") && !Logged("site SHADOW_COUNT_HIGH 0x30e46d8: applied (")
               && Logged("renodx_attila64: site SHADOW_SIZE_MAX 0x30e46eb: applied (game's bytes kept)") && !Logged("game's counts"),
           "cascades: the row's line and each written site's");
    plugin.game_started(&api);
    Expect(plugin.block->rows_changed.load() == 0 && Logged(every_site_unchanged),
           "cascades: game start finds the written counts unchanged");
    *at(SITES[LABEL_SHADOW_EXTRA_MAX].address) ^= 1;
    plugin.game_started(&api);
    Expect(plugin.block->rows_changed.load() == RowBit(ROW_SHADOW_MAP) && Logged("SHADOW_EXTRA_MAX changed after we patched it"),
           "cascades: a later change of a written count is seen");
    *at(SITES[LABEL_SHADOW_EXTRA_MAX].address) ^= 1;
    plugin.block->rows_changed.store(0);
    plugin.game_started(&api);
    Expect(plugin.block->rows_changed.load() == 0 && !Logged("Medium's own writes changed"),
           "cascades: Medium's writes unchanged at the game's start, no line");
    *at(MEDIUM_CASCADES_ADDRESS + MEDIUM_COUNT_OFFSET) ^= 1;
    plugin.game_started(&api);
    Expect(plugin.block->rows_changed.load() == RowBit(ROW_SHADOW_MAP)
               && Logged("renodx_attila64: shadow cascades: Medium's own writes changed after we patched the counts"),
           "cascades: Medium's writes changed after the counts were written are seen at the game's start");
  }
  {
    const auto [plugin, result] = cascade_case(L"cascades_1_size_1", L"1", L"1", true, 0);
    const std::array<float, 2> doubled = {4096.f, 4096.f};
    Expect(result == 0 && plugin.block->rows_applied == ALL_ROWS && cascades_are(TWO_CASCADES)
               && std::memcmp(at(SITES[LABEL_SHADOW_SIZE_MAX].address), doubled.data(), sizeof(doubled)) == 0
               && Logged("renodx_attila64: shadow cascades 2: patched") && Logged("renodx_attila64: shadow map 4096: patched")
               && !Logged("(game's bytes kept)"),
           "cascades: with ShadowMapSize=1 too all seven sites are written");
  }
  {
    const auto [plugin, result] = cascade_case(L"cascades_0", L"0", L"0", true, 0);
    Expect(result == 0 && plugin.block->rows_applied == ALL_ROWS && shadow_sizes(2048.f, 1024.f) && cascades_are(GAME_CASCADES)
               && Logged("renodx_attila64: shadow cascades: game's counts") && !Logged("game's counts (")
               && Logged("renodx_attila64: site SHADOW_EXTRA_MAX 0x30e4717: applied (game's bytes kept)"),
           "cascades: ShadowCascades=0 keeps the game's bytes, the row is in");
    *at(MEDIUM_CASCADES_ADDRESS + MEDIUM_COUNT_OFFSET) ^= 1;
    plugin.game_started(&api);
    Expect(plugin.block->rows_changed.load() == 0 && !Logged("Medium's own writes changed"),
           "cascades: with nothing written Medium's writes are not watched at the game's start");
  }
  {
    const auto [plugin, result] = cascade_case(L"cascades_not_dx11", L"1", L"0", false, 0);
    Expect(result == 0 && plugin.block->rows_applied == ALL_ROWS && shadow_sizes(2048.f, 1024.f)
               && Logged("renodx_attila64: shadow cascades: game's counts (ShadowCascades=1 not written: not DX11)"),
           "cascades: nothing is written off the DirectX 11 route");
  }
  for (const auto& [folder, key_text] : {std::pair{L"cascades_2", L"2"}, std::pair{L"cascades_minus_1", L"-1"}}) {
    const auto [plugin, result] = cascade_case(folder, key_text, L"0", true, 0);
    Expect(result == 0 && plugin.block->rows_applied == ALL_ROWS && shadow_sizes(2048.f, 1024.f)
               && Logged("renodx_attila64: shadow cascades: game's counts (ShadowCascades=") && Logged(" out of range)"),
           "cascades: a key other than 0 or 1 writes nothing");
  }
  // The fourth cascade site is not the game's: the row is refused, none of its seven sites is written.
  {
    const auto [plugin, result] = cascade_case(L"cascades_differs", L"1", L"1", true, SITES[LABEL_SHADOW_EXTRA_MAX].address);
    *at(SITES[LABEL_SHADOW_EXTRA_MAX].address) ^= 1;
    Expect(result == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_SHADOW_MAP))
               && plugin.block->rows_refused == (RowBit(ROW_SHADOW_MAP) | RowBit(ROW_GAME_CODE)) && shadow_sizes(2048.f, 1024.f)
               && Logged("site SHADOW_EXTRA_MAX 0x30e4717: skipped (DIFFERS)") && Logged("site SHADOW_COUNT_HIGH 0x30e46d8: skipped (ROW)")
               && !Logged("renodx_attila64: shadow cascades") && !Logged("renodx_attila64: shadow map") && !Logged("withdrawn"),
           "cascades: the fourth cascade site differs, the whole row stays out with the game's bytes");
  }
  // Medium's own cascade writes are not 2 and 0: the row is refused whatever the keys say.
  for (const auto& [folder, key_text, offset] :
       {std::tuple{L"cascades_medium_count", L"1", MEDIUM_COUNT_OFFSET}, std::tuple{L"cascades_medium_extra", L"1", MEDIUM_EXTRA_OFFSET},
        std::tuple{L"cascades_medium_key_0", L"0", MEDIUM_COUNT_OFFSET}}) {
    const auto [plugin, result] = cascade_case(folder, key_text, L"1", true, MEDIUM_CASCADES_ADDRESS + offset);
    Expect(result == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_SHADOW_MAP))
               && plugin.block->rows_refused == (RowBit(ROW_SHADOW_MAP) | RowBit(ROW_GAME_CODE)) && shadow_sizes(2048.f, 1024.f)
               && Logged("renodx_attila64: shadow cascades: Medium's own writes at 0x30e4751 are not the game's, the shadow map row stays out")
               && Logged("site SHADOW_SIZE_HIGH 0x30e46b7: skipped (ROW)") && Logged("site SHADOW_EXTRA_MAX 0x30e4717: skipped (ROW)")
               && !Logged("patched") && !Logged("game's counts") && !Logged("withdrawn"),
           "cascades: Medium's writes are not the game's, the whole row stays out");
  }
  // The loader refuses the fourth cascade write: the three counts written before it go back, the sizes come out too.
  {
    SetEnvironmentVariableA(tweaks::ROUTE_VARIABLE, "1");
    set_ini_key_off(nullptr);
    WritePrivateProfileStringW(INI_VALUES_SECTION, INI_SHADOW_CASCADES, L"1", ini_path.c_str());
    write_game();
    failing_patch_calls = 1u << 3;
    const auto [plugin, result] = init_copy(L"cascades_write_fails", MODULE_NAME, api);
    Expect(result == 0 && failing_patch_calls == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_SHADOW_MAP))
               && plugin.block->rows_refused == (RowBit(ROW_SHADOW_MAP) | RowBit(ROW_GAME_CODE)) && shadow_sizes(2048.f, 1024.f)
               && !Logged("renodx_attila64: shadow cascades") && !Logged("renodx_attila64: shadow map"),
           "cascades: a refused fourth write rolls the row back to the game's bytes");
    Expect(Logged("site SHADOW_EXTRA_MAX 0x30e4717: skipped (WRITE_FAILED)") && Logged("site SHADOW_SIZE_MEDIUM 0x30e4737: skipped (ROW)")
               && Logged("site SHADOW_COUNT_HIGH 0x30e46d8: withdrawn (a later site of its row failed)")
               && Logged("site SHADOW_EXTRA_HIGH 0x30e46e3: withdrawn (a later site of its row failed)")
               && Logged("site SHADOW_COUNT_MAX 0x30e470c: withdrawn (a later site of its row failed)")
               && Logged("site SHADOW_SIZE_HIGH 0x30e46b7: withdrawn") && Logged("site SHADOW_SIZE_MAX 0x30e46eb: withdrawn")
               && !Logged("could not be put back"),
           "cascades: each site that was in has its withdrawn line");
  }
  // The fifth of the seven is not the game's: nothing of the row is written, the other rows go in.
  SetEnvironmentVariableA(tweaks::ROUTE_VARIABLE, "1");
  set_ini_key_off(nullptr);
  WritePrivateProfileStringW(INI_VALUES_SECTION, INI_SHADOW_MAP_SIZE, L"1", ini_path.c_str());
  write_game();
  *at(SITES[LABEL_SHADOW_COUNT_MAX].address) ^= 1;
  {
    const auto [plugin, result] = init_copy(L"shadow_differs", MODULE_NAME, api);
    *at(SITES[LABEL_SHADOW_COUNT_MAX].address) ^= 1;
    Expect(result == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_SHADOW_MAP))
               && plugin.block->rows_refused == (RowBit(ROW_SHADOW_MAP) | RowBit(ROW_GAME_CODE)) && plugin.block->rows_tried == ALL_ROWS
               && shadow_sizes(2048.f, 1024.f) && !Logged("renodx_attila64: shadow map"),
           "shadow: one site that differs leaves the whole row out, nothing written");
    Expect(Logged("site SHADOW_COUNT_MAX 0x30e470c: skipped (DIFFERS)") && Logged("site SHADOW_EXTRA_MAX 0x30e4717: skipped (NEIGHBOUR)")
               && Logged("site SHADOW_SIZE_HIGH 0x30e46b7: skipped (ROW)") && Logged("site SHADOW_SIZE_MEDIUM 0x30e4737: skipped (ROW)")
               && !Logged("withdrawn"),
           "shadow: the site's own refusal, and the row's for its other sites");
    plugin.game_started(&api);
    char hooks_unchanged[64];
    std::snprintf(hooks_unchanged, sizeof(hooks_unchanged), "game started: %u sites unchanged", JUMP_SITE_COUNT);
    Expect(plugin.block->rows_changed.load() == 0 && Logged(hooks_unchanged), "shadow: a refused row's sites are not re-checked");
  }
  // The loader refuses the third write: the two sizes already written go back.
  write_game();
  failing_patch_calls = 1u << 2;
  {
    const auto [plugin, result] = init_copy(L"shadow_write_fails", MODULE_NAME, api);
    Expect(result == 0 && failing_patch_calls == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_SHADOW_MAP))
               && plugin.block->rows_refused == (RowBit(ROW_SHADOW_MAP) | RowBit(ROW_GAME_CODE)) && shadow_sizes(2048.f, 1024.f) && !Logged("renodx_attila64: shadow map"),
           "shadow: a refused write rolls the row back to the game's bytes");
    Expect(Logged("site SHADOW_SIZE_MEDIUM 0x30e4737: skipped (WRITE_FAILED)")
               && Logged("site SHADOW_SIZE_HIGH 0x30e46b7: withdrawn (a later site of its row failed)")
               && Logged("site SHADOW_SIZE_MAX 0x30e46eb: withdrawn (a later site of its row failed)")
               && Logged("site SHADOW_EXTRA_MAX 0x30e4717: withdrawn (a later site of its row failed)")
               && !Logged("could not be put back"),
           "shadow: each site that was in has its withdrawn line");
  }
  // The loader refuses the second write: the sites after it are left out for their row, only the three before it
  // were in and come out.
  write_game();
  failing_patch_calls = 1u << 1;
  {
    const auto [plugin, result] = init_copy(L"shadow_second_write_fails", MODULE_NAME, api);
    Expect(result == 0 && failing_patch_calls == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_SHADOW_MAP))
               && plugin.block->rows_refused == (RowBit(ROW_SHADOW_MAP) | RowBit(ROW_GAME_CODE)) && shadow_sizes(2048.f, 1024.f) && !Logged("renodx_attila64: shadow map"),
           "shadow: a refused second write leaves the game's bytes everywhere");
    Expect(Logged("site SHADOW_SIZE_MAX 0x30e46eb: skipped (WRITE_FAILED)") && Logged("site SHADOW_COUNT_MAX 0x30e470c: skipped (ROW)")
               && Logged("site SHADOW_EXTRA_MAX 0x30e4717: skipped (ROW)") && Logged("site SHADOW_SIZE_MEDIUM 0x30e4737: skipped (ROW)")
               && Logged("site SHADOW_SIZE_HIGH 0x30e46b7: withdrawn") && Logged("site SHADOW_COUNT_HIGH 0x30e46d8: withdrawn")
               && Logged("site SHADOW_EXTRA_HIGH 0x30e46e3: withdrawn") && !Logged("_MAX 0x30e46eb: withdrawn")
               && !Logged("_MAX 0x30e470c: withdrawn") && !Logged("_MAX 0x30e4717: withdrawn") && !Logged("_MEDIUM 0x30e4737: withdrawn")
               && !Logged("could not be put back"),
           "shadow: after a refused write the row's later sites are skipped for the row, only the earlier ones withdrawn");
  }
  // The loader refuses the third write and then the first put-back (the sprite site's write comes between them): that
  // size keeps our bytes, its line says so, the row stays out and none of its sites is re-checked at the game's start.
  write_game();
  failing_patch_calls = (1u << 2) | (1u << 4);
  {
    const auto [plugin, result] = init_copy(L"shadow_put_back_fails", MODULE_NAME, api);
    Expect(result == 0 && failing_patch_calls == 0 && plugin.block->rows_applied == (ALL_ROWS & ~RowBit(ROW_SHADOW_MAP))
               && plugin.block->rows_refused == (RowBit(ROW_SHADOW_MAP) | RowBit(ROW_GAME_CODE)) && !Logged("renodx_attila64: shadow map"),
           "shadow: a refused put-back leaves the row refused");
    const std::array<float, 2> doubled = {4096.f, 4096.f};
    Expect(std::memcmp(at(SITES[LABEL_SHADOW_SIZE_HIGH].address), doubled.data(), sizeof(doubled)) == 0
               && site_untouched(SITES[LABEL_SHADOW_SIZE_MAX]) && site_untouched(SITES[LABEL_SHADOW_SIZE_MEDIUM]),
           "shadow: only the size whose put-back was refused keeps our bytes");
    Expect(Logged("site SHADOW_SIZE_HIGH 0x30e46b7: its bytes could not be put back")
               && !Logged("site SHADOW_SIZE_HIGH 0x30e46b7: withdrawn")
               && Logged("site SHADOW_SIZE_MAX 0x30e46eb: withdrawn (a later site of its row failed)")
               && Logged("site SHADOW_SIZE_MEDIUM 0x30e4737: skipped (WRITE_FAILED)"),
           "shadow: the put-back's own line names the site");
    plugin.game_started(&api);
    char hooks_unchanged[64];
    std::snprintf(hooks_unchanged, sizeof(hooks_unchanged), "game started: %u sites unchanged", JUMP_SITE_COUNT);
    Expect(plugin.block->rows_changed.load() == 0 && Logged(hooks_unchanged),
           "shadow: a site that could not be put back counts as out at the game's start");
  }
  SetEnvironmentVariableA(tweaks::ROUTE_VARIABLE, nullptr);
  set_ini_key_off(nullptr);

  write_game();
  *at(BUILD_ID_NOTE_ADDRESS + 20) ^= 1;
  {
    const auto [plugin, result] = init_copy(L"wrong_game", MODULE_NAME, api);
    Expect(result == 1 && status_of(plugin) == STATUS_WRONG_GAME && every_site_untouched() && !Logged("loader frame cap"),
           "dll: another game build refuses and writes nothing");
  }

  write_game();
  {
    LoaderApi short_range = api;
    short_range.game_hi = GAME_HI_MIN - 1;
    const auto [plugin, result] = init_copy(L"wrong_loader", MODULE_NAME, short_range);
    Expect(result == 1 && status_of(plugin) == STATUS_WRONG_LOADER && site_untouched(SITES[0]) && !Logged("loader frame cap"),
           "dll: a short game range refuses");
  }
  {
    const auto [plugin, result] = init_copy(L"renamed", L"renodx_attila64_old.dll", api);
    Expect(result == 1 && status_of(plugin) == STATUS_WRONG_LOADER && Logged("renamed") && every_site_untouched()
               && !Logged("loader frame cap"),
           "dll: a renamed file refuses and writes nothing");
  }
  DeleteFileW(ini_path.c_str());

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
