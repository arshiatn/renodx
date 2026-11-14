// The rays' geometry-shader captures (src/games/twattila/rt_core.hpp SceneShader::capture_geometry_shader) on WARP against the
// games' dumped bytes: the sprite soldiers' imposter vertex shader (vs40_main_gs) streamed out through its game's gs_main and
// through twattila/rt_capture_imposter_gs (the header the add-ons embed), per game:
//   Warhammer 381E079E + 12D3A4BC (shader model 4.0) and 883D12E7 + D6954DFC (4.1), Troy 370A7440 + DC05F551, Warhammer II
//   370A7440 + 29E8BDF7, Pharaoh 370A7440 + 27A16995.
// 256 soldiers, half standing, half flat (v1.w below 0), random cards and atlas cells (some wrapping). The camera 1000 km away
// level with them along a direction, rt_sun's texel 0 that direction: the game turns each card to the camera, the capture to
// the sun, so they must match.
// A. every corner's world position (the game's TEXCOORD2.xyz) within 2e-4 m of the capture's, in the strip's order;
// U. every corner's UV (the game's TEXCOORD1.xy) as the capture's two halves (rt_capture.hlsli RtWorldUv) within a half's step;
// N. the capture writes 2 triangles a soldier (its SO statistics): the count rt_core assumes (6 corners a point);
// C. CONTROL: the camera turned 90 degrees from the sun: corners differ by more than 0.1 m.
// tests.exe <dump root: the folder holding dump_shader_<game>_pack\cso>
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
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "../../src/games/twattila/rt_capture_imposter_gs.h"

static ID3D11Device* device = nullptr;
static ID3D11DeviceContext* context = nullptr;
constexpr UINT SOLDIERS = 256;

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

struct Streamed {
  std::vector<float> values;  // `components` floats a corner
  uint64_t primitives = 0;    // the SO statistics' NumPrimitivesWritten
};

// The soldiers (7 float4 inputs each, the vertex shader's input order) as points through `vs` and `gs`'s stream output of
// `semantic` / `index` (`components` floats); `gs_view` at the geometry shader's t0, `camera` at every stage's b0.
static Streamed StreamOut(const std::vector<uint8_t>& vs_code, const void* gs_code, size_t gs_size, const std::vector<float>& soldiers, const char* semantic,
                          UINT index, BYTE components, ID3D11Buffer* camera, ID3D11ShaderResourceView* gs_view) {
  Streamed out;
  ID3D11ShaderReflection* reflection = nullptr;
  if (FAILED(D3DReflect(vs_code.data(), vs_code.size(), IID_ID3D11ShaderReflection, reinterpret_cast<void**>(&reflection)))) return out;
  D3D11_SHADER_DESC desc = {};
  reflection->GetDesc(&desc);
  std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
  std::vector<std::string> names;
  names.reserve(desc.InputParameters);
  for (UINT i = 0; i < desc.InputParameters; ++i) {
    D3D11_SIGNATURE_PARAMETER_DESC parameter = {};
    reflection->GetInputParameterDesc(i, &parameter);
    names.emplace_back(parameter.SemanticName);
    elements.push_back({names.back().c_str(), parameter.SemanticIndex, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, UINT(elements.size() * 16), D3D11_INPUT_PER_VERTEX_DATA, 0});
  }
  const UINT stride = UINT(elements.size() * 16);
  const UINT count = UINT(soldiers.size() * 4 / stride);
  const D3D11_SO_DECLARATION_ENTRY entry = {0, semantic, index, 0, components, 0};
  const UINT out_stride = components * 4u;
  const UINT room = count * 8;
  const D3D11_BUFFER_DESC vertex_desc = {UINT(soldiers.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  const D3D11_SUBRESOURCE_DATA vertex_data = {soldiers.data(), 0, 0};
  const D3D11_BUFFER_DESC out_desc = {room * out_stride, D3D11_USAGE_DEFAULT, D3D11_BIND_STREAM_OUTPUT, 0, 0, 0};
  const D3D11_BUFFER_DESC read_desc = {room * out_stride, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
  const D3D11_QUERY_DESC query_desc = {D3D11_QUERY_SO_STATISTICS, 0};
  ID3D11InputLayout* layout = nullptr;
  ID3D11Buffer* vertex_buffer = nullptr;
  ID3D11VertexShader* vs = nullptr;
  ID3D11GeometryShader* gs = nullptr;
  ID3D11Buffer* output = nullptr;
  ID3D11Buffer* read = nullptr;
  ID3D11Query* query = nullptr;
  const bool made = SUCCEEDED(device->CreateInputLayout(elements.data(), UINT(elements.size()), vs_code.data(), vs_code.size(), &layout))
                    && SUCCEEDED(device->CreateBuffer(&vertex_desc, &vertex_data, &vertex_buffer))
                    && SUCCEEDED(device->CreateVertexShader(vs_code.data(), vs_code.size(), nullptr, &vs))
                    && SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(gs_code, gs_size, &entry, 1, &out_stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &gs))
                    && SUCCEEDED(device->CreateBuffer(&out_desc, nullptr, &output)) && SUCCEEDED(device->CreateBuffer(&read_desc, nullptr, &read))
                    && SUCCEEDED(device->CreateQuery(&query_desc, &query));
  if (made) {
    const UINT offset = 0;
    context->IASetInputLayout(layout);
    context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    context->VSSetShader(vs, nullptr, 0);
    context->GSSetShader(gs, nullptr, 0);
    context->PSSetShader(nullptr, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &camera);
    context->GSSetConstantBuffers(0, 1, &camera);
    context->GSSetShaderResources(0, 1, &gs_view);
    context->SOSetTargets(1, &output, &offset);
    context->Begin(query);
    context->Draw(count, 0);
    context->End(query);
    context->SOSetTargets(0, nullptr, nullptr);
    context->GSSetShader(nullptr, nullptr, 0);
    D3D11_QUERY_DATA_SO_STATISTICS statistics = {};
    while (context->GetData(query, &statistics, sizeof(statistics), 0) == S_FALSE) {
    }
    out.primitives = statistics.NumPrimitivesWritten;
    context->CopyResource(read, output);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(context->Map(read, 0, D3D11_MAP_READ, 0, &mapped))) {
      const auto* floats = static_cast<const float*>(mapped.pData);
      out.values.assign(floats, floats + (statistics.NumPrimitivesWritten * 3 * components));
      context->Unmap(read, 0);
    }
  } else {
    std::printf("  could not make the stream output (%s %u)\n", semantic, index);
  }
  for (IUnknown* object : std::initializer_list<IUnknown*>{layout, vertex_buffer, vs, gs, output, read, query}) {
    if (object != nullptr) object->Release();
  }
  reflection->Release();
  return out;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("tests.exe <dump root>\n");
    return 2;
  }
  const std::string root = argv[1];
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
  std::mt19937 rng(7);
  const auto range = [&rng](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
  std::vector<float> soldiers;
  for (UINT soldier = 0; soldier < SOLDIERS; ++soldier) {
    const bool flat = (soldier % 2) == 1;
    const std::array<float, 28> inputs = {
        range(-50.f, 50.f), range(-5.f, 5.f), range(-50.f, 50.f), range(0.f, 7.f),
        range(0.f, 1.f), range(0.f, 1.f), range(-1.f, 1.f), flat ? -range(0.f, 1.f) : range(0.f, 0.2f),
        range(1.f, 2.5f), range(-1.f, 1.f), range(0.2f, 1.f), range(-1.f, 1.f),
        range(-1.f, 1.f), range(0.1f, 1.f), range(0.1f, 1.f), range(-0.2f, 0.2f),
        range(-1.f, 1.f), range(-1.f, 1.f), range(-1.f, 1.f), range(-1.f, 1.f),
        range(-1.f, 1.f), range(-1.f, 1.f), 0.f, 0.f,
        range(0.05f, 0.2f), range(0.05f, 0.2f), 0.f, 0.f,
    };
    soldiers.insert(soldiers.end(), inputs.begin(), inputs.end());
  }
  // camera b0: camera_position c0, view_projection c9-c12 (identity: only TEXCOORD2 / TEXCOORD1 are compared).
  const auto camera_buffer = [](const std::array<float, 3>& position) {
    std::array<float, 16 * 4> constants = {};
    constants[0] = position[0];
    constants[1] = position[1];
    constants[2] = position[2];
    for (int row = 0; row < 4; ++row) constants[((9 + row) * 4) + row] = 1.f;
    const D3D11_BUFFER_DESC desc = {UINT(constants.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    const D3D11_SUBRESOURCE_DATA data = {constants.data(), 0, 0};
    ID3D11Buffer* buffer = nullptr;
    device->CreateBuffer(&desc, &data, &buffer);
    return buffer;
  };
  const std::array<float, 3> toward_sun = {0.6f, 0.f, 0.8f};
  ID3D11Buffer* sun_camera = camera_buffer({toward_sun[0] * 1.0e6f, 0.f, toward_sun[2] * 1.0e6f});
  ID3D11Buffer* turned_camera = camera_buffer({-toward_sun[2] * 1.0e6f, 0.f, toward_sun[0] * 1.0e6f});
  // rt_sun: texel 0 the way to the sun.
  const std::array<float, 4> sun_texel = {toward_sun[0], toward_sun[1], toward_sun[2], 0.f};
  const D3D11_TEXTURE2D_DESC sun_desc = {1, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  const D3D11_SUBRESOURCE_DATA sun_data = {sun_texel.data(), 16, 16};
  ID3D11Texture2D* sun_texture = nullptr;
  ID3D11ShaderResourceView* rt_sun = nullptr;
  device->CreateTexture2D(&sun_desc, &sun_data, &sun_texture);
  device->CreateShaderResourceView(sun_texture, nullptr, &rt_sun);

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
    const Streamed world = StreamOut(vs, gs.data(), gs.size(), soldiers, "TEXCOORD", 2, 4, sun_camera, nullptr);
    const Streamed uv = StreamOut(vs, gs.data(), gs.size(), soldiers, "TEXCOORD", 1, 2, sun_camera, nullptr);
    const Streamed turned = StreamOut(vs, gs.data(), gs.size(), soldiers, "TEXCOORD", 2, 4, turned_camera, nullptr);
    const Streamed capture = StreamOut(vs, __rt_capture_imposter_gs, sizeof(__rt_capture_imposter_gs), soldiers, "TEXCOORD", 0, 4, sun_camera, rt_sun);
    const size_t corners = capture.values.size() / 4;
    bool ok = corners == SOLDIERS * 6 && world.values.size() == corners * 4 && uv.values.size() == corners * 2 && turned.values.size() == corners * 4;
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
        const float game = uv.values[(corner * 2) + axis];
        const float captured = HalfValue((packed >> (axis * 16)) & 0xFFFFu);
        // The capture's half, or the next one down / up (its own float of the UV may round a step apart before the cut).
        const float step = std::abs(HalfValue(HalfBits(game) + 1u) - HalfValue(HalfBits(game)));
        if (std::abs(game - captured) > step * 1.01f) ++uv_off;
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
  std::printf("%s\n", all ? "ALL PASSED" : "SOME FAILED");
  return all ? 0 : 1;
}
