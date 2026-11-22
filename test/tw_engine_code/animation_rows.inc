// ====================================================================================================================
// 23. Animation Rows on Workers: the animation update's call of the animation texture fill (Attila +0x1893d2a in the update +0x1893cc0; engine_code.hpp 19.),
// included inside tests.cpp's anonymous namespace after lod_pick.inc (JumpTo, Random, Address, Read32 and CHECK of the earlier parts are used). The call goes through a
// cut stub to animation_rows::Dispatch, which for now runs the game's own fill (pass-through), so a row that is on changes nothing. The game's fill itself is replaced in both
// copies of the DLL by a fake (FakeFill: logs its arguments, takes a few items of the row, returns a result with the upper bits set), after Prepare has found it:
// 23a. the site and stub as Prepare made them: the game's bytes, the jump into the stub, the stub's three words on the second page (the count, the two slots) and its last
//    jump to the fill at this copy's base + the build's RVA (the copies map at other bases than the DLL's own), the pages' protections (the stubs' page read-execute, the
//    data page read-write, one allocation), no sentinel left, the sweep entry's stub the same way (the displaced instructions, the jump back at the sweep's `cmp`);
// 23b. the stub alone (the seven arguments again, the callee pops them, eax the result) against the fill's own call, with the slots on and off: the result and the log equal;
//    the whole update (+0x1893cc0) in both copies on random worlds (1 to 6 textures, the rotating tweak on and off, starts and ends of the row, a fill that returns 0): the
//    result, the world and the log of the fill's calls equal;
// 23c. the cut rule: a parked thread at each byte of each stub, the rows switched off each time: nothing comes out, the slots are 0 (the stubs run the game's call), and
//    everything comes out once the thread is gone; a thread in a function the stub called (the count above 0): the sites stay in, then come out; threads just outside the
//    stubs do not hold anything;
// 23d. another mod's bytes over a site on its way out: that site is left alone and the module is pinned; a thread kept in the stub for MAX_FAILED_WRITES presents: pinned; an
//    Uninstall with the count stuck above 0 (an unwind through a stub): the sites stay in, the slots are 0, pinned;
// 23e. 200 switches under 4 threads that run whole updates in both copies on their own worlds.
// ====================================================================================================================

struct FillCall {
  uint32_t cache = 0;
  uint32_t device = 0;
  uint32_t force = 0;
  uint32_t list = 0;
  uint32_t texture = 0;
  uint32_t row_pointer = 0;
  uint32_t row = 0;
  uint32_t end = 0;
  uint32_t out_before = 0;
  bool operator==(const FillCall&) const = default;
};

struct FillScript {
  uint32_t seed = 0;
  uint32_t calls = 0;
  uint32_t fail_at = ~0u;  // the call that returns 0
};

thread_local FillScript fill_script;
thread_local std::vector<FillCall> fill_log;

// The fill's own signature (thiscall, ecx = the cache, seven arguments, the callee pops them) as a fastcall with an unused edx.
using FillFunction = uint32_t(__fastcall*)(void* cache, void*, void* device, uint32_t force, void* list, uint32_t texture, uint32_t* row, uint32_t end, uint32_t* out);

uint32_t __fastcall FakeFill(void* cache, void*, void* device, uint32_t force, void* list, uint32_t texture, uint32_t* row, uint32_t end, uint32_t* out) {
  fill_log.push_back({Address(cache), Address(device), force, Address(list), texture, Address(row), *row, end, *out});
  const uint32_t take = std::min(end - *row, (fill_script.seed + fill_script.calls * 5) % 4 + (fill_script.calls % 3 == 0 ? 1u : 0u));
  *row += take;
  *out += take;
  Write32(static_cast<uint8_t*>(cache) + 0x20, take);
  // The upper bits of eax are the fill's too: the stub and Dispatch keep them (the update reads only al).
  return 0x12345600u | (fill_script.calls++ != fill_script.fail_at ? 1u : 0u);
}

// The animation update's frame: thiscall (ecx = the cache), five stack arguments, callee pops: (device, force, list, row, end).
using UpdateFunction = uint32_t(__fastcall*)(void* cache, void*, void* device, uint32_t force, void* list, uint32_t* row, uint32_t end);
constexpr uint32_t ANIMATION_UPDATE_RVA = 0x1893cc0;
constexpr uint32_t ANIMATION_UPDATE_TWEAK_AT = 0x1C;  // the `mov ecx, imm32` of the rotating tweak's object, in the update

struct AnimationWorld {
  alignas(16) uint8_t cache[0x40];  // [+4] the texture count, [+0x28] the frame number, [+0x2c] the current texture
  uint32_t row;
  uint8_t device[16];
  uint8_t list[16];
  uint32_t padding[4];
  // The fields' bytes, not the struct's tail padding.
  bool operator==(const AnimationWorld& other) const { return std::memcmp(this, &other, offsetof(AnimationWorld, padding) + sizeof(padding)) == 0; }
};

struct AnimationRun {
  uint32_t result = 0;
  AnimationWorld world = {};
  std::vector<FillCall> log;
};

void AnimationRotate(uint8_t* image, bool on) {
  *reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(Read32(image + ANIMATION_UPDATE_RVA + ANIMATION_UPDATE_TWEAK_AT)) + 0x48) = (on ? 1 : 0);
}

struct AnimationSpec {
  uint32_t textures = 1;
  uint32_t current = 0;
  uint32_t start = 0;
  uint32_t end = 0;
  uint32_t force = 0;
  uint32_t seed = 0;
  uint32_t fail_at = ~0u;
  bool rotate = false;
};

AnimationSpec AnimationRandom(Random* random) {
  AnimationSpec spec;
  spec.textures = 1 + random->Next() % 6;
  spec.current = random->Next() % spec.textures;
  spec.end = random->Next() % 40;
  spec.start = (spec.end == 0 ? 0 : random->Next() % (spec.end + 1));
  spec.force = random->Next();
  spec.seed = random->Next();
  spec.fail_at = (random->Next() % 4 == 0 ? random->Next() % 5 : ~0u);
  spec.rotate = (random->Next() & 1) != 0;
  return spec;
}

// The update of `image` on a copy of `world`'s state set from `spec`; `world` is left as it was found.
AnimationRun AnimationUpdate(uint8_t* image, AnimationWorld* world, const AnimationSpec& spec) {
  AnimationWorld saved = *world;
  Write32(world->cache + 4, spec.textures);
  Write32(world->cache + 0x28, 7);
  Write32(world->cache + 0x2c, spec.current);
  world->row = spec.start;
  fill_log.clear();
  fill_script = {.seed = spec.seed, .calls = 0, .fail_at = spec.fail_at};
  AnimationRotate(image, spec.rotate);
  AnimationRun run;
  run.result = reinterpret_cast<UpdateFunction>(image + ANIMATION_UPDATE_RVA)(world->cache, nullptr, world->device, spec.force, world->list, &world->row, spec.end) & 0xFF;
  run.world = *world;
  run.log = fill_log;
  *world = saved;
  return run;
}

// Both copies on the same world: the result's al, every byte of the world and the fill's calls equal. Empty: equal.
const char* AnimationBoth(uint8_t* game, uint8_t* patched, AnimationWorld* world, const AnimationSpec& spec) {
  const AnimationRun a = AnimationUpdate(game, world, spec);
  const AnimationRun b = AnimationUpdate(patched, world, spec);
  if (a.result != b.result) return "the result";
  if (!(a.world == b.world)) return "the world";
  if (a.log != b.log) return "the fill's calls";
  return "";
}

// A thread in a function the fill's stub called: it stays there until `release`.
std::atomic<bool> slow_entered = false;
std::atomic<bool> slow_release = false;
uint32_t __fastcall SlowDispatch(void* cache, void*, void* device, uint32_t force, void* list, uint32_t texture, uint32_t* row, uint32_t end, uint32_t* out) {
  slow_entered = true;
  while (!slow_release.load()) Sleep(1);
  return twattila::animation_rows::Dispatch(cache, nullptr, device, force, list, texture, row, end, out);
}

// ==== S4/S5 exactness part begin (b3t_) ====
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// The exactness part (S4, S5): whole updates and single fill calls in the game's copy (A) and ours (B, through the stub and animation_rows::Dispatch) from equal
// state, every byte of the world compared. The world is one block of memory (header, list entries, instances, animations, the records' buffer and the fake
// allocator's arena); the game's own fill and record push run in both copies, with the game's allocator, free, texture getter, the texture's Map / Unmap and the
// serial texture fill (the tail's) pointed at fakes that log their arguments.
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t AR_ALLOC_RVA = 0xC6AE0;       // the game's allocator: cdecl (count of 16-byte records, 0), the record push's growth
constexpr uint32_t AR_FREE_RVA = 0xE98C0;        // cdecl (block)
constexpr uint32_t AR_TEXTURE_OF_RVA = 0x173CE80;  // thiscall (the texture) -> the device texture the tail maps
constexpr uint32_t AR_SERIAL_FILL_RVA = 0x17DB0F0;  // thiscall (the map's description), one argument (the row range), callee pops
constexpr uint32_t AR_TBB_TWEAK_RVA = 0x2A628E8;
constexpr uint32_t AR_FILL_TWEAK_RVA = 0x2A61058;
constexpr uint32_t AR_BLOCK_ITEMS = 512;
constexpr uint32_t AR_INSTANCE_BYTES = 0xB0;      // +0x5c the texture, +0x60 the animation, +0x70 the record word, +0xa4 the texel's float bits
constexpr uint32_t AR_ANIMATION_BYTES = 0x80;     // +4 n, +0xc the flag, +0x10 frame, +0x14 texture, +0x18 texel
constexpr uint32_t AR_MAX_TEXTURES = 6;
constexpr uint32_t AR_HEADER_BYTES = 0x1000;
// The header's objects.
constexpr uint32_t AR_CACHE = 0;            // +4 textures, +8 the texture array, +0x10 row size, +0x14 texels per row, +0x1c / +0x20 / +0x24 the records, +0x28 frame, +0x2c current
constexpr uint32_t AR_LIST = 0x40;          // +4 count, +8 the entries
constexpr uint32_t AR_ROW = 0x60;
constexpr uint32_t AR_OUT = 0x64;
constexpr uint32_t AR_DEVICE = 0x70;
constexpr uint32_t AR_TEXTURE_ARRAY = 0x80;
constexpr uint32_t AR_TEXTURES = 0xA0;      // 16 bytes each: the vtable, the height
constexpr uint32_t AR_VTABLE = 0x100;       // 32 slots; slot 26 (+0x68) is the height
constexpr uint32_t AR_DEVICE_TEXTURE = 0x180;
constexpr uint32_t AR_DEVICE_VTABLE = 0x1A0;  // slot 9 (+0x24) Map, slot 10 (+0x28) Unmap
constexpr uint32_t AR_MAPPED = 0x200;

struct ArWorld {
  uint8_t* base = nullptr;
  size_t bytes = 0;
  size_t items_at = 0;
  size_t instances_at = 0;
  size_t animations_at = 0;
  size_t records_at = 0;
  size_t arena_at = 0;
  uint32_t arena_bytes = 0;
  uint32_t arena_used = 0;
  uint32_t entries = 0;
  uint32_t distinct = 0;
  uint32_t map_calls = 0;
  uint32_t map_fails_at = ~0u;  // the Map call that answers 0
  uint32_t height_calls = 0;
  uint32_t height_shift_from = ~0u;  // the height() call from which the answer is shifted by height_delta (a game run that differs from ours)
  uint32_t height_delta = 0;
  std::vector<uint32_t> log;
  ArWorld() = default;
  ArWorld(const ArWorld&) = delete;
  ArWorld& operator=(const ArWorld&) = delete;
  ~ArWorld() {
    if (base != nullptr) VirtualFree(base, 0, MEM_RELEASE);
  }
};

thread_local ArWorld* ar_current = nullptr;

uint32_t* __cdecl ArAlloc(int32_t count, uint32_t) {
  ArWorld* world = ar_current;
  const uint32_t bytes = (count < 0 ? 0u : static_cast<uint32_t>(count) << 4);
  world->log.push_back(0xA0000000u | (static_cast<uint32_t>(count) & 0x0FFFFFFF));
  if (bytes > world->arena_bytes - world->arena_used) {
    world->log.push_back(0xDEADA110u);
    return nullptr;
  }
  world->log.push_back(world->arena_used);
  uint8_t* block = world->base + world->arena_at + world->arena_used;
  world->arena_used += bytes;
  return reinterpret_cast<uint32_t*>(block);
}

void __cdecl ArFree(void* block) {
  ArWorld* world = ar_current;
  world->log.push_back(0xF0000000u);
  world->log.push_back(Address(block) - Address(world->base));
}

uint32_t __fastcall ArHeight(void* texture, void*) {
  ArWorld* world = ar_current;
  const uint32_t height = Read32(static_cast<const uint8_t*>(texture) + 4);
  return (world->height_calls++ >= world->height_shift_from ? height + world->height_delta : height);
}

void* __fastcall ArTextureOf(void* texture, void*) {
  ArWorld* world = ar_current;
  world->log.push_back(0xC0000000u | ((Address(texture) - Address(world->base) - AR_TEXTURES) >> 4));
  return world->base + AR_DEVICE_TEXTURE;
}

uint32_t __fastcall ArMap(void*, void*, void* device, uint32_t first, uint32_t flags, uint32_t* pointer, uint32_t* pitch, uint32_t zero_a, uint32_t zero_b) {
  ArWorld* world = ar_current;
  world->log.push_back(0xD0000000u | world->map_calls);
  world->log.push_back(Address(device) - Address(world->base));
  world->log.push_back(first ^ flags ^ zero_a ^ zero_b);
  if (world->map_calls++ == world->map_fails_at) return 0;
  *pointer = Address(world->base + AR_MAPPED);
  *pitch = 0x100;
  return 1;
}

uint32_t __fastcall ArUnmap(void*, void*, void* device, uint32_t zero) {
  ArWorld* world = ar_current;
  world->log.push_back(0xE0000000u);
  world->log.push_back(Address(device) - Address(world->base) + zero);
  return 1;
}

// The serial texture fill: what the tail hands it (the map's description in ecx: the force byte, the cache, the mapped pointer, the pitch; the row range at the argument)
// and a hash of the records [0, count) of the cache's vector.
void __fastcall ArSerialFill(void* description, void*, const uint8_t* range) {
  ArWorld* world = ar_current;
  const auto* d = static_cast<const uint8_t*>(description);
  const uint32_t count = Read32(range + 4);
  const uint8_t* cache = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(Read32(d + 4)));
  const uint8_t* data = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(Read32(cache + 0x24)));
  uint32_t hash = 2166136261u;
  for (uint32_t i = 0; i < count * 4; ++i) hash = (hash ^ Read32(data + 4 * i)) * 16777619u;
  world->log.push_back(0xB0000000u | count);
  world->log.push_back(hash);
  world->log.push_back(Read32(range) + (d[0] << 8) + Read32(d + 8) - Address(world->base) + Read32(d + 0xC));
}

// What the tests need to run the game's fill: the five functions pointed at the fakes in both copies, put back at the end.
struct ArHooks {
  struct Saved {
    uint8_t* at;
    std::array<uint8_t, 5> bytes;
  };
  std::vector<Saved> saved;
  void Hook(uint8_t* at, const void* to) {
    saved.push_back({.at = at});
    std::memcpy(saved.back().bytes.data(), at, 5);
    JumpTo(at, to);
  }
  void Install(uint8_t* image) {
    Hook(image + AR_ALLOC_RVA, reinterpret_cast<const void*>(&ArAlloc));
    Hook(image + AR_FREE_RVA, reinterpret_cast<const void*>(&ArFree));
    Hook(image + AR_TEXTURE_OF_RVA, reinterpret_cast<const void*>(&ArTextureOf));
    Hook(image + AR_SERIAL_FILL_RVA, reinterpret_cast<const void*>(&ArSerialFill));
  }
  ~ArHooks() {
    for (const Saved& hook : saved) std::memcpy(hook.at, hook.bytes.data(), 5);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }
};

// What the world is made of; every field has a default so a case names only what it changes.
struct ArSpec {
  uint64_t seed = 1;
  uint32_t items = 0;               // the list's count
  uint32_t extra_entries = 0;       // entries past the count (garbage pointers, or live instances with live_extra)
  bool live_extra = false;
  uint32_t textures = 1;
  uint32_t current = 0;
  uint32_t frame = 100;
  uint32_t row_size = 1024;
  uint32_t texels_per_row = 1000;
  std::array<uint32_t, AR_MAX_TEXTURES> heights = {64, 64, 64, 64, 64, 64};
  uint32_t need_min = 4;            // the animations' n
  uint32_t need_max = 60;
  uint32_t oversize_percent = 0;    // animations with n above the row size
  uint32_t flag_percent = 50;
  uint32_t share_percent = 50;      // entries that take one of the shared animations
  uint32_t duplicate_percent = 0;   // list entries that repeat an earlier entry's instance
  uint32_t pre_state = 0;           // 0 mixed, 1 valid, 2 stale frame, 3 other texture, 4 minus one words
  bool pre_texel_edge = false;      // the placements' texel words are the values that matter to the float conversion (0, 2^24 + 1, 2^31, 0xFFFFFFFE, ...)
  uint32_t record_cap = 0;          // the records' buffer at the start
  bool alias_records = false;       // the records' buffer lies inside the animations (P-alias broken on purpose: a record write changes a word the gather read)
  uint32_t initial_count = 0;       // records already in the vector (at most record_cap; the game's fill keeps pushing after them)
  uint32_t null_instance_at = ~0u;  // list entries made null
  uint32_t null_animation_at = ~0u;
  uint32_t frame_offset = 0;        // a placement's frame that stands for "valid": the cache's frame plus this
};

// What one run does: a whole update (+0x1893cc0, the textures in turn) or one call of the fill.
struct ArCall {
  bool update = true;
  uint32_t start = 0;
  uint32_t end = 0;
  uint32_t force = 0;
  uint32_t texture = 0;
  bool tweak = false;               // the tweak at +0x2a61058: the rotation and the tail's Map path
  uint32_t map_fails_at = ~0u;
  uint32_t rounding = 0;            // MXCSR's rounding control bits (0 nearest, 1 down, 2 up, 3 toward zero)
  uint32_t height_shift_from = ~0u;  // from this height() call on the textures answer height_delta more (the second call is the game's own run in Check)
  uint32_t height_delta = 0;
  uint32_t game_height_shift_from = ~0u;  // the same for the game's copy (a Check mismatch's final state is the game's run on the shifted heights)
  bool records_free = false;        // the records and the allocator may differ (a game run that makes other records than ours, as in Check): everything else must still be equal
  bool check_sentinel = false;      // Check mode leaves its all-ones sentinel record after the last record (design 3.14): that one record may differ from the game's own run
};

struct ArFault {
  bool happened = false;
  uint32_t address = 0;  // where, relative to the image the run was in (all ones: outside it)
  uint32_t target = 0;   // the memory touched
};

// What the module's own counters say of one run (all zero when the build has none: TWATTILA_WORKER_CONTROLS not defined).
struct ArPaths {
  std::array<uint32_t, 12> taken = {};  // calls of Dispatch per path (animation_rows::Path), the delta of the run
  uint32_t items = 0;                   // the last call's finished items
  uint32_t records = 0;
  uint32_t rounds = 0;
  uint32_t workers = 0;
  bool ordered = false;
  bool streaming = false;
  uint32_t gathered_items = 0;   // what phase A read in the call (S-1 / S-2 bounds)
  uint32_t gathered_blocks = 0;
};

struct ArResult {
  uint32_t eax = 0;
  uint32_t row = 0;
  uint32_t out = 0;
  uint32_t flags = 0;  // MXCSR's status flags after the run
  uint32_t arena_used = 0;
  ArFault fault;
  std::vector<uint32_t> log;
  ArPaths paths;
};

#if defined(TWATTILA_WORKER_CONTROLS)
namespace ar_module = twattila::animation_rows::internal;
using ArPath = twattila::animation_rows::Path;
std::array<uint32_t, 12> ArReadPaths() {
  std::array<uint32_t, 12> counts = {};
  for (size_t path = 0; path < ar_module::paths.size() && path < counts.size(); ++path) counts[path] = ar_module::paths[path].load();
  return counts;
}
#endif

struct ArContext {
  ArWorld world;
  std::vector<uint8_t> initial;
  std::vector<uint8_t> after_game;
  ArSpec spec;
};

uint8_t* ArAt(ArWorld* world, size_t offset) { return world->base + offset; }

void ArFillRandom(uint8_t* at, size_t bytes, Random* random) {
  for (size_t i = 0; i + 4 <= bytes; i += 4) Write32(at + i, random->Next());
}

// A spec's world: random bytes everywhere the game does not look, the fields the loop reads set from the spec. False if the memory is not to be had.
bool ArBuild(ArContext* context, const ArSpec& spec) {
  ArWorld* world = &context->world;
  context->spec = spec;
  Random random = {.state = (spec.seed * 0x9E3779B97F4A7C15ull) | 1};
  for (int warm = 0; warm < 4; ++warm) random.Next();
  const uint32_t entries = spec.items + spec.extra_entries;
  const uint32_t distinct = std::max<uint32_t>(1, static_cast<uint32_t>(static_cast<uint64_t>(entries) * (100 - spec.share_percent) / 100));
  size_t at = AR_HEADER_BYTES;
  const auto take = [&](size_t bytes) {
    const size_t where = at;
    at += (bytes + 63) & ~static_cast<size_t>(63);
    return where;
  };
  world->items_at = take(static_cast<size_t>(entries) * 4 + 64);
  world->instances_at = take(static_cast<size_t>(entries) * AR_INSTANCE_BYTES);
  world->animations_at = take(static_cast<size_t>(distinct) * AR_ANIMATION_BYTES);
  world->records_at = take(static_cast<size_t>(spec.record_cap) * 16 + 64);
  world->arena_bytes = static_cast<uint32_t>(std::max<size_t>(1 << 16, static_cast<size_t>(entries) * 96 + static_cast<size_t>(spec.record_cap) * 48));
  world->arena_at = take(world->arena_bytes);
  if (world->base == nullptr || world->bytes < at) {
    if (world->base != nullptr) VirtualFree(world->base, 0, MEM_RELEASE);
    world->base = static_cast<uint8_t*>(VirtualAlloc(nullptr, at, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    world->bytes = (world->base != nullptr ? at : 0);
    if (world->base == nullptr) return false;
  }
  const size_t used = at;
  world->bytes = used;
  world->entries = entries;
  world->distinct = distinct;
  ArFillRandom(world->base, used, &random);
  const uint32_t base_address = Address(world->base);
  const auto word = [&](size_t offset) { return reinterpret_cast<uint32_t*>(world->base + offset); };
  // Texture objects: the vtable at +0, the height at +4.
  for (uint32_t i = 0; i < AR_MAX_TEXTURES; ++i) {
    *word(AR_TEXTURES + 16 * i) = base_address + AR_VTABLE;
    *word(AR_TEXTURES + 16 * i + 4) = spec.heights[i];
    *word(AR_TEXTURE_ARRAY + 4 * i) = base_address + AR_TEXTURES + 16 * i;
  }
  for (uint32_t slot = 0; slot < 32; ++slot) *word(AR_VTABLE + 4 * slot) = 0;
  *word(AR_VTABLE + 0x68) = Address(reinterpret_cast<const void*>(&ArHeight));
  *word(AR_DEVICE_TEXTURE) = base_address + AR_DEVICE_VTABLE;
  for (uint32_t slot = 0; slot < 16; ++slot) *word(AR_DEVICE_VTABLE + 4 * slot) = 0;
  *word(AR_DEVICE_VTABLE + 0x24) = Address(reinterpret_cast<const void*>(&ArMap));
  *word(AR_DEVICE_VTABLE + 0x28) = Address(reinterpret_cast<const void*>(&ArUnmap));
  // The cache and the list.
  *word(AR_CACHE + 4) = spec.textures;
  *word(AR_CACHE + 8) = base_address + AR_TEXTURE_ARRAY;
  *word(AR_CACHE + 0x10) = spec.row_size;
  *word(AR_CACHE + 0x14) = spec.texels_per_row;
  *word(AR_CACHE + 0x1c) = spec.record_cap;
  *word(AR_CACHE + 0x20) = std::min(spec.initial_count, spec.record_cap);
  *word(AR_CACHE + 0x24) = (spec.record_cap != 0 ? base_address + static_cast<uint32_t>(spec.alias_records ? world->animations_at : world->records_at) : 0);
  *word(AR_CACHE + 0x28) = spec.frame;
  *word(AR_CACHE + 0x2c) = spec.current;
  *word(AR_LIST + 4) = spec.items;
  *word(AR_LIST + 8) = base_address + static_cast<uint32_t>(world->items_at);
  *word(AR_ROW) = 0;
  *word(AR_OUT) = 0x77;

  // The animations: n, the flag, the placement words. The slot's offset inside its bytes varies so that +0x10 .. +0x1b of the placement is on one cache line or across two.
  std::vector<uint32_t> animation_address(distinct);
  const std::array<uint32_t, 5> line_offsets = {0, 8, 40, 44, 56};
  for (uint32_t k = 0; k < distinct; ++k) {
    uint8_t* animation = world->base + world->animations_at + static_cast<size_t>(k) * AR_ANIMATION_BYTES + line_offsets[random.Next() % 5];
    animation_address[k] = Address(animation);
    uint32_t n = spec.need_min + random.Next() % (spec.need_max - spec.need_min + 1);
    if (spec.oversize_percent != 0 && random.Next() % 100 < spec.oversize_percent) n = spec.row_size + random.Next() % (spec.row_size / 2 + 3);
    Write32(animation + 4, n);
    Write32(animation + 0xC, random.Next() % 100 < spec.flag_percent ? (random.Next() | 1) : 0);
    uint32_t mode = spec.pre_state;
    if (mode == 0) mode = 1 + random.Next() % 6;  // 1 valid, 2 stale, 3 other texture, 4 minus ones, 5 texel minus one, 6 random words
    const uint32_t valid_frame = spec.frame + spec.frame_offset;
    static constexpr std::array<uint32_t, 12> EDGE_TEXELS = {0, 1, 2, 0x00FFFFFFu, 0x01000000u, 0x01000001u, 0x01000003u, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFEu, 0xFFFFFF01u};
    const uint32_t pre_texel = (spec.pre_texel_edge ? (k % 13 == 12 ? (random.Next() | 0x01000000u) : EDGE_TEXELS[k % 13])
                                                    : (random.Next() & 0x7FFFFFFF));
    switch (mode) {
      case 1:
        Write32(animation + 0x10, valid_frame);
        Write32(animation + 0x14, spec.current);
        Write32(animation + 0x18, pre_texel);
        break;
      case 2:
        Write32(animation + 0x10, spec.frame - 1 - random.Next() % 5);
        Write32(animation + 0x14, spec.current);
        Write32(animation + 0x18, pre_texel);
        break;
      case 3:
        Write32(animation + 0x10, valid_frame);
        Write32(animation + 0x14, spec.current + 1 + random.Next() % 3);
        Write32(animation + 0x18, pre_texel);
        break;
      case 4:
        Write32(animation + 0x10, valid_frame);
        Write32(animation + 0x14, ~0u);
        Write32(animation + 0x18, ~0u);
        break;
      case 5:
        Write32(animation + 0x10, valid_frame);
        Write32(animation + 0x14, spec.current);
        Write32(animation + 0x18, ~0u);
        break;
      default:
        break;
    }
  }
  // The instances and the list entries (a duplicate entry repeats an earlier entry's pointer).
  for (uint32_t e = 0; e < entries; ++e) {
    uint8_t* instance = world->base + world->instances_at + static_cast<size_t>(e) * AR_INSTANCE_BYTES;
    Write32(instance + 0x60, animation_address[spec.share_percent == 0 ? e : random.Next() % distinct]);
    uint32_t pointer = Address(instance);
    if (e < spec.items && e > 0 && random.Next() % 100 < spec.duplicate_percent) pointer = Read32(world->base + world->items_at + 4 * (random.Next() % e));
    if (e >= spec.items && !spec.live_extra) pointer = 0x80000000u | random.Next();
    Write32(world->base + world->items_at + 4 * e, pointer);
  }
  if (spec.null_instance_at < entries) Write32(world->base + world->items_at + 4 * spec.null_instance_at, 0);
  if (spec.null_animation_at < entries) {
    if (const uint32_t pointer = Read32(world->base + world->items_at + 4 * spec.null_animation_at); pointer != 0) {
      Write32(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(pointer)) + 0x60, 0);
    }
  }
  context->initial.assign(world->base, world->base + world->bytes);
  return true;
}

// Where in the world a byte is: for the first difference's message.
std::string ArRegion(const ArWorld& world, size_t offset) {
  if (offset < AR_HEADER_BYTES) return std::format("header +0x{:X}", offset);
  if (offset < world.instances_at) return std::format("list entry {}", (offset - world.items_at) / 4);
  if (offset < world.animations_at) {
    return std::format("instance {} +0x{:X}", (offset - world.instances_at) / AR_INSTANCE_BYTES, (offset - world.instances_at) % AR_INSTANCE_BYTES);
  }
  if (offset < world.records_at) {
    return std::format("animation {} +0x{:X}", (offset - world.animations_at) / AR_ANIMATION_BYTES, (offset - world.animations_at) % AR_ANIMATION_BYTES);
  }
  if (offset < world.arena_at) return std::format("the initial records' buffer +0x{:X}", offset - world.records_at);
  return std::format("the arena +0x{:X} (record {} word {})", offset - world.arena_at, (offset - world.arena_at) / 16, ((offset - world.arena_at) / 4) % 4);
}

int ArFilter(EXCEPTION_POINTERS* information, ArFault* fault, const uint8_t* image) {
  if (information->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
  const uint32_t where = Address(information->ExceptionRecord->ExceptionAddress);
  fault->happened = true;
  fault->address = (where >= Address(image) && where - Address(image) < 0x4000000u ? where - Address(image) : ~0u);
  fault->target = static_cast<uint32_t>(information->ExceptionRecord->ExceptionInformation[1]);
  return EXCEPTION_EXECUTE_HANDLER;
}

// One run of the update or the fill. Nothing with a destructor in this frame (it holds a __try).
uint32_t ArInvoke(const ArCall& call, const void* function, ArWorld* world, ArFault* fault, const uint8_t* image) {
  uint8_t* const base = world->base;
  uint32_t* const row = reinterpret_cast<uint32_t*>(base + AR_ROW);
  uint32_t* const out = reinterpret_cast<uint32_t*>(base + AR_OUT);
  __try {
    if (call.update) {
      return reinterpret_cast<UpdateFunction>(const_cast<void*>(function))(base + AR_CACHE, nullptr, base + AR_DEVICE, call.force, base + AR_LIST, row, call.end);
    }
    return reinterpret_cast<FillFunction>(const_cast<void*>(function))(base + AR_CACHE, nullptr, base + AR_DEVICE, call.force, base + AR_LIST, call.texture, row, call.end, out);
  } __except (ArFilter(GetExceptionInformation(), fault, image)) {
    return 0xFA17FA17u;
  }
}

// The world as the context's initial copy has it, the call run on `image` through `function`.
void ArRunSide(ArContext* context, const uint8_t* image, const void* function, const ArCall& call, uint32_t height_shift_from, ArResult* result) {
  ArWorld* world = &context->world;
  std::memcpy(world->base, context->initial.data(), world->bytes);
  world->arena_used = 0;
  world->map_calls = 0;
  world->map_fails_at = call.map_fails_at;
  world->height_calls = 0;
  world->height_shift_from = height_shift_from;
  world->height_delta = call.height_delta;
  world->log.clear();
  Write32(world->base + AR_ROW, call.start);
  ar_current = world;
  *result = {};
  _mm_setcsr(0x1F80 | (call.rounding << 13));
  result->eax = ArInvoke(call, function, world, &result->fault, image);
  result->flags = _mm_getcsr() & 0x3F;
  _mm_setcsr(0x1F80);
  ar_current = nullptr;
  result->row = Read32(world->base + AR_ROW);
  result->out = Read32(world->base + AR_OUT);
  result->arena_used = world->arena_used;
  result->log = world->log;
}

// The two copies' tweaks: the rotation / the tail's Map path from the call, the parallel (TBB) fill always off.
void ArSetTweaks(uint8_t* game, uint8_t* patched, bool tweak) {
  for (uint8_t* image : {game, patched}) {
    *reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(Address(image) + AR_FILL_TWEAK_RVA) + 0x48) = (tweak ? 1 : 0);
    *reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(Address(image) + AR_TBB_TWEAK_RVA) + 0x48) = 0;
  }
}

// What the cases add up.
struct ArStats {
  uint64_t runs = 0;
  uint64_t items_stepped = 0;   // the game's own rise of the row, summed
  uint64_t stopped_early = 0;   // runs that ended below `end`
  uint64_t faults = 0;
  uint64_t equal_faults = 0;
  uint64_t tail_runs = 0;       // runs with a Map in the log
  uint64_t allocations = 0;
  std::array<uint64_t, 12> paths = {};  // Dispatch's calls per path (the module's counters; zero without TWATTILA_WORKER_CONTROLS)
  uint64_t workers_used = 0;           // calls of ours that woke workers
};
thread_local ArStats ar_stats;

// The game's copy against ours on one world and one call. Empty: equal in everything (the result, every byte of the world, the row, `out`, MXCSR's flags, the
// allocator's and the tail's log, the fault's place and memory).
std::string ArCompare(ArContext* context, uint8_t* game, const void* game_function, uint8_t* patched, const void* patched_function, const ArCall& call,
                      ArResult* game_result = nullptr, ArResult* patched_result = nullptr) {
  ArWorld* world = &context->world;
  ArSetTweaks(game, patched, call.tweak);
  ArResult a;
  ArResult b;
  ArRunSide(context, game, game_function, call, call.game_height_shift_from, &a);
  context->after_game.assign(world->base, world->base + world->bytes);
#if defined(TWATTILA_WORKER_CONTROLS)
  const std::array<uint32_t, 12> paths_before = ArReadPaths();
#endif
  ArRunSide(context, patched, patched_function, call, call.height_shift_from, &b);
#if defined(TWATTILA_WORKER_CONTROLS)
  const std::array<uint32_t, 12> paths_after = ArReadPaths();
  for (size_t path = 0; path < paths_after.size(); ++path) b.paths.taken[path] = paths_after[path] - paths_before[path];
  b.paths.items = ar_module::last_call.items;
  b.paths.records = ar_module::last_call.records;
  b.paths.rounds = ar_module::last_call.rounds;
  b.paths.workers = ar_module::last_call.workers;
  b.paths.ordered = ar_module::last_call.ordered;
  b.paths.streaming = ar_module::last_call.streaming;
  b.paths.gathered_items = ar_module::last_call.gathered_items;
  b.paths.gathered_blocks = ar_module::last_call.gathered_blocks;
#endif
  ++ar_stats.runs;
  for (size_t path = 0; path < b.paths.taken.size(); ++path) ar_stats.paths[path] += b.paths.taken[path];
  ar_stats.workers_used += (b.paths.taken[0] != 0 && b.paths.workers != 0 ? 1 : 0);
  ar_stats.items_stepped += (a.row >= call.start ? a.row - call.start : 0);
  ar_stats.stopped_early += (a.row < call.end ? 1 : 0);
  ar_stats.faults += (a.fault.happened ? 1 : 0);
  ar_stats.equal_faults += (a.fault.happened && b.fault.happened ? 1 : 0);
  ar_stats.allocations += a.arena_used != 0 ? 1 : 0;
  for (uint32_t event : a.log) ar_stats.tail_runs += ((event & 0xF0000000u) == 0xD0000000u ? 1 : 0);
  if (game_result != nullptr) *game_result = a;
  if (patched_result != nullptr) *patched_result = b;
  if (a.fault.happened != b.fault.happened) return std::format("the game {} and ours {}", a.fault.happened ? "faults" : "does not", b.fault.happened ? "faults" : "does not");
  if (a.fault.happened && (a.fault.address != b.fault.address || a.fault.target != b.fault.target)) {
    return std::format("the fault differs: game +0x{:X} memory {:08X}, ours +0x{:X} memory {:08X}", a.fault.address, a.fault.target, b.fault.address, b.fault.target);
  }
  // Only al: the caller tests al, and the rest of eax is whatever the game's loop last held (the list's address after a loop, the height after none), so it differs
  // whenever the loop is not run on the same call (ours runs it in the dispatcher and leaves the game's fill the tail).
  if ((a.eax & 0xFF) != (b.eax & 0xFF)) return std::format("the result: game {:08X}, ours {:08X}", a.eax, b.eax);
  if (a.row != b.row) return std::format("the row: game {}, ours {}", a.row, b.row);
  if (a.out != b.out) return std::format("out: game {}, ours {}", a.out, b.out);
  if (a.flags != b.flags) return std::format("MXCSR's flags: game {:02X}, ours {:02X}", a.flags, b.flags);
  if (!call.records_free && a.arena_used != b.arena_used) return std::format("the allocator's use: game {}, ours {}", a.arena_used, b.arena_used);
  if (!call.records_free && a.log != b.log) {
    size_t at = 0;
    while (at < a.log.size() && at < b.log.size() && a.log[at] == b.log[at]) ++at;
    return std::format("the log of the allocator and the tail differs at event {} (game {:08X} of {}, ours {:08X} of {})", at, at < a.log.size() ? a.log[at] : 0u,
                       a.log.size(), at < b.log.size() ? b.log[at] : 0u, b.log.size());
  }
  if (call.records_free) {
    std::memcpy(world->base + world->records_at, context->after_game.data() + world->records_at, world->bytes - world->records_at);
    std::memcpy(world->base + AR_CACHE + 0x1c, context->after_game.data() + AR_CACHE + 0x1c, 12);
  }
  if (call.check_sentinel && std::memcmp(world->base, context->after_game.data(), world->bytes) != 0) {
    // The sentinels: Check fills the record after its last (and the ones before, which the game's own run overwrites) with all ones, which stays where the game's own run makes
    // fewer records or none there: at most one 16-byte record per texture of a call, all ones in ours, anything else in the game's run.
    uint32_t exempt = 0;
    for (size_t slot = AR_HEADER_BYTES & ~static_cast<size_t>(15); slot + 16 <= world->bytes && exempt < AR_MAX_TEXTURES; slot += 16) {
      if (std::memcmp(world->base + slot, context->after_game.data() + slot, 16) == 0) continue;
      bool all_ones = true;
      for (size_t i = 0; all_ones && i < 16; ++i) all_ones = (world->base[slot + i] == 0xFF);
      if (!all_ones) continue;
      std::memcpy(world->base + slot, context->after_game.data() + slot, 16);
      ++exempt;
    }
  }
  if (std::memcmp(world->base, context->after_game.data(), world->bytes) != 0) {
    size_t at = 0;
    while (world->base[at] == context->after_game[at]) ++at;
    size_t differing = 0;
    size_t last = at;
    for (size_t i = at; i < world->bytes; ++i) {
      if (world->base[i] != context->after_game[i]) {
        ++differing;
        last = i;
      }
    }
    return std::format("the world at {}: game {:02X}, ours {:02X} ({} bytes differ up to {}; the vector's data at +{:X})", ArRegion(*world, at), context->after_game[at], world->base[at], differing,
                       ArRegion(*world, last), Read32(world->base + AR_CACHE + 0x24) - Address(world->base));
  }
  return "";
}

// A spec of the random cases: the sizes are drawn so that every kind of call shows up (empty lists to `max_items`, a row size from one item to a few dozen, a height that
// stops the call from a tenth of the list to well past it, no height at all and a huge one, every kind of pre-existing placement, growth from every capacity).
ArSpec ArRandomSpec(Random* random, uint32_t max_items) {
  ArSpec spec;
  spec.seed = (static_cast<uint64_t>(random->Next()) << 32) | random->Next();
  const uint32_t kind = random->Next() % 20;
  if (kind == 0) {
    spec.items = 0;
  } else if (kind < 5) {
    spec.items = 1 + random->Next() % 300;
  } else if (kind < 12) {
    spec.items = 300 + random->Next() % 4700;
  } else {
    spec.items = 5000 + random->Next() % (max_items - 5000 + 1);
  }
  spec.extra_entries = 64;
  spec.live_extra = (random->Next() & 1) != 0;
  spec.textures = 1 + random->Next() % AR_MAX_TEXTURES;
  spec.current = random->Next() % spec.textures;
  spec.frame = random->Next();
  spec.need_max = 2 + random->Next() % 300;
  spec.need_min = random->Next() % (spec.need_max + 1);
  spec.row_size = spec.need_max * (1 + random->Next() % 30) + random->Next() % 64;
  spec.oversize_percent = (random->Next() % 8 == 0 ? 1 + random->Next() % 20 : 0);
  spec.flag_percent = std::array<uint32_t, 3>{0, 50, 100}[random->Next() % 3];
  spec.share_percent = std::array<uint32_t, 4>{0, 10, 50, 100}[random->Next() % 4];
  spec.duplicate_percent = std::array<uint32_t, 4>{0, 0, 5, 30}[random->Next() % 4];
  spec.pre_state = (random->Next() % 10 < 6 ? 0 : 1 + random->Next() % 4);
  switch (random->Next() % 6) {
    case 0:
      spec.texels_per_row = random->Next() % 100000;
      break;
    case 1:
      spec.texels_per_row = (1u << 24) - 50 + random->Next() % 100;
      break;
    case 2:
      spec.texels_per_row = 0x7FFFFF00u + random->Next() % 512;
      break;
    case 3:
      spec.texels_per_row = 0xFFFFFFFEu - random->Next() % 4;
      break;
    default:
      spec.texels_per_row = random->Next();
      break;
  }
  const uint32_t records = (random->Next() % 5 == 0 ? 0 : spec.items / 4);
  spec.record_cap = std::array<uint32_t, 12>{0, 1, 2, 3, 7, 8, 64, 100, 1000, records, records + 1, records > 0 ? records - 1 : 0}[random->Next() % 12];
  // The height of each texture: from "stops about here" in the list.
  const uint32_t fraction = std::array<uint32_t, 6>{10, 40, 80, 100, 150, 400}[random->Next() % 6];
  const uint64_t average = (spec.need_min + spec.need_max) / 2 * 11 / 10 + 1;
  const uint64_t rows = static_cast<uint64_t>(spec.items) * fraction / 100 * average / spec.row_size + 1;
  for (uint32_t& height : spec.heights) {
    const uint32_t roll = random->Next() % 12;
    height = (roll == 0 ? 0 : roll == 1 ? 1000000 : static_cast<uint32_t>(std::max<uint64_t>(1, rows * (50 + random->Next() % 100) / 100)));
  }
  return spec;
}

ArCall ArRandomCall(const ArSpec& spec, Random* random) {
  ArCall call;
  call.update = (random->Next() % 3 != 0);
  call.tweak = (random->Next() & 1) != 0;
  call.end = (random->Next() % 100 < 63 ? spec.items : random->Next() % (spec.items + 1));
  call.start = (random->Next() % 2 == 0 ? 0 : random->Next() % (call.end + 1));
  if (random->Next() % 40 == 0 && spec.extra_entries != 0) call.end = spec.items + 1 + random->Next() % spec.extra_entries;
  const uint32_t force_roll = random->Next() % 10;
  call.force = (force_roll < 7 ? 0 : force_roll == 7 ? (random->Next() & 1 ? 0x100u : 0x200u) : force_roll == 8 ? 1u : random->Next());
  call.texture = (random->Next() % 50 == 0 ? random->Next() % (spec.textures + 3) : spec.current);
  call.rounding = (random->Next() % 5 == 0 ? random->Next() % 4 : 0);
  call.map_fails_at = (random->Next() % 10 == 0 ? random->Next() % 4 : ~0u);
  return call;
}

// The game's fill in the patched copy, for the dispatchers below that stand in for animation_rows::Dispatch.
const uint8_t* ar_game_fill = nullptr;
uint32_t ar_mutant = 0;

// Mutants of the game's call: each changes one thing a whole update or a fill call leaves, so that the comparison must see it (the harness's own CONTROLs).
uint32_t __fastcall ArMutantDispatch(void* cache, void* edx, void* device, uint32_t force, void* list, uint32_t texture, uint32_t* row, uint32_t end, uint32_t* out) {
  const auto fill = reinterpret_cast<FillFunction>(const_cast<uint8_t*>(ar_game_fill));
  const uint32_t start = *row;
  uint32_t used_end = end;
  if (ar_mutant == 1 && end - start > 3) used_end = end - 1;
  const unsigned csr_before = _mm_getcsr();
  if (ar_mutant == 7) _mm_setcsr(csr_before | (3 << 13));
  if (ar_mutant == 8) ArAlloc(1, 0);
  uint32_t result = fill(cache, edx, device, force, list, texture, row, used_end, out);
  if (ar_mutant == 7) _mm_setcsr((_mm_getcsr() & ~0x6000u) | (csr_before & 0x6000u));
  const auto cache_bytes = static_cast<uint8_t*>(cache);
  const auto list_bytes = static_cast<uint8_t*>(list);
  if (*row > start) {
    const auto entries = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(Read32(list_bytes + 8)));
    auto* instance = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(entries[*row - 1]));
    if (ar_mutant == 2) Write32(instance + 0x5C, Read32(instance + 0x5C) ^ 1);
    if (ar_mutant == 9) {
      auto* animation = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(Read32(instance + 0x60)));
      Write32(animation + 0x18, Read32(animation + 0x18) ^ 0x100);
    }
    if (ar_mutant == 12) Write32(instance + 0xA4, Read32(instance + 0xA4) ^ 1);
  }
  if (ar_mutant == 3) *out += 1;
  if (ar_mutant == 4 && Read32(cache_bytes + 0x1C) != 0 && Read32(cache_bytes + 0x24) != 0) {
    auto* data = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(Read32(cache_bytes + 0x24)));
    Write32(data + 4, Read32(data + 4) ^ 1);
  }
  if (ar_mutant == 5) result ^= 1;
  if (ar_mutant == 6) Write32(cache_bytes + 0x28, Read32(cache_bytes + 0x28) + 1);
  if (ar_mutant == 10) Write32(list_bytes + 4, Read32(list_bytes + 4) ^ 1);
  if (ar_mutant == 11 && *row > start) *row -= 1;
  return result;
}

// 23f. to 23t: the exactness of the animation fill (S4, S5). Called by RunAnimationRowsTests after its site and stub parts, with the game's own fill in both copies.
// What needs the real Dispatch (the module's path counters and test switches, TWATTILA_WORKER_CONTROLS) is under that macro: without it the cases that remain are
// the ones the pass-through stub also passes (it IS the game's fill), and the run says which were skipped.
int RunAnimationRowsExactFor(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick, bool streaming) {
  namespace ec = twattila::engine_code::internal;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  using twattila::animation_rows::Stage;
  const uint32_t fill_rva = build.code->fill_rva;
  CHECK(fill_rva == 0x182E080);
  const auto set_stage = [&](Stage stage) {
#if defined(TWATTILA_WORKER_CONTROLS)
    twattila::animation_rows::Update(stage);
#endif
    ec::failed_writes = 0;
    twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 1.f, 0, {.animation = stage});
  };
  const uint32_t dispatch = Address(reinterpret_cast<const void*>(&twattila::animation_rows::Dispatch));
  ArHooks hooks;
  hooks.Install(game);
  hooks.Install(patched);
  ar_game_fill = patched + fill_rva;
  const uint8_t* const game_fill = game + fill_rva;
  const uint8_t* const stub = ec::page + ec::WORKER_FILL_PAGE;
  auto context = std::make_unique<ArContext>();
  uint64_t printed = 0;
  if (std::getenv("AR_UNBUFFERED") != nullptr) std::setvbuf(stdout, nullptr, _IONBF, 0);  // a hang then shows the last case that printed
  const uint64_t seed_base = (std::getenv("AR_SEED") != nullptr ? std::strtoull(std::getenv("AR_SEED"), nullptr, 16) : 0x23F00D5EEDull);
  const char* const only = std::getenv("AR_ONLY");
  const auto selected = [&](const char* name) { return only == nullptr || std::strstr(only, name) != nullptr; };

  // The pool: `workers` threads besides the render thread may be woken by the animation fill (0: it runs alone). The env limit counts the render thread too.
  uint32_t env_generation = 1000;
  const auto set_workers = [&](uint32_t workers) {
    SetEnvironmentVariableA("RENODX_TW_WORKER_LIMITS", std::format("animation:{}", workers + 1).c_str());
    twattila::worker_pool::Update({.threads = 16, .in_use = true, .env_generation = ++env_generation});
  };
  struct Config {
    const char* name;
    uint32_t workers;
    uint32_t min_items;
  };
  const std::array<Config, 4> configs = {{{"alone, small calls too", 0, 1}, {"3 workers, small calls too", 3, 1}, {"15 workers", 15, 64}, {"7 workers, the design's minimum", 7, 2048}}};
  const auto apply = [&](const Config& config) {
    set_workers(config.workers);
#if defined(TWATTILA_WORKER_CONTROLS)
    ar_module::controls.min_items = config.min_items;
#endif
  };

  const auto compare = [&](const ArCall& call, ArResult* game_result = nullptr, ArResult* patched_result = nullptr) {
    const uint8_t* const game_function = (call.update ? game + ANIMATION_UPDATE_RVA : game_fill);
    const uint8_t* const patched_function = (call.update ? patched + ANIMATION_UPDATE_RVA : stub);
    return ArCompare(context.get(), game, game_function, patched, patched_function, call, game_result, patched_result);
  };
  // After a call that faulted through the stub: the stub's count stays raised (an unwind), which pins the module: back to 0 for the next call.
  const auto after_fault = [&] {
    ec::worker_data->cut_active.store(0);
#if defined(TWATTILA_WORKER_CONTROLS)
    ar_module::ring_in_use.store(false);
#endif
  };
  const auto describe = [](const ArSpec& spec, const ArCall& call) {
    return std::format("seed {:016X}, items {} start {} end {} {} force {:X} texture {}/{} tweak {} rounding {} caps {} pre {} share {} dup {}", spec.seed, spec.items,
                       call.start, call.end, call.update ? "update" : "fill", call.force, call.texture, spec.textures, call.tweak ? 1 : 0, call.rounding,
                       spec.record_cap, spec.pre_state, spec.share_percent, spec.duplicate_percent);
  };
  // One world, each of the calls on it; the number of calls that differ.
  const auto run_case = [&](const char* label, uint64_t index, const ArSpec& spec, const std::vector<ArCall>& calls) -> uint64_t {
    if (!ArBuild(context.get(), spec)) {
      std::printf("   %s %llu: the world could not be made\n", label, index);
      return 1;
    }
    uint64_t wrong = 0;
    for (const ArCall& call : calls) {
      const std::string what = compare(call);
      after_fault();
      if (what.empty()) continue;
      ++wrong;
      if (++printed <= 12) std::printf("   %s %llu: %s: %s\n", label, index, describe(spec, call).c_str(), what.c_str());
    }
    return wrong;
  };
  // The calls every world is run through: the update with the tweak off and on, and the fill on each texture it has.
  const auto standard_calls = [&](const ArSpec& spec) {
    std::vector<ArCall> calls;
    calls.push_back({.update = true, .start = 0, .end = spec.items});
    calls.push_back({.update = true, .start = 0, .end = spec.items, .tweak = true});
    for (uint32_t texture = 0; texture < spec.textures; ++texture) {
      calls.push_back({.update = false, .start = 0, .end = spec.items, .texture = texture, .tweak = (texture & 1) != 0});
    }
    return calls;
  };
  // Makes a random call one that cannot fault and that no other thread's call of the same copy can change: no null item, a texture and an end the world holds, the tweak and the
  // rounding as set. (Where the game's own code reads garbage it can read what another thread's call is changing: the faults are 23o's.)
  const auto sanitize = [](ArSpec* spec, ArCall* call) {
    spec->null_instance_at = spec->null_animation_at = ~0u;
    call->tweak = false;
    call->rounding = 0;
    call->check_sentinel = true;
    call->texture %= std::max<uint32_t>(1, spec->textures);
    call->end = std::min(call->end, spec->items);
  };
  // A line of what the paths counters say, for a case that wants to show it ran the real thing.
  const auto path_line = [&](const ArStats& before) {
    std::string line;
#if defined(TWATTILA_WORKER_CONTROLS)
    static constexpr std::array<const char*, 12> NAMES = {"ours", "off", "forced", "row at end", "few items", "end past list", "texture range", "vector not empty", "no rows", "ring busy", "bad item", "check memory"};
    for (size_t path = 0; path < NAMES.size(); ++path) {
      if (const uint64_t count = ar_stats.paths[path] - before.paths[path]; count != 0) line += std::format("{}{} {}", line.empty() ? "" : ", ", NAMES[path], count);
    }
    line += std::format("; {} woke workers", ar_stats.workers_used - before.workers_used);
#else
    line = "pass-through stub: no paths counted";
#endif
    return line;
  };

#if defined(TWATTILA_WORKER_CONTROLS)
  // The schedule through the module's own env path (RENODX_TW_ANIMATION_SCHEDULE, read again whenever the generation changes).
  static uint32_t schedule_generation = 500;
  SetEnvironmentVariableA("RENODX_TW_ANIMATION_SCHEDULE", streaming ? "stream" : "bulk");
  twattila::animation_rows::Update(Stage::ON, ++schedule_generation);
  CHECK(ar_module::schedule.load() == (streaming ? twattila::animation_rows::Schedule::STREAM : twattila::animation_rows::Schedule::BULK));
#endif
  set_stage(Stage::ON);
  CHECK((ec::written & ec::WORKER_FILL_BIT) != 0 && ec::worker_data->fill_slot.load() == dispatch);
  ec::worker_data->cut_active.store(0);
  const uint32_t max_items = (quick ? 30000 : 200000);
  Random random = {.state = seed_base * 0x2545F4914F6CDD1Dull + 1};
  std::printf("   S4/S5 seeds: base %llX (AR_SEED=<hex> AR_ONLY=<case names> AR_SCHEDULE=bulk|stream to repeat), schedule %s, items up to %u%s\n", seed_base, streaming ? "stream" : "bulk", max_items,
#if defined(TWATTILA_WORKER_CONTROLS)
              ", the module's counters and switches on"
#else
              ", the pass-through stub only: no counters, no switches (build with TWATTILA_WORKER_CONTROLS against the real Dispatch)"
#endif
  );
  set_workers(0);

  // 23f. The harness's own CONTROLs: a dispatcher that changes one thing (or nothing) after the game's own call, each of them seen by the comparison.
  if (selected("self")) {
    ArSpec spec = {.seed = 0x5E1F, .items = 4000, .textures = 2, .current = 1, .frame = 77, .row_size = 600, .texels_per_row = 0x01000001u,
                   .need_min = 3, .need_max = 40, .flag_percent = 100, .share_percent = 20, .pre_state = 2, .record_cap = 0};
    spec.heights = {200, 200, 200, 200, 200, 200};
    CHECK(ArBuild(context.get(), spec));
    const ArCall call = {.update = false, .start = 0, .end = 4000, .force = 0, .texture = 1, .tweak = true};
    CHECK(compare(call).empty());
    ec::worker_data->fill_slot.store(Address(reinterpret_cast<const void*>(&ArMutantDispatch)));
    ar_mutant = 0;
    CHECK(compare(call).empty());
    const std::array<const char*, 13> names = {"nothing", "the end one short", "an instance's texture word", "out", "a record word", "the result's al", "the cache's frame",
                                               "MXCSR's rounding", "an extra allocation", "an animation's placement", "the list's count", "the row one short",
                                               "an instance's texel word"};
    int seen = 0;
    for (uint32_t mutant = 1; mutant < names.size(); ++mutant) {
      ar_mutant = mutant;
      const std::string what = compare(call);
      if (!what.empty()) ++seen;
      std::printf("   control %u (%s): %s\n", mutant, names[mutant], what.empty() ? "NOT SEEN" : what.c_str());
      CHECK(!what.empty());
    }
    // The same through a whole update (its fill calls go through the slot too). (`out` and the fill's result are the update's own locals and al: not seen through it.)
    for (uint32_t mutant : {1u, 2u, 4u, 6u, 9u, 10u, 11u, 12u}) {
      ar_mutant = mutant;
      ArCall whole = call;
      whole.update = true;
      const std::string what = compare(whole);
      if (what.empty()) std::printf("   control %u through a whole update: NOT SEEN\n", mutant);
      CHECK(!what.empty());
    }
    ar_mutant = 0;
    ec::worker_data->fill_slot.store(dispatch);
    std::printf("23f. the comparison's own controls: the do-nothing dispatcher equal, each of %d changes seen (the call, and 8 of them through a whole update)\n", seen);
  }

  // 23g. Random worlds: lists of 0 to max_items, every kind of need, flag, row size, texels per row, height, pre-existing placement and records' capacity; the end below the count in
  // 37 % of the calls, non-zero starts, force and texture values the fallbacks take, all four rounding modes in a fifth of the calls, the tweak on and off; in each pool config.
  if (selected("random")) {
    const uint64_t cases = (quick ? 80 : 300);
    for (const Config& config : configs) {
      apply(config);
      const ArStats before = ar_stats;
      uint64_t wrong = 0;
      for (uint64_t index = 0; index < cases; ++index) {
        const ArSpec spec = ArRandomSpec(&random, max_items);
        wrong += run_case("random", index, spec, {ArRandomCall(spec, &random), ArRandomCall(spec, &random)});
      }
      std::printf("23g. %s: %llu random worlds, %llu calls (%llu stopped before their end, %llu stepped items, %llu with the tail's Map, %llu faults the same in both; %s): %llu differ\n",
                  config.name, cases, ar_stats.runs - before.runs, ar_stats.stopped_early - before.stopped_early, ar_stats.items_stepped - before.items_stepped,
                  ar_stats.tail_runs - before.tail_runs, ar_stats.equal_faults - before.equal_faults, path_line(before).c_str(), wrong);
      CHECK(wrong == 0);
    }
  }

  // 23h. Shared animations 0 / 10 / 50 / 100 % and duplicate instances (in one block and across blocks): 5 to 12 k items, every standard call, in each pool config.
  if (selected("shared")) {
    for (const Config& config : configs) {
      apply(config);
      const ArStats before = ar_stats;
      uint64_t wrong = 0;
      uint64_t index = 0;
      for (const uint32_t share : {0u, 10u, 50u, 100u}) {
        for (const uint32_t duplicate : {0u, 5u, 50u, 100u}) {
          ArSpec spec = {.seed = seed_base + 1000 * index, .items = 5000 + random.Next() % (quick ? 2000 : 7000), .extra_entries = 8, .textures = 2, .current = 0,
                         .frame = random.Next(), .row_size = 4096, .texels_per_row = 40000, .need_min = 1, .need_max = 120, .flag_percent = 50,
                         .share_percent = share, .duplicate_percent = duplicate, .pre_state = 0, .record_cap = 0};
          spec.heights = {500, 40, 500, 500, 500, 500};
          wrong += run_case("shared", index++, spec, standard_calls(spec));
        }
      }
      std::printf("23h. %s: shared animations 0 / 10 / 50 / 100 %% x duplicate instances 0 / 5 / 50 / 100 %%, %llu worlds, %llu calls (%s): %llu differ\n", config.name, index,
                  ar_stats.runs - before.runs, path_line(before).c_str(), wrong);
      CHECK(wrong == 0);
    }
  }

  // 23i. Cached-placement pre-states (valid, stale frame, other texture, minus one, mixed), with the valid frame one off as well.
  if (selected("prestate")) {
    for (const Config& config : configs) {
      apply(config);
      const ArStats before = ar_stats;
      uint64_t wrong = 0;
      uint64_t index = 0;
      for (const uint32_t state : {0u, 1u, 2u, 3u, 4u}) {
        for (const uint32_t frame_offset : {0u, 1u}) {
          for (const uint32_t share : {0u, 60u}) {
            ArSpec spec = {.seed = seed_base + 77 + 1000 * index, .items = 4000 + random.Next() % 4000, .extra_entries = 8, .textures = 3, .current = 1,
                           .frame = random.Next(), .row_size = 2000, .texels_per_row = 9000, .need_min = 0, .need_max = 90, .flag_percent = 50,
                           .share_percent = share, .duplicate_percent = 5, .pre_state = state, .record_cap = 3, .frame_offset = frame_offset};
            wrong += run_case("prestate", index++, spec, standard_calls(spec));
          }
        }
      }
      std::printf("23i. %s: placements valid / stale / other texture / minus one / mixed (the frame also one off), %llu worlds, %llu calls (%s): %llu differ\n", config.name, index,
                  ar_stats.runs - before.runs, path_line(before).c_str(), wrong);
      CHECK(wrong == 0);
    }
  }

  // 23j. Oversize items: an item larger than a row opens the next row and leaves the current one empty (row 0 too), and after it `row size - used` wraps, so every later item fits.
  if (selected("oversize")) {
    for (const Config& config : configs) {
      apply(config);
      const ArStats before = ar_stats;
      uint64_t wrong = 0;
      uint64_t index = 0;
      for (const uint32_t row_size : {20u, 100u, 700u, 5000u}) {
        for (const uint32_t oversize : {1u, 5u, 30u, 100u}) {
          for (const uint32_t flag : {0u, 100u}) {
            ArSpec spec = {.seed = seed_base + 5000 + 1000 * index, .items = 4500 + random.Next() % 3000, .extra_entries = 8, .textures = 2, .current = 0,
                           .frame = random.Next(), .row_size = row_size, .texels_per_row = 7001, .need_min = 0, .need_max = std::max(2u, row_size / 3),
                           .oversize_percent = oversize, .flag_percent = flag, .share_percent = 0, .duplicate_percent = 0, .pre_state = 2, .record_cap = 0};
            spec.heights = {300, 3000, 300, 300, 300, 300};
            wrong += run_case("oversize", index++, spec, standard_calls(spec));
          }
        }
      }
      std::printf("23j. %s: oversize items (row sizes 20 .. 5000, 1 .. 100 %% of the items larger than a row, with and without the flag), %llu worlds, %llu calls (%s): %llu differ\n",
                  config.name, index, ar_stats.runs - before.runs, path_line(before).c_str(), wrong);
      CHECK(wrong == 0);
    }
  }

  // 23k. Growth of the records' vector inside a call: every record is a placement (stale, unshared), so a call of n items makes n records, from capacity 0 and from capacities around
  // n and its halves and the powers of two it passes.
  if (selected("growth")) {
    for (const Config& config : {configs[0], configs[1]}) {
    apply(config);
    const ArStats before = ar_stats;
    uint64_t wrong = 0;
    uint64_t index = 0;
    for (const uint32_t items : {1u, 2u, 3u, 4u, 5u, 16u, 17u, 511u, 512u, 513u, 1023u, 1024u, 1025u, 2047u, 2048u, 2049u, 4095u, 4096u, 4097u, 8191u, 8192u, 8193u}) {
      std::vector<uint32_t> capacities = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, items - 1, items, items + 1, items / 2, items / 2 + 1, items / 2 - 1 + (items < 2 ? 1 : 0)};
      for (uint32_t power = 1; power <= items; power *= 2) {
        capacities.push_back(power - 1);
        capacities.push_back(power);
        capacities.push_back(power + 1);
      }
      std::sort(capacities.begin(), capacities.end());
      capacities.erase(std::unique(capacities.begin(), capacities.end()), capacities.end());
      for (const uint32_t capacity : capacities) {
        ArSpec spec = {.seed = seed_base + 9000 + items * 31 + capacity, .items = items, .extra_entries = 4, .textures = 1, .current = 0, .frame = 5, .row_size = 100000,
                       .texels_per_row = 300000, .need_min = 1, .need_max = 3, .flag_percent = 0, .share_percent = 0, .duplicate_percent = 0, .pre_state = 2,
                       .record_cap = capacity};
        spec.heights = {1000000, 1, 1, 1, 1, 1};
        const ArCall calls[] = {{.update = false, .start = 0, .end = items, .texture = 0, .tweak = true}, {.update = true, .start = 0, .end = items}};
        wrong += run_case("growth", index++, spec, {std::begin(calls), std::end(calls)});
      }
    }
    std::printf("23k. %s: growth inside a call, lists of 1 to 8193 items from capacity 0 and from the capacities around the count, its half and the powers of two, %llu worlds (%s): %llu differ\n",
                config.name, index, path_line(before).c_str(), wrong);
    CHECK(wrong == 0);
    }
  }

  // 23l. F, the texel's float: the pre-existing texel words (the fast path) and the computed ones (texels per row at the edges) at 0, 2^24 + 1, 2^31, 0xFFFFFFFE and random values
  // above 2^24, all four MXCSR rounding modes on the dispatching thread; the result, every instance word and the sticky flags equal to the game's.
  if (selected("float")) {
    for (const Config& config : {configs[0], configs[1]}) {
      apply(config);
      const ArStats before = ar_stats;
      uint64_t wrong = 0;
      uint64_t index = 0;
      for (const uint32_t rounding : {0u, 1u, 2u, 3u}) {
        for (const uint32_t texels_per_row : {0u, 1u, 3u, 0x01000001u, 0x01000003u, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFEu, 0xFFFFFF01u, 0x123456F7u}) {
          for (const uint32_t state : {1u, 2u}) {
            ArSpec spec = {.seed = seed_base + 20000 + 1000 * index, .items = 3000 + random.Next() % 2000, .extra_entries = 8, .textures = 1, .current = 0,
                           .frame = random.Next(), .row_size = 3000, .texels_per_row = texels_per_row, .need_min = 1, .need_max = 80, .flag_percent = 50,
                           .share_percent = 30, .duplicate_percent = 0, .pre_state = state, .pre_texel_edge = true, .record_cap = 0};
            spec.heights = {400, 1, 1, 1, 1, 1};
            wrong += run_case("float", index++, spec,
                              {{.update = false, .start = 0, .end = spec.items, .texture = 0, .tweak = true, .rounding = rounding},
                               {.update = true, .start = 0, .end = spec.items, .rounding = rounding}});
          }
        }
      }
      std::printf("23l. %s: F at the edge values, 4 MXCSR rounding modes, %llu worlds (%s): %llu differ\n", config.name, index, path_line(before).c_str(), wrong);
      CHECK(wrong == 0);
    }
  }


#if defined(TWATTILA_WORKER_CONTROLS)
  // The cases below read the module's counters and set its test switches: every case puts the switches back (the next case, and the production code, run with them off).
  const auto reset_switches = [&] {
    const uint32_t min_items = ar_module::controls.min_items;
    ar_module::controls = {};
    ar_module::controls.min_items = min_items;
    ar_module::failed.store(false);
    ar_module::ring_in_use.store(false);
    twattila::animation_rows::Update(Stage::ON);
  };
  const auto expected_path = [](const ArResult& result, ArPath wanted) {
    uint32_t others = 0;
    for (size_t path = 0; path < result.paths.taken.size(); ++path) {
      if (path != static_cast<size_t>(wanted) && !(wanted == ArPath::OURS && path == static_cast<size_t>(ArPath::BAD_ITEM))) others += result.paths.taken[path];
    }
    return result.paths.taken[static_cast<size_t>(wanted)] >= 1 && others == 0;
  };
  using ArCases = std::vector<std::pair<ArSpec, std::vector<ArCall>>>;
  const auto count_differences = [&](const ArCases& worlds) -> uint64_t {
    uint64_t wrong = 0;
    for (const auto& [spec, calls] : worlds) {
      CHECK(ArBuild(context.get(), spec));
      for (const ArCall& call : calls) {
        wrong += (compare(call).empty() ? 0 : 1);
        after_fault();
      }
    }
    return wrong;
  };
  // 23m. A texel of 0xFFFFFFFF (the first item of row 1 when the texels per row are all ones): its animation reads as not placed, so a later item of the same animation places
  // again, and a duplicate instance gets two different words: the writes must land in item order (ordered mode). With a scatter that lands late (a switch) the last write wins wrongly
  // unless the order is kept.
  if (selected("ordered")) {
    for (const Config& config : {configs[0], configs[1], configs[2]}) {
      apply(config);
      reset_switches();
      const ArStats before = ar_stats;
      uint64_t wrong = 0;
      uint64_t ordered_calls = 0;
      const uint64_t worlds = (quick ? 10 : 40);
      ArCases cases;
      for (uint64_t index = 0; index < worlds; ++index) {
        ArSpec spec = {.seed = seed_base + 40000 + index * 17, .items = 5000 + random.Next() % 4000, .extra_entries = 8, .textures = 1, .current = 0,
                       .frame = random.Next(), .row_size = 300 + random.Next() % 300, .texels_per_row = 0xFFFFFFFFu, .need_min = 2, .need_max = 60, .flag_percent = 50,
                       .share_percent = 90, .duplicate_percent = 90, .pre_state = (index % 3 == 0 ? 0u : index % 3 == 1 ? 2u : 5u), .record_cap = 0};
        spec.heights = {3000, 1, 1, 1, 1, 1};
        cases.push_back({spec, {{.update = false, .start = 0, .end = spec.items, .texture = 0}, {.update = true, .start = 0, .end = spec.items},
                                {.update = false, .start = 7, .end = spec.items, .texture = 0, .tweak = true}}});
      }
      for (const auto& [spec, calls] : cases) {
        CHECK(ArBuild(context.get(), spec));
        for (const ArCall& call : calls) {
          ArResult game_result;
          ArResult patched_result;
          const std::string what = compare(call, &game_result, &patched_result);
          ordered_calls += (patched_result.paths.ordered && patched_result.paths.taken[0] != 0 ? 1 : 0);
          if (!what.empty() && ++printed <= 12) std::printf("   ordered: %s: %s\n", describe(spec, call).c_str(), what.c_str());
          wrong += (what.empty() ? 0 : 1);
        }
      }
      // The control: no ordered mode, and the first block's scatter lands 15 ms late; the same worlds must differ somewhere (with workers: the blocks land in parallel).
      uint64_t seen = 0;
      if (config.workers != 0) {
        ar_module::controls.no_ordered_mode = true;
        ar_module::controls.scatter_delay_block = 0;
        ar_module::controls.scatter_delay_ms = 15;
        seen = count_differences(cases);
        ar_module::controls.no_ordered_mode = false;
        const uint64_t late_but_ordered = count_differences(cases);
        reset_switches();
        std::printf("23m. %s: texel 0xFFFFFFFF with duplicate instances, %llu worlds, %llu calls, %llu in ordered mode (%s): %llu differ; CONTROL no ordered mode + late scatter: %llu differ (must be > 0); ordered mode with the late scatter: %llu differ\n",
                    config.name, worlds, ar_stats.runs - before.runs, ordered_calls, path_line(before).c_str(), wrong, seen, late_but_ordered);
        CHECK(seen > 0 && late_but_ordered == 0);
      } else {
        std::printf("23m. %s: texel 0xFFFFFFFF with duplicate instances, %llu worlds, %llu calls, %llu in ordered mode (%s): %llu differ\n", config.name, worlds,
                    ar_stats.runs - before.runs, ordered_calls, path_line(before).c_str(), wrong);
      }
      CHECK(wrong == 0 && ordered_calls > 0);
    }
  }

  // 23n. Every fallback of Dispatch (3.3) with a counter that says which path the call took (and only that): force 0x100 is not forced, a start at the end, a call of 2047 / 2048
  // items, an end past the list's count (with the game stopping on its height before the garbage past it), a texture past the cache's count (its entry exists), a vector
  // that is not empty, a texture with no rows, the ring in use, the row off.
  if (selected("fallback")) {
    apply(configs[3]);
    reset_switches();
    const ArStats before = ar_stats;
    ArSpec spec = {.seed = seed_base + 60000, .items = 6000, .extra_entries = 24, .textures = 4, .current = 1, .frame = 9, .row_size = 3000, .texels_per_row = 5000,
                   .need_min = 2, .need_max = 50, .flag_percent = 50, .share_percent = 40, .pre_state = 0, .record_cap = 0};
    spec.heights = {300, 300, 40, 0, 300, 300};
    struct Fallback {
      const char* name;
      ArCall call;
      ArPath expected;
      uint32_t initial_count;
      bool ring_busy;
      bool row_off;
      bool live_extra;
    };
    const uint32_t items = spec.items;
    const std::vector<Fallback> list = {
        {"force 1", {.update = false, .end = items, .force = 1, .texture = 1}, ArPath::FORCED},
        {"force 0x101", {.update = false, .end = items, .force = 0x101, .texture = 1}, ArPath::FORCED},
        {"force 0xFF", {.update = false, .end = items, .force = 0xFF, .texture = 1}, ArPath::FORCED},
        {"force 0x80000001", {.update = false, .end = items, .force = 0x80000001u, .texture = 1}, ArPath::FORCED},
        {"force 0x100 (not forced)", {.update = false, .end = items, .force = 0x100, .texture = 1}, ArPath::OURS},
        {"force 0x80000000 (not forced)", {.update = false, .end = items, .force = 0x80000000u, .texture = 1}, ArPath::OURS},
        {"force 0xFFFFFF00 (not forced)", {.update = false, .end = items, .force = 0xFFFFFF00u, .texture = 1, .tweak = true}, ArPath::OURS},
        {"start at the end", {.update = false, .start = items, .end = items, .texture = 1}, ArPath::ROW_AT_END},
        {"start 0, end 0", {.update = false, .start = 0, .end = 0, .texture = 1}, ArPath::ROW_AT_END},
        {"start past the end", {.update = false, .start = 100, .end = 50, .texture = 1}, ArPath::ROW_AT_END},
        {"1 item", {.update = false, .start = 0, .end = 1, .texture = 1}, ArPath::FEW_ITEMS},
        {"100 items from 5", {.update = false, .start = 5, .end = 105, .texture = 1}, ArPath::FEW_ITEMS},
        {"2047 items", {.update = false, .start = 0, .end = 2047, .texture = 1}, ArPath::FEW_ITEMS},
        {"2048 items", {.update = false, .start = 0, .end = 2048, .texture = 1}, ArPath::OURS},
        {"2048 items from 7", {.update = false, .start = 7, .end = 2055, .texture = 1, .tweak = true}, ArPath::OURS},
        {"2049 items", {.update = false, .start = 0, .end = 2049, .texture = 1}, ArPath::OURS},
        {"end past the list's count by 1 (garbage past it)", {.update = false, .start = 0, .end = items + 1, .texture = 2}, ArPath::END_PAST_LIST},
        {"end past the list's count by 20", {.update = false, .start = 0, .end = items + 20, .texture = 2}, ArPath::END_PAST_LIST},
        {"end past the list's count, live entries past it", {.update = false, .start = 0, .end = items + 10, .texture = 2}, ArPath::END_PAST_LIST, 0, false, false, true},
        {"end at the count", {.update = false, .start = 0, .end = items, .texture = 2}, ArPath::OURS},
        {"texture past the cache's count (its object exists)", {.update = false, .start = 0, .end = items, .texture = 4}, ArPath::TEXTURE_RANGE},
        {"texture 5", {.update = false, .start = 0, .end = items, .texture = 5}, ArPath::TEXTURE_RANGE},
        {"a vector with 7 records", {.update = false, .start = 0, .end = items, .texture = 1, .tweak = true}, ArPath::VECTOR_NOT_EMPTY, 7},
        {"a texture with no rows", {.update = false, .start = 0, .end = items, .texture = 3}, ArPath::NO_ROWS},
        {"the ring in use", {.update = false, .start = 0, .end = items, .texture = 1}, ArPath::RING_BUSY, 0, true},
        {"the row off (the site still in)", {.update = false, .start = 0, .end = items, .texture = 1}, ArPath::STAGE_OFF, 0, false, true},
    };
    uint64_t wrong = 0;
    for (const Fallback& fallback : list) {
      ArSpec local = spec;
      local.record_cap = (fallback.initial_count != 0 ? 100 : 0);
      local.initial_count = fallback.initial_count;
      local.live_extra = fallback.live_extra;
      CHECK(ArBuild(context.get(), local));
      reset_switches();
      ar_module::controls.min_items = 2048;
      if (fallback.ring_busy) ar_module::ring_in_use.store(true);
      if (fallback.row_off) twattila::animation_rows::Update(Stage::OFF);
      ArResult game_result;
      ArResult patched_result;
      const std::string what = compare(fallback.call, &game_result, &patched_result);
      after_fault();
      const bool path_ok = expected_path(patched_result, fallback.expected);
      if (!what.empty() || !path_ok) {
        ++wrong;
        std::printf("   fallback \"%s\": %s%s\n", fallback.name, what.empty() ? "equal" : what.c_str(), path_ok ? "" : ", but not the path expected");
      }
    }
    reset_switches();
    std::printf("23n. every fallback: %zu calls, each took the path it should and left the game's result: %llu wrong (%s)\n", list.size(), wrong, path_line(before).c_str());
    CHECK(wrong == 0);
  }

  // 23o. Null instances and animations in every kind of place: the game's loop faults on the item (+0x182e0e0 / +0x182e0e3), and ours must stop there, leave the world as the loop
  // had it to that item, and run the game's own loop, which faults on the same instruction at the same memory (alone, with 3 and with 15 workers).
  if (selected("fault")) {
    for (const Config& config : {configs[0], configs[1], configs[2]}) {
      apply(config);
      reset_switches();
      const ArStats before = ar_stats;
      uint64_t wrong = 0;
      uint64_t faults = 0;
      uint64_t cases = 0;
      for (const bool null_animation : {false, true}) {
        for (const uint32_t at : {0u, 1u, 100u, 511u, 512u, 513u, 1023u, 1024u, 2047u, 2048u, 4095u, 4096u, 4097u, 4999u}) {
          ArSpec spec = {.seed = seed_base + 70000 + at, .items = 5000, .extra_entries = 8, .textures = 1, .current = 0, .frame = 3, .row_size = 5000, .texels_per_row = 6000,
                         .need_min = 1, .need_max = 70, .flag_percent = 50, .share_percent = 30, .duplicate_percent = 10, .pre_state = 0, .record_cap = 0};
          spec.heights = {1000000, 1, 1, 1, 1, 1};
          (null_animation ? spec.null_animation_at : spec.null_instance_at) = at;
          CHECK(ArBuild(context.get(), spec));
          for (const ArCall& call : {ArCall{.update = false, .start = 0, .end = 5000, .texture = 0}, ArCall{.update = false, .start = 0, .end = 5000, .texture = 0, .tweak = true},
                                     ArCall{.update = true, .start = 0, .end = 5000}}) {
            ArResult game_result;
            ArResult patched_result;
            const std::string what = compare(call, &game_result, &patched_result);
            after_fault();
            ++cases;
            faults += (game_result.fault.happened && patched_result.fault.happened ? 1 : 0);
            if (!what.empty() || !game_result.fault.happened) {
              ++wrong;
              if (++printed <= 12) std::printf("   fault %s at %u: %s: %s\n", null_animation ? "animation" : "instance", at, describe(spec, call).c_str(), what.empty() ? "the game did not fault" : what.c_str());
            }
          }
        }
      }
      std::printf("23o. %s: a null %s in %llu calls (%llu faulted the same way in both, same instruction and memory; %s): %llu differ\n", config.name, "instance or animation", cases,
                  faults, path_line(before).c_str(), wrong);
      CHECK(wrong == 0 && faults == cases);
    }
  }

  // 23p. Check mode: no mismatch on any world (the state after the call is the game's own run in the end), the tweak at +0x2a61058 off (no `out`, no Map) and on; an injected
  // wrong texel word is reported once, turns the row off and the final state is still the game's; a game run that makes more or fewer rows than ours is reported.
  if (selected("check")) {
    const auto mismatch_lines = [&] {
      const std::unique_lock lock(log_mutex);
      uint64_t count = 0;
      for (const std::string& line : log_lines) count += (line.find("Animation Rows on Workers: Check found a difference") != std::string::npos ? 1 : 0);
      return count;
    };
    for (const Config& config : {configs[0], configs[1]}) {
      apply(config);
      reset_switches();
      twattila::animation_rows::Update(Stage::CHECK);
      const ArStats before = ar_stats;
      const uint32_t checks_before = ar_module::check.calls;
      uint64_t wrong = 0;
      const uint64_t cases = (quick ? 60 : 200);
      for (uint64_t index = 0; index < cases; ++index) {
        const ArSpec spec = ArRandomSpec(&random, max_items);
        ArCall first = ArRandomCall(spec, &random);
        ArCall second = ArRandomCall(spec, &random);
        first.tweak = false;
        second.tweak = true;
        first.check_sentinel = second.check_sentinel = true;
        wrong += run_case("check", index, spec, {first, second});
      }
      std::printf("23p. %s: Check on %llu random worlds (the tweak off and on), %u Check calls, %llu differ, the row %s (%s)\n", config.name, cases, ar_module::check.calls - checks_before,
                  wrong, ar_module::failed.load() ? "WENT OFF" : "stayed on", path_line(before).c_str());
      CHECK(wrong == 0 && !ar_module::failed.load() && ar_module::check.calls > checks_before);
    }
    // An injected wrong texel word.
    apply(configs[1]);
    {
      const uint64_t lines_before = mismatch_lines();
      uint64_t wrong = 0;
      uint64_t reported = 0;
      for (const uint32_t item : {0u, 1u, 511u, 512u, 1500u, 2999u}) {
        ArSpec spec = {.seed = seed_base + 80000 + item, .items = 3000, .extra_entries = 8, .textures = 1, .current = 0, .frame = 1, .row_size = 4000, .texels_per_row = 7000,
                       .need_min = 1, .need_max = 60, .flag_percent = 50, .share_percent = 20, .duplicate_percent = 5, .pre_state = 0, .record_cap = 0};
        spec.heights = {1000000, 1, 1, 1, 1, 1};
        reset_switches();
        twattila::animation_rows::Update(Stage::CHECK);
        ar_module::controls.min_items = 1;
        ar_module::controls.corrupt_item = item;
        CHECK(ArBuild(context.get(), spec));
        const std::string what = compare({.update = false, .start = 0, .end = 3000, .texture = 0, .tweak = (item & 1) != 0, .check_sentinel = true});
        after_fault();
        if (!what.empty()) {
          ++wrong;
          std::printf("   injected at %u: the final state is not the game's: %s\n", item, what.c_str());
        }
        reported += (ar_module::failed.load() ? 1 : 0);
      }
      reset_switches();
      const uint64_t lines = mismatch_lines() - lines_before;
      std::printf("23p. Check, one wrong texel word injected at 6 items: reported %llu of 6 times (%llu log lines, the row off each time), the final state the game's in all: %llu wrong\n", reported, lines,
                  wrong);
      CHECK(wrong == 0 && reported == 6 && lines == 6);
    }
    // A game run that makes more or fewer rows than ours (the second call of the textures' height is shifted), the final state the game's run on the shifted heights.
    {
      uint64_t wrong = 0;
      uint64_t reported = 0;
      // Ten items to a row, so the 9000 items take 900 rows: a height from 900 up makes them all. The game's run makes the rows of its own (the shifted) height, ours those of the
      // unshifted one: the shifts that leave both at 900 or more (and 0) are the same run and must not be reported.
      for (const auto& [height, shift] : std::array<std::pair<uint32_t, int32_t>, 6>{{{500, -400}, {500, 300}, {500, 1}, {500, -1}, {1000, 0}, {1000, -50}}}) {
        const int32_t delta = shift;
        const bool differs = std::min<int32_t>(900, static_cast<int32_t>(height) + shift) != std::min<int32_t>(900, static_cast<int32_t>(height));
        ArSpec spec = {.seed = seed_base + 81000 + height * 8 + shift, .items = 9000, .extra_entries = 8, .textures = 1, .current = 0, .frame = 1, .row_size = 100, .texels_per_row = 7000,
                       .need_min = 10, .need_max = 10, .flag_percent = 0, .share_percent = 0, .duplicate_percent = 0, .pre_state = 2, .record_cap = 0};
        spec.heights = {height, 1, 1, 1, 1, 1};
        reset_switches();
        twattila::animation_rows::Update(Stage::CHECK);
        ar_module::controls.min_items = 1;
        CHECK(ArBuild(context.get(), spec));
        ArCall call = {.update = false, .start = 0, .end = 9000, .texture = 0, .tweak = delta > 0, .height_shift_from = 1, .height_delta = static_cast<uint32_t>(delta),
                       .game_height_shift_from = 0, .records_free = true, .check_sentinel = true};
        const std::string what = compare(call);
        after_fault();
        const bool was_reported = ar_module::failed.load();
        reported += (was_reported == differs ? 1 : 0);
        if (!what.empty() || was_reported != differs) {
          ++wrong;
          if (const std::unique_lock lock(log_mutex); !log_lines.empty()) std::printf("   last log line: %s\n", log_lines.back().c_str());
          std::printf("   ours at %u rows, the game's run %+d: %s (reported %d, expected %d)\n", height, delta, what.empty() ? "the final state is the game's" : what.c_str(), was_reported ? 1 : 0, differs ? 1 : 0);
        }
      }
      reset_switches();
      std::printf("23p. Check, the game's own run making more or fewer records than ours (4 cases) or the same (2): reported as it should %llu of 6, the final state the game's: %llu wrong\n", reported, wrong);
      CHECK(wrong == 0 && reported == 6);
    }
  }

  // 23q. The controls that must fail: each switch breaks one rule of the design, the same worlds run clean with it off and must differ with it on.
  if (selected("controls")) {
    int control_failures = 0;
    const auto control = [&](const char* name, const Config& config, const std::function<void()>& on, const std::function<ArCases()>& make, bool broken_world = false) {
      apply(config);
      reset_switches();
      const ArCases worlds = make();
      uint64_t calls = 0;
      for (const auto& [spec, call_list] : worlds) calls += call_list.size();
      const uint64_t clean = count_differences(worlds);
      on();
      const uint64_t seen = count_differences(worlds);
      reset_switches();
      std::printf("23q. CONTROL %s (%s): %llu calls, clean %llu differ, with the switch %llu differ\n", name, config.name, calls, clean, seen);
      if (broken_world ? clean == 0 : (clean != 0 || seen == 0)) ++control_failures;  // a broken world differs without any switch
    };
    const auto random_worlds = [&](uint64_t count, uint32_t items_cap) {
      ArCases worlds;
      for (uint64_t index = 0; index < count; ++index) {
        const ArSpec spec = ArRandomSpec(&random, items_cap);
        worlds.push_back({spec, standard_calls(spec)});
      }
      return worlds;
    };
    if (!streaming) control("scatter before allocate (C before B publishes)", configs[1], [&] { ar_module::controls.scatter_before_allocate = true; }, [&] { return random_worlds(6, 8000); });
    if (!streaming) control("scatter before allocate", configs[0], [&] { ar_module::controls.scatter_before_allocate = true; }, [&] { return random_worlds(6, 8000); });
    control("F on a worker with round-down MXCSR", configs[1], [&] { ar_module::controls.convert_in_scatter = true; }, [&] {
      ArCases worlds;
      for (const uint32_t texels_per_row : {0x01000001u, 0x01000003u, 0x7FFFFFFFu, 0x123456F7u}) {
        ArSpec spec = {.seed = seed_base + 90000 + texels_per_row, .items = 3500, .extra_entries = 8, .textures = 1, .current = 0, .frame = 1, .row_size = 3000,
                       .texels_per_row = texels_per_row, .need_min = 1, .need_max = 80, .flag_percent = 50, .share_percent = 30, .pre_state = 2, .record_cap = 0};
        spec.heights = {400, 1, 1, 1, 1, 1};
        worlds.push_back({spec, {{.update = false, .start = 0, .end = spec.items, .texture = 0}, {.update = true, .start = 0, .end = spec.items}}});
      }
      return worlds;
    });
    control("a vector that grows by itself (the allocator's log)", configs[1], [&] { ar_module::controls.own_vector_growth = true; }, [&] {
      ArCases worlds;
      for (const uint32_t capacity : {0u, 1u, 100u}) {
        ArSpec spec = {.seed = seed_base + 91000 + capacity, .items = 3000, .extra_entries = 8, .textures = 1, .current = 0, .frame = 1, .row_size = 100000, .texels_per_row = 7000,
                       .need_min = 1, .need_max = 3, .flag_percent = 0, .share_percent = 0, .pre_state = 2, .record_cap = capacity};
        spec.heights = {1000000, 1, 1, 1, 1, 1};
        worlds.push_back({spec, {{.update = false, .start = 0, .end = spec.items, .texture = 0}, {.update = true, .start = 0, .end = spec.items}}});
      }
      return worlds;
    });
    control("int64 room instead of u32 (an oversize item)", configs[1], [&] { ar_module::controls.wide_room = true; }, [&] {
      ArCases worlds;
      for (const uint32_t row_size : {20u, 100u, 700u}) {
        ArSpec spec = {.seed = seed_base + 92000 + row_size, .items = 4500, .extra_entries = 8, .textures = 1, .current = 0, .frame = 1, .row_size = row_size, .texels_per_row = 7001,
                       .need_min = 0, .need_max = std::max(2u, row_size / 3), .oversize_percent = 20, .flag_percent = 0, .share_percent = 0, .pre_state = 2, .record_cap = 0};
        spec.heights = {3000, 1, 1, 1, 1, 1};
        worlds.push_back({spec, {{.update = false, .start = 0, .end = spec.items, .texture = 0}, {.update = true, .start = 0, .end = spec.items}}});
      }
      return worlds;
    });
    if (!streaming) control("B's blocks in swapped order", configs[1], [&] { ar_module::controls.swap_allocate_blocks = true; }, [&] { return random_worlds(6, 12000); });
    control("no end <= count check (garbage past the count, ours gathers it, alone)", configs[0], [&] { ar_module::controls.no_count_check = true; }, [&] {
      ArCases worlds;
      for (const uint32_t count : {3000u, 2600u, 1700u}) {
        // The game stops on its height at 2700 items of 10 texels (rows of 100, 270 rows), before the garbage; the gather of the block that holds the count reads past it.
        ArSpec spec = {.seed = seed_base + 93000 + count, .items = count, .extra_entries = 2200, .textures = 1, .current = 0, .frame = 1, .row_size = 100, .texels_per_row = 7000,
                       .need_min = 10, .need_max = 10, .flag_percent = 0, .share_percent = 0, .pre_state = 2, .record_cap = 0};
        spec.heights = {270, 1, 1, 1, 1, 1};
        worlds.push_back({spec, {{.update = false, .start = 0, .end = count + 2000, .texture = 0}}});
      }
      return worlds;
    });
    control("P-alias broken on purpose (the records' buffer inside the animations)", configs[1], [&] {}, [&] {
      ArCases worlds;
      for (uint32_t index = 0; index < 6; ++index) {
        ArSpec spec = {.seed = seed_base + 94000 + index, .items = 4000, .extra_entries = 8, .textures = 1, .current = 0, .frame = 1, .row_size = 3000, .texels_per_row = 7000,
                       .need_min = 5, .need_max = 60, .flag_percent = 50, .share_percent = 95, .pre_state = 2, .record_cap = 40, .alias_records = true};
        spec.heights = {1000000, 1, 1, 1, 1, 1};
        worlds.push_back({spec, {{.update = false, .start = 0, .end = spec.items, .texture = 0}}});
      }
      return worlds;
    }, true);
    CHECK(control_failures == 0);
  }

  // 23r. Forward-order restore in Check: an instance twice in a call (in different rounds), and a game run that stops earlier than ours: the instance words past its stop must be the
  // original ones, which only the reverse-order restore leaves.
  if (selected("restore")) {
    apply(configs[0]);
    reset_switches();
    ArCases worlds;
    for (uint32_t index = 0; index < 6; ++index) {
      ArSpec spec = {.seed = seed_base + 95000 + index, .items = 9000, .extra_entries = 8, .textures = 1, .current = 0, .frame = 1, .row_size = 100, .texels_per_row = 7000,
                     .need_min = 10, .need_max = 10, .flag_percent = 0, .share_percent = 0, .duplicate_percent = 90, .pre_state = 2, .record_cap = 0};
      spec.heights = {100, 1, 1, 1, 1, 1};
      worlds.push_back({spec, {{.update = false, .start = 0, .end = 9000, .texture = 0, .height_shift_from = 1, .height_delta = static_cast<uint32_t>(-80),
                                .game_height_shift_from = 0, .records_free = true, .check_sentinel = true}}});
    }
    twattila::animation_rows::Update(Stage::CHECK);
    uint64_t clean = 0;
    for (const auto& [spec, calls] : worlds) {
      CHECK(ArBuild(context.get(), spec));
      for (const ArCall& call : calls) {
        ar_module::failed.store(false);
        twattila::animation_rows::Update(Stage::CHECK);
        clean += (compare(call).empty() ? 0 : 1);
      }
    }
    ar_module::controls.forward_restore = true;
    uint64_t seen = 0;
    for (const auto& [spec, calls] : worlds) {
      CHECK(ArBuild(context.get(), spec));
      for (const ArCall& call : calls) {
        ar_module::failed.store(false);
        twattila::animation_rows::Update(Stage::CHECK);
        seen += (compare(call).empty() ? 0 : 1);
      }
    }
    reset_switches();
    std::printf("23r. Check's restore: a game run 80 rows (of 100) shorter than ours with duplicate instances, reverse order: %llu differ; CONTROL forward order: %llu differ (must be > 0)\n", clean, seen);
    CHECK(clean == 0 && seen > 0);
  }

  // 23s. The switch loop: Off (the site out), Check and On, 200 switches while 4 threads run whole updates and fill calls in both copies (they are not the pool's owner: their calls
  // run alone, and a second thread in the module's ring is sent to the game's own fill); nothing differs, the row never failed, every switch happened.
  if (selected("switch")) {
    apply(configs[0]);
    reset_switches();
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> run_count = 0;
    std::mutex replay_mutex;
    std::vector<std::pair<ArSpec, ArCall>> replay;  // the calls that differed: run again alone, to tell a race of the threads from a difference of the code
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        auto own = std::make_unique<ArContext>();
        Random thread_random = {.state = 0xB3C0000ull + static_cast<uint64_t>(worker) * 0x9E3779B97F4Aull};
        while (!stop.load()) {
          ArSpec spec = ArRandomSpec(&thread_random, 6000);
          ArCall call = ArRandomCall(spec, &thread_random);
          sanitize(&spec, &call);
          if (!ArBuild(own.get(), spec)) continue;
          const uint8_t* const game_function = (call.update ? game + ANIMATION_UPDATE_RVA : game_fill);
          const uint8_t* const patched_function = (call.update ? patched + ANIMATION_UPDATE_RVA : stub);
          if (const std::string what = ArCompare(own.get(), game, game_function, patched, patched_function, call); !what.empty() && ++bad <= 5) {
            std::printf("   thread %d: %s: %s\n", worker, describe(spec, call).c_str(), what.c_str());
            const std::unique_lock lock(replay_mutex);
            replay.push_back({spec, call});
          }
          ++run_count;
        }
      });
    }
    int waited = 0;
    int never = 0;
    std::array<int, 3> reached = {};
    for (int switches = 0; switches < 200; ++switches) {
      const int mode = switches % 3;  // 0 off, 1 check, 2 on
      const Stage stage = (mode == 0 ? Stage::OFF : mode == 1 ? Stage::CHECK : Stage::ON);
      int tries = 0;
      do {
        set_stage(stage);
        ++tries;
      } while ((((ec::written & ec::WORKER_FILL_BIT) != 0) != (mode != 0)) && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      waited += tries > 1 ? 1 : 0;
      const bool reached_now = (((ec::written & ec::WORKER_FILL_BIT) != 0) == (mode != 0));
      never += reached_now ? 0 : 1;
      reached[mode] += reached_now ? 1 : 0;
    }
    set_stage(Stage::ON);
    stop = true;
    for (auto& thread : threads) thread.join();
    ec::worker_data->cut_active.store(0);
    for (const auto& [spec, call] : replay) {
      CHECK(ArBuild(context.get(), spec));
      const std::string again = compare(call);
      after_fault();
      std::printf("   the same call alone: %s\n", again.empty() ? "equal (a race between the test's threads)" : again.c_str());
    }
    std::printf("23s. 200 switches Off / Check / On (%d / %d / %d reached) while 4 threads made %llu comparisons of whole updates and fill calls: %llu wrong (%d writes waited for a thread inside a stub, %d switches never reached; the row %s)\n",
                reached[0], reached[1], reached[2], run_count.load(), bad.load(), waited, never, ar_module::failed.load() ? "FAILED" : "never failed");
    CHECK(bad.load() == 0 && never == 0 && run_count.load() > 0 && !ar_module::failed.load() && !ec::module_pinned);
    reset_switches();
  }
#else
  std::printf("23m. to 23s. (the fallbacks' paths, the controls, Check, the faults through ours and the switch loop need the real Dispatch: build with TWATTILA_WORKER_CONTROLS) skipped\n");
#endif


#if defined(TWATTILA_WORKER_CONTROLS)
  // ==== S5: the schedules (streaming and bulk), the ring, the pool's limits, the stalls and the re-entry. The cases run under the schedule of this pass (`streaming`). ====

  // Ends the process when a case hangs (a test that waits for ever blocks the whole suite): after `seconds` it runs `release` (a test switch that may be what hangs the call), and when
  // the case is not done after as long again, or at once when there is no release, it says so and ends the process with 3.
  struct Watchdog {
    std::atomic<bool> done = false;
    std::thread thread;
    Watchdog(const char* name, uint32_t seconds, void (*release)() = nullptr)
        : thread([this, name, seconds, release] {
            const auto waited = [&] {
              for (uint32_t tenth = 0; tenth < seconds * 10 && !done.load(); ++tenth) Sleep(100);
              return done.load();
            };
            if (waited()) return;
            if (release != nullptr) {
              std::printf("   %s: not done after %u s: the test switch that may hang it is released\n", name, seconds);
              release();
              if (waited()) return;
            }
            std::printf("   %s: HUNG for %u s: ending the process\n", name, seconds * (release != nullptr ? 2 : 1));
            std::fflush(stdout);
            std::_Exit(3);
          }) {}
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    ~Watchdog() {
      done = true;
      thread.join();
    }
  };
  namespace ar_api = twattila::animation_rows;
  constexpr uint32_t ROUND_CAP_ITEMS = ar_api::RING_SLOTS * ar_api::BLOCK_ITEMS;  // bulk's rounds are at most 64 blocks
  const auto round_up_blocks = [](uint32_t items) { return (items + ar_api::BLOCK_ITEMS - 1) / ar_api::BLOCK_ITEMS * ar_api::BLOCK_ITEMS; };
  // A world every S5 case can use: no early stop, so every item is finished.
  const auto plain_world = [&](uint64_t seed, uint32_t items, uint32_t duplicate_percent) {
    ArSpec spec = {.seed = seed, .items = items, .extra_entries = 8, .textures = 1, .current = 0, .frame = 3, .row_size = 6000, .texels_per_row = 7000, .need_min = 1, .need_max = 30,
                   .flag_percent = 50, .share_percent = 30, .duplicate_percent = duplicate_percent, .pre_state = static_cast<uint32_t>(seed % 3), .record_cap = 0};
    spec.heights = {4000, 1, 1, 1, 1, 1};
    return spec;
  };

  // 23t. The pool's limit 1..16 through the env (animation:K, the render thread counted): K - 1 workers asked for, the schedule of the pass in the call, and every block gathered once.
  if (selected("limits")) {
    const Watchdog watchdog("23t", 120);
    uint64_t wrong = 0;
    uint64_t calls = 0;
    const ArSpec spec = plain_world(seed_base + 100000, 12000, 10);  // 24 blocks
    for (uint32_t workers = 0; workers <= 15; ++workers) {
      set_workers(workers);
      reset_switches();
      ar_module::controls.min_items = 1;
      CHECK(ArBuild(context.get(), spec));
      for (const bool tweak : {false, true}) {
        ArResult game_result;
        ArResult patched_result;
        const ArCall call = {.update = false, .start = 0, .end = spec.items, .texture = 0, .tweak = tweak};
        const std::string what = compare(call, &game_result, &patched_result);
        after_fault();
        ++calls;
        const ArPaths& paths = patched_result.paths;
        const bool shape = paths.taken[static_cast<size_t>(ArPath::OURS)] == 1 && paths.workers == workers && paths.streaming == (streaming && workers != 0) && paths.items == spec.items
                           && paths.gathered_items == spec.items && paths.gathered_blocks == 24;
        if (!what.empty() || !shape) {
          ++wrong;
          std::printf("   limit %u: %s (workers asked %u, streaming %d, items %u, gathered %u in %u blocks)\n", workers + 1, what.empty() ? "the run is equal, the shape is not" : what.c_str(),
                      paths.workers, paths.streaming ? 1 : 0, paths.items, paths.gathered_items, paths.gathered_blocks);
        }
      }
    }
    std::printf("23t. %s: the env limit 1 .. 16 (0 .. 15 workers), %llu calls: workers asked for, the schedule and the 24 blocks gathered once each as they should be, equal to the game's: %llu wrong\n",
                streaming ? "stream" : "bulk", calls, wrong);
    CHECK(wrong == 0);
  }

  // 23u. The ring wraps (more than its 64 blocks): lists of 65 blocks, 70000, 98304 and 100000 items, from item 0 and from item 7, alone and with workers; every block is gathered once.
  if (selected("wrap")) {
    const Watchdog watchdog("23u", 300);
    for (const Config& config : {configs[0], configs[1], configs[2]}) {
      apply(config);
      reset_switches();
      uint64_t wrong = 0;
      uint64_t calls = 0;
      const ArStats before = ar_stats;
      for (const uint32_t items : {33280u, 70000u, 98304u, 100000u}) {
        const ArSpec spec = plain_world(seed_base + 110000 + items, items, 20);
        CHECK(ArBuild(context.get(), spec));
        for (const ArCall& call : {ArCall{.update = false, .start = 0, .end = items, .texture = 0}, ArCall{.update = true, .start = 0, .end = items},
                                   ArCall{.update = false, .start = 7, .end = items, .texture = 0, .tweak = true}}) {
          ArResult game_result;
          ArResult patched_result;
          const std::string what = compare(call, &game_result, &patched_result);
          after_fault();
          ++calls;
          const uint32_t total = items - call.start;
          const ArPaths& paths = patched_result.paths;
          const bool shape = paths.items == total && paths.gathered_items == total && paths.gathered_blocks == (total + ar_api::BLOCK_ITEMS - 1) / ar_api::BLOCK_ITEMS;
          if (!what.empty() || !shape) {
            ++wrong;
            std::printf("   wrap %u items from %u: %s (finished %u, gathered %u in %u blocks)\n", items, call.start, what.empty() ? "the run is equal, the shape is not" : what.c_str(), paths.items,
                        paths.gathered_items, paths.gathered_blocks);
          }
        }
      }
      std::printf("23u. %s, %s: the ring wraps (65 to 196 blocks), %llu calls, every block gathered once (%s): %llu wrong\n", streaming ? "stream" : "bulk", config.name, calls,
                  path_line(before).c_str(), wrong);
      CHECK(wrong == 0);
    }
  }

  // 23v. S-1 / S-2: a call that stops early reads little more than it finished. After a primer call that finished 300 items (bulk's first round is max(4096, the last call's items)) a call
  // of 120000 items that stops at 1500: bulk gathers at most its first round and the next; streaming at most the blocks up to B's, the look-ahead of 16 and one in flight; alone one block more.
  if (selected("bounds")) {
    const Watchdog watchdog("23v", 120);
    for (const Config& config : {configs[0], configs[1], configs[2]}) {
      apply(config);
      reset_switches();
      uint64_t wrong = 0;
      ArSpec prime = {.seed = seed_base + 120000, .items = 120000, .extra_entries = 8, .textures = 1, .current = 0, .frame = 3, .row_size = 1000, .texels_per_row = 7000, .need_min = 10,
                      .need_max = 10, .flag_percent = 0, .share_percent = 0, .duplicate_percent = 0, .pre_state = 0, .record_cap = 0};
      prime.heights = {3, 1, 1, 1, 1, 1};  // 100 items to a row: 300 items
      ArSpec target = prime;
      target.heights = {15, 1, 1, 1, 1, 1};  // 1500 items
      const ArCall call = {.update = false, .start = 0, .end = 120000, .texture = 0};
      CHECK(ArBuild(context.get(), prime));
      wrong += compare(call).empty() ? 0 : 1;
      after_fault();
      const uint32_t last_before = ar_module::last_items;
      CHECK(ArBuild(context.get(), target));
      ArResult game_result;
      ArResult patched_result;
      wrong += compare(call, &game_result, &patched_result).empty() ? 0 : 1;
      after_fault();
      const ArPaths& paths = patched_result.paths;
      const uint32_t finished = paths.items;
      uint32_t bound = 0;
      if (paths.workers == 0) {
        bound = round_up_blocks(finished) + ar_api::BLOCK_ITEMS;
      } else if (streaming) {
        bound = round_up_blocks(finished) + (ar_api::LOOKAHEAD_BLOCKS + 2) * ar_api::BLOCK_ITEMS;
      } else {
        const uint32_t first_round = std::min(ROUND_CAP_ITEMS, round_up_blocks(std::max(ar_api::FIRST_ROUND_ITEMS, last_before)));
        bound = first_round + std::min(ROUND_CAP_ITEMS, 2 * first_round) + ar_api::BLOCK_ITEMS;
      }
      const bool inside = finished == 1500 && last_before == 300 && paths.gathered_items >= finished && paths.gathered_items <= bound;
      wrong += inside ? 0 : 1;
      std::printf("23v. %s, %s: a call of 120000 items that stops at %u (the last call finished %u) gathered %u items in %u blocks, at most %u allowed: %s\n", streaming ? "stream" : "bulk", config.name,
                  finished, last_before, paths.gathered_items, paths.gathered_blocks, bound, wrong == 0 ? "inside" : "WRONG");
      CHECK(wrong == 0);
    }
  }

  // 23w. The stalls (S-2, S-3): a gather unit that waits (the window stalls: B waits or gathers the block itself), B that waits before it allocates a block (C and A run ahead), a scatter unit
  // that lands late, a scatter claim that pauses between its check and its CAS (it must never scatter a block B has not published); equal in each, on a plain world and on one in ordered mode.
  if (selected("delays")) {
    const Watchdog watchdog("23w", 240);
    struct Delay {
      const char* name;
      std::function<void()> set;
    };
    const std::vector<Delay> delays = {
        {"gather of block 0 waits 25 ms", [] { ar_module::controls.gather_delay_block = 0; ar_module::controls.gather_delay_ms = 25; }},
        {"gather of block 3 waits 25 ms", [] { ar_module::controls.gather_delay_block = 3; ar_module::controls.gather_delay_ms = 25; }},
        {"gather of block 20 waits 25 ms", [] { ar_module::controls.gather_delay_block = 20; ar_module::controls.gather_delay_ms = 25; }},
        {"gather of block 38 waits 25 ms", [] { ar_module::controls.gather_delay_block = 38; ar_module::controls.gather_delay_ms = 25; }},
        {"B waits 25 ms before block 0", [] { ar_module::controls.allocate_delay_block = 0; ar_module::controls.allocate_delay_ms = 25; }},
        {"B waits 25 ms before block 2", [] { ar_module::controls.allocate_delay_block = 2; ar_module::controls.allocate_delay_ms = 25; }},
        {"B waits 25 ms before block 17", [] { ar_module::controls.allocate_delay_block = 17; ar_module::controls.allocate_delay_ms = 25; }},
        {"B waits 25 ms before block 38", [] { ar_module::controls.allocate_delay_block = 38; ar_module::controls.allocate_delay_ms = 25; }},
        {"scatter of block 5 lands 20 ms late", [] { ar_module::controls.scatter_delay_block = 5; ar_module::controls.scatter_delay_ms = 20; }},
        {"scatter of block 30 lands 20 ms late", [] { ar_module::controls.scatter_delay_block = 30; ar_module::controls.scatter_delay_ms = 20; }},
        {"scatter claim pauses 1 spin", [] { ar_module::controls.claim_gap_spins = 1; }},
        {"scatter claim pauses 100 spins", [] { ar_module::controls.claim_gap_spins = 100; }},
        {"scatter claim pauses 100000 spins", [] { ar_module::controls.claim_gap_spins = 100000; }},
    };
    ArCases worlds;
    {
      const ArSpec plain = plain_world(seed_base + 130000, 20000, 20);  // 40 blocks
      worlds.push_back({plain, {{.update = false, .start = 0, .end = plain.items, .texture = 0}}});
      ArSpec ordered = plain_world(seed_base + 130001, 20000, 90);
      ordered.texels_per_row = 0xFFFFFFFFu;
      worlds.push_back({ordered, {{.update = false, .start = 0, .end = ordered.items, .texture = 0}}});
    }
    for (const Config& config : {configs[1], configs[2]}) {
      uint64_t wrong = 0;
      for (const Delay& delay : delays) {
        apply(config);
        reset_switches();
        delay.set();
        const uint64_t differ = count_differences(worlds);
        reset_switches();
        if (differ != 0) {
          ++wrong;
          std::printf("   %s: %llu of the 2 worlds differ\n", delay.name, differ);
        }
      }
      std::printf("23w. %s, %s: %zu stalls (gather, B, scatter, claim gap) x a plain and an ordered-mode world of 40 blocks: %llu stalls made a difference\n", streaming ? "stream" : "bulk", config.name,
                  delays.size(), wrong);
      CHECK(wrong == 0);
    }
    // The claim race under load: 15 workers, a long gap, random worlds.
    apply(configs[2]);
    reset_switches();
    ar_module::controls.claim_gap_spins = 3000;
    ArCases random_cases;
    for (uint64_t index = 0; index < (quick ? 12u : 40u); ++index) {
      ArSpec spec = ArRandomSpec(&random, 30000);
      spec.null_instance_at = spec.null_animation_at = ~0u;
      random_cases.push_back({spec, standard_calls(spec)});
    }
    const uint64_t differ = count_differences(random_cases);
    reset_switches();
    std::printf("23w. %s: the scatter claim pauses 3000 spins, 15 workers, %zu random worlds: %llu calls differ\n", streaming ? "stream" : "bulk", random_cases.size(), differ);
    CHECK(differ == 0);
  }

  // 23x. S-5: a call of Dispatch inside a call (the render thread, from the hook that runs after each block B allocated) finds the ring taken and runs the game's own fill, whose result on
  // its own world is the game's; and a second thread calling while a slow call of ours holds the ring gets the game's path too, whatever the order.
  if (selected("reentry")) {
    const Watchdog watchdog("23x", 120);
    struct Reentry {
      ArContext* nested;
      const ArResult* expected;
      const std::vector<uint8_t>* expected_bytes;
      ArCall call;
      const uint8_t* image;
      const void* function;
      uint32_t calls;
      uint32_t wrong;
    };
    for (const Config& config : {configs[0], configs[1], configs[2]}) {
      apply(config);
      reset_switches();
      const ArSpec outer_spec = plain_world(seed_base + 140000, 15000, 20);
      const ArSpec nested_spec = plain_world(seed_base + 140001, 6000, 20);
      auto nested = std::make_unique<ArContext>();
      CHECK(ArBuild(nested.get(), nested_spec));
      const ArCall nested_call = {.update = false, .start = 0, .end = nested_spec.items, .texture = 0};
      ArResult expected;
      ArRunSide(nested.get(), game, game_fill, nested_call, ~0u, &expected);
      const std::vector<uint8_t> expected_bytes(nested->world.base, nested->world.base + nested->world.bytes);
      Reentry state = {nested.get(), &expected, &expected_bytes, nested_call, patched, stub, 0, 0};
      ar_module::controls.after_block_context = &state;
      ar_module::controls.after_block = [](uint32_t block, void* raw) {
        auto* reentry = static_cast<Reentry*>(raw);
        if (block % 3 != 0) return;
        ArWorld* outer = ar_current;
        ArResult result;
        ArRunSide(reentry->nested, reentry->image, reentry->function, reentry->call, ~0u, &result);
        ar_current = outer;
        const ArWorld& world = reentry->nested->world;
        const bool same = (result.eax & 0xFF) == (reentry->expected->eax & 0xFF) && result.row == reentry->expected->row && result.out == reentry->expected->out
                          && result.arena_used == reentry->expected->arena_used && result.log == reentry->expected->log && !result.fault.happened
                          && std::memcmp(world.base, reentry->expected_bytes->data(), world.bytes) == 0;
        ++reentry->calls;
        reentry->wrong += same ? 0 : 1;
      };
      CHECK(ArBuild(context.get(), outer_spec));
      const ArStats before = ar_stats;
      const std::string what = compare({.update = false, .start = 0, .end = outer_spec.items, .texture = 0});
      after_fault();
      const uint64_t busy = ar_stats.paths[static_cast<size_t>(ArPath::RING_BUSY)] - before.paths[static_cast<size_t>(ArPath::RING_BUSY)];
      const uint64_t ours = ar_stats.paths[static_cast<size_t>(ArPath::OURS)] - before.paths[static_cast<size_t>(ArPath::OURS)];
      ar_module::controls.after_block = nullptr;
      ar_module::controls.after_block_context = nullptr;
      std::printf("23x. %s, %s: Dispatch inside Dispatch (the hook after every 3rd of 30 blocks): %u nested calls, %llu took the ring-busy path (the outer call %llu x ours), the nested runs wrong in %u, the outer run %s\n",
                  streaming ? "stream" : "bulk", config.name, state.calls, busy, ours, state.wrong, what.empty() ? "equal" : what.c_str());
      CHECK(what.empty() && state.calls == 10 && busy == state.calls && ours == 1 && state.wrong == 0);
    }
    // A second thread beside a slow call: its calls meet the ring held or free, and are equal either way.
    for (const Config& config : {configs[0], configs[1]}) {
      apply(config);
      reset_switches();
      ar_module::controls.allocate_delay_block = 2;
      ar_module::controls.allocate_delay_ms = 40;
      std::atomic<bool> stop = false;
      std::atomic<uint64_t> thread_wrong = 0;
      std::atomic<uint64_t> thread_calls = 0;
      const std::array<uint32_t, 12> busy_before = ArReadPaths();
      std::thread other([&] {
        auto own = std::make_unique<ArContext>();
        Random thread_random = {.state = 0x5EC0D7ull};
        while (!stop.load()) {
          ArSpec spec = ArRandomSpec(&thread_random, 8000);
          ArCall call = ArRandomCall(spec, &thread_random);
          sanitize(&spec, &call);
          if (!ArBuild(own.get(), spec)) continue;
          const uint8_t* const game_function = (call.update ? game + ANIMATION_UPDATE_RVA : game_fill);
          const uint8_t* const patched_function = (call.update ? patched + ANIMATION_UPDATE_RVA : stub);
          thread_wrong += ArCompare(own.get(), game, game_function, patched, patched_function, call).empty() ? 0 : 1;
          ++thread_calls;
        }
      });
      uint64_t wrong = 0;
      const ArSpec outer_spec = plain_world(seed_base + 140100, 12000, 20);
      CHECK(ArBuild(context.get(), outer_spec));
      for (int call_index = 0; call_index < 6; ++call_index) {
        wrong += compare({.update = false, .start = 0, .end = outer_spec.items, .texture = 0}).empty() ? 0 : 1;
        after_fault();
      }
      stop = true;
      other.join();
      const uint32_t busy = ArReadPaths()[static_cast<size_t>(ArPath::RING_BUSY)] - busy_before[static_cast<size_t>(ArPath::RING_BUSY)];
      reset_switches();
      std::printf("23x. %s, %s: a second thread made %llu calls beside 6 slow calls of ours: %llu of its calls and %llu of ours differ, %u took the ring-busy path\n", streaming ? "stream" : "bulk",
                  config.name, thread_calls.load(), thread_wrong.load(), wrong, busy);
      CHECK(wrong == 0 && thread_wrong.load() == 0 && thread_calls.load() > 0 && busy > 0);
    }
  }

  // 23y. The schedule through the env: bulk and stream parse, anything else (and none) is stream, the value is read again only when the generation changes, and the schedule is in the call.
  if (selected("schedule")) {
    const Watchdog watchdog("23y", 120);
    apply(configs[1]);
    reset_switches();
    const ArSpec spec = plain_world(seed_base + 150000, 12000, 10);
    const auto set_env = [&](const char* text) {
      SetEnvironmentVariableA("RENODX_TW_ANIMATION_SCHEDULE", text);
      twattila::animation_rows::Update(Stage::ON, ++schedule_generation);
    };
    struct Value {
      const char* text;
      ar_api::Schedule expected;
    };
    uint64_t wrong = 0;
    for (const Value& value : {Value{"bulk", ar_api::Schedule::BULK}, Value{"stream", ar_api::Schedule::STREAM}, Value{nullptr, ar_api::Schedule::STREAM},
                               Value{"nonsense", ar_api::Schedule::STREAM}, Value{"BULK", ar_api::Schedule::STREAM}, Value{"bulk", ar_api::Schedule::BULK}}) {
      set_env(value.text);
      CHECK(ArBuild(context.get(), spec));
      ArResult game_result;
      ArResult patched_result;
      const std::string what = compare({.update = false, .start = 0, .end = spec.items, .texture = 0}, &game_result, &patched_result);
      after_fault();
      const bool same_stream = patched_result.paths.workers != 0 && patched_result.paths.streaming == (value.expected == ar_api::Schedule::STREAM);
      const bool parsed = ar_module::schedule.load() == value.expected;
      if (!what.empty() || !same_stream || !parsed) {
        ++wrong;
        std::printf("   env \"%s\": %s (schedule %d, call streaming %d)\n", value.text == nullptr ? "(unset)" : value.text, what.empty() ? "equal" : what.c_str(),
                    static_cast<int>(ar_module::schedule.load()), patched_result.paths.streaming ? 1 : 0);
      }
    }
    // The same generation again: the env is not read.
    SetEnvironmentVariableA("RENODX_TW_ANIMATION_SCHEDULE", "stream");
    twattila::animation_rows::Update(Stage::ON, schedule_generation);
    wrong += (ar_module::schedule.load() == ar_api::Schedule::BULK) ? 0 : 1;
    set_env(streaming ? "stream" : "bulk");
    CHECK(ar_module::schedule.load() == (streaming ? ar_api::Schedule::STREAM : ar_api::Schedule::BULK));
    std::printf("23y. the env schedule: bulk, stream, unset, an unparsable value and another case, the generation unchanged: %llu wrong\n", wrong);
    CHECK(wrong == 0);
  }

  // 23z. The streaming control: a scatter unit that takes a gathered block before B has published it (stale texel words) must make the comparison fail (the call may also hang: the watchdog
  // releases the switch after 20 s and ends the process after 40).
  if (selected("ignores") && streaming) {
    static std::atomic<bool> released = false;
    apply(configs[1]);
    reset_switches();
    ArCases worlds;
    for (uint32_t index = 0; index < 6; ++index) {
      const ArSpec spec = plain_world(seed_base + 160000 + index, 20000, 20);
      worlds.push_back({spec, {{.update = false, .start = 0, .end = spec.items, .texture = 0}}});
    }
    const uint64_t clean = count_differences(worlds);
    released = false;
    uint64_t seen = 0;
    {
      const Watchdog watchdog("23z", 20, [] {
        released = true;
        ar_module::controls.scatter_ignores_publish = false;
      });
      ar_module::controls.scatter_ignores_publish = true;
      seen = count_differences(worlds);
    }
    reset_switches();
    std::printf("23z. CONTROL a scatter unit that ignores B's publish (stream, 3 workers): clean %llu differ, with the switch %llu differ%s\n", clean, seen, released.load() ? " (the call hung and was released)" : "");
    CHECK(clean == 0 && (seen > 0 || released.load()));
  }
#endif
  set_workers(0);
  set_stage(Stage::OFF);
  CHECK((ec::written & ec::WORKER_SITES) == 0 && ec::worker_data->fill_slot.load() == 0 && ec::worker_data->cut_active.load() == 0 && !ec::module_pinned);
  ArSetTweaks(game, patched, false);
  twattila::worker_pool::Update({.threads = 4, .in_use = false});
  SetEnvironmentVariableA("RENODX_TW_WORKER_LIMITS", nullptr);
  SetEnvironmentVariableA("RENODX_TW_ANIMATION_SCHEDULE", nullptr);
  return 0;
}

// The same cases under each schedule (AR_SCHEDULE=bulk or stream picks one; without the module's switches the schedule does not matter: once).
int RunAnimationRowsExactTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  const char* const only = std::getenv("AR_SCHEDULE");
#if defined(TWATTILA_WORKER_CONTROLS)
  for (const bool streaming : {false, true}) {
    if (only != nullptr && std::strcmp(only, streaming ? "stream" : "bulk") != 0) continue;
    if (const int failed = RunAnimationRowsExactFor(build, game, patched, quick, streaming); failed != 0) return failed;
  }
  return 0;
#else
  return RunAnimationRowsExactFor(build, game, patched, quick, only != nullptr && std::strcmp(only, "stream") == 0);
#endif
}

// ==== S4/S5 exactness part end (b3t_) ====

int RunAnimationRowsTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  using twattila::animation_rows::Stage;
  const uint32_t fill_call_rva = build.code->fill_call_rva;
  const uint32_t fill_rva = build.code->fill_rva;
  CHECK((fill_call_rva != 0) == (build.code->fill_rva != 0));
  const auto update_rows = [&](bool fill, bool sweep) {
    twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 1.f, 0,
                                  {.animation = (fill ? Stage::ON : Stage::OFF), .pending_sweep_trace = sweep});
  };
  const auto fill_slot = [] { return ec::worker_data->fill_slot.load(); };
  const auto sweep_slot = [] { return ec::worker_data->sweep_slot.load(); };
  const uint32_t dispatch = Address(reinterpret_cast<const void*>(&twattila::animation_rows::Dispatch));
  const uint32_t arm = Address(reinterpret_cast<const void*>(&twattila::pending_sweep::Arm));
  if (build.code->sweep_rva == 0) {
    std::printf("23. worker sites: this build has neither the animation fill call nor the sweep (skipped)\n");
    CHECK((ec::found & ec::WORKER_SITES) == 0);
    return 0;
  }
  update_rows(false, false);
  CHECK((ec::written & ec::WORKER_SITES) == 0 && fill_slot() == 0 && sweep_slot() == 0);
  const bool has_fill = fill_call_rva != 0;

  // 23a. The sites and stubs as Prepare made them.
  CHECK(reinterpret_cast<uintptr_t>(patched) != PREFERRED_BASE && reinterpret_cast<uintptr_t>(game) != PREFERRED_BASE);  // the copies are relocated
  CHECK(ec::worker_data != nullptr && ec::worker_cut.begin == ec::page + ec::WORKER_FILL_PAGE && ec::worker_cut.end == ec::page + ec::PAGE_SIZE
        && ec::worker_cut.active == &ec::worker_data->cut_active);
  MEMORY_BASIC_INFORMATION code_info = {};
  MEMORY_BASIC_INFORMATION data_info = {};
  CHECK(VirtualQuery(ec::page, &code_info, sizeof(code_info)) == sizeof(code_info) && VirtualQuery(ec::page + ec::PAGE_SIZE, &data_info, sizeof(data_info)) == sizeof(data_info));
  CHECK(code_info.Protect == PAGE_EXECUTE_READ && code_info.RegionSize == ec::PAGE_SIZE && data_info.Protect == PAGE_READWRITE && data_info.RegionSize == ec::PAGE_SIZE
        && code_info.AllocationBase == ec::page && data_info.AllocationBase == ec::page && reinterpret_cast<uint8_t*>(ec::worker_data) == ec::page + ec::PAGE_SIZE);
  CHECK(::Address(&ec::worker_data->fill_slot) == ::Address(ec::worker_data) + 0x40 && ::Address(&ec::worker_data->sweep_slot) == ::Address(ec::worker_data) + 0x44);
  const auto no_sentinel = [](const uint8_t* stub, size_t size) {
    for (size_t at = 0; at + 3 <= size; ++at) {
      if (stub[at] == 0x5A && stub[at + 1] == 0x5A && stub[at + 2] == 0x5A) return false;
    }
    return true;
  };
  // A stub's absolute operands at their offsets, its last jump at `jump_at` leads to `callee`.
  const auto stub_holds = [&](const uint8_t* stub, size_t size, std::span<const std::pair<size_t, const void*>> words, size_t jump_at, const uint8_t* callee) {
    for (const auto& [at, expected] : words) {
      if (Read32(stub + at) != Address(expected)) return false;
    }
    return no_sentinel(stub, size) && stub[jump_at - 1] == 0xE9 && Address(stub + jump_at + 4) + Read32(stub + jump_at) == Address(callee);
  };
  // The stubs of the page now (Prepare again, in 23d, makes another).
  const auto fill_stub = [] { return static_cast<const uint8_t*>(ec::page + ec::WORKER_FILL_PAGE); };
  const auto sweep_stub = [] { return static_cast<const uint8_t*>(ec::page + ec::WORKER_SWEEP_PAGE); };
  CHECK(ec::sites[ec::WORKER_FILL_INDEX].opcode == 0xE8 && ec::sites[ec::WORKER_SWEEP_INDEX].opcode == 0xE9);
  if (has_fill) {
    const uint8_t* const site = patched + fill_call_rva + ec::FILL_CALL_AT;
    CHECK(ec::sites[ec::WORKER_FILL_INDEX].at == site && (ec::found >> ec::WORKER_FILL_INDEX & 1) != 0);
    CHECK(site[0] == 0xE8 && Address(site + 5) + Read32(site + 1) == Address(patched + fill_rva) && std::memcmp(site, game + fill_call_rva + ec::FILL_CALL_AT, 5) == 0);
    CHECK(std::memcmp(ec::sites[ec::WORKER_FILL_INDEX].game.data(), site, 5) == 0);
    const std::array<std::pair<size_t, const void*>, 5> words = {{{1, &ec::worker_data->fill_slot}, {12, &ec::worker_data->cut_active}, {17, &ec::worker_data->fill_slot},
                                                                  {58, &ec::worker_data->cut_active}, {68, &ec::worker_data->cut_active}}};
    CHECK(stub_holds(fill_stub(), ec::FILL_CALL_STUB.size(), words, 73, patched + fill_rva));
    CHECK(std::memcmp(fill_stub() + 25, ec::FILL_CALL_STUB.data() + 25, 30) == 0 && std::memcmp(fill_stub() + 62, "\xC2\x1C\x00", 3) == 0);
  }
  CHECK(ec::sites[ec::WORKER_SWEEP_INDEX].at == patched + build.code->sweep_rva + ec::SWEEP_ENTRY_AT && (ec::found >> ec::WORKER_SWEEP_INDEX & 1) != 0);
  {
    constexpr std::array<uint8_t, 5> ENTRY = {0x8B, 0x46, 0x18, 0x53, 0x8D};  // mov eax, [esi + 0x18]; push ebx; the first byte of lea ebx, [eax - 1]
    const uint8_t* const site = patched + build.code->sweep_rva + ec::SWEEP_ENTRY_AT;
    CHECK(std::memcmp(site, ENTRY.data(), 5) == 0 && std::memcmp(game + build.code->sweep_rva + ec::SWEEP_ENTRY_AT, ENTRY.data(), 5) == 0);
    CHECK(site[5] == 0x58 && site[6] == 0xFF && site[7] == 0x3B && site[8] == 0xD8);  // the rest of the lea, then the cmp the stub goes back to
    CHECK(ec::SWEEP_ENTRY_RESUME == ec::SWEEP_ENTRY_AT + 7);
    const std::array<std::pair<size_t, const void*>, 4> words = {{{1, &ec::worker_data->sweep_slot}, {12, &ec::worker_data->cut_active}, {17, &ec::worker_data->sweep_slot},
                                                                  {32, &ec::worker_data->cut_active}}};
    CHECK(stub_holds(sweep_stub(), ec::SWEEP_ENTRY_STUB.size(), words, 44, patched + build.code->sweep_rva + ec::SWEEP_ENTRY_RESUME));
    CHECK(std::memcmp(sweep_stub() + 36, ENTRY.data(), 3) == 0 && sweep_stub()[39] == 0x53 && std::memcmp(sweep_stub() + 40, "\x8D\x58\xFF", 3) == 0);
  }
  update_rows(has_fill, true);
  CHECK((ec::written & ec::WORKER_SITES) == (has_fill ? ec::WORKER_SITES : ec::WORKER_SWEEP_BIT));
  CHECK(fill_slot() == (has_fill ? dispatch : 0) && sweep_slot() == arm);
  if (has_fill) {
    const uint8_t* const site = patched + fill_call_rva + ec::FILL_CALL_AT;
    CHECK(site[0] == 0xE8 && Address(site + 5) + Read32(site + 1) == Address(fill_stub()));
  }
  {
    const uint8_t* const site = patched + build.code->sweep_rva + ec::SWEEP_ENTRY_AT;
    CHECK(site[0] == 0xE9 && Address(site + 5) + Read32(site + 1) == Address(sweep_stub()));
  }
  std::printf("23. worker sites: found, written and read-execute / read-write pages as the design has them (the copies at %p and %p, not the DLL's own base)\n",
              static_cast<void*>(game), static_cast<void*>(patched));
  if (!has_fill) {
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == 0 && sweep_slot() == 0);
    std::printf("   (this build has no animation fill call: only the sweep entry is looked for)\n");
    return 0;
  }

  // The game's fill is the fake in both copies from here (after Prepare found it); the harness puts the bytes back at the end.
  std::array<std::array<uint8_t, 5>, 2> saved_fill = {};
  std::memcpy(saved_fill[0].data(), game + fill_rva, 5);
  std::memcpy(saved_fill[1].data(), patched + fill_rva, 5);
  JumpTo(game + fill_rva, reinterpret_cast<const void*>(&FakeFill));
  JumpTo(patched + fill_rva, reinterpret_cast<const void*>(&FakeFill));
  const auto restore_fill = [&] {
    std::memcpy(game + fill_rva, saved_fill[0].data(), 5);
    std::memcpy(patched + fill_rva, saved_fill[1].data(), 5);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  };
  const auto reprepare = [&] {  // the sites found again (the fill's head must be the game's for that)
    restore_fill();
    for (ec::Site& site : ec::sites) site.at = nullptr;
    ec::prepared = false;
    ec::written = 0;
    ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
    JumpTo(game + fill_rva, reinterpret_cast<const void*>(&FakeFill));
    JumpTo(patched + fill_rva, reinterpret_cast<const void*>(&FakeFill));
  };
  const auto foreign = [&](size_t index, const std::array<uint8_t, ec::SITE_SIZE>& bytes) {
    DWORD protection;
    VirtualProtect(ec::sites[index].at, ec::SITE_SIZE, PAGE_EXECUTE_READWRITE, &protection);
    std::memcpy(ec::sites[index].at, bytes.data(), ec::SITE_SIZE);
    VirtualProtect(ec::sites[index].at, ec::SITE_SIZE, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  };
  auto world = std::make_unique<AnimationWorld>();
  Random random = {.state = 0x23A1D00D5EEDull};
  for (uint8_t& byte : world->cache) byte = static_cast<uint8_t>(random.Next());

  // 23b. The stub alone against the fill's own call, then whole updates.
  {
    const auto stub_function = reinterpret_cast<FillFunction>(const_cast<uint8_t*>(fill_stub()));
    const auto game_function = reinterpret_cast<FillFunction>(game + fill_rva);
    uint64_t bad = 0;
    const int rounds = (quick ? 2000 : 20000);
    for (int round = 0; round < rounds; ++round) {
      const AnimationSpec spec = AnimationRandom(&random);
      const bool on = (round & 1) != 0;
      ec::worker_data->fill_slot.store(on ? dispatch : 0);  // a slot off runs the game's call from the stub; on, through Dispatch
      std::array<AnimationRun, 2> runs;
      for (int side = 0; side < 2; ++side) {
        AnimationWorld saved = *world;
        fill_log.clear();
        fill_script = {.seed = spec.seed, .calls = 0, .fail_at = spec.fail_at};
        world->row = spec.start;
        uint32_t out = 0x77;
        const FillFunction function = (side == 0 ? game_function : stub_function);
        runs[side].result = function(world->cache, nullptr, world->device, spec.force, world->list, spec.current, &world->row, spec.end, &out);
        runs[side].world = *world;
        runs[side].log = fill_log;
        runs[side].world.padding[0] = out;
        *world = saved;
      }
      if (runs[0].result != runs[1].result || !(runs[0].world == runs[1].world) || runs[0].log != runs[1].log) {
        if (++bad <= 5) std::printf("   round %d: the stub differs from the fill's own call (result %08X / %08X)\n", round, runs[0].result, runs[1].result);
      }
    }
    CHECK(bad == 0);
    ec::worker_data->fill_slot.store(dispatch);
    std::printf("   the stub against the game's call, %d random calls with the slot on and off: results (eax whole), world, out and the fill's log equal\n", rounds);
    uint64_t wrong = 0;
    const int updates = (quick ? 4000 : 40000);
    for (int round = 0; round < updates; ++round) {
      const AnimationSpec spec = AnimationRandom(&random);
      if (const char* what = AnimationBoth(game, patched, world.get(), spec); what[0] != 0 && ++wrong <= 5) {
        std::printf("   update %d (%u textures, rows %u to %u, rotate %d): %s differs\n", round, spec.textures, spec.start, spec.end, spec.rotate ? 1 : 0, what);
      }
    }
    CHECK(wrong == 0);
    std::printf("   %d whole updates in both copies (rotating tweak on and off, a failing fill, any start and end): the result, the world and the fill's calls equal\n", updates);
  }

  // 23c. The cut rule with a parked thread at each byte of each stub.
  {
    ParkedThread parked;
    HANDLE thread = parked.Open();
    CHECK(thread != nullptr);
    int wrong = 0;
    int checked = 0;
    // Eip at `at`: the rows go off; where the thread counts as inside the stubs nothing may come out.
    const auto try_off = [&](const uint8_t* at, bool inside) {
      wrong += (SuspendThread(thread) == static_cast<DWORD>(-1)) ? 1 : 0;
      CONTEXT saved = {.ContextFlags = CONTEXT_CONTROL};
      wrong += (GetThreadContext(thread, &saved) == FALSE) ? 1 : 0;
      CONTEXT moved = saved;
      moved.Eip = Address(at);
      wrong += (SetThreadContext(thread, &moved) == FALSE) ? 1 : 0;
      ec::failed_writes = 0;
      update_rows(false, false);
      const bool still_in = (ec::written & ec::WORKER_SITES) == ec::WORKER_SITES;
      wrong += (still_in != inside || (inside && (fill_slot() != 0 || sweep_slot() != 0))) ? 1 : 0;
      wrong += (SetThreadContext(thread, &saved) == FALSE) ? 1 : 0;
      ResumeThread(thread);
      ++checked;
      ec::failed_writes = 0;
      update_rows(true, true);  // back on
      wrong += ((ec::written & ec::WORKER_SITES) != ec::WORKER_SITES || fill_slot() != dispatch || sweep_slot() != arm) ? 1 : 0;
    };
    for (size_t offset = 0; offset < ec::FILL_CALL_STUB.size(); ++offset) try_off(fill_stub() + offset, true);
    for (size_t offset = 0; offset < ec::SWEEP_ENTRY_STUB.size(); ++offset) try_off(sweep_stub() + offset, true);
    try_off(fill_stub() - 1, false);           // the byte before the stubs: another stub's
    try_off(ec::page + ec::PAGE_SIZE, false);  // the data page's first byte
    parked.Stop();
    CloseHandle(thread);
    CHECK(wrong == 0);
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == 0 && fill_slot() == 0 && sweep_slot() == 0);
    update_rows(true, true);
    CHECK((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES && fill_slot() == dispatch);
    std::printf("   a parked thread at each of the %d bytes of the two stubs: nothing comes out, the slots read 0 meanwhile; just outside, both come out\n", checked - 2);
  }

  // 23c (2). A thread inside a function the stub called (the count above 0): the sites stay in, then come out.
  {
    ec::worker_data->fill_slot.store(Address(reinterpret_cast<const void*>(&SlowDispatch)));
    slow_entered = false;
    slow_release = false;
    std::atomic<bool> caller_ok = false;
    std::thread caller([&] {
      auto own = std::make_unique<AnimationWorld>();
      const AnimationSpec spec = {.textures = 1, .current = 0, .start = 0, .end = 9, .force = 0, .seed = 3, .fail_at = ~0u, .rotate = false};
      const AnimationRun run = AnimationUpdate(patched, own.get(), spec);
      caller_ok = (run.result == 1 && !run.log.empty());
    });
    while (!slow_entered.load()) Sleep(1);
    CHECK(ec::worker_data->cut_active.load() == 1);
    ec::failed_writes = 0;
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES && fill_slot() == 0 && sweep_slot() == 0);
    // The same with the sweep's row alone off: the fill's site is wanted, nothing changes there; the sweep entry's stub is cut too.
    ec::failed_writes = 0;
    update_rows(true, false);
    CHECK((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES && fill_slot() == dispatch);  // asked again: its word is back (the slow thread is past its test)
    ec::failed_writes = 0;
    update_rows(false, false);
    slow_release = true;
    caller.join();
    CHECK(caller_ok.load() && ec::worker_data->cut_active.load() == 0);
    ec::failed_writes = 0;
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == 0 && fill_slot() == 0 && sweep_slot() == 0 && !ec::module_pinned);
    update_rows(true, true);
    CHECK((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES && fill_slot() == dispatch && sweep_slot() == arm);
    std::printf("   a thread inside the called function (count 1): both sites stay in, the slots read 0 or as asked, and both come out once it is gone\n");
  }

  // 23d. The pin: another mod's bytes over a site on its way out, a thread kept in the stub, an Uninstall with the count stuck above 0.
  {
    const std::array<uint8_t, ec::SITE_SIZE> hook = {0xE9, 0x10, 0x20, 0x30, 0x40};
    foreign(ec::WORKER_FILL_INDEX, hook);
    ec::failed_writes = 0;
    update_rows(false, false);
    CHECK(ec::module_pinned && (ec::written & ec::WORKER_SITES) == 0 && (ec::found & ec::WORKER_FILL_BIT) == 0
          && std::memcmp(ec::sites[ec::WORKER_FILL_INDEX].at, hook.data(), 5) == 0 && fill_slot() == 0);
    ec::module_pinned = false;
    foreign(ec::WORKER_FILL_INDEX, ec::sites[ec::WORKER_FILL_INDEX].game);
    reprepare();
    update_rows(true, true);
    CHECK((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES);

    // A thread kept in the stub for MAX_FAILED_WRITES presents: pinned, the sites still in.
    ParkedThread parked;
    HANDLE thread = parked.Open();
    CHECK(thread != nullptr && SuspendThread(thread) != static_cast<DWORD>(-1));
    CONTEXT saved = {.ContextFlags = CONTEXT_CONTROL};
    CHECK(GetThreadContext(thread, &saved) != FALSE);
    CONTEXT moved = saved;
    moved.Eip = Address(fill_stub() + 20);
    CHECK(SetThreadContext(thread, &moved) != FALSE);
    ec::failed_writes = 0;
    for (uint32_t present = 0; present < ec::MAX_FAILED_WRITES; ++present) update_rows(false, false);
    CHECK(ec::module_pinned && ec::failed_writes == ec::MAX_FAILED_WRITES && (ec::written & ec::WORKER_SITES) == ec::WORKER_SITES);
    CHECK(SetThreadContext(thread, &saved) != FALSE);
    ResumeThread(thread);
    parked.Stop();
    CloseHandle(thread);
    ec::module_pinned = false;
    update_rows(true, true);  // the rows asked for again: the count starts over
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == 0 && !ec::module_pinned);
    update_rows(true, true);

    // An unwind through a stub left the count raised: Uninstall cannot take the sites out; the module is pinned, the words are 0.
    ec::worker_data->cut_active.store(1);
    twattila::engine_code::Uninstall();
    CHECK((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES && ec::module_pinned && fill_slot() == 0 && sweep_slot() == 0);
    ec::worker_data->cut_active.store(0);
    ec::module_pinned = false;
    ec::failed_writes = 0;
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == 0 && !ec::module_pinned);
    std::printf("   another mod's bytes over a site, a thread kept in a stub, a stuck count at Uninstall: each leaves the site in, pins the module and zeroes the words\n");
  }

  // 23e0. A thread parked at each byte of each worker site's own five bytes (SuspendThread / SetThreadContext, as 29d): refused strictly inside (the group stands down), free at
  // the first byte and the byte after, going in and going out. The sweep entry is the only site whose five bytes hold three instruction starts (the review's Should 1b).
  {
    ParkedThread parked;
    HANDLE thread = parked.Open();
    CHECK(thread != nullptr);
    const uint32_t all = (has_fill ? ec::WORKER_SITES : ec::WORKER_SWEEP_BIT);
    int wrong = 0;
    int checked = 0;
    // The rows `on` with the thread's Eip at `where`: true when the sites changed.
    const auto try_rows = [&](const uint8_t* where, bool on) {
      wrong += (SuspendThread(thread) == static_cast<DWORD>(-1)) ? 1 : 0;
      CONTEXT saved = {.ContextFlags = CONTEXT_CONTROL};
      wrong += (GetThreadContext(thread, &saved) == FALSE) ? 1 : 0;
      CONTEXT moved = saved;
      moved.Eip = Address(where);
      wrong += (SetThreadContext(thread, &moved) == FALSE) ? 1 : 0;
      ec::failed_writes = 0;
      update_rows(on && has_fill, on);
      const bool changed = ((ec::written & ec::WORKER_SITES) == (on ? all : 0));
      wrong += (SetThreadContext(thread, &saved) == FALSE) ? 1 : 0;
      ResumeThread(thread);
      ++checked;
      return changed;
    };
    update_rows(false, false);
    CHECK((ec::written & ec::WORKER_SITES) == 0);
    for (const uint32_t index : {ec::WORKER_FILL_INDEX, ec::WORKER_SWEEP_INDEX}) {
      if (index == ec::WORKER_FILL_INDEX && !has_fill) continue;
      const uint8_t* const start = ec::sites[index].at;
      for (size_t offset = 0; offset <= ec::SITE_SIZE; ++offset) {
        const bool inside = (offset > 0 && offset < ec::SITE_SIZE);
        wrong += (try_rows(start + offset, true) == inside) ? 1 : 0;
        ec::failed_writes = 0;
        update_rows(has_fill, true);
        wrong += ((ec::written & ec::WORKER_SITES) != all) ? 1 : 0;
        wrong += (try_rows(start + offset, false) == inside) ? 1 : 0;
        ec::failed_writes = 0;
        update_rows(false, false);
        wrong += ((ec::written & ec::WORKER_SITES) != 0 || fill_slot() != 0 || sweep_slot() != 0) ? 1 : 0;
      }
    }
    parked.Stop();
    CloseHandle(thread);
    ec::failed_writes = 0;
    CHECK(wrong == 0 && (ec::written & ec::WORKER_SITES) == 0 && !ec::module_pinned);
    std::printf("   a thread parked at %d places (each byte of each worker site's own five and the byte after, going in and going out): refused strictly inside, free at the ends\n", checked);
  }

  // 23e. 200 switches under 4 threads running whole updates in both copies on their own worlds, and a sweep of the pending list in both copies between them (the sweep entry's
  // stub, its Arm call and the row's tracing flag flip with the fill site).
  {
    // The sweep's group as pending_sweep_split.inc builds it (that file is included after this one): `this` with the count at +0x18 and the entries at +0x1c, lists never ready.
    struct SweepGroup {
      alignas(16) uint8_t self[0x40];
      uint32_t entries[2 * 12];
      alignas(16) uint8_t lists[12][0x50];
    };
    using SweepFunction = void(__fastcall*)(void* list);
    const uint32_t sweep_rva = build.code->sweep_rva;
    const auto sweep_a = reinterpret_cast<SweepFunction>(game + sweep_rva);
    const auto sweep_b = reinterpret_cast<SweepFunction>(patched + sweep_rva);
    // The sweep's rotating tweak (one byte at +0x48 of the object its first `mov ecx` names): on in both copies for the threads, put back after.
    std::array<uint8_t*, 2> tweaks = {};
    std::array<uint8_t, 2> tweaks_before = {};
    if (sweep_rva != 0) {
      tweaks = {reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(Read32(game + sweep_rva + 4)) + 0x48), reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(Read32(patched + sweep_rva + 4)) + 0x48)};
      for (size_t copy = 0; copy < tweaks.size(); ++copy) {
        tweaks_before[copy] = *tweaks[copy];
        *tweaks[copy] = 1;
      }
    }
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> run_count = 0;
    std::atomic<uint64_t> sweep_count = 0;
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        auto own = std::make_unique<AnimationWorld>();
        auto group = std::make_unique<SweepGroup>();
        Random thread_random = {.state = 0xA11CE0000ull + static_cast<uint64_t>(worker) * 0x9E3779B97F4Aull};
        while (!stop.load()) {
          AnimationSpec spec = AnimationRandom(&thread_random);
          spec.rotate = false;  // the tweak is one byte of the copy: the threads agree on it
          if (const char* what = AnimationBoth(game, patched, own.get(), spec); what[0] != 0 && ++bad <= 5) {
            std::printf("   thread %d: the update differs in %s\n", worker, what);
          }
          ++run_count;
          if (sweep_rva == 0) continue;
          const uint32_t count = thread_random.Next() % 13;
          std::memset(group.get(), 0, sizeof(*group));
          Write32(group->self + 0x18, count);
          Write32(group->self + 0x1C, Address(group->entries));
          for (uint32_t entry = 0; entry < count; ++entry) group->entries[2 * entry] = Address(group->lists[entry]);
          const SweepGroup saved = *group;
          sweep_a(group->self);
          const SweepGroup after_game = *group;
          *group = saved;
          sweep_b(group->self);
          if (std::memcmp(&after_game, group.get(), sizeof(*group)) != 0 && ++bad <= 5) std::printf("   thread %d: the sweep of %u lists differs\n", worker, count);
          ++sweep_count;
        }
      });
    }
    int waited = 0;
    int never = 0;
    bool on = true;
    for (int switches = 0; switches < 200; ++switches) {
      on = !on;
      int tries = 0;
      do {
        update_rows(on, on);
        ++tries;
      } while ((((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES) != on) && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      waited += tries > 1 ? 1 : 0;
      never += (((ec::written & ec::WORKER_SITES) == ec::WORKER_SITES) != on) ? 1 : 0;
    }
    update_rows(true, true);
    stop = true;
    for (auto& thread : threads) thread.join();
    if (sweep_rva != 0) {
      for (size_t copy = 0; copy < tweaks.size(); ++copy) *tweaks[copy] = tweaks_before[copy];
    }
    std::printf("   200 switches while 4 threads made %llu whole updates and %llu sweeps in both copies: %llu wrong (%d writes waited for a thread inside a stub, %d switches never reached)\n",
                run_count.load(), sweep_count.load(), bad.load(), waited, never);
    CHECK(bad.load() == 0 && never == 0 && run_count.load() > 0 && (sweep_rva == 0 || sweep_count.load() > 0) && (ec::written & ec::WORKER_SITES) == ec::WORKER_SITES && fill_slot() == dispatch && !ec::module_pinned);
  }

  update_rows(false, false);
  CHECK((ec::written & ec::WORKER_SITES) == 0 && fill_slot() == 0 && sweep_slot() == 0);
  restore_fill();
  AnimationRotate(game, false);
  AnimationRotate(patched, false);
  return RunAnimationRowsExactTests(build, game, patched, quick);
}
