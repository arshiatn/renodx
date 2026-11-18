// EJOB stubs (Delegated WakeAll, design d2). Intel syntax, i686, position independent: the table's addresses and the
// game's functions are sentinels the add-on replaces in Prepare:
//   absolute 0x5A5A5A01..04: KEY PENDING ASLEEP RUNNING (the side table's fields)
//   rel32    0x5A5A5A11..14: WAKE (the game's Wake), ACQUIRE (AcquireInf), WAKEALL_REST (WakeAll + 5), STOP (the game's Stop)
.intel_syntax noprefix
.text
.set KEY,     0x5A5A5A01
.set PENDING, 0x5A5A5A02
.set ASLEEP,  0x5A5A5A03
.set RUNNING, 0x5A5A5A04

.globl wake_all
wake_all:                              // thiscall, ecx = pool; the game's WakeAll's contract (ebx esi edi kept)
  mov eax, dword ptr [KEY]
  cmp eax, ecx
  je claimed
  test eax, eax
  jne game_loop
  xor eax, eax
  lock cmpxchg dword ptr [KEY], ecx    // the first pool claims the table
  jne game_loop
claimed:
  cmp dword ptr [ecx + 4], 32          // more workers than mask bits: the game's loop
  ja game_loop
  lock or dword ptr [PENDING], 1       // locked: no later load passes it
  mov eax, dword ptr [ASLEEP]
  test eax, eax
  je delegate                          // nobody asleep in our Sleep: run the loop here, as the game does
  bsf edx, eax                         // the lowest sleeping worker
  mov eax, dword ptr [ecx + 8]
  mov eax, dword ptr [eax + edx*4]
  test eax, eax
  je delegate                          // a failed head check takes the request itself (table clean)
  cmp dword ptr [eax + 8], edx         // its index in the pool
  jne delegate
  mov ecx, eax
  .byte 0xE9
  .long 0x5A5A5A11                     // jmp the game's Wake: the sleeper runs the loop

delegate:                              // ecx = pool; one loop for every request up to now; eax ecx edx clobbered
  lock inc dword ptr [RUNNING]
  xor eax, eax
  xchg dword ptr [PENDING], eax
  test eax, eax
  je delegate_done
  call game_loop
delegate_done:
  lock dec dword ptr [RUNNING]
  ret

game_loop:                             // the game's WakeAll: its first 5 bytes, then its own code
  push ebx
  push esi
  push edi
  mov edi, ecx
  .byte 0xE9
  .long 0x5A5A5A13

.globl sleep
sleep:                                 // thiscall, ecx = worker; replaces the game's Sleep whole
  push esi
  mov esi, ecx
  cmp dword ptr [esi + 0x18], 3
  je sleep_out
  mov dword ptr [esi + 0x18], 1
  mov eax, dword ptr [esi + 4]         // pool
  cmp eax, dword ptr [KEY]
  jne sleep_game
  mov ecx, dword ptr [esi + 8]         // index
  cmp ecx, 32
  jae sleep_game
  cmp ecx, dword ptr [eax + 4]         // within the pool's size (the array may still be growing)
  jae sleep_game
  mov edx, dword ptr [eax + 8]
  cmp dword ptr [edx + ecx*4], esi
  jne sleep_game
  lock bts dword ptr [ASLEEP], ecx
  cmp dword ptr [PENDING], 0           // a request that missed the bit
  je acquire
  mov ecx, eax
  call delegate
acquire:
  lea ecx, [esi + 0x1c]
  .byte 0xE8
  .long 0x5A5A5A12                     // call AcquireInf
  mov ecx, dword ptr [esi + 8]
  lock btr dword ptr [ASLEEP], ecx
  cmp dword ptr [PENDING], 0           // the request that woke us
  je reset
  mov ecx, dword ptr [esi + 4]
  call delegate
reset:
  xor ecx, ecx
  lea eax, [esi + 0x50]
  xchg dword ptr [eax], ecx
sleep_out:
  pop esi
  ret
sleep_game:                            // not our pool or worker: the game's Sleep
  lea ecx, [esi + 0x1c]
  .byte 0xE8
  .long 0x5A5A5A12
  jmp reset

.globl stop
stop:                                  // thiscall, ecx = worker: the worker destructor's call of the game's Stop
  mov eax, dword ptr [ecx + 4]
  cmp eax, dword ptr [KEY]
  jne stop_game
  xor eax, eax
  xchg dword ptr [PENDING], eax        // a request that no worker will run any more is dropped
  mov eax, 0x10000000                  // 2^28 pauses (3 s on Zen 4, 0.8 to 9 s by CPU): a delegate preempted for longer is not waited for
drain:
  cmp dword ptr [RUNNING], 0
  je stop_game
  pause
  dec eax
  jne drain
stop_game:
  .byte 0xE9
  .long 0x5A5A5A14                     // jmp the game's Stop
