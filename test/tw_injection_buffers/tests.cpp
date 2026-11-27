// mods::shader's native D3D11 injection (PushNativeD3D11Injection, use_native_d3d11_injection: Attila's Fewer Constant
// Uploads) on WARP with ReShade's command list mocked and the context's Map counted (its vtable slot):
// 1. The opt-in is off by default: not taken (false, no map, nothing bound) on a device that never used it; then on.
// 2. 200 pushes of the same 432 bytes (Attila's ShaderInjectData size): one upload (one WRITE_DISCARD map), 200 binds
//    (slot 13 cleared before each push, bound again after it at VS, HS, DS, GS and PS, not CS); the bound buffer holds
//    the bytes (a staging copy). A dispatch's push binds CS only.
// 3. One float changed: one new upload, the bound buffer holds the new bytes; 50 more pushes: no upload.
//    CONTROL: the re-upload skipped (the ring told the bytes are uploaded): the bound buffer differs from the bytes.
// 4. Not taken (false, no map, nothing bound): a deferred context, another API, slot 14.
// 5. The GPU's view: 24 changes in a row without a flush, each followed by a draw into its own pixel that reads b13 (its
//    first and last 16 bytes), then 4 draws of the same bytes: every pixel holds its own push's values; one upload a
//    change; the ring grew past 1 buffer and stays at 8 or fewer. CONTROL: a change with the re-upload skipped draws the
//    old values.
// 6. The latch: the opt-in off after the ring's device used it: still taken there (an upload, the bytes bound), not on
//    another device that never used it. CONTROL: the ring told it follows no device: not taken (ReShade's push).
// 7. destroy_device of the ring's device releases it (then not taken with the opt-in off); another device's leaves it.
// 8. (`hardware`, run.ps1 -Hardware: alone in its own process, so the Map count is never patched in) No regression on
//    the hardware adapter, flags 0 as the 64-bit port's device: 540 draws a frame into a 3840 x 2160 target, each after
//    its arm's push. Arm R is ReShade's D3D11 push (d3d11_impl_command_list.cpp push_constants: its copy, a 432-byte
//    WRITE_DISCARD map, 5 binds); arm F is PushNativeD3D11Injection with the bytes changed 4 times a frame (F(4)) or
//    every draw (F(540)). Runs R1, F(4)a, F(4)b, R2, F(540), each 60 warm-up and 600 timed frames, median a run.
//    PASS when max(F(4)a, F(4)b) <= mean(R1, R2) + 3 x A/A, A/A = max(|R1 - R2|, 2 % of the mean). F/R, F(540)/R and
//    A/A are printed as numbers; a REGRESSION exits 1.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "src/mods/shader.hpp"
#include "test/tw_particle_history/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;
namespace shader = renodx::mods::shader;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

extern "C" __declspec(dllexport) const char* const NAME = "Injection buffers test";
extern "C" {
__declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t) { return true; }
__declspec(dllexport) void ReShadeUnregisterAddon(void*) {}
__declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
}

struct Device : mock::Device {
  void get_private_data(const uint8_t[16], uint64_t* data) const override { *data = 0; }
  void set_private_data(const uint8_t[16], const uint64_t) override {}
};

struct OtherApiDevice : Device {
  reshade::api::device_api get_api() const override { return reshade::api::device_api::d3d12; }
};

using MapFunction = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
static MapFunction original_map = nullptr;
static int discard_maps = 0;
static HRESULT STDMETHODCALLTYPE CountingMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type,
                                             UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped) {
  if (type == D3D11_MAP_WRITE_DISCARD) {
    ++discard_maps;
  }
  return original_map(context, resource, subresource, type, flags, mapped);
}

constexpr UINT SLOT = 13;
constexpr size_t FLOATS = 108;
static std::array<float, FLOATS> injection = {};

// Cases 5 and 8: a full-screen triangle, and a pixel shader that reads b13's first and last 16 bytes (so the driver
// keeps the binds).
constexpr char FULL_SCREEN_VS[] =
    "float4 main(uint id : SV_VertexID) : SV_Position { return float4(id == 2 ? 3.f : -1.f, id == 1 ? 3.f : -1.f, 0.f, 1.f); }";
constexpr char INJECTION_PS[] =
    "cbuffer injection : register(b13) { float4 values[27]; };"
    "float4 main(float4 position : SV_Position) : SV_Target { return values[0] + values[26]; }";

static ComPtr<ID3D11Buffer> Bound(ID3D11DeviceContext* context, int stage) {
  ComPtr<ID3D11Buffer> buffer;
  switch (stage) {
    case 0: context->VSGetConstantBuffers(SLOT, 1, buffer.GetAddressOf()); break;
    case 1: context->HSGetConstantBuffers(SLOT, 1, buffer.GetAddressOf()); break;
    case 2: context->DSGetConstantBuffers(SLOT, 1, buffer.GetAddressOf()); break;
    case 3: context->GSGetConstantBuffers(SLOT, 1, buffer.GetAddressOf()); break;
    case 4: context->PSGetConstantBuffers(SLOT, 1, buffer.GetAddressOf()); break;
    default: context->CSGetConstantBuffers(SLOT, 1, buffer.GetAddressOf()); break;
  }
  return buffer;
}

static void Unbind(ID3D11DeviceContext* context) {
  ID3D11Buffer* none = nullptr;
  context->VSSetConstantBuffers(SLOT, 1, &none);
  context->HSSetConstantBuffers(SLOT, 1, &none);
  context->DSSetConstantBuffers(SLOT, 1, &none);
  context->GSSetConstantBuffers(SLOT, 1, &none);
  context->PSSetConstantBuffers(SLOT, 1, &none);
  context->CSSetConstantBuffers(SLOT, 1, &none);
}

// The graphics stages hold one buffer at SLOT and CS none (or CS alone with `compute`).
static ComPtr<ID3D11Buffer> BoundOnce(ID3D11DeviceContext* context, bool compute) {
  const ComPtr<ID3D11Buffer> first = Bound(context, compute ? 5 : 0);
  if (!first) return nullptr;
  for (int stage = 0; stage < 6; ++stage) {
    const bool expected = compute ? (stage == 5) : (stage != 5);
    const ComPtr<ID3D11Buffer> buffer = Bound(context, stage);
    if (expected ? (buffer != first) : (buffer != nullptr)) return nullptr;
  }
  return first;
}

static bool Holds(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Buffer* buffer, const std::array<float, FLOATS>& bytes) {
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  if (FAILED(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf()))) return false;
  context->CopyResource(staging.Get(), buffer);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
  const bool same = std::memcmp(mapped.pData, bytes.data(), sizeof(bytes)) == 0;
  context->Unmap(staging.Get(), 0);
  return same;
}

static ComPtr<ID3DBlob> Compile(const char* source, const char* target) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", target, 0, 0, code.GetAddressOf(),
                        errors.GetAddressOf()))) {
    std::printf("compile: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return nullptr;
  }
  return code;
}

int main(int argc, char** argv) {
  const bool hardware = argc > 1 && std::strcmp(argv[1], "hardware") == 0;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> immediate;
  // Flags 0 on the default adapter in case 8: the 64-bit port's own D3D11CreateDevice.
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, (hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP), nullptr, 0, nullptr,
                                    0, D3D11_SDK_VERSION, &device, nullptr, &immediate)));
  Device game_device;
  game_device.native = device;
  mock::CommandList cmd_list;
  cmd_list.owner = &game_device;
  cmd_list.native = immediate;
  for (size_t index = 0; index < FLOATS; ++index) {
    injection[index] = 0.25f * static_cast<float>(index + 1);
  }
  shader::shader_injection = injection.data();
  shader::shader_injection_size = FLOATS;
  auto& ring = shader::internal::native_injection_ring;

  if (hardware) {
    // 8. The timing follows Microsoft's "Accurately Profiling Direct3D API Calls" (QPC around the calls, an event query
    // and a flush to bound the GPU work) with a frame latency of 2, as IDXGIDevice1::SetMaximumFrameLatency keeps a
    // game's present queue: each frame ends with an event and a Flush, then waits for frame N-2's event outside the timed
    // span. Without the Flush and the wait the CPU runs ahead, and arm R's renames behave unlike a game's.
    {
      ComPtr<IDXGIDevice> dxgi_device;
      ComPtr<IDXGIAdapter> adapter;
      DXGI_ADAPTER_DESC adapter_desc = {};
      CHECK(SUCCEEDED(device.As(&dxgi_device)) && SUCCEEDED(dxgi_device->GetAdapter(adapter.GetAddressOf()))
            && SUCCEEDED(adapter->GetDesc(&adapter_desc)));
      std::printf("8 adapter: %ls, device flags %u\n", adapter_desc.Description, device->GetCreationFlags());
    }
    constexpr UINT DRAWS = 540;
    constexpr UINT WARM_UP_FRAMES = 60;
    constexpr UINT TIMED_FRAMES = 600;
    const D3D11_TEXTURE2D_DESC target_desc = {.Width = 3840, .Height = 2160, .MipLevels = 1, .ArraySize = 1,
                                              .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .SampleDesc = {1, 0},
                                              .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    CHECK(SUCCEEDED(device->CreateTexture2D(&target_desc, nullptr, target.GetAddressOf())));
    CHECK(SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf())));
    const ComPtr<ID3DBlob> vs_code = Compile(FULL_SCREEN_VS, "vs_5_0");
    const ComPtr<ID3DBlob> ps_code = Compile(INJECTION_PS, "ps_5_0");
    CHECK(vs_code && ps_code);
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    CHECK(SUCCEEDED(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, vs.GetAddressOf())));
    CHECK(SUCCEEDED(device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, ps.GetAddressOf())));
    const D3D11_VIEWPORT viewport = {.TopLeftX = 0.f, .TopLeftY = 0.f, .Width = static_cast<float>(target_desc.Width),
                                     .Height = static_cast<float>(target_desc.Height), .MinDepth = 0.f, .MaxDepth = 1.f};
    immediate->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    immediate->VSSetShader(vs.Get(), nullptr, 0);
    immediate->PSSetShader(ps.Get(), nullptr, 0);
    immediate->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
    immediate->RSSetViewports(1, &viewport);
    // Arm R's buffer and ReShade's own copy of the pushed values (its _push_constants / _push_constants_data).
    const D3D11_BUFFER_DESC push_desc = {.ByteWidth = sizeof(injection), .Usage = D3D11_USAGE_DYNAMIC,
                                         .BindFlags = D3D11_BIND_CONSTANT_BUFFER, .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
    ComPtr<ID3D11Buffer> push_buffer;
    CHECK(SUCCEEDED(device->CreateBuffer(&push_desc, nullptr, push_buffer.GetAddressOf())));
    std::array<float, FLOATS> reshade_values = {};
    std::array<ComPtr<ID3D11Query>, 3> frame_done;
    const D3D11_QUERY_DESC event_desc = {.Query = D3D11_QUERY_EVENT};
    for (auto& query : frame_done) {
      CHECK(SUCCEEDED(device->CreateQuery(&event_desc, query.GetAddressOf())));
    }
    LARGE_INTEGER frequency = {};
    QueryPerformanceFrequency(&frequency);
    shader::use_native_d3d11_injection.store(true);
    UINT frame = 0;
    float changed_value = 0.f;
    // One run: the median submission time of a frame, in microseconds; nullopt when a push or the wait failed.
    const auto median_frame_us = [&](bool native, UINT changes_a_frame) -> std::optional<double> {
      std::vector<double> frame_us;
      frame_us.reserve(TIMED_FRAMES);
      for (UINT run_frame = 0; run_frame < WARM_UP_FRAMES + TIMED_FRAMES; ++run_frame, ++frame) {
        LARGE_INTEGER start = {};
        QueryPerformanceCounter(&start);
        for (UINT draw = 0; draw < DRAWS; ++draw) {
          if (draw % (DRAWS / changes_a_frame) == 0) {
            injection[0] = ++changed_value;
          }
          if (native) {
            if (!shader::PushNativeD3D11Injection(&cmd_list, SLOT, false)) return std::nullopt;
          } else {
            std::memcpy(reshade_values.data(), injection.data(), sizeof(injection));
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (FAILED(immediate->Map(push_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return std::nullopt;
            std::memcpy(mapped.pData, reshade_values.data(), sizeof(reshade_values));
            immediate->Unmap(push_buffer.Get(), 0);
            ID3D11Buffer* const bound = push_buffer.Get();
            immediate->VSSetConstantBuffers(SLOT, 1, &bound);
            immediate->HSSetConstantBuffers(SLOT, 1, &bound);
            immediate->DSSetConstantBuffers(SLOT, 1, &bound);
            immediate->GSSetConstantBuffers(SLOT, 1, &bound);
            immediate->PSSetConstantBuffers(SLOT, 1, &bound);
          }
          immediate->Draw(3, 0);
        }
        immediate->End(frame_done[frame % frame_done.size()].Get());
        immediate->Flush();
        LARGE_INTEGER end = {};
        QueryPerformanceCounter(&end);
        if (run_frame >= WARM_UP_FRAMES) {
          frame_us.push_back(1e6 * static_cast<double>(end.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart));
        }
        if (frame < 2) continue;
        HRESULT done = S_FALSE;
        while ((done = immediate->GetData(frame_done[(frame - 2) % frame_done.size()].Get(), nullptr, 0,
                                          D3D11_ASYNC_GETDATA_DONOTFLUSH))
               == S_FALSE) {
          std::this_thread::yield();
        }
        if (done != S_OK) return std::nullopt;
      }
      std::ranges::nth_element(frame_us, frame_us.begin() + TIMED_FRAMES / 2);
      return frame_us[TIMED_FRAMES / 2];
    };
    const std::optional<double> reshade_1 = median_frame_us(false, 4);
    const std::optional<double> native_4a = median_frame_us(true, 4);
    const std::optional<double> native_4b = median_frame_us(true, 4);
    const std::optional<double> reshade_2 = median_frame_us(false, 4);
    const std::optional<double> native_540 = median_frame_us(true, DRAWS);
    CHECK(reshade_1 && native_4a && native_4b && reshade_2 && native_540);
    const double reshade = (*reshade_1 + *reshade_2) / 2.0;
    const double native_4 = std::max(*native_4a, *native_4b);
    const double spread = std::max(std::abs(*reshade_1 - *reshade_2), 0.02 * reshade);
    const double limit = reshade + 3.0 * spread;
    std::printf("8 %u draws a frame at %ux%u, median us a frame: R1 %.1f, F(4)a %.1f, F(4)b %.1f, R2 %.1f, F(540) %.1f\n", DRAWS,
                target_desc.Width, target_desc.Height, *reshade_1, *native_4a, *native_4b, *reshade_2, *native_540);
    std::printf("8 R %.1f, A/A %.1f (%.1f %%), F/R %.3f, F(540)/R %.3f, ring %zu buffers; gate F(4) %.1f <= R + 3 x A/A %.1f\n",
                reshade, spread, 100.0 * spread / reshade, native_4 / reshade, *native_540 / reshade, ring.buffers.size(), native_4,
                limit);
    if (native_4 > limit) {
      std::printf("REGRESSION: F(4) above R + 3 x A/A\n");
      return 1;
    }
    std::printf("PASS\n");
    return 0;
  }

  ComPtr<ID3D11DeviceContext> deferred;
  CHECK(SUCCEEDED(device->CreateDeferredContext(0, &deferred)));
  {
    void** vtable = *reinterpret_cast<void***>(immediate.Get());
    DWORD protection = 0;
    CHECK(VirtualProtect(&vtable[14], sizeof(void*), PAGE_READWRITE, &protection));
    original_map = reinterpret_cast<MapFunction>(vtable[14]);
    vtable[14] = reinterpret_cast<void*>(&CountingMap);
    VirtualProtect(&vtable[14], sizeof(void*), protection, &protection);
  }
  {
    // The hook counts: one dynamic buffer's DISCARD map.
    const D3D11_BUFFER_DESC desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                    .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
    ComPtr<ID3D11Buffer> probe;
    CHECK(SUCCEEDED(device->CreateBuffer(&desc, nullptr, probe.GetAddressOf())));
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    CHECK(SUCCEEDED(immediate->Map(probe.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
    immediate->Unmap(probe.Get(), 0);
    CHECK(discard_maps == 1);
    discard_maps = 0;
  }

  // 1.
  CHECK(!shader::use_native_d3d11_injection.load());
  CHECK(!shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
  CHECK(discard_maps == 0 && Bound(immediate.Get(), 4) == nullptr && ring.device == nullptr);
  shader::use_native_d3d11_injection.store(true);
  std::printf("1 off by default: not taken on a device that never used it\n");

  // 2.
  ComPtr<ID3D11Buffer> first_bound;
  for (int push = 0; push < 200; ++push) {
    Unbind(immediate.Get());
    CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
    const ComPtr<ID3D11Buffer> bound = BoundOnce(immediate.Get(), false);
    CHECK(bound != nullptr);
    if (push == 0) {
      first_bound = bound;
    }
    CHECK(bound == first_bound);
  }
  CHECK(discard_maps == 1);
  CHECK(Holds(device.Get(), immediate.Get(), first_bound.Get(), injection));
  Unbind(immediate.Get());
  CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, true));
  CHECK(BoundOnce(immediate.Get(), true) == first_bound);
  CHECK(discard_maps == 1);
  std::printf("2 200 pushes of the same bytes: 1 upload, 200 binds (VS HS DS GS PS), the bytes bound; a dispatch: CS\n");

  // 3.
  injection[57] = -3.5f;
  Unbind(immediate.Get());
  CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
  CHECK(discard_maps == 2);
  ComPtr<ID3D11Buffer> changed = BoundOnce(immediate.Get(), false);
  CHECK(changed != nullptr && Holds(device.Get(), immediate.Get(), changed.Get(), injection));
  for (int push = 0; push < 50; ++push) {
    CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
  }
  CHECK(discard_maps == 2);
  // CONTROL: the re-upload skipped.
  injection[3] = 99.f;
  ring.uploaded.assign(reinterpret_cast<const uint8_t*>(injection.data()), reinterpret_cast<const uint8_t*>(injection.data()) + sizeof(injection));
  CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
  CHECK(discard_maps == 2);
  CHECK(!Holds(device.Get(), immediate.Get(), BoundOnce(immediate.Get(), false).Get(), injection));
  ring.uploaded.clear();  // the next push uploads again
  CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
  CHECK(discard_maps == 3 && Holds(device.Get(), immediate.Get(), BoundOnce(immediate.Get(), false).Get(), injection));
  std::printf("3 a change: 1 new upload, the new bytes bound; 50 more: no upload; CONTROL: the upload skipped, the bound bytes differ\n");

  // 4.
  mock::CommandList deferred_list;
  deferred_list.owner = &game_device;
  deferred_list.native = deferred;
  OtherApiDevice other_device;
  mock::CommandList other_list;
  other_list.owner = &other_device;
  other_list.native = immediate;
  Unbind(immediate.Get());
  injection[0] = 7.f;
  CHECK(!shader::PushNativeD3D11Injection(&deferred_list, SLOT, false));
  CHECK(!shader::PushNativeD3D11Injection(&other_list, SLOT, false));
  CHECK(!shader::PushNativeD3D11Injection(&cmd_list, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, false));
  CHECK(discard_maps == 3 && Bound(immediate.Get(), 4) == nullptr);
  std::printf("4 not taken: a deferred context, another API, slot 14 (no map, nothing bound)\n");

  // 5.
  constexpr UINT PIXELS = 32;
  constexpr UINT CHANGES = 24;
  const D3D11_TEXTURE2D_DESC target_desc = {.Width = PIXELS, .Height = 1, .MipLevels = 1, .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {1, 0},
                                            .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  CHECK(SUCCEEDED(device->CreateTexture2D(&target_desc, nullptr, target.GetAddressOf())));
  CHECK(SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf())));
  const ComPtr<ID3DBlob> vs_code = Compile(FULL_SCREEN_VS, "vs_5_0");
  const ComPtr<ID3DBlob> ps_code = Compile(INJECTION_PS, "ps_5_0");
  CHECK(vs_code && ps_code);
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11PixelShader> ps;
  CHECK(SUCCEEDED(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, vs.GetAddressOf())));
  CHECK(SUCCEEDED(device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, ps.GetAddressOf())));
  immediate->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  immediate->VSSetShader(vs.Get(), nullptr, 0);
  immediate->PSSetShader(ps.Get(), nullptr, 0);
  immediate->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  const auto draw_pixel = [&](UINT pixel) {
    const D3D11_VIEWPORT viewport = {.TopLeftX = static_cast<float>(pixel), .TopLeftY = 0.f, .Width = 1.f, .Height = 1.f,
                                     .MinDepth = 0.f, .MaxDepth = 1.f};
    immediate->RSSetViewports(1, &viewport);
    immediate->Draw(3, 0);
  };
  const auto values_of = [](UINT change) {
    return std::array<float, 4>{static_cast<float>(change), 0.5f, -static_cast<float>(change), 2.f};
  };
  std::vector<std::array<float, 4>> expected(PIXELS);
  const int maps_before = discard_maps;
  for (UINT change = 0; change < CHANGES; ++change) {
    const auto values = values_of(change);
    for (int component = 0; component < 4; ++component) {
      injection[component] = values[component];
      injection[104 + component] = 1000.f * static_cast<float>(component + 1);
    }
    CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
    draw_pixel(change);
    for (int component = 0; component < 4; ++component) {
      expected[change][component] = values[component] + injection[104 + component];
    }
  }
  for (UINT pixel = CHANGES; pixel < CHANGES + 4; ++pixel) {
    CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
    draw_pixel(pixel);
    expected[pixel] = expected[CHANGES - 1];
  }
  CHECK(discard_maps - maps_before == static_cast<int>(CHANGES));
  const size_t ring_size = ring.buffers.size();
  CHECK(ring_size > 1 && ring_size <= shader::internal::NATIVE_INJECTION_BUFFERS);
  // CONTROL: a change drawn with the re-upload skipped.
  const auto stale_values = expected[CHANGES - 1];
  injection[0] = 12345.f;
  ring.uploaded.assign(reinterpret_cast<const uint8_t*>(injection.data()), reinterpret_cast<const uint8_t*>(injection.data()) + sizeof(injection));
  CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
  draw_pixel(CHANGES + 4);
  {
    D3D11_TEXTURE2D_DESC staging_desc = target_desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    CHECK(SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf())));
    immediate->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    CHECK(SUCCEEDED(immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    const auto* pixels = static_cast<const std::array<float, 4>*>(mapped.pData);
    bool all_own = true;
    for (UINT pixel = 0; pixel < CHANGES + 4; ++pixel) {
      if (pixels[pixel] == expected[pixel]) continue;
      std::printf("  pixel %u: %g %g %g %g, expected %g %g %g %g\n", pixel, pixels[pixel][0], pixels[pixel][1], pixels[pixel][2],
                  pixels[pixel][3], expected[pixel][0], expected[pixel][1], expected[pixel][2], expected[pixel][3]);
      all_own = false;
    }
    const bool control_stale = pixels[CHANGES + 4] == stale_values && pixels[CHANGES + 4][0] != 12345.f + 1000.f;
    immediate->Unmap(staging.Get(), 0);
    CHECK(all_own);
    CHECK(control_stale);
  }
  ring.uploaded.clear();
  std::printf("5 %u changes without a flush, a draw each, 4 more: every pixel its own push's values, %u uploads, %zu buffers in the ring;"
              " CONTROL: the skipped upload draws the old values\n", CHANGES, CHANGES, ring_size);

  // 6.
  shader::use_native_d3d11_injection.store(false);
  injection[1] = -8.f;
  Unbind(immediate.Get());
  const int maps_before_latch = discard_maps;
  CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
  CHECK(discard_maps == maps_before_latch + 1 && Holds(device.Get(), immediate.Get(), BoundOnce(immediate.Get(), false).Get(), injection));
  Device another_device;
  another_device.native = device;
  mock::CommandList another_list;
  another_list.owner = &another_device;
  another_list.native = immediate;
  Unbind(immediate.Get());
  CHECK(!shader::PushNativeD3D11Injection(&another_list, SLOT, false));
  CHECK(Bound(immediate.Get(), 4) == nullptr && ring.device == &game_device);
  // CONTROL: the latch forgotten.
  ring.device = nullptr;
  CHECK(!shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
  CHECK(discard_maps == maps_before_latch + 1 && Bound(immediate.Get(), 4) == nullptr);
  ring.device = &game_device;
  std::printf("6 the opt-in off: the ring's device keeps it (an upload, the bytes bound), another device not; CONTROL: forgotten, not taken\n");

  // 7.
  shader::OnDestroyDevice(&another_device);
  CHECK(ring.device == &game_device && !ring.buffers.empty());
  shader::OnDestroyDevice(&game_device);
  CHECK(ring.device == nullptr && ring.buffers.empty() && ring.uploaded.empty() && ring.current == 0);
  CHECK(!shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
  std::printf("7 destroy_device: another device's leaves the ring, its own releases it (then not taken with the opt-in off)\n");
  std::printf("PASS\n");
  return 0;
}
