// Faster Effect Prefetch stub (src/games/twattila/engine_code.hpp 8., CHILD_SUM_STUB = ChildSumStub<K>()): the loop bottom of the effect
// tree's child sum (+0x1890b90; Thrones +0x1905ef0). The site replaces `lea ecx, [ecx + edx*4]; cmp esi, ecx` (5 bytes at +38); the last
// jmp goes to the loop's `jne` (+43), patched in Prepare. Entry: ecx = the vector's begin, edx = its size, esi = the next slot. Written for
// the K in `CHILD_SUM_AHEAD` (4: the adds' immediates 2K and 4K follow it). Each slot has its own check: the near slot
// [esi + 2K - 4, esi + 2K) is loaded only when it lies wholly inside [begin, end) with no wrap, the far slot [esi + 4K - 4, esi + 4K)
// the same, whatever esi's alignment against the begin; a near slot that fails fails the far one too (skip).
.intel_syntax noprefix
.text
.globl stub
stub:
  cmp esi, ecx                // esi against the begin
  lea ecx, [ecx + edx*4]      // the end (no flags)
  jb skip                     // esi below the begin: stale, no load
  mov eax, esi
  add eax, 8                  // 2K
  jc skip                     // esi + 2K past 2^32 (esi + 4K too)
  cmp eax, ecx
  ja skip                     // the near slot is not wholly inside (the far one neither)
  mov eax, dword ptr [eax - 4] // the child K / 2 - 1 after the next one
  prefetcht0 byte ptr [eax]
  mov eax, esi
  add eax, 16                 // 4K
  jc skip                     // esi + 4K past 2^32
  cmp eax, ecx
  ja skip                     // the far slot is not wholly inside
  mov eax, dword ptr [eax - 4] // the child K - 1 after the next one
  prefetcht0 byte ptr [eax]
skip:
  cmp esi, ecx                // the flags the game's jne tests
  .byte 0xE9
  .long 0                     // jmp the loop's jne
