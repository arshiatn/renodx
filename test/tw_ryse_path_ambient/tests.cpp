// Ryse's Path-Traced Ambient (rysesonofrome/ray_tracing.hpp) on WARP.
// 1. The tiled lighting (the game's cs 0x897232B5 and the add-on's raised one, tiled_lights_cs.h) with only probe lights
//    (lightType 1) and the probes' reflections black (t33 cubes, t6 reflections): with the game's diffuse probes (t34) the
//    scene is lit, with the add-on's black cube (one layer) at t34 it is black, u1 and u2 alike: the diffuse probes are
//    the lighting's only ambient, the black cube takes all of it. Control: with the probes' reflections back, the black
//    cube keeps them (the scene not black) and differs from the game's.
// 2. rt_probe_sky (the add-on's build): the widest probe before the sun (later entries are older frames') at the six axes
//    times its colour and attenuation into the sun texture's texels 3-8, w 1 but texel 6's 0: the helper's OpenSky then
//    equals the cube at the normal (control: texel 6's w 1 stretches y); no light list: zeros.
// 3. The ray-traced AO in place of the game's SSDO amount (the AO method 2). a) ssdo00 (the add-on's build against the
//    game's bytes, a camera above a floor facing a wall, GT-VBAO saved): the AO method 0 is the game's (GT-VBAO only at
//    1), 2 leaves the SSDO amount out (w 0 without LSDO, the terrain's LSDO alone with it), the direction kept. Control:
//    the game's amount at the wall's foot, GT-VBAO at the method 1. The same for its two permutations against their
//    bytes (optional arguments): 0xC17645D2 (depth samples from t5) and 0xA65B60AF (no LSDO, PER_BATCH tighter). b) contact shadows' 0x52C5E003 adds the rays' amount
//    (1 - visibility) / SSDOParams.z onto t5's alpha, colour untouched; control: the old max blend. c) the game's tiled
//    lighting with that t5 lights as with the rays' amount alone (LSDO kept); control: the max over the game's SSDO keeps
//    its halo where the rays see open sky.
// 4. The SSDO reflection occlusion left out under the traced reflections / specular occlusion (ray_tracing.hpp): its copy
//    of PER_BATCH (CopySubresourceRegion, then a zero float over SSDOParams.y) holds the game's 32 floats with y 0; the
//    game's lighting with it: t5 open, the same picture; t5 occluded, the same diffuse (u2) and a brighter scene (the
//    probes' reflections no longer occluded by SSDO; the control that the term was live).
// Arguments: the repository (its build's embedded csos), the game's dumped 0x897232B5.cs_5_0.cso and 0xD1106E8B.ps_5_0.cso,
// optionally its 0xC17645D2.ps_5_0.cso and 0xA65B60AF.ps_5_0.cso; all only read.
#include <windows.h>

#include <DirectXPackedVector.h>
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
#include <fstream>
#include <iterator>
#include <vector>

#include "src/games/rysesonofrome/shared.h"
#include "src/games/rysesonofrome/tiled_lights_cs.h"

using Microsoft::WRL::ComPtr;
using DirectX::PackedVector::XMConvertFloatToHalf;
using DirectX::PackedVector::XMConvertHalfToFloat;

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

// A 2D texture of `texels` (rows of `pitch` bytes) and its view.
struct Image {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11ShaderResourceView> srv;
};

Image Texture2D(UINT width, UINT height, DXGI_FORMAT format, const void* texels, UINT pitch, UINT bind = D3D11_BIND_SHADER_RESOURCE) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width,
                                     .Height = height,
                                     .MipLevels = 1,
                                     .ArraySize = 1,
                                     .Format = format,
                                     .SampleDesc = {.Count = 1},
                                     .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = bind};
  const D3D11_SUBRESOURCE_DATA data = {.pSysMem = texels, .SysMemPitch = pitch};
  Image image;
  device->CreateTexture2D(&desc, &data, image.texture.GetAddressOf());
  if ((bind & D3D11_BIND_SHADER_RESOURCE) != 0) device->CreateShaderResourceView(image.texture.Get(), nullptr, image.srv.GetAddressOf());
  return image;
}

// `texture`'s texels (`texel_bytes` each), row after row.
std::vector<uint8_t> Read(ID3D11Texture2D* texture, UINT texel_bytes) {
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = 0;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
  context->CopyResource(staging.Get(), texture);
  std::vector<uint8_t> texels(static_cast<size_t>(desc.Width) * desc.Height * texel_bytes);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    for (UINT row = 0; row < desc.Height; ++row) {
      std::memcpy(&texels[static_cast<size_t>(row) * desc.Width * texel_bytes], static_cast<const uint8_t*>(mapped.pData) + (row * mapped.RowPitch),
                  static_cast<size_t>(desc.Width) * texel_bytes);
    }
    context->Unmap(staging.Get(), 0);
  }
  return texels;
}

// An R32G32B32A32_FLOAT (or, `half`, R16G16B16A16_FLOAT) texture's channels as floats.
std::vector<float> Floats(ID3D11Texture2D* texture, bool half = false) {
  const auto bytes = Read(texture, half ? 8u : 16u);
  std::vector<float> channels(bytes.size() / (half ? 2u : 4u));
  if (!half) {
    std::memcpy(channels.data(), bytes.data(), bytes.size());
    return channels;
  }
  for (size_t index = 0; index < channels.size(); ++index) {
    uint16_t value = 0;
    std::memcpy(&value, &bytes[index * 2], 2);
    channels[index] = XMConvertHalfToFloat(value);
  }
  return channels;
}

ComPtr<ID3D11Buffer> Constants(const void* data, UINT bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA init = {.pSysMem = data};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &init, buffer.GetAddressOf());
  return buffer;
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
  return {.scene = Floats(targets[0].Get()), .diffuse = Floats(targets[1].Get())};
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

// The largest colour difference over the largest colour of `reference` (alpha left out).
double Relative(const std::vector<float>& texels, const std::vector<float>& reference) {
  double difference = 0.0;
  for (size_t index = 0; index < texels.size(); ++index) {
    if (index % 4 != 3) difference = std::max(difference, static_cast<double>(std::abs(texels[index] - reference[index])));
  }
  return difference / std::max(Measure(reference)[1], 1e-9);
}

// The full-screen triangle with ssdo00's inputs: uv, the 2 x 2 pattern's coordinates (4-pixel tiles), and the world view
// ray to the far plane (x right, y forward, z up; view: tan x, tan y, far).
constexpr char SSDO_VERTEX[] = R"(
cbuffer Screen : register(b0) {
  float4 screen_size;
  float4 view;
};
void main(uint id : SV_VertexID, out float4 position : SV_Position, out float4 uv : TEXCOORD0, out float4 ray : TEXCOORD1) {
  const float2 corner = float2((id << 1) & 2, id & 2);
  position = float4(corner * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.f, 1.f);
  uv = float4(corner, corner * screen_size.xy / 4.f);
  ray = float4(float3((2.f * corner.x - 1.f) * view.x, 1.f, (1.f - 2.f * corner.y) * view.y) * view.z, 0.f);
}
)";

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::printf("usage: ryse_path_ambient.exe <repository> <the game's 0x897232B5.cs_5_0.cso> <the game's 0xD1106E8B.ps_5_0.cso>\n");
    return 2;
  }
  const std::filesystem::path repository = argv[1];
  const std::filesystem::path embed = repository / "build/rysesonofrome.include/embed";
  const auto game_code = ReadFile(argv[2]);
  const auto game_ssdo_code = ReadFile(argv[3]);
  const auto sky_code = ReadFile(embed / "rt_probe_sky.cso");
  const auto ssdo_code = ReadFile(embed / "0xD1106E8B.cso");
  const auto pass_vertex_code = ReadFile(embed / "0x52C5E002.cso");
  const auto pass_code = ReadFile(embed / "0x52C5E003.cso");
  if (game_code.empty() || game_ssdo_code.empty() || sky_code.empty() || ssdo_code.empty() || pass_vertex_code.empty() || pass_code.empty()) {
    std::printf("missing: the game's csos or the build's rt_probe_sky / 0xD1106E8B / 0x52C5E002 / 0x52C5E003.cso\n");
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
    // w 1, texel 6's 0 (the helper's OpenSky then takes the cube at the normal as it is: Ryse's world is z up).
    const std::array<float, 4> expected = {0.1f * static_cast<float>(face + 1) * 0.5f * 0.8f, 1.f * 1.f * 0.8f, 1.f * 2.f * 0.8f,
                                           (face == 3) ? 0.f : 1.f};
    for (UINT channel = 0; channel < 4; ++channel) worst = std::max(worst, std::abs(with_list[((3 + face) * 4) + channel] - expected[channel]));
    std::printf("  sky texel %u: %.4f %.4f %.4f %.1f (expected %.4f %.4f %.4f %.0f)\n", 3 + face, with_list[(3 + face) * 4],
                with_list[((3 + face) * 4) + 1], with_list[((3 + face) * 4) + 2], with_list[((3 + face) * 4) + 3], expected[0], expected[1],
                expected[2], expected[3]);
  }
  // The helper's OpenSky (dlaa_helper/rt_path.hlsli) on these texels: the cube at the normal, scaled by texel 4's w; texel
  // 6's w not 0 stretches the normal's y four times (Attila's sun shaders). It must equal the plain cube at the normal
  // (the tiled lighting samples its probes at the world normal); CONTROL: texel 6's w 1 (as before) must differ.
  const auto open_sky = [&](std::array<float, 3> normal, float texel6_w) {
    if (texel6_w != 0.f) {
      normal[1] *= 4.f;
      const float length = std::sqrt((normal[0] * normal[0]) + (normal[1] * normal[1]) + (normal[2] * normal[2]));
      for (float& axis : normal) axis /= length;
    }
    std::array<float, 3> light = {};
    for (UINT axis = 0; axis < 3; ++axis) {
      const UINT texel = 3 + (2 * axis) + ((normal[axis] < 0.f) ? 1u : 0u);
      for (UINT channel = 0; channel < 3; ++channel) light[channel] += normal[axis] * normal[axis] * with_list[(texel * 4) + channel];
    }
    for (float& channel : light) channel *= with_list[(4 * 4) + 3];
    return light;
  };
  float sky_difference = 0.f;
  float stretched_difference = 0.f;
  for (const std::array<float, 3>& normal : {std::array<float, 3>{0.f, 0.6f, 0.8f}, std::array<float, 3>{0.48f, -0.6f, 0.64f},
                                             std::array<float, 3>{-0.8f, 0.36f, -0.48f}}) {
    const auto plain = open_sky(normal, 0.f);
    const auto written = open_sky(normal, with_list[(6 * 4) + 3]);
    const auto stretched = open_sky(normal, 1.f);
    for (UINT channel = 0; channel < 3; ++channel) {
      sky_difference = std::max(sky_difference, std::abs(written[channel] - plain[channel]));
      stretched_difference = std::max(stretched_difference, std::abs(stretched[channel] - plain[channel]) / std::max(plain[channel], 1e-6f));
    }
  }
  std::printf("  the helper's OpenSky with the written texel 6 against the cube at the normal: largest difference %.3g; CONTROL "
              "texel 6's w 1 (y four times over): %.1f %% off\n",
              sky_difference, 100.f * stretched_difference);
  CHECK(sky_difference == 0.f);
  CHECK(stretched_difference > 0.01f);
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

  // 3a. ssdo00 at half resolution: a camera above a floor (1.5 m below) facing a wall 12 m ahead, camera space x right,
  // y up, z forward (SSDO_CameraMatrix identity); the terrain's height 0 (1.5 m above the floor) for LSDO.
  constexpr UINT SSDO_WIDTH = 160;
  constexpr UINT SSDO_HEIGHT = 90;
  constexpr float FAR_PLANE = 1000.f;
  constexpr float TAN_X = 0.7f;
  constexpr float TAN_Y = TAN_X * SSDO_HEIGHT / SSDO_WIDTH;
  std::vector<float> ssdo_depth(SSDO_WIDTH * SSDO_HEIGHT);
  std::vector<uint32_t> ssdo_normals(SSDO_WIDTH * SSDO_HEIGHT);
  const auto pack = [](float x, float y, float z) {
    const auto byte = [](float value) { return static_cast<uint32_t>(std::lround(((value * 0.5f) + 0.5f) * 255.f)); };
    return byte(x) | (byte(y) << 8) | (byte(z) << 16) | (255u << 24);
  };
  for (UINT y = 0; y < SSDO_HEIGHT; ++y) {
    const float ray_y = TAN_Y * (1.f - (2.f * (static_cast<float>(y) + 0.5f) / SSDO_HEIGHT));
    const bool on_floor = ray_y < 0.f && -1.5f / ray_y < 12.f;
    for (UINT x = 0; x < SSDO_WIDTH; ++x) {
      ssdo_depth[(y * SSDO_WIDTH) + x] = (on_floor ? -1.5f / ray_y : 12.f) / FAR_PLANE;
      ssdo_normals[(y * SSDO_WIDTH) + x] = on_floor ? pack(0.f, 1.f, 0.f) : pack(0.f, 0.f, -1.f);
    }
  }
  const std::vector<float> heights(SSDO_WIDTH * SSDO_HEIGHT, 0.f);
  const auto ssdo_normal = Texture2D(SSDO_WIDTH, SSDO_HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, ssdo_normals.data(), SSDO_WIDTH * 4);
  const auto ssdo_depth_image = Texture2D(SSDO_WIDTH, SSDO_HEIGHT, DXGI_FORMAT_R32_FLOAT, ssdo_depth.data(), SSDO_WIDTH * 4);
  const auto terrain = Texture2D(SSDO_WIDTH, SSDO_HEIGHT, DXGI_FORMAT_R32_FLOAT, heights.data(), SSDO_WIDTH * 4);
  // PER_BATCH: identity camera matrices, LSDO's texture-to-world scale 1, far, screen size, the shadow map's texel, the
  // SSDO radii, view space.
  std::array<std::array<float, 4>, 15> batch = {};
  batch[1] = {1.f, 0.f, 0.f, 0.f};
  batch[2] = {0.f, 1.f, 0.f, 0.f};
  batch[3] = {0.f, 0.f, 1.f, 0.f};
  batch[4] = batch[1];
  batch[5] = batch[2];
  batch[6] = batch[3];
  batch[7] = {1.f, 1.f, 1.f, 0.f};
  batch[9] = {0.1f, FAR_PLANE, 0.f, 0.f};
  batch[11] = {static_cast<float>(SSDO_WIDTH), static_cast<float>(SSDO_HEIGHT), 1.f / SSDO_WIDTH, 1.f / SSDO_HEIGHT};
  batch[12] = {1.f / 1024, 1.f / 1024, 0.f, 0.f};
  batch[13] = {0.5f, 0.5f, 0.002f, 0.1f};
  batch[14] = {2.f * TAN_X, -2.f * TAN_Y, -TAN_X, TAN_Y};
  const std::array<float, 8> screen = {batch[11][0], batch[11][1], batch[11][2], batch[11][3], TAN_X, TAN_Y, FAR_PLANE, 0.f};
  const auto screen_buffer = Constants(screen.data(), sizeof(screen));
  ComPtr<ID3DBlob> vertex_blob;
  ComPtr<ID3D11VertexShader> ssdo_vertex;
  if (FAILED(D3DCompile(SSDO_VERTEX, sizeof(SSDO_VERTEX) - 1, "ssdo_vertex", nullptr, nullptr, "main", "vs_5_0", 0, 0, vertex_blob.GetAddressOf(), nullptr))
      || FAILED(device->CreateVertexShader(vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize(), nullptr, ssdo_vertex.GetAddressOf()))) {
    std::printf("ssdo00's vertex shader could not be made\n");
    return 2;
  }
  const D3D11_SAMPLER_DESC point_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
                                         .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                         .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
                                         .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                         .MaxAnisotropy = 1,
                                         .ComparisonFunc = D3D11_COMPARISON_NEVER,
                                         .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> point;
  device->CreateSamplerState(&point_desc, point.GetAddressOf());
  // `shader` at the AO method `method` with GT-VBAO the saved row (ssao_mode 1), LSDO on or off; `packed`: 0xA65B60AF's
  // PER_BATCH (no LSDO_Params: c9-c14 one register down).
  const auto ssdo_run = [&](ID3D11PixelShader* shader, float method, bool lsdo, bool packed) {
    auto lsdo_batch = batch;
    lsdo_batch[8] = {lsdo ? 1.f : 0.f, 0.f, 0.f, 0.f};
    if (packed) std::copy(batch.begin() + 9, batch.end(), lsdo_batch.begin() + 8);
    ShaderInjectData injection = {};
    injection.ssao_mode = 1.f;
    injection.ssao_radius = 3.f;
    injection.ssao_strength = 1.f;
    injection.ssao_thickness = 0.1f;
    injection.ssao_quality = 1.f;
    injection.ssao_directions = 1.f;
    injection.ao_method = method;
    std::array<float, 96> injection_floats = {};
    static_assert(sizeof(ShaderInjectData) <= sizeof(injection_floats));
    std::memcpy(injection_floats.data(), &injection, sizeof(injection));
    const auto batch_buffer = Constants(lsdo_batch.data(), sizeof(lsdo_batch));
    const auto injection_buffer = Constants(injection_floats.data(), sizeof(injection_floats));
    const D3D11_TEXTURE2D_DESC target_desc = {.Width = SSDO_WIDTH,
                                              .Height = SSDO_HEIGHT,
                                              .MipLevels = 1,
                                              .ArraySize = 1,
                                              .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                              .SampleDesc = {.Count = 1},
                                              .BindFlags = D3D11_BIND_RENDER_TARGET};
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    device->CreateTexture2D(&target_desc, nullptr, target.GetAddressOf());
    device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf());
    context->ClearState();
    ID3D11RenderTargetView* output = target_view.Get();
    context->OMSetRenderTargets(1, &output, nullptr);
    const D3D11_VIEWPORT viewport = {.Width = SSDO_WIDTH, .Height = SSDO_HEIGHT, .MaxDepth = 1.f};
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(ssdo_vertex.Get(), nullptr, 0);
    ID3D11Buffer* screen_constants = screen_buffer.Get();
    context->VSSetConstantBuffers(0, 1, &screen_constants);
    context->PSSetShader(shader, nullptr, 0);
    ID3D11Buffer* batch_constants = batch_buffer.Get();
    ID3D11Buffer* injection_constants = injection_buffer.Get();
    context->PSSetConstantBuffers(0, 1, &batch_constants);
    context->PSSetConstantBuffers(13, 1, &injection_constants);
    const std::array<ID3D11ShaderResourceView*, 2> inputs = {ssdo_normal.srv.Get(), ssdo_depth_image.srv.Get()};
    context->PSSetShaderResources(0, 2, inputs.data());
    // 0xC17645D2's depth samples (t5): the same depth here.
    context->PSSetShaderResources(5, 1, &inputs[1]);
    ID3D11ShaderResourceView* heightmap = terrain.srv.Get();
    context->PSSetShaderResources(11, 1, &heightmap);
    const std::array<ID3D11SamplerState*, 2> samplers = {point.Get(), point.Get()};
    context->PSSetSamplers(0, 2, samplers.data());
    context->PSSetSamplers(11, 1, samplers.data());
    context->PSSetSamplers(5, 1, samplers.data());
    context->Draw(3, 0);
    return Floats(target.Get());
  };
  // ssdo00 or a permutation (`name`): the game's bytes against the build's.
  const auto check_ssdo = [&](const char* name, const std::vector<char>& game_bytes, const std::vector<char>& built, bool packed) {
    ComPtr<ID3D11PixelShader> game_ssdo;
    ComPtr<ID3D11PixelShader> ssdo;
    if (game_bytes.empty() || built.empty() || FAILED(device->CreatePixelShader(game_bytes.data(), game_bytes.size(), nullptr, game_ssdo.GetAddressOf()))
        || FAILED(device->CreatePixelShader(built.data(), built.size(), nullptr, ssdo.GetAddressOf()))) {
      std::printf("%s's shaders could not be made\n", name);
      ++failures;
      return;
    }
    const auto game_off = ssdo_run(game_ssdo.Get(), 0.f, false, packed);
    const auto game_on = ssdo_run(game_ssdo.Get(), 0.f, true, packed);
    const auto ours_off = ssdo_run(ssdo.Get(), 0.f, false, packed);
    const auto ours_on = ssdo_run(ssdo.Get(), 0.f, true, packed);
    const auto rays_off = ssdo_run(ssdo.Get(), 2.f, false, packed);
    const auto rays_on = ssdo_run(ssdo.Get(), 2.f, true, packed);
    const auto bitmask = ssdo_run(ssdo.Get(), 1.f, false, packed);
    float game_difference = 0.f;
    float direction_difference = 0.f;
    float rays_amount = 0.f;
    float lsdo_difference = 0.f;
    float game_amount = 0.f;
    float bitmask_difference = 0.f;
    double lsdo_sum = 0.0;
    for (size_t index = 0; index < game_off.size(); ++index) {
      game_difference = std::max({game_difference, std::abs(game_off[index] - ours_off[index]), std::abs(game_on[index] - ours_on[index])});
      if (index % 4 != 3) {
        direction_difference = std::max({direction_difference, std::abs(rays_off[index] - ours_off[index]), std::abs(rays_on[index] - ours_on[index])});
        continue;
      }
      // The terrain's share: the game's amount with LSDO less without.
      const float lsdo = ours_on[index] - ours_off[index];
      lsdo_sum += lsdo;
      rays_amount = std::max(rays_amount, std::abs(rays_off[index]));
      lsdo_difference = std::max(lsdo_difference, std::abs(rays_on[index] - lsdo));
      game_amount = std::max(game_amount, game_off[index]);
      bitmask_difference = std::max(bitmask_difference, std::abs(bitmask[index] - ours_off[index]));
    }
    std::printf("%s, GT-VBAO saved: the AO method 0 against the game's bytes, largest difference %.3g (LSDO off and on); the method 2: "
                "amount largest %.3g without LSDO, %.3g from the terrain's share with it (its mean %.4f), direction largest difference %.3g\n",
                name, game_difference, rays_amount, lsdo_difference, lsdo_sum / (SSDO_WIDTH * SSDO_HEIGHT), direction_difference);
    std::printf("  control: the game's amount largest %.3f; GT-VBAO at the method 1 differs by up to %.3f\n", game_amount, bitmask_difference);
    CHECK(game_difference < 1e-3f);
    CHECK(rays_amount == 0.f);
    // No LSDO in 0xA65B60AF: its share 0.
    CHECK(lsdo_difference < 1e-5f && (packed ? lsdo_sum == 0.0 : lsdo_sum / (SSDO_WIDTH * SSDO_HEIGHT) > 1e-3));
    CHECK(direction_difference < 1e-6f);
    CHECK(game_amount > 0.05f && bitmask_difference > 0.05f);
  };
  check_ssdo("ssdo00", game_ssdo_code, ssdo_code, false);
  if (argc >= 6) {
    check_ssdo("ssdo01 0xC17645D2", ReadFile(argv[4]), ReadFile(embed / "0xC17645D2.cso"), false);
    check_ssdo("ssdo02 0xA65B60AF", ReadFile(argv[5]), ReadFile(embed / "0xA65B60AF.cso"), true);
  } else {
    std::printf("ssdo00's permutations: not tested (no game csos given)\n");
  }

  // 3b. 0x52C5E003 onto t5 (RGBA16F, the blurred directional occlusion) with the sun in another slice (its mask gets
  // nothing). The rays see open sky on the left half, 0.2-1 on the right; t5 holds ssdo00's direction and LSDO (the method
  // 2) or the game's amount and LSDO (the old max blend's input).
  std::array<float, 32> lighting_constants = constants;
  lighting_constants[20] = 0.5f;  // SSDOParams: the lights', the reflections' and the ambient's occlusion
  lighting_constants[21] = 0.7f;
  lighting_constants[22] = 0.9f;
  const auto lighting_buffer = Constants(lighting_constants.data(), sizeof(lighting_constants));
  std::vector<float> visibility(EDGE * EDGE);
  std::vector<uint16_t> ours_texels(EDGE * EDGE * 4);
  std::vector<uint16_t> old_texels(EDGE * EDGE * 4);
  std::vector<float> reference_texels(EDGE * EDGE * 4);
  for (UINT texel = 0; texel < EDGE * EDGE; ++texel) {
    visibility[texel] = (texel % EDGE < EDGE / 2) ? 1.f : (0.2f + (0.8f * Random()));
    for (UINT channel = 0; channel < 3; ++channel) {
      ours_texels[(texel * 4) + channel] = XMConvertFloatToHalf(0.3f + (0.4f * Random()));
      old_texels[(texel * 4) + channel] = ours_texels[(texel * 4) + channel];
      reference_texels[(texel * 4) + channel] = XMConvertHalfToFloat(ours_texels[(texel * 4) + channel]);
    }
    const float lsdo = 0.1f * Random();
    ours_texels[(texel * 4) + 3] = XMConvertFloatToHalf(lsdo);
    old_texels[(texel * 4) + 3] = XMConvertFloatToHalf(lsdo + (0.35f * Random()));
    reference_texels[(texel * 4) + 3] = XMConvertHalfToFloat(ours_texels[(texel * 4) + 3]) + ((1.f - visibility[texel]) / lighting_constants[22]);
  }
  ComPtr<ID3D11VertexShader> pass_vertex;
  ComPtr<ID3D11PixelShader> pass;
  // contact_shadows.hpp's blends: the mask max, t5's alpha (written alone) max or added (the AO method 2).
  D3D11_BLEND_DESC blend_desc = {.IndependentBlendEnable = TRUE};
  blend_desc.RenderTarget[0] = {
      .BlendEnable = TRUE,
      .SrcBlend = D3D11_BLEND_ONE,
      .DestBlend = D3D11_BLEND_ONE,
      .BlendOp = D3D11_BLEND_OP_MAX,
      .SrcBlendAlpha = D3D11_BLEND_ONE,
      .DestBlendAlpha = D3D11_BLEND_ONE,
      .BlendOpAlpha = D3D11_BLEND_OP_MAX,
      .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
  };
  blend_desc.RenderTarget[1] = blend_desc.RenderTarget[0];
  blend_desc.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
  D3D11_BLEND_DESC replacing_desc = blend_desc;
  replacing_desc.RenderTarget[1].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  ComPtr<ID3D11BlendState> max_blend;
  ComPtr<ID3D11BlendState> replacing_blend;
  const D3D11_RASTERIZER_DESC no_cull_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE, .DepthClipEnable = TRUE};
  ComPtr<ID3D11RasterizerState> no_cull;
  if (FAILED(device->CreateVertexShader(pass_vertex_code.data(), pass_vertex_code.size(), nullptr, pass_vertex.GetAddressOf()))
      || FAILED(device->CreatePixelShader(pass_code.data(), pass_code.size(), nullptr, pass.GetAddressOf()))
      || FAILED(device->CreateBlendState(&blend_desc, max_blend.GetAddressOf()))
      || FAILED(device->CreateBlendState(&replacing_desc, replacing_blend.GetAddressOf()))
      || FAILED(device->CreateRasterizerState(&no_cull_desc, no_cull.GetAddressOf()))) {
    std::printf("contact shadows' objects could not be made\n");
    return 2;
  }
  // 0x52C5E001's output: the sun in slice 1, no channels.
  const std::array<uint32_t, 2> sun_values = {1u, 0u};
  const D3D11_BUFFER_DESC sun_buffer_desc = {.ByteWidth = sizeof(sun_values), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA sun_buffer_data = {.pSysMem = sun_values.data()};
  ComPtr<ID3D11Buffer> sun_buffer;
  device->CreateBuffer(&sun_buffer_desc, &sun_buffer_data, sun_buffer.GetAddressOf());
  D3D11_SHADER_RESOURCE_VIEW_DESC sun_view = {.Format = DXGI_FORMAT_R32_UINT, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
  sun_view.Buffer = {.FirstElement = 0, .NumElements = 2};
  ComPtr<ID3D11ShaderResourceView> sun_srv;
  device->CreateShaderResourceView(sun_buffer.Get(), &sun_view, sun_srv.GetAddressOf());
  const std::array<float, 4> contact_values = {1.f, 0.1f, 0.f, 0.f};
  const auto contact_buffer = Constants(contact_values.data(), sizeof(contact_values));
  const std::vector<float> pass_depth(EDGE * EDGE, 0.5f);
  const std::vector<uint32_t> mask_texels(EDGE * EDGE, 0u);
  const auto pass_depth_image = Texture2D(EDGE, EDGE, DXGI_FORMAT_R32_FLOAT, pass_depth.data(), EDGE * 4);
  const auto visibility_image = Texture2D(EDGE, EDGE, DXGI_FORMAT_R32_FLOAT, visibility.data(), EDGE * 4);
  const auto pass_run = [&](const std::vector<uint16_t>& texels, ID3D11BlendState* blend) {
    auto occlusion = Texture2D(EDGE, EDGE, DXGI_FORMAT_R16G16B16A16_FLOAT, texels.data(), EDGE * 8, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
    const auto mask = Texture2D(EDGE, EDGE, DXGI_FORMAT_R8G8B8A8_UNORM, mask_texels.data(), EDGE * 4, D3D11_BIND_RENDER_TARGET);
    ComPtr<ID3D11RenderTargetView> mask_target;
    ComPtr<ID3D11RenderTargetView> occlusion_target;
    device->CreateRenderTargetView(mask.texture.Get(), nullptr, mask_target.GetAddressOf());
    device->CreateRenderTargetView(occlusion.texture.Get(), nullptr, occlusion_target.GetAddressOf());
    context->ClearState();
    const D3D11_VIEWPORT viewport = {.Width = EDGE, .Height = EDGE, .MaxDepth = 1.f};
    const std::array<ID3D11ShaderResourceView*, 4> inputs = {pass_depth_image.srv.Get(), sun_srv.Get(), nullptr, visibility_image.srv.Get()};
    const std::array<ID3D11Buffer*, 2> buffers = {lighting_buffer.Get(), contact_buffer.Get()};
    const std::array<ID3D11RenderTargetView*, 2> targets = {mask_target.Get(), occlusion_target.Get()};
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(pass_vertex.Get(), nullptr, 0);
    context->PSSetShader(pass.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 4, inputs.data());
    context->PSSetConstantBuffers(0, 2, buffers.data());
    context->RSSetState(no_cull.Get());
    context->RSSetViewports(1, &viewport);
    context->OMSetBlendState(blend, nullptr, 0xFFFFFFFF);
    context->OMSetRenderTargets(2, targets.data(), nullptr);
    context->Draw(3, 0);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    return occlusion;
  };
  const auto ours_occlusion = pass_run(ours_texels, replacing_blend.Get());
  const auto old_occlusion = pass_run(old_texels, max_blend.Get());
  const auto ours_read = Floats(ours_occlusion.texture.Get(), true);
  const auto old_read = Floats(old_occlusion.texture.Get(), true);
  float colour_difference = 0.f;
  float added_difference = 0.f;
  float max_difference = 0.f;
  for (size_t index = 0; index < ours_read.size(); ++index) {
    if (index % 4 != 3) {
      colour_difference = std::max({colour_difference, std::abs(ours_read[index] - reference_texels[index]), std::abs(old_read[index] - reference_texels[index])});
      continue;
    }
    const float rays = (1.f - visibility[index / 4]) / lighting_constants[22];
    added_difference = std::max(added_difference, std::abs(ours_read[index] - reference_texels[index]));
    max_difference = std::max(max_difference, std::abs(old_read[index] - std::max(XMConvertHalfToFloat(old_texels[index]), rays)));
  }
  std::printf("0x52C5E003 onto t5: the added alpha against LSDO + (1 - visibility) / SSDOParams.z, largest difference %.3g; colour "
              "kept (largest difference %.3g); control, the old max blend against max(t5, the rays'), largest difference %.3g\n",
              added_difference, colour_difference, max_difference);
  CHECK(added_difference < 1e-3f);
  CHECK(colour_difference == 0.f);
  CHECK(max_difference < 1e-3f);

  // 3c. The game's tiled lighting (probes, their reflections on) with each t5.
  auto lit_textures = textures;
  lit_textures[5] = ours_occlusion.srv;
  const Lit ours_lit = Run(game_lighting.Get(), probes, lit_textures, specular_cubes.Get(), diffuse_cubes.Get(), spots.Get(), lighting_buffer.Get(), sampler.Get());
  lit_textures[5] = Texture2D(EDGE, EDGE, DXGI_FORMAT_R32G32B32A32_FLOAT, reference_texels.data(), EDGE * 16).srv;
  const Lit reference_lit = Run(game_lighting.Get(), probes, lit_textures, specular_cubes.Get(), diffuse_cubes.Get(), spots.Get(), lighting_buffer.Get(), sampler.Get());
  lit_textures[5] = old_occlusion.srv;
  const Lit old_lit = Run(game_lighting.Get(), probes, lit_textures, specular_cubes.Get(), diffuse_cubes.Get(), spots.Get(), lighting_buffer.Get(), sampler.Get());
  lit_textures[5] = Texture2D(EDGE, EDGE, DXGI_FORMAT_R16G16B16A16_FLOAT, old_texels.data(), EDGE * 8).srv;
  const Lit game_lit = Run(game_lighting.Get(), probes, lit_textures, specular_cubes.Get(), diffuse_cubes.Get(), spots.Get(), lighting_buffer.Get(), sampler.Get());
  // The colour channels' sum on the left half (the rays see open sky there).
  const auto open_half = [](const std::vector<float>& texels) {
    double sum = 0.0;
    for (size_t index = 0; index < texels.size(); ++index) {
      if (index % 4 != 3 && (index / 4) % EDGE < EDGE / 2) sum += texels[index];
    }
    return sum;
  };
  const double ours_scene = Relative(ours_lit.scene, reference_lit.scene);
  const double ours_diffuse = Relative(ours_lit.diffuse, reference_lit.diffuse);
  std::printf("game's lighting with the rays' AO in place of SSDO's: against the rays' amount alone (LSDO kept), largest relative "
              "difference %.3g (u2 %.3g)\n",
              ours_scene, ours_diffuse);
  std::printf("  where the rays see open sky: u2 sum %.4f ours, %.4f the old max over SSDO, %.4f the game's SSDO alone\n", open_half(ours_lit.diffuse),
              open_half(old_lit.diffuse), open_half(game_lit.diffuse));
  CHECK(ours_scene < 2e-3 && ours_diffuse < 2e-3);
  CHECK(open_half(old_lit.diffuse) < 0.99 * open_half(ours_lit.diffuse));
  CHECK(Relative(old_lit.diffuse, reference_lit.diffuse) > 0.02);

  // 4. The SSDO reflection occlusion left out under the traced reflections / specular occlusion: ray_tracing.hpp's copy of
  // the game's PER_BATCH (a dynamic buffer here) with SSDOParams.y 0, then the game's lighting with t6 black (the probes'
  // share whole) and t5 occluded (3b's) or open (direction 0.5, amount 0).
  const D3D11_BUFFER_DESC game_batch_desc = {.ByteWidth = sizeof(lighting_constants),
                                             .Usage = D3D11_USAGE_DYNAMIC,
                                             .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                             .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
  ComPtr<ID3D11Buffer> game_batch;
  device->CreateBuffer(&game_batch_desc, nullptr, game_batch.GetAddressOf());
  if (D3D11_MAPPED_SUBRESOURCE mapped = {}; SUCCEEDED(context->Map(game_batch.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    std::memcpy(mapped.pData, lighting_constants.data(), sizeof(lighting_constants));
    context->Unmap(game_batch.Get(), 0);
  }
  const D3D11_BUFFER_DESC neutral_desc = {.ByteWidth = sizeof(lighting_constants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const std::array<float, 4> zeros = {};
  const D3D11_SUBRESOURCE_DATA zero_data = {.pSysMem = zeros.data()};
  const D3D11_BUFFER_DESC zero_desc = {.ByteWidth = sizeof(zeros), .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  ComPtr<ID3D11Buffer> neutral_batch;
  ComPtr<ID3D11Buffer> zero;
  device->CreateBuffer(&neutral_desc, nullptr, neutral_batch.GetAddressOf());
  device->CreateBuffer(&zero_desc, &zero_data, zero.GetAddressOf());
  context->ClearState();
  const D3D11_BOX whole_batch = {.left = 0, .right = sizeof(lighting_constants), .bottom = 1, .back = 1};
  context->CopySubresourceRegion(neutral_batch.Get(), 0, 0, 0, 0, game_batch.Get(), 0, &whole_batch);
  const D3D11_BOX one_float = {.right = 4, .bottom = 1, .back = 1};
  context->CopySubresourceRegion(neutral_batch.Get(), 0, (5 * 16) + 4, 0, 0, zero.Get(), 0, &one_float);
  const D3D11_BUFFER_DESC batch_staging_desc = {.ByteWidth = sizeof(lighting_constants), .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> batch_staging;
  device->CreateBuffer(&batch_staging_desc, nullptr, batch_staging.GetAddressOf());
  context->CopyResource(batch_staging.Get(), neutral_batch.Get());
  std::array<float, 32> neutral_values = {};
  neutral_values.fill(-99.f);
  if (D3D11_MAPPED_SUBRESOURCE mapped = {}; SUCCEEDED(context->Map(batch_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    std::memcpy(neutral_values.data(), mapped.pData, sizeof(neutral_values));
    context->Unmap(batch_staging.Get(), 0);
  }
  int batch_differences = 0;
  for (size_t index = 0; index < neutral_values.size(); ++index) {
    batch_differences += (neutral_values[index] != ((index == 21) ? 0.f : lighting_constants[index])) ? 1 : 0;
  }
  std::printf("PER_BATCH's copy with SSDOParams.y 0: %d of 32 floats off what they should be (SSDOParams %.2f %.2f %.2f)\n", batch_differences,
              neutral_values[20], neutral_values[21], neutral_values[22]);
  CHECK(batch_differences == 0);
  std::vector<float> open_texels(EDGE * EDGE * 4, 0.5f);
  for (size_t texel = 0; texel < EDGE * EDGE; ++texel) open_texels[(texel * 4) + 3] = 0.f;
  const auto open_occlusion = Texture2D(EDGE, EDGE, DXGI_FORMAT_R32G32B32A32_FLOAT, open_texels.data(), EDGE * 16);
  const auto lit_with = [&](ID3D11ShaderResourceView* occlusion, ID3D11Buffer* batch) {
    auto inputs = textures;
    inputs[5] = occlusion;
    return Run(game_lighting.Get(), probes, inputs, specular_cubes.Get(), diffuse_cubes.Get(), spots.Get(), batch, sampler.Get());
  };
  const Lit occluded_game = lit_with(ours_occlusion.srv.Get(), game_batch.Get());
  const Lit occluded_neutral = lit_with(ours_occlusion.srv.Get(), neutral_batch.Get());
  const Lit open_game = lit_with(open_occlusion.srv.Get(), game_batch.Get());
  const Lit open_neutral = lit_with(open_occlusion.srv.Get(), neutral_batch.Get());
  const double open_difference = Relative(open_neutral.scene, open_game.scene) + Relative(open_neutral.diffuse, open_game.diffuse);
  const double diffuse_difference = Relative(occluded_neutral.diffuse, occluded_game.diffuse);
  const double specular_difference = Relative(occluded_neutral.scene, occluded_game.scene);
  std::printf("game's lighting with the copy: t5 open, scene and u2 differ by %.3g; t5 occluded, u2 (diffuse) by %.3g, the scene by %.3g "
              "(sum %.4f with the SSDO reflection occlusion, %.4f without; open %.4f)\n",
              open_difference, diffuse_difference, specular_difference, Measure(occluded_game.scene)[0], Measure(occluded_neutral.scene)[0],
              Measure(open_game.scene)[0]);
  CHECK(open_difference == 0.0);
  CHECK(diffuse_difference == 0.0);
  CHECK(specular_difference > 0.01 && Measure(occluded_neutral.scene)[0] > Measure(occluded_game.scene)[0]);
  std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
