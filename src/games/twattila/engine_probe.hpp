/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

// The probe build of engine_code.hpp (included only with TWATTILA_ENGINE_PROBE, from the middle of engine_code.hpp's internal namespace; a private build for
// the bench, never a release, no row). Each site of the effect and render list code that a prefetch or a faster loop works on gets a probe stub: the site's jump
// goes to it first, it counts the histograms the plan's decisions need (the item push runs, the ready check's children and its first child not ready, the
// pending list sweep's length, the particles of an emitter, the children of an effect, the particle writer's table entry deltas), keeps every register and the
// flags (pushfd, the registers it spends) and goes on with `jmp` to the site's production stub, which runs exactly as it always does: the production stubs are
// untouched (the probe build's only change to them is the address their site's jump goes to). The counters live in a page of their own and are bumped with
// locked increments (the effect loops run on the job threads too). They are dumped to the log once, N presents after the first site was written (N from the
// ReShade.ini key the addon reads and hands to Arm(); 0 or no key: never) and the jump bytes of every site are logged each time the sites change. The stubs'
// source is test/tw_engine_code/stubs/probes.s (the counters' field sentinels F_* there are the indices of `Counters` below, in order). The probes only read what
// the game's own code of that site reads in the game (live objects: the effect, emitter and list objects, the first 16 children's ready bytes: the ready check
// probe reads them also while [this + 4] is clear, where the game's check would not; no game list holds a child without its object), the cost is a few locked
// increments a call (a probe build's timing is not a release's), and none of it is in a release: no row, no setting, no counter in the production stubs.

namespace probe {

constexpr size_t FIELDS = 20;  // STUB_FIELD_SENTINEL + 0 .. 19
constexpr size_t SLOT = 0x50;  // the code page's room for a probe stub (the largest is 76 bytes)
constexpr size_t STACK_USED = 20;  // the stack below esp a probe stub writes (the ready check's: five pushes); dead after its jump on, but not the game's own bytes there

struct Counters {
  uint32_t evec_run[2][17];     // 0, 1: the remaining items at stub A's / B's entry (bucket 0: none, n: 2^(n-1) .. 2^n - 1, 16: 32768 or more)
  uint32_t evec_calls[2];       // 2, 3: the entries with edx != 0 (the per-item call path: the game's code)
  uint32_t ready_children[18];  // 4: the ready check's children (0 .. 15, 16: 16 or more)
  uint32_t ready_first[18];     // 5: the first child not ready (0 .. 15), 17: none of the first 16
  uint32_t sweep_iterations;    // 6
  uint32_t sweep_len[32];       // 7: log2 of the pending list count at a sweep's start
  uint32_t sweep_last;          // 8
  uint32_t particle_exec;       // 9: the particle loop's bottoms
  uint32_t particle_taken;      // 10: of them with the slot 2 places on inside the size (PARTICLE_PREFETCH_AHEAD)
  uint32_t particle_size[32];   // 11: the particles of an emitter at the loop's exit (31: 31 or more)
  uint32_t sum_exec;            // 12: the child sum loop's bottoms
  uint32_t sum_children[32];    // 13: the children of a call at the loop's exit
  uint32_t entry_calls;         // 14: the particle writer's table entry lookups
  uint32_t entry_null;          // 15
  uint32_t entry_one;           // 16: the entry 24 bytes after the previous one's
  uint32_t entry_zero;          // 17: the same entry
  uint32_t entry_other;         // 18
  uint32_t entry_prev;          // 19
};
static_assert(sizeof(Counters) <= 0x1000);

inline uint8_t* code = nullptr;  // the probe stubs (RWX: private build)
inline Counters* counters = nullptr;
inline uint32_t installed = 0;                      // the sites that have a probe stub in front of their production stub
inline std::array<uint8_t*, 32> stub_of = {};       // by site index
inline std::array<size_t, 32> size_of = {};
inline uint32_t dump_after = 0;                     // the presents after the first write at which the histograms are dumped (0: never)
inline uint32_t presents_on = 0;

struct Probe {
  size_t site;  // the index in `sites`
  const uint8_t* code;
  size_t size;
};

// The stubs' bytes (probes.s through probe_arrays.py: pushfd, the registers it spends, locked increments, the registers back, popfd, `jmp` the production stub).
constexpr std::array<uint8_t, 56> PROBE_EVEC_A = {
    0x9C,                                          // +0x00 pushfd
    0x50,                                          // +0x01 push eax
    0x51,                                          // +0x02 push ecx
    0x31, 0xC9,                                    // +0x03 xor ecx, ecx
    0x8B, 0x44, 0x24, 0x40,                        // +0x05 mov eax, dword ptr [esp + 0x40]
    0x39, 0xC5,                                    // +0x09 cmp ebp, eax
    0x73, 0x10,                                    // +0x0B jae a_count
    0x29, 0xE8,                                    // +0x0D sub eax, ebp
    0x0F, 0xBD, 0xC8,                              // +0x0F bsr ecx, eax
    0x41,                                          // +0x12 inc ecx
    0x83, 0xF9, 0x10,                              // +0x13 cmp ecx, 0x10
    0x76, 0x05,                                    // +0x16 jbe a_count
    0xB9, 0x10, 0x00, 0x00, 0x00,                  // +0x18 mov ecx, 0x10
    // a_count:
    0xF0, 0xFF, 0x04, 0x8D, 0x01, 0x5A, 0x5A, 0x5A, // +0x1D lock inc dword ptr [ecx*4 + F_EVEC_A_RUN]
    0x85, 0xD2,                                    // +0x25 test edx, edx
    0x74, 0x07,                                    // +0x27 je a_done
    0xF0, 0xFF, 0x05, 0x03, 0x5A, 0x5A, 0x5A,      // +0x29 lock inc dword ptr [F_EVEC_CALLS_A]
    // a_done:
    0x59,                                          // +0x30 pop ecx
    0x58,                                          // +0x31 pop eax
    0x9D,                                          // +0x32 popfd
    0xE9, 0x00, 0x00, 0x00, 0x00,                  // +0x33 jmp 0x38
};
constexpr std::array<uint8_t, 54> PROBE_EVEC_B = {
    0x9C,                                          // +0x00 pushfd
    0x50,                                          // +0x01 push eax
    0x51,                                          // +0x02 push ecx
    0x31, 0xC9,                                    // +0x03 xor ecx, ecx
    0x39, 0xDD,                                    // +0x05 cmp ebp, ebx
    0x73, 0x12,                                    // +0x07 jae b_count
    0x89, 0xD8,                                    // +0x09 mov eax, ebx
    0x29, 0xE8,                                    // +0x0B sub eax, ebp
    0x0F, 0xBD, 0xC8,                              // +0x0D bsr ecx, eax
    0x41,                                          // +0x10 inc ecx
    0x83, 0xF9, 0x10,                              // +0x11 cmp ecx, 0x10
    0x76, 0x05,                                    // +0x14 jbe b_count
    0xB9, 0x10, 0x00, 0x00, 0x00,                  // +0x16 mov ecx, 0x10
    // b_count:
    0xF0, 0xFF, 0x04, 0x8D, 0x02, 0x5A, 0x5A, 0x5A, // +0x1B lock inc dword ptr [ecx*4 + F_EVEC_B_RUN]
    0x85, 0xD2,                                    // +0x23 test edx, edx
    0x74, 0x07,                                    // +0x25 je b_done
    0xF0, 0xFF, 0x05, 0x04, 0x5A, 0x5A, 0x5A,      // +0x27 lock inc dword ptr [F_EVEC_CALLS_B]
    // b_done:
    0x59,                                          // +0x2E pop ecx
    0x58,                                          // +0x2F pop eax
    0x9D,                                          // +0x30 popfd
    0xE9, 0x00, 0x00, 0x00, 0x00,                  // +0x31 jmp 0x36
};
constexpr std::array<uint8_t, 70> PROBE_READY = {
    0x9C,                                          // +0x00 pushfd
    0x50,                                          // +0x01 push eax
    0x52,                                          // +0x02 push edx
    0x56,                                          // +0x03 push esi
    0x51,                                          // +0x04 push ecx
    0x8B, 0x41, 0x40,                              // +0x05 mov eax, dword ptr [ecx + 0x40]
    0xBA, 0x10, 0x00, 0x00, 0x00,                  // +0x08 mov edx, 0x10
    0x39, 0xD0,                                    // +0x0D cmp eax, edx
    0x0F, 0x47, 0xC2,                              // +0x0F cmova eax, edx
    0xF0, 0xFF, 0x04, 0x85, 0x05, 0x5A, 0x5A, 0x5A, // +0x12 lock inc dword ptr [eax*4 + F_READY_CHILDREN]
    0x8B, 0x71, 0x44,                              // +0x1A mov esi, dword ptr [ecx + 0x44]
    0x31, 0xD2,                                    // +0x1D xor edx, edx
    // r_next:
    0x39, 0xC2,                                    // +0x1F cmp edx, eax
    0x73, 0x0C,                                    // +0x21 jae r_none
    0x8B, 0x0C, 0xD6,                              // +0x23 mov ecx, dword ptr [esi + edx*8]
    0x80, 0x79, 0x6E, 0x00,                        // +0x26 cmp byte ptr [ecx + 0x6e], 0
    0x74, 0x08,                                    // +0x2A je r_found
    0x42,                                          // +0x2C inc edx
    0xEB, 0xF0,                                    // +0x2D jmp r_next
    // r_none:
    0xBA, 0x11, 0x00, 0x00, 0x00,                  // +0x2F mov edx, 0x11
    // r_found:
    0xF0, 0xFF, 0x04, 0x95, 0x06, 0x5A, 0x5A, 0x5A, // +0x34 lock inc dword ptr [edx*4 + F_READY_FIRST]
    0x59,                                          // +0x3C pop ecx
    0x5E,                                          // +0x3D pop esi
    0x5A,                                          // +0x3E pop edx
    0x58,                                          // +0x3F pop eax
    0x9D,                                          // +0x40 popfd
    0xE9, 0x00, 0x00, 0x00, 0x00,                  // +0x41 jmp 0x46
};
constexpr std::array<uint8_t, 49> PROBE_SWEEP = {
    0x9C,                                          // +0x00 pushfd
    0x50,                                          // +0x01 push eax
    0x51,                                          // +0x02 push ecx
    0xF0, 0xFF, 0x05, 0x07, 0x5A, 0x5A, 0x5A,      // +0x03 lock inc dword ptr [F_SWEEP_ITER]
    0x3B, 0x1D, 0x09, 0x5A, 0x5A, 0x5A,            // +0x0A cmp ebx, dword ptr [F_SWEEP_LAST]
    0x72, 0x11,                                    // +0x10 jb s_same
    0x8B, 0x46, 0x18,                              // +0x12 mov eax, dword ptr [esi + 0x18]
    0x83, 0xC8, 0x01,                              // +0x15 or eax, 1
    0x0F, 0xBD, 0xC8,                              // +0x18 bsr ecx, eax
    0xF0, 0xFF, 0x04, 0x8D, 0x08, 0x5A, 0x5A, 0x5A, // +0x1B lock inc dword ptr [ecx*4 + F_SWEEP_LEN]
    // s_same:
    0x89, 0x1D, 0x09, 0x5A, 0x5A, 0x5A,            // +0x23 mov dword ptr [F_SWEEP_LAST], ebx
    0x59,                                          // +0x29 pop ecx
    0x58,                                          // +0x2A pop eax
    0x9D,                                          // +0x2B popfd
    0xE9, 0x00, 0x00, 0x00, 0x00,                  // +0x2C jmp 0x31
};
constexpr std::array<uint8_t, 59> PROBE_PARTICLE = {
    0x9C,                                          // +0x00 pushfd
    0x50,                                          // +0x01 push eax
    0x51,                                          // +0x02 push ecx
    0xF0, 0xFF, 0x05, 0x0A, 0x5A, 0x5A, 0x5A,      // +0x03 lock inc dword ptr [F_PARTICLE_EXEC]
    0x8B, 0x47, 0x64,                              // +0x0A mov eax, dword ptr [edi + 0x64]
    0x39, 0xC3,                                    // +0x0D cmp ebx, eax
    0x72, 0x14,                                    // +0x0F jb p_inside
    0xB9, 0x1F, 0x00, 0x00, 0x00,                  // +0x11 mov ecx, 0x1f
    0x39, 0xC8,                                    // +0x16 cmp eax, ecx
    0x0F, 0x47, 0xC1,                              // +0x18 cmova eax, ecx
    0xF0, 0xFF, 0x04, 0x85, 0x0C, 0x5A, 0x5A, 0x5A, // +0x1B lock inc dword ptr [eax*4 + F_PARTICLE_SIZE]
    0xEB, 0x0E,                                    // +0x23 jmp p_done
    // p_inside:
    0x8D, 0x4B, 0x02,                              // +0x25 lea ecx, [ebx + 2]
    0x39, 0xC1,                                    // +0x28 cmp ecx, eax
    0x73, 0x07,                                    // +0x2A jae p_done
    0xF0, 0xFF, 0x05, 0x0B, 0x5A, 0x5A, 0x5A,      // +0x2C lock inc dword ptr [F_PARTICLE_TAKEN]
    // p_done:
    0x59,                                          // +0x33 pop ecx
    0x58,                                          // +0x34 pop eax
    0x9D,                                          // +0x35 popfd
    0xE9, 0x00, 0x00, 0x00, 0x00,                  // +0x36 jmp 0x3b
};
constexpr std::array<uint8_t, 43> PROBE_SUM = {
    0x9C,                                          // +0x00 pushfd
    0x50,                                          // +0x01 push eax
    0xF0, 0xFF, 0x05, 0x0D, 0x5A, 0x5A, 0x5A,      // +0x02 lock inc dword ptr [F_SUM_EXEC]
    0x8D, 0x04, 0x91,                              // +0x09 lea eax, [ecx + edx*4]
    0x39, 0xC6,                                    // +0x0C cmp esi, eax
    0x72, 0x14,                                    // +0x0E jb m_done
    0x83, 0xFA, 0x1F,                              // +0x10 cmp edx, 0x1f
    0x89, 0xD0,                                    // +0x13 mov eax, edx
    0x76, 0x05,                                    // +0x15 jbe m_count
    0xB8, 0x1F, 0x00, 0x00, 0x00,                  // +0x17 mov eax, 0x1f
    // m_count:
    0xF0, 0xFF, 0x04, 0x85, 0x0E, 0x5A, 0x5A, 0x5A, // +0x1C lock inc dword ptr [eax*4 + F_SUM_CHILDREN]
    // m_done:
    0x58,                                          // +0x24 pop eax
    0x9D,                                          // +0x25 popfd
    0xE9, 0x00, 0x00, 0x00, 0x00,                  // +0x26 jmp 0x2b
};
constexpr std::array<uint8_t, 76> PROBE_ENTRY = {
    0x9C,                                          // +0x00 pushfd
    0x51,                                          // +0x01 push ecx
    0xF0, 0xFF, 0x05, 0x0F, 0x5A, 0x5A, 0x5A,      // +0x02 lock inc dword ptr [F_ENTRY_CALLS]
    0x85, 0xC0,                                    // +0x09 test eax, eax
    0x75, 0x09,                                    // +0x0B jne e_nonnull
    0xF0, 0xFF, 0x05, 0x10, 0x5A, 0x5A, 0x5A,      // +0x0D lock inc dword ptr [F_ENTRY_NULL]
    0xEB, 0x2F,                                    // +0x14 jmp e_done
    // e_nonnull:
    0x89, 0xC1,                                    // +0x16 mov ecx, eax
    0x2B, 0x0D, 0x14, 0x5A, 0x5A, 0x5A,            // +0x18 sub ecx, dword ptr [F_ENTRY_PREV]
    0xA3, 0x14, 0x5A, 0x5A, 0x5A,                  // +0x1E mov dword ptr [F_ENTRY_PREV], eax
    0x83, 0xF9, 0x18,                              // +0x23 cmp ecx, 0x18
    0x74, 0x0D,                                    // +0x26 je e_one
    0x85, 0xC9,                                    // +0x28 test ecx, ecx
    0x74, 0x12,                                    // +0x2A je e_zero
    0xF0, 0xFF, 0x05, 0x13, 0x5A, 0x5A, 0x5A,      // +0x2C lock inc dword ptr [F_ENTRY_OTHER]
    0xEB, 0x10,                                    // +0x33 jmp e_done
    // e_one:
    0xF0, 0xFF, 0x05, 0x11, 0x5A, 0x5A, 0x5A,      // +0x35 lock inc dword ptr [F_ENTRY_ONE]
    0xEB, 0x07,                                    // +0x3C jmp e_done
    // e_zero:
    0xF0, 0xFF, 0x05, 0x12, 0x5A, 0x5A, 0x5A,      // +0x3E lock inc dword ptr [F_ENTRY_ZERO]
    // e_done:
    0x59,                                          // +0x45 pop ecx
    0x9D,                                          // +0x46 popfd
    0xE9, 0x00, 0x00, 0x00, 0x00,                  // +0x47 jmp 0x4c
};

// The sites that have a probe, in the order of their slots.
inline const std::array<Probe, 7> PROBES = {{{ITEM_PUSH_FIRST_SITE, PROBE_EVEC_A.data(), PROBE_EVEC_A.size()},
                                              {ITEM_PUSH_FIRST_SITE + 1, PROBE_EVEC_B.data(), PROBE_EVEC_B.size()},
                                              {4, PROBE_READY.data(), PROBE_READY.size()},
                                              {SWEEP_PREFETCH_INDEX, PROBE_SWEEP.data(), PROBE_SWEEP.size()},
                                              {PARTICLE_PREFETCH_INDEX, PROBE_PARTICLE.data(), PROBE_PARTICLE.size()},
                                              {CHILD_SUM_INDEX, PROBE_SUM.data(), PROBE_SUM.size()},
                                              {ENTRY_PREFETCH_INDEX, PROBE_ENTRY.data(), PROBE_ENTRY.size()}}};

// The counters' addresses in the sentinels' order.
inline std::array<const uint8_t*, FIELDS> Fields() {
  const auto at = [](const void* pointer) { return static_cast<const uint8_t*>(pointer); };
  return {at(counters->evec_run[0]), at(counters->evec_run[1]), at(&counters->evec_calls[0]), at(&counters->evec_calls[1]), at(counters->ready_children),
          at(counters->ready_first), at(&counters->sweep_iterations), at(counters->sweep_len), at(&counters->sweep_last), at(&counters->particle_exec),
          at(&counters->particle_taken), at(counters->particle_size), at(&counters->sum_exec), at(counters->sum_children), at(&counters->entry_calls),
          at(&counters->entry_null), at(&counters->entry_one), at(&counters->entry_zero), at(&counters->entry_other), at(&counters->entry_prev)};
}

// The two pages (once, in Prepare); false if either is missing (then no site gets a probe).
inline bool Allocate() {
  code = static_cast<uint8_t*>(VirtualAlloc(nullptr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  counters = static_cast<Counters*>(VirtualAlloc(nullptr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  return code != nullptr && counters != nullptr;
}

inline void Arm(uint32_t presents) { dump_after = presents; }

inline std::string Row(const char* name, const uint32_t* values, size_t count) {
  std::string text = std::format("[Total War] probe: {}:", name);
  for (size_t index = 0; index < count; ++index) text += std::format(" {}", values[index]);
  return text;
}

// The histograms in the log (one line each).
inline void Dump() {
  const Counters& c = *counters;
  renodx::utils::log::i(Row("item push loop A, items left at the stub's entry (0: none, n: 2^(n-1) .. 2^n - 1)", c.evec_run[0], 17));
  renodx::utils::log::i(Row("item push loop B, items left at the stub's entry", c.evec_run[1], 17));
  renodx::utils::log::i("[Total War] probe: item push entries with a per-item call: A ", c.evec_calls[0], ", B ", c.evec_calls[1]);
  renodx::utils::log::i(Row("ready check, children (16: 16 or more)", c.ready_children, 17));
  renodx::utils::log::i(Row("ready check, first child not ready (17: none of the first 16)", c.ready_first, 18));
  renodx::utils::log::i("[Total War] probe: pending list sweep iterations ", c.sweep_iterations);
  renodx::utils::log::i(Row("pending list sweep, log2 of the count at a sweep's start", c.sweep_len, 16));
  renodx::utils::log::i("[Total War] probe: particle loop bottoms ", c.particle_exec, ", with the slot 2 on inside the size ", c.particle_taken);
  renodx::utils::log::i(Row("particle loop, particles of an emitter at the loop's exit (31: 31 or more)", c.particle_size, 32));
  renodx::utils::log::i("[Total War] probe: child sum loop bottoms ", c.sum_exec);
  renodx::utils::log::i(Row("child sum, children of a call at the loop's exit (31: 31 or more)", c.sum_children, 32));
  renodx::utils::log::i("[Total War] probe: particle writer entries ", c.entry_calls, ": none ", c.entry_null, ", the next (+24) ", c.entry_one, ", the same ", c.entry_zero, ", other ",
                        c.entry_other);
}

// Each present: counts the presents since the sites were first written and dumps once at the Nth.
inline void Tick(uint32_t written_sites) {
  if (counters == nullptr || dump_after == 0 || written_sites == 0) return;
  if (++presents_on == dump_after) Dump();
}

// The five jump bytes of every site now (after a write), and which of them are the game's own.
inline void LogSites(const std::array<Site, std::tuple_size_v<decltype(sites)>>& all, uint32_t written_sites) {
  for (size_t index = 0; index < all.size(); ++index) {
    const Site& site = all[index];
    if (site.at == nullptr) continue;
    renodx::utils::log::i("[Total War] probe: site ", site.name, " ", std::format("{:02X} {:02X} {:02X} {:02X} {:02X}", site.at[0], site.at[1], site.at[2], site.at[3], site.at[4]),
                          ((written_sites >> index & 1) != 0 ? " (the jump)" : " (the game's bytes)"));
  }
}

}  // namespace probe
