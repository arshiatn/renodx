// Ryse's sun shadow mask passes (sunshadow00-04) on WARP with a cascade map that got no casters (Skip the Game's Sun
// Shadow Maps, rysesonofrome/shadow_skip.hpp): the game's own bytes and the add-on's built replacements (Sun Shadow
// Softness off and on: PCSS) must write no shadow (mask 0) into a map cleared to 1 as the game clears it, D16 like the
// game's (R16_TYPELESS). Control: the same map with a caster nearer than the receivers must write full shadow (1), and
// the built shaders must match the game's in both. Arguments: the repository (its build's embedded csos) and the folder of
// the game's dumped csos (bin\cmd_Decompiler-1.3.16\dump_shader_ryse); both only read.
#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <vector>

#include "src/games/rysesonofrome/shared.h"

using Microsoft::WRL::ComPtr;

namespace {

int failures = 0;

#define CHECK(condition)                                       \
  do {                                                         \
    if (!(condition)) {                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #condition); \
      ++failures;                                              \
    }                                                          \
  } while (false)

std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// The mask passes' inputs: TEXCOORD0 the rotation texture's (0: one texel), TEXCOORD1 the screen's uv, TEXCOORD4 (01 / 03
// declare it) unused here.
constexpr char VERTEX_SHADER[] = R"(
struct Output {
  float4 position : SV_Position;
  float4 rotation : TEXCOORD0;
  float4 uv : TEXCOORD1;
  float3 ray : TEXCOORD4;
};
Output main(uint id : SV_VertexID) {
  const float2 uv = float2((id << 1) & 2, id & 2);
  Output output;
  output.position = float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.f, 1.f);
  output.rotation = 0.f;
  output.uv = float4(uv, 0.f, 0.f);
  output.ray = 0.f;
  return output;
}
)";

constexpr UINT WIDTH = 64;
constexpr UINT HEIGHT = 64;
constexpr UINT MAP_SIZE = 256;
constexpr float RECEIVER_DEPTH = 0.5f;

// PER_BATCH (b0) of all five, as the decompiles name it.
struct PerBatch {
  std::array<float, 4> basis_magnitudes;
  std::array<float, 4> basis_y;
  std::array<float, 4> adaption;
  std::array<float, 4> basis_x;
  std::array<float, 4> blend_info;
  std::array<std::array<float, 4>, 8> kernel;
  std::array<float, 4> near_far;
  std::array<std::array<float, 4>, 4> tex_gen;
  std::array<float, 4> depth_shift;
  std::array<float, 4> fading_distance;
  std::array<float, 4> camera;
  std::array<float, 4> one_over_far;
  std::array<float, 4> basis_z;
  std::array<float, 4> depth_bias;
  std::array<float, 4> inverse_map_size;
  std::array<float, 4> kernel_radius;
};

ComPtr<ID3D11ShaderResourceView> Texture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, const void* data, UINT row_bytes) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width,
                                     .Height = height,
                                     .MipLevels = 1,
                                     .ArraySize = 1,
                                     .Format = format,
                                     .SampleDesc = {.Count = 1},
                                     .Usage = D3D11_USAGE_IMMUTABLE,
                                     .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data, .SysMemPitch = row_bytes};
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> view;
  if (FAILED(device->CreateTexture2D(&desc, &initial, texture.GetAddressOf()))
      || FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()))) {
    std::printf("texture %ux%u f%d could not be made\n", width, height, static_cast<int>(format));
    std::exit(2);
  }
  return view;
}

ComPtr<ID3D11Buffer> Constants(ID3D11Device* device, const void* data, UINT bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = (bytes + 15u) & ~15u, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  std::vector<uint8_t> padded(desc.ByteWidth, 0);
  std::memcpy(padded.data(), data, bytes);
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = padded.data()};
  ComPtr<ID3D11Buffer> buffer;
  if (FAILED(device->CreateBuffer(&desc, &initial, buffer.GetAddressOf()))) std::exit(2);
  return buffer;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: ryse_shadow_skip.exe <repository> <the game's dumped shader folder>\n");
    return 2;
  }
  const std::filesystem::path repository = argv[1];
  const std::filesystem::path dumps = argv[2];

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::printf("no WARP device\n");
    return 2;
  }
  ComPtr<ID3DBlob> vertex_code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(VERTEX_SHADER, sizeof(VERTEX_SHADER) - 1, "vs", nullptr, nullptr, "main", "vs_5_0", 0, 0, vertex_code.GetAddressOf(),
                        errors.GetAddressOf()))) {
    std::printf("vs: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return 2;
  }
  ComPtr<ID3D11VertexShader> vertex_shader;
  device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex_shader.GetAddressOf());

  // The cascade map as the game's: R16_TYPELESS, a D16 depth view, an R16_UNORM shader view.
  const D3D11_TEXTURE2D_DESC map_desc = {.Width = MAP_SIZE,
                                         .Height = MAP_SIZE,
                                         .MipLevels = 1,
                                         .ArraySize = 1,
                                         .Format = DXGI_FORMAT_R16_TYPELESS,
                                         .SampleDesc = {.Count = 1},
                                         .BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE};
  ComPtr<ID3D11Texture2D> map;
  ComPtr<ID3D11DepthStencilView> map_depth;
  ComPtr<ID3D11ShaderResourceView> map_view;
  const D3D11_DEPTH_STENCIL_VIEW_DESC depth_desc = {.Format = DXGI_FORMAT_D16_UNORM, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D};
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_R16_UNORM, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D};
  view_desc.Texture2D.MipLevels = 1;
  if (FAILED(device->CreateTexture2D(&map_desc, nullptr, map.GetAddressOf()))
      || FAILED(device->CreateDepthStencilView(map.Get(), &depth_desc, map_depth.GetAddressOf()))
      || FAILED(device->CreateShaderResourceView(map.Get(), &view_desc, map_view.GetAddressOf()))) {
    std::printf("the map could not be made\n");
    return 2;
  }

  // The scene's depth 1 everywhere (its basis below puts every pixel at the receiver depth), one rotation texel, the
  // edge mask 1 (the full kernel: the costliest path).
  const std::vector<float> scene_depth(WIDTH * HEIGHT, 1.f);
  const std::array<uint8_t, 4> rotation = {255, 128, 128, 255};
  const std::vector<float> edges(WIDTH * HEIGHT, 1.f);
  const auto scene_view = Texture(device.Get(), WIDTH, HEIGHT, DXGI_FORMAT_R32_FLOAT, scene_depth.data(), WIDTH * 4);
  const auto rotation_view = Texture(device.Get(), 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, rotation.data(), 4);
  const auto edge_view = Texture(device.Get(), WIDTH, HEIGHT, DXGI_FORMAT_R32_FLOAT, edges.data(), WIDTH * 4);

  // The pass's basis: map uv = the pixel / the screen, depth RECEIVER_DEPTH; a small rotated kernel; no fade, no blend.
  PerBatch per_batch = {};
  per_batch.basis_magnitudes = {1.f / WIDTH, 1.f / HEIGHT, 1.f, 0.f};
  per_batch.basis_x = {1.f, 0.f, 0.f, 0.f};
  per_batch.basis_y = {0.f, 1.f, 0.f, 0.f};
  per_batch.basis_z = {0.f, 0.f, RECEIVER_DEPTH, 0.f};
  per_batch.camera = {0.f, 0.f, 0.f, 1.f};
  for (size_t index = 0; index < per_batch.kernel.size(); ++index) {
    const float angle = static_cast<float>(index) * 0.785f;
    per_batch.kernel[index] = {std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle)};
  }
  per_batch.inverse_map_size = {1.f / MAP_SIZE, 1.f / MAP_SIZE, 0.f, 0.f};
  per_batch.kernel_radius = {2.f, 2.f, 0.f, 0.f};
  const auto batch_buffer = Constants(device.Get(), &per_batch, sizeof(per_batch));

  const D3D11_SAMPLER_DESC point_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
                                         .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
                                         .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
                                         .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
                                         .MaxLOD = D3D11_FLOAT32_MAX};
  const D3D11_SAMPLER_DESC comparison_desc = {.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
                                              .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                              .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                              .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                              .ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL,
                                              .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> point_sampler;
  ComPtr<ID3D11SamplerState> comparison_sampler;
  device->CreateSamplerState(&point_desc, point_sampler.GetAddressOf());
  device->CreateSamplerState(&comparison_desc, comparison_sampler.GetAddressOf());

  const D3D11_TEXTURE2D_DESC target_desc = {.Width = WIDTH,
                                            .Height = HEIGHT,
                                            .MipLevels = 1,
                                            .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                            .SampleDesc = {.Count = 1},
                                            .BindFlags = D3D11_BIND_RENDER_TARGET};
  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11Texture2D> staging;
  ComPtr<ID3D11RenderTargetView> target_view;
  device->CreateTexture2D(&target_desc, nullptr, target.GetAddressOf());
  device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf());
  device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf());

  // The map as the frame leaves it: cleared to 1 (the game's clear), then, with a caster, cleared to `caster` (a caster
  // over the whole map, nearer than every receiver).
  const auto prepare_map = [&](float depth) {
    context->ClearState();
    context->ClearDepthStencilView(map_depth.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
    if (depth < 1.f) context->ClearDepthStencilView(map_depth.Get(), D3D11_CLEAR_DEPTH, depth, 0);
  };
  // One mask pass of `shader` with Sun Shadow Softness `softness` (the game's shader ignores b13): the least and most mask
  // value written (1 = shadow).
  const auto draw = [&](ID3D11PixelShader* shader, float softness) {
    ShaderInjectData injection = {};
    injection.sun_shadow_softness = softness;
    const auto injection_buffer = Constants(device.Get(), &injection, sizeof(injection));
    std::array<ID3D11ShaderResourceView*, 6> views = {scene_view.Get(), rotation_view.Get(), map_view.Get(), nullptr, nullptr, edge_view.Get()};
    std::array<ID3D11SamplerState*, 6> samplers = {point_sampler.Get(), comparison_sampler.Get(), point_sampler.Get(),
                                                   point_sampler.Get(), point_sampler.Get(), point_sampler.Get()};
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->PSSetShader(shader, nullptr, 0);
    context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    context->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
    ID3D11Buffer* batch = batch_buffer.Get();
    ID3D11Buffer* settings = injection_buffer.Get();
    context->PSSetConstantBuffers(0, 1, &batch);
    context->PSSetConstantBuffers(13, 1, &settings);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(WIDTH), .Height = static_cast<float>(HEIGHT), .MaxDepth = 1.f};
    context->RSSetViewports(1, &viewport);
    const std::array<float, 4> clear = {-1.f, -1.f, -1.f, -1.f};
    context->ClearRenderTargetView(target_view.Get(), clear.data());
    ID3D11RenderTargetView* targets[] = {target_view.Get()};
    context->OMSetRenderTargets(1, targets, nullptr);
    context->Draw(3, 0);
    context->CopyResource(staging.Get(), target.Get());
    std::array<float, 2> range = {1e9f, -1e9f};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
      for (UINT y = 0; y < HEIGHT; ++y) {
        const auto* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + (y * mapped.RowPitch));
        for (UINT x = 0; x < WIDTH; ++x) {
          range[0] = std::min(range[0], row[x * 4]);
          range[1] = std::max(range[1], row[x * 4]);
        }
      }
      context->Unmap(staging.Get(), 0);
    }
    return range;
  };

  constexpr std::array<std::pair<uint32_t, const char*>, 5> PASSES = {{
      {0xE9264ADE, "sunshadow00 (near cascades, character map)"},
      {0xB5DC6E36, "sunshadow01 (near cascades)"},
      {0xF2B4EF30, "sunshadow02 (middle cascade)"},
      {0xF864A048, "sunshadow03 (middle cascade)"},
      {0xF81C7168, "sunshadow04 (kept far map)"},
  }};
  for (const auto& [hash, name] : PASSES) {
    const auto game = ReadFile(dumps / std::format("0x{:08X}.ps_5_0.cso", hash));
    const auto built = ReadFile(repository / std::format("build/rysesonofrome.include/embed/0x{:08X}.cso", hash));
    ComPtr<ID3D11PixelShader> game_shader;
    ComPtr<ID3D11PixelShader> built_shader;
    if (game.empty() || built.empty() || FAILED(device->CreatePixelShader(game.data(), game.size(), nullptr, game_shader.GetAddressOf()))
        || FAILED(device->CreatePixelShader(built.data(), built.size(), nullptr, built_shader.GetAddressOf()))) {
      std::printf("0x%08X: missing or not made (game %zu bytes, built %zu bytes)\n", hash, game.size(), built.size());
      ++failures;
      continue;
    }
    for (const float caster : {1.f, 0.2f}) {
      prepare_map(caster);
      const auto game_range = draw(game_shader.Get(), 0.f);
      const auto built_range = draw(built_shader.Get(), 0.f);
      const auto soft_range = draw(built_shader.Get(), 1.5f);
      std::printf("0x%08X %s, %s: mask game %.4f..%.4f, built %.4f..%.4f, built with Softness 1.5 (PCSS) %.4f..%.4f\n", hash, name,
                  (caster == 1.f ? "no casters (skipped)" : "control, caster at 0.2"), game_range[0], game_range[1], built_range[0],
                  built_range[1], soft_range[0], soft_range[1]);
      const float expected = (caster == 1.f) ? 0.f : 1.f;
      for (const auto& range : {game_range, built_range, soft_range}) {
        CHECK(std::abs(range[0] - expected) <= 1e-4f && std::abs(range[1] - expected) <= 1e-4f);
      }
    }
  }
  std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
