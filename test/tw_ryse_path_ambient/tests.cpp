// Ryse's Path-Traced Ambient (rysesonofrome/ray_tracing.hpp) on WARP.
// 1. The tiled lighting (the game's cs 0x897232B5 and the add-on's raised one, tiled_lights_cs.h) with only probe lights
//    (lightType 1) and the probes' reflections black (t33 cubes, t6 reflections): with the game's diffuse probes (t34) the
//    scene is lit, with the add-on's black cube (one layer) at t34 it is black, u1 and u2 alike: the diffuse probes are
//    the lighting's only ambient, the black cube takes all of it. Control: with the probes' reflections back, the black
//    cube keeps them (the scene not black) and differs from the game's.
// 2. rt_probe_sky (the add-on's build): the widest probe before the sun (later entries are older frames') at the six axes
//    times its colour and attenuation into the sun texture's texels 3-8, w 1; no light list: zeros.
// Arguments: the repository (its build's embedded csos) and the game's dumped 0x897232B5.cs_5_0.cso; both only read.
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "src/games/rysesonofrome/tiled_lights_cs.h"

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

constexpr UINT EDGE = 64;
constexpr UINT TILES = (EDGE / 8) * (EDGE / 8);
constexpr UINT LIGHTS = 512;

ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
uint32_t seed = 12345;

float Random() {
  seed = seed * 1664525u + 1013904223u;
  return static_cast<float>(seed >> 8) / 16777216.f;
}

std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// A float texture (cube array when `cube`: slices / 6 cubes) of random texels in 0.01-0.99, or of `value` when given.
ComPtr<ID3D11ShaderResourceView> Texture(UINT width, UINT height, UINT slices, bool cube, DXGI_FORMAT format, UINT channels, float value = -1.f) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width,
                                     .Height = height,
                                     .MipLevels = 1,
                                     .ArraySize = slices,
                                     .Format = format,
                                     .SampleDesc = {.Count = 1},
                                     .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                     .MiscFlags = cube ? UINT{D3D11_RESOURCE_MISC_TEXTURECUBE} : 0u};
  std::vector<float> texels(static_cast<size_t>(width) * height * slices * channels);
  for (auto& texel : texels) texel = (value >= 0.f) ? value : (0.01f + (0.98f * Random()));
  std::vector<D3D11_SUBRESOURCE_DATA> data(slices);
  for (UINT slice = 0; slice < slices; ++slice) {
    data[slice] = {.pSysMem = texels.data() + (static_cast<size_t>(slice) * width * height * channels), .SysMemPitch = width * channels * 4u};
  }
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, data.data(), texture.GetAddressOf());
  D3D11_SHADER_RESOURCE_VIEW_DESC view = {.Format = format};
  if (cube) {
    view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
    view.TextureCubeArray = {.MostDetailedMip = 0, .MipLevels = 1, .First2DArrayFace = 0, .NumCubes = slices / 6};
  } else if (slices > 1) {
    view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    view.Texture2DArray = {.MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = 0, .ArraySize = slices};
  } else {
    view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D = {.MostDetailedMip = 0, .MipLevels = 1};
  }
  ComPtr<ID3D11ShaderResourceView> srv;
  device->CreateShaderResourceView(texture.Get(), &view, srv.GetAddressOf());
  return srv;
}

// The add-on's black cube as ray_tracing.hpp makes it (RGBA16F, 1 x 1, one cube).
ComPtr<ID3D11ShaderResourceView> BlackCube() {
  const std::array<uint64_t, 6> black = {};
  std::array<D3D11_SUBRESOURCE_DATA, 6> faces = {};
  for (auto& face : faces) face = {.pSysMem = &black[0], .SysMemPitch = 8};
  const D3D11_TEXTURE2D_DESC desc = {.Width = 1,
                                     .Height = 1,
                                     .MipLevels = 1,
                                     .ArraySize = 6,
                                     .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
                                     .SampleDesc = {.Count = 1},
                                     .Usage = D3D11_USAGE_IMMUTABLE,
                                     .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                     .MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE};
  D3D11_SHADER_RESOURCE_VIEW_DESC view = {.Format = DXGI_FORMAT_R16G16B16A16_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY};
  view.TextureCubeArray = {.MostDetailedMip = 0, .MipLevels = 1, .First2DArrayFace = 0, .NumCubes = 1};
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
  device->CreateTexture2D(&desc, faces.data(), texture.GetAddressOf());
  device->CreateShaderResourceView(texture.Get(), &view, srv.GetAddressOf());
  return srv;
}

ComPtr<ID3D11ShaderResourceView> Structured(const std::vector<uint8_t>& bytes, UINT stride) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>(bytes.size()),
                                  .Usage = D3D11_USAGE_DEFAULT,
                                  .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                  .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                                  .StructureByteStride = stride};
  const D3D11_SUBRESOURCE_DATA data = {.pSysMem = bytes.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &data, buffer.GetAddressOf());
  D3D11_SHADER_RESOURCE_VIEW_DESC view = {.Format = DXGI_FORMAT_UNKNOWN, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
  view.Buffer = {.FirstElement = 0, .NumElements = static_cast<UINT>(bytes.size() / stride)};
  ComPtr<ID3D11ShaderResourceView> srv;
  device->CreateShaderResourceView(buffer.Get(), &view, srv.GetAddressOf());
  return srv;
}

// The tiled lighting's lights: `probes` probe lights (lightType 1, layers 0 / 1) inside every tile, the rest empty.
struct Lights {
  std::vector<uint8_t> cull = std::vector<uint8_t>(LIGHTS * 80, 0);
  std::vector<uint8_t> shade = std::vector<uint8_t>(LIGHTS * 256, 0);
};

Lights Probes(UINT probes) {
  Lights lights;
  for (UINT index = 0; index < probes; ++index) {
    uint8_t* cull = lights.cull.data() + (index * 80);
    const uint32_t volume_type = 1;
    const std::array<float, 2> bounds = {0.f, 1.f};
    const std::array<float, 4> pos_rad = {Random(), Random(), Random(), 1e6f};
    std::memcpy(cull, &volume_type, 4);
    std::memcpy(cull + 8, bounds.data(), 8);
    std::memcpy(cull + 16, pos_rad.data(), 16);
    std::array<float, 64> values = {};
    for (auto& value : values) value = 0.05f + (0.95f * Random());
    uint8_t* shade = lights.shade.data() + (index * 256);
    std::memcpy(shade, values.data(), 256);
    const std::array<uint32_t, 3> header = {1u, index % 2u, 0u};
    std::memcpy(shade, header.data(), 12);
  }
  return lights;
}

struct Lit {
  std::vector<float> scene;
  std::vector<float> diffuse;
};

// One dispatch of `shader`: t0-t8 `textures`, t32 the shading list, t33 `specular`, t34 `diffuse`, t35 spots, t38 the culling.
Lit Run(ID3D11ComputeShader* shader, const Lights& lights, const std::array<ComPtr<ID3D11ShaderResourceView>, 9>& textures,
        ID3D11ShaderResourceView* specular, ID3D11ShaderResourceView* diffuse, ID3D11ShaderResourceView* spots, ID3D11Buffer* constants,
        ID3D11SamplerState* sampler) {
  const auto shade = Structured(lights.shade, 256);
  const auto cull = Structured(lights.cull, 80);
  const D3D11_BUFFER_DESC list_desc = {.ByteWidth = TILES * 255, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS};
  ComPtr<ID3D11Buffer> list;
  device->CreateBuffer(&list_desc, nullptr, list.GetAddressOf());
  D3D11_UNORDERED_ACCESS_VIEW_DESC list_view = {.Format = DXGI_FORMAT_R8_UINT, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER};
  list_view.Buffer = {.FirstElement = 0, .NumElements = TILES * 255};
  std::array<ComPtr<ID3D11UnorderedAccessView>, 3> uavs;
  device->CreateUnorderedAccessView(list.Get(), &list_view, uavs[0].GetAddressOf());
  const D3D11_TEXTURE2D_DESC target_desc = {.Width = EDGE,
                                            .Height = EDGE,
                                            .MipLevels = 1,
                                            .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                            .SampleDesc = {.Count = 1},
                                            .BindFlags = D3D11_BIND_UNORDERED_ACCESS};
  std::array<ComPtr<ID3D11Texture2D>, 2> targets;
  for (size_t index = 0; index < targets.size(); ++index) {
    device->CreateTexture2D(&target_desc, nullptr, targets[index].GetAddressOf());
    device->CreateUnorderedAccessView(targets[index].Get(), nullptr, uavs[1 + index].GetAddressOf());
  }
  context->ClearState();
  context->CSSetShader(shader, nullptr, 0);
  std::array<ID3D11ShaderResourceView*, 9> low = {};
  for (size_t slot = 0; slot < low.size(); ++slot) low[slot] = textures[slot].Get();
  context->CSSetShaderResources(0, static_cast<UINT>(low.size()), low.data());
  std::array<ID3D11ShaderResourceView*, 7> high = {shade.Get(), specular, diffuse, spots, nullptr, nullptr, cull.Get()};
  context->CSSetShaderResources(32, static_cast<UINT>(high.size()), high.data());
  std::array<ID3D11UnorderedAccessView*, 3> bound = {uavs[0].Get(), uavs[1].Get(), uavs[2].Get()};
  context->CSSetUnorderedAccessViews(0, static_cast<UINT>(bound.size()), bound.data(), nullptr);
  context->CSSetConstantBuffers(0, 1, &constants);
  context->CSSetSamplers(15, 1, &sampler);
  context->Dispatch(EDGE / 8, EDGE / 8, 1);
  const auto read = [](ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
    context->CopyResource(staging.Get(), texture);
    std::vector<float> texels(static_cast<size_t>(desc.Width) * desc.Height * 4);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
      for (UINT row = 0; row < desc.Height; ++row) {
        std::memcpy(&texels[static_cast<size_t>(row) * desc.Width * 4], static_cast<const uint8_t*>(mapped.pData) + (row * mapped.RowPitch), desc.Width * 16);
      }
      context->Unmap(staging.Get(), 0);
    }
    return texels;
  };
  return {.scene = read(targets[0].Get()), .diffuse = read(targets[1].Get())};
}

// The colour channels' sum and largest magnitude (alpha left out: the lighting writes 0 there).
std::array<double, 2> Measure(const std::vector<float>& texels) {
  std::array<double, 2> measure = {0.0, 0.0};
  for (size_t index = 0; index < texels.size(); ++index) {
    if (index % 4 == 3) continue;
    measure[0] += texels[index];
    measure[1] = std::max(measure[1], static_cast<double>(std::abs(texels[index])));
  }
  return measure;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: ryse_path_ambient.exe <repository> <the game's 0x897232B5.cs_5_0.cso>\n");
    return 2;
  }
  const std::filesystem::path repository = argv[1];
  const auto game_code = ReadFile(argv[2]);
  const auto sky_code = ReadFile(repository / "build/rysesonofrome.include/embed/rt_probe_sky.cso");
  if (game_code.empty() || sky_code.empty()) {
    std::printf("missing: %s%s\n", game_code.empty() ? "the game's cso " : "", sky_code.empty() ? "the build's rt_probe_sky.cso" : "");
    return 2;
  }
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::printf("no WARP device\n");
    return 2;
  }
  ComPtr<ID3D11ComputeShader> game_lighting;
  ComPtr<ID3D11ComputeShader> raised_lighting;
  ComPtr<ID3D11ComputeShader> probe_sky;
  const auto& raised_code = rysesonofrome::tiled_lights_cs::CODE;
  if (FAILED(device->CreateComputeShader(game_code.data(), game_code.size(), nullptr, game_lighting.GetAddressOf()))
      || FAILED(device->CreateComputeShader(raised_code.data(), raised_code.size(), nullptr, raised_lighting.GetAddressOf()))
      || FAILED(device->CreateComputeShader(sky_code.data(), sky_code.size(), nullptr, probe_sky.GetAddressOf()))) {
    std::printf("shaders could not be made\n");
    return 2;
  }

  // 1. The lighting. t0 depth, t1-t3 the G-buffer, t4 the shadow mask (array), t5 the directional occlusion, t6 the
  // reflections (black: alpha 0, the probes' own), t7 / t8 the BRDF table and colour bleeding.
  const std::array<ComPtr<ID3D11ShaderResourceView>, 9> textures = {
      Texture(EDGE, EDGE, 1, false, DXGI_FORMAT_R32_FLOAT, 1),
      Texture(EDGE, EDGE, 1, false, DXGI_FORMAT_R32G32B32A32_FLOAT, 4),
      Texture(EDGE, EDGE, 1, false, DXGI_FORMAT_R32G32B32A32_FLOAT, 4),
      Texture(EDGE, EDGE, 1, false, DXGI_FORMAT_R32G32B32A32_FLOAT, 4),
      Texture(EDGE, EDGE, 4, false, DXGI_FORMAT_R32G32B32A32_FLOAT, 4),
      Texture(EDGE, EDGE, 1, false, DXGI_FORMAT_R32G32B32A32_FLOAT, 4),
      Texture(EDGE, EDGE, 1, false, DXGI_FORMAT_R32G32B32A32_FLOAT, 4, 0.f),
      Texture(EDGE, EDGE, 1, false, DXGI_FORMAT_R32G32B32A32_FLOAT, 4),
      Texture(EDGE, EDGE, 1, false, DXGI_FORMAT_R32G32B32A32_FLOAT, 4),
  };
  const auto specular_cubes = Texture(8, 8, 12, true, DXGI_FORMAT_R32G32B32A32_FLOAT, 4);
  const auto black_specular = Texture(8, 8, 12, true, DXGI_FORMAT_R32G32B32A32_FLOAT, 4, 0.f);
  const auto diffuse_cubes = Texture(8, 8, 12, true, DXGI_FORMAT_R32G32B32A32_FLOAT, 4);
  const auto black_cube = BlackCube();
  const auto spots = Texture(16, 16, 4, false, DXGI_FORMAT_R32G32B32A32_FLOAT, 4);
  std::array<float, 32> constants = {};
  for (auto& value : constants) value = (Random() * 2.f) - 1.f;
  constants[28] = EDGE;
  constants[29] = EDGE;
  constants[30] = 1.f / EDGE;
  constants[31] = 1.f / EDGE;
  const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = sizeof(constants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA constants_data = {.pSysMem = constants.data()};
  ComPtr<ID3D11Buffer> constants_buffer;
  device->CreateBuffer(&constants_desc, &constants_data, constants_buffer.GetAddressOf());
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
                                           .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .MaxAnisotropy = 1,
                                           .ComparisonFunc = D3D11_COMPARISON_NEVER,
                                           .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf());

  const Lights probes = Probes(6);
  for (const auto& [name, shader] : {std::pair{"game's", game_lighting.Get()}, std::pair{"raised", raised_lighting.Get()}}) {
    const Lit game_probes = Run(shader, probes, textures, black_specular.Get(), diffuse_cubes.Get(), spots.Get(), constants_buffer.Get(), sampler.Get());
    const Lit no_probes = Run(shader, probes, textures, black_specular.Get(), black_cube.Get(), spots.Get(), constants_buffer.Get(), sampler.Get());
    const auto lit = Measure(game_probes.scene);
    const auto lit_diffuse = Measure(game_probes.diffuse);
    const auto black = Measure(no_probes.scene);
    const auto black_diffuse = Measure(no_probes.diffuse);
    std::printf("%s lighting, probes only, their reflections black: scene sum %.4f (u2 %.4f) with the game's diffuse probes, "
                "largest %.3g (u2 %.3g) with the black cube\n",
                name, lit[0], lit_diffuse[0], black[1], black_diffuse[1]);
    CHECK(lit[0] > 1.0);
    CHECK(black[1] == 0.0 && black_diffuse[1] == 0.0);
    // Control: the probes' reflections back (t33): kept under the black cube, which still takes the diffuse.
    const Lit reflected = Run(shader, probes, textures, specular_cubes.Get(), diffuse_cubes.Get(), spots.Get(), constants_buffer.Get(), sampler.Get());
    const Lit reflected_black = Run(shader, probes, textures, specular_cubes.Get(), black_cube.Get(), spots.Get(), constants_buffer.Get(), sampler.Get());
    const auto with = Measure(reflected.scene);
    const auto without = Measure(reflected_black.scene);
    std::printf("  control, the probes' reflections on: scene sum %.4f with the game's diffuse probes, %.4f with the black cube\n", with[0], without[0]);
    CHECK(without[0] > 0.01 && without[0] < with[0]);
  }

  // 2. rt_probe_sky. Cubes: 1 x 1 faces, cube c face f = (0.1 (f + 1), c, 1).
  std::vector<float> faces(2 * 6 * 4);
  for (UINT cube = 0; cube < 2; ++cube) {
    for (UINT face = 0; face < 6; ++face) {
      float* texel = &faces[((cube * 6) + face) * 4];
      texel[0] = 0.1f * static_cast<float>(face + 1);
      texel[1] = static_cast<float>(cube);
      texel[2] = 1.f;
      texel[3] = 1.f;
    }
  }
  const D3D11_TEXTURE2D_DESC faces_desc = {.Width = 1,
                                           .Height = 1,
                                           .MipLevels = 1,
                                           .ArraySize = 12,
                                           .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                           .SampleDesc = {.Count = 1},
                                           .Usage = D3D11_USAGE_IMMUTABLE,
                                           .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                           .MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE};
  std::array<D3D11_SUBRESOURCE_DATA, 12> faces_data = {};
  for (UINT slice = 0; slice < 12; ++slice) faces_data[slice] = {.pSysMem = &faces[slice * 4], .SysMemPitch = 16};
  ComPtr<ID3D11Texture2D> faces_texture;
  device->CreateTexture2D(&faces_desc, faces_data.data(), faces_texture.GetAddressOf());
  D3D11_SHADER_RESOURCE_VIEW_DESC faces_view = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY};
  faces_view.TextureCubeArray = {.MostDetailedMip = 0, .MipLevels = 1, .First2DArrayFace = 0, .NumCubes = 2};
  ComPtr<ID3D11ShaderResourceView> faces_srv;
  device->CreateShaderResourceView(faces_texture.Get(), &faces_view, faces_srv.GetAddressOf());
  // The list: a small probe, the widest before the sun (layer 1, colour (0.5, 1, 2), attenuation 0.8), a point light, the
  // sun, then an older frame's wider probe.
  std::vector<uint8_t> list(8 * 256, 0);
  const auto entry = [&](UINT index, uint32_t type, uint32_t layer, float radius, std::array<float, 3> colour, float attenuation) {
    uint8_t* at = list.data() + (index * 256);
    const std::array<uint32_t, 2> header = {type, layer};
    const std::array<float, 4> position_radius = {0.f, 0.f, 0.f, radius};
    const std::array<float, 4> color = {colour[0], colour[1], colour[2], 1.f};
    std::memcpy(at, header.data(), 8);
    std::memcpy(at + 16, position_radius.data(), 16);
    std::memcpy(at + 32, &attenuation, 4);
    std::memcpy(at + 96, color.data(), 16);
  };
  entry(0, 1, 0, 10.f, {1.f, 1.f, 1.f}, 1.f);
  entry(1, 1, 1, 500.f, {0.5f, 1.f, 2.f}, 0.8f);
  entry(2, 5, 0, 900.f, {1.f, 1.f, 1.f}, 1.f);
  entry(3, 8, 0, 0.f, {1.f, 1.f, 1.f}, 1.f);
  entry(4, 1, 0, 9999.f, {9.f, 9.f, 9.f}, 1.f);
  const auto list_srv = Structured(list, 256);
  const D3D11_TEXTURE2D_DESC sun_desc = {.Width = 16,
                                         .Height = 1,
                                         .MipLevels = 1,
                                         .ArraySize = 1,
                                         .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                         .SampleDesc = {.Count = 1},
                                         .BindFlags = D3D11_BIND_UNORDERED_ACCESS};
  ComPtr<ID3D11Texture2D> sun;
  ComPtr<ID3D11UnorderedAccessView> sun_uav;
  device->CreateTexture2D(&sun_desc, nullptr, sun.GetAddressOf());
  device->CreateUnorderedAccessView(sun.Get(), nullptr, sun_uav.GetAddressOf());
  const auto sky = [&](ID3D11ShaderResourceView* lights, ID3D11ShaderResourceView* cubes) {
    const std::array<float, 4> marker = {-7.f, -7.f, -7.f, -7.f};
    context->ClearState();
    context->ClearUnorderedAccessViewFloat(sun_uav.Get(), marker.data());
    std::array<ID3D11ShaderResourceView*, 2> inputs = {lights, cubes};
    ID3D11UnorderedAccessView* output = sun_uav.Get();
    ID3D11SamplerState* linear = sampler.Get();
    context->CSSetShader(probe_sky.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 2, inputs.data());
    context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
    context->CSSetSamplers(0, 1, &linear);
    context->Dispatch(1, 1, 1);
    D3D11_TEXTURE2D_DESC desc = sun_desc;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
    context->CopyResource(staging.Get(), sun.Get());
    std::array<float, 64> texels = {};
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
      std::memcpy(texels.data(), mapped.pData, sizeof(texels));
      context->Unmap(staging.Get(), 0);
    }
    return texels;
  };
  const auto with_list = sky(list_srv.Get(), faces_srv.Get());
  float worst = 0.f;
  for (UINT face = 0; face < 6; ++face) {
    const std::array<float, 4> expected = {0.1f * static_cast<float>(face + 1) * 0.5f * 0.8f, 1.f * 1.f * 0.8f, 1.f * 2.f * 0.8f, 1.f};
    for (UINT channel = 0; channel < 4; ++channel) worst = std::max(worst, std::abs(with_list[((3 + face) * 4) + channel] - expected[channel]));
    std::printf("  sky texel %u: %.4f %.4f %.4f %.1f (expected %.4f %.4f %.4f 1)\n", 3 + face, with_list[(3 + face) * 4], with_list[((3 + face) * 4) + 1],
                with_list[((3 + face) * 4) + 2], with_list[((3 + face) * 4) + 3], expected[0], expected[1], expected[2]);
  }
  std::printf("rt_probe_sky: the widest probe before the sun, largest difference %.3g; texels 0-2 untouched: %s\n", worst,
              (with_list[0] == -7.f && with_list[8] == -7.f) ? "yes" : "no");
  CHECK(worst <= 1e-5f);
  CHECK(with_list[0] == -7.f && with_list[8] == -7.f && with_list[9 * 4] == -7.f);
  const auto without_list = sky(nullptr, nullptr);
  bool empty = true;
  for (UINT texel = 3; texel < 9; ++texel) {
    for (UINT channel = 0; channel < 4; ++channel) empty = empty && without_list[(texel * 4) + channel] == 0.f;
  }
  std::printf("rt_probe_sky without a light list: texels 3-8 zero (no sky): %s\n", empty ? "yes" : "no");
  CHECK(empty);
  std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
