// Faster Effect Prefetch stub (src/games/twattila/engine_code.hpp 8., CHILD_SUM_STUB): the loop bottom of the effect tree's child
// sum (+0x1890b90; Thrones +0x1905ef0). The site replaces `lea ecx, [ecx + edx*4]; cmp esi, ecx` (5 bytes at +38); the last jmp
// goes to the loop's `jne` (+43), patched in Prepare. Entry: ecx = the vector's begin, edx = its size, esi = the next slot.
.intel_syntax noprefix
.text
.globl stub
stub:
  cmp esi, ecx                // esi against the begin
  lea ecx, [ecx + edx*4]      // the end (no flags)
  jb skip                     // esi below the begin: stale, no load
  lea eax, [esi + 4]
  cmp eax, ecx
  jae skip                    // no slot after the next one
  mov eax, dword ptr [eax]    // the child after the next one
  prefetcht0 byte ptr [eax]
skip:
  cmp esi, ecx                // the flags the game's jne tests
  .byte 0xE9
  .long 0                     // jmp the loop's jne
