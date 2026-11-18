// Faster Effect Prefetch stub for the particle update loop (src/games/twattila/engine_code.hpp 10., PARTICLE_PREFETCH_STUB): the loop of
// the emitter update (+0x1893470; Thrones +0x1908b80 for the loop) takes particle [[edi + 0x68] + ebx*4] for ebx < [edi + 0x64], calls its
// update through the vtable, and ends with `cmp ebx, [edi + 0x64]; jb the loop's top` (5 bytes at +0x1893883, the site). The stub prefetches the
// four lines of the particle K = 2 places after the next one (ebx is already the next one's index); the two jumps go to the loop's top and to
// the code after the loop (+0x1893888), patched in Prepare. esi is dead there (reloaded at the top, only popped after the loop); only
// esi is written; the flags the jb tests are the game's cmp. The one load is a slot of the vector as it stands right now.
.intel_syntax noprefix
.text
.globl stub
stub:
  lea esi, [ebx + 2]                 // the particle K = 2 after the next one
  cmp esi, dword ptr [edi + 0x64]    // inside the count as it is now (no wrap: lea wraps like the load's address does)
  jae skip
  mov esi, dword ptr [edi + 0x68]    // the array as it is now
  mov esi, dword ptr [esi + ebx*4 + 8]
  prefetcht0 byte ptr [esi]
  prefetcht0 byte ptr [esi + 0x40]
  prefetcht0 byte ptr [esi + 0x80]
  prefetcht0 byte ptr [esi + 0xc0]
skip:
  cmp ebx, dword ptr [edi + 0x64]    // the flags the game's jb tests
  .byte 0x0F, 0x82
  .long 0                            // jb the loop's top
  .byte 0xE9
  .long 0                            // jmp the code after the loop
