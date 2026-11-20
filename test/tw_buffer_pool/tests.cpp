// twattila/buffer_pool.hpp (Fewer Driver Waits: the game's dynamic constant buffers through one ring of NO_OVERWRITE
// pieces) on WARP (the same d3d11.dll runtime bodies as the game's device), a single-threaded device as the game's, x86
// and amd64:
// 1. The ring's 35 bodies (the three copies' versions after a Flush among them): inside d3d11.dll, distinct, none
//    starting with a jump. Refused (logged, nothing hooked): two devices known, a multithreaded one.
// 2. The rules it relies on: the caps; a copy into a dynamic constant buffer lands, UpdateSubresource into one does not.
// 3. Pixels, compute output and copies, ring off vs on, each run on a fresh device: per-draw DISCARDs of a 208 B buffer
//    (the shaders read past its end), a once-a-frame one bound before its DISCARD, NO_OVERWRITE windows of a 1 KB one
//    (Set1), VS / PS / CS, Get1 / Set1 round trips, a private state swapped in and back (d3d11's lazy application),
//    ClearState, copies from and into game buffers (each through both versions of its body: after a Flush or not),
//    buffers bound and mapped before the install (the ring in at frame 0's present). Equal: the ring; the full ring (busy pieces: the fallback, logged once); another thread's call (the
//    ring unwinds at the render thread's next call, then stays off, logged); the map trace's measure path on beside it
//    (its records the caller's buffers). CONTROLS (must differ): no rebind, the Get hooks off, a piece reused early,
//    the copy hooks off, the swap's settling call left out.
// 4. Get / Get1 / Map parity against a second device (ring off) over random Set / Set1 (invalid windows, null arrays,
//    windows past a buffer's end) / Map-Unmap / ClearState / swaps: buffers and windows equal; every game buffer's
//    references its own plus one per slot of the ring at every step, its own alone after the ring's removal (the
//    device's too). A buffer the game lets go of while bound lives until unbound, as natively; CONTROL: without the
//    ring's reference it goes at once. Our own dynamic buffers (made below ReShade) never tracked.
// 5. The device's end with the ring on: its hooks out, its references let go, its records freed at the next present;
//    every body's first bytes back, the modules let go.
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
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
};

static Device MakeDevice(UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED) {
  Device made;
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, &level, 1, D3D11_SDK_VERSION, made.device.GetAddressOf(), nullptr,
                    made.context.GetAddressOf());
  made.device.As(&made.device1);
  made.context.As(&made.context1);
  made.device1->CreateDeviceContextState(D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1),
                                         nullptr, made.private_state.GetAddressOf());
  return made;
}

enum class Control { NONE, FULL, FOREIGN, MEASURE, NO_REBIND, GET_OFF, EARLY_REUSE, COPY_OFF, SETTLE_OFF };
constexpr std::array<const char*, 9> CONTROL_NAMES = {"ring",          "full ring",       "another thread",     "measure path beside",
                                                      "CONTROL no rebind", "CONTROL Get off", "CONTROL early reuse", "CONTROL copy off",
                                                      "CONTROL no settle"};

struct Output {
  std::vector<uint32_t> pixels;
  std::vector<uint32_t> computed;
  std::vector<uint32_t> copies;
  std::string failed;  // empty: ran
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

// NO_REBIND: the slots 0-2 of every stage put back where they were after each upload (as with no rebind).
static bool UnmapWithoutRebind(ID3D11Resource* resource, UINT subresource) {
  auto* context = bp::internal::pool.context;
  std::array<std::array<ID3D11Buffer*, 3>, hooks::STAGES> buffers = {};
  std::array<std::array<UINT, 3>, hooks::STAGES> firsts = {};
  std::array<std::array<UINT, 3>, hooks::STAGES> counts = {};
  for (size_t stage = 0; stage < hooks::STAGES; ++stage) {
    bp::internal::Native<hooks::internal::GetConstantBuffers1Function>(hooks::internal::GET_CONSTANT_BUFFERS1 + stage)(
        context, 0, 3, buffers[stage].data(), firsts[stage].data(), counts[stage].data());
  }
  const bool done = bp::internal::RingUnmap(resource, subresource);
  for (size_t stage = 0; stage < hooks::STAGES; ++stage) {
    bp::internal::Native<hooks::internal::SetConstantBuffers1Function>(hooks::internal::SET_CONSTANT_BUFFERS1 + stage)(
        context, 0, 3, buffers[stage].data(), firsts[stage].data(), counts[stage].data());
    for (ID3D11Buffer* buffer : buffers[stage]) {
      if (buffer != nullptr) buffer->Release();
    }
  }
  return done;
}

// The frames, ring off or on (with `control`): the render target, the compute output and every frame's copies.
static Output Run(bool ring, Control control, uint32_t frames, uint32_t draws) {
  Output out;
  const auto ring_paths = bp::internal::ring_paths;
  if (ring) {
    switch (control) {
      case Control::NO_REBIND: bp::internal::ring_paths.unmap = UnmapWithoutRebind; break;
      case Control::GET_OFF: bp::internal::ring_paths.get = [](hooks::Stage, UINT, UINT, ID3D11Buffer**, UINT*, UINT*) {}; break;
      case Control::COPY_OFF:
        bp::internal::ring_paths.copy = [](ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, const D3D11_BOX*, const UINT*) { return false; };
        bp::internal::ring_paths.copy_resource = [](ID3D11Resource*, ID3D11Resource*) { return false; };
        break;
      case Control::EARLY_REUSE:
        bp::internal::piece_cap = 2;
        bp::internal::piece_done = [](ID3D11DeviceContext*, ID3D11Query*) { return S_OK; };
        break;
      case Control::FULL:
        bp::internal::piece_cap = bp::internal::FIRST_PIECES;
        bp::internal::piece_done = [](ID3D11DeviceContext*, ID3D11Query*) { return S_FALSE; };
        break;
      default: break;
    }
  }
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
  bind_all(false);
  for (uint32_t frame = 0; frame < frames; ++frame) {
    if (ring && control == Control::MEASURE && frame == 2) {
      measured.clear();
      if (!hooks::Install(context, Measure, false)) out.failed = "the measure path not installed beside the ring";
    }
    if (ring && control == Control::MEASURE && frame == 3) hooks::Uninstall();
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
    write(b.Get(), D3D11_MAP_WRITE_DISCARD, 0, 256, 1000 + frame, -1);
    if (frame == 4) context->CopyResource(b.Get(), b_source.Get());
    for (uint32_t step = 0; step < draws; ++step) {
      ++index;
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
          // The swap back without the hook's settling call: d3d11 applies the state at the next call, outside the hook.
          ComPtr<ID3DDeviceContextState> replaced;
          native_swap(game_state.Get(), replaced.GetAddressOf());
          ++hooks::internal::state.depth;
          bp::internal::RingSwap(game_state.Get(), replaced.Get());
          --hooks::internal::state.depth;
        } else {
          context1->SwapDeviceContextState(game_state.Get(), nullptr);
        }
        ++index;
        write(a.Get(), D3D11_MAP_WRITE_DISCARD, 0, 208, index, static_cast<int>(index));
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
    for (auto [copy, bytes] : {std::pair{copy_a.Get(), 208u}, std::pair{copy_b.Get(), 256u}, std::pair{copy_c.Get(), 1024u}}) {
      const auto words = Read(made.device.Get(), context, copy, bytes);
      out.copies.insert(out.copies.end(), words.begin(), words.end());
    }
    bp::Update(context, ring, false);
    if (ring && frame == 0 && !bp::internal::pool.installed) out.failed = "not installed at frame 0's present";
  }
  if (ring && control == Control::MEASURE) {
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
  out.pixels = pixels();
  out.computed = Read(made.device.Get(), context, output.Get(), 1024 * 16);
  if (ring) {
    bp::Update(context, false, false);
    if (bp::internal::pool.installed || !hooks::internal::state.hooks.empty() || !hooks::internal::state.ring_hooks.empty()) {
      out.failed = "the ring's hooks left after Update(off)";
    }
  }
  bp::internal::ring_paths = ring_paths;
  bp::internal::piece_cap = bp::internal::MAX_PIECES;
  bp::internal::piece_done = nullptr;
  bp::ForgetDevice(made.device.Get());
  return out;
}

static size_t Differences(const std::vector<uint32_t>& left, const std::vector<uint32_t>& right) {
  size_t count = (left.size() > right.size() ? left.size() - right.size() : right.size() - left.size());
  for (size_t index = 0; index < std::min(left.size(), right.size()); ++index) count += (left[index] != right[index] ? 1 : 0);
  return count;
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  CHECK(Compile());

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
    loss::devices = {{.device = made.device.Get(), .single_threaded = true}, {.device = made.device.Get(), .single_threaded = true}};
    bp::Update(made.context.Get(), true, false);
    CHECK(!bp::internal::pool.installed && Logged("2 D3D11 devices") == 1 && hooks::internal::state.hooks.empty() && bp::internal::pool.copies == false);
    bp::Update(made.context.Get(), true, false);
    CHECK(Logged("2 D3D11 devices") == 1);  // refused once until the row is off
    bp::Update(made.context.Get(), false, false);
    loss::devices = {{.device = made.device.Get(), .single_threaded = false}};
    bp::Update(made.context.Get(), true, false);
    CHECK(!bp::internal::pool.installed && Logged("not single-threaded") == 1 && hooks::internal::state.hooks.empty());
    bp::Update(made.context.Get(), false, false);
    bp::ForgetDevice(made.device.Get());
    std::printf("1. 35 distinct bodies in d3d11.dll (the ring's 18 printed), no jump prologue; refused once each (logged, nothing hooked): 2 devices, a"
                " multithreaded one\n");
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
      const bool equal = pixels == 0 && computed == 0 && copies == 0;
      const bool must_equal = static_cast<Control>(control) <= Control::MEASURE;
      std::printf("  %-22s pixels %zu, compute %zu, copies %zu words differ\n", CONTROL_NAMES[control], pixels, computed, copies);
      CHECK(equal == must_equal);
      switch (static_cast<Control>(control)) {
        case Control::NONE: CHECK(Logged("Fewer Driver Waits: on") == 1 && Logged("pieces still in use") == 0); break;
        case Control::FULL: CHECK(Logged("all 4 pieces still in use") == 1); break;
        case Control::FOREIGN: CHECK(Logged("called the game's context") == 1 && Logged("off until the row") == 1); break;
        case Control::COPY_OFF: CHECK(copies != 0); break;
        default: break;
      }
    }
    std::printf("3. %u frames x %u draws (lit words %zu): equal with the ring, a full ring, another thread's call and the measure path beside it;"
                " every CONTROL differs\n",
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
    using Get1 = void (STDMETHODCALLTYPE ID3D11DeviceContext1::*)(UINT, UINT, ID3D11Buffer**, UINT*, UINT*);
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
    // The ring out: every reference as natively, the device's too, and the binds the same.
    bp::Update(ring.context.Get(), false, false);
    CHECK(!bp::internal::pool.installed && compare(3002));
    for (size_t index = 0; index < SIZES.size(); ++index) CHECK(References(ring_buffers[index].Get()) == References(native_buffers[index].Get()));
    CHECK(References(ring.device.Get()) == device_references);
    std::printf("4. Get / Get1 parity over 3000 random steps, every game buffer's references its own plus one per slot of the ring at every step,"
                " its own alone after the removal (the device: %lu references as before); Map parity; our own buffer untracked; a buffer let go of"
                " while bound lives until unbound, as natively; CONTROL: without the ring's reference it goes at once\n",
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
  std::printf("PASS\n");
  return 0;
}
