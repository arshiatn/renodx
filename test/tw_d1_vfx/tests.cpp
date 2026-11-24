// Divinity: Original Sin EE's VFX Brightness (src/games/divinityoriginalsinenhanced/vfx_brightness.hpp) on the game's
// own effect shaders (the census's csos, read by run.ps1 outside the repo), each of EFFECT_HASHES (row x) and
// INDICATOR_HASHES (row y, the markers) patched by Patch with its row:
// 1. The program (D3DDisassemble): the game's with every o0 written into one temporary more (o0 -> rT, dcl_temps + 1),
//    before each ret "mad o0.xyz, rT.xyzx, cb7[0].<row x 4>, rT.xyzx" and "mov o0.w, rT.wwww", and dcl_constantbuffer
//    CB7[1] before dcl_temps; nothing else.
// 2. On WARP (both shaders created; every texture, sampler and constant buffer the game's reflection names filled with
//    fixed noise, 0 - 1, a set with constants 0 - 16, and sets of -8 - 8 until one draws; the interpolants fixed
//    functions of the position): cb7 unbound, both sliders at 1 (cb7 0, 0) and only the other row's slider at 2 the
//    game's bits wherever the game's colour is finite; its own row's slider alone at 2 its rgb twice the game's, exact in
//    float, its alpha the game's bits. Controls: a pixel drawn (not discarded) in each shader, and at 2 the colour off the
//    game's somewhere.
// tests.exe <the census's cso folder>; exit code 0 when every check holds.
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
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "src/games/divinityoriginalsinenhanced/vfx_brightness.hpp"

using Microsoft::WRL::ComPtr;
using Texel = std::array<float, 4>;

static int failures = 0;
#define CHECK(x, ...)                                  \
  do {                                                 \
    if (!(x)) {                                        \
      std::printf("FAIL line %d: %s: ", __LINE__, #x); \
      std::printf(__VA_ARGS__);                        \
      std::printf("\n");                               \
      ++failures;                                      \
    }                                                  \
  } while (false)

constexpr UINT TEXTURE_SIZE = 64;
constexpr float SENTINEL = -12345.f;  // the target's clear: a discarded pixel keeps it

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;

static std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Fixed noise: the same numbers every run.
static float Noise(uint32_t* state) {
  *state = *state * 1664525u + 1013904223u;
  return static_cast<float>(*state >> 8) / 16777216.f;
}

// The disassembly's declarations (0) and instructions (1), comments and the version line left out.
static std::array<std::vector<std::string>, 2> Disassembly(std::span<const uint8_t> code) {
  std::array<std::vector<std::string>, 2> lines;
  ComPtr<ID3DBlob> text;
  if (FAILED(D3DDisassemble(code.data(), code.size(), 0, nullptr, &text))) return lines;
  std::istringstream stream(std::string(static_cast<const char*>(text->GetBufferPointer()), text->GetBufferSize()));
  for (std::string line; std::getline(stream, line);) {
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) continue;
    const std::string trimmed = line.substr(start, line.find_last_not_of(" \r\n\t") - start + 1);
    if (trimmed.empty() || trimmed.starts_with("//") || trimmed.starts_with("ps_") || trimmed[0] == '\0') continue;
    lines[trimmed.starts_with("dcl_") ? 0 : 1].push_back(trimmed);
  }
  return lines;
}

// What one shader reads, from the game's reflection: an interpolant vertex shader and every binding, fixed noise.
struct Inputs {
  ComPtr<ID3D11VertexShader> vertex_shader;
  std::vector<std::pair<UINT, ComPtr<ID3D11ShaderResourceView>>> textures;
  std::vector<std::pair<UINT, ComPtr<ID3D11SamplerState>>> samplers;
  std::vector<std::pair<UINT, ComPtr<ID3D11Buffer>>> constants;
  std::string skipped;
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

static Inputs MakeInputs(std::span<const uint8_t> game, float constant_low, float constant_scale, uint32_t seed) {
  Inputs inputs;
  ComPtr<ID3D11ShaderReflection> reflection;
  if (FAILED(D3DReflect(game.data(), game.size(), IID_PPV_ARGS(&reflection)))) {
    inputs.skipped = "no reflection";
    return inputs;
  }
  D3D11_SHADER_DESC shader_desc = {};
  reflection->GetDesc(&shader_desc);
  std::vector<D3D11_SIGNATURE_PARAMETER_DESC> parameters(shader_desc.InputParameters);
  for (UINT index = 0; index < shader_desc.InputParameters; ++index) reflection->GetInputParameterDesc(index, &parameters[index]);
  std::ranges::sort(parameters, [](const auto& a, const auto& b) {
    return (a.Register != b.Register) ? (a.Register < b.Register) : (std::countr_zero(a.Mask) < std::countr_zero(b.Mask));
  });
  std::string outputs = "float4 position : SV_Position;\n";
  std::string writes;
  int member = 0;
  for (const auto& parameter : parameters) {
    if (parameter.SystemValueType != D3D_NAME_UNDEFINED) continue;
    const int components = std::popcount(static_cast<unsigned>(parameter.Mask));
    const bool integer = parameter.ComponentType != D3D_REGISTER_COMPONENT_FLOAT32;
    const std::string size = (components > 1 ? std::to_string(components) : "");
    outputs += std::format("{}{} v{} : {}{};\n", (integer ? "nointerpolation uint" : "float"), size, member, parameter.SemanticName,
                           parameter.SemanticIndex);
    writes += std::format("o.v{} = ({}{})(float4(uv.x, uv.y, 1.0 - uv.x, uv.y * 0.5 + 0.25) * {:.3f} + {:.3f}).{};\n", member,
                          (integer ? "uint" : "float"), size, 1.f + 0.37f * member, 0.05f * member,
                          std::string("xyzw").substr(0, components));
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
    if (bind.Type == D3D_SIT_TEXTURE) {
      const bool cube = bind.Dimension == D3D_SRV_DIMENSION_TEXTURECUBE;
      if (bind.Dimension != D3D_SRV_DIMENSION_TEXTURE2D && !cube) {
        inputs.skipped = std::format("texture {} of dimension {}", name, static_cast<int>(bind.Dimension));
        return inputs;
      }
      const UINT slices = cube ? 6u : 1u;
      std::vector<float> texels(TEXTURE_SIZE * TEXTURE_SIZE * 4 * slices);
      for (float& texel : texels) texel = Noise(&state);
      std::vector<D3D11_SUBRESOURCE_DATA> init(slices);
      for (UINT slice = 0; slice < slices; ++slice) init[slice] = {&texels[slice * TEXTURE_SIZE * TEXTURE_SIZE * 4], TEXTURE_SIZE * 4 * sizeof(float), 0};
      const D3D11_TEXTURE2D_DESC desc = {TEXTURE_SIZE, TEXTURE_SIZE, 1, slices, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                         D3D11_BIND_SHADER_RESOURCE, 0, (cube ? static_cast<UINT>(D3D11_RESOURCE_MISC_TEXTURECUBE) : 0u)};
      ComPtr<ID3D11Texture2D> texture;
      ComPtr<ID3D11ShaderResourceView> view;
      device->CreateTexture2D(&desc, init.data(), &texture);
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = DXGI_FORMAT_R32G32B32A32_FLOAT};
      if (cube) {
        view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        view_desc.TextureCube = {.MostDetailedMip = 0, .MipLevels = 1};
      } else {
        view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view_desc.Texture2D = {.MostDetailedMip = 0, .MipLevels = 1};
      }
      device->CreateShaderResourceView(texture.Get(), &view_desc, &view);
      inputs.textures.emplace_back(bind.BindPoint, view);
    } else if (bind.Type == D3D_SIT_SAMPLER) {
      const bool point = name.find("Point") != std::string::npos;
      const bool comparison = (bind.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0;
      const auto address = (name.find("Wrap") != std::string::npos) ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
      D3D11_FILTER filter = point ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      if (comparison) filter = point ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT : D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
      const D3D11_SAMPLER_DESC desc = {.Filter = filter, .AddressU = address, .AddressV = address, .AddressW = address,
                                       .ComparisonFunc = (comparison ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_NEVER),
                                       .MaxLOD = D3D11_FLOAT32_MAX};
      ComPtr<ID3D11SamplerState> sampler;
      device->CreateSamplerState(&desc, &sampler);
      inputs.samplers.emplace_back(bind.BindPoint, sampler);
    } else if (bind.Type == D3D_SIT_CBUFFER) {
      D3D11_SHADER_BUFFER_DESC buffer_desc = {};
      reflection->GetConstantBufferByName(bind.Name)->GetDesc(&buffer_desc);
      std::vector<float> values(buffer_desc.Size / 4);
      for (float& value : values) value = constant_low + Noise(&state) * constant_scale;
      inputs.constants.emplace_back(bind.BindPoint, MakeConstants(values.data(), buffer_desc.Size));
    } else {
      inputs.skipped = std::format("binding {} of type {}", name, static_cast<int>(bind.Type));
      return inputs;
    }
  }
  return inputs;
}

// `brightness`: cb7[0] (null: cb7 unbound).
static std::vector<Texel> Draw(ID3D11PixelShader* shader, const Inputs& inputs, const Texel* brightness) {
  const D3D11_TEXTURE2D_DESC target_desc = {TEXTURE_SIZE, TEXTURE_SIZE, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> target_view;
  device->CreateTexture2D(&target_desc, nullptr, &target);
  device->CreateRenderTargetView(target.Get(), nullptr, &target_view);
  context->ClearState();
  const FLOAT clear[4] = {SENTINEL, SENTINEL, SENTINEL, SENTINEL};
  context->ClearRenderTargetView(target_view.Get(), clear);
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  const D3D11_VIEWPORT viewport = {0, 0, TEXTURE_SIZE, TEXTURE_SIZE, 0, 1};
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(inputs.vertex_shader.Get(), nullptr, 0);
  context->PSSetShader(shader, nullptr, 0);
  for (const auto& [slot, view] : inputs.textures) context->PSSetShaderResources(slot, 1, view.GetAddressOf());
  for (const auto& [slot, sampler] : inputs.samplers) context->PSSetSamplers(slot, 1, sampler.GetAddressOf());
  for (const auto& [slot, buffer] : inputs.constants) context->PSSetConstantBuffers(slot, 1, buffer.GetAddressOf());
  if (brightness != nullptr) {
    const auto buffer = MakeConstants(brightness->data(), sizeof(Texel));
    context->PSSetConstantBuffers(divinity::vfx_brightness::SLOT, 1, buffer.GetAddressOf());
  }
  context->Draw(3, 0);
  const D3D11_TEXTURE2D_DESC staging_desc = {TEXTURE_SIZE, TEXTURE_SIZE, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&staging_desc, nullptr, &staging);
  context->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<Texel> texels(TEXTURE_SIZE * TEXTURE_SIZE);
  for (UINT row = 0; row < TEXTURE_SIZE; ++row) {
    std::memcpy(&texels[row * TEXTURE_SIZE], static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch, TEXTURE_SIZE * sizeof(Texel));
  }
  context->Unmap(staging.Get(), 0);
  return texels;
}

int main(int argc, char** argv) {
  if (argc < 2 || FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) {
    std::printf("FAIL: usage (tests.exe <census cso folder>) or no WARP device\n");
    return 1;
  }
  const std::filesystem::path folder = argv[1];
  const std::string slot = std::to_string(divinity::vfx_brightness::SLOT);
  size_t checked = 0;
  size_t pixels_total = 0;
  size_t infinite_total = 0;
  std::vector<std::pair<uint32_t, uint32_t>> shaders;  // hash, row
  for (const uint32_t hash : divinity::vfx_brightness::EFFECT_HASHES) shaders.emplace_back(hash, 0u);
  for (const uint32_t hash : divinity::vfx_brightness::INDICATOR_HASHES) shaders.emplace_back(hash, 1u);
  std::array<size_t, 2> checked_rows = {};
  for (const auto [hash, row] : shaders) {
    const std::string name = std::format("0x{:08X}", hash);
    CHECK(divinity::vfx_brightness::Row(hash) == static_cast<int>(row), "%s: in one list only", name.c_str());
    const auto game = ReadFile(folder / (name + ".ps_5_0.cso"));
    CHECK(!game.empty(), "%s: the game's cso", name.c_str());
    if (game.empty()) continue;
    const auto patched = divinity::vfx_brightness::Patch(game, row);
    CHECK(!patched.empty(), "%s: not patched", name.c_str());
    if (patched.empty()) continue;

    // 1. The program.
    const auto before = Disassembly(game);
    const auto after = Disassembly(patched);
    std::vector<std::string> declarations;
    std::string temp;
    for (const std::string& line : before[0]) {
      if (line.starts_with("dcl_temps ")) {
        temp = "r" + line.substr(10);
        declarations.push_back("dcl_constantbuffer CB" + slot + "[1], immediateIndexed");
        declarations.push_back("dcl_temps " + std::to_string(std::stoi(line.substr(10)) + 1));
      } else {
        declarations.push_back(line);
      }
    }
    std::vector<std::string> instructions;
    for (std::string line : before[1]) {
      for (size_t at = line.find("o0."); at != std::string::npos; at = line.find("o0.", at + temp.size())) line.replace(at, 2, temp);
      if (line == "ret") {
        instructions.push_back("mad o0.xyz, " + temp + ".xyzx, cb" + slot + "[0]." + std::string(4, "xy"[row]) + ", " + temp + ".xyzx");
        instructions.push_back("mov o0.w, " + temp + ".wwww");
      }
      instructions.push_back(line);
    }
    CHECK(!temp.empty() && after[0] == declarations, "%s: declarations (the game's, cb7, dcl_temps + 1)", name.c_str());
    const auto [expected_line, patched_line] = std::ranges::mismatch(instructions, after[1]);
    CHECK(after[1] == instructions, "%s: instructions (the game's with o0 -> %s, the brightness before each ret): '%s' where '%s'",
          name.c_str(), temp.c_str(), (patched_line == after[1].end() ? "(end)" : patched_line->c_str()),
          (expected_line == instructions.end() ? "(end)" : expected_line->c_str()));

    // 2. On WARP.
    ComPtr<ID3D11PixelShader> game_shader;
    ComPtr<ID3D11PixelShader> patched_shader;
    CHECK(SUCCEEDED(device->CreatePixelShader(game.data(), game.size(), nullptr, &game_shader)), "%s: the game's shader", name.c_str());
    CHECK(SUCCEEDED(device->CreatePixelShader(patched.data(), patched.size(), nullptr, &patched_shader)), "%s: the patched shader",
          name.c_str());
    if (game_shader == nullptr || patched_shader == nullptr) continue;
    size_t drawn = 0;
    size_t off_unbound = 0;
    size_t off_one = 0;
    size_t off_other = 0;
    size_t off_two = 0;
    size_t changed_two = 0;
    size_t infinite = 0;
    // Constants 0 - 1 and 0 - 16, then sets of -8 - 8 (decals: inside their box and facing their normal), at least two,
    // more until one draws and changes at 2 (at most 64).
    for (uint32_t set = 0; set < 66 && (set < 4 || drawn == 0 || changed_two == 0); ++set) {
      const Inputs inputs = MakeInputs(game, (set >= 2 ? -8.f : 0.f), (set == 0 ? 1.f : 16.f), (0x1234567u + set * 0x9E3779B9u) ^ hash);
      CHECK(inputs.skipped.empty(), "%s: not drawn (%s)", name.c_str(), inputs.skipped.c_str());
      if (!inputs.skipped.empty()) break;
      constexpr Texel AT_ONE = {0.f, 0.f, 0.f, 0.f};
      Texel at_two = AT_ONE;  // its own row's slider at 2
      at_two[row] = 1.f;
      Texel other_at_two = AT_ONE;  // the other row's
      other_at_two[1 - row] = 1.f;
      const auto reference = Draw(game_shader.Get(), inputs, nullptr);
      const auto unbound = Draw(patched_shader.Get(), inputs, nullptr);
      const auto one = Draw(patched_shader.Get(), inputs, &AT_ONE);
      const auto other = Draw(patched_shader.Get(), inputs, &other_at_two);
      const auto two = Draw(patched_shader.Get(), inputs, &at_two);
      for (size_t i = 0; i < reference.size(); ++i) {
        const Texel& game_texel = reference[i];
        if (game_texel[3] != SENTINEL || game_texel[0] != SENTINEL) ++drawn;
        // x 0 + x: an infinite colour becomes NaN (the game's colour never is: counted, not checked).
        if (!std::isfinite(game_texel[0]) || !std::isfinite(game_texel[1]) || !std::isfinite(game_texel[2])) {
          ++infinite;
          continue;
        }
        off_unbound += (std::memcmp(&unbound[i], &game_texel, sizeof(Texel)) != 0) ? 1 : 0;
        off_one += (std::memcmp(&one[i], &game_texel, sizeof(Texel)) != 0) ? 1 : 0;
        off_other += (std::memcmp(&other[i], &game_texel, sizeof(Texel)) != 0) ? 1 : 0;
        const bool discarded = game_texel[0] == SENTINEL && game_texel[3] == SENTINEL;
        bool doubled = std::bit_cast<uint32_t>(two[i][3]) == std::bit_cast<uint32_t>(game_texel[3]);
        for (int c = 0; c < 3; ++c) {
          const float expected = discarded ? SENTINEL : game_texel[c] * 2.f;
          doubled = doubled && std::bit_cast<uint32_t>(two[i][c]) == std::bit_cast<uint32_t>(expected);
          if (two[i][c] != game_texel[c]) ++changed_two;
        }
        off_two += doubled ? 0 : 1;
      }
    }
    CHECK(drawn > 0, "%s: CONTROL: no pixel drawn (every input set discards)", name.c_str());
    CHECK(off_unbound == 0 && off_one == 0 && off_other == 0, "%s: %zu pixels off the game's with cb7 unbound, %zu at 1, %zu with the other row at 2",
          name.c_str(), off_unbound, off_one, off_other);
    CHECK(off_two == 0, "%s: %zu pixels at 2 not rgb x 2 / the game's alpha", name.c_str(), off_two);
    CHECK(changed_two > 0, "%s: CONTROL: at 2 nothing changed", name.c_str());
    ++checked;
    ++checked_rows[row];
    pixels_total += drawn;
    infinite_total += infinite;
  }
  std::printf("VFX Brightness: %zu of %zu effect shaders (%zu of %zu, markers %zu of %zu) patched and checked (%zu pixels drawn, %zu with an "
              "infinite game colour left out)\n",
              checked, shaders.size(), checked_rows[0], divinity::vfx_brightness::EFFECT_HASHES.size(), checked_rows[1],
              divinity::vfx_brightness::INDICATOR_HASHES.size(), pixels_total, infinite_total);
  std::printf(failures == 0 ? "tw_d1_vfx: all checks passed\n" : "tw_d1_vfx: %d checks failed\n", failures);
  return failures == 0 ? 0 : 1;
}
