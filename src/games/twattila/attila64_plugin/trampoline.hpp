/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>

#include "loader_api.h"
#include "sites.h"

// The plugin's own trampoline for a site inside a game function (sites.h SITE_MID), where the loader's entry hook
// cannot be used: that one enters through rax, and inside a function every register and flag is live.
//
// The shape is the usual one of the free hook libraries:
// - the site gets a 5-byte jmp rel32 to code within its reach, and the trampoline is the replaced instructions followed
//   by a jmp back (Microsoft Detours, MIT: detours.cpp detour_gen_jmp_immediate and the trampoline's rbCode; MinHook,
//   BSD 2-Clause: trampoline.c CreateTrampolineFunction);
// - the way out of rel32 reach is an indirect call through a 64-bit pointer beside the code (MinHook's CALL_ABS,
//   Detours' detour_gen_jmp_indirect);
// - the stub around the handler saves every register and the flags into a context, calls the handler with it, loads
//   them back and goes on through an address on the stack (SafetyHook, BSL-1.0: its MidHook stub).
// Ours, and why:
// - no decoder and no fix-ups: those libraries move RIP-relative operands and branches of the copied instructions
//   (Detours' DetourCopyInstruction, MinHook's HDE). Our sites are generated offline, and the generator refuses a site
//   with a RIP-relative operand, a branch, a branch target, a jump-table entry or a landing pad in its bytes, so the
//   copy is the bytes as they are.
// - the memory is the loader's (LoaderApi::code_near_game), not a search for a free region within 2 GB as theirs, and
//   the site's bytes are written by the loader (LoaderApi::patch): the plugin never writes game code itself.
// - a gate in front of the stub: while none of the handler's scale bits is set the game's own instructions run after
//   13 instructions that keep every register and flag, with no call. The site of the first user runs once per soldier
//   and frame; SafetyHook's stub always saves everything.
// - the bytes of the site after the jump are int3 (Detours' detour_gen_brk); nothing may run them.
// A limit shared with SafetyHook's stub: the way on is a ret to a pushed address, which user-mode CET shadow stacks
// refuse. The loader's exe is not built CET-compatible; a later one that is needs another way on.
namespace twattila::attila64_plugin::trampoline {

// The registers at the site, in memory order of the stub's frame. The handler may change any of them, and the game goes
// on with what it left: at `resume`, first the trampoline (the site's own instructions, then the game's next one).
// A handler that did the work of the site's instructions itself sets `resume` to the site's end.
struct MidContext {
  std::array<std::array<uint8_t, 16>, 16> xmm = {};
  uint64_t r15 = 0;
  uint64_t r14 = 0;
  uint64_t r13 = 0;
  uint64_t r12 = 0;
  uint64_t r11 = 0;
  uint64_t r10 = 0;
  uint64_t r9 = 0;
  uint64_t r8 = 0;
  uint64_t rdi = 0;
  uint64_t rsi = 0;
  uint64_t rbp = 0;
  uint64_t rbx = 0;
  uint64_t rdx = 0;
  uint64_t rcx = 0;
  uint64_t rax = 0;
  uint64_t rflags = 0;
  uint64_t resume = 0;
};
static_assert(offsetof(MidContext, r15) == 0x100 && offsetof(MidContext, rax) == 0x170
              && offsetof(MidContext, rflags) == 0x178 && offsetof(MidContext, resume) == 0x180
              && sizeof(MidContext) == 0x188,
              "the stub's pushes");
// The game's calling convention: the stub passes the context in rdi. noexcept: nothing can unwind through the stub.
using MidHandler = void(ATTILA64_GAME_CALL*)(MidContext* context) noexcept;

// The System V red zone below rsp, which the stub steps over before its first push.
inline constexpr uint8_t RED_ZONE = 0x80;
inline constexpr size_t MID_GATE_BYTES = 42;  // the trampoline's offset in the stub
inline constexpr size_t MID_STUB_FIXED_BYTES = 381;  // everything but the copied instructions
inline constexpr size_t MID_STUB_CAPACITY = MID_STUB_FIXED_BYTES + SITE_MAX_LENGTH;
inline constexpr size_t JUMP_BYTES = 5;  // jmp rel32

// Whether a jmp rel32 at `from` reaches `to`.
[[nodiscard]] constexpr bool InJumpReach(uintptr_t from, uintptr_t to) {
  const auto distance = static_cast<int64_t>(to - (from + JUMP_BYTES));
  return distance >= INT32_MIN && distance <= INT32_MAX;
}

// The stub of a mid-function site, built for the address `code` it will run at (the caller copies it there):
//   gate:        with no bit of `gate_mask` set in *gate, on to the trampoline, every register and flag kept
//   trampoline:  the site's instructions, jmp site + length
//   slow:        the registers into a MidContext, handler(context), the registers back, on to context.resume
// Returns the bytes used (MID_STUB_FIXED_BYTES + the site's). The site's end must be within jmp rel32 reach of the stub
// (InJumpReach). MXCSR and the x87 state are not saved (as SafetyHook's stub): a handler is compiled code of ours that
// leaves both alone.
inline size_t BuildMidStub(uintptr_t code, uintptr_t site_address, std::span<const uint8_t> site_bytes,
                           MidHandler handler, const std::atomic<uint32_t>* gate, uint32_t gate_mask,
                           std::array<uint8_t, MID_STUB_CAPACITY>* stub) {
  size_t size = 0;
  const auto emit = [&](std::initializer_list<uint8_t> bytes) {
    std::memcpy(stub->data() + size, bytes.begin(), bytes.size());
    size += bytes.size();
  };
  const auto emit_value = [&](uint64_t value, size_t bytes) {
    std::memcpy(stub->data() + size, &value, bytes);
    size += bytes;
  };
  // movups between xmm0-15 and [rsp + 16 x index]: `opcode` 0x11 stores, 0x10 loads.
  const auto emit_xmm_moves = [&](uint8_t opcode) {
    for (uint8_t index = 0; index < 16; ++index) {
      const auto reg_bits = static_cast<uint8_t>((index & 7) << 3);
      if (index < 8) {
        emit({0x0f, opcode, static_cast<uint8_t>(0x44 | reg_bits), 0x24, static_cast<uint8_t>(index * 16)});
      } else {
        emit({0x44, 0x0f, opcode, static_cast<uint8_t>(0x84 | reg_bits), 0x24});
        emit_value(index * 16u, 4);
      }
    }
  };
  // add al, 0x7f; sahf; pop rax: the flags lahf and seto al saved, and rax.
  const auto emit_flags_back = [&] { emit({0x04, 0x7f, 0x9e, 0x58}); };

  // The gate. lahf / seto keep the six status flags a test changes (popfq is much slower); lea, push, pop and mov
  // change none.
  emit({0x48, 0x8d, 0x64, 0x24, static_cast<uint8_t>(-RED_ZONE)});  // lea rsp, [rsp - RED_ZONE]
  emit({0x50, 0x9f, 0x0f, 0x90, 0xc0});                             // push rax; lahf; seto al
  emit({0x51, 0x48, 0xb9});                                         // push rcx; mov rcx, gate
  emit_value(reinterpret_cast<uintptr_t>(gate), 8);
  emit({0xf7, 0x01});  // test dword ptr [rcx], gate_mask
  emit_value(gate_mask, 4);
  emit({0x59, 0x75, static_cast<uint8_t>(4 + 8 + site_bytes.size() + JUMP_BYTES)});  // pop rcx; jnz slow
  emit_flags_back();
  emit({0x48, 0x8d, 0xa4, 0x24, RED_ZONE, 0x00, 0x00, 0x00});  // lea rsp, [rsp + RED_ZONE]

  // The trampoline, at MID_GATE_BYTES.
  std::memcpy(stub->data() + size, site_bytes.data(), site_bytes.size());
  size += site_bytes.size();
  emit({0xe9});  // jmp site + length
  emit_value(site_address + site_bytes.size() - (code + size + 4), 4);

  // slow: the context, pushed from its end. Its `resume` first: the trampoline, from the pointer after the code.
  emit_flags_back();
  const size_t resume_operand = size + 2;
  emit({0xff, 0x35, 0, 0, 0, 0});  // push [rip + resume]
  emit({0x9c, 0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57});  // pushfq; push rax..rdi
  emit({0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});  // r8..r15
  emit({0x48, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00});  // sub rsp, 0x100
  emit_xmm_moves(0x11);
  // The handler's argument; its stack aligned as a call needs it, whatever the site's rsp is. rbx is in the context
  // already, and the handler keeps it (callee-saved).
  emit({0x48, 0x89, 0xe7, 0x48, 0x89, 0xe3, 0x48, 0x83, 0xe4, 0xf0});  // mov rdi, rsp; mov rbx, rsp; and rsp, -16
  const size_t handler_operand = size + 2;
  emit({0xff, 0x15, 0, 0, 0, 0});  // call [rip + handler]
  emit({0x48, 0x89, 0xdc});        // mov rsp, rbx
  emit_xmm_moves(0x10);
  emit({0x48, 0x81, 0xc4, 0x00, 0x01, 0x00, 0x00});  // add rsp, 0x100
  emit({0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58});  // r15..r8
  emit({0x5f, 0x5e, 0x5d, 0x5b, 0x5a, 0x59, 0x58, 0x9d});  // pop rdi..rax; popfq
  emit({0xc2, RED_ZONE, 0x00});  // ret RED_ZONE: to context.resume, the game's rsp again

  const auto patch_operand = [&](size_t operand) {
    const auto displacement = static_cast<uint32_t>(size - (operand + 4));
    std::memcpy(stub->data() + operand, &displacement, sizeof(displacement));
  };
  patch_operand(resume_operand);
  emit_value(code + MID_GATE_BYTES, 8);
  patch_operand(handler_operand);
  emit_value(reinterpret_cast<uintptr_t>(handler), 8);
  return size;
}

}  // namespace twattila::attila64_plugin::trampoline
