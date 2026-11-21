// Threaded Draw Recording on the game's own depth loop (empire.retail.dll +0x17b17f0) and colour loop (+0x17b0400), x86 only. The DLL is only READ from disk and
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
// 6. The colour loop (A group, 11 sites) on its real bytes, as 2 and 3 do for the depth loop: sorted and interleaved meshes, textures
//    (a null one is no texture), factions (none, a list of 4 or more colours, a shorter one), batch sizes (the variation path and the
//    batch virtual call), animation-only changes, a maximum count, meshes without a technique or of an unknown class, the tracker's
//    constants-dirty and change flags (+0x10, +0x12, +0x13) and the game's constants cache flag on and off: the log (flushes, virtual
//    calls, animation calls, the constants' dirty calls, the binds and the faction colours) and the tracker equal the serial run.
//    CONTROLs again, and a B round the dispatcher runs directly before it has planned anything (the sorted list's accessor SetGame
//    found). Some items carry a faction mask: the loop's gather runs the game's vector push_back and free, faked in both copies like
//    the vector constructor. What stays synthetic-only (test/tw_draw_recording, test/tw_draw_recording_sites): the batchfix
//    continuation bytes.
// 7. The colour bodies' 5 state blocks (B group, Colour B) on their real bytes, entered at the site and left at the block's end by a
//    jump the test writes in both copies: random registers, locals, tracker and constants cell (3000 cases per block). A runs the
//    game's block; B with the tracker outside the arena runs the thunk's direct path (log, tracker, frame, registers, cell equal); B
//    with a slot's tracker runs the record path (one op, the tracker's first 0x14 bytes as the game writes them, esp, ebp, esi and
//    the reloaded ebx as the game leaves them), and the replay of that op makes the game's calls.
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
#include <string_view>
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

// One present of the add-on: the pool's row (`threads`, the render thread included), then the user's own Update.
void UpdateAt(uint32_t threads, const dr::Settings& settings) {
  twattila::worker_pool::Update({.threads = threads, .in_use = settings.stage != dr::Stage::OFF});
  dr::Update(settings);
}

// The game's addresses (RVAs): the loop and what it calls.
constexpr uint32_t LOOP_RVA = 0x17b17f0;
constexpr uint32_t STATE_OWNER_RVA = 0x178c460;  // returns the object whose +0x2950 is the animation state
constexpr uint32_t ANIM_VALUE_RVA = 0x1846e50;   // (this, animation) -> the value the state is given
constexpr uint32_t TECHNIQUE_CHECK_RVA = 0x18eedd0;
constexpr uint32_t FLUSH_BODY_RVA = 0x17783a8;  // behind the flush's head and its `je` for no instances
constexpr uint32_t FLUSH_BODY_BYTES = 0x34;     // mov ecx, [esp + 0x34]
constexpr uint32_t LOOP_END_RVA = 0x17b1a04;
constexpr uint32_t WEIGHTED_SLOTS[] = {0x54, 0x58, 0x60, 0x64, 0x68};
// The colour loop (A group) and what it calls that the depth loop does not: the constants' dirty call, the shader resource bind, the
// faction colours call and the 12-byte vector constructor of its default colours; the texture view lookup stays the game's (+4).
// The constants' cache flag is the operand of its two guards.
constexpr uint32_t COLOUR_LOOP_RVA = 0x17b0400;
constexpr uint32_t COLOUR_LOOP_END_RVA = 0x17b09e2;
constexpr uint32_t DIRTY_RVA = 0x191d960;
constexpr uint32_t SET_SRV_RVA = 0x18d2b60;
constexpr uint32_t FACTION_COLOURS_RVA = 0x1885350;
constexpr uint32_t VECTOR_INIT_RVA = 0xbeea0;
constexpr uint32_t PUSH_BACK_RVA = 0x1972b0;  // the faction mask's gather: the vector's push_back (thiscall, one argument) and free (cdecl)
constexpr uint32_t FREE_RVA = 0xe98c0;
constexpr uint32_t CACHE_FLAG_RVA = 0x1da661b;

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
  uint8_t pad_a[8];
  uint32_t value10;  // the colour loop's second constant is the default one for 0x71
  uint8_t pad_b[0x2c];
  uint32_t view_tag;
  uint32_t pass;
  uint8_t pad1[0x14];
  uint8_t flag;
  uint8_t pad2[0x23];
};
static_assert(sizeof(Mesh) == 0x80 && offsetof(Mesh, value10) == 0x10 && offsetof(Mesh, view_tag) == 0x40 && offsetof(Mesh, pass) == 0x44
              && offsetof(Mesh, flag) == 0x5c);

// A texture: the game's lookup (`mov eax, [ecx + 4]`) gives the view at +4.
struct Texture {
  uint32_t pad;
  uint32_t view;
};

// The colour loop reads an item's two textures (+0x38, +0x3c), its faction mask (+0x54) and faction (+0x58), its animation (+0x5c)
// and its batch size (+0xec); the depth loop its animation.
struct Item {
  Mesh* mesh;
  uint8_t pad0[0x38 - 4];
  Texture* tex_a;
  Texture* tex_b;
  uint8_t pad1[0x54 - 0x40];
  uint32_t mask;
  int32_t faction;
  int32_t anim;
  uint8_t pad2[0xec - 0x60];
  uint32_t batch;
  uint8_t pad3[0x100 - 0xf0];
};
static_assert(sizeof(Item) == 0x100 && offsetof(Item, tex_a) == 0x38 && offsetof(Item, mask) == 0x54 && offsetof(Item, faction) == 0x58
              && offsetof(Item, anim) == 0x5c && offsetof(Item, batch) == 0xec);

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
// The colour loop's: 4 the constants' dirty call (the block, the cell's value), 5 a shader resource bind (slot, view), 6 the faction
// colours (the state, 12 floats).
using LogEntry = std::array<uint32_t, 15>;
std::vector<LogEntry> game_log;  // only the render thread writes it (the recorded calls run on the workers' copies)
enum Fake { OWNER, ANIM_VALUE, ANIM_CALL, TECHNIQUE_CHECK, VF54, STRIDE, VF58, VF60, VF64, VF68, FLUSH, FAKES };
std::array<std::atomic<uint32_t>, FAKES> fake_calls;
// The colour loop's own (reached only by it).
enum ColourFake { DIRTY, SRV, FACTION_COLOURS, FACTION_LIST, VF6C, VECTOR_INIT, PUSH_BACK, FREE, COLOUR_FAKES };
std::array<std::atomic<uint32_t>, COLOUR_FAKES> colour_calls;

alignas(16) uint8_t state_owner[0x7000];
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
// The colour loop's fakes. The ones that log run on the main thread only (the recorded constants, binds and faction block are replayed
// there). The state the loop works on is the owner's +0x2950; its two constants' blocks (+0x2ac and +0x2d4) hold their cell's address
// at +0x14.
constexpr uint32_t STATE_OFFSET = 0x2950;
std::array<uint32_t, 2> colour_cells = {};
std::array<Texture, 6> textures = {};

void __fastcall FakeDirty(uint32_t block, void*) {
  CheckMainThread();
  colour_calls[DIRTY].fetch_add(1, std::memory_order_relaxed);
  const uint32_t cell = Read32(reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(block)) + 0x14);
  game_log.push_back({4, block, Read32(reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(cell))), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
}
void __fastcall FakeSetSrv(uint32_t slot, void*, uint32_t view) {
  CheckMainThread();
  colour_calls[SRV].fetch_add(1, std::memory_order_relaxed);
  game_log.push_back({5, slot, view, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
}
// The faction colours call: the state and the 4 colours (12 floats) it reads.
void __fastcall FakeFactionColours(uint32_t state, void*, const uint32_t* colours) {
  CheckMainThread();
  colour_calls[FACTION_COLOURS].fetch_add(1, std::memory_order_relaxed);
  LogEntry entry = {6, state};
  for (uint32_t i = 0; i < 12; ++i) entry[2 + i] = colours[i];
  game_log.push_back(entry);
}
struct FactionList {
  uint32_t pad;
  uint32_t count;
  const float* data;
};
std::array<std::array<float, 24>, 5> faction_colours;  // up to 8 colours of 3 floats: a list's count is 4 + faction
std::array<FactionList, 5> faction_lists;
struct Node {
  const uint32_t* vtable;
};
std::array<uint32_t, 0x40> node_vtable;
Node colour_node = {node_vtable.data()};
// The node's slot +0xc0 (a faction's colour list): writes the list's address through the out pointer.
void __fastcall FakeFactionList(Node*, void*, int32_t faction, uint32_t* out) {
  CheckMainThread();
  colour_calls[FACTION_LIST].fetch_add(1, std::memory_order_relaxed);
  *out = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&faction_lists[static_cast<uint32_t>(faction) % faction_lists.size()]));
}
// The batch-size virtual call (slot +0x6c): the pending count that makes a flush.
uint32_t __fastcall FakeVf6c(Mesh* mesh, void*) {
  colour_calls[VF6C].fetch_add(1, std::memory_order_relaxed);
  return (mesh->kind & 3) + 1;
}
void __fastcall FakeVectorInit(float* vector, void*) {
  colour_calls[VECTOR_INIT].fetch_add(1, std::memory_order_relaxed);
  vector[0] = vector[1] = vector[2] = 0;
}
// The mask's gather vector is {pad, count, data} (a List's layout): push_back appends the 12-byte colour the argument points to.
void __fastcall FakePushBack(uint32_t* vector, void*, const float* colour) {
  colour_calls[PUSH_BACK].fetch_add(1, std::memory_order_relaxed);
  auto* const data = static_cast<float*>(std::realloc(reinterpret_cast<void*>(static_cast<uintptr_t>(vector[2])), 12u * (vector[1] + 1)));
  std::memcpy(data + 3 * vector[1], colour, 12);
  vector[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
  ++vector[1];
}
void __cdecl FakeFree(void* data) {
  colour_calls[FREE].fetch_add(1, std::memory_order_relaxed);
  std::free(data);
}
// A faction mask: the colours' picks in its first 4 bytes (the gather reads one byte per colour of the list, up to 8), then the index
// the loop keeps in the tracker's +0xc.
struct Mask {
  uint8_t picks[4];
  int32_t index;
  uint8_t more[248];
};
std::array<Mask, 4> masks = {};

// The flush's body (its head, with the entry hook or not, is the game's): the cursor +0x5c advances by count x stride, the +0x28 cache
// is written, the +0x69 byte is read and cleared.
void __cdecl FlushImpl(Mesh* mesh, const uint32_t* args) {
  CheckMainThread();
  Count(FLUSH);
  auto* any = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(args[3]));
  // The real flush writes +0x40 before it reads it and never reads +0x44 (the replay restores the snapshot of both only to keep the
  // meshes' state): the entry carries what it can depend on, not +0x44 (the colour loop's batch sizes make it differ in a call's
  // traced runs).
  game_log.push_back({1, reinterpret_cast<uint32_t>(mesh), args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9],
                      mesh->view_tag, 0, *any});
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
  vtable[0x6c / 4] = reinterpret_cast<uint32_t>(&FakeVf6c);
}

// After PrepareRecording (it checks the callees' heads and the vtables' slots against the game's).
void InstallFakes(uint8_t* image) {
  JumpTo(image + STATE_OWNER_RVA, reinterpret_cast<const void*>(&FakeOwner));
  JumpTo(image + ANIM_VALUE_RVA, reinterpret_cast<const void*>(&FakeAnimValue));
  JumpTo(image + ec::ANIM_CALL_RVA, reinterpret_cast<const void*>(&FakeAnimCall));
  JumpTo(image + TECHNIQUE_CHECK_RVA, reinterpret_cast<const void*>(&FakeTechniqueCheck));
  JumpTo(image + FLUSH_BODY_RVA, flush_body);
  JumpTo(image + ec::FLUSH_EMPTY_RVA, empty_flush_exit);
  JumpTo(image + DIRTY_RVA, reinterpret_cast<const void*>(&FakeDirty));
  JumpTo(image + SET_SRV_RVA, reinterpret_cast<const void*>(&FakeSetSrv));
  JumpTo(image + FACTION_COLOURS_RVA, reinterpret_cast<const void*>(&FakeFactionColours));
  JumpTo(image + VECTOR_INIT_RVA, reinterpret_cast<const void*>(&FakeVectorInit));
  JumpTo(image + PUSH_BACK_RVA, reinterpret_cast<const void*>(&FakePushBack));
  JumpTo(image + FREE_RVA, reinterpret_cast<const void*>(&FakeFree));
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
  // The colour loop's list: each mesh's items together (a sorted list), or `interleaved` short runs of random meshes; the textures,
  // the faction and the batch size change from item to item by these percents (a null texture is no texture: the loop leaves the
  // slot as it is; faction -1 is no faction).
  bool colour = false;
  bool interleaved = false;
  uint32_t tex_percent = 30;
  uint32_t zero_tex_percent = 10;
  uint32_t faction_percent = 20;
  uint32_t faction_none_percent = 10;
  uint32_t batch_percent = 15;
  // The mask changes from item to item by this percent (30 percent of the changes are no mask).
  uint32_t mask_percent = 25;
  // The game's constants cache flag, and whether the driver sets the tracker's constants-dirty and change flags (+0x10, +0x12,
  // +0x13) before some of its calls.
  uint8_t cache = 1;
  bool flag_portions = false;
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
  bool colour = false;
  uint8_t cache = 1;
  bool flag_portions = false;
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
  world->colour = spec.colour;
  world->cache = spec.cache;
  world->flag_portions = spec.flag_portions;
  world->meshes.resize(60);
  world->unknown_class.resize(60);
  for (uint32_t m = 0; m < world->meshes.size(); ++m) {
    Mesh& mesh = world->meshes[m];
    mesh.flag = static_cast<uint8_t>(m & 1);
    mesh.value10 = (m % 3 == 0 ? 0x71 : 0x10);
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
  if (spec.colour) {
    // Its own random stream (the depth lists stay as they were); the meshes' items overwrite the depth list's.
    uint64_t colour_state = spec.seed * 0xd1b54a32d192ed03ull + 7;
    const auto colour_random = [&colour_state](uint32_t bound) {
      colour_state ^= colour_state << 13;
      colour_state ^= colour_state >> 7;
      colour_state ^= colour_state << 17;
      return static_cast<uint32_t>((colour_state >> 11) % bound);
    };
    const uint32_t mesh_count = static_cast<uint32_t>(world->meshes.size());
    std::vector<uint32_t> order(mesh_count);
    for (uint32_t m = 0; m < mesh_count; ++m) order[m] = m;
    for (uint32_t m = mesh_count; m > 1; --m) std::swap(order[m - 1], order[colour_random(m)]);
    if (spec.interleaved) {
      for (uint32_t k = 0; k < spec.items;) {
        const uint32_t run = 1 + colour_random(6);
        Mesh* const mesh = &world->meshes[colour_random(mesh_count)];
        for (uint32_t r = 0; r < run && k < spec.items; ++r, ++k) world->items[k].mesh = mesh;
      }
    } else {
      // A sorted list: each mesh's items together, once, in runs of random lengths.
      std::vector<uint32_t> ends(mesh_count - 1);
      for (uint32_t& end : ends) end = colour_random(spec.items);
      std::sort(ends.begin(), ends.end());
      ends.push_back(spec.items);
      for (uint32_t k = 0, run = 0; run < mesh_count; ++run) {
        for (; k < ends[run]; ++k) world->items[k].mesh = &world->meshes[order[run]];
      }
    }
    const auto pick_texture = [&]() -> Texture* {
      return (colour_random(100) < spec.zero_tex_percent ? nullptr : &textures[colour_random(static_cast<uint32_t>(textures.size()))]);
    };
    Texture* tex_a = &textures[0];
    Texture* tex_b = &textures[1];
    int32_t faction = -1;
    uint32_t batch = 0;
    uint32_t mask = 0;
    for (Item& item : world->items) {
      if (colour_random(100) < spec.tex_percent) tex_a = pick_texture();
      if (colour_random(100) < spec.tex_percent) tex_b = pick_texture();
      if (colour_random(100) < spec.faction_percent) faction = (colour_random(100) < spec.faction_none_percent ? -1 : static_cast<int32_t>(colour_random(5)));
      if (colour_random(100) < spec.batch_percent) batch = colour_random(3);
      if (colour_random(100) < spec.mask_percent) {
        mask = (colour_random(100) < 30 ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&masks[colour_random(static_cast<uint32_t>(masks.size()))])));
      }
      item.tex_a = tex_a;
      item.tex_b = tex_b;
      item.faction = faction;
      item.mask = mask;
      item.batch = batch;
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
  if (world->colour) {
    // The colour state (the faction, the two textures, the mask's index) is nothing any item has; the constants' cells start unequal
    // to the values they are given; the cache flag is the world's.
    Field<uint32_t>(t, 0x00) = 7;
    Field<uint32_t>(t, 0x04) = 0xbeef;
    Field<uint32_t>(t, 0x08) = 0xbeef;
    Field<uint32_t>(t, 0x0c) = 0xffffffff;
    colour_cells = {0x3fc00000, 0x40200000};
    Write32(state_owner + STATE_OFFSET + 0x2ac + 0x14, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&colour_cells[0])));
    Write32(state_owner + STATE_OFFSET + 0x2e8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&colour_cells[1])));
    game[CACHE_FLAG_RVA] = patched[CACHE_FLAG_RVA] = world->cache;
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
  std::array<uint32_t, 8> op_kinds = {};  // flush, vf68, animation, constant, bind, faction, vf64, state ops the chunks recorded (traced runs)
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
  for (uint32_t begin = 0, portion = 0; begin < total; ++portion) {
    uint8_t* const t = world->tracker.data();
    if (world->flag_portions) {
      // Constants dirty before every other call, the change flags before some.
      Field<uint8_t>(t, 0x10) = portion % 2;
      Field<uint8_t>(t, 0x12) = (portion % 3 == 0 ? 1 : 0);
      Field<uint8_t>(t, 0x13) = (portion % 4 == 1 ? 1 : 0);
    }
    const uint32_t stride = std::max<uint32_t>(1, Field<uint32_t>(t, 0x50));
    const uint32_t end = std::min(total, begin + world->portion_numerator / stride);
    run_portion(begin, end);
    begin = end;
  }
}

// thiscall: ecx the node, four arguments on the stack (the colour loop's: five, one unused), the callee pops them.
using LoopFn = void(__fastcall*)(void* node, void* unused, void* tracker, void* list, uint32_t begin, uint32_t end);
using ColourFn = void(__fastcall*)(void* node, void* unused, void* tracker, void* list, uint32_t unused_argument, uint32_t begin, uint32_t end);

void CallLoop(uintptr_t loop, World* world, uint32_t begin, uint32_t end) {
  if (world->colour) {
    reinterpret_cast<ColourFn>(loop)(&colour_node, nullptr, world->tracker.data(), &world->list, 0, begin, end);
  } else {
    reinterpret_cast<LoopFn>(loop)(NODE, nullptr, world->tracker.data(), &world->list, begin, end);
  }
}

Outcome RunSerial(World* world) {
  Bind(world, game);
  ResetWorld(world);
  const uintptr_t loop = reinterpret_cast<uintptr_t>(game + (world->colour ? COLOUR_LOOP_RVA : LOOP_RVA));
  for (int pass = 0; pass < 2; ++pass) {
    Driver(world, [&](uint32_t begin, uint32_t end) { CallLoop(loop, world, begin, end); });
  }
  return Capture(world);
}

bool through_cut = false;  // the cut thunk (the site at the loop's caller) instead of the dispatcher

uintptr_t CutThunk(bool colour) {
  const ec::Site& site = ec::recording_sites[static_cast<size_t>(colour ? ec::RecordingSite::COLOUR_CUT : ec::RecordingSite::DEPTH_CUT)];
  return reinterpret_cast<uintptr_t>(site.at + ec::SITE_SIZE + static_cast<int32_t>(Read32(site.jump.data() + 1)));
}

// Two passes of the driver through the dispatcher (the world is reset and the settings applied).
Outcome RunCalls(World* world, bool tracing) {
  Outcome total = {};
  for (int pass = 0; pass < 2; ++pass) {
    Driver(world, [&](uint32_t begin, uint32_t end) {
      if (through_cut) {
        CallLoop(CutThunk(world->colour), world, begin, end);
      } else {
        CallLoop(world->colour ? reinterpret_cast<uintptr_t>(&dr::DispatchColour) : reinterpret_cast<uintptr_t>(&dr::DispatchDepth), world, begin, end);
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
  // N-1: a compare that differed in an earlier run latched its group off; every run starts with none latched, and the runs that must
  // record check that none latched in them.
  di::latched_groups = 0;
  if (update) UpdateAt(threads, {.stage = stage, .loops = (world->colour ? 2u : 1u)});
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
      for (const std::vector<LogEntry>* log : {&serial.log, &staged.log}) {
        if (at >= log->size()) continue;
        std::printf("    %s:", log == &serial.log ? "serial" : "staged");
        for (const uint32_t field : (*log)[at]) std::printf(" %x", field);
        std::printf("\n");
      }
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
const uint64_t COLOUR_ENTRY_MODE = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::COLOUR_A)] | ec::RECORDING_ENTRY_BIT;
const uint64_t COLOUR_FALLBACK_MODE = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::COLOUR_A)] | ec::RECORDING_FALLBACK[static_cast<size_t>(dr::Group::COLOUR_A)];

// engine_code's own window, as Update calls it.
void SetSites(uint64_t mask) {
  uint32_t stood_down = 0;
  uint64_t recording_stood_down = 0;
  REQUIRE(ec::PausedWrite(0, &stood_down, mask, &recording_stood_down));
  REQUIRE(stood_down == 0 && recording_stood_down == 0);
  ec::recording_written = mask;
}

const char* ModeName(uint64_t mode) { return mode == ENTRY_MODE || mode == COLOUR_ENTRY_MODE ? "entry hook" : "call sites"; }

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

// S-1: the reason each traced call ran directly (di::trace.calls) is one the list can explain: a small call or no cut anywhere, an
// unknown class only where the list has meshes of one, a continuation or a conflict only on the interleaved list. Returns the calls
// with no cut (the bound on them is the caller's).
uint32_t DirectReasonsExplained(const ListCase& list) {
  uint32_t no_cut = 0;
  for (const di::CallTrace& call : di::trace.calls) {
    const std::string_view reason = call.direct_reason;
    if (reason == "no cut") {
      ++no_cut;
    } else if (!reason.empty() && reason != "small call") {
      const bool allowed = (reason == "unknown class" ? list.spec.unknown_percent > 0
                                                      : (reason == "continuation" || reason == "conflict") && list.spec.interleaved);
      if (!allowed) std::printf("  %s: a call ran directly for '%s'\n", list.name, call.direct_reason);
      CHECK(allowed);
    }
  }
  return no_cut;
}

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

std::vector<uint8_t> loop_bytes_b;    // B's depth loop as it was mapped (no site in)
std::vector<uint8_t> colour_bytes_b;  // B's colour loop

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
  // The fakes are the only changes to A: 12 jumps (the flush's exit for no instances among them) and the 6 slots of each of the 10 weighted vtables.
  const size_t expected = 12 * 5 + ec::WEIGHTED_CLASSES.size() * 6 * 4;
  size_t differing = 0;
  for (uint32_t at = 0; at < image_size; ++at) {
    if (game[at] != pristine[at]) ++differing;
  }
  CHECK(differing <= expected && differing >= expected / 2);
  std::printf("1. the loop's %u bytes in A are the file's (B's differ in the one relocated address only); its 7 direct calls go to the state owner, the flush (3 sites), the technique check, the animation value and call; its 6 indirect calls are the vtable slots +0x14 +0x54 +0x58 +0x60 +0x64 +0x68\n",
              LOOP_END_RVA - LOOP_RVA);
}

// B's loop (the depth loop, or the colour loop) differs from its mapped bytes only in the written sites of `mask` that lie in it
// (SITE_SIZE bytes each); the flush entry's hook is outside it. Returns the number of sites of `mask` in the loop.
size_t CheckWrittenSites(uint64_t mask, bool colour = false) {
  const uint32_t loop_begin = (colour ? COLOUR_LOOP_RVA : LOOP_RVA);
  const uint32_t loop_end = (colour ? COLOUR_LOOP_END_RVA : LOOP_END_RVA);
  const std::vector<uint8_t>& mapped = (colour ? colour_bytes_b : loop_bytes_b);
  size_t in_loop = 0;
  for (size_t index = 0; index < ec::RECORDING_SITE_COUNT; ++index) {
    const ec::Site& site = ec::recording_sites[index];
    const uint32_t rva = static_cast<uint32_t>(site.at - patched);
    const bool written = ((mask >> index) & 1) != 0;
    CHECK((std::memcmp(site.at, (written ? site.jump : site.game).data(), ec::SITE_SIZE) == 0));
    if (rva >= loop_begin && rva < loop_end && written) ++in_loop;
  }
  for (uint32_t at = loop_begin; at < loop_end; ++at) {
    if (patched[at] == mapped[at - loop_begin]) continue;
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
  uint32_t compares_equal = 0;
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
              CHECK(di::latched_groups == 0);
              if (tracing) CHECK(DirectReasonsExplained(list) == 0);
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
              compares_equal += staged.compares[static_cast<size_t>(di::CompareResult::EQUAL)];
              ++cases;
            }
          }
        }
      }
    }
  }
  through_cut = false;
  CHECK(reruns_seen > 0);
  // The traced calls' compare runs matched the chunks' ops: the compare itself ran (a run that never compares proves nothing).
  CHECK(compares_equal > 0);
  std::printf("2b. the real loop through the dispatcher: %u cases (9 lists x 4 stages x K x trace off / on; the flush entry's hook and the 3 call-site fallbacks; DispatchDepth and the cut thunk): log, tracker (+0x50, +0x5c, +0x28, +0x69 included) and the meshes' +0x40 / +0x44 equal the serial run; %u chunks run in all, %u run again; %u traced calls' compare runs equal; recorded in the first list at K 4 (both modes, the two ways in): %u flush, %u vf68, %u animation ops\n",
              cases, chunks_seen, reruns_seen, compares_equal, ops_flush, ops_vf68, ops_anim);
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

// ---------------------------------------------------------------- 6. the colour loop

constexpr uint32_t TEXTURE_VIEW_RVA = 0xd5c20;

std::vector<ListCase> ColourLists() {
  return {{"sorted meshes", {.items = 6000, .seed = 31, .max_run = 150, .colour = true}},
          {"textures and factions change often", {.items = 6000, .seed = 32, .max_run = 60, .colour = true, .tex_percent = 80, .faction_percent = 70}},
          {"batch sizes change", {.items = 6000, .seed = 33, .max_run = 200, .colour = true, .batch_percent = 60}},
          {"animation only", {.items = 6000, .seed = 34, .max_run = 100, .anim_change_percent = 40, .colour = true, .tex_percent = 0, .faction_percent = 0, .batch_percent = 0}},
          {"max count flushes", {.items = 6000, .seed = 35, .max_run = 150, .max_count = 6, .colour = true, .batch_percent = 0}},
          {"no technique, unknown classes", {.items = 6000, .seed = 36, .max_run = 40, .none_percent = 40, .unknown_percent = 15, .colour = true}},
          {"interleaved meshes", {.items = 6000, .seed = 37, .colour = true, .interleaved = true}},
          {"flags, cache off", {.items = 20000, .seed = 38, .portion_numerator = 8000, .colour = true, .cache = 0, .flag_portions = true}},
          {"flags, cache on", {.items = 20000, .seed = 39, .portion_numerator = 8000, .colour = true, .cache = 1, .flag_portions = true}},
          {"masks often", {.items = 6000, .seed = 40, .max_run = 60, .colour = true, .faction_percent = 60, .faction_none_percent = 0, .mask_percent = 70}}};
}

void TestColourImages() {
  // The loop's bytes in A are the file's (the fakes are outside it); every call it makes, from its bytes.
  CHECK(DifferingBytes(game, COLOUR_LOOP_RVA, COLOUR_LOOP_END_RVA) == 0);
  CHECK(game[COLOUR_LOOP_RVA] == 0x55 && game[COLOUR_LOOP_END_RVA - 5] == 0xE9 && game[COLOUR_LOOP_END_RVA] == 0xCC);
  const std::array<DirectCall, 19> direct = {{{0x17b0430, STATE_OWNER_RVA},
                                              {0x17b04a5, ec::FLUSH_RVA},
                                              {0x17b0500, TECHNIQUE_CHECK_RVA},
                                              {0x17b054d, ec::FLUSH_RVA},
                                              {0x17b05f1, ec::FLUSH_RVA},
                                              {0x17b0736, ec::FLUSH_RVA},
                                              {0x17b0763, ANIM_VALUE_RVA},
                                              {0x17b076d, ec::ANIM_CALL_RVA},
                                              {0x17b07a5, DIRTY_RVA},
                                              {0x17b07e1, DIRTY_RVA},
                                              {0x17b07f4, TEXTURE_VIEW_RVA},
                                              {0x17b0808, SET_SRV_RVA},
                                              {0x17b081b, TEXTURE_VIEW_RVA},
                                              {0x17b082f, SET_SRV_RVA},
                                              {0x17b0872, VECTOR_INIT_RVA},
                                              {0x17b0912, FACTION_COLOURS_RVA},
                                              {0x17b0972, PUSH_BACK_RVA},
                                              {0x17b099c, FACTION_COLOURS_RVA},
                                              {0x17b09aa, FREE_RVA}}};
  for (const DirectCall& call : direct) CHECK(game[call.at] == 0xE8 && call.at + 5 + Read32(game + call.at + 1) == call.target);
  const std::vector<IndirectCall> indirect = {{0x17b04de, {0xFF, 0x50, 0x58}},                      // call [eax + 0x58]
                                              {0x17b04ec, {0x8B, 0x51, 0x60, 0x8B, 0xCB, 0xFF, 0xD2}},  // mov edx, [ecx + 0x60]; mov ecx, ebx; call edx
                                              {0x17b056d, {0xFF, 0x50, 0x54}},                      // call [eax + 0x54]
                                              {0x17b0574, {0xFF, 0x52, 0x14}},                      // call [edx + 0x14]
                                              {0x17b0583, {0x8B, 0x40, 0x68, 0xFF, 0xD0}},          // mov eax, [eax + 0x68]; call eax
                                              {0x17b05c1, {0xFF, 0x50, 0x6C}},                      // call [eax + 0x6c]
                                              {0x17b0942, {0xFF, 0x90, 0xC0, 0x00, 0x00, 0x00}},    // call [eax + 0xc0] (the node's faction list)
                                              {0x17b09ce, {0x8B, 0x40, 0x64, 0xFF, 0xD0}}};         // mov eax, [eax + 0x64]; call eax
  for (const IndirectCall& call : indirect) CHECK(std::memcmp(game + call.at, call.bytes.data(), call.bytes.size()) == 0);
  // The constants' two guards read one cache flag, the one the test sets.
  CHECK(game[0x17b0787] == 0x80 && game[0x17b0788] == 0x3D && game[0x17b07bb] == 0x80 && game[0x17b07bc] == 0x3D);
  CHECK(Read32(game + 0x17b0789) == Read32(game + 0x17b07bd) && Read32(game + 0x17b0789) - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(game)) == CACHE_FLAG_RVA);
  CHECK(CACHE_FLAG_RVA < image_size);
  std::printf("6a. the colour loop's %u bytes in A are the file's; its 19 direct calls (the state owner, the flush x4, the technique check, the animation value and call, the constants' dirty call x2, the texture view x2, the shader resource bind x2, the vector constructor, the faction colours x2, the mask gather's push_back and free) and 8 indirect ones (vtable slots +0x14 +0x54 +0x58 +0x60 +0x64 +0x68 +0x6c, the node's +0xc0) are the ones the fakes stand for; the two constants' guards read the one cache flag\n",
              COLOUR_LOOP_END_RVA - COLOUR_LOOP_RVA);
}

void TestColourStages() {
  uint32_t cases = 0;
  uint32_t reruns_seen = 0;
  uint32_t chunks_seen = 0;
  uint32_t compares_equal = 0;
  uint32_t conflicts = 0;
  std::array<uint32_t, 8> ops = {};
  for (const uint64_t mode : {COLOUR_ENTRY_MODE, COLOUR_FALLBACK_MODE}) {
    SetSites(mode);
    CHECK(CheckWrittenSites(mode, true) >= 6);
    for (const bool cut : {false, true}) {
      through_cut = cut;
      for (const ListCase& list : ColourLists()) {
        const auto world = BuildWorld(list.spec);
        const Outcome serial = RunSerial(world.get());
        CHECK(serial.log.size() > 100);
        for (const dr::Stage stage : RECORD_STAGES) {
          for (const uint32_t k : {4u, 16u}) {
            for (const bool tracing : {false, true}) {
              const Outcome staged = RunStaged(world.get(), stage, k, tracing);
              char label[160];
              std::snprintf(label, sizeof(label), "colour, %s, %s, %s, %s, K %u%s", ModeName(mode), cut ? "cut thunk" : "DispatchColour", list.name, StageName(stage), k, tracing ? ", traced" : "");
              CHECK(Equal(serial, staged, label));
              CHECK(staged.portions > 2);
              // A call inside one long mesh run has no cut and runs directly (a few portions of a list with 60 meshes); the rest are
              // all recorded.
              if (tracing) {
                const uint32_t no_cut = DirectReasonsExplained(list);
                CHECK(list.spec.interleaved || no_cut <= staged.portions / 4);
              } else {
                CHECK(list.spec.interleaved || staged.direct_calls <= staged.small_calls + staged.portions / 4);
              }
              CHECK(!CompareDiffers(staged));
              CHECK(di::latched_groups == 0);
              if (stage != dr::Stage::CHUNKED) {
                if (list.spec.interleaved) {
                  // Two chunks set one mesh up (the lists are not sorted): the owner check sends the call to the direct run after
                  // its chunks were recorded, none replayed.
                  CHECK(staged.replayed + staged.reruns <= staged.chunks);
                  conflicts += staged.chunks - staged.replayed - staged.reruns;
                } else {
                  CHECK(staged.replayed + staged.reruns == staged.chunks);
                }
              }
              if (k >= 4) CHECK(staged.chunks >= 2 * 2);
              if (tracing && stage == dr::Stage::RECORDED && k == 4) {
                for (size_t kind = 0; kind < ops.size(); ++kind) ops[kind] += staged.op_kinds[kind];
              }
              reruns_seen += staged.reruns;
              chunks_seen += staged.chunks;
              compares_equal += staged.compares[static_cast<size_t>(di::CompareResult::EQUAL)];
              ++cases;
            }
          }
        }
      }
    }
  }
  through_cut = false;
  // Every fake the colour loop reaches was reached by it, the chunks recorded every kind of op it has, and some chunks were run again.
  for (size_t fake = 0; fake < COLOUR_FAKES; ++fake) CHECK(colour_calls[fake].load() > 0);
  CHECK(ops[0] > 100 && ops[1] > 100 && ops[2] > 100 && ops[3] > 0 && ops[4] > 100 && ops[5] > 0);
  CHECK(reruns_seen > 0);
  CHECK(compares_equal > 0);
  CHECK(conflicts > 0);
  std::printf("6b. the real colour loop through the dispatcher: %u cases (10 lists x 4 stages x K 4 / 16 x trace off / on; the flush entry's hook and the call-site fallbacks; DispatchColour and the cut thunk): log (flushes, virtual calls, animation calls, constants' dirty calls, binds, faction colours) and tracker equal the serial run; %u chunks run in all, %u run again, %u of the unsorted list's meshes' chunks left to the direct run (the owner check); %u compare runs equal; recorded at K 4: %u flush, %u vf68, %u animation, %u constant, %u bind, %u faction ops; every fake reached (dirty %u, bind %u, faction colours %u, faction list %u, batch size %u, vector init %u, push_back %u, free %u)\n",
              cases, chunks_seen, reruns_seen, conflicts, compares_equal, ops[0], ops[1], ops[2], ops[3], ops[4], ops[5], colour_calls[DIRTY].load(), colour_calls[SRV].load(),
              colour_calls[FACTION_COLOURS].load(), colour_calls[FACTION_LIST].load(), colour_calls[VF6C].load(), colour_calls[VECTOR_INIT].load(), colour_calls[PUSH_BACK].load(),
              colour_calls[FREE].load());
}

void TestColourControls() {
  SetSites(COLOUR_ENTRY_MODE);
  // A traced run that differs would switch the group off for the rest of the run (S9b): the controls look at every run.
  di::latched_groups = 0;
  di::controls.latch_groups = false;
  const auto world = BuildWorld(ColourLists()[1].spec);
  const Outcome serial = RunSerial(world.get());
  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::RECORDED, 8), "colour control baseline"));

  di::controls.translate_pointers = false;
  const Outcome untranslated = RunStaged(world.get(), dr::Stage::RECORDED, 4);
  di::controls.translate_pointers = true;
  CHECK(!Equal(serial, untranslated, "control: translation off", false));

  di::controls.capture_any = false;
  const Outcome uncaptured = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  di::controls.capture_any = true;
  CHECK(!Equal(serial, uncaptured, "control: any capture off", false));

  di::controls.validate_seeds = false;
  const Outcome unvalidated = RunStaged(world.get(), dr::Stage::RECORDED, 8);
  di::controls.validate_seeds = true;
  CHECK(!Equal(serial, unvalidated, "control: validation off", false));

  // A list that is not sorted (a mesh set up by two chunks): the owner check sends the call to the direct run; without it the
  // chunks replay with the meshes' state two threads wrote.
  const auto unsorted = BuildWorld(ColourLists()[6].spec);
  const Outcome unsorted_serial = RunSerial(unsorted.get());
  CHECK(Equal(unsorted_serial, RunStaged(unsorted.get(), dr::Stage::RECORDED, 8), "colour, unsorted list"));
  // Only threads make the difference (a recorded stage runs the chunks one after the other), so this one is told, not required.
  di::controls.owner_check = false;
  bool unowned_differs = false;
  for (int attempt = 0; attempt < 20 && !unowned_differs; ++attempt) {
    unowned_differs |= !Equal(unsorted_serial, RunStaged(unsorted.get(), dr::Stage::WORKERS, 16, true), "control: owner check off", false);
  }
  di::controls.owner_check = true;
  std::printf("   owner check off on the workers (K 16, an unsorted list): %s\n", unowned_differs ? "differs" : "equal this time");

  // The colour init site's bytes put back to the game's: no chunk, and no run again, starts its animation at the seed.
  const auto animated = BuildWorld(ColourLists()[3].spec);
  const Outcome animated_serial = RunSerial(animated.get());
  const Outcome with_init = RunStaged(animated.get(), dr::Stage::RECORDED, 8, true);
  CHECK(Equal(animated_serial, with_init, "colour, animation only"));
  const ec::Site& init = ec::recording_sites[static_cast<size_t>(ec::RecordingSite::COLOUR_INIT)];
  std::memcpy(init.at, init.game.data(), ec::SITE_SIZE);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  const Outcome without_init = RunStaged(animated.get(), dr::Stage::RECORDED, 8, true);
  CHECK(!Equal(animated_serial, without_init, "control: colour init site out", false));
  std::memcpy(init.at, init.jump.data(), ec::SITE_SIZE);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  CHECK(Equal(animated_serial, RunStaged(animated.get(), dr::Stage::RECORDED, 8), "control: colour init site back"));
  CHECK(Equal(serial, RunStaged(world.get(), dr::Stage::WORKERS, 8), "colour controls cleared"));
  di::controls.latch_groups = true;
  di::latched_groups = 0;
  std::printf("6c. colour CONTROLs: pointer translation off, any capture off, seed validation off, the colour init site's bytes put back to the game's: each run differs; the runs with the rule back are equal\n");
}

// A B round the dispatcher runs directly before it has planned anything walks the sorted list with the game's accessor (the one
// SetGame found from the DLL's bytes, here the real code in B): a zero pointer would crash, and the meshes must be visited in order.
std::vector<uint32_t> mesh_visits;
void __fastcall FakeMeshBody(uint32_t* mesh, void*, void*, void*, void*, uint32_t, void*) { mesh_visits.push_back(mesh[1]); }
uint32_t fake_view_type = 0x60;
uint32_t __fastcall FakeViewType(void*, void*) { return fake_view_type; }

void TestMeshBeforePlan() {
  REQUIRE(di::game_calls.accessor != 0 && di::game_calls.vf64_stub != 0);
  std::array<uint32_t, 0x140 / 4> mesh_vtable = {};
  mesh_vtable[0x138 / 4] = reinterpret_cast<uint32_t>(&FakeMeshBody);
  std::array<std::array<uint32_t, 2>, 6> meshes = {};
  std::array<uint32_t, 6> slots = {};
  for (uint32_t i = 0; i < meshes.size(); ++i) {
    meshes[i] = {reinterpret_cast<uint32_t>(mesh_vtable.data()), i};
    slots[i] = reinterpret_cast<uint32_t>(&meshes[i]);
  }
  struct Sorted {
    uint32_t pad[3];
    const uint32_t* data;
  } sorted = {{0, 0, 0}, slots.data()};
  std::array<uint32_t, 0x40> view_vtable = {};
  view_vtable[0x3c / 4] = reinterpret_cast<uint32_t>(&FakeViewType);
  const struct {
    const uint32_t* vtable;
  } view = {view_vtable.data()};
  alignas(16) uint8_t tracker[dr::TRACKER_SIZE] = {};
  uint32_t cases = 0;
  for (const bool usable : {true, false}) {
    for (const dr::Stage stage : {dr::Stage::DIRECT, dr::Stage::WORKERS}) {
      const dr::GameCalls calls = di::game_calls;
      di::game_calls.usable = usable;
      UpdateAt(4, {.stage = stage, .loops = 0xf});
      mesh_visits.clear();
      dr::DispatchMesh(tracker, nullptr, const_cast<void*>(static_cast<const void*>(&view)), 0, nullptr, &sorted, 1, 4);
      di::game_calls = calls;
      CHECK((mesh_visits == std::vector<uint32_t>{1, 2, 3, 4}));
      ++cases;
    }
  }
  std::printf("6d. a B round run directly before it is planned (stage direct, or the dispatcher not usable) visits the sorted list's meshes in order through the accessor SetGame found (%u cases)\n", cases);
}

// M1: the per-mesh cut is both B groups'. With Depth B's sites, the cut and the flush entry hook written and Colour B's 20 sites out
// (a stand-down, a failed write), both rows on: the groups the engine code says are in are Depth B (and A when they are), the
// dispatcher takes only those rows, and a colour round runs directly (the fakes would flag a logging call off the main thread, and a
// recorded round would run the body with no sites on workers).
void TestGroupsIn() {
  const size_t depth_b = static_cast<size_t>(dr::Group::DEPTH_B);
  const size_t colour_b = static_cast<size_t>(dr::Group::COLOUR_B);
  const uint64_t depth_b_mode = ec::RECORDING_OWN[depth_b] | ec::RECORDING_ENTRY_BIT | ec::RecordingBit(ec::RecordingSite::DEPTH_B_CUT);
  REQUIRE((depth_b_mode & ec::RECORDING_OWN[colour_b]) == ec::RecordingBit(ec::RecordingSite::DEPTH_B_CUT));
  std::array<uint32_t, 0x140 / 4> mesh_vtable = {};
  mesh_vtable[0x138 / 4] = reinterpret_cast<uint32_t>(&FakeMeshBody);
  std::array<std::array<uint32_t, 2>, 6> meshes = {};
  std::array<uint32_t, 6> slots = {};
  for (uint32_t i = 0; i < meshes.size(); ++i) {
    meshes[i] = {reinterpret_cast<uint32_t>(mesh_vtable.data()), i};
    slots[i] = reinterpret_cast<uint32_t>(&meshes[i]);
  }
  struct Sorted {
    uint32_t pad[3];
    const uint32_t* data;
  } sorted = {{0, 0, 0}, slots.data()};
  std::array<uint32_t, 0x40> view_vtable = {};
  view_vtable[0x3c / 4] = reinterpret_cast<uint32_t>(&FakeViewType);
  const struct {
    const uint32_t* vtable;
  } view = {view_vtable.data()};
  alignas(16) uint8_t tracker[dr::TRACKER_SIZE] = {};
  uint32_t cases = 0;
  for (const dr::Stage stage : {dr::Stage::RECORDED, dr::Stage::WORKERS}) {
    for (const uint32_t colour_view : {0x10u, 0x20u}) {
      SetSites(depth_b_mode);
      CHECK(twattila::engine_code::DrawRecordingGroupsIn() == 1u << depth_b);
      // The Colour B row is on, and its sites are not: the dispatcher's loops mask has Depth B only.
      UpdateAt(4, {.stage = stage, .loops = 0xc, .groups_in = twattila::engine_code::DrawRecordingGroupsIn()});
      CHECK(di::config.loops == 1u << depth_b);
      REQUIRE(shared->arena_size != 0);
      fake_view_type = colour_view;
      mesh_visits.clear();
      di::last_call = {};
      dr::DispatchMesh(tracker, nullptr, const_cast<void*>(static_cast<const void*>(&view)), 0, nullptr, &sorted, 1, 4);
      CHECK((mesh_visits == std::vector<uint32_t>{1, 2, 3, 4}));
      CHECK(di::last_call.direct);
      ++cases;
    }
  }
  fake_view_type = 0x60;
  // Both B groups' sites written: both are in; Colour B's flush path is the same entry hook.
  SetSites(depth_b_mode | ec::RECORDING_OWN[colour_b]);
  CHECK(twattila::engine_code::DrawRecordingGroupsIn() == (1u << depth_b | 1u << colour_b));
  UpdateAt(4, {.stage = dr::Stage::WORKERS, .loops = 0xc, .groups_in = twattila::engine_code::DrawRecordingGroupsIn()});
  CHECK(di::config.loops == 0xc);
  SetSites(0);
  CHECK(twattila::engine_code::DrawRecordingGroupsIn() == 0);
  std::printf("6e. a B group whose sites are out (Colour B's 20 out, Depth B's, the cut and the entry hook in, both rows on) is not in the groups-in mask, "
              "the dispatcher's loops are Depth B's only, and a colour round runs directly (%u cases); with both in, both rows run\n", cases);
}

// ---------------------------------------------------------------- 7. the colour bodies' state blocks (B group)

// What a block reads, as the game's own code has it (decoded from its bytes here, not from engine_code's table): stack operands are
// esp-relative bytes at the site, -1 is a register or "none" as the comment says.
struct BlockOperands {
  ec::RecordingSite site;
  int snow_flag;      // a stack byte; -1: bh
  int snow_value;     // 0: the constant 0, 1: ah, 2: bl, 3: cl
  uint32_t cfg;       // the local that holds the colour state
  int diffuse_flag;   // -1: the block has no textures
  int diffuse;
  int normal_flag;
  int normal;
  int faction_flag;   // a stack byte; -1: the old faction instead (the default colours when it is not -1)
  int old_faction;    // a local; -1: edi
  int faction;        // the item's faction (a local)
  int item;           // a local; -1: ebx, which the game also keeps in [esp + 0x1c]
  bool reloaded;      // the block's exit reloads ebx: the test's exit stub is 4 bytes on
  bool wide;          // which of the two scratch sets the block writes (below)
};
constexpr std::array<BlockOperands, 5> BLOCKS = {{
    {ec::RecordingSite::COLOUR_B0_STATE, 0x0f, 0, 0x10, -1, -1, -1, -1, -1, -1, 0, -1, false, false},
    {ec::RecordingSite::COLOUR_B1_STATE, 0x11, 1, 0x18, -1, -1, -1, -1, 0x13, -1, 0x20, -1, true, true},
    {ec::RecordingSite::COLOUR_B2_STATE, -1, 2, 0x18, 0x13, 0x24, 0x10, 0x1c, 0x11, -1, 0x28, 0x20, false, true},
    {ec::RecordingSite::COLOUR_B3_STATE, 0x0e, 3, 0x10, -1, -1, -1, -1, -1, 0x1c, 0, -1, false, false},
    {ec::RecordingSite::COLOUR_B4_STATE, 0x11, 1, 0x18, -1, -1, -1, -1, 0x13, -1, 0x20, -1, true, true},
}};
// The frame bytes a block writes besides what the thunk replaces (the default colours it builds, the faction list's out pointer, the
// gather's vector): the scratch the game's code after the block never reads (checked on the game's bytes: no esp-relative access to
// them after the block's end in any of the 5 bodies). Inclusive ranges of esp offsets.
constexpr std::array<std::array<uint32_t, 2>, 3> NARROW_SCRATCH = {{{0x28, 0x2b}, {0x34, 0x37}, {0x40, 0x7f}}};
constexpr std::array<std::array<uint32_t, 2>, 7> WIDE_SCRATCH = {{{0x14, 0x17}, {0x2c, 0x33}, {0x38, 0x67}, {0x70, 0x73}, {0x7c, 0x7f}, {0x88, 0x8b}, {0x94, 0x97}}};

// Registers in x86 order.
enum Reg { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };
using Registers = std::array<uint32_t, 8>;
Registers run_in = {};
Registers run_out = {};
uint32_t run_saved_esp = 0;
uint32_t run_entry = 0;
uint32_t block_cell = 0;  // the snow constant's cell the colour state points at

// A cdecl void() that loads run_in, jumps to run_entry (the game's block or its thunk) and, at `exit` (where a jump is written in
// the block's end, in both images), saves the registers into run_out and returns.
struct BlockHarness {
  void(__cdecl* run)();
  const uint8_t* exit;
};
BlockHarness BuildBlockHarness() {
  auto* const page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  ec::Assembler assembler(std::span<uint8_t>(page, 0x1000));
  const auto memory = [&](uint8_t opcode, uint32_t reg, const void* where) {
    assembler.BytesAndOperand({opcode, static_cast<uint8_t>(0x05 | reg << 3)}, ec::Address(where));
  };
  BlockHarness harness = {reinterpret_cast<void(__cdecl*)()>(assembler.Cursor()), nullptr};
  assembler.Bytes({0x55, 0x53, 0x56, 0x57});  // push ebp, ebx, esi, edi
  memory(0x89, ESP, &run_saved_esp);
  memory(0x8B, ESP, &run_in[ESP]);
  for (const uint32_t reg : {EAX, ECX, EDX, EBX, EBP, ESI, EDI}) memory(0x8B, reg, &run_in[reg]);
  assembler.BytesAndOperand({0xFF, 0x25}, ec::Address(&run_entry));
  harness.exit = assembler.Cursor();
  for (uint32_t reg = EAX; reg <= EDI; ++reg) memory(0x89, reg, &run_out[reg]);
  memory(0x8B, ESP, &run_saved_esp);
  assembler.Bytes({0x5F, 0x5E, 0x5B, 0x5D, 0xC3});
  REQUIRE(assembler.Fits());
  return harness;
}

uint32_t block_random_state = 0x2545F491;
uint32_t BlockRandom() {
  block_random_state ^= block_random_state << 13;
  block_random_state ^= block_random_state >> 17;
  block_random_state ^= block_random_state << 5;
  return block_random_state;
}
uint32_t BlockRandom(uint32_t count) { return BlockRandom() % count; }

// An item as the block reads it: its faction mask at +0x34.
struct BlockItem {
  uint8_t pad[0x34];
  uint32_t mask;
  uint8_t pad2[0x40];
};

constexpr uint32_t BLOCK_FRAME_SIZE = 0xb0;

// What one run of a block leaves that the game's code after it can see.
struct BlockRun {
  std::vector<LogEntry> log;
  std::array<uint8_t, dr::TRACKER_SIZE> tracker = {};
  std::array<uint8_t, BLOCK_FRAME_SIZE> frame = {};
  uint32_t cell = 0;
  Registers out = {};
};

void TestStateBlocks() {
  const uint64_t mode = ec::RECORDING_OWN[static_cast<size_t>(dr::Group::COLOUR_B)] | ec::RECORDING_ENTRY_BIT;
  SetSites(mode);
  CheckWrittenSites(mode);
  // The thunks' slot test needs the arena (a slot's tracker is in it, the game's own is not).
  UpdateAt(4, {.stage = dr::Stage::WORKERS, .loops = 2u});
  REQUIRE(shared->arena_size != 0);
  const BlockHarness harness = BuildBlockHarness();
  // The fakes run on this stack below the frame (esp is the frame's start): room for the heap and the log, not the 0x180 bytes that
  // once let a deep call write into the neighbouring statics.
  alignas(16) static uint8_t frame_memory[0x2400];
  alignas(16) static uint32_t ebp_block[8];
  ebp_block[4] = reinterpret_cast<uint32_t>(&colour_node);
  alignas(16) static uint8_t tracker_plain[dr::TRACKER_SIZE];
  alignas(16) static uint8_t tracker_initial[dr::TRACKER_SIZE];
  static std::array<BlockItem, 3> items = {};
  items[1].mask = reinterpret_cast<uint32_t>(&masks[1]);
  items[2].mask = reinterpret_cast<uint32_t>(&masks[3]);
  uint8_t* const cfg = state_owner + STATE_OFFSET;
  Write32(cfg + 0x2ac + 0x14, reinterpret_cast<uint32_t>(&block_cell));
  uint8_t* const frame_at = frame_memory + 0x2000;
  const uint32_t esp = reinterpret_cast<uint32_t>(frame_at);
  uint32_t cases = 0;
  std::array<uint32_t, 4> parts_seen = {};  // the cases with a snow constant, a texture bind, the default colours, the item's colours
  uint32_t masked = 0;
  std::array<uint32_t, 3> calls_seen = {};  // dirty calls, binds, faction colour calls the game made
  for (const BlockOperands& operands : BLOCKS) {
    const ec::RecordingSiteInfo& info = ec::RECORDING_INFO[static_cast<size_t>(operands.site)];
    const uint32_t exit_rva = info.exit_rva + (operands.reloaded ? 4 : 0);
    std::array<std::array<uint8_t, 5>, 2> saved = {};
    std::memcpy(saved[0].data(), game + exit_rva, 5);
    std::memcpy(saved[1].data(), patched + exit_rva, 5);
    JumpTo(game + exit_rva, harness.exit);
    JumpTo(patched + exit_rva, harness.exit);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    for (uint32_t index = 0; index < 3000; ++index) {
      // The inputs: the frame random but for the operands the block reads, which the test gives in the game's own types.
      std::array<uint8_t, BLOCK_FRAME_SIZE> frame = {};
      for (uint8_t& byte : frame) byte = static_cast<uint8_t>(BlockRandom());
      Registers regs = {BlockRandom(), BlockRandom(), BlockRandom(), BlockRandom(), 0, 0, 0, BlockRandom()};
      const auto poke8 = [&](int offset, uint32_t value) { frame[static_cast<size_t>(offset)] = static_cast<uint8_t>(value); };
      const auto poke32 = [&](int offset, uint32_t value) { std::memcpy(frame.data() + offset, &value, 4); };
      const auto flag = [] { return BlockRandom(2) == 0 ? 0u : 1 + BlockRandom(255); };
      const uint8_t snow_byte = static_cast<uint8_t>(BlockRandom());
      const uint32_t snow = flag();
      if (operands.snow_flag >= 0) {
        poke8(operands.snow_flag, snow);
      } else {
        regs[EBX] = (regs[EBX] & 0xFFFF00FF) | snow << 8;
      }
      if (operands.snow_value == 1) regs[EAX] = (regs[EAX] & 0xFFFF00FF) | snow_byte << 8;
      if (operands.snow_value == 2) regs[EBX] = (regs[EBX] & 0xFFFFFF00) | snow_byte;
      if (operands.snow_value == 3) regs[ECX] = (regs[ECX] & 0xFFFFFF00) | snow_byte;
      poke32(static_cast<int>(operands.cfg), reinterpret_cast<uint32_t>(cfg));
      if (operands.diffuse_flag >= 0) {
        poke8(operands.diffuse_flag, flag());
        poke32(operands.diffuse, reinterpret_cast<uint32_t>(&textures[BlockRandom(static_cast<uint32_t>(textures.size()))]));
        poke8(operands.normal_flag, flag());
        poke32(operands.normal, reinterpret_cast<uint32_t>(&textures[BlockRandom(static_cast<uint32_t>(textures.size()))]));
      }
      const uint32_t item = reinterpret_cast<uint32_t>(&items[BlockRandom(3)]);
      constexpr uint32_t FACTIONS[] = {0xFFFFFFFF, 0xFFFFFFFF, 0, 1, 2, 3, 4, 9};
      if (operands.faction_flag >= 0) {
        poke8(operands.faction_flag, flag());
        poke32(operands.faction, FACTIONS[BlockRandom(8)]);
        if (operands.item < 0) {
          regs[EBX] = item;
          poke32(0x1c, item);
        } else {
          poke32(operands.item, item);
        }
      } else {
        // The old faction: -1 (no default colours) or another value (the default colours).
        const uint32_t old_faction = (BlockRandom(2) == 0 ? 0xFFFFFFFF : FACTIONS[2 + BlockRandom(6)]);
        if (operands.old_faction < 0) {
          regs[EDI] = old_faction;
        } else {
          poke32(operands.old_faction, old_faction);
        }
      }
      // The snow constant's cell: the value the block will give it, or not.
      const float snow_value = (operands.snow_value == 0 ? 0.0f : static_cast<float>(snow_byte));
      uint32_t cell = BlockRandom();
      if (BlockRandom(3) == 0) std::memcpy(&cell, &snow_value, 4);
      const uint8_t cache = static_cast<uint8_t>(BlockRandom(2));
      for (uint8_t& byte : tracker_initial) byte = static_cast<uint8_t>(BlockRandom());

      // One run: the game's block in A, B's thunk on a tracker outside the arena (esi = tracker_plain) or on a slot's (esi in it).
      const auto run = [&](uint8_t* image, uint8_t* tracker) {
        game[CACHE_FLAG_RVA] = patched[CACHE_FLAG_RVA] = cache;
        block_cell = cell;
        std::memcpy(frame_at, frame.data(), frame.size());
        run_in = regs;
        run_in[ESP] = esp;
        run_in[EBP] = reinterpret_cast<uint32_t>(ebp_block);
        run_in[ESI] = reinterpret_cast<uint32_t>(tracker);
        run_entry = reinterpret_cast<uint32_t>(image + info.rva);
        game_log.clear();
        harness.run();
        BlockRun result = {.log = game_log, .cell = block_cell, .out = run_out};
        std::memcpy(result.tracker.data(), tracker, dr::TRACKER_SIZE);
        std::memcpy(result.frame.data(), frame_at, frame.size());
        // The gather's vector (a local of the wide variants, the ones that gather an item's faction colours) holds a heap address
        // that differs from run to run.
        if (operands.wide) std::memset(result.frame.data() + 0x34, 0, 4);
        return result;
      };
      std::memcpy(tracker_plain, tracker_initial, sizeof(tracker_plain));
      const BlockRun reference = run(game, tracker_plain);
      std::memcpy(tracker_plain, tracker_initial, sizeof(tracker_plain));
      const BlockRun direct = run(patched, tracker_plain);
      CHECK(direct.log == reference.log && direct.tracker == reference.tracker && direct.frame == reference.frame
            && direct.cell == reference.cell && direct.out == reference.out);
      // The block writes the tracker's first 0x14 bytes and nothing past them; the registers it leaves that the game's code after
      // it relies on are esp, ebp, esi and, where the game reloads it, ebx.
      CHECK(std::memcmp(reference.tracker.data() + 0x14, tracker_initial + 0x14, dr::TRACKER_SIZE - 0x14) == 0);
      CHECK(reference.out[ESP] == esp && reference.out[EBP] == reinterpret_cast<uint32_t>(ebp_block));
      if (operands.reloaded) CHECK(reference.out[EBX] == Read32(frame_at + 0x1c));
      // On a slot: Record lays one op and writes the tracker's first 0x14 bytes as the game does; the replay makes the game's calls.
      uint32_t op_used = 0;
      dr::Slot* const slot = di::PrepareSlot(0, static_cast<uint32_t>(dr::Group::COLOUR_B), tracker_initial, 4096, &op_used);
      const BlockRun recorded = run(patched, slot->tracker.data());
      CHECK(slot->flags == 0 && slot->ops_cursor - slot->ops_begin == 1 + di::STATE_PAYLOAD
            && (slot->ops_begin[0] & 0xffff) == static_cast<uint32_t>(di::OpKind::STATE) && (slot->ops_begin[0] >> 16) == 1 + di::STATE_PAYLOAD);
      CHECK(recorded.log.empty() && recorded.cell == cell);  // nothing is called while recording
      CHECK(std::memcmp(recorded.tracker.data(), reference.tracker.data(), 0x14) == 0);
      // The thunk leaves the frame as it was; the game's block changes it only in its scratch.
      std::array<uint8_t, BLOCK_FRAME_SIZE> unchanged = frame;
      if (operands.wide) std::memset(unchanged.data() + 0x34, 0, 4);
      CHECK(recorded.frame == unchanged);
      for (uint32_t at = 0; at < BLOCK_FRAME_SIZE; ++at) {
        if (reference.frame[at] == unchanged[at]) continue;
        const auto in_range = [at](const auto& range) { return at >= range[0] && at <= range[1]; };
        CHECK(operands.wide ? std::any_of(WIDE_SCRATCH.begin(), WIDE_SCRATCH.end(), in_range)
                            : std::any_of(NARROW_SCRATCH.begin(), NARROW_SCRATCH.end(), in_range));
      }
      CHECK(recorded.out[ESP] == esp && recorded.out[EBP] == reinterpret_cast<uint32_t>(ebp_block) && recorded.out[ESI] == reinterpret_cast<uint32_t>(slot->tracker.data()));
      if (operands.reloaded) CHECK(recorded.out[EBX] == Read32(frame_at + 0x1c));
      dr::StateBlock block = {};
      std::memcpy(&block, slot->ops_begin + 1, sizeof(block));
      game_log.clear();
      block_cell = cell;
      game[CACHE_FLAG_RVA] = patched[CACHE_FLAG_RVA] = cache;
      di::ReplayState(block);
      CHECK(game_log == reference.log && block_cell == reference.cell);
      ++cases;
      parts_seen[0] += ((block.parts & dr::STATE_SNOW) != 0 ? 1 : 0);
      parts_seen[1] += ((block.parts & (dr::STATE_DIFFUSE | dr::STATE_NORMAL)) != 0 ? 1 : 0);
      parts_seen[2] += ((block.parts & dr::STATE_FACTION) != 0 && block.faction == -1 ? 1 : 0);
      parts_seen[3] += ((block.parts & dr::STATE_FACTION) != 0 && block.faction != -1 ? 1 : 0);
      masked += ((block.parts & dr::STATE_FACTION) != 0 && block.mask != 0 ? 1 : 0);
      for (const LogEntry& entry : reference.log) {
        if (entry[0] >= 4 && entry[0] <= 6) ++calls_seen[entry[0] - 4];
      }
    }
    std::memcpy(game + exit_rva, saved[0].data(), 5);
    std::memcpy(patched + exit_rva, saved[1].data(), 5);
  }
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  CHECK(parts_seen[0] > 1000 && parts_seen[1] > 300 && parts_seen[2] > 100 && parts_seen[3] > 100 && masked > 50);
  CHECK(calls_seen[0] > 300 && calls_seen[1] > 300 && calls_seen[2] > 300);
  std::printf("7. colour B state blocks, the game's own bytes against the thunk and the replay: %u random cases over the 5 variants (snow constant in %u, shader resources in %u, the default colours in %u, an item's faction in %u, %u of them with a mask; the game made %u constant, %u bind and %u faction calls): the thunk with the tracker outside the arena equals the game (log, tracker, frame, registers, cell); on a slot it records one op, Record writes the tracker's first 0x14 bytes as the game does, nothing is called, and the replay's calls and cell equal the game's; esp, ebp and esi as they were, ebx as the game reloads it\n",
              cases, parts_seen[0], parts_seen[1], parts_seen[2], parts_seen[3], masked, calls_seen[0], calls_seen[1], calls_seen[2]);
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
  colour_bytes_b.assign(patched + COLOUR_LOOP_RVA, patched + COLOUR_LOOP_END_RVA);
  std::printf("empire.retail.dll build %08lX, %zu bytes: mapped twice (relocated, never loaded)\n", nt->FileHeader.TimeDateStamp, file.size());

  // B prepared by engine_code with the DLL as it ships, then the fakes in both; the sites are written by its PausedWrite.
  REQUIRE(ec::PrepareRecording(reinterpret_cast<HMODULE>(patched)));
  REQUIRE(ec::recording_found == (uint64_t{1} << ec::RECORDING_SITE_COUNT) - 1);
  REQUIRE(di::game_calls.usable && di::game_calls.loops[static_cast<size_t>(dr::Group::DEPTH_A)] == ec::Address(patched + LOOP_RVA));
  REQUIRE(di::game_calls.loops[static_cast<size_t>(dr::Group::COLOUR_A)] == ec::Address(patched + COLOUR_LOOP_RVA));
  REQUIRE(((di::game_calls.usable_groups >> static_cast<size_t>(dr::Group::COLOUR_A)) & 1) != 0);
  shared = dr::SharedPage();
  REQUIRE(shared != nullptr);
  flush_body = BuildFlushBody();
  empty_flush_exit = BuildEmptyFlushExit();
  main_thread_id = GetCurrentThreadId();
  FillSlots(unknown_vtable.data());
  stride_vtable.fill(0);
  stride_vtable[0x14 / 4] = reinterpret_cast<uint32_t>(&FakeStride);
  for (uint32_t kind = 0; kind < stride_objects.size(); ++kind) stride_objects[kind] = {.vtable = stride_vtable.data(), .kind = kind};
  node_vtable.fill(0);
  node_vtable[0xc0 / 4] = reinterpret_cast<uint32_t>(&FakeFactionList);
  for (uint32_t k = 0; k < masks.size(); ++k) {
    const uint8_t picks[4][4] = {{3, 1, 0, 2}, {0, 0, 1, 1}, {2, 3, 2, 3}, {7, 6, 5, 4}};
    std::memcpy(masks[k].picks, picks[k], 4);
    masks[k].index = static_cast<int32_t>(k);
  }
  for (uint32_t i = 0; i < textures.size(); ++i) textures[i].view = (i == textures.size() - 1 ? 0 : 0x100 + 0x10 * i);
  for (uint32_t faction = 0; faction < faction_lists.size(); ++faction) {
    for (uint32_t i = 0; i < faction_colours[faction].size(); ++i) faction_colours[faction][i] = static_cast<float>(faction * 100 + i) + 0.5f;
    // Faction 3's list has fewer than 4 colours: nothing is sent for it.
    faction_lists[faction] = {0, (faction == 3 ? 2u : 4 + faction), faction_colours[faction].data()};
  }
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
  TestColourImages();
  TestColourStages();
  TestColourControls();
  TestMeshBeforePlan();
  TestGroupsIn();
  TestStateBlocks();
  SetSites(0);
  CHECK(CheckWrittenSites(0) == 0 && CheckWrittenSites(0, true) == 0);  // the sites out: the game's own bytes again
  CHECK(std::memcmp(patched + LOOP_RVA, loop_bytes_b.data(), loop_bytes_b.size()) == 0);
  CHECK(std::memcmp(patched + COLOUR_LOOP_RVA, colour_bytes_b.data(), colour_bytes_b.size()) == 0);
  UpdateAt(1, {.stage = dr::Stage::OFF});
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}

#endif
