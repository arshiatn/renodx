// Shogun 2's own ray tracing and helper-upscaler compute shaders (src/games/twshogun2completeedition) on WARP.
// tests <repo root> <DLSS input capture folder>
// 1. rt_sun (Shogun 2's own) with the 2026-11-14 data run's three sun draws (battle, sea, campaign: lighting_VS_PS,
//    the camera, environment_PS) and synthetic harmonics against the sun shaders' ambient (TestSun).
// 2. upscale_inputs with the capture's frame 0 (render 2560x1440: RGBA16F colour, R24G8 depth, RG16F motion, NGX motion
//    scale (-1, 1)) into the bridge's formats one texel larger each way: the colour (its square root and not), the depth
//    as stored, the motion x the scale, nothing past the render size. CONTROLS: motion scale (1, 1) and the square root
//    off must differ.
// 3. upscale_output and its Upscaling Debug View (TestOutput).
// 4. sss_foliage_depth, Light Through Foliage's depth (TestFoliage).
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
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static int failures = 0;

#define CHECK(x, ...)                                         \
  do {                                                        \
    if (!(x)) {                                               \
      std::printf("FAIL line %d: %s: ", __LINE__, #x);        \
      std::printf(__VA_ARGS__);                               \
      std::printf("\n");                                      \
      ++failures;                                             \
    }                                                         \
  } while (false)

static ComPtr<ID3D11ComputeShader> Compile(const std::string& path, const D3D_SHADER_MACRO* macros = nullptr) {
  const std::wstring wide(path.begin(), path.end());
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompileFromFile(wide.c_str(), macros, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                code.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("compile failed (%s): %s\n", path.c_str(), errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    std::exit(2);
  }
  ComPtr<ID3D11ComputeShader> shader;
  device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf());
  return shader;
}

static ComPtr<ID3D11Buffer> Constants(const void* data, UINT bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &initial, buffer.GetAddressOf());
  return buffer;
}

static ComPtr<ID3D11Texture2D> Texture(UINT width, UINT height, DXGI_FORMAT format, const void* data, UINT pitch, UINT bind) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = 1, .Format = format, .SampleDesc = {.Count = 1},
                                     .Usage = D3D11_USAGE_DEFAULT, .BindFlags = bind};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data, .SysMemPitch = pitch};
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, (data != nullptr) ? &initial : nullptr, texture.GetAddressOf());
  return texture;
}

// The texture's texels (rows of `texel_bytes` each, packed).
static std::vector<uint8_t> Read(ID3D11Texture2D* texture, UINT texel_bytes) {
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
  context->CopyResource(staging.Get(), texture);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<uint8_t> texels(static_cast<size_t>(desc.Width) * desc.Height * texel_bytes);
  for (UINT row = 0; row < desc.Height; ++row) {
    std::memcpy(texels.data() + (static_cast<size_t>(row) * desc.Width * texel_bytes), static_cast<const uint8_t*>(mapped.pData) + (static_cast<size_t>(row) * mapped.RowPitch),
                static_cast<size_t>(desc.Width) * texel_bytes);
  }
  context->Unmap(staging.Get(), 0);
  return texels;
}

static float Half(uint16_t half) {
  const uint32_t sign = (half & 0x8000u) << 16;
  const uint32_t exponent = (half >> 10) & 0x1Fu;
  const uint32_t mantissa = half & 0x3FFu;
  if (exponent == 0) return (sign ? -1.f : 1.f) * std::ldexp(static_cast<float>(mantissa), -24);
  const uint32_t bits = sign | ((exponent == 31 ? 255u : exponent + 112u) << 23) | (mantissa << 13);
  float value = 0.f;
  std::memcpy(&value, &bits, 4);
  return value;
}

static bool Near(float a, float b, float relative) {
  return std::fabs(a - b) <= (relative * std::max(std::fabs(a), std::fabs(b))) + 1e-30f;
}

// The data run's sun draws: the first 16 registers of lighting_VS_PS (rt_sun reads to c14), the camera's position
// (camera_VS_PS c0) and environment_PS c0-c5 (the probe grid: area c3, size c4.x as a uint, lerp c4.z, terms c5.w).
struct SunDraw {
  const char* name;
  std::array<float, 64> lighting;
  std::array<float, 4> camera;
  std::array<float, 24> environment;
};
static const std::array<SunDraw, 3> SUN_DRAWS = {{
    {.name = "battle ED1BED93",
     .lighting = {-0.7960979f, -0.5877858f, 0.1440006f, 9.459185e-39f, 84112.55f, 71577.57f, 49356.4f, 1.f, 1.f, 1.092847e-38f, 1.046938e-38f,
                  1.056113e-38f, 0.2828865f, 0.5209318f, 1.f, 2.938806e-39f, 0.1855839f, 0.2251126f, 0.2623321f, 9.275548e-39f, 0.4870255f,
                  0.5041848f, 0.5121719f, 1.010206e-38f, 0.03784777f, 0.06576082f, 0.01148963f, 8.90818e-39f, 0.1782103f, 0.2059063f, 0.236235f,
                  1.046941e-38f, 0.1549197f, 0.2253252f, 0.3116145f, 9.275529e-39f, 0.0004800692f, 0.004606451f, 0.006022009f, 9.091843e-39f,
                  0.02067193f, 0.04099049f, 0.05384444f, 1.401298e-45f, 1.f, 0.f, 9.431818f, 300.f, 0.5f, 9.183704e-39f, 1.065305e-38f,
                  9.275545e-39f, -1024.f, -1024.f, 1024.f, 1024.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 9.00003e-39f, 8.908199e-39f},
     .camera = {637.9034f, 94.96758f, 21.81959f, 0.f},
     .environment = {0.7960978f, 0.5877857f, -0.1440006f, 7.216687e-43f, 1.f, 10.f, 100000.f, 1.f, 1.f, 0.f, 0.f, 1.f, -1024.f, -1024.f, 1024.f,
                     1024.f, 7.006492e-45f, 25.f, 1.f, 0.661202f, 0.f, 10.f, 1.f, 9.f}},
    {.name = "sea ED1BED93",
     .lighting = {-0.9207556f, -0.3894183f, -0.02371511f, 1.401298e-45f, 42567.57f, 42567.57f, 42567.57f, 1.f, 0.273224f, 0.f, 1.401298e-45f,
                  3.761622e-37f, 0.431058f, 0.4802828f, 0.5899238f, 1.509546e-39f, 0.5115643f, 0.5709938f, 0.7114751f, 1.641559e-39f, 0.2423867f,
                  0.2754562f, 0.3265477f, 1.837411e-40f, 0.01018352f, 0.06104831f, 0.09266015f, 4.484155e-44f, 0.2285052f, 0.3219273f, 0.5068477f,
                  1.469438e-39f, 0.4680364f, 0.5627694f, 0.7438115f, 1.469438e-39f, 0.02350721f, 0.07074028f, 0.06077817f, 2.296168e-41f,
                  0.009838917f, 0.03318591f, 0.004290012f, 1.401298e-45f, 1.f, 0.f, 1.f, 0.f, 0.f, 2.984752e-39f, 0.f, 0.f, -1024.f, -1024.f,
                  1024.f, 1024.f, 0.f, 1.5f, 0.f, 0.f, 0.f, 1.f, 0.f, 2.296028e-41f},
     .camera = {-183.4287f, 90.f, 480.9623f, 0.f},
     .environment = {0.9207556f, 0.3894183f, 0.02371511f, 1.015599e-34f, 0.15f, 10.f, 100000.f, 1.f, 0.5932106f, 0.f, 0.f, 1.f, -1024.f, -1024.f,
                     1024.f, 1024.f, 7.006492e-45f, 25.f, 1.f, 0.240437f, 0.076503f, 5.7386f, 0.639344f, 9.f}},
    {.name = "campaign C3E0E939",
     .lighting = {-0.9427759f, -0.2034619f, 0.2641531f, 2.062166e-07f, 150000.f, 150000.f, 150000.f, 1.f, 0.661202f, 4032.942f, 5.438893e-20f,
                  2.062166e-07f, 0.2942063f, 0.2942063f, 0.2942063f, 8.631999e-43f, 0.2526698f, 0.2556754f, 0.263554f, 8.631999e-43f, 0.228759f,
                  0.2939129f, 0.4690664f, 8.631999e-43f, 0.001834614f, 0.01617394f, 0.0001349572f, 8.631999e-43f, 0.4920455f, 0.5278161f,
                  0.7307833f, 8.631999e-43f, 0.578449f, 0.578449f, 0.578449f, 8.631999e-43f, 0.01930526f, 0.04002449f, 0.05108619f, 8.631999e-43f,
                  0.02965025f, 0.06060925f, 0.09691535f, 1.401298e-45f, 0.f, 0.f, 3.f, 350.f, 0.2732f, 8.631999e-43f, 2.232359e-15f, 8.631999e-43f,
                  -266.f, -266.f, 266.f, 266.f, 0.f, 2.033898f, 0.f, 0.f, 0.f, 1.f, 2.213649e-15f, 8.631999e-43f},
     .camera = {-266.f, 12.70201f, 247.1187f, 0.f},
     .environment = {0.9427759f, 0.2034619f, -0.2641531f, 5.739719e-42f, 1.f, 10.f, 100000.f, 1.f, 3.333333f, 0.f, 0.f, 1.f, -266.f, -266.f, 266.f,
                     266.f, 2.242078e-44f, 256.f, 1.f, 0.52459f, 0.f, 10.f, 1.f, 9.f}},
}};

using SunTexels = std::array<std::array<float, 4>, 9>;
using Harmonics = std::vector<std::array<float, 4>>;  // (grid x 3)^2 texels, RGBA32F

static uint32_t Bits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, 4);
  return bits;
}

// rt_sun's nine texels from one draw (ray_tracing.hpp's SUN_OFFSETS: sun_direction 0, cube 12, sun_colour 4).
static SunTexels RunSun(ID3D11ComputeShader* shader, const SunDraw& draw, const std::array<float, 72>& lighting, const std::array<float, 24>& environment,
                        const Harmonics& current, const Harmonics& previous) {
  constexpr std::array<uint32_t, 8> SUN_OFFSETS = {0, 12, 0, 0, 4, 0, 0, 0};
  const std::vector<std::array<float, 4>> initial(9, {7.f, 7.f, 7.f, 7.f});
  const auto texture = Texture(9, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, initial.data(), 9 * 16, D3D11_BIND_UNORDERED_ACCESS);
  ComPtr<ID3D11UnorderedAccessView> uav;
  device->CreateUnorderedAccessView(texture.Get(), nullptr, uav.GetAddressOf());
  const UINT side = static_cast<UINT>(std::lround(std::sqrt(static_cast<double>(current.size()))));
  const std::array<ComPtr<ID3D11Texture2D>, 2> harmonics = {
      Texture(side, side, DXGI_FORMAT_R32G32B32A32_FLOAT, current.data(), side * 16, D3D11_BIND_SHADER_RESOURCE),
      Texture(side, side, DXGI_FORMAT_R32G32B32A32_FLOAT, previous.data(), side * 16, D3D11_BIND_SHADER_RESOURCE)};
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> views;
  for (size_t i = 0; i < 2; ++i) device->CreateShaderResourceView(harmonics[i].Get(), nullptr, views[i].GetAddressOf());
  const std::array<ComPtr<ID3D11Buffer>, 4> buffers = {Constants(lighting.data(), 288), Constants(SUN_OFFSETS.data(), 32),
                                                       Constants(draw.camera.data(), 16), Constants(environment.data(), 96)};
  const std::array<ID3D11Buffer*, 4> bound = {buffers[0].Get(), buffers[1].Get(), buffers[2].Get(), buffers[3].Get()};
  const std::array<ID3D11ShaderResourceView*, 2> bound_views = {views[0].Get(), views[1].Get()};
  context->CSSetShader(shader, nullptr, 0);
  context->CSSetConstantBuffers(0, 4, bound.data());
  context->CSSetShaderResources(0, 2, bound_views.data());
  context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
  context->Dispatch(1, 1, 1);
  context->ClearState();
  SunTexels texels = {};
  std::memcpy(texels.data(), Read(texture.Get(), 16).data(), sizeof(texels));
  return texels;
}

// The game's harmonics at an axis normal, as sunlight00's loop addresses them (decompiled: r11 the cell's and next cell's
// texel corners x 3 clamped to 3 x size - 3, a and b along x at the cell's row, c and d at the next row) and weights them
// (r15 / r16's one-hot terms: 1, n.x, n.y, n.z, n.x n.z, n.z n.y, n.x n.y, 3 n.z^2 - 1, n.x^2 - n.y^2), clamped at 0.
static std::array<float, 3> GameHarmonics(const Harmonics& texels, const SunDraw& draw, const std::array<float, 24>& environment,
                                          const std::array<float, 3>& n) {
  const auto size = static_cast<int>(Bits(environment[16]));
  const int width = size * 3;
  float place[2] = {};
  int cell[2] = {};
  int next[2] = {};
  for (int axis = 0; axis < 2; ++axis) {
    const float position = draw.camera[axis * 2];
    const float relative = (position - environment[12 + axis]) / (environment[14 + axis] - environment[12 + axis]);
    place[axis] = std::clamp((relative * static_cast<float>(size)) - 0.5f, 0.f, static_cast<float>(size - 1));
    const float whole = std::floor(place[axis]);
    place[axis] -= whole;
    cell[axis] = std::min(static_cast<int>(whole) * 3, (size * 3) - 3);
    next[axis] = std::min((static_cast<int>(whole) * 3) + 3, (size * 3) - 3);
  }
  const std::array<float, 9> weights = {1.f, n[0], n[1], n[2], n[0] * n[2], n[2] * n[1], n[0] * n[1], (n[2] * n[2] * 3.f) - 1.f, (n[0] * n[0]) - (n[1] * n[1])};
  const int terms = static_cast<int>(std::lround(environment[23]));
  std::array<float, 3> sum = {};
  for (int term = 0; term < terms && term < 9; ++term) {
    const int column = term % 3;
    const int row = term / 3;
    for (int channel = 0; channel < 3; ++channel) {
      const float a = texels[((cell[1] + row) * width) + cell[0] + column][channel];
      const float b = texels[((cell[1] + row) * width) + next[0] + column][channel];
      const float c = texels[((next[1] + row) * width) + cell[0] + column][channel];
      const float d = texels[((next[1] + row) * width) + next[0] + column][channel];
      const float near_row = a + (place[0] * (b - a));
      const float far_row = c + (place[0] * (d - c));
      sum[channel] += (near_row + (place[1] * (far_row - near_row))) * weights[term];
    }
  }
  for (float& value : sum) value = std::max(value, 0.f);
  return sum;
}

static bool SameSky(const SunTexels& a, const SunTexels& b) {
  for (size_t texel = 3; texel < 9; ++texel) {
    for (size_t channel = 0; channel < 4; ++channel) {
      if (!Near(a[texel][channel], b[texel][channel], 1e-5f)) return false;
    }
  }
  return true;
}

// 1. rt_sun: texel 0 the way to the sun, texel 1 untouched, texel 2 sun_colour x 1.90384671e-005, texels 3-8 the sun
//    shaders' diffuse ambient at the six sides (with the harmonics flag c10.w: g_cube_ambient_influence x the cube + the
//    rest x g_ambient_fudge_factor x the harmonics at the camera's place; without: the fudge factor x the cube; the
//    previous harmonics by g_specular_probe_lerp below 0.999), w 1 but texel 6's 0. Synthetic harmonics (seeded) in grids
//    the traced sizes (5 x 5, 16 x 16 probes). CONTROLS: the shared rt_sun (the cube alone, texel 6's w 1) differs; the
//    flag cleared and the previous harmonics at lerp 0.4 change the sky where the influence is below 1 (sea, campaign),
//    not in the battle (influence and fudge factor 1: the cube alone, as the game's).
static void TestSun(const std::string& games) {
  const auto shader = Compile(games + "/twshogun2completeedition/rt_sun.cs_5_0.hlsl");
  const D3D_SHADER_MACRO shared_macros[] = {{"TW_RT_SUN_SCALE_SWITCH", "0"}, {"TW_RT_SUN_LIGHT_SCALE", "1.90384671e-005f"}, {nullptr, nullptr}};
  const auto shared = Compile(games + "/twattila/rt_sun.cs_5_0.hlsl", shared_macros);
  std::mt19937 random(1114);
  std::uniform_real_distribution<float> coefficient(-0.3f, 0.9f);
  for (const auto& draw : SUN_DRAWS) {
    const auto size = static_cast<size_t>(Bits(draw.environment[16]));
    Harmonics current(size * 3 * size * 3);
    Harmonics previous(current.size());
    for (auto& texel : current) texel = {coefficient(random), coefficient(random), coefficient(random), 1.f};
    for (auto& texel : previous) texel = {coefficient(random), coefficient(random), coefficient(random), 1.f};
    std::array<float, 72> lighting = {};
    std::copy(draw.lighting.begin(), draw.lighting.end(), lighting.begin());
    const auto& c = lighting;
    const bool harmonics_on = Bits(c[43]) != 0u;
    for (const float probe_lerp : {draw.environment[18], 0.4f}) {
      auto environment = draw.environment;
      environment[18] = probe_lerp;
      const auto texels = RunSun(shader.Get(), draw, lighting, environment, current, previous);
      const float length = std::sqrt((c[0] * c[0]) + (c[1] * c[1]) + (c[2] * c[2]));
      for (size_t axis = 0; axis < 3; ++axis) {
        CHECK(Near(texels[0][axis], -c[axis] / length, 1e-5f), "%s: sun way %zu %g", draw.name, axis, texels[0][axis]);
        CHECK(Near(texels[2][axis], c[4 + axis] * 1.90384671e-005f, 1e-6f), "%s: sun light %zu %g", draw.name, axis, texels[2][axis]);
      }
      CHECK(texels[0][3] == 1.f && texels[2][3] == 1.f && texels[1][0] == 7.f && texels[1][3] == 7.f, "%s: texels 0-2", draw.name);
      for (size_t side = 0; side < 6; ++side) {
        std::array<float, 3> normal = {};
        normal[side / 2] = ((side & 1) != 0) ? -1.f : 1.f;
        auto harmonics = GameHarmonics(current, draw, environment, normal);
        if (probe_lerp < 0.999f) {
          const auto before = GameHarmonics(previous, draw, environment, normal);
          for (size_t channel = 0; channel < 3; ++channel) harmonics[channel] = before[channel] + (probe_lerp * (harmonics[channel] - before[channel]));
        }
        for (size_t channel = 0; channel < 3; ++channel) {
          const float cube = c[12 + (side * 4) + channel];
          const float expected = harmonics_on ? ((c[8] * cube) + ((1.f - c[8]) * c[57] * harmonics[channel])) : (c[57] * cube);
          CHECK(std::fabs(texels[3 + side][channel] - expected) <= 1e-5f + (1e-5f * std::fabs(expected)), "%s lerp %g: sky %zu.%zu %g, expected %g",
                draw.name, probe_lerp, side, channel, texels[3 + side][channel], expected);
        }
        CHECK(texels[3 + side][3] == ((side == 3) ? 0.f : 1.f), "%s: sky weight %zu %g", draw.name, side, texels[3 + side][3]);
      }
      // The harmonics count where the influence is below 1 (the battle's is 1: the cube alone, as the game's).
      const bool harmonics_count = harmonics_on && c[8] < 1.f;
      if (probe_lerp != draw.environment[18]) {
        const auto other = RunSun(shader.Get(), draw, lighting, draw.environment, current, previous);
        CHECK(SameSky(texels, other) != harmonics_count, "%s: CONTROL the previous harmonics at lerp 0.4", draw.name);
        continue;
      }
      const bool cube_alone = SameSky(texels, RunSun(shared.Get(), draw, lighting, environment, current, previous));
      CHECK(!cube_alone, "%s: CONTROL the shared rt_sun's sky matched", draw.name);
      auto no_flag = lighting;
      no_flag[43] = 0.f;
      // Cleared: the fudge factor x the cube, the same only where the influence and the fudge factor are 1 (the battle).
      CHECK(SameSky(texels, RunSun(shader.Get(), draw, no_flag, environment, current, previous)) == (c[8] == 1.f && c[57] == 1.f),
            "%s: CONTROL the flag cleared", draw.name);
      std::printf("rt_sun %s: influence %.3f fudge %.3f harmonics %s, grid %zu: sky +y %.4f %.4f %.4f (cube %.4f %.4f %.4f)\n", draw.name, c[8], c[57],
                  harmonics_on ? "on" : "off", size, texels[5][0], texels[5][1], texels[5][2], c[20], c[21], c[22]);
    }
  }
}

static std::vector<uint8_t> Load(const std::string& path, UINT* width, UINT* height) {
  std::ifstream file(path, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (bytes.size() < 16) {
    std::printf("missing %s\n", path.c_str());
    std::exit(2);
  }
  std::memcpy(width, bytes.data(), 4);
  std::memcpy(height, bytes.data() + 4, 4);
  return {bytes.begin() + 16, bytes.end()};
}

struct InputsResult {
  std::vector<uint8_t> color;
  std::vector<uint8_t> depth;
  std::vector<uint8_t> motion;
};

static void TestInputs(const std::string& games, const std::string& capture) {
  UINT width = 0;
  UINT height = 0;
  const auto color = Load(capture + "/shogun2_dlss_capture_0_color.bin", &width, &height);
  const auto depth = Load(capture + "/shogun2_dlss_capture_0_depth.bin", &width, &height);
  const auto motion = Load(capture + "/shogun2_dlss_capture_0_motion.bin", &width, &height);
  const auto source_color = Texture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, color.data(), width * 8, D3D11_BIND_SHADER_RESOURCE);
  const auto source_depth = Texture(width, height, DXGI_FORMAT_R24G8_TYPELESS, depth.data(), width * 4, D3D11_BIND_SHADER_RESOURCE);
  const auto source_motion = Texture(width, height, DXGI_FORMAT_R16G16_FLOAT, motion.data(), width * 4, D3D11_BIND_SHADER_RESOURCE);
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> sources;
  device->CreateShaderResourceView(source_color.Get(), nullptr, sources[0].GetAddressOf());
  const D3D11_SHADER_RESOURCE_VIEW_DESC depth_view = {.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                      .Texture2D = {.MipLevels = 1}};
  device->CreateShaderResourceView(source_depth.Get(), &depth_view, sources[1].GetAddressOf());
  device->CreateShaderResourceView(source_motion.Get(), nullptr, sources[2].GetAddressOf());
  const auto shader = Compile(games + "/twshogun2completeedition/upscale_inputs.cs_5_0.hlsl");
  // The bridge's textures one texel larger each way, a sentinel in every texel.
  const UINT out_width = width + 1;
  const UINT out_height = height + 1;
  const auto run = [&](float square_root, float scale_x, float scale_y) {
    struct Settings {
      float square_root;
      float motion_scale[2];
      float pad;
      uint32_t render_size[2];
      uint32_t pad2[2];
    } settings = {.square_root = square_root, .motion_scale = {scale_x, scale_y}, .pad = 0.f, .render_size = {width, height}, .pad2 = {}};
    const std::vector<uint16_t> color_fill(static_cast<size_t>(out_width) * out_height * 4, 0x4700);  // 7.0
    const std::vector<float> depth_fill(static_cast<size_t>(out_width) * out_height, 7.f);
    const std::vector<uint16_t> motion_fill(static_cast<size_t>(out_width) * out_height * 2, 0x4700);
    const std::array<ComPtr<ID3D11Texture2D>, 3> outputs = {
        Texture(out_width, out_height, DXGI_FORMAT_R16G16B16A16_FLOAT, color_fill.data(), out_width * 8, D3D11_BIND_UNORDERED_ACCESS),
        Texture(out_width, out_height, DXGI_FORMAT_R32_FLOAT, depth_fill.data(), out_width * 4, D3D11_BIND_UNORDERED_ACCESS),
        Texture(out_width, out_height, DXGI_FORMAT_R16G16_FLOAT, motion_fill.data(), out_width * 4, D3D11_BIND_UNORDERED_ACCESS)};
    std::array<ComPtr<ID3D11UnorderedAccessView>, 3> uavs;
    for (size_t i = 0; i < 3; ++i) device->CreateUnorderedAccessView(outputs[i].Get(), nullptr, uavs[i].GetAddressOf());
    const auto constants = Constants(&settings, sizeof(settings));
    const std::array<ID3D11ShaderResourceView*, 3> views = {sources[0].Get(), sources[1].Get(), sources[2].Get()};
    const std::array<ID3D11UnorderedAccessView*, 3> targets = {uavs[0].Get(), uavs[1].Get(), uavs[2].Get()};
    context->CSSetShader(shader.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 3, views.data());
    context->CSSetUnorderedAccessViews(0, 3, targets.data(), nullptr);
    context->CSSetConstantBuffers(0, 1, constants.GetAddressOf());
    context->Dispatch((out_width + 7) / 8, (out_height + 7) / 8, 1);
    context->ClearState();
    return InputsResult{.color = Read(outputs[0].Get(), 8), .depth = Read(outputs[1].Get(), 4), .motion = Read(outputs[2].Get(), 4)};
  };
  const auto plain = run(0.f, -1.f, 1.f);
  const auto rooted = run(1.f, -1.f, 1.f);
  const auto unscaled = run(0.f, 1.f, 1.f);
  size_t bad_color = 0, bad_root = 0, bad_depth = 0, bad_motion = 0, outside = 0, negative = 0, moving = 0, root_same = 0, scale_same = 0;
  double sky = 0.0, ground = 0.0;
  for (UINT y = 0; y < out_height; ++y) {
    for (UINT x = 0; x < out_width; ++x) {
      const size_t out = (static_cast<size_t>(y) * out_width) + x;
      const auto* color_out = reinterpret_cast<const uint16_t*>(plain.color.data()) + (out * 4);
      const auto* root_out = reinterpret_cast<const uint16_t*>(rooted.color.data()) + (out * 4);
      float depth_out = 0.f;
      std::memcpy(&depth_out, plain.depth.data() + (out * 4), 4);
      const auto* motion_out = reinterpret_cast<const uint16_t*>(plain.motion.data()) + (out * 2);
      const auto* motion_unscaled = reinterpret_cast<const uint16_t*>(unscaled.motion.data()) + (out * 2);
      if (x >= width || y >= height) {
        outside += (color_out[0] != 0x4700 || depth_out != 7.f || motion_out[0] != 0x4700) ? 1 : 0;
        continue;
      }
      const size_t in = (static_cast<size_t>(y) * width) + x;
      const auto* color_in = reinterpret_cast<const uint16_t*>(color.data()) + (in * 4);
      for (size_t channel = 0; channel < 3; ++channel) {
        const float value = std::max(Half(color_in[channel]), 0.f);
        negative += (Half(color_in[channel]) < 0.f) ? 1 : 0;
        bad_color += Near(Half(color_out[channel]), value, 1e-3f) ? 0 : 1;
        bad_root += Near(Half(root_out[channel]), std::sqrt(value), 1e-3f) ? 0 : 1;
        root_same += (!Near(std::sqrt(value), value, 1e-2f) && root_out[channel] == color_out[channel]) ? 1 : 0;
      }
      bad_color += (color_out[3] == 0x3C00) ? 0 : 1;  // alpha 1
      uint32_t stored = 0;
      std::memcpy(&stored, depth.data() + (in * 4), 4);
      const float expected_depth = static_cast<float>(stored & 0xFFFFFFu) / 16777215.f;
      bad_depth += Near(depth_out, expected_depth, 1e-6f) ? 0 : 1;
      if (y < 40) sky += depth_out;
      if (y >= height - 40) ground += depth_out;
      const auto* motion_in = reinterpret_cast<const uint16_t*>(motion.data()) + (in * 2);
      bad_motion += (Half(motion_out[0]) == -Half(motion_in[0]) && Half(motion_out[1]) == Half(motion_in[1])) ? 0 : 1;
      moving += (Half(motion_in[0]) != 0.f) ? 1 : 0;
      scale_same += (Half(motion_in[0]) != 0.f && motion_unscaled[0] == motion_out[0]) ? 1 : 0;
    }
  }
  const double rows = 40.0 * width;
  CHECK(bad_color == 0 && bad_root == 0 && bad_depth == 0 && bad_motion == 0, "colour %zu, root %zu, depth %zu, motion %zu", bad_color, bad_root,
        bad_depth, bad_motion);
  CHECK(outside == 0, "%zu texels past the render size written", outside);
  CHECK(moving > 0 && scale_same == 0, "CONTROL motion scale (1, 1): %zu of %zu moving texels the same", scale_same, moving);
  CHECK(root_same == 0, "CONTROL square root off: %zu the same", root_same);
  std::printf("upscale_inputs %ux%u: %zu negative colour channels clamped, %zu moving texels; depth top 40 rows %.4f, bottom %.4f (0 near)\n", width,
              height, negative, moving, sky / rows, ground / rows);
}

// twattila/motion.hlsli's MotionColour.
static std::array<float, 3> MotionColour(float x, float y) {
  const float hue = (std::atan2(y, x) * (0.5f / 3.14159265f)) + 0.5f;
  const float brightness = std::clamp(std::log2(1.f + std::sqrt((x * x) + (y * y))) / 5.f, 0.f, 1.f);
  std::array<float, 3> rgb = {};
  const std::array<float, 3> offsets = {0.f, 2.f / 3.f, 1.f / 3.f};
  for (size_t channel = 0; channel < 3; ++channel) {
    const float shifted = hue + offsets[channel];
    rgb[channel] = std::clamp((std::fabs(((shifted - std::floor(shifted)) * 6.f) - 3.f)) - 1.f, 0.f, 1.f) * brightness;
  }
  return rgb;
}

// 3. upscale_output from a 1280x720 render to 1920x1080 (synthetic: the result 0.5 everywhere as a square root, the
//    motion -20..20 pixels by position, the colour input a ramp): view 0 squares the result; the Upscaling Debug View
//    (1) shows MotionColour of the stretched render texel over the dimmed input. CONTROL: view 0 is not view 1.
static void TestOutput(const std::string& games) {
  constexpr UINT RENDER_WIDTH = 1280, RENDER_HEIGHT = 720, WIDTH = 1920, HEIGHT = 1080;
  const std::vector<std::array<float, 4>> result(static_cast<size_t>(WIDTH) * HEIGHT, {0.5f, 0.5f, 0.5f, 1.f});
  std::vector<std::array<float, 2>> motion(static_cast<size_t>(WIDTH) * HEIGHT);
  std::vector<std::array<float, 4>> input(static_cast<size_t>(WIDTH) * HEIGHT);
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const size_t at = (static_cast<size_t>(y) * WIDTH) + x;
      motion[at] = {(static_cast<float>(x) - 640.f) / 32.f, (static_cast<float>(y) - 360.f) / 18.f};
      const float ramp = static_cast<float>(x) / static_cast<float>(RENDER_WIDTH);  // a square root: the scene ramp^2
      input[at] = {ramp, ramp, ramp, 1.f};
    }
  }
  const std::array<ComPtr<ID3D11Texture2D>, 3> sources = {
      Texture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, result.data(), WIDTH * 16, D3D11_BIND_SHADER_RESOURCE),
      Texture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32_FLOAT, motion.data(), WIDTH * 8, D3D11_BIND_SHADER_RESOURCE),
      Texture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, input.data(), WIDTH * 16, D3D11_BIND_SHADER_RESOURCE)};
  std::array<ComPtr<ID3D11ShaderResourceView>, 3> views;
  for (size_t i = 0; i < 3; ++i) device->CreateShaderResourceView(sources[i].Get(), nullptr, views[i].GetAddressOf());
  const auto shader = Compile(games + "/twshogun2completeedition/upscale_output.cs_5_0.hlsl");
  const auto run = [&](uint32_t debug_view) {
    struct Settings {
      float square_root;
      float motion_scale[2];
      float pad;
      uint32_t render_size[2];
      uint32_t debug_view;
      uint32_t pad2;
    } settings = {.square_root = 1.f, .motion_scale = {-1.f, 1.f}, .pad = 0.f, .render_size = {RENDER_WIDTH, RENDER_HEIGHT}, .debug_view = debug_view,
                  .pad2 = 0};
    const auto target = Texture(WIDTH, HEIGHT, DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
    ComPtr<ID3D11UnorderedAccessView> uav;
    device->CreateUnorderedAccessView(target.Get(), nullptr, uav.GetAddressOf());
    const auto constants = Constants(&settings, sizeof(settings));
    const std::array<ID3D11ShaderResourceView*, 3> bound = {views[0].Get(), views[1].Get(), views[2].Get()};
    context->CSSetShader(shader.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 3, bound.data());
    context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
    context->CSSetConstantBuffers(0, 1, constants.GetAddressOf());
    context->Dispatch((WIDTH + 7) / 8, (HEIGHT + 7) / 8, 1);
    context->ClearState();
    std::vector<std::array<float, 4>> texels(static_cast<size_t>(WIDTH) * HEIGHT);
    std::memcpy(texels.data(), Read(target.Get(), 16).data(), texels.size() * 16);
    return texels;
  };
  const auto plain = run(0);
  const auto debug = run(1);
  size_t bad_plain = 0, bad_debug = 0, same = 0, checked = 0;
  for (UINT y = 0; y < HEIGHT; y += 7) {
    for (UINT x = 0; x < WIDTH; x += 5) {
      const size_t at = (static_cast<size_t>(y) * WIDTH) + x;
      const UINT input_x = std::min(static_cast<UINT>((static_cast<float>(x) + 0.5f) * RENDER_WIDTH / WIDTH), RENDER_WIDTH - 1);
      const UINT input_y = std::min(static_cast<UINT>((static_cast<float>(y) + 0.5f) * RENDER_HEIGHT / HEIGHT), RENDER_HEIGHT - 1);
      const size_t from = (static_cast<size_t>(input_y) * WIDTH) + input_x;
      const auto colour = MotionColour(motion[from][0], motion[from][1]);
      const float dim = 0.1f * input[from][0] * input[from][0];
      for (size_t channel = 0; channel < 3; ++channel) {
        bad_plain += Near(plain[at][channel], 0.25f, 1e-6f) ? 0 : 1;
        bad_debug += (std::fabs(debug[at][channel] - std::max(colour[channel], dim)) <= 2e-3f) ? 0 : 1;
      }
      same += (debug[at][0] == plain[at][0] && debug[at][1] == plain[at][1] && debug[at][2] == plain[at][2]) ? 1 : 0;
      ++checked;
    }
  }
  CHECK(bad_plain == 0 && bad_debug == 0, "view 0 %zu, Debug View %zu of %zu texels' channels", bad_plain, bad_debug, checked * 3);
  CHECK(same * 100 < checked, "CONTROL: %zu of %zu texels the same with the Debug View", same, checked);
  std::printf("upscale_output %ux%u -> %ux%u: %zu texels checked, Debug View against MotionColour\n", RENDER_WIDTH, RENDER_HEIGHT, WIDTH, HEIGHT,
              checked);
}

// 4. sss_foliage_depth (Light Through Foliage) with the capture's depth (R24G8) and synthetic G-buffer constants whose
//    alpha byte cycles 0-255 by pixel: the depth negative exactly where the byte has 128, the stored depth elsewhere.
//    CONTROL: the alpha byte 127 (every other bit) leaves the depth.
static void TestFoliage(const std::string& games, const std::string& capture) {
  UINT width = 0;
  UINT height = 0;
  const auto depth = Load(capture + "/shogun2_dlss_capture_0_depth.bin", &width, &height);
  std::vector<uint16_t> constants(static_cast<size_t>(width) * height * 4);
  for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height; ++pixel) {
    // The alpha as the G-buffer's RGBA16F holds byte / 255 (exact enough in half for round(x 255)).
    const float alpha = static_cast<float>(pixel % 256) / 255.f;
    uint32_t bits = 0;
    std::memcpy(&bits, &alpha, 4);
    const uint32_t exponent = (bits >> 23) & 0xFF;
    const uint16_t half = (alpha == 0.f) ? 0 : static_cast<uint16_t>(((exponent - 112) << 10) | ((bits >> 13) & 0x3FF));
    constants[(pixel * 4) + 3] = half;
  }
  const auto depth_texture = Texture(width, height, DXGI_FORMAT_R24G8_TYPELESS, depth.data(), width * 4, D3D11_BIND_SHADER_RESOURCE);
  const auto constants_texture = Texture(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, constants.data(), width * 8, D3D11_BIND_SHADER_RESOURCE);
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> views;
  const D3D11_SHADER_RESOURCE_VIEW_DESC depth_view = {.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                                      .Texture2D = {.MipLevels = 1}};
  device->CreateShaderResourceView(depth_texture.Get(), &depth_view, views[0].GetAddressOf());
  device->CreateShaderResourceView(constants_texture.Get(), nullptr, views[1].GetAddressOf());
  const auto target = Texture(width, height, DXGI_FORMAT_R32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
  ComPtr<ID3D11UnorderedAccessView> uav;
  device->CreateUnorderedAccessView(target.Get(), nullptr, uav.GetAddressOf());
  const auto shader = Compile(games + "/twshogun2completeedition/sss_foliage_depth.cs_5_0.hlsl");
  const std::array<ID3D11ShaderResourceView*, 2> bound = {views[0].Get(), views[1].Get()};
  context->CSSetShader(shader.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 2, bound.data());
  context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
  context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
  context->ClearState();
  const auto out = Read(target.Get(), 4);
  size_t bad = 0, negative = 0, control_negative = 0;
  for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height; ++pixel) {
    uint32_t stored = 0;
    std::memcpy(&stored, depth.data() + (pixel * 4), 4);
    const float expected_depth = static_cast<float>(stored & 0xFFFFFFu) / 16777215.f;
    float value = 0.f;
    std::memcpy(&value, out.data() + (pixel * 4), 4);
    const bool foliage = ((pixel % 256) & 128u) != 0u;
    bad += Near(value, foliage ? -expected_depth : expected_depth, 1e-6f) ? 0 : 1;
    negative += (value < 0.f) ? 1 : 0;
    control_negative += ((pixel % 256) == 127 && value < 0.f) ? 1 : 0;
  }
  CHECK(bad == 0, "%zu pixels' depth wrong", bad);
  CHECK(control_negative == 0, "CONTROL: %zu pixels with the byte 127 negative", control_negative);
  std::printf("sss_foliage_depth %ux%u: %zu foliage pixels negative (half the bytes have 128)\n", width, height, negative);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("tests <repo root> <capture folder>\n");
    return 2;
  }
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    return 2;
  }
  const std::string games = std::string(argv[1]) + "/src/games";
  TestSun(games);
  TestInputs(games, argv[2]);
  TestOutput(games);
  TestFoliage(games, argv[2]);
  std::printf(failures == 0 ? "PASS\n" : "FAILED: %d\n", failures);
  return failures == 0 ? 0 : 1;
}
