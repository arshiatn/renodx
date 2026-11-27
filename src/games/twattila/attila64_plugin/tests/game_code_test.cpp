/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The harness for the game code row's stubs (game_code.hpp, from the next steps on): maps the Linux build's load
// segments at their addresses, copies the game's float-to-half function (0x3009b90) and the sprite row's region of 20
// halves (0x32d9de5..0x32da007, its calls pointed at that copy) into code near the game as the references, and runs
// code through a thunk that loads every register, the flags and MXCSR from a state (rsp on a test stack) and captures
// them all where the code ends. This step's checks are the harness's own: a run of no code gives back its state, a
// state with one flag, MXCSR bit, register bit, xmm byte or memory byte changed is seen, each reference run twice gives
// the same state and memory (A/A), and each gives a known answer. A mapped image that is not the one the design read
// fails before any copied byte runs.
// game_code_test.exe <renodx_attila64.dll> <game ELF> [-bench]; SKIPPED (exit 77, reported by build.ps1 as not run)
// without the ELF or when the game's range is taken. -bench times the reference row twice (A/A), without checks.

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <random>

#include "../loader_api.h"
#include "fake_loader.hpp"

namespace {

using namespace twattila::attila64_plugin;
using namespace twattila::attila64_plugin::test;

inline constexpr uintptr_t HALF = 0x3009b90;
inline constexpr size_t HALF_BYTES = 0xb3;
inline constexpr uintptr_t ROWS_REGION = 0x32d9de5;
inline constexpr uintptr_t ROWS_REGION_END = 0x32da007;
inline constexpr size_t ROWS_CALLS = 20;
// FNV-1a 64 of the two held ranges as the design read them (pk5_game_code_64.md 4.2): 0x3009b90..0x3009c43 and
// 0x32d9d49..0x32da032.
inline constexpr uint64_t HALF_RANGE_HASH = 0xeb2efd4ede0b8a3a;
inline constexpr uint64_t ROWS_RANGE_HASH = 0x71aed05d902f839e;

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

  // The built DLL, for the literal check of the later steps: readable here.
  HANDLE dll = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  LARGE_INTEGER dll_size = {};
  Expect(dll != INVALID_HANDLE_VALUE && GetFileSizeEx(dll, &dll_size) && dll_size.QuadPart > 0,
         "the plugin DLL is readable");
  if (dll != INVALID_HANDLE_VALUE) {
    CloseHandle(dll);
  }

  const auto fnv64 = [&](uintptr_t lo, uintptr_t hi) {
    uint64_t hash = 0xcbf29ce484222325;
    for (uintptr_t address = lo; address < hi; ++address) {
      hash = (hash ^ *at(address)) * 0x100000001b3;
    }
    return hash;
  };
  // Another image's bytes are never run: the test fails here instead of crashing in them.
  const bool image_read =
      fnv64(HALF, HALF + HALF_BYTES) == HALF_RANGE_HASH && fnv64(0x32d9d49, 0x32da032) == ROWS_RANGE_HASH;
  Expect(image_read, "the mapped image holds the half and the sprite rows as the design read them");
  if (!image_read) {
    std::printf("FAIL: %d checks, %d failure(s)\n", checks, failures);
    return 1;
  }

  // The thunk, entered with rdi = in, rsi = out. Ours: the design asks for a register-exact runner of the game's own
  // bytes; it is the register-snapshot runner pattern of tools such as llvm-exegesis (load a whole state, run, store a
  // whole state), emitted here because the state includes rsp on a test stack and MXCSR. Deviation from the design's
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
  std::memcpy(reference_half, at(HALF), HALF_BYTES);
  std::memcpy(reference_rows, at(ROWS_REGION), ROWS_REGION_END - ROWS_REGION);
  size_t calls = 0;
  bool calls_to_half = true;
  for (size_t offset = 0; offset + 8 <= ROWS_REGION_END - ROWS_REGION; ++offset) {
    uint8_t* call = reference_rows + offset;
    if (call[0] != 0x48 || call[1] != 0x89 || call[2] != 0xef || call[3] != 0xe8) continue;
    int32_t displacement = 0;
    std::memcpy(&displacement, call + 4, sizeof(displacement));
    calls_to_half = calls_to_half && ROWS_REGION + offset + 8 + displacement == HALF;
    displacement =
        static_cast<int32_t>(reinterpret_cast<intptr_t>(reference_half) - reinterpret_cast<intptr_t>(call + 8));
    std::memcpy(call + 4, &displacement, sizeof(displacement));
    ++calls;
  }
  // Calls that are not the ones read would run elsewhere: fail before any copy runs.
  Expect(calls == ROWS_CALLS && calls_to_half, "the region: 20 calls of the half, each after a mov rdi, rbp");
  if (calls != ROWS_CALLS || !calls_to_half) {
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
  const auto random_state = [&] {
    CpuState state;
    for (uint64_t& value : state.gpr) {
      value = random();
    }
    state.flags = 0x202 | (random() & ARITHMETIC_FLAGS);
    for (auto& xmm : state.xmm) {
      for (uint8_t& byte : xmm) {
        byte = static_cast<uint8_t>(random());
      }
    }
    return state;
  };
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

  if (argc > 3 && std::wcscmp(argv[3], L"-bench") == 0) {
    // The reference row through the thunk, twice (A/A): what the later steps' stub numbers stand next to.
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
  }

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
