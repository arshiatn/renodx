// twattila/buffer_pool.hpp (Fewer Driver Waits: the game's dynamic constant buffers through one ring of NO_OVERWRITE
// pieces) on WARP (the same d3d11.dll runtime bodies as the game's device), a single-threaded device as the game's, x86
// and amd64:
// 1. The ring's 35 bodies (the three copies' versions after a Flush among them): inside d3d11.dll, distinct, none
//    starting with a jump. Two devices known: the install waits (logged once, no refusal), in once the game's stands
//    alone. Refused (logged, nothing hooked): a multithreaded one. The row's Off latched (the ring stays in, logged
//    once a latch; its writes, Get and copies as on). CONTROL: unlatched, out at once.
//    A stop while latched (another thread's call): out at the next present, the cause and the restart hint logged.
// 2. The rules it relies on: the caps; a copy into a dynamic constant buffer lands, UpdateSubresource into one does not.
// 3. Pixels, compute output and copies, ring off vs on, each run on a fresh device: per-draw DISCARDs of a 208 B buffer
//    (the shaders read past its end), a once-a-frame one bound before its DISCARD, NO_OVERWRITE windows of a 1 KB one
//    (Set1), a window past a buffer's end (the zero segment), VS / PS / CS, Get1 / Set1 round trips, a private state
//    swapped in and back (d3d11's lazy application; drawn after the swap back before the next upload), a second one
//    bound before the install and drawn at once when first swapped in, ClearState, copies from and into game buffers
//    (each through both versions of its body: after a Flush or not; one into a raw-views buffer), buffers bound and
//    mapped before the install (the ring in at frame 0's present), a script on the fresh ring at frame 1 (b drawn from
//    two segments, the ring filled with 64 KB uploads, b again, the GPU done, the ring wrapped over every piece, b drawn
//    again), and after the ring's removal the private state's binds and a draw there. Equal: the ring; the full ring
//    (busy pieces: one wait, then the fallback, each logged once); the full ring, then the wrap with b still bound (b
//    falls back, not into the closed piece); another thread's call (the ring unwinds at the render thread's next call,
//    then stays off, logged); the map trace's measure path on beside it, installed after the ring or before it (its
//    records the caller's buffers). None logs a table check. CONTROLS (must differ): no rebind, the Get hooks off, a piece reused
//    early (the script's first draws: deterministic), the copy hooks off, the swap's settling call left out, a known
//    state's rebinds at its swap left out, a new state's seed left out.
// 4. Get / Get1 / Map parity against a second device (ring off) over random Set / Set1 (invalid windows, null arrays,
//    windows past a buffer's end) / Map-Unmap / ClearState / swaps: buffers and windows equal; every game buffer's
//    references its own plus one per slot of the ring at every step, its own alone after the ring's removal (the
//    device's too). A Get past the slots: bounded to them. A buffer the game lets go of while bound lives until
//    unbound, as natively; CONTROL: without the ring's reference it goes at once. Our own dynamic buffers (made below
//    ReShade) never tracked. Out by buffer_pool::Uninstall on the render thread, slots bound.
// 5. The device's end with the ring on: its hooks out, its references let go, its records freed at the next present;
//    every body's first bytes back, the modules let go.
// 6. The unwind's edges: a buffer destroyed since the present gets no write-back, another thread's bind is what the next
//    Get returns; a lost device (an event's error): no write-backs, off (logged); a body in the table that the
//    install never saw (a copy's, Map's: out at the present; PS Set1's: out at a swap), logged; a swap below the hook
//    (the next swap's replaced state not the active one): out at that swap, logged; a buffer destroyed while mapped
//    through the ring: its map ends (the ring out at the row's off).
// 7. buffer_pool::Uninstall off the render thread: the hooks kept to the process's end, the ring unwound at the render
//    thread's next call (its buffer written back and bound again).
// 8. The reclaim rule: 300 frames of 64 B DISCARDs with a straggler (written every 30 frames), the pieces' events passing
//    3 presents after their End: at most 12 pieces, every frame's compute output equal to the native run; CONTROL with
//    the old retire distance (the next piece alone): the ring grows to its cap. Events passing only with a flush: no
//    growth, equal. The record itself a straggler in a retired piece: written once.
// 9. The full ring's wait (no growth past piece_cap): an event passing after some polls without a flush: one wait, no
//    fallback, logged once; never passing: one wait, then the fallback (logged once), no further wait or flush, the
//    buffer's bytes as written; after a switch that succeeded, the next full ring waits again (not logged again).
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "src/games/twattila/buffer_pool.hpp"

using Microsoft::WRL::ComPtr;
namespace bp = twattila::buffer_pool;
namespace hooks = twattila::context_hooks;
namespace loss = twattila::device_loss;
// 64 KB uploads that fill the ring's first pieces (4 a piece).
constexpr size_t RING_UPLOADS = bp::internal::FIRST_PIECES * (bp::internal::PIECE_BYTES / 65536);

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::fflush(stdout); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Buffer pool test";
static std::vector<std::string> log_lines;
extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  std::printf("  log: %s\n", message);
  log_lines.emplace_back(message);
}
}

static size_t Logged(const char* text) {
  return static_cast<size_t>(std::ranges::count_if(log_lines, [text](const std::string& line) { return line.find(text) != std::string::npos; }));
}

static uint32_t Word(uint32_t seed, uint32_t index) {
  uint32_t value = seed * 0x9E3779B9u + index * 0x85EBCA6Bu + 1;
  value ^= value >> 15;
  value *= 0x2C1B3C6Du;
  return value ^ (value >> 12);
}

static ULONG References(IUnknown* object) {
  object->AddRef();
  return object->Release();
}

// ReShade's destroy_resource for a game buffer: its private data's last release, at the buffer's end (`gone` set).
constexpr GUID WATCHER = {0x1f0e7c42, 0x6a1b, 0x4d2e, {0x9c, 0x55, 0x0b, 0x7e, 0x31, 0xd4, 0x8a, 0x26}};
class Watcher final : public IUnknown {
 public:
  Watcher(const ID3D11Resource* resource, bool* gone) : resource_(resource), gone_(gone) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override {
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG left = --references_;
    if (left == 0) {
      bp::Forget(resource_);
      if (gone_ != nullptr) *gone_ = true;
      delete this;
    }
    return left;
  }

 private:
  const ID3D11Resource* resource_;
  bool* gone_;
  ULONG references_ = 1;
};

static void Watch(ID3D11Buffer* buffer, bool* gone) {
  auto* watcher = new Watcher(buffer, gone);
  buffer->SetPrivateDataInterface(WATCHER, watcher);
  watcher->Release();
}

static ComPtr<ID3D11Buffer> Buffer(ID3D11Device* device, UINT bytes, D3D11_USAGE usage, UINT bind, UINT cpu, uint32_t seed) {
  std::vector<uint32_t> words(bytes / 4);
  for (UINT index = 0; index < words.size(); ++index) words[index] = Word(seed, index);
  const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = usage, .BindFlags = bind, .CPUAccessFlags = cpu};
  const D3D11_SUBRESOURCE_DATA data = {.pSysMem = words.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, (usage == D3D11_USAGE_STAGING ? nullptr : &data), buffer.GetAddressOf());
  return buffer;
}

// The context's bodies as context_hooks reads them: the table settled, the copies' two versions (after a copy, after a
// Flush: FLUSHED_COPIES).
static std::array<void*, hooks::internal::BODIES> ReadBodies(ID3D11Device* device, ID3D11DeviceContext* context) {
  const auto read = [context] {
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    context->IAGetPrimitiveTopology(&topology);
    void* const* table = *reinterpret_cast<void* const* const*>(context);
    std::array<void*, hooks::internal::BODIES> bodies = {};
    for (size_t body = 0; body < bodies.size(); ++body) bodies[body] = table[hooks::internal::BODY_SLOTS[body]];
    return bodies;
  };
  const D3D11_BUFFER_DESC desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  ComPtr<ID3D11Buffer> from;
  ComPtr<ID3D11Buffer> to;
  device->CreateBuffer(&desc, nullptr, from.GetAddressOf());
  device->CreateBuffer(&desc, nullptr, to.GetAddressOf());
  context->CopyResource(to.Get(), from.Get());
  auto bodies = read();
  context->Flush();
  const auto flushed = read();
  for (size_t index = 0; index < 3; ++index) {
    bodies[hooks::internal::FLUSHED_COPIES + index] = flushed[hooks::internal::COPY_SUBRESOURCE_REGION + index];
  }
  return bodies;
}

// A buffer the game makes through ReShade's proxy: init_resource (its initial data), its end watched.
static ComPtr<ID3D11Buffer> GameBuffer(ID3D11Device* device, UINT bytes, uint32_t seed) {
  auto buffer = Buffer(device, bytes, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, seed);
  std::vector<uint32_t> words(bytes / 4);
  for (UINT index = 0; index < words.size(); ++index) words[index] = Word(seed, index);
  bp::Track(device, buffer.Get(), words.data());
  Watch(buffer.Get(), nullptr);
  return buffer;
}

// A buffer's bytes as the GPU has them (a copy to staging).
static std::vector<uint32_t> Read(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Resource* resource, UINT bytes) {
  const auto staging = Buffer(device, bytes, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0);
  context->CopyResource(staging.Get(), resource);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  std::vector<uint32_t> words(bytes / 4);
  if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    std::memcpy(words.data(), mapped.pData, bytes);
    context->Unmap(staging.Get(), 0);
  }
  return words;
}

constexpr const char* SOURCE = R"(
cbuffer A : register(b0) { uint4 a[16]; };
cbuffer B : register(b1) { uint4 b[16]; };
cbuffer C : register(b2) { uint4 c[32]; };
cbuffer D : register(b3) { uint4 d[4096]; };
struct V { float4 position : SV_Position; nointerpolation uint4 value : VALUE; };
V Vs(uint id : SV_VertexID) {
  V v;
  const float2 uv = float2((id << 1) & 2, id & 2);
  v.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  v.value = a[1] ^ (a[15] << 1);
  return v;
}
uint4 Ps(V v) : SV_Target { return v.value * 3 + a[3] + a[13] + b[0] * 5 + b[7] * 7 + c[0] * 11 + c[15] * 13 + c[16] * 17 + d[0] * 19 + d[4095] * 23; }
RWStructuredBuffer<uint4> output : register(u0);
[numthreads(1, 1, 1)] void Cs() { output[a[0].w & 1023] = a[2] + b[2] * 3 + c[1] * 5 + a[14]; }
)";
static ComPtr<ID3DBlob> vs_blob;
static ComPtr<ID3DBlob> ps_blob;
static ComPtr<ID3DBlob> cs_blob;

static bool Compile() {
  for (auto [entry, target, blob] : {std::tuple{"Vs", "vs_5_0", std::addressof(vs_blob)}, std::tuple{"Ps", "ps_5_0", std::addressof(ps_blob)},
                                    std::tuple{"Cs", "cs_5_0", std::addressof(cs_blob)}}) {
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3DCompile(SOURCE, std::strlen(SOURCE), nullptr, nullptr, nullptr, entry, target, 0, 0, blob->GetAddressOf(), errors.GetAddressOf()))) {
      std::printf("shader %s: %s\n", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
      return false;
    }
  }
  return true;
}

struct Device {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11Device1> device1;
  ComPtr<ID3D11DeviceContext1> context1;
  ComPtr<ID3DDeviceContextState> private_state;
  ComPtr<ID3DDeviceContextState> bound_state;  // Run's second private state (bound before the install)
};

static Device MakeDevice(UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED) {
  Device made;
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, &level, 1, D3D11_SDK_VERSION, made.device.GetAddressOf(), nullptr,
                    made.context.GetAddressOf());
  made.device.As(&made.device1);
  made.context.As(&made.context1);
  for (ComPtr<ID3DDeviceContextState>* state : {std::addressof(made.private_state), std::addressof(made.bound_state)}) {
    made.device1->CreateDeviceContextState(D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                           nullptr, state->GetAddressOf());
  }
  return made;
}

enum class Control { NONE, FULL, FOREIGN, MEASURE, MEASURE_FIRST, WRAP, NO_REBIND, GET_OFF, EARLY_REUSE, COPY_OFF, SETTLE_OFF, NO_FOUND_REBIND, NO_SEED };
constexpr std::array<const char*, 13> CONTROL_NAMES = {"ring",          "full ring",           "another thread",       "measure path beside",
                                                       "measure path first", "full ring, then wrap", "CONTROL no rebind", "CONTROL Get off",
                                                       "CONTROL early reuse", "CONTROL copy off",  "CONTROL no settle",  "CONTROL no swap rebind",
                                                       "CONTROL no seed"};
// WRAP: the pieces' events busy until the script's GPU wait, then polled for real.
static bool wrap_polls = false;
using Get1 = void (STDMETHODCALLTYPE ID3D11DeviceContext1::*)(UINT, UINT, ID3D11Buffer**, UINT*, UINT*);

struct Output {
  std::vector<uint32_t> pixels;
  std::vector<uint32_t> computed;
  std::vector<uint32_t> copies;
  std::vector<uint32_t> after;   // after the ring's removal: the private state's binds (VS, PS, CS slots 0-3)
  std::vector<uint32_t> script;  // the frame-1 script's pixels
  std::string failed;            // empty: ran
};

struct Measured {
  hooks::Kind kind;
  ID3D11Resource* resource;
  std::vector<ID3D11Buffer*> buffers;
  bool ring_buffer;  // one of the ring's own buffers (a piece, the zero segment) in it, as they were then
};
static std::vector<Measured> measured;
static void Measure(const hooks::Call& call) {
  Measured record = {.kind = call.kind, .resource = call.resource};
  if (call.buffers != nullptr) record.buffers.assign(call.buffers, call.buffers + std::min<UINT>(call.count, 14));
  const auto ring_buffer = [](const void* buffer) {
    return buffer != nullptr
           && (buffer == bp::internal::pool.zero.Get()
               || std::ranges::any_of(bp::internal::pool.pieces, [buffer](const auto& piece) { return piece->buffer.Get() == buffer; }));
  };
  record.ring_buffer = ring_buffer(call.resource) || std::ranges::any_of(record.buffers, ring_buffer);
  measured.push_back(std::move(record));
}

// The CONTROLs that undo the ring's rebinds: every stage's slots put back after `body` as d3d11 had them before it (the
// ring's entries as `body` left them).
template <typename Body>
static void KeepBinds(Body body) {
  auto* context = bp::internal::pool.context;
  constexpr UINT SLOTS = bp::internal::SLOTS;
  std::array<std::array<ID3D11Buffer*, SLOTS>, hooks::STAGES> buffers = {};
  std::array<std::array<UINT, SLOTS>, hooks::STAGES> firsts = {};
  std::array<std::array<UINT, SLOTS>, hooks::STAGES> counts = {};
  for (size_t stage = 0; stage < hooks::STAGES; ++stage) {
    bp::internal::Native<hooks::internal::GetConstantBuffers1Function>(hooks::internal::GET_CONSTANT_BUFFERS1 + stage)(
        context, 0, SLOTS, buffers[stage].data(), firsts[stage].data(), counts[stage].data());
  }
  body();
  for (size_t stage = 0; stage < hooks::STAGES; ++stage) {
    bp::internal::Native<hooks::internal::SetConstantBuffers1Function>(hooks::internal::SET_CONSTANT_BUFFERS1 + stage)(
        context, 0, SLOTS, buffers[stage].data(), firsts[stage].data(), counts[stage].data());
    for (ID3D11Buffer* buffer : buffers[stage]) {
      if (buffer != nullptr) buffer->Release();
    }
  }
}

// NO_REBIND: no slot rebound at an upload.
static bool UnmapWithoutRebind(ID3D11Resource* resource, UINT subresource) {
  bool done = false;
  KeepBinds([&] { done = bp::internal::RingUnmap(resource, subresource); });
  return done;
}

// NO_FOUND_REBIND (KNOWN): a known state's slots left as d3d11 applied them at its swap (its entries on the segments, as
// without the found path's BindSlot). NO_SEED: a new state's slots neither taken nor rebound (its table empty, as
// without its Seed).
template <bool KNOWN>
static void SwapWithout(ID3DDeviceContextState* state, ID3DDeviceContextState* previous_state) {
  const bool known = std::ranges::any_of(bp::internal::pool.states, [state](const auto& table) { return table->key == state && !table->dead->load(); });
  if (known != KNOWN) {
    bp::internal::RingSwap(state, previous_state);
    return;
  }
  KeepBinds([&] {
    bp::internal::RingSwap(state, previous_state);
    if (KNOWN || bp::internal::pool.stopped) return;
    for (size_t stage = 0; stage < hooks::STAGES; ++stage) {
      for (uint32_t slots = bp::internal::pool.active->used[stage]; slots != 0; slots &= slots - 1) {
        bp::internal::Assign(stage, static_cast<UINT>(std::countr_zero(slots)), nullptr, 0, 0, {});
      }
    }
  });
}

// The frames, ring off or on (with `control`): the render target, the compute output and every frame's copies.
static Output Run(bool ring, Control control, uint32_t frames, uint32_t draws) {
  Output out;
  const auto ring_paths = bp::internal::ring_paths;
  if (ring) {
    switch (control) {
      case Control::NO_REBIND: bp::internal::ring_paths.unmap = UnmapWithoutRebind; break;
      case Control::NO_FOUND_REBIND: bp::internal::ring_paths.swap = SwapWithout<true>; break;
      case Control::NO_SEED: bp::internal::ring_paths.swap = SwapWithout<false>; break;
      case Control::GET_OFF: bp::internal::ring_paths.get = [](hooks::Stage, UINT, UINT, ID3D11Buffer**, UINT*, UINT*) {}; break;
      case Control::COPY_OFF:
        bp::internal::ring_paths.copy = [](ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, const D3D11_BOX*, const UINT*) { return false; };
        bp::internal::ring_paths.copy_resource = [](ID3D11Resource*, ID3D11Resource*) { return false; };
        break;
      case Control::EARLY_REUSE:
        bp::internal::piece_cap = 2;
        bp::internal::piece_done = [](ID3D11DeviceContext*, ID3D11Query*, bool) { return S_OK; };
        break;
      case Control::FULL:
        bp::internal::piece_cap = bp::internal::FIRST_PIECES;
        bp::internal::piece_done = [](ID3D11DeviceContext*, ID3D11Query*, bool) { return S_FALSE; };
        break;
      case Control::WRAP:
        // The old retire distance (the next piece alone): b goes into the last piece, which keeps room beside it when the
        // ring is full.
        bp::internal::piece_cap = bp::internal::FIRST_PIECES;
        bp::internal::retire_ahead = 1;
        bp::internal::piece_done = [](ID3D11DeviceContext* context, ID3D11Query* query, bool flush) {
          return (wrap_polls ? context->GetData(query, nullptr, 0, (flush ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH)) : S_FALSE);
        };
        break;
      default: break;
    }
  }
  wrap_polls = false;
  std::vector<uint32_t> script;
  Device made = MakeDevice();
  auto* context = made.context.Get();
  auto* context1 = made.context1.Get();
  loss::devices = {{.device = made.device.Get(), .single_threaded = true}};
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11PixelShader> ps;
  ComPtr<ID3D11ComputeShader> cs;
  made.device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, vs.GetAddressOf());
  made.device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, ps.GetAddressOf());
  made.device->CreateComputeShader(cs_blob->GetBufferPointer(), cs_blob->GetBufferSize(), nullptr, cs.GetAddressOf());
  const D3D11_TEXTURE2D_DESC target_desc = {.Width = 128, .Height = 64, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_UINT,
                                            .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  made.device->CreateTexture2D(&target_desc, nullptr, target.GetAddressOf());
  made.device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf());
  const D3D11_BUFFER_DESC output_desc = {.ByteWidth = 1024 * 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                         .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = 16};
  ComPtr<ID3D11Buffer> output;
  ComPtr<ID3D11UnorderedAccessView> output_view;
  made.device->CreateBuffer(&output_desc, nullptr, output.GetAddressOf());
  const D3D11_UNORDERED_ACCESS_VIEW_DESC output_view_desc = {.Format = DXGI_FORMAT_UNKNOWN, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
                                                             .Buffer = {.FirstElement = 0, .NumElements = 1024}};
  made.device->CreateUnorderedAccessView(output.Get(), &output_view_desc, output_view.GetAddressOf());
  const UINT zeros[4] = {};
  context->ClearUnorderedAccessViewUint(output_view.Get(), zeros);
  const float clear[4] = {};
  context->ClearRenderTargetView(target_view.Get(), clear);
  const auto a = GameBuffer(made.device.Get(), 208, 1);
  const auto b = GameBuffer(made.device.Get(), 256, 2);
  const auto c = GameBuffer(made.device.Get(), 1024, 3);
  // 64 KB, a few DISCARDs a frame in a row: a full piece every 4 of them (the early reuse control's draws in flight).
  const auto d = GameBuffer(made.device.Get(), 65536, 9);
  const auto own = Buffer(made.device.Get(), 256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 4);
  const auto copy_a = Buffer(made.device.Get(), 208, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 5);
  const auto copy_b = Buffer(made.device.Get(), 256, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 6);
  const auto copy_c = Buffer(made.device.Get(), 1024, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 7);
  const auto b_source = Buffer(made.device.Get(), 256, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 8);
  // As water_effects' copy of the game's lighting buffer.
  const D3D11_BUFFER_DESC raw_desc = {.ByteWidth = 256, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                      .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS};
  ComPtr<ID3D11Buffer> copy_raw;
  made.device->CreateBuffer(&raw_desc, nullptr, copy_raw.GetAddressOf());
  UINT window = 0;
  const auto write = [&](ID3D11Buffer* buffer, D3D11_MAP type, UINT offset, UINT bytes, uint32_t seed, int index_word) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(buffer, 0, type, 0, &mapped))) return;
    auto* words = static_cast<uint32_t*>(mapped.pData) + (offset / 4);
    for (UINT index = 0; index < bytes / 4; ++index) words[index] = Word(seed, index);
    if (index_word >= 0) words[3] = static_cast<uint32_t>(index_word) & 1023;
    context->Unmap(buffer, 0);
  };
  const auto bind_window = [&] {
    const UINT first = window * 16;
    const UINT count = 16;
    context1->PSSetConstantBuffers1(2, 1, c.GetAddressOf(), &first, &count);
    context1->CSSetConstantBuffers1(2, 1, c.GetAddressOf(), &first, &count);
  };
  const auto bind_all = [&](bool swapped) {
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    context->CSSetShader(cs.Get(), nullptr, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
    context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
    context->VSSetConstantBuffers(0, 1, a.GetAddressOf());
    ID3D11Buffer* const pair[2] = {(swapped ? b.Get() : a.Get()), (swapped ? a.Get() : b.Get())};
    context->PSSetConstantBuffers(0, 2, pair);
    context->PSSetConstantBuffers(3, 1, d.GetAddressOf());
    context->CSSetConstantBuffers(0, 2, pair);
    bind_window();
  };
  uint32_t index = 0;
  const auto draw = [&] {
    const D3D11_VIEWPORT viewport = {.TopLeftX = static_cast<float>(index % 128), .TopLeftY = static_cast<float>((index / 128) % 64), .Width = 1,
                                     .Height = 1, .MinDepth = 0, .MaxDepth = 1};
    context->RSSetViewports(1, &viewport);
    context->Draw(3, 0);
  };
  const auto native_swap = [&](ID3DDeviceContextState* state, ID3DDeviceContextState** previous) {
    bp::internal::Native<hooks::internal::SwapStateFunction>(hooks::internal::SWAP_STATE)(context1, state, previous);
  };
  // The second private state's binds, before the install (the ring sees it first at frame 4's swap).
  {
    ComPtr<ID3DDeviceContextState> game_state;
    context1->SwapDeviceContextState(made.bound_state.Get(), game_state.GetAddressOf());
    bind_all(true);
    context1->SwapDeviceContextState(game_state.Get(), nullptr);
  }
  bind_all(false);
  const bool measuring = ring && (control == Control::MEASURE || control == Control::MEASURE_FIRST);
  for (uint32_t frame = 0; frame < frames; ++frame) {
    if (measuring && frame == (control == Control::MEASURE ? 2u : 0u)) {
      measured.clear();
      if (!hooks::Install(context, Measure, false)) out.failed = "the measure path not installed beside the ring";
    }
    if (measuring && frame == 3) hooks::Uninstall();
    if (control == Control::FOREIGN && frame == 3) {
      std::thread([&] { context->PSSetConstantBuffers(5, 1, own.GetAddressOf()); }).join();
      if (ring) {
        // The render thread's next call: the ring unwinds (every slot on the game's buffer itself).
        ID3D11Buffer* probe = nullptr;
        context->VSGetConstantBuffers(0, 1, &probe);
        if (probe != a.Get()) out.failed = "Get after the unwind";
        if (probe != nullptr) probe->Release();
        ID3D11Buffer* bound = nullptr;
        UINT first = 0;
        UINT count = 0;
        bp::internal::Native<hooks::internal::GetConstantBuffers1Function>(hooks::internal::GET_CONSTANT_BUFFERS1 + 4)(context1, 0, 1, &bound,
                                                                                                                    &first, &count);
        if (!bp::internal::pool.stopped || bound != a.Get()) out.failed = "not unwound after another thread's call";
        if (bound != nullptr) bound->Release();
      }
    }
    if (frame == 1) {
      // The ring fresh (in at frame 0's present; FIRST_PIECES pieces of 4 uploads of d). b at piece 0's units 0 and 1,
      // each drawn; RING_UPLOADS - 1 uploads of d (3 in piece 0): b retired into the piece half a ring on (WRAP: the
      // last piece's first), the last upload finds piece 0 ended (WRAP: busy, the full ring with room left in the
      // current piece; EARLY_REUSE: free, overwritten while the two draws wait in WARP: no flush). b again, drawn; the
      // GPU done (WRAP: polled for real now); RING_UPLOADS uploads of d wrap over every piece, b drawn again.
      const auto script_draw = [&] {
        ++index;
        script.push_back(index);
        draw();
      };
      write(b.Get(), D3D11_MAP_WRITE_DISCARD, 0, 256, 2001, -1);
      script_draw();
      write(b.Get(), D3D11_MAP_WRITE_DISCARD, 0, 256, 2002, -1);
      script_draw();
      for (uint32_t upload = 0; upload < RING_UPLOADS - 1; ++upload) write(d.Get(), D3D11_MAP_WRITE_DISCARD, 0, 65536, 3000 + upload, -1);
      write(b.Get(), D3D11_MAP_WRITE_DISCARD, 0, 256, 2003, -1);
      if (ring && control == Control::WRAP && bp::internal::Find(b.Get())->piece != nullptr) out.failed = "b written into the full ring's closed piece";
      script_draw();
      wrap_polls = true;
      context->Flush();
      Read(made.device.Get(), context, b_source.Get(), 256);
      for (uint32_t upload = 0; upload < RING_UPLOADS; ++upload) write(d.Get(), D3D11_MAP_WRITE_DISCARD, 0, 65536, 4000 + upload, -1);
      script_draw();
      if (ring && control == Control::WRAP) {
        bp::internal::piece_cap = bp::internal::MAX_PIECES;
        bp::internal::piece_done = nullptr;
        bp::internal::retire_ahead = 0;
      }
    }
    write(b.Get(), D3D11_MAP_WRITE_DISCARD, 0, 256, 1000 + frame, -1);
    if (frame == 4) context->CopyResource(b.Get(), b_source.Get());
    for (uint32_t step = 0; step < draws; ++step) {
      ++index;
      if (frame == 1 && step == 100) {
        // b's window from its end (16 constants): zeros (the ring: the zero segment, b's bytes in a piece).
        const UINT first = 16;
        const UINT count = 16;
        context1->PSSetConstantBuffers1(1, 1, b.GetAddressOf(), &first, &count);
      }
      if (frame == 1 && step == 110) context->PSSetConstantBuffers(1, 1, b.GetAddressOf());
      if (step % 9 != 4) write(a.Get(), D3D11_MAP_WRITE_DISCARD, 0, 208, index, static_cast<int>(index));
      if (step >= 200 && step < 212) write(d.Get(), D3D11_MAP_WRITE_DISCARD, 0, 65536, index * 3, -1);
      if (step % 5 == 0) {
        window = (window + 1) % 4;
        write(c.Get(), (window == 0 ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE), window * 256, 256, index * 7, -1);
        bind_window();
      }
      if (step % 11 == 0) {
        std::array<ID3D11Buffer*, 3> held = {};
        std::array<UINT, 3> firsts = {};
        std::array<UINT, 3> counts = {};
        context1->PSGetConstantBuffers1(0, 3, held.data(), firsts.data(), counts.data());
        context->PSSetConstantBuffers(0, 1, own.GetAddressOf());
        context1->PSSetConstantBuffers1(0, 3, held.data(), firsts.data(), counts.data());
        for (ID3D11Buffer* buffer : held) {
          if (buffer != nullptr) buffer->Release();
        }
      }
      if (frame == 2 && step == draws / 2) {
        ComPtr<ID3DDeviceContextState> game_state;
        context1->SwapDeviceContextState(made.private_state.Get(), game_state.GetAddressOf());
        bind_all(true);
        ++index;
        write(a.Get(), D3D11_MAP_WRITE_DISCARD, 0, 208, index, static_cast<int>(index));
        draw();
        context->Dispatch(1, 1, 1);
        if (ring && control == Control::SETTLE_OFF) {
          // The swap back without the hook's settling call: d3d11 applies the state inside the ring's swap path, at its table
          // check's settle after the found path's rebinds (they are overwritten: the game state's old segments).
          ComPtr<ID3DDeviceContextState> replaced;
          native_swap(game_state.Get(), replaced.GetAddressOf());
          ++hooks::internal::state.depth;
          bp::internal::RingSwap(game_state.Get(), replaced.Get());
          --hooks::internal::state.depth;
        } else {
          context1->SwapDeviceContextState(game_state.Get(), nullptr);
        }
        // Drawn before a's next upload: the game state's slots on a's segment from the private state (the swap's rebinds).
        ++index;
        draw();
        ++index;
        write(a.Get(), D3D11_MAP_WRITE_DISCARD, 0, 208, index, static_cast<int>(index));
      }
      if (frame == 4 && step == draws / 2) {
        // The second private state drawn at once, its binds from before the install (taken at the swap, rebound).
        ComPtr<ID3DDeviceContextState> game_state;
        context1->SwapDeviceContextState(made.bound_state.Get(), game_state.GetAddressOf());
        ++index;
        draw();
        context->Dispatch(1, 1, 1);
        context1->SwapDeviceContextState(game_state.Get(), nullptr);
        ++index;
      }
      if (frame == 3 && step == 5) {
        context->ClearState();
        bind_all(false);
      }
      draw();
      if (step % 13 == 0) context->Dispatch(1, 1, 1);
    }
    // Each copy in both of d3d11's versions of its body: after a Flush on every other frame.
    if (frame % 2 == 0) context->Flush();
    context->CopyResource(copy_a.Get(), a.Get());
    if (frame % 2 == 1) context->Flush();
    const D3D11_BOX box = {.left = 32, .top = 0, .front = 0, .right = 160, .bottom = 1, .back = 1};
    context->CopySubresourceRegion(copy_b.Get(), 0, 16, 0, 0, b.Get(), 0, &box);
    if (frame % 2 == 0) context->Flush();
    // c's newest window alone (the others undefined since its DISCARD).
    const D3D11_BOX window_box = {.left = window * 256, .top = 0, .front = 0, .right = (window * 256) + 256, .bottom = 1, .back = 1};
    context1->CopySubresourceRegion1(copy_c.Get(), 0, 0, 0, 0, c.Get(), 0, &window_box, 0);
    context->CopyResource(copy_raw.Get(), b.Get());
    for (auto [copy, bytes] :
         {std::pair{copy_a.Get(), 208u}, std::pair{copy_b.Get(), 256u}, std::pair{copy_c.Get(), 1024u}, std::pair{copy_raw.Get(), 256u}}) {
      const auto words = Read(made.device.Get(), context, copy, bytes);
      out.copies.insert(out.copies.end(), words.begin(), words.end());
    }
    bp::Update(context, ring, false);
    if (ring && frame == 0 && !bp::internal::pool.installed) out.failed = "not installed at frame 0's present";
    if (ring && frame == 0 && control == Control::MEASURE_FIRST
        && (hooks::internal::state.hooks.size() != hooks::internal::MEASURE_BODIES
            || hooks::internal::state.ring_hooks.size() != hooks::internal::BODIES - hooks::internal::MEASURE_BODIES)) {
      out.failed = "the ring's hooks beside the measure path";
    }
  }
  if (measuring) {
    // The ring's own buffers (its pieces, the zero segment) never in a record: the caller's arguments alone.
    size_t maps = 0;
    for (const auto& record : measured) {
      if (record.kind == hooks::Kind::MAP || record.kind == hooks::Kind::UNMAP) maps += (record.resource == a.Get() ? 1 : 0);
      if (record.ring_buffer) out.failed = "a map, bind or seed recorded with a piece";
    }
    if (maps < draws || measured.size() < 7 || measured[4].buffers[0] != a.Get()) out.failed = "the measure path's records";
    if (hooks::internal::state.ring_hooks.empty() || hooks::internal::state.observer != nullptr) out.failed = "the ring's hooks after the measure path";
  }
  const auto pixels = [&] {
    const D3D11_TEXTURE2D_DESC staging_desc = {.Width = 128, .Height = 64, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_UINT,
                                               .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Texture2D> staging;
    made.device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf());
    context->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    std::vector<uint32_t> words(128 * 64 * 4);
    if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
      for (UINT row = 0; row < 64; ++row) std::memcpy(&words[row * 128 * 4], static_cast<const uint8_t*>(mapped.pData) + (row * mapped.RowPitch), 128 * 16);
      context->Unmap(staging.Get(), 0);
    }
    return words;
  };
  if (ring) {
    bp::Update(context, false, false);
    if (bp::internal::pool.installed || !hooks::internal::state.hooks.empty() || !hooks::internal::state.ring_hooks.empty()) {
      out.failed = "the ring's hooks left after Update(off)";
    }
  }
  // The private state after the removal (FOREIGN: the unwind at frame 3): its binds and a draw there as natively (none of
  // the ring's segments left in it), a, b and all of c newly written (its window there an old one).
  ++index;
  write(a.Get(), D3D11_MAP_WRITE_DISCARD, 0, 208, index, static_cast<int>(index));
  write(b.Get(), D3D11_MAP_WRITE_DISCARD, 0, 256, 7000, -1);
  write(c.Get(), D3D11_MAP_WRITE_DISCARD, 0, 1024, 7001, -1);
  {
    ComPtr<ID3DDeviceContextState> game_state;
    context1->SwapDeviceContextState(made.private_state.Get(), game_state.GetAddressOf());
    const std::array<ID3D11Buffer*, 6> names = {a.Get(), b.Get(), c.Get(), d.Get(), own.Get(), nullptr};
    constexpr std::array<Get1, 3> GETS1 = {&ID3D11DeviceContext1::VSGetConstantBuffers1, &ID3D11DeviceContext1::PSGetConstantBuffers1,
                                           &ID3D11DeviceContext1::CSGetConstantBuffers1};
    for (const Get1 get : GETS1) {
      std::array<ID3D11Buffer*, 4> held = {};
      std::array<UINT, 4> firsts = {};
      std::array<UINT, 4> counts = {};
      (context1->*get)(0, 4, held.data(), firsts.data(), counts.data());
      for (size_t slot = 0; slot < held.size(); ++slot) {
        out.after.push_back(static_cast<uint32_t>(std::ranges::find(names, held[slot]) - names.begin()));  // 6: neither (a piece)
        out.after.push_back(firsts[slot]);
        out.after.push_back(counts[slot]);
        if (held[slot] != nullptr) held[slot]->Release();
      }
    }
    script.push_back(index);
    draw();
    context1->SwapDeviceContextState(game_state.Get(), nullptr);
  }
  out.pixels = pixels();
  out.computed = Read(made.device.Get(), context, output.Get(), 1024 * 16);
  for (const uint32_t drawn : script) {
    const size_t pixel = ((drawn % 128) + (((drawn / 128) % 64) * 128)) * 4;
    out.script.insert(out.script.end(), out.pixels.begin() + static_cast<std::ptrdiff_t>(pixel),
                      out.pixels.begin() + static_cast<std::ptrdiff_t>(pixel + 4));
  }
  bp::internal::ring_paths = ring_paths;
  bp::internal::piece_cap = bp::internal::MAX_PIECES;
  bp::internal::piece_done = nullptr;
  bp::internal::retire_ahead = 0;
  bp::ForgetDevice(made.device.Get());
  return out;
}

static size_t Differences(const std::vector<uint32_t>& left, const std::vector<uint32_t>& right) {
  size_t count = (left.size() > right.size() ? left.size() - right.size() : right.size() - left.size());
  for (size_t index = 0; index < std::min(left.size(), right.size()); ++index) count += (left[index] != right[index] ? 1 : 0);
  return count;
}

// 8's pieces' events as the GPU would answer them a few frames behind: each piece seen ended at a present (the GPU idle
// there: a seam's pass is the GPU's) with that present's number; a pass forgets it.
using Ended = std::pair<ID3D11Query*, uint32_t>;
static std::vector<Ended> ended_at;
static uint32_t presents = 0;
static size_t flushed_passes = 0;

static void NoteEnded() {
  for (const auto& piece : bp::internal::pool.pieces) {
    const auto found = std::ranges::find(ended_at, piece->done.Get(), &Ended::first);
    if (piece->phase != bp::internal::Phase::ENDED) {
      if (found != ended_at.end()) ended_at.erase(found);
    } else if (found == ended_at.end()) {
      ended_at.emplace_back(piece->done.Get(), presents);
    }
  }
  ++presents;
}

// (a): an event passes 3 presents after the one it was first seen ended at.
static HRESULT PassedAfterThreePresents(ID3D11DeviceContext*, ID3D11Query* query, bool) {
  const auto found = std::ranges::find(ended_at, query, &Ended::first);
  if (found == ended_at.end() || presents - found->second < 3) return S_FALSE;
  ended_at.erase(found);
  return S_OK;
}

// (b): an event passes only when asked with a flush.
static HRESULT PassedWithFlush(ID3D11DeviceContext*, ID3D11Query*, bool flush) {
  if (!flush) return S_FALSE;
  ++flushed_passes;
  return S_OK;
}

// 9 (c): an event passes once `cap_polls_left` polls without a flush were answered not yet (-1: never); the flushes
// never pass. Every call counted.
static int cap_polls_left = 0;
static size_t cap_polls = 0;
static size_t cap_flushes = 0;
static HRESULT PassedAfterPolls(ID3D11DeviceContext*, ID3D11Query*, bool flush) {
  if (flush) {
    ++cap_flushes;
    return S_FALSE;
  }
  ++cap_polls;
  if (cap_polls_left == 0) return S_OK;
  if (cap_polls_left > 0) --cap_polls_left;
  return S_FALSE;
}

constexpr uint32_t STREAM_UPLOADS = 1536;  // a frame's 64 B writes: 1.5 pieces
constexpr uint32_t STREAM_BUFFERS = 16;

struct Stream {
  std::vector<uint32_t> computed;  // each frame's output, read back at its present
  size_t pieces = 0;               // the ring's at the end
  std::string failed;
};

// 8's frames: STREAM_UPLOADS DISCARDs a frame of 64 B game buffers in turn (every 32nd read by a dispatch at b0, which
// writes its own output slot), a straggler written every 30 frames (read by every dispatch at b1 and b2); each frame's
// output read back (the GPU idle at the present), the ring's present after it (the ring in at frame 0's).
static Stream RunStream(bool ring, uint32_t frames) {
  Stream out;
  Device made = MakeDevice();
  auto* context = made.context.Get();
  loss::devices = {{.device = made.device.Get(), .single_threaded = true}};
  ended_at.clear();
  presents = 0;
  ComPtr<ID3D11ComputeShader> cs;
  made.device->CreateComputeShader(cs_blob->GetBufferPointer(), cs_blob->GetBufferSize(), nullptr, cs.GetAddressOf());
  const D3D11_BUFFER_DESC output_desc = {.ByteWidth = 1024 * 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                         .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = 16};
  ComPtr<ID3D11Buffer> output;
  ComPtr<ID3D11UnorderedAccessView> output_view;
  made.device->CreateBuffer(&output_desc, nullptr, output.GetAddressOf());
  const D3D11_UNORDERED_ACCESS_VIEW_DESC output_view_desc = {.Format = DXGI_FORMAT_UNKNOWN, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
                                                             .Buffer = {.FirstElement = 0, .NumElements = 1024}};
  made.device->CreateUnorderedAccessView(output.Get(), &output_view_desc, output_view.GetAddressOf());
  const UINT zeros[4] = {};
  context->ClearUnorderedAccessViewUint(output_view.Get(), zeros);
  std::array<ComPtr<ID3D11Buffer>, STREAM_BUFFERS> per_draw;
  for (uint32_t index = 0; index < STREAM_BUFFERS; ++index) per_draw[index] = GameBuffer(made.device.Get(), 64, 100 + index);
  const auto straggler = GameBuffer(made.device.Get(), 64, 200);
  const auto write = [context](ID3D11Buffer* buffer, uint32_t seed, int index_word) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    auto* words = static_cast<uint32_t*>(mapped.pData);
    for (UINT index = 0; index < 16; ++index) words[index] = Word(seed, index);
    if (index_word >= 0) words[3] = static_cast<uint32_t>(index_word) & 1023;
    context->Unmap(buffer, 0);
  };
  context->CSSetShader(cs.Get(), nullptr, 0);
  context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
  const std::array<ID3D11Buffer*, 2> straggler_slots = {straggler.Get(), straggler.Get()};
  context->CSSetConstantBuffers(1, 2, straggler_slots.data());
  uint32_t dispatches = 0;
  for (uint32_t frame = 0; frame < frames; ++frame) {
    if (frame % 30 == 1) write(straggler.Get(), 50000 + frame, -1);
    for (uint32_t upload = 0; upload < STREAM_UPLOADS; ++upload) {
      ID3D11Buffer* buffer = per_draw[upload % STREAM_BUFFERS].Get();
      const bool dispatched = upload % 32 == 31;
      write(buffer, (frame * STREAM_UPLOADS) + upload, (dispatched ? static_cast<int>(dispatches & 1023) : -1));
      if (dispatched) {
        context->CSSetConstantBuffers(0, 1, &buffer);
        context->Dispatch(1, 1, 1);
        ++dispatches;
      }
    }
    const auto words = Read(made.device.Get(), context, output.Get(), 1024 * 16);
    out.computed.insert(out.computed.end(), words.begin(), words.end());
    bp::Update(context, ring, false);
    if (ring && frame == 0 && !bp::internal::pool.installed) out.failed = "not installed at frame 0's present";
    if (ring) NoteEnded();
  }
  out.pieces = bp::internal::pool.pieces.size();
  if (ring) bp::Update(context, false, false);
  bp::ForgetDevice(made.device.Get());
  return out;
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  CHECK(Compile());
  // The removals below take the ring out at the row's Off (part 1 latches it on purpose).
  bp::internal::latch_off = false;

  // 1. The bodies; refusals.
  {
    Device made = MakeDevice();
    const auto d3d11 = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"d3d11.dll"));
    const auto bodies = ReadBodies(made.device.Get(), made.context.Get());
    for (size_t body = 0; body < bodies.size(); ++body) {
      HMODULE module = nullptr;
      CHECK(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(bodies[body]), &module));
      CHECK(reinterpret_cast<uintptr_t>(module) == d3d11);
      const auto* code = static_cast<const uint8_t*>(bodies[body]);
      if (body >= hooks::internal::MEASURE_BODIES) {
        std::printf("  slot %3zu d3d11.dll+0x%06zx  %02x %02x %02x %02x\n", hooks::internal::BODY_SLOTS[body],
                    static_cast<size_t>(reinterpret_cast<uintptr_t>(bodies[body]) - d3d11), code[0], code[1], code[2], code[3]);
      }
      CHECK(code[0] != 0xE9 && code[0] != 0xEB && !(code[0] == 0xFF && code[1] == 0x25) && !(code[0] == 0x68 && code[5] == 0xC3));
    }
    auto sorted = bodies;
    std::ranges::sort(sorted);
    CHECK(std::ranges::adjacent_find(sorted) == sorted.end());
    // (f) A second device known: no install, the waiting line once, no refusal; the device out: in at the next present.
    loss::devices = {{.device = made.device.Get(), .single_threaded = true}, {.device = made.device.Get(), .single_threaded = true}};
    bp::Update(made.context.Get(), true, false);
    bp::Update(made.context.Get(), true, false);
    CHECK(!bp::internal::pool.installed && hooks::internal::state.hooks.empty() && bp::internal::pool.copies == false && bp::internal::pool.refused == nullptr);
    CHECK(Logged("Fewer Driver Waits: waiting (2 D3D11 devices known, not only the game's).") == 1 && Logged("2 D3D11 devices") == 1
          && Logged("not installed") == 0);
    loss::devices = {{.device = made.device.Get(), .single_threaded = true}};
    bp::Update(made.context.Get(), true, false);
    CHECK(bp::internal::pool.installed && Logged("Fewer Driver Waits: on (") == 1);
    bp::Update(made.context.Get(), false, false);
    CHECK(!bp::internal::pool.installed);
    loss::devices = {{.device = made.device.Get(), .single_threaded = false}};
    bp::Update(made.context.Get(), true, false);
    CHECK(!bp::internal::pool.installed && Logged("not single-threaded") == 1 && hooks::internal::state.hooks.empty());
    bp::Update(made.context.Get(), false, false);
    // The row's Off latched: the ring stays in (logged once), a DISCARD still goes into a piece, Get and a copy give the
    // game's buffer and its newest bytes; back On clears it (the next Off logs again). CONTROL: unlatched, Off takes it
    // out (pieces and copies gone), the bytes written back.
    loss::devices = {{.device = made.device.Get(), .single_threaded = true}};
    const auto latched = GameBuffer(made.device.Get(), 256, 21);
    bp::Update(made.context.Get(), true, false);
    CHECK(bp::internal::pool.installed);
    bp::internal::latch_off = true;
    bp::Update(made.context.Get(), false, false);
    bp::Update(made.context.Get(), false, false);
    CHECK(bp::internal::pool.installed && Logged("stays on until the game restarts") == 1);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    CHECK(SUCCEEDED(made.context->Map(latched.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
    std::memset(mapped.pData, 0x5A, 256);
    made.context->Unmap(latched.Get(), 0);
    made.context->PSSetConstantBuffers(0, 1, latched.GetAddressOf());
    ComPtr<ID3D11Buffer> bound;
    made.context->PSGetConstantBuffers(0, 1, bound.GetAddressOf());
    const std::vector<uint32_t> written(64, 0x5A5A5A5Au);
    CHECK(bound == latched && bp::internal::Find(latched.Get())->piece != nullptr
          && Read(made.device.Get(), made.context.Get(), latched.Get(), 256) == written);
    bp::Update(made.context.Get(), true, false);
    bp::Update(made.context.Get(), false, false);
    CHECK(bp::internal::pool.installed && Logged("stays on until the game restarts") == 2);
    bp::internal::latch_off = false;
    bp::Update(made.context.Get(), false, false);
    CHECK(!bp::internal::pool.installed && bp::internal::pool.pieces.empty() && !bp::internal::pool.copies);
    CHECK(Read(made.device.Get(), made.context.Get(), latched.Get(), 256) == written);
    CHECK(Logged("Restart the game") == 0);
    // A stop while latched (another thread's call): the ring still unwinds and goes out at the Off's next present, the
    // cause and the restart hint logged.
    bp::internal::latch_off = true;
    bp::Update(made.context.Get(), true, false);
    bp::Update(made.context.Get(), false, false);
    CHECK(bp::internal::pool.installed && Logged("stays on until the game restarts") == 3);
    ID3D11Buffer* const none = nullptr;
    std::thread([&made, none] { made.context->PSSetConstantBuffers(0, 1, &none); }).join();
    bound.Reset();
    made.context->PSGetConstantBuffers(0, 1, bound.GetAddressOf());
    CHECK(bound == nullptr && bp::internal::pool.stopped);
    bp::Update(made.context.Get(), false, false);
    CHECK(!bp::internal::pool.installed && Logged("called the game's context") == 1 && Logged("off. Restart the game for full speed.") == 1);
    CHECK(Read(made.device.Get(), made.context.Get(), latched.Get(), 256) == written);
    bp::internal::latch_off = false;
    bp::ForgetDevice(made.device.Get());
    std::printf("1. 35 distinct bodies in d3d11.dll (the ring's 18 printed), no jump prologue; 2 devices: waits (logged once, no refusal), in when"
                " alone; refused once (logged, nothing hooked): a multithreaded one; the row's Off latched: still in (logged once a latch), its writes in a piece, Get / copy the game's"
                " buffer and bytes; CONTROL unlatched: out at once, the bytes written back; a stop while latched: out, cause and restart"
                " hint logged\n");
  }

  // 2. The rules.
  {
    Device made = MakeDevice();
    D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
    CHECK(SUCCEEDED(made.device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))));
    CHECK(options.ConstantBufferOffsetting && options.MapNoOverwriteOnDynamicConstantBuffer);
    const auto dynamic = Buffer(made.device.Get(), 256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 11);
    const auto source = Buffer(made.device.Get(), 256, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 12);
    const auto before = Read(made.device.Get(), made.context.Get(), dynamic.Get(), 256);
    std::array<uint32_t, 64> update = {};
    update.fill(0x55);
    made.context->UpdateSubresource(dynamic.Get(), 0, nullptr, update.data(), 0, 0);
    CHECK(Read(made.device.Get(), made.context.Get(), dynamic.Get(), 256) == before);
    made.context->CopyResource(dynamic.Get(), source.Get());
    CHECK(Read(made.device.Get(), made.context.Get(), dynamic.Get(), 256) == Read(made.device.Get(), made.context.Get(), source.Get(), 256));
    std::printf("2. rules: offsets and NO_OVERWRITE on dynamic constant buffers; UpdateSubresource into one dropped, a copy into one lands\n");
  }

  // 3. Pixels, ring off vs on.
  {
    constexpr uint32_t FRAMES = 6;
    constexpr uint32_t DRAWS = 500;
    const Output off = Run(false, Control::NONE, FRAMES, DRAWS);
    CHECK(off.failed.empty());
    size_t lit = static_cast<size_t>(std::ranges::count_if(off.pixels, [](uint32_t word) { return word != 0; }));
    CHECK(lit > FRAMES * DRAWS * 3 && std::ranges::count_if(off.computed, [](uint32_t word) { return word != 0; }) > 100);
    for (size_t control = 0; control < CONTROL_NAMES.size(); ++control) {
      log_lines.clear();
      const Output on = Run(true, static_cast<Control>(control), FRAMES, DRAWS);
      if (!on.failed.empty()) {
        std::printf("FAIL %s: %s\n", CONTROL_NAMES[control], on.failed.c_str());
        return 1;
      }
      const size_t pixels = Differences(off.pixels, on.pixels);
      const size_t computed = Differences(off.computed, on.computed);
      const size_t copies = Differences(off.copies, on.copies);
      const size_t after = Differences(off.after, on.after);
      const size_t script = Differences(off.script, on.script);
      const bool equal = pixels == 0 && computed == 0 && copies == 0 && after == 0;
      const bool must_equal = static_cast<Control>(control) <= Control::WRAP;
      std::printf("  %-22s pixels %zu (script %zu), compute %zu, copies %zu, private state after %zu words differ\n", CONTROL_NAMES[control], pixels,
                  script, computed, copies, after);
      CHECK(equal == must_equal);
      CHECK(Logged("unknown to it") == 0 && Logged("swap it never saw") == 0);
      switch (static_cast<Control>(control)) {
        case Control::NONE: CHECK(Logged("Fewer Driver Waits: on") == 1 && Logged("pieces still in use") == 0 && Logged("waited") == 0); break;
        case Control::FULL:
        case Control::WRAP:
          CHECK(Logged(std::format("all {} pieces in use by the GPU: waited ", bp::internal::FIRST_PIECES).c_str()) == 1
                && Logged(std::format("all {} pieces still in use", bp::internal::FIRST_PIECES).c_str()) == 1);
          break;
        case Control::FOREIGN: CHECK(Logged("called the game's context") == 1 && Logged("off. Restart the game for full speed.") == 1); break;
        case Control::EARLY_REUSE: CHECK(script != 0); break;
        case Control::COPY_OFF: CHECK(copies != 0); break;
        case Control::SETTLE_OFF: CHECK(after != 0); break;
        default: break;
      }
    }
    std::printf("3. %u frames x %u draws (lit words %zu): equal with the ring, a full ring, the full ring then its wrap, another thread's call and"
                " the measure path beside it (after the ring, before it), the private state after the removal too, no table check logged; every"
                " CONTROL differs (early reuse: in the script's draws; the swaps' rebinds and seed among them)\n",
                FRAMES, DRAWS, lit);
  }

  // 4. Get / Get1 / Map parity and references against a second device, ring off.
  {
    Device ring = MakeDevice();
    Device native = MakeDevice();
    loss::devices = {{.device = ring.device.Get(), .single_threaded = true}};
    constexpr std::array<UINT, 4> SIZES = {256, 208, 1024, 512};
    std::array<ComPtr<ID3D11Buffer>, 5> ring_buffers;
    std::array<ComPtr<ID3D11Buffer>, 5> native_buffers;
    for (size_t index = 0; index < SIZES.size(); ++index) {
      ring_buffers[index] = GameBuffer(ring.device.Get(), SIZES[index], static_cast<uint32_t>(20 + index));
      native_buffers[index] = Buffer(native.device.Get(), SIZES[index], D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE,
                                     static_cast<uint32_t>(20 + index));
    }
    // Our own (made below ReShade): never tracked.
    ring_buffers[4] = Buffer(ring.device.Get(), 256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 30);
    native_buffers[4] = Buffer(native.device.Get(), 256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 30);
    CHECK(bp::internal::Find(ring_buffers[4].Get()) == nullptr && bp::internal::Find(ring_buffers[0].Get()) != nullptr);
    // Bound before the install.
    ring.context->PSSetConstantBuffers(3, 1, ring_buffers[2].GetAddressOf());
    native.context->PSSetConstantBuffers(3, 1, native_buffers[2].GetAddressOf());
    const ULONG device_references = References(ring.device.Get());
    bp::Update(ring.context.Get(), true, false);
    CHECK(bp::internal::pool.installed);
    const auto index_of = [](const auto& buffers, ID3D11Buffer* buffer) {
      if (buffer == nullptr) return -1;
      for (size_t index = 0; index < buffers.size(); ++index) {
        if (buffers[index].Get() == buffer) return static_cast<int>(index);
      }
      return 100;  // neither: a piece or the zero segment
    };
    constexpr std::array<Get1, hooks::STAGES> GETS1 = {&ID3D11DeviceContext1::VSGetConstantBuffers1, &ID3D11DeviceContext1::HSGetConstantBuffers1,
                                                        &ID3D11DeviceContext1::DSGetConstantBuffers1, &ID3D11DeviceContext1::GSGetConstantBuffers1,
                                                        &ID3D11DeviceContext1::PSGetConstantBuffers1, &ID3D11DeviceContext1::CSGetConstantBuffers1};
    using Get = void (STDMETHODCALLTYPE ID3D11DeviceContext::*)(UINT, UINT, ID3D11Buffer**);
    constexpr std::array<Get, hooks::STAGES> GETS = {&ID3D11DeviceContext::VSGetConstantBuffers, &ID3D11DeviceContext::HSGetConstantBuffers,
                                                      &ID3D11DeviceContext::DSGetConstantBuffers, &ID3D11DeviceContext::GSGetConstantBuffers,
                                                      &ID3D11DeviceContext::PSGetConstantBuffers, &ID3D11DeviceContext::CSGetConstantBuffers};
    using Set = void (STDMETHODCALLTYPE ID3D11DeviceContext::*)(UINT, UINT, ID3D11Buffer* const*);
    constexpr std::array<Set, hooks::STAGES> SETS = {&ID3D11DeviceContext::VSSetConstantBuffers, &ID3D11DeviceContext::HSSetConstantBuffers,
                                                      &ID3D11DeviceContext::DSSetConstantBuffers, &ID3D11DeviceContext::GSSetConstantBuffers,
                                                      &ID3D11DeviceContext::PSSetConstantBuffers, &ID3D11DeviceContext::CSSetConstantBuffers};
    using Set1 = void (STDMETHODCALLTYPE ID3D11DeviceContext1::*)(UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
    constexpr std::array<Set1, hooks::STAGES> SETS1 = {&ID3D11DeviceContext1::VSSetConstantBuffers1, &ID3D11DeviceContext1::HSSetConstantBuffers1,
                                                        &ID3D11DeviceContext1::DSSetConstantBuffers1, &ID3D11DeviceContext1::GSSetConstantBuffers1,
                                                        &ID3D11DeviceContext1::PSSetConstantBuffers1, &ID3D11DeviceContext1::CSSetConstantBuffers1};
    // The slots of every state's table holding `buffer` (each one reference).
    const auto Held = [](const ID3D11Buffer* buffer) {
      size_t held = 0;
      for (const auto& table : bp::internal::pool.states) {
        for (const auto& stage : table->slots) {
          held += static_cast<size_t>(
              std::ranges::count_if(stage, [buffer](const bp::internal::Slot& slot) { return slot.buffer != nullptr && slot.buffer->buffer == buffer; }));
        }
      }
      return held;
    };
    const auto compare = [&](size_t step) {
      for (size_t stage = 0; stage < hooks::STAGES; ++stage) {
        std::array<ID3D11Buffer*, 14> got_ring = {};
        std::array<ID3D11Buffer*, 14> got_native = {};
        std::array<UINT, 14> first_ring = {};
        std::array<UINT, 14> first_native = {};
        std::array<UINT, 14> count_ring = {};
        std::array<UINT, 14> count_native = {};
        (ring.context1.Get()->*GETS1[stage])(0, 14, got_ring.data(), first_ring.data(), count_ring.data());
        (native.context1.Get()->*GETS1[stage])(0, 14, got_native.data(), first_native.data(), count_native.data());
        bool same = first_ring == first_native && count_ring == count_native;
        for (size_t slot = 0; slot < 14; ++slot) same = same && index_of(ring_buffers, got_ring[slot]) == index_of(native_buffers, got_native[slot]);
        std::array<ID3D11Buffer*, 14> plain_ring = {};
        std::array<ID3D11Buffer*, 14> plain_native = {};
        (ring.context.Get()->*GETS[stage])(0, 14, plain_ring.data());
        (native.context.Get()->*GETS[stage])(0, 14, plain_native.data());
        for (size_t slot = 0; slot < 14; ++slot) same = same && index_of(ring_buffers, plain_ring[slot]) == index_of(native_buffers, plain_native[slot]);
        for (auto* buffer : {&got_ring, &got_native, &plain_ring, &plain_native}) {
          for (ID3D11Buffer* held : *buffer) {
            if (held != nullptr) held->Release();
          }
        }
        if (!same) {
          std::printf("FAIL step %zu stage %zu: Get / Get1 differ\n", step, stage);
          return false;
        }
      }
      for (size_t index = 0; index < SIZES.size(); ++index) {
        if (References(ring_buffers[index].Get()) != References(native_buffers[index].Get()) + Held(ring_buffers[index].Get())) {
          std::printf("FAIL step %zu: buffer %zu: %lu references, natively %lu, %zu slots of the ring\n", step, index,
                      References(ring_buffers[index].Get()), References(native_buffers[index].Get()), Held(ring_buffers[index].Get()));
          return false;
        }
      }
      return true;
    };
    // Map parity: the invalid ones fail alike, a DISCARD's pitches.
    struct MapCase {
      UINT subresource;
      D3D11_MAP type;
      UINT flags;
    };
    constexpr std::array<MapCase, 4> MAP_CASES = {MapCase{1, D3D11_MAP_WRITE_DISCARD, 0}, MapCase{0, D3D11_MAP_READ, 0},
                                                  MapCase{0, D3D11_MAP_WRITE_DISCARD, D3D11_MAP_FLAG_DO_NOT_WAIT},
                                                  MapCase{0, D3D11_MAP_WRITE_DISCARD, 0}};
    for (const MapCase& map_case : MAP_CASES) {
      std::array<HRESULT, 2> results = {};
      std::array<D3D11_MAPPED_SUBRESOURCE, 2> mapped = {};
      for (size_t side = 0; side < 2; ++side) {
        Device& made = (side == 0 ? ring : native);
        ID3D11Buffer* buffer = (side == 0 ? ring_buffers[1] : native_buffers[1]).Get();
        results[side] = made.context->Map(buffer, map_case.subresource, map_case.type, map_case.flags, &mapped[side]);
        if (SUCCEEDED(results[side])) made.context->Unmap(buffer, map_case.subresource);
      }
      std::printf("  Map(subresource %u, type %d, flags 0x%x): 0x%08lx\n", map_case.subresource, static_cast<int>(map_case.type), map_case.flags,
                  static_cast<unsigned long>(results[1]));
      CHECK(results[0] == results[1]);
      CHECK(FAILED(results[0]) == (map_case.flags != 0 || map_case.subresource != 0 || map_case.type == D3D11_MAP_READ));
      if (SUCCEEDED(results[0])) CHECK(mapped[0].RowPitch == mapped[1].RowPitch && mapped[0].DepthPitch == mapped[1].DepthPitch && mapped[0].RowPitch == 208);
    }
    CHECK(compare(0));
    // A Get past the slots (invalid) of empty PS slots 12-13: bounded to them, the entries past them as the caller left
    // them.
    {
      auto* const untouched = reinterpret_cast<ID3D11Buffer*>(uintptr_t{16});
      std::array<ID3D11Buffer*, 4> got = {untouched, untouched, untouched, untouched};
      std::array<UINT, 4> firsts = {7, 7, 7, 7};
      ring.context->PSGetConstantBuffers(12, 4, got.data());
      CHECK(got[0] == nullptr && got[1] == nullptr && got[2] == untouched && got[3] == untouched);
      got = {untouched, untouched, untouched, untouched};
      ring.context1->PSGetConstantBuffers1(13, 3, got.data(), firsts.data(), nullptr);
      CHECK(got[0] == nullptr && got[1] == untouched && firsts[0] == 0 && firsts[1] == 7);
    }
    std::mt19937 random(5);
    const auto pick = [&] { return static_cast<int>(random() % 6) - 1; };  // -1: null, 0-3: game buffers, 4: our own
    size_t windows = 0;
    size_t invalid = 0;
    size_t swaps = 0;
    for (size_t step = 1; step <= 3000; ++step) {
      const uint32_t kind = random() % 20;
      const size_t stage = random() % hooks::STAGES;
      const UINT start = random() % 14;
      // Slots past 14 corrupt d3d11's state (a private state's: a crash at the next swap, ring or not): never.
      const UINT count = 1 + (random() % std::min(3u, 14 - start));
      std::array<int, 3> chosen = {pick(), pick(), pick()};
      std::array<ID3D11Buffer*, 3> on_ring = {};
      std::array<ID3D11Buffer*, 3> on_native = {};
      for (size_t index = 0; index < 3; ++index) {
        on_ring[index] = (chosen[index] < 0 ? nullptr : ring_buffers[chosen[index]].Get());
        on_native[index] = (chosen[index] < 0 ? nullptr : native_buffers[chosen[index]].Get());
      }
      if (kind < 6) {
        (ring.context.Get()->*SETS[stage])(start, count, on_ring.data());
        (native.context.Get()->*SETS[stage])(start, count, on_native.data());
      } else if (kind < 12) {
        std::array<UINT, 3> firsts = {};
        std::array<UINT, 3> counts = {};
        for (size_t index = 0; index < 3; ++index) {
          firsts[index] = 16 * (random() % 10);
          counts[index] = 16 * (random() % 6);
        }
        const bool bad = random() % 10 == 0;
        if (bad) {
          firsts[random() % count] = 8;
          ++invalid;
        }
        const bool whole = random() % 10 == 0;
        (ring.context1.Get()->*SETS1[stage])(start, count, on_ring.data(), (whole ? nullptr : firsts.data()), (whole ? nullptr : counts.data()));
        (native.context1.Get()->*SETS1[stage])(start, count, on_native.data(), (whole ? nullptr : firsts.data()), (whole ? nullptr : counts.data()));
        ++windows;
      } else if (kind < 18) {
        const size_t index = random() % SIZES.size();
        for (auto* buffer : {ring_buffers[index].Get(), native_buffers[index].Get()}) {
          auto* context = (buffer == ring_buffers[index].Get() ? ring.context.Get() : native.context.Get());
          D3D11_MAPPED_SUBRESOURCE mapped = {};
          CHECK(SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
          std::memset(mapped.pData, static_cast<int>(step), SIZES[index]);
          context->Unmap(buffer, 0);
        }
      } else if (kind == 18 && random() % 8 == 0) {
        ring.context->ClearState();
        native.context->ClearState();
      } else {
        for (Device* made : {&ring, &native}) {
          auto& buffers = (made == &ring ? on_ring : on_native);
          ComPtr<ID3DDeviceContextState> previous;
          made->context1->SwapDeviceContextState(made->private_state.Get(), previous.GetAddressOf());
          (made->context.Get()->*SETS[stage])(start, count, buffers.data());
          if (made == &ring) {
            const size_t index = random() % SIZES.size();
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            CHECK(SUCCEEDED(ring.context->Map(ring_buffers[index].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
            ring.context->Unmap(ring_buffers[index].Get(), 0);
          }
          made->context1->SwapDeviceContextState(previous.Get(), nullptr);
        }
        ++swaps;
      }
      if (!compare(step)) return 1;
    }
    std::printf("  %zu window binds (%zu invalid), %zu swaps through the private state\n", windows, invalid, swaps);
    // A buffer the game lets go of while bound lives on until unbound, as natively (d3d11's binds hold it; the ring's
    // slot references, its segment bound instead). CONTROL: without the ring's references it goes at once.
    const auto let_go = [&](bool control, bool* ring_gone, bool* native_gone) {
      auto ring_buffer = Buffer(ring.device.Get(), 256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 50);
      bp::Track(ring.device.Get(), ring_buffer.Get(), nullptr);
      auto native_buffer = Buffer(native.device.Get(), 256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 50);
      Watch(ring_buffer.Get(), ring_gone);
      Watch(native_buffer.Get(), native_gone);
      ring.context->PSSetConstantBuffers(7, 1, ring_buffer.GetAddressOf());
      native.context->PSSetConstantBuffers(7, 1, native_buffer.GetAddressOf());
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      ring.context->Map(ring_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
      ring.context->Unmap(ring_buffer.Get(), 0);
      const size_t held = Held(ring_buffer.Get());
      if (control) {
        for (size_t index = 0; index < held; ++index) ring_buffer->Release();
      }
      ID3D11Buffer* const raw = ring_buffer.Detach();
      native_buffer.Reset();
      raw->Release();
      // d3d11 destroys a buffer let go of at the next Flush.
      ring.context->Flush();
      native.context->Flush();
      return std::pair{held, raw};
    };
    bool ring_gone = false;
    bool native_gone = false;
    const auto [held, raw] = let_go(false, &ring_gone, &native_gone);
    CHECK(held == 1 && !ring_gone && !native_gone);
    ID3D11Buffer* got = nullptr;
    ring.context->PSGetConstantBuffers(7, 1, &got);
    CHECK(got == raw);
    got->Release();
    ID3D11Buffer* const no_buffer = nullptr;
    ring.context->PSSetConstantBuffers(7, 1, &no_buffer);
    native.context->PSSetConstantBuffers(7, 1, &no_buffer);
    ring.context->Flush();
    native.context->Flush();
    CHECK(ring_gone && native_gone);
    bool control_gone = false;
    bool control_native_gone = false;
    const auto [control_held, control_raw] = let_go(true, &control_gone, &control_native_gone);
    CHECK(control_held == 1 && control_gone && !control_native_gone);
    // The control's slot out by hand (its buffer gone), then the native one unbound.
    for (const auto& table : bp::internal::pool.states) {
      for (size_t stage = 0; stage < hooks::STAGES; ++stage) {
        for (UINT slot = 0; slot < bp::internal::SLOTS; ++slot) {
          if (table->slots[stage][slot].buffer != nullptr && table->slots[stage][slot].buffer->buffer == control_raw) {
            table->slots[stage][slot] = {};
            table->used[stage] &= static_cast<uint16_t>(~(1u << slot));
          }
        }
      }
    }
    ring.context->PSSetConstantBuffers(7, 1, &no_buffer);
    native.context->PSSetConstantBuffers(7, 1, &no_buffer);
    native.context->Flush();
    CHECK(control_native_gone && compare(3001));
    // The ring out (Uninstall on the render thread, slots bound): every reference as natively, the device's too, and the
    // binds the same.
    bp::Uninstall();
    CHECK(!bp::internal::pool.installed && hooks::internal::state.hooks.empty() && hooks::internal::state.ring_hooks.empty() && compare(3002));
    for (size_t index = 0; index < SIZES.size(); ++index) CHECK(References(ring_buffers[index].Get()) == References(native_buffers[index].Get()));
    CHECK(References(ring.device.Get()) == device_references);
    std::printf("4. Get / Get1 parity over 3000 random steps, every game buffer's references its own plus one per slot of the ring at every step,"
                " its own alone after the removal (the device: %lu references as before); Map parity; a Get past the slots bounded to them; our"
                " own buffer untracked; a buffer let go of"
                " while bound lives until unbound, as natively; CONTROL: without the ring's reference it goes at once; out by Uninstall\n",
                device_references);
    bp::ForgetDevice(ring.device.Get());
  }

  // 5. The device's end with the ring on.
  {
    Device made = MakeDevice();
    loss::devices = {{.device = made.device.Get(), .single_threaded = true}};
    const auto bodies = ReadBodies(made.device.Get(), made.context.Get());
    std::array<std::array<uint8_t, 16>, hooks::internal::BODIES> prologues = {};
    for (size_t body = 0; body < prologues.size(); ++body) std::memcpy(prologues[body].data(), bodies[body], 16);
    auto buffer = GameBuffer(made.device.Get(), 256, 40);
    made.context->PSSetConstantBuffers(0, 1, buffer.GetAddressOf());
    bp::Update(made.context.Get(), true, false);
    CHECK(bp::internal::pool.installed && hooks::internal::state.hooks.size() == hooks::internal::MEASURE_BODIES
          && hooks::internal::state.ring_hooks.size() == hooks::internal::BODIES - hooks::internal::MEASURE_BODIES);
    CHECK(hooks::internal::state.bodies == bodies);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    CHECK(SUCCEEDED(made.context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
    made.context->Unmap(buffer.Get(), 0);
    const ULONG with_ring = References(buffer.Get());
    bp::ForgetDevice(made.device.Get());
    CHECK(!bp::internal::pool.installed && hooks::internal::state.hooks.empty() && hooks::internal::state.ring_hooks.empty());
    CHECK(hooks::internal::state.lifetime.module == nullptr && hooks::internal::state.ring_lifetime.module == nullptr);
    CHECK(References(buffer.Get()) == with_ring - 1 && References(buffer.Get()) == 1);
    for (size_t body = 0; body < prologues.size(); ++body) CHECK(std::memcmp(bodies[body], prologues[body].data(), 16) == 0);
    const size_t records = bp::internal::pool.records.size();
    bp::Update(made.context.Get(), false, false);
    CHECK(records != 0 && bp::internal::pool.records.empty());
    std::printf("5. the device's end with the ring on: hooks out, its reference on the bound buffer let go (%lu -> 1), %zu records freed at the next"
                " present; every body's first 16 bytes back, both modules let go\n",
                with_ring, records);
  }
  // 6. The unwind's edges.
  {
    log_lines.clear();
    Device made = MakeDevice();
    auto* context = made.context.Get();
    loss::devices = {{.device = made.device.Get(), .single_threaded = true}};
    const auto fill = [context](ID3D11Buffer* buffer, int value, UINT bytes) {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
      std::memset(mapped.pData, value, bytes);
      context->Unmap(buffer, 0);
      return true;
    };
    // `kept`: its record destroyed as at its end, the buffer kept so a write-back into it would show; `destroyed`: gone;
    // `bound`: at PS b0, unbound natively by another thread.
    auto kept = GameBuffer(made.device.Get(), 256, 60);
    auto destroyed = Buffer(made.device.Get(), 256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 61);
    bool destroyed_gone = false;
    bp::Track(made.device.Get(), destroyed.Get(), nullptr);
    Watch(destroyed.Get(), &destroyed_gone);
    auto bound = GameBuffer(made.device.Get(), 256, 62);
    context->PSSetConstantBuffers(0, 1, bound.GetAddressOf());
    const auto kept_bytes = Read(made.device.Get(), context, kept.Get(), 256);
    bp::Update(context, true, false);
    CHECK(bp::internal::pool.installed);
    for (ID3D11Buffer* buffer : {kept.Get(), destroyed.Get(), bound.Get()}) CHECK(fill(buffer, 0x77, 256));
    CHECK(bp::internal::Find(kept.Get())->piece != nullptr && bp::internal::Find(destroyed.Get())->piece != nullptr);
    bp::Forget(kept.Get());
    destroyed.Reset();
    context->Flush();
    CHECK(destroyed_gone);
    std::thread([context] {
      ID3D11Buffer* const none = nullptr;
      context->PSSetConstantBuffers(0, 1, &none);
    }).join();
    // The render thread's next call: the unwind, then the Get as d3d11 has the slot now.
    ID3D11Buffer* probe = nullptr;
    context->PSGetConstantBuffers(0, 1, &probe);
    CHECK(probe == nullptr && bp::internal::pool.stopped && made.device->GetDeviceRemovedReason() == S_OK);
    CHECK(Read(made.device.Get(), context, kept.Get(), 256) == kept_bytes && References(bound.Get()) == 1);
    bp::Update(context, true, false);
    CHECK(!bp::internal::pool.installed && Logged("off. Restart the game for full speed.") == 1);
    bp::Update(context, false, false);
    // A lost device: an event's error at the ring's wrap (64 KB upload RING_UPLOADS - 1), no write-back of `narrow`.
    // Reserved (RENODX_TW_SET's "memory" line): the first pieces and the copies while in, nothing out.
    log_lines.clear();
    auto wide = GameBuffer(made.device.Get(), 65536, 63);
    auto narrow = GameBuffer(made.device.Get(), 256, 64);
    const auto narrow_bytes = Read(made.device.Get(), context, narrow.Get(), 256);
    bp::internal::piece_done = [](ID3D11DeviceContext*, ID3D11Query*, bool) { return DXGI_ERROR_DEVICE_REMOVED; };
    bp::Update(context, true, false);
    CHECK(bp::internal::pool.installed && fill(narrow.Get(), 0x55, 256));
    CHECK(bp::Reserved().starts_with(std::format("; Fewer Driver Waits in: {} pieces ({} KB), copies ", bp::internal::FIRST_PIECES,
                                                 bp::internal::FIRST_PIECES * 256)));
    size_t uploads = 0;
    while (!bp::internal::pool.lost && uploads < 2 * RING_UPLOADS) {
      CHECK(fill(wide.Get(), static_cast<int>(uploads), 65536));
      ++uploads;
    }
    CHECK(uploads == RING_UPLOADS - 1 && bp::internal::pool.stopped && Logged("pieces still in use") == 0);
    CHECK(Read(made.device.Get(), context, narrow.Get(), 256) == narrow_bytes);
    bp::Update(context, true, false);
    CHECK(!bp::internal::pool.installed && Logged("off. Restart the game for full speed.") == 1);
    bp::Update(context, true, false);
    CHECK(!bp::internal::pool.installed && Logged("Fewer Driver Waits: on") == 1);
    bp::Update(context, false, false);
    bp::internal::piece_done = nullptr;
    // Bodies unknown to the install in the table (taken for others): slot 47's both versions (a copy) and slot 14 (Map),
    // out at the present; slot 123 (PS Set1), out at a swap before it returns. Logged.
    for (const size_t body : {hooks::internal::COPY_RESOURCE, hooks::internal::MAP, hooks::internal::SET_CONSTANT_BUFFERS1 + 4}) {
      log_lines.clear();
      bp::Update(context, true, false);
      CHECK(bp::internal::pool.installed);
      auto& bodies = hooks::internal::state.bodies;
      const auto read_bodies = bodies;
      bodies[body] = &log_lines;
      if (body == hooks::internal::COPY_RESOURCE) bodies[hooks::internal::FLUSHED_COPIES + 1] = &log_lines;
      if (body == hooks::internal::SET_CONSTANT_BUFFERS1 + 4) {
        ComPtr<ID3DDeviceContextState> game_state;
        made.context1->SwapDeviceContextState(made.private_state.Get(), game_state.GetAddressOf());
        CHECK(bp::internal::pool.stopped);
        made.context1->SwapDeviceContextState(game_state.Get(), nullptr);
      }
      bp::Update(context, true, false);
      const std::string line = "body unknown to it (slot " + std::to_string(hooks::internal::BODY_SLOTS[body]) + ")";
      CHECK(!bp::internal::pool.installed && Logged(line.c_str()) == 1 && Logged("off. Restart the game for full speed.") == 1);
      bodies = read_bodies;
      bp::Update(context, false, false);
    }
    // A swap below the hook (native): the next swap's replaced state is not the active table's. Out at that swap.
    log_lines.clear();
    bp::Update(context, true, false);
    CHECK(bp::internal::pool.installed);
    {
      ComPtr<ID3DDeviceContextState> game_state;
      made.context1->SwapDeviceContextState(made.private_state.Get(), game_state.GetAddressOf());
      ComPtr<ID3DDeviceContextState> replaced;
      bp::internal::Native<hooks::internal::SwapStateFunction>(hooks::internal::SWAP_STATE)(made.context1.Get(), game_state.Get(), replaced.GetAddressOf());
      ++hooks::internal::state.depth;
      D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
      context->IAGetPrimitiveTopology(&topology);
      --hooks::internal::state.depth;
      CHECK(!bp::internal::pool.stopped);
      ComPtr<ID3DDeviceContextState> previous;
      made.context1->SwapDeviceContextState(made.private_state.Get(), previous.GetAddressOf());
      CHECK(previous.Get() == game_state.Get() && bp::internal::pool.stopped && Logged("a state swap it never saw") == 1);
      made.context1->SwapDeviceContextState(game_state.Get(), nullptr);
    }
    bp::Update(context, true, false);
    CHECK(!bp::internal::pool.installed && Logged("off. Restart the game for full speed.") == 1);
    bp::Update(context, false, false);
    // A buffer destroyed while mapped through the ring (d3d11 never saw the map): its map ends with it, the ring out at
    // the row's off.
    bp::Update(context, true, false);
    CHECK(bp::internal::pool.installed);
    {
      auto ring_mapped = Buffer(made.device.Get(), 256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 66);
      bool ring_mapped_gone = false;
      bp::Track(made.device.Get(), ring_mapped.Get(), nullptr);
      Watch(ring_mapped.Get(), &ring_mapped_gone);
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      CHECK(SUCCEEDED(context->Map(ring_mapped.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) && bp::internal::pool.open_maps == 1);
      ring_mapped.Reset();
      context->Flush();
      CHECK(ring_mapped_gone);
    }
    bp::Update(context, false, false);
    CHECK(!bp::internal::pool.installed && bp::internal::pool.open_maps == 0);
    CHECK(bp::Reserved().starts_with("; Fewer Driver Waits out: 0 pieces (0 KB), copies 0 KB for "));
    bp::ForgetDevice(made.device.Get());
    std::printf("6. unwind edges: no write-back into a buffer destroyed since the present, another thread's unbind is what the next Get"
                " returns (the device not removed); a lost device at upload %zu: no write-back, off (logged); unknown bodies in"
                " the table (a copy's, Map's: out at the present; PS Set1's: out at a swap), a swap below the hook: out, logged; a buffer"
                " destroyed while ring-mapped: its map ends, the ring out at the row's off\n",
                uploads);
  }

  // 7. Uninstall off the render thread (the add-on's detach on another).
  {
    log_lines.clear();
    Device made = MakeDevice();
    auto* context = made.context.Get();
    loss::devices = {{.device = made.device.Get(), .single_threaded = true}};
    auto buffer = GameBuffer(made.device.Get(), 256, 65);
    context->PSSetConstantBuffers(0, 1, buffer.GetAddressOf());
    bp::Update(context, true, false);
    CHECK(bp::internal::pool.installed);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    CHECK(SUCCEEDED(context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
    std::memset(mapped.pData, 0x33, 256);
    context->Unmap(buffer.Get(), 0);
    std::thread([] { bp::Uninstall(); }).join();
    CHECK(bp::internal::pool.installed && bp::internal::pool.unwind_wanted && !bp::internal::pool.stopped && !hooks::internal::state.ring_hooks.empty()
          && hooks::internal::state.lifetime.retain_until_exit && hooks::internal::state.ring_lifetime.retain_until_exit);
    ID3D11Buffer* probe = nullptr;
    context->PSGetConstantBuffers(0, 1, &probe);
    CHECK(probe == buffer.Get() && bp::internal::pool.stopped);
    probe->Release();
    ID3D11Buffer* native = nullptr;
    UINT first = 1;
    UINT count = 0;
    bp::internal::Native<hooks::internal::GetConstantBuffers1Function>(hooks::internal::GET_CONSTANT_BUFFERS1 + 4)(made.context1.Get(), 0, 1, &native,
                                                                                                                &first, &count);
    CHECK(native == buffer.Get() && first == 0 && count == 4096);
    native->Release();
    CHECK(Read(made.device.Get(), context, buffer.Get(), 256) == std::vector<uint32_t>(64, 0x33333333) && References(buffer.Get()) == 1);
    // At the process's end the hooks pass every call on; out here for the test's end (as at a present).
    bp::Update(context, false, false);
    CHECK(!bp::internal::pool.installed && hooks::internal::state.hooks.empty() && hooks::internal::state.ring_hooks.empty());
    bp::ForgetDevice(made.device.Get());
    std::printf("7. Uninstall off the render thread: hooks kept (to the process's end), the ring unwound at the render thread's next call (the"
                " buffer written back and bound again, its reference let go)\n");
  }

  // 8. The reclaim rule: (a) the stream with a straggler, the GPU 3 presents behind: the ring stays small, the outputs as
  // without it; CONTROL: the old retire distance (the next piece alone) grows the ring to its cap. (b) An event that
  // passes only when asked with a flush: no growth. (g) The record itself a straggler half a ring ahead: written once.
  {
    log_lines.clear();
    constexpr uint32_t FRAMES = 300;
    constexpr uint32_t FLUSH_FRAMES = 100;
    const Stream off = RunStream(false, FRAMES);
    CHECK(off.failed.empty() && std::ranges::count_if(off.computed, [](uint32_t word) { return word != 0; }) > 1000);
    bp::internal::piece_done = PassedAfterThreePresents;
    const Stream on = RunStream(true, FRAMES);
    bp::internal::retire_ahead = 1;
    const Stream control = RunStream(true, FRAMES);
    bp::internal::retire_ahead = 0;
    bp::internal::piece_done = PassedWithFlush;
    const Stream flushed = RunStream(true, FLUSH_FRAMES);
    bp::internal::piece_done = nullptr;
    const std::vector<uint32_t> off_start(off.computed.begin(), off.computed.begin() + (size_t{FLUSH_FRAMES} * 1024 * 4));
    std::printf("  stream, 3 presents behind: %zu pieces, %zu words differ; CONTROL retire distance 1: %zu pieces; flush only: %zu pieces"
                " (%zu passes), %zu words differ\n",
                on.pieces, Differences(off.computed, on.computed), control.pieces, flushed.pieces, flushed_passes,
                Differences(off_start, flushed.computed));
    CHECK(on.failed.empty() && control.failed.empty() && flushed.failed.empty());
    CHECK(on.pieces <= 12 && Differences(off.computed, on.computed) == 0);
    CHECK(control.pieces == bp::internal::MAX_PIECES);
    CHECK(flushed.pieces == bp::internal::FIRST_PIECES && flushed_passes > 0 && Differences(off_start, flushed.computed) == 0);
    // (g): x at piece 0's unit 0, 64 KB uploads fill pieces 0-3 to the last unit, then x again: the switch to piece 4
    // retires piece 0 (half a ring on) but leaves x to its own upload (one unit), piece 0 ended.
    Device made = MakeDevice();
    loss::devices = {{.device = made.device.Get(), .single_threaded = true}};
    const auto x = GameBuffer(made.device.Get(), 64, 70);
    const auto wide = GameBuffer(made.device.Get(), 65536, 71);
    bp::Update(made.context.Get(), true, false);
    CHECK(bp::internal::pool.installed);
    const auto fill = [&made](ID3D11Buffer* buffer, int value, UINT bytes) {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(made.context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
      std::memset(mapped.pData, value, bytes);
      made.context->Unmap(buffer, 0);
      return true;
    };
    CHECK(fill(x.Get(), 0x41, 64));
    for (int upload = 0; upload < 15; ++upload) CHECK(fill(wide.Get(), upload, 65536));
    const auto& pool = bp::internal::pool;
    CHECK(pool.current == 3 && pool.next_unit == bp::internal::PIECE_UNITS && bp::internal::Find(x.Get())->piece == pool.pieces[0].get());
    CHECK(fill(x.Get(), 0x42, 64));
    const bp::internal::Buffer* moved = bp::internal::Find(x.Get());
    CHECK(pool.current == 4 && pool.next_unit == 1 && moved->piece == pool.pieces[4].get() && moved->first == 0
          && pool.pieces[0]->phase == bp::internal::Phase::ENDED);
    CHECK(Read(made.device.Get(), made.context.Get(), x.Get(), 64) == std::vector<uint32_t>(16, 0x42424242u));
    bp::Update(made.context.Get(), false, false);
    bp::ForgetDevice(made.device.Get());
    std::printf("8. reclaim: the stream's ring at %zu pieces with the straggler retired half a ring ahead (CONTROL, the old rule: %zu), equal to"
                " the native outputs; an event passing only with the flush: no growth; the record as the straggler written once\n",
                on.pieces, control.pieces);
  }

  // 9. The full ring's wait (c): piece_cap 4 (no growth past the first pieces), 64 KB uploads of one buffer (4 a piece),
  // the ring's laps counted in RING_UPLOADS.
  {
    log_lines.clear();
    Device made = MakeDevice();
    loss::devices = {{.device = made.device.Get(), .single_threaded = true}};
    const auto wide = GameBuffer(made.device.Get(), 65536, 72);
    bp::internal::piece_cap = 4;
    bp::internal::piece_done = PassedAfterPolls;
    bp::Update(made.context.Get(), true, false);
    CHECK(bp::internal::pool.installed);
    uint8_t value = 0;
    // One upload of the next value: its time in ms (-1: the map failed).
    const auto fill = [&made, &wide, &value] {
      const auto start = std::chrono::steady_clock::now();
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(made.context->Map(wide.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return -1.0;
      std::memset(mapped.pData, ++value, 65536);
      made.context->Unmap(wide.Get(), 0);
      return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    const auto in_ring = [&wide] { return bp::internal::Find(wide.Get())->piece != nullptr; };
    const auto& pool = bp::internal::pool;
    // Passing after 3 polls: the first lap's end waits (one flush), every upload in the ring.
    cap_polls_left = 3;
    double passed = 0;
    bool all_in_ring = true;
    for (size_t upload = 0; upload < 2 * RING_UPLOADS; ++upload) {
      const double ms = fill();
      CHECK(ms >= 0);
      passed = std::max(passed, ms);
      all_in_ring = all_in_ring && in_ring();
    }
    std::printf("  passing after 3 polls: %zu polls, %zu flushes, the longest upload %.2f ms\n", cap_polls, cap_flushes, passed);
    CHECK(all_in_ring && cap_flushes == 1 && cap_polls == 4 + (bp::internal::FIRST_PIECES - 1) && pool.pieces.size() == bp::internal::FIRST_PIECES);
    CHECK(Logged(std::format("all {} pieces in use by the GPU: waited ", bp::internal::FIRST_PIECES).c_str()) == 1 && Logged("old way") == 0);
    // Never passing: the next lap's start waits FULL_WAIT_MS, then the fallback; the rest of the lap neither waits nor
    // flushes; the bytes as written.
    cap_polls_left = -1;
    cap_polls = 0;
    cap_flushes = 0;
    const double timed_out = fill();
    CHECK(timed_out >= 90 && !in_ring() && pool.full_waited && cap_flushes == 1 && Logged("old way") == 1);
    cap_polls = 0;
    cap_flushes = 0;
    double slowest = 0;
    bool none_in_ring = true;
    for (size_t upload = 1; upload < RING_UPLOADS; ++upload) {
      const double ms = fill();
      CHECK(ms >= 0);
      slowest = std::max(slowest, ms);
      none_in_ring = none_in_ring && !in_ring();
    }
    CHECK(none_in_ring && cap_polls == RING_UPLOADS - 1 && cap_flushes == 0 && Logged("waited") == 1 && Logged("old way") == 1);
    CHECK(Read(made.device.Get(), made.context.Get(), wide.Get(), 65536) == std::vector<uint32_t>(16384, value * 0x01010101u));
    // A switch that succeeds, a lap, then never passing again: waited again (no second line).
    cap_polls_left = 0;
    CHECK(fill() >= 0 && in_ring() && !pool.full_waited);
    for (size_t upload = 1; upload < RING_UPLOADS; ++upload) CHECK(fill() >= 0 && in_ring());
    cap_polls_left = -1;
    const double again = fill();
    CHECK(again >= 90 && !in_ring() && Logged("waited") == 1 && Logged("old way") == 1);
    CHECK(Read(made.device.Get(), made.context.Get(), wide.Get(), 65536) == std::vector<uint32_t>(16384, value * 0x01010101u));
    bp::internal::piece_cap = bp::internal::MAX_PIECES;
    bp::internal::piece_done = nullptr;
    bp::Update(made.context.Get(), false, false);
    bp::ForgetDevice(made.device.Get());
    std::printf("9. the full ring's wait: an event passing after polls: one wait (one flush), no fallback; never passing: one wait (%.0f ms),"
                " the fallback, %zu more uploads without a wait or a flush (slowest %.2f ms), the bytes as written; after a switch: a wait"
                " again (%.0f ms); each line once\n",
                timed_out, RING_UPLOADS - 1, slowest, again);
  }
  std::printf("PASS\n");
  return 0;
}
