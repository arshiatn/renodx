// The game's other builds of Attila's replaced shaders. The lit particles' pixel shaders (particle_vfx_main
// ps_main_high_quality and ps_main, every shadow method and shader model: Attila's vfx00 / vfx01 and the builds
// src/games/twattila/shaders/vfx00<method> / vfx01 replace, made by
// tools/tw/attila_gaps/atg_vfx_make.py) on WARP against the game's dumped bytes (Attila's; Thrones of Britannia's are the
// same). As tools/tw/thrones/par_th_vfx_test, per build:
// A. t49, b8, b9 unbound, every control at 1, fog off: the replacement equals the game's, bit for bit (discards the same).
// F. the same with random fog (the fog's sun_direction.y fix): bit for bit.
// E. VFX Brightness 1, 2, 3: every rgb affine in it, alpha and discards unchanged; CONTROL: some pixels move.
// High quality builds (the sun received through the game's shadow map, or none):
// B. t49 = 1 (the rays' sun fully occluded) equals the game's with every shadow test failing (comparison sampler NEVER; the
//    shadow map at -1e30 where the shader compares itself; no shadow map: ps_receive_shadows_high_quality_params.x 0);
//    CONTROL the game's with t49 = 1 differs.
// C. in a half-size layer (viewport W/2 x H/2, b8 layer_scale 2) with t49 = 1 on the right half of a full-size map: its
//    pixels from W/4 on equal B's failing game draw at that viewport, the rest the game's; CONTROL b8 unbound differs.
// ps_main builds: D. t49 = 1 ignored (no shadow).
// X. CONTROL: another build's game shader (another shadow method or path, the same bindings) against the replacement differs.
// The battle fog (battlefog00 A6A0F887 and its MSAA build battlefog01 A538288B, per sample on a 4x multisampled depth), random
// fog, t43 / t44 / b9 / b12 unbound:
// A. Fog Strength 1, Fog & Sky Smoothing off: the game's within 1e-4 x the value (fxc orders some adds otherwise: a few
//    ulp, as battlefog00 since it was made).
// G. Fog Strength 0.5: rgb bit for bit, alpha never above A's; CONTROL: some alpha moves.
// H. Fog & Sky Smoothing Medium: CONTROL: some rgb moves.
// The rays' captures of draws no trace has shown (Rarer Objects in the Rays), through stream output with the game's input layout
// (CheckCaptures): the tessellated rigid 9AEE504E as triangles (T1-T4), the ropes 8D0D26B9 (R) and 3926A823 (Q).
// tests.exe <the game's dumped csos> <the add-on's built csos (build32\<game>.include\embed)> <VFX Brightness's float in b13>
//           <Fog Strength's float in b13 (-1: no fog builds)> [all: every build's replacement must be there]
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <random>
#include <string>
#include <vector>

static const UINT W = 64, H = 64;
static const float SENTINEL = -7.f;  // the target's clear: a discarded pixel keeps it
static ID3D11Device* device = nullptr;
static ID3D11DeviceContext* context = nullptr;
static std::mt19937 rng(1);
static ID3D11Buffer* injection = nullptr;  // b13: the add-on's settings, VFX Brightness 1
static ID3D11Buffer* brighter_2 = nullptr;  // VFX Brightness 2
static ID3D11Buffer* brighter_3 = nullptr;  // VFX Brightness 3
static ID3D11Buffer* fog_half = nullptr;    // Fog Strength 0.5
static ID3D11Buffer* sky_smooth = nullptr;  // Fog & Sky Smoothing Medium, Strength 1
static float Range(float lo, float hi) { return lo + (hi - lo) * std::uniform_real_distribution<float>(0.f, 1.f)(rng); }

static std::vector<uint8_t> Load(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// A W x H RGBA32F texture with 4 samples a pixel, each a hash of its pixel, sample and `seed` in 0..1 (drawn: a multisampled
// texture takes no initial data).
static ID3D11ShaderResourceView* MakeMultisampled(uint32_t seed) {
  const std::string source = "float4 vs(uint id : SV_VertexID) : SV_Position { return float4(float2((id << 1) & 2, id & 2) * float2(2, -2) + float2(-1, 1), 0, 1); }\n"
                             "float4 ps(float4 p : SV_Position, uint s : SV_SampleIndex) : SV_Target {\n"
                             "  uint4 h = uint4(uint2(p.xy), s, " + std::to_string(seed) + "u) * uint4(73856093u, 19349663u, 83492791u, 2654435761u);\n"
                             "  uint x = h.x ^ h.y ^ h.z ^ h.w; x ^= x >> 13; x *= 0x5BD1E995u; x ^= x >> 15;\n"
                             "  return float4(x & 0xFFu, (x >> 8) & 0xFFu, (x >> 16) & 0xFFu, x >> 24) / 255.0; }\n";
  ID3DBlob* vs_blob = nullptr;
  ID3DBlob* ps_blob = nullptr;
  D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vs_blob, nullptr);
  D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, &ps_blob, nullptr);
  ID3D11VertexShader* vs = nullptr;
  ID3D11PixelShader* ps = nullptr;
  device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vs);
  device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &ps);
  const D3D11_TEXTURE2D_DESC desc = {W, H, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {4, 0}, D3D11_USAGE_DEFAULT,
                                     D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0};
  ID3D11Texture2D* texture = nullptr;
  device->CreateTexture2D(&desc, nullptr, &texture);
  ID3D11RenderTargetView* rtv = nullptr;
  device->CreateRenderTargetView(texture, nullptr, &rtv);
  context->OMSetRenderTargets(1, &rtv, nullptr);
  const D3D11_VIEWPORT viewport = {0.f, 0.f, float(W), float(H), 0.f, 1.f};
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vs, nullptr, 0);
  context->PSSetShader(ps, nullptr, 0);
  context->Draw(3, 0);
  context->OMSetRenderTargets(0, nullptr, nullptr);
  ID3D11ShaderResourceView* view = nullptr;
  device->CreateShaderResourceView(texture, nullptr, &view);
  rtv->Release();
  texture->Release();
  vs->Release();
  ps->Release();
  vs_blob->Release();
  ps_blob->Release();
  return view;
}

// A W x H (cube: 16 x 16) RGBA32F texture: random texels, all `uniform` when not NaN, or 1 from column `from_column` on.
static ID3D11ShaderResourceView* MakeTexture(D3D_SRV_DIMENSION dimension, uint32_t seed, float uniform = NAN, int from_column = -1) {
  if (dimension == D3D_SRV_DIMENSION_TEXTURE2DMS) return MakeMultisampled(seed);
  std::mt19937 own(seed);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  const bool cube = dimension == D3D_SRV_DIMENSION_TEXTURECUBE;
  const UINT width = cube ? 16 : W;
  const UINT height = cube ? 16 : H;
  const UINT slices = cube ? 6 : 1;
  std::vector<float> data(size_t(width) * height * 4 * slices);
  for (size_t i = 0; i < data.size(); ++i) {
    const float value = unit(own);
    if (from_column >= 0) {
      data[i] = int((i / 4) % width) >= from_column ? 1.f : 0.f;
    } else {
      data[i] = std::isnan(uniform) ? value : uniform;
    }
  }
  std::vector<D3D11_SUBRESOURCE_DATA> init(slices);
  for (UINT slice = 0; slice < slices; ++slice) init[slice] = {data.data() + size_t(slice) * width * height * 4, width * 16, 0};
  const D3D11_TEXTURE2D_DESC desc = {width, height, 1, slices, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                                     cube ? UINT(D3D11_RESOURCE_MISC_TEXTURECUBE) : 0u};
  ID3D11Texture2D* texture = nullptr;
  device->CreateTexture2D(&desc, init.data(), &texture);
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
  view_desc.Format = desc.Format;
  view_desc.ViewDimension = dimension;
  if (cube) {
    view_desc.TextureCube = {0, 1};
  } else if (dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY) {
    view_desc.Texture2DArray = {0, 1, 0, 1};
  } else {
    view_desc.Texture2D = {0, 1};
  }
  ID3D11ShaderResourceView* view = nullptr;
  device->CreateShaderResourceView(texture, &view_desc, &view);
  texture->Release();
  return view;
}

static ID3D11Buffer* MakeConstants(const std::vector<float>& values) {
  const D3D11_BUFFER_DESC desc = {UINT(values.size() * 4), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
  const D3D11_SUBRESOURCE_DATA init = {values.data(), 0, 0};
  ID3D11Buffer* buffer = nullptr;
  device->CreateBuffer(&desc, &init, &buffer);
  return buffer;
}

struct Tally {
  uint64_t values = 0, bits = 0, misses = 0, nans = 0, discards = 0;
  double largest = 0.0;
  void Add(float got, float want) {
    ++values;
    if (std::isnan(got) || std::isnan(want)) {
      if (std::isnan(got) != std::isnan(want)) ++nans;
      return;
    }
    if ((got == SENTINEL) != (want == SENTINEL)) ++discards;
    if (std::memcmp(&got, &want, 4) != 0) ++bits;
    const double difference = std::fabs(double(got) - want);
    largest = std::max(largest, difference / std::max(1.0, std::fabs(double(want))));
    if (difference > 1e-4 * std::max(1.0, std::fabs(double(want)))) ++misses;
  }
  bool Exact() const { return bits == 0 && nans == 0 && discards == 0; }
  bool Same() const { return misses == 0 && nans == 0 && discards == 0; }
  void Print(const char* what) const {
    std::printf("  %s: values %llu, bits differ %llu, misses %llu, discards differ %llu, nan %llu, largest %.2e (relative)\n", what,
                (unsigned long long)values, (unsigned long long)bits, (unsigned long long)misses, (unsigned long long)discards,
                (unsigned long long)nans, largest);
  }
};

enum class Kind { COMPARISON_SAMPLER, SHADOW_TEXTURE, NO_SHADOW_MAP, PS_MAIN };

// The game's shader's inputs (a full-screen triangle: each input an affine function of the viewport's uv), target, samplers,
// constants (b0-b7 random but shadowmap_PS in reach, every emitter receiving the sun, fog_VS_PS zero unless `fog`) and
// textures (the shadow map at -1e30 when `failing_map`).
struct Fixture {
  ID3D11ShaderReflection* reflection = nullptr;
  D3D11_SHADER_DESC desc = {};
  ID3D11VertexShader* vs = nullptr;
  ID3D11Texture2D* target = nullptr;
  ID3D11Texture2D* staging = nullptr;
  ID3D11RenderTargetView* rtv = nullptr;
  std::array<ID3D11SamplerState*, 16> samplers = {};
  std::array<ID3D11SamplerState*, 16> never = {};

  bool Make(const std::vector<uint8_t>& game_code) {
    if (FAILED(D3DReflect(game_code.data(), game_code.size(), IID_PPV_ARGS(&reflection)))) return false;
    reflection->GetDesc(&desc);
    std::string inputs, outputs;
    for (UINT i = 0; i < desc.InputParameters; ++i) {
      D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
      reflection->GetInputParameterDesc(i, &parameter);
      if (parameter.SystemValueType != D3D_NAME_UNDEFINED) continue;
      const int width = std::popcount(unsigned(parameter.Mask));
      const std::string input = "e" + std::to_string(i);
      const std::string swizzle = std::string(".xyzw").substr(0, width + 1);
      const std::string semantic = std::string(parameter.SemanticName) + std::to_string(parameter.SemanticIndex);
      if (parameter.ComponentType == D3D_REGISTER_COMPONENT_UINT32 || parameter.ComponentType == D3D_REGISTER_COMPONENT_SINT32) {
        // An index (the particles' emitter): small, the same over the draw.
        const std::string type = (parameter.ComponentType == D3D_REGISTER_COMPONENT_UINT32 ? "uint" : "int") + std::to_string(width);
        inputs += "  nointerpolation " + type + " " + input + " : " + semantic + ";\n";
        outputs += "  o." + input + " = (" + type + ")(values[" + std::to_string(parameter.Register) + "] * 8.0)" + swizzle + ";\n";
        continue;
      }
      inputs += "  float" + std::to_string(width) + " " + input + " : " + semantic + ";\n";
      outputs += "  o." + input + " = (values[" + std::to_string(parameter.Register) + "] + values[" + std::to_string(parameter.Register + 8)
                 + "] * uv.xyxy)" + swizzle + ";\n";
    }
    const std::string vs_text = "cbuffer c : register(b0) { float4 values[32]; };\nstruct O {\n  float4 p : SV_Position;\n" + inputs
                                + "};\nO main(uint id : SV_VertexID) { O o; float2 uv = float2((id << 1) & 2, id & 2); o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0.5, 1);\n"
                                + outputs + "  return o; }\n";
    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* errors = nullptr;
    if (FAILED(D3DCompile(vs_text.data(), vs_text.size(), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs_blob, &errors))) {
      std::printf("vs: %s\n", errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
      return false;
    }
    device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vs);
    D3D11_TEXTURE2D_DESC target_desc = {W, H, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
    device->CreateTexture2D(&target_desc, nullptr, &target);
    device->CreateRenderTargetView(target, nullptr, &rtv);
    target_desc.Usage = D3D11_USAGE_STAGING;
    target_desc.BindFlags = 0;
    target_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    device->CreateTexture2D(&target_desc, nullptr, &staging);
    D3D11_SAMPLER_DESC sampler_desc = {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP,
                                       0.f, 1, D3D11_COMPARISON_NEVER, {0, 0, 0, 0}, 0.f, D3D11_FLOAT32_MAX};
    ID3D11SamplerState* plain = nullptr;
    device->CreateSamplerState(&sampler_desc, &plain);
    sampler_desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    ID3D11SamplerState* comparison = nullptr;
    device->CreateSamplerState(&sampler_desc, &comparison);
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    ID3D11SamplerState* failing = nullptr;
    device->CreateSamplerState(&sampler_desc, &failing);
    samplers.fill(plain);
    never.fill(plain);
    for (UINT i = 0; i < desc.BoundResources; ++i) {
      D3D11_SHADER_INPUT_BIND_DESC bind = {};
      reflection->GetResourceBindingDesc(i, &bind);
      if (bind.Type == D3D_SIT_SAMPLER && (bind.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0) {
        samplers[bind.BindPoint] = comparison;
        never[bind.BindPoint] = failing;
      }
    }
    return true;
  }

  void Inputs(int run, bool fog, float receive, bool failing_map, std::array<ID3D11Buffer*, 8>* buffers,
              std::array<ID3D11ShaderResourceView*, 24>* views) const {
    rng.seed(7000 + run);
    std::vector<float> constants(4096 * 4);
    D3D11_SHADER_INPUT_BIND_DESC shadowmap = {}, fog_bind = {}, emitters = {}, emitters_ps = {};
    const bool has_shadowmap = SUCCEEDED(reflection->GetResourceBindingDescByName("shadowmap_PS", &shadowmap));
    const bool has_fog = SUCCEEDED(reflection->GetResourceBindingDescByName("fog_VS_PS", &fog_bind));
    const bool has_emitters = SUCCEEDED(reflection->GetResourceBindingDescByName("cb_vfx_emitter_constants_common", &emitters));
    const bool has_emitters_ps = SUCCEEDED(reflection->GetResourceBindingDescByName("cb_vfx_emitter_constants_ps", &emitters_ps));
    const auto offset = [&](const char* buffer, const char* name) -> int {
      D3D11_SHADER_VARIABLE_DESC variable = {};
      if (FAILED(reflection->GetConstantBufferByName(buffer)->GetVariableByName(name)->GetDesc(&variable))) return -1;
      return int(variable.StartOffset / 4);
    };
    const auto set = [&](const char* name, std::initializer_list<float> values, UINT stride) {
      const int start = offset("shadowmap_PS", name);
      if (start < 0) return;
      UINT at = UINT(start);
      for (const float value : values) {
        constants[at] = value;
        at += stride;
      }
    };
    for (UINT slot = 0; slot < buffers->size(); ++slot) {
      for (auto& value : constants) value = Range(0.02f, 1.f);
      if (has_fog && fog_bind.BindPoint == slot && !fog) std::fill(constants.begin(), constants.end(), 0.f);
      // Every emitter receives the sun's shadow (behaviour_mask.x bits 0x40000 and 0x100000, the shaders' gate) at `receive`
      // (ps_receive_shadows_high_quality_params.x).
      if (has_emitters && emitters.BindPoint == slot) {
        const int start = offset("cb_vfx_emitter_constants_common", "behaviour_mask");
        for (int element = 0; start >= 0 && element < 512; ++element) {
          uint32_t bits = 0;
          std::memcpy(&bits, &constants[start + element * 4], 4);
          bits |= 0x00140000u;
          std::memcpy(&constants[start + element * 4], &bits, 4);
        }
      }
      if (has_emitters_ps && emitters_ps.BindPoint == slot) {
        const int start = offset("cb_vfx_emitter_constants_ps", "ps_receive_shadows_high_quality_params");
        for (int element = 0; start >= 0 && element < 512; ++element) constants[start + element * 4] = receive;
      }
      if (has_shadowmap && shadowmap.BindPoint == slot) {
        set("g_vHardShadowBufferSize", {float(W), float(H), 1.f / W, 1.f / H}, 1);
        set("g_iSplitCount", {4.f}, 1);
        set("g_split_distances", {0.f, 1e6f, 1e6f, 1e6f, 1e6f}, 4);
        set("g_split_uv_texture_bounds", {-1e6f, -1e6f, 1e6f, 1e6f, -1e6f, -1e6f, 1e6f, 1e6f, -1e6f, -1e6f, 1e6f, 1e6f, -1e6f, -1e6f, 1e6f, 1e6f}, 1);
      }
      (*buffers)[slot] = MakeConstants(constants);
    }
    for (UINT i = 0; i < desc.BoundResources; ++i) {
      D3D11_SHADER_INPUT_BIND_DESC bind = {};
      reflection->GetResourceBindingDesc(i, &bind);
      if (bind.Type != D3D_SIT_TEXTURE) continue;
      const bool shadow_map = std::string(bind.Name).find("ShadowBuffer") != std::string::npos;
      (*views)[bind.BindPoint] = MakeTexture(bind.Dimension, 9000 + run * 32 + bind.BindPoint, (shadow_map && failing_map) ? -1e30f : NAN);
    }
  }

  std::vector<float> Draw(ID3D11PixelShader* shader, const std::array<ID3D11Buffer*, 8>& buffers, const std::array<ID3D11ShaderResourceView*, 24>& views,
                          ID3D11ShaderResourceView* occlusion, ID3D11Buffer* layer, bool failing_sampler, UINT width = W, UINT height = H,
                          ID3D11Buffer* settings = nullptr) const {
    ID3D11Buffer* lights = nullptr;
    context->PSSetConstantBuffers(0, 8, buffers.data());
    context->PSSetConstantBuffers(8, 1, &layer);
    context->PSSetConstantBuffers(9, 1, &lights);
    ID3D11Buffer* injected = settings != nullptr ? settings : injection;
    context->PSSetConstantBuffers(13, 1, &injected);
    context->VSSetConstantBuffers(0, 1, buffers.data());
    context->PSSetSamplers(0, 16, (failing_sampler ? never : samplers).data());
    context->PSSetShaderResources(0, 24, views.data());
    context->PSSetShaderResources(49, 1, &occlusion);
    const float clear[4] = {SENTINEL, SENTINEL, SENTINEL, SENTINEL};
    context->ClearRenderTargetView(rtv, clear);
    context->OMSetRenderTargets(1, &rtv, nullptr);
    const D3D11_VIEWPORT viewport = {0.f, 0.f, float(width), float(height), 0.f, 1.f};
    context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs, nullptr, 0);
    context->PSSetShader(shader, nullptr, 0);
    context->Draw(3, 0);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    std::vector<float> out(size_t(W) * H * 4);
    context->CopyResource(staging, target);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    for (UINT y = 0; y < H; ++y) std::memcpy(out.data() + size_t(y) * W * 4, static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, W * 16);
    context->Unmap(staging, 0);
    return out;
  }
};

static ID3D11PixelShader* Shader(const std::vector<uint8_t>& code) {
  ID3D11PixelShader* shader = nullptr;
  if (code.empty() || FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, &shader))) return nullptr;
  return shader;
}

static void Release(std::array<ID3D11Buffer*, 8>* buffers, std::array<ID3D11ShaderResourceView*, 24>* views) {
  for (auto& view : *views) {
    if (view != nullptr) view->Release();
    view = nullptr;
  }
  for (auto& buffer : *buffers) {
    if (buffer != nullptr) buffer->Release();
    buffer = nullptr;
  }
}

struct Build {
  const char* name;
  const char* game;     // the dumped cso
  const char* built;    // the add-on's cso
  Kind kind;
  const char* control;  // CONTROL X: another build's game cso with the same bindings ("" none)
};

static bool Check(const Build& build, const std::string& dump, const std::string& built) {
  const auto game_code = Load(dump + "/" + build.game);
  ID3D11PixelShader* game = Shader(game_code);
  ID3D11PixelShader* replacement = Shader(Load(built + "/" + build.built));
  ID3D11PixelShader* control = build.control[0] != 0 ? Shader(Load(dump + "/" + build.control)) : nullptr;
  Fixture fixture;
  if (game == nullptr || replacement == nullptr || (build.control[0] != 0 && control == nullptr) || !fixture.Make(game_code)) {
    std::printf("%s: shaders not loaded (%s, %s)\n  FAILED\n", build.name, build.game, build.built);
    return false;
  }
  const bool shadowed = build.kind != Kind::PS_MAIN;
  ID3D11ShaderResourceView* full = MakeTexture(D3D_SRV_DIMENSION_TEXTURE2D, 2, 1.f);
  ID3D11ShaderResourceView* right_half = MakeTexture(D3D_SRV_DIMENSION_TEXTURE2D, 3, NAN, int(W / 2));
  ID3D11Buffer* layer = MakeConstants({0.f, 0.f, float(W / 2), float(H / 2), 2.f, 0.f, 0.f, 0.f});
  Tally a, f, b, b_control, c, c_control, d, x;
  uint64_t written = 0, shadow_moves = 0, layer_shadowed = 0, e_moved = 0, e_misses = 0, fog_moves = 0;
  for (int run = 0; run < 12; ++run) {
    std::array<ID3D11Buffer*, 8> buffers = {}, fog_buffers = {}, failing_buffers = {};
    std::array<ID3D11ShaderResourceView*, 24> views = {}, fog_views = {}, failing_views = {};
    fixture.Inputs(run, false, 1.f, false, &buffers, &views);
    fixture.Inputs(run, true, 1.f, false, &fog_buffers, &fog_views);
    fixture.Inputs(run, false, build.kind == Kind::NO_SHADOW_MAP ? 0.f : 1.f, build.kind == Kind::SHADOW_TEXTURE, &failing_buffers, &failing_views);
    const bool failing_sampler = build.kind == Kind::COMPARISON_SAMPLER;
    const auto game_out = fixture.Draw(game, buffers, views, nullptr, nullptr, false);
    const auto replacement_out = fixture.Draw(replacement, buffers, views, nullptr, nullptr, false);
    for (size_t i = 0; i < game_out.size(); ++i) {
      a.Add(replacement_out[i], game_out[i]);
      if (game_out[i] != SENTINEL) ++written;
    }
    const auto game_fog = fixture.Draw(game, fog_buffers, fog_views, nullptr, nullptr, false);
    const auto replacement_fog = fixture.Draw(replacement, fog_buffers, fog_views, nullptr, nullptr, false);
    for (size_t i = 0; i < game_fog.size(); ++i) {
      f.Add(replacement_fog[i], game_fog[i]);
      if (game_fog[i] != game_out[i]) ++fog_moves;
    }
    const auto out_2 = fixture.Draw(replacement, buffers, views, nullptr, nullptr, false, W, H, brighter_2);
    const auto out_3 = fixture.Draw(replacement, buffers, views, nullptr, nullptr, false, W, H, brighter_3);
    for (size_t i = 0; i + 3 < game_out.size(); i += 4) {
      if ((replacement_out[i] == SENTINEL) != (out_3[i] == SENTINEL)) {
        ++e_misses;
        continue;
      }
      if (replacement_out[i] == SENTINEL) continue;
      bool same = true, affine = true;
      for (size_t k = 0; k < 3; ++k) {
        const float scale = std::max({1.f, std::abs(out_3[i + k]), std::abs(replacement_out[i + k])});
        same = same && out_3[i + k] == replacement_out[i + k];
        affine = affine && std::abs(out_3[i + k] - 2.f * out_2[i + k] + replacement_out[i + k]) <= 1e-5f * scale;
      }
      if (!same) ++e_moved;
      if (!affine || out_3[i + 3] != replacement_out[i + 3] || std::isnan(out_3[i])) ++e_misses;
    }
    if (control != nullptr) {
      const auto control_out = fixture.Draw(control, buffers, views, nullptr, nullptr, false);
      for (size_t i = 0; i < game_out.size(); ++i) x.Add(replacement_out[i], control_out[i]);
    }
    if (shadowed) {
      const auto failing_out = fixture.Draw(game, failing_buffers, failing_views, nullptr, nullptr, failing_sampler);
      const auto occluded_out = fixture.Draw(replacement, buffers, views, full, nullptr, false);
      const auto game_occluded = fixture.Draw(game, buffers, views, full, nullptr, false);
      for (size_t i = 0; i < game_out.size(); ++i) {
        b.Add(occluded_out[i], failing_out[i]);
        b_control.Add(game_occluded[i], failing_out[i]);
        if (failing_out[i] != game_out[i]) ++shadow_moves;
      }
      const auto small_game = fixture.Draw(game, buffers, views, nullptr, nullptr, false, W / 2, H / 2);
      const auto small_failing = fixture.Draw(game, failing_buffers, failing_views, nullptr, nullptr, failing_sampler, W / 2, H / 2);
      const auto small_layer = fixture.Draw(replacement, buffers, views, right_half, layer, false, W / 2, H / 2);
      const auto small_unscaled = fixture.Draw(replacement, buffers, views, right_half, nullptr, false, W / 2, H / 2);
      for (size_t i = 0; i < small_game.size(); ++i) {
        const UINT column = UINT((i / 4) % W);
        const float want = column >= W / 4 ? small_failing[i] : small_game[i];
        c.Add(small_layer[i], want);
        c_control.Add(small_unscaled[i], want);
        if (column >= W / 4 && column < W / 2 && small_failing[i] != small_game[i]) ++layer_shadowed;
      }
    } else {
      const auto ignored_out = fixture.Draw(replacement, buffers, views, full, nullptr, false);
      for (size_t i = 0; i < game_out.size(); ++i) d.Add(ignored_out[i], replacement_out[i]);
    }
    Release(&buffers, &views);
    Release(&fog_buffers, &fog_views);
    Release(&failing_buffers, &failing_views);
  }
  std::printf("%s: %s / %s (written %llu of %llu values)\n", build.name, build.game, build.built, (unsigned long long)written, (unsigned long long)a.values);
  a.Print("A unbound vs the game's");
  std::printf("  (fog moves %llu values)\n", (unsigned long long)fog_moves);
  f.Print("F fog on vs the game's");
  std::printf("  E VFX Brightness 1 / 2 / 3: pixels moved %llu (CONTROL > 0); not affine, alpha or discards changed %llu\n",
              (unsigned long long)e_moved, (unsigned long long)e_misses);
  bool ok = a.Exact() && f.Exact() && written > 0 && fog_moves > 0 && e_moved > 0 && e_misses == 0;
  if (shadowed) {
    std::printf("  shadow moves %llu values; in the layer's shadowed half %llu\n", (unsigned long long)shadow_moves, (unsigned long long)layer_shadowed);
    b.Print("B t49 = 1 vs every shadow test failing");
    b_control.Print("B CONTROL the game's with t49 = 1 (misses > 0)");
    c.Print("C half-size layer vs its expectation");
    c_control.Print("C CONTROL b8 unbound (misses > 0)");
    ok = ok && b.Same() && b_control.misses > 0 && c.Same() && c_control.misses > 0 && shadow_moves > 0 && layer_shadowed > 0;
  } else {
    d.Print("D t49 = 1 vs unbound (ignored)");
    ok = ok && d.Exact();
  }
  if (control != nullptr) {
    x.Print((std::string("X CONTROL the game's ") + build.control + " (misses > 0)").c_str());
    ok = ok && x.misses > 0;
  }
  std::printf("  %s\n", ok ? "PASSED" : "FAILED");
  return ok;
}

// The battle fog: A, G, H (above).
static bool CheckFog(const Build& build, const std::string& dump, const std::string& built) {
  const auto game_code = Load(dump + "/" + build.game);
  ID3D11PixelShader* game = Shader(game_code);
  ID3D11PixelShader* replacement = Shader(Load(built + "/" + build.built));
  Fixture fixture;
  if (game == nullptr || replacement == nullptr || !fixture.Make(game_code)) {
    std::printf("%s: shaders not loaded (%s, %s)\n  FAILED\n", build.name, build.game, build.built);
    return false;
  }
  Tally a;
  uint64_t written = 0, rgb_moved = 0, alpha_moved = 0, alpha_up = 0, smooth_moved = 0;
  for (int run = 0; run < 12; ++run) {
    std::array<ID3D11Buffer*, 8> buffers = {};
    std::array<ID3D11ShaderResourceView*, 24> views = {};
    fixture.Inputs(run, true, 1.f, false, &buffers, &views);
    const auto game_out = fixture.Draw(game, buffers, views, nullptr, nullptr, false);
    const auto replacement_out = fixture.Draw(replacement, buffers, views, nullptr, nullptr, false);
    const auto half_out = fixture.Draw(replacement, buffers, views, nullptr, nullptr, false, W, H, fog_half);
    const auto smooth_out = fixture.Draw(replacement, buffers, views, nullptr, nullptr, false, W, H, sky_smooth);
    for (size_t i = 0; i < game_out.size(); ++i) {
      a.Add(replacement_out[i], game_out[i]);
      if (game_out[i] != SENTINEL && game_out[i] != 0.f) ++written;
      if (i % 4 == 3) {
        if (half_out[i] != replacement_out[i]) ++alpha_moved;
        if (half_out[i] > replacement_out[i]) ++alpha_up;
      } else {
        if (std::memcmp(&half_out[i], &replacement_out[i], 4) != 0) ++rgb_moved;
        if (smooth_out[i] != replacement_out[i]) ++smooth_moved;
      }
    }
    Release(&buffers, &views);
  }
  std::printf("%s: %s / %s (written %llu of %llu values)\n", build.name, build.game, build.built, (unsigned long long)written, (unsigned long long)a.values);
  a.Print("A rows at the game's vs the game's");
  std::printf("  G Fog Strength 0.5: rgb moved %llu (0), alpha moved %llu (CONTROL > 0), alpha above A's %llu (0)\n", (unsigned long long)rgb_moved,
              (unsigned long long)alpha_moved, (unsigned long long)alpha_up);
  std::printf("  H Fog & Sky Smoothing Medium: rgb moved %llu (CONTROL > 0)\n", (unsigned long long)smooth_moved);
  const bool ok = a.Same() && written > 0 && rgb_moved == 0 && alpha_moved > 0 && alpha_up == 0 && smooth_moved > 0;
  std::printf("  %s\n", ok ? "PASSED" : "FAILED");
  return ok;
}

// One draw through stream output (nothing rasterized): `vs` with the game's input layout (`layout_code`'s input signature, a
// float4 an input in one vertex buffer: `vertices`), through `hs` / `ds` (patches of three) when given; the last stage's
// `semantic` / `index` as float4s, as many vertices as it wrote.
struct Stages {
  const std::vector<uint8_t>* layout_code;
  const std::vector<uint8_t>* vs;
  const std::vector<uint8_t>* hs = nullptr;
  const std::vector<uint8_t>* ds = nullptr;
  std::array<ID3D11Buffer*, 3> constants = {};  // b0-b2 of every stage
  ID3D11ShaderResourceView* ds_texture = nullptr;
  ID3D11SamplerState* ds_sampler = nullptr;
};

static UINT InputCount(const std::vector<uint8_t>& code) {
  ID3D11ShaderReflection* reflection = nullptr;
  if (FAILED(D3DReflect(code.data(), code.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(&reflection)))) return 0;
  D3D11_SHADER_DESC desc = {};
  reflection->GetDesc(&desc);
  UINT count = 0;
  for (UINT i = 0; i < desc.InputParameters; ++i) {
    D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
    reflection->GetInputParameterDesc(i, &parameter);
    count += parameter.SystemValueType == D3D_NAME_UNDEFINED ? 1 : 0;
  }
  reflection->Release();
  return count;
}

static std::vector<float> StreamOut(const Stages& stages, const std::vector<float>& vertices, D3D11_PRIMITIVE_TOPOLOGY topology, const char* semantic,
                                    UINT index, BYTE components = 4) {
  std::vector<float> out;
  ID3D11ShaderReflection* reflection = nullptr;
  if (FAILED(D3DReflect(stages.layout_code->data(), stages.layout_code->size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(&reflection)))) return out;
  D3D11_SHADER_DESC desc = {};
  reflection->GetDesc(&desc);
  std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
  for (UINT i = 0; i < desc.InputParameters; ++i) {
    D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
    reflection->GetInputParameterDesc(i, &parameter);
    if (parameter.SystemValueType != D3D_NAME_UNDEFINED) continue;
    elements.push_back({parameter.SemanticName, parameter.SemanticIndex, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, UINT(elements.size() * 16),
                        D3D11_INPUT_PER_VERTEX_DATA, 0});
  }
  const UINT stride = UINT(elements.size() * 16);
  const UINT count = UINT(vertices.size() * 4 / stride);
  const auto& last = stages.ds != nullptr ? *stages.ds : *stages.vs;
  const D3D11_SO_DECLARATION_ENTRY entry = {0, semantic, index, 0, components, 0};
  const UINT out_stride = components * 4;
  const UINT room = count * 64;  // a tessellated patch's vertices
  const D3D11_BUFFER_DESC vertex_desc = {UINT(vertices.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  const D3D11_SUBRESOURCE_DATA vertex_data = {vertices.data(), 0, 0};
  const D3D11_BUFFER_DESC out_desc = {room * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_STREAM_OUTPUT, 0, 0, 0};
  const D3D11_BUFFER_DESC read_desc = {room * 16, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
  const D3D11_QUERY_DESC query_desc = {D3D11_QUERY_SO_STATISTICS, 0};
  ID3D11InputLayout* layout = nullptr;
  ID3D11Buffer* vertex_buffer = nullptr;
  ID3D11VertexShader* vs = nullptr;
  ID3D11HullShader* hs = nullptr;
  ID3D11DomainShader* ds = nullptr;
  ID3D11GeometryShader* gs = nullptr;
  ID3D11Buffer* output = nullptr;
  ID3D11Buffer* read = nullptr;
  ID3D11Query* query = nullptr;
  const bool made = SUCCEEDED(device->CreateInputLayout(elements.data(), UINT(elements.size()), stages.layout_code->data(), stages.layout_code->size(), &layout))
                    && SUCCEEDED(device->CreateBuffer(&vertex_desc, &vertex_data, &vertex_buffer))
                    && SUCCEEDED(device->CreateVertexShader(stages.vs->data(), stages.vs->size(), nullptr, &vs))
                    && (stages.hs == nullptr || SUCCEEDED(device->CreateHullShader(stages.hs->data(), stages.hs->size(), nullptr, &hs)))
                    && (stages.ds == nullptr || SUCCEEDED(device->CreateDomainShader(stages.ds->data(), stages.ds->size(), nullptr, &ds)))
                    && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(last.data(), last.size(), &entry, 1, &out_stride, 1, D3D11_SO_NO_RASTERIZED_STREAM,
                                                                             nullptr, &gs))
                    && SUCCEEDED(device->CreateBuffer(&out_desc, nullptr, &output)) && SUCCEEDED(device->CreateBuffer(&read_desc, nullptr, &read))
                    && SUCCEEDED(device->CreateQuery(&query_desc, &query));
  if (made) {
    const UINT offset = 0;
    context->IASetInputLayout(layout);
    context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
    context->IASetPrimitiveTopology(topology);
    context->VSSetShader(vs, nullptr, 0);
    context->HSSetShader(hs, nullptr, 0);
    context->DSSetShader(ds, nullptr, 0);
    context->GSSetShader(gs, nullptr, 0);
    context->PSSetShader(nullptr, nullptr, 0);
    context->VSSetConstantBuffers(0, 3, stages.constants.data());
    context->HSSetConstantBuffers(0, 3, stages.constants.data());
    context->DSSetConstantBuffers(0, 3, stages.constants.data());
    context->DSSetShaderResources(0, 1, &stages.ds_texture);
    context->DSSetSamplers(0, 1, &stages.ds_sampler);
    context->SOSetTargets(1, &output, &offset);
    context->Begin(query);
    context->Draw(count, 0);
    context->End(query);
    context->SOSetTargets(0, nullptr, nullptr);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    D3D11_QUERY_DATA_SO_STATISTICS statistics = {};
    while (context->GetData(query, &statistics, sizeof(statistics), 0) == S_FALSE) {}
    const UINT written = UINT(std::min<UINT64>(statistics.NumPrimitivesWritten * (topology == D3D11_PRIMITIVE_TOPOLOGY_POINTLIST ? 1 : 3), room));
    context->CopyResource(read, output);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context->Map(read, 0, D3D11_MAP_READ, 0, &mapped))) {
      const float* data = static_cast<const float*>(mapped.pData);
      out.assign(data, data + size_t(written) * components);
      context->Unmap(read, 0);
    }
  }
  for (IUnknown* object : std::initializer_list<IUnknown*>{layout, vertex_buffer, vs, hs, ds, gs, output, read, query, reflection}) {
    if (object != nullptr) object->Release();
  }
  return out;
}

// A camera_VS_PS (b0): camera_position, view (c1-c4), projection (c5-c8), view_projection (c9-c12), inv_view (c13-c16), each
// matrix by its rows acting on column vectors (the shaders' dot(r, m._m0i_m1i_m2i_m3i) with column-major registers).
struct Camera {
  std::array<double, 3> position = {};
  std::array<std::array<double, 4>, 4> view = {}, projection = {}, inv_view = {};
  std::vector<float> Constants() const {
    std::vector<float> values(36 * 4, 0.f);
    for (int axis = 0; axis < 3; ++axis) values[axis] = float(position[axis]);
    for (int row = 0; row < 4; ++row) {
      for (int column = 0; column < 4; ++column) {
        double combined = 0.0;
        for (int k = 0; k < 4; ++k) combined += projection[row][k] * view[k][column];
        values[(1 + row) * 4 + column] = float(view[row][column]);
        values[(5 + row) * 4 + column] = float(projection[row][column]);
        values[(9 + row) * 4 + column] = float(combined);
        values[(13 + row) * 4 + column] = float(inv_view[row][column]);
      }
    }
    return values;
  }
};

// A camera at `position` looking along a random direction: view a rotation and translation, inv_view its inverse, a perspective.
static Camera MakeCamera(const std::array<double, 3>& position) {
  Camera camera;
  camera.position = position;
  // A random rotation (Gram-Schmidt of two random vectors).
  std::array<double, 3> forward = {Range(-1.f, 1.f), Range(-1.f, 1.f), Range(-1.f, 1.f)};
  std::array<double, 3> up = {Range(-1.f, 1.f), Range(-1.f, 1.f), Range(-1.f, 1.f)};
  const auto normalize = [](std::array<double, 3>* v) {
    const double length = std::sqrt((*v)[0] * (*v)[0] + (*v)[1] * (*v)[1] + (*v)[2] * (*v)[2]);
    for (double& c : *v) c /= length;
  };
  normalize(&forward);
  const double along = up[0] * forward[0] + up[1] * forward[1] + up[2] * forward[2];
  for (int axis = 0; axis < 3; ++axis) up[axis] -= along * forward[axis];
  normalize(&up);
  const std::array<double, 3> right = {up[1] * forward[2] - up[2] * forward[1], up[2] * forward[0] - up[0] * forward[2], up[0] * forward[1] - up[1] * forward[0]};
  const std::array<std::array<double, 3>, 3> rows = {right, up, forward};
  for (int row = 0; row < 3; ++row) {
    double translation = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
      camera.view[row][axis] = rows[row][axis];
      camera.inv_view[axis][row] = rows[row][axis];
      translation -= rows[row][axis] * position[axis];
    }
    camera.view[row][3] = translation;
    camera.inv_view[row][3] = position[row];
  }
  camera.view[3][3] = 1.0;
  camera.inv_view[3][3] = 1.0;
  const double focal = 1.5, near_plane = 0.5, far_plane = 5000.0;
  camera.projection[0][0] = focal;
  camera.projection[1][1] = focal * 16.0 / 9.0;
  camera.projection[2][2] = far_plane / (far_plane - near_plane);
  camera.projection[2][3] = -near_plane * far_plane / (far_plane - near_plane);
  camera.projection[3][2] = 1.0;
  return camera;
}

// Random rows of a placement (POSITION1-3 / the instance's world rows): turned and scaled within `turn`, moved within `move`.
static std::array<std::array<float, 4>, 3> MakeRows(float turn, float move) {
  std::array<std::array<float, 4>, 3> rows = {};
  for (auto& row : rows) row = {Range(-turn, turn), Range(-turn, turn), Range(-turn, turn), Range(-move, move)};
  return rows;
}

static bool SameBits(const float* a, const float* b, size_t count) { return std::memcmp(a, b, count * 4) == 0; }

// The rays' captures of draws no trace has shown (Rarer Objects in the Rays: addon.cpp SCENE_SHADERS' held_row):
// T. the tessellated rigid 9AEE504E (3-control-point patches; HS 0A3E0133, DS 1C0B3B3A), 64 patches, each its placement:
//    T1 rt_capture_rigid_sm41's TEXCOORD0 (a triangle list, no hull or domain shader: rt_core.hpp SceneShader::patch_triangles)
//       equals the game vertex shader's world position (its TEXCOORD0) bit for bit, w 1;
//    T2 motion_record_rigid_sm41's rows (TEXCOORD0-2) are the vertices' POSITION1-3 bit for bit;
//    T3 the game's whole chain (Tessellation 4, displacement 0.1) with the camera 17 km away (past 20 m: no tessellation, the
//       displacement faded out) streams out the capture's triangles, patch for patch, their corners bit for bit;
//    T4 CONTROL: with the camera among the patches (under 5 m: Tessellation 4 in full, the displacement in full) the chain's
//       triangles leave the capture's plane, by at most the displacement (what the rays leave out within 20 m).
// R. the rope 8D0D26B9: rt_capture_rope_sm3's TEXCOORD0 equals the game's world position (its TEXCOORD5) bit for bit.
// Q. the placed rope 3926A823 (turned to the camera in view space): rt_capture_rope_rigid_sm3's TEXCOORD0 through the game's
//    view and projection is the game's SV_Position within 1e-4 x |w|; CONTROL: its TEXCOORD5 (the rope's middle) is not.
static bool CheckCaptures(const std::string& dump, const std::string& built) {
  const auto rigid_game = Load(dump + "/0x9AEE504E.vs_5_0.cso");
  const auto hull = Load(dump + "/0x0A3E0133.hs_5_0.cso");
  const auto domain = Load(dump + "/0x1C0B3B3A.ds_5_0.cso");
  const auto rigid_capture = Load(built + "/rt_capture_rigid_sm41.cso");
  const auto rigid_rows = Load(built + "/motion_record_rigid_sm41.cso");
  const auto rope_game = Load(dump + "/0x8D0D26B9.vs_4_0.cso");
  const auto rope_capture = Load(built + "/rt_capture_rope_sm3.cso");
  const auto placed_rope_game = Load(dump + "/0x3926A823.vs_4_0.cso");
  const auto placed_rope_capture = Load(built + "/rt_capture_rope_rigid_sm3.cso");
  for (const auto* code : {&rigid_game, &hull, &domain, &rigid_capture, &rigid_rows, &rope_game, &rope_capture, &placed_rope_game, &placed_rope_capture}) {
    if (code->empty()) {
      std::printf("captures: a shader is missing\n  FAILED\n");
      return false;
    }
  }
  bool ok = true;

  // T: 64 patches, each its own placement; POSITION0 and the rows small (the world within 1.25 m of the origin).
  constexpr UINT PATCHES = 64;
  const UINT inputs = InputCount(rigid_game);  // POSITION0, TEXCOORD0, NORMAL0-2, COLOR0, POSITION1-7
  std::vector<float> vertices;
  for (UINT patch = 0; patch < PATCHES; ++patch) {
    const auto rows = MakeRows(0.5f, 0.5f);
    for (int corner = 0; corner < 3; ++corner) {
      for (UINT input = 0; input < inputs; ++input) {
        if (input == 0) {
          vertices.insert(vertices.end(), {Range(-0.5f, 0.5f), Range(-0.5f, 0.5f), Range(-0.5f, 0.5f), 1.f});
        } else if (input >= 6 && input <= 8) {
          vertices.insert(vertices.end(), rows[input - 6].begin(), rows[input - 6].end());
        } else {
          vertices.insert(vertices.end(), {Range(0.f, 1.f), Range(0.f, 1.f), Range(0.f, 1.f), Range(0.f, 1.f)});
        }
      }
    }
  }
  // rigid_config_PS: g_tesselation_factor 4, g_tesselation_displacement_scale 0.1; wind_VS 0. The HS culls patches facing away
  // from cb0[3]: 0 (NaN: none culled).
  ID3D11Buffer* config = MakeConstants({0.f, 0.f, 0.f, 0.f, 0.f, 4.f, 0.1f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f});
  ID3D11Buffer* wind = MakeConstants(std::vector<float>(4, 0.f));
  Camera far_camera = MakeCamera({1e4, 1e4, 1e4});
  Camera near_camera = MakeCamera({0.0, 0.0, 0.0});
  auto far_constants = far_camera.Constants();
  auto near_constants = near_camera.Constants();
  for (int k = 0; k < 4; ++k) far_constants[12 + k] = near_constants[12 + k] = 0.f;
  ID3D11Buffer* far_buffer = MakeConstants(far_constants);
  ID3D11Buffer* near_buffer = MakeConstants(near_constants);
  ID3D11ShaderResourceView* displacement = MakeTexture(D3D_SRV_DIMENSION_TEXTURE2D, 77);
  const D3D11_SAMPLER_DESC sampler_desc = {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP,
                                           0.f, 1, D3D11_COMPARISON_NEVER, {0.f, 0.f, 0.f, 0.f}, 0.f, D3D11_FLOAT32_MAX};
  ID3D11SamplerState* sampler = nullptr;
  device->CreateSamplerState(&sampler_desc, &sampler);
  const Stages game_vs = {.layout_code = &rigid_game, .vs = &rigid_game, .constants = {far_buffer, config, wind}};
  const Stages capture = {.layout_code = &rigid_game, .vs = &rigid_capture, .constants = {far_buffer, config, wind}};
  const Stages record = {.layout_code = &rigid_game, .vs = &rigid_rows, .constants = {far_buffer, config, wind}};
  Stages chain = {.layout_code = &rigid_game, .vs = &rigid_game, .hs = &hull, .ds = &domain, .constants = {far_buffer, config, wind},
                  .ds_texture = displacement, .ds_sampler = sampler};
  const auto captured = StreamOut(capture, vertices, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, "TEXCOORD", 0);
  // The game's TEXCOORD0 is its xyz.
  const auto world = StreamOut(game_vs, vertices, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, "TEXCOORD", 0, 3);
  uint64_t t1_differ = 0, t1_w = 0;
  for (size_t v = 0; v < PATCHES * 3 && captured.size() == PATCHES * 12 && world.size() == PATCHES * 9; ++v) {
    t1_differ += SameBits(&captured[v * 4], &world[v * 3], 3) ? 0 : 1;
    t1_w += captured[v * 4 + 3] == 1.f ? 0 : 1;
  }
  const bool t1 = captured.size() == PATCHES * 12 && world.size() == PATCHES * 9 && t1_differ == 0 && t1_w == 0;
  std::printf("T tessellated rigid 9AEE504E (%u patches)\n  T1 capture vs the game's world position: %zu / %zu vertices, %llu differ, %llu w not 1 (0, 0)\n", PATCHES,
              captured.size() / 4, world.size() / 3, (unsigned long long)t1_differ, (unsigned long long)t1_w);
  uint64_t t2_differ = 0;
  for (UINT row = 0; row < 3; ++row) {
    const auto rows = StreamOut(record, vertices, D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, "TEXCOORD", row);
    for (size_t v = 0; v < PATCHES * 3; ++v) {
      t2_differ += (rows.size() == PATCHES * 12 && SameBits(&rows[v * 4], &vertices[(v * inputs + 6 + row) * 4], 4)) ? 0 : 1;
    }
  }
  std::printf("  T2 rows vs POSITION1-3: %llu of %u differ (0)\n", (unsigned long long)t2_differ, PATCHES * 9);
  const auto far_chain = StreamOut(chain, vertices, D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, "TEXCOORD", 0);
  uint64_t t3_differ = 0;
  // Corner order within a triangle is the tessellator's: compared as sets.
  const auto sorted = [](const float* triangle) {
    std::array<std::array<uint32_t, 3>, 3> corners = {};
    for (int c = 0; c < 3; ++c) std::memcpy(corners[c].data(), triangle + c * 4, 12);
    std::sort(corners.begin(), corners.end());
    return corners;
  };
  for (UINT patch = 0; patch < PATCHES && far_chain.size() == captured.size(); ++patch) {
    t3_differ += sorted(&far_chain[patch * 12]) == sorted(&captured[patch * 12]) ? 0 : 1;
  }
  const bool t3 = far_chain.size() == captured.size() && t3_differ == 0;
  std::printf("  T3 the game's chain 17 km away: %zu triangles (%u), %llu differ from the capture's (0)\n", far_chain.size() / 12, PATCHES,
              (unsigned long long)t3_differ);
  chain.constants[0] = near_buffer;
  const auto near_chain = StreamOut(chain, vertices, D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, "TEXCOORD", 0);
  const size_t per_patch = near_chain.size() / 12 / PATCHES;
  double largest = 0.0;
  uint64_t off_plane = 0;
  for (UINT patch = 0; patch < PATCHES && per_patch > 0 && near_chain.size() == per_patch * PATCHES * 12; ++patch) {
    const float* a = &captured[patch * 12];
    const std::array<double, 3> e1 = {double(a[4]) - a[0], double(a[5]) - a[1], double(a[6]) - a[2]};
    const std::array<double, 3> e2 = {double(a[8]) - a[0], double(a[9]) - a[1], double(a[10]) - a[2]};
    std::array<double, 3> n = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (double& c : n) c /= length;
    for (size_t v = patch * per_patch * 3; v < (patch + 1) * per_patch * 3; ++v) {
      const float* p = &near_chain[v * 4];
      const double distance = std::fabs((p[0] - a[0]) * n[0] + (p[1] - a[1]) * n[1] + (p[2] - a[2]) * n[2]);
      largest = std::max(largest, distance);
      off_plane += distance > 1e-3 ? 1 : 0;
    }
  }
  const bool t4 = per_patch > 1 && near_chain.size() == per_patch * PATCHES * 12 && off_plane > 0 && largest <= 0.1 + 1e-4;
  std::printf("  T4 CONTROL the chain among the patches: %zu triangles a patch (> 1), %llu of %zu corners off the capture's plane (> 0), at most %.4f (<= 0.1)\n",
              per_patch, (unsigned long long)off_plane, near_chain.size() / 4, largest);
  const bool t2 = t2_differ == 0;
  std::printf("  %s\n", t1 && t2 && t3 && t4 ? "PASSED" : "FAILED");
  ok = ok && t1 && t2 && t3 && t4;
  for (IUnknown* object : std::initializer_list<IUnknown*>{config, wind, far_buffer, near_buffer, displacement, sampler}) {
    if (object != nullptr) object->Release();
  }

  // R and Q: 300 vertices a rope, the camera 30-60 m away.
  constexpr UINT ROPE_VERTICES = 300;
  const Camera rope_camera = MakeCamera({Range(30.f, 60.f), Range(5.f, 20.f), Range(-60.f, -30.f)});
  ID3D11Buffer* rope_constants = MakeConstants(rope_camera.Constants());
  for (const bool placed : {false, true}) {
    const auto& game_code = placed ? placed_rope_game : rope_game;
    const UINT rope_inputs = InputCount(game_code);  // POSITION0, NORMAL0, TEXCOORD0-2 (and POSITION1-6 placed)
    std::vector<float> rope;
    for (UINT v = 0; v < ROPE_VERTICES; ++v) {
      const auto rows = MakeRows(1.f, 10.f);
      for (UINT input = 0; input < rope_inputs; ++input) {
        if (input == 0) {
          rope.insert(rope.end(), {Range(-10.f, 10.f), Range(0.f, 10.f), Range(-10.f, 10.f), 1.f});
        } else if (input == 1) {
          rope.insert(rope.end(), {Range(-1.f, 1.f), Range(-1.f, 1.f), Range(-1.f, 1.f), 0.f});
        } else if (input == 3) {
          rope.insert(rope.end(), {Range(0.02f, 0.2f), Range(0.f, 1.f), 0.f, 0.f});  // the width, the uv's v
        } else if (input >= 5 && input <= 7) {
          rope.insert(rope.end(), rows[input - 5].begin(), rows[input - 5].end());
        } else {
          rope.insert(rope.end(), {Range(0.f, 1.f), Range(0.f, 1.f), Range(0.f, 1.f), Range(0.f, 1.f)});
        }
      }
    }
    const Stages game_stages = {.layout_code = &game_code, .vs = &game_code, .constants = {rope_constants}};
    const Stages capture_stages = {.layout_code = &game_code, .vs = placed ? &placed_rope_capture : &rope_capture, .constants = {rope_constants}};
    const auto rope_world = StreamOut(capture_stages, rope, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, "TEXCOORD", 0);
    const auto game_world = StreamOut(game_stages, rope, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, "TEXCOORD", 5);
    const bool counted = rope_world.size() == ROPE_VERTICES * 4 && game_world.size() == ROPE_VERTICES * 4;
    if (!placed) {
      // CONTROL: the game's from another camera (the ribbon turns to it).
      ID3D11Buffer* other_constants = MakeConstants(MakeCamera({-40.0, 30.0, 50.0}).Constants());
      const Stages other_stages = {.layout_code = &game_code, .vs = &game_code, .constants = {other_constants}};
      const auto other_world = StreamOut(other_stages, rope, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, "TEXCOORD", 5);
      other_constants->Release();
      uint64_t differ = 0, control_differ = 0;
      for (UINT v = 0; v < ROPE_VERTICES && counted && other_world.size() == ROPE_VERTICES * 4; ++v) {
        differ += SameBits(&rope_world[v * 4], &game_world[v * 4], 4) ? 0 : 1;
        control_differ += SameBits(&rope_world[v * 4], &other_world[v * 4], 4) ? 0 : 1;
      }
      const bool r = counted && other_world.size() == ROPE_VERTICES * 4 && differ == 0 && control_differ > ROPE_VERTICES / 2;
      std::printf("R rope 8D0D26B9: capture vs the game's world position: %zu vertices, %llu differ (0); CONTROL the game's from another camera: %llu differ (> %u)\n  %s\n",
                  rope_world.size() / 4, (unsigned long long)differ, (unsigned long long)control_differ, ROPE_VERTICES / 2, r ? "PASSED" : "FAILED");
      ok = ok && r;
      continue;
    }
    const auto game_clip = StreamOut(game_stages, rope, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, "SV_Position", 0);
    const auto clip_of = [&rope_camera](const float* world) {
      std::array<double, 4> view = {}, clip = {};
      for (int row = 0; row < 4; ++row) {
        for (int k = 0; k < 4; ++k) view[row] += rope_camera.view[row][k] * double(k < 3 ? world[k] : 1.f);
      }
      for (int row = 0; row < 4; ++row) {
        for (int k = 0; k < 4; ++k) clip[row] += rope_camera.projection[row][k] * view[k];
      }
      return clip;
    };
    uint64_t misses = 0, control_misses = 0;
    for (UINT v = 0; v < ROPE_VERTICES && counted && game_clip.size() == ROPE_VERTICES * 4; ++v) {
      const auto ours = clip_of(&rope_world[v * 4]);
      const auto middle = clip_of(&game_world[v * 4]);
      const double tolerance = 1e-4 * std::max(1.0, std::fabs(double(game_clip[v * 4 + 3])));
      bool miss = false, control_miss = false;
      for (int c = 0; c < 4; ++c) {
        miss = miss || std::fabs(ours[c] - game_clip[v * 4 + c]) > tolerance;
        control_miss = control_miss || std::fabs(middle[c] - game_clip[v * 4 + c]) > tolerance;
      }
      misses += miss ? 1 : 0;
      control_misses += control_miss ? 1 : 0;
    }
    const bool q = counted && game_clip.size() == ROPE_VERTICES * 4 && misses == 0 && control_misses > ROPE_VERTICES / 2;
    std::printf("Q placed rope 3926A823: capture through view and projection vs the game's SV_Position: %llu of %u miss (0); CONTROL the rope's middle: %llu miss (> %u)\n  %s\n",
                (unsigned long long)misses, ROPE_VERTICES, (unsigned long long)control_misses, ROPE_VERTICES / 2, q ? "PASSED" : "FAILED");
    ok = ok && q;
  }
  rope_constants->Release();
  return ok;
}

int main(int argc, char** argv) {
  if (argc < 5) {
    std::printf("usage: tests.exe <the game's dumped csos> <the add-on's built csos> <VFX Brightness's float in b13> <Fog Strength's (-1 none)> [all]\n");
    return 2;
  }
  const std::string dump = argv[1];
  const std::string built = argv[2];
  const int brightness = std::atoi(argv[3]);
  const int fog_strength = std::atoi(argv[4]);
  const bool all = argc > 5 && std::string(argv[5]) == "all";
  D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context))) return 1;
  // VFX Brightness and (Attila's two after it: weather, snow) at 1, Fog Strength 1 and Fog & Sky Smoothing off (the float
  // before Fog Strength is its strength, the one before that the smoothing; shared.h): the game's look. Then the others.
  std::vector<float> settings(256 * 4, 0.f);
  for (int k = 0; k < 3; ++k) settings[brightness + k] = 1.f;
  if (fog_strength >= 2) settings[fog_strength] = 1.f;
  injection = MakeConstants(settings);
  if (fog_strength >= 2) {
    settings[fog_strength] = 0.5f;
    fog_half = MakeConstants(settings);
    settings[fog_strength] = 1.f;
    settings[fog_strength - 2] = 2.f;
    settings[fog_strength - 1] = 1.f;
    sky_smooth = MakeConstants(settings);
    settings[fog_strength - 2] = 0.f;
    settings[fog_strength - 1] = 0.f;
  }
  settings[brightness] = 2.f;
  brighter_2 = MakeConstants(settings);
  settings[brightness] = 3.f;
  brighter_3 = MakeConstants(settings);
  constexpr std::array<Build, 13> BUILDS = {{
      {"vfx00 high quality, gather 5.0, sm_5_0", "0xACE33D5D.ps_5_0.cso", "0xACE33D5D.cso", Kind::COMPARISON_SAMPLER, ""},
      {"high quality, gather 4.1, sm_4_1", "0xF5F73B79.ps_4_1.cso", "0xF5F73B79.cso", Kind::SHADOW_TEXTURE, ""},
      {"high quality, software, sm_5_0", "0x79A71E7B.ps_5_0.cso", "0x79A71E7B.cso", Kind::SHADOW_TEXTURE, "0x905429B6.ps_5_0.cso"},
      {"high quality, software, sm_4_0", "0x1F60E59D.ps_4_0.cso", "0x1F60E59D.cso", Kind::SHADOW_TEXTURE, "0x2F66954B.ps_4_0.cso"},
      {"high quality, pcf, sm_5_0", "0x905429B6.ps_5_0.cso", "0x905429B6.cso", Kind::SHADOW_TEXTURE, "0xDF0F041A.ps_5_0.cso"},
      {"high quality, pcf, sm_4_0", "0x2F66954B.ps_4_0.cso", "0x2F66954B.cso", Kind::SHADOW_TEXTURE, "0x3A12A66D.ps_4_0.cso"},
      {"high quality, fetch4, sm_5_0", "0xDF0F041A.ps_5_0.cso", "0xDF0F041A.cso", Kind::SHADOW_TEXTURE, "0x79A71E7B.ps_5_0.cso"},
      {"high quality, fetch4, sm_4_0", "0x3A12A66D.ps_4_0.cso", "0x3A12A66D.cso", Kind::SHADOW_TEXTURE, "0x1F60E59D.ps_4_0.cso"},
      {"high quality, no shadows, sm_5_0", "0xF9D9D3FB.ps_5_0.cso", "0xF9D9D3FB.cso", Kind::NO_SHADOW_MAP, ""},
      {"high quality, no shadows, sm_4_0", "0xEA5AB372.ps_4_0.cso", "0xEA5AB372.cso", Kind::NO_SHADOW_MAP, ""},
      {"vfx01 ps_main, sm_5_0", "0xF5379F78.ps_5_0.cso", "0xF5379F78.cso", Kind::PS_MAIN, ""},
      {"ps_main, sm_4_0", "0x2CA01137.ps_4_0.cso", "0x2CA01137.cso", Kind::PS_MAIN, ""},
      {"ps_main, gather 4.1, sm_4_1", "0x6DCB7DA0.ps_4_1.cso", "0x6DCB7DA0.cso", Kind::PS_MAIN, ""},
  }};
  int passed = 0, checked = 0;
  for (const Build& build : BUILDS) {
    if (!all && !std::ifstream(built + "/" + build.built).good()) {
      std::printf("%s: not in this add-on (skipped)\n", build.name);
      continue;
    }
    ++checked;
    passed += Check(build, dump, built) ? 1 : 0;
  }
  constexpr std::array<Build, 2> FOG_BUILDS = {{
      {"battlefog00, sm_3_0", "0xA6A0F887.ps_4_0.cso", "0xA6A0F887.cso", Kind::PS_MAIN, ""},
      {"battlefog01 MSAA, sm_4_1", "0xA538288B.ps_4_1.cso", "0xA538288B.cso", Kind::PS_MAIN, ""},
  }};
  for (const Build& build : FOG_BUILDS) {
    if (fog_strength < 2) break;
    if (!all && !std::ifstream(built + "/" + build.built).good()) {
      std::printf("%s: not in this add-on (skipped)\n", build.name);
      continue;
    }
    ++checked;
    passed += CheckFog(build, dump, built) ? 1 : 0;
  }
  if (all || std::ifstream(built + "/rt_capture_rope_sm3.cso").good()) {
    ++checked;
    passed += CheckCaptures(dump, built) ? 1 : 0;
  } else {
    std::printf("captures: not in this add-on (skipped)\n");
  }
  std::printf("%s: %d of %d\n", passed == checked && checked > 0 ? "PASS" : "FAIL", passed, checked);
  return passed == checked && checked > 0 ? 0 : 1;
}
