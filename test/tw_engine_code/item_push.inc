// ====================================================================================================================
// 19. The render lists' item push loops (Attila +0x17b83a0 head +0x17b8490 "A" and +0x17b7bc0 head +0x17b7cb0 "B", Thrones the same bytes; Faster Game CPU Code;
// engine_code.hpp 15.), included inside tests.cpp's anonymous namespace after entry_prefetch.inc (LoopCall, EnterParticleLoop and ParticleLoopReturn of part 15,
// JumpTo, HoldSites and ParkedAtSites are used). Each loop pushes `item + 0x50` (A) or `item + 0x38` (B) of the next entry of a list's item array into a vector
// of 4-byte slots {+0x7c capacity, +0x80 size, +0x84 data} at edi, one item a round, with the vector's growth (a doubling copy), the per-item call (edx != 0)
// and the aliasing check of `push_back` in the loop. The stub at the head runs the rounds but the last with the size in a register (a guard first):
// 19a. the sites and the stubs as Prepare made them (jumps, bytes, the guard);
// 19b. the guard against an exact model: no run's store may reach a location the stub doesn't load again (the object's three fields, the list's item array field,
//    the stack from the stub's pushes to the loop's slots); never accepted when the model sees an overlap (millions of tuples around every anchor), the
//    clauses' mutants must each be accepted wrongly;
// 19c. the whole loops, both copies of the DLL (the calls of the allocator, the free and the per-item callback pointed at fakes that log them, the exit at the
//    loop's end hooked to the harness, entered at the head with a fake frame on the thread's stack): random lists (0 to 5,000 items, starts from 0 to the top of
//    the address space through the wrap, ends at or below the start), vectors (capacity 0, 1 .., the size at capacity - 1 / capacity / above it, a capacity of
//    0x100000 and more, 0x4000000k with the block's first stored slot on a field in the overlap layouts), growth in the middle, per-item calls that stop after some call, the vector's block and the item array ending at a no-access page (the same fault, the same
//    memory), and layouts in which the block, the vector's object or the list overlap each other or the frame (the guard sends those to the game's code): every
//    register, the flags, esp, xmm0-7, the frame above esp, every byte of the world and the log of the fakes' calls equal (or both fault with the same memory);
// 19d. CONTROLs: mutants of the stubs and of the guard, each must be seen;
// 19e. 200 switches under 4 threads that run both loops in both copies, a parked thread at each byte of each site;
// 19f. timing: the loops for 1 to 4,096 items, game / stub.
// To reproduce a failing scenario (`engine_code.exe <dll> [quick] push`): ITEM_PUSH_SEED (hex) and ITEM_PUSH_FIRST / ITEM_PUSH_LAST run 19c's scenarios of that range
// alone, ITEM_PUSH_EXPLAIN prints what differs, ITEM_PUSH_TRACE the spec and each run's exceptions, ITEM_PUSH_THREADS_ONLY runs 19e alone.
// ====================================================================================================================

constexpr uint32_t PUSH_ITEMS = 5200;      // the item array's entries
constexpr uint32_t PUSH_SLOTS = 8192;      // the vector's own block
constexpr uint32_t PUSH_ARENA = 40000;     // the fake allocator's slots
constexpr uint32_t PUSH_LOG = 96;          // dwords of the log (kind, value) pairs
constexpr uint32_t PUSH_EDI = 0x40;        // edi = vec + PUSH_EDI
constexpr uint32_t PUSH_LOCALS = 0x100;    // the frame's bytes above esp (LoopCall's esp is at frame + 0x100)
constexpr uint32_t PUSH_EDGE_BYTES = 0x1000;
// The run's frame lives in an array on the thread's stack (an access violation is dispatched on the stack below esp: room below the frame for the dispatcher, the
// array's own, or it runs into the frames below the array, which hold the __try's registration record).
constexpr uint32_t PUSH_ARENA_BYTES = 0x5000;
constexpr uint32_t PUSH_ARENA_CALL = 0x4000;

struct ItemPushWorld {
  LoopCall call;
  alignas(16) uint8_t vec[0x180];
  alignas(16) uint8_t list[0x40];
  alignas(16) uint8_t object[0x40];     // what edx points at for the per-item call
  alignas(16) uint8_t items[8][0x80];   // the items of the runs with calls (the callback path of A loads [item + 0x38])
  uint32_t log_count;
  uint32_t arena_used;
  uint32_t callbacks;
  uint32_t zero_after;  // the callback clears [esp + 0x3c] (edx) after this many calls
  uint32_t edx_slot;    // the address of this run's [esp + 0x3c]
  uint32_t unused[3];
  uint32_t log[PUSH_LOG];
  alignas(16) uint8_t edge_snapshot[PUSH_EDGE_BYTES];  // the page that ends at a no-access page, as the run starts and ends
  uint32_t item_array[PUSH_ITEMS + 4];  // PushSetup fills rounds + 2 entries
  uint32_t block[PUSH_SLOTS + 16];
  uint32_t arena[PUSH_ARENA];
};

thread_local ItemPushWorld* push_world = nullptr;

// A world in committed pages that end exactly where a no-access page begins (and one before them): a runaway store or copy (a layout can overwrite the vector's size
// field with an item) faults at the world's end instead of overwriting the next heap block.
constexpr size_t PUSH_WORLD_PAGES = (sizeof(ItemPushWorld) + 0xFFF) / 0x1000;
ItemPushWorld* PushAllocWorld() {
  auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, (PUSH_WORLD_PAGES + 2) * 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (base == nullptr) return nullptr;
  DWORD protection;
  VirtualProtect(base, 0x1000, PAGE_NOACCESS, &protection);
  VirtualProtect(base + (PUSH_WORLD_PAGES + 1) * 0x1000, 0x1000, PAGE_NOACCESS, &protection);
  return reinterpret_cast<ItemPushWorld*>(base + (PUSH_WORLD_PAGES + 1) * 0x1000 - sizeof(ItemPushWorld));
}
void PushFreeWorld(ItemPushWorld* world) {
  VirtualFree(reinterpret_cast<uint8_t*>(world) + sizeof(ItemPushWorld) - (PUSH_WORLD_PAGES + 1) * 0x1000, 0, MEM_RELEASE);
}

// What a crash outside the guarded runs says: the scenario under way and where it was.
uint64_t push_scenario_now = 0;
uint32_t push_layout_now = 0;


void PushLog(uint32_t kind, uint32_t value) {
  ItemPushWorld* world = push_world;
  if (world->log_count < PUSH_LOG / 2) {
    world->log[2 * world->log_count] = kind;
    world->log[2 * world->log_count + 1] = value;
  }
  ++world->log_count;
}

// The game's allocator (cdecl, `slots` 4-byte slots and a zero; null if the arena has no room) and free, and the per-item call (thiscall, ecx the object, one
// stack argument): they log their arguments, the callback clears the frame's edx slot after `zero_after` calls.
uint32_t* __cdecl PushAlloc(uint32_t slots, uint32_t) {
  ItemPushWorld* world = push_world;
  PushLog(0xA0000000u | (slots & 0xFFFFFF), world->arena_used);
  if (slots > PUSH_ARENA - world->arena_used) return nullptr;
  uint32_t* block = world->arena + world->arena_used;
  world->arena_used += slots;
  return block;
}
void __cdecl PushFree(uint32_t* block) { PushLog(0xF0000000u, Address(block)); }
void __fastcall PushCallback(void* object, void*, uint32_t argument) {
  ItemPushWorld* world = push_world;
  PushLog(0xC0000000u | (Address(object) & 0xFFFFFF), argument);
  if (++world->callbacks == world->zero_after) *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(world->edx_slot)) = 0;
}

struct PushSpec {
  bool b;
  uint32_t start;     // ebp at the head
  uint32_t end;
  uint32_t size;
  uint32_t capacity;
  uint32_t mode;      // 0: edx = 0; 1: edx the object (calls every round); 2: the callback clears edx after `zero_after` calls
  uint32_t zero_after;
  uint32_t layout;
  uint32_t shift;
};

thread_local PushSpec push_spec_now = {};
thread_local ItemPushWorld* push_world_now = nullptr;

enum PushLayout : uint32_t {
  PUSH_PLAIN,
  PUSH_BLOCK_ON_FIELDS,  // the block overlaps the vector object's capacity / size / data
  PUSH_BLOCK_ON_LIST,    // the block overlaps the list's item array field
  PUSH_BLOCK_ON_FRAME,   // the block overlaps the loop's stack slots
  PUSH_OBJECT_ON_FRAME,  // the vector's object overlaps the stack slots
  PUSH_LIST_ON_FRAME,    // the list overlaps the stack slots
  PUSH_LIST_ON_DATA,     // the list's item array field is the vector's data field (the same pointer)
  PUSH_ARRAY_ON_BLOCK,   // the item array lies in the vector's own block (source and destination overlap)
  PUSH_BLOCK_EDGE,       // the block ends at a no-access page
  PUSH_ITEMS_EDGE,       // the item array ends at a no-access page
  PUSH_LAYOUTS
};

// The rounds the game's loop makes from `start` to `end`: do { ++ebp } while (ebp < end), unsigned, capped.
uint32_t PushRounds(uint32_t start, uint32_t end) {
  uint32_t rounds = 0;
  uint32_t ebp = start;
  do {
    ++rounds;
    ++ebp;
  } while (ebp < end && rounds <= PUSH_ITEMS);
  return rounds;
}

constexpr std::array<uint32_t, 6> HUGE_CAPACITIES = {0x100000, 0x100001, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFF};

PushSpec PushRandomSpec(Random* random) {
  PushSpec spec = {.b = (random->Next() & 1) != 0};
  spec.layout = (random->Next() % 100 < 80 ? PUSH_PLAIN : 1 + random->Next() % (PUSH_LAYOUTS - 1));
  uint32_t rounds;
  const uint32_t pick = random->Next() % 100;
  if (pick < 35) {
    rounds = 1 + random->Next() % 12;
  } else if (pick < 65) {
    rounds = 13 + random->Next() % 190;
  } else if (pick < 90) {
    rounds = 200 + random->Next() % 1300;
  } else {
    rounds = 1500 + random->Next() % (PUSH_ITEMS - 1500);
  }
  if (spec.layout != PUSH_PLAIN) rounds = 1 + random->Next() % 40;
  switch (random->Next() % 12) {
    case 8:
      spec.start = random->Next() % 5000;
      spec.end = spec.start + rounds;
      break;
    case 9:
      spec.start = random->Next() % (0xFFFFFFFFu - rounds);
      spec.end = spec.start + rounds;
      break;
    case 10: {  // the index wraps through 0xFFFFFFFF
      const uint32_t before = random->Next() % 4;
      rounds = std::max(rounds, before + 1);
      spec.start = 0xFFFFFFFFu - before;
      spec.end = rounds - before - 1;
      break;
    }
    case 11:  // ebp at the head is not below the end
      spec.start = (random->Next() & 1) != 0 ? random->Next() % 64 : random->Next();
      spec.end = spec.start - std::min(spec.start, random->Next() % 3 == 0 ? 0u : random->Next() % 4);  // never wraps below 0: that would be a run of 4 billion rounds
      break;
    default:
      spec.start = (random->Next() % 3 == 0 ? random->Next() % 4 : 0);
      spec.end = spec.start + rounds;
      break;
  }
  // The item array's base (item_array - 4 * start), an item (above 2 GB) or a stack address ends up in a slot the loop reads as a pointer or a count in the layouts
  // that overlap the vector, the list or the frame: a low start keeps the base inside the world and an item plus 4 * the index above 2 GB (a huge index would wrap it
  // into another thread's stack), and the stack addresses stay out of the vector's fields (PushSetup), so both runs read the same memory.
  if (spec.layout >= PUSH_BLOCK_ON_FIELDS && spec.layout <= PUSH_LIST_ON_DATA) {
    spec.start %= 16;
    spec.end = spec.start + rounds;
  }
  const uint32_t actual = PushRounds(spec.start, spec.end);
  // The vector.
  const uint32_t profile = random->Next() % 12;
  uint32_t size = 0;
  uint32_t capacity = 0;
  switch (profile) {
    case 0:
      break;
    case 1:
    case 2:
      capacity = 1 + random->Next() % 8;
      size = random->Next() % (capacity + 1);
      break;
    case 3:
    case 4:
    case 5:  // fills up exactly, one short or one over
      size = random->Next() % (std::min(actual, 400u) + 1);
      capacity = static_cast<uint32_t>(std::max<int64_t>(1, static_cast<int64_t>(size) + actual + static_cast<int>(random->Next() % 5) - 2));
      break;
    case 6:
    case 7:
      size = random->Next() % 400;
      capacity = size + actual + random->Next() % 64;
      break;
    case 8:
    case 9:
      capacity = 1 + random->Next() % 300;
      size = random->Next() % (capacity + 1);
      break;
    case 10:  // a capacity the stub leaves alone
      capacity = HUGE_CAPACITIES[random->Next() % HUGE_CAPACITIES.size()];
      size = random->Next() % 64;
      break;
    default:
      capacity = 1 + random->Next() % 40;
      size = capacity - (random->Next() % 3 == 0 ? 0 : 1);
      if (random->Next() % 4 == 0) size = capacity + 1 + random->Next() % 3;  // above the capacity: the stubs' `cmp ecx, esi; jae` leaves it to the game
      break;
  }
  if (spec.layout >= PUSH_BLOCK_ON_FIELDS && spec.layout <= PUSH_ARRAY_ON_BLOCK) {
    capacity = 2 + random->Next() % 11;
    size = random->Next() % capacity;
    switch (random->Next() % 8) {
      case 0:  // above the capacity
        size = capacity + 1 + random->Next() % 3;
        break;
      case 1:  // a capacity whose 4 * capacity wraps to a few slots (the stubs' `cmp esi, 0x100000; jae`: the guard's extent would miss the stores, see PushSetup)
        if (spec.layout == PUSH_BLOCK_ON_FIELDS || spec.layout == PUSH_BLOCK_ON_LIST) {
          capacity = 0x40000000 + random->Next() % 4;
          size = 4 + random->Next() % 40;
        }
        break;
      default:
        break;
    }
  }
  spec.size = size;
  spec.capacity = capacity;
  const uint32_t mode = random->Next() % 20;
  spec.mode = (mode < 14 ? 0 : (mode < 17 ? 1 : 2));
  spec.zero_after = 1 + random->Next() % std::max(actual, 1u);
  spec.shift = random->Next();
  return spec;
}

// The world as the head sees it: the frame (random above esp, then the loop's slots), the vector object, the list and its item array, the registers; the
// vector's block and the item array wherever the layout puts them. `frame_esp` is the run's esp (the frame will sit there when the run starts), `edge` the page
// ending at a no-access page. The loop's slots: [esp + 0x1c] the list, [esp + 0x34] the end (A) / [esp + 0x30] (B), [esp + 0x3c] edx, [esp + 0x40] edi.
void PushSetup(ItemPushWorld* world, uint32_t frame_esp, uint8_t* edge, const PushSpec& spec, Random* random) {
  uint8_t* esp = world->call.frame + PUSH_LOCALS;
  for (uint32_t at = 0; at < PUSH_LOCALS; at += 4) Write32(esp + at, random->Next());
  const uint32_t rounds = PushRounds(spec.start, spec.end);
  const bool small_items = spec.layout >= PUSH_BLOCK_ON_FIELDS && spec.layout <= PUSH_LIST_ON_DATA;
  uint32_t edi = Address(world->vec) + PUSH_EDI;
  uint32_t list = Address(world->list);
  uint32_t data = Address(world->block);
  uint32_t array = Address(world->item_array) - 4 * spec.start;
  uint32_t capacity = spec.capacity;
  uint32_t size = spec.size;
  uint32_t array_entries_at = Address(world->item_array);  // entry 0's address
  const uint32_t edge_end = Address(edge) + PUSH_EDGE_BYTES;
  uint32_t readable = 0;
  switch (spec.layout) {
    // Overlaps by whole slots (an item stored over a field's part would make a pointer of the neighbours' bytes: a wild store into the test's own memory),
    // from the block that ends where the fields start to the one that starts where they end.
    // With a capacity of 0x4000000k the block's 4 * capacity wraps to 4k bytes, which a guard without the stubs' `cmp esi, 0x100000; jae` would take for the block's
    // extent: the first slot a run stores (slot `size`) is put on a field, 4 or more slots past the wrapped extent.
    case PUSH_BLOCK_ON_FIELDS:
      data = edi + 0x7C + 4 * (spec.shift % (capacity + 4)) - 4 * capacity;
      if (capacity >= 0x40000000) data = edi + 0x7C + 4 * (spec.shift % 3) - 4 * size;
      break;
    case PUSH_BLOCK_ON_LIST:
      data = list + 8 + 4 * (spec.shift % (capacity + 2)) - 4 * capacity;
      if (capacity >= 0x40000000) data = list + 8 + 4 * (spec.shift % 3) - 4 * size;
      break;
    // On the frame: from esp up (what is below esp is dead: the game's own calls and any exception overwrite it).
    case PUSH_BLOCK_ON_FRAME:
      data = frame_esp + 4 * (spec.shift % 0x31);
      break;
    // The fields never overlap the slot of edi (a stack address as the size or the data pointer: the growth would copy the stack above the frame, which differs
    // between the two runs), nor the list's item array field.
    case PUSH_OBJECT_ON_FRAME: {
      uint32_t slot = spec.shift % 0x3C;
      slot += (slot >= 14 && slot <= 16 ? 3 : 0);
      edi = frame_esp - 0x7C + 4 * slot;
      break;
    }
    case PUSH_LIST_ON_FRAME: {
      uint32_t slot = spec.shift % 0x3E;
      slot += (slot == 16 ? 1 : 0);
      list = frame_esp - 8 + 4 * slot;
      break;
    }
    case PUSH_LIST_ON_DATA:
      list = edi + 0x7C;
      array = data;
      break;
    case PUSH_ARRAY_ON_BLOCK:
      array_entries_at = Address(world->block) + 4 * ((spec.shift / 64) % 9);
      array = array_entries_at - 4 * spec.start;
      break;
    case PUSH_BLOCK_EDGE:
      readable = spec.shift % std::min(rounds + capacity + 4, PUSH_EDGE_BYTES / 4);  // at most the page's slots: the block never starts below the page
      data = edge_end - 4 * readable;
      capacity = size + rounds + 2;
      break;
    case PUSH_ITEMS_EDGE:
      readable = spec.shift % std::min(rounds + 1, PUSH_EDGE_BYTES / 4);
      array_entries_at = edge_end - 4 * readable;
      array = array_entries_at - 4 * spec.start;
      break;
    default:
      break;
  }
  // The world's copy of an address (the frame's bytes are copied onto the thread's stack when the run starts).
  const auto world_at = [&](uint32_t address) {
    const uint32_t from_frame = address - (frame_esp - PUSH_LOCALS);
    if (from_frame < 2 * PUSH_LOCALS) return esp - PUSH_LOCALS + from_frame;
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address));
  };
  const auto fill_edge = [&] {
    for (uint32_t at = 0; at < PUSH_EDGE_BYTES; at += 4) Write32(world->edge_snapshot + at, random->Next());
  };
  if (spec.layout == PUSH_BLOCK_EDGE || spec.layout == PUSH_ITEMS_EDGE) fill_edge();
  // The block's first `size` slots, then the item array's entries (the array can lie in the block: written last, its entries are the items).
  // (In the layouts where a slot of the block can end up as the list's pointer, the block holds no valid pointers: the loop reads [that + 4 * index] and a random
  // word would be some other thread's stack.)
  for (uint32_t slot = 0; slot < (small_items ? 64u : std::min(size, PUSH_SLOTS)) && spec.layout != PUSH_BLOCK_EDGE; ++slot) {
    if (spec.layout == PUSH_ARRAY_ON_BLOCK) break;
    world->block[slot] = (small_items ? 0x80000000u : 0u) | random->Next();
  }
  if (spec.layout == PUSH_ARRAY_ON_BLOCK) {
    for (uint32_t slot = 0; slot < PUSH_SLOTS && slot < capacity + 64; ++slot) world->block[slot] = random->Next();
  }
  const bool calls = (spec.mode != 0);
  for (uint32_t entry = 0; entry < rounds + 2; ++entry) {
    uint32_t value = random->Next();
    if (calls) value = Address(world->items[random->Next() % 8]);
    if (small_items) value = 0x80000000u | (0x20 + (random->Next() & 0xFFF));  // above 2 GB: a field that gets one is no valid pointer
    uint8_t* at = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(array_entries_at + 4 * entry));
    if (spec.layout == PUSH_ITEMS_EDGE) {
      if (entry >= readable) continue;
      at = world->edge_snapshot + PUSH_EDGE_BYTES - 4 * readable + 4 * entry;
    }
    Write32(at, value);
  }
  // Frame slots first or the objects first: where they overlap, the later writes win.
  const auto write_objects = [&] {
    Write32(world_at(edi + 0x7C), capacity);
    Write32(world_at(edi + 0x80), size);
    Write32(world_at(edi + 0x84), data);
    Write32(world_at(list + 8), array);
  };
  const auto write_slots = [&] {
    Write32(esp + 0x1C, list);
    Write32(esp + (spec.b ? 0x30 : 0x34), spec.end);
    Write32(esp + 0x3C, spec.mode != 0 ? Address(world->object) : 0u);
    Write32(esp + 0x40, edi);
  };
  if (spec.layout == PUSH_PLAIN || spec.layout >= PUSH_BLOCK_EDGE || (spec.shift & 1) != 0) {
    write_slots();
    write_objects();
  } else {
    write_objects();
    write_slots();
  }
  world->log_count = 0;
  world->arena_used = 0;
  world->callbacks = 0;
  world->zero_after = (spec.mode == 2 ? spec.zero_after : 0xFFFFFFFFu);
  world->edx_slot = frame_esp + 0x3C;
  std::memset(world->log, 0, sizeof(world->log));
  for (uint32_t& value : world->call.in) value = random->Next();
  world->call.in[2] = (spec.mode != 0 ? Address(world->object) : 0u);
  world->call.in[3] = (spec.b ? spec.end : world->call.in[3]);
  world->call.in[5] = edi;
  world->call.in[6] = spec.start;
  world->call.flags_in = (random->Next() & 0x8D5) | 2;
}

// Scenario `index` of the seed: the spec, the world (in `world`).
PushSpec PushScenario(ItemPushWorld* world, uint32_t frame_esp, uint8_t* edge, uint64_t seed, uint64_t index) {
  Random random = {.state = (seed + index) * 0x9E3779B97F4A7C15ull | 1};
  for (int warm = 0; warm < 4; ++warm) random.Next();
  const PushSpec spec = PushRandomSpec(&random);
  push_spec_now = spec;
  push_world_now = world;
  static const bool trace = std::getenv("ITEM_PUSH_TRACE") != nullptr;
  if (trace) std::fprintf(stderr, "spec %llu: layout %u start %08X end %08X size %u capacity %08X shift %08X\n", index, spec.layout, spec.start, spec.end, spec.size, spec.capacity, spec.shift);
  PushSetup(world, frame_esp, edge, spec, &random);
  return spec;
}

// A run on the heap (valid worlds only: the threads and the timing).
void PushRunHeap(ItemPushWorld* world, uint32_t target) {
  push_world = world;
  world->call.target = target;
  EnterParticleLoop(&world->call);
}

// Equal bytes, but a dword that differs by exactly the distance between the two copies of the DLL is a return address a call left behind (as in the other parts: a
// corrupted vector can copy dead stack).
bool PushWordsEqual(const uint8_t* x, const uint8_t* y, size_t size, uint32_t copy_distance) {
  if (std::memcmp(x, y, size) == 0) return true;
  for (size_t at = 0; at + 4 <= size; at += 4) {
    const uint32_t u = Read32(x + at);
    const uint32_t v = Read32(y + at);
    if (u != v && v - u != copy_distance) return false;
  }
  return true;
}

// "" if the two runs agree (the registers, flags, esp and xmm only where both finished).
const char* PushCompare(const ItemPushWorld& a, const ItemPushWorld& b, bool a_done, bool b_done, uint32_t copy_distance) {
  if (a_done != b_done) return "ONE of the copies faulted";
  if (a_done) {
    for (size_t reg = 0; reg < 7; ++reg) {
      if (a.call.out[reg] != b.call.out[reg]) return "a register";
    }
    if (a.call.esp_after != b.call.esp_after) return "esp";
    if (a.call.flags_out != b.call.flags_out) return "the flags";
    if (std::memcmp(a.call.xmm_out, b.call.xmm_out, sizeof(a.call.xmm_out)) != 0) return "xmm";
  }
  if (!PushWordsEqual(a.call.frame + PUSH_LOCALS, b.call.frame + PUSH_LOCALS, PUSH_LOCALS, copy_distance)) return "the frame above esp";
  if (!PushWordsEqual(a.vec, b.vec, sizeof(ItemPushWorld) - offsetof(ItemPushWorld, vec), copy_distance)) return "memory (the vector, the list, the arena or the log)";
  return "";
}

// What differs between two runs, for a failing scenario (ITEM_PUSH_EXPLAIN): the registers, then the first dwords of memory with the member they are in.
void PushExplain(const ItemPushWorld& a, const ItemPushWorld& b) {
  const std::array<const char*, 7> names = {"eax", "ecx", "edx", "ebx", "esi", "edi", "ebp"};
  for (size_t reg = 0; reg < 7; ++reg) {
    if (a.call.out[reg] != b.call.out[reg]) std::fprintf(stderr, "    %s: game %08X ours %08X\n", names[reg], a.call.out[reg], b.call.out[reg]);
  }
  std::fprintf(stderr, "    flags game %08X ours %08X esp game %08X ours %08X; finished: game %u ours %u\n", a.call.flags_out, b.call.flags_out, a.call.esp_after, b.call.esp_after,
               a.unused[0], b.unused[0]);
  for (const ItemPushWorld* world : {&a, &b}) {
    const uint8_t* object = world->vec + PUSH_EDI;
    std::fprintf(stderr, "    %s: capacity %08X size %08X data %08X (block %08X arena %08X); log (%u calls):", world == &a ? "game" : "ours", Read32(object + 0x7C), Read32(object + 0x80),
                 Read32(object + 0x84), Address(world->block), Address(world->arena), world->log_count);
    for (uint32_t entry = 0; entry < std::min<uint32_t>(world->log_count, PUSH_LOG / 2); ++entry) std::fprintf(stderr, " %08X/%08X", world->log[2 * entry], world->log[2 * entry + 1]);
    std::fprintf(stderr, "\n");
  }
  const auto* x = reinterpret_cast<const uint8_t*>(&a);
  const auto* y = reinterpret_cast<const uint8_t*>(&b);
  struct Member {
    const char* name;
    size_t at;
  };
  const std::array<Member, 12> members = {{{"frame", offsetof(ItemPushWorld, call.frame)}, {"vec", offsetof(ItemPushWorld, vec)}, {"list", offsetof(ItemPushWorld, list)},
                                           {"object", offsetof(ItemPushWorld, object)}, {"items", offsetof(ItemPushWorld, items)}, {"counters", offsetof(ItemPushWorld, log_count)},
                                           {"log", offsetof(ItemPushWorld, log)}, {"edge", offsetof(ItemPushWorld, edge_snapshot)}, {"item_array", offsetof(ItemPushWorld, item_array)},
                                           {"block", offsetof(ItemPushWorld, block)}, {"arena", offsetof(ItemPushWorld, arena)}, {"end", sizeof(ItemPushWorld)}}};
  int shown = 0;
  for (size_t at = offsetof(ItemPushWorld, call.frame); at + 4 <= sizeof(ItemPushWorld) && shown < 24; at += 4) {
    if (Read32(x + at) == Read32(y + at)) continue;
    size_t member = 0;
    while (member + 1 < members.size() && members[member + 1].at <= at) ++member;
    std::fprintf(stderr, "    %s+0x%zX: game %08X ours %08X\n", members[member].name, at - members[member].at, Read32(x + at), Read32(y + at));
    ++shown;
  }
}

// Scenario `index` of `seed` in both copies of the DLL: the spec and the world are made in `world` (a copy goes to `backup`), the game's loop runs on it (the world
// after goes to `result`), then the patched copy's on the world as it was (`world` ends as that run left it). Each run's frame is in an array on this function's own
// stack at a fixed place (the world's overlap layouts point into it): an access violation is dispatched on the stack below esp and unwinds to the __try's frame, so
// that frame must lie above the array (a callee's frame would be run into). Returns "" if the two runs agree, else what differs; `faulted`: the game's run faulted.
const char* PushBoth(ItemPushWorld* world, ItemPushWorld* backup, ItemPushWorld* result, uint8_t* edge, uint64_t seed, uint64_t index,
                     const std::array<uint32_t, 2>& game_heads, const std::array<uint32_t, 2>& patched_heads, PushSpec* spec_out, bool* faulted) {
  alignas(16) uint8_t arena[PUSH_ARENA_BYTES];
  auto* on_stack = reinterpret_cast<LoopCall*>(arena + PUSH_ARENA_CALL);
  push_scenario_now = index;
  *spec_out = PushScenario(world, Address(on_stack) + LOOP_ESP, edge, seed, index);
  push_layout_now = spec_out->layout;
  *backup = *world;
  bool done[2] = {true, true};
  for (int copy = 0; copy < 2; ++copy) {
    *world = *backup;
    push_world = world;
    std::memcpy(edge, world->edge_snapshot, PUSH_EDGE_BYTES);
    std::memcpy(on_stack, &world->call, sizeof(LoopCall));
    on_stack->target = (copy == 0 ? game_heads : patched_heads)[spec_out->b];
    static const bool run_trace = (std::getenv("ITEM_PUSH_TRACE") != nullptr);
    if (run_trace) {
      std::fprintf(stderr, "  run %d: flags_in %08X target %08X backup flags %08X world flags %08X on_stack %p\n", copy, on_stack->flags_in, on_stack->target, backup->call.flags_in,
                   world->call.flags_in, on_stack);
    }
    __try {
      EnterParticleLoop(on_stack);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
      done[copy] = false;
    }
    std::memcpy(&world->call, on_stack, sizeof(LoopCall));
    world->unused[0] = done[copy] ? 1 : 0;
    if (done[copy]) world->call.esp_after -= Address(on_stack);
    std::memcpy(world->edge_snapshot, edge, PUSH_EDGE_BYTES);
    if (copy == 0) *result = *world;
  }
  *faulted = !done[0];
  return PushCompare(*result, *world, done[0], done[1], patched_heads[0] - game_heads[0]);
}

// The guard (the page's `guard`) called with esp = `site_esp` as the stubs call it (the call and the guard's push go below it): eax the list, edx the data,
// esi the capacity, edi the vector's object; returns its esi (0: no store of a run reaches a location the stub doesn't load again).
__declspec(naked) uint32_t __cdecl PushGuardCall(uint32_t guard, uint32_t list, uint32_t data, uint32_t capacity, uint32_t object, uint32_t site_esp) {
  __asm {
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, esp
    mov eax, [ebp + 24]
    mov edx, [ebp + 28]
    mov esi, [ebp + 32]
    mov edi, [ebp + 36]
    mov ebx, [ebp + 20]
    mov esp, [ebp + 40]
    call ebx
    mov esp, ebp
    mov eax, esi
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
  }
}

// The exact model: does a store of a run (any slot of the block, the size field, the loop's scratch slots) reach a location the stub loads once and keeps (the list
// pointer on the stack, the list's item array field, the capacity, size and data fields, A's end / B's limit slot)? The size store is the size load's own place.
bool PushOverlaps(uint32_t list, uint32_t data, uint32_t capacity, uint32_t object, uint32_t site_esp, bool b) {
  struct Range {
    uint64_t lo;
    uint64_t hi;
  };
  const auto range = [](uint64_t at, uint64_t size) { return Range{at, at + size}; };
  const auto overlap = [](const Range& x, const Range& y) { return x.lo < y.hi && y.lo < x.hi; };
  const std::array<Range, 4> stores = {range(data, 4ull * capacity), range(object + 0x80ull, 4), range(site_esp + 0x14ull, 4), range(site_esp + 0x30ull, 4)};
  const size_t store_count = (b ? 3 : 4);
  const std::array<Range, 6> loads = {range(site_esp + 0x1Cull, 4), range(list + 8ull, 4), range(object + 0x7Cull, 4), range(object + 0x84ull, 4),
                                      range(object + 0x80ull, 4), (b ? range(site_esp - 4ull, 4) : range(site_esp + 0x34ull, 4))};
  for (size_t store = 0; store < store_count; ++store) {
    for (size_t load = 0; load < loads.size(); ++load) {
      if (store == 1 && load == 4) continue;
      if (overlap(stores[store], loads[load])) return true;
    }
  }
  return false;
}

struct GuardTruth {
  uint64_t tuples = 0;
  uint64_t wrongly_accepted = 0;  // the guard says 0 where the model sees an overlap
  uint64_t accepted = 0;
  uint64_t refused_without_overlap = 0;
};

// `tuples` random (list, data, capacity, object, esp) around the anchors (every location the model compares) against the model.
GuardTruth PushGuardTruth(const uint8_t* guard, uint8_t* buffer, uint64_t tuples, Random* random) {
  GuardTruth truth;
  const uint32_t site_esp = Address(buffer) + 0x800;
  const auto around = [&](uint32_t anchor) {
    switch (random->Next() % 5) {
      case 0: return anchor;
      case 1: return anchor + (random->Next() % 9) - 4;
      case 2: return anchor + (random->Next() % 0x41) - 0x20;
      case 3: return anchor + (random->Next() % 0x201) - 0x100;
      default: return anchor + (random->Next() % 0x2001) - 0x1000;
    }
  };
  const std::array<uint32_t, 12> top = {0xFFFFFF00, 0xFFFFFF01, 0xFFFFFFF0, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0, 4, 0x10, 0xFFFFFE00, 0xFFFFFEF0, 0xFFFFFFC0};
  for (uint64_t tuple = 0; tuple < tuples; ++tuple) {
    const bool b = (random->Next() & 1) != 0;
    // One thing is placed on the frame or on another, the rest anywhere near or far.
    const auto place = [&](uint32_t other) {
      switch (random->Next() % 8) {
        case 0: return around(site_esp);
        case 1: return around(site_esp + 0x1C);
        case 2: return around(site_esp - 0x40 + (random->Next() % 0x100));
        case 3: return around(other);
        case 4: return top[random->Next() % top.size()];
        case 5: return random->Next();
        default: return around(Address(buffer) + (random->Next() % 0x1000));
      }
    };
    const uint32_t object = place(site_esp - 0x7C);
    const uint32_t list = place((random->Next() & 1) != 0 ? object + 0x78 : object + 0x7C);
    const uint32_t capacity = (random->Next() % 4 == 0 ? random->Next() % 0x100000 : random->Next() % 40);
    const uint32_t data = place((random->Next() & 1) != 0 ? object + 0x7C - 4 * (random->Next() % 8) : list + 8);
    const uint32_t result = PushGuardCall(Address(guard), list, data, capacity, object, site_esp);
    ++truth.tuples;
    // Beyond the guard's contract (it refuses these): an object or list at the very top, a block that wraps.
    const bool wraps = (object > 0xFFFFFF00 || list > 0xFFFFFF00 || static_cast<uint64_t>(data) + 4ull * capacity > 0xFFFFFFFFull);
    const bool overlap = wraps || PushOverlaps(list, data, capacity, object, site_esp, b);
    if (result == 0) {
      ++truth.accepted;
      if (overlap) ++truth.wrongly_accepted;
    } else if (!overlap) {
      ++truth.refused_without_overlap;
    }
  }
  return truth;
}

// Both copies' loops: the head (the site) of each, the loop region's start, the exit after the loop, the calls inside it.
struct PushLoop {
  uint32_t head_rva;
  uint32_t exit_rva;
  std::array<uint32_t, 4> call_rvas;  // the allocator twice, the free, the per-item call
};

// The offsets of the pattern's `E8 ?? ?? ?? ??`.
std::vector<uint32_t> PushCallOffsets(std::string_view pattern) {
  std::vector<std::string_view> tokens;
  for (size_t at = 0; at < pattern.size(); at += 3) tokens.push_back(pattern.substr(at, 2));
  std::vector<uint32_t> offsets;
  for (size_t index = 0; index + 4 < tokens.size(); ++index) {
    if (tokens[index] == "E8" && tokens[index + 1] == "??" && tokens[index + 2] == "??" && tokens[index + 3] == "??" && tokens[index + 4] == "??") {
      offsets.push_back(static_cast<uint32_t>(index));
    }
  }
  return offsets;
}

// Timing: ns a call of the loop for `items` items into a vector with room, the size cleared before each call; game / stub (rounds interleaved, median of `rounds`).
std::string PushTimingLines(const std::array<PushLoop, 2>& loops, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  auto world = std::make_unique<ItemPushWorld>();
  std::string text;
  const int rounds = (quick ? 7 : 9);
  constexpr std::array<uint32_t, 12> COUNTS = {1, 2, 3, 4, 5, 6, 8, 16, 64, 256, 1024, 4096};
  Random random = {.state = 0x7117};
  for (size_t which = 0; which < loops.size(); ++which) {
    std::string line = std::format("   timing, loop {} (ns a call; game / stub; (min-max of the stub's)):", which == 0 ? "A" : "B");
    for (const uint32_t items : COUNTS) {
      PushSpec spec = {.b = (which == 1), .start = 0, .end = items, .size = 0, .capacity = PUSH_SLOTS, .mode = 0, .layout = PUSH_PLAIN, .shift = 0};
      PushSetup(world.get(), Address(world->call.frame) + PUSH_LOCALS, nullptr, spec, &random);
      const std::array<uint32_t, 2> targets = {Address(game + loops[which].head_rva), Address(patched + loops[which].head_rva)};
      const uint32_t empty_target = Address(patched + loops[which].exit_rva);
      std::array<std::vector<double>, 3> times;
      const int calls = std::max(20, (quick ? 200'000 : 600'000) / static_cast<int>(items));
      for (int round = 0; round < rounds; ++round) {
        for (size_t column = 0; column < 3; ++column) {
          const uint32_t target = (column == 2 ? empty_target : targets[column]);
          const auto start = std::chrono::steady_clock::now();
          for (int call = 0; call < calls; ++call) {
            Write32(world->vec + PUSH_EDI + 0x80, 0);
            PushRunHeap(world.get(), target);
          }
          times[column].push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / calls);
        }
      }
      for (auto& column : times) std::sort(column.begin(), column.end());
      line += std::format(" {}: {:.0f} / {:.0f} ({:.0f}-{:.0f}) [entry {:.0f}];", items, times[0][times[0].size() / 2] - times[2][times[2].size() / 2],
                          times[1][times[1].size() / 2] - times[2][times[2].size() / 2], times[1].front() - times[2][times[2].size() / 2],
                          times[1].back() - times[2][times[2].size() / 2], times[2][times[2].size() / 2]);
    }
    text += line + "\n";
  }
  return text;
}

// A mutant of the page: `bytes` written at `at` (an offset in the guard-and-stubs block) while it runs, put back after.
struct PushMutant {
  const char* what;
  size_t at;
  std::array<uint8_t, 6> bytes;
  size_t size;
  uint64_t least;  // how many of the scenarios must show it
  size_t second_at = 0;    // a second place nopped out with it (the mutants that need two tests gone; none: second_size 0)
  size_t second_size = 0;
};

LONG WINAPI PushCrash(EXCEPTION_POINTERS* pointers) {
  std::printf("CRASH %08lX at %p (eax %08lX ecx %08lX edx %08lX ebx %08lX esp %08lX ebp %08lX esi %08lX edi %08lX) in scenario %llu, layout %u\n", pointers->ExceptionRecord->ExceptionCode,
              pointers->ExceptionRecord->ExceptionAddress, pointers->ContextRecord->Eax, pointers->ContextRecord->Ecx, pointers->ContextRecord->Edx, pointers->ContextRecord->Ebx,
              pointers->ContextRecord->Esp, pointers->ContextRecord->Ebp, pointers->ContextRecord->Esi, pointers->ContextRecord->Edi, push_scenario_now, push_layout_now);
  std::printf("  the test's own offset %08lX, thread %lu, access %lu at %p\n",
              static_cast<unsigned long>(reinterpret_cast<uintptr_t>(pointers->ExceptionRecord->ExceptionAddress) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))),
              GetCurrentThreadId(), pointers->ExceptionRecord->NumberParameters > 0 ? static_cast<unsigned long>(pointers->ExceptionRecord->ExceptionInformation[0]) : 0ul,
              pointers->ExceptionRecord->NumberParameters > 1 ? reinterpret_cast<void*>(pointers->ExceptionRecord->ExceptionInformation[1]) : nullptr);
  std::printf("  this thread's scenario: loop %c layout %u start %08X end %08X size %u capacity %08X mode %u shift %08X, world %p (%zu bytes), edge %p\n", push_spec_now.b ? 'B' : 'A', push_spec_now.layout, push_spec_now.start, push_spec_now.end, push_spec_now.size, push_spec_now.capacity, push_spec_now.mode, push_spec_now.shift, push_world_now, sizeof(ItemPushWorld), nullptr);
  return EXCEPTION_CONTINUE_SEARCH;
}

int RunPushTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  SetUnhandledExceptionFilter(PushCrash);
  PVOID trace_handler = nullptr;
  if (std::getenv("ITEM_PUSH_TRACE") != nullptr) {
    trace_handler = AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* pointers) -> LONG {
      std::fprintf(stderr, "  exception %08lX at %p esp %08lX (access %lu address %p)\n", pointers->ExceptionRecord->ExceptionCode, pointers->ExceptionRecord->ExceptionAddress,
                   pointers->ContextRecord->Esp, pointers->ExceptionRecord->NumberParameters > 0 ? static_cast<unsigned long>(pointers->ExceptionRecord->ExceptionInformation[0]) : 0ul,
                   pointers->ExceptionRecord->NumberParameters > 1 ? reinterpret_cast<void*>(pointers->ExceptionRecord->ExceptionInformation[1]) : nullptr);
      return EXCEPTION_CONTINUE_SEARCH;
    });
  }
  uint8_t* const stub_base = ec::page + ec::ITEM_PUSH_PAGE;
  constexpr std::array<uint8_t, 10> GAME_SITE = {0x8B, 0x44, 0x24, 0x1C, 0x8B, 0x8F, 0x80, 0x00, 0x00, 0x00};  // mov eax, [esp + 0x1c]; mov ecx, [edi + 0x80]
  const std::array<std::string_view, 2> patterns = {ec::ITEM_PUSH_A, ec::ITEM_PUSH_B};
  const std::array<uint32_t, 2> region_rva = {build.code->push_loop_a_rva, build.code->push_loop_b_rva};
  std::array<PushLoop, 2> loops = {};
  for (size_t loop = 0; loop < 2; ++loop) {
    const size_t bytes = (patterns[loop].size() + 1) / 3;
    loops[loop].head_rva = region_rva[loop] + static_cast<uint32_t>(ec::ITEM_PUSH_SITE);
    loops[loop].exit_rva = region_rva[loop] + static_cast<uint32_t>(bytes);
    const std::vector<uint32_t> calls = PushCallOffsets(patterns[loop]);
    CHECK(calls.size() == 4);
    for (size_t call = 0; call < 4; ++call) loops[loop].call_rvas[call] = region_rva[loop] + calls[call];
  }

  // 19a. The sites and the stubs as Prepare made them.
  for (size_t loop = 0; loop < 2; ++loop) {
    const ec::Site& site = ec::sites[ec::ITEM_PUSH_FIRST_SITE + loop];
    const uint32_t site_rva = loops[loop].head_rva;
    const uint8_t* stub = stub_base + ec::ITEM_PUSH_STUB_AT[loop];
    const uint8_t* stub_end = stub_base + ec::ITEM_PUSH_STUB_AT[loop + 1];
    CHECK(site.at == patched + site_rva && ((ec::written >> (ec::ITEM_PUSH_FIRST_SITE + loop)) & 1) != 0);
    CHECK(std::memcmp(game + site_rva, GAME_SITE.data(), GAME_SITE.size()) == 0 && std::memcmp(site.game.data(), GAME_SITE.data(), 5) == 0);
    CHECK(patched[site_rva] == 0xE9 && SiteJumpTarget(ec::ITEM_PUSH_FIRST_SITE + loop, patched + site_rva) == Address(stub));
    CHECK(std::memcmp(patched + site_rva + 5, GAME_SITE.data() + 5, 5) == 0);  // the rest of the second instruction is the game's, never run
    CHECK(stub_end[-5] == 0xE9 && Address(stub_end) + Read32(stub_end - 4) == Address(patched + site_rva + ec::ITEM_PUSH_SIZE));
    CHECK(std::memcmp(stub, ec::ITEM_PUSH_STUB.data() + ec::ITEM_PUSH_STUB_AT[loop], ec::ITEM_PUSH_STUB_AT[loop + 1] - ec::ITEM_PUSH_STUB_AT[loop] - 4) == 0);
    // The loop's last instruction is the `jb` to the head, the game's code goes on at the exit.
    const uint32_t exit_rva = loops[loop].exit_rva;
    CHECK(game[exit_rva - 6] == 0x0F && game[exit_rva - 5] == 0x82 && Address(game + exit_rva) + Read32(game + exit_rva - 4) == Address(game + site_rva));
  }
  CHECK(std::memcmp(stub_base, ec::ITEM_PUSH_STUB.data(), ec::ITEM_PUSH_STUB_AT[0]) == 0);
  std::printf("19. item push loops: both sites (+0x%X in the loops' regions of %zu and %zu bytes) found and written, the guard (%zu bytes) and the stubs (%zu and %zu bytes) in the page at 0x%zX\n",
              static_cast<uint32_t>(ec::ITEM_PUSH_SITE), (patterns[0].size() + 1) / 3, (patterns[1].size() + 1) / 3, ec::ITEM_PUSH_STUB_AT[0],
              ec::ITEM_PUSH_STUB_AT[1] - ec::ITEM_PUSH_STUB_AT[0], ec::ITEM_PUSH_STUB_AT[2] - ec::ITEM_PUSH_STUB_AT[1], ec::ITEM_PUSH_PAGE);

  // The loops' calls and way out, in both copies (after Prepare: its pattern reads these bytes), put back at the end.
  struct Hook {
    uint8_t* at;
    std::array<uint8_t, 5> saved;
  };
  std::vector<Hook> hooks;
  const auto hook = [&](uint8_t* at, const void* to, bool call) {
    hooks.push_back({.at = at});
    std::memcpy(hooks.back().saved.data(), at, 5);
    JumpTo(at, to);
    if (call) at[0] = 0xE8;
  };
  const std::array<const void*, 4> fakes = {reinterpret_cast<const void*>(&PushAlloc), reinterpret_cast<const void*>(&PushAlloc),
                                            reinterpret_cast<const void*>(&PushFree), reinterpret_cast<const void*>(&PushCallback)};
  for (uint8_t* image : {game, patched}) {
    for (const PushLoop& loop : loops) {
      for (size_t call = 0; call < 4; ++call) hook(image + loop.call_rvas[call], fakes[call], true);
      hook(image + loop.exit_rva, reinterpret_cast<const void*>(&ParticleLoopReturn), false);
    }
  }
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  const std::array<uint32_t, 2> game_heads = {Address(game + loops[0].head_rva), Address(game + loops[1].head_rva)};
  const std::array<uint32_t, 2> patched_heads = {Address(patched + loops[0].head_rva), Address(patched + loops[1].head_rva)};

  auto* edge = static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * PUSH_EDGE_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  CHECK(edge != nullptr);
  DWORD edge_protection;
  CHECK(VirtualProtect(edge + PUSH_EDGE_BYTES, PUSH_EDGE_BYTES, PAGE_NOACCESS, &edge_protection) != FALSE);
  ItemPushWorld* const world = PushAllocWorld();
  ItemPushWorld* const backup = PushAllocWorld();
  ItemPushWorld* const result = PushAllocWorld();
  CHECK(world != nullptr && backup != nullptr && result != nullptr && Address(world) % 16 == 0);

  // 19b. The guard against the exact model, and each clause's mutant.
  DWORD protection;
  const bool only_threads = (std::getenv("ITEM_PUSH_THREADS_ONLY") != nullptr);
  if (std::getenv("ITEM_PUSH_FIRST") == nullptr && !only_threads) {
    auto buffer = std::make_unique<std::array<uint8_t, 0x1000>>();
    Random random = {.state = 0x6A4D1};
    const uint64_t tuples = (quick ? 400'000 : 6'000'000);
    const GuardTruth truth = PushGuardTruth(stub_base, buffer->data(), tuples, &random);
    std::printf("   guard against the model: %llu tuples (the object, the list and the block around the stack slots, each other, the top of the address space; capacities to 0xFFFFF): accepted %llu, refused %llu of which %llu had no overlap (the guard is cautious), accepted with an overlap: %llu\n",
                truth.tuples, truth.accepted, truth.tuples - truth.accepted, truth.refused_without_overlap, truth.wrongly_accepted);
    CHECK(truth.wrongly_accepted == 0 && truth.accepted > truth.tuples / 20 && truth.tuples - truth.accepted > truth.tuples / 20);
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection) != FALSE);
    const std::array<PushMutant, 10> guard_mutants = {{{"no guard (xor esi, esi; ret)", 0, {0x31, 0xF6, 0xC3}, 3, 1},
                                                       {"the object at the top of the address space not refused", 0x07, {0x90, 0x90}, 2, 1},
                                                       {"the list at the top of the address space not refused", 0x0E, {0x90, 0x90}, 2, 1},
                                                       {"a block that wraps not refused", 0x15, {0x90, 0x90}, 2, 1},
                                                       {"slots on the object's fields not refused", 0x26, {0x90, 0x90}, 2, 1},
                                                       {"slots on the list's field not refused", 0x34, {0x90, 0x90}, 2, 1},
                                                       {"the size field on the list's field not refused", 0x3E, {0x90, 0x90}, 2, 1},
                                                       {"slots on the stack not refused", 0x4A, {0x90, 0x90}, 2, 1},
                                                       {"the object on the stack not refused", 0x57, {0x90, 0x90}, 2, 1},
                                                       {"the list on the stack not refused", 0x61, {0x90, 0x90}, 2, 1}}};
    std::string line;
    for (const PushMutant& mutant : guard_mutants) {
      std::array<uint8_t, 6> saved = {};
      std::memcpy(saved.data(), stub_base + mutant.at, mutant.size);
      std::memcpy(stub_base + mutant.at, mutant.bytes.data(), mutant.size);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      Random mutant_random = {.state = 0x6A4D1};
      const GuardTruth mutated = PushGuardTruth(stub_base, buffer->data(), tuples / 4, &mutant_random);
      std::memcpy(stub_base + mutant.at, saved.data(), mutant.size);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      line += std::format("\n   CONTROL, {}: accepted with an overlap {} of {}", mutant.what, mutated.wrongly_accepted, mutated.tuples);
      CHECK(mutated.wrongly_accepted >= mutant.least);
    }
    std::printf("%s\n", line.c_str() + 1);
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READ, &protection) != FALSE);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }

  // 19c. The whole loops in both copies.
  HoldSites(ec::ITEM_PUSH_SITES, ec::ITEM_PUSH_SITES);
  const uint64_t SEED = (std::getenv("ITEM_PUSH_SEED") != nullptr ? std::strtoull(std::getenv("ITEM_PUSH_SEED"), nullptr, 16) : 0x19C0);
  const auto run_scenario = [&](uint64_t seed, uint64_t index, bool* faulted, PushSpec* spec_out) {
    return PushBoth(world, backup, result, edge, seed, index, game_heads, patched_heads, spec_out, faulted);
  };
  if (!only_threads) {
    const uint64_t scenarios = (std::getenv("ITEM_PUSH_LAST") != nullptr ? std::strtoull(std::getenv("ITEM_PUSH_LAST"), nullptr, 10) + 1 : (quick ? 20'000 : 250'000));
    std::array<uint64_t, PUSH_LAYOUTS> by_layout = {};
    std::array<uint64_t, PUSH_LAYOUTS> faulted_by_layout = {};
    uint64_t run_expected = 0;
    uint64_t callback_runs = 0;
    uint64_t grown = 0;
    uint64_t rounds_total = 0;
    uint64_t wrong = 0;
    for (uint64_t index = (std::getenv("ITEM_PUSH_FIRST") != nullptr ? std::strtoull(std::getenv("ITEM_PUSH_FIRST"), nullptr, 10) : 0); index < scenarios; ++index) {
      bool faulted = false;
      PushSpec spec;
      const char* what = run_scenario(SEED, index, &faulted, &spec);
      ++by_layout[spec.layout];
      faulted_by_layout[spec.layout] += faulted ? 1 : 0;
      rounds_total += PushRounds(spec.start, spec.end);
      const uint32_t remaining = (spec.start < spec.end ? spec.end - spec.start : 0);
      if (spec.layout == PUSH_PLAIN && spec.mode == 0 && remaining >= 5 && spec.size < spec.capacity && spec.capacity < 0x100000
          && std::min<uint64_t>(remaining, spec.capacity - spec.size) >= 5) {
        ++run_expected;
      }
      callback_runs += (spec.mode != 0 ? 1 : 0);
      grown += (spec.size >= spec.capacity || spec.capacity - spec.size < remaining ? 1 : 0);
      if (what[0] != 0) {
        ++wrong;
        Report("  item push loop: scenario %llu (loop %c, layout %u, start %08X end %08X size %u capacity %u mode %u): %s differs\n", index, spec.b ? 'B' : 'A', spec.layout,
               spec.start, spec.end, spec.size, spec.capacity, spec.mode, what);
        if (std::getenv("ITEM_PUSH_EXPLAIN") != nullptr) {
          std::fprintf(stderr, "  scenario %llu differs (%s), shift %08X zero_after %u\n", index, what, spec.shift, spec.zero_after);
          PushExplain(*result, *world);
        }
      }
    }
    std::string layouts;
    for (uint32_t layout = 0; layout < PUSH_LAYOUTS; ++layout) {
      layouts += std::format("{}{} ({} faulted)", layout == 0 ? "" : ", ", by_layout[layout], faulted_by_layout[layout]);
    }
    std::printf("   the whole loops: %llu scenarios (%llu rounds in all; a run of the stub expected at the start in %llu, per-item calls in %llu, a growth in the loop expected in %llu), by layout (plain, block on the object's fields / the list's field / the stack, object on the stack, list on the stack, list on data, array on the block, block at a no-access page, items at a no-access page): %s: NOT equal %llu\n",
                scenarios, rounds_total, run_expected, callback_runs, grown, layouts.c_str(), wrong);
    CHECK(wrong == 0);
    CHECK(run_expected > scenarios / 10 && faulted_by_layout[PUSH_BLOCK_EDGE] > scenarios / 400 && faulted_by_layout[PUSH_ITEMS_EDGE] > scenarios / 400);
  }

  // 19d. CONTROLs on the stubs: each mutant is run on the same scenarios.
  if (!only_threads) {
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection) != FALSE);
    const uint64_t scenarios = (quick ? 4000 : 12000);
    // The stubs' two `at least five items left` tests are the same condition twice (before and after the room limit), so each alone is equivalent to the stub
    // (not a CONTROL); both gone must be seen. The size-below-capacity and the capacity-below-0x100000 jumps matter only for a size above the capacity and a
    // capacity whose 4 * capacity wraps (PushRandomSpec).
    const std::array<PushMutant, 34> mutants = {{
        {"A: no edx test", 0x7A, {0x90, 0x90}, 2, 5},
        {"A: no unsigned `ebp below the end` test", 0x82, {0x90, 0x90}, 2, 5},
        {"A: the guard's answer ignored", 0xA7, {0x90, 0x90}, 2, 5},
        {"A: no room limit (every remaining item run)", 0xB0, {0x90, 0x90, 0x90}, 3, 5},
        {"A: the last item run too (no dec)", 0xB8, {0x90}, 1, 20},
        {"A: the first scratch slot [esp + 0x30] not written", 0xC5, {0x90, 0x90, 0x90, 0x90}, 4, 1},
        {"A: +0x54 for +0x50", 0xCB, {0x54}, 1, 20},
        {"A: the second scratch slot [esp + 0x14] not written", 0xCC, {0x90, 0x90, 0x90, 0x90}, 4, 1},
        {"A: the slot not written", 0xD0, {0x90, 0x90, 0x90}, 3, 20},
        {"A: the size not incremented", 0xD3, {0x90}, 1, 20},
        {"A: the size not stored", 0xD4, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, 6, 20},
        {"A: jbe for jb at the run's bottom", 0xDD, {0x76}, 1, 20},
        {"A: edx not zero again", 0xDF, {0x90, 0x90}, 2, 5},
        {"A: eax not the list again", 0xE1, {0x90, 0x90, 0x90, 0x90}, 4, 5},
        {"B: no edx test", 0xF6, {0x90, 0x90}, 2, 5},
        {"B: no unsigned `ebp below the end` test", 0xFA, {0x90, 0x90}, 2, 5},
        {"B: the guard's answer ignored", 0x121, {0x90, 0x90}, 2, 5},
        {"B: no room limit (every remaining item run)", 0x12E, {0x90, 0x90, 0x90}, 3, 5},
        {"B: the last item run too (no dec)", 0x136, {0x90}, 1, 20},
        {"B: +0x3C for +0x38", 0x146, {0x3C}, 1, 20},
        {"B: the scratch slot [esp + 0x14] not written", 0x147, {0x90, 0x90, 0x90, 0x90}, 4, 1},
        {"B: the slot not written", 0x14B, {0x90, 0x90, 0x90}, 3, 20},
        {"B: the size not incremented", 0x14E, {0x90}, 1, 20},
        {"B: the size not stored", 0x14F, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, 6, 20},
        {"B: the limit is the end (cmp ebp, ebx)", 0x156, {0x39, 0xDD, 0x90}, 3, 20},
        {"B: jbe for jb at the run's bottom", 0x159, {0x76}, 1, 20},
        {"B: edx not zero again", 0x15E, {0x90, 0x90}, 2, 5},
        {"B: eax not the list again", 0x160, {0x90, 0x90, 0x90, 0x90}, 4, 5},
        {"A: no size-below-capacity test (cmp ecx, esi; jae)", 0x90, {0x90, 0x90}, 2, 1},
        {"A: no capacity-below-0x100000 test", 0x98, {0x90, 0x90}, 2, 1},
        {"A: both `at least five items left` tests gone", 0x89, {0x90, 0x90}, 2, 5, 0xB6, 2},
        {"B: no size-below-capacity test (cmp ecx, esi; jae)", 0x10A, {0x90, 0x90}, 2, 1},
        {"B: no capacity-below-0x100000 test", 0x112, {0x90, 0x90}, 2, 1},
        {"B: both `at least five items left` tests gone", 0x103, {0x90, 0x90}, 2, 5, 0x134, 2},
    }};
    for (const PushMutant& mutant : mutants) {
      std::array<uint8_t, 6> saved = {};
      std::array<uint8_t, 2> saved_second = {};
      std::memcpy(saved.data(), stub_base + mutant.at, mutant.size);
      std::memcpy(saved_second.data(), stub_base + mutant.second_at, mutant.second_size);
      std::memcpy(stub_base + mutant.at, mutant.bytes.data(), mutant.size);
      std::memset(stub_base + mutant.second_at, 0x90, mutant.second_size);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      uint64_t seen = 0;
      uint64_t seen_faulted = 0;
      uint64_t faulted_scenarios = 0;
      for (uint64_t index = 0; index < scenarios; ++index) {
        bool faulted = false;
        PushSpec spec;
        const bool seen_one = run_scenario(SEED, index, &faulted, &spec)[0] != 0;
        seen += seen_one ? 1 : 0;
        seen_faulted += (seen_one && faulted) ? 1 : 0;
        faulted_scenarios += faulted ? 1 : 0;
      }
      std::memcpy(stub_base + mutant.at, saved.data(), mutant.size);
      std::memcpy(stub_base + mutant.second_at, saved_second.data(), mutant.second_size);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      std::printf("   CONTROL, %s: %llu of %llu scenarios seen as different (%llu of the %llu that fault)\n", mutant.what, seen, scenarios, seen_faulted, faulted_scenarios);
      CHECK(seen >= mutant.least);
    }
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READ, &protection) != FALSE);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }

  // 19e. The sites switched with their row 200 times while 4 threads run both loops of both copies on their own worlds, then a parked thread at each byte.
  {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> scenarios_run = 0;
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        auto* thread_edge = static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * PUSH_EDGE_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        DWORD thread_protection;
        VirtualProtect(thread_edge + PUSH_EDGE_BYTES, PUSH_EDGE_BYTES, PAGE_NOACCESS, &thread_protection);
        ItemPushWorld* const thread_world = PushAllocWorld();
        ItemPushWorld* const thread_backup = PushAllocWorld();
        ItemPushWorld* const thread_result = PushAllocWorld();
        for (uint64_t index = 0; !stop.load(); ++index) {
          bool faulted;
          PushSpec spec;
          if (const char* what = PushBoth(thread_world, thread_backup, thread_result, thread_edge, 0x19E0 + static_cast<uint64_t>(worker) * 0x1000000ull, index, game_heads,
                                          patched_heads, &spec, &faulted);
              what[0] != 0) {
            if (++bad <= 5) {
              std::printf("   thread %d scenario %llu (loop %c, layout %u, start %08X end %08X size %u capacity %u mode %u zero_after %u shift %08X): %s differs\n", worker, index,
                          spec.b ? 'B' : 'A', spec.layout, spec.start, spec.end, spec.size, spec.capacity, spec.mode, spec.zero_after, spec.shift, what);
              PushExplain(*thread_result, *thread_world);
            }
          }
          ++scenarios_run;
        }
        VirtualFree(thread_edge, 0, MEM_RELEASE);
        PushFreeWorld(thread_world);
        PushFreeWorld(thread_backup);
        PushFreeWorld(thread_result);
      });
    }
    int busy = 0;
    bool on = true;
    for (int switches = 0; switches < 200; ++switches) {
      on = !on;
      int tries = 0;
      do {
        twattila::engine_code::Update(on, twattila::engine_code::Game::ATTILA, false, false, true, true, true);
        ++tries;
      } while ((((ec::written >> ec::ITEM_PUSH_FIRST_SITE) & 1) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
    }
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true);
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("   200 switches while 4 threads made %llu scenarios in both copies on their own worlds: %llu wrong (%d writes waited for a thread inside a site)\n", scenarios_run.load(),
                bad.load(), busy);
    CHECK(bad.load() == 0 && (ec::written & ec::ITEM_PUSH_SITES) == ec::ITEM_PUSH_SITES);
  }
  if (const int failed = ParkedAtSites(ec::ITEM_PUSH_SITES, 10); failed != 0) return failed;
  std::printf("   a parked thread at each site -1 .. +10: refused at +1 .. +4 and written at the others\n");

  // 19f. Timing.
  HoldSites(ec::ITEM_PUSH_SITES, ec::ITEM_PUSH_SITES);
  std::printf("%s", PushTimingLines(loops, game, patched, quick).c_str());
  HoldSites(ec::ITEM_PUSH_SITES, ec::ITEM_PUSH_SITES);

  for (size_t index = hooks.size(); index-- > 0;) std::memcpy(hooks[index].at, hooks[index].saved.data(), 5);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  VirtualFree(edge, 0, MEM_RELEASE);
  PushFreeWorld(world);
  PushFreeWorld(backup);
  PushFreeWorld(result);
  return 0;
}
