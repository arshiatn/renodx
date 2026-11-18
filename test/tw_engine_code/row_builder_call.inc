// ====================================================================================================================
// E4: the whole row builder (+0x1906fa0; thiscall-like: ecx = this, 4 stack arguments, ret 0x10; 4 rows a call) end to end in the
// game's copy (A) against the patched copy (B), on random instance data.
// ====================================================================================================================

struct FnCall {
  uint32_t in[7];        // 0: eax ecx edx ebx esi edi ebp (ecx and ebp are set by the harness)
  uint32_t function;     // 28
  uint32_t this_ptr;     // 32
  uint32_t args[4];      // 36
  uint32_t saved_esp;    // 52
  uint32_t out[7];       // 56
  uint32_t esp_after;    // 84
  uint32_t flags_after;  // 88
  uint32_t faulted;      // 92
  alignas(16) uint8_t stack[0x2000];  // 96; E = stack + 0x1F00
  alignas(16) uint8_t fx_in[512];     // 8288
  alignas(16) uint8_t fx_out[512];    // 8800
  alignas(16) uint8_t fx_saved[512];  // 9312
};
static_assert(offsetof(FnCall, stack) == 96 && offsetof(FnCall, fx_in) == 8288 && offsetof(FnCall, fx_out) == 8800 && offsetof(FnCall, fx_saved) == 9312);
constexpr uint32_t FN_STACK_E = 0x1F00;

static FnCall* fn_call = nullptr;

__declspec(naked) void __cdecl CallFn(FnCall*) {
  __asm {
    pushad
    mov ebp, [esp + 36]
    mov [ebp + 52], esp
    fxsave [ebp + 9312]
    fxrstor [ebp + 8288]
    lea esp, [ebp + 8032]
    push dword ptr [ebp + 48]
    push dword ptr [ebp + 44]
    push dword ptr [ebp + 40]
    push dword ptr [ebp + 36]
    mov eax, [ebp + 0]
    mov edx, [ebp + 8]
    mov ebx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]
    mov ecx, [ebp + 32]
    call dword ptr [ebp + 28]
    pushfd
    pop dword ptr [ebp + 88]
    mov [ebp + 56], eax
    mov [ebp + 60], ecx
    mov [ebp + 64], edx
    mov [ebp + 68], ebx
    mov [ebp + 72], esi
    mov [ebp + 76], edi
    mov [ebp + 80], ebp
    mov [ebp + 84], esp
    fxsave [ebp + 8800]
    fxrstor [ebp + 9312]
    mov esp, [ebp + 52]
    popad
    ret
  }
}

__declspec(naked) void FnFaultLanding() {
  __asm {
    fxrstor [ebp + 9312]
    mov esp, [ebp + 52]
    popad
    ret
  }
}

LONG CALLBACK FnHandler(EXCEPTION_POINTERS* info) {
  if (fn_call == nullptr || info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
  fn_call->faulted = 1 + static_cast<uint32_t>(info->ContextRecord->Eip);
  info->ContextRecord->Ebp = Address(fn_call);
  info->ContextRecord->Eip = Address(reinterpret_cast<const void*>(&FnFaultLanding));
  return EXCEPTION_CONTINUE_EXECUTION;
}

constexpr uint32_t FN_THIS = 0x000, FN_MESH_TABLE = 0x180, FN_MESHES = 0x200, FN_RECORDS = 0x400, FN_TABLE = 0x1000, FN_INSTANCES = 0x2000, FN_VEC3 = 0x2200, FN_ROWS = 0x2400;
struct FnWorld {
  FnCall call;
  alignas(16) std::array<uint8_t, 0x3000> bytes;
};

// The probe on the game's copy: counts how many of the rows have all 20 lanes zero / tiny / normal (the stub's vector path), then goes on
// as if nothing happened (every register, the flags, the x87 and SSE state kept).
static std::atomic<uint64_t> fn_rows_fast = 0;
static std::atomic<uint64_t> fn_rows_slow = 0;
static uint32_t probe_resume = 0;
static std::array<std::atomic<uint64_t>, 20> fn_lane_bad = {};
static int fn_flavor = 0;

extern "C" void __cdecl ProbeC(const uint8_t* frame, uint32_t source_object, uint32_t xmm0_bits) {
  RowsWorld* none = nullptr;
  (void)none;
  const auto* source = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(source_object));
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
  const std::array<uint32_t, 20> lanes = {xmm0_bits, Read32(frame + 0x24), Read32(source + 0x1C), from_int, Read32(frame + 0x70), Read32(frame + 0x58),
                                          mul(Read32(frame + 0x50), Read32(frame + 0x74)), Read32(source + 0x40), mul(Read32(frame + 0x74), Read32(frame + 0x4C)),
                                          Read32(frame + 0x48), 0, Read32(frame + 0x44), Read32(source + 0x28), Read32(source + 0x2C), Read32(source + 0x30),
                                          Read32(source + 0x34), Read32(source + 0x38), Read32(source + 0x3C), Read32(second + 0x48), Read32(second + 0x4C)};
  (RowsFast(lanes) ? fn_rows_fast : fn_rows_slow)++;
  for (size_t lane = 0; lane < lanes.size() && fn_flavor == 0; ++lane) {
    const uint32_t a = lanes[lane] & 0x7FFFFFFF;
    if (a >= 0x33800000 && !(a >= 0x38800000 && a < 0x47800000)) fn_lane_bad[lane]++;
  }
}

__declspec(naked) void Probe() {
  __asm {
    pushfd
    pushad
    mov ebp, esp
    lea ebx, [esp + 36]
    sub esp, 528
    and esp, -16
    fxsave [esp]
    movd ecx, xmm0
    push ecx
    push esi
    push ebx
    call ProbeC
    add esp, 12
    fxrstor [esp]
    mov esp, ebp
    popad
    popfd
    lea eax, [esp + 0x38]
    push eax
    jmp dword ptr [probe_resume]
  }
}

uint32_t FnModerate(Random* random) {
  const uint32_t sign = (random->Next() & 1) << 31;
  switch (random->Next() % 8) {
    case 0: return sign;
    case 1: return sign | (random->Next() & 0x7FFFFF);                               // denormal
    case 2: return sign | (1 + random->Next() % 80) << 23 | (random->Next() & 0x7FFFFF);   // tiny
    default: return sign | (110 + random->Next() % 28) << 23 | (random->Next() & 0x7FFFFF);  // up to 2^10
  }
}

// The instance data the row builder reads (all inside `bytes`), by flavor: 0 lane values from the normal classes, 1 a few nasty
// ones (denormals, NaNs, infinities, huge), 2 many; the loop-driving inputs (angle, positions, scales) stay finite.
void FnRandom(FnWorld* world, Random* random, int flavor, uint32_t function, uint32_t mxcsr) {
  FnCall* call = &world->call;
  uint8_t* bytes = world->bytes.data();
  for (uint8_t& byte : world->bytes) byte = static_cast<uint8_t>(random->Next());
  const uint32_t base = Address(bytes);
  const auto put = [&](uint32_t at, uint32_t value) { Write32(bytes + at, value); };
  const auto draw = [&]() -> uint32_t {
    if (flavor == 0) return RowsFloat(random, false);
    if (flavor == 1) return random->Next() % 12 == 0 ? AdvFloat(random) : RowsFloat(random, false);
    return random->Next() % 2 == 0 ? AdvFloat(random) : RowsFloat(random, random->Next() % 3 == 0);
  };
  put(FN_THIS + 0x84, FnModerate(random));
  put(FN_THIS + 0x8C, flavor == 0 ? (0x3F800000u | (random->Next() & 0x7FFFFF)) : draw());
  put(FN_THIS + 0x90, flavor == 0 ? (0x3F000000u | (random->Next() & 0xFFFFFF)) : draw());
  put(FN_THIS + 0x134, base + FN_MESH_TABLE);
  for (uint32_t index = 0; index < 4; ++index) put(FN_MESH_TABLE + 4 * index, base + FN_MESHES + 0x60 * std::min(index, 2u));
  for (uint32_t mesh = 0; mesh < 3; ++mesh) {
    const uint32_t at = FN_MESHES + 0x60 * mesh;
    put(at + 0x1C, base + FN_RECORDS);
    put(at + 0x24, (flavor == 0 || random->Next() % 2 == 0) ? 1 + random->Next() % 5000 : random->Next());
    put(at + 0x2C, random->Next() % 4 == 0 ? 3u : random->Next() % 7);
    put(at + 0x48, draw());
    put(at + 0x4C, draw());
  }
  for (uint32_t record = 0; record < 6; ++record) {
    const uint32_t at = FN_RECORDS + 60 * record;
    const uint32_t sign = (random->Next() & 1) << 31;
    switch (random->Next() % 20) {
      case 0: put(at + 0xC, sign); break;                  // zero: the game puts 999999.0 there
      case 1: put(at + 0xC, 0x497423F0); break;           // 999999.0 itself
      default: put(at + 0xC, sign | (120 + random->Next() % 12) << 23 | (random->Next() & 0x7FFFFF)); break;  // 2^-7 .. 2^4, normal
    }
    put(at + 0x10, random->Next() % 20 == 0 ? 0u : random->Next() % 64);
    put(at + 0x14, draw());
    put(at + 0x18, draw());
    put(at + 0x20, random->Next() % 10 == 0 ? 0u : base + FN_TABLE);
    put(at + 0x28, random->Next() % 2 == 0 ? 0u : (draw() | 0x00800000u));
    put(at + 0x2C, draw());
    put(at + 0x30, draw());
  }
  for (uint32_t entry = 0; entry < 0x1000 / 4; ++entry) {
    put(FN_TABLE + 4 * entry, flavor == 0 ? ((random->Next() & 0x80000000u) | static_cast<uint32_t>(118 + random->Next() % 14) << 23 | (random->Next() & 0x7FFFFF)) : draw());
  }
  for (uint32_t instance = 0; instance < 4; ++instance) {
    const uint32_t at = FN_INSTANCES + 0x50 * instance;
    put(at + 0x00, random->Next() % 4);
    put(at + 0x04, FnModerate(random));
    put(at + 0x08, flavor == 0 ? FnModerate(random) : draw());
    put(at + 0x0C, FnModerate(random));
    for (uint32_t field = 0x10; field <= 0x18; field += 4) put(at + field, flavor == 0 ? FnModerate(random) : draw());
    put(at + 0x1C, (random->Next() & 0x80000000u) | (static_cast<uint32_t>((flavor == 0 ? 113 : 100) + random->Next() % (flavor == 0 ? 19 : 32)) << 23) | (random->Next() & 0x7FFFFF));  // |angle| up to 2^5, finite
    put(at + 0x20, random->Next() % 6);
    put(at + 0x24, FnModerate(random));
    for (uint32_t field = 0x28; field <= 0x40; field += 4) put(at + field, draw());
    put(at + 0x44, static_cast<uint32_t>(random->Next() % 3));
    put(at + 0x48, base + FN_ROWS + 0x100 * (2 * instance) + (random->Next() & 15));
    put(at + 0x4C, base + FN_ROWS + 0x100 * (2 * instance + 1) + (random->Next() & 15));
  }
  put(FN_VEC3 + 0, FnModerate(random));
  put(FN_VEC3 + 4, draw());
  put(FN_VEC3 + 8, FnModerate(random));

  const uint32_t instance = random->Next() % 4;
  const uint32_t mode_pick = random->Next() % 10;
  const uint32_t mode = (mode_pick < 6 ? 4u : (mode_pick < 7 ? 3u : (mode_pick < 8 ? 2u : (mode_pick < 9 ? 1u : 0u))));
  const uint32_t at = FN_INSTANCES + 0x50 * instance;
  if (mode == 1 || mode == 3) put(at + 0x40, (random->Next() % 4 == 0 ? 0u : 0x3F800000u) | (random->Next() & 0x007FFFFF) | (random->Next() % 3) << 23);  // 1.0 or above (and now and then below: the early exit)
  if (mode == 2 && random->Next() % 4 == 0) put(at + 0x40, 0x3F800000);  // equal: the early exit
  call->function = function;
  call->this_ptr = base + FN_THIS;
  call->args[0] = base + at;
  call->args[1] = base + FN_VEC3;
  call->args[2] = FnModerate(random);  // a float the function subtracts in its angle maths (not a dead argument)
  call->args[3] = mode;
  for (uint32_t& value : call->in) value = random->Next();
  call->faulted = 0;
  for (uint8_t& byte : call->stack) byte = static_cast<uint8_t>(random->Next());
  // machine state: x87 control word mostly the game's own, random registers; MXCSR from the caller
  uint8_t* fx = call->fx_in;
  for (int index = 0; index < 512; ++index) fx[index] = static_cast<uint8_t>(random->Next());
  const auto put16 = [&](int index, uint32_t value) { fx[index] = static_cast<uint8_t>(value); fx[index + 1] = static_cast<uint8_t>(value >> 8); };
  put16(0, random->Next() % 3 == 0 ? (0x7F | (random->Next() & 0x1F00)) : 0x037F);
  put16(2, 0);              // an empty x87 stack (the function pushes up to 3 values: a stack overflow would make QNaNs, equal in both)
  fx[4] = 0;
  put16(6, 0);
  Write32(fx + 24, mxcsr);
  for (int reg = 0; reg < 8; ++reg) std::memset(fx + 32 + 16 * reg + 10, 0, 6);
}

struct FnDiff {
  const char* what = "";
  bool both_faulted = false;
  int below_lowest = 0x2000, below_highest = -0x10000;
  bool xmm_1_7_differ = false;
  uint32_t mxcsr_out = 0;
};

bool FnEqual(const FnWorld& world, const FnWorld& a, const FnWorld& b, FnDiff* diff) {
  *diff = {};
  if (a.call.faulted != 0 || b.call.faulted != 0) {
    diff->both_faulted = a.call.faulted != 0 && b.call.faulted != 0;
    diff->what = (diff->both_faulted ? "" : "ONE of the copies faulted");
    return diff->both_faulted;
  }
  const uint32_t stack_e = Address(world.call.stack) + FN_STACK_E;
  for (uint32_t at = 0; at < FN_STACK_E - 0x14; ++at) {
    if (a.call.stack[at] == b.call.stack[at]) continue;
    diff->below_lowest = std::min<int>(diff->below_lowest, static_cast<int>(at) - static_cast<int>(FN_STACK_E));
    diff->below_highest = std::max<int>(diff->below_highest, static_cast<int>(at) - static_cast<int>(FN_STACK_E));
  }
  diff->xmm_1_7_differ = std::memcmp(a.call.fx_out + 176, b.call.fx_out + 176, 112) != 0;
  std::memcpy(&diff->mxcsr_out, a.call.fx_out + 24, 4);
  if (std::memcmp(a.call.out, b.call.out, sizeof(a.call.out)) != 0) diff->what = "registers";
  else if (a.call.esp_after != b.call.esp_after || a.call.esp_after != stack_e) diff->what = "esp";
  else if (((a.call.flags_after ^ b.call.flags_after) & 0x8D5) != 0) diff->what = "flags";
  else if (std::memcmp(a.call.fx_out, b.call.fx_out, 160) != 0) diff->what = (std::memcmp(a.call.fx_out + 24, b.call.fx_out + 24, 4) != 0 ? "MXCSR" : "x87 state");
  else if (std::memcmp(a.call.fx_out + 160, b.call.fx_out + 160, 16) != 0) diff->what = "xmm0";
  else if (std::memcmp(a.call.stack + FN_STACK_E - 0x10, b.call.stack + FN_STACK_E - 0x10, 0x20) != 0) diff->what = "the caller's argument slots and above";
  else if (a.bytes != b.bytes) diff->what = "memory";
  return diff->what[0] == 0;
}

void RunFn(FnWorld* world, const FnWorld& backup, FnWorld* out) {
  *world = backup;
  world->call.faulted = 0;
  fn_call = &world->call;
  CallFn(&world->call);
  fn_call = nullptr;
  *out = *world;
}

int RunFnTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  const uint32_t region_rva = build.code->half_rows_rva;
  const uint32_t function_rva = region_rva - 0x571;
  CHECK(game[function_rva] == 0x83 && game[function_rva + 1] == 0xEC && game[function_rva + 2] == 0x5C && patched[function_rva] == 0x83);
  const uint32_t game_function = Address(game + function_rva);
  const uint32_t patched_function = Address(patched + function_rva);
  auto world = std::make_unique<FnWorld>();
  auto backup = std::make_unique<FnWorld>();
  auto a = std::make_unique<FnWorld>();
  auto b = std::make_unique<FnWorld>();
  auto c = std::make_unique<FnWorld>();
  std::printf("FnWorld at %p, sizeof %zu, fx aligned %d\n", static_cast<void*>(world.get()), sizeof(FnWorld), Address(world->call.fx_in) % 16 == 0);
  CHECK(Address(world->call.fx_in) % 16 == 0 && Address(world->call.fx_out) % 16 == 0 && Address(world->call.fx_saved) % 16 == 0 && Address(world->call.stack) % 16 == 0);
  const PVOID handler = AddVectoredExceptionHandler(1, FnHandler);
  CHECK(handler != nullptr);

  // The probe on the game's copy at the region (it runs the region's first 5 bytes itself, then the game's code).
  uint8_t* probe_at = game + region_rva;
  std::array<uint8_t, 5> probe_saved;
  std::memcpy(probe_saved.data(), probe_at, 5);
  probe_resume = Address(probe_at + 5);
  JumpTo(probe_at, reinterpret_cast<const void*>(&Probe));

  // Variants of the patched copy: all the CPU sites ("as shipped") and the half rows site alone (the game's own bytes at the others).
  const std::array<bool, std::tuple_size_v<decltype(ec::sites)>> found_before = [&] {
    std::array<bool, std::tuple_size_v<decltype(ec::sites)>> result = {};
    for (size_t index = 0; index < ec::sites.size(); ++index) result[index] = ec::sites[index].at != nullptr && std::memcmp(ec::sites[index].at, ec::sites[index].jump.data(), 5) == 0;
    return result;
  }();
  CHECK(found_before[8]);
  const auto set_variant = [&](bool all) {
    for (size_t index = 0; index < ec::sites.size(); ++index) {
      if (!found_before[index] || index == 8) continue;
      const auto& bytes = all ? ec::sites[index].jump : ec::sites[index].game;
      std::memcpy(ec::sites[index].at, bytes.data(), 5);
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  };

  // A watchdog: a hang prints where the main thread is and ends the process.
  static std::atomic<uint64_t> progress = 0;
  static std::atomic<bool> done = false;
  HANDLE main_thread = nullptr;
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
  std::thread watchdog([&] {
    uint64_t last = ~0ull;
    int same = 0;
    while (!done.load()) {
      Sleep(1000);
      const uint64_t now = progress.load();
      same = (now == last ? same + 1 : 0);
      last = now;
      if (same >= 5) {
        SuspendThread(main_thread);
        CONTEXT context = {.ContextFlags = CONTEXT_FULL | CONTEXT_EXTENDED_REGISTERS};
        GetThreadContext(main_thread, &context);
        for (int reg = 0; reg < 8; ++reg) {
          const auto* x = reinterpret_cast<const float*>(context.ExtendedRegisters + 160 + 16 * reg);
          std::printf("WATCHDOG: xmm%d = %g %g %g %g (bits %08X)\n", reg, x[0], x[1], x[2], x[3], *reinterpret_cast<const uint32_t*>(x));
        }
        {
          const uint8_t* bytes = backup->bytes.data();
          const uint32_t base = Address(bytes);
          const uint32_t instance = backup->call.args[0] - base;
          std::printf("WATCHDOG: mode %u instance +%X: mesh %u angle %08X record %u pos %08X %08X %08X +0x24 %08X; vec3 %08X %08X %08X; fcw %04X mxcsr %08X\n", backup->call.args[3], instance,
                      Read32(bytes + instance), Read32(bytes + instance + 0x1C), Read32(bytes + instance + 0x20), Read32(bytes + instance + 4), Read32(bytes + instance + 8),
                      Read32(bytes + instance + 0xC), Read32(bytes + instance + 0x24), Read32(bytes + FN_VEC3), Read32(bytes + FN_VEC3 + 4), Read32(bytes + FN_VEC3 + 8),
                      backup->call.fx_in[0] | backup->call.fx_in[1] << 8, Read32(backup->call.fx_in + 24));
        }
        std::printf("WATCHDOG: no progress for 5 s at round %llu: Eip %08X (game copy %08X, patched %08X, probe %p) Esp %08X Eax %08X Ecx %08X Edx %08X Esi %08X Edi %08X Ebp %08X\n", now, static_cast<uint32_t>(context.Eip),
                    game_function, patched_function, reinterpret_cast<void*>(&Probe), static_cast<uint32_t>(context.Esp), static_cast<uint32_t>(context.Eax), static_cast<uint32_t>(context.Ecx), static_cast<uint32_t>(context.Edx), static_cast<uint32_t>(context.Esi), static_cast<uint32_t>(context.Edi), static_cast<uint32_t>(context.Ebp));
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 3);
      }
    }
  });
  // CONTROL: the stub with a wrong constant (a normal value's half + 0x4001 instead of + 0x4000) must be caught by this harness.
  {
    uint8_t* constant = ec::page + ec::HALF_ROWS_PAGE + ec::HALF_ROWS_CONSTANTS_AT + 0x50;
    DWORD old;
    CHECK(VirtualProtect(constant, 16, PAGE_EXECUTE_READWRITE, &old) != FALSE);
    CHECK(Read32(constant) == 0x4000);
    for (int lane = 0; lane < 4; ++lane) Write32(constant + 4 * lane, 0x4001);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    Random control_random = {.state = 0xC0DE};
    int seen = 0;
    for (int round = 0; round < 3000; ++round) {
      FnRandom(world.get(), &control_random, 0, game_function, 0x1F80);
      *backup = *world;
      RunFn(world.get(), *backup, a.get());
      backup->call.function = patched_function;
      RunFn(world.get(), *backup, b.get());
      FnDiff diff;
      if (!FnEqual(*world, *a, *b, &diff)) ++seen;
    }
    for (int lane = 0; lane < 4; ++lane) Write32(constant + 4 * lane, 0x4000);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    std::printf("E4 control (the stub's constant 0x4000 -> 0x4001): %d of 3000 calls seen as different\n", seen);
    CHECK(seen > 500);
  }
  const int calls = quick ? 60000 : 400000;
  uint64_t total = 0, wrong_all = 0, wrong_only = 0, both_fault = 0, one_fault = 0, early_exit_like = 0, xmm_differ = 0, dead_low = 0;
  int worst_low = 0x2000, worst_high = -1;
  std::array<uint64_t, 6> flags_new = {};
  uint64_t rows_before_fast = 0, rows_before_slow = 0;
  Random random = {.state = 0xF00D};
  for (int round = 0; round < calls; ++round) {
    progress = round + 1;
    if (round < 3) std::printf("   E4 round %d starting\n", round);
    const uint32_t mxcsr =0x1F80 | (random.Next() & 3) << 13 | (random.Next() % 4 == 0 ? 1u << 6 : 0u) | (random.Next() % 4 == 0 ? 1u << 15 : 0u) | ((round & 1) != 0 ? (random.Next() & 0x3F) : 0);
    const int flavor = (round % 4 < 2 ? 0 : (round % 4 == 2 ? 1 : 2));
    FnRandom(world.get(), &random, flavor, game_function, mxcsr);
    *backup = *world;
    backup->call.function = game_function;
    fn_flavor = flavor;
    const uint64_t fast_before = fn_rows_fast.load(), slow_before = fn_rows_slow.load();
    RunFn(world.get(), *backup, a.get());
    rows_before_fast += fn_rows_fast.load() - fast_before;
    rows_before_slow += fn_rows_slow.load() - slow_before;
    if (fn_rows_fast.load() == fast_before && fn_rows_slow.load() == slow_before) ++early_exit_like;
    for (const bool all : {true, false}) {
      set_variant(all);
      backup->call.function = patched_function;
      RunFn(world.get(), *backup, all ? b.get() : c.get());
    }
    ++total;
    if (round < 0) {
      for (int word = 0; word < 36; ++word) {
        const uint32_t at = FN_STACK_E - 0x14 - 4 * word;
        std::printf("   round %d stack E%+d: game %08X ours %08X (only half rows %08X)\n", round, static_cast<int>(at) - static_cast<int>(FN_STACK_E), Read32(a->call.stack + at), Read32(b->call.stack + at),
                    Read32(c->call.stack + at));
      }
    }
    FnDiff diff_all, diff_only;
    const bool same_all = FnEqual(*world, *a, *b, &diff_all);
    const bool same_only = FnEqual(*world, *a, *c, &diff_only);
    if (diff_all.both_faulted) ++both_fault;
    if (!same_all && std::strstr(diff_all.what, "ONE") != nullptr) ++one_fault;
    if (diff_all.xmm_1_7_differ) ++xmm_differ;
    if (diff_all.below_highest > -0x10000) {
      ++dead_low;
      worst_low = std::min(worst_low, diff_all.below_lowest);
      worst_high = std::max(worst_high, diff_all.below_highest);
    }
    if (!same_all) {
      ++wrong_all;
      Report("  E4: round %d: as shipped (all sites): %s differs (mode %u, flavor %d)\n", round, diff_all.what, backup->call.args[3], flavor);
    }
    if (!same_only) {
      ++wrong_only;
      Report("  E4: round %d: half rows site alone: %s differs (mode %u, flavor %d)\n", round, diff_only.what, backup->call.args[3], flavor);
    }
    (void)flags_new;
  }
  set_variant(true);
  done = true;
  watchdog.join();
  CloseHandle(main_thread);
  RemoveVectoredExceptionHandler(handler);
  std::memcpy(probe_at, probe_saved.data(), 5);
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  std::printf("E4 whole row builder: %llu calls (%llu left early: no rows), the rows the game's copy built: %llu with every lane zero / tiny / normal (the stub's vector path), %llu with a nasty lane (the game's code)\n",
              total, early_exit_like, rows_before_fast, rows_before_slow);
  std::printf("   NOT equal, patched copy as shipped (all CPU sites): %llu; half rows site alone: %llu; both copies faulted %llu; only one %llu; xmm1-7 differ at return in %llu (dead by the ABI); stack bytes below the call's frame that differ: %s\n",
              wrong_all, wrong_only, both_fault, one_fault, xmm_differ, dead_low != 0 ? std::format("{} calls, E{:+d} .. E{:+d}", dead_low, worst_low, worst_high).c_str() : "none");
  std::printf("   rows by the lane that kept them off the vector path (a lane can be one of several), lanes 0..19:");
  for (const auto& count : fn_lane_bad) std::printf(" %llu", count.load());
  std::printf("\n");
  (void)flags_new;
  return (wrong_all != 0 || wrong_only != 0 || one_fault != 0) ? 1 : 0;
}
