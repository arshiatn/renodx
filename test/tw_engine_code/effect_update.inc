// ====================================================================================================================
// 26. Parallel Effect Update's scheduler (src/games/twattila/effect_update.hpp), included inside tests.cpp's anonymous namespace after effect_sites.inc (eu / ei of part 25,
// Address, Read32, Write32, Random and CHECK are used). Attila only.
// The scheduler runs here on a fake game: effects and a node table in an arena laid out as the scheduler reads them (the vector's count at +0x18 and entries at +0x1C, the
// table at node + 0xC8), instances whose update is a script of shared operations (each preceded by what its site's thunk does: the gates, the direct thread's fast path, the
// slow path through the handler with a Frame), a sweep that removes the flagged ones, forwarded calls that land in stubs on a fake image (the render thread's RIGID_ADD and
// RANDOM_FILL). The same world is walked twice, once as the game's loop does it (the gates closed) and once in rounds on the real worker pool; the logs, the shared words, the
// effects' vectors, the table and every instance must be equal, and the order points must never overlap.
// 26a. WalkEqualsGameLoop (test 1): effects of 0 to 40 instances, the render switch off, several rounds in one world, K = 1 to 8 workers;
// 26b. ParticlePath (2), SlotOrder (3), SoundLog (4), BurstStatics (7), RigidAddOrder (8, every forwarded call on the render thread), each as its own script;
// 26c. ChildSet (5: children into earlier, same and later effects, the late units), NewType (6: a new effect type inserted before and after the current slot: late segments),
//    TrackedChild (9);
// 26d. NoDeadlock (12): tiny effects with children into the next ones, K = 2 to 8, delays; AllocationFailure (14): a failure injected at every block of a round (before the
//    round: serial; in the round: the units drained, the rest serial); PrivateWritesOnly (15): the scheduler's reads of the game's structures with those pages read-only;
//    the serial reasons; Automatic L1 (the table's sum with the game's key test, the hysteresis, the floor, the resets, the slot limit, the bench env, the switch line); the
//    census (a render thread hit outside a unit, a foreign thread), the watched words, Update's gates; the handler writes Frame.eax only where it answers; the scheduler's
//    stack high-water;
// 26f. L2 (Parallel Effect Spawns): SpawnEqualsSerial (the create path's three sites staged and merged in the serial order: CPU and GPU pipelines, with and without events, the other
//    callers of the draw), OverflowDetected (a buffer that fills: counted, the merged buffer is serial's), the guard (a budget below the minimum, the last round's spawns past half),
//    the slot pool's words watched (S10), a walk call from a thread that is not the render thread with no round open (N9); AllocationFailure with L2 (the drain commits and merges);
//    AllocationFailure with new types right after the failing effect (R1);
// 26e. CONTROLs: no order points, no enumeration wait, a late child left out (LateChildOff), forwarding off (RigidOnWorker), sweeps on workers, no epoch, a sweep before its
//    units (SweepEarly), a unit that waits for the frontier to pass it (WaitOnLater: the hang detector), a sweep thunk with a wrong frame (SweepFrameWrong): each must be seen.
//    The L2 controls: CommitReversed, MergeByCompletion, OverflowIgnored, DeferAnyCaller (a deferred draw's tag outside its record: a leak).
//    The serial reasons and the foreign-thread count are taken only in a trace window: those cases open one.
// 26g. The game's own routines (the design's tests 2, 3, 8 and 12 on the real code that runs on a fake object graph): the random triple, the create path's call of it, the spawn
//    query and write on the real CPU and GPU pipelines (their vtables), the second node's pipeline, the slot take and return on a real pool; the serial walk calls them in image A
//    (the game as it ships), the round in image B (the thunks in, the production handler), the shared words and the real memory compared; the controls on them.
// Not here: test 13 (RegisterStateKept) is 25c's; ClaimFour (G = 4) is not built, the code has G = 1 only. The routines that need the game's own graphs (the burst, the sound start,
// the creators, the tracked child, the attach, the rigid add, the particle path with its emitter, the per-instance update) are stood in for by the scripts (26a to 26f);
// `tests.exe <dll> quick effect_update` runs this part alone.
// ====================================================================================================================

namespace eut {

constexpr uint32_t SENTINEL_EAX = 0xE4E4E4E4;
constexpr uint32_t NO_SLOT = 0xFFFFFFFF;
constexpr uint32_t SENTINEL_UNITS = 3001;  // last_round_units before a walk: a round that ran leaves its own count
namespace wp = twattila::worker_pool;

struct Effect {  // the game's effect as the scheduler reads it
  uint8_t before[0x14] = {};
  uint32_t vector_head = 0;  // +0x14: ecx of the append call
  uint32_t count = 0;        // +0x18
  uint32_t entries = 0;      // +0x1C: 8-byte entries, the instance pointer first
  uint32_t index = 0;
  uint32_t capacity = 0;
};
static_assert(offsetof(Effect, vector_head) == eu::internal::APPEND_VECTOR_OFFSET && offsetof(Effect, count) == 0x18 && offsetof(Effect, entries) == 0x1C);

struct Entry {
  uint32_t instance = 0;
  uint32_t block = 0;
};

struct Instance {
  uint32_t id = 0;
  uint32_t effect = 0;
  uint32_t seed = 0;
  uint32_t dead = 0;
  uint32_t updates = 0;
  uint32_t private_value = 0;
  uint32_t sink = 0;
};

struct LogEntry {
  uint32_t id = 0;
  uint32_t op = 0;
  uint32_t value = 0;
  bool operator==(const LogEntry&) const = default;
};

struct Slot {  // the key (a string: its length, capacity and the bytes' pointer), the effect
  uint32_t length = 0, capacity = 0, bytes = 0, effect = 0;
};
static_assert(sizeof(Slot) == eu::internal::SLOT_SIZE && offsetof(Slot, effect) == eu::internal::SLOT_EFFECT_OFFSET && offsetof(Slot, bytes) == eu::internal::KEY_BYTES_OFFSET);
// The table's sentinel keys (the game's are strings too): every free slot holds a copy of the empty key in its own bytes; a live
// slot's key has the same length ("k" and four hex digits of the slot), so the game's compare reads the bytes.
constexpr std::string_view EMPTY_KEY = "empty";
constexpr std::string_view DELETED_KEY = "erased";

struct Shape {
  uint32_t effects = 8;
  uint32_t min_units = 0;
  uint32_t max_units = 40;
  std::string ops;      // the script alphabet
  uint32_t max_ops = 4;
  uint32_t busy = 200;  // private work per instance (loops)
  uint32_t gap = 2;     // table slots between two effects
  uint32_t child_reach = 0;      // 0: a child goes to any effect; k: to one of the next k (wrapping)
  uint32_t dead_percent = 20;
  uint32_t enumerate_delay = 0;  // microseconds between two effects of the walk
  uint32_t max_types = 8;        // new effect types a world takes in all
  uint32_t spawn_room = 0;       // the fake pipeline's room for spawns (0: the node has no pipeline)
  bool gpu = false;              // the GPU pipeline (else the CPU's)
  bool types_near = false;       // a new type takes the free slot right after its unit's effect (a type that lands between the enqueued effects and the one that failed)
  bool sweep_draws = false;      // the sweep's destructors draw the game's random triple (a dead instance's, in the render thread's ordered pseudo-unit)
  uint32_t other_room = 0;       // not 0: a second node with a pipeline of its own of this room, which the 'O' operation spawns into (the first node's is the round's)
  bool real = false;             // 26g: the operations call the game's own routines in image A (the serial walk) and B (the round) on objects laid out as the game lays them out
  uint32_t pool_capacity = 4096; // the slot list's capacity word (the list never grows in a fake world; 26h sets it to the count a held slot leaves, so a return would grow it)
};

struct World {
  uint8_t* arena = nullptr;
  size_t arena_size = 0;
  size_t used = 0;
  uint8_t* node = nullptr;
  Slot* slots = nullptr;
  uint32_t table_slots = 0;
  char* key_bytes = nullptr;  // 8 bytes per slot for its key's text (in the arena)
  std::vector<Effect*> effects;
  std::vector<Instance> instances;
  std::vector<LogEntry> log;
  std::vector<uint32_t> free_slots;
  std::vector<DWORD> rigid_threads;
  std::vector<DWORD> sweep_threads;
  std::string ops;
  uint32_t max_ops = 0;
  uint32_t busy = 0;
  uint32_t child_reach = 0;
  uint32_t dead_percent = 0;
  uint32_t enumerate_delay = 0;
  uint32_t render_on = 1;
  uint32_t lcg = 1;
  uint32_t next_id = 0;
  uint32_t spawn_count = 0, sound_seq = 0, statics = 0, rigid_created = 0, rigid_count = 0, fill_count = 0, tracked = 0, events = 0, attached = 0;
  uint32_t rogue = 0;
  uint32_t slots_per_effect = 1;
  uint32_t types_left = 0;
  std::atomic<uint32_t> inside = 0;
  std::atomic<uint32_t> overlap = 0;
  std::atomic<uint32_t> updates = 0;
  // What the ops write beside the arena (the arena goes read-only in one test): the pipeline's buffer, the slot pool, the emitters.
  std::vector<uint8_t> aux = std::vector<uint8_t>(0x4000);
  size_t aux_used = 0;
  uint32_t pipeline = 0, buffer = 0, pool = 0, pool_limits = 0, emitter_plain = 0, emitter_events = 0;
  bool gpu = false;
  bool types_near = false;
  bool sweep_draws = false;
  uint32_t other_node = 0, other_capacity = 0, other_count = 0, other_answered = 0;  // the second node: its spawn count, and the answers a handler wrongly gave for it
  std::vector<std::array<uint32_t, 12>> spawn_buffer;
  // 26g: the image whose routines the operations call (null: the fake operations), the second node's pipeline, and the memory of the pipelines' arrays.
  uint8_t* image = nullptr;
  uint32_t other_pipeline = 0, other_buffer = 0;
  bool other_gpu = false;
  std::vector<uint8_t> big;
  size_t big_used = 0;

  uint8_t* Aux(size_t bytes) {
    aux_used = (aux_used + 15) & ~static_cast<size_t>(15);
    uint8_t* at = aux.data() + aux_used;
    aux_used += bytes;
    return at;
  }
  uint8_t* Big(size_t bytes) {
    big_used = (big_used + 15) & ~static_cast<size_t>(15);
    uint8_t* at = big.data() + big_used;
    big_used += bytes;
    return at;
  }

  ~World() {
    if (arena != nullptr) VirtualFree(arena, 0, MEM_RELEASE);
  }
  void* Take(size_t bytes) {
    used = (used + 15) & ~static_cast<size_t>(15);
    void* at = arena + used;
    used += bytes;
    return at;
  }
};

static World* world = nullptr;

uint8_t* At(uint32_t address) { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address)); }
uint32_t Word(uint32_t address) { return Read32(At(address)); }
void SetWord(uint32_t address, uint32_t value) { Write32(At(address), value); }

// A key at `key` ({length, capacity, the bytes' pointer}) holding `text` in `bytes` (8 at most).
void SetKey(uint8_t* key, char* bytes, std::string_view text) {
  std::memcpy(bytes, text.data(), text.size());
  Write32(key, static_cast<uint32_t>(text.size()));
  Write32(key + 4, 8);
  Write32(key + eu::internal::KEY_BYTES_OFFSET, Address(bytes));
}
void SetSlotKey(World& w, uint32_t slot, std::string_view text) { SetKey(reinterpret_cast<uint8_t*>(&w.slots[slot]), w.key_bytes + 8 * slot, text); }
void SetLiveKey(World& w, uint32_t slot) { SetSlotKey(w, slot, std::format("k{:04x}", slot)); }

// The game's slot test (its iterator's advance, +0x192D5D0): a key that equals neither the empty nor the deleted key, the lengths
// then the bytes through the pointers (+0xDE0D0).
bool Live(const World& w, uint32_t slot) {
  const auto same = [](const uint8_t* key, const uint8_t* other) {
    const uint32_t length = Read32(key);
    return length == Read32(other)
           && (length == 0 || std::memcmp(At(Read32(key + eu::internal::KEY_BYTES_OFFSET)), At(Read32(other + eu::internal::KEY_BYTES_OFFSET)), length) == 0);
  };
  const auto* key = reinterpret_cast<const uint8_t*>(&w.slots[slot]);
  return !same(key, w.node + eu::internal::EMPTY_KEY_OFFSET) && !same(key, w.node + eu::internal::DELETED_KEY_OFFSET);
}
static std::atomic<uint32_t> eax_violations = 0;
static std::atomic<uint32_t> forwarded_calls = 0;
static DWORD render_thread = 0;
static std::atomic<uint64_t> progress = 0;  // walks finished

// Counts the threads inside the shared part of an operation: more than one is two direct units at once.
struct Shared {
  World& w;
  explicit Shared(World& world_) : w(world_) {
    if (w.inside.fetch_add(1) != 0) w.overlap.fetch_add(1);
  }
  ~Shared() { w.inside.fetch_sub(1); }
};

// What a thunk does at an order point for the calling thread: the gate, the direct thread's fast path (one word), else the slow path through the handler with a Frame. True: the
// handler answered for the function (eax in *eax). The handler may write Frame.eax only where it answers.
bool Touch(eu::Site site, uint32_t ecx = 0, uint32_t first = 0, uint32_t second = 0, uint32_t* eax = nullptr, uint32_t esi = 0, uint32_t return_address = 0x0BADBAD0) {
  eu::Data* const data = ei::data;
  const bool walk = (site == eu::Site::WALK_CALL);
  if ((walk ? data->walk_armed : data->walk_active).load() == 0) return false;
  volatile uint32_t marker = 0;
  const bool asks_always = (eu::SITES[static_cast<size_t>(site)].kind != eu::Kind::ORDER || eu::AsksAlways(site));  // the walk's call, the appends, the forwarded and enumeration sites
  const uint32_t direct_high = data->direct_high.load();
  if (!asks_always && !(eu::StagesSpawns(site) && data->staging.load() != 0) && direct_high != 0 && direct_high - Address(const_cast<uint32_t*>(&marker)) < data->limit.load()) return false;
  struct {
    uint32_t regs[9];
    uint32_t stack[8];
  } frame = {};
  static_assert(offsetof(eu::Frame, stack) == sizeof(frame.regs));
  frame.regs[1] = esi;
  frame.regs[6] = ecx;
  frame.regs[7] = SENTINEL_EAX;
  frame.stack[0] = return_address;
  frame.stack[1] = first;
  frame.stack[2] = second;
  const auto handler = reinterpret_cast<eu::Handler>(static_cast<uintptr_t>(data->handler.load()));
  const uint32_t answer = handler(reinterpret_cast<eu::Frame*>(&frame), static_cast<uint32_t>(site));
  const bool may_answer = eu::SITES[static_cast<size_t>(site)].return_bytes >= 0;
  if ((answer == 1 && !may_answer) || (!(answer == 1) && frame.regs[7] != SENTINEL_EAX)) eax_violations.fetch_add(1);
  if (answer == 1 && may_answer && eax != nullptr) *eax = frame.regs[7];
  return answer == 1 && may_answer;
}

// 26g: the game's own routines on the worlds' objects. A world with an image calls them there: image A (the game as it ships) for the serial walk, image B (the thunks in) for
// the round. The objects are laid out as the game's routines read them; Harvest copies what they left into the fields Equal compares.
static uint8_t* real_game = nullptr;
static uint8_t* real_patched = nullptr;
constexpr uint32_t COUNT0 = 10;           // the spawn buffer's count before the world's first spawn
constexpr uint32_t CREATE_STUB_BYTES = 14;  // the create path's call of the draw, replayed (PlantCreateDraw): the bytes before the return address it pushes
using RealQuery = uint8_t(__thiscall*)(uint32_t);
using RealWrite = uint8_t(__thiscall*)(uint32_t, uint32_t);
using RealPool = uint8_t(__thiscall*)(uint32_t, uint32_t*);

uint8_t* RealSite(const World& w, eu::Site site) { return w.image + eu::SITES[static_cast<size_t>(site)].rva; }
uint32_t RealState(const World& w) { return Read32(w.image + ei::RANDOM_STATE_RVA); }

// The game's three-step draw. `emitter` not 0: the call the create path makes (a stub planted before the create path's return address in the image: the return address the
// handler tests, the emitter in esi); else a call from the test's code (every other caller of the draw).
void RealDraw(const World& w, std::array<uint32_t, 3>* out, uint32_t emitter = 0) {
  auto* floats = reinterpret_cast<float*>(out->data());
  if (emitter != 0) {
    reinterpret_cast<uint32_t(__cdecl*)(float*, uint32_t)>(w.image + ei::CREATE_DRAW_RETURN_RVA - CREATE_STUB_BYTES)(floats, emitter);
  } else {
    reinterpret_cast<uint32_t(__cdecl*)(float*)>(RealSite(w, eu::Site::RANDOM_TRIPLE))(floats);
  }
}

// push esi; mov esi, [esp + 12]; push [esp + 8]; call draw  <- the return address pushed here is the create path's; add esp, 4; pop esi; ret. cdecl (out, emitter).
std::array<uint8_t, CREATE_STUB_BYTES + 5> CreateDrawStub() {
  const int32_t relative = static_cast<int32_t>(eu::SITES[static_cast<size_t>(eu::Site::RANDOM_TRIPLE)].rva) - static_cast<int32_t>(ei::CREATE_DRAW_RETURN_RVA);
  std::array<uint8_t, CREATE_STUB_BYTES + 5> code = {0x56, 0x8B, 0x74, 0x24, 0x0C, 0xFF, 0x74, 0x24, 0x08, 0xE8, 0, 0, 0, 0, 0x83, 0xC4, 0x04, 0x5E, 0xC3};
  std::memcpy(&code[10], &relative, 4);
  return code;
}

// A pipeline as the game lays it out (CPU: the buffer at +0x10 with its count at +0x28, capacity at +8 and three arrays of 16-byte elements at +0xC / +0x18 / +0x24, the
// simulation's room from two words; GPU: the pool at +0x14 with the count at +0x14, capacity at +0xC and the 48-byte elements at +0x10). Its vtable is the image's (the game's
// own has_free and spawn) when the world has one, else the fake image's; the arrays exist for the game's spawn only.
uint32_t MakePipeline(World& w, uint32_t* buffer_out, bool gpu, uint32_t room, uint32_t count0) {
  const uint32_t capacity = count0 + room;
  const uint32_t buffer = Address(w.Aux(0x40));
  const uint32_t pipeline = Address(w.Aux(0x40));
  const uint32_t vtable_base = Address(w.image != nullptr ? w.image : ei::base);
  if (gpu) {
    SetWord(pipeline, vtable_base + ei::GPU_PIPELINE_VTABLE_RVA);
    SetWord(pipeline + 0x14, buffer);
    SetWord(buffer + 0xC, capacity);
    SetWord(buffer + 0x14, count0);
    if (w.image != nullptr) SetWord(buffer + 0x10, Address(w.Big(48 * capacity)));
  } else {
    const uint32_t simulation = Address(w.Aux(0x20));
    const uint32_t used = Address(w.Aux(0x20));
    SetWord(pipeline, vtable_base + ei::CPU_PIPELINE_VTABLE_RVA);
    SetWord(pipeline + 0x10, buffer);
    SetWord(pipeline + 0x14, simulation);
    SetWord(pipeline + 0x18, used);
    SetWord(buffer + 8, capacity);
    SetWord(buffer + 0x28, count0);
    SetWord(simulation + 8, capacity - room / 8);  // the simulation's room is a little smaller than the buffer's
    SetWord(used + 0xC, 0);
    if (w.image != nullptr) {
      for (uint32_t part = 0; part < 3; ++part) SetWord(buffer + 0xC + 0xC * part, Address(w.Big(16 * capacity)));
    }
  }
  *buffer_out = buffer;
  return pipeline;
}

// What the game's routines left in a world with an image, in the fields Equal compares: the random state, the spawn buffer's records past its first elements, the second node's
// count, the slot pool's free list.
void Harvest(World& w) {
  w.lcg = RealState(w);
  w.spawn_buffer.clear();
  if (w.pipeline != 0) {
    const uint32_t count = Word(w.buffer + (w.gpu ? 0x14 : 0x28));
    for (uint32_t index = COUNT0; index < count; ++index) {
      std::array<uint32_t, 12> record = {};
      if (w.gpu) {
        std::memcpy(record.data(), At(Word(w.buffer + 0x10)) + 48 * index, 48);
      } else {
        for (uint32_t part = 0; part < 3; ++part) std::memcpy(record.data() + 4 * part, At(Word(w.buffer + 0xC + 0xC * part)) + 16 * index, 16);
      }
      w.spawn_buffer.push_back(record);
    }
  }
  w.other_count = (w.other_pipeline != 0 ? Word(w.other_buffer + (w.other_gpu ? 0x14 : 0x28)) : 0);
  w.free_slots.clear();
  for (uint32_t index = 0, count = Word(w.pool + 0x10); index < count; ++index) w.free_slots.push_back(reinterpret_cast<const uint16_t*>(At(Word(w.pool + 0x14)))[index]);
}

void Busy(Instance& me, uint32_t loops) {
  uint32_t value = me.sink | 1;
  for (uint32_t i = 0; i < loops; ++i) value = value * 1664525u + 1013904223u + i;
  me.sink = value;
}

uint32_t __cdecl RigidCallback(uint32_t self, uint32_t arg) {
  World& w = *world;
  w.log.push_back({self, 'G', arg});
  w.rigid_threads.push_back(GetCurrentThreadId());
  return ++w.rigid_count;
}

uint32_t __cdecl FillCallback(uint32_t self, uint32_t first, uint32_t second) {
  World& w = *world;
  w.lcg = w.lcg * 1664525u + 1013904223u;
  w.lcg = w.lcg * 1664525u + 1013904223u;
  w.log.push_back({self, 'F', w.lcg ^ first ^ (second << 3)});
  w.rigid_threads.push_back(GetCurrentThreadId());
  ++w.fill_count;
  return w.fill_count;
}

// A new instance appended to `target`'s vector as the game's append does it; false when the vector is full.
bool AppendChild(World& w, uint32_t target_index, Random* rng, uint32_t* appended_entry) {
  Effect& target = *w.effects[target_index];
  if (target.count >= target.capacity || w.instances.size() == w.instances.capacity()) return false;
  w.instances.push_back({.id = w.next_id++, .effect = target_index, .seed = rng->Next() | 1});
  auto* entries = reinterpret_cast<Entry*>(static_cast<uintptr_t>(target.entries));
  entries[target.count] = {.instance = Address(&w.instances.back())};
  *appended_entry = Address(&entries[target.count]);
  ++target.count;
  return true;
}

uint32_t PickTarget(World& w, const Instance& me, Random* rng) {
  const uint32_t size = static_cast<uint32_t>(w.effects.size());
  return w.child_reach == 0 ? rng->Next() % size : (me.effect + 1 + rng->Next() % w.child_reach) % size;
}

// The slot pool's words as the scheduler watches them: the free list's count at +0x10, the minimum and maximum in the object at +0x18.
void PoolSync(World& w) { SetWord(w.pool + 0x10, static_cast<uint32_t>(w.free_slots.size())); }
void PoolTake(World& w, uint32_t slot) {
  SetWord(w.pool_limits + 0x50, std::min(Word(w.pool_limits + 0x50), slot));
  SetWord(w.pool_limits + 0x54, std::max(Word(w.pool_limits + 0x54), slot));
}

// The game's functions of the spawn path on the fake pipeline (CPU: the buffer at +0x10 with its count at +0x28 and capacity at +8, the simulation's room from two words; GPU:
// the buffer at +0x14, count +0x14, capacity +0xC). The has_free and the spawn are the game's own rules; the draw moves the world's random state three steps and writes floats in
// [1, 2) (never a tag).
bool HasFree(const World& w) {
  const uint32_t count = Word(w.buffer + (w.gpu ? 0x14 : 0x28));
  const uint32_t capacity = Word(w.buffer + (w.gpu ? 0xC : 8));
  if (w.gpu) return count < capacity;
  const int32_t room = static_cast<int32_t>(Word(Word(w.pipeline + 0x14) + 8)) - static_cast<int32_t>(Word(Word(w.pipeline + 0x18) + 0xC)) - static_cast<int32_t>(count);
  return room != 0 && count < capacity;
}

uint32_t __cdecl DrawEntry(float* out) {
  World& w = *world;
  const Shared shared(w);
  auto* bits = reinterpret_cast<uint32_t*>(out);
  for (int step = 0; step < 3; ++step) {
    w.lcg = w.lcg * 1664525u + 1013904223u;
    bits[step] = 0x3F800000u | (w.lcg >> 9);
  }
  return Address(out);
}

uint8_t __thiscall SpawnEntry(uint32_t, uint32_t particle) {
  World& w = *world;
  const Shared shared(w);
  const uint32_t count_at = w.buffer + (w.gpu ? 0x14 : 0x28);
  if (Word(count_at) >= Word(w.buffer + (w.gpu ? 0xC : 8))) return 0;
  std::array<uint32_t, 12> record;
  std::memcpy(record.data(), At(particle + eu::internal::SPAWN_RECORD_OFFSET), sizeof(record));
  w.spawn_buffer.push_back(record);
  SetWord(count_at, Word(count_at) + 1);
  return 1;
}

// One create: "is there room", the three-step draw, the spawn, each preceded by what its thunk does. `create_path`: the draw's return address is the create path's (else another
// caller's); `events`: the emitter raises events; `value_dword`: where the drawn value goes in the record (the census says dword 1 on the create path).
void SpawnPath(World& w, Instance& me, bool events, bool create_path, uint32_t value_dword) {
  if (w.pipeline == 0) return;
  if (w.image != nullptr) {
    // The game's own functions: the query and the write through the node's pipeline (its vtable), the draw (the create path's own call of it, or another caller's).
    if (reinterpret_cast<RealQuery>(RealSite(w, eu::Site::SPAWN_QUERY))(Address(w.node)) == 0) return;
    std::array<uint32_t, 11 + 12> particle = {};
    std::array<uint32_t, 3> out = {};
    RealDraw(w, &out, create_path ? (events ? w.emitter_events : w.emitter_plain) : 0);
    particle[11] = me.id;
    particle[11 + value_dword] = out[0];
    particle[11 + 2] = me.updates;
    me.private_value += reinterpret_cast<RealWrite>(RealSite(w, eu::Site::SPAWN_WRITE))(Address(w.node), Address(particle.data())) & 1;
    return;
  }
  uint32_t has = 0;
  if (!Touch(eu::Site::SPAWN_QUERY, Address(w.node), 0, 0, &has)) has = HasFree(w) ? 1 : 0;  // (the serial walk's call: the game's own function)
  if ((has & 0xFF) == 0) return;
  std::array<uint32_t, 11 + 12> particle = {};  // the spawn takes 48 bytes at +0x2C of the particle
  std::array<uint32_t, 3> out = {};
  uint32_t drawn = 0;
  const uint32_t return_address = create_path ? Address(ei::base) + eu::internal::CREATE_DRAW_RETURN_RVA : 0x0BADBAD0;
  if (Touch(eu::Site::RANDOM_TRIPLE, 0, Address(out.data()), 0, &drawn, events ? w.emitter_events : w.emitter_plain, return_address)) {
    if (drawn != Address(out.data())) eax_violations.fetch_add(1);
  } else {
    DrawEntry(reinterpret_cast<float*>(out.data()));
  }
  particle[11] = me.id;
  particle[11 + value_dword] = out[0];
  particle[11 + 2] = me.updates;
  uint32_t wrote = 0;
  if (!Touch(eu::Site::SPAWN_WRITE, Address(w.node), Address(particle.data()), 0, &wrote)) wrote = SpawnEntry(w.pipeline, Address(particle.data()));
  me.private_value += wrote & 1;
}

void DoOp(World& w, Instance& me, char op, Random* rng, uint32_t* held_slot) {
  const auto log = [&](char what, uint32_t value) { w.log.push_back({me.id, static_cast<uint32_t>(what), value}); };
  uint32_t result = 0;
  switch (op) {
    case 'R':
      if (w.image != nullptr) {
        std::array<uint32_t, 3> out = {};
        RealDraw(w, &out);
        const Shared shared(w);
        log('R', RealState(w) ^ out[0]);
        break;
      }
      Touch(eu::Site::RANDOM_TRIPLE);
      {
        const Shared shared(w);
        w.lcg = w.lcg * 1664525u + 1013904223u;
        log('R', w.lcg);
      }
      break;
    case 'P':
      Touch(eu::Site::SPAWN_QUERY);
      {
        const Shared shared(w);
        log('q', w.spawn_count);
      }
      Touch(eu::Site::SPAWN_WRITE);
      {
        const Shared shared(w);
        log('w', ++w.spawn_count);
      }
      Touch(eu::Site::RANDOM_TRIPLE);
      {
        const Shared shared(w);
        w.lcg = w.lcg * 1664525u + 1013904223u;
        log('R', w.lcg);
      }
      Touch(eu::Site::SURFACE_RANDOM);
      {
        const Shared shared(w);
        w.lcg = w.lcg * 22695477u + 1u;
        log('r', w.lcg);
      }
      break;
    case 'S':
      if (w.image != nullptr) {
        // The game's own take on the real pool (a list of 16-bit ids, the minimum and maximum in the object at +0x18); a unit that holds one only passes the order point.
        bool took = false;
        if (*held_slot == NO_SLOT) {
          took = reinterpret_cast<RealPool>(RealSite(w, eu::Site::SLOT_TAKE))(w.pool, held_slot) != 0;
        } else {
          Touch(eu::Site::SLOT_TAKE, w.pool);
        }
        const Shared shared(w);
        if (took) me.private_value += *held_slot;
        log('S', *held_slot);
        break;
      }
      Touch(eu::Site::SLOT_TAKE, w.pool);
      {
        const Shared shared(w);
        if (*held_slot == NO_SLOT && !w.free_slots.empty()) {
          *held_slot = w.free_slots.back();
          w.free_slots.pop_back();
          me.private_value += *held_slot;
          PoolTake(w, *held_slot);
          PoolSync(w);
        }
        log('S', *held_slot);
      }
      break;
    case 'L':
      SpawnPath(w, me, false, true, eu::internal::RECORD_DRAW_INDEX);
      break;
    case 'E':
      SpawnPath(w, me, true, true, eu::internal::RECORD_DRAW_INDEX);
      break;
    case 'M':
      SpawnPath(w, me, false, false, 3);
      break;
    case 'O': {
      // The spawn functions of a node that is not the round's (ecx is another node): never staged, never answered, in the serial order like any order point.
      if (w.other_node == 0) break;
      if (w.image != nullptr) {
        // The game's own functions on the second node's pipeline (the kind the round's is not): the thunks never answer them, the unit waits its turn.
        const uint8_t has_room = reinterpret_cast<RealQuery>(RealSite(w, eu::Site::SPAWN_QUERY))(w.other_node);
        const uint32_t count_at = w.other_buffer + (w.other_gpu ? 0x14 : 0x28);
        {
          const Shared shared(w);
          log('o', Word(count_at));
        }
        if (has_room == 0) break;
        std::array<uint32_t, 11 + 12> particle = {};
        particle[11] = me.id;
        reinterpret_cast<RealWrite>(RealSite(w, eu::Site::SPAWN_WRITE))(w.other_node, Address(particle.data()));
        const Shared shared(w);
        log('p', Word(count_at));
        break;
      }
      uint32_t has = 0;
      const bool query_answered = Touch(eu::Site::SPAWN_QUERY, w.other_node, 0, 0, &has);
      bool room = false;
      {
        const Shared shared(w);
        w.other_answered += query_answered ? 1 : 0;
        room = (w.other_count < w.other_capacity);
        log('o', w.other_count);
      }
      if (!room) break;
      std::array<uint32_t, 11 + 12> particle = {};
      const bool write_answered = Touch(eu::Site::SPAWN_WRITE, w.other_node, Address(particle.data()), 0, &has);
      const Shared shared(w);
      w.other_answered += write_answered ? 1 : 0;
      log('p', ++w.other_count);
      break;
    }
    case 'W':
      // A rogue: the slot pool's count written with no order point.
      SetWord(w.pool + 0x10, Word(w.pool + 0x10) ^ 0x100);
      break;
    case 'N':
      for (const eu::Site site : {eu::Site::SOUND_START, eu::Site::SOUND_HANDLE, eu::Site::SOUND_PLAY, eu::Site::SOUND_EMITTER}) {
        Touch(site);
        const Shared shared(w);
        log('N', ++w.sound_seq);
      }
      break;
    case 'D':
      Touch(eu::Site::EVENT_DISPATCH);
      {
        const Shared shared(w);
        log('D', ++w.events);
      }
      break;
    case 'B':
      Touch(eu::Site::BURST_1);
      {
        const Shared shared(w);
        log('B', ++w.statics);
      }
      break;
    case 'G':
      Touch(eu::Site::RIGID_CREATE);
      {
        const Shared shared(w);
        log('g', ++w.rigid_created);
      }
      if (!Touch(eu::Site::RIGID_ADD, me.id, w.rigid_created, 0, &result)) {
        const Shared shared(w);
        result = RigidCallback(me.id, w.rigid_created);
      } else {
        forwarded_calls.fetch_add(1);
      }
      {
        const Shared shared(w);
        log('h', result);
      }
      break;
    case 'F':
      if (!Touch(eu::Site::RANDOM_FILL, me.id, me.id * 3, me.id + 7, &result)) {
        const Shared shared(w);
        result = FillCallback(me.id, me.id * 3, me.id + 7);
      } else {
        forwarded_calls.fetch_add(1);
      }
      {
        const Shared shared(w);
        log('f', result);
      }
      break;
    case 'K':
      Touch(eu::Site::TRACKED_CHILD);
      {
        const Shared shared(w);
        log('K', ++w.tracked);
      }
      break;
    case 'A':
      Touch(eu::Site::ATTACH_PAIR);
      {
        const Shared shared(w);
        log('A', ++w.attached);
      }
      Touch(eu::Site::ATTACH_LIST);
      {
        const Shared shared(w);
        log('a', ++w.attached);
      }
      break;
    case 'C': {
      Touch((rng->Next() & 1) != 0 ? eu::Site::CHILD_CREATE_A : eu::Site::CHILD_CREATE_B);
      uint32_t entry = 0;
      uint32_t target_index = 0;
      bool appended = false;
      {
        const Shared shared(w);
        target_index = PickTarget(w, me, rng);
        appended = AppendChild(w, target_index, rng, &entry);
        log('C', appended ? (w.next_id << 8 | target_index) : 0xFFFF);
      }
      if (appended) Touch((rng->Next() & 1) != 0 ? eu::Site::CHILD_APPEND_A : eu::Site::CHILD_APPEND_B, w.effects[target_index]->vector_head, entry);
      break;
    }
    case 'Y': {
      Touch((rng->Next() & 1) != 0 ? eu::Site::TYPE_ADD_A : eu::Site::TYPE_ADD_B);
      uint32_t entry = 0;
      uint32_t effect_index = 0;
      uint32_t vector_head = 0;
      bool appended = false;
      {
        const Shared shared(w);
        std::array<uint32_t, 64> free = {};
        uint32_t free_count = 0;
        for (uint32_t slot = 0; slot < w.table_slots && free_count < free.size(); ++slot) {
          if (!Live(w, slot)) free[free_count++] = slot;
        }
        if (w.types_left != 0 && free_count != 0 && w.effects.size() < w.effects.capacity() && w.used + 2 * (sizeof(Effect) + 16 + 8 * sizeof(Entry)) < w.arena_size) {
          uint32_t slot = free[rng->Next() % free_count];
          const uint32_t after = 1 + me.effect * w.slots_per_effect + 1;
          if (w.types_near && after < w.table_slots && !Live(w, after)) slot = after;
          --w.types_left;
          auto* effect = new (w.Take(sizeof(Effect))) Effect;
          auto* entries = static_cast<Entry*>(w.Take(sizeof(Entry) * 8));
          effect_index = static_cast<uint32_t>(w.effects.size());
          effect->index = effect_index;
          effect->capacity = 8;
          effect->entries = Address(entries);
          effect->vector_head = Address(effect) + eu::internal::APPEND_VECTOR_OFFSET;
          w.effects.push_back(effect);
          SetLiveKey(w, slot);
          w.slots[slot].effect = Address(effect);
          vector_head = effect->vector_head;
          appended = AppendChild(w, effect_index, rng, &entry);
          log('Y', slot << 8 | effect_index);
        } else {
          log('Y', 0xFFFF);
        }
      }
      if (appended) Touch(eu::Site::CHILD_APPEND_A, vector_head, entry);
      break;
    }
    case 'X':
      me.dead = 1;
      break;
    case 'Z': {
      // A rogue: shared state written without an order point (the watched word changes under a unit that is not direct).
      w.rogue = w.lcg;
      w.lcg ^= 0x5A5A5A5Au;
      break;
    }
    default:
      break;
  }
}

void UpdateImpl(Instance& me) {
  World& w = *world;
  ++me.updates;
  w.updates.fetch_add(1);
  Random rng = {.state = ((static_cast<uint64_t>(me.seed) << 24) ^ (me.updates * 0x9E3779B97F4A7C15ull)) | 1};
  Busy(me, w.busy + rng.Next() % (w.busy + 1));
  uint32_t held_slot = NO_SLOT;
  const uint32_t count = w.ops.empty() ? 0 : rng.Next() % (w.max_ops + 1);
  for (uint32_t index = 0; index < count; ++index) {
    DoOp(w, me, w.ops[rng.Next() % w.ops.size()], &rng, &held_slot);
    Busy(me, w.busy / 2);
  }
  if (held_slot != NO_SLOT) {
    const uint32_t id = held_slot;
    if (w.image != nullptr) {
      reinterpret_cast<RealPool>(RealSite(w, eu::Site::SLOT_RETURN))(w.pool, &held_slot);  // the game's own return: the id back on the list, the variable set to -1
    } else {
      Touch(eu::Site::SLOT_RETURN, w.pool, Address(&held_slot));
    }
    const Shared shared(w);
    if (w.image == nullptr) {
      if (Word(w.pool + 0x10) >= Word(w.pool + 0xC)) SetWord(w.pool + 0xC, 2 * Word(w.pool + 0xC));  // the game's push grows the array when the count has reached the capacity
      w.free_slots.push_back(id);
      PoolSync(w);
    }
    w.log.push_back({me.id, 's', id});
  }
  if (rng.Next() % 100 < w.dead_percent) me.dead = 1;
}

void __thiscall UpdateInstance(void* self, void*, void*, void*) { UpdateImpl(*static_cast<Instance*>(self)); }
uint8_t __thiscall RenderSwitch(void*) { return static_cast<uint8_t>(world->render_on); }

void SweepImpl(Effect& effect) {
  World& w = *world;
  const Shared shared(w);
  auto* entries = reinterpret_cast<Entry*>(static_cast<uintptr_t>(effect.entries));
  uint32_t kept = 0;
  w.sweep_threads.push_back(GetCurrentThreadId());
  for (uint32_t index = 0; index < effect.count; ++index) {
    auto* instance = reinterpret_cast<Instance*>(static_cast<uintptr_t>(entries[index].instance));
    if (instance->dead != 0) {
      w.log.push_back({instance->id, '~', effect.index});
      if (w.sweep_draws) {
        // The destructor's random triple: the game's own draw, after every unit before the sweep (the pseudo-unit is direct; the handler never answers or stages it).
        if (w.image != nullptr) {
          std::array<uint32_t, 3> out = {};
          RealDraw(w, &out);
          w.log.push_back({instance->id, 'd', RealState(w) ^ out[0]});
        } else {
          Touch(eu::Site::RANDOM_TRIPLE);
          w.lcg = w.lcg * 1664525u + 1013904223u;
          w.log.push_back({instance->id, 'd', w.lcg});
        }
      }
    } else {
      if (kept != index) entries[kept] = entries[index];
      ++kept;
    }
  }
  if (kept != effect.count) effect.count = kept;
}

void __thiscall SweepEffect(uint32_t effect) { SweepImpl(*reinterpret_cast<Effect*>(static_cast<uintptr_t>(effect))); }

// The game's walk of one effect when no round takes it: the cached count, each instance, the sweep.
void SerialEffect(uint32_t effect_address) {
  World& w = *world;
  auto& effect = *reinterpret_cast<Effect*>(static_cast<uintptr_t>(effect_address));
  if (w.render_on == 0) return;
  const uint32_t count = effect.count;
  for (uint32_t index = 0; index < count; ++index) {
    UpdateImpl(*reinterpret_cast<Instance*>(static_cast<uintptr_t>(reinterpret_cast<Entry*>(static_cast<uintptr_t>(effect.entries))[index].instance)));
  }
  SweepImpl(effect);
}

// The game's loop over a node's table: each effect through the walk's call site (the thunk's gate, the handler), the walk's end. A
// slot with the empty or the deleted key is skipped (the iterator), then a null effect (the loop's body).
void GameWalk(World& w, int hook) {
  for (uint32_t slot = 0; slot < Read32(w.node + eu::internal::TABLE_OFFSET + 4); ++slot) {
    if (!Live(w, slot)) continue;
    const uint32_t effect = reinterpret_cast<const Slot*>(static_cast<uintptr_t>(Read32(w.node + eu::internal::TABLE_OFFSET)))[slot].effect;
    if (effect == 0) continue;
    if (w.enumerate_delay != 0) {
      const uint64_t until = wp::Qpc() + wp::qpc_frequency * w.enumerate_delay / 1000000;
      while (wp::Qpc() < until) _mm_pause();
    }
    if (hook == 1 && slot == 2 * w.slots_per_effect + 1) Touch(eu::Site::SPAWN_QUERY);  // the render thread, in a round, outside a unit
    if (hook == 2 && slot == 2 * w.slots_per_effect + 1) std::thread([] { Touch(eu::Site::SPAWN_QUERY); }).join();  // a thread that runs no unit
    if (hook == 3 && slot == 2 * w.slots_per_effect + 1) {
      // The walk's call and end from a thread that is not the render thread, in the middle of the walk.
      std::thread([] {
        Touch(eu::Site::WALK_CALL);
        Touch(eu::Site::WALK_END);
      }).join();
    }
    struct {
      uint32_t regs[9];
      uint32_t stack[8];
    } frame = {};
    if (ei::data->walk_armed.load() != 0) {
      frame.regs[6] = effect;
      frame.stack[0] = 0x0BADBAD0;
      frame.stack[1] = 0x11;
      frame.stack[2] = Address(w.node) + eu::internal::NODE_ARGUMENT_OFFSET;
      frame.stack[3] = 0x33;
      const auto handler = reinterpret_cast<eu::Handler>(static_cast<uintptr_t>(ei::data->handler.load()));
      if (handler(reinterpret_cast<eu::Frame*>(&frame), static_cast<uint32_t>(eu::Site::WALK_CALL)) == 1) continue;
    }
    SerialEffect(effect);
  }
  if (ei::data->walk_active.load() != 0) Touch(eu::Site::WALK_END);
}

std::unique_ptr<World> Build(uint64_t seed, const Shape& shape, uint8_t* image = nullptr) {
  auto w = std::make_unique<World>();
  w->image = image;
  if (image != nullptr) w->big.resize(48 * (COUNT0 + shape.spawn_room + shape.other_room) + 256);
  Random rng = {.state = seed | 1};
  std::vector<uint32_t> counts(shape.effects);
  uint32_t total = 0;
  for (uint32_t& count : counts) {
    count = shape.min_units + rng.Next() % (shape.max_units - shape.min_units + 1);
    total += count;
  }
  w->table_slots = shape.effects * shape.gap + 64;
  w->arena_size = ((0x400 + w->table_slots * (16 + 8) + 32 + shape.effects * (sizeof(Effect) + 64) + (total + shape.effects * 192) * sizeof(Entry) + 96 * (sizeof(Effect) + 64 + 8 * sizeof(Entry))) + 0xFFF) & ~static_cast<size_t>(0xFFF);
  w->arena = static_cast<uint8_t*>(VirtualAlloc(nullptr, w->arena_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  w->node = static_cast<uint8_t*>(w->Take(0x400));
  w->slots = static_cast<Slot*>(w->Take(w->table_slots * sizeof(Slot)));
  Write32(w->node + eu::internal::TABLE_OFFSET, Address(w->slots));
  Write32(w->node + eu::internal::TABLE_OFFSET + 4, w->table_slots);
  w->key_bytes = static_cast<char*>(w->Take(w->table_slots * 8));
  auto* sentinels = static_cast<char*>(w->Take(16));
  SetKey(w->node + eu::internal::EMPTY_KEY_OFFSET, sentinels, EMPTY_KEY);
  SetKey(w->node + eu::internal::DELETED_KEY_OFFSET, sentinels + 8, DELETED_KEY);
  for (uint32_t slot = 0; slot < w->table_slots; ++slot) SetSlotKey(*w, slot, EMPTY_KEY);
  w->instances.reserve(total + 4096);
  w->log.reserve((static_cast<size_t>(total) + 4096) * (shape.max_ops * 6 + 8));
  w->effects.reserve(shape.effects + 128);
  w->rigid_threads.reserve(total * 2 + 4096);
  w->sweep_threads.reserve(shape.effects * 8 + 4096);
  w->free_slots.reserve(256);
  for (uint32_t index = 0; index < 64; ++index) w->free_slots.push_back(1000 + index);
  w->ops = shape.ops;
  w->max_ops = shape.max_ops;
  w->busy = shape.busy;
  w->child_reach = shape.child_reach;
  w->types_left = shape.max_types;
  w->slots_per_effect = shape.gap;
  w->dead_percent = shape.dead_percent;
  w->enumerate_delay = shape.enumerate_delay;
  w->lcg = rng.Next() | 1;
  for (uint32_t index = 0; index < shape.effects; ++index) {
    auto* effect = new (w->Take(sizeof(Effect))) Effect;
    const uint32_t capacity = counts[index] + 192;
    auto* entries = static_cast<Entry*>(w->Take(capacity * sizeof(Entry)));
    effect->index = index;
    effect->capacity = capacity;
    effect->entries = Address(entries);
    effect->vector_head = Address(effect) + eu::internal::APPEND_VECTOR_OFFSET;
    for (uint32_t position = 0; position < counts[index]; ++position) {
      w->instances.push_back({.id = w->next_id++, .effect = index, .seed = rng.Next() | 1});
      entries[position].instance = Address(&w->instances.back());
    }
    effect->count = counts[index];
    w->effects.push_back(effect);
    SetLiveKey(*w, 1 + index * shape.gap);
    w->slots[1 + index * shape.gap].effect = Address(effect);
  }
  // The slot pool, the emitters (a gate byte pair at +0x298 / +0x5C of the object two pointers away) and the node's pipeline.
  w->pool = Address(w->Aux(0x40));
  w->pool_limits = Address(w->Aux(0x60));
  SetWord(w->pool + 0x18, w->pool_limits);
  SetWord(w->pool_limits + 0x50, 0xFFFFFFFFu);
  PoolSync(*w);
  SetWord(w->pool + 0xC, (image != nullptr ? 256 : shape.pool_capacity));
  if (image != nullptr) {
    // The game's list: the ids as 16-bit words behind the pointer at +0x14 (the capacity above: the real return never grows it).
    auto* ids = reinterpret_cast<uint16_t*>(w->Aux(512));
    for (uint32_t index = 0; index < 64; ++index) ids[index] = static_cast<uint16_t>(w->free_slots[index]);
    SetWord(w->pool + 0x14, Address(ids));
  }
  w->types_near = shape.types_near;
  w->sweep_draws = shape.sweep_draws;
  if (shape.other_room != 0) {
    w->other_node = Address(w->Aux(0x200));
    w->other_capacity = shape.other_room;
    if (image != nullptr) {
      w->other_gpu = !shape.gpu;  // the kind the round's pipeline is not
      w->other_pipeline = MakePipeline(*w, &w->other_buffer, w->other_gpu, shape.other_room, 0);
      Write32(At(w->other_node) + eu::internal::PIPELINE_OFFSET, w->other_pipeline);
    }
  }
  for (uint32_t* emitter : {&w->emitter_plain, &w->emitter_events}) {
    *emitter = Address(w->Aux(0x20));
    const uint32_t owner = Address(w->Aux(0x80));
    const uint32_t settings = Address(w->Aux(0x300));
    SetWord(*emitter + 0x10, owner);
    SetWord(owner + 0x78, settings);
    if (emitter == &w->emitter_events) {
      At(settings)[0x298] = 1;
      At(settings)[0x5C] = 1;
    }
  }
  if (shape.spawn_room != 0) {
    w->gpu = shape.gpu;
    w->pipeline = MakePipeline(*w, &w->buffer, shape.gpu, shape.spawn_room, COUNT0);
    Write32(w->node + eu::internal::PIPELINE_OFFSET, w->pipeline);
  }
  return w;
}

bool Equal(const World& a, const World& b, std::string* why) {
  const auto fail = [&](const char* what) {
    *why = what;
    return false;
  };
  for (size_t index = 0; index < std::min(a.log.size(), b.log.size()); ++index) {
    if (!(a.log[index] == b.log[index])) {
      *why = std::format("log entry {} of {} and {}: ({} {} {:x}) against ({} {} {:x})", index, a.log.size(), b.log.size(), a.log[index].id, static_cast<char>(a.log[index].op),
                         a.log[index].value, b.log[index].id, static_cast<char>(b.log[index].op), b.log[index].value);
      for (size_t at = (index > 6 ? index - 6 : 0); at < std::min(index + 6, std::min(a.log.size(), b.log.size())); ++at) {
        *why += std::format("\n      {}: serial ({} {} {:x})  parallel ({} {} {:x})", at, a.log[at].id, static_cast<char>(a.log[at].op), a.log[at].value, b.log[at].id,
                            static_cast<char>(b.log[at].op), b.log[at].value);
      }
      for (size_t at = 0, shown = 0; at < std::min(a.instances.size(), b.instances.size()) && shown < 8; ++at) {
        if (a.instances[at].updates != b.instances[at].updates) {
          *why += std::format("\n      instance {} (effect {}): {} updates in the game's loop, {} in the round", a.instances[at].id, a.instances[at].effect, a.instances[at].updates,
                              b.instances[at].updates);
          ++shown;
        }
      }
      return false;
    }
  }
  if (a.log.size() != b.log.size()) return fail(std::format("log length {} against {}", a.log.size(), b.log.size()).c_str());
  if (a.lcg != b.lcg || a.next_id != b.next_id || a.spawn_count != b.spawn_count || a.statics != b.statics || a.rigid_count != b.rigid_count || a.fill_count != b.fill_count
      || a.tracked != b.tracked || a.sound_seq != b.sound_seq || a.events != b.events || a.free_slots != b.free_slots) {
    return fail("a shared word");
  }
  if (a.effects.size() != b.effects.size() || a.instances.size() != b.instances.size()) return fail("effects or instances counted");
  if (a.spawn_buffer != b.spawn_buffer) return fail("the spawn buffer");
  if (a.other_count != b.other_count || a.other_answered != 0 || b.other_answered != 0) return fail("the second node's spawns, or an answer the handler gave for it");
  if (a.pipeline != 0 && Word(a.buffer + (a.gpu ? 0x14 : 0x28)) != Word(b.buffer + (b.gpu ? 0x14 : 0x28))) return fail("the spawn count");
  if (Word(a.pool + 0x10) != Word(b.pool + 0x10) || Word(a.pool_limits + 0x50) != Word(b.pool_limits + 0x50) || Word(a.pool_limits + 0x54) != Word(b.pool_limits + 0x54)) {
    return fail("the slot pool's words");
  }
  for (size_t index = 0; index < a.effects.size(); ++index) {
    const Effect& x = *a.effects[index];
    const Effect& y = *b.effects[index];
    if (x.count != y.count) return fail("an effect's count");
    for (uint32_t entry = 0; entry < x.count; ++entry) {
      const auto id = [](const Effect& effect, uint32_t at) {
        return reinterpret_cast<const Instance*>(static_cast<uintptr_t>(reinterpret_cast<const Entry*>(static_cast<uintptr_t>(effect.entries))[at].instance))->id;
      };
      if (id(x, entry) != id(y, entry)) return fail("an effect's entry");
    }
  }
  for (uint32_t slot = 0; slot < a.table_slots; ++slot) {
    const auto index_of = [&](const World& w) {
      return (!Live(w, slot) || w.slots[slot].effect == 0) ? NO_SLOT : reinterpret_cast<const Effect*>(static_cast<uintptr_t>(w.slots[slot].effect))->index;
    };
    if (index_of(a) != index_of(b)) return fail("the table");
  }
  for (size_t index = 0; index < a.instances.size(); ++index) {
    const Instance& x = a.instances[index];
    const Instance& y = b.instances[index];
    if (x.id != y.id || x.dead != y.dead || x.updates != y.updates || x.private_value != y.private_value || x.sink != y.sink) {
      *why = std::format("instance {}: updates {} against {}, dead {} against {}", x.id, x.updates, y.updates, x.dead, y.dead);
      return false;
    }
  }
  return true;
}

// The scheduler's own state between two rounds.
void ResetScheduler() {
  ei::row_on = false;
  ei::session_off = false;
  ei::off_said = false;
  ei::last_round_units = ei::MIN_ROUND_UNITS;
  ei::last_node = 0;
  ei::unit_rule = {0, 0, 0};  // every world runs as a round (the all-empty one too); the rule's own cases set theirs
  ei::was_parallel = false;
  ei::rule_switch = {};
  ei::rule_env_applied = 0;  // Update(row) passes env generation 0: the env is not read
  ei::rule_env_said = false;
  ei::sum_trace = {};
  ei::capacity_window = 0;
  ei::spawns_row = false;
  ei::spawns_off = false;
  ei::last_round_spawned = 0;
  ei::stage_grow = 0;
  ei::stage_skip = false;
  ei::event_count = 0;
  for (auto& said : ei::events_said) said = 0;
  for (auto& counter : ei::counters) counter = 0;
  for (auto& counter : ei::serial_rounds) counter = 0;
  ei::session_totals = {};
  ei::unreported_counts = {};
  ei::fault_unreported = false;
  ei::counts_line_ticks = 0;
  ei::session_used = false;
  ei::controls = {};
}

// The spawn records' blocks given back (a test that wants the first L2 round to reserve its pool, or counts the allocations).
void FreeRecords() {
  for (uint32_t block = 0; block < ei::records.capacity / ei::BLOCK_SIZE; ++block) delete[] ei::records.blocks[block];
  ei::records = {};
}

// One walk of a world: serial (the gates closed) or a round (armed, the preconditions met).
void Walk(World& w, bool round, int hook = 0) {
  world = &w;
  if (w.image != nullptr) {
    Write32(w.image + ei::RANDOM_STATE_RVA, w.lcg);  // the game's state is the image's (the round watches B's)
    ei::entries.random_state = reinterpret_cast<const uint32_t*>(real_patched + ei::RANDOM_STATE_RVA);
  } else {
    ei::entries.random_state = &w.lcg;
  }
  if (round) {
    ei::row_on = true;
    ei::session_off = false;
    ei::last_round_units = SENTINEL_UNITS;
    ei::last_node = Address(w.node);
    ei::data->walk_armed.store(1);
  } else {
    ei::data->walk_armed.store(0);
  }
  GameWalk(w, round ? hook : 0);
  ei::data->walk_armed.store(0);
  if (w.image != nullptr) Harvest(w);
  progress.fetch_add(1);
}

// The bytes of a stub that calls a cdecl function with the thiscall's registers and stack words and pops them: `words` stack arguments.
void EmitStub(uint8_t* at, uint32_t callback, int words) {
  std::vector<uint8_t> code;
  for (int word = 0; word < words; ++word) code.insert(code.end(), {0xFF, 0x74, 0x24, static_cast<uint8_t>(4 * words)});  // push dword ptr [esp + 4 * words]
  code.push_back(0x51);                                                                                                  // push ecx
  code.push_back(0xB8);
  for (int byte = 0; byte < 4; ++byte) code.push_back(static_cast<uint8_t>(callback >> (8 * byte)));
  code.insert(code.end(), {0xFF, 0xD0, 0x83, 0xC4, static_cast<uint8_t>(4 * (words + 1)), 0xC2, static_cast<uint8_t>(4 * words), 0x00});
  std::memcpy(at, code.data(), code.size());
}

// Bytes of the thread's stack that are committed (the guard page is the next one down): the deepest the thread has gone, a page at a time. 0 when unknown.
uint32_t StackUsedBytes() {
  MEMORY_BASIC_INFORMATION info = {};
  uint8_t marker = 0;
  VirtualQuery(&marker, &info, sizeof(info));
  ULONG_PTR low = 0;
  ULONG_PTR high = 0;
  GetCurrentThreadStackLimits(&low, &high);
  for (uint8_t* at = static_cast<uint8_t*>(info.AllocationBase); at < reinterpret_cast<uint8_t*>(high);) {
    VirtualQuery(at, &info, sizeof(info));
    if ((info.Protect & PAGE_GUARD) != 0) return static_cast<uint32_t>(high - reinterpret_cast<ULONG_PTR>(info.BaseAddress));
    at += info.RegionSize;
  }
  return 0;
}
static std::atomic<uint32_t> deepest_stack = 0;
void StackProbe(void*, uint32_t) {
  const uint32_t used = StackUsedBytes();
  uint32_t seen = deepest_stack.load();
  while (used > seen && !deepest_stack.compare_exchange_weak(seen, used)) {
  }
}

// Ends the process with the scheduler's state when no walk has finished for 30 seconds: a hang is a failure that must say where.
struct Watchdog {
  std::atomic<bool> stop = false;
  std::thread thread;
  Watchdog() : thread([this] {
    uint64_t last = ~0ull;
    uint32_t quiet = 0;
    while (!stop.load()) {
      Sleep(200);
      if (progress.load() != last) {
        last = progress.load();
        quiet = 0;
      } else if (++quiet >= 150) {
        const ei::Round& round = ei::current_round;
        const uint64_t frontier = round.frontier.load();
        std::printf("FAIL: no walk finished for 30 s. open %d, frontier (segment %u, position %u), segments %u, units %u, cursor %u, late unclaimed %u, enumeration done %d, waiters %u, epoch %u, "
                    "mailbox %u, armed %u, active %u, direct_high %x\n",
                    round.open, static_cast<uint32_t>(frontier >> 32), static_cast<uint32_t>(frontier), round.segment_count.load(), round.published_units.load(), round.cursor.load(),
                    round.late_unclaimed.load(), round.enumeration_done.load(), round.waiters.load(), round.epoch.load(), round.mailbox.state.load(), ei::data->walk_armed.load(),
                    ei::data->walk_active.load(), ei::data->direct_high.load());
        if (static_cast<uint32_t>(frontier >> 32) < round.segment_count.load()) {
          const ei::Segment& segment = ei::segments[static_cast<uint32_t>(frontier >> 32)];
          const uint32_t position = static_cast<uint32_t>(frontier);
          std::printf("  frontier segment: first unit %u, units %u, late %u, sweep %u, next %u, rank %u\n", segment.first_unit, segment.units, segment.late_count.load(), segment.sweep.load(),
                      segment.next, segment.rank);
          const ei::Unit* at = nullptr;
          if (position < segment.units) {
            at = &ei::units[segment.first_unit + position];
          } else if (position - segment.units < segment.late_count.load()) {
            at = ei::LateUnit(segment, position - segment.units);
          }
          if (at != nullptr) std::printf("  frontier unit: segment %u position %u late %d done %u claimed %u direct %d\n", at->segment, at->position, at->late, at->done.load(), at->claimed.load(), at->direct);
        }
        uint32_t not_done = 0;
        for (uint32_t index = 0; index < round.published_units.load(); ++index) {
          const ei::Unit& unit = ei::units[index];
          if (unit.done.load() != 0) continue;
          if (not_done++ < 6) std::printf("  unit %u: segment %u position %u late %d claimed %u direct %d\n", index, unit.segment, unit.position, unit.late, unit.claimed.load(), unit.direct);
        }
        std::printf("  %u units not done\n", not_done);
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 3);
      }
    }
  }) {}
  ~Watchdog() {
    stop = true;
    thread.join();
  }
};

}  // namespace eut

int RunEffectUpdateTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  using namespace eut;
  namespace ec = twattila::engine_code::internal;
  if (build.code != &ec::ATTILA_BUILD) {
    std::printf("26. effect update scheduler: Attila only (skipped)\n");
    return 0;
  }
  // The scheduler's globals as the add-on has them are replaced for the part: a Data of this test's, a fake image for the forwarded calls, the fake game's functions.
  eu::Data* const saved_data = ei::data;
  const uint8_t* const saved_base = ei::base;
  const auto saved_entries = ei::entries;
  auto* fake = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x2000000, MEM_RESERVE, PAGE_NOACCESS));
  CHECK(fake != nullptr);
  for (const eu::Site site : {eu::Site::RIGID_ADD, eu::Site::RANDOM_FILL}) {
    uint8_t* page = fake + (eu::SITES[static_cast<size_t>(site)].rva & ~0xFFFu);
    CHECK(VirtualAlloc(page, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE) == page);
  }
  EmitStub(fake + eu::SITES[static_cast<size_t>(eu::Site::RIGID_ADD)].rva, Address(reinterpret_cast<const void*>(&RigidCallback)), 1);
  EmitStub(fake + eu::SITES[static_cast<size_t>(eu::Site::RANDOM_FILL)].rva, Address(reinterpret_cast<const void*>(&FillCallback)), 2);
  FlushInstructionCache(GetCurrentProcess(), fake, 0x2000000);
  auto* data = new eu::Data;
  ei::data = data;
  ei::base = fake;
  eu::SetHandler(&ei::Handle);
  ei::entries = {.update_instance = reinterpret_cast<decltype(ei::entries.update_instance)>(&UpdateInstance),
                 .render_switch = reinterpret_cast<decltype(ei::entries.render_switch)>(&RenderSwitch),
                 .render_switch_object = fake,
                 .sweep = reinterpret_cast<decltype(ei::entries.sweep)>(&SweepEffect),
                 .random_state = nullptr,
                 .draw = &DrawEntry,
                 .spawn = &SpawnEntry};
  render_thread = GetCurrentThreadId();
  ResetScheduler();
  const Watchdog watchdog;
  const auto pool = [&](uint32_t threads) {
    wp::Update({.threads = threads, .in_use = true});
    return wp::Available(wp::User::EFFECT_UPDATE);
  };
  CHECK(pool(4) == 3);
  // A trace window for the counts that only a window takes (the serial reasons, the foreign-thread hits).
  const auto trace_open = [&](uint32_t frames, uint32_t threads) {
    wp::ArmTrace(frames);
    wp::Update({.threads = threads, .in_use = true});
    return wp::Tracing();
  };
  const auto trace_close = [&](uint32_t threads) {
    for (int present = 0; present < 400 && wp::Tracing(); ++present) wp::Update({.threads = threads, .in_use = true});
    return !wp::Tracing();
  };

  // 26a. The sweep thunk (Prepare's, in image B): the game's own sweep, the second half of the per-effect function, called alone as thiscall(effect) on fake effects: the
  // flagged entries (a byte at +0x6E of the instance) swapped out of the vector from the back and counted off, as the same loop modelled here; the callee-saved registers
  // and the stack come back right (a wrong prologue copy or return would corrupt or crash the caller).
  {
    const auto saved_sites = ei::sites;
    const auto saved_thunks = ei::thunks;
    const uint8_t* const saved_sweep = ei::sweep;
    uint8_t* const saved_page = ei::page;
    uint8_t* const saved_page_end = ei::page_end;
    const bool saved_prepared = ei::prepared;
    const bool saved_refused = ei::refused;
    const uint32_t saved_found = ei::found;
    CHECK(eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::sweep != nullptr);
    const auto thunk = reinterpret_cast<void(__thiscall*)(uint32_t)>(const_cast<uint8_t*>(ei::sweep));
    Random random = {.state = 0x5EE9C0DEull};
    uint64_t flagged_total = 0;
    uint64_t wrong = 0;
    for (int trial = 0; trial < (quick ? 400 : 4000); ++trial) {
      const uint32_t count = random.Next() % 41;
      std::vector<std::array<uint8_t, 0x80>> blocks(count + 1);
      std::vector<Entry> entries(count + 1);
      Effect effect;
      effect.count = count;
      effect.entries = Address(entries.data());
      const uint32_t percent = random.Next() % 101;
      for (uint32_t index = 0; index < count; ++index) {
        blocks[index].fill(0);
        blocks[index][0x6E] = (random.Next() % 100 < percent) ? 1 + random.Next() % 200 : 0;
        entries[index] = {.instance = Address(blocks[index].data()), .block = 0};
      }
      std::vector<Entry> expected = entries;
      uint32_t remaining = count;
      for (uint32_t at = remaining - 1; at < remaining; --at) {
        const auto* flag = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(expected[at].instance));
        if (flag[0x6E] != 0) {
          expected[at] = expected[remaining - 1];
          --remaining;
          ++flagged_total;
        }
      }
      thunk(Address(&effect));
      bool same = effect.count == remaining && std::memcmp(entries.data(), expected.data(), expected.size() * sizeof(Entry)) == 0;
      wrong += same ? 0 : 1;
    }
    CHECK(wrong == 0 && flagged_total > 1000);
    // The frame the thunk builds: as many zero arguments as the sweep's `ret` pops (read from the image). CONTROL SweepFrameWrong: a thunk with one too few is seen by the same check
    // (it is never run: its `ret 0xC` would pop what is not its own).
    const auto pushed_bytes = [](const uint8_t* thunk) {
      uint32_t bytes = 0;
      while (thunk[bytes / 2] == 0x6A && thunk[bytes / 2 + 1] == 0x00) bytes += 4;
      return bytes;
    };
    const uint32_t popped_bytes = patched[eu::SWEEP_CHECKS[1].rva + 8];
    CHECK(popped_bytes == eu::SWEEP_ARGUMENT_BYTES && pushed_bytes(ei::sweep) == popped_bytes);
    VirtualFree(ei::page, 0, MEM_RELEASE);
    eu::control_sweep_frame_wrong = true;
    CHECK(eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::sweep != nullptr);
    eu::control_sweep_frame_wrong = false;
    const uint32_t wrong_pushed = pushed_bytes(ei::sweep);
    CHECK(wrong_pushed != popped_bytes);
    std::printf("26a. CONTROL SweepFrameWrong: a sweep thunk that pushes %u bytes of arguments where the sweep pops %u: seen\n", wrong_pushed, popped_bytes);
    // Prepare took the module's base and the site state: all of it back, the fake image's base first.
    VirtualFree(ei::page, 0, MEM_RELEASE);
    ei::page = saved_page;
    ei::page_end = saved_page_end;
    ei::data = data;
    ei::base = fake;
    ei::sites = saved_sites;
    ei::thunks = saved_thunks;
    ei::sweep = saved_sweep;
    ei::prepared = saved_prepared;
    ei::refused = saved_refused;
    ei::found = saved_found;
    std::printf("26a. sweep thunk: %d fake effects of 0 to 40 entries, %llu flagged swapped out as the game's loop does: equal, the registers and the stack kept\n", quick ? 400 : 4000,
                static_cast<unsigned long long>(flagged_total));
  }

  std::string why;
  const auto finish_round_checks = [&](const World& w) {
    return ei::data->walk_active.load() == 0 && ei::data->direct_high.load() == 0 && ei::data->active.load() == 0 && !ei::current_round.open && w.inside.load() == 0;
  };
  // One case: the same world twice, the game's loop and rounds, `rounds` walks each; every shared result equal, no overlap, no event, no hang.
  uint64_t cases = 0;
  uint64_t units_in_rounds = 0;
  const auto run_case = [&](const Shape& shape, uint64_t seed, int rounds, uint32_t workers) -> int {
    CHECK(pool(workers + 1) == workers);
    auto serial = Build(seed, shape, shape.real ? real_game : nullptr);
    auto parallel = Build(seed, shape, shape.real ? real_patched : nullptr);
    for (int round = 0; round < rounds; ++round) {
      ei::event_count = 0;
      Walk(*serial, false);
      Walk(*parallel, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS);  // the round ran
      units_in_rounds += ei::last_round_units;
      CHECK(finish_round_checks(*parallel));
      if (!Equal(*serial, *parallel, &why)) {
        std::printf("   seed %llu round %d, %u workers: %s\n", static_cast<unsigned long long>(seed), round, workers, why.c_str());
        std::printf("   events %u, late overflows %u, failed rounds %u, census %u, foreign %u, watched %u\n", ei::event_count.load(),
                    ei::counters[static_cast<size_t>(ei::Counter::LATE_OVERFLOWS)].load(), ei::counters[static_cast<size_t>(ei::Counter::FAILED_ROUNDS)].load(),
                    ei::counters[static_cast<size_t>(ei::Counter::CENSUS_ERRORS)].load(), ei::counters[static_cast<size_t>(ei::Counter::FOREIGN_HITS)].load(),
                    ei::counters[static_cast<size_t>(ei::Counter::WATCHED_MISMATCHES)].load());
        for (uint32_t event = 0; event < std::min<uint32_t>(ei::event_count.load(), 4); ++event) {
          std::printf("   event %u: kind %u site %u value %x\n", event, static_cast<uint32_t>(ei::events[event].kind), ei::events[event].site, ei::events[event].value);
        }
        return 1;
      }
      CHECK(parallel->overlap.load() == 0 && ei::event_count.load() == 0 && eax_violations.load() == 0);
      CHECK(ei::counters[static_cast<size_t>(ei::Counter::HANGS)].load() == 0 && !ei::session_off);
    }
    ++cases;
    return 0;
  };
  const int scale = quick ? 1 : 4;

  // 26a. WalkEqualsGameLoop: shapes without shared operations (only the sweep and the dead flags), the render switch off, K = 1 to 8.
  {
    Shape shape;
    shape.ops = "";
    for (uint32_t workers = 1; workers <= 7; ++workers) {
      for (int seed = 1; seed <= 3 * scale; ++seed) {
        shape.effects = 4 + 7 * seed;
        shape.max_units = 4 + 11 * seed;
        if (run_case(shape, 1000 + seed * 17 + workers, 3, workers) != 0) return 1;
      }
    }
    shape.min_units = 0;
    shape.max_units = 0;  // every effect empty
    if (run_case(shape, 77, 2, 3) != 0) return 1;
    // The render switch off: the walk's call answers and nothing runs, as the game's own function does nothing.
    shape.max_units = 20;
    auto serial = Build(5, shape);
    auto parallel = Build(5, shape);
    serial->render_on = 0;
    parallel->render_on = 0;
    Walk(*serial, false);
    Walk(*parallel, true);
    CHECK(Equal(*serial, *parallel, &why) && parallel->updates.load() == 0 && serial->updates.load() == 0 && ei::last_round_units != SENTINEL_UNITS);
    std::printf("26a. WalkEqualsGameLoop: %llu cases (1 to 7 workers, 4 to 53 effects of 0 to 70 instances, 3 rounds each in one world, the sweep removing the flagged), the render switch off, "
                "empty effects: equal, no round left open\n", static_cast<unsigned long long>(cases));
  }

  // 26b. One script each: ParticlePath, SlotOrder, SoundLog, BurstStatics, RigidAddOrder (+ the random fill), EventDispatch / attach.
  const auto scripts = [&](const char* name, const char* ops, uint32_t reach, uint32_t seeds, bool forwarded) -> int {
    Shape shape;
    shape.ops = ops;
    shape.max_ops = 5;
    shape.effects = 14;
    shape.max_units = 30;
    shape.child_reach = reach;
    const uint64_t before = cases;
    const uint32_t forwards_before = forwarded_calls.load();
    for (uint32_t workers : {2u, 3u, 5u, 7u}) {
      for (uint32_t seed = 1; seed <= seeds; ++seed) {
        shape.busy = (seed & 1) != 0 ? 60 : 600;
        if (run_case(shape, 2000 + seed * 31 + workers, 2, workers) != 0) return 1;
      }
    }
    if (forwarded) {
      CHECK(forwarded_calls.load() > forwards_before);
    }
    std::printf("26b. %s: %llu cases equal to the serial walk, no overlap, no event\n", name, static_cast<unsigned long long>(cases - before));
    return 0;
  };
  if (scripts("ParticlePath (spawn query / write, random triple, surface random)", "PRP", 0, 4 * scale, false) != 0) return 1;
  if (scripts("SlotOrder (slot take and return)", "SSR", 0, 4 * scale, false) != 0) return 1;
  if (scripts("SoundLog (the four sound sites, event dispatch)", "NND", 0, 4 * scale, false) != 0) return 1;
  if (scripts("BurstStatics", "BBR", 0, 3 * scale, false) != 0) return 1;
  if (scripts("AttachOrder (attach pair and list)", "AAR", 0, 3 * scale, false) != 0) return 1;
  {
    if (scripts("RigidAddOrder (create, forwarded add, random fill)", "GGFR", 0, 4 * scale, true) != 0) return 1;
    // Every forwarded call was made by the render thread.
    Shape shape;
    shape.ops = "GF";
    shape.max_ops = 4;
    shape.effects = 10;
    shape.max_units = 40;
    auto serial = Build(21, shape);
    auto parallel = Build(21, shape);
    CHECK(pool(4) == 3);
    Walk(*serial, false);
    Walk(*parallel, true);
    CHECK(Equal(*serial, *parallel, &why));
    size_t off_render = 0;
    for (const DWORD id : parallel->rigid_threads) off_render += (id != render_thread) ? 1 : 0;
    CHECK(!parallel->rigid_threads.empty() && off_render == 0);
    std::printf("26b. every one of %zu forwarded RIGID_ADD / RANDOM_FILL calls ran on the render thread, in the serial order\n", parallel->rigid_threads.size());
  }

  // 26c. ChildSet: children into any effect (earlier, the same, later: late units), TrackedChild, NewType: a type inserted before and after the current slot.
  if (scripts("ChildSet (children into earlier, same and later effects)", "CCRK", 0, 5 * scale, false) != 0) return 1;
  if (scripts("TrackedChild (tracked children, children into the next effects)", "KCKS", 3, 4 * scale, false) != 0) return 1;
  {
    Shape shape;
    shape.ops = "YRCY";
    shape.max_ops = 4;
    shape.effects = 12;
    shape.max_units = 24;
    shape.gap = 3;
    const uint64_t before = cases;
    for (uint32_t workers : {2u, 4u, 6u}) {
      for (uint32_t seed = 1; seed <= 5 * scale; ++seed) {
        if (run_case(shape, 3000 + seed * 13 + workers, 2, workers) != 0) return 1;
      }
    }
    std::printf("26c. NewType (a type registered in the walk, its first child; slots before and after the current effect): %llu cases equal\n", static_cast<unsigned long long>(cases - before));
  }

  // 26f. L2.
  {
    const auto counter = [](ei::Counter which) { return ei::counters[static_cast<size_t>(which)].load(); };
    ResetScheduler();
    ei::spawns_row = true;
    CHECK(trace_open(60, 4));
    eu::Update(true, true);  // a present inside the window (the add-on's Update runs at every present): the window is the one whose headroom is printed
    Shape shape;
    shape.ops = "LLENMS";
    shape.max_ops = 5;
    shape.effects = 14;
    shape.max_units = 30;
    shape.spawn_room = 100000;
    const uint64_t before = cases;
    for (const bool gpu : {false, true}) {
      shape.gpu = gpu;
      for (uint32_t workers : {2u, 3u, 5u, 7u}) {
        for (uint32_t seed = 1; seed <= 3 * scale; ++seed) {
          shape.busy = (seed & 1) != 0 ? 60 : 600;
          if (run_case(shape, 5000 + seed * 29 + workers + (gpu ? 500 : 0), 2, workers) != 0) return 1;
        }
      }
    }
    CHECK(counter(ei::Counter::STAGED_SPAWNS) > 0 && counter(ei::Counter::DEFERRED_DRAWS) > 0 && counter(ei::Counter::SPAWN_ROUNDS) >= cases - before
          && counter(ei::Counter::SPAWN_OVERFLOWS) == 0 && counter(ei::Counter::TAG_LEAKS) == 0 && counter(ei::Counter::GUARD_ROUNDS) == 0);
    std::printf("26f. SpawnEqualsSerial (CPU and GPU pipelines; the create path with and without events, the draw's other callers, sound and slots): %llu cases equal to the serial walk "
                "(the buffer, its count, the random state, the pool's words): %u spawns merged, %u draws deferred, no overflow, no tag leak\n",
                static_cast<unsigned long long>(cases - before), counter(ei::Counter::STAGED_SPAWNS), counter(ei::Counter::DEFERRED_DRAWS));
    CHECK(trace_close(4));
    // The window's stack headroom is printed once, by the first present after it: positive, and under the stack's whole reservation.
    {
      ei::written = eu::ALL_SITES;
      {
        const std::unique_lock lock(log_mutex);
        log_lines.clear();
      }
      eu::Update(true);
      eu::Update(true);
      ei::written = 0;
      size_t printed = 0;
      uint32_t kilobytes = 0;
      const std::unique_lock lock(log_mutex);
      for (const std::string& line : log_lines) {
        const size_t at = line.find("stack headroom ");
        if (at != std::string::npos && line.find("Parallel Effect Update (trace window") != std::string::npos) {
          ++printed;
          kilobytes = static_cast<uint32_t>(std::stoul(line.substr(at + 15)));
        }
      }
      CHECK(printed == 1 && kilobytes > 0 && kilobytes < 1024);
      std::printf("26f. the trace window's stack headroom: one line, %u KB\n", kilobytes);
    }

    // N5 (the B1.4 review's conditions). S1: a sweep whose destructors draw the random triple (the render thread's ordered pseudo-unit; the draw is neither answered nor staged and
    // raises no census error), rows L1 and L2, with units that draw and spawn before it. S2: the spawn functions of a node that is not the round's: the unit waits its turn like at any
    // order point, the handler never answers them, and the second node's spawns come out in the serial order (its own buffer is not the round's).
    for (const bool spawns : {false, true}) {
      for (const bool gpu : {false, true}) {
        for (const uint32_t workers : {2u, 4u, 7u}) {
          for (uint32_t seed = 1; seed <= 2 * scale; ++seed) {
            ResetScheduler();
            ei::spawns_row = spawns;
            Shape drawing;
            drawing.ops = "LERPS";
            drawing.max_ops = 4;
            drawing.effects = 12;
            drawing.max_units = 25;
            drawing.dead_percent = 45;
            drawing.spawn_room = 100000;
            drawing.gpu = gpu;
            drawing.sweep_draws = true;
            drawing.busy = (seed & 1) != 0 ? 60 : 400;
            if (run_case(drawing, 8000 + seed * 31 + workers + (gpu ? 700 : 0), 2, workers) != 0) return 1;
            ResetScheduler();
            ei::spawns_row = spawns;
            Shape other;
            other.ops = "LEOOPM";
            other.max_ops = 5;
            other.effects = 12;
            other.max_units = 25;
            other.spawn_room = 100000;
            other.other_room = 90;
            other.gpu = gpu;
            other.busy = (seed & 1) != 0 ? 60 : 400;
            if (run_case(other, 9000 + seed * 37 + workers + (gpu ? 700 : 0), 2, workers) != 0) return 1;
          }
        }
      }
    }
    {
      // The cases did what they claim: a sweep drew, the second node took spawns and ran out of room, and a round in L2 staged the first node's.
      ResetScheduler();
      ei::spawns_row = true;
      CHECK(pool(4) == 3);
      Shape both;
      both.ops = "LOOE";
      both.max_ops = 5;
      both.effects = 12;
      both.max_units = 25;
      both.dead_percent = 45;
      both.spawn_room = 100000;
      both.other_room = 30;
      both.sweep_draws = true;
      if (run_case(both, 9999, 1, 4) != 0) return 1;
      auto serial = Build(9999, both);
      auto parallel = Build(9999, both);
      Walk(*serial, false);
      Walk(*parallel, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*parallel) && Equal(*serial, *parallel, &why));
      size_t draws = 0;
      for (const LogEntry& entry : serial->log) draws += (entry.op == 'd' ? 1 : 0);
      CHECK(draws > 0 && serial->other_count == both.other_room && parallel->other_count == both.other_room && !serial->spawn_buffer.empty()
            && ei::event_count.load() == 0);
      std::printf("26f. N5: sweeps whose destructors draw (%zu draws in the case, no census error) and a second node's spawn functions (its %u spawns, the room full, never answered): equal to the serial walk, "
                  "L1 and L2, CPU and GPU pipelines\n", draws, parallel->other_count);
    }

    // The buffer fills while units run ahead: the overflow is counted and the merged buffer is the serial one (its first records).
    for (const bool gpu : {false, true}) {
      ResetScheduler();
      ei::spawns_row = true;
      CHECK(pool(4) == 3);
      Shape over;
      over.ops = "LL";
      over.max_ops = 5;
      over.effects = 14;
      over.min_units = 20;
      over.max_units = 30;
      over.busy = 50;
      over.spawn_room = 700;
      over.gpu = gpu;
      auto serial = Build(6001, over);
      auto parallel = Build(6001, over);
      Walk(*serial, false);
      Walk(*parallel, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*parallel));
      const size_t kept = gpu ? 700 : 613;  // the buffer's room; the CPU pipeline's simulation room is a little smaller
      CHECK(serial->spawn_buffer.size() == kept && serial->spawn_buffer == parallel->spawn_buffer);
      CHECK(counter(ei::Counter::SPAWN_OVERFLOWS) >= 1 && ei::event_count.load() >= 1 && !ei::session_off && parallel->overlap.load() == 0);
    }
    std::printf("26f. OverflowDetected (CPU and GPU): a buffer of 700 / 613 records that fills while units run ahead: the overflow counted and stored, the merged buffer equal to the serial one\n");

    // M1: the pool is sized by the budget, not by the units. The first L2 round (no pool yet) with more spawning units than one block of the old per-unit chunks held: staged whole,
    // equal to the serial walk, nothing full, L2 stays on.
    CHECK(trace_open(60, 4));
    for (const bool gpu : {false, true}) {
      ResetScheduler();
      FreeRecords();
      ei::spawns_row = true;
      CHECK(pool(4) == 3);
      Shape many;
      many.ops = "LL";
      many.max_ops = 3;
      many.effects = 90;
      many.min_units = 99;
      many.max_units = 101;
      many.busy = 20;
      many.spawn_room = 100000;
      many.gpu = gpu;
      auto serial = Build(6101, many);
      auto parallel = Build(6101, many);
      Walk(*serial, false);
      Walk(*parallel, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*parallel) && Equal(*serial, *parallel, &why));
      CHECK(counter(ei::Counter::STAGED_SPAWNS) > 2 * ei::BLOCK_SIZE && ei::last_round_units > ei::BLOCK_SIZE && counter(ei::Counter::STAGE_FULLS) == 0
            && counter(ei::Counter::SPAWN_OVERFLOWS) == 0 && counter(ei::Counter::TAG_LEAKS) == 0 && !ei::spawns_off && !ei::session_off && !ei::stage_skip);
    }
    CHECK(trace_close(4));
    std::printf("26f. M1 (CPU and GPU): the first L2 round, a pool sized by the budget, more than 4096 units spawning: staged whole, equal to the serial walk, no full pool, L2 stays on\n");

    // A pool that is full (the units stage more than the pool holds): an overflow, counted; the next round does not stage and equals the serial walk; the round after has a pool grown
    // to what the full one asked for and merges the serial buffer.
    CHECK(trace_open(60, 4));
    for (const bool gpu : {false, true}) {
      ResetScheduler();
      FreeRecords();
      ei::spawns_row = true;
      CHECK(pool(4) == 3);
      Shape full;
      full.ops = "LL";
      full.max_ops = 3;
      full.effects = 90;
      full.min_units = 99;
      full.max_units = 101;
      full.busy = 20;
      full.spawn_room = 3000;
      full.gpu = gpu;
      auto first = Build(6102, full);
      Walk(*first, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*first));
      CHECK(counter(ei::Counter::STAGE_FULLS) > 0 && counter(ei::Counter::SPAWN_OVERFLOWS) > 0 && ei::stage_skip && ei::stage_grow > ei::BLOCK_SIZE && !ei::spawns_off && !ei::session_off);
      const uint32_t fulls = counter(ei::Counter::STAGE_FULLS);
      // The round after: a guard round.
      ei::last_round_spawned = 0;
      auto serial = Build(6102, full);
      auto second = Build(6102, full);
      Walk(*serial, false);
      Walk(*second, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*second) && Equal(*serial, *second, &why));
      CHECK(counter(ei::Counter::GUARD_ROUNDS) == 1 && !ei::stage_skip && counter(ei::Counter::STAGE_FULLS) == fulls);
      // The round after that: the pool fits, the buffer is the serial one, L2 still on.
      ei::last_round_spawned = 0;
      auto third = Build(6102, full);
      Walk(*third, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*third));
      CHECK(counter(ei::Counter::STAGE_FULLS) == fulls && counter(ei::Counter::SPAWN_OVERFLOWS) > 0 && serial->spawn_buffer == third->spawn_buffer && !ei::spawns_off && !ei::session_off);
    }
    CHECK(trace_close(4));
    std::printf("26f. full pool (CPU and GPU): an overflow counted, no off for the session; the next round a guard round equal to the serial walk, the one after with a grown pool and the "
                "serial buffer\n");

    // Whether the stored events hold the SPAWNS_OFF event with this reason.
    const auto spawns_off_reason = [](ei::SpawnsOffReason reason) {
      for (uint32_t event = 0; event < std::min<uint32_t>(ei::event_count.load(), static_cast<uint32_t>(ei::events.size())); ++event) {
        if (ei::events[event].kind == ei::EventKind::SPAWNS_OFF && ei::events[event].value == static_cast<uint32_t>(reason)) return true;
      }
      return false;
    };
    // The cap: a pool at 4 times the budget's pool that is full turns L2 off for the session, with its reason stored once (B1 stays on); a pool that cannot be reserved does too.
    for (const bool gpu : {false, true}) {
      ResetScheduler();
      FreeRecords();
      ei::spawns_row = true;
      CHECK(pool(4) == 3);
      Shape capped;
      capped.ops = "LL";
      capped.max_ops = 3;
      capped.effects = 90;
      capped.min_units = 99;
      capped.max_units = 101;
      capped.busy = 20;
      capped.spawn_room = 600;  // the cap (4 x the budget's pool: 4024) is less than one block (4096): the first pool is at it
      capped.gpu = gpu;
      auto world = Build(6103, capped);
      Walk(*world, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*world) && ei::spawns_off && !ei::session_off && ei::stage_skip);
      CHECK(spawns_off_reason(ei::SpawnsOffReason::CAP_REACHED));
      // Off for good: the next round does not stage (not a guard round either: the row is not asked for any more) and equals the serial walk.
      auto serial = Build(6103, capped);
      auto next = Build(6103, capped);
      Walk(*serial, false);
      Walk(*next, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*next) && Equal(*serial, *next, &why));
    }
    {
      // A round that ran, so the units and segments are reserved: only the pool's block is allocated next, and that fails.
      ResetScheduler();
      FreeRecords();
      ei::spawns_row = true;
      CHECK(pool(4) == 3);
      Shape reserved = shape;
      reserved.spawn_room = 100000;
      auto warm = Build(6104, reserved);
      Walk(*warm, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*warm) && !ei::spawns_off);
      FreeRecords();
      ei::controls.allocations = 0;
      ei::controls.fail_allocation_at = 1;
      auto serial = Build(6104, reserved);
      auto failing = Build(6104, reserved);
      Walk(*serial, false);
      Walk(*failing, true);
      ei::controls.fail_allocation_at = 0;
      CHECK(finish_round_checks(*failing) && Equal(*serial, *failing, &why) && ei::spawns_off && !ei::session_off);
      CHECK(spawns_off_reason(ei::SpawnsOffReason::RESERVATION));
    }
    std::printf("26f. L2 off for the session: a full pool at its cap, a pool that cannot be reserved: the event with its reason, the walk equal to the serial one, B1 stays on\n");

    // The guard: less room than the minimum, or the last round's spawns past half of it: a round that does not stage (the order-point path, equal to the serial walk).
    CHECK(trace_open(60, 4));
    for (const uint32_t mode : {0u, 1u}) {
      ResetScheduler();
      ei::spawns_row = true;
      CHECK(pool(4) == 3);
      Shape guarded = shape;
      guarded.spawn_room = (mode == 0 ? 300 : 1000);
      auto serial = Build(6002, guarded);
      auto parallel = Build(6002, guarded);
      Walk(*serial, false);
      if (mode == 1) ei::last_round_spawned = 600;
      Walk(*parallel, true);
      CHECK(Equal(*serial, *parallel, &why) && finish_round_checks(*parallel) && ei::last_round_units != SENTINEL_UNITS);
      CHECK(counter(ei::Counter::STAGED_SPAWNS) == 0 && counter(ei::Counter::SPAWN_ROUNDS) == 0 && counter(ei::Counter::GUARD_ROUNDS) == 1 && ei::event_count.load() == 0);
    }
    CHECK(trace_close(4));
    std::printf("26f. guard: a room of 300 (below the minimum) and a round after one that spawned 600 of 1000: no staging, the order-point path, equal to the serial walk, each counted\n");

    // S10: the slot pool's words are watched from the round's first slot site: a rogue write to the count is seen at the next direct unit or the walk's end.
    {
      Shape pool_shape;
      pool_shape.ops = "SWSN";
      pool_shape.effects = 10;
      pool_shape.min_units = 20;
      pool_shape.max_units = 40;
      pool_shape.max_ops = 6;
      pool_shape.busy = 100;
      CHECK(pool(4) == 3);
      uint32_t seen = 0;
      bool pool_word = false;
      for (uint32_t seed = 1; seed <= 30 && seen == 0; ++seed) {
        ResetScheduler();
        auto parallel = Build(7000 + seed, pool_shape);
        Walk(*parallel, true);
        CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*parallel));
        seen = counter(ei::Counter::WATCHED_MISMATCHES);
        for (uint32_t event = 0; event < std::min<uint32_t>(ei::event_count.load(), static_cast<uint32_t>(ei::events.size())); ++event) {
          pool_word = pool_word || (ei::events[event].kind == ei::EventKind::WATCHED_MISMATCH && ei::events[event].value == parallel->pool + 0x10);
        }
      }
      CHECK(seen != 0 && pool_word && ei::session_off);
      ResetScheduler();
      // M2: a pool that never gave a slot has no limits object ([pool+0x18] null): the round must not read through it (the min / max words are left out).
      {
        Shape bare_shape = pool_shape;
        bare_shape.ops = "SN";
        auto bare = Build(7100, bare_shape);
        SetWord(bare->pool + 0x18, 0);
        Walk(*bare, true);
        CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*bare));
        ResetScheduler();
      }
      std::printf("26f. slot pool words (S10): a rogue write to the pool's count with no order point: seen (%u), the event names the word, B1 off for the session; the legal changes of the many other rounds never were\n", seen);
    }

    // N9: a walk call from a thread that is not the render thread, no round open: answered 0, nothing written (the gate, the node and the counters stay).
    {
      ResetScheduler();
      CHECK(pool(4) == 3);
      ei::row_on = true;
      ei::last_node = 0x1234;
      data->walk_armed.store(1);
      std::thread([] { Touch(eu::Site::WALK_CALL, 0, 0, 0x4321); }).join();
      CHECK(ei::last_node == 0x1234 && data->walk_armed.load() == 1 && !ei::current_round.open && counter(ei::Counter::CENSUS_ERRORS) == 0 && eax_violations.load() == 0);
      data->walk_armed.store(0);
      ResetScheduler();
      std::printf("26f. walk call from another thread with no round open: answered 0, the gate, the node and the counters untouched (N9)\n");
    }
  }

  // 26d. NoDeadlock: tiny effects, children into the next ones, K = 2 to 8, delays between the effects; a watchdog ends the process if a round hangs.
  {
    std::atomic<bool> done = false;
    std::atomic<uint64_t> beat = 0;
    std::thread watchdog([&] {
      uint64_t last = ~0ull;
      for (int tick = 0; !done.load(); ++tick) {
        Sleep(100);
        if (tick % 200 == 199) {
          if (beat.load() == last) {
            std::printf("FAIL: NoDeadlock hung\n");
            std::fflush(stdout);
            TerminateProcess(GetCurrentProcess(), 3);
          }
          last = beat.load();
        }
      }
    });
    Shape shape;
    shape.ops = "CCKRC";
    shape.max_ops = 3;
    shape.effects = 30;
    shape.min_units = 1;
    shape.max_units = 3;
    shape.child_reach = 2;
    shape.busy = 40;
    const uint64_t before = cases;
    for (uint32_t workers = 1; workers <= 7; ++workers) {
      for (uint32_t seed = 1; seed <= 6 * scale; ++seed) {
        shape.enumerate_delay = (seed % 3 == 0) ? 15 : 0;
        shape.child_reach = 1 + seed % 4;
        if (run_case(shape, 4000 + seed * 7 + workers, 2, workers) != 0) {
          done = true;
          watchdog.join();
          return 1;
        }
        beat.fetch_add(1);
      }
    }
    done = true;
    watchdog.join();
    std::printf("26d. NoDeadlock: %llu cases of 30 tiny effects with children into the next 1 to 4, 1 to 7 workers, delays between effects: no hang, equal\n",
                static_cast<unsigned long long>(cases - before));
  }

  // Frame.eax and the sites: every call so far wrote eax only where it answered (counted in Touch); the answers 1 came at forwarded sites and the walk's call only.
  CHECK(eax_violations.load() == 0);

  // 26d. AllocationFailure: a failure injected at every block, before the round (serial for the walk) and inside it (the units drained, the rest serial). The second shape
  // registers new effect types in the walk: a unit that runs after the failure must not make a late segment (the walk's rest takes that effect the game's way).
  {
    // Blocks: the scheduler's tables are rebuilt from nothing each time so that the allocations of a round are counted.
    const auto clear_blocks = [] {
      for (uint32_t block = 0; block < ei::units.capacity / ei::BLOCK_SIZE; ++block) delete[] ei::units.blocks[block];
      for (uint32_t block = 0; block < ei::segments.capacity / ei::BLOCK_SIZE; ++block) delete[] ei::segments.blocks[block];
      FreeRecords();
      ei::units = {};
      ei::segments = {};
    };
    const auto failure_sweep = [&](const Shape& shape, uint64_t seed, const char* name, bool spawns = false) -> int {
      CHECK(pool(4) == 3);
      CHECK(trace_open(60, 4));
      clear_blocks();
      ei::spawns_row = spawns;
      ei::controls.fail_allocation_at = 0xFFFFFFFF;  // counting only
      {
        auto serial = Build(seed, shape);
        auto parallel = Build(seed, shape);
        Walk(*serial, false);
        Walk(*parallel, true);
        CHECK(Equal(*serial, *parallel, &why));
      }
      const uint32_t clean_allocations = ei::controls.allocations;
      CHECK(clean_allocations >= 3);
      uint32_t drained = 0;
      uint32_t before_round = 0;
      for (uint32_t failing = 1; failing <= clean_allocations + 1; ++failing) {
        clear_blocks();
        ResetScheduler();
        ei::spawns_row = spawns;
        ei::controls.fail_allocation_at = failing;
        auto serial = Build(seed, shape);
        auto parallel = Build(seed, shape);
        ei::event_count = 0;
        Walk(*serial, false);
        Walk(*parallel, true);
        if (!Equal(*serial, *parallel, &why)) {
          std::printf("   %s: failure at allocation %u: %s\n", name, failing, why.c_str());
          return 1;
        }
        CHECK(finish_round_checks(*parallel) && data->walk_armed.load() == 0);
        const bool failed_in_round = ei::counters[static_cast<size_t>(ei::Counter::FAILED_ROUNDS)].load() != 0;
        const bool serial_walk = ei::serial_rounds[static_cast<size_t>(ei::SerialReason::ALLOCATION)].load() != 0;
        drained += failed_in_round ? 1 : 0;
        before_round += serial_walk ? 1 : 0;
        CHECK(failing > clean_allocations || failed_in_round || serial_walk);
      }
      ResetScheduler();
      CHECK(trace_close(4));
      std::printf("26d. AllocationFailure (%s): a failure at each of the %u block allocations of a round and one past: %u before the round (serial), %u inside it (drained, the rest serial); the "
                  "result equal to the game's loop every time\n", name, clean_allocations, before_round, drained);
      CHECK(before_round + drained == clean_allocations);
      return 0;
    };
    {
      Shape shape;
      shape.ops = "R";
      shape.max_ops = 2;
      shape.effects = 90;
      shape.min_units = 99;
      shape.max_units = 101;
      shape.busy = 20;
      if (failure_sweep(shape, 31, "plain") != 0) return 1;
    }
    {
      Shape shape;
      shape.ops = "YR";
      shape.max_ops = 3;
      shape.effects = 60;
      shape.min_units = 70;
      shape.max_units = 80;
      shape.busy = 20;
      shape.gap = 4;
      if (failure_sweep(shape, 33, "new types") != 0) return 1;
    }
    {
      // The failure's drain commits and merges the staged spawns before the walk is closed; the walk's rest (the game's own loop) finds the buffer as serial left it.
      Shape shape;
      shape.ops = "LLNS";
      shape.max_ops = 2;
      shape.effects = 90;
      shape.min_units = 99;
      shape.max_units = 101;
      shape.busy = 20;
      shape.spawn_room = 12000;
      if (failure_sweep(shape, 35, "L2 spawns", true) != 0) return 1;
      shape.gpu = true;
      if (failure_sweep(shape, 36, "L2 spawns, GPU", true) != 0) return 1;
    }
    {
      // R1: a type registered in the drain takes the free slot right after its unit's effect, between the last enqueued effect and the one whose walk call failed: the game's
      // own loop is past that slot, so the type needs a late segment (its first child is updated in this walk as serial does).
      Shape shape;
      shape.ops = "YR";
      shape.max_ops = 3;
      shape.effects = 60;
      shape.min_units = 70;
      shape.max_units = 80;
      shape.busy = 20;
      shape.gap = 4;
      shape.types_near = true;
      shape.max_types = 60;
      if (failure_sweep(shape, 37, "new types right after the failing effect") != 0) return 1;
    }
  }

  // The serial reasons: effect instances below the threshold, a new node, no workers, the session off. Each walk equals the game's loop and leaves the gate closed; the table
  // is summed only where the rule decides (not before a new node, the session or the workers say serial).
  {
    Shape shape;
    shape.ops = "R";
    shape.effects = 6;
    shape.max_units = 20;
    CHECK(trace_open(60, 4));
    const auto reason_case = [&](const char* name, ei::SerialReason reason, uint32_t workers, auto&& setup) -> int {
      ResetScheduler();
      CHECK(pool(workers + 1) == workers);
      auto serial = Build(41, shape);
      auto parallel = Build(41, shape);
      Walk(*serial, false);
      world = parallel.get();
      ei::entries.random_state = &parallel->lcg;
      ei::row_on = true;
      ei::last_round_units = SENTINEL_UNITS;
      ei::last_node = Address(parallel->node);
      setup(*parallel);
      data->walk_armed.store(1);
      GameWalk(*parallel, false);
      data->walk_armed.store(0);
      CHECK(Equal(*serial, *parallel, &why) && ei::serial_rounds[static_cast<size_t>(reason)].load() == 1 && finish_round_checks(*parallel));
      CHECK(ei::controls.table_sums == (reason == ei::SerialReason::FEW_UNITS ? 1u : 0u));
      std::printf("26d. serial reason \"%s\": the walk equals the game's loop\n", name);
      return 0;
    };
    if (reason_case("effect instances below the threshold", ei::SerialReason::FEW_UNITS, 3, [](World&) { ei::unit_rule = {0, 100000, 100000}; }) != 0) return 1;
    if (reason_case("a new node", ei::SerialReason::NEW_NODE, 3, [](World&) { ei::last_node = 0x1234; }) != 0) return 1;
    if (reason_case("no workers", ei::SerialReason::NO_WORKERS, 0, [](World&) {}) != 0) return 1;
    if (reason_case("the session is off", ei::SerialReason::SESSION_OFF, 3, [](World&) { ei::session_off = true; }) != 0) return 1;
    // Outside a trace window the reasons are not counted: the same walk, the gate closed all the same.
    CHECK(trace_close(4));
    {
      ResetScheduler();
      CHECK(pool(4) == 3);
      auto serial = Build(41, shape);
      auto parallel = Build(41, shape);
      Walk(*serial, false);
      world = parallel.get();
      ei::entries.random_state = &parallel->lcg;
      ei::row_on = true;
      ei::unit_rule = {0, 100000, 100000};
      ei::last_node = Address(parallel->node);
      data->walk_armed.store(1);
      GameWalk(*parallel, false);
      data->walk_armed.store(0);
      CHECK(Equal(*serial, *parallel, &why) && ei::serial_rounds[static_cast<size_t>(ei::SerialReason::FEW_UNITS)].load() == 0 && finish_round_checks(*parallel)
            && ei::controls.table_sums == 1 && ei::sum_trace.sums == 0);
      // The window's own counts are printed by the first present after it; presents after that print nothing: no counter line, no serial reason.
      ei::written = eu::ALL_SITES;
      eu::Update(true);
      {
        const std::unique_lock lock(log_mutex);
        log_lines.clear();
      }
      for (int present = 0; present < 5; ++present) eu::Update(true);
      ei::written = 0;
      size_t printed = 0;
      {
        const std::unique_lock lock(log_mutex);
        for (const std::string& line : log_lines) printed += (line.find("Parallel Effect Update") != std::string::npos) ? 1 : 0;
      }
      CHECK(printed == 0);
      std::printf("26d. serial reasons outside a trace window: not counted, five presents print nothing\n");
    }
    ResetScheduler();
  }

  // Automatic L1: the rule on the table's sum (the game's key test), its hysteresis and floor, the resets, the slot limit, the bench env and the switch line (once per switch).
  // Every walk equals the game's loop. (NO_WORKERS before the sum: the serial reasons above.)
  {
    const auto clear_log = [] {
      const std::unique_lock lock(log_mutex);
      log_lines.clear();
    };
    const auto lines_with = [](std::string_view text) {
      const std::unique_lock lock(log_mutex);
      size_t found = 0;
      for (const std::string& line : log_lines) found += (line.find(text) != std::string::npos ? 1 : 0);
      return found;
    };
    const auto serial_count = [](ei::SerialReason reason) { return ei::serial_rounds[static_cast<size_t>(reason)].load(); };
    // A fresh world of `effects` x `each` instances (its node the last one) walked by the game's loop and with the row on: equal, summed to effects x each; *ran: a round ran.
    const auto rule_walk = [&](uint32_t effects, uint32_t each, uint64_t seed, bool* ran) -> int {
      Shape shape;
      shape.ops = "R";
      shape.effects = effects;
      shape.min_units = each;
      shape.max_units = each;
      shape.busy = 20;
      auto serial = Build(seed, shape);
      auto parallel = Build(seed, shape);
      Walk(*serial, false);
      Walk(*parallel, true);
      CHECK(Equal(*serial, *parallel, &why) && finish_round_checks(*parallel) && ei::sum_trace.instances == effects * each);
      *ran = (ei::last_round_units != SENTINEL_UNITS);
      return 0;
    };
    CHECK(trace_open(200, 4));
    CHECK(pool(4) == 3);

    // The hysteresis at {0, 200, 160}: 100 -> 210 -> 170 -> 150 -> 210 instances run serial, parallel, parallel, serial, parallel; a line at each switch only.
    {
      ResetScheduler();
      clear_log();
      ei::unit_rule = {0, 200, 160};
      struct Step {
        uint32_t each;
        bool parallel;
        const char* line;
      };
      const std::array<Step, 5> steps = {{{10, false, nullptr},
                                          {21, true, "Parallel Effect Update: parallel from here (210 effect instances, from 200)."},
                                          {17, true, nullptr},
                                          {15, false, "Parallel Effect Update: serial from here (150 effect instances, below 160)."},
                                          {21, true, "Parallel Effect Update: parallel from here (210 effect instances, from 200)."}}};
      size_t switches = 0;
      for (size_t step = 0; step < steps.size(); ++step) {
        bool ran = !steps[step].parallel;
        if (rule_walk(10, steps[step].each, 500 + step, &ran) != 0) return 1;
        CHECK(ran == steps[step].parallel && ei::was_parallel == ran);
        eu::Update(true);
        switches += (steps[step].line != nullptr ? 1 : 0);
        CHECK(lines_with("from here") == switches && (steps[step].line == nullptr || lines_with(steps[step].line) != 0));
        if (step == 0) {
          // The trace window's line: the serial reason, the sum, the table's capacity (once a window) and its three biggest effects.
          CHECK(lines_with("serial (effect instances below the threshold) 1, table instances 100, table sum ") == 1 && lines_with(" us, table capacity 84, top effects 0x") == 1
                && lines_with(" 10 0x") == 1);
        }
      }
      CHECK(lines_with("table capacity") == 1 && lines_with("table instances 210, table sum ") == 2 && lines_with("table instances 150, table sum ") == 1);
      std::printf("26d. Automatic L1: at {0, 200, 160} 100 -> 210 -> 170 -> 150 -> 210 instances ran serial, parallel, parallel, serial, parallel, each equal to the game's loop; "
                  "%zu switch lines, one per switch; the trace window's line shows the table's instances, its capacity once and its biggest effects\n",
                  switches);
    }

    // The floor at {256, 0, 0}: 255 instances serial (below the threshold), 256 a round.
    {
      ResetScheduler();
      ei::unit_rule = {256, 0, 0};
      bool ran = true;
      if (rule_walk(15, 17, 520, &ran) != 0) return 1;
      CHECK(!ran && serial_count(ei::SerialReason::FEW_UNITS) == 1);
      if (rule_walk(16, 16, 521, &ran) != 0) return 1;
      CHECK(ran && serial_count(ei::SerialReason::FEW_UNITS) == 1);
      std::printf("26d. Automatic L1: the floor at {256, 0, 0}: 255 instances serial, 256 a round\n");
    }

    // The game's key test: a deleted slot and a free slot (the empty key, its own copy of the bytes) that keep a stale effect on a no-access page, and a live key with no
    // effect: the sum skips all three (no fault), the walk equals the game's.
    {
      ResetScheduler();
      auto* stale = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS));
      CHECK(stale != nullptr);
      Shape shape;
      shape.ops = "R";
      shape.effects = 8;
      shape.max_units = 20;
      auto serial = Build(530, shape);
      auto parallel = Build(530, shape);
      uint32_t total = 0;
      for (const Effect* effect : parallel->effects) total += effect->count;
      for (World* w : {serial.get(), parallel.get()}) {
        SetSlotKey(*w, 0, DELETED_KEY);
        w->slots[0].effect = Address(stale);
        w->slots[w->table_slots - 1].effect = Address(stale);
        SetLiveKey(*w, w->table_slots - 2);
      }
      Walk(*serial, false);
      Walk(*parallel, true);
      CHECK(Equal(*serial, *parallel, &why) && finish_round_checks(*parallel) && ei::last_round_units != SENTINEL_UNITS && ei::sum_trace.instances == total
            && ei::controls.table_sums == 1);
      CHECK(VirtualFree(stale, 0, MEM_RELEASE) != 0);
      std::printf("26d. Automatic L1: a deleted and an empty-key slot with a stale effect on a no-access page and a live key with no effect: skipped by the game's key test "
                  "(%u instances summed), the walk equal to the game's\n",
                  total);
    }

    // The resets: a new node, the row off and the session off each put a parallel state back to serial (and say so): a 170 world after it is serial at {0, 200, 160}.
    for (const char* reset : {"new node", "row off", "session off"}) {
      ResetScheduler();
      clear_log();
      ei::unit_rule = {0, 200, 160};
      bool ran = false;
      if (rule_walk(10, 21, 540, &ran) != 0) return 1;
      CHECK(ran && ei::was_parallel);
      eu::Update(true);
      if (reset == std::string_view("new node")) {
        // A walk of another node: serial (not summed), the game's own loop.
        Shape shape;
        shape.effects = 4;
        shape.max_units = 5;
        auto other = Build(541, shape);
        world = other.get();
        ei::entries.random_state = &other->lcg;
        data->walk_armed.store(1);
        GameWalk(*other, 0);
        data->walk_armed.store(0);
        CHECK(serial_count(ei::SerialReason::NEW_NODE) == 1 && ei::controls.table_sums == 1 && finish_round_checks(*other));
        eu::Update(true);
      } else if (reset == std::string_view("row off")) {
        eu::Update(false);
      } else {
        ei::session_off = true;
        eu::Update(true);
      }
      CHECK(!ei::was_parallel && lines_with(std::format("Parallel Effect Update: serial from here ({}).", reset)) == 1 && lines_with("from here") == 2);
      if (rule_walk(10, 17, 542, &ran) != 0) return 1;
      CHECK(!ran && serial_count(ei::SerialReason::FEW_UNITS) == 1);
      std::printf("26d. Automatic L1: reset by \"%s\" (one line), a 170 world after it serial at {0, 200, 160}\n", reset);
    }

    // The slot limit: a table of 4264 slots is not summed (serial, one stored note, said once at the present); 4096 slots are summed.
    {
      ResetScheduler();
      clear_log();
      Shape shape;
      shape.ops = "R";
      shape.effects = 6;
      shape.max_units = 20;
      shape.gap = 700;  // 6 x 700 + 64 slots
      auto serial = Build(560, shape);
      auto parallel = Build(560, shape);
      Walk(*serial, false);
      Walk(*parallel, true);
      CHECK(Equal(*serial, *parallel, &why) && finish_round_checks(*parallel) && ei::last_round_units == SENTINEL_UNITS && ei::controls.table_sums == 0
            && serial_count(ei::SerialReason::BIG_TABLE) == 1);
      CHECK(ei::event_count.load() == 1 && ei::events[0].kind == ei::EventKind::BIG_TABLE && ei::events[0].value == 4264);
      eu::Update(true);
      CHECK(lines_with("Parallel Effect Update: effect table above the slot limit, walks serial (site ") == 1 && lines_with("value 0x10A8)") == 1);
      auto again = Build(561, shape);
      Walk(*again, true);
      CHECK(serial_count(ei::SerialReason::BIG_TABLE) == 1 && ei::event_count.load() == 0 && finish_round_checks(*again));  // (the present's line took the first)
      shape.gap = 672;  // 6 x 672 + 64 = 4096 slots: summed
      auto limit = Build(562, shape);
      Walk(*limit, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && ei::controls.table_sums == 1 && finish_round_checks(*limit));
      std::printf("26d. Automatic L1: a table of 4264 slots walks serial without a sum (one note, said once); 4096 slots are summed\n");
    }

    // The bench env RENODX_TW_L1_UNITS=<on>:<off>, read when the generation changes: clamped to at least 256 and off to at most on; a bad value is said once and the
    // shipped rule applies; no value: the shipped rule.
    {
      ResetScheduler();
      clear_log();
      using Rule = std::array<uint32_t, 3>;
      const auto env = [](const char* value, uint32_t generation) {
        SetEnvironmentVariableA("RENODX_TW_L1_UNITS", value);
        eu::Update(true, false, generation);
        return Rule{ei::unit_rule.floor, ei::unit_rule.on, ei::unit_rule.off};
      };
      CHECK(env("30000:20000", 1) == (Rule{256, 30000, 20000})
            && lines_with("Parallel Effect Update: parallel from 30000 effect instances, serial below 20000 (RENODX_TW_L1_UNITS, bench only).") == 1);
      CHECK(env("100:50", 2) == (Rule{256, 256, 256}));
      CHECK(env("20000:30000", 3) == (Rule{256, 20000, 20000}));
      CHECK(env("bad", 4) == (Rule{256, 20000, 16000}) && lines_with("RENODX_TW_L1_UNITS=\"bad\" does not parse; parallel from 20000, serial below 16000.") == 1);
      CHECK(env("30000:", 5) == (Rule{256, 20000, 16000}) && lines_with("does not parse") == 1);
      CHECK(env("30000:20000", 5) == (Rule{256, 20000, 16000}));  // the same generation: not read again
      CHECK(env(nullptr, 6) == (Rule{256, 20000, 16000}) && lines_with("RENODX_TW_L1_UNITS") == 4);
      std::printf("26d. Automatic L1: RENODX_TW_L1_UNITS 30000:20000, 100:50 (clamped to 256), 20000:30000 (off to on), bad values said once, none: the shipped 256 / 20000 / 16000\n");
    }
    CHECK(trace_close(4));
    ResetScheduler();
  }

  // PrivateWritesOnly: with the game's structures (the node, the table, the effects, their entries) read-only for the whole round, the scheduler completes it: it only reads them.
  {
    Shape shape;
    shape.ops = "RSN";
    shape.effects = 20;
    shape.max_units = 30;
    shape.dead_percent = 0;
    CHECK(pool(4) == 3);
    auto serial = Build(51, shape);
    auto parallel = Build(51, shape);
    Walk(*serial, false);
    DWORD old = 0;
    CHECK(VirtualProtect(parallel->arena, parallel->arena_size, PAGE_READONLY, &old) != 0);
    Walk(*parallel, true);
    CHECK(VirtualProtect(parallel->arena, parallel->arena_size, PAGE_READWRITE, &old) != 0);
    CHECK(Equal(*serial, *parallel, &why) && ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*parallel));
    std::printf("26d. PrivateWritesOnly: a round over %u instances with every page of the game's structures read-only completed: the scheduler writes only its own state\n", ei::last_round_units);
  }

  // The census: the render thread at a site outside a unit in a round, a thread that runs no unit at a site: each counted once and answered 0 (the original path); the render
  // thread's makes the session stop using B1, the foreign thread's does not; the walk itself still equals the game's loop.
  {
    Shape shape;
    shape.ops = "R";
    shape.effects = 8;
    shape.max_units = 25;
    CHECK(pool(4) == 3);
    CHECK(trace_open(60, 4));
    for (const int hook : {1, 2}) {
      ResetScheduler();
      CHECK(pool(4) == 3);
      auto serial = Build(61, shape);
      auto parallel = Build(61, shape);
      Walk(*serial, false);
      Walk(*parallel, true, hook);
      CHECK(Equal(*serial, *parallel, &why) && finish_round_checks(*parallel) && ei::last_round_units != SENTINEL_UNITS);
      const auto counter = [](ei::Counter which) { return ei::counters[static_cast<size_t>(which)].load(); };
      if (hook == 1) {
        CHECK(counter(ei::Counter::CENSUS_ERRORS) == 1 && counter(ei::Counter::FOREIGN_HITS) == 0 && counter(ei::Counter::FAILED_ROUNDS) == 1 && ei::session_off);
        CHECK(ei::event_count.load() == 2);  // the census error and the failed round, once each
        std::printf("26d. census: the render thread at a site outside a unit in a round: counted once, answered 0, the round ended (drained, the rest of the walk the game's way), B1 off for the session, "
                    "the walk equal to the game's loop\n");
      } else {
        CHECK(counter(ei::Counter::FOREIGN_HITS) == 1 && counter(ei::Counter::CENSUS_ERRORS) == 0 && !ei::session_off && ei::event_count.load() == 0);
        std::printf("26d. census: a thread that runs no unit at a site while a round is open: counted once (in the trace window, no log line), answered 0, the walk equal to the game's loop\n");
      }
    }
    CHECK(trace_close(4));
    ResetScheduler();
    // A walk's call or end from a worker thread inside an open round: a census error, answered 0 (the render thread's walk goes on).
    {
      CHECK(pool(4) == 3);
      auto parallel = Build(62, shape);
      Walk(*parallel, true, 3);
      CHECK(ei::counters[static_cast<size_t>(ei::Counter::CENSUS_ERRORS)].load() == 2 && ei::session_off && finish_round_checks(*parallel));
      std::printf("26d. census: a walk call and a walk end from another thread in an open round: counted, answered 0, B1 off for the session\n");
    }
    ResetScheduler();
  }

  // The watched words: units that change the random state with no order point (a rogue write) are seen at the next direct unit or the walk's end: an event, B1 off for the session
  // after the walk; Update says so once, prints the event, and arms no more.
  {
    Shape shape;
    shape.ops = "RZRZ";
    shape.effects = 10;
    shape.max_units = 40;
    shape.min_units = 20;
    shape.max_ops = 6;
    shape.busy = 100;
    CHECK(pool(4) == 3);
    uint32_t seen = 0;
    for (uint32_t seed = 1; seed <= 20 && seen == 0; ++seed) {
      ResetScheduler();
      auto parallel = Build(70 + seed, shape);
      Walk(*parallel, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*parallel));
      seen = ei::counters[static_cast<size_t>(ei::Counter::WATCHED_MISMATCHES)].load();
    }
    const uint32_t stored_events = ei::event_count.load();  // once per site (the last direct unit's site): at most one per mismatch
    CHECK(seen != 0 && ei::session_off && stored_events != 0 && stored_events <= seen);
    {
      const std::unique_lock lock(log_mutex);
      log_lines.clear();
    }
    eu::Update(true);
    eu::Update(true);
    size_t said_off = 0;
    size_t said_changed = 0;
    {
      const std::unique_lock lock(log_mutex);
      for (const std::string& line : log_lines) {
        said_off += (line.find("off for the session") != std::string::npos) ? 1 : 0;
        said_changed += (line.find("watched word changed") != std::string::npos) ? 1 : 0;
      }
    }
    CHECK(said_off == 1 && said_changed == stored_events && data->walk_armed.load() == 0);
    std::printf("26d. watched words: %u rogue changes seen, B1 off for the session after the walk; Update printed the %u events (one per site) once and said \"off\" once, and arms no more\n", seen,
                stored_events);
    // An event kind and site is stored once per session.
    ResetScheduler();
    ei::Note(ei::EventKind::HANG, 3, 1);
    ei::Note(ei::EventKind::HANG, 3, 2);
    ei::Note(ei::EventKind::HANG, 4, 3);
    ei::Note(ei::EventKind::CENSUS_ERROR, 3, 4);
    CHECK(ei::event_count.load() == 3);
    ResetScheduler();
    // The gate arms only with the whole set of sites written.
    eu::Update(true);
    CHECK(data->walk_armed.load() == 0);
    ei::written = eu::ALL_SITES & ~(1u << 5);
    eu::Update(true);
    CHECK(data->walk_armed.load() == 0);
    ei::written = eu::ALL_SITES;
    eu::Update(true);
    CHECK(data->walk_armed.load() == 1);
    eu::Update(false);
    CHECK(data->walk_armed.load() == 0 && data->walk_active.load() == 0);
    ei::written = 0;
    std::printf("26d. Update: the row on arms the walk's gate when every site is written, never with a site missing; off disarms it\n");
  }

  // The scheduler's stack: the deepest committed page of each pool thread and the render thread after everything above (the nesting is one wait inside one unit; the thunk's
  // slow frame is 572 bytes and the handler's frames come on top in the game).
  {
    CHECK(pool(8) == 7);
    deepest_stack = 0;
    wp::Start({.work = &StackProbe, .user = wp::User::EFFECT_UPDATE}, 7);
    StackProbe(nullptr, 0);
    wp::Join();
    std::printf("26d. StackHighWater: %u bytes committed at the deepest of the render thread and 7 pool threads after the cases above (the pool's reservation is %u)\n", deepest_stack.load(),
                wp::internal::WORKER_STACK_BYTES);
    CHECK(deepest_stack.load() != 0 && deepest_stack.load() < wp::internal::WORKER_STACK_BYTES / 2);
  }

  // 26e. CONTROLs: each rule left out must show: the log or a shared word differs, two direct units overlap, a worker thread id shows, the hang / timeout counters move.
  {
    const auto shown = [&](const char* name, const Shape& shape, uint32_t workers, int rounds, auto&& arm, auto&& seen) -> int {
      ResetScheduler();
      CHECK(pool(workers + 1) == workers);
      arm();
      bool detected = false;
      int ran = 0;
      for (int round = 0; round < rounds && !detected; ++round, ++ran) {
        auto serial = Build(900 + round, shape);
        auto parallel = Build(900 + round, shape);
        Walk(*serial, false);
        Walk(*parallel, true);
        detected = seen(*serial, *parallel);
      }
      ResetScheduler();
      std::printf("26e. CONTROL %s: %s after %d round%s\n", name, detected ? "seen" : "NOT SEEN", ran, ran == 1 ? "" : "s");
      CHECK(detected);
      return 0;
    };
    const auto differs = [&](const World& a, const World& b) {
      std::string reason;
      return !Equal(a, b, &reason) || b.overlap.load() != 0;
    };
    {
      Shape shape;
      shape.ops = "RN";
      shape.effects = 6;
      shape.min_units = 40;
      shape.max_units = 60;
      shape.max_ops = 4;
      shape.busy = 150;
      if (shown("NoOrderPoints", shape, 4, 30, [] { ei::controls.no_order_points = true; }, differs) != 0) return 1;
    }
    {
      Shape shape;
      shape.ops = "YCR";
      shape.effects = 14;
      shape.min_units = 4;
      shape.max_units = 12;
      shape.gap = 3;
      shape.max_ops = 3;
      shape.busy = 30;
      shape.enumerate_delay = 60;
      if (shown("NoEnumerationWait (a type registered while the walk still enumerates: its effect is walked twice)", shape, 4, 40, [] { ei::controls.no_enumeration_wait = true; }, differs) != 0) return 1;
    }
    {
      Shape shape;
      shape.ops = "CRC";
      shape.effects = 12;
      shape.min_units = 4;
      shape.max_units = 12;
      shape.child_reach = 3;
      shape.max_ops = 3;
      if (shown("LateChildOff (a child appended to a later effect is never updated)", shape, 3, 30, [] { ei::controls.late_child_off = true; }, differs) != 0) return 1;
    }
    {
      Shape shape;
      shape.ops = "GF";
      shape.effects = 8;
      shape.min_units = 20;
      shape.max_units = 40;
      shape.max_ops = 3;
      const auto off_render = [&](const World&, const World& b) {
        for (const DWORD id : b.rigid_threads) {
          if (id != render_thread) return true;
        }
        return false;
      };
      if (shown("RigidOnWorker (forwarding off: a worker thread id in the log)", shape, 4, 20, [] { ei::controls.rigid_on_worker = true; }, off_render) != 0) return 1;
    }
    {
      Shape shape;
      shape.ops = "RS";
      shape.effects = 40;
      shape.min_units = 6;
      shape.max_units = 14;
      shape.max_ops = 3;
      shape.busy = 400;
      const auto off_render = [&](const World&, const World& b) {
        for (const DWORD id : b.sweep_threads) {
          if (id != render_thread) return true;
        }
        return false;
      };
      if (shown("SweepOnWorker (a sweep on a worker thread)", shape, 6, 200, [] { ei::controls.sweep_on_worker = true; }, off_render) != 0) return 1;
    }
    {
      // SweepEarly: the render thread sweeps an effect as soon as enumeration is done: the flagged instances are not flagged yet, the log of removals and the vectors differ.
      Shape shape;
      shape.ops = "R";
      shape.effects = 8;
      shape.min_units = 20;
      shape.max_units = 30;
      shape.max_ops = 2;
      shape.busy = 400;
      shape.dead_percent = 60;
      if (shown("SweepEarly (a sweep before its effect's units are done)", shape, 4, 60, [] { ei::controls.sweep_early = true; }, differs) != 0) return 1;
    }
    {
      // WaitOnLater: a unit waits for the frontier to pass it, which only its own end can do: nothing moves, and the hang detector trips (the control lets the round go on after the
      // first hang, so the round still ends and equals the game's loop).
      Shape shape;
      shape.ops = "RN";
      shape.effects = 5;
      shape.min_units = 6;
      shape.max_units = 9;
      shape.max_ops = 3;
      shape.busy = 100;
      const auto hung = [&](const World&, const World&) { return ei::counters[static_cast<size_t>(ei::Counter::HANGS)].load() != 0 && ei::event_count.load() != 0; };
      if (shown("WaitOnLater (a unit waits for the frontier to pass it: a hang)", shape, 3, 2, [] { ei::controls.wait_on_later = true; }, hung) != 0) return 1;
    }
    {
      // NoEpoch: the units' waits are never woken: they end by their 50 ms timeout. A trace window counts the timeouts; the same rounds with the epoch have none.
      Shape shape;
      shape.ops = "RRR";
      shape.effects = 3;
      shape.min_units = 6;
      shape.max_units = 8;
      shape.max_ops = 3;
      shape.busy = 100000;
      shape.dead_percent = 0;
      CHECK(pool(4) == 3);
      wp::ArmTrace(8);
      wp::Update({.threads = 4, .in_use = true});
      CHECK(wp::Tracing());
      const auto timeouts = [&](bool no_epoch) {
        ResetScheduler();
        ei::controls.no_epoch = no_epoch;
        auto serial = Build(950, shape);
        auto parallel = Build(950, shape);
        Walk(*serial, false);
        Walk(*parallel, true);
        std::string reason;
        const bool equal = Equal(*serial, *parallel, &reason);
        const uint32_t count = ei::counters[static_cast<size_t>(ei::Counter::WAKE_TIMEOUTS)].load();
        ResetScheduler();
        return equal ? count : 0xFFFFFFFFu;
      };
      const uint32_t with_epoch = timeouts(false);
      const uint32_t without_epoch = timeouts(true);
      for (int present = 0; present < 9; ++present) wp::Update({.threads = 4, .in_use = true});
      CHECK(!wp::Tracing());
      std::printf("26e. CONTROL NoEpoch: %u wait timeouts with the epoch, %u without (the round still completes, equal, by timeouts)\n", with_epoch, without_epoch);
      CHECK(with_epoch == 0 && without_epoch != 0 && without_epoch != 0xFFFFFFFFu);
    }
    {
      // The L2 controls: each rule left out shows in the merged buffer, the random state or the counters.
      const auto room_shape = [](const char* ops, uint32_t room) {
        Shape shape;
        shape.ops = ops;
        shape.max_ops = 5;
        shape.effects = 14;
        shape.min_units = 20;
        shape.max_units = 30;
        shape.busy = 50;
        shape.spawn_room = room;
        return shape;
      };
      if (shown("CommitReversed (the commits' draws in the reverse of the serial order)", room_shape("L", 100000), 3, 4, [] { ei::spawns_row = true; ei::controls.commit_reversed = true; },
                differs) != 0) {
        return 1;
      }
      if (shown("MergeByCompletion (the staged records merged in the reverse of the serial order)", room_shape("L", 100000), 3, 4, [] { ei::spawns_row = true; ei::controls.merge_by_completion = true; },
                differs) != 0) {
        return 1;
      }
      const auto unreported = [&](const World&, const World& b) {
        return ei::counters[static_cast<size_t>(ei::Counter::SPAWN_OVERFLOWS)].load() == 0 && b.spawn_buffer.size() == 613;  // the buffer filled and dropped, nothing said
      };
      if (shown("OverflowIgnored (a full buffer with units that ran ahead: no overflow counted)", room_shape("LL", 700), 3, 6, [] { ei::spawns_row = true; ei::controls.overflow_ignored = true; },
                unreported) != 0) {
        return 1;
      }
      const auto leaked = [&](const World&, const World&) { return ei::counters[static_cast<size_t>(ei::Counter::TAG_LEAKS)].load() != 0; };
      if (shown("DeferAnyCaller (the draw of a caller that is not the create path deferred: its tag lands outside the record's draw dword)", room_shape("M", 100000), 3, 4,
                [] {
                  ei::spawns_row = true;
                  ei::controls.defer_any_caller = true;
                  wp::ArmTrace(8);
                  wp::Update({.threads = 4, .in_use = true});
                },
                leaked) != 0) {
        return 1;
      }
      for (int present = 0; present < 9; ++present) wp::Update({.threads = 4, .in_use = true});
      CHECK(!wp::Tracing());
    }
  }
  // 26h. B1 logging. F1: the always-counted counters outside trace windows (session totals, one line when a fault is among them, at most one a second, a window's line followed by
  // the totals, the detach's totals); F2: a slot list growth on a worker is noted once.
  {
    const auto clear_log = [] {
      const std::unique_lock lock(log_mutex);
      log_lines.clear();
    };
    const auto lines_with = [](const char* text) {
      const std::unique_lock lock(log_mutex);
      size_t found = 0;
      for (const std::string& line : log_lines) found += (line.find(text) != std::string::npos ? 1 : 0);
      return found;
    };
    const auto add = [](ei::Counter counter, uint32_t amount) { ei::counters[static_cast<size_t>(counter)].fetch_add(amount); };
    ei::written = eu::ALL_SITES;
    eu::Update(true);  // (what the windows before counted and nobody printed yet)
    ResetScheduler();
    clear_log();
    ei::current_round.tracing = false;
    // Outside a window: a fault among the counts prints one line (the by-design ones and the slow-path ones ride along); the trace-only counters are not counted.
    ei::Count(ei::Counter::UNITS, 9);
    ei::Count(ei::Counter::HANGS, 2);
    ei::Count(ei::Counter::SPAWN_OVERFLOWS, 5);
    ei::Count(ei::Counter::FOREIGN_HITS, 1);
    ei::Count(ei::Counter::WAKE_TIMEOUTS, 4);
    eu::Update(true);
    CHECK(lines_with("Parallel Effect Update (safety counts outside trace windows): wake timeouts 4, foreign hits 1, hangs 2, spawn overflows 5") == 1);
    CHECK(lines_with("session totals") == 0 && ei::counters[static_cast<size_t>(ei::Counter::UNITS)].load() == 0);
    // Another fault within a second: no line (the count waits); the totals hold it.
    ei::Count(ei::Counter::HANGS, 1);
    eu::Update(true);
    CHECK(lines_with("safety counts outside trace windows") == 1);
    // After the second: the waiting count and the new one in one line; by-design counts alone print nothing.
    ei::counts_line_ticks = 0;  // (the second has passed)
    ei::Count(ei::Counter::CENSUS_ERRORS, 1);
    eu::Update(true);
    CHECK(lines_with("Parallel Effect Update (safety counts outside trace windows): hangs 1, census errors 1") == 1);
    ei::counts_line_ticks = 0;  // (the limit would let it print: it is the by-design counts that do not)
    ei::Count(ei::Counter::SPAWN_OVERFLOWS, 3);
    ei::Count(ei::Counter::STAGE_FULLS, 2);
    ei::Count(ei::Counter::WAKE_TIMEOUTS, 1);
    eu::Update(true);
    CHECK(lines_with("safety counts outside trace windows") == 2);
    // A foreign thread's hit alone is a fault (until real play says otherwise): its line, with the by-design counts that waited.
    ei::Count(ei::Counter::FOREIGN_HITS, 1);
    eu::Update(true);
    CHECK(lines_with("Parallel Effect Update (safety counts outside trace windows): wake timeouts 1, foreign hits 1, spawn overflows 3, stage full 2") == 1 && lines_with("safety counts outside trace windows") == 3);
    // The detach: what was counted since the last present joins the totals, which are printed once.
    ei::Count(ei::Counter::TAG_LEAKS, 1);
    eu::Detach();
    CHECK(lines_with("Parallel Effect Update (session totals): wake timeouts 5, foreign hits 2, hangs 3, watched mismatches 0, census errors 1, late overflows 0, failed rounds 0, "
                     "spawn overflows 8, tag leaks 1, stage full 2") == 1);
    std::printf("26h. F1: counts outside trace windows: one line when a fault is among them (not for spawn overflows, stage full or wake timeouts alone; one a second at most), the session totals at the detach\n");

    // In a window: its line carries the always-counted counts; the session's totals are printed once, after the window (below).
    ResetScheduler();
    clear_log();
    CHECK(trace_open(8, 4));
    {
      Shape shape;
      shape.ops = "RN";
      shape.effects = 6;
      shape.max_units = 20;
      shape.max_ops = 3;
      if (run_case(shape, 6161, 1, 3) != 0) return 1;
    }
    add(ei::Counter::HANGS, 1);
    add(ei::Counter::SPAWN_OVERFLOWS, 2);
    eu::Update(true);
    eu::Update(true);
    {
      const std::unique_lock lock(log_mutex);
      size_t window_lines = 0;
      for (const std::string& line : log_lines) {
        window_lines += (line.find("Parallel Effect Update (trace window") != std::string::npos && line.find("hangs 1") != std::string::npos ? 1 : 0);
      }
      CHECK(window_lines == 1);  // the window's line carries the counts
    }
    CHECK(lines_with("(session totals)") == 0 && lines_with("safety counts outside trace windows") == 0);  // none inside the window
    CHECK(trace_close(4));
    eu::Update(true);
    eu::Update(true);
    CHECK(lines_with("Parallel Effect Update (session totals): ") == 1);  // once, at the first present after it
    {
      const std::unique_lock lock(log_mutex);
      size_t at = 0;
      while (at < log_lines.size() && log_lines[at].find("Parallel Effect Update (session totals): ") == std::string::npos) ++at;
      CHECK(at < log_lines.size() && log_lines[at].find("hangs 1,") != std::string::npos && log_lines[at].find("spawn overflows 2,") != std::string::npos);
    }
    // A window with the row never on (session_used false): no totals line after it.
    ResetScheduler();
    clear_log();
    CHECK(trace_open(8, 4));
    eu::Update(false);
    CHECK(trace_close(4));
    eu::Update(false);
    eu::Update(false);
    CHECK(lines_with("(session totals)") == 0 && lines_with("stack headroom") <= 1);
    std::printf("26h. F1: a trace window's line carries them; the session totals are printed once, at the first present after the window (none when the row was never on)\n");

    // F2: the game's push of an id onto the slot list grows its array when the count has reached the capacity: a unit that ran on a worker and left the capacity word changed is
    // noted once (kind and site), the new capacity the value; a unit on the render thread (the game's own thread) is not.
    ResetScheduler();
    clear_log();
    {
      uint32_t capacity_word = 63;
      ei::current_round.watched = {};
      ei::current_round.watched[0] = {.address = &capacity_word, .name = "pool capacity", .value = 63, .pool_capacity = true};
      ei::current_round.watched_count = 1;
      ei::current_round.owner = GetCurrentThreadId();
      capacity_word = 126;
      ei::RefreshWatched();
      CHECK(ei::event_count.load() == 0 && ei::current_round.watched[0].value == 126);  // the owner's unit: the game's normal growth
      capacity_word = 252;
      std::thread([] { ei::RefreshWatched(); }).join();
      CHECK(ei::event_count.load() == 1 && ei::events[0].kind == ei::EventKind::SLOT_GROWTH && ei::events[0].site == static_cast<uint32_t>(eu::Site::SLOT_RETURN)
            && ei::events[0].value == 252 && ei::events[0].thread != GetCurrentThreadId());
      capacity_word = 504;
      std::thread([] { ei::RefreshWatched(); }).join();
      CHECK(ei::event_count.load() == 1);  // once per kind and site
      ei::current_round.watched_count = 0;
      ei::current_round.owner = 0;
      eu::Update(true);
      CHECK(lines_with("Parallel Effect Update: slot list growth on a worker (site slot return, thread ") == 1 && lines_with("value 0xFC)") == 1);
    }
    // The same in a round: units that return a slot while the list is full, on workers (the render thread may run the first, so rounds are walked until a worker's is seen).
    ResetScheduler();
    clear_log();
    ei::spawns_row = false;
    CHECK(pool(5) == 4);
    {
      Shape shape;
      shape.ops = "S";
      shape.effects = 8;
      shape.max_units = 20;
      shape.max_ops = 2;
      shape.dead_percent = 10;
      shape.pool_capacity = 63;  // a unit that holds a slot has taken the list to its capacity
      bool seen = false;
      for (uint32_t seed = 0; seed < 12 && !seen; ++seed) {
        auto serial = Build(7171 + seed, shape);
        auto parallel = Build(7171 + seed, shape);
        Walk(*serial, false);
        CHECK(ei::event_count.load() == 0);
        Walk(*parallel, true);
        CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*parallel) && Equal(*serial, *parallel, &why));
        CHECK(Word(parallel->pool + 0xC) == Word(serial->pool + 0xC) && Word(serial->pool + 0xC) == 126);
        seen = (ei::event_count.load() == 1);
        CHECK(ei::event_count.load() <= 1);
        if (seen) {
          CHECK(ei::events[0].kind == ei::EventKind::SLOT_GROWTH && ei::events[0].site == static_cast<uint32_t>(eu::Site::SLOT_RETURN) && ei::events[0].value == 126
                && ei::events[0].thread != render_thread);
        }
      }
      CHECK(seen);
      eu::Update(true);
      CHECK(lines_with("Parallel Effect Update: slot list growth on a worker (site slot return, thread ") == 1 && lines_with("value 0x7E)") == 1);
    }
    ei::written = 0;
    ResetScheduler();
    std::printf("26h. F2: a slot list growth on a worker (the capacity word changed by a unit that ran there): one event line (site slot return, value = the new capacity), once per site; on the render thread: none\n");
  }

  // 26g. The game's own routines on objects laid out as the game lays them out: image B has the thunks in (the row on, the production handler), image A is the game as it ships.
  eu::Data* real_data = nullptr;
  const uint8_t* real_base = nullptr;
  {
    const auto rows = [&](bool on) {
      twattila::engine_code::Update(false, twattila::engine_code::Game::ATTILA, false, false, false, false, false, false, 1.f, 0, {.effect_update = on});
    };
    real_game = game;
    real_patched = patched;
    // The create path's call of the draw is replayed in both images by a stub before the return address the handler tests (the game's create routine itself never runs here):
    // no site and nothing else the tests use lies there.
    const auto stub = CreateDrawStub();
    const uint32_t stub_at = ei::CREATE_DRAW_RETURN_RVA - CREATE_STUB_BYTES;
    for (size_t index = 0; index < eu::SITE_COUNT; ++index) {
      const eu::SiteInfo& info = eu::SITES[index];
      CHECK(info.rva + eu::SizeOf(info) <= stub_at || info.rva >= stub_at + stub.size());
    }
    std::array<uint8_t, stub.size()> saved_game_code, saved_patched_code;
    std::memcpy(saved_game_code.data(), game + stub_at, stub.size());
    std::memcpy(saved_patched_code.data(), patched + stub_at, stub.size());
    for (uint8_t* image : {game, patched}) {
      std::memcpy(image + stub_at, stub.data(), stub.size());
      FlushInstructionCache(GetCurrentProcess(), image + stub_at, stub.size());
    }
    CHECK(eu::Prepare(reinterpret_cast<HMODULE>(patched)) && ei::found == eu::ALL_SITES);  // (the sets the sites part leaves are Prepare's to take again)
    rows(true);
    CHECK(ei::written == eu::ALL_SITES && ei::data != nullptr && ei::base == patched);
    real_data = ei::data;
    real_base = ei::base;
    eu::SetHandler(&ei::Handle);
    ei::entries.draw = reinterpret_cast<decltype(ei::entries.draw)>(patched + ei::DRAW_RVA);
    ei::entries.spawn = nullptr;  // the merge calls the pipeline's own spawn (its vtable)
    ResetScheduler();
    CHECK(pool(4) == 3);

    // The game's routines take the objects: the query and the write on the CPU and the GPU pipeline (the simulation's room ends the query first on the CPU's; the buffer's capacity
    // ends the write), the second node's, the slot pool's take and return, the create path's call equal to a plain call; in A and in B with the gates closed.
    for (uint8_t* image : {game, patched}) {
      for (const bool gpu : {false, true}) {
        Shape shape;
        shape.real = true;
        shape.spawn_room = 100;
        shape.other_room = 5;
        shape.gpu = gpu;
        for (const bool second : {false, true}) {
          auto probe = Build(11, shape, image);
          const uint32_t pipeline_node = second ? probe->other_node : Address(probe->node);
          const bool kind_gpu = second ? probe->other_gpu : gpu;
          const uint32_t room = second ? shape.other_room : shape.spawn_room;
          const uint32_t query_room = (kind_gpu ? room : room - room / 8);
          const auto query = [&] { return reinterpret_cast<RealQuery>(RealSite(*probe, eu::Site::SPAWN_QUERY))(pipeline_node); };
          const auto write = [&](const std::array<uint32_t, 11 + 12>& particle) {
            return reinterpret_cast<RealWrite>(RealSite(*probe, eu::Site::SPAWN_WRITE))(pipeline_node, Address(particle.data()));
          };
          uint32_t spawned = 0;
          std::array<uint32_t, 11 + 12> particle = {};
          const auto fill = [&] {
            for (uint32_t part = 0; part < 12; ++part) particle[11 + part] = (spawned + (second ? 5000 : 0)) * 16 + part;
          };
          while (query() != 0 && spawned < 1000) {
            fill();
            CHECK(write(particle) == 1);
            ++spawned;
          }
          CHECK(spawned == query_room);
          while (spawned < room) {
            fill();
            CHECK(write(particle) == 1);
            ++spawned;
          }
          fill();
          CHECK(write(particle) == 0);  // the buffer is full: refused
          Harvest(*probe);
          if (second) {
            CHECK(probe->other_count == room);
          } else {
            CHECK(probe->spawn_buffer.size() == room && probe->spawn_buffer[7][5] == 7 * 16 + 5 && probe->spawn_buffer[room - 1][11] == (room - 1) * 16 + 11);
          }
        }
        // The slot pool: the game's take pops the end of the list and sets the minimum and maximum; its return pushes the id back and sets the variable to -1; -1 is ignored.
        auto probe = Build(12, shape, image);
        const auto take = [&](uint32_t* slot) { return reinterpret_cast<RealPool>(RealSite(*probe, eu::Site::SLOT_TAKE))(probe->pool, slot); };
        const auto give = [&](uint32_t* slot) { reinterpret_cast<RealPool>(RealSite(*probe, eu::Site::SLOT_RETURN))(probe->pool, slot); };
        uint32_t first = NO_SLOT, second = NO_SLOT;
        CHECK(take(&first) == 1 && first == 1063 && take(&second) == 1 && second == 1062 && Word(probe->pool + 0x10) == 62);
        CHECK(Word(probe->pool_limits + 0x50) == 1062 && Word(probe->pool_limits + 0x54) == 1063);
        give(&first);
        CHECK(first == NO_SLOT && Word(probe->pool + 0x10) == 63);
        give(&first);
        CHECK(first == NO_SLOT && Word(probe->pool + 0x10) == 63);
        uint32_t again = NO_SLOT;
        CHECK(take(&again) == 1 && again == 1063);
        for (uint32_t index = 0; index < 62; ++index) {
          uint32_t slot = NO_SLOT;
          CHECK(take(&slot) == 1);
        }
        uint32_t none = 0;
        CHECK(take(&none) == 0 && none == NO_SLOT && Word(probe->pool + 0x10) == 0);
        // The create path's call of the draw equals a plain call (the stub, the gates closed).
        auto draws = Build(13, shape, image);
        std::array<uint32_t, 3> plain = {}, created = {};
        Write32(image + ei::RANDOM_STATE_RVA, 0x2468ACE);
        RealDraw(*draws, &plain);
        const uint32_t state_plain = RealState(*draws);
        Write32(image + ei::RANDOM_STATE_RVA, 0x2468ACE);
        RealDraw(*draws, &created, draws->emitter_plain);
        CHECK(plain == created && RealState(*draws) == state_plain && state_plain != 0x2468ACE);
      }
    }
    std::printf("26g. the game's own routines on the real layouts (A and B, CPU and GPU): spawn query / write (full buffers refused, the simulation's room on the CPU's), the second node, "
                "slot take / return, the create path's call of the draw equal to a plain call\n");

    // The rounds: the serial walk in image A, the round in image B through the real thunks and the production handler. L1 and L2, CPU and GPU, 2 to 7 workers.
    struct Recipe {
      const char* ops;
      uint32_t room;
      uint32_t max_ops;
      uint32_t max_units;
      bool sweep_draws;
      uint32_t other_room;
    };
    const std::array<Recipe, 5> recipes = {{
        {"RMRS", 3000, 4, 25, true, 0},     // plain draws, the other callers' spawns, the slot pool, the sweep's draws
        {"LELSR", 3000, 4, 25, true, 0},    // the create path (plain and with events), the deferred draws
        {"LEOOMS", 3000, 5, 25, false, 90}, // the second node: its own pipeline, never answered, in the serial order
        {"LLLL", 600, 6, 40, false, 0},     // a buffer that fills (L1 only: in L2 the units that ran ahead drew for creates serial would not make; the buffer is checked below)
        {"EL", 3000, 5, 25, true, 0},       // dying instances draw in the sweep between spawning units
    }};
    uint64_t real_cases = 0;
    for (const bool spawns : {false, true}) {
      for (const bool gpu : {false, true}) {
        for (const uint32_t workers : {2u, 4u, 7u}) {
          for (uint32_t seed = 1; seed <= 2 * scale; ++seed) {
            for (size_t recipe_index = 0; recipe_index < recipes.size(); ++recipe_index) {
              const Recipe& recipe = recipes[recipe_index];
              if (spawns && recipe.room == 600) continue;
              ResetScheduler();
              ei::spawns_row = spawns;
              Shape shape;
              shape.real = true;
              shape.ops = recipe.ops;
              shape.max_ops = recipe.max_ops;
              shape.effects = 12;
              shape.max_units = recipe.max_units;
              shape.dead_percent = 45;
              shape.spawn_room = recipe.room;
              shape.sweep_draws = recipe.sweep_draws;
              shape.other_room = recipe.other_room;
              shape.gpu = gpu;
              shape.busy = (seed & 1) != 0 ? 60 : 400;
              if (run_case(shape, 30000 + seed * 43 + workers * 7 + recipe_index * 1009 + (gpu ? 700 : 0) + (spawns ? 80000 : 0), 2, workers) != 0) return 1;
              ++real_cases;
            }
          }
        }
      }
    }
    // The cases did what they claim, in a round that staged: the draws deferred and made at the commits, the records merged by the game's own spawn, the second node full, the sweep
    // drawing.
    for (const bool gpu : {false, true}) {
      ResetScheduler();
      ei::spawns_row = true;
      CHECK(trace_open(8, 5));
      Shape shape;
      shape.real = true;
      shape.ops = "LELOOMS";
      shape.max_ops = 5;
      shape.effects = 12;
      shape.max_units = 25;
      shape.dead_percent = 45;
      shape.spawn_room = 3000;
      shape.other_room = 30;
      shape.sweep_draws = true;
      shape.gpu = gpu;
      auto serial = Build(31337, shape, game);
      auto parallel = Build(31337, shape, patched);
      Walk(*serial, false);
      Walk(*parallel, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*parallel) && Equal(*serial, *parallel, &why));
      size_t sweep_draws = 0;
      for (const LogEntry& entry : serial->log) sweep_draws += (entry.op == 'd' ? 1 : 0);
      CHECK(sweep_draws > 0 && parallel->other_count == (parallel->other_gpu ? shape.other_room : shape.other_room - shape.other_room / 8) && serial->spawn_buffer.size() > 100 && parallel->spawn_buffer == serial->spawn_buffer);
      CHECK(ei::counters[static_cast<size_t>(ei::Counter::DEFERRED_DRAWS)].load() > 0 && ei::last_round_spawned == serial->spawn_buffer.size() && ei::event_count.load() == 0);
      const uint32_t deferred = ei::counters[static_cast<size_t>(ei::Counter::DEFERRED_DRAWS)].load();
      CHECK(trace_close(5));
      std::printf("26g. a round that staged (%s pipeline): %zu spawns merged by the game's own spawn, %u draws deferred, %zu sweep draws, the second node full (%u): equal to the serial walk\n",
                  gpu ? "GPU" : "CPU", serial->spawn_buffer.size(), deferred, sweep_draws, parallel->other_count);
    }
    // A buffer that fills in L2 (OverflowDetected on the game's own spawn): the merged buffer is serial's whatever the units that ran ahead drew; counted.
    for (const bool gpu : {false, true}) {
      ResetScheduler();
      ei::spawns_row = true;
      CHECK(pool(5) == 4);
      Shape shape;
      shape.real = true;
      shape.ops = "LLLL";
      shape.max_ops = 6;
      shape.effects = 12;
      shape.max_units = 40;
      shape.spawn_room = 600;
      shape.gpu = gpu;
      auto serial = Build(4242, shape, game);
      auto parallel = Build(4242, shape, patched);
      Walk(*serial, false);
      Walk(*parallel, true);
      CHECK(ei::last_round_units != SENTINEL_UNITS && finish_round_checks(*parallel) && parallel->spawn_buffer == serial->spawn_buffer && !serial->spawn_buffer.empty());
      CHECK(Word(parallel->buffer + (gpu ? 0x14 : 0x28)) == Word(serial->buffer + (gpu ? 0x14 : 0x28)) && ei::counters[static_cast<size_t>(ei::Counter::SPAWN_OVERFLOWS)].load() > 0);
      std::printf("26g. a buffer that fills in L2 (%s pipeline): %zu spawns, the merged buffer equal to the serial walk's, the overflow counted\n", gpu ? "GPU" : "CPU", serial->spawn_buffer.size());
    }
    std::printf("26g. the real routines: %llu cases (5 scripts, L1 and L2, CPU and GPU, 2 / 4 / 7 workers, 2 rounds each): the draws, the spawns, the pool, the image's random state and "
                "every shared word equal to the serial walk in the game's own image\n", static_cast<unsigned long long>(real_cases));

    // CONTROLs: each rule left out must show on the real routines too.
    const auto must_differ = [&](const char* name, const Shape& shape, uint32_t workers, int rounds, auto&& arm) -> int {
      ResetScheduler();
      CHECK(pool(workers + 1) == workers);
      arm();
      bool detected = false;
      int ran = 0;
      for (int round = 0; round < rounds && !detected; ++round, ++ran) {
        auto serial = Build(900 + round, shape, game);
        auto parallel = Build(900 + round, shape, patched);
        Walk(*serial, false);
        Walk(*parallel, true);
        std::string reason;
        detected = !Equal(*serial, *parallel, &reason);
      }
      ResetScheduler();
      std::printf("26g. CONTROL %s: %s after %d round%s\n", name, detected ? "seen" : "NOT SEEN", ran, ran == 1 ? "" : "s");
      CHECK(detected);
      return 0;
    };
    {
      Shape shape;
      shape.real = true;
      shape.ops = "RMS";
      shape.effects = 6;
      shape.min_units = 40;
      shape.max_units = 60;
      shape.max_ops = 4;
      shape.busy = 150;
      shape.spawn_room = 3000;
      if (must_differ("NoOrderPoints (the game's draws, spawns and slots run on the workers)", shape, 4, 30, [] { ei::controls.no_order_points = true; }) != 0) return 1;
    }
    {
      Shape shape;
      shape.real = true;
      shape.ops = "L";
      shape.effects = 12;
      shape.max_units = 25;
      shape.max_ops = 4;
      shape.spawn_room = 3000;
      if (must_differ("CommitReversed (the real draws made at the commits in the reverse of the serial order)", shape, 4, 6, [] { ei::spawns_row = true; ei::controls.commit_reversed = true; }) != 0) return 1;
      if (must_differ("MergeByCompletion (the records merged by the game's own spawn in the order the units finished)", shape, 4, 6, [] { ei::spawns_row = true; ei::controls.merge_by_completion = true; }) != 0) return 1;
    }
    rows(false);
    CHECK(ei::written == 0);
    std::memcpy(game + stub_at, saved_game_code.data(), stub.size());
    std::memcpy(patched + stub_at, saved_patched_code.data(), stub.size());
    for (uint8_t* image : {game, patched}) FlushInstructionCache(GetCurrentProcess(), image + stub_at, stub.size());
    ResetScheduler();
  }
  std::printf("26. not built: ClaimFour (the code has G = 1 only); test 13 is 25c's\n");

  wp::Update({});
  ResetScheduler();
  ei::data = (saved_data != nullptr ? saved_data : real_data);
  ei::base = (saved_base != nullptr ? saved_base : real_base);
  ei::entries = saved_entries;
  if (ei::data != nullptr) eu::SetHandler(&eu::PassThrough);
  VirtualFree(fake, 0, MEM_RELEASE);
  delete data;
  std::printf("26. effect update scheduler: %llu cases, %llu units in rounds, no overlap, no Frame.eax written where the handler did not answer\n", static_cast<unsigned long long>(cases),
              static_cast<unsigned long long>(units_in_rounds));
  return 0;
}
