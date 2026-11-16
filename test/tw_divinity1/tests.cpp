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
// 4. The grade alone (grade.hlsl, GradeAfterCurve): the stock's grade below 1 (ColorMatrix identity, saturation 1.97 and
//    0.34, Black Cove's colours) within 1e-4, and continuous where the brightest channel crosses 1.
// 5. Report: luminance falls along the ramps against the game's own, OkLab distance to the game's SDR picture below its
//    clip, and fire / sky hue at Hue Clip 0 - 1 for both modes (the defaults come from it).
// tests.exe <game cso> <add-on cso> <grade cso>.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
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

static std::vector<Float4> Draw(ID3D11PixelShader* shader, const Globals& globals, const divinity::ShaderInjectData& injection) {
  const D3D11_TEXTURE2D_DESC target_desc = {WIDTH, HEIGHT, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
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
  context->PSSetShaderResources(0, 1, scene.GetAddressOf());
  const D3D11_VIEWPORT viewport = {0.f, 0.f, WIDTH, HEIGHT, 0.f, 1.f};
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
  std::vector<Float4> texels(WIDTH * HEIGHT);
  for (UINT row = 0; row < HEIGHT; ++row) {
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
  // Saturation in gamma space about BT.709 luminance (a stand-in: the engine's own matrix is not known offline).
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      const float weight = std::array<float, 3>{0.2126f, 0.7152f, 0.0722f}[c];
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
      .sun_shadow_softness = 1.f,
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

int main(int argc, char** argv) {
  REQUIRE(argc >= 4);
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

  // 4. The grade alone: the stock's below 1 (within 1e-4), continuous where the brightest channel crosses 1.
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
    // Each colour at its brightest channel 0.5, just below 1, just above 1, 3.
    constexpr std::array<float, 4> LEVELS = {0.5f, 1.f - 1e-4f, 1.f + 1e-4f, 3.f};
    for (const Globals& globals : grades) {
      for (const Float3& colour : colours) {
        const float colour_max = std::max({colour[0], colour[1], colour[2]});
        for (const float level : LEVELS) {
          const float scale = level / colour_max;
          cases.push_back({.color = {colour[0] * scale, colour[1] * scale, colour[2] * scale, 0.f}, .color_matrix = globals.color_matrix,
                           .source_color = globals.source_color, .target_color = globals.target_color});
        }
      }
    }
    const auto results = RunGrade(argv[3], cases);
    float worst_stock = 0.f;
    float worst_jump = 0.f;
    for (size_t i = 0; i < cases.size(); i += LEVELS.size()) {
      const Globals& globals = grades[i / (LEVELS.size() * colours.size())];
      for (size_t level = 0; level < 2; ++level) {
        const Float3 colour = {cases[i + level].color[0], cases[i + level].color[1], cases[i + level].color[2]};
        const Float3 stock_grade = StockGrade(colour, globals);
        for (int c = 0; c < 3; ++c) worst_stock = std::max(worst_stock, std::abs(stock_grade[c] - results[i + level][c]));
      }
      for (int c = 0; c < 3; ++c) {
        worst_jump = std::max(worst_jump, std::abs(results[i + 2][c] - results[i + 1][c]) / std::max(std::abs(results[i + 1][c]), 1e-3f));
      }
    }
    std::printf("4. grade: largest difference to the stock's below 1 %.3g, largest relative jump across 1 %.3g (%zu cases)\n",
                worst_stock, worst_jump, cases.size());
    CHECK(worst_stock <= 1e-4f);
    CHECK(worst_jump <= 2e-3f);
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

  std::printf(failures == 0 ? "tw_divinity1: all checks passed\n" : "tw_divinity1: %d checks failed\n", failures);
  return failures == 0 ? 0 : 1;
}
