// Faster Sweep Prefetch stub for the pending list sweep (src/games/twattila/engine_code.hpp 16., SWEEP_STUB = SweepPrefetchStub(8, 4, 2)). The sweep (the render thread's
// per-frame +0x1893290, thiscall, one caller) walks the vector of {list object, control block} at [esi + 0x1c], count at [esi + 0x18], downwards (ebx = count - 1 .. 0)
// and calls the ready check on each entry's list object (the check reads [L + 4], [L + 0x44] (the children array), [L + 0x40] (the count) and the first child's object
// pointer and ready byte [child + 0x6e]). The site is `mov eax, [esi + 0x1c]; mov ecx, [eax + ebx*8]` (3 + 3 bytes; the jump takes the first 5): the stub runs both,
// after three stages for the entries A, B and C places below ebx (each inside the vector as it is right now: unsigned `ebx - d < count`): stage A loads the entry
// and prefetches the list object's first two lines, stage B loads the entry (a null list object is skipped) and the list object's children array pointer (the
// line was prefetched A - B entries ago) and prefetches the array's line, stage C loads the entry, the count (0: skipped), the array pointer and the first child's
// object pointer and prefetches the child's ready byte. Every load is of an address the game's own check of that entry reads a few entries later in the same sweep.
// The stub goes on at the call (+6). Written for A = 8, B = 4, C = 2 (the `lea edx, [ebx - d]` displacements follow them).
.intel_syntax noprefix
.text
.globl stub
stub:
  mov eax, dword ptr [esi + 0x1c]
  lea edx, [ebx - 8]
  cmp edx, dword ptr [esi + 0x18]
  jae skip_a
  mov ecx, dword ptr [eax + edx*8]
  prefetcht0 byte ptr [ecx]
  prefetcht0 byte ptr [ecx + 0x40]
skip_a:
  lea edx, [ebx - 4]
  cmp edx, dword ptr [esi + 0x18]
  jae skip_b
  mov ecx, dword ptr [eax + edx*8]
  test ecx, ecx
  je skip_b
  mov ecx, dword ptr [ecx + 0x44]
  prefetcht0 byte ptr [ecx]
skip_b:
  lea edx, [ebx - 2]
  cmp edx, dword ptr [esi + 0x18]
  jae skip_c
  mov ecx, dword ptr [eax + edx*8]
  test ecx, ecx
  je skip_c
  cmp dword ptr [ecx + 0x40], 0
  je skip_c
  mov ecx, dword ptr [ecx + 0x44]
  mov ecx, dword ptr [ecx]
  prefetcht0 byte ptr [ecx + 0x6e]
skip_c:
  mov ecx, dword ptr [eax + ebx*8]
  .byte 0xE9
  .long 0                            // jmp the call (+6; patched in Prepare)
