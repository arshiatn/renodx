/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// PT DI step 4a, W1 (designs/pt_di_rtxdi.md 2.1, 6 W1, R2.5): src/games/twattila/tw_point_light.hlsli against each of
// Attila's 40 point-light pixel shaders, the game's own DXBC (cmd_Decompiler's dump of data.pack) run on the same
// inputs, D3D11 WARP. 64 x 48 pixels a draw, a full-screen triangle; random G-buffer (some texels at exactly 0 / 1),
// SSAO, light (inside the pixels' reach), colour, camera and inverse view-projection; the point shadow map lit
// everywhere (visibility 1).
//   W1 fxc   point_light_ps.hlsl at the variant's own target (fxc /O3), every pixel the variant's model (stencil & 3):
//            4 seeds x HDR on / off; bit-exact on every rgba value (both NaN counts as equal), else at most 2 ulp
//            with the instruction named (no row needs it today). Shadow variants: the hard path (g_shadow_soft_edges
//            0); the soft path (PCF) is printed as information (its lit sum need not be exactly 1).
//   W1 dxc   point_light_cs.hlsl (dxc cs_6_5 -O3, D3D12 WARP, the helper's compiler) on records of what the pixel
//            probe read, against the same game outputs (rgb): the same bar. The include spells out the DXBC's mad()s
//            and acos and makes its result precise: dxc's Acos and fast fmul / fadd gave up to 8e8 ulp without them.
//   model    per slm / skinlm pair, a random stencil & 3 per pixel: 1 = the slm shader's value, 3 = the skinlm one's,
//            0 / 2 = rgb 0 (both draws stencilled out), bit-exact.
//   static   fxc ps_4_0 / ps_4_1 / ps_5_0 / cs_5_0 and dxc cs_6_5 / cs_6_9 compile the include without warnings.
//   table    each variant's DXBC binds s_ssao exactly when the table says slm (the table's model is the shader's).
// Controls (each must change the light on lit pixels; CONTROL lines): the falloff kind swapped (every variant), the
// albedo squared on skinlm (Revision 1 S6, the 20 skinlm variants), the model swapped (every variant).
// tests.exe <repository> <the dump's cso folder> <folder of dxcompiler.dll and dxil.dll>; exit 0 when every gate passes
// and every control fails.
#include <windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT W = 64;
constexpr UINT H = 48;
constexpr size_t PIXELS = size_t(W) * H;
constexpr uint32_t SEEDS = 4;
constexpr uint32_t GROUP = 64;
constexpr uint32_t MODEL_SLM = 1;
constexpr uint32_t MODEL_SKIN = 3;
constexpr uint32_t BAR_ULP = 2;

// The 40 (shaders/pointlightNN_<hash>.<target>.hlsl; names.tsv's fxc names): "shadow" = the point-shadow family's
// shadowed draw (the gather ones sample the game's point shadow map), lowdef = the draw's output saturated.
struct Variant {
  int index;
  uint32_t hash;
  const char* target;
  bool linear_falloff;
  bool skin;
  bool low_def;
  bool shadow;
};

constexpr std::array<Variant, 40> VARIANTS = {{
    {.index = 0, .hash = 0x6426B042, .target = "ps_4_0", .linear_falloff = false, .skin = false, .low_def = false, .shadow = false},
    {.index = 1, .hash = 0x90981DA1, .target = "ps_4_0", .linear_falloff = false, .skin = true, .low_def = false, .shadow = false},
    {.index = 2, .hash = 0x89FF812E, .target = "ps_4_0", .linear_falloff = false, .skin = false, .low_def = true, .shadow = false},
    {.index = 3, .hash = 0xFD3BF1A8, .target = "ps_4_0", .linear_falloff = false, .skin = true, .low_def = true, .shadow = false},
    {.index = 4, .hash = 0x593989DE, .target = "ps_4_0", .linear_falloff = true, .skin = false, .low_def = false, .shadow = false},
    {.index = 5, .hash = 0x0D7A1213, .target = "ps_4_0", .linear_falloff = true, .skin = true, .low_def = false, .shadow = false},
    {.index = 6, .hash = 0xB8F368CF, .target = "ps_4_0", .linear_falloff = true, .skin = false, .low_def = true, .shadow = false},
    {.index = 7, .hash = 0xC9E4E52D, .target = "ps_4_0", .linear_falloff = true, .skin = true, .low_def = true, .shadow = false},
    {.index = 8, .hash = 0xF59F5A63, .target = "ps_4_1", .linear_falloff = true, .skin = true, .low_def = false, .shadow = false},
    {.index = 9, .hash = 0x80A99678, .target = "ps_4_1", .linear_falloff = true, .skin = true, .low_def = true, .shadow = false},
    {.index = 10, .hash = 0xC15B836C, .target = "ps_4_1", .linear_falloff = true, .skin = false, .low_def = false, .shadow = false},
    {.index = 11, .hash = 0xC499C463, .target = "ps_4_1", .linear_falloff = true, .skin = false, .low_def = true, .shadow = false},
    {.index = 12, .hash = 0x448E583F, .target = "ps_4_1", .linear_falloff = true, .skin = true, .low_def = false, .shadow = true},
    {.index = 13, .hash = 0x3DD3CC25, .target = "ps_4_1", .linear_falloff = true, .skin = true, .low_def = true, .shadow = true},
    {.index = 14, .hash = 0x2F02F050, .target = "ps_4_1", .linear_falloff = true, .skin = false, .low_def = false, .shadow = true},
    {.index = 15, .hash = 0xEA5A7C4F, .target = "ps_4_1", .linear_falloff = true, .skin = false, .low_def = true, .shadow = true},
    {.index = 16, .hash = 0xE521F992, .target = "ps_4_1", .linear_falloff = false, .skin = true, .low_def = false, .shadow = false},
    {.index = 17, .hash = 0x27D78FB0, .target = "ps_4_1", .linear_falloff = false, .skin = true, .low_def = true, .shadow = false},
    {.index = 18, .hash = 0xD0F7E8B9, .target = "ps_4_1", .linear_falloff = false, .skin = false, .low_def = false, .shadow = false},
    {.index = 19, .hash = 0xA932AFD8, .target = "ps_4_1", .linear_falloff = false, .skin = false, .low_def = true, .shadow = false},
    {.index = 20, .hash = 0x42375E7D, .target = "ps_4_1", .linear_falloff = false, .skin = true, .low_def = false, .shadow = true},
    {.index = 21, .hash = 0x0CF2D4D9, .target = "ps_4_1", .linear_falloff = false, .skin = true, .low_def = true, .shadow = true},
    {.index = 22, .hash = 0x181545DF, .target = "ps_4_1", .linear_falloff = false, .skin = false, .low_def = false, .shadow = true},
    {.index = 23, .hash = 0xA7D5B0B1, .target = "ps_4_1", .linear_falloff = false, .skin = false, .low_def = true, .shadow = true},
    {.index = 24, .hash = 0x43CA2116, .target = "ps_5_0", .linear_falloff = true, .skin = true, .low_def = false, .shadow = false},
    {.index = 25, .hash = 0xBB678513, .target = "ps_5_0", .linear_falloff = true, .skin = true, .low_def = true, .shadow = false},
    {.index = 26, .hash = 0x463D51CA, .target = "ps_5_0", .linear_falloff = true, .skin = false, .low_def = false, .shadow = false},
    {.index = 27, .hash = 0x194C988B, .target = "ps_5_0", .linear_falloff = true, .skin = false, .low_def = true, .shadow = false},
    {.index = 28, .hash = 0x507F23D6, .target = "ps_5_0", .linear_falloff = true, .skin = true, .low_def = false, .shadow = true},
    {.index = 29, .hash = 0x819D9051, .target = "ps_5_0", .linear_falloff = true, .skin = true, .low_def = true, .shadow = true},
    {.index = 30, .hash = 0xD18997A5, .target = "ps_5_0", .linear_falloff = true, .skin = false, .low_def = false, .shadow = true},
    {.index = 31, .hash = 0xB5C224DF, .target = "ps_5_0", .linear_falloff = true, .skin = false, .low_def = true, .shadow = true},
    {.index = 32, .hash = 0x2CDF370B, .target = "ps_5_0", .linear_falloff = false, .skin = true, .low_def = false, .shadow = false},
    {.index = 33, .hash = 0xA2EDBD29, .target = "ps_5_0", .linear_falloff = false, .skin = true, .low_def = true, .shadow = false},
    {.index = 34, .hash = 0xD17728DD, .target = "ps_5_0", .linear_falloff = false, .skin = false, .low_def = false, .shadow = false},
    {.index = 35, .hash = 0x490DFC4B, .target = "ps_5_0", .linear_falloff = false, .skin = false, .low_def = true, .shadow = false},
    {.index = 36, .hash = 0xC1FE2079, .target = "ps_5_0", .linear_falloff = false, .skin = true, .low_def = false, .shadow = true},
    {.index = 37, .hash = 0x134C5AA8, .target = "ps_5_0", .linear_falloff = false, .skin = true, .low_def = true, .shadow = true},
    {.index = 38, .hash = 0xE48A600D, .target = "ps_5_0", .linear_falloff = false, .skin = false, .low_def = false, .shadow = true},
    {.index = 39, .hash = 0x5C3798EA, .target = "ps_5_0", .linear_falloff = false, .skin = false, .low_def = true, .shadow = true},
}};

std::string Name(const Variant& variant) {
  char text[96];
  std::snprintf(text, sizeof(text), "pointlight%02d 0x%08X %s %s%s %s %s", variant.index, variant.hash, variant.target,
                variant.linear_falloff ? "linear" : "smooth", variant.shadow ? " shadow" : "",
                variant.skin ? "skinlm" : "slm", variant.low_def ? "lowdef" : "highdef");
  return text;
}

// One draw's inputs: textures (rgba32f, W x H), the light, and every constant by its reflected name (32-bit words).
struct Inputs {
  std::array<std::vector<float>, 4> channels;
  std::vector<float> ssao;
  std::vector<uint32_t> models;
  std::array<float, 4> light = {};
  std::array<float, 4> colour = {};
  std::map<std::string, std::vector<uint32_t>> constants;
};

std::vector<uint32_t> Words(std::initializer_list<float> values) {
  std::vector<uint32_t> words;
  for (const float value : values) words.push_back(std::bit_cast<uint32_t>(value));
  return words;
}

Inputs MakeInputs(uint32_t seed, bool hdr, bool soft_shadows) {
  std::mt19937 rng(seed * 7919u + 17u);
  const auto unit = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
  // A texel value in [lo, hi], or (1 in 16) exactly lo or hi.
  const auto texel = [&](float lo, float hi) {
    const uint32_t pick = rng() % 32u;
    if (pick == 0u) return lo;
    if (pick == 1u) return hi;
    return unit(lo, hi);
  };
  Inputs inputs;
  for (int channel = 0; channel < 4; ++channel) {
    auto& data = inputs.channels[channel];
    data.resize(PIXELS * 4);
    for (size_t i = 0; i < data.size(); ++i) {
      data[i] = texel(0.f, 1.f);
      if (channel == 3) data[i] = unit(0.05f, 0.95f);  // .x the depth
    }
  }
  inputs.ssao.resize(PIXELS * 4);
  for (float& value : inputs.ssao) value = texel(0.f, 1.f);
  inputs.models.resize(PIXELS);
  for (uint32_t& model : inputs.models) model = rng() % 4u;
  // World = (ndc x 2.0, ndc y 1.5, depth x 2) + offsets, a little skewed, w near 1 (row j: dp4 of (ndc, depth, 1)).
  const std::array<float, 3> offset = {unit(-1.f, 1.f), unit(-1.f, 1.f), unit(-0.5f, 0.5f)};
  const std::vector<uint32_t> matrix = Words({2.f, unit(-0.1f, 0.1f), unit(-0.1f, 0.1f), offset[0],
                                              unit(-0.1f, 0.1f), 1.5f, unit(-0.1f, 0.1f), offset[1],
                                              unit(-0.1f, 0.1f), unit(-0.1f, 0.1f), 2.f, offset[2],
                                              unit(-0.02f, 0.02f), unit(-0.02f, 0.02f), unit(-0.02f, 0.02f), 1.f});
  // The light over a random pixel's world point, reach 1.5-4 m (most pixels inside, some outside).
  const float ndc_x = unit(-0.6f, 0.6f);
  const float ndc_y = unit(-0.6f, 0.6f);
  inputs.light = {2.f * ndc_x + offset[0] + unit(-0.5f, 0.5f), 1.5f * ndc_y + offset[1] + unit(-0.5f, 0.5f),
                  1.f + offset[2] - unit(0.4f, 1.6f), unit(1.5f, 4.f)};
  inputs.colour = {unit(0.f, 3.f), unit(0.f, 3.f), unit(0.f, 3.f), 1.f};
  inputs.constants["camera_position"] = Words({unit(-1.f, 1.f), unit(-1.f, 1.f), unit(-4.f, -2.f)});
  inputs.constants["inv_view_projection"] = matrix;
  inputs.constants["g_screen_size"] = Words({float(W), float(H), 1.f / float(W), 1.f / float(H)});
  inputs.constants["g_vpos_texel_offset"] = Words({0.f});
  inputs.constants["g_viewport_offset_scale"] = Words({0.f, 0.f, 1.f, 1.f});
  inputs.constants["g_hdr_on"] = Words({hdr ? 1.f : 0.f});
  inputs.constants["g_shadow_buffer_size"] = Words({32.f, 32.f, 1.f / 32.f, 1.f / 32.f});
  inputs.constants["g_shadow_soft_edges"] = Words({soft_shadows ? 1.f : 0.f});
  inputs.constants["g_light_proj_zw"] = Words({1.001f, -0.1f});
  inputs.constants["g_point_light_shadow_map_sample_bias"] = Words({0.001f});
  // The probe's: the sun texture's texel 2 w (rt_sun.cs_5_0.hlsl: 0.004 with g_hdr_on, else 1).
  inputs.constants["probe_diffuse_scale"] = Words({hdr ? 0.004f : 1.f});
  return inputs;
}

ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;

// Draws `ps` (its reflection `code`) over the W x H target(s) with `inputs` bound by name; one rgba32f vector per
// render target the shader writes.
std::vector<std::vector<float>> Draw(ID3D11PixelShader* ps, ID3DBlob* code, const Inputs& inputs) {
  ComPtr<ID3D11ShaderReflection> reflection;
  if (FAILED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())))) return {};
  D3D11_SHADER_DESC shader_desc = {};
  reflection->GetDesc(&shader_desc);
  // A vertex shader feeding the pixel shader's own inputs: POSITION2 the light, POSITION1 its colour (flat), else 0.
  std::string outputs;
  std::string body;
  const auto float4_text = [](const std::array<float, 4>& v) {
    std::ostringstream text;
    text.precision(9);
    text << "float4(" << v[0] << "," << v[1] << "," << v[2] << "," << v[3] << ")";
    return text.str();
  };
  for (UINT i = 0; i < shader_desc.InputParameters; ++i) {
    D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
    reflection->GetInputParameterDesc(i, &parameter);
    const std::string semantic = parameter.SemanticName;
    if (semantic == "SV_Position" || semantic == "SV_POSITION") continue;
    const std::string name = "o" + std::to_string(i);
    outputs += ", out float4 " + name + " : " + semantic + std::to_string(parameter.SemanticIndex);
    std::string value = "float4(0,0,0,0)";
    if (semantic == "POSITION" && parameter.SemanticIndex == 2) value = float4_text(inputs.light);
    if (semantic == "POSITION" && parameter.SemanticIndex == 1) value = float4_text(inputs.colour);
    body += " " + name + " = " + value + ";";
  }
  const std::string source = "void main(uint id : SV_VertexID, out float4 p : SV_Position" + outputs
                             + ") { p = float4(float2((id << 1) & 2, id & 2) * float2(2, -2) + float2(-1, 1), 0.5, 1);"
                             + body + " }";
  ComPtr<ID3DBlob> vs_code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source.c_str(), source.size(), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs_code, &errors))) {
    std::printf("FAIL setup: vertex shader %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return {};
  }
  ComPtr<ID3D11VertexShader> vs;
  device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &vs);
  context->ClearState();
  std::vector<ComPtr<ID3D11Buffer>> buffers;
  std::vector<ComPtr<ID3D11ShaderResourceView>> views;
  std::vector<ComPtr<ID3D11SamplerState>> samplers;
  const auto texture = [&](DXGI_FORMAT format, UINT width, UINT height, UINT layers, const void* data, UINT pitch,
                           bool array) {
    std::vector<D3D11_SUBRESOURCE_DATA> initial(layers);
    for (UINT l = 0; l < layers; ++l) {
      initial[l] = {.pSysMem = static_cast<const uint8_t*>(data) + (size_t(pitch) * height * l), .SysMemPitch = pitch};
    }
    const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = layers,
                                       .Format = format, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                       .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    ComPtr<ID3D11Texture2D> made;
    device->CreateTexture2D(&desc, initial.data(), &made);
    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = format};
    if (array) {
      view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
      view_desc.Texture2DArray = {.MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = 0, .ArraySize = layers};
    } else {
      view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      view_desc.Texture2D = {.MostDetailedMip = 0, .MipLevels = 1};
    }
    device->CreateShaderResourceView(made.Get(), &view_desc, views.emplace_back().GetAddressOf());
    return views.back().Get();
  };
  // The point shadow map: 32 x 32 x 6, lit everywhere (the depth far past every pixel's).
  const std::vector<float> lit(32 * 32 * 6, 3.4e38f);
  for (UINT index = 0; index < shader_desc.BoundResources; ++index) {
    D3D11_SHADER_INPUT_BIND_DESC bind = {};
    reflection->GetResourceBindingDesc(index, &bind);
    const std::string name = bind.Name;
    if (bind.Type == D3D_SIT_CBUFFER) {
      ID3D11ShaderReflectionConstantBuffer* buffer = reflection->GetConstantBufferByName(bind.Name);
      D3D11_SHADER_BUFFER_DESC desc = {};
      buffer->GetDesc(&desc);
      std::vector<uint32_t> data(desc.Size / 4, 0u);
      for (UINT v = 0; v < desc.Variables; ++v) {
        ID3D11ShaderReflectionVariable* variable = buffer->GetVariableByIndex(v);
        D3D11_SHADER_VARIABLE_DESC variable_desc = {};
        variable->GetDesc(&variable_desc);
        if (const auto found = inputs.constants.find(variable_desc.Name); found != inputs.constants.end()) {
          std::copy_n(found->second.begin(), std::min(found->second.size(), size_t(variable_desc.Size / 4)),
                      data.begin() + variable_desc.StartOffset / 4);
        }
      }
      const D3D11_BUFFER_DESC buffer_desc = {.ByteWidth = desc.Size, .Usage = D3D11_USAGE_DEFAULT,
                                             .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
      const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data.data()};
      device->CreateBuffer(&buffer_desc, &initial, buffers.emplace_back().GetAddressOf());
      context->PSSetConstantBuffers(bind.BindPoint, 1, buffers.back().GetAddressOf());
    } else if (bind.Type == D3D_SIT_SAMPLER) {
      D3D11_SAMPLER_DESC desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                 .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                 .MaxLOD = D3D11_FLOAT32_MAX};
      if ((bind.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0) {
        desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
      }
      device->CreateSamplerState(&desc, samplers.emplace_back().GetAddressOf());
      context->PSSetSamplers(bind.BindPoint, 1, samplers.back().GetAddressOf());
    } else if (bind.Type == D3D_SIT_TEXTURE) {
      ID3D11ShaderResourceView* view = nullptr;
      if (name.find("shadow_map") != std::string::npos) {
        view = texture(DXGI_FORMAT_R32_FLOAT, 32, 32, 6, lit.data(), 32 * 4, true);
      } else if (name == "probe_models") {
        view = texture(DXGI_FORMAT_R32_UINT, W, H, 1, inputs.models.data(), W * 4, false);
      } else if (name.find("ssao") != std::string::npos) {
        view = texture(DXGI_FORMAT_R32G32B32A32_FLOAT, W, H, 1, inputs.ssao.data(), W * 16, false);
      } else {
        for (int channel = 0; channel < 4; ++channel) {
          if (name.find("channel_" + std::to_string(channel + 1)) == std::string::npos) continue;
          view = texture(DXGI_FORMAT_R32G32B32A32_FLOAT, W, H, 1, inputs.channels[channel].data(), W * 16, false);
        }
      }
      if (view == nullptr) {
        std::printf("FAIL setup: no texture for %s\n", name.c_str());
        return {};
      }
      context->PSSetShaderResources(bind.BindPoint, 1, &view);
    }
  }
  const UINT target_count = shader_desc.OutputParameters;
  const D3D11_TEXTURE2D_DESC target_desc = {.Width = W, .Height = H, .MipLevels = 1, .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1},
                                            .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
  std::vector<ComPtr<ID3D11Texture2D>> targets(target_count);
  std::vector<ComPtr<ID3D11RenderTargetView>> target_views(target_count);
  std::vector<ID3D11RenderTargetView*> bound(target_count);
  const float clear[4] = {-7.f, -7.f, -7.f, -7.f};
  for (UINT t = 0; t < target_count; ++t) {
    device->CreateTexture2D(&target_desc, nullptr, &targets[t]);
    device->CreateRenderTargetView(targets[t].Get(), nullptr, &target_views[t]);
    context->ClearRenderTargetView(target_views[t].Get(), clear);
    bound[t] = target_views[t].Get();
  }
  context->VSSetShader(vs.Get(), nullptr, 0);
  context->PSSetShader(ps, nullptr, 0);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D11_VIEWPORT viewport = {.Width = float(W), .Height = float(H), .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(target_count, bound.data(), nullptr);
  context->Draw(3, 0);
  context->OMSetRenderTargets(0, nullptr, nullptr);
  std::vector<std::vector<float>> results(target_count);
  D3D11_TEXTURE2D_DESC read_desc = target_desc;
  read_desc.Usage = D3D11_USAGE_STAGING;
  read_desc.BindFlags = 0;
  read_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  for (UINT t = 0; t < target_count; ++t) {
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&read_desc, nullptr, &staging);
    context->CopyResource(staging.Get(), targets[t].Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return {};
    results[t].resize(PIXELS * 4);
    for (UINT y = 0; y < H; ++y) {
      std::memcpy(results[t].data() + size_t(y) * W * 4, static_cast<uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch, W * 16);
    }
    context->Unmap(staging.Get(), 0);
  }
  return results;
}

// Monotonic key of a float's bits (+0 and -0 the same), for ulp distances.
int64_t OrderKey(float value) {
  const uint32_t bits = std::bit_cast<uint32_t>(value);
  return ((bits & 0x80000000u) != 0) ? -int64_t(bits & 0x7FFFFFFFu) : int64_t(bits);
}

struct Distance {
  size_t values = 0;
  size_t apart = 0;  // bits differ (both NaN: equal)
  uint64_t max_ulp = 0;
  size_t lit = 0;  // pixels with a non-zero rgb in the game's output

  void Add(float game, float ours) {
    ++values;
    if (std::bit_cast<uint32_t>(game) == std::bit_cast<uint32_t>(ours) || (std::isnan(game) && std::isnan(ours))) return;
    ++apart;
    const uint64_t ulp = (std::isnan(game) || std::isnan(ours)) ? UINT64_MAX : uint64_t(std::llabs(OrderKey(game) - OrderKey(ours)));
    max_ulp = std::max(max_ulp, ulp);
  }
  void Add(const Distance& other) {
    values += other.values;
    apart += other.apart;
    max_ulp = std::max(max_ulp, other.max_ulp);
    lit += other.lit;
  }
  [[nodiscard]] std::string Text() const {
    char text[128];
    if (apart == 0) {
      std::snprintf(text, sizeof(text), "BIT-EXACT (%zu values)", values);
    } else if (max_ulp == UINT64_MAX) {
      std::snprintf(text, sizeof(text), "%zu of %zu apart, a NaN against a number", apart, values);
    } else {
      std::snprintf(text, sizeof(text), "%zu of %zu apart, <= %llu ulp", apart, values, static_cast<unsigned long long>(max_ulp));
    }
    return text;
  }
};

// rgba of every pixel (`rgb_only`: rgb), the game's against ours.
Distance Compare(const std::vector<float>& game, const std::vector<float>& ours, bool rgb_only) {
  Distance distance;
  for (size_t pixel = 0; pixel < PIXELS; ++pixel) {
    const float* g = &game[pixel * 4];
    const float* o = &ours[pixel * 4];
    distance.lit += (g[0] != 0.f || g[1] != 0.f || g[2] != 0.f) ? 1 : 0;
    for (int channel = 0; channel < (rgb_only ? 3 : 4); ++channel) distance.Add(g[channel], o[channel]);
  }
  return distance;
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string Format(const char* format, auto... values) {
  char text[512];
  std::snprintf(text, sizeof(text), format, values...);
  return text;
}

// The compute probe's record (point_light_cs.hlsl ProbeRecord).
struct ProbeRecord {
  std::array<float, 4> channel_1;
  std::array<float, 4> channel_2;
  std::array<float, 4> channel_3;
  std::array<float, 4> position_ssao;
  std::array<float, 4> light;
  std::array<float, 4> colour_scale;
  std::array<float, 4> camera_position;
  uint32_t model;
  uint32_t linear_falloff;
  uint32_t low_def;
  uint32_t padding;
};
static_assert(sizeof(ProbeRecord) == 128);

}  // namespace

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc < 4) {
    std::printf("usage: tests.exe <repository> <the dump's cso folder> <folder of dxcompiler.dll and dxil.dll>\n");
    return 2;
  }
  const std::filesystem::path repository = argv[1];
  const std::filesystem::path cso_folder = argv[2];
  const std::string dxc_folder = argv[3];
  int gates_failed = 0;
  int controls_passed = 0;
  const auto gate = [&](const std::string& what, bool pass, const std::string& detail) {
    std::printf("%s %s: %s\n", pass ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    gates_failed += pass ? 0 : 1;
  };
  const auto control = [&](const std::string& what, bool failed, const std::string& detail) {
    std::printf("CONTROL %s %s: %s\n", failed ? "FAILS as it must" : "PASSED (wrong)", what.c_str(), detail.c_str());
    controls_passed += failed ? 0 : 1;
  };
  const auto setup_failed = [&](const char* what) {
    std::printf("FAIL setup: %s\n", what);
    return 1;
  };
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) {
    return setup_failed("no D3D11 WARP device");
  }
  const std::filesystem::path probe_ps = repository / "test" / "tw_point_light" / "point_light_ps.hlsl";
  const std::filesystem::path probe_cs = repository / "test" / "tw_point_light" / "point_light_cs.hlsl";

  // fxc: the pixel probe at the three targets (/O3), the compute probe at cs_5_0; no warnings.
  std::map<std::string, std::pair<ComPtr<ID3DBlob>, ComPtr<ID3D11PixelShader>>> probes;
  for (const char* target : {"ps_4_0", "ps_4_1", "ps_5_0", "cs_5_0"}) {
    const bool compute = std::string(target) == "cs_5_0";
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    const HRESULT compiled = D3DCompileFromFile((compute ? probe_cs : probe_ps).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                                "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    const std::string message = (errors != nullptr) ? static_cast<const char*>(errors->GetBufferPointer()) : "";
    gate(std::string("static fxc ") + target + " /O3 compiles without warnings", SUCCEEDED(compiled) && message.empty(),
         message.empty() ? "clean" : message);
    if (FAILED(compiled)) return setup_failed("fxc probe");
    if (compute) continue;
    ComPtr<ID3D11PixelShader> ps;
    if (FAILED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps))) return setup_failed("probe pixel shader");
    probes[target] = {code, ps};
  }

  // The game's 40.
  std::vector<std::pair<ComPtr<ID3DBlob>, ComPtr<ID3D11PixelShader>>> games(VARIANTS.size());
  for (const Variant& variant : VARIANTS) {
    const std::filesystem::path path = cso_folder / Format("0x%08X.%s.cso", variant.hash, variant.target);
    const std::vector<uint8_t> bytes = ReadFile(path);
    ComPtr<ID3DBlob> code;
    ComPtr<ID3D11PixelShader> ps;
    if (bytes.empty() || FAILED(D3DCreateBlob(bytes.size(), &code))) return setup_failed("a game shader's DXBC");
    std::memcpy(code->GetBufferPointer(), bytes.data(), bytes.size());
    if (FAILED(device->CreatePixelShader(bytes.data(), bytes.size(), nullptr, &ps))) return setup_failed("a game pixel shader");
    ComPtr<ID3D11ShaderReflection> reflection;
    D3DReflect(bytes.data(), bytes.size(), IID_PPV_ARGS(&reflection));
    D3D11_SHADER_INPUT_BIND_DESC ssao = {};
    const bool binds_ssao = reflection != nullptr && SUCCEEDED(reflection->GetResourceBindingDescByName("s_ssao", &ssao));
    gate("table " + Name(variant), binds_ssao == !variant.skin, binds_ssao ? "binds s_ssao (slm)" : "no s_ssao (skinlm)");
    games[variant.index] = {code, ps};
  }

  // W1 fxc, its controls, and the dxc records.
  struct Run {
    uint32_t seed;
    bool hdr;
  };
  std::vector<Run> runs;
  for (uint32_t seed = 1; seed <= SEEDS; ++seed) {
    for (const bool hdr : {true, false}) runs.push_back({.seed = seed, .hdr = hdr});
  }
  std::vector<Inputs> run_inputs;
  for (const Run& run : runs) run_inputs.push_back(MakeInputs(run.seed, run.hdr, false));
  // game_outputs[variant][run]: target 0.
  std::vector<std::vector<std::vector<float>>> game_outputs(VARIANTS.size());
  // The dxc records' position / SSAO, light and colour per run (the probe's targets 1-3).
  std::vector<std::vector<std::vector<float>>> read_back(runs.size());
  std::vector<Distance> fxc_distances(VARIANTS.size());
  std::vector<std::string> soft_text(VARIANTS.size());
  std::vector<std::string> control_text(VARIANTS.size());
  const auto probe_constants = [](Inputs inputs, uint32_t model, bool linear_falloff, bool low_def, bool squared_albedo) {
    inputs.constants["probe_model"] = {model};
    inputs.constants["probe_linear"] = {linear_falloff ? 1u : 0u};
    inputs.constants["probe_low_def"] = {low_def ? 1u : 0u};
    inputs.constants["probe_squared_albedo"] = {squared_albedo ? 1u : 0u};
    return inputs;
  };
  for (const Variant& variant : VARIANTS) {
    const auto& [game_code, game_ps] = games[variant.index];
    const auto& [probe_code, probe_ps] = probes[variant.target];
    const uint32_t model = variant.skin ? MODEL_SKIN : MODEL_SLM;
    Distance total;
    for (size_t r = 0; r < runs.size(); ++r) {
      const auto game = Draw(game_ps.Get(), game_code.Get(), run_inputs[r]);
      const auto ours = Draw(probe_ps.Get(), probe_code.Get(),
                             probe_constants(run_inputs[r], model, variant.linear_falloff, variant.low_def, false));
      if (game.size() != 1 || ours.size() != 4) return setup_failed("a draw");
      total.Add(Compare(game[0], ours[0], false));
      game_outputs[variant.index].push_back(game[0]);
      if (read_back[r].empty()) read_back[r] = {ours[1], ours[2], ours[3]};
    }
    fxc_distances[variant.index] = total;
    const bool within = total.apart == 0 || (total.max_ulp <= BAR_ULP);
    gate("W1 fxc " + Name(variant), within && total.lit > total.values / 16,
         total.Text() + Format(", %.0f%% of pixels lit", 100.0 * double(total.lit) / double(total.values / 4)));
    if (variant.shadow) {
      // The soft path (PCF over the lit map), information only.
      Distance soft;
      for (uint32_t seed = 1; seed <= 2; ++seed) {
        const Inputs inputs = MakeInputs(seed, true, true);
        const auto game = Draw(game_ps.Get(), game_code.Get(), inputs);
        const auto ours = Draw(probe_ps.Get(), probe_code.Get(),
                               probe_constants(inputs, model, variant.linear_falloff, variant.low_def, false));
        if (game.size() != 1 || ours.size() != 4) return setup_failed("a draw");
        soft.Add(Compare(game[0], ours[0], false));
      }
      soft_text[variant.index] = soft.Text();
      std::printf("INFO soft shadows (PCF, lit map) %s: %s\n", Name(variant).c_str(), soft.Text().c_str());
    }
    // Controls on run 0's inputs (rgb).
    const auto& game = game_outputs[variant.index][0];
    const auto control_draw = [&](uint32_t control_model, bool linear_falloff, bool squared_albedo) {
      const auto ours = Draw(probe_ps.Get(), probe_code.Get(),
                             probe_constants(run_inputs[0], control_model, linear_falloff, variant.low_def, squared_albedo));
      return Compare(game, ours.empty() ? std::vector<float>(PIXELS * 4, 0.f) : ours[0], true);
    };
    const Distance swapped_kind = control_draw(model, !variant.linear_falloff, false);
    control("kind swapped " + Name(variant), swapped_kind.apart > 0, swapped_kind.Text());
    const Distance swapped_model = control_draw(variant.skin ? MODEL_SLM : MODEL_SKIN, variant.linear_falloff, false);
    control("model swapped " + Name(variant), swapped_model.apart > 0, swapped_model.Text());
    control_text[variant.index] = Format("kind %zu, model %zu", swapped_kind.apart, swapped_model.apart);
    if (variant.skin) {
      const Distance squared = control_draw(model, variant.linear_falloff, true);
      control("albedo squared on skinlm " + Name(variant), squared.apart > 0, squared.Text());
      control_text[variant.index] += Format(", squared albedo %zu", squared.apart);
    }
  }

  // The model: one draw of ours with stencil & 3 at random per pixel against the slm / skinlm pair.
  for (const Variant& slm : VARIANTS) {
    if (slm.skin) continue;
    const auto twin = std::find_if(VARIANTS.begin(), VARIANTS.end(), [&](const Variant& other) {
      return other.skin && other.linear_falloff == slm.linear_falloff && other.low_def == slm.low_def
             && other.shadow == slm.shadow && std::string(other.target) == slm.target;
    });
    if (twin == VARIANTS.end()) return setup_failed("an slm variant's skinlm twin");
    const auto& [probe_code, probe_ps] = probes[slm.target];
    Distance pair;
    size_t zero_apart = 0;
    for (size_t r = 0; r < 2; ++r) {
      const auto ours = Draw(probe_ps.Get(), probe_code.Get(), probe_constants(run_inputs[r], 0u, slm.linear_falloff, slm.low_def, false));
      if (ours.size() != 4) return setup_failed("a draw");
      for (size_t pixel = 0; pixel < PIXELS; ++pixel) {
        const uint32_t model = run_inputs[r].models[pixel];
        for (int channel = 0; channel < 3; ++channel) {
          const float value = ours[0][pixel * 4 + channel];
          if (model == MODEL_SLM) {
            pair.Add(game_outputs[slm.index][r][pixel * 4 + channel], value);
          } else if (model == MODEL_SKIN) {
            pair.Add(game_outputs[twin->index][r][pixel * 4 + channel], value);
          } else {
            zero_apart += std::bit_cast<uint32_t>(value) != 0u ? 1 : 0;
          }
        }
      }
    }
    gate(Format("model stencil & 3 pointlight%02d / pointlight%02d", slm.index, twin->index), pair.apart == 0 && zero_apart == 0,
         pair.Text() + Format("; stencil 0 / 2: %zu rgb values not 0", zero_apart));
  }

  // dxc (-O3 as the helper's build.ps1): cs_6_9 (the helper's OMM builds) compiled, cs_6_5 run on D3D12 WARP over the
  // records of every run x the 8 (model, falloff, lowdef) combinations.
  std::vector<std::string> dxc_text(VARIANTS.size(), "-");
  {
    if (LoadLibraryA((dxc_folder + "\\dxil.dll").c_str()) == nullptr) return setup_failed("no dxil.dll");
    const HMODULE dxc_module = LoadLibraryA((dxc_folder + "\\dxcompiler.dll").c_str());
    if (dxc_module == nullptr) return setup_failed("no dxcompiler.dll");
    const auto dxc_create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dxc_module, "DxcCreateInstance"));
    if (dxc_create == nullptr) return setup_failed("no DxcCreateInstance");
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    ComPtr<IDxcIncludeHandler> includes;
    ComPtr<IDxcBlobEncoding> source;
    if (FAILED(dxc_create(CLSID_DxcUtils, IID_PPV_ARGS(&utils))) || FAILED(dxc_create(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))
        || FAILED(utils->CreateDefaultIncludeHandler(&includes))) {
      return setup_failed("dxc instances");
    }
    if (FAILED(utils->LoadFile(probe_cs.c_str(), nullptr, &source))) return setup_failed("dxc probe source");
    const DxcBuffer source_buffer = {.Ptr = source->GetBufferPointer(), .Size = source->GetBufferSize(), .Encoding = DXC_CP_ACP};
    const auto compile = [&](const wchar_t* target, const char* name) {
      std::vector<LPCWSTR> arguments = {probe_cs.c_str(), L"-T", target, L"-E", L"main", L"-O3"};
      ComPtr<IDxcResult> result;
      HRESULT status = E_FAIL;
      ComPtr<IDxcBlob> object;
      if (FAILED(compiler->Compile(&source_buffer, arguments.data(), static_cast<UINT32>(arguments.size()), includes.Get(), IID_PPV_ARGS(&result)))
          || FAILED(result->GetStatus(&status))) {
        std::printf("FAIL setup: dxc %s compile call\n", name);
        return object;
      }
      ComPtr<IDxcBlobUtf8> errors;
      result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
      const std::string error_text = (errors != nullptr && errors->GetStringLength() > 0) ? errors->GetStringPointer() : "";
      gate(std::string("static dxc ") + name + " -O3 compiles without warnings", SUCCEEDED(status) && error_text.empty(),
           error_text.empty() ? "clean" : error_text);
      if (SUCCEEDED(status)) result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
      return object;
    };
    if (compile(L"cs_6_9", "cs_6_9") == nullptr) return setup_failed("dxc cs_6_9");
    const ComPtr<IDxcBlob> object = compile(L"cs_6_5", "cs_6_5");
    if (object == nullptr) return setup_failed("dxc cs_6_5");

    // Records: run r, combination c (bit 0 skinlm, bit 1 linear, bit 2 lowdef), pixel p at ((r * 8 + c) * PIXELS + p).
    std::vector<ProbeRecord> records(runs.size() * 8 * PIXELS);
    for (size_t r = 0; r < runs.size(); ++r) {
      const Inputs& inputs = run_inputs[r];
      for (uint32_t c = 0; c < 8; ++c) {
        for (size_t p = 0; p < PIXELS; ++p) {
          ProbeRecord& record = records[(r * 8 + c) * PIXELS + p];
          std::memcpy(record.channel_1.data(), &inputs.channels[0][p * 4], 16);
          std::memcpy(record.channel_2.data(), &inputs.channels[1][p * 4], 16);
          std::memcpy(record.channel_3.data(), &inputs.channels[2][p * 4], 16);
          std::memcpy(record.position_ssao.data(), &read_back[r][0][p * 4], 16);
          std::memcpy(record.light.data(), &read_back[r][1][p * 4], 16);
          std::memcpy(record.colour_scale.data(), &read_back[r][2][p * 4], 16);
          record.colour_scale[3] = std::bit_cast<float>(inputs.constants.at("probe_diffuse_scale")[0]);
          for (int i = 0; i < 3; ++i) record.camera_position[i] = std::bit_cast<float>(inputs.constants.at("camera_position")[i]);
          record.model = ((c & 1u) != 0) ? MODEL_SKIN : MODEL_SLM;
          record.linear_falloff = (c >> 1) & 1u;
          record.low_def = (c >> 2) & 1u;
        }
      }
    }
    static_assert((size_t(W) * H) % GROUP == 0);

    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device12;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))
        || FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device12)))) {
      return setup_failed("no D3D12 WARP device");
    }
    const std::array<D3D12_ROOT_PARAMETER, 2> parameters = {{
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV, .Descriptor = {.ShaderRegister = 0, .RegisterSpace = 0}},
        {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 0, .RegisterSpace = 0}},
    }};
    const D3D12_ROOT_SIGNATURE_DESC root_desc = {.NumParameters = static_cast<UINT>(parameters.size()), .pParameters = parameters.data()};
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> root_errors;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &root_errors))
        || FAILED(device12->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root)))) {
      return setup_failed("D3D12 root signature");
    }
    const D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {
        .pRootSignature = root.Get(), .CS = {.pShaderBytecode = object->GetBufferPointer(), .BytecodeLength = object->GetBufferSize()}};
    if (FAILED(device12->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline)))) return setup_failed("D3D12 pipeline");
    const auto buffer = [&](size_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {
      const D3D12_HEAP_PROPERTIES heap_properties = {.Type = heap};
      const D3D12_RESOURCE_DESC desc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, .Width = size, .Height = 1, .DepthOrArraySize = 1,
                                        .MipLevels = 1, .SampleDesc = {.Count = 1}, .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR, .Flags = flags};
      ComPtr<ID3D12Resource> resource;
      device12->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource));
      return resource;
    };
    const size_t input_size = records.size() * sizeof(ProbeRecord);
    const size_t output_size = records.size() * 16;
    const ComPtr<ID3D12Resource> upload = buffer(input_size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    const ComPtr<ID3D12Resource> output = buffer(output_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const ComPtr<ID3D12Resource> readback = buffer(output_size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    if (upload == nullptr || output == nullptr || readback == nullptr) return setup_failed("D3D12 buffers");
    void* mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, &mapped))) return setup_failed("D3D12 upload map");
    std::memcpy(mapped, records.data(), input_size);
    upload->Unmap(0, nullptr);
    ComPtr<ID3D12CommandQueue> queue;
    const D3D12_COMMAND_QUEUE_DESC queue_desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    if (FAILED(device12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)))
        || FAILED(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))
        || FAILED(device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_PPV_ARGS(&list)))
        || FAILED(device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
      return setup_failed("D3D12 queue, allocator, list or fence");
    }
    list->SetComputeRootSignature(root.Get());
    list->SetComputeRootShaderResourceView(0, upload->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
    list->Dispatch(static_cast<UINT>(records.size() / GROUP), 1, 1);
    const D3D12_RESOURCE_BARRIER barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                                            .Transition = {.pResource = output.Get(), .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                                           .StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                           .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE}};
    list->ResourceBarrier(1, &barrier);
    list->CopyResource(readback.Get(), output.Get());
    if (FAILED(list->Close())) return setup_failed("D3D12 list close");
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    if (FAILED(queue->Signal(fence.Get(), 1)) || FAILED(fence->SetEventOnCompletion(1, nullptr))) return setup_failed("D3D12 fence wait");
    if (FAILED(device12->GetDeviceRemovedReason())) return setup_failed("D3D12 WARP device removed");
    std::vector<float> lights(records.size() * 4);
    const D3D12_RANGE read_range = {.Begin = 0, .End = output_size};
    if (FAILED(readback->Map(0, &read_range, &mapped))) return setup_failed("D3D12 readback map");
    std::memcpy(lights.data(), mapped, output_size);
    const D3D12_RANGE written = {.Begin = 0, .End = 0};
    readback->Unmap(0, &written);

    for (const Variant& variant : VARIANTS) {
      const uint32_t c = (variant.skin ? 1u : 0u) | (variant.linear_falloff ? 2u : 0u) | (variant.low_def ? 4u : 0u);
      Distance total;
      for (size_t r = 0; r < runs.size(); ++r) {
        const std::vector<float> ours(lights.begin() + ptrdiff_t((r * 8 + c) * PIXELS * 4), lights.begin() + ptrdiff_t((r * 8 + c + 1) * PIXELS * 4));
        total.Add(Compare(game_outputs[variant.index][r], ours, true));
      }
      dxc_text[variant.index] = total.Text();
      gate("W1 dxc cs_6_5 (D3D12 WARP, rgb) " + Name(variant), total.apart == 0 || total.max_ulp <= BAR_ULP, total.Text());
    }
  }

  std::printf("\n| # | hash | target | variant | W1 fxc (WARP) | dxc cs_6_5 (WARP, rgb) | soft PCF | controls apart |\n|---|---|---|---|---|---|---|---|\n");
  for (const Variant& variant : VARIANTS) {
    std::printf("| %02d | 0x%08X | %s | %s%s %s %s | %s | %s | %s | %s |\n", variant.index, variant.hash, variant.target,
                variant.linear_falloff ? "linear" : "smooth", variant.shadow ? " shadow" : "", variant.skin ? "skinlm" : "slm",
                variant.low_def ? "lowdef" : "highdef", fxc_distances[variant.index].Text().c_str(), dxc_text[variant.index].c_str(),
                soft_text[variant.index].empty() ? "-" : soft_text[variant.index].c_str(), control_text[variant.index].c_str());
  }
  const bool all_ok = gates_failed == 0 && controls_passed == 0;
  std::printf("\n%s: %d gate(s) failed, %d control(s) passed that must fail\n", all_ok ? "ALL OK" : "NOT OK", gates_failed, controls_passed);
  return all_ok ? 0 : 1;
}
