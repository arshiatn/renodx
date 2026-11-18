// Faster Effect Prefetch stub for the emitter update loop (src/games/twattila/engine_code.hpp 11., EMITTER_PREFETCH_STUB): the serial arm of
// +0x1893330 calls the emitter update (+0x1893470) for the emitters [[container + 8] + i*8], i = 0 .. the count cached at [esp + 0x34], and ends each
// round with `inc esi; cmp esi, [esp + 0x34]; je the way out` (5 bytes at +0x18933ce, the site, the `je` at +0x18933d3). The stub runs the
// same two instructions around the prefetch of the emitter K = 1 places after the next one: its first two lines and the definition's first line
// ([[emitter + 0x58] + 0x2c], the one real load is [emitter + 0x58]); the jump goes to the `je`, patched in Prepare. ecx is dead there (the loop's top
// and the way out both write it first); only ecx is written; the flags the je tests are the game's cmp. The container comes from [esp + 0x20], the
// slot the game reloads ebx from, so nothing here depends on a register the call keeps.
.intel_syntax noprefix
.text
.globl stub
stub:
  inc esi                            // the next emitter's index
  lea ecx, [esi + 1]                 // the emitter K = 1 after it
  cmp ecx, dword ptr [esp + 0x34]    // inside the count the loop runs to
  jae skip
  mov ecx, dword ptr [esp + 0x20]    // the container, as the game reloads it
  mov ecx, dword ptr [ecx + 8]       // the elements as they are now
  mov ecx, dword ptr [ecx + esi*8 + 8]
  test ecx, ecx
  je skip
  prefetcht0 byte ptr [ecx]
  prefetcht0 byte ptr [ecx + 0x40]
  mov ecx, dword ptr [ecx + 0x58]    // the definition: the one real load of the stub
  prefetcht0 byte ptr [ecx + 0x2c]
skip:
  cmp esi, dword ptr [esp + 0x34]    // the flags the game's je tests
  .byte 0xE9
  .long 0                            // jmp the game's je (+0x18933d3)
