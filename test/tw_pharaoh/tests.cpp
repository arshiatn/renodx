// Pharaoh's (src/games/twpharaohdynasties) ray-traced captures against the game's own bytes.
// tests.exe <the game's dumped csos> <the add-on's built csos (build\twpharaohdynasties.include\embed)>
// 1. The campaign's trees (rigidcampaigntrees vs40_main 16D14106, Troy's 07321725 line for line): the capture
//    (rt_capture_campaign_trees, Troy's) from random instanced vertices: its world the game's TEXCOORD0, its UV TEXCOORD0's
//    (o3.xy) as halves; CONTROL an instance row a metre off. Their shadow pass 81696642 through Troy's outside-view geometry
//    shader (RT_WORLD_UV + RT_COLOUR) with every triangle outside the main view: each corner its TEXCOORD0 world and the UV;
//    CONTROL Warhammer II's RT_WORLD_UV (no COLOR0 member) on the same vertex shader differs.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../../src/games/twtroy/rt_outside_view_colour_world_uv_gs.h"
#include "../../src/games/twwarhammer2/rt_outside_view_world_uv_gs.h"

extern "C" __declspec(dllexport) const char* const NAME = "Pharaoh test";
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) { std::printf("  log: %s\n", message); }

using Microsoft::WRL::ComPtr;

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
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

// The camera (b0, 784 bytes) of a fog draw in Troy's tools\tw\troy\runs\ATN_session3_030818\trace_04_031153.txt: Pharaoh's camera
// buffer has the same layout (camera_position c0, view c1, projection c5, view_projection c9, inv_view c13).
constexpr std::array<float, 196> CAMERA = {
    625.85455f, 126.21127f, -607.9252f, 6.6291106e-10f, 0.6160624f, 5.1151194e-09f, 0.7876973f, 93.29556f,
    -0.27021432f, 0.9393195f, 0.21133609f, 179.0387f, -0.7398995f, -0.34304333f, 0.5786795f, 858.15924f, 0.f, 0.f,
    0.f, 1.f, 0.88294804f, 0.f, 0.00022786458f, 0.f, 0.f, 1.5696855f, 0.00033150436f, 0.f, 0.f, 0.f, 1.000077f,
    -1.000077f, 0.f, 0.f, 1.f, 0.f, 0.5437825f, -7.8162906e-05f, 0.6956276f, 82.57068f, -0.42439675f, 1.4743224f,
    0.33192304f, 281.3189f, -0.7399565f, -0.34306973f, 0.5787241f, 857.2253f, -0.7398995f, -0.34304333f, 0.5786795f,
    858.15924f, 0.6160624f, -0.27021432f, -0.7398995f, 625.85455f, 5.1151208e-09f, 0.93931973f, -0.34304342f,
    126.21129f, 0.7876973f, 0.21133609f, 0.5786795f, -607.9252f, 0.f, 0.f, 0.f, 1.f, 1.1325696f, 0.f, 0.f,
    -0.00025807245f, 0.f, 0.63707036f, 0.f, -0.00021119158f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, -0.99992305f, 1.f,
    0.6977302f, -0.17213511f, -625.8183f, 625.12665f, 1.1008985e-05f, 0.59840715f, -126.20397f, 125.870476f,
    0.89212406f, 0.13463713f, 607.8901f, -607.35846f, 5.375481e-09f, 1.0750962e-08f, -0.9999423f, 1.0000193f, 1.f,
    13000.f, 7.6929e-05f, 7.6929e-05f, 24.614f, 1.1325694f, 0.6370703f, 1.134464f, 3840.f, 2160.f, 0.00026041668f,
    0.00046296295f, 0.f, 6.26474e-10f, 2.6254088e-06f, 2.5607402e-12f, 1920.f, 1080.f, 0.00052083336f, 0.0009259259f,
    0.f, 0.f, 6.4094063e-10f, 6.8242946e-07f, 3840.f, 2160.f, 0.00026041668f, 0.00046296295f, 0.f, 0.f, 0.f, 0.f, 0.f,
    0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, -1.f, -1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 1e-45f, 1.f, 3.4444333e-12f,
    7.0366745e+22f, 6.8192746e-10f, 0.f, 0.f, 0.f, 0.f, 624.589f, 125.269806f, -608.3733f, 6.8192746e-10f, 625.9845f,
    125.26984f, -606.589f, 1.0313943e-08f, 625.64026f, 126.46669f, -606.3197f, 6.8192746e-10f, 624.24475f, 126.46666f,
    -608.104f, 1.06222515e-05f, -15825.483f, -12112.719f, -6432.9443f, 6.8192746e-10f, 2315.5881f, -12112.719f,
    16762.225f, 1.6691486e-07f, -2160.1956f, 3446.0122f, 20262.76f, 6.8192746e-10f, -20301.268f, 3446.0122f,
    -2932.4094f, 0.f, 0.f, 0.f, 1.3f, 0.f, 0.f, 0.f, 0.f, 0.f,
};

static std::vector<char> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static ComPtr<ID3D11Buffer> ConstantBuffer(const void* data, UINT size) {
  std::vector<char> padded((size + 15) & ~15u, 0);
  std::memcpy(padded.data(), data, size);
  const D3D11_BUFFER_DESC desc = {.ByteWidth = static_cast<UINT>(padded.size()), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = padded.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &initial, &buffer);
  return buffer;
}

static float HalfToFloat(uint32_t bits) {
  const uint32_t exponent = (bits >> 10) & 0x1Fu;
  const uint32_t value = (exponent == 0) ? ((bits & 0x8000u) << 16) : (((bits & 0x8000u) << 16) | ((exponent + 112u) << 23) | ((bits & 0x3FFu) << 13));
  float result;
  std::memcpy(&result, &value, 4);
  return result;
}

// The vertex shader's inputs (its signature's, system values aside) each 4 floats of `vertices` in order, from one buffer.
static ComPtr<ID3D11InputLayout> InputLayout(const std::vector<char>& code, UINT* stride) {
  ComPtr<ID3D11ShaderReflection> reflection;
  D3DReflect(code.data(), code.size(), IID_PPV_ARGS(&reflection));
  if (reflection == nullptr) return nullptr;
  D3D11_SHADER_DESC shader_desc = {};
  reflection->GetDesc(&shader_desc);
  std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
  for (UINT parameter = 0; parameter < shader_desc.InputParameters; ++parameter) {
    D3D11_SIGNATURE_PARAMETER_DESC input = {};
    reflection->GetInputParameterDesc(parameter, &input);
    if (input.SystemValueType != D3D_NAME_UNDEFINED) continue;
    const DXGI_FORMAT format = (input.ComponentType == D3D_REGISTER_COMPONENT_UINT32) ? DXGI_FORMAT_R32G32B32A32_UINT
                               : (input.ComponentType == D3D_REGISTER_COMPONENT_SINT32) ? DXGI_FORMAT_R32G32B32A32_SINT
                                                                                         : DXGI_FORMAT_R32G32B32A32_FLOAT;
    elements.push_back({input.SemanticName, input.SemanticIndex, format, 0, static_cast<UINT>(16 * elements.size()), D3D11_INPUT_PER_VERTEX_DATA, 0});
  }
  ComPtr<ID3D11InputLayout> layout;
  device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()), code.data(), code.size(), &layout);
  *stride = static_cast<UINT>(16 * elements.size());
  return layout;
}

// What `geometry` streams out (4 floats a vertex) of `vertices` drawn as `topology` with the vertex shader `vertex_code` and
// `constants` at the vertex stage's b0..; `geometry_constant` at the geometry stage's b0.
static std::vector<float> Run(const std::vector<char>& vertex_code, ID3D11GeometryShader* geometry, D3D11_PRIMITIVE_TOPOLOGY topology,
                              const std::vector<float>& vertices, std::span<const ComPtr<ID3D11Buffer>> constants,
                              ID3D11Buffer* geometry_constant) {
  ComPtr<ID3D11VertexShader> shader;
  device->CreateVertexShader(vertex_code.data(), vertex_code.size(), nullptr, &shader);
  UINT input_stride = 0;
  const ComPtr<ID3D11InputLayout> layout = InputLayout(vertex_code, &input_stride);
  CHECK(shader != nullptr && geometry != nullptr && layout != nullptr, "stream output: the shaders");
  if (shader == nullptr || geometry == nullptr || layout == nullptr) return {};
  const UINT count = static_cast<UINT>(vertices.size() * 4 / input_stride);
  const D3D11_BUFFER_DESC input_desc = {.ByteWidth = static_cast<UINT>(vertices.size() * 4), .Usage = D3D11_USAGE_DEFAULT,
                                        .BindFlags = D3D11_BIND_VERTEX_BUFFER};
  const D3D11_SUBRESOURCE_DATA input_data = {.pSysMem = vertices.data()};
  ComPtr<ID3D11Buffer> input;
  device->CreateBuffer(&input_desc, &input_data, &input);
  const UINT stride = 16;
  const D3D11_BUFFER_DESC out_desc = {.ByteWidth = count * stride, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_STREAM_OUTPUT};
  ComPtr<ID3D11Buffer> out;
  device->CreateBuffer(&out_desc, nullptr, &out);
  context->ClearState();
  context->IASetInputLayout(layout.Get());
  const UINT offset = 0;
  context->IASetVertexBuffers(0, 1, input.GetAddressOf(), &input_stride, &offset);
  context->IASetPrimitiveTopology(topology);
  for (UINT slot = 0; slot < constants.size(); ++slot) context->VSSetConstantBuffers(slot, 1, constants[slot].GetAddressOf());
  if (geometry_constant != nullptr) context->GSSetConstantBuffers(0, 1, &geometry_constant);
  context->VSSetShader(shader.Get(), nullptr, 0);
  context->GSSetShader(geometry, nullptr, 0);
  context->SOSetTargets(1, out.GetAddressOf(), &offset);
  context->Draw(count, 0);
  ID3D11Buffer* no_target = nullptr;
  context->SOSetTargets(1, &no_target, &offset);
  const D3D11_BUFFER_DESC staging_desc = {.ByteWidth = count * stride, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  device->CreateBuffer(&staging_desc, nullptr, &staging);
  context->CopyResource(staging.Get(), out.Get());
  std::vector<float> values(count * 4, 0.f);
  D3D11_MAPPED_SUBRESOURCE mapped;
  if (SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
    std::memcpy(values.data(), mapped.pData, values.size() * 4);
    context->Unmap(staging.Get(), 0);
  }
  return values;
}

// A vertex shader's output `semantic` (`semantic_index`) for each point of `vertices`, streamed out through a geometry shader made
// from its own bytes.
static std::vector<float> StreamOut(const std::vector<char>& code, const char* semantic, const std::vector<float>& vertices,
                                    std::span<const ComPtr<ID3D11Buffer>> constants, UINT semantic_index = 0) {
  const D3D11_SO_DECLARATION_ENTRY entry = {.SemanticName = semantic, .SemanticIndex = semantic_index, .ComponentCount = 4};
  const UINT stride = 16;
  ComPtr<ID3D11GeometryShader> stream;
  device->CreateGeometryShaderWithStreamOutput(code.data(), code.size(), &entry, 1, &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM,
                                               nullptr, &stream);
  return Run(code, stream.Get(), D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, vertices, constants, nullptr);
}

// Each triangle corner's SV_Position out of the geometry shader `geometry_code` over `vertices` drawn with `vertex_code`;
// `main_view` at the geometry stage's b0.
static std::vector<float> StreamOutThrough(const std::vector<char>& vertex_code, std::span<const uint8_t> geometry_code,
                                           const std::vector<float>& vertices, std::span<const ComPtr<ID3D11Buffer>> constants,
                                           ID3D11Buffer* main_view) {
  const D3D11_SO_DECLARATION_ENTRY entry = {.SemanticName = "SV_Position", .ComponentCount = 4};
  const UINT stride = 16;
  ComPtr<ID3D11GeometryShader> geometry;
  device->CreateGeometryShaderWithStreamOutput(geometry_code.data(), geometry_code.size(), &entry, 1, &stride, 1,
                                               D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &geometry);
  return Run(vertex_code, geometry.Get(), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, vertices, constants, main_view);
}

// The campaign's trees: the capture against the game's 16D14106 from random instanced vertices, their shadow pass 81696642
// through Troy's RT_WORLD_UV + RT_COLOUR geometry shader with every triangle outside the main view.
static void TestCampaignTrees(const std::string& game_folder, const std::string& built_folder) {
  const auto game = ReadFile(game_folder + "\\0x16D14106.vs_5_0.cso");
  const auto shadow = ReadFile(game_folder + "\\0x81696642.vs_5_0.cso");
  const auto capture = ReadFile(built_folder + "\\rt_capture_campaign_trees.cso");
  CHECK(!game.empty() && !shadow.empty() && !capture.empty(), "campaign trees: the csos");
  if (game.empty() || shadow.empty() || capture.empty()) return;
  uint32_t seed = 777;
  const auto random = [&seed](float low, float high) {
    seed = seed * 1664525u + 1013904223u;
    return low + (high - low) * static_cast<float>(seed >> 8) / 16777216.f;
  };
  // 13 inputs x 4 floats: POSITION0 the model's point (w 1), TEXCOORD0 its UV, the normals, colour, the instance's rows in
  // POSITION1-3 (a turn and scale, the place near the camera in w), the rest random. 85 triangles.
  constexpr int VERTICES = 255;
  std::vector<float> vertices;
  for (int vertex = 0; vertex < VERTICES; ++vertex) {
    const float turn = random(0.f, 6.28f);
    const float scale = random(0.6f, 1.4f);
    for (int input = 0; input < 13; ++input) {
      std::array<float, 4> value = {random(-1.f, 1.f), random(-1.f, 1.f), random(-1.f, 1.f), random(0.f, 1.f)};
      if (input == 0) value = {random(-4.f, 4.f), random(0.f, 12.f), random(-4.f, 4.f), 1.f};
      if (input == 1) value = {random(0.f, 1.f), random(0.f, 1.f), 0.f, 0.f};
      if (input == 7) value = {scale * std::cos(turn), 0.f, scale * std::sin(turn), CAMERA[0] + random(-300.f, 300.f)};
      if (input == 8) value = {0.f, scale, 0.f, CAMERA[1] - random(20.f, 200.f)};
      if (input == 9) value = {-scale * std::sin(turn), 0.f, scale * std::cos(turn), CAMERA[2] + random(-300.f, 300.f)};
      vertices.insert(vertices.end(), value.begin(), value.end());
    }
  }
  const std::array<ComPtr<ID3D11Buffer>, 1> constants = {ConstantBuffer(CAMERA.data(), sizeof(CAMERA))};
  const auto reference = StreamOut(game, "TEXCOORD", vertices, constants);
  const auto worlds = StreamOut(capture, "TEXCOORD", vertices, constants);
  auto moved = vertices;
  for (int vertex = 0; vertex < VERTICES; ++vertex) moved[vertex * 52 + 7 * 4 + 3] += 1.f;
  const auto moved_worlds = StreamOut(capture, "TEXCOORD", moved, constants);
  const auto shadow_worlds = StreamOut(shadow, "TEXCOORD", vertices, constants);
  // Every triangle outside the main view (w -1 everywhere).
  std::array<float, 56> main_view = {};
  main_view[13 * 4 + 3] = -1.f;
  const auto view = ConstantBuffer(main_view.data(), sizeof(main_view));
  const auto kept = StreamOutThrough(shadow, std::span<const uint8_t>(__rt_outside_view_colour_world_uv, sizeof(__rt_outside_view_colour_world_uv)),
                                     vertices, constants, view.Get());
  const auto kept_control = StreamOutThrough(shadow, std::span<const uint8_t>(__rt_outside_view_world_uv, sizeof(__rt_outside_view_world_uv)),
                                             vertices, constants, view.Get());
  if (reference.size() != VERTICES * 4 || worlds.size() != VERTICES * 4 || moved_worlds.size() != VERTICES * 4
      || shadow_worlds.size() != VERTICES * 4 || kept.size() != VERTICES * 4 || kept_control.size() != VERTICES * 4) {
    CHECK(false, "campaign trees: stream output sizes");
    return;
  }
  // The world against `expected` (x / w), relative to the size; the UV halves in w against TEXCOORD0.
  const auto errors = [&vertices](const std::vector<float>& world, const std::vector<float>& expected) {
    double place = 0.0;
    double uv = 0.0;
    for (int vertex = 0; vertex < VERTICES; ++vertex) {
      for (int axis = 0; axis < 3; ++axis) {
        const double want = static_cast<double>(expected[vertex * 4 + axis]) / expected[vertex * 4 + 3];
        place = std::max(place, std::abs(world[vertex * 4 + axis] - want) / std::max(1.0, std::abs(want)));
      }
      uint32_t bits;
      std::memcpy(&bits, &world[vertex * 4 + 3], 4);
      uv = std::max(uv, static_cast<double>(std::abs(HalfToFloat(bits & 0xFFFFu) - vertices[vertex * 52 + 4])));
      uv = std::max(uv, static_cast<double>(std::abs(HalfToFloat(bits >> 16) - vertices[vertex * 52 + 5])));
    }
    return std::array<double, 2>{place, uv};
  };
  const auto capture_errors = errors(worlds, reference);
  const auto moved_errors = errors(moved_worlds, reference);
  const auto kept_errors = errors(kept, shadow_worlds);
  const auto control_errors = errors(kept_control, shadow_worlds);
  CHECK(capture_errors[0] < 1e-6 && capture_errors[1] < 1e-3, "campaign trees: the capture %g off the game's world, its UV %g",
        capture_errors[0], capture_errors[1]);
  CHECK(moved_errors[0] > 1e-3, "campaign trees: CONTROL an instance row a metre off = the game's (%g)", moved_errors[0]);
  CHECK(kept_errors[0] < 1e-6 && kept_errors[1] < 1e-3, "campaign trees' shadow pass: kept corners %g off its world, the UV %g",
        kept_errors[0], kept_errors[1]);
  CHECK(control_errors[0] > 1e-3, "campaign trees' shadow pass: CONTROL without COLOR0 = its world (%g)", control_errors[0]);
  std::printf("1 16D14106: the capture's world within %.2g of the game's, its UV within %.2g; CONTROL a row a metre off %.2g. "
              "81696642 kept outside the view: its world within %.2g, the UV within %.2g; CONTROL without COLOR0 %.2g\n",
              capture_errors[0], capture_errors[1], moved_errors[0], kept_errors[0], kept_errors[1], control_errors[0]);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: pharaoh.exe <the game's csos> <the add-on's built csos>\n");
    return 2;
  }
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context))) {
    std::printf("no WARP device\n");
    return 1;
  }
  TestCampaignTrees(argv[1], argv[2]);
  std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
