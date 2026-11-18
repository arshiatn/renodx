// EHALF stub: the 20 float to half conversions of the sprite row builder (+0x1906fa0, region +0x1907511 .. +0x19077e2) in one go.
// Position independent; sentinels replaced in Prepare:
//   absolute 0x5A5A5B00 + 0x10 k: the constants table (k = 0..9)
//   rel32    0x5A5A5A11..13: HALF (the game's float to half function), RESUME (the region start + 5), END (the region's end)
// Entry: esp = F (the frame), esi = the source object, xmm0 = the first float (live in). The first 5 bytes of the region
// (lea eax, [esp + 0x38]; push eax) are what the site's jump replaced: `fallback` runs them and goes on in the game's code.
.intel_syntax noprefix
.text
.set C_ABS,      0x5A5A5B00
.set C_387FFFFF, 0x5A5A5B10
.set C_33000400, 0x5A5A5B20
.set C_ONE,      0x5A5A5B30
.set C_1FFF,     0x5A5A5B40
.set C_4000,     0x5A5A5B50
.set C_477FFFFF, 0x5A5A5B60
.set C_33800000, 0x5A5A5B70
.set C_80000000, 0x5A5A5B80
.set C_84FFFFFF, 0x5A5A5B90

// xmm4 = 4 floats' bits: no lane may be a half denormal, an overflow, an infinity or a NaN (only zeros, tiny values, normals).
.macro CHECK_VECTOR
  movdqa xmm5, xmm4
  pand xmm5, xmmword ptr [C_ABS]
  movdqa xmm6, xmm5
  pcmpgtd xmm6, xmmword ptr [C_477FFFFF]   // a above 0x477fffff
  psubd xmm5, xmmword ptr [C_33800000]
  pxor xmm5, xmmword ptr [C_80000000]
  pcmpgtd xmm5, xmmword ptr [C_84FFFFFF]   // a - 0x33800000 at least 0x05000000 (unsigned): not a half denormal
  pandn xmm6, xmm5
  movmskps eax, xmm6
  cmp eax, 15
  jne fallback
.endm

// xmm4 = 4 floats' bits (checked) -> xmm6 = their halves, sign extended to dwords: the game's rounding (the low 13 bits
// without their lowest set bit added, half way down), zero and tiny values to 0 or 1, the sign.
.macro CONVERT
  movdqa xmm7, xmm4
  psrld xmm7, 31
  pslld xmm7, 15                           // the sign, in place
  pand xmm4, xmmword ptr [C_ABS]           // a = |f|
  movdqa xmm5, xmm4
  pcmpgtd xmm5, xmmword ptr [C_33000400]
  psrld xmm5, 31                           // tiny values: 1 above 0x33000400, else 0
  movdqa xmm6, xmm4
  psubd xmm6, xmmword ptr [C_ONE]
  pand xmm6, xmm4
  pand xmm6, xmmword ptr [C_1FFF]
  paddd xmm6, xmm4
  psrld xmm6, 13
  paddd xmm6, xmmword ptr [C_4000]         // a normal value's half (+ 0x4000, as the game's)
  pcmpgtd xmm4, xmmword ptr [C_387FFFFF]   // normal: a above 0x387fffff
  pand xmm6, xmm4
  pandn xmm4, xmm5
  por xmm6, xmm4
  paddd xmm6, xmm7
  pslld xmm6, 16
  psrad xmm6, 16
.endm

.globl region
region:
  movd xmm3, edx                           // the game's code never writes edx: it is back in it at every exit
  mov ecx, dword ptr [esp + 0x68]          // the row
  mov edx, dword ptr [esp + 0x5c]          // the second object
  // The game reads each input just before it uses it, after the halves before it were stored: the same only while no
  // store reaches an input. The row's halves (+8 .. +0x2f), the frame's temporaries and the pushed arguments against the
  // source object (+0x1c .. +0x43), the second object (+0x48 .. +0x4f) and the frame: any overlap, the game's code.
  mov eax, ecx
  sub eax, esi
  add eax, 0x13
  cmp eax, 0x4f
  jb fallback
  mov eax, ecx
  sub eax, edx
  sub eax, 0x19
  cmp eax, 0x2f
  jb fallback
  lea eax, [ecx + 0x6f]
  sub eax, esp
  cmp eax, 0xdf
  jb fallback
  lea eax, [esi + 0xb]
  sub eax, esp
  cmp eax, 0x67
  jb fallback
  lea eax, [edx + 0x17]
  sub eax, esp
  cmp eax, 0x47
  jb fallback
  // floats 0..3: xmm0, [F+0x24], [esi+0x1c], (float) int [F+0x20]
  movss xmm5, dword ptr [esp + 0x24]
  movaps xmm4, xmm0
  unpcklps xmm4, xmm5
  movss xmm5, dword ptr [esi + 0x1c]
  movd xmm6, dword ptr [esp + 0x20]
  cvtdq2ps xmm6, xmm6
  unpcklps xmm5, xmm6
  movlhps xmm4, xmm5
  CHECK_VECTOR
  CONVERT
  movdqa xmm1, xmm6
  // floats 4..7: [F+0x70], [F+0x58], [F+0x50] * [F+0x74], [esi+0x40]
  movss xmm4, dword ptr [esp + 0x70]
  movss xmm5, dword ptr [esp + 0x58]
  unpcklps xmm4, xmm5
  movss xmm5, dword ptr [esp + 0x50]
  mulss xmm5, dword ptr [esp + 0x74]
  movss xmm6, dword ptr [esi + 0x40]
  unpcklps xmm5, xmm6
  movlhps xmm4, xmm5
  CHECK_VECTOR
  CONVERT
  packssdw xmm1, xmm6
  // floats 8..11: [F+0x74] * [F+0x4c], [F+0x48], 0, [F+0x44]
  movss xmm4, dword ptr [esp + 0x74]
  mulss xmm4, dword ptr [esp + 0x4c]
  movss xmm5, dword ptr [esp + 0x48]
  unpcklps xmm4, xmm5
  xorps xmm5, xmm5
  movss xmm6, dword ptr [esp + 0x44]
  unpcklps xmm5, xmm6
  movlhps xmm4, xmm5
  CHECK_VECTOR
  CONVERT
  movdqa xmm2, xmm6
  // floats 12..15: [esi+0x28 .. 0x34]
  movups xmm4, xmmword ptr [esi + 0x28]
  CHECK_VECTOR
  CONVERT
  packssdw xmm2, xmm6
  // floats 16..19: [esi+0x38], [esi+0x3c], [second+0x48], [second+0x4c]
  movlps xmm4, qword ptr [esi + 0x38]
  movhps xmm4, qword ptr [edx + 0x48]
  CHECK_VECTOR
  CONVERT
  packssdw xmm6, xmm6
  // all 20 halves: the row's words +8 .. +0x2f (the game's store order is not the memory order: float 17 goes to +0x16)
  movdqu xmmword ptr [ecx + 8], xmm1
  movdqu xmmword ptr [ecx + 0x18], xmm2
  movd dword ptr [ecx + 0x28], xmm6        // the halves of floats 16 and 17 (float 19's half is the game's own, stored after the region)
  pextrw eax, xmm6, 2
  mov word ptr [ecx + 0x2c], ax
  // the frame's temporaries as the game leaves them: the (float) int, the product, the last float
  movd xmm7, dword ptr [esp + 0x20]
  cvtdq2ps xmm7, xmm7
  movss dword ptr [esp + 0x38], xmm7
  movss xmm7, dword ptr [esp + 0x50]
  mulss xmm7, dword ptr [esp + 0x74]
  movss dword ptr [esp + 0x70], xmm7
  movss xmm0, dword ptr [edx + 0x4c]
  movss dword ptr [esp + 0x74], xmm0
  movd edx, xmm3
  // the last conversion for real: eax and ecx as the game's function leaves them, and the stack as it is there
  lea eax, [esp + 0x74]
  sub esp, 0x10
  mov dword ptr [esp], eax
  .byte 0xE8
  .long 0x5A5A5A11                         // call the game's float to half
  .byte 0xE9
  .long 0x5A5A5A13                         // jmp the region's end

fallback:                                  // the two instructions the site's jump replaced, then the game's code
  movd edx, xmm3
  lea eax, [esp + 0x38]
  push eax
  .byte 0xE9
  .long 0x5A5A5A12                         // jmp the region's start + 5

.balign 16
.globl constants
constants:
  .long 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF
  .long 0x387FFFFF, 0x387FFFFF, 0x387FFFFF, 0x387FFFFF
  .long 0x33000400, 0x33000400, 0x33000400, 0x33000400
  .long 1, 1, 1, 1
  .long 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF
  .long 0x4000, 0x4000, 0x4000, 0x4000
  .long 0x477FFFFF, 0x477FFFFF, 0x477FFFFF, 0x477FFFFF
  .long 0x33800000, 0x33800000, 0x33800000, 0x33800000
  .long 0x80000000, 0x80000000, 0x80000000, 0x80000000
  .long 0x84FFFFFF, 0x84FFFFFF, 0x84FFFFFF, 0x84FFFFFF
