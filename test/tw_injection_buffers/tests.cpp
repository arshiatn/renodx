// mods::shader's native D3D11 injection (PushNativeD3D11Injection, use_native_d3d11_injection: Attila's Fewer Constant
// Uploads) on WARP with ReShade's command list mocked (its push_constants counted) and the context's Map counted (its
// vtable slot):
// 1. The opt-in is off by default.
// 2. 200 pushes of the same 432 bytes (Attila's ShaderInjectData size): one upload (one WRITE_DISCARD map), 200 binds
//    (slot 13 cleared before each push, bound again after it at VS, HS, DS, GS and PS, not CS), no ReShade push; the
//    bound buffer holds the bytes (a staging copy). A dispatch's push binds CS only.
// 3. One float changed: one new upload, the bound buffer holds the new bytes; 50 more pushes: no upload.
//    CONTROL: the re-upload skipped (the ring told the bytes are uploaded): the bound buffer differs from the bytes.
// 4. Not taken (false, no map, nothing bound): a deferred context, another API, slot 14.
// 5. The GPU's view: 24 changes in a row without a flush, each followed by a draw into its own pixel that reads b13 (its
//    first and last 16 bytes), then 4 draws of the same bytes: every pixel holds its own push's values; one upload a
//    change; the ring grew past 1 buffer and stays at 8 or fewer. CONTROL: a change with the re-upload skipped draws the
//    old values.
// 6. destroy_device of the ring's device releases it; another device's leaves it.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
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

struct CommandList : mock::CommandList {
  int reshade_pushes = 0;
  void push_constants(reshade::api::shader_stage, reshade::api::pipeline_layout, uint32_t, uint32_t, uint32_t, const void*) override {
    ++reshade_pushes;
  }
};

using MapFunction = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
static MapFunction original_map = nullptr;
static int discard_maps = 0;
static HRESULT STDMETHODCALLTYPE CountingMap(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_MAP type,
                                             UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped) {
  if (type == D3D11_MAP_WRITE_DISCARD) ++discard_maps;
  return original_map(context, resource, subresource, type, flags, mapped);
}

constexpr UINT SLOT = 13;
constexpr size_t FLOATS = 108;
static std::array<float, FLOATS> injection = {};

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

int main() {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> immediate;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr,
                                    &immediate)));
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

  Device game_device;
  game_device.native = device;
  CommandList cmd_list;
  cmd_list.owner = &game_device;
  cmd_list.native = immediate;
  for (size_t index = 0; index < FLOATS; ++index) injection[index] = 0.25f * static_cast<float>(index + 1);
  shader::shader_injection = injection.data();
  shader::shader_injection_size = FLOATS;
  auto& ring = shader::internal::native_injection_ring;

  // 1.
  CHECK(!shader::use_native_d3d11_injection.load());
  std::printf("1 off by default\n");

  // 2.
  ComPtr<ID3D11Buffer> first_bound;
  for (int push = 0; push < 200; ++push) {
    Unbind(immediate.Get());
    CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
    const ComPtr<ID3D11Buffer> bound = BoundOnce(immediate.Get(), false);
    CHECK(bound != nullptr);
    if (push == 0) first_bound = bound;
    CHECK(bound == first_bound);
  }
  CHECK(discard_maps == 1);
  CHECK(cmd_list.reshade_pushes == 0);
  CHECK(Holds(device.Get(), immediate.Get(), first_bound.Get(), injection));
  Unbind(immediate.Get());
  CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, true));
  CHECK(BoundOnce(immediate.Get(), true) == first_bound);
  CHECK(discard_maps == 1);
  std::printf("2 200 pushes of the same bytes: 1 upload, 200 binds (VS HS DS GS PS), no ReShade push, the bytes bound; a dispatch: CS\n");

  // 3.
  injection[57] = -3.5f;
  Unbind(immediate.Get());
  CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
  CHECK(discard_maps == 2);
  ComPtr<ID3D11Buffer> changed = BoundOnce(immediate.Get(), false);
  CHECK(changed != nullptr && Holds(device.Get(), immediate.Get(), changed.Get(), injection));
  for (int push = 0; push < 50; ++push) CHECK(shader::PushNativeD3D11Injection(&cmd_list, SLOT, false));
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
  CommandList deferred_list;
  deferred_list.owner = &game_device;
  deferred_list.native = deferred;
  OtherApiDevice other_device;
  CommandList other_list;
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
  const ComPtr<ID3DBlob> vs_code = Compile(
      "float4 main(uint id : SV_VertexID) : SV_Position { return float4(id == 2 ? 3.f : -1.f, id == 1 ? 3.f : -1.f, 0.f, 1.f); }",
      "vs_5_0");
  const ComPtr<ID3DBlob> ps_code = Compile(
      "cbuffer injection : register(b13) { float4 values[27]; };"
      "float4 main(float4 position : SV_Position) : SV_Target { return values[0] + values[26]; }",
      "ps_5_0");
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
    for (int component = 0; component < 4; ++component) expected[change][component] = values[component] + injection[104 + component];
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
  Device another_device;
  shader::OnDestroyDevice(&another_device);
  CHECK(ring.device == &game_device && !ring.buffers.empty());
  shader::OnDestroyDevice(&game_device);
  CHECK(ring.device == nullptr && ring.buffers.empty() && ring.uploaded.empty());
  std::printf("6 destroy_device: another device's leaves the ring, its own releases it\n");
  std::printf("PASS\n");
  return 0;
}
