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
// 5-6. rt_capture_tree_billboard and rt_capture_imposter against the game's csos (TestBillboard, TestImposter; their folder third).
// 7. rt_water_plane (TestWaterPlane). 8-9. ocean00-03 and waterplane00-01 against the game's csos (TestWaters).
// 10. rt_capture / rt_rows_cascade_rigid and _variant against the game's 0xAC25DAB8 / 0x7B067623 / 0x124C85A5 / 0xB0E006EE
//     (TestPlacedShadow).
// 11. rt_capture_cascade_cloth / imposter / tree / far_tree / unit through Pharaoh's outside-view geometry shaders against the
//     game's 0x8A00A1D4 / 0xE3DA20D4 / 0x5851CA62 / 0x6F20485E / 0x30C453EE / 0xA22FA941, sail / rope against 0xA42F9EDE /
//     0xF7EDF855 / 0x4E3EE717, wind / weighted / grass / far_tree_3k against 0xEDCD8445 / 0x1E0425DE / 0xF6CBD06E / 0x63E273F4
//     (TestCascadeLeaves).
// 12. rt_capture_cascade_tree_leaves' fade against the game's leaf shadow pass 0x5851CA62 + 0xA97EF2E0 (TestLeafFade).
// 13. Coloured Shadows, the rays' AO and (Rays Replace Shadow Maps) sun visibility in place of the game's in
//     shaders/sunlight00-03 (TestSunTint).
// 14. autoexposure00's Modded Auto Exposure against the game's 0x8233E11F (TestAutoExposure).
// 15. indicator00-02's Indicator Brightness against the game's 0xD12FA660 / 0xC454C1EA / 0xC57C222D (TestIndicators).
// 16. campaignoverlay00 at 33% against 100% and the game's 0xD7E27B35 (TestCampaignOverlay).
// 17. rt_particle_quads (Smoke Shadows' quad shader for 0xBEA3F2D4's outputs) on synthetic quads (TestParticleQuads).
// 18. rt_capture_displacement / rt_capture_terrain_patches (Rarer Objects in the Rays) against the game's tessellated draws
//     0x5074655A / 0xC24D8705 with their hull and domain shaders (TestPatches; the csos' folder third).
// 19. sss_soft_mask (SSS Softness, Softer Away from the Caster, Debug View) on synthetic masks (TestSoftMask).
// 20. Light Through Banners' replay against the game's cloth G-buffer draw 0xBDFF60D4 + 0x0FBCC65A (TestBannerReplay; the csos'
//     folder third).
// 21. ssrresolve00 against the game's 0xF87ECE85: unbound the game's, the rays' AO in place of the game's, one occlusion on
//     the environment under Ray-Traced Specular Occlusion, the radiance (t9) unread under the rays' reflections (TestResolve;
//     the csos' folder third).
// 22. rt_ambient_into_game (Ray-Traced AO into the game's SSAO target for its other readers) (TestAmbientIntoGame).
#include <windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <vector>

// __rt_outside_view_leaf: the geometry shader rt_core streams Shogun 2's cloth and sprite soldiers out of view through.
#include "../../src/games/twpharaohdynasties/rt_outside_view_leaf_gs.h"
// __rt_outside_view_world: the same for its soldiers and horses (their world alone).
#include "../../src/games/twpharaohdynasties/rt_outside_view_world_gs.h"
// ShaderInjectData: the HDR addon's b13 (parts 8-9's Water Clarity and Caustics).
#include "../../src/games/twshogun2completeedition/shared.h"

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

// 5. rt_capture_tree_billboard (far trees as sun-facing captures, in clip space as the game's) against the game's own
//    rigidtreebillboard vertex shader (0xFAAE535B's cso, streamed out: its SV_Position and o3.zw view and blend) with the
//    camera put toward the sun, looking at the tree: for 8 sun ways x 8 tree facings the capture's card is the game's
//    and at a right angle to the sun's way, its UV the game's nearer view (u - 0.25 x view), a faded card (v4.w 0) a
//    NaN. CONTROL: the game's views with the camera on the far side differ.
// 6. rt_capture_imposter (sprite soldiers turned to the sun) against the game's imposter vertex shader (0x03FB2979's cso)
//    with the camera level with the soldier on the sun's way: standing and flat cards are the game's, so is the UV.
//    CONTROL: with the camera a quarter turn away the game's cards differ.
struct BillboardVertex {
  std::array<float, 4> position, normal;
  std::array<float, 2> uv;
  std::array<float, 4> colour, place, facing;
};

static const D3D11_INPUT_ELEMENT_DESC BILLBOARD_LAYOUT[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0}};

// The imposter's vertex: POSITION0-3 (POSITION1, the last frame's place, unused here) and TEXCOORD0.
struct ImposterVertex {
  std::array<float, 4> place;
  std::array<float, 3> last_place;
  std::array<float, 4> frame, corner, flat;
};

static const D3D11_INPUT_ELEMENT_DESC IMPOSTER_LAYOUT[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 60, D3D11_INPUT_PER_VERTEX_DATA, 0}};

// One output (`semantic`, `components` of it) of a vertex shader for each of `vertices` (points), streamed out; its
// constants at b0.., rt_sun at t0. With `geometry`: that geometry shader's output instead (`vertices` a triangle list), its
// constants at b0.
template <typename Vertex>
static std::vector<std::array<float, 4>> StreamOut(const void* code, size_t size, const char* semantic, UINT index, BYTE components,
                                                   std::span<const D3D11_INPUT_ELEMENT_DESC> layout, const std::vector<Vertex>& vertices,
                                                   std::span<ID3D11Buffer* const> constants, ID3D11ShaderResourceView* sun,
                                                   std::span<const BYTE> geometry = {}, ID3D11Buffer* geometry_constants = nullptr) {
  const D3D11_SO_DECLARATION_ENTRY entry = {.Stream = 0, .SemanticName = semantic, .SemanticIndex = index, .StartComponent = 0,
                                            .ComponentCount = components, .OutputSlot = 0};
  const UINT stride = 16;
  ComPtr<ID3D11GeometryShader> stream;
  device->CreateGeometryShaderWithStreamOutput(geometry.empty() ? code : geometry.data(), geometry.empty() ? size : geometry.size(), &entry, 1,
                                               &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, stream.GetAddressOf());
  ComPtr<ID3D11VertexShader> vertex;
  device->CreateVertexShader(code, size, nullptr, vertex.GetAddressOf());
  ComPtr<ID3D11InputLayout> input;
  device->CreateInputLayout(layout.data(), static_cast<UINT>(layout.size()), code, size, input.GetAddressOf());
  const auto count = static_cast<UINT>(vertices.size());
  const D3D11_BUFFER_DESC vertex_desc = {.ByteWidth = count * static_cast<UINT>(sizeof(Vertex)), .Usage = D3D11_USAGE_DEFAULT,
                                         .BindFlags = D3D11_BIND_VERTEX_BUFFER};
  const D3D11_SUBRESOURCE_DATA vertex_data = {.pSysMem = vertices.data()};
  ComPtr<ID3D11Buffer> vertex_buffer;
  device->CreateBuffer(&vertex_desc, &vertex_data, vertex_buffer.GetAddressOf());
  const D3D11_BUFFER_DESC out_desc = {.ByteWidth = count * 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_STREAM_OUTPUT};
  ComPtr<ID3D11Buffer> out;
  device->CreateBuffer(&out_desc, nullptr, out.GetAddressOf());
  const UINT vertex_stride = sizeof(Vertex);
  const UINT offset = 0;
  context->IASetInputLayout(input.Get());
  context->IASetPrimitiveTopology(geometry.empty() ? D3D11_PRIMITIVE_TOPOLOGY_POINTLIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &vertex_stride, &offset);
  context->VSSetShader(vertex.Get(), nullptr, 0);
  context->VSSetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.data());
  context->VSSetShaderResources(0, 1, &sun);
  context->GSSetShader(stream.Get(), nullptr, 0);
  context->GSSetConstantBuffers(0, 1, &geometry_constants);
  context->SOSetTargets(1, out.GetAddressOf(), &offset);
  context->Draw(count, 0);
  context->ClearState();
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = count * 16, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf());
  context->CopyResource(staging.Get(), out.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<std::array<float, 4>> result(count);
  std::memcpy(result.data(), mapped.pData, count * 16);
  context->Unmap(staging.Get(), 0);
  return result;
}

// A game shader's cso (`folder`/0x<name>.vs_5_0.cso) and our capture version (twshogun2completeedition/<capture>.vs_5_0.hlsl).
static std::pair<std::vector<char>, ComPtr<ID3DBlob>> Shaders(const std::string& games, const std::string& folder, const char* name,
                                                            const wchar_t* capture_name) {
  std::ifstream file(folder + "/0x" + name + ".vs_5_0.cso", std::ios::binary);
  std::vector<char> game_code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const std::wstring path(games.begin(), games.end());
  ComPtr<ID3DBlob> capture;
  ComPtr<ID3DBlob> errors;
  if (game_code.empty()
      || FAILED(D3DCompileFromFile((path + L"/twshogun2completeedition/" + capture_name + L".vs_5_0.hlsl").c_str(), nullptr,
                                   D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                   capture.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("missing 0x%s, or %s\n", name, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    std::exit(2);
  }
  return {std::move(game_code), capture};
}

static ComPtr<ID3D11ShaderResourceView> SunView(const std::array<float, 4>& toward_sun) {
  const std::vector<std::array<float, 4>> texels(9, toward_sun);
  const auto texture = Texture(9, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, texels.data(), 9 * 16, D3D11_BIND_SHADER_RESOURCE);
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf());
  return view;
}

static void TestBillboard(const std::string& games, const std::string& folder) {
  const auto [game_code, capture] = Shaders(games, folder, "FAAE535B", L"rt_capture_tree_billboard");
  size_t checked = 0, bad_view = 0, bad_card = 0, far_side_differs = 0;
  bool faded_nan = false;
  for (int sun_way = 0; sun_way < 8; ++sun_way) {
    const float sun_angle = (static_cast<float>(sun_way) * 0.785398f) + 0.1f;
    const std::array<float, 4> toward_sun = {std::cos(sun_angle) * 0.8f, 0.6f, std::sin(sun_angle) * 0.8f, 1.f};
    const auto sun_view = SunView(toward_sun);
    for (int turn = 0; turn < 8; ++turn) {
      const float facing_angle = (static_cast<float>(turn) * 0.785398f) + 0.3f;
      const std::array<float, 4> place = {100.f, 5.f, -40.f, 1.f};
      std::vector<BillboardVertex> vertices;
      for (int corner = 0; corner < 4; ++corner) {
        vertices.push_back({.position = {(corner & 1) ? 1.f : -1.f, (corner & 2) ? 1.f : 0.f, 0.f, 1.f}, .normal = {0.f, 0.f, 1.f, 0.f},
                            .uv = {0.8f, 0.5f}, .colour = {1.f, 1.f, 1.f, 1.f}, .place = place,
                            .facing = {std::cos(facing_angle), 0.f, std::sin(facing_angle), 10.f}});
      }
      vertices.push_back(vertices[0]);
      vertices.back().place[3] = 0.f;  // faded out
      // The view and view_projection the identity (clip space is the world), the view's forward (_m02, _m22: c4.xz) from
      // the sun to the tree, as a camera at the sun looks.
      std::array<float, 44 * 4> camera = {};
      for (int row = 0; row < 4; ++row) camera[((2 + row) * 4) + row] = camera[((10 + row) * 4) + row] = 1.f;
      const float across = std::hypot(toward_sun[0], toward_sun[2]);
      camera[(4 * 4) + 0] = -toward_sun[0] / across;
      camera[(4 * 4) + 2] = -toward_sun[2] / across;
      for (const float away : {1000.f, -1000.f}) {
        camera[0] = place[0] + (toward_sun[0] * away);
        camera[1] = 50.f;
        camera[2] = place[2] + (toward_sun[2] * away);
        const auto camera_buffer = Constants(camera.data(), sizeof(camera));
        const std::array<ID3D11Buffer*, 1> constants = {camera_buffer.Get()};
        const auto game = StreamOut(game_code.data(), game_code.size(), "TEXCOORD", 1, 4, BILLBOARD_LAYOUT, vertices, constants, sun_view.Get());
        const auto game_card = StreamOut(game_code.data(), game_code.size(), "SV_POSITION", 0, 4, BILLBOARD_LAYOUT, vertices, constants, sun_view.Get());
        const auto ours = StreamOut(capture->GetBufferPointer(), capture->GetBufferSize(), "SV_POSITION", 0, 4, BILLBOARD_LAYOUT, vertices,
                                    constants, sun_view.Get());
        const auto our_uv = StreamOut(capture->GetBufferPointer(), capture->GetBufferSize(), "TEXCOORD", 1, 2, BILLBOARD_LAYOUT, vertices,
                                      constants, sun_view.Get());
        for (int corner = 0; corner < 4; ++corner) {
          const float game_view = game[corner][2] + ((game[corner][3] > 0.5f) ? 1.f : 0.f);
          const float our_view = std::round((0.8f - our_uv[corner][0]) / 0.25f);
          if (away < 0.f) {
            far_side_differs += (game_view != our_view) ? 1 : 0;
            continue;
          }
          ++checked;
          bad_view += (game_view == our_view && our_uv[corner][1] == 0.5f) ? 0 : 1;
          const float along_sun = ((ours[corner][0] - place[0]) * toward_sun[0]) + ((ours[corner][2] - place[2]) * toward_sun[2]);
          const bool game_card_same = std::fabs(ours[corner][0] - game_card[corner][0]) < 1e-3f && std::fabs(ours[corner][1] - game_card[corner][1]) < 1e-3f
                                      && std::fabs(ours[corner][2] - game_card[corner][2]) < 1e-3f && ours[corner][3] == game_card[corner][3];
          bad_card += (std::fabs(along_sun) < 1e-3f && game_card_same) ? 0 : 1;
        }
        faded_nan = faded_nan || (away > 0.f && std::isnan(ours[4][0]));
      }
    }
  }
  CHECK(bad_view == 0 && bad_card == 0 && checked == 256, "views %zu, cards %zu of %zu", bad_view, bad_card, checked);
  CHECK(faded_nan, "a faded card not a NaN");
  CHECK(far_side_differs > 0, "CONTROL: the far side's views all matched");
  std::printf("rt_capture_tree_billboard: %zu corners the game's view toward the sun, %zu of 256 differ from the far side\n", checked,
              far_side_differs);
}

static void TestImposter(const std::string& games, const std::string& folder) {
  const auto [game_code, capture] = Shaders(games, folder, "03FB2979", L"rt_capture_imposter");
  const std::array<float, 4> size = {0.125f, 0.25f, 0.f, 0.f};
  const auto size_buffer = Constants(size.data(), sizeof(size));
  const std::array<float, 4> place = {30.f, 2.f, 70.f, 3.f};
  std::vector<ImposterVertex> vertices;
  for (int corner = 0; corner < 6; ++corner) {
    const float side = (corner & 1) ? 0.6f : -0.6f;
    vertices.push_back({.place = place, .last_place = {place[0], place[1], place[2]},
                        .frame = {0.375f + (0.01f * static_cast<float>(corner)), 0.5f, 0.f, (corner < 4) ? 0.5f : -0.5f},
                        .corner = {(corner & 1) ? 1.f : 0.f, (corner & 2) ? 1.f : 0.f, side, 0.f},
                        .flat = {0.f, side, (corner & 2) ? 0.9f : 0.f, (corner & 2) ? 1.8f : 0.f}});
  }
  size_t checked = 0, bad_card = 0, bad_uv = 0, turned_differs = 0;
  for (int sun_way = 0; sun_way < 8; ++sun_way) {
    const float sun_angle = (static_cast<float>(sun_way) * 0.785398f) + 0.2f;
    const std::array<float, 4> toward_sun = {std::cos(sun_angle) * 0.7f, 0.714f, std::sin(sun_angle) * 0.7f, 1.f};
    const auto sun_view = SunView(toward_sun);
    for (const float turn : {0.f, 1.5708f}) {
      std::array<float, 44 * 4> camera = {};
      for (int row = 0; row < 4; ++row) camera[((2 + row) * 4) + row] = camera[((10 + row) * 4) + row] = 1.f;
      camera[0] = place[0] + (std::cos(sun_angle + turn) * 500.f);
      camera[1] = place[1];
      camera[2] = place[2] + (std::sin(sun_angle + turn) * 500.f);
      const auto camera_buffer = Constants(camera.data(), sizeof(camera));
      const std::array<ID3D11Buffer*, 2> constants = {camera_buffer.Get(), size_buffer.Get()};
      const auto run = [&](const void* code, size_t code_size, const char* semantic, UINT index, BYTE components) {
        return StreamOut(code, code_size, semantic, index, components, IMPOSTER_LAYOUT, vertices, constants, sun_view.Get());
      };
      const auto game_card = run(game_code.data(), game_code.size(), "SV_POSITION", 0, 4);
      const auto game_uv = run(game_code.data(), game_code.size(), "TEXCOORD", 1, 2);
      const auto ours = run(capture->GetBufferPointer(), capture->GetBufferSize(), "SV_POSITION", 0, 4);
      const auto our_uv = run(capture->GetBufferPointer(), capture->GetBufferSize(), "TEXCOORD", 1, 2);
      for (size_t corner = 0; corner < vertices.size(); ++corner) {
        bool same = true;
        for (int axis = 0; axis < 4; ++axis) same = same && std::fabs(ours[corner][axis] - game_card[corner][axis]) < 1e-3f;
        if (turn != 0.f) {
          turned_differs += same ? 0 : 1;
          continue;
        }
        ++checked;
        bad_card += same ? 0 : 1;
        bad_uv += (std::fabs(our_uv[corner][0] - game_uv[corner][0]) < 1e-6f && std::fabs(our_uv[corner][1] - game_uv[corner][1]) < 1e-6f) ? 0 : 1;
      }
    }
  }
  CHECK(bad_card == 0 && bad_uv == 0 && checked == 48, "cards %zu, UVs %zu of %zu", bad_card, bad_uv, checked);
  CHECK(turned_differs > 0, "CONTROL: the turned camera's cards all matched");
  std::printf("rt_capture_imposter: %zu corners (32 standing, 16 flat) the game's toward the sun, %zu of 48 differ a quarter turn away\n",
              checked, turned_differs);
}

// 7. rt_water_plane (Ray-Traced Water Reflections' water surface) for a camera 50 m over the sea (the plane y 0) looking
//    down: the seabed half (y -3) and the sky's far depth get the plane's depth (the CPU's), the water's guides (normal up,
//    specular 0.02, roughness 0.03) and mask 1; on the land half (y +2) a band of water plane mesh (y 2.5, nearer: its depth)
//    is water too, a band under the land (y 1) is not; the rest of the land keeps its depth and guides, mask 0. CONTROL: a
//    plane over the camera and no meshes mark nothing.
static std::array<std::array<double, 4>, 4> Inverse(std::array<std::array<double, 4>, 4> matrix) {
  std::array<std::array<double, 4>, 4> inverse = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  for (int column = 0; column < 4; ++column) {
    int pivot = column;
    for (int row = column + 1; row < 4; ++row) pivot = (std::fabs(matrix[row][column]) > std::fabs(matrix[pivot][column])) ? row : pivot;
    std::swap(matrix[column], matrix[pivot]);
    std::swap(inverse[column], inverse[pivot]);
    const double scale = 1.0 / matrix[column][column];
    for (int index = 0; index < 4; ++index) {
      matrix[column][index] *= scale;
      inverse[column][index] *= scale;
    }
    for (int row = 0; row < 4; ++row) {
      const double factor = (row == column) ? 0.0 : matrix[row][column];
      for (int index = 0; index < 4; ++index) {
        matrix[row][index] -= factor * matrix[column][index];
        inverse[row][index] -= factor * inverse[column][index];
      }
    }
  }
  return inverse;
}

static ComPtr<ID3D11UnorderedAccessView> Uav(ID3D11Texture2D* texture) {
  ComPtr<ID3D11UnorderedAccessView> view;
  device->CreateUnorderedAccessView(texture, nullptr, view.GetAddressOf());
  return view;
}

static ComPtr<ID3D11ShaderResourceView> Srv(ID3D11Resource* resource) {
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(resource, nullptr, view.GetAddressOf());
  return view;
}

static void TestWaterPlane(const std::string& games) {
  const auto shader = Compile(games + "/twshogun2completeedition/rt_water_plane.cs_5_0.hlsl");
  constexpr UINT SIZE = 64;
  constexpr double HEIGHT = 50.0;
  constexpr double FOCAL = 1.5;
  const double a = 1000.0 / 999.0;
  const double b = -a;
  // clip = (x, y, z, 1) M: looking down from (0, 50, 0), screen up along +z, depth a + b / (50 - y); its columns at c10-13.
  const std::array<std::array<double, 4>, 4> matrix = {{{FOCAL, 0, 0, 0}, {0, 0, -a, -1}, {0, FOCAL, 0, 0}, {0, 0, (a * HEIGHT) + b, HEIGHT}}};
  const auto inverse = Inverse(matrix);
  std::array<float, 34 * 4> camera = {};
  camera[1] = static_cast<float>(HEIGHT);
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      camera[((10 + column) * 4) + row] = static_cast<float>(matrix[row][column]);
      camera[((30 + column) * 4) + row] = static_cast<float>(inverse[row][column]);
    }
  }
  const auto camera_buffer = Constants(camera.data(), sizeof(camera));
  const auto depth_at = [&](double y) { return static_cast<float>(a + (b / (HEIGHT - y))); };
  std::vector<float> depth(SIZE * SIZE);
  std::vector<float> meshes(SIZE * SIZE, 1.f);
  std::vector<float> expected(SIZE * SIZE);  // the water's depth, or 0: land
  for (UINT y = 0; y < SIZE; ++y) {
    for (UINT x = 0; x < SIZE; ++x) {
      const size_t pixel = (y * SIZE) + x;
      const bool sea = ((x + 0.5) / SIZE * 2.0) - 1.0 < 0.0;
      const bool sky = 1.0 - ((y + 0.5) / SIZE * 2.0) > 0.75;
      depth[pixel] = sky ? 1.f : depth_at(sea ? -3.0 : 2.0);
      if (!sea && !sky && y >= 20 && y < 32) meshes[pixel] = depth_at((y < 28) ? 2.5 : 1.0);
      expected[pixel] = (sky || sea) ? depth_at(0.0) : ((meshes[pixel] < depth[pixel]) ? meshes[pixel] : 0.f);
    }
  }
  const auto depth_view = Srv(Texture(SIZE, SIZE, DXGI_FORMAT_R32_FLOAT, depth.data(), SIZE * 4, D3D11_BIND_SHADER_RESOURCE).Get());
  const auto meshes_view = Srv(Texture(SIZE, SIZE, DXGI_FORMAT_R32_FLOAT, meshes.data(), SIZE * 4, D3D11_BIND_SHADER_RESOURCE).Get());
  size_t marked = 0, by_mesh = 0, bad = 0, control_marked = 0, soft_bad = 0;
  // The control, then Softer Water Reflections' roughness (0.06: the water's guide 15 / 255, the rest as without it).
  for (const int variant : {0, 1, 2}) {
    const bool control = variant == 1;
    const bool soft = variant == 2;
    const std::vector<uint8_t> grey(SIZE * SIZE * 4, 0x80);
    const std::vector<float> nines(SIZE * SIZE * 4, 9.f);
    const std::array<ComPtr<ID3D11Texture2D>, 6> targets = {
        Texture(SIZE, SIZE, DXGI_FORMAT_R32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS),
        Texture(SIZE, SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, grey.data(), SIZE * 4, D3D11_BIND_UNORDERED_ACCESS),
        Texture(SIZE, SIZE, DXGI_FORMAT_R8G8B8A8_UNORM, grey.data(), SIZE * 4, D3D11_BIND_UNORDERED_ACCESS),
        Texture(SIZE, SIZE, DXGI_FORMAT_R32G32B32A32_FLOAT, nines.data(), SIZE * 16, D3D11_BIND_UNORDERED_ACCESS),
        Texture(SIZE, SIZE, DXGI_FORMAT_R8_UNORM, grey.data(), SIZE, D3D11_BIND_UNORDERED_ACCESS),
        Texture(SIZE, SIZE, DXGI_FORMAT_R8_UNORM, grey.data(), SIZE, D3D11_BIND_UNORDERED_ACCESS)};
    std::array<ComPtr<ID3D11UnorderedAccessView>, 6> views;
    std::array<ID3D11UnorderedAccessView*, 6> bound = {};
    for (size_t index = 0; index < views.size(); ++index) {
      views[index] = Uav(targets[index].Get());
      bound[index] = views[index].Get();
    }
    const std::array<uint32_t, 12> plane = {0u, Bits(1.f), 0u, Bits(control ? -60.f : 0.f), SIZE, SIZE, 1u, control ? 0u : 1u,
                                            Bits(soft ? 0.06f : 0.f)};
    const auto plane_buffer = Constants(plane.data(), sizeof(plane));
    const std::array<ID3D11Buffer*, 2> buffers = {camera_buffer.Get(), plane_buffer.Get()};
    const std::array<ID3D11ShaderResourceView*, 2> inputs = {depth_view.Get(), meshes_view.Get()};
    context->CSSetShader(shader.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 2, inputs.data());
    context->CSSetConstantBuffers(0, 2, buffers.data());
    context->CSSetUnorderedAccessViews(0, 6, bound.data(), nullptr);
    context->Dispatch(SIZE / 8, SIZE / 8, 1);
    context->ClearState();
    const auto surface = Read(targets[0].Get(), 4);
    const auto specular = Read(targets[2].Get(), 4);
    const auto normals = Read(targets[3].Get(), 16);
    const auto roughness = Read(targets[4].Get(), 1);
    const auto mask = Read(targets[5].Get(), 1);
    for (size_t pixel = 0; pixel < depth.size(); ++pixel) {
      float surface_depth = 0.f;
      std::array<float, 4> normal = {};
      std::memcpy(&surface_depth, surface.data() + (pixel * 4), 4);
      std::memcpy(normal.data(), normals.data() + (pixel * 16), 16);
      if (control) {
        control_marked += (mask[pixel] != 0 || surface_depth != depth[pixel]) ? 1 : 0;
        continue;
      }
      if (soft) {
        soft_bad += (roughness[pixel] != ((expected[pixel] != 0.f) ? 15 : 0x80)) ? 1 : 0;
        continue;
      }
      marked += (mask[pixel] == 255) ? 1 : 0;
      by_mesh += (mask[pixel] == 255 && meshes[pixel] == surface_depth) ? 1 : 0;
      const bool water_ok = std::fabs(surface_depth - expected[pixel]) < 1e-5f && mask[pixel] == 255 && normal[0] == 0.f && normal[1] == 1.f
                            && normal[2] == 0.f && roughness[pixel] == 8 && specular[pixel * 4] == 5;
      const bool land_ok = surface_depth == depth[pixel] && mask[pixel] == 0 && normal[1] == 9.f && roughness[pixel] == 0x80 && specular[pixel * 4] == 0x80;
      bad += ((expected[pixel] != 0.f) ? water_ok : land_ok) ? 0 : 1;
    }
  }
  const auto water = static_cast<size_t>(std::ranges::count_if(expected, [](float value) { return value != 0.f; }));
  CHECK(bad == 0 && marked == water && by_mesh > 0, "%zu pixels wrong, %zu marked of %zu (%zu by the meshes)", bad, marked, water, by_mesh);
  CHECK(control_marked == 0, "CONTROL: %zu pixels marked under a plane over the camera, no meshes", control_marked);
  CHECK(soft_bad == 0, "Softer Water Reflections: %zu pixels' roughness not 15 on water / kept elsewhere", soft_bad);
  std::printf("rt_water_plane %ux%u: %zu water pixels (%zu the meshes') their depth and guides, the rest kept; 0 in the control; "
              "Softer Water Reflections' roughness 0.06 on them alone\n", SIZE, SIZE, marked, by_mesh);
}

// A water pixel shader of the game's and ours (shaders/), what their draw needs: the vertex shader giving their inputs,
// the slots of their textures by kind, the cbuffers at b3 and (not empty) b4.
struct WaterShader {
  const char* hash;
  const wchar_t* ours;
  const char* vertex_source;
  std::vector<UINT> plain_slots;  // RGBA 8 x 8 (the harmonics' 9 x 9)
  UINT harmonics_slot;
  std::vector<UINT> single_slots;  // one channel
  UINT cube_slot;
  std::vector<UINT> cube_array_slots;  // the probe grid's 4 cubes
  UINT samplers;
  std::vector<float> scalars;        // b3
  std::vector<float> ocean_scalars;  // b4 (ocean ultra's ocean_PS)
};

// #include from the folders of the files opened so far, the latest first, as fxc finds shared.h's ../../shaders/renodx.hlsl
// (D3D_COMPILE_STANDARD_FILE_INCLUDE looked from the first file's folder; the compiler's parent pointers aren't the buffers given).
class FolderInclude : public ID3DInclude {
 public:
  explicit FolderInclude(const std::filesystem::path& root) : folders_{root} {}
  HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* bytes) noexcept override {
    const auto folder = std::find_if(folders_.rbegin(), folders_.rend(),
                                     [name](const std::filesystem::path& candidate) { return std::filesystem::exists(candidate / name); });
    if (folder == folders_.rend()) return E_FAIL;
    const std::filesystem::path path = std::filesystem::weakly_canonical(*folder / name);
    if (std::ranges::find(folders_, path.parent_path()) == folders_.end()) folders_.push_back(path.parent_path());
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto* copy = new char[text.size() + 1];
    std::memcpy(copy, text.data(), text.size());
    *data = copy;
    *bytes = static_cast<UINT>(text.size());
    return S_OK;
  }
  HRESULT __stdcall Close(LPCVOID data) noexcept override {
    delete[] static_cast<const char*>(data);
    return S_OK;
  }

 private:
  std::vector<std::filesystem::path> folders_;
};

// 8-9. shaders/ocean00-03 and waterplane00-01 (the rays' reflection in the water's pixel shaders: every water quality) against
//      the game's (0x16ADFD60's, 0x6B8954A6's, 0x5487502A's, 0xE73D4A5E's, 0x9B9EC116's, 0x892D56F9's csos) on a 32 x 32 target, same inputs, in the probe and the sky branch: the rays unbound, bound with no
//      hits (share 0), or flagged off (b10 0: CONTROL): the five outputs the game's; the rays' light (share 1, flags 1): the
//      colour changes where the mask is 1 and nowhere else. waterplane00 / 01's Water Clarity and Caustics (b13): at clarity 1,
//      caustics 0 the game's outputs; clarity 4 and caustics 1 each change the colour (CONTROL that they reach it).
static void TestWater(const std::string& games, const std::string& folder, const WaterShader& water) {
  std::ifstream file(folder + "/0x" + water.hash + ".ps_5_0.cso", std::ios::binary);
  const std::vector<char> game_code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const std::wstring path(games.begin(), games.end());
  ComPtr<ID3DBlob> vertex_code;
  ComPtr<ID3DBlob> ours_code;
  ComPtr<ID3DBlob> errors;
  FolderInclude include(path + L"/twshogun2completeedition/shaders");
  if (game_code.empty()
      || FAILED(D3DCompile(water.vertex_source, std::strlen(water.vertex_source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0,
                           vertex_code.GetAddressOf(), errors.GetAddressOf()))
      || FAILED(D3DCompileFromFile((path + L"/twshogun2completeedition/shaders/" + water.ours + L".ps_5_0.hlsl").c_str(), nullptr,
                                   &include, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, ours_code.GetAddressOf(),
                                   errors.GetAddressOf()))) {
    std::printf("missing 0x%s, or %s\n", water.hash, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    std::exit(2);
  }
  ComPtr<ID3D11VertexShader> vertex;
  ComPtr<ID3D11PixelShader> game;
  ComPtr<ID3D11PixelShader> ours;
  device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex.GetAddressOf());
  device->CreatePixelShader(game_code.data(), game_code.size(), nullptr, game.GetAddressOf());
  device->CreatePixelShader(ours_code->GetBufferPointer(), ours_code->GetBufferSize(), nullptr, ours.GetAddressOf());
  constexpr UINT SIZE = 32;
  std::mt19937 random(7);
  std::uniform_real_distribution<float> unit(0.05f, 0.95f);
  const auto values = [&](size_t count) {
    std::vector<float> filled(count);
    for (auto& value : filled) value = unit(random);
    return filled;
  };
  std::array<ComPtr<ID3D11ShaderResourceView>, 16> views;
  UINT view_count = 0;
  for (const UINT slot : water.plain_slots) {
    const UINT width = (slot == water.harmonics_slot) ? 9 : 8;
    const auto texels = values(static_cast<size_t>(width) * width * 4);
    views[slot] = Srv(Texture(width, width, DXGI_FORMAT_R32G32B32A32_FLOAT, texels.data(), width * 16, D3D11_BIND_SHADER_RESOURCE).Get());
    view_count = std::max(view_count, slot + 1);
  }
  for (const UINT slot : water.single_slots) {
    const auto texels = values(64);
    views[slot] = Srv(Texture(8, 8, DXGI_FORMAT_R32_FLOAT, texels.data(), 32, D3D11_BIND_SHADER_RESOURCE).Get());
    view_count = std::max(view_count, slot + 1);
  }
  std::vector<UINT> cube_slots = water.cube_array_slots;
  cube_slots.push_back(water.cube_slot);
  for (const UINT slot : cube_slots) {
    const bool single_cube = slot == water.cube_slot;
    const UINT faces = single_cube ? 6 : 24;
    const D3D11_TEXTURE2D_DESC desc = {.Width = 4, .Height = 4, .MipLevels = 1, .ArraySize = faces, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                       .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                       .MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE};
    std::vector<std::vector<float>> face_texels(faces);
    std::vector<D3D11_SUBRESOURCE_DATA> initial(faces);
    for (UINT face = 0; face < faces; ++face) {
      face_texels[face] = values(64);
      initial[face] = {.pSysMem = face_texels[face].data(), .SysMemPitch = 64};
    }
    ComPtr<ID3D11Texture2D> cube;
    device->CreateTexture2D(&desc, initial.data(), cube.GetAddressOf());
    D3D11_SHADER_RESOURCE_VIEW_DESC view = {.Format = desc.Format,
                                            .ViewDimension = single_cube ? D3D11_SRV_DIMENSION_TEXTURECUBE : D3D11_SRV_DIMENSION_TEXTURECUBEARRAY};
    if (single_cube) {
      view.TextureCube = {.MipLevels = 1};
    } else {
      view.TextureCubeArray = {.MipLevels = 1, .NumCubes = 4};
    }
    device->CreateShaderResourceView(cube.Get(), &view, views[slot].GetAddressOf());
    view_count = std::max(view_count, slot + 1);
  }
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_WRAP, .AddressW = D3D11_TEXTURE_ADDRESS_WRAP, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf());
  const std::vector<ID3D11SamplerState*> samplers(water.samplers, sampler.Get());
  std::array<float, 44 * 4> camera = {};
  camera[1] = 30.f;
  for (int row = 0; row < 4; ++row) camera[((2 + row) * 4) + row] = camera[((10 + row) * 4) + row] = 1.f;
  camera[35 * 4] = 1.5f;  // time_in_sec
  camera[(37 * 4) + 0] = camera[(37 * 4) + 1] = SIZE;
  camera[(37 * 4) + 2] = camera[(37 * 4) + 3] = 1.f / SIZE;
  std::array<float, 7 * 4> environment = {0.3f, -0.8f, 0.5f, 0.f, 1.f, 10.f, 0.8f, 0.5f, 1.f, 1.f, 1.f, 1.f, -20.f, -20.f, 20.f, 20.f};
  environment[16] = std::bit_cast<float>(2u);  // g_probe_grid_size
  environment[18] = 0.5f;                      // g_specular_probe_lerp
  environment[23] = 9.f;                       // g_spherical_harmonic_terms
  std::array<float, 18 * 4> lighting = {};
  const auto lighting_values = values(lighting.size());
  std::copy(lighting_values.begin(), lighting_values.end(), lighting.begin());
  lighting[0] = 0.3f;
  lighting[1] = -0.8f;
  lighting[2] = 0.5f;
  lighting[7] = 1.f;                        // sun_specular
  lighting[43] = std::bit_cast<float>(1u);  // g_use_spherical_harmonics
  lighting[47] = 0.f;                       // g_cloud_shadow_scale
  const auto camera_buffer = Constants(camera.data(), sizeof(camera));
  const auto lighting_buffer = Constants(lighting.data(), sizeof(lighting));
  const auto scalars_buffer = Constants(water.scalars.data(), static_cast<UINT>(water.scalars.size() * sizeof(float)));
  const auto ocean_buffer = water.ocean_scalars.empty()
                                ? ComPtr<ID3D11Buffer>()
                                : Constants(water.ocean_scalars.data(), static_cast<UINT>(water.ocean_scalars.size() * sizeof(float)));
  std::vector<float> traced(SIZE * SIZE * 4);
  std::vector<float> mask(SIZE * SIZE);
  for (size_t pixel = 0; pixel < mask.size(); ++pixel) {
    traced[(pixel * 4) + 0] = 3.f;
    traced[(pixel * 4) + 1] = 0.5f;
    traced[(pixel * 4) + 2] = 0.1f;
    mask[pixel] = ((pixel % SIZE) < SIZE / 2) ? 1.f : 0.f;
  }
  const auto mask_view = Srv(Texture(SIZE, SIZE, DXGI_FORMAT_R32_FLOAT, mask.data(), SIZE * 4, D3D11_BIND_SHADER_RESOURCE).Get());
  const auto traced_with = [&](float share) {
    for (size_t pixel = 0; pixel < mask.size(); ++pixel) traced[(pixel * 4) + 3] = share;
    return Srv(Texture(SIZE, SIZE, DXGI_FORMAT_R32G32B32A32_FLOAT, traced.data(), SIZE * 16, D3D11_BIND_SHADER_RESOURCE).Get());
  };
  const std::array<ComPtr<ID3D11ShaderResourceView>, 2> rays = {traced_with(0.f), traced_with(1.f)};
  const std::array<uint32_t, 4> flags_on = {1u, 0u, 0u, 0u};
  const std::array<uint32_t, 4> flags_off = {};
  const std::array<ComPtr<ID3D11Buffer>, 2> flags = {Constants(flags_off.data(), 16), Constants(flags_on.data(), 16)};
  // One pass: its five outputs (RGBA x 3, then two singles), as floats.
  const auto run = [&](ID3D11PixelShader* pixel_shader, ID3D11Buffer* environment_buffer, ID3D11ShaderResourceView* rays_view, ID3D11Buffer* flag,
                       ID3D11Buffer* injection = nullptr) {
    std::array<ComPtr<ID3D11Texture2D>, 5> outputs;
    std::array<ComPtr<ID3D11RenderTargetView>, 5> targets;
    std::array<ID3D11RenderTargetView*, 5> bound = {};
    for (size_t index = 0; index < outputs.size(); ++index) {
      outputs[index] = Texture(SIZE, SIZE, (index < 3) ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32_FLOAT, nullptr, 0, D3D11_BIND_RENDER_TARGET);
      device->CreateRenderTargetView(outputs[index].Get(), nullptr, targets[index].GetAddressOf());
      bound[index] = targets[index].Get();
    }
    std::array<ID3D11ShaderResourceView*, 16> bound_views = {};
    for (size_t index = 0; index < views.size(); ++index) bound_views[index] = views[index].Get();
    const std::array<ID3D11Buffer*, 5> buffers = {camera_buffer.Get(), environment_buffer, lighting_buffer.Get(), scalars_buffer.Get(),
                                                  ocean_buffer.Get()};
    const D3D11_VIEWPORT viewport = {.Width = SIZE, .Height = SIZE, .MaxDepth = 1.f};
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex.Get(), nullptr, 0);
    context->PSSetShader(pixel_shader, nullptr, 0);
    context->PSSetShaderResources(0, view_count, bound_views.data());
    context->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
    context->PSSetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
    if (rays_view != nullptr) {
      context->PSSetShaderResources(45, 1, mask_view.GetAddressOf());
      context->PSSetShaderResources(47, 1, &rays_view);
      context->PSSetConstantBuffers(10, 1, &flag);
    }
    if (injection != nullptr) context->PSSetConstantBuffers(13, 1, &injection);
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(5, bound.data(), nullptr);
    context->Draw(3, 0);
    context->ClearState();
    std::array<std::vector<float>, 5> result;
    for (size_t index = 0; index < outputs.size(); ++index) {
      const auto bytes = Read(outputs[index].Get(), (index < 3) ? 16 : 4);
      result[index].resize(bytes.size() / 4);
      std::memcpy(result[index].data(), bytes.data(), bytes.size());
    }
    return result;
  };
  const auto same = [](float x, float y) { return (std::isnan(x) && std::isnan(y)) || std::fabs(x - y) <= (1e-4f * std::max(std::fabs(x), std::fabs(y))) + 1e-5f; };
  size_t differs_unbound = 0, differs_kept = 0, changed = 0, changed_outside = 0, control_changed = 0;
  const bool water_rows = std::wcsncmp(water.ours, L"waterplane", 10) == 0;
  size_t differs_neutral = 0, clearer = 0, caustics = 0;
  const auto injection = [](float clarity, float strength) {
    ShaderInjectData data = {};
    data.water_clarity = clarity;
    data.water_caustics = strength;
    // A constant buffer's size in whole registers.
    std::array<float, ((sizeof(ShaderInjectData) + 15) / 16) * 4> registers = {};
    std::memcpy(registers.data(), &data, sizeof(data));
    return Constants(registers.data(), sizeof(registers));
  };
  const std::array<ComPtr<ID3D11Buffer>, 3> water_settings = {injection(1.f, 0.f), injection(4.f, 0.f), injection(1.f, 1.f)};
  for (const float probes : {1.f, 0.f}) {
    environment[17] = probes;  // g_specular_probes_count
    const auto environment_buffer = Constants(environment.data(), sizeof(environment));
    const auto reference = run(game.Get(), environment_buffer.Get(), nullptr, nullptr);
    const auto unbound = run(ours.Get(), environment_buffer.Get(), nullptr, nullptr);
    const auto no_hits = run(ours.Get(), environment_buffer.Get(), rays[0].Get(), flags[1].Get());
    const auto control = run(ours.Get(), environment_buffer.Get(), rays[1].Get(), flags[0].Get());
    const auto hits = run(ours.Get(), environment_buffer.Get(), rays[1].Get(), flags[1].Get());
    if (water_rows) {
      const auto neutral = run(ours.Get(), environment_buffer.Get(), nullptr, nullptr, water_settings[0].Get());
      const auto clear = run(ours.Get(), environment_buffer.Get(), nullptr, nullptr, water_settings[1].Get());
      const auto rippled = run(ours.Get(), environment_buffer.Get(), nullptr, nullptr, water_settings[2].Get());
      for (size_t output = 0; output < 5; ++output) {
        for (size_t value = 0; value < reference[output].size(); ++value) {
          differs_neutral += same(neutral[output][value], reference[output][value]) ? 0 : 1;
          clearer += same(clear[output][value], reference[output][value]) ? 0 : 1;
          caustics += same(rippled[output][value], reference[output][value]) ? 0 : 1;
        }
      }
    }
    for (size_t output = 0; output < 5; ++output) {
      const size_t channels = (output < 3) ? 4 : 1;
      for (size_t value = 0; value < reference[output].size(); ++value) {
        const float expected = reference[output][value];
        const bool masked = mask[value / channels] > 0.5f;
        differs_unbound += same(unbound[output][value], expected) ? 0 : 1;
        differs_kept += same(no_hits[output][value], expected) ? 0 : 1;
        control_changed += same(control[output][value], expected) ? 0 : 1;
        changed += (masked && output == 0 && !same(hits[output][value], expected)) ? 1 : 0;
        changed_outside += ((!masked || output != 0) && !same(hits[output][value], expected)) ? 1 : 0;
      }
    }
  }
  const std::string name(water.ours, water.ours + std::wcslen(water.ours));
  CHECK(differs_unbound == 0 && differs_kept == 0, "%s: %zu values differ unbound, %zu with no hits", name.c_str(), differs_unbound, differs_kept);
  CHECK(changed > 0 && changed_outside == 0, "%s: %zu colour values changed under the mask, %zu elsewhere", name.c_str(), changed, changed_outside);
  CHECK(control_changed == 0, "CONTROL %s: %zu values changed with the flags off", name.c_str(), control_changed);
  if (water_rows) {
    CHECK(differs_neutral == 0, "%s: %zu values differ at clarity 1, caustics 0", name.c_str(), differs_neutral);
    CHECK(clearer > 0 && caustics > 0, "CONTROL %s: clarity 4 changed %zu values, caustics 1 %zu", name.c_str(), clearer, caustics);
    std::printf("%s: clarity 1, caustics 0 the game's; clarity 4 changes %zu values, caustics 1 %zu\n", name.c_str(), clearer, caustics);
  }
  std::printf("%s 32x32, probe and sky branches: the game's outputs unbound / no hits / flags off; %zu colour values the rays' under the mask\n",
              name.c_str(), changed);
}

static void TestWaters(const std::string& games, const std::string& folder) {
  // The ocean's inputs: COLOR0 and TEXCOORD0-6 (v2 its world position, v7 the view's way and distance).
  const char* ocean_vertex =
      "void main(uint id : SV_VertexID, out float4 p : SV_Position0, out float3 c : COLOR0, out float4 t0 : TEXCOORD0, out float4 t1 : TEXCOORD1,\n"
      "          out float4 t2 : TEXCOORD2, out float4 t3 : TEXCOORD3, out float4 t4 : TEXCOORD4, out float4 t5 : TEXCOORD5, out float4 t6 : TEXCOORD6) {\n"
      "  const float2 uv = float2((id << 1) & 2, id & 2);\n"
      "  p = float4(uv * float2(2, -2) + float2(-1, 1), 0.5, 1);\n"
      "  c = float3(uv, 0.5);\n"
      "  t0 = float4(uv.x * 40 - 20, 0.5, uv.y * 40 - 20, 1);\n"
      "  t1 = p;\n"
      "  t2 = float4(uv * 0.5, 0.5, 1);\n"
      "  t3 = float4(0.8, uv, 0);\n"
      "  t4 = float4(uv * 3, uv * 2);\n"
      "  t5 = float4(normalize(float3(uv.x - 0.5, -0.4, uv.y - 0.5)), 30);\n"
      "  t6 = float4(uv, 10, 1);\n"
      "}\n";
  // The water plane's: TEXCOORD0-6 (v1-v2 its UVs, v4 its world position, v7 the overlay's projection).
  const char* plane_vertex =
      "void main(uint id : SV_VertexID, out float4 p : SV_Position0, out float4 t0 : TEXCOORD0, out float4 t1 : TEXCOORD1,\n"
      "          out float4 t2 : TEXCOORD2, out float4 t3 : TEXCOORD3, out float4 t4 : TEXCOORD4, out float4 t5 : TEXCOORD5, out float4 t6 : TEXCOORD6) {\n"
      "  const float2 uv = float2((id << 1) & 2, id & 2);\n"
      "  p = float4(uv * float2(2, -2) + float2(-1, 1), 0.5, 1);\n"
      "  t0 = float4(uv * 0.7 + 0.4, uv * 0.3);\n"
      "  t1 = float4(uv * 0.5 + 0.4, uv * 0.2);\n"
      "  t2 = float4(uv * 0.02, uv * 0.03);\n"
      "  t3 = float4(uv.x * 40 - 20, 0.5, uv.y * 40 - 20, 1);\n"
      "  t4 = p;\n"
      "  t5 = float4(uv, 10, 1);\n"
      "  t6 = float4(uv * 0.5, 0.5, 1);\n"
      "}\n";
  TestWater(games, folder,
            {.hash = "16ADFD60", .ours = L"ocean00_0x16ADFD60", .vertex_source = ocean_vertex, .plain_slots = {0, 1, 2, 3, 7, 8, 9, 10, 11},
             .harmonics_slot = 7, .single_slots = {12}, .cube_slot = 4, .cube_array_slots = {5, 6}, .samplers = 9, .scalars = {0.3f, 0.7f, 0.f, 0.f}});
  TestWater(games, folder,
            {.hash = "6B8954A6", .ours = L"ocean01_0x6B8954A6", .vertex_source = ocean_vertex, .plain_slots = {0, 1, 2, 3, 7, 8, 9, 10, 11},
             .harmonics_slot = 7, .single_slots = {12}, .cube_slot = 4, .cube_array_slots = {5, 6}, .samplers = 9, .scalars = {0.3f, 0.7f, 0.f, 0.f},
             .ocean_scalars = {0.02f, 50.f, 0.5f, 0.3f, 0.1f, 2.f, 0.f, 0.f}});
  // Low (no harmonics: its slot past the textures) and medium water quality.
  TestWater(games, folder,
            {.hash = "5487502A", .ours = L"ocean02_0x5487502A", .vertex_source = ocean_vertex, .plain_slots = {0, 1, 2, 6, 7, 8},
             .harmonics_slot = 16, .single_slots = {}, .cube_slot = 3, .cube_array_slots = {4, 5}, .samplers = 8, .scalars = {0.3f, 0.7f, 0.f, 0.f}});
  TestWater(games, folder,
            {.hash = "E73D4A5E", .ours = L"ocean03_0xE73D4A5E", .vertex_source = ocean_vertex, .plain_slots = {0, 1, 2, 3, 7, 8, 9, 10, 11},
             .harmonics_slot = 7, .single_slots = {12}, .cube_slot = 4, .cube_array_slots = {5, 6}, .samplers = 9, .scalars = {0.3f, 0.7f, 0.f, 0.f}});
  const std::vector<float> plane_scalars = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.f, 1.f, -1000.f, 1000.f, -1000.f, 1000.f, 0.f};
  TestWater(games, folder,
            {.hash = "9B9EC116", .ours = L"waterplane00_0x9B9EC116", .vertex_source = plane_vertex, .plain_slots = {3, 4, 5, 6, 7, 8},
             .harmonics_slot = 3, .single_slots = {}, .cube_slot = 0, .cube_array_slots = {1, 2}, .samplers = 7, .scalars = plane_scalars});
  // Low water quality: no t_diffuse (the rest one slot down).
  TestWater(games, folder,
            {.hash = "892D56F9", .ours = L"waterplane01_0x892D56F9", .vertex_source = plane_vertex, .plain_slots = {3, 4, 5, 6, 7},
             .harmonics_slot = 3, .single_slots = {}, .cube_slot = 0, .cube_array_slots = {1, 2}, .samplers = 6, .scalars = plane_scalars});
}

// 10. rt_capture_cascade_rigid and rt_rows_cascade_rigid (Shadow Casters Out of View: the shadow pass's rigid casters placed by their
//     instances' rows) against the game's rigidterrainblend vs30_shadow (0xAC25DAB8's cso): 256 random instances and corners, the
//     capture's world position the game's SV_Position with an identity view-projection; with a main view at
//     cb0[10-13] an instance's rows left out (translation 1e30) exactly where the game's own projection of its origin is in that view
//     (|x|, |y| <= w), kept as v6-v8 elsewhere; with cb0 all 0 (a new mesh's first rows) all kept. CONTROL: the view turned away
//     leaves out another set. The same for variantrigid vs30_shadow (0xB0E006EE: rt_capture / rt_rows_cascade_variant, its rows
//     v8-v10) with its sway's weight (v6.w) 0; with it 1 the game's corners stray from ours by the sway alone, under 2 cm. The rigid
//     versions also against rigidambientmapmesh_mtb vs30_shadow (0x7B067623: the same with a TEXCOORD1 it never reads) and
//     rigidburning vs30_shadow (0x124C85A5: the same with a POSITION5 it never reads, POSITION0's w 1 as a mesh's).
struct RigidShadowVertex {
  std::array<std::array<float, 4>, 10> inputs;  // v0-v9
};

static const D3D11_INPUT_ELEMENT_DESC RIGID_SHADOW_LAYOUT[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 112, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 128, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 144, D3D11_INPUT_PER_VERTEX_DATA, 0}};

static ComPtr<ID3DBlob> CompileVertex(const std::string& games, const wchar_t* name) {
  const std::wstring path(games.begin(), games.end());
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompileFromFile((path + L"/twshogun2completeedition/" + name + L".vs_5_0.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("compile failed: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    std::exit(2);
  }
  return code;
}

// variantrigid vs30_shadow's vertex (0xB0E006EE): v0-v15, its rows v8-v10, its sway's weight v6.w, v14 a uint.
struct VariantShadowVertex {
  std::array<std::array<float, 4>, 16> inputs;
};

static const D3D11_INPUT_ELEMENT_DESC VARIANT_SHADOW_LAYOUT[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 112, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 128, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 144, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 160, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 176, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 192, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 208, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 7, DXGI_FORMAT_R32G32B32A32_UINT, 0, 224, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 8, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 240, D3D11_INPUT_PER_VERTEX_DATA, 0}};

// `rows`: the first of the instance's three world rows among the inputs; `sway`: the input whose w weighs the game's wind sway (0: none).
template <typename Vertex>
static void TestPlacedShadow(const std::string& games, const std::string& folder, const char* name, const wchar_t* capture_name,
                             const wchar_t* rows_name, std::span<const D3D11_INPUT_ELEMENT_DESC> layout, size_t rows, size_t sway) {
  std::ifstream file(folder + "/0x" + name + ".vs_5_0.cso", std::ios::binary);
  const std::vector<char> game((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (game.empty()) {
    std::printf("missing 0x%s\n", name);
    std::exit(2);
  }
  const auto capture = CompileVertex(games, capture_name);
  const auto rows_code = CompileVertex(games, rows_name);
  std::mt19937 random(11);
  std::uniform_real_distribution<float> spread(-1.f, 1.f);
  std::vector<Vertex> vertices(256);
  std::vector<Vertex> origins(256);
  for (size_t index = 0; index < vertices.size(); ++index) {
    auto& inputs = vertices[index].inputs;
    for (auto& input : inputs) input = {spread(random), spread(random), spread(random), spread(random)};
    inputs[0][3] = 1.f;
    if (sway != 0) inputs[sway][3] = 0.f;
    // Rows: a turn and a scale about y, the translation in w.
    const float turn = spread(random) * 3.14159f;
    const float scale = 1.f + (0.5f * spread(random));
    inputs[rows] = {std::cos(turn) * scale, 0.f, std::sin(turn) * scale, 100.f * spread(random)};
    inputs[rows + 1] = {0.f, scale, 0.f, 5.f * spread(random)};
    inputs[rows + 2] = {-std::sin(turn) * scale, 0.f, std::cos(turn) * scale, 100.f * spread(random)};
    origins[index] = vertices[index];
    origins[index].inputs[0] = {0.f, 0.f, 0.f, 1.f};
  }
  std::array<float, 44 * 4> identity = {};
  for (int row = 0; row < 4; ++row) identity[((10 + row) * 4) + row] = 1.f;
  identity[35 * 4] = 38.696f;  // time_in_sec (the battle trace's)
  // A main view at c10-13 (each register a row acting on column vectors, as the game's view_projection's columns): looking along +z
  // from (0, 0, -60), 0.8 wide.
  std::array<float, 44 * 4> view = {};
  const std::array<std::array<float, 4>, 4> main_view = {{{1.25f, 0.f, 0.f, 0.f}, {0.f, 1.25f, 0.f, 0.f}, {0.f, 0.f, 1.f, 0.f}, {0.f, 0.f, 1.f, 60.f}}};
  std::array<float, 44 * 4> turned = {};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      view[((10 + row) * 4) + column] = main_view[row][column];
      turned[((10 + row) * 4) + column] = (column == 2) ? -main_view[row][column] : main_view[row][column];
    }
  }
  std::array<float, 44 * 4> zero = {};
  const auto identity_buffer = Constants(identity.data(), sizeof(identity));
  const auto view_buffer = Constants(view.data(), sizeof(view));
  const auto turned_buffer = Constants(turned.data(), sizeof(turned));
  const auto zero_buffer = Constants(zero.data(), sizeof(zero));
  // wind_VS: direction, gust, multipliers and strengths.
  const std::array<float, 12> wind = {0.8f, -0.6f, 0.8f, -0.6f, 0.f, 1.f, 1.3f, 0.7f, 0.5f, 300.f, 200.f, 0.f};
  const auto wind_buffer = Constants(wind.data(), sizeof(wind));
  const auto run = [&](const void* code, size_t size, const char* semantic, UINT index, const std::vector<Vertex>& points, ID3D11Buffer* camera) {
    const std::array<ID3D11Buffer*, 2> constants = {camera, wind_buffer.Get()};
    return StreamOut(code, size, semantic, index, 4, layout, points, constants, nullptr);
  };
  const auto game_world = run(game.data(), game.size(), "SV_POSITION", 0, vertices, identity_buffer.Get());
  const auto ours_world = run(capture->GetBufferPointer(), capture->GetBufferSize(), "TEXCOORD", 0, vertices, identity_buffer.Get());
  size_t bad_world = 0;
  for (size_t index = 0; index < vertices.size(); ++index) {
    for (int axis = 0; axis < 4; ++axis) bad_world += (std::fabs(ours_world[index][axis] - game_world[index][axis]) < 1e-4f) ? 0 : 1;
  }
  // The sway left out: with its weight 1 the game's corners stray from ours by it alone.
  float most_stray = 0.f;
  size_t swayed = 0;
  if (sway != 0) {
    auto swaying = vertices;
    for (auto& vertex : swaying) vertex.inputs[sway][3] = 1.f;
    const auto game_swaying = run(game.data(), game.size(), "SV_POSITION", 0, swaying, identity_buffer.Get());
    for (size_t index = 0; index < swaying.size(); ++index) {
      float stray = 0.f;
      for (int axis = 0; axis < 3; ++axis) stray = std::max(stray, std::fabs(game_swaying[index][axis] - ours_world[index][axis]));
      most_stray = std::max(most_stray, stray);
      swayed += (stray > 1e-5f) ? 1 : 0;
    }
  }
  // Left out where the game's projection of the origin is in the view; the rows are the instance's otherwise.
  size_t left_out = 0, bad_rows = 0, new_left_out = 0, turned_differs = 0;
  for (ID3D11Buffer* camera : {view_buffer.Get(), zero_buffer.Get(), turned_buffer.Get()}) {
    const auto origin_clip = run(game.data(), game.size(), "SV_POSITION", 0, origins, camera);
    const auto row0 = run(rows_code->GetBufferPointer(), rows_code->GetBufferSize(), "TEXCOORD", 0, origins, camera);
    const auto row2 = run(rows_code->GetBufferPointer(), rows_code->GetBufferSize(), "TEXCOORD", 2, origins, camera);
    for (size_t index = 0; index < origins.size(); ++index) {
      const auto& clip = origin_clip[index];
      const bool in_view = camera != zero_buffer.Get() && clip[3] + clip[0] >= 0.f && clip[3] - clip[0] >= 0.f && clip[3] + clip[1] >= 0.f
                           && clip[3] - clip[1] >= 0.f;
      const bool out = row0[index][3] == 1e30f && row2[index][3] == 1e30f;
      const bool kept = row0[index] == origins[index].inputs[rows] && row2[index] == origins[index].inputs[rows + 2];
      if (camera == view_buffer.Get()) {
        left_out += out ? 1 : 0;
        bad_rows += (in_view ? out : kept) ? 0 : 1;
      } else if (camera == zero_buffer.Get()) {
        new_left_out += out ? 1 : 0;
        bad_rows += kept ? 0 : 1;
      } else {
        bad_rows += (in_view ? out : kept) ? 0 : 1;
      }
    }
    if (camera == turned_buffer.Get()) {
      const auto view_rows = run(rows_code->GetBufferPointer(), rows_code->GetBufferSize(), "TEXCOORD", 0, origins, view_buffer.Get());
      for (size_t index = 0; index < origins.size(); ++index) turned_differs += ((row0[index][3] == 1e30f) != (view_rows[index][3] == 1e30f)) ? 1 : 0;
    }
  }
  CHECK(bad_world == 0, "%s: %zu world components differ from the game's", name, bad_world);
  CHECK(sway == 0 || (swayed > 0 && most_stray < 0.02f), "%s: the sway moved %zu corners, at most %f", name, swayed, most_stray);
  CHECK(bad_rows == 0 && left_out > 0 && left_out < origins.size() && new_left_out == 0, "%s: %zu rows wrong, %zu of %zu left out, %zu with no view",
        name, bad_rows, left_out, origins.size(), new_left_out);
  CHECK(turned_differs > 0, "CONTROL: %s: the view turned away left out the same instances", name);
  std::printf("%ls / %ls (0x%s): 256 corners the game's world (the sway left out: %zu corners up to %.4f m); %zu of 256 instances left out as "
              "the game's view has them, 0 with no view; %zu differ turned away\n",
              capture_name, rows_name, name, swayed, most_stray, left_out, turned_differs);
}

// 11. rt_capture_cascade_cloth, imposter, tree and far_tree (cloth, sprite soldiers, leaves and branches, far tree cards casting
//     from out of view) against the game's shadow vertex shaders (0x8A00A1D4, 0xE3DA20D4, 0x5851CA62, 0x6F20485E, 0x30C453EE csos)
//     with an identity view-projection (the shadow camera's view, the trees' rows, blend, time and wind, the cards' places, facings
//     and sizes random): 64 random triangles each, every corner's world position (TEXCOORD2) the game's SV_Position and its UV
//     (TEXCOORD1) the game's (a far card's: the atlas view its pixel shader 0x6190CA61 reads where the blend is over half, u - 0.25
//     (view + 1), else u - 0.25 view); a card the game fades out wholly (TEXCOORD1.w 0) a NaN. rt_capture_cascade_unit (soldiers and
//     horses) against 0xA22FA941 with a random animation map at t0 (two bones a corner, its instance's row): TEXCOORD0 the game's
//     SV_Position, through the world-only geometry shader. rt_capture_cascade_sail (ships' sails and sail symbols) against
//     0xA42F9EDE / 0xF7EDF855 with a random world_matrix at b1, rt_capture_cascade_rope against 0x4E3EE717 (ribbons turned to
//     the camera at c0, world alone). rt_capture_cascade_wind against 0xEDCD8445 (rigid rows swayed by COLOR0.w and the wind at
//     b1), rt_capture_cascade_weighted against 0x1E0425DE (the units' bones from a random matrix stack at b1), world alone;
//     rt_capture_cascade_grass against 0xF6CBD06E (blades shrunk and swayed from a camera at c0 with a random inv_view, clumps
//     up to 375 m away: some past the wind's reach). rt_capture_cascade_far_tree_3k against 0x63E273F4 (the other far cards: 8
//     views in a row, the UV its pixel shader 0x9D7E2040 reads, (u + view) / 8, the nearer view; a faded card a NaN). Through
//     Pharaoh's outside-view geometry shader with a main view at
//     cb0[10-13] (as rt_core streams them out): the
//     triangles wholly outside it (the CPU's test of the game's corners) kept, their world positions and UVs (two halves in w) the
//     game's; the others collapsed onto their first corner. CONTROL: the view turned away keeps another set.
struct ClothVertex {
  std::array<std::array<float, 4>, 4> inputs;  // v0-v3
};

// A ship's sail (0xA42F9EDE, 0xF7EDF855: POSITION0, TEXCOORD0-2, COLOR0-1) or rope (0x4E3EE717: POSITION0, NORMAL0, TEXCOORD0-2).
struct NavalVertex {
  std::array<std::array<float, 4>, 6> inputs;
};

static const D3D11_INPUT_ELEMENT_DESC SAIL_LAYOUT[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0}};

static const D3D11_INPUT_ELEMENT_DESC ROPE_LAYOUT[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0}};

// The trees' vertex: POSITION0-6 (POSITION4 the UV), TEXCOORD0-6 (two instance row sets at 0-2 and 4-6).
struct TreeVertex {
  std::array<std::array<float, 4>, 14> inputs;
};

static const D3D11_INPUT_ELEMENT_DESC TREE_LAYOUT[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 112, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 128, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 144, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 160, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 176, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 192, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 208, D3D11_INPUT_PER_VERTEX_DATA, 0}};

static const D3D11_INPUT_ELEMENT_DESC CLOTH_LAYOUT[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0}};

// What a capture version writes: a leaf's world (TEXCOORD2) and UV (TEXCOORD1) for RT_LEAF, a far card's the same with the UV in
// its atlas view (30C453EE's four views, or 63E273F4's `face_count` in a row), a unit's world alone (TEXCOORD0) for RT_WORLD.
enum class CascadeCorners { leaf, far_card, card_faces, world };

// The soldiers' and horses' shadow vertex (0xA22FA941): POSITION0-8, TEXCOORD0-5 (the instance's world rows at 0-2), its row in
// the animation map (TEXCOORD6) and TEXCOORD7.
struct UnitVertex {
  std::array<std::array<float, 4>, 15> inputs;
  std::array<uint32_t, 4> row;
  std::array<float, 2> rest;
};

static const D3D11_INPUT_ELEMENT_DESC UNIT_LAYOUT[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 7, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 112, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 8, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 128, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 144, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 160, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 176, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 192, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 208, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 224, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 6, DXGI_FORMAT_R32G32B32A32_UINT, 0, 240, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 7, DXGI_FORMAT_R32G32_FLOAT, 0, 256, D3D11_INPUT_PER_VERTEX_DATA, 0}};

template <typename Vertex>
static void TestCascadeLeaf(const std::string& games, const std::string& folder, const char* name, const wchar_t* capture_name,
                            std::span<const D3D11_INPUT_ELEMENT_DESC> layout, const std::vector<Vertex>& vertices,
                            std::span<ID3D11Buffer* const> constants, CascadeCorners corners = CascadeCorners::leaf,
                            ID3D11ShaderResourceView* vertex_view = nullptr, float face_count = 0.f) {
  const bool atlas = corners == CascadeCorners::far_card;
  const bool faces = corners == CascadeCorners::card_faces;
  const bool with_uv = corners != CascadeCorners::world;
  const auto [game_code, capture] = Shaders(games, folder, name, capture_name);
  const auto run = [&](const void* code, size_t size, const char* semantic, UINT index, BYTE components) {
    return StreamOut(code, size, semantic, index, components, layout, vertices, constants, vertex_view);
  };
  const auto game_world = run(game_code.data(), game_code.size(), "SV_POSITION", 0, 4);
  // A far card's TEXCOORD1: the UV, the atlas view and the blend toward the next.
  auto game_uv = with_uv ? run(game_code.data(), game_code.size(), "TEXCOORD", 1, (atlas || faces) ? 4 : 2)
                         : std::vector<std::array<float, 4>>(vertices.size());
  if (faces) {
    // CONTROL: the cards look at every view, each with its next under and over half.
    uint64_t seen = 0;
    for (auto& uv : game_uv) {
      const float next = (uv[3] > 0.5f) ? 1.f : 0.f;
      seen |= uint64_t{1} << ((static_cast<int>(uv[2]) * 2) + static_cast<int>(next));
      uv[0] = (uv[0] + std::fmod(uv[2] + next, face_count)) / face_count;
    }
    const uint64_t all = (uint64_t{1} << (2 * static_cast<int>(face_count))) - 1;
    CHECK(seen == all, "CONTROL: %s: the cards' views and blends 0x%llX, not all %d", name, static_cast<unsigned long long>(seen),
          2 * static_cast<int>(face_count));
  }
  if (atlas) {
    // CONTROL: the cards look at all four views, each with its next under and over half.
    uint32_t seen = 0;
    for (auto& uv : game_uv) {
      seen |= 1u << ((static_cast<int>(uv[2]) * 2) + ((uv[3] > 0.5f) ? 1 : 0));
      uv[0] -= 0.25f * (uv[2] + ((uv[3] > 0.5f) ? 1.f : 0.f));
    }
    CHECK(seen == 0xFFu, "CONTROL: %s: the cards' views and blends 0x%X, not all eight", name, seen);
  }
  const auto our_world = run(capture->GetBufferPointer(), capture->GetBufferSize(), "TEXCOORD", with_uv ? 2 : 0, 4);
  const auto our_uv = with_uv ? run(capture->GetBufferPointer(), capture->GetBufferSize(), "TEXCOORD", 1, 2) : game_uv;
  size_t bad_world = 0, bad_uv = 0;
  for (size_t corner = 0; corner < vertices.size(); ++corner) {
    for (int axis = 0; axis < 4; ++axis) bad_world += (std::fabs(our_world[corner][axis] - game_world[corner][axis]) < 1e-3f) ? 0 : 1;
    for (int axis = 0; axis < 2; ++axis) bad_uv += (std::fabs(our_uv[corner][axis] - game_uv[corner][axis]) < 1e-5f) ? 0 : 1;
  }
  // Main views at c10-13 (rows acting on column vectors): along +z from (0, 0, -60), 0.8 wide; turned: along -z from (0, 0, 60).
  size_t kept = 0, collapsed = 0, bad_kept = 0, turned_differs = 0;
  std::vector<bool> kept_ahead;
  for (const float facing : {1.f, -1.f}) {
    const std::array<std::array<float, 4>, 4> main_view = {{{1.25f, 0.f, 0.f, 0.f}, {0.f, 1.25f, 0.f, 0.f}, {0.f, 0.f, facing, 0.f}, {0.f, 0.f, facing, 60.f}}};
    std::array<float, 44 * 4> registers = {};
    for (int row = 0; row < 4; ++row) std::ranges::copy(main_view[row], registers.begin() + ((10 + row) * 4));
    const auto view_buffer = Constants(registers.data(), sizeof(registers));
    const auto streamed = StreamOut(capture->GetBufferPointer(), capture->GetBufferSize(), "SV_POSITION", 0, 4, layout, vertices, constants,
                                    vertex_view, with_uv ? std::span<const BYTE>(__rt_outside_view_leaf) : std::span<const BYTE>(__rt_outside_view_world),
                                    view_buffer.Get());
    for (size_t triangle = 0; triangle * 3 < vertices.size(); ++triangle) {
      std::array<std::array<float, 4>, 3> clip = {};
      for (size_t corner = 0; corner < 3; ++corner) {
        const auto& world = game_world[(triangle * 3) + corner];
        for (int row = 0; row < 4; ++row) {
          clip[corner][row] = (main_view[row][0] * world[0]) + (main_view[row][1] * world[1]) + (main_view[row][2] * world[2]) + main_view[row][3];
        }
      }
      const auto all = [&clip](auto test) { return std::ranges::all_of(clip, test); };
      const bool outside = all([](const auto& p) { return p[3] <= 0.f; }) || all([](const auto& p) { return p[0] > p[3]; })
                           || all([](const auto& p) { return p[0] < -p[3]; }) || all([](const auto& p) { return p[1] > p[3]; })
                           || all([](const auto& p) { return p[1] < -p[3]; }) || all([](const auto& p) { return p[2] > p[3]; });
      if (facing > 0.f) {
        kept_ahead.push_back(outside);
        kept += outside ? 1 : 0;
        collapsed += outside ? 0 : 1;
      } else {
        turned_differs += (outside != kept_ahead[triangle]) ? 1 : 0;
      }
      for (size_t corner = 0; corner < 3; ++corner) {
        const size_t source = (triangle * 3) + (outside ? corner : 0);
        const auto& out = streamed[(triangle * 3) + corner];
        const uint32_t halves = std::bit_cast<uint32_t>(out[3]);
        bool same = with_uv ? (std::fabs(Half(static_cast<uint16_t>(halves & 0xFFFFu)) - game_uv[source][0]) <= 1e-3f * std::max(1.f, std::fabs(game_uv[source][0]))
                               && std::fabs(Half(static_cast<uint16_t>(halves >> 16)) - game_uv[source][1]) <= 1e-3f * std::max(1.f, std::fabs(game_uv[source][1])))
                            : (out[3] == 1.f);
        for (int axis = 0; axis < 3; ++axis) same = same && std::fabs(out[axis] - game_world[source][axis]) < 1e-3f;
        bad_kept += same ? 0 : 1;
      }
    }
  }
  CHECK(bad_world == 0 && bad_uv == 0, "%s: %zu world components, %zu UV components differ from the game's", name, bad_world, bad_uv);
  CHECK(bad_kept == 0 && kept > 0 && collapsed > 0, "%s: %zu streamed corners wrong, %zu kept, %zu collapsed", name, bad_kept, kept, collapsed);
  CHECK(turned_differs > 0, "CONTROL: %s: the view turned away kept the same triangles", name);
  std::printf("%ls (0x%s): %zu corners the game's world%s; %zu of %zu triangles out of view kept, %zu differ turned away\n", capture_name,
              name, vertices.size(), with_uv ? " and UV" : "", kept, vertices.size() / 3, turned_differs);
}

// Grass in the shadow pass (0xF6CBD06E): POSITION0, TEXCOORD0 (xy), NORMAL0-1, COLOR0, its clump's rows TEXCOORD1-3 (the place in w).
struct GrassVertex {
  std::array<std::array<float, 4>, 8> inputs;
};

static const D3D11_INPUT_ELEMENT_DESC GRASS_LAYOUT[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 112, D3D11_INPUT_PER_VERTEX_DATA, 0}};

static void TestCascadeLeaves(const std::string& games, const std::string& folder) {
  std::mt19937 random(12);
  std::uniform_real_distribution<float> spread(-1.f, 1.f);
  std::array<float, 44 * 4> camera = {};
  for (int row = 0; row < 4; ++row) camera[((10 + row) * 4) + row] = 1.f;
  // The imposter's shadow camera's view (c2-c5): random.
  for (int value = 2 * 4; value < 6 * 4; ++value) camera[value] = spread(random);
  camera[35 * 4] = 38.696f;  // time_in_sec (the battle trace's)
  const auto camera_buffer = Constants(camera.data(), sizeof(camera));
  // wind_VS: direction, gust, multipliers and strengths.
  const std::array<float, 12> wind = {0.8f, -0.6f, 0.8f, -0.6f, 0.f, 1.f, 1.3f, 0.7f, 0.5f, 300.f, 200.f, 0.f};
  const auto wind_buffer = Constants(wind.data(), sizeof(wind));
  const std::array<float, 4> size = {0.125f, 0.25f, 0.f, 0.f};
  const auto size_buffer = Constants(size.data(), sizeof(size));
  std::vector<ClothVertex> cloth;
  std::vector<ImposterVertex> imposters;
  std::vector<TreeVertex> trees;
  for (int triangle = 0; triangle < 64; ++triangle) {
    const std::array<float, 3> centre = {100.f * spread(random), 100.f * spread(random), 100.f * spread(random)};
    const float frame = std::floor(2.f + (2.f * spread(random)));
    for (int corner = 0; corner < 3; ++corner) {
      cloth.push_back({.inputs = {{{centre[0] + (2.f * spread(random)), centre[1] + (2.f * spread(random)), centre[2] + (2.f * spread(random)), 1.f},
                                   {spread(random), spread(random), spread(random), spread(random)},
                                   {0.5f + (0.5f * spread(random)), 0.5f + (0.5f * spread(random)), spread(random), spread(random)},
                                   {spread(random), spread(random), spread(random), spread(random)}}}});
      imposters.push_back({.place = {centre[0], centre[1], centre[2], frame}, .last_place = centre,
                           .frame = {0.5f + (0.5f * spread(random)), 0.5f + (0.5f * spread(random)), spread(random), 0.5f * spread(random)},
                           .corner = {(corner & 1) ? 1.f : 0.f, (corner & 2) ? 1.f : 0.f, spread(random), 0.f},
                           .flat = {0.6f * spread(random), 0.6f * spread(random), 1.8f * spread(random), spread(random)}});
      // Two row sets (a turn about y, the centre in w, the second a little apart), blended by v5.xy (summing to 1) at the sets
      // v5.zw names; v6.w the sway's weight past the saturate's 0.7, v10.xy the phase and the fade.
      TreeVertex tree = {};
      auto& inputs = tree.inputs;
      for (auto& input : inputs) input = {spread(random), spread(random), spread(random), spread(random)};
      inputs[0] = {2.f * spread(random), 2.f * spread(random), 2.f * spread(random), 1.f};
      const float blend = 0.5f + (0.5f * spread(random));
      inputs[5] = {blend, 1.f - blend, (corner & 1) ? 1.f : 0.f, (corner & 2) ? 1.f : 0.f};
      inputs[6][3] = 0.75f + (0.75f * spread(random));
      for (const int first : {7, 11}) {
        const float turn = spread(random) * 3.14159f;
        const float apart = (first == 11) ? 0.3f : 0.f;
        inputs[first] = {std::cos(turn), 0.f, std::sin(turn), centre[0] + apart};
        inputs[first + 1] = {0.f, 1.f, 0.f, centre[1]};
        inputs[first + 2] = {-std::sin(turn), 0.f, std::cos(turn), centre[2] - apart};
      }
      trees.push_back(tree);
    }
  }
  const std::array<ID3D11Buffer*, 1> cloth_constants = {camera_buffer.Get()};
  TestCascadeLeaf(games, folder, "8A00A1D4", L"rt_capture_cascade_cloth", CLOTH_LAYOUT, cloth, cloth_constants);
  const std::array<ID3D11Buffer*, 2> imposter_constants = {camera_buffer.Get(), size_buffer.Get()};
  TestCascadeLeaf(games, folder, "E3DA20D4", L"rt_capture_cascade_imposter", IMPOSTER_LAYOUT, imposters, imposter_constants);
  const std::array<ID3D11Buffer*, 2> tree_constants = {camera_buffer.Get(), wind_buffer.Get()};
  TestCascadeLeaf(games, folder, "5851CA62", L"rt_capture_cascade_tree", TREE_LAYOUT, trees, tree_constants);
  TestCascadeLeaf(games, folder, "6F20485E", L"rt_capture_cascade_tree", TREE_LAYOUT, trees, tree_constants);
  std::vector<BillboardVertex> cards;
  for (int triangle = 0; triangle < 64; ++triangle) {
    const std::array<float, 4> place = {100.f * spread(random), 100.f * spread(random), 100.f * spread(random), 0.55f + (0.45f * spread(random))};
    const std::array<float, 4> facing = {spread(random), spread(random), spread(random), 5.f + (4.f * spread(random))};
    for (int corner = 0; corner < 3; ++corner) {
      cards.push_back({.position = {spread(random), 0.5f + (0.5f * spread(random)), 0.f, 1.f}, .normal = {0.f, 0.f, 1.f, 0.f},
                       .uv = {0.5f + (0.5f * spread(random)), 0.5f + (0.5f * spread(random))}, .colour = {1.f, 1.f, 1.f, 1.f},
                       .place = place, .facing = facing});
    }
  }
  const std::array<ID3D11Buffer*, 1> card_constants = {camera_buffer.Get()};
  TestCascadeLeaf(games, folder, "30C453EE", L"rt_capture_cascade_far_tree", BILLBOARD_LAYOUT, cards, card_constants, CascadeCorners::far_card);
  const auto [card_code, card_capture] = Shaders(games, folder, "30C453EE", L"rt_capture_cascade_far_tree");
  std::vector<BillboardVertex> faded(1, cards[0]);
  faded[0].place[3] = 0.f;
  const auto faded_world = StreamOut(card_capture->GetBufferPointer(), card_capture->GetBufferSize(), "TEXCOORD", 2, 4, BILLBOARD_LAYOUT, faded,
                                     card_constants, nullptr);
  CHECK(std::isnan(faded_world[0][0]), "rt_capture_cascade_far_tree: a faded card not a NaN");
  // The other card set (0x63E273F4): POSITION0 the corner, TEXCOORD0 the UV, TEXCOORD1 the place (w the fade), TEXCOORD2 the
  // facing (w the size); 8 views (tree_billboard_constants at b1: c1.w). 128 cards: every view and half seen.
  std::vector<std::array<std::array<float, 4>, 4>> cards_3k;
  for (int triangle = 0; triangle < 128; ++triangle) {
    const std::array<float, 4> place = {100.f * spread(random), 100.f * spread(random), 100.f * spread(random), 0.55f + (0.45f * spread(random))};
    const std::array<float, 4> facing = {spread(random), spread(random), spread(random), 5.f + (4.f * spread(random))};
    for (int corner = 0; corner < 3; ++corner) {
      cards_3k.push_back({{{spread(random), 0.5f + (0.5f * spread(random)), 0.f, 1.f},
                           {0.5f + (0.5f * spread(random)), 0.5f + (0.5f * spread(random)), 0.f, 0.f}, place, facing}});
    }
  }
  static const D3D11_INPUT_ELEMENT_DESC CARD_3K_LAYOUT[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0}};
  const std::array<float, 8> billboard = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 8.f};
  const auto billboard_buffer = Constants(billboard.data(), sizeof(billboard));
  const std::array<ID3D11Buffer*, 2> card_3k_constants = {camera_buffer.Get(), billboard_buffer.Get()};
  TestCascadeLeaf(games, folder, "63E273F4", L"rt_capture_cascade_far_tree_3k", CARD_3K_LAYOUT, cards_3k, card_3k_constants,
                  CascadeCorners::card_faces, nullptr, 8.f);
  const auto [card_3k_code, card_3k_capture] = Shaders(games, folder, "63E273F4", L"rt_capture_cascade_far_tree_3k");
  std::vector<std::array<std::array<float, 4>, 4>> faded_3k(1, cards_3k[0]);
  faded_3k[0][2][3] = 0.f;
  const auto faded_3k_world = StreamOut(card_3k_capture->GetBufferPointer(), card_3k_capture->GetBufferSize(), "TEXCOORD", 2, 4, CARD_3K_LAYOUT,
                                        faded_3k, card_3k_constants, nullptr);
  CHECK(std::isnan(faded_3k_world[0][0]), "rt_capture_cascade_far_tree_3k: a faded card not a NaN");
  // Soldiers and horses: 22 bones (their bytes k / 255: three texels each from 3k) in 8 instances' rows of a random animation map;
  // a corner's two bones, its blend (v5.w) and both positions (v0, v1) random, its instance's world rows at TEXCOORD0-2.
  std::vector<std::array<float, 4>> bones(64 * 8);
  for (auto& texel : bones) texel = {spread(random), spread(random), spread(random), 10.f * spread(random)};
  const auto animation = Texture(64, 8, DXGI_FORMAT_R32G32B32A32_FLOAT, bones.data(), 64 * 16, D3D11_BIND_SHADER_RESOURCE);
  ComPtr<ID3D11ShaderResourceView> animation_view;
  device->CreateShaderResourceView(animation.Get(), nullptr, animation_view.GetAddressOf());
  std::uniform_int_distribution<int> bone(0, 21);
  std::vector<UnitVertex> units;
  for (int triangle = 0; triangle < 64; ++triangle) {
    const std::array<float, 3> centre = {100.f * spread(random), 100.f * spread(random), 100.f * spread(random)};
    const auto instance = static_cast<uint32_t>(triangle % 8);
    const float turn = spread(random) * 3.14159f;
    for (int corner = 0; corner < 3; ++corner) {
      UnitVertex unit = {};
      for (auto& input : unit.inputs) input = {spread(random), spread(random), spread(random), spread(random)};
      unit.inputs[3][3] = static_cast<float>(bone(random)) / 255.f;
      unit.inputs[4][3] = static_cast<float>(bone(random)) / 255.f;
      unit.inputs[5][3] = 0.5f + (0.5f * spread(random));
      unit.inputs[9] = {std::cos(turn), 0.f, std::sin(turn), centre[0]};
      unit.inputs[10] = {0.f, 1.f, 0.f, centre[1]};
      unit.inputs[11] = {-std::sin(turn), 0.f, std::cos(turn), centre[2]};
      unit.row = {instance, 0u, 0u, 0u};
      units.push_back(unit);
    }
  }
  TestCascadeLeaf(games, folder, "A22FA941", L"rt_capture_cascade_unit", UNIT_LAYOUT, units, card_constants, CascadeCorners::world,
                  animation_view.Get());
  // The same corners drawn without the animation map: their bones from a random matrix stack (150 registers) at b1.
  std::vector<std::array<float, 4>> stack(150);
  for (auto& row : stack) row = {spread(random), spread(random), spread(random), 10.f * spread(random)};
  const auto stack_buffer = Constants(stack.data(), static_cast<UINT>(stack.size() * sizeof(stack[0])));
  const std::array<ID3D11Buffer*, 2> weighted_constants = {camera_buffer.Get(), stack_buffer.Get()};
  TestCascadeLeaf(games, folder, "1E0425DE", L"rt_capture_cascade_weighted", UNIT_LAYOUT, units, weighted_constants, CascadeCorners::world);
  // Wind-swayed rigid meshes: the rigid shadow vertex (rows v6-v8), the sway's weight COLOR0.w (v5.w) from 0 to 1.
  std::vector<RigidShadowVertex> swaying;
  for (int triangle = 0; triangle < 64; ++triangle) {
    const std::array<float, 3> centre = {100.f * spread(random), 5.f * spread(random), 100.f * spread(random)};
    const float turn = spread(random) * 3.14159f;
    for (int corner = 0; corner < 3; ++corner) {
      RigidShadowVertex vertex = {};
      for (auto& input : vertex.inputs) input = {spread(random), spread(random), spread(random), spread(random)};
      vertex.inputs[0] = {2.f * spread(random), 2.f * spread(random), 2.f * spread(random), 1.f};
      vertex.inputs[5][3] = 0.5f + (0.5f * spread(random));
      vertex.inputs[6] = {std::cos(turn), 0.f, std::sin(turn), centre[0]};
      vertex.inputs[7] = {0.f, 1.f, 0.f, centre[1]};
      vertex.inputs[8] = {-std::sin(turn), 0.f, std::cos(turn), centre[2]};
      swaying.push_back(vertex);
    }
  }
  TestCascadeLeaf(games, folder, "EDCD8445", L"rt_capture_cascade_wind", RIGID_SHADOW_LAYOUT, swaying, tree_constants, CascadeCorners::world);
  // Grass: the pass's camera at (10, 20, -30) with a random inv_view (c18-21), inv_lod_distance 0.01 (grass_PS at b2); clumps
  // turned about y, up to 375 m away (the wind's reach 300 m).
  std::array<float, 44 * 4> grass_camera = camera;
  grass_camera[0] = 10.f;
  grass_camera[1] = 20.f;
  grass_camera[2] = -30.f;
  for (int value = 18 * 4; value < 22 * 4; ++value) grass_camera[value] = spread(random);
  const auto grass_camera_buffer = Constants(grass_camera.data(), sizeof(grass_camera));
  const std::array<float, 4> lod = {0.01f, 0.f, 0.f, 0.f};
  const auto lod_buffer = Constants(lod.data(), sizeof(lod));
  std::vector<GrassVertex> grass;
  for (int triangle = 0; triangle < 64; ++triangle) {
    const std::array<float, 3> clump = {250.f * spread(random), 5.f * spread(random), 250.f * spread(random)};
    const float turn = spread(random) * 3.14159f;
    for (int corner = 0; corner < 3; ++corner) {
      GrassVertex vertex = {};
      for (auto& input : vertex.inputs) input = {spread(random), spread(random), spread(random), spread(random)};
      vertex.inputs[0][3] = 1.f;
      vertex.inputs[4][3] = 0.5f + (0.5f * spread(random));
      vertex.inputs[5] = {std::cos(turn), 0.f, std::sin(turn), clump[0]};
      vertex.inputs[6] = {0.f, 1.f, 0.f, clump[1]};
      vertex.inputs[7] = {-std::sin(turn), 0.f, std::cos(turn), clump[2]};
      grass.push_back(vertex);
    }
  }
  const std::array<ID3D11Buffer*, 3> grass_constants = {grass_camera_buffer.Get(), wind_buffer.Get(), lod_buffer.Get()};
  TestCascadeLeaf(games, folder, "F6CBD06E", L"rt_capture_cascade_grass", GRASS_LAYOUT, grass, grass_constants);
  // Sails: their mesh near the origin, a random turn and place in world_matrix (three registers, a column each); ropes: their
  // ribbons around random points, the shadow camera at c0.
  const float sail_turn = spread(random) * 3.14159f;
  const std::array<float, 12> sail_world = {std::cos(sail_turn), 0.f, std::sin(sail_turn), 60.f * spread(random), 0.f, 1.f, 0.f, 60.f * spread(random),
                -std::sin(sail_turn), 0.f, std::cos(sail_turn), 60.f * spread(random)};
  const auto sail_buffer = Constants(sail_world.data(), sizeof(sail_world));
  std::vector<NavalVertex> naval;
  for (int triangle = 0; triangle < 64; ++triangle) {
    const std::array<float, 3> centre = {100.f * spread(random), 100.f * spread(random), 100.f * spread(random)};
    for (int corner = 0; corner < 3; ++corner) {
      NavalVertex vertex = {};
      for (auto& input : vertex.inputs) input = {spread(random), spread(random), spread(random), spread(random)};
      vertex.inputs[0] = {centre[0] + (3.f * spread(random)), centre[1] + (3.f * spread(random)), centre[2] + (3.f * spread(random)), 1.f};
      vertex.inputs[3][0] = 0.5f + (0.5f * spread(random));  // the rope's width (its TEXCOORD1.x), the sail's TEXCOORD2
      naval.push_back(vertex);
    }
  }
  const std::array<ID3D11Buffer*, 2> sail_constants = {camera_buffer.Get(), sail_buffer.Get()};
  TestCascadeLeaf(games, folder, "A42F9EDE", L"rt_capture_cascade_sail", SAIL_LAYOUT, naval, sail_constants);
  TestCascadeLeaf(games, folder, "F7EDF855", L"rt_capture_cascade_sail", SAIL_LAYOUT, naval, sail_constants);
  std::array<float, 44 * 4> rope_camera = {};
  for (int row = 0; row < 4; ++row) rope_camera[((10 + row) * 4) + row] = 1.f;
  rope_camera[0] = 30.f;
  rope_camera[1] = 400.f;
  rope_camera[2] = -20.f;
  const auto rope_camera_buffer = Constants(rope_camera.data(), sizeof(rope_camera));
  const std::array<ID3D11Buffer*, 1> rope_constants = {rope_camera_buffer.Get()};
  TestCascadeLeaf(games, folder, "4E3EE717", L"rt_capture_cascade_rope", ROPE_LAYOUT, naval, rope_constants, CascadeCorners::world);
}

// 12. rt_capture_cascade_tree_leaves (Shadow Casters Out of View's leaves): a dissolved tree's leaves left out (NaN) exactly where the
//     game's leaf shadow pass (0x5851CA62 + 0xA97EF2E0's csos: a quad over the whole view, the mask's alpha 1 and the dissolve map 0,
//     the texels it keeps longest) draws no sample (an occlusion query), for 12 fades from 0 to 1.5. CONTROL: rt_capture_cascade_tree
//     (the branches' version, no fade) keeps the dissolved ones.
static void TestLeafFade(const std::string& games, const std::string& folder) {
  const auto read = [&folder](const char* name) {
    std::ifstream file(folder + "/" + name, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  };
  const auto game_vertex = read("0x5851CA62.vs_5_0.cso");
  const auto game_pixel = read("0xA97EF2E0.ps_5_0.cso");
  if (game_vertex.empty() || game_pixel.empty()) {
    std::printf("missing 0x5851CA62 or 0xA97EF2E0\n");
    std::exit(2);
  }
  const auto leaves = CompileVertex(games, L"rt_capture_cascade_tree_leaves");
  const auto tree = CompileVertex(games, L"rt_capture_cascade_tree");
  ComPtr<ID3D11VertexShader> vertex_shader;
  ComPtr<ID3D11PixelShader> pixel_shader;
  ComPtr<ID3D11InputLayout> input;
  device->CreateVertexShader(game_vertex.data(), game_vertex.size(), nullptr, vertex_shader.GetAddressOf());
  device->CreatePixelShader(game_pixel.data(), game_pixel.size(), nullptr, pixel_shader.GetAddressOf());
  device->CreateInputLayout(TREE_LAYOUT, static_cast<UINT>(std::size(TREE_LAYOUT)), game_vertex.data(), game_vertex.size(), input.GetAddressOf());
  // t0 the dissolve map (0), t1 the diffuse map (alpha 1).
  const std::vector<uint32_t> dissolve(16, 0x00000000u);
  const std::vector<uint32_t> diffuse(16, 0xFFFFFFFFu);
  const auto dissolve_texture = Texture(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, dissolve.data(), 16, D3D11_BIND_SHADER_RESOURCE);
  const auto diffuse_texture = Texture(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, diffuse.data(), 16, D3D11_BIND_SHADER_RESOURCE);
  const auto dissolve_view = Srv(dissolve_texture.Get());
  const auto diffuse_view = Srv(diffuse_texture.Get());
  const std::array<ID3D11ShaderResourceView*, 2> views = {dissolve_view.Get(), diffuse_view.Get()};
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_WRAP, .AddressW = D3D11_TEXTURE_ADDRESS_WRAP, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf());
  const std::array<ID3D11SamplerState*, 2> samplers = {sampler.Get(), sampler.Get()};
  const auto target = Texture(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, nullptr, 0, D3D11_BIND_RENDER_TARGET);
  ComPtr<ID3D11RenderTargetView> target_view;
  device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf());
  const D3D11_RASTERIZER_DESC rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE, .DepthClipEnable = TRUE};
  ComPtr<ID3D11RasterizerState> rasterizer;
  device->CreateRasterizerState(&rasterizer_desc, rasterizer.GetAddressOf());
  const D3D11_QUERY_DESC query_desc = {.Query = D3D11_QUERY_OCCLUSION};
  ComPtr<ID3D11Query> query;
  device->CreateQuery(&query_desc, query.GetAddressOf());
  // An identity view-projection, no wind: the game's corners where the quad's are.
  std::array<float, 44 * 4> camera = {};
  for (int row = 0; row < 4; ++row) camera[((10 + row) * 4) + row] = 1.f;
  camera[35 * 4] = 38.696f;
  const auto camera_buffer = Constants(camera.data(), sizeof(camera));
  const std::array<float, 12> calm = {};
  const auto wind_buffer = Constants(calm.data(), sizeof(calm));
  const std::array<ID3D11Buffer*, 2> constants = {camera_buffer.Get(), wind_buffer.Get()};
  size_t bad = 0, left_out = 0, control_differs = 0;
  constexpr std::array<float, 12> FADES = {0.f, 0.3f, 0.5f, 0.7f, 0.8f, 0.85f, 0.89f, 0.9f, 0.92f, 1.f, 1.2f, 1.5f};
  for (const float fade : FADES) {
    std::vector<TreeVertex> quad;
    for (const auto& corner : std::array<std::array<float, 2>, 6>{{{-1.f, -1.f}, {-1.f, 1.f}, {1.f, -1.f}, {1.f, -1.f}, {-1.f, 1.f}, {1.f, 1.f}}}) {
      TreeVertex vertex = {};
      auto& inputs = vertex.inputs;
      inputs[0] = {corner[0], corner[1], 0.5f, 1.f};
      inputs[4] = {0.5f, 0.5f, 0.f, 0.f};  // the UV
      inputs[5] = {1.f, 0.f, 0.f, 0.f};    // all the first row set
      inputs[7] = {1.f, 0.f, 0.f, 0.f};
      inputs[8] = {0.f, 1.f, 0.f, 0.f};
      inputs[9] = {0.f, 0.f, 1.f, 0.f};
      inputs[10] = {0.f, 1.f - fade, 0.f, 0.f};
      quad.push_back(vertex);
    }
    const D3D11_BUFFER_DESC vertex_desc = {.ByteWidth = static_cast<UINT>(quad.size() * sizeof(TreeVertex)), .Usage = D3D11_USAGE_DEFAULT,
                                           .BindFlags = D3D11_BIND_VERTEX_BUFFER};
    const D3D11_SUBRESOURCE_DATA vertex_data = {.pSysMem = quad.data()};
    ComPtr<ID3D11Buffer> vertex_buffer;
    device->CreateBuffer(&vertex_desc, &vertex_data, vertex_buffer.GetAddressOf());
    const UINT stride = sizeof(TreeVertex);
    const UINT offset = 0;
    const D3D11_VIEWPORT viewport = {.Width = 16.f, .Height = 16.f, .MaxDepth = 1.f};
    context->IASetInputLayout(input.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->VSSetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.data());
    context->PSSetShader(pixel_shader.Get(), nullptr, 0);
    context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    context->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
    context->RSSetState(rasterizer.Get());
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
    context->Begin(query.Get());
    context->Draw(static_cast<UINT>(quad.size()), 0);
    context->End(query.Get());
    UINT64 samples = 0;
    while (context->GetData(query.Get(), &samples, sizeof(samples), 0) != S_OK) {}
    context->ClearState();
    const auto ours = StreamOut(leaves->GetBufferPointer(), leaves->GetBufferSize(), "TEXCOORD", 2, 4, TREE_LAYOUT, quad, constants, nullptr);
    const auto kept = StreamOut(tree->GetBufferPointer(), tree->GetBufferSize(), "TEXCOORD", 2, 4, TREE_LAYOUT, quad, constants, nullptr);
    const bool gone = std::ranges::all_of(ours, [](const std::array<float, 4>& world) { return std::isnan(world[0]); });
    const bool all_kept = std::ranges::none_of(ours, [](const std::array<float, 4>& world) { return std::isnan(world[0]); });
    bad += ((samples == 0) == gone && (gone || all_kept)) ? 0 : 1;
    left_out += gone ? 1 : 0;
    control_differs += ((samples == 0) != std::isnan(kept[0][0])) ? 1 : 0;
    std::printf("  fade %.2f: the game's %llu samples, ours %s\n", fade, samples, gone ? "left out" : "kept");
  }
  CHECK(bad == 0 && left_out > 0 && left_out < FADES.size(), "%zu fades wrong, %zu left out of %zu", bad, left_out, FADES.size());
  CHECK(control_differs > 0, "CONTROL: the branches' version left out the dissolved leaves too");
  std::printf("rt_capture_cascade_tree_leaves: %zu of %zu fades left out where the game's leaf shadow draws nothing; the branches' version "
              "keeps %zu of them\n", left_out, FADES.size(), control_differs);
}

// 13. Coloured Shadows in shaders/sunlight00-03 (RtSunColour: the sun's colour x (1 - t50)) on a 16 x 16 target with random
//     G-buffer, depth, shadow, AO, probes and lighting: the filter 1 everywhere (all of the sun's colour taken out) gives what
//     the sun colour (lighting c1) 0 gives unbound, so every use of the sun's colour is filtered; unbound the sun lights the
//     picture (it differs from c1 0). CONTROL: a filter of 0.5 differs from both.
//     The rays' AO (t41, rt_sun.hlsli RtInPlace): bound at 0.37 over the random t_ssao, the picture is t_ssao 0.37's with t41
//     unbound, to the bit (the traced value in place of the game's, ATN 2026-11-16). CONTROL: the random t_ssao's unbound
//     picture differs (the AO shows in the output).
struct SunShader {
  const wchar_t* file;
  UINT lighting_slot;
  UINT constant_buffers;  // b0.. bound
  std::vector<UINT> cube_array_slots;
  std::vector<UINT> harmonics_slots;  // 9 x 9
  UINT textures;  // t0.. bound (the rest 16 x 16 RGBA)
  UINT samplers;
  UINT ao_slot;  // t_ssao
  UINT shadow_slot;  // t_shadow (the game's mask)
};

static void TestSunTint(const std::string& games) {
  constexpr char VERTEX[] = "float4 main(uint id : SV_VertexID) : SV_Position { const float2 uv = float2((id << 1) & 2, id & 2); "
                            "return float4(uv * float2(2, -2) + float2(-1, 1), 0.5, 1); }";
  ComPtr<ID3DBlob> vertex_code;
  ComPtr<ID3DBlob> errors;
  D3DCompile(VERTEX, std::strlen(VERTEX), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vertex_code.GetAddressOf(), errors.GetAddressOf());
  ComPtr<ID3D11VertexShader> vertex;
  device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex.GetAddressOf());
  constexpr UINT SIZE = 16;
  std::mt19937 random(13);
  std::uniform_real_distribution<float> unit(0.05f, 0.95f);
  const auto values = [&](size_t count) {
    std::vector<float> filled(count);
    for (auto& value : filled) value = unit(random);
    return filled;
  };
  const auto plain = [&](UINT width, const std::vector<float>& texels) {
    return Srv(Texture(width, width, DXGI_FORMAT_R32G32B32A32_FLOAT, texels.data(), width * 16, D3D11_BIND_SHADER_RESOURCE).Get());
  };
  // The camera: every matrix the identity, near 0.1 and far 1000, the render size 16.
  std::array<float, 44 * 4> camera = {};
  for (int matrix = 0; matrix < 8; ++matrix) {
    for (int row = 0; row < 4; ++row) camera[((2 + (matrix * 4) + row) * 4) + row] = 1.f;
  }
  camera[34 * 4] = 0.1f;
  camera[(34 * 4) + 1] = 1000.f;
  camera[35 * 4] = 1.5f;
  for (const int at : {37, 38, 40}) {
    camera[(at * 4) + 0] = camera[(at * 4) + 1] = SIZE;
    camera[(at * 4) + 2] = camera[(at * 4) + 3] = 1.f / SIZE;
  }
  std::array<float, 7 * 4> environment = {0.3f, -0.8f, 0.5f, 0.f, 1.f, 10.f, 0.8f, 0.5f, 1.f, 1.f, 1.f, 1.f, -20.f, -20.f, 20.f, 20.f};
  environment[16] = std::bit_cast<float>(2u);  // g_probe_grid_size
  environment[17] = 1.f;                       // g_specular_probes_count
  environment[18] = 0.5f;                      // g_specular_probe_lerp
  environment[23] = 9.f;                       // g_spherical_harmonic_terms
  std::array<float, 18 * 4> lighting = {};
  const auto lighting_values = values(lighting.size());
  std::copy(lighting_values.begin(), lighting_values.end(), lighting.begin());
  lighting[0] = 0.3f;
  lighting[1] = -0.8f;
  lighting[2] = 0.5f;
  lighting[43] = std::bit_cast<float>(1u);  // g_use_spherical_harmonics
  lighting[47] = 0.f;                       // g_cloud_shadow_scale
  // sun_colour in the game's units (its shaders scale it by 1.90384671e-005: about 1 here).
  lighting[4] = 52525.f;
  lighting[5] = 47000.f;
  lighting[6] = 41000.f;
  auto dark = lighting;
  dark[4] = dark[5] = dark[6] = 0.f;
  const auto camera_buffer = Constants(camera.data(), sizeof(camera));
  const auto environment_buffer = Constants(environment.data(), sizeof(environment));
  const auto lit_buffer = Constants(lighting.data(), sizeof(lighting));
  const auto dark_buffer = Constants(dark.data(), sizeof(dark));
  const auto other_values = values(16 * 4);
  const auto other_buffer = Constants(other_values.data(), static_cast<UINT>(other_values.size() * sizeof(float)));
  const auto filter = [&](float value) { return plain(SIZE, std::vector<float>(SIZE * SIZE * 4, value)); };
  const auto all_filtered = filter(1.f);
  const auto half_filtered = filter(0.5f);
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf());
  const std::wstring path(games.begin(), games.end());
  const std::array<SunShader, 4> shaders = {{
      {.file = L"sunlight00_0xED1BED93", .lighting_slot = 2, .constant_buffers = 4, .cube_array_slots = {0, 1}, .harmonics_slots = {2, 3}, .textures = 12, .samplers = 4,
       .ao_slot = 5, .shadow_slot = 11},
      {.file = L"sunlight01_0x5496DE73", .lighting_slot = 2, .constant_buffers = 4, .cube_array_slots = {0, 1}, .harmonics_slots = {2, 3}, .textures = 12, .samplers = 4,
       .ao_slot = 5, .shadow_slot = 11},
      {.file = L"sunlight02_0xC3E0E939", .lighting_slot = 3, .constant_buffers = 5, .cube_array_slots = {2, 3}, .harmonics_slots = {4, 5}, .textures = 14, .samplers = 6,
       .ao_slot = 7, .shadow_slot = 13},
      {.file = L"sunlight03_0xD5FDDE8F", .lighting_slot = 3, .constant_buffers = 5, .cube_array_slots = {2, 3}, .harmonics_slots = {4, 5}, .textures = 14, .samplers = 6,
       .ao_slot = 7, .shadow_slot = 13},
  }};
  const auto traced_ambient = filter(0.37f);
  for (const auto& shader : shaders) {
    ComPtr<ID3DBlob> code;
    FolderInclude include(path + L"/twshogun2completeedition/shaders");
    if (FAILED(D3DCompileFromFile((path + L"/twshogun2completeedition/shaders/" + shader.file + L".ps_5_0.hlsl").c_str(), nullptr, &include,
                                  "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.ReleaseAndGetAddressOf()))) {
      std::printf("compile failed: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
      std::exit(2);
    }
    ComPtr<ID3D11PixelShader> pixel;
    device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, pixel.GetAddressOf());
    std::vector<ComPtr<ID3D11ShaderResourceView>> views(shader.textures);
    for (UINT slot = 0; slot < shader.textures; ++slot) {
      if (std::ranges::find(shader.cube_array_slots, slot) != shader.cube_array_slots.end()) {
        const D3D11_TEXTURE2D_DESC desc = {.Width = 4, .Height = 4, .MipLevels = 1, .ArraySize = 24, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                           .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                           .MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE};
        std::vector<std::vector<float>> face_texels(24);
        std::vector<D3D11_SUBRESOURCE_DATA> initial(24);
        for (UINT face = 0; face < 24; ++face) {
          face_texels[face] = values(64);
          initial[face] = {.pSysMem = face_texels[face].data(), .SysMemPitch = 64};
        }
        ComPtr<ID3D11Texture2D> cubes;
        device->CreateTexture2D(&desc, initial.data(), cubes.GetAddressOf());
        const D3D11_SHADER_RESOURCE_VIEW_DESC view = {.Format = desc.Format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY,
                                                      .TextureCubeArray = {.MipLevels = 1, .NumCubes = 4}};
        device->CreateShaderResourceView(cubes.Get(), &view, views[slot].GetAddressOf());
      } else {
        const UINT width = (std::ranges::find(shader.harmonics_slots, slot) != shader.harmonics_slots.end()) ? 9 : SIZE;
        views[slot] = plain(width, values(static_cast<size_t>(width) * width * 4));
      }
    }
    const std::vector<ID3D11SamplerState*> samplers(shader.samplers, sampler.Get());
    // `ambient`: t41 (the rays' AO); `ssao`: in place of the random t_ssao; `sun`: t40 (the darker), `alone`: t51 (the rays'
    // sun in place of the game's mask); `shadow`: in place of the random t_shadow.
    const auto run = [&](ID3D11Buffer* lighting_buffer, ID3D11ShaderResourceView* tint, ID3D11ShaderResourceView* ambient = nullptr,
                         ID3D11ShaderResourceView* ssao = nullptr, ID3D11ShaderResourceView* sun = nullptr,
                         ID3D11ShaderResourceView* alone = nullptr, ID3D11ShaderResourceView* shadow = nullptr) {
      const auto output = Texture(SIZE, SIZE, DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_RENDER_TARGET);
      ComPtr<ID3D11RenderTargetView> target;
      device->CreateRenderTargetView(output.Get(), nullptr, target.GetAddressOf());
      std::vector<ID3D11Buffer*> buffers(shader.constant_buffers, other_buffer.Get());
      buffers[0] = camera_buffer.Get();
      buffers[1] = environment_buffer.Get();
      buffers[shader.lighting_slot] = lighting_buffer;
      std::vector<ID3D11ShaderResourceView*> bound(views.size());
      for (size_t slot = 0; slot < views.size(); ++slot) bound[slot] = views[slot].Get();
      if (ssao != nullptr) bound[shader.ao_slot] = ssao;
      if (shadow != nullptr) bound[shader.shadow_slot] = shadow;
      const D3D11_VIEWPORT viewport = {.Width = SIZE, .Height = SIZE, .MaxDepth = 1.f};
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->VSSetShader(vertex.Get(), nullptr, 0);
      context->PSSetShader(pixel.Get(), nullptr, 0);
      context->PSSetShaderResources(0, static_cast<UINT>(bound.size()), bound.data());
      const std::array<ID3D11ShaderResourceView*, 2> traced = {sun, ambient};
      context->PSSetShaderResources(40, static_cast<UINT>(traced.size()), traced.data());
      const std::array<ID3D11ShaderResourceView*, 2> tint_and_alone = {tint, alone};
      context->PSSetShaderResources(50, static_cast<UINT>(tint_and_alone.size()), tint_and_alone.data());
      context->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
      context->PSSetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());
      context->RSSetViewports(1, &viewport);
      context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
      context->Draw(3, 0);
      context->ClearState();
      const auto bytes = Read(output.Get(), 16);
      std::vector<float> result(bytes.size() / 4);
      std::memcpy(result.data(), bytes.data(), bytes.size());
      return result;
    };
    const auto unbound = run(lit_buffer.Get(), nullptr);
    const auto filtered = run(lit_buffer.Get(), all_filtered.Get());
    const auto no_sun = run(dark_buffer.Get(), nullptr);
    const auto half = run(lit_buffer.Get(), half_filtered.Get());
    const auto same = [](float x, float y) { return std::fabs(x - y) <= (1e-5f * std::max(std::fabs(x), std::fabs(y))) + 1e-6f; };
    size_t bad = 0, not_finite = 0, sunlit = 0, between = 0;
    for (size_t value = 0; value < unbound.size(); ++value) {
      not_finite += std::isfinite(unbound[value]) ? 0 : 1;
      bad += same(filtered[value], no_sun[value]) ? 0 : 1;
      sunlit += same(unbound[value], no_sun[value]) ? 0 : 1;
      between += (!same(half[value], unbound[value]) && !same(half[value], no_sun[value])) ? 1 : 0;
    }
    const std::string name(shader.file, shader.file + std::wcslen(shader.file));
    CHECK(bad == 0 && not_finite == 0 && sunlit > 0, "%s: %zu values differ filtered from no sun, %zu not finite, %zu sunlit (unbound: %zu zero, "
          "at most %f; no sun at most %f)", name.c_str(), bad, not_finite, sunlit,
          static_cast<size_t>(std::ranges::count(unbound, 0.f)), *std::ranges::max_element(unbound), *std::ranges::max_element(no_sun));
    CHECK(between > 0, "CONTROL %s: half the filter gave the unfiltered or the sunless picture", name.c_str());
    std::printf("%s Coloured Shadows: the filter 1 gives the sun colour 0's picture; %zu of %zu values sunlit unbound, %zu between at half\n",
                name.c_str(), sunlit, unbound.size(), between);
    const auto in_place = run(lit_buffer.Get(), nullptr, traced_ambient.Get());
    const auto game_at_traced = run(lit_buffer.Get(), nullptr, nullptr, traced_ambient.Get());
    size_t ao_apart = 0, ao_bits = 0, ao_shown = 0;
    for (size_t value = 0; value < unbound.size(); ++value) {
      ao_apart += same(in_place[value], game_at_traced[value]) ? 0 : 1;
      ao_bits += (std::bit_cast<uint32_t>(in_place[value]) == std::bit_cast<uint32_t>(game_at_traced[value])) ? 0 : 1;
      ao_shown += same(in_place[value], unbound[value]) ? 0 : 1;
    }
    CHECK(ao_apart == 0, "%s: %zu values of t41 0.37 differ from t_ssao 0.37 unbound", name.c_str(), ao_apart);
    CHECK(ao_shown > 0, "CONTROL %s: t41 0.37 left the random t_ssao's picture", name.c_str());
    std::printf("%s AO in place: t41 0.37 = t_ssao 0.37 (%zu apart, %zu not to the bit); the random t_ssao's picture %zu values apart\n",
                name.c_str(), ao_apart, ao_bits, ao_shown);
    // Rays Replace Shadow Maps: t51 0.37 over the game's mask 0.2 (t40 bound too: it then is not read) = the game's with
    // t_shadow 0.37; CONTROL: the darker (t40 0.37 without t51, min(0.2, 0.37)) differs from it. g_Shadow_low_clamp
    // (lighting c15.x) 0 here: the random one could lift both above 0.37.
    auto unclamped = lighting;
    unclamped[60] = 0.f;
    const auto unclamped_buffer = Constants(unclamped.data(), sizeof(unclamped));
    const auto traced_sun = filter(0.37f);
    const auto darker_mask = filter(0.2f);
    const auto alone = run(unclamped_buffer.Get(), nullptr, nullptr, nullptr, traced_sun.Get(), traced_sun.Get(), darker_mask.Get());
    const auto game_at_sun = run(unclamped_buffer.Get(), nullptr, nullptr, nullptr, nullptr, nullptr, traced_sun.Get());
    const auto darker = run(unclamped_buffer.Get(), nullptr, nullptr, nullptr, traced_sun.Get(), nullptr, darker_mask.Get());
    size_t sun_apart = 0, sun_bits = 0, darker_shown = 0;
    for (size_t value = 0; value < unbound.size(); ++value) {
      sun_apart += same(alone[value], game_at_sun[value]) ? 0 : 1;
      sun_bits += (std::bit_cast<uint32_t>(alone[value]) == std::bit_cast<uint32_t>(game_at_sun[value])) ? 0 : 1;
      darker_shown += same(alone[value], darker[value]) ? 0 : 1;
    }
    CHECK(sun_apart == 0, "%s: %zu values of t51 0.37 over the mask 0.2 differ from t_shadow 0.37 unbound", name.c_str(), sun_apart);
    CHECK(darker_shown > 0, "CONTROL %s: t51 0.37 gave the darker's picture (min with the mask 0.2)", name.c_str());
    std::printf("%s sun in place: t51 0.37 over the mask 0.2 = t_shadow 0.37 (%zu apart, %zu not to the bit); the darker's picture %zu values "
                "apart\n",
                name.c_str(), sun_apart, sun_bits, darker_shown);
  }
}

// 14. shaders/autoexposure00 (the game's auto exposure, cs_compute_tone_mapper_brightness 0x8233E11F: the mean luma along four
//     focal points' crosses) on a 64 x 64 picture of luma 0.2 with a fire (five texels of 400) on the first cross, the last
//     frame's brightness 1 (a whole second's adaptation, so the result is the target): Modded Auto Exposure off (b13 all 0)
//     the game's cso's brightness; on (4 stops) brighter than off (the fire weighs less). CONTROL: on without the fire the
//     same as off.
static void TestAutoExposure(const std::string& games, const std::string& folder) {
  std::ifstream file(folder + "/0x8233E11F.cs_5_0.cso", std::ios::binary);
  const std::vector<char> game_code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const std::wstring path(games.begin(), games.end());
  FolderInclude include(path + L"/twshogun2completeedition/shaders");
  ComPtr<ID3DBlob> ours_code;
  ComPtr<ID3DBlob> errors;
  if (game_code.empty()
      || FAILED(D3DCompileFromFile((path + L"/twshogun2completeedition/shaders/autoexposure00_0x8233E11F.cs_5_0.hlsl").c_str(), nullptr, &include,
                                   "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, ours_code.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("missing 0x8233E11F, or %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    std::exit(2);
  }
  ComPtr<ID3D11ComputeShader> game;
  ComPtr<ID3D11ComputeShader> ours;
  device->CreateComputeShader(game_code.data(), game_code.size(), nullptr, game.GetAddressOf());
  device->CreateComputeShader(ours_code->GetBufferPointer(), ours_code->GetBufferSize(), nullptr, ours.GetAddressOf());
  const std::array<float, 4> tone_mapping = {1.f, 0.f, 0.f, 0.f};
  const auto tone_mapping_buffer = Constants(tone_mapping.data(), sizeof(tone_mapping));
  // Auto_exposure_cb: 4 points (c0.x), their places (c1-c4.xy), the cross radius 8 (c32.z), target exposure -2 (c32.w),
  // adaptation speeds 10 (c33.xy), max correction 10 (c33.z), sensitivity 0 (c33.w), a second's delta time (c34.x).
  std::array<float, 35 * 4> exposure = {};
  exposure[0] = std::bit_cast<float>(4u);
  const std::array<std::array<float, 2>, 4> points = {{{0.25f, 0.25f}, {0.75f, 0.25f}, {0.25f, 0.75f}, {0.75f, 0.75f}}};
  for (size_t point = 0; point < points.size(); ++point) std::ranges::copy(points[point], exposure.begin() + ((1 + point) * 4));
  exposure[(32 * 4) + 2] = std::bit_cast<float>(8u);
  exposure[(32 * 4) + 3] = -2.f;
  exposure[(33 * 4) + 0] = exposure[(33 * 4) + 1] = 10.f;
  exposure[(33 * 4) + 2] = 10.f;
  exposure[34 * 4] = 1.f;
  const auto exposure_buffer = Constants(exposure.data(), sizeof(exposure));
  const auto injection = [](float protection) {
    ShaderInjectData data = {};
    data.auto_exposure_highlight_protection = protection;
    data.auto_exposure_highlight_headroom_stops = 4.f;
    std::array<float, ((sizeof(ShaderInjectData) + 15) / 16) * 4> registers = {};
    std::memcpy(registers.data(), &data, sizeof(data));
    return Constants(registers.data(), sizeof(registers));
  };
  const std::array<ComPtr<ID3D11Buffer>, 2> protections = {injection(0.f), injection(1.f)};
  // The brightness one dispatch leaves from the last frame's 1.
  const auto run = [&](ID3D11ComputeShader* shader, bool fire, ID3D11Buffer* protection) {
    std::vector<std::array<float, 4>> picture(64 * 64, {0.2f, 0.2f, 0.2f, 1.f});
    if (fire) {
      for (int x = 14; x <= 18; ++x) picture[(16 * 64) + x] = {400.f, 400.f, 400.f, 1.f};
    }
    const auto picture_view = Srv(Texture(64, 64, DXGI_FORMAT_R32G32B32A32_FLOAT, picture.data(), 64 * 16, D3D11_BIND_SHADER_RESOURCE).Get());
    const std::array<float, 4> last = {1.f, 0.f, 0.f, 0.f};
    const D3D11_BUFFER_DESC output_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                           .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = 16};
    const D3D11_SUBRESOURCE_DATA output_data = {.pSysMem = last.data()};
    ComPtr<ID3D11Buffer> output;
    device->CreateBuffer(&output_desc, &output_data, output.GetAddressOf());
    const D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {.Format = DXGI_FORMAT_UNKNOWN, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
                                                       .Buffer = {.FirstElement = 0, .NumElements = 1}};
    ComPtr<ID3D11UnorderedAccessView> output_view;
    device->CreateUnorderedAccessView(output.Get(), &uav_desc, output_view.GetAddressOf());
    const std::array<ID3D11Buffer*, 2> constants = {tone_mapping_buffer.Get(), exposure_buffer.Get()};
    context->CSSetShader(shader, nullptr, 0);
    context->CSSetConstantBuffers(0, 2, constants.data());
    if (protection != nullptr) context->CSSetConstantBuffers(13, 1, &protection);
    context->CSSetShaderResources(0, 1, picture_view.GetAddressOf());
    context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
    context->Dispatch(1, 1, 1);
    context->ClearState();
    const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Buffer> staging;
    device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf());
    context->CopyResource(staging.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    float brightness = 0.f;
    std::memcpy(&brightness, mapped.pData, 4);
    context->Unmap(staging.Get(), 0);
    return brightness;
  };
  const float game_fire = run(game.Get(), true, nullptr);
  const float off_fire = run(ours.Get(), true, protections[0].Get());
  const float on_fire = run(ours.Get(), true, protections[1].Get());
  const float off_plain = run(ours.Get(), false, protections[0].Get());
  const float on_plain = run(ours.Get(), false, protections[1].Get());
  CHECK(Near(off_fire, game_fire, 1e-5f), "autoexposure00 off: brightness %f, the game's %f", off_fire, game_fire);
  CHECK(on_fire > off_fire * 1.5f, "autoexposure00 on: brightness %f, off %f", on_fire, off_fire);
  CHECK(on_plain == off_plain, "CONTROL autoexposure00 without the fire: on %f, off %f", on_plain, off_plain);
  std::printf("autoexposure00: with a fire on a cross off %.4f (the game's %.4f), Modded Auto Exposure on %.4f; without it %.4f both\n",
              off_fire, game_fire, on_fire, on_plain);
}

// 15. shaders/indicator00-02 (indicator ps30_main, ps30_main_deferred, ps30_main_to_overlay) against the game's csos on a 32 x 32
//     target cleared to -1 (discarded pixels keep it), random textures (indicator colour, G-buffer constants, depth) and
//     interpolants, a view depth of 40 x the depth texture: in HDR at brightness 1 and in SDR at 2 the game's, bit for bit;
//     in HDR at 2 the colour twice the game's where it drew, alpha and discards the game's.
static void TestIndicators(const std::string& games, const std::string& folder) {
  constexpr char VERTEX_SOURCE[] =
      "struct Out { float4 position : SV_Position; float4 uv : TEXCOORD0; float3 world : TEXCOORD1; float4 colour : COLOR0;"
      "  float4 depth : TEXCOORD2; };"
      "Out main(uint id : SV_VertexID) {"
      "  Out o; const float2 p = float2((id << 1) & 2, id & 2);"
      "  o.position = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);"
      "  o.uv = float4(p * 3.7f, 0, 0); o.world = float3(p.x * 100, p.y * 80 - 20, 0);"
      "  o.colour = float4(0.3f + p.x, 0.9f - p.y * 0.2f, 0.5f, 0.2f + p.y); o.depth = float4(0, 0, p.x * 60 + p.y * 30, 1);"
      "  return o; }";
  ComPtr<ID3DBlob> vertex_code;
  D3DCompile(VERTEX_SOURCE, sizeof(VERTEX_SOURCE) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vertex_code.GetAddressOf(), nullptr);
  ComPtr<ID3D11VertexShader> vertex;
  device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex.GetAddressOf());
  std::mt19937 random(15);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  const auto texture = [&](UINT size, DXGI_FORMAT format, UINT channels) {
    std::vector<float> texels(static_cast<size_t>(size) * size * channels);
    for (auto& texel : texels) texel = unit(random);
    return Srv(Texture(size, size, format, texels.data(), size * channels * 4, D3D11_BIND_SHADER_RESOURCE).Get());
  };
  const std::array<ComPtr<ID3D11ShaderResourceView>, 3> views = {texture(8, DXGI_FORMAT_R32G32B32A32_FLOAT, 4),
                                                                 texture(8, DXGI_FORMAT_R32G32B32A32_FLOAT, 4),
                                                                 texture(32, DXGI_FORMAT_R32_FLOAT, 1)};
  std::array<float, 44 * 4> camera = {};
  camera[1] = 30.f;                    // camera_position.y
  camera[(28 * 4) + 2] = 40.f;         // inv_projection's z column: view depth 40 x the depth
  camera[(29 * 4) + 3] = 1.f;          // its w column
  camera[(35 * 4) + 2] = 0.5f;         // g_texel_offset
  camera[(37 * 4) + 0] = camera[(37 * 4) + 1] = 32.f;  // screen_size
  const auto camera_buffer = Constants(camera.data(), sizeof(camera));
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_WRAP, .AddressW = D3D11_TEXTURE_ADDRESS_WRAP, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf());
  const auto injection = [](float hdr, float brightness) {
    ShaderInjectData data = {};
    data.hdr = hdr;
    data.indicator_brightness = brightness;
    std::array<float, ((sizeof(ShaderInjectData) + 15) / 16) * 4> registers = {};
    std::memcpy(registers.data(), &data, sizeof(data));
    return Constants(registers.data(), sizeof(registers));
  };
  const std::array<ComPtr<ID3D11Buffer>, 3> settings = {injection(1.f, 1.f), injection(0.f, 2.f), injection(1.f, 2.f)};
  const std::wstring path(games.begin(), games.end());
  for (const auto& [hash, name] : std::array<std::pair<const char*, const wchar_t*>, 3>{
           {{"D12FA660", L"indicator00_0xD12FA660"}, {"C454C1EA", L"indicator01_0xC454C1EA"}, {"C57C222D", L"indicator02_0xC57C222D"}}}) {
    std::ifstream file(folder + "/0x" + hash + ".ps_5_0.cso", std::ios::binary);
    const std::vector<char> game_code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    FolderInclude include(path + L"/twshogun2completeedition/shaders");
    ComPtr<ID3DBlob> ours_code;
    ComPtr<ID3DBlob> errors;
    if (game_code.empty()
        || FAILED(D3DCompileFromFile((path + L"/twshogun2completeedition/shaders/" + name + L".ps_5_0.hlsl").c_str(), nullptr, &include, "main",
                                     "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, ours_code.GetAddressOf(), errors.GetAddressOf()))) {
      std::printf("missing 0x%s, or %s\n", hash, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
      std::exit(2);
    }
    ComPtr<ID3D11PixelShader> game;
    ComPtr<ID3D11PixelShader> ours;
    device->CreatePixelShader(game_code.data(), game_code.size(), nullptr, game.GetAddressOf());
    device->CreatePixelShader(ours_code->GetBufferPointer(), ours_code->GetBufferSize(), nullptr, ours.GetAddressOf());
    const auto run = [&](ID3D11PixelShader* pixel_shader, ID3D11Buffer* injected) {
      const auto target = Texture(32, 32, DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_RENDER_TARGET);
      ComPtr<ID3D11RenderTargetView> target_view;
      device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf());
      const std::array<float, 4> cleared = {-1.f, -1.f, -1.f, -1.f};
      context->ClearRenderTargetView(target_view.Get(), cleared.data());
      std::array<ID3D11ShaderResourceView*, 3> bound = {views[0].Get(), views[1].Get(), views[2].Get()};
      const D3D11_VIEWPORT viewport = {.Width = 32.f, .Height = 32.f, .MaxDepth = 1.f};
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->VSSetShader(vertex.Get(), nullptr, 0);
      context->PSSetShader(pixel_shader, nullptr, 0);
      context->PSSetShaderResources(0, 3, bound.data());
      context->PSSetSamplers(0, 1, sampler.GetAddressOf());
      context->PSSetConstantBuffers(0, 1, camera_buffer.GetAddressOf());
      context->PSSetConstantBuffers(13, 1, &injected);
      context->RSSetViewports(1, &viewport);
      context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
      context->Draw(3, 0);
      context->ClearState();
      const auto bytes = Read(target.Get(), 16);
      std::vector<float> values(bytes.size() / 4);
      std::memcpy(values.data(), bytes.data(), bytes.size());
      return values;
    };
    const auto reference = run(game.Get(), nullptr);
    const auto neutral = run(ours.Get(), settings[0].Get());
    const auto sdr = run(ours.Get(), settings[1].Get());
    const auto brighter = run(ours.Get(), settings[2].Get());
    size_t differs = 0, drawn = 0, discarded = 0, bad_brighter = 0;
    for (size_t value = 0; value < reference.size(); ++value) {
      differs += (neutral[value] == reference[value] && sdr[value] == reference[value]) ? 0 : 1;
      const bool kept = reference[value - (value % 4) + 3] != -1.f;
      const float expected = (kept && (value % 4) < 3) ? reference[value] * 2.f : reference[value];
      bad_brighter += (brighter[value] == expected) ? 0 : 1;
      if (value % 4 == 3) (kept ? drawn : discarded) += 1;
    }
    CHECK(differs == 0 && bad_brighter == 0, "%ls: %zu values differ at 1 / in SDR, %zu not twice the colour at 2", name, differs, bad_brighter);
    CHECK(drawn > 0, "CONTROL %ls: nothing drawn", name);
    std::printf("%ls: %zu of 1024 pixels drawn (%zu discarded) the game's at 1 and in SDR, twice its colour at 2 in HDR\n", name, drawn,
                discarded);
  }
}

// 16. shaders/campaignoverlay00 (the campaign map's terrain overlay, the game's 0xD7E27B35): at 100% (16 x 16) the game's bit for
//     bit; at 33% (the same depth in the top-left 16 x 16 of a 48 x 48 target, the viewport 16 x 16, the camera's display size
//     48) the same picture there within 1e-4 (its lookups blend bilinearly, so the map positions' rounding moves it little).
//     CONTROL: the game's at 33% differs from its 100%.
static void TestCampaignOverlay(const std::string& games, const std::string& folder) {
  constexpr char VERTEX_SOURCE[] =
      "float4 main(uint id : SV_VertexID) : SV_Position {"
      "  const float2 p = float2((id << 1) & 2, id & 2); return float4(p * float2(2, -2) + float2(-1, 1), 0, 1); }";
  ComPtr<ID3DBlob> vertex_code;
  D3DCompile(VERTEX_SOURCE, sizeof(VERTEX_SOURCE) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vertex_code.GetAddressOf(), nullptr);
  ComPtr<ID3D11VertexShader> vertex;
  device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex.GetAddressOf());
  std::ifstream file(folder + "/0xD7E27B35.ps_5_0.cso", std::ios::binary);
  const std::vector<char> game_code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const std::wstring path(games.begin(), games.end());
  ComPtr<ID3DBlob> ours_code;
  ComPtr<ID3DBlob> errors;
  if (game_code.empty()
      || FAILED(D3DCompileFromFile((path + L"/twshogun2completeedition/shaders/campaignoverlay00_0xD7E27B35.ps_5_0.hlsl").c_str(), nullptr,
                                   D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, ours_code.GetAddressOf(),
                                   errors.GetAddressOf()))) {
    std::printf("missing 0xD7E27B35, or %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    std::exit(2);
  }
  ComPtr<ID3D11PixelShader> game;
  ComPtr<ID3D11PixelShader> ours;
  device->CreatePixelShader(game_code.data(), game_code.size(), nullptr, game.GetAddressOf());
  device->CreatePixelShader(ours_code->GetBufferPointer(), ours_code->GetBufferSize(), nullptr, ours.GetAddressOf());
  constexpr UINT SIZE = 16;
  std::mt19937 random(16);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  std::vector<float> depth(SIZE * SIZE);
  for (auto& value : depth) value = unit(random);
  std::vector<uint32_t> lookup(64);
  for (auto& value : lookup) value = static_cast<uint32_t>(random() % 8u);
  std::vector<float> palette(8 * 4);
  for (auto& value : palette) value = unit(random);
  const auto lookup_view = Srv(Texture(8, 8, DXGI_FORMAT_R32_UINT, lookup.data(), 32, D3D11_BIND_SHADER_RESOURCE).Get());
  const auto palette_view = Srv(Texture(8, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, palette.data(), 128, D3D11_BIND_SHADER_RESOURCE).Get());
  // The map: x 0.5 ndc.x + 0.5, y 0.5 ndc.y + 0.1 depth + 0.5, w 1 (cb0[30], [32], [33]); the area 0..1, blend 0.8.
  const std::array<float, 12> shroud = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f};
  const auto shroud_buffer = Constants(shroud.data(), sizeof(shroud));
  const std::array<float, 4> overlay = {0.8f, 0.f, 0.f, 0.f};
  const auto overlay_buffer = Constants(overlay.data(), sizeof(overlay));
  // One pass at `scale` (1 or 3): the target and textures scale x SIZE, the depth in their top-left, the viewport SIZE.
  const auto run = [&](ID3D11PixelShader* pixel_shader, UINT scale) {
    const UINT size = SIZE * scale;
    std::vector<float> placed(static_cast<size_t>(size) * size);
    for (auto& value : placed) value = unit(random);
    for (UINT y = 0; y < SIZE; ++y) std::copy_n(depth.begin() + (y * SIZE), SIZE, placed.begin() + (y * size));
    const auto depth_view = Srv(Texture(size, size, DXGI_FORMAT_R32_FLOAT, placed.data(), size * 4, D3D11_BIND_SHADER_RESOURCE).Get());
    const auto colour_view = Srv(Texture(size, size, DXGI_FORMAT_R32_FLOAT, placed.data(), size * 4, D3D11_BIND_SHADER_RESOURCE).Get());
    std::array<float, 43 * 4> camera = {};
    camera[(30 * 4) + 0] = 0.5f;
    camera[(30 * 4) + 3] = 0.5f;
    camera[(32 * 4) + 1] = 0.5f;
    camera[(32 * 4) + 2] = 0.1f;
    camera[(32 * 4) + 3] = 0.5f;
    camera[(33 * 4) + 3] = 1.f;
    camera[(37 * 4) + 0] = camera[(37 * 4) + 1] = static_cast<float>(size);
    camera[(38 * 4) + 0] = camera[(38 * 4) + 1] = static_cast<float>(SIZE);
    camera[(42 * 4) + 0] = camera[(42 * 4) + 1] = 1.f / static_cast<float>(size - 1);
    const auto camera_buffer = Constants(camera.data(), sizeof(camera));
    const auto target = Texture(size, size, DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_RENDER_TARGET);
    ComPtr<ID3D11RenderTargetView> target_view;
    device->CreateRenderTargetView(target.Get(), nullptr, target_view.GetAddressOf());
    const std::array<ID3D11ShaderResourceView*, 4> views = {colour_view.Get(), depth_view.Get(), lookup_view.Get(), palette_view.Get()};
    const std::array<ID3D11Buffer*, 3> constants = {camera_buffer.Get(), shroud_buffer.Get(), overlay_buffer.Get()};
    const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(SIZE), .Height = static_cast<float>(SIZE), .MaxDepth = 1.f};
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex.Get(), nullptr, 0);
    context->PSSetShader(pixel_shader, nullptr, 0);
    context->PSSetShaderResources(0, 4, views.data());
    context->PSSetConstantBuffers(0, 3, constants.data());
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
    context->Draw(3, 0);
    context->ClearState();
    const auto bytes = Read(target.Get(), 16);
    std::vector<float> values(SIZE * SIZE * 4);
    for (UINT y = 0; y < SIZE; ++y) std::memcpy(values.data() + (y * SIZE * 4), bytes.data() + (static_cast<size_t>(y) * size * 16), SIZE * 16);
    return values;
  };
  const auto game_full = run(game.Get(), 1);
  const auto ours_full = run(ours.Get(), 1);
  const auto game_third = run(game.Get(), 3);
  const auto ours_third = run(ours.Get(), 3);
  size_t full_differs = 0, third_differs = 0, control_differs = 0;
  float most_apart = 0.f;
  for (size_t value = 0; value < game_full.size(); ++value) {
    full_differs += (ours_full[value] == game_full[value]) ? 0 : 1;
    most_apart = std::max(most_apart, std::fabs(ours_third[value] - ours_full[value]));
    third_differs += (std::fabs(ours_third[value] - ours_full[value]) <= 1e-4f) ? 0 : 1;
    control_differs += (std::fabs(game_third[value] - game_full[value]) <= 1e-4f) ? 0 : 1;
  }
  CHECK(full_differs == 0, "campaignoverlay00 at 100%%: %zu values differ from the game's", full_differs);
  CHECK(third_differs == 0, "campaignoverlay00 at 33%%: %zu values apart from 100%% (at most %g)", third_differs, most_apart);
  CHECK(control_differs > 0, "CONTROL campaignoverlay00: the game's 33%% the same as its 100%%");
  std::printf("campaignoverlay00: 100%% the game's bit for bit; 33%% within %g of 100%%; the game's 33%% %zu of %zu values apart\n", most_apart,
              control_differs, game_full.size());
}

// 17. rt_particle_quads (Smoke Shadows' records from particle_vertex_30 0xBEA3F2D4's outputs: TEXCOORD5 world, COLOR0, the two
//     cells' uv in TEXCOORD0.xy / TEXCOORD1.xy, their blend TEXCOORD3.w, the alpha scale TEXCOORD4.z) on 48 random rectangles in
//     space, each two triangles (the first's corners in any of three orders, its right angle anywhere), drawn as the game draws a
//     quad an instance: one record per quad, its centre the rectangle's, its size its two sides, its colour COLOR0's, its
//     opacity saturate(TEXCOORD4.z x COLOR0.w x lerp(alpha at the two cells' centre uv)) with a texture whose alpha is its u
//     (one mip). CONTROL: the second triangles write nothing (48 records for 96 triangles).
static void TestParticleQuads(const std::string& games) {
  struct QuadCorner {
    std::array<float, 4> position, colour, uv, uv_next, view, alpha, world;
  };
  constexpr char VERTEX_SOURCE[] =
      "struct Corner { float4 position : SV_POSITION; float4 colour : COLOR0; float4 uv : TEXCOORD0; float4 uv_next : TEXCOORD1;"
      "  float4 view : TEXCOORD3; float4 alpha : TEXCOORD4; float4 world : TEXCOORD5; };"
      "Corner main(Corner corner) { return corner; }";
  const std::array<D3D11_INPUT_ELEMENT_DESC, 7> layout = {{
      {"SV_POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0}}};
  ComPtr<ID3DBlob> vertex_code;
  D3DCompile(VERTEX_SOURCE, sizeof(VERTEX_SOURCE) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vertex_code.GetAddressOf(), nullptr);
  const std::wstring path(games.begin(), games.end());
  FolderInclude include(path + L"/twshogun2completeedition");
  ComPtr<ID3DBlob> geometry_code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompileFromFile((path + L"/twshogun2completeedition/rt_particle_quads.gs_5_0.hlsl").c_str(), nullptr, &include, "main",
                                "gs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, geometry_code.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("rt_particle_quads: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    std::exit(2);
  }
  const std::array<D3D11_SO_DECLARATION_ENTRY, 4> entries = {{{0, "SV_POSITION", 0, 0, 4, 0}, {0, "COLOR", 0, 0, 4, 0},
                                                               {0, "TEXCOORD", 3, 0, 3, 0}, {0, "TEXCOORD", 0, 0, 1, 0}}};
  const UINT stride = 48;
  ComPtr<ID3D11GeometryShader> geometry;
  device->CreateGeometryShaderWithStreamOutput(geometry_code->GetBufferPointer(), geometry_code->GetBufferSize(), entries.data(),
                                               static_cast<UINT>(entries.size()), &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                               geometry.GetAddressOf());
  ComPtr<ID3D11VertexShader> vertex;
  device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex.GetAddressOf());
  ComPtr<ID3D11InputLayout> input;
  device->CreateInputLayout(layout.data(), static_cast<UINT>(layout.size()), vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(),
                            input.GetAddressOf());
  // The texture: alpha = u (16 x 16, one mip), point sampled at its texel centres' grid.
  std::vector<std::array<float, 4>> texels(256);
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) texels[(y * 16) + x] = {1.f, 1.f, 1.f, (static_cast<float>(x) + 0.5f) / 16.f};
  }
  const auto texture_view = Srv(Texture(16, 16, DXGI_FORMAT_R32G32B32A32_FLOAT, texels.data(), 16 * 16, D3D11_BIND_SHADER_RESOURCE).Get());
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf());
  std::array<float, 39 * 4> camera = {};
  camera[0] = 1000.f;  // the camera far from every quad
  camera[(38 * 4) + 0] = camera[(38 * 4) + 1] = 1024.f;
  const auto camera_buffer = Constants(camera.data(), sizeof(camera));
  std::mt19937 random(17);
  std::uniform_real_distribution<float> spread(-1.f, 1.f);
  struct Expected {
    std::array<float, 3> centre;
    std::array<float, 2> size;
    std::array<float, 4> colour;
  };
  std::vector<QuadCorner> corners;
  std::vector<Expected> expected;
  for (int quad = 0; quad < 48; ++quad) {
    const std::array<float, 3> centre = {50.f * spread(random), 50.f * spread(random), 50.f * spread(random)};
    // Two perpendicular axes: a random unit vector and one across it.
    std::array<float, 3> axis_a = {spread(random), spread(random), spread(random)};
    const float length_a = std::hypot(axis_a[0], axis_a[1], axis_a[2]);
    for (auto& value : axis_a) value /= length_a;
    const std::array<float, 3> helper = (std::fabs(axis_a[1]) < 0.9f) ? std::array<float, 3>{0.f, 1.f, 0.f} : std::array<float, 3>{1.f, 0.f, 0.f};
    std::array<float, 3> axis_b = {(axis_a[1] * helper[2]) - (axis_a[2] * helper[1]), (axis_a[2] * helper[0]) - (axis_a[0] * helper[2]),
                                   (axis_a[0] * helper[1]) - (axis_a[1] * helper[0])};
    const float length_b = std::hypot(axis_b[0], axis_b[1], axis_b[2]);
    for (auto& value : axis_b) value /= length_b;
    const std::array<float, 2> size = {1.f + (2.f * (spread(random) + 1.f)), 1.f + (2.f * (spread(random) + 1.f))};
    const std::array<float, 4> colour = {0.5f + (0.5f * spread(random)), 0.5f + (0.5f * spread(random)), 0.5f + (0.5f * spread(random)),
                                         0.5f + (0.5f * spread(random))};
    const float blend = 0.5f + (0.5f * spread(random));
    const float scale = 0.75f + (0.5f * spread(random));
    // Its uv: the first cell over u 0.1-0.4, the second over 0.5-0.9 (both along axis a).
    const auto corner_at = [&](float a, float b) {
      QuadCorner corner = {};
      for (int axis = 0; axis < 3; ++axis) corner.world[axis] = centre[axis] + (axis_a[axis] * a * size[0]) + (axis_b[axis] * b * size[1]);
      corner.world[3] = 1.f;
      corner.position = {a, b, 0.5f, 1.f};
      corner.colour = colour;
      corner.uv = {0.25f + (0.3f * a), 0.5f + (0.3f * b), 0.f, 0.f};
      corner.uv_next = {0.7f + (0.4f * a), 0.5f + (0.3f * b), 0.f, 0.f};
      corner.view = {0.f, 0.f, 0.f, blend};
      corner.alpha = {0.f, 0.f, scale, 0.f};
      return corner;
    };
    const std::array<QuadCorner, 4> quad_corners = {corner_at(-0.5f, -0.5f), corner_at(0.5f, -0.5f), corner_at(-0.5f, 0.5f), corner_at(0.5f, 0.5f)};
    // The first triangle 0, 1, 2 (its right angle at corner 0), rotated by the quad's number; the second 2, 1, 3.
    const std::array<int, 3> first = {0, 1, 2};
    for (int index = 0; index < 3; ++index) corners.push_back(quad_corners[first[(index + quad) % 3]]);
    for (const int index : {2, 1, 3}) corners.push_back(quad_corners[index]);
    const float alpha = std::clamp(scale * (colour[3] * (0.25f + (blend * (0.7f - 0.25f)))), 0.f, 1.f);
    expected.push_back({.centre = centre, .size = size, .colour = {colour[0], colour[1], colour[2], alpha}});
  }
  const auto count = static_cast<UINT>(corners.size());
  const D3D11_BUFFER_DESC vertex_desc = {.ByteWidth = count * static_cast<UINT>(sizeof(QuadCorner)), .Usage = D3D11_USAGE_DEFAULT,
                                         .BindFlags = D3D11_BIND_VERTEX_BUFFER};
  const D3D11_SUBRESOURCE_DATA vertex_data = {.pSysMem = corners.data()};
  ComPtr<ID3D11Buffer> vertex_buffer;
  device->CreateBuffer(&vertex_desc, &vertex_data, vertex_buffer.GetAddressOf());
  const D3D11_BUFFER_DESC out_desc = {.ByteWidth = count * stride, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_STREAM_OUTPUT};
  ComPtr<ID3D11Buffer> out;
  device->CreateBuffer(&out_desc, nullptr, out.GetAddressOf());
  ComPtr<ID3D11Query> statistics;
  const D3D11_QUERY_DESC query_desc = {.Query = D3D11_QUERY_SO_STATISTICS};
  device->CreateQuery(&query_desc, statistics.GetAddressOf());
  const UINT vertex_stride = sizeof(QuadCorner);
  const UINT offset = 0;
  context->IASetInputLayout(input.Get());
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &vertex_stride, &offset);
  context->VSSetShader(vertex.Get(), nullptr, 0);
  context->GSSetShader(geometry.Get(), nullptr, 0);
  context->GSSetConstantBuffers(0, 1, camera_buffer.GetAddressOf());
  context->GSSetShaderResources(0, 1, texture_view.GetAddressOf());
  context->GSSetSamplers(3, 1, sampler.GetAddressOf());
  context->SOSetTargets(1, out.GetAddressOf(), &offset);
  context->Begin(statistics.Get());
  context->Draw(count, 0);
  context->End(statistics.Get());
  context->ClearState();
  D3D11_QUERY_DATA_SO_STATISTICS written = {};
  while (context->GetData(statistics.Get(), &written, sizeof(written), 0) != S_OK) {}
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = count * stride, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf());
  context->CopyResource(staging.Get(), out.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<std::array<float, 12>> records(count);
  std::memcpy(records.data(), mapped.pData, static_cast<size_t>(count) * stride);
  context->Unmap(staging.Get(), 0);
  size_t bad = 0;
  for (size_t quad = 0; quad < expected.size(); ++quad) {
    const auto& record = records[quad];
    const auto& want = expected[quad];
    bool same = true;
    for (int axis = 0; axis < 3; ++axis) same = same && std::fabs(record[axis] - want.centre[axis]) < 1e-3f;
    const std::array<float, 2> sizes = {std::min(record[8], record[9]), std::max(record[8], record[9])};
    same = same && std::fabs(sizes[0] - std::min(want.size[0], want.size[1])) < 1e-3f && std::fabs(sizes[1] - std::max(want.size[0], want.size[1])) < 1e-3f;
    for (int channel = 0; channel < 4; ++channel) same = same && std::fabs(record[4 + channel] - want.colour[channel]) < 2e-3f;
    bad += same ? 0 : 1;
  }
  CHECK(bad == 0 && written.NumPrimitivesWritten == expected.size(), "rt_particle_quads: %zu of %zu records wrong, %llu written", bad,
        expected.size(), written.NumPrimitivesWritten);
  std::printf("rt_particle_quads: %zu quads (their first triangles' corners in three orders) one record each, centre, size, colour and "
              "opacity right; %llu records for %u triangles\n",
              expected.size(), written.NumPrimitivesWritten, count / 3);
}

// 18. rt_capture_displacement and rt_capture_terrain_patches (Rarer Objects in the Rays: the patches the game tessellates as the
//     triangles they are) against the game's tessellated draws streamed out of their domain shaders: rigidcommon vs30_displacement
//     (0x5074655A, hull 0x91442562, domain 0x94CEC8A9) and terraintile vs30_infield_silouette_sm5 (0xC24D8705, 0x54F0069F,
//     0x4FE62BBA). 64 random patches 40-80 m in front of the camera (rigid: g_tesselation_factor 4, its fade there whole: one
//     triangle a patch, no displacement; terrain: its roughness under the threshold): each triangle's corners the game's bit for
//     bit (SV_Position, the rigid UV), whichever corner the tessellator starts with. CONTROLS: the rigid patches 3 m away
//     tessellate and move (displaced), rough terrain tessellates (more triangles).
struct PatchVertex {
  std::array<float, 4> position, uv, normal, tangent, bitangent, colour, row0, row1, row2, row3;
};

static const D3D11_INPUT_ELEMENT_DESC DISPLACEMENT_LAYOUT[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 112, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 128, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"POSITION", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 144, D3D11_INPUT_PER_VERTEX_DATA, 0}};
// terraintile's: POSITION0 (the world) and NORMAL0 (w: the roughness).
static const D3D11_INPUT_ELEMENT_DESC TERRAIN_PATCH_LAYOUT[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0}};

static std::vector<char> GameCode(const std::string& folder, const char* name) {
  std::ifstream file(folder + "/0x" + name, std::ios::binary);
  std::vector<char> code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (code.empty()) {
    std::printf("missing 0x%s\n", name);
    std::exit(2);
  }
  return code;
}

static void TestPatches(const std::string& games, const std::string& folder) {
  const auto rigid_shaders = Shaders(games, folder, "5074655A", L"rt_capture_displacement");
  const auto terrain_shaders = Shaders(games, folder, "C24D8705", L"rt_capture_terrain_patches");
  const auto rigid_hull = GameCode(folder, "91442562.hs_5_0.cso");
  const auto rigid_domain = GameCode(folder, "94CEC8A9.ds_5_0.cso");
  const auto terrain_hull = GameCode(folder, "54F0069F.hs_5_0.cso");
  const auto terrain_domain = GameCode(folder, "4FE62BBA.ds_5_0.cso");
  constexpr UINT PATCHES = 64;
  constexpr UINT MOST_TRIANGLES = PATCHES * 128;
  // A projection in camera_VS_PS c10-13 (columns; w the depth; the view the identity: the camera at 0 looking along +z, view's c4 (0, 0, 1) for the hull's back-face test).
  std::array<float, 44 * 4> camera = {};
  for (int row = 0; row < 4; ++row) camera[((2 + row) * 4) + row] = 1.f;
  const std::array<float, 16> projection = {1.2f, 0.03f, 0.f, 0.f, -0.02f, 1.7f, 0.f, 0.f, 0.01f, -0.04f, 1.0001f, -0.10001f, 0.f, 0.f, 1.f, 0.f};
  std::ranges::copy(projection, camera.begin() + (10 * 4));
  const auto camera_buffer = Constants(camera.data(), sizeof(camera));
  const std::array<float, 12> wind = {};
  const auto wind_buffer = Constants(wind.data(), sizeof(wind));
  // rigid_config_PS: g_tesselation_factor 4 (c1.y), g_tesselation_displacement_scale 0.1 (c1.z).
  const std::array<float, 12> rigid_config = {0.f, 0.f, 0.f, 0.f, 0.f, 4.f, 0.1f, 0.f, 0.f, 0.f, 0.f, 0.f};
  const auto rigid_config_buffer = Constants(rigid_config.data(), sizeof(rigid_config));
  // terrain_VS_PS: near_terrain_size_meters 512 (c0.x), g_terrain_silouette_roughness_threshold 0.5 (c8.y); terrain_tile_VS_DS 256.
  std::array<float, 12 * 4> terrain = {};
  terrain[0] = 512.f;
  terrain[(8 * 4) + 1] = 0.5f;
  const auto terrain_buffer = Constants(terrain.data(), sizeof(terrain));
  const std::array<float, 4> tile = {256.f, 0.f, 0.f, 0.f};
  const auto tile_buffer = Constants(tile.data(), sizeof(tile));
  const std::vector<std::array<float, 4>> heights(16, {0.5f, 0.5f, 0.5f, 0.5f});
  const auto height_texture = Texture(4, 4, DXGI_FORMAT_R32G32B32A32_FLOAT, heights.data(), 4 * 16, D3D11_BIND_SHADER_RESOURCE);
  ComPtr<ID3D11ShaderResourceView> height_view;
  device->CreateShaderResourceView(height_texture.Get(), nullptr, height_view.GetAddressOf());
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_WRAP, .AddressW = D3D11_TEXTURE_ADDRESS_WRAP, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf());

  // Random patches `distance` m ahead, facing the camera (the hull shader drops those facing away), placed by rows turned about z.
  std::mt19937 random(18);
  const auto patches = [&random](float distance, float roughness) {
    std::uniform_real_distribution<float> unit(-1.f, 1.f);
    std::vector<PatchVertex> vertices;
    for (UINT patch = 0; patch < PATCHES; ++patch) {
      const float turn = unit(random) * 3.14159f;
      const std::array<float, 3> place = {unit(random) * 20.f, unit(random) * 10.f, distance + (unit(random) * 0.1f * distance)};
      std::array<std::array<float, 3>, 3> corners = {};
      for (auto& corner : corners) corner = {unit(random) * 2.f, unit(random) * 2.f, unit(random) * 0.3f};
      const std::array<float, 3> a = {corners[2][0] - corners[0][0], corners[2][1] - corners[0][1], corners[2][2] - corners[0][2]};
      const std::array<float, 3> b = {corners[1][0] - corners[0][0], corners[1][1] - corners[0][1], corners[1][2] - corners[0][2]};
      if ((a[0] * b[1]) - (a[1] * b[0]) < 0.f) std::swap(corners[1], corners[2]);
      for (const auto& corner : corners) {
        PatchVertex vertex = {.uv = {unit(random), unit(random), unit(random), unit(random)}, .normal = {0.5f, 0.5f, 1.f, roughness},
                              .tangent = {1.f, 0.5f, 0.5f, 0.f}, .bitangent = {0.5f, 1.f, 0.5f, 0.f}, .colour = {1.f, 1.f, 1.f, 1.f},
                              .row0 = {std::cos(turn), -std::sin(turn), 0.f, place[0]}, .row1 = {std::sin(turn), std::cos(turn), 0.f, place[1]},
                              .row2 = {0.f, 0.f, 1.f, place[2]}};
        if (roughness < 0.f) {
          vertex.position = {corner[0], corner[1], corner[2], 1.f};  // rigid: the mesh's, placed by the rows
        } else {
          // terrain: the world, as the rigid ones' rows would put it
          vertex.position = {(vertex.row0[0] * corner[0]) + (vertex.row0[1] * corner[1]) + place[0],
                             (vertex.row1[0] * corner[0]) + (vertex.row1[1] * corner[1]) + place[1], corner[2] + place[2], 1.f};
          vertex.normal = {unit(random) * 0.3f, unit(random) * 0.3f, 1.f, roughness};
        }
        vertices.push_back(vertex);
      }
    }
    return vertices;
  };
  // Each streamed vertex's SV_Position and (with `uv`) TEXCOORD1, out of the game's domain shader (`hull` given: patches) or our
  // capture version (a triangle list); the triangles written.
  struct Streamed {
    std::vector<std::array<float, 8>> vertices;
    UINT64 triangles = 0;
  };
  const auto stream = [&](std::span<const char> vertex_code, std::span<const char> hull, std::span<const char> domain,
                          std::span<const D3D11_INPUT_ELEMENT_DESC> layout, const std::vector<PatchVertex>& vertices,
                          std::span<ID3D11Buffer* const> vertex_constants, std::span<ID3D11Buffer* const> hull_constants,
                          std::span<ID3D11Buffer* const> domain_constants, bool uv) {
    const std::array<D3D11_SO_DECLARATION_ENTRY, 2> entries = {{
        {.SemanticName = "SV_POSITION", .ComponentCount = 4},
        {.SemanticName = "TEXCOORD", .SemanticIndex = 1, .ComponentCount = 4},
    }};
    const UINT stride = 32;
    const auto& streamed_code = hull.empty() ? vertex_code : domain;
    ComPtr<ID3D11GeometryShader> output;
    device->CreateGeometryShaderWithStreamOutput(streamed_code.data(), streamed_code.size(), entries.data(), uv ? 2 : 1, &stride, 1,
                                                 D3D11_SO_NO_RASTERIZED_STREAM, nullptr, output.GetAddressOf());
    ComPtr<ID3D11VertexShader> vertex_shader;
    device->CreateVertexShader(vertex_code.data(), vertex_code.size(), nullptr, vertex_shader.GetAddressOf());
    ComPtr<ID3D11HullShader> hull_shader;
    ComPtr<ID3D11DomainShader> domain_shader;
    if (!hull.empty()) {
      device->CreateHullShader(hull.data(), hull.size(), nullptr, hull_shader.GetAddressOf());
      device->CreateDomainShader(domain.data(), domain.size(), nullptr, domain_shader.GetAddressOf());
    }
    ComPtr<ID3D11InputLayout> input;
    device->CreateInputLayout(layout.data(), static_cast<UINT>(layout.size()), vertex_code.data(), vertex_code.size(), input.GetAddressOf());
    const auto count = static_cast<UINT>(vertices.size());
    const D3D11_BUFFER_DESC vertex_desc = {.ByteWidth = count * static_cast<UINT>(sizeof(PatchVertex)), .Usage = D3D11_USAGE_DEFAULT,
                                           .BindFlags = D3D11_BIND_VERTEX_BUFFER};
    const D3D11_SUBRESOURCE_DATA vertex_data = {.pSysMem = vertices.data()};
    ComPtr<ID3D11Buffer> vertex_buffer;
    device->CreateBuffer(&vertex_desc, &vertex_data, vertex_buffer.GetAddressOf());
    const D3D11_BUFFER_DESC out_desc = {.ByteWidth = MOST_TRIANGLES * 3 * stride, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_STREAM_OUTPUT};
    ComPtr<ID3D11Buffer> out;
    device->CreateBuffer(&out_desc, nullptr, out.GetAddressOf());
    ComPtr<ID3D11Query> statistics;
    const D3D11_QUERY_DESC query_desc = {.Query = D3D11_QUERY_SO_STATISTICS};
    device->CreateQuery(&query_desc, statistics.GetAddressOf());
    const UINT vertex_stride = sizeof(PatchVertex);
    const UINT offset = 0;
    context->IASetInputLayout(input.Get());
    context->IASetPrimitiveTopology(hull.empty() ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &vertex_stride, &offset);
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    context->VSSetConstantBuffers(0, static_cast<UINT>(vertex_constants.size()), vertex_constants.data());
    context->HSSetShader(hull_shader.Get(), nullptr, 0);
    context->HSSetConstantBuffers(0, static_cast<UINT>(hull_constants.size()), hull_constants.data());
    context->DSSetShader(domain_shader.Get(), nullptr, 0);
    context->DSSetConstantBuffers(0, static_cast<UINT>(domain_constants.size()), domain_constants.data());
    context->DSSetShaderResources(0, 1, height_view.GetAddressOf());
    context->DSSetSamplers(0, 1, sampler.GetAddressOf());
    context->GSSetShader(output.Get(), nullptr, 0);
    context->SOSetTargets(1, out.GetAddressOf(), &offset);
    context->Begin(statistics.Get());
    context->Draw(count, 0);
    context->End(statistics.Get());
    context->ClearState();
    D3D11_QUERY_DATA_SO_STATISTICS written = {};
    while (context->GetData(statistics.Get(), &written, sizeof(written), 0) != S_OK) {}
    const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = out_desc.ByteWidth, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Buffer> staging;
    device->CreateBuffer(&staging_desc, nullptr, staging.GetAddressOf());
    context->CopyResource(staging.Get(), out.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    Streamed result = {.vertices = std::vector<std::array<float, 8>>(std::min<UINT64>(written.NumPrimitivesWritten, MOST_TRIANGLES) * 3),
                       .triangles = written.NumPrimitivesWritten};
    std::memcpy(result.vertices.data(), mapped.pData, result.vertices.size() * stride);
    context->Unmap(staging.Get(), 0);
    return result;
  };
  // Triangles whose corners (the first `floats` of each) are not the game's, in any order.
  const auto unlike = [](const Streamed& game, const Streamed& ours, int floats) {
    size_t bad = 0;
    for (size_t triangle = 0; triangle < ours.vertices.size() / 3; ++triangle) {
      for (size_t corner = 0; corner < 3; ++corner) {
        const auto& our = ours.vertices[(triangle * 3) + corner];
        bool found = false;
        for (size_t other = 0; other < 3 && (triangle * 3) + other < game.vertices.size(); ++other) {
          found = found || std::equal(our.begin(), our.begin() + floats, game.vertices[(triangle * 3) + other].begin(),
                                      [](float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); });
        }
        if (!found) {
          ++bad;
          break;
        }
      }
    }
    return bad;
  };

  const std::array<ID3D11Buffer*, 3> rigid_vertex_constants = {camera_buffer.Get(), wind_buffer.Get(), rigid_config_buffer.Get()};
  const std::array<ID3D11Buffer*, 2> rigid_constants = {camera_buffer.Get(), rigid_config_buffer.Get()};
  const std::span<const char> rigid_ours(static_cast<const char*>(rigid_shaders.second->GetBufferPointer()), rigid_shaders.second->GetBufferSize());
  const auto rigid = [&](float distance) {
    const auto vertices = patches(distance, -1.f);
    return std::pair{stream(rigid_shaders.first, rigid_hull, rigid_domain, DISPLACEMENT_LAYOUT, vertices, rigid_vertex_constants, rigid_constants,
                            rigid_constants, true),
                     stream(rigid_ours, {}, {}, DISPLACEMENT_LAYOUT, vertices, rigid_vertex_constants, {}, {}, true)};
  };
  const auto [rigid_game, rigid_ours_far] = rigid(60.f);
  const size_t rigid_bad = unlike(rigid_game, rigid_ours_far, 8);
  CHECK(rigid_game.triangles == PATCHES && rigid_ours_far.triangles == PATCHES && rigid_bad == 0,
        "rt_capture_displacement: %llu game triangles, %llu ours, %zu unlike", rigid_game.triangles, rigid_ours_far.triangles, rigid_bad);
  const auto [rigid_near_game, rigid_near_ours] = rigid(3.f);
  const size_t rigid_near_bad = unlike(rigid_near_game, rigid_near_ours, 4);
  CHECK(rigid_near_game.triangles > PATCHES && rigid_near_bad > 0, "CONTROL: near rigid patches: %llu game triangles, %zu unlike",
        rigid_near_game.triangles, rigid_near_bad);

  const std::array<ID3D11Buffer*, 1> terrain_hull_constants = {terrain_buffer.Get()};
  const std::array<ID3D11Buffer*, 3> terrain_domain_constants = {camera_buffer.Get(), terrain_buffer.Get(), tile_buffer.Get()};
  const std::array<ID3D11Buffer*, 1> terrain_vertex_constants = {camera_buffer.Get()};
  const std::span<const char> terrain_ours(static_cast<const char*>(terrain_shaders.second->GetBufferPointer()), terrain_shaders.second->GetBufferSize());
  const auto terrain_run = [&](float roughness) {
    const auto vertices = patches(60.f, roughness);
    return std::pair{stream(terrain_shaders.first, terrain_hull, terrain_domain, TERRAIN_PATCH_LAYOUT, vertices, {}, terrain_hull_constants,
                            terrain_domain_constants, false),
                     stream(terrain_ours, {}, {}, TERRAIN_PATCH_LAYOUT, vertices, terrain_vertex_constants, {}, {}, false)};
  };
  const auto [terrain_game, terrain_ours_smooth] = terrain_run(0.f);
  const size_t terrain_bad = unlike(terrain_game, terrain_ours_smooth, 4);
  CHECK(terrain_game.triangles == PATCHES && terrain_ours_smooth.triangles == PATCHES && terrain_bad == 0,
        "rt_capture_terrain_patches: %llu game triangles, %llu ours, %zu unlike", terrain_game.triangles, terrain_ours_smooth.triangles,
        terrain_bad);
  const auto [terrain_rough_game, terrain_rough_ours] = terrain_run(1.f);
  CHECK(terrain_rough_game.triangles > PATCHES, "CONTROL: rough terrain: %llu game triangles", terrain_rough_game.triangles);
  std::printf("rt_capture_displacement / terrain_patches: %u far patches each the game's corners bit for bit (rigid with its UV); near rigid "
              "%llu triangles (%zu of %u unlike), rough terrain %llu triangles\n",
              PATCHES, rigid_near_game.triangles, rigid_near_bad, PATCHES, terrain_rough_game.triangles);
}

// 19. sss_soft_mask (SSS Softness, Softer Away from the Caster and Debug View: Bend's shadows and the cascades put back
//     together) on a 64 x 64 mask with a 40 x 32 render part, its left half one surface (depth 0.9) and its right another
//     (0.99): at radius 0 each rendered texel the darker of the cascades (split r) and Bend's (the copy) bit for bit, the rest
//     untouched; at 3 px Bend's 1 on the left and 0 on the right stay 1 and 0 (no tap across the depth step), random Bend
//     shadows on one surface come out smoother. CONTROL: with one depth everywhere the left half's texels at the step darken.
//     Softer Away (64 x 2160 rendered: the most penumbra 32 px): no penumbras the min bit for bit; penumbras of 16 px smooth
//     random shadows, never across the step.
static void TestSoftMask(const std::string& games) {
  constexpr UINT SIZE = 64;
  constexpr UINT RENDER_WIDTH = 40;
  constexpr UINT RENDER_HEIGHT = 32;
  constexpr UINT TALL = 2160;
  const auto shader = Compile(games + "/twshogun2completeedition/sss_soft_mask.cs_5_0.hlsl");
  std::mt19937 random(19);
  // Runs the pass on a SIZE x `height` mask rendered `render` (R8, 0.5 where it never writes); the mask after it.
  const auto run = [&](UINT height, std::array<UINT, 2> render, float softness, float most, const std::vector<float>& depth,
                       const std::vector<uint8_t>& bend, const std::vector<uint32_t>& split) {
    const auto depth_texture = Texture(SIZE, height, DXGI_FORMAT_R32_FLOAT, depth.data(), SIZE * 4, D3D11_BIND_SHADER_RESOURCE);
    const auto bend_texture = Texture(SIZE, height, DXGI_FORMAT_R8_UNORM, bend.data(), SIZE, D3D11_BIND_SHADER_RESOURCE);
    const auto split_texture = Texture(SIZE, height, DXGI_FORMAT_R8G8B8A8_UNORM, split.data(), SIZE * 4, D3D11_BIND_SHADER_RESOURCE);
    const std::vector<uint8_t> untouched(static_cast<size_t>(SIZE) * height, 128);
    const auto mask = Texture(SIZE, height, DXGI_FORMAT_R8_UNORM, untouched.data(), SIZE, D3D11_BIND_UNORDERED_ACCESS);
    const std::array<float, 4> constants = {static_cast<float>(render[0]), static_cast<float>(render[1]), softness, most};
    const auto constant_buffer = Constants(constants.data(), sizeof(constants));
    const std::array<ComPtr<ID3D11ShaderResourceView>, 3> views = {Srv(depth_texture.Get()), Srv(bend_texture.Get()), Srv(split_texture.Get())};
    const std::array<ID3D11ShaderResourceView*, 3> sources = {views[0].Get(), views[1].Get(), views[2].Get()};
    const auto target = Uav(mask.Get());
    context->CSSetShader(shader.Get(), nullptr, 0);
    context->CSSetConstantBuffers(0, 1, constant_buffer.GetAddressOf());
    context->CSSetShaderResources(0, 3, sources.data());
    context->CSSetUnorderedAccessViews(0, 1, target.GetAddressOf(), nullptr);
    context->Dispatch((SIZE + 7) / 8, (height + 7) / 8, 1);
    context->ClearState();
    return Read(mask.Get(), 1);
  };
  // Inputs for a SIZE x `height` mask: two surfaces (the left half of the render width nearer), one surface, Bend's random,
  // Bend lit on the left and shadowed on the right, the cascades random (penumbra 0) or lit with a penumbra share.
  const auto inputs = [&](UINT height, UINT width) {
    struct Inputs {
      std::vector<float> two_surfaces, one_surface;
      std::vector<uint8_t> random_bend, step_bend;
      std::vector<uint32_t> random_split;
    } made;
    const size_t count = static_cast<size_t>(SIZE) * height;
    made.one_surface.assign(count, 0.9f);
    for (size_t texel = 0; texel < count; ++texel) {
      const bool left = texel % SIZE < width / 2;
      made.two_surfaces.push_back(left ? 0.9f : 0.99f);
      made.step_bend.push_back(left ? 255 : 0);
      made.random_bend.push_back(static_cast<uint8_t>(random() & 0xFF));
      made.random_split.push_back(0xFF000000u | static_cast<uint32_t>(random() & 0xFF));
    }
    return made;
  };
  const auto lit_split = [](UINT height, uint32_t penumbra) { return std::vector<uint32_t>(static_cast<size_t>(SIZE) * height, 0xFF0000FFu | (penumbra << 8)); };
  // Rendered texels unlike `want` (all when `want` empty: those not the min), and written outside the render part.
  const auto compare = [](const std::vector<uint8_t>& mask, std::array<UINT, 2> render, const std::vector<uint8_t>& bend,
                          const std::vector<uint32_t>& split) {
    std::array<size_t, 2> bad = {};
    for (size_t texel = 0; texel < mask.size(); ++texel) {
      if (texel % SIZE < render[0] && texel / SIZE < render[1]) {
        bad[0] += (mask[texel] == std::min<uint32_t>(bend[texel], split[texel] & 0xFF)) ? 0 : 1;
      } else {
        bad[1] += (mask[texel] == 128) ? 0 : 1;
      }
    }
    return bad;
  };
  const auto roughness = [](const std::vector<uint8_t>& mask, std::array<UINT, 2> render) {
    double sum = 0.0;
    for (UINT y = 0; y < render[1]; ++y) {
      for (UINT x = 1; x < render[0]; ++x) sum += std::abs(static_cast<int>(mask[(y * SIZE) + x]) - static_cast<int>(mask[(y * SIZE) + x - 1]));
    }
    return sum / (render[1] * (render[0] - 1));
  };
  // Texels whose value is not Bend's step (none may take shadows across the depth step); left texels darker than lit.
  const auto step_check = [](const std::vector<uint8_t>& mask, std::array<UINT, 2> render, const std::vector<uint8_t>& step) {
    std::array<size_t, 2> counts = {};
    for (UINT y = 0; y < render[1]; ++y) {
      for (UINT x = 0; x < render[0]; ++x) {
        const UINT texel = (y * SIZE) + x;
        counts[0] += (mask[texel] == step[texel]) ? 0 : 1;
        counts[1] += (x < render[0] / 2 && mask[texel] < 255) ? 1 : 0;
      }
    }
    return counts;
  };

  constexpr std::array<UINT, 2> RENDER = {RENDER_WIDTH, RENDER_HEIGHT};
  const auto square = inputs(SIZE, RENDER_WIDTH);
  const auto exact = compare(run(SIZE, RENDER, 0.f, 0.f, square.two_surfaces, square.random_bend, square.random_split), RENDER, square.random_bend,
                             square.random_split);
  CHECK(exact[0] == 0 && exact[1] == 0, "sss_soft_mask radius 0: %zu texels not the min, %zu outside the render part written", exact[0], exact[1]);
  const auto apart = step_check(run(SIZE, RENDER, 3.f, 0.f, square.two_surfaces, square.step_bend, lit_split(SIZE, 0)), RENDER, square.step_bend);
  const auto joined = step_check(run(SIZE, RENDER, 3.f, 0.f, square.one_surface, square.step_bend, lit_split(SIZE, 0)), RENDER, square.step_bend);
  CHECK(apart[0] == 0, "sss_soft_mask: %zu texels took Bend's shadows across the depth step", apart[0]);
  CHECK(joined[1] > 0, "CONTROL: one surface, no left texel darkened at the step");
  const double rough = roughness(run(SIZE, RENDER, 0.f, 0.f, square.one_surface, square.random_bend, lit_split(SIZE, 0)), RENDER);
  const double smooth = roughness(run(SIZE, RENDER, 3.f, 0.f, square.one_surface, square.random_bend, lit_split(SIZE, 0)), RENDER);
  CHECK(smooth < 0.5 * rough, "sss_soft_mask: 3 px not smoother (%.1f against %.1f)", smooth, rough);

  // Softer Away from the Caster: a share 128 of 255 of the most (32 px at 2160 lines): about 16 px.
  constexpr std::array<UINT, 2> TALL_RENDER = {SIZE, TALL};
  const auto tall = inputs(TALL, SIZE);
  const auto none = compare(run(TALL, TALL_RENDER, 0.f, 32.f, tall.two_surfaces, tall.random_bend, tall.random_split), TALL_RENDER,
                            tall.random_bend, tall.random_split);
  CHECK(none[0] == 0 && none[1] == 0, "sss_soft_mask no penumbras: %zu texels not the min, %zu written outside", none[0], none[1]);
  const double tall_rough = roughness(run(TALL, TALL_RENDER, 0.f, 32.f, tall.one_surface, tall.random_bend, lit_split(TALL, 0)), TALL_RENDER);
  const double tall_smooth = roughness(run(TALL, TALL_RENDER, 0.f, 32.f, tall.one_surface, tall.random_bend, lit_split(TALL, 128)), TALL_RENDER);
  const double tall_off = roughness(run(TALL, TALL_RENDER, 0.f, 0.f, tall.one_surface, tall.random_bend, lit_split(TALL, 128)), TALL_RENDER);
  const auto tall_apart = step_check(run(TALL, TALL_RENDER, 0.f, 32.f, tall.two_surfaces, tall.step_bend, lit_split(TALL, 128)), TALL_RENDER,
                                     tall.step_bend);
  CHECK(tall_smooth < 0.5 * tall_rough && tall_apart[0] == 0, "sss_soft_mask penumbras: %.1f against %.1f, %zu across the step",
        tall_smooth, tall_rough, tall_apart[0]);
  CHECK(tall_off == tall_rough, "CONTROL: penumbras read with Softer Away off (%.1f against %.1f)", tall_off, tall_rough);
  std::printf("sss_soft_mask: radius 0 the min bit for bit (%u x %u rendered, the rest untouched); 3 px none across the depth step "
              "(%zu darken on one surface), neighbour steps %.1f -> %.1f; 16 px penumbras %.1f -> %.1f, none across the step\n",
              RENDER_WIDTH, RENDER_HEIGHT, joined[1], rough, smooth, tall_rough, tall_smooth);
}

// 20. Light Through Banners' replay (screen_space_shadows.hpp OnThinDraw) after the game's cloth G-buffer draw: its vertex
//     shader 0xBDFF60D4 (the game's cso, then ours: shaders/clothvelocity00, written from the source) with its G-buffer pixel
//     shader 0x0FBCC65A (cso; its alpha test discards the diffuse map's hole) over a cleared depth (R24G8, the game's f44),
//     then the replay as the module draws it (sss_thin into an R32F target, less-equal, not written, depth bias -4), then a
//     nearer cloth over the banner's left: by Bend's match (bend_sss_gpu.hlsli ThinCasters) every banner pixel left in view
//     is a banner's, none the nearer one covers, none in the hole; both vertex shaders the same depth and banners' depth bit
//     for bit, SV_Position their output 0 (sss_thin's only input). CONTROL (must fail): the replay without the bias and with
//     less, no banner pixel drawn (the replay's depth is the game's draw's, bit for bit).
static void TestBannerReplay(const std::string& games, const std::string& folder) {
  constexpr UINT SIZE = 64;
  constexpr float CLEAR_DEPTH = 0.999f;
  const auto read = [&folder](const char* name) {
    std::ifstream file(folder + "/" + name, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  };
  const auto game_vertex = read("0xBDFF60D4.vs_5_0.cso");
  const auto game_pixel = read("0x0FBCC65A.ps_5_0.cso");
  if (game_vertex.empty() || game_pixel.empty()) {
    std::printf("missing 0xBDFF60D4 or 0x0FBCC65A\n");
    std::exit(2);
  }
  const std::wstring games_path(games.begin(), games.end());
  ComPtr<ID3DBlob> ours_vertex;
  ComPtr<ID3DBlob> thin_code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompileFromFile((games_path + L"/twshogun2completeedition/shaders/clothvelocity00_0xBDFF60D4.vs_5_0.hlsl").c_str(), nullptr,
                                D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, ours_vertex.GetAddressOf(),
                                errors.ReleaseAndGetAddressOf()))
      || FAILED(D3DCompileFromFile((games_path + L"/twshogun2completeedition/sss_thin.ps_5_0.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                   "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, thin_code.GetAddressOf(), errors.ReleaseAndGetAddressOf()))) {
    std::printf("compile failed: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    std::exit(2);
  }
  ComPtr<ID3D11PixelShader> game_pixel_shader;
  ComPtr<ID3D11PixelShader> thin_pixel_shader;
  device->CreatePixelShader(game_pixel.data(), game_pixel.size(), nullptr, game_pixel_shader.GetAddressOf());
  device->CreatePixelShader(thin_code->GetBufferPointer(), thin_code->GetBufferSize(), nullptr, thin_pixel_shader.GetAddressOf());

  // camera_VS_PS: the camera at the origin looking down +z, 60 degrees high (view_projection c10-c13 as columns), identity view
  // (c2-c5), time 1 against 0 before (not paused). cloth_VS: g_world identity. cloth_PS: its two colours.
  std::array<float, 44 * 4> camera = {};
  const float focal = 1.f / std::tan(30.f * 3.14159265f / 180.f);
  const float near_plane = 0.1f;
  const float far_plane = 1000.f;
  camera[10 * 4] = focal;
  camera[(11 * 4) + 1] = focal;
  camera[(12 * 4) + 2] = far_plane / (far_plane - near_plane);
  camera[(12 * 4) + 3] = -near_plane * far_plane / (far_plane - near_plane);
  camera[(13 * 4) + 2] = 1.f;
  for (int row = 0; row < 4; ++row) camera[((2 + row) * 4) + row] = 1.f;
  camera[35 * 4] = 1.f;
  for (const UINT at : {37u, 38u, 40u}) {
    camera[at * 4] = static_cast<float>(SIZE);
    camera[(at * 4) + 1] = static_cast<float>(SIZE);
    camera[(at * 4) + 2] = 1.f / static_cast<float>(SIZE);
    camera[(at * 4) + 3] = 1.f / static_cast<float>(SIZE);
  }
  const std::array<float, 16> world = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
  const std::array<float, 8> colours = {0.8f, 0.1f, 0.1f, 0.f, 0.1f, 0.1f, 0.8f, 0.f};
  const auto camera_buffer = Constants(camera.data(), sizeof(camera));
  const auto world_buffer = Constants(world.data(), sizeof(world));
  const auto colour_buffer = Constants(colours.data(), sizeof(colours));
  // The diffuse map: alpha 0 in its middle 2 x 2 texels (the pixel shader's alpha test discards there).
  std::vector<uint32_t> diffuse(16, 0xFFFFFFFFu);
  for (const int texel : {5, 6, 9, 10}) diffuse[texel] = 0x00FFFFFFu;
  const auto diffuse_texture = Texture(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, diffuse.data(), 16, D3D11_BIND_SHADER_RESOURCE);
  const auto diffuse_view = Srv(diffuse_texture.Get());
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf());
  // The G-buffer's three colour targets, the depth, the banners' depth.
  std::array<ComPtr<ID3D11Texture2D>, 3> gbuffer;
  std::array<ComPtr<ID3D11RenderTargetView>, 3> gbuffer_views;
  for (size_t index = 0; index < gbuffer.size(); ++index) {
    gbuffer[index] = Texture(SIZE, SIZE, DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr, 0, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    device->CreateRenderTargetView(gbuffer[index].Get(), nullptr, gbuffer_views[index].GetAddressOf());
  }
  const auto depth = Texture(SIZE, SIZE, DXGI_FORMAT_R24G8_TYPELESS, nullptr, 0, D3D11_BIND_DEPTH_STENCIL);
  const D3D11_DEPTH_STENCIL_VIEW_DESC depth_view_desc = {.Format = DXGI_FORMAT_D24_UNORM_S8_UINT, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D};
  ComPtr<ID3D11DepthStencilView> depth_view;
  device->CreateDepthStencilView(depth.Get(), &depth_view_desc, depth_view.GetAddressOf());
  const auto thin = Texture(SIZE, SIZE, DXGI_FORMAT_R32_FLOAT, nullptr, 0, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
  ComPtr<ID3D11RenderTargetView> thin_view;
  device->CreateRenderTargetView(thin.Get(), nullptr, thin_view.GetAddressOf());
  // The game's states (two-sided cloth, depth less and written) and the replay's (the module's).
  const D3D11_RASTERIZER_DESC game_rasterizer_desc = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE, .DepthClipEnable = TRUE};
  D3D11_RASTERIZER_DESC nearer_desc = game_rasterizer_desc;
  nearer_desc.DepthBias -= 4;
  ComPtr<ID3D11RasterizerState> game_rasterizer;
  ComPtr<ID3D11RasterizerState> nearer;
  device->CreateRasterizerState(&game_rasterizer_desc, game_rasterizer.GetAddressOf());
  device->CreateRasterizerState(&nearer_desc, nearer.GetAddressOf());
  const D3D11_DEPTH_STENCIL_DESC game_depth_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL, .DepthFunc = D3D11_COMPARISON_LESS};
  const D3D11_DEPTH_STENCIL_DESC replay_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO, .DepthFunc = D3D11_COMPARISON_LESS_EQUAL};
  const D3D11_DEPTH_STENCIL_DESC strict_desc = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO, .DepthFunc = D3D11_COMPARISON_LESS};
  ComPtr<ID3D11DepthStencilState> game_depth;
  ComPtr<ID3D11DepthStencilState> replay_depth;
  ComPtr<ID3D11DepthStencilState> strict_depth;
  device->CreateDepthStencilState(&game_depth_desc, game_depth.GetAddressOf());
  device->CreateDepthStencilState(&replay_desc, replay_depth.GetAddressOf());
  device->CreateDepthStencilState(&strict_desc, strict_depth.GetAddressOf());
  // The banner: 8 x 8 m, its right 2 m farther (z 9 to 11), the UV over it; the nearer cloth at z 7 over its left part, its UV
  // on an opaque texel.
  struct ClothVertex {
    std::array<float, 4> position, normal, uv, previous;
  };
  const auto quad = [](float left, float right, float bottom, float top, float near_z, float far_z, bool mapped) {
    std::vector<ClothVertex> vertices;
    for (const auto& corner : std::array<std::array<float, 2>, 6>{{{0.f, 0.f}, {0.f, 1.f}, {1.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}}}) {
      const std::array<float, 4> position = {left + ((right - left) * corner[0]), bottom + ((top - bottom) * corner[1]),
                                             near_z + ((far_z - near_z) * corner[0]), 1.f};
      const std::array<float, 4> uv = mapped ? std::array<float, 4>{corner[0], 1.f - corner[1], 0.f, 0.f} : std::array<float, 4>{0.1f, 0.1f, 0.f, 0.f};
      vertices.push_back({.position = position, .normal = {0.f, 0.f, -1.f, 0.f}, .uv = uv, .previous = position});
    }
    const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(ClothVertex)), .Usage = D3D11_USAGE_DEFAULT,
                                    .BindFlags = D3D11_BIND_VERTEX_BUFFER};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = vertices.data()};
    ComPtr<ID3D11Buffer> buffer;
    device->CreateBuffer(&desc, &data, buffer.GetAddressOf());
    return buffer;
  };
  const auto banner = quad(-4.f, 4.f, -4.f, 4.f, 9.f, 11.f, true);
  const auto cover = quad(-5.f, -1.5f, -5.f, 5.f, 7.f, 7.f, false);
  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(SIZE), .Height = static_cast<float>(SIZE), .MaxDepth = 1.f};

  struct Result {
    std::vector<uint32_t> after_banner, after_cover;  // the depth (24-bit) after the banner, after the nearer cloth
    std::vector<float> banners;                       // the banners' depth
    bool position_first = false;
  };
  const auto run = [&](const void* vertex_code, size_t vertex_size, bool strict) {
    Result result;
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11InputLayout> input;
    device->CreateVertexShader(vertex_code, vertex_size, nullptr, vertex_shader.GetAddressOf());
    device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)), vertex_code, vertex_size, input.GetAddressOf());
    ComPtr<ID3D11ShaderReflection> reflection;
    D3D11_SIGNATURE_PARAMETER_DESC output = {};
    result.position_first = SUCCEEDED(D3DReflect(vertex_code, vertex_size, IID_PPV_ARGS(reflection.GetAddressOf())))
                            && SUCCEEDED(reflection->GetOutputParameterDesc(0, &output)) && output.Register == 0
                            && _stricmp(output.SemanticName, "SV_Position") == 0;
    context->ClearDepthStencilView(depth_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, CLEAR_DEPTH, 0);
    constexpr std::array<FLOAT, 4> FAR_PLANE = {1.f, 1.f, 1.f, 1.f};
    context->ClearRenderTargetView(thin_view.Get(), FAR_PLANE.data());
    const UINT stride = sizeof(ClothVertex);
    const UINT offset = 0;
    const auto draw_cloth = [&](ID3D11Buffer* vertices) {
      std::array<ID3D11RenderTargetView*, 3> targets = {gbuffer_views[0].Get(), gbuffer_views[1].Get(), gbuffer_views[2].Get()};
      context->OMSetRenderTargets(static_cast<UINT>(targets.size()), targets.data(), depth_view.Get());
      context->OMSetDepthStencilState(game_depth.Get(), 0);
      context->RSSetState(game_rasterizer.Get());
      context->PSSetShader(game_pixel_shader.Get(), nullptr, 0);
      context->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
      context->Draw(6, 0);
    };
    context->IASetInputLayout(input.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex_shader.Get(), nullptr, 0);
    const std::array<ID3D11Buffer*, 2> vertex_constants = {camera_buffer.Get(), world_buffer.Get()};
    const std::array<ID3D11Buffer*, 2> pixel_constants = {camera_buffer.Get(), colour_buffer.Get()};
    context->VSSetConstantBuffers(0, 2, vertex_constants.data());
    context->PSSetConstantBuffers(0, 2, pixel_constants.data());
    context->PSSetShaderResources(0, 1, diffuse_view.GetAddressOf());
    context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    context->RSSetViewports(1, &viewport);
    // The depth buffer's 24-bit values.
    const auto read_depth = [&depth] {
      const auto bytes = Read(depth.Get(), 4);
      std::vector<uint32_t> values(bytes.size() / 4);
      std::memcpy(values.data(), bytes.data(), bytes.size());
      for (auto& value : values) value &= 0xFFFFFFu;
      return values;
    };
    draw_cloth(banner.Get());
    result.after_banner = read_depth();
    // The replay: the banners' depth, the game's depth tested, not written.
    context->OMSetRenderTargets(1, thin_view.GetAddressOf(), depth_view.Get());
    context->OMSetDepthStencilState(strict ? strict_depth.Get() : replay_depth.Get(), 0);
    context->RSSetState(strict ? game_rasterizer.Get() : nearer.Get());
    context->PSSetShader(thin_pixel_shader.Get(), nullptr, 0);
    context->Draw(6, 0);
    draw_cloth(cover.Get());
    result.after_cover = read_depth();
    const auto texels = Read(thin.Get(), 4);
    result.banners.resize(static_cast<size_t>(SIZE) * SIZE);
    std::memcpy(result.banners.data(), texels.data(), texels.size());
    context->ClearState();
    return result;
  };
  // Bend's ThinCasters match: the banners' depth is the scene's (within the replay's bias), not at the far plane.
  const auto matches = [](float banner_depth, float scene) {
    const float remaining = std::fabs(1.f - scene);
    return remaining > 1e-7f && std::fabs(banner_depth - scene) <= 4e-7f + (2e-4f * remaining);
  };
  constexpr uint32_t CLEARED = static_cast<uint32_t>(CLEAR_DEPTH * 16777215.f + 0.5f);
  struct Counts {
    size_t visible = 0, visible_matched = 0, covered = 0, covered_matched = 0, hole = 0, hole_matched = 0;
  };
  const auto count = [&](const Result& result) {
    Counts counts;
    for (size_t pixel = 0; pixel < result.banners.size(); ++pixel) {
      const uint32_t banner_depth = result.after_banner[pixel];
      const uint32_t final_depth = result.after_cover[pixel];
      const bool matched = matches(result.banners[pixel], static_cast<float>(final_depth) / 16777215.f);
      if (banner_depth != CLEARED && final_depth == banner_depth) {
        ++counts.visible;
        counts.visible_matched += matched ? 1 : 0;
      } else if (banner_depth != CLEARED) {
        ++counts.covered;
        counts.covered_matched += matched ? 1 : 0;
      } else if (result.banners[pixel] < 1.f) {
        ++counts.hole;
        counts.hole_matched += matched ? 1 : 0;
      }
    }
    return counts;
  };
  const Result game = run(game_vertex.data(), game_vertex.size(), false);
  const Result ours = run(ours_vertex->GetBufferPointer(), ours_vertex->GetBufferSize(), false);
  const Result strict = run(game_vertex.data(), game_vertex.size(), true);
  const Counts game_counts = count(game);
  const Counts strict_counts = count(strict);
  CHECK(game.position_first && ours.position_first, "SV_Position not the vertex shaders' output 0 (game %d, ours %d)", game.position_first,
        ours.position_first);
  CHECK(game_counts.visible > 400 && game_counts.visible_matched == game_counts.visible, "banner replay: %zu of %zu banner pixels matched",
        game_counts.visible_matched, game_counts.visible);
  CHECK(game_counts.covered > 50 && game_counts.covered_matched == 0, "banner replay: %zu of %zu covered pixels matched", game_counts.covered_matched,
        game_counts.covered);
  CHECK(game_counts.hole > 20 && game_counts.hole_matched == 0, "banner replay: %zu of %zu hole pixels matched", game_counts.hole_matched,
        game_counts.hole);
  CHECK(ours.after_cover == game.after_cover && ours.banners == game.banners, "banner replay: clothvelocity00 not the game's depth bit for bit");
  CHECK(strict_counts.visible_matched == 0, "CONTROL: the strict replay matched %zu banner pixels", strict_counts.visible_matched);
  std::printf("banner replay (0xBDFF60D4 + 0x0FBCC65A): %zu / %zu banner pixels matched, %zu / %zu covered, %zu / %zu in the hole; "
              "clothvelocity00 the same bit for bit; CONTROL less without the bias: %zu matched\n",
              game_counts.visible_matched, game_counts.visible, game_counts.covered_matched, game_counts.covered, game_counts.hole_matched,
              game_counts.hole, strict_counts.visible_matched);
}

// 21. shaders/ssrresolve00 (environment_specular_light cs50_resolve) against the game's 0xF87ECE85 on a 16 x 16 render with
//     random G-buffer, depth, environment, BRDF and AO (flags 1, 65, 129, 193: lit, with the AO-free 64 and 128 bits):
//     (a) nothing of ours bound: the game's output; (b) the rays' AO at t41 (0.37) over the random t_ssao: the game's with
//     t_ssao 0.37; (c) Ray-Traced Specular Occlusion at t46 (0.6) with no confidence (hit 0): the environment the sun light
//     draws added (the game's output with full confidence and no reflection: G1 = -environment) taken off, 0.6 x the open
//     one (the same with t_ssao 1: G1') put back: G1 - 0.6 G1'. CONTROL: the version before (0.6 x the AO'd environment:
//     G1 x 0.4, the game's AO under the rays' occlusion) differs. (d) With t46 bound the reflection's hit (full confidence,
//     the random t9 less the black one's output) is the same over the random t_ssao and t_ssao 1: no AO on it. CONTROL:
//     without t46 (the game's) the random AO changes it. (e) With the rays' reflections at t47 (and the sea's mask at t45
//     both ways: 0 and 1) the output is the same over the random t9 and a black one: the preconvolve copy (cs50_preconvolve_copy
//     0xBC3CB71D, t9's only writer; its other readers the mip chain left out under the rays) is unread then. CONTROL:
//     without t47 the random t9 changes the output.
static void TestResolve(const std::string& games, const std::string& folder) {
  std::ifstream file(folder + "/0xF87ECE85.cs_5_0.cso", std::ios::binary);
  const std::vector<char> game_code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const std::wstring path(games.begin(), games.end());
  FolderInclude include(path + L"/twshogun2completeedition/shaders");
  ComPtr<ID3DBlob> ours_code;
  ComPtr<ID3DBlob> errors;
  if (game_code.empty()
      || FAILED(D3DCompileFromFile((path + L"/twshogun2completeedition/shaders/ssrresolve00_0xF87ECE85.cs_5_0.hlsl").c_str(), nullptr, &include,
                                   "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, ours_code.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("missing 0xF87ECE85, or %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
    std::exit(2);
  }
  ComPtr<ID3D11ComputeShader> game;
  ComPtr<ID3D11ComputeShader> ours;
  device->CreateComputeShader(game_code.data(), game_code.size(), nullptr, game.GetAddressOf());
  device->CreateComputeShader(ours_code->GetBufferPointer(), ours_code->GetBufferSize(), nullptr, ours.GetAddressOf());
  constexpr UINT SIZE = 16;
  std::mt19937 random(21);
  std::uniform_real_distribution<float> unit(0.05f, 0.95f);
  const auto values = [&](size_t count) {
    std::vector<float> filled(count);
    for (auto& value : filled) value = unit(random);
    return filled;
  };
  const auto plain = [](UINT width, const std::vector<float>& texels) {
    return Srv(Texture(width, width, DXGI_FORMAT_R32G32B32A32_FLOAT, texels.data(), width * 16, D3D11_BIND_SHADER_RESOURCE).Get());
  };
  const auto flat = [&](float x, float y) {
    std::vector<float> texels(SIZE * SIZE * 4);
    for (size_t texel = 0; texel < SIZE * SIZE; ++texel) {
      texels[texel * 4] = x;
      texels[(texel * 4) + 1] = y;
    }
    return plain(SIZE, texels);
  };
  // The camera: every matrix the identity, near 0.1 and far 1000, the render (screen, viewport, target) 16 x 16.
  std::array<float, 44 * 4> camera = {};
  for (int matrix = 0; matrix < 8; ++matrix) {
    for (int row = 0; row < 4; ++row) camera[((2 + (matrix * 4) + row) * 4) + row] = 1.f;
  }
  camera[34 * 4] = 0.1f;
  camera[(34 * 4) + 1] = 1000.f;
  for (const int at : {37, 38, 40}) {
    camera[(at * 4) + 0] = camera[(at * 4) + 1] = SIZE;
    camera[(at * 4) + 2] = camera[(at * 4) + 3] = 1.f / SIZE;
  }
  std::array<float, 7 * 4> environment = {0.3f, -0.8f, 0.5f, 0.f, 1.f, 10.f, 0.8f, 0.5f, 1.f, 1.f, 1.f, 1.f, -20.f, -20.f, 20.f, 20.f};
  environment[16] = std::bit_cast<float>(2u);  // g_probe_grid_size
  environment[17] = 1.f;                       // g_specular_probes_count
  environment[18] = 0.5f;                      // g_specular_probe_lerp (the previous cubes too)
  const std::array<ComPtr<ID3D11Buffer>, 2> buffers = {Constants(camera.data(), sizeof(camera)), Constants(environment.data(), sizeof(environment))};
  // t0 / t1: four cubes each; t2 the BRDF table; t4-t7 the G-buffer (t6.w the flags byte), depth; t9 the lit scene.
  std::array<ComPtr<ID3D11ShaderResourceView>, 10> views;
  for (const UINT slot : {0u, 1u}) {
    const D3D11_TEXTURE2D_DESC desc = {.Width = 4, .Height = 4, .MipLevels = 1, .ArraySize = 24, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                       .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                       .MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE};
    std::vector<std::vector<float>> face_texels(24);
    std::vector<D3D11_SUBRESOURCE_DATA> initial(24);
    for (UINT face = 0; face < 24; ++face) {
      face_texels[face] = values(64);
      initial[face] = {.pSysMem = face_texels[face].data(), .SysMemPitch = 64};
    }
    ComPtr<ID3D11Texture2D> cubes;
    device->CreateTexture2D(&desc, initial.data(), cubes.GetAddressOf());
    const D3D11_SHADER_RESOURCE_VIEW_DESC view = {.Format = desc.Format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY,
                                                  .TextureCubeArray = {.MipLevels = 1, .NumCubes = 4}};
    device->CreateShaderResourceView(cubes.Get(), &view, views[slot].GetAddressOf());
  }
  for (const UINT slot : {2u, 3u, 4u, 5u, 7u, 9u}) views[slot] = plain(SIZE, values(SIZE * SIZE * 4));
  auto constants_texels = values(SIZE * SIZE * 4);
  constexpr std::array<float, 4> FLAGS = {1.f, 65.f, 129.f, 193.f};
  for (size_t texel = 0; texel < SIZE * SIZE; ++texel) constants_texels[(texel * 4) + 3] = FLAGS[texel % FLAGS.size()] / 255.f;
  views[6] = plain(SIZE, constants_texels);
  const auto full_confidence = flat(8.f, 32768.f);
  const auto no_confidence = flat(0.f, 32768.f);
  const auto black = plain(SIZE, std::vector<float>(SIZE * SIZE * 4, 0.f));
  const auto traced_ambient = plain(SIZE, std::vector<float>(SIZE * SIZE * 4, 0.37f));
  const auto open = plain(SIZE, std::vector<float>(SIZE * SIZE * 4, 1.f));
  const auto occlusion = plain(SIZE, std::vector<float>(SIZE * SIZE * 4, 0.6f));
  const D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                           .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> sampler;
  device->CreateSamplerState(&sampler_desc, sampler.GetAddressOf());
  const std::array<ID3D11SamplerState*, 4> samplers = {sampler.Get(), sampler.Get(), sampler.Get(), sampler.Get()};
  struct Inputs {
    ID3D11ShaderResourceView* ssao = nullptr;  // else the random t3
    ID3D11ShaderResourceView* hit = nullptr;
    ID3D11ShaderResourceView* radiance = nullptr;  // else the random t9
    ID3D11ShaderResourceView* ambient = nullptr;   // t41
    ID3D11ShaderResourceView* specular_occlusion = nullptr;  // t46
    ID3D11ShaderResourceView* reflection = nullptr;          // t47 (the rays' reflections)
    ID3D11ShaderResourceView* water = nullptr;               // t45 (the sea's mask)
  };
  const auto run = [&](ID3D11ComputeShader* shader, const Inputs& inputs) {
    const auto output = Texture(SIZE, SIZE, DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 0, D3D11_BIND_UNORDERED_ACCESS);
    const auto output_view = Uav(output.Get());
    std::array<ID3D11ShaderResourceView*, 10> bound = {};
    for (size_t slot = 0; slot < views.size(); ++slot) bound[slot] = views[slot].Get();
    if (inputs.ssao != nullptr) bound[3] = inputs.ssao;
    bound[8] = inputs.hit;
    if (inputs.radiance != nullptr) bound[9] = inputs.radiance;
    const std::array<ID3D11Buffer*, 2> constant_buffers = {buffers[0].Get(), buffers[1].Get()};
    context->CSSetShader(shader, nullptr, 0);
    context->CSSetConstantBuffers(0, 2, constant_buffers.data());
    context->CSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
    context->CSSetShaderResources(0, static_cast<UINT>(bound.size()), bound.data());
    context->CSSetShaderResources(41, 1, &inputs.ambient);
    context->CSSetShaderResources(46, 1, &inputs.specular_occlusion);
    context->CSSetShaderResources(47, 1, &inputs.reflection);
    context->CSSetShaderResources(45, 1, &inputs.water);
    context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
    context->Dispatch(1, 1, 1);
    context->ClearState();
    const auto bytes = Read(output.Get(), 16);
    std::vector<float> result(SIZE * SIZE * 3);
    for (size_t texel = 0; texel < SIZE * SIZE; ++texel) std::memcpy(&result[texel * 3], bytes.data() + (texel * 16), 12);
    return result;
  };
  const auto same = [](float x, float y) { return std::fabs(x - y) <= (2e-5f * std::max(std::fabs(x), std::fabs(y))) + 1e-6f; };
  const auto apart = [&](const std::vector<float>& a, const std::vector<float>& b) {
    size_t count = 0;
    for (size_t value = 0; value < a.size(); ++value) count += same(a[value], b[value]) ? 0 : 1;
    return count;
  };
  // (a)
  const Inputs lit = {.hit = full_confidence.Get()};
  const auto game_lit = run(game.Get(), lit);
  const size_t unbound_apart = apart(run(ours.Get(), lit), game_lit);
  const size_t lit_values = static_cast<size_t>(std::ranges::count_if(game_lit, [](float value) { return value != 0.f; }));
  CHECK(unbound_apart == 0 && lit_values > 0, "unbound: %zu of %zu values off the game's (%zu not 0)", unbound_apart, game_lit.size(), lit_values);
  // (b)
  const size_t ambient_apart = apart(run(ours.Get(), {.hit = full_confidence.Get(), .ambient = traced_ambient.Get()}),
                                     run(game.Get(), {.ssao = traced_ambient.Get(), .hit = full_confidence.Get()}));
  const size_t ambient_shown = apart(run(game.Get(), {.ssao = traced_ambient.Get(), .hit = full_confidence.Get()}), game_lit);
  CHECK(ambient_apart == 0 && ambient_shown > 0, "t41 0.37: %zu values off the game's with t_ssao 0.37 (%zu apart from the random AO's)",
        ambient_apart, ambient_shown);
  // (c)
  const auto environment_ao = run(game.Get(), {.hit = full_confidence.Get(), .radiance = black.Get()});
  const auto environment_open = run(game.Get(), {.ssao = open.Get(), .hit = full_confidence.Get(), .radiance = black.Get()});
  const auto occluded = run(ours.Get(), {.hit = no_confidence.Get(), .specular_occlusion = occlusion.Get()});
  std::vector<float> expected(occluded.size());
  std::vector<float> stacked(occluded.size());
  for (size_t value = 0; value < occluded.size(); ++value) {
    expected[value] = environment_ao[value] - (0.6f * environment_open[value]);
    stacked[value] = environment_ao[value] * 0.4f;
  }
  const size_t occlusion_apart = apart(occluded, expected);
  const size_t stacked_apart = apart(occluded, stacked);
  CHECK(occlusion_apart == 0, "t46 0.6: %zu of %zu values off G1 - 0.6 G1'", occlusion_apart, occluded.size());
  CHECK(stacked_apart > 0, "CONTROL: t46 0.6 gave 0.6 x the AO'd environment");
  // (d) The hit's share: the output with the random t9 less the one with t9 black, at each AO.
  const auto hit_apart = [&](ID3D11ShaderResourceView* specular_occlusion) {
    const Inputs random_ao = {.hit = full_confidence.Get(), .specular_occlusion = specular_occlusion};
    const Inputs open_ao = {.ssao = open.Get(), .hit = full_confidence.Get(), .specular_occlusion = specular_occlusion};
    Inputs random_black = random_ao;
    Inputs open_black = open_ao;
    random_black.radiance = open_black.radiance = black.Get();
    const auto a = run(ours.Get(), random_ao);
    const auto a_black = run(ours.Get(), random_black);
    const auto b = run(ours.Get(), open_ao);
    const auto b_black = run(ours.Get(), open_black);
    size_t count = 0, shown = 0;
    for (size_t value = 0; value < a.size(); ++value) {
      const float tolerance = (1e-5f * (std::fabs(a[value]) + std::fabs(a_black[value]) + std::fabs(b[value]) + std::fabs(b_black[value]))) + 1e-6f;
      count += (std::fabs((a[value] - a_black[value]) - (b[value] - b_black[value])) <= tolerance) ? 0 : 1;
      shown += (std::fabs(b[value] - b_black[value]) > tolerance) ? 1 : 0;
    }
    return std::pair{count, shown};
  };
  const auto [hit_occluded_apart, hit_shown] = hit_apart(occlusion.Get());
  const auto [hit_game_apart, hit_game_shown] = hit_apart(nullptr);
  CHECK(hit_occluded_apart == 0 && hit_shown > 0, "t46 0.6: the hit over the random AO %zu values off its share with AO 1 (%zu shown)",
        hit_occluded_apart, hit_shown);
  CHECK(hit_game_apart > 0, "CONTROL: without t46 the random AO left the hit as with AO 1");
  // (e)
  const auto traced_reflection = plain(SIZE, values(SIZE * SIZE * 4));
  const auto sea = plain(SIZE, std::vector<float>(SIZE * SIZE * 4, 1.f));
  size_t radiance_apart = 0;
  for (ID3D11ShaderResourceView* water : {black.Get(), sea.Get()}) {
    const Inputs rays = {.hit = full_confidence.Get(), .reflection = traced_reflection.Get(), .water = water};
    Inputs rays_black = rays;
    rays_black.radiance = black.Get();
    radiance_apart += apart(run(ours.Get(), rays), run(ours.Get(), rays_black));
  }
  const size_t radiance_shown = apart(run(ours.Get(), lit), run(ours.Get(), {.hit = full_confidence.Get(), .radiance = black.Get()}));
  CHECK(radiance_apart == 0 && radiance_shown > 0, "t47 bound: the random t9 %zu values off a black t9 (without t47 %zu)", radiance_apart,
        radiance_shown);
  std::printf("ssrresolve00: unbound the game's (%zu of %zu values lit, 0 apart); t41 0.37 = t_ssao 0.37 (%zu apart; %zu apart from the random "
              "AO); t46 0.6 the open environment x 0.6 (%zu apart; the AO'd one's %zu apart); t46: the hit without the AO (%zu of %zu shown "
              "apart; without t46 %zu apart); t47: t9 unread (%zu apart over the sea's mask 0 and 1; without t47 %zu apart)\n",
              lit_values, game_lit.size(), ambient_apart, ambient_shown, occlusion_apart, stacked_apart, hit_occluded_apart, hit_shown,
              hit_game_apart, radiance_apart, radiance_shown);
}

// 22. rt_ambient_into_game (Ray-Traced AO for every reader of the game's SSAO: ray_tracing.hpp CopyAmbient after the frame's
//     first sun light draw, the line sweep and collect left out and their target cleared open) on a 13 x 11 render in the
//     game's 16 x 16 R8 target: each render pixel the traced visibility (t41's value, within one 8-bit code), every other
//     texel the cleared 1. CONTROL: the traced values one pixel to the side are off.
static void TestAmbientIntoGame(const std::string& games) {
  const auto shader = Compile(games + "/twshogun2completeedition/rt_ambient_into_game.cs_5_0.hlsl");
  constexpr UINT RENDER_WIDTH = 13;
  constexpr UINT RENDER_HEIGHT = 11;
  constexpr UINT TARGET = 16;
  std::mt19937 random(22);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  std::vector<float> traced(RENDER_WIDTH * RENDER_HEIGHT);
  for (auto& value : traced) value = unit(random);
  const auto ambient = Texture(RENDER_WIDTH, RENDER_HEIGHT, DXGI_FORMAT_R32_FLOAT, traced.data(), RENDER_WIDTH * 4, D3D11_BIND_SHADER_RESOURCE);
  const std::vector<uint8_t> cleared(TARGET * TARGET, 255);  // ClearUnorderedAccessViewFloat's 1 (OnGameAmbientCollect)
  const auto target = Texture(TARGET, TARGET, DXGI_FORMAT_R8_UNORM, cleared.data(), TARGET,
                              D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
  const auto ambient_view = Srv(ambient.Get());
  const auto target_view = Uav(target.Get());
  context->CSSetShader(shader.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 1, ambient_view.GetAddressOf());
  context->CSSetUnorderedAccessViews(0, 1, target_view.GetAddressOf(), nullptr);
  context->Dispatch((RENDER_WIDTH + 7) / 8, (RENDER_HEIGHT + 7) / 8, 1);
  context->ClearState();
  const auto texels = Read(target.Get(), 1);
  size_t render_apart = 0;
  size_t outside_apart = 0;
  size_t shifted_apart = 0;
  for (UINT y = 0; y < TARGET; ++y) {
    for (UINT x = 0; x < TARGET; ++x) {
      const int code = texels[(y * TARGET) + x];
      if (x < RENDER_WIDTH && y < RENDER_HEIGHT) {
        render_apart += (std::abs(code - static_cast<int>(std::lround(traced[(y * RENDER_WIDTH) + x] * 255.f))) > 1) ? 1 : 0;
        if (x + 1 < RENDER_WIDTH) {
          shifted_apart += (std::abs(code - static_cast<int>(std::lround(traced[(y * RENDER_WIDTH) + x + 1] * 255.f))) > 1) ? 1 : 0;
        }
      } else {
        outside_apart += (code != 255) ? 1 : 0;
      }
    }
  }
  CHECK(render_apart == 0 && outside_apart == 0 && shifted_apart > 50, "ambient into the game's target: %zu render pixels apart, %zu outside not 1, CONTROL shifted %zu",
        render_apart, outside_apart, shifted_apart);
  std::printf("rt_ambient_into_game: %u render pixels the traced AO (%zu apart), %u others the cleared 1 (%zu apart); CONTROL one pixel to the "
              "side %zu of %u apart\n",
              RENDER_WIDTH * RENDER_HEIGHT, render_apart, TARGET * TARGET - (RENDER_WIDTH * RENDER_HEIGHT), outside_apart, shifted_apart,
              (RENDER_WIDTH - 1) * RENDER_HEIGHT);
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
  TestWaterPlane(games);
  if (argc > 3) {
    TestBillboard(games, argv[3]);
    TestImposter(games, argv[3]);
    TestWaters(games, argv[3]);
    TestPlacedShadow<RigidShadowVertex>(games, argv[3], "AC25DAB8", L"rt_capture_cascade_rigid", L"rt_rows_cascade_rigid",
                                        RIGID_SHADOW_LAYOUT, 6, 0);
    // Its TEXCOORD1 over TEXCOORD0's first half (unread).
    std::vector<D3D11_INPUT_ELEMENT_DESC> mtb_layout(std::begin(RIGID_SHADOW_LAYOUT), std::end(RIGID_SHADOW_LAYOUT));
    mtb_layout.push_back({"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0});
    TestPlacedShadow<RigidShadowVertex>(games, argv[3], "7B067623", L"rt_capture_cascade_rigid", L"rt_rows_cascade_rigid", mtb_layout, 6, 0);
    // Burning buildings: their POSITION5 over TEXCOORD0's first half (unread).
    std::vector<D3D11_INPUT_ELEMENT_DESC> burning_layout(std::begin(RIGID_SHADOW_LAYOUT), std::end(RIGID_SHADOW_LAYOUT));
    burning_layout.push_back({"POSITION", 5, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0});
    TestPlacedShadow<RigidShadowVertex>(games, argv[3], "124C85A5", L"rt_capture_cascade_rigid", L"rt_rows_cascade_rigid", burning_layout, 6, 0);
    TestPlacedShadow<VariantShadowVertex>(games, argv[3], "B0E006EE", L"rt_capture_cascade_variant", L"rt_rows_cascade_variant",
                                          VARIANT_SHADOW_LAYOUT, 8, 6);
    TestCascadeLeaves(games, argv[3]);
    TestLeafFade(games, argv[3]);
    TestAutoExposure(games, argv[3]);
    TestIndicators(games, argv[3]);
    TestCampaignOverlay(games, argv[3]);
    TestPatches(games, argv[3]);
    TestBannerReplay(games, argv[3]);
    TestResolve(games, argv[3]);
  }
  TestSunTint(games);
  TestParticleQuads(games);
  TestSoftMask(games);
  TestAmbientIntoGame(games);
  std::printf(failures == 0 ? "PASS\n" : "FAILED: %d\n", failures);
  return failures == 0 ? 0 : 1;
}
