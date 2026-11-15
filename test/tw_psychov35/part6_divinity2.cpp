// Part 6 (Divinity 2): the add-on's LUT builder t00 (PPComputeLUT 0x7016F292, cs_5_0) and LUT pass t01 (PPApplyLUT
// 0xC5E7DE4B) on WARP, compiled by part6_divinity2.ps1 from this tree and from the commit before PsychoV35 with the
// build's flags. part6_divinity2.exe <before_cso> <after_cso> [<the game's own cso folder>]
// 1. Every mode that existed before (Off, Vanilla, Tweaked ACES, PsychoV30) and every SDR LUT (outputDevice 0, the
//    inventory's) give the same LUT bits as before, sliders at their defaults and moved, PsychoV35's sliders moved too.
// 2. PsychoV35 (Rendering Mode 4) through the real chain (t00 -> 32^3 R10G10B10A2 LUT -> t01) and evaluated directly
//    (part6_divinity2.hlsl), 64 colours x 48 exposures (0.18 x 2^-10 .. 2^13.5), Peak / Paper White 1000 / 203,
//    2500 / 203, 4000 / 275: finite; every BT.2020 channel within Peak (+ 1.5 PQ codes through the 10-bit LUT); grey 0.18
//    at 15 nits x Paper White / 203 (+-1 % direct, +-3 % through the LUT); grey never darker as it rises; the HDR LUT
//    tagged alpha 0 (t01's fixed shaper domain), the SDR LUT alpha 1. Report: the LUT's error against the direct
//    evaluation in 10-bit PQ codes (decides whether 32^3 is enough).
// 3. Control that must fail: PsychoV35 fed the game's own tone curve's output (a curve stacked on a curve) misses grey's
//    15 nits; the grey check has to say so.
// 4. Report: PsychoV35's hue (OkLab) against the SDR ACES stand-in where the stand-in reaches its clip, for fire, red, sky,
//    green and 48 hues, Hue Clip 0 to 1: where Hue Clip Emulation's default comes from until the game's SDR LUT is traced.
// 5. Report (with the game's folder): Off against the game's own t00 bytes, in 16-bit LUT units.
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

// t00's _Globals (c0 - c32): identity grading, white balance identity, the game's captured 1000-nit TsParams for the
// HDR LUT (vanilla.hlsli), 48 nits and sRGB for the SDR one.
static std::array<Float4, 33> GameConstants(bool hdr) {
  std::array<Float4, 33> c = {};
  c[0] = {0.f, 1.f, 1.f, 1.f};  // ToneMapLuminanceApply, RedCorrectionScale, HighlightsFixAmount
  for (int group = 0; group < 4; ++group) {
    for (int control = 0; control < 4; ++control) c[1 + group * 5 + control] = {1.f, 1.f, 1.f, 1.f};  // sat, contrast, gamma, gain
    c[1 + group * 5 + 4] = {0.f, 0.f, 0.f, 0.f};                                                       // offset
  }
  c[21] = {0.09f, 0.5f, 0.f, 0.f};
  for (int row = 0; row < 4; ++row) c[22 + row][row] = 1.f;
  const uint32_t output_device = hdr ? 1u : 0u;
  c[26] = {-5.600501537f, -4.f, 0.f, -1.085051537f};
  c[27] = {0.681241237f, 1.f, 2.840514660f, hdr ? 3.f : 1.681241237f};
  c[28] = {0.f, -4.f, -4.f, -3.157376528f};
  c[29] = {-0.4852499962f, 1.847732544f, 1.847732544f, -0.3328630924f};
  c[30] = {1.695345879f, 2.758124113f, 3.f, 3.f};
  c[31] = {3.f, hdr ? 0.0001f : 0.02f, hdr ? 1000.f : 48.f, SHAPER_LOW};
  c[32] = {SHAPER_HIGH, 1.f, std::bit_cast<float>(output_device), 0.f};
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
      .hue_clip = 0.8f, .v35_exposure = 1.f, .v35_highlights = 1.f, .v35_shadows = 1.f, .v35_contrast = 1.f,
      .v35_highlight_contrast = 1.f, .v35_shadow_contrast = 1.f, .v35_flare = 0.f, .v35_saturation = 1.f, .v35_dechroma = 0.f,
      .v35_highlight_saturation = 1.f, .v35_roll_off = 1.f, .v35_gamut_compression = 1.f,
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

static std::vector<Float4> ApplyLut(ID3D11PixelShader* shader, const Lut& lut, ID3D11ShaderResourceView* base,
                                    const dos2::ShaderInjectData& injection) {
  context->ClearState();
  const Float4 globals = {SHAPER_LOW, SHAPER_HIGH, 0.f, 0.f};  // Params, FadeValue
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

// part6_divinity2.hlsl: two outputs per input (see its header).
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
  const std::array<uint32_t, 4> direct = {mode, 0, 0, 0};
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

// 1. Bits ///////////////////////////////////////////////////////////////////////////////////////////////////////////

static void TestBits(ID3D11ComputeShader* before, ID3D11ComputeShader* after) {
  int compared = 0;
  for (const bool hdr : {true, false}) {
    for (const float mode : {0.f, 1.f, 2.f, 3.f, 4.f}) {
      // PsychoV35's HDR LUT is new: only its SDR LUT (the stock path) must be unchanged.
      if (hdr && mode == 4.f) continue;
      for (const bool moved : {false, true}) {
        auto injection = Injection(mode);
        if (moved) MoveSliders(&injection);
        const auto a = BuildLut(before, DXGI_FORMAT_R16G16B16A16_UNORM, GameConstants(hdr), injection);
        const auto b = BuildLut(after, DXGI_FORMAT_R16G16B16A16_UNORM, GameConstants(hdr), injection);
        const bool same = (a.bytes == b.bytes);
        if (!same) std::printf("1. %s LUT, mode %.0f, sliders %s: bits differ\n", hdr ? "HDR" : "SDR", mode, moved ? "moved" : "default");
        CHECK(same);
        ++compared;
      }
    }
  }
  std::printf("1. %d LUTs compared with the commit before PsychoV35 (modes 0-3 HDR and SDR, mode 4 SDR)\n", compared);
}

// 2. / 3. PsychoV35 ////////////////////////////////////////////////////////////////////////////////////////////////

struct Display {
  float peak;
  float paper_white;
  float mid_gray_option;  // v35_mid_gray: 0 the game's HDR curve, 1 its SDR picture
};

static void TestPsychoV35(ID3D11ComputeShader* t00, ID3D11PixelShader* t01, ID3D11ComputeShader* direct_shader,
                          ID3D11ShaderResourceView* base, const std::vector<Float4>& ramp) {
  for (const Display display : {Display{1000.f, 203.f, 0.f}, Display{2500.f, 203.f, 0.f}, Display{4000.f, 275.f, 0.f},
                                Display{1000.f, 203.f, 1.f}}) {
    auto injection = Injection(4.f);
    injection.peak_white_nits = display.peak;
    injection.diffuse_white_nits = display.paper_white;
    injection.v35_mid_gray = display.mid_gray_option;
    const auto lut = BuildLut(t00, DXGI_FORMAT_R10G10B10A2_UNORM, GameConstants(true), injection);
    CHECK((std::bit_cast<uint32_t>(std::array<uint8_t, 4>{lut.bytes[0], lut.bytes[1], lut.bytes[2], lut.bytes[3]}) >> 30) == 0u);
    const auto chain = ApplyLut(t01, lut, base, injection);
    const auto direct = Direct(direct_shader, ramp, 0u, injection);
    // The game's addressing: t00 writes texel i for the shaper's PQ i / 31, t01 samples the PQ code u straight (no
    // half-texel remap), so a sample reads the colour at PQ (32 u - 0.5) / 31. V35 evaluated at that colour separates the
    // LUT's interpolation error from this offset (the game's own, every mode, Off too).
    std::vector<Float4> sampled_ramp = ramp;
    const float shaper_scale = 10000.f / (SHAPER_HIGH - SHAPER_LOW);
    const float shaper_bias = -shaper_scale * SHAPER_LOW;
    for (auto& texel : sampled_ramp) {
      for (int c = 0; c < 3; ++c) {
        const float u = NitsToPQ(texel[c] * shaper_scale + shaper_bias);
        const float sampled = std::clamp((u * LUT_SIZE - 0.5f) / (LUT_SIZE - 1), 0.f, 1.f);
        texel[c] = (PQToNits(sampled) - shaper_bias) / shaper_scale;
      }
    }
    const auto direct_sampled = Direct(direct_shader, sampled_ramp, 0u, injection);
    const float ceiling = PQToNits(NitsToPQ(display.peak) + 1.5f / 1023.f);
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
      direct_within_peak += (std::max({exact[0], exact[1], exact[2]}) <= display.peak * 1.0001f);
      // Inside the LUT's shaper domain only (above it the game clamps the scene too).
      if (std::max({ramp[i][0], ramp[i][1], ramp[i][2]}) < SHAPER_HIGH) {
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
    // The HDR curve's mid gray is 15 nits at Paper White 203; the SDR picture's is the stand-in's 0.18 (shader's .w).
    const float mid_gray = direct[(GREY_ROW * COLOURS) * 2][3];
    if (display.mid_gray_option == 0.f) CHECK(mid_gray == 15.f / 203.f);
    const float expected = mid_gray * display.paper_white;
    const float grey_direct = LuminanceBT2020(direct[(GREY_ROW * COLOURS) * 2]);
    const float grey_sampled = LuminanceBT2020(direct_sampled[(GREY_ROW * COLOURS) * 2]);
    const Float4 grey_pixel = chain[GREY_ROW * COLOURS];
    const float grey_chain = LuminanceBT2020({PQToNits(grey_pixel[0]), PQToNits(grey_pixel[1]), PQToNits(grey_pixel[2]), 0.f});
    CHECK(GreyOk(grey_direct, expected, 0.01f));
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
    std::sort(errors.begin(), errors.end());
    std::sort(offset_errors.begin(), offset_errors.end());
    std::printf("2. Peak %.0f / Paper White %.0f, mid gray %s: grey %.2f nits direct (target %.2f), %.2f through the LUT\n"
                "   (%.2f at the colour the game's addressing samples); LUT interpolation: median %.2f, 99%% %.2f, max %.2f\n"
                "   10-bit PQ codes; with the addressing offset: median %.2f, 99%% %.2f\n",
                display.peak, display.paper_white, display.mid_gray_option == 0.f ? "HDR" : "SDR", grey_direct, expected,
                grey_chain, grey_sampled, errors[errors.size() / 2], errors[errors.size() * 99 / 100], worst_codes,
                offset_errors[offset_errors.size() / 2], offset_errors[offset_errors.size() * 99 / 100]);
  }
  // The SDR LUT (the inventory's, outputDevice 0) keeps the stock path and its alpha 1.
  const auto sdr = BuildLut(t00, DXGI_FORMAT_R10G10B10A2_UNORM, GameConstants(false), Injection(4.f));
  CHECK((std::bit_cast<uint32_t>(std::array<uint8_t, 4>{sdr.bytes[0], sdr.bytes[1], sdr.bytes[2], sdr.bytes[3]}) >> 30) == 3u);

  // 3. The stacked chain must fail the grey check.
  const auto stacked = Direct(direct_shader, ramp, 1u, Injection(4.f));
  const float stacked_grey = LuminanceBT2020(stacked[(GREY_ROW * COLOURS) * 2]);
  CHECK(!GreyOk(stacked_grey, 15.f, 0.01f));
  std::printf("3. stacked control (PsychoV35 on the game curve's output): grey %.2f nits, the grey check %s\n", stacked_grey,
              GreyOk(stacked_grey, 15.f, 0.01f) ? "PASSED IT (wrong)" : "rejects it");
}

// 4. Hue report /////////////////////////////////////////////////////////////////////////////////////////////////////

static void ReportHue(ID3D11ComputeShader* direct_shader, const std::vector<Float4>& ramp) {
  std::printf("4. |hue difference| (OkLab degrees) of PsychoV35 against the SDR ACES stand-in where it reaches its clip:\n"
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
  std::printf("   closest to the stand-in on average: Hue Clip %.0f (level C until the game's SDR LUT is traced)\n", best_strength * 100.f);
}

int main(int argc, char** argv) {
  REQUIRE(argc >= 3);
  const std::filesystem::path before = argv[1];
  const std::filesystem::path after = argv[2];
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
  const auto t01_after = LoadPixel(after / "t01.cso");
  const auto direct_shader = LoadCompute(after / "part6_direct.cso");
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
  if (argc >= 4) {
    // 5. Off against the game's own bytes (the decompile is not bit-exact by design: report only).
    const auto game = LoadCompute(std::filesystem::path(argv[3]) / "0x7016F292.cs_5_0.cso");
    for (const bool hdr : {true, false}) {
      const auto a = BuildLut(game.Get(), DXGI_FORMAT_R16G16B16A16_UNORM, GameConstants(hdr), Injection(0.f));
      const auto b = BuildLut(t00_after.Get(), DXGI_FORMAT_R16G16B16A16_UNORM, GameConstants(hdr), Injection(0.f));
      int worst = 0;
      for (size_t i = 0; i + 1 < a.bytes.size(); i += 2) {
        const int x = a.bytes[i] | (a.bytes[i + 1] << 8);
        const int y = b.bytes[i] | (b.bytes[i + 1] << 8);
        worst = std::max(worst, std::abs(x - y));
      }
      std::printf("5. Off, %s LUT: the add-on's t00 against the game's own bytes: max %d of 65535\n", hdr ? "HDR" : "SDR", worst);
    }
  }
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}
