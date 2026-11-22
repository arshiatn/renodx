// view_share.hpp's Patch against a base commit's (run.ps1 -Base, default atn-mods): every pixel shader of the seven
// view_share games' dumps (Attila, Thrones of Britannia, Rome II, Warhammer, Warhammer II, Troy, Pharaoh) whose patched bytes
// differ, each game's settings as its add-on installs them (whole textures, viewport chains, target sizes, its
// RENDER_SCALE_READS region slots). G20: the contact march's texel addresses, a uv projected back from a rebuilt NDC, the
// water's refraction distortion.
// For each changed shader, on WARP (a W x H view; 33%: the view top-left of a 3W x 3H target, cb12 1/3):
// 1. 100%: the new patch with cb12 unbound gives the game's bits.
// 2. The runtime's patches on top (screen_space_shadows' Patch / PatchAmbient on the sun and forward rows, water_effects'
//    caustics / absorption / scale line on the water rows) take the new code as they took the old (same steps), and at 100%
//    the chain on the new code gives the old chain's bits.
// 3. 33% against the game at 100% (beyond 8 px of the view's edges, 1e-5 relative): the new patch is no further apart than
//    the base's (the CONTROL: the base's own count, listed per shader). Render-space textures (G-buffer, depth, scene: by
//    name, and the region slots) random pictures top-left of a 3x texture, their edge texels beyond; whole textures and the
//    rest the same random picture at both scales; integers 1, other constants random, matrices identity-like.
// Classes: FIXED (base apart, new 0), BETTER, SAME, WORSE (a failure).
// CHANGES_GAME=<folder>: that game alone; CHANGES_HASH=<hash>: that shader alone, both disassemblies printed.
// tests.exe <repo> <dumps folder>
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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <regex>
#include <span>
#include <string>
#include <vector>

#include "ambient_patch.h"

using Microsoft::WRL::ComPtr;
using Texel = std::array<float, 4>;

constexpr UINT W = 64, H = 48, S = 3, EDGE = 8;
static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static ComPtr<ID3D11SamplerState> point_clamp, comparison;
static int failures = 0;

static std::vector<uint8_t> ReadBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}

static std::string ReadText(const std::filesystem::path& path) {
  const auto bytes = ReadBytes(path);
  return {bytes.begin(), bytes.end()};
}

// The text of `name = {{ ... }};` in `text` (empty when not there).
static std::string Block(const std::string& text, const std::string& name) {
  const size_t at = text.find(name + " = {");
  if (at == std::string::npos) return {};
  const size_t end = text.find("}};", at);
  return text.substr(at, end - at + 3);
}

// Each `{<prefix>...}` entry of a block, its braces matched.
static std::vector<std::string> Entries(const std::string& block, const std::string& prefix) {
  std::vector<std::string> entries;
  for (size_t at = block.find(prefix); at != std::string::npos; at = block.find(prefix, at + 1)) {
    int depth = 0;
    size_t end = at;
    for (; end < block.size(); ++end) {
      depth += (block[end] == '{') ? 1 : (block[end] == '}') ? -1 : 0;
      if (depth == 0) break;
    }
    entries.push_back(block.substr(at, end - at + 1));
  }
  return entries;
}

static uint32_t Field(const std::string& entry, const std::string& name, uint32_t fallback) {
  std::smatch match;
  if (std::regex_search(entry, match, std::regex("\\." + name + " = (0x[0-9A-Fa-f]+|0b[01]+|\\d+|true|false)"))) {
    const std::string value = match[1];
    if (value == "true") return 1;
    if (value == "false") return 0;
    if (value.starts_with("0b")) return static_cast<uint32_t>(std::stoull(value.substr(2), nullptr, 2));
    return static_cast<uint32_t>(std::stoull(value, nullptr, 0));
  }
  return fallback;
}

static uint32_t Hash(const std::string& entry) {
  std::smatch match;
  std::regex_search(entry, match, std::regex("0x([0-9A-Fa-f]{8})"));
  return static_cast<uint32_t>(std::stoul(match[1], nullptr, 16));
}

static ComPtr<ID3D11ShaderResourceView> Texture(const std::vector<Texel>& texels, UINT width, UINT height, UINT layers = 1, bool cube = false,
                                                bool single = false) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = layers,
                                     .Format = single ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {1, 0},
                                     .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                     .MiscFlags = cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0u};
  std::vector<float> singles;
  if (single) {
    for (const auto& texel : texels) singles.push_back(texel[0]);
  }
  std::vector<D3D11_SUBRESOURCE_DATA> init(layers);
  for (UINT layer = 0; layer < layers; ++layer) {
    init[layer] = {.pSysMem = single ? static_cast<const void*>(singles.data() + size_t{layer} * width * height)
                                     : static_cast<const void*>(texels.data() + size_t{layer} * width * height),
                   .SysMemPitch = width * (single ? 4u : 16u)};
  }
  ComPtr<ID3D11Texture2D> texture;
  if (FAILED(device->CreateTexture2D(&desc, init.data(), &texture))) return nullptr;
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(texture.Get(), nullptr, &view);
  return view;
}

static ComPtr<ID3D11ShaderResourceView> Volume(uint32_t seed) {
  constexpr UINT SIZE = 16;
  std::mt19937 rng(seed);
  std::vector<Texel> texels(SIZE * SIZE * SIZE);
  for (auto& texel : texels) {
    for (float& value : texel) value = std::uniform_real_distribution<float>(0.f, 1.f)(rng);
  }
  const D3D11_TEXTURE3D_DESC desc = {.Width = SIZE, .Height = SIZE, .Depth = SIZE, .MipLevels = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                     .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA init = {.pSysMem = texels.data(), .SysMemPitch = SIZE * 16, .SysMemSlicePitch = SIZE * SIZE * 16};
  ComPtr<ID3D11Texture3D> texture;
  device->CreateTexture3D(&desc, &init, &texture);
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(texture.Get(), nullptr, &view);
  return view;
}

// Smooth random pictures (a few texels' features, so a coordinate a texel or two off reads near the same value, a wrong
// mapping reads another).
static std::vector<Texel> Picture(UINT width, UINT height, uint32_t seed) {
  std::mt19937 rng(seed);
  std::array<std::array<float, 4>, 4> phases = {};
  for (auto& channel : phases) {
    for (float& value : channel) value = std::uniform_real_distribution<float>(0.f, 6.283f)(rng);
  }
  std::vector<Texel> picture(size_t{width} * height);
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      for (UINT channel = 0; channel < 4; ++channel) {
        const auto& phase = phases[channel];
        picture[size_t{y} * width + x][channel] = 0.5f + 0.2f * std::sin(0.37f * x + phase[0]) * std::cos(0.29f * y + phase[1])
                                                  + 0.15f * std::sin(0.11f * (x + y) + phase[2]) + 0.1f * std::cos(0.53f * y - 0.17f * x + phase[3]);
      }
    }
  }
  return picture;
}

// `picture` (W x H) in the top-left of a `scale` times larger texture, its edge texels beyond it.
static std::vector<Texel> Screen(const std::vector<Texel>& picture, UINT scale) {
  const UINT width = W * scale, height = H * scale;
  std::vector<Texel> texels(size_t{width} * height);
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) texels[size_t{y} * width + x] = picture[std::min(y, H - 1) * W + std::min(x, W - 1)];
  }
  return texels;
}

struct Buffer {
  UINT slot = 0;
  std::vector<uint8_t> bytes;
  std::vector<std::pair<std::string, UINT>> offsets;
  void Set(const std::string& name, const std::vector<float>& values) {
    for (const auto& [variable, offset] : offsets) {
      if (variable == name) std::memcpy(bytes.data() + offset, values.data(), values.size() * 4);
    }
  }
};

static ComPtr<ID3D11Buffer> Constants(const std::vector<uint8_t>& bytes) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>(std::max<size_t>(16, (bytes.size() + 15) / 16 * 16)), .Usage = D3D11_USAGE_DEFAULT,
                                  .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  std::vector<uint8_t> padded(desc.ByteWidth, 0);
  std::memcpy(padded.data(), bytes.data(), bytes.size());
  const D3D11_SUBRESOURCE_DATA init = {.pSysMem = padded.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &init, &buffer);
  return buffer;
}

struct Result {
  std::vector<float> values;
  bool made = false;
};

// One pixel shader's WARP frame: its bindings and inputs from its reflection.
struct Frame {
  std::vector<D3D11_SHADER_INPUT_BIND_DESC> binds;
  std::vector<std::string> names;
  std::vector<Buffer> buffers;
  ComPtr<ID3D11VertexShader> vertex_shader;
  UINT targets = 0;
};

static const std::regex RENDER_SPACE("gbuffer|depth|light_buffer|scene|colour_buffer|color_buffer|velocity|ssao|refraction|reflection|distance",
                                     std::regex::icase);

static bool MakeFrame(const std::vector<uint8_t>& code, Frame* frame) {
  ComPtr<ID3D11ShaderReflection> reflection;
  if (FAILED(D3DReflect(code.data(), code.size(), IID_PPV_ARGS(&reflection)))) return false;
  D3D11_SHADER_DESC shader_desc = {};
  reflection->GetDesc(&shader_desc);
  std::string vs = "struct Out {\n  float4 position : SV_Position;\n";
  std::string body;
  for (UINT index = 0; index < shader_desc.InputParameters; ++index) {
    D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
    reflection->GetInputParameterDesc(index, &parameter);
    if (parameter.SystemValueType != D3D_NAME_UNDEFINED) continue;
    const std::string field = "v" + std::to_string(index);
    const std::string type = (parameter.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ? "nointerpolation uint4 "
                             : (parameter.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ? "nointerpolation int4 " : "float4 ";
    vs += "  " + type + field + " : " + parameter.SemanticName + std::to_string(parameter.SemanticIndex) + ";\n";
    body += "  o." + field + " = float4(ndc.x * " + std::to_string(0.8f - 0.03f * index) + " + " + std::to_string(0.01f * index) +
            ", ndc.y * " + std::to_string(0.7f + 0.02f * index) + " - " + std::to_string(0.01f * index) + ", 0.5 + 0.1 * ndc.x, " +
            std::to_string(1.f + 0.05f * index) + ");\n";
  }
  vs += "};\nOut main(uint id : SV_VertexID) {\n  Out o = (Out)0;\n  const float2 corner = float2((id << 1) & 2, id & 2);\n"
        "  const float2 ndc = corner * float2(2, -2) + float2(-1, 1);\n  o.position = float4(ndc, 0.5, 1);\n" + body + "  return o;\n}\n";
  ComPtr<ID3DBlob> vs_blob, errors;
  if (FAILED(D3DCompile(vs.data(), vs.size(), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs_blob, &errors))) return false;
  device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &frame->vertex_shader);
  for (UINT index = 0; index < shader_desc.OutputParameters; ++index) {
    D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
    reflection->GetOutputParameterDesc(index, &parameter);
    if (parameter.SystemValueType == D3D_NAME_TARGET) frame->targets = std::max(frame->targets, parameter.Register + 1);
  }
  std::mt19937 rng(64 + shader_desc.InstructionCount);
  frame->binds.resize(shader_desc.BoundResources);
  const std::vector<float> identity = {1.f, 0.f, 0.1f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.1f, 0.05f, 0.3f, 1.f};
  for (UINT index = 0; index < shader_desc.BoundResources; ++index) {
    reflection->GetResourceBindingDesc(index, &frame->binds[index]);
    const auto& bind = frame->binds[index];
    frame->names.emplace_back(bind.Name);
    if (bind.Type != D3D_SIT_CBUFFER) continue;
    auto* constant_buffer = reflection->GetConstantBufferByName(bind.Name);
    D3D11_SHADER_BUFFER_DESC buffer_desc = {};
    constant_buffer->GetDesc(&buffer_desc);
    Buffer buffer{.slot = bind.BindPoint, .bytes = std::vector<uint8_t>(buffer_desc.Size)};
    for (UINT variable_index = 0; variable_index < buffer_desc.Variables; ++variable_index) {
      auto* variable = constant_buffer->GetVariableByIndex(variable_index);
      D3D11_SHADER_VARIABLE_DESC variable_desc = {};
      variable->GetDesc(&variable_desc);
      D3D11_SHADER_TYPE_DESC type_desc = {};
      variable->GetType()->GetDesc(&type_desc);
      buffer.offsets.emplace_back(variable_desc.Name, variable_desc.StartOffset);
      const bool integer = type_desc.Type == D3D_SVT_INT || type_desc.Type == D3D_SVT_UINT || type_desc.Type == D3D_SVT_BOOL;
      for (UINT offset = 0; offset + 4 <= variable_desc.Size; offset += 4) {
        // Integers 1 (counts, flags: no endless loop, the game's contact march on), floats random.
        const float value = std::uniform_real_distribution<float>(0.1f, 0.9f)(rng);
        const uint32_t one = 1;
        std::memcpy(buffer.bytes.data() + variable_desc.StartOffset + offset, integer ? static_cast<const void*>(&one) : static_cast<const void*>(&value), 4);
      }
    }
    for (const char* matrix : {"view", "projection", "view_projection", "inv_view", "inv_projection", "inv_view_projection"}) {
      buffer.Set(matrix, identity);
    }
    buffer.Set("camera_position", {0.f, 0.f, -0.5f});
    buffer.Set("g_vpos_texel_offset", {0.f});
    buffer.Set("g_viewport_origin", {0.f, 0.f});
    frame->buffers.push_back(std::move(buffer));
  }
  return frame->targets != 0;
}

// `shader` at `scale` (the view top-left of a scale-times target), cb12 `share` (none: unbound).
static Result Run(const Frame& frame, ID3D11PixelShader* shader, UINT scale, const float* share, uint64_t region_slots,
                  const std::vector<std::string>& whole) {
  const UINT target_width = W * scale, target_height = H * scale;
  std::array<ComPtr<ID3D11ShaderResourceView>, 64> views = {};
  std::array<ID3D11SamplerState*, 16> samplers = {};
  for (size_t index = 0; index < frame.binds.size(); ++index) {
    const auto& bind = frame.binds[index];
    const std::string& name = frame.names[index];
    if (bind.Type == D3D_SIT_SAMPLER) {
      samplers[bind.BindPoint] = ((bind.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0) ? comparison.Get() : point_clamp.Get();
      continue;
    }
    if (bind.Type != D3D_SIT_TEXTURE || bind.BindPoint >= views.size()) continue;
    const bool shadow = std::regex_search(name, std::regex("shadow", std::regex::icase));
    const uint32_t texture_seed = 640 + static_cast<uint32_t>(index);
    switch (bind.Dimension) {
      case D3D_SRV_DIMENSION_TEXTURE3D:
        views[bind.BindPoint] = Volume(7);
        break;
      case D3D_SRV_DIMENSION_TEXTURECUBE:
        views[bind.BindPoint] = Texture(Picture(16, 16 * 6, texture_seed), 16, 16, 6, true);
        break;
      case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
        views[bind.BindPoint] = Texture(Picture(16, 16 * 12, texture_seed), 16, 16, 12, true);
        break;
      case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
        views[bind.BindPoint] = Texture(Picture(W, H * 4, texture_seed), W, H, 4, false, shadow);
        break;
      case D3D_SRV_DIMENSION_TEXTURE2D: {
        // The whole passes' outputs hold the whole view at both scales (their names, view_share's whole_textures, by the
        // name a reader gives them: the sampler's or the texture's).
        const bool whole_texture = std::ranges::any_of(whole, [&](const std::string& entry) {
          return name == entry || ("s_" + name.substr(name.find('_') + 1)) == entry || name == entry.substr(0, entry.size() - 8) + "texture";
        });
        const auto picture = Picture(W, H, texture_seed);
        const bool render_space = !shadow && !whole_texture && (((region_slots >> bind.BindPoint) & 1) != 0 || std::regex_search(name, RENDER_SPACE));
        views[bind.BindPoint] = (render_space && scale != 1) ? Texture(Screen(picture, scale), W * scale, H * scale, 1, false, false)
                                                             : Texture(picture, W, H, 1, false, shadow);
        break;
      }
      default:
        break;  // MS: unbound (g_num_of_samples 1)
    }
  }
  D3D11_TEXTURE2D_DESC desc = {.Width = target_width, .Height = target_height, .MipLevels = 1, .ArraySize = 1,
                               .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT,
                               .BindFlags = D3D11_BIND_RENDER_TARGET};
  std::vector<ComPtr<ID3D11Texture2D>> textures(frame.targets);
  std::vector<ComPtr<ID3D11RenderTargetView>> targets(frame.targets);
  std::vector<ID3D11RenderTargetView*> raw_targets(frame.targets);
  const float clear[4] = {-7.f, -7.f, -7.f, -7.f};
  for (UINT index = 0; index < frame.targets; ++index) {
    device->CreateTexture2D(&desc, nullptr, &textures[index]);
    device->CreateRenderTargetView(textures[index].Get(), nullptr, &targets[index]);
    context->ClearRenderTargetView(targets[index].Get(), clear);
    raw_targets[index] = targets[index].Get();
  }
  std::vector<ComPtr<ID3D11Buffer>> made;
  for (Buffer buffer : frame.buffers) {
    const float width = static_cast<float>(target_width), height = static_cast<float>(target_height);
    buffer.Set("g_screen_size", {width, height, 1.f / width, 1.f / height});
    buffer.Set("g_render_target_dimensions", {width, height, 1.f / width, 1.f / height});
    buffer.Set("g_viewport_dimensions", {static_cast<float>(W), static_cast<float>(H), 1.f / W, 1.f / H});
    buffer.Set("inv_rt_size", {1.f / width, 1.f / height, 1.f, 1.f});
    made.push_back(Constants(buffer.bytes));
    context->PSSetConstantBuffers(buffer.slot, 1, made.back().GetAddressOf());
  }
  if (share != nullptr) {
    std::vector<uint8_t> bytes(16);
    std::memcpy(bytes.data(), share, 16);
    made.push_back(Constants(bytes));
    context->PSSetConstantBuffers(12, 1, made.back().GetAddressOf());
  }
  std::array<ID3D11ShaderResourceView*, 64> raw_views = {};
  for (size_t index = 0; index < views.size(); ++index) raw_views[index] = views[index].Get();
  context->PSSetShaderResources(0, static_cast<UINT>(raw_views.size()), raw_views.data());
  context->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
  context->OMSetRenderTargets(frame.targets, raw_targets.data(), nullptr);
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(W), .Height = static_cast<float>(H), .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(frame.vertex_shader.Get(), nullptr, 0);
  context->PSSetShader(shader, nullptr, 0);
  context->Draw(3, 0);
  Result result{.made = true};
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  for (UINT index = 0; index < frame.targets; ++index) {
    ComPtr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&desc, nullptr, &staging);
    context->CopyResource(staging.Get(), textures[index].Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return {};
    const size_t at = result.values.size();
    result.values.resize(at + size_t{W} * H * 4);
    for (UINT y = 0; y < H; ++y) {
      std::memcpy(result.values.data() + at + size_t{y} * W * 4, static_cast<const uint8_t*>(mapped.pData) + size_t{y} * mapped.RowPitch, W * 16);
    }
    context->Unmap(staging.Get(), 0);
  }
  context->ClearState();
  return result;
}

// Values apart beyond `tolerance` (relative above 1), only beyond EDGE px of the view's edges when `inner`; `bits`: not
// bit-equal.
static uint64_t Apart(const Result& a, const Result& b, double tolerance, bool inner, uint64_t* bits = nullptr) {
  uint64_t apart = 0, differ = 0;
  const size_t planes = a.values.size() / (size_t{W} * H * 4);
  if (a.values.size() != b.values.size()) return UINT64_MAX;
  for (size_t plane = 0; plane < planes; ++plane) {
    for (UINT y = 0; y < H; ++y) {
      for (UINT x = 0; x < W; ++x) {
        if (inner && (x < EDGE || y < EDGE || x >= W - EDGE || y >= H - EDGE)) continue;
        for (UINT channel = 0; channel < 4; ++channel) {
          const size_t at = ((plane * H + y) * W + x) * 4 + channel;
          const float first = a.values[at], second = b.values[at];
          if (std::memcmp(&first, &second, 4) == 0) continue;
          ++differ;
          const double difference = std::fabs(static_cast<double>(first) - second);
          if (!(difference <= tolerance * std::max(1.0, std::fabs(static_cast<double>(first))))) ++apart;
        }
      }
    }
  }
  if (bits != nullptr) *bits = differ;
  return apart;
}

static void Disassemble(const char* what, const std::vector<uint8_t>& code) {
  ComPtr<ID3DBlob> text;
  if (FAILED(D3DDisassemble(code.data(), code.size(), 0, nullptr, &text))) return;
  std::printf("=== %s\n%s\n", what, static_cast<const char*>(text->GetBufferPointer()));
}

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  const std::filesystem::path repo = argv[1], dumps = argv[2];
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) return 1;
  D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                     .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
  device->CreateSamplerState(&sampler_desc, &point_clamp);
  sampler_desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
  device->CreateSamplerState(&sampler_desc, &comparison);
  const float third = 1.f / 3.f;
  const std::array<float, 4> share = {third, third, third, third};
  const std::string attila_rt = ReadText(repo / "src/games/twattila/rt_scene.hpp");
  const char* only_hash = std::getenv("CHANGES_HASH");

  uint64_t total_changed = 0;
  for (const char* game : {"twattila", "twthronesofbritannia", "twrome2", "twwarhammer", "twwarhammer2", "twtroy", "twpharaohdynasties"}) {
    if (const char* only = std::getenv("CHANGES_GAME"); only != nullptr && std::string(only) != game) continue;
    const std::string text = ReadText(repo / "src/games" / game / "addon.cpp");
    const auto cso = dumps / (std::string("dump_shader_") + game + "_pack") / "cso";
    std::vector<std::string> whole;
    if (text.find("view_share::WHOLE_TEXTURES,") != std::string::npos) whole.assign(view_share_patch::WHOLE_TEXTURES.begin(), view_share_patch::WHOLE_TEXTURES.end());
    std::vector<std::string> target_sizes;
    if (text.find("SetTargetSizes(") != std::string::npos) target_sizes.emplace_back("inv_rt_size");
    const bool chains = text.find("SetViewportChains(true)") != std::string::npos;
    uint32_t colour_register = 1;
    if (std::smatch match; std::regex_search(text, match, std::regex("sun_colour_register = (\\d+);"))) colour_register = std::stoul(match[1]);
    const bool declare_position = text.find("PatchForwardShadows(SHADOW_READER_SHADERS, true)") != std::string::npos;
    std::string shadow_block = Block(text, "SHADOW_READER_SHADERS");
    if (shadow_block.empty()) shadow_block = Block(attila_rt, "SHADOW_READER_SHADERS");
    const auto suns = Entries(Block(text, "LIGHT_BOUNCE_DRAWS"), "{.shader_hash = 0x");
    const auto waters = Entries(Block(text, "WATER_SHADERS"), "{.hash = 0x");
    std::vector<std::pair<uint32_t, uint64_t>> region_reads;
    for (const auto& entry : Entries(Block(text, "RENDER_SCALE_READS"), "{.shader_hash = 0x")) {
      uint64_t slots = Field(entry, "slots", 0);
      if (std::smatch match; std::regex_search(entry, match, std::regex("uint64_t\\{1\\} << (\\d+)"))) slots = uint64_t{1} << std::stoul(match[1]);
      region_reads.emplace_back(Hash(entry), slots);
    }
    uint32_t shaders = 0, changed = 0, fixed = 0, better = 0, same = 0, worse = 0, unreached = 0;
    const int game_failures = failures;
    for (const auto& entry : std::filesystem::directory_iterator(cso)) {
      const std::string file = entry.path().filename().string();
      if (file.find(".ps_") == std::string::npos || !file.ends_with(".cso")) continue;
      const uint32_t hash = static_cast<uint32_t>(std::stoul(file.substr(2, 8), nullptr, 16));
      if (only_hash != nullptr && std::strtoul(only_hash, nullptr, 16) != hash) continue;
      ++shaders;
      const auto code = ReadBytes(entry.path());
      uint64_t slots = 0;
      for (const auto& [read_hash, read_slots] : region_reads) slots |= (read_hash == hash) ? read_slots : 0;
      const auto program = twattila::dxbc::Read(code);
      const auto after = view_share_patch::Patch(code, view_share_patch::Reflect(code, program, whole, target_sizes), slots, chains);
      const auto before = view_share_base::Patch(code, view_share_base::Reflect(code, program, whole, target_sizes), slots, chains);
      if (after.code == before.code) continue;
      ++changed;
      const auto& old_code = before.code.empty() ? code : before.code;
      const auto& new_code = after.code.empty() ? code : after.code;
      if (only_hash != nullptr) {
        Disassemble("BASE", old_code);
        Disassemble("NEW", new_code);
      }
      const auto line = std::format("  {:08X} {}: ndc {} -> {}, projected {} -> {}, whole {} -> {}, offsets {}", hash, file, before.ndc, after.ndc,
                                    before.projected, after.projected, before.whole, after.whole, after.offset);
      std::string notes;
      bool ok = true;
      // 2. The runtime's patches on top.
      const auto chain = [&](const std::vector<uint8_t>& start, std::vector<std::string>* steps) {
        std::vector<uint8_t> result = start;
        const std::string hex = std::format("0x{:08X}", hash);
        const bool sun = std::ranges::any_of(suns, [&](const std::string& row) { return Hash(row) == hash; });
        const bool forward = shadow_block.find(hex) != std::string::npos;
        if (sun || forward) {
          const auto shadow = ambient_patch::Patch(start, forward, colour_register, forward && declare_position);
          steps->push_back(std::string("shadows:") + (shadow.empty() ? "empty" : "patched"));
          if (!shadow.empty()) result = shadow;
          if (!forward) {
            uint32_t sites = 0;
            for (const auto& row : suns) {
              if (Hash(row) != hash) continue;
              if (row.find("AMBIENT_AFTER_HARMONICS_Z") != std::string::npos) {
                sites = twattila::light_bounce::AMBIENT_AFTER_HARMONICS_Z;
              } else if (row.find("AMBIENT_AFTER_HARMONICS") != std::string::npos) {
                sites = twattila::light_bounce::AMBIENT_AFTER_HARMONICS;
              } else {
                sites = Field(row, "ambient_final_sites", 0);
              }
            }
            bool foliage = false;
            const auto ambient = ambient_patch::PatchAmbient(result, &foliage, sites);
            steps->push_back(std::string("ambient:") + (ambient.empty() ? "empty" : "patched"));
            if (!ambient.empty()) result = ambient;
          }
        }
        for (const auto& row : waters) {
          if (Hash(row) != hash) continue;
          water_patch::WaterShader water = {.hash = hash, .lighting_slot = Field(row, "lighting_slot", 0), .world_input = Field(row, "world_input", 0),
                                            .refraction_slot = Field(row, "refraction_slot", 0), .caustics = Field(row, "caustics", 1) != 0,
                                            .normal_slot = Field(row, "normal_slot", 0), .material_water = Field(row, "material_water", 0) != 0,
                                            .every_refraction = Field(row, "every_refraction", 0) != 0};
          water.opacity = {.type = Field(row, "type", 0), .component = Field(row, "component", 0), .divisor = Field(row, "divisor", 0) != 0,
                           .component_only = Field(row, "component_only", 0) != 0};
          if (std::smatch match; std::regex_search(row, match, std::regex("\\.index = \\{(\\d+), (\\d+)\\}"))) {
            water.opacity.index = {static_cast<uint32_t>(std::stoul(match[1])), static_cast<uint32_t>(std::stoul(match[2]))};
          }
          if (!water.material_water) {
            if (!water.caustics) continue;
            const auto caustics = water_patch::PatchWaterCaustics(result, water);
            steps->push_back(std::string("caustics:") + (caustics.empty() ? "empty" : "patched"));
            if (!caustics.empty()) result = caustics;
            continue;
          }
          std::vector<uint8_t> material = water.caustics ? water_patch::PatchWaterCaustics(result, water) : result;
          steps->push_back(std::string("material caustics:") + (material.empty() ? "empty" : "patched"));
          if (!material.empty()) material = water_patch::PatchWaterAbsorption(material, water.opacity);
          steps->push_back(std::string("absorption:") + (material.empty() ? "empty" : "patched"));
          if (!material.empty()) {
            int sites = 0;
            if (auto lined = water_patch::PatchWaterScaleLine(material, &sites); !lined.empty()) material = std::move(lined);
            steps->push_back(std::format("scale line sites:{}", sites));
            result = std::move(material);
          }
        }
        return result;
      };
      std::vector<std::string> old_steps, new_steps;
      const auto old_chained_code = chain(old_code, &old_steps);
      const auto new_chained_code = chain(new_code, &new_steps);
      if (old_steps != new_steps) {
        ok = false;
        notes += " CHAIN CHANGED:";
        for (size_t index = 0; index < std::max(old_steps.size(), new_steps.size()); ++index) {
          notes += " " + (index < old_steps.size() ? old_steps[index] : "-") + "/" + (index < new_steps.size() ? new_steps[index] : "-");
        }
      }
      Frame frame;
      ComPtr<ID3D11PixelShader> game_shader, old_shader, new_shader, old_chained, new_chained;
      if (!MakeFrame(code, &frame) || FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, &game_shader))
          || FAILED(device->CreatePixelShader(old_code.data(), old_code.size(), nullptr, &old_shader))
          || FAILED(device->CreatePixelShader(new_code.data(), new_code.size(), nullptr, &new_shader))
          || FAILED(device->CreatePixelShader(old_chained_code.data(), old_chained_code.size(), nullptr, &old_chained))
          || FAILED(device->CreatePixelShader(new_chained_code.data(), new_chained_code.size(), nullptr, &new_chained))) {
        std::printf("%s: a shader or its frame not made: FAIL\n", line.c_str());
        ++failures;
        continue;
      }
      // 1.
      const auto at100 = Run(frame, game_shader.Get(), 1, nullptr, slots, whole);
      uint64_t bits = 0;
      Apart(at100, Run(frame, new_shader.Get(), 1, nullptr, slots, whole), 0.0, false, &bits);
      if (bits != 0) {
        ok = false;
        notes += std::format(" 100% NOT THE GAME'S BITS ({})", bits);
      }
      if (!old_steps.empty()) {
        Apart(Run(frame, old_chained.Get(), 1, nullptr, slots, whole), Run(frame, new_chained.Get(), 1, nullptr, slots, whole), 0.0, false, &bits);
        if (bits != 0) {
          ok = false;
          notes += std::format(" CHAIN AT 100% {} values not bit-equal", bits);
        }
      }
      // 3.
      const uint64_t old_apart = Apart(at100, Run(frame, old_shader.Get(), S, share.data(), slots, whole), 1e-5, true);
      const uint64_t new_apart = Apart(at100, Run(frame, new_shader.Get(), S, share.data(), slots, whole), 1e-5, true);
      const char* verdict = "SAME";
      if (new_apart > old_apart) {
        verdict = "WORSE";
        ++worse;
        ok = false;
      } else if (new_apart == 0 && old_apart != 0) {
        verdict = "FIXED";
        ++fixed;
      } else if (new_apart < old_apart) {
        verdict = "BETTER";
        ++better;
      } else {
        ++same;
        // The changed sites not reached in this frame (both the same: a branch not taken with these constants).
        unreached += (old_apart == 0) ? 1 : 0;
      }
      failures += ok ? 0 : 1;
      std::printf("%s; 33%% against 100%%: base %llu, new %llu apart (CONTROL: the base's): %s %s%s\n", line.c_str(),
                  static_cast<unsigned long long>(old_apart), static_cast<unsigned long long>(new_apart), verdict, ok ? "ok" : "FAIL", notes.c_str());
    }
    total_changed += changed;
    std::printf("%s: %u pixel shaders, %u changed: %u FIXED, %u BETTER, %u SAME (%u both exact here), %u WORSE; %d failed\n\n", game, shaders,
                changed, fixed, better, same, unreached, worse, failures - game_failures);
  }
  std::printf("%llu changed shaders\n", static_cast<unsigned long long>(total_changed));
  std::printf(failures == 0 ? "VIEW SHARE CHANGES ALL PASSED\n" : "VIEW SHARE CHANGES: %d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
