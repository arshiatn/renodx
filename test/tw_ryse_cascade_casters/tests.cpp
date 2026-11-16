// Ryse's Shadow Casters Out of View (rysesonofrome/ray_tracing.hpp SceneDraw, rt_core's cascade capture) on WARP with the
// game's own sun caster vertex shaders (the dumped 0x7EB6531F: solid, SV_Position; 0x48CE820A: alpha-tested, SV_Position and
// TEXCOORD0) and the geometry shaders the add-on streams them through (Warhammer III's rt_outside_view, Ryse's
// rt_outside_view_uv). The draw as rt_core makes it: the game's vertex shader bound, PER_FRAME (b2) replaced by rt_core's
// cascade buffer (36 float4, 0 but the main view's matrix at Host::cascade_view_row, here 0, as CaptureCamera read it from
// the G-buffer's b2 c0-c3), stream output only. Checked over 512 triangles (in the view, behind the camera, beside it,
// across its edges):
// - the same vertex shader streamed out alone (rt_core's G-buffer path) gives the main view's clip space: main view x world
//   on the CPU (ObjWorldMatrix applied), within 1e-5 of the largest component;
// - through the geometry shader a triangle wholly outside one of the view's planes (or behind it) keeps its three corners
//   bit for bit, any other collapses onto its first corner; the UV pass on unchanged (uv shader);
// - the solid shader's geometry shader takes the alpha-tested vertex shader too (its signature a prefix).
// CONTROLS (must fail): the vertex shader streamed alone (nothing collapsed) against the rule; for the first check the
// game's own PER_FRAME (the light's matrix) left at b2 and rt_core's buffer with the
// matrix at Warhammer III's row 10.
// Arguments: the repository and the folder with the dumped 0x7EB6531F.vs_5_0.cso and 0x48CE820A.vs_5_0.cso; only read.
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

#include "src/games/rysesonofrome/rt_outside_view_uv_gs.h"
#include "src/games/twwarhammer3/rt_outside_view_gs.h"

using Microsoft::WRL::ComPtr;

namespace {

int failures = 0;

#define CHECK(condition)                                       \
  do {                                                         \
    if (!(condition)) {                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #condition); \
      ++failures;                                              \
    }                                                          \
  } while (false)

std::vector<char> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

uint32_t seed = 7331;
float Random(float low, float high) {
  seed = seed * 1664525u + 1013904223u;
  return low + ((high - low) * static_cast<float>(seed >> 8) / 16777216.f);
}

using Matrix = std::array<float, 16>;  // rows acting on column vectors, as the shaders' g_VS_ViewProjMatr

Matrix Multiply(const Matrix& a, const Matrix& b) {
  Matrix result = {};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      for (int k = 0; k < 4; ++k) result[(row * 4) + column] += a[(row * 4) + k] * b[(k * 4) + column];
    }
  }
  return result;
}

struct Vertex {
  std::array<float, 4> position;
  std::array<float, 2> uv;
  std::array<float, 4> color;
  std::array<float, 4> tangent;
  std::array<float, 4> binormal;
};

constexpr UINT TRIANGLES = 512;
constexpr UINT CORNERS = TRIANGLES * 3;

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: ryse_cascade_casters.exe <repository> <dump folder>\n");
    return 2;
  }
  const std::filesystem::path dumps = argv[2];
  const auto solid_code = ReadFile(dumps / "0x7EB6531F.vs_5_0.cso");
  const auto cut_out_code = ReadFile(dumps / "0x48CE820A.vs_5_0.cso");
  if (solid_code.empty() || cut_out_code.empty()) {
    std::printf("the dumped caster vertex shaders are missing\n");
    return 2;
  }
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                               context.GetAddressOf()))) {
    std::printf("no WARP device\n");
    return 2;
  }

  // The main view: z up, the camera at (12, -40, 3) looking along +y, 60 degrees, 16:9, near 0.25, far 4000 (Ryse's world is z-up).
  const float f = 1.f / std::tan(0.5f * 1.0472f);
  const float aspect = 16.f / 9.f;
  const float near_plane = 0.25f;
  const float far_plane = 4000.f;
  const Matrix projection = {f / aspect, 0.f, 0.f, 0.f,  //
                             0.f, 0.f, f, 0.f,           //
                             0.f, far_plane / (far_plane - near_plane), 0.f, -near_plane * far_plane / (far_plane - near_plane),
                             0.f, 1.f, 0.f, 0.f};
  const std::array<float, 3> camera = {12.f, -40.f, 3.f};
  const Matrix to_camera = {1.f, 0.f, 0.f, -camera[0], 0.f, 1.f, 0.f, -camera[1], 0.f, 0.f, 1.f, -camera[2], 0.f, 0.f, 0.f, 1.f};
  const Matrix main_view = Multiply(projection, to_camera);
  // The light's (the game's PER_FRAME in its shadow pass): an orthographic view down a slanted sun.
  const Matrix light_view = {0.02f, 0.f, 0.005f, 0.1f, 0.f, 0.02f, 0.004f, -0.2f, 0.001f, -0.002f, 0.003f, 0.5f, 0.f, 0.f, 0.f, 1.f};
  // ObjWorldMatrix (PER_INSTANCE b1, 3 rows): turned, scaled, moved.
  const std::array<float, 16> per_instance = {0.9f, -0.1f, 0.f, 5.f,  //
                                              0.1f, 0.9f, 0.f, 20.f,  //
                                              0.f, 0.f, 1.1f, -1.f,   //
                                              0.f, 0.f, 0.f, 0.f};     // BendInfo

  // Triangles around the view: centres in front, behind, beside, above and across the edges; corners a few metres apart.
  std::vector<Vertex> vertices;
  vertices.reserve(CORNERS);
  for (UINT triangle = 0; triangle < TRIANGLES; ++triangle) {
    const std::array<float, 3> centre = {Random(-120.f, 120.f), Random(-160.f, 160.f), Random(-40.f, 60.f)};
    const float size = Random(0.5f, 30.f);
    for (UINT corner = 0; corner < 3; ++corner) {
      vertices.push_back({.position = {centre[0] + Random(-size, size), centre[1] + Random(-size, size), centre[2] + Random(-size, size), Random(0.f, 1.f)},
                          .uv = {Random(-2.f, 3.f), Random(-2.f, 3.f)},
                          .color = {1.f, 1.f, 1.f, 1.f},
                          .tangent = {1.f, 0.f, 0.f, 1.f},
                          .binormal = {0.f, 1.f, 0.f, 1.f}});
    }
  }
  // The main view's clip position of each corner on the CPU (ObjWorldMatrix, then the main view).
  std::vector<std::array<float, 4>> expected(CORNERS);
  for (UINT corner = 0; corner < CORNERS; ++corner) {
    std::array<float, 4> world = {0.f, 0.f, 0.f, 1.f};
    for (int row = 0; row < 3; ++row) {
      world[row] = (per_instance[(row * 4) + 0] * vertices[corner].position[0]) + (per_instance[(row * 4) + 1] * vertices[corner].position[1])
                   + (per_instance[(row * 4) + 2] * vertices[corner].position[2]) + per_instance[(row * 4) + 3];
    }
    for (int row = 0; row < 4; ++row) {
      expected[corner][row] = (main_view[(row * 4) + 0] * world[0]) + (main_view[(row * 4) + 1] * world[1]) + (main_view[(row * 4) + 2] * world[2])
                              + (main_view[(row * 4) + 3] * world[3]);
    }
  }

  const D3D11_BUFFER_DESC vertex_desc = {.ByteWidth = static_cast<UINT>(sizeof(Vertex) * CORNERS), .Usage = D3D11_USAGE_DEFAULT,
                                         .BindFlags = D3D11_BIND_VERTEX_BUFFER};
  const D3D11_SUBRESOURCE_DATA vertex_data = {.pSysMem = vertices.data()};
  ComPtr<ID3D11Buffer> vertex_buffer;
  device->CreateBuffer(&vertex_desc, &vertex_data, vertex_buffer.GetAddressOf());
  const auto constants = [&](const float* values, UINT count) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = count * 4, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = values};
    ComPtr<ID3D11Buffer> buffer;
    device->CreateBuffer(&desc, &data, buffer.GetAddressOf());
    return buffer;
  };
  const auto instance_buffer = constants(per_instance.data(), 16);
  // rt_core's cascade buffer (Capture: 36 float4, the main view's matrix at row * 4), with Ryse's row 0 and Warhammer III's 10.
  const auto cascade_buffer = [&](UINT row) {
    std::array<float, 36 * 4> values = {};
    std::ranges::copy(main_view, values.begin() + (row * 4));
    return constants(values.data(), static_cast<UINT>(values.size()));
  };
  const auto ryse_cascade = cascade_buffer(0);
  const auto wh3_cascade = cascade_buffer(10);
  // The game's PER_FRAME in its shadow pass (32 float4): the light's matrix at c0-c3, its position at c6, the time at c8.
  std::array<float, 32 * 4> light_frame = {};
  std::ranges::copy(light_view, light_frame.begin());
  light_frame[(6 * 4) + 0] = 100.f;
  light_frame[(6 * 4) + 2] = 300.f;
  light_frame[(8 * 4) + 2] = 12.5f;
  const auto light_buffer = constants(light_frame.data(), static_cast<UINT>(light_frame.size()));

  const std::array<D3D11_INPUT_ELEMENT_DESC, 5> elements = {{
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"BINORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0},
  }};
  struct Shader {
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11InputLayout> layout;
  };
  const auto make_shader = [&](const std::vector<char>& code) {
    Shader shader;
    CHECK(SUCCEEDED(device->CreateVertexShader(code.data(), code.size(), nullptr, shader.vertex.GetAddressOf())));
    CHECK(SUCCEEDED(device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()), code.data(), code.size(), shader.layout.GetAddressOf())));
    return shader;
  };
  const Shader solid = make_shader(solid_code);
  const Shader cut_out = make_shader(cut_out_code);

  // rt_core's stream output: SV_POSITION into buffer 0, the UV into buffer 1 (with the uv shader).
  const std::array<D3D11_SO_DECLARATION_ENTRY, 2> entries = {{
      {.SemanticName = "SV_POSITION", .SemanticIndex = 0, .ComponentCount = 4},
      {.SemanticName = "TEXCOORD", .SemanticIndex = 0, .ComponentCount = 2, .OutputSlot = 1},
  }};
  constexpr std::array<UINT, 2> STRIDES = {16, 8};
  const auto stream_output = [&](std::span<const uint8_t> code, bool uv) {
    ComPtr<ID3D11GeometryShader> shader;
    CHECK(SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(code.data(), code.size(), entries.data(), uv ? 2 : 1, STRIDES.data(), uv ? 2 : 1,
                                                                 D3D11_SO_NO_RASTERIZED_STREAM, nullptr, shader.GetAddressOf())));
    return shader;
  };
  const auto solid_alone = stream_output({reinterpret_cast<const uint8_t*>(solid_code.data()), solid_code.size()}, false);
  const auto keep = stream_output(__rt_outside_view, false);
  const auto keep_uv = stream_output(__rys_outside_view_uv, true);

  struct Streamed {
    std::vector<std::array<float, 4>> positions;
    std::vector<std::array<float, 2>> uvs;
  };
  const auto read_back = [&](ID3D11Buffer* buffer, UINT bytes, void* out) {
    const D3D11_BUFFER_DESC desc = {.ByteWidth = bytes, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
    ComPtr<ID3D11Buffer> staging;
    device->CreateBuffer(&desc, nullptr, staging.GetAddressOf());
    context->CopyResource(staging.Get(), buffer);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    std::memcpy(out, mapped.pData, bytes);
    context->Unmap(staging.Get(), 0);
  };
  // One draw as rt_core's Capture makes it: the game's vertex shader, `frame` at b2, the stream output's geometry shader.
  const auto draw = [&](const Shader& shader, ID3D11GeometryShader* geometry, ID3D11Buffer* frame, bool uv) {
    const D3D11_BUFFER_DESC position_desc = {.ByteWidth = CORNERS * 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_STREAM_OUTPUT};
    const D3D11_BUFFER_DESC uv_desc = {.ByteWidth = CORNERS * 8, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_STREAM_OUTPUT};
    std::array<ComPtr<ID3D11Buffer>, 2> targets;
    device->CreateBuffer(&position_desc, nullptr, targets[0].GetAddressOf());
    device->CreateBuffer(&uv_desc, nullptr, targets[1].GetAddressOf());
    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
    context->IASetInputLayout(shader.layout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(shader.vertex.Get(), nullptr, 0);
    const std::array<ID3D11Buffer*, 2> vertex_constants = {instance_buffer.Get(), frame};
    context->VSSetConstantBuffers(1, 2, vertex_constants.data());
    context->GSSetShader(geometry, nullptr, 0);
    context->PSSetShader(nullptr, nullptr, 0);
    const std::array<ID3D11Buffer*, 2> outputs = {targets[0].Get(), targets[1].Get()};
    const std::array<UINT, 2> offsets = {0, 0};
    context->SOSetTargets(uv ? 2 : 1, outputs.data(), offsets.data());
    context->Draw(CORNERS, 0);
    const std::array<ID3D11Buffer*, 2> none = {};
    context->SOSetTargets(2, none.data(), offsets.data());
    Streamed streamed = {.positions = std::vector<std::array<float, 4>>(CORNERS), .uvs = std::vector<std::array<float, 2>>(CORNERS)};
    read_back(targets[0].Get(), CORNERS * 16, streamed.positions.data());
    if (uv) read_back(targets[1].Get(), CORNERS * 8, streamed.uvs.data());
    return streamed;
  };
  // Corners off the CPU's main view by more than 1e-5 of their largest component (plus 1e-4): 0 means the main view's clip space.
  const auto off_main_view = [&](const Streamed& streamed) {
    UINT off = 0;
    for (UINT corner = 0; corner < CORNERS; ++corner) {
      float largest = 0.f;
      for (const float value : expected[corner]) largest = std::max(largest, std::abs(value));
      const float tolerance = 1e-4f + (1e-5f * largest);
      for (int component = 0; component < 4; ++component) {
        if (std::abs(streamed.positions[corner][component] - expected[corner][component]) > tolerance) {
          ++off;
          break;
        }
      }
    }
    return off;
  };

  // 1. The vertex shader alone with rt_core's buffer at b2: the main view's clip space.
  const Streamed reference = draw(solid, solid_alone.Get(), ryse_cascade.Get(), false);
  const UINT reference_off = off_main_view(reference);
  std::printf("0x7EB6531F streamed alone, rt_core's buffer at b2 (row 0): %u of %u corners off the main view\n", reference_off, CORNERS);
  CHECK(reference_off == 0);
  const UINT light_off = off_main_view(draw(solid, solid_alone.Get(), light_buffer.Get(), false));
  const UINT row_10_off = off_main_view(draw(solid, solid_alone.Get(), wh3_cascade.Get(), false));
  std::printf("CONTROL, the game's PER_FRAME (the light's) left at b2: %u corners off; rt_core's buffer at row 10: %u off (both must be > 0)\n",
              light_off, row_10_off);
  CHECK(light_off > 0);
  CHECK(row_10_off > 0);

  // 2. Through the geometry shaders: a triangle wholly outside kept bit for bit, any other collapsed onto its first corner.
  std::array<UINT, 2> kinds = {};  // outside, collapsed
  std::vector<bool> outside(TRIANGLES);
  for (UINT triangle = 0; triangle < TRIANGLES; ++triangle) {
    const auto& a = reference.positions[triangle * 3];
    const auto& b = reference.positions[(triangle * 3) + 1];
    const auto& c = reference.positions[(triangle * 3) + 2];
    const auto all = [&](auto test) { return test(a) && test(b) && test(c); };
    outside[triangle] = all([](const auto& p) { return p[3] <= 0.f; }) || all([](const auto& p) { return p[0] > p[3]; })
                        || all([](const auto& p) { return p[0] < -p[3]; }) || all([](const auto& p) { return p[1] > p[3]; })
                        || all([](const auto& p) { return p[1] < -p[3]; }) || all([](const auto& p) { return p[2] > p[3]; });
    ++kinds[outside[triangle] ? 0 : 1];
  }
  std::printf("triangles: %u wholly outside the main view, %u in it or across its edges\n", kinds[0], kinds[1]);
  CHECK(kinds[0] > TRIANGLES / 8 && kinds[1] > TRIANGLES / 8);
  // Corners not as the rule keeps them (and UV changed, with `uv`).
  const auto not_kept = [&](const char* name, const Streamed& streamed, bool uv) {
    UINT wrong = 0;
    UINT wrong_uv = 0;
    for (UINT corner = 0; corner < CORNERS; ++corner) {
      const UINT triangle = corner / 3;
      const auto& wanted = outside[triangle] ? reference.positions[corner] : reference.positions[triangle * 3];
      if (std::memcmp(streamed.positions[corner].data(), wanted.data(), sizeof(wanted)) != 0) ++wrong;
      if (uv && (streamed.uvs[corner][0] != vertices[corner].uv[0] || streamed.uvs[corner][1] != vertices[corner].uv[1])) ++wrong_uv;
    }
    std::printf("%s: %u corners not as the rule keeps them, %u UV wrong\n", name, wrong, wrong_uv);
    return wrong + wrong_uv;
  };
  CHECK(not_kept("0x7EB6531F through rt_outside_view", draw(solid, keep.Get(), ryse_cascade.Get(), false), false) == 0);
  CHECK(not_kept("0x48CE820A through rt_outside_view_uv", draw(cut_out, keep_uv.Get(), ryse_cascade.Get(), true), true) == 0);
  CHECK(not_kept("0x48CE820A through rt_outside_view (its signature a prefix)", draw(cut_out, keep.Get(), ryse_cascade.Get(), false), false) == 0);
  std::printf("CONTROL (must be > 0): ");
  CHECK(not_kept("0x7EB6531F streamed alone, nothing collapsed", reference, false) > 0);

  std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
