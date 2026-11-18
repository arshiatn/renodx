// Float4 reload stub with 4-byte displacements (src/games/twattila/engine_code.hpp 12., Float4ReloadFarStub(0x90), the first of
// SOLDIER_ROWS_STUBS; 0xa0 and 0xb0 the same with their displacements): the soldier draw (+0x17f61a0; Thrones +0x186ade2's rows, Rome II
// +0x14b46e5's) builds each matrix row at [esp + 0x90 / 0xa0 / 0xb0] with four 4-byte stores and reloads it with one 16-byte (8 bytes of code)
// `movups xmm0, [esp + 0x90]`, which can't be forwarded from them. Each lane is loaded from its own store instead; the last jmp goes to
// the instruction after the 8-byte site, patched in Prepare. The same 128 bits in xmm0, no flag, no other register. Needs SSE4.1.
.intel_syntax noprefix
.text
.globl stub
stub:
  movss xmm0, dword ptr [esp + 0x90]
  insertps xmm0, dword ptr [esp + 0x94], 0x10
  insertps xmm0, dword ptr [esp + 0x98], 0x20
  insertps xmm0, dword ptr [esp + 0x9c], 0x30
  .byte 0xE9
  .long 0                     // jmp back
