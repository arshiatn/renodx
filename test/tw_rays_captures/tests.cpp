// The rays' captures of draws the shared capture code needed a new path for (src/games/twattila/rt_core.hpp), on WARP against
// the games' dumped bytes.
// The sprite soldiers (SceneShader::capture_geometry_shader): the imposter vertex shader (vs40_main_gs) streamed out through its
// game's gs_main and through twattila/rt_capture_imposter_gs (the header the add-ons embed), per game:
//   Warhammer 381E079E + 12D3A4BC (shader model 4.0) and 883D12E7 + D6954DFC (4.1), Troy 370A7440 + DC05F551, Warhammer II
//   370A7440 + 29E8BDF7, Pharaoh 370A7440 + 27A16995.
// 256 soldiers, half standing, half flat (v1.w below 0), random cards and atlas cells (some wrapping). The camera 1000 km away
// level with them along a direction, rt_sun's texel 0 that direction: the game turns each card to the camera, the capture to
// the sun, so they must match.
// A. every corner's world position (the game's TEXCOORD2.xyz) within 2e-4 m of the capture's, in the strip's order;
// U. every corner's UV (the game's TEXCOORD1.xy) as the capture's two halves (rt_capture.hlsli RtWorldUv) within a half's step;
// N. the capture writes 2 triangles a soldier (its SO statistics): the count rt_core assumes (6 corners a point);
// C. CONTROL: the camera turned 90 degrees from the sun: corners differ by more than 0.1 m.
// T. The tessellated rigid objects (SceneShader::patch_triangles, Rarer Objects in the Rays), as test/tw_attila_builds' 9AEE504E:
//   Warhammer E168387B (HS F04A8DF4, DS 7C266A97) through F6DBA4F4's capture and rows, Warhammer II 4CC2D68F (HS EB3218C8, DS
//   C92A2D5F) through 16AFAF14's, Warhammer III D5D2905E and 958EC025 (HS FE7929BE, DS 187E9195, its world at the domain shader's
//   TEXCOORD1) through its rt_capture_rigid and rt_rows_rigid, Rome II E51A2D2A (HS E2BF92FC, DS A13A267D) through F343950E's
//   (the add-ons' built shaders; Rome II's from build32); 64 patches of 3, each its placement:
//   T1 the capture's TEXCOORD0 (a triangle list, no hull or domain shader) is the game vertex shader's world position (its
//      TEXCOORD0) bit for bit, w 1;
//   T2 the rows shader's TEXCOORD0-2 are POSITION1-3 bit for bit;
//   T3 the game's chain (Tessellation 4, displacement 0.1) with the camera 17 km away (past 20 m: no tessellation, the
//      displacement faded out) streams out the capture's triangles, patch for patch, their corners bit for bit;
//   T4 CONTROL: the camera among the patches (under 5 m: Tessellation 4, the displacement in full): the chain's triangles leave
//      the capture's plane, by at most the displacement.
// G. Rome II's tessellated terrain (rigidterrain vs50_geom B1C7E29F, HS BE9FC167, DS 93979D65: Phong tessellation, the factor
//   the CPU's POSITION4.x, 1 where the ground is flat) through 83AB3AFC's capture, 64 patches of 3 on a random height map:
//   G1 the capture's TEXCOORD0 is the game vertex shader's world position (its TEXCOORD1) bit for bit, w 1;
//   G2 the game's chain at factor 1 streams out the capture's triangles, patch for patch, each corner within 1e-6 (the domain
//      shader's 0.2 / 0.8 Phong blend at a corner);
//   G3 CONTROL: at factor 4 with the corners' normals apart the chain's triangles leave the capture's plane.
// B. Batched Unit Capture (twattila/rt_core.hpp: Capture's batched draws, FlushBatch), Attila's main units 5F9CD405 (shader model 3)
//   and A691CFF4 (4.1) with their capture versions rt_capture_unit_sm3 / sm41 (build32's twattila.include\embed): 3 draws as the game
//   draws them (VB0 stride 28: POSITION0 R16G16B16A16_FLOAT, POSITION1-5 R8G8B8A8_UNORM but POSITION3 R16G16_FLOAT; VB1 stride 120 an
//   instance: TEXCOORD0-6 R32G32B32A32_FLOAT, TEXCOORD7 R32G32_FLOAT; the input layout made for the game's shader), each through its
//   mesh's remap indices as points, its own base vertex and first instance, two animation textures 512 wide, the second draw's place
//   in `captured` 100 texels past the first's end, the third's right after the second's:
//   B1 the batched corners (rt_batch_unit_mesh + rt_batch_unit_rows streamed at the draws, rt_batch_unit_sm3 / sm41 over them in
//      FlushBatch's runs, copied into `captured`) are the per-draw stream output's bit for bit, w 1;
//   B2 the second draw marked stale, and the third drawn with a width of 256 at b2: their corners NaN, the first's still bit for bit;
//   B3 CONTROL: the first draw's animation texture written again after the draws, before the flush: its corners differ.
// tests.exe <dump root: the folder holding dump_shader_<game>_pack\cso> <build root: the folder holding <game>.include\embed>
//           <build32 root: the same for the 32-bit games>
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
#include <fstream>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "../../src/games/twattila/rt_capture_imposter_gs.h"

static ID3D11Device* device = nullptr;
static ID3D11DeviceContext* context = nullptr;
constexpr UINT SOLDIERS = 256;
constexpr UINT PATCHES = 64;

static std::vector<uint8_t> Load(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// rt_capture.hlsli's RtHalfBits.
static uint32_t HalfBits(float value) {
  const uint32_t bits = std::bit_cast<uint32_t>(value);
  const uint32_t sign = (bits >> 16) & 0x8000u;
  const int exponent = static_cast<int>((bits >> 23) & 0xFFu) - 112;
  if (exponent <= 0) return sign;
  if (exponent >= 31) return sign | 0x7C00u;
  return sign | (static_cast<uint32_t>(exponent) << 10) | ((bits >> 13) & 0x3FFu);
}

static float HalfValue(uint32_t half) {
  const float magnitude = ((half & 0x7C00u) == 0u) ? 0.f : std::ldexp(1.f + static_cast<float>(half & 0x3FFu) / 1024.f, static_cast<int>((half >> 10) & 0x1Fu) - 15);
  return ((half & 0x8000u) != 0u) ? -magnitude : magnitude;
}

static ID3D11Buffer* MakeConstants(std::span<const float> values) {
  const D3D11_BUFFER_DESC desc = {UINT(values.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
  const D3D11_SUBRESOURCE_DATA data = {values.data(), 0, 0};
  ID3D11Buffer* buffer = nullptr;
  device->CreateBuffer(&desc, &data, &buffer);
  return buffer;
}

// One draw of `vertices` (a float4 an input of the vertex shader's input signature, system values left out) through stream
// output, nothing rasterized: the vertex shader, then the game's tessellation (hs and ds) or a geometry shader (gs) when given;
// the last stage's `semantic` / `index` (`components` floats a vertex), as many as it wrote.
struct Draw {
  const std::vector<uint8_t>* vs;
  std::span<const uint8_t> gs = {};
  const std::vector<uint8_t>* hs = nullptr;
  const std::vector<uint8_t>* ds = nullptr;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
  std::array<ID3D11Buffer*, 3> vs_constants = {};  // b0-b2 (b0 the geometry shader's too)
  std::array<ID3D11Buffer*, 2> tessellation_constants = {};  // the hull and domain shaders' b0-b1
  ID3D11ShaderResourceView* gs_view = nullptr;
  ID3D11ShaderResourceView* ds_view = nullptr;
  ID3D11SamplerState* ds_sampler = nullptr;
  // Bytes a vertex of `vertices` (0: a float4 an input of this vertex shader): the game shader's, when this one reads a first part
  // of its inputs (the tessellated games' capture and rows shaders).
  UINT stride = 0;
  ID3D11ShaderResourceView* vs_view = nullptr;
  ID3D11SamplerState* vs_sampler = nullptr;
};

struct Streamed {
  std::vector<float> values;  // `components` floats a vertex
  uint64_t primitives = 0;    // the SO statistics' NumPrimitivesWritten
};

static Streamed StreamOut(const Draw& draw, const std::vector<float>& vertices, const char* semantic, UINT index, BYTE components) {
  Streamed out;
  ID3D11ShaderReflection* reflection = nullptr;
  if (FAILED(D3DReflect(draw.vs->data(), draw.vs->size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(&reflection)))) return out;
  D3D11_SHADER_DESC desc = {};
  reflection->GetDesc(&desc);
  std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
  for (UINT i = 0; i < desc.InputParameters; ++i) {
    D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
    reflection->GetInputParameterDesc(i, &parameter);
    if (parameter.SystemValueType != D3D_NAME_UNDEFINED) continue;
    elements.push_back({parameter.SemanticName, parameter.SemanticIndex, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, UINT(elements.size() * 16), D3D11_INPUT_PER_VERTEX_DATA, 0});
  }
  const UINT stride = (draw.stride != 0) ? draw.stride : UINT(elements.size() * 16);
  const UINT count = UINT(vertices.size() * 4 / stride);
  const std::span<const uint8_t> last = !draw.gs.empty() ? draw.gs : std::span<const uint8_t>(draw.ds != nullptr ? *draw.ds : *draw.vs);
  const D3D11_SO_DECLARATION_ENTRY entry = {0, semantic, index, 0, components, 0};
  const UINT out_stride = components * 4u;
  const UINT room = count * 64;  // a tessellated patch's vertices
  const D3D11_BUFFER_DESC vertex_desc = {UINT(vertices.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  const D3D11_SUBRESOURCE_DATA vertex_data = {vertices.data(), 0, 0};
  const D3D11_BUFFER_DESC out_desc = {room * out_stride, D3D11_USAGE_DEFAULT, D3D11_BIND_STREAM_OUTPUT, 0, 0, 0};
  const D3D11_BUFFER_DESC read_desc = {room * out_stride, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
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
  const bool made = SUCCEEDED(device->CreateInputLayout(elements.data(), UINT(elements.size()), draw.vs->data(), draw.vs->size(), &layout))
                    && SUCCEEDED(device->CreateBuffer(&vertex_desc, &vertex_data, &vertex_buffer))
                    && SUCCEEDED(device->CreateVertexShader(draw.vs->data(), draw.vs->size(), nullptr, &vs))
                    && (draw.hs == nullptr || SUCCEEDED(device->CreateHullShader(draw.hs->data(), draw.hs->size(), nullptr, &hs)))
                    && (draw.ds == nullptr || SUCCEEDED(device->CreateDomainShader(draw.ds->data(), draw.ds->size(), nullptr, &ds)))
                    && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(last.data(), last.size(), &entry, 1, &out_stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &gs))
                    && SUCCEEDED(device->CreateBuffer(&out_desc, nullptr, &output)) && SUCCEEDED(device->CreateBuffer(&read_desc, nullptr, &read))
                    && SUCCEEDED(device->CreateQuery(&query_desc, &query));
  if (made) {
    const UINT offset = 0;
    context->IASetInputLayout(layout);
    context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
    context->IASetPrimitiveTopology(draw.topology);
    context->VSSetShader(vs, nullptr, 0);
    context->HSSetShader(hs, nullptr, 0);
    context->DSSetShader(ds, nullptr, 0);
    context->GSSetShader(gs, nullptr, 0);
    context->PSSetShader(nullptr, nullptr, 0);
    context->VSSetConstantBuffers(0, 3, draw.vs_constants.data());
    context->GSSetConstantBuffers(0, 1, draw.vs_constants.data());
    context->HSSetConstantBuffers(0, 2, draw.tessellation_constants.data());
    context->DSSetConstantBuffers(0, 2, draw.tessellation_constants.data());
    context->GSSetShaderResources(0, 1, &draw.gs_view);
    context->DSSetShaderResources(0, 1, &draw.ds_view);
    context->DSSetSamplers(0, 1, &draw.ds_sampler);
    context->VSSetShaderResources(0, 1, &draw.vs_view);
    context->VSSetSamplers(0, 1, &draw.vs_sampler);
    context->SOSetTargets(1, &output, &offset);
    context->Begin(query);
    context->Draw(count, 0);
    context->End(query);
    context->SOSetTargets(0, nullptr, nullptr);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    D3D11_QUERY_DATA_SO_STATISTICS statistics = {};
    while (context->GetData(query, &statistics, sizeof(statistics), 0) == S_FALSE) {
    }
    out.primitives = statistics.NumPrimitivesWritten;
    // A point list streamed by the vertex shader alone writes points, anything else triangles.
    const UINT corners = (draw.topology == D3D11_PRIMITIVE_TOPOLOGY_POINTLIST && draw.gs.empty()) ? 1u : 3u;
    const UINT written = UINT(std::min<UINT64>(statistics.NumPrimitivesWritten * corners, room));
    context->CopyResource(read, output);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context->Map(read, 0, D3D11_MAP_READ, 0, &mapped))) {
      const auto* floats = static_cast<const float*>(mapped.pData);
      out.values.assign(floats, floats + (size_t(written) * components));
      context->Unmap(read, 0);
    }
  } else {
    std::printf("  could not make the stream output (%s %u)\n", semantic, index);
  }
  for (IUnknown* object : std::initializer_list<IUnknown*>{layout, vertex_buffer, vs, hs, ds, gs, output, read, query, reflection}) {
    if (object != nullptr) object->Release();
  }
  return out;
}

static std::mt19937 rng(7);
static float Range(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }

// camera b0: camera_position c0, view_projection c9-c12 identity (the soldiers: only TEXCOORD2 / TEXCOORD1 are compared), the rest
// 0 (c3 too: the hull shaders cull no patch with a NaN direction); 48 registers, Warhammer II's.
static ID3D11Buffer* MakeCamera(const std::array<float, 3>& position) {
  std::array<float, 48 * 4> constants = {};
  std::copy(position.begin(), position.end(), constants.begin());
  for (int row = 0; row < 4; ++row) constants[((9 + row) * 4) + row] = 1.f;
  return MakeConstants(constants);
}

static bool SameBits(const float* a, const float* b, size_t count) { return std::memcmp(a, b, count * 4) == 0; }

// T for one game (see the top).
struct TessellatedGame {
  const char* game;
  const char* pack;
  const char* vs;
  const char* hs;
  const char* ds;
  const char* capture;  // the add-on's built shaders (<game>.include\embed)
  const char* rows;
  UINT factor;          // rigid_config_PS: g_tesselation_factor's float, g_tesselation_displacement_scale the next
  UINT chain_world = 0;       // the domain shader's TEXCOORD holding the world position (w 1)
  bool config_at_b1 = false;  // the vertex shader's rigid_config_PS at b1, wind_VS at b2 (Rome II); false: the wind at b1
};

static bool CheckTessellated(const TessellatedGame& game, const std::string& dump_root, const std::string& build_root) {
  const std::string dump = dump_root + "\\dump_shader_" + game.pack + "_pack\\cso\\";
  const std::string built = build_root + "\\" + game.pack + ".include\\embed\\";
  const auto vs = Load(dump + game.vs + ".cso");
  const auto hs = Load(dump + game.hs + ".cso");
  const auto ds = Load(dump + game.ds + ".cso");
  const auto capture = Load(built + game.capture + ".cso");
  const auto rows = Load(built + game.rows + ".cso");
  std::printf("T %s: vs %s, hs %s, ds %s; %s, %s (%u patches)\n", game.game, game.vs, game.hs, game.ds, game.capture, game.rows, PATCHES);
  if (vs.empty() || hs.empty() || ds.empty() || capture.empty() || rows.empty()) {
    std::printf("  MISSING a shader's bytes\n");
    return false;
  }
  // The game vertex shader's inputs: POSITION0, TEXCOORD0, NORMAL0-2, COLOR0, POSITION1-6 (Rome II: 12), POSITION7 (Warhammer,
  // Warhammer II: 13), then POSITION8-9 (Warhammer III: 15) and the last rows POSITION10-12 (its 958EC025: 18); system values
  // left out. POSITION0 and the rows small (the world within 1.25 m of the origin), each patch its placement.
  UINT inputs = 0;
  ID3D11ShaderReflection* reflection = nullptr;
  if (SUCCEEDED(D3DReflect(vs.data(), vs.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(&reflection)))) {
    D3D11_SHADER_DESC desc = {};
    reflection->GetDesc(&desc);
    for (UINT i = 0; i < desc.InputParameters; ++i) {
      D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
      reflection->GetInputParameterDesc(i, &parameter);
      inputs += (parameter.SystemValueType == D3D_NAME_UNDEFINED) ? 1u : 0u;
    }
    reflection->Release();
  }
  const UINT stride = inputs * 16;
  std::vector<float> vertices;
  for (UINT patch = 0; patch < PATCHES; ++patch) {
    std::array<std::array<float, 4>, 3> placement = {};
    for (auto& row : placement) row = {Range(-0.5f, 0.5f), Range(-0.5f, 0.5f), Range(-0.5f, 0.5f), Range(-0.5f, 0.5f)};
    for (int corner = 0; corner < 3; ++corner) {
      for (UINT input = 0; input < inputs; ++input) {
        if (input == 0) {
          vertices.insert(vertices.end(), {Range(-0.5f, 0.5f), Range(-0.5f, 0.5f), Range(-0.5f, 0.5f), 1.f});
        } else if (input >= 6 && input <= 8) {
          vertices.insert(vertices.end(), placement[input - 6].begin(), placement[input - 6].end());
        } else {
          vertices.insert(vertices.end(), {Range(0.f, 1.f), Range(0.f, 1.f), Range(0.f, 1.f), Range(0.f, 1.f)});
        }
      }
    }
  }
  // rigid_config_PS: Tessellation 4, displacement 0.1, g_pixel_selection_active 0; the wind 0.
  std::array<float, 5 * 4> config_values = {};
  config_values[game.factor] = 4.f;
  config_values[game.factor + 1] = 0.1f;
  const std::array<float, 2 * 4> no_wind = {};
  ID3D11Buffer* config = MakeConstants(config_values);
  ID3D11Buffer* wind = MakeConstants(no_wind);
  ID3D11Buffer* far_camera = MakeCamera({1e4f, 1e4f, 1e4f});
  ID3D11Buffer* near_camera = MakeCamera({0.f, 0.f, 0.f});
  std::array<float, 16 * 16 * 4> texels = {};
  for (float& texel : texels) texel = Range(0.f, 1.f);
  const D3D11_TEXTURE2D_DESC texture_desc = {16, 16, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA texture_data = {texels.data(), 16 * 16, 0};
  ID3D11Texture2D* texture = nullptr;
  ID3D11ShaderResourceView* displacement = nullptr;
  device->CreateTexture2D(&texture_desc, &texture_data, &texture);
  device->CreateShaderResourceView(texture, nullptr, &displacement);
  const D3D11_SAMPLER_DESC sampler_desc = {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP,
                                           0.f, 1, D3D11_COMPARISON_NEVER, {0.f, 0.f, 0.f, 0.f}, 0.f, D3D11_FLOAT32_MAX};
  ID3D11SamplerState* sampler = nullptr;
  device->CreateSamplerState(&sampler_desc, &sampler);

  const std::array<ID3D11Buffer*, 3> vs_constants = game.config_at_b1 ? std::array<ID3D11Buffer*, 3>{far_camera, config, wind}
                                                                      : std::array<ID3D11Buffer*, 3>{far_camera, wind, config};
  const auto captured =
      StreamOut({.vs = &capture, .topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, .vs_constants = vs_constants, .stride = stride}, vertices, "TEXCOORD", 0, 4).values;
  // The game's TEXCOORD0 is its xyz.
  const auto world = StreamOut({.vs = &vs, .topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, .vs_constants = vs_constants}, vertices, "TEXCOORD", 0, 3).values;
  uint64_t t1_differ = 0;
  uint64_t t1_w = 0;
  const bool t1_sizes = captured.size() == PATCHES * 12 && world.size() == PATCHES * 9;
  for (size_t v = 0; t1_sizes && v < PATCHES * 3; ++v) {
    t1_differ += SameBits(&captured[v * 4], &world[v * 3], 3) ? 0 : 1;
    t1_w += captured[(v * 4) + 3] == 1.f ? 0 : 1;
  }
  const bool t1 = t1_sizes && t1_differ == 0 && t1_w == 0;
  std::printf("  T1 capture vs the game's world position: %zu / %zu vertices, %llu differ, %llu w not 1 (0, 0)\n", captured.size() / 4, world.size() / 3,
              (unsigned long long)t1_differ, (unsigned long long)t1_w);
  uint64_t t2_differ = 0;
  for (UINT row = 0; row < 3; ++row) {
    const auto rows_out = StreamOut({.vs = &rows, .vs_constants = vs_constants, .stride = stride}, vertices, "TEXCOORD", row, 4).values;
    for (size_t v = 0; v < PATCHES * 3; ++v) {
      t2_differ += (rows_out.size() == PATCHES * 12 && SameBits(&rows_out[v * 4], &vertices[((v * inputs) + 6 + row) * 4], 4)) ? 0 : 1;
    }
  }
  const bool t2 = t2_differ == 0;
  std::printf("  T2 rows vs POSITION1-3: %llu of %u differ (0)\n", (unsigned long long)t2_differ, PATCHES * 9);
  Draw chain = {.vs = &vs, .hs = &hs, .ds = &ds, .topology = D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, .vs_constants = vs_constants,
                .tessellation_constants = {far_camera, config}, .ds_view = displacement, .ds_sampler = sampler};
  // The domain shader's world position, w 1.
  const auto far_chain = StreamOut(chain, vertices, "TEXCOORD", game.chain_world, 4).values;
  // Corner order within a triangle is the tessellator's: compared as sets.
  const auto sorted = [](const float* triangle) {
    std::array<std::array<uint32_t, 4>, 3> corners = {};
    for (int c = 0; c < 3; ++c) std::memcpy(corners[c].data(), triangle + (c * 4), 16);
    std::sort(corners.begin(), corners.end());
    return corners;
  };
  uint64_t t3_differ = 0;
  for (UINT patch = 0; patch < PATCHES && far_chain.size() == captured.size(); ++patch) {
    t3_differ += sorted(&far_chain[patch * 12]) == sorted(&captured[patch * 12]) ? 0 : 1;
  }
  const bool t3 = !captured.empty() && far_chain.size() == captured.size() && t3_differ == 0;
  std::printf("  T3 the game's chain 17 km away: %zu triangles (%u), %llu differ from the capture's (0)\n", far_chain.size() / 12, PATCHES,
              (unsigned long long)t3_differ);
  chain.vs_constants[0] = near_camera;
  chain.tessellation_constants[0] = near_camera;
  const auto near_chain = StreamOut(chain, vertices, "TEXCOORD", game.chain_world, 4).values;
  const size_t per_patch = near_chain.size() / 12 / PATCHES;
  double largest = 0.0;
  uint64_t off_plane = 0;
  for (UINT patch = 0; t1 && patch < PATCHES && per_patch > 0 && near_chain.size() == per_patch * PATCHES * 12; ++patch) {
    const float* a = &captured[patch * 12];
    const std::array<double, 3> e1 = {double(a[4]) - a[0], double(a[5]) - a[1], double(a[6]) - a[2]};
    const std::array<double, 3> e2 = {double(a[8]) - a[0], double(a[9]) - a[1], double(a[10]) - a[2]};
    std::array<double, 3> n = {(e1[1] * e2[2]) - (e1[2] * e2[1]), (e1[2] * e2[0]) - (e1[0] * e2[2]), (e1[0] * e2[1]) - (e1[1] * e2[0])};
    const double length = std::sqrt((n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2]));
    for (double& c : n) c /= length;
    for (size_t v = patch * per_patch * 3; v < (patch + 1) * per_patch * 3; ++v) {
      const float* p = &near_chain[v * 4];
      const double distance = std::fabs(((p[0] - a[0]) * n[0]) + ((p[1] - a[1]) * n[1]) + ((p[2] - a[2]) * n[2]));
      largest = std::max(largest, distance);
      off_plane += distance > 1e-3 ? 1 : 0;
    }
  }
  const bool t4 = per_patch > 1 && near_chain.size() == per_patch * PATCHES * 12 && off_plane > 0 && largest <= 0.1 + 1e-4;
  std::printf("  T4 CONTROL the chain among the patches: %zu triangles a patch (> 1), %llu of %zu corners off the capture's plane (> 0), at most %.4f (<= 0.1)\n",
              per_patch, (unsigned long long)off_plane, near_chain.size() / 4, largest);
  const bool passed = t1 && t2 && t3 && t4;
  std::printf("  %s\n", passed ? "PASSED" : "FAILED");
  for (IUnknown* object : std::initializer_list<IUnknown*>{config, wind, far_camera, near_camera, texture, displacement, sampler}) {
    if (object != nullptr) object->Release();
  }
  return passed;
}

// G (see the top).
static bool CheckTerrain(const std::string& dump_root, const std::string& build32_root) {
  const std::string dump = dump_root + "\\dump_shader_twrome2_pack\\cso\\";
  const auto vs = Load(dump + "0xB1C7E29F.vs_5_0.cso");
  const auto hs = Load(dump + "0xBE9FC167.hs_5_0.cso");
  const auto ds = Load(dump + "0x93979D65.ds_5_0.cso");
  const auto capture = Load(build32_root + "\\twrome2.include\\embed\\rt_capture_terrain_old_sm41_a.cso");
  std::printf("G Rome II terrain: vs B1C7E29F, hs BE9FC167, ds 93979D65; rt_capture_terrain_old_sm41_a (%u patches)\n", PATCHES);
  if (vs.empty() || hs.empty() || ds.empty() || capture.empty()) {
    std::printf("  MISSING a shader's bytes\n");
    return false;
  }
  // POSITION0, NORMAL0, POSITION1-3 (the block's rows: x, height, z), POSITION4 (x the CPU's tessellation factor): each patch
  // its rows, its corners within the height map's unit square.
  constexpr UINT INPUTS = 6;
  const auto make_vertices = [](float factor, bool tilted) {
    std::vector<float> vertices;
    for (UINT patch = 0; patch < PATCHES; ++patch) {
      const std::array<std::array<float, 4>, 3> rows = {{{Range(0.5f, 1.f), 0.f, 0.f, Range(0.f, 0.2f)},
                                                         {0.f, Range(0.5f, 1.f), 0.f, Range(-1.f, 1.f)},
                                                         {0.f, 0.f, Range(0.5f, 1.f), Range(0.f, 0.2f)}}};
      for (int corner = 0; corner < 3; ++corner) {
        vertices.insert(vertices.end(), {Range(0.f, 0.6f), Range(-0.5f, 0.5f), Range(0.f, 0.6f), 1.f});
        // Flat (y 1: factor 1 whatever POSITION4) or tilted apart per corner (never within the hull shader's 0.999 of y).
        if (tilted) {
          vertices.insert(vertices.end(), {Range(0.2f, 0.6f), 1.f, -Range(0.2f, 0.6f), 0.f});
        } else {
          vertices.insert(vertices.end(), {0.f, 1.f, 0.f, 0.f});
        }
        for (const auto& row : rows) vertices.insert(vertices.end(), row.begin(), row.end());
        vertices.insert(vertices.end(), {factor, 0.f, 0.f, 0.f});
      }
    }
    return vertices;
  };
  const auto flat = make_vertices(1.f, false);
  const auto tilted = make_vertices(4.f, true);
  // lf_block_cb: the height map 16 x 16 (texel 1 / 16), the world's xz to its uv as is; vs_height_map_cb: heights x 10, the
  // detail x 1, offset 0.5.
  const std::array<float, 4 * 4> block = {16.f, 16.f, 1.f / 16.f, 1.f / 16.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f};
  const std::array<float, 2 * 4> heights = {10.f, 1.f, 0.5f, 0.f, 0.f, 1.f, 0.f, 0.f};
  ID3D11Buffer* block_constants = MakeConstants(block);
  ID3D11Buffer* height_constants = MakeConstants(heights);
  // 30 m away (past the vertex shader's 20 m: its 0.09 m lift by the height's sign in full).
  ID3D11Buffer* camera = MakeCamera({30.f, 0.f, 30.f});
  std::array<float, 16 * 16 * 4> texels = {};
  for (float& texel : texels) texel = Range(0.f, 1.f);
  const D3D11_TEXTURE2D_DESC texture_desc = {16, 16, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA texture_data = {texels.data(), 16 * 16, 0};
  ID3D11Texture2D* texture = nullptr;
  ID3D11ShaderResourceView* height_map = nullptr;
  device->CreateTexture2D(&texture_desc, &texture_data, &texture);
  device->CreateShaderResourceView(texture, nullptr, &height_map);
  const D3D11_SAMPLER_DESC sampler_desc = {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP,
                                           0.f, 1, D3D11_COMPARISON_NEVER, {0.f, 0.f, 0.f, 0.f}, 0.f, D3D11_FLOAT32_MAX};
  ID3D11SamplerState* sampler = nullptr;
  device->CreateSamplerState(&sampler_desc, &sampler);
  const std::array<ID3D11Buffer*, 3> vs_constants = {camera, block_constants, height_constants};
  Draw draw = {.vs = &capture, .topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, .vs_constants = vs_constants, .vs_view = height_map,
               .vs_sampler = sampler};
  const auto captured = StreamOut(draw, flat, "TEXCOORD", 0, 4).values;
  draw.vs = &vs;
  const auto world = StreamOut(draw, flat, "TEXCOORD", 1, 3).values;
  uint64_t g1_differ = 0;
  uint64_t g1_w = 0;
  const bool g1_sizes = captured.size() == PATCHES * 12 && world.size() == PATCHES * 9;
  for (size_t v = 0; g1_sizes && v < PATCHES * 3; ++v) {
    g1_differ += SameBits(&captured[v * 4], &world[v * 3], 3) ? 0 : 1;
    g1_w += captured[(v * 4) + 3] == 1.f ? 0 : 1;
  }
  const bool g1 = g1_sizes && g1_differ == 0 && g1_w == 0;
  std::printf("  G1 capture vs the game's world position: %zu / %zu vertices, %llu differ, %llu w not 1 (0, 0)\n", captured.size() / 4, world.size() / 3,
              (unsigned long long)g1_differ, (unsigned long long)g1_w);
  draw.hs = &hs;
  draw.ds = &ds;
  draw.topology = D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
  draw.tessellation_constants = {camera, nullptr};
  // The domain shader's TEXCOORD1: the world position.
  const auto chain = StreamOut(draw, flat, "TEXCOORD", 1, 3).values;
  // Corner order within a triangle is the tessellator's: each capture corner's nearest chain corner.
  uint64_t g2_differ = 0;
  double g2_largest = 0.0;
  for (UINT patch = 0; patch < PATCHES && g1 && chain.size() == PATCHES * 9; ++patch) {
    for (int corner = 0; corner < 3; ++corner) {
      const float* c = &captured[((patch * 3) + corner) * 4];
      double nearest = 1e30;
      for (int other = 0; other < 3; ++other) {
        const float* p = &chain[((patch * 3) + other) * 3];
        double largest = 0.0;
        for (int axis = 0; axis < 3; ++axis) largest = std::max(largest, std::fabs(double(p[axis]) - c[axis]) / std::max(1.0, std::fabs(double(c[axis]))));
        nearest = std::min(nearest, largest);
      }
      g2_largest = std::max(g2_largest, nearest);
      g2_differ += nearest <= 1e-6 ? 0 : 1;
    }
  }
  const bool g2 = g1 && chain.size() == PATCHES * 9 && g2_differ == 0;
  std::printf("  G2 the game's chain at factor 1: %zu triangles (%u), %llu corners off the capture's, largest %.2e (<= 1e-6)\n", chain.size() / 9, PATCHES,
              (unsigned long long)g2_differ, g2_largest);
  const auto tilted_captured = StreamOut({.vs = &capture, .topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, .vs_constants = vs_constants, .vs_view = height_map,
                                          .vs_sampler = sampler},
                                         tilted, "TEXCOORD", 0, 4)
                                   .values;
  const auto tilted_chain = StreamOut(draw, tilted, "TEXCOORD", 1, 3).values;
  const size_t per_patch = tilted_chain.size() / 9 / PATCHES;
  uint64_t off_plane = 0;
  for (UINT patch = 0; patch < PATCHES && per_patch > 1 && tilted_captured.size() == PATCHES * 12 && tilted_chain.size() == per_patch * PATCHES * 9; ++patch) {
    const float* a = &tilted_captured[patch * 12];
    const std::array<double, 3> e1 = {double(a[4]) - a[0], double(a[5]) - a[1], double(a[6]) - a[2]};
    const std::array<double, 3> e2 = {double(a[8]) - a[0], double(a[9]) - a[1], double(a[10]) - a[2]};
    std::array<double, 3> n = {(e1[1] * e2[2]) - (e1[2] * e2[1]), (e1[2] * e2[0]) - (e1[0] * e2[2]), (e1[0] * e2[1]) - (e1[1] * e2[0])};
    const double length = std::sqrt((n[0] * n[0]) + (n[1] * n[1]) + (n[2] * n[2]));
    for (double& c : n) c /= length;
    for (size_t v = patch * per_patch * 3; v < (patch + 1) * per_patch * 3; ++v) {
      const float* p = &tilted_chain[v * 3];
      off_plane += std::fabs(((p[0] - a[0]) * n[0]) + ((p[1] - a[1]) * n[1]) + ((p[2] - a[2]) * n[2])) > 1e-3 ? 1 : 0;
    }
  }
  const bool g3 = per_patch > 1 && off_plane > 0;
  std::printf("  G3 CONTROL factor 4, normals apart: %zu triangles a patch (> 1), %llu corners off the capture's plane (> 0)\n", per_patch,
              (unsigned long long)off_plane);
  const bool passed = g1 && g2 && g3;
  std::printf("  %s\n", passed ? "PASSED" : "FAILED");
  for (IUnknown* object : std::initializer_list<IUnknown*>{block_constants, height_constants, camera, texture, height_map, sampler}) {
    if (object != nullptr) object->Release();
  }
  return passed;
}

// B (see the top) for one unit family.
struct BatchedFamily {
  const char* name;
  const char* game_vs;  // the dump's (its input layout)
  const char* capture;  // the add-on's built shaders
  const char* compute;
};

static bool CheckBatchedUnits(const BatchedFamily& family, const std::string& dump_root, const std::string& build32_root) {
  using Microsoft::WRL::ComPtr;
  const auto game_vs = Load(dump_root + "\\dump_shader_twattila_pack\\cso\\" + family.game_vs + ".cso");
  const std::string built = build32_root + "\\twattila.include\\embed\\";
  const auto capture = Load(built + family.capture + ".cso");
  const auto mesh = Load(built + "rt_batch_unit_mesh.cso");
  const auto rows = Load(built + "rt_batch_unit_rows.cso");
  const auto compute = Load(built + family.compute + ".cso");
  std::printf("B %s: vs %s; %s, rt_batch_unit_mesh, rt_batch_unit_rows, %s\n", family.name, family.game_vs, family.capture, family.compute);
  if (game_vs.empty() || capture.empty() || mesh.empty() || rows.empty() || compute.empty()) {
    std::printf("  MISSING a shader's bytes\n");
    return false;
  }
  const std::array<D3D11_INPUT_ELEMENT_DESC, 14> layout = {{
      {"POSITION", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"POSITION", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"POSITION", 2, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"POSITION", 3, DXGI_FORMAT_R16G16_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"POSITION", 4, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"POSITION", 5, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"TEXCOORD", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1},
      {"TEXCOORD", 7, DXGI_FORMAT_R32G32_FLOAT, 1, 112, D3D11_INPUT_PER_INSTANCE_DATA, 1},
  }};
  ComPtr<ID3D11InputLayout> input_layout;
  if (FAILED(device->CreateInputLayout(layout.data(), UINT(layout.size()), game_vs.data(), game_vs.size(), input_layout.GetAddressOf()))) {
    std::printf("  no input layout for the game's shader\n");
    return false;
  }
  // The draws: vertices, instances, base vertex, first instance, place in `captured`, animation texture.
  struct DrawSpec {
    UINT vertices;
    UINT instances;
    UINT base_vertex;
    UINT first_instance;
    UINT captured_first;
    UINT texture;
  };
  constexpr std::array<DrawSpec, 3> DRAWS = {{{150, 33, 0, 0, 0, 0},
                                              {97, 1, 200, 40, (150 * 33) + 100, 0},
                                              {211, 70, 400, 50, (150 * 33) + 100 + 97, 1}}};
  constexpr UINT TEXELS = (150 * 33) + 100 + 97 + (211 * 70);
  constexpr UINT VERTICES = 700;
  constexpr UINT INSTANCES = 128;
  constexpr UINT ANIMATION_WIDTH = 512;
  constexpr UINT ANIMATION_HEIGHT = 32;
  std::vector<uint8_t> vertex_data(size_t{VERTICES} * 28);
  for (UINT v = 0; v < VERTICES; ++v) {
    uint8_t* vertex = &vertex_data[size_t{v} * 28];
    const std::array<uint16_t, 4> position = {uint16_t(HalfBits(Range(-1.f, 1.f))), uint16_t(HalfBits(Range(-0.2f, 1.9f))),
                                              uint16_t(HalfBits(Range(-1.f, 1.f))), uint16_t(HalfBits(1.f))};
    std::memcpy(vertex, position.data(), 8);
    for (size_t byte = 8; byte < 28; ++byte) vertex[byte] = uint8_t(rng() & 0xFFu);
    const std::array<uint16_t, 2> uv = {uint16_t(HalfBits(Range(0.f, 1.f))), uint16_t(HalfBits(Range(0.f, 1.f)))};
    std::memcpy(vertex + 16, uv.data(), 4);
  }
  std::vector<float> instance_data(size_t{INSTANCES} * 30);
  for (UINT i = 0; i < INSTANCES; ++i) {
    float* instance = &instance_data[size_t{i} * 30];
    for (int value = 0; value < 30; ++value) instance[value] = Range(-2.f, 2.f);
    for (int row = 0; row < 3; ++row) instance[(row * 4) + 3] = Range(-500.f, 500.f);
    // TEXCOORD3.x: the instance's first bone texel (bones 0-255 a vertex, 3 texels each: within the texture).
    instance[12] = float((rng() % 2400u) * 3u);
  }
  const auto make_buffer = [](const void* data, UINT bytes, UINT bind, UINT misc, ComPtr<ID3D11Buffer>* made) {
    const D3D11_BUFFER_DESC desc = {bytes, D3D11_USAGE_DEFAULT, bind, 0, misc, 0};
    const D3D11_SUBRESOURCE_DATA init = {data, 0, 0};
    return SUCCEEDED(device->CreateBuffer(&desc, (data != nullptr ? &init : nullptr), made->GetAddressOf()));
  };
  const auto make_animation = [](ComPtr<ID3D11Texture2D>* texture, ComPtr<ID3D11ShaderResourceView>* view) {
    std::vector<float> texels(size_t{ANIMATION_WIDTH} * ANIMATION_HEIGHT * 4);
    for (float& texel : texels) texel = Range(-1.f, 1.f);
    const D3D11_TEXTURE2D_DESC desc = {ANIMATION_WIDTH, ANIMATION_HEIGHT, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                       D3D11_BIND_SHADER_RESOURCE, 0, 0};
    const D3D11_SUBRESOURCE_DATA init = {texels.data(), ANIMATION_WIDTH * 16, 0};
    return SUCCEEDED(device->CreateTexture2D(&desc, &init, texture->GetAddressOf()))
           && SUCCEEDED(device->CreateShaderResourceView(texture->Get(), nullptr, view->GetAddressOf()));
  };
  ComPtr<ID3D11Buffer> vertex_buffer, instance_buffer, camera, config, animation_size, narrow_size;
  std::array<ComPtr<ID3D11Texture2D>, 2> animation_textures;
  std::array<ComPtr<ID3D11ShaderResourceView>, 2> animation_maps;
  std::array<ComPtr<ID3D11Buffer>, 3> remaps;
  const std::array<float, 36 * 4> camera_values = {};
  const std::array<float, 5 * 4> config_values = {};
  const std::array<float, 4> size_values = {float(ANIMATION_HEIGHT), 0.f, float(ANIMATION_WIDTH), 0.f};
  const std::array<float, 4> narrow_values = {float(ANIMATION_HEIGHT), 0.f, float(ANIMATION_WIDTH / 2), 0.f};
  bool made = make_buffer(vertex_data.data(), UINT(vertex_data.size()), D3D11_BIND_VERTEX_BUFFER, 0, &vertex_buffer)
              && make_buffer(instance_data.data(), UINT(instance_data.size() * 4), D3D11_BIND_VERTEX_BUFFER, 0, &instance_buffer)
              && make_buffer(camera_values.data(), sizeof(camera_values), D3D11_BIND_CONSTANT_BUFFER, 0, &camera)
              && make_buffer(config_values.data(), sizeof(config_values), D3D11_BIND_CONSTANT_BUFFER, 0, &config)
              && make_buffer(size_values.data(), sizeof(size_values), D3D11_BIND_CONSTANT_BUFFER, 0, &animation_size)
              && make_buffer(narrow_values.data(), sizeof(narrow_values), D3D11_BIND_CONSTANT_BUFFER, 0, &narrow_size)
              && make_animation(&animation_textures[0], &animation_maps[0]) && make_animation(&animation_textures[1], &animation_maps[1]);
  for (size_t d = 0; d < DRAWS.size(); ++d) {
    std::vector<uint32_t> remap(250);
    for (uint32_t index = 0; index < remap.size(); ++index) remap[index] = index;
    std::shuffle(remap.begin(), remap.end(), rng);
    made = made && make_buffer(remap.data(), DRAWS[d].vertices * 4, D3D11_BIND_INDEX_BUFFER, 0, &remaps[d]);
  }
  ComPtr<ID3D11VertexShader> capture_vs, mesh_vs, rows_vs;
  ComPtr<ID3D11GeometryShader> capture_gs, mesh_gs, rows_gs;
  ComPtr<ID3D11ComputeShader> batch_cs;
  const D3D11_SO_DECLARATION_ENTRY world_entry = {0, "TEXCOORD", 0, 0, 4, 0};
  const std::array<D3D11_SO_DECLARATION_ENTRY, 2> mesh_entries = {{{0, "TEXCOORD", 0, 0, 4, 0}, {0, "TEXCOORD", 1, 0, 4, 0}}};
  const std::array<D3D11_SO_DECLARATION_ENTRY, 4> rows_entries = {
      {{0, "TEXCOORD", 0, 0, 4, 0}, {0, "TEXCOORD", 1, 0, 4, 0}, {0, "TEXCOORD", 2, 0, 4, 0}, {0, "TEXCOORD", 3, 0, 4, 0}}};
  constexpr UINT WORLD_STRIDE = 16;
  constexpr UINT MESH_STRIDE = 32;
  constexpr UINT ROWS_STRIDE = 64;
  made = made && SUCCEEDED(device->CreateVertexShader(capture.data(), capture.size(), nullptr, capture_vs.GetAddressOf()))
         && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(capture.data(), capture.size(), &world_entry, 1, &WORLD_STRIDE, 1,
                                                                   D3D11_SO_NO_RASTERIZED_STREAM, nullptr, capture_gs.GetAddressOf()))
         && SUCCEEDED(device->CreateVertexShader(mesh.data(), mesh.size(), nullptr, mesh_vs.GetAddressOf()))
         && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(mesh.data(), mesh.size(), mesh_entries.data(), 2, &MESH_STRIDE, 1,
                                                                   D3D11_SO_NO_RASTERIZED_STREAM, nullptr, mesh_gs.GetAddressOf()))
         && SUCCEEDED(device->CreateVertexShader(rows.data(), rows.size(), nullptr, rows_vs.GetAddressOf()))
         && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(rows.data(), rows.size(), rows_entries.data(), 4, &ROWS_STRIDE, 1,
                                                                   D3D11_SO_NO_RASTERIZED_STREAM, nullptr, rows_gs.GetAddressOf()))
         && SUCCEEDED(device->CreateComputeShader(compute.data(), compute.size(), nullptr, batch_cs.GetAddressOf()));
  if (!made) {
    std::printf("  could not make the draws' objects\n");
    return false;
  }
  const auto read_back = [](ID3D11Buffer* buffer, UINT bytes) {
    std::vector<uint32_t> values(bytes / 4);
    const D3D11_BUFFER_DESC desc = {bytes, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
    ComPtr<ID3D11Buffer> staging;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const D3D11_BOX whole = {0, 0, 0, bytes, 1, 1};
    if (SUCCEEDED(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf()))) {
      context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, buffer, 0, &whole);
      if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        std::memcpy(values.data(), mapped.pData, bytes);
        context->Unmap(staging.Get(), 0);
      }
    }
    return values;
  };
  // The game's state at a unit draw, then the add-on's draw through the remap indices (rt_core.hpp Capture).
  const auto game_state = [&](size_t d, ID3D11Buffer* size) {
    const std::array<ID3D11Buffer*, 2> buffers = {vertex_buffer.Get(), instance_buffer.Get()};
    constexpr std::array<UINT, 2> STRIDES = {28, 120};
    constexpr std::array<UINT, 2> OFFSETS = {0, 0};
    context->IASetVertexBuffers(0, 2, buffers.data(), STRIDES.data(), OFFSETS.data());
    context->IASetInputLayout(input_layout.Get());
    context->IASetIndexBuffer(remaps[d].Get(), DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    const std::array<ID3D11Buffer*, 3> constants = {camera.Get(), config.Get(), size};
    context->VSSetConstantBuffers(0, 3, constants.data());
    context->VSSetShaderResources(0, 1, animation_maps[DRAWS[d].texture].GetAddressOf());
    context->PSSetShader(nullptr, nullptr, 0);
  };
  ComPtr<ID3D11Buffer> streamed;
  made = make_buffer(nullptr, TEXELS * 16, D3D11_BIND_STREAM_OUTPUT, 0, &streamed);
  for (size_t d = 0; made && d < DRAWS.size(); ++d) {
    game_state(d, animation_size.Get());
    const UINT offset = DRAWS[d].captured_first * 16;
    context->VSSetShader(capture_vs.Get(), nullptr, 0);
    context->GSSetShader(capture_gs.Get(), nullptr, 0);
    context->SOSetTargets(1, streamed.GetAddressOf(), &offset);
    context->DrawIndexedInstanced(DRAWS[d].vertices, DRAWS[d].instances, 0, int(DRAWS[d].base_vertex), DRAWS[d].first_instance);
  }
  context->SOSetTargets(0, nullptr, nullptr);
  const std::vector<uint32_t> per_draw = read_back(streamed.Get(), TEXELS * 16);
  // The batched path: the meshes and rows streamed at the draws, then FlushBatch's table, runs (by animation texture and width),
  // dispatches and copies.
  const auto batched = [&](bool stale_second, bool narrow_third, bool written_first) {
    ComPtr<ID3D11Buffer> meshes, rows_buffer, table_buffer, output, run, width, captured;
    ComPtr<ID3D11ShaderResourceView> meshes_view, rows_view, table_view;
    ComPtr<ID3D11UnorderedAccessView> output_view;
    std::vector<uint32_t> none;
    const auto view = [](ID3D11Buffer* buffer, DXGI_FORMAT format, UINT elements, ComPtr<ID3D11ShaderResourceView>* made_view) {
      D3D11_SHADER_RESOURCE_VIEW_DESC desc = {format, D3D11_SRV_DIMENSION_BUFFER};
      desc.Buffer.NumElements = elements;
      return SUCCEEDED(device->CreateShaderResourceView(buffer, &desc, made_view->GetAddressOf()));
    };
    D3D11_UNORDERED_ACCESS_VIEW_DESC output_desc = {DXGI_FORMAT_R32_TYPELESS, D3D11_UAV_DIMENSION_BUFFER};
    output_desc.Buffer = {0, TEXELS * 4, D3D11_BUFFER_UAV_FLAG_RAW};
    if (!make_buffer(nullptr, VERTICES * 32, D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_SHADER_RESOURCE, 0, &meshes)
        || !view(meshes.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, VERTICES * 2, &meshes_view)
        || !make_buffer(nullptr, INSTANCES * 64, D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_SHADER_RESOURCE, 0, &rows_buffer)
        || !view(rows_buffer.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, INSTANCES * 4, &rows_view)
        || !make_buffer(nullptr, UINT(DRAWS.size()) * 32, D3D11_BIND_SHADER_RESOURCE, 0, &table_buffer)
        || !view(table_buffer.Get(), DXGI_FORMAT_R32G32B32A32_UINT, UINT(DRAWS.size()) * 2, &table_view)
        || !make_buffer(nullptr, TEXELS * 16, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, &output)
        || FAILED(device->CreateUnorderedAccessView(output.Get(), &output_desc, output_view.GetAddressOf()))
        || !make_buffer(nullptr, 16, D3D11_BIND_CONSTANT_BUFFER, 0, &run) || !make_buffer(nullptr, 16, D3D11_BIND_CONSTANT_BUFFER, 0, &width)
        || !make_buffer(nullptr, TEXELS * 16, D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_SHADER_RESOURCE, 0, &captured)) {
      return none;
    }
    UINT mesh_texels = 0;
    UINT instances = 0;
    std::vector<std::array<uint32_t, 8>> table;
    for (size_t d = 0; d < DRAWS.size(); ++d) {
      game_state(d, (narrow_third && d == 2) ? narrow_size.Get() : animation_size.Get());
      const UINT mesh_offset = mesh_texels * 16;
      const UINT rows_offset = instances * 64;
      context->VSSetShader(mesh_vs.Get(), nullptr, 0);
      context->GSSetShader(mesh_gs.Get(), nullptr, 0);
      context->SOSetTargets(1, meshes.GetAddressOf(), &mesh_offset);
      context->DrawIndexedInstanced(DRAWS[d].vertices, 1, 0, int(DRAWS[d].base_vertex), DRAWS[d].first_instance);
      context->VSSetShader(rows_vs.Get(), nullptr, 0);
      context->GSSetShader(rows_gs.Get(), nullptr, 0);
      context->SOSetTargets(1, rows_buffer.GetAddressOf(), &rows_offset);
      context->DrawIndexedInstanced(1, DRAWS[d].instances, 0, int(DRAWS[d].base_vertex), DRAWS[d].first_instance);
      table.push_back({mesh_texels, DRAWS[d].vertices, instances, DRAWS[d].instances, 0, 0, (stale_second && d == 1) ? 1u : 0u, 0});
      mesh_texels += DRAWS[d].vertices * 2;
      instances += DRAWS[d].instances;
    }
    context->SOSetTargets(0, nullptr, nullptr);
    context->GSSetShader(nullptr, nullptr, 0);
    if (written_first) {
      std::vector<float> texels(size_t{ANIMATION_WIDTH} * ANIMATION_HEIGHT * 4);
      for (float& texel : texels) texel = Range(-1.f, 1.f);
      context->UpdateSubresource(animation_textures[0].Get(), 0, nullptr, texels.data(), ANIMATION_WIDTH * 16, 0);
    }
    // FlushBatch: outputs in order, groups from each run's start; a run per animation texture (all one width here).
    std::vector<std::array<uint32_t, 3>> runs;
    uint32_t texels = 0;
    uint32_t run_first = 0;
    uint32_t run_groups = 0;
    for (size_t d = 0; d < DRAWS.size(); ++d) {
      if (d != 0 && DRAWS[d].texture != DRAWS[d - 1].texture) {
        runs.push_back({run_first, uint32_t(d) - run_first, run_groups});
        run_first = uint32_t(d);
        run_groups = 0;
      }
      table[d][4] = texels;
      table[d][5] = run_groups;
      run_groups += ((DRAWS[d].vertices * DRAWS[d].instances) + 63) / 64;
      texels += DRAWS[d].vertices * DRAWS[d].instances;
    }
    runs.push_back({run_first, uint32_t(DRAWS.size()) - run_first, run_groups});
    context->UpdateSubresource(table_buffer.Get(), 0, nullptr, table.data(), 0, 0);
    const std::array<ID3D11ShaderResourceView*, 3> views = {meshes_view.Get(), rows_view.Get(), table_view.Get()};
    const std::array<ID3D11Buffer*, 2> constants = {width.Get(), run.Get()};
    context->CSSetShader(batch_cs.Get(), nullptr, 0);
    context->CSSetShaderResources(2, 3, views.data());
    context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
    context->CSSetConstantBuffers(2, 2, constants.data());
    for (const auto& [first, count, groups] : runs) {
      const float animation_width = float(ANIMATION_WIDTH);
      const std::array<float, 4> width_values = {0.f, 0.f, animation_width, 0.f};
      const std::array<uint32_t, 4> run_values = {first, count, groups, std::bit_cast<uint32_t>(animation_width)};
      context->UpdateSubresource(width.Get(), 0, nullptr, width_values.data(), 0, 0);
      context->UpdateSubresource(run.Get(), 0, nullptr, run_values.data(), 0, 0);
      context->CSSetShaderResources(0, 1, animation_maps[DRAWS[first].texture].GetAddressOf());
      context->Dispatch(std::min(groups, 65535u), (groups + 65534) / 65535, 1);
    }
    ID3D11UnorderedAccessView* no_view = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &no_view, nullptr);
    for (size_t copied = 0; copied < DRAWS.size();) {
      size_t next = copied + 1;
      while (next < DRAWS.size() && DRAWS[next].captured_first == DRAWS[next - 1].captured_first + (DRAWS[next - 1].vertices * DRAWS[next - 1].instances)) ++next;
      const D3D11_BOX stretch = {table[copied][4] * 16, 0, 0, (table[next - 1][4] + (DRAWS[next - 1].vertices * DRAWS[next - 1].instances)) * 16, 1, 1};
      context->CopySubresourceRegion(captured.Get(), 0, DRAWS[copied].captured_first * 16, 0, 0, output.Get(), 0, &stretch);
      copied = next;
    }
    return read_back(captured.Get(), TEXELS * 16);
  };
  // Per draw: corners differing from the per-draw stream output's, and NaN (a quiet NaN's bits in all four).
  const auto compare = [&](const std::vector<uint32_t>& values, size_t d, uint64_t* differ, uint64_t* nan) {
    *differ = 0;
    *nan = 0;
    for (UINT corner = 0; corner < DRAWS[d].vertices * DRAWS[d].instances; ++corner) {
      const size_t at = size_t{DRAWS[d].captured_first + corner} * 4;
      *differ += (values.size() == per_draw.size() && std::memcmp(&values[at], &per_draw[at], 16) == 0) ? 0 : 1;
      *nan += (values.size() == per_draw.size() && values[at] == 0x7FC00000u && values[at + 1] == 0x7FC00000u && values[at + 2] == 0x7FC00000u
               && values[at + 3] == 0x7FC00000u)
                  ? 1
                  : 0;
    }
  };
  uint64_t w_not_1 = 0;
  for (const DrawSpec& draw : DRAWS) {
    for (UINT corner = 0; corner < draw.vertices * draw.instances; ++corner) {
      w_not_1 += (std::bit_cast<float>(per_draw[(size_t{draw.captured_first + corner} * 4) + 3]) == 1.f) ? 0 : 1;
    }
  }
  const auto whole = batched(false, false, false);
  std::array<std::array<uint64_t, 2>, 3> b1 = {};
  for (size_t d = 0; d < DRAWS.size(); ++d) compare(whole, d, &b1[d][0], &b1[d][1]);
  const bool b1_ok = !whole.empty() && w_not_1 == 0 && b1[0][0] == 0 && b1[1][0] == 0 && b1[2][0] == 0;
  std::printf("  B1 batched vs per-draw: %llu / %llu / %llu of %u / %u / %u corners differ, w not 1 %llu (0)\n", (unsigned long long)b1[0][0],
              (unsigned long long)b1[1][0], (unsigned long long)b1[2][0], DRAWS[0].vertices * DRAWS[0].instances, DRAWS[1].vertices * DRAWS[1].instances,
              DRAWS[2].vertices * DRAWS[2].instances, (unsigned long long)w_not_1);
  const auto left_out = batched(true, true, false);
  std::array<std::array<uint64_t, 2>, 3> b2 = {};
  for (size_t d = 0; d < DRAWS.size(); ++d) compare(left_out, d, &b2[d][0], &b2[d][1]);
  const bool b2_ok = !left_out.empty() && b2[0][0] == 0 && b2[1][1] == DRAWS[1].vertices * DRAWS[1].instances
                     && b2[2][1] == DRAWS[2].vertices * DRAWS[2].instances;
  std::printf("  B2 stale second, third at width 256: first %llu differ (0), second %llu NaN (%u), third %llu NaN (%u)\n", (unsigned long long)b2[0][0],
              (unsigned long long)b2[1][1], DRAWS[1].vertices * DRAWS[1].instances, (unsigned long long)b2[2][1], DRAWS[2].vertices * DRAWS[2].instances);
  const auto written = batched(false, false, true);
  std::array<uint64_t, 2> b3 = {};
  compare(written, 0, &b3[0], &b3[1]);
  const bool b3_ok = !written.empty() && b3[0] > 0;
  std::printf("  B3 CONTROL the first draw's bones written before the flush: %llu of %u corners differ (> 0)\n", (unsigned long long)b3[0],
              DRAWS[0].vertices * DRAWS[0].instances);
  const bool passed = b1_ok && b2_ok && b3_ok;
  std::printf("  %s\n", passed ? "PASSED" : "FAILED");
  return passed;
}

int main(int argc, char** argv) {
  if (argc < 4) {
    std::printf("tests.exe <dump root> <build root> <build32 root>\n");
    return 2;
  }
  const std::string root = argv[1];
  const std::string build_root = argv[2];
  const std::string build32_root = argv[3];
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) {
    std::printf("no WARP device\n");
    return 2;
  }
  struct Pair {
    const char* game;
    const char* pack;
    const char* vs;
    const char* gs;
  };
  const std::array<Pair, 5> pairs = {{
      {"Warhammer sm4.0", "twwarhammer", "0x381E079E.vs_4_0", "0x12D3A4BC.gs_4_0"},
      {"Warhammer sm4.1", "twwarhammer", "0x883D12E7.vs_4_1", "0xD6954DFC.gs_4_1"},
      {"Troy", "twtroy", "0x370A7440.vs_5_0", "0xDC05F551.gs_5_0"},
      {"Warhammer II", "twwarhammer2", "0x370A7440.vs_5_0", "0x29E8BDF7.gs_5_0"},
      {"Pharaoh", "twpharaohdynasties", "0x370A7440.vs_5_0", "0x27A16995.gs_5_0"},
  }};
  // The soldiers: POSITION0 (place, w the atlas column's offset), POSITION1 (the cell's corner, w < 0: flat on the ground),
  // POSITION2 (x height, z half width), TEXCOORD0 (y across / z back for a flat one, w lift), TEXCOORD1 (passed through),
  // TEXCOORD2 (passed through), TEXCOORD3 (the cell's size, some wrapping the atlas's row).
  std::vector<float> soldiers;
  for (UINT soldier = 0; soldier < SOLDIERS; ++soldier) {
    const bool flat = (soldier % 2) == 1;
    const std::array<float, 28> inputs = {
        Range(-50.f, 50.f), Range(-5.f, 5.f), Range(-50.f, 50.f), Range(0.f, 7.f),
        Range(0.f, 1.f), Range(0.f, 1.f), Range(-1.f, 1.f), flat ? -Range(0.f, 1.f) : Range(0.f, 0.2f),
        Range(1.f, 2.5f), Range(-1.f, 1.f), Range(0.2f, 1.f), Range(-1.f, 1.f),
        Range(-1.f, 1.f), Range(0.1f, 1.f), Range(0.1f, 1.f), Range(-0.2f, 0.2f),
        Range(-1.f, 1.f), Range(-1.f, 1.f), Range(-1.f, 1.f), Range(-1.f, 1.f),
        Range(-1.f, 1.f), Range(-1.f, 1.f), 0.f, 0.f,
        Range(0.05f, 0.2f), Range(0.05f, 0.2f), 0.f, 0.f,
    };
    soldiers.insert(soldiers.end(), inputs.begin(), inputs.end());
  }
  const std::array<float, 3> toward_sun = {0.6f, 0.f, 0.8f};
  ID3D11Buffer* sun_camera = MakeCamera({toward_sun[0] * 1.0e6f, 0.f, toward_sun[2] * 1.0e6f});
  ID3D11Buffer* turned_camera = MakeCamera({-toward_sun[2] * 1.0e6f, 0.f, toward_sun[0] * 1.0e6f});
  // rt_sun: texel 0 the way to the sun.
  const std::array<float, 4> sun_texel = {toward_sun[0], toward_sun[1], toward_sun[2], 0.f};
  const D3D11_TEXTURE2D_DESC sun_desc = {1, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA sun_data = {sun_texel.data(), 16, 16};
  ID3D11Texture2D* sun_texture = nullptr;
  ID3D11ShaderResourceView* rt_sun = nullptr;
  device->CreateTexture2D(&sun_desc, &sun_data, &sun_texture);
  device->CreateShaderResourceView(sun_texture, nullptr, &rt_sun);
  const std::span<const uint8_t> capture_gs(__rt_capture_imposter_gs, sizeof(__rt_capture_imposter_gs));

  bool all = true;
  for (const Pair& pair : pairs) {
    const std::string folder = root + "\\dump_shader_" + pair.pack + "_pack\\cso\\";
    const auto vs = Load(folder + pair.vs + ".cso");
    const auto gs = Load(folder + pair.gs + ".cso");
    std::printf("%s: vs %s + gs %s\n", pair.game, pair.vs, pair.gs);
    if (vs.empty() || gs.empty()) {
      std::printf("  MISSING the game's bytes\n");
      all = false;
      continue;
    }
    const Draw game = {.vs = &vs, .gs = gs, .vs_constants = {sun_camera}};
    const Streamed world = StreamOut(game, soldiers, "TEXCOORD", 2, 4);
    const Streamed uv = StreamOut(game, soldiers, "TEXCOORD", 1, 2);
    const Streamed turned = StreamOut({.vs = &vs, .gs = gs, .vs_constants = {turned_camera}}, soldiers, "TEXCOORD", 2, 4);
    const Streamed capture = StreamOut({.vs = &vs, .gs = capture_gs, .vs_constants = {sun_camera}, .gs_view = rt_sun}, soldiers, "TEXCOORD", 0, 4);
    const size_t corners = capture.values.size() / 4;
    const bool ok = corners == SOLDIERS * 6 && world.values.size() == corners * 4 && uv.values.size() == corners * 2 && turned.values.size() == corners * 4;
    double largest = 0.0;
    double largest_turned = 0.0;
    uint64_t uv_off = 0;
    for (size_t corner = 0; ok && corner < corners; ++corner) {
      for (size_t axis = 0; axis < 3; ++axis) {
        largest = std::max(largest, std::abs(double(world.values[(corner * 4) + axis]) - capture.values[(corner * 4) + axis]));
        largest_turned = std::max(largest_turned, std::abs(double(turned.values[(corner * 4) + axis]) - capture.values[(corner * 4) + axis]));
      }
      const uint32_t packed = std::bit_cast<uint32_t>(capture.values[(corner * 4) + 3]);
      for (size_t axis = 0; axis < 2; ++axis) {
        const float game_uv = uv.values[(corner * 2) + axis];
        const float captured = HalfValue((packed >> (axis * 16)) & 0xFFFFu);
        // The capture's half, or the next one down / up (its own float of the UV may round a step apart before the cut).
        const float step = std::abs(HalfValue(HalfBits(game_uv) + 1u) - HalfValue(HalfBits(game_uv)));
        if (std::abs(game_uv - captured) > step * 1.01f) ++uv_off;
      }
    }
    const bool a = ok && largest <= 2e-4;
    const bool u = ok && uv_off == 0;
    const bool n = capture.primitives == SOLDIERS * 2;
    const bool c = ok && largest_turned > 0.1;
    std::printf("  A corners %zu (%u), largest difference %.2e m (<= 2e-4)\n", corners, SOLDIERS * 6, largest);
    std::printf("  U UVs off by more than a half's step: %llu (0)\n", static_cast<unsigned long long>(uv_off));
    std::printf("  N the capture's triangles %llu (%u: 2 a soldier)\n", static_cast<unsigned long long>(capture.primitives), SOLDIERS * 2);
    std::printf("  C CONTROL the camera turned 90 degrees: largest difference %.2f m (> 0.1)\n", largest_turned);
    const bool passed = a && u && n && c;
    std::printf("  %s\n", passed ? "PASSED" : "FAILED");
    all = all && passed;
  }
  // rigid_config_PS's g_tesselation_factor: Warhammer's and Rome II's at offset 20, Warhammer II's at 16, Warhammer III's at 3 (its
  // hull shader's cb1[0].w, the domain shader's displacement cb1[1].x; its vertex shaders read them at cb2, the wind at cb1, both
  // bound here).
  const std::array<TessellatedGame, 4> tessellated = {{
      {"Warhammer", "twwarhammer", "0xE168387B.vs_5_0", "0xF04A8DF4.hs_5_0", "0x7C266A97.ds_5_0", "rt_capture_rigid_standard_sm41",
       "motion_record_rigid_rigidambientmapfiremesh_f6db", 5},
      {"Warhammer II", "twwarhammer2", "0x4CC2D68F.vs_5_0", "0xEB3218C8.hs_5_0", "0xC92A2D5F.ds_5_0", "rt_capture_rigid_standard",
       "motion_record_rigid_rigidstandard_16af", 4},
      {"Warhammer III D5D2905E", "twwarhammer3", "0xD5D2905E.vs_5_0", "0xFE7929BE.hs_5_0", "0x187E9195.ds_5_0", "rt_capture_rigid", "rt_rows_rigid", 3, 1},
      {"Warhammer III 958EC025", "twwarhammer3", "0x958EC025.vs_5_0", "0xFE7929BE.hs_5_0", "0x187E9195.ds_5_0", "rt_capture_rigid", "rt_rows_rigid", 3, 1},
  }};
  for (const TessellatedGame& game : tessellated) {
    all = CheckTessellated(game, root, build_root) && all;
  }
  all = CheckTessellated({"Rome II", "twrome2", "0xE51A2D2A.vs_5_0", "0xE2BF92FC.hs_5_0", "0xA13A267D.ds_5_0", "rt_capture_rigid_sm41_b",
                          "motion_record_rigid_sm41_b", 5, 0, true},
                         root, build32_root)
        && all;
  all = CheckTerrain(root, build32_root) && all;
  for (const BatchedFamily& family : {BatchedFamily{"Attila main units sm3", "0x5F9CD405.vs_4_0", "rt_capture_unit_sm3", "rt_batch_unit_sm3"},
                                      BatchedFamily{"Attila main units sm4.1", "0xA691CFF4.vs_4_1", "rt_capture_unit_sm41", "rt_batch_unit_sm41"}}) {
    all = CheckBatchedUnits(family, root, build32_root) && all;
  }
  std::printf("%s\n", all ? "ALL PASSED" : "SOME FAILED");
  return all ? 0 : 1;
}
