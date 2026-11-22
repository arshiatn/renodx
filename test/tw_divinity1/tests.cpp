// Divinity: Original Sin EE's tone map (0x30A1D40D, PPFilmicToneMappingWithColorCorrection) on WARP: the game's own
// bytes (the shader as the game ships it, read by run.ps1 outside the repo) against the add-on's, compiled by run.ps1
// with the build's flags.
// Input: the scene-linear Base the stock loads (64 colours: grey, 48 hues, fire, sky, skin, foliage, black ... x 48
// exposure steps, 2^-8 to 2^10.8 of the colour, grey 0.18 at row 20). Params from the game's atmospheres (the six most
// common Hable A / B / D and the extremes of mid gray 0.036 and 0.230); ColorMatrix identity and a saturation 1.97 one
// (the engine's own construction is unknown: level C); Source / Target colours none, and Black Cove's.
// 1. SDR (Native): the add-on's mode 0 = saturate(the game's bytes) within 1e-6 (the UNORM target's clamp).
// 2. Extended (3) and PsychoV35 (4) run once on the scene: grey 0.18 lands on the game's own mid gray (Hable(0.18) /
//    Hable(W) for the live A / B / D) within 1%; the grey ramp never gets darker; finite; BT.2020 within Peak.
//    Control (must fail): today's mode 1 (UC2 Extended + UpgradeToneMap + PsychoV30 on its result).
//    Output SDR (2b): both modes within BT.709 0 - 1, grey still on the game's mid gray.
//    Peak under Paper White (2c, 400 / 500): every HDR mode as at Peak = Paper White.
// 3. Extended: equal to the game's Hable below its pivot (within 1e-3); fire (1, 0.4, 0.1) turns yellow / white above 1
//    (G / R rises toward 1 as SDR's clip does), the per-channel shoulder.
// 4. The grade alone (grade.hlsl, GradeAfterCurve) with Smooth Grade Shoulder (Experimental) on: the stock's grade below
//    the shoulder's start 0.8 (ColorMatrix identity, saturation 1.97 and 0.34, Black Cove's colours) within 1e-4, its
//    slope continuous through the shoulder; control (must fail): the hard knee at 1. The change against it by brightness
//    band (report: the row's look change). The row off (its default) equals the knee at 1 bit for bit.
// 5. Report: luminance falls along the ramps against the game's own, OkLab distance to the game's SDR picture below its
//    clip, and fire / sky hue at Hue Clip 0 - 1 for both modes (the defaults come from it).
// 6. Smooth Grade Shoulder off = the add-on's tone map before the shoulder (a6762afab^, built by run.ps1) bit for bit in
//    SDR (Native), Extended and PsychoV35 (every atmosphere, grade, fade, Hue Clip 0 / 0.7, Output HDR10 / SDR); control
//    (must fail): the row on differs.
// 7. V35 FIT: PsychoV35's Contrast, Shadows, Saturation and Highlight Contrast minimax-fitted to the game's SDR below
//    white with R1's traced save constants; shared.h DIVINITY_V35_* within 5 % and within 0.5 % of the fit's best,
//    PsychoV35's highlights within 10 % at 1000 / 203 and 2500 / 275; controls: Musa's own sliders must fail the 5 %, HDR
//    (Extended) must be the game's grey. The menu's constants: a report.
// tests.exe <game cso> <add-on cso> <grade cso> <grade control cso> <grade off cso> <add-on before the shoulder cso>.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <string>
#include <vector>

namespace divinity {
#include "src/games/divinityoriginalsinenhanced/shared.h"
}  // namespace divinity

using Microsoft::WRL::ComPtr;
using Float3 = std::array<float, 3>;
using Float4 = std::array<float, 4>;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

constexpr UINT WIDTH = 64;
constexpr UINT HEIGHT = 48;
constexpr UINT GREY = 0;
constexpr UINT FIRE = 49;
constexpr UINT SKY = 51;
constexpr UINT BLACK = 63;
constexpr UINT GREY_ROW = 20;  // 0.18

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static ComPtr<ID3D11VertexShader> full_screen;

// The stock's cbuffer _Globals (b0).
struct Globals {
  Float4 params;                     // A, B, D, fade
  std::array<Float4, 4> color_matrix;  // row_major
  Float4 source_color;
  Float4 target_color;
};

struct Atmosphere {
  float a, b, d;
};
// atmospheres.tsv (243 atmospheres): the six most common A / B / D, then mid gray's extremes.
constexpr std::array<Atmosphere, 8> ATMOSPHERES = {{
    {0.22f, 0.30f, 0.20f}, {0.13f, 0.07f, 0.21f}, {0.50f, 0.24f, 0.52f}, {0.30f, 0.21f, 0.71f},
    {0.48f, 0.14f, 0.62f}, {0.29f, 0.11f, 0.26f}, {0.19f, 0.17f, 0.71f}, {0.80f, 0.21f, 0.21f},
}};

static double HableRaw(double x, const Atmosphere& atmosphere) {
  const double a = atmosphere.a, b = atmosphere.b, c = 0.1, d = atmosphere.d, e = 0.01, f = 0.3;
  return (x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f) - e / f;
}
static double Hable(double x, const Atmosphere& atmosphere) { return HableRaw(x, atmosphere) / HableRaw(11.2, atmosphere); }

static std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static ComPtr<ID3D11PixelShader> LoadPixelShader(const std::filesystem::path& path) {
  const std::vector<char> code = ReadFile(path);
  REQUIRE(!code.empty());
  ComPtr<ID3D11PixelShader> shader;
  REQUIRE(SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, &shader)));
  return shader;
}

static Float3 Hue(float hue) {
  const auto channel = [hue](float n) {
    const float k = std::fmod(n + hue * 6.f, 6.f);
    return 1.f - std::max(0.f, std::min({k, 4.f - k, 1.f}));
  };
  return {channel(5.f), channel(3.f), channel(1.f)};
}

static std::vector<Float4> SceneColours() {
  std::array<Float3, WIDTH> colours = {};
  colours[GREY] = {1.f, 1.f, 1.f};
  for (UINT i = 1; i <= 48; ++i) colours[i] = Hue((i - 1) / 48.f);
  colours[FIRE] = {1.f, 0.4f, 0.1f};
  colours[50] = {1.f, 0.6f, 0.2f};
  colours[SKY] = {0.35f, 0.6f, 1.f};
  colours[52] = {0.8f, 0.55f, 0.4f};
  colours[53] = {0.2f, 0.5f, 0.1f};
  for (UINT i = 54; i < BLACK; ++i) colours[i] = {0.5f + 0.05f * (i - 54), 0.5f, 0.5f - 0.05f * (i - 54)};
  colours[BLACK] = {0.f, 0.f, 0.f};
  std::vector<Float4> texels;
  for (UINT row = 0; row < HEIGHT; ++row) {
    const float scale = 0.18f * std::exp2((static_cast<float>(row) - GREY_ROW) * 0.4f);
    for (UINT column = 0; column < WIDTH; ++column) {
      texels.push_back({colours[column][0] * scale, colours[column][1] * scale, colours[column][2] * scale, 1.f});
    }
  }
  return texels;
}

static float SceneGrey(UINT row) { return 0.18f * std::exp2((static_cast<float>(row) - GREY_ROW) * 0.4f); }

template <typename T>
static ComPtr<ID3D11Buffer> ConstantBuffer(const T& data) {
  const D3D11_BUFFER_DESC desc = {(sizeof(T) + 15) / 16 * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
  std::vector<char> padded(desc.ByteWidth);
  std::memcpy(padded.data(), &data, sizeof(T));
  const D3D11_SUBRESOURCE_DATA init = {padded.data(), 0, 0};
  ComPtr<ID3D11Buffer> buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &init, &buffer)));
  return buffer;
}

static ComPtr<ID3D11ShaderResourceView> scene;

// `input`: the Base the stock loads, WIDTH wide (the target as tall as it).
static std::vector<Float4> Draw(ID3D11PixelShader* shader, const Globals& globals, const divinity::ShaderInjectData& injection,
                                ID3D11ShaderResourceView* input = scene.Get()) {
  ComPtr<ID3D11Resource> input_resource;
  input->GetResource(&input_resource);
  D3D11_TEXTURE2D_DESC input_desc = {};
  static_cast<ID3D11Texture2D*>(input_resource.Get())->GetDesc(&input_desc);
  const UINT height = input_desc.Height;
  const D3D11_TEXTURE2D_DESC target_desc = {WIDTH, height, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                            D3D11_BIND_RENDER_TARGET, 0, 0};
  ComPtr<ID3D11Texture2D> target;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&target_desc, nullptr, &target)));
  ComPtr<ID3D11RenderTargetView> target_view;
  REQUIRE(SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, &target_view)));
  const auto globals_buffer = ConstantBuffer(globals);
  const auto injection_buffer = ConstantBuffer(injection);
  context->ClearState();
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(full_screen.Get(), nullptr, 0);
  context->PSSetShader(shader, nullptr, 0);
  context->PSSetConstantBuffers(0, 1, globals_buffer.GetAddressOf());
  context->PSSetConstantBuffers(10, 1, injection_buffer.GetAddressOf());
  context->PSSetShaderResources(0, 1, &input);
  const D3D11_VIEWPORT viewport = {0.f, 0.f, WIDTH, static_cast<float>(height), 0.f, 1.f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  context->Draw(3, 0);
  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, &staging)));
  context->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<Float4> texels(WIDTH * height);
  for (UINT row = 0; row < height; ++row) {
    std::memcpy(&texels[row * WIDTH], static_cast<const char*>(mapped.pData) + row * mapped.RowPitch, WIDTH * sizeof(Float4));
  }
  context->Unmap(staging.Get(), 0);
  return texels;
}

static Float4 At(const std::vector<Float4>& texels, UINT column, UINT row) { return texels[row * WIDTH + column]; }

// The add-on's output: gamma 2.2 values (Scene and UI white equal here: no extra scale), decoded.
static Float3 Linear(const Float4& encoded) {
  Float3 linear = {};
  for (int c = 0; c < 3; ++c) linear[c] = std::copysign(std::pow(std::abs(encoded[c]), 2.2f), encoded[c]);
  return linear;
}

static float Luminance(const Float3& color) { return 0.2126f * color[0] + 0.7152f * color[1] + 0.0722f * color[2]; }

static Float3 BT2020(const Float3& color) {
  return {0.627403896f * color[0] + 0.329283039f * color[1] + 0.0433130657f * color[2],
          0.0690972894f * color[0] + 0.919540395f * color[1] + 0.0113623156f * color[2],
          0.0163914389f * color[0] + 0.0880133077f * color[1] + 0.895595253f * color[2]};
}

static Float3 OkLab(const Float3& color) {
  const float l = std::cbrt(std::max(0.4122214708f * color[0] + 0.5363325363f * color[1] + 0.0514459929f * color[2], 0.f));
  const float m = std::cbrt(std::max(0.2119034982f * color[0] + 0.6806995451f * color[1] + 0.1073969566f * color[2], 0.f));
  const float s = std::cbrt(std::max(0.0883024619f * color[0] + 0.2817188376f * color[1] + 0.6299787005f * color[2], 0.f));
  return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s, 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
          0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

static float HueDegrees(const Float3& color) {
  const Float3 lab = OkLab(color);
  return std::atan2(lab[2], lab[1]) * 180.f / std::numbers::pi_v<float>;
}

static Globals MakeGlobals(const Atmosphere& atmosphere, float saturation, bool black_cove) {
  Globals globals = {.params = {atmosphere.a, atmosphere.b, atmosphere.d, 0.f}};
  // Saturation in gamma space about the engine's weights (R1's traced ColorMatrix: saturation 1.1 / 1.14 with these).
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      const float weight = std::array<float, 3>{0.3086f, 0.6094f, 0.0820f}[c];
      globals.color_matrix[r][c] = (1.f - saturation) * weight + (r == c ? saturation : 0.f);
    }
  }
  globals.color_matrix[3] = {0.299f, 0.587f, 0.114f, 0.f};  // alpha: a luma (FXAA's), the stock writes the matrix's last row
  if (black_cove) {
    globals.source_color = {0.654902f, 0.654902f, 0.654902f, 0.f};
    globals.target_color = {0.415686f, 0.725490f, 0.192157f, 0.f};
  }
  return globals;
}

// Output: 0 HDR10 (the proxy's HDR path), 2 SDR.
static divinity::ShaderInjectData Injection(float mode, float peak_nits, float hue_clip, float output = 0.f) {
  return {
      .peak_white_nits = peak_nits, .diffuse_white_nits = 203.f, .graphics_white_nits = 203.f, .swap_chain_encoding = output,
      .tonemap_type = mode, .vignette = 1.f, .bloom = 1.f, .godrays = 1.f, .ui = 1.f, .firehighlightmultiplier = 0.5f,
      .candlehighlightmultiplier = 1.f, .candlehighlightmultiplierpsycho = 1.f, .player_light_intensity = 1.f,
      .local_light_intensity = 1.f, .exposure = 1.f, .highlights = 1.f, .shadows = 1.f, .cone_response_exponent = 1.f,
      .contrast = 1.f, .purity_scale = 1.f, .brazier_boost_enabled = 0.f, .hue_clip = hue_clip, .v35_exposure = 1.f,
      .v35_highlights = 1.f, .v35_shadows = 1.f, .v35_contrast = 1.f, .v35_highlight_contrast = 1.f,
      .v35_shadow_contrast = 1.f, .v35_flare = 0.f, .v35_saturation = 1.f, .v35_dechroma = 0.f,
      .v35_highlight_saturation = 1.f, .v35_roll_off = 1.f, .v35_gamut_compression = 1.f, .ext_hue_clip = hue_clip,
      .sun_shadow_softness = 1.f, .grade_shoulder = 0.f,
  };
}

// The grade alone (grade.hlsl): one GradeAfterCurve per case.
struct GradeCase {
  Float4 color;
  std::array<Float4, 4> color_matrix;
  Float4 source_color;
  Float4 target_color;
};

static std::vector<Float4> RunGrade(const std::filesystem::path& path, const std::vector<GradeCase>& cases) {
  const std::vector<char> code = ReadFile(path);
  REQUIRE(!code.empty());
  ComPtr<ID3D11ComputeShader> shader;
  REQUIRE(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &shader)));
  const auto count = static_cast<UINT>(cases.size());
  const D3D11_BUFFER_DESC input_desc = {static_cast<UINT>(count * sizeof(GradeCase)), D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                                        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(GradeCase)};
  const D3D11_SUBRESOURCE_DATA input_init = {cases.data(), 0, 0};
  ComPtr<ID3D11Buffer> input;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&input_desc, &input_init, &input)));
  ComPtr<ID3D11ShaderResourceView> input_view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(input.Get(), nullptr, &input_view)));
  const D3D11_BUFFER_DESC output_desc = {static_cast<UINT>(count * sizeof(Float4)), D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0,
                                         D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(Float4)};
  ComPtr<ID3D11Buffer> output;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&output_desc, nullptr, &output)));
  ComPtr<ID3D11UnorderedAccessView> output_view;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(output.Get(), nullptr, &output_view)));
  context->ClearState();
  context->CSSetShader(shader.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 1, input_view.GetAddressOf());
  context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
  context->Dispatch((count + 63) / 64, 1, 1);
  const D3D11_BUFFER_DESC staging_desc = {static_cast<UINT>(count * sizeof(Float4)), D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
  ComPtr<ID3D11Buffer> staging;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&staging_desc, nullptr, &staging)));
  context->CopyResource(staging.Get(), output.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<Float4> results(count);
  std::memcpy(results.data(), mapped.pData, count * sizeof(Float4));
  context->Unmap(staging.Get(), 0);
  return results;
}

// The stock's grade on a colour within 0 - 1 (linear in, linear out), as its shader runs it after the curve, without its
// UNORM target's clip at 1 (a grade that brightens a channel past 1 keeps it in HDR).
static Float3 StockGrade(const Float3& color, const Globals& globals) {
  std::array<float, 4> encoded = {std::pow(color[0], 1.f / 2.2f), std::pow(color[1], 1.f / 2.2f), std::pow(color[2], 1.f / 2.2f), 1.f};
  Float3 graded = {};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 4; ++c) graded[r] += globals.color_matrix[r][c] * encoded[c];
  }
  float distance = 0.f;
  for (int c = 0; c < 3; ++c) distance += (graded[c] - globals.source_color[c]) * (graded[c] - globals.source_color[c]);
  const float weight = std::max(0.f, 1.f - std::sqrt(distance));
  Float3 linear = {};
  for (int c = 0; c < 3; ++c) {
    const float toward_target = std::clamp(globals.target_color[c] + graded[c], -1.f, 1.f) - graded[c];
    linear[c] = std::pow(std::max(toward_target * weight * weight + graded[c], 0.f), 2.2f);
  }
  return linear;
}

// Check 2 / 3 on one mode: true when every part holds (the control's result is asserted false by the caller).
struct OnSceneResult {
  bool mid_gray = true, monotonic = true, finite = true, within_peak = true, hable_below_pivot = true;
};

static OnSceneResult CheckOnScene(ID3D11PixelShader* shader, float mode, float peak_nits) {
  OnSceneResult result;
  for (const Atmosphere& atmosphere : ATMOSPHERES) {
    const auto texels = Draw(shader, MakeGlobals(atmosphere, 1.f, false), Injection(mode, peak_nits, 0.8f));
    const double mid_gray = Hable(0.18, atmosphere);
    const float grey = Luminance(Linear(At(texels, GREY, GREY_ROW)));
    if (!(std::abs(grey - mid_gray) <= 0.01 * mid_gray)) {
      if (result.mid_gray) std::printf("  mode %.0f A %.2f B %.2f D %.2f: grey 0.18 -> %.5f, Hable's mid gray %.5f\n", mode, atmosphere.a, atmosphere.b, atmosphere.d, grey, mid_gray);
      result.mid_gray = false;
    }
    // Below the pivot (UC2 Extended's: the scene where Hable's third derivative is 0; 0.27 - 1 here): up to 0.25.
    for (UINT row = 0; row < HEIGHT && SceneGrey(row) <= 0.25f; ++row) {
      const double expected = Hable(SceneGrey(row), atmosphere);
      const float got = Luminance(Linear(At(texels, GREY, row)));
      if (!(std::abs(got - expected) <= 1e-3 * std::max(expected, 1e-3))) result.hable_below_pivot = false;
    }
    float last = -1.f;
    for (UINT row = 0; row < HEIGHT; ++row) {
      const float y = Luminance(Linear(At(texels, GREY, row)));
      if (y < last - 1e-5f) result.monotonic = false;
      last = std::max(last, y);
    }
    const float peak = peak_nits / 203.f;
    for (const Float4& texel : texels) {
      if (!std::isfinite(texel[0]) || !std::isfinite(texel[1]) || !std::isfinite(texel[2]) || !std::isfinite(texel[3])) result.finite = false;
      const Float3 bt2020 = BT2020(Linear(texel));
      if (std::max({bt2020[0], bt2020[1], bt2020[2]}) > peak * 1.002f) result.within_peak = false;
    }
  }
  return result;
}

// Part 7 (the V35 fit; test/tw_shogun2_romer_toe's measure, the recipe every game copies): PsychoV35 over SDR, both
// linear (Paper White 1): grey where SDR is 0.05 - 1, 1 - 15 and 15 - 100 nits; colour (OkLab chroma at equal
// lightness, the geometric mean over the 63 colours and each colour's own) where SDR is 1 - 30 and 30 - 150 nits.
constexpr float NITS = 203.f;

struct Range {
  float low = 1e9f, high = 0.f;
};

struct Comparison {
  std::array<Range, 3> grey;
  std::array<float, 2> chroma = {};
  std::array<Range, 2> per_colour;
};

static Comparison Compare(const std::vector<Float3>& sdr, const std::vector<Float3>& v35) {
  const auto rows = static_cast<UINT>(sdr.size() / WIDTH);
  Comparison comparison;
  auto& grey = comparison.grey;
  for (UINT y = 0; y < rows; ++y) {
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
    for (UINT y = 0; y < rows; ++y) {
      const Float3& game_colour = sdr[y * WIDTH + column];
      const float sdr_nits = Luminance(game_colour) * NITS;
      const int band = (sdr_nits >= 1.f && sdr_nits <= 30.f) ? 0 : ((sdr_nits > 30.f && sdr_nits <= 150.f) ? 1 : -1);
      if (band < 0) continue;
      const Float3 game_lab = OkLab(game_colour);
      const float game_chroma = std::hypot(game_lab[1], game_lab[2]);
      if (game_chroma < 0.01f) continue;
      for (UINT j = 0; j + 1 < rows; ++j) {
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

static void Report(const char* name, const std::vector<Float3>& sdr, const std::vector<Float3>& v35) {
  const Comparison c = Compare(sdr, v35);
  std::printf("   %s: PsychoV35 over SDR (Native) below white, 1000 / 203 nits\n"
              "     grey where SDR is 0.05 - 1 nits x%.3f..x%.3f, 1 - 15 nits x%.3f..x%.3f, 15 - 100 nits x%.3f..x%.3f\n"
              "     colour at equal lightness, SDR 1 - 30 nits x%.3f (per colour x%.2f..x%.2f), 30 - 150 nits x%.3f (x%.2f..x%.2f)\n",
              name, c.grey[0].low, c.grey[0].high, c.grey[1].low, c.grey[1].high, c.grey[2].low, c.grey[2].high, c.chroma[0],
              c.per_colour[0].low, c.per_colour[0].high, c.chroma[1], c.per_colour[1].low, c.per_colour[1].high);
}

int main(int argc, char** argv) {
  REQUIRE(argc >= 7);
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
  // A full-screen triangle with the stock's interface (SV_Position, TexCoord0).
  constexpr char VERTEX[] =
      "void main(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TexCoord0) {"
      "  uv = float2((id << 1) & 2, id & 2); position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }";
  ComPtr<ID3DBlob> vertex_code;
  REQUIRE(SUCCEEDED(D3DCompile(VERTEX, sizeof(VERTEX) - 1, nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vertex_code, nullptr)));
  REQUIRE(SUCCEEDED(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, &full_screen)));
  const auto texels = SceneColours();
  const D3D11_TEXTURE2D_DESC scene_desc = {WIDTH, HEIGHT, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                           D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA scene_init = {texels.data(), WIDTH * sizeof(Float4), 0};
  ComPtr<ID3D11Texture2D> scene_texture;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&scene_desc, &scene_init, &scene_texture)));
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(scene_texture.Get(), nullptr, &scene)));
  const auto stock = LoadPixelShader(argv[1]);
  const auto addon = LoadPixelShader(argv[2]);

  // 1. SDR (Native): saturate(the game's bytes).
  {
    float worst = 0.f;
    for (const Atmosphere& atmosphere : ATMOSPHERES) {
      for (const float saturation : {1.f, 1.97f}) {
        for (const bool black_cove : {false, true}) {
          Globals globals = MakeGlobals(atmosphere, saturation, black_cove);
          for (const float fade : {0.f, 0.35f}) {
            globals.params[3] = fade;
            const auto game = Draw(stock.Get(), globals, Injection(0.f, 1000.f, 0.f));
            const auto ours = Draw(addon.Get(), globals, Injection(0.f, 1000.f, 0.f));
            for (size_t i = 0; i < game.size(); ++i) {
              for (int c = 0; c < 4; ++c) worst = std::max(worst, std::abs(std::clamp(game[i][c], 0.f, 1.f) - ours[i][c]));
            }
          }
        }
      }
    }
    std::printf("1. SDR (Native) against saturate(the game's bytes): largest difference %.3g\n", worst);
    CHECK(worst <= 1e-6f);
  }

  // 2 / 3. Each HDR curve once on the scene; today's mode 1 the control.
  for (const float peak_nits : {1000.f, 400.f}) {
    const OnSceneResult extended = CheckOnScene(addon.Get(), 3.f, peak_nits);
    const OnSceneResult psycho35 = CheckOnScene(addon.Get(), 4.f, peak_nits);
    const OnSceneResult control = CheckOnScene(addon.Get(), 1.f, peak_nits);
    std::printf("2. Peak %.0f: Extended mid gray %d monotonic %d finite %d within Peak %d Hable below pivot %d\n", peak_nits,
                extended.mid_gray, extended.monotonic, extended.finite, extended.within_peak, extended.hable_below_pivot);
    std::printf("   PsychoV35 mid gray %d monotonic %d finite %d within Peak %d\n", psycho35.mid_gray, psycho35.monotonic,
                psycho35.finite, psycho35.within_peak);
    std::printf("   control (mode 1, UC2 Extended + UpgradeToneMap + PsychoV30) mid gray %d Hable below pivot %d\n",
                control.mid_gray, control.hable_below_pivot);
    CHECK(extended.mid_gray && extended.monotonic && extended.finite && extended.within_peak && extended.hable_below_pivot);
    CHECK(psycho35.mid_gray && psycho35.monotonic && psycho35.finite && psycho35.within_peak);
    CHECK(!(control.mid_gray && control.hable_below_pivot));
  }

  // 2b. Output SDR: both modes fitted into BT.709 within 1 (Peak 1), grey still on the game's mid gray, finite.
  for (const float mode : {3.f, 4.f}) {
    bool inside = true;
    bool mid_gray = true;
    for (const Atmosphere& atmosphere : ATMOSPHERES) {
      // The game's grade itself can leave BT.709 (a saturation matrix past 1: the stock's UNORM target, here the proxy's
      // SDR encode, clips it): the curves alone are checked.
      const auto plain = Draw(addon.Get(), MakeGlobals(atmosphere, 1.f, false), Injection(mode, 1000.f, 0.7f, 2.f));
      for (const Float4& texel : plain) {
        const Float3 linear = Linear(texel);
        for (const float channel : linear) {
          if (!std::isfinite(channel) || channel < -2e-3f || channel > 1.002f) inside = false;
        }
      }
      const double expected = Hable(0.18, atmosphere);
      if (std::abs(Luminance(Linear(At(plain, GREY, GREY_ROW))) - expected) > 0.01 * expected) mid_gray = false;
    }
    std::printf("2b. Output SDR, mode %.0f: BT.709 within 0 - 1 %d, grey on mid gray %d\n", mode, inside, mid_gray);
    CHECK(inside && mid_gray);
  }

  // 2c. Peak under Paper White (the sliders allow 400 with 500): every HDR mode as at Peak = Paper White (Peak over Paper
  //     White never under 1; before, PsychoV30 and Pragmap got 0.8).
  for (const float mode : {1.f, 2.f, 3.f, 4.f}) {
    auto under = Injection(mode, 400.f, 0.7f);
    under.diffuse_white_nits = 500.f;
    auto equal = Injection(mode, 500.f, 0.7f);
    equal.diffuse_white_nits = 500.f;
    const Globals globals = MakeGlobals(ATMOSPHERES[0], 1.f, false);
    const auto ours_under = Draw(addon.Get(), globals, under);
    const auto ours_equal = Draw(addon.Get(), globals, equal);
    float worst = 0.f;
    for (size_t i = 0; i < ours_under.size(); ++i) {
      for (int c = 0; c < 4; ++c) worst = std::max(worst, std::abs(ours_under[i][c] - ours_equal[i][c]));
    }
    std::printf("2c. Mode %.0f, Peak 400 / Paper White 500 against 500 / 500: largest difference %.3g\n", mode, worst);
    CHECK(worst <= 1e-6f);
  }

  // 3. Extended's fire: G / R rises toward 1 above the clip.
  {
    const auto extended = Draw(addon.Get(), MakeGlobals(ATMOSPHERES[0], 1.f, false), Injection(3.f, 1000.f, 0.f));
    const auto game = Draw(stock.Get(), MakeGlobals(ATMOSPHERES[0], 1.f, false), Injection(0.f, 1000.f, 0.f));
    std::printf("3. fire (1, 0.4, 0.1), G / R by exposure (row: SDR, Extended):");
    float first_over = -1.f;
    float top = 0.f;
    for (UINT row = 24; row < HEIGHT; row += 4) {
      const Float3 sdr = Linear({std::clamp(game[row * WIDTH + FIRE][0], 0.f, 1.f), std::clamp(game[row * WIDTH + FIRE][1], 0.f, 1.f),
                                 std::clamp(game[row * WIDTH + FIRE][2], 0.f, 1.f), 1.f});
      const Float3 ours = Linear(At(extended, FIRE, row));
      std::printf(" %u: %.2f, %.2f;", row, sdr[1] / sdr[0], ours[1] / ours[0]);
      if (first_over < 0.f && ours[0] > 1.f) first_over = ours[1] / ours[0];
      top = ours[1] / ours[0];
    }
    std::printf("\n");
    CHECK(first_over > 0.f && top > first_over + 0.2f && top > 0.75f);
  }

  // 4. The grade alone: the stock's below the shoulder's start 0.8 (within 1e-4); the slope continuous through the
  //    shoulder (0.8 - 2: the slope's change across 2 x 5e-4 of brightness, relative to the colour's brightest channel,
  //    under 0.05), where the control (the replaced hard knee at 1) must break it; the look against the control by
  //    brightness band: the same below 0.8, the change reported above it.
  {
    std::vector<GradeCase> cases;
    std::vector<Globals> grades;
    for (const float saturation : {1.f, 1.97f, 0.34f}) {
      for (const bool black_cove : {false, true}) grades.push_back(MakeGlobals(ATMOSPHERES[0], saturation, black_cove));
    }
    std::vector<Float3> colours;
    for (UINT i = 0; i < 48; ++i) colours.push_back(Hue(i / 48.f));
    colours.push_back({1.f, 0.4f, 0.1f});
    colours.push_back({0.35f, 0.6f, 1.f});
    colours.push_back({1.f, 1.f, 1.f});
    colours.push_back({1.f, 0.9f, 0.85f});
    // Each colour at its brightest channel: 0.5 and just below the start (the stock's grade), five steps of 5e-4 around
    // each slope point, then 64 levels from 1/8 to 16 (the bands).
    constexpr float SHOULDER_START = 0.8f;
    constexpr float STEP = 5e-4f;
    constexpr std::array<float, 5> SLOPE_POINTS = {0.8f, 0.9f, 1.f, 1.25f, 2.f};
    constexpr size_t SLOPE_FIRST = 2;
    constexpr size_t RAMP_FIRST = SLOPE_FIRST + SLOPE_POINTS.size() * 5;
    std::vector<float> levels = {0.5f, SHOULDER_START - 1e-4f};
    for (const float point : SLOPE_POINTS) {
      for (int step = -2; step <= 2; ++step) levels.push_back(point * (1.f + step * STEP));
    }
    for (int i = 0; i < 64; ++i) levels.push_back(std::exp2(-3.f + 7.f * i / 63.f));
    for (const Globals& globals : grades) {
      for (const Float3& colour : colours) {
        const float colour_max = std::max({colour[0], colour[1], colour[2]});
        for (const float level : levels) {
          const float scale = level / colour_max;
          cases.push_back({.color = {colour[0] * scale, colour[1] * scale, colour[2] * scale, 0.f}, .color_matrix = globals.color_matrix,
                           .source_color = globals.source_color, .target_color = globals.target_color});
        }
      }
    }
    const auto results = RunGrade(argv[3], cases);
    const auto control = RunGrade(argv[4], cases);
    const auto off = RunGrade(argv[5], cases);
    // The row off: the knee at 1 itself, every case bit for bit.
    size_t off_unequal = 0;
    for (size_t i = 0; i < cases.size(); ++i) off_unequal += (std::memcmp(&off[i], &control[i], sizeof(Float4)) != 0) ? 1 : 0;
    std::printf("4. grade, Smooth Grade Shoulder off against the knee at 1: %zu of %zu cases not bit-equal\n", off_unequal, cases.size());
    CHECK(off_unequal == 0);
    // The slope's change at one point: (y(+2) - y(+1)) - (y(-1) - y(-2)) over one step, relative to the brightest channel.
    const auto slope_change = [&](const std::vector<Float4>& out, size_t first) {
      float brightest = 1e-3f;
      for (int c = 0; c < 3; ++c) brightest = std::max(brightest, std::abs(out[first + 2][c]));
      float change = 0.f;
      for (int c = 0; c < 3; ++c) {
        const float above = out[first + 4][c] - out[first + 3][c];
        const float below = out[first + 1][c] - out[first][c];
        change = std::max(change, std::abs(above - below) / (STEP * brightest));
      }
      return change;
    };
    float worst_stock = 0.f;
    std::array<float, SLOPE_POINTS.size()> slope_addon = {};
    std::array<float, SLOPE_POINTS.size()> slope_control = {};
    // Bands of the brightest channel: below the start, 0.8 - 1, 1 - 2, 2 - 4, 4 - 16; OkLab distance over lightness.
    constexpr std::array<float, 5> BAND_TOPS = {SHOULDER_START, 1.f, 2.f, 4.f, 16.f};
    std::array<float, BAND_TOPS.size()> band_distance = {};
    std::array<float, BAND_TOPS.size()> band_alpha = {};
    float below_start = 0.f;
    for (size_t i = 0; i < cases.size(); i += levels.size()) {
      const Globals& globals = grades[i / (levels.size() * colours.size())];
      for (size_t level = 0; level < 2; ++level) {
        const Float3 colour = {cases[i + level].color[0], cases[i + level].color[1], cases[i + level].color[2]};
        const Float3 stock_grade = StockGrade(colour, globals);
        for (int c = 0; c < 3; ++c) worst_stock = std::max(worst_stock, std::abs(stock_grade[c] - results[i + level][c]));
      }
      for (size_t point = 0; point < SLOPE_POINTS.size(); ++point) {
        slope_addon[point] = std::max(slope_addon[point], slope_change(results, i + SLOPE_FIRST + point * 5));
        slope_control[point] = std::max(slope_control[point], slope_change(control, i + SLOPE_FIRST + point * 5));
      }
      for (size_t level = RAMP_FIRST; level < levels.size(); ++level) {
        const Float4& ours = results[i + level];
        const Float4& before = control[i + level];
        const size_t band = std::ranges::lower_bound(BAND_TOPS, levels[level]) - BAND_TOPS.begin();
        if (levels[level] < SHOULDER_START) {
          for (int c = 0; c < 4; ++c) below_start = std::max(below_start, std::abs(ours[c] - before[c]) / std::max(1.f, std::abs(before[c])));
        }
        const Float3 a = OkLab({ours[0], ours[1], ours[2]});
        const Float3 b = OkLab({before[0], before[1], before[2]});
        const float distance = std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
        band_distance[band] = std::max(band_distance[band], distance / std::max(b[0], 1e-3f));
        band_alpha[band] = std::max(band_alpha[band], std::abs(ours[3] - before[3]));
      }
    }
    std::printf("4. grade: largest difference to the stock's below the shoulder's start %.3g (%zu cases)\n", worst_stock, cases.size());
    std::printf("   slope change across 2 x %.0e of brightness at 0.8 / 0.9 / 1 / 1.25 / 2: add-on", STEP);
    for (const float change : slope_addon) std::printf(" %.4f", change);
    std::printf("; control (hard knee at 1)");
    for (const float change : slope_control) std::printf(" %.4f", change);
    std::printf("\n   against the control: below 0.8 largest difference %.3g; OkLab distance over lightness (alpha) 0.8 - 1 %.4f (%.4f),"
                " 1 - 2 %.4f (%.4f), 2 - 4 %.4f (%.4f), 4 - 16 %.4f (%.4f)\n",
                below_start, band_distance[1], band_alpha[1], band_distance[2], band_alpha[2], band_distance[3], band_alpha[3],
                band_distance[4], band_alpha[4]);
    CHECK(worst_stock <= 1e-4f);
    CHECK(*std::ranges::max_element(slope_addon) <= 0.05f);
    CHECK(slope_control[2] > 0.05f);
    CHECK(below_start <= 1e-6f);
  }

  // Report: luminance falls along the exposure ramps, ours against the game's SDR picture (the selective colour can dim a
  // colour as it passes Source on its own).
  for (const float mode : {0.f, 3.f, 4.f}) {
    int falls = 0;
    for (const Atmosphere& atmosphere : {ATMOSPHERES[0], ATMOSPHERES[7]}) {
      const auto ours = Draw(mode == 0.f ? stock.Get() : addon.Get(), MakeGlobals(atmosphere, 1.97f, true), Injection(mode, 1000.f, 0.8f));
      for (UINT column = 0; column < BLACK; ++column) {
        float last = -1.f;
        for (UINT row = 0; row < HEIGHT; ++row) {
          const Float4 encoded = At(ours, column, row);
          const float y = Luminance(Linear(mode == 0.f ? Float4{std::clamp(encoded[0], 0.f, 1.f), std::clamp(encoded[1], 0.f, 1.f),
                                                                  std::clamp(encoded[2], 0.f, 1.f), 1.f}
                                                         : encoded));
          if (y < last * 0.995f - 1e-5f) ++falls;
          last = std::max(last, y);
        }
      }
    }
    std::printf("   %s, saturation 1.97 + Black Cove's colours: %d falls along the ramps\n", mode == 0.f ? "the game's SDR" : (mode == 3.f ? "Extended" : "PsychoV35"), falls);
  }

  // 5. Report.
  {
    const Globals globals = MakeGlobals(ATMOSPHERES[0], 1.f, false);
    const auto game = Draw(stock.Get(), globals, Injection(0.f, 1000.f, 0.f));
    for (const float mode : {3.f, 4.f, 1.f}) {
      const auto ours = Draw(addon.Get(), globals, Injection(mode, 1000.f, 0.8f));
      double distance = 0.0;
      int count = 0;
      for (UINT row = 0; row < HEIGHT; ++row) {
        for (UINT column = 0; column < BLACK; ++column) {
          const Float4 encoded = At(game, column, row);
          if (std::max({encoded[0], encoded[1], encoded[2]}) >= 0.98f) continue;
          const Float3 a = OkLab(Linear(encoded));
          const Float3 b = OkLab(Linear(At(ours, column, row)));
          distance += std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
          ++count;
        }
      }
      std::printf("5. mode %.0f: mean OkLab distance to the game's SDR picture below its clip %.4f (%d colours)\n", mode, distance / count, count);
    }
    const UINT bright = 34;
    std::printf("   hue (OkLab degrees) at row %u: SDR fire %.1f sky %.1f\n", bright, HueDegrees(Linear(At(game, FIRE, bright))),
                HueDegrees(Linear(At(game, SKY, bright))));
    for (const float mode : {4.f, 3.f}) {
      for (int step = 0; step <= 10; ++step) {
        const float hue_clip = step * 0.1f;
        const auto ours = Draw(addon.Get(), globals, Injection(mode, 1000.f, hue_clip));
        std::printf("   %s Hue Clip %.1f: fire %.1f sky %.1f\n", mode == 4.f ? "PsychoV35" : "Extended ", hue_clip,
                    HueDegrees(Linear(At(ours, FIRE, bright))), HueDegrees(Linear(At(ours, SKY, bright))));
      }
    }
  }

  // 6. Smooth Grade Shoulder off against the tone map before the shoulder (bit for bit); the row on the control.
  {
    const auto before = LoadPixelShader(argv[6]);
    size_t texels_off = 0;
    size_t unequal_off = 0;
    std::array<float, 3> worst_on = {};  // SDR (Native), Extended, PsychoV35
    constexpr std::array<float, 3> MODES = {0.f, 3.f, 4.f};
    for (const Atmosphere& atmosphere : ATMOSPHERES) {
      for (const float saturation : {1.f, 1.97f, 0.34f}) {
        for (const bool black_cove : {false, true}) {
          Globals globals = MakeGlobals(atmosphere, saturation, black_cove);
          for (const float fade : {0.f, 0.35f}) {
            globals.params[3] = fade;
            for (size_t mode = 0; mode < MODES.size(); ++mode) {
              for (const float hue_clip : {0.f, 0.7f}) {
                for (const float output : {0.f, 2.f}) {
                  auto injection = Injection(MODES[mode], 1000.f, hue_clip, output);
                  const auto old = Draw(before.Get(), globals, injection);
                  const auto ours = Draw(addon.Get(), globals, injection);
                  injection.grade_shoulder = 1.f;
                  const auto shoulder = Draw(addon.Get(), globals, injection);
                  for (size_t i = 0; i < old.size(); ++i) {
                    ++texels_off;
                    unequal_off += (std::memcmp(&old[i], &ours[i], sizeof(Float4)) != 0) ? 1 : 0;
                    for (int c = 0; c < 4; ++c) worst_on[mode] = std::max(worst_on[mode], std::abs(old[i][c] - shoulder[i][c]));
                  }
                }
              }
            }
          }
        }
      }
    }
    std::printf("6. Smooth Grade Shoulder off against the tone map before it (a6762afab^): %zu of %zu texels not bit-equal\n",
                unequal_off, texels_off);
    std::printf("   control, the row on: largest difference SDR (Native) %.3g, Extended %.3g, PsychoV35 %.3g\n", worst_on[0], worst_on[1],
                worst_on[2]);
    CHECK(unequal_off == 0);
    CHECK(worst_on[0] == 0.f);
    CHECK(worst_on[1] > 1e-3f && worst_on[2] > 1e-3f);
  }

  // 7. V35 FIT (handle-sdr-tonemap-lut: the defaults match the vanilla look; tools\tw\testbin\scratch\g04_\v35_pilot.md).
  //    PsychoV35's Contrast, Shadows, Saturation and Highlight Contrast fitted to the game's SDR below white, a minimax: the
  //    worst of grey |V35 / SDR - 1| where SDR is 0.05 - 1 and 1 - 15 nits and |chroma - 1| (OkLab at equal lightness, the
  //    geometric mean) where SDR is 1 - 30 nits. A candidate counts only if it keeps PsychoV35's highlights: grey within
  //    10 % of Musa's own where Musa's own is 100 nits up to 95 % of Peak. Coarse to fine: every 0.1 over the ranges, then
  //    0.05 and 0.01 steps around the best. Hue Clip 70 (the add-on's default), 1000 / 203 nits, Output HDR10. The game's
  //    constants as R1 traced them in the save (ThePrisonGardenOfStoneKing Sunny: A 0.22, B 0.18, D 0.2, saturation 1.1,
  //    Source / Target white, the matrix's alpha row 0 0 0 1); the menu's (Menuscreen_Cyseal) a report. The scene: grey and
  //    21 hues x saturation 0.4 / 0.8 / 0.95 at one luminance a row, a quarter stop a row from 2^-16 (96 rows).
  {
    constexpr UINT FIT_HEIGHT = 96;
    std::vector<Float3> columns = {{1.f, 1.f, 1.f}};
    for (int h = 0; h < 21; ++h) {
      for (const float s : {0.4f, 0.8f, 0.95f}) {
        Float3 colour = Hue(h / 21.f);
        for (float& channel : colour) channel = 1.f - s * (1.f - channel);
        columns.push_back(colour);
      }
    }
    std::vector<Float4> fit_texels;
    for (UINT y = 0; y < FIT_HEIGHT; ++y) {
      const float brightness = std::exp2(-16.f + 0.25f * y);
      for (const Float3& colour : columns) {
        const float scale = brightness / Luminance(colour);
        fit_texels.push_back({colour[0] * scale, colour[1] * scale, colour[2] * scale, 1.f});
      }
    }
    const D3D11_TEXTURE2D_DESC fit_desc = {WIDTH, FIT_HEIGHT, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                           D3D11_BIND_SHADER_RESOURCE, 0, 0};
    const D3D11_SUBRESOURCE_DATA fit_init = {fit_texels.data(), WIDTH * sizeof(Float4), 0};
    ComPtr<ID3D11Texture2D> fit_texture;
    REQUIRE(SUCCEEDED(device->CreateTexture2D(&fit_desc, &fit_init, &fit_texture)));
    ComPtr<ID3D11ShaderResourceView> fit_scene;
    REQUIRE(SUCCEEDED(device->CreateShaderResourceView(fit_texture.Get(), nullptr, &fit_scene)));

    const auto traced = [](const Atmosphere& atmosphere, float saturation) {
      Globals globals = MakeGlobals(atmosphere, saturation, false);
      globals.color_matrix[3] = {0.f, 0.f, 0.f, 1.f};
      globals.source_color = globals.target_color = {1.f, 1.f, 1.f, 0.f};
      return globals;
    };
    const Globals save = traced({0.22f, 0.18f, 0.2f}, 1.1f);
    const Globals menu = traced({0.48f, 0.14f, 0.62f}, 1.14f);
    // Decoded at 2.2 (Paper White 1); the stock's through its UNORM target's clip.
    const auto decoded = [](const std::vector<Float4>& texels, bool clip) {
      std::vector<Float3> linear;
      for (const Float4& texel : texels) {
        linear.push_back(Linear(clip ? Float4{std::clamp(texel[0], 0.f, 1.f), std::clamp(texel[1], 0.f, 1.f), std::clamp(texel[2], 0.f, 1.f), 1.f}
                                     : texel));
      }
      return linear;
    };
    const auto game_sdr = [&](const Globals& globals) {
      return decoded(Draw(stock.Get(), globals, Injection(0.f, 1000.f, 0.f), fit_scene.Get()), true);
    };
    const auto ours = [&](const Globals& globals, const divinity::ShaderInjectData& injection) {
      return decoded(Draw(addon.Get(), globals, injection, fit_scene.Get()), false);
    };

    struct Slider {
      const char* name;
      float low, high;
    };
    constexpr std::array<Slider, 4> SLIDERS = {
        {{"Contrast", 0.8f, 2.f}, {"Shadows", 0.3f, 1.5f}, {"Saturation", 0.7f, 1.3f}, {"Highlight Contrast", 0.6f, 1.2f}}};
    using Values = std::array<float, 4>;
    const auto with = [](divinity::ShaderInjectData si, const Values& values) {
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
    const auto highlights = [](const std::vector<Float3>& fitted, const std::vector<Float3>& own, float paper_white, float peak) {
      float worst = 0.f;
      for (UINT y = 0; y < FIT_HEIGHT; ++y) {
        const float own_nits = Luminance(own[y * WIDTH]) * paper_white;
        if (own_nits < 100.f || own_nits > 0.95f * peak) continue;
        worst = std::max(worst, std::abs((Luminance(fitted[y * WIDTH]) / Luminance(own[y * WIDTH])) - 1.f));
      }
      return worst;
    };

    const auto sdr = game_sdr(save);
    const auto own = Injection(4.f, 1000.f, 0.7f);
    const auto own_ramp = ours(save, own);
    Values best = {1.f, 1.f, 1.f, 1.f};
    float best_score = 1e9f;
    int candidates = 0;
    const auto start = std::chrono::steady_clock::now();
    bool first_pass = true;
    for (const auto& [step, reach] : {std::pair{0.1f, 6}, std::pair{0.05f, 2}, std::pair{0.01f, 3}}) {
      const Values centre = (first_pass ? Values{1.4f, 0.9f, 1.f, 0.9f} : best);
      first_pass = false;
      std::array<std::vector<float>, 4> grid;
      for (size_t d = 0; d < SLIDERS.size(); ++d) {
        for (int k = -reach; k <= reach; ++k) {
          const float value = std::round((centre[d] + (static_cast<float>(k) * step)) * 100.f) / 100.f;
          if (value >= SLIDERS[d].low - 1e-4f && value <= SLIDERS[d].high + 1e-4f) grid[d].push_back(value);
        }
      }
      for (const float contrast : grid[0]) {
        for (const float shadows : grid[1]) {
          for (const float saturation : grid[2]) {
            for (const float highlight_contrast : grid[3]) {
              const Values values = {contrast, shadows, saturation, highlight_contrast};
              const auto ramp = ours(save, with(own, values));
              ++candidates;
              if (highlights(ramp, own_ramp, NITS, 1000.f) > 0.1f) continue;
              if (const float score = below_white(Compare(sdr, ramp)); score < best_score) {
                best = values;
                best_score = score;
              }
            }
          }
        }
      }
    }
    std::printf("7. V35 fit, R1's save (A 0.22, B 0.18, D 0.2, saturation 1.1):\n"
                "   minimax fit (%s %.1f - %.1f, %s %.1f - %.1f, %s %.1f - %.1f, %s %.1f - %.1f): %.2f / %.2f / %.2f / %.2f, "
                "worst %.1f %% (%d candidates, %.0f s)\n",
                SLIDERS[0].name, SLIDERS[0].low, SLIDERS[0].high, SLIDERS[1].name, SLIDERS[1].low, SLIDERS[1].high,
                SLIDERS[2].name, SLIDERS[2].low, SLIDERS[2].high, SLIDERS[3].name, SLIDERS[3].low, SLIDERS[3].high, best[0],
                best[1], best[2], best[3], 100.f * best_score, candidates,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    const auto shipped = with(own, {DIVINITY_V35_CONTRAST, DIVINITY_V35_SHADOWS, DIVINITY_V35_SATURATION, DIVINITY_V35_HIGHLIGHT_CONTRAST});
    const auto shipped_ramp = ours(save, shipped);
    const float shipped_score = below_white(Compare(sdr, shipped_ramp));
    const float own_score = below_white(Compare(sdr, own_ramp));
    std::printf("   shared.h DIVINITY_V35_* (%.2f / %.2f / %.2f / %.2f): worst %.1f %%; Musa's own (control, must fail 5 %%): %.1f %%\n",
                DIVINITY_V35_CONTRAST, DIVINITY_V35_SHADOWS, DIVINITY_V35_SATURATION, DIVINITY_V35_HIGHLIGHT_CONTRAST,
                100.f * shipped_score, 100.f * own_score);
    CHECK(shipped_score < 0.05f);
    CHECK(shipped_score <= best_score + 0.005f);
    CHECK(own_score > 0.05f);
    Report("Musa's own sliders, the save", sdr, own_ramp);
    Report("the V35 defaults (shared.h), the save", sdr, shipped_ramp);
    for (const auto& [peak, paper_white] : {std::pair{1000.f, 203.f}, std::pair{2500.f, 275.f}}) {
      auto shipped_at = shipped;
      auto own_at = own;
      shipped_at.peak_white_nits = own_at.peak_white_nits = peak;
      shipped_at.diffuse_white_nits = own_at.diffuse_white_nits = paper_white;
      shipped_at.graphics_white_nits = own_at.graphics_white_nits = paper_white;
      const float worst = highlights(ours(save, shipped_at), ours(save, own_at), paper_white, peak);
      std::printf("   highlights, Peak %.0f / Paper White %.0f: grey within %.1f %% of Musa's own (100 nits - 95 %% of Peak)\n",
                  peak, paper_white, 100.f * worst);
      CHECK(worst <= 0.1f);
    }
    // The measure's own check: HDR (Extended) is the game's Hable below its pivot (every grey band here), so a wrong
    // decode, white or binding would show.
    const Comparison extended = Compare(sdr, ours(save, Injection(3.f, 1000.f, 0.7f)));
    std::printf("   control, HDR (Extended): grey %.2f %% off, chroma x%.3f (must be the game's grey)\n", 100.f * grey_error(extended),
                extended.chroma[0]);
    CHECK(grey_error(extended) < 0.005f);
    const auto menu_sdr = game_sdr(menu);
    Report("Musa's own sliders, the menu (A 0.48, B 0.14, D 0.62, saturation 1.14)", menu_sdr, ours(menu, own));
    Report("the V35 defaults (shared.h), the menu", menu_sdr, ours(menu, shipped));
  }

  std::printf(failures == 0 ? "tw_divinity1: all checks passed\n" : "tw_divinity1: %d checks failed\n", failures);
  return failures == 0 ? 0 : 1;
}
