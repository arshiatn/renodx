// Faster Particle Prefetch stub for the particle writer's table entry (src/games/twattila/engine_code.hpp 14., ENTRY_FETCH_STUB): the writer (+0x18960d0)
// looks up the table entry of its six stream pointers (a thiscall returning the table's base + 24 * index, no load of the entry), then runs
// `mov dword ptr [esp + 0x7c], eax; test eax, eax; je the way out` (the 5 bytes at +0x18960f5, the site: the mov and the test's first byte). The first load of the
// entry is ~1,000 instructions on, so the stub prefetches the entry's first and last byte (24 bytes: one line or two) between the game's mov and test. No register
// but the flags changes (the test is the game's), no store but the game's own, and a prefetch never faults. The jump goes to the game's je (+0x18960fb), patched
// in Prepare.
.intel_syntax noprefix
.text
.globl stub
stub:
  mov dword ptr [esp + 0x7c], eax    // the game's store
  prefetcht0 byte ptr [eax]          // the entry's first line
  prefetcht0 byte ptr [eax + 0x17]   // and its last byte's (the entry is 24 bytes)
  test eax, eax                      // the flags the game's je tests
  .byte 0xE9
  .long 0                            // jmp the game's je
