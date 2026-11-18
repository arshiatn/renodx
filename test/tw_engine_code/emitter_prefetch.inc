// ====================================================================================================================
// 16. The emitter update loop's prefetch (the effect update +0x1893330, its serial arm's loop +0x18933c0 .. +0x18933e5, site +0x18933ce; Thrones
// function +0x1908690; Faster Effect Prefetch; engine_code.hpp 11.), included inside tests.cpp's anonymous namespace after particle_prefetch.inc (LoopCall,
// EnterParticleLoop, ParticleLoopReturn, LoopBlockEnd and EnterSumBlock's friends are that test's: the loop is entered by itself on a fake frame at
// its top, its exit +0x18933eb hooked to ParticleLoopReturn, its call of the emitter update +0x1893470 pointed at a fake). The loop calls the update with the
// emitter [[container + 8] + i*8], i from esi while `inc esi; cmp esi, [esp + 0x34]; je the way out` (the container's address is the slot [esp + 0x20] it
// reloads ebx from); the stub at the `inc esi` takes the emitter K = 1 after the next one and prefetches its first two lines and its definition's.
// a. the site and the stub as Prepare made them (jumps, bytes);
// b. the loop's bottom alone (the game's two instructions and a copy of the stub, both exits to the harness): every esi around the count 0 to 48, esi at
//    the edges of the address space, random registers, null slots, an array that ends where the next page is no access (a load past the vector crashes
//    the test), every register but ecx, the flags, esp and which way it went equal; CONTROL: `ja` for `jae` must fault at the edge;
// c. the whole loop in both copies of the DLL: random vectors of 1 to 48 emitters (null slots among them, the emitters' definition pointers garbage or
//    valid) with a fake update that counts its calls, hashes their order, stores into its emitter, overwrites ebx edi ebp (which the game's callee keeps:
//    nothing may depend on them) and may change the vector at one call (a new array, grown or not, or only the size field), the array ending at a
//    no-access page, garbage in the slots the loop never reads (below the start, past the count): every register but ecx, the flags, esp, xmm0-7, every
//    byte of the container, the emitters, the definitions, the context and the frame (a return address apart) and the call order equal;
//    CONTROLs: a stub that writes eax, ends with the wrong compare, stores, has no null test, has the wrong bound or reads ebx for the container must each be seen;
// d. 200 switches under 4 threads that run the loop in both copies, a parked thread at each byte of the site;
// e. timing, K from 1 to 3: emitters of 256 bytes spread over 256 MB with their definitions over another 128 MB, the update loads the two lines and
//    multiplies 1 / 100 / 1000 times.
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
  uint32_t calls;      // 0: the updates so far
  uint32_t order;      // 4: a hash of the order, the emitters and what they hold
  uint32_t trigger;    // 8: the update after which the container changes (0: never)
  uint32_t container;  // 12
  uint32_t new_size;   // 16
  uint32_t new_array;  // 20
  uint32_t unused[2];  // 24
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
// its call number in the emitter (+0xC), and after call `trigger` changes the container.
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
    jne unchanged
    mov ebp, [edx + 12]
    mov edi, [edx + 16]
    mov [ebp + 4], edi
    mov edi, [edx + 20]
    mov [ebp + 8], edi
    mov eax, [edx]
  unchanged:
    imul eax, eax, -1640531535  // 0x9E3779B1
    mov ebx, 0x40
    mov edi, 0x44
    mov ebp, 0x48
    ret 0xC
  }
}

// The update of the timing: loads the emitter's first line and its definition's word, then multiplies `emitter_busy` times (3 cycles each, dependent).
uint32_t emitter_busy = 1;
__declspec(naked) void EmitterUpdateBusy() {
  __asm {
    mov eax, [ecx]
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

struct EmitterSpec {
  uint32_t count;       // the count the loop runs to ([esp + 0x34]), 0 .. 48
  uint32_t start;       // the first emitter's index (esi), below the count
  uint32_t extent;      // the elements' room in slots: count (the last slot ends where the next page is no access) .. 48 (garbage past the count)
  uint32_t nulls;       // 0 .. 8: the chance in eight that a slot the loop reads is null
  uint32_t mode;        // 0 the vector stays; 1 a new array of new_extent slots, the size new_extent (it grew); 2 a new array, the size as it was; 3 only the size changes
  uint32_t trigger;     // the update after which it changes (1 on)
  uint32_t new_extent;  // modes 1 and 2: count .. 48
  uint32_t new_size;    // mode 3
};

EmitterSpec EmitterRandomSpec(Random* random) {
  EmitterSpec spec = {.count = 1 + random->Next() % EMITTER_SLOTS};
  spec.start = ((random->Next() & 3) == 0 ? random->Next() % spec.count : 0);
  spec.extent = ((random->Next() & 1) != 0 ? spec.count : spec.count + random->Next() % (EMITTER_SLOTS - spec.count + 1));
  spec.nulls = (random->Next() % 3 == 0 ? 1 + random->Next() % 8 : 0);
  spec.mode = (random->Next() % 10 < 4 ? 0 : 1 + random->Next() % 3);
  spec.trigger = 1 + random->Next() % (spec.count - spec.start);
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
  world->context = {.trigger = (spec.mode != 0 ? spec.trigger : 0), .container = Address(world->container), .new_size = new_size, .new_array = new_array};
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

// What differs between two runs ("" if nothing): every register but ecx, esp, the flags, xmm0-7, the container, the emitters, the definitions and the
// context, the frame (a dword that differs by exactly the distance between the two copies of the DLL is a return address a call left behind).
const char* EmitterDiff(const EmitterWorld& a, const EmitterWorld& b, uint32_t copy_distance) {
  for (const size_t reg : {0, 2, 3, 4, 5, 6}) {
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
  CHECK(ec::sites[13].at == patched + site_rva && (ec::written >> 13 & 1) != 0);
  CHECK(std::memcmp(game + site_rva, GAME_SITE.data(), 5) == 0 && std::memcmp(ec::sites[13].game.data(), GAME_SITE.data(), 5) == 0);
  CHECK(game[site_rva + 5] == 0x74);  // the je the stub goes on at
  CHECK(patched[site_rva] == 0xE9 && Address(patched + site_rva + 5) + Read32(patched + site_rva + 1) == Address(stub));
  CHECK(stub[STUB_SIZE - 5] == 0xE9 && Address(stub + STUB_SIZE) + Read32(stub + STUB_SIZE - 4) == Address(patched + site_rva + 5));
  CHECK(std::memcmp(stub, ec::EMITTER_PREFETCH_STUB.data(), STUB_SIZE - 4) == 0);
  CHECK(std::memcmp(game + function_rva + EMITTER_LOOP_TOP, TOP.data(), TOP.size()) == 0 && game[function_rva + EMITTER_LOOP_CALL] == 0xE8
        && std::memcmp(game + function_rva + EMITTER_LOOP_EXIT, EXIT.data(), EXIT.size()) == 0);
  std::printf("16. emitter loop prefetch: the site (+0x%X in the function) found and written, the stub (%zu bytes, %u emitter after the next one) jumps back to the game's je\n",
              static_cast<uint32_t>(ec::EMITTER_LOOP_SITE), STUB_SIZE, static_cast<uint32_t>(ec::EMITTER_PREFETCH_AHEAD));

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
  // instruction: the same je and jmp).
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
    const auto place = [&](uint32_t count) {  // `count` slots (null or an emitter), the array ending where the next page is no access
      for (uint32_t slot = 0; slot < count; ++slot) {
        Write32(arrays - 8 * count + 8 * slot, ((random.Next() & 3) == 0 ? 0 : Address(emitters.data() + EMITTER_BYTES * (random.Next() % EMITTER_SLOTS))));
        Write32(arrays - 8 * count + 8 * slot + 4, random.Next());
      }
      Write32(container.data() + 4, count);
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
    const auto run_one = [&](uint32_t count, uint32_t esi) {
      place(count);
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
      const char* difference = [&] {
        for (const size_t reg : {0, 2, 3, 4, 5, 6}) {
          if (a->out[reg] != b->out[reg]) return "registers";
        }
        if (a->flags_out != b->flags_out) return "flags";
        if (a->esp_after - Address(a.get()) != b->esp_after - Address(b.get())) return "esp";  // an offset into each run's own call
        if (exit_a != exit_b || exit_a == 0) return "way out";
        return (std::memcmp(a->frame, b->frame, sizeof(a->frame)) != 0 ? "frame" : "");
      }();
      ++runs;
      taken += (exit_a == 1 ? 1 : 0);
      loads += (static_cast<uint32_t>(esi + 1 + ec::EMITTER_PREFETCH_AHEAD) < count ? 1 : 0);
      if (difference[0] != 0) {
        ++wrong;
        Report("  emitter bottom: esi %08X count %u: %s differs (exit %u / %u, flags %04X / %04X)\n", esi, count, difference, exit_a, exit_b, a->flags_out, b->flags_out);
      }
    };
    // Every esi around every count, then the edges of the address space and random ones.
    for (uint32_t count = 0; count <= EMITTER_SLOTS; ++count) {
      for (uint32_t esi = 0; esi <= count + 6; ++esi) run_one(count, esi);
      for (uint32_t esi = 0xFFFFFFE0u; esi != 0; ++esi) run_one(count, esi);
      for (uint32_t esi = 0x7FFFFFF0u; esi < 0x80000010u; ++esi) run_one(count, esi);
    }
    const int random_runs = (quick ? 1'000'000 : 5'000'000);
    for (int run = 0; run < random_runs; ++run) {
      const uint32_t count = random.Next() % (EMITTER_SLOTS + 1);
      run_one(count, (random.Next() & 3) == 0 ? random.Next() : random.Next() % (count + 8));
    }
    CHECK(wrong == 0 && loads > runs / 8 && taken > runs / 64 && taken < runs - runs / 8);  // the je is taken only at esi == count
    std::printf("   the loop's bottom alone: %llu runs (%llu with a slot to load, %llu taking the je): every register but ecx, the flags, esp, the frame and the way out equal; "
                "no load past the vector (the array ends at a no-access page)\n", runs, loads, taken);
    // CONTROL: `ja` for `jae` moves the bound one slot: with esi + 1 == count the stub loads the slot after the last one, where the array's page ends. The
    // harness must see that fault in each such run, and in none with esi = 0 and a count above 2.
    {
      uint8_t* jump = ours_block + 8;
      CHECK(*jump == 0x73);
      *jump = 0x77;
      FlushInstructionCache(GetCurrentProcess(), ours_block, STUB_SIZE);
      int at_edge = 0;
      int faulted_at_edge = 0;
      int faulted_inside = 0;
      for (uint32_t count = ec::EMITTER_PREFETCH_AHEAD; count <= EMITTER_SLOTS; ++count) {
        place(count);
        start(b.get(), count, count - 1 - ec::EMITTER_PREFETCH_AHEAD);  // esi + 1 + K == count
        b->target = Address(ours_block);
        ++at_edge;
        faulted_at_edge += EnterParticleLoopGuarded(b.get()) ? 0 : 1;
        if (count > ec::EMITTER_PREFETCH_AHEAD + 1) {
          start(b.get(), count, 0);
          b->target = Address(ours_block);
          faulted_inside += EnterParticleLoopGuarded(b.get()) ? 0 : 1;
        }
      }
      *jump = 0x73;
      FlushInstructionCache(GetCurrentProcess(), ours_block, STUB_SIZE);
      std::printf("   CONTROL, the bound `ja` for `jae`: the load past the last slot faulted in %d of %d runs at the edge (all), %d of the runs well inside (none)\n",
                  faulted_at_edge, at_edge, faulted_inside);
      CHECK(faulted_at_edge == at_edge && faulted_inside == 0);
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
    std::array<uint64_t, 4> modes = {};
    std::array<uint64_t, 4> wrong_modes = {};
    uint64_t not_run = 0;  // plain vectors whose update count is not the number of emitters from the start: the harness did not run the loop
    uint64_t prefetching = 0;
    uint64_t with_nulls = 0;
    uint64_t with_garbage_after = 0;
    const auto compare = [&](const EmitterSpec& spec) {
      EmitterSetup(world.get(), ends, spec, &random);
      *backup = *world;
      RunEmitterLoop(world.get(), *backup, game_top, result_a.get());
      RunEmitterLoop(world.get(), *backup, patched_top, result_b.get());
      ++modes[spec.mode];
      not_run += (spec.mode == 0 && result_a->context.calls != spec.count - spec.start ? 1 : 0);
      prefetching += (spec.start + 1 + ec::EMITTER_PREFETCH_AHEAD < spec.count ? 1 : 0);
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
            }
          }
        }
      }
    }
    const int runs = (quick ? 200'000 : 1'000'000);
    for (int run = 0; run < runs; ++run) compare(EmitterRandomSpec(&random));
    CHECK(wrong_modes[0] == 0 && wrong_modes[1] == 0 && wrong_modes[2] == 0 && wrong_modes[3] == 0 && not_run == 0);
    CHECK(prefetching > 20'000 && with_nulls > 20'000 && with_garbage_after > 20'000);
    std::printf("   the whole loop: %llu vectors (%llu plain, %llu with a new array (grown or not), %llu with only the size changed; %llu long enough to prefetch, %llu with null "
                "slots, %llu with garbage past the count): every register but ecx, the flags, esp, xmm0-7, the container, the emitters, the definitions, the frame and the call order equal\n",
                modes[0] + modes[1] + modes[2] + modes[3], modes[0], modes[1] + modes[2], modes[3], prefetching, with_nulls, with_garbage_after);
  }

  // 16d. CONTROLs: the stub with each of six changes must be seen (plain vectors; a load of a wrong slot or through a wrong pointer is seen as a fault,
  // the ones that write or store as a difference).
  {
    DWORD protection;
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection) != FALSE);
    struct Change {
      const char* what;
      size_t at;
      std::array<uint8_t, 4> bytes;
      size_t size;
      uint32_t nulls;
    };
    const std::array<Change, 6> changes = {{{"writes eax (lea eax, [esi + K])", 2, {0x46}, 1, 0},
                                            {"ends with the wrong compare (cmp esi, esi)", 39, {0x3B, 0xF6, 0x90, 0x90}, 4, 0},
                                            {"stores (mov [esp - 0x40], ecx for a prefetch)", 28, {0x89, 0x4C, 0x24, 0xC0}, 4, 0},
                                            {"has no null test (je skip removed)", 23, {0x90, 0x90}, 2, 8},
                                            {"has the wrong bound (ja skip)", 8, {0x77}, 1, 0},
                                            {"reads ebx for the container (mov ecx, ebx)", 10, {0x89, 0xD9, 0x90, 0x90}, 4, 0}}};
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
        spec.mode = 0;
        spec.trigger = 0;
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
    bool on = true;
    for (int switches = 0; switches < 200; ++switches) {
      on = !on;
      int tries = 0;
      do {
        twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, on);
        ++tries;
      } while (((ec::written >> 13 & 1) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
    }
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true);
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("   200 switches while 4 threads ran the loop %llu times in each copy: %llu different (%d writes waited for a thread inside a site)\n", loops.load(), bad.load(), busy);
    CHECK(bad.load() == 0 && (ec::written >> 13 & 1) != 0);
  }

  // A parked thread whose saved Eip is each byte of the site (and the byte before, the bytes after): the row switched, the site changes only with Eip at
  // its first byte or outside it.
  {
    std::atomic<bool> stop = false;
    std::thread parked([&] {
      while (!stop.load()) Sleep(1);
    });
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, GetThreadId(parked.native_handle()));
    CHECK(thread != nullptr);
    const uint8_t* site = patched + site_rva;
    bool on = (ec::written >> 13 & 1) != 0;
    CHECK(on);
    int wrong = 0;
    for (int offset = -1; offset <= 6; ++offset) {
      CHECK(SuspendThread(thread) != static_cast<DWORD>(-1));
      CONTEXT saved = {.ContextFlags = CONTEXT_CONTROL};
      CHECK(GetThreadContext(thread, &saved) != FALSE);
      CONTEXT moved = saved;
      moved.Eip = Address(site + offset);
      CHECK(SetThreadContext(thread, &moved) != FALSE);
      const uint32_t before = ec::written;
      twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, !on);
      const bool changed = ((ec::written ^ before) >> 13 & 1) != 0;
      wrong += (changed != !(offset > 0 && offset < 5)) ? 1 : 0;
      if (changed) on = !on;
      CHECK(SetThreadContext(thread, &saved) != FALSE);
      ResumeThread(thread);
    }
    for (int tries = 0; !on && tries < static_cast<int>(ec::MAX_FAILED_WRITES); ++tries) {
      twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true);
      on = (ec::written >> 13 & 1) != 0;
    }
    stop = true;
    parked.join();
    CloseHandle(thread);
    CHECK(wrong == 0 && on);
    std::printf("   a parked thread at the site -1 .. +6: the site is refused at +1 .. +4 and written at the others\n");
  }

  // 16f. Timing. Emitters of 256 bytes in 256 MB, each one's definition (128 bytes) at random in another 128 MB, vectors of `per_call` emitters picked at
  // random (a fresh set for every run, so no variant finds what another left in the cache); the update loads the emitter's first line and its
  // definition's word and multiplies `emitter_busy` times. The game's copy and ours in the two images, and copies of the function with the stub for
  // K = 1 .. 3 and without it; ns an emitter, the median of 7 rounds.
  {
    constexpr size_t POOL = 1u << 20;
    constexpr size_t PICKS = 1u << 16;
    constexpr int ROUNDS = 7;
    constexpr size_t VARIANTS = 6;  // game, ours, the copy without the stub, K = 1 .. 3
    auto* emitter_pool = static_cast<uint8_t*>(VirtualAlloc(nullptr, POOL * 256, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* definition_pool = static_cast<uint8_t*>(VirtualAlloc(nullptr, POOL * 128, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECK(emitter_pool != nullptr && definition_pool != nullptr);
    std::vector<uint32_t> permutation(POOL);
    for (size_t index = 0; index < POOL; ++index) permutation[index] = static_cast<uint32_t>(index);
    for (size_t index = 0; index < POOL; ++index) {
      std::swap(permutation[index], permutation[index + random.Next() % (POOL - index)]);
      Write32(emitter_pool + index * 256 + 0x58, Address(definition_pool + static_cast<size_t>(permutation[index]) * 128));
      Write32(definition_pool + index * 128 + 0x2C, random.Next());
    }
    // Scratch copies of the function: its bytes, the call at the busy update, the way out at the harness, the stub for K behind.
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
      if (variant > 2) {  // K = variant - 2: the site jumps to the stub, whose jump goes on at the je
        const auto ahead_stub = ec::EmitterPrefetchStub(static_cast<uint8_t>(variant - 2));
        uint8_t* ahead_code = copy + 0x140;
        std::memcpy(ahead_code, ahead_stub.data(), ahead_stub.size());
        Write32(ahead_code + ahead_stub.size() - 4, Address(copy + ec::EMITTER_LOOP_SITE + 5) - Address(ahead_code + ahead_stub.size()));
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
        elements[2 * index] = Address(emitter_pool + static_cast<size_t>(permutation[hot ? index % 64 : index]) * 256);
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
    for (const auto& [busy, per_call] : {std::pair{1u, 64u}, std::pair{100u, 64u}, std::pair{1000u, 64u}, std::pair{100u, 16u}, std::pair{1000u, 16u}}) {
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
      lines += std::format("   ns an emitter, update of {} multiplies (alone, hot: {:.1f}), vectors of {}: game {:.1f}, ours (K = {}) {:.1f}, copies: no stub {:.1f}, K=1 {:.1f}, K=2 {:.1f}, "
                           "K=3 {:.1f}\n", busy, hot, per_call, median[0], static_cast<uint32_t>(ec::EMITTER_PREFETCH_AHEAD), median[1], median[2], median[3], median[4], median[5]);
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
