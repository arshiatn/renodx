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
// 6. Timing, A against B: float to half on sprite-like rows, the truncation, fmod(x, 1).
// 7. The truncation block as the Attila CPU Performance Fix leaves it: left alone, the other two still found.
// 8. The render lists' push (+0x176ced0, Faster Render Lists): 1 M random pushes (thread, vector sizes, the "has items"
//    byte, the list's other bytes, the item, every register): the registers, esp and every byte of the list, its
//    vectors and their items equal; on and off 200 times while 4 threads push to one list: every item in its place,
//    every return the game's; timing with 4 threads pushing to 16 lists (the byte cleared each round, as the merge does).
// 9. The ready check (+0x18635a0, Faster Game CPU Code; thiscall, no arguments): every child count 0 to 64 x every
//    position of the first child not ready (or none) x 7 values of [this + 4] x the last child with and without a
//    control block x the count's 3 bits above the 29 that x 8 keeps; every [this + 4] with 0 and 1 child; 2 M random
//    vectors (up to 4096 children; uses from 1 to 0xFFFFFFFF, the carry edges among them; objects no code reads at random
//    addresses): eax ecx edx ebx esi edi ebp, the arithmetic flags, esp, the 64 bytes of the call's stack, every count
//    and [this] equal, the game's results as derived from its bytes (engine_code.hpp), no dispose or destroy run;
//    CONTROL: a child with uses 0 (the game disposes it) must differ; on and off 200 times while 4 threads call both
//    copies on vectors sharing control blocks: every al the game's, every count back; timing, 1 and 4 threads.
// Thrones of Britannia's and Rome II's DLLs the same way (engine_code's BUILDS, each picked by its truncation's pattern):
// their RVAs, their CRT's state, their truncation's stack slots; Rome II has no ready check (its pattern found 0 times).
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
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
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
  uint32_t ready_check_rva;         // 0: none in this build
};
constexpr TestBuild ATTILA = {"Attila", &twattila::engine_code::internal::ATTILA_BUILD, 0x169a220, 0x17f6515, 0x17f6539, 0x1a05cc0,
                              0x1dae4f4, 0x2a8b504, 0x2a8b4cc, 0x1c, 0x28, 0x176ced0, 0x18635a0};
constexpr TestBuild THRONES = {"Thrones of Britannia", &twattila::engine_code::internal::THRONES_BUILD, 0x1707eb0, 0x186aa48, 0x186aa6c,
                               0x1b0a030, 0x1eec634, 0x25fde74, 0x25fde3c, 0x20, 0x18, 0x17defc0, 0x18d8400};
constexpr TestBuild ROME2 = {"Rome II", &twattila::engine_code::internal::ROME2_BUILD, 0x1368030, 0x14b434a, 0x14b436e, 0x169ca30,
                             0x19ebd74, 0x21263cc, 0x2126394, 0x1c, 0x20, 0, 0};

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
  CHECK(Matches(game + text_rva, text_size, ec::READY_CHECK) == (build.ready_check_rva != 0 ? 1u : 0u));
  CHECK(build.code->ready_check_rva == build.ready_check_rva);
  // B's own code as mapped (A's differs in every relocated address).
  const std::vector<uint8_t> unpatched(patched + text_rva, patched + text_rva + text_size);
  ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
  CHECK(ec::sites[0].at == patched + build.truncation_block_rva);
  CHECK(ec::sites[1].at == patched + build.half_rva);
  CHECK(ec::sites[2].at == patched + build.fmod_rva);
  CHECK(ec::sites[3].at == (build.list_push_function_rva != 0 ? patched + build.code->list_push_rva + ec::LIST_PUSH_SITE : nullptr));
  CHECK(ec::sites[4].at == (build.ready_check_rva != 0 ? patched + build.ready_check_rva : nullptr));
  CHECK(std::memcmp(unpatched.data(), patched + text_rva, text_size) == 0);
  twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, true);
  CHECK(ec::written == ec::found
        && ec::found == (0b00111u | (build.list_push_function_rva != 0 ? 0b01000u : 0u) | (build.ready_check_rva != 0 ? 0b10000u : 0u)));
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
    std::array<uint8_t, 0x48> self = {};
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
      const std::array<uint8_t, 0x48> self_before = self;
      ReadyCall call = {.in = {random->Next(), Address(self.data()), random->Next(), random->Next(), random->Next(), random->Next(), random->Next()},
                        .flags_in = 0x202 | (random->Next() & 0x8D5)};
      for (uint8_t& byte : call.stack) byte = static_cast<uint8_t>(random->Next());
      const auto world_kept = [&] { return std::equal(blocks.begin(), blocks.begin() + n, blocks_before.begin()) && self == self_before; };
      ReadyCall a = call;
      ready_function = game_ready;
      CallReady(&a);
      const bool game_kept = world_kept();
      ReadyCall b = call;
      ready_function = patched_ready;
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
      if (!good) {
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
    std::printf("9. ready check: %llu vectors (%llu without a child, %llu with the flag 0, %llu all ready, %llu with one not ready): "
                "every register, the flags, esp, the stack, every count equal and the game's as derived (%.1f s)\n",
                cases, kinds[0], kinds[1], kinds[2], kinds[3], std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    // CONTROL: a dead child (uses 0, never in the game's vectors): the game's release disposes and destroys it, the stub
    // never touches it; the compare must see it.
    std::printf("   CONTROL, a child with uses 0 (must differ):\n");
    CHECK(!check(3, 3, 1, true, &random, 1));
    CHECK(disposed.load() == 0x10001);
    disposed = 0;

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
  // alone, the others still found. Then one byte of the ready check changed (as another mod's patch would): left alone.
  uint8_t* block = patched + build.truncation_block_rva;
  for (const size_t at : {23, 31}) std::memcpy(block + at, "\x0F\x1F\x40\x00", 4);
  block[28] = 0x4C;
  for (ec::Site& site : ec::sites) site.at = nullptr;
  ec::prepared = false;
  ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
  CHECK(ec::sites[0].at == nullptr && ec::sites[1].at != nullptr && ec::sites[2].at != nullptr);
  CHECK((ec::sites[3].at != nullptr) == (build.list_push_function_rva != 0));
  CHECK((ec::sites[4].at != nullptr) == (build.ready_check_rva != 0));
  CHECK(log_lines.back().find("left alone") != std::string::npos && log_lines.back().find("soldier draw rounding") != std::string::npos);
  if (build.ready_check_rva != 0) {
    patched[build.ready_check_rva + 0x1F] = 0x90;  // the copy's lock prefix
    for (ec::Site& site : ec::sites) site.at = nullptr;
    ec::prepared = false;
    ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
    CHECK(ec::sites[4].at == nullptr && ec::sites[1].at != nullptr);
    CHECK(log_lines.back().find("left alone") != std::string::npos && log_lines.back().find("ready checks") != std::string::npos);
  } else {
    CHECK(log_lines.back().find("ready checks") == std::string::npos);
  }
  std::printf("7. a block another mod changed: left alone, the others found%s\nPASS\n",
              (build.ready_check_rva != 0 ? "; a changed ready check left alone" : ""));
  return 0;
}

#endif
