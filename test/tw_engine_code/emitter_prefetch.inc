// ====================================================================================================================
// 16. The emitter update loop's prefetch (the effect update +0x1893330, its serial arm's loop +0x18933c0 .. +0x18933e5, site +0x18933ce; Thrones
// function +0x1908690; Faster Emitter Prefetch; engine_code.hpp 11.), included inside tests.cpp's anonymous namespace after particle_prefetch.inc (LoopCall,
// EnterParticleLoop, ParticleLoopReturn, LoopBlockEnd and EnterSumBlock's friends are that test's: the loop is entered by itself on a fake frame at
// its top, its exit +0x18933eb hooked to ParticleLoopReturn, its call of the emitter update +0x1893470 pointed at a fake). The loop calls the update with the
// emitter [[container + 8] + i*8], i from esi while `inc esi; cmp esi, [esp + 0x34]; je the way out` (the container's address is the slot [esp + 0x20] it
// reloads ebx from); the stub at the `inc esi` works on the slots K = 1 and K + 1 after the next one: it prefetches the emitter's three lines in the
// second and loads the definition of the first (whose lines the round before prefetched) to prefetch the definition's line, both behind one check of the
// second slot against the count and against the size the vector has at that moment.
// a. the site and the stub as Prepare made them (jumps, bytes);
// b. the loop's bottom alone (the game's two instructions and a copy of the stub, both exits to the harness): every esi around the count 0 to 48, esi at
//    the edges of the address space, random registers, null slots, a vector size below, at or above the count, an array that ends where the next page is
//    no access (a load past the vector crashes the test), every register but ecx and edx, the flags, esp and which way it went equal, ecx and edx as the
//    stub's path leaves them (the slot, or the index; the container, or the elements); CONTROLs: `ja` for `jae` on either bound must be seen;
// c. the whole loop in both copies of the DLL: random vectors of 1 to 48 emitters (null slots among them, the emitters' definition pointers garbage or
//    valid) with a fake update that counts its calls, hashes their order, stores into its emitter, overwrites ebx edi ebp (which the game's callee keeps:
//    nothing may depend on them) and may change the vector at one call (a new array, grown or not, or only the size field; or the size shrunk to put the
//    next two slots outside it, those slots made no-access pointers until the next call puts them back), the array ending at a no-access page, garbage
//    in the slots the loop never reads (below the start, past the count): every register but ecx and edx, the flags, esp, xmm0-7, every byte of the
//    container, the emitters, the definitions, the context and the frame (a return address apart) and the call order equal;
//    CONTROLs: a stub that writes eax, ends with the wrong compare, stores, has no null test, has the wrong live-size bound, has no live-size bound or
//    reads ebx for the container must each be seen;
// d. 200 switches under 4 threads that run the loop in both copies, a parked thread at each byte of the site;
// e. timing against the stub that takes one slot and loads its definition at once: emitters of 256 bytes at 8 mod 16 spread over 256 MB with their
//    definitions over another 128 MB, the update loads the lines at +0x14, +0x48, +0x58 and +0x70 and the definition's word, and multiplies 1 / 100 /
//    1000 times.
// ====================================================================================================================

constexpr uint32_t EMITTER_SLOTS = 48;        // the vector's room in 8-byte slots: its block ends where the next page is no access
constexpr uint32_t EMITTER_BYTES = 0x80;
constexpr uint32_t DEFINITION_BYTES = 0x40;
constexpr uint32_t EMITTER_FUNCTION_SIZE = 316;
constexpr uint32_t EMITTER_LOOP_TOP = 0x90;   // +0x18933c0: mov ecx, [ebx + 8]
constexpr uint32_t EMITTER_LOOP_CALL = 0x99;  // +0x18933c9: call the emitter update
constexpr uint32_t EMITTER_LOOP_EXIT = 0xBB;  // +0x18933eb: mov ecx, [edi + 0x18], where the je's way out (mov edi, [esp + 0x10]) comes to

// What the fake update counts and may change.
struct EmitterContext {
  uint32_t calls;         // 0: the updates so far
  uint32_t order;         // 4: a hash of the order, the emitters and what they hold
  uint32_t trigger;       // 8: the update after which the container changes (0: never)
  uint32_t container;     // 12
  uint32_t new_size;      // 16
  uint32_t new_array;     // 20
  uint32_t restore;       // 24: the update after which the hole is closed again (0: never)
  uint32_t hole;          // 28: the address of the first of the two slots the hole covers (0: no hole)
  std::array<uint32_t, 2> saved;  // 32: their pointers
  uint32_t restore_size;  // 40: the size the hole's close puts back
  uint32_t poison;        // 44: what the hole puts in the two slots
};

// The container [+4 the size, +8 the elements, 8 bytes each: a pointer and a control block word], 48 emitters [+8 their number, +0xC the update's call
// number, +0x58 their definition, +0x6e a byte: whether +0x58 holds a valid definition], 48 definitions [+0x2c a word], the context.
struct EmitterWorld {
  LoopCall call;
  alignas(16) uint8_t container[0x20];
  alignas(16) uint8_t emitters[EMITTER_SLOTS][EMITTER_BYTES];
  alignas(16) uint8_t definitions[EMITTER_SLOTS][DEFINITION_BYTES];
  EmitterContext context;
};

// The emitter update (thiscall, ecx the emitter or null, three stack arguments of which the third is the context, ret 0xC; esi kept as the game's callee
// keeps it, ebx edi ebp overwritten with small addresses: the loop reloads them, and nothing may depend on what a call leaves in them; eax comes out a hash of
// the call number): counts its calls, hashes the emitter's address and, when the byte at +0x6e says it holds one, its definition's word at +0x2c, records
// its call number in the emitter (+0xC), after call `trigger` changes the container, and with a hole (the context's `hole`) puts no-access pointers into two
// slots at call `trigger` and the slots' own pointers and the size back at call `restore`.
__declspec(naked) void EmitterUpdate() {
  __asm {
    mov edx, [esp + 0xC]
    mov eax, [edx]
    inc eax
    mov [edx], eax
    mov ebx, [edx + 4]
    imul ebx, ebx, 31
    add ebx, ecx
    test ecx, ecx
    jz hashed
    mov [ecx + 0xC], eax
    cmp byte ptr [ecx + 0x6E], 0
    je hashed
    mov edi, [ecx + 0x58]
    add ebx, [edi + 0x2C]
  hashed:
    mov [edx + 4], ebx
    cmp eax, [edx + 8]
    jne not_trigger
    mov ebp, [edx + 12]
    mov edi, [edx + 16]
    mov [ebp + 4], edi
    mov edi, [edx + 20]
    mov [ebp + 8], edi
    mov eax, [edx]
  not_trigger:
    mov edi, [edx + 28]
    test edi, edi
    jz unchanged
    cmp eax, [edx + 8]
    jne hole_closing
    mov ebx, [edx + 44]
    mov [edi], ebx
    mov [edi + 8], ebx
    jmp unchanged
  hole_closing:
    cmp eax, [edx + 24]
    jne unchanged
    mov ebx, [edx + 32]
    mov [edi], ebx
    mov ebx, [edx + 36]
    mov [edi + 8], ebx
    mov ebp, [edx + 12]
    mov ebx, [edx + 40]
    mov [ebp + 4], ebx
  unchanged:
    imul eax, eax, -1640531535  // 0x9E3779B1
    mov ebx, 0x40
    mov edi, 0x44
    mov ebp, 0x48
    ret 0xC
  }
}

// The update of the timing: loads the emitter's words at +0x14, +0x48 and +0x70 (the real update reads and writes from +0x14 through +0x77: on every line of
// the object) and its definition's word, then multiplies `emitter_busy` times (3 cycles each, dependent).
uint32_t emitter_busy = 1;
__declspec(naked) void EmitterUpdateBusy() {
  __asm {
    mov eax, [ecx + 0x14]
    add eax, [ecx + 0x48]
    add eax, [ecx + 0x70]
    mov edx, [ecx + 0x58]
    add eax, [edx + 0x2C]
    mov edx, emitter_busy
  spin:
    imul eax, eax, 3
    dec edx
    jnz spin
    ret 0xC
  }
}

// The stub as it was before the second stage: one slot, K = 1 after the next emitter's, behind the same two bounds, its two lines prefetched and its
// definition loaded at once (53 bytes; stubs/emitter_prefetch.s is the stub of the header). Only the timing builds it, to show what the second stage gains.
constexpr std::array<uint8_t, 53> ONE_STAGE_STUB = {
    0x46,                          // inc esi
    0x8D, 0x4E, 0x01,              // lea ecx, [esi + 1]
    0x3B, 0x4C, 0x24, 0x34,        // cmp ecx, dword ptr [esp + 0x34]
    0x73, 0x22,                    // jae skip
    0x8B, 0x54, 0x24, 0x20,        // mov edx, dword ptr [esp + 0x20]
    0x3B, 0x4A, 0x04,              // cmp ecx, dword ptr [edx + 4]
    0x73, 0x19,                    // jae skip
    0x8B, 0x4A, 0x08,              // mov ecx, dword ptr [edx + 8]
    0x8B, 0x4C, 0xF1, 0x08,        // mov ecx, dword ptr [ecx + esi*8 + 8]
    0x85, 0xC9,                    // test ecx, ecx
    0x74, 0x0E,                    // je skip
    0x0F, 0x18, 0x09,              // prefetcht0 byte ptr [ecx]
    0x0F, 0x18, 0x49, 0x40,        // prefetcht0 byte ptr [ecx + 0x40]
    0x8B, 0x49, 0x58,              // mov ecx, dword ptr [ecx + 0x58]
    0x0F, 0x18, 0x49, 0x2C,        // prefetcht0 byte ptr [ecx + 0x2c]
    0x3B, 0x74, 0x24, 0x34,        // skip: cmp esi, dword ptr [esp + 0x34]
    0xE9, 0x00, 0x00, 0x00, 0x00,  // jmp the game's je
};

struct EmitterSpec {
  uint32_t count;       // the count the loop runs to ([esp + 0x34]), 0 .. 48
  uint32_t start;       // the first emitter's index (esi), below the count
  uint32_t extent;      // the elements' room in slots: count (the last slot ends where the next page is no access) .. 48 (garbage past the count)
  uint32_t nulls;       // 0 .. 8: the chance in eight that a slot the loop reads is null
  uint32_t mode;        // 0 the vector stays; 1 a new array of new_extent slots, the size new_extent (it grew); 2 a new array, the size as it was; 3 only the size changes;
                        // 4 a hole: after update `trigger` the size is start + trigger + K, so that the two slots the stub reads next (start + trigger + K and the one
                        // after, both below the count) lie outside the vector, and those slots hold pointers to a no-access page until update trigger + 1 puts them back
  uint32_t trigger;     // the update after which it changes (1 on)
  uint32_t new_extent;  // modes 1 and 2: count .. 48
  uint32_t new_size;    // mode 3
};

// The slot K after the next emitter's, K = the stub's: the hole (mode 4) starts there.
constexpr uint32_t EMITTER_AHEAD = twattila::engine_code::internal::EMITTER_PREFETCH_AHEAD;

EmitterSpec EmitterRandomSpec(Random* random) {
  EmitterSpec spec = {.count = 1 + random->Next() % EMITTER_SLOTS};
  spec.start = ((random->Next() & 3) == 0 ? random->Next() % spec.count : 0);
  spec.extent = ((random->Next() & 1) != 0 ? spec.count : spec.count + random->Next() % (EMITTER_SLOTS - spec.count + 1));
  spec.nulls = (random->Next() % 3 == 0 ? 1 + random->Next() % 8 : 0);
  spec.mode = (random->Next() % 10 < 4 ? 0 : 1 + random->Next() % 4);
  if (spec.mode == 4 && spec.count - spec.start < EMITTER_AHEAD + 3) spec.mode = 0;  // room for the hole's two slots below the count
  spec.trigger = 1 + random->Next() % (spec.mode == 4 ? spec.count - spec.start - EMITTER_AHEAD - 2 : spec.count - spec.start);
  spec.new_extent = spec.count + random->Next() % (EMITTER_SLOTS - spec.count + 1);
  spec.new_size = random->Next() % (2 * EMITTER_SLOTS);
  return spec;
}

// Fills `world` (every byte at random, then the structure) for `spec`; `ends` are two blocks' ends (LoopBlockEnd): the array is placed to end exactly there,
// so a load of a slot past its room crashes the test. Slots the loop can never reach (below the start, past the count) hold garbage: small numbers,
// no-access and wrapping addresses, the no-access page itself; the slots it reads hold null or an emitter. The registers, flags, xmm and frame are
// random; `target` is left to the run.
void EmitterSetup(EmitterWorld* world, const std::array<uint8_t*, 2>& ends, const EmitterSpec& spec, Random* random) {
  constexpr std::array<uint32_t, 6> GARBAGE = {0x10, 0xDEAD0000, 0xFFFFFFF0, 0xFFFFFFC0, 0x7FFF0000, 1};
  for (size_t at = 0; at + 4 <= sizeof(EmitterWorld); at += 4) Write32(reinterpret_cast<uint8_t*>(world) + at, random->Next());
  for (uint32_t index = 0; index < EMITTER_SLOTS; ++index) {
    uint8_t* emitter = world->emitters[index];
    Write32(emitter + 8, index);
    emitter[0x6E] = static_cast<uint8_t>(random->Next() & 1);
    Write32(emitter + 0x58, (emitter[0x6E] != 0 ? Address(world->definitions[random->Next() % EMITTER_SLOTS])
                                                : ((random->Next() & 3) == 0 ? Address(ends[0]) + (random->Next() & 0xC0) : GARBAGE[random->Next() % GARBAGE.size()])));
  }
  const auto fill_array = [&](uint8_t* end, uint32_t room) {
    const uint32_t array = Address(end) - 8 * room;
    for (uint32_t slot = 0; slot < room; ++slot) {
      uint32_t value = ((random->Next() & 7) == 0 ? Address(end) + (random->Next() & 0xC0) : GARBAGE[random->Next() % GARBAGE.size()]);
      if (slot >= spec.start && slot < spec.count) {
        value = ((random->Next() & 7) < spec.nulls ? 0 : Address(world->emitters[random->Next() % EMITTER_SLOTS]));
      }
      Write32(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(array)) + 8 * slot, value);
      Write32(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(array)) + 8 * slot + 4, random->Next());
    }
    return array;
  };
  const uint32_t array = fill_array(ends[0], spec.extent);
  const bool another_array = (spec.mode == 1 || spec.mode == 2);
  const uint32_t new_array = (another_array ? fill_array(ends[1], spec.new_extent) : array);
  Write32(world->container + 4, spec.count);
  Write32(world->container + 8, array);
  uint32_t new_size = spec.count;
  if (spec.mode == 1) new_size = spec.new_extent;
  if (spec.mode == 3) new_size = spec.new_size;
  const uint32_t hole_slot = spec.start + spec.trigger + EMITTER_AHEAD;  // the first slot the stub reads after update `trigger`
  if (spec.mode == 4) new_size = hole_slot;
  world->context = {.trigger = (spec.mode != 0 ? spec.trigger : 0), .container = Address(world->container), .new_size = new_size, .new_array = new_array};
  if (spec.mode == 4) {
    const auto* hole = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(array + 8 * hole_slot));
    world->context.restore = spec.trigger + 1;
    world->context.hole = Address(hole);
    world->context.saved = {Read32(hole), Read32(hole + 8)};
    world->context.restore_size = spec.count;
    world->context.poison = Address(ends[1]);
  }
  for (uint32_t& value : world->call.in) value = random->Next();
  world->call.in[3] = Address(world->container);
  world->call.in[4] = spec.start;
  world->call.in[5] = Address(&world->context);
  uint8_t* esp = world->call.frame + 0x100;
  Write32(esp + 0x20, world->call.in[3]);  // the slots the loop reloads ebx, ebp, eax and edi from after a round
  Write32(esp + 0x24, world->call.in[6]);
  Write32(esp + 0x28, world->call.in[0]);
  Write32(esp + 0x2C, world->call.in[5]);
  Write32(esp + 0x34, spec.count);
  world->call.flags_in = (random->Next() & 0x8D5) | 2;
}

// One run of the loop at `target` from `backup`, left in `out`.
void RunEmitterLoop(EmitterWorld* world, const EmitterWorld& backup, uint32_t target, EmitterWorld* out) {
  *world = backup;
  world->call.target = target;
  EnterParticleLoop(&world->call);
  *out = *world;
}

// A loop entered by itself with an access violation caught: false if it faulted. The call runs on the thread's own stack (in an arena with room below
// it for the exception record): the exception dispatch fails fast when esp is outside the stack, as it is on the heap's.
bool EnterParticleLoopGuarded(LoopCall* call) {
  alignas(16) uint8_t arena[0x1000];
  static_assert(0x600 + sizeof(LoopCall) <= sizeof(arena));
  auto* on_stack = reinterpret_cast<LoopCall*>(arena + 0x600);
  std::memcpy(on_stack, call, sizeof(LoopCall));
  __try {
    EnterParticleLoop(on_stack);
  } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
    return false;
  }
  std::memcpy(call, on_stack, sizeof(LoopCall));
  call->esp_after += Address(call) - Address(on_stack);  // as an offset into the caller's own call
  return true;
}

// The same with an access violation caught: false if the loop faulted (`out` is then not meaningful).
bool RunEmitterLoopGuarded(EmitterWorld* world, const EmitterWorld& backup, uint32_t target, EmitterWorld* out) {
  *world = backup;
  world->call.target = target;
  if (!EnterParticleLoopGuarded(&world->call)) return false;
  *out = *world;
  return true;
}

// What differs between two runs ("" if nothing): every register but ecx and edx (the stub's, dead at the site), esp, the flags, xmm0-7, the container, the
// emitters, the definitions and the context, the frame (a dword that differs by exactly the distance between the two copies of the DLL is a return address a
// call left behind).
const char* EmitterDiff(const EmitterWorld& a, const EmitterWorld& b, uint32_t copy_distance) {
  for (const size_t reg : {0, 3, 4, 5, 6}) {
    if (a.call.out[reg] != b.call.out[reg]) return "registers";
  }
  if (a.call.esp_after != b.call.esp_after) return "esp";
  if (a.call.flags_out != b.call.flags_out) return "flags";
  if (std::memcmp(a.call.xmm_out, b.call.xmm_out, sizeof(a.call.xmm_out)) != 0) return "xmm";
  if (std::memcmp(a.container, b.container, sizeof(a.container)) != 0 || std::memcmp(a.emitters, b.emitters, sizeof(a.emitters)) != 0
      || std::memcmp(a.definitions, b.definitions, sizeof(a.definitions)) != 0 || std::memcmp(&a.context, &b.context, sizeof(a.context)) != 0) {
    return "memory (the container, the emitters, the definitions or the context)";
  }
  for (size_t at = 0; at < sizeof(a.call.frame); at += 4) {
    const uint32_t x = Read32(a.call.frame + at);
    const uint32_t y = Read32(b.call.frame + at);
    if (x != y && y - x != copy_distance) return "stack";
  }
  return "";
}

// The loop's bottom by itself, in two blocks that end at the harness: which way it went is recorded by the exit that ran (both leave through
// ParticleLoopReturn, as the whole loop does).
uint32_t emitter_exit = 0;
uint32_t emitter_exit_return = 0;
__declspec(naked) void EmitterBlockTaken() {
  __asm {
    mov emitter_exit, 1
    jmp dword ptr [emitter_exit_return]
  }
}
__declspec(naked) void EmitterBlockFell() {
  __asm {
    mov emitter_exit, 2
    jmp dword ptr [emitter_exit_return]
  }
}

int RunEmitterTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  const uint32_t function_rva = build.code->emitter_loop_rva;
  const uint32_t site_rva = function_rva + ec::EMITTER_LOOP_SITE;
  const uint32_t copy_distance = static_cast<uint32_t>(patched - game);
  constexpr std::array<uint8_t, 5> GAME_SITE = {0x46, 0x3B, 0x74, 0x24, 0x34};  // inc esi; cmp esi, [esp + 0x34]
  emitter_exit_return = Address(reinterpret_cast<const void*>(&ParticleLoopReturn));
  uint8_t* const stub = ec::page + ec::EMITTER_PREFETCH_PAGE;
  constexpr size_t STUB_SIZE = ec::EMITTER_PREFETCH_STUB.size();
  constexpr std::array<uint8_t, 3> TOP = {0x8B, 0x4B, 0x08};   // mov ecx, [ebx + 8]
  constexpr std::array<uint8_t, 3> EXIT = {0x8B, 0x4F, 0x18};  // mov ecx, [edi + 0x18]

  // 16a. The site and the stub as Prepare made them, and the loop where the test expects it.
  CHECK(ec::sites[ec::EMITTER_PREFETCH_INDEX].at == patched + site_rva && (ec::written >> ec::EMITTER_PREFETCH_INDEX & 1) != 0);
  CHECK(std::memcmp(game + site_rva, GAME_SITE.data(), 5) == 0 && std::memcmp(ec::sites[ec::EMITTER_PREFETCH_INDEX].game.data(), GAME_SITE.data(), 5) == 0);
  CHECK(game[site_rva + 5] == 0x74);  // the je the stub goes on at
  CHECK(patched[site_rva] == 0xE9 && Address(patched + site_rva + 5) + Read32(patched + site_rva + 1) == Address(stub));
  CHECK(stub[STUB_SIZE - 5] == 0xE9 && Address(stub + STUB_SIZE) + Read32(stub + STUB_SIZE - 4) == Address(patched + site_rva + 5));
  CHECK(std::memcmp(stub, ec::EMITTER_PREFETCH_STUB.data(), STUB_SIZE - 4) == 0);
  CHECK(std::memcmp(game + function_rva + EMITTER_LOOP_TOP, TOP.data(), TOP.size()) == 0 && game[function_rva + EMITTER_LOOP_CALL] == 0xE8
        && std::memcmp(game + function_rva + EMITTER_LOOP_EXIT, EXIT.data(), EXIT.size()) == 0);
  std::printf("16. emitter loop prefetch: the site (+0x%X in the function) found and written, the stub (%zu bytes, the slots %u and %u after the next emitter's) jumps back to the game's je\n",
              static_cast<uint32_t>(ec::EMITTER_LOOP_SITE), STUB_SIZE, static_cast<uint32_t>(ec::EMITTER_PREFETCH_AHEAD), static_cast<uint32_t>(ec::EMITTER_PREFETCH_AHEAD) + 1);

  // The harness's call and way out in both copies (after Prepare: its pattern reads these bytes), put back at the end.
  std::array<uint8_t, 5> call_game = {};
  std::array<uint8_t, 5> call_ours = {};
  std::array<uint8_t, 5> exit_game = {};
  std::array<uint8_t, 5> exit_ours = {};
  std::memcpy(call_game.data(), game + function_rva + EMITTER_LOOP_CALL, 5);
  std::memcpy(call_ours.data(), patched + function_rva + EMITTER_LOOP_CALL, 5);
  std::memcpy(exit_game.data(), game + function_rva + EMITTER_LOOP_EXIT, 5);
  std::memcpy(exit_ours.data(), patched + function_rva + EMITTER_LOOP_EXIT, 5);
  for (uint8_t* image : {game, patched}) {
    JumpTo(image + function_rva + EMITTER_LOOP_CALL, reinterpret_cast<const void*>(&EmitterUpdate));
    image[function_rva + EMITTER_LOOP_CALL] = 0xE8;
    JumpTo(image + function_rva + EMITTER_LOOP_EXIT, reinterpret_cast<const void*>(&ParticleLoopReturn));
  }
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  const uint32_t game_top = Address(game + function_rva + EMITTER_LOOP_TOP);
  const uint32_t patched_top = Address(patched + function_rva + EMITTER_LOOP_TOP);
  Random random = {.state = 0x16E16};

  // 16b. The bottom alone. The game's block: inc esi; cmp esi, [esp + 0x34]; je taken; jmp fell. Ours: the stub's bytes (its jump goes on to the next
  // instruction: the same je and jmp). The vector's size ([container + 4]) is the count the loop runs to, or another one (below, at or above it).
  {
    uint8_t* blocks = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    CHECK(blocks != nullptr);
    uint8_t* game_block = blocks;
    uint8_t* ours_block = blocks + 0x40;
    const auto tail = [&](uint8_t* at) {  // je +5 (to the taken exit); jmp fell; taken: jmp taken
      at[0] = 0x74;
      at[1] = 0x05;
      at[2] = 0xE9;
      Write32(at + 3, Address(reinterpret_cast<const void*>(&EmitterBlockFell)) - Address(at + 7));
      at[7] = 0xE9;
      Write32(at + 8, Address(reinterpret_cast<const void*>(&EmitterBlockTaken)) - Address(at + 12));
    };
    std::memcpy(game_block, GAME_SITE.data(), 5);
    tail(game_block + 5);
    std::memcpy(ours_block, stub, STUB_SIZE - 4);
    Write32(ours_block + STUB_SIZE - 4, 0);
    tail(ours_block + STUB_SIZE);
    FlushInstructionCache(GetCurrentProcess(), blocks, 0x1000);
    auto* arrays = LoopBlockEnd();
    alignas(16) std::array<uint8_t, 0x20> container = {};
    alignas(16) std::array<uint8_t, EMITTER_SLOTS * EMITTER_BYTES> emitters = {};
    for (size_t at = 0; at + 4 <= emitters.size(); at += 4) Write32(emitters.data() + at, random.Next());
    auto a = std::make_unique<LoopCall>();
    auto b = std::make_unique<LoopCall>();
    CHECK(Address(a.get()) % 16 == 0 && Address(b.get()) % 16 == 0);
    uint64_t runs = 0;
    uint64_t loads = 0;
    uint64_t wrong = 0;
    uint64_t taken = 0;
    const auto place = [&](uint32_t count, uint32_t live) {  // `count` slots (null or an emitter), the array ending where the next page is no access
      for (uint32_t slot = 0; slot < count; ++slot) {
        Write32(arrays - 8 * count + 8 * slot, ((random.Next() & 3) == 0 ? 0 : Address(emitters.data() + EMITTER_BYTES * (random.Next() % EMITTER_SLOTS))));
        Write32(arrays - 8 * count + 8 * slot + 4, random.Next());
      }
      Write32(container.data() + 4, live);
      Write32(container.data() + 8, Address(arrays) - 8 * count);
    };
    const auto start = [&](LoopCall* call, uint32_t count, uint32_t esi) {  // every register random, esi, the container and the count set
      std::memset(call, 0, sizeof(*call));
      for (uint32_t& value : call->in) value = random.Next();
      call->in[3] = Address(container.data());
      call->in[4] = esi;
      call->flags_in = (random.Next() & 0x8D5) | 2;
      Write32(call->frame + 0x100 + 0x20, Address(container.data()));
      Write32(call->frame + 0x100 + 0x34, count);
    };
    // The stub's ecx and edx (the harness's input edx is `edx_in`): with the slot K + 1 after the next emitter's, `far_slot` (the lea's value), not below
    // the count the loop runs to ecx is that index and edx the input's; below it but not below the size the vector has, ecx is that index and edx the
    // container; else ecx is the pointer in that slot and edx the elements.
    struct Registers {
      uint32_t ecx;
      uint32_t edx;
    };
    const auto expected = [&](uint32_t count, uint32_t live, uint32_t esi, uint32_t edx_in) -> Registers {
      const uint32_t far_slot = esi + 2 + ec::EMITTER_PREFETCH_AHEAD;
      if (far_slot >= count) return {far_slot, edx_in};
      if (far_slot >= live) return {far_slot, Address(container.data())};
      const uint32_t elements = Address(arrays) - 8 * count;
      return {Read32(reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(elements + 8 * far_slot))), elements};
    };
    const auto run_one = [&](uint32_t count, uint32_t live, uint32_t esi) {
      place(count, live);
      start(a.get(), count, esi);
      *b = *a;
      emitter_exit = 0;
      a->target = Address(game_block);
      EnterParticleLoop(a.get());
      const uint32_t exit_a = emitter_exit;
      emitter_exit = 0;
      b->target = Address(ours_block);
      EnterParticleLoop(b.get());
      const uint32_t exit_b = emitter_exit;
      const Registers registers = expected(count, live, esi, b->in[2]);
      const char* difference = [&] {
        for (const size_t reg : {0, 3, 4, 5, 6}) {
          if (a->out[reg] != b->out[reg]) return "registers";
        }
        if (b->out[1] != registers.ecx || b->out[2] != registers.edx) return "ecx or edx";
        if (a->flags_out != b->flags_out) return "flags";
        if (a->esp_after - Address(a.get()) != b->esp_after - Address(b.get())) return "esp";  // an offset into each run's own call
        if (exit_a != exit_b || exit_a == 0) return "way out";
        return (std::memcmp(a->frame, b->frame, sizeof(a->frame)) != 0 ? "frame" : "");
      }();
      ++runs;
      taken += (exit_a == 1 ? 1 : 0);
      loads += (static_cast<uint32_t>(esi + 2 + ec::EMITTER_PREFETCH_AHEAD) < std::min(count, live) ? 1 : 0);
      if (difference[0] != 0) {
        ++wrong;
        Report("  emitter bottom: esi %08X count %u size %u: %s differs (exit %u / %u, flags %04X / %04X)\n", esi, count, live, difference, exit_a, exit_b, a->flags_out,
               b->flags_out);
      }
    };
    // Every esi around every count with the size as the count, one more or one less than the slot the stub reads and 0, then the edges of the address space
    // and random ones with a random size half of the time.
    for (uint32_t count = 0; count <= EMITTER_SLOTS; ++count) {
      for (uint32_t esi = 0; esi <= count + 6; ++esi) {
        for (const uint32_t live : {count, esi + 2 + ec::EMITTER_PREFETCH_AHEAD, esi + 3 + ec::EMITTER_PREFETCH_AHEAD, esi + 1 + ec::EMITTER_PREFETCH_AHEAD, 0u}) {
          run_one(count, live, esi);
        }
      }
      // esi = 0xFFFFFFFE is the one value where `lea ecx, [esi + 2]` wraps to 0 (both bounds pass, slot K reads one slot below the array): the loop never has it
      // (esi stays at or below the count, under 2^29), and no run here can show it, the page below the array being readable.
      for (uint32_t esi = 0xFFFFFFE0u; esi != 0; ++esi) run_one(count, count, esi);
      for (uint32_t esi = 0x7FFFFFF0u; esi < 0x80000010u; ++esi) run_one(count, count, esi);
    }
    const int random_runs = (quick ? 1'000'000 : 5'000'000);
    for (int run = 0; run < random_runs; ++run) {
      const uint32_t count = random.Next() % (EMITTER_SLOTS + 1);
      const uint32_t live = ((random.Next() & 1) != 0 ? count : random.Next() % (count + 4));
      run_one(count, live, (random.Next() & 3) == 0 ? random.Next() : random.Next() % (count + 8));
    }
    CHECK(wrong == 0 && loads > runs / 8 && taken > runs / 64 && taken < runs - runs / 8);  // the je is taken only at esi == count
    std::printf("   the loop's bottom alone: %llu runs (%llu with a slot to load, %llu taking the je): every register but ecx and edx, the flags, esp, the frame and the way out "
                "equal, ecx and edx as the stub's path leaves them; no load past the vector (the array ends at a no-access page)\n", runs, loads, taken);
    // CONTROLs: each bound moved or removed must be seen (a fault, or a different ecx or edx) in every run it matters in. With `ja` for `jae` the stub
    // reads the slot at the bound itself: the slot after the last one when the bound is the count (the array's page ends there: a fault), a slot inside the
    // array when it is the size; with the jump removed it goes on in every run the bound would have stopped.
    {
      struct Mutant {
        const char* what;
        size_t at;
        size_t size;
        std::array<uint8_t, 2> bytes;
        bool (*matters)(uint32_t far_slot, uint32_t count, uint32_t live);
      };
      constexpr size_t COUNT_BOUND = 8;  // jae skip after cmp ecx, [esp + 0x34]
      constexpr size_t LIVE_BOUND = 17;  // jae skip after cmp ecx, [edx + 4]
      CHECK(stub[COUNT_BOUND] == 0x73 && stub[LIVE_BOUND] == 0x73);
      const std::array<Mutant, 4> mutants = {{{"the count bound `ja` for `jae`", COUNT_BOUND, 1, {0x77, 0}, [](uint32_t far_slot, uint32_t count, uint32_t) { return far_slot == count; }},
                                              {"no count bound", COUNT_BOUND, 2, {0x90, 0x90}, [](uint32_t far_slot, uint32_t count, uint32_t) { return far_slot >= count; }},
                                              {"the live-size bound `ja` for `jae`", LIVE_BOUND, 1, {0x77, 0},
                                               [](uint32_t far_slot, uint32_t count, uint32_t live) { return far_slot < count && far_slot == live; }},
                                              {"no live-size bound", LIVE_BOUND, 2, {0x90, 0x90},
                                               [](uint32_t far_slot, uint32_t count, uint32_t live) { return far_slot < count && far_slot >= live; }}}};
      for (const Mutant& mutant : mutants) {
        std::array<uint8_t, 2> original = {};
        std::memcpy(original.data(), ours_block + mutant.at, mutant.size);
        std::memcpy(ours_block + mutant.at, mutant.bytes.data(), mutant.size);
        FlushInstructionCache(GetCurrentProcess(), ours_block, STUB_SIZE);
        uint64_t mattering = 0;
        uint64_t seen = 0;
        for (uint32_t count = 0; count <= EMITTER_SLOTS; ++count) {
          for (uint32_t esi = 0; esi <= count + 6; ++esi) {
            const uint32_t far_slot = esi + 2 + ec::EMITTER_PREFETCH_AHEAD;
            for (const uint32_t live : {count, far_slot, far_slot + 1, 0u, count + 3}) {
              if (!mutant.matters(far_slot, count, live)) continue;
              place(count, live);
              start(b.get(), count, esi);
              b->target = Address(ours_block);
              const uint32_t edx_in = b->in[2];
              ++mattering;
              if (!EnterParticleLoopGuarded(b.get())) {
                ++seen;
              } else {
                const Registers registers = expected(count, live, esi, edx_in);
                seen += (b->out[1] != registers.ecx || b->out[2] != registers.edx) ? 1 : 0;
              }
            }
          }
        }
        std::memcpy(ours_block + mutant.at, original.data(), mutant.size);
        FlushInstructionCache(GetCurrentProcess(), ours_block, STUB_SIZE);
        std::printf("   CONTROL, %s: seen in %llu of the %llu runs it matters in (all)\n", mutant.what, seen, mattering);
        CHECK(mattering > 100 && seen == mattering);
      }
      CHECK(std::memcmp(ours_block, stub, STUB_SIZE - 4) == 0);
    }
    VirtualFree(arrays - 0x1000, 0, MEM_RELEASE);
    VirtualFree(blocks, 0, MEM_RELEASE);
  }

  // 16c. The whole loop in both copies.
  auto world = std::make_unique<EmitterWorld>();
  auto backup = std::make_unique<EmitterWorld>();
  auto result_a = std::make_unique<EmitterWorld>();
  auto result_b = std::make_unique<EmitterWorld>();
  CHECK(Address(world.get()) % 16 == 0 && Address(backup.get()) % 16 == 0 && Address(result_a.get()) % 16 == 0 && Address(result_b.get()) % 16 == 0);
  const std::array<uint8_t*, 2> ends = {LoopBlockEnd(), LoopBlockEnd()};
  {
    std::array<uint64_t, 5> modes = {};
    std::array<uint64_t, 5> wrong_modes = {};
    uint64_t not_run = 0;  // plain and hole vectors whose update count is not the number of emitters from the start: the harness did not run the loop
    uint64_t prefetching = 0;
    uint64_t with_nulls = 0;
    uint64_t with_garbage_after = 0;
    const auto compare = [&](const EmitterSpec& spec) {
      EmitterSetup(world.get(), ends, spec, &random);
      *backup = *world;
      RunEmitterLoop(world.get(), *backup, game_top, result_a.get());
      RunEmitterLoop(world.get(), *backup, patched_top, result_b.get());
      ++modes[spec.mode];
      not_run += ((spec.mode == 0 || spec.mode == 4) && result_a->context.calls != spec.count - spec.start ? 1 : 0);
      prefetching += (spec.start + 2 + ec::EMITTER_PREFETCH_AHEAD < spec.count ? 1 : 0);
      with_nulls += (spec.nulls != 0 ? 1 : 0);
      with_garbage_after += (spec.extent > spec.count ? 1 : 0);
      if (const char* what = EmitterDiff(*result_a, *result_b, copy_distance); what[0] != 0) {
        ++wrong_modes[spec.mode];
        Report("  emitter loop: count %u start %u mode %u trigger %u: %s differs\n", spec.count, spec.start, spec.mode, spec.trigger, what);
      }
    };
    for (uint32_t count = 1; count <= 12; ++count) {  // every case of a small vector
      for (const uint32_t start : {0u, count / 2}) {
        for (const uint32_t nulls : {0u, 3u, 8u}) {
          for (const uint32_t extent : {count, count + 3}) {
            compare({.count = count, .start = start, .extent = extent, .nulls = nulls, .mode = 0, .trigger = 0, .new_extent = count, .new_size = count});
            for (uint32_t trigger = 1; trigger <= count - start; ++trigger) {
              for (uint32_t room = count; room <= count + 4; room += 2) {
                compare({.count = count, .start = start, .extent = extent, .nulls = nulls, .mode = 1, .trigger = trigger, .new_extent = room, .new_size = room});
                compare({.count = count, .start = start, .extent = extent, .nulls = nulls, .mode = 2, .trigger = trigger, .new_extent = room, .new_size = count});
              }
              for (const uint32_t size : {0u, 1u, count, 2 * count + 5}) {
                compare({.count = count, .start = start, .extent = extent, .nulls = nulls, .mode = 3, .trigger = trigger, .new_extent = count, .new_size = size});
              }
              if (trigger + EMITTER_AHEAD + 2 <= count - start) {  // the hole: the two slots after update `trigger`'s stub reads lie below the count
                compare({.count = count, .start = start, .extent = extent, .nulls = nulls, .mode = 4, .trigger = trigger, .new_extent = count, .new_size = count});
              }
            }
          }
        }
      }
    }
    const int runs = (quick ? 200'000 : 1'000'000);
    for (int run = 0; run < runs; ++run) compare(EmitterRandomSpec(&random));
    CHECK(wrong_modes[0] == 0 && wrong_modes[1] == 0 && wrong_modes[2] == 0 && wrong_modes[3] == 0 && wrong_modes[4] == 0 && not_run == 0);
    CHECK(prefetching > 20'000 && with_nulls > 20'000 && with_garbage_after > 20'000 && modes[4] > 20'000);
    std::printf("   the whole loop: %llu vectors (%llu plain, %llu with a new array (grown or not), %llu with only the size changed, %llu with a hole (the next two slots outside "
                "the size, no-access pointers until the next call); %llu long enough to prefetch, %llu with null slots, %llu with garbage past the count): every register but "
                "ecx and edx, the flags, esp, xmm0-7, the container, the emitters, the definitions, the frame and the call order equal\n",
                modes[0] + modes[1] + modes[2] + modes[3] + modes[4], modes[0], modes[1] + modes[2], modes[3], modes[4], prefetching, with_nulls, with_garbage_after);
  }

  // 16d. CONTROLs: the stub with each of six changes must be seen (plain vectors, and vectors with a hole for the
  // live-size bound; a load of a wrong slot or through a wrong pointer is seen as a fault, the ones that write or store
  // as a difference).
  {
    DWORD protection;
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection) != FALSE);
    struct Change {
      const char* what;
      size_t at;
      std::array<uint8_t, 4> bytes;
      size_t size;
      uint32_t nulls;
      uint32_t mode;  // 0 a plain vector, 4 a vector with a hole
    };
    const std::array<Change, 6> changes = {{{"writes eax (lea eax, [esi + K + 1])", 2, {0x46}, 1, 0, 0},
                                            {"ends with the wrong compare (cmp esi, esi)", 52, {0x3B, 0xF6, 0x90, 0x90}, 4, 0, 0},
                                            {"stores (mov [esp - 0x40], ecx for a prefetch)", 44, {0x89, 0x4C, 0x24, 0xC0}, 4, 0, 0},
                                            {"has no null test (je lines removed)", 28, {0x90, 0x90}, 2, 8, 0},
                                            {"has no live-size bound (jae skip removed)", 17, {0x90, 0x90}, 2, 0, 4},
                                            {"reads ebx for the container (mov edx, ebx)", 10, {0x89, 0xDA, 0x90, 0x90}, 4, 0, 0}}};
    for (const Change& change : changes) {
      std::array<uint8_t, 4> original = {};
      std::memcpy(original.data(), stub + change.at, change.size);
      std::memcpy(stub + change.at, change.bytes.data(), change.size);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      int seen = 0;
      int faulted = 0;
      constexpr int CONTROL_RUNS = 5000;
      for (int run = 0; run < CONTROL_RUNS; ++run) {
        EmitterSpec spec = EmitterRandomSpec(&random);
        spec.count = 6 + random.Next() % (EMITTER_SLOTS - 5);
        spec.start = 0;
        spec.extent = spec.count;
        spec.nulls = change.nulls;
        spec.mode = change.mode;
        spec.trigger = (change.mode == 4 ? 1 + random.Next() % (spec.count - EMITTER_AHEAD - 2) : 0);
        EmitterSetup(world.get(), ends, spec, &random);
        *backup = *world;
        RunEmitterLoop(world.get(), *backup, game_top, result_a.get());
        const bool completed = RunEmitterLoopGuarded(world.get(), *backup, patched_top, result_b.get());
        faulted += (completed ? 0 : 1);
        seen += (!completed || EmitterDiff(*result_a, *result_b, copy_distance)[0] != 0 ? 1 : 0);
      }
      std::memcpy(stub + change.at, original.data(), change.size);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      std::printf("   CONTROL, a stub that %s: %d of %d runs seen as different (%d of them a fault)\n", change.what, seen, CONTROL_RUNS, faulted);
      CHECK(seen == CONTROL_RUNS);
    }
    CHECK(std::memcmp(stub, ec::EMITTER_PREFETCH_STUB.data(), STUB_SIZE - 4) == 0);
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READ, &protection) != FALSE);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }

  // 16e. The prefetch row on and off 200 times while 4 threads run the loop in both copies on their own vectors.
  {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> loops = 0;
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        auto thread_world = std::make_unique<EmitterWorld>();
        auto thread_backup = std::make_unique<EmitterWorld>();
        auto thread_a = std::make_unique<EmitterWorld>();
        auto thread_b = std::make_unique<EmitterWorld>();
        const std::array<uint8_t*, 2> thread_ends = {LoopBlockEnd(), LoopBlockEnd()};
        Random thread_random = {.state = 0xE00D00ull + static_cast<uint64_t>(worker)};
        while (!stop.load()) {
          EmitterSetup(thread_world.get(), thread_ends, EmitterRandomSpec(&thread_random), &thread_random);
          *thread_backup = *thread_world;
          // The inputs and outputs are in the world: repeated runs keep the thread inside the loop and its stub most of the time.
          for (int repeat = 0; repeat < 8; ++repeat) {
            RunEmitterLoop(thread_world.get(), *thread_backup, game_top, thread_a.get());
            RunEmitterLoop(thread_world.get(), *thread_backup, patched_top, thread_b.get());
            if (EmitterDiff(*thread_a, *thread_b, copy_distance)[0] != 0) ++bad;
            ++loops;
          }
        }
        VirtualFree(thread_ends[0] - 0x1000, 0, MEM_RELEASE);
        VirtualFree(thread_ends[1] - 0x1000, 0, MEM_RELEASE);
      });
    }
    int busy = 0;
    int never = 0;
    bool on = true;
    for (int switches = 0; switches < 200; ++switches) {
      on = !on;
      int tries = 0;
      do {
        twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, on);
        ++tries;
      } while (((ec::written >> ec::EMITTER_PREFETCH_INDEX & 1) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
      never += ((ec::written >> ec::EMITTER_PREFETCH_INDEX & 1) != 0) != on ? 1 : 0;
    }
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("   200 switches while 4 threads ran the loop %llu times in each copy: %llu different (%d writes waited for a thread inside a site, %d switches never reached)\n", loops.load(),
                bad.load(), busy, never);
    CHECK(bad.load() == 0 && never == 0 && loops.load() > 0 && (ec::written >> ec::EMITTER_PREFETCH_INDEX & 1) != 0);
  }

  // A parked thread whose saved Eip is each byte of the site (and the byte before, the bytes after): the row switched, the site changes only with Eip at
  // its first byte or outside it.
  {
    ParkedThread parked;
    HANDLE thread = parked.Open();
    CHECK(thread != nullptr);
    const uint8_t* site = patched + site_rva;
    bool on = (ec::written >> ec::EMITTER_PREFETCH_INDEX & 1) != 0;
    CHECK(on);
    int wrong = 0;
    for (int offset = -1; offset <= 6; ++offset) {
      CHECK(SuspendThread(thread) != static_cast<DWORD>(-1));
      CONTEXT saved = {.ContextFlags = CONTEXT_CONTROL};
      CHECK(GetThreadContext(thread, &saved) != FALSE);
      CONTEXT moved = saved;
      moved.Eip = Address(site + offset);
      CHECK(SetThreadContext(thread, &moved) != FALSE);
      const ec::SiteMask before = ec::written;
      twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, !on);
      const bool changed = ((ec::written ^ before) >> ec::EMITTER_PREFETCH_INDEX & 1) != 0;
      wrong += (changed != !(offset > 0 && offset < 5)) ? 1 : 0;
      if (changed) on = !on;
      CHECK(SetThreadContext(thread, &saved) != FALSE);
      ResumeThread(thread);
    }
    for (int tries = 0; !on && tries < static_cast<int>(ec::MAX_FAILED_WRITES); ++tries) {
      twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
      on = (ec::written >> ec::EMITTER_PREFETCH_INDEX & 1) != 0;
    }
    parked.Stop();
    CloseHandle(thread);
    CHECK(wrong == 0 && on);
    std::printf("   a parked thread at the site -1 .. +6: the site is refused at +1 .. +4 and written at the others\n");
  }

  // 16f. Timing. Emitters of 256 bytes at 8 mod 16 (as the heap places them: the lines of +0x58 and +0x70 move with it) in 256 MB, each one's definition
  // (128 bytes, also 8 mod 16) at random in another 128 MB, vectors of `per_call` emitters picked at random (a fresh set for every run, so no variant finds
  // what another left in the cache); the update loads the emitter's words at +0x14, +0x48 and +0x70 and its definition's word and multiplies
  // `emitter_busy` times. The game's copy and ours (the two stages, K = 1) in the two images, and copies of the function without a stub, with the stub
  // that takes one slot and loads its definition at once (ONE_STAGE_STUB) and with the two stages for K = 2 and 3; ns an emitter, the median of 7 rounds
  // taking turns.
  {
    constexpr size_t POOL = 1u << 20;
    constexpr size_t PICKS = 1u << 16;
    constexpr int ROUNDS = 7;
    constexpr size_t VARIANTS = 6;  // game, ours, the copy without the stub, one stage, two stages K = 2, K = 3
    auto* emitter_pool = static_cast<uint8_t*>(VirtualAlloc(nullptr, POOL * 256, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* definition_pool = static_cast<uint8_t*>(VirtualAlloc(nullptr, POOL * 128, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECK(emitter_pool != nullptr && definition_pool != nullptr);
    const auto emitter_at = [&](size_t index) { return emitter_pool + index * 256 + 8 + 16 * (index & 3); };
    const auto definition_at = [&](size_t index) { return definition_pool + index * 128 + 8 + 16 * (index & 3); };
    std::vector<uint32_t> permutation(POOL);
    for (size_t index = 0; index < POOL; ++index) permutation[index] = static_cast<uint32_t>(index);
    for (size_t index = 0; index < POOL; ++index) {
      std::swap(permutation[index], permutation[index + random.Next() % (POOL - index)]);
      Write32(emitter_at(index) + 0x58, Address(definition_at(permutation[index])));
      Write32(definition_at(index) + 0x2C, random.Next());
    }
    // Scratch copies of the function: its bytes, the call at the busy update, the way out at the harness, the stub behind.
    auto* scratch = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    CHECK(scratch != nullptr);
    const auto point_at_busy = [&](uint8_t* function) {
      JumpTo(function + EMITTER_LOOP_CALL, reinterpret_cast<const void*>(&EmitterUpdateBusy));
      function[EMITTER_LOOP_CALL] = 0xE8;
    };
    for (uint8_t* image : {game, patched}) point_at_busy(image + function_rva);
    std::array<uint32_t, VARIANTS> targets = {game_top, patched_top, 0, 0, 0, 0};
    for (size_t variant = 2; variant < VARIANTS; ++variant) {
      uint8_t* copy = scratch + 0x200 * (variant - 2);
      std::memcpy(copy, game + function_rva, EMITTER_FUNCTION_SIZE);
      point_at_busy(copy);
      JumpTo(copy + EMITTER_LOOP_EXIT, reinterpret_cast<const void*>(&ParticleLoopReturn));
      if (variant > 2) {  // the site jumps to the stub, whose jump goes on at the je
        const auto two_stages = ec::EmitterPrefetchStub(static_cast<uint8_t>(variant - 2));
        const std::span<const uint8_t> code = (variant == 3 ? std::span<const uint8_t>(ONE_STAGE_STUB) : std::span<const uint8_t>(two_stages));
        uint8_t* ahead_code = copy + 0x140;
        std::memcpy(ahead_code, code.data(), code.size());
        Write32(ahead_code + code.size() - 4, Address(copy + ec::EMITTER_LOOP_SITE + 5) - Address(ahead_code + code.size()));
        JumpTo(copy + ec::EMITTER_LOOP_SITE, ahead_code);
      }
      targets[variant] = Address(copy + EMITTER_LOOP_TOP);
    }
    FlushInstructionCache(GetCurrentProcess(), scratch, 0x1000);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    std::vector<uint32_t> elements(2 * PICKS);
    alignas(16) std::array<uint8_t, 0x20> container = {};
    auto call = std::make_unique<LoopCall>();
    CHECK(Address(call.get()) % 16 == 0);
    call->in[3] = Address(container.data());
    call->flags_in = 2;
    Write32(call->frame + 0x100 + 0x20, Address(container.data()));
    // `PICKS` emitters in vectors of `per_call` each (a different random set every call), or the same 64 in every vector (hot).
    const auto run_vectors = [&](size_t variant, bool hot, uint32_t per_call) {
      for (size_t index = 0; index < PICKS; ++index) {
        std::swap(permutation[index], permutation[index + random.Next() % (POOL - index)]);
        elements[2 * index] = Address(emitter_at(permutation[hot ? index % 64 : index]));
      }
      const size_t vectors = PICKS / per_call;
      const auto start = std::chrono::steady_clock::now();
      for (size_t vector = 0; vector < vectors; ++vector) {
        Write32(container.data() + 4, per_call);
        Write32(container.data() + 8, Address(elements.data() + 2 * vector * per_call));
        Write32(call->frame + 0x100 + 0x34, per_call);
        call->target = targets[variant];
        EnterParticleLoop(call.get());
      }
      return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / static_cast<double>(vectors * per_call);
    };
    std::string lines;
    // An update of 1 multiply is nothing but the loads; 100 is about 300 cycles; 1000 about 3000, nearer a real update.
    for (const auto& [busy, per_call] : {std::pair{1u, 64u}, std::pair{100u, 64u}, std::pair{1000u, 64u}, std::pair{100u, 16u}, std::pair{1000u, 16u}, std::pair{100u, 4u}}) {
      emitter_busy = busy;
      std::array<std::vector<double>, VARIANTS> ns;
      for (int round = 0; round < ROUNDS; ++round) {
        for (size_t variant = 0; variant < VARIANTS; ++variant) ns[variant].push_back(run_vectors(variant, false, per_call));
      }
      std::array<double, VARIANTS> median = {};
      for (size_t variant = 0; variant < VARIANTS; ++variant) {
        std::sort(ns[variant].begin(), ns[variant].end());
        median[variant] = ns[variant][ROUNDS / 2];
      }
      const double hot = run_vectors(2, true, 64);
      lines += std::format("   ns an emitter, update of {} multiplies (alone, hot: {:.1f}), vectors of {}: game {:.1f}, ours (two stages, K = {}) {:.1f}, copies: no stub {:.1f}, "
                           "one stage {:.1f}, two stages K=2 {:.1f}, K=3 {:.1f}\n",
                           busy, hot, per_call, median[0], static_cast<uint32_t>(ec::EMITTER_PREFETCH_AHEAD), median[1], median[2], median[3], median[4], median[5]);
    }
    std::fputs(lines.c_str(), stdout);
    VirtualFree(emitter_pool, 0, MEM_RELEASE);
    VirtualFree(definition_pool, 0, MEM_RELEASE);
    VirtualFree(scratch, 0, MEM_RELEASE);
  }
  VirtualFree(ends[0] - 0x1000, 0, MEM_RELEASE);
  VirtualFree(ends[1] - 0x1000, 0, MEM_RELEASE);
  std::memcpy(game + function_rva + EMITTER_LOOP_CALL, call_game.data(), 5);
  std::memcpy(patched + function_rva + EMITTER_LOOP_CALL, call_ours.data(), 5);
  std::memcpy(game + function_rva + EMITTER_LOOP_EXIT, exit_game.data(), 5);
  std::memcpy(patched + function_rva + EMITTER_LOOP_EXIT, exit_ours.data(), 5);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  return 0;
}
