// Particle writer float4 reload stub (src/games/twattila/engine_code.hpp 9., FLOAT4_RELOAD_STUB): the particle writer (+0x18960d0;
// Thrones +0x190b430) builds a float4 at [esp + 0x40 .. 0x4f] with four 4-byte stores and reloads it with one 16-byte
// `movups xmm0, [esp + 0x40]`, which can't be forwarded from them. The site is that 5-byte instruction (+0x189685b and +0x18968c1; one
// stub each, the same bytes); each lane is loaded from its own store instead, and the last jmp goes to the game's `movups [eax], xmm0`
// after the site, patched in Prepare. The same 128 bits in xmm0, no flag, no other register. Needs SSE4.1.
.intel_syntax noprefix
.text
.globl stub
stub:
  movss xmm0, dword ptr [esp + 0x40]
  insertps xmm0, dword ptr [esp + 0x44], 0x10
  insertps xmm0, dword ptr [esp + 0x48], 0x20
  insertps xmm0, dword ptr [esp + 0x4c], 0x30
  .byte 0xE9
  .long 0                     // jmp back
