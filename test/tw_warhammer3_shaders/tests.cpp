// Warhammer III's shaders against the game's on WARP (src/games/twwarhammer3).
// tests.exe <the game's dumped csos (dump_shader_twwarhammer3_pack\cso)> <the add-on's built csos (build\twwarhammer3.include\embed)>
// 1. Casters the rays take as dynamic (rt_scene streams the game's vertex shader out and makes world positions of its
//    SV_Position with the frame's camera, RT_HOST.camera: the camera buffer's c10-c13 and its inverse): 03298FD5 (campaign
//    rigid texture blend), 1C3500F0 (siege vertex-push tentacles, against its own world output TEXCOORD1) and 70F4C9F9
//    (held since batch 3, the reference). Input layouts from each shader's signature (POSITION1-3 and TEXCOORD1 per instance,
//    as the game's vb1), 3 vertices x 6 instances of random positions and rows, c10-c13 a perspective view-projection, cb1 /
//    cb2 random (wind, pushes): world from SV_Position within 1 cm of rows x vertex (or the shader's world output). CONTROL:
//    another camera's inverse misses by metres. Without the game's t1 / t2, 1C3500F0's push is 0 here.
// 2. Reflection Quality: the water march EF6B7767 (the game's bytes) against ssr_game_march (ours, swapped in off 1) on a
//    512 x 256 scene: a camera 10 m over water looking 12 degrees down, posts and a far wall, the G-buffer's water flag and
//    8 levels of hierarchical depth. With b1 unbound, steps 0 and steps 100 ours gives the game's bits; 25 steps fewer hits,
//    400 at least as many. Again with every other texel of the hierarchical depth above the water near the camera (the march
//    steps texel by texel there): 100 steps end rays, 400 more hits. CONTROL: 25 steps differ from the game's.
// 3. Modded Auto Exposure: the game's auto exposure 1E448816 against ours (shaders/autoexposure00) on a 256 x 128 light of
//    random luma with 16 focal points, from a last brightness of 0.8 and of 0 (its first frame: 1): with the row off ours gives
//    the game's bits; with a fire (luma 500) on one focal point's cross the game's brightness falls, ours with the row on
//    (2 stops) stays within 5% of the frame without the fire. CONTROL: ours with the row on differs from the game's.
// 4. Bend / the rays over the kept maps: the sun mask E6BCBA8A's contact march off through the add-on's sss copy (flag 0 at
//    byte 28) = the game's with the flag 0. CONTROL: the game's flag 1 shadows pixels.
// 5. Torch shadows under the rays: the 4 light PS that read the game's point shadow map keep the map for a light the rays don't
//    trace and take the rays' shadow in place of it for one they do. CONTROL: their unshadowed twin differs from the map's light.
// 6. Ray-Traced Specular Occlusion (a third argument: an older build's csos): reflection00's cubes dimmed by t46 alone, not
//    by the AO too. CONTROL: the older build's take the AO.
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
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "../../src/games/twwarhammer3/shared.h"

using Microsoft::WRL::ComPtr;
using Matrix = std::array<double, 16>;  // rows acting on column vectors (the camera buffer's registers, row by row)

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

static std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static Matrix Multiply(const Matrix& a, const Matrix& b) {
  Matrix out = {};
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      for (int k = 0; k < 4; ++k) out[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
    }
  }
  return out;
}

static Matrix Inverse(Matrix m) {
  Matrix inverse = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  for (int column = 0; column < 4; ++column) {
    int pivot = column;
    for (int r = column + 1; r < 4; ++r) {
      if (std::fabs(m[r * 4 + column]) > std::fabs(m[pivot * 4 + column])) pivot = r;
    }
    for (int c = 0; c < 4; ++c) {
      std::swap(m[column * 4 + c], m[pivot * 4 + c]);
      std::swap(inverse[column * 4 + c], inverse[pivot * 4 + c]);
    }
    const double scale = 1.0 / m[column * 4 + column];
    for (int c = 0; c < 4; ++c) {
      m[column * 4 + c] *= scale;
      inverse[column * 4 + c] *= scale;
    }
    for (int r = 0; r < 4; ++r) {
      if (r == column) continue;
      const double factor = m[r * 4 + column];
      for (int c = 0; c < 4; ++c) {
        m[r * 4 + c] -= factor * m[column * 4 + c];
        inverse[r * 4 + c] -= factor * inverse[column * 4 + c];
      }
    }
  }
  return inverse;
}

static std::array<double, 4> Transform(const Matrix& m, const std::array<double, 4>& v) {
  std::array<double, 4> out = {};
  for (int r = 0; r < 4; ++r) {
    for (int k = 0; k < 4; ++k) out[r] += m[r * 4 + k] * v[k];
  }
  return out;
}

// The game's projection: depth 0 at near, 1 at far, view z forward.
static Matrix Projection(double vertical_fov, double aspect, double near_plane, double far_plane) {
  const double f = 1.0 / std::tan(0.5 * vertical_fov);
  return {f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, far_plane / (far_plane - near_plane), -far_plane * near_plane / (far_plane - near_plane), 0, 0, 1, 0};
}

// A camera at `eye` looking along +z turned by `yaw` and down by `pitch`.
static Matrix View(const std::array<double, 3>& eye, double yaw, double pitch) {
  const double cy = std::cos(yaw);
  const double sy = std::sin(yaw);
  const double cp = std::cos(pitch);
  const double sp = std::sin(pitch);
  const std::array<double, 3> right = {cy, 0, -sy};
  const std::array<double, 3> up = {sy * sp, cp, cy * sp};
  const std::array<double, 3> forward = {sy * cp, -sp, cy * cp};
  const auto dot = [&eye](const std::array<double, 3>& axis) { return axis[0] * eye[0] + axis[1] * eye[1] + axis[2] * eye[2]; };
  return {right[0], right[1], right[2], -dot(right), up[0], up[1], up[2], -dot(up), forward[0], forward[1], forward[2], -dot(forward), 0, 0, 0, 1};
}

static ComPtr<ID3D11Buffer> ConstantBuffer(const void* data, UINT size) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = (size + 15) & ~15u, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  std::vector<uint8_t> padded(desc.ByteWidth, 0);
  std::memcpy(padded.data(), data, size);
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = padded.data()};
  ComPtr<ID3D11Buffer> buffer;
  device->CreateBuffer(&desc, &initial, &buffer);
  return buffer;
}

// The game's camera buffer (b0 "camera", 66 registers, its matrices row by row) over `width` x `height` targets.
static std::array<float, 66 * 4> CameraBuffer(const std::array<double, 3>& eye, const Matrix& view, const Matrix& projection, float near_plane,
                                              float far_plane, UINT width, UINT height) {
  std::array<float, 66 * 4> camera = {};
  const auto put = [&camera](int base, const Matrix& m) {
    for (int k = 0; k < 16; ++k) camera[base * 4 + k] = static_cast<float>(m[k]);
  };
  const Matrix view_projection = Multiply(projection, view);
  for (int k = 0; k < 3; ++k) camera[k] = static_cast<float>(eye[k]);
  put(2, view);
  put(6, projection);
  put(10, view_projection);
  put(18, Inverse(view));
  put(26, Inverse(projection));
  put(30, Inverse(view_projection));
  camera[34 * 4] = near_plane;
  camera[34 * 4 + 1] = far_plane;
  for (int reg : {37, 39, 41}) {
    camera[reg * 4] = static_cast<float>(width);
    camera[reg * 4 + 1] = static_cast<float>(height);
    camera[reg * 4 + 2] = 1.f / static_cast<float>(width);
    camera[reg * 4 + 3] = 1.f / static_cast<float>(height);
  }
  return camera;
}

// ------------------------------------------------------------------------------------------------------------- casters
static void TestCaster(const std::string& pack, const std::string& spec) {
  const std::string hash = spec.substr(0, spec.find(':'));
  std::string world_semantic;
  UINT world_index = 0;
  if (const size_t colon = spec.find(':'); colon != std::string::npos) {
    const size_t second = spec.find(':', colon + 1);
    world_semantic = spec.substr(colon + 1, second - colon - 1);
    world_index = static_cast<UINT>(std::stoi(spec.substr(second + 1)));
  }
  const std::vector<uint8_t> code = ReadFile(pack + "\\0x" + hash + ".vs_5_0.cso");
  ComPtr<ID3D11ShaderReflection> reflection;
  CHECK(!code.empty() && SUCCEEDED(D3DReflect(code.data(), code.size(), IID_PPV_ARGS(&reflection))), "%s: no code", hash.c_str());
  if (reflection == nullptr) return;
  D3D11_SHADER_DESC shader_desc = {};
  reflection->GetDesc(&shader_desc);
  std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
  std::vector<std::string> names;
  names.reserve(64);
  UINT offset0 = 0;
  UINT offset1 = 0;
  int position_offset = -1;
  std::array<int, 3> row_offsets = {-1, -1, -1};
  for (UINT p = 0; p < shader_desc.InputParameters; ++p) {
    D3D11_SIGNATURE_PARAMETER_DESC param = {};
    reflection->GetInputParameterDesc(p, &param);
    if (param.SystemValueType != D3D_NAME_UNDEFINED) continue;
    names.emplace_back(param.SemanticName);
    const bool position = names.back() == "POSITION";
    const bool per_instance = (position && param.SemanticIndex >= 1) || (names.back() == "TEXCOORD" && param.SemanticIndex == 1);
    UINT& offset = per_instance ? offset1 : offset0;
    if (position && param.SemanticIndex == 0) position_offset = static_cast<int>(offset);
    if (position && param.SemanticIndex >= 1 && param.SemanticIndex <= 3) row_offsets[param.SemanticIndex - 1] = static_cast<int>(offset);
    const bool is_uint = param.ComponentType == D3D_REGISTER_COMPONENT_UINT32;
    elements.push_back({names.back().c_str(), param.SemanticIndex, is_uint ? DXGI_FORMAT_R32G32B32A32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT,
                        per_instance ? 1u : 0u, offset, per_instance ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA,
                        per_instance ? 1u : 0u});
    offset += 16;
  }
  ComPtr<ID3D11InputLayout> layout;
  CHECK(position_offset >= 0 && row_offsets[2] >= 0
            && SUCCEEDED(device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()), code.data(), code.size(), &layout)),
        "%s: no layout (position %d, rows %d %d %d)", hash.c_str(), position_offset, row_offsets[0], row_offsets[1], row_offsets[2]);
  if (layout == nullptr) return;
  constexpr UINT VERTICES = 3;
  constexpr UINT INSTANCES = 6;
  std::mt19937 random(12345);
  std::uniform_real_distribution<float> any(-1.f, 1.f);
  std::vector<float> vertex_data(VERTICES * offset0 / 4);
  for (float& value : vertex_data) value = any(random);
  std::vector<float> instance_data(INSTANCES * offset1 / 4);
  for (float& value : instance_data) value = any(random);
  constexpr std::array<std::array<float, 3>, INSTANCES> PLACES = {{{0.f, 0.f, 50.f}, {10.f, 2.f, 30.f}, {-20.f, 0.f, 80.f}, {20.f, 1.f, 120.f}, {0.f, -3.f, 15.f}, {5.f, 8.f, 60.f}}};
  for (UINT i = 0; i < INSTANCES; ++i) {
    float* instance = &instance_data[i * offset1 / 4];
    const float angle = 0.3f * static_cast<float>(i) + 0.1f;
    const float scale = 1.f + 0.1f * static_cast<float>(i);
    const std::array<std::array<float, 4>, 3> rows = {{{scale * std::cos(angle), 0.f, scale * std::sin(angle), PLACES[i][0]},
                                                       {0.f, scale, 0.f, PLACES[i][1]},
                                                       {-scale * std::sin(angle), 0.f, scale * std::cos(angle), PLACES[i][2]}}};
    for (int r = 0; r < 3; ++r) std::memcpy(instance + row_offsets[r] / 4, rows[r].data(), 16);
  }
  // The camera buffer: c10-c13 the view-projection (66 registers, as the game's), time 1.7 at c35.
  const Matrix projection = Projection(1.0472, 1.777, 1.0, 13000.0);
  const Matrix view_projection = Multiply(projection, View({1.0, 5.0, -10.0}, 0.2, 0.0));
  const Matrix other = Multiply(projection, View({1.0, 5.0, -10.0}, 0.25, 0.0));
  std::array<float, 66 * 4> camera = {};
  for (int k = 0; k < 16; ++k) camera[10 * 4 + k] = static_cast<float>(view_projection[k]);
  camera[35 * 4] = 1.7f;
  const ComPtr<ID3D11Buffer> camera_buffer = ConstantBuffer(camera.data(), sizeof(camera));
  D3D11_BUFFER_DESC vb_desc = {.ByteWidth = static_cast<UINT>(vertex_data.size() * 4), .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_VERTEX_BUFFER};
  D3D11_SUBRESOURCE_DATA vb_data = {.pSysMem = vertex_data.data()};
  ComPtr<ID3D11Buffer> vertices;
  device->CreateBuffer(&vb_desc, &vb_data, &vertices);
  vb_desc.ByteWidth = static_cast<UINT>(instance_data.size() * 4);
  vb_data.pSysMem = instance_data.data();
  ComPtr<ID3D11Buffer> instances;
  device->CreateBuffer(&vb_desc, &vb_data, &instances);

  std::vector<D3D11_SO_DECLARATION_ENTRY> declaration = {{.SemanticName = "SV_POSITION", .SemanticIndex = 0, .ComponentCount = 4}};
  if (!world_semantic.empty()) declaration.push_back({.SemanticName = world_semantic.c_str(), .SemanticIndex = world_index, .ComponentCount = 4});
  UINT so_stride = 16 * static_cast<UINT>(declaration.size());
  const UINT floats_per_vertex = so_stride / 4;
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11GeometryShader> gs;
  device->CreateVertexShader(code.data(), code.size(), nullptr, &vs);
  CHECK(SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(code.data(), code.size(), declaration.data(), static_cast<UINT>(declaration.size()),
                                                               &so_stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &gs)),
        "%s: no stream output", hash.c_str());
  if (gs == nullptr) return;
  D3D11_BUFFER_DESC so_desc = {.ByteWidth = 1 << 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_STREAM_OUTPUT};
  ComPtr<ID3D11Buffer> so;
  device->CreateBuffer(&so_desc, nullptr, &so);
  so_desc.Usage = D3D11_USAGE_STAGING;
  so_desc.BindFlags = 0;
  so_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  device->CreateBuffer(&so_desc, nullptr, &staging);
  const std::array<ID3D11Buffer*, 2> buffers = {vertices.Get(), instances.Get()};
  const std::array<UINT, 2> strides = {offset0, offset1};
  const std::array<UINT, 2> offsets = {0, 0};
  context->IASetVertexBuffers(0, 2, buffers.data(), strides.data(), offsets.data());
  context->IASetInputLayout(layout.Get());
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vs.Get(), nullptr, 0);
  // cb1 and cb2 (rigid_config, the material's: wind, push) random, so a shader that moves its vertices moves them.
  std::array<float, 64 * 4> random_constants = {};
  for (float& value : random_constants) value = any(random);
  const ComPtr<ID3D11Buffer> random_buffer = ConstantBuffer(random_constants.data(), sizeof(random_constants));
  const std::array<ID3D11Buffer*, 3> constant_buffers = {camera_buffer.Get(), random_buffer.Get(), random_buffer.Get()};
  context->VSSetConstantBuffers(0, 3, constant_buffers.data());
  context->GSSetShader(gs.Get(), nullptr, 0);
  context->PSSetShader(nullptr, nullptr, 0);
  UINT so_offset = 0;
  context->SOSetTargets(1, so.GetAddressOf(), &so_offset);
  context->DrawInstanced(VERTICES, INSTANCES, 0, 0);
  ID3D11Buffer* none = nullptr;
  context->SOSetTargets(1, &none, &so_offset);
  context->GSSetShader(nullptr, nullptr, 0);
  context->VSSetShader(nullptr, nullptr, 0);
  context->CopyResource(staging.Get(), so.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<float> clip(VERTICES * INSTANCES * floats_per_vertex);
  std::memcpy(clip.data(), mapped.pData, clip.size() * 4);
  context->Unmap(staging.Get(), 0);

  const auto worst = [&](const Matrix& camera_used) {
    const Matrix inverse = Inverse(camera_used);
    double error = 0.0;
    for (UINT i = 0; i < INSTANCES; ++i) {
      const float* instance = &instance_data[i * offset1 / 4];
      for (UINT v = 0; v < VERTICES; ++v) {
        const float* position = &vertex_data[v * offset0 / 4 + position_offset / 4];
        const float* c = &clip[(i * VERTICES + v) * floats_per_vertex];
        const auto world = Transform(inverse, {c[0], c[1], c[2], c[3]});
        for (int r = 0; r < 3; ++r) {
          const float* row = instance + row_offsets[r] / 4;
          const double expected = world_semantic.empty()
                                      ? double(row[0]) * position[0] + double(row[1]) * position[1] + double(row[2]) * position[2] + row[3]
                                      : double(c[4 + r]);
          error = std::max(error, std::fabs(world[r] / world[3] - expected));
        }
      }
    }
    return error;
  };
  const double error = worst(view_projection);
  const double control = worst(other);
  std::printf("caster %s: %zu inputs (vb0 %u, vb1 %u bytes), world from SV_Position against %s: largest error %.6f m; CONTROL another camera %.3f m\n",
              hash.c_str(), elements.size(), offset0, offset1, world_semantic.empty() ? "rows x vertex" : spec.c_str(), error, control);
  CHECK(error < 0.01, "%s: world from SV_Position %.6f m off", hash.c_str(), error);
  CHECK(control > 0.1, "%s: CONTROL another camera only %.6f m off", hash.c_str(), control);
}

// --------------------------------------------------------------------------------------------------------- the march
constexpr UINT MARCH_WIDTH = 512;
constexpr UINT MARCH_HEIGHT = 256;
constexpr UINT DEPTH_LEVELS = 8;
constexpr float NEAR_TEXEL = 0.0005f;

struct MarchScene {
  std::vector<float> normals;   // t0: the normal (two numbers in 0..1), rgba
  std::vector<float> colours;   // t1
  std::vector<float> surfaces;  // t2: metal, roughness, -, flags / 255
  std::vector<std::vector<float>> depth_levels;  // t4: nearest, farthest
  std::vector<bool> water;      // at the hierarchical depth's first level (all four pixels water)
  std::array<float, 66 * 4> camera = {};
};

static MarchScene BuildMarchScene() {
  MarchScene scene;
  const std::array<double, 3> eye = {0.0, 10.0, 0.0};
  const Matrix view = View(eye, 0.0, 0.2094);
  const Matrix projection = Projection(1.0472, static_cast<double>(MARCH_WIDTH) / MARCH_HEIGHT, 1.0, 2000.0);
  const Matrix view_projection = Multiply(projection, view);
  const Matrix inverse_view_projection = Inverse(view_projection);
  scene.camera = CameraBuffer(eye, view, projection, 1.f, 2000.f, MARCH_WIDTH, MARCH_HEIGHT);
  // Posts (x, z, width, height) and a wall at z 400, 80 m high; the water at y 0.
  std::mt19937 random(777);
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  std::vector<std::array<double, 4>> posts;
  for (int i = 0; i < 40; ++i) posts.push_back({-80.0 + 160.0 * uniform(random), 40.0 + 300.0 * uniform(random), 1.0 + 2.0 * uniform(random), 4.0 + 30.0 * uniform(random)});
  const size_t pixels = static_cast<size_t>(MARCH_WIDTH) * MARCH_HEIGHT;
  scene.normals.assign(pixels * 4, 0.5f);
  scene.colours.assign(pixels * 4, 0.9f);
  scene.surfaces.assign(pixels * 4, 0.f);
  std::vector<float> depth(pixels, 1.f);
  std::vector<bool> water_pixel(pixels, false);
  for (UINT y = 0; y < MARCH_HEIGHT; ++y) {
    for (UINT x = 0; x < MARCH_WIDTH; ++x) {
      const double ndc_x = (x + 0.5) / MARCH_WIDTH * 2.0 - 1.0;
      const double ndc_y = 1.0 - (y + 0.5) / MARCH_HEIGHT * 2.0;
      auto far_point = Transform(inverse_view_projection, {ndc_x, ndc_y, 1.0, 1.0});
      const std::array<double, 3> direction = {far_point[0] / far_point[3] - eye[0], far_point[1] / far_point[3] - eye[1], far_point[2] / far_point[3] - eye[2]};
      double nearest = 1e30;
      std::array<double, 3> normal = {0, 0, -1};
      bool is_water = false;
      if (direction[1] < 0.0) {
        nearest = -eye[1] / direction[1];
        normal = {0, 1, 0};
        is_water = true;
      }
      if (direction[2] > 0.0) {
        const double t = (400.0 - eye[2]) / direction[2];
        if (t < nearest && eye[1] + t * direction[1] <= 80.0) {
          nearest = t;
          normal = {0, 0, -1};
          is_water = false;
        }
      }
      for (const auto& post : posts) {
        const std::array<double, 3> low = {post[0], 0.0, post[1]};
        const std::array<double, 3> high = {post[0] + post[2], post[3], post[1] + post[2]};
        double enter = 0.0;
        double leave = 1e30;
        int axis_in = -1;
        bool miss = false;
        for (int a = 0; a < 3 && !miss; ++a) {
          if (std::fabs(direction[a]) < 1e-12) {
            miss = eye[a] < low[a] || eye[a] > high[a];
            continue;
          }
          double t0 = (low[a] - eye[a]) / direction[a];
          double t1 = (high[a] - eye[a]) / direction[a];
          if (t0 > t1) std::swap(t0, t1);
          if (t0 > enter) {
            enter = t0;
            axis_in = a;
          }
          leave = std::min(leave, t1);
          miss = enter > leave;
        }
        if (!miss && axis_in >= 0 && enter < nearest) {
          nearest = enter;
          normal = {0, 0, 0};
          normal[axis_in] = direction[axis_in] > 0 ? -1.0 : 1.0;
          is_water = false;
        }
      }
      const size_t i = static_cast<size_t>(y) * MARCH_WIDTH + x;
      if (nearest < 1e29) {
        const auto clip = Transform(view_projection, {eye[0] + nearest * direction[0], eye[1] + nearest * direction[1], eye[2] + nearest * direction[2], 1.0});
        depth[i] = static_cast<float>(clip[2] / clip[3]);
      }
      // The normal in view space, in the G-buffer's two numbers (decoded (2x s, 2y s, 2(x^2 + y^2) - 1), s = sqrt(1 - x^2 - y^2)).
      std::array<double, 3> v = {};
      for (int r = 0; r < 3; ++r) v[r] = view[r * 4] * normal[0] + view[r * 4 + 1] * normal[1] + view[r * 4 + 2] * normal[2];
      const double length_squared = std::clamp((v[2] + 1.0) / 2.0, 0.0, 0.999999);
      const double s = std::sqrt(1.0 - length_squared);
      scene.normals[i * 4] = static_cast<float>(0.5 * (v[0] / (2.0 * s)) + 0.5);
      scene.normals[i * 4 + 1] = static_cast<float>(0.5 * (v[1] / (2.0 * s)) + 0.5);
      scene.surfaces[i * 4] = 1.f;
      scene.surfaces[i * 4 + 1] = 0.1f;
      scene.surfaces[i * 4 + 3] = is_water ? 64.f / 255.f : 0.f;
      water_pixel[i] = is_water;
    }
  }
  // The first level: half the size, nearest and farthest of four pixels; then each level of the one below.
  UINT width = MARCH_WIDTH / 2;
  UINT height = MARCH_HEIGHT / 2;
  std::vector<float> level(static_cast<size_t>(width) * height * 2);
  scene.water.assign(static_cast<size_t>(width) * height, false);
  for (UINT y = 0; y < height; ++y) {
    for (UINT x = 0; x < width; ++x) {
      float nearest = 1.f;
      float farthest = 0.f;
      bool all_water = true;
      for (UINT k = 0; k < 4; ++k) {
        const size_t i = static_cast<size_t>(2 * y + k / 2) * MARCH_WIDTH + 2 * x + k % 2;
        nearest = std::min(nearest, depth[i]);
        farthest = std::max(farthest, depth[i]);
        all_water = all_water && water_pixel[i];
      }
      level[(static_cast<size_t>(y) * width + x) * 2] = nearest;
      level[(static_cast<size_t>(y) * width + x) * 2 + 1] = farthest;
      scene.water[static_cast<size_t>(y) * width + x] = all_water;
    }
  }
  scene.depth_levels.push_back(level);
  return scene;
}

static void BuildDepthLevels(MarchScene* scene) {
  scene->depth_levels.resize(1);
  UINT width = MARCH_WIDTH / 2;
  UINT height = MARCH_HEIGHT / 2;
  for (UINT l = 1; l < DEPTH_LEVELS; ++l) {
    const std::vector<float>& below = scene->depth_levels.back();
    const UINT next_width = width / 2;
    const UINT next_height = height / 2;
    std::vector<float> level(static_cast<size_t>(next_width) * next_height * 2);
    for (UINT y = 0; y < next_height; ++y) {
      for (UINT x = 0; x < next_width; ++x) {
        float nearest = 1.f;
        float farthest = 0.f;
        for (UINT k = 0; k < 4; ++k) {
          const size_t i = (static_cast<size_t>(2 * y + k / 2) * width + 2 * x + k % 2) * 2;
          nearest = std::min(nearest, below[i]);
          farthest = std::max(farthest, below[i + 1]);
        }
        level[(static_cast<size_t>(y) * next_width + x) * 2] = nearest;
        level[(static_cast<size_t>(y) * next_width + x) * 2 + 1] = farthest;
      }
    }
    scene->depth_levels.push_back(std::move(level));
    width = next_width;
    height = next_height;
  }
}

static ComPtr<ID3D11ShaderResourceView> PixelTexture(const std::vector<float>& rgba) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = MARCH_WIDTH, .Height = MARCH_HEIGHT, .MipLevels = 1, .ArraySize = 1,
                                     .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA data = {.pSysMem = rgba.data(), .SysMemPitch = MARCH_WIDTH * 16};
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, &data, &texture);
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(texture.Get(), nullptr, &view);
  return view;
}

// The march's t_rw_hit (x 1 a hit, y its weight), two floats a texel. `steps` empty: b1 unbound.
static std::vector<float> RunMarch(const std::vector<uint8_t>& code, const MarchScene& scene, std::optional<uint32_t> steps) {
  ComPtr<ID3D11ComputeShader> shader;
  CHECK(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &shader)), "march shader");
  if (shader == nullptr) return {};
  std::array<ComPtr<ID3D11ShaderResourceView>, 5> views = {PixelTexture(scene.normals), PixelTexture(scene.colours), PixelTexture(scene.surfaces),
                                                           PixelTexture(scene.colours)};
  const D3D11_TEXTURE2D_DESC depth_desc = {.Width = MARCH_WIDTH / 2, .Height = MARCH_HEIGHT / 2, .MipLevels = DEPTH_LEVELS, .ArraySize = 1,
                                           .Format = DXGI_FORMAT_R32G32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                           .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  std::array<D3D11_SUBRESOURCE_DATA, DEPTH_LEVELS> depth_data = {};
  for (UINT l = 0; l < DEPTH_LEVELS; ++l) depth_data[l] = {.pSysMem = scene.depth_levels[l].data(), .SysMemPitch = ((MARCH_WIDTH / 2) >> l) * 8};
  ComPtr<ID3D11Texture2D> depth;
  device->CreateTexture2D(&depth_desc, depth_data.data(), &depth);
  device->CreateShaderResourceView(depth.Get(), nullptr, &views[4]);
  const D3D11_TEXTURE2D_DESC hit_desc = {.Width = MARCH_WIDTH / 2, .Height = MARCH_HEIGHT / 2, .MipLevels = 1, .ArraySize = 1,
                                         .Format = DXGI_FORMAT_R32G32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                         .BindFlags = D3D11_BIND_UNORDERED_ACCESS};
  ComPtr<ID3D11Texture2D> hit;
  device->CreateTexture2D(&hit_desc, nullptr, &hit);
  ComPtr<ID3D11UnorderedAccessView> hit_view;
  device->CreateUnorderedAccessView(hit.Get(), nullptr, &hit_view);
  const ComPtr<ID3D11Buffer> camera = ConstantBuffer(scene.camera.data(), sizeof(scene.camera));
  const std::array<uint32_t, 8> flags = {0, 0, 0, 0, steps.value_or(0)};
  const ComPtr<ID3D11Buffer> flags_buffer = ConstantBuffer(flags.data(), sizeof(flags));
  std::array<ID3D11ShaderResourceView*, 5> raw = {};
  for (size_t i = 0; i < views.size(); ++i) raw[i] = views[i].Get();
  context->CSSetShader(shader.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 5, raw.data());
  context->CSSetUnorderedAccessViews(0, 1, hit_view.GetAddressOf(), nullptr);
  const std::array<ID3D11Buffer*, 2> buffers = {camera.Get(), steps ? flags_buffer.Get() : nullptr};
  context->CSSetConstantBuffers(0, 2, buffers.data());
  context->Dispatch(MARCH_WIDTH / 2 / 16, MARCH_HEIGHT / 2 / 16, 1);
  std::array<ID3D11ShaderResourceView*, 5> no_views = {};
  ID3D11UnorderedAccessView* no_output = nullptr;
  context->CSSetShaderResources(0, 5, no_views.data());
  context->CSSetUnorderedAccessViews(0, 1, &no_output, nullptr);
  D3D11_TEXTURE2D_DESC staging_desc = hit_desc;
  staging_desc.BindFlags = 0;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&staging_desc, nullptr, &staging);
  context->CopyResource(staging.Get(), hit.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<float> out(static_cast<size_t>(MARCH_WIDTH / 2) * (MARCH_HEIGHT / 2) * 2);
  for (UINT y = 0; y < MARCH_HEIGHT / 2; ++y) {
    std::memcpy(&out[static_cast<size_t>(y) * (MARCH_WIDTH / 2) * 2], static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, (MARCH_WIDTH / 2) * 8);
  }
  context->Unmap(staging.Get(), 0);
  return out;
}

static int Differ(const std::vector<float>& a, const std::vector<float>& b) {
  int texels = 0;
  for (size_t i = 0; i + 1 < std::min(a.size(), b.size()); i += 2) {
    texels += (std::memcmp(&a[i], &b[i], 8) != 0) ? 1 : 0;
  }
  return texels + static_cast<int>(std::max(a.size(), b.size()) - std::min(a.size(), b.size())) / 2;
}

static int Hits(const std::vector<float>& hit) {
  int count = 0;
  for (size_t i = 0; i < hit.size(); i += 2) count += (hit[i] == 1.f) ? 1 : 0;
  return count;
}

static void TestMarch(const std::string& pack, const std::string& built) {
  const std::vector<uint8_t> game = ReadFile(pack + "\\0xEF6B7767.cs_5_0.cso");
  const std::vector<uint8_t> ours = ReadFile(built + "\\ssr_game_march.cso");
  CHECK(!game.empty() && !ours.empty(), "march: no code (%zu, %zu bytes)", game.size(), ours.size());
  if (game.empty() || ours.empty()) return;
  for (const bool checkered : {false, true}) {
    MarchScene scene = BuildMarchScene();
    int water = 0;
    for (const bool w : scene.water) water += w ? 1 : 0;
    if (checkered) {
      // Every other texel that isn't water near the camera: rays above the water step texel by texel.
      std::vector<float>& first = scene.depth_levels[0];
      for (UINT y = 0; y < MARCH_HEIGHT / 2; ++y) {
        for (UINT x = 0; x < MARCH_WIDTH / 2; ++x) {
          const size_t i = static_cast<size_t>(y) * (MARCH_WIDTH / 2) + x;
          if (!scene.water[i] && (x + y) % 2 == 0) first[i * 2] = first[i * 2 + 1] = NEAR_TEXEL;
        }
      }
    }
    BuildDepthLevels(&scene);
    const char* name = checkered ? "checkered" : "posts";
    const auto game_hit = RunMarch(game, scene, std::nullopt);
    const auto unbound = RunMarch(ours, scene, std::nullopt);
    const auto zero = RunMarch(ours, scene, 0u);
    const auto hundred = RunMarch(ours, scene, 100u);
    const auto quarter = RunMarch(ours, scene, 25u);
    const auto four = RunMarch(ours, scene, 400u);
    const auto endless = RunMarch(ours, scene, 100000u);
    std::printf("march %s (%d water texels of %u): the game's %d hits; ours b1 unbound %d texels differ, steps 0 %d, steps 100 %d; hits at 25 / 100 / "
                "400 / 100000 steps %d / %d / %d / %d; CONTROL 25 steps: %d texels differ\n",
                name, water, (MARCH_WIDTH / 2) * (MARCH_HEIGHT / 2), Hits(game_hit), Differ(game_hit, unbound), Differ(game_hit, zero),
                Differ(game_hit, hundred), Hits(quarter), Hits(hundred), Hits(four), Hits(endless), Differ(game_hit, quarter));
    CHECK(Hits(game_hit) > 0, "march %s: no hit at all", name);
    CHECK(Differ(game_hit, unbound) == 0 && Differ(game_hit, zero) == 0 && Differ(game_hit, hundred) == 0, "march %s: not the game's bits", name);
    CHECK(Hits(quarter) < Hits(hundred), "march %s: 25 steps not fewer hits", name);
    CHECK(Differ(game_hit, quarter) > 0, "march %s: CONTROL 25 steps the game's", name);
    CHECK(Hits(four) >= Hits(hundred), "march %s: 400 steps fewer hits", name);
    if (checkered) CHECK(Hits(four) > Hits(hundred), "march %s: 400 steps no more hits", name);
  }
}

// ----------------------------------------------------------------------------------------------------- auto exposure
constexpr UINT LIGHT_WIDTH = 256;
constexpr UINT LIGHT_HEIGHT = 128;

// The brightness the auto exposure writes, from `previous`, over `light` (rgba), with the add-on's settings at b10.
static float RunExposure(const std::vector<uint8_t>& code, const std::vector<float>& light, float previous, float protection) {
  ComPtr<ID3D11ComputeShader> shader;
  CHECK(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &shader)), "auto exposure shader");
  if (shader == nullptr) return -1.f;
  const D3D11_TEXTURE2D_DESC desc = {.Width = LIGHT_WIDTH, .Height = LIGHT_HEIGHT, .MipLevels = 1, .ArraySize = 1,
                                     .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA data = {.pSysMem = light.data(), .SysMemPitch = LIGHT_WIDTH * 16};
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, &data, &texture);
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(texture.Get(), nullptr, &view);
  const D3D11_BUFFER_DESC output_desc = {.ByteWidth = 4, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                         .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = 4};
  const D3D11_SUBRESOURCE_DATA output_data = {.pSysMem = &previous};
  ComPtr<ID3D11Buffer> output;
  device->CreateBuffer(&output_desc, &output_data, &output);
  ComPtr<ID3D11UnorderedAccessView> output_view;
  device->CreateUnorderedAccessView(output.Get(), nullptr, &output_view);
  // tone_mapping: brightness 1, auto exposure on. Auto_exposure_cb: 16 focal points (a 4 x 4 grid), radius 8, target 2^-2,
  // adaptation 0.5 a second each way, correction +- 0.5, sensitivity 0.5, a second (the speed limit not reached).
  const std::array<float, 4> tone_mapping = {1.f, 0.f, std::bit_cast<float>(1), 0.f};
  std::array<float, 35 * 4> exposure = {};
  exposure[0] = std::bit_cast<float>(16u);
  for (int i = 0; i < 16; ++i) {
    exposure[(1 + i) * 4] = 0.125f + 0.25f * static_cast<float>(i % 4);
    exposure[(1 + i) * 4 + 1] = 0.125f + 0.25f * static_cast<float>(i / 4);
  }
  exposure[32 * 4 + 2] = std::bit_cast<float>(8u);
  exposure[32 * 4 + 3] = -2.f;
  exposure[33 * 4] = 0.5f;
  exposure[33 * 4 + 1] = 0.5f;
  exposure[33 * 4 + 2] = 0.5f;
  exposure[33 * 4 + 3] = 0.5f;
  exposure[34 * 4] = 1.f;
  ShaderInjectData injection = {};
  injection.auto_exposure_highlight_protection = protection;
  injection.auto_exposure_highlight_headroom_stops = 2.f;
  const std::array<ComPtr<ID3D11Buffer>, 3> buffers = {ConstantBuffer(tone_mapping.data(), sizeof(tone_mapping)),
                                                       ConstantBuffer(exposure.data(), sizeof(exposure)),
                                                       ConstantBuffer(&injection, sizeof(injection))};
  context->CSSetShader(shader.Get(), nullptr, 0);
  context->CSSetShaderResources(0, 1, view.GetAddressOf());
  context->CSSetUnorderedAccessViews(0, 1, output_view.GetAddressOf(), nullptr);
  context->CSSetConstantBuffers(0, 2, buffers[0].GetAddressOf());
  context->CSSetConstantBuffers(10, 1, buffers[2].GetAddressOf());
  context->Dispatch(1, 1, 1);
  ID3D11ShaderResourceView* no_view = nullptr;
  ID3D11UnorderedAccessView* no_output = nullptr;
  context->CSSetShaderResources(0, 1, &no_view);
  context->CSSetUnorderedAccessViews(0, 1, &no_output, nullptr);
  D3D11_BUFFER_DESC staging_desc = {.ByteWidth = 4, .Usage = D3D11_USAGE_STAGING, .CPUAccessFlags = D3D11_CPU_ACCESS_READ};
  ComPtr<ID3D11Buffer> staging;
  device->CreateBuffer(&staging_desc, nullptr, &staging);
  context->CopyResource(staging.Get(), output.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  float brightness = 0.f;
  std::memcpy(&brightness, mapped.pData, 4);
  context->Unmap(staging.Get(), 0);
  return brightness;
}

static void TestAutoExposure(const std::string& pack, const std::string& built) {
  const std::vector<uint8_t> game = ReadFile(pack + "\\0x1E448816.cs_5_0.cso");
  const std::vector<uint8_t> ours = ReadFile(built + "\\0x1E448816.cso");
  CHECK(!game.empty() && !ours.empty(), "auto exposure: no code (%zu, %zu bytes)", game.size(), ours.size());
  if (game.empty() || ours.empty()) return;
  std::mt19937 random(4242);
  std::uniform_real_distribution<float> luma(0.f, 0.6f);
  std::vector<float> light(static_cast<size_t>(LIGHT_WIDTH) * LIGHT_HEIGHT * 4);
  for (float& value : light) value = luma(random);
  int same = 0;
  int runs = 0;
  for (const float previous : {0.8f, 0.f, 1.3f}) {
    ++runs;
    const float game_brightness = RunExposure(game, light, previous, 0.f);
    const float ours_brightness = RunExposure(ours, light, previous, 0.f);
    same += std::memcmp(&game_brightness, &ours_brightness, 4) == 0 ? 1 : 0;
    std::printf("auto exposure from %.2f: the game's %.8f, ours off %.8f\n", previous, game_brightness, ours_brightness);
  }
  CHECK(same == runs, "auto exposure off: %d of %d not the game's bits", runs - same, runs);
  // A fire on the second focal point's cross (its row: x 88..104 at y 16).
  const float calm_game = RunExposure(game, light, 0.8f, 0.f);
  std::vector<float> fire = light;
  for (UINT x = 88; x <= 104; ++x) {
    for (int c = 0; c < 3; ++c) fire[(static_cast<size_t>(16) * LIGHT_WIDTH + x) * 4 + c] = 500.f;
  }
  const float fire_game = RunExposure(game, fire, 0.8f, 0.f);
  const float fire_ours = RunExposure(ours, fire, 0.8f, 1.f);
  const float calm_ours = RunExposure(ours, light, 0.8f, 1.f);
  std::printf("auto exposure from 0.80, without / with a fire: the game's %.5f / %.5f; ours on %.5f / %.5f\n", calm_game, fire_game,
              calm_ours, fire_ours);
  CHECK(fire_game < 0.95f * calm_game, "the fire doesn't darken the game's (%.5f against %.5f)", fire_game, calm_game);
  CHECK(std::fabs(fire_ours - calm_game) < 0.05f * calm_game, "ours on with the fire %.5f, the game's without %.5f", fire_ours, calm_game);
  CHECK(fire_ours != fire_game, "CONTROL: ours on with the fire is the game's");
}

// ------------------------------------------------------------------------------------- full-screen pixel shader passes
constexpr UINT PASS_WIDTH = 160;
constexpr UINT PASS_HEIGHT = 96;

// A camera 3 m over flat ground looking 20 degrees down, posts ahead (x, z, width, height) and a wall at z 40: the device depth of
// each pixel (1 where nothing is hit) and the camera buffer.
struct PassScene {
  std::vector<float> depth;
  std::array<float, 66 * 4> camera = {};
  Matrix view_projection = {};
  Matrix inverse_view_projection = {};
};

static PassScene BuildPassScene() {
  PassScene scene;
  const std::array<double, 3> eye = {0.0, 3.0, 0.0};
  const Matrix view = View(eye, 0.0, 0.35);
  const Matrix projection = Projection(1.0472, static_cast<double>(PASS_WIDTH) / PASS_HEIGHT, 0.5, 500.0);
  scene.view_projection = Multiply(projection, view);
  scene.inverse_view_projection = Inverse(scene.view_projection);
  scene.camera = CameraBuffer(eye, view, projection, 0.5f, 500.f, PASS_WIDTH, PASS_HEIGHT);
  constexpr std::array<std::array<double, 4>, 6> POSTS = {{{-3.0, 8.0, 0.6, 3.0}, {1.0, 10.0, 0.4, 2.0}, {3.5, 14.0, 1.0, 4.0},
                                                          {-1.5, 18.0, 0.5, 1.5}, {0.2, 6.0, 0.3, 0.8}, {-6.0, 25.0, 2.0, 6.0}}};
  scene.depth.assign(static_cast<size_t>(PASS_WIDTH) * PASS_HEIGHT, 1.f);
  for (UINT y = 0; y < PASS_HEIGHT; ++y) {
    for (UINT x = 0; x < PASS_WIDTH; ++x) {
      const double ndc_x = (x + 0.5) / PASS_WIDTH * 2.0 - 1.0;
      const double ndc_y = 1.0 - (y + 0.5) / PASS_HEIGHT * 2.0;
      const auto far_point = Transform(scene.inverse_view_projection, {ndc_x, ndc_y, 1.0, 1.0});
      const std::array<double, 3> direction = {far_point[0] / far_point[3] - eye[0], far_point[1] / far_point[3] - eye[1],
                                               far_point[2] / far_point[3] - eye[2]};
      double nearest = 1e30;
      if (direction[1] < 0.0) nearest = -eye[1] / direction[1];
      if (direction[2] > 0.0) nearest = std::min(nearest, (40.0 - eye[2]) / direction[2]);
      for (const auto& post : POSTS) {
        const std::array<double, 3> low = {post[0], 0.0, post[1]};
        const std::array<double, 3> high = {post[0] + post[2], post[3], post[1] + post[2]};
        double enter = 0.0;
        double leave = 1e30;
        for (int a = 0; a < 3; ++a) {
          double t0 = (low[a] - eye[a]) / direction[a];
          double t1 = (high[a] - eye[a]) / direction[a];
          if (t0 > t1) std::swap(t0, t1);
          enter = std::max(enter, t0);
          leave = std::min(leave, t1);
        }
        if (enter <= leave && enter > 0.0) nearest = std::min(nearest, enter);
      }
      if (nearest >= 1e29) continue;
      const auto clip = Transform(scene.view_projection, {eye[0] + nearest * direction[0], eye[1] + nearest * direction[1],
                                                          eye[2] + nearest * direction[2], 1.0});
      scene.depth[static_cast<size_t>(y) * PASS_WIDTH + x] = static_cast<float>(clip[2] / clip[3]);
    }
  }
  return scene;
}

static ComPtr<ID3D11ShaderResourceView> PassTexture(const std::vector<float>& rgba) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = PASS_WIDTH, .Height = PASS_HEIGHT, .MipLevels = 1, .ArraySize = 1,
                                     .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA data = {.pSysMem = rgba.data(), .SysMemPitch = PASS_WIDTH * 16};
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, &data, &texture);
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(texture.Get(), nullptr, &view);
  return view;
}

// A slices x size x size R32_FLOAT array (the game's point light shadow map: a cube's faces).
static ComPtr<ID3D11ShaderResourceView> ArrayTexture(UINT size, UINT slices, const std::vector<float>& values) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = size, .Height = size, .MipLevels = 1, .ArraySize = slices, .Format = DXGI_FORMAT_R32_FLOAT,
                                     .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  std::vector<D3D11_SUBRESOURCE_DATA> data(slices);
  for (UINT s = 0; s < slices; ++s) data[s] = {.pSysMem = &values[static_cast<size_t>(s) * size * size], .SysMemPitch = size * 4};
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, data.data(), &texture);
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(texture.Get(), nullptr, &view);
  return view;
}

// A full-screen draw of `code` (a pixel shader) into an R32G32B32A32_FLOAT target: its views at t0.., constants at b0..,
// samplers at s0..; the vertex shader's TEXCOORD0 the uv, POSITION1 / POSITION2 `light` (a point light's colour, its place and
// reach). All four channels back.
static std::vector<float> RunPass(const std::vector<uint8_t>& code, const std::vector<ID3D11ShaderResourceView*>& views,
                                  const std::vector<ID3D11Buffer*>& constants, const std::vector<ID3D11SamplerState*>& samplers,
                                  const std::array<float, 8>& light) {
  static ComPtr<ID3D11VertexShader> full_screen;
  if (!full_screen) {
    const char* source =
        "cbuffer light_vs : register(b0) { float4 colour; float4 place; };"
        "struct O { float4 position : SV_Position; float4 uv : TEXCOORD0; float4 colour : POSITION1; float4 place : POSITION2; };"
        "O main(uint id : SV_VertexID) { O o; float2 uv = float2((id << 1) & 2, id & 2);"
        "o.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = float4(uv, 0, 0); o.colour = colour; o.place = place; return o; }";
    ComPtr<ID3DBlob> blob;
    if (SUCCEEDED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &blob, nullptr))) {
      device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &full_screen);
    }
  }
  ComPtr<ID3D11PixelShader> shader;
  CHECK(full_screen && SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, &shader)), "pass shader");
  if (shader == nullptr) return {};
  const D3D11_TEXTURE2D_DESC target_desc = {.Width = PASS_WIDTH, .Height = PASS_HEIGHT, .MipLevels = 1, .ArraySize = 1,
                                            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                            .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target;
  device->CreateTexture2D(&target_desc, nullptr, &target);
  ComPtr<ID3D11RenderTargetView> target_view;
  device->CreateRenderTargetView(target.Get(), nullptr, &target_view);
  constexpr std::array<float, 4> CLEAR = {-7.f, -7.f, -7.f, -7.f};
  context->ClearRenderTargetView(target_view.Get(), CLEAR.data());
  const ComPtr<ID3D11Buffer> light_buffer = ConstantBuffer(light.data(), sizeof(light));
  const D3D11_VIEWPORT viewport = {.Width = static_cast<float>(PASS_WIDTH), .Height = static_cast<float>(PASS_HEIGHT), .MaxDepth = 1.f};
  context->ClearState();
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(full_screen.Get(), nullptr, 0);
  context->VSSetConstantBuffers(0, 1, light_buffer.GetAddressOf());
  context->PSSetShader(shader.Get(), nullptr, 0);
  context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  context->PSSetConstantBuffers(0, static_cast<UINT>(constants.size()), constants.data());
  context->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
  context->RSSetViewports(1, &viewport);
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  context->Draw(3, 0);
  context->ClearState();
  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.BindFlags = 0;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&staging_desc, nullptr, &staging);
  context->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<float> out(static_cast<size_t>(PASS_WIDTH) * PASS_HEIGHT * 4);
  for (UINT y = 0; y < PASS_HEIGHT; ++y) {
    std::memcpy(&out[static_cast<size_t>(y) * PASS_WIDTH * 4], static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, PASS_WIDTH * 16);
  }
  context->Unmap(staging.Get(), 0);
  return out;
}

// Values (floats) whose bits differ.
static int DifferentValues(const std::vector<float>& a, const std::vector<float>& b) {
  int values = 0;
  for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) values += (std::memcmp(&a[i], &b[i], 4) != 0) ? 1 : 0;
  return values + static_cast<int>(std::max(a.size(), b.size()) - std::min(a.size(), b.size()));
}

// 4. The sun mask E6BCBA8A (the game's bytes) under Bend or the rays over the kept maps: its contact march goes, the rest stays.
//    The scene of BuildPassScene in a low sun from behind the posts; the mask's other shadows lit (the cascades' and the terrain's
//    flags 0, no clouds) so only the march can shadow. The add-on's route (twwarhammer3/addon.cpp traced_lights: the game's sss
//    constants with the flag 1 copied by CopyResource, a zero written at byte 28 by CopySubresourceRegion) against the game with the
//    flag 0 written on the CPU: 0 values apart. CONTROL: the flag 1 as the game binds it shadows pixels.
static void TestSunMaskMarch(const std::string& pack) {
  const std::vector<uint8_t> game = ReadFile(pack + "\\0xE6BCBA8A.ps_5_0.cso");
  CHECK(!game.empty(), "sun mask: no code");
  if (game.empty()) return;
  const PassScene scene = BuildPassScene();
  const size_t pixels = static_cast<size_t>(PASS_WIDTH) * PASS_HEIGHT;
  std::vector<float> normals(pixels * 4, 0.5f);
  std::vector<float> materials(pixels * 4, 0.f);
  std::vector<float> depth(pixels * 4, 0.f);
  for (size_t i = 0; i < pixels; ++i) depth[i * 4] = scene.depth[i];
  const auto normals_view = PassTexture(normals);
  const auto materials_view = PassTexture(materials);
  const auto depth_view = PassTexture(depth);
  // shadowmap_PS (40 registers): one cascade split; lighting_VS_PS (30): the sun's direction at c0.yzw, cloud scale 1 at c12.w.
  std::array<float, 40 * 4> shadow_map = {};
  shadow_map[33 * 4 + 3] = 1.f;
  std::array<float, 30 * 4> lighting = {};
  const std::array<float, 3> sun = {0.25f, -0.35f, -0.9f};
  const float sun_length = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
  for (int k = 0; k < 3; ++k) lighting[1 + k] = sun[k] / sun_length;
  lighting[12 * 4 + 3] = 1.f;
  // sss: thickness 0.3 m from 5 m on, +2% a metre; the march (c1.w) as given; the cascades (c2.x) and terrain (c2.y) off.
  const auto sss = [](uint32_t march) {
    std::array<float, 3 * 4> values = {0.3f, 5.f, 0.02f, 0.f, 0.f, 0.f, 0.f, std::bit_cast<float>(march), 0.f, 0.f, 0.f, 0.f};
    return values;
  };
  const ComPtr<ID3D11Buffer> camera = ConstantBuffer(scene.camera.data(), sizeof(scene.camera));
  const ComPtr<ID3D11Buffer> shadow_map_buffer = ConstantBuffer(shadow_map.data(), sizeof(shadow_map));
  const ComPtr<ID3D11Buffer> lighting_buffer = ConstantBuffer(lighting.data(), sizeof(lighting));
  const auto without = sss(0u);
  const auto with = sss(1u);
  const ComPtr<ID3D11Buffer> march_off = ConstantBuffer(without.data(), sizeof(without));
  const ComPtr<ID3D11Buffer> march_on = ConstantBuffer(with.data(), sizeof(with));
  // The add-on's copy of the game's buffer, the flag from a buffer of zeros.
  D3D11_BUFFER_DESC desc = {};
  march_on->GetDesc(&desc);
  const D3D11_BUFFER_DESC copy_desc = {.ByteWidth = desc.ByteWidth, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  ComPtr<ID3D11Buffer> copied;
  device->CreateBuffer(&copy_desc, nullptr, &copied);
  constexpr std::array<uint32_t, 4> ZEROS = {};
  const D3D11_BUFFER_DESC zeros_desc = {.ByteWidth = sizeof(ZEROS), .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA zeros_data = {.pSysMem = ZEROS.data()};
  ComPtr<ID3D11Buffer> zeros;
  device->CreateBuffer(&zeros_desc, &zeros_data, &zeros);
  context->CopyResource(copied.Get(), march_on.Get());
  const D3D11_BOX flag = {.left = 0, .top = 0, .front = 0, .right = 4, .bottom = 1, .back = 1};
  context->CopySubresourceRegion(copied.Get(), 0, 28, 0, 0, zeros.Get(), 0, &flag);
  const D3D11_SAMPLER_DESC comparison_desc = {.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                              .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                              .ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL, .MaxLOD = D3D11_FLOAT32_MAX};
  const D3D11_SAMPLER_DESC linear_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
                                          .AddressV = D3D11_TEXTURE_ADDRESS_WRAP, .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
                                          .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> comparison;
  ComPtr<ID3D11SamplerState> linear;
  device->CreateSamplerState(&comparison_desc, &comparison);
  device->CreateSamplerState(&linear_desc, &linear);
  const std::vector<ID3D11ShaderResourceView*> views = {nullptr, nullptr, nullptr, nullptr, normals_view.Get(), materials_view.Get(), depth_view.Get(), nullptr};
  const auto run = [&](ID3D11Buffer* sss_buffer) {
    return RunPass(game, views, {camera.Get(), shadow_map_buffer.Get(), lighting_buffer.Get(), sss_buffer},
                   {comparison.Get(), linear.Get(), linear.Get()}, {});
  };
  const auto reference = run(march_off.Get());
  const auto ours = run(copied.Get());
  const auto game_march = run(march_on.Get());
  int shadowed = 0;
  int lit = 0;
  for (size_t i = 0; i < pixels; ++i) {
    shadowed += (game_march[i * 4] < 0.999f) ? 1 : 0;
    lit += (reference[i * 4] == 1.f) ? 1 : 0;
  }
  std::printf("sun mask: march off through the add-on's copy against the game's with the flag 0: %d values apart; the game's flag 0 lit at %d of %zu "
              "pixels; CONTROL the game's march on: %d pixels shadowed, %d values apart\n",
              DifferentValues(reference, ours), lit, pixels, shadowed, DifferentValues(reference, game_march));
  CHECK(DifferentValues(reference, ours) == 0, "sun mask: the copy's march off is not the game's flag 0");
  CHECK(shadowed > 0 && DifferentValues(reference, game_march) > 0, "sun mask: CONTROL the march shadows nothing");
}

// 5. The light PS that read the game's point shadow map under the rays (twwarhammer3/addon.cpp traced_lights, their copies through
//    rt_local_lights.hlsli RtLocalLightTraced): the scene of BuildPassScene, a light 4 m ahead at 1.5 m, reach 9 m, G-buffer
//    channels 1 - 3 (and the aux one) fixed noise, the depth the scene's, a 32 x 32 x 6 map of random depths; soft edges 0 and 1
//    (the two branches). The game's shadowed PS with its whole map lit (a 1 x 1 x 6 map at FLT_MAX: every compare passed) = the
//    game's unshadowed twin bit for bit. Ours (within 1e-5 of the value): nothing at t42, or the rays tracing only a light 1 m away
//    = the game's shadowed PS with its map (an untraced light keeps the game's shadow); the rays tracing this light, reached = the
//    game's twin (the rays' shadow in place of the map's), blocked = no light, a shadow ratio of 0.5 (Torch and Fire Shadows from
//    the Light Bounce) = half the twin's. Our unshadowed twins within 1e-5 of the game's. CONTROL: the twin (what the 11-16 route
//    ran for every light under the rays) differs from the game's shadowed PS with its map.
static void TestPointShadowTwins(const std::string& pack, const std::string& built) {
  struct Twin {
    const char* shadowed;
    const char* twin;
    UINT map_slot;  // the shadowed PS's map: after the G-buffer channels (the aux one at t4)
  };
  constexpr std::array<Twin, 4> TWINS = {{{"B2453C88", "8AD6BFD4", 4}, {"1315B4F2", "EBF0B0A0", 5}, {"BE7249F1", "2240F035", 4},
                                         {"EE766574", "F551453C", 5}}};
  const PassScene scene = BuildPassScene();
  const size_t pixels = static_cast<size_t>(PASS_WIDTH) * PASS_HEIGHT;
  std::mt19937 random(2468);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  std::array<ComPtr<ID3D11ShaderResourceView>, 5> gbuffer;
  for (size_t channel = 0; channel < gbuffer.size(); ++channel) {
    std::vector<float> values(pixels * 4);
    for (float& value : values) value = unit(random);
    if (channel == 3) {
      for (size_t i = 0; i < pixels; ++i) values[i * 4] = scene.depth[i];
    }
    gbuffer[channel] = PassTexture(values);
  }
  std::vector<float> lit_values(6, std::numeric_limits<float>::max());
  const auto lit_map = ArrayTexture(1, 6, lit_values);
  std::vector<float> random_values(32 * 32 * 6);
  for (float& value : random_values) value = 0.9f + 0.1f * unit(random);
  const auto random_map = ArrayTexture(32, 6, random_values);
  const ComPtr<ID3D11Buffer> camera = ConstantBuffer(scene.camera.data(), sizeof(scene.camera));
  constexpr std::array<float, 4> VIEWPORT = {0.f, 0.f, 1.f, 1.f};
  const ComPtr<ID3D11Buffer> dynamic_light = ConstantBuffer(VIEWPORT.data(), sizeof(VIEWPORT));
  const D3D11_SAMPLER_DESC comparison_desc = {.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                              .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                              .ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL, .MaxLOD = D3D11_FLOAT32_MAX};
  const D3D11_SAMPLER_DESC point_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                         .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                         .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX};
  ComPtr<ID3D11SamplerState> comparison;
  ComPtr<ID3D11SamplerState> point;
  device->CreateSamplerState(&comparison_desc, &comparison);
  device->CreateSamplerState(&point_desc, &point);
  // The light: its colour, its place and reach.
  constexpr std::array<float, 8> LIGHT = {1.f, 0.7f, 0.4f, 0.f, 0.5f, 1.5f, 7.f, 9.f};
  // rt_local_lights.hlsli's t42 (every pixel the same word) and b9 (one traced light at `place`; w 1: the word is a shadow ratio).
  const auto mask_view = [&](uint32_t word) {
    const std::vector<uint32_t> words(pixels, word);
    const D3D11_TEXTURE2D_DESC desc = {.Width = PASS_WIDTH, .Height = PASS_HEIGHT, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_UINT,
                                       .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    const D3D11_SUBRESOURCE_DATA data = {.pSysMem = words.data(), .SysMemPitch = PASS_WIDTH * 4};
    ComPtr<ID3D11Texture2D> texture;
    device->CreateTexture2D(&desc, &data, &texture);
    ComPtr<ID3D11ShaderResourceView> view;
    device->CreateShaderResourceView(texture.Get(), nullptr, &view);
    return view;
  };
  const auto lights_buffer = [](const std::array<float, 3>& place, bool ratio) {
    std::array<uint32_t, (128 + 1 + 128) * 4> values = {};
    for (int k = 0; k < 3; ++k) values[k] = std::bit_cast<uint32_t>(place[k]);
    values[3] = std::bit_cast<uint32_t>(9.f);
    values[128 * 4] = 1u;
    values[128 * 4 + 3] = ratio ? 1u : 0u;
    for (int k = 0; k < 3; ++k) values[129 * 4 + k] = std::bit_cast<uint32_t>(1.f);
    return ConstantBuffer(values.data(), sizeof(values));
  };
  const ComPtr<ID3D11Buffer> this_light = lights_buffer({LIGHT[4], LIGHT[5], LIGHT[6]}, false);
  const ComPtr<ID3D11Buffer> other_light = lights_buffer({LIGHT[4] + 1.f, LIGHT[5], LIGHT[6]}, false);
  const ComPtr<ID3D11Buffer> this_light_ratio = lights_buffer({LIGHT[4], LIGHT[5], LIGHT[6]}, true);
  const auto reached_mask = mask_view(1u);
  const auto blocked_mask = mask_view(0u);
  const auto half_mask = mask_view(std::bit_cast<uint32_t>(0.5f));
  // Our copies are decompiles: a few values round in the last bits. The largest difference, relative to the value.
  const auto apart = [](const std::vector<float>& a, const std::vector<float>& b, float scale) {
    if (a.size() != b.size()) return std::numeric_limits<float>::infinity();
    float largest = 0.f;
    for (size_t i = 0; i < a.size(); ++i) {
      const float expected = ((i % 4) == 3) ? b[i] : (b[i] * scale);
      largest = std::max(largest, std::abs(a[i] - expected) / std::max(std::abs(expected), 1e-3f));
    }
    return largest;
  };
  for (const auto& twin : TWINS) {
    const std::vector<uint8_t> shadowed = ReadFile(pack + "\\0x" + twin.shadowed + ".ps_5_0.cso");
    const std::vector<uint8_t> game_twin = ReadFile(pack + "\\0x" + twin.twin + ".ps_5_0.cso");
    const std::vector<uint8_t> ours_shadowed = ReadFile(built + "\\0x" + twin.shadowed + ".cso");
    const std::vector<uint8_t> ours_twin = ReadFile(built + "\\0x" + twin.twin + ".cso");
    CHECK(!shadowed.empty() && !game_twin.empty() && !ours_shadowed.empty() && !ours_twin.empty(), "point shadows %s: no code", twin.shadowed);
    if (shadowed.empty() || game_twin.empty() || ours_shadowed.empty() || ours_twin.empty()) continue;
    for (const float soft_edges : {0.f, 1.f}) {
      // pointlight_shadowmap_cb: a 32-texel map, bias 0.02, soft edges, the cube's depth 10 cm to 10 m, light width 5 cm, sample bias 1.
      const std::array<float, 12> shadow_constants = {32.f, 32.f, 1.f / 32.f, 1.f / 32.f, 0.02f, soft_edges, 1.0101f, -0.10101f, 0.05f, 1.f, 0.f, 0.f};
      const ComPtr<ID3D11Buffer> shadow_buffer = ConstantBuffer(shadow_constants.data(), sizeof(shadow_constants));
      std::vector<ID3D11ShaderResourceView*> views;
      for (UINT slot = 0; slot < twin.map_slot; ++slot) views.push_back(gbuffer[slot].Get());
      const std::vector<ID3D11Buffer*> constants = {camera.Get(), dynamic_light.Get(), shadow_buffer.Get()};
      const std::vector<ID3D11SamplerState*> samplers = {comparison.Get(), point.Get()};
      const auto twin_light = RunPass(game_twin, views, constants, samplers, LIGHT);
      const auto ours_twin_light = RunPass(ours_twin, views, constants, samplers, LIGHT);
      views.push_back(lit_map.Get());
      const auto lit_light = RunPass(shadowed, views, constants, samplers, LIGHT);
      views.back() = random_map.Get();
      const auto shadowed_light = RunPass(shadowed, views, constants, samplers, LIGHT);
      const auto ours_untraced = RunPass(ours_shadowed, views, constants, samplers, LIGHT);
      // Ours with the rays' mask at t42 and their lights at b9.
      const auto traced = [&](ID3D11ShaderResourceView* mask, ID3D11Buffer* lights) {
        std::vector<ID3D11ShaderResourceView*> traced_views = views;
        traced_views.resize(43, nullptr);
        traced_views[42] = mask;
        std::vector<ID3D11Buffer*> traced_constants = constants;
        traced_constants.resize(10, nullptr);
        traced_constants[9] = lights;
        return RunPass(ours_shadowed, traced_views, traced_constants, samplers, LIGHT);
      };
      const auto ours_other = traced(reached_mask.Get(), other_light.Get());
      const auto ours_reached = traced(reached_mask.Get(), this_light.Get());
      const auto ours_blocked = traced(blocked_mask.Get(), this_light.Get());
      const auto ours_half = traced(half_mask.Get(), this_light_ratio.Get());
      int lighted = 0;
      int shadowed_pixels = 0;
      int blocked_lit = 0;
      for (size_t i = 0; i < pixels; ++i) {
        const bool twin_lit = twin_light[i * 4] > 0.f || twin_light[i * 4 + 1] > 0.f || twin_light[i * 4 + 2] > 0.f;
        lighted += twin_lit ? 1 : 0;
        shadowed_pixels += (twin_lit && shadowed_light[i * 4] < twin_light[i * 4]) ? 1 : 0;
        blocked_lit += (ours_blocked[i * 4] != 0.f || ours_blocked[i * 4 + 1] != 0.f || ours_blocked[i * 4 + 2] != 0.f || ours_blocked[i * 4 + 3] != 1.f) ? 1 : 0;
      }
      const float untraced_apart = apart(ours_untraced, shadowed_light, 1.f);
      const float other_apart = apart(ours_other, shadowed_light, 1.f);
      const float reached_apart = apart(ours_reached, twin_light, 1.f);
      const float half_apart = apart(ours_half, twin_light, 0.5f);
      const float twin_apart = apart(ours_twin_light, twin_light, 1.f);
      std::printf("point shadows %s (twin %s), soft edges %.0f: %d of %zu pixels lit by the twin, %d darker with the game's map; the game's with "
                  "its map lit %d values apart from the twin; ours (largest apart of the value): untraced %.2g and another light traced %.2g "
                  "from the game's with its map, traced reached %.2g from the twin, blocked %d pixels lit, ratio 0.5 %.2g from half the twin; "
                  "our twin %.2g; CONTROL the twin under the rays: %d values apart from the game's with its map\n",
                  twin.shadowed, twin.twin, soft_edges, lighted, pixels, shadowed_pixels, DifferentValues(twin_light, lit_light), untraced_apart,
                  other_apart, reached_apart, blocked_lit, half_apart, twin_apart, DifferentValues(twin_light, shadowed_light));
      CHECK(lighted > 0, "point shadows %s: the light reaches nothing", twin.shadowed);
      CHECK(DifferentValues(twin_light, lit_light) == 0, "point shadows %s: lit map not the twin", twin.shadowed);
      CHECK(untraced_apart < 1e-5f, "point shadows %s: ours untraced not the game's map", twin.shadowed);
      CHECK(other_apart < 1e-5f, "point shadows %s: ours with another light traced not the game's map", twin.shadowed);
      CHECK(reached_apart < 1e-5f, "point shadows %s: ours traced and reached not the twin", twin.shadowed);
      CHECK(blocked_lit == 0, "point shadows %s: ours traced and blocked still lit", twin.shadowed);
      CHECK(half_apart < 1e-5f, "point shadows %s: ours at ratio 0.5 not half the twin", twin.shadowed);
      CHECK(twin_apart < 1e-5f, "point shadows %s: our twin not the game's", twin.twin);
      CHECK(shadowed_pixels > 0 && DifferentValues(twin_light, shadowed_light) > 0, "point shadows %s: CONTROL the twin is the game's with its map",
            twin.shadowed);
    }
  }
}

// 6. Ray-Traced Specular Occlusion in reflection00 (89AF6FF5, the cubes off water): the resolve's output stands over the sun
//    light's cubes (with the AO), so the cubes a frame shows = out(t46) - out(t46 = 0). The scene of BuildPassScene, every
//    surface flagged (not water), one random cube, random G-buffer, no rays (t47 unbound), the AO (t1) random 0.2 - 1 or white.
//    A: t46 unbound, ours = the older build (`older`: the csos before this fix) bit for bit. B: t46 random 0.2 - 1: the shown
//    cubes with the random AO within 1e-5 of those with the white AO (one occlusion: t46). C: at the white AO ours = the older
//    build's within 1e-5. CONTROL: the older build's shown cubes with the random AO differ from its white-AO ones.
static void TestReflectionOcclusion(const std::string& built, const std::string& older) {
  const std::vector<uint8_t> ours = ReadFile(built + "\\0x89AF6FF5.cso");
  const std::vector<uint8_t> before = ReadFile(older + "\\0x89AF6FF5.cso");
  CHECK(!ours.empty() && !before.empty(), "reflection occlusion: no code");
  if (ours.empty() || before.empty()) return;
  const PassScene scene = BuildPassScene();
  const size_t pixels = static_cast<size_t>(PASS_WIDTH) * PASS_HEIGHT;
  std::mt19937 random(1357);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  const auto noise = [&](float low, float high) {
    std::vector<float> values(pixels * 4);
    for (float& value : values) value = low + (high - low) * unit(random);
    return values;
  };
  std::vector<float> material = noise(0.2f, 0.9f);
  std::vector<float> depth(pixels * 4, 0.f);
  for (size_t i = 0; i < pixels; ++i) {
    material[i * 4 + 3] = 1.f / 255.f;  // the flags: bit 0 (the resolve's surfaces), not water
    depth[i * 4] = scene.depth[i];
  }
  std::vector<float> hits(pixels * 4, 1000.f);
  std::vector<float> radiance = noise(0.f, 2.f);
  for (size_t i = 0; i < pixels; ++i) radiance[i * 4 + 3] = 1.f;
  const std::array<ComPtr<ID3D11ShaderResourceView>, 7> scene_views = {PassTexture(noise(0.3f, 0.7f)), PassTexture(noise(0.f, 1.f)),
                                                                       PassTexture(material), PassTexture(depth), PassTexture(noise(0.f, 1.f)),
                                                                       PassTexture(hits), PassTexture(radiance)};
  const auto random_ao = PassTexture(noise(0.2f, 1.f));
  const auto white_ao = PassTexture(std::vector<float>(pixels * 4, 1.f));
  const auto random_occlusion = PassTexture(noise(0.2f, 1.f));
  const auto closed_occlusion = PassTexture(std::vector<float>(pixels * 4, 0.f));
  // t0: one 8 x 8 cube of random light.
  std::vector<float> cube(8 * 8 * 4 * 6);
  for (float& value : cube) value = 0.2f + 0.8f * unit(random);
  const D3D11_TEXTURE2D_DESC cube_desc = {.Width = 8, .Height = 8, .MipLevels = 1, .ArraySize = 6, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                          .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                                          .MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE};
  std::array<D3D11_SUBRESOURCE_DATA, 6> faces = {};
  for (size_t face = 0; face < faces.size(); ++face) faces[face] = {.pSysMem = &cube[face * 8 * 8 * 4], .SysMemPitch = 8 * 16};
  ComPtr<ID3D11Texture2D> cube_texture;
  device->CreateTexture2D(&cube_desc, faces.data(), &cube_texture);
  D3D11_SHADER_RESOURCE_VIEW_DESC cube_view_desc = {.Format = cube_desc.Format, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY};
  cube_view_desc.TextureCubeArray = {.MostDetailedMip = 0, .MipLevels = 1, .First2DArrayFace = 0, .NumCubes = 1};
  ComPtr<ID3D11ShaderResourceView> cube_view;
  device->CreateShaderResourceView(cube_texture.Get(), &cube_view_desc, &cube_view);
  // environment_PS: cube 0 at weight 1; cb_ssao: no overscan; lighting_VS_PS: the sun, no spherical harmonics, cloud scale 1.
  std::array<float, 48 * 4> environment = {};
  environment[0] = 1.f;
  environment[15 * 4 + 1] = 1.f;
  constexpr std::array<float, 4> SSAO = {1.f, 1.f, 0.f, 0.f};
  std::array<float, 30 * 4> lighting = {};
  const std::array<float, 3> sun = {0.3f, -0.8f, 0.5f};
  const float sun_length = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
  for (int k = 0; k < 3; ++k) lighting[1 + k] = sun[k] / sun_length;
  lighting[4] = 2.f;
  lighting[5] = 1.8f;
  lighting[6] = 1.5f;
  lighting[12 * 4 + 3] = 1.f;
  lighting[18 * 4 + 3] = 1.f;
  lighting[29 * 4] = 0.2f;
  lighting[29 * 4 + 2] = 1.f;
  lighting[29 * 4 + 3] = 1.f;
  const std::array<ComPtr<ID3D11Buffer>, 4> constants = {ConstantBuffer(environment.data(), sizeof(environment)),
                                                         ConstantBuffer(scene.camera.data(), sizeof(scene.camera)),
                                                         ConstantBuffer(SSAO.data(), sizeof(SSAO)), ConstantBuffer(lighting.data(), sizeof(lighting))};
  const D3D11_SAMPLER_DESC linear_desc = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                          .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                          .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX};
  D3D11_SAMPLER_DESC point_desc = linear_desc;
  point_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  ComPtr<ID3D11SamplerState> linear;
  ComPtr<ID3D11SamplerState> point;
  device->CreateSamplerState(&linear_desc, &linear);
  device->CreateSamplerState(&point_desc, &point);
  const std::array<ID3D11SamplerState*, 5> samplers = {linear.Get(), linear.Get(), linear.Get(), point.Get(), linear.Get()};
  const D3D11_TEXTURE2D_DESC out_desc = {.Width = PASS_WIDTH, .Height = PASS_HEIGHT, .MipLevels = 1, .ArraySize = 1,
                                         .Format = DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT,
                                         .BindFlags = D3D11_BIND_UNORDERED_ACCESS};
  ComPtr<ID3D11Texture2D> out;
  device->CreateTexture2D(&out_desc, nullptr, &out);
  ComPtr<ID3D11UnorderedAccessView> out_view;
  device->CreateUnorderedAccessView(out.Get(), nullptr, &out_view);
  D3D11_TEXTURE2D_DESC staging_desc = out_desc;
  staging_desc.BindFlags = 0;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&staging_desc, nullptr, &staging);
  // The resolve's output (rgb per pixel) with this AO and t46 (nullptr: unbound).
  const auto run = [&](const std::vector<uint8_t>& code, ID3D11ShaderResourceView* ao, ID3D11ShaderResourceView* occlusion) {
    ComPtr<ID3D11ComputeShader> shader;
    CHECK(SUCCEEDED(device->CreateComputeShader(code.data(), code.size(), nullptr, &shader)), "reflection shader");
    std::vector<float> rgb(pixels * 3, 0.f);
    if (shader == nullptr) return rgb;
    std::array<ID3D11ShaderResourceView*, 47> views = {cube_view.Get(), ao};
    for (size_t k = 0; k < scene_views.size(); ++k) views[5 + k] = scene_views[k].Get();
    views[46] = occlusion;
    constexpr std::array<float, 4> CLEAR = {-7.f, -7.f, -7.f, -7.f};
    context->ClearState();
    context->ClearUnorderedAccessViewFloat(out_view.Get(), CLEAR.data());
    context->CSSetShader(shader.Get(), nullptr, 0);
    context->CSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    const std::array<ID3D11Buffer*, 4> raw_constants = {constants[0].Get(), constants[1].Get(), constants[2].Get(), constants[3].Get()};
    context->CSSetConstantBuffers(0, static_cast<UINT>(raw_constants.size()), raw_constants.data());
    context->CSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
    context->CSSetUnorderedAccessViews(0, 1, out_view.GetAddressOf(), nullptr);
    context->Dispatch(PASS_WIDTH / 8, PASS_HEIGHT / 8, 1);
    context->ClearState();
    context->CopyResource(staging.Get(), out.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    for (UINT y = 0; y < PASS_HEIGHT; ++y) {
      const auto* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch);
      for (UINT x = 0; x < PASS_WIDTH; ++x) {
        for (int c = 0; c < 3; ++c) rgb[(static_cast<size_t>(y) * PASS_WIDTH + x) * 3 + c] = row[x * 4 + c];
      }
    }
    context->Unmap(staging.Get(), 0);
    return rgb;
  };
  // The cubes a frame shows: the output over the sun light's cubes (= minus the output with t46 0).
  const auto shown = [&](const std::vector<uint8_t>& code, ID3D11ShaderResourceView* ao) {
    std::vector<float> cubes = run(code, ao, random_occlusion.Get());
    const std::vector<float> closed = run(code, ao, closed_occlusion.Get());
    for (size_t i = 0; i < cubes.size(); ++i) cubes[i] -= closed[i];
    return cubes;
  };
  // The largest difference of a against b relative to b, and the values more than 1e-3 apart.
  const auto apart = [](const std::vector<float>& a, const std::vector<float>& b) {
    float largest = 0.f;
    int values = 0;
    for (size_t i = 0; i < a.size(); ++i) {
      const float relative = std::abs(a[i] - b[i]) / std::max(std::abs(b[i]), 1e-3f);
      largest = std::max(largest, relative);
      values += (relative > 1e-3f) ? 1 : 0;
    }
    return std::pair{largest, values};
  };
  const int unbound_apart = DifferentValues(run(ours, random_ao.Get(), nullptr), run(before, random_ao.Get(), nullptr));
  const auto ours_white = shown(ours, white_ao.Get());
  const auto [one_largest, one_values] = apart(shown(ours, random_ao.Get()), ours_white);
  const auto [white_largest, white_values] = apart(ours_white, shown(before, white_ao.Get()));
  const auto [control_largest, control_values] = apart(shown(before, random_ao.Get()), shown(before, white_ao.Get()));
  int reached = 0;
  for (size_t i = 0; i < pixels; ++i) reached += (ours_white[i * 3] > 1e-3f || ours_white[i * 3 + 1] > 1e-3f || ours_white[i * 3 + 2] > 1e-3f) ? 1 : 0;
  std::printf("reflection occlusion: %d of %zu pixels show cubes; A t46 unbound %d values apart from the older build; B the random AO "
              "against the white largest %.2g of the value (%d values over 1e-3); C the white AO against the older build %.2g (%d); "
              "CONTROL the older build's random AO against its white %.2g (%d values over 1e-3)\n",
              reached, pixels, unbound_apart, one_largest, one_values, white_largest, white_values, control_largest, control_values);
  CHECK(reached > static_cast<int>(pixels) / 2, "reflection occlusion: the cubes reach %d pixels", reached);
  CHECK(unbound_apart == 0, "reflection occlusion: A without t46 not the older build");
  CHECK(one_largest < 1e-5f, "reflection occlusion: B the AO still dims the cubes t46 dims");
  CHECK(white_largest < 1e-5f, "reflection occlusion: C the white AO not the older build's");
  CHECK(control_values > 0, "reflection occlusion: CONTROL the older build's AO dims nothing");
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: tests <the game's csos> <the add-on's built csos> [<the older build's csos>]\n");
    return 2;
  }
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) {
    std::printf("no WARP device\n");
    return 2;
  }
  for (const char* spec : {"03298FD5", "1C3500F0:TEXCOORD:1", "70F4C9F9"}) TestCaster(argv[1], spec);
  TestMarch(argv[1], argv[2]);
  TestAutoExposure(argv[1], argv[2]);
  TestSunMaskMarch(argv[1]);
  TestPointShadowTwins(argv[1], argv[2]);
  if (argc > 3) TestReflectionOcclusion(argv[2], argv[3]);
  std::printf("%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
