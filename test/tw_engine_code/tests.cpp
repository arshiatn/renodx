// Faster Game CPU Code (src/games/twattila/engine_code.hpp) against the game's own code. empire.retail.dll is read
// from disk and mapped twice (relocated, never loaded as a DLL: no DllMain, no imports), copy A as the game ships it,
// copy B patched by engine_code (Prepare and Update, as the addon does it). Then each routine runs in both with the
// same inputs and every register, the stack and the x87 state are compared:
// 1. Each pattern exactly once in A; B differs from A in the three 5-byte sites only; back to A's bytes after off.
// 2. float to half (+0x169a220): all 2^32 floats (random other registers), eax ecx edx ebx esi edi ebp esp equal; then
//    10 M random floats at 16 addresses (the pointer's high half shows in eax for the smallest values).
// 3. The soldier draw's truncation (+0x17f6515, a block inside a function: entered with a fake frame, its end jumps back
//    to the harness): all 2^32 floats with x87 control word 0x27F and random frames; 2^24 with other rounding and
//    precision modes; every denormal with DAZ / FTZ on: the 256-byte frame, eax, esp, the control word and x87 TOP
//    equal (A's x87 sticky flags and C1 differ by design: counted, not compared).
// 4. fmod (+0x1a05cc0, x87 arguments): 20 M pairs of floats (the game's fld dword arguments: every class), 20 M pairs of
//    80-bit values (64-bit mantissas, exponents up to +-16382 apart, all four signs), the game's typical (x, 1) and
//    (x, n) pairs; other control words, a user _matherr and the CRT's flag on: the 80-bit result, the whole status word,
//    the control word, eax ecx edx ebx esi edi ebp esp equal. Arguments whose CRT path raises a math error (infinite
//    x, zero y, NaN) aren't run: the unstarted CRT's errno would fault; the stub hands every non-normal argument to the
//    same CRT code, shown for zero x and infinite y.
// 5. Toggling on and off 200 times while 4 threads run B's float to half: their results stay A's, no fault.
// 6. Timing, A against B: float to half on sprite-like rows, the truncation, fmod(x, 1); the ready check (0 and 16
//    children) also against the stub as it ended before (pop ecx) and with rol / ror, all four called directly (no
//    jump through the site).
// 7. The truncation block as the Attila CPU Performance Fix leaves it: left alone, the other two still found; the log
//    names the fix in Attila (CONTROL: one byte more changed names an unknown mod), an unknown mod in the others.
// 8. The render lists' push (+0x176ced0, Faster Render Lists): 1 M random pushes (thread, vector sizes, the "has items"
//    byte, the list's other bytes, the item, every register): the registers, esp and every byte of the list, its
//    vectors and their items equal; on and off 200 times while 4 threads push to one list: every item in its place,
//    every return the game's; timing with 4 threads pushing to 16 lists (the byte cleared each round, as the merge does).
// 9. The ready check (+0x18635a0, Faster Game CPU Code; thiscall, no arguments): every child count 0 to 64 x every
//    position of the first child not ready (or none) x 7 values of [this + 4] x the last child with and without a
//    control block x the count's 3 bits above the 29 that x 8 keeps; every [this + 4] with 0 and 1 child; 2 M random
//    vectors (up to 4096 children; uses from 1 to 0xFFFFFFFF, the carry edges among them; objects no code reads at random
//    addresses; `this` at a low address (a page below 16 MB, top byte 0) or at the top of the address space (a page at 0x7Fxxxxxx: top
//    byte 0x7F; both taken before main): eax ecx edx ebx esi edi ebp, the arithmetic flags, esp, the 64 bytes of the call's stack, every count
//    and [this] equal, the game's results as derived from its bytes (engine_code.hpp), no dispose or destroy run;
//    CONTROLs: a child with uses 0 (the game disposes it) must differ; the tail with `or cl, al` for `mov cl, al` (merges
//    al into this's top byte, not a replace) must agree for a low `this` and differ for a high one; on and off 200 times while 4 threads call both
//    copies on vectors sharing control blocks: every al the game's, every count back; timing, 1 and 4 threads.
// 10. Faster Job Wake-Ups (the job system's WakeAll, Sleep and the worker destructor's call of Stop, as the add-on's third row;
//    the game's copy against ours on the game's own functions, the semaphores, workers and destructor included): one thread,
//    no sleeper: every worker's state, wake counter, semaphore count and event equal after WakeAll twice, Sleep and the
//    destructor; ebx esi edi ebp esp kept with random registers; a soak with 24 workers and 8 submitters (every item consumed
//    once, every WakeAll's postcondition: every worker looked at the queue after it), the same with the row switched on and
//    off 200 times; 600 shutdown cycles (a delegate in its loop while the workers are destroyed: a use after free faults);
//    33 workers (the game's loop); CONTROL: the request never recorded (must fail); the render thread's time in WakeAll.
// 11. The sprite soldiers' row builder (Faster Game CPU Code; the 20 conversions of +0x1907511 .. +0x19077e2 done four at a
//    time): the region entered with a fake frame in the game's copy and in ours, every register, esp, xmm0, the frame from F up,
//    the row and the objects equal: 2 M random calls (the vector path taken exactly when all 20 floats are zeros, tiny values or
//    normals), every offset of the row's and the objects' pointers around each overlap check's edge, all 2^32 bit patterns in
//    all 18 float lanes at once, 200 switches while 4 threads run it, timing; then half_rows_adversarial.inc (quick length; `adv
//    full` for the long runs): the MXCSR / x87 state and the flags, faults at page edges, the dead stack, one lane at a time on
//    the vector path with the others random, pointer soup, a refused PausedWrite, the whole row builder (row_builder_call.inc).
// 13. Another mod's bytes in a site: left alone, and the sites that depend on it with it; the particle writer's reloads also without
//    SSE4.1 (not looked for; the three prefetch sites still found) and with a changed byte in the function's tail (both left alone, named); a changed
//    byte in the child sum, the particle loop or the emitter function (that site left alone, named once by its row, a stood-down site not named again).
// 14. The particle writer's two float4 reloads (+0x18960d0, Thrones +0x190b430; Faster Game CPU Code; particle_reload.inc): the region
//    alone with random 16 stack bytes of every float class per lane (NaN payloads, subnormals, -0, infinities), stored as the game
//    stores them: every register, xmm0-7, the flags, esp and the frame equal, CONTROLs with one lane read from the wrong address; the
//    whole function on a fake object graph, both sides of the je at +0x18961a1 and of the comiss at +0x189660c (registers, x87, MXCSR,
//    xmm, flags, esp, the frame, the six streams and the objects equal, with each site alone and both, CONTROLs per stub and lane);
//    200 switches while 4 threads run it; a parked thread at each byte of each site; timing (also in 6: the reload after a store
//    backlog, game / ours; the multiply on subnormals). Rome II has neither site.
// 12. The effect tree's child sum (+0x1890b90; Faster Effect Prefetch): the whole function on random trees with getters that
//    change the size mid-loop or move the data pointer, the loop's bottom alone at every place of esi around the vector (a
//    no-access page follows the data: no load past it; eax shows the far child loaded, a copy without the far block the near
//    one), CONTROLs (the near check with the far load, the near load one slot too far, no jc, no jb, no ja), cold-children
//    timing for K = 2 to 16, the two-prefetch forms and lists of 2 to 128, 200 switches under 4 threads.
// 15. The particle update loop's prefetch (+0x1893820 .. +0x1893888, Thrones +0x1908b80; Faster Effect Prefetch; particle_prefetch.inc): the loop's bottom
//    alone (every ebx around the size 0 to 48, the edges of the address space, an array ending at a no-access page: no load past the vector), the whole loop
//    in both copies of the DLL on random emitters (a fake update that counts and hashes its calls and may shrink the vector, swap the array, grow it; garbage
//    in the slots the loop never reads): every register but esi, the flags, esp, xmm0-7, all memory and the call order equal; CONTROLs (a stub that writes
//    eax, ends with the wrong compare, stores); 200 switches under 4 threads, a parked thread at each byte of the site; timing for K = 1 to 4. Rome II has none.
// 16. The emitter update loop's prefetch (the effect update +0x1893330, loop +0x18933c0, site +0x18933ce, Thrones +0x1908690; Faster Emitter Prefetch;
//    emitter_prefetch.inc): the loop's bottom alone (every esi around the count 0 to 48, the edges of the address space, null slots, the vector's size
//    below, at or above the loop's count, an array ending at a no-access page: no load past the vector; ecx and edx as the stub's paths leave them), the
//    whole loop in both copies of the DLL on random vectors (null slots, definition pointers that are garbage, a fake update that counts and hashes its
//    calls, overwrites ebx edi ebp and may swap the array, grow the vector or change only its size, or shrink the vector over two slots of no-access
//    pointers that the next call puts back; garbage in the slots the loop never reads): every register but ecx and edx, the flags, esp, xmm0-7, all memory
//    and the call order equal; CONTROLs (a stub that writes eax, ends with the wrong compare, stores, has no null test, the wrong live-size bound, no
//    live-size bound at all, reads ebx for the container); 200 switches under 4 threads, a parked thread at each byte of the site; timing against the
//    stub that takes one slot and loads its definition at once. Rome II has none.
// 17. More float4 reloads (Faster Game CPU Code; more_reloads.inc): the soldier draw's three matrix row reloads (+0x17f6937 / +0x17f6956 /
//    +0x17f6963, Thrones +0x186ae7c .., Rome II +0x14b47d2 ..): each alone with random lanes of every float class and any MXCSR (registers,
//    xmm0-7, flags, MXCSR, esp, the frame equal; CONTROLs a lane 4 bytes off); both ways of the draw into them run in both copies (the
//    game's code from each way's last branch to the jump after the third reload) with the three sites and each alone, CONTROLs per stub and
//    lane; 200 switches under 4 threads; a parked thread at each byte of each site; timing. In 7 the x87 fix's block leaves them found; in
//    13 a changed site, a changed pattern byte, no SSE4.1. The matrix product's three row reloads (+0x168d7f0 / +0x168d803 / +0x168d812,
//    Thrones +0x16faf10 .., Rome II +0x135b7b0 ..) the same way, the whole function on random matrices (done already, none, up to 24) instead
//    of the ways: registers, x87 / MXCSR / xmm, flags, esp, the stack, the matrices and the objects equal.
// 18. The particle writer's table entry prefetch (+0x18960f5, Thrones +0x190b455; Faster Particle Prefetch; entry_prefetch.inc): the whole function on the part 14
//    fake graph with the site alone and with the reload sites (every register, x87 / MXCSR / xmm, the flags, esp, the frame, the streams and the objects equal, both
//    sides of the je), entries in odd places (null, across a no-access page's edge, the top of the address space, random ones: both copies fault in the same place
//    with the same memory or neither does), CONTROLs (no test, a write to ecx, no store, a load for the prefetch, the jump past the je), 200 switches under 4
//    threads, a parked thread at each byte of the site, the three micro-benchmarks (the entry hot / cold, the multiply's stale operand normal / subnormal, the
//    definition chain hot / cold / chain-prefetched). Rome II has none.
// 19. The render lists' item push loops (+0x17b8490 / +0x17b7cb0, Thrones +0x182c220 / +0x182ba40; Faster Game CPU Code; item_push.inc): the guard against an exact model
//    of the locations a run's stores may reach (millions of tuples, a mutant per clause must be accepted wrongly), the whole loops in both copies of the DLL (the
//    allocator, the free and the per-item call faked and logged) on random lists, vectors and registers (starts through the index's wrap, ends at or below the start,
//    capacities 0 to 0x100000 and more, growth in the middle, per-item calls that stop, the block and the item array ending at a no-access page, and layouts in which the
//    block, the object and the list overlap each other or the frame): every register, the flags, esp, xmm, the frame above esp, every byte and the fakes' log equal or
//    both fault with the same memory; CONTROLs (a mutant of each stub instruction that matters); 200 switches under 4 threads, a parked thread at each byte of each
//    site; timing for 1 to 4,096 items. Rome II has neither loop.
// 20. The pending list sweep's chain prefetch (+0x18932b1 in +0x1893290, Thrones +0x1908611 in +0x19085f0; Faster Sweep Prefetch; sweep_prefetch.inc): whole sweeps
//    in both copies of the DLL on random vectors of list objects with children, the game's own ready check and release code running (null list objects, lists
//    without children, garbage children arrays, ready ones erased, the vector and the lists at the edge of a page between no-access pages), every register but eax /
//    ecx / edx, the flags, esp, xmm, the frame, every byte and the count of the releases' calls equal or both fault with the same memory; CONTROLs (a mutant of each
//    check that matters); 200 switches under 4 threads, a parked thread at each byte of the site; timing for 4,000 cold lists of 1 to 4 and 5 to 8 children. Rome II has none.
// 22. Model Detail Distance (the game's LOD pick, +0x183d570, Rome II +0x14f0710, Thrones +0x18b2060; the slider's row, engine_code.hpp 18.; lod_pick.inc): the site's first three
//    instructions run by the stub, which multiplies the distance squared on the stack by the reciprocal of the slider squared as the old Detours hook did: the callers' shape
//    pinned, the stub alone and the whole function against the game's with the distance multiplied (every register, the flags, x87 / MXCSR / xmm, esp, the stack) at 14 sliders
//    and any MXCSR, the slider moved with 4 threads calling, a parked thread at each byte, the status words, another mod's bytes, the pattern not found, CONTROLs (mutants of the stub
//    and a restore left out) that must be seen, timing; `tests.exe <dll> quick lod` runs it alone.
// 25. The Parallel Effect Update's 27 sites (src/games/twattila/effect_update_sites.hpp; engine_code.hpp PausedWrite's third table; effect_sites.inc; Attila only): the table and
//    the whole code section scanned for a branch into a site, the sites in and out (whole or not at all, another mod's bytes), every thunk as built (the original path equal to the
//    game's own instructions, the slow path's Frame, the state kept through a handler that clobbers xmm / x87 / MXCSR, the answers, the E8 sites' callee), the fast path's one word,
//    the slow path's stack, the real sites with a handler, a parked thread at each byte, the count, 200 switches under 4 threads, CONTROLs (a torn low / high pair, no fxsave);
//    `tests.exe <dll> quick effect` runs it alone.
// 26. The Parallel Effect Update's scheduler (src/games/twattila/effect_update.hpp; effect_update.inc; Attila only): rounds on the real worker pool over a fake game (effects, a node
//    table, instances whose update is a script of order-point operations, a sweep, forwarded calls into stubs on a fake image) against the game's loop: equal logs, shared words,
//    vectors and table, no two direct units at once; each script, children and new types (late units and segments), tiny effects with children into the next (no deadlock),
//    an allocation failure at every block, the serial reasons, the census, the watched words, the stack, CONTROLs (no order points, no enumeration wait, late child off,
//    forwarding off, sweeps on workers, no epoch); `tests.exe <dll> quick effect_update` runs it alone.
// 29. Parallel Instance Fill's hook-up (src/games/twattila/instance_fill.hpp; engine_code.hpp PausedWrite's fourth table; instance_fill.inc; Attila only): the 4 call sites as Prepare made them, the row on and off,
//    another mod's bytes going in (the group stands down, found 0) and over our call going out (that site stays, the rest out, the module pinned), a page that cannot be made writable
//    (the control: going in none is written, going out only that site stays; the protection as it was), a parked thread at each byte of each site, the dispatcher's count above 0 and stuck at
//    Uninstall, both tables of one window, Uninstall; `tests.exe <dll> quick fill` runs it alone.
// 30. Exit Job Drain (src/games/twattila/engine_code.hpp 21.; exit_drain.inc; Attila only): the site (+0xC9AC1) and its signature in the game's code, the E8 callers of the app's delete, the four
//    cases that leave it alone (signature, app delete, no accessor, pool pointer outside the image), in and out, the drain on a fake job pool (workers running or woken are waited for, the
//    cap of 1000 ms, no pool, a worker of another pool, the drain once only, the game's own delete after); `tests.exe <dll> quick exit` runs it alone.
// Thrones of Britannia's and Rome II's DLLs the same way (engine_code's BUILDS, each picked by its truncation's pattern):
// their RVAs, their CRT's state, their truncation's stack slots; Rome II has no ready check (its pattern found 0 times).
// tests.exe [empire.retail.dll] [quick]  (quick: 2^28 instead of 2^32 in 2 and 3)
#include <windows.h>
#include <emmintrin.h>
#include <xmmintrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "src/games/twattila/engine_code.hpp"
#include "src/games/twattila/effect_update.hpp"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Engine code test";

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
  if (log_lines.size() <= 4) std::printf("  log: %s\n", message);
}

#if defined(_WIN64)

int main() {
  std::printf("SKIP: Attila is 32-bit; engine_code is x86 only (its x64 Update and Uninstall do nothing).\n");
  twattila::engine_code::Update(true);
  twattila::engine_code::Uninstall();
  return 0;
}

#else

namespace {

constexpr uint32_t PREFERRED_BASE = 0x10000000;

// A committed page at the first 64 KB base from `first` towards `last` (excluded) where one can be had; null if none.
uint8_t* FirstFreePage(uintptr_t first, uintptr_t last) {
  const uintptr_t step = (first < last ? 0x10000u : 0u - 0x10000u);
  for (uintptr_t where = first; where != last; where += step) {
    if (void* page = VirtualAlloc(reinterpret_cast<void*>(where), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); page != nullptr) {
      return static_cast<uint8_t*>(page);
    }
  }
  return nullptr;
}

// Part 9's two `this` pages, taken before main runs at fixed places. The top byte 0: below 16 MB, the first free base from
// 0x10000 up (by part 9 the blocks VirtualAlloc placed bottom-up, the earlier parts' kept reservations, grown heap segments and
// the DLL's two copies, can hold every base there). The top byte 0x7F: the first free base from 0x7FFE0000 down (the process
// isn't large-address aware; MEM_TOP_DOWN alone gave a page below 0x7F000000 in 2 of 30 launches).
uint8_t* const low_page = FirstFreePage(0x10000, 0x1000000);
uint8_t* const high_page = FirstFreePage(0x7FFE0000, 0x7EFF0000);

// A game's DLL: where the routines are, the CRT's state its start-up would have set (the test never runs it: the encoded
// user _matherr, nullptr being the cookie itself as its decode rotates the xor; the flag its finish tests), and the
// truncation's float and integer above the block's esp at entry.
struct TestBuild {
  const char* game;
  const twattila::engine_code::internal::Build* code;
  uint32_t half_rva;
  uint32_t truncation_block_rva;
  uint32_t truncation_resume_rva;
  uint32_t fmod_rva;
  uint32_t cookie_rva;
  uint32_t matherr_rva;
  uint32_t crt_flag_rva;
  uint32_t float_slot;
  uint32_t integer_slot;
  uint32_t list_push_function_rva;  // 0: none in this build
  uint32_t ready_check_rva;         // 0: none in this build
  std::array<uint32_t, 2> soldier_ways;  // the soldier draw's two ways into its matrix row reloads: the first instruction after each one's last branch
};
constexpr TestBuild ATTILA = {"Attila", &twattila::engine_code::internal::ATTILA_BUILD, 0x169a220, 0x17f6515, 0x17f6539, 0x1a05cc0,
                              0x1dae4f4, 0x2a8b504, 0x2a8b4cc, 0x1c, 0x28, 0x176ced0, 0x18635a0,
                              {0x17f65b8, 0x17f697d}};
constexpr TestBuild THRONES = {"Thrones of Britannia", &twattila::engine_code::internal::THRONES_BUILD, 0x1707eb0, 0x186aa48, 0x186aa6c,
                               0x1b0a030, 0x1eec634, 0x25fde74, 0x25fde3c, 0x20, 0x18, 0x17defc0, 0x18d8400,
                               {0x186aafd, 0x186aec2}};
constexpr TestBuild ROME2 = {"Rome II", &twattila::engine_code::internal::ROME2_BUILD, 0x1368030, 0x14b434a, 0x14b436e, 0x169ca30,
                             0x19ebd74, 0x21263cc, 0x2126394, 0x1c, 0x20, 0, 0, {0x14b4417, 0x14b4806}};

uint32_t Read32(const uint8_t* at) {
  uint32_t value;
  std::memcpy(&value, at, sizeof(value));
  return value;
}

void Write32(uint8_t* at, uint32_t value) { std::memcpy(at, &value, sizeof(value)); }

unsigned long long Read64(const uint8_t* at) {
  unsigned long long value;
  std::memcpy(&value, at, sizeof(value));
  return value;
}

// The DLL's sections at a new address, relocated. Executable and writable (a test).
uint8_t* Map(const std::vector<uint8_t>& file, uint32_t* image_size) {
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

size_t Matches(const uint8_t* text, size_t size, std::string_view pattern) {
  size_t count = 0;
  std::string_view rest(reinterpret_cast<const char*>(text), size);
  while (uint8_t* match = tw::engine_tweaks::FindPattern({rest}, pattern)) {
    ++count;
    const size_t skip = static_cast<size_t>(match - reinterpret_cast<const uint8_t*>(rest.data())) + 1;
    rest.remove_prefix(skip);
  }
  return count;
}

struct Random {
  uint64_t state;
  uint32_t Next() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return static_cast<uint32_t>(state >> 16);
  }
  // A random word that is no address of this process (2 GB and up; the exe is not large-address-aware): the random words of a world that a wrong stub (a CONTROL) or a
  // layout can read as a pointer are these, so that such a read always raises an access violation, never a read of some other thread's stack guard page (a random
  // user address can be one: STATUS_GUARD_PAGE_VIOLATION is not caught by the runs' filters and the first touch would clear the other thread's guard).
  uint32_t NextUnmapped() { return 0x80000000u | Next(); }
};

// A thread that sleeps until stopped, for the parts that park it at chosen bytes (SuspendThread, SetThreadContext). The constructor returns once the thread runs its loop and has had time to
// leave its start: frozen inside the start it can hold the heap's or the loader's lock, and the next allocation of the thread that froze it would wait for it for good. Stop (or the
// destructor, on every way out of a part, a failing CHECK included) joins it.
struct ParkedThread {
  std::atomic<bool> stop = false;
  std::atomic<bool> running = false;
  std::thread thread;
  ParkedThread()
      : thread([this] {
          running = true;
          while (!stop.load()) Sleep(1);
        }) {
    while (!running.load()) Sleep(1);
    Sleep(20);
  }
  ParkedThread(const ParkedThread&) = delete;
  ParkedThread& operator=(const ParkedThread&) = delete;
  ~ParkedThread() { Stop(); }
  HANDLE Open() { return OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, GetThreadId(thread.native_handle())); }
  void Stop() {
    stop = true;
    if (thread.joinable()) thread.join();
  }
};

// A call with every register set: in (eax ecx edx ebx esi edi ebp), out after, esp as the callee left it (cdecl: at the
// argument; a callee popping it, thiscall or stdcall: past it).
struct RegisterCall {
  uint32_t in[7];       // 0
  uint32_t function;    // 28
  uint32_t argument;    // 32
  uint32_t out[7];      // 36
  uint32_t esp_before;  // 64: the argument's slot
  uint32_t esp_after;   // 68
};

__declspec(naked) void __cdecl CallWithRegisters(RegisterCall*) {
  __asm {
    pushad
    mov ebp, [esp + 36]
    push ebp
    push ebp
    push dword ptr [ebp + 32]
    call set_up
    push eax
    mov eax, [esp + 8]
    mov [eax + 40], ecx
    mov [eax + 44], edx
    mov [eax + 48], ebx
    mov [eax + 52], esi
    mov [eax + 56], edi
    mov [eax + 60], ebp
    lea ecx, [esp + 4]
    mov [eax + 68], ecx
    pop ecx
    mov [eax + 36], ecx
    mov esp, [eax + 64]
    add esp, 12
    popad
    ret
  set_up:
    lea eax, [esp + 4]
    mov [ebp + 64], eax
    push dword ptr [ebp + 28]
    mov eax, [ebp + 0]
    mov ecx, [ebp + 4]
    mov edx, [ebp + 8]
    mov ebx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]
    mov ebp, [ebp + 24]
    ret
  }
}

// _CIfmod: x (80-bit) then y on the x87 stack (st1 x, st0 y), the result in st0; every register set as above.
struct FmodCall {
  uint32_t in[7];          // 0
  uint32_t function;       // 28
  uint16_t control;        // 32
  uint16_t unused_0;       // 34
  uint8_t x[10];           // 36
  uint8_t unused_1[2];     // 46
  uint8_t y[10];           // 48
  uint8_t unused_2[2];     // 58
  uint32_t out[7];         // 60
  uint8_t result[10];      // 88
  uint16_t status;         // 98: after the call, before the result is popped
  uint16_t control_after;  // 100
  uint16_t empty_after;    // 102: the status word after the result is popped (TOP back)
  uint16_t saved_control;  // 104
  uint16_t unused_3;       // 106
  uint32_t esp_before;     // 108
  uint32_t esp_after;      // 112
};

__declspec(naked) void __cdecl CallFmod(FmodCall*) {
  __asm {
    pushad
    mov ebp, [esp + 36]
    fnstcw word ptr [ebp + 104]
    fninit
    fldcw word ptr [ebp + 32]
    fld tbyte ptr [ebp + 36]
    fld tbyte ptr [ebp + 48]
    push ebp
    call set_up
    push eax
    mov eax, [esp + 4]
    fnstsw word ptr [eax + 98]
    fnstcw word ptr [eax + 100]
    fstp tbyte ptr [eax + 88]
    fnstsw word ptr [eax + 102]
    mov [eax + 64], ecx
    mov [eax + 68], edx
    mov [eax + 72], ebx
    mov [eax + 76], esi
    mov [eax + 80], edi
    mov [eax + 84], ebp
    lea ecx, [esp + 4]
    mov [eax + 112], ecx
    pop ecx
    mov [eax + 60], ecx
    fninit
    fldcw word ptr [eax + 104]
    add esp, 4
    popad
    ret
  set_up:
    lea eax, [esp + 4]
    mov [ebp + 108], eax
    push dword ptr [ebp + 28]
    mov eax, [ebp + 0]
    mov ecx, [ebp + 4]
    mov edx, [ebp + 8]
    mov ebx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]
    mov ebp, [ebp + 24]
    ret
  }
}

// The truncation block, entered with esp at frame + 128 (the soldier draw's esp there) and eax set; its end
// (+0x17f6539) jumps to BlockReturn (the test's jump), which finds the call by esp.
struct BlockCall {
  uint32_t eax_in;         // 0
  uint32_t block;          // 4
  uint16_t control;        // 8
  uint16_t saved_control;  // 10
  uint32_t saved_esp;      // 12
  uint32_t eax_out;        // 16
  uint16_t status;         // 20
  uint16_t control_after;  // 22
  uint32_t esp_after;      // 24
  uint8_t unused[36];      // 28
  uint8_t frame[256];      // 64: esp at frame + 128 = the call + 192
};
constexpr uint32_t BLOCK_FRAME_ESP = 192;

__declspec(naked) void __cdecl EnterBlock(BlockCall*) {
  __asm {
    pushad
    mov ebp, [esp + 36]
    fnstcw word ptr [ebp + 10]
    fninit
    fldcw word ptr [ebp + 8]
    mov [ebp + 12], esp
    lea esp, [ebp + 192]
    mov eax, [ebp + 0]
    jmp dword ptr [ebp + 4]
  }
}

__declspec(naked) void BlockReturn() {
  __asm {
    mov [esp - 176], eax
    mov eax, esp
    sub eax, 192
    mov [eax + 24], esp
    fnstsw word ptr [eax + 20]
    fnstcw word ptr [eax + 22]
    fninit
    fldcw word ptr [eax + 10]
    mov esp, [eax + 12]
    popad
    ret
  }
}

void JumpTo(uint8_t* at, const void* to) {
  at[0] = 0xE9;
  Write32(at + 1, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(to) - reinterpret_cast<uintptr_t>(at + 5)));
  FlushInstructionCache(GetCurrentProcess(), at, 5);
}

// The code sections of the mapped DLL at `image` made `protection` (the harness maps its copies read-write-execute). The CONTROLs that run a deliberately wrong stub
// on random worlds can store through a garbage pointer (a stack slot never written, a register not set), and a store that lands in a copy's code stays there
// and breaks the byte-for-byte comparison at the end of the run (which copy's code a garbage pointer reaches depends on where the OS maps the copies); read-only,
// such a store faults in the copy that makes it, like any other wild store the CONTROL's world has. False if a section could not be protected (the caller CHECKs).
bool ProtectCode(uint8_t* image, DWORD protection) {
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
  const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
  bool all = true;
  for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
    DWORD previous;
    if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) all &= VirtualProtect(image + section->VirtualAddress, section->Misc.VirtualSize, protection, &previous) != FALSE;
  }
  return all;
}

// Runs `body(first, last, worker)` over [0, count) on every core; returns the sum of what each returned.
template <typename Body>
uint64_t Parallel(uint64_t count, Body body) {
  const unsigned workers = std::max(1u, std::thread::hardware_concurrency());
  std::vector<std::thread> threads;
  std::vector<uint64_t> results(workers, 0);
  for (unsigned worker = 0; worker < workers; ++worker) {
    threads.emplace_back([&, worker] {
      const uint64_t first = count * worker / workers;
      const uint64_t last = count * (worker + 1) / workers;
      results[worker] = body(first, last, worker);
    });
  }
  for (auto& thread : threads) thread.join();
  uint64_t total = 0;
  for (const uint64_t result : results) total += result;
  return total;
}

std::mutex print_mutex;
std::atomic<int> printed = 0;
template <typename... Args>
void Report(const char* format, Args... args) {
  if (printed.fetch_add(1) >= 12) return;
  const std::unique_lock lock(print_mutex);
  std::printf(format, args...);
}

// The render list's thread index provider: its vtable's +4 (a thiscall without arguments) returns the calling thread's.
thread_local uint32_t push_thread = 0;
uint32_t __fastcall ThreadIndexOf(void*, void*) { return push_thread; }

// One render list as the game lays it out: the provider at +0, the threads' vectors {capacity, size, data} at +0x18, the
// "has items" byte at +0x1c; 8 threads, room for 64 items each.
struct PushWorld {
  std::array<uint32_t, 2> vtable;
  uint32_t provider;
  alignas(64) std::array<uint8_t, 64> list;
  alignas(64) std::array<uint32_t, 8 * 3> headers;
  std::array<std::array<uint32_t, 64>, 8> data;
};

uint32_t Address(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

// Where site `index`'s jump (at `site`) leads: the production stub, through the probe stub in front of it in the probe build (its last jump goes on to the stub).
uint32_t SiteJumpTarget(size_t index, const uint8_t* site) {
  uint32_t target = Address(site + 5) + Read32(site + 1);
#if defined(TWATTILA_ENGINE_PROBE)
  namespace probe = twattila::engine_code::internal::probe;
  if (probe::stub_of[index] != nullptr && target == Address(probe::stub_of[index])) {
    const uint8_t* end = probe::stub_of[index] + probe::size_of[index];
    target = Address(end) + Read32(end - 4);
  }
#else
  (void)index;
#endif
  return target;
}

// The probe build's stubs push and pop up to five dwords right below esp at their site (twattila::engine_code::internal::probe::STACK_USED): dead after the jump (the game's
// code writes there next), so the tests that compare the dead stack below the site's esp `esp_at` (an offset in the same frame as `at`) skip those bytes; the production
// build compares every byte.
bool ProbeDeadStack(uint32_t at, uint32_t esp_at) {
#if defined(TWATTILA_ENGINE_PROBE)
  return at < esp_at && at + twattila::engine_code::internal::probe::STACK_USED >= esp_at;
#else
  (void)at;
  (void)esp_at;
  return false;
#endif
}

// The ready check (thiscall, no arguments) on a stack of its own inside the call: esp at stack + 48 at the call (the
// return address at stack + 44, the routine's pushes below it), every register and the arithmetic flags set; after it
// every register, the arithmetic flags and esp, and the stack's bytes as the routine left them. One thread at a time.
struct ReadyCall {
  uint32_t in[7];      // 0: eax ecx edx ebx esi edi ebp
  uint32_t flags_in;   // 28
  uint32_t out[7];     // 32
  uint32_t flags_out;  // 60: lahf's SF ZF AF PF CF in bits 8-15, OF in bit 0
  uint32_t saved_esp;  // 64
  uint32_t esp_after;  // 68
  uint8_t stack[64];   // 72
};
constexpr uint32_t READY_CALL_ESP = 72 + 48;
ReadyCall* ready_call = nullptr;
uint32_t ready_function = 0;
uint32_t ready_eax = 0;
uint32_t ready_flags = 0;

__declspec(naked) void __cdecl CallReady(ReadyCall*) {
  __asm {
    pushad
    mov ebp, [esp + 36]
    mov ready_call, ebp
    mov [ebp + 64], esp
    push dword ptr [ebp + 28]
    popfd
    lea esp, [ebp + 120]  // READY_CALL_ESP
    mov eax, [ebp + 0]
    mov ecx, [ebp + 4]
    mov edx, [ebp + 8]
    mov ebx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]
    mov ebp, [ebp + 24]
    call dword ptr [ready_function]
    mov ready_eax, eax
    lahf
    seto al
    movzx eax, ax
    mov ready_flags, eax
    mov eax, ready_call
    mov [eax + 36], ecx
    mov [eax + 40], edx
    mov [eax + 44], ebx
    mov [eax + 48], esi
    mov [eax + 52], edi
    mov [eax + 56], ebp
    mov [eax + 68], esp
    mov ecx, ready_eax
    mov [eax + 32], ecx
    mov ecx, ready_flags
    mov [eax + 60], ecx
    mov esp, [eax + 64]
    popad
    ret
  }
}

// A control block's vtable for the ready check's children (MSVC's _Ref_count_base: vtable, uses +4, weaks +8): dispose
// and destroy (thiscall) only counted, the game's release runs them when a count reaches 0.
std::atomic<uint32_t> disposed = 0;
void __fastcall Dispose(void*, void*) { disposed.fetch_add(1); }
void __fastcall Destroy(void*, void*) { disposed.fetch_add(0x10000); }
const std::array<uint32_t, 2> BLOCK_VTABLE = {Address(reinterpret_cast<const void*>(&Dispose)), Address(reinterpret_cast<const void*>(&Destroy))};

// The ready check stub's last 11 bytes (bswap ecx; mov cl, al; bswap ecx; lea esp, [esp + 4]; ret) after its first 83, which the
// older tails and the control below keep.
constexpr std::array<uint8_t, 11> READY_CHECK_TAIL = {0x0F, 0xC9, 0x88, 0xC1, 0x0F, 0xC9, 0x8D, 0x64, 0x24, 0x04, 0xC3};
constexpr size_t READY_CHECK_KEPT = twattila::engine_code::internal::READY_CHECK_STUB.size() - READY_CHECK_TAIL.size();

// An 80-bit value from sign, exponent (15 bits) and 64-bit mantissa.
void Extended(uint8_t* out, bool sign, uint32_t exponent, uint64_t mantissa) {
  std::memcpy(out, &mantissa, 8);
  const uint16_t top = static_cast<uint16_t>((sign ? 0x8000u : 0u) | (exponent & 0x7FFFu));
  std::memcpy(out + 8, &top, 2);
}

// A float's 80-bit value (exact).
void FromFloat(uint8_t* out, uint32_t bits) {
  float value;
  std::memcpy(&value, &bits, 4);
  __asm {
    mov eax, out
    fld dword ptr [value]
    fstp tbyte ptr [eax]
  }
}

// ---- 10. Faster Job Wake-Ups: the game's own job functions, the game's copy against the patched one ----

// The worker constructor's pattern: its call of the semaphore constructor's wrapper (thiscall, 4 arguments) is at +69.
constexpr std::string_view JOB_WORKER_CONSTRUCTOR =
    "8B 44 24 04 56 8B F1 6A 00 68 ?? ?? ?? ?? 89 46 04 8D 4E 1C 8B 44 24 14 89 46 08 8B 44 24 18 40 C7 06 ?? ?? ?? ?? 50 6A "
    "00 C7 46 0C 00 00 00 00 C7 46 10 FF FF FF FF C7 46 14 FF FF FF FF C7 46 18 00 00 00 00 E8 ?? ?? ?? ?? 8B CE C7 46 50 00 "
    "00 00 00";
constexpr size_t JOB_WORKER_CONSTRUCTOR_SEMAPHORE_CALL = 69;
constexpr uint32_t WORKER_SEMAPHORE = 0x1C;  // the semaphore inside a worker; its count is at +0x24, so the worker's +0x40

// The module's kernel32 imports (the test maps the DLL without its imports): the slots the semaphores' code calls through.
void FillKernelImports(uint8_t* image) {
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + reinterpret_cast<const IMAGE_DOS_HEADER*>(image)->e_lfanew);
  const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  for (const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(image + directory.VirtualAddress); descriptor->Name != 0; ++descriptor) {
    if (_stricmp(reinterpret_cast<const char*>(image + descriptor->Name), "kernel32.dll") != 0) continue;
    const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA32*>(image + descriptor->OriginalFirstThunk);
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(image + descriptor->FirstThunk);
    for (size_t index = 0; names[index].u1.AddressOfData != 0; ++index) {
      if ((names[index].u1.Ordinal & IMAGE_ORDINAL_FLAG32) != 0) continue;
      const auto* name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(image + names[index].u1.AddressOfData);
      slots[index].u1.Function = static_cast<DWORD>(reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleA("kernel32.dll"), name->Name)));
    }
  }
}

// One copy of the game's code with the job functions callable (thiscall as __fastcall with the second argument unused).
struct JobCopy {
  const char* name;
  uint8_t* image;
  void(__fastcall* wake_all)(void*, void*);
  void(__fastcall* sleep)(void*, void*);
  void(__fastcall* destroy)(void*, void*);  // the worker destructor: the call of Stop is the third site
  void*(__fastcall* make_semaphore)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t);
  uint8_t* pool;  // the same address in every run: engine_code's table keys the first pool it sees
  const volatile uint32_t* asleep;  // the table's mask of workers asleep in our Sleep (null: the game's copy)
};

// The pool as the game lays it out (+4 the worker count, +8 the workers' array, +0x1c an object the worker destructor
// notifies: none), the workers' pages (+4 pool, +8 index, +0xc thread handle, +0x14 -1, +0x18 state, +0x1c semaphore (0,
// count + 1), +0x50 wake counter) and the threads that run them.
uint8_t* NewWorker(const JobCopy& copy, uint32_t index, uint32_t count, HANDLE thread) {
  auto* worker = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  Write32(worker + 4, Address(copy.pool));
  Write32(worker + 8, index);
  Write32(worker + 0xC, Address(thread));
  Write32(worker + 0x14, 0xFFFFFFFF);
  copy.make_semaphore(worker + WORKER_SEMAPHORE, nullptr, 0, count + 1, 0, 0);
  Write32(copy.pool + 4, count);
  Write32(copy.pool + 8, Address(copy.pool + 0x100));
  Write32(copy.pool + 0x20, count);
  Write32(copy.pool + 0x100 + 4 * index, Address(worker));
  return worker;
}

// Decommits a worker's page (a worker used afterwards faults) and keeps its reservation for a while: each VirtualAlloc reserves
// 64 KB of the 32-bit address space, so the oldest go back (the shutdown cycles would otherwise use up 2 GB).
void DecommitWorker(uint8_t* worker) {
  static std::mutex lock;
  static std::deque<uint8_t*> kept;
  VirtualFree(worker, 0x1000, MEM_DECOMMIT);
  const std::unique_lock guard(lock);
  kept.push_back(worker);
  while (kept.size() > 1500) {
    VirtualFree(kept.front(), 0, MEM_RELEASE);
    kept.pop_front();
  }
}

struct JobRun {
  const JobCopy* copy = nullptr;
  uint32_t count = 0;
  std::vector<uint8_t*> workers;
  std::mutex queue_lock;
  std::deque<uint64_t> queue;
  uint64_t capacity = 0;
  std::unique_ptr<std::atomic<uint8_t>[]> consumed;
  std::unique_ptr<std::atomic<uint64_t>[]> looks;  // each worker's looks at the queue
  std::atomic<uint64_t> pushed = 0;
  std::atomic<uint64_t> done = 0;
  std::atomic<uint64_t> requests = 0;
  std::atomic<uint64_t> sleeper_seen = 0;  // requests that found a worker asleep in our Sleep: the head is woken, not the loop run here
  std::atomic<bool> stop_submitting = false;
  std::atomic<bool> stop_checking = false;
  struct Check {
    std::chrono::steady_clock::time_point deadline;
    std::vector<uint64_t> looks;
  };
  std::mutex check_lock;
  std::deque<Check> checks;
  std::mutex failure_lock;
  std::string failure;

  void Fail(const std::string& message) {
    const std::unique_lock lock(failure_lock);
    if (failure.empty()) failure = message;
  }
  bool Pop(uint64_t* item) {
    const std::unique_lock lock(queue_lock);
    if (queue.empty()) return false;
    *item = queue.front();
    queue.pop_front();
    return true;
  }
  void Consume(uint64_t item, Random* random) {
    if (consumed[item].fetch_add(1) != 0) Fail(std::format("item {} consumed twice", item));
    for (uint32_t spin = random->Next() % 40; spin != 0; --spin) _mm_pause();
    done.fetch_add(1);
  }
};

// A worker as the game's loop runs it: the game's Sleep, a look at the queue, back to sleep; state 3 (Stop) ends it.
struct WorkerContext {
  JobRun* run;
  uint32_t index;
};
DWORD WINAPI JobWorkerMain(void* parameter) {
  const auto* context = static_cast<WorkerContext*>(parameter);
  JobRun* run = context->run;
  uint8_t* worker = run->workers[context->index];
  Random random = {.state = 0xABCDEF ^ (context->index * 0x9E3779B97F4A7C15ull)};
  for (;;) {
    run->copy->sleep(worker, nullptr);
    run->looks[context->index].fetch_add(1);
    if (*reinterpret_cast<volatile uint32_t*>(worker + 0x18) == 3) return 0;
    uint64_t item;
    while (run->Pop(&item)) run->Consume(item, &random);
  }
}

// Submitters push batches, call WakeAll when the queue was empty (the game's rule at its 67 call sites), and (half of
// them) help; the rest leave the items to the workers.
void JobSubmitterMain(JobRun* run, uint32_t seed, bool helps) {
  Random random = {.state = 0x1234567ull * (seed + 1)};
  while (!run->stop_submitting.load()) {
    const uint32_t batch = 1 + random.Next() % 64;
    bool was_empty;
    {
      const std::unique_lock lock(run->queue_lock);
      was_empty = run->queue.empty();
      if (run->pushed.load() + batch > run->capacity) return;
      for (uint32_t index = 0; index < batch; ++index) run->queue.push_back(run->pushed.fetch_add(1));
    }
    if (was_empty) {
      if ((run->requests.fetch_add(1) & 7) == 0) {
        JobRun::Check check = {.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3), .looks = std::vector<uint64_t>(run->count)};
        for (uint32_t worker = 0; worker < run->count; ++worker) check.looks[worker] = run->looks[worker].load();
        const std::unique_lock lock(run->check_lock);
        run->checks.push_back(std::move(check));
      }
      if (run->copy->asleep != nullptr && *run->copy->asleep != 0) run->sleeper_seen.fetch_add(1);
      run->copy->wake_all(run->copy->pool, nullptr);
    }
    uint64_t item;
    if (helps) {
      while (run->Pop(&item)) run->Consume(item, &random);
    }
    // A gap, often long enough for the workers to fall asleep (their sleeps run 40 us to 1 ms in a battle).
    LARGE_INTEGER start, now, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const long long gap = frequency.QuadPart * (random.Next() % 600) / 1000000;
    do {
      _mm_pause();
      QueryPerformanceCounter(&now);
    } while (now.QuadPart - start.QuadPart < gap);
  }
}

struct SoakResult {
  uint64_t items = 0;
  uint64_t requests = 0;
  uint64_t sleeper_seen = 0;
  std::string failure;
};

// workers + submitters threads for `seconds` (`during` runs every few ms on the calling thread and returns whether it wants
// more time); then the queue must drain, every item must have been consumed once, every request's WakeAll postcondition
// must hold (every worker looked at the queue after it within 3 s; `postcondition`), and the workers are destroyed by the
// game's destructor (Stop).
SoakResult Soak(const JobCopy& copy, uint32_t workers, uint32_t submitters, double seconds, const std::function<bool()>& during,
                bool postcondition = true) {
  JobRun run;
  run.copy = &copy;
  run.count = workers;
  run.capacity = 200'000 + static_cast<uint64_t>(seconds * 3'000'000);
  run.consumed = std::make_unique<std::atomic<uint8_t>[]>(run.capacity);
  run.looks = std::make_unique<std::atomic<uint64_t>[]>(workers);
  std::vector<WorkerContext> contexts(workers);
  std::vector<HANDLE> threads(workers);
  run.workers.resize(workers);
  Write32(copy.pool + 4, workers);
  for (uint32_t index = 0; index < workers; ++index) {
    contexts[index] = {&run, index};
    threads[index] = CreateThread(nullptr, 0, &JobWorkerMain, &contexts[index], CREATE_SUSPENDED, nullptr);
    run.workers[index] = NewWorker(copy, index, workers, threads[index]);
  }
  for (const HANDLE thread : threads) ResumeThread(thread);
  std::thread checker([&] {
    while (!run.stop_checking.load()) {
      Sleep(20);
      const std::unique_lock lock(run.check_lock);
      while (!run.checks.empty() && run.checks.front().deadline < std::chrono::steady_clock::now()) {
        for (uint32_t worker = 0; worker < workers; ++worker) {
          if (postcondition && run.looks[worker].load() <= run.checks.front().looks[worker]) {
            run.Fail(std::format("worker {} never looked at the queue after a WakeAll (3 s)", worker));
          }
        }
        run.checks.pop_front();
      }
    }
  });
  std::vector<std::thread> submitting;
  for (uint32_t index = 0; index < submitters; ++index) submitting.emplace_back(JobSubmitterMain, &run, index, (index & 1) != 0);
  const auto start = std::chrono::steady_clock::now();
  while (run.failure.empty()) {
    const bool more = (during && during());
    if (!more && std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= seconds) break;
    Sleep(5);
  }
  run.stop_submitting = true;
  for (auto& thread : submitting) thread.join();
  // Nothing may be left queued: the last pushes may have been the workers' alone.
  for (int wait = 0; wait < 6000 && run.done.load() < run.pushed.load(); ++wait) Sleep(1);
  if (run.done.load() < run.pushed.load()) {
    run.Fail(std::format("lost wake-up: {} of {} items never consumed, every worker asleep", run.pushed.load() - run.done.load(), run.pushed.load()));
  }
  // Every request's check must have come due and held.
  for (int wait = 0; wait < 400 && postcondition; ++wait) {
    {
      const std::unique_lock lock(run.check_lock);
      if (run.checks.empty()) break;
    }
    Sleep(10);
  }
  run.stop_checking = true;
  checker.join();
  for (uint64_t item = 0; item < run.pushed.load() && run.failure.empty(); ++item) {
    if (run.consumed[item].load() != 1) run.Fail(std::format("item {} consumed {} times", item, run.consumed[item].load()));
  }
  for (uint32_t index = 0; index < workers; ++index) {
    // The game's destructor: Stop (state 3, the semaphore released, the thread waited for and closed), then the semaphore.
    copy.destroy(run.workers[index], nullptr);
    DecommitWorker(run.workers[index]);
  }
  return {.items = run.pushed.load(), .requests = run.requests.load(), .sleeper_seen = run.sleeper_seen.load(), .failure = run.failure};
}

// engine_code's table (null while the copy is the game's).
uint32_t JobTableField(uint32_t line) { return Read32(twattila::engine_code::internal::job_table + 64 * line); }

// A thread that runs the job copy's WakeAll again and again, an access violation (a freed worker's page) counted and ending it.
struct DelegateState {
  const JobCopy* copy = nullptr;
  std::atomic<bool> stop = false;
  std::atomic<uint32_t> faults = 0;
};

bool CallGuarded(void(__fastcall* function)(void*, void*), void* argument) {
  __try {
    function(argument, nullptr);
    return true;
  } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
    return false;
  }
}

DWORD WINAPI DelegateMain(void* parameter) {
  auto* state = static_cast<DelegateState*>(parameter);
  while (!state->stop.load()) {
    if (!CallGuarded(state->copy->wake_all, state->copy->pool)) {
      state->faults.fetch_add(1);
      return 1;
    }
  }
  return 0;
}

int RunJobTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(game + reinterpret_cast<const IMAGE_DOS_HEADER*>(game)->e_lfanew);
  const auto* text_header = IMAGE_FIRST_SECTION(nt);
  const uint32_t text_rva = text_header->VirtualAddress;
  const uint32_t text_size = text_header->Misc.VirtualSize;
  CHECK(Matches(game + text_rva, text_size, ec::JOB_WAKE_ALL) == 1 && Matches(game + text_rva, text_size, ec::JOB_WAKE) == 1);
  CHECK(Matches(game + text_rva, text_size, ec::JOB_SLEEP) == 1 && Matches(game + text_rva, text_size, ec::JOB_STOP) == 1);
  CHECK(Matches(game + text_rva, text_size, ec::JOB_WORKER_DESTRUCTOR) == 1 && Matches(game + text_rva, text_size, JOB_WORKER_CONSTRUCTOR) == 1);
  const uint32_t wake_all_rva = build.code->job_wake_all_rva;
  const uint32_t sleep_rva = build.code->job_sleep_rva;
  const uint32_t destructor_rva = build.code->job_worker_destructor_rva;
  CHECK(ec::sites[5].at == patched + wake_all_rva && ec::sites[6].at == patched + sleep_rva);
  CHECK(ec::sites[7].at == patched + destructor_rva + ec::JOB_WORKER_DESTRUCTOR_STOP_CALL && ec::sites[7].opcode == 0xE8);
  CHECK(ec::job_table != nullptr && JobTableField(0) == 0);
  // No sentinel is left in the stub, and every call and jump in it lands in the page or on the game's functions.
  const uint8_t* stub = ec::page + ec::JOB_PAGE;
  for (size_t at = 0; at + 4 <= ec::JOB_STUB.size(); ++at) CHECK(Read32(stub + at) - ec::STUB_FIELD_SENTINEL >= 0x20);
  const uint8_t* constructor = tw::engine_tweaks::FindPattern({std::string_view(reinterpret_cast<const char*>(game + text_rva), text_size)}, JOB_WORKER_CONSTRUCTOR);
  CHECK(constructor != nullptr);
  const uint8_t* semaphore_call = constructor + JOB_WORKER_CONSTRUCTOR_SEMAPHORE_CALL;
  const uint32_t make_semaphore_rva = static_cast<uint32_t>(semaphore_call + 5 + static_cast<int32_t>(Read32(semaphore_call + 1)) - game);

  FillKernelImports(game);
  FillKernelImports(patched);
  const auto copy_of = [&](const char* name, uint8_t* image) {
    JobCopy copy = {
        .name = name,
        .image = image,
        .wake_all = reinterpret_cast<void(__fastcall*)(void*, void*)>(image + wake_all_rva),
        .sleep = reinterpret_cast<void(__fastcall*)(void*, void*)>(image + sleep_rva),
        .destroy = reinterpret_cast<void(__fastcall*)(void*, void*)>(image + destructor_rva),
        .make_semaphore = reinterpret_cast<void*(__fastcall*)(void*, void*, uint32_t, uint32_t, uint32_t, uint32_t)>(image + make_semaphore_rva),
        .pool = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)),
        .asleep = (image == patched ? reinterpret_cast<const volatile uint32_t*>(ec::job_table + 2 * 64) : nullptr),
    };
    return copy;
  };
  const JobCopy original = copy_of("the game's", game);
  const JobCopy ours = copy_of("ours", patched);
  CHECK(original.pool != nullptr && ours.pool != nullptr);
  const auto stubs_on = [&](bool on) { twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, on); };
  stubs_on(true);
  CHECK(ec::written == ((ec::CPU_CODE_SITES & ec::found) | ec::JOB_SITES));

  // 10a. One thread, no sleeper: the same state in the game's and in our copy after WakeAll (every worker's wake counter, state
  // and semaphore count; the events' signals), after Sleep (a woken worker: state 1, counter 0, count 0; a stopped one
  // untouched) and after the destructor (state 3, a pending request dropped).
  {
    constexpr uint32_t N = 7;
    const auto world = [&](const JobCopy& copy) {
      std::vector<uint8_t*> workers;
      for (uint32_t index = 0; index < N; ++index) workers.push_back(NewWorker(copy, index, N, nullptr));
      return workers;
    };
    const auto state_of = [&](const std::vector<uint8_t*>& workers) {
      std::vector<uint32_t> state;
      for (uint8_t* worker : workers) {
        for (const uint32_t at : {0x18u, 0x1Cu, 0x40u, 0x44u, 0x50u}) state.push_back(Read32(worker + at));
        const DWORD signaled = WaitForSingleObject(*reinterpret_cast<HANDLE*>(worker + 0x3C), 0);  // "not empty" (consumes it)
        state.push_back(signaled);
        if (signaled == WAIT_OBJECT_0) SetEvent(*reinterpret_cast<HANDLE*>(worker + 0x3C));
      }
      return state;
    };
    const std::vector<uint8_t*> a = world(original);
    const std::vector<uint8_t*> b = world(ours);
    const auto both = [&](auto&& call) {
      call(original, a);
      call(ours, b);
    };
    both([&](const JobCopy& copy, const std::vector<uint8_t*>&) { copy.wake_all(copy.pool, nullptr); });
    CHECK(state_of(a) == state_of(b));
    CHECK(Read32(a[3] + 0x50) == 1 && Read32(a[3] + 0x40) == 1 && Read32(a[3] + 0x18) == 2);
    both([&](const JobCopy& copy, const std::vector<uint8_t*>&) { copy.wake_all(copy.pool, nullptr); });  // counters 2, counts 1
    CHECK(state_of(a) == state_of(b) && Read32(a[3] + 0x50) == 2);
    both([&](const JobCopy& copy, const std::vector<uint8_t*>& workers) {
      for (uint8_t* worker : workers) copy.sleep(worker, nullptr);  // each takes its count
    });
    CHECK(state_of(a) == state_of(b) && Read32(a[3] + 0x50) == 0 && Read32(a[3] + 0x40) == 0 && Read32(a[3] + 0x18) == 1);
    CHECK(JobTableField(0) == Address(ours.pool) && JobTableField(1) == 0 && JobTableField(2) == 0 && JobTableField(3) == 0);  // claimed; the request taken; no bit left; no loop
    both([&](const JobCopy&, const std::vector<uint8_t*>& workers) { Write32(workers[2] + 0x18, 3); });
    both([&](const JobCopy& copy, const std::vector<uint8_t*>& workers) { copy.sleep(workers[2], nullptr); });  // stopped: returns at once
    CHECK(state_of(a) == state_of(b) && Read32(a[2] + 0x18) == 3);
    Write32(ec::job_table + 64, 1);  // a request no worker will run any more
    both([&](const JobCopy& copy, const std::vector<uint8_t*>& workers) {
      for (uint8_t* worker : workers) copy.destroy(worker, nullptr);
    });
    CHECK(JobTableField(1) == 0 && JobTableField(2) == 0 && JobTableField(3) == 0);  // Stop dropped the pending request
    for (const std::vector<uint8_t*>* workers : {&a, &b}) {
      for (uint8_t* worker : *workers) DecommitWorker(worker);
    }
    std::printf("10. job wake-ups: 7 workers, one thread: WakeAll twice, Sleep, the destructor: every worker's state, counter, count and event equal\n");
  }

  // 10b. The registers: ebx, esi, edi, ebp and esp come back as they went in, whatever eax, ecx, edx and the flags hold.
  {
    constexpr uint32_t N = 5;
    Random random = {.state = 0x5EED5};
    uint64_t calls = 0;
    for (const JobCopy* copy : {&original, &ours}) {
      for (int round = 0; round < 1000; ++round) {
        std::vector<uint8_t*> workers;
        for (uint32_t index = 0; index < N; ++index) workers.push_back(NewWorker(*copy, index, N, nullptr));
        for (const auto& [function, argument] : {std::pair{copy->wake_all, copy->pool}, std::pair{copy->sleep, workers[round % N]}, std::pair{copy->destroy, workers[(round + 1) % N]}}) {
          if (function == copy->sleep) Write32(argument + 0x40, 1);  // a count to take: no wait
          RegisterCall call = {.function = Address(reinterpret_cast<const void*>(function)), .argument = 0};
          for (uint32_t& in : call.in) in = random.Next();
          call.in[1] = Address(argument);
          CallWithRegisters(&call);
          ++calls;
          CHECK(call.esp_after == call.esp_before);
          for (const size_t kept : {3, 4, 5, 6}) CHECK(call.out[kept] == call.in[kept]);
          if (function == copy->destroy) {
            for (uint32_t index = 0; index < N; ++index) {
              if (workers[index] == argument) workers[index] = nullptr;
            }
          }
        }
        for (uint8_t* worker : workers) {
          if (worker != nullptr) DecommitWorker(worker);
        }
        // The destructor ran on one worker only; the others' semaphores stay (a test: the handles end with the process).
      }
    }
    std::printf("   ebx esi edi ebp esp kept by WakeAll, Sleep and the destructor with random registers (%llu calls)\n", calls);
  }

  // 10c. The soak: 24 workers, 8 submitters, the game's own Wake, Sleep, semaphores and destructor.
  const double seconds = (quick ? 4.0 : 15.0);
  for (const JobCopy* copy : {&original, &ours}) {
    const SoakResult result = Soak(*copy, 24, 8, seconds, nullptr);
    std::printf("   soak, %s: %llu items, %llu WakeAll requests%s: %s\n", copy->name, result.items, result.requests,
                (copy == &ours ? std::format(" ({} found a sleeper)", result.sleeper_seen).c_str() : ""),
                result.failure.empty() ? "every item once, every request's postcondition held, workers destroyed" : result.failure.c_str());
    CHECK(result.failure.empty());
    if (copy == &ours) CHECK(result.sleeper_seen > result.requests / 10);
    if (copy == &ours) CHECK(JobTableField(1) == 0 && JobTableField(2) == 0 && JobTableField(3) == 0);
  }

  // 10d. The row switched on and off while threads run (workers inside the game's Sleep when it turns on, inside ours when
  // it turns off), 200 times (60 quick).
  {
    const int target_switches = (quick ? 60 : 200);
    int switches = 0;
    bool on = true;
    const SoakResult result = Soak(ours, 24, 8, (quick ? 4.0 : 10.0), [&] {
      if (switches >= target_switches) return false;
      on = !on;
      stubs_on(on);
      ++switches;
      return true;
    });
    std::printf("   soak with %d switches: %llu items, %llu WakeAll requests: %s\n", switches, result.items, result.requests,
                result.failure.empty() ? "every item once, every postcondition held" : result.failure.c_str());
    CHECK(result.failure.empty() && switches == target_switches);
    stubs_on(true);
  }

  // 10e. Shutdown cycles: a few workers and submitters for 30 ms, then the destruction right behind the last WakeAll (a
  // delegate may still be in its loop: the drain). A worker used after its page is decommitted faults and ends the test.
  {
    const int cycles = (quick ? 150 : 600);
    uint64_t items = 0;
    Random random = {.state = 0xD00D};
    for (const JobCopy* copy : {&original, &ours}) {
      for (int cycle = 0; cycle < cycles; ++cycle) {
        const uint32_t workers = 2 + random.Next() % 23;
        const SoakResult result = Soak(*copy, workers, 1 + random.Next() % 4, 0.03, nullptr, false);
        items += result.items;
        CHECK(result.failure.empty());
        if (copy == &ours) CHECK(JobTableField(1) == 0 && JobTableField(2) == 0 && JobTableField(3) == 0);
      }
    }
    std::printf("   %d shutdown cycles each (2 to 24 workers, 1 to 4 submitters), %llu items: no fault, table clean after each\n", cycles, items);
  }

  // 10e2. A delegate thread suspended inside its wake loop while the pool is destroyed (10e meets it rarely): every Stop waits for
  // it, and the delegate resumed 400 ms later (its drain bound is 2^28 pauses, seconds) writes to no freed worker (the workers'
  // pages are decommitted: a write there faults under the delegate's exception filter).
  struct ForcedResult {
    uint32_t suspended_runs = 0;
    uint32_t gave_up_runs = 0;  // a Stop that did not wait for the suspended delegate
    uint32_t faulted_runs = 0;
  };
  const auto forced_preemption = [&] {
    ForcedResult total;
    for (const uint32_t workers : {2u, 4u, 8u, 16u}) {
      for (int repeat = 0; repeat < 2; ++repeat) {
        for (uint32_t line = 0; line < 4; ++line) *reinterpret_cast<volatile uint32_t*>(ec::job_table + 64 * line) = 0;
        std::vector<uint8_t*> table_workers;
        for (uint32_t index = 0; index < workers; ++index) table_workers.push_back(NewWorker(ours, index, workers, nullptr));
        DelegateState state;
        state.copy = &ours;
        HANDLE delegate = CreateThread(nullptr, 0, &DelegateMain, &state, 0, nullptr);
        const uint8_t* loop_begin = patched + build.code->job_wake_all_rva + 0x10;
        const uint8_t* loop_end = patched + build.code->job_wake_all_rva + 0x20;
        bool suspended = false;
        for (int attempt = 0; attempt < 5'000'000 && !suspended; ++attempt) {
          SuspendThread(delegate);
          CONTEXT context = {.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER};
          GetThreadContext(delegate, &context);
          const auto* eip = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(context.Eip));
          suspended = JobTableField(3) == 1 && eip >= loop_begin && eip < loop_end && context.Esi + 1 < context.Ebx;  // workers still to wake
          if (suspended) break;
          ResumeThread(delegate);
          for (int spin = attempt % 97; spin > 0; --spin) YieldProcessor();
        }
        if (!suspended) {
          state.stop = true;
          WaitForSingleObject(delegate, 5000);
          CloseHandle(delegate);
          for (uint8_t* worker : table_workers) {
            ours.destroy(worker, nullptr);
            DecommitWorker(worker);
          }
          continue;
        }
        ++total.suspended_runs;
        std::atomic<uint32_t> destroyed = 0;
        std::thread destroyer([&] {
          for (uint8_t* worker : table_workers) {
            ours.destroy(worker, nullptr);
            DecommitWorker(worker);
            destroyed.fetch_add(1);
          }
        });
        Sleep(400);
        if (destroyed.load() != 0) ++total.gave_up_runs;
        state.stop = true;
        ResumeThread(delegate);
        destroyer.join();
        WaitForSingleObject(delegate, 10000);
        CloseHandle(delegate);
        if (state.faults.load() != 0) ++total.faulted_runs;
      }
    }
    return total;
  };
  {
    const ForcedResult result = forced_preemption();
    CHECK(result.suspended_runs >= 4 && result.gave_up_runs == 0 && result.faulted_runs == 0);
    std::printf("   a delegate suspended in its loop while 2 to 16 workers are destroyed (%u runs): every Stop waited for it, no freed worker touched\n", result.suspended_runs);
  }

  // 10e3. CONTROL: the drain's bound cut to 4 M pauses (about 50 ms): the Stops give up and the resumed delegate wakes freed
  // workers, so the test above must be able to see it.
  {
    uint8_t* bound = ec::page + ec::JOB_PAGE + ec::JOB_STOP_STUB + 20;  // mov eax, imm32 of the drain
    CHECK(bound[-1] == 0xB8 && Read32(bound) == 0x10000000);
    DWORD protection;
    VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection);
    Write32(bound, 4'000'000);
    VirtualProtect(ec::page, ec::PAGE_SIZE, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), ec::page, ec::PAGE_SIZE);
    const ForcedResult result = forced_preemption();
    VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection);
    Write32(bound, 0x10000000);
    VirtualProtect(ec::page, ec::PAGE_SIZE, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), ec::page, ec::PAGE_SIZE);
    for (uint32_t line = 0; line < 4; ++line) *reinterpret_cast<volatile uint32_t*>(ec::job_table + 64 * line) = 0;  // a faulted delegate never left its loop
    std::printf("   CONTROL, the drain cut to 4 M pauses (must fail): %u of %u runs gave up, %u faulted\n", result.gave_up_runs, result.suspended_runs, result.faulted_runs);
    CHECK(result.suspended_runs >= 4 && result.gave_up_runs != 0);
  }

  // 10f. More workers than the mask has bits: the game's own loop (the stub stands down), same soak.
  {
    const SoakResult result = Soak(ours, 33, 6, (quick ? 2.0 : 5.0), nullptr);
    std::printf("   soak with 33 workers (the game's loop): %llu items: %s\n", result.items, result.failure.empty() ? "every item once" : result.failure.c_str());
    CHECK(result.failure.empty());
  }

  // 10g. CONTROL: the `lock or [pending], 1` made a no-op: the delegated wake-up then wakes one worker that has nothing to
  // run, so the soak must notice (a lost wake-up or a worker that never looked).
  {
    uint8_t* patched_stub = ec::page + ec::JOB_PAGE;
    uint8_t* immediate = patched_stub + 0x1F + 7;  // lock or dword ptr [pending], 1
    CHECK(*immediate == 1);
    DWORD protection;
    VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection);
    *immediate = 0;
    VirtualProtect(ec::page, ec::PAGE_SIZE, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), ec::page, ec::PAGE_SIZE);
    const SoakResult result = Soak(ours, 24, 8, 3.0, nullptr);
    VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection);
    *immediate = 1;
    VirtualProtect(ec::page, ec::PAGE_SIZE, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), ec::page, ec::PAGE_SIZE);
    std::printf("   CONTROL, the request never recorded (must fail): %s\n", result.failure.empty() ? "NOT NOTICED" : result.failure.c_str());
    CHECK(!result.failure.empty());
    // The workers asleep, the queue left over: the table is reusable (pending cleared by the delegates).
    CHECK(Soak(ours, 8, 2, 1.0, nullptr).failure.empty());
  }

  // 10h. What the render thread pays: WakeAll with 15 workers asleep (a gap between the calls long enough for them to go
  // back to sleep), the game's against ours.
  {
    std::string line;
    for (const JobCopy* copy : {&original, &ours}) {
      JobRun run;
      run.copy = copy;
      run.count = 15;
      run.capacity = 1;
      run.consumed = std::make_unique<std::atomic<uint8_t>[]>(1);
      run.looks = std::make_unique<std::atomic<uint64_t>[]>(15);
      std::vector<WorkerContext> contexts(15);
      run.workers.resize(15);
      Write32(copy->pool + 4, 15);
      for (uint32_t index = 0; index < 15; ++index) {
        contexts[index] = {&run, index};
        HANDLE thread = CreateThread(nullptr, 0, &JobWorkerMain, &contexts[index], CREATE_SUSPENDED, nullptr);
        run.workers[index] = NewWorker(*copy, index, 15, thread);
      }
      for (uint32_t index = 0; index < 15; ++index) ResumeThread(*reinterpret_cast<HANDLE*>(run.workers[index] + 0xC));
      LARGE_INTEGER frequency;
      QueryPerformanceFrequency(&frequency);
      std::vector<double> microseconds;
      for (int call = 0; call < 3000; ++call) {
        LARGE_INTEGER start, end;
        QueryPerformanceCounter(&start);
        do {
          _mm_pause();
          QueryPerformanceCounter(&end);
        } while (end.QuadPart - start.QuadPart < frequency.QuadPart / 2500);  // 400 us
        QueryPerformanceCounter(&start);
        copy->wake_all(copy->pool, nullptr);
        QueryPerformanceCounter(&end);
        microseconds.push_back(1e6 * (end.QuadPart - start.QuadPart) / frequency.QuadPart);
      }
      std::sort(microseconds.begin(), microseconds.end());
      line += std::format("{}{} {:.1f} us (p90 {:.1f})", line.empty() ? "" : ", ", copy->name, microseconds[microseconds.size() / 2], microseconds[microseconds.size() * 9 / 10]);
      for (uint32_t index = 0; index < 15; ++index) {
        copy->destroy(run.workers[index], nullptr);
        DecommitWorker(run.workers[index]);
      }
    }
    std::printf("   median call of WakeAll with 15 workers asleep: %s\n", line.c_str());
  }
  stubs_on(false);
  twattila::engine_code::Update(true);
  return 0;
}

// ---- 11. The sprite row builder's 20 half floats (engine_code.hpp 7.): the region of +0x1907511, entered with a fake frame ----

// The region entered with esp = F in the call's frame, eax ecx edx ebx esi edi set, xmm0 the first float, xmm1 to xmm7
// random; its end (HALF_ROWS_END, jumped to by both copies) returns to the harness with the registers, esp and the xmm
// registers saved. ebp is the call itself in both copies (the region leaves it alone).
struct RowsCall {
  uint32_t in[7];             // 0: eax ecx edx ebx esi edi (ebp: the call)
  uint32_t block;             // 28
  uint32_t saved_esp;         // 32
  uint32_t esp_after;         // 36
  uint32_t out[7];            // 40: eax ecx edx ebx esi edi ebp
  uint32_t xmm0_in;           // 68
  uint32_t xmm0_out;          // 72
  uint32_t unused;            // 76
  alignas(16) uint8_t xmm_in[112];   // 80: xmm1 to xmm7
  alignas(16) uint8_t xmm_out[112];  // 192
  alignas(16) uint8_t frame[0x200];  // 304: F = frame + 0x100
  // The state and fault parts of half_rows_adversarial.inc:
  uint32_t mxcsr_in;                  // 816
  uint32_t mxcsr_out;                 // 820
  uint32_t mxcsr_saved;               // 824
  uint32_t frame_top;                 // 828: EnterRows4: F, in a stack of its own
  alignas(16) uint8_t xmm0_full[16];  // 832
  alignas(16) uint8_t fx_in[512];     // 848
  alignas(16) uint8_t fx_out[512];    // 1360
  alignas(16) uint8_t fx_saved[512];  // 1872
  uint32_t faulted;                   // 2384: the fault tests
  uint32_t fault_address;             // 2388
  uint32_t fault_write;               // 2392
  uint32_t fault_eip;                 // 2396
  uint32_t fault_esp;                 // 2400
  uint32_t fault_regs[8];             // 2404
};
static_assert(offsetof(RowsCall, frame) == 304 && offsetof(RowsCall, xmm_out) == 192 && offsetof(RowsCall, out) == 40);
static_assert(offsetof(RowsCall, mxcsr_in) == 816 && offsetof(RowsCall, xmm0_full) == 832 && offsetof(RowsCall, fx_in) == 848 && offsetof(RowsCall, fx_out) == 1360
              && offsetof(RowsCall, fx_saved) == 1872);

__declspec(naked) void __cdecl EnterRows(RowsCall*) {
  __asm {
    pushad
    mov ebp, [esp + 36]
    mov [ebp + 32], esp
    movups xmm1, [ebp + 80]
    movups xmm2, [ebp + 96]
    movups xmm3, [ebp + 112]
    movups xmm4, [ebp + 128]
    movups xmm5, [ebp + 144]
    movups xmm6, [ebp + 160]
    movups xmm7, [ebp + 176]
    movss xmm0, [ebp + 68]
    lea esp, [ebp + 560]  // F = 304 + 0x100
    mov eax, [ebp + 0]
    mov ecx, [ebp + 4]
    mov edx, [ebp + 8]
    mov ebx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]
    jmp dword ptr [ebp + 28]
  }
}

// The region's end: esp is F - 0x10 there in both copies.
__declspec(naked) void RowsReturn() {
  __asm {
    mov [esp - 8], eax
    lea eax, [esp - 544]  // the call: esp = F - 0x10 = the call + 560 - 16
    mov [eax + 44], ecx
    mov [eax + 48], edx
    mov [eax + 52], ebx
    mov [eax + 56], esi
    mov [eax + 60], edi
    mov [eax + 64], ebp
    mov ecx, [esp - 8]
    mov [eax + 40], ecx
    mov [eax + 36], esp
    movss [eax + 72], xmm0
    movups [eax + 192], xmm1
    movups [eax + 208], xmm2
    movups [eax + 224], xmm3
    movups [eax + 240], xmm4
    movups [eax + 256], xmm5
    movups [eax + 272], xmm6
    movups [eax + 288], xmm7
    mov esp, [eax + 32]
    popad
    ret
  }
}

// One thread's world: the call with its frame, and the arena the pointers lead to (the source object, the second object, the
// row). It stays at one address: every run starts from a copy of the same inputs.
struct RowsWorld {
  RowsCall call;
  alignas(16) std::array<uint8_t, 0x400> arena;
};

// What the game's region reads: the frame's float slots (and the int at +0x20), the source object's floats, the second
// object's and xmm0.
constexpr std::array<uint32_t, 8> ROWS_FRAME_FLOATS = {0x24, 0x44, 0x48, 0x4C, 0x50, 0x58, 0x70, 0x74};
constexpr std::array<uint32_t, 8> ROWS_SOURCE_FLOATS = {0x1C, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40};

// A float's bits: from the classes the conversion tells apart (normals, the rounding's ties, exact halves, zeros, tiny values
// and the edges between the classes), or `special`: half denormals, overflow, infinities, NaNs, any bits.
uint32_t RowsFloat(Random* random, bool special) {
  const uint32_t sign = (random->Next() & 1) << 31;
  const uint32_t mantissa = random->Next() & 0x7FFFFF;
  if (special) {
    switch (random->Next() % 4) {
      case 0: return sign | (103 + random->Next() % 10) << 23 | mantissa;
      case 1: return sign | (143 + random->Next() % 113) << 23 | mantissa;
      case 2: return sign | std::array<uint32_t, 6>{0x7F800000, 0x7FC00000, 0x47800000, 0x33800000, 0x387FFFFF, 0x33800001}[random->Next() % 6];
      default: return random->Next();
    }
  }
  switch (random->Next() % 9) {
    case 0: return sign | (113 + random->Next() % 30) << 23 | mantissa;
    case 1: return sign | (113 + random->Next() % 30) << 23 | (mantissa & ~0x1FFFu) | std::array<uint32_t, 6>{0, 0x1000, 0x0FFF, 0x1001, 0x1FFF, 0x0800}[random->Next() % 6];
    case 2: return sign;
    case 3: return sign | (random->Next() % 103) << 23 | mantissa;
    case 4: return sign | std::array<uint32_t, 8>{0x33000400, 0x33000401, 0x337FFFFF, 0x38800000, 0x477FFFFF, 0x33000000, 0x38800001, 0x477FE000}[random->Next() % 8];
    case 5: return sign | (120 + random->Next() % 16) << 23 | mantissa;
    case 6: return sign | (random->Next() % 4 == 0 ? 0u : 0x3F800000u);
    case 7: return sign | (100 + random->Next() % 3) << 23 | mantissa;
    default: return sign | (113 + random->Next() % 30) << 23 | (mantissa & 0x7FE000);
  }
}

// The 20 floats' bits as the game's region converts them (the products as mulss, the int as cvtdq2ps).
std::array<uint32_t, 20> RowsLanes(const RowsWorld& world) {
  const uint8_t* frame = world.call.frame + 0x100;
  const auto* source = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(world.call.in[4]));
  const auto* second = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(Read32(frame + 0x5C)));
  const auto mul = [](uint32_t a, uint32_t b) {
    float x, y;
    std::memcpy(&x, &a, 4);
    std::memcpy(&y, &b, 4);
    const float product = _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(x), _mm_set_ss(y)));
    uint32_t bits;
    std::memcpy(&bits, &product, 4);
    return bits;
  };
  const float converted = _mm_cvtss_f32(_mm_cvtepi32_ps(_mm_cvtsi32_si128(static_cast<int>(Read32(frame + 0x20)))));
  uint32_t from_int;
  std::memcpy(&from_int, &converted, 4);
  return {world.call.xmm0_in, Read32(frame + 0x24), Read32(source + 0x1C), from_int, Read32(frame + 0x70), Read32(frame + 0x58),
          mul(Read32(frame + 0x50), Read32(frame + 0x74)), Read32(source + 0x40), mul(Read32(frame + 0x74), Read32(frame + 0x4C)),
          Read32(frame + 0x48), 0, Read32(frame + 0x44), Read32(source + 0x28), Read32(source + 0x2C), Read32(source + 0x30),
          Read32(source + 0x34), Read32(source + 0x38), Read32(source + 0x3C), Read32(second + 0x48), Read32(second + 0x4C)};
}

// Whether the stub's vector path takes these lanes: every one zero, tiny (below 0x33800000 without its sign) or normal.
bool RowsFast(const std::array<uint32_t, 20>& lanes) {
  for (const uint32_t bits : lanes) {
    const uint32_t a = bits & 0x7FFFFFFF;
    if (a >= 0x33800000 && !(a >= 0x38800000 && a < 0x47800000)) return false;
  }
  return true;
}

// A random world: random bytes everywhere, the floats the region reads from `RowsFloat` (one draw in `special_rate` special,
// 0: none), the objects and the row apart in the arena.
void RowsRandom(RowsWorld* world, Random* random, uint32_t special_rate) {
  for (uint8_t& byte : world->arena) byte = static_cast<uint8_t>(random->Next());
  RowsCall* call = &world->call;
  for (uint8_t& byte : call->frame) byte = static_cast<uint8_t>(random->Next());
  for (uint8_t& byte : call->xmm_in) byte = static_cast<uint8_t>(random->Next());
  for (uint32_t& value : call->in) value = random->Next();
  const auto draw = [&] { return RowsFloat(random, special_rate != 0 && random->Next() % special_rate == 0); };
  uint8_t* frame = call->frame + 0x100;
  uint8_t* source = world->arena.data();
  uint8_t* second = world->arena.data() + 0x100;
  call->xmm0_in = draw();
  for (const uint32_t at : ROWS_FRAME_FLOATS) Write32(frame + at, draw());
  Write32(frame + 0x20, static_cast<uint32_t>(special_rate != 0 && random->Next() % special_rate == 0 ? static_cast<int32_t>(random->Next()) : static_cast<int32_t>(random->Next() % 131071) - 65535));
  for (const uint32_t at : ROWS_SOURCE_FLOATS) Write32(source + at, draw());
  Write32(second + 0x48, draw());
  Write32(second + 0x4C, draw());
  call->in[4] = Address(source);
  call->in[6] = 0;
  Write32(frame + 0x5C, Address(second));
  Write32(frame + 0x68, Address(world->arena.data() + 0x220));
}

// One run from `backup`: the world entered at `block`, then copied to `out` (the call and the arena as they are after).
void RunRows(uint32_t block, RowsWorld* world, const RowsWorld& backup, RowsWorld* out) {
  *world = backup;
  world->call.block = block;
  EnterRows(&world->call);
  *out = *world;
}

// Equal after one run (A the game's code, B the patched one): every register, esp, xmm0, the frame from F up, the arena
// (the row and the objects); A leaves xmm1 to xmm7 alone. `fast`: B took the vector path (the pushed argument slots below
// F differ: the stub pushes only the last, the game's code all 4).
thread_local const char* rows_difference = "";
bool RowsEqual(const RowsWorld& world, const RowsWorld& backup, const RowsWorld& a, const RowsWorld& b, bool* fast) {
  const uint32_t frame_top = Address(world.call.frame) + 0x100;
  *fast = std::memcmp(a.call.frame + 0xF0, b.call.frame + 0xF0, 16) != 0;
  rows_difference = "";
  if (std::memcmp(a.call.out, b.call.out, sizeof(a.call.out)) != 0) rows_difference = "registers";
  else if (a.call.esp_after != b.call.esp_after || a.call.esp_after != frame_top - 0x10) rows_difference = "esp";
  else if (a.call.xmm0_out != b.call.xmm0_out) rows_difference = "xmm0";
  else if (b.call.out[6] != Address(&world.call)) rows_difference = "ebp";
  else if (std::memcmp(a.call.frame + 0x100, b.call.frame + 0x100, 0x100) != 0) rows_difference = "frame";
  else if (a.arena != b.arena) {
    rows_difference = "arena";
    for (size_t at = 0; at < a.arena.size(); ++at) {
      if (a.arena[at] != b.arena[at]) Report("    arena +0x%zX: game %02X ours %02X (before %02X)\n", at, a.arena[at], b.arena[at], backup.arena[at]);
    }
  }
  else if (std::memcmp(a.call.xmm_out, backup.call.xmm_in, sizeof(a.call.xmm_out)) != 0) rows_difference = "the game's xmm registers";
  return rows_difference[0] == 0;
}

int RunRowsTests(const TestBuild& build, uint8_t* game, uint8_t* patched, uint32_t text_rva, uint32_t text_size, bool quick) {
  namespace ec = twattila::engine_code::internal;
  const uint32_t region_rva = build.code->half_rows_rva;
  const uint32_t end_rva = region_rva + ec::HALF_ROWS_END;
  CHECK(ec::sites[8].at == patched + region_rva && (ec::written >> 8 & 1) != 0);
  CHECK(Matches(game + text_rva, text_size, ec::HALF_ROWS) == 1);
  // No sentinel is left in the stub, and its constants are where it expects them.
  const uint8_t* stub = ec::page + ec::HALF_ROWS_PAGE;
  for (size_t at = 0; at + 4 <= ec::HALF_ROWS_STUB.size(); ++at) {
    CHECK(Read32(stub + at) - ec::STUB_CONSTANT_SENTINEL >= 0x100 && Read32(stub + at) - ec::STUB_CALL_SENTINEL >= 0x10);
  }
  CHECK(std::memcmp(stub + ec::HALF_ROWS_CONSTANTS_AT, ec::HALF_ROWS_CONSTANTS.data(), sizeof(ec::HALF_ROWS_CONSTANTS)) == 0);
  std::array<uint8_t, 5> game_end, patched_end;
  std::memcpy(game_end.data(), game + end_rva, 5);
  std::memcpy(patched_end.data(), patched + end_rva, 5);
  JumpTo(game + end_rva, reinterpret_cast<const void*>(&RowsReturn));
  JumpTo(patched + end_rva, reinterpret_cast<const void*>(&RowsReturn));
  const uint32_t game_block = Address(game + region_rva);
  const uint32_t patched_block = Address(patched + region_rva);
  auto world = std::make_unique<RowsWorld>();
  auto backup = std::make_unique<RowsWorld>();
  auto a = std::make_unique<RowsWorld>();
  auto b = std::make_unique<RowsWorld>();

  // 11a. Random calls: every other one with a special lane in 24 draws or so. The vector path must be taken exactly when every
  // one of the 20 lanes is a zero, a tiny value or a normal.
  {
    Random random = {.state = 0xA11CE};
    uint64_t fast = 0, slow = 0, wrong = 0;
    for (int run = 0; run < 2'000'000; ++run) {
      RowsRandom(world.get(), &random, (run & 1) != 0 ? 24 : 0);
      *backup = *world;
      const bool expect_fast = RowsFast(RowsLanes(*world));
      RunRows(game_block, world.get(), *backup, a.get());
      RunRows(patched_block, world.get(), *backup, b.get());
      bool took_fast;
      const bool same = RowsEqual(*world, *backup, *a, *b, &took_fast);
      (took_fast ? fast : slow)++;
      if (!same || took_fast != expect_fast) {
        ++wrong;
        Report("  rows: run %d, same %d (%s), took the vector path %d, expected %d\n", run, same ? 1 : 0, rows_difference, took_fast ? 1 : 0, expect_fast ? 1 : 0);
      }
    }
    CHECK(wrong == 0 && fast > 200'000 && slow > 100'000);
    std::printf("11. sprite row builder: 2 M random calls: registers, esp, frame, row and objects, xmm0 equal; vector path %llu, the game's code %llu (the calls with a special lane)\n", fast, slow);
  }

  // 11b. Pointers that overlap: the row against the source object, the second object and the frame, each object against the frame's
  // temporaries, at every offset around the checks' edges: inside, the game's code; outside, the vector path; equal either way.
  {
    Random random = {.state = 0xA11A5};
    uint64_t cases = 0, fast = 0;
    for (int which = 0; which < 5; ++which) {
      for (int offset = -0x90; offset <= 0x90; ++offset) {
        // A row over the frame's two pointer slots (the second object's at F+0x5C, the row's at F+0x68) breaks the game's own code
        // (it reads the pointers again after the halves' stores): not a case.
        if (which == 2 && ((offset + 8 < 0x60 && offset + 0x30 > 0x5C) || (offset + 8 < 0x6C && offset + 0x30 > 0x68))) continue;
        // An object whose floats lie below F is on the dead stack, where the game's pushed arguments and the callee's frames write:
        // no live object is there (the row is covered either way, its check reaches 0x6F below F).
        if ((which == 3 || which == 4) && offset < -0x1C) continue;
        for (int round = 0; round < 40; ++round) {
          RowsRandom(world.get(), &random, 0);
          uint8_t* frame_top = world->call.frame + 0x100;
          uint8_t* arena = world->arena.data();
          uint32_t row = Address(arena + 0x220);
          uint32_t source = Address(arena);
          uint32_t second = Address(arena + 0x100);
          switch (which) {
            case 0: row = source + offset; break;
            case 1: row = second + offset; break;
            case 2: row = Address(frame_top) + offset; break;
            case 3: source = Address(frame_top) + offset; break;
            default: second = Address(frame_top) + offset; break;
          }
          // The floats the objects hold (they may be the frame's own bytes or the row's), then the pointers.
          for (const uint32_t at : ROWS_SOURCE_FLOATS) Write32(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(source)) + at, RowsFloat(&random, false));
          Write32(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(second)) + 0x48, RowsFloat(&random, false));
          Write32(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(second)) + 0x4C, RowsFloat(&random, false));
          world->call.in[4] = source;
          Write32(frame_top + 0x5C, second);
          Write32(frame_top + 0x68, row);
          *backup = *world;
          RunRows(game_block, world.get(), *backup, a.get());
          RunRows(patched_block, world.get(), *backup, b.get());
          bool took_fast;
          const bool same = RowsEqual(*world, *backup, *a, *b, &took_fast);
          ++cases;
          fast += took_fast ? 1 : 0;
          if (!same) {
            Report("  rows alias: kind %d offset %d (%d): NOT equal (%s; the vector path %d)\n", which, offset, round, rows_difference, took_fast ? 1 : 0);
            CHECK(same);
          }
        }
      }
    }
    CHECK(fast > cases / 20 && fast < cases);
    std::printf("   %llu calls with overlapping pointers (5 kinds, every offset -0x90 .. 0x90 around the edges): equal, the vector path in %llu\n", cases, fast);
  }

  // 11c. Every bit pattern in every lane at once: one pattern a call in all 18 float lanes (the integer lane fixed at 1000, the
  // zero lane at 0; the product lanes x * 1.0 and 1.0 * x give x), 2^32 calls (2^28 quick): the 20 halves, registers and frame equal.
  {
    const uint64_t patterns = (quick ? (1ull << 28) : (1ull << 32));
    std::atomic<uint64_t> fast_calls = 0;
    const auto start = std::chrono::steady_clock::now();
    const uint64_t wrong = Parallel(patterns, [&](uint64_t first, uint64_t last, unsigned worker) -> uint64_t {
      auto in = std::make_unique<RowsWorld>();
      auto saved = std::make_unique<RowsWorld>();
      auto ra = std::make_unique<RowsWorld>();
      auto rb = std::make_unique<RowsWorld>();
      Random random = {.state = 0x5A17ull ^ (first * 0x2545F4914F6CDD1Dull) ^ worker};
      RowsRandom(in.get(), &random, 0);
      uint8_t* frame = in->call.frame + 0x100;
      uint8_t* source = in->arena.data();
      uint8_t* second = in->arena.data() + 0x100;
      Write32(frame + 0x20, 1000);
      uint64_t bad = 0;
      uint64_t fast_here = 0;
      for (uint64_t index = first; index < last; ++index) {
        const auto bits = static_cast<uint32_t>(index * 0x9E3779B1ull);  // every 32-bit pattern once over 2^32 calls, spread over the whole range when quick
        in->call.xmm0_in = bits;
        Write32(frame + 0x74, 0x3F800000);  // 1.0 (the region leaves its last float there): both products give the pattern
        // The pushed argument slots as the previous run left them would hide which path a run took (RowsEqual compares them).
        for (uint32_t slot = 0; slot < 4; ++slot) Write32(frame - 0x10 + 4 * slot, random.Next());
        for (const uint32_t at : {0x24u, 0x70u, 0x58u, 0x50u, 0x4Cu, 0x48u, 0x44u}) Write32(frame + at, bits);
        for (const uint32_t at : ROWS_SOURCE_FLOATS) Write32(source + at, bits);
        Write32(second + 0x48, bits);
        Write32(second + 0x4C, bits);
        *saved = *in;
        RunRows(game_block, in.get(), *saved, ra.get());
        RunRows(patched_block, in.get(), *saved, rb.get());
        bool took_fast;
        if (!RowsEqual(*in, *saved, *ra, *rb, &took_fast)) {
          ++bad;
          Report("  rows all lanes %08X: NOT equal (eax %08X / %08X)\n", bits, ra->call.out[0], rb->call.out[0]);
        }
        fast_here += took_fast ? 1 : 0;
      }
      fast_calls += fast_here;
      return bad;
    });
    CHECK(wrong == 0);
    std::printf("   every bit pattern in all 18 float lanes at once, %llu calls (%llu on the vector path): the 20 halves, registers, frame equal (%.0f s)\n", patterns, fast_calls.load(),
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
  }

  // 11d. The sites switched off and on 200 times while 4 threads run both copies' regions.
  {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> calls = 0;
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        auto in = std::make_unique<RowsWorld>();
        auto saved = std::make_unique<RowsWorld>();
        auto ra = std::make_unique<RowsWorld>();
        auto rb = std::make_unique<RowsWorld>();
        Random random = {.state = 0xFEEDull ^ static_cast<uint64_t>(worker)};
        while (!stop.load()) {
          RowsRandom(in.get(), &random, (random.Next() & 3) == 0 ? 24 : 0);
          *saved = *in;
          RunRows(game_block, in.get(), *saved, ra.get());
          RunRows(patched_block, in.get(), *saved, rb.get());
          bool took_fast;
          if (!RowsEqual(*in, *saved, *ra, *rb, &took_fast)) ++bad;
          ++calls;
        }
      });
    }
    int busy = 0;
    bool on = true;
    for (int switches = 0; switches < 200; ++switches) {
      on = !on;
      int tries = 0;
      do {
        twattila::engine_code::Update(on);
        ++tries;
      } while ((ec::written != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
    }
    twattila::engine_code::Update(true);
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("   200 switches while 4 threads made %llu calls of each copy: %llu not equal (%d writes waited for a thread inside a site)\n", calls.load(), bad.load(), busy);
    CHECK(bad.load() == 0 && (ec::written >> 8 & 1) != 0);
  }

  // 11e. Timing, one thread, the same all-normal call: the game's region with its float to half, the patched copy with only the
  // other sites (today: the half stub's 20 calls), and with this region too.
  {
    Random random = {.state = 0x71E};
    RowsRandom(world.get(), &random, 0);
    *backup = *world;
    CHECK(RowsFast(RowsLanes(*world)));
    const auto time = [&](uint32_t block) {
      constexpr int CALLS = 2'000'000;
      const auto begin = std::chrono::steady_clock::now();
      for (int index = 0; index < CALLS; ++index) RunRows(block, world.get(), *backup, a.get());
      return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() * 1e9 / CALLS;
    };
    const double game_ns = time(game_block);
    const double both_ns = time(patched_block);
    DWORD protection;
    VirtualProtect(patched + region_rva, 5, PAGE_EXECUTE_READWRITE, &protection);
    std::memcpy(patched + region_rva, ec::sites[8].game.data(), 5);
    const double today_ns = time(patched_block);
    std::memcpy(patched + region_rva, ec::sites[8].jump.data(), 5);
    VirtualProtect(patched + region_rva, 5, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    std::printf("   ns a call (with the harness's entry and copies), the game's / half stub only (today) / half stub + this region: %.1f / %.1f / %.1f\n", game_ns, today_ns, both_ns);
  }
  std::memcpy(game + end_rva, game_end.data(), 5);
  std::memcpy(patched + end_rva, patched_end.data(), 5);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  return 0;
}

// ---- 12. The effect tree's child sum (engine_code.hpp 8.): +0x1890b90, prefetches of the children K - 1 and K / 2 - 1 after the next one ----

// The getters the children's vtable slot 7 (+0x1c) points to (thiscall: ecx the child, the values at +8 and +12). The first two count
// their calls; the mutating one also sets its parent's size at the sum_trigger-th call (the game reads the bounds again after
// every call), the moving one the size and the data pointer; the timed one only returns the value.
uint32_t sum_parent = 0;
uint32_t sum_calls = 0;
uint32_t sum_trigger = 0;
uint32_t sum_new_size = 0;
uint32_t sum_new_data = 0;

__declspec(naked) void SumGetPlain() {
  __asm {
    mov eax, [ecx + 8]
    inc sum_calls
    ret
  }
}

__declspec(naked) void SumGetAdd() {
  __asm {
    mov eax, [ecx + 8]
    add eax, [ecx + 12]
    inc sum_calls
    ret
  }
}

__declspec(naked) void SumGetMutating() {
  __asm {
    mov eax, [ecx + 8]
    mov edx, sum_calls
    inc edx
    mov sum_calls, edx
    cmp edx, sum_trigger
    jne done
    mov ecx, sum_parent
    mov edx, sum_new_size
    mov [ecx + 0x64], edx
  done:
    ret
  }
}

// Sets the parent's size and data pointer at the sum_trigger-th call: the vector's window moves inside its block (the old block stays
// readable, the end where it was: the loop still stops there).
__declspec(naked) void SumGetMoving() {
  __asm {
    mov eax, [ecx + 8]
    mov edx, sum_calls
    inc edx
    mov sum_calls, edx
    cmp edx, sum_trigger
    jne done
    mov ecx, sum_parent
    mov edx, sum_new_size
    mov [ecx + 0x64], edx
    mov edx, sum_new_data
    mov [ecx + 0x68], edx
  done:
    ret
  }
}

__declspec(naked) void SumGetTimed() {
  __asm {
    mov eax, [ecx + 8]
    ret
  }
}

// A getter that keeps the core busy for sum_busy cycles of dependent work after its load (a getter with real work in it).
uint32_t sum_busy = 0;
__declspec(naked) void SumGetBusy() {
  __asm {
    mov eax, [ecx + 8]
    mov edx, sum_busy
    test edx, edx
    jz done
  spin:
    dec edx
    jnz spin
  done:
    ret
  }
}

constexpr size_t SUM_SLOTS = 48;

// `this` (+0x64 the size, +0x68 the data), the children (+0 the vtable, +8 and +12 values), four vtables, and the data: 48
// slots that end where the page's next one is no access (a load past them faults); the page's room below them is readable.
struct SumWorld {
  alignas(64) uint8_t parent[0x80];
  alignas(64) uint8_t children[SUM_SLOTS][64];
  alignas(64) uint32_t vtables[4][8];
  uint32_t* slots;
};

struct SumSpec {
  uint32_t size;
  uint32_t kind[SUM_SLOTS];  // 0 plain, 1 add, 2 mutating, 3 moving
  uint32_t first[SUM_SLOTS];
  uint32_t second[SUM_SLOTS];
  uint32_t garbage_from;  // the slots from here on hold invalid pointers
  uint32_t trigger;       // the mutating or moving getter's call that changes the vector (0: never)
  uint32_t new_size;
  int32_t shift;          // the moving getter: the data pointer moves by this many slots (the size by the opposite), the end stays
};

struct SumResult {
  ReadyCall call;
  uint32_t size_after;
  uint32_t data_after;
  uint32_t calls;
};

std::unique_ptr<SumWorld> MakeSumWorld() {
  auto world = std::make_unique<SumWorld>();
  std::memset(world.get(), 0, sizeof(SumWorld));
  auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  DWORD protection;
  VirtualProtect(memory + 0x1000, 0x1000, PAGE_NOACCESS, &protection);
  world->slots = reinterpret_cast<uint32_t*>(memory + 0x1000 - SUM_SLOTS * sizeof(uint32_t));
  world->vtables[0][7] = Address(reinterpret_cast<const void*>(&SumGetPlain));
  world->vtables[1][7] = Address(reinterpret_cast<const void*>(&SumGetAdd));
  world->vtables[2][7] = Address(reinterpret_cast<const void*>(&SumGetMutating));
  world->vtables[3][7] = Address(reinterpret_cast<const void*>(&SumGetMoving));
  return world;
}

void SumSetup(SumWorld* world, const SumSpec& spec) {
  constexpr std::array<uint32_t, 3> GARBAGE = {0x10, 0xDEAD0000, 0xFFFFFFF0};
  for (size_t index = 0; index < SUM_SLOTS; ++index) {
    uint8_t* child = world->children[index];
    Write32(child, Address(world->vtables[spec.kind[index]]));
    Write32(child + 8, spec.first[index]);
    Write32(child + 12, spec.second[index]);
    world->slots[index] = (index >= spec.garbage_from ? GARBAGE[index % 3] : Address(child));
  }
  Write32(world->parent + 0x64, spec.size);
  Write32(world->parent + 0x68, Address(world->slots));
  sum_parent = Address(world->parent);
  sum_calls = 0;
  sum_trigger = spec.trigger;
  sum_new_size = spec.new_size;
  sum_new_data = Address(world->slots) + 4u * static_cast<uint32_t>(spec.shift);
}

void SumRandom(SumSpec* spec, Random* random) {
  const uint32_t mode = random->Next() % 5;
  spec->size = random->Next() % 41;
  for (size_t index = 0; index < SUM_SLOTS; ++index) {
    spec->kind[index] = (mode == 0 ? random->Next() % 2 : (mode == 4 ? 3 : 2));
    spec->first[index] = random->Next() % 1000;
    spec->second[index] = random->Next() % 1000;
  }
  spec->garbage_from = SUM_SLOTS;
  spec->trigger = 0;
  spec->new_size = spec->size;
  spec->shift = 0;
  if (mode == 0) {
    spec->garbage_from = spec->size;  // the slots past the size are never read
  } else if (mode == 1 && spec->size >= 1) {  // truncated at a call: the rest never read, whatever they hold
    spec->trigger = 1 + random->Next() % spec->size;
    spec->new_size = spec->trigger;
    spec->garbage_from = spec->trigger;
  } else if (mode == 2 && spec->size >= 1) {  // one to eight more children, within the 48 slots
    spec->trigger = 1 + random->Next() % spec->size;
    spec->new_size = std::min<uint32_t>(spec->size + 1 + random->Next() % 8, SUM_SLOTS);
  } else if (mode == 3 && spec->size >= 2) {  // the last one removed
    spec->trigger = 1 + random->Next() % (spec->size - 1);
    spec->new_size = spec->size - 1;
  } else if (mode == 4 && spec->size >= 1) {  // the window moves inside the block: up to 8 slots back or up to the end, the end stays
    spec->garbage_from = spec->size;
    spec->trigger = 1 + random->Next() % spec->size;
    spec->shift = static_cast<int32_t>(random->Next() % (spec->size + 9)) - 8;
    spec->new_size = spec->size - static_cast<uint32_t>(spec->shift);
  }
}

// One run of the function at `function` on a world: every register random, the arithmetic flags random.
void RunSum(SumWorld* world, const SumSpec& spec, uint32_t function, const std::array<uint32_t, 7>& in, uint32_t flags, SumResult* result) {
  SumSetup(world, spec);
  std::memset(&result->call, 0, sizeof(ReadyCall));
  std::memcpy(result->call.in, in.data(), sizeof(result->call.in));
  result->call.in[1] = Address(world->parent);
  result->call.flags_in = (flags & 0x8D5) | 2;
  ready_function = function;
  CallReady(&result->call);
  result->size_after = Read32(world->parent + 0x64);
  result->data_after = Read32(world->parent + 0x68);
  result->calls = sum_calls;
}

bool SumEqual(const SumResult& a, const SumResult& b, const std::array<uint32_t, 7>& in) {
  // esp as an offset into each run's own ReadyCall.
  return std::memcmp(a.call.out, b.call.out, sizeof(a.call.out)) == 0 && a.call.flags_out == b.call.flags_out
         && a.call.esp_after - Address(&a.call) == b.call.esp_after - Address(&b.call) && a.size_after == b.size_after
         && a.data_after == b.data_after && a.calls == b.calls && a.call.out[3] == in[3] && a.call.out[4] == in[4] && a.call.out[5] == in[5]
         && a.call.out[6] == in[6];
}

// The loop's bottom entered by itself: the game's two instructions and the stub's copy (its jump to the loop's `jne` pointing
// back at the harness), the registers, the flags and esp saved after; eax is not compared (the loop writes it before it reads it).
struct SumBlockCall {
  uint32_t in[7];      // 0: eax ecx edx ebx esi edi ebp
  uint32_t flags_in;   // 28
  uint32_t out[7];     // 32
  uint32_t flags_out;  // 60
  uint32_t saved_esp;  // 64
  uint32_t esp_after;  // 68
};
uint32_t sum_block_target = 0;
SumBlockCall* sum_block_call = nullptr;
uint32_t sum_block_eax = 0;
uint32_t sum_block_flags = 0;

__declspec(naked) void __cdecl EnterSumBlock(SumBlockCall*) {
  __asm {
    pushad
    mov ebp, [esp + 36]
    mov sum_block_call, ebp
    mov [ebp + 64], esp
    push dword ptr [ebp + 28]
    popfd
    mov eax, [ebp + 0]
    mov ecx, [ebp + 4]
    mov edx, [ebp + 8]
    mov ebx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]
    mov ebp, [ebp + 24]
    jmp dword ptr [sum_block_target]
  }
}

__declspec(naked) void SumBlockReturn() {
  __asm {
    mov sum_block_eax, eax
    lahf
    seto al
    movzx eax, ax
    mov sum_block_flags, eax
    mov eax, sum_block_call
    mov [eax + 36], ecx
    mov [eax + 40], edx
    mov [eax + 44], ebx
    mov [eax + 48], esi
    mov [eax + 52], edi
    mov [eax + 56], ebp
    mov [eax + 68], esp
    mov ecx, sum_block_eax
    mov [eax + 32], ecx
    mov ecx, sum_block_flags
    mov [eax + 60], ecx
    mov esp, [eax + 64]
    popad
    ret
  }
}

// A block entered by itself (EnterSumBlock) with an access violation caught: false if it faulted.
bool EnterSumBlockGuarded(SumBlockCall* call) {
  __try {
    EnterSumBlock(call);
    return true;
  } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
    return false;
  }
}

int RunSumTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  const uint32_t function_rva = build.code->child_sum_rva;
  CHECK(ec::sites[ec::CHILD_SUM_INDEX].at == patched + function_rva + ec::CHILD_SUM_SITE && (ec::written >> ec::CHILD_SUM_INDEX & 1) != 0);
  CHECK(std::memcmp(ec::sites[ec::CHILD_SUM_INDEX].game.data(), game + function_rva + ec::CHILD_SUM_SITE, 5) == 0);
  const uint32_t game_function = Address(game + function_rva);
  const uint32_t patched_function = Address(patched + function_rva);
  auto world = MakeSumWorld();
  auto result_a = std::make_unique<SumResult>();
  auto result_b = std::make_unique<SumResult>();

  // 12a. The whole function on random trees: sizes 0 to 40, plain and adding getters, and getters that change the vector at one call
  // (truncating, growing within the 48 slots, removing the last child, or moving the data pointer inside its block with the end
  // where it was: the old esi then lies below the new begin or inside the new window); the slots the loop never reads hold invalid
  // pointers. Every register, the flags, esp, the size, the data pointer and the number of getter calls equal; the data's page is
  // followed by no access.
  {
    Random random = {.state = 0x5EED5};
    SumSpec spec;
    uint64_t wrong = 0;
    uint64_t sizes_seen[4] = {};
    uint64_t moved_below = 0;  // the data pointer moved past the old esi: the stub's esi lies below the new begin
    const int runs = (quick ? 400'000 : 2'000'000);
    for (int run = 0; run < runs; ++run) {
      SumRandom(&spec, &random);
      std::array<uint32_t, 7> in;
      for (uint32_t& value : in) value = random.Next();
      const uint32_t flags = random.Next();
      RunSum(world.get(), spec, game_function, in, flags, result_a.get());
      RunSum(world.get(), spec, patched_function, in, flags, result_b.get());
      const bool moving = (spec.trigger != 0 && spec.kind[0] == 3);
      sizes_seen[spec.trigger == 0 ? 0 : (moving ? 3 : (spec.new_size > spec.size ? 1 : 2))] += 1;
      moved_below += (moving && spec.shift > static_cast<int32_t>(spec.trigger) ? 1 : 0);
      if (!SumEqual(*result_a, *result_b, in)) {
        ++wrong;
        Report("  sum: run %d: NOT equal (size %u, trigger %u -> %u shift %d; eax %08X / %08X, ecx %08X / %08X, edx %08X / %08X, flags %04X / %04X, calls %u / %u)\n", run,
               spec.size, spec.trigger, spec.new_size, spec.shift, result_a->call.out[0], result_b->call.out[0], result_a->call.out[1], result_b->call.out[1],
               result_a->call.out[2], result_b->call.out[2], result_a->call.flags_out, result_b->call.flags_out, result_a->calls, result_b->calls);
      }
      if (spec.trigger == 0 || moving) {  // no getter changes which children are summed: the sum is the plain one
        uint32_t expected = 0;
        for (uint32_t index = 0; index < spec.size; ++index) expected += spec.first[index] + (spec.kind[index] == 1 ? spec.second[index] : 0);
        CHECK(result_a->call.out[0] == expected && result_a->calls == spec.size);
      }
    }
    CHECK(wrong == 0 && sizes_seen[3] > static_cast<uint64_t>(runs) / 8 && moved_below > static_cast<uint64_t>(runs) / 64);
    std::printf("12. effect tree child sum: %d random trees (%llu plain, %llu growing, %llu shrinking, %llu with the data pointer moved, %llu of them past the stub's esi): "
                "every register, the flags, esp, the size, the data pointer and the call count equal\n",
                runs, sizes_seen[0], sizes_seen[1], sizes_seen[2], sizes_seen[3], moved_below);
  }

  // 12b. The loop's bottom by itself: esi at every place around the vector (below its begin, inside, at the end - 4, at its end, past
  // it, or anywhere), the size 0 to 48, the begin moved; the slots hold random pointers. Every register but eax, the flags and esp equal;
  // eax tells which way the stub went: the far child (the last load) when the far slot [esi + 4K - 4, esi + 4K) is inside, else esi + 4K
  // when the near slot [esi + 2K - 4, esi + 2K) is, else esi + 2K (or the eax it came with, esi below the begin). The near block's load
  // shows only in a copy with the far block made nops ("near alone": eax the near child when the near slot is inside); CONTROLs: that
  // copy with the near check and the far load, and with the near load one slot too far, [esi + 2K], must differ in every run where the near
  // slot is inside; the whole stub with its jb, its near ja or its far ja a nop must be seen (a different eax, or a fault).
  {
    const uint32_t near_end = 2u * ec::CHILD_SUM_AHEAD;
    const uint32_t far_end = 4u * ec::CHILD_SUM_AHEAD;
    const uint32_t page_end = Address(world->slots) + SUM_SLOTS * 4;
    const auto slot_inside = [](uint32_t esi, uint32_t slot_end, uint32_t end) { return esi + slot_end >= esi && esi + slot_end <= end; };
    const auto read_at = [](uint32_t address) { return Read32(reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(address))); };
    const auto expected_eax = [&](uint32_t begin, uint32_t count, uint32_t esi, uint32_t eax) {
      if (esi < begin) return eax;
      if (!slot_inside(esi, near_end, begin + 4 * count)) return esi + near_end;
      if (!slot_inside(esi, far_end, begin + 4 * count)) return esi + far_end;
      return read_at(esi + far_end - 4);
    };
    const auto expected_near_eax = [&](uint32_t begin, uint32_t count, uint32_t esi, uint32_t eax) {
      if (esi < begin) return eax;
      if (!slot_inside(esi, near_end, begin + 4 * count)) return esi + near_end;
      return read_at(esi + near_end - 4);
    };
    // The stub's bytes the copies change (ChildSumStub's layout).
    constexpr size_t BELOW_BEGIN_JB = 5;   // jb skip: esi below the begin
    constexpr size_t NEAR_BLOCK = 7;       // mov eax, esi .. the near prefetch
    constexpr size_t NEAR_JC = 12;         // jc skip after add eax, 2K
    constexpr size_t NEAR_JA = 16;         // ja skip: the near slot is not wholly inside
    constexpr size_t NEAR_LOAD_DISP = 20;  // mov eax, [eax - 4]: the near child
    constexpr size_t FAR_BLOCK = 24;       // mov eax, esi .. the far prefetch, up to skip
    constexpr size_t FAR_JC = 29;          // jc skip after add eax, 4K
    constexpr size_t FAR_JA = 33;          // ja skip: the far slot is not wholly inside
    constexpr size_t SKIP = 41;
    const uint8_t* stub = ec::page + ec::CHILD_SUM_PAGE;
    CHECK(stub[BELOW_BEGIN_JB] == 0x72 && stub[NEAR_BLOCK] == 0x89 && stub[NEAR_JC] == 0x72 && stub[NEAR_JA] == 0x77 && stub[NEAR_LOAD_DISP - 2] == 0x8B
          && stub[NEAR_LOAD_DISP] == 0xFC && stub[FAR_BLOCK] == 0x89 && stub[FAR_JC] == 0x72 && stub[FAR_JA] == 0x77 && stub[SKIP] == 0x39
          && SKIP + 7 == ec::CHILD_SUM_STUB.size());
    uint8_t* blocks = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    uint8_t* game_block = blocks;
    const uint8_t bytes[5] = {0x8D, 0x0C, 0x91, 0x3B, 0xF1};
    std::memcpy(game_block, bytes, 5);
    JumpTo(game_block + 5, reinterpret_cast<const void*>(&SumBlockReturn));
    // A copy of the stub's bytes at `at` (0x40 apart), ending at the harness.
    const auto stub_copy = [&](size_t at, const uint8_t* from) {
      uint8_t* copy = blocks + at;
      std::memcpy(copy, from, ec::CHILD_SUM_STUB.size());
      JumpTo(copy + ec::CHILD_SUM_STUB.size() - 5, reinterpret_cast<const void*>(&SumBlockReturn));
      return copy;
    };
    uint8_t* patched_block = stub_copy(0x40, stub);
    uint8_t* near_block = stub_copy(0x80, stub);
    std::fill(near_block + FAR_BLOCK, near_block + SKIP, uint8_t{0x90});
    uint8_t* near_far_load_block = stub_copy(0xC0, near_block);
    near_far_load_block[NEAR_LOAD_DISP] = static_cast<uint8_t>(near_end - 4);  // [eax + 2K - 4] = [esi + 4K - 4]
    uint8_t* near_past_block = stub_copy(0x100, near_block);
    near_past_block[NEAR_LOAD_DISP] = 0;  // [eax + 0] = [esi + 2K]: the slot after the near slot
    // The near check's jc a nop; the far one's in a copy with the near block made nops (with it, the far check runs only after the near
    // one passed, which no vector of the process allows when esi + 4K wraps).
    std::array<uint8_t*, 2> no_jc_blocks = {stub_copy(0x140, stub), stub_copy(0x180, stub)};
    std::fill(no_jc_blocks[1] + NEAR_BLOCK, no_jc_blocks[1] + FAR_BLOCK, uint8_t{0x90});
    for (const auto [block, at] : {std::pair{no_jc_blocks[0], NEAR_JC}, std::pair{no_jc_blocks[1], FAR_JC}}) {
      block[at] = 0x66;
      block[at + 1] = 0x90;
    }
    // The whole stub with one conditional jump a nop: esi below the begin (jb), the near slot not wholly inside (ja), the far one (ja).
    constexpr std::array<size_t, 3> MUTANT_AT = {BELOW_BEGIN_JB, NEAR_JA, FAR_JA};
    std::array<uint8_t*, 3> mutant_blocks = {};
    for (size_t mutant = 0; mutant < mutant_blocks.size(); ++mutant) {
      mutant_blocks[mutant] = stub_copy(0x1C0 + 0x40 * mutant, stub);
      mutant_blocks[mutant][MUTANT_AT[mutant]] = 0x66;
      mutant_blocks[mutant][MUTANT_AT[mutant] + 1] = 0x90;
    }
    Random random = {.state = 0xB10C};
    SumBlockCall a, b;
    uint64_t loads_possible = 0;
    uint64_t near_inside = 0;
    std::array<uint64_t, 2> control_runs = {};   // near check + far load, near load at [esi + 2K]
    std::array<uint64_t, 2> control_differ = {};
    std::array<uint64_t, 3> mutant_runs = {};    // jb, near ja, far ja a nop: the runs with esi around the data
    std::array<uint64_t, 3> mutant_seen = {};
    uint64_t wrong = 0;
    const int runs = (quick ? 1'000'000 : 5'000'000);
    for (int run = 0; run < runs; ++run) {
      for (size_t index = 0; index < SUM_SLOTS; ++index) world->slots[index] = random.Next();
      const uint32_t size = random.Next() % (SUM_SLOTS + 1);
      const uint32_t moved = (size > 0 ? random.Next() % std::min<uint32_t>(size + 1, 9) : 0);
      const uint32_t begin = Address(world->slots + moved);
      const uint32_t count = size - moved;
      const int place = static_cast<int>(random.Next() % (SUM_SLOTS + 10)) - 4;  // slots from the data's start
      uint32_t esi = Address(world->slots) + 4 * place + (random.Next() & 3);  // any byte: the load must not depend on the alignment
      if ((random.Next() & 15) == 0) esi = random.Next();
      if ((random.Next() & 15) == 0) esi = 0u - far_end + random.Next() % far_end;  // esi + 4K wraps
      std::memset(&a, 0, sizeof(a));
      for (uint32_t& value : a.in) value = random.Next();
      a.in[1] = begin;
      a.in[2] = count;
      a.in[4] = esi;
      a.flags_in = (random.Next() & 0x8D5) | 2;
      b = a;
      sum_block_target = Address(game_block);
      EnterSumBlock(&a);
      sum_block_target = Address(patched_block);
      EnterSumBlock(&b);
      const uint32_t end = begin + 4 * count;
      const bool near_passes = (esi >= begin && slot_inside(esi, near_end, end));
      loads_possible += (esi >= begin && slot_inside(esi, far_end, end)) ? 1 : 0;
      near_inside += (near_passes ? 1 : 0);
      SumBlockCall c = a;
      sum_block_target = Address(near_block);
      EnterSumBlock(&c);
      const bool same = std::memcmp(a.out + 1, b.out + 1, 6 * sizeof(uint32_t)) == 0 && a.flags_out == b.flags_out && a.esp_after == b.esp_after
                        && a.out[1] == end && b.out[0] == expected_eax(begin, count, esi, b.in[0])
                        && std::memcmp(a.out + 1, c.out + 1, 6 * sizeof(uint32_t)) == 0 && a.flags_out == c.flags_out && a.esp_after == c.esp_after
                        && c.out[0] == expected_near_eax(begin, count, esi, c.in[0]);
      if (!same) {
        ++wrong;
        Report("  sum block: run %d: NOT equal (begin %08X count %u esi %08X: ecx %08X / %08X, flags %04X / %04X)\n", run, begin, count, esi, a.out[1], b.out[1],
               a.flags_out, b.flags_out);
      }
      // The CONTROLs where their loads can't fault: the far slot [esi + 4K - 4, esi + 4K) and the slot after the near one, [esi + 2K, esi + 2K + 4),
      // both below the no-access page.
      const std::array<uint32_t, 2> control_limit = {page_end - far_end, page_end - near_end - 4};
      for (size_t control = 0; control < control_runs.size(); ++control) {
        if (!near_passes || esi > control_limit[control]) continue;
        c = a;
        sum_block_target = Address(control == 0 ? near_far_load_block : near_past_block);
        EnterSumBlock(&c);
        ++control_runs[control];
        control_differ[control] += (c.out[0] != expected_near_eax(begin, count, esi, c.in[0])) ? 1 : 0;
      }
      // The mutants with esi around the data (a wild esi makes them fault too, which proves nothing about the check): a different eax, or a fault.
      if (static_cast<uint64_t>(esi) + 64 >= Address(world->slots) && esi <= page_end + 64ull) {
        for (size_t mutant = 0; mutant < mutant_blocks.size(); ++mutant) {
          SumBlockCall m = a;
          sum_block_target = Address(mutant_blocks[mutant]);
          ++mutant_runs[mutant];
          mutant_seen[mutant] += (!EnterSumBlockGuarded(&m) || m.out[0] != expected_eax(begin, count, esi, m.in[0])) ? 1 : 0;
        }
      }
    }
    CHECK(wrong == 0 && loads_possible > static_cast<uint64_t>(runs) / 8);
    std::printf("   the loop's bottom alone (K = %u): %d runs (%llu with the far slot to prefetch from, %llu the near one): every register but eax, the flags and "
                "esp equal, eax the way the stub went (the far child loaded; the near child in the copy without the far block); no load past the data\n",
                static_cast<uint32_t>(ec::CHILD_SUM_AHEAD), runs, loads_possible, near_inside);
    std::printf("   CONTROL, the near check with the far load: %llu of %llu differ; the near load one slot too far, [esi + 2K]: %llu of %llu differ (all must)\n",
                control_differ[0], control_runs[0], control_differ[1], control_runs[1]);
    CHECK(control_runs[0] > static_cast<uint64_t>(runs) / 8 && control_differ[0] == control_runs[0]);
    CHECK(control_runs[1] > static_cast<uint64_t>(runs) / 8 && control_differ[1] == control_runs[1]);
    std::printf("   CONTROL, the whole stub with its jb, its near ja or its far ja a nop: seen in %llu of %llu, %llu of %llu, %llu of %llu runs around the data "
                "(a different eax, or a fault; each must be seen)\n",
                mutant_seen[0], mutant_runs[0], mutant_seen[1], mutant_runs[1], mutant_seen[2], mutant_runs[2]);
    CHECK(mutant_seen[0] > 0 && mutant_seen[1] > 0 && mutant_seen[2] > 0);

    // 12b2. esi at EVERY byte around a vector whose end is the no-access page, every begin (byte-granular) and size, and esi -4K .. -1 (esi + 4K
    // wraps) against a few vectors: a load past the vector (or from near address 0) crashes the test. CONTROL: the stub without either jc
    // in the wrap loop, faults caught (must fault).
    {
      uint64_t sweep_runs = 0;
      uint64_t sweep_wrong = 0;
      const auto run_one = [&](uint32_t begin, uint32_t count, uint32_t esi) {
        std::memset(&a, 0, sizeof(a));
        for (uint32_t index = 0; index < 7; ++index) a.in[index] = 0x1234567 * (index + 1);
        a.in[1] = begin;
        a.in[2] = count;
        a.in[4] = esi;
        a.flags_in = 0x42;
        b = a;
        sum_block_target = Address(game_block);
        EnterSumBlock(&a);
        sum_block_target = Address(patched_block);
        EnterSumBlock(&b);
        ++sweep_runs;
        sweep_wrong += (std::memcmp(a.out + 1, b.out + 1, 6 * sizeof(uint32_t)) != 0 || a.flags_out != b.flags_out || a.esp_after != b.esp_after
                        || b.out[0] != expected_eax(begin, count, esi, b.in[0]))
                           ? 1
                           : 0;
      };
      for (uint32_t begin = page_end - SUM_SLOTS * 4; begin <= page_end; ++begin) {
        for (uint32_t count = 0; begin + 4 * count <= page_end; ++count) {
          for (uint32_t esi = begin - far_end; esi < page_end + far_end; ++esi) run_one(begin, count, esi);
        }
      }
      // The vector at the top holds K / 2 - 1 slots: no near slot fits in it without the wrap (its memory is not the process's).
      std::array<uint64_t, 2> no_jc_faults = {};
      for (uint32_t esi = 0u - far_end; esi != 0; ++esi) {
        for (const auto [begin, count] : {std::pair{0xFFFFFFF0u, ec::CHILD_SUM_AHEAD / 2u - 1}, std::pair{esi & ~3u, 0u},
                                          std::pair{Address(world->slots), static_cast<uint32_t>(SUM_SLOTS)}, std::pair{0x10000u, 4u}}) {
          run_one(begin, count, esi);
          for (size_t control = 0; control < no_jc_blocks.size(); ++control) {
            b = a;
            sum_block_target = Address(no_jc_blocks[control]);
            no_jc_faults[control] += EnterSumBlockGuarded(&b) ? 0 : 1;
          }
        }
      }
      CHECK(sweep_wrong == 0);
      std::printf("   esi at every byte around a vector ending at a no-access page, and esi + 4K wrapping: %llu runs, no load past the vector, every register but eax, the flags and esp equal, "
                  "eax the way the stub went\n",
                  sweep_runs);
      std::printf("   CONTROL, the wrap runs without the near check's jc: %llu faults, the far block alone without its jc: %llu (must fault)\n", no_jc_faults[0],
                  no_jc_faults[1]);
      CHECK(no_jc_faults[0] > 0 && no_jc_faults[1] > 0);
    }
    VirtualFree(blocks, 0, MEM_RELEASE);
  }

  // 12c. Timing: children of one line in 256 MB, the lists' slots a fresh random set for every run (no variant finds what another left in the
  // cache), the getter busy for `busy` cycles after its load, lists of 2 to 128. The game's copy and ours in the two images; in copies of the
  // function one prefetch for K = 2 / 4 / 8 / 16 (the slot [esi + 4K - 4, esi + 4K); K = 2 was the stub of E3P-a), two prefetches (K and K / 2)
  // behind the one bound check of K's slot ("K+K/2", the stub of E3P-d: lists of K or fewer get none) or each slot behind its own ("K|K/2",
  // ChildSumStub), and the copy without a stub; ns a child, the median of 7 rounds. The hot line: the same 256 children in every list.
  {
    constexpr size_t CHILDREN = 1u << 21;
    constexpr size_t SLOTS = 1u << 18;
    constexpr int ROUNDS = 7;
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, CHILDREN * 128, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECK(memory != nullptr);
    uint32_t vtable[8] = {};
    vtable[7] = Address(reinterpret_cast<const void*>(&SumGetBusy));
    for (size_t index = 0; index < CHILDREN; ++index) {
      Write32(memory + index * 128, Address(vtable));
      Write32(memory + index * 128 + 8, static_cast<uint32_t>(index & 0xFF));
    }
    // Copies of the function at 0x100 apart, each stub 0x80 behind its copy: the site jumps to the stub, the stub's jump to the copy's jne.
    const size_t function_size = (ec::CHILD_SUM.size() + 1) / 3;
    auto* scratch = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    CHECK(scratch != nullptr);
    std::vector<std::pair<std::string, uint32_t>> variants = {{"game", Address(game + function_rva)},
                                                              {std::format("ours (K={})", ec::CHILD_SUM_AHEAD), Address(patched + function_rva)}};
    // False when the scratch page has no room for another copy (nothing written).
    const auto add_copy = [&](std::string name, std::span<const uint8_t> stub) {
      if (variants.size() - 2 >= 0x1000 / 0x100) return false;
      uint8_t* copy = scratch + 0x100 * (variants.size() - 2);
      std::memcpy(copy, game + function_rva, function_size);
      if (!stub.empty()) {
        uint8_t* stub_code = copy + 0x80;
        std::memcpy(stub_code, stub.data(), stub.size());
        Write32(stub_code + stub.size() - 4, Address(copy + ec::CHILD_SUM_RESUME) - Address(stub_code + stub.size()));
        JumpTo(copy + ec::CHILD_SUM_SITE, stub_code);
      }
      variants.emplace_back(std::move(name), Address(copy));
      return true;
    };
    CHECK(add_copy("no stub", {}));
    for (const uint8_t ahead : {2, 4, 8, 16}) {
      // One prefetch: the far slot alone (the bound check as in ChildSumStub).
      const std::array<uint8_t, 31> one = {0x39, 0xCE, 0x8D, 0x0C, 0x91, 0x72, 0x11, 0x89, 0xF0, 0x83, 0xC0, static_cast<uint8_t>(ahead * 4), 0x72, 0x0A,
                                           0x39, 0xC8, 0x77, 0x06, 0x8B, 0x40, 0xFC, 0x0F, 0x18, 0x08, 0x39, 0xCE, 0xE9, 0, 0, 0, 0};
      CHECK(add_copy(std::format("K={}", ahead), one));
    }
    // The two-prefetch forms for K = AHEAD (ChildSumStub<AHEAD> checks its preconditions at compile time).
    const auto add_two_prefetch_forms = [&]<uint8_t AHEAD>() {
      // The stub before (E3P-d): both prefetches behind the far slot's one check, the near load `mov eax, [esi + 2K - 4]`.
      const std::array<uint8_t, 37> shared = {0x39, 0xCE, 0x8D, 0x0C, 0x91, 0x72, 0x17, 0x89, 0xF0, 0x83, 0xC0, static_cast<uint8_t>(AHEAD * 4), 0x72, 0x10,
                                              0x39, 0xC8, 0x77, 0x0C, 0x8B, 0x40, 0xFC, 0x0F, 0x18, 0x08, 0x8B, 0x46, static_cast<uint8_t>(AHEAD * 2 - 4),
                                              0x0F, 0x18, 0x08, 0x39, 0xCE, 0xE9, 0, 0, 0, 0};
      return add_copy(std::format("K={}+{}", AHEAD, AHEAD / 2), shared) && add_copy(std::format("K={}|{}", AHEAD, AHEAD / 2), ec::ChildSumStub<AHEAD>());
    };
    CHECK(add_two_prefetch_forms.operator()<4>() && add_two_prefetch_forms.operator()<8>() && add_two_prefetch_forms.operator()<16>());
    FlushInstructionCache(GetCurrentProcess(), scratch, 0x1000);
    std::vector<uint32_t> permutation(CHILDREN);
    for (size_t index = 0; index < CHILDREN; ++index) permutation[index] = static_cast<uint32_t>(index);
    std::vector<uint32_t> pointers(SLOTS);
    std::vector<std::array<uint8_t, 0x80>> parents(SLOTS / 2);  // the shortest lists are 2 long
    Random random = {.state = 0x71337};
    using SumFn = uint32_t(__fastcall*)(void* self, void* unused_edx);
    const auto run_lists = [&](size_t variant, bool hot, uint32_t list) {
      for (size_t index = 0; index < SLOTS; ++index) {
        if (hot) {
          pointers[index] = Address(memory + (index % 256) * 128);
        } else {  // a partial shuffle: other children for every slot
          const size_t other = index + random.Next() % (CHILDREN - index);
          std::swap(permutation[index], permutation[other]);
          pointers[index] = Address(memory + static_cast<size_t>(permutation[index]) * 128);
        }
      }
      const size_t lists = SLOTS / list;
      for (size_t index = 0; index < lists; ++index) {
        Write32(parents[index].data() + 0x64, list);
        Write32(parents[index].data() + 0x68, Address(pointers.data() + index * list));
      }
      const auto sum = reinterpret_cast<SumFn>(variants[variant].second);
      uint32_t total = 0;
      const auto start = std::chrono::steady_clock::now();
      for (size_t index = 0; index < lists; ++index) total += sum(parents[index].data(), nullptr);
      const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / static_cast<double>(lists * list);
      return ns + (total == 0xFFFFFFFFu ? 1 : 0);
    };
    // One line: the median of 7 rounds for every variant.
    const auto time_line = [&](std::string line, bool hot, uint32_t list) {
      std::vector<std::vector<double>> ns(variants.size());
      for (int round = 0; round < ROUNDS; ++round) {
        for (size_t variant = 0; variant < variants.size(); ++variant) ns[variant].push_back(run_lists(variant, hot, list));
      }
      for (size_t variant = 0; variant < variants.size(); ++variant) {
        std::sort(ns[variant].begin(), ns[variant].end());
        line += std::format("{} {} {:.1f}", variant == 0 ? ":" : ",", variants[variant].first, ns[variant][ROUNDS / 2]);
      }
      return line + "\n";
    };
    std::string lines;
    std::vector<std::pair<uint32_t, uint32_t>> rows = {{0u, 32u}, {40u, 32u}, {150u, 32u}, {0u, 8u}, {40u, 8u}, {150u, 8u}, {40u, 128u}, {150u, 128u}};
    for (const uint32_t list : {2u, 3u, 4u, 5u, 6u}) {  // short lists: the near slot alone fits in them
      for (const uint32_t busy : {0u, 40u, 150u}) rows.emplace_back(busy, list);
    }
    for (const auto& [busy, list] : rows) {
      sum_busy = busy;
      lines += time_line(std::format("   ns a child, getter busy {} cycles, lists of {}", busy, list), false, list);
    }
    sum_busy = 0;
    lines += time_line("   ns a child, hot (the same 256 children), getter busy 0, lists of 32", true, 32);
    std::fputs(lines.c_str(), stdout);
    VirtualFree(memory, 0, MEM_RELEASE);
    VirtualFree(scratch, 0, MEM_RELEASE);
  }

  // 12d. The prefetch site switched off and on 200 times while 4 threads sum their own lists with both copies of the function.
  {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> calls = 0;
    using SumFn = uint32_t(__fastcall*)(void* self, void* unused_edx);
    const auto game_sum = reinterpret_cast<SumFn>(game + function_rva);
    const auto patched_sum = reinterpret_cast<SumFn>(patched + function_rva);
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        uint32_t vtable[8] = {};
        vtable[7] = Address(reinterpret_cast<const void*>(&SumGetTimed));
        std::array<std::array<uint8_t, 64>, 40> children = {};
        std::array<uint32_t, 40> slots = {};
        alignas(64) std::array<uint8_t, 0x80> parent = {};
        Random thread_random = {.state = 0xAB00ull + static_cast<uint64_t>(worker)};
        while (!stop.load()) {
          const uint32_t size = thread_random.Next() % 41;
          uint32_t expected = 0;
          for (uint32_t index = 0; index < 40; ++index) {
            const uint32_t value = thread_random.Next() % 100000;
            Write32(children[index].data(), Address(vtable));
            Write32(children[index].data() + 8, value);
            slots[index] = Address(children[index].data());
            expected += (index < size ? value : 0);
          }
          Write32(parent.data() + 0x64, size);
          Write32(parent.data() + 0x68, Address(slots.data()));
          if (game_sum(parent.data(), nullptr) != expected) ++bad;
          if (patched_sum(parent.data(), nullptr) != expected) ++bad;
          ++calls;
        }
      });
    }
    int busy = 0;
    bool on = true;
    for (int switches = 0; switches < 200; ++switches) {
      on = !on;
      int tries = 0;
      do {
        twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, on, true);
        ++tries;
      } while (((ec::written >> ec::CHILD_SUM_INDEX & 1) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
    }
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("   200 switches while 4 threads made %llu sums with each copy: %llu wrong (%d writes waited for a thread inside the site)\n", calls.load(), bad.load(), busy);
    CHECK(bad.load() == 0 && (ec::written >> ec::CHILD_SUM_INDEX & 1) != 0);
  }

  // 12e. A parked thread whose saved Eip is each byte of the site (and the jne after it): the row switched, the site changes only
  // with Eip at the site's first byte or outside it (the random switches above hardly ever meet a thread inside the 5 bytes).
  {
    ParkedThread parked;
    HANDLE thread = parked.Open();
    CHECK(thread != nullptr);
    const uint8_t* site = patched + function_rva + ec::CHILD_SUM_SITE;
    bool on = (ec::written >> ec::CHILD_SUM_INDEX & 1) != 0;
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
      twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, !on, true);
      const bool changed = ((ec::written ^ before) >> ec::CHILD_SUM_INDEX & 1) != 0;
      wrong += (changed != !(offset > 0 && offset < 5)) ? 1 : 0;
      if (changed) on = !on;
      CHECK(SetThreadContext(thread, &saved) != FALSE);
      ResumeThread(thread);
    }
    for (int tries = 0; !on && tries < static_cast<int>(ec::MAX_FAILED_WRITES); ++tries) {
      twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
      on = (ec::written >> ec::CHILD_SUM_INDEX & 1) != 0;
    }
    parked.Stop();
    CloseHandle(thread);
    CHECK(wrong == 0 && on);
    std::printf("   a parked thread at site -1 .. +6: the site is refused at +1 .. +4 and written at the others\n");
  }
  return 0;
}

// The state, fault, whole-function and thread parts of the half rows (review 3), run by `tests.exe <dll> quick|full adv <part>`.
#include "half_rows_adversarial.inc"

// The particle writer's float4 reloads (part 14): the region alone, the whole function, switching, parked threads, timing.
#include "particle_reload.inc"

// The particle update loop's prefetch (part 15): the loop's bottom alone, the whole loop, controls, switching, parked threads, timing.
#include "particle_prefetch.inc"

// The emitter update loop's prefetch (part 16): the loop's bottom alone, the whole loop, controls, switching, parked threads, timing.
#include "emitter_prefetch.inc"

// More float4 reloads (part 17): each alone, the soldier draw's ways into them, controls, switching, parked threads, timing.
#include "more_reloads.inc"

// The particle writer's table entry prefetch (part 18): the whole function, odd entries, controls, switching, parked threads, timing.
#include "entry_prefetch.inc"

// The render lists' item push loops (part 19): the guard against a model, the whole loops, controls, switching, parked threads, timing.
#include "item_push.inc"

// The pending list sweep's chain prefetch (part 20): whole sweeps, controls, switching, parked threads, timing.
#include "sweep_prefetch.inc"

// Model Detail Distance (part 22): the LOD pick's site, its callers, the stub's exactness, switching, parked threads, controls.
#include "lod_pick.inc"

// Animation Rows on Workers (part 23): the animation update's call of the animation texture fill, its cut stub, the cut rule, the pin, switching.
#include "animation_rows.inc"

// The pending list sweep's trace (part 24): the entry site's stub, the trace-only Arm.
#include "pending_sweep_split.inc"

// The effect update's sites (part 25): the table, the thunks as built, the fast path's word, the stack, parked threads, switching.
#include "effect_sites.inc"

// The effect update's scheduler (part 26): rounds against the game's loop on a fake game, the late units and segments, the failure order, the census, the controls.
#include "effect_update.inc"

// Cached Particle Count (part 28): its two sites in the game's code, the stubs on a fake game, the cache against the fresh sum, the controls.
#include "particle_count.inc"

// Parallel Instance Fill's hook-up (part 29): its 4 sites in PausedWrite's fourth table, Update's bookkeeping, another mod's bytes, a page that cannot be made writable, a parked thread, the dispatcher's count, Uninstall.
#include "instance_fill.inc"

// Exit Job Drain (part 30): its site in the game's code, the four no-patch cases, in and out, the drain on a fake job pool, the cap.
#include "exit_drain.inc"

#if defined(TWATTILA_ENGINE_PROBE)
// The probe build (part 21, `probe` alone): the probe stubs in front of the production stubs, each alone, parts 19 and 20 through them.
#include "engine_probe.inc"
#endif

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const char* path = (argc > 1 ? argv[1] : R"(C:\Program Files (x86)\Steam\steamapps\common\Total War Attila\empire.retail.dll)");
  const bool quick = (argc > 2 && std::string(argv[2]) == "quick");
  std::ifstream stream(path, std::ios::binary);
  CHECK(stream.good());
  const std::vector<uint8_t> file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data())->e_lfanew);
  CHECK(nt->OptionalHeader.ImageBase == PREFERRED_BASE);
  const auto* text_header = IMAGE_FIRST_SECTION(nt);
  const uint32_t text_rva = text_header->VirtualAddress;
  const uint32_t text_size = text_header->Misc.VirtualSize;
  namespace ec = twattila::engine_code::internal;
  uint32_t image_size = 0;
  uint8_t* game = Map(file, &image_size);
  uint8_t* patched = Map(file, &image_size);
  CHECK(game != nullptr && patched != nullptr);
  const TestBuild* found = &ATTILA;
  for (const TestBuild* candidate : {&THRONES, &ROME2}) {
    if (Matches(game + text_rva, text_size, candidate->code->truncation) == 1) found = candidate;
  }
  const TestBuild& build = *found;
  std::printf("empire.retail.dll build %08lX, %zu bytes: %s's\n", nt->FileHeader.TimeDateStamp, file.size(), build.game);
  for (uint8_t* image : {game, patched}) {
    Write32(image + build.matherr_rva, Read32(image + build.cookie_rva));
    Write32(image + build.crt_flag_rva, 0);
  }

  // 1. Patterns, sites.
  for (const std::string_view pattern : {build.code->truncation, ec::HALF, ec::FMOD, ec::FMOD_DISPATCH, ec::FMOD_CLASSIFY, ec::FMOD_FINISH}) {
    CHECK(Matches(game + text_rva, text_size, pattern) == 1);
  }
  CHECK(Matches(game + text_rva, text_size, build.code->fmod_matherr) >= 1);
  CHECK(Matches(game + text_rva, text_size, ec::LIST_PUSH) == (build.list_push_function_rva != 0 ? 1u : 0u));
  CHECK(Matches(game + text_rva, text_size, ec::READY_CHECK) == (build.ready_check_rva != 0 ? 1u : 0u));
  CHECK(build.code->ready_check_rva == build.ready_check_rva);
  CHECK(Matches(game + text_rva, text_size, ec::CHILD_SUM) == (build.code->child_sum_rva != 0 ? 1u : 0u));
  CHECK(Matches(game + text_rva, text_size, ec::FLOAT4_RELOAD) == (build.code->float4_reload_rva != 0 ? 1u : 0u));
  CHECK(Matches(game + text_rva, text_size, ec::PARTICLE_LOOP) == (build.code->particle_loop_rva != 0 ? 1u : 0u));
  CHECK(Matches(game + text_rva, text_size, ec::EMITTER_LOOP) == (build.code->emitter_loop_rva != 0 ? 1u : 0u));
  CHECK(Matches(game + text_rva, text_size, ec::ENTRY_FETCH) == (build.code->entry_fetch_rva != 0 ? 1u : 0u));
  CHECK(Matches(game + text_rva, text_size, ec::ITEM_PUSH_A) == (build.code->push_loop_a_rva != 0 ? 1u : 0u));
  CHECK(Matches(game + text_rva, text_size, ec::ITEM_PUSH_B) == (build.code->push_loop_b_rva != 0 ? 1u : 0u));
  CHECK(Matches(game + text_rva, text_size, ec::SWEEP) == (build.code->sweep_rva != 0 ? 1u : 0u));
  CHECK(Matches(game + text_rva, text_size, ec::PICK_LOD) == 1);
  CHECK(Matches(game + text_rva, text_size, ec::PARTICLE_EPOCH) == (build.code->particle_epoch_rva != 0 ? 1u : 0u));
  CHECK(Matches(game + text_rva, text_size, ec::PARTICLE_COUNT) == (build.code->particle_count_rva != 0 ? 1u : 0u));
  CHECK(Matches(game + text_rva, text_size, build.code->soldier_rows) == 1);
  CHECK(Matches(game + text_rva, text_size, ec::SOLDIER_ROWS) + Matches(game + text_rva, text_size, ec::ROME2_SOLDIER_ROWS) == 1);
  CHECK(Matches(game + text_rva, text_size, ec::MATRIX_PRODUCT) == 1);
  // B's own code as mapped (A's differs in every relocated address).
  const std::vector<uint8_t> unpatched(patched + text_rva, patched + text_rva + text_size);
  ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
  CHECK(ec::sites[0].at == patched + build.truncation_block_rva);
  CHECK(ec::sites[1].at == patched + build.half_rva);
  CHECK(ec::sites[2].at == patched + build.fmod_rva);
  CHECK(ec::sites[3].at == (build.list_push_function_rva != 0 ? patched + build.code->list_push_rva + ec::LIST_PUSH_SITE : nullptr));
  CHECK(ec::sites[4].at == (build.ready_check_rva != 0 ? patched + build.ready_check_rva : nullptr));
  for (size_t reload = 0; reload < ec::FLOAT4_RELOAD_AT.size(); ++reload) {
    CHECK(ec::sites[ec::FLOAT4_RELOAD_FIRST_SITE + reload].at == (build.code->float4_reload_rva != 0 ? patched + build.code->float4_reload_rva + ec::FLOAT4_RELOAD_AT[reload] : nullptr));
  }
  CHECK(ec::sites[ec::PARTICLE_PREFETCH_INDEX].at == (build.code->particle_loop_rva != 0 ? patched + build.code->particle_loop_rva + ec::PARTICLE_LOOP_SITE : nullptr));
  CHECK(ec::sites[ec::EMITTER_PREFETCH_INDEX].at == (build.code->emitter_loop_rva != 0 ? patched + build.code->emitter_loop_rva + ec::EMITTER_LOOP_SITE : nullptr));
  CHECK(ec::sites[ec::ENTRY_PREFETCH_INDEX].at == (build.code->entry_fetch_rva != 0 ? patched + build.code->entry_fetch_rva + ec::ENTRY_FETCH_SITE : nullptr));
  CHECK(ec::sites[ec::ITEM_PUSH_FIRST_SITE].at == (build.code->push_loop_a_rva != 0 ? patched + build.code->push_loop_a_rva + ec::ITEM_PUSH_SITE : nullptr));
  CHECK(ec::sites[ec::ITEM_PUSH_FIRST_SITE + 1].at == (build.code->push_loop_b_rva != 0 ? patched + build.code->push_loop_b_rva + ec::ITEM_PUSH_SITE : nullptr));
  CHECK(ec::sites[ec::SWEEP_PREFETCH_INDEX].at == (build.code->sweep_rva != 0 ? patched + build.code->sweep_rva + ec::SWEEP_SITE : nullptr));
  CHECK(ec::sites[ec::MODEL_DETAIL_INDEX].at == patched + build.code->pick_lod_rva);
  CHECK(ec::sites[ec::PARTICLE_EPOCH_INDEX].at == (build.code->particle_epoch_rva != 0 ? patched + build.code->particle_epoch_rva : nullptr));
  CHECK(ec::sites[ec::PARTICLE_COUNT_INDEX].at == (build.code->particle_count_rva != 0 ? patched + build.code->particle_count_rva : nullptr));
  CHECK(ec::sites[ec::WORKER_FILL_INDEX].at == (build.code->fill_call_rva != 0 ? patched + build.code->fill_call_rva + ec::FILL_CALL_AT : nullptr)
        && ec::sites[ec::WORKER_FILL_INDEX].opcode == 0xE8);
  CHECK(ec::sites[ec::WORKER_SWEEP_INDEX].at == (build.code->sweep_rva != 0 ? patched + build.code->sweep_rva + ec::SWEEP_ENTRY_AT : nullptr));
  for (size_t row = 0; row < ec::SOLDIER_ROWS_STUBS.size(); ++row) {
    CHECK(ec::sites[ec::SOLDIER_ROWS_FIRST_SITE + row].at == patched + build.code->soldier_rows_rva + build.code->soldier_rows_at[row]);
    CHECK(ec::sites[ec::MATRIX_PRODUCT_FIRST_SITE + row].at == patched + build.code->matrix_product_rva + ec::MATRIX_PRODUCT_AT[row]);
  }
  CHECK(std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
  twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, true, false, true, true, true, true);
  CHECK(ec::written == (ec::found & ~ec::JOB_SITES & ~ec::MODEL_DETAIL_BIT & ~ec::WORKER_SITES & ~ec::PARTICLE_COUNT_SITES & ~ec::EXIT_DRAIN_BIT)  // the LOD pick's row is asked for by the slider (part 22), the worker rows by part 23, Cached Particle Count by part 28
        && ec::found == (ec::MODEL_DETAIL_BIT | 0b00111u | (build.list_push_function_rva != 0 ? 0b01000u : 0u) | (build.ready_check_rva != 0 ? 0b10000u : 0u)
                         | (build.code->job_wake_all_rva != 0 ? ec::JOB_SITES : 0u) | (build.code->half_rows_rva != 0 ? 0b100000000u : 0u)
                         | (build.code->child_sum_rva != 0 ? ec::CHILD_SUM_BIT : 0u) | (build.code->particle_loop_rva != 0 ? ec::PARTICLE_PREFETCH_BIT : 0u)
                         | (build.code->emitter_loop_rva != 0 ? ec::EMITTER_PREFETCH_BIT : 0u)
                         | (build.code->entry_fetch_rva != 0 ? ec::ENTRY_PREFETCH_BIT : 0u)
                         | (build.code->push_loop_a_rva != 0 ? ec::ITEM_PUSH_SITES : 0u)
                         | (build.code->sweep_rva != 0 ? ec::SWEEP_PREFETCH_BIT | ec::WORKER_SWEEP_BIT : 0u)
                         | (build.code->fill_call_rva != 0 ? ec::WORKER_FILL_BIT : 0u)
                         | (build.code->float4_reload_rva != 0 ? ec::FLOAT4_RELOAD_SITES : 0u) | ec::SOLDIER_ROWS_SITES | ec::MATRIX_PRODUCT_SITES
                         | (build.code->particle_count_rva != 0 ? ec::PARTICLE_COUNT_SITES : 0u)
                         | (build.code->exit_destroy_rva != 0 ? ec::EXIT_DRAIN_BIT : 0u)));  // found, not asked for here: Exit Job Drain is asked for by part 30
  size_t differing = 0;
  for (uint32_t at = text_rva; at < text_rva + text_size; ++at) {
    if (unpatched[at - text_rva] == patched[at]) continue;
    ++differing;
    bool in_site = false;
    for (const ec::Site& site : ec::sites) in_site |= (patched + at >= site.at && patched + at < site.at + ec::SITE_SIZE);
    CHECK(in_site);
  }
  std::printf("1. patterns once each; %zu bytes differ, all in the %d sites\n", differing, std::popcount(ec::written));

  // The truncation's end jumps back to the harness in both copies (after Prepare: its pattern reads these bytes).
  JumpTo(game + build.truncation_resume_rva, reinterpret_cast<const void*>(&BlockReturn));
  JumpTo(patched + build.truncation_resume_rva, reinterpret_cast<const void*>(&BlockReturn));

  // One part alone, right after part 1 (tests.exe <dll> quick rows | sum).
  if (argc > 3 && std::string(argv[3]) == "rows" && build.code->half_rows_rva != 0) {
    if (const int failed = RunRowsTests(build, game, patched, text_rva, text_size, quick); failed != 0) return failed;
    std::printf("PASS (rows only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "adv" && build.code->half_rows_rva != 0) {
    twattila::engine_code::Update(true);
    const int failed = RunAdv(build, game, patched, text_rva, text_size, quick, argc > 4 ? argv[4] : "all");
    std::printf(failed != 0 ? "FAILED (adv)\n" : "DONE (adv)\n");
    return failed;
  }
  if (argc > 3 && std::string(argv[3]) == "jobs" && build.code->job_wake_all_rva != 0) {
    twattila::engine_code::Update(true);
    if (const int failed = RunJobTests(build, game, patched, quick); failed != 0) return failed;
    std::printf("PASS (jobs only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "sum" && build.code->child_sum_rva != 0) {
    if (const int failed = RunSumTests(build, game, patched, quick); failed != 0) return failed;
    std::printf("PASS (sum only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "reload" && build.code->float4_reload_rva != 0) {
    if (const int failed = RunReloadTests(build, game, patched, quick); failed != 0) return failed;
    std::printf("PASS (reload only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "prefetch" && build.code->particle_loop_rva != 0) {
    if (const int failed = RunParticleTests(build, game, patched, quick); failed != 0) return failed;
    std::printf("PASS (prefetch only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "more") {
    if (const int failed = RunSoldierRowsTests(build, game, patched, quick); failed != 0) return failed;
    if (const int failed = RunMatrixProductTests(build, game, patched, quick); failed != 0) return failed;
    std::printf("PASS (more reloads only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "emitter" && build.code->emitter_loop_rva != 0) {
    if (const int failed = RunEmitterTests(build, game, patched, quick); failed != 0) return failed;
    std::printf("PASS (emitter only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "push" && build.code->push_loop_a_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true);
    if (const int failed = RunPushTests(build, game, patched, quick); failed != 0) return failed;
    std::printf("PASS (push only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "entry" && build.code->entry_fetch_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true);
    if (const int failed = RunEntryTests(build, game, patched, quick); failed != 0) return failed;
    std::printf("PASS (entry only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "lod") {
    if (const int failed = RunLodTests(build, game, patched, text_rva, text_size, image_size, quick); failed != 0) return failed;
    std::printf("PASS (lod only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "workers") {
    twattila::engine_code::Update(false);
    if (const int failed = RunAnimationRowsTests(build, game, patched, quick); failed != 0) return failed;
    if (const int failed = RunPendingSweepTests(build, game, patched, quick); failed != 0) return failed;
    std::printf("PASS (workers only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "effect") {
    twattila::engine_code::Update(false);
    if (const int failed = RunEffectSitesTests(build, game, patched, text_rva, text_size, quick); failed != 0) return failed;
    std::printf("PASS (effect only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "effect_update") {
    twattila::engine_code::Update(false);
    if (const int failed = RunEffectUpdateTests(build, game, patched, quick); failed != 0) return failed;
    std::printf("PASS (effect update only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "fill") {
    twattila::engine_code::Update(false);
    if (const int failed = RunInstanceFillTests(build, game, patched, text_rva, text_size); failed != 0) return failed;
    std::printf("PASS (instance fill only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "count") {
    twattila::engine_code::Update(false);
    if (const int failed = RunParticleCountTests(build, game, patched, text_rva, text_size, quick); failed != 0) return failed;
    std::printf("PASS (particle count only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "exit") {
    twattila::engine_code::Update(false);
    if (const int failed = RunExitDrainTests(build, game, patched, text_rva, text_size); failed != 0) return failed;
    std::printf("PASS (exit drain only)\n");
    return 0;
  }
  if (argc > 3 && std::string(argv[3]) == "sweep" && build.code->sweep_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true, true);
    if (const int failed = RunSweepTests(build, game, patched, quick); failed != 0) return failed;
    std::printf("PASS (sweep only)\n");
    return 0;
  }
#if defined(TWATTILA_ENGINE_PROBE)
  // The probe build runs only this (the other parts check the production sites' bytes).
  if (argc > 3 && std::string(argv[3]) == "probe") {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true, true);
    if (const int failed = RunProbeTests(build, game, patched, quick); failed != 0) return failed;
    std::printf("PASS (probe only)\n");
    return 0;
  }
  std::printf("the probe build runs only `probe`\n");
  return 2;
#endif

  // 2. float to half.
  {
    const uint64_t count = (quick ? (1ull << 28) : (1ull << 32));
    const auto start = std::chrono::steady_clock::now();
    const uint64_t wrong = Parallel(count, [&](uint64_t first, uint64_t last, unsigned worker) -> uint64_t {
      Random random = {.state = 0x9E3779B97F4A7C15ull ^ (first * 0x2545F4914F6CDD1Dull) ^ worker};
      float value = 0.f;
      RegisterCall a = {};
      RegisterCall b = {};
      uint64_t bad = 0;
      for (uint64_t bits = first; bits < last; ++bits) {
        const auto u = static_cast<uint32_t>(bits);
        std::memcpy(&value, &u, 4);
        for (uint32_t& in : a.in) in = random.Next();
        a.function = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(game + build.half_rva));
        a.argument = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&value));
        std::memcpy(b.in, a.in, sizeof(a.in));
        b.function = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(patched + build.half_rva));
        b.argument = a.argument;
        CallWithRegisters(&a);
        CallWithRegisters(&b);
        if (std::memcmp(a.out, b.out, sizeof(a.out)) != 0 || a.esp_after != a.esp_before || b.esp_after != b.esp_before) {
          ++bad;
          Report("  half %08X: game eax %08X ecx %08X edx %08X, ours %08X %08X %08X\n", u, a.out[0], a.out[1], a.out[2],
                 b.out[0], b.out[1], b.out[2]);
        }
      }
      return bad;
    });
    CHECK(wrong == 0);
    // The pointer's high half: floats at 16 addresses 64 KiB apart and more.
    std::vector<uint8_t*> places;
    for (int index = 0; index < 16; ++index) {
      places.push_back(static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x10000 * (index + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)));
    }
    Random random = {.state = 12345};
    for (int run = 0; run < 10'000'000; ++run) {
      auto* value = reinterpret_cast<uint32_t*>(places[run % 16] + 4 * (random.Next() % 1024));
      *value = random.Next() ^ (random.Next() << 16);
      if ((run & 3) == 0) *value &= 0x80FFFFFF;  // the smallest exponents often
      RegisterCall a = {.function = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(game + build.half_rva)),
                        .argument = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value))};
      for (uint32_t& in : a.in) in = random.Next();
      RegisterCall b = a;
      b.function = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(patched + build.half_rva));
      CallWithRegisters(&a);
      CallWithRegisters(&b);
      CHECK(std::memcmp(a.out, b.out, sizeof(a.out)) == 0);
    }
    for (uint8_t* place : places) VirtualFree(place, 0, MEM_RELEASE);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("2. float to half: %llu floats and 10 M at 16 addresses, every register equal (%.0f s)\n", count, seconds);
  }

  // 3. The truncation block.
  {
    const auto run = [&](uint64_t count, uint32_t (*float_of)(uint64_t, Random*), uint16_t (*control_of)(Random*),
                         uint32_t mxcsr, uint64_t* flag_differences) -> uint64_t {
      std::atomic<uint64_t> flags = 0;
      const uint64_t wrong = Parallel(count, [&](uint64_t first, uint64_t last, unsigned worker) -> uint64_t {
        const uint32_t saved_mxcsr = _mm_getcsr();
        _mm_setcsr(mxcsr);
        Random random = {.state = 0xD1B54A32D192ED03ull ^ (first * 0x9E3779B97F4A7C15ull) ^ worker};
        BlockCall a = {};
        BlockCall b = {};
        uint64_t bad = 0;
        uint64_t flag_count = 0;
        for (uint64_t index = first; index < last; ++index) {
          for (size_t at = 0; at < sizeof(a.frame); at += 4) Write32(a.frame + at, random.Next());
          const uint32_t input = float_of(index, &random);
          Write32(a.frame + 128 + build.float_slot, input);
          a.eax_in = random.Next();
          a.control = control_of(&random);
          a.block = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(game + build.truncation_block_rva));
          b.eax_in = a.eax_in;
          b.control = a.control;
          b.block = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(patched + build.truncation_block_rva));
          std::memcpy(b.frame, a.frame, sizeof(a.frame));
          EnterBlock(&a);
          EnterBlock(&b);
          const bool same = std::memcmp(a.frame, b.frame, sizeof(a.frame)) == 0 && a.eax_out == b.eax_out
                            && a.esp_after == reinterpret_cast<uintptr_t>(a.frame + 128) && b.esp_after == reinterpret_cast<uintptr_t>(b.frame + 128)
                            && a.control_after == (a.control | 0x40) && b.control_after == a.control_after
                            && (a.status & 0x3800) == (b.status & 0x3800);
          if (!same) {
            ++bad;
            Report("  truncation %08X cw %04X: game %08X, ours %08X\n", input, a.control, Read32(a.frame + 128 + build.integer_slot),
                   Read32(b.frame + 128 + build.integer_slot));
          }
          flag_count += ((a.status ^ b.status) & 0x023F) != 0 ? 1 : 0;
        }
        _mm_setcsr(saved_mxcsr);
        flags += flag_count;
        return bad;
      });
      *flag_differences = flags;
      return wrong;
    };
    const auto start = std::chrono::steady_clock::now();
    uint64_t flag_differences = 0;
    const uint64_t count = (quick ? (1ull << 28) : (1ull << 32));
    CHECK(run(count, [](uint64_t index, Random*) { return static_cast<uint32_t>(index); }, [](Random*) -> uint16_t { return 0x27F; },
              0x1F80, &flag_differences) == 0);
    std::printf("3. truncation: %llu floats at control word 0x27F equal (x87 flags or C1 differ in %llu: not compared)\n", count, flag_differences);
    CHECK(run(1ull << 24, [](uint64_t, Random* random) { return random->Next() ^ (random->Next() << 16); },
              [](Random* random) -> uint16_t { return static_cast<uint16_t>(0x3F | (random->Next() & 0x0F00)); }, 0x1F80, &flag_differences) == 0);
    std::printf("   2^24 random floats at every rounding and precision mode equal\n");
    CHECK(run(1ull << 24, [](uint64_t index, Random*) { return static_cast<uint32_t>(index) | ((index & 1) != 0 ? 0x80000000u : 0u); },
              [](Random*) -> uint16_t { return 0x27F; }, 0x9FC0, &flag_differences) == 0);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("   every denormal (and its negative) with DAZ and FTZ on equal (%.0f s)\n", seconds);
  }

  // 4. fmod.
  {
    // edx: the CRT's fmod table, an address in each copy.
    const uint32_t copy_distance = static_cast<uint32_t>(patched - game);
    const auto compare = [&](const FmodCall& a, const FmodCall& b) {
      uint32_t b_out[7];
      std::memcpy(b_out, b.out, sizeof(b_out));
      b_out[2] -= copy_distance;
      return std::memcmp(a.out, b_out, sizeof(a.out)) == 0 && std::memcmp(a.result, b.result, 10) == 0 && a.status == b.status
             && a.control_after == b.control_after && a.control_after == (a.control | 0x40) && a.empty_after == b.empty_after
             && a.esp_after == a.esp_before && b.esp_after == b.esp_before;
    };
    // 0: normal, 1 zero, 2 denormal, 3 infinite, 4 NaN
    const auto float_class = [](uint32_t bits) {
      const uint32_t exponent = (bits >> 23) & 0xFF;
      const uint32_t mantissa = bits & 0x7FFFFF;
      if (exponent == 0xFF) return (mantissa == 0 ? 3 : 4);
      if (exponent == 0) return (mantissa == 0 ? 1 : 2);
      return 0;
    };
    const auto call_both = [&](FmodCall* a, FmodCall* b, Random* random) {
      for (uint32_t& in : a->in) in = random->Next();
      a->function = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(game + build.fmod_rva));
      *b = *a;
      b->function = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(patched + build.fmod_rva));
      CallFmod(a);
      CallFmod(b);
      return compare(*a, *b);
    };
    const auto start = std::chrono::steady_clock::now();
    // Floats: a denormal x or y is normal in 80 bits; an infinite x, a zero y and NaNs raise the CRT's errors.
    const uint64_t float_wrong = Parallel(20'000'000, [&](uint64_t first, uint64_t last, unsigned worker) -> uint64_t {
      Random random = {.state = 0xA0761D6478BD642Full ^ (first * 0x9E3779B97F4A7C15ull) ^ worker};
      uint64_t bad = 0;
      for (uint64_t index = first; index < last; ++index) {
        uint32_t x = random.Next() ^ (random.Next() << 16);
        uint32_t y = random.Next() ^ (random.Next() << 16);
        // Near exponents often (the usual fmod), any exponents otherwise.
        if ((index & 1) != 0) y = (y & 0x807FFFFF) | ((((x >> 23) & 0xFF) - (random.Next() % 24)) & 0xFF) << 23;
        if (float_class(x) >= 3 || float_class(y) == 1 || float_class(y) == 4) continue;
        FmodCall a = {.control = 0x27F};
        FromFloat(a.x, x);
        FromFloat(a.y, y);
        FmodCall b;
        if (!call_both(&a, &b, &random)) {
          ++bad;
          Report("  fmod(%08X, %08X): game eax %08X ecx %08X edx %08X sw %04X cw %04X r %016llX, ours %08X %08X %08X %04X %04X %016llX\n",
                 x, y, a.out[0], a.out[1], a.out[2], a.status, a.control_after, Read64(a.result), b.out[0], b.out[1], b.out[2],
                 b.status, b.control_after, Read64(b.result));
        }
      }
      return bad;
    });
    CHECK(float_wrong == 0);
    std::printf("4. fmod: 20 M float pairs (normal, zero x, denormal, infinite y) equal\n");
    const uint64_t extended_wrong = Parallel(20'000'000, [&](uint64_t first, uint64_t last, unsigned worker) -> uint64_t {
      Random random = {.state = 0x8BB84B93962EACC9ull ^ (first * 0x9E3779B97F4A7C15ull) ^ worker};
      uint64_t bad = 0;
      for (uint64_t index = first; index < last; ++index) {
        const uint64_t x_mantissa = (static_cast<uint64_t>(random.Next()) << 32 | random.Next()) | 0x8000000000000000ull;
        const uint64_t y_mantissa = (static_cast<uint64_t>(random.Next()) << 32 | random.Next()) | 0x8000000000000000ull;
        const uint32_t x_exponent = 1 + random.Next() % 0x7FFE;
        uint32_t y_exponent = 1 + random.Next() % 0x7FFE;
        if ((index & 3) != 0) y_exponent = std::clamp<int>(static_cast<int>(x_exponent) - static_cast<int>(random.Next() % 200), 1, 0x7FFE);
        FmodCall a = {.control = 0x27F};
        Extended(a.x, (random.Next() & 1) != 0, x_exponent, x_mantissa);
        Extended(a.y, (random.Next() & 1) != 0, y_exponent, y_mantissa);
        FmodCall b;
        if (!call_both(&a, &b, &random)) {
          ++bad;
          Report("  fmod 80-bit exponents %u %u\n", x_exponent, y_exponent);
        }
      }
      return bad;
    });
    CHECK(extended_wrong == 0);
    std::printf("   20 M 80-bit pairs equal\n");
    // The game's: an animation time and a cycle (fld1, a frame count).
    Random random = {.state = 77};
    for (int index = 0; index < 2'000'000; ++index) {
      const float x = static_cast<float>(random.Next() % 100000) * 0.0137f;
      const float y = ((index & 1) != 0 ? 1.f : static_cast<float>(1 + random.Next() % 64));
      FmodCall a = {.control = 0x27F};
      uint32_t x_bits;
      uint32_t y_bits;
      std::memcpy(&x_bits, &x, 4);
      std::memcpy(&y_bits, &y, 4);
      FromFloat(a.x, x_bits);
      FromFloat(a.y, y_bits);
      FmodCall b;
      CHECK(call_both(&a, &b, &random));
    }
    std::printf("   2 M of the game's (time, 1) and (time, frames) equal\n");
    // Other control words (the stub hands them to the CRT), a user _matherr, the CRT's flag.
    for (const uint16_t control : {0x37F, 0x07F, 0x67F, 0xA7F, 0xE7F, 0x25F, 0x27E, 0x0272}) {
      for (int index = 0; index < 100'000; ++index) {
        FmodCall a = {.control = control};
        FromFloat(a.x, 0x3F800000u + (random.Next() & 0x0FFFFFFF));
        FromFloat(a.y, 0x3E000000u + (random.Next() & 0x00FFFFFF));
        FmodCall b;
        CHECK(call_both(&a, &b, &random));
      }
    }
    for (const int setting : {0, 1}) {
      uint8_t* address_a = game + (setting == 0 ? build.matherr_rva : build.crt_flag_rva);
      uint8_t* address_b = patched + (setting == 0 ? build.matherr_rva : build.crt_flag_rva);
      const uint32_t saved = Read32(address_a);
      Write32(address_a, (setting == 0 ? ~saved : 1u));
      Write32(address_b, Read32(address_a));
      for (int index = 0; index < 100'000; ++index) {
        FmodCall a = {.control = 0x27F};
        FromFloat(a.x, 0x3F800000u + (random.Next() & 0x0FFFFFFF));
        FromFloat(a.y, 0x3E000000u + (random.Next() & 0x00FFFFFF));
        FmodCall b;
        CHECK(call_both(&a, &b, &random));
      }
      Write32(address_a, saved);
      Write32(address_b, saved);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("   8 other control words, a user _matherr, the CRT's flag set: equal (%.0f s)\n", seconds);
  }

  // 5. On and off while 4 threads run the patched float to half.
  {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> wrong = 0;
    std::atomic<uint64_t> calls = 0;
    using Half = uint16_t(__cdecl*)(const float*);
    const auto game_half = reinterpret_cast<Half>(game + build.half_rva);
    const auto patched_half = reinterpret_cast<Half>(patched + build.half_rva);
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        Random random = {.state = 1000u + static_cast<uint64_t>(worker)};
        uint64_t local = 0;
        while (!stop.load(std::memory_order_relaxed)) {
          const uint32_t bits = random.Next() ^ (random.Next() << 16);
          float value;
          std::memcpy(&value, &bits, 4);
          if (game_half(&value) != patched_half(&value)) wrong.fetch_add(1);
          ++local;
        }
        calls += local;
      });
    }
    constexpr int SWITCHES = 200;
    int toggles = 0;
    int busy = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int round = 0; round < SWITCHES; ++round) {
      const bool on = (round % 2) != 0;  // written at the start: off first
      int tries = 0;
      do {
        twattila::engine_code::Update(on);
        ++tries;
      } while ((ec::written != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries - 1;
      toggles += ((ec::written != 0) == on) ? 1 : 0;
    }
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    stop = true;
    for (auto& thread : threads) thread.join();
    CHECK(wrong == 0);
    CHECK(toggles == SWITCHES);
    std::printf("5. %d switches while 4 threads made %llu calls: all equal (%d writes waited for a thread inside a site; "
                "%.1f ms a write)\n",
                SWITCHES, calls.load(), busy, milliseconds / (SWITCHES + busy));
  }

  // The ready check stub's first bytes with another tail (the older ones in 6, the controls in 9), in a page of executable memory of its own.
  const auto ready_check_with_tail = [](std::span<const uint8_t> tail) {
    auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (code != nullptr) {
      std::memcpy(code, ec::READY_CHECK_STUB.data(), READY_CHECK_KEPT);
      std::memcpy(code + READY_CHECK_KEPT, tail.data(), tail.size());
    }
    return code;
  };

  // 6. Timing (one thread, warm).
  {
    if (ec::written == 0) twattila::engine_code::Update(true);
    CHECK(ec::written == (ec::CPU_CODE_SITES & ec::found));
    using Half = uint16_t(__cdecl*)(const float*);
    std::vector<float> rows(20 * 4096);
    Random random = {.state = 5};
    for (size_t index = 0; index < rows.size(); ++index) {
      switch (index % 20) {
        case 0: case 1: case 2: rows[index] = static_cast<float>(random.Next() % 200000) * 0.01f - 1000.f; break;  // position
        case 3: case 4: rows[index] = static_cast<float>(random.Next() % 1000) / 1000.f; break;                   // uv
        case 5: case 6: case 7: case 8: rows[index] = static_cast<float>(random.Next() % 256) / 255.f; break;      // colour
        case 9: rows[index] = static_cast<float>(random.Next() % 8); break;                                       // direction
        default: rows[index] = ((random.Next() & 3) == 0 ? 0.f : static_cast<float>(random.Next() % 4096) / 64.f); break;
      }
    }
    const auto time_half = [&](Half half) {
      volatile uint32_t sink = 0;
      const auto start = std::chrono::steady_clock::now();
      for (int round = 0; round < 200; ++round) {
        for (const float& value : rows) sink = sink + half(&value);
      }
      return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / (200.0 * rows.size());
    };
    const double half_game = time_half(reinterpret_cast<Half>(game + build.half_rva));
    const double half_ours = time_half(reinterpret_cast<Half>(patched + build.half_rva));
    const auto time_block = [&](uint8_t* image) {
      BlockCall call = {.block = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image + build.truncation_block_rva)), .control = 0x27F};
      const auto start = std::chrono::steady_clock::now();
      for (int index = 0; index < 2'000'000; ++index) {
        const float value = static_cast<float>(index & 1023) * 0.37f;
        std::memcpy(call.frame + 128 + build.float_slot, &value, 4);
        EnterBlock(&call);
      }
      return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 2'000'000.0;
    };
    const double block_game = time_block(game);
    const double block_ours = time_block(patched);
    const auto time_fmod = [&](uint8_t* image) {
      FmodCall call = {.function = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image + build.fmod_rva)), .control = 0x27F};
      FromFloat(call.y, 0x3F800000u);
      const auto start = std::chrono::steady_clock::now();
      for (int index = 0; index < 2'000'000; ++index) {
        const float value = static_cast<float>(index & 4095) * 0.0137f;
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        FromFloat(call.x, bits);
        CallFmod(&call);
      }
      return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 2'000'000.0;
    };
    const double fmod_game = time_fmod(game);
    const double fmod_ours = time_fmod(patched);
    std::printf("6. ns a call, game / ours: float to half %.2f / %.2f, truncation (with the harness) %.2f / %.2f, "
                "fmod(x, 1) (with the harness) %.2f / %.2f\n",
                half_game, half_ours, block_game, block_ours, fmod_game, fmod_ours);
    // The ready check: the game's, the stub as it ended before (pop ecx: a 4-byte reload of the slot whose top byte it had
    // just stored), with rol / ror (the next one) and ours (bswap); all four called directly, as the page holds them (no jump through
    // the site into the page for ours only); 0 and 16 children (all ready, each with a control block), the best of 5 runs.
    if (build.ready_check_rva != 0) {
      CHECK(std::equal(READY_CHECK_TAIL.begin(), READY_CHECK_TAIL.end(), ec::READY_CHECK_STUB.begin() + READY_CHECK_KEPT));
      constexpr std::array<uint8_t, 2> POPPING_TAIL = {0x59, 0xC3};  // pop ecx; ret
      constexpr std::array<uint8_t, 15> ROTATING_TAIL = {0xC1, 0xC1, 0x08, 0x88, 0xC1, 0xC1, 0xC9, 0x08, 0x8D, 0x64, 0x24, 0x04, 0x3B, 0xC9, 0xC3};
      uint8_t* popping = ready_check_with_tail(POPPING_TAIL);
      uint8_t* rotating = ready_check_with_tail(ROTATING_TAIL);
      CHECK(popping != nullptr && rotating != nullptr);
      std::array<uint8_t, 0x48> self = {};
      self[4] = 1;
      std::vector<std::array<uint8_t, 0x70>> objects(16);
      std::vector<std::array<uint32_t, 3>> blocks(16, {Address(BLOCK_VTABLE.data()), 1, 1});
      std::vector<uint32_t> children(32);
      for (size_t index = 0; index < objects.size(); ++index) {
        objects[index][0x6e] = 1;
        children[2 * index] = Address(objects[index].data());
        children[2 * index + 1] = Address(blocks[index].data());
      }
      Write32(self.data() + 0x44, Address(children.data()));
      using ReadyCheck = uint8_t(__fastcall*)(const void* self, void* unused_edx);
      const std::array<ReadyCheck, 4> checks = {reinterpret_cast<ReadyCheck>(game + build.ready_check_rva), reinterpret_cast<ReadyCheck>(popping),
                                                reinterpret_cast<ReadyCheck>(rotating), reinterpret_cast<ReadyCheck>(ec::page + ec::READY_CHECK_PAGE)};
      std::string line;
      for (const uint32_t count : {0u, 16u}) {
        Write32(self.data() + 0x40, count);
        std::array<double, 4> best = {1e9, 1e9, 1e9, 1e9};
        for (size_t which = 0; which < checks.size(); ++which) {
          CHECK(checks[which](self.data(), nullptr) == 1);
          for (int run = 0; run < 5; ++run) {
            volatile uint32_t sink = 0;
            const auto start = std::chrono::steady_clock::now();
            for (int index = 0; index < 2'000'000; ++index) sink = sink + checks[which](self.data(), nullptr);
            best[which] = std::min(best[which], std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 2'000'000.0);
          }
        }
        line += std::format("{}{} children {:.2f} / {:.2f} / {:.2f} / {:.2f}", line.empty() ? "" : ", ", count, best[0], best[1], best[2], best[3]);
      }
      VirtualFree(popping, 0, MEM_RELEASE);
      VirtualFree(rotating, 0, MEM_RELEASE);
      std::printf("   ready check, game / the stub with pop ecx / with rol and ror / ours (bswap): %s\n", line.c_str());
    }
    // The particle writer's reload after the four stores (game's movups / the stub's lane loads / no reload), and the multiply.
    if (build.code->float4_reload_rva != 0) std::fputs(ReloadTimingLines().c_str(), stdout);
  }

  // 8. The render lists' push.
  if (build.list_push_function_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, true);
    CHECK(ec::written == (ec::found & ~(ec::JOB_SITES | ec::PREFETCH_SITES | ec::EMITTER_PREFETCH_BIT | ec::ENTRY_PREFETCH_BIT | ec::SWEEP_PREFETCH_BIT | ec::MODEL_DETAIL_BIT | ec::WORKER_SITES | ec::PARTICLE_COUNT_SITES | ec::EXIT_DRAIN_BIT)));
    const uint32_t game_push = Address(game + build.list_push_function_rva);
    const uint32_t patched_push = Address(patched + build.list_push_function_rva);
    const auto start = std::chrono::steady_clock::now();
    auto world = std::make_unique<PushWorld>();
    auto before = std::make_unique<PushWorld>();
    auto after_game = std::make_unique<PushWorld>();
    Random random = {.state = 0x5EED};
    uint32_t kept = 0;
    constexpr int PUSHES = 1'000'000;
    for (int run = 0; run < PUSHES; ++run) {
      for (auto& byte : world->list) byte = static_cast<uint8_t>(random.Next());
      const uint32_t flags[] = {0, 1, 2, 0xFF, random.Next() & 0xFF};
      world->list[0x1c] = static_cast<uint8_t>(flags[random.Next() % 5]);
      world->vtable = {random.Next(), Address(reinterpret_cast<const void*>(&ThreadIndexOf))};
      world->provider = Address(world->vtable.data());
      Write32(world->list.data(), Address(&world->provider));
      Write32(world->list.data() + 0x18, Address(world->headers.data()));
      for (uint32_t thread = 0; thread < 8; ++thread) {
        world->headers[thread * 3] = 64;
        world->headers[thread * 3 + 1] = random.Next() % 64;
        world->headers[thread * 3 + 2] = Address(world->data[thread].data());
        if ((run & 63) == 0) {
          for (uint32_t& item : world->data[thread]) item = random.Next();
        }
      }
      push_thread = random.Next() % 8;
      const uint32_t item = random.Next();
      RegisterCall a = {.function = game_push, .argument = Address(&item)};
      for (uint32_t& in : a.in) in = random.Next();
      a.in[1] = Address(world->list.data());  // ecx: the list
      RegisterCall b = a;
      b.function = patched_push;
      *before = *world;
      CallWithRegisters(&a);
      *after_game = *world;
      *world = *before;
      CallWithRegisters(&b);
      kept += (before->list[0x1c] == 1) ? 1 : 0;
      if (std::memcmp(a.out, b.out, sizeof(a.out)) != 0 || a.esp_after != a.esp_before + 4 || b.esp_after != b.esp_before + 4
          || std::memcmp(after_game.get(), world.get(), sizeof(PushWorld)) != 0) {
        std::printf("  push %d (thread %u, size %u, byte %02X): eax %08X / %08X, ecx %08X / %08X\n", run, push_thread,
                    before->headers[push_thread * 3 + 1], before->list[0x1c], a.out[0], b.out[0], a.out[1], b.out[1]);
        CHECK(false);
      }
      CHECK(world->list[0x1c] == 1);
    }
    std::printf("8. render list push: %d random pushes (%u with the byte already 1), every register, esp and byte equal (%.1f s)\n",
                PUSHES, kept, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());

    // On and off while 4 threads push to one list, each to its own vector, the byte cleared now and then (the merge's).
    using Push = uint32_t(__fastcall*)(uint8_t* list, void* unused_edx, const uint32_t* item);
    {
      constexpr uint32_t CHUNK = 1u << 16;
      std::vector<std::vector<uint32_t>> data(4, std::vector<uint32_t>(CHUNK));
      *world = {};
      world->vtable = {0, Address(reinterpret_cast<const void*>(&ThreadIndexOf))};
      world->provider = Address(world->vtable.data());
      Write32(world->list.data(), Address(&world->provider));
      Write32(world->list.data() + 0x18, Address(world->headers.data()));
      for (uint32_t thread = 0; thread < 4; ++thread) {
        world->headers[thread * 3] = CHUNK;
        world->headers[thread * 3 + 2] = Address(data[thread].data());
      }
      const auto push = reinterpret_cast<Push>(patched_push);
      std::atomic<bool> stop = false;
      std::atomic<uint64_t> wrong = 0;
      std::atomic<uint64_t> pushes = 0;
      std::vector<std::thread> threads;
      for (uint32_t thread = 0; thread < 4; ++thread) {
        threads.emplace_back([&, thread] {
          push_thread = thread;
          uint64_t local = 0;
          uint32_t* size = &world->headers[thread * 3 + 1];
          while (!stop.load(std::memory_order_relaxed)) {
            for (uint32_t index = 0; index < CHUNK; ++index) {
              const uint32_t item = (thread << 24) | index;
              if (push(world->list.data(), nullptr, &item) != (((thread + 1) << 24) | index)) wrong.fetch_add(1);
            }
            for (uint32_t index = 0; index < CHUNK; ++index) {
              if (data[thread][index] != ((thread << 24) | index)) wrong.fetch_add(1);
            }
            if (*size != CHUNK) wrong.fetch_add(1);
            *size = 0;
            local += CHUNK;
          }
          pushes += local;
        });
      }
      constexpr int SWITCHES = 200;
      int toggles = 0;
      for (int round = 0; round < SWITCHES; ++round) {
        const bool on = (round % 2) != 0;  // on at the start: off first
        int tries = 0;
        do {
          reinterpret_cast<volatile uint8_t*>(world->list.data())[0x1c] = 0;
          twattila::engine_code::Update(false, twattila::engine_code::Game::ATTILA, on);
          ++tries;
        } while ((ec::written != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
        toggles += ((ec::written != 0) == on) ? 1 : 0;
      }
      stop = true;
      for (auto& thread : threads) thread.join();
      CHECK(wrong == 0);
      CHECK(toggles == SWITCHES);
      std::printf("   %d switches while 4 threads pushed %llu items to one list: every item and return the game's\n", SWITCHES,
                  pushes.load());
    }

    // Timing: 4 threads push to 16 lists 32 bytes apart (the game's), each list's byte cleared before each round.
    {
      constexpr uint32_t LISTS = 16;
      constexpr uint32_t ROUND = 512;  // pushes a thread makes to each list a round
      constexpr int ROUNDS = 400;
      struct alignas(64) Lists {
        std::array<uint8_t, 32 * LISTS> lists;
        std::array<std::array<uint32_t, 4 * 3>, LISTS> headers;
      };
      auto lists = std::make_unique<Lists>();
      std::vector<std::vector<uint32_t>> data(4 * LISTS, std::vector<uint32_t>(ROUND));
      std::array<uint32_t, 2> vtable = {0, Address(reinterpret_cast<const void*>(&ThreadIndexOf))};
      const uint32_t provider = Address(vtable.data());
      const auto time = [&](uint32_t function) {
        const auto push = reinterpret_cast<Push>(function);
        std::atomic<int> ready = 0;
        std::atomic<int> round_done = 0;
        std::atomic<int> round_go = 0;
        std::vector<std::thread> threads;
        for (uint32_t list = 0; list < LISTS; ++list) {
          uint8_t* at = lists->lists.data() + 32 * list;
          Write32(at, Address(&provider));
          Write32(at + 0x18, Address(lists->headers[list].data()));
        }
        double seconds = 0.;
        for (uint32_t thread = 0; thread < 4; ++thread) {
          threads.emplace_back([&, thread] {
            push_thread = thread;
            ++ready;
            for (int round = 0; round < ROUNDS; ++round) {
              while (round_go.load() <= round) {}
              for (uint32_t index = 0; index < ROUND; ++index) {
                for (uint32_t list = 0; list < LISTS; ++list) {
                  const uint32_t item = index;
                  push(lists->lists.data() + 32 * list, nullptr, &item);
                }
              }
              ++round_done;
            }
          });
        }
        while (ready.load() < 4) {}
        for (int round = 0; round < ROUNDS; ++round) {
          for (uint32_t list = 0; list < LISTS; ++list) {
            lists->lists[32 * list + 0x1c] = 0;
            for (uint32_t thread = 0; thread < 4; ++thread) {
              lists->headers[list][thread * 3] = ROUND;
              lists->headers[list][thread * 3 + 1] = 0;
              lists->headers[list][thread * 3 + 2] = Address(data[list * 4 + thread].data());
            }
          }
          const auto round_start = std::chrono::steady_clock::now();
          round_go = round + 1;
          while (round_done.load() < 4 * (round + 1)) {}
          seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - round_start).count();
        }
        for (auto& thread : threads) thread.join();
        return seconds * 1e9 / (static_cast<double>(ROUNDS) * ROUND * LISTS);
      };
      const double game_ns = time(game_push);
      const double ours_ns = time(patched_push);
      const double game_again = time(game_push);
      const double ours_again = time(patched_push);
      std::printf("   4 threads, 16 lists: ns a push (wall clock, each thread's), game %.2f / %.2f, ours %.2f / %.2f\n", game_ns,
                  game_again, ours_ns, ours_again);
    }
  } else {
    std::printf("8. render list push: none in this build\n");
  }

  // 9. The ready check.
  if (build.ready_check_rva != 0) {
    twattila::engine_code::Update(true);
    CHECK(ec::written == (ec::CPU_CODE_SITES & ec::found) && (ec::written >> 4 & 1) != 0);
    const uint32_t game_ready = Address(game + build.ready_check_rva);
    const uint32_t patched_ready = Address(patched + build.ready_check_rva);
    const auto start = std::chrono::steady_clock::now();
    // The object ([+4] its flag, [+0x40] the count, [+0x44] the children), the children ({object, control block}), the
    // objects (the ready byte at +0x6e), the control blocks ({vtable, uses, weaks}).
    constexpr uint32_t MAX_CHILDREN = 4096;
    // `this`: at a low address (top byte 0: low_page, below 16 MB) or at the top of the
    // address space (high_page: top byte 0x7F, the process isn't large-address aware), picked for each vector; a merge of al into the top byte
    // is not a replace only for the second.
    CHECK(low_page != nullptr && (Address(low_page) >> 24) == 0);
    CHECK(high_page != nullptr && (Address(high_page) >> 24) == 0x7F);
    int this_place = 0;  // 0: either; 1: the low page; 2: the high one
    uint64_t high_this = 0;
    uint32_t ours_ready = patched_ready;  // the function run as ours (the controls change it)
    bool report_failures = true;
    std::vector<uint32_t> children(2 * MAX_CHILDREN);
    std::vector<std::array<uint8_t, 0x70>> objects(MAX_CHILDREN);
    std::vector<std::array<uint32_t, 3>> blocks(MAX_CHILDREN);
    std::vector<std::array<uint32_t, 3>> blocks_before(MAX_CHILDREN);
    const auto uses_of = [](Random* random) -> uint32_t {
      constexpr std::array<uint32_t, 12> EDGES = {1, 2, 0xFF, 0x100, 0xFFFF, 0x10000, 0xFFFFFF, 0x1000000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF};
      switch (random->Next() % 3) {
        case 0:  return EDGES[random->Next() % EDGES.size()];
        case 1:  return 1 + random->Next() % 300;
        default: return std::max(1u, random->Next());
      }
    };
    std::array<uint64_t, 4> kinds = {};  // no child, the flag 0, every child ready, a child not ready
    // One vector of `count` children (the routine's lea keeps count x 8: its low 29 bits), the first `not_ready` of them
    // ready (not_ready >= that: all), the last one with a control block or not, the others at random; a child neither
    // code may read (after the first not ready, or all with the flag 0) half the time at a random object address. `dead`:
    // that child's uses 0 (CONTROL). Both copies called with the same registers, flags and stack bytes.
    const auto check = [&](uint32_t count, uint32_t not_ready, uint8_t flag, bool last_block, Random* random, uint32_t dead = ~0u) {
      const uint32_t n = count & 0x1FFFFFFF;
      const bool at_top = (this_place == 2 || (this_place == 0 && (random->Next() & 1) != 0));
      const std::span<uint8_t, 0x48> self = (at_top ? std::span<uint8_t, 0x48>(high_page + 0x80, 0x48) : std::span<uint8_t, 0x48>(low_page + 0x80, 0x48));
      high_this += (at_top ? 1 : 0);
      for (uint8_t& byte : self) byte = static_cast<uint8_t>(random->Next());
      self[4] = flag;
      Write32(self.data() + 0x40, count);
      Write32(self.data() + 0x44, (n == 0 && (random->Next() & 1) != 0) ? random->Next() : Address(children.data()));
      for (uint32_t index = 0; index < n; ++index) {
        objects[index][0x6e] = static_cast<uint8_t>(index < not_ready ? 1 + random->Next() % 255 : (index == not_ready ? 0 : random->Next()));
        const bool read = (flag != 0 && index <= not_ready);
        children[2 * index] = ((read || (random->Next() & 1) == 0) ? Address(objects[index].data()) : random->Next());
        const bool has_block = (index + 1 == n ? last_block : (random->Next() & 1) != 0) || index == dead;
        blocks[index] = {Address(BLOCK_VTABLE.data()), (index == dead ? 0u : uses_of(random)), (index == dead ? 1u : 1 + random->Next() % 1000)};
        children[2 * index + 1] = (has_block ? Address(blocks[index].data()) : 0u);
      }
      std::copy_n(blocks.begin(), n, blocks_before.begin());
      std::array<uint8_t, 0x48> self_before;
      std::copy(self.begin(), self.end(), self_before.begin());
      ReadyCall call = {.in = {random->Next(), Address(self.data()), random->Next(), random->Next(), random->Next(), random->Next(), random->Next()},
                        .flags_in = 0x202 | (random->Next() & 0x8D5)};
      for (uint8_t& byte : call.stack) byte = static_cast<uint8_t>(random->Next());
      const auto world_kept = [&] { return std::equal(blocks.begin(), blocks.begin() + n, blocks_before.begin()) && std::equal(self.begin(), self.end(), self_before.begin()); };
      ReadyCall a = call;
      ready_function = game_ready;
      CallReady(&a);
      const bool game_kept = world_kept();
      ReadyCall b = call;
      ready_function = ours_ready;
      CallReady(&b);
      const bool ours_kept = world_kept();
      // The game's results as engine_code.hpp derives them from its bytes.
      const uint8_t result = (n == 0 ? flag : static_cast<uint8_t>(flag != 0 && not_ready >= n ? 1 : 0));
      uint32_t high = count;
      if (n != 0) high = (children[2 * n - 1] != 0 ? blocks_before[n - 1][1] + 1 : children[2 * n - 2]);
      const uint32_t ecx = (Address(self.data()) & 0x00FFFFFFu) | (static_cast<uint32_t>(result) << 24);
      const bool derived = a.out[0] == ((high & 0xFFFFFF00u) | result) && a.out[1] == ecx
                           && std::memcmp(a.out + 2, call.in + 2, 5 * sizeof(uint32_t)) == 0 && a.flags_out == 0x4600
                           && a.esp_after == Address(&a) + READY_CALL_ESP && std::memcmp(a.stack, call.stack, 28) == 0
                           && Read32(a.stack + 28) == call.in[5] && Read32(a.stack + 32) == call.in[4] && Read32(a.stack + 36) == call.in[3]
                           && Read32(a.stack + 40) == ecx && std::memcmp(a.stack + 48, call.stack + 48, 16) == 0;
      const bool same = std::memcmp(a.out, b.out, sizeof(a.out)) == 0 && a.flags_out == b.flags_out
                        && b.esp_after == Address(&b) + READY_CALL_ESP && std::memcmp(a.stack, b.stack, sizeof(a.stack)) == 0;
      const bool good = derived && same && game_kept && ours_kept && disposed.load() == 0;
      if (!good && report_failures) {
        Report("  ready check: count %08X, first not ready %u, flag %02X, last block %d: derived %d same %d kept %d %d; eax %08X / %08X "
               "ecx %08X / %08X flags %04X / %04X\n", count, not_ready, flag, last_block ? 1 : 0, derived ? 1 : 0, same ? 1 : 0,
               game_kept ? 1 : 0, ours_kept ? 1 : 0, a.out[0], b.out[0], a.out[1], b.out[1], a.flags_out, b.flags_out);
      }
      ++kinds[n == 0 ? 0 : (flag == 0 ? 1 : (not_ready >= n ? 2 : 3))];
      return good;
    };
    Random random = {.state = 0xC0FFEE};
    uint64_t cases = 0;
    constexpr std::array<uint8_t, 6> FLAGS = {0, 1, 2, 0x7F, 0x80, 0xFF};
    for (uint32_t n = 0; n <= 64; ++n) {
      for (uint32_t not_ready = 0; not_ready <= n; ++not_ready) {
        for (size_t flag_index = 0; flag_index <= FLAGS.size(); ++flag_index) {
          const uint8_t flag = (flag_index < FLAGS.size() ? FLAGS[flag_index] : static_cast<uint8_t>(random.Next()));
          for (const bool last_block : {false, true}) {
            for (uint32_t high = 0; high < 8; ++high) {
              CHECK(check(n | (high << 29), not_ready, flag, last_block, &random));
              ++cases;
            }
          }
        }
      }
    }
    for (uint32_t flag = 0; flag < 256; ++flag) {
      for (uint32_t high = 0; high < 8; ++high) {
        CHECK(check(high << 29, 0, static_cast<uint8_t>(flag), false, &random));
        ++cases;
      }
      for (const uint32_t not_ready : {0u, 1u}) {
        for (const bool last_block : {false, true}) {
          CHECK(check(1, not_ready, static_cast<uint8_t>(flag), last_block, &random));
          ++cases;
        }
      }
    }
    constexpr int RANDOM_VECTORS = 2'000'000;
    for (int run = 0; run < RANDOM_VECTORS; ++run) {
      const uint32_t n = ((run & 255) == 0 ? random.Next() % (MAX_CHILDREN + 1) : random.Next() % 65);
      const uint32_t not_ready = ((random.Next() & 3) == 0 ? n : random.Next() % (n + 1));
      CHECK(check(n | (random.Next() << 29), not_ready, static_cast<uint8_t>(random.Next()), (random.Next() & 1) != 0, &random));
      ++cases;
    }
    std::printf("9. ready check: %llu vectors (%llu without a child, %llu with the flag 0, %llu all ready, %llu with one not ready; %llu with `this` at the top of the "
                "address space): every register, the flags, esp, the stack, every count equal and the game's as derived (%.1f s)\n",
                cases, kinds[0], kinds[1], kinds[2], kinds[3], high_this, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    CHECK(high_this > cases / 3);
    // CONTROL: a dead child (uses 0, never in the game's vectors): the game's release disposes and destroys it, the stub
    // never touches it; the compare must see it.
    std::printf("   CONTROL, a child with uses 0 (must differ):\n");
    CHECK(!check(3, 3, 1, true, &random, 1));
    CHECK(disposed.load() == 0x10001);
    disposed = 0;
    // CONTROL: the tail with `or cl, al` for `mov cl, al` (the equal compare's flags kept by a cmp ecx, ecx): al merged into the top byte of `this`
    // instead of put in it. With a top byte of 0 the result and the flags are the same; with 0x7F they are not (a child exists: al is 0 or 1).
    {
      constexpr std::array<uint8_t, 13> MERGING_TAIL = {0x0F, 0xC9, 0x08, 0xC1, 0x0F, 0xC9, 0x8D, 0x64, 0x24, 0x04, 0x3B, 0xC9, 0xC3};
      constexpr int CONTROL_VECTORS = 20'000;
      uint8_t* merging = ready_check_with_tail(MERGING_TAIL);
      CHECK(merging != nullptr);
      ours_ready = Address(merging);
      report_failures = false;
      std::array<int, 2> agreeing = {};  // `this` at the low page, `this` at the top
      for (const int place : {1, 2}) {
        this_place = place;
        for (int run = 0; run < CONTROL_VECTORS; ++run) {
          const uint32_t n = 1 + random.Next() % 64;
          agreeing[place - 1] += check(n | (random.Next() << 29), random.Next() % (n + 1), static_cast<uint8_t>(random.Next()), (random.Next() & 1) != 0, &random) ? 1 : 0;
        }
      }
      this_place = 0;
      report_failures = true;
      ours_ready = patched_ready;
      VirtualFree(merging, 0, MEM_RELEASE);
      std::printf("   CONTROL, `or cl, al` for `mov cl, al`: %d of %d agree with `this` at the low page (all), %d of %d with `this` at the top (none)\n", agreeing[0],
                  CONTROL_VECTORS, agreeing[1], CONTROL_VECTORS);
      CHECK(agreeing[0] == CONTROL_VECTORS && agreeing[1] == 0);
    }

    // On and off while 4 threads call both copies on 8 vectors sharing 16 control blocks (the game's copies and releases
    // move the counts the stub reads: eax's bits from the last child's uses may then differ, so al alone, the caller's).
    using ReadyCheck = uint8_t(__fastcall*)(const void* self, void* unused_edx);
    {
      struct Vector {
        std::array<uint8_t, 0x48> self;
        std::array<uint32_t, 2 * 64> children;
        uint8_t result;
      };
      std::vector<Vector> vectors(8);
      std::array<std::array<uint32_t, 3>, 16> shared_blocks = {};
      for (auto& block : shared_blocks) block = {Address(BLOCK_VTABLE.data()), 1 + random.Next() % 4, 1};
      const auto blocks_start = shared_blocks;
      std::array<std::array<uint8_t, 0x70>, 64> shared_objects = {};
      for (size_t index = 1; index < shared_objects.size(); ++index) shared_objects[index][0x6e] = static_cast<uint8_t>(1 + index);
      for (uint32_t index = 0; index < vectors.size(); ++index) {
        Vector& vector = vectors[index];
        const uint32_t n = 8 * index;
        vector.self = {};
        vector.self[4] = (index == 5 ? 0 : 1);
        Write32(vector.self.data() + 0x40, n);
        Write32(vector.self.data() + 0x44, Address(vector.children.data()));
        // Odd vectors hold object 0 (not ready) somewhere.
        const uint32_t not_ready = ((index & 1) != 0 ? random.Next() % n : n);
        for (uint32_t child = 0; child < n; ++child) {
          vector.children[2 * child] = Address(shared_objects[child == not_ready ? 0 : 1 + random.Next() % 63].data());
          vector.children[2 * child + 1] = ((random.Next() & 3) != 0 ? Address(shared_blocks[random.Next() % 16].data()) : 0u);
        }
        vector.result = (n == 0 ? vector.self[4] : static_cast<uint8_t>(vector.self[4] != 0 && not_ready >= n ? 1 : 0));
      }
      const auto game_check = reinterpret_cast<ReadyCheck>(game + build.ready_check_rva);
      const auto patched_check = reinterpret_cast<ReadyCheck>(patched + build.ready_check_rva);
      std::atomic<bool> stop = false;
      std::atomic<uint64_t> wrong = 0;
      std::atomic<uint64_t> calls = 0;
      std::vector<std::thread> threads;
      for (int worker = 0; worker < 4; ++worker) {
        threads.emplace_back([&, worker] {
          Random thread_random = {.state = 2000u + static_cast<uint64_t>(worker)};
          uint64_t local = 0;
          while (!stop.load(std::memory_order_relaxed)) {
            const Vector& vector = vectors[thread_random.Next() % vectors.size()];
            if (patched_check(vector.self.data(), nullptr) != vector.result) wrong.fetch_add(1);
            if (game_check(vector.self.data(), nullptr) != vector.result) wrong.fetch_add(1);
            ++local;
          }
          calls += local;
        });
      }
      constexpr int SWITCHES = 200;
      int toggles = 0;
      int busy = 0;
      for (int round = 0; round < SWITCHES; ++round) {
        const bool on = (round % 2) != 0;  // on at the start: off first
        int tries = 0;
        do {
          twattila::engine_code::Update(on);
          ++tries;
        } while ((ec::written != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
        busy += tries - 1;
        toggles += ((ec::written != 0) == on) ? 1 : 0;
      }
      stop = true;
      for (auto& thread : threads) thread.join();
      CHECK(wrong == 0);
      CHECK(toggles == SWITCHES);
      CHECK(shared_blocks == blocks_start);
      CHECK(disposed.load() == 0);
      std::printf("   %d switches while 4 threads made %llu calls of each copy on 8 vectors sharing 16 control blocks: every "
                  "result the game's, every count back (%d writes waited for a thread inside a site)\n",
                  SWITCHES, calls.load(), busy);
    }

    // Timing: one thread on 16 and 64 children, all ready (every child with its own control block), and 16 with the first
    // not ready; 4 threads on the same 16 (the game's copies and releases share the control blocks' lines).
    {
      CHECK(ec::written == (ec::CPU_CODE_SITES & ec::found));
      const auto make = [&](uint32_t n, uint32_t not_ready, std::array<uint8_t, 0x48>* object) {
        *object = {};
        (*object)[4] = 1;
        Write32(object->data() + 0x40, n);
        Write32(object->data() + 0x44, Address(children.data()));
        for (uint32_t index = 0; index < n; ++index) {
          objects[index][0x6e] = (index == not_ready ? 0 : 1);
          blocks[index] = {Address(BLOCK_VTABLE.data()), 1, 1};
          children[2 * index] = Address(objects[index].data());
          children[2 * index + 1] = Address(blocks[index].data());
        }
      };
      const auto time = [&](uint32_t function, const std::array<uint8_t, 0x48>& object, int thread_count) {
        const auto ready_check = reinterpret_cast<ReadyCheck>(function);
        constexpr int CALLS = 400'000;
        std::atomic<int> waiting = thread_count;
        std::vector<std::thread> threads;
        std::vector<double> seconds(thread_count);
        for (int worker = 0; worker < thread_count; ++worker) {
          threads.emplace_back([&, worker] {
            --waiting;
            while (waiting.load() > 0) {}
            volatile uint32_t sink = 0;
            const auto thread_start = std::chrono::steady_clock::now();
            for (int index = 0; index < CALLS; ++index) sink = sink + ready_check(object.data(), nullptr);
            seconds[worker] = std::chrono::duration<double>(std::chrono::steady_clock::now() - thread_start).count();
          });
        }
        for (auto& thread : threads) thread.join();
        return *std::max_element(seconds.begin(), seconds.end()) * 1e9 / CALLS;
      };
      std::array<uint8_t, 0x48> object = {};
      std::string line;
      for (const auto& [n, not_ready, thread_count] : {std::tuple{16u, 16u, 1}, std::tuple{64u, 64u, 1}, std::tuple{16u, 0u, 1}, std::tuple{16u, 16u, 4}}) {
        make(n, not_ready, &object);
        const double game_ns = time(game_ready, object, thread_count);
        const double ours_ns = time(patched_ready, object, thread_count);
        line += std::format("{}{} children{}{}: {:.1f} / {:.1f}", line.empty() ? "" : ", ", n, not_ready < n ? " (the first not ready)" : "",
                            thread_count > 1 ? std::format(", {} threads", thread_count) : "", game_ns, ours_ns);
      }
      std::printf("   ns a call, game / ours: %s\n", line.c_str());
    }
  } else {
    std::printf("9. ready check: none in this build\n");
  }
  if (argc > 3 && std::string(argv[3]) == "ready") {  // parts 1 to 9, in their places (tests.exe <dll> quick ready)
    std::printf("PASS (up to 9)\n");
    return 0;
  }

  // 10. Faster Job Wake-Ups (the third row; its sites are off until here).
  if (build.code->job_wake_all_rva != 0) {
    twattila::engine_code::Update(true);
    if (const int failed = RunJobTests(build, game, patched, quick); failed != 0) return failed;
  } else {
    CHECK(ec::sites[5].at == nullptr && ec::sites[6].at == nullptr && ec::sites[7].at == nullptr);
    std::printf("10. job wake-ups: none in this build\n");
  }

  // 11. The sprite row builder's 20 half floats (a site of Faster Game CPU Code).
  if (build.code->half_rows_rva != 0) {
    twattila::engine_code::Update(true);
    if (const int failed = RunRowsTests(build, game, patched, text_rva, text_size, quick); failed != 0) return failed;
    twattila::engine_code::Update(true);
    if (const int failed = RunAdv(build, game, patched, text_rva, text_size, true, "all"); failed != 0) return failed;
    twattila::engine_code::Update(true);
  } else {
    CHECK(ec::sites[8].at == nullptr);
    std::printf("11. sprite row builder: none in this build\n");
  }

  // 12. The effect tree's child sum (Faster Effect Prefetch, the fourth row).
  if (build.code->child_sum_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
    if (const int failed = RunSumTests(build, game, patched, quick); failed != 0) return failed;
  } else {
    CHECK(ec::sites[ec::CHILD_SUM_INDEX].at == nullptr);
    std::printf("12. effect tree child sum: none in this build\n");
  }

  // 14. The particle writer's two float4 reloads (Faster Game CPU Code).
  if (build.code->float4_reload_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
    if (const int failed = RunReloadTests(build, game, patched, quick); failed != 0) return failed;
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
  } else {
    CHECK(ec::sites[ec::FLOAT4_RELOAD_FIRST_SITE].at == nullptr && ec::sites[ec::FLOAT4_RELOAD_FIRST_SITE + 1].at == nullptr);
    std::printf("14. particle writer float4 reloads: none in this build\n");
  }

  // 15. The particle update loop's prefetch (Faster Effect Prefetch).
  if (build.code->particle_loop_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
    if (const int failed = RunParticleTests(build, game, patched, quick); failed != 0) return failed;
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
  } else {
    CHECK(ec::sites[ec::PARTICLE_PREFETCH_INDEX].at == nullptr);
    std::printf("15. particle loop prefetch: none in this build\n");
  }

  // 16. The emitter update loop's prefetch (Faster Effect Prefetch).
  if (build.code->emitter_loop_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
    if (const int failed = RunEmitterTests(build, game, patched, quick); failed != 0) return failed;
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
  } else {
    CHECK(ec::sites[ec::EMITTER_PREFETCH_INDEX].at == nullptr);
    std::printf("16. emitter loop prefetch: none in this build\n");
  }

  // 16f. The Faster Effect Prefetch row and the Faster Emitter Prefetch row are independent in Update: each writes only its own sites (the child sum and the
  // particle loop; the emitter loop), whatever the other is asked for.
  if (build.code->child_sum_rva != 0 && build.code->particle_loop_rva != 0 && build.code->emitter_loop_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, false);
    CHECK((ec::written & ec::EMITTER_PREFETCH_BIT) == 0 && (ec::written & ec::PREFETCH_SITES) == ec::PREFETCH_SITES);
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, false, true);
    CHECK((ec::written & ec::PREFETCH_SITES) == 0 && (ec::written & ec::EMITTER_PREFETCH_BIT) == ec::EMITTER_PREFETCH_BIT);
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
    std::printf("16f. prefetch rows: the effect row alone writes the child sum and the particle loop, the emitter row alone the emitter loop\n");
  }

  // 17. More float4 reloads (Faster Game CPU Code): the soldier draw's matrix rows and the matrix product (every build has both).
  twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
  if (const int failed = RunSoldierRowsTests(build, game, patched, quick); failed != 0) return failed;
  if (const int failed = RunMatrixProductTests(build, game, patched, quick); failed != 0) return failed;
  twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);

  // 18. The particle writer's table entry prefetch (Faster Particle Prefetch).
  if (build.code->entry_fetch_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true);
    if (const int failed = RunEntryTests(build, game, patched, quick); failed != 0) return failed;
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true);
  } else {
    CHECK(ec::sites[ec::ENTRY_PREFETCH_INDEX].at == nullptr);
    std::printf("18. particle writer table entry prefetch: none in this build\n");
  }

  // 19. The render lists' item push loops (Faster Game CPU Code).
  if (build.code->push_loop_a_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true);
    if (const int failed = RunPushTests(build, game, patched, quick); failed != 0) return failed;
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true);
  } else {
    CHECK(ec::sites[ec::ITEM_PUSH_FIRST_SITE].at == nullptr && ec::sites[ec::ITEM_PUSH_FIRST_SITE + 1].at == nullptr);
    std::printf("19. item push loops: none in this build\n");
  }

  // 20. The pending list sweep's chain prefetch (Faster Sweep Prefetch).
  if (build.code->sweep_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true, true);
    if (const int failed = RunSweepTests(build, game, patched, quick); failed != 0) return failed;
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, true, true);
  } else {
    CHECK(ec::sites[ec::SWEEP_PREFETCH_INDEX].at == nullptr);
    std::printf("20. sweep chain prefetch: none in this build\n");
  }

  // Off: the game's bytes back (the harness's jumps removed first).
  std::memcpy(game + build.truncation_resume_rva, file.data() + (build.truncation_resume_rva - text_rva + text_header->PointerToRawData), 5);
  std::memcpy(patched + build.truncation_resume_rva, unpatched.data() + (build.truncation_resume_rva - text_rva), 5);
  twattila::engine_code::Update(false);
  CHECK(ec::written == 0);
  CHECK(std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
  twattila::engine_code::Uninstall();
  std::printf("off: the game's code back byte for byte\n");

  // 22. Model Detail Distance: the LOD pick's site and stub (lod_pick.inc; the row is asked for by the slider, so it is off in the parts before and after).
  // After the Off block: its Prepare calls look for the truncation site again, which the harness's jump at the truncation's resume point hid.
  if (const int failed = RunLodTests(build, game, patched, text_rva, text_size, image_size, quick); failed != 0) return failed;

  // 23. 24. The worker sites (Animation Rows on Workers, the Pending Sweep trace): the cut stubs, the cut rule, the pin, the pass-through and the trace-only Arm.
  if (const int failed = RunAnimationRowsTests(build, game, patched, quick); failed != 0) return failed;
  if (const int failed = RunPendingSweepTests(build, game, patched, quick); failed != 0) return failed;
  if (const int failed = RunEffectSitesTests(build, game, patched, text_rva, text_size, quick); failed != 0) return failed;
  if (const int failed = RunEffectUpdateTests(build, game, patched, quick); failed != 0) return failed;
  if (const int failed = RunParticleCountTests(build, game, patched, text_rva, text_size, quick); failed != 0) return failed;
  if (const int failed = RunInstanceFillTests(build, game, patched, text_rva, text_size); failed != 0) return failed;
  if (const int failed = RunExitDrainTests(build, game, patched, text_rva, text_size); failed != 0) return failed;

  // 13. Another mod's bytes in a site after the start: that site is not touched, nothing that goes in on the strength of it is
  // (the three job sites; the half rows after the half function), the rest is, and the site is left alone from then on.
  {
    const auto foreign = [&](size_t index, const std::array<uint8_t, ec::SITE_SIZE>& bytes) {
      DWORD protection;
      VirtualProtect(ec::sites[index].at, ec::SITE_SIZE, PAGE_EXECUTE_READWRITE, &protection);
      std::memcpy(ec::sites[index].at, bytes.data(), ec::SITE_SIZE);
      VirtualProtect(ec::sites[index].at, ec::SITE_SIZE, protection, &protection);
    };
    const auto holds = [&](size_t index, const std::array<uint8_t, ec::SITE_SIZE>& bytes) { return std::memcmp(ec::sites[index].at, bytes.data(), ec::SITE_SIZE) == 0; };
    const auto prepare_again = [&] {
      for (ec::Site& site : ec::sites) site.at = nullptr;
      ec::prepared = false;
      ec::written = 0;
      ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
    };
    const std::array<uint8_t, ec::SITE_SIZE> hook = {0xE9, 0x10, 0x20, 0x30, 0x40};
    const uint32_t all = ec::found;
    const auto on = [&] {
      twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, true, true, true, true, true, true, 2.f, 0,
                                    {.animation = twattila::animation_rows::Stage::ON, .pending_sweep_trace = true, .particle_count = true});
    };
    const auto off = [&] { twattila::engine_code::Update(false); };
    const auto game_bytes = [&](uint32_t mask) {  // every site of `mask` holds the game's bytes
      for (size_t index = 0; index < ec::sites.size(); ++index) {
        if ((mask >> index & 1) != 0 && !holds(index, ec::sites[index].game)) return false;
      }
      return true;
    };
    if (build.code->job_wake_all_rva != 0) {
      // (a) the job sleeps changed before the jobs go in
      foreign(6, hook);
      on();
      CHECK(ec::found == (all & ~ec::JOB_SITES) && ec::written == (ec::found & ~ec::EXIT_DRAIN_BIT) && holds(6, hook) && game_bytes(ec::JOB_SITES & ~0b01000000u));
      CHECK(log_lines.back().find("left alone") != std::string::npos && log_lines.back().find("job stops") != std::string::npos);
      foreign(6, ec::sites[6].game);
      off();
      CHECK(ec::written == 0 && game_bytes(all));
      prepare_again();
      // (b) the job destructor's call of Stop changed while the jobs are on: switching off leaves it (a foreign call), restores the other two
      on();
      CHECK(ec::written == all);
      foreign(7, hook);
      off();
      CHECK(ec::written == 0 && holds(7, hook) && game_bytes(all & ~0b10000000u) && (ec::found & 0b10000000u) == 0);
      foreign(7, ec::sites[7].game);
      // switched on again with the destructor's call out of found: the other two job sites do not go in alone
      on();
      CHECK((ec::written & ec::JOB_SITES) == 0 && game_bytes(ec::JOB_SITES));
      off();
      CHECK(ec::written == 0 && game_bytes(all));
      prepare_again();
    }
    if (build.code->half_rows_rva != 0) {
      // (c) the half function changed before the half rows go in
      foreign(1, hook);
      on();
      CHECK((ec::found & (ec::HALF_ROWS_BIT | ec::HALF_FUNCTION_BIT)) == 0 && (ec::written & (ec::HALF_ROWS_BIT | ec::HALF_FUNCTION_BIT)) == 0
            && ec::written == (ec::found & ~ec::EXIT_DRAIN_BIT) && holds(1, hook));
      foreign(1, ec::sites[1].game);
      off();
      CHECK(ec::written == 0 && game_bytes(all));
      prepare_again();
      // the half function changed while the half rows are on: switching off leaves it (a foreign hook) and takes the half rows out; switched on again the half rows do not go in alone
      on();
      CHECK((ec::written & ec::HALF_ROWS_BIT) != 0 && ec::written == all);
      foreign(1, hook);
      off();
      CHECK(ec::written == 0 && holds(1, hook) && game_bytes(all & ~ec::HALF_FUNCTION_BIT) && (ec::found & ec::HALF_FUNCTION_BIT) == 0);
      on();
      CHECK((ec::written & ec::HALF_ROWS_BIT) == 0 && holds(1, hook) && game_bytes(ec::HALF_ROWS_BIT));
      foreign(1, ec::sites[1].game);
      off();
      CHECK(ec::written == 0 && game_bytes(all));
      prepare_again();
    }
    if (build.code->particle_count_rva != 0) {
      // (n) Cached Particle Count's pair: the epoch changed before the row goes in: the sum does not go in either (it would answer the first value for the session); our sum's jump
      // changed while the row is on: switching off leaves it, and the epoch's jump stays in with it (its bump keeps the cache honest for a hook that chains to our sum's stub)
      constexpr size_t EPOCH = ec::PARTICLE_EPOCH_INDEX;
      constexpr size_t COUNT = ec::PARTICLE_COUNT_INDEX;
      foreign(EPOCH, hook);
      on();
      CHECK((ec::found & ec::PARTICLE_COUNT_SITES) == 0 && (ec::written & ec::PARTICLE_COUNT_SITES) == 0 && ec::written == (ec::found & ~ec::EXIT_DRAIN_BIT) && holds(EPOCH, hook)
            && game_bytes(ec::PARTICLE_COUNT_BIT));
      foreign(EPOCH, ec::sites[EPOCH].game);
      off();
      CHECK(ec::written == 0 && game_bytes(all));
      prepare_again();
      foreign(COUNT, hook);  // the sum's bytes changed before the row goes in
      on();
      CHECK((ec::found & ec::PARTICLE_COUNT_SITES) == 0 && (ec::written & ec::PARTICLE_COUNT_SITES) == 0 && ec::written == (ec::found & ~ec::EXIT_DRAIN_BIT) && holds(COUNT, hook)
            && game_bytes(ec::PARTICLE_EPOCH_BIT));
      foreign(COUNT, ec::sites[COUNT].game);
      off();
      CHECK(ec::written == 0 && game_bytes(all));
      prepare_again();
      on();
      CHECK(ec::written == all && holds(EPOCH, ec::sites[EPOCH].jump) && holds(COUNT, ec::sites[COUNT].jump));
      foreign(COUNT, hook);
      off();
      // like every site that stands down on the way out, both count as another mod's now (written 0, found without them); the epoch's jump is still in the game's code
      CHECK(ec::written == 0 && (ec::found & ec::PARTICLE_COUNT_SITES) == 0 && holds(COUNT, hook) && holds(EPOCH, ec::sites[EPOCH].jump) && game_bytes(all & ~ec::PARTICLE_COUNT_SITES));
      foreign(COUNT, ec::sites[COUNT].game);
      foreign(EPOCH, ec::sites[EPOCH].game);
      prepare_again();
      // the epoch's jump overwritten (the game's bytes back, no chain) while the row is on: switching off leaves the epoch out of found, and switching on again does not put the
      // sum in alone (it would answer the first value of each node for the session)
      on();
      CHECK(ec::written == all);
      foreign(EPOCH, ec::sites[EPOCH].game);
      off();
      CHECK(ec::written == 0 && (ec::found & ec::PARTICLE_COUNT_SITES) == ec::PARTICLE_COUNT_BIT && game_bytes(all));
      on();
      CHECK((ec::written & ec::PARTICLE_COUNT_SITES) == 0 && game_bytes(ec::PARTICLE_COUNT_SITES));
      off();
      CHECK(ec::written == 0 && game_bytes(all));
      prepare_again();
    }
    // (d) the soldier rounding site changed while the rows are on: switching off leaves it, restores the others
    on();
    CHECK(ec::written == all);
    foreign(0, hook);
    off();
    CHECK(ec::written == 0 && holds(0, hook) && game_bytes(all & ~1u));
    foreign(0, ec::sites[0].game);
    prepare_again();
    if (build.code->float4_reload_rva != 0) {
      // (e) one of the particle writer's reloads changed while the rows are on (the colour's, then the vector's): switching off leaves it,
      // restores the other reload and the rest
      constexpr size_t COLOUR = ec::FLOAT4_RELOAD_FIRST_SITE;
      constexpr size_t VECTOR = ec::FLOAT4_RELOAD_FIRST_SITE + 1;
      for (const size_t changed : {COLOUR, VECTOR}) {
        on();
        CHECK(ec::written == all);
        foreign(changed, hook);
        off();
        CHECK(ec::written == 0 && holds(changed, hook) && game_bytes(all & ~(1u << changed)) && (ec::found & (1u << changed)) == 0);
        foreign(changed, ec::sites[changed].game);
        prepare_again();
      }
      // (f) without SSE4.1 (insertps) neither reload is looked for, and none is named as left alone
      ec::sse4_1 = false;
      prepare_again();
      CHECK(ec::sites[COLOUR].at == nullptr && ec::sites[VECTOR].at == nullptr && log_lines.back().find("particle") == std::string::npos);
      CHECK(ec::sites[ec::CHILD_SUM_INDEX].at != nullptr && (build.code->particle_loop_rva == 0 || ec::sites[ec::PARTICLE_PREFETCH_INDEX].at != nullptr)
            && (build.code->emitter_loop_rva == 0 || ec::sites[ec::EMITTER_PREFETCH_INDEX].at != nullptr));  // the prefetch sites need no SSE4.1
      CHECK((ec::found & (ec::SOLDIER_ROWS_SITES | ec::MATRIX_PRODUCT_SITES)) == 0 && log_lines.back().find("matrix") == std::string::npos);
      ec::sse4_1 = true;
      prepare_again();
      CHECK(ec::sites[COLOUR].at != nullptr && ec::sites[VECTOR].at != nullptr && (ec::found & ec::SOLDIER_ROWS_SITES) == ec::SOLDIER_ROWS_SITES
            && (ec::found & ec::MATRIX_PRODUCT_SITES) == ec::MATRIX_PRODUCT_SITES);
      // (g) one byte changed in the function's tail, outside the sites and in each site's first byte: both reloads left alone, named
      uint8_t* tail = patched + build.code->float4_reload_rva;
      for (const size_t at : {size_t{14}, ec::FLOAT4_RELOAD_AT[0], ec::FLOAT4_RELOAD_AT[1]}) {
        tail[at] ^= 1;
        prepare_again();
        CHECK(ec::sites[COLOUR].at == nullptr && ec::sites[VECTOR].at == nullptr && ec::sites[ec::CHILD_SUM_INDEX].at != nullptr);
        CHECK(log_lines.back().find("left alone (not the game's code as expected: another mod or build): ") != std::string::npos
              && log_lines.back().ends_with("particle colour reload, particle vector reload"));
        tail[at] ^= 1;
        prepare_again();
        CHECK(ec::sites[COLOUR].at != nullptr && ec::sites[VECTOR].at != nullptr && ec::found == all);
      }
    }
    if (build.code->particle_loop_rva != 0) {
      // (h) the particle loop's prefetch site changed while the rows are on: switching off leaves it, restores the others
      on();
      CHECK(ec::written == all);
      foreign(ec::PARTICLE_PREFETCH_INDEX, hook);
      off();
      CHECK(ec::written == 0 && holds(ec::PARTICLE_PREFETCH_INDEX, hook) && game_bytes(all & ~ec::PARTICLE_PREFETCH_BIT)
            && (ec::found & ec::PARTICLE_PREFETCH_BIT) == 0);
      // The site was named when it stood down; a present with the row on does not name it again as not the game's code.
      const size_t logged_before = log_lines.size();
      on();
      CHECK(std::none_of(log_lines.begin() + logged_before, log_lines.end(),
                         [](const std::string& line) { return line.find("particle prefetch") != std::string::npos; }));
      off();
      foreign(ec::PARTICLE_PREFETCH_INDEX, ec::sites[ec::PARTICLE_PREFETCH_INDEX].game);
      prepare_again();
      // (i) one byte changed in the loop outside the site: not found (the child sum and the rest are), the row's warning names it
      uint8_t* loop = patched + build.code->particle_loop_rva;
      loop[10] ^= 1;
      prepare_again();
      CHECK(ec::sites[ec::PARTICLE_PREFETCH_INDEX].at == nullptr && ec::sites[ec::CHILD_SUM_INDEX].at != nullptr && ec::sites[ec::FLOAT4_RELOAD_FIRST_SITE].at != nullptr);
      ec::prefetch_logged = 0;
      on();
      CHECK(ec::written == (ec::found & ~ec::EXIT_DRAIN_BIT) && (ec::written & ec::PARTICLE_PREFETCH_BIT) == 0);
      CHECK(std::ranges::any_of(log_lines, [](const std::string& line) { return line.ends_with("another mod or build): particle prefetch"); }));
      off();
      loop[10] ^= 1;
      prepare_again();
      CHECK(ec::sites[ec::PARTICLE_PREFETCH_INDEX].at != nullptr && ec::found == all);
    }
    if (build.code->emitter_loop_rva != 0) {
      // (j) the emitter loop's prefetch site changed while the rows are on: switching off leaves it, restores the others
      on();
      CHECK(ec::written == all);
      foreign(ec::EMITTER_PREFETCH_INDEX, hook);
      off();
      CHECK(ec::written == 0 && holds(ec::EMITTER_PREFETCH_INDEX, hook) && game_bytes(all & ~ec::EMITTER_PREFETCH_BIT)
            && (ec::found & ec::EMITTER_PREFETCH_BIT) == 0);
      const size_t logged_before = log_lines.size();  // as for the particle loop's site: named once, when it stood down
      on();
      CHECK(std::none_of(log_lines.begin() + logged_before, log_lines.end(),
                         [](const std::string& line) { return line.find("emitter prefetch") != std::string::npos; }));
      off();
      foreign(ec::EMITTER_PREFETCH_INDEX, ec::sites[ec::EMITTER_PREFETCH_INDEX].game);
      prepare_again();
      // (k) one byte changed in the function outside the site: not found (the others are), the row's warning names it
      uint8_t* function = patched + build.code->emitter_loop_rva;
      function[12] ^= 1;
      prepare_again();
      CHECK(ec::sites[ec::EMITTER_PREFETCH_INDEX].at == nullptr && ec::sites[ec::CHILD_SUM_INDEX].at != nullptr && ec::sites[ec::PARTICLE_PREFETCH_INDEX].at != nullptr);
      ec::prefetch_logged = 0;
      on();
      CHECK(ec::written == (ec::found & ~ec::EXIT_DRAIN_BIT) && (ec::written & ec::EMITTER_PREFETCH_BIT) == 0);
      CHECK(std::ranges::any_of(log_lines, [](const std::string& line) { return line.ends_with("another mod or build): emitter prefetch"); }));
      off();
      function[12] ^= 1;
      prepare_again();
      CHECK(ec::sites[ec::EMITTER_PREFETCH_INDEX].at != nullptr && ec::found == all);
    }
    if (build.code->child_sum_rva != 0) {
      // (o) one byte changed in the child sum (outside its site, in the site, in the jne after it; the pattern is the whole function): not found
      // (the others are), the row's warning names it
      uint8_t* sum = patched + build.code->child_sum_rva;
      for (const size_t changed : {size_t{10}, ec::CHILD_SUM_SITE, ec::CHILD_SUM_RESUME}) {
        sum[changed] ^= 1;
        prepare_again();
        CHECK(ec::sites[ec::CHILD_SUM_INDEX].at == nullptr && ec::found == (all & ~ec::CHILD_SUM_BIT));
        ec::prefetch_logged = 0;
        const size_t logged_before = log_lines.size();
        on();
        CHECK(ec::written == (ec::found & ~ec::EXIT_DRAIN_BIT) && (ec::written & ec::CHILD_SUM_BIT) == 0);
        CHECK(std::any_of(log_lines.begin() + logged_before, log_lines.end(),
                          [](const std::string& line) { return line.ends_with("another mod or build): child prefetch"); }));
        off();
        sum[changed] ^= 1;
        prepare_again();
        CHECK(ec::sites[ec::CHILD_SUM_INDEX].at != nullptr && ec::found == all);
      }
    }
    // (l) each of the soldier draw's and the matrix product's row reloads changed while the rows are on: switching off leaves it, restores the
    // other five and the rest
    for (size_t changed = ec::SOLDIER_ROWS_FIRST_SITE; changed < ec::MATRIX_PRODUCT_FIRST_SITE + 3; ++changed) {
      on();
      CHECK(ec::written == all);
      foreign(changed, hook);
      off();
      CHECK(ec::written == 0 && holds(changed, hook) && game_bytes(all & ~(1u << changed)) && (ec::found & (1u << changed)) == 0);
      foreign(changed, ec::sites[changed].game);
      prepare_again();
    }
    // (m) one byte changed in the rows' pattern (its first, a byte between the stores, each site's first and last, the jump after them):
    // the three left alone, named; without SSE4.1 not looked for (part (f))
    {
      uint8_t* rows = patched + build.code->soldier_rows_rva;
      const size_t size = (build.code->soldier_rows.size() + 1) / 3;
      const auto& at = build.code->soldier_rows_at;
      for (const size_t changed : {size_t{0}, size_t{30}, size_t{at[0]}, size_t{at[0] + 7u}, size_t{at[1]}, size_t{at[2]}, size_t{at[2] + 7u}, size - 4}) {
        rows[changed] ^= 1;
        prepare_again();
        CHECK((ec::found & ec::SOLDIER_ROWS_SITES) == 0 && ec::sites[0].at != nullptr);
        CHECK(log_lines.back().find("left alone (not the game's code as expected: another mod or build): ") != std::string::npos
              && log_lines.back().find("soldier matrix row 1, soldier matrix row 2, soldier matrix row 3") != std::string::npos);
        rows[changed] ^= 1;
        prepare_again();
        CHECK(ec::found == all);
      }
    }
    // (n) one byte changed in the matrix product (its first, one between the stores, each site's first and last, its ret): the three left
    // alone, named
    {
      uint8_t* product = patched + build.code->matrix_product_rva;
      const size_t size = (ec::MATRIX_PRODUCT.size() + 1) / 3;
      const auto& at = ec::MATRIX_PRODUCT_AT;
      for (const size_t changed : {size_t{0}, size_t{0x100}, at[0], at[0] + 4, at[1], at[2], at[2] + 4, size - 3}) {
        product[changed] ^= 1;
        prepare_again();
        CHECK((ec::found & ec::MATRIX_PRODUCT_SITES) == 0 && (ec::found & ec::SOLDIER_ROWS_SITES) == ec::SOLDIER_ROWS_SITES);
        CHECK(log_lines.back().find("left alone (not the game's code as expected: another mod or build): ") != std::string::npos
              && log_lines.back().find("matrix product row 1, matrix product row 2, matrix product row 3") != std::string::npos);
        product[changed] ^= 1;
        prepare_again();
        CHECK(ec::found == all);
      }
    }
    std::printf("13. another mod's bytes in a site: left alone, the sites that depend on it stay out, the rest as asked\n");
  }

  // 7. With the Attila CPU Performance Fix's x87 fix the truncation block holds fisttp and nops for both fldcw (bytes
  // 23..26, 28 and 31..34 of the block, ATN's heapfix.log: 1277 of 1277 blocks, 165 by fisttp): that site is left
  // alone, the others still found, Attila's log naming the fix. Then one byte of the ready check changed (as another mod's
  // patch would): left alone.
  uint8_t* block = patched + build.truncation_block_rva;
  for (const size_t at : {23, 31}) std::memcpy(block + at, "\x0F\x1F\x40\x00", 4);
  block[28] = 0x4C;
  for (ec::Site& site : ec::sites) site.at = nullptr;
  ec::prepared = false;
  ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
  CHECK(ec::sites[0].at == nullptr && ec::sites[1].at != nullptr && ec::sites[2].at != nullptr);
  CHECK((ec::found & ec::SOLDIER_ROWS_SITES) == ec::SOLDIER_ROWS_SITES);  // the same function, clear of the fix's block
  CHECK((ec::found & ec::MATRIX_PRODUCT_SITES) == ec::MATRIX_PRODUCT_SITES);
  CHECK((ec::sites[3].at != nullptr) == (build.list_push_function_rva != 0));
  CHECK((ec::sites[4].at != nullptr) == (build.ready_check_rva != 0));
  // The reason: Attila's names the fix (its block at its RVA), the other games' builds the unknown mod.
  const std::string_view UNKNOWN = "; left alone (not the game's code as expected: another mod or build): ";
  const std::string_view X87_FIX = "; left alone (the Attila CPU Performance Fix's x87 fix rewrote it): ";
  const bool attila = (build.code->truncation_rva == ec::TRUNCATION_RVA);
  const std::string first_line = log_lines.back();
  CHECK(first_line.ends_with(std::format("{}soldier draw rounding", attila ? X87_FIX : UNKNOWN)));
  CHECK(first_line.find(attila ? UNKNOWN : X87_FIX) == std::string::npos);
  std::string control_line;
  if (attila) {
    // CONTROL: one byte more changed (fistp back, the nops kept: not the fix's form): the unknown mod.
    block[28] = 0x5C;
    for (ec::Site& site : ec::sites) site.at = nullptr;
    ec::prepared = false;
    ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
    control_line = log_lines.back();
    CHECK(ec::sites[0].at == nullptr && control_line.ends_with(std::format("{}soldier draw rounding", UNKNOWN)) && control_line.find(X87_FIX) == std::string::npos);
    block[28] = 0x4C;
  }
  std::string ready_line;
  uint8_t ready_saved = 0;
  if (build.ready_check_rva != 0) {
    ready_saved = patched[build.ready_check_rva + 0x1F];
    patched[build.ready_check_rva + 0x1F] = 0x90;  // the copy's lock prefix
    for (ec::Site& site : ec::sites) site.at = nullptr;
    ec::prepared = false;
    ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
    ready_line = log_lines.back();
    CHECK(ec::sites[4].at == nullptr && ec::sites[1].at != nullptr);
    CHECK(ready_line.ends_with(attila ? std::format("{}ready checks{}soldier draw rounding", UNKNOWN, X87_FIX)
                                      : std::format("{}soldier draw rounding, ready checks", UNKNOWN)));
  } else {
    CHECK(log_lines.back().find("ready checks") == std::string::npos);
  }
  // 7b. The later builds of the Attila CPU Performance Fix (the control word reload split, bytes 5..34 of the block: movzx,
  // fstcw, shl, mov ax, or, mov, fisttp; ATN's real log "165 of the fisttp blocks with the control word reload split") leave
  // the block in another form: named as the fix too, with the ready check back. CONTROL: one byte of it changed (the movzx's
  // displacement): the unknown mod.
  std::string split_line;
  std::string split_control_line;
  std::string x87c_line;
  std::string x87c_control_line;
  if (attila) {
    if (build.ready_check_rva != 0) patched[build.ready_check_rva + 0x1F] = ready_saved;
    const std::array<uint8_t, 30> split = {0x0F, 0xB7, 0x44, 0x24, 0x1A, 0xD9, 0x7C, 0x24, 0x18, 0xC1, 0xE0, 0x10, 0x66, 0x8B, 0x44,
                                           0x24, 0x18, 0x0D, 0x00, 0x0C, 0x00, 0x00, 0x89, 0x44, 0x24, 0x1C, 0xDB, 0x4C, 0x24, 0x2C};
    std::memcpy(block + 5, split.data(), split.size());
    for (ec::Site& site : ec::sites) site.at = nullptr;
    ec::prepared = false;
    ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
    split_line = log_lines.back();
    CHECK(ec::sites[0].at == nullptr && ec::sites[1].at != nullptr && ec::sites[2].at != nullptr);
    CHECK(split_line.ends_with(std::format("{}soldier draw rounding", X87_FIX)) && split_line.find(UNKNOWN) == std::string::npos);
    block[5 + 4] ^= 1;
    for (ec::Site& site : ec::sites) site.at = nullptr;
    ec::prepared = false;
    ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
    split_control_line = log_lines.back();
    CHECK(ec::sites[0].at == nullptr && split_control_line.ends_with(std::format("{}soldier draw rounding", UNKNOWN))
          && split_control_line.find(X87_FIX) == std::string::npos);
    block[5 + 4] ^= 1;
    // 7c. heapfix's future x87c: the whole 36-byte block (push eax; cvttss2si eax, [esp + 0x20]; mov [esp + 0x2c], eax; pop eax;
    // jmp +0x16; 22 int3). CONTROL: its jump's displacement changed.
    std::array<uint8_t, 36> x87c = {0x50, 0xF3, 0x0F, 0x2C, 0x44, 0x24, 0x20, 0x89, 0x44, 0x24, 0x2C, 0x58, 0xEB, 0x16};
    std::fill(x87c.begin() + 14, x87c.end(), uint8_t{0xCC});
    std::memcpy(block, x87c.data(), x87c.size());
    for (ec::Site& site : ec::sites) site.at = nullptr;
    ec::prepared = false;
    ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
    x87c_line = log_lines.back();
    CHECK(ec::sites[0].at == nullptr && ec::sites[1].at != nullptr && ec::sites[2].at != nullptr);
    CHECK(x87c_line.ends_with(std::format("{}soldier draw rounding", X87_FIX)) && x87c_line.find(UNKNOWN) == std::string::npos);
    block[13] ^= 1;
    for (ec::Site& site : ec::sites) site.at = nullptr;
    ec::prepared = false;
    ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
    x87c_control_line = log_lines.back();
    CHECK(ec::sites[0].at == nullptr && x87c_control_line.ends_with(std::format("{}soldier draw rounding", UNKNOWN))
          && x87c_control_line.find(X87_FIX) == std::string::npos);
  }
  std::printf("7. a block another mod changed: left alone, the others found%s\n  log: %s\n", (build.ready_check_rva != 0 ? "; a changed ready check left alone" : ""),
              first_line.c_str());
  if (!split_line.empty()) std::printf("  log (the fix's split form): %s\n  log (CONTROL, one byte of it changed): %s\n", split_line.c_str(), split_control_line.c_str());
  if (!x87c_line.empty()) std::printf("  log (the fix's x87c form): %s\n  log (CONTROL, its jump changed): %s\n", x87c_line.c_str(), x87c_control_line.c_str());
  if (!control_line.empty()) std::printf("  log (CONTROL, not the fix's form): %s\n", control_line.c_str());
  if (!ready_line.empty()) std::printf("  log: %s\n", ready_line.c_str());
  std::printf("PASS\n");
  return 0;
}

#endif
