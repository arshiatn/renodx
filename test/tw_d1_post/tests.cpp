// Divinity: Original Sin EE's replaced passes besides the tone map (bloom, god rays, UI, FXAA, SMAA, vignette, fog of
// war, outlines, fire, candles, lights, the exe's colour shader) on WARP, Vanilla exact: in Rendering Mode SDR (Native)
// each add-on shader gives the game's own bytes' picture, and a deliberately wrong copy of it (o0.x * 0.99 + 0.004 at its end,
// written by run.ps1) must not. The light passes (point and spot, plain and shadowed): their Player / Local Light
// Intensity row at 2 in HDR gives twice the game's light. The tone map has its own test (test/tw_divinity1).
// Inputs as the game's shader declares them (its reflection, the game's shaders ship it): every texture 64 x 64 of fixed
// noise (0 - 1 for passes after the tone map, 0 - 4 before it), every constant buffer of fixed values 0 - 1 (a third set 0 - 16), samplers by
// name (Point / Wrap / comparison), the interpolants from one vertex shader made from the game's input signature (fixed
// functions of the position; both shaders get the same). The add-on's shader data at b10: Rendering Mode SDR, every row
// at its default.
// Compared: passes after the tone map through saturate (the game's UNORM targets), passes before it as they are
// (R11G11B10F / RGBA16F targets), relative to max(1, |game|).
// tests.exe <manifest>: lines "<hash> <pre|post> <game cso> <add-on cso> <mutant cso> [row|soft] [cb<slot>=<file> ...]"; exit
// code 0 when every check holds. A line's cb<slot> files (floats, whitespace apart, zeros after them) are a fourth input set's
// constant buffers at those slots: values that reach a pass's branch the noise does not.
// Built with DIVINITY2 (test/tw_d2_passes/run.ps1): Divinity: Original Sin 2 DE's passes, its shader data (Rendering Mode
// Off, every row at its default); a "soft" pass (the PCSS sun shadows) at Sun Shadow Softness 2 must change the game's
// shadow (the inputs reach the scaled light size).
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
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace divinity {
#ifdef DIVINITY2
#include "src/games/divinity2originalsindefinitive/shared.h"
#else
#include "src/games/divinityoriginalsinenhanced/shared.h"
#endif
}  // namespace divinity

using Microsoft::WRL::ComPtr;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)

constexpr UINT TEXTURE_SIZE = 64;
constexpr float TOLERANCE = 1e-5f;
constexpr UINT INJECTION_SLOT = 10;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

static std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Fixed noise: the same numbers every run.
static float Noise(uint32_t* state) {
  *state = *state * 1664525u + 1013904223u;
  return static_cast<float>(*state >> 8) / 16777216.f;
}

#ifdef DIVINITY2
// Rendering Mode Off (the game's own), every row at its default (addon.cpp).
static divinity::ShaderInjectData VanillaInjection() {
  return {.peak_white_nits = 1000.f, .diffuse_white_nits = 203.f, .graphics_white_nits = 203.f, .bloom = 1.f, .tonemap_type = 0.f,
          .sun_shadow_softness = 1.f};
}

// "soft" passes: twice the sun's light size.
static divinity::ShaderInjectData ChangedInjection() {
  auto changed = VanillaInjection();
  changed.sun_shadow_softness = 2.f;
  return changed;
}
#else
// Rendering Mode SDR (Native), every row at its default (addon.cpp).
static divinity::ShaderInjectData VanillaInjection() {
  return {
      .peak_white_nits = 1000.f, .diffuse_white_nits = 203.f, .graphics_white_nits = 203.f, .swap_chain_encoding = 1.f,
      .tonemap_type = 0.f, .vignette = 1.f, .bloom = 1.f, .godrays = 1.f, .ui = 1.f, .firehighlightmultiplier = 1.f,
      .candlehighlightmultiplier = 1.f, .candlehighlightmultiplierpsycho = 1.f, .player_light_intensity = 1.f,
      .local_light_intensity = 1.f, .exposure = 1.f, .highlights = 1.f, .shadows = 1.f, .cone_response_exponent = 1.f,
      .contrast = 1.f, .purity_scale = 1.f, .brazier_boost_enabled = 0.f, .hue_clip = 0.7f, .v35_exposure = 1.f,
      .v35_highlights = 1.f, .v35_shadows = 1.f, .v35_contrast = 1.f, .v35_highlight_contrast = 1.f,
      .v35_shadow_contrast = 1.f, .v35_flare = 0.f, .v35_saturation = 1.f, .v35_dechroma = 0.f,
      .v35_highlight_saturation = 1.f, .v35_roll_off = 1.f, .v35_gamut_compression = 1.f, .ext_hue_clip = 0.7f,
      .sun_shadow_softness = 1.f,
  };
}

// "row" passes: the light rows at 2 in HDR (Extended), the light passes' output doubles.
static divinity::ShaderInjectData ChangedInjection() {
  auto changed = VanillaInjection();
  changed.tonemap_type = 3.f;
  changed.player_light_intensity = 2.f;
  changed.local_light_intensity = 2.f;
  return changed;
}
#endif

// What one pass reads, made from the game's shader's reflection.
struct Inputs {
  ComPtr<ID3D11VertexShader> vertex_shader;
  std::vector<std::pair<UINT, ComPtr<ID3D11ShaderResourceView>>> textures;
  std::vector<std::pair<UINT, ComPtr<ID3D11SamplerState>>> samplers;
  std::vector<std::pair<UINT, ComPtr<ID3D11Buffer>>> constants;
  std::string skipped;  // a binding this harness cannot make
};

static ComPtr<ID3D11Buffer> MakeConstants(const void* data, UINT bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = (bytes + 15u) & ~15u, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  std::vector<uint8_t> padded(desc.ByteWidth, 0);
  std::memcpy(padded.data(), data, bytes);
  const D3D11_SUBRESOURCE_DATA init = {.pSysMem = padded.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &init, &buffer);
  return buffer;
}

// `fixed`: constant buffers by slot from the manifest (null: noise in every one).
static Inputs MakeInputs(const std::vector<char>& game_code, float range, float constant_scale, uint32_t seed,
                         const std::map<UINT, std::vector<float>>* fixed) {
  Inputs inputs;
  ComPtr<ID3D11ShaderReflection> reflection;
  if (FAILED(D3DReflect(game_code.data(), game_code.size(), IID_PPV_ARGS(&reflection)))) {
    inputs.skipped = "no reflection in the game's shader";
    return inputs;
  }
  D3D11_SHADER_DESC shader_desc = {};
  reflection->GetDesc(&shader_desc);

  // The vertex shader: the game's input signature in its order, each a fixed function of the position.
  std::string outputs = "float4 position : SV_Position;\n";
  std::string writes;
  std::vector<D3D11_SIGNATURE_PARAMETER_DESC> parameters(shader_desc.InputParameters);
  for (UINT index = 0; index < shader_desc.InputParameters; ++index) reflection->GetInputParameterDesc(index, &parameters[index]);
  std::ranges::sort(parameters, [](const auto& a, const auto& b) {
    return (a.Register != b.Register) ? (a.Register < b.Register) : (std::countr_zero(a.Mask) < std::countr_zero(b.Mask));
  });
  int member = 0;
  for (const auto& parameter : parameters) {
    if (parameter.SystemValueType != D3D_NAME_UNDEFINED) continue;
    const int components = std::popcount(static_cast<unsigned>(parameter.Mask));
    const bool integer = parameter.ComponentType != D3D_REGISTER_COMPONENT_FLOAT32;
    const std::string type = std::format("{}{}", (integer ? "nointerpolation uint" : "float"), (components > 1 ? std::to_string(components) : ""));
    outputs += std::format("{} v{} : {}{};\n", type, member, parameter.SemanticName, parameter.SemanticIndex);
    const std::string swizzle = std::string("xyzw").substr(0, components);
    writes += std::format("o.v{} = ({})(float4(uv.x, uv.y, 1.0 - uv.x, uv.y * 0.5 + 0.25) * {:.3f} + {:.3f}).{};\n", member, type.substr(integer ? 16 : 0),
                          1.f + 0.37f * member, 0.05f * member, swizzle);
    ++member;
  }
  const std::string source = "struct Out {\n" + outputs + "};\nOut main(uint id : SV_VertexID) {\n  float2 uv = float2((id << 1) & 2, id & 2);\n"
                             "  Out o;\n  o.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n" + writes + "  return o;\n}\n";
  ComPtr<ID3DBlob> vertex_code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vertex_code, &errors))) {
    inputs.skipped = "vertex shader: " + std::string(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return inputs;
  }
  device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), nullptr, &inputs.vertex_shader);

  uint32_t state = seed;
  for (UINT index = 0; index < shader_desc.BoundResources; ++index) {
    D3D11_SHADER_INPUT_BIND_DESC bind = {};
    reflection->GetResourceBindingDesc(index, &bind);
    const std::string name = bind.Name;
    switch (bind.Type) {
      case D3D_SIT_TEXTURE: {
        // 2D, cube (a point light's shadow) and 2D arrays (4 slices): each face or slice its own noise.
        const bool cube = bind.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE;
        const bool array = bind.Dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY;
        if (bind.Dimension != D3D_SRV_DIMENSION_TEXTURE2D && !cube && !array) {
          inputs.skipped = std::format("texture {} (t{}) of dimension {}", name, bind.BindPoint, static_cast<int>(bind.Dimension));
          return inputs;
        }
        const UINT slices = cube ? 6u : (array ? 4u : 1u);
        std::vector<float> texels(TEXTURE_SIZE * TEXTURE_SIZE * 4 * slices);
        for (float& texel : texels) texel = Noise(&state) * range;
        std::vector<D3D11_SUBRESOURCE_DATA> init(slices);
        for (UINT slice = 0; slice < slices; ++slice) {
          init[slice] = {&texels[slice * TEXTURE_SIZE * TEXTURE_SIZE * 4], TEXTURE_SIZE * 4 * sizeof(float), 0};
        }
        const D3D11_TEXTURE2D_DESC desc = {TEXTURE_SIZE, TEXTURE_SIZE, 1, slices, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                           D3D11_BIND_SHADER_RESOURCE, 0, (cube ? static_cast<UINT>(D3D11_RESOURCE_MISC_TEXTURECUBE) : 0u)};
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        device->CreateTexture2D(&desc, init.data(), &texture);
        D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT};
        if (cube) {
          view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
          view_desc.TextureCube = {.MostDetailedMip = 0, .MipLevels = 1};
        } else if (array) {
          view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
          view_desc.Texture2DArray = {.MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = 0, .ArraySize = slices};
        } else {
          view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
          view_desc.Texture2D = {.MostDetailedMip = 0, .MipLevels = 1};
        }
        device->CreateShaderResourceView(texture.Get(), &view_desc, &view);
        inputs.textures.emplace_back(bind.BindPoint, view);
        break;
      }
      case D3D_SIT_SAMPLER: {
        const bool point = name.find("Point") != std::string::npos;
        const bool wrap = name.find("Wrap") != std::string::npos;
        const bool comparison = (bind.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0;
        const auto address = wrap ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        D3D11_FILTER filter = point ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        if (comparison) filter = point ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT : D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        const D3D11_SAMPLER_DESC desc = {.Filter = filter, .AddressU = address, .AddressV = address, .AddressW = address,
                                         .ComparisonFunc = (comparison ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_NEVER),
                                         .MaxLOD = D3D11_FLOAT32_MAX};
        ComPtr<ID3D11SamplerState> sampler;
        device->CreateSamplerState(&desc, &sampler);
        inputs.samplers.emplace_back(bind.BindPoint, sampler);
        break;
      }
      case D3D_SIT_CBUFFER: {
        D3D11_SHADER_BUFFER_DESC buffer_desc = {};
        reflection->GetConstantBufferByName(bind.Name)->GetDesc(&buffer_desc);
        std::vector<float> values(buffer_desc.Size / 4);
        for (float& value : values) value = Noise(&state) * constant_scale;
        if (fixed != nullptr && fixed->contains(bind.BindPoint)) {
          const auto& file_values = fixed->at(bind.BindPoint);
          std::ranges::fill(values, 0.f);
          std::copy_n(file_values.begin(), std::min(file_values.size(), values.size()), values.begin());
        }
        inputs.constants.emplace_back(bind.BindPoint, MakeConstants(values.data(), buffer_desc.Size));
        break;
      }
      default:
        inputs.skipped = std::format("binding {} of type {}", name, static_cast<int>(bind.Type));
        return inputs;
    }
  }
  return inputs;
}

static std::vector<std::array<float, 4>> Draw(const std::vector<char>& code, const Inputs& inputs, ID3D11Buffer* injection) {
  ComPtr<ID3D11PixelShader> pixel_shader;
  if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, &pixel_shader))) return {};
  const D3D11_TEXTURE2D_DESC target_desc = {TEXTURE_SIZE, TEXTURE_SIZE, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  device->CreateTexture2D(&target_desc, nullptr, &target);
  device->CreateRenderTargetView(target.Get(), nullptr, &target_view);
  context->ClearState();
  const FLOAT clear[4] = {};
  context->ClearRenderTargetView(target_view.Get(), clear);
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  const D3D11_VIEWPORT viewport = {0, 0, TEXTURE_SIZE, TEXTURE_SIZE, 0, 1};
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(inputs.vertex_shader.Get(), nullptr, 0);
  context->PSSetShader(pixel_shader.Get(), nullptr, 0);
  for (const auto& [slot, view] : inputs.textures) context->PSSetShaderResources(slot, 1, view.GetAddressOf());
  for (const auto& [slot, sampler] : inputs.samplers) context->PSSetSamplers(slot, 1, sampler.GetAddressOf());
  for (const auto& [slot, buffer] : inputs.constants) context->PSSetConstantBuffers(slot, 1, buffer.GetAddressOf());
  context->PSSetConstantBuffers(INJECTION_SLOT, 1, &injection);
  context->Draw(3, 0);
  const D3D11_TEXTURE2D_DESC staging_desc = {TEXTURE_SIZE, TEXTURE_SIZE, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&staging_desc, nullptr, &staging);
  context->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<std::array<float, 4>> texels(TEXTURE_SIZE * TEXTURE_SIZE);
  for (UINT row = 0; row < TEXTURE_SIZE; ++row) {
    std::memcpy(&texels[row * TEXTURE_SIZE], static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch, TEXTURE_SIZE * sizeof(texels[0]));
  }
  context->Unmap(staging.Get(), 0);
  return texels;
}

// The largest difference (relative to max(1, |game|)); through saturate for a pass after the tone map. A NaN on one
// side only counts as infinite.
static float Difference(const std::vector<std::array<float, 4>>& game, const std::vector<std::array<float, 4>>& ours, bool post) {
  if (game.size() != ours.size() || game.empty()) return INFINITY;
  float worst = 0.f;
  for (size_t index = 0; index < game.size(); ++index) {
    for (int channel = 0; channel < 4; ++channel) {
      float a = game[index][channel];
      float b = ours[index][channel];
      if (std::isnan(a) || std::isnan(b)) {
        if (std::isnan(a) != std::isnan(b)) return INFINITY;
        continue;
      }
      if (post) {
        a = std::clamp(a, 0.f, 1.f);
        b = std::clamp(b, 0.f, 1.f);
      }
      worst = std::max(worst, std::abs(a - b) / std::max(1.f, std::abs(a)));
    }
  }
  return worst;
}

int main(int argc, char** argv) {
  if (argc < 2 || FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) {
    std::printf("FAIL: usage or no WARP device\n");
    return 1;
  }
  const auto vanilla = VanillaInjection();
  const ComPtr<ID3D11Buffer> injection = MakeConstants(&vanilla, sizeof(vanilla));
  const auto changed = ChangedInjection();
  const ComPtr<ID3D11Buffer> changed_injection = MakeConstants(&changed, sizeof(changed));
  std::ifstream manifest(argv[1]);
  int passes = 0;
  for (std::string line; std::getline(manifest, line);) {
    std::istringstream fields(line);
    std::string hash, stage, game_path, addon_path, mutant_path, row;
    if (!(fields >> hash >> stage >> game_path >> addon_path >> mutant_path)) continue;
    std::map<UINT, std::vector<float>> fixed;
    for (std::string token; fields >> token;) {
      if (const size_t equals = token.find('='); token.starts_with("cb") && equals != std::string::npos) {
        std::ifstream values_file(token.substr(equals + 1));
        auto& values = fixed[static_cast<UINT>(std::stoul(token.substr(2, equals - 2)))];
        for (float value = 0.f; values_file >> value;) values.push_back(value);
        CHECK(!values.empty());
      } else {
        row = token;
      }
    }
    const bool post = stage == "post";
    const auto game_code = ReadFile(game_path);
    const auto addon_code = ReadFile(addon_path);
    const auto mutant_code = ReadFile(mutant_path);
    CHECK(!game_code.empty() && !addon_code.empty() && !mutant_code.empty());
    float worst = 0.f;
    float mutant_worst = 0.f;  // caught when any input set shows it (saturate hides it where a set's output is past 1)
    float row_worst = 0.f;     // a light pass at its row's 2 against twice the game's light
    float soft_change = 0.f;   // a soft pass at Sun Shadow Softness 2 against the game's shadow
    bool flat = true;  // the game's output one value everywhere: these inputs reach one branch only
    std::string skipped;
    // Three noise sets: other values everywhere, the third with constants 0 - 16 (other branches of a pass's tests); a
    // fourth with the line's own constant buffers.
    struct InputSet {
      uint32_t seed;
      float constant_scale;
      const std::map<UINT, std::vector<float>>* fixed;
    };
    std::vector<InputSet> sets = {{0x1234567u, 1.f, nullptr}, {0x89ABCDEu, 1.f, nullptr}, {0x2468ACEu, 16.f, nullptr}};
    if (!fixed.empty()) sets.push_back({0x13579BDu, 1.f, &fixed});
    for (const auto& set : sets) {
      const Inputs inputs = MakeInputs(game_code, (post ? 1.f : 4.f), set.constant_scale,
                                       set.seed ^ static_cast<uint32_t>(std::hash<std::string>{}(hash)), set.fixed);
      if (!inputs.skipped.empty()) {
        skipped = inputs.skipped;
        break;
      }
      const auto game = Draw(game_code, inputs, injection.Get());
      for (const auto& texel : game) {
        if (!game.empty() && texel != game[0]) flat = false;
      }
      worst = std::max(worst, Difference(game, Draw(addon_code, inputs, injection.Get()), post));
      mutant_worst = std::max(mutant_worst, Difference(game, Draw(mutant_code, inputs, injection.Get()), post));
      if (row == "row") {
        auto twice = game;
        for (auto& texel : twice) {
          for (int channel = 0; channel < 3; ++channel) texel[channel] *= 2.f;
        }
        row_worst = std::max(row_worst, Difference(twice, Draw(addon_code, inputs, changed_injection.Get()), post));
      }
      if (row == "soft") soft_change = std::max(soft_change, Difference(game, Draw(addon_code, inputs, changed_injection.Get()), post));
    }
    if (!skipped.empty()) {
      std::printf("0x%s: NOT CHECKED (%s)\n", hash.c_str(), skipped.c_str());
      CHECK(skipped.empty());
      continue;
    }
    std::printf("0x%s (%s): add-on against the game's bytes %.3g, the wrong copy %.3g%s", hash.c_str(), stage.c_str(), worst, mutant_worst,
                (flat ? " (the game's output one value: one branch only)" : ""));
    if (row == "row") std::printf("; its light row at 2 against twice the game's light %.3g", row_worst);
    if (row == "soft") std::printf("; Sun Shadow Softness 2 against the game's shadow %.3g", soft_change);
    std::printf("\n");
    CHECK(worst <= TOLERANCE);
    CHECK(mutant_worst > TOLERANCE);
    CHECK(row != "row" || row_worst <= TOLERANCE);
    CHECK(row != "soft" || soft_change > TOLERANCE);
    ++passes;
  }
#ifdef DIVINITY2
  constexpr const char* NAME = "tw_d2_passes";
#else
  constexpr const char* NAME = "tw_d1_post";
#endif
  if (failures == 0) {
    std::printf("%s: %d passes Vanilla exact, every wrong copy caught\n", NAME, passes);
  } else {
    std::printf("%s: %d passes run, %d checks failed\n", NAME, passes, failures);
  }
  return failures == 0 ? 0 : 1;
}
