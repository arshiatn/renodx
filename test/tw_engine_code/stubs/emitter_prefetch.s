// Faster Emitter Prefetch stub for the emitter update loop (src/games/twattila/engine_code.hpp 11., EMITTER_PREFETCH_STUB): the serial arm of
// +0x1893330 calls the emitter update (+0x1893470) for the emitters [[container + 8] + i*8], i = 0 .. the count cached at [esp + 0x34], and ends each
// round with `inc esi; cmp esi, [esp + 0x34]; je the way out` (5 bytes at +0x18933ce, the site, the `je` at +0x18933d3). The stub runs the same two
// instructions around two prefetch stages, K = 1 and K + 1 = 2 slots after the next emitter's (esi is the next emitter's index after the `inc`): it
// loads the definition pointer ([emitter + 0x58], the stub's one real load) of the emitter in slot esi + K, whose lines the previous round prefetched,
// and prefetches the definition's line; and it prefetches the lines at +0, +0x40 and +0x70 of the emitter in slot esi + K + 1 (the three lines the
// emitter's first 0x78 bytes lie on). Both stages run behind one check of the slot esi + K + 1: below the count the loop runs to, and below the size
// the vector has right now ([container + 4]). edx and ecx are written, and both are dead there (the loop's top, the way out and the callee write or
// never read them first); the flags the je tests are the game's cmp. The container comes from [esp + 0x20], the slot the game reloads ebx from.
// The jump goes to the game's je (+0x18933d3), patched in Prepare.
.intel_syntax noprefix
.text
.globl stub
stub:
  inc esi                            // the next emitter's index
  lea ecx, [esi + 2]                 // the slot K + 1 = 2 after it
  cmp ecx, dword ptr [esp + 0x34]    // below the count the loop runs to
  jae skip
  mov edx, dword ptr [esp + 0x20]    // the container, as the game reloads it
  cmp ecx, dword ptr [edx + 4]       // below the size the vector has now: the slot holds a live shared_ptr
  jae skip
  mov edx, dword ptr [edx + 8]       // the elements as they are now
  mov ecx, dword ptr [edx + esi*8 + 8]   // the emitter K = 1 after the next one
  test ecx, ecx
  je lines
  mov ecx, dword ptr [ecx + 0x58]    // its definition: the one real load of the stub, on a line the last round prefetched
  prefetcht0 byte ptr [ecx + 0x2c]
lines:
  mov ecx, dword ptr [edx + esi*8 + 16]  // the emitter K + 1 = 2 after the next one (a null one: the prefetches go nowhere)
  prefetcht0 byte ptr [ecx]
  prefetcht0 byte ptr [ecx + 0x40]
  prefetcht0 byte ptr [ecx + 0x70]
skip:
  cmp esi, dword ptr [esp + 0x34]    // the flags the game's je tests
  .byte 0xE9
  .long 0                            // jmp the game's je (+0x18933d3)
