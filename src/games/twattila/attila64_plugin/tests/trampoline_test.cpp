/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The plugin's own trampoline (trampoline.hpp) and its way into a site (patcher.hpp ApplyMidSite), without a game: test
// beds that load every register and the flags, run a site and store them all again (fake_loader.hpp WriteMidBed). Two
// sites: the sprite site's own 8 bytes, and a 13-byte function prologue that pushes, moves the stack pointer and
// stores (the longest kind a row may bring). Each at two stack alignments and with the status flags all clear and all
// set: the stub's layout, the gate (no call, the game's result, every register and flag as without the stub), the
// handler's context, a handler that changes nothing, one that changes a register, one that does the site's work itself
// and skips it, the refusals, and code memory mapped without write access.
// trampoline_test.exe; exit code = failures.

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

#include "../loader_api.h"
#include "../patcher.hpp"
#include "../sites.h"
#include "../trampoline.hpp"
#include "fake_loader.hpp"

namespace {

using namespace twattila::attila64_plugin;
using namespace twattila::attila64_plugin::test;

// mulss xmm0, [rax]; addss xmm0, [rcx]
inline constexpr std::array<uint8_t, 8> SPRITE_BYTES = {0xf3, 0x0f, 0x59, 0x00, 0xf3, 0x0f, 0x58, 0x01};
// push r12; mov rbp, rsp; sub rsp, 0x20; mov [rbp - 8], rdi
inline constexpr std::array<uint8_t, 13> PROLOGUE_BYTES = {0x41, 0x54, 0x48, 0x89, 0xe5, 0x48, 0x83,
                                                           0xec, 0x20, 0x48, 0x89, 0x7d, 0xf8};
inline constexpr uint32_t PROLOGUE_STACK = 0x28;
// The prologue's writes into the bed's record, and its stack bytes off again:
// mov rcx, [rbp - 8]; mov [rax + slot], rcx; add rsp, 0x20; pop qword ptr [rax + pushed]
inline constexpr std::array<uint8_t, 21> PROLOGUE_TAIL = {0x48, 0x8b, 0x4d, 0xf8, 0x48, 0x89, 0x88, 0x88, 0x00, 0x00, 0x00,
                                                          0x48, 0x83, 0xc4, 0x20, 0x8f, 0x80, 0x90, 0x00, 0x00, 0x00};
static_assert(offsetof(Registers, slot) == 0x88 && offsetof(Registers, pushed) == 0x90);
// An 8-byte nop: the sprite site's bed with nothing at the site.
inline constexpr std::array<uint8_t, 8> NOP_BYTES = {0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
inline constexpr uint64_t STATUS_FLAGS = 0x8d5;  // CF, PF, AF, ZF, SF, OF
inline constexpr uint64_t FLAGS_BASE = 0x202;    // the always-set bit and the interrupt flag
inline constexpr uint32_t GATE_MASK = 0x00c00000;
inline constexpr size_t RSP = 4;
inline constexpr size_t RBP = 5;

static_assert(trampoline::InJumpReach(0x1000, 0x80001004) && !trampoline::InJumpReach(0x1000, 0x80001005));
static_assert(trampoline::InJumpReach(0x90000000, 0x10000005) && !trampoline::InJumpReach(0x90000000, 0x10000004));

// What the test's handler is to do at its next calls, and what it saw.
struct HandlerPlan {
  int calls = 0;
  trampoline::MidContext seen = {};
  uint64_t resume = 0;  // not 0: where the game goes on
  bool set_xmm0 = false;
  float xmm0 = 0.f;
  bool set_r11 = false;
  uint64_t r11 = 0;
};
HandlerPlan plan;

ATTILA64_GAME_CALL void TestHandler(trampoline::MidContext* context) noexcept {
  ++plan.calls;
  plan.seen = *context;
  // Work of the kind a handler does, on the registers the stub must put back: floating point and a library call.
  char text[64];
  std::snprintf(text, sizeof(text), "%f %d", 1.5 * plan.calls, plan.calls);
  if (plan.set_xmm0) {
    std::memcpy(context->xmm[0].data(), &plan.xmm0, sizeof(plan.xmm0));
  }
  if (plan.set_r11) {
    context->r11 = plan.r11;
  }
  if (plan.resume != 0) {
    context->resume = plan.resume;
  }
}

uint8_t* far_code = nullptr;
void* FarCode(size_t /*bytes*/) noexcept { return far_code; }
void* NoCode(size_t /*bytes*/) noexcept { return nullptr; }

}  // namespace

int main() {
  char exe_dir[MAX_PATH];
  GetModuleFileNameA(nullptr, exe_dir, MAX_PATH);
  *std::strrchr(exe_dir, '\\') = '\0';
  const LoaderApi api = MakeApi(exe_dir, GAME_HI_MIN);

  // The stub's layout, built for a made-up address.
  {
    constexpr uintptr_t CODE = 0x5300100;
    constexpr uintptr_t SITE = 0xa5049c;
    std::atomic<uint32_t> gate = 0;
    std::array<uint8_t, trampoline::MID_STUB_CAPACITY> stub = {};
    const size_t size = trampoline::BuildMidStub(CODE, SITE, SPRITE_BYTES, &TestHandler, &gate, GATE_MASK, &stub);
    Expect(size == trampoline::MID_STUB_FIXED_BYTES + SPRITE_BYTES.size(), "stub: its size is the fixed part and the site's bytes");
    const uint8_t* trampoline_code = stub.data() + trampoline::MID_GATE_BYTES;
    int32_t back = 0;
    std::memcpy(&back, trampoline_code + SPRITE_BYTES.size() + 1, sizeof(back));
    Expect(std::memcmp(trampoline_code, SPRITE_BYTES.data(), SPRITE_BYTES.size()) == 0
               && trampoline_code[SPRITE_BYTES.size()] == 0xe9
               && CODE + trampoline::MID_GATE_BYTES + SPRITE_BYTES.size() + trampoline::JUMP_BYTES + back == SITE + SPRITE_BYTES.size(),
           "stub: the trampoline is the site's bytes and a jump to the site's end");
    uint64_t resume = 0;
    uint64_t handler = 0;
    uint64_t gate_address = 0;
    uint32_t mask = 0;
    std::memcpy(&resume, stub.data() + size - 16, sizeof(resume));
    std::memcpy(&handler, stub.data() + size - 8, sizeof(handler));
    std::memcpy(&gate_address, stub.data() + 13, sizeof(gate_address));
    std::memcpy(&mask, stub.data() + 23, sizeof(mask));
    Expect(resume == CODE + trampoline::MID_GATE_BYTES && handler == reinterpret_cast<uintptr_t>(&TestHandler)
               && gate_address == reinterpret_cast<uintptr_t>(&gate) && mask == GATE_MASK,
           "stub: the first resume address is the trampoline; the handler, the gate and its mask are in");
    // The gate's jnz lands on the slow path's first byte, right after the trampoline's jump.
    Expect(stub[28] == 0x75 && 30 + stub[29] == trampoline::MID_GATE_BYTES + SPRITE_BYTES.size() + trampoline::JUMP_BYTES,
           "stub: the gate's branch skips the trampoline");
  }

  // The beds' code: within jmp rel32 reach of the fake loader's code memory, as the game is.
  uint8_t* bed_code = nullptr;
  for (uintptr_t at = 0x20000000; bed_code == nullptr && at < 0x60000000; at += 0x10000) {
    bed_code = static_cast<uint8_t*>(
        VirtualAlloc(reinterpret_cast<void*>(at), 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
  }
  if (bed_code == nullptr) {
    std::printf("FAIL: no memory for the test beds\n");
    return 1;
  }
  struct Bed {
    MidBed run = nullptr;
    Site site = {};
    // The site sets rbp from rsp and its flags from an rsp subtraction: rbp is compared as its distance from rsp,
    // the status flags not at all (the stack is not at the same address at every call of the bed).
    bool rbp_from_rsp = false;
  };
  const auto make_bed = [&](std::span<const uint8_t> bytes, uint32_t reserve, uint32_t site_stack, std::span<const uint8_t> tail) {
    uintptr_t capture = 0;
    Bed bed = {.run = reinterpret_cast<MidBed>(bed_code)};
    bed.site.address = static_cast<uint32_t>(WriteMidBed(bed_code, bytes, 0, reserve, site_stack, tail, &capture));
    bed.site.length = static_cast<uint8_t>(bytes.size());
    bed.site.kind = SITE_MID;
    std::memcpy(bed.site.window.data() + SITE_MARGIN, bytes.data(), bytes.size());
    bed.rbp_from_rsp = (site_stack != 0);
    bed_code += MID_BED_CAPACITY + bytes.size() + tail.size();
    return bed;
  };
  float fade_width = 22.5f;
  float sprite_distance = 90.f;
  uint64_t readable = 0;
  const auto registers_in = [&](uint64_t status_flags) {
    Registers in;
    for (size_t number = 0; number < in.gpr.size(); ++number) {
      in.gpr[number] = 0x1111111111111111 * (number + 1);
    }
    in.gpr[0] = reinterpret_cast<uintptr_t>(&fade_width);       // rax
    in.gpr[1] = reinterpret_cast<uintptr_t>(&sprite_distance);  // rcx
    in.gpr[3] = reinterpret_cast<uintptr_t>(&readable);         // rbx
    in.gpr[RSP] = 0;
    in.flags = FLAGS_BASE | status_flags;
    for (size_t number = 0; number < in.xmm.size(); ++number) {
      in.xmm[number].fill(static_cast<uint8_t>(0xa0 + number));
    }
    const float dither = 0.25f;
    std::memcpy(in.xmm[0].data(), &dither, sizeof(dither));
    return in;
  };
  const auto run = [](const Bed& bed, const Registers& in) {
    Registers out;
    bed.run(&in, &out);
    if (bed.rbp_from_rsp) {
      out.gpr[RBP] -= out.gpr[RSP];
      out.flags &= ~STATUS_FLAGS;
      out.red_zone = {};  // below the site's own frame: old stack
    }
    out.gpr[RSP] = 0;
    return out;
  };
  const auto site_bytes = [](const Bed& bed) { return reinterpret_cast<const uint8_t*>(uintptr_t{bed.site.address}); };

  std::atomic<uint32_t> gate = 0;
  for (const bool prologue : {false, true}) {
    for (const uint32_t reserve : {0u, 8u}) {
      for (const uint64_t status_flags : {uint64_t{0}, STATUS_FLAGS}) {
        std::printf("  %s site, %u stack bytes more, status flags %#llx\n", (prologue ? "13-byte prologue" : "8-byte sprite"),
                    reserve, status_flags);
        const Bed bed = (prologue ? make_bed(PROLOGUE_BYTES, reserve, PROLOGUE_STACK, PROLOGUE_TAIL)
                                  : make_bed(SPRITE_BYTES, reserve, 0, {}));
        const Registers in = registers_in(status_flags);
        const Registers game = run(bed, in);
        float game_result = 0.f;
        std::memcpy(&game_result, game.xmm[0].data(), sizeof(game_result));
        Expect(prologue ? (game.slot == in.gpr[7] && game.pushed == in.gpr[12] && game.gpr[RBP] == 0x20)
                        : (game_result == (0.25f * 22.5f) + 90.f && (game.flags & STATUS_FLAGS) == status_flags
                           && game.red_zone == std::array{in.gpr[7], in.gpr[7]}),
               "bed: the site's own instructions do their work, the red zone holds what the bed wrote");

        AppliedSite applied;
        gate = 0;
        plan = {};
        Expect(ApplyMidSite(api, bed.site, &TestHandler, &gate, GATE_MASK, &applied) == REFUSAL_NONE && applied.applied,
               "mid site: applied");
        const auto* stub = static_cast<const uint8_t*>(applied.original) - trampoline::MID_GATE_BYTES;
        int32_t to_stub = 0;
        std::memcpy(&to_stub, site_bytes(bed) + 1, sizeof(to_stub));
        bool rest_int3 = true;
        for (size_t index = trampoline::JUMP_BYTES; index < bed.site.length; ++index) {
          rest_int3 = rest_int3 && site_bytes(bed)[index] == 0xcc;
        }
        Expect(site_bytes(bed)[0] == 0xe9 && site_bytes(bed) + trampoline::JUMP_BYTES + to_stub == stub && rest_int3
                   && SiteUnchanged(bed.site, applied),
               "mid site: a jump to the stub, int3 after it, the snapshot matches");
        Expect(std::memcmp(applied.original, bed.site.window.data() + SITE_MARGIN, bed.site.length) == 0,
               "mid site: the trampoline starts with the site's own bytes");

        Expect(run(bed, in) == game && plan.calls == 0, "gate closed: the game's result, every register and flag, no call");
        gate = ~GATE_MASK;
        Expect(run(bed, in) == game && plan.calls == 0, "gate: other bits do not open it");

        gate = 0x00400000;
        Expect(run(bed, in) == game && plan.calls == 1, "gate open, a handler that changes nothing: called once, the game's result");
        bool context_ok = (plan.seen.rflags & STATUS_FLAGS) == status_flags
                          && plan.seen.resume == reinterpret_cast<uintptr_t>(applied.original);
        const std::array<uint64_t, 16> seen = {plan.seen.rax, plan.seen.rcx, plan.seen.rdx, plan.seen.rbx, 0,
                                               plan.seen.rbp, plan.seen.rsi, plan.seen.rdi, plan.seen.r8, plan.seen.r9,
                                               plan.seen.r10, plan.seen.r11, plan.seen.r12, plan.seen.r13, plan.seen.r14,
                                               plan.seen.r15};
        context_ok = context_ok && seen == in.gpr && plan.seen.xmm == in.xmm;
        Expect(context_ok, "the handler's context: every register and the flags as at the site, resume at the trampoline");

        plan.set_r11 = true;
        plan.r11 = 0xfeedfacecafef00d;
        Registers changed = game;
        changed.gpr[11] = plan.r11;
        gate = GATE_MASK;
        Expect(run(bed, in) == changed && plan.calls == 2, "a register the handler changed reaches the game, nothing else differs");
        plan.set_r11 = false;

        if (!prologue) {
          // The handler does the site's work itself: its xmm0, and on at the site's end.
          const Bed empty = make_bed(NOP_BYTES, reserve, 0, {});
          Registers skipped = run(empty, in);
          plan.set_xmm0 = true;
          plan.xmm0 = 41.625f;
          plan.resume = uint64_t{bed.site.address} + bed.site.length;
          std::memcpy(skipped.xmm[0].data(), &plan.xmm0, sizeof(plan.xmm0));
          Expect(run(bed, in) == skipped && plan.calls == 3,
                 "a handler that skips the site: its result in the low float of xmm0, the site's instructions not run");
        }
        gate = 0;
        plan = {};
        Expect(run(bed, in) == game && plan.calls == 0, "gate closed again: the game's own instructions");
      }
    }
  }

  // Refusals: each leaves the site's bytes alone.
  {
    const Bed bed = make_bed(SPRITE_BYTES, 0, 0, {});
    const Registers in = registers_in(0);
    const Registers game = run(bed, in);
    const auto untouched = [&] {
      return std::memcmp(site_bytes(bed), SPRITE_BYTES.data(), SPRITE_BYTES.size()) == 0 && run(bed, in) == game;
    };
    AppliedSite applied;
    LoaderApi other = api;
    other.code_near_game = &NoCode;
    Expect(ApplyMidSite(other, bed.site, &TestHandler, &gate, GATE_MASK, &applied) == REFUSAL_NO_MEMORY && !applied.applied
               && untouched(),
           "refused: no code memory left (NO_MEMORY)");
    for (uintptr_t at = 0x700000000000; far_code == nullptr && at < 0x700010000000; at += 0x10000) {
      far_code = static_cast<uint8_t*>(
          VirtualAlloc(reinterpret_cast<void*>(at), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    }
    other.code_near_game = &FarCode;
    Expect(far_code != nullptr && ApplyMidSite(other, bed.site, &TestHandler, &gate, GATE_MASK, &applied) == REFUSAL_NO_MEMORY
               && !applied.applied && untouched() && far_code[0] == 0,
           "refused: code memory out of a jump's reach (NO_MEMORY), nothing written there");
    failing_patch_calls = 1;
    Expect(ApplyMidSite(api, bed.site, &TestHandler, &gate, GATE_MASK, &applied) == REFUSAL_WRITE_FAILED && !applied.applied
               && untouched(),
           "refused: the loader did not write the site (WRITE_FAILED)");

    // Code memory the loader mapped without write access: opened for the copy, closed again.
    auto* next_code = static_cast<uint8_t*>(FakeCodeNearGame(0));
    DWORD before = 0;
    VirtualProtect(next_code, trampoline::MID_STUB_CAPACITY, PAGE_EXECUTE_READ, &before);
    plan = {};
    gate = GATE_MASK;
    MEMORY_BASIC_INFORMATION page = {};
    Expect(ApplyMidSite(api, bed.site, &TestHandler, &gate, GATE_MASK, &applied) == REFUSAL_NONE
               && VirtualQuery(next_code, &page, sizeof(page)) != 0 && page.Protect == PAGE_EXECUTE_READ
               && static_cast<uint8_t*>(applied.original) == next_code + trampoline::MID_GATE_BYTES
               && run(bed, in) == game && plan.calls == 1,
           "code memory without write access: the stub is in and runs, the access as it was");
    VirtualProtect(next_code, trampoline::MID_STUB_CAPACITY, before, &before);
  }

  std::printf("%s: %d checks, %d failure(s)\n", ((failures == 0) ? "PASS" : "FAIL"), checks, failures);
  return failures;
}
