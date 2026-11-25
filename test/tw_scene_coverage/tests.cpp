// twattila/rt_core.hpp's scene_draw hosts' masked draws (SceneCoverage; Tales of Arise's prepass), on WARP with the real
// rt_core: a masked pixel shader (t0's alpha at a TEXCOORD, discarded below 0.3333, as Tales' masked prepass) bound, its
// coverage as OnInitPipeline classifies it.
// 1. Host::leaf_cutoff: a host with 0.333 gives its masked draws that cut-off (SceneShader::alpha_cutoff: their captures
//    RT_OWN_CUTOFF) and the bridge's rt_leaf_cutoff 0.333, through InstallScene (scene_draw hosts) and
//    InstallReplayedScene (R.U.S.E.). Default (every current host): alpha_cutoff left as scene_draw set it (0, or a value
//    of its own) and the bridge's 0 (the helper's 0.5), as before; listed shaders' own cut-off still taken. CONTROL: the
//    rule before (the listed shaders' largest, 0 for a scene_draw host) gives 0 for the 0.333 host.
// 2. Masks at a scaled UV (ClassifyCoverage): the UV times a material register right before the sample (Tales' shapes:
//    cb1[1].xy, cb1[11].zz) MASKED with that register and its components; unscaled as before. CONTROLS: a UV scaled and
//    moved, a UV scaled by a computed value: left out, as before.
// 3. SceneCoverage: a scaled mask stays out of the scene (as before) unless Host::mask_uv_scales; then in, with its register.
// 4. rt_vertices on WARP: this add-on's (Tales: RT_VERTICES_UV_SCALES) against the plain build (the include before).
// 5. Capture's copy of the register from a dynamic constant buffer (as the game's) into the registers' buffer.
// 6. With <dump folder> <masked_ps.tsv> (tools/toa/rt_tests: Tales' masked prepass shaders as 3Dmigoto dumped them, and the
//    scanner's table of their UV registers): each mask's classification against the table.
// tests.exe <repository> [<dump folder> <masked_ps.tsv>]
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "src/games/twattila/rt_core.hpp"

using Microsoft::WRL::ComPtr;

extern "C" __declspec(dllexport) const char* const NAME = "Scene coverage test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char*) {}
// InstallScene reads the log details key (reshade::get_config_value calls this export unchecked): none set.
extern "C" __declspec(dllexport) bool ReShadeGetConfigValue(void*, reshade::api::effect_runtime*, const char*, const char*, char*, size_t*) {
  return false;
}

namespace core = twattila::rt_scene::internal;
using twattila::rt_scene::Host;
using twattila::rt_scene::SceneShader;

#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (false)

namespace {

ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;

// `source` compiled as the build compiles (O3), bound as the context's pixel shader and classified as OnInitPipeline does.
ComPtr<ID3D11PixelShader> Bind(const char* source, core::PixelCoverage* coverage) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source, std::strlen(source), "test", nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
    std::printf("compile failed: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    return {};
  }
  ComPtr<ID3D11PixelShader> shader;
  if (FAILED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader))) return {};
  *coverage = core::ClassifyCoverage({static_cast<const uint8_t*>(code->GetBufferPointer()), code->GetBufferSize()});
  const std::unique_lock lock(core::pixel_coverage_mutex);
  core::pixel_coverage[reinterpret_cast<uint64_t>(shader.Get())] = *coverage;
  context->PSSetShader(shader.Get(), nullptr, 0);
  return shader;
}

bool NoDraw(ID3D11DeviceContext*, SceneShader*) { return false; }

// Tales' masked prepass shape: t0's alpha at TEXCOORD0, biased, x a material factor, discarded below 0.3333.
constexpr const char* MASKED = R"(
Texture2D<float4> t0 : register(t0);
SamplerState s0 : register(s0);
cbuffer cb1 : register(b1) { float4 material[10]; }
void main(float4 position : SV_Position, float2 uv : TEXCOORD0) {
  if (t0.SampleBias(s0, uv, -1.f).w * material[9].y - 0.3333f < 0.f) discard;
})";

// Tales' scaled masks: the UV times a material register (cb1[1].xy, cb1[11].zz) right before the sample.
constexpr const char* SCALED_XY = R"(
Texture2D<float4> t0 : register(t0);
SamplerState s0 : register(s0);
cbuffer cb1 : register(b1) { float4 material[10]; }
void main(float4 position : SV_Position, float2 uv : TEXCOORD0) {
  if (t0.SampleBias(s0, uv * material[1].xy, -1.f).w * material[9].y - 0.3333f < 0.f) discard;
})";
constexpr const char* SCALED_ZZ = R"(
Texture2D<float4> t0 : register(t0);
SamplerState s0 : register(s0);
cbuffer cb1 : register(b1) { float4 material[15]; }
void main(float4 position : SV_Position, float2 uv : TEXCOORD0) {
  if (t0.SampleBias(s0, uv * material[11].zz, -1.f).w * material[14].x - 0.3333f < 0.f) discard;
})";
// CONTROLS: a UV moved (scale and offset), a UV scaled by a computed value: left out, as before.
constexpr const char* MOVED = R"(
Texture2D<float4> t0 : register(t0);
SamplerState s0 : register(s0);
cbuffer cb1 : register(b1) { float4 material[10]; }
void main(float4 position : SV_Position, float2 uv : TEXCOORD0) {
  if (t0.SampleBias(s0, uv * material[1].xy + material[1].zw, -1.f).w - 0.3333f < 0.f) discard;
})";
constexpr const char* COMPUTED = R"(
Texture2D<float4> t0 : register(t0);
SamplerState s0 : register(s0);
cbuffer cb1 : register(b1) { float4 material[10]; }
void main(float4 position : SV_Position, float2 uv : TEXCOORD0) {
  if (t0.SampleBias(s0, uv * frac(material[1].xy * position.xy), -1.f).w - 0.3333f < 0.f) discard;
})";

std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 && argc != 4) {
    std::printf("usage: tests.exe <repository> [<dump folder> <masked_ps.tsv>]\n");
    return 2;
  }
  const std::string repository = argv[1];
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
  core::PixelCoverage coverage;
  const auto masked = Bind(MASKED, &coverage);
  CHECK(masked && coverage.coverage == core::Coverage::MASKED && coverage.uv_semantic == "TEXCOORD" && coverage.uv_index == 0);

  // 1. Host::leaf_cutoff.
  const std::array<SceneShader, 2> listed = {SceneShader{.hash = 1, .alpha_cutoff = 0.01f}, SceneShader{.hash = 2}};
  for (const bool replayed : {false, true}) {
    const auto install = [replayed](const Host& host, std::span<const SceneShader> shaders) {
      if (replayed) {
        twattila::rt_scene::InstallReplayedScene(host);
      } else {
        twattila::rt_scene::InstallScene(shaders, host);
      }
    };
    install(Host{.scene_draw = NoDraw, .leaf_cutoff = 0.333f}, {});
    SceneShader shader;
    CHECK(twattila::rt_scene::SceneCoverage(context.Get(), &shader));
    CHECK(shader.foliage && shader.alpha_cutoff == 0.333f && core::leaf_cutoff == 0.333f);
    // CONTROL: the rule before for this host (no listed shaders: 0).
    const float before = 0.f;
    CHECK(before != core::leaf_cutoff);
    // Default: as before.
    install(Host{.scene_draw = NoDraw}, {});
    SceneShader plain;
    SceneShader own = {.alpha_cutoff = 0.01f};
    CHECK(twattila::rt_scene::SceneCoverage(context.Get(), &plain) && twattila::rt_scene::SceneCoverage(context.Get(), &own));
    CHECK(plain.foliage && plain.alpha_cutoff == 0.f && own.alpha_cutoff == 0.01f && core::leaf_cutoff == 0.f);
    std::printf("part 1 (%s): host 0.333: draw %.3f, bridge %.3f (CONTROL the rule before: %.3f); default: draw %.3f / own %.3f kept, bridge %.3f\n",
                replayed ? "InstallReplayedScene" : "InstallScene", shader.alpha_cutoff, 0.333f, before, plain.alpha_cutoff, own.alpha_cutoff,
                core::leaf_cutoff);
  }
  // Listed shaders (the TW games: no scene_draw): their largest cut-off, as before.
  twattila::rt_scene::InstallScene(listed, Host{});
  CHECK(core::leaf_cutoff == 0.01f);
  twattila::rt_scene::InstallScene(listed, Host{.leaf_cutoff = 0.333f});
  CHECK(core::leaf_cutoff == 0.333f);
  std::printf("part 1: listed shaders 0.01 -> bridge %.3f as before; with the host's 0.333 the larger\n", 0.01f);

  // 2. Masks at a scaled UV (ClassifyCoverage): MASKED with the register right before the sample.
  struct Shape {
    const char* name;
    const char* source;
    core::Coverage coverage;
    UINT slot;
    UINT register_index;
    UINT components;
  };
  const std::array<Shape, 5> shapes = {{
      {"unscaled", MASKED, core::Coverage::MASKED, twattila::rt_scene::NO_UV_SCALE, 0, 0},
      {"x cb1[1].xy", SCALED_XY, core::Coverage::MASKED, 1, 1, 0 | (1 << 2)},
      {"x cb1[11].zz", SCALED_ZZ, core::Coverage::MASKED, 1, 11, 2 | (2 << 2)},
      {"CONTROL x cb1[1].xy + cb1[1].zw", MOVED, core::Coverage::LEFT_OUT, twattila::rt_scene::NO_UV_SCALE, 0, 0},
      {"CONTROL x frac(computed)", COMPUTED, core::Coverage::LEFT_OUT, twattila::rt_scene::NO_UV_SCALE, 0, 0},
  }};
  std::array<ComPtr<ID3D11PixelShader>, 5> bound;
  for (size_t i = 0; i < shapes.size(); ++i) {
    core::PixelCoverage found;
    bound[i] = Bind(shapes[i].source, &found);
    CHECK(bound[i]);
    std::printf("part 2: %-32s %s", shapes[i].name, found.coverage == core::Coverage::MASKED ? "masked" : (found.coverage == core::Coverage::LEFT_OUT ? "left out" : "solid"));
    if (found.uv_scale_slot != twattila::rt_scene::NO_UV_SCALE) std::printf(" at cb%u[%u] components %u", found.uv_scale_slot, found.uv_scale_register, found.uv_scale_components);
    std::printf("\n");
    CHECK(found.coverage == shapes[i].coverage && found.uv_scale_slot == shapes[i].slot);
    if (found.uv_scale_slot != twattila::rt_scene::NO_UV_SCALE) {
      CHECK(found.uv_scale_register == shapes[i].register_index && found.uv_scale_components == shapes[i].components && found.uv_semantic == "TEXCOORD");
    }
  }

  // 3. SceneCoverage: a scaled mask stays out (as before) unless the host scales UVs; then in, with its register.
  context->PSSetShader(bound[1].Get(), nullptr, 0);
  twattila::rt_scene::InstallReplayedScene(Host{.scene_draw = NoDraw, .leaf_cutoff = 0.333f});
  SceneShader out;
  CHECK(!twattila::rt_scene::SceneCoverage(context.Get(), &out));
  twattila::rt_scene::InstallReplayedScene(Host{.scene_draw = NoDraw, .leaf_cutoff = 0.333f, .mask_uv_scales = true});
  SceneShader in;
  CHECK(twattila::rt_scene::SceneCoverage(context.Get(), &in));
  CHECK(in.foliage && in.uv_scale_slot == 1 && in.uv_scale_register == 1 && in.uv_scale_components == 4 && in.alpha_cutoff == 0.333f);
  context->PSSetShader(bound[0].Get(), nullptr, 0);
  SceneShader unscaled;
  CHECK(twattila::rt_scene::SceneCoverage(context.Get(), &unscaled) && unscaled.uv_scale_slot == twattila::rt_scene::NO_UV_SCALE);
  std::printf("part 3: scaled mask: default host left out (as before), Host::mask_uv_scales in at cb%u[%u]; unscaled unchanged\n", in.uv_scale_slot,
              in.uv_scale_register);

  // 4. rt_vertices on WARP, this add-on's (RT_VERTICES_UV_SCALES) against the plain build (the include before): 4096 clip-space
  //    vertices (identity camera), UVs 0.1 .. 1.1; scaled ranges [100, 400) x .xy, [1000, 1003) x .zz, [4000, 4096) x .xw of
  //    their registers. Expected: the plain build's output with those UVs scaled on the CPU (the same float products), bit for
  //    bit. No ranges: the plain build's bits. CONTROL: the plain build with the ranges bound leaves them unscaled.
  {
    constexpr UINT VERTICES = 4096;
    constexpr UINT WIDTH = 1024;
    std::vector<std::array<float, 4>> positions(VERTICES);
    std::vector<std::array<float, 2>> uvs(VERTICES);
    for (UINT i = 0; i < VERTICES; ++i) {
      positions[i] = {(i % 64) / 32.f - 1.f, (i / 64) / 32.f - 1.f, 0.5f, 1.f};
      uvs[i] = {0.1f + (0.001f * static_cast<float>(i % 1000)), 1.1f - (0.0007f * static_cast<float>(i % 1300))};
    }
    const std::array<std::array<uint32_t, 4>, 3> ranges = {{{100, 300, 0, 1}, {1000, 3, 2, 2}, {4000, 96, 0, 3}}};
    const std::array<std::array<float, 4>, 3> registers = {{{4.f, 2.5f, 7.f, 9.f}, {1.f, 1.f, 3.25f, 1.f}, {0.5f, 6.f, 6.f, 12.f}}};
    std::vector<std::array<float, 2>> scaled_uvs = uvs;
    for (size_t r = 0; r < ranges.size(); ++r) {
      for (uint32_t i = ranges[r][0]; i < ranges[r][0] + ranges[r][1]; ++i) {
        scaled_uvs[i] = {uvs[i][0] * registers[r][ranges[r][2]], uvs[i][1] * registers[r][ranges[r][3]]};
      }
    }
    const auto buffer = [&](const void* data, UINT bytes, DXGI_FORMAT format, UINT elements) {
      const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
      const D3D11_SUBRESOURCE_DATA init = {.pSysMem = data};
      ComPtr<ID3D11Buffer> made;
      ComPtr<ID3D11ShaderResourceView> view;
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {.Format = format, .ViewDimension = D3D11_SRV_DIMENSION_BUFFER};
      view_desc.Buffer.NumElements = elements;
      device->CreateBuffer(&desc, &init, &made);
      device->CreateShaderResourceView(made.Get(), &view_desc, &view);
      return view;
    };
    const auto captured = buffer(positions.data(), VERTICES * 16, DXGI_FORMAT_R32G32B32A32_FLOAT, VERTICES);
    const auto captured_uv = buffer(uvs.data(), VERTICES * 8, DXGI_FORMAT_R32G32_FLOAT, VERTICES);
    const auto scaled_uv = buffer(scaled_uvs.data(), VERTICES * 8, DXGI_FORMAT_R32G32_FLOAT, VERTICES);
    const auto range_view = buffer(ranges.data(), sizeof(ranges), DXGI_FORMAT_R32G32B32A32_UINT, 3);
    const auto register_view = buffer(registers.data(), sizeof(registers), DXGI_FORMAT_R32G32B32A32_FLOAT, 3);
    const D3D11_TEXTURE2D_DESC out_desc = {WIDTH, VERTICES / WIDTH, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                           D3D11_BIND_UNORDERED_ACCESS, 0, 0};
    ComPtr<ID3D11Texture2D> out;
    ComPtr<ID3D11UnorderedAccessView> out_uav;
    CHECK(SUCCEEDED(device->CreateTexture2D(&out_desc, nullptr, &out)) && SUCCEEDED(device->CreateUnorderedAccessView(out.Get(), nullptr, &out_uav)));
    D3D11_TEXTURE2D_DESC read_desc = out_desc;
    read_desc.Usage = D3D11_USAGE_STAGING, read_desc.BindFlags = 0, read_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> read;
    CHECK(SUCCEEDED(device->CreateTexture2D(&read_desc, nullptr, &read)));
    // rt_core's VertexConstants: counts, the inverse view-projection (identity), world_part (x WORLD_BASE, y the ranges).
    struct VertexConstants {
      std::array<uint32_t, 4> counts;
      std::array<float, 16> inverse_view_projection;
      std::array<uint32_t, 4> world_part;
    };
    const D3D11_BUFFER_DESC constants_desc = {.ByteWidth = sizeof(VertexConstants), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    ComPtr<ID3D11Buffer> constants;
    CHECK(SUCCEEDED(device->CreateBuffer(&constants_desc, nullptr, &constants)));
    const auto run = [&](ID3D11ComputeShader* shader, ID3D11ShaderResourceView* uv, uint32_t range_count) {
      const VertexConstants values = {.counts = {VERTICES, WIDTH, 1u, 0u},
                                      .inverse_view_projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                                      .world_part = {0u, range_count, 0u, 0u}};
      context->UpdateSubresource(constants.Get(), 0, nullptr, &values, 0, 0);
      const std::array<ID3D11ShaderResourceView*, 4> views = {captured.Get(), uv, range_view.Get(), register_view.Get()};
      context->CSSetShader(shader, nullptr, 0);
      context->CSSetShaderResources(0, 4, views.data());
      context->CSSetConstantBuffers(0, 1, constants.GetAddressOf());
      context->CSSetUnorderedAccessViews(0, 1, out_uav.GetAddressOf(), nullptr);
      context->Dispatch((VERTICES + 255) / 256, 1, 1);
      context->ClearState();
      context->CopyResource(read.Get(), out.Get());
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      context->Map(read.Get(), 0, D3D11_MAP_READ, 0, &mapped);
      std::vector<uint32_t> words(size_t{VERTICES} * 4);
      for (UINT y = 0; y < VERTICES / WIDTH; ++y) {
        std::memcpy(words.data() + (size_t{y} * WIDTH * 4), static_cast<const char*>(mapped.pData) + (size_t{y} * mapped.RowPitch), WIDTH * 16);
      }
      context->Unmap(read.Get(), 0);
      return words;
    };
    ComPtr<ID3D11ComputeShader> built;
    CHECK(SUCCEEDED(device->CreateComputeShader(__rt_vertices.data(), __rt_vertices.size(), nullptr, &built)));
    // The plain build: the shared source without RT_VERTICES_UV_SCALES, as the build compiles it.
    const std::wstring source = std::wstring(repository.begin(), repository.end()) + L"\\src\\games\\twattila\\rt_vertices.cs_5_0.hlsl";
    const std::array<D3D_SHADER_MACRO, 3> macros = {{{"__SHADER_TARGET_MAJOR", "5"}, {"__SHADER_TARGET_MINOR", "0"}, {nullptr, nullptr}}};
    ComPtr<ID3DBlob> plain_code;
    ComPtr<ID3DBlob> errors;
    CHECK(SUCCEEDED(D3DCompileFromFile(source.c_str(), macros.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0",
                                       D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY, 0, &plain_code, &errors)));
    ComPtr<ID3D11ComputeShader> plain;
    CHECK(SUCCEEDED(device->CreateComputeShader(plain_code->GetBufferPointer(), plain_code->GetBufferSize(), nullptr, &plain)));
    const auto differing = [](const std::vector<uint32_t>& left, const std::vector<uint32_t>& right) {
      size_t count = 0;
      for (size_t i = 0; i < left.size(); ++i) count += left[i] != right[i] ? 1 : 0;
      return count;
    };
    const auto expected = run(plain.Get(), scaled_uv.Get(), 0);
    const auto none = run(built.Get(), captured_uv.Get(), 0);
    const auto plain_none = run(plain.Get(), captured_uv.Get(), 0);
    const auto scaled = run(built.Get(), captured_uv.Get(), 3);
    const auto control = run(plain.Get(), captured_uv.Get(), 3);
    const size_t none_apart = differing(none, plain_none);
    const size_t scaled_apart = differing(scaled, expected);
    const size_t control_apart = differing(control, expected);
    std::printf("part 4: rt_vertices, no ranges: %zu of %u values apart from the plain build; ranges: %zu apart from the CPU's products "
                "(CONTROL the plain build with them: %zu apart, %u vertices scaled)\n",
                none_apart, VERTICES * 4, scaled_apart, control_apart, 300 + 3 + 96);
    CHECK(none_apart == 0 && scaled_apart == 0 && control_apart > 0);
  }

  // 5. Capture's copy of the register: 16 bytes of a dynamic constant buffer (as the game's) at (first constant + register) x 16
  //    into the registers' buffer at a draw's place. CONTROL: the next register differs.
  {
    std::array<float, 64> material = {};
    for (size_t i = 0; i < material.size(); ++i) material[i] = static_cast<float>(i) + 0.5f;
    const D3D11_BUFFER_DESC material_desc = {.ByteWidth = sizeof(material), .Usage = D3D11_USAGE_DYNAMIC, .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
                                             .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE};
    ComPtr<ID3D11Buffer> material_buffer;
    CHECK(SUCCEEDED(device->CreateBuffer(&material_desc, nullptr, &material_buffer)));
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    CHECK(SUCCEEDED(context->Map(material_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
    std::memcpy(mapped.pData, material.data(), sizeof(material));
    context->Unmap(material_buffer.Get(), 0);
    const D3D11_BUFFER_DESC registers_desc = {.ByteWidth = 64, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    ComPtr<ID3D11Buffer> registers;
    CHECK(SUCCEEDED(device->CreateBuffer(&registers_desc, nullptr, &registers)));
    constexpr UINT REGISTER = 11;
    const D3D11_BOX box = {.left = REGISTER * 16, .top = 0, .front = 0, .right = (REGISTER * 16) + 16, .bottom = 1, .back = 1};
    context->CopySubresourceRegion(registers.Get(), 0, 2 * 16, 0, 0, material_buffer.Get(), 0, &box);
    const D3D11_BUFFER_DESC read_desc = {.ByteWidth = 64, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Buffer> read;
    CHECK(SUCCEEDED(device->CreateBuffer(&read_desc, nullptr, &read)));
    context->CopyResource(read.Get(), registers.Get());
    CHECK(SUCCEEDED(context->Map(read.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    std::array<float, 16> copied = {};
    std::memcpy(copied.data(), mapped.pData, sizeof(copied));
    context->Unmap(read.Get(), 0);
    const bool same = std::equal(copied.begin() + 8, copied.begin() + 12, material.begin() + (REGISTER * 4));
    const bool next = std::equal(copied.begin() + 8, copied.begin() + 12, material.begin() + ((REGISTER + 1) * 4));
    std::printf("part 5: register %u copied from a dynamic constant buffer: (%.1f %.1f %.1f %.1f) %s; CONTROL register %u %s\n", REGISTER,
                copied[8], copied[9], copied[10], copied[11], same ? "as written" : "OTHER", REGISTER + 1, next ? "the same (FAIL)" : "differs");
    CHECK(same && !next);
  }

  // 6. Tales' dumped masks against the scanner's table (its scale: (slot, register, 'uv components') or None).
  if (argc == 4) {
    std::ifstream table(argv[3]);
    std::string line;
    int masks = 0, scaled_table = 0, scaled_found = 0, unscaled_kept = 0, left_out = 0, wrong = 0;
    while (std::getline(table, line)) {
      std::vector<std::string> fields;
      std::stringstream row(line);
      for (std::string field; std::getline(row, field, '\t');) fields.push_back(field);
      if (fields.size() < 5 || fields[4] != "mask") continue;
      const auto code = ReadFile(std::string(argv[2]) + "\\0x" + fields[0] + ".ps_5_0.cso");
      if (code.empty()) continue;
      ++masks;
      const core::PixelCoverage found = core::ClassifyCoverage(code);
      UINT slot = twattila::rt_scene::NO_UV_SCALE, index = 0, components = 0;
      if (fields[2] != "None") {
        ++scaled_table;
        unsigned table_slot = 0, table_register = 0;
        char u = 'x', v = 'y';
        std::sscanf(fields[2].c_str(), "(%u, %u, '%c%c')", &table_slot, &table_register, &u, &v);
        const auto component = [](char c) { return static_cast<UINT>(c == 'w' ? 3 : c - 'x'); };
        slot = table_slot, index = table_register, components = component(u) | (component(v) << 2);
      }
      if (found.coverage != core::Coverage::MASKED) {
        ++left_out;
        std::printf("part 6: %s left out (table: %s)\n", fields[0].c_str(), fields[2].c_str());
        continue;
      }
      const bool same = found.uv_scale_slot == slot && (slot == twattila::rt_scene::NO_UV_SCALE
                                                        || (found.uv_scale_register == index && found.uv_scale_components == components));
      if (!same) {
        ++wrong;
        std::printf("part 6: %s at cb%u[%u] components %u, the table's %s\n", fields[0].c_str(), found.uv_scale_slot, found.uv_scale_register,
                    found.uv_scale_components, fields[2].c_str());
      } else if (slot != twattila::rt_scene::NO_UV_SCALE) {
        ++scaled_found;
      } else {
        ++unscaled_kept;
      }
    }
    std::printf("part 6: %d dumped masks: %d of the table's %d scaled ones masked at its register (left out before), %d unscaled masked as before, "
                "%d left out, %d at another register\n",
                masks, scaled_found, scaled_table, unscaled_kept, left_out, wrong);
    CHECK(masks > 0 && scaled_found > 0 && wrong == 0);
  }
  std::printf("PASS\n");
  return 0;
}
