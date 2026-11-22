/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 *
 * PsychoV35's defaults against the game's own SDR picture (handle-sdr-tonemap-lut: the defaults match the vanilla look;
 * the recipe every game copies: tools\tw\testbin\scratch\g04_\v35_pilot.md) on WARP, through the add-on's built LUT
 * builder lut00 (0x61F067B4, CombineLUTs without SDR LUTs; embed/0x61F067B4.h) as the game draws it: its 32^3 LUT (one
 * slice a layer), then sampled as the tone mapper samples it (tonemap.hlsli: LinToLog, the half-texel scale, x 1.05;
 * SDR clipped at 1), both as a 2.2 display shows them, Paper White = UI = 203 nits, Peak 1000. Level B: the CombineLUTs
 * constants are Unreal 4.23's defaults (no trace has them): the film tonemapper (slope 0.88, toe 0.55, shoulder 0.26,
 * black clip 0, white clip 0.04), blue correction 0.6, expand gamut 1, white balance 6500 K, the grading neutral, the
 * in-game Brightness 40 (InverseGamma.y 1).
 * Scene: 64 columns (grey, 21 hues x saturation 0.4 / 0.8 / 0.95) x 64 rows, a quarter stop a row from 2^-8 of mid grey.
 * Report: Musa's own sliders against SDR (grey where SDR is 0.05 - 1, 1 - 15, 15 - 100 nits; OkLab chroma at equal
 * lightness where SDR is 1 - 30 and 30 - 150 nits). The V35 FIT: Contrast 0.8 - 2, Shadows 0.3 - 1.5, Saturation 0.7 -
 * 1.3, Highlight Contrast 0.6 - 1.2, coarse to fine (0.1, 0.05, 0.01), minimax of grey 0.05 - 1 / 1 - 15 nits and chroma
 * 1 - 30 nits; a candidate counts only if it keeps PsychoV35's highlights at 1000 / 203 (grey within 10, 15 or 20 % of
 * Musa's own from 100 nits to 95 % of Peak: three fits, the smallest tolerance whose best is within 5 % is the one).
 * Checks: shared.h EXR_V35_* within 5 % and within 0.5 % of that fit's best, the highlight guard at 1000 / 203 within its
 * tolerance, 2500 / 275 reported and within 20 %; controls: Musa's own sliders must fail the 5 %, HDR (UE Extended: the
 * film curve below mid grey) must be the game's grey within 0.5 %; the grey ramps rise, SDR reaches 150 nits.
 * Built and run by test/tw_expeditions_unit/run.ps1 (after the expeditionsrome target); -Tests v35_fit.
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <embed/0x61F067B4.h>

namespace expeditions {
#include "src/games/expeditionsrome/shared.h"
}  // namespace expeditions

namespace {

using Microsoft::WRL::ComPtr;
using Float3 = std::array<float, 3>;
using Float4 = std::array<float, 4>;
using Injection = expeditions::ShaderInjectData;

constexpr UINT LUT_SIZE = 32;
constexpr UINT WIDTH = 64;   // grey, then 21 hues x 3 saturations
constexpr UINT HEIGHT = 64;  // a quarter stop a row
constexpr float FIRST_STOP = -8.f;  // of mid grey
constexpr float NITS = 203.f;
constexpr float HUE_CLIP = 0.7f;  // addon.cpp HUE_CLIP_DEFAULT (70)

ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
int failures = 0;

#define CHECK(x, ...)                                  \
  do {                                                 \
    if (!(x)) {                                        \
      std::printf("FAIL line %d: %s: ", __LINE__, #x); \
      std::printf(__VA_ARGS__);                        \
      std::printf("\n");                               \
      ++failures;                                      \
    }                                                  \
  } while (false)

// A full-screen triangle per LUT slice (its instance), the slice through the geometry shader's render target array
// index: lut00's inputs TEXCOORD0 (the uv), SV_Position, SV_RenderTargetArrayIndex (the slice: blue).
constexpr char SLICE_SHADERS[] = R"(
struct VertexOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; uint slice : SLICE; };
VertexOutput vertex_main(uint id : SV_VertexID, uint instance : SV_InstanceID) {
  VertexOutput output;
  output.uv = float2((id << 1) & 2, id & 2);
  output.position = float4((output.uv * float2(2.f, -2.f)) + float2(-1.f, 1.f), 0.f, 1.f);
  output.slice = instance;
  return output;
}
struct GeometryOutput { float2 uv : TEXCOORD0; float4 position : SV_Position; uint slice : SV_RenderTargetArrayIndex; };
[maxvertexcount(3)]
void geometry_main(triangle VertexOutput input[3], inout TriangleStream<GeometryOutput> stream) {
  [unroll]
  for (uint corner = 0; corner < 3; ++corner) {
    GeometryOutput output;
    output.uv = input[corner].uv;
    output.position = input[corner].position;
    output.slice = input[corner].slice;
    stream.Append(output);
  }
})";

ComPtr<ID3DBlob> Compile(const char* entry, const char* target) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(SLICE_SHADERS, sizeof(SLICE_SHADERS) - 1, "slices", nullptr, nullptr, entry, target, 0, 0, code.GetAddressOf(),
                        errors.GetAddressOf()))) {
    std::printf("compile %s: %s\n", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    std::exit(2);
  }
  return code;
}

// CombineLUTs' constants (cb0, 67 registers), Unreal 4.23's defaults (the header's level B).
std::array<Float4, 67> GameConstants() {
  std::array<Float4, 67> cb0 = {};
  cb0[26] = {0.f, 1.f, 0.f, 0.f};          // MappingPolynomial: x^2, x, 1
  cb0[27] = {1.f / 2.2f, 1.f, 1.f, 0.f};   // InverseGamma: .y 2.2 / display gamma (Brightness 40: 2.2)
  cb0[35] = {0.f, 0.f, 0.f, 1.f};          // .w: the film tonemapper
  cb0[36] = {0.88f, 0.55f, 0.26f, 0.f};    // FilmSlope, FilmToe, FilmShoulder, FilmBlackClip
  cb0[37] = {0.04f, 0.f, 0.f, 0.f};        // FilmWhiteClip
  cb0[42] = {0.f, 1.f, 1.f, 1.f};          // .yzw ColorScale
  cb0[43] = {0.f, 0.f, 0.f, 0.f};          // OverlayColor
  cb0[44] = {6500.f, 0.f, 0.f, 0.f};       // WhiteTemp, WhiteTint
  // ColorSaturation, Contrast, Gamma, Gain, Offset: global (45 - 49), shadows, midtones, highlights.
  for (UINT row = 45; row < 65; ++row) {
    const float value = ((row - 45) % 5 == 4) ? 0.f : 1.f;
    cb0[row] = {value, value, value, value};
  }
  cb0[65] = {0.09f, 0.5f, 0.f, 0.f};       // ColorCorrectionShadowsMax, HighlightsMin, OutputDevice 0 (sRGB), OutputGamut
  cb0[66] = {0.6f, 1.f, 0.f, 0.f};         // BlueCorrection, ExpandGamut
  return cb0;
}

ComPtr<ID3D11Buffer> Constants(const void* data, UINT bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = ((bytes + 15) / 16) * 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  std::vector<uint8_t> registers(desc.ByteWidth);
  std::memcpy(registers.data(), data, bytes);
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = registers.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &initial, buffer.GetAddressOf());
  return buffer;
}

// The LUT lut00 draws with the add-on's `si` (RGBA, [blue][green][red]).
std::vector<Float4> DrawLut(const Injection& si) {
  static ComPtr<ID3D11VertexShader> vertex;
  static ComPtr<ID3D11GeometryShader> geometry;
  static ComPtr<ID3D11PixelShader> lut00;
  static ComPtr<ID3D11Buffer> game_constants;
  static ComPtr<ID3D11Texture3D> target;
  static ComPtr<ID3D11RenderTargetView> target_view;
  static ComPtr<ID3D11Texture3D> staging;
  if (vertex == nullptr) {
    const auto vertex_code = Compile("vertex_main", "vs_5_0");
    const auto geometry_code = Compile("geometry_main", "gs_5_0");
    device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, vertex.GetAddressOf());
    device->CreateGeometryShader(geometry_code->GetBufferPointer(), geometry_code->GetBufferSize(), nullptr, geometry.GetAddressOf());
    if (FAILED(device->CreatePixelShader(__0x61F067B4.data(), __0x61F067B4.size(), nullptr, lut00.GetAddressOf()))) {
      std::printf("lut00 (0x61F067B4): CreatePixelShader failed\n");
      std::exit(2);
    }
    const auto cb0 = GameConstants();
    game_constants = Constants(cb0.data(), static_cast<UINT>(sizeof(cb0)));
    D3D11_TEXTURE3D_DESC desc = {.Width = LUT_SIZE, .Height = LUT_SIZE, .Depth = LUT_SIZE, .MipLevels = 1,
                                 .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
    device->CreateTexture3D(&desc, nullptr, target.GetAddressOf());
    const D3D11_RENDER_TARGET_VIEW_DESC view_desc = {.Format = desc.Format, .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D,
                                                     .Texture3D = {.MipSlice = 0, .FirstWSlice = 0, .WSize = LUT_SIZE}};
    device->CreateRenderTargetView(target.Get(), &view_desc, target_view.GetAddressOf());
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    device->CreateTexture3D(&desc, nullptr, staging.GetAddressOf());
  }
  const auto injection = Constants(&si, static_cast<UINT>(sizeof(si)));
  const D3D11_VIEWPORT viewport = {.Width = LUT_SIZE, .Height = LUT_SIZE, .MaxDepth = 1.f};
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vertex.Get(), nullptr, 0);
  context->GSSetShader(geometry.Get(), nullptr, 0);
  context->PSSetShader(lut00.Get(), nullptr, 0);
  context->PSSetConstantBuffers(0, 1, game_constants.GetAddressOf());
  context->PSSetConstantBuffers(13, 1, injection.GetAddressOf());
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  context->DrawInstanced(3, LUT_SIZE, 0, 0);
  context->ClearState();
  context->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<Float4> lut(static_cast<size_t>(LUT_SIZE) * LUT_SIZE * LUT_SIZE);
  for (UINT blue = 0; blue < LUT_SIZE; ++blue) {
    for (UINT green = 0; green < LUT_SIZE; ++green) {
      const auto* row = reinterpret_cast<const Float4*>(static_cast<const uint8_t*>(mapped.pData) + (blue * mapped.DepthPitch) + (green * mapped.RowPitch));
      std::copy(row, row + LUT_SIZE, lut.begin() + ((blue * LUT_SIZE + green) * LUT_SIZE));
    }
  }
  context->Unmap(staging.Get(), 0);
  return lut;
}

float Luminance(const Float3& c) { return 0.2126390059f * c[0] + 0.7151686788f * c[1] + 0.0721923154f * c[2]; }

Float3 Hue(float degrees, float saturation) {
  Float3 rgb;
  for (int i = 0; i < 3; ++i) {
    const float k = std::fmod(degrees / 60.f + (i == 0 ? 5.f : (i == 1 ? 3.f : 1.f)), 6.f);
    rgb[i] = 1.f - std::max(0.f, std::min({k, 4.f - k, 1.f}));
  }
  for (float& channel : rgb) channel = 1.f - saturation * (1.f - channel);
  return rgb;
}

// Each column's colour at one luminance a row: mid grey x 2^(FIRST_STOP + row / 4), linear BT.709 (the scene colour).
std::vector<Float3> Scene() {
  std::vector<Float3> columns = {{1.f, 1.f, 1.f}};
  for (int h = 0; h < 21; ++h) {
    for (const float s : {0.4f, 0.8f, 0.95f}) columns.push_back(Hue(h * 360.f / 21.f, s));
  }
  std::vector<Float3> scene(WIDTH * HEIGHT);
  for (UINT y = 0; y < HEIGHT; ++y) {
    const float brightness = 0.18f * std::exp2(FIRST_STOP + 0.25f * y);
    for (UINT x = 0; x < WIDTH; ++x) {
      const float scale = brightness / Luminance(columns[x]);
      scene[y * WIDTH + x] = {columns[x][0] * scale, columns[x][1] * scale, columns[x][2] * scale};
    }
  }
  return scene;
}

// The scene through the LUT as tonemap.hlsli samples it (LinToLog, the half-texel scale and offset, trilinear) and
// outputs it (x 1.05, the SDR LUT clipped at 1), decoded as the 2.2 display shows it (Paper White 1).
std::vector<Float3> Picture(const std::vector<Float4>& lut, bool hdr) {
  static const std::vector<Float3> scene = Scene();
  std::vector<Float3> picture(scene.size());
  for (size_t pixel = 0; pixel < scene.size(); ++pixel) {
    std::array<float, 3> texel = {};
    for (int i = 0; i < 3; ++i) {
      const float log_coordinate = std::clamp((std::log2(scene[pixel][i] + 0.00266771927f) * 0.0714285746f) + 0.610726953f, 0.f, 1.f);
      texel[i] = log_coordinate * static_cast<float>(LUT_SIZE - 1);
    }
    std::array<UINT, 3> low = {};
    std::array<float, 3> weight = {};
    for (int i = 0; i < 3; ++i) {
      low[i] = std::min(static_cast<UINT>(texel[i]), LUT_SIZE - 2);
      weight[i] = texel[i] - static_cast<float>(low[i]);
    }
    Float3 sampled = {};
    for (UINT corner = 0; corner < 8; ++corner) {
      float corner_weight = 1.f;
      size_t index = 0;
      for (int i = 2; i >= 0; --i) {
        const UINT bit = (corner >> i) & 1u;
        corner_weight *= (bit != 0u) ? weight[i] : 1.f - weight[i];
        index = (index * LUT_SIZE) + low[i] + bit;
      }
      for (int c = 0; c < 3; ++c) sampled[c] += corner_weight * lut[index][c];
    }
    for (int c = 0; c < 3; ++c) {
      const float encoded = hdr ? (1.05f * sampled[c]) : std::clamp(1.05f * sampled[c], 0.f, 1.f);
      picture[pixel][c] = std::copysign(std::pow(std::abs(encoded), 2.2f), encoded);
    }
  }
  return picture;
}

// OkLab (Ottosson) from linear BT.709: {L, a, b}.
Float3 OkLab(const Float3& c) {
  const float l = 0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2];
  const float m = 0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2];
  const float s = 0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2];
  const float l3 = std::cbrt(l), m3 = std::cbrt(m), s3 = std::cbrt(s);
  return {0.2104542553f * l3 + 0.7936177850f * m3 - 0.0040720468f * s3, 1.9779984951f * l3 - 2.4285922050f * m3 + 0.4505937099f * s3,
          0.0259040371f * l3 + 0.7827717662f * m3 - 0.8086757660f * s3};
}

// The add-on's shader_injection at `hdr` (0 SDR, 1 UE Extended, 3 PsychoV35): Reset's values (addon.cpp
// ApplyResetPreset, psycho35::Defaults at Hue Clip 70), HDR10 output at `peak` / `paper_white` (UI = Paper White).
Injection Settings(float hdr, float peak = 1000.f, float paper_white = NITS) {
  Injection si = {};
  si.peak_white_nits = peak;
  si.diffuse_white_nits = paper_white;
  si.graphics_white_nits = paper_white;
  si.hdr = hdr;
  si.bloom = si.vignette = 1.f;
  si.exposure = si.highlights = si.shadows = si.contrast = si.saturation = si.highlight_saturation = si.roll_off = 1.f;
  si.godrays = si.volumetric_fog = 1.f;
  si.ext_exposure = si.ext_highlights = si.ext_shadows = si.ext_contrast = si.ext_saturation = si.ext_highlight_saturation = 1.f;
  si.film_grain = si.chromatic_aberration = 1.f;
  si.hue_clip = HUE_CLIP;
  si.v35_exposure = si.v35_highlights = si.v35_shadows = si.v35_contrast = 1.f;
  si.v35_highlight_contrast = si.v35_shadow_contrast = 1.f;
  si.v35_saturation = si.v35_highlight_saturation = si.v35_roll_off = si.v35_gamut_compression = 1.f;
  return si;
}

std::vector<Float3> Run(const Injection& si) {
  return Picture(DrawLut(si), si.hdr != 0.f);
}

struct Range {
  float low = 1e9f, high = 0.f;
};

// PsychoV35 over SDR: grey where SDR is 0.05 - 1, 1 - 15 and 15 - 100 nits; colour (OkLab chroma at equal lightness,
// the geometric mean over the 63 colours and each colour's own) where SDR is 1 - 30 and 30 - 150 nits.
struct Comparison {
  std::array<Range, 3> grey;
  std::array<float, 2> chroma = {};
  std::array<Range, 2> per_colour;
};

Comparison Compare(const std::vector<Float3>& sdr, const std::vector<Float3>& v35) {
  Comparison comparison;
  auto& grey = comparison.grey;
  for (UINT y = 0; y < HEIGHT; ++y) {
    const float sdr_nits = Luminance(sdr[y * WIDTH]) * NITS;
    const float ratio = Luminance(v35[y * WIDTH]) / std::max(Luminance(sdr[y * WIDTH]), 1e-9f);
    const int band = (sdr_nits >= 0.05f && sdr_nits <= 1.f) ? 0 : ((sdr_nits > 1.f && sdr_nits <= 15.f) ? 1 : ((sdr_nits > 15.f && sdr_nits <= 100.f) ? 2 : -1));
    if (band < 0) continue;
    grey[band].low = std::min(grey[band].low, ratio);
    grey[band].high = std::max(grey[band].high, ratio);
  }
  // Chroma at equal lightness: PsychoV35's chroma interpolated where its lightness is SDR's.
  std::array<double, 2> log_sums = {};
  std::array<int, 2> counts = {};
  auto& per_colour = comparison.per_colour;
  for (UINT column = 1; column < WIDTH; ++column) {
    std::array<double, 2> column_sums = {};
    std::array<int, 2> column_counts = {};
    for (UINT y = 0; y < HEIGHT; ++y) {
      const Float3& game_colour = sdr[y * WIDTH + column];
      const float sdr_nits = Luminance(game_colour) * NITS;
      const int band = (sdr_nits >= 1.f && sdr_nits <= 30.f) ? 0 : ((sdr_nits > 30.f && sdr_nits <= 150.f) ? 1 : -1);
      if (band < 0) continue;
      const Float3 game_lab = OkLab(game_colour);
      const float game_chroma = std::hypot(game_lab[1], game_lab[2]);
      if (game_chroma < 0.01f) continue;
      for (UINT j = 0; j + 1 < HEIGHT; ++j) {
        const Float3 below = OkLab(v35[j * WIDTH + column]);
        const Float3 above = OkLab(v35[(j + 1) * WIDTH + column]);
        if (game_lab[0] < below[0] || game_lab[0] > above[0]) continue;
        const float t = (above[0] > below[0]) ? (game_lab[0] - below[0]) / (above[0] - below[0]) : 0.f;
        const float chroma = std::lerp(std::hypot(below[1], below[2]), std::hypot(above[1], above[2]), t);
        column_sums[band] += std::log(chroma / game_chroma);
        ++column_counts[band];
        break;
      }
    }
    for (int band = 0; band < 2; ++band) {
      if (column_counts[band] == 0) continue;
      const float column_ratio = static_cast<float>(std::exp(column_sums[band] / column_counts[band]));
      per_colour[band].low = std::min(per_colour[band].low, column_ratio);
      per_colour[band].high = std::max(per_colour[band].high, column_ratio);
      log_sums[band] += column_sums[band];
      counts[band] += column_counts[band];
    }
  }
  for (int band = 0; band < 2; ++band) {
    comparison.chroma[band] = static_cast<float>(std::exp(log_sums[band] / std::max(counts[band], 1)));
  }
  return comparison;
}

void Report(const char* name, const std::vector<Float3>& sdr, const std::vector<Float3>& v35) {
  bool sdr_rises = true, v35_rises = true;
  for (UINT y = 1; y < HEIGHT; ++y) {
    sdr_rises = sdr_rises && Luminance(sdr[y * WIDTH]) >= Luminance(sdr[(y - 1) * WIDTH]) - 1e-7f;
    v35_rises = v35_rises && Luminance(v35[y * WIDTH]) >= Luminance(v35[(y - 1) * WIDTH]) - 1e-7f;
  }
  const float sdr_top = Luminance(sdr[(HEIGHT - 1) * WIDTH]) * NITS;
  CHECK(sdr_rises && v35_rises, "%s: the grey ramp falls somewhere (SDR %d, PsychoV35 %d)", name, sdr_rises, v35_rises);
  CHECK(sdr_top > 150.f, "%s: SDR's top %g nits (the ramp misses the bands)", name, sdr_top);
  const Comparison c = Compare(sdr, v35);
  std::printf("%s: PsychoV35 over Rendering Mode SDR below white, 1000 / 203 nits\n"
              "   grey where SDR is 0.05 - 1 nits x%.3f..x%.3f, 1 - 15 nits x%.3f..x%.3f, 15 - 100 nits x%.3f..x%.3f\n"
              "   colour at equal lightness, SDR 1 - 30 nits x%.3f (per colour x%.2f..x%.2f), 30 - 150 nits x%.3f (x%.2f..x%.2f)\n",
              name, c.grey[0].low, c.grey[0].high, c.grey[1].low, c.grey[1].high, c.grey[2].low, c.grey[2].high, c.chroma[0],
              c.per_colour[0].low, c.per_colour[0].high, c.chroma[1], c.per_colour[1].low, c.per_colour[1].high);
}

}  // namespace

int wmain() {
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::printf("no WARP device\n");
    return 2;
  }
  const auto sdr = Run(Settings(0.f));
  const Injection own = Settings(3.f);
  Report("Expeditions: Rome, Musa's own sliders", sdr, Run(own));

  struct Slider {
    const char* name;
    float low, high;
  };
  constexpr std::array<Slider, 4> SLIDERS = {
      {{"Contrast", 0.8f, 2.f}, {"Shadows", 0.3f, 1.5f}, {"Saturation", 0.7f, 1.3f}, {"Highlight Contrast", 0.6f, 1.2f}}};
  using Values = std::array<float, 4>;
  const auto with = [](Injection si, const Values& values) {
    si.v35_contrast = values[0];
    si.v35_shadows = values[1];
    si.v35_saturation = values[2];
    si.v35_highlight_contrast = values[3];
    return si;
  };
  const auto grey_error = [](const Comparison& c) {
    return std::max({std::abs(c.grey[0].low - 1.f), std::abs(c.grey[0].high - 1.f), std::abs(c.grey[1].low - 1.f),
                     std::abs(c.grey[1].high - 1.f)});
  };
  const auto below_white = [&](const Comparison& c) { return std::max(grey_error(c), std::abs(c.chroma[0] - 1.f)); };
  const auto highlights = [](const std::vector<Float3>& fitted, const std::vector<Float3>& reference, float paper_white, float peak) {
    float worst = 0.f;
    for (UINT y = 0; y < HEIGHT; ++y) {
      const float reference_nits = Luminance(reference[y * WIDTH]) * paper_white;
      if (reference_nits < 100.f || reference_nits > 0.95f * peak) continue;
      worst = std::max(worst, std::abs((Luminance(fitted[y * WIDTH]) / Luminance(reference[y * WIDTH])) - 1.f));
    }
    return worst;
  };

  // V35 FIT (the pilot's search: every 0.1 over the ranges, then 0.05 and 0.01 steps around the best; the guard only for
  // a candidate that beats the best), once per highlight tolerance.
  const auto own_ramp = Run(own);
  std::printf("Expeditions: Rome V35 fit (level B: Unreal 4.23's default film settings), %s %.1f - %.1f, %s %.1f - %.1f, "
              "%s %.1f - %.1f, %s %.1f - %.1f\n",
              SLIDERS[0].name, SLIDERS[0].low, SLIDERS[0].high, SLIDERS[1].name, SLIDERS[1].low, SLIDERS[1].high,
              SLIDERS[2].name, SLIDERS[2].low, SLIDERS[2].high, SLIDERS[3].name, SLIDERS[3].low, SLIDERS[3].high);
  Values best = {1.f, 1.f, 1.f, 1.f};
  float best_score = 1e9f;
  float tolerance = 0.f;
  for (const float guard : {0.1f, 0.15f, 0.2f}) {
    Values fit = {1.f, 1.f, 1.f, 1.f};
    float fit_score = 1e9f;
    int candidates = 0;
    bool first_pass = true;
    const auto start = std::chrono::steady_clock::now();
    for (const auto& [step, reach] : {std::pair{0.1f, 6}, std::pair{0.05f, 2}, std::pair{0.01f, 3}}) {
      const Values centre = (first_pass ? Values{1.4f, 0.9f, 1.f, 0.9f} : fit);
      first_pass = false;
      std::array<std::vector<float>, 4> grid;
      for (size_t d = 0; d < SLIDERS.size(); ++d) {
        for (int k = -reach; k <= reach; ++k) {
          const float value = std::round((centre[d] + (static_cast<float>(k) * step)) * 100.f) / 100.f;
          if (value >= SLIDERS[d].low - 1e-4f && value <= SLIDERS[d].high + 1e-4f) {
            grid[d].push_back(value);
          }
        }
      }
      for (const float contrast : grid[0]) {
        for (const float shadows : grid[1]) {
          for (const float saturation : grid[2]) {
            for (const float highlight_contrast : grid[3]) {
              const Values values = {contrast, shadows, saturation, highlight_contrast};
              const auto ramp = Run(with(own, values));
              ++candidates;
              const float score = below_white(Compare(sdr, ramp));
              if (score < fit_score && highlights(ramp, own_ramp, NITS, 1000.f) <= guard) {
                fit = values;
                fit_score = score;
              }
            }
          }
        }
      }
    }
    std::printf("   guard %.0f %%: %.2f / %.2f / %.2f / %.2f, worst %.1f %%, highlights %.1f %% (%d candidates, %.0f s)\n",
                100.f * guard, fit[0], fit[1], fit[2], fit[3], 100.f * fit_score,
                100.f * highlights(Run(with(own, fit)), own_ramp, NITS, 1000.f), candidates,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    if (tolerance == 0.f && fit_score < 0.05f) {
      best = fit;
      best_score = fit_score;
      tolerance = guard;
    }
  }
  CHECK(tolerance != 0.f, "no fit within 5 %% at any highlight tolerance: keep Musa's own and report it");
  std::printf("   the fit: guard %.0f %% (the smallest within 5 %%)\n", 100.f * tolerance);

  const Injection shipped = with(own, {EXR_V35_CONTRAST, EXR_V35_SHADOWS, EXR_V35_SATURATION, EXR_V35_HIGHLIGHT_CONTRAST});
  const float own_score = below_white(Compare(sdr, own_ramp));
  const auto shipped_ramp = Run(shipped);
  const float shipped_score = below_white(Compare(sdr, shipped_ramp));
  std::printf("   shared.h EXR_V35_* (%.2f / %.2f / %.2f / %.2f): worst %.1f %%; Musa's own (control, must fail 5 %%): %.1f %%\n",
              EXR_V35_CONTRAST, EXR_V35_SHADOWS, EXR_V35_SATURATION, EXR_V35_HIGHLIGHT_CONTRAST, 100.f * shipped_score,
              100.f * own_score);
  CHECK(shipped_score < 0.05f, "the V35 defaults miss the game by %.1f %%", 100.f * shipped_score);
  CHECK(shipped_score <= best_score + 0.005f, "the V35 defaults (%.1f %%) are not the fit's best (%.1f %%)", 100.f * shipped_score,
        100.f * best_score);
  CHECK(own_score > 0.05f, "control: Musa's own sliders pass (%.1f %%): the measure sees nothing", 100.f * own_score);
  Report("Expeditions: Rome V35 defaults (after the fit)", sdr, shipped_ramp);
  for (const auto& [peak, paper_white, bound] : {std::tuple{1000.f, 203.f, tolerance}, std::tuple{2500.f, 275.f, 0.2f}}) {
    const float worst = highlights(Run(with(Settings(3.f, peak, paper_white), {EXR_V35_CONTRAST, EXR_V35_SHADOWS, EXR_V35_SATURATION,
                                                                                EXR_V35_HIGHLIGHT_CONTRAST})),
                                   Run(Settings(3.f, peak, paper_white)), paper_white, peak);
    std::printf("   highlights, Peak %.0f / Paper White %.0f: grey within %.1f %% of Musa's own (100 nits - 95 %% of Peak)\n", peak,
                paper_white, 100.f * worst);
    CHECK(worst <= bound + 1e-4f, "the V35 defaults move PsychoV35's highlights by %.1f %% at %.0f / %.0f (bound %.0f %%)", 100.f * worst,
          peak, paper_white, 100.f * bound);
  }
  // The measure's own check: HDR (UE Extended) is the game's film curve below mid grey (every grey band here), so a
  // wrong decode, white or binding would show.
  const Comparison extended = Compare(sdr, Run(Settings(1.f)));
  const float extended_grey = grey_error(extended);
  std::printf("   control, HDR (UE Extended): grey %.2f %% off, chroma x%.3f (must be the game's grey)\n", 100.f * extended_grey,
              extended.chroma[0]);
  CHECK(extended_grey < 0.005f, "HDR (UE Extended) is %.2f %% off the game's grey below mid grey", 100.f * extended_grey);
  std::printf(failures == 0 ? "PASS v35_fit\n" : "FAIL v35_fit: %d\n", failures);
  return failures == 0 ? 0 : 1;
}
