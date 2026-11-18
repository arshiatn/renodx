// ====================================================================================================================
// 14. The particle writer's two float4 reloads (+0x18960d0, Thrones +0x190b430; Faster Game CPU Code; engine_code.hpp 9.), included inside
// tests.cpp's anonymous namespace after row_builder_call.inc (FnCall and CallFn, the whole-function call with every register, the flags,
// the x87 and SSE state, are that test's). The game's `movups xmm0, [esp + 0x40]` after four 4-byte stores against the stub's lane loads:
// 14a. the sites and stubs as Prepare made them;
// 14b. the region alone with random 16 stack bytes of every float class per lane (NaN payloads, subnormals, -0, infinities), stored as
//    the game stores them, any MXCSR: every register, all of xmm0-7, the flags, MXCSR, esp and the frame equal; CONTROL: a stub with one
//    lane wrong (each of the four) must differ (ReloadAlone, which part 17 runs for its sites too);
// 14c. the whole function on a fake object graph (the fields engine_code.hpp 9. lists), both sides of the je at +0x18961a1 and of the
//    comiss at +0x189660c: registers, x87 / MXCSR / xmm, flags, esp, the frame (return addresses apart), the six streams and the
//    objects equal, with each site alone and both;
// 14d. CONTROL: each stub with each lane wrong;
// 14e. 200 switches while 4 threads run the whole function with both copies;
// 14f. a parked thread at each byte of both sites;
// 14g. timing.
// ====================================================================================================================

// A float's bits, any lane: mostly ordinary, then zeros, subnormals, infinities, NaNs with payloads (quiet and signalling), the edges.
uint32_t ReloadFloat(Random* random) {
  const uint32_t sign = (random->Next() & 1) << 31;
  const uint32_t mantissa = random->Next() & 0x7FFFFF;
  switch (random->Next() % 12) {
    case 0: return random->Next() ^ (random->Next() << 16);
    case 1: return sign;
    case 2: return sign | mantissa;  // subnormal, or zero
    case 3: return sign | 0x7F800000;
    case 4: return sign | 0x7FC00000 | mantissa;                       // a quiet NaN with a payload
    case 5: return sign | 0x7F800000 | std::max(1u, mantissa & 0x3FFFFF);  // a signalling NaN with a payload
    case 6: return sign | std::array<uint32_t, 8>{1, 0x007FFFFF, 0x00800000, 0x7F7FFFFF, 0x3F800000, 0x00400000, 0x7FC00000, 0x7FFFFFFF}[random->Next() % 8];
    default: return sign | (100 + random->Next() % 50) << 23 | mantissa;
  }
}

// 2^-9 .. 2^2, either sign: the values the matrix maths takes (every branch of the function is reached with these).
uint32_t ReloadOrdinary(Random* random) { return ((random->Next() & 1) << 31) | (118 + random->Next() % 12) << 23 | (random->Next() & 0x7FFFFF); }

// ---- a. the region alone ----
// A block of code entered with every register, the flags, MXCSR and a 0x200-byte frame set; its end jumps to ReloadBlockReturn. The
// block must leave esp as it found it: the frame's last two dwords ([esp + 0x1d8], [esp + 0x1dc]) carry the target and the call
// itself to the return, so threads can run blocks at once (part 17).
struct ReloadBlockCall {
  uint32_t in[7];       // 0: eax ecx edx ebx esi edi ebp
  uint32_t flags_in;    // 28
  uint32_t target;      // 32
  uint32_t lanes[4];    // 36: stored with movss, as the game does, to esp + lanes_at .. + 0xc before the block runs
  uint32_t out[7];      // 52
  uint32_t flags_out;   // 80: lahf's SF ZF AF PF CF in bits 8-15, OF in bit 0
  uint32_t saved_esp;   // 84
  uint32_t esp_after;   // 88
  uint32_t lanes_at = 0x40;  // 92
  alignas(16) uint8_t xmm_in[8][16];   // 96
  alignas(16) uint8_t xmm_out[8][16];  // 224
  alignas(16) uint8_t frame[0x200];    // 352: esp = frame + 0x20, so the block's [esp + 0x40] is frame + 0x60
  uint32_t mxcsr_in = 0x1F80;          // 864
  uint32_t mxcsr_out = 0;              // 868
  uint32_t saved_mxcsr = 0;            // 872: the harness's
};
static_assert(offsetof(ReloadBlockCall, out) == 52 && offsetof(ReloadBlockCall, flags_out) == 80 && offsetof(ReloadBlockCall, esp_after) == 88
              && offsetof(ReloadBlockCall, lanes_at) == 92 && offsetof(ReloadBlockCall, xmm_in) == 96 && offsetof(ReloadBlockCall, xmm_out) == 224
              && offsetof(ReloadBlockCall, frame) == 352 && offsetof(ReloadBlockCall, mxcsr_in) == 864 && offsetof(ReloadBlockCall, saved_mxcsr) == 872);

__declspec(naked) void __cdecl EnterReloadBlock(ReloadBlockCall*) {
  __asm {
    pushad
    mov ebp, [esp + 36]
    mov [ebp + 84], esp
    stmxcsr [ebp + 872]
    lea esp, [ebp + 384]
    mov eax, [ebp + 32]
    mov [esp + 0x1D8], eax
    mov [esp + 0x1DC], ebp
    mov eax, [ebp + 92]
    movss xmm0, dword ptr [ebp + 36]
    movss dword ptr [esp + eax], xmm0
    movss xmm0, dword ptr [ebp + 40]
    movss dword ptr [esp + eax + 4], xmm0
    movss xmm0, dword ptr [ebp + 44]
    movss dword ptr [esp + eax + 8], xmm0
    movss xmm0, dword ptr [ebp + 48]
    movss dword ptr [esp + eax + 0xC], xmm0
    ldmxcsr [ebp + 864]
    movups xmm0, [ebp + 96]
    movups xmm1, [ebp + 112]
    movups xmm2, [ebp + 128]
    movups xmm3, [ebp + 144]
    movups xmm4, [ebp + 160]
    movups xmm5, [ebp + 176]
    movups xmm6, [ebp + 192]
    movups xmm7, [ebp + 208]
    push dword ptr [ebp + 28]
    popfd
    mov eax, [ebp + 0]
    mov ecx, [ebp + 4]
    mov edx, [ebp + 8]
    mov ebx, [ebp + 12]
    mov esi, [ebp + 16]
    mov edi, [ebp + 20]
    mov ebp, [ebp + 24]
    jmp dword ptr [esp + 0x1D8]
  }
}

// The block's end: everything saved into the call in its frame (the block's eax left in that slot, the target's slot cleared: both
// copies' frames then compare equal when their registers do), the harness's MXCSR and registers back.
__declspec(naked) void ReloadBlockReturn() {
  __asm {
    xchg eax, [esp + 0x1DC]
    mov [eax + 56], ecx
    mov [eax + 60], edx
    mov [eax + 64], ebx
    mov [eax + 68], esi
    mov [eax + 72], edi
    mov [eax + 76], ebp
    mov [eax + 88], esp
    movups [eax + 224], xmm0
    movups [eax + 240], xmm1
    movups [eax + 256], xmm2
    movups [eax + 272], xmm3
    movups [eax + 288], xmm4
    movups [eax + 304], xmm5
    movups [eax + 320], xmm6
    movups [eax + 336], xmm7
    stmxcsr [eax + 868]
    mov ebp, eax
    lahf
    seto al
    movzx eax, ax
    mov [ebp + 80], eax
    mov eax, [esp + 0x1DC]
    mov [ebp + 52], eax
    mov dword ptr [esp + 0x1D8], 0
    ldmxcsr [ebp + 872]
    mov esp, [ebp + 84]
    popad
    ret
  }
}

bool ReloadBlockEqual(const ReloadBlockCall& a, const ReloadBlockCall& b) {
  return std::memcmp(a.out, b.out, sizeof(a.out)) == 0 && a.flags_out == b.flags_out && a.esp_after - Address(&a) == b.esp_after - Address(&b)
         && std::memcmp(a.xmm_out, b.xmm_out, sizeof(a.xmm_out)) == 0 && std::memcmp(a.frame, b.frame, sizeof(a.frame)) == 0
         && a.mxcsr_out == b.mxcsr_out;
}

// ---- b. the whole function ----
// The object graph the function reads, in `bytes`: this (+0x0c the colour source P, +0x10 W, +0x14 Q, the floats at +0x18 .. +0x4c, +0x6c,
// +0x70, +0x78, +0x90 .. +0x9c, +0xcc X, +0xd0 the index), P (+0x18 .. +0x24, +0x70, +0x80 .. +0x8f), W (+0x78 the definition D), D (+0x1c and
// +0x2c bytes, +0xc8 and +0xd0 floats), Q (+8 R), R (+0x18 an integer), X (+0x180 Y), Y (+4 Z), Z (+8 the table of 24-byte entries of six
// stream pointers), the six floats the second argument points at, and six streams.
constexpr uint32_t RELOAD_THIS = 0x000, RELOAD_P = 0x100, RELOAD_W = 0x200, RELOAD_D = 0x240, RELOAD_Q = 0x340, RELOAD_R = 0x360, RELOAD_X = 0x380, RELOAD_Y = 0x540,
                   RELOAD_Z = 0x560, RELOAD_TABLE = 0x580, RELOAD_SIX = 0x600, RELOAD_STREAMS = 0x700, RELOAD_STREAM_STRIDE = 0x80;
constexpr uint32_t RELOAD_WORLD_SIZE = 0x1000;
static_assert(RELOAD_STREAMS + 6 * RELOAD_STREAM_STRIDE <= RELOAD_WORLD_SIZE);

struct ReloadWorld {
  FnCall call;
  alignas(16) std::array<uint8_t, RELOAD_WORLD_SIZE> bytes;
};

// Fills the graph (flavor 0: ordinary floats; 1: some of every class; 2: many) and returns the first argument's bits.
uint32_t ReloadFillWorld(uint8_t* bytes, Random* random, int flavor) {
  for (uint32_t at = 0; at < RELOAD_WORLD_SIZE; ++at) bytes[at] = static_cast<uint8_t>(random->Next());
  const uint32_t base = Address(bytes);
  const auto put = [&](uint32_t at, uint32_t value) { Write32(bytes + at, value); };
  const auto draw = [&]() -> uint32_t {
    const uint32_t nasty = (flavor == 0 ? 0u : (flavor == 1 ? 12u : 2u));
    return (nasty != 0 && random->Next() % nasty == 0) ? ReloadFloat(random) : ReloadOrdinary(random);
  };
  put(RELOAD_THIS + 0x0C, base + RELOAD_P);
  put(RELOAD_THIS + 0x10, base + RELOAD_W);
  put(RELOAD_THIS + 0x14, base + RELOAD_Q);
  for (uint32_t field = 0x18; field <= 0x4C; field += 4) put(RELOAD_THIS + field, draw());
  for (const uint32_t field : {0x6Cu, 0x70u, 0x78u, 0x90u, 0x94u, 0x98u, 0x9Cu}) put(RELOAD_THIS + field, draw());
  put(RELOAD_THIS + 0xCC, base + RELOAD_X);
  put(RELOAD_THIS + 0xD0, random->Next() % 8 == 0 ? 0xFFFFFFFFu : random->Next() % 4);  // -1: the table has no entry
  for (const uint32_t field : {0x18u, 0x1Cu, 0x20u, 0x24u, 0x70u}) put(RELOAD_P + field, draw());
  put(RELOAD_W + 0x78, base + RELOAD_D);
  bytes[RELOAD_D + 0x1C] = (random->Next() % 3 == 0 ? 0 : static_cast<uint8_t>(1 + random->Next() % 255));  // the je at +0x18961a1
  bytes[RELOAD_D + 0x2C] = (random->Next() % 2 == 0 ? 0 : static_cast<uint8_t>(1 + random->Next() % 255));
  put(RELOAD_D + 0xC8, random->Next() % 4 == 0 ? draw() : (0x3C000000u | (random->Next() & 0x7FFFFF)));  // > 0 mostly: the definition's scale
  put(RELOAD_D + 0xD0, draw());
  put(RELOAD_Q + 8, base + RELOAD_R);
  put(RELOAD_R + 0x18, random->Next() % 4 != 0 ? random->Next() % 1000 : random->Next());
  put(RELOAD_X + 0x180, random->Next() % 10 == 0 ? 0u : base + RELOAD_Y);
  put(RELOAD_Y + 4, base + RELOAD_Z);
  put(RELOAD_Z + 8, base + RELOAD_TABLE);
  for (uint32_t entry = 0; entry < 4; ++entry) {
    for (uint32_t stream = 0; stream < 6; ++stream) {
      put(RELOAD_TABLE + 24 * entry + 4 * stream, base + RELOAD_STREAMS + RELOAD_STREAM_STRIDE * stream + (random->Next() & 15));
    }
  }
  for (uint32_t index = 0; index < 6; ++index) put(RELOAD_SIX + 4 * index, draw());
  return draw();
}

// One call's state: the graph, the arguments, random registers, MXCSR from the caller; the stack filled with 0xFF (the function's own
// writes then show which way it went) or at random.
// A call's registers, stack (0xFF with `canary`, else random) and x87 / SSE state at random, MXCSR `mxcsr`; the function, this and the
// arguments are the caller's.
void ReloadCallRandom(FnCall* call, Random* random, uint32_t mxcsr, bool canary) {
  for (uint32_t& value : call->in) value = random->Next();
  call->faulted = 0;
  for (uint8_t& byte : call->stack) byte = (canary ? 0xFF : static_cast<uint8_t>(random->Next()));
  uint8_t* fx = call->fx_in;
  for (int index = 0; index < 512; ++index) fx[index] = static_cast<uint8_t>(random->Next());
  const auto put16 = [&](int index, uint32_t value) { fx[index] = static_cast<uint8_t>(value); fx[index + 1] = static_cast<uint8_t>(value >> 8); };
  put16(0, 0x037F);
  put16(2, 0);
  fx[4] = 0;
  put16(6, 0);
  Write32(fx + 24, mxcsr);
  for (int reg = 0; reg < 8; ++reg) std::memset(fx + 32 + 16 * reg + 10, 0, 6);
}

void ReloadRandom(ReloadWorld* world, Random* random, int flavor, uint32_t function, uint32_t mxcsr, bool canary) {
  FnCall* call = &world->call;
  const uint32_t first = ReloadFillWorld(world->bytes.data(), random, flavor);
  const uint32_t base = Address(world->bytes.data());
  call->function = function;
  call->this_ptr = base + RELOAD_THIS;
  call->args[0] = first;
  call->args[1] = base + RELOAD_SIX;
  call->args[2] = random->Next();
  call->args[3] = random->Next();
  ReloadCallRandom(call, random, mxcsr, canary);
}

void RunReload(ReloadWorld* world, const ReloadWorld& backup, ReloadWorld* out) {
  *world = backup;
  CallFn(&world->call);
  *out = *world;
}

// What differs between the game's run and ours ("" if nothing): registers, esp, the arithmetic flags, x87, MXCSR and all of xmm0-7, the
// graph and the streams, the stack around the frame (a dword that differs by exactly the distance between the two copies of the DLL is a
// return address a call left behind).
const char* ReloadDiff(const ReloadWorld& a, const ReloadWorld& b, uint32_t copy_distance) {
  if (std::memcmp(a.call.out, b.call.out, sizeof(a.call.out)) != 0) return "registers";
  if (a.call.esp_after != b.call.esp_after) return "esp";
  if (((a.call.flags_after ^ b.call.flags_after) & 0x8D5) != 0) return "flags";
  if (std::memcmp(a.call.fx_out, b.call.fx_out, 160) != 0) return (std::memcmp(a.call.fx_out + 24, b.call.fx_out + 24, 4) != 0 ? "MXCSR" : "x87 state");
  if (std::memcmp(a.call.fx_out + 160, b.call.fx_out + 160, 128) != 0) return "xmm";
  if (a.bytes != b.bytes) return "memory (the streams or the objects)";
  for (uint32_t at = FN_STACK_E - 0x400; at < FN_STACK_E + 0x20; at += 4) {
    const uint32_t x = Read32(a.call.stack + at);
    const uint32_t y = Read32(b.call.stack + at);
    if (x != y && y - x != copy_distance) return "stack";
  }
  return "";
}

// Which way the game's run went, read from the slots only that way writes (the 0xFF filled stack; the function's esp after its prologue is
// the call's E - 0xE0): 0 no table entry (the callee's [esp + 0x90] never written), 1 the body without the matrix (the byte at D + 0x1c is 0:
// [esp + 0x5c] never written), 2 the matrix and the comiss's jb ([esp + 0x68 / 0x6c / 0x70] hold 1, 2, 0), 3 the matrix and the square root.
int ReloadPath(const ReloadWorld& a) {
  const auto slot = [&](uint32_t esp_offset) { return Read32(a.call.stack + FN_STACK_E - 0xE0 + esp_offset); };
  if (slot(0x90) == 0xFFFFFFFF) return 0;
  if (slot(0x5C) == 0xFFFFFFFF) return 1;
  return (slot(0x68) == 1 && slot(0x6C) == 2 && slot(0x70) == 0) ? 2 : 3;
}

// The writer on a thread's own world (fastcall carries this in ecx; the float and the pointer go on the stack, popped by ret 8).
using ReloadWriter = void(__fastcall*)(void* self, void* unused_edx, float first, const float* six);

// Timing of the reload after the four stores, in assembly: each iteration optionally stores `cold` times to random lines of a big buffer,
// stores the four lanes, calls the block (which ends with ret; the call's return address is why the stores are 4 below), and stores
// xmm0 to another random line.
struct ReloadTimingCall {
  uint32_t block;       // 0
  uint32_t iterations;  // 4
  uint32_t buffer;      // 8
  uint32_t mask;        // 12
  uint32_t cold;        // 16
  uint32_t saved_esp;   // 20
  uint32_t checksum;    // 24
  uint32_t unused;      // 28
  alignas(16) uint8_t frame[0x100];  // 32: esp = frame + 0x80
};
static_assert(offsetof(ReloadTimingCall, frame) == 32);

__declspec(naked) void __cdecl ReloadTimingLoop(ReloadTimingCall*) {
  __asm {
    pushad
    mov ebp, [esp + 36]
    mov [ebp + 20], esp
    lea esp, [ebp + 160]
    mov ebx, [ebp + 4]
    mov esi, [ebp + 8]
    mov edi, 0x9E3779B9
  top:
    mov ecx, [ebp + 16]
  cold_stores:
    test ecx, ecx
    jz cold_done
    imul edi, edi, 1664525
    add edi, 1013904223
    mov eax, edi
    shr eax, 4
    and eax, [ebp + 12]
    mov [esi + eax], ecx
    dec ecx
    jmp cold_stores
  cold_done:
    movd xmm1, edi
    movss dword ptr [esp + 0x3C], xmm1
    movss dword ptr [esp + 0x40], xmm1
    movss dword ptr [esp + 0x44], xmm1
    movss dword ptr [esp + 0x48], xmm1
    call dword ptr [ebp + 0]
    imul edi, edi, 1664525
    add edi, 1013904223
    mov eax, edi
    shr eax, 4
    and eax, [ebp + 12]
    movups [esi + eax], xmm0
    dec ebx
    jnz top
    movd eax, xmm0
    mov [ebp + 24], eax
    mov esp, [ebp + 20]
    popad
    ret
  }
}

// Four independent mulss a loop: throughput of the multiply for the operand classes (is a subnormal operand or result a microcode assist?).
__declspec(naked) void __cdecl MulssThroughput(uint32_t first_bits, uint32_t second_bits, uint32_t count) {
  __asm {
    movd xmm0, dword ptr [esp + 4]
    movd xmm1, dword ptr [esp + 8]
    mov eax, [esp + 12]
  top:
    movaps xmm2, xmm0
    mulss xmm2, xmm1
    movaps xmm3, xmm0
    mulss xmm3, xmm1
    movaps xmm4, xmm0
    mulss xmm4, xmm1
    movaps xmm5, xmm0
    mulss xmm5, xmm1
    sub eax, 4
    jnz top
    ret
  }
}

// One line: the reload after the four stores, game / ours / none, hot stores and after a backlog of 6 stores to cold lines; the multiply.
std::string ReloadTimingLines() {
  namespace ec = twattila::engine_code::internal;
  constexpr size_t BUFFER = 256u << 20;
  auto* buffer = static_cast<uint8_t*>(VirtualAlloc(nullptr, BUFFER, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (buffer == nullptr || code == nullptr) return "   reload timing: no memory\n";
  std::memset(buffer, 1, BUFFER);
  constexpr std::array<uint8_t, 6> GAME = {0x0F, 0x10, 0x44, 0x24, 0x40, 0xC3};  // movups xmm0, [esp + 0x40]; ret
  constexpr std::array<uint8_t, 4> NONE = {0x0F, 0x28, 0xC1, 0xC3};              // movaps xmm0, xmm1; ret
  constexpr size_t STUB_CODE = ec::FLOAT4_RELOAD_STUB.size() - 5;
  std::memcpy(code, GAME.data(), GAME.size());
  std::memcpy(code + 0x40, ec::page + ec::FLOAT4_RELOAD_PAGE, STUB_CODE);
  code[0x40 + STUB_CODE] = 0xC3;
  std::memcpy(code + 0x80, NONE.data(), NONE.size());
  auto call = std::make_unique<ReloadTimingCall>();
  if (Address(call.get()) % 16 != 0) return "   reload timing: unaligned\n";
  std::string text;
  for (const uint32_t cold : {0u, 6u}) {
    std::array<double, 3> best = {1e9, 1e9, 1e9};
    for (size_t which = 0; which < best.size(); ++which) {
      *call = {};
      call->block = Address(code + 0x40 * which);
      call->iterations = (cold == 0 ? 20'000'000u : 2'000'000u);
      call->buffer = Address(buffer);
      call->mask = static_cast<uint32_t>(BUFFER - 64) & ~63u;
      call->cold = cold;
      for (int round = 0; round < 5; ++round) {
        const auto start = std::chrono::steady_clock::now();
        ReloadTimingLoop(call.get());
        best[which] = std::min(best[which], std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / call->iterations);
      }
    }
    text += std::format("{}{} stores to cold lines: {:.2f} / {:.2f} / {:.2f}", text.empty() ? "" : ", ", cold, best[0], best[1], best[2]);
  }
  VirtualFree(buffer, 0, MEM_RELEASE);
  VirtualFree(code, 0, MEM_RELEASE);
  std::string multiply;
  const std::array<std::tuple<const char*, uint32_t, uint32_t>, 3> cases = {{{"normal x normal", 0x3FC00000, 0x40200000},
                                                                          {"subnormal x 1.0 (subnormal result)", 0x00002710, 0x3F800000},
                                                                          {"1e-30 x 1e-10 (subnormal result)", 0x0DA24260, 0x2EDBE6FF}}};
  for (const auto& [name, first, second] : cases) {
    double best = 1e9;
    for (int round = 0; round < 5; ++round) {
      const auto start = std::chrono::steady_clock::now();
      MulssThroughput(first, second, 40'000'000);
      best = std::min(best, std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 40'000'000.0);
    }
    multiply += std::format("{}{} {:.2f}", multiply.empty() ? "" : ", ", name, best);
  }
  return std::format("   particle writer reload (the four stores, the load, a store; ns an iteration, game / ours / no reload): {}\n   mulss, ns an operation: {}\n", text, multiply);
}

// Every float class at the edges: zeros, subnormals, the smallest normals, ones, the largest, infinities, NaNs (quiet, signalling, payloads).
constexpr std::array<uint32_t, 32> RELOAD_SPECIAL = {0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007FFFFF, 0x807FFFFF, 0x00800000, 0x80800000, 0x3F800000, 0xBF800000, 0x7F7FFFFF,
                                                     0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7F800001, 0xFF800001, 0x7FBFFFFF, 0xFFBFFFFF, 0x7FC00000, 0xFFC00000, 0x7FC00001, 0xFFC00001,
                                                     0x7FFFFFFF, 0xFFFFFFFF, 0x7FA5A5A5, 0xFFDEAD01, 0x0000FFFF, 0x00400000, 0x80400000, 0x12345678, 0x5A5A5A5A, 0xA5A5A5A5};
// Where a reload stub's four lane displacements are (Float4ReloadStub's bytes; Float4ReloadFarStub's: each disp32's low byte).
constexpr std::array<size_t, 4> RELOAD_NEAR_DISPLACEMENT_AT = {5, 12, 20, 28};
constexpr std::array<size_t, 4> RELOAD_FAR_DISPLACEMENT_AT = {5, 15, 26, 37};

// MXCSR for a call: any rounding, DAZ and FTZ at times, sticky flags set on odd rounds.
uint32_t ReloadMxcsr(Random* random, int round) {
  return 0x1F80 | (random->Next() & 3) << 13 | (random->Next() % 4 == 0 ? 1u << 6 : 0u) | (random->Next() % 4 == 0 ? 1u << 15 : 0u)
         | ((round & 1) != 0 ? (random->Next() & 0x3F) : 0);
}

// The sites `of` names hold their jump where `on` has their bit, the game's bytes where not (the test's own variants, not Update's).
void HoldSites(uint32_t on, uint32_t of) {
  namespace ec = twattila::engine_code::internal;
  for (size_t index = 0; index < ec::sites.size(); ++index) {
    if (((of >> index) & 1) == 0) continue;
    const ec::Site& site = ec::sites[index];
    std::memcpy(site.at, (((on >> index) & 1) != 0 ? site.jump : site.game).data(), ec::SITE_SIZE);
  }
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
}

// One reload site alone (engine_code's site `index`): the game's `load_size`-byte movups (`game_bytes`) against the page's stub (the site's
// jump target, `stub_size` bytes with its jump back), each followed by a jump to the harness, on random lanes stored with movss at esp +
// `displacement` (`runs` random ones, then every special value in each lane `contexts` times): every register, xmm0-7, the flags, MXCSR, esp
// and the frame equal. CONTROL: each lane's displacement (its byte at `displacement_at`) moved by 4 must be seen.
int ReloadAlone(size_t index, const uint8_t* game_bytes, size_t load_size, size_t stub_size, uint32_t displacement,
                const std::array<size_t, 4>& displacement_at, int runs, int contexts, Random* random) {
  namespace ec = twattila::engine_code::internal;
  const ec::Site& site = ec::sites[index];
  const uint8_t* stub = site.at + ec::SITE_SIZE + Read32(site.jump.data() + 1);
  uint8_t* blocks = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  CHECK(blocks != nullptr && stub_size <= 0x40 && load_size + 5 <= 0x40);
  const auto make_block = [&](uint32_t slot, const uint8_t* bytes, size_t size) {
    uint8_t* at = blocks + 0x40 * slot;
    std::memcpy(at, bytes, size);
    JumpTo(at + size, reinterpret_cast<const void*>(&ReloadBlockReturn));
    return Address(at);
  };
  const uint32_t game_block = make_block(0, game_bytes, load_size);
  const uint32_t ours_block = make_block(1, stub, stub_size - 5);
  std::array<uint32_t, 4> wrong_blocks = {};
  for (size_t lane = 0; lane < 4; ++lane) {
    std::array<uint8_t, 0x40> bytes = {};
    std::memcpy(bytes.data(), stub, stub_size - 5);
    bytes[displacement_at[lane]] += 4;
    wrong_blocks[lane] = make_block(2 + static_cast<uint32_t>(lane), bytes.data(), stub_size - 5);
  }
  auto game_call = std::make_unique<ReloadBlockCall>();
  auto ours_call = std::make_unique<ReloadBlockCall>();
  CHECK(Address(game_call.get()) % 16 == 0 && Address(ours_call.get()) % 16 == 0);
  // One run of both blocks on the same random input; true if every register, xmm0-7, the flags, MXCSR, esp and the frame are equal.
  int round = 0;
  const auto region_run = [&](uint32_t target, const std::array<uint32_t, 4>* lanes) {
    for (uint32_t& value : game_call->in) value = random->Next();
    game_call->flags_in = (random->Next() & 0x8D5) | 2;
    game_call->mxcsr_in = ReloadMxcsr(random, ++round);
    game_call->lanes_at = displacement;
    for (uint8_t& byte : game_call->frame) byte = static_cast<uint8_t>(random->Next());
    for (uint8_t& byte : std::span(&game_call->xmm_in[0][0], 128)) byte = static_cast<uint8_t>(random->Next());
    for (uint32_t& lane : game_call->lanes) lane = ReloadFloat(random);
    if (lanes != nullptr) std::copy(lanes->begin(), lanes->end(), game_call->lanes);
    *ours_call = *game_call;
    game_call->target = game_block;
    ours_call->target = target;
    EnterReloadBlock(game_call.get());
    EnterReloadBlock(ours_call.get());
    return ReloadBlockEqual(*game_call, *ours_call);
  };
  uint64_t wrong = 0;
  for (int run = 0; run < runs; ++run) {
    if (!region_run(ours_block, nullptr)) {
      ++wrong;
      Report("  reload region of site %zu: run %d NOT equal (lanes %08X %08X %08X %08X)\n", index, run, game_call->lanes[0], game_call->lanes[1], game_call->lanes[2], game_call->lanes[3]);
    }
  }
  CHECK(wrong == 0);
  // Every special value in every lane against random others, then the specials in all four lanes at once.
  uint64_t special_runs = 0;
  for (const uint32_t value : RELOAD_SPECIAL) {
    for (size_t lane = 0; lane < 4; ++lane) {
      for (int context = 0; context < contexts; ++context) {
        std::array<uint32_t, 4> lanes = {ReloadFloat(random), ReloadFloat(random), ReloadFloat(random), ReloadFloat(random)};
        lanes[lane] = value;
        CHECK(region_run(ours_block, &lanes));
        ++special_runs;
      }
    }
    for (int context = 0; context < contexts / 10; ++context) {
      const std::array<uint32_t, 4> lanes = {value, value, RELOAD_SPECIAL[random->Next() % RELOAD_SPECIAL.size()], RELOAD_SPECIAL[random->Next() % RELOAD_SPECIAL.size()]};
      CHECK(region_run(ours_block, &lanes));
      ++special_runs;
    }
  }
  std::string controls;
  for (size_t lane = 0; lane < 4; ++lane) {
    int seen = 0;
    for (int run = 0; run < 20'000; ++run) seen += region_run(wrong_blocks[lane], nullptr) ? 0 : 1;
    controls += std::format("{}{}", controls.empty() ? "" : " / ", seen);
    CHECK(seen > 19'000);
  }
  std::printf("   %s alone ([esp + 0x%X]): %d random runs and %llu with every special value (NaN payloads, subnormals, -0, infinities) in each lane: every register, xmm0-7, the flags, MXCSR, esp and the frame equal; CONTROL, lane 0 / 1 / 2 / 3 from the wrong address: seen in %s of 20000\n",
              std::string(site.name).c_str(), displacement, runs, special_runs, controls.c_str());
  VirtualFree(blocks, 0, MEM_RELEASE);
  return 0;
}

// A parked thread whose saved Eip is each byte of each site of `mask` (from the byte before to +`last_offset`), the rows switched each time: a site
// changes only with Eip at its first byte or outside its jump's 5 bytes (any write waits while Eip is inside one). The rows end on. `entry_row`: the sites
// are the particle writer's entry prefetch's, which its own row (Update's seventh argument) switches, not Faster Game CPU Code; `sweep_row`: the pending list
// sweep's (the eighth argument).
int ParkedAtSites(uint32_t mask, int last_offset, bool entry_row = false, bool sweep_row = false) {
  namespace ec = twattila::engine_code::internal;
  std::atomic<bool> stop = false;
  std::thread parked([&] {
    while (!stop.load()) Sleep(1);
  });
  HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, GetThreadId(parked.native_handle()));
  CHECK(thread != nullptr);
  bool on = true;
  CHECK((ec::written & mask) == mask);
  int wrong = 0;
  for (size_t index = 0; index < ec::sites.size(); ++index) {
    if (((mask >> index) & 1) == 0) continue;
    const uint8_t* site = ec::sites[index].at;
    for (int offset = -1; offset <= last_offset; ++offset) {
      CHECK(SuspendThread(thread) != static_cast<DWORD>(-1));
      CONTEXT saved = {.ContextFlags = CONTEXT_CONTROL};
      CHECK(GetThreadContext(thread, &saved) != FALSE);
      CONTEXT moved = saved;
      moved.Eip = Address(site + offset);
      CHECK(SetThreadContext(thread, &moved) != FALSE);
      const uint32_t before = ec::written;
      twattila::engine_code::Update(entry_row || sweep_row || !on, twattila::engine_code::Game::ATTILA, false, false, true, true, entry_row ? !on : sweep_row, sweep_row && !on);
      const bool changed = (((ec::written ^ before) >> index) & 1) != 0;
      wrong += (changed != !(offset > 0 && offset < 5)) ? 1 : 0;
      if (((ec::written ^ before) & (entry_row ? ec::ENTRY_PREFETCH_BIT : (sweep_row ? ec::SWEEP_PREFETCH_BIT : ec::CPU_CODE_SITES))) != 0) on = !on;
      CHECK(SetThreadContext(thread, &saved) != FALSE);
      ResumeThread(thread);
    }
  }
  for (int tries = 0; !on && tries < static_cast<int>(ec::MAX_FAILED_WRITES); ++tries) {
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true, entry_row || sweep_row, sweep_row);
    on = (ec::written & mask) == mask;
  }
  stop = true;
  parked.join();
  CloseHandle(thread);
  CHECK(wrong == 0 && on && (ec::written & mask) == mask);
  return 0;
}

int RunReloadTests(const TestBuild& build, uint8_t* game, uint8_t* patched, bool quick) {
  namespace ec = twattila::engine_code::internal;
  const uint32_t tail_rva = build.code->float4_reload_rva;
  const uint32_t function_rva = tail_rva - 0x70B;
  const uint32_t copy_distance = static_cast<uint32_t>(patched - game);
  const uint32_t game_function = Address(game + function_rva);
  const uint32_t patched_function = Address(patched + function_rva);
  constexpr std::array<uint8_t, 5> MOVUPS = {0x0F, 0x10, 0x44, 0x24, 0x40};
  CHECK(std::memcmp(game + function_rva, "\x55\x8B\xEC\x83\xE4\xF0\x81\xEC\xB8\x00\x00\x00\x56\x57\x8B\xF9", 16) == 0);
  const std::array<const uint8_t*, 2> stubs = {ec::page + ec::FLOAT4_RELOAD_PAGE, ec::page + ec::FLOAT4_RELOAD_PAGE + ec::FLOAT4_RELOAD_STRIDE};

  // 14a. The sites and stubs as Prepare made them: the game's movups at the two places, found, written; each stub's jump back is the
  // instruction after its site (the game's `movups [eax], xmm0`), each site's jump goes to its stub; the stubs are the same bytes.
  for (size_t reload = 0; reload < 2; ++reload) {
    const uint32_t site_rva = tail_rva + ec::FLOAT4_RELOAD_AT[reload];
    CHECK(ec::sites[ec::FLOAT4_RELOAD_FIRST_SITE + reload].at == patched + site_rva && ((ec::written >> (ec::FLOAT4_RELOAD_FIRST_SITE + reload)) & 1) != 0);
    CHECK(std::memcmp(game + site_rva, MOVUPS.data(), 5) == 0 && std::memcmp(ec::sites[ec::FLOAT4_RELOAD_FIRST_SITE + reload].game.data(), MOVUPS.data(), 5) == 0);
    CHECK(game[site_rva + 5] == 0x0F && game[site_rva + 6] == 0x11 && game[site_rva + 7] == 0x00);
    CHECK(patched[site_rva] == 0xE9 && Address(patched + site_rva + 5) + Read32(patched + site_rva + 1) == Address(stubs[reload]));
    CHECK(stubs[reload][30] == 0xE9 && Address(stubs[reload] + 35) + Read32(stubs[reload] + 31) == Address(patched + site_rva + 5));
  }
  CHECK(std::memcmp(stubs[0], stubs[1], 30) == 0);
  std::printf("14. particle writer float4 reloads: the two sites (+%X, +%X in the tail) found and written, the stubs jump back after them\n",
              static_cast<uint32_t>(ec::FLOAT4_RELOAD_AT[0]), static_cast<uint32_t>(ec::FLOAT4_RELOAD_AT[1]));

  // 14b. The region alone (both stubs are the same bytes: the first).
  Random random = {.state = 0x14A};
  if (const int failed = ReloadAlone(ec::FLOAT4_RELOAD_FIRST_SITE, MOVUPS.data(), MOVUPS.size(), ec::FLOAT4_RELOAD_STUB.size(), 0x40,
                                     RELOAD_NEAR_DISPLACEMENT_AT, quick ? 2'000'000 : 8'000'000, 2000, &random);
      failed != 0) {
    return failed;
  }

  // 14c. The whole function on a fake object graph, the game's copy against the patched copy with both sites, and with each alone.
  auto world = std::make_unique<ReloadWorld>();
  auto backup = std::make_unique<ReloadWorld>();
  auto result_a = std::make_unique<ReloadWorld>();
  auto result_b = std::make_unique<ReloadWorld>();
  CHECK(Address(world->call.fx_in) % 16 == 0 && Address(world->call.fx_out) % 16 == 0 && Address(world->call.fx_saved) % 16 == 0 && Address(world->call.stack) % 16 == 0);
  {
    const int calls = (quick ? 60'000 : 400'000);
    std::array<uint64_t, 4> paths = {};  // no entry, no matrix, matrix with the jb, matrix with the sqrt (the stack filled 0xFF)
    constexpr std::array<uint32_t, 3> VARIANTS = {0b11u << ec::FLOAT4_RELOAD_FIRST_SITE, 0b01u << ec::FLOAT4_RELOAD_FIRST_SITE, 0b10u << ec::FLOAT4_RELOAD_FIRST_SITE};  // both sites, the colour's alone, the vector's alone
    std::array<uint64_t, 3> wrong = {};
    for (int round = 0; round < calls; ++round) {
      const bool canary = (round % 4 != 3);
      ReloadRandom(world.get(), &random, round % 4 < 2 ? 0 : (round % 4 == 2 ? 1 : 2), game_function, ReloadMxcsr(&random, round), canary);
      *backup = *world;
      backup->call.function = game_function;
      RunReload(world.get(), *backup, result_a.get());
      backup->call.function = patched_function;
      for (size_t variant = 0; variant < VARIANTS.size(); ++variant) {
        HoldSites(VARIANTS[variant], ec::FLOAT4_RELOAD_SITES);
        RunReload(world.get(), *backup, result_b.get());
        if (const char* what = ReloadDiff(*result_a, *result_b, copy_distance); what[0] != 0) {
          ++wrong[variant];
          Report("  reload whole function: round %d, sites %X: %s differs\n", round, VARIANTS[variant] >> ec::FLOAT4_RELOAD_FIRST_SITE, what);
        }
      }
      if (canary) ++paths[ReloadPath(*result_a)];
    }
    HoldSites(VARIANTS[0], ec::FLOAT4_RELOAD_SITES);
    std::printf("   the whole function: %d calls (no table entry %llu, no matrix %llu, matrix and the comiss's jb %llu, matrix and the square root %llu): NOT equal with both sites %llu, the colour's alone %llu, the vector's alone %llu\n",
                calls, paths[0], paths[1], paths[2], paths[3], wrong[0], wrong[1], wrong[2]);
    CHECK(wrong[0] == 0 && wrong[1] == 0 && wrong[2] == 0);
    const uint64_t at_least = (quick ? 500 : 3000);
    CHECK(paths[0] > at_least && paths[1] > at_least && paths[2] > at_least && paths[3] > at_least);
  }

  // 14d. CONTROL for the whole function: each stub with each lane read from the wrong place must be seen in the streams.
  {
    DWORD protection;
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READWRITE, &protection) != FALSE);
    for (size_t reload = 0; reload < 2; ++reload) {
      uint8_t* stub = ec::page + ec::FLOAT4_RELOAD_PAGE + reload * ec::FLOAT4_RELOAD_STRIDE;
      for (size_t lane = 0; lane < 4; ++lane) {
        stub[RELOAD_NEAR_DISPLACEMENT_AT[lane]] += 4;
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        int seen = 0;
        for (int round = 0; round < 1500; ++round) {
          ReloadRandom(world.get(), &random, 0, game_function, 0x1F80, true);
          *backup = *world;
          backup->call.function = game_function;
          RunReload(world.get(), *backup, result_a.get());
          backup->call.function = patched_function;
          RunReload(world.get(), *backup, result_b.get());
          seen += (ReloadDiff(*result_a, *result_b, copy_distance)[0] != 0) ? 1 : 0;
        }
        stub[RELOAD_NEAR_DISPLACEMENT_AT[lane]] -= 4;
        FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
        // Not all 1500: no table entry takes ~20 % of the calls, and without the matrix the vector's lanes come from constants (two of them
        // equal), so a lane read from its neighbour's place gives the same bits there.
        std::printf("   CONTROL, the %s stub's lane %zu from the wrong address: %d of 1500 calls seen as different\n", reload == 0 ? "colour" : "vector", lane, seen);
        CHECK(seen > 400);
      }
    }
    CHECK(VirtualProtect(ec::page, ec::PAGE_SIZE, PAGE_EXECUTE_READ, &protection) != FALSE);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  }

  // 14e. The writer switched with the row (CPU code) on and off 200 times while 4 threads call both copies on their own graphs.
  {
    std::atomic<bool> stop = false;
    std::atomic<uint64_t> bad = 0;
    std::atomic<uint64_t> calls = 0;
    const auto game_writer = reinterpret_cast<ReloadWriter>(game + function_rva);
    const auto patched_writer = reinterpret_cast<ReloadWriter>(patched + function_rva);
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) {
      threads.emplace_back([&, worker] {
        struct alignas(16) Graph {
          std::array<uint8_t, RELOAD_WORLD_SIZE> bytes;
        };
        auto graph = std::make_unique<Graph>();
        auto before = std::make_unique<Graph>();
        auto after_game = std::make_unique<Graph>();
        Random thread_random = {.state = 0xE0E0ull + static_cast<uint64_t>(worker)};
        while (!stop.load()) {
          const uint32_t argument = ReloadFillWorld(graph->bytes.data(), &thread_random, static_cast<int>(thread_random.Next() % 3));
          float first_argument;
          std::memcpy(&first_argument, &argument, 4);
          *before = *graph;
          // The inputs aren't written by the function (only the streams are, with the same bits each call): repeated calls keep the
          // threads inside the function, and its stubs, most of the time.
          for (int repeat = 0; repeat < 32; ++repeat) {
            game_writer(graph->bytes.data(), nullptr, first_argument, reinterpret_cast<const float*>(graph->bytes.data() + RELOAD_SIX));
          }
          *after_game = *graph;
          *graph = *before;
          for (int repeat = 0; repeat < 32; ++repeat) {
            patched_writer(graph->bytes.data(), nullptr, first_argument, reinterpret_cast<const float*>(graph->bytes.data() + RELOAD_SIX));
          }
          if (graph->bytes != after_game->bytes) ++bad;
          calls += 32;
        }
      });
    }
    int busy = 0;
    bool on = true;
    for (int switches = 0; switches < 200; ++switches) {
      on = !on;
      int tries = 0;
      do {
        twattila::engine_code::Update(on, twattila::engine_code::Game::ATTILA, false, false, true, true);
        ++tries;
      } while ((((ec::written >> ec::FLOAT4_RELOAD_FIRST_SITE) & 1) != 0) != on && tries < static_cast<int>(ec::MAX_FAILED_WRITES));
      busy += tries > 1 ? 1 : 0;
    }
    twattila::engine_code::Update(true, twattila::engine_code::Game::ATTILA, false, false, true, true);
    stop = true;
    for (auto& thread : threads) thread.join();
    std::printf("   200 switches while 4 threads made %llu calls of each copy on their own graphs: %llu wrong (%d writes waited for a thread inside a site)\n", calls.load(), bad.load(), busy);
    CHECK(bad.load() == 0 && ((ec::written >> ec::FLOAT4_RELOAD_FIRST_SITE) & 3) == 3);
  }

  // 14f. A parked thread whose saved Eip is each byte of each site (and the byte before, the bytes after): the row switched, a site
  // changes only with Eip at its first byte or outside it.
  if (const int failed = ParkedAtSites(ec::FLOAT4_RELOAD_SITES, 6); failed != 0) return failed;
  std::printf("   a parked thread at each site -1 .. +6: refused at +1 .. +4 and written at the others\n");

  // 14g. Timing: the whole function with streams in hot lines, in cold ones (random lines of 256 MB), and in write-combined memory (as a
  // mapped vertex buffer is: rows appended in order, 16 bytes a call to each stream), game / ours, the best of 5 (part 6 prints the region's
  // timing after a store backlog and the multiply's).
  {
    constexpr size_t BUFFER = 256u << 20;
    constexpr size_t WRITE_COMBINED_STREAM = 8u << 20;
    auto* buffer = static_cast<uint8_t*>(VirtualAlloc(nullptr, BUFFER, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* write_combined = static_cast<uint8_t*>(VirtualAlloc(nullptr, 6 * WRITE_COMBINED_STREAM, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE | PAGE_WRITECOMBINE));
    CHECK(buffer != nullptr && write_combined != nullptr);
    std::memset(buffer, 1, BUFFER);
    const uint32_t argument = ReloadFillWorld(world->bytes.data(), &random, 0);
    float first_argument;
    std::memcpy(&first_argument, &argument, 4);
    uint8_t* bytes = world->bytes.data();
    Write32(bytes + RELOAD_THIS + 0xD0, 0);  // table entry 0, the matrix path (the byte at D + 0x1c set), Y present
    bytes[RELOAD_D + 0x1C] = 1;
    Write32(bytes + RELOAD_X + 0x180, Address(bytes + RELOAD_Y));
    const auto game_writer = reinterpret_cast<ReloadWriter>(game + function_rva);
    const auto patched_writer = reinterpret_cast<ReloadWriter>(patched + function_rva);
    std::string line;
    constexpr std::array<const char*, 3> STREAMS = {"hot", "cold", "write-combined"};
    for (size_t mode = 0; mode < STREAMS.size(); ++mode) {
      std::array<double, 2> best = {1e9, 1e9};
      for (int round = 0; round < 5; ++round) {
        for (size_t which = 0; which < 2; ++which) {
          const auto writer = (which == 0 ? game_writer : patched_writer);
          Random stream_random = {.state = 0x57AA};
          constexpr int CALLS = 400'000;
          const auto start = std::chrono::steady_clock::now();
          for (int index = 0; index < CALLS; ++index) {
            for (uint32_t stream = 0; stream < 6 && mode != 0; ++stream) {
              const size_t at = (mode == 1 ? ((static_cast<size_t>(stream_random.Next()) << 6) & (BUFFER - 64))
                                           : (stream * WRITE_COMBINED_STREAM + ((static_cast<size_t>(index) * 16) & (WRITE_COMBINED_STREAM - 32))));
              Write32(bytes + RELOAD_TABLE + 4 * stream, Address((mode == 1 ? buffer : write_combined) + at));
            }
            writer(bytes, nullptr, first_argument, reinterpret_cast<const float*>(bytes + RELOAD_SIX));
          }
          best[which] = std::min(best[which], std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / CALLS);
        }
      }
      line += std::format("{}{} streams {:.1f} / {:.1f}", line.empty() ? "" : ", ", STREAMS[mode], best[0], best[1]);
    }
    VirtualFree(buffer, 0, MEM_RELEASE);
    VirtualFree(write_combined, 0, MEM_RELEASE);
    std::printf("   ns a call of the whole function (fake graph, the matrix path), game / ours: %s\n", line.c_str());
  }
  return 0;
}
