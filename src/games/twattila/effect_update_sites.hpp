/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <windows.h>

#include <cstdint>

#if !defined(_WIN64)
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <format>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../utils/log.hpp"
#include "../twrome2/engine_tweaks.hpp"
#endif

// Parallel Effect Update's sites in empire.retail.dll (Attila 1.6.0, x86): the 27 places where the game's effect update
// touches state that several effects share (the particle spawn buffer, the random generator, the sound system, the
// emitter slot pool, children, rigid adds, ...), and the walk that calls it for every effect. This file owns the table of
// sites, the thunks that each site's jump goes to, the words they read and the checks that keep the sites out of
// code that is not the game's as expected. The scheduler that decides what a site does (effect_update.hpp, a later step)
// plugs in as the `Handler`; until then every site is consulted only while `walk_armed` / `walk_active` are set, and
// nothing sets them: the sites are in the game's code but never change what it does.
//
// A site is an E9 (a jump) over whole instructions of the game, padded with NOPs, or an E8 (a call) retargeted. Every
// thunk is one of three paths:
//   original: the game's own instructions that the jump replaced, then a jump to the code after them (or the call's own
//             target); this is all a game thread ever runs while no walk is open;
//   fast:     while a walk is open, the thread that runs the one direct (oldest) unit goes on the original path without
//             calling anything: `walk_active`, then `direct_high - esp < limit` (one word, `direct_high` the HighLimit of
//             the direct thread's stack, `limit` the smallest stack reservation of any thread that may be direct: stacks
//             never overlap, so for any other thread the difference is past `limit`, and 0 means nobody is direct);
//   slow:     everything else calls `Handler` in a frame of its own (pushfd, pushad, a 16-aligned fxsave of the vector
//             state, the frame's address and the site id on the stack, cdecl). The handler returns an action: 0 runs the
//             original path, 1 returns from the function with the Frame's eax (only the sites with a `return_bytes`).
//             `active` counts the threads from the first instruction of the slow path to the last before its popad
//             (E8 sites from before the game's callee is called): PausedWrite takes the sites out only while it is 0 and
//             no thread is anywhere in the thunk page.
// No return address into the thunk page ever sits on a game thread's stack outside a counted region: the E8 thunks call
// the original callee only after `lock inc active`, and the walk end's call of the game's free function is a push and a jump
// (the callee returns straight into the game).
namespace twattila::effect_update {

#if !defined(_WIN64)

enum class Site : uint8_t {
  WALK_CALL,
  WALK_END,
  SPAWN_QUERY,
  SPAWN_WRITE,
  RANDOM_TRIPLE,
  SURFACE_RANDOM,
  SLOT_TAKE,
  SLOT_RETURN,
  SOUND_START,
  SOUND_HANDLE,
  SOUND_PLAY,
  SOUND_EMITTER,
  EVENT_DISPATCH,
  RIGID_CREATE,
  BURST_1,
  RIGID_ADD,
  TRACKED_CHILD,
  CHILD_CREATE_A,
  CHILD_CREATE_B,
  CHILD_APPEND_A,
  CHILD_APPEND_B,
  CREATE_RANDOM,
  TYPE_ADD_A,
  TYPE_ADD_B,
  ATTACH_PAIR,
  ATTACH_LIST,
  RANDOM_FILL,
  COUNT,
};
constexpr size_t SITE_COUNT = static_cast<size_t>(Site::COUNT);
constexpr uint32_t ALL_SITES = (1u << SITE_COUNT) - 1;
static_assert(SITE_COUNT == 27);

// How a site's thunk is built.
enum class Kind : uint8_t {
  ORDER,        // E9 over whole instructions; the walk's gate, then the direct thread's pass
  WALK_CALL,    // E8 of the walk's call of the per-effect function; its gate is `walk_armed`; action 1 returns (`ret 0xC`)
  WALK_END,     // E9 over `lea ecx, [esp + 0x60]; call <free>`: the gate only; the original path jumps to the free function
  APPEND_CALL,  // E8 of a call of the game's append: runs it, then reports (the effect and the appended pointer)
};

struct SiteInfo {
  std::string_view name;
  uint32_t rva;
  std::string_view pattern;  // the bytes the site replaces, `??` for a relocated or relative operand; its length is the site's
  Kind kind = Kind::ORDER;
  uint32_t callee_rva = 0;       // E8 sites (and the walk end's call): the call's target
  std::string_view callee_head;  // its first bytes ("" : not checked)
  int16_t return_bytes = -1;     // ORDER and WALK_CALL sites that may answer for the function: its `ret` operand; -1: never
};

constexpr size_t MAX_SITE_SIZE = 11;
constexpr size_t SizeOf(const SiteInfo& info) { return (info.pattern.size() + 1) / 3; }

// The Steam build's RVAs (nothing else is supported: another build fails the bytes check and B1 stays out).
constexpr std::array<SiteInfo, SITE_COUNT> SITES = {{
    {.name = "walk call",
     .rva = 0x192D5C4,
     .pattern = "E8 ?? ?? ?? ??",
     .kind = Kind::WALK_CALL,
     .callee_rva = 0x1893330,
     .callee_head = "83 EC 20 57 8B F9",
     .return_bytes = 0xC},
    {.name = "walk end",
     .rva = 0x192D65A,
     .pattern = "8D 4C 24 60 E8 ?? ?? ?? ??",
     .kind = Kind::WALK_END,
     .callee_rva = 0xE06F0},
    {.name = "spawn query", .rva = 0x18F90F0, .pattern = "8B 89 80 01 00 00", .return_bytes = 0},
    {.name = "spawn write", .rva = 0x1927090, .pattern = "8B 89 80 01 00 00", .return_bytes = 4},
    {.name = "random triple", .rva = 0x190B780, .pattern = "69 15 ?? ?? ?? ?? FD 43 03 00", .return_bytes = 0},
    {.name = "surface random", .rva = 0x183FA94, .pattern = "B9 ?? ?? ?? ??"},
    {.name = "slot take", .rva = 0x183C980, .pattern = "56 8B F1 57 8B 7C 24 0C"},
    {.name = "slot return", .rva = 0x17F9D80, .pattern = "56 8B 74 24 08"},
    {.name = "sound start", .rva = 0x1891FF0, .pattern = "8B 0D ?? ?? ?? ??"},
    {.name = "sound handle", .rva = 0x1892042, .pattern = "8B 0D ?? ?? ?? ??"},
    {.name = "sound play", .rva = 0x1892080, .pattern = "8B 0D ?? ?? ?? ??"},
    {.name = "sound emitter", .rva = 0x18999B1, .pattern = "F3 0F 10 44 24 18"},
    {.name = "event dispatch", .rva = 0x18637C0, .pattern = "8B 54 24 04 8B 42 10"},
    {.name = "rigid create", .rva = 0x181BF10, .pattern = "83 EC 5C 56 8B F1"},
    {.name = "burst statics", .rva = 0x1895130, .pattern = "81 EC 94 00 00 00"},
    {.name = "rigid add", .rva = 0x17F9450, .pattern = "A1 ?? ?? ?? ??", .return_bytes = 4},
    {.name = "tracked child", .rva = 0x187AEC0, .pattern = "FF 74 24 04 8B 4C 24 0C"},
    {.name = "child create A", .rva = 0x1819320, .pattern = "83 EC 08 8A 44 24 1C"},
    {.name = "child create B", .rva = 0x1819270, .pattern = "83 EC 08 8A 44 24 14"},
    {.name = "child append A",
     .rva = 0x18193A6,
     .pattern = "E8 ?? ?? ?? ??",
     .kind = Kind::APPEND_CALL,
     .callee_rva = 0x1873820,
     .callee_head = "56 8B F1 8B 4E 04 8B 06"},
    {.name = "child append B",
     .rva = 0x18192EC,
     .pattern = "E8 ?? ?? ?? ??",
     .kind = Kind::APPEND_CALL,
     .callee_rva = 0x1873820,
     .callee_head = "56 8B F1 8B 4E 04 8B 06"},
    {.name = "create random", .rva = 0x181A930, .pattern = "83 EC 10 F3 0F 10 15 ?? ?? ?? ??"},
    {.name = "type add A", .rva = 0x190BF50, .pattern = "8B 54 24 04 8B 81 48 01 00 00"},
    {.name = "type add B", .rva = 0x190BF30, .pattern = "8D 81 08 01 00 00"},
    {.name = "attach pair", .rva = 0x17F16B0, .pattern = "83 EC 08 53 55"},
    {.name = "attach list", .rva = 0x17FA1B0, .pattern = "56 8D 44 24 08"},
    {.name = "random fill", .rva = 0x184DA60, .pattern = "83 EC 14 56 8B F1", .return_bytes = 8},
}};

// The heap fix's presort owns +0x192D2DA .. +0x192D2DE (its fourth patch): no site of ours may touch it.
constexpr uint32_t PRESORT_BEGIN = 0x192D2DA;
constexpr uint32_t PRESORT_END = 0x192D2DF;
constexpr bool SitesFit() {
  for (const SiteInfo& info : SITES) {
    const uint32_t end = info.rva + static_cast<uint32_t>(SizeOf(info));
    if (SizeOf(info) > MAX_SITE_SIZE || (info.rva < PRESORT_END && end > PRESORT_BEGIN)) return false;
    // A callee's operand is the whole 4 bytes of an E8 at the pattern's end.
    if (info.kind == Kind::WALK_CALL || info.kind == Kind::APPEND_CALL) {
      if (SizeOf(info) != 5) return false;
    }
  }
  return true;
}
static_assert(SitesFit());

// Functions that must not start with an E9 that is not ours (another tool's hook at one: its stack and its register use
// are not ours to share with workers). The sites' own entries are in it: they start with our E9 once written.
constexpr std::array<uint32_t, 35> HOOKED_ENTRIES = {
    0x1893470, 0x1892960, 0x1892D10, 0x1895AF0, 0x18960D0, 0x181BDC0, 0x18297F0, 0x183FA30, 0x1893330, 0x1891FA0,
    0x1892010, 0x1899980, 0x17D9B80, 0x1895130, 0x17F9450, 0x187AEC0, 0x1819320, 0x1819270, 0x1873820, 0x181A930,
    0x190B780, 0x181BF10, 0x18637C0, 0x183C980, 0x17F9D80, 0x18F90F0, 0x1927090, 0x1929E60, 0x17F16B0, 0x17FA1B0,
    0x190BF50, 0x190BF30, 0x184DA60, 0x192D290, 0x1898C90};

// What a thunk's slow path hands the handler: what pushfd and pushad leave on the stack (lowest address first), then the
// game's own stack at the site: for a function's entry its return address and its arguments; for a site inside a
// function the stack as the instruction there sees it. Writing `eax` and answering 1 returns it from the function.
struct Frame {
  uint32_t edi;
  uint32_t esi;
  uint32_t ebp;
  uint32_t esp;  // pushad's copy: the stack before it, the eflags word below
  uint32_t ebx;
  uint32_t edx;
  uint32_t ecx;
  uint32_t eax;
  uint32_t eflags;
  uint32_t stack[1];
};

// The slow path's call: cdecl, `site` is a Site. 0: the original path; 1: return from the function (the sites with a
// `return_bytes`; for the others 1 means 0).
using Handler = uint32_t(__cdecl*)(Frame* frame, uint32_t site);

// The thunks' words, on a page of their own (never executable). The walk's gates and the direct thread's stack word are
// read by every thunk, so they have a cache line each: the count on one, the (rarely written) gates and the limit
// on another, the direct word (written at every handover) on the third.
struct Data {
  alignas(64) std::atomic<int32_t> active = 0;  // +0x00: threads in a counted part of a thunk
  alignas(64) std::atomic<uint32_t> walk_armed = 0;  // +0x40: the walk's call asks the handler (a round is open or may open)
  std::atomic<uint32_t> walk_active = 0;             // +0x44: a round is open: the order points are live
  std::atomic<uint32_t> limit = 0;                   // +0x48: the smallest stack reservation of any thread that may be direct
  std::atomic<uint32_t> handler = 0;                 // +0x4C: the Handler
  alignas(64) std::atomic<uint32_t> direct_high = 0;  // +0x80: the direct thread's stack HighLimit; 0: none
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  std::atomic<uint32_t> direct_low = 0;  // +0x84: the control's low word
#endif
};
static_assert(offsetof(Data, walk_armed) == 0x40 && offsetof(Data, handler) == 0x4C && offsetof(Data, direct_high) == 0x80
              && sizeof(Data) <= 0x1000);

// The handler a thunk has until a scheduler is installed: the original path.
inline uint32_t __cdecl PassThrough(Frame*, uint32_t) { return 0; }

#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
// The controls of test/tw_engine_code: thunks built wrong on purpose, which the tests must catch.
static bool control_no_vector_save = false;  // no fxsave / fxrstor around the handler
static bool control_torn_pair = false;       // the stack's range as a low and a high word, read separately
#endif

namespace internal {

inline uint32_t Address(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

// Machine code written into the thunk page. A thunk that does not fit fails the whole preparation.
class Assembler {
 public:
  explicit Assembler(std::span<uint8_t> memory) : cursor_(memory.data()), limit_(memory.data() + memory.size()) {}

  uint8_t* Cursor() const { return cursor_; }
  bool Fits() const { return fits_; }

  void Bytes(std::initializer_list<uint8_t> bytes) {
    if (cursor_ + bytes.size() > limit_) {
      fits_ = false;
      return;
    }
    for (const uint8_t byte : bytes) {
      *cursor_++ = byte;
    }
  }

  // The game's own instructions as the loaded image has them (absolute operands relocated).
  void Copy(const uint8_t* bytes, size_t size) {
    if (cursor_ + size > limit_) {
      fits_ = false;
      return;
    }
    std::memcpy(cursor_, bytes, size);
    cursor_ += size;
  }

  // The bytes and a 32-bit operand: an absolute address or an immediate.
  void BytesAndOperand(std::initializer_list<uint8_t> bytes, uint32_t operand) {
    Bytes(bytes);
    if (cursor_ + sizeof(operand) > limit_) {
      fits_ = false;
      return;
    }
    std::memcpy(cursor_, &operand, sizeof(operand));
    cursor_ += sizeof(operand);
  }

  // A call or jump (E8 / E9) to `target`.
  void Relative(uint8_t opcode, const void* target) {
    BytesAndOperand({opcode}, Address(target) - (Address(cursor_) + 5));
  }

  // A conditional jump forward (0F 80+cc, 32-bit displacement): Bind where it lands.
  uint8_t* Branch(uint8_t condition) {
    BytesAndOperand({0x0F, condition}, 0);
    return (fits_ ? cursor_ - 4 : nullptr);
  }

  void Bind(uint8_t* displacement) {
    if (displacement == nullptr) return;
    const uint32_t offset = Address(cursor_) - (Address(displacement) + 4);
    std::memcpy(displacement, &offset, sizeof(offset));
  }

 private:
  uint8_t* cursor_;
  uint8_t* limit_;
  bool fits_ = true;
};

// The slow path: from the first push to the handler's return and the count's decrement; `counted`: the count was raised by
// the caller (the E8 thunks, before they call the game's function). The action is in ebx afterwards (callee saved, and
// popad has not run yet).
inline void EmitSlowPath(Assembler* assembler, const Data* data, Site site, bool counted) {
  assembler->Bytes({0x9C, 0x60});  // pushfd; pushad
  if (!counted) assembler->BytesAndOperand({0xF0, 0xFF, 0x05}, Address(&data->active));  // lock inc [active]
  assembler->Bytes({0x8B, 0xEC, 0x83, 0xE4, 0xF0});                                      // mov ebp, esp; and esp, -16
  assembler->BytesAndOperand({0x81, 0xEC}, 512);                                         // sub esp, 512
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
  const bool save_vector_state = !control_no_vector_save;
#else
  constexpr bool save_vector_state = true;
#endif
  if (save_vector_state) assembler->Bytes({0x0F, 0xAE, 0x04, 0x24});                      // fxsave [esp]
  assembler->BytesAndOperand({0x68}, static_cast<uint32_t>(site));                        // push the site
  assembler->Bytes({0x55});                                                              // push ebp: the Frame
  assembler->BytesAndOperand({0xFF, 0x15}, Address(&data->handler));                     // call [handler]
  assembler->Bytes({0x83, 0xC4, 0x08, 0x8B, 0xD8});                                      // add esp, 8; mov ebx, eax
  if (save_vector_state) assembler->Bytes({0x0F, 0xAE, 0x0C, 0x24});                      // fxrstor [esp]
  assembler->Bytes({0x8B, 0xE5});                                                        // mov esp, ebp
  assembler->BytesAndOperand({0xF0, 0xFF, 0x0D}, Address(&data->active));                // lock dec [active]
}


// The thunk of site `index` for the game's code at `at`; `callee` the call's target (the E8 sites and the walk end),
// `resume` the code after the replaced instructions. Returns the thunk's first byte.
inline uint8_t* EmitThunk(Assembler* assembler, size_t index, const Data* data, const uint8_t* at, const uint8_t* callee,
                          const uint8_t* resume) {
  const SiteInfo& info = SITES[index];
  const Site site = static_cast<Site>(index);
  uint8_t* const entry = assembler->Cursor();
  const uint32_t gate = Address(info.kind == Kind::WALK_CALL ? &data->walk_armed : &data->walk_active);
  assembler->BytesAndOperand({0x83, 0x3D}, gate);  // cmp dword ptr [gate], 0
  assembler->Bytes({0x00});
  std::array<uint8_t*, 2> to_original = {assembler->Branch(0x84), nullptr};  // je original

  if (info.kind == Kind::APPEND_CALL) {
    // The game's append is thiscall with one argument (`ret 4`) that changes ecx: count, call it with a copy of the
    // argument, take ecx back, report, return as the callee would have.
    assembler->BytesAndOperand({0xF0, 0xFF, 0x05}, Address(&data->active));  // lock inc [active]
    assembler->Bytes({0x51, 0xFF, 0x74, 0x24, 0x08});                         // push ecx; push dword ptr [esp + 8]
    assembler->Relative(0xE8, callee);
    assembler->Bytes({0x59});  // pop ecx
    EmitSlowPath(assembler, data, site, true);
    assembler->Bytes({0x61, 0x9D, 0xC2, 0x04, 0x00});  // popad; popfd; ret 4
    assembler->Bind(to_original[0]);
    assembler->Relative(0xE9, callee);
    return entry;
  }

  if (info.kind == Kind::ORDER) {
#if defined(TWATTILA_EFFECT_UPDATE_CONTROLS)
    if (control_torn_pair) {
      // The first design's two loads: a thread preempted between them can see a low and a high of two different threads.
      assembler->BytesAndOperand({0x3B, 0x25}, Address(&data->direct_low));  // cmp esp, [low]
      uint8_t* const to_slow = assembler->Branch(0x82);                       // jb slow
      assembler->BytesAndOperand({0x3B, 0x25}, Address(&data->direct_high));  // cmp esp, [high]
      to_original[1] = assembler->Branch(0x82);                                // jb original
      assembler->Bind(to_slow);
    } else
#endif
    {
      // push eax; mov eax, [high]; sub eax, esp; cmp eax, [limit]; pop eax (pop keeps the flags); jb original
      assembler->Bytes({0x50});
      assembler->BytesAndOperand({0xA1}, Address(&data->direct_high));
      assembler->Bytes({0x2B, 0xC4});
      assembler->BytesAndOperand({0x3B, 0x05}, Address(&data->limit));
      assembler->Bytes({0x58});
      to_original[1] = assembler->Branch(0x82);
    }
  }

  EmitSlowPath(assembler, data, site, false);
  const bool answers = (info.return_bytes >= 0);
  uint8_t* to_answer = nullptr;
  if (answers) {
    assembler->Bytes({0x85, 0xDB});       // test ebx, ebx
    to_answer = assembler->Branch(0x85);  // jnz answer
  }
  assembler->Bytes({0x61, 0x9D});  // popad; popfd
  for (uint8_t* const displacement : to_original) {
    assembler->Bind(displacement);
  }
  switch (info.kind) {
    case Kind::WALK_CALL:
      assembler->Relative(0xE9, callee);
      break;
    case Kind::WALK_END:
      // The game's `lea`, then its call as a push and a jump: the callee returns into the game, no address of ours.
      assembler->Copy(at, 4);
      assembler->BytesAndOperand({0x68}, Address(resume));
      assembler->Relative(0xE9, callee);
      break;
    default:
      assembler->Copy(at, SizeOf(info));
      assembler->Relative(0xE9, resume);
      break;
  }
  if (answers) {
    assembler->Bind(to_answer);
    assembler->Bytes({0x61, 0x9D});  // popad; popfd
    if (info.return_bytes == 0) {
      assembler->Bytes({0xC3});
    } else {
      assembler->Bytes({0xC2, static_cast<uint8_t>(info.return_bytes & 0xFF), static_cast<uint8_t>(info.return_bytes >> 8)});
    }
  }
  return entry;
}

// One site as Prepare made it: where, the game's bytes, the bytes that go in (E9 or E8 and the displacement to the thunk,
// NOPs after).
struct PatchedSite {
  uint8_t* at = nullptr;
  uint8_t size = 0;
  std::array<uint8_t, MAX_SITE_SIZE> game = {};
  std::array<uint8_t, MAX_SITE_SIZE> patch = {};
};

constexpr size_t CODE_SIZE = 0x2000;  // the thunks (read-execute once built); the Data page follows, read-write for good

static std::array<PatchedSite, SITE_COUNT> sites;
static std::array<const uint8_t*, SITE_COUNT> thunks = {};
static const uint8_t* base = nullptr;
static uint8_t* page = nullptr;      // the thunk page
static uint8_t* page_end = nullptr;  // its end
static Data* data = nullptr;
static bool prepared = false;
static bool refused = false;  // an entry hooked by another tool at a turn-on: nothing goes in again
static uint32_t found = 0;    // ALL_SITES, or 0: the sites go in whole or not at all
static uint32_t written = 0;  // the sites holding their jump (ALL_SITES or 0, or what stood down on its way out left)
static uint32_t asked = 0;    // what the row last asked for

// The branches (jmp, jcc, call, loop with a rel8 or rel32 target) of `code` (the game's bytes from `first_rva`) whose
// target is inside a patched range past its first byte, by the branch's RVA: no byte pattern knows the instruction
// boundaries, so a byte of another instruction that looks like a branch is a hit too (the sources the shipped build
// has are listed in IGNORED_SOURCES, each one read in the disassembly).
// A thread that jumped into a range would run the middle of our jump: the shipped build has none (test/tw_engine_code scans the
// whole code section, Prepare the 2 KB around each site).
// +17F94C5 and +1819284 / +1819333: the immediate of a `push` (0x78; the 0xE0 of 0x12A5EDE0); +181A95C: the address operand of an
// `imul` (0x11DA7408); +192D661: the displacement of the call at +192D65E (inside the walk end's own site).
constexpr std::array<uint32_t, 5> IGNORED_SOURCES = {0x17F94C5, 0x1819284, 0x1819333, 0x181A95C, 0x192D661};

inline size_t BranchesIntoSites(std::span<const uint8_t> code, uint32_t first_rva, std::vector<uint32_t>* hits) {
  size_t count = 0;
  const auto read32 = [&](size_t at) {
    uint32_t value;
    std::memcpy(&value, code.data() + at, sizeof(value));
    return value;
  };
  for (size_t at = 0; at < code.size(); ++at) {
    const uint8_t opcode = code[at];
    size_t length = 0;
    int32_t displacement = 0;
    if ((opcode == 0xE8 || opcode == 0xE9) && at + 5 <= code.size()) {
      length = 5;
      displacement = static_cast<int32_t>(read32(at + 1));
    } else if (opcode == 0x0F && at + 6 <= code.size() && (code[at + 1] & 0xF0) == 0x80) {
      length = 6;
      displacement = static_cast<int32_t>(read32(at + 2));
    } else if ((opcode == 0xEB || (opcode & 0xF0) == 0x70 || (opcode >= 0xE0 && opcode <= 0xE3)) && at + 2 <= code.size()) {
      length = 2;
      displacement = static_cast<int8_t>(code[at + 1]);
    } else {
      continue;
    }
    const uint32_t source = first_rva + static_cast<uint32_t>(at);
    const uint32_t target = source + static_cast<uint32_t>(length) + static_cast<uint32_t>(displacement);
    for (const SiteInfo& info : SITES) {
      if (target <= info.rva || target >= info.rva + SizeOf(info)) continue;
      if (std::ranges::find(IGNORED_SOURCES, source) != IGNORED_SOURCES.end()) continue;
      ++count;
      if (hits != nullptr) hits->push_back(source);
    }
  }
  return count;
}

// No entry of the list starts with an E9 that is not one of our own sites' (that tool's hook runs on a stack and in
// registers that workers would share).
inline bool EntriesFree() {
  for (const uint32_t rva : HOOKED_ENTRIES) {
    if (base[rva] != 0xE9) continue;
    bool ours = false;
    for (size_t index = 0; index < SITE_COUNT; ++index) {
      ours |= (sites[index].at == base + rva && (written >> index & 1) != 0);
    }
    if (!ours) return false;
  }
  return true;
}

}  // namespace internal

// Once: each site found by its bytes (and each call's target by its), no branch of the game into a site, no entry
// hooked, the thunks built in a page, each site's bytes made (nothing written to the game). One log line. The sites are
// all found or none.
inline bool Prepare(HMODULE module) {
  using namespace internal;
  prepared = true;
  refused = false;
  found = 0;
  base = reinterpret_cast<const uint8_t*>(module);
  const auto sections = tw::engine_tweaks::ModuleSections(module);
  const auto read32 = [](const uint8_t* at) {
    uint32_t value;
    std::memcpy(&value, at, sizeof(value));
    return value;
  };
  const auto holds = [&](const uint8_t* at, std::string_view pattern) {
    const size_t size = (pattern.size() + 1) / 3;
    for (const std::string_view memory : sections.code) {
      const auto* begin = reinterpret_cast<const uint8_t*>(memory.data());
      if (at >= begin && size <= memory.size() && at <= begin + memory.size() - size) {
        return tw::engine_tweaks::FindPattern({std::string_view(reinterpret_cast<const char*>(at), size)}, pattern) == at;
      }
    }
    return false;
  };
  // The target of the call whose opcode byte is at `instruction`.
  const auto target = [&](const uint8_t* instruction) { return instruction + 5 + static_cast<int32_t>(read32(instruction + 1)); };
  const auto callee_of = [&](size_t index) {
    const SiteInfo& info = SITES[index];
    return (info.kind == Kind::WALK_END ? target(base + info.rva + 4) : target(base + info.rva));
  };

  std::string refused_names;
  const auto refuse = [&](std::string_view name) { refused_names += std::format("{}{}", refused_names.empty() ? "" : ", ", name); };
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    const SiteInfo& info = SITES[index];
    bool good = holds(base + info.rva, info.pattern);
    if (good && info.callee_rva != 0) {
      good = (callee_of(index) == base + info.callee_rva) && (info.callee_head.empty() || holds(base + info.callee_rva, info.callee_head));
    }
    if (!good) refuse(info.name);
  }
  if (!refused_names.empty() || !EntriesFree()) {
    renodx::utils::log::w("[Total War] Parallel Effect Update: left alone (not the game's code as expected: another mod or build)",
                          (refused_names.empty() ? ": an entry hooked by another tool" : ": "), refused_names);
    return false;
  }
  // No branch of the game into the middle of a site (the game's own code around each one; the 2 KB either side).
  size_t branches = 0;
  for (const SiteInfo& info : SITES) {
    for (const std::string_view memory : sections.code) {
      const auto* begin = reinterpret_cast<const uint8_t*>(memory.data());
      const uint8_t* at = base + info.rva;
      if (at < begin || at >= begin + memory.size()) continue;
      const uint8_t* from = std::max(begin, at - 0x800);
      const uint8_t* to = std::min(begin + memory.size(), at + 0x800);
      branches += BranchesIntoSites({from, to}, static_cast<uint32_t>(from - base), nullptr);
    }
  }
  if (branches != 0) {
    renodx::utils::log::w("[Total War] Parallel Effect Update: left alone (the game jumps into the middle of a site)");
    return false;
  }

  uint8_t* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, CODE_SIZE + 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (memory == nullptr) {
    renodx::utils::log::w("[Total War] Parallel Effect Update: left alone (the thunks' page could not be allocated)");
    return false;
  }
  Data* const words = new (memory + CODE_SIZE) Data;
  words->handler.store(Address(reinterpret_cast<const void*>(&PassThrough)));
  Assembler assembler(std::span<uint8_t>(memory, CODE_SIZE));
  std::array<const uint8_t*, SITE_COUNT> built = {};
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    const SiteInfo& info = SITES[index];
    const uint8_t* const at = base + info.rva;
    built[index] = EmitThunk(&assembler, index, words, at, (info.callee_rva != 0 ? callee_of(index) : nullptr), at + SizeOf(info));
  }
  DWORD protection;
  if (!assembler.Fits() || VirtualProtect(memory, CODE_SIZE, PAGE_EXECUTE_READ, &protection) == FALSE) {
    VirtualFree(memory, 0, MEM_RELEASE);
    renodx::utils::log::w("[Total War] Parallel Effect Update: left alone (the thunks' page could not be made)");
    return false;
  }
  FlushInstructionCache(GetCurrentProcess(), memory, CODE_SIZE);
  for (size_t index = 0; index < SITE_COUNT; ++index) {
    const SiteInfo& info = SITES[index];
    PatchedSite& site = sites[index];
    site = {.at = const_cast<uint8_t*>(base) + info.rva, .size = static_cast<uint8_t>(SizeOf(info))};
    std::memcpy(site.game.data(), site.at, site.size);
    site.patch.fill(0x90);
    site.patch[0] = (info.kind == Kind::WALK_CALL || info.kind == Kind::APPEND_CALL ? 0xE8 : 0xE9);
    const uint32_t displacement = Address(built[index]) - (Address(site.at) + 5);
    std::memcpy(site.patch.data() + 1, &displacement, sizeof(displacement));
  }
  thunks = built;
  page = memory;
  page_end = memory + CODE_SIZE;
  data = words;
  found = ALL_SITES;
  renodx::utils::log::i("[Total War] Parallel Effect Update: ", SITE_COUNT, " sites found");
  return true;
}

// The row's wish for the sites: all of them (found) while it is on, none otherwise. Going in with an entry hooked by
// another tool (started since Prepare) stands the feature down for the session.
inline uint32_t Wanted(bool row) {
  using namespace internal;
  if (!row || refused) return 0;
  if ((found & ~written) != 0 && !EntriesFree()) {
    found &= written;
    refused = true;
    renodx::utils::log::w("[Total War] Parallel Effect Update: left alone (the game's functions are hooked by another tool)");
    return 0;
  }
  return found;
}

// The scheduler's handler (effect_update.hpp); PassThrough until one is set.
inline void SetHandler(Handler handler) {
  if (internal::data != nullptr) internal::data->handler.store(internal::Address(reinterpret_cast<const void*>(handler)));
}

// Before the sites come out: no thread enters a thunk's slow path from now on. Outside any round (the present on the owner
// thread): the scheduler has closed its round and cleared these before it returns from the walk.
inline void Disarm() {
  if (internal::data == nullptr) return;
  internal::data->walk_armed.store(0);
  internal::data->walk_active.store(0);
}

#endif  // !defined(_WIN64)

}  // namespace twattila::effect_update
