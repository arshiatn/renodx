// Faster Effect Prefetch stub (src/games/twattila/engine_code.hpp 8., CHILD_SUM_STUB): the loop bottom of the effect tree's child
// sum (+0x1890b90; Thrones +0x1905ef0). The site replaces `lea ecx, [ecx + edx*4]; cmp esi, ecx` (5 bytes at +38); the last jmp
// goes to the loop's `jne` (+43), patched in Prepare. Entry: ecx = the vector's begin, edx = its size, esi = the next slot.
// The load of the slot after the next one happens only when its 4 bytes [esi + 4, esi + 8) lie wholly inside [begin, end) with
// no wrap, whatever esi's alignment against the begin.
.intel_syntax noprefix
.text
.globl stub
stub:
  cmp esi, ecx                // esi against the begin
  lea ecx, [ecx + edx*4]      // the end (no flags)
  jb skip                     // esi below the begin: stale, no load
  mov eax, esi
  add eax, 8
  jc skip                     // esi + 8 past 2^32
  cmp eax, ecx
  ja skip                     // the slot after the next one is not wholly inside
  mov eax, dword ptr [eax - 4] // the child after the next one
  prefetcht0 byte ptr [eax]
skip:
  cmp esi, ecx                // the flags the game's jne tests
  .byte 0xE9
  .long 0                     // jmp the loop's jne
