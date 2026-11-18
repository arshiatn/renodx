// Faster Game CPU Code stubs for the render list push loops (src/games/twattila/engine_code.hpp 15., EVEC_STUB): the guard and the two stubs, one after the other
// (the stubs call the guard by a relative call, so the file's layout is the page's). The two loops (Attila +0x17b83a0 head +0x17b8490: "A", and +0x17b7bc0
// head +0x17b7cb0: "B") push an item's `item + 0x50` (A) or `item + 0x38` (B) into the vector at edi ({+0x7c capacity, +0x80 size, +0x84 data} of 4-byte
// slots) one item a round of the loop, with the size in memory (`inc dword ptr [edi + 0x80]`: a store-forward chain), and reload the list ([esp + 0x1c] -> +8
// the item array), the capacity, the data pointer and the end every round. The site is the loop head's `mov eax, [esp + 0x1c]; mov ecx, [edi + 0x80]` (4 + 6
// bytes; the jump takes the first 5): the stub runs both, then, with edx == 0 (no per-item call), the size below the capacity and the room for MIN_RUN items or
// more, runs the rounds for all items but the last of the room / of the list with the size in a register, writing what the game writes in its order (the two
// frame slots, the slot, the size) and none of the loads that cannot change, and goes on at the instruction after the two (the game's code does the last
// round: every register that is live there, the flags and every byte of memory are then the game's). Where the guard can't show that no store of the run
// reaches a location the game reads again, the game's code runs the round. A: the end is [esp + 0x34], the frame slots [esp + 0x30] (the item) and [esp + 0x14];
// B: the end is ebx (live), the frame slot [esp + 0x14] only (the limit goes on the stack). Patched in Prepare: the last jump of each stub (the game's code
// after the two instructions).
.intel_syntax noprefix
.text
.globl guard
// guard: eax = the list, edx = the data pointer, esi = the capacity (< 0x100000), edi = the vector's object; returns esi = 0 if no store of a run (the
// slots [edx, edx + 4 * capacity), the size field [edi + 0x80]) can reach a location the loop reads again: the object's capacity / size / data fields
// [edi + 0x7c, edi + 0x88), the list's item array field [eax + 8, eax + 0xc), the stack from the stub's own pushes to the loop's slots (esp as of the site
// - 8 .. + 0x38: the limit B pushes, the list pointer, the end and the two scratch slots), esi = 1 if not. Keeps eax ecx edx edi ebp ebx and esp (inside it
// esp is the site's esp - 8: the call and the push).
guard:
  push ebx
  cmp edi, 0xFFFFFF00
  ja bad
  cmp eax, 0xFFFFFF00
  ja bad
  lea ebx, [edx + esi*4]
  cmp ebx, edx
  jb bad
  lea esi, [edi + 0x7c]
  cmp ebx, esi
  jbe hdr_clear
  lea esi, [edi + 0x88]
  cmp edx, esi
  jb bad
hdr_clear:
  lea esi, [eax + 8]
  cmp ebx, esi
  jbe field_clear
  lea esi, [eax + 0xc]
  cmp edx, esi
  jb bad
field_clear:
  lea esi, [edi + 0x7b]
  sub esi, eax
  cmp esi, 7
  jb bad
  cmp ebx, esp
  jbe frame_clear
  lea esi, [esp + 0x40]
  cmp edx, esi
  jb bad
frame_clear:
  lea esi, [edi + 0x87]
  sub esi, esp
  cmp esi, 0x4C
  jb bad
  lea esi, [eax + 0xb]
  sub esi, esp
  cmp esi, 0x44
  jb bad
  xor esi, esi
  pop ebx
  ret
bad:
  mov esi, 1
  pop ebx
  ret

.globl stub_a
stub_a:
  mov eax, dword ptr [esp + 0x1c]
  mov ecx, dword ptr [edi + 0x80]
  test edx, edx
  jne keep_a
  mov ebx, dword ptr [esp + 0x34]
  cmp ebp, ebx                       // the game's `jb` is unsigned: ebp not below the end is no run (the difference below can't wrap)
  jae keep_a
  sub ebx, ebp
  cmp ebx, 5                         // MIN_RUN + 1 items left at least
  jb keep_a
  mov esi, dword ptr [edi + 0x7c]
  cmp ecx, esi
  jae keep_a
  cmp esi, 0x100000
  jae keep_a
  mov edx, dword ptr [edi + 0x84]
  call guard
  test esi, esi
  jne game_a
  mov esi, dword ptr [edi + 0x7c]
  sub esi, ecx
  cmp ebx, esi
  cmova ebx, esi
  cmp ebx, 5
  jb game_a
  dec ebx
  add ebx, ebp
  mov eax, dword ptr [esp + 0x1c]
  mov eax, dword ptr [eax + 8]
run_a:
  mov esi, dword ptr [eax + ebp*4]
  mov dword ptr [esp + 0x30], esi
  add esi, 0x50
  mov dword ptr [esp + 0x14], esi
  mov dword ptr [edx + ecx*4], esi
  inc ecx
  mov dword ptr [edi + 0x80], ecx
  inc ebp
  cmp ebp, ebx
  jb run_a
game_a:
  xor edx, edx
keep_a:
  mov eax, dword ptr [esp + 0x1c]
  .byte 0xE9
  .long 0                            // jmp the game's code after the site's two instructions (patched in Prepare)

.globl stub_b
stub_b:
  mov eax, dword ptr [esp + 0x1c]
  mov ecx, dword ptr [edi + 0x80]
  test edx, edx
  jne keep_b
  cmp ebp, ebx
  jae keep_b
  mov esi, ebx
  sub esi, ebp
  cmp esi, 5
  jb keep_b
  mov esi, dword ptr [edi + 0x7c]
  cmp ecx, esi
  jae keep_b
  cmp esi, 0x100000
  jae keep_b
  mov edx, dword ptr [edi + 0x84]
  call guard
  test esi, esi
  jne game_b
  mov esi, dword ptr [edi + 0x7c]
  sub esi, ecx
  mov eax, ebx
  sub eax, ebp
  cmp eax, esi
  cmova eax, esi
  cmp eax, 5
  jb game_b
  dec eax
  add eax, ebp
  push eax
  mov eax, dword ptr [esp + 0x20]
  mov eax, dword ptr [eax + 8]
run_b:
  mov esi, dword ptr [eax + ebp*4]
  add esi, 0x38
  mov dword ptr [esp + 0x18], esi
  mov dword ptr [edx + ecx*4], esi
  inc ecx
  mov dword ptr [edi + 0x80], ecx
  inc ebp
  cmp ebp, dword ptr [esp]
  jb run_b
  add esp, 4
game_b:
  xor edx, edx
keep_b:
  mov eax, dword ptr [esp + 0x1c]
  .byte 0xE9
  .long 0                            // jmp the game's code after the site's two instructions (patched in Prepare)
