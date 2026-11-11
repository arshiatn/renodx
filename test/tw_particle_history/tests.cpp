// Smooth Half-Size Particles (twattila/particle_history.hpp and its resolve), Warhammer III's composite
// (twwarhammer3/vfx_particles.hpp) and Attila's Particles at Half Resolution (twattila/particle_layer.hpp) on WARP, with the
// addons' own shaders: built once a game, PARTICLE_TEST_WH3 against build\twwarhammer3.include, else Attila against
// build32\twattila.include. tests.exe [<Warhammer III's shader dump folder: 43606BB8's bytes>] [--time]
// Both games, the resolve on a 64 x 32 layer of a 128 x 64 render (a flat wall 20 m away, the camera looking at it):
// 1. A spark flipping between two neighbouring texels every frame: both average it (over 8 frames their spread at most 25% of
//    their mean, the mean within 5% of half the spark).
// 2. A spark moving 4 texels a frame: at least 90% of it where it is, at most 5% where it was.
// 3. Smoke fixed on the wall while the camera pans 0.37 texels a frame: its edge less than half a texel from this frame's.
// 4. A 100 m camera jump, a 20 m move that takes all of it off screen, a frame left out, a restart: this frame's layer to the
//    bit (and a control without any of them: the history used).
// Warhammer III (the game's composite 43606BB8 from the dump against ours, both through vfx_particles.hpp's callbacks):
// 5. Flat depth, a first frame, a uniform half zeroth moment: ours gives the game's to 1e-5.
// 6. A near box in front of smoke the game drew into the half layer behind it (its depth the farthest of each 2 x 2): the box's
//    pixels get under 1% of it, where the game's gives the pixels next to the edge half of it; away from the edge the game's.
// 7. Rows off: the game's shader stays bound, nothing captured. The composite with a writable depth target on the depth the
//    upsample reads: the game's composite (one log line); read-only: ours.
// Attila (particle_layer.hpp's callbacks with mock ReShade objects):
// 8. Over, premultiplied and added particles through the layer: within one R11G11B10 rounding of the blend math away from the
//    quads' edges; a subtract or destination-colour blend stays at full size (one log line), and the frame's later draws.
// 9. At 66% render scale (42 x 31 of 64 x 48): the layer's depth is the farthest of each 2 x 2 by the CPU's model, the
//    composite writes only the render's part.
// 10. The layer's camera is the game's but c30 (the layer's size); the game's state after each draw; a static scene with Smooth
//     Half-Size Particles as without (its first frame to the bit); a layer no reader takes stops the row (one log line) until
//     it goes off and on; a particle draw with an output-stage UAV bound stays at full size, and at the layer's reader such a
//     UAV is bound again after the composite with its counter kept.
// --time: on the hardware GPU at 4K, the passes' time (median of 60 frames).
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <numeric>
#include <random>
#include <span>
#include <string>
#include <vector>
#ifdef PARTICLE_TEST_WH3
#include "src/games/twwarhammer3/vfx_particles.hpp"
#else
#include "src/games/twattila/particle_layer.hpp"
#endif
#include "test/tw_particle_history/mock_reshade.hpp"

using Microsoft::WRL::ComPtr;
using Float4 = std::array<float, 4>;
using Matrix = std::array<std::array<float, 4>, 4>;  // m[row][column], column vectors: clip = m x position
namespace action = renodx::utils::command_action;
namespace frame_history = twattila::frame_history;
namespace particle_history = twattila::particle_history;
using DrawContext = action::CommandContext<action::DrawArguments>;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (false)
#define REQUIRE(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

static std::mutex log_mutex;
static std::vector<std::string> log_lines;
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* message) {
  const std::unique_lock lock(log_mutex);
  log_lines.emplace_back(message);
}
static size_t LogCount(const char* text) {
  const std::unique_lock lock(log_mutex);
  return std::ranges::count_if(log_lines, [&](const std::string& line) { return line.find(text) != std::string::npos; });
}

static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static ComPtr<ID3D11VertexShader> full_screen_vs;
static ComPtr<ID3D11PixelShader> copy_ps;

constexpr char FULL_SCREEN_VS[] = R"(
struct Out { float4 position : SV_Position; float4 colour : COLOR0; float2 uv : TEXCOORD0; };
Out main(uint id : SV_VertexID) {
  Out o;
  const float2 uv = float2((id << 1) & 2, id & 2);
  o.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  o.colour = 1;
  o.uv = uv;
  return o;
})";
constexpr char COPY_PS[] = R"(
Texture2D<float4> source : register(t0);
float4 main(float4 position : SV_Position) : SV_Target { return source.Load(int3(position.xy, 0)); })";

static ComPtr<ID3DBlob> Compile(const char* source, const char* target) {
  ComPtr<ID3DBlob> code;
  ComPtr<ID3DBlob> errors;
  if (FAILED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                        code.GetAddressOf(), errors.GetAddressOf()))) {
    std::printf("compile: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
    std::exit(1);
  }
  return code;
}
static ComPtr<ID3D11VertexShader> VertexShader(const char* source) {
  const auto code = Compile(source, "vs_5_0");
  ComPtr<ID3D11VertexShader> shader;
  REQUIRE(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf())));
  return shader;
}
static ComPtr<ID3D11PixelShader> PixelShader(const char* source) {
  const auto code = Compile(source, "ps_5_0");
  ComPtr<ID3D11PixelShader> shader;
  REQUIRE(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf())));
  return shader;
}

static void MakeDevice(D3D_DRIVER_TYPE type) {
  device.Reset();
  context.Reset();
  REQUIRE(SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr,
                                      context.GetAddressOf())));
  full_screen_vs = VertexShader(FULL_SCREEN_VS);
  copy_ps = PixelShader(COPY_PS);
}

// `shader` over `width` x `height` of `target` with `source` at t0, the state put back after.
static void DrawWith(ID3D11PixelShader* shader, ID3D11RenderTargetView* target, UINT width, UINT height, ID3D11ShaderResourceView* source) {
  const particle_history::DrawState kept(context.Get());
  context->OMSetRenderTargets(1, &target, nullptr);
  const D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
  context->RSSetViewports(1, &viewport);
  context->RSSetState(nullptr);
  context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
  context->OMSetDepthStencilState(nullptr, 0);
  context->IASetInputLayout(nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(full_screen_vs.Get(), nullptr, 0);
  context->GSSetShader(nullptr, nullptr, 0);
  context->PSSetShader(shader, nullptr, 0);
  context->PSSetShaderResources(0, 1, &source);
  context->Draw(3, 0);
}

static D3D11_TEXTURE2D_DESC DescOf(ID3D11ShaderResourceView* view) {
  ComPtr<ID3D11Resource> resource;
  view->GetResource(resource.GetAddressOf());
  ComPtr<ID3D11Texture2D> texture;
  resource.As(&texture);
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  return desc;
}

// Every texel of `view` as floats (through a copy into RGBA32F).
static std::vector<Float4> Read(ID3D11ShaderResourceView* view) {
  const D3D11_TEXTURE2D_DESC desc = DescOf(view);
  frame_history::Target target;
  REQUIRE(frame_history::MakeTarget(device.Get(), desc.Width, desc.Height, DXGI_FORMAT_R32G32B32A32_FLOAT, &target));
  DrawWith(copy_ps.Get(), target.rtv.Get(), desc.Width, desc.Height, view);
  D3D11_TEXTURE2D_DESC staging_desc = {};
  target.texture->GetDesc(&staging_desc);
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, staging.GetAddressOf())));
  context->CopyResource(staging.Get(), target.texture.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<Float4> values(static_cast<size_t>(desc.Width) * desc.Height);
  for (UINT y = 0; y < desc.Height; ++y) {
    std::memcpy(&values[static_cast<size_t>(y) * desc.Width], static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                desc.Width * sizeof(Float4));
  }
  context->Unmap(staging.Get(), 0);
  return values;
}

// A texture of `format` holding `values` (drawn from RGBA32F: the format's own rounding).
static frame_history::Target Upload(DXGI_FORMAT format, UINT width, UINT height, const std::vector<Float4>& values) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                     .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA data = {.pSysMem = values.data(), .SysMemPitch = width * static_cast<UINT>(sizeof(Float4))};
  ComPtr<ID3D11Texture2D> source;
  ComPtr<ID3D11ShaderResourceView> source_view;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, &data, source.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(source.Get(), nullptr, source_view.GetAddressOf())));
  frame_history::Target target;
  REQUIRE(frame_history::MakeTarget(device.Get(), width, height, format, &target));
  DrawWith(copy_ps.Get(), target.rtv.Get(), width, height, source_view.Get());
  return target;
}

static std::vector<Float4> Expand(const std::vector<float>& values) {
  std::vector<Float4> expanded(values.size());
  for (size_t index = 0; index < values.size(); ++index) expanded[index] = {values[index], 0.f, 0.f, 1.f};
  return expanded;
}

struct Depth {
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11DepthStencilView> dsv;
  ComPtr<ID3D11ShaderResourceView> srv;
};
static Depth MakeDepth(UINT width, UINT height, const std::vector<float>& values) {
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32_TYPELESS,
                                     .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT,
                                     .BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE};
  const D3D11_SUBRESOURCE_DATA data = {.pSysMem = values.data(), .SysMemPitch = width * static_cast<UINT>(sizeof(float))};
  Depth depth;
  REQUIRE(SUCCEEDED(device->CreateTexture2D(&desc, &data, depth.texture.GetAddressOf())));
  const D3D11_DEPTH_STENCIL_VIEW_DESC dsv = {.Format = DXGI_FORMAT_D32_FLOAT, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D};
  const D3D11_SHADER_RESOURCE_VIEW_DESC srv = {.Format = DXGI_FORMAT_R32_FLOAT, .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
                                               .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1}};
  REQUIRE(SUCCEEDED(device->CreateDepthStencilView(depth.texture.Get(), &dsv, depth.dsv.GetAddressOf())));
  REQUIRE(SUCCEEDED(device->CreateShaderResourceView(depth.texture.Get(), &srv, depth.srv.GetAddressOf())));
  return depth;
}

static ComPtr<ID3D11Buffer> MakeBuffer(const void* data, UINT size) {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = size, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data};
  ComPtr<ID3D11Buffer> buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &initial, buffer.GetAddressOf())));
  return buffer;
}

// A structured buffer's UAV with a counter (the game's output-stage UAV in the tests), and its counter read back.
static ComPtr<ID3D11UnorderedAccessView> MakeCounted() {
  const D3D11_BUFFER_DESC desc = {.ByteWidth = 64 * 4, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_UNORDERED_ACCESS,
                                  .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, .StructureByteStride = 4};
  ComPtr<ID3D11Buffer> buffer;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, nullptr, buffer.GetAddressOf())));
  const D3D11_UNORDERED_ACCESS_VIEW_DESC view = {.Format = DXGI_FORMAT_UNKNOWN, .ViewDimension = D3D11_UAV_DIMENSION_BUFFER,
                                                 .Buffer = {.FirstElement = 0, .NumElements = 64, .Flags = D3D11_BUFFER_UAV_FLAG_COUNTER}};
  ComPtr<ID3D11UnorderedAccessView> uav;
  REQUIRE(SUCCEEDED(device->CreateUnorderedAccessView(buffer.Get(), &view, uav.GetAddressOf())));
  return uav;
}
static std::vector<uint8_t> ReadBuffer(ID3D11Buffer* buffer);
static uint32_t CounterOf(ID3D11UnorderedAccessView* uav) {
  const std::array<uint32_t, 4> nothing = {};
  const D3D11_BUFFER_DESC desc = {.ByteWidth = 16, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_CONSTANT_BUFFER};
  const D3D11_SUBRESOURCE_DATA initial = {.pSysMem = nothing.data()};
  ComPtr<ID3D11Buffer> count;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, &initial, count.GetAddressOf())));
  context->CopyStructureCount(count.Get(), 0, uav);
  const auto bytes = ReadBuffer(count.Get());
  uint32_t value = 0;
  std::memcpy(&value, bytes.data(), sizeof(value));
  return value;
}
// The output-stage UAV at `slot` now.
static ComPtr<ID3D11UnorderedAccessView> BoundOutputView(UINT slot) {
  ComPtr<ID3D11UnorderedAccessView> view;
  context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, slot, 1, view.GetAddressOf());
  return view;
}

static std::vector<uint8_t> ReadBuffer(ID3D11Buffer* buffer) {
  D3D11_BUFFER_DESC desc = {};
  buffer->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING;
  desc.BindFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  REQUIRE(SUCCEEDED(device->CreateBuffer(&desc, nullptr, staging.GetAddressOf())));
  context->CopyResource(staging.Get(), buffer);
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  REQUIRE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
  std::vector<uint8_t> bytes(desc.ByteWidth);
  std::memcpy(bytes.data(), mapped.pData, desc.ByteWidth);
  context->Unmap(staging.Get(), 0);
  return bytes;
}

// ---- The camera: at (x, 0, 0) looking down +z (x right, y up), 60 degrees high, near 0.5, far 2000 (depth 0 near, 1 far).
#ifdef PARTICLE_TEST_WH3
constexpr UINT CAMERA_ROWS = 48;
constexpr UINT VIEW_ROW = 2, PROJECTION_ROW = 6, VIEW_PROJECTION_ROW = 10, INV_VIEW_ROW = 18, INV_PROJECTION_ROW = 26;
constexpr UINT SCREEN_SIZE_ROW = 37, VIEWPORT_ROW = 39;
#else
constexpr UINT CAMERA_ROWS = 36;
constexpr UINT VIEW_ROW = 1, PROJECTION_ROW = 5, VIEW_PROJECTION_ROW = 9, INV_VIEW_ROW = 13, INV_PROJECTION_ROW = 17;
constexpr UINT SCREEN_SIZE_ROW = 27, VIEWPORT_ROW = 29, RENDER_TARGET_ROW = 30;
#endif
constexpr float NEAR_PLANE = 0.5f;
constexpr float FAR_PLANE = 2000.f;
constexpr float DEPTH_A = FAR_PLANE / (FAR_PLANE - NEAR_PLANE);
constexpr float DEPTH_B = -NEAR_PLANE * FAR_PLANE / (FAR_PLANE - NEAR_PLANE);
static float TanHalf() { return std::tan(30.f * 3.14159265f / 180.f); }
static float DepthAt(float distance) { return DEPTH_A + DEPTH_B / distance; }

struct CameraSetup {
  float x = 0.f;
  float render_width = 0.f;
  float render_height = 0.f;
  float display_width = 0.f;
  float display_height = 0.f;
};

static std::vector<float> CameraValues(const CameraSetup& setup) {
  std::vector<float> values(CAMERA_ROWS * 4, 0.f);
  const float b = 1.f / TanHalf();
  const float a = b * setup.render_height / setup.render_width;
  const Matrix projection = {{{a, 0, 0, 0}, {0, b, 0, 0}, {0, 0, DEPTH_A, DEPTH_B}, {0, 0, 1, 0}}};
  const Matrix inverse_projection = {{{1 / a, 0, 0, 0}, {0, 1 / b, 0, 0}, {0, 0, 0, 1}, {0, 0, 1 / DEPTH_B, -DEPTH_A / DEPTH_B}}};
  const Matrix view = {{{1, 0, 0, -setup.x}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  const Matrix inverse_view = {{{1, 0, 0, setup.x}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  Matrix view_projection = {};
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      for (int k = 0; k < 4; ++k) view_projection[row][column] += projection[row][k] * view[k][column];
    }
  }
  const auto put = [&](UINT first, const Matrix& matrix) {
    for (UINT row = 0; row < 4; ++row) {
      for (UINT column = 0; column < 4; ++column) values[(first + row) * 4 + column] = matrix[row][column];
    }
  };
  values[0] = setup.x;
  put(VIEW_ROW, view);
  put(PROJECTION_ROW, projection);
  put(VIEW_PROJECTION_ROW, view_projection);
  put(INV_VIEW_ROW, inverse_view);
  put(INV_PROJECTION_ROW, inverse_projection);
  const auto size = [&](UINT row, float width, float height) {
    values[row * 4 + 0] = width;
    values[row * 4 + 1] = height;
    values[row * 4 + 2] = 1.f / width;
    values[row * 4 + 3] = 1.f / height;
  };
  size(SCREEN_SIZE_ROW, setup.display_width, setup.display_height);
  size(VIEWPORT_ROW, setup.render_width, setup.render_height);
#ifndef PARTICLE_TEST_WH3
  size(RENDER_TARGET_ROW, setup.display_width, setup.display_height);
#endif
  return values;
}

static ComPtr<ID3D11Buffer> MakeCamera(const CameraSetup& setup) {
  const auto values = CameraValues(setup);
  return MakeBuffer(values.data(), static_cast<UINT>(values.size() * sizeof(float)));
}
static void SetCamera(ID3D11Buffer* camera, const CameraSetup& setup) {
  const auto values = CameraValues(setup);
  context->UpdateSubresource(camera, 0, nullptr, values.data(), 0, 0);
}

// ---- The resolve on its own (particle_history::Resolve), a 64 x 32 layer of a 128 x 64 render.
constexpr UINT LAYER_WIDTH = 64;
constexpr UINT LAYER_HEIGHT = 32;
constexpr size_t LAYER_TEXELS = static_cast<size_t>(LAYER_WIDTH) * LAYER_HEIGHT;
struct LayerValue {
  float luminance = 0.f;
  float coverage = 0.f;
};
struct ResolveRig {
  particle_history::History history;
  ComPtr<ID3D11Buffer> camera = MakeCamera({.render_width = 128, .render_height = 64, .display_width = 128, .display_height = 64});
};
struct Resolved {
  std::vector<Float4> colour;
  std::vector<Float4> extra;
  std::vector<Float4> input;
  std::vector<Float4> input_optical_depth;
};

static Resolved ResolveFrame(ResolveRig* rig, const std::vector<LayerValue>& values, float camera_x, float wall, uint64_t frame, bool restart) {
  std::vector<Float4> colours(LAYER_TEXELS);
  std::vector<Float4> optical_depths(LAYER_TEXELS);
  for (size_t index = 0; index < LAYER_TEXELS; ++index) {
    const float luminance = values[index].luminance;
    const float coverage = values[index].coverage;
#ifdef PARTICLE_TEST_WH3
    colours[index] = {luminance, luminance, luminance, coverage};
    optical_depths[index] = {-std::log(1.f - coverage), 0.f, 0.f, 1.f};
#else
    colours[index] = {luminance, luminance, luminance, 1.f - coverage};
    optical_depths[index] = {0.f, 0.f, 0.f, 1.f};
#endif
  }
  const auto colour = Upload(DXGI_FORMAT_R16G16B16A16_FLOAT, LAYER_WIDTH, LAYER_HEIGHT, colours);
  const auto optical_depth = Upload(DXGI_FORMAT_R32_FLOAT, LAYER_WIDTH, LAYER_HEIGHT, optical_depths);
  const auto depth = Upload(DXGI_FORMAT_R32_FLOAT, LAYER_WIDTH, LAYER_HEIGHT, Expand(std::vector<float>(LAYER_TEXELS, DepthAt(wall))));
  SetCamera(rig->camera.Get(), {.x = camera_x, .render_width = 128, .render_height = 64, .display_width = 128, .display_height = 64});
#ifdef PARTICLE_TEST_WH3
  ID3D11ShaderResourceView* layer_optical_depth = optical_depth.srv.Get();
#else
  ID3D11ShaderResourceView* layer_optical_depth = nullptr;
#endif
  const auto smoothed = particle_history::Resolve(context.Get(),
                                                  {.colour = colour.srv.Get(),
                                                   .optical_depth = layer_optical_depth,
                                                   .depth = depth.srv.Get(),
                                                   .camera = rig->camera.Get(),
                                                   .viewport = {0.f, 0.f, static_cast<float>(LAYER_WIDTH), static_cast<float>(LAYER_HEIGHT), 0.f, 1.f}},
                                                  frame, restart, &rig->history);
  REQUIRE(smoothed[0] != nullptr && smoothed[1] != nullptr);
  return {Read(smoothed[0]), Read(smoothed[1]), Read(colour.srv.Get()), Read(optical_depth.srv.Get())};
}

static void TestResolve() {
  constexpr size_t ROW = 16 * LAYER_WIDTH;
  // 1. The flipping spark.
  {
    ResolveRig rig;
    std::array<std::vector<float>, 2> seen;
    for (uint64_t frame = 0; frame < 40; ++frame) {
      std::vector<LayerValue> values(LAYER_TEXELS);
      values[ROW + 20 + (frame & 1)] = {10.f, 0.5f};
      const auto out = ResolveFrame(&rig, values, 0.f, 20.f, frame, frame == 0);
      if (frame >= 32) {
        seen[0].push_back(out.colour[ROW + 20][0]);
        seen[1].push_back(out.colour[ROW + 21][0]);
      }
    }
    for (const auto& texel : seen) {
      const float mean = std::accumulate(texel.begin(), texel.end(), 0.f) / static_cast<float>(texel.size());
      const auto [lowest, highest] = std::ranges::minmax(texel);
      std::printf("1. flipping spark: mean %.4f (half the spark 5), spread %.1f%% of it\n", mean, 100.f * (highest - lowest) / mean);
      CHECK(highest - lowest <= 0.25f * mean);
      CHECK(std::abs(mean - 5.f) <= 0.25f);
    }
  }
  // 2. The moving spark.
  {
    ResolveRig rig;
    float least_there = 1e9f;
    float most_left = 0.f;
    for (uint64_t frame = 0; frame < 13; ++frame) {
      std::vector<LayerValue> values(LAYER_TEXELS);
      const size_t x = 6 + 4 * frame;
      values[ROW + x] = {10.f, 0.5f};
      const auto out = ResolveFrame(&rig, values, 0.f, 20.f, frame, frame == 0);
      if (frame >= 2) {
        least_there = std::min(least_there, out.colour[ROW + x][0]);
        most_left = std::max(most_left, out.colour[ROW + x - 4][0]);
      }
    }
    std::printf("2. moving spark (10): at least %.4f where it is, at most %.4f where it was\n", least_there, most_left);
    CHECK(least_there >= 9.f);
    CHECK(most_left <= 0.5f);
  }
  // 3. Panning: smoke fixed on the wall, its edge where it crosses half.
  {
    ResolveRig rig;
    const float a = (1.f / TanHalf()) * 64.f / 128.f;
    const float texel_metres = 2.f * (2.f / 128.f) * 20.f / a;
    const auto crossing = [&](const std::vector<Float4>& image) {
      for (UINT x = 0; x + 1 < LAYER_WIDTH; ++x) {
        const float left = image[ROW + x][0];
        const float right = image[ROW + x + 1][0];
        if (left < 2.f && right >= 2.f) return static_cast<float>(x) + (2.f - left) / (right - left);
      }
      return -1.f;
    };
    float worst = 0.f;
    for (uint64_t frame = 0; frame < 31; ++frame) {
      const float camera_x = static_cast<float>(frame) * 0.37f * texel_metres;
      std::vector<LayerValue> values(LAYER_TEXELS);
      for (UINT y = 0; y < LAYER_HEIGHT; ++y) {
        for (UINT x = 0; x < LAYER_WIDTH; ++x) {
          const float ndc_x = static_cast<float>(2 * x + 1) / 128.f * 2.f - 1.f;
          const float world_x = ndc_x * 20.f / a + camera_x;
          const float smoke = 1.f / (1.f + std::exp(-world_x / (1.5f * texel_metres)));
          values[static_cast<size_t>(y) * LAYER_WIDTH + x] = {4.f * smoke, 0.6f * smoke};
        }
      }
      const auto out = ResolveFrame(&rig, values, camera_x, 20.f, frame, frame == 0);
      if (frame >= 10) {
        const float now = crossing(out.input);
        const float shown = crossing(out.colour);
        CHECK(now >= 0.f && shown >= 0.f);
        worst = std::max(worst, std::abs(shown - now));
      }
    }
    std::printf("3. panning smoke (0.37 texels a frame): edge at most %.3f texels from this frame's\n", worst);
    CHECK(worst < 0.5f);
  }
  // 4. This frame's layer alone, to the bit; the control keeps some of the last frames.
  {
    std::mt19937 random(11);
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    std::vector<LayerValue> before(LAYER_TEXELS);
    for (auto& value : before) value = {4.f * unit(random), 0.8f * unit(random)};
    std::vector<LayerValue> after = before;
    for (auto& value : after) value.luminance *= 1.1f;
    struct Case {
      const char* name;
      float camera_x;
      float wall;
      uint64_t frame;
      bool restart;
      bool alone;
    };
    for (const Case& test : {Case{"100 m jump", 100.f, 20.f, 10, false, true}, Case{"20 m, all off screen", 20.f, 5.f, 10, false, true},
                             Case{"a frame left out", 0.f, 20.f, 11, false, true}, Case{"restart", 0.f, 20.f, 10, true, true},
                             Case{"control", 0.f, 20.f, 10, false, false}}) {
      ResolveRig rig;
      for (uint64_t frame = 0; frame < 10; ++frame) ResolveFrame(&rig, before, 0.f, test.wall, frame, frame == 0);
      const auto out = ResolveFrame(&rig, after, test.camera_x, test.wall, test.frame, test.restart);
      size_t same = 0;
      for (size_t index = 0; index < LAYER_TEXELS; ++index) {
        bool equal = std::memcmp(&out.colour[index], &out.input[index], sizeof(Float4)) == 0;
#ifdef PARTICLE_TEST_WH3
        equal = equal && out.extra[index][0] == out.input_optical_depth[index][0];
#endif
        equal = equal && out.extra[index][1] == 1.f;
        same += equal ? 1 : 0;
      }
      std::printf("4. %s: %zu of %zu texels this frame's to the bit\n", test.name, same, LAYER_TEXELS);
      CHECK(test.alone ? (same == LAYER_TEXELS) : (same < LAYER_TEXELS / 2));
    }
  }
}

#ifdef PARTICLE_TEST_WH3
// ---- Warhammer III: the composite through vfx_particles.hpp's callbacks.
namespace vfx = twwarhammer3::vfx_particles;
using IndirectContext = action::CommandContext<action::IndirectArguments>;

struct CompositeInputs {
  UINT width = 64;
  UINT height = 48;
  std::vector<float> full_depth;
  std::vector<float> half_depth;
  std::vector<Float4> full_optical_depth;  // the sharp layer's with the half one's added by nearest (A759AD5D)
  std::vector<Float4> full_colour;
  std::vector<Float4> half_colour;
  std::vector<Float4> ui_full;
  std::vector<Float4> ui_half;
  std::vector<Float4> half_optical_depth;
};
// The inputs as the game's textures.
struct CompositeTextures {
  UINT width = 0;
  UINT height = 0;
  Depth full_depth;  // the game's depth buffer
  frame_history::Target half_depth;
  frame_history::Target full_optical_depth;
  frame_history::Target half_optical_depth;
  frame_history::Target full_colour;
  frame_history::Target half_colour;
  frame_history::Target ui_full;
  frame_history::Target ui_half;
};
static CompositeTextures UploadComposite(const CompositeInputs& in) {
  const UINT half_width = in.width / 2;
  const UINT half_height = in.height / 2;
  return {.width = in.width,
          .height = in.height,
          .full_depth = MakeDepth(in.width, in.height, in.full_depth),
          .half_depth = Upload(DXGI_FORMAT_R32_FLOAT, half_width, half_height, Expand(in.half_depth)),
          .full_optical_depth = Upload(DXGI_FORMAT_R32_FLOAT, in.width, in.height, in.full_optical_depth),
          .half_optical_depth = Upload(DXGI_FORMAT_R32_FLOAT, half_width, half_height, in.half_optical_depth),
          .full_colour = Upload(DXGI_FORMAT_R16G16B16A16_FLOAT, in.width, in.height, in.full_colour),
          .half_colour = Upload(DXGI_FORMAT_R16G16B16A16_FLOAT, half_width, half_height, in.half_colour),
          .ui_full = Upload(DXGI_FORMAT_R16G16B16A16_FLOAT, in.width, in.height, in.ui_full),
          .ui_half = Upload(DXGI_FORMAT_R16G16B16A16_FLOAT, half_width, half_height, in.ui_half)};
}

struct CompositeOutputs {
  std::vector<Float4> o0;
  std::vector<Float4> o1;
  bool replaced = false;
};
struct CompositeRig {
  mock::Device owner;
  mock::CommandList commands;
  ComPtr<ID3D11PixelShader> game_shader;
  ComPtr<ID3D11SamplerState> sampler;
  ComPtr<ID3D11Buffer> camera;
  ComPtr<ID3D11DepthStencilState> no_depth;
};

struct GpuTimer;
static void Stamp(GpuTimer* timer, size_t index);

static CompositeOutputs RunComposite(CompositeRig* rig, bool smoothing, const CompositeTextures& in, bool read = true, GpuTimer* timer = nullptr,
                                     ID3D11DepthStencilView* depth_target = nullptr) {
  const UINT half_width = in.width / 2;
  const UINT half_height = in.height / 2;
  frame_history::Target o0;
  frame_history::Target o1;
  REQUIRE(frame_history::MakeTarget(device.Get(), in.width, in.height, DXGI_FORMAT_R32G32B32A32_FLOAT, &o0));
  REQUIRE(frame_history::MakeTarget(device.Get(), in.width, in.height, DXGI_FORMAT_R32G32B32A32_FLOAT, &o1));

  vfx::smoothing_row = smoothing ? 1.f : 0.f;
  vfx::OnPresent();
  // The two layers' moments draws (the sharp one's depth at full size, the half one's), then the moments' combine.
  IndirectContext moments;
  moments.cmd_list = &rig->commands;
  moments.matched_shader_hash = 0x713B947E;
  moments.arguments.draw_count = 1;
  const D3D11_VIEWPORT full_viewport = {0.f, 0.f, static_cast<float>(in.width), static_cast<float>(in.height), 0.f, 1.f};
  const D3D11_VIEWPORT half_viewport = {0.f, 0.f, static_cast<float>(half_width), static_cast<float>(half_height), 0.f, 1.f};
  for (const auto& [depth, viewport] : {std::pair{in.full_depth.srv.Get(), full_viewport}, std::pair{in.half_depth.srv.Get(), half_viewport}}) {
    context->PSSetShaderResources(4, 1, &depth);
    context->RSSetViewports(1, &viewport);
    const auto result = vfx::internal::OnParticleDraw(moments);
    CHECK(result.finalize_callback == nullptr && result.post_callback == nullptr && !result.bypass);
  }
  CHECK(vfx::internal::smoothing.layer_count == (smoothing ? 2u : 0u));
  DrawContext combine;
  combine.cmd_list = &rig->commands;
  combine.matched_shader_hash = 0xA759AD5D;
  context->PSSetShaderResources(0, 1, in.half_optical_depth.srv.GetAddressOf());
  CHECK(vfx::internal::OnMomentsCombine(combine).finalize_callback == nullptr);

  // The game's composite.
  const std::array<ID3D11RenderTargetView*, 2> targets = {o0.rtv.Get(), o1.rtv.Get()};
  context->OMSetRenderTargets(2, targets.data(), depth_target);
  context->RSSetViewports(1, &full_viewport);
  context->RSSetState(nullptr);
  context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
  context->OMSetDepthStencilState(rig->no_depth.Get(), 0);
  context->IASetInputLayout(nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(full_screen_vs.Get(), nullptr, 0);
  context->PSSetShader(rig->game_shader.Get(), nullptr, 0);
  const std::array<ID3D11ShaderResourceView*, 5> views = {in.full_optical_depth.srv.Get(), in.full_colour.srv.Get(), in.half_colour.srv.Get(),
                                                          in.ui_full.srv.Get(), in.ui_half.srv.Get()};
  context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
  context->PSSetSamplers(0, 1, rig->sampler.GetAddressOf());
  context->PSSetConstantBuffers(0, 1, rig->camera.GetAddressOf());
  DrawContext composite;
  composite.cmd_list = &rig->commands;
  composite.matched_shader_hash = 0x43606BB8;
  Stamp(timer, 0);
  const auto result = vfx::internal::OnComposite(composite);
  Stamp(timer, 1);
  ComPtr<ID3D11PixelShader> bound;
  context->PSGetShader(bound.GetAddressOf(), nullptr, nullptr);
  const bool replaced = result.finalize_callback != nullptr;
  CHECK(replaced == (bound.Get() != rig->game_shader.Get()));
  context->Draw(3, 0);
  Stamp(timer, 2);
  if (result.finalize_callback != nullptr) result.finalize_callback(composite, nullptr);
  // All as the game set it.
  bound.Reset();
  context->PSGetShader(bound.GetAddressOf(), nullptr, nullptr);
  CHECK(bound.Get() == rig->game_shader.Get());
  std::array<ID3D11ShaderResourceView*, particle_history::VIEW_COUNT> ours = {};
  context->PSGetShaderResources(particle_history::VIEWS_SLOT, particle_history::VIEW_COUNT, ours.data());
  std::array<ID3D11Buffer*, particle_history::CONSTANTS_COUNT> constants = {};
  context->PSGetConstantBuffers(particle_history::CONSTANTS_SLOT, particle_history::CONSTANTS_COUNT, constants.data());
  CHECK(std::ranges::all_of(ours, [](auto* view) { return view == nullptr; }));
  CHECK(std::ranges::all_of(constants, [](auto* buffer) { return buffer == nullptr; }));
  const std::array<ID3D11ShaderResourceView*, 5> none = {};
  context->PSSetShaderResources(0, static_cast<UINT>(none.size()), none.data());
  context->OMSetRenderTargets(0, nullptr, nullptr);
  if (!read) return {.replaced = replaced};
  return {Read(o0.srv.Get()), Read(o1.srv.Get()), replaced};
}

static CompositeRig MakeCompositeRig(const char* dump, UINT width, UINT height) {
  CompositeRig rig;
  rig.owner.native = device;
  rig.commands.owner = &rig.owner;
  rig.commands.native = context;
  std::ifstream file(std::string(dump) + "\\0x43606BB8.ps_5_0.cso", std::ios::binary);
  const std::vector<uint8_t> code{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  REQUIRE(!code.empty());
  REQUIRE(SUCCEEDED(device->CreatePixelShader(code.data(), code.size(), nullptr, rig.game_shader.GetAddressOf())));
  const D3D11_SAMPLER_DESC sampler = {.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                      .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                      .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX};
  REQUIRE(SUCCEEDED(device->CreateSamplerState(&sampler, rig.sampler.GetAddressOf())));
  const D3D11_DEPTH_STENCIL_DESC no_depth = {};
  REQUIRE(SUCCEEDED(device->CreateDepthStencilState(&no_depth, rig.no_depth.GetAddressOf())));
  rig.camera = MakeCamera({.render_width = static_cast<float>(width), .render_height = static_cast<float>(height),
                           .display_width = static_cast<float>(width), .display_height = static_cast<float>(height)});
  return rig;
}

static CompositeInputs RandomInputs(UINT width, UINT height, float distance, uint32_t seed) {
  std::mt19937 random(seed);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  const size_t full = static_cast<size_t>(width) * height;
  const size_t half = full / 4;
  CompositeInputs in = {.width = width, .height = height};
  in.full_depth.assign(full, DepthAt(distance));
  in.half_depth.assign(half, DepthAt(distance));
  in.half_optical_depth.assign(half, {0.35f, 0.f, 0.f, 1.f});
  for (size_t index = 0; index < full; ++index) {
    in.full_optical_depth.push_back({2.f * unit(random) + 0.35f, 0.f, 0.f, 1.f});
    in.full_colour.push_back({2.f * unit(random), 2.f * unit(random), 2.f * unit(random), unit(random)});
    in.ui_full.push_back({0.5f * unit(random), 0.5f * unit(random), 0.5f * unit(random), 0.3f * unit(random)});
  }
  for (size_t index = 0; index < half; ++index) {
    in.half_colour.push_back({3.f * unit(random), 3.f * unit(random), 3.f * unit(random), unit(random)});
    in.ui_half.push_back({0.5f * unit(random), 0.5f * unit(random), 0.5f * unit(random), 0.3f * unit(random)});
  }
  return in;
}

static void TestComposite(const char* dump) {
  constexpr UINT WIDTH = 64;
  constexpr UINT HEIGHT = 48;
  auto rig = MakeCompositeRig(dump, WIDTH, HEIGHT);
  // 5. Flat depth, the first frame, a uniform half zeroth moment.
  {
    const auto in = UploadComposite(RandomInputs(WIDTH, HEIGHT, 30.f, 5));
    const auto game = RunComposite(&rig, false, in);
    const auto ours = RunComposite(&rig, true, in);
    CHECK(!game.replaced && ours.replaced);
    float worst = 0.f;
    for (size_t index = 0; index < game.o0.size(); ++index) {
      for (int channel = 0; channel < 4; ++channel) {
        for (const auto* pair : {&game.o0, &game.o1}) {
          const auto& mine = (pair == &game.o0) ? ours.o0 : ours.o1;
          const float expected = (*pair)[index][channel];
          worst = std::max(worst, std::abs(mine[index][channel] - expected) / std::max(std::abs(expected), 1e-3f));
        }
      }
    }
    std::printf("5. composite, flat depth, first frame: ours from the game's at most %.2e (relative)\n", worst);
    CHECK(worst <= 1e-5f);
  }
  // 6. A near box (10 m, pixels x >= 33, rows 8-39) in front of smoke (30 m) the game drew into the half layer behind it.
  {
    const size_t full = static_cast<size_t>(WIDTH) * HEIGHT;
    CompositeInputs in = {.width = WIDTH, .height = HEIGHT};
    in.full_depth.resize(full);
    for (UINT y = 0; y < HEIGHT; ++y) {
      for (UINT x = 0; x < WIDTH; ++x) in.full_depth[y * WIDTH + x] = DepthAt((x >= 33 && y >= 8 && y < 40) ? 10.f : 50.f);
    }
    const float far_depth = DepthAt(50.f);
    for (UINT y = 0; y < HEIGHT / 2; ++y) {
      for (UINT x = 0; x < WIDTH / 2; ++x) {
        float farthest = 0.f;
        for (UINT tap = 0; tap < 4; ++tap) farthest = std::max(farthest, in.full_depth[(2 * y + (tap >> 1)) * WIDTH + 2 * x + (tap & 1)]);
        in.half_depth.push_back(farthest);
        const bool smoke = x >= 8 && x < 28 && y >= 6 && y < 20 && farthest == far_depth;
        in.half_colour.push_back(smoke ? Float4{1.5f, 1.2f, 0.9f, 0.8f} : Float4{});
        in.half_optical_depth.push_back({smoke ? 0.6f : 0.f, 0.f, 0.f, 1.f});
      }
    }
    for (UINT y = 0; y < HEIGHT; ++y) {
      for (UINT x = 0; x < WIDTH; ++x) in.full_optical_depth.push_back(in.half_optical_depth[(y / 2) * (WIDTH / 2) + x / 2]);
    }
    in.full_colour.assign(full, {});
    in.ui_full.assign(full, {});
    in.ui_half.assign(full / 4, {});
    const auto textures = UploadComposite(in);
    const auto game = RunComposite(&rig, false, textures);
    const auto ours = RunComposite(&rig, true, textures);
    float box_light = 0.f;
    float box_transmittance = 1.f;
    float game_box_light = 1e9f;
    float game_box_transmittance = 0.f;
    float smoke_difference = 0.f;
    float smoke_light = 0.f;
    for (UINT y = 14; y < 38; ++y) {
      for (UINT x = 18; x < 44; ++x) {
        const size_t index = y * WIDTH + x;
        if (x >= 33) {
          box_light = std::max({box_light, ours.o0[index][0], ours.o0[index][1], ours.o0[index][2]});
          box_transmittance = std::min(box_transmittance, ours.o0[index][3]);
          if (x <= 34) {
            game_box_light = std::min(game_box_light, game.o0[index][0]);
            game_box_transmittance = std::max(game_box_transmittance, game.o0[index][3]);
          }
        } else {
          smoke_light = std::max(smoke_light, game.o0[index][0]);
          for (int channel = 0; channel < 4; ++channel) {
            smoke_difference = std::max(smoke_difference, std::abs(ours.o0[index][channel] - game.o0[index][channel]));
          }
        }
      }
    }
    // The smoke's share on the box: of its optical depth (0.6) and of its light.
    const float optical_share = -std::log(box_transmittance) / 0.6f;
    const float light_share = box_light / smoke_light;
    std::printf("6. box edge: on the box ours %.2f%% of the smoke's optical depth and %.2f%% of its light; the game's next to the edge "
                "lets through %.3f and lights %.3f; smoke away from the edge ours from the game's %.2e\n",
                100.f * optical_share, 100.f * light_share, game_box_transmittance, game_box_light, smoke_difference);
    CHECK(optical_share <= 0.01f && light_share <= 0.01f);
    CHECK(game_box_transmittance <= 0.8f && game_box_light >= 0.2f);
    CHECK(smoke_difference <= 1e-4f);
  }
  // 7. Rows off (checked in RunComposite): the game's shader stays; nothing captured.
  {
    const auto in = UploadComposite(RandomInputs(WIDTH, HEIGHT, 30.f, 7));
    const auto game = RunComposite(&rig, false, in);
    CHECK(!game.replaced && vfx::internal::smoothing.layer_count == 0 && !vfx::internal::smoothing.half_optical_depth);
    std::printf("7. rows off: the game's composite, nothing captured\n");
    // The composite's depth target on the depth buffer the upsample reads: writable, the game's composite; read-only, ours.
    const auto clash = RunComposite(&rig, true, in, false, nullptr, in.full_depth.dsv.Get());
    const auto again = RunComposite(&rig, true, in, false, nullptr, in.full_depth.dsv.Get());
    const D3D11_DEPTH_STENCIL_VIEW_DESC read_only_desc = {.Format = DXGI_FORMAT_D32_FLOAT, .ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D,
                                                          .Flags = D3D11_DSV_READ_ONLY_DEPTH};
    ComPtr<ID3D11DepthStencilView> read_only;
    REQUIRE(SUCCEEDED(device->CreateDepthStencilView(in.full_depth.texture.Get(), &read_only_desc, read_only.GetAddressOf())));
    const auto shared = RunComposite(&rig, true, in, false, nullptr, read_only.Get());
    const size_t clash_lines = LogCount("the composite's depth target is a depth it reads");
    std::printf("7. depth target on the depth it reads: writable %s / %s, read-only %s; %zu log line\n", clash.replaced ? "ours" : "the game's",
                again.replaced ? "ours" : "the game's", shared.replaced ? "ours" : "the game's", clash_lines);
    CHECK(!clash.replaced && !again.replaced && shared.replaced && clash_lines == 1);
  }
  CHECK(LogCount("Smooth Half-Size Particles: the first composite smoothed") == 1);
}
#else
// ---- Attila: particle_layer.hpp's callbacks.
namespace layer = twattila::particle_layer;

constexpr char QUAD_VS[] = R"(
cbuffer quad : register(b2) { float4 rect; float4 colour; };
float4 main(uint id : SV_VertexID) : SV_Position { return float4(lerp(rect.xy, rect.zw, float2(id & 1, id >> 1)), 0.3, 1); })";
constexpr char QUAD_PS[] = R"(
cbuffer quad : register(b2) { float4 rect; float4 colour; };
float4 main(float4 position : SV_Position) : SV_Target { return colour; })";
constexpr char GRADIENT_PS[] = R"(
float4 main(float4 position : SV_Position) : SV_Target { return float4(0.2 + position.x / 64, 0.5, 1.0 - position.y / 48, 1); })";

struct Particle {
  std::array<int, 4> rect;  // render pixels: left, top, right, bottom (exclusive)
  Float4 colour;
  D3D11_BLEND source;
  D3D11_BLEND destination;
  D3D11_BLEND_OP operation = D3D11_BLEND_OP_ADD;
  UINT8 mask = D3D11_COLOR_WRITE_ENABLE_ALL;
};

struct AttilaRig {
  mock::Device owner;
  mock::CommandList commands;
  mock::Swapchain swapchain;
  UINT display_width = 64;
  UINT display_height = 48;
  D3D11_VIEWPORT viewport = {};
  frame_history::Target light;
  Depth scene;
  std::vector<float> camera_values;
  ComPtr<ID3D11Buffer> camera;
  ComPtr<ID3D11Buffer> sentinel;
  ComPtr<ID3D11Buffer> quad;
  ComPtr<ID3D11VertexShader> quad_vs;
  ComPtr<ID3D11PixelShader> quad_ps;
  ComPtr<ID3D11PixelShader> gradient_ps;
  ComPtr<ID3D11DepthStencilState> depth_test;
  ComPtr<ID3D11UnorderedAccessView> output_view;  // bound at the output stage's slot 1 with the particles when set
};

static AttilaRig MakeAttilaRig(UINT display_width, UINT display_height, UINT render_width, UINT render_height, const std::vector<float>& depth) {
  AttilaRig rig;
  rig.owner.native = device;
  rig.commands.owner = &rig.owner;
  rig.commands.native = context;
  rig.swapchain.owner = &rig.owner;
  rig.owner.back_buffer.type = reshade::api::resource_type::texture_2d;
  rig.owner.back_buffer.texture.width = display_width;
  rig.owner.back_buffer.texture.height = display_height;
  rig.display_width = display_width;
  rig.display_height = display_height;
  rig.viewport = {0.f, 0.f, static_cast<float>(render_width), static_cast<float>(render_height), 0.f, 1.f};
  REQUIRE(frame_history::MakeTarget(device.Get(), display_width, display_height, DXGI_FORMAT_R11G11B10_FLOAT, &rig.light));
  rig.scene = MakeDepth(display_width, display_height, depth);
  rig.camera_values = CameraValues({.render_width = static_cast<float>(render_width), .render_height = static_cast<float>(render_height),
                                    .display_width = static_cast<float>(display_width), .display_height = static_cast<float>(display_height)});
  rig.camera = MakeBuffer(rig.camera_values.data(), static_cast<UINT>(rig.camera_values.size() * sizeof(float)));
  const std::array<float, 8> nothing = {};
  rig.sentinel = MakeBuffer(nothing.data(), sizeof(nothing));
  rig.quad = MakeBuffer(nothing.data(), sizeof(nothing));
  rig.quad_vs = VertexShader(QUAD_VS);
  rig.quad_ps = PixelShader(QUAD_PS);
  rig.gradient_ps = PixelShader(GRADIENT_PS);
  const D3D11_DEPTH_STENCIL_DESC depth_test = {.DepthEnable = TRUE, .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO, .DepthFunc = D3D11_COMPARISON_LESS};
  REQUIRE(SUCCEEDED(device->CreateDepthStencilState(&depth_test, rig.depth_test.GetAddressOf())));
  return rig;
}

struct GpuTimer;
static void Stamp(GpuTimer* timer, size_t index);

// The particles as the game draws them (each through particle_layer.hpp's callback): whether each went into the layer.
static std::vector<bool> DrawParticles(AttilaRig* rig, std::span<const Particle> particles, GpuTimer* timer = nullptr) {
  std::vector<bool> layered;
  for (const Particle& particle : particles) {
    D3D11_BLEND_DESC blend_desc = {};
    blend_desc.RenderTarget[0] = {.BlendEnable = TRUE, .SrcBlend = particle.source, .DestBlend = particle.destination, .BlendOp = particle.operation,
                                  .SrcBlendAlpha = D3D11_BLEND_ONE, .DestBlendAlpha = D3D11_BLEND_ZERO, .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                                  .RenderTargetWriteMask = particle.mask};
    ComPtr<ID3D11BlendState> blend;
    REQUIRE(SUCCEEDED(device->CreateBlendState(&blend_desc, blend.GetAddressOf())));
    const float width = rig->viewport.Width;
    const float height = rig->viewport.Height;
    const std::array<float, 8> quad = {static_cast<float>(particle.rect[0]) / width * 2.f - 1.f, 1.f - static_cast<float>(particle.rect[1]) / height * 2.f,
                                       static_cast<float>(particle.rect[2]) / width * 2.f - 1.f, 1.f - static_cast<float>(particle.rect[3]) / height * 2.f,
                                       particle.colour[0], particle.colour[1], particle.colour[2], particle.colour[3]};
    context->UpdateSubresource(rig->quad.Get(), 0, nullptr, quad.data(), 0, 0);
    const UINT keep_count = UINT_MAX;
    context->OMSetRenderTargetsAndUnorderedAccessViews(1, rig->light.rtv.GetAddressOf(), rig->scene.dsv.Get(), 1, rig->output_view ? 1 : 0,
                                                       rig->output_view.GetAddressOf(), &keep_count);
    context->OMSetBlendState(blend.Get(), nullptr, 0xFFFFFFFFu);
    context->OMSetDepthStencilState(rig->depth_test.Get(), 0);
    context->RSSetViewports(1, &rig->viewport);
    context->RSSetState(nullptr);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(rig->quad_vs.Get(), nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->PSSetShader(rig->quad_ps.Get(), nullptr, 0);
    context->VSSetConstantBuffers(0, 1, rig->camera.GetAddressOf());
    context->GSSetConstantBuffers(0, 1, rig->camera.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, rig->camera.GetAddressOf());
    context->VSSetConstantBuffers(2, 1, rig->quad.GetAddressOf());
    context->PSSetConstantBuffers(2, 1, rig->quad.GetAddressOf());
    context->PSSetConstantBuffers(particle_history::CONSTANTS_SLOT, 1, rig->sentinel.GetAddressOf());
    DrawContext command;
    command.cmd_list = &rig->commands;
    command.matched_shader_hash = 0xA7F257B5;
    if (layered.empty()) Stamp(timer, 0);
    const auto result = layer::internal::OnParticleDraw(command);
    if (layered.empty()) Stamp(timer, 1);
    context->Draw(4, 0);
    if (result.finalize_callback != nullptr) result.finalize_callback(command, nullptr);
    layered.push_back(result.finalize_callback != nullptr);
    // The game's state as it set it.
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depth;
    context->OMGetRenderTargets(1, target.GetAddressOf(), depth.GetAddressOf());
    ComPtr<ID3D11BlendState> bound_blend;
    context->OMGetBlendState(bound_blend.GetAddressOf(), nullptr, nullptr);
    D3D11_VIEWPORT viewport = {};
    UINT count = 1;
    context->RSGetViewports(&count, &viewport);
    std::array<ComPtr<ID3D11Buffer>, 4> buffers;
    context->VSGetConstantBuffers(0, 1, buffers[0].GetAddressOf());
    context->GSGetConstantBuffers(0, 1, buffers[1].GetAddressOf());
    context->PSGetConstantBuffers(0, 1, buffers[2].GetAddressOf());
    context->PSGetConstantBuffers(particle_history::CONSTANTS_SLOT, 1, buffers[3].GetAddressOf());
    CHECK(target.Get() == rig->light.rtv.Get() && depth.Get() == rig->scene.dsv.Get() && bound_blend.Get() == blend.Get());
    CHECK(BoundOutputView(1).Get() == rig->output_view.Get());
    CHECK(count == 1 && std::memcmp(&viewport, &rig->viewport, sizeof(viewport)) == 0);
    CHECK(buffers[0].Get() == rig->camera.Get() && buffers[1].Get() == rig->camera.Get() && buffers[2].Get() == rig->camera.Get()
          && buffers[3].Get() == rig->sentinel.Get());
  }
  Stamp(timer, 2);
  return layered;
}

// A frame: the present before it, the scene's light (a gradient over the display), the particles, the light buffer's next reader
// (`reader`); the light buffer after it.
static std::vector<Float4> AttilaFrame(AttilaRig* rig, std::span<const Particle> particles, bool reader = true, std::vector<bool>* layered = nullptr,
                                       bool read = true, GpuTimer* timer = nullptr) {
  layer::Update(&rig->swapchain);
  DrawWith(rig->gradient_ps.Get(), rig->light.rtv.Get(), rig->display_width, rig->display_height, nullptr);
  const auto drawn = DrawParticles(rig, particles, timer);
  if (layered != nullptr) *layered = drawn;
  if (reader) {
    DrawContext command;
    command.cmd_list = &rig->commands;
    command.matched_shader_hash = 0x781681E4;
    CHECK(layer::internal::OnLayerReader(command).finalize_callback == nullptr);
  }
  Stamp(timer, 3);
  context->OMSetRenderTargets(0, nullptr, nullptr);
  return read ? Read(rig->light.srv.Get()) : std::vector<Float4>{};
}

static void TestLayer() {
  const std::array<Particle, 4> particles = {{
      {{8, 8, 56, 40}, {1.0f, 0.4f, 0.1f, 0.5f}, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA},
      {{16, 4, 48, 44}, {0.3f, 0.3f, 0.6f, 0.7f}, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, 0x7},
      {{4, 12, 60, 36}, {2.0f, 1.0f, 0.5f, 0.25f}, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, 0x7},
      {{20, 16, 44, 32}, {0.5f, 0.2f, 0.1f, 0.0f}, D3D11_BLEND_ONE, D3D11_BLEND_ONE},
  }};
  // 8. Through the layer against the blend math.
  {
    auto rig = MakeAttilaRig(64, 48, 64, 48, std::vector<float>(64 * 48, DepthAt(30.f)));
    layer::half_resolution_row = 0.f;
    const auto scene = AttilaFrame(&rig, {});
    const auto direct = AttilaFrame(&rig, particles);
    layer::half_resolution_row = 1.f;
    std::vector<bool> layered;
    const auto through_layer = AttilaFrame(&rig, particles, true, &layered);
    CHECK(std::ranges::all_of(layered, [](bool value) { return value; }));
    // 10. The layer's camera: the game's but c30 (32 x 24).
    {
      const auto bytes = ReadBuffer(layer::internal::runtime.objects.camera.Get());
      std::vector<float> copy(bytes.size() / sizeof(float));
      std::memcpy(copy.data(), bytes.data(), bytes.size());
      auto expected = rig.camera_values;
      expected[120] = 32.f;
      expected[121] = 24.f;
      expected[122] = 1.f / 32.f;
      expected[123] = 1.f / 24.f;
      CHECK(copy == expected);
      std::printf("10. the layer's camera: the game's but c30 (%g x %g)\n", copy[120], copy[121]);
    }
    const auto near_edge = [&](int x, int y) {
      return std::ranges::any_of(particles, [&](const Particle& particle) {
        const auto& r = particle.rect;
        return x == r[0] - 1 || x == r[0] || x == r[2] - 1 || x == r[2] || y == r[1] - 1 || y == r[1] || y == r[3] - 1 || y == r[3];
      });
    };
    constexpr std::array<float, 3> STEP = {1.f / 64.f, 1.f / 64.f, 1.f / 32.f};  // R11G11B10's mantissas
    float worst_layer = 0.f;
    float worst_direct = 0.f;
    size_t compared = 0;
    for (int y = 0; y < 48; ++y) {
      for (int x = 0; x < 64; ++x) {
        if (near_edge(x, y)) continue;
        const size_t index = static_cast<size_t>(y) * 64 + x;
        Float4 light = scene[index];
        for (const Particle& particle : particles) {
          const auto& r = particle.rect;
          if (x < r[0] || x >= r[2] || y < r[1] || y >= r[3]) continue;
          const float a = particle.colour[3];
          for (int channel = 0; channel < 3; ++channel) {
            const float source = particle.colour[channel] * ((particle.source == D3D11_BLEND_SRC_ALPHA) ? a : 1.f);
            light[channel] = source + light[channel] * ((particle.destination == D3D11_BLEND_INV_SRC_ALPHA) ? 1.f - a : 1.f);
          }
        }
        for (int channel = 0; channel < 3; ++channel) {
          const float allowed = STEP[channel] * std::max(light[channel], 0.05f);
          worst_layer = std::max(worst_layer, std::abs(through_layer[index][channel] - light[channel]) / allowed);
          worst_direct = std::max(worst_direct, std::abs(direct[index][channel] - light[channel]) / allowed);
        }
        ++compared;
      }
    }
    std::printf("8. blends through the layer: %zu pixels away from the edges, at most %.2f R11G11B10 steps from the blend math "
                "(the game's direct draws %.2f)\n", compared, worst_layer, worst_direct);
    CHECK(worst_layer <= 1.f);
    // 10. A static scene with Smooth Half-Size Particles: the first frame as without to the bit, then within a step.
    layer::smoothing_row = 1.f;
    const auto first = AttilaFrame(&rig, particles);
    CHECK(std::memcmp(first.data(), through_layer.data(), first.size() * sizeof(Float4)) == 0);
    float worst_static = 0.f;
    for (int frame = 0; frame < 4; ++frame) {
      const auto again = AttilaFrame(&rig, particles);
      for (size_t index = 0; index < again.size(); ++index) {
        for (int channel = 0; channel < 3; ++channel) {
          worst_static = std::max(worst_static, std::abs(again[index][channel] - through_layer[index][channel])
                                                    / (STEP[channel] * std::max(through_layer[index][channel], 0.05f)));
        }
      }
    }
    std::printf("10. static scene smoothed: first frame to the bit, the next 4 at most %.2f steps from without\n", worst_static);
    CHECK(worst_static <= 1.f);
    layer::smoothing_row = 0.f;
    // 8. Refused blends: full size, and the frame's later particles too; one log line.
    const std::array<Particle, 2> subtract_first = {{
        {{8, 8, 56, 40}, {1.0f, 0.4f, 0.1f, 0.5f}, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_OP_REV_SUBTRACT},
        particles[0],
    }};
    AttilaFrame(&rig, subtract_first, true, &layered);
    CHECK(!layered[0] && !layered[1]);
    const std::array<Particle, 1> destination_colour = {{{{8, 8, 56, 40}, {1.0f, 0.4f, 0.1f, 0.5f}, D3D11_BLEND_DEST_COLOR, D3D11_BLEND_ZERO}}};
    AttilaFrame(&rig, destination_colour, true, &layered);
    CHECK(!layered[0]);
    AttilaFrame(&rig, std::span(particles).first(1), true, &layered);
    CHECK(layered[0]);
    CHECK(LogCount("a particle blend it can't take") == 1);
    std::printf("8. subtract and destination-colour blends at full size (and the frame's later ones), one log line\n");
    // 10. An output-stage UAV with the particles: full size, the UAV and its counter as the game bound them.
    rig.output_view = MakeCounted();
    {
      const UINT seven = 7;
      context->OMSetRenderTargetsAndUnorderedAccessViews(1, rig.light.rtv.GetAddressOf(), nullptr, 1, 1, rig.output_view.GetAddressOf(), &seven);
    }
    AttilaFrame(&rig, particles, true, &layered);
    const bool uav_draws_full = std::ranges::none_of(layered, [](bool value) { return value; });
    CHECK(uav_draws_full);
    CHECK(LogCount("MSAA, a scissor or UAVs") == 1);
    const uint32_t count_after_draws = CounterOf(rig.output_view.Get());
    CHECK(count_after_draws == 7);
    // At the reader: the game's target at slot 0 and the UAV at slot 1 bound again after the composite (the layer's passes under
    // DrawState), its counter kept.
    const auto reader_view = rig.output_view;
    rig.output_view.Reset();
    frame_history::Target reader_target;
    REQUIRE(frame_history::MakeTarget(device.Get(), 32, 24, DXGI_FORMAT_R16G16B16A16_FLOAT, &reader_target));
    size_t reader_kept = 0;
    for (const float smoothing : {0.f, 1.f}) {
      layer::smoothing_row = smoothing;
      layer::Update(&rig.swapchain);
      DrawWith(rig.gradient_ps.Get(), rig.light.rtv.Get(), rig.display_width, rig.display_height, nullptr);
      const auto drawn = DrawParticles(&rig, std::span(particles).first(1));
      CHECK(drawn[0]);
      const UINT keep_count = UINT_MAX;
      context->OMSetRenderTargetsAndUnorderedAccessViews(1, reader_target.rtv.GetAddressOf(), nullptr, 1, 1, reader_view.GetAddressOf(), &keep_count);
      DrawContext reader;
      reader.cmd_list = &rig.commands;
      reader.matched_shader_hash = 0x781681E4;
      layer::internal::OnLayerReader(reader);
      ComPtr<ID3D11RenderTargetView> target;
      context->OMGetRenderTargets(1, target.GetAddressOf(), nullptr);
      const bool kept = target.Get() == reader_target.rtv.Get() && BoundOutputView(1).Get() == reader_view.Get() && CounterOf(reader_view.Get()) == 7;
      CHECK(kept);
      reader_kept += kept ? 1 : 0;
      CHECK(layer::internal::runtime.closed_frame == layer::internal::runtime.frame);
      context->OMSetRenderTargets(0, nullptr, nullptr);
    }
    layer::smoothing_row = 0.f;
    std::printf("10. an output-stage UAV: particles with it at full size (%s), counter %u; at the reader target and UAV bound again, "
                "counter kept (%zu of 2, smoothing off / on)\n", uav_draws_full ? "all" : "NOT all", count_after_draws, reader_kept);
    // 10. No reader: stopped at the next present (one log line), until the row goes off and on.
    AttilaFrame(&rig, particles, false, &layered);
    CHECK(layered[0]);
    AttilaFrame(&rig, particles, true, &layered);
    CHECK(!layered[0] && layer::internal::runtime.stopped);
    AttilaFrame(&rig, particles, true, &layered);
    CHECK(!layered[0]);
    layer::half_resolution_row = 0.f;
    AttilaFrame(&rig, {});
    layer::half_resolution_row = 1.f;
    AttilaFrame(&rig, particles, true, &layered);
    CHECK(layered[0]);
    CHECK(LogCount("nothing read the light buffer after the particles") == 1);
    std::printf("10. a layer no reader took: stopped (one log line) until the row went off and on\n");
    CHECK(LogCount("Particles at Half Resolution: the first layer") == 1);
    layer::half_resolution_row = 0.f;
    AttilaFrame(&rig, {});
  }
  // 9. At 66% (42 x 31 of 64 x 48): the layer's depth by the CPU's model, nothing written outside the render.
  {
    std::mt19937 random(9);
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    std::vector<float> depth(64 * 48);
    for (auto& value : depth) value = 0.95f + 0.0495f * unit(random);
    frame_history::render_width = 42;
    frame_history::render_height = 31;
    auto rig = MakeAttilaRig(64, 48, 42, 31, depth);
    const std::array<Particle, 1> cover = {{{{0, 0, 42, 31}, {1.0f, 0.5f, 0.25f, 0.5f}, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA}}};
    layer::half_resolution_row = 1.f;
    const auto scene = AttilaFrame(&rig, {});
    std::vector<bool> layered;
    const auto after = AttilaFrame(&rig, cover, true, &layered);
    CHECK(layered[0]);
    const auto& objects = layer::internal::runtime.objects;
    const auto layer_depth = Read(objects.depth_view.Get());
    const D3D11_VIEWPORT& layer_viewport = layer::internal::runtime.layer_viewport;
    CHECK(layer_viewport.Width == 21.f && layer_viewport.Height == 16.f);
    const float b = 1.f / TanHalf();
    const float a = b * 31.f / 42.f;
    const auto distance = [&](int x, int y) {
      const float u = (static_cast<float>(x) + 0.5f) * (1.f / 42.f);
      const float v = (static_cast<float>(y) + 0.5f) * (1.f / 31.f);
      const float d = depth[static_cast<size_t>(y) * 64 + x];
      const float w = d / DEPTH_B - DEPTH_A / DEPTH_B;
      const float px = (u * 2.f - 1.f) / a / w;
      const float py = (1.f - v * 2.f) / b / w;
      const float pz = 1.f / w;
      return std::sqrt(px * px + py * py + pz * pz);
    };
    size_t exact = 0;
    size_t ties = 0;
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 21; ++x) {
        float farthest = -1.f;
        float chosen = 0.f;
        for (int tap = 0; tap < 4; ++tap) {
          const int px = std::min(2 * x + (tap & 1), 41);
          const int py = std::min(2 * y + (tap >> 1), 30);
          const float here = distance(px, py);
          if (!(here <= farthest)) {
            farthest = here;
            chosen = depth[static_cast<size_t>(py) * 64 + px];
          }
        }
        const float made = layer_depth[static_cast<size_t>(y) * 32 + x][0];
        if (made == chosen) {
          ++exact;
        } else {
          // Another of the 2 x 2 within float rounding of the farthest.
          bool tie = false;
          for (int tap = 0; tap < 4; ++tap) {
            const int px = std::min(2 * x + (tap & 1), 41);
            const int py = std::min(2 * y + (tap >> 1), 30);
            tie = tie || (depth[static_cast<size_t>(py) * 64 + px] == made && std::abs(distance(px, py) - farthest) <= 1e-5f * farthest);
          }
          ties += tie ? 1 : 0;
        }
      }
    }
    size_t outside_changed = 0;
    size_t inside_changed = 0;
    for (int y = 0; y < 48; ++y) {
      for (int x = 0; x < 64; ++x) {
        const size_t index = static_cast<size_t>(y) * 64 + x;
        const bool same = std::memcmp(&scene[index], &after[index], sizeof(Float4)) == 0;
        if (x >= 42 || y >= 31) {
          outside_changed += same ? 0 : 1;
        } else {
          inside_changed += same ? 0 : 1;
        }
      }
    }
    std::printf("9. 66%%: layer depth %zu of 336 texels the CPU's farthest (%zu float ties); light changed in %zu of 1302 render pixels, "
                "%zu outside\n", exact, ties, inside_changed, outside_changed);
    CHECK(exact + ties == 21 * 16);
    CHECK(outside_changed == 0 && inside_changed == 42 * 31);
    layer::half_resolution_row = 0.f;
    AttilaFrame(&rig, {});
    frame_history::render_width = 0;
    frame_history::render_height = 0;
  }
}
#endif

// ---- --time: the passes on the hardware GPU at 4K (timestamps around them; median of 60 frames).
struct GpuTimer {
  ComPtr<ID3D11Query> disjoint;
  std::array<ComPtr<ID3D11Query>, 4> stamps;
  std::array<bool, 4> used = {};
  GpuTimer() {
    const D3D11_QUERY_DESC disjoint_desc = {.Query = D3D11_QUERY_TIMESTAMP_DISJOINT};
    REQUIRE(SUCCEEDED(device->CreateQuery(&disjoint_desc, disjoint.GetAddressOf())));
    const D3D11_QUERY_DESC stamp_desc = {.Query = D3D11_QUERY_TIMESTAMP};
    for (auto& stamp : stamps) REQUIRE(SUCCEEDED(device->CreateQuery(&stamp_desc, stamp.GetAddressOf())));
  }
  void Begin() {
    used = {};
    context->Begin(disjoint.Get());
  }
  // Milliseconds from stamp 0 to each stamp (negative: not stamped), or none when the clock jumped.
  std::array<double, 4> End() {
    context->End(disjoint.Get());
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock = {};
    while (context->GetData(disjoint.Get(), &clock, sizeof(clock), 0) != S_OK) {}
    std::array<double, 4> times = {-1.0, -1.0, -1.0, -1.0};
    if (clock.Disjoint) return times;
    std::array<UINT64, 4> ticks = {};
    for (size_t index = 0; index < stamps.size(); ++index) {
      if (!used[index]) continue;
      while (context->GetData(stamps[index].Get(), &ticks[index], sizeof(UINT64), 0) != S_OK) {}
      times[index] = static_cast<double>(ticks[index] - ticks[0]) * 1000.0 / static_cast<double>(clock.Frequency);
    }
    return times;
  }
};
static void Stamp(GpuTimer* timer, size_t index) {
  if (timer == nullptr) return;
  context->End(timer->stamps[index].Get());
  timer->used[index] = true;
}
static double Median(std::vector<double> values) {
  std::ranges::sort(values);
  return values.empty() ? 0.0 : values[values.size() / 2];
}

static void TimePasses(const char* dump) {
  MakeDevice(D3D_DRIVER_TYPE_HARDWARE);
  GpuTimer timer;
  constexpr int FRAMES = 60;
#ifdef PARTICLE_TEST_WH3
  auto rig = MakeCompositeRig(dump, 3840, 2160);
  const auto in = UploadComposite(RandomInputs(3840, 2160, 30.f, 3));
  for (const bool smoothing : {false, true}) {
    std::vector<double> resolve;
    std::vector<double> composite;
    for (int frame = 0; frame < FRAMES + 5; ++frame) {
      timer.Begin();
      RunComposite(&rig, smoothing, in, false, &timer);
      const auto times = timer.End();
      if (frame >= 5 && times[2] >= 0.0) {
        resolve.push_back(times[1]);
        composite.push_back(times[2] - times[1]);
      }
    }
    std::printf("time, 4K (half layer 1920 x 1080), %s: resolve %.3f ms, composite %.3f ms\n", smoothing ? "smoothed" : "the game's",
                Median(resolve), Median(composite));
  }
#else
  (void)dump;
  auto rig = MakeAttilaRig(3840, 2160, 3840, 2160, std::vector<float>(3840 * 2160, DepthAt(30.f)));
  std::vector<Particle> smoke(100, Particle{{0, 0, 3840, 2160}, {0.5f, 0.45f, 0.4f, 0.05f}, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA});
  for (const auto& [half, smoothing] : {std::pair{0.f, 0.f}, std::pair{1.f, 0.f}, std::pair{1.f, 1.f}}) {
    layer::half_resolution_row = half;
    layer::smoothing_row = smoothing;
    std::vector<double> open;
    std::vector<double> draws;
    std::vector<double> close;
    for (int frame = 0; frame < FRAMES + 5; ++frame) {
      timer.Begin();
      AttilaFrame(&rig, smoke, true, nullptr, false, &timer);
      const auto times = timer.End();
      if (frame >= 5 && times[3] >= 0.0) {
        open.push_back(times[1]);
        draws.push_back(times[2] - times[1]);
        close.push_back(times[3] - times[2]);
      }
    }
    std::printf("time, 4K, 100 full-screen particles, %s: layer opened %.3f ms, particles %.3f ms, layer in %.3f ms\n",
                half == 0.f ? "full size" : (smoothing == 0.f ? "half size" : "half size smoothed"), Median(open), Median(draws), Median(close));
  }
  layer::half_resolution_row = 0.f;
  layer::smoothing_row = 0.f;
  AttilaFrame(&rig, {}, true, nullptr, false);
#endif
}

int main(int argc, char** argv) {
  const char* dump = nullptr;
  bool time = false;
  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "--time") == 0) {
      time = true;
    } else {
      dump = argv[index];
    }
  }
  MakeDevice(D3D_DRIVER_TYPE_WARP);
#ifdef PARTICLE_TEST_WH3
  std::printf("Warhammer III\n");
  REQUIRE(dump != nullptr);
#else
  std::printf("Attila\n");
#endif
  TestResolve();
#ifdef PARTICLE_TEST_WH3
  TestComposite(dump);
#else
  TestLayer();
#endif
  if (time) TimePasses(dump);
  std::printf(failures == 0 ? "PASS\n" : "%d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
