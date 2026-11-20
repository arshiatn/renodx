// Threaded Draw Recording on the game's own depth loop (empire.retail.dll +0x17b17f0), x86 only. The DLL is only READ from disk and
// mapped twice, relocated, executable and writable, never loaded as a DLL (no DllMain, no imports), as test/tw_engine_code does:
// copy A as the game ships it (the serial reference), copy B prepared by engine_code (PrepareRecording) with the sites written by its
// PausedWrite, the dispatcher (draw_recording.hpp) running the loop in B on its worker threads. Everything the loop calls outside its
// own code is a fake in both copies, written over the callee's first bytes (the flush's body behind its entry, the state owner, the
// animation value and call, the technique check) or into the weighted vtables' slots (+0x54 +0x58 +0x60 +0x64 +0x68, and the stride
// object's +0x14): the fakes are the ones test/tw_draw_recording models (the flush advances the cursor by count x stride, writes the
// +0x28 cache and reads and clears the +0x69 byte), and the loop's own bytes are the game's.
// 1. The images: the loop's bytes in A are the file's, in B only the sites differ; the fakes' places hold what was expected.
// 2. The loop serially in A, then through the dispatcher in B (the cut thunk or DispatchDepth) at every stage with the flush entry's
//    hook and with the three call-site fallbacks: the fakes' log (flush, vf68 and animation calls, in order), the tracker's 0xa0 bytes
//    (+0x50 the stride, +0x58 .. +0x78 the state), every mesh's +0x40 / +0x44 equal.
// 3. Lists: short and long runs, animation -1 runs, the seed-breaking pattern, a maximum count of 6, meshes of a class the sites do
//    not know, tiny op buffers, a 200000-item call, calls with fewer items than the floor, the direct stages with the sites in.
// 4. The calls that must run directly: entry state (pending instances, change mask) and the F2 continuation of foreign mesh state.
// 5. CONTROLs, each must differ: the continuation guard, seed validation, pointer translation, the any capture, the stride rule, the
//    depth init site's bytes put back to the game's.
// tests.exe [empire.retail.dll]
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" __declspec(dllexport) const char* const NAME = "Draw recording loop test";

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
}

#if defined(_WIN64)

int main() {
  std::printf("SKIP: Attila is 32-bit; the sites are x86 only.\n");
  return 0;
}

#else

#include "src/games/twattila/engine_code.hpp"

namespace dr = twattila::draw_recording;
namespace di = twattila::draw_recording::internal;
namespace ec = twattila::engine_code::internal;

namespace {

int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

// The game's addresses (RVAs): the loop and what it calls.
constexpr uint32_t LOOP_RVA = 0x17b17f0;
constexpr uint32_t STATE_OWNER_RVA = 0x178c460;  // returns the object whose +0x2950 is the animation state
constexpr uint32_t ANIM_VALUE_RVA = 0x1846e50;   // (this, animation) -> the value the state is given
constexpr uint32_t TECHNIQUE_CHECK_RVA = 0x18eedd0;
constexpr uint32_t FLUSH_BODY_RVA = 0x17783a8;  // behind the flush's head and its `je` for no instances
constexpr uint32_t FLUSH_BODY_BYTES = 0x34;     // mov ecx, [esp + 0x34]
constexpr uint32_t LOOP_END_RVA = 0x17b1a04;
constexpr uint32_t WEIGHTED_SLOTS[] = {0x54, 0x58, 0x60, 0x64, 0x68};

std::vector<uint8_t> file;

uint32_t Read32(const uint8_t* at) {
  uint32_t value;
  std::memcpy(&value, at, sizeof(value));
  return value;
}

void Write32(uint8_t* at, uint32_t value) { std::memcpy(at, &value, sizeof(value)); }

// The DLL's sections at a new address, relocated. Executable and writable (a test).
uint8_t* Map(uint32_t* image_size) {
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
  *image_size = nt->OptionalHeader.SizeOfImage;
  auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, *image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (base == nullptr) return nullptr;
  std::memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
  const auto* section = IMAGE_FIRST_SECTION(nt);
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    const size_t size = std::min<size_t>(section->SizeOfRawData, section->Misc.VirtualSize);
    if (size != 0) std::memcpy(base + section->VirtualAddress, file.data() + section->PointerToRawData, size);
  }
  const uint32_t delta = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base)) - nt->OptionalHeader.ImageBase;
  const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  for (uint32_t at = 0; at < directory.Size;) {
    const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(base + directory.VirtualAddress + at);
    if (block->SizeOfBlock == 0) break;
    const auto* entries = reinterpret_cast<const uint16_t*>(block + 1);
    const size_t count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
    for (size_t entry = 0; entry < count; ++entry) {
      if ((entries[entry] >> 12) != IMAGE_REL_BASED_HIGHLOW) continue;
      uint8_t* fixed = base + block->VirtualAddress + (entries[entry] & 0xFFF);
      Write32(fixed, Read32(fixed) + delta);
    }
    at += block->SizeOfBlock;
  }
  return base;
}

void JumpTo(uint8_t* at, const void* to) {
  at[0] = 0xE9;
  Write32(at + 1, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(to) - reinterpret_cast<uintptr_t>(at + 5)));
}

uint8_t* game = nullptr;     // A: as the game ships it
uint8_t* patched = nullptr;  // B: the sites
uint32_t image_size = 0;
std::vector<uint8_t> pristine;  // A's bytes before any fake

template <class T>
T& Field(void* base, uint32_t offset) {
  return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

// ---------------------------------------------------------------- the fakes

struct Mesh {
  uint32_t* vtable;
  uint32_t kind;
  uint8_t pad0[0x38];
  uint32_t view_tag;
  uint32_t pass;
  uint8_t pad1[0x14];
  uint8_t flag;
  uint8_t pad2[0x23];
};
static_assert(sizeof(Mesh) == 0x80 && offsetof(Mesh, view_tag) == 0x40 && offsetof(Mesh, pass) == 0x44 && offsetof(Mesh, flag) == 0x5c);

struct Item {
  Mesh* mesh;
  uint8_t pad0[0x5c - 4];
  int32_t anim;
  uint8_t pad1[0x100 - 0x60];
};
static_assert(sizeof(Item) == 0x100 && offsetof(Item, anim) == 0x5c);

struct List {
  uint32_t pad;
  uint32_t count;
  Item** data;
};
static_assert(offsetof(List, count) == 4 && offsetof(List, data) == 8);

struct StrideObject {
  const uint32_t* vtable;
  uint32_t kind;
};

// kind, mesh, the 10 flush arguments (or the call's arguments), view_tag, pass, the +0x69 byte the flush saw.
using LogEntry = std::array<uint32_t, 15>;
std::vector<LogEntry> game_log;  // only the render thread writes it (the recorded calls run on the workers' copies)
enum Fake { OWNER, ANIM_VALUE, ANIM_CALL, TECHNIQUE_CHECK, VF54, STRIDE, VF58, VF60, VF64, VF68, FLUSH, FAKES };
std::array<std::atomic<uint32_t>, FAKES> fake_calls;

alignas(16) uint8_t state_owner[0x3000];
std::array<StrideObject, 8> stride_objects;
std::array<uint32_t, 0x20> stride_vtable;
std::array<uint32_t, 0x70 / 4> unknown_vtable;  // a class the sites do not know, with the same fakes
uint8_t* flush_body = nullptr;
uint8_t* empty_flush_exit = nullptr;
std::atomic<uint32_t> empty_flushes = 0;  // flushes of no instances the game's own code let through (its head's exit)
DWORD main_thread_id = 0;

// The fakes that log what the game would have done run on the main thread only: a call on a worker is a deferred call that ran.
void CheckMainThread() {
  if (GetCurrentThreadId() != main_thread_id) {
    std::printf("FAIL: a logging fake ran off the main thread\n");
    ++failures;
  }
}

std::atomic<bool> vf64_other = false;  // a control: the fake answers differently in the run that sets it

uint32_t TechniqueOf(uint32_t kind) { return (kind == 0 ? 0 : (kind == 1 ? 0x101 : kind * 0x10)); }

void Count(Fake fake) { fake_calls[fake].fetch_add(1, std::memory_order_relaxed); }

uint8_t* __fastcall FakeOwner(void*, void*) {
  Count(OWNER);
  return state_owner;
}
uint32_t __fastcall FakeAnimValue(void*, void*, int32_t anim) {
  Count(ANIM_VALUE);
  return static_cast<uint32_t>(anim) * 3 + 1;
}
void __fastcall FakeAnimCall(void* state, void*, uint32_t value) {
  CheckMainThread();
  Count(ANIM_CALL);
  game_log.push_back({3, reinterpret_cast<uint32_t>(state), value, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
}
// ecx is the technique itself (an opaque number here); the loop keeps it only when this says yes.
uint8_t __fastcall FakeTechniqueCheck(void* technique, void*) {
  Count(TECHNIQUE_CHECK);
  return (reinterpret_cast<uintptr_t>(technique) & 1) == 0;
}
void* __fastcall FakeVf54(Mesh* mesh, void*) {
  Count(VF54);
  return &stride_objects[mesh->kind];
}
uint32_t __fastcall FakeStride(StrideObject* object, void*) {
  Count(STRIDE);
  return 4 + object->kind;
}
uint32_t __fastcall FakeVf58(Mesh* mesh, void*) {
  Count(VF58);
  return 0x1000 + mesh->kind;
}
// The first argument is vf58's result: a wrong one makes a technique no kind has.
uint32_t __fastcall FakeVf60(Mesh* mesh, void*, uint32_t from_vf58, uint32_t zero) {
  Count(VF60);
  return (from_vf58 == 0x1000 + mesh->kind && zero == 0 ? TechniqueOf(mesh->kind) : 0xBAD0BAD0);
}
// (tracker +0x18, tracker +0x1c, the item): the answer depends on all of them.
uint8_t __fastcall FakeVf64(Mesh* mesh, void*, uint32_t tracker_18, uint32_t tracker_1c, const Item* item) {
  Count(VF64);
  return static_cast<uint8_t>(static_cast<uint32_t>(item->anim) + mesh->kind + tracker_1c + (tracker_18 >> 4) + (vf64_other.load(std::memory_order_relaxed) ? 1 : 0));
}
uint8_t __fastcall FakeVf68(Mesh* mesh, void*, uint32_t view, uint32_t zero) {
  CheckMainThread();
  Count(VF68);
  game_log.push_back({2, reinterpret_cast<uint32_t>(mesh), view, zero, 0, 0, 0, 0, 0, 0, 0, 0, mesh->view_tag, mesh->pass, 0});
  return static_cast<uint8_t>(mesh->kind & 1);
}
// The flush's body (its head, with the entry hook or not, is the game's): the cursor +0x5c advances by count x stride, the +0x28 cache
// is written, the +0x69 byte is read and cleared.
void __cdecl FlushImpl(Mesh* mesh, const uint32_t* args) {
  CheckMainThread();
  Count(FLUSH);
  auto* any = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(args[3]));
  game_log.push_back({1, reinterpret_cast<uint32_t>(mesh), args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9],
                      mesh->view_tag, mesh->pass, *any});
  *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(args[6])) += args[4] * (mesh->kind + 1);
  *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(args[8])) = args[4] ^ args[2];
  *any = 0;
}

// The code behind the flush's head: lea eax, [esp + 0x30] (the arguments); FlushImpl(ebx = the mesh, eax); the game's own way out
// (pop edi esi ebp ebx; add esp, 0x1c; ret 0x28).
uint8_t* BuildFlushBody() {
  auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  const uint8_t code[] = {0x8D, 0x44, 0x24, 0x30, 0x50, 0x53, 0xB8, 0, 0, 0, 0, 0xFF, 0xD0, 0x83, 0xC4, 0x08, 0x5F, 0x5E, 0x5D, 0x5B, 0x83, 0xC4, 0x1C, 0xC2, 0x28, 0x00};
  std::memcpy(page, code, sizeof(code));
  Write32(page + 7, reinterpret_cast<uint32_t>(&FlushImpl));
  return page;
}

// The flush's exit for a count of 0 (lock inc [empty_flushes]; then the game's own pops and `ret 0x28`), so that a list with
// max_count 0 can be shown to reach it.
uint8_t* BuildEmptyFlushExit() {
  auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  const uint8_t code[] = {0xF0, 0xFF, 0x05, 0, 0, 0, 0, 0x5F, 0x5E, 0x5D, 0x5B, 0x83, 0xC4, 0x1C, 0xC2, 0x28, 0x00};
  std::memcpy(page, code, sizeof(code));
  Write32(page + 3, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&empty_flushes)));
  return page;
}

// The vtable slots of the weighted classes' fakes, as the game's vtable would hold them.
void FillSlots(uint32_t* vtable) {
  vtable[0x54 / 4] = reinterpret_cast<uint32_t>(&FakeVf54);
  vtable[0x58 / 4] = reinterpret_cast<uint32_t>(&FakeVf58);
  vtable[0x60 / 4] = reinterpret_cast<uint32_t>(&FakeVf60);
  vtable[0x64 / 4] = reinterpret_cast<uint32_t>(&FakeVf64);
  vtable[0x68 / 4] = reinterpret_cast<uint32_t>(&FakeVf68);
}

// After PrepareRecording (it checks the callees' heads and the vtables' slots against the game's).
void InstallFakes(uint8_t* image) {
  JumpTo(image + STATE_OWNER_RVA, reinterpret_cast<const void*>(&FakeOwner));
  JumpTo(image + ANIM_VALUE_RVA, reinterpret_cast<const void*>(&FakeAnimValue));
  JumpTo(image + ec::ANIM_CALL_RVA, reinterpret_cast<const void*>(&FakeAnimCall));
  JumpTo(image + TECHNIQUE_CHECK_RVA, reinterpret_cast<const void*>(&FakeTechniqueCheck));
  JumpTo(image + FLUSH_BODY_RVA, flush_body);
  JumpTo(image + ec::FLUSH_EMPTY_RVA, empty_flush_exit);
  for (const ec::WeightedClass& weighted : ec::WEIGHTED_CLASSES) FillSlots(reinterpret_cast<uint32_t*>(image + weighted.vtable));
}

// ---------------------------------------------------------------- the world

struct Spec {
  uint32_t items = 6000;
  uint32_t seed = 1;
  uint32_t max_run = 8;
  uint32_t none_percent = 10;
  uint32_t anim_minus_percent = 0;
  uint32_t anim_change_percent = 20;
  uint32_t max_count = 1000;
  bool pattern = false;
  // Items from this percent of the list on use meshes without an enabled technique.
  uint32_t enabled_percent = 100;
  // The list starts with 40 items of one mesh, and the tracker starts as a continuation of it with foreign mesh state.
  bool continuation = false;
  // Meshes (percent) of a class the sites do not know.
  uint32_t unknown_percent = 0;
  // The driver's portion = this / the tracker's stride.
  uint32_t portion_numerator = 12000;
};

struct World {
  std::vector<Mesh> meshes;
  std::vector<bool> unknown_class;
  std::vector<Item> items;
  std::vector<Item*> pointers;
  List list = {};
  alignas(16) std::array<uint8_t, dr::TRACKER_SIZE> tracker = {};
  uint32_t max_count = 1000;
  uint32_t portion_numerator = 12000;
  bool continuation = false;
  // The tracker's state when a call starts (a call the loop itself ended leaves both 0).
  uint32_t pending = 0;
  uint32_t change_mask = 0;
};

std::unique_ptr<World> BuildWorld(const Spec& spec) {
  auto world = std::make_unique<World>();
  uint64_t state = spec.seed * 0x9e3779b97f4a7c15ull + 1;
  const auto random = [&state](uint32_t bound) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return static_cast<uint32_t>((state >> 11) % bound);
  };
  world->max_count = spec.max_count;
  world->portion_numerator = spec.portion_numerator;
  world->continuation = spec.continuation;
  world->meshes.resize(60);
  world->unknown_class.resize(60);
  for (uint32_t m = 0; m < world->meshes.size(); ++m) {
    Mesh& mesh = world->meshes[m];
    mesh.flag = static_cast<uint8_t>(m & 1);
    if (spec.pattern) {
      mesh.kind = (m < 6 ? 2 + m % 4 : 0);
    } else if (spec.enabled_percent < 100) {
      mesh.kind = (m < 4 ? m % 2 : 2 + random(4));
    } else if (random(100) < spec.none_percent) {
      mesh.kind = random(2);
    } else {
      mesh.kind = 2 + random(4);
    }
    world->unknown_class[m] = (random(100) < spec.unknown_percent);
  }
  world->items.resize(spec.items);
  uint32_t i = 0;
  int32_t anim = 0;
  uint32_t pattern_run = 0;
  while (i < spec.items) {
    uint32_t mesh_index = 0;
    uint32_t run = 1;
    if (spec.pattern) {
      // Five items of a valid mesh, three of one without a technique.
      const bool none = (pattern_run % 2 == 1);
      mesh_index = (none ? 6 + (pattern_run / 2) % 4 : (pattern_run / 2) % 6);
      run = (none ? 3 : 5);
      ++pattern_run;
    } else {
      if (spec.enabled_percent < 100) {
        const bool tail = (static_cast<uint64_t>(i) * 100 >= static_cast<uint64_t>(spec.items) * spec.enabled_percent);
        mesh_index = (tail ? random(4) : 4 + random(static_cast<uint32_t>(world->meshes.size()) - 4));
      } else {
        mesh_index = random(static_cast<uint32_t>(world->meshes.size()));
      }
      // Log-uniform lengths from 1 to max_run.
      run = 1 + random(1u << random(1 + static_cast<uint32_t>(std::bit_width(spec.max_run))));
      run = std::min(run, spec.max_run);
    }
    if (!spec.pattern && random(100) < spec.anim_minus_percent) anim = -1;
    for (uint32_t r = 0; r < run && i < spec.items; ++r, ++i) {
      if (spec.pattern) {
        anim = 5;
      } else if (random(100) < spec.anim_change_percent) {
        anim = (random(100) < spec.anim_minus_percent ? -1 : static_cast<int32_t>(random(7)));
      }
      world->items[i].mesh = &world->meshes[mesh_index];
      world->items[i].anim = anim;
    }
  }
  if (spec.continuation) {
    world->meshes[0].kind = 2;
    world->unknown_class[0] = false;
    for (uint32_t r = 0; r < 40 && r < spec.items; ++r) {
      world->items[r].mesh = &world->meshes[0];
      world->items[r].anim = 0;
    }
  }
  world->pointers.resize(spec.items);
  for (uint32_t k = 0; k < spec.items; ++k) world->pointers[k] = &world->items[k];
  world->list = {.count = spec.items, .data = world->pointers.data()};
  return world;
}

// The meshes' vtables in `image` (the weighted class by index, or the unknown one): the world is the same, the copy that runs it differs.
void Bind(World* world, const uint8_t* image) {
  for (size_t m = 0; m < world->meshes.size(); ++m) {
    world->meshes[m].vtable = (world->unknown_class[m] ? unknown_vtable.data()
                                                       : reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(image) + ec::WEIGHTED_CLASSES[m % ec::WEIGHTED_CLASSES.size()].vtable));
  }
}

dr::Shared* shared = nullptr;

void ResetWorld(World* world) {
  for (Mesh& mesh : world->meshes) {
    mesh.view_tag = 0xffffffff;
    mesh.pass = 0xffffffff;
  }
  world->tracker = {};
  uint8_t* t = world->tracker.data();
  Field<uint32_t>(t, 0x18) = 0xd0;
  Field<uint32_t>(t, 0x1c) = 0xe0;
  Field<uint32_t>(t, 0x20) = 0x12345;
  Field<uint32_t>(t, 0x24) = 0xb0;
  Field<uint32_t>(t, 0x50) = 6;
  Field<uint32_t>(t, 0x64) = world->max_count;
  if (world->continuation || world->pending != 0 || world->change_mask != 0) {
    // A call that starts in the middle of a mesh whose state is another view's (or not this call's).
    Mesh* mesh = (world->continuation ? world->items[0].mesh : &world->meshes.back());
    Field<uint32_t>(t, 0x70) = reinterpret_cast<uint32_t>(mesh);
    Field<uint32_t>(t, 0x74) = 0x20;
    Field<uint32_t>(t, 0x58) = world->pending;
    Field<uint32_t>(t, 0x78) = world->change_mask;
    mesh->view_tag = 0xabc;
    mesh->pass = 0x77;
  }
  shared->seed_anim = -1;
  shared->last_anim = -1;
  game_log.clear();
}

struct Outcome {
  std::vector<LogEntry> log;
  std::array<uint8_t, dr::TRACKER_SIZE> tracker = {};
  std::vector<std::array<uint32_t, 2>> mesh_fields;
  uint32_t chunks = 0;
  uint32_t reruns = 0;
  uint32_t replayed = 0;
  uint32_t ops = 0;
  uint32_t direct_calls = 0;
  // The driver's calls, and those with fewer items than the dispatcher's floor.
  uint32_t portions = 0;
  uint32_t small_calls = 0;
  std::array<uint32_t, 6> compares = {};
  std::array<uint32_t, 3> op_kinds = {};  // flush, vf68, animation ops the chunks recorded (traced runs)
  uint32_t most_buffer = 0;
  std::string first_reason;
};

Outcome Capture(World* world) {
  Outcome outcome = {.log = game_log, .tracker = world->tracker};
  for (const Mesh& mesh : world->meshes) outcome.mesh_fields.push_back({mesh.view_tag, mesh.pass});
  return outcome;
}

void* const NODE = reinterpret_cast<void*>(0x1000);

// The driver: portions of the list sized from the tracker's +0x50 (read after each portion), back to back on one tracker.
template <class RunPortion>
void Driver(World* world, RunPortion&& run_portion) {
  const uint32_t total = world->list.count;
  for (uint32_t begin = 0; begin < total;) {
    const uint32_t stride = std::max<uint32_t>(1, Field<uint32_t>(world->tracker.data(), 0x50));
    const uint32_t end = std::min(total, begin + world->portion_numerator / stride);
    run_portion(begin, end);
    begin = end;
  }
}

// thiscall: ecx the node, four arguments on the stack, the callee pops them.
using LoopFn = void(__fastcall*)(void* node, void* unused, void* tracker, void* list, uint32_t begin, uint32_t end);

Outcome RunSerial(World* world) {
  Bind(world, game);
  ResetWorld(world);
  const auto loop = reinterpret_cast<LoopFn>(game + LOOP_RVA);
  for (int pass = 0; pass < 2; ++pass) {
    Driver(world, [&](uint32_t begin, uint32_t end) { loop(NODE, nullptr, world->tracker.data(), &world->list, begin, end); });
  }
  return Capture(world);
}

bool through_cut = false;  // the cut thunk (the site at the loop's caller) instead of DispatchDepth

LoopFn CutThunk() {
  const ec::Site& site = ec::recording_sites[static_cast<size_t>(ec::RecordingSite::DEPTH_CUT)];
  return reinterpret_cast<LoopFn>(site.at + ec::SITE_SIZE + static_cast<int32_t>(Read32(site.jump.data() + 1)));
}

// Two passes of the driver through the dispatcher (the world is reset and the settings applied).
Outcome RunCalls(World* world, bool tracing) {
  Outcome total = {};
  for (int pass = 0; pass < 2; ++pass) {
    Driver(world, [&](uint32_t begin, uint32_t end) {
      if (through_cut) {
        CutThunk()(NODE, nullptr, world->tracker.data(), &world->list, begin, end);
      } else {
        dr::DispatchDepth(NODE, nullptr, world->tracker.data(), &world->list, begin, end);
      }
      ++total.portions;
      total.small_calls += (end - begin < di::MIN_ITEMS ? 1 : 0);
      total.chunks += di::last_call.chunks;
      total.reruns += di::last_call.reruns;
      total.replayed += di::last_call.replayed;
      total.ops += di::last_call.ops;
      total.direct_calls += (di::last_call.direct ? 1 : 0);
      if (tracing && !di::trace.calls.empty()) {
        const di::CallTrace& call = di::trace.calls.back();
        ++total.compares[static_cast<size_t>(call.compare_result)];
        if (total.first_reason.empty()) total.first_reason = call.direct_reason;
      }
    });
  }
  if (tracing) {
    for (const di::CallTrace& call : di::trace.calls) {
      total.most_buffer = std::max(total.most_buffer, call.op_buffer_bytes);
      for (size_t kind = 0; kind < total.op_kinds.size(); ++kind) total.op_kinds[kind] += call.ops[kind + 1];
    }
  }
  di::trace.active = false;
  Outcome outcome = Capture(world);
  outcome.chunks = total.chunks;
  outcome.reruns = total.reruns;
  outcome.replayed = total.replayed;
  outcome.ops = total.ops;
  outcome.direct_calls = total.direct_calls;
  outcome.portions = total.portions;
  outcome.small_calls = total.small_calls;
  outcome.compares = total.compares;
  outcome.op_kinds = total.op_kinds;
  outcome.most_buffer = total.most_buffer;
  outcome.first_reason = total.first_reason;
  return outcome;
}

Outcome RunStaged(World* world, dr::Stage stage, uint32_t threads, bool tracing = false, bool update = true) {
  Bind(world, patched);
  ResetWorld(world);
  if (update) dr::Update({.stage = stage, .threads = threads, .loops = 1}, true);
  di::trace.active = tracing;
  di::trace.frames_left = 1u << 30;
  // Traced calls compare every call (the game's own trace compares only its last frame).
  di::controls.compare_every_call = tracing;
  di::trace.calls.clear();
  return RunCalls(world, tracing);
}

bool Equal(const Outcome& serial, const Outcome& staged, const char* label, bool print = true) {
  bool equal = true;
  if (serial.log != staged.log) {
    equal = false;
    if (print) {
      size_t at = 0;
      while (at < std::min(serial.log.size(), staged.log.size()) && serial.log[at] == staged.log[at]) ++at;
      std::printf("  %s: logs differ at entry %zu of %zu / %zu\n", label, at, serial.log.size(), staged.log.size());
    }
  }
  if (serial.tracker != staged.tracker) {
    equal = false;
    if (print) std::printf("  %s: tracker bytes differ\n", label);
  }
  if (serial.mesh_fields != staged.mesh_fields) {
    equal = false;
    if (print) std::printf("  %s: mesh fields differ\n", label);
  }
  return equal;
}

bool CompareDiffers(const Outcome& outcome) {
  return outcome.compares[static_cast<size_t>(di::CompareResult::DIFFERS)] + outcome.compares[static_cast<size_t>(di::CompareResult::COMPARE_HAS_MORE)] > 0;
}

// Which sites are in B: the flush entry's hook (and the own sites), or the call-site fallbacks (and the own sites).
const uint64_t ENTRY_MODE = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::DEPTH_A)] | ec::RECORDING_ENTRY_BIT;
const uint64_t FALLBACK_MODE = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::DEPTH_A)] | ec::RECORDING_FALLBACK[static_cast<size_t>(dr::Group::DEPTH_A)];

// engine_code's own window, as Update calls it.
void SetSites(uint64_t mask) {
  uint32_t stood_down = 0;
  uint64_t recording_stood_down = 0;
  REQUIRE(ec::PausedWrite(0, &stood_down, mask, &recording_stood_down));
  REQUIRE(stood_down == 0 && recording_stood_down == 0);
  ec::recording_written = mask;
}

const char* ModeName(uint64_t mode) { return mode == ENTRY_MODE ? "entry hook" : "call sites"; }

const std::array<dr::Stage, 4> RECORD_STAGES = {dr::Stage::CHUNKED, dr::Stage::RECORDED, dr::Stage::ONE_WORKER, dr::Stage::WORKERS};
const char* StageName(dr::Stage stage) {
  switch (stage) {
    case dr::Stage::CHUNKED: return "chunked";
    case dr::Stage::RECORDED: return "recorded";
    case dr::Stage::ONE_WORKER: return "one worker";
    case dr::Stage::WORKERS: return "workers";
    default: return "?";
  }
}

struct ListCase {
  const char* name;
  Spec spec;
};

std::vector<ListCase> Lists() {
  return {{"short runs", {.items = 6000, .seed = 1, .max_run = 8}},
          {"runs 1 to 300", {.items = 9000, .seed = 2, .max_run = 300, .none_percent = 15}},
          {"animation -1 runs", {.items = 6000, .seed = 3, .max_run = 20, .anim_minus_percent = 60, .anim_change_percent = 40}},
          {"no technique before cuts", {.items = 6000, .seed = 4, .max_run = 6, .none_percent = 45}},
          {"seed breaking pattern", {.items = 4000, .seed = 5, .pattern = true}},
          {"max count flushes", {.items = 6000, .seed = 6, .max_run = 120, .max_count = 6}},
          {"max count 0 (flushes of no instances)", {.items = 6000, .seed = 9, .max_run = 60, .max_count = 0}},
          {"last chunks without technique", {.items = 6000, .seed = 7, .max_run = 12, .enabled_percent = 10}},
          {"last chunks without technique or animation", {.items = 6000, .seed = 8, .max_run = 12, .anim_minus_percent = 100, .enabled_percent = 10}}};
}

// ---------------------------------------------------------------- 1. the images

size_t DifferingBytes(const uint8_t* image, uint32_t from, uint32_t to) {
  size_t count = 0;
  for (uint32_t at = from; at < to; ++at) {
    if (image[at] != pristine[at]) ++count;
  }
  return count;
}

std::vector<uint8_t> loop_bytes_b;  // B's loop as it was mapped (no site in)

// Every call the loop makes, from its bytes: the 7 direct ones (the target's RVA) and the 6 through a register that a vtable slot fills.
struct DirectCall {
  uint32_t at;
  uint32_t target;
};
struct IndirectCall {
  uint32_t at;
  std::vector<uint8_t> bytes;
};

void TestImages() {
  // A: the loop's bytes are the file's (relocated) and B's differ from A's only in the one absolute address (the relocation's delta).
  CHECK(DifferingBytes(game, LOOP_RVA, LOOP_END_RVA) == 0);
  size_t apart = 0;
  for (uint32_t at = LOOP_RVA; at < LOOP_END_RVA; ++at) {
    if (game[at] != patched[at]) {
      ++apart;
      CHECK(at >= 0x17b180d && at < 0x17b1811);  // mov ecx, [the global]
    }
  }
  CHECK(apart > 0);
  CHECK(game[LOOP_RVA] == 0x83 && game[LOOP_RVA + 1] == 0xEC && game[LOOP_RVA + 2] == 0x08 && game[LOOP_END_RVA - 5] == 0xE9 && game[LOOP_END_RVA] == 0xCC);
  const std::array<DirectCall, 7> direct = {{{0x17b181a, STATE_OWNER_RVA},
                                             {0x17b1889, ec::FLUSH_RVA},
                                             {0x17b18e0, TECHNIQUE_CHECK_RVA},
                                             {0x17b1933, ec::FLUSH_RVA},
                                             {0x17b19aa, ec::FLUSH_RVA},
                                             {0x17b19c9, ANIM_VALUE_RVA},
                                             {0x17b19d3, ec::ANIM_CALL_RVA}}};
  for (const DirectCall& call : direct) CHECK(game[call.at] == 0xE8 && call.at + 5 + Read32(game + call.at + 1) == call.target);
  const std::vector<IndirectCall> indirect = {{0x17b18be, {0xFF, 0x50, 0x58}},        // call [eax + 0x58]
                                              {0x17b18cc, {0x8B, 0x51, 0x60, 0x8B, 0xCF, 0xFF, 0xD2}},  // mov edx, [ecx + 0x60]; mov ecx, edi; call edx
                                              {0x17b1952, {0xFF, 0x50, 0x54}},        // call [eax + 0x54]
                                              {0x17b1959, {0xFF, 0x52, 0x14}},        // call [edx + 0x14]
                                              {0x17b1968, {0x8B, 0x40, 0x68, 0xFF, 0xD0}},  // mov eax, [eax + 0x68]; call eax
                                              {0x17b19f0, {0x8B, 0x40, 0x64, 0xFF, 0xD0}}};  // mov eax, [eax + 0x64]; call eax
  for (const IndirectCall& call : indirect) CHECK(std::memcmp(game + call.at, call.bytes.data(), call.bytes.size()) == 0);
  // The flush's body behind its head (and its `je` for no instances), where the fake goes.
  CHECK(Read32(pristine.data() + FLUSH_BODY_RVA) == 0x34244C8B);
  // The fakes are the only changes to A: 6 jumps (the flush's exit for no instances among them) and the 5 slots of each of the 10 weighted vtables.
  const size_t expected = 6 * 5 + ec::WEIGHTED_CLASSES.size() * 5 * 4;
  size_t differing = 0;
  for (uint32_t at = 0; at < image_size; ++at) {
    if (game[at] != pristine[at]) ++differing;
  }
  CHECK(differing <= expected && differing >= expected / 2);
  std::printf("1. the loop's %u bytes in A are the file's (B's differ in the one relocated address only); its 7 direct calls go to the state owner, the flush (3 sites), the technique check, the animation value and call; its 6 indirect calls are the vtable slots +0x14 +0x54 +0x58 +0x60 +0x64 +0x68\n",
              LOOP_END_RVA - LOOP_RVA);
}

// B's loop differs from its mapped bytes only in the written sites of `mask` that lie in it (SITE_SIZE bytes each); the flush entry's hook is outside it.
// Returns the number of sites of `mask` in the loop.
size_t CheckWrittenSites(uint64_t mask) {
  size_t in_loop = 0;
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
    const ec::Site& site = ec::recording_sites[index];
    const uint32_t rva = static_cast<uint32_t>(site.at - patched);
    const bool written = ((mask >> index) & 1) != 0;
    CHECK((std::memcmp(site.at, (written ? site.jump : site.game).data(), ec::SITE_SIZE) == 0));
    if (rva >= LOOP_RVA && rva < LOOP_END_RVA && written) ++in_loop;
  }
  for (uint32_t at = LOOP_RVA; at < LOOP_END_RVA; ++at) {
    if (patched[at] == loop_bytes_b[at - LOOP_RVA]) continue;
    bool inside = false;
    for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
      const uint32_t rva = static_cast<uint32_t>(ec::recording_sites[index].at - patched);
      inside |= (((mask >> index) & 1) != 0 && at >= rva && at < rva + ec::SITE_SIZE);
    }
    CHECK(inside);
  }
  CHECK((patched[ec::FLUSH_RVA] == 0xE9) == ((mask & ec::RECORDING_ENTRY_BIT) != 0));
  return in_loop;
}

// ---------------------------------------------------------------- 2. the serial run against the dispatcher

void TestSerialRun() {
  for (const std::atomic<uint32_t>& calls : fake_calls) CHECK(calls.load() == 0);
  const auto world = BuildWorld(Lists()[0].spec);
  const Outcome serial = RunSerial(world.get());
  CHECK(serial.log.size() > 1000);
  std::array<size_t, 4> by_kind = {};
  for (const LogEntry& entry : serial.log) ++by_kind[entry[0]];
  CHECK(by_kind[1] > 100 && by_kind[2] > 100 && by_kind[3] > 100);
  // Every fake the loop reaches was reached by it.
  for (size_t fake = 0; fake < FAKES; ++fake) CHECK(fake_calls[fake].load() > 0);
  CHECK(Read32(serial.tracker.data() + 0x50) >= 4);
  std::printf("2a. the serial run in A: %zu flushes, %zu vf68 calls, %zu animation calls in the log; all %d fakes reached (state owner %u, animation value %u, technique check %u, vf54 %u, stride %u, vf58 %u, vf60 %u, vf64 %u)\n",
              by_kind[1], by_kind[2], by_kind[3], FAKES, fake_calls[OWNER].load(), fake_calls[ANIM_VALUE].load(), fake_calls[TECHNIQUE_CHECK].load(), fake_calls[VF54].load(), fake_calls[STRIDE].load(),
              fake_calls[VF58].load(), fake_calls[VF60].load(), fake_calls[VF64].load());
}

void TestStages() {
  uint32_t cases = 0;
  uint32_t reruns_seen = 0;
  uint32_t chunks_seen = 0;
  uint32_t ops_flush = 0;
  uint32_t ops_vf68 = 0;
  uint32_t ops_anim = 0;
  for (const uint64_t mode : {ENTRY_MODE, FALLBACK_MODE}) {
    SetSites(mode);
    CHECK(CheckWrittenSites(mode) == (mode == ENTRY_MODE ? 3u : 6u));
    for (const bool cut : {false, true}) {
      through_cut = cut;
      // The cut thunk is the same call in both modes; the entry-hook mode gets the full grid, the fallback and the cut a smaller one.
      const bool full = (mode == ENTRY_MODE && !cut);
      for (const ListCase& list : Lists()) {
        const auto world = BuildWorld(list.spec);
        empty_flushes = 0;
        const Outcome serial = RunSerial(world.get());
        CHECK(serial.log.size() > 100);
        // The loop's flushes of no instances (the game's own exit for count 0): only a list with max_count 0 reaches it.
        CHECK((empty_flushes.load() > 0) == (list.spec.max_count == 0));
        for (const dr::Stage stage : RECORD_STAGES) {
          for (const uint32_t k : (full ? std::vector<uint32_t>{2, 3, 4, 8, 16} : std::vector<uint32_t>{4, 16})) {
            for (const bool tracing : {false, true}) {
              const Outcome staged = RunStaged(world.get(), stage, k, tracing);
              char label[160];
              std::snprintf(label, sizeof(label), "%s, %s, %s, %s, K %u%s", ModeName(mode), cut ? "cut thunk" : "DispatchDepth", list.name, StageName(stage), k, tracing ? ", traced" : "");
              CHECK(Equal(serial, staged, label));
              CHECK(staged.portions > 2);
              CHECK(staged.direct_calls == staged.small_calls);
              CHECK(!CompareDiffers(staged));
              if (stage != dr::Stage::CHUNKED) CHECK(staged.replayed + staged.reruns == staged.chunks);
              if (stage != dr::Stage::CHUNKED && staged.chunks > 0) CHECK(staged.ops > 0 || staged.reruns > 0);
              if (k >= 4) CHECK(staged.chunks >= 2 * 2);
              if (tracing && stage == dr::Stage::RECORDED && k == 4 && list.spec.seed == 1) {
                // The chunks really recorded the loop's three kinds of side effect, whichever way the flush is reached.
                CHECK(staged.op_kinds[0] > 100 && staged.op_kinds[1] > 100 && staged.op_kinds[2] > 100);
                ops_flush += staged.op_kinds[0];
                ops_vf68 += staged.op_kinds[1];
                ops_anim += staged.op_kinds[2];
              }
              reruns_seen += staged.reruns;
              chunks_seen += staged.chunks;
              ++cases;
            }
          }
        }
      }
    }
  }
  through_cut = false;
  CHECK(reruns_seen > 0);
  std::printf("2b. the real loop through the dispatcher: %u cases (9 lists x 4 stages x K x trace off / on; the flush entry's hook and the 3 call-site fallbacks; DispatchDepth and the cut thunk): log, tracker (+0x50, +0x5c, +0x28, +0x69 included) and the meshes' +0x40 / +0x44 equal the serial run; %u chunks run in all, %u run again; recorded in the first list at K 4 (both modes, the two ways in): %u flush, %u vf68, %u animation ops\n",
              cases, chunks_seen, reruns_seen, ops_flush, ops_vf68, ops_anim);
}

void TestDirectStages() {
  uint32_t cases = 0;
  for (const uint64_t mode : {ENTRY_MODE, FALLBACK_MODE}) {
    SetSites(mode);
    const auto world = BuildWorld(Lists()[0].spec);
    const Outcome serial = RunSerial(world.get());
    for (const dr::Stage stage : {dr::Stage::OFF, dr::Stage::DIRECT}) {
      const Outcome staged = RunStaged(world.get(), stage, 8);
      CHECK(Equal(serial, staged, "direct stage with the sites in"));
      CHECK(staged.direct_calls == staged.portions);
      ++cases;
    }
    // The small call (fewer items than the floor), the dispatcher not usable, another thread.
    const auto few = BuildWorld({.items = 300, .seed = 14, .max_run = 10});
    const Outcome few_serial = RunSerial(few.get());
    const Outcome few_staged = RunStaged(few.get(), dr::Stage::WORKERS, 8);
    CHECK(Equal(few_serial, few_staged, "small call") && few_staged.direct_calls == few_staged.portions);
    const dr::GameCalls calls = di::game_calls;
    di::game_calls.usable = false;
    const Outcome unusable = RunStaged(world.get(), dr::Stage::WORKERS, 8);
    di::game_calls = calls;
    CHECK(Equal(serial, unusable, "not usable") && unusable.direct_calls == unusable.portions);
    // The sites out (the arena size is 0, as it is when no site is in): the dispatcher runs the loop directly, as for a cut left in.
    const uint32_t arena_size = shared->arena_size;
    shared->arena_size = 0;
    const Outcome sites_out = RunStaged(world.get(), dr::Stage::WORKERS, 8);
    shared->arena_size = arena_size;
    CHECK(Equal(serial, sites_out, "arena size 0") && sites_out.direct_calls == sites_out.portions);
    cases += 3;
  }
  std::printf("2c. the sites in and the loop run directly (stage off / direct, a call under the floor, the dispatcher not usable, the arena size 0): %u cases equal\n", cases);
}

// ---------------------------------------------------------------- 3. lists

void TestUnknownClass() {
  SetSites(ENTRY_MODE);
  uint32_t cases = 0;
  uint32_t reruns = 0;
  for (const uint32_t percent : {5u, 30u, 100u}) {
    const auto world = BuildWorld({.items = 6000, .seed = 50 + percent, .max_run = 10, .unknown_percent = percent});
    const Outcome serial = RunSerial(world.get());
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      for (const uint32_t k : {4u, 16u}) {
        const Outcome staged = RunStaged(world.get(), stage, k, true);
        CHECK(Equal(serial, staged, "unknown class"));
        if (percent >= 30) CHECK(staged.reruns > 0);
        CHECK(!CompareDiffers(staged));
        reruns += staged.reruns;
        ++cases;
      }
    }
  }
  std::printf("3a. meshes of a class the sites do not know (5, 30, 100 percent): %u cases equal, %u chunks run again\n", cases, reruns);
}

void TestOverflowAndLarge() {
  SetSites(ENTRY_MODE);
  uint32_t reruns = 0;
  {
    const auto world = BuildWorld({.items = 6000, .seed = 12, .max_run = 10});
    const Outcome serial = RunSerial(world.get());
    for (const uint32_t limit : {200u, 1000u, 4000u}) {
      di::op_buffer_limit = limit;
      for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
        for (const uint32_t k : {2u, 5u, 16u}) {
          const Outcome staged = RunStaged(world.get(), stage, k, true);
          CHECK(Equal(serial, staged, "tiny buffers"));
          CHECK(staged.reruns > 0 && !CompareDiffers(staged));
          reruns += staged.reruns;
        }
      }
    }
    di::op_buffer_limit = di::OP_ARENA_BYTES;
  }
  const auto world = BuildWorld({.items = 200000, .seed = 41, .max_run = 8, .max_count = 6, .portion_numerator = 12000000});
  const Outcome serial = RunSerial(world.get());
  for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
    for (const uint32_t k : {2u, 3u}) {
      const Outcome staged = RunStaged(world.get(), stage, k, true);
      CHECK(Equal(serial, staged, "large call"));
      CHECK(staged.portions == 2 && staged.direct_calls == 0);
      CHECK(staged.most_buffer <= di::OP_ARENA_BYTES);
    }
  }
  std::printf("3b. tiny op buffers (%u chunks run again) and a 200000-item call at K 2 / 3: logs, trackers and meshes equal\n", reruns);
}

// ---------------------------------------------------------------- 4. the calls that run directly

void TestEntryAndContinuation() {
  SetSites(ENTRY_MODE);
  const auto world = BuildWorld({.items = 6000, .seed = 16, .max_run = 10});
  for (const uint32_t variant : {0u, 1u}) {
    world->pending = (variant == 0 ? 5 : 0);
    world->change_mask = (variant == 1 ? 8 : 0);
    const Outcome serial = RunSerial(world.get());
    for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
      const Outcome staged = RunStaged(world.get(), stage, 8, true);
      CHECK(Equal(serial, staged, variant == 0 ? "pending instances" : "change mask"));
      CHECK(staged.first_reason == "entry state");
      CHECK(staged.direct_calls > staged.small_calls);
    }
  }
  world->pending = 0;
  world->change_mask = 0;

  // F2: the first mesh of the call is the tracker's last one, with state another view wrote, and it recurs later in the list.
  const auto continuation = BuildWorld({.items = 6000, .seed = 17, .max_run = 10, .max_count = 6, .continuation = true});
  const Outcome serial = RunSerial(continuation.get());
  uint32_t cases = 0;
  for (const uint64_t mode : {ENTRY_MODE, FALLBACK_MODE}) {
    SetSites(mode);
    for (const dr::Stage stage : RECORD_STAGES) {
      for (uint32_t k = 2; k <= 16; k += 2) {
        const Outcome staged = RunStaged(continuation.get(), stage, k, true);
        CHECK(Equal(serial, staged, "continuation"));
        CHECK(staged.first_reason == "continuation");
        ++cases;
      }
    }
  }
  SetSites(ENTRY_MODE);
  // Guard off: the compare run has set the mesh up before chunk 0's snapshot is taken, so the replay flushes it with the wrong
  // state (the replay restores the flush's mesh snapshot; only the compare ignores it). The chunks on the workers differ too.
  di::controls.guard_continuation = false;
  const Outcome unguarded_recorded = RunStaged(continuation.get(), dr::Stage::RECORDED, 4, true);
  CHECK(!Equal(serial, unguarded_recorded, "control: continuation guard off", false));
  bool unguarded_differs = false;
  for (int attempt = 0; attempt < 5 && !unguarded_differs; ++attempt) {
    for (const uint32_t k : {8u, 16u}) {
      const Outcome unguarded = RunStaged(continuation.get(), dr::Stage::WORKERS, k, true);
      unguarded_differs |= !Equal(serial, unguarded, "control: continuation guard off", false);
    }
  }
  di::controls.guard_continuation = true;
  std::printf("   guard off on the workers (K 8, 16): %s\n", unguarded_differs ? "differs" : "equal this time");
  std::printf("4. a call that starts with pending instances or a change mask runs directly (entry state); the F2 continuation of foreign mesh state runs directly (%u cases, both flush modes, the real loop's unchanged-mesh path); CONTROL, guard off: differs\n", cases);
}

// ---------------------------------------------------------------- 5. controls

void TestControls() {
  SetSites(ENTRY_MODE);
  const auto world = BuildWorld({.items = 6000, .seed = 21, .max_run = 8});
  const Outcome serial = RunSerial(world.get());
  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::RECORDED, 8), "control baseline"));

  const auto pattern = BuildWorld({.items = 4000, .seed = 22, .pattern = true});
  const Outcome pattern_serial = RunSerial(pattern.get());
  // With validation on the pattern list is run again at its misses; off, the run differs.
  const Outcome validated = RunStaged(pattern.get(), dr::Stage::RECORDED, 8);
  CHECK(Equal(pattern_serial, validated, "pattern validated"));
  CHECK(validated.reruns > 0);
  di::controls.validate_seeds = false;
  const Outcome unvalidated = RunStaged(pattern.get(), dr::Stage::RECORDED, 8);
  di::controls.validate_seeds = true;
  CHECK(!Equal(pattern_serial, unvalidated, "control: validation off", false));

  di::controls.translate_pointers = false;
  const Outcome untranslated = RunStaged(world.get(), dr::Stage::RECORDED, 4);
  di::controls.translate_pointers = true;
  CHECK(!Equal(serial, untranslated, "control: translation off", false));

  // The any byte the loop sets itself must travel in the flush op.
  di::controls.capture_any = false;
  const Outcome uncaptured = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  di::controls.capture_any = true;
  CHECK(!Equal(serial, uncaptured, "control: any capture off", false));

  // The stride of the last chunk that wrote one, not the last chunk's.
  const auto tail = BuildWorld({.items = 6000, .seed = 8, .max_run = 12, .anim_minus_percent = 100, .enabled_percent = 10});
  const Outcome tail_serial = RunSerial(tail.get());
  di::controls.stride_non_zero = false;
  const Outcome stale = RunStaged(tail.get(), dr::Stage::RECORDED, 8);
  di::controls.stride_non_zero = true;
  CHECK(!Equal(tail_serial, stale, "control: stride copied always", false));
  CHECK(Equal(tail_serial, RunStaged(tail.get(), dr::Stage::RECORDED, 8), "control: stride rule on"));

  // The depth init site's bytes put back to the game's (written by hand: the window's sets are whole groups, and this takes one site
  // of the group out): no chunk, and no run again, starts its animation at its seed.
  const ec::Site& init = ec::recording_sites[static_cast<size_t>(ec::RecordingSite::DEPTH_INIT)];
  std::memcpy(init.at, init.game.data(), ec::SITE_SIZE);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  const auto animated = BuildWorld({.items = 6000, .seed = 23, .max_run = 10, .anim_change_percent = 30});
  const Outcome animated_serial = RunSerial(animated.get());
  const Outcome without_init = RunStaged(animated.get(), dr::Stage::RECORDED, 8, true);
  CHECK(!Equal(animated_serial, without_init, "control: init site out", false));
  std::memcpy(init.at, init.jump.data(), ec::SITE_SIZE);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  CHECK(Equal(animated_serial, RunStaged(animated.get(), dr::Stage::RECORDED, 8), "control: init site back"));

  // The harness sees what the loop's calls on a worker's copy decide: the unrecorded virtual call (vf64, which ORs into the copy's +0x69
  // byte) answering differently in the recorded run makes the flush ops, and so the log, differ.
  vf64_other = true;
  const Outcome other = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  vf64_other = false;
  CHECK(!Equal(serial, other, "control: vf64 answers differently", false));

  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::WORKERS, 8), "controls cleared"));
  std::printf("5. CONTROLs: seed validation off, pointer translation off, any capture off, stride copied always, the depth init site's bytes put back to the game's, an unrecorded virtual call (vf64) answering differently in the recorded run: each run differs; the runs with the rule back are equal\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const char* path = (argc > 1 ? argv[1] : R"(C:\Program Files (x86)\Steam\steamapps\common\Total War Attila\empire.retail.dll)");
  std::ifstream stream(path, std::ios::binary);
  REQUIRE(stream.good());
  file.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data())->e_lfanew);
  REQUIRE(nt->OptionalHeader.ImageBase == 0x10000000);
  game = Map(&image_size);
  patched = Map(&image_size);
  REQUIRE(game != nullptr && patched != nullptr);
  pristine.assign(game, game + image_size);
  loop_bytes_b.assign(patched + LOOP_RVA, patched + LOOP_END_RVA);
  std::printf("empire.retail.dll build %08lX, %zu bytes: mapped twice (relocated, never loaded)\n", nt->FileHeader.TimeDateStamp, file.size());

  // B prepared by engine_code with the DLL as it ships, then the fakes in both; the sites are written by its PausedWrite.
  REQUIRE(ec::PrepareRecording(reinterpret_cast<HMODULE>(patched)));
  REQUIRE(ec::recording_found == (1u << ec::RECORDING_SITE_COUNT) - 1);
  REQUIRE(di::game_calls.usable && di::game_calls.loops[static_cast<size_t>(dr::Group::DEPTH_A)] == ec::Address(patched + LOOP_RVA));
  shared = dr::SharedPage();
  REQUIRE(shared != nullptr);
  flush_body = BuildFlushBody();
  empty_flush_exit = BuildEmptyFlushExit();
  main_thread_id = GetCurrentThreadId();
  FillSlots(unknown_vtable.data());
  stride_vtable.fill(0);
  stride_vtable[0x14 / 4] = reinterpret_cast<uint32_t>(&FakeStride);
  for (uint32_t kind = 0; kind < stride_objects.size(); ++kind) stride_objects[kind] = {.vtable = stride_vtable.data(), .kind = kind};
  InstallFakes(game);
  InstallFakes(patched);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);

  TestImages();
  TestSerialRun();
  TestStages();
  TestDirectStages();
  TestUnknownClass();
  TestOverflowAndLarge();
  TestEntryAndContinuation();
  TestControls();
  SetSites(0);
  CHECK(CheckWrittenSites(0) == 0);  // the sites out: the game's own bytes again
  CHECK(std::memcmp(patched + LOOP_RVA, loop_bytes_b.data(), loop_bytes_b.size()) == 0);
  dr::Update({.stage = dr::Stage::OFF}, false);
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}

#endif
