// The overlays' readers below 100% (bug4_, DEBUG44 (f): ships' selection indicators as stretched stripes at DLSS Performance):
// every RENDER_SCALE_READS row of the seven view_share games whose slots name an overlay texture (colour_overlay,
// ui_colour_overlay, normal_overlay, s_overlay, t_overlay, tex_terrain_overlay), the game's bytes from its dump, patched as
// view_share::OnCreatePipeline patches them (the addon's whole textures, viewport chains, target sizes).
// 1. The patch reads the overlay through the frame's share: one region site or more.
// 2. The patches the runtime puts on top (screen_space_shadows' Patch / PatchAmbient on the sun and forward rows, water_effects'
//    caustics / absorption / scale line on the water rows) take the new code as they took the old (empty or not alike), and
//    at 100% (cb12 unbound) the chain on the new code gives the old chain's bits.
// WARP, a full-screen draw at a W x H viewport at 0, 0 (100%: a W x H target; 33%: 3W x 3H, the picture top-left, edges
// beyond): the overlays random pictures, the other render-space textures (G-buffer, depth) one texel each (their mapping
// view_share's other sites), the rest (maps of their own, whole passes' outputs) the same random picture at both scales.
// The camera as the game and the render scale set it (g_screen_size and g_render_target_dimensions the target's,
// g_viewport_dimensions the viewport's), the matrices identity-like with a projective last row.
// 3. 100%: the patch with cb12 unbound gives the game's bits.
// 4. 100%: another overlay picture changes the game's output (the overlay counts; tried over 8 seeds of the one-texel
//    textures, else the row is listed: its overlay branch not reached in this frame).
// 5. 33%: the patch (cb12 1/3) with the overlay 3x (its picture top-left) against the patch at zw 1 with the overlay whole,
//    all else alike: the overlay read through the frame's share (1e-5 beyond 8 px of the edges; the overlay point-sampled,
//    a 3x texture's coordinate rounding moving a few texels: up to 1% of the values). CONTROL (where 4 holds): the patch
//    without its region sites (today's) on the 3x overlay: apart (10% of the values or more).
// 6. 33%, an overlay not display-sized (OnCommand's zw 1, the overlay whole): the patch gives today's bits.
// Listed: the patch at 33% against the game at 100% (other sites show there too: the ocean's refraction distortion).
// OVERLAY_GAME=<folder>: that game alone; OVERLAY_DUMP=<hash>: its patched bytes written, nothing drawn.
// tests.exe <repo> <dumps folder>
#include <windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
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
static ComPtr<ID3D11SamplerState> point_clamp, linear_clamp, comparison;
static int failures = 0;

static std::string ReadText(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}

static std::vector<uint8_t> ReadBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
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

static std::vector<Texel> Picture(UINT count, uint32_t seed, bool uniform = false) {
  std::mt19937 rng(seed);
  std::vector<Texel> picture(count);
  Texel first = {};
  for (float& value : first) value = std::uniform_real_distribution<float>(0.f, 1.f)(rng);
  for (auto& texel : picture) {
    if (uniform) {
      texel = first;
      continue;
    }
    for (float& value : texel) value = std::uniform_real_distribution<float>(0.f, 1.f)(rng);
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
  uint64_t overlay_slots = 0;
};

enum class Overlay { SCREEN, WHOLE, OTHER_PICTURE };

static const std::regex RENDER_SPACE("gbuffer|depth|light_buffer|scene|colour_buffer|color_buffer|velocity", std::regex::icase);

static bool MakeFrame(const std::vector<uint8_t>& code, uint64_t region_slots, Frame* frame) {
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
    // xy over the view in -0.8..0.8 (a projected uv's xy / w + 0.5 in 0.1..0.9), z and w near 0.5 and 1.
    body += "  o." + field + " = float4(ndc.x * " + std::to_string(0.8f - 0.03f * index) + " + " + std::to_string(0.01f * index) +
            ", ndc.y * " + std::to_string(0.7f + 0.02f * index) + " - " + std::to_string(0.01f * index) + ", 0.5 + 0.1 * ndc.x, " +
            std::to_string(1.f + 0.05f * index) + ");\n";
  }
  vs += "};\nOut main(uint id : SV_VertexID) {\n  Out o = (Out)0;\n  const float2 corner = float2((id << 1) & 2, id & 2);\n"
        "  const float2 ndc = corner * float2(2, -2) + float2(-1, 1);\n  o.position = float4(ndc, 0.5, 1);\n" + body + "  return o;\n}\n";
  ComPtr<ID3DBlob> vs_blob, errors;
  if (FAILED(D3DCompile(vs.data(), vs.size(), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs_blob, &errors))) {
    std::printf("    vs: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return false;
  }
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
    if (bind.Type == D3D_SIT_TEXTURE && ((region_slots >> bind.BindPoint) & 1) != 0 && frame->names.back().find("overlay") != std::string::npos) {
      frame->overlay_slots |= uint64_t{1} << bind.BindPoint;
    }
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
        // Integers 1 (counts, flags: no endless loop), floats random.
        const float value = std::uniform_real_distribution<float>(0.1f, 0.9f)(rng);
        const uint32_t one = 1;
        std::memcpy(buffer.bytes.data() + variable_desc.StartOffset + offset, integer ? static_cast<const void*>(&one) : static_cast<const void*>(&value), 4);
      }
    }
    for (const char* matrix : {"view", "projection", "view_projection", "inv_view", "inv_projection", "inv_view_projection", "g_overlay_view_projection",
                               "overlay_view_projection_matrix", "view_projection_matrix"}) {
      buffer.Set(matrix, identity);
    }
    buffer.Set("camera_position", {0.f, 0.f, -0.5f});
    buffer.Set("g_vpos_texel_offset", {0.f});
    buffer.Set("g_viewport_origin", {0.f, 0.f});
    frame->buffers.push_back(std::move(buffer));
  }
  return frame->targets != 0 && frame->overlay_slots != 0;
}

// `shader` at `scale` (the picture top-left of a scale-times target), cb12 `share` (none: unbound); the one-texel textures from
// `seed`, the overlays as `overlay`.
static Result Run(const Frame& frame, ID3D11PixelShader* shader, UINT scale, const float* share, uint32_t seed, Overlay overlay) {
  const UINT target_width = W * scale, target_height = H * scale;
  std::array<ComPtr<ID3D11ShaderResourceView>, 64> views = {};
  std::array<ID3D11SamplerState*, 16> samplers = {};
  for (size_t index = 0; index < frame.binds.size(); ++index) {
    const auto& bind = frame.binds[index];
    const std::string& name = frame.names[index];
    if (bind.Type == D3D_SIT_SAMPLER) {
      samplers[bind.BindPoint] = ((bind.uFlags & D3D_SIF_COMPARISON_SAMPLER) != 0) ? comparison.Get()
                                 : (name.find("point") != std::string::npos || name.find("nearest") != std::string::npos
                                    || name.find("overlay") != std::string::npos) ? point_clamp.Get()
                                                                                                                         : linear_clamp.Get();
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
        views[bind.BindPoint] = Texture(Picture(16 * 16 * 6, texture_seed), 16, 16, 6, true);
        break;
      case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
        views[bind.BindPoint] = Texture(Picture(16 * 16 * 12, texture_seed), 16, 16, 12, true);
        break;
      case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
        views[bind.BindPoint] = Texture(Picture(W * H * 4, texture_seed), W, H, 4, false, shadow);
        break;
      case D3D_SRV_DIMENSION_TEXTURE2D:
        if (((frame.overlay_slots >> bind.BindPoint) & 1) != 0) {
          const auto picture = Picture(W * H, texture_seed + ((overlay == Overlay::OTHER_PICTURE) ? 1000u : 0u));
          views[bind.BindPoint] = (overlay == Overlay::WHOLE || scale == 1) ? Texture(picture, W, H) : Texture(Screen(picture, scale), W * scale, H * scale);
        } else if (std::regex_search(name, RENDER_SPACE)) {
          views[bind.BindPoint] = Texture(Picture(W * H * scale * scale, seed * 97 + texture_seed, true), W * scale, H * scale, 1, false, shadow);
        } else {
          views[bind.BindPoint] = Texture(Picture(W * H, texture_seed), W, H, 1, false, shadow);
        }
        break;
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

// Values apart beyond `tolerance` (relative above 1), only beyond EDGE px of the view's edges when `inner`; the largest
// finite difference of the last call in last_largest.
static double last_largest = 0.0;
static uint64_t Apart(const Result& a, const Result& b, double tolerance, bool inner, uint64_t* bits = nullptr) {
  uint64_t apart = 0, differ = 0;
  last_largest = 0.0;
  const size_t planes = a.values.size() / (size_t{W} * H * 4);
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
          if (std::isfinite(difference)) last_largest = std::max(last_largest, difference);
        }
      }
    }
  }
  if (bits != nullptr) *bits = differ;
  return apart;
}

struct Chain {
  std::vector<uint8_t> code;
  std::vector<std::string> steps;  // each step: "name:empty" / "name:patched"
};

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  const std::filesystem::path repo = argv[1], dumps = argv[2];
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) return 1;
  D3D11_SAMPLER_DESC sampler_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                     .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP, .MaxLOD = D3D11_FLOAT32_MAX};
  device->CreateSamplerState(&sampler_desc, &point_clamp);
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  device->CreateSamplerState(&sampler_desc, &linear_clamp);
  sampler_desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
  device->CreateSamplerState(&sampler_desc, &comparison);
  const float third = 1.f / 3.f;
  const std::array<float, 4> share = {third, third, third, third};
  const std::array<float, 4> share_whole = {third, third, 1.f, 1.f};
  const std::string attila_rt = ReadText(repo / "src/games/twattila/rt_scene.hpp");

  for (const char* game : {"twattila", "twthronesofbritannia", "twrome2", "twwarhammer", "twwarhammer2", "twtroy", "twpharaohdynasties"}) {
    // OVERLAY_GAME=<folder>: that game alone.
    if (const char* only = std::getenv("OVERLAY_GAME"); only != nullptr && std::string(only) != game) continue;
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
    int rows = 0, counted = 0, chained = 0, game_failures = failures;
    for (const auto& entry : Entries(Block(text, "RENDER_SCALE_READS"), "{.shader_hash = 0x")) {
      const uint32_t hash = Hash(entry);
      uint64_t slots = Field(entry, "slots", 0);
      if (std::smatch match; std::regex_search(entry, match, std::regex("uint64_t\\{1\\} << (\\d+)"))) slots = uint64_t{1} << std::stoul(match[1]);
      std::filesystem::path file;
      for (const auto& found : std::filesystem::directory_iterator(cso)) {
        const auto name = found.path().filename().string();
        if (name.starts_with(std::format("0x{:08X}.ps_", hash)) && name.ends_with(".cso")) file = found.path();
      }
      if (file.empty()) continue;
      const auto code = ReadBytes(file);
      Frame frame;
      if (!MakeFrame(code, slots, &frame)) continue;  // no overlay in its slots: another kind of region read
      ++rows;
      const auto reflection = view_share_patch::Reflect(code, twattila::dxbc::Read(code), whole, target_sizes);
      const auto before = view_share_patch::Patch(code, reflection, slots & ~frame.overlay_slots, chains);
      const auto after = view_share_patch::Patch(code, reflection, slots, chains);
      const auto& old_code = before.code.empty() ? code : before.code;
      const auto& new_code = after.code.empty() ? code : after.code;
      // OVERLAY_DUMP=<hash>: its patched bytes written next to the test (fxc /dumpbin them), nothing drawn.
      if (const char* dump = std::getenv("OVERLAY_DUMP"); dump != nullptr) {
        if (std::strtoul(dump, nullptr, 16) == hash) {
          std::ofstream(std::format("0x{:08X}_patched.cso", hash), std::ios::binary).write(reinterpret_cast<const char*>(new_code.data()),
                                                                                        static_cast<std::streamsize>(new_code.size()));
        }
        continue;
      }
      const auto line = std::format("  {:08X} {} slots {:b} overlay {:b}: region sites {} -> {}", hash, file.filename().string(), slots,
                                    frame.overlay_slots, before.region, after.region);
      bool ok = after.region > before.region;
      std::string notes = ok ? "" : " NO REGION SITE";
      // 2. The runtime's patches on top.
      const auto chain = [&](const std::vector<uint8_t>& start) {
        Chain result{.code = start};
        const std::string hex = std::format("0x{:08X}", hash);
        const bool sun = std::ranges::any_of(suns, [&](const std::string& sun) { return Hash(sun) == hash; });
        const bool forward = shadow_block.find(hex) != std::string::npos;
        if (sun || forward) {
          const auto shadow = ambient_patch::Patch(start, forward, colour_register, forward && declare_position);
          result.steps.push_back(std::string("shadows:") + (shadow.empty() ? "empty" : "patched"));
          if (!shadow.empty()) result.code = shadow;
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
            const auto ambient = ambient_patch::PatchAmbient(result.code, &foliage, sites);
            result.steps.push_back(std::string("ambient:") + (ambient.empty() ? "empty" : "patched"));
            if (!ambient.empty()) result.code = ambient;
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
            const auto caustics = water_patch::PatchWaterCaustics(result.code, water);
            result.steps.push_back(std::string("caustics:") + (caustics.empty() ? "empty" : "patched"));
            if (!caustics.empty()) result.code = caustics;
            continue;
          }
          std::vector<uint8_t> material = water.caustics ? water_patch::PatchWaterCaustics(result.code, water) : result.code;
          result.steps.push_back(std::string("material caustics:") + (material.empty() ? "empty" : "patched"));
          if (!material.empty()) material = water_patch::PatchWaterAbsorption(material, water.opacity);
          result.steps.push_back(std::string("absorption:") + (material.empty() ? "empty" : "patched"));
          if (!material.empty()) {
            int sites = 0;
            if (auto lined = water_patch::PatchWaterScaleLine(material, &sites); !lined.empty()) material = std::move(lined);
            result.steps.push_back(std::format("scale line sites:{}", sites));
            result.code = std::move(material);
          }
        }
        return result;
      };
      const auto old_chain = chain(old_code);
      const auto new_chain = chain(new_code);
      if (old_chain.steps != new_chain.steps) {
        ok = false;
        notes += " CHAIN CHANGED:";
        for (size_t index = 0; index < std::max(old_chain.steps.size(), new_chain.steps.size()); ++index) {
          notes += " " + (index < old_chain.steps.size() ? old_chain.steps[index] : "-") + "/" + (index < new_chain.steps.size() ? new_chain.steps[index] : "-");
        }
      }
      ComPtr<ID3D11PixelShader> game_shader, before_shader, ours, old_chained, new_chained;
      if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, &game_shader))
          || FAILED(device->CreatePixelShader(old_code.data(), old_code.size(), nullptr, &before_shader))
          || FAILED(device->CreatePixelShader(new_code.data(), new_code.size(), nullptr, &ours))
          || FAILED(device->CreatePixelShader(old_chain.code.data(), old_chain.code.size(), nullptr, &old_chained))
          || FAILED(device->CreatePixelShader(new_chain.code.data(), new_chain.code.size(), nullptr, &new_chained))) {
        std::printf("%s: a shader not made: FAIL\n", line.c_str());
        ++failures;
        continue;
      }
      if (!old_chain.steps.empty()) {
        ++chained;
        const auto old100 = Run(frame, old_chained.Get(), 1, nullptr, 0, Overlay::SCREEN);
        const auto new100 = Run(frame, new_chained.Get(), 1, nullptr, 0, Overlay::SCREEN);
        uint64_t bits = 0;
        Apart(old100, new100, 0.0, false, &bits);
        if (bits != 0) {
          ok = false;
          notes += std::format(" CHAIN AT 100% {} values not bit-equal", bits);
        }
      }
      // 3-6 on WARP: the seed whose one-texel textures reach the overlay.
      uint32_t seed = 0;
      Result at100;
      bool reaches = false;
      for (; seed < 8 && !reaches; ++seed) {
        at100 = Run(frame, game_shader.Get(), 1, nullptr, seed, Overlay::SCREEN);
        reaches = Apart(at100, Run(frame, game_shader.Get(), 1, nullptr, seed, Overlay::OTHER_PICTURE), 1e-6, false) != 0;
      }
      --seed;
      uint64_t bits = 0;
      Apart(at100, Run(frame, ours.Get(), 1, nullptr, seed, Overlay::SCREEN), 0.0, false, &bits);
      if (bits != 0) {
        ok = false;
        notes += std::format(" 100% NOT THE GAME'S BITS ({})", bits);
      }
      // 5. The overlay read through the frame's share: the overlay 3x at zw 1/3 against it whole at zw 1, all else alike
      //    (point sampling; its rounding on a 3x texture moves a few texels: up to 1% of the values).
      const auto ours33 = Run(frame, ours.Get(), S, share.data(), seed, Overlay::SCREEN);
      const auto ours33_whole = Run(frame, ours.Get(), S, share_whole.data(), seed, Overlay::WHOLE);
      const uint64_t compared = uint64_t{W - 2 * EDGE} * (H - 2 * EDGE) * 4 * frame.targets;
      const uint64_t mapped = Apart(ours33_whole, ours33, 1e-5, true);
      const double largest = last_largest;
      // CONTROL: the patch without the region sites (today's) on the 3x overlay against the same reference.
      const uint64_t control = Apart(ours33_whole, Run(frame, before_shader.Get(), S, share.data(), seed, Overlay::SCREEN), 1e-5, true);
      // 6. zw 1 (an overlay not display-sized, OnCommand's size check): the patch gives today's bits.
      uint64_t whole_bits = 0;
      Apart(Run(frame, before_shader.Get(), S, share_whole.data(), seed, Overlay::WHOLE), ours33_whole, 0.0, true, &whole_bits);
      // Listed: the patch at 33% against the game at 100% (other sites' differences show here too: the ocean's refraction).
      const uint64_t against_100 = Apart(at100, ours33, 1e-5, true);
      if (mapped * 100 > compared) {
        ok = false;
        notes += std::format(" OVERLAY MAPPING {} OF {} APART", mapped, compared);
      }
      if (whole_bits != 0) {
        ok = false;
        notes += std::format(" ZW 1 NOT TODAY'S BITS ({})", whole_bits);
      }
      if (reaches) {
        ++counted;
        if (control * 10 < compared) {
          ok = false;
          notes += " A CONTROL PASSED";
        }
      }
      failures += ok ? 0 : 1;
      std::printf("%s; chain %s; overlay %s (seed %u); 33%%: mapping %llu of %llu apart (largest %.3g), CONTROL %llu, zw 1 bits %llu; "
                  "against 100%% %llu (listed): %s%s\n",
                  line.c_str(), old_chain.steps.empty() ? "none" : "kept", reaches ? "counts" : "not reached: listed", seed,
                  static_cast<unsigned long long>(mapped), static_cast<unsigned long long>(compared), largest, static_cast<unsigned long long>(control),
                  static_cast<unsigned long long>(whole_bits), static_cast<unsigned long long>(against_100), ok ? "ok" : "FAIL", notes.c_str());
    }
    std::printf("%s: %d overlay rows, %d with the overlay reached, %d with a patch on top, %d failed\n\n", game, rows, counted, chained,
                failures - game_failures);
    if (rows == 0) {
      std::printf("%s: no overlay row: FAIL\n", game);
      ++failures;
    }
  }
  std::printf(failures == 0 ? "OVERLAY READS ALL PASSED\n" : "OVERLAY READS: %d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
