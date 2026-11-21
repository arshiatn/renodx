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
//    the serial reasons, the census (a render thread hit outside a unit, a foreign thread), the watched words, Update's gates; the handler writes Frame.eax only where it
//    answers; the scheduler's stack high-water;
// 26e. CONTROLs: no order points, no enumeration wait, a late child left out (LateChildOff), forwarding off (RigidOnWorker), sweeps on workers, no epoch, a sweep before its
//    units (SweepEarly), a unit that waits for the frontier to pass it (WaitOnLater: the hang detector), a sweep thunk with a wrong frame (SweepFrameWrong): each must be seen.
//    The serial reasons and the foreign-thread count are taken only in a trace window: those cases open one.
// Not here: test 13 (RegisterStateKept) is 25c's; ClaimFour (G = 4) is not built, the code has G = 1 only. The real routines on fake object graphs (the design's tests 2, 4, 5, 6, 7,
// 9 and 15 on the game's own functions) are not run: the scripts stand in for them; `tests.exe <dll> quick effect_update` runs this part alone.
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

struct Slot {
  uint32_t a = 0, b = 0, c = 0, effect = 0;
};
static_assert(sizeof(Slot) == eu::internal::SLOT_SIZE && offsetof(Slot, effect) == eu::internal::SLOT_EFFECT_OFFSET);

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
};

struct World {
  uint8_t* arena = nullptr;
  size_t arena_size = 0;
  size_t used = 0;
  uint8_t* node = nullptr;
  Slot* slots = nullptr;
  uint32_t table_slots = 0;
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
bool Touch(eu::Site site, uint32_t ecx = 0, uint32_t first = 0, uint32_t second = 0, uint32_t* eax = nullptr) {
  eu::Data* const data = ei::data;
  const bool walk = (site == eu::Site::WALK_CALL);
  if ((walk ? data->walk_armed : data->walk_active).load() == 0) return false;
  volatile uint32_t marker = 0;
  const bool asks_always = (eu::SITES[static_cast<size_t>(site)].kind != eu::Kind::ORDER || eu::AsksAlways(site));  // the walk's call, the appends, the forwarded and enumeration sites
  const uint32_t direct_high = data->direct_high.load();
  if (!asks_always && direct_high != 0 && direct_high - Address(const_cast<uint32_t*>(&marker)) < data->limit.load()) return false;
  struct {
    uint32_t regs[9];
    uint32_t stack[8];
  } frame = {};
  static_assert(offsetof(eu::Frame, stack) == sizeof(frame.regs));
  frame.regs[6] = ecx;
  frame.regs[7] = SENTINEL_EAX;
  frame.stack[0] = 0x0BADBAD0;
  frame.stack[1] = first;
  frame.stack[2] = second;
  const auto handler = reinterpret_cast<eu::Handler>(static_cast<uintptr_t>(data->handler.load()));
  const uint32_t answer = handler(reinterpret_cast<eu::Frame*>(&frame), static_cast<uint32_t>(site));
  const bool may_answer = eu::SITES[static_cast<size_t>(site)].return_bytes >= 0;
  if ((answer == 1 && !may_answer) || (!(answer == 1) && frame.regs[7] != SENTINEL_EAX)) eax_violations.fetch_add(1);
  if (answer == 1 && may_answer && eax != nullptr) *eax = frame.regs[7];
  return answer == 1 && may_answer;
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

void DoOp(World& w, Instance& me, char op, Random* rng, uint32_t* held_slot) {
  const auto log = [&](char what, uint32_t value) { w.log.push_back({me.id, static_cast<uint32_t>(what), value}); };
  uint32_t result = 0;
  switch (op) {
    case 'R':
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
      Touch(eu::Site::SLOT_TAKE);
      {
        const Shared shared(w);
        if (*held_slot == NO_SLOT && !w.free_slots.empty()) {
          *held_slot = w.free_slots.back();
          w.free_slots.pop_back();
          me.private_value += *held_slot;
        }
        log('S', *held_slot);
      }
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
          if (w.slots[slot].effect == 0) free[free_count++] = slot;
        }
        if (w.types_left != 0 && free_count != 0 && w.effects.size() < w.effects.capacity() && w.used + 2 * (sizeof(Effect) + 16 + 8 * sizeof(Entry)) < w.arena_size) {
          const uint32_t slot = free[rng->Next() % free_count];
          --w.types_left;
          auto* effect = new (w.Take(sizeof(Effect))) Effect;
          auto* entries = static_cast<Entry*>(w.Take(sizeof(Entry) * 8));
          effect_index = static_cast<uint32_t>(w.effects.size());
          effect->index = effect_index;
          effect->capacity = 8;
          effect->entries = Address(entries);
          effect->vector_head = Address(effect) + eu::internal::APPEND_VECTOR_OFFSET;
          w.effects.push_back(effect);
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
    Touch(eu::Site::SLOT_RETURN);
    const Shared shared(w);
    w.free_slots.push_back(held_slot);
    w.log.push_back({me.id, 's', held_slot});
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

// The game's loop over a node's table: each effect through the walk's call site (the thunk's gate, the handler), the walk's end.
void GameWalk(World& w, int hook) {
  for (uint32_t slot = 0; slot < Read32(w.node + eu::internal::TABLE_OFFSET + 4); ++slot) {
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

std::unique_ptr<World> Build(uint64_t seed, const Shape& shape) {
  auto w = std::make_unique<World>();
  Random rng = {.state = seed | 1};
  std::vector<uint32_t> counts(shape.effects);
  uint32_t total = 0;
  for (uint32_t& count : counts) {
    count = shape.min_units + rng.Next() % (shape.max_units - shape.min_units + 1);
    total += count;
  }
  w->table_slots = shape.effects * shape.gap + 64;
  w->arena_size = ((0x400 + w->table_slots * 16 + shape.effects * (sizeof(Effect) + 64) + (total + shape.effects * 192) * sizeof(Entry) + 96 * (sizeof(Effect) + 64 + 8 * sizeof(Entry))) + 0xFFF) & ~static_cast<size_t>(0xFFF);
  w->arena = static_cast<uint8_t*>(VirtualAlloc(nullptr, w->arena_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  w->node = static_cast<uint8_t*>(w->Take(0x400));
  w->slots = static_cast<Slot*>(w->Take(w->table_slots * sizeof(Slot)));
  Write32(w->node + eu::internal::TABLE_OFFSET, Address(w->slots));
  Write32(w->node + eu::internal::TABLE_OFFSET + 4, w->table_slots);
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
    w->slots[1 + index * shape.gap].effect = Address(effect);
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
    const auto index_of = [](const Slot& s) { return s.effect == 0 ? NO_SLOT : reinterpret_cast<const Effect*>(static_cast<uintptr_t>(s.effect))->index; };
    if (index_of(a.slots[slot]) != index_of(b.slots[slot])) return fail("the table");
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
  ei::small_round_presents = 0;
  ei::event_count = 0;
  for (auto& said : ei::events_said) said = 0;
  for (auto& counter : ei::counters) counter = 0;
  for (auto& counter : ei::serial_rounds) counter = 0;
  ei::controls = {};
}

// One walk of a world: serial (the gates closed) or a round (armed, the preconditions met).
void Walk(World& w, bool round, int hook = 0) {
  world = &w;
  ei::entries.random_state = &w.lcg;
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

int RunEffectUpdateTests(const TestBuild& build, uint8_t* patched, bool quick) {
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
                 .random_state = nullptr};
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
    return data->walk_active.load() == 0 && data->direct_high.load() == 0 && data->active.load() == 0 && !ei::current_round.open && w.inside.load() == 0;
  };
  // One case: the same world twice, the game's loop and rounds, `rounds` walks each; every shared result equal, no overlap, no event, no hang.
  uint64_t cases = 0;
  uint64_t units_in_rounds = 0;
  const auto run_case = [&](const Shape& shape, uint64_t seed, int rounds, uint32_t workers) -> int {
    CHECK(pool(workers + 1) == workers);
    auto serial = Build(seed, shape);
    auto parallel = Build(seed, shape);
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
      ei::units = {};
      ei::segments = {};
    };
    const auto failure_sweep = [&](const Shape& shape, uint64_t seed, const char* name) -> int {
      CHECK(pool(4) == 3);
      CHECK(trace_open(60, 4));
      clear_blocks();
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
  }

  // The serial reasons: too few units last round, a new node, no workers, the session off. Each walk equals the game's loop and leaves the gate closed.
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
      std::printf("26d. serial reason \"%s\": the walk equals the game's loop\n", name);
      return 0;
    };
    if (reason_case("too few units last round", ei::SerialReason::SMALL, 3, [](World&) { ei::last_round_units = 10; }) != 0) return 1;
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
      ei::last_round_units = 10;
      ei::last_node = Address(parallel->node);
      data->walk_armed.store(1);
      GameWalk(*parallel, false);
      data->walk_armed.store(0);
      CHECK(Equal(*serial, *parallel, &why) && ei::serial_rounds[static_cast<size_t>(ei::SerialReason::SMALL)].load() == 0 && finish_round_checks(*parallel));
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
  }
  std::printf("26. not built: ClaimFour (the code has G = 1 only); the pool count and min/max words (the spawn objects' fields, L2); test 13 is 25c's\n");

  wp::Update({});
  ResetScheduler();
  ei::data = saved_data;
  ei::base = saved_base;
  ei::entries = saved_entries;
  if (saved_data != nullptr) eu::SetHandler(&eu::PassThrough);
  VirtualFree(fake, 0, MEM_RELEASE);
  delete data;
  std::printf("26. effect update scheduler: %llu cases, %llu units in rounds, no overlap, no Frame.eax written where the handler did not answer\n", static_cast<unsigned long long>(cases),
              static_cast<unsigned long long>(units_in_rounds));
  return 0;
}
