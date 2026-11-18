// ====================================================================================================================
// 15. The particle update loop's prefetch (the emitter update +0x1893470, its loop +0x1893820 .. +0x1893888; Thrones loop +0x1908b80; Faster
// Effect Prefetch; engine_code.hpp 10.), included inside tests.cpp's anonymous namespace after particle_reload.inc (SumBlockCall, EnterSumBlock and
// SumBlockReturn, the loop bottom's entry and exit of part 12, are that test's). The loop takes the particle [[edi + 0x68] + ebx*4], stores to it,
// calls its update through the vtable (five stack arguments, ret 0x14; the vector's size and array are read again after every call) and ends with
// `cmp ebx, [edi + 0x64]; jb the top`; the stub at that bottom loads the slot K particles after the next one and prefetches its four lines.
// a. the site and the stub as Prepare made them (jumps, bytes);
// b. the loop's bottom alone (the game's three instructions and a copy of the stub, both exits to the harness): every ebx around the count 0 to 48,
//    ebx at the edges of the address space, random registers, an array that ends where the next page is no access (a load past the vector crashes
//    the test): every register but esi, the flags, esp and which way it went equal;
// c. the whole loop in both copies of the DLL (the harness's jump from +0x1893888 back to itself): random emitters of 1 to 48 particles with a fake update
//    that counts its calls, hashes their order, writes into its particle and may change the vector at one call (shrink to any size, another array of any
//    size, grow in place), the array ending at a no-access page, garbage in the slots the loop never reads, random registers, flags, xmm and frame:
//    every register but esi, the flags, esp, xmm0-7, every byte of the emitter, the particles, the context and the frame (a return address apart),
//    the call count and their order equal; CONTROLs: a stub that writes eax, one that ends with the wrong compare, one that stores must each be seen;
// d. 200 switches under 4 threads that run the loop in both copies, a parked thread at each byte of the site;
// e. timing, K from 1 to 4: particles of four lines spread over 512 MB, the update 0 / ~300 / ~600 cycles after loading the four lines.
// ====================================================================================================================

constexpr uint32_t LOOP_SLOTS = 48;      // the vector's room: its block ends where the next page is no access
constexpr uint32_t LOOP_ITEM_SIZE = 0x80;

// One emitter's bookkeeping, shared by its particles: what the fake update counts and may change.
struct LoopContext {
  uint32_t calls;      // 0: the updates so far
  uint32_t order;      // 4: a hash of the order the particles were updated in
  uint32_t trigger;    // 8: the update after which the vector changes (0: never)
  uint32_t parent;     // 12: the emitter
  uint32_t new_count;  // 16
  uint32_t new_array;  // 20
  uint32_t unused[2];  // 24
};

// The loop entered by itself: esp at frame + 0x100 (16 aligned as the function's, [esp + 0x40] .. [esp + 0x4f] inside the frame, room below for the
// calls), every register, the flags and xmm0-7 set; at the exit (+0x1893888, which the test jumps to ParticleLoopReturn) the same saved, the call found
// by esp.
struct LoopCall {
  uint32_t in[7];       // 0: eax ecx edx ebx esi edi ebp
  uint32_t flags_in;    // 28
  uint32_t target;      // 32: the loop's top (the game's copy or ours)
  uint32_t saved_esp;   // 36
  uint32_t out[7];      // 40
  uint32_t flags_out;   // 68: lahf's SF ZF AF PF CF in bits 8-15, OF in bit 0
  uint32_t esp_after;   // 72
  uint32_t unused;      // 76
  alignas(16) uint8_t xmm_in[8][16];   // 80
  alignas(16) uint8_t xmm_out[8][16];  // 208
  alignas(16) uint8_t frame[0x200];    // 336
};
static_assert(offsetof(LoopCall, out) == 40 && offsetof(LoopCall, flags_out) == 68 && offsetof(LoopCall, esp_after) == 72
              && offsetof(LoopCall, xmm_in) == 80 && offsetof(LoopCall, xmm_out) == 208 && offsetof(LoopCall, frame) == 336);
constexpr uint32_t LOOP_ESP = 336 + 0x100;

__declspec(naked) void __cdecl EnterParticleLoop(LoopCall*) {
  __asm {
    pushad
    mov ebp, [esp + 36]
    mov [ebp + 36], esp
    lea esp, [ebp + 592]  // LOOP_ESP
    movups xmm0, [ebp + 80]
    movups xmm1, [ebp + 96]
    movups xmm2, [ebp + 112]
    movups xmm3, [ebp + 128]
    movups xmm4, [ebp + 144]
    movups xmm5, [ebp + 160]
    movups xmm6, [ebp + 176]
    movups xmm7, [ebp + 192]
    push dword ptr [ebp + 28]
    popfd
    push dword ptr [ebp + 32]
    mov eax, [ebp + 0]
    mov ecx, [ebp + 4]
    mov edx, [ebp + 8]
    mov ebx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]
    mov ebp, [ebp + 24]
    ret  // to the loop's top: esp back at frame + 0x100
  }
}

__declspec(naked) void ParticleLoopReturn() {
  __asm {
    mov [esp - 4], eax
    lahf
    seto al
    movzx eax, ax
    mov [esp - 8], eax
    lea eax, [esp - 592]  // the call: esp is LOOP_ESP into it
    mov [eax + 44], ecx
    mov [eax + 48], edx
    mov [eax + 52], ebx
    mov [eax + 56], esi
    mov [eax + 60], edi
    mov [eax + 64], ebp
    mov [eax + 72], esp
    movups [eax + 208], xmm0
    movups [eax + 224], xmm1
    movups [eax + 240], xmm2
    movups [eax + 256], xmm3
    movups [eax + 272], xmm4
    movups [eax + 288], xmm5
    movups [eax + 304], xmm6
    movups [eax + 320], xmm7
    mov ecx, [esp - 4]
    mov [eax + 40], ecx
    mov ecx, [esp - 8]
    mov [eax + 68], ecx
    mov ebp, eax
    mov esp, [ebp + 36]
    popad
    ret
  }
}

// The particles' vtable: [+0xc] is the update. The fake update (thiscall, ecx the particle, ret 0x14 for the five arguments the loop pushes; ebx esi
// edi ebp kept; eax, ecx and edx are the callee's to spend): counts its calls in the context, hashes the particle's index into `order`, records its
// call number in the particle (+0xc), and after call `trigger` stores the new size and array into the emitter; eax comes out as a hash of the call
// number: the loop keeps its upper bits (it sets only al after the call).
uint32_t loop_vtable[8] = {};
__declspec(naked) void LoopUpdate() {
  __asm {
    mov edx, [ecx + 0x10]
    mov eax, [edx]
    inc eax
    mov [edx], eax
    mov [ecx + 0xC], eax
    push eax
    mov eax, [edx + 4]
    imul eax, eax, 31
    add eax, [ecx + 8]
    mov [edx + 4], eax
    pop eax
    cmp eax, [edx + 8]
    jne unchanged
    mov ecx, [edx + 12]
    mov eax, [edx + 16]
    mov [ecx + 0x64], eax
    mov eax, [edx + 20]
    mov [ecx + 0x68], eax
    mov eax, [edx]
  unchanged:
    imul eax, eax, -1640531535  // 0x9E3779B1
    ret 0x14
  }
}

// The same particles with the update of the timing: loads the particle's four lines and does `loop_busy` dependent multiplies (3 cycles each).
uint32_t loop_busy_vtable[8] = {};
uint32_t loop_busy = 1;
__declspec(naked) void LoopUpdateBusy() {
  __asm {
    mov eax, [ecx]
    add eax, [ecx + 0x40]
    add eax, [ecx + 0x80]
    add eax, [ecx + 0xC0]
    mov edx, loop_busy
  spin:
    imul eax, eax, 3
    dec edx
    jnz spin
    ret 0x14
  }
}

// A page of memory followed by one that is no access; returns the first page's end.
uint8_t* LoopBlockEnd() {
  auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  DWORD protection;
  VirtualProtect(memory + 0x1000, 0x1000, PAGE_NOACCESS, &protection);
  return memory + 0x1000;
}

// The emitter [+0x64 the size, +0x68 the array, +8 a word the loop compares with 1, +0x6c a byte it writes], the two words at ebp, 48 particles (+0 the
// vtable, +8 the index, +0xc the update's call number, +0x10 the context, +0x18 .. +0x3f the loop's stores, +0x64 the byte it tests), the context.
struct LoopWorld {
  LoopCall call;
  alignas(16) uint8_t emitter[0x80];
  alignas(16) uint8_t args[0x20];
  alignas(16) uint8_t items[LOOP_SLOTS][LOOP_ITEM_SIZE];
  LoopContext context;
};

struct LoopSpec {
  uint32_t count;      // the vector's size at the start, 1 .. 48
  uint32_t start;      // the first particle's index (ebx), below the size
  uint32_t mode;       // 0 the vector stays; 1 it shrinks to new_count; 2 another array of new_count slots; 3 it grows in place to new_count
  uint32_t trigger;    // the update after which it changes (1 on)
  uint32_t new_count;
};

LoopSpec LoopRandomSpec(Random* random) {
  LoopSpec spec = {.count = 1 + random->Next() % LOOP_SLOTS};
  spec.start = ((random->Next() & 3) == 0 ? random->Next() % spec.count : 0);
  spec.mode = (random->Next() % 10 < 4 ? 0 : 1 + random->Next() % 3);
  spec.trigger = 1 + random->Next() % (spec.count - spec.start);
  switch (spec.mode) {
    case 1: spec.new_count = random->Next() % (spec.count + 1); break;
    case 2: spec.new_count = 1 + random->Next() % LOOP_SLOTS; break;
    case 3: spec.new_count = spec.count + random->Next() % (LOOP_SLOTS - spec.count + 1); break;
    default: spec.new_count = spec.count; break;
  }
  return spec;
}

// Fills `world` (every byte the loop or the update could read at random, then the structure) for `spec`; `ends` are two blocks' ends (LoopBlockEnd):
// the vector's array is placed to end exactly there, so a load of a slot past the size the vector has at that moment crashes the test. Slots the loop
// can never reach hold garbage (small numbers, no-access and wrapping addresses, the no-access page itself); the registers, flags, xmm and frame
// are random; `target` is left to the run.
void LoopSetup(LoopWorld* world, const std::array<uint8_t*, 2>& ends, const LoopSpec& spec, Random* random) {
  constexpr std::array<uint32_t, 6> GARBAGE = {0x10, 0xDEAD0000, 0xFFFFFFF0, 0xFFFFFFC0, 0x7FFF0000, 1};
  const auto fill = [&](uint8_t* bytes, size_t size) {
    for (size_t at = 0; at + 4 <= size; at += 4) Write32(bytes + at, random->Next());
  };
  fill(reinterpret_cast<uint8_t*>(world), sizeof(LoopWorld));
  const bool traced = (spec.mode != 0);
  const uint32_t extent = (spec.mode == 3 ? spec.new_count : spec.count);
  const uint32_t array = Address(ends[0]) - 4 * extent;
  // Slots up to `valid` are reached by the game's loop (before or after the change).
  uint32_t valid = spec.count;
  if (spec.mode == 1) valid = std::min(spec.count, std::max(spec.new_count, spec.start + spec.trigger));
  if (spec.mode == 2) valid = std::min(spec.count, spec.start + spec.trigger);
  if (spec.mode == 3) valid = spec.new_count;
  for (uint32_t slot = 0; slot < extent; ++slot) {
    uint32_t value = Address(world->items[random->Next() % LOOP_SLOTS]);
    if (slot >= valid) {
      value = ((random->Next() & 7) == 0 ? Address(ends[0]) + (random->Next() & 0xC0) : GARBAGE[random->Next() % GARBAGE.size()]);
    }
    Write32(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(array)) + 4 * slot, value);
  }
  const uint32_t new_count = spec.new_count;
  const uint32_t array2 = Address(ends[1]) - 4 * new_count;
  if (spec.mode == 2) {
    for (uint32_t slot = 0; slot < new_count; ++slot) {
      Write32(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(array2)) + 4 * slot, Address(world->items[random->Next() % LOOP_SLOTS]));
    }
  }
  for (uint32_t index = 0; index < LOOP_SLOTS; ++index) {
    uint8_t* item = world->items[index];
    Write32(item, Address(loop_vtable));
    Write32(item + 8, index);
    Write32(item + 0x10, Address(&world->context));
    item[0x64] = ((random->Next() & 1) != 0 ? static_cast<uint8_t>(random->Next()) : 0);
  }
  Write32(world->emitter + 0x64, spec.count);
  Write32(world->emitter + 0x68, array);
  world->context = {.parent = Address(world->emitter), .new_count = (traced ? new_count : spec.count), .new_array = (spec.mode == 2 ? array2 : array)};
  world->context.trigger = (traced ? spec.trigger : 0);
  for (uint32_t& value : world->call.in) value = random->Next();
  world->call.in[3] = spec.start;
  world->call.in[5] = Address(world->emitter);
  world->call.in[6] = Address(world->args);
  world->call.flags_in = (random->Next() & 0x8D5) | 2;
}

// One run of the loop at `target` from `backup`, left in `out`.
void RunLoop(LoopWorld* world, const LoopWorld& backup, uint32_t target, LoopWorld* out) {
  *world = backup;
  world->call.target = target;
  EnterParticleLoop(&world->call);
  *out = *world;
}

// What differs between two runs ("" if nothing): every register but esi, esp, the flags, xmm0-7, the emitter, the particles and the context, the
// frame (a dword that differs by exactly the distance between the two copies of the DLL is a return address a call left behind).
const char* LoopDiff(const LoopWorld& a, const LoopWorld& b, uint32_t copy_distance) {
  for (const size_t reg : {0, 1, 2, 3, 5, 6}) {
    if (a.call.out[reg] != b.call.out[reg]) return "registers";
  }
  if (a.call.esp_after != b.call.esp_after) return "esp";
  if (a.call.flags_out != b.call.flags_out) return "flags";
  if (std::memcmp(a.call.xmm_out, b.call.xmm_out, sizeof(a.call.xmm_out)) != 0) return "xmm";
  if (std::memcmp(a.emitter, b.emitter, sizeof(a.emitter)) != 0 || std::memcmp(a.args, b.args, sizeof(a.args)) != 0
      || std::memcmp(a.items, b.items, sizeof(a.items)) != 0 || std::memcmp(&a.context, &b.context, sizeof(a.context)) != 0) {
    return "memory (the emitter, the particles or the context)";
  }
  for (size_t at = 0; at < sizeof(a.call.frame); at += 4) {
    const uint32_t x = Read32(a.call.frame + at);
    const uint32_t y = Read32(b.call.frame + at);
    if (x != y && y - x != copy_distance) return "stack";
  }
  return "";
}

// The loop's bottom by itself, in two blocks that end at the harness: which way it went is recorded by the exit that ran.
uint32_t particle_exit = 0;
uint32_t particle_exit_return = 0;
__declspec(naked) void ParticleBlockTaken() {
  __asm {
    mov particle_exit, 1
    jmp dword ptr [particle_exit_return]
  }
}
__declspec(naked) void ParticleBlockFell() {
  __asm {
    mov particle_exit, 2
    jmp dword ptr [particle_exit_return]
  }
}

int RunParticleTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  const uint32_t loop_rva = build.code->particle_loop_rva;
  const uint32_t site_rva = loop_rva + ec::PARTICLE_LOOP_SITE;
  const uint32_t exit_rva = loop_rva + ec::PARTICLE_LOOP_EXIT;
  const uint32_t copy_distance = static_cast<uint32_t>(patched - game);
  constexpr std::array<uint8_t, 5> GAME_SITE = {0x3B, 0x5F, 0x64, 0x72, 0x98};  // cmp ebx, [edi + 0x64]; jb the top
  loop_vtable[3] = Address(reinterpret_cast<const void*>(&LoopUpdate));
  loop_busy_vtable[3] = Address(reinterpret_cast<const void*>(&LoopUpdateBusy));
  particle_exit_return = Address(reinterpret_cast<const void*>(&SumBlockReturn));
  std::array<uint8_t, 5> exit_game = {};
  std::array<uint8_t, 5> exit_ours = {};
  uint8_t* const stub = ec::page + ec::PARTICLE_PREFETCH_PAGE;
  constexpr size_t STUB_SIZE = ec::PARTICLE_PREFETCH_STUB.size();

  // 15a. The site and the stub as Prepare made them.
  CHECK(ec::sites[12].at == patched + site_rva && (ec::written >> 12 & 1) != 0);
  CHECK(std::memcmp(game + site_rva, GAME_SITE.data(), 5) == 0 && std::memcmp(ec::sites[12].game.data(), GAME_SITE.data(), 5) == 0);
  CHECK(patched[site_rva] == 0xE9 && Address(patched + site_rva + 5) + Read32(patched + site_rva + 1) == Address(stub));
  CHECK(stub[STUB_SIZE - 5] == 0xE9 && Address(stub + STUB_SIZE) + Read32(stub + STUB_SIZE - 4) == Address(patched + exit_rva));
  CHECK(stub[STUB_SIZE - 11] == 0x0F && stub[STUB_SIZE - 10] == 0x82 && Address(stub + STUB_SIZE - 5) + Read32(stub + STUB_SIZE - 9) == Address(patched + loop_rva));
  CHECK(std::memcmp(stub, ec::PARTICLE_PREFETCH_STUB.data(), STUB_SIZE - 11) == 0);
  CHECK(game[exit_rva] == 0x84 && game[exit_rva + 1] == 0xC0);  // test al, al: the way out of the loop
  std::printf("15. particle loop prefetch: the site (+0x%X in the loop) found and written, the stub (%zu bytes, %u particles ahead) jumps back to the loop's top and its way out\n",
              static_cast<uint32_t>(ec::PARTICLE_LOOP_SITE), STUB_SIZE, static_cast<uint32_t>(ec::PARTICLE_PREFETCH_AHEAD));

  // 15b. The bottom alone. The game's block: cmp ebx, [edi + 0x64]; jb taken; jmp fell. Ours: the stub's bytes with its two jumps to the same exits.
  {
    uint8_t* blocks = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    CHECK(blocks != nullptr);
    const auto aim = [&](uint8_t* block, size_t size) {  // the block's last two jumps to the harness's exits
      block[size - 11] = 0x0F;
      block[size - 10] = 0x82;
      Write32(block + size - 9, Address(reinterpret_cast<const void*>(&ParticleBlockTaken)) - Address(block + size - 5));
      block[size - 5] = 0xE9;
      Write32(block + size - 4, Address(reinterpret_cast<const void*>(&ParticleBlockFell)) - Address(block + size));
      FlushInstructionCache(GetCurrentProcess(), block, size);
    };
    uint8_t* game_block = blocks;
    uint8_t* ours_block = blocks + 0x40;
    std::memcpy(game_block, GAME_SITE.data(), 3);
    aim(game_block, 3 + 11);
    std::memcpy(ours_block, stub, STUB_SIZE - 11);
    aim(ours_block, STUB_SIZE);
    auto* arrays = LoopBlockEnd();
    alignas(16) std::array<uint8_t, 0x80> emitter = {};
    Random random = {.state = 0xB0770};
    SumBlockCall a, b;
    uint64_t runs = 0;
    uint64_t loads = 0;
    uint64_t wrong = 0;
    uint64_t taken = 0;
    const auto place = [&](uint32_t count) {  // `count` random slots, the array ending where the next page is no access
      for (uint32_t slot = 0; slot < count; ++slot) Write32(arrays - 4 * count + 4 * slot, random.Next());
      Write32(emitter.data() + 0x64, count);
      Write32(emitter.data() + 0x68, Address(arrays) - 4 * count);
    };
    const auto start = [&](SumBlockCall* call, uint32_t ebx) {  // every register random, ebx and the emitter set
      std::memset(call, 0, sizeof(*call));
      for (uint32_t& value : call->in) value = random.Next();
      call->in[3] = ebx;
      call->in[5] = Address(emitter.data());
      call->flags_in = (random.Next() & 0x8D5) | 2;
    };
    const auto run_one = [&](uint32_t count, uint32_t ebx) {
      place(count);
      start(&a, ebx);
      b = a;
      particle_exit = 0;
      sum_block_target = Address(game_block);
      EnterSumBlock(&a);
      const uint32_t exit_a = particle_exit;
      particle_exit = 0;
      sum_block_target = Address(ours_block);
      EnterSumBlock(&b);
      const uint32_t exit_b = particle_exit;
      const bool same = [&] {
        for (const size_t reg : {0, 1, 2, 3, 5, 6}) {
          if (a.out[reg] != b.out[reg]) return false;
        }
        return a.flags_out == b.flags_out && a.esp_after == b.esp_after && exit_a == exit_b && exit_a != 0;
      }();
      ++runs;
      taken += (exit_a == 1 ? 1 : 0);
      loads += (static_cast<uint32_t>(ebx + ec::PARTICLE_PREFETCH_AHEAD) < count ? 1 : 0);
      if (!same) {
        ++wrong;
        Report("  particle bottom: ebx %08X count %u: NOT equal (exit %u / %u, flags %04X / %04X)\n", ebx, count, exit_a, exit_b, a.flags_out, b.flags_out);
      }
    };
    // Every ebx around every size, then the edges of the address space and random ones.
    for (uint32_t count = 0; count <= LOOP_SLOTS; ++count) {
      for (uint32_t ebx = 0; ebx <= count + 6; ++ebx) run_one(count, ebx);
      for (uint32_t ebx = 0xFFFFFFE0u; ebx != 0; ++ebx) run_one(count, ebx);
      for (uint32_t ebx = 0x7FFFFFF0u; ebx < 0x80000010u; ++ebx) run_one(count, ebx);
    }
    const int random_runs = (quick ? 1'000'000 : 5'000'000);
    for (int run = 0; run < random_runs; ++run) {
      const uint32_t count = random.Next() % (LOOP_SLOTS + 1);
      run_one(count, (random.Next() & 3) == 0 ? random.Next() : random.Next() % (count + 8));
    }
    CHECK(wrong == 0 && loads > runs / 8 && taken > runs / 8 && taken < runs - runs / 8);
    std::printf("   the loop's bottom alone: %llu runs (%llu with a slot to load, %llu taking the jb): every register but esi, the flags, esp and the way out equal; "
                "no load past the vector (the array ends at a no-access page)\n", runs, loads, taken);
    // CONTROL: `ja` for `jae` moves the bound one slot: with ebx + K == count the stub loads the slot after the last one, where the array's page ends. The
    // harness must see that fault (caught here) in each such run, and in none with ebx = 0.
    {
      uint8_t* jump = ours_block + 6;
      CHECK(*jump == 0x73);
      *jump = 0x77;
      FlushInstructionCache(GetCurrentProcess(), ours_block, STUB_SIZE);
      int at_edge = 0;
      int faulted_at_edge = 0;
      int faulted_inside = 0;
      for (uint32_t count = ec::PARTICLE_PREFETCH_AHEAD; count <= LOOP_SLOTS; ++count) {
        place(count);
        sum_block_target = Address(ours_block);
        start(&b, count - ec::PARTICLE_PREFETCH_AHEAD);
        ++at_edge;
        faulted_at_edge += EnterSumBlockGuarded(&b) ? 0 : 1;
        if (count > ec::PARTICLE_PREFETCH_AHEAD) {
          start(&b, 0);
          faulted_inside += EnterSumBlockGuarded(&b) ? 0 : 1;
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

  // The harness's way out of the loop in both copies (after Prepare: the pattern reads these bytes), put back at the end.
  std::memcpy(exit_game.data(), game + exit_rva, 5);
  std::memcpy(exit_ours.data(), patched + exit_rva, 5);
  JumpTo(game + exit_rva, reinterpret_cast<const void*>(&ParticleLoopReturn));
  JumpTo(patched + exit_rva, reinterpret_cast<const void*>(&ParticleLoopReturn));
  const uint32_t game_top = Address(game + loop_rva);
  const uint32_t patched_top = Address(patched + loop_rva);

  // 15c. The whole loop in both copies.
  auto world = std::make_unique<LoopWorld>();
  auto backup = std::make_unique<LoopWorld>();
  auto result_a = std::make_unique<LoopWorld>();
  auto result_b = std::make_unique<LoopWorld>();
  CHECK(Address(world.get()) % 16 == 0 && Address(backup.get()) % 16 == 0 && Address(result_a.get()) % 16 == 0 && Address(result_b.get()) % 16 == 0);
  const std::array<uint8_t*, 2> ends = {LoopBlockEnd(), LoopBlockEnd()};
  Random random = {.state = 0x15C15};
  {
    std::array<uint64_t, 4> modes = {};
    std::array<uint64_t, 4> wrong_modes = {};
    uint64_t not_run = 0;  // plain emitters whose update count is not the number of particles from the start: the harness did not run the loop
    uint64_t prefetching = 0;
    const auto compare = [&](const LoopSpec& spec) {
      LoopSetup(world.get(), ends, spec, &random);
      *backup = *world;
      RunLoop(world.get(), *backup, game_top, result_a.get());
      RunLoop(world.get(), *backup, patched_top, result_b.get());
      ++modes[spec.mode];
      not_run += (spec.mode == 0 && result_a->context.calls != spec.count - spec.start ? 1 : 0);
      prefetching += (spec.start + 1 + ec::PARTICLE_PREFETCH_AHEAD < spec.count ? 1 : 0);
      if (const char* what = LoopDiff(*result_a, *result_b, copy_distance); what[0] != 0) {
        ++wrong_modes[spec.mode];
        Report("  particle loop: count %u start %u mode %u trigger %u -> %u: %s differs\n", spec.count, spec.start, spec.mode, spec.trigger, spec.new_count, what);
      }
    };
    for (uint32_t count = 1; count <= 12; ++count) {  // every case of a small vector
      for (const uint32_t start : {0u, count / 2}) {
        compare({.count = count, .start = start, .mode = 0, .trigger = 0, .new_count = count});
        for (uint32_t trigger = 1; trigger <= count - start; ++trigger) {
          for (uint32_t size = 0; size <= count; ++size) compare({.count = count, .start = start, .mode = 1, .trigger = trigger, .new_count = size});
          for (uint32_t size = 1; size <= 16; ++size) compare({.count = count, .start = start, .mode = 2, .trigger = trigger, .new_count = size});
          for (uint32_t size = count; size <= 16; ++size) compare({.count = count, .start = start, .mode = 3, .trigger = trigger, .new_count = size});
        }
      }
    }
    const int runs = (quick ? 200'000 : 1'000'000);
    for (int run = 0; run < runs; ++run) compare(LoopRandomSpec(&random));
    CHECK(wrong_modes[0] == 0 && wrong_modes[1] == 0 && wrong_modes[2] == 0 && wrong_modes[3] == 0 && not_run == 0);
    CHECK(prefetching > 100'000);
    std::printf("   the whole loop: %llu emitters (%llu plain, %llu shrinking, %llu with another array, %llu growing in place; %llu long enough to prefetch): every register but esi, "
                "the flags, esp, xmm0-7, the emitter, the particles, the context, the frame and the call order equal\n",
                modes[0] + modes[1] + modes[2] + modes[3], modes[0], modes[1], modes[2], modes[3], prefetching);
  }

  // 15d. CONTROLs: the stub with each of three changes must be seen (plain emitters of 6 to 48 particles: the prefetch runs at the first bottom).
  {
    DWORD protection;
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection) != FALSE);
    struct Change {
      const char* what;
      size_t at;
      std::array<uint8_t, 4> bytes;
      size_t size;
    };
    const std::array<Change, 3> changes = {{{"writes eax (lea eax, [ebx + K])", 1, {0x43}, 1},
                                            {"ends with the wrong compare (cmp esi, [edi + 0x64])", 37, {0x77}, 1},
                                            {"stores (mov [esp - 0x40], ecx for a prefetch)", 18, {0x89, 0x4C, 0x24, 0xC0}, 4}}};
    for (const Change& change : changes) {
      std::array<uint8_t, 4> original = {};
      std::memcpy(original.data(), stub + change.at, change.size);
      std::memcpy(stub + change.at, change.bytes.data(), change.size);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      int seen = 0;
      constexpr int CONTROL_RUNS = 5000;
      for (int run = 0; run < CONTROL_RUNS; ++run) {
        LoopSpec spec = LoopRandomSpec(&random);
        spec.count = 6 + random.Next() % (LOOP_SLOTS - 5);
        spec.start = 0;
        spec.mode = 0;
        spec.trigger = 0;
        spec.new_count = spec.count;
        LoopSetup(world.get(), ends, spec, &random);
        *backup = *world;
        RunLoop(world.get(), *backup, game_top, result_a.get());
        RunLoop(world.get(), *backup, patched_top, result_b.get());
        seen += (LoopDiff(*result_a, *result_b, copy_distance)[0] != 0 ? 1 : 0);
      }
      std::memcpy(stub + change.at, original.data(), change.size);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      std::printf("   CONTROL, a stub that %s: %d of %d runs seen as different\n", change.what, seen, CONTROL_RUNS);
      CHECK(seen == CONTROL_RUNS);
    }
    CHECK(std::memcmp(stub, ec::PARTICLE_PREFETCH_STUB.data(), STUB_SIZE - 11) == 0);
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READ, &protection) != FALSE);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }

  // 15e. The prefetch row on and off 200 times while 4 threads run the loop in both copies on their own emitters.
  {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> loops = 0;
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        auto thread_world = std::make_unique<LoopWorld>();
        auto thread_backup = std::make_unique<LoopWorld>();
        auto thread_a = std::make_unique<LoopWorld>();
        auto thread_b = std::make_unique<LoopWorld>();
        const std::array<uint8_t*, 2> thread_ends = {LoopBlockEnd(), LoopBlockEnd()};
        Random thread_random = {.state = 0xD00D00ull + static_cast<uint64_t>(worker)};
        while (!stop.load()) {
          LoopSetup(thread_world.get(), thread_ends, LoopRandomSpec(&thread_random), &thread_random);
          *thread_backup = *thread_world;
          // The inputs and outputs are in the world: repeated runs keep the thread inside the loop and its stub most of the time.
          for (int repeat = 0; repeat < 8; ++repeat) {
            RunLoop(thread_world.get(), *thread_backup, game_top, thread_a.get());
            RunLoop(thread_world.get(), *thread_backup, patched_top, thread_b.get());
            if (LoopDiff(*thread_a, *thread_b, copy_distance)[0] != 0) ++bad;
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
      } while (((ec::written >> 12 & 1) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
    }
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true);
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("   200 switches while 4 threads ran the loop %llu times in each copy: %llu different (%d writes waited for a thread inside a site)\n", loops.load(), bad.load(), busy);
    CHECK(bad.load() == 0 && (ec::written >> 12 & 1) != 0);
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
    bool on = (ec::written >> 12 & 1) != 0;
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
      const bool changed = ((ec::written ^ before) >> 12 & 1) != 0;
      wrong += (changed != !(offset > 0 && offset < 5)) ? 1 : 0;
      if (changed) on = !on;
      CHECK(SetThreadContext(thread, &saved) != FALSE);
      ResumeThread(thread);
    }
    for (int tries = 0; !on && tries < static_cast<int>(ec::MAX_FAILED_WRITES); ++tries) {
      twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true);
      on = (ec::written >> 12 & 1) != 0;
    }
    stop = true;
    parked.join();
    CloseHandle(thread);
    CHECK(wrong == 0 && on);
    std::printf("   a parked thread at the site -1 .. +6: the site is refused at +1 .. +4 and written at the others\n");
  }

  // 15f. Timing. Particles of four lines (256 bytes) in 512 MB, each emitter 256 particles picked at random (a fresh set for every run, so no
  // variant finds what another left in the cache); the update loads the four lines and multiplies `busy` times. The game's copy and ours in the
  // two images, and the stub for K = 1 .. 4 in copies of the loop; ns a particle, the median of 7 rounds.
  {
    constexpr size_t PARTICLES = 1u << 21;
    constexpr uint32_t PER_EMITTER = 256;
    constexpr int EMITTERS = 300;
    constexpr int ROUNDS = 7;
    constexpr size_t VARIANTS = 7;  // game, ours, K = 1 .. 4 (scratch copies of the loop), the scratch copy without the stub
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, PARTICLES * 256, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECK(memory != nullptr);
    for (size_t index = 0; index < PARTICLES; ++index) {
      Write32(memory + index * 256, Address(loop_busy_vtable));
      memory[index * 256 + 0x64] = 1;
    }
    // Scratch copies of the loop: the game's 104 bytes, the harness's way out after them, the stub for K behind.
    auto* scratch = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    CHECK(scratch != nullptr);
    std::array<uint32_t, VARIANTS> targets = {game_top, patched_top, 0, 0, 0, 0, 0};
    for (size_t variant = 2; variant < VARIANTS; ++variant) {
      uint8_t* copy = scratch + 0x100 * (variant - 2);
      std::memcpy(copy, game + loop_rva, ec::PARTICLE_LOOP_EXIT);
      JumpTo(copy + ec::PARTICLE_LOOP_EXIT, reinterpret_cast<const void*>(&ParticleLoopReturn));
      if (variant < 6) {  // K = variant - 1: the site jumps to the stub, whose two jumps go to the copy's top and its way out
        const auto ahead_stub = ec::ParticlePrefetchStub(static_cast<uint8_t>(variant - 1));
        uint8_t* ahead_code = copy + 0x80;
        std::memcpy(ahead_code, ahead_stub.data(), ahead_stub.size());
        Write32(ahead_code + ahead_stub.size() - 9, Address(copy) - Address(ahead_code + ahead_stub.size() - 5));
        Write32(ahead_code + ahead_stub.size() - 4, Address(copy + ec::PARTICLE_LOOP_EXIT) - Address(ahead_code + ahead_stub.size()));
        JumpTo(copy + ec::PARTICLE_LOOP_SITE, ahead_code);
      }
      targets[variant] = Address(copy);
    }
    FlushInstructionCache(GetCurrentProcess(), scratch, 0x1000);
    std::vector<uint32_t> permutation(PARTICLES);
    for (size_t index = 0; index < PARTICLES; ++index) permutation[index] = static_cast<uint32_t>(index);
    std::vector<uint32_t> pointers(static_cast<size_t>(EMITTERS) * PER_EMITTER);
    auto call = std::make_unique<LoopCall>();
    CHECK(Address(call.get()) % 16 == 0);
    alignas(16) std::array<uint8_t, 0x80> emitter = {};
    alignas(16) std::array<uint8_t, 0x20> args = {};
    for (uint32_t& value : call->in) value = 0;
    call->in[5] = Address(emitter.data());
    call->in[6] = Address(args.data());
    call->flags_in = 2;
    // The same 76800 particles in emitters of `per_emitter` each (a different random set every call), or the same 256 particles in every emitter (hot).
    const auto run_emitters = [&](size_t variant, bool hot, uint32_t per_emitter) {
      for (size_t index = 0; index < pointers.size(); ++index) {
        if (hot) {
          pointers[index] = Address(memory + (index % PER_EMITTER) * 256);
        } else {  // a partial shuffle: other particles every slot
          const size_t other = index + random.Next() % (PARTICLES - index);
          std::swap(permutation[index], permutation[other]);
          pointers[index] = Address(memory + static_cast<size_t>(permutation[index]) * 256);
        }
      }
      const size_t emitters = pointers.size() / per_emitter;
      const auto start = std::chrono::steady_clock::now();
      for (size_t emitter_index = 0; emitter_index < emitters; ++emitter_index) {
        Write32(emitter.data() + 0x64, per_emitter);
        Write32(emitter.data() + 0x68, Address(pointers.data() + emitter_index * per_emitter));
        call->target = targets[variant];
        EnterParticleLoop(call.get());
      }
      return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / static_cast<double>(emitters * per_emitter);
    };
    std::string lines;
    // The update of 100 multiplies is about 300 cycles (the line says its ns hot); the emitters of 8 and 32 show what the first particles of an emitter,
    // which nothing prefetched, cost.
    for (const auto& [busy, per_emitter] : {std::pair{1u, 256u}, std::pair{40u, 256u}, std::pair{100u, 256u}, std::pair{200u, 256u}, std::pair{100u, 32u}, std::pair{100u, 8u}}) {
      loop_busy = busy;
      std::array<std::vector<double>, VARIANTS> ns;
      for (int round = 0; round < ROUNDS; ++round) {
        for (size_t variant = 0; variant < VARIANTS; ++variant) ns[variant].push_back(run_emitters(variant, false, per_emitter));
      }
      std::array<double, VARIANTS> median = {};
      for (size_t variant = 0; variant < VARIANTS; ++variant) {
        std::sort(ns[variant].begin(), ns[variant].end());
        median[variant] = ns[variant][ROUNDS / 2];
      }
      // Hot: the copy without the stub (the update alone) and with K = 2 (what the stub costs when nothing misses); one run to warm the 256
      // particles up, then the median of 7 rounds each, the two taking turns.
      run_emitters(6, true, PER_EMITTER);
      std::array<std::vector<double>, 2> hot_ns;
      for (int round = 0; round < ROUNDS; ++round) {
        hot_ns[0].push_back(run_emitters(6, true, PER_EMITTER));
        hot_ns[1].push_back(run_emitters(3, true, PER_EMITTER));
      }
      for (std::vector<double>& values : hot_ns) std::sort(values.begin(), values.end());
      lines += std::format("   ns a particle, update of {} multiplies (hot: no stub {:.1f}, K=2 {:.1f}), emitters of {}: game {:.1f}, ours (K = {}) {:.1f}, copies: no stub {:.1f}, K=1 {:.1f}, "
                           "K=2 {:.1f}, K=3 {:.1f}, K=4 {:.1f}\n",
                           busy, hot_ns[0][ROUNDS / 2], hot_ns[1][ROUNDS / 2], per_emitter, median[0], static_cast<uint32_t>(ec::PARTICLE_PREFETCH_AHEAD), median[1],
                           median[6], median[2], median[3], median[4], median[5]);
    }
    std::fputs(lines.c_str(), stdout);
    VirtualFree(memory, 0, MEM_RELEASE);
    VirtualFree(scratch, 0, MEM_RELEASE);
  }
  VirtualFree(ends[0] - 0x1000, 0, MEM_RELEASE);
  VirtualFree(ends[1] - 0x1000, 0, MEM_RELEASE);
  std::memcpy(game + exit_rva, exit_game.data(), 5);
  std::memcpy(patched + exit_rva, exit_ours.data(), 5);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  return 0;
}
