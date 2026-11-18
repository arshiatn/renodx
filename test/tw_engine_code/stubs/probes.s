// The probe build's histogram stubs (src/games/twattila/engine_code.hpp 17., compiled only with TWATTILA_ENGINE_PROBE; a private build, no row). Each stub is a prefix of a
// site: its jump goes here first, the stub keeps every register and the flags (pushfd, the registers it spends), counts into the probe data page with locked increments
// (the effect loops run on the job threads too) and goes on with `jmp` to the site's production stub, which then runs exactly as it always does: the production stubs are
// untouched. The counters' addresses are the field sentinels STUB_FIELD_SENTINEL + i (resolve() puts the data page's addresses in): the numbers F0 .. F19 below.
.intel_syntax noprefix
.text
.set F_EVEC_A_RUN, 0x5A5A5A01     // [17] the remaining items at stub A's entry: bucket 0: none, n: 2^(n-1) .. 2^n - 1, 16: 32768 or more
.set F_EVEC_B_RUN, 0x5A5A5A02     // [17] the same at stub B's
.set F_EVEC_CALLS_A, 0x5A5A5A03   // [1] A's entries with edx != 0 (the per-item call path: the game's code)
.set F_EVEC_CALLS_B, 0x5A5A5A04
.set F_READY_CHILDREN, 0x5A5A5A05 // [18] the ready check's children (0 .. 15, 16 = 16 or more)
.set F_READY_FIRST, 0x5A5A5A06    // [18] the first child not ready (0 .. 15), 17: none of the first 16 (or the list's flag clear)
.set F_SWEEP_ITER, 0x5A5A5A07     // [1] the sweep loop's iterations
.set F_SWEEP_LEN, 0x5A5A5A08      // [32] log2 of the pending list count at a sweep's start
.set F_SWEEP_LAST, 0x5A5A5A09     // [1] the last iteration's ebx (a sweep starts when ebx does not fall)
.set F_PARTICLE_EXEC, 0x5A5A5A0A  // [1] the particle loop's bottoms
.set F_PARTICLE_TAKEN, 0x5A5A5A0B // [1] of them with the slot PARTICLE_PREFETCH_AHEAD (2) places on inside the size: the prefetch taken
.set F_PARTICLE_SIZE, 0x5A5A5A0C  // [32] the particle count of an emitter at the loop's exit (0 .. 30, 31 = 31 or more)
.set F_SUM_EXEC, 0x5A5A5A0D       // [1] the child sum loop's bottoms
.set F_SUM_CHILDREN, 0x5A5A5A0E   // [32] the child count of a call at the loop's exit
.set F_ENTRY_CALLS, 0x5A5A5A0F    // [1] the particle writer's table entry lookups
.set F_ENTRY_NULL, 0x5A5A5A10     // [1] the lookup found no entry
.set F_ENTRY_ONE, 0x5A5A5A11      // [1] the entry 24 bytes after the previous one's (the next index)
.set F_ENTRY_ZERO, 0x5A5A5A12     // [1] the same entry
.set F_ENTRY_OTHER, 0x5A5A5A13    // [1] another
.set F_ENTRY_PREV, 0x5A5A5A14     // [1] the previous entry
.globl probe_evec_a
.globl probe_evec_b
.globl probe_ready
.globl probe_sweep
.globl probe_particle
.globl probe_sum
.globl probe_entry
.globl probe_end

// Item push loop A's head (ebp the index, [esp + 0x34] the end: the three pushes below move it to [esp + 0x40]).
probe_evec_a:
  pushfd
  push eax
  push ecx
  xor ecx, ecx
  mov eax, dword ptr [esp + 0x40]
  cmp ebp, eax
  jae a_count
  sub eax, ebp
  bsr ecx, eax
  inc ecx
  cmp ecx, 16
  jbe a_count
  mov ecx, 16
a_count:
  lock inc dword ptr [ecx*4 + F_EVEC_A_RUN]
  test edx, edx
  je a_done
  lock inc dword ptr [F_EVEC_CALLS_A]
a_done:
  pop ecx
  pop eax
  popfd
  .byte 0xE9
  .long 0

// Loop B's head (ebp the index, ebx the end).
probe_evec_b:
  pushfd
  push eax
  push ecx
  xor ecx, ecx
  cmp ebp, ebx
  jae b_count
  mov eax, ebx
  sub eax, ebp
  bsr ecx, eax
  inc ecx
  cmp ecx, 16
  jbe b_count
  mov ecx, 16
b_count:
  lock inc dword ptr [ecx*4 + F_EVEC_B_RUN]
  test edx, edx
  je b_done
  lock inc dword ptr [F_EVEC_CALLS_B]
b_done:
  pop ecx
  pop eax
  popfd
  .byte 0xE9
  .long 0

// The ready check's entry (ecx the list object; the whole function is the stub): the count, and the first child not ready among the first 16 (17 when the list's flag
// [+4] is clear: the game's check reads no child of such a list, so neither does the probe: its array pointer and children may be anything).
probe_ready:
  pushfd
  push eax
  push edx
  push esi
  push ecx
  mov eax, dword ptr [ecx + 0x40]
  mov edx, 16
  cmp eax, edx
  cmova eax, edx
  lock inc dword ptr [eax*4 + F_READY_CHILDREN]
  mov esi, dword ptr [ecx + 0x44]
  cmp byte ptr [ecx + 4], 0
  je r_none
  xor edx, edx
r_next:
  cmp edx, eax
  jae r_none
  mov ecx, dword ptr [esi + edx*8]
  cmp byte ptr [ecx + 0x6e], 0
  je r_found
  inc edx
  jmp r_next
r_none:
  mov edx, 17
r_found:
  lock inc dword ptr [edx*4 + F_READY_FIRST]
  pop ecx
  pop esi
  pop edx
  pop eax
  popfd
  .byte 0xE9
  .long 0

// The pending list sweep's loop top (esi this, ebx the index): the iterations and the count of each sweep.
probe_sweep:
  pushfd
  push eax
  push ecx
  lock inc dword ptr [F_SWEEP_ITER]
  cmp ebx, dword ptr [F_SWEEP_LAST]
  jb s_same
  mov eax, dword ptr [esi + 0x18]
  or eax, 1
  bsr ecx, eax
  lock inc dword ptr [ecx*4 + F_SWEEP_LEN]
s_same:
  mov dword ptr [F_SWEEP_LAST], ebx
  pop ecx
  pop eax
  popfd
  .byte 0xE9
  .long 0

// The particle update loop's bottom (ebx the next particle's index, edi the emitter: [edi + 0x64] its particle count).
probe_particle:
  pushfd
  push eax
  push ecx
  lock inc dword ptr [F_PARTICLE_EXEC]
  mov eax, dword ptr [edi + 0x64]
  cmp ebx, eax
  jb p_inside
  mov ecx, 31
  cmp eax, ecx
  cmova eax, ecx
  lock inc dword ptr [eax*4 + F_PARTICLE_SIZE]
  jmp p_done
p_inside:
  lea ecx, [ebx + 2]
  cmp ecx, eax
  jae p_done
  lock inc dword ptr [F_PARTICLE_TAKEN]
p_done:
  pop ecx
  pop eax
  popfd
  .byte 0xE9
  .long 0

// The effect tree's child sum loop's bottom (esi the next slot, ecx the vector's begin, edx its size).
probe_sum:
  pushfd
  push eax
  lock inc dword ptr [F_SUM_EXEC]
  lea eax, dword ptr [ecx + edx*4]
  cmp esi, eax
  jb m_done
  cmp edx, 31
  mov eax, edx
  jbe m_count
  mov eax, 31
m_count:
  lock inc dword ptr [eax*4 + F_SUM_CHILDREN]
m_done:
  pop eax
  popfd
  .byte 0xE9
  .long 0

// The particle writer's table entry (eax the entry the lookup returned, before the game's store).
probe_entry:
  pushfd
  push ecx
  lock inc dword ptr [F_ENTRY_CALLS]
  test eax, eax
  jne e_nonnull
  lock inc dword ptr [F_ENTRY_NULL]
  jmp e_done
e_nonnull:
  mov ecx, eax
  sub ecx, dword ptr [F_ENTRY_PREV]
  mov dword ptr [F_ENTRY_PREV], eax
  cmp ecx, 24
  je e_one
  test ecx, ecx
  je e_zero
  lock inc dword ptr [F_ENTRY_OTHER]
  jmp e_done
e_one:
  lock inc dword ptr [F_ENTRY_ONE]
  jmp e_done
e_zero:
  lock inc dword ptr [F_ENTRY_ZERO]
e_done:
  pop ecx
  popfd
  .byte 0xE9
  .long 0
probe_end:
