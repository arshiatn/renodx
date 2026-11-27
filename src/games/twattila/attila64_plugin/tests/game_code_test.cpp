/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The game code row's stubs (game_code.hpp) against the game's own bytes. The harness maps the Linux build's load
// segments at their addresses, copies the game's float-to-half function (0x3009b90) and the sprite row's region of 20
// halves (0x32d9de5..0x32da007, its calls pointed at that copy) into code near the game as the references, and runs
// code through a thunk that loads every register, the flags and MXCSR from a state (rsp on a test stack) and captures
// them all where the code ends. Its own checks: a run of no code gives back its state, a state with one flag, MXCSR
// bit, register bit, xmm byte or memory byte changed is seen, each reference run twice gives the same state and memory
// (A/A), and each gives a known answer. A mapped image that is not the one the design read fails before any copied
// byte runs.
// The row, through copies of the built DLL (dll_cases\game_code_*, a ReShade.ini next to this exe for the keys): the
// DLL's own init places the sprite rows' stub and writes its site, with each refusal, the value key, the install key,
// code memory without write access and the re-check at game start.
// The sprite rows' stub as that init placed it, entered through its site, against the reference region: each word at
// its place, every class edge alone in every lane, random and edge frames under all 16 MXCSR modes with the whole
// memory and the registers the game keeps, rows and objects that end at a page without access, every overlap of the
// row, the frame and the objects, the game's whole loop of four rows, and (the long runs, skipped once a check above
// has failed) every 2^32 value in all lanes at once and in each lane alone.
// The half's stub, which the DLL does not write: placed here as the DLL would (its bytes, the game's function behind
// them, the site's jump through the plugin's own ApplyJump), against the reference for all 2^32 inputs, then taken out.
// And the row's word in the DLL file as a batch tool looks for it.
// game_code_test.exe <renodx_attila64.dll> <game ELF> [-bench]; SKIPPED (exit 77, reported by build.ps1 as not run)
// without the ELF or when the game's range is taken. -bench times instead of the long runs (2^20 of their inputs
// only, no gate): the half against its stub as a caller calls it, and the region against the sprite rows' stub a row.

#include <windows.h>

#include <emmintrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../game_code.hpp"
#include "../loader_api.h"
#include "../patcher.hpp"
#include "fake_loader.hpp"

namespace {

using namespace twattila::attila64_plugin;
using namespace twattila::attila64_plugin::test;

// The half's function is its whole held range; the region runs from the sprite rows' site to that stub's way on.
inline constexpr const Site& HALF_SITE = SITES[LABEL_HALF_ENTRY];
inline constexpr const Site& ROWS_SITE = SITES[LABEL_SPRITE_ROWS];
inline constexpr const HeldRange& HALF_RANGE = *std::ranges::find(HELD_RANGES, LABEL_HALF_ENTRY, &HeldRange::label);
inline constexpr const HeldRange& ROWS_RANGE = *std::ranges::find(HELD_RANGES, LABEL_SPRITE_ROWS, &HeldRange::label);
inline constexpr uintptr_t HALF_ENTRY = HALF_RANGE.lo;
inline constexpr size_t HALF_BYTES = HALF_RANGE.hi - HALF_RANGE.lo;
inline constexpr uintptr_t ROWS_REGION = ROWS_SITE.address;
inline constexpr uintptr_t ROWS_REGION_END = SPRITE_ROWS_DONE;
inline constexpr size_t ROWS_CALLS = 20;
inline constexpr uint32_t GAME_CODE = RowBit(ROW_GAME_CODE);
// The half's function as a caller calls it: only rax counts, and what the System V convention lets a callee change.
using HalfCall = uint32_t(ATTILA64_GAME_CALL*)(const uint32_t* value);

inline constexpr uint8_t RAX = 0;
inline constexpr uint8_t RSP = 4;
inline constexpr uint8_t RBP = 5;
inline constexpr uint8_t RSI = 6;
inline constexpr uint8_t RDI = 7;
inline constexpr uint8_t RBX = 3;
inline constexpr uint8_t R13 = 13;
inline constexpr uint8_t R15 = 15;
inline constexpr uint64_t FLAG_CF = 0x1;
inline constexpr uint32_t FLOAT_ONE = 0x3f800000;
inline constexpr uint16_t HALF_ONE = 0x3c00;
// CF PF AF ZF SF OF: the flags a test state may set (with IF and the reserved bit 1; DF stays clear, as the ABI wants).
inline constexpr uint64_t ARITHMETIC_FLAGS = 0x8d5;
inline constexpr uint32_t MXCSR_DEFAULT = 0x1f80;
inline constexpr uint32_t MXCSR_PRECISION = 0x20;

// Everything code under test can see or change in registers. The host fields are the thunk's own.
struct CpuState {
  std::array<uint64_t, 16> gpr = {};  // by register number: rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8-r15
  uint64_t flags = 0x202;
  uint32_t mxcsr = MXCSR_DEFAULT;
  uint32_t host_mxcsr = 0;
  uint64_t host_rsp = 0;
  std::array<std::array<uint8_t, 16>, 16> xmm = {};
};
static_assert(offsetof(CpuState, flags) == 0x80 && offsetof(CpuState, mxcsr) == 0x88
              && offsetof(CpuState, host_rsp) == 0x90 && offsetof(CpuState, xmm) == 0x98);
using Thunk = void(ATTILA64_GAME_CALL*)(const CpuState* in, CpuState* out);

// What a run's memory is: the test stack around rsp, a sprite row, and the two objects the region reads (r13, r15).
struct Arena {
  alignas(64) std::array<uint8_t, 0x600> bytes = {};
  bool operator==(const Arena&) const = default;
};
inline constexpr size_t ARENA_RSP = 0x200;    // 16-aligned: the region's movaps reads of [rsp + 0x10] and [rsp + 0x40]
inline constexpr size_t ARENA_ROW = 0x380;    // rbx: the region writes [rbx + 0x10, rbx + 0x38)
inline constexpr size_t ARENA_R13 = 0x400;    // [r13 + 0x1c, r13 + 0x44)
inline constexpr size_t ARENA_R15 = 0x480;    // [r15 + 0x60, r15 + 0x68)
inline constexpr size_t ARENA_INPUT = 0x500;  // the half's [rdi]

// The row's 20 words in the row's order, [rbx + 0x10 + 2 x lane]: where each one's float comes from. A product is
// [rsp + offset] x [rsp + second] in the game's operand order; both products share [rsp + 0x6c].
enum LaneBase : uint8_t { BASE_RSP, BASE_R13, BASE_R15, BASE_PRODUCT, BASE_ZERO };
struct Lane {
  LaneBase base = BASE_RSP;
  uint8_t offset = 0;
  uint8_t second = 0;
};
inline constexpr size_t ROW_WORDS = 20;
inline constexpr size_t ROW_WORDS_OFFSET = 0x10;
inline constexpr uint8_t SHARED_FACTOR = 0x6c;
inline constexpr std::array<Lane, ROW_WORDS> LANES = {{
    {BASE_RSP, 0x38}, {BASE_RSP, 0x34}, {BASE_R13, 0x1c}, {BASE_RSP, 0x08},
    {BASE_RSP, 0x68}, {BASE_RSP, 0x64}, {BASE_PRODUCT, 0x30, SHARED_FACTOR}, {BASE_R13, 0x40},
    {BASE_PRODUCT, SHARED_FACTOR, 0x2c}, {BASE_RSP, 0x10}, {BASE_ZERO}, {BASE_RSP, 0x40},
    {BASE_R13, 0x28}, {BASE_R13, 0x2c}, {BASE_R13, 0x30}, {BASE_R13, 0x34},
    {BASE_R13, 0x38}, {BASE_R13, 0x3c}, {BASE_R15, 0x60}, {BASE_R15, 0x64},
}};
// One row for the bench's runner: the four registers the region reads through.
struct RowPointers {
  uint64_t rsp = 0;
  uint64_t rbx = 0;
  uint64_t r13 = 0;
  uint64_t r15 = 0;
};
using RowsRunner = void(ATTILA64_GAME_CALL*)(const RowPointers* rows);
// The sprite rows' site replaced one instruction; the game's next one, where the stub's second way out goes on.
inline constexpr std::array<uint8_t, 9> ROWS_SCRATCH_STORE = {0xf3, 0x0f, 0x11, 0x84, 0x24, 0x94, 0x00, 0x00, 0x00};
// The game's loop around the region: from its counter's reset to the function's epilogue.
inline constexpr uintptr_t ROWS_LOOP = 0x32d9d3e;
inline constexpr uintptr_t ROWS_LOOP_END = 0x32da020;
inline constexpr size_t ZERO_LANE = std::ranges::find(LANES, BASE_ZERO, &Lane::base) - LANES.begin();
// a = a float's bits without the sign: the game's function has a path of its own for [DENORMAL_HALF_FIRST,
// NORMAL_HALF_FIRST), the halves below the smallest normal one.
inline constexpr uint32_t DENORMAL_HALF_FIRST = 0x33800000;
inline constexpr uint32_t NORMAL_HALF_FIRST = 0x38800000;
// Both neighbours of every class's first and last value, of the rounding's ties, and of the float's own classes.
inline constexpr std::array<uint32_t, 32> CLASS_EDGES = {
    0x00000000, 0x00000001, 0x007fffff, 0x00800000, 0x33000400, 0x33000401, 0x337fffff, 0x33800000,
    0x33800001, 0x387fffff, 0x38800000, 0x38800001, 0x38801000, 0x38801001, 0x3f800000, 0x3f800fff,
    0x3f801000, 0x3f801001, 0x3f802fff, 0x3f803000, 0x3f803001, 0x477fefff, 0x477ff000, 0x477fffff,
    0x47800000, 0x47800001, 0x7f7fffff, 0x7f800000, 0x7f800001, 0x7fbfffff, 0x7fc00000, 0x7fffffff,
};
// A row's memory of its own for the long runs and the bench: the frame, the row and the two objects, apart.
struct LightRow {
  alignas(64) std::array<uint8_t, 0x200> bytes = {};
};
inline constexpr size_t LIGHT_RSP = 0x40;   // [0x38, 0xd8)
inline constexpr size_t LIGHT_ROW = 0x100;  // [0x110, 0x138)
inline constexpr size_t LIGHT_R13 = 0x140;  // [0x15c, 0x184)
inline constexpr size_t LIGHT_R15 = 0x190;  // [0x1f0, 0x1f8)

// The page-edge runs: an access violation while one is expected goes on at the thunk's way back (rax = the run's out
// state, rsp the host's from it). Code near the game has no unwind data and rsp is on a test stack, so a frame-based
// handler would never be found.
DWORD fault_out_slot = 0;
uintptr_t fault_resume = 0;
thread_local bool fault_expected = false;
thread_local bool fault_seen = false;
LONG CALLBACK ResumeAfterFault(EXCEPTION_POINTERS* info) {
  if (!fault_expected || info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  fault_expected = false;
  fault_seen = true;
  const auto* out = static_cast<const CpuState*>(TlsGetValue(fault_out_slot));
  info->ContextRecord->Rax = reinterpret_cast<DWORD64>(out);
  info->ContextRecord->Rsp = out->host_rsp;
  info->ContextRecord->Rip = fault_resume;
  return EXCEPTION_CONTINUE_EXECUTION;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  // First, before anything else can map there. Executable: the references are the game's own code.
  if (!ReserveGame(PAGE_EXECUTE_READWRITE)) {
    std::printf("SKIPPED: the game range 0x400000..0x527d000 is taken\n");
    return EXIT_SKIPPED;
  }
  if (argc < 3) {
    std::printf("SKIPPED: game_code_test.exe <renodx_attila64.dll> <game ELF> [-bench]\n");
    return EXIT_SKIPPED;
  }
  GameImage image;
  if (const int mapped = MapGameElf(argv[2], &image); mapped != 0) return mapped;
  char exe_dir[MAX_PATH];
  GetModuleFileNameA(nullptr, exe_dir, MAX_PATH);
  *std::strrchr(exe_dir, '\\') = '\0';
  LoaderApi api = MakeApi(exe_dir, image.end);
  const auto at = [](uintptr_t address) { return reinterpret_cast<uint8_t*>(address); };

  // The DLL file holds the row's word as one whole string of its own (a zero byte before and after): a batch tool
  // looks for exactly that and refuses a game-code arm on a DLL without the row. The ini key is a wide string, and
  // the word inside a longer line (or a line ending in it, which a linker may merge the word into) is not that.
  {
    using std::string_view_literals::operator""sv;
    std::ifstream dll_file(argv[1], std::ios::binary);
    const std::string dll_bytes{std::istreambuf_iterator<char>(dll_file), std::istreambuf_iterator<char>()};
    Expect(dll_bytes.find("\0game code\0"sv) != std::string::npos,
           "dll: the file holds the row's word \"game code\" as a whole string");
  }

  // Another image's bytes are never run: the test fails here instead of crashing in them.
  const bool image_read = HeldRangeUnchanged(HALF_RANGE, false) && HeldRangeUnchanged(ROWS_RANGE, false);
  Expect(image_read, "the mapped image holds the half and the sprite rows as the design read them");
  if (!image_read) {
    std::printf("FAIL: %d checks, %d failure(s)\n", checks, failures);
    return 1;
  }

  // The thunk, entered with rdi = in, rsi = out. Ours: a register-exact runner of the game's own bytes (load a whole
  // state, run, store a whole state), emitted here because the state includes rsp on a test stack and MXCSR. It saves
  // only the System V callee-saved registers itself (rbx, rbp, r12-r15); rsi, rdi and xmm6-15, which the host's MS ABI
  // keeps, are saved by the compiler around the call because Thunk is sysv_abi. TLS slots (the TEB's TlsSlots at
  // gs:0x1480) carry what no register may, so threads may run it at once: the out pointer, the code's address, rax
  // while the capture stores the rest, where code that ends in the image goes on (the capture, or the bench's runner),
  // a mark the stub's second way out leaves, and the bench runner's own rsp and end of rows.
  const DWORD out_slot = TlsAlloc();
  const DWORD target_slot = TlsAlloc();
  const DWORD rax_slot = TlsAlloc();
  const DWORD exit_slot = TlsAlloc();
  const DWORD fallback_slot = TlsAlloc();
  const DWORD runner_rsp_slot = TlsAlloc();
  const DWORD runner_end_slot = TlsAlloc();
  if (std::max({out_slot, target_slot, rax_slot, exit_slot, fallback_slot, runner_rsp_slot, runner_end_slot}) >= 64) {
    std::printf("FAIL: no TLS slot below 64 for the thunk\n");
    return 1;
  }
  const auto tls_offset = [](DWORD slot) { return static_cast<uint32_t>(0x1480 + (8 * slot)); };
  auto* thunk_code = static_cast<uint8_t*>(api.code_near_game(0x400));
  uintptr_t capture = 0;
  uintptr_t exit_pad = 0;
  uintptr_t fallback_pad = 0;
  uintptr_t rows_runner = 0;
  uintptr_t rows_runner_next = 0;
  {
    uint8_t* code = thunk_code;
    const auto emit = [&](std::initializer_list<uint8_t> bytes) { code = std::copy(bytes.begin(), bytes.end(), code); };
    const auto emit_value = [&](uint64_t value, size_t size) {
      std::memcpy(code, &value, size);
      code += size;
    };
    // `opcode` with [`base` + displacement32] and register `number` (a general or an xmm one; base below r8).
    const auto emit_access = [&](bool wide, std::initializer_list<uint8_t> opcode, uint8_t number, uint8_t base,
                                 size_t displacement) {
      if (wide || number >= 8) {
        emit({static_cast<uint8_t>(0x40 | (wide ? 0x08 : 0) | ((number >= 8) ? 0x04 : 0))});
      }
      emit(opcode);
      emit({static_cast<uint8_t>(0x80 | ((number & 7) << 3) | base)});
      emit_value(displacement, 4);
    };
    // gs:[slot] with register `number` below r8: prefix, REX.W, opcode, [disp32] without a base.
    const auto emit_tls = [&](std::initializer_list<uint8_t> opcode, uint8_t number, DWORD slot) {
      emit({0x65, 0x48});
      emit(opcode);
      emit({static_cast<uint8_t>((number << 3) | 0x04), 0x25});
      emit_value(tls_offset(slot), 4);
    };
    emit({0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});  // push rbx, rbp, r12-r15
    emit_access(true, {0x89}, RSP, RSI, offsetof(CpuState, host_rsp));     // mov [rsi + host_rsp], rsp
    emit_access(false, {0x0f, 0xae}, 3, RSI, offsetof(CpuState, host_mxcsr));  // stmxcsr [rsi + host_mxcsr]
    emit_tls({0x89}, RSI, out_slot);                                       // mov gs:[out], rsi
    emit_access(false, {0x0f, 0xae}, 2, RDI, offsetof(CpuState, mxcsr));   // ldmxcsr [rdi + mxcsr]
    for (uint8_t number = 0; number < 16; ++number) {
      // movups xmm, [rdi + ..]
      emit_access(false, {0x0f, 0x10}, number, RDI, offsetof(CpuState, xmm) + (16 * number));
    }
    emit_access(true, {0x8b}, RSP, RDI, 8 * RSP);  // mov rsp, [rdi + rsp]: the test stack
    // push qword [rdi + flags]; popfq (the push's slot is the test stack's [rsp - 8], which code under test may write)
    emit_access(false, {0xff}, 6, RDI, offsetof(CpuState, flags));
    emit({0x9d});
    for (const uint8_t number : {0, 1, 2, 3, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 7}) {
      emit_access(true, {0x8b}, number, RDI, 8 * number);  // mov reg, [rdi + ..]; rdi last
    }
    emit({0x65, 0xff, 0x24, 0x25});  // jmp qword gs:[target]
    emit_value(tls_offset(target_slot), 4);

    capture = reinterpret_cast<uintptr_t>(code);
    emit_tls({0x89}, RAX, rax_slot);  // mov gs:[rax slot], rax
    emit_tls({0x8b}, RAX, out_slot);  // mov rax, gs:[out]
    for (const uint8_t number : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}) {
      emit_access(true, {0x89}, number, RAX, 8 * number);  // mov [rax + ..], reg (rsp included)
    }
    emit_access(false, {0x0f, 0xae}, 3, RAX, offsetof(CpuState, mxcsr));  // stmxcsr [rax + mxcsr]
    for (uint8_t number = 0; number < 16; ++number) {
      // movups [rax + ..], xmm
      emit_access(false, {0x0f, 0x11}, number, RAX, offsetof(CpuState, xmm) + (16 * number));
    }
    // Back on the host's stack (a mov leaves the flags), then the flags, the code's rax and the host's MXCSR.
    emit_access(true, {0x8b}, RSP, RAX, offsetof(CpuState, host_rsp));  // mov rsp, [rax + host_rsp]
    fault_resume = reinterpret_cast<uintptr_t>(code);
    emit({0x9c});                                                       // pushfq; pop qword [rax + flags]
    emit_access(false, {0x8f}, 0, RAX, offsetof(CpuState, flags));
    emit_tls({0x8b}, 1, rax_slot);                                      // mov rcx, gs:[rax slot]; mov [rax], rcx
    emit_access(true, {0x89}, 1, RAX, 8 * RAX);
    emit_access(false, {0x0f, 0xae}, 2, RAX, offsetof(CpuState, host_mxcsr));  // ldmxcsr [rax + host_mxcsr]
    emit({0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5d, 0x5b, 0xc3});   // pop r15-r12, rbp, rbx; ret

    // Where code that ends in the image goes on: jmp qword gs:[exit].
    exit_pad = reinterpret_cast<uintptr_t>(code);
    emit({0x65, 0xff, 0x24, 0x25});
    emit_value(tls_offset(exit_slot), 4);
    // The stub's second way out, marked: mov byte gs:[mark], 1 (no register, no flag), the game's instruction that
    // stood at SPRITE_ROWS_FALLBACK, and on to the one after it.
    fallback_pad = reinterpret_cast<uintptr_t>(code);
    emit({0x65, 0xc6, 0x04, 0x25});
    emit_value(tls_offset(fallback_slot), 4);
    emit({0x01});
    code = std::copy(ROWS_SCRATCH_STORE.begin(), ROWS_SCRATCH_STORE.end(), code);
    emit({0xe9});
    emit_value(uint64_t{SPRITE_ROWS_FALLBACK} + ROWS_SCRATCH_STORE.size() - (reinterpret_cast<uintptr_t>(code) + 4), 4);
    // The bench's runner, entered with rdi = rows: each row's four registers loaded, rbp = rsp + 0x94 as the game's
    // loop has it, the code at gs:[target] entered; where that code ends (gs:[exit] = the next row's label here) the
    // next row, up to gs:[end]. r12 is the cursor: the region and the stub keep it.
    rows_runner = reinterpret_cast<uintptr_t>(code);
    emit({0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});  // push rbx, rbp, r12-r15
    emit_tls({0x89}, RSP, runner_rsp_slot);                              // mov gs:[runner rsp], rsp
    emit({0x49, 0x89, 0xfc});                                            // mov r12, rdi
    const uint8_t* next_row = code;
    emit({0x49, 0x8b, 0x24, 0x24});                                      // mov rsp, [r12]
    emit({0x49, 0x8b, 0x5c, 0x24, 0x08});                                // mov rbx, [r12 + 8]
    emit({0x4d, 0x8b, 0x6c, 0x24, 0x10});                                // mov r13, [r12 + 16]
    emit({0x4d, 0x8b, 0x7c, 0x24, 0x18});                                // mov r15, [r12 + 24]
    emit({0x48, 0x8d, 0xac, 0x24, 0x94, 0x00, 0x00, 0x00});              // lea rbp, [rsp + 0x94]
    emit({0x65, 0xff, 0x24, 0x25});                                      // jmp qword gs:[target]
    emit_value(tls_offset(target_slot), 4);
    rows_runner_next = reinterpret_cast<uintptr_t>(code);
    emit({0x49, 0x83, 0xc4, 0x20});                                      // add r12, 32
    emit({0x65, 0x4c, 0x3b, 0x24, 0x25});                                // cmp r12, gs:[end]
    emit_value(tls_offset(runner_end_slot), 4);
    emit({0x72, static_cast<uint8_t>(next_row - (code + 2))});           // jb next_row
    emit_tls({0x8b}, RSP, runner_rsp_slot);                              // mov rsp, gs:[runner rsp]
    emit({0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5d, 0x5b, 0xc3});  // pop r15-r12, rbp, rbx; ret
    Expect(code <= thunk_code + 0x400, "the thunk fits its code");
    FlushInstructionCache(GetCurrentProcess(), thunk_code, static_cast<size_t>(code - thunk_code));
  }
  fault_out_slot = out_slot;
  // One run of the code at `target` from `in`; *out holds what the code left (rax..r15, flags, MXCSR, xmm). Code that
  // ends in the image ends at the capture; the mark of the stub's second way out is taken off first.
  const auto run = [&](uintptr_t target, const CpuState& in, CpuState* out) {
    TlsSetValue(target_slot, reinterpret_cast<void*>(target));
    TlsSetValue(exit_slot, reinterpret_cast<void*>(capture));
    TlsSetValue(fallback_slot, nullptr);
    reinterpret_cast<Thunk>(thunk_code)(&in, out);
  };
  const auto fallback_taken = [&] { return TlsGetValue(fallback_slot) != nullptr; };
  // The first field two states differ in, or nullptr. `flags_mask`: the flag bits that count.
  const auto difference = [](const CpuState& a, const CpuState& b, uint64_t flags_mask) -> const char* {
    if (a.gpr != b.gpr) return "a general register";
    if (((a.flags ^ b.flags) & flags_mask) != 0) return "the flags";
    if (a.mxcsr != b.mxcsr) return "MXCSR";
    if (a.xmm != b.xmm) return "an xmm register";
    return nullptr;
  };

  // The references: the half's own bytes, and the region's with each of its 20 calls (each right after a
  // mov rdi, rbp) pointed at that copy and a jump to the capture after its last instruction.
  auto* reference = static_cast<uint8_t*>(api.code_near_game(HALF_BYTES + (ROWS_REGION_END - ROWS_REGION) + 5));
  uint8_t* reference_half = reference;
  uint8_t* reference_rows = reference + HALF_BYTES;
  std::memcpy(reference_half, at(HALF_ENTRY), HALF_BYTES);
  std::memcpy(reference_rows, at(ROWS_REGION), ROWS_REGION_END - ROWS_REGION);
  size_t calls = 0;
  bool calls_to_half = true;
  for (size_t offset = 0; offset + 8 <= ROWS_REGION_END - ROWS_REGION; ++offset) {
    uint8_t* call = reference_rows + offset;
    if (call[0] != 0x48 || call[1] != 0x89 || call[2] != 0xef || call[3] != 0xe8) continue;
    int32_t displacement = 0;
    std::memcpy(&displacement, call + 4, sizeof(displacement));
    calls_to_half = calls_to_half && ROWS_REGION + offset + 8 + displacement == HALF_ENTRY;
    displacement =
        static_cast<int32_t>(reinterpret_cast<intptr_t>(reference_half) - reinterpret_cast<intptr_t>(call + 8));
    std::memcpy(call + 4, &displacement, sizeof(displacement));
    ++calls;
  }
  // Calls that are not the ones read would run elsewhere: fail before any copy runs.
  const bool calls_read = (calls == ROWS_CALLS && calls_to_half);
  Expect(calls_read, "the region: 20 calls of the half, each after a mov rdi, rbp");
  if (!calls_read) {
    std::printf("FAIL: %d checks, %d failure(s)\n", checks, failures);
    return 1;
  }
  {
    uint8_t* jump = reference_rows + (ROWS_REGION_END - ROWS_REGION);
    const auto back = static_cast<int32_t>(static_cast<intptr_t>(exit_pad) - reinterpret_cast<intptr_t>(jump + 5));
    jump[0] = 0xe9;
    std::memcpy(jump + 1, &back, sizeof(back));
  }
  FlushInstructionCache(GetCurrentProcess(), reference, HALF_BYTES + (ROWS_REGION_END - ROWS_REGION) + 5);

  std::mt19937_64 random(0x5eed1127);
  const auto random_state_from = [](std::mt19937_64* generator) {
    CpuState state;
    for (uint64_t& value : state.gpr) {
      value = (*generator)();
    }
    state.flags = 0x202 | ((*generator)() & ARITHMETIC_FLAGS);
    for (auto& xmm : state.xmm) {
      for (uint8_t& byte : xmm) {
        byte = static_cast<uint8_t>((*generator)());
      }
    }
    return state;
  };
  const auto random_state = [&] { return random_state_from(&random); };
  Arena arena;
  const auto arena_at = [&](size_t offset) { return reinterpret_cast<uintptr_t>(arena.bytes.data() + offset); };

  // A run of no code (the target is the capture itself) gives back its state, rsp included. The controls: one flag, one
  // MXCSR flag, one bit of a general register (not rsp) and one byte of an xmm register flipped going in come back
  // flipped, and the compare must see each; one byte of the memory flipped is a different Arena.
  {
    bool identity = true;
    bool flag_seen = true;
    bool mxcsr_seen = true;
    bool gpr_seen = true;
    bool xmm_seen = true;
    bool arena_seen = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
      CpuState in = random_state();
      in.gpr[RSP] = arena_at(ARENA_RSP);
      CpuState out;
      run(capture, in, &out);
      identity = identity && difference(in, out, ~uint64_t{0}) == nullptr;
      CpuState flipped = in;
      flipped.flags ^= FLAG_CF;
      CpuState flipped_out;
      run(capture, flipped, &flipped_out);
      flag_seen = flag_seen && difference(out, flipped_out, ~uint64_t{0}) != nullptr
                  && (out.flags ^ flipped_out.flags) == FLAG_CF;
      flipped = in;
      flipped.mxcsr ^= MXCSR_PRECISION;
      run(capture, flipped, &flipped_out);
      mxcsr_seen = mxcsr_seen && difference(out, flipped_out, ~uint64_t{0}) != nullptr
                   && flipped_out.mxcsr == (in.mxcsr ^ MXCSR_PRECISION);
      const size_t gpr = (RSP + 1 + (random() % 15)) % 16;
      const uint64_t gpr_bit = uint64_t{1} << (random() % 64);
      flipped = in;
      flipped.gpr[gpr] ^= gpr_bit;
      run(capture, flipped, &flipped_out);
      gpr_seen = gpr_seen && difference(out, flipped_out, ~uint64_t{0}) != nullptr
                 && (out.gpr[gpr] ^ flipped_out.gpr[gpr]) == gpr_bit;
      const size_t xmm = random() % 16;
      const size_t xmm_byte = random() % 16;
      flipped = in;
      flipped.xmm[xmm][xmm_byte] ^= 0x80;
      run(capture, flipped, &flipped_out);
      xmm_seen = xmm_seen && difference(out, flipped_out, ~uint64_t{0}) != nullptr
                 && (out.xmm[xmm][xmm_byte] ^ flipped_out.xmm[xmm][xmm_byte]) == 0x80;
      Arena changed = arena;
      changed.bytes[random() % changed.bytes.size()] ^= 1;
      arena_seen = arena_seen && !(changed == arena);
    }
    Expect(identity, "harness: a run of no code gives back every register, the flags and MXCSR");
    Expect(flag_seen, "harness control: one flag flipped is a difference");
    Expect(mxcsr_seen, "harness control: one MXCSR flag flipped is a difference");
    Expect(gpr_seen, "harness control: one bit of a general register flipped is a difference");
    Expect(xmm_seen, "harness control: one byte of an xmm register flipped is a difference");
    Expect(arena_seen, "harness control: one byte of the memory flipped is a difference");
  }

  // The reference half, called (its return address on the test stack points at the capture), A/A: twice from the same
  // state, the same state and memory out; [rdi] untouched.
  {
    bool same = true;
    bool input_kept = true;
    for (int iteration = 0; iteration < 100000; ++iteration) {
      const auto x = static_cast<uint32_t>(random());
      arena = {};
      std::memcpy(arena.bytes.data() + ARENA_INPUT, &x, sizeof(x));
      std::memcpy(arena.bytes.data() + ARENA_RSP, &capture, sizeof(capture));
      const Arena before = arena;
      CpuState in = random_state();
      in.gpr[RSP] = arena_at(ARENA_RSP);
      in.gpr[RDI] = arena_at(ARENA_INPUT);
      CpuState first;
      run(reinterpret_cast<uintptr_t>(reference_half), in, &first);
      const Arena first_arena = arena;
      arena = before;
      CpuState second;
      run(reinterpret_cast<uintptr_t>(reference_half), in, &second);
      same = same && difference(first, second, ~uint64_t{0}) == nullptr && first_arena == arena;
      input_kept = input_kept && std::memcmp(arena.bytes.data() + ARENA_INPUT, &x, sizeof(x)) == 0
                   && first.gpr[RSP] == in.gpr[RSP] + 8;
    }
    Expect(same, "A/A: the reference half twice from one state gives one state and memory");
    Expect(input_kept, "the reference half leaves [rdi] and returns through the test stack");
    // A known answer, so A/A cannot pass on code that did nothing: the half of 1.0f is 0x3c00.
    arena = {};
    std::memcpy(arena.bytes.data() + ARENA_INPUT, &FLOAT_ONE, sizeof(FLOAT_ONE));
    std::memcpy(arena.bytes.data() + ARENA_RSP, &capture, sizeof(capture));
    CpuState in = random_state();
    in.gpr[RSP] = arena_at(ARENA_RSP);
    in.gpr[RDI] = arena_at(ARENA_INPUT);
    CpuState out;
    run(reinterpret_cast<uintptr_t>(reference_half), in, &out);
    Expect(static_cast<uint16_t>(out.gpr[RAX]) == HALF_ONE, "the reference half of 1.0f is 0x3c00");
  }

  // The reference region over one row, A/A: random frame, row and objects (any bits), twice from the same state.
  // rbp = rsp + 0x94, as the game's loop sets it before the region.
  const auto row_state = [&] {
    for (uint8_t& byte : arena.bytes) {
      byte = static_cast<uint8_t>(random());
    }
    CpuState in = random_state();
    in.gpr[RSP] = arena_at(ARENA_RSP);
    in.gpr[RBP] = arena_at(ARENA_RSP) + 0x94;
    in.gpr[RBX] = arena_at(ARENA_ROW);
    in.gpr[R13] = arena_at(ARENA_R13);
    in.gpr[R15] = arena_at(ARENA_R15);
    return in;
  };
  {
    bool same = true;
    bool kept = true;
    for (int iteration = 0; iteration < 10000; ++iteration) {
      const CpuState in = row_state();
      const Arena before = arena;
      CpuState first;
      run(reinterpret_cast<uintptr_t>(reference_rows), in, &first);
      const Arena first_arena = arena;
      arena = before;
      CpuState second;
      run(reinterpret_cast<uintptr_t>(reference_rows), in, &second);
      same = same && difference(first, second, ~uint64_t{0}) == nullptr && first_arena == arena;
      kept = kept && first.gpr[RSP] == in.gpr[RSP] && first.gpr[RBP] == in.gpr[RBP] && first.gpr[RBX] == in.gpr[RBX]
             && first.gpr[R13] == in.gpr[R13] && first.gpr[R15] == in.gpr[R15];
    }
    Expect(same, "A/A: the reference region twice from one state gives one state and memory");
    Expect(kept, "the reference region keeps rsp, rbp, rbx, r13 and r15");
    // A known answer over the random fill: every source 1.0f (the two products 1.0f x 1.0f), so each of the 20 words is
    // 0x3c00 but the region's constant 0 at +0x24.
    const CpuState in = row_state();
    for (const size_t source : {0x08, 0x10, 0x2c, 0x30, 0x34, 0x38, 0x40, 0x64, 0x68, 0x6c}) {
      std::memcpy(arena.bytes.data() + ARENA_RSP + source, &FLOAT_ONE, sizeof(FLOAT_ONE));
    }
    for (size_t source = 0x1c; source < 0x44; source += 4) {
      std::memcpy(arena.bytes.data() + ARENA_R13 + source, &FLOAT_ONE, sizeof(FLOAT_ONE));
    }
    for (const size_t source : {0x60, 0x64}) {
      std::memcpy(arena.bytes.data() + ARENA_R15 + source, &FLOAT_ONE, sizeof(FLOAT_ONE));
    }
    CpuState out;
    run(reinterpret_cast<uintptr_t>(reference_rows), in, &out);
    bool words_known = true;
    for (size_t word = 0x10; word < 0x38; word += 2) {
      uint16_t value = 0;
      std::memcpy(&value, arena.bytes.data() + ARENA_ROW + word, sizeof(value));
      words_known = words_known && value == ((word == 0x24) ? 0 : HALF_ONE);
    }
    Expect(words_known, "the reference region with every source 1.0f writes 0x3c00 in each word but +0x24's 0");
  }

  // The row through the built DLL: copies under dll_cases\<case>\ (each its own module and globals), on the mapped
  // image. Only the game code row is installed (the other rows' install keys are 0, the engine values off), so the
  // only game bytes a case changes are the two held ranges and their sites' windows, put back before each case.
  wchar_t exe_dir_wide[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_dir_wide, MAX_PATH);
  *std::wcsrchr(exe_dir_wide, L'\\') = L'\0';
  const std::wstring cases_dir = std::wstring(exe_dir_wide) + L"\\dll_cases";
  const std::wstring ini_path = std::wstring(exe_dir_wide) + L"\\ReShade.ini";
  CreateDirectoryW(cases_dir.c_str(), nullptr);
  std::vector<std::vector<uint8_t>> game_bytes;
  for (const HeldRange& range : HELD_RANGES) {
    game_bytes.emplace_back(at(range.lo - SITE_MARGIN), at(range.hi + SITE_MARGIN));
  }
  const auto write_ini = [&](bool installed, bool stubs_wanted) {
    DeleteFileW(ini_path.c_str());
    WritePrivateProfileStringW(INI_SECTION, INI_TWEAKS, L"0", ini_path.c_str());
    for (uint32_t row = 0; row < ROW_COUNT; ++row) {
      if (row == ROW_GAME_CODE && installed) continue;
      WritePrivateProfileStringW(INI_SECTION, ROW_INI_KEYS[row], L"0", ini_path.c_str());
    }
    if (!stubs_wanted) {
      WritePrivateProfileStringW(INI_VALUES_SECTION, INI_FASTER_GAME_CODE, L"0", ini_path.c_str());
    }
  };
  // The game's bytes back but for one bit of the byte at `changed` (0: none), one DLL copy loaded, the log emptied;
  // the copy and its init's result (-1: not loaded).
  const auto init_case = [&](const wchar_t* folder, uintptr_t changed) {
    for (size_t index = 0; index < HELD_RANGES.size(); ++index) {
      std::memcpy(at(HELD_RANGES[index].lo - SITE_MARGIN), game_bytes[index].data(), game_bytes[index].size());
    }
    if (changed != 0) {
      *at(changed) ^= 1;
    }
    const std::wstring directory = cases_dir + L"\\" + folder;
    CreateDirectoryW(directory.c_str(), nullptr);
    const std::wstring target = directory + L"\\" + MODULE_NAME;
    Expect(CopyFileW(argv[1], target.c_str(), FALSE) != FALSE, "a copy of the plugin DLL");
    const Plugin plugin = LoadPlugin(target.c_str());
    {
      const std::scoped_lock lock(log_mutex);
      log_lines.clear();
    }
    return std::pair{plugin, ((plugin.init != nullptr) ? plugin.init(&api) : -1)};
  };
  const auto logged_line = [](const std::string& line) {
    const std::shared_lock lock(log_mutex);
    return std::ranges::find(log_lines, line) != log_lines.end();
  };
  const auto site_line = [](const Site& site, const char* state) {
    char line[128];
    std::snprintf(line, sizeof(line), "renodx_attila64: site %s %#x: %s", LABEL_WORDS[site.label], site.address, state);
    return std::string(line);
  };
  const auto changed_line = [](const Site& site) {
    return std::string("renodx_attila64: site ") + LABEL_WORDS[site.label] + " changed after we patched it";
  };
  const auto holds_game_bytes = [&](const Site& site) {
    return std::memcmp(at(site.address), site.window.data() + SITE_MARGIN, site.length) == 0;
  };
  // The rows words of a loaded copy: applied, refused (this row's bit) and tried.
  const auto rows_are = [](const Plugin& plugin, uint32_t applied, uint32_t refused, uint32_t tried) {
    return plugin.block != nullptr && plugin.block->rows_applied == applied
           && (plugin.block->rows_refused & GAME_CODE) == refused && plugin.block->rows_tried == tried;
  };
  const auto next_code = [&] { return reinterpret_cast<uintptr_t>(api.code_near_game(0)); };
  // The stubs' place in code memory handed out from `memory` on, after `lead` bytes: on the next cache line.
  const auto stubs_at = [](uintptr_t memory, size_t lead) {
    return (memory + lead + (game_code::BLOCK_ALIGNMENT - 1)) & ~uintptr_t{game_code::BLOCK_ALIGNMENT - 1};
  };
  // The sprite rows' site jumps to `stubs`, int3 after the jump; there the stub's bytes, its two jumps leading to the
  // game's code after the region and after the site.
  const auto rows_stub_is_in = [&](uintptr_t stubs) {
    std::array<uint8_t, 6> jump = {0xe9, 0, 0, 0, 0, 0xcc};
    const auto displacement = static_cast<uint32_t>(stubs - (ROWS_SITE.address + 5));
    std::memcpy(jump.data() + 1, &displacement, sizeof(displacement));
    auto stub = game_code::SPRITE_ROWS;
    for (const auto& [rel32_offset, target] : std::initializer_list<std::pair<size_t, uint32_t>>{
             {0x4b9, SPRITE_ROWS_DONE}, {0x4c4, SPRITE_ROWS_FALLBACK}}) {
      const auto back = static_cast<uint32_t>(target - (stubs + rel32_offset + 4));
      std::memcpy(stub.data() + rel32_offset, &back, sizeof(back));
    }
    return std::memcmp(at(ROWS_SITE.address), jump.data(), jump.size()) == 0
           && std::memcmp(at(stubs), stub.data(), stub.size()) == 0;
  };
  const size_t stubs_bytes = (game_code::BLOCK_ALIGNMENT - 1) + game_code::LAYOUT.bytes;
  const auto stubs_line = [](uintptr_t stubs) {
    char line[160];
    std::snprintf(line, sizeof(line), "renodx_attila64: game code stubs %#zx..%#zx (sprite rows 0x51b bytes at %#zx)",
                  stubs, stubs + game_code::LAYOUT.bytes, stubs);
    return std::string(line);
  };

  std::printf("  the row: in by default\n");
  {
    write_ini(true, true);
    const uintptr_t memory = next_code();
    const uintptr_t stubs = stubs_at(memory, game_code::LEAD_GAP_BYTES);
    const size_t used_before = near_code_used;
    const auto [plugin, result] = init_case(L"game_code_default", 0);
    Expect(result == 0 && rows_are(plugin, GAME_CODE, 0, GAME_CODE)
               && logged_line(site_line(HALF_SITE, "checked (check only)"))
               && logged_line(site_line(ROWS_SITE, "applied")) && logged_line(stubs_line(stubs))
               && Logged("sprites off, game code on"),
           "row: in, the sprite rows' site applied, the half's site checked, the stubs' line");
    Expect(rows_stub_is_in(stubs) && holds_game_bytes(HALF_SITE)
               && near_code_used - used_before == game_code::LEAD_GAP_BYTES + stubs_bytes,
           "row: the sprite rows' stub on a cache line past the gap with its two jumps into the game, its site's jump "
           "to it, the half's site the game's");
    if (plugin.game_started != nullptr) {
      plugin.game_started(&api);
      Expect(plugin.block->rows_changed.load() == 0 && Logged("game started: 1 sites unchanged")
                 && !Logged("changed after we patched it"),
             "game start: the written site and both held ranges are unchanged (our own jump is not a change)");
      // Each change after the init is seen at game start, in the range's site's words: a byte of the half's function
      // (the stub's other way calls it), that function's own site (not written: its whole range counts), a byte of
      // the sprite rows' range, and our jump itself.
      const auto changed_at_start = [&](uintptr_t address) {
        {
          const std::scoped_lock lock(log_mutex);
          log_lines.clear();
        }
        *at(address) ^= 1;
        plugin.game_started(&api);
        *at(address) ^= 1;
        return (plugin.block->rows_changed.load() & GAME_CODE) != 0;
      };
      Expect(changed_at_start(HALF_RANGE.hi - 1) && logged_line(changed_line(HALF_SITE))
                 && !logged_line(changed_line(ROWS_SITE)) && Logged("game started: 1 sites unchanged"),
             "game start: a changed byte of the half's function is seen");
      Expect(changed_at_start(HALF_SITE.address) && logged_line(changed_line(HALF_SITE))
                 && !logged_line(changed_line(ROWS_SITE)),
             "game start: a changed byte of the site that was only checked is seen");
      Expect(changed_at_start(ROWS_RANGE.lo) && logged_line(changed_line(ROWS_SITE))
                 && !logged_line(changed_line(HALF_SITE)),
             "game start: a changed byte of the sprite rows' range is seen");
      Expect(changed_at_start(ROWS_SITE.address + 1) && logged_line(changed_line(ROWS_SITE))
                 && !logged_line(changed_line(HALF_SITE)) && Logged("game started: 0 sites unchanged"),
             "game start: our changed jump is seen, once");
    }
  }
  std::printf("  the row: refusals\n");
  // One changed byte before the init: both sites say `half_words` / `rows_words`, the row is refused and still tried,
  // and nothing is written (the byte changed back, both ranges are the game's) or taken.
  const auto refused_case = [&](const wchar_t* folder, uintptr_t changed, const char* half_words,
                                const char* rows_words) {
    write_ini(true, true);
    const size_t used_before = near_code_used;
    const auto [plugin, result] = init_case(folder, changed);
    *at(changed) ^= 1;
    bool game_kept = true;
    for (size_t index = 0; index < HELD_RANGES.size(); ++index) {
      game_kept = game_kept && std::equal(game_bytes[index].begin(), game_bytes[index].end(),
                                          at(HELD_RANGES[index].lo - SITE_MARGIN));
    }
    return result == 0 && rows_are(plugin, 0, GAME_CODE, GAME_CODE) && logged_line(site_line(HALF_SITE, half_words))
           && logged_line(site_line(ROWS_SITE, rows_words)) && Logged("sprites off, game code off")
           && !Logged("game code stubs") && near_code_used == used_before && game_kept;
  };
  Expect(refused_case(L"game_code_half_changed", HALF_RANGE.hi - 1, "skipped (HELD)", "skipped (HELD)"),
         "row: a changed byte of the half's function refuses the whole row, the sprite rows' site too");
  Expect(refused_case(L"game_code_rows_changed", ROWS_RANGE.lo, "skipped (HELD)", "skipped (HELD)"),
         "row: a changed byte of the sprite rows' range refuses the whole row");
  Expect(refused_case(L"game_code_site_changed", ROWS_SITE.address, "skipped (HELD)", "skipped (DIFFERS)"),
         "row: a changed byte of the sprite rows' site refuses it as its own, and the row");
  Expect(refused_case(L"game_code_half_site_changed", HALF_SITE.address, "skipped (DIFFERS)", "skipped (HELD)"),
         "row: a changed byte of the half's site, which is only checked, refuses the row");
  {
    write_ini(true, true);
    failing_patch_calls = 1;
    const auto [plugin, result] = init_case(L"game_code_patch_refused", 0);
    Expect(result == 0 && failing_patch_calls == 0 && rows_are(plugin, 0, GAME_CODE, GAME_CODE)
               && logged_line(site_line(ROWS_SITE, "skipped (WRITE_FAILED)")) && !Logged("game code stubs")
               && !Logged("withdrawn") && Logged("sprites off, game code off") && holds_game_bytes(HALF_SITE)
               && holds_game_bytes(ROWS_SITE),
           "row: the sprite rows' jump refused by the loader refuses the row, both sites the game's");
  }
  {
    // The stubs' code memory may be asked for (the fake loader counts nothing it refuses).
    write_ini(true, true);
    hook_mode = HOOK_NO_MEMORY;
    const auto [plugin, result] = init_case(L"game_code_no_memory", 0);
    hook_mode = HOOK_NORMAL;
    Expect(result == 0 && rows_are(plugin, 0, GAME_CODE, GAME_CODE)
               && logged_line(site_line(HALF_SITE, "skipped (NO_MEMORY)"))
               && logged_line(site_line(ROWS_SITE, "skipped (NO_MEMORY)")) && !Logged("game code stubs")
               && holds_game_bytes(HALF_SITE) && holds_game_bytes(ROWS_SITE),
           "row: no code memory, with the gap or without, refuses the row and writes nothing");
  }
  {
    write_ini(true, true);
    const uintptr_t memory = next_code();
    const size_t used_before = near_code_used;
    failing_code_calls = 1;
    const auto [plugin, result] = init_case(L"game_code_no_gap", 0);
    Expect(result == 0 && failing_code_calls == 0 && rows_are(plugin, GAME_CODE, 0, GAME_CODE)
               && rows_stub_is_in(stubs_at(memory, 0)) && logged_line(stubs_line(stubs_at(memory, 0)))
               && near_code_used - used_before == stubs_bytes,
           "row: no code memory for the gap puts the stubs in without it");
  }
  {
    write_ini(true, false);
    const size_t used_before = near_code_used;
    const auto [plugin, result] = init_case(L"game_code_value_0", 0);
    Expect(result == 0 && rows_are(plugin, GAME_CODE, 0, GAME_CODE)
               && logged_line(site_line(HALF_SITE, "checked (value 0)"))
               && logged_line(site_line(ROWS_SITE, "checked (value 0)")) && Logged("sprites off, game code on")
               && !Logged("game code stubs") && near_code_used == used_before && holds_game_bytes(HALF_SITE)
               && holds_game_bytes(ROWS_SITE),
           "row: the value key 0 leaves the row in with nothing written or taken");
    if (plugin.game_started != nullptr) {
      *at(ROWS_RANGE.lo) ^= 1;
      plugin.game_started(&api);
      *at(ROWS_RANGE.lo) ^= 1;
      Expect(plugin.block->rows_changed.load() == 0 && Logged("game started: 0 sites unchanged")
                 && !Logged("changed after we patched it"),
             "game start: with nothing written the held ranges are not looked at again");
    }
  }
  {
    write_ini(false, true);
    const size_t used_before = near_code_used;
    const auto [plugin, result] = init_case(L"game_code_key_0", 0);
    Expect(result == 0 && rows_are(plugin, 0, GAME_CODE, 0) && logged_line(site_line(HALF_SITE, "skipped (OFF)"))
               && logged_line(site_line(ROWS_SITE, "skipped (OFF)")) && Logged("sprites off, game code off")
               && near_code_used == used_before && holds_game_bytes(HALF_SITE) && holds_game_bytes(ROWS_SITE),
           "row: the install key 0 leaves the row out, and out of the rows tried");
  }
  {
    // Code memory the loader mapped without write access: opened for the copy, closed again.
    write_ini(true, true);
    const uintptr_t memory = next_code();
    const uintptr_t stubs = stubs_at(memory, game_code::LEAD_GAP_BYTES);
    DWORD before = 0;
    VirtualProtect(at(memory), game_code::LEAD_GAP_BYTES + stubs_bytes, PAGE_EXECUTE_READ, &before);
    const auto [plugin, result] = init_case(L"game_code_read_only", 0);
    MEMORY_BASIC_INFORMATION page = {};
    Expect(result == 0 && rows_are(plugin, GAME_CODE, 0, GAME_CODE) && rows_stub_is_in(stubs)
               && VirtualQuery(at(stubs), &page, sizeof(page)) != 0 && page.Protect == PAGE_EXECUTE_READ
               && !Logged("stays writable"),
           "row: code memory without write access: the stubs are in, the access as it was");
    VirtualProtect(at(memory), game_code::LEAD_GAP_BYTES + stubs_bytes, before, &before);
  }

  // The sprite rows' stub for its runs: as one more init placed it, entered through the game's own bytes.
  write_ini(true, true);
  const auto [rows_plugin, rows_result] = init_case(L"game_code_rows", 0);
  DeleteFileW(ini_path.c_str());
  std::array<uint8_t, 6> site_jump = {};
  std::memcpy(site_jump.data(), at(ROWS_SITE.address), site_jump.size());
  const bool rows_in = (rows_result == 0 && rows_are(rows_plugin, GAME_CODE, 0, GAME_CODE) && site_jump[0] == 0xe9
                        && std::equal(ROWS_SCRATCH_STORE.begin(), ROWS_SCRATCH_STORE.end(), at(SPRITE_ROWS_FALLBACK)));
  Expect(rows_in, "the sprite rows' stub is in for its runs, the game's next instruction as it was read");
  if (!rows_in) {
    std::printf("FAIL: %d checks, %d failure(s)\n", checks, failures);
    return failures;
  }
  // From here on the image holds three jumps of the test's own (so no game start is asked of a copy again): the
  // game's instruction after the site goes through the mark's pad, so that a run tells which way the stub took, and
  // the code ends at a jump to the thunk's way on: after the region for one row, at the loop's end for the loop.
  const auto write_jump = [&](uintptr_t address, uintptr_t target) {
    *at(address) = 0xe9;
    const auto displacement = static_cast<uint32_t>(target - (address + 5));
    std::memcpy(at(address + 1), &displacement, sizeof(displacement));
  };
  std::array<uint8_t, 5> region_end_bytes = {};
  std::array<uint8_t, 5> loop_end_bytes = {};
  std::memcpy(region_end_bytes.data(), at(ROWS_REGION_END), region_end_bytes.size());
  std::memcpy(loop_end_bytes.data(), at(ROWS_LOOP_END), loop_end_bytes.size());
  write_jump(SPRITE_ROWS_FALLBACK, fallback_pad);
  write_jump(ROWS_REGION_END, exit_pad);
  // The site as the game has it (the reference of the whole-state runs: the game's own code where it is) or ours.
  const auto put_site = [&](const uint8_t* bytes) { std::memcpy(at(ROWS_SITE.address), bytes, ROWS_SITE.length); };

  // Where a lane's own float is (a product's: the factor it does not share).
  const auto lane_source = [&](const Lane& lane, uintptr_t rsp, uintptr_t r13, uintptr_t r15) {
    switch (lane.base) {
      case BASE_R13:     return at(r13 + lane.offset);
      case BASE_R15:     return at(r15 + lane.offset);
      case BASE_PRODUCT: return at(rsp + ((lane.offset == SHARED_FACTOR) ? lane.second : lane.offset));
      default:           return at(rsp + lane.offset);
    }
  };
  // The 20 words of the row state `in` by the lane table and the reference half. A product's factors are multiplied
  // here in the host's rounding, which is the game's only for a factor of 1.0f or a NaN beside a number.
  const auto expected_words = [&](const CpuState& in) {
    std::array<uint16_t, ROW_WORDS> words = {};
    for (size_t index = 0; index < ROW_WORDS; ++index) {
      const Lane& lane = LANES[index];
      if (lane.base == BASE_ZERO) continue;
      uint32_t x = 0;
      if (lane.base == BASE_PRODUCT) {
        x = static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_castps_si128(
            _mm_mul_ss(_mm_load_ss(reinterpret_cast<const float*>(at(in.gpr[RSP] + lane.offset))),
                       _mm_load_ss(reinterpret_cast<const float*>(at(in.gpr[RSP] + lane.second)))))));
      } else {
        std::memcpy(&x, lane_source(lane, in.gpr[RSP], in.gpr[R13], in.gpr[R15]), sizeof(x));
      }
      words[index] = static_cast<uint16_t>(reinterpret_cast<HalfCall>(reference_half)(&x));
    }
    return words;
  };
  // The row state `in` through the game's entry: the row's 20 words (filled with another value first) are the expected.
  const auto row_words_right = [&](const CpuState& in) {
    std::memset(at(in.gpr[RBX] + ROW_WORDS_OFFSET), 0xa5, 2 * ROW_WORDS);
    const std::array<uint16_t, ROW_WORDS> expected = expected_words(in);
    CpuState out;
    run(ROWS_REGION, in, &out);
    return std::memcmp(at(in.gpr[RBX] + ROW_WORDS_OFFSET), expected.data(), sizeof(expected)) == 0;
  };
  // A row state with 1.0f in every float of the memory.
  const auto ones_row_state = [&] {
    const CpuState in = row_state();
    for (size_t offset = 0; offset < arena.bytes.size(); offset += sizeof(FLOAT_ONE)) {
      std::memcpy(arena.bytes.data() + offset, &FLOAT_ONE, sizeof(FLOAT_ONE));
    }
    return in;
  };

  std::printf("  the sprite rows: words, class edges\n");
  {
    // Each word at its place: a different float in every lane (2.0f, 3.0f, ..; a product's own factor, the shared one
    // 1.0f). The lane table itself first, against the game's region; then the stub, on its own path.
    const CpuState in = ones_row_state();
    for (size_t index = 0; index < ROW_WORDS; ++index) {
      const auto value = static_cast<float>(index + 2);
      std::memcpy(lane_source(LANES[index], in.gpr[RSP], in.gpr[R13], in.gpr[R15]), &value, sizeof(value));
    }
    const std::array<uint16_t, ROW_WORDS> expected = expected_words(in);
    CpuState out;
    run(reinterpret_cast<uintptr_t>(reference_rows), in, &out);
    bool distinct = true;
    for (size_t index = 0; index < ROW_WORDS; ++index) {
      distinct = distinct && std::ranges::count(expected, expected[index]) == 1;
    }
    Expect(distinct
               && std::memcmp(arena.bytes.data() + ARENA_ROW + ROW_WORDS_OFFSET, expected.data(), sizeof(expected)) == 0,
           "the lane table: the game's region writes each lane's half at its word, 20 different ones");
    Expect(row_words_right(in) && !fallback_taken(),
           "the sprite rows' stub writes each lane's half at its word, on its own path");
  }
  {
    // Every class edge, with either sign, alone in every lane (the others 1.0f): the words, and the way the stub
    // took: the game's code exactly for a denormal half.
    const CpuState in = ones_row_state();
    bool words_right = true;
    bool ways_right = true;
    for (const Lane& lane : LANES) {
      if (lane.base == BASE_ZERO) continue;
      uint8_t* source = lane_source(lane, in.gpr[RSP], in.gpr[R13], in.gpr[R15]);
      for (const uint32_t edge : CLASS_EDGES) {
        for (const uint32_t sign : {0u, 0x80000000u}) {
          const uint32_t x = edge | sign;
          std::memcpy(source, &x, sizeof(x));
          // The run first: the way it took is read after it.
          const bool words = row_words_right(in);
          words_right = words_right && words;
          ways_right = ways_right && fallback_taken() == (edge >= DENORMAL_HALF_FIRST && edge < NORMAL_HALF_FIRST);
        }
      }
      std::memcpy(source, &FLOAT_ONE, sizeof(FLOAT_ONE));
    }
    // And 10^4 random denormal halves, either sign, each alone in a lane.
    for (int iteration = 0; iteration < 10000; ++iteration) {
      const Lane& lane = LANES[(ZERO_LANE + 1 + (random() % (ROW_WORDS - 1))) % ROW_WORDS];
      uint8_t* source = lane_source(lane, in.gpr[RSP], in.gpr[R13], in.gpr[R15]);
      const uint32_t x = (DENORMAL_HALF_FIRST + static_cast<uint32_t>(random() % (NORMAL_HALF_FIRST - DENORMAL_HALF_FIRST)))
                         | (static_cast<uint32_t>(random()) << 31);
      std::memcpy(source, &x, sizeof(x));
      const bool words = row_words_right(in);
      words_right = words_right && words;
      ways_right = ways_right && fallback_taken();
      std::memcpy(source, &FLOAT_ONE, sizeof(FLOAT_ONE));
    }
    Expect(words_right, "the sprite rows' stub: every class edge alone in every lane gives the game's words");
    Expect(ways_right, "the sprite rows' stub: the game's code does the row exactly when a lane holds a denormal half");
  }

  // One row state through the game's own code where it is (the site the game's) and through the site's jump, both
  // entered at `entry` from the same memory: what differs, or nullptr. Equal: the whole memory, the registers the
  // game's loop keeps (rbx, rsp, rbp, r12-r15) and MXCSR. The slot below rsp holds the game's last return address
  // after its code; the stub's own path writes none there, so it still holds the thunk's flags.
  const auto row_differs = [&](const CpuState& in, uintptr_t entry, bool* fell_back) -> const char* {
    const Arena before = arena;
    put_site(ROWS_SITE.window.data() + SITE_MARGIN);
    CpuState game;
    run(entry, in, &game);
    const Arena game_arena = arena;
    arena = before;
    put_site(site_jump.data());
    CpuState ours;
    run(entry, in, &ours);
    *fell_back = fallback_taken();
    const size_t slot = static_cast<size_t>(in.gpr[RSP] - 8 - arena_at(0));
    if (!*fell_back) {
      if (std::memcmp(arena.bytes.data() + slot, &in.flags, sizeof(in.flags)) != 0) return "the slot below rsp";
      std::memcpy(arena.bytes.data() + slot, game_arena.bytes.data() + slot, sizeof(in.flags));
    }
    if (!(arena == game_arena)) return "the memory";
    for (const uint8_t number : {RBX, RSP, RBP, uint8_t{12}, R13, uint8_t{14}, R15}) {
      if (game.gpr[number] != ours.gpr[number]) return "a register the game keeps";
    }
    return ((game.mxcsr != ours.mxcsr) ? "MXCSR" : nullptr);
  };

  std::printf("  the sprite rows: whole states, overlaps, page ends, the loop\n");
  // MXCSR for mode 0..15: rounding x flush to zero x denormals are zero; every exception masked, the sticky flags any.
  const auto mxcsr_mode = [&](uint32_t mode) {
    return MXCSR_DEFAULT | ((mode & 3) << 13) | (((mode & 4) != 0) ? 0x8000u : 0u) | (((mode & 8) != 0) ? 0x40u : 0u)
           | (static_cast<uint32_t>(random()) & 0x3f);
  };
  {
    // 10^6 frames, random ones (any bits) and frames of class edges in turn, under all 16 MXCSR modes.
    const char* differs = nullptr;
    size_t own_path = 0;
    size_t game_path = 0;
    for (uint32_t mode = 0; mode < 16; ++mode) {
      for (int iteration = 0; iteration < 62500 && differs == nullptr; ++iteration) {
        CpuState in = row_state();
        if ((iteration % 2) != 0) {
          for (size_t offset = ARENA_RSP; offset < ARENA_R15 + 0x68; offset += sizeof(uint32_t)) {
            const uint32_t x = CLASS_EDGES[random() % CLASS_EDGES.size()] | (static_cast<uint32_t>(random()) << 31);
            std::memcpy(arena.bytes.data() + offset, &x, sizeof(x));
          }
        }
        in.mxcsr = mxcsr_mode(mode);
        bool fell_back = false;
        differs = row_differs(in, ROWS_REGION, &fell_back);
        if (fell_back) {
          ++game_path;
        } else {
          ++own_path;
        }
      }
    }
    if (differs != nullptr) {
      std::printf("  whole states: %s differs\n", differs);
    }
    std::printf("  whole states: %zu rows on the stub's own path, %zu through the game's code\n", own_path, game_path);
    Expect(differs == nullptr && own_path > 100000 && game_path > 100000,
           "the sprite rows' stub: random and edge frames under 16 MXCSR modes leave the game's memory, kept "
           "registers and MXCSR");
  }
  {
    // Every overlap of the row, the frame and the two objects that the game's order of loads and stores can see, in
    // steps of 2 bytes: `moved` at `anchor` + [first, last]. Inside, the game's code must do the row (1.0f everywhere:
    // no other reason for it); at both ends the ranges only touch and the stub's own path runs. Any bits as well.
    struct Overlap {
      uint8_t moved = 0;
      uint8_t anchor = 0;
      int16_t first = 0;
      int16_t last = 0;
    };
    const char* differs = nullptr;
    bool inside_game = true;
    bool ends_own = true;
    for (const Overlap& overlap : {Overlap{RBX, RSP, -0x40, 0x88}, Overlap{RBX, R13, -0x1c, 0x34},
                                   Overlap{RBX, R15, 0x28, 0x58}, Overlap{R13, RSP, -0x4c, 0x7c},
                                   Overlap{R15, RSP, -0x70, 0x38}}) {
      for (int distance = overlap.first; distance <= overlap.last && differs == nullptr; distance += 2) {
        for (const bool ones : {true, false}) {
          CpuState in = (ones ? ones_row_state() : row_state());
          in.gpr[overlap.moved] = in.gpr[overlap.anchor] + static_cast<uint64_t>(int64_t{distance});
          bool fell_back = false;
          const char* run_differs = row_differs(in, ROWS_REGION, &fell_back);
          differs = ((differs != nullptr) ? differs : run_differs);
          if (!ones) continue;
          const bool end = (distance == overlap.first || distance == overlap.last);
          inside_game = inside_game && (end || fell_back);
          ends_own = ends_own && (!end || !fell_back);
        }
      }
    }
    if (differs != nullptr) {
      std::printf("  overlaps: %s differs\n", differs);
    }
    Expect(differs == nullptr, "the sprite rows' stub: every overlap of the row, the frame and the objects as the game");
    Expect(inside_game, "the sprite rows' stub: the game's code does every row that overlaps a range it reads");
    Expect(ends_own, "the sprite rows' stub: ranges that only touch stay on its own path");
  }
  {
    // The row and the objects at the start and at the end of memory with no access before and after it: the stub
    // reads and writes inside them. Four bytes past the end the game's region faults, and so does the stub. The frame
    // of these runs is on a stack of its own: Windows writes the fault's record below rsp.
    constexpr size_t FAULT_STACK = 0x10000;
    constexpr size_t PAGE = 0x1000;
    auto* pages =
        static_cast<uint8_t*>(VirtualAlloc(nullptr, FAULT_STACK + (4 * PAGE), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    const auto low = reinterpret_cast<uintptr_t>(pages) + FAULT_STACK + PAGE;
    const uintptr_t high = low + (2 * PAGE);
    for (size_t offset = 0; offset < FAULT_STACK + (4 * PAGE); offset += sizeof(FLOAT_ONE)) {
      std::memcpy(pages + offset, &FLOAT_ONE, sizeof(FLOAT_ONE));
    }
    DWORD before = 0;
    VirtualProtect(at(low - PAGE), PAGE, PAGE_NOACCESS, &before);
    VirtualProtect(at(high), PAGE, PAGE_NOACCESS, &before);
    void* handler = AddVectoredExceptionHandler(1, ResumeAfterFault);
    // The frame on the fault stack, the row and the objects in the middle of the two pages, but `moved` at `value`.
    const auto edge_state = [&](uint8_t moved, uintptr_t value) {
      CpuState in = random_state();
      in.gpr[RSP] = reinterpret_cast<uintptr_t>(pages) + FAULT_STACK - PAGE;
      in.gpr[RBX] = low + 0x800;
      in.gpr[R13] = low + 0x900;
      in.gpr[R15] = low + 0xa00;
      in.gpr[moved] = value;
      in.gpr[RBP] = in.gpr[RSP] + 0x94;
      return in;
    };
    struct Edge {
      uint8_t moved = 0;
      uint8_t first = 0;
      uint8_t end = 0;
    };
    bool inside = true;
    bool game_faults = true;
    bool stub_faults = true;
    for (const Edge& edge : {Edge{RBX, 0x10, 0x38}, Edge{R13, 0x1c, 0x44}, Edge{R15, 0x60, 0x68}, Edge{RSP, 0, 0xa0}}) {
      // The frame's start has the thunk's own slot below it: only its end, which is 8 bytes past the game's frame
      // (rsp is 16-aligned and the frame 0x98 bytes; the saved registers follow it).
      for (const uintptr_t value : {low - edge.first, high - edge.end}) {
        if (edge.moved == RSP && value == low) continue;
        const CpuState in = edge_state(edge.moved, value);
        fault_seen = false;
        fault_expected = true;
        inside = inside && row_words_right(in) && !fault_seen && !fallback_taken();
        fault_expected = false;
      }
      if (edge.moved == RSP) continue;
      const CpuState in = edge_state(edge.moved, high - edge.end + 4);
      CpuState out;
      for (const bool ours : {false, true}) {
        fault_seen = false;
        fault_expected = true;
        run((ours ? ROWS_REGION : reinterpret_cast<uintptr_t>(reference_rows)), in, &out);
        fault_expected = false;
        if (ours) {
          stub_faults = stub_faults && fault_seen;
        } else {
          game_faults = game_faults && fault_seen;
        }
      }
    }
    RemoveVectoredExceptionHandler(handler);
    VirtualFree(pages, 0, MEM_RELEASE);
    Expect(inside, "the sprite rows' stub: a row, an object or the frame that ends (or starts) at memory without "
                   "access gives the game's words, without a fault");
    Expect(game_faults, "page ends control: the game's region faults on a row or an object four bytes past the end");
    Expect(stub_faults, "the sprite rows' stub faults where the game's region does");
  }
  std::uniform_real_distribution<float> position(-1000.f, 1000.f);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  // A float as a sprite's row holds them: a position, a value in 0..1, or zero.
  const auto realistic_float = [&] {
    const uint64_t kind = random() % 6;
    return ((kind == 0) ? 0.f : ((kind < 3) ? unit(random) : position(random)));
  };
  {
    // The game's whole loop of four rows, from its counter's reset to the function's epilogue: the game's code around
    // the region sets r14, xmm4 and rbp itself and reads what the stub leaves. Any bits and realistic floats in turn,
    // 10^4 loops under each of the 16 MXCSR modes.
    std::memcpy(at(ROWS_REGION_END), region_end_bytes.data(), region_end_bytes.size());
    write_jump(ROWS_LOOP_END, exit_pad);
    const char* differs = nullptr;
    size_t own_path = 0;
    size_t game_path = 0;
    for (int iteration = 0; iteration < 160000 && differs == nullptr; ++iteration) {
      CpuState in = row_state();
      in.mxcsr = mxcsr_mode(static_cast<uint32_t>(iteration / 2) % 16);
      if ((iteration % 2) != 0) {
        for (size_t offset = 0; offset < arena.bytes.size(); offset += sizeof(float)) {
          const float value = realistic_float();
          std::memcpy(arena.bytes.data() + offset, &value, sizeof(value));
        }
      }
      in.gpr[RBX] = arena_at(0x340);
      in.gpr[R13] = arena_at(0x470);
      in.gpr[R15] = arena_at(0x4c0);
      bool fell_back = false;
      differs = row_differs(in, ROWS_LOOP, &fell_back);
      if (fell_back) {
        ++game_path;
      } else {
        ++own_path;
      }
    }
    std::memcpy(at(ROWS_LOOP_END), loop_end_bytes.data(), loop_end_bytes.size());
    write_jump(ROWS_REGION_END, exit_pad);
    if (differs != nullptr) {
      std::printf("  the loop: %s differs\n", differs);
    }
    std::printf("  the loop: %zu times every row on the stub's own path, %zu times a row through the game's code\n",
                own_path, game_path);
    Expect(differs == nullptr && own_path > 10000 && game_path > 10000,
           "the game's loop of four rows with the stub leaves the game's memory, kept registers and MXCSR");
  }

  // The long runs: all 2^32 inputs on four threads, a quarter each. With -bench only the first 2^18 of each quarter,
  // so that the timing below does not wait; such a run says so and is no gate. A job ends at the first input that
  // differs, and the other threads' jobs soon after.
  const bool bench = (argc > 3 && std::wcscmp(argv[3], L"-bench") == 0);
  std::atomic<bool> input_differs = false;
  const auto inputs_equal = [&](const char* what, const auto& job) {
    constexpr uint64_t QUARTER = uint64_t{1} << 30;
    constexpr size_t THREADS = 4;
    const uint64_t inputs_each = (bench ? (QUARTER >> 12) : QUARTER);
    std::array<uint64_t, THREADS> bad = {};
    std::array<std::thread, THREADS> threads;
    input_differs = false;
    const auto start = std::chrono::steady_clock::now();
    for (size_t index = 0; index < THREADS; ++index) {
      threads[index] =
          std::thread([&, index] { job(QUARTER * index, (QUARTER * index) + inputs_each, index, &bad[index]); });
    }
    bool all_equal = true;
    for (size_t index = 0; index < THREADS; ++index) {
      threads[index].join();
      if (bad[index] != (QUARTER * index) + inputs_each) {
        all_equal = false;
        std::printf("  %s differs at input %#llx\n", what, bad[index]);
      }
    }
    const std::chrono::duration<double> took = std::chrono::steady_clock::now() - start;
    std::printf("  %s: %s inputs in %.0f s\n", what, (bench ? "2^20 (-bench: not the full run)" : "all 2^32"),
                took.count());
    return all_equal && !input_differs;
  };
  // Inputs [first, last) as the float of `lane` alone (ROW_WORDS: of every lane at once), the other lanes 1.0f:
  // rows of their own memory through the game's entry, 64 a call of the bench's runner, their 20 words against the
  // reference half's, and the way of each call's rows: the game's code for one of them exactly when the call holds
  // a denormal half. *bad: the input that differs (the call's first for the way), or `last`.
  // Ours: the other lanes hold 1.0f, one value for all of them; the lanes do not read each other.
  const auto lanes_differ = [&](size_t lane, uint64_t first, uint64_t last, uint64_t* bad) {
    constexpr size_t BATCH = 64;
    const size_t lanes_first = ((lane == ROW_WORDS) ? 0 : lane);
    const size_t lanes_end = ((lane == ROW_WORDS) ? ROW_WORDS : (lane + 1));
    std::vector<LightRow> rows(BATCH);
    std::array<RowPointers, BATCH> pointers;
    std::array<std::array<uint8_t*, ROW_WORDS>, BATCH> sources = {};
    for (size_t row = 0; row < BATCH; ++row) {
      for (size_t offset = 0; offset < rows[row].bytes.size(); offset += sizeof(FLOAT_ONE)) {
        std::memcpy(rows[row].bytes.data() + offset, &FLOAT_ONE, sizeof(FLOAT_ONE));
      }
      const auto memory = reinterpret_cast<uintptr_t>(rows[row].bytes.data());
      pointers[row] = {.rsp = memory + LIGHT_RSP, .rbx = memory + LIGHT_ROW, .r13 = memory + LIGHT_R13,
                       .r15 = memory + LIGHT_R15};
      for (size_t index = 0; index < ROW_WORDS; ++index) {
        sources[row][index] = lane_source(LANES[index], pointers[row].rsp, pointers[row].r13, pointers[row].r15);
      }
    }
    std::array<uint16_t, ROW_WORDS> expected;
    expected.fill(HALF_ONE);
    expected[ZERO_LANE] = 0;
    TlsSetValue(target_slot, reinterpret_cast<void*>(ROWS_REGION));
    TlsSetValue(exit_slot, reinterpret_cast<void*>(rows_runner_next));
    *bad = last;
    for (uint64_t input = first; input < last && !input_differs.load(std::memory_order_relaxed); input += BATCH) {
      const size_t count = static_cast<size_t>(std::min<uint64_t>(BATCH, last - input));
      bool denormal = false;
      for (size_t row = 0; row < count; ++row) {
        const auto x = static_cast<uint32_t>(input + row);
        denormal = denormal || ((x & 0x7fffffff) >= DENORMAL_HALF_FIRST && (x & 0x7fffffff) < NORMAL_HALF_FIRST);
        for (size_t index = lanes_first; index < lanes_end; ++index) {
          if (LANES[index].base == BASE_ZERO) continue;
          std::memcpy(sources[row][index], &x, sizeof(x));
        }
        std::memset(at(pointers[row].rbx + ROW_WORDS_OFFSET), 0xa5, 2 * ROW_WORDS);
      }
      TlsSetValue(runner_end_slot, pointers.data() + count);
      TlsSetValue(fallback_slot, nullptr);
      reinterpret_cast<RowsRunner>(rows_runner)(pointers.data());
      uint64_t differs_at = ((fallback_taken() == denormal) ? last : input);
      for (size_t row = 0; row < count && differs_at == last; ++row) {
        const auto x = static_cast<uint32_t>(input + row);
        const auto half = static_cast<uint16_t>(reinterpret_cast<HalfCall>(reference_half)(&x));
        for (size_t index = lanes_first; index < lanes_end; ++index) {
          expected[index] = ((index == ZERO_LANE) ? uint16_t{0} : half);
        }
        if (std::memcmp(at(pointers[row].rbx + ROW_WORDS_OFFSET), expected.data(), sizeof(expected)) != 0) {
          differs_at = input + row;
        }
      }
      if (differs_at == last) continue;
      *bad = differs_at;
      input_differs = true;
      return;
    }
  };
  // The half's stub. The DLL does not write it (game_code.hpp), so it is placed here as the DLL would place it: its
  // bytes on a cache line past the gap, the game's function behind them, and the site's jump through the plugin's own
  // ApplyJump. Then it is reached through the game's own entry, against the reference.
  std::printf("  the half's stub\n");
  const uintptr_t half_stub = stubs_at(
      reinterpret_cast<uintptr_t>(api.code_near_game(game_code::LEAD_GAP_BYTES + (game_code::BLOCK_ALIGNMENT - 1)
                                                     + game_code::HALF.size() + HALF_BYTES)),
      game_code::LEAD_GAP_BYTES);
  std::memcpy(at(half_stub), game_code::HALF.data(), game_code::HALF.size());
  std::memcpy(at(half_stub + game_code::HALF.size()), reference_half, HALF_BYTES);
  FlushInstructionCache(GetCurrentProcess(), at(half_stub), game_code::HALF.size() + HALF_BYTES);
  AppliedSite half_applied;
  const bool half_in = (ApplyJump(api, HALF_SITE, half_stub, &half_applied) == REFUSAL_NONE && half_applied.applied
                        && *at(HALF_SITE.address) == 0xe9 && HeldRangeUnchanged(HALF_RANGE, true)
                        && !HeldRangeUnchanged(HALF_RANGE, false));
  Expect(half_in, "the half's stub is in for its runs: the site's jump, the rest of its function the game's");
  if (!half_in) {
    std::printf("FAIL: %d checks, %d failure(s)\n", checks, failures);
    return failures;
  }
  // Inputs [first, last): each called (its return address on the test stack points at the capture) in the reference and
  // through the game's entry from one state, the other registers, the flags and the memory drawn anew every 4096
  // inputs. Equal: every general register (rsp past the return address), every flag, MXCSR, every xmm register; and
  // the memory as it was, [rdi] included, but for the thunk's own flags slot below rsp. *bad: the input that differs,
  // or `last`.
  const auto half_inputs_differ = [&](uint64_t first, uint64_t last, size_t thread, uint64_t* bad) {
    std::mt19937_64 generator(0x7a1f + thread);
    Arena memory;
    Arena expected;
    CpuState in;
    *bad = last;
    for (uint64_t input = first; input < last; ++input) {
      if (input == first || (input & 0xfff) == 0) {
        if (input_differs.load(std::memory_order_relaxed)) return;
        for (uint8_t& byte : memory.bytes) {
          byte = static_cast<uint8_t>(generator());
        }
        in = random_state_from(&generator);
        in.gpr[RSP] = reinterpret_cast<uintptr_t>(memory.bytes.data() + ARENA_RSP);
        in.gpr[RDI] = reinterpret_cast<uintptr_t>(memory.bytes.data() + ARENA_INPUT);
        std::memcpy(memory.bytes.data() + ARENA_RSP, &capture, sizeof(capture));
        std::memcpy(memory.bytes.data() + ARENA_RSP - 8, &in.flags, sizeof(in.flags));
        expected = memory;
      }
      const auto x = static_cast<uint32_t>(input);
      std::memcpy(memory.bytes.data() + ARENA_INPUT, &x, sizeof(x));
      std::memcpy(expected.bytes.data() + ARENA_INPUT, &x, sizeof(x));
      CpuState game;
      run(reinterpret_cast<uintptr_t>(reference_half), in, &game);
      const bool game_kept = (memory == expected);
      CpuState ours;
      run(HALF_ENTRY, in, &ours);
      if (game_kept && memory == expected && difference(game, ours, ~uint64_t{0}) == nullptr
          && ours.gpr[RSP] == in.gpr[RSP] + 8) {
        continue;
      }
      *bad = input;
      input_differs = true;
      return;
    }
  };
  // Known answers first, one per class, so that a stub that is not reached cannot pass: 1.0f, infinity for a NaN, the
  // smallest half's two neighbours, and a denormal half (the game's own code behind the stub). Each from a random
  // state: code that did nothing would leave a random rax.
  {
    const auto half_of = [&](uintptr_t target, uint32_t x) {
      arena = {};
      std::memcpy(arena.bytes.data() + ARENA_INPUT, &x, sizeof(x));
      std::memcpy(arena.bytes.data() + ARENA_RSP, &capture, sizeof(capture));
      CpuState in = random_state();
      in.gpr[RSP] = arena_at(ARENA_RSP);
      in.gpr[RDI] = arena_at(ARENA_INPUT);
      CpuState out;
      run(target, in, &out);
      return static_cast<uint16_t>(out.gpr[RAX]);
    };
    bool known = true;
    for (const auto& [x, half] : std::initializer_list<std::pair<uint32_t, uint16_t>>{
             {FLOAT_ONE, HALF_ONE}, {0xffc00001, 0xfc00}, {0x33000400, 0x0000}, {0x33000401, 0x0001},
             {0x38000000, 0x0200}}) {
      known = known && half_of(reinterpret_cast<uintptr_t>(reference_half), x) == half
              && half_of(HALF_ENTRY, x) == half;
    }
    Expect(known, "the half's stub and the reference: 1.0f, a NaN, the smallest half's edge and a denormal half");
  }
  if (failures == 0) {
    Expect(inputs_equal("the half", half_inputs_differ),
           (bench ? "the half's stub equals the game's function for 2^20 inputs (-bench)"
                  : "the half's stub equals the game's function for all 2^32 inputs: every register, the flags, MXCSR "
                    "and the memory"));
  }

  if (bench) {
    // The half as its callers use it (a float stored to a slot, the slot's address passed, the low word of the result
    // stored): the game's function (the reference copy) against the game's entry with our jump and stub, on three
    // input sets, each timed twice in turn (A/A). The number for the rule: the stub is written by default only when
    // it is at least 10 % faster a call.
    constexpr size_t SET = 4096;
    constexpr int PASSES = 5000;
    for (const char* set : {"normal", "tiny", "mixed"}) {
      std::array<float, SET> values = {};
      for (size_t index = 0; index < SET; ++index) {
        const bool tiny = (set[0] == 't' || (set[0] == 'm' && (random() % 4) == 0));
        values[index] = (tiny ? 0.f : (((random() % 2) == 0) ? position(random) : unit(random)));
      }
      std::array<uint16_t, SET> words = {};
      const auto time_calls = [&](HalfCall half) {
        // The pointer is read through a volatile so that neither run is specialised for its target.
        HalfCall volatile target = half;
        const auto start = std::chrono::steady_clock::now();
        for (int pass = 0; pass < PASSES; ++pass) {
          const HalfCall call = target;
          for (size_t index = 0; index < SET; ++index) {
            uint32_t slot = 0;
            std::memcpy(&slot, &values[index], sizeof(slot));
            words[index] = static_cast<uint16_t>(call(&slot));
          }
        }
        const std::chrono::duration<double, std::nano> took = std::chrono::steady_clock::now() - start;
        return took.count() / (double{PASSES} * SET);
      };
      // Nine timings of each in turn; the median of each, and of the nine gains, for both halves of the A/A.
      for (const char* pass : {"A", "A'"}) {
        constexpr size_t TIMINGS = 9;
        std::array<double, TIMINGS> game = {};
        std::array<double, TIMINGS> ours = {};
        std::array<double, TIMINGS> gain = {};
        for (size_t timing = 0; timing < TIMINGS; ++timing) {
          game[timing] = time_calls(reinterpret_cast<HalfCall>(reference_half));
          ours[timing] = time_calls(reinterpret_cast<HalfCall>(HALF_ENTRY));
          gain[timing] = 100.0 * (game[timing] - ours[timing]) / game[timing];
        }
        std::ranges::sort(game);
        std::ranges::sort(ours);
        std::ranges::sort(gain);
        std::printf("  bench %s: the half, %s inputs: the game's %.3f ns a call, the stub %.3f ns, %+.1f %% faster "
                    "(%+.1f .. %+.1f)\n",
                    pass, set, game[TIMINGS / 2], ours[TIMINGS / 2], gain[TIMINGS / 2], gain.front(), gain.back());
      }
    }
  }

  // The half's site out again, as the row's step takes a site out when a later one fails: a put-back the loader
  // refuses leaves our jump in and the site counted as out (RevertPatch's rule); the next one puts the game's bytes
  // back.
  failing_patch_calls = 1;
  const bool put_back_refused = !RevertPatch(api, HALF_SITE, &half_applied) && !half_applied.applied
                                && *at(HALF_SITE.address) == 0xe9;
  Expect(put_back_refused && RevertPatch(api, HALF_SITE, &half_applied) && holds_game_bytes(HALF_SITE)
             && HeldRangeUnchanged(HALF_RANGE, false),
         "the half's site out again: a refused put-back leaves the jump, the next one the game's whole function");

  // The sprite rows' long runs, after the half's shorter one.
  if (failures != 0) {
    std::printf("  the sprite rows' long runs are skipped: a check above failed\n");
  } else {
    Expect(inputs_equal("the sprite rows, every lane at once",
                        [&](uint64_t first, uint64_t last, size_t /*thread*/, uint64_t* bad) {
                          lanes_differ(ROW_WORDS, first, last, bad);
                        }),
           (bench ? "the sprite rows' stub gives the game's words for 2^20 values in every lane at once (-bench)"
                  : "the sprite rows' stub gives the game's words for all 2^32 values in every lane at once"));
    for (size_t lane = 0; lane < ROW_WORDS && failures == 0; ++lane) {
      if (LANES[lane].base == BASE_ZERO) continue;
      char what[64];
      std::snprintf(what, sizeof(what), "the sprite rows, lane %zu alone", lane);
      char check[128];
      std::snprintf(check, sizeof(check), "the sprite rows' stub gives the game's words for %s values in lane %zu alone",
                    (bench ? "2^20 (-bench)" : "all 2^32"), lane);
      Expect(inputs_equal(what, [&](uint64_t first, uint64_t last, size_t /*thread*/, uint64_t* bad) {
               lanes_differ(lane, first, last, bad);
             }),
             check);
    }
  }

  if (bench) {
    // The sprite rows, a row: the game's region (the reference copy, its calls to the copy of the half) against the
    // game's entry with our jump and stub, over 256 rows of their own memory through the runner, each timed nine
    // times in turn, twice (A/A). The number for the rule: the stub is written by default only when it is at least
    // 2 times as fast a row on realistic rows. The second set has a denormal half in every row, so the game's code
    // does each one after the stub's checks (and the test's mark).
    constexpr size_t ROWS = 256;
    constexpr int PASSES = 4000;
    for (const char* set : {"realistic", "denormal"}) {
      std::vector<LightRow> rows(ROWS);
      std::array<RowPointers, ROWS> pointers;
      size_t game_path = 0;
      TlsSetValue(exit_slot, reinterpret_cast<void*>(rows_runner_next));
      for (size_t row = 0; row < ROWS; ++row) {
        for (size_t offset = 0; offset < rows[row].bytes.size(); offset += sizeof(float)) {
          const float value = ((set[0] == 'd' && offset == LIGHT_RSP + 0x38) ? 1e-5f : realistic_float());
          std::memcpy(rows[row].bytes.data() + offset, &value, sizeof(value));
        }
        const auto memory = reinterpret_cast<uintptr_t>(rows[row].bytes.data());
        pointers[row] = {.rsp = memory + LIGHT_RSP, .rbx = memory + LIGHT_ROW, .r13 = memory + LIGHT_R13,
                         .r15 = memory + LIGHT_R15};
        TlsSetValue(target_slot, reinterpret_cast<void*>(ROWS_REGION));
        TlsSetValue(runner_end_slot, &pointers[row] + 1);
        TlsSetValue(fallback_slot, nullptr);
        reinterpret_cast<RowsRunner>(rows_runner)(&pointers[row]);
        game_path += (fallback_taken() ? 1 : 0);
      }
      TlsSetValue(runner_end_slot, pointers.data() + ROWS);
      const auto time_rows = [&](uintptr_t target) {
        TlsSetValue(target_slot, reinterpret_cast<void*>(target));
        const auto start = std::chrono::steady_clock::now();
        for (int pass = 0; pass < PASSES; ++pass) {
          reinterpret_cast<RowsRunner>(rows_runner)(pointers.data());
        }
        const std::chrono::duration<double, std::nano> took = std::chrono::steady_clock::now() - start;
        return took.count() / (double{PASSES} * ROWS);
      };
      for (const char* pass : {"A", "A'"}) {
        constexpr size_t TIMINGS = 9;
        std::array<double, TIMINGS> game = {};
        std::array<double, TIMINGS> ours = {};
        std::array<double, TIMINGS> times = {};
        for (size_t timing = 0; timing < TIMINGS; ++timing) {
          game[timing] = time_rows(reinterpret_cast<uintptr_t>(reference_rows));
          ours[timing] = time_rows(ROWS_REGION);
          times[timing] = game[timing] / ours[timing];
        }
        std::ranges::sort(game);
        std::ranges::sort(ours);
        std::ranges::sort(times);
        std::printf("  bench %s: the sprite rows, %s rows (%zu of %zu through the game's code): the game's %.2f ns a "
                    "row, the stub %.2f ns, %.2f times as fast (%.2f .. %.2f)\n",
                    pass, set, game_path, ROWS, game[TIMINGS / 2], ours[TIMINGS / 2], times[TIMINGS / 2], times.front(),
                    times.back());
      }
    }
  }

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
