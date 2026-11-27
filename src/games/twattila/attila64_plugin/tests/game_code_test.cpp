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
// DLL's own init places the stubs and writes the sites, with each refusal, the value key, the install key, code memory
// without write access and the re-check at game start; the half's stub against the reference for all 2^32 inputs; and
// the row's word in the DLL file as a batch tool looks for it.
// game_code_test.exe <renodx_attila64.dll> <game ELF> [-bench]; SKIPPED (exit 77, reported by build.ps1 as not run)
// without the ELF or when the game's range is taken. -bench times instead of the full run (2^20 of the half's inputs
// only, no gate): the reference row twice (A/A), and the half against its stub as a caller calls it.

#include <windows.h>

#include <algorithm>
#include <array>
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

  // The thunk, entered with rdi = in, rsi = out. Ours: the design asks for a register-exact runner of the game's own
  // bytes (load a whole state, run, store a whole state), emitted here because the state includes rsp on a test stack
  // and MXCSR. Deviation from the design's
  // thunk: it saves only the System V callee-saved registers itself (rbx, rbp, r12-r15); rsi, rdi and xmm6-15, which
  // the host's MS ABI keeps, are saved by the compiler around the call because Thunk is sysv_abi. Three TLS slots (the
  // TEB's TlsSlots at gs:0x1480) carry what no register may: the out pointer, the code's address and rax while the
  // capture stores the rest; so threads may run it at once.
  const DWORD out_slot = TlsAlloc();
  const DWORD target_slot = TlsAlloc();
  const DWORD rax_slot = TlsAlloc();
  if (out_slot >= 64 || target_slot >= 64 || rax_slot >= 64) {
    std::printf("FAIL: no TLS slot below 64 for the thunk\n");
    return 1;
  }
  const auto tls_offset = [](DWORD slot) { return static_cast<uint32_t>(0x1480 + (8 * slot)); };
  auto* thunk_code = static_cast<uint8_t*>(api.code_near_game(0x300));
  uintptr_t capture = 0;
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
    emit({0x9c});                                                       // pushfq; pop qword [rax + flags]
    emit_access(false, {0x8f}, 0, RAX, offsetof(CpuState, flags));
    emit_tls({0x8b}, 1, rax_slot);                                      // mov rcx, gs:[rax slot]; mov [rax], rcx
    emit_access(true, {0x89}, 1, RAX, 8 * RAX);
    emit_access(false, {0x0f, 0xae}, 2, RAX, offsetof(CpuState, host_mxcsr));  // ldmxcsr [rax + host_mxcsr]
    emit({0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5d, 0x5b, 0xc3});   // pop r15-r12, rbp, rbx; ret
    Expect(code <= thunk_code + 0x300, "the thunk fits its code");
    FlushInstructionCache(GetCurrentProcess(), thunk_code, static_cast<size_t>(code - thunk_code));
  }
  // One run of the code at `target` from `in`; *out holds what the code left (rax..r15, flags, MXCSR, xmm).
  const auto run = [&](uintptr_t target, const CpuState& in, CpuState* out) {
    TlsSetValue(target_slot, reinterpret_cast<void*>(target));
    reinterpret_cast<Thunk>(thunk_code)(&in, out);
  };
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
    const auto back = static_cast<int32_t>(static_cast<intptr_t>(capture) - reinterpret_cast<intptr_t>(jump + 5));
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
  // The half's site jumps to `stubs`, int3 after the jump; there the stub's bytes and the game's function behind them.
  const auto half_is_in = [&](uintptr_t stubs) {
    std::array<uint8_t, 7> jump = {0xe9, 0, 0, 0, 0, 0xcc, 0xcc};
    const auto displacement = static_cast<uint32_t>(stubs - (HALF_SITE.address + 5));
    std::memcpy(jump.data() + 1, &displacement, sizeof(displacement));
    return std::memcmp(at(HALF_SITE.address), jump.data(), jump.size()) == 0
           && std::memcmp(at(stubs), game_code::HALF.data(), game_code::HALF.size()) == 0
           && std::memcmp(at(stubs + game_code::HALF.size()), reference_half, HALF_BYTES) == 0;
  };
  const size_t stubs_bytes = (game_code::BLOCK_ALIGNMENT - 1) + game_code::LAYOUT.bytes;
  const auto stubs_line = [](uintptr_t stubs) {
    char line[160];
    std::snprintf(line, sizeof(line), "renodx_attila64: game code stubs %#zx..%#zx (half 0x145 bytes at %#zx)", stubs,
                  stubs + game_code::LAYOUT.bytes, stubs);
    return std::string(line);
  };

  std::printf("  the row: in by default\n");
  {
    write_ini(true, true);
    const uintptr_t memory = next_code();
    const uintptr_t stubs = stubs_at(memory, game_code::LEAD_GAP_BYTES);
    const size_t used_before = near_code_used;
    const auto [plugin, result] = init_case(L"game_code_default", 0);
    Expect(result == 0 && rows_are(plugin, GAME_CODE, 0, GAME_CODE) && logged_line(site_line(HALF_SITE, "applied"))
               && logged_line(site_line(ROWS_SITE, "checked (check only)")) && logged_line(stubs_line(stubs))
               && Logged("sprites off, game code on"),
           "row: in, the half's site applied, the sprite rows' site checked, the stubs' line");
    Expect(half_is_in(stubs) && holds_game_bytes(ROWS_SITE)
               && near_code_used - used_before == game_code::LEAD_GAP_BYTES + stubs_bytes,
           "row: the half's stub and the game's function behind it on a cache line past the gap, its site's jump to "
           "it, the sprite rows' site the game's");
    if (plugin.game_started != nullptr) {
      plugin.game_started(&api);
      Expect(plugin.block->rows_changed.load() == 0 && Logged("game started: 1 sites unchanged")
                 && !Logged("changed after we patched it"),
             "game start: the written site and both held ranges are unchanged (our own jump is not a change)");
      // Each change after the init is seen at game start, in the range's site's words: a byte of the half's function
      // behind our jump, a byte of the sprite rows' range, that range's own site (not written: the whole range counts),
      // and our jump itself.
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
      Expect(changed_at_start(ROWS_RANGE.lo) && logged_line(changed_line(ROWS_SITE))
                 && !logged_line(changed_line(HALF_SITE)),
             "game start: a changed byte of the sprite rows' range is seen");
      Expect(changed_at_start(ROWS_SITE.address) && logged_line(changed_line(ROWS_SITE))
                 && !logged_line(changed_line(HALF_SITE)),
             "game start: a changed byte of the site that was only checked is seen");
      Expect(changed_at_start(HALF_SITE.address + 1) && logged_line(changed_line(HALF_SITE))
                 && !logged_line(changed_line(ROWS_SITE)) && Logged("game started: 0 sites unchanged"),
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
  Expect(refused_case(L"game_code_site_changed", HALF_SITE.address, "skipped (DIFFERS)", "skipped (HELD)"),
         "row: a changed byte of the half's site refuses it as its own, and the row");
  {
    write_ini(true, true);
    failing_patch_calls = 1;
    const auto [plugin, result] = init_case(L"game_code_patch_refused", 0);
    Expect(result == 0 && failing_patch_calls == 0 && rows_are(plugin, 0, GAME_CODE, GAME_CODE)
               && logged_line(site_line(HALF_SITE, "skipped (WRITE_FAILED)"))
               && logged_line(site_line(ROWS_SITE, "skipped (ROW)")) && !Logged("game code stubs")
               && holds_game_bytes(HALF_SITE) && holds_game_bytes(ROWS_SITE),
           "row: the half's jump refused by the loader refuses the row, the later site is not tried");
  }
  {
    write_ini(true, true);
    hook_mode = HOOK_NO_MEMORY;
    const auto [plugin, result] = init_case(L"game_code_no_memory", 0);
    hook_mode = HOOK_NORMAL;
    Expect(result == 0 && rows_are(plugin, 0, GAME_CODE, GAME_CODE)
               && logged_line(site_line(HALF_SITE, "skipped (NO_MEMORY)"))
               && logged_line(site_line(ROWS_SITE, "skipped (NO_MEMORY)")) && holds_game_bytes(HALF_SITE),
           "row: no code memory, with the gap or without, refuses the row and writes nothing");
  }
  {
    write_ini(true, true);
    const uintptr_t memory = next_code();
    const size_t used_before = near_code_used;
    failing_code_calls = 1;
    const auto [plugin, result] = init_case(L"game_code_no_gap", 0);
    Expect(result == 0 && failing_code_calls == 0 && rows_are(plugin, GAME_CODE, 0, GAME_CODE)
               && half_is_in(stubs_at(memory, 0)) && logged_line(stubs_line(stubs_at(memory, 0)))
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
      *at(HALF_RANGE.hi - 1) ^= 1;
      plugin.game_started(&api);
      *at(HALF_RANGE.hi - 1) ^= 1;
      Expect(plugin.block->rows_changed.load() == 0 && Logged("game started: 0 sites unchanged")
                 && !Logged("changed after we patched it"),
             "game start: with nothing written the held ranges are not looked at again");
    }
  }
  {
    write_ini(false, true);
    const auto [plugin, result] = init_case(L"game_code_key_0", 0);
    Expect(result == 0 && rows_are(plugin, 0, GAME_CODE, 0) && logged_line(site_line(HALF_SITE, "skipped (OFF)"))
               && logged_line(site_line(ROWS_SITE, "skipped (OFF)")) && Logged("sprites off, game code off")
               && holds_game_bytes(HALF_SITE),
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
    Expect(result == 0 && rows_are(plugin, GAME_CODE, 0, GAME_CODE) && half_is_in(stubs)
               && VirtualQuery(at(stubs), &page, sizeof(page)) != 0 && page.Protect == PAGE_EXECUTE_READ
               && !Logged("stays writable"),
           "row: code memory without write access: the stubs are in, the access as it was");
    VirtualProtect(at(memory), game_code::LEAD_GAP_BYTES + stubs_bytes, before, &before);
  }

  // The half's stub, as the DLL's init placed it and reached through the game's own entry, against the reference.
  write_ini(true, true);
  const auto [half_plugin, half_result] = init_case(L"game_code_half", 0);
  DeleteFileW(ini_path.c_str());
  const bool half_in = (half_result == 0 && rows_are(half_plugin, GAME_CODE, 0, GAME_CODE)
                        && *at(HALF_SITE.address) == 0xe9);
  Expect(half_in, "the half's stub is in for its runs");
  if (!half_in) {
    std::printf("FAIL: %d checks, %d failure(s)\n", checks, failures);
    return failures;
  }
  // Inputs [first, last): each called (its return address on the test stack points at the capture) in the reference and
  // through the game's entry from one state, the other registers, the flags and the memory drawn anew every 4096
  // inputs. Equal: every general register (rsp past the return address), every flag, MXCSR, every xmm register; and
  // the memory as it was, [rdi] included, but for the thunk's own flags slot below rsp. *first_bad: the lowest input
  // that differs, or `last`.
  const auto half_inputs_differ = [&](uint64_t first, uint64_t last, uint64_t seed, uint64_t* first_bad) {
    std::mt19937_64 generator(seed);
    Arena memory;
    Arena expected;
    CpuState in;
    *first_bad = last;
    for (uint64_t input = first; input < last; ++input) {
      if (input == first || (input & 0xfff) == 0) {
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
      *first_bad = std::min(*first_bad, input);
    }
  };
  // Known answers through the stub first, one per class, so a stub that is not reached cannot pass: 1.0f, infinity
  // for a NaN, the smallest half's two neighbours, and a denormal half (the game's own code behind the stub).
  {
    bool known = true;
    for (const auto& [x, half] : std::initializer_list<std::pair<uint32_t, uint16_t>>{
             {FLOAT_ONE, HALF_ONE}, {0xffc00001, 0xfc00}, {0x33000400, 0x0000}, {0x33000401, 0x0001}, {0x38000000, 0x0200}}) {
      known = known && static_cast<uint16_t>(reinterpret_cast<HalfCall>(HALF_ENTRY)(&x)) == half
              && static_cast<uint16_t>(reinterpret_cast<HalfCall>(reference_half)(&x)) == half;
    }
    Expect(known, "the half's stub and the reference: 1.0f, a NaN, the smallest half's edge and a denormal half");
  }
  const bool bench = (argc > 3 && std::wcscmp(argv[3], L"-bench") == 0);
  {
    // All 2^32 inputs on four threads. With -bench only the first 2^18 of each thread's quarter, so that the timing
    // below does not wait for the full run; that run says so and is no gate.
    constexpr uint64_t QUARTER = uint64_t{1} << 30;
    constexpr size_t THREADS = 4;
    const uint64_t inputs_each = (bench ? (QUARTER >> 12) : QUARTER);
    std::array<uint64_t, THREADS> first_bad = {};
    std::array<std::thread, THREADS> threads;
    const auto start = std::chrono::steady_clock::now();
    for (size_t index = 0; index < THREADS; ++index) {
      threads[index] = std::thread(half_inputs_differ, QUARTER * index, (QUARTER * index) + inputs_each, 0x7a1f + index,
                                   &first_bad[index]);
    }
    bool all_equal = true;
    for (size_t index = 0; index < THREADS; ++index) {
      threads[index].join();
      if (first_bad[index] != (QUARTER * index) + inputs_each) {
        all_equal = false;
        std::printf("  the half differs at input %#llx\n", first_bad[index]);
      }
    }
    const std::chrono::duration<double> took = std::chrono::steady_clock::now() - start;
    std::printf("  the half: %s inputs in %.0f s\n", (bench ? "2^20 (-bench: not the full run)" : "all 2^32"),
                took.count());
    Expect(all_equal, (bench ? "the half's stub equals the game's function for 2^20 inputs (-bench)"
                             : "the half's stub equals the game's function for all 2^32 inputs: every register, the "
                               "flags, MXCSR and the memory"));
  }

  if (bench) {
    // The reference row through the thunk, twice (A/A): what the next step's stub number stands next to.
    const CpuState in = row_state();
    const Arena before = arena;
    for (const char* pass : {"A", "A'"}) {
      constexpr int ROWS = 1000000;
      CpuState out;
      const auto start = std::chrono::steady_clock::now();
      for (int row = 0; row < ROWS; ++row) {
        arena = before;
        run(reinterpret_cast<uintptr_t>(reference_rows), in, &out);
      }
      const std::chrono::duration<double, std::nano> took = std::chrono::steady_clock::now() - start;
      std::printf("  bench %s: the reference row %.1f ns (thunk and arena copy included)\n", pass, took.count() / ROWS);
    }
    // The half as its callers use it (a float stored to a slot, the slot's address passed, the low word of the result
    // stored): the game's function (the reference copy) against the game's entry with our jump and stub, on three
    // input sets, each timed twice in turn (A/A). The number for the design's rule: the stub is written by default
    // only when it is at least 10 % faster a call.
    constexpr size_t SET = 4096;
    constexpr int PASSES = 5000;
    std::uniform_real_distribution<float> position(-1000.f, 1000.f);
    std::uniform_real_distribution<float> unit(0.f, 1.f);
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

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
