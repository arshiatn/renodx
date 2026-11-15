// Faster Game CPU Code (src/games/twattila/engine_code.hpp) against the game's own code. empire.retail.dll is read
// from disk and mapped twice (relocated, never loaded as a DLL: no DllMain, no imports), copy A as the game ships it,
// copy B patched by engine_code (Prepare and Update, as the addon does it). Then each routine runs in both with the
// same inputs and every register, the stack and the x87 state are compared:
// 1. Each pattern exactly once in A; B differs from A in the 5-byte sites only; back to A's bytes after off.
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
// 6. Timing, A against B: float to half on sprite-like rows, the truncation, fmod(x, 1).
// 7. The truncation block as the Attila CPU Performance Fix leaves it: left alone, the other two still found.
// 8. The render lists' push (+0x176ced0, Faster Render Lists): 1 M random pushes (thread, vector sizes, the "has items"
//    byte, the list's other bytes, the item, every register): the registers, esp and every byte of the list, its
//    vectors and their items equal; on and off 200 times while 4 threads push to one list: every item in its place,
//    every return the game's; timing with 4 threads pushing to 16 lists (the byte cleared each round, as the merge does).
// 9. The render lists' sort (the call to std::sort<uint32_t> at +0x17b9217, Faster Game CPU Code): the stub against the
//    game's sort on arrays of 0 to 300000 values of 9 kinds (around 128 and 262144, where the stub hands over) and 20000
//    random ones, then with every scratch slot taken: the array, ebx esi edi ebp and esp equal; 20000 random lists
//    through the merge-then-sort function (+0x17b91f0) in both copies: every byte, eax ebx esi edi ebp esp equal; 12
//    threads through the stub at once; on and off 200 times while 4 threads merge and sort; timing.
// 10. The instance gather's prefetch (+0x182c990, Faster Game CPU Code): 20000 random draws (ranges, strides, the items'
//    pointers ending where a page without access starts) in both copies: every register, esp and destination byte
//    equal; on and off 200 times while 4 threads gather; timing with cold sources.
// 11. The sort or its caller not at its RVA: the sort's site left alone with a log line, no scratch (no search of the
//    image for another prologue or another of the seven alike callers).
// Thrones of Britannia's and Rome II's DLLs the same way (engine_code's BUILDS, each picked by its truncation's pattern):
// their RVAs, their CRT's state, their truncation's stack slots.
// tests.exe [empire.retail.dll] [quick]  (quick: 2^28 instead of 2^32 in 2 and 3)
#include <windows.h>
#include <xmmintrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "src/games/twattila/engine_code.hpp"

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
  uint32_t sort_rva;                // std::sort<uint32_t>; 0: its render list caller isn't in this build
  uint32_t sort_caller_rva;
  uint32_t gather_rva;  // the instance gather; 0: none in this build
};
constexpr TestBuild ATTILA = {"Attila", &twattila::engine_code::internal::ATTILA_BUILD, 0x169a220, 0x17f6515, 0x17f6539, 0x1a05cc0,
                              0x1dae4f4, 0x2a8b504, 0x2a8b4cc, 0x1c, 0x28, 0x176ced0, 0xda6b0, 0x17b91f0, 0x182c990};
constexpr TestBuild THRONES = {"Thrones of Britannia", &twattila::engine_code::internal::THRONES_BUILD, 0x1707eb0, 0x186aa48, 0x186aa6c,
                               0x1b0a030, 0x1eec634, 0x25fde74, 0x25fde3c, 0x20, 0x18, 0x17defc0, 0x220850, 0x182cf80, 0x18a13e0};
constexpr TestBuild ROME2 = {"Rome II", &twattila::engine_code::internal::ROME2_BUILD, 0x1368030, 0x14b434a, 0x14b436e, 0x169ca30,
                             0x19ebd74, 0x21263cc, 0x2126394, 0x1c, 0x20, 0, 0, 0, 0};

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

// std::sort<uint32_t>'s cdecl call (first, last, ideal, pred): ebx esi edi ebp set, every register out after; esp as
// the callee left it (cdecl: at the arguments).
struct SortCall {
  uint32_t in[4];       // 0: ebx esi edi ebp
  uint32_t function;    // 16
  uint32_t args[4];     // 20: first last ideal pred
  uint32_t out[7];      // 36: eax ecx edx ebx esi edi ebp
  uint32_t esp_before;  // 64: the arguments' slot
  uint32_t esp_after;   // 68
};

__declspec(naked) void __cdecl CallSort(SortCall*) {
  __asm {
    pushad
    mov ebp, [esp + 36]
    push ebp
    push dword ptr [ebp + 32]
    push dword ptr [ebp + 28]
    push dword ptr [ebp + 24]
    push dword ptr [ebp + 20]
    call set_up
    push eax
    mov eax, [esp + 20]
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
    add esp, 20
    popad
    ret
  set_up:
    lea eax, [esp + 4]
    mov [ebp + 64], eax
    push dword ptr [ebp + 16]
    mov ebx, [ebp + 0]
    mov esi, [ebp + 4]
    mov edi, [ebp + 8]
    mov ebp, [ebp + 12]
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

// One render list for the merge-then-sort function (+0x17b91f0), as the game lays it out: the merged vector {capacity,
// size, data} at +4, the threads' vectors' {capacity, count, data} at +0x10, the "has items" byte at +0x1c. The merged
// vector has room for everything (the merge then never allocates: the game's allocator isn't started in a mapped copy).
struct SortList {
  std::array<uint32_t, 8> list = {};
  std::vector<uint32_t> headers;
  std::vector<std::vector<uint32_t>> threads;
  std::vector<uint32_t> merged;
};

// Values sorted by the game: 0 random, 1 pointers to 16-byte heap slots, 2 few values, 3 sorted, 4 reversed, 5 all equal,
// 6 the sign bit with small values, 7 the second byte only, 8 the top byte only.
uint32_t SortValue(int kind, uint32_t index, uint32_t count, Random* random) {
  switch (kind) {
    case 0:  return random->Next() ^ (random->Next() << 16);
    case 1:  return 0x0A000000u + (random->Next() % (1u << 22)) * 16u;
    case 2:  return random->Next() % 16;
    case 3:  return index * 7;
    case 4:  return (count - index) * 7;
    case 5:  return 0x12345678u;
    case 6:  return ((random->Next() & 1) != 0 ? 0x80000000u : 0u) | (random->Next() % 1000);
    case 7:  return 0x11220033u | ((random->Next() & 0xFF) << 8);
    default: return (random->Next() & 0xFF) << 24;
  }
}

SortList MakeSortList(uint64_t seed, uint32_t max_per_thread) {
  Random random = {.state = seed};
  SortList list;
  const uint32_t threads = 1 + random.Next() % 16;
  const int kind = static_cast<int>(random.Next() % 9);
  list.headers.resize(threads * 3);
  list.threads.resize(threads);
  uint32_t total = random.Next() % 64;  // already merged
  std::vector<uint32_t> counts(threads);
  for (uint32_t& count : counts) {
    count = ((random.Next() & 3) == 0 ? 0 : random.Next() % (max_per_thread + 1));
    total += count;
  }
  list.merged.resize(total + 16);
  const uint32_t merged = total - std::accumulate(counts.begin(), counts.end(), 0u);
  for (uint32_t index = 0; index < list.merged.size(); ++index) list.merged[index] = (index < merged ? SortValue(kind, index, total, &random) : 0xCDCDCDCDu);
  uint32_t index = merged;
  for (uint32_t thread = 0; thread < threads; ++thread) {
    list.threads[thread].resize(counts[thread] + random.Next() % 8);
    for (uint32_t item = 0; item < counts[thread]; ++item) list.threads[thread][item] = SortValue(kind, index++, total, &random);
    list.headers[thread * 3] = static_cast<uint32_t>(list.threads[thread].size());
    list.headers[thread * 3 + 1] = counts[thread];
    list.headers[thread * 3 + 2] = (list.threads[thread].empty() ? 0u : Address(list.threads[thread].data()));
  }
  list.list = {random.Next(), static_cast<uint32_t>(list.merged.size()), merged, Address(list.merged.data()), threads, threads,
               Address(list.headers.data()), (random.Next() % 8 == 0 ? 0u : 1u) | (random.Next() & 0xFFFFFF00u)};
  return list;
}

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
  if (build.sort_rva != 0) {
    CHECK(Matches(game + text_rva, text_size, ec::SORT) == 1);
    CHECK(Matches(game + text_rva, text_size, ec::SORT_CALLER) == 7);  // seven kinds of list, each its own sort
  }
  CHECK(Matches(game + text_rva, text_size, ec::GATHER) == (build.gather_rva != 0 ? 1u : 0u));
  // B's own code as mapped (A's differs in every relocated address).
  const std::vector<uint8_t> unpatched(patched + text_rva, patched + text_rva + text_size);
  ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
  CHECK(ec::sites[0].at == patched + build.truncation_block_rva);
  CHECK(ec::sites[1].at == patched + build.half_rva);
  CHECK(ec::sites[2].at == patched + build.fmod_rva);
  CHECK(ec::sites[3].at == (build.list_push_function_rva != 0 ? patched + build.code->list_push_rva + ec::LIST_PUSH_SITE : nullptr));
  CHECK(ec::sites[4].at == (build.sort_rva != 0 ? patched + build.sort_caller_rva + ec::SORT_CALLER_SITE : nullptr));
  CHECK(ec::sites[5].at == (build.gather_rva != 0 ? patched + build.gather_rva + ec::GATHER_SITE : nullptr));
  CHECK(std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
  twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, true);
  const uint32_t expected_sites = 0b000111u | (build.list_push_function_rva != 0 ? 0b001000u : 0u) | (build.sort_rva != 0 ? 0b010000u : 0u)
                                  | (build.gather_rva != 0 ? 0b100000u : 0u);
  CHECK(ec::written == ec::found && ec::found == expected_sites);
  if (build.sort_rva != 0) CHECK(patched[build.sort_caller_rva + ec::SORT_CALLER_SITE] == 0xE8);  // still a call
  size_t differing = 0;
  for (uint32_t at = text_rva; at < text_rva + text_size; ++at) {
    if (unpatched[at - text_rva] == patched[at]) continue;
    ++differing;
    bool in_site = false;
    for (const ec::Site& site : ec::sites) in_site |= (patched + at >= site.at && patched + at < site.at + ec::SITE_SIZE);
    CHECK(in_site);
  }
  std::printf("1. patterns once each; %zu bytes differ, all in the %d sites\n", differing, std::popcount(ec::found));

  // The truncation's end jumps back to the harness in both copies (after Prepare: its pattern reads these bytes).
  JumpTo(game + build.truncation_resume_rva, reinterpret_cast<const void*>(&BlockReturn));
  JumpTo(patched + build.truncation_resume_rva, reinterpret_cast<const void*>(&BlockReturn));

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
  }

  // 8. The render lists' push.
  if (build.list_push_function_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, true);
    CHECK(ec::written == ec::found);
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

  // 9. The render lists' sort.
  if (build.sort_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, true);
    CHECK((ec::written & 0b10000u) != 0);
    const uint32_t game_sort = Address(game + build.sort_rva);
    const uint32_t stub = Address(ec::page + ec::SORT_PAGE);
    const auto start = std::chrono::steady_clock::now();
    // The stub against the game's sort: the array, ebx esi edi ebp and esp equal (eax ecx edx: the caller reads none).
    uint64_t sorts = 0;
    const auto compare = [&](std::vector<uint32_t> values, Random* random) {
      std::vector<uint32_t> ours = values;
      SortCall a = {.function = game_sort, .args = {Address(values.data()), Address(values.data() + values.size()),
                                                    static_cast<uint32_t>(values.size()), random->Next()}};
      for (uint32_t& in : a.in) in = random->Next();
      SortCall b = a;
      b.function = stub;
      b.args[0] = Address(ours.data());
      b.args[1] = Address(ours.data() + ours.size());
      CallSort(&a);
      CallSort(&b);
      ++sorts;
      return values == ours && std::memcmp(a.out + 3, b.out + 3, 16) == 0 && a.esp_after == a.esp_before && b.esp_after == b.esp_before;
    };
    Random random = {.state = 0x50F7};
    for (const uint32_t count : {0u, 1u, 2u, 31u, 32u, 33u, 127u, 128u, 129u, 255u, 256u, 1000u, 4096u, 65536u, 262143u, 262144u, 262145u, 300000u}) {
      for (int kind = 0; kind < 9; ++kind) {
        std::vector<uint32_t> values(count);
        for (uint32_t index = 0; index < count; ++index) values[index] = SortValue(kind, index, count, &random);
        if (!compare(values, &random)) {
          std::printf("  sort: %u values of kind %d differ\n", count, kind);
          CHECK(false);
        }
      }
    }
    for (int run = 0; run < 20000; ++run) {
      const uint32_t count = ((run & 7) == 0 ? random.Next() % 70000 : random.Next() % 5000);
      const int kind = static_cast<int>(random.Next() % 9);
      std::vector<uint32_t> values(count);
      for (uint32_t index = 0; index < count; ++index) values[index] = SortValue(kind, index, count, &random);
      CHECK(compare(values, &random));
    }
    // Every slot in use: the game's sort.
    auto* slots_in_use = reinterpret_cast<volatile uint32_t*>(ec::sort_scratch);
    CHECK(*slots_in_use == 0);
    *slots_in_use = 0xFF;
    for (int run = 0; run < 1000; ++run) {
      std::vector<uint32_t> values(128 + random.Next() % 5000);
      for (uint32_t& value : values) value = random.Next() ^ (random.Next() << 16);
      CHECK(compare(values, &random));
    }
    *slots_in_use = 0;
    std::printf("9. render list sort: %llu arrays (0 to 300000 values, 9 kinds; 1000 with every slot taken) sorted as the "
                "game's, ebx esi edi ebp esp equal (%.1f s)\n",
                sorts, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());

    // The merge-then-sort function in both copies (its call to the sort is the site): every byte of the list, its
    // vectors, eax ebx esi edi ebp and esp equal.
    const uint32_t game_caller = Address(game + build.sort_caller_rva);
    const uint32_t patched_caller = Address(patched + build.sort_caller_rva);
    for (int run = 0; run < 20000; ++run) {
      const uint64_t seed = 0xC0FFEEull + static_cast<uint64_t>(run);
      const uint32_t per_thread = ((run & 15) == 0 ? 20000u : 600u);
      SortList a_list = MakeSortList(seed, per_thread);
      SortList b_list = MakeSortList(seed, per_thread);
      RegisterCall a = {.function = game_caller, .argument = Address(a_list.list.data())};
      for (uint32_t& in : a.in) in = random.Next();
      RegisterCall b = a;
      b.function = patched_caller;
      b.argument = Address(b_list.list.data());
      CallWithRegisters(&a);
      CallWithRegisters(&b);
      bool same = a.out[0] == b.out[0] && std::memcmp(a.out + 3, b.out + 3, 16) == 0 && a.esp_after == a.esp_before + 4
                  && b.esp_after == b.esp_before + 4 && a_list.merged == b_list.merged && a_list.threads == b_list.threads;
      for (size_t at = 0; at < a_list.headers.size(); ++at) {
        const bool pointer = (at % 3 == 2);
        same &= (pointer ? (a_list.headers[at] != 0) == (b_list.headers[at] != 0) : a_list.headers[at] == b_list.headers[at]);
      }
      for (size_t at = 0; at < a_list.list.size(); ++at) {
        const bool pointer = (at == 3 || at == 6);
        same &= (pointer || a_list.list[at] == b_list.list[at]);
      }
      if (!same) {
        std::printf("  merge and sort, seed %llu: %u / %u values\n", seed, a_list.list[2], b_list.list[2]);
        CHECK(false);
      }
    }
    std::printf("   20000 lists through the merge-then-sort function (1 to 16 threads' vectors, up to 320000 values): "
                "every byte, eax ebx esi edi ebp esp equal\n");

    // 12 threads through the stub at once (more than its 8 slots): every array the game's.
    {
      using Sort = void(__cdecl*)(uint32_t*, uint32_t*, int, int);
      const auto game_function = reinterpret_cast<Sort>(game_sort);
      const auto stub_function = reinterpret_cast<Sort>(stub);
      std::atomic<uint64_t> wrong = 0;
      std::atomic<uint64_t> done = 0;
      std::vector<std::thread> threads;
      for (int worker = 0; worker < 12; ++worker) {
        threads.emplace_back([&, worker] {
          Random local = {.state = 0xABCull + static_cast<uint64_t>(worker)};
          for (int run = 0; run < 400; ++run) {
            std::vector<uint32_t> values(128 + local.Next() % 40000);
            for (uint32_t& value : values) value = SortValue(static_cast<int>(local.Next() % 9), 0, 1, &local);
            std::vector<uint32_t> ours = values;
            game_function(values.data(), values.data() + values.size(), static_cast<int>(values.size()), 0);
            stub_function(ours.data(), ours.data() + ours.size(), static_cast<int>(ours.size()), 0);
            if (values != ours) wrong.fetch_add(1);
            done.fetch_add(1);
          }
        });
      }
      for (auto& thread : threads) thread.join();
      CHECK(wrong == 0);
      CHECK(*slots_in_use == 0);
      std::printf("   12 threads, %llu arrays at once: all the game's, every slot free after\n", done.load());
    }

    // On and off 200 times while 4 threads run the patched merge-then-sort function.
    {
      using Caller = uint32_t(__stdcall*)(void*);
      const auto caller = reinterpret_cast<Caller>(patched_caller);
      std::atomic<bool> stop = false;
      std::atomic<uint64_t> wrong = 0;
      std::atomic<uint64_t> lists = 0;
      std::vector<std::thread> threads;
      for (int worker = 0; worker < 4; ++worker) {
        threads.emplace_back([&, worker] {
          uint64_t seed = 0x7000000ull * static_cast<uint64_t>(worker + 1);
          while (!stop.load(std::memory_order_relaxed)) {
            SortList list = MakeSortList(++seed, 2000);
            std::vector<uint32_t> expected(list.merged.begin(), list.merged.begin() + list.list[2]);
            if ((list.list[7] & 0xFF) == 1) {
              for (uint32_t thread = 0; thread < list.threads.size(); ++thread) {
                expected.insert(expected.end(), list.threads[thread].begin(), list.threads[thread].begin() + list.headers[thread * 3 + 1]);
              }
            }
            std::sort(expected.begin(), expected.end());
            const uint32_t count = caller(list.list.data());
            if (count != expected.size() || !std::equal(expected.begin(), expected.end(), list.merged.begin())) wrong.fetch_add(1);
            lists.fetch_add(1);
          }
        });
      }
      constexpr int SWITCHES = 200;
      int toggles = 0;
      for (int round = 0; round < SWITCHES; ++round) {
        const bool on = (round % 2) != 0;  // on at the start: off first
        int tries = 0;
        do {
          twattila::engine_code::Update(on);
          ++tries;
        } while (((ec::written & 0b10000u) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
        toggles += (((ec::written & 0b10000u) != 0) == on) ? 1 : 0;
      }
      stop = true;
      for (auto& thread : threads) thread.join();
      CHECK(wrong == 0);
      CHECK(toggles == SWITCHES);
      std::printf("   %d switches while 4 threads merged and sorted %llu lists: every list sorted, every count the game's\n",
                  SWITCHES, lists.load());
    }

    // Timing (one thread, warm): the game's sort against the stub.
    {
      twattila::engine_code::Update(true);
      std::printf("   us a sort, game / ours:");
      for (const uint32_t count : {256u, 1024u, 16384u, 65536u, 262144u}) {
        for (const int kind : {0, 1}) {
          std::vector<uint32_t> source(count);
          for (uint32_t index = 0; index < count; ++index) source[index] = SortValue(kind, index, count, &random);
          const auto time = [&](uint32_t function) {
            using Sort = void(__cdecl*)(uint32_t*, uint32_t*, int, int);
            std::vector<double> runs;
            for (int run = 0; run < 9; ++run) {
              std::vector<uint32_t> values = source;
              const auto begin = std::chrono::steady_clock::now();
              reinterpret_cast<Sort>(function)(values.data(), values.data() + count, static_cast<int>(count), 0);
              runs.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count());
            }
            std::sort(runs.begin(), runs.end());
            return runs[4];
          };
          std::printf(" %u %s %.1f / %.1f;", count, (kind == 0 ? "random" : "pointers"), time(game_sort), time(stub));
        }
      }
      std::printf("\n");
    }
  } else {
    std::printf("9. render list sort: none in this build\n");
  }

  // 10. The instance gather's prefetch.
  if (build.gather_rva != 0) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, true);
    CHECK((ec::written & 0b100000u) != 0);
    const uint32_t game_gather = Address(game + build.gather_rva);
    const uint32_t patched_gather = Address(patched + build.gather_rva);
    // The items' pointers end where a page without access starts: a read past the loop's end would fault.
    constexpr uint32_t MAX_ITEMS = 4096;
    auto* item_pages = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x5000, MEM_RESERVE, PAGE_NOACCESS));
    CHECK(item_pages != nullptr && VirtualAlloc(item_pages, 0x4000, MEM_COMMIT, PAGE_READWRITE) != nullptr);
    auto* items_end = reinterpret_cast<uint32_t*>(item_pages + 0x4000);
    std::vector<uint8_t> sources(1 << 20);
    Random random = {.state = 0x6A7};
    for (uint8_t& byte : sources) byte = static_cast<uint8_t>(random.Next());
    // The gather's object: first index +0x18, end +0x1c, +0x24 a pointer to its draw's description: stride +0x44,
    // destination +0x48, the items' pointers +0x84.
    struct Gather {
      std::array<uint8_t, 0x40> self = {};
      std::array<uint8_t, 0x100> desc = {};
      uint32_t desc_pointer = 0;
      std::vector<uint8_t> destination;
      void Set(uint32_t first, uint32_t end, uint32_t stride, const uint32_t* items, const std::vector<uint8_t>& initial) {
        destination = initial;
        Write32(desc.data() + 0x44, stride);
        Write32(desc.data() + 0x48, Address(destination.data()));
        Write32(desc.data() + 0x84, Address(items));
        desc_pointer = Address(desc.data());
        Write32(self.data() + 0x18, first);
        Write32(self.data() + 0x1c, end);
        Write32(self.data() + 0x24, Address(&desc_pointer));
      }
    };
    auto a_world = std::make_unique<Gather>();
    auto b_world = std::make_unique<Gather>();
    for (int run = 0; run < 20000; ++run) {
      const uint32_t end = random.Next() % (MAX_ITEMS + 1);
      const uint32_t first = (end == 0 ? 0 : random.Next() % (end + 1));
      const uint32_t stride = random.Next() % 260;
      uint32_t* items = items_end - end;
      for (uint32_t index = 0; index < end; ++index) items[index] = Address(sources.data() + random.Next() % (sources.size() - 260));
      std::vector<uint8_t> initial(static_cast<size_t>(end) * stride + 64);
      for (uint8_t& byte : initial) byte = static_cast<uint8_t>(random.Next());
      a_world->Set(first, end, stride, items, initial);
      b_world->Set(first, end, stride, items, initial);
      RegisterCall a = {.function = game_gather};
      for (uint32_t& in : a.in) in = random.Next();
      a.in[1] = Address(a_world->self.data());  // ecx: this
      RegisterCall b = a;
      b.function = patched_gather;
      b.in[1] = Address(b_world->self.data());
      CallWithRegisters(&a);
      CallWithRegisters(&b);
      // ecx: memcpy's, or `this` when the loop doesn't run (each copy's own object).
      const bool same_ecx = a.out[1] == b.out[1] || (a.out[1] == a.in[1] && b.out[1] == b.in[1]);
      if (a.out[0] != b.out[0] || !same_ecx || std::memcmp(a.out + 2, b.out + 2, 20) != 0 || a.esp_after != a.esp_before
          || b.esp_after != b.esp_before || a_world->destination != b_world->destination) {
        std::printf("  gather run %d (%u..%u, stride %u): eax %08X / %08X\n", run, first, end, stride, a.out[0], b.out[0]);
        CHECK(false);
      }
    }
    std::printf("10. instance gather: 20000 random draws (0 to 4096 items, strides 0 to 259, the items' pointers ending at a "
                "page without access): every register, esp and destination byte equal\n");

    // On and off 200 times while 4 threads run the patched gather.
    {
      using GatherCall = uint32_t(__thiscall*)(void*);
      const auto gather = reinterpret_cast<GatherCall>(patched_gather);
      std::atomic<bool> stop = false;
      std::atomic<uint64_t> wrong = 0;
      std::atomic<uint64_t> draws = 0;
      std::vector<std::thread> threads;
      for (int worker = 0; worker < 4; ++worker) {
        threads.emplace_back([&, worker] {
          Random local = {.state = 0x900ull + static_cast<uint64_t>(worker)};
          std::vector<uint32_t> items(2000);
          for (uint32_t& item : items) item = Address(sources.data() + local.Next() % (sources.size() - 260));
          auto world = std::make_unique<Gather>();
          std::vector<uint8_t> expected(items.size() * 96 + 64, 0);
          for (size_t index = 0; index < items.size(); ++index) {
            std::memcpy(expected.data() + index * 96, reinterpret_cast<const void*>(static_cast<uintptr_t>(items[index])), 96);
          }
          while (!stop.load(std::memory_order_relaxed)) {
            world->Set(0, static_cast<uint32_t>(items.size()), 96, items.data(), std::vector<uint8_t>(expected.size(), 0));
            if (gather(world->self.data()) != 0 || world->destination != expected) wrong.fetch_add(1);
            draws.fetch_add(1);
          }
        });
      }
      constexpr int SWITCHES = 200;
      int toggles = 0;
      for (int round = 0; round < SWITCHES; ++round) {
        const bool on = (round % 2) != 0;  // on at the start: off first
        int tries = 0;
        do {
          twattila::engine_code::Update(on);
          ++tries;
        } while (((ec::written & 0b100000u) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
        toggles += (((ec::written & 0b100000u) != 0) == on) ? 1 : 0;
      }
      stop = true;
      for (auto& thread : threads) thread.join();
      CHECK(wrong == 0);
      CHECK(toggles == SWITCHES);
      std::printf("   %d switches while 4 threads gathered %llu draws: every destination the game's\n", SWITCHES, draws.load());
    }

    // Timing: 30000 items of 128 bytes on random slots of 64 MB, 64 MB written between runs (cold).
    {
      twattila::engine_code::Update(true);
      std::vector<uint8_t> arena(64u << 20);
      std::vector<uint8_t> garbage(64u << 20);
      std::vector<uint32_t> items(30000);
      for (uint32_t& item : items) item = Address(arena.data() + (random.Next() % ((64u << 20) / 256)) * 256);
      auto world = std::make_unique<Gather>();
      const auto time = [&](uint32_t function) {
        std::vector<double> runs;
        for (int run = 0; run < 9; ++run) {
          world->Set(0, static_cast<uint32_t>(items.size()), 128, items.data(), std::vector<uint8_t>(items.size() * 128 + 64));
          for (size_t at = 0; at < garbage.size(); at += 64) garbage[at] += 1;
          const auto begin = std::chrono::steady_clock::now();
          reinterpret_cast<uint32_t(__thiscall*)(void*)>(function)(world->self.data());
          runs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
        }
        std::sort(runs.begin(), runs.end());
        return runs[4];
      };
      std::printf("   30000 items of 128 bytes, cold: ms a gather, game %.3f / ours %.3f\n", time(game_gather), time(patched_gather));
    }
  } else {
    std::printf("10. instance gather: none in this build\n");
  }

  // Off: the game's bytes back (the harness's jumps removed first).
  std::memcpy(game + build.truncation_resume_rva, file.data() + (build.truncation_resume_rva - text_rva + text_header->PointerToRawData), 5);
  std::memcpy(patched + build.truncation_resume_rva, unpatched.data() + (build.truncation_resume_rva - text_rva), 5);
  twattila::engine_code::Update(false);
  CHECK(ec::written == 0);
  CHECK(std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
  twattila::engine_code::Uninstall();
  std::printf("off: the game's code back byte for byte\n");

  // 7. With the Attila CPU Performance Fix's x87 fix the truncation block holds fisttp and nops for both fldcw (bytes
  // 23..26, 28 and 31..34 of the block, ATN's heapfix.log: 1277 of 1277 blocks, 165 by fisttp): that site is left
  // alone, the others still found.
  uint8_t* block = patched + build.truncation_block_rva;
  for (const size_t at : {23, 31}) std::memcpy(block + at, "\x0F\x1F\x40\x00", 4);
  block[28] = 0x4C;
  for (ec::Site& site : ec::sites) site.at = nullptr;
  ec::prepared = false;
  ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
  CHECK(ec::sites[0].at == nullptr && ec::sites[1].at != nullptr && ec::sites[2].at != nullptr);
  CHECK((ec::sites[3].at != nullptr) == (build.list_push_function_rva != 0));
  CHECK((ec::sites[4].at != nullptr) == (build.sort_rva != 0));
  CHECK((ec::sites[5].at != nullptr) == (build.gather_rva != 0));
  CHECK(log_lines.back().find("left alone") != std::string::npos && log_lines.back().find("soldier draw rounding") != std::string::npos);
  std::printf("7. a block another mod changed: left alone, the other two found\n");

  // 11. The sort or its caller not at the Steam build's place: the render list sort left alone with its log line, no
  // scratch, no search of the image (SORT matches once and SORT_CALLER seven times elsewhere: a scan would find them).
  if (build.sort_rva != 0) {
    for (const bool caller_moved : {false, true}) {
      ec::Build moved = *build.code;
      if (caller_moved) {
        moved.sort_caller_rva += 0x10;
      } else {
        moved.sort_rva += 0x10;
      }
      if (ec::sort_scratch != nullptr) VirtualFree(ec::sort_scratch, 0, MEM_RELEASE);
      ec::sort_scratch = nullptr;
      for (ec::Site& site : ec::sites) site.at = nullptr;
      ec::prepared = false;
      const size_t lines_before = log_lines.size();
      ec::Prepare(reinterpret_cast<HMODULE>(patched), moved);
      CHECK(ec::sites[4].at == nullptr && ec::sort_scratch == nullptr);
      CHECK(ec::sites[1].at != nullptr && ec::sites[2].at != nullptr);
      CHECK(std::any_of(log_lines.begin() + static_cast<ptrdiff_t>(lines_before), log_lines.end(),
                        [](const std::string& line) { return line.find("render list sort left alone") != std::string::npos; }));
    }
    std::printf("11. the sort or its caller moved: left alone, no scratch\n");
  }
  std::printf("PASS\n");
  return 0;
}

#endif
