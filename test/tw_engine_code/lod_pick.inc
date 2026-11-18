// ====================================================================================================================
// 22. Model Detail Distance: the game's LOD pick (PickLod: Attila +0x183d570, Rome II +0x14f0710, Thrones +0x18b2060; the slider's row; engine_code.hpp 18.), included
// inside tests.cpp's anonymous namespace after sweep_prefetch.inc (FnCall, CallFn, ReloadBlockCall, EnterReloadBlock, ReloadBlockReturn, ReloadCallRandom, ReloadFloat,
// ReloadMxcsr, RELOAD_SPECIAL, JumpTo, Random, Report and Address of the earlier parts are used). The stub multiplies the distance squared on the stack by the reciprocal of the
// slider squared: the add-on's old Detours hook's math (the reference below: `distance_squared * (1.f / (slider * slider))`, two single roundings, under the thread's MXCSR). The stub
// leaves MXCSR's control bits alone and its multiply raises the sticky exception flags the hook's multiply raised: the comparisons expect the game's MXCSR | exactly those flags.
// 22a. The site and stub as Prepare made them; the function's callers (direct calls, jumps, absolute references) as pinned: each pushes the two argument slots for the callee-pop
//    call, fills the distance slot and reads nothing below esp after the call (the stub writes the product over that slot);
// 22b. the stub alone, in a block entered with every register, the flags, xmm0-7, MXCSR (any rounding mode, DAZ, FTZ, sticky flags) and a frame set, against the game's three
//    instructions (called with the distance already multiplied): every register, the flags, xmm0-7, MXCSR (plus the multiply's sticky flags), esp and the frame (but the stub's
//    scratch below esp) equal, at the
//    sliders 1x, the next float up, 8x and others, 0.5x to 3e19x, with random distances, zeros, denormals, infinities, NaNs and every RELOAD_SPECIAL value; CONTROLs: a mutant of each
//    stub instruction that matters (a reciprocal one ulp off, a divide, a clobbered register, a missing restore of xmm0 or the flags, a wrong slot) must be seen (esp's balance is by
//    the two lea and compared, but a mutant of it derails the harness itself, which finds its frame by esp);
// 22c. the whole function in both copies of the DLL on random models (0, 1 and up to 10 LODs, the one-LOD path, the forced path through the fake singleton the function asks, the
//    distance's ulp neighbours as the LODs' distances): every register, the flags, x87 / MXCSR / xmm, esp, the model and the stack equal; CONTROLs in the page;
// 22d. the slider moved while 4 threads call the function: 1x <-> above 1x (the jump in and the game's bytes back) and the float alone; every result is one the sliders give, and after
//    each move the main thread's own call gets the new slider's result;
// 22e. a parked thread at each byte of the site and of the stub; 22f. the status words, another mod's bytes, the pattern not found, the missing-restore CONTROL; 22g. timing.
// ====================================================================================================================

// What the game's function asks, to take the forced and the ordinary paths: the singleton it loads at +0x34 (an absolute address in each copy of the DLL) and the method it calls
// (+0x3a: call [eax + 0x84] in Attila, +0x7c in Rome II, +0x8c in Thrones). One shared fake (the tests run one pair of calls at a time).
std::atomic<uint32_t> lod_bias = 0;
uint32_t __fastcall LodBias(void*, void*) { return lod_bias.load(std::memory_order_relaxed); }
std::array<uint32_t, 64> lod_vtable = {};
uint32_t lod_singleton = 0;

bool LodInstallBias(uint8_t* copy, uint32_t function_rva) {
  const uint8_t* function = copy + function_rva;
  if (function[0x32] != 0x8B || function[0x33] != 0x0D || function[0x38] != 0x8B || function[0x39] != 0x01 || function[0x3A] != 0xFF) return false;
  if (function[0x3B] != 0x90 && function[0x3B] != 0x50) return false;
  const uint32_t slot = (function[0x3B] == 0x90 ? Read32(function + 0x3C) : function[0x3C]);
  if (slot % 4 != 0 || slot / 4 >= lod_vtable.size()) return false;
  lod_vtable[slot / 4] = Address(reinterpret_cast<const void*>(&LodBias));
  lod_singleton = Address(lod_vtable.data());
  Write32(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(Read32(function + 0x34))), Address(&lod_singleton));
  return true;
}

// The add-on's old Detours hook (OnPickLod, Update's store), as it was: the reciprocal of the slider squared, one multiply of the distance squared by it. Never inlined, so that the
// compiler can't fold them or move them across the MXCSR changes of LodProduct.
__declspec(noinline) float OldHookReciprocal(float slider) { return 1.f / (slider * slider); }
__declspec(noinline) float OldHookMath(float distance_squared, float scale) { return distance_squared * scale; }
float (*volatile old_hook_math)(float, float) = &OldHookMath;

// MXCSR's sticky exception flags (bits 0-5).
constexpr uint32_t MXCSR_FLAGS = 0x3F;

// The hook's product of `distance_bits` and `scale`, under the MXCSR `mxcsr`; `raised`: the sticky flags the multiply sets.
uint32_t LodProduct(uint32_t distance_bits, float scale, uint32_t mxcsr, uint32_t* raised) {
  const uint32_t saved = _mm_getcsr();
  _mm_setcsr(mxcsr & ~MXCSR_FLAGS);
  const float product = old_hook_math(std::bit_cast<float>(distance_bits), scale);
  *raised = _mm_getcsr() & MXCSR_FLAGS;
  _mm_setcsr(saved);
  return std::bit_cast<uint32_t>(product);
}

// A distance squared for a call: mostly a LOD distance (1 to 1e6), the callers' zero, any class of float, denormals, the largest.
uint32_t LodDistance(Random* random) {
  switch (random->Next() % 10) {
    case 0: return 0;
    case 1: return ReloadFloat(random);
    case 2: return random->Next() ^ (random->Next() << 16);
    case 3: return (random->Next() & 0x007FFFFF) | ((random->Next() & 1) << 31);
    case 4: return 0x7F7FFFFF - (random->Next() & 0xFF);
    default: return std::bit_cast<uint32_t>(static_cast<float>(random->Next() % 4'000'000) * 0.25f + 1.f);
  }
}

// The sliders of the tests: 1x, the next float up from it, 8x (the row's maximum) and between, then below and above the row's range (the multiply is the same for any float in the
// cell: a reciprocal that underflows to a subnormal or to zero too).
constexpr std::array<float, 14> LOD_SLIDERS = {1.f, 1.0000001f, 1.25f, 1.5f, 2.f, 3.f, 4.f, 5.f, 7.9999995f, 8.f, 0.5f, 16.f, 1e9f, 3e19f};

// The function's types: thiscall as fastcall (the model in ecx, edx unused, the rest on the stack, callee pops).
using LodPick = void*(__fastcall*)(void* model, void* unused_edx, float distance_squared, void** next);

// ---- the whole function ----
// The model the function reads (+0x28 the LOD count, +0x2c the array of entry pointers); each entry's +0x20 and +0x2c (the one-LOD path's), +0x38 distance squared, +0x44 and +0x45
// flag bytes; the `next` out slot at LOD_NEXT of the world.
constexpr uint32_t LOD_WORLD_SIZE = 0x1000;
constexpr uint32_t LOD_MODEL = 0x000, LOD_NEXT = 0x040, LOD_ARRAY = 0x100, LOD_ENTRIES = 0x200, LOD_ENTRY_STRIDE = 0x80, LOD_ENTRY_MAX = 16;
static_assert(LOD_ENTRIES + LOD_ENTRY_MAX * LOD_ENTRY_STRIDE <= LOD_WORLD_SIZE);
constexpr uint32_t LOD_ENTRY_ESP = FN_STACK_E - 20;  // the function's esp at its entry: the four arguments pushed (16 bytes), then the return address

struct LodWorld {
  FnCall call;
  alignas(16) std::array<uint8_t, LOD_WORLD_SIZE> bytes;
};

// `pivot_bits`: the distance squared the function will see (the multiplied one), whose ulp neighbours some entries take: the selection then shows one ulp of the product.
void LodFill(LodWorld* world, Random* random, uint32_t pivot_bits) {
  uint8_t* bytes = world->bytes.data();
  for (uint8_t& byte : world->bytes) byte = static_cast<uint8_t>(random->Next());
  const uint32_t base = Address(bytes);
  const uint32_t count = (random->Next() % 7 == 0 ? random->Next() % 2 : 2 + random->Next() % 9);
  Write32(bytes + LOD_MODEL + 0x28, count);
  Write32(bytes + LOD_MODEL + 0x2C, base + LOD_ARRAY);
  const uint32_t mode = random->Next() % 3;
  float previous = 0.f;
  for (uint32_t index = 0; index < LOD_ENTRY_MAX; ++index) {
    Write32(bytes + LOD_ARRAY + 4 * index, base + LOD_ENTRIES + LOD_ENTRY_STRIDE * index);
    uint8_t* entry = bytes + LOD_ENTRIES + LOD_ENTRY_STRIDE * index;
    uint32_t distance_bits = random->Next();
    if (mode == 0 && pivot_bits >= 0x00800000 && pivot_bits < 0x7F7FFFF0) {
      distance_bits = pivot_bits + index - 3;
    } else if (mode == 1) {
      previous += static_cast<float>(random->Next() % 3000) * 0.5f + 1.f;
      distance_bits = std::bit_cast<uint32_t>(previous);
    }
    Write32(entry + 0x38, distance_bits);
    Write32(entry + 0x20, random->Next() % 2 == 0 ? 0 : random->Next());
    Write32(entry + 0x2C, random->Next() % 3 == 0 ? 0 : random->Next());
    entry[0x44] = (random->Next() % 4 == 0 ? 0 : static_cast<uint8_t>(1 + random->Next() % 255));
  }
}

// A world to call: the model, the registers, the stack and the x87 / SSE state at random, MXCSR `mxcsr`; the function and the distance are the caller's.
void LodSetUp(LodWorld* world, Random* random, uint32_t pivot_bits, uint32_t mxcsr) {
  FnCall* call = &world->call;
  LodFill(world, random, pivot_bits);
  const uint32_t base = Address(world->bytes.data());
  call->this_ptr = base + LOD_MODEL;
  call->args[1] = base + LOD_NEXT;
  call->args[2] = random->Next();
  call->args[3] = random->Next();
  ReloadCallRandom(call, random, mxcsr, false);
}

void LodRun(LodWorld* world, const LodWorld& backup, LodWorld* out, uint32_t function, uint32_t distance_bits) {
  *world = backup;
  world->call.function = function;
  world->call.args[0] = distance_bits;
  CallFn(&world->call);
  *out = *world;
}

// What differs between the game's run (the distance already multiplied) and ours ("" if nothing): registers, esp, the flags, x87, MXCSR, all of xmm0-7, the model and its out slot,
// the stack (a dword that differs by exactly the distance between the two copies of the DLL is a return address a call left behind). The stub's scratch is the 16 bytes below the
// entry's esp that the function's four pushes write over; MXCSR is the game's with the sticky flags `raised` that the stub's multiply sets.
const char* LodDiff(const LodWorld& a, const LodWorld& b, uint32_t copy_distance, uint32_t raised) {
  if (a.call.faulted != 0 || b.call.faulted != 0) return "a fault";
  if (std::memcmp(a.call.out, b.call.out, sizeof(a.call.out)) != 0) return "registers";
  if (a.call.esp_after != b.call.esp_after) return "esp";
  if (((a.call.flags_after ^ b.call.flags_after) & 0x8D5) != 0) return "flags";
  if (std::memcmp(a.call.fx_out, b.call.fx_out, 24) != 0 || std::memcmp(a.call.fx_out + 28, b.call.fx_out + 28, 132) != 0) return "x87 state";
  if ((Read32(a.call.fx_out + 24) | raised) != Read32(b.call.fx_out + 24)) return "MXCSR";
  if (std::memcmp(a.call.fx_out + 160, b.call.fx_out + 160, 128) != 0) return "xmm";
  if (a.bytes != b.bytes) return "memory (the model or its out slot)";
  for (uint32_t at = FN_STACK_E - 0x400; at < FN_STACK_E + 0x20; at += 4) {
    const uint32_t x = Read32(a.call.stack + at);
    const uint32_t y = Read32(b.call.stack + at);
    if (x != y && y - x != copy_distance) return "stack";
  }
  return "";
}

// ---- the stub alone ----
// Equal when every register, the flags, xmm0-7, MXCSR (with the sticky flags `raised`) and esp are, and the frame but the stub's scratch (frame + 0x10 .. + 0x20: the saved xmm0; the two
// displaced pushes cover the upper 8 bytes, the function's four pushes would cover all).
bool LodBlockEqual(const ReloadBlockCall& a, const ReloadBlockCall& b, uint32_t raised) {
  return std::memcmp(a.out, b.out, sizeof(a.out)) == 0 && a.flags_out == b.flags_out && a.esp_after - Address(&a) == b.esp_after - Address(&b)
         && std::memcmp(a.xmm_out, b.xmm_out, sizeof(a.xmm_out)) == 0 && (a.mxcsr_out | raised) == b.mxcsr_out && std::memcmp(a.frame, b.frame, 0x10) == 0
         && std::memcmp(a.frame + 0x18, b.frame + 0x18, sizeof(a.frame) - 0x18) == 0;
}

// The row's slider as Update gets it, until the site holds what the slider says (a thread inside it makes a write wait, up to MAX_FAILED_WRITES presents).
bool LodSlider(float value) {
  namespace ec = twattila::engine_code::internal;
  const bool on = (value != 1.f);
  int tries = 0;
  do {
    twattila::engine_code::Update(false, twattila::engine_code::Game::ATTILA, false, false, false, false, false, false, value);
    ++tries;
  } while ((((ec::written >> ec::MODEL_DETAIL_INDEX) & 1) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
  return (((ec::written >> ec::MODEL_DETAIL_INDEX) & 1) != 0) == on;
}

// A model for the threads and the timing: 10 LODs whose distances squared are 1, 4, 9 .. 100, each with its flag set.
struct alignas(16) LodSimpleModel {
  std::array<uint8_t, LOD_WORLD_SIZE> bytes;
};
void LodSimpleFill(LodSimpleModel* model) {
  uint8_t* bytes = model->bytes.data();
  std::memset(bytes, 0, LOD_WORLD_SIZE);
  const uint32_t base = Address(bytes);
  Write32(bytes + LOD_MODEL + 0x28, 10);
  Write32(bytes + LOD_MODEL + 0x2C, base + LOD_ARRAY);
  for (uint32_t index = 0; index < 10; ++index) {
    Write32(bytes + LOD_ARRAY + 4 * index, base + LOD_ENTRIES + LOD_ENTRY_STRIDE * index);
    uint8_t* entry = bytes + LOD_ENTRIES + LOD_ENTRY_STRIDE * index;
    Write32(entry + 0x38, std::bit_cast<uint32_t>(static_cast<float>((index + 1) * (index + 1))));
    entry[0x44] = 1;
  }
}

// The entry the function picks for the distance squared `distance` (default MXCSR): its index, 10 for none.
uint32_t LodSimpleIndex(const LodSimpleModel& model, const void* entry) {
  if (entry == nullptr) return 10;
  return (Address(entry) - Address(model.bytes.data()) - LOD_ENTRIES) / LOD_ENTRY_STRIDE;
}

// The add-on's old Detours hook as it was built (a jump from the function's first 5 bytes to OnPickLod, which calls the trampoline: the displaced instructions and a jump back), for the timing.
std::atomic<bool> lod_old_active = true;
std::atomic<float> lod_old_scale = 1.f / 9.f;
LodPick lod_old_original = nullptr;
void* __fastcall LodOldHook(void* model, void* unused_edx, float distance_squared, void** next) {
  if (!lod_old_active.load(std::memory_order_relaxed)) return lod_old_original(model, unused_edx, distance_squared, next);
  return lod_old_original(model, unused_edx, distance_squared * lod_old_scale.load(std::memory_order_relaxed), next);
}

// The stub's CONTROLs: bytes of its code replaced (`edits`: offset and bytes) or the float's bits moved (`cell_delta`); each must be seen by the comparisons. `block` is the stub alone's
// copy of it, `runs` and `seen` the count of the comparisons that ran it and found it different; `whole` (the ones that leave the registers the function needs alone) also in the page.
struct LodMutant {
  const char* what;
  std::vector<std::pair<size_t, std::vector<uint8_t>>> edits;
  int cell_delta = 0;
  bool whole = false;
  uint32_t block = 0;
  int runs = 0;
  int seen = 0;
};

std::vector<LodMutant> LodMutants() {
  return {
      {.what = "the reciprocal one ulp low (a truncating division)", .cell_delta = -1, .whole = true},
      {.what = "the reciprocal one ulp high", .cell_delta = 1},
      {.what = "divss for mulss", .edits = {{16, {0x5E}}}, .whole = true},
      {.what = "the product stored in the next slot (esp + 0x18)", .edits = {{27, {0x18}}}},
      {.what = "no store of the product", .edits = {{22, {0x90, 0x90, 0x90, 0x90, 0x90, 0x90}}}, .whole = true},
      {.what = "push eax for push ebx", .edits = {{36, {0x50}}}, .whole = true},
      {.what = "ecx clobbered, xmm0 not restored (mov ecx, [esp])", .edits = {{28, {0x8B, 0x0C, 0x24, 0x90}}}},
      {.what = "xmm0 not restored", .edits = {{28, {0x90, 0x90, 0x90, 0x90}}}, .whole = true},
      {.what = "the flags clobbered (add esp, 16 for lea)", .edits = {{32, {0x83, 0xC4, 0x10, 0x90}}}},
      {.what = "xmm1 used for the multiply (xmm1 clobbered)", .edits = {{11, {0x4C}}, {17, {0x0D}}, {25, {0x4C}}}, .whole = true},
      {.what = "mov ebp from the wrong slot", .edits = {{41, {0x14}}}},
  };
}

int RunLodTests(const TestBuild& build, uint8_t* game, uint8_t* patched, uint32_t text_rva, uint32_t text_size, uint32_t image_size, bool quick) {
  namespace ec = twattila::engine_code::internal;
  constexpr auto ATTILA_GAME = twattila::engine_code::Game::ATTILA;
  const uint32_t function_rva = build.code->pick_lod_rva;
  CHECK(function_rva != 0);
  const uint32_t copy_distance = static_cast<uint32_t>(patched - game);
  const uint32_t game_function = Address(game + function_rva);
  const uint32_t patched_function = Address(patched + function_rva);
  CHECK(LodInstallBias(game, function_rva) && LodInstallBias(patched, function_rva));
  twattila::engine_code::Update(false);
  CHECK(ec::written == 0);
  const auto page_stub = [&] { return static_cast<const uint8_t*>(ec::page + ec::MODEL_DETAIL_PAGE); };

  // 22a. The site and stub as Prepare made them.
  CHECK(ec::sites[ec::MODEL_DETAIL_INDEX].at == patched + function_rva && ((ec::found >> ec::MODEL_DETAIL_INDEX) & 1) != 0 && ec::model_detail_scale != nullptr);
  constexpr std::array<uint8_t, 6> FIRST = {0x53, 0x55, 0x8B, 0x6C, 0x24, 0x10};  // push ebx; push ebp; mov ebp, [esp + 0x10]
  CHECK(std::memcmp(game + function_rva, FIRST.data(), FIRST.size()) == 0 && std::memcmp(ec::sites[ec::MODEL_DETAIL_INDEX].game.data(), FIRST.data(), 5) == 0);
  const uint8_t* stub = page_stub();
  CHECK(ec::MODEL_DETAIL_STUB.size() == 47 && ec::PICK_LOD_RESUME == FIRST.size());
  CHECK(std::memcmp(stub, ec::MODEL_DETAIL_STUB.data(), 18) == 0 && Read32(stub + 18) == Address(ec::model_detail_scale)
        && std::memcmp(stub + 22, ec::MODEL_DETAIL_STUB.data() + 22, 20) == 0);
  CHECK(std::memcmp(stub + 36, FIRST.data(), FIRST.size()) == 0);  // the game's three instructions, run by the stub
  CHECK(stub[42] == 0xE9 && Address(stub + 47) + Read32(stub + 43) == patched_function + ec::PICK_LOD_RESUME);
  CHECK(ec::model_detail_scale->load() == ec::UNSCALED_BITS);
  {
    // The callers (direct calls; no jumps and no absolute references to the function): pinned, so that another build with another caller is looked at before it is trusted.
    struct Callers {
      const char* game;
      std::array<uint32_t, 5> calls;
    };
    const std::array<Callers, 3> PINNED = {{{"Attila", {0x17F6432, 0x17FF98F, 0x18375EC, 0x183E4AD, 0x186391C}},
                                            {"Rome II", {0x14B421B, 0x14BCA4C, 0x14E9B4B, 0x14F197D, 0x1517194}},
                                            {"Thrones of Britannia", {0x186A965, 0x1873F3F, 0x18AC06C, 0x18B2F9D, 0x18D877C}}}};
    const uint8_t* target = game + function_rva;
    std::vector<uint32_t> calls;
    std::string jumps;
    for (uint32_t at = text_rva; at + 5 <= text_rva + text_size; ++at) {
      const uint8_t opcode = game[at];
      if ((opcode != 0xE8 && opcode != 0xE9) || Address(game + at + 5) + Read32(game + at + 1) != Address(target)) continue;
      if (opcode == 0xE8) {
        calls.push_back(at);
      } else {
        jumps += std::format(" +{:X}", at);
      }
    }
    std::string absolute;
    for (uint32_t at = 0; at + 4 <= image_size; ++at) {
      if (Read32(game + at) == Address(target)) absolute += std::format(" +{:X}", at);
    }
    const auto pinned = std::ranges::find_if(PINNED, [&](const Callers& callers) { return std::string_view(callers.game) == build.game; });
    CHECK(pinned != PINNED.end() && std::ranges::equal(calls, pinned->calls) && jumps.empty() && absolute.empty());
    // Each caller pushes the argument slots (`push eax` the out pointer, `push ecx` the distance's slot), fills the slot in place (a store of a register, or zero) and calls; the
    // next 64 bytes have no [esp - n] (a SIB 0x24 with a negative 8-bit displacement, a conservative scan): the slot is not read again.
    int zero_callers = 0;
    for (const uint32_t call : calls) {
      const uint8_t* at = game + call;
      const bool zero = std::memcmp(at - 9, "\x50\x51\xC7\x04\x24\x00\x00\x00\x00", 9) == 0;
      const bool stored = (std::memcmp(at - 7, "\x50\x51\xF3\x0F\x11\x04\x24", 7) == 0 || std::memcmp(at - 7, "\x50\x51\xF3\x0F\x11\x0C\x24", 7) == 0
                           || std::memcmp(at - 11, "\x50\x51\x8B\x4C\x24\x20\xF3\x0F\x11\x0C\x24", 11) == 0
                           || std::memcmp(at - 11, "\x50\x51\x8B\x4C\x24\x18\xF3\x0F\x11\x0C\x24", 11) == 0);
      CHECK(zero || stored);
      zero_callers += zero ? 1 : 0;
      for (uint32_t offset = 5; offset + 2 < 64; ++offset) {
        CHECK(!((at[offset] & 0xC7) == 0x44 && at[offset + 1] == 0x24 && at[offset + 2] >= 0x80));
      }
    }
    std::printf("22a. Model Detail Distance (the LOD pick, +%X): the site (the game's first three instructions, 6 bytes) and its stub as Prepare made them; the %zu callers (%d with distance 0)\n"
                "    fill the slot they pushed and read nothing below esp after the call; no jumps or absolute references to the function\n",
                function_rva, calls.size(), zero_callers);
  }

  // 22b. The stub alone.
  Random random = {.state = 0x22B};
  {
    constexpr uint32_t STRIDE = 0x80;
    auto* blocks = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    CHECK(blocks != nullptr);
    constexpr std::array<uint8_t, 4> POP_ARGS = {0x8D, 0x64, 0x24, 0x08};  // lea esp, [esp + 8]: the two pushes of the displaced instructions given back (the block leaves esp as it found it)
    uint32_t used = 0;
    const auto make_block = [&](const uint8_t* bytes, size_t size) {
      uint8_t* at = blocks + STRIDE * used++;
      std::memcpy(at, bytes, size);
      std::memcpy(at + size, POP_ARGS.data(), POP_ARGS.size());
      JumpTo(at + size + POP_ARGS.size(), reinterpret_cast<const void*>(&ReloadBlockReturn));
      return Address(at);
    };
    const uint32_t game_block = make_block(FIRST.data(), FIRST.size());
    constexpr size_t BODY = 42;  // the stub's code before its jump
    const uint32_t ours_block = make_block(stub, BODY);
    std::vector<LodMutant> mutants = LodMutants();
    for (LodMutant& mutant : mutants) {
      std::array<uint8_t, BODY> bytes;
      std::memcpy(bytes.data(), stub, BODY);
      for (const auto& [at, edit] : mutant.edits) std::copy(edit.begin(), edit.end(), bytes.begin() + at);
      mutant.block = make_block(bytes.data(), BODY);
    }
    auto game_call = std::make_unique<ReloadBlockCall>();
    auto ours_call = std::make_unique<ReloadBlockCall>();
    CHECK(Address(game_call.get()) % 16 == 0 && Address(ours_call.get()) % 16 == 0);
    int round = 0;
    uint64_t product_wrong = 0;
    // One run of the game's block (given the multiplied distance) and a block of ours (the raw one): true when everything is equal and the slot the stub wrote holds the hook's product.
    const auto block_run = [&](uint32_t ours_target, uint32_t distance_bits, float scale) {
      uint32_t raised = 0;
      for (uint32_t& value : game_call->in) value = random.Next();
      game_call->flags_in = (random.Next() & 0x8D5) | 2;
      game_call->mxcsr_in = ReloadMxcsr(&random, ++round);
      game_call->lanes_at = 4;
      for (uint8_t& byte : game_call->frame) byte = static_cast<uint8_t>(random.Next());
      for (uint8_t& byte : std::span(&game_call->xmm_in[0][0], 128)) byte = static_cast<uint8_t>(random.Next());
      for (uint32_t& lane : game_call->lanes) lane = random.Next();
      const uint32_t product = LodProduct(distance_bits, scale, game_call->mxcsr_in, &raised);
      *ours_call = *game_call;
      game_call->lanes[0] = product;
      ours_call->lanes[0] = distance_bits;
      game_call->target = game_block;
      ours_call->target = ours_target;
      EnterReloadBlock(game_call.get());
      EnterReloadBlock(ours_call.get());
      const bool equal = LodBlockEqual(*game_call, *ours_call, raised);
      if (ours_target == ours_block && Read32(ours_call->frame + 0x24) != product) ++product_wrong;
      return equal;
    };
    const int runs = (quick ? 30'000 : 150'000);
    uint64_t wrong = 0;
    uint64_t total = 0;
    for (const float value : LOD_SLIDERS) {
      CHECK(LodSlider(value));
      const float scale = OldHookReciprocal(value);
      const uint32_t scale_bits = std::bit_cast<uint32_t>(scale);
      CHECK(ec::model_detail_scale->load() == scale_bits);
      for (int run = 0; run < runs; ++run) {
        const uint32_t distance_bits = LodDistance(&random);
        ++total;
        if (!block_run(ours_block, distance_bits, scale)) {
          ++wrong;
          Report("  stub alone at %gx: distance %08X NOT equal\n", value, distance_bits);
        }
      }
      // Every special value (NaN payloads, infinities, subnormals, zeros, the ends of the range) against random registers.
      for (const uint32_t special : RELOAD_SPECIAL) {
        for (int context = 0; context < 20; ++context) {
          ++total;
          if (!block_run(ours_block, special, scale)) {
            ++wrong;
            Report("  stub alone at %gx: special %08X NOT equal\n", value, special);
          }
        }
      }
      // CONTROLs on the same kind of runs (the divide at 1x gives the distance back: only above it).
      if (value == 1.f) continue;
      for (LodMutant& mutant : mutants) {
        ec::model_detail_scale->store(scale_bits + mutant.cell_delta);
        for (int run = 0; run < runs / 10; ++run) {
          ++mutant.runs;
          mutant.seen += block_run(mutant.block, LodDistance(&random), scale) ? 0 : 1;
        }
        ec::model_detail_scale->store(scale_bits);
      }
    }
    CHECK(wrong == 0 && product_wrong == 0);
    std::printf("22b. the stub alone: %llu runs at 14 sliders (1x, 1.0000001x, 1.25x .. 8x, 0.5x, 16x, 1e9x, 3e19x) with random distances and every special value, any MXCSR: every register, the flags, xmm0-7, MXCSR (the game's plus the multiply's sticky flags),\n"
                "    esp and the frame equal to the game's three instructions called with the multiplied distance, and the slot holds the hook's product\n",
                total);
    for (const LodMutant& mutant : mutants) {
      std::printf("    CONTROL, %s: seen in %d of %d runs\n", mutant.what, mutant.seen, mutant.runs);
      CHECK(mutant.seen > mutant.runs / 20);
    }
    VirtualFree(blocks, 0, MEM_RELEASE);
  }

  // 22c. The whole function in both copies of the DLL.
  {
    auto world = std::make_unique<LodWorld>();
    auto backup = std::make_unique<LodWorld>();
    auto result_a = std::make_unique<LodWorld>();
    auto result_b = std::make_unique<LodWorld>();
    CHECK(Address(world->call.fx_in) % 16 == 0 && Address(world->call.fx_out) % 16 == 0 && Address(world->call.fx_saved) % 16 == 0 && Address(world->call.stack) % 16 == 0);
    std::array<uint64_t, 5> paths = {};  // no LOD, one LOD returned without the distance, the forced path asked, an entry found by the distance, none found
    uint64_t wrong = 0;
    uint64_t total = 0;
    // One call of both copies on the same world: the game's with the multiplied distance, ours with the raw one, with the site written (the slider) or not (1x).
    const auto whole_run = [&](float value, uint32_t mxcsr, uint32_t distance_bits, bool count_paths) -> const char* {
      uint32_t raised = 0;
      const uint32_t multiplied = (value == 1.f ? distance_bits : LodProduct(distance_bits, OldHookReciprocal(value), mxcsr, &raised));
      LodSetUp(world.get(), &random, multiplied, mxcsr);  // in the world that runs: the model's pointers are its own
      *backup = *world;
      lod_bias = (random.Next() % 5 == 0 ? 1 + random.Next() % 3 : 0);
      LodRun(world.get(), *backup, result_a.get(), game_function, multiplied);
      LodRun(world.get(), *backup, result_b.get(), patched_function, distance_bits);
      if (count_paths) {
        const uint32_t count = Read32(backup->bytes.data() + LOD_MODEL + 0x28);
        const uint8_t* first = backup->bytes.data() + LOD_ENTRIES;
        if (count == 0) {
          ++paths[0];
        } else if (count == 1 && Read32(first + 0x20) == 0 && Read32(first + 0x2C) > 0) {
          ++paths[1];
        } else if (lod_bias != 0) {
          ++paths[2];
        } else {
          ++paths[result_a->call.out[0] != 0 ? 3 : 4];
        }
      }
      return LodDiff(*result_a, *result_b, copy_distance, raised);
    };
    const int runs = (quick ? 40'000 : 200'000);
    for (const float value : LOD_SLIDERS) {
      CHECK(LodSlider(value));
      for (int run = 0; run < runs; ++run) {
        ++total;
        if (const char* what = whole_run(value, ReloadMxcsr(&random, run), LodDistance(&random), true); what[0] != 0) {
          ++wrong;
          Report("  PickLod at %gx: run %d: %s differs\n", value, run, what);
        }
      }
    }
    CHECK(wrong == 0 && std::ranges::all_of(paths, [](uint64_t count) { return count > 100; }));
    std::printf("22c. the whole function in both copies of the DLL, %llu calls at 14 sliders on random models (no LOD %llu, one LOD without the distance %llu, the forced path %llu, an entry found %llu,\n"
                "    none found %llu): every register, the flags, x87 / MXCSR / xmm, esp, the model and the stack equal\n",
                total, paths[0], paths[1], paths[2], paths[3], paths[4]);
    // CONTROLs in the page: a wrong stub must be seen by these runs too (the page read-write meanwhile).
    DWORD protection;
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection) != FALSE);
    CHECK(LodSlider(3.f));
    auto* page_stub_bytes = ec::page + ec::MODEL_DETAIL_PAGE;
    const uint32_t scale_bits = ec::model_detail_scale->load();
    for (const LodMutant& mutant : LodMutants()) {
      if (!mutant.whole) continue;
      std::array<uint8_t, 42> saved;
      std::memcpy(saved.data(), page_stub_bytes, saved.size());
      for (const auto& [at, edit] : mutant.edits) std::copy(edit.begin(), edit.end(), page_stub_bytes + at);
      ec::model_detail_scale->store(scale_bits + mutant.cell_delta);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      int seen = 0;
      constexpr int CONTROL_RUNS = 4000;
      for (int run = 0; run < CONTROL_RUNS; ++run) seen += whole_run(3.f, ReloadMxcsr(&random, run), LodDistance(&random), false)[0] != 0 ? 1 : 0;
      std::memcpy(page_stub_bytes, saved.data(), saved.size());
      ec::model_detail_scale->store(scale_bits);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      std::printf("    CONTROL in the page, %s: %d of %d calls seen as different\n", mutant.what, seen, CONTROL_RUNS);
      CHECK(seen > CONTROL_RUNS / 20);
    }
    CHECK(std::memcmp(page_stub_bytes, ec::MODEL_DETAIL_STUB.data(), 18) == 0 && std::memcmp(page_stub_bytes + 22, ec::MODEL_DETAIL_STUB.data() + 22, 20) == 0);
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READ, &protection) != FALSE);
    CHECK(LodSlider(1.f));
  }

  // 22d. The slider moved while 4 threads call the function (the patched copy's).
  {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> calls = 0;
    constexpr std::array<float, 4> ABOVE = {2.f, 3.f, 4.f, 8.f};
    // The index each slider picks for the distance squared 64 (1, 4, 9 .. 100 are the LODs'): 8, 4, 2, 2, 1 for 1x, 2x, 3x, 4x, 8x; the set the threads may see.
    const auto index_for = [](float value) {
      const float scaled = (value == 1.f ? 64.f : OldHookMath(64.f, OldHookReciprocal(value)));
      uint32_t index = 0;
      while (index < 10 && static_cast<float>((index + 1) * (index + 1)) <= scaled) ++index;
      return index;
    };
    uint32_t allowed = 1u << index_for(1.f);
    for (const float value : ABOVE) allowed |= 1u << index_for(value);
    const auto pick = reinterpret_cast<LodPick>(patched_function);
    const auto game_pick = reinterpret_cast<LodPick>(game_function);
    lod_bias = 0;
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&] {
        auto model = std::make_unique<LodSimpleModel>();
        LodSimpleFill(model.get());
        uint64_t mine = 0;
        while (!stop.load()) {
          for (int repeat = 0; repeat < 64; ++repeat) {
            void* next = nullptr;
            const uint32_t index = LodSimpleIndex(*model, pick(model->bytes.data() + LOD_MODEL, nullptr, 64.f, &next));
            if (((allowed >> index) & 1) == 0) ++bad;
          }
          mine += 64;
        }
        calls += mine;
      });
    }
    auto main_model = std::make_unique<LodSimpleModel>();
    LodSimpleFill(main_model.get());
    uint64_t main_wrong = 0;
    int busy = 0;
    const auto main_call = [&](float value) {
      void* next = nullptr;
      main_wrong += LodSimpleIndex(*main_model, pick(main_model->bytes.data() + LOD_MODEL, nullptr, 64.f, &next)) != index_for(value) ? 1 : 0;
      void* next_game = nullptr;
      main_wrong += LodSimpleIndex(*main_model, game_pick(main_model->bytes.data() + LOD_MODEL, nullptr, 64.f, &next_game)) != index_for(1.f) ? 1 : 0;  // the game's copy never scales
    };
    const ec::Site& model_site = ec::sites[ec::MODEL_DETAIL_INDEX];
    uint64_t float_only = 0;
    for (int switches = 0; switches < 200; ++switches) {
      const float value = (switches % 2 == 0 ? ABOVE[(switches / 2) % ABOVE.size()] : 1.f);
      int tries = 0;
      do {
        twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, value);
        ++tries;
      } while ((((ec::written >> ec::MODEL_DETAIL_INDEX) & 1) != 0) != (value != 1.f) && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
      CHECK((((ec::written >> ec::MODEL_DETAIL_INDEX) & 1) != 0) == (value != 1.f));
      const auto& expected_bytes = (value != 1.f ? model_site.jump : model_site.game);
      CHECK(std::memcmp(model_site.at, expected_bytes.data(), ec::SITE_SIZE) == 0);
      main_call(value);
      // Above 1x, the float alone: no write, the site and the written mask as they were.
      if (value != 1.f) {
        const uint32_t before = ec::written;
        for (const float other : ABOVE) {
          twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, other);
          CHECK(ec::written == before && std::memcmp(model_site.at, model_site.jump.data(), ec::SITE_SIZE) == 0);
          CHECK(ec::model_detail_scale->load() == std::bit_cast<uint32_t>(OldHookReciprocal(other)));
          main_call(other);
          ++float_only;
        }
      }
    }
    CHECK(LodSlider(3.f));
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("22d. 200 switches 1x <-> above 1x (%d waited for a thread inside the site) and %llu float-only moves while 4 threads made %llu calls: %llu results outside the sliders' set, %llu wrong calls of the\n"
                "    main thread (the slider's own entry after each move, the game's copy never scaled)\n",
                busy, float_only, calls.load(), bad.load(), main_wrong);
    CHECK(bad.load() == 0 && main_wrong == 0 && calls.load() > 100'000);
    CHECK(LodSlider(1.f));
  }

  // 22e. A parked thread at each byte of the site (refused at +1 .. +4, written at the others) and of the stub (written at each).
  {
    std::atomic<bool> stop = false;
    std::thread parked([&] {
      while (!stop.load()) Sleep(1);
    });
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, GetThreadId(parked.native_handle()));
    CHECK(thread != nullptr);
    CHECK(LodSlider(1.f));
    int wrong = 0;
    bool thread_ok = true;
    const auto toggle_with_thread_at = [&](const uint8_t* where) {
      thread_ok &= (SuspendThread(thread) != static_cast<DWORD>(-1));
      CONTEXT saved = {.ContextFlags = CONTEXT_CONTROL};
      thread_ok &= (GetThreadContext(thread, &saved) != FALSE);
      CONTEXT moved = saved;
      moved.Eip = Address(where);
      thread_ok &= (SetThreadContext(thread, &moved) != FALSE);
      const uint32_t before = ec::written;
      const float toggled = ((((before >> ec::MODEL_DETAIL_INDEX) & 1) != 0) ? 1.f : 2.f);
      twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, toggled);
      const bool changed = (((ec::written ^ before) >> ec::MODEL_DETAIL_INDEX) & 1) != 0;
      thread_ok &= (SetThreadContext(thread, &saved) != FALSE);
      ResumeThread(thread);
      return changed;
    };
    const uint8_t* site = ec::sites[ec::MODEL_DETAIL_INDEX].at;
    for (int offset = -1; offset <= 7; ++offset) {
      const bool changed = toggle_with_thread_at(site + offset);
      wrong += (changed != !(offset > 0 && offset < static_cast<int>(ec::SITE_SIZE))) ? 1 : 0;
    }
    for (size_t offset = 0; offset < ec::MODEL_DETAIL_STUB.size(); ++offset) wrong += toggle_with_thread_at(page_stub() + offset) ? 0 : 1;
    CHECK(LodSlider(1.f));
    stop = true;
    parked.join();
    CloseHandle(thread);
    CHECK(thread_ok && wrong == 0);
    std::printf("22e. a parked thread at the site -1 .. +7: refused at +1 .. +4 and written at the others; at each of the stub's %zu bytes: written\n", ec::MODEL_DETAIL_STUB.size());
  }

  // 22f. The status words, another mod's bytes in the site, the pattern not found, the missing restore.
  {
    const auto text = [&](float value) { const char* words = twattila::engine_code::ModelDetailStatus(value); return std::string(words != nullptr ? words : "(none)"); };
    const auto prepare_again = [&] {
      for (ec::Site& site : ec::sites) site.at = nullptr;
      ec::prepared = false;
      ec::written = 0;
      ec::Prepare(reinterpret_cast<HMODULE>(patched), *build.code);
    };
    constexpr std::string_view HOOKED = "object detail hooked";
    constexpr std::string_view AT_ONE = "object detail at 1x: the game's LOD pick left alone";
    constexpr std::string_view NOT_FOUND = "object detail: the game's LOD code was not found";
    constexpr std::string_view NOT_YET = "object detail: not hooked yet";
    CHECK(LodSlider(2.f));
    CHECK(text(2.f) == HOOKED && text(1.f) == HOOKED);  // while the site is written: hooked, whatever the slider says now (the next present restores it)
    CHECK(LodSlider(1.f));
    CHECK(text(1.f) == AT_ONE && text(2.f) == NOT_YET);  // found, above 1x and not written (a write refused, say): its own words
    ec::prepared = false;
    CHECK(text(2.f) == "(none)" && text(1.f) == AT_ONE);
    ec::prepared = true;
    // The row at 1x writes nothing: the game's bytes, found.
    CHECK(std::memcmp(ec::sites[ec::MODEL_DETAIL_INDEX].at, ec::sites[ec::MODEL_DETAIL_INDEX].game.data(), ec::SITE_SIZE) == 0);
    // Another mod's bytes in the site: left alone, named, the status says so, nothing written over them.
    {
      const std::array<uint8_t, ec::SITE_SIZE> hook = {0xE9, 0x10, 0x20, 0x30, 0x40};
      uint8_t* site_at = ec::sites[ec::MODEL_DETAIL_INDEX].at;
      DWORD protection;
      VirtualProtect(site_at, ec::SITE_SIZE, PAGE_EXECUTE_READWRITE, &protection);
      std::memcpy(site_at, hook.data(), ec::SITE_SIZE);
      VirtualProtect(site_at, ec::SITE_SIZE, protection, &protection);
      twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 2.f);
      CHECK(((ec::written >> ec::MODEL_DETAIL_INDEX) & 1) == 0 && ((ec::found >> ec::MODEL_DETAIL_INDEX) & 1) == 0 && std::memcmp(site_at, hook.data(), ec::SITE_SIZE) == 0);
      CHECK(log_lines.back().find("model detail: left alone (its bytes changed since the start: another mod)") != std::string::npos);
      CHECK(text(2.f) == NOT_FOUND && text(1.f) == AT_ONE);
      VirtualProtect(site_at, ec::SITE_SIZE, PAGE_EXECUTE_READWRITE, &protection);
      std::memcpy(site_at, ec::sites[ec::MODEL_DETAIL_INDEX].game.data(), ec::SITE_SIZE);
      VirtualProtect(site_at, ec::SITE_SIZE, protection, &protection);
      prepare_again();
      CHECK(((ec::found >> ec::MODEL_DETAIL_INDEX) & 1) != 0 && LodSlider(2.f) && LodSlider(1.f));
    }
    // One byte of the function's pattern changed (another build, or a mod's rewrite elsewhere in it): not found, never written, the status says so above 1x.
    {
      uint8_t* changed = patched + function_rva + 0x12;
      *changed ^= 1;
      prepare_again();
      CHECK(ec::sites[ec::MODEL_DETAIL_INDEX].at == nullptr && ((ec::found >> ec::MODEL_DETAIL_INDEX) & 1) == 0 && ec::sites[ec::MODEL_DETAIL_INDEX].at == nullptr);
      twattila::engine_code::Update(false, ATTILA_GAME, false, false, false, false, false, false, 2.f);
      CHECK(ec::written == 0 && text(2.f) == NOT_FOUND && text(1.f) == AT_ONE);
      *changed ^= 1;
      prepare_again();
      CHECK(((ec::found >> ec::MODEL_DETAIL_INDEX) & 1) != 0 && text(2.f) == NOT_YET);
    }
    // The missing-restore CONTROL: the check that the game's own function is back at 1x (written clear, the bytes the game's, a call the game's) fails when the jump is left in.
    {
      const auto restored = [&] {
        void* next = nullptr;
        LodSimpleModel model;
        LodSimpleFill(&model);
        lod_bias = 0;
        const bool same = LodSimpleIndex(model, reinterpret_cast<LodPick>(patched_function)(model.bytes.data(), nullptr, 64.f, &next)) == LodSimpleIndex(model, reinterpret_cast<LodPick>(game_function)(model.bytes.data(), nullptr, 64.f, &next));
        return ((ec::written >> ec::MODEL_DETAIL_INDEX) & 1) == 0 && std::memcmp(ec::sites[ec::MODEL_DETAIL_INDEX].at, ec::sites[ec::MODEL_DETAIL_INDEX].game.data(), ec::SITE_SIZE) == 0 && same;
      };
      CHECK(LodSlider(3.f) && !restored());  // on: the jump is in, the picks differ
      CHECK(LodSlider(1.f) && restored());
      const uint8_t* site_at = ec::sites[ec::MODEL_DETAIL_INDEX].at;
      DWORD protection;
      VirtualProtect(const_cast<uint8_t*>(site_at), ec::SITE_SIZE, PAGE_EXECUTE_READWRITE, &protection);
      std::memcpy(const_cast<uint8_t*>(site_at), ec::sites[ec::MODEL_DETAIL_INDEX].jump.data(), ec::SITE_SIZE);  // the restore missing
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      CHECK(!restored());
      std::memcpy(const_cast<uint8_t*>(site_at), ec::sites[ec::MODEL_DETAIL_INDEX].game.data(), ec::SITE_SIZE);
      VirtualProtect(const_cast<uint8_t*>(site_at), ec::SITE_SIZE, protection, &protection);
      FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
      CHECK(restored());
    }
    std::printf("22f. status words: \"object detail hooked\" while the site is written, \"object detail at 1x: the game's LOD pick left alone\" at 1x, \"object detail: not hooked yet\" before the write; another mod's bytes in the\n"
                "    site: left alone and named, the status \"not found\"; a changed pattern byte: not found, nothing written; CONTROL: a site left written at 1x is seen by the 1x check\n");
  }

  // 22g. Timing: the game's function against ours with the site written (3x) and at 1x (the game's bytes), ns a call, the model's picks at the 5th LOD.
  {
    auto model = std::make_unique<LodSimpleModel>();
    LodSimpleFill(model.get());
    lod_bias = 0;
    const int calls = (quick ? 4'000'000 : 20'000'000);
    const auto measure = [&](LodPick pick) {
      double best = 1e9;
      volatile uintptr_t sink = 0;
      for (int round = 0; round < 7; ++round) {
        void* next = nullptr;
        const auto start = std::chrono::steady_clock::now();
        for (int call = 0; call < calls; ++call) sink = reinterpret_cast<uintptr_t>(pick(model->bytes.data() + LOD_MODEL, nullptr, 20.f + static_cast<float>(call & 1), &next));
        best = std::min(best, std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / calls);
      }
      (void)sink;
      return best;
    };
    const double game_ns = measure(reinterpret_cast<LodPick>(game_function));
    CHECK(LodSlider(1.f));
    const double one_ns = measure(reinterpret_cast<LodPick>(patched_function));
    CHECK(LodSlider(3.f));
    const double ours_ns = measure(reinterpret_cast<LodPick>(patched_function));
    CHECK(LodSlider(1.f));  // the site written is the game's bytes again: the old hook below goes over them, and they come back, with the row off
    DWORD protection;
    // The old hook on the same function at 3x (its jump over the site, then the game's bytes back).
    auto* trampoline = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    CHECK(trampoline != nullptr);
    std::memcpy(trampoline, FIRST.data(), FIRST.size());
    JumpTo(trampoline + FIRST.size(), patched + function_rva + FIRST.size());
    lod_old_original = reinterpret_cast<LodPick>(trampoline);
    lod_old_scale = OldHookReciprocal(3.f);
    uint8_t* site_at = ec::sites[ec::MODEL_DETAIL_INDEX].at;
    CHECK(VirtualProtect(site_at, ec::SITE_SIZE, PAGE_EXECUTE_READWRITE, &protection) != FALSE);
    JumpTo(site_at, reinterpret_cast<const void*>(&LodOldHook));
    const double old_ns = measure(reinterpret_cast<LodPick>(patched_function));
    std::memcpy(site_at, ec::sites[ec::MODEL_DETAIL_INDEX].game.data(), ec::SITE_SIZE);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    CHECK(VirtualProtect(site_at, ec::SITE_SIZE, protection, &protection) != FALSE);
    VirtualFree(trampoline, 0, MEM_RELEASE);
    std::printf("22g. timing, ns a call (best of 7, a hot loop; the 5th of 10 LODs): the game's %.2f, ours at 1x (the game's bytes) %.2f, ours at 3x (the stub) %.2f, the old Detours hook at 3x %.2f\n",
                game_ns, one_ns, ours_ns, old_ns);
  }
  // Detach with the site written: Uninstall puts the game's bytes back (a jump left in the game's code would run a stub nobody sets the float of).
  CHECK(LodSlider(3.f));
  twattila::engine_code::Uninstall();
  CHECK(ec::written == 0 && std::memcmp(ec::sites[ec::MODEL_DETAIL_INDEX].at, ec::sites[ec::MODEL_DETAIL_INDEX].game.data(), ec::SITE_SIZE) == 0);
  twattila::engine_code::Update(false);
  // Off, and nothing stood down: the site is still found and holds the game's bytes (the parts after this one take `found` as it stands).
  CHECK(ec::written == 0 && ((ec::found >> ec::MODEL_DETAIL_INDEX) & 1) != 0
        && std::memcmp(ec::sites[ec::MODEL_DETAIL_INDEX].at, ec::sites[ec::MODEL_DETAIL_INDEX].game.data(), ec::SITE_SIZE) == 0);
  return 0;
}
