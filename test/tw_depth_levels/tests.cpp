#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

using Microsoft::WRL::ComPtr;
static std::span<const unsigned char> __depth_levels;
static int fail_at = 0;
static int calls = 0;
// The sole retained test reference lets us check that failed construction drops
// every production reference, including those indirectly held by mip views.
static ComPtr<ID3D11Texture2D> observed_texture;
static HRESULT TestCreateTexture2D(ID3D11Device* device, const D3D11_TEXTURE2D_DESC* desc,
                                   const D3D11_SUBRESOURCE_DATA* data, ID3D11Texture2D** out) {
  if (++calls == fail_at) return E_OUTOFMEMORY;
  const auto hr = device->CreateTexture2D(desc, data, out);
  if (SUCCEEDED(hr)) observed_texture = *out;
  return hr;
}
static HRESULT TestCreateShaderResourceView(ID3D11Device* device, ID3D11Resource* resource,
                                            const D3D11_SHADER_RESOURCE_VIEW_DESC* desc,
                                            ID3D11ShaderResourceView** out) {
  if (++calls == fail_at) return E_OUTOFMEMORY;
  return device->CreateShaderResourceView(resource, desc, out);
}
static HRESULT TestCreateUnorderedAccessView(ID3D11Device* device, ID3D11Resource* resource,
                                             const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc,
                                             ID3D11UnorderedAccessView** out) {
  if (++calls == fail_at) return E_OUTOFMEMORY;
  return device->CreateUnorderedAccessView(resource, desc, out);
}
#include "depth_levels_under_test.hpp"
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}

#define CHECK(x)                                  \
  do {                                            \
    if (!(x)) {                                   \
      std::printf("FAIL %d: %s\n", __LINE__, #x); \
      std::abort();                               \
    }                                             \
  } while (false)
namespace levels = twattila::depth_levels;
int main(int argc, char** argv) {
  CHECK(argc == 2);
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                    D3D11_SDK_VERSION, &device, nullptr, &context)));
  ComPtr<ID3DBlob> shader, errors;
  const std::wstring shader_path(argv[1], argv[1] + std::strlen(argv[1]));
  CHECK(SUCCEEDED(D3DCompileFromFile(shader_path.c_str(), nullptr, nullptr, "main", "cs_5_0", 0, 0, &shader, &errors)));
  __depth_levels = {static_cast<const unsigned char*>(shader->GetBufferPointer()), shader->GetBufferSize()};
  std::array<float, 64 * 64> pixels;
  pixels.fill(0.5f);
  D3D11_TEXTURE2D_DESC desc = {.Width = 64, .Height = 64, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA data = {.pSysMem = pixels.data(), .SysMemPitch = 64 * sizeof(float)};
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11ShaderResourceView> depth_view;
  CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &data, &depth)));
  CHECK(SUCCEEDED(device->CreateShaderResourceView(depth.Get(), nullptr, &depth_view)));
  auto& objects = levels::internal::runtime.objects;
  for (int step = 1; step <= 12; ++step) {
    objects = {};
    observed_texture.Reset();
    calls = 0;
    fail_at = step;
    CHECK(!levels::Build(context.Get(), depth_view.Get()));
    CHECK(calls == step);
    CHECK(objects.depth_width == 0 && objects.depth_height == 0);
    CHECK(!objects.texture && !objects.view);
    for (UINT i = 0; i < levels::internal::LEVELS; ++i) {
      CHECK(!objects.level_views[i] && !objects.level_writes[i]);
    }
    if (observed_texture) {
      observed_texture->AddRef();
      CHECK(observed_texture->Release() == 1);
    }
    fail_at = 0;
    CHECK(!levels::Build(context.Get(), depth_view.Get()));
    CHECK(calls == step);  // cooldown did not attempt any allocation
    objects.retry_after = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    calls = 0;
    auto result = levels::Build(context.Get(), depth_view.Get());
    CHECK(result && calls == 12 && objects.depth_width == 64 && objects.depth_height == 64);
    for (UINT i = 0; i < levels::internal::LEVELS; ++i) {
      CHECK(objects.level_views[i] && objects.level_writes[i]);
    }
    CHECK(levels::Build(context.Get(), depth_view.Get()).Get() == result.Get());
    CHECK(calls == 12);  // complete same-size cache reused
    D3D11_TEXTURE2D_DESC staging_desc;
    objects.texture->GetDesc(&staging_desc);
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    CHECK(SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, &staging)));
    context->CopyResource(staging.Get(), objects.texture.Get());
    for (UINT i = 0; i < levels::internal::LEVELS; ++i) {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      CHECK(SUCCEEDED(context->Map(staging.Get(), i, D3D11_MAP_READ, 0, &mapped)));
      CHECK(*static_cast<const UINT*>(mapped.pData) == 0x3f000000u);
      context->Unmap(staging.Get(), i);
    }
    std::printf("PASS allocation step %d: rollback, cooldown, same-size recovery, five mip readbacks\n", step);
  }
  // A failed resize must discard the previous cache and recover at the new size.
  desc.Width = 128;
  depth_view.Reset();
  depth.Reset();
  CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &depth)));
  CHECK(SUCCEEDED(device->CreateShaderResourceView(depth.Get(), nullptr, &depth_view)));
  calls = 0;
  fail_at = 12;
  CHECK(!levels::Build(context.Get(), depth_view.Get()));
  CHECK(!objects.view && !objects.texture && objects.depth_width == 0);
  for (UINT i = 0; i < levels::internal::LEVELS; ++i) CHECK(!objects.level_views[i] && !objects.level_writes[i]);
  objects.retry_after = {};
  fail_at = 0;
  CHECK(levels::Build(context.Get(), depth_view.Get()) && objects.depth_width == 128);
  context->ClearState();
  context->Flush();
  objects = {};
  std::puts("PASS resize recovery");
}
