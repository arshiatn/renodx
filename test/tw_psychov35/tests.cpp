// PsychoV35 and Hue Clip on WARP: Attila's tone mappers (t00 - t10: tone_mapper.hlsli's Hue Clip, hdr_to_screen.hlsli's
// PsychoV35, Rendering Mode 3) and WH3's t00 (Rendering Mode 4), compiled by run.ps1 from this tree and from d3d6425b with
// the build's flags. tests.exe <before_cso> <after_cso>. PsychoV35: Musa's (twattila/psycho_test35.hlsli), its own v35_*
// sliders.
// 1. Every other mode gives the same bits as d3d6425b: Attila SDR / PsychoV30 / PsychoV31, WH3 SDR / Extended /
//    PsychoV30 / PsychoV31, at HDR10, scRGB and SDR output, sliders at their defaults and moved, Hue Clip at 0.7 and
//    PsychoV35's sliders moved (which must do nothing there). 64 colours (every hue, greys, negative channels, black) x
//    48 brightness steps.
// 2. Hue Clip alone (twattila/psycho35.hlsli's HueClip, every game's): fire orange and sky blue move their middle
//    channel toward the clip's place, the smallest and largest channel keep their ratio, BT.709 luminance is kept; grey
//    and strength 0 unchanged; a negative channel keeps its sign; a dark colour (far below the clip) about unchanged.
// 3. PsychoV35 through the real chain (Attila t00 -> t01, WH3 t00), an exposure ramp of 64 colours, 2^-8 to 2^12 of the
//    game's white, at 203 / 1000 and 203 / 400 nits and SDR output, every V35 slider at its ends: finite; BT.2020 within Peak
//    (SDR: BT.709 within 1) with Gamut Compression 1, luminance within Peak always (Gamut Compression 0: within 1%);
//    grey's ramp never darker as it rises.
// 4. Report: PsychoV35's hue (OkLab) against the game's own SDR picture (the same chain in its SDR mode) where SDR
//    reaches its clip, for fire, sky blue and 48 hues, Hue Clip 0 to 1 (sky to 2): what the default comes from.
// 5. The same for the other games' kinds of stock curve (hue_curves.hlsl: a luminance curve then each channel clipped,
//    or a curve on each channel), PsychoV35 right after Hue Clip as their shaders call it. Its WH3 form agrees with part
//    4's WH3 (1.00); WH2's own t00 (curve 6: its anchored handoff, at burns 0.1, 0.3 and 0.5) too (1.00); WH1's tone
//    mappers (curve 7: Attila's engine Hue Clip without the highlight limit, the stock whiteout off and 0 to 10); Pharaoh's
//    t00 (curve 8: its lift, black and mid-gray handoff at burn 0.2 and 0).
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
#include <functional>
#include <iterator>
#include <map>
#include <numbers>
#include <string>
#include <tuple>
#include <vector>

namespace attila {
#include "src/games/twattila/shared.h"
}  // namespace attila
#undef SRC_TEMPLATE_SHARED_H_
namespace wh3 {
#include "src/games/twwarhammer3/shared.h"
}  // namespace wh3

using Microsoft::WRL::ComPtr;
using Float3 = std::array<float, 3>;
using Float4 = std::array<float, 4>;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

constexpr UINT WIDTH = 64;
constexpr UINT HEIGHT = 48;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static ComPtr<ID3D11VertexShader> full_screen;
static ComPtr<ID3D11SamplerState> linear_clamp;

struct Shader {
  std::vector<char> code;
  ComPtr<ID3D11PixelShader> pixel;
  ComPtr<ID3D11ComputeShader> compute;
  ComPtr<ID3D11ShaderReflection> reflection;
};

static std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static Shader LoadPixelShader(const std::filesystem::path& path) {
  Shader shader{.code = ReadFile(path)};
  REQUIRE(!shader.code.empty());
  REQUIRE(SUCCEEDED(device->CreatePixelShader(shader.code.data(), shader.code.size(), nullptr, &shader.pixel)));
  REQUIRE(SUCCEEDED(D3DReflect(shader.code.data(), shader.code.size(), IID_PPV_ARGS(&shader.reflection))));
  return shader;
}

static ComPtr<ID3D11ShaderResourceView> Texture2D(UINT width, UINT height, const std::vector<Float4>& texels) {
  const D3D11_TEXTURE2D_DESC desc = {width, height, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                     D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA init = {texels.data(), width * 16, 0};
  ComPtr<ID3D11Texture2D> texture;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, &init, &texture)));
  ComPtr<ID3D11ShaderResourceView> view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)));
  return view;
}

// A 4-sample texture of one value (the MSAA tone mappers' depth).
static ComPtr<ID3D11ShaderResourceView> MultisampledTexture(const Float4& value) {
  const D3D11_TEXTURE2D_DESC desc = {WIDTH, HEIGHT, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {4, 0}, D3D11_USAGE_DEFAULT,
                                     D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0};
  ComPtr<ID3D11Texture2D> texture;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)));
  ComPtr<ID3D11RenderTargetView> target;
  REQUIRE(SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &target)));
  context->ClearRenderTargetView(target.Get(), value.data());
  ComPtr<ID3D11ShaderResourceView> view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)));
  return view;
}

// The LUTs' identity for the game's addressing (a coordinate of 0 - 1 straight into SampleLevel): texel i holds
// (i + 0.5) / size, so a sample at c returns c (clamped within half a texel of the ends).
static ComPtr<ID3D11ShaderResourceView> IdentityLut3D(UINT size) {
  std::vector<Float4> texels;
  for (UINT z = 0; z < size; ++z) {
    for (UINT y = 0; y < size; ++y) {
      for (UINT x = 0; x < size; ++x) {
        texels.push_back({(x + 0.5f) / size, (y + 0.5f) / size, (z + 0.5f) / size, 1.f});
      }
    }
  }
  const D3D11_TEXTURE3D_DESC desc = {size, size, size, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_USAGE_DEFAULT,
                                     D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA init = {texels.data(), size * 16, size * size * 16};
  ComPtr<ID3D11Texture3D> texture;
  REQUIRE(SUCCEEDED(device->CreateTexture3D(&desc, &init, &texture)));
  ComPtr<ID3D11ShaderResourceView> view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)));
  return view;
}

template <typename T>
static ComPtr<ID3D11ShaderResourceView> Structured(const std::vector<T>& elements) {
  const D3D11_BUFFER_DESC desc = {static_cast<UINT>(elements.size() * sizeof(T)), D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                                  D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(T)};
  const D3D11_SUBRESOURCE_DATA init = {elements.data(), 0, 0};
  ComPtr<ID3D11Buffer> buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &init, &buffer)));
  ComPtr<ID3D11ShaderResourceView> view;
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(buffer.Get(), nullptr, &view)));
  return view;
}

using Words = std::vector<uint32_t>;
static Words F(std::initializer_list<float> values) {
  Words words;
  for (const float value : values) words.push_back(std::bit_cast<uint32_t>(value));
  return words;
}

// What a draw binds, by the names in the shader's reflection: views, cbuffer variables (32-bit words from the
// variable's start, arrays with a register per element), and the injection's bytes for `shader_injection`.
struct Bindings {
  std::map<std::string, ComPtr<ID3D11ShaderResourceView>> views;
  std::map<std::string, Words> words;
  std::vector<uint8_t> injection;
};

template <typename T>
static std::vector<uint8_t> Bytes(const T& value) {
  std::vector<uint8_t> bytes(sizeof(T));
  std::memcpy(bytes.data(), &value, sizeof(T));
  return bytes;
}

static std::vector<Float4> Draw(const Shader& shader, const Bindings& bindings) {
  context->ClearState();
  D3D11_SHADER_DESC desc = {};
  shader.reflection->GetDesc(&desc);
  std::vector<ComPtr<ID3D11Buffer>> buffers;
  for (UINT i = 0; i < desc.BoundResources; ++i) {
    D3D11_SHADER_INPUT_BIND_DESC bind = {};
    shader.reflection->GetResourceBindingDesc(i, &bind);
    const std::string name = bind.Name;
    if (bind.Type == D3D_SIT_CBUFFER) {
      auto* constants = shader.reflection->GetConstantBufferByName(bind.Name);
      D3D11_SHADER_BUFFER_DESC constants_desc = {};
      constants->GetDesc(&constants_desc);
      std::vector<uint8_t> data((constants_desc.Size + 15u) & ~15u, 0);
      if (name == "shader_injection") {
        std::memcpy(data.data(), bindings.injection.data(), std::min(data.size(), bindings.injection.size()));
      } else {
        for (UINT v = 0; v < constants_desc.Variables; ++v) {
          D3D11_SHADER_VARIABLE_DESC variable = {};
          constants->GetVariableByIndex(v)->GetDesc(&variable);
          if (const auto found = bindings.words.find(variable.Name); found != bindings.words.end()) {
            std::memcpy(data.data() + variable.StartOffset, found->second.data(),
                        std::min<size_t>(variable.Size, found->second.size() * 4));
          }
        }
      }
      const D3D11_BUFFER_DESC buffer_desc = {static_cast<UINT>(data.size()), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
      const D3D11_SUBRESOURCE_DATA init = {data.data(), 0, 0};
      ComPtr<ID3D11Buffer> buffer;
      REQUIRE(SUCCEEDED(device->CreateBuffer(&buffer_desc, &init, &buffer)));
      context->PSSetConstantBuffers(bind.BindPoint, 1, buffer.GetAddressOf());
      context->CSSetConstantBuffers(bind.BindPoint, 1, buffer.GetAddressOf());
      buffers.push_back(buffer);
    } else if (bind.Type == D3D_SIT_TEXTURE || bind.Type == D3D_SIT_STRUCTURED) {
      const auto found = bindings.views.find(name);
      if (found == bindings.views.end()) {
        std::printf("no view for %s\n", name.c_str());
        std::exit(1);
      }
      context->PSSetShaderResources(bind.BindPoint, 1, found->second.GetAddressOf());
    } else if (bind.Type == D3D_SIT_SAMPLER) {
      context->PSSetSamplers(bind.BindPoint, 1, linear_clamp.GetAddressOf());
    }
  }
  const D3D11_TEXTURE2D_DESC target_desc = {WIDTH, HEIGHT, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                            D3D11_BIND_RENDER_TARGET, 0, 0};
  ComPtr<ID3D11Texture2D> target;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&target_desc, nullptr, &target)));
  ComPtr<ID3D11RenderTargetView> target_view;
  REQUIRE(SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, &target_view)));
  const D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(WIDTH), static_cast<float>(HEIGHT), 0.f, 1.f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(full_screen.Get(), nullptr, 0);
  context->PSSetShader(shader.pixel.Get(), nullptr, 0);
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
  std::vector<Float4> pixels(WIDTH * HEIGHT);
  for (UINT y = 0; y < HEIGHT; ++y) {
    std::memcpy(&pixels[y * WIDTH], static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, WIDTH * 16);
  }
  context->Unmap(staging.Get(), 0);
  return pixels;
}

// Colours ////////////////////////////////////////////////////////////////////////////////////////////////////////////

// RenoDX's BT.709 Y (renodx::color::y::from::BT709).
static float Luminance(const Float3& c) { return 0.2126390059f * c[0] + 0.7151686788f * c[1] + 0.0721923154f * c[2]; }

static Float3 Hue(float degrees, float saturation) {
  Float3 rgb;
  for (int i = 0; i < 3; ++i) {
    const float k = std::fmod(degrees / 60.f + (i == 0 ? 5.f : (i == 1 ? 3.f : 1.f)), 6.f);
    rgb[i] = 1.f - std::max(0.f, std::min({k, 4.f - k, 1.f}));
  }
  for (float& channel : rgb) channel = 1.f - saturation * (1.f - channel);
  return rgb;
}

// The 64 columns of the bit comparison: 12 hues x 4 saturations, greys, negative channels, fire, sky, black.
static std::vector<Float3> PatternColumns() {
  std::vector<Float3> columns;
  for (int h = 0; h < 12; ++h) {
    for (const float s : {0.3f, 0.7f, 0.95f, 1.f}) columns.push_back(Hue(h * 30.f, s));
  }
  for (const float g : {1.f, 0.5f, 0.25f, 2.f}) columns.push_back({g, g, g});
  for (const Float3 c : {Float3{1.f, 0.5f, -0.05f}, Float3{-0.02f, 0.6f, 1.f}, Float3{0.8f, -0.1f, 0.2f}, Float3{1.2f, 0.9f, -0.2f}}) columns.push_back(c);
  for (const Float3 c : {Float3{1.f, 0.35f, 0.08f}, Float3{1.f, 0.5f, 0.1f}, Float3{1.f, 0.25f, 0.03f}, Float3{1.f, 0.6f, 0.2f}}) columns.push_back(c);
  for (const Float3 c : {Float3{0.3f, 0.55f, 1.f}, Float3{0.45f, 0.7f, 1.f}, Float3{0.f, 0.f, 0.f}, Float3{1e-7f, 2e-7f, 0.f}}) columns.push_back(c);
  return columns;
}

// Scene: column colour x 2^(row step), luminance-normalised so a row is one brightness for every column.
static std::vector<Float4> Scene(const std::vector<Float3>& columns, float first_stop, float stops_per_row, float white) {
  std::vector<Float4> texels(WIDTH * HEIGHT);
  for (UINT y = 0; y < HEIGHT; ++y) {
    const float brightness = white * std::exp2(first_stop + stops_per_row * y);
    for (UINT x = 0; x < WIDTH; ++x) {
      const Float3& c = columns[x % columns.size()];
      const float y_of_c = Luminance(c);
      const float scale = (y_of_c > 1e-6f ? brightness / y_of_c : brightness);
      texels[y * WIDTH + x] = {c[0] * scale, c[1] * scale, c[2] * scale, 1.f};
    }
  }
  return texels;
}

// The game's tone mappers' inputs ///////////////////////////////////////////////////////////////////////////////////

static Words ScreenSize() { return F({WIDTH, HEIGHT, 1.f / WIDTH, 1.f / HEIGHT}); }
static Words Identity4x4() { return F({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}); }
// A mild grade for the bit comparison (the colour matrix: rows of output weights, the last row an offset).
static Words GradeMatrix() { return F({1.05f, 0.02f, -0.03f, 0, -0.04f, 0.97f, 0.05f, 0, 0.02f, -0.01f, 0.92f, 0, 0.01f, 0.f, 0.015f, 1}); }

// Attila: identity or an S-shaped S-curve per channel (256 x 1, the game's addressing, as the LUTs above).
static ComPtr<ID3D11ShaderResourceView> AttilaSCurve(bool identity) {
  std::vector<Float4> texels(256);
  for (int i = 0; i < 256; ++i) {
    const float x = (i + 0.5f) / 256.f;
    const float s = x * x * (3.f - 2.f * x);
    texels[i] = identity ? Float4{x, x, x, 1.f} : Float4{0.6f * x + 0.4f * s, 0.5f * x + 0.5f * s, 0.7f * x + 0.3f * s, 1.f};
  }
  return Texture2D(256, 1, texels);
}

static attila::ShaderInjectData AttilaInjection(float mode, float encoding) {
  attila::ShaderInjectData si = {};
  si.peak_white_nits = 1000.f;
  si.diffuse_white_nits = 203.f;
  si.graphics_white_nits = 203.f;
  si.gamma_correction = 1.f;
  si.swap_chain_encoding = encoding;
  si.hdr = mode;
  si.bloom = 1.f;
  si.highlights = 1.f;
  si.shadows = 1.f;
  si.cone_response_exponent = 1.f;
  si.contrast = 1.f;
  si.purity_scale = 1.f;
  si.hue_shift = 1.f;
  si.lensflare = 1.f;
  si.uvdistort = 1.f;
  si.dof = 1.f;
  si.vignette = 1.f;
  si.v31_exposure = 1.f;
  si.v31_highlights = 1.f;
  si.v31_shadows = 1.f;
  si.v31_contrast = 1.f;
  si.v31_highlight_contrast = 1.f;
  si.v31_shadow_contrast = 1.f;
  si.v31_saturation = 1.f;
  si.v31_highlight_saturation = 0.7f;
  si.v31_roll_off = 1.f;
  si.v31_gamut_compression = 1.f;
  si.v31_gamut_compression_mode = 1.f;
  si.v31_highlight_hue_shift = 0.04f;
  si.v35_exposure = 1.f;
  si.v35_highlights = 1.f;
  si.v35_shadows = 1.f;
  si.v35_contrast = 1.f;
  si.v35_highlight_contrast = 1.f;
  si.v35_shadow_contrast = 1.f;
  si.v35_saturation = 1.f;
  si.v35_highlight_saturation = 1.f;
  si.v35_roll_off = 1.f;
  si.v35_gamut_compression = 1.f;
  si.effects_share = 1.f;
  si.sky_smooth_strength = 1.f;
  si.fog_strength = 1.f;
  si.hue_clip = 0.7f;
  return si;
}

static wh3::ShaderInjectData Wh3Injection(float mode, float encoding) {
  wh3::ShaderInjectData si = {};
  si.peak_white_nits = 1000.f;
  si.diffuse_white_nits = 203.f;
  si.graphics_white_nits = 203.f;
  si.gamma_correction = 1.f;
  si.swap_chain_encoding = encoding;
  si.uvdistort = 1.f;
  si.hdr = mode;
  si.bloom = 1.f;
  si.vignette = 1.f;
  si.exposure_tpm = 1.f;
  si.highlights_tpm = 1.f;
  si.shadows_tpm = 1.f;
  si.contrast_tpm = 1.f;
  si.saturation_tpm = 1.f;
  si.exposure = 1.f;
  si.highlights = 0.7f;
  si.shadows = 1.f;
  si.contrast = 1.f;
  si.saturation = 1.f;
  si.cone_response_exponent = 1.f;
  si.lensflare = 1.f;
  si.sharpening = 1.f;
  si.render_scale_x = 1.f;
  si.render_scale_y = 1.f;
  si.dof = 1.f;
  si.hue_shift = 1.f;
  si.v31_exposure = 1.f;
  si.v31_highlights = 1.f;
  si.v31_shadows = 1.f;
  si.v31_contrast = 1.f;
  si.v31_highlight_contrast = 1.f;
  si.v31_shadow_contrast = 1.f;
  si.v31_saturation = 1.f;
  si.v31_highlight_saturation = 1.f;
  si.v31_roll_off = 1.f;
  si.v31_gamut_compression = 1.f;
  si.v31_gamut_compression_mode = 1.f;
  si.v31_highlight_hue_shift = 0.15f;
  si.v35_exposure = 1.f;
  si.v35_highlights = 1.f;
  si.v35_shadows = 1.f;
  si.v35_contrast = 1.f;
  si.v35_highlight_contrast = 1.f;
  si.v35_shadow_contrast = 1.f;
  si.v35_saturation = 1.f;
  si.v35_highlight_saturation = 1.f;
  si.v35_roll_off = 1.f;
  si.v35_gamut_compression = 1.f;
  si.fog_strength = 1.f;
  si.shadow_map_scale = 1.f;
  si.hue_clip = 0.7f;
  return si;
}

// Attila's t00 / t09 / t10 (black and white points 0 / 1 unless given) and t01 - t08.
static Bindings AttilaToneMapper(const std::vector<Float4>& scene, bool identity_curve, float black = 0.f, float white = 1.f) {
  Bindings b;
  b.views["g_hdr_rgb_texture_sampler"] = Texture2D(WIDTH, HEIGHT, scene);
  b.views["g_hdr_rgb_bloom_texture_sampler"] = Texture2D(1, 1, {Float4{}});
  b.views["g_black_and_white_points_sampler"] = Texture2D(1, 1, {Float4{black, white, 0.f, 0.f}});
  b.views["g_scurve_texture_sampler"] = AttilaSCurve(identity_curve);
  b.words["g_screen_size"] = ScreenSize();
  b.words["g_viewport_dimensions"] = ScreenSize();
  b.words["g_render_target_dimensions"] = ScreenSize();
  b.words["g_luma_black_point"] = F({black});
  b.words["g_luma_white_point"] = F({white});
  b.words["g_luma_high_pass_threshold"] = F({0.8f});
  return b;
}

static Bindings AttilaHdrToScreen(const std::vector<Float4>& frame, bool identity_grade) {
  Bindings b;
  b.views["frame_sampler"] = Texture2D(WIDTH, HEIGHT, frame);
  b.views["gbuffer_channel_4_sampler"] = Texture2D(WIDTH, HEIGHT, std::vector<Float4>(WIDTH * HEIGHT, Float4{0.5f, 0.5f, 0.5f, 1.f}));
  b.views["gbuffer_channel_4_texture_ms"] = MultisampledTexture({0.5f, 0.5f, 0.5f, 1.f});
  b.views["s_fog_of_war_mask"] = Texture2D(1, 1, {Float4{1.f, 1.f, 1.f, 1.f}});
  b.views["distortion_sampler"] = Texture2D(WIDTH, HEIGHT, std::vector<Float4>(WIDTH * HEIGHT, Float4{}));
  b.words["g_screen_size"] = ScreenSize();
  b.words["g_viewport_dimensions"] = ScreenSize();
  b.words["g_render_target_dimensions"] = ScreenSize();
  b.words["inv_view_projection"] = Identity4x4();
  b.words["colour_matrix"] = identity_grade ? Identity4x4() : GradeMatrix();
  b.words["g_brightness"] = F({1.f});
  b.words["g_gamma_output"] = F({2.f});
  b.words["g_inv_gamma_output"] = F({0.5f});
  return b;
}

static Bindings Wh3ToneMapper(const std::vector<Float4>& scene, bool identity_grade, float brightness, float burn) {
  Bindings b;
  b.views["g_hdr_rgb_texture"] = Texture2D(WIDTH, HEIGHT, scene);
  b.views["g_hdr_rgb_bloom_texture"] = Texture2D(1, 1, {Float4{}});
  b.views["distortion_texture"] = Texture2D(WIDTH, HEIGHT, std::vector<Float4>(WIDTH * HEIGHT, Float4{}));
  b.views["gbuffer_channel_3_texture"] = Texture2D(WIDTH, HEIGHT, std::vector<Float4>(WIDTH * HEIGHT, Float4{}));
  b.views["gbuffer_channel_4_texture"] = Texture2D(WIDTH, HEIGHT, std::vector<Float4>(WIDTH * HEIGHT, Float4{}));
  const auto lut = IdentityLut3D(32);
  for (const char* name : {"lut_texture_1", "lut_texture_2", "lut_texture_3", "lut_secondary_texture_1", "lut_secondary_texture_2",
                           "lut_secondary_texture_3"}) {
    b.views[name] = lut;
  }
  b.views["g_auto_exposure_input_buffer"] = Structured(std::vector<float>{brightness});
  b.words["g_screen_size"] = ScreenSize();
  b.words["g_brightness"] = F({1.f});
  b.words["g_gamma_output"] = F({2.2f});
  b.words["g_inv_gamma_output"] = F({1.f / 2.2f});
  b.words["g_tone_mapping_brightness"] = F({brightness});
  b.words["g_tone_mapping_burn"] = F({burn});
  b.words["g_use_auto_exposure"] = {1u};
  b.words["colour_matrix"] = identity_grade ? Identity4x4() : GradeMatrix();
  b.words["lut_weights"] = identity_grade ? F({1, 0, 0, 0, 0, 0, 0, 0, 0}) : F({0.5f, 0, 0, 0, 0.3f, 0, 0, 0, 0.2f});
  b.words["overscan"] = F({1.f});
  return b;
}

// The tone mappers' outputs as linear BT.709 relative to the UI's white: Attila 2.0-encoded, WH3 2.2.
static Float3 Decode(const Float4& o, float gamma) {
  Float3 c;
  for (int i = 0; i < 3; ++i) c[i] = std::copysign(std::pow(std::abs(o[i]), gamma), o[i]);
  return c;
}

static Float3 Bt709ToBt2020(const Float3& c) {
  return {0.6274040f * c[0] + 0.3292820f * c[1] + 0.0433136f * c[2], 0.0690970f * c[0] + 0.9195400f * c[1] + 0.0113612f * c[2],
          0.0163916f * c[0] + 0.0880132f * c[1] + 0.8955950f * c[2]};
}

// OkLab (Ottosson) from linear BT.709: {L, a, b}.
static Float3 OkLab(const Float3& c) {
  const float l = 0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2];
  const float m = 0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2];
  const float s = 0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2];
  const float l3 = std::cbrt(l), m3 = std::cbrt(m), s3 = std::cbrt(s);
  return {0.2104542553f * l3 + 0.7936177850f * m3 - 0.0040720468f * s3, 1.9779984951f * l3 - 2.4285922050f * m3 + 0.4505937099f * s3,
          0.0259040371f * l3 + 0.7827717662f * m3 - 0.8086757660f * s3};
}

// 1. Bits //////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Pair {
  Shader before;
  Shader after;
};

static int CompareBits(const char* name, const Pair& pair, const Bindings& bindings) {
  const auto a = Draw(pair.before, bindings);
  const auto b = Draw(pair.after, bindings);
  int different = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::memcmp(&a[i], &b[i], sizeof(Float4)) != 0) {
      if (different == 0) {
        std::printf("  %s pixel %zu: %.9g %.9g %.9g vs %.9g %.9g %.9g\n", name, i, a[i][0], a[i][1], a[i][2], b[i][0], b[i][1], b[i][2]);
      }
      ++different;
    }
  }
  return different;
}

// Slider sets for the bit comparison: defaults, then everything moved (V30 and V31 alike).
static void MoveAttilaSliders(attila::ShaderInjectData* si, int set) {
  if (set == 0) return;
  si->highlights = 1.3f;
  si->shadows = 0.8f;
  si->contrast = 1.15f;
  si->purity_scale = 1.25f;
  si->cone_response_exponent = 0.9f;
  si->hue_shift = 1.6f;
  si->v31_exposure = 1.4f;
  si->v31_highlights = 1.2f;
  si->v31_shadows = 0.85f;
  si->v31_contrast = 1.1f;
  si->v31_highlight_contrast = 1.2f;
  si->v31_shadow_contrast = 0.9f;
  si->v31_flare = 0.002f;
  si->v31_saturation = 1.2f;
  si->v31_dechroma = 0.3f;
  si->v31_highlight_saturation = 1.3f;
  si->v31_roll_off = 0.5f;
  si->v31_gamut_compression = 0.7f;
  si->v31_gamut_compression_mode = (set == 1 ? 0.f : 3.f);
  si->v31_highlight_hue_shift = 0.5f;
  si->v31_highlight_saturation_shift = 0.4f;
  si->v35_exposure = 1.3f;
  si->v35_highlights = 1.15f;
  si->v35_shadows = 0.9f;
  si->v35_contrast = 1.2f;
  si->v35_highlight_contrast = 0.8f;
  si->v35_shadow_contrast = 1.1f;
  si->v35_flare = 0.05f;
  si->v35_saturation = 0.8f;
  si->v35_dechroma = 0.5f;
  si->v35_highlight_saturation = 1.4f;
  si->v35_roll_off = 1.6f;
  si->v35_gamut_compression = 0.4f;
  si->gamma_correction = (set == 1 ? 0.f : 1.f);
  si->bloom = 0.6f;
}

static void MoveWh3Sliders(wh3::ShaderInjectData* si, int set) {
  if (set == 0) return;
  si->exposure_tpm = 1.3f;
  si->highlights_tpm = 1.2f;
  si->shadows_tpm = 0.8f;
  si->contrast_tpm = 1.1f;
  si->saturation_tpm = 1.2f;
  si->exposure = 1.2f;
  si->highlights = 1.1f;
  si->shadows = 0.9f;
  si->contrast = 1.05f;
  si->saturation = 1.2f;
  si->cone_response_exponent = 0.9f;
  si->hue_shift = 1.5f;
  si->v31_exposure = 1.4f;
  si->v31_highlights = 1.2f;
  si->v31_shadows = 0.85f;
  si->v31_contrast = 1.1f;
  si->v31_highlight_contrast = 1.2f;
  si->v31_shadow_contrast = 0.9f;
  si->v31_flare = 0.002f;
  si->v31_saturation = 1.2f;
  si->v31_dechroma = 0.3f;
  si->v31_highlight_saturation = 1.3f;
  si->v31_roll_off = 0.5f;
  si->v31_gamut_compression = 0.7f;
  si->v31_gamut_compression_mode = (set == 1 ? 0.f : 3.f);
  si->v31_highlight_hue_shift = 0.5f;
  si->v31_highlight_saturation_shift = 0.4f;
  si->v35_exposure = 1.3f;
  si->v35_highlights = 1.15f;
  si->v35_shadows = 0.9f;
  si->v35_contrast = 1.2f;
  si->v35_highlight_contrast = 0.8f;
  si->v35_shadow_contrast = 1.1f;
  si->v35_flare = 0.05f;
  si->v35_saturation = 0.8f;
  si->v35_dechroma = 0.5f;
  si->v35_highlight_saturation = 1.4f;
  si->v35_roll_off = 1.6f;
  si->v35_gamut_compression = 0.4f;
  si->gamma_correction = (set == 1 ? 0.f : 1.f);
  si->bloom = 0.6f;
}

static void TestBits(const std::map<std::string, Pair>& shaders) {
  const auto columns = PatternColumns();
  const auto scene = Scene(columns, -6.f, 0.4f, 1.f);
  int draws = 0;
  int total_different = 0;
  for (const auto& [name, pair] : shaders) {
    const bool wh3_shader = name.starts_with("w_");
    const bool tone_mapper = (name == "a_t00" || name == "a_t09" || name == "a_t10");
    const std::vector<float> modes = (wh3_shader ? std::vector<float>{0.f, 1.f, 2.f, 3.f} : std::vector<float>{0.f, 1.f, 2.f});
    int different = 0;
    for (const float mode : modes) {
      for (const float encoding : {0.f, 1.f, 2.f}) {
        for (int set = 0; set < 3; ++set) {
          if (mode == 0.f && set == 2) continue;
          Bindings bindings;
          if (wh3_shader) {
            bindings = Wh3ToneMapper(scene, set != 1, 0.9f, set == 0 ? 0.1f : 0.5f);
            auto si = Wh3Injection(mode, encoding);
            MoveWh3Sliders(&si, set);
            bindings.injection = Bytes(si);
          } else {
            bindings = (tone_mapper ? AttilaToneMapper(scene, set == 0, 0.002f, 1.6f) : AttilaHdrToScreen(scene, set == 0));
            auto si = AttilaInjection(mode, encoding);
            MoveAttilaSliders(&si, set);
            bindings.injection = Bytes(si);
          }
          const std::string label = name + " mode " + std::to_string(static_cast<int>(mode)) + " output " +
                                    std::to_string(static_cast<int>(encoding)) + " sliders " + std::to_string(set);
          different += CompareBits(label.c_str(), pair, bindings);
          ++draws;
        }
      }
    }
    std::printf("1. %s: modes %s, %d pixels different\n", name.c_str(), wh3_shader ? "SDR / Extended / V30 / V31" : "SDR / V30 / V31", different);
    total_different += different;
  }
  std::printf("1. %d draws of %u pixels on each side, %d pixels different\n", draws, WIDTH * HEIGHT, total_different);
  CHECK(total_different == 0);
}

// 2. Hue Clip alone ////////////////////////////////////////////////////////////////////////////////////////////////

struct HueClipCase {
  Float4 color_and_strength;
  Float4 clip_scale;
};

static std::vector<Float4> RunHueClip(const std::filesystem::path& path, const std::vector<HueClipCase>& cases) {
  const auto code = ReadFile(path);
  REQUIRE(!code.empty());
  ComPtr<ID3D11ComputeShader> shader;
  REQUIRE(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &shader)));
  std::vector<HueClipCase> padded = cases;
  padded.resize((cases.size() + 63) / 64 * 64, HueClipCase{});
  const auto input = Structured(padded);
  const D3D11_BUFFER_DESC desc = {static_cast<UINT>(padded.size() * 16), D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0,
                                  D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16};
  ComPtr<ID3D11Buffer> output;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &output)));
  ComPtr<ID3D11UnorderedAccessView> output_view;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(output.Get(), nullptr, &output_view)));
  context->ClearState();
  context->CSSetShader(shader.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 1, input.GetAddressOf());
  context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
  context->Dispatch(static_cast<UINT>(padded.size() / 64), 1, 1);
  D3D11_BUFFER_DESC staging_desc = desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  staging_desc.MiscFlags = 0;
  ComPtr<ID3D11Buffer> staging;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&staging_desc, nullptr, &staging)));
  context->CopyResource(staging.Get(), output.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<Float4> results(cases.size());
  std::memcpy(results.data(), mapped.pData, cases.size() * 16);
  context->Unmap(staging.Get(), 0);
  return results;
}

// Where the middle channel sits between the smallest and the largest, by magnitude.
static float MiddlePlace(const Float3& c) {
  const Float3 m = {std::abs(c[0]), std::abs(c[1]), std::abs(c[2])};
  const float low = std::min({m[0], m[1], m[2]});
  const float high = std::max({m[0], m[1], m[2]});
  const float middle = m[0] + m[1] + m[2] - low - high;
  return high > low ? (middle - low) / (high - low) : 0.f;
}

static void TestHueClip(const std::filesystem::path& folder) {
  struct Expect {
    const char* name;
    Float3 color;
    float strength;
    float clip_scale;
  };
  const std::vector<Expect> expects = {
      {"fire orange, 4x the clip", {4.f, 1.2f, 0.2f}, 1.f, 1.f},
      {"fire orange, half", {4.f, 1.2f, 0.2f}, 0.5f, 1.f},
      {"fire orange at the clip", {1.f, 0.35f, 0.08f}, 1.f, 1.f},
      {"sky blue, 2x the clip", {0.7f, 1.1f, 2.f}, 1.f, 1.f},
      {"sky blue at the clip", {0.35f, 0.55f, 1.f}, 1.f, 1.f},
      {"grey", {0.5f, 0.5f, 0.5f}, 1.f, 1.f},
      {"bright grey", {30.f, 30.f, 30.f}, 1.f, 1.f},
      {"negative blue", {2.f, 0.5f, -0.1f}, 1.f, 1.f},
      {"negative red", {-0.05f, 0.9f, 1.6f}, 1.f, 1.f},
      {"strength 0", {4.f, 1.2f, 0.2f}, 0.f, 1.f},
      {"dark fire (1/64 of the clip)", {1.f / 64.f, 0.35f / 64.f, 0.08f / 64.f}, 1.f, 1.f},
      {"fire, clip at 0.25 (WH3's scale)", {1.f, 0.3f, 0.05f}, 1.f, 4.f},
      {"black", {0.f, 0.f, 0.f}, 1.f, 1.f},
  };
  std::vector<HueClipCase> cases;
  for (const auto& e : expects) cases.push_back({{e.color[0], e.color[1], e.color[2], e.strength}, {e.clip_scale, 0, 0, 0}});
  const auto attila_results = RunHueClip(folder / "hue_clip_twattila.cso", cases);
  for (size_t i = 0; i < expects.size(); ++i) {
    const auto& e = expects[i];
    const Float3 out = {attila_results[i][0], attila_results[i][1], attila_results[i][2]};
    const float y_in = Luminance(e.color);
    const float y_out = Luminance(out);
    const float place_in = MiddlePlace(e.color);
    const float place_out = MiddlePlace(out);
    std::printf("2. %-34s (%.4g %.4g %.4g) -> (%.4g %.4g %.4g): middle place %.3f -> %.3f, Y %.6g -> %.6g\n", e.name, e.color[0],
                e.color[1], e.color[2], out[0], out[1], out[2], place_in, place_out, y_in, y_out);
    for (int c = 0; c < 3; ++c) {
      CHECK(std::isfinite(out[c]));
      CHECK(std::signbit(out[c]) == std::signbit(e.color[c]) || out[c] == 0.f);
    }
    if (y_in > 0.f) CHECK(std::abs(y_out - y_in) <= 1e-5f * y_in);
    // The smallest and the largest magnitude keep their ratio (only the middle channel moves before the luminance).
    const auto ratio = [](const Float3& c) {
      const float low = std::min({std::abs(c[0]), std::abs(c[1]), std::abs(c[2])});
      const float high = std::max({std::abs(c[0]), std::abs(c[1]), std::abs(c[2])});
      return high > 0.f ? low / high : 0.f;
    };
    CHECK(std::abs(ratio(out) - ratio(e.color)) <= 1e-5f);
    const bool unchanged = (e.strength == 0.f || place_in == 0.f && e.color[0] == e.color[1] && e.color[1] == e.color[2]);
    if (unchanged) {
      CHECK(std::memcmp(out.data(), e.color.data(), 12) == 0);
    } else if (std::string(e.name).starts_with("dark")) {
      CHECK(std::abs(place_out - place_in) < 0.005f);
    } else if (std::string(e.name) != "black") {
      CHECK(place_out > place_in + 0.01f);  // the middle channel toward the largest: fire yellower, sky bluer-cyan
    }
  }
}

// 3. PsychoV35 through the chain ///////////////////////////////////////////////////////////////////////////////////

struct Chain {
  const Pair* attila_tone_mapper;
  const Pair* attila_hdr_to_screen;
  const Pair* wh3;
  const Shader* curves;  // hue_curves.hlsl (part 5)
};

// Linear BT.709 in units of the UI's white (x graphics nits = nits) of PsychoV35's (or the SDR mode's) output.
static std::vector<Float3> RunAttila(const Chain& chain, const std::vector<Float4>& scene, const attila::ShaderInjectData& si) {
  Bindings first = AttilaToneMapper(scene, true);
  first.injection = Bytes(si);
  const auto handoff = Draw(chain.attila_tone_mapper->after, first);
  Bindings second = AttilaHdrToScreen(handoff, true);
  second.injection = Bytes(si);
  const auto output = Draw(chain.attila_hdr_to_screen->after, second);
  std::vector<Float3> linear;
  for (const auto& o : output) linear.push_back(Decode(o, 2.f));
  return linear;
}

static std::vector<Float3> RunWh3(const Chain& chain, const std::vector<Float4>& scene, const wh3::ShaderInjectData& si, float burn) {
  Bindings bindings = Wh3ToneMapper(scene, true, 1.f, burn);
  bindings.injection = Bytes(si);
  const auto output = Draw(chain.wh3->after, bindings);
  std::vector<Float3> linear;
  for (const auto& o : output) linear.push_back(Decode(o, 2.2f));
  return linear;
}

// WH3's white in the scene: where its Extended Reinhard reaches 1 (exposure 1): x = sqrt(W), W = 1 / burn - 0.999.
static float Wh3White(float burn) { return std::sqrt(1.f / burn - 0.999f); }

static void CheckRamp(const char* label, const std::vector<Float3>& linear, float peak_nits, float ui_nits, bool sdr, bool gamut_fit,
                      bool monotonic, int* failures_in_ramp) {
  int bad = 0;
  float highest_y = 0.f;
  float highest_channel = 0.f;
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) {
      const Float3& c = linear[y * WIDTH + x];
      const bool finite = std::isfinite(c[0]) && std::isfinite(c[1]) && std::isfinite(c[2]);
      const float scale = (sdr ? 1.f : ui_nits);
      const float limit = (sdr ? 1.f : peak_nits);
      const Float3 target = (sdr ? c : Bt709ToBt2020(c));
      const float lum = Luminance(c) * scale;
      const float high = std::max({target[0], target[1], target[2]}) * scale;
      const float low = std::min({target[0], target[1], target[2]}) * scale;
      highest_y = std::max(highest_y, lum);
      highest_channel = std::max(highest_channel, high);
      // Without the fit (Gamut Compression 0) Musa's V35 lands up to 0.5% over Peak near white: its mean is the cones'
      // mean, not BT.709's Y (the display clips it there).
      bool ok = finite && lum <= limit * (gamut_fit ? 1.002f : 1.01f) + 1e-3f;
      if (gamut_fit) ok = ok && high <= limit * 1.002f + 1e-3f && low >= -limit * 2e-3f;
      if (!ok) {
        if (bad == 0) std::printf("   %s: pixel %u,%u (%.6g %.6g %.6g) limit %.1f\n", label, x, y, c[0], c[1], c[2], limit);
        ++bad;
      }
    }
  }
  // Grey (column 48) never darker as the scene gets brighter.
  int darker = 0;
  for (UINT y = 1; y < HEIGHT; ++y) {
    if (Luminance(linear[y * WIDTH + 48]) + 1e-6f < Luminance(linear[(y - 1) * WIDTH + 48])) ++darker;
  }
  std::printf("3. %-58s highest Y %8.2f, channel %8.2f (limit %.0f), %d outside, grey darker %d\n", label, highest_y, highest_channel,
              sdr ? 1.f : peak_nits, bad, darker);
  *failures_in_ramp += bad + (monotonic ? darker : 0);
}

static void TestPsychoV35(const Chain& chain) {
  const auto columns = PatternColumns();
  // exposure, highlights, shadows, contrast, saturation, roll-off, gamut compression, hue clip
  const std::vector<std::array<float, 8>> slider_sets = {
      {1, 1, 1, 1, 1, 1, 1, 0.5f}, {1, 1, 1, 1, 1, 1, 1, 0}, {1, 1, 1, 1, 1, 1, 1, 1},  {2, 1, 1, 1, 1, 1, 1, 0.5f},
      {1, 0.25f, 1, 1, 1, 1, 1, 0.5f}, {1, 0.5f, 1, 1, 1, 1, 1, 0.5f}, {1, 1, 0.25f, 1, 1, 1, 1, 0.5f},
      {1, 1, 0.5f, 1, 1, 1, 1, 0.5f},  {1, 1, 0.75f, 1, 1, 1, 1, 0.5f},
      {0, 1, 1, 1, 1, 1, 1, 0.5f}, {1, 0, 1, 1, 1, 1, 1, 0.5f}, {1, 2, 1, 1, 1, 1, 1, 0.5f}, {1, 1, 0, 1, 1, 1, 1, 0.5f},
      {1, 1, 2, 1, 1, 1, 1, 0.5f}, {1, 1, 1, 0, 1, 1, 1, 0.5f}, {1, 1, 1, 2, 1, 1, 1, 0.5f}, {1, 1, 1, 1, 0, 1, 1, 0.5f},
      {1, 1, 1, 1, 2, 1, 1, 0.5f}, {1, 1, 1, 1, 1, 0, 1, 0.5f}, {1, 1, 1, 1, 1, 2, 1, 0.5f}, {1, 1, 1, 1, 1, 1, 0, 0.5f},
      {2, 2, 2, 2, 2, 2, 1, 1},    {0.5f, 0, 0, 0.5f, 2, 0, 1, 1},
  };
  // Musa's own rows, at their defaults above, then at their ends with the others at theirs: highlight contrast, shadow
  // contrast, flare (as parsed: 0.1 at the slider's 1), dechroma, highlight saturation.
  constexpr std::array<float, 8> DEFAULTS = {1, 1, 1, 1, 1, 1, 1, 0.5f};
  std::vector<std::pair<std::array<float, 8>, std::array<float, 5>>> sets;
  for (const auto& s : slider_sets) sets.push_back({s, {1, 1, 0, 0, 1}});
  for (const std::array<float, 5>& e : {std::array<float, 5>{0, 1, 0, 0, 1}, {2, 1, 0, 0, 1}, {1, 0, 0, 0, 1}, {1, 2, 0, 0, 1},
                                        {1, 1, 0.1f, 0, 1}, {1, 1, 0, 1, 1}, {1, 1, 0, 0, 0}, {1, 1, 0, 0, 2}, {2, 0, 0.1f, 1, 2},
                                        {0, 2, 0.1f, 1, 0}}) {
    sets.push_back({DEFAULTS, e});
  }
  int failures_in_ramp = 0;
  for (const auto& [peak, encoding] : {std::pair{1000.f, 1.f}, std::pair{400.f, 0.f}, std::pair{1000.f, 2.f}}) {
    for (const auto& [s, e] : sets) {
      char settings[160];
      std::snprintf(settings, sizeof(settings), "%.0f nits %s, sliders %.2f %.2f %.2f %.2f %.2f %.2f %.2f hue %.1f own %.2f %.2f %.2f %.2f %.2f",
                    peak, encoding == 2.f ? "SDR" : (encoding == 0.f ? "HDR10" : "scRGB"), s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7],
                    e[0], e[1], e[2], e[3], e[4]);
      auto a = AttilaInjection(3.f, encoding);
      auto w = Wh3Injection(4.f, encoding);
      a.peak_white_nits = w.peak_white_nits = peak;
      a.v35_exposure = w.v35_exposure = s[0];
      a.v35_highlights = w.v35_highlights = s[1];
      a.v35_shadows = w.v35_shadows = s[2];
      a.v35_contrast = w.v35_contrast = s[3];
      a.v35_saturation = w.v35_saturation = s[4];
      a.v35_roll_off = w.v35_roll_off = s[5];
      a.v35_gamut_compression = w.v35_gamut_compression = s[6];
      a.hue_clip = w.hue_clip = s[7];
      a.v35_highlight_contrast = w.v35_highlight_contrast = e[0];
      a.v35_shadow_contrast = w.v35_shadow_contrast = e[1];
      a.v35_flare = w.v35_flare = e[2];
      a.v35_dechroma = w.v35_dechroma = e[3];
      a.v35_highlight_saturation = w.v35_highlight_saturation = e[4];
      const bool sdr = (encoding == 2.f);
      const bool fit = (s[6] == 1.f);
      // V35's own Highlights under 0.5 bends white back toward mid gray (0: white at mid gray): not monotonic there.
      const bool monotonic = (s[1] >= 0.5f);
      CheckRamp((std::string("Attila ") + settings).c_str(), RunAttila(chain, Scene(columns, -8.f, 20.f / 47.f, 1.f), a), peak, 203.f, sdr,
                fit, monotonic, &failures_in_ramp);
      // WH3's Output SDR: Peak, Paper White and UI all the display's white (shared.h's OutputShaderInjection).
      CheckRamp((std::string("WH3 ") + settings).c_str(), RunWh3(chain, Scene(columns, -8.f, 20.f / 47.f, Wh3White(0.3f)), w, 0.3f),
                peak, 203.f, sdr, fit, monotonic, &failures_in_ramp);
    }
  }
  CHECK(failures_in_ramp == 0);
}

// 4. Hue against the game's SDR ////////////////////////////////////////////////////////////////////////////////////

static float HueDegrees(const Float3& lab) { return std::atan2(lab[2], lab[1]) * 180.f / std::numbers::pi_v<float>; }

static void ReportHue(const Chain& chain) {
  // 64 columns: fire 0-7, sky blue 8-15 (b >= g >= r), 24 hues x 2 saturations 16-63; rows 2^-2 .. 2^7.4 of white.
  std::vector<Float3> columns;
  for (const float g : {0.2f, 0.3f, 0.4f, 0.5f}) {
    for (const float b : {0.03f, 0.1f}) columns.push_back({1.f, g, b});
  }
  for (const float g : {0.45f, 0.55f, 0.65f, 0.75f}) {
    for (const float r : {0.2f, 0.35f}) columns.push_back({r, g, 1.f});
  }
  for (int h = 0; h < 24; ++h) {
    for (const float s : {0.8f, 0.95f}) columns.push_back(Hue(h * 15.f, s));
  }
  const float first_stop = -2.f;
  const float stops_per_row = 0.2f;
  const std::vector<float> strengths = {0.f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.f, 1.25f, 1.5f, 2.f};
  struct Game {
    const char* name;
    std::function<std::vector<Float3>(float mode, float hue_clip)> run;
  };
  std::vector<Game> games = {
      {"Attila", [&](float mode, float hue_clip) {
         auto si = AttilaInjection(mode, 1.f);
         si.hue_clip = hue_clip;
         return RunAttila(chain, Scene(columns, first_stop, stops_per_row, 1.f), si);
       }},
  };
  for (const float burn : {0.1f, 0.5f}) {
    games.push_back({burn == 0.1f ? "WH3 burn 0.1" : "WH3 burn 0.5", [&, burn](float mode, float hue_clip) {
                       auto si = Wh3Injection(mode, 1.f);
                       si.hue_clip = hue_clip;
                       return RunWh3(chain, Scene(columns, first_stop, stops_per_row, Wh3White(burn)), si, burn);
                     }});
  }
  // 5. The other games: their kind of stock curve (hue_curves.hlsl), PsychoV35 at its defaults, white at 1.
  const char* const curve_names[] = {"5. Luminance Reinhard + clip (WH2)", "5. Luminance Hable + clip (Rome Remastered)",
                                     "5. Clip alone (RUSE)", "5. Hable per channel (Ryse)",
                                     "5. UE film per AP1 channel (Tales, Expeditions, Shogun 2)", "5. ACES fit per channel (Troy, Pharaoh)"};
  // Then Warhammer II's own t00 (curve 6) at three of its maps' burns, white where its stock curve reaches 1.
  std::vector<std::tuple<std::string, int, float>> curves;
  for (int curve = 0; curve < 6; ++curve) curves.emplace_back(curve_names[curve], curve, 0.f);
  for (const float burn : {0.1f, 0.3f, 0.5f}) {
    char name[64];
    std::snprintf(name, sizeof(name), "5. WH2's t00 (2333E142), burn %.1f", burn);
    curves.emplace_back(name, 6, 1.f / burn - 0.999f);
  }
  // Then Warhammer's tone mappers (curve 7), white 1: without the stock whiteout, and with whiteout_start 0 / end 10 (t00
  // D275C403's bloom_buffer in ATN's 2026-11-14 battle traces; his campaign's 0.144 / 10).
  curves.emplace_back("5. WH1's tone mappers (D275C403), no whiteout", 7, 0.f);
  curves.emplace_back("5. WH1's tone mappers (D275C403), whiteout 0 to 10", 7, 10.f);
  // Then Pharaoh's t00 (curve 8) at its traced burn 0.2 (battle and siege, tools/tw/pharaoh/phase2.md) and without one.
  curves.emplace_back("5. Pharaoh's t00 (976C1C74), burn 0.2", 8, 0.2f);
  curves.emplace_back("5. Pharaoh's t00 (976C1C74), burn 0", 8, 0.f);
  for (const auto& [curve_name, curve, white] : curves) {
    games.push_back({curve_name.c_str(), [&, curve, white](float mode, float hue_clip) {
                       Bindings bindings;
                       bindings.views["scene"] = Texture2D(WIDTH, HEIGHT, Scene(columns, first_stop, stops_per_row, (curve == 6) ? std::sqrt(white) : 1.f));
                       bindings.words["curve"] = F({static_cast<float>(curve)});
                       bindings.words["strength"] = F({hue_clip});
                       bindings.words["sdr"] = F({mode == 0.f ? 1.f : 0.f});
                       bindings.words["white"] = F({white});
                       // Curve 7: `white` is the whiteout's end (0: none, its start and end far above white).
                       bindings.words["whiteout_start"] = F({(curve == 7 && white == 0.f) ? 1e6f : 0.f});
                       bindings.words["whiteout_end"] = F({(curve == 7 && white == 0.f) ? 2e6f : white});
                       bindings.injection = Bytes(AttilaInjection(3.f, 0.f));
                       std::vector<Float3> linear;
                       for (const auto& o : Draw(*chain.curves, bindings)) linear.push_back({o[0], o[1], o[2]});
                       return linear;
                     }});
  }
  for (const auto& game : games) {
    const bool wh3_game = std::string(game.name).starts_with("WH3");
    const auto sdr = game.run(0.f, 0.f);
    std::printf("4. %s: |hue difference| (OkLab degrees) of PsychoV35 against the game's SDR where SDR reaches its clip (the\n"
                "   brightest channel at 0.95 or more, both chroma 0.04 or more); signed mean in brackets (+ = toward yellow\n"
                "   for fire, toward cyan for sky); last column: below the clip (SDR's brightest channel under 0.8)\n",
                game.name);
    std::printf("   hue clip   fire               sky blue           all hues           below the clip\n");
    for (const float strength : strengths) {
      const auto hdr = game.run(wh3_game ? 4.f : 3.f, strength);
      double sums[3] = {}, signed_sums[3] = {}, counts[3] = {}, below_sum = 0, below_count = 0;
      for (UINT y = 0; y < HEIGHT; ++y) {
        for (UINT x = 0; x < WIDTH; ++x) {
          const Float3& v = sdr[y * WIDTH + x];
          const Float3& h = hdr[y * WIDTH + x];
          const Float3 lab_v = OkLab(v);
          const Float3 lab_h = OkLab(h);
          const float chroma_v = std::hypot(lab_v[1], lab_v[2]);
          const float chroma_h = std::hypot(lab_h[1], lab_h[2]);
          if (chroma_v < 0.04f || chroma_h < 0.04f) continue;
          float difference = HueDegrees(lab_h) - HueDegrees(lab_v);
          difference = std::remainder(difference, 360.f);
          const float sdr_high = std::max({v[0], v[1], v[2]});
          if (sdr_high < 0.8f) {
            below_sum += std::abs(difference);
            ++below_count;
            continue;
          }
          if (sdr_high < 0.95f) continue;
          const int family = (x < 8 ? 0 : (x < 16 ? 1 : 2));
          // Fire's hue rises toward yellow, sky's falls toward cyan (OkLab hue of sky blue ~ 250, cyan ~ 195).
          const float toward = (family == 1 ? -difference : difference);
          sums[family] += std::abs(difference);
          signed_sums[family] += toward;
          ++counts[family];
          sums[2] += (family == 2 ? 0.f : std::abs(difference));
          counts[2] += (family == 2 ? 0 : 1);
        }
      }
      std::printf("   %8.2f", strength);
      for (int f = 0; f < 3; ++f) {
        std::printf("   %6.2f (%+6.2f) n%-4.0f", counts[f] ? sums[f] / counts[f] : 0.0, counts[f] ? signed_sums[f] / counts[f] : 0.0, counts[f]);
      }
      std::printf("   %6.2f\n", below_count ? below_sum / below_count : 0.0);
    }
  }
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

  std::map<std::string, Pair> shaders;
  for (const char* name : {"a_t00", "a_t01", "a_t02", "a_t03", "a_t04", "a_t05", "a_t06", "a_t07", "a_t08", "a_t09", "a_t10", "w_t00"}) {
    shaders[name] = {LoadPixelShader(before / (std::string(name) + ".cso")), LoadPixelShader(after / (std::string(name) + ".cso"))};
  }
  const bool report_only = (argc >= 4 && std::string(argv[3]) == "--report");
  if (!report_only) {
    TestBits(shaders);
    TestHueClip(after);
  }
  const Shader curves = LoadPixelShader(after / "hue_curves.cso");
  const Chain chain = {&shaders["a_t00"], &shaders["a_t01"], &shaders["w_t00"], &curves};
  if (!report_only) TestPsychoV35(chain);
  ReportHue(chain);
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}
