// Rome II's replaced shaders (src/games/twrome2/shaders) on WARP.
// tests.exe <before .cso> <after .cso> <AO before .cso> <AO after .cso>: the water's screen-space reflections 7C9C8ECF
// (waterreflection00) and the ambient occlusion's resolve 1ED22F9C (ssao01, part 3), the build
// before this change and the one after it (run.ps1 compiles both with the build's flags), drawn as the game draws it: a
// full-screen quad over the reflection target (half the display) at the render scale's viewport, its TEXCOORD0 the
// viewport's 0 - 1 (helper_quad_vs), the display-size depth at t1, the scene copy at t0 (drawn whole), the water's height
// map at t2 (water on a checker of 8 m tiles). A synthetic camera 40 m above a lake at y 0 with its bed at y -2 and a
// wall 300 m ahead (150 m tall, 300 m wide); point samplers.
// Paths: the game's march (Reflection Method Game's), the hierarchical march (its min pyramid at t10), the rays' reflection
// (t47 at the render size, b10's x 1).
// 1. 100% (b12 unbound), Reflection Quality 1: after = before, bit for bit, every path. CONTROLs Quality 2 and 0.25
//    (the game's march) differ.
// 2. 50%: the display 2x, the depth's view in its top-left (the rest 1, far), the scene copy and target 2x, the viewport
//    the render's half, b12 (0.5, 0.5, 0.5, 0.5): after's view = after at 100% (game's march and rays bit for bit, the
//    hierarchical march allowed a few pixels: its pyramid has one level more). CONTROLs: before at 50%, after at 50% with
//    b12 unbound.
// 3. The game's HBAO resolve (ssao01, the replacement's game's code) over a 64 x 64 view of a bumpy depth (view depth 4 -
//    6 m through an identity inv_projection, the game's 4 directions x 4 steps, radius 4 m, a random jitter texture): with
//    rt_ambient_alone 0 after = before, bit for bit; with 1 (the rays' AO or the paths' in place of the game's, rt_scene
//    multiplying the traced visibility in after the pass) every channel of every pixel 1. CONTROL: before occludes (some
//    pixels below 1).
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
#include <string>
#include <vector>

#include "../../src/games/twrome2/shared.h"

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

// The display at 100%: the reflection target half of it.
constexpr UINT WIDTH = 128;
constexpr UINT HEIGHT = 64;
constexpr float NEAR_Z = 1.f;
constexpr float FAR_Z = 5000.f;
constexpr float SENTINEL = -7.f;

using Row = std::array<double, 4>;
using Matrix = std::array<Row, 4>;

static Matrix Multiply(const Matrix& a, const Matrix& b) {
  Matrix m = {};
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      for (int k = 0; k < 4; ++k) m[i][j] += a[i][k] * b[k][j];
    }
  }
  return m;
}

static Matrix Inverse(Matrix m) {
  Matrix r = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  for (int c = 0; c < 4; ++c) {
    int p = c;
    for (int i = c + 1; i < 4; ++i) {
      if (std::abs(m[i][c]) > std::abs(m[p][c])) p = i;
    }
    std::swap(m[c], m[p]);
    std::swap(r[c], r[p]);
    const double d = m[c][c];
    for (int j = 0; j < 4; ++j) {
      m[c][j] /= d;
      r[c][j] /= d;
    }
    for (int i = 0; i < 4; ++i) {
      if (i == c) continue;
      const double f = m[i][c];
      for (int j = 0; j < 4; ++j) {
        m[i][j] -= f * m[c][j];
        r[i][j] -= f * r[c][j];
      }
    }
  }
  return r;
}

static std::array<double, 4> Apply(const Matrix& m, const std::array<double, 4>& v) {
  std::array<double, 4> o = {};
  for (int i = 0; i < 4; ++i) {
    for (int k = 0; k < 4; ++k) o[i] += m[i][k] * v[k];
  }
  return o;
}

// The camera (column vectors: clip = projection x view x world), its depth 0 near.
struct Camera {
  Matrix view_projection;
  Matrix inverse;
  std::array<double, 3> position;
};

static Camera MakeCamera() {
  const std::array<double, 3> eye = {0.0, 40.0, -150.0};
  const std::array<double, 3> at = {0.0, 0.0, 60.0};
  std::array<double, 3> f = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
  const double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
  for (auto& c : f) c /= fl;
  std::array<double, 3> x = {f[2], 0.0, -f[0]};  // cross((0, 1, 0), f)
  const double xl = std::sqrt(x[0] * x[0] + x[2] * x[2]);
  for (auto& c : x) c /= xl;
  const std::array<double, 3> y = {f[1] * x[2] - f[2] * x[1], f[2] * x[0] - f[0] * x[2], f[0] * x[1] - f[1] * x[0]};
  const auto dot = [&](const std::array<double, 3>& a) { return a[0] * eye[0] + a[1] * eye[1] + a[2] * eye[2]; };
  const Matrix view = {{{x[0], x[1], x[2], -dot(x)}, {y[0], y[1], y[2], -dot(y)}, {f[0], f[1], f[2], -dot(f)}, {0, 0, 0, 1}}};
  const double sy = 1.0 / std::tan(0.5 * 0.9);
  const double sx = sy * HEIGHT / WIDTH;
  const double q = FAR_Z / (FAR_Z - NEAR_Z);
  const Matrix projection = {{{sx, 0, 0, 0}, {0, sy, 0, 0}, {0, 0, q, -NEAR_Z * q}, {0, 0, 1, 0}}};
  const Matrix vp = Multiply(projection, view);
  return {vp, Inverse(vp), eye};
}

// The view's depth at a texel of a w x h view: the lake bed (y -2) or the wall (z 150, |x| < 150, y < 150), else far.
static float SceneDepth(const Camera& camera, UINT px, UINT py, UINT w, UINT h) {
  const double ndc_x = (px + 0.5) / w * 2.0 - 1.0;
  const double ndc_y = 1.0 - (py + 0.5) / h * 2.0;
  auto n = Apply(camera.inverse, {ndc_x, ndc_y, 0.0, 1.0});
  auto fa = Apply(camera.inverse, {ndc_x, ndc_y, 1.0, 1.0});
  std::array<double, 3> a = {n[0] / n[3], n[1] / n[3], n[2] / n[3]};
  std::array<double, 3> b = {fa[0] / fa[3], fa[1] / fa[3], fa[2] / fa[3]};
  const std::array<double, 3> d = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  double t = 2.0;
  if (d[1] < 0.0) t = std::min(t, (-2.0 - a[1]) / d[1]);
  if (d[2] > 0.0) {
    const double tw = (150.0 - a[2]) / d[2];
    const double wx = a[0] + d[0] * tw;
    const double wy = a[1] + d[1] * tw;
    if (tw > 0.0 && std::abs(wx) < 150.0 && wy < 150.0 && wy > -2.0) t = std::min(t, tw);
  }
  if (t > 1.0) return 1.f;
  const auto clip = Apply(camera.view_projection, {a[0] + d[0] * t, a[1] + d[1] * t, a[2] + d[2] * t, 1.0});
  return static_cast<float>(clip[2] / clip[3]);
}

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

// A float4 (or, `single`, R32 with a min-mip chain: the hierarchical march's pyramid) texture's view from texels.
static ComPtr<ID3D11ShaderResourceView> View(UINT width, UINT height, const std::vector<std::array<float, 4>>& texels, bool pyramid = false) {
  UINT levels = 1;
  if (pyramid) {
    while ((std::max(width, height) >> levels) != 0) ++levels;
  }
  const D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = levels, .ArraySize = 1,
                                     .Format = pyramid ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT, .SampleDesc = {1, 0},
                                     .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
  std::vector<std::vector<float>> data(levels);
  std::vector<D3D11_SUBRESOURCE_DATA> initial(levels);
  UINT w = width;
  UINT h = height;
  for (UINT level = 0; level < levels; ++level) {
    if (!pyramid) {
      data[level].resize(size_t{w} * h * 4);
      std::memcpy(data[level].data(), texels.data(), data[level].size() * 4);
    } else if (level == 0) {
      for (const auto& t : texels) data[0].push_back(t[0]);
    } else {
      const UINT pw = std::max(1u, w * 2);
      const UINT ph = std::max(1u, h * 2);
      const auto& prev = data[level - 1];
      for (UINT y = 0; y < h; ++y) {
        for (UINT x = 0; x < w; ++x) {
          float m = 1.f;
          for (UINT dy = 0; dy < 2; ++dy) {
            for (UINT dx = 0; dx < 2; ++dx) {
              const UINT sx = std::min(x * 2 + dx, pw - 1);
              const UINT sy = std::min(y * 2 + dy, ph - 1);
              m = std::min(m, prev[size_t{sy} * pw + sx]);
            }
          }
          data[level].push_back(m);
        }
      }
    }
    initial[level] = {.pSysMem = data[level].data(), .SysMemPitch = w * (pyramid ? 4u : 16u)};
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
  }
  ComPtr<ID3D11Texture2D> texture;
  device->CreateTexture2D(&desc, initial.data(), &texture);
  ComPtr<ID3D11ShaderResourceView> view;
  device->CreateShaderResourceView(texture.Get(), nullptr, &view);
  return view;
}

enum class Path { GAME, HIERARCHICAL, RAYS };

static ComPtr<ID3D11VertexShader> quad;
static ComPtr<ID3D11SamplerState> point_clamp;

// The shader's output over its view: `scale` 1 (100%) or 2 (50%: the display, depth, scene copy and target 2x, the view in
// their top-left); `share` b12's value (null: unbound).
static std::vector<std::array<float, 4>> Run(const std::vector<char>& cso, Path path, UINT scale, const float* share, float quality) {
  ComPtr<ID3D11PixelShader> shader;
  CHECK(SUCCEEDED(device->CreatePixelShader(cso.data(), cso.size(), nullptr, &shader)), "the reflection shader");
  if (!shader) return {};
  static const Camera camera = MakeCamera();
  // The view at the render size (the 100% display): its depth; at 50% the display's top-left of it, the rest far.
  const UINT dw = WIDTH * scale;
  const UINT dh = HEIGHT * scale;
  std::vector<std::array<float, 4>> depth(size_t{dw} * dh, {1.f, 0.f, 0.f, 0.f});
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) depth[size_t{y} * dw + x] = {SceneDepth(camera, x, y, WIDTH, HEIGHT), 0.f, 0.f, 0.f};
  }
  // The scene copy (half the display, drawn whole): a pattern of the 100% copy's texels, each 2 x 2 at 50%.
  const UINT cw = dw / 2;
  const UINT ch = dh / 2;
  std::vector<std::array<float, 4>> scene(size_t{cw} * ch);
  for (UINT y = 0; y < ch; ++y) {
    for (UINT x = 0; x < cw; ++x) {
      const UINT i = x / scale;
      const UINT j = y / scale;
      scene[size_t{y} * cw + x] = {i / 64.f, j / 32.f, static_cast<float>((i * 7 + j * 3) % 13) / 13.f, 1.f};
    }
  }
  // The rays' reflection at the render size (the same at both scales).
  std::vector<std::array<float, 4>> rays(size_t{WIDTH} * HEIGHT);
  for (UINT y = 0; y < HEIGHT; ++y) {
    for (UINT x = 0; x < WIDTH; ++x) rays[size_t{y} * WIDTH + x] = {x / 128.f, y / 64.f, 0.5f, ((x + y) % 5) / 4.f};
  }
  const auto depth_view = View(dw, dh, depth);
  const auto scene_view = View(cw, ch, scene);
  // The water's height map over the playable area: water (w 1) on a checker of 8 m tiles, so the depth's world position
  // decides which pixels are water (the shader's first depth read).
  std::vector<std::array<float, 4>> heights(256 * 256);
  for (UINT y = 0; y < 256; ++y) {
    for (UINT x = 0; x < 256; ++x) heights[size_t{y} * 256 + x] = {0.f, 0.f, 0.f, static_cast<float>((x + y) % 2)};
  }
  const auto height_view = View(256, 256, heights);
  const auto pyramid_view = View(dw, dh, depth, true);
  const auto rays_view = View(WIDTH, HEIGHT, rays);

  // screen_space_refl_params: view_projection (c0-c3: each output's coefficients), its inverse (c4-c7), the camera and
  // the reflection height (c8), the water's heights, tiles, playable area (c9), quality (c10).
  std::array<float, 44> params = {};
  for (int j = 0; j < 4; ++j) {
    for (int k = 0; k < 4; ++k) {
      params[j * 4 + k] = static_cast<float>(camera.view_projection[j][k]);
      params[16 + j * 4 + k] = static_cast<float>(camera.inverse[j][k]);
    }
  }
  params[32] = static_cast<float>(camera.position[0]);
  params[33] = static_cast<float>(camera.position[1]);
  params[34] = static_cast<float>(camera.position[2]);
  params[36] = 0.f;  // min_water_height
  params[37] = 0.f;  // max_water_height
  params[38] = 16.f;
  params[39] = 2000.f;  // playable_area_size
  params[40] = 1.f;     // quality_factor
  ShaderInjectData injection = {};
  injection.ssr_quality = quality;
  injection.ssr_hierarchical = (path == Path::HIERARCHICAL) ? 1.f : 0.f;
  const std::array<uint32_t, 4> flags = {path == Path::RAYS ? 1u : 0u, 0u, 0u, 0u};
  const auto params_buffer = ConstantBuffer(params.data(), sizeof(params));
  const auto flags_buffer = ConstantBuffer(flags.data(), sizeof(flags));
  const auto share_buffer = (share != nullptr) ? ConstantBuffer(share, 16) : nullptr;
  const auto injection_buffer = ConstantBuffer(&injection, sizeof(injection));

  const D3D11_TEXTURE2D_DESC target_desc = {.Width = cw, .Height = ch, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                            .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target;
  device->CreateTexture2D(&target_desc, nullptr, &target);
  ComPtr<ID3D11RenderTargetView> target_view;
  device->CreateRenderTargetView(target.Get(), nullptr, &target_view);
  const float clear[4] = {SENTINEL, SENTINEL, SENTINEL, SENTINEL};
  context->ClearState();
  context->ClearRenderTargetView(target_view.Get(), clear);
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  // The render scale's viewport: the view's half-size reflection (WIDTH / 2 x HEIGHT / 2) at both scales.
  const D3D11_VIEWPORT viewport = {.Width = WIDTH / 2.f, .Height = HEIGHT / 2.f, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(quad.Get(), nullptr, 0);
  context->PSSetShader(shader.Get(), nullptr, 0);
  ID3D11Buffer* b0 = params_buffer.Get();
  context->PSSetConstantBuffers(0, 1, &b0);
  ID3D11Buffer* b10 = flags_buffer.Get();
  context->PSSetConstantBuffers(10, 1, &b10);
  ID3D11Buffer* b12_13[2] = {share_buffer.Get(), injection_buffer.Get()};
  context->PSSetConstantBuffers(12, 2, b12_13);
  ID3D11ShaderResourceView* t0_2[3] = {scene_view.Get(), depth_view.Get(), height_view.Get()};
  context->PSSetShaderResources(0, 3, t0_2);
  ID3D11ShaderResourceView* t10 = (path == Path::HIERARCHICAL) ? pyramid_view.Get() : nullptr;
  context->PSSetShaderResources(10, 1, &t10);
  ID3D11ShaderResourceView* t47 = (path == Path::RAYS) ? rays_view.Get() : nullptr;
  context->PSSetShaderResources(47, 1, &t47);
  ID3D11SamplerState* samplers[3] = {point_clamp.Get(), point_clamp.Get(), point_clamp.Get()};
  context->PSSetSamplers(0, 3, samplers);
  context->Draw(3, 0);

  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&staging_desc, nullptr, &staging);
  context->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<std::array<float, 4>> view(size_t{WIDTH / 2} * (HEIGHT / 2));
  for (UINT y = 0; y < HEIGHT / 2; ++y) {
    std::memcpy(&view[size_t{y} * (WIDTH / 2)], static_cast<const char*>(mapped.pData) + size_t{y} * mapped.RowPitch, (WIDTH / 2) * 16);
  }
  context->Unmap(staging.Get(), 0);
  return view;
}

// Pixels whose four values differ in any bit; `drawn`: those the shader wrote (not discarded) in `a`.
static int Differ(const std::vector<std::array<float, 4>>& a, const std::vector<std::array<float, 4>>& b, int* drawn = nullptr) {
  int count = 0;
  if (drawn != nullptr) *drawn = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
    if (std::memcmp(a[i].data(), b[i].data(), 16) != 0) ++count;
    if (drawn != nullptr && a[i][0] != SENTINEL && (a[i][3] > 0.f)) ++*drawn;
  }
  return a.size() == b.size() ? count : -1;
}

// Part 3: the HBAO resolve's bytes (`code`) over the view, shader_injection's rt_ambient_alone `alone`.
static std::vector<std::array<float, 4>> RunAmbient(const std::vector<char>& code, float alone) {
  constexpr UINT SIZE = 64;
  ComPtr<ID3D11PixelShader> shader;
  if (FAILED(device->CreatePixelShader(code.data(), code.size(), nullptr, &shader))) return {};
  // camera_VS_PS: inv_projection (c17-c20) identity, so the view depth is the texture's x; g_screen_size (c27).
  std::array<float, 35 * 4> camera = {};
  for (int row = 0; row < 4; ++row) camera[(17 + row) * 4 + row] = 1.f;
  camera[27 * 4 + 0] = SIZE;
  camera[27 * 4 + 1] = SIZE;
  camera[27 * 4 + 2] = 1.f / SIZE;
  camera[27 * 4 + 3] = 1.f / SIZE;
  camera[29 * 4 + 0] = SIZE;
  camera[29 * 4 + 1] = SIZE;
  // ssao_resolve_PS: steps, directions, radius, angle bias; attenuation, contrast, AO resolution; fov, strength, minimum,
  // jitter resolution.
  const std::array<float, 12> resolve = {4.f, 4.f, 4.f, 0.1f, 1.f, 1.f, SIZE, SIZE, 1.f, 1.f, 0.f, 4.f};
  ShaderInjectData injection = {};
  injection.rt_ambient_alone = alone;
  const auto camera_buffer = ConstantBuffer(camera.data(), sizeof(camera));
  const auto resolve_buffer = ConstantBuffer(resolve.data(), sizeof(resolve));
  const auto injection_buffer = ConstantBuffer(&injection, sizeof(injection));
  std::vector<std::array<float, 4>> depth(size_t{SIZE} * SIZE);
  std::vector<std::array<float, 4>> jitter(16);
  uint32_t seed = 12345u;
  const auto random = [&seed] {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / 16777216.f;
  };
  for (UINT y = 0; y < SIZE; ++y) {
    for (UINT x = 0; x < SIZE; ++x) {
      const float bump = ((x / 8 + y / 8) % 2 == 0) ? 0.f : 1.5f;
      depth[size_t{y} * SIZE + x] = {4.f + bump + 0.5f * std::sin(0.3f * x), 0.f, 0.f, 0.f};
    }
  }
  for (auto& texel : jitter) texel = {random(), random(), random(), random()};
  const auto depth_view = View(SIZE, SIZE, depth);
  const auto jitter_view = View(4, 4, jitter);
  const D3D11_TEXTURE2D_DESC target_desc = {.Width = SIZE, .Height = SIZE, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
                                            .SampleDesc = {1, 0}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_RENDER_TARGET};
  ComPtr<ID3D11Texture2D> target;
  device->CreateTexture2D(&target_desc, nullptr, &target);
  ComPtr<ID3D11RenderTargetView> target_view;
  device->CreateRenderTargetView(target.Get(), nullptr, &target_view);
  const float clear[4] = {SENTINEL, SENTINEL, SENTINEL, SENTINEL};
  context->ClearState();
  context->ClearRenderTargetView(target_view.Get(), clear);
  context->OMSetRenderTargets(1, target_view.GetAddressOf(), nullptr);
  const D3D11_VIEWPORT viewport = {.Width = SIZE, .Height = SIZE, .MaxDepth = 1.f};
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(quad.Get(), nullptr, 0);
  context->PSSetShader(shader.Get(), nullptr, 0);
  ID3D11Buffer* b0_1[2] = {camera_buffer.Get(), resolve_buffer.Get()};
  context->PSSetConstantBuffers(0, 2, b0_1);
  ID3D11Buffer* b13 = injection_buffer.Get();
  context->PSSetConstantBuffers(13, 1, &b13);
  ID3D11ShaderResourceView* t0_1[2] = {depth_view.Get(), jitter_view.Get()};
  context->PSSetShaderResources(0, 2, t0_1);
  ID3D11SamplerState* samplers[2] = {point_clamp.Get(), point_clamp.Get()};
  context->PSSetSamplers(0, 2, samplers);
  context->Draw(3, 0);
  D3D11_TEXTURE2D_DESC staging_desc = target_desc;
  staging_desc.Usage = D3D11_USAGE_STAGING;
  staging_desc.BindFlags = 0;
  staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  device->CreateTexture2D(&staging_desc, nullptr, &staging);
  context->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped = {};
  context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
  std::vector<std::array<float, 4>> view(size_t{SIZE} * SIZE);
  for (UINT y = 0; y < SIZE; ++y) {
    std::memcpy(&view[size_t{y} * SIZE], static_cast<const char*>(mapped.pData) + size_t{y} * mapped.RowPitch, SIZE * 16);
  }
  context->Unmap(staging.Get(), 0);
  return view;
}

int main(int argc, char** argv) {
  if (argc < 5) {
    std::printf("usage: tests.exe <before .cso> <after .cso> <AO before .cso> <AO after .cso>\n");
    return 2;
  }
  const auto before = ReadFile(argv[1]);
  const auto after = ReadFile(argv[2]);
  const auto ambient_before = ReadFile(argv[3]);
  const auto ambient_after = ReadFile(argv[4]);
  const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context))) return 1;
  // helper_quad_vs's: TEXCOORD0 the quad's 0 - 1 over the viewport (a full-screen triangle).
  const char* source =
      "struct O { float4 position : SV_Position; float2 uv : TEXCOORD0; };"
      "O main(uint id : SV_VertexID) { O o; o.uv = float2((id << 1) & 2, id & 2);"
      "o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }";
  ComPtr<ID3DBlob> code;
  if (FAILED(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &code, nullptr))) return 1;
  device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &quad);
  const D3D11_SAMPLER_DESC sampler = {.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT, .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
                                      .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP, .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
                                      .ComparisonFunc = D3D11_COMPARISON_NEVER, .MaxLOD = D3D11_FLOAT32_MAX};
  device->CreateSamplerState(&sampler, &point_clamp);

  const float half[4] = {0.5f, 0.5f, 0.5f, 0.5f};
  const char* names[3] = {"game's march", "hierarchical march", "rays"};
  for (const Path path : {Path::GAME, Path::HIERARCHICAL, Path::RAYS}) {
    const char* name = names[static_cast<int>(path)];
    int drawn = 0;
    const auto reference = Run(after, path, 1, nullptr, 1.f);
    const int same_100 = Differ(reference, Run(before, path, 1, nullptr, 1.f), &drawn);
    std::printf("%s, 100%%: after vs before, %d of %zu pixels differ (%d reflected)\n", name, same_100, reference.size(), drawn);
    CHECK(same_100 == 0 && drawn > 0, "%s at 100%%", name);
    const int at_50 = Differ(Run(after, path, 2, half, 1.f), reference);
    std::printf("%s, 50%%: after vs after at 100%%, %d differ\n", name, at_50);
    CHECK(path == Path::HIERARCHICAL ? (at_50 >= 0 && at_50 <= static_cast<int>(reference.size()) / 50) : at_50 == 0, "%s at 50%%", name);
    const int before_50 = Differ(Run(before, path, 2, half, 1.f), reference);
    const int unbound_50 = Differ(Run(after, path, 2, nullptr, 1.f), reference);
    std::printf("%s, CONTROL 50%%: before %d differ, after with b12 unbound %d differ\n", name, before_50, unbound_50);
    CHECK(before_50 > static_cast<int>(reference.size()) / 10 && unbound_50 > static_cast<int>(reference.size()) / 10, "%s CONTROL", name);
  }
  // Reflection Quality: the game's march's hits move by less than a step (the same scene texel at most pixels). The
  // hierarchical march's step cap (32 at least) is never reached on this 128-pixel view: printed only.
  for (const Path path : {Path::GAME, Path::HIERARCHICAL}) {
    const auto game = Run(before, path, 1, nullptr, 1.f);
    const int quality_2 = Differ(Run(after, path, 1, nullptr, 2.f), game);
    const int quality_quarter = Differ(Run(after, path, 1, nullptr, 0.25f), game);
    std::printf("%s, CONTROL Reflection Quality 2: %d differ, 0.25: %d differ\n", names[static_cast<int>(path)], quality_2, quality_quarter);
    CHECK(path != Path::GAME || (quality_2 > 0 && quality_quarter > 0), "Reflection Quality");
  }
  // Part 3: the HBAO resolve under the rays' AO.
  {
    const auto game = RunAmbient(ambient_before, 0.f);
    const auto kept = RunAmbient(ambient_after, 0.f);
    const auto alone = RunAmbient(ambient_after, 1.f);
    int open = 0;
    int occluded = 0;
    for (const auto& texel : alone) open += (texel == std::array<float, 4>{1.f, 1.f, 1.f, 1.f}) ? 1 : 0;
    for (const auto& texel : game) occluded += (texel[0] < 1.f && texel[0] != SENTINEL) ? 1 : 0;
    const int differ = Differ(kept, game);
    std::printf("HBAO resolve: rt_ambient_alone 0 after vs before, %d of %zu pixels differ; 1: %d of %zu every channel 1; CONTROL before "
                "occludes %d (min %.4f)\n",
                differ, game.size(), open, alone.size(), occluded,
                game.empty() ? 0.f : (*std::ranges::min_element(game, {}, [](const auto& texel) { return texel[0]; }))[0]);
    CHECK(!game.empty() && differ == 0, "HBAO resolve: rt_ambient_alone 0 is the game's code");
    CHECK(!alone.empty() && open == static_cast<int>(alone.size()), "HBAO resolve: rt_ambient_alone 1 leaves no AO of its own");
    CHECK(occluded > static_cast<int>(game.size()) / 10, "HBAO resolve CONTROL: the game's occludes");
  }
  std::printf(failures == 0 ? "PASS\n" : "FAILED (%d)\n", failures);
  return failures == 0 ? 0 : 1;
}
