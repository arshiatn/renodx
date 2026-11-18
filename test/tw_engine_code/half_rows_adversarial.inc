// The half rows' adversarial parts (Sonnet 5.5 review 3 of EHALF), included inside tests.cpp's anonymous namespace; run as
// `tests.exe <dll> quick|full adv e1|e2|e3|e4|e7a|e7b|e7d|e8|all` (e1 MXCSR + x87 + flags, e2 faults at page edges, e3 the
// frame below the exit esp, e4 the whole row builder end to end, e7a pointer soup, e7b PausedWrite refusals, e7d thread stress
// through the site, e8 one lane at a time on the vector path with the others random).

// PausedWrite as the tests call it: the sites to hold their jumps, False when a thread stood inside one.
bool PausedWriteMask(uint32_t patched) {
  uint32_t stood_down;
  return twattila::engine_code::internal::PausedWrite(patched, &stood_down);
}

// ---- full machine state entry / return: FXRSTOR loads x87 (control, status, tags, ST0-7), MXCSR and xmm0-7 from fx_in; the
// return FXSAVEs them to fx_out and puts the harness's own state back ----
__declspec(naked) void __cdecl EnterRows3(RowsCall*) {
  __asm {
    pushad
    mov ebp, [esp + 36]
    mov [ebp + 32], esp
    fxsave [ebp + 1872]
    fxrstor [ebp + 848]
    lea esp, [ebp + 560]
    mov eax, [ebp + 0]
    mov ecx, [ebp + 4]
    mov edx, [ebp + 8]
    mov ebx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]
    jmp dword ptr [ebp + 28]
  }
}

__declspec(naked) void RowsReturn3() {
  __asm {
    mov [esp - 8], eax
    lea eax, [esp - 544]
    pushfd
    pop dword ptr [eax + 76]
    fxsave [eax + 1360]
    mov [eax + 44], ecx
    mov [eax + 48], edx
    mov [eax + 52], ebx
    mov [eax + 56], esi
    mov [eax + 60], edi
    mov [eax + 64], ebp
    mov ecx, [esp - 8]
    mov [eax + 40], ecx
    mov [eax + 36], esp
    fxrstor [eax + 1872]
    mov esp, [eax + 32]
    popad
    ret
  }
}

void RunRows3(uint32_t block, RowsWorld* world, const RowsWorld& backup, RowsWorld* out) {
  *world = backup;
  world->call.block = block;
  EnterRows3(&world->call);
  *out = *world;
}

// A float's bits for the nastiest classes (any lane), `RowsFloat` for the normal classes.
uint32_t AdvFloat(Random* random) {
  const uint32_t sign = (random->Next() & 1) << 31;
  switch (random->Next() % 16) {
    case 0: return sign;
    case 1: return sign | (random->Next() & 0x7FFFFF);
    case 2: return sign | 0x00800000 | (random->Next() & 0x7FFFFF);
    case 3: return sign | ((random->Next() % 60) + 1) << 23 | (random->Next() & 0x7FFFFF);
    case 4: return sign | 0x7F800000 | (random->Next() & 0x7FFFFF);
    case 5: return sign | (200 + random->Next() % 55) << 23 | (random->Next() & 0x7FFFFF);
    case 6: return random->Next();
    case 7: return sign | 0x7FC00000;
    default: return RowsFloat(random, random->Next() % 4 == 0);
  }
}

// Two operands whose product lands around the conversion's class edges (underflow, half denormals, normals, overflow), with
// denormal operands now and then (DAZ changes their product).
void AdvProduct(Random* random, uint32_t* a, uint32_t* b) {
  const uint32_t sign_a = (random->Next() & 1) << 31;
  const uint32_t sign_b = (random->Next() & 1) << 31;
  const int target = static_cast<int>(random->Next() % 190) - 160;  // the product's exponent (unbiased), -160 .. 29
  int exponent_a = static_cast<int>(random->Next() % 254) + 1;
  int exponent_b = 127 + target - (exponent_a - 127);
  if (exponent_b < 0 || exponent_b > 254) exponent_b = (exponent_b < 0 ? 0 : 254);
  if (random->Next() % 8 == 0) exponent_a = 0;
  if (random->Next() % 8 == 0) exponent_b = 0;
  *a = sign_a | static_cast<uint32_t>(exponent_a) << 23 | (random->Next() & 0x7FFFFF);
  *b = sign_b | static_cast<uint32_t>(exponent_b) << 23 | (random->Next() & 0x7FFFFF);
}

// FXSAVE image: arbitrary x87 control / status / tag words and registers, the xmm registers, `mxcsr`.
void AdvFx(RowsCall* call, Random* random, uint32_t mxcsr) {
  uint8_t* fx = call->fx_in;
  for (int at = 0; at < 512; ++at) fx[at] = static_cast<uint8_t>(random->Next());
  const auto put16 = [&](int at, uint32_t value) { fx[at] = static_cast<uint8_t>(value); fx[at + 1] = static_cast<uint8_t>(value >> 8); };
  put16(0, 0x7F | (random->Next() & 0x1F00));  // control word: every exception masked, any precision / rounding / infinity bit
  put16(2, random->Next() & 0x7F3F);            // status word: any TOP, condition codes and sticky flags, no ES / B
  put16(6, random->Next() & 0x7FF);
  Write32(fx + 24, mxcsr);
  for (int reg = 0; reg < 8; ++reg) std::memset(fx + 32 + 16 * reg + 10, 0, 6);
  std::memcpy(fx + 160, &call->xmm0_in, 4);  // xmm0's low lane is the region's first float
  std::memcpy(call->xmm0_full, fx + 160, 16);
}

// A world for the state tests: pointers at odd addresses, floats by `mode` (0: all lanes normal classes, 1: a few nasty ones, 2:
// all lanes nasty), the product operands by AdvProduct, the integer lane with extremes now and then.
void AdvRandom(RowsWorld* world, Random* random, int mode, uint32_t mxcsr) {
  RowsRandom(world, random, 0);
  RowsCall* call = &world->call;
  uint8_t* frame = call->frame + 0x100;
  uint8_t* arena = world->arena.data();
  uint8_t* source = arena + (random->Next() & 15);
  uint8_t* second = arena + 0x100 + (random->Next() & 15);
  uint8_t* row = arena + 0x220 + (random->Next() & 15);
  const auto draw = [&]() -> uint32_t {
    if (mode == 0) return RowsFloat(random, false);
    if (mode == 1) return random->Next() % 20 == 0 ? AdvFloat(random) : RowsFloat(random, false);
    return AdvFloat(random);
  };
  call->xmm0_in = draw();
  for (const uint32_t at : {0x24u, 0x44u, 0x48u, 0x58u, 0x70u}) Write32(frame + at, draw());
  uint32_t a, b, c, d;
  AdvProduct(random, &a, &b);
  AdvProduct(random, &c, &d);
  if (mode == 2 && random->Next() % 2 == 0) {
    a = draw(), b = draw(), c = draw(), d = draw();
  }
  Write32(frame + 0x50, a);
  Write32(frame + 0x74, b);
  Write32(frame + 0x4C, random->Next() % 3 == 0 ? c : d);
  if (random->Next() % 3 == 0) Write32(frame + 0x74, c);  // second product's operand too: x * y with x the first product's operand
  static constexpr std::array<int32_t, 12> INTS = {0, 1, -1, 65519, 65520, -65519, -65520, 0x7FFFFFFF, static_cast<int32_t>(0x80000000), 16777217, 16777216, -16777217};
  Write32(frame + 0x20, random->Next() % 4 == 0 ? static_cast<uint32_t>(INTS[random->Next() % INTS.size()]) : static_cast<uint32_t>(static_cast<int32_t>(random->Next() % 131071) - 65535));
  for (const uint32_t at : ROWS_SOURCE_FLOATS) Write32(source + at, draw());
  Write32(second + 0x48, draw());
  Write32(second + 0x4C, draw());
  call->in[4] = Address(source);
  Write32(frame + 0x5C, Address(second));
  Write32(frame + 0x68, Address(row));
  AdvFx(call, random, mxcsr);
}

struct AdvDiff {
  const char* what = "";
  bool took_fast = false;
  int below_lowest = 0x100;  // the lowest / highest byte of the frame below F (index into frame[0..0x100)) where A and B differ
  int below_highest = -1;
  uint32_t mxcsr_out_a = 0;
};

// Everything the game's region leaves: registers, esp, the whole x87 state, MXCSR, xmm0 (all 128 bits), the frame from F up, the
// arena; the game's code leaves xmm1 to xmm7 as they were. The frame below F is only measured.
bool AdvEqual(const RowsWorld& world, const RowsWorld& backup, const RowsWorld& a, const RowsWorld& b, AdvDiff* diff, bool compare_flags = false) {
  const uint32_t frame_top = Address(world.call.frame) + 0x100;
  *diff = {};
  diff->took_fast = std::memcmp(a.call.frame + 0xF0, b.call.frame + 0xF0, 16) != 0;
  for (int at = 0; at < 0x100; ++at) {
    if (a.call.frame[at] == b.call.frame[at]) continue;
    diff->below_lowest = std::min(diff->below_lowest, at);
    diff->below_highest = std::max(diff->below_highest, at);
  }
  std::memcpy(&diff->mxcsr_out_a, a.call.fx_out + 24, 4);
  if (std::memcmp(a.call.out, b.call.out, sizeof(a.call.out)) != 0) diff->what = "registers";
  else if (a.call.esp_after != b.call.esp_after || a.call.esp_after != frame_top - 0x10) diff->what = "esp";
  else if (compare_flags && ((a.call.unused ^ b.call.unused) & 0x8D5) != 0) diff->what = "EFLAGS at the region's end (CF PF AF ZF SF OF)";
  else if (b.call.out[6] != Address(&world.call)) diff->what = "ebp";
  else if (std::memcmp(a.call.fx_out, b.call.fx_out, 160) != 0) {
    diff->what = (std::memcmp(a.call.fx_out + 24, b.call.fx_out + 24, 4) != 0 ? "MXCSR" : "x87 state");
  }
  else if (std::memcmp(a.call.fx_out + 160, b.call.fx_out + 160, 16) != 0) diff->what = "xmm0";
  else if (std::memcmp(a.call.frame + 0x100, b.call.frame + 0x100, 0x100) != 0) diff->what = "frame";
  else if (a.arena != b.arena) diff->what = "arena";
  else if (std::memcmp(a.call.fx_out + 176, backup.call.fx_in + 176, 112) != 0) diff->what = "the game's xmm1-7";
  return diff->what[0] == 0;
}

// ---- fault tests: a stack of its own (room for the exception dispatch below F), a vectored handler that records the fault and
// returns to the harness ----
static RowsCall* adv_call = nullptr;

__declspec(naked) void __cdecl EnterRows4(RowsCall*) {
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
    mov esp, [ebp + 828]
    mov eax, [ebp + 0]
    mov ecx, [ebp + 4]
    mov edx, [ebp + 8]
    mov ebx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]
    jmp dword ptr [ebp + 28]
  }
}

__declspec(naked) void RowsReturn4() {
  __asm {
    mov [esp - 8], eax
    mov eax, dword ptr [adv_call]
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
    mov esp, [eax + 32]
    popad
    ret
  }
}

__declspec(naked) void FaultLanding() {
  __asm {
    popad
    ret
  }
}

LONG CALLBACK AdvHandler(EXCEPTION_POINTERS* info) {
  if (adv_call == nullptr || info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
  RowsCall* call = adv_call;
  const CONTEXT* context = info->ContextRecord;
  call->faulted = 1;
  call->fault_write = static_cast<uint32_t>(info->ExceptionRecord->ExceptionInformation[0]);
  call->fault_address = static_cast<uint32_t>(info->ExceptionRecord->ExceptionInformation[1]);
  call->fault_eip = context->Eip;
  call->fault_esp = context->Esp;
  call->fault_regs[0] = context->Eax;
  call->fault_regs[1] = context->Ecx;
  call->fault_regs[2] = context->Edx;
  call->fault_regs[3] = context->Ebx;
  call->fault_regs[4] = context->Esi;
  call->fault_regs[5] = context->Edi;
  call->fault_regs[6] = context->Ebp;
  call->fault_regs[7] = context->EFlags;
  info->ContextRecord->Esp = call->saved_esp;
  info->ContextRecord->Eip = Address(reinterpret_cast<const void*>(&FaultLanding));
  return EXCEPTION_CONTINUE_EXECUTION;
}

// The region in a stack of its own (stack_top = F): the frame the world holds is copied there and back.
void RunRows4(uint32_t block, RowsWorld* world, const RowsWorld& backup, RowsWorld* out, uint8_t* stack_top) {
  *world = backup;
  world->call.block = block;
  world->call.faulted = 0;
  world->call.frame_top = Address(stack_top);
  std::memcpy(stack_top - 0x100, backup.call.frame, 0x200);
  adv_call = &world->call;
  EnterRows4(&world->call);
  adv_call = nullptr;
  std::memcpy(world->call.frame, stack_top - 0x100, 0x200);
  if (world->call.faulted == 0) world->call.esp_after = world->call.esp_after - Address(stack_top) + Address(world->call.frame) + 0x100;
  *out = *world;
}

// Both copies' region ends jump to `where` (restored by the caller from the saved bytes).
struct AdvEnds {
  uint8_t* game_end;
  uint8_t* patched_end;
  std::array<uint8_t, 5> game_saved, patched_saved;
  void Set(uint8_t* game, uint8_t* patched, uint32_t end_rva, const void* where) {
    game_end = game + end_rva;
    patched_end = patched + end_rva;
    std::memcpy(game_saved.data(), game_end, 5);
    std::memcpy(patched_saved.data(), patched_end, 5);
    JumpTo(game_end, where);
    JumpTo(patched_end, where);
  }
  void Restore() {
    std::memcpy(game_end, game_saved.data(), 5);
    std::memcpy(patched_end, patched_saved.data(), 5);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }
};

#include "row_builder_call.inc"

static uint32_t loop_target = 0;
static volatile LONG loop_stop = 0;
static volatile LONG loop_count = 0;

// The patched copy's region end for the thread stress: back to the region's start (through the site), until told to stop.
__declspec(naked) void LoopBack() {
  __asm {
    cmp dword ptr [loop_stop], 0
    jne done
    lock inc dword ptr [loop_count]
    add esp, 0x10
    jmp dword ptr [loop_target]
  done:
    jmp RowsReturn
  }
}

DWORD WINAPI AdvSleeper(LPVOID) {
  Sleep(INFINITE);
  return 0;
}

int RunAdv(const TestBuild& build, uint8_t* game, uint8_t* patched, uint32_t text_rva, uint32_t text_size, bool quick, const std::string& which_part) {
  namespace ec = twattila::engine_code::internal;
  (void)text_rva;
  (void)text_size;
  const uint32_t region_rva = build.code->half_rows_rva;
  const uint32_t end_rva = region_rva + ec::HALF_ROWS_END;
  CHECK(ec::sites[8].at == patched + region_rva && (ec::written >> 8 & 1) != 0);
  const uint32_t game_block = Address(game + region_rva);
  const uint32_t patched_block = Address(patched + region_rva);
  const auto wanted = [&](const char* part) { return which_part == "all" || which_part == part; };
  auto world = std::make_unique<RowsWorld>();
  auto backup = std::make_unique<RowsWorld>();
  auto a = std::make_unique<RowsWorld>();
  auto b = std::make_unique<RowsWorld>();
  std::printf("RowsWorld at %p: fx areas aligned %d, sizeof %zu\n", static_cast<void*>(world.get()), (Address(world->call.fx_in) % 16) == 0, sizeof(RowsWorld));
  CHECK(Address(world->call.fx_in) % 16 == 0 && Address(world->call.fx_out) % 16 == 0 && Address(world->call.fx_saved) % 16 == 0);
  AdvEnds ends;
  ends.Set(game, patched, end_rva, reinterpret_cast<const void*>(&RowsReturn3));

  // ---- E1 + E5: MXCSR (rounding mode x DAZ x FTZ x flags preset or not) and the whole x87 state, random and nasty inputs ----
  if (wanted("e1")) {
    const int per_config = quick ? 20000 : 150000;
    uint64_t total = 0, total_fast = 0, wrong = 0;
    std::array<uint64_t, 6> new_flags_fast = {};  // runs on the vector path where the region raised each sticky flag (IE DE ZE OE UE PE)
    std::array<uint64_t, 6> new_flags_slow = {};
    uint64_t control_runs = 0, control_seen = 0, flags_differ_shipped = 0;
    auto c = std::make_unique<RowsWorld>();
    Random random = {.state = 0xADE1};
    for (int rc = 0; rc < 4; ++rc) {
      for (int daz = 0; daz < 2; ++daz) {
        for (int ftz = 0; ftz < 2; ++ftz) {
          uint64_t fast_here = 0, wrong_here = 0;
          for (int run = 0; run < per_config; ++run) {
            const uint32_t flags = (run & 1) != 0 ? (random.Next() & 0x3F) : 0;
            const uint32_t mxcsr = 0x1F80 | rc << 13 | daz << 6 | ftz << 15 | flags;
            AdvRandom(world.get(), &random, run % 3, mxcsr);
            *backup = *world;
            RunRows3(game_block, world.get(), *backup, a.get());
            RunRows3(patched_block, world.get(), *backup, b.get());
            AdvDiff diff;
            const bool same = AdvEqual(*world, *backup, *a, *b, &diff);
            ++total;
            fast_here += diff.took_fast ? 1 : 0;
            flags_differ_shipped += ((a->call.unused ^ b->call.unused) & 0x8D5) != 0 ? 1 : 0;
            // The half function's own site back to the game's bytes (the stub's real last call and the fallback then reach the game's own
            // function): the arithmetic flags at the region's end must be the game's too. (The half-float stub's flags differ: flags after a
            // call are dead in every caller.)
            std::memcpy(ec::sites[1].at, ec::sites[1].game.data(), 5);
            FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
            RunRows3(patched_block, world.get(), *backup, c.get());
            std::memcpy(ec::sites[1].at, ec::sites[1].jump.data(), 5);
            FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
            AdvDiff diff_flags;
            if (!AdvEqual(*world, *backup, *a, *c, &diff_flags, true)) {
              ++wrong_here;
              Report("  E1: rc %d daz %d ftz %d run %d: with the half site as the game's: %s differs (vector path %d)\n", rc, daz, ftz, run, diff_flags.what, diff_flags.took_fast ? 1 : 0);
            }
            // The harness really loads and returns the state: the game's code leaves FCW / FSW / FTW / ST0-7 and MXCSR flags as set (the
            // sticky bits only grow).
            if (std::memcmp(a->call.fx_out, backup->call.fx_in, 4) != 0 || std::memcmp(a->call.fx_out + 32, backup->call.fx_in + 32, 128) != 0
                || (diff.mxcsr_out_a & ~0x3Fu) != (mxcsr & ~0x3Fu) || (diff.mxcsr_out_a & flags) != flags) {
              ++wrong_here;
              Report("  E1: the harness's state load failed (run %d)\n", run);
            }
            // CONTROL: ours under another rounding mode / DAZ must be seen as different now and then.
            if (rc == 0 && daz == 0 && ftz == 0 && run < 20000) {
              backup->call.mxcsr_in = 0;
              auto saved_fx = std::make_unique<std::array<uint8_t, 512>>();
              std::memcpy(saved_fx->data(), backup->call.fx_in, 512);
              Write32(backup->call.fx_in + 24, 0x1F80 | 3 << 13 | 1 << 6 | 1 << 15);
              RunRows3(game_block, world.get(), *backup, c.get());
              AdvDiff control_diff;
              const bool control_same = AdvEqual(*world, *backup, *a, *c, &control_diff);
              control_runs++;
              control_seen += control_same ? 0 : 1;
              std::memcpy(backup->call.fx_in, saved_fx->data(), 512);
            }
            const uint32_t raised = diff.mxcsr_out_a & 0x3F & ~flags;
            for (int bit = 0; bit < 6; ++bit) {
              if ((raised >> bit & 1) != 0) (diff.took_fast ? new_flags_fast : new_flags_slow)[bit]++;
            }
            if (!same) {
              ++wrong_here;
              Report("  E1: rc %d daz %d ftz %d run %d: %s differs (vector path %d, MXCSR out %08X)\n", rc, daz, ftz, run, diff.what, diff.took_fast ? 1 : 0, diff.mxcsr_out_a);
            }
          }
          total_fast += fast_here;
          wrong += wrong_here;
          std::printf("   E1 rc %d daz %d ftz %d: %d runs (%llu on the vector path), %llu not equal\n", rc, daz, ftz, per_config, fast_here, wrong_here);
        }
      }
    }
    std::printf("E1/E5 MXCSR x x87 state: %llu runs (%llu vector path), %llu NOT equal; sticky flags newly raised IE/DE/ZE/OE/UE/PE: vector path %llu/%llu/%llu/%llu/%llu/%llu, game's path %llu/%llu/%llu/%llu/%llu/%llu\n",
                total, total_fast, wrong, new_flags_fast[0], new_flags_fast[1], new_flags_fast[2], new_flags_fast[3], new_flags_fast[4], new_flags_fast[5],
                new_flags_slow[0], new_flags_slow[1], new_flags_slow[2], new_flags_slow[3], new_flags_slow[4], new_flags_slow[5]);
    std::printf("   arithmetic flags at the region's end differ from the game's in %llu of %llu runs with the half-float site's stub in (the stub's flags), in 0 with the half site as the game's (checked: %s)\n",
                flags_differ_shipped, total, wrong == 0 ? "no difference" : "SEE ABOVE");
    std::printf("   E1 control (the game's code under RC 3 + DAZ + FTZ against RC 0): %llu of %llu runs seen as different\n", control_seen, control_runs);
    if (wrong != 0 || control_seen == 0) {
      ends.Restore();
      return 1;
    }
  }

  // ---- E2: faults at page edges: a pointer's accesses cross into a no-access (or, for the row, read-only) page ----
  if (wanted("e2")) {
    JumpTo(game + end_rva, reinterpret_cast<const void*>(&RowsReturn4));
    JumpTo(patched + end_rva, reinterpret_cast<const void*>(&RowsReturn4));
    const PVOID handler = AddVectoredExceptionHandler(1, AdvHandler);
    auto* stack = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* area = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x5000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECK(handler != nullptr && stack != nullptr && area != nullptr);
    uint8_t* stack_top = stack + 0x8000;
    uint8_t* edge = area + 0x3000;
    Random random = {.state = 0xE2E2};
    std::vector<uint8_t> pristine(0x3000), pages_a(0x3000), pages_b(0x3000);
    for (size_t at = 0; at < 0x3000; at += 4) Write32(pristine.data() + at, RowsFloat(&random, false));
    DWORD old;
    struct Tally {
      uint64_t cases = 0, both = 0, neither = 0, only_one = 0, same_address = 0, other_address = 0, differs_when_clear = 0, pages_differ_at_fault = 0, other_kind = 0, a_changed = 0, b_changed = 0;
    };
    int printed_examples = 0;
    const auto run_case = [&](uint32_t row, uint32_t source, uint32_t second, DWORD protection, Tally* tally, const char* label, int delta) -> bool {
      (void)0;
      AdvRandom(world.get(), &random, 0, 0x1F80);
      world->call.in[4] = source;
      Write32(world->call.frame + 0x100 + 0x5C, second);
      Write32(world->call.frame + 0x100 + 0x68, row);
      *backup = *world;
      VirtualProtect(edge, 0x1000, protection, &old);
      std::memcpy(area, pristine.data(), 0x3000);
      RunRows4(game_block, world.get(), *backup, a.get(), stack_top);
      std::memcpy(pages_a.data(), area, 0x3000);
      std::memcpy(area, pristine.data(), 0x3000);
      RunRows4(patched_block, world.get(), *backup, b.get(), stack_top);
      std::memcpy(pages_b.data(), area, 0x3000);
      VirtualProtect(edge, 0x1000, PAGE_READWRITE, &old);
      ++tally->cases;
      const bool fa = a->call.faulted != 0, fb = b->call.faulted != 0;
      if (fa && fb) {
        ++tally->both;
        if (a->call.fault_address == b->call.fault_address) ++tally->same_address;
        else {
          ++tally->other_address;
          if (printed_examples < 12) {
            ++printed_examples;
            std::printf("   %s delta %+d: both fault, the game's first fault at %08X (%s, eip +%X), ours at %08X (%s, eip %08X)\n", label, delta, a->call.fault_address,
                        a->call.fault_write != 0 ? "write" : "read", a->call.fault_eip - game_block, b->call.fault_address, b->call.fault_write != 0 ? "write" : "read", b->call.fault_eip);
          }
        }
        if (a->call.fault_write != b->call.fault_write) ++tally->other_kind;
        if (pages_a != pages_b) ++tally->pages_differ_at_fault;
        for (size_t at = 0; at < pages_a.size(); ++at) {
          tally->a_changed += pages_a[at] != pristine[at] ? 1 : 0;
          tally->b_changed += pages_b[at] != pristine[at] ? 1 : 0;
        }
        if (label[0] == 'r' && label[1] == 'o' && (delta == 30 || delta == 10) && protection == PAGE_NOACCESS) {
          std::string line_a, line_b;
          for (size_t at = 0; at < pages_a.size(); ++at) {
            if (pages_a[at] != pristine[at]) line_a += std::format(" {:+d}", static_cast<int>(at) - 0x3000);
            if (pages_b[at] != pristine[at]) line_b += std::format(" {:+d}", static_cast<int>(at) - 0x3000);
          }
          std::printf("   row delta %d: bytes the game changed (relative to the edge):%s\n   ours:%s\n", delta, line_a.c_str(), line_b.c_str());
        }
        return true;
      }
      if (!fa && !fb) {
        ++tally->neither;
        bool same = std::memcmp(a->call.out, b->call.out, sizeof(a->call.out)) == 0 && a->call.esp_after == b->call.esp_after && a->call.xmm0_out == b->call.xmm0_out
                    && std::memcmp(a->call.frame + 0x100, b->call.frame + 0x100, 0x100) == 0 && a->arena == b->arena && pages_a == pages_b;
        if (!same) {
          ++tally->differs_when_clear;
          std::printf("   %s delta %+d: neither faults, NOT equal: regs %d esp %d xmm0 %d frame %d arena %d pages %d (second %08X source %08X row %08X, edge %08X)\n", label, delta,
                      std::memcmp(a->call.out, b->call.out, sizeof(a->call.out)) != 0, a->call.esp_after != b->call.esp_after, a->call.xmm0_out != b->call.xmm0_out,
                      std::memcmp(a->call.frame + 0x100, b->call.frame + 0x100, 0x100) != 0, a->arena != b->arena, pages_a != pages_b, second, source, row, Address(edge));
          for (int reg = 0; reg < 7; ++reg) {
            if (a->call.out[reg] != b->call.out[reg]) std::printf("      out[%d]: game %08X ours %08X\n", reg, a->call.out[reg], b->call.out[reg]);
          }
          for (size_t at = 0; at < pages_a.size(); ++at) {
            if (pages_a[at] != pages_b[at]) {
              std::printf("      page +%zX: game %02X ours %02X\n", at, pages_a[at], pages_b[at]);
              break;
            }
          }
        }
        return same;
      }
      ++tally->only_one;
      std::printf("   %s delta %+d: ONLY ONE FAULTS: the game %d (%08X), ours %d (%08X)\n", label, delta, fa ? 1 : 0, a->call.fault_address, fb ? 1 : 0, b->call.fault_address);
      return false;
    };
    const uint32_t row0 = Address(area + 0x1000 + 3), source0 = Address(area + 0x1400 + 1), second0 = Address(area + 0x1800 + 5);
    const uint32_t edge_address = Address(edge);
    bool ok = true;
    // One pointer at the edge, the other two on good memory.
    for (const DWORD protection : {static_cast<DWORD>(PAGE_NOACCESS), static_cast<DWORD>(PAGE_READONLY)}) {
      for (int which = 0; which < 3; ++which) {
        if (protection == PAGE_READONLY && which != 0) continue;
        Tally tally;
        static constexpr const char* NAMES[3] = {"row", "source", "second"};
        const int low = (which == 0 ? -0x34 : -0x10), high = (which == 0 ? 0x48 : (which == 1 ? 0x50 : 0x58));
        for (int delta = low; delta <= high; ++delta) {
          const uint32_t at = edge_address - delta;
          ok &= run_case(which == 0 ? at : row0, which == 1 ? at : source0, which == 2 ? at : second0, protection, &tally, NAMES[which], delta);
        }
        std::printf("E2 %-6s at the edge of a %s page, delta %+d..%+d: %llu cases: both fault %llu (same address %llu, other %llu, read/write kind differs %llu, partial stores differ %llu; bytes changed before the fault: game %llu ours %llu), neither %llu (not equal %llu), only one %llu\n",
                    NAMES[which], protection == PAGE_NOACCESS ? "no-access" : "read-only", low, high, tally.cases, tally.both, tally.same_address, tally.other_address, tally.other_kind,
                    tally.pages_differ_at_fault, tally.a_changed, tally.b_changed, tally.neither, tally.differs_when_clear, tally.only_one);
        if (tally.only_one != 0 || tally.differs_when_clear != 0 || tally.other_kind != 0) ok = false;
      }
    }
    // Two bad pointers at once (the first fault can differ: the game stores before it reads the second object).
    {
      Tally tally;
      for (const int dr : {-0x20, 0x10, 0x28}) {
        for (const int ds : {-0x20, 0x20, 0x40}) {
          for (const int dq : {-0x20, 0x4C}) {
            ok &= run_case(edge_address - dr, edge_address - ds, edge_address - dq, PAGE_NOACCESS, &tally, "row+source+second", dr * 1000 + ds * 10 + dq);
          }
        }
      }
      std::printf("E2 all three pointers at the edge (18 placements): both fault %llu (same address %llu, other %llu), neither %llu, only one %llu\n", tally.both, tally.same_address, tally.other_address,
                  tally.neither, tally.only_one);
      if (tally.only_one != 0) ok = false;
    }
    RemoveVectoredExceptionHandler(handler);
    VirtualFree(stack, 0, MEM_RELEASE);
    VirtualFree(area, 0, MEM_RELEASE);
    JumpTo(game + end_rva, reinterpret_cast<const void*>(&RowsReturn3));
    JumpTo(patched + end_rva, reinterpret_cast<const void*>(&RowsReturn3));
    if (!ok) {
      std::printf("E2: FAILED\n");
      ends.Restore();
      return 1;
    }
  }

  // ---- E3: the frame below F, byte by byte, for a row over the frame at every offset, and for random worlds ----
  if (wanted("e3")) {
    Random random = {.state = 0xE3E3};
    std::array<uint64_t, 0x100> differing = {};  // by frame byte (index 0x100 - k = F - k)
    uint64_t cases = 0, fast = 0, wrong = 0;
    for (int offset = -0x90; offset <= 0x90; ++offset) {
      if ((offset + 8 < 0x60 && offset + 0x30 > 0x5C) || (offset + 8 < 0x6C && offset + 0x30 > 0x68)) continue;
      for (int round = 0; round < 60; ++round) {
        AdvRandom(world.get(), &random, 0, 0x1F80);
        uint8_t* frame_top = world->call.frame + 0x100;
        Write32(frame_top + 0x68, Address(frame_top) + offset);
        *backup = *world;
        RunRows3(game_block, world.get(), *backup, a.get());
        RunRows3(patched_block, world.get(), *backup, b.get());
        AdvDiff diff;
        const bool same = AdvEqual(*world, *backup, *a, *b, &diff);
        ++cases;
        fast += diff.took_fast ? 1 : 0;
        for (int at = 0; at < 0x100; ++at) differing[at] += (a->call.frame[at] != b->call.frame[at]) ? 1 : 0;
        if (!same) {
          ++wrong;
          Report("  E3: row at F%+d: %s differs\n", offset, diff.what);
        }
        // A row over the frame: its offset against what differs below F, in a table.
        if (diff.below_highest >= 0 && round == 0 && offset >= -0x7f && offset <= -0x6f) {
          std::printf("   row at F%+d: frame differs from F%+d to F%+d (vector path %d)\n", offset, diff.below_lowest - 0x100, diff.below_highest - 0x100, diff.took_fast ? 1 : 0);
        }
      }
    }
    int lowest = 0x100, highest = -1;
    for (int at = 0; at < 0x100; ++at) {
      if (differing[at] == 0) continue;
      lowest = std::min(lowest, at);
      highest = std::max(highest, at);
    }
    std::printf("E3 row over the frame, F-0x90 .. F+0x90 x 60: %llu cases (%llu vector path), %llu NOT equal from F up; bytes below F that differ: F%+d .. F%+d\n",
                cases, fast, wrong, lowest - 0x100, highest - 0x100);
    if (wrong != 0) {
      ends.Restore();
      return 1;
    }
    // The same over random worlds (no row over the frame): where the pushed arguments and the callee's frames differ.
    std::array<uint64_t, 0x100> random_differing = {};
    for (int run = 0; run < 20000; ++run) {
      AdvRandom(world.get(), &random, run % 3, 0x1F80);
      *backup = *world;
      RunRows3(game_block, world.get(), *backup, a.get());
      RunRows3(patched_block, world.get(), *backup, b.get());
      AdvDiff diff;
      CHECK(AdvEqual(*world, *backup, *a, *b, &diff));
      for (int at = 0; at < 0x100; ++at) random_differing[at] += (a->call.frame[at] != b->call.frame[at]) ? 1 : 0;
    }
    lowest = 0x100, highest = -1;
    for (int at = 0; at < 0x100; ++at) {
      if (random_differing[at] == 0) continue;
      lowest = std::min(lowest, at);
      highest = std::max(highest, at);
    }
    std::printf("E3 random worlds (20000): bytes below F that differ between the game's and ours: F%+d .. F%+d\n", lowest - 0x100, highest - 0x100);
  }
  // ---- E8: part 11c per lane, so that every lane's patterns take the vector path: one lane under test, 2^N bit patterns spread over the whole range, the other
  // lanes random values of the vector classes (zero, tiny, normal) that differ lane to lane ----
  if (wanted("e8")) {
    JumpTo(game + end_rva, reinterpret_cast<const void*>(&RowsReturn));
    JumpTo(patched + end_rva, reinterpret_cast<const void*>(&RowsReturn));
    const uint64_t patterns = quick ? (1ull << 24) : (1ull << 28);
    static constexpr const char* LANE_NAMES[19] = {"xmm0", "F+24", "esi+1c", "int F+20", "F+70", "F+58", "F+74 (x F+50 = 1.0)", "esi+40", "F+4c (x F+74 = 1.0)", "F+48", "F+44", "esi+28", "esi+2c",
                                                   "esi+30", "esi+34", "esi+38", "esi+3c", "second+48", "second+4c"};
    uint64_t total_wrong = 0;
    for (uint32_t lane = 0; lane < 19; ++lane) {
      std::atomic<uint64_t> fast_calls = 0;
      const uint64_t wrong = Parallel(patterns, [&](uint64_t first, uint64_t last, unsigned worker) -> uint64_t {
        auto in = std::make_unique<RowsWorld>();
        auto saved = std::make_unique<RowsWorld>();
        auto ra = std::make_unique<RowsWorld>();
        auto rb = std::make_unique<RowsWorld>();
        Random random = {.state = 0xE8E8ull ^ (first * 0x2545F4914F6CDD1Dull) ^ worker ^ (static_cast<uint64_t>(lane) << 40)};
        uint64_t bad = 0, fast_here = 0;
        for (uint64_t index = first; index < last; ++index) {
          RowsRandom(in.get(), &random, 0);  // every other lane a vector class, different in each
          uint8_t* frame = in->call.frame + 0x100;
          uint8_t* source = in->arena.data();
          uint8_t* second = in->arena.data() + 0x100;
          const auto bits = static_cast<uint32_t>(index * 0x9E3779B1ull + lane);
          for (uint32_t slot = 0; slot < 4; ++slot) Write32(frame - 0x10 + 4 * slot, random.Next());
          switch (lane) {
            case 0: in->call.xmm0_in = bits; break;
            case 1: Write32(frame + 0x24, bits); break;
            case 2: Write32(source + 0x1C, bits); break;
            case 3: Write32(frame + 0x20, bits); break;
            case 4: Write32(frame + 0x70, bits); break;
            case 5: Write32(frame + 0x58, bits); break;
            case 6: Write32(frame + 0x50, 0x3F800000); Write32(frame + 0x74, bits); Write32(frame + 0x4C, 0); break;
            case 7: Write32(source + 0x40, bits); break;
            case 8: Write32(frame + 0x50, 0); Write32(frame + 0x74, 0x3F800000); Write32(frame + 0x4C, bits); break;
            case 9: Write32(frame + 0x48, bits); break;
            case 10: Write32(frame + 0x44, bits); break;
            case 11: case 12: case 13: case 14: Write32(source + 0x28 + 4 * (lane - 11), bits); break;
            case 15: Write32(source + 0x38, bits); break;
            case 16: Write32(source + 0x3C, bits); break;
            case 17: Write32(second + 0x48, bits); break;
            default: Write32(second + 0x4C, bits); break;
          }
          *saved = *in;
          RunRows(game_block, in.get(), *saved, ra.get());
          RunRows(patched_block, in.get(), *saved, rb.get());
          bool took_fast;
          if (!RowsEqual(*in, *saved, *ra, *rb, &took_fast)) {
            ++bad;
            Report("  E8 lane %u pattern %08X: NOT equal\n", lane, bits);
          }
          fast_here += took_fast ? 1 : 0;
        }
        fast_calls += fast_here;
        return bad;
      });
      std::printf("   E8 lane %-22s %llu patterns, %llu on the vector path, %llu NOT equal\n", LANE_NAMES[lane], patterns, fast_calls.load(), wrong);
      total_wrong += wrong;
    }
    {
      // The integer lane over every value the vector path can take (|int| below 65520) and a margin, with different neighbors each time.
      Random random = {.state = 0xE8E9};
      uint64_t bad = 0, fast_calls = 0, calls = 0;
      for (int64_t value = -70000; value <= 70000; ++value) {
        for (int repeat = 0; repeat < 4; ++repeat) {
          RowsRandom(world.get(), &random, 0);
          uint8_t* frame = world->call.frame + 0x100;
          for (uint32_t slot = 0; slot < 4; ++slot) Write32(frame - 0x10 + 4 * slot, random.Next());
          Write32(frame + 0x20, static_cast<uint32_t>(static_cast<int32_t>(value)));
          *backup = *world;
          RunRows(game_block, world.get(), *backup, a.get());
          RunRows(patched_block, world.get(), *backup, b.get());
          bool took_fast;
          if (!RowsEqual(*world, *backup, *a, *b, &took_fast)) ++bad;
          fast_calls += took_fast ? 1 : 0;
          ++calls;
        }
      }
      std::printf("   E8 integer lane -70000 .. 70000 x 4 neighbor sets: %llu calls, %llu on the vector path, %llu NOT equal\n", calls, fast_calls, bad);
      total_wrong += bad;
    }
    std::printf("E8 one lane under test at a time, the others vector-class random: %llu NOT equal\n", total_wrong);
    JumpTo(game + end_rva, reinterpret_cast<const void*>(&RowsReturn3));
    JumpTo(patched + end_rva, reinterpret_cast<const void*>(&RowsReturn3));
    if (total_wrong != 0) {
      ends.Restore();
      return 1;
    }
  }

  // ---- E7a: pointer soup: the source, second object and row anywhere around each other, the frame and the arena, at odd addresses ----
  if (wanted("e7a")) {
    Random random = {.state = 0x5017};
    const int runs = quick ? 400000 : 3000000;
    uint64_t wrong = 0, fast = 0, aliased = 0;
    std::array<uint64_t, 4> by_kind = {};
    for (int run = 0; run < runs; ++run) {
      AdvRandom(world.get(), &random, run % 2, 0x1F80 | (random.Next() & 3) << 13);
      RowsCall* call = &world->call;
      uint8_t* frame_top = call->frame + 0x100;
      uint8_t* arena = world->arena.data();
      const uint32_t frame_address = Address(frame_top), arena_address = Address(arena);
      uint32_t pointers[3];  // source, second, row
      for (int which = 0; which < 3; ++which) {
        switch (random.Next() % 5) {
          case 0: pointers[which] = arena_address + (random.Next() % 0x80); break;                     // all in one small heap region
          case 1: pointers[which] = arena_address + 0x100 + (random.Next() % 0x180); break;
          case 2: pointers[which] = frame_address + static_cast<int32_t>(random.Next() % 0x150) - 0xA0; break;  // F-0xA0 .. F+0xAF
          case 3: pointers[which] = (which == 0 ? arena_address + 0x20 : pointers[random.Next() % which]) + static_cast<int32_t>(random.Next() % 0x60) - 0x30; break;  // near another one
          default: pointers[which] = (which == 0 ? arena_address + 0x20 : pointers[random.Next() % which]); break;  // the very same address
        }
      }
      // The frame's own pointer slots must stay out of the way of any store (the game's code reads them again after the row's stores).
      const uint32_t row = pointers[2];
      if ((row + 8 < frame_address + 0x60 && row + 0x30 > frame_address + 0x5C) || (row + 8 < frame_address + 0x6C && row + 0x30 > frame_address + 0x68)) continue;
      // Objects and row must keep clear of what lies below the frame's 0x100 bytes and above its top.
      bool in_range = true;
      for (int which = 0; which < 3; ++which) {
        const uint32_t lo = which == 2 ? pointers[which] : pointers[which];
        const uint32_t hi = pointers[which] + 0x50;
        const bool in_arena = lo >= arena_address && hi <= arena_address + 0x400;
        const bool in_frame = lo >= frame_address - 0xA0 && hi <= frame_address + 0x100;
        in_range &= in_arena || in_frame;
      }
      if (!in_range) continue;
      // An object whose floats the game reads from the dead stack below the frame (where its own pushed arguments and the callee's
      // frames write: [F-0x50, F)) is not a live object: not a case (the same rule as part 11b's).
      if ((pointers[0] + 0x1C < frame_address && pointers[0] + 0x44 > frame_address - 0x50) || (pointers[1] + 0x48 < frame_address && pointers[1] + 0x50 > frame_address - 0x50)) continue;
      const auto at = [](uint32_t address) { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address)); };
      // Floats of the objects (this may overwrite the frame's slots or the other object: the last writes win).
      const int mode = run % 2;
      const auto draw = [&]() -> uint32_t { return mode == 0 ? RowsFloat(&random, false) : (random.Next() % 20 == 0 ? AdvFloat(&random) : RowsFloat(&random, false)); };
      for (const uint32_t offset : ROWS_SOURCE_FLOATS) Write32(at(pointers[0]) + offset, draw());
      Write32(at(pointers[1]) + 0x48, draw());
      Write32(at(pointers[1]) + 0x4C, draw());
      call->in[4] = pointers[0];
      Write32(frame_top + 0x5C, pointers[1]);
      Write32(frame_top + 0x68, pointers[2]);
      *backup = *world;
      RunRows3(game_block, world.get(), *backup, a.get());
      RunRows3(patched_block, world.get(), *backup, b.get());
      AdvDiff diff;
      if (!AdvEqual(*world, *backup, *a, *b, &diff)) {
        ++wrong;
        Report("  E7a: run %d: %s differs (source %08X second %08X row %08X, F %08X, arena %08X)\n", run, diff.what, pointers[0], pointers[1], pointers[2], frame_address, arena_address);
      }
      fast += diff.took_fast ? 1 : 0;
      by_kind[(pointers[0] == pointers[1] ? 1 : 0) + (pointers[0] == pointers[2] || pointers[1] == pointers[2] ? 2 : 0)]++;
      if ((pointers[0] > pointers[1] ? pointers[0] - pointers[1] : pointers[1] - pointers[0]) < 0x50 || (pointers[2] > pointers[0] ? pointers[2] - pointers[0] : pointers[0] - pointers[2]) < 0x50) ++aliased;
    }
    std::printf("E7a pointer soup (%d tries): %llu NOT equal; vector path in %llu (arguments slots differ), objects / row within 0x50 of each other in %llu; same address pairs: source=second %llu, row=an object %llu\n", runs, wrong, fast, aliased, by_kind[1] + by_kind[3],
                by_kind[2] + by_kind[3]);
    if (wrong != 0) {
      ends.Restore();
      return 1;
    }
  }

  // ---- E7b: PausedWrite against a thread whose Eip is at the site's byte k (0 .. 5): refused exactly for 1 .. 4 ----
  if (wanted("e7b")) {
    ends.Restore();  // the real site bytes are what the jump replaces
    uint8_t* site = ec::sites[8].at;
    HANDLE thread = CreateThread(nullptr, 0, AdvSleeper, nullptr, CREATE_SUSPENDED, nullptr);
    CHECK(thread != nullptr);
    CONTEXT context = {.ContextFlags = CONTEXT_CONTROL};
    CHECK(GetThreadContext(thread, &context) != FALSE);
    const DWORD benign = context.Eip;
    bool ok = true;
    std::string line;
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, true, false, true);  // the state the loop below switches off and on
    std::printf("   E7b starts with written %X of found %X\n", ec::written, ec::found);
    for (int k = 0; k <= 5; ++k) {
      context.Eip = Address(site) + k;
      CHECK(SetThreadContext(thread, &context) != FALSE);
      const uint32_t before = ec::written;
      twattila::engine_code::Update(false, twattila::engine_code::Game::ATTILA, true, false, true);
      const bool refused = (ec::written == before);
      line += std::format(" k={}:{}", k, refused ? "refused" : "written");
      if (refused != (k >= 1 && k <= 4)) ok = false;
      context.Eip = benign;
      CHECK(SetThreadContext(thread, &context) != FALSE);
      twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, true, false, true);
      if (ec::written != before) ok = false;
    }
    TerminateThread(thread, 0);
    CloseHandle(thread);
    std::printf("E7b PausedWrite with a suspended thread at the half rows site + k:%s; %s\n", line.c_str(), ok ? "as designed (refused exactly for 1..4)" : "NOT AS DESIGNED");
    if (!ok) return 1;
  }

  // ---- E7d: 4 threads that run the patched region through the site back to back (the loop is one region long: a thread is at the site's
  // 5th byte, the original `push eax`, a few cycles in a hundred) while the row is switched off and on 400 times ----
  if (wanted("e7d")) {
    ends.Restore();
    JumpTo(patched + end_rva, reinterpret_cast<const void*>(&LoopBack));
    loop_target = patched_block;
    loop_stop = 0;
    loop_count = 0;
    std::atomic<bool> started = false;
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        auto in = std::make_unique<RowsWorld>();
        Random random = {.state = 0x7D7Dull + static_cast<uint64_t>(worker)};
        AdvRandom(in.get(), &random, 0, 0x1F80);
        in->call.block = patched_block;
        started = true;
        EnterRows(&in->call);
      });
    }
    while (!started.load()) Sleep(1);
    int busy = 0, flips = 0;
    std::array<int, 2> tries_total = {}, switches_by_direction = {}, failed_by_direction = {};
    bool on = true;
    const int switches = quick ? 200 : 600;
    for (int round = 0; round < switches; ++round) {
      on = !on;
      int tries = 0;
      do {
        twattila::engine_code::Update(on, twattila::engine_code::Game::ATTILA, true, false, true);
        ++tries;
      } while (((ec::written >> 8 & 1) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
      const bool done = ((ec::written >> 8 & 1) != 0) == on;
      flips += done ? 1 : 0;
      tries_total[on ? 1 : 0] += tries;
      switches_by_direction[on ? 1 : 0]++;
      failed_by_direction[on ? 1 : 0] += done ? 0 : 1;
      Sleep(1);
    }
    std::printf("E7d (all CPU sites by Update) %d switches while 4 threads ran the region %ld times through the site: %d done, %d needed more than one try; turning off: %d switches, %d tries, %d never done; turning on: %d switches, %d tries, %d never done\n",
                switches, loop_count, flips, busy, switches_by_direction[0], tries_total[0], failed_by_direction[0], switches_by_direction[1], tries_total[1], failed_by_direction[1]);
    // Every subset of the CPU sites turned off together: refusals in 20 tries each.
    {
      const std::array<uint32_t, 5> sites5 = {0, 1, 2, 4, 8};
      for (uint32_t subset = 1; subset < 32; ++subset) {
        uint32_t off_mask = 0;
        for (uint32_t bit = 0; bit < 5; ++bit) off_mask |= (subset >> bit & 1) != 0 ? 1u << sites5[bit] : 0u;
        int refused = 0;
        for (int round = 0; round < 20; ++round) {
          const uint32_t current = ec::written;
          if (!PausedWriteMask(current & ~off_mask)) {
            ++refused;
            continue;
          }
          ec::written = current & ~off_mask;
          Sleep(1);
          while (!PausedWriteMask(current)) {
          }
          ec::written = current;
        }
        if (refused != 0 || subset == 31) std::printf("   subset %02X (sites %08X off together): refused %d of 20\n", subset, off_mask, refused);
      }
    }
    // Each CPU site alone, turned off and on by PausedWrite: which one a busy thread refuses.
    for (const uint32_t site : {0u, 1u, 2u, 4u, 8u}) {
      int refused_off = 0, refused_on = 0;
      for (int round = 0; round < 50; ++round) {
        const uint32_t current = ec::written;
        if (!PausedWriteMask(current & ~(1u << site))) {
          ++refused_off;
          continue;
        }
        ec::written = current & ~(1u << site);
        Sleep(1);
        if (!PausedWriteMask(current)) {
          ++refused_on;
          while (!PausedWriteMask(current)) {
          }
        }
        ec::written = current;
        Sleep(1);
      }
      std::printf("E7d site %u alone, 50 rounds: turning off refused %d times, turning on refused %d times\n", site, refused_off, refused_on);
    }
    // Site 8 alone, by PausedWrite itself.
    {
      uint32_t mask = ec::written;
      int single_done = 0, single_tries = 0, single_failed = 0;
      for (int round = 0; round < switches; ++round) {
        const uint32_t wanted_mask = mask ^ (1u << 8);
        int tries = 0;
        bool done = false;
        while (!done && tries < 30) {
          done = PausedWriteMask(wanted_mask);
          ++tries;
        }
        if (done) {
          mask = wanted_mask;
          ec::written = mask;
          ++single_done;
        } else {
          ++single_failed;
        }
        single_tries += tries;
        Sleep(1);
      }
      std::printf("E7d (half rows site alone, PausedWrite) %d switches: %d done, %d tries in all, %d never done\n", switches, single_done, single_tries, single_failed);
      if ((ec::written >> 8 & 1) == 0) {
        while (!PausedWriteMask(ec::written | (1u << 8))) {
        }
        ec::written |= 1u << 8;
      }
    }
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, true, false, true);
    loop_stop = 1;
    for (auto& thread : threads) thread.join();
    std::printf("E7d: no crash\n");
    ends.Restore();
  }

  if (wanted("e4")) {
    ends.Restore();
    if (RunFnTests(build, game, patched, quick) != 0) return 1;
  }
  ends.Restore();
  (void)patched_block;
  (void)game_block;
  return 0;
}
