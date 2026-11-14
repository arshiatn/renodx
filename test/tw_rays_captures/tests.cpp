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
//   C92A2D5F) through 16AFAF14's (the add-ons' built shaders); 64 patches of 3, each its placement:
//   T1 the capture's TEXCOORD0 (a triangle list, no hull or domain shader) is the game vertex shader's world position (its
//      TEXCOORD0) bit for bit, w 1;
//   T2 the rows shader's TEXCOORD0-2 are POSITION1-3 bit for bit;
//   T3 the game's chain (Tessellation 4, displacement 0.1) with the camera 17 km away (past 20 m: no tessellation, the
//      displacement faded out) streams out the capture's triangles, patch for patch, their corners bit for bit;
//   T4 CONTROL: the camera among the patches (under 5 m: Tessellation 4, the displacement in full): the chain's triangles leave
//      the capture's plane, by at most the displacement.
// tests.exe <dump root: the folder holding dump_shader_<game>_pack\cso> <build root: the folder holding <game>.include\embed>
#include <windows.h>

#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>

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
  const UINT stride = UINT(elements.size() * 16);
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
  // POSITION0, TEXCOORD0, NORMAL0-2, COLOR0, POSITION1-7 (13 inputs); POSITION0 and the rows small (the world within 1.25 m of
  // the origin), each patch its placement.
  constexpr UINT INPUTS = 13;
  std::vector<float> vertices;
  for (UINT patch = 0; patch < PATCHES; ++patch) {
    std::array<std::array<float, 4>, 3> placement = {};
    for (auto& row : placement) row = {Range(-0.5f, 0.5f), Range(-0.5f, 0.5f), Range(-0.5f, 0.5f), Range(-0.5f, 0.5f)};
    for (int corner = 0; corner < 3; ++corner) {
      for (UINT input = 0; input < INPUTS; ++input) {
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

  const std::array<ID3D11Buffer*, 3> vs_constants = {far_camera, wind, config};
  const auto captured = StreamOut({.vs = &capture, .topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, .vs_constants = vs_constants}, vertices, "TEXCOORD", 0, 4).values;
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
    const auto rows_out = StreamOut({.vs = &rows, .vs_constants = vs_constants}, vertices, "TEXCOORD", row, 4).values;
    for (size_t v = 0; v < PATCHES * 3; ++v) {
      t2_differ += (rows_out.size() == PATCHES * 12 && SameBits(&rows_out[v * 4], &vertices[((v * INPUTS) + 6 + row) * 4], 4)) ? 0 : 1;
    }
  }
  const bool t2 = t2_differ == 0;
  std::printf("  T2 rows vs POSITION1-3: %llu of %u differ (0)\n", (unsigned long long)t2_differ, PATCHES * 9);
  Draw chain = {.vs = &vs, .hs = &hs, .ds = &ds, .topology = D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST, .vs_constants = vs_constants,
                .tessellation_constants = {far_camera, config}, .ds_view = displacement, .ds_sampler = sampler};
  // The domain shader's TEXCOORD0: the world position, w 1.
  const auto far_chain = StreamOut(chain, vertices, "TEXCOORD", 0, 4).values;
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
  const auto near_chain = StreamOut(chain, vertices, "TEXCOORD", 0, 4).values;
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

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("tests.exe <dump root> <build root>\n");
    return 2;
  }
  const std::string root = argv[1];
  const std::string build_root = argv[2];
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
  // rigid_config_PS's g_tesselation_factor: Warhammer's at offset 20, Warhammer II's at 16.
  const std::array<TessellatedGame, 2> tessellated = {{
      {"Warhammer", "twwarhammer", "0xE168387B.vs_5_0", "0xF04A8DF4.hs_5_0", "0x7C266A97.ds_5_0", "rt_capture_rigid_standard_sm41",
       "motion_record_rigid_rigidambientmapfiremesh_f6db", 5},
      {"Warhammer II", "twwarhammer2", "0x4CC2D68F.vs_5_0", "0xEB3218C8.hs_5_0", "0xC92A2D5F.ds_5_0", "rt_capture_rigid_standard",
       "motion_record_rigid_rigidstandard_16af", 4},
  }};
  for (const TessellatedGame& game : tessellated) {
    all = CheckTessellated(game, root, build_root) && all;
  }
  std::printf("%s\n", all ? "ALL PASSED" : "SOME FAILED");
  return all ? 0 : 1;
}
