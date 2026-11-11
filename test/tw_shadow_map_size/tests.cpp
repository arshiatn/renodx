// Warhammer III's Shadow Map Size (twwarhammer3/shadow_map_size.hpp and its three pixel shaders) on WARP: the maps made at
// twice their size, the viewport and scissor a draw into them gets, the shared shadow constants' size, and what a reader
// then finds, at the game's size (1x) and at twice (2x). tests.exe <the built shaders' folder> <the game's shaders' folder>
// 1. create_resource: only the game's 4096 x 4096 [4] R32 depth arrays grow, only with the row on.
// 2. Targets and areas: a 4096 viewport and scissor rect become 8192 while a larger map is the depth target, in either order
//    of the game's calls, on a deferred context too; they go back when another target is bound; a viewport the game set
//    itself at 8192 on another target is not touched; other sizes are not touched.
// 3. The shared shadow constants: 4096, 4096, 1 / 4096, 1 / 4096 become the larger size at a map's unmap and at an update,
//    every other byte the game's; not while no larger map is alive, not when the game's size is another.
// 4. Writer and reader agree: a rectangle drawn with the game's 4096 viewport into the map, read back at uv x the constants'
//    size, is found at the same uv at 1x and at 2x.
// 5. The three pixel shaders: at the game's size ours give the game's own bytes' result to the bit; at 2x, over a map whose
//    texels are the 1x map's doubled, the two half-size copies give the 1x result to the bit and the kept maps' copy gives
//    the 1x result doubled.
// 6. Shadow Maps Under Rays: the maps made at 2048 and 1024 (only those made here tracked, not the game's own of that size),
//    the viewport and constants at their size, and the three shaders with shader_injection.shadow_map_scale 0.5 and 0.25: the
//    half copies over the game's 2048 / 1024 targets give the game's result to the bit at 0.5 and the 4 x 4 deepest at 0.25,
//    the kept maps' copy the game's result halved / quartered.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <string>
#include <vector>
#include "src/games/twwarhammer3/shadow_map_size.hpp"
#include "src/games/twwarhammer3/shared.h"

using Microsoft::WRL::ComPtr;
namespace size = twwarhammer3::shadow_map_size;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
}

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> immediate;
static std::mt19937 rng(7);

static std::vector<uint8_t> Load(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

struct Map {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11DepthStencilView> depth;
  ComPtr<ID3D11ShaderResourceView> view;
};

// A depth array as the game makes its shadow maps, through the addon's create and init events as ReShade raises them.
static Map MakeMap(uint32_t asked, uint32_t slices, bool announce) {
  reshade::api::resource_desc desc(asked, asked, static_cast<uint16_t>(slices), 1, reshade::api::format::r32_typeless, 1, reshade::api::memory_heap::gpu_only,
                                   reshade::api::resource_usage::depth_stencil | reshade::api::resource_usage::shader_resource);
  if (announce) size::OnCreateResource(nullptr, desc, nullptr, reshade::api::resource_usage::undefined);
  const D3D11_TEXTURE2D_DESC native = {.Width = desc.texture.width, .Height = desc.texture.height, .MipLevels = 1, .ArraySize = slices,
                                       .Format = DXGI_FORMAT_R32_TYPELESS, .SampleDesc = {1, 0},
                                       .BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE};
  Map map;
  if (FAILED(device->CreateTexture2D(&native, nullptr, map.texture.GetAddressOf()))) return map;
  const D3D11_DEPTH_STENCIL_VIEW_DESC depth_desc = {.Format = DXGI_FORMAT_D32_FLOAT, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY,
                                                    .Texture2DArray = {.MipSlice = 0, .FirstArraySlice = 0, .ArraySize = 1}};
  device->CreateDepthStencilView(map.texture.Get(), &depth_desc, map.depth.GetAddressOf());
  const D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_R32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY,
                                                     .Texture2DArray = {.MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = 0, .ArraySize = slices}};
  device->CreateShaderResourceView(map.texture.Get(), &view_desc, map.view.GetAddressOf());
  if (announce) size::OnInitResource(nullptr, desc, nullptr, reshade::api::resource_usage::undefined, {reinterpret_cast<uint64_t>(map.texture.Get())});
  return map;
}

static uint32_t WidthOf(const Map& map) {
  D3D11_TEXTURE2D_DESC desc = {};
  map.texture->GetDesc(&desc);
  return desc.Width;
}

static float ViewportOf(ID3D11DeviceContext* context) {
  UINT count = 1;
  D3D11_VIEWPORT viewport = {};
  context->RSGetViewports(&count, &viewport);
  return (count != 0 && viewport.Width == viewport.Height) ? viewport.Width : -1.f;
}

static LONG ScissorOf(ID3D11DeviceContext* context) {
  UINT count = 1;
  D3D11_RECT rect = {};
  context->RSGetScissorRects(&count, &rect);
  return (count != 0 && rect.right == rect.bottom) ? rect.right : -1;
}

// The game's two calls and ReShade's events after each, as its proxy raises them.
static void GameSetsViewport(ID3D11DeviceContext* context, float width) {
  const D3D11_VIEWPORT viewport = {.TopLeftX = 0.f, .TopLeftY = 0.f, .Width = width, .Height = width, .MinDepth = 0.f, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  size::AreaBound(context, width, width);
}

static void GameSetsScissor(ID3D11DeviceContext* context, LONG width) {
  const D3D11_RECT rect = {.left = 0, .top = 0, .right = width, .bottom = width};
  context->RSSetScissorRects(1, &rect);
  size::AreaBound(context, static_cast<float>(width), static_cast<float>(width));
}

static void GameBindsDepth(ID3D11DeviceContext* context, ID3D11DepthStencilView* depth) {
  context->OMSetRenderTargets(0, nullptr, depth);
  size::TargetsBound(context);
}

static std::vector<float> ReadSlice(ID3D11Texture2D* texture, uint32_t slice) {
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  const D3D11_TEXTURE2D_DESC staging_desc = {.Width = desc.Width, .Height = desc.Height, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_TYPELESS,
                                             .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf());
  immediate->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture, slice, nullptr);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<float> texels(size_t{desc.Width} * desc.Height);
  for (uint32_t y = 0; y < desc.Height; ++y) {
    std::memcpy(texels.data() + (size_t{y} * desc.Width), static_cast<const uint8_t*>(mapped.pData) + (size_t{y} * mapped.RowPitch), size_t{desc.Width} * 4);
  }
  immediate->Unmap(staging.Get(), 0);
  return texels;
}

static ComPtr<ID3DBlob> Compile(const char* source, const char* target) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", target, 0, 0, code.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("%s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "compile failed");
  }
  return code;
}

// A full-target triangle with COLOR and TEXCOORD as the game's copies' vertex shader gives them (uv 0..1, y down), or a
// rectangle in clip space at a depth.
constexpr const char* TRIANGLE_VS =
    "cbuffer c : register(b0) { float4 area; float depth; }\n"
    "void main(uint id : SV_VertexID, out float4 position : SV_Position, out float4 colour : COLOR0, out float2 uv : TEXCOORD0) {\n"
    "  const float2 corner = float2((id == 1) ? 2.f : 0.f, (id == 2) ? 2.f : 0.f);\n"
    "  uv = corner;\n"
    "  colour = 1.f;\n"
    "  position = float4(lerp(area.xy, area.zw, corner), depth, 1.f);\n"
    "}\n";
// The same with the uv straight after the position (C883E3FA's input).
constexpr const char* TRIANGLE_UV_VS =
    "void main(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD0) {\n"
    "  uv = float2((id == 1) ? 2.f : 0.f, (id == 2) ? 2.f : 0.f);\n"
    "  position = float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.f, 1.f);\n"
    "}\n";

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                                    immediate.GetAddressOf())));
  constexpr uint32_t GAME = size::GAME_SIZE;
  constexpr uint32_t LARGER = size::GAME_SIZE * 2;

  // 1.
  {
    reshade::api::resource_desc map(GAME, GAME, 4, 1, reshade::api::format::r32_typeless, 1, reshade::api::memory_heap::gpu_only,
                                    reshade::api::resource_usage::depth_stencil | reshade::api::resource_usage::shader_resource);
    size::made_size = size::GAME_SIZE;
    CHECK(!size::OnCreateResource(nullptr, map, nullptr, reshade::api::resource_usage::undefined) && map.texture.width == GAME);
    size::made_size = LARGER;
    for (auto other : {reshade::api::resource_desc(2048, 2048, 4, 1, reshade::api::format::r32_typeless, 1, reshade::api::memory_heap::gpu_only,
                                                   reshade::api::resource_usage::depth_stencil | reshade::api::resource_usage::shader_resource),
                       reshade::api::resource_desc(GAME, GAME, 1, 1, reshade::api::format::r32_typeless, 1, reshade::api::memory_heap::gpu_only,
                                                   reshade::api::resource_usage::depth_stencil | reshade::api::resource_usage::shader_resource),
                       reshade::api::resource_desc(GAME, GAME, 4, 1, reshade::api::format::r32_float, 1, reshade::api::memory_heap::gpu_only,
                                                   reshade::api::resource_usage::render_target | reshade::api::resource_usage::shader_resource),
                       reshade::api::resource_desc(GAME, GAME, 4, 1, reshade::api::format::r24_g8_typeless, 1, reshade::api::memory_heap::gpu_only,
                                                   reshade::api::resource_usage::depth_stencil | reshade::api::resource_usage::shader_resource)}) {
      const uint32_t before = other.texture.width;
      CHECK(!size::OnCreateResource(nullptr, other, nullptr, reshade::api::resource_usage::undefined) && other.texture.width == before);
    }
    CHECK(size::OnCreateResource(nullptr, map, nullptr, reshade::api::resource_usage::undefined) && map.texture.width == LARGER
          && map.texture.height == LARGER && map.texture.depth_or_layers == 4);
  }

  // The game's two maps with the row on, and a depth target of the game's size that is none of them.
  Map dynamic = MakeMap(GAME, 4, true);
  CHECK(dynamic.texture && WidthOf(dynamic) == LARGER && size::live_maps == 1);
  size::made_size = size::GAME_SIZE;
  Map other = MakeMap(GAME, 1, true);
  size::made_size = LARGER;
  CHECK(other.texture && WidthOf(other) == GAME && size::live_maps == 1);

  // 2.
  ComPtr<ID3D11DeviceContext> deferred;
  CHECK(SUCCEEDED(device->CreateDeferredContext(0, deferred.GetAddressOf())));
  for (ID3D11DeviceContext* context : {immediate.Get(), deferred.Get()}) {
    // The viewport first, then the target.
    GameSetsViewport(context, static_cast<float>(GAME));
    GameSetsScissor(context, static_cast<LONG>(GAME));
    CHECK(ViewportOf(context) == GAME && ScissorOf(context) == static_cast<LONG>(GAME));
    GameBindsDepth(context, dynamic.depth.Get());
    CHECK(ViewportOf(context) == LARGER && ScissorOf(context) == static_cast<LONG>(LARGER));
    // The game binds another target and takes its viewport for set already.
    GameBindsDepth(context, other.depth.Get());
    CHECK(ViewportOf(context) == GAME && ScissorOf(context) == static_cast<LONG>(GAME));
    // The target first, then the viewport and the rect.
    GameBindsDepth(context, dynamic.depth.Get());
    GameSetsViewport(context, 2048.f);
    CHECK(ViewportOf(context) == 2048.f);
    GameSetsViewport(context, static_cast<float>(GAME));
    GameSetsScissor(context, static_cast<LONG>(GAME));
    CHECK(ViewportOf(context) == LARGER && ScissorOf(context) == static_cast<LONG>(LARGER));
    // Bound again with the larger viewport in place: left.
    GameBindsDepth(context, dynamic.depth.Get());
    CHECK(ViewportOf(context) == LARGER);
    // The game sets its own viewport on another target: the game's, also at the larger size.
    GameBindsDepth(context, other.depth.Get());
    GameSetsViewport(context, static_cast<float>(LARGER));
    GameBindsDepth(context, other.depth.Get());
    CHECK(ViewportOf(context) == LARGER);
    GameSetsViewport(context, static_cast<float>(GAME));
    CHECK(ViewportOf(context) == GAME);
    context->ClearState();
  }
  // No larger map alive: nothing is touched.
  {
    size::OnDestroyResource(nullptr, {reinterpret_cast<uint64_t>(dynamic.texture.Get())});
    CHECK(size::live_maps == 0);
    GameSetsViewport(immediate.Get(), static_cast<float>(GAME));
    GameBindsDepth(immediate.Get(), dynamic.depth.Get());
    CHECK(ViewportOf(immediate.Get()) == GAME);
    immediate->ClearState();
    reshade::api::resource_desc larger(LARGER, LARGER, 4, 1, reshade::api::format::r32_typeless, 1, reshade::api::memory_heap::gpu_only,
                                       reshade::api::resource_usage::depth_stencil | reshade::api::resource_usage::shader_resource);
    size::OnInitResource(nullptr, larger, nullptr, reshade::api::resource_usage::undefined, {reinterpret_cast<uint64_t>(dynamic.texture.Get())});
    CHECK(size::live_maps == 1);
  }

  // 3.
  constexpr UINT BYTES = static_cast<UINT>(size::CONSTANTS_BYTES);
  const auto game_constants = [](float map_size) {
    std::array<uint8_t, BYTES> bytes;
    bytes.fill(0xAB);
    const std::array<float, 4> first = {map_size, map_size, 1.f / map_size, 1.f / map_size};
    std::memcpy(bytes.data(), first.data(), sizeof(first));
    return bytes;
  };
  const auto read_back = [](ID3D11Buffer* buffer) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = BYTES, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Buffer> staging;
    device->CreateBuffer(&desc, nullptr, staging.GetAddressOf());
    immediate->CopyResource(staging.Get(), buffer);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    std::array<uint8_t, BYTES> bytes = {};
    std::memcpy(bytes.data(), mapped.pData, BYTES);
    immediate->Unmap(staging.Get(), 0);
    return bytes;
  };
  const auto make_constants = [](D3D11_USAGE usage, UINT bytes) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = usage, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                    .CPUAccessFlags = (usage == D3D11_USAGE_DYNAMIC) ? static_cast<UINT>(D3D11_CPU_ACCESS_WRITE) : 0u};
    ComPtr<ID3D11Buffer> buffer;
    device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf());
    const reshade::api::resource_desc announced(bytes, reshade::api::memory_heap::cpu_to_gpu, reshade::api::resource_usage::constant_buffer);
    size::OnInitResource(nullptr, announced, nullptr, reshade::api::resource_usage::undefined, {reinterpret_cast<uint64_t>(buffer.Get())});
    return buffer;
  };
  const auto upload = [](ID3D11Buffer* buffer, const std::array<uint8_t, BYTES>& bytes) {
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    immediate->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    size::OnMapBufferRegion(nullptr, {reinterpret_cast<uint64_t>(buffer)}, 0, UINT64_MAX, reshade::api::map_access::write_discard, &mapped.pData);
    D3D11_BUFFER_DESC desc = {};
    buffer->GetDesc(&desc);
    std::memcpy(mapped.pData, bytes.data(), desc.ByteWidth);
    size::OnUnmapBufferRegion(nullptr, {reinterpret_cast<uint64_t>(buffer)});
    immediate->Unmap(buffer, 0);
  };
  const auto larger_constants = [&] {
    auto bytes = game_constants(static_cast<float>(GAME));
    const std::array<float, 4> first = {static_cast<float>(LARGER), static_cast<float>(LARGER), 1.f / LARGER, 1.f / LARGER};
    std::memcpy(bytes.data(), first.data(), sizeof(first));
    return bytes;
  }();
  ComPtr<ID3D11Buffer> shadow_constants = make_constants(D3D11_USAGE_DYNAMIC, BYTES);
  {
    upload(shadow_constants.Get(), game_constants(static_cast<float>(GAME)));
    CHECK(read_back(shadow_constants.Get()) == larger_constants && size::constants.open_map_count == 0);
    // Another size of the game's (a lower Shadows setting): left.
    upload(shadow_constants.Get(), game_constants(2048.f));
    CHECK(read_back(shadow_constants.Get()) == game_constants(2048.f));
    // A buffer of another size holding the same floats: left.
    ComPtr<ID3D11Buffer> other_constants = make_constants(D3D11_USAGE_DYNAMIC, 656);
    upload(other_constants.Get(), game_constants(static_cast<float>(GAME)));
    {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = 656, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
      ComPtr<ID3D11Buffer> staging;
      device->CreateBuffer(&desc, nullptr, staging.GetAddressOf());
      immediate->CopyResource(staging.Get(), other_constants.Get());
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
      float first = 0.f;
      std::memcpy(&first, mapped.pData, sizeof(first));
      immediate->Unmap(staging.Get(), 0);
      CHECK(first == GAME);
    }
    // An update, on the immediate and on a deferred context.
    ComPtr<ID3D11Buffer> updated = make_constants(D3D11_USAGE_DEFAULT, BYTES);
    const auto game = game_constants(static_cast<float>(GAME));
    CHECK(size::UpdateWithSize(immediate.Get(), game.data(), {reinterpret_cast<uint64_t>(updated.Get())}, 0, UINT64_MAX));
    CHECK(read_back(updated.Get()) == larger_constants);
    const auto lower = game_constants(2048.f);
    CHECK(!size::UpdateWithSize(immediate.Get(), lower.data(), {reinterpret_cast<uint64_t>(updated.Get())}, 0, UINT64_MAX));
    CHECK(!size::UpdateWithSize(immediate.Get(), game.data(), {reinterpret_cast<uint64_t>(other_constants.Get())}, 0, UINT64_MAX));
    CHECK(!size::UpdateWithSize(immediate.Get(), game.data(), {reinterpret_cast<uint64_t>(updated.Get())}, 0, 8));
    // No larger map alive: the game's bytes.
    size::OnDestroyResource(nullptr, {reinterpret_cast<uint64_t>(dynamic.texture.Get())});
    upload(shadow_constants.Get(), game);
    CHECK(read_back(shadow_constants.Get()) == game);
    CHECK(!size::UpdateWithSize(immediate.Get(), game.data(), {reinterpret_cast<uint64_t>(updated.Get())}, 0, UINT64_MAX));
    reshade::api::resource_desc larger(LARGER, LARGER, 4, 1, reshade::api::format::r32_typeless, 1, reshade::api::memory_heap::gpu_only,
                                       reshade::api::resource_usage::depth_stencil | reshade::api::resource_usage::shader_resource);
    size::OnInitResource(nullptr, larger, nullptr, reshade::api::resource_usage::undefined, {reinterpret_cast<uint64_t>(dynamic.texture.Get())});
    size::OnDestroyResource(nullptr, {reinterpret_cast<uint64_t>(updated.Get())});
    size::OnDestroyResource(nullptr, {reinterpret_cast<uint64_t>(other_constants.Get())});
  }

  // 4. A caster's rectangle drawn with the game's calls, read as the fog reads it: ld at uv x the constants' size.
  const ComPtr<ID3DBlob> triangle_code = Compile(TRIANGLE_VS, "vs_5_0");
  const ComPtr<ID3DBlob> triangle_uv_code = Compile(TRIANGLE_UV_VS, "vs_5_0");
  CHECK(triangle_code && triangle_uv_code);
  ComPtr<ID3D11VertexShader> triangle;
  ComPtr<ID3D11VertexShader> triangle_uv;
  device->CreateVertexShader(triangle_code->GetBufferPointer(), triangle_code->GetBufferSize(), nullptr, triangle.GetAddressOf());
  device->CreateVertexShader(triangle_uv_code->GetBufferPointer(), triangle_uv_code->GetBufferSize(), nullptr, triangle_uv.GetAddressOf());
  const D3D11_DEPTH_STENCIL_DESC always_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL, .DepthFunc = D3D11_COMPARISON_ALWAYS};
  ComPtr<ID3D11DepthStencilState> always;
  device->CreateDepthStencilState(&always_desc, always.GetAddressOf());
  const D3D11_RASTERIZER_DESC raster_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE, .DepthClipEnable = TRUE, .ScissorEnable = TRUE};
  ComPtr<ID3D11RasterizerState> scissored;
  device->CreateRasterizerState(&raster_desc, scissored.GetAddressOf());
  const D3D11_BUFFER_DESC area_desc = {.ByteWidth = 32, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  ComPtr<ID3D11Buffer> area_buffer;
  device->CreateBuffer(&area_desc, nullptr, area_buffer.GetAddressOf());
  {
    // A rectangle from uv (0.25, 0.5) to (0.875, 0.9375) at depth 0.5: the triangle's first corner and twice its extent, cut by nothing.
    const std::array<float, 2> uv_first = {0.25f, 0.5f};
    const std::array<float, 2> uv_last = {0.875f, 0.9375f};
    Map game_map = MakeMap(GAME, 1, false);
    CHECK(game_map.texture);
    for (const bool larger : {false, true}) {
      const Map& map = (larger ? dynamic : game_map);
      immediate->ClearState();
      immediate->ClearDepthStencilView(map.depth.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
      // The game's own order and numbers, whatever the map's size: viewport and scissor 4096.
      GameSetsViewport(immediate.Get(), static_cast<float>(GAME));
      GameSetsScissor(immediate.Get(), static_cast<LONG>(GAME));
      GameBindsDepth(immediate.Get(), map.depth.Get());
      // The rectangle as two corners in clip space (y up): the triangle's corners at `first` and at twice the extent, the
      // scissor's part beyond the map cut by the viewport's own clip.
      const std::array<float, 8> area = {(uv_first[0] * 2.f) - 1.f, 1.f - (uv_first[1] * 2.f), (uv_last[0] * 2.f) - 1.f, 1.f - (uv_last[1] * 2.f), 0.5f};
      immediate->UpdateSubresource(area_buffer.Get(), 0, nullptr, area.data(), 0, 0);
      immediate->VSSetShader(triangle.Get(), nullptr, 0);
      immediate->VSSetConstantBuffers(0, 1, area_buffer.GetAddressOf());
      immediate->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      immediate->OMSetDepthStencilState(always.Get(), 0);
      immediate->RSSetState(scissored.Get());
      immediate->Draw(3, 0);
      immediate->ClearState();
      // The reader's size: the game's constants as uploaded while this map is the one alive.
      if (!larger) size::OnDestroyResource(nullptr, {reinterpret_cast<uint64_t>(dynamic.texture.Get())});
      upload(shadow_constants.Get(), game_constants(static_cast<float>(GAME)));
      const auto constants = read_back(shadow_constants.Get());
      if (!larger) {
        reshade::api::resource_desc desc(LARGER, LARGER, 4, 1, reshade::api::format::r32_typeless, 1, reshade::api::memory_heap::gpu_only,
                                         reshade::api::resource_usage::depth_stencil | reshade::api::resource_usage::shader_resource);
        size::OnInitResource(nullptr, desc, nullptr, reshade::api::resource_usage::undefined, {reinterpret_cast<uint64_t>(dynamic.texture.Get())});
      }
      std::array<float, 4> reader_size = {};
      std::memcpy(reader_size.data(), constants.data(), sizeof(reader_size));
      const uint32_t width = WidthOf(map);
      CHECK(reader_size[0] == static_cast<float>(width) && reader_size[2] == 1.f / static_cast<float>(width));
      const std::vector<float> texels = ReadSlice(map.texture.Get(), 0);
      // The triangle covers the rectangle's lower left half (from the first corner along both edges): uv inside it reads
      // the caster's depth, uv outside the far depth, the same uv at both sizes.
      uint32_t wrong = 0;
      std::uniform_real_distribution<float> any(0.f, 1.f);
      for (int sample = 0; sample < 20000; ++sample) {
        const float u = any(rng);
        const float v = any(rng);
        const float s = (u - uv_first[0]) / (uv_last[0] - uv_first[0]);
        const float t = (v - uv_first[1]) / (uv_last[1] - uv_first[1]);
        // Away from the triangle's edges by more than a game texel.
        const float edge = std::min({s, t, 2.f - s - t});
        if (std::abs(edge) < 0.01f || s > 2.f || t > 2.f) continue;
        const bool inside = edge > 0.f && u < 1.f && v < 1.f;
        const auto x = static_cast<uint32_t>(u * reader_size[0]);
        const auto y = static_cast<uint32_t>(v * reader_size[1]);
        const float depth = texels[(size_t{y} * width) + x];
        wrong += (depth == (inside ? 0.5f : 1.f)) ? 0u : 1u;
      }
      std::printf("writer and reader at %ux: map %u, viewport from the game's 4096, reader's size %g: %u of the samples wrong\n", larger ? 2u : 1u, width,
                  reader_size[0], wrong);
      CHECK(wrong == 0);
    }
  }

  // 5. The three shaders. Maps of one slice: 1x at 512 (any size up to 4096 is the game's case), 2x at 8192 from a 4096 one.
  const std::string ours = argv[1];
  const std::string games = argv[2];
  const auto pixel_shader = [](const std::string& path) {
    const auto code = Load(path);
    ComPtr<ID3D11PixelShader> shader;
    if (!code.empty()) device->CreatePixelShader(code.data(), code.size(), nullptr, shader.GetAddressOf());
    return shader;
  };
  const std::array<const char*, 3> hashes = {"0xC883E3FA", "0x800F7F7F", "0xD5DCC85F"};
  std::array<ComPtr<ID3D11PixelShader>, 3> our_shaders;
  std::array<ComPtr<ID3D11PixelShader>, 3> game_shaders;
  for (size_t index = 0; index < hashes.size(); ++index) {
    our_shaders[index] = pixel_shader(ours + "/" + hashes[index] + ".cso");
    game_shaders[index] = pixel_shader(games + "/" + hashes[index] + ".ps_5_0.cso");
    CHECK(our_shaders[index] && game_shaders[index]);
  }
  // A map of one slice filled with `values` (by a copy from a staging texture).
  const auto filled = [](uint32_t width, const std::vector<float>& values) {
    Map map = MakeMap(width, 1, false);
    const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = width, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_TYPELESS, .SampleDesc = {1, 0},
                                       .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = values.data(), .SysMemPitch = width * 4};
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&desc, &data, staging.GetAddressOf());
    immediate->CopyResource(map.texture.Get(), staging.Get());
    return map;
  };
  const auto random_map = [](uint32_t width) {
    std::vector<float> values(size_t{width} * width);
    std::uniform_real_distribution<float> any(0.f, 1.f);
    for (float& value : values) value = any(rng);
    return values;
  };
  const auto doubled = [](uint32_t width, const std::vector<float>& values) {
    std::vector<float> twice(size_t{width} * width * 4);
    for (uint32_t y = 0; y < width * 2; ++y) {
      for (uint32_t x = 0; x < width * 2; ++x) twice[(size_t{y} * width * 2) + x] = values[(size_t{y / 2} * width) + (x / 2)];
    }
    return twice;
  };
  // One of the copies drawn into `target` with `viewport`, its inputs at t0 and t1, its constants at b0.
  const auto draw_copy = [&](ID3D11PixelShader* shader, ID3D11VertexShader* vertex_shader, const Map& target, float viewport_size, ID3D11ShaderResourceView* first,
                             ID3D11ShaderResourceView* second, ID3D11Buffer* shader_constants, ID3D11Buffer* injection = nullptr) {
    immediate->ClearState();
    immediate->ClearDepthStencilView(target.depth.Get(), D3D11_CLEAR_DEPTH, 0.25f, 0);
    const D3D11_VIEWPORT viewport = {.TopLeftX = 0.f, .TopLeftY = 0.f, .Width = viewport_size, .Height = viewport_size, .MinDepth = 0.f, .MaxDepth = 1.f};
    immediate->RSSetViewports(1, &viewport);
    immediate->OMSetRenderTargets(0, nullptr, target.depth.Get());
    immediate->OMSetDepthStencilState(always.Get(), 0);
    const std::array<float, 8> whole = {-1.f, 1.f, 1.f, -1.f, 0.f};
    immediate->UpdateSubresource(area_buffer.Get(), 0, nullptr, whole.data(), 0, 0);
    immediate->VSSetShader(vertex_shader, nullptr, 0);
    immediate->VSSetConstantBuffers(0, 1, area_buffer.GetAddressOf());
    immediate->PSSetShader(shader, nullptr, 0);
    const std::array<ID3D11ShaderResourceView*, 2> inputs = {first, second};
    immediate->PSSetShaderResources(0, 2, inputs.data());
    immediate->PSSetConstantBuffers(0, 1, &shader_constants);
    if (injection != nullptr) immediate->PSSetConstantBuffers(10, 1, &injection);
    immediate->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    immediate->Draw(3, 0);
    immediate->ClearState();
    return ReadSlice(target.texture.Get(), 0);
  };
  const D3D11_BUFFER_DESC copy_desc = {.ByteWidth = 64, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  ComPtr<ID3D11Buffer> copy_constants;
  device->CreateBuffer(&copy_desc, nullptr, copy_constants.GetAddressOf());
  const std::array<uint32_t, 16> slice_zero = {};
  // The two half-size copies.
  for (const size_t index : {size_t{1}, size_t{2}}) {
    immediate->UpdateSubresource(copy_constants.Get(), 0, nullptr, slice_zero.data(), 0, 0);
    // 1x, the game's bytes against ours: sources 512, target 256 (800F7F7F's ratio) and target 128 under a 256 viewport (D5DCC85F's).
    const auto small_a = random_map(512);
    const auto small_b = random_map(512);
    const Map source_a = filled(512, small_a);
    const Map source_b = filled(512, small_b);
    const Map target = MakeMap((index == 1) ? 256 : 128, 1, false);
    const auto by_game = draw_copy(game_shaders[index].Get(), triangle.Get(), target, 256.f, source_a.view.Get(), source_b.view.Get(), copy_constants.Get());
    const auto by_ours = draw_copy(our_shaders[index].Get(), triangle.Get(), target, 256.f, source_a.view.Get(), source_b.view.Get(), copy_constants.Get());
    const bool same = std::memcmp(by_game.data(), by_ours.data(), by_game.size() * 4) == 0;
    std::printf("%s at 1x: ours against the game's bytes: %s\n", hashes[index], same ? "the same to the bit" : "DIFFERENT");
    CHECK(same);
    // 2x: the map 4096 and its doubled 8192; the game's bytes on the 4096 against ours on the 8192, the game's targets and viewports.
    const auto game_values = random_map(GAME);
    const Map game_source = filled(GAME, game_values);
    const Map larger_source = filled(LARGER, doubled(GAME, game_values));
    const Map game_target = MakeMap((index == 1) ? 2048 : 1024, 1, false);
    const auto at_game = draw_copy(game_shaders[index].Get(), triangle.Get(), game_target, 2048.f, game_source.view.Get(), game_source.view.Get(), copy_constants.Get());
    const auto at_larger = draw_copy(our_shaders[index].Get(), triangle.Get(), game_target, 2048.f, larger_source.view.Get(), larger_source.view.Get(), copy_constants.Get());
    const bool agree = std::memcmp(at_game.data(), at_larger.data(), at_game.size() * 4) == 0;
    std::printf("%s at 2x: ours on the 8192 map against the game's on the 4096 one: %s\n", hashes[index], agree ? "the same to the bit" : "DIFFERENT");
    CHECK(agree);
  }
  // The kept maps' copy: a map moved by whole texels of the game's size, the constants as the run printed them (buffer_size 4096).
  {
    const auto copy_constants_of = [](float moved_texels_x, float moved_texels_y, float buffer_size) {
      // current: the whole map; previous: the same square moved (clip units: 2 a map).
      const float dx = moved_texels_x * 2.f / static_cast<float>(size::GAME_SIZE);
      const float dy = moved_texels_y * 2.f / static_cast<float>(size::GAME_SIZE);
      return std::array<float, 16>{-1.f, 1.f, 1.f, -1.f, -1.f + dx, 1.f + dy, 1.f + dx, -1.f + dy, 2.f, -2.f, buffer_size, buffer_size, 0.f, 0.f, 0.f, 0.f};
    };
    const auto game_values = random_map(GAME);
    const Map game_source = filled(GAME, game_values);
    const Map larger_source = filled(LARGER, doubled(GAME, game_values));
    const Map game_target = MakeMap(GAME, 1, false);
    uint32_t apart = 0;
    for (const auto& [moved_x, moved_y] : {std::pair{0.f, 0.f}, std::pair{37.f, -112.f}, std::pair{-900.f, 5.f}}) {
      const auto constants = copy_constants_of(moved_x, moved_y, static_cast<float>(GAME));
      immediate->UpdateSubresource(copy_constants.Get(), 0, nullptr, constants.data(), 0, 0);
      const auto by_game = draw_copy(game_shaders[0].Get(), triangle_uv.Get(), game_target, static_cast<float>(GAME), game_source.view.Get(), nullptr, copy_constants.Get());
      const auto by_ours = draw_copy(our_shaders[0].Get(), triangle_uv.Get(), game_target, static_cast<float>(GAME), game_source.view.Get(), nullptr, copy_constants.Get());
      const bool same = std::memcmp(by_game.data(), by_ours.data(), by_game.size() * 4) == 0;
      std::printf("0xC883E3FA at 1x, moved %g, %g texels: ours against the game's bytes: %s\n", moved_x, moved_y, same ? "the same to the bit" : "DIFFERENT");
      CHECK(same);
      // 2x: into the larger map with the larger viewport, the game's constants unchanged (buffer_size still 4096).
      const auto at_larger = draw_copy(our_shaders[0].Get(), triangle_uv.Get(), dynamic, static_cast<float>(LARGER), larger_source.view.Get(), nullptr, copy_constants.Get());
      for (uint32_t y = 0; y < LARGER; ++y) {
        for (uint32_t x = 0; x < LARGER; ++x) {
          apart += (at_larger[(size_t{y} * LARGER) + x] == by_game[(size_t{y / 2} * GAME) + (x / 2)]) ? 0u : 1u;
        }
      }
    }
    std::printf("0xC883E3FA at 2x: ours on the 8192 maps against the game's 4096 result doubled: %u texels apart\n", apart);
    CHECK(apart == 0);
    // A map of the game's own making whose buffer_size is not its size (a lower Shadows setting, should the game do that):
    // the game's own math, whatever it reads.
    const Map small_source = filled(512, random_map(512));
    const Map small_target = MakeMap(512, 1, false);
    for (const float buffer_size : {256.f, 1024.f}) {
      const auto constants = copy_constants_of(37.f, -112.f, buffer_size);
      immediate->UpdateSubresource(copy_constants.Get(), 0, nullptr, constants.data(), 0, 0);
      const auto by_game = draw_copy(game_shaders[0].Get(), triangle_uv.Get(), small_target, 512.f, small_source.view.Get(), nullptr, copy_constants.Get());
      const auto by_ours = draw_copy(our_shaders[0].Get(), triangle_uv.Get(), small_target, 512.f, small_source.view.Get(), nullptr, copy_constants.Get());
      const bool same = std::memcmp(by_game.data(), by_ours.data(), by_game.size() * 4) == 0;
      std::printf("0xC883E3FA on a 512 map with buffer_size %g: ours against the game's bytes: %s\n", buffer_size, same ? "the same to the bit" : "DIFFERENT");
      CHECK(same);
    }
  }
  CHECK(std::ranges::count_if(log_lines, [](const std::string& line) { return line.find("Shadow Map Size: the game's shadow maps made at 8192") != std::string::npos; }) >= 1);

  // 6.
  {
    const auto injection_of = [](float scale) {
      ShaderInjectData data = {};
      data.shadow_map_scale = scale;
      const D3D11_BUFFER_DESC desc = {.ByteWidth = (sizeof(ShaderInjectData) + 15u) & ~15u, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      std::vector<uint8_t> bytes(desc.ByteWidth);
      std::memcpy(bytes.data(), &data, sizeof(data));
      const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = bytes.data()};
      ComPtr<ID3D11Buffer> buffer;
      device->CreateBuffer(&desc, &initial, buffer.GetAddressOf());
      return buffer;
    };
    // The deepest of each `factor` x `factor` block.
    const auto reduced = [](uint32_t width, const std::vector<float>& values, uint32_t factor) {
      const uint32_t out = width / factor;
      std::vector<float> result(size_t{out} * out, 0.f);
      for (uint32_t y = 0; y < width; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
          float& deepest = result[(size_t{y / factor} * out) + (x / factor)];
          deepest = std::max(deepest, values[(size_t{y} * width) + x]);
        }
      }
      return result;
    };
    for (const uint32_t factor : {2u, 4u}) {
      const uint32_t made = GAME / factor;
      const float scale = 1.f / static_cast<float>(factor);
      // Made at `made`; the game's own map of that size is not tracked.
      size::made_size = made;
      const uint32_t live_before = size::live_maps;
      Map own = MakeMap(made, 4, true);
      CHECK(own.texture && WidthOf(own) == made && size::live_maps == live_before);
      Map made_map = MakeMap(GAME, 4, true);
      CHECK(made_map.texture && WidthOf(made_map) == made && size::live_maps == live_before + 1);
      CHECK(size::Scale() == scale);
      GameSetsViewport(immediate.Get(), static_cast<float>(GAME));
      GameSetsScissor(immediate.Get(), static_cast<LONG>(GAME));
      GameBindsDepth(immediate.Get(), made_map.depth.Get());
      CHECK(ViewportOf(immediate.Get()) == made && ScissorOf(immediate.Get()) == static_cast<LONG>(made));
      GameBindsDepth(immediate.Get(), other.depth.Get());
      CHECK(ViewportOf(immediate.Get()) == GAME && ScissorOf(immediate.Get()) == static_cast<LONG>(GAME));
      GameBindsDepth(immediate.Get(), own.depth.Get());
      CHECK(ViewportOf(immediate.Get()) == GAME);
      immediate->ClearState();
      {
        ComPtr<ID3D11Buffer> constants_buffer = make_constants(D3D11_USAGE_DYNAMIC, BYTES);
        upload(constants_buffer.Get(), game_constants(static_cast<float>(GAME)));
        CHECK(read_back(constants_buffer.Get()) == game_constants(static_cast<float>(made)));
      }
      const auto injection = injection_of(scale);
      // The half copies: the game's bytes on a 4096 map into its own targets against ours on the reduced map.
      for (const size_t index : {size_t{1}, size_t{2}}) {
        immediate->UpdateSubresource(copy_constants.Get(), 0, nullptr, slice_zero.data(), 0, 0);
        const auto game_values = random_map(GAME);
        const Map game_source = filled(GAME, game_values);
        const Map small_source = filled(made, reduced(GAME, game_values, factor));
        const uint32_t target_size = (index == 1) ? 2048u : 1024u;
        const Map game_target = MakeMap(target_size, 1, false);
        const auto at_game = draw_copy(game_shaders[index].Get(), triangle.Get(), game_target, 2048.f, game_source.view.Get(), game_source.view.Get(), copy_constants.Get());
        const auto at_small = draw_copy(our_shaders[index].Get(), triangle.Get(), game_target, 2048.f, small_source.view.Get(), small_source.view.Get(), copy_constants.Get(),
                                        injection.Get());
        // At 0.5 the same; at 0.25 the 4 x 4 deepest under the game's 2 x 2 (never shallower).
        const auto expected = (factor == 2) ? at_game : [&] {
          const auto blocks = reduced(GAME, game_values, 4);
          std::vector<float> result(size_t{target_size} * target_size);
          for (uint32_t y = 0; y < target_size; ++y) {
            for (uint32_t x = 0; x < target_size; ++x) result[(size_t{y} * target_size) + x] = blocks[(size_t{y / 2} * (GAME / 4)) + (x / 2)];
          }
          return result;
        }();
        uint32_t apart = 0;
        uint32_t shallower = 0;
        for (size_t texel = 0; texel < at_small.size(); ++texel) {
          apart += (at_small[texel] == expected[texel]) ? 0u : 1u;
          shallower += (at_small[texel] < at_game[texel]) ? 1u : 0u;
        }
        std::printf("%s at 1/%u: ours on the %u map against the game's on the 4096 one: %u texels apart, %u shallower\n", hashes[index], factor, made, apart, shallower);
        CHECK(apart == 0 && shallower == 0);
      }
      // The kept maps' copy: moved by whole texels of the made size; ours against the game's result reduced.
      {
        const auto game_values = random_map(GAME);
        const Map game_source = filled(GAME, game_values);
        const Map small_source = filled(made, reduced(GAME, game_values, factor));
        const Map game_target = MakeMap(GAME, 1, false);
        const Map small_target = MakeMap(made, 1, false);
        uint32_t apart = 0;
        for (const auto& [moved_x, moved_y] : {std::pair{0.f, 0.f}, std::pair{40.f, -112.f}, std::pair{-904.f, 8.f}}) {
          const float dx = moved_x * 2.f / static_cast<float>(GAME);
          const float dy = moved_y * 2.f / static_cast<float>(GAME);
          const std::array<float, 16> constants = {-1.f, 1.f, 1.f, -1.f, -1.f + dx, 1.f + dy, 1.f + dx, -1.f + dy, 2.f, -2.f, static_cast<float>(GAME), static_cast<float>(GAME)};
          immediate->UpdateSubresource(copy_constants.Get(), 0, nullptr, constants.data(), 0, 0);
          const auto by_game = draw_copy(game_shaders[0].Get(), triangle_uv.Get(), game_target, static_cast<float>(GAME), game_source.view.Get(), nullptr, copy_constants.Get());
          const auto by_small = draw_copy(our_shaders[0].Get(), triangle_uv.Get(), small_target, static_cast<float>(made), small_source.view.Get(), nullptr, copy_constants.Get(),
                                          injection.Get());
          // A copy's discarded texels keep the target's clear (0.25) in both.
          const auto expected = reduced(GAME, by_game, factor);
          for (size_t texel = 0; texel < by_small.size(); ++texel) apart += (by_small[texel] == expected[texel]) ? 0u : 1u;
        }
        std::printf("0xC883E3FA at 1/%u: ours on the %u maps against the game's 4096 result reduced: %u texels apart\n", factor, made, apart);
        CHECK(apart == 0);
      }
      size::OnDestroyResource(nullptr, {reinterpret_cast<uint64_t>(made_map.texture.Get())});
    }
    size::made_size = LARGER;
  }
  std::printf("PASS\n");
  return 0;
}
