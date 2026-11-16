// Part 6 (Divinity 2): the add-on's LUT builder t00 (PPComputeLUT 0x7016F292, cs_5_0) and LUT pass t01 (PPApplyLUT
// 0xC5E7DE4B) on WARP, compiled by part6_divinity2.ps1 from this tree and from the commit before PsychoV35 with the
// build's flags, against the game's own bytes of both. part6_divinity2.exe <before_cso> <after_cso> <the game's cso folder>
// Every LUT is built from t00's constants as the R1b data run traced them (TracedConstants).
// 1. Every mode that existed before (Off, Vanilla, Tweaked ACES, PsychoV30) on every traced HDR LUT (the city's, the
//    menu's, the neutral one) and every mode on the inventory's SDR LUT give the same LUT bits as before, sliders at their
//    defaults and moved, PsychoV35's sliders moved too.
// 2. PsychoV35 (Rendering Mode 4) through the real chain (t00 -> 32^3 R10G10B10A2 LUT -> t01) and evaluated directly
//    (part6_divinity2.hlsl), 64 colours x 48 exposures (0.18 x 2^-10 .. 2^13.5), Peak / Paper White 1000 / 203,
//    2500 / 203, 4000 / 275 on the neutral HDR LUT: finite; every BT.2020 channel within Peak (+ 1.5 PQ codes through the
//    10-bit LUT); grey 0.18 at 15 nits x Paper White / 203 (+-1 % direct, +-3 % through the LUT); grey never darker as it
//    rises; the HDR LUT tagged alpha 0 (t01's fixed shaper domain), the SDR LUT alpha 1. Report: the LUT's error against
//    the direct evaluation in 10-bit PQ codes. The city's LUT (ATN's grade): the same bounds, grey as its gain puts it.
// 3. Control that must fail: PsychoV35 fed the game's own tone curve's output (a curve stacked on a curve) misses grey's
//    15 nits; the grey check has to say so.
// 4. Report: PsychoV35's hue (OkLab) against the game's SDR picture where it reaches its clip, for fire, red, sky, green
//    and 48 hues, Hue Clip 0 to 1.
// 5. Off against the game's own t00, bit for bit, on every traced cb0, and the inventory's SDR LUT in every mode (it stays
//    stock). Control that must fail: the add-on fed the cb0 the test assumed before the trace (no luminance blend,
//    highlight fix 1) differs from the game's LUT.
// 6. Vanilla's Per-Channel Highlights (on by default): at Peak 600 (compressed) grey unchanged, within Peak, fire's core
//    whiter above the knee; at Peak 2500 (expanded) the same bits as off. Part 1 runs Vanilla with it off: the
//    brightest-channel shoulder keeps its bits.
// 7. Exact Colour LUT (Experimental, off): off = t01's pixels as before; on = a modded LUT read at its texel centres
//    (grey 15 nits through the LUT, interpolation error only); Off's LUT read as before either way.
// 8. Hue Clip's reference (SdrReference) against the game's own SDR LUT: every texel within 1.5 8-bit sRGB codes. Control
//    that must fail: the stand-in used before the trace (RenoDX's ACES at 0.02 / 48 nits alone) misses it.
// 9. t01 against the game's own bytes, pixel for pixel, on the game's HDR (Off) and SDR LUTs, Off and PsychoV35, fade 0 and
//    0.5. Control that must fail: the game's pass fed the SDR LUT's shaper domain for the HDR LUT differs.
// 10. PsychoV35 at its Divinity 2 defaults (shared.h: the game's response) against the game (its own t00, Off), both
//    through the chain (t00 -> LUT -> t01, the game's addressing), Peak 1000 / Paper White 203 (the game's reference),
//    the neutral and the city's LUT: grey within 5 % of the game where the game puts it at 0.05 - 1 and 1 - 15 nits;
//    colour (OkLab chroma at equal lightness, the ramp's 63 colours) within 5 % on average where the game's luminance is
//    1 - 30 nits (mid tones); grey at +5 .. +8 stops within 10 % of PsychoV35's own sliders (its highlights kept) at
//    1000 / 203 and 2500 / 275. Control that must fail: PsychoV35's own sliders (all 1, before ATN's 2026-11-16 decision).
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace dos2 {
#include "src/games/divinity2originalsindefinitive/shared.h"
}  // namespace dos2

using Microsoft::WRL::ComPtr;
using Float3 = std::array<float, 3>;
using Float4 = std::array<float, 4>;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

constexpr UINT LUT_SIZE = 32;
constexpr UINT COLOURS = 64;
constexpr UINT EXPOSURES = 48;
constexpr UINT GREY_ROW = 20;  // exposure 2^0: the scene's 0.18
constexpr float SHAPER_LOW = 5.4931616e-6f;
constexpr float SHAPER_HIGH = 1516.4874f;
constexpr float SDR_REFERENCE_CODES = 1.5f;  // part 8's bound, 8-bit sRGB codes

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static ComPtr<ID3D11VertexShader> full_screen;
static ComPtr<ID3D11SamplerState> linear_clamp;

static std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static ComPtr<ID3D11ComputeShader> LoadCompute(const std::filesystem::path& path) {
  const auto code = ReadFile(path);
  REQUIRE(!code.empty());
  ComPtr<ID3D11ComputeShader> shader;
  REQUIRE(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &shader)));
  return shader;
}

static ComPtr<ID3D11PixelShader> LoadPixel(const std::filesystem::path& path) {
  const auto code = ReadFile(path);
  REQUIRE(!code.empty());
  ComPtr<ID3D11PixelShader> shader;
  REQUIRE(SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, &shader)));
  return shader;
}

static ComPtr<ID3D11Buffer> Constants(const void* data, size_t size) {
  std::vector<uint8_t> bytes((size + 15u) & ~size_t{15}, 0);
  std::memcpy(bytes.data(), data, size);
  const D3D11_BUFFER_DESC desc = {static_cast<UINT>(bytes.size()), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
  const D3D11_SUBRESOURCE_DATA init = {bytes.data(), 0, 0};
  ComPtr<ID3D11Buffer> buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &init, &buffer)));
  return buffer;
}

// t00's _Globals (c0 - c32) as the R1b data run traced them (2026-11-16, Arx, the add-on's frame trace): c0
// ToneMapLuminanceApply, RedCorrectionScale, HighlightsFixAmount (the same in every dispatch); c1 - c20
// ColorCorrectionParams (global, shadows, midtones, highlights: saturation, contrast, gamma, gain, offset); c21 the
// ranges; c22 - c25 white balance; c26 - c32 Params (tone scale, shaper domain c31.w / c32.x, HDR gamma, output device).
enum class Frame {
  CITY_HDR,       // the city's HDR LUT: ATN's grade (global saturation 1.1, gain 0.92), ACES 1.1 at 1e-4 / 1000 nits
  MENU_HDR,       // the main menu's HDR LUT: its own grade, the same tone constants
  INVENTORY_SDR,  // the inventory's SDR LUT (output device 0): no grade, ACES 1.1 at 0.02 / 48 nits, sRGB
  NEUTRAL_HDR,    // the city's HDR LUT with the inventory's grade (identity): the colour checks' input
};
constexpr const char* FRAME_NAMES[] = {"city HDR", "menu HDR", "inventory SDR", "neutral HDR"};

constexpr Float4 TRACED_C0 = {0.100000001f, 1.f, 0.600000024f, 1.f};

static std::array<Float4, 33> TracedConstants(Frame frame) {
  constexpr Float4 ONES = {1.f, 1.f, 1.f, 1.f};
  constexpr Float4 ZEROS = {0.f, 0.f, 0.f, 0.f};
  std::array<Float4, 33> c = {{
      TRACED_C0,
      {1.f, 1.f, 1.f, 1.10000002f}, ONES, ONES, {1.f, 1.f, 1.f, 0.920000017f}, ZEROS,
      ONES, ONES, ONES, ONES, ZEROS,
      ONES, ONES, ONES, ONES, ZEROS,
      ONES, ONES, ONES, ONES, ZEROS,
      {0.0900000036f, 0.5f, 0.f, 0.f},
      {1.f, -2.56113708e-08f, 0.f, 0.f}, {-1.05937943e-08f, 1.f, 0.f, 0.f}, {0.f, 1.86264515e-09f, 1.f, 0.f}, {0.f, 0.f, 0.f, 1.f},
      {-5.60050154f, -4.f, 0.f, -1.08505154f}, {0.681241274f, 1.f, 2.84051466f, 3.f}, {0.f, -4.f, -4.f, -3.15737653f},
      {-0.485249996f, 1.84773254f, 1.84773254f, -0.332863092f}, {1.69534588f, 2.75812411f, 3.f, 3.f},
      {3.f, 9.99999975e-05f, 1000.f, 5.49316155e-06f}, {1516.48743f, 0.999997973f, std::bit_cast<float>(1u), 0.f},
  }};
  switch (frame) {
    case Frame::MENU_HDR:
      c[1] = {0.898039222f, 1.f, 1.f, 1.10000002f};
      c[2] = {1.f, 1.f, 1.f, 0.980000019f};
      c[3] = {1.f, 1.f, 1.f, 0.939999998f};
      c[4] = {1.f, 1.f, 1.f, 1.22000003f};
      c[7] = {1.f, 1.f, 1.f, 1.5f};
      c[13] = {1.f, 1.f, 1.f, 0.949999988f};
      c[17] = {1.f, 1.f, 1.f, 1.5f};
      c[19] = {1.f, 1.f, 1.f, 1.13999999f};
      break;
    case Frame::INVENTORY_SDR:
      c[1] = ONES;
      c[4] = ONES;
      c[26] = {-2.70142221f, -1.69896996f, 0.f, -0.744727314f};
      c[27] = {0.681241274f, 1.f, 1.21196759f, 1.68124127f};
      c[28] = {0.f, -1.69896996f, -1.69896996f, -0.865895987f};
      c[29] = {0.175761819f, 1.18672085f, 1.18672085f, 0.175761819f};
      c[30] = {1.18672085f, 1.57124126f, 1.68124127f, 1.68124127f};
      c[31] = {1.68124127f, 0.0199999996f, 48.f, 0.00198873784f};
      c[32] = {16.2917404f, 1.f, 0.f, 0.f};
      break;
    case Frame::NEUTRAL_HDR:
      c[1] = ONES;
      c[4] = ONES;
      break;
    case Frame::CITY_HDR:
      break;
  }
  return c;
}

static dos2::ShaderInjectData Injection(float mode) {
  return {
      .peak_white_nits = 1000.f, .diffuse_white_nits = 203.f, .graphics_white_nits = 203.f, .bloom = 1.f, .tonemap_type = mode,
      .exposure = 1.f, .highlights = 1.f, .shadows = 1.f, .contrast = 1.f, .saturation = 1.f, .cone_response_exponent = 1.f,
      .aces_exposure = 1.f, .aces_contrast = 1.f, .aces_highlights = 1.f, .aces_shadows = 1.f, .aces_saturation = 1.f,
      .aces_highlight_saturation = 1.f, .aces_tweaked_exposure = 1.f, .aces_tweaked_contrast = 1.f, .aces_tweaked_highlights = 1.f,
      .aces_tweaked_shadows = 1.f, .aces_tweaked_saturation = 1.f, .aces_tweaked_highlight_saturation = 1.f,
      .fire_color_strength = 0.45f, .fire_color_hue = 0.65f, .aces_tweaked_fire_color_strength = 0.25f,
      .aces_tweaked_fire_color_hue = 0.6f, .vanilla_fire_color_strength = 0.f, .vanilla_fire_color_hue = 0.75f,
      .hue_clip = 0.8f, .v35_exposure = 1.f, .v35_highlights = 1.f, .v35_shadows = DOS2_V35_SHADOWS,
      .v35_contrast = DOS2_V35_CONTRAST, .v35_highlight_contrast = DOS2_V35_HIGHLIGHT_CONTRAST, .v35_shadow_contrast = 1.f,
      .v35_flare = 0.f, .v35_saturation = DOS2_V35_SATURATION, .v35_dechroma = 0.f, .v35_highlight_saturation = 1.f,
      .v35_roll_off = 1.f, .v35_gamut_compression = 1.f,
  };
}

static void MoveSliders(dos2::ShaderInjectData* injection) {
  injection->exposure = 1.2f;
  injection->highlights = 0.8f;
  injection->contrast = 1.1f;
  injection->saturation = 0.9f;
  injection->aces_exposure = 1.15f;
  injection->aces_highlight_saturation = 0.7f;
  injection->aces_tweaked_contrast = 0.88f;
  injection->aces_tweaked_shadows = 1.05f;
  injection->fire_color_strength = 0.7f;
  injection->aces_tweaked_fire_color_strength = 0.5f;
  injection->vanilla_fire_color_strength = 0.6f;
  injection->hue_clip = 0.5f;
  injection->v35_exposure = 1.3f;
  injection->v35_highlight_saturation = 1.4f;
}

struct Lut {
  ComPtr<ID3D11Texture3D> texture;
  ComPtr<ID3D11ShaderResourceView> view;
  std::vector<uint8_t> bytes;  // read back, LUT_SIZE^3 texels
};

static Lut BuildLut(ID3D11ComputeShader* shader, DXGI_FORMAT format, const std::array<Float4, 33>& game,
                    const dos2::ShaderInjectData& injection) {
  context->ClearState();
  const D3D11_TEXTURE3D_DESC desc = {LUT_SIZE, LUT_SIZE, LUT_SIZE, 1, format, D3D11_USAGE_DEFAULT,
                                     D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0};
  Lut lut;
  REQUIRE(SUCCEEDED(device->CreateTexture3D(&desc, nullptr, &lut.texture)));
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(lut.texture.Get(), nullptr, &lut.view)));
  ComPtr<ID3D11UnorderedAccessView> uav;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(lut.texture.Get(), nullptr, &uav)));
  const auto game_buffer = Constants(game.data(), sizeof(game));
  const auto injection_buffer = Constants(&injection, sizeof(injection));
  context->CSSetConstantBuffers(0, 1, game_buffer.GetAddressOf());
  context->CSSetConstantBuffers(10, 1, injection_buffer.GetAddressOf());
  context->CSSetUnorderedAccessViews(0, 1, uav.GetAddressOf(), nullptr);
  context->CSSetShader(shader, nullptr, 0);
  context->Dispatch(1, 1, LUT_SIZE);
  D3D11_TEXTURE3D_DESC staging_desc = desc;
  staging_desc.BindFlags = 0;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture3D> staging;
  REQUIRE(SUCCEEDED(device->CreateTexture3D(&staging_desc, nullptr, &staging)));
  context->CopyResource(staging.Get(), lut.texture.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  const UINT texel_bytes = (format == DXGI_FORMAT_R16G16B16A16_UNORM ? 8u : 4u);
  lut.bytes.resize(LUT_SIZE * LUT_SIZE * LUT_SIZE * texel_bytes);
  for (UINT z = 0; z < LUT_SIZE; ++z) {
    for (UINT y = 0; y < LUT_SIZE; ++y) {
      std::memcpy(&lut.bytes[((z * LUT_SIZE + y) * LUT_SIZE) * texel_bytes],
                  static_cast<const uint8_t*>(mapped.pData) + z * mapped.DepthPitch + y * mapped.RowPitch, LUT_SIZE * texel_bytes);
    }
  }
  context->Unmap(staging.Get(), 0);
  return lut;
}

// t01's _Globals: Params = the shaper domain of the LUT's dispatch (t00's c31.w / c32.x), FadeValue.
static std::vector<Float4> ApplyLut(ID3D11PixelShader* shader, const Lut& lut, ID3D11ShaderResourceView* base,
                                    const std::array<Float4, 33>& game, const dos2::ShaderInjectData& injection,
                                    float fade = 0.f) {
  context->ClearState();
  const Float4 globals = {game[31][3], game[32][0], fade, 0.f};
  const auto globals_buffer = Constants(globals.data(), sizeof(globals));
  const auto injection_buffer = Constants(&injection, sizeof(injection));
  context->PSSetConstantBuffers(0, 1, globals_buffer.GetAddressOf());
  context->PSSetConstantBuffers(10, 1, injection_buffer.GetAddressOf());
  ID3D11ShaderResourceView* views[2] = {base, lut.view.Get()};
  context->PSSetShaderResources(0, 2, views);
  context->PSSetSamplers(0, 1, linear_clamp.GetAddressOf());
  const D3D11_TEXTURE2D_DESC target_desc = {COLOURS, EXPOSURES, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                            D3D11_BIND_RENDER_TARGET, 0, 0};
  ComPtr<ID3D11Texture2D> target;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&target_desc, nullptr, &target)));
  ComPtr<ID3D11RenderTargetView> target_view;
  REQUIRE(SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, &target_view)));
  const D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(COLOURS), static_cast<float>(EXPOSURES), 0.f, 1.f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(full_screen.Get(), nullptr, 0);
  context->PSSetShader(shader, nullptr, 0);
  context->Draw(3, 0);
  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.BindFlags = 0;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, &staging)));
  context->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<Float4> pixels(COLOURS * EXPOSURES);
  for (UINT y = 0; y < EXPOSURES; ++y) {
    std::memcpy(&pixels[y * COLOURS], static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, COLOURS * 16);
  }
  context->Unmap(staging.Get(), 0);
  return pixels;
}

// part6_divinity2.hlsl: two outputs per input (see its header), with the traced c0.
static std::vector<Float4> Direct(ID3D11ComputeShader* shader, const std::vector<Float4>& inputs, uint32_t mode,
                                  const dos2::ShaderInjectData& injection) {
  context->ClearState();
  const D3D11_BUFFER_DESC input_desc = {static_cast<UINT>(inputs.size() * 16), D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                                        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16};
  const D3D11_SUBRESOURCE_DATA init = {inputs.data(), 0, 0};
  ComPtr<ID3D11Buffer> input_buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&input_desc, &init, &input_buffer)));
  ComPtr<ID3D11ShaderResourceView> input_view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(input_buffer.Get(), nullptr, &input_view)));
  const D3D11_BUFFER_DESC output_desc = {static_cast<UINT>(inputs.size() * 32), D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0,
                                         D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16};
  ComPtr<ID3D11Buffer> output_buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&output_desc, nullptr, &output_buffer)));
  ComPtr<ID3D11UnorderedAccessView> output_view;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(output_buffer.Get(), nullptr, &output_view)));
  const std::array<uint32_t, 4> direct = {mode, std::bit_cast<uint32_t>(TRACED_C0[0]), std::bit_cast<uint32_t>(TRACED_C0[2]),
                                          std::bit_cast<uint32_t>(TRACED_C0[3])};
  const auto direct_buffer = Constants(direct.data(), sizeof(direct));
  const auto injection_buffer = Constants(&injection, sizeof(injection));
  context->CSSetConstantBuffers(1, 1, direct_buffer.GetAddressOf());
  context->CSSetConstantBuffers(10, 1, injection_buffer.GetAddressOf());
  context->CSSetShaderResources(0, 1, input_view.GetAddressOf());
  context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
  context->CSSetShader(shader, nullptr, 0);
  context->Dispatch(static_cast<UINT>((inputs.size() + 63) / 64), 1, 1);
  D3D11_BUFFER_DESC staging_desc = output_desc;
  staging_desc.BindFlags = 0;
  staging_desc.MiscFlags = 0;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&staging_desc, nullptr, &staging)));
  context->CopyResource(staging.Get(), output_buffer.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<Float4> outputs(inputs.size() * 2);
  std::memcpy(outputs.data(), mapped.pData, outputs.size() * 16);
  context->Unmap(staging.Get(), 0);
  return outputs;
}

// Colours ///////////////////////////////////////////////////////////////////////////////////////////////////////////

static Float3 Hue(float degrees, float saturation) {
  Float3 rgb;
  for (int i = 0; i < 3; ++i) {
    const float k = std::fmod(degrees / 60.f + (i == 0 ? 5.f : (i == 1 ? 3.f : 1.f)), 6.f);
    rgb[i] = 1.f - saturation * std::max(0.f, std::min({k, 4.f - k, 1.f}));
  }
  return rgb;
}

// Column 0 grey, 1 fire, 2 red, 3 sky, 4 green, 5 - 52 48 hues, the rest less saturated hues; row y = 0.18 x 2^((y - 20) / 2).
static std::vector<Float4> Ramp() {
  std::vector<Float3> colours = {{1.f, 1.f, 1.f}, {1.f, 0.35f, 0.08f}, {1.f, 0.05f, 0.05f}, {0.2f, 0.5f, 1.f}, {0.1f, 1.f, 0.1f}};
  for (int h = 0; h < 48; ++h) colours.push_back(Hue(h * 7.5f, 0.9f));
  while (colours.size() < COLOURS) colours.push_back(Hue(colours.size() * 33.f, 0.5f));
  std::vector<Float4> texels;
  for (UINT y = 0; y < EXPOSURES; ++y) {
    const float scale = 0.18f * std::exp2((static_cast<float>(y) - GREY_ROW) * 0.5f);
    for (const auto& c : colours) texels.push_back({c[0] * scale, c[1] * scale, c[2] * scale, 1.f});
  }
  return texels;
}

static float PQToNits(float code) {
  const float p = std::pow(std::max(code, 0.f), 1.f / 78.84375f);
  return 10000.f * std::pow(std::max(p - 0.8359375f, 0.f) / (18.8515625f - 18.6875f * p), 1.f / 0.1593017578f);
}

static float NitsToPQ(float nits) {
  const float y = std::pow(std::max(nits, 0.f) / 10000.f, 0.1593017578f);
  return std::pow((0.8359375f + 18.8515625f * y) / (1.f + 18.6875f * y), 78.84375f);
}

static float SrgbEncode(float linear) {
  return linear <= 0.0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
}

static float LuminanceBT2020(const Float4& c) { return 0.2627002120f * c[0] + 0.6779980715f * c[1] + 0.0593017165f * c[2]; }

static Float3 OkLab(const Float3& c) {
  const float l = 0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2];
  const float m = 0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2];
  const float s = 0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2];
  const float l3 = std::cbrt(l), m3 = std::cbrt(m), s3 = std::cbrt(s);
  return {0.2104542553f * l3 + 0.7936177850f * m3 - 0.0040720468f * s3, 1.9779984951f * l3 - 2.4285922050f * m3 + 0.4505937099f * s3,
          0.0259040371f * l3 + 0.7827717662f * m3 - 0.8086757660f * s3};
}

static bool GreyOk(float nits, float expected, float tolerance) {
  return std::abs(nits / expected - 1.f) <= tolerance;
}

// The largest difference of two 16-bit UNORM LUTs, in 16-bit units.
static int WorstDifference(const Lut& a, const Lut& b) {
  int worst = 0;
  for (size_t i = 0; i + 1 < a.bytes.size(); i += 2) {
    worst = std::max(worst, std::abs((a.bytes[i] | (a.bytes[i + 1] << 8)) - (b.bytes[i] | (b.bytes[i + 1] << 8))));
  }
  return worst;
}

// 1. Bits ///////////////////////////////////////////////////////////////////////////////////////////////////////////

static void TestBits(ID3D11ComputeShader* before, ID3D11ComputeShader* after) {
  int compared = 0;
  for (const Frame frame : {Frame::CITY_HDR, Frame::MENU_HDR, Frame::NEUTRAL_HDR, Frame::INVENTORY_SDR}) {
    const bool hdr = (frame != Frame::INVENTORY_SDR);
    for (const float mode : {0.f, 1.f, 2.f, 3.f, 4.f}) {
      // PsychoV35's HDR LUT is new: only its SDR LUT (the stock path) must be unchanged.
      if (hdr && mode == 4.f) continue;
      for (const bool moved : {false, true}) {
        auto injection = Injection(mode);
        if (moved) MoveSliders(&injection);
        const auto a = BuildLut(before, DXGI_FORMAT_R16G16B16A16_UNORM, TracedConstants(frame), injection);
        const auto b = BuildLut(after, DXGI_FORMAT_R16G16B16A16_UNORM, TracedConstants(frame), injection);
        const bool same = (a.bytes == b.bytes);
        if (!same) {
          std::printf("1. %s LUT, mode %.0f, sliders %s: bits differ\n", FRAME_NAMES[static_cast<int>(frame)], mode,
                      moved ? "moved" : "default");
        }
        CHECK(same);
        ++compared;
      }
    }
  }
  std::printf("1. %d LUTs compared with the commit before PsychoV35 (modes 0-3 on the city / menu / neutral HDR LUTs, 0-4 on "
              "the inventory's SDR LUT; traced cb0s)\n", compared);
}

// 2. / 3. PsychoV35 ////////////////////////////////////////////////////////////////////////////////////////////////

struct V35Case {
  float peak;
  float paper_white;
  float mid_gray_option;  // v35_mid_gray: 0 the game's HDR curve, 1 its SDR picture
  Frame frame;
};

static void TestPsychoV35(ID3D11ComputeShader* t00, ID3D11PixelShader* t01, ID3D11ComputeShader* direct_shader,
                          ID3D11ShaderResourceView* base, const std::vector<Float4>& ramp) {
  for (const V35Case test : {V35Case{1000.f, 203.f, 0.f, Frame::NEUTRAL_HDR}, V35Case{2500.f, 203.f, 0.f, Frame::NEUTRAL_HDR},
                             V35Case{4000.f, 275.f, 0.f, Frame::NEUTRAL_HDR}, V35Case{1000.f, 203.f, 1.f, Frame::NEUTRAL_HDR},
                             V35Case{1000.f, 203.f, 0.f, Frame::CITY_HDR}}) {
    auto injection = Injection(4.f);
    injection.peak_white_nits = test.peak;
    injection.diffuse_white_nits = test.paper_white;
    injection.v35_mid_gray = test.mid_gray_option;
    const auto game = TracedConstants(test.frame);
    // The city's grade on its own: grey (saturation leaves it alone) goes in scaled by the global gain
    // (ColorCorrectionParams[3].w; every region's own gain is 1). Colours are compared on the neutral LUT only.
    const bool graded = (test.frame != Frame::NEUTRAL_HDR);
    const float grey_gain = game[4][3];
    const auto lut = BuildLut(t00, DXGI_FORMAT_R10G10B10A2_UNORM, game, injection);
    CHECK((std::bit_cast<uint32_t>(std::array<uint8_t, 4>{lut.bytes[0], lut.bytes[1], lut.bytes[2], lut.bytes[3]}) >> 30) == 0u);
    const auto chain = ApplyLut(t01, lut, base, game, injection);
    std::vector<Float4> graded_ramp = ramp;
    for (auto& texel : graded_ramp) {
      for (int c = 0; c < 3; ++c) texel[c] *= grey_gain;
    }
    const auto direct = Direct(direct_shader, graded_ramp, 0u, injection);
    // The game's addressing: t00 writes texel i for the shaper's PQ i / 31, t01 samples the PQ code u straight (no
    // half-texel remap), so a sample reads the colour at PQ (32 u - 0.5) / 31. V35 evaluated at that colour separates the
    // LUT's interpolation error from this offset (the game's own, every mode, Off too).
    std::vector<Float4> sampled_ramp = graded_ramp;
    const float shaper_scale = 10000.f / (SHAPER_HIGH - SHAPER_LOW);
    const float shaper_bias = -shaper_scale * SHAPER_LOW;
    for (UINT i = 0; i < sampled_ramp.size(); ++i) {
      for (int c = 0; c < 3; ++c) {
        // The LUT is addressed by the scene colour before the grade.
        const float u = NitsToPQ(ramp[i][c] * shaper_scale + shaper_bias);
        const float sampled = std::clamp((u * LUT_SIZE - 0.5f) / (LUT_SIZE - 1), 0.f, 1.f);
        sampled_ramp[i][c] = (PQToNits(sampled) - shaper_bias) / shaper_scale * grey_gain;
      }
    }
    const auto direct_sampled = Direct(direct_shader, sampled_ramp, 0u, injection);
    const float ceiling = PQToNits(NitsToPQ(test.peak) + 1.5f / 1023.f);
    int finite = 0, within_peak = 0, direct_within_peak = 0;
    float worst_codes = 0.f;
    std::vector<float> errors;
    std::vector<float> offset_errors;
    for (UINT i = 0; i < chain.size(); ++i) {
      const Float4 nits = {PQToNits(chain[i][0]), PQToNits(chain[i][1]), PQToNits(chain[i][2]), 0.f};
      const Float4& exact = direct[i * 2];
      const Float4& exact_sampled = direct_sampled[i * 2];
      finite += (std::isfinite(nits[0]) && std::isfinite(nits[1]) && std::isfinite(nits[2]) && std::isfinite(exact[0]) &&
                 std::isfinite(exact[1]) && std::isfinite(exact[2]));
      within_peak += (std::max({nits[0], nits[1], nits[2]}) <= ceiling);
      direct_within_peak += (std::max({exact[0], exact[1], exact[2]}) <= test.peak * 1.0001f);
      // Inside the LUT's shaper domain only (above it the game clamps the scene too).
      if (!graded && std::max({ramp[i][0], ramp[i][1], ramp[i][2]}) < SHAPER_HIGH) {
        for (int c = 0; c < 3; ++c) {
          const float codes = std::abs(chain[i][c] - NitsToPQ(std::max(exact_sampled[c], 0.f))) * 1023.f;
          worst_codes = std::max(worst_codes, codes);
          errors.push_back(codes);
          offset_errors.push_back(std::abs(chain[i][c] - NitsToPQ(std::max(exact[c], 0.f))) * 1023.f);
        }
      }
    }
    CHECK(finite == static_cast<int>(chain.size()));
    CHECK(within_peak == static_cast<int>(chain.size()));
    CHECK(direct_within_peak == static_cast<int>(chain.size()));
    // The HDR curve's mid gray is 15 nits at Paper White 203; the SDR picture's is SdrReference's (shader's .w).
    const float mid_gray = direct[(GREY_ROW * COLOURS) * 2][3];
    if (test.mid_gray_option == 0.f) CHECK(mid_gray == 15.f / 203.f);
    const float expected = mid_gray * test.paper_white;
    const float grey_direct = LuminanceBT2020(direct[(GREY_ROW * COLOURS) * 2]);
    const float grey_sampled = LuminanceBT2020(direct_sampled[(GREY_ROW * COLOURS) * 2]);
    const Float4 grey_pixel = chain[GREY_ROW * COLOURS];
    const float grey_chain = LuminanceBT2020({PQToNits(grey_pixel[0]), PQToNits(grey_pixel[1]), PQToNits(grey_pixel[2]), 0.f});
    if (!graded) CHECK(GreyOk(grey_direct, expected, 0.01f));
    CHECK(GreyOk(grey_chain, grey_sampled, 0.03f));
    bool rising_direct = true, rising_chain = true;
    for (UINT y = 1; y < EXPOSURES; ++y) {
      rising_direct &= LuminanceBT2020(direct[(y * COLOURS) * 2]) >= LuminanceBT2020(direct[((y - 1) * COLOURS) * 2]);
      const Float4 now = chain[y * COLOURS];
      const Float4 last = chain[(y - 1) * COLOURS];
      rising_chain &= (now[0] + now[1] + now[2]) >= (last[0] + last[1] + last[2]) - 0.5f / 1023.f;
    }
    CHECK(rising_direct);
    CHECK(rising_chain);
    if (graded) {
      std::printf("2. %s LUT (gain %.2f), Peak %.0f / Paper White %.0f: grey %.2f nits direct, %.2f through the LUT (%.2f at "
                  "the colour the game's addressing samples); within Peak, finite, rising\n",
                  FRAME_NAMES[static_cast<int>(test.frame)], grey_gain, test.peak, test.paper_white, grey_direct, grey_chain,
                  grey_sampled);
      continue;
    }
    std::sort(errors.begin(), errors.end());
    std::sort(offset_errors.begin(), offset_errors.end());
    std::printf("2. Peak %.0f / Paper White %.0f, mid gray %s: grey %.2f nits direct (target %.2f), %.2f through the LUT\n"
                "   (%.2f at the colour the game's addressing samples); LUT interpolation: median %.2f, 99%% %.2f, max %.2f\n"
                "   10-bit PQ codes; with the addressing offset: median %.2f, 99%% %.2f\n",
                test.peak, test.paper_white, test.mid_gray_option == 0.f ? "HDR" : "SDR", grey_direct, expected,
                grey_chain, grey_sampled, errors[errors.size() / 2], errors[errors.size() * 99 / 100], worst_codes,
                offset_errors[offset_errors.size() / 2], offset_errors[offset_errors.size() * 99 / 100]);
  }
  // The SDR LUT (the inventory's, outputDevice 0) keeps the stock path and its alpha 1.
  const auto sdr = BuildLut(t00, DXGI_FORMAT_R10G10B10A2_UNORM, TracedConstants(Frame::INVENTORY_SDR), Injection(4.f));
  CHECK((std::bit_cast<uint32_t>(std::array<uint8_t, 4>{sdr.bytes[0], sdr.bytes[1], sdr.bytes[2], sdr.bytes[3]}) >> 30) == 3u);

  // 3. The stacked chain must fail the grey check.
  const auto stacked = Direct(direct_shader, ramp, 1u, Injection(4.f));
  const float stacked_grey = LuminanceBT2020(stacked[(GREY_ROW * COLOURS) * 2]);
  CHECK(!GreyOk(stacked_grey, 15.f, 0.01f));
  std::printf("3. stacked control (PsychoV35 on the game curve's output): grey %.2f nits, the grey check %s\n", stacked_grey,
              GreyOk(stacked_grey, 15.f, 0.01f) ? "PASSED IT (wrong)" : "rejects it");
}

// 5. Off against the game's own t00 ////////////////////////////////////////////////////////////////////////////////

static void TestOffExact(ID3D11ComputeShader* game_t00, ID3D11ComputeShader* t00) {
  int compared = 0;
  for (const Frame frame : {Frame::CITY_HDR, Frame::MENU_HDR, Frame::NEUTRAL_HDR, Frame::INVENTORY_SDR}) {
    const auto game = BuildLut(game_t00, DXGI_FORMAT_R16G16B16A16_UNORM, TracedConstants(frame), Injection(0.f));
    for (const float mode : {0.f, 1.f, 2.f, 3.f, 4.f}) {
      // An HDR LUT is the game's own in Off alone; the SDR LUT in every mode.
      if (frame != Frame::INVENTORY_SDR && mode != 0.f) continue;
      const int worst = WorstDifference(game, BuildLut(t00, DXGI_FORMAT_R16G16B16A16_UNORM, TracedConstants(frame), Injection(mode)));
      if (worst != 0) std::printf("5. %s LUT, mode %.0f: %d of 65535 from the game's bytes\n", FRAME_NAMES[static_cast<int>(frame)], mode, worst);
      CHECK(worst == 0);
      ++compared;
    }
  }
  auto assumed = TracedConstants(Frame::CITY_HDR);
  assumed[0] = {0.f, 1.f, 1.f, 1.f};
  const int control = WorstDifference(BuildLut(game_t00, DXGI_FORMAT_R16G16B16A16_UNORM, TracedConstants(Frame::CITY_HDR), Injection(0.f)),
                                      BuildLut(t00, DXGI_FORMAT_R16G16B16A16_UNORM, assumed, Injection(0.f)));
  CHECK(control > 0);
  std::printf("5. Off = the game's own t00 bit for bit on %d LUTs (city / menu / neutral HDR, inventory SDR in every mode); "
              "control: the pre-trace c0 gives %d of 65535 off the game's city LUT (%s)\n",
              compared, control, control > 0 ? "caught" : "NOT CAUGHT");
}

// 6. Vanilla's per-channel shoulder /////////////////////////////////////////////////////////////////////////////////

// Vanilla (Rendering Mode 1) through the chain, Per-Channel Highlights on and off. Peak 600 / Paper White 203 (the native
// 1000 compressed): grey (below the knee) the same, every channel within Peak, the fire column's core whiter (smallest /
// largest channel) with it on wherever the largest channel is above the knee inside the shaper domain. Peak 2500 (an
// expansion): the same bits on and off (no per-channel boost).
static std::vector<Float4> VanillaChain(ID3D11ComputeShader* t00, ID3D11PixelShader* t01, ID3D11ShaderResourceView* base,
                                        float peak, float per_channel) {
  auto injection = Injection(1.f);
  injection.peak_white_nits = peak;
  injection.vanilla_per_channel_shoulder = per_channel;
  const auto game = TracedConstants(Frame::NEUTRAL_HDR);
  const auto lut = BuildLut(t00, DXGI_FORMAT_R10G10B10A2_UNORM, game, injection);
  return ApplyLut(t01, lut, base, game, injection);
}

static void TestVanillaShoulder(ID3D11ComputeShader* t00, ID3D11PixelShader* t01, ID3D11ShaderResourceView* base,
                                const std::vector<Float4>& ramp) {
  CHECK(VanillaChain(t00, t01, base, 2500.f, 0.f) == VanillaChain(t00, t01, base, 2500.f, 1.f));
  const std::array<std::vector<Float4>, 2> outputs = {VanillaChain(t00, t01, base, 600.f, 0.f),
                                                      VanillaChain(t00, t01, base, 600.f, 1.f)};
  const float ceiling = PQToNits(NitsToPQ(600.f) + 1.5f / 1023.f);
  int within_peak = 0, whiter = 0, compared = 0;
  float whiteness_off = 0.f, whiteness_on = 0.f;
  for (UINT i = 0; i < outputs[1].size(); ++i) {
    within_peak += (std::max({PQToNits(outputs[1][i][0]), PQToNits(outputs[1][i][1]), PQToNits(outputs[1][i][2])}) <= ceiling);
  }
  for (UINT y = 0; y < EXPOSURES; ++y) {
    const UINT i = y * COLOURS + 1;  // fire
    if (std::max({ramp[i][0], ramp[i][1], ramp[i][2]}) >= SHAPER_HIGH) continue;
    const Float3 off = {PQToNits(outputs[0][i][0]), PQToNits(outputs[0][i][1]), PQToNits(outputs[0][i][2])};
    const Float3 on = {PQToNits(outputs[1][i][0]), PQToNits(outputs[1][i][1]), PQToNits(outputs[1][i][2])};
    if (std::max({off[0], off[1], off[2]}) < 120.f) continue;  // the knee: ToneMap(1) = 107 nits
    whiteness_off = std::min({off[0], off[1], off[2]}) / std::max({off[0], off[1], off[2]});
    whiteness_on = std::min({on[0], on[1], on[2]}) / std::max({on[0], on[1], on[2]});
    whiter += (whiteness_on >= whiteness_off);
    ++compared;
  }
  CHECK(within_peak == static_cast<int>(outputs[1].size()));
  CHECK(compared > 0 && whiter == compared);
  CHECK(std::memcmp(&outputs[0][GREY_ROW * COLOURS], &outputs[1][GREY_ROW * COLOURS], sizeof(Float4)) == 0);
  std::printf("6. Vanilla, Peak 600: fire above the knee whiter with Per-Channel Highlights in %d of %d exposures "
              "(brightest: smallest / largest channel %.3f off, %.3f on); grey unchanged; Peak 2500 the same bits\n",
              whiter, compared, whiteness_off, whiteness_on);
}

// 7. Exact Colour LUT (Experimental) ////////////////////////////////////////////////////////////////////////////////

// t01 with the row off gives the same pixels as before (V35's LUT and Off's); on, a modded LUT is read at its texel
// centres: grey through the LUT within 3 % of the mode's 15 nits, and the LUT's error against the direct evaluation is
// interpolation alone. Off's (the game's own) LUT is read as before either way.
static void TestExactSampling(ID3D11ComputeShader* t00, ID3D11PixelShader* t01_before, ID3D11PixelShader* t01_after,
                              ID3D11ComputeShader* direct_shader, ID3D11ShaderResourceView* base, const std::vector<Float4>& ramp) {
  const auto game = TracedConstants(Frame::NEUTRAL_HDR);
  for (const float mode : {0.f, 4.f}) {
    auto injection = Injection(mode);
    const auto lut = BuildLut(t00, DXGI_FORMAT_R10G10B10A2_UNORM, game, injection);
    CHECK(ApplyLut(t01_before, lut, base, game, injection) == ApplyLut(t01_after, lut, base, game, injection));
    injection.exact_lut_sampling = 1.f;
    const auto exact = ApplyLut(t01_after, lut, base, game, injection);
    if (mode == 0.f) {
      CHECK(exact == ApplyLut(t01_before, lut, base, game, injection));
      continue;
    }
    const auto direct = Direct(direct_shader, ramp, 0u, injection);
    std::vector<float> errors;
    for (UINT i = 0; i < exact.size(); ++i) {
      if (std::max({ramp[i][0], ramp[i][1], ramp[i][2]}) >= SHAPER_HIGH) continue;
      for (int c = 0; c < 3; ++c) errors.push_back(std::abs(exact[i][c] - NitsToPQ(std::max(direct[i * 2][c], 0.f))) * 1023.f);
    }
    std::sort(errors.begin(), errors.end());
    const Float4 grey = exact[GREY_ROW * COLOURS];
    const float grey_nits = LuminanceBT2020({PQToNits(grey[0]), PQToNits(grey[1]), PQToNits(grey[2]), 0.f});
    CHECK(GreyOk(grey_nits, 15.f, 0.03f));
    std::printf("7. Exact Colour LUT, PsychoV35 1000 / 203: grey %.2f nits through the LUT (15.00 direct); LUT vs direct: "
                "median %.2f, 99%% %.2f 10-bit PQ codes; off: the same pixels as before; Off's LUT unchanged\n",
                grey_nits, errors[errors.size() / 2], errors[errors.size() * 99 / 100]);
  }
}

// 8. Hue Clip's reference against the game's SDR LUT ///////////////////////////////////////////////////////////////

// Every texel of the game's own SDR LUT (its t00 bytes, the inventory's traced cb0, 16-bit) against SdrReference at the
// colour t00 builds the texel for (texel i: the shaper's PQ i / 31 over the SDR domain), in 8-bit sRGB codes. Mode 3 of
// the direct shader is the stand-in used before the trace, which has to miss.
static void TestSdrReference(ID3D11ComputeShader* game_t00, ID3D11ComputeShader* direct_shader) {
  const auto game = TracedConstants(Frame::INVENTORY_SDR);
  const auto lut = BuildLut(game_t00, DXGI_FORMAT_R16G16B16A16_UNORM, game, Injection(0.f));
  const float low = game[31][3];
  const float high = game[32][0];
  const auto shaper = [&](UINT i) { return low + PQToNits(i / 31.f) / 10000.f * (high - low); };
  std::vector<Float4> colours;
  for (UINT z = 0; z < LUT_SIZE; ++z) {
    for (UINT y = 0; y < LUT_SIZE; ++y) {
      for (UINT x = 0; x < LUT_SIZE; ++x) colours.push_back({shaper(x), shaper(y), shaper(z), 1.f});
    }
  }
  auto injection = Injection(4.f);
  injection.v35_mid_gray = 1.f;
  std::array<float, 2> worst = {};
  float mid_gray = 0.f;
  for (const uint32_t mode : {2u, 3u}) {
    const auto out = Direct(direct_shader, colours, mode, injection);
    std::vector<float> codes;
    for (UINT i = 0; i < colours.size(); ++i) {
      for (int c = 0; c < 3; ++c) {
        const int texel = lut.bytes[i * 8 + c * 2] | (lut.bytes[i * 8 + c * 2 + 1] << 8);
        codes.push_back(std::abs(texel / 65535.f - SrgbEncode(out[i * 2 + 1][c])) * 255.f);
      }
    }
    std::sort(codes.begin(), codes.end());
    worst[mode - 2u] = codes.back();
    mid_gray = out[1][3];  // SdrReference's in both modes
    std::printf("8. %s against the game's SDR LUT: median %.3f, 99%% %.3f, max %.3f 8-bit sRGB codes\n",
                mode == 2u ? "SdrReference (Hue Clip's reference)" : "control: the pre-trace stand-in (RenoDX ACES alone)",
                codes[codes.size() / 2], codes[codes.size() * 99 / 100], codes.back());
  }
  CHECK(worst[0] <= SDR_REFERENCE_CODES);
  CHECK(worst[1] > SDR_REFERENCE_CODES);
  std::printf("8. the SDR picture's mid gray: %.5f of white (%.2f nits at Paper White 203); the control %s\n", mid_gray,
              mid_gray * 203.f, worst[1] > SDR_REFERENCE_CODES ? "misses (caught)" : "PASSED (wrong)");
}

// 9. t01 against the game's own bytes ///////////////////////////////////////////////////////////////////////////////

static void TestT01Exact(ID3D11PixelShader* game_t01, ID3D11PixelShader* t01, ID3D11ComputeShader* game_t00,
                         ID3D11ShaderResourceView* base) {
  int compared = 0;
  for (const Frame frame : {Frame::CITY_HDR, Frame::INVENTORY_SDR}) {
    const auto game = TracedConstants(frame);
    const auto lut = BuildLut(game_t00, DXGI_FORMAT_R10G10B10A2_UNORM, game, Injection(0.f));
    for (const float mode : {0.f, 4.f}) {
      for (const float fade : {0.f, 0.5f}) {
        const bool same = (ApplyLut(game_t01, lut, base, game, Injection(mode), fade) == ApplyLut(t01, lut, base, game, Injection(mode), fade));
        if (!same) std::printf("9. %s LUT, mode %.0f, fade %.1f: pixels differ\n", FRAME_NAMES[static_cast<int>(frame)], mode, fade);
        CHECK(same);
        ++compared;
      }
    }
  }
  const auto city = TracedConstants(Frame::CITY_HDR);
  const auto lut = BuildLut(game_t00, DXGI_FORMAT_R10G10B10A2_UNORM, city, Injection(0.f));
  const bool control = (ApplyLut(game_t01, lut, base, TracedConstants(Frame::INVENTORY_SDR), Injection(0.f)) ==
                        ApplyLut(t01, lut, base, city, Injection(0.f)));
  CHECK(!control);
  std::printf("9. t01 = the game's own pass pixel for pixel on %d draws (its HDR and SDR LUTs, Off and PsychoV35, fade 0 / "
              "0.5); control: the SDR shaper domain on the HDR LUT %s\n", compared, control ? "PASSED (wrong)" : "differs (caught)");
  // The game's own chain (its t00 and t01) at the scene's 0.18, with its LUT addressing.
  constexpr std::array<Frame, 3> GREY_FRAMES = {Frame::NEUTRAL_HDR, Frame::CITY_HDR, Frame::INVENTORY_SDR};
  std::array<float, 3> grey = {};
  for (size_t index = 0; index < GREY_FRAMES.size(); ++index) {
    const auto game = TracedConstants(GREY_FRAMES[index]);
    const auto game_lut = BuildLut(game_t00, DXGI_FORMAT_R10G10B10A2_UNORM, game, Injection(0.f));
    const Float4 pixel = ApplyLut(game_t01, game_lut, base, game, Injection(0.f))[GREY_ROW * COLOURS];
    if (GREY_FRAMES[index] == Frame::INVENTORY_SDR) {
      const auto decode = [](float code) { return code <= 0.04045f ? code / 12.92f : std::pow((code + 0.055f) / 1.055f, 2.4f); };
      grey[index] = 0.2126f * decode(pixel[0]) + 0.7152f * decode(pixel[1]) + 0.0722f * decode(pixel[2]);
    } else {
      grey[index] = LuminanceBT2020({PQToNits(pixel[0]), PQToNits(pixel[1]), PQToNits(pixel[2]), 0.f});
    }
  }
  std::printf("9. the game's own chain at the scene's 0.18: HDR %.2f nits (no grade), %.2f (the city's grade); SDR %.4f of "
              "white\n", grey[0], grey[1], grey[2]);
}

// 10. PsychoV35 against the game ////////////////////////////////////////////////////////////////////////////////////

static void TestGameResponse(ID3D11ComputeShader* game_t00, ID3D11ComputeShader* t00, ID3D11PixelShader* t01,
                             ID3D11ShaderResourceView* base) {
  // BT.2020 nits over Paper White 203 as linear BT.709 (renodx's BT2020_TO_BT709_MAT) for OkLab.
  constexpr float BT2020_TO_BT709[3][3] = {{1.660491f, -0.58764114f, -0.07284986f},
                                           {-0.12455047f, 1.1328999f, -0.00834942f},
                                           {-0.01815076f, -0.1005789f, 1.11872966f}};
  const auto lab = [&](const Float4& nits) {
    Float3 bt709 = {};
    for (int row = 0; row < 3; ++row) {
      bt709[row] = (BT2020_TO_BT709[row][0] * nits[0] + BT2020_TO_BT709[row][1] * nits[1] + BT2020_TO_BT709[row][2] * nits[2]) / 203.f;
    }
    return OkLab(bt709);
  };
  const auto chain_nits = [&](ID3D11ComputeShader* shader, Frame frame, const dos2::ShaderInjectData& injection) {
    const auto game = TracedConstants(frame);
    const auto pixels = ApplyLut(t01, BuildLut(shader, DXGI_FORMAT_R10G10B10A2_UNORM, game, injection), base, game, injection);
    std::vector<Float4> nits(pixels.size());
    for (size_t i = 0; i < pixels.size(); ++i) {
      nits[i] = {PQToNits(pixels[i][0]), PQToNits(pixels[i][1]), PQToNits(pixels[i][2]), 0.f};
    }
    return nits;
  };
  const auto own_sliders = [](dos2::ShaderInjectData injection) {
    injection.v35_contrast = 1.f;
    injection.v35_shadows = 1.f;
    injection.v35_saturation = 1.f;
    injection.v35_highlight_contrast = 1.f;
    return injection;
  };
  struct Match {
    float shadows_low = 1e9f, shadows_high = 0.f;  // grey over the game's where the game is 0.05 - 1 nits
    float mid_low = 1e9f, mid_high = 0.f;          // 1 - 15 nits
    float chroma = 0.f;                            // chroma at equal lightness over the game's, the game's 1 - 30 nits
    float chroma_low = 1e9f, chroma_high = 0.f;    // per colour
  };
  const auto compare = [&](const std::vector<Float4>& game, const std::vector<Float4>& v35) {
    Match match;
    for (UINT y = 0; y < EXPOSURES; ++y) {
      const float game_grey = LuminanceBT2020(game[y * COLOURS]);
      const float ratio = LuminanceBT2020(v35[y * COLOURS]) / game_grey;
      if (game_grey >= 0.05f && game_grey <= 1.f) {
        match.shadows_low = std::min(match.shadows_low, ratio);
        match.shadows_high = std::max(match.shadows_high, ratio);
      } else if (game_grey > 1.f && game_grey <= 15.f) {
        match.mid_low = std::min(match.mid_low, ratio);
        match.mid_high = std::max(match.mid_high, ratio);
      }
    }
    double log_sum = 0.;
    int count = 0;
    for (UINT column = 1; column < COLOURS; ++column) {
      double column_sum = 0.;
      int column_count = 0;
      for (UINT y = 0; y < EXPOSURES; ++y) {
        const Float4& game_nits = game[y * COLOURS + column];
        const float game_luminance = LuminanceBT2020(game_nits);
        if (game_luminance < 1.f || game_luminance > 30.f) continue;
        const Float3 game_lab = lab(game_nits);
        // PsychoV35's chroma where its lightness is the game's (both rise with exposure).
        for (UINT j = 0; j + 1 < EXPOSURES; ++j) {
          const Float3 below = lab(v35[j * COLOURS + column]);
          const Float3 above = lab(v35[(j + 1) * COLOURS + column]);
          if (game_lab[0] < below[0] || game_lab[0] > above[0]) continue;
          const float t = (above[0] > below[0]) ? (game_lab[0] - below[0]) / (above[0] - below[0]) : 0.f;
          const float chroma = std::lerp(std::hypot(below[1], below[2]), std::hypot(above[1], above[2]), t);
          column_sum += std::log(chroma / std::hypot(game_lab[1], game_lab[2]));
          ++column_count;
          break;
        }
      }
      if (column_count == 0) continue;
      const float column_ratio = static_cast<float>(std::exp(column_sum / column_count));
      match.chroma_low = std::min(match.chroma_low, column_ratio);
      match.chroma_high = std::max(match.chroma_high, column_ratio);
      log_sum += column_sum;
      count += column_count;
    }
    match.chroma = static_cast<float>(std::exp(log_sum / std::max(count, 1)));
    return match;
  };
  const auto matches = [](const Match& match) {
    return match.shadows_low >= 0.95f && match.shadows_high <= 1.05f && match.mid_low >= 0.95f && match.mid_high <= 1.05f &&
           std::abs(match.chroma - 1.f) <= 0.05f;
  };
  for (const Frame frame : {Frame::NEUTRAL_HDR, Frame::CITY_HDR}) {
    const auto game = chain_nits(game_t00, frame, Injection(0.f));
    const Match defaults = compare(game, chain_nits(t00, frame, Injection(4.f)));
    const Match own = compare(game, chain_nits(t00, frame, own_sliders(Injection(4.f))));
    CHECK(matches(defaults));
    CHECK(!matches(own));
    std::printf("10. %s LUT, PsychoV35 against the game (Off) through the chain, 1000 / 203; Divinity 2's defaults (contrast "
                "%.2f, shadows %.2f, saturation %.2f, highlight contrast %.2f) / its own sliders (control, %s):\n"
                "    grey where the game is 0.05 - 1 nits x%.3f..x%.3f / x%.3f..x%.3f, 1 - 15 nits x%.3f..x%.3f / x%.3f..x%.3f\n"
                "    colour at equal lightness, the game's 1 - 30 nits: x%.3f (per colour x%.2f..x%.2f) / x%.3f (x%.2f..x%.2f)\n",
                FRAME_NAMES[static_cast<int>(frame)], DOS2_V35_CONTRAST, DOS2_V35_SHADOWS, DOS2_V35_SATURATION,
                DOS2_V35_HIGHLIGHT_CONTRAST, matches(own) ? "PASSED (wrong)" : "fails (caught)", defaults.shadows_low,
                defaults.shadows_high, own.shadows_low, own.shadows_high, defaults.mid_low, defaults.mid_high, own.mid_low,
                own.mid_high, defaults.chroma, defaults.chroma_low, defaults.chroma_high, own.chroma, own.chroma_low,
                own.chroma_high);
  }
  // PsychoV35's highlights: grey at +5 .. +8 stops with the defaults over its own sliders.
  for (const auto& [peak, paper_white] : {std::pair{1000.f, 203.f}, std::pair{2500.f, 275.f}}) {
    auto injection = Injection(4.f);
    injection.peak_white_nits = peak;
    injection.diffuse_white_nits = paper_white;
    const auto defaults = chain_nits(t00, Frame::NEUTRAL_HDR, injection);
    const auto own = chain_nits(t00, Frame::NEUTRAL_HDR, own_sliders(injection));
    float low = 1e9f, high = 0.f;
    for (UINT y = GREY_ROW + 10; y <= GREY_ROW + 16; ++y) {
      const float ratio = LuminanceBT2020(defaults[y * COLOURS]) / LuminanceBT2020(own[y * COLOURS]);
      low = std::min(low, ratio);
      high = std::max(high, ratio);
    }
    CHECK(low >= 0.9f && high <= 1.1f);
    std::printf("10. highlights, Peak %.0f / Paper White %.0f: grey at +5 .. +8 stops x%.3f..x%.3f of PsychoV35's own sliders\n",
                peak, paper_white, low, high);
  }
}

// 4. Hue report /////////////////////////////////////////////////////////////////////////////////////////////////////

static void ReportHue(ID3D11ComputeShader* direct_shader, const std::vector<Float4>& ramp) {
  std::printf("4. |hue difference| (OkLab degrees) of PsychoV35 against the game's SDR picture where it reaches its clip:\n"
              "   Hue Clip   fire    red    sky  green  48 hues (mean)\n");
  float best_mean = 1e9f, best_strength = 0.f;
  for (const float strength : {0.f, 0.2f, 0.4f, 0.6f, 0.8f, 1.f}) {
    auto injection = Injection(4.f);
    injection.hue_clip = strength;
    const auto out = Direct(direct_shader, ramp, 2u, injection);
    std::array<float, 53> differences = {};
    for (UINT column = 1; column < 53; ++column) {
      for (UINT y = 0; y < EXPOSURES; ++y) {
        const Float4& sdr = out[(y * COLOURS + column) * 2 + 1];
        if (std::max({sdr[0], sdr[1], sdr[2]}) < 0.999f) continue;
        const Float4& v = out[(y * COLOURS + column) * 2];
        const Float3 lab_v = OkLab({v[0], v[1], v[2]});
        const Float3 lab_s = OkLab({sdr[0], sdr[1], sdr[2]});
        float difference = std::abs(std::atan2(lab_v[2], lab_v[1]) - std::atan2(lab_s[2], lab_s[1])) * 180.f / 3.14159265f;
        differences[column] = std::min(difference, 360.f - difference);
        break;
      }
    }
    float mean = 0.f;
    for (UINT column = 5; column < 53; ++column) mean += differences[column] / 48.f;
    if (mean < best_mean) {
      best_mean = mean;
      best_strength = strength;
    }
    std::printf("   %8.0f %6.1f %6.1f %6.1f %6.1f %8.1f\n", strength * 100.f, differences[1], differences[2], differences[3],
                differences[4], mean);
  }
  std::printf("   closest to the game's SDR picture on average: Hue Clip %.0f\n", best_strength * 100.f);
}

int main(int argc, char** argv) {
  REQUIRE(argc >= 4);
  const std::filesystem::path before = argv[1];
  const std::filesystem::path after = argv[2];
  const std::filesystem::path game_shaders = argv[3];
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context)));
  const char* vertex_source =
      "struct O { float4 position : SV_Position; float2 uv : TEXCOORD0; };"
      "O main(uint id : SV_VertexID) { O o; o.uv = float2((id << 1) & 2, id & 2);"
      "o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }";
  ComPtr<ID3DBlob> vertex_code;
  REQUIRE(SUCCEEDED(D3DCompile(vertex_source, std::strlen(vertex_source), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vertex_code, nullptr)));
  REQUIRE(SUCCEEDED(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, &full_screen)));
  const D3D11_SAMPLER_DESC sampler_desc = {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP,
                                           D3D11_TEXTURE_ADDRESS_CLAMP, 0, 1, D3D11_COMPARISON_NEVER, {}, 0, D3D11_FLOAT32_MAX};
  REQUIRE(SUCCEEDED(device->CreateSamplerState(&sampler_desc, &linear_clamp)));

  const auto t00_before = LoadCompute(before / "t00.cso");
  const auto t00_after = LoadCompute(after / "t00.cso");
  const auto t01_before = LoadPixel(before / "t01.cso");
  const auto t01_after = LoadPixel(after / "t01.cso");
  const auto direct_shader = LoadCompute(after / "part6_direct.cso");
  const auto game_t00 = LoadCompute(game_shaders / "0x7016F292.cs_5_0.cso");
  const auto game_t01 = LoadPixel(game_shaders / "0xC5E7DE4B.ps_5_0.cso");
  const auto ramp = Ramp();
  const D3D11_TEXTURE2D_DESC base_desc = {COLOURS, EXPOSURES, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                          D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA base_init = {ramp.data(), COLOURS * 16, 0};
  ComPtr<ID3D11Texture2D> base_texture;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&base_desc, &base_init, &base_texture)));
  ComPtr<ID3D11ShaderResourceView> base;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(base_texture.Get(), nullptr, &base)));

  TestBits(t00_before.Get(), t00_after.Get());
  TestPsychoV35(t00_after.Get(), t01_after.Get(), direct_shader.Get(), base.Get(), ramp);
  ReportHue(direct_shader.Get(), ramp);
  TestOffExact(game_t00.Get(), t00_after.Get());
  TestVanillaShoulder(t00_after.Get(), t01_after.Get(), base.Get(), ramp);
  TestExactSampling(t00_after.Get(), t01_before.Get(), t01_after.Get(), direct_shader.Get(), base.Get(), ramp);
  TestSdrReference(game_t00.Get(), direct_shader.Get());
  TestT01Exact(game_t01.Get(), t01_after.Get(), game_t00.Get(), base.Get());
  TestGameResponse(game_t00.Get(), t00_after.Get(), t01_after.Get(), base.Get());
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}
